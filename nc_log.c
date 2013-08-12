/*
 * twemproxy - A fast and lightweight proxy for memcached protocol.
 * Copyright (C) 2011 Twitter, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <nc_core.h>

static struct logger logger;

int
log_init(int level, char *name)
{
    struct logger *l = &logger;

	//to make sure it in the suitable range
    l->level = MAX(LOG_EMERG, MIN(level, LOG_PVERB));
    l->name = name;
    if (name == NULL || !strlen(name)) {
        l->fd = STDERR_FILENO;
    } else {
		//try to open the log file
        l->fd = open(name, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (l->fd < 0) {
            log_stderr("opening log file '%s' failed: %s", name,
                       strerror(errno));
            return -1;
        }
    }

    return 0;
}

/**
 * @brief release the file descriptor
 *
 * @return  void 
 * @retval  0 means sucess 
 * @author luyao
 * @date 2013/08/11 15:55:01
**/
void
log_deinit(void)
{
    struct logger *l = &logger;

    if (l->fd < 0 || l->fd == STDERR_FILENO) {
        return;
    }

    close(l->fd);
}

/**
 * @brief what's the situation for this function?
 *
 * @return  void 
 * @retval  0 means sucess 
 * @author luyao
 * @date 2013/08/11 15:55:34
**/
void
log_reopen(void)
{
    struct logger *l = &logger;

    if (l->fd != STDERR_FILENO) {
        close(l->fd);
        l->fd = open(l->name, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (l->fd < 0) {
            log_stderr("reopening log file '%s' failed, ignored: %s", l->name,
                       strerror(errno));
        }
    }
}

/**
 * @brief the higher the level is, the more logs system will have
 *
 * @return  void 
 * @retval  0 means sucess 
 * @author luyao
 * @date 2013/08/11 15:56:10
**/
void
log_level_up(void)
{
    struct logger *l = &logger;

    if (l->level < LOG_PVERB) {
        l->level++;
        loga("up log level to %d", l->level);
    }
}

/**
 * @brief 越小的日志级别的重要度越高！！
 *
 * @return  void 
 * @retval  0 means sucess 
 * @author luyao
 * @date 2013/08/11 15:56:58
**/
void
log_level_down(void)
{
    struct logger *l = &logger;

    if (l->level > LOG_EMERG) {
        l->level--;
        loga("down log level to %d", l->level);
    }
}

/**
 * @brief 设置日志级别
 *
 * @param [in/out] level   : int
 * @return  void 
 * @retval  0 means sucess 
 * @author luyao
 * @date 2013/08/11 15:57:21
**/
void
log_level_set(int level)
{
    struct logger *l = &logger;

    l->level = MAX(LOG_EMERG, MIN(level, LOG_PVERB));
    loga("set log level to %d", l->level);
}

int
/**
 * @brief 检查是否应该记录log（当前的日志级别和设定的log级别）
 * 当前设定的level较高时，需要记录
 *
 * @param [in/out] level   : int
 * @author luyao
 * @date 2013/08/11 15:57:38
**/
log_loggable(int level)
{
    struct logger *l = &logger;

    if (level > l->level) {
        return 0;
    }

    return 1;
}

void
_log(const char *file, int line, int panic, const char *fmt, ...)
{
    struct logger *l = &logger;
    int len, size, errno_save;
    char buf[LOG_MAX_LEN], *timestr;
    va_list args;
    struct tm *local;
    time_t t;
    ssize_t n;

    if (l->fd < 0) {
        return;
    }

	///<save the errno
    errno_save = errno;
    len = 0;            /* length of output buffer */
    size = LOG_MAX_LEN; /* size of output buffer */

    t = time(NULL);
    local = localtime(&t);
    timestr = asctime(local);

	//here, we need to know something about the %.*s
	//输出的时候用于控制宽度,why?
	//输入的时候用于忽略掉某些东西
	//这里对len没有做任何控制！！！
	//so size-len可能溢出，此时，如果args中的数据很长，那么，内存一定会被写坏！
	//trigger:命名一个名字足够长的log文件 > 256Byte
	//传入一条足够长的日志
    len += nc_scnprintf(buf + len, size - len, "[%.*s] %s:%d ",
                        strlen(timestr) - 1, timestr, file, line);

    va_start(args, fmt);
    len += nc_vscnprintf(buf + len, size - len, fmt, args);
    va_end(args);

    buf[len++] = '\n';

	///<call write
    n = nc_write(l->fd, buf, len);
    if (n < 0) {
        l->nerror++;
    }

	//函数内的函数调用可能产生errno，全部忽略掉
	//优点？
    errno = errno_save;

	//so, the meaning of panic is the last words in the log?
	//临终遗言
    if (panic) {
        abort();
    }
}

void
_log_stderr(const char *fmt, ...)
{
    struct logger *l = &logger;
    int len, size, errno_save;
    char buf[4 * LOG_MAX_LEN];
    va_list args;
    ssize_t n;

    errno_save = errno;
    len = 0;                /* length of output buffer */
    size = 4 * LOG_MAX_LEN; /* size of output buffer */

    va_start(args, fmt);
    len += nc_vscnprintf(buf, size, fmt, args);
    va_end(args);

    buf[len++] = '\n';

    n = nc_write(STDERR_FILENO, buf, len);
    if (n < 0) {
        l->nerror++;
    }

    errno = errno_save;
}

/*
 * Hexadecimal dump in the canonical hex + ascii display
 * See -C option in man hexdump
 */
void
_log_hexdump(const char *file, int line, char *data, int datalen,
             const char *fmt, ...)
{
    struct logger *l = &logger;
    char buf[8 * LOG_MAX_LEN];
    int i, off, len, size, errno_save;
    ssize_t n;

    if (l->fd < 0) {
        return;
    }

    /* log hexdump */
    errno_save = errno;
    off = 0;                  /* data offset */
    len = 0;                  /* length of output buffer */
    size = 8 * LOG_MAX_LEN;   /* size of output buffer */

    while (datalen != 0 && (len < size - 1)) {
        char *save, *str;
        unsigned char c;
        int savelen;

        len += nc_scnprintf(buf + len, size - len, "%08x  ", off);

        save = data;
        savelen = datalen;

        for (i = 0; datalen != 0 && i < 16; data++, datalen--, i++) {
            c = (unsigned char)(*data);
            str = (i == 7) ? "  " : " ";
            len += nc_scnprintf(buf + len, size - len, "%02x%s", c, str);
        }
        for ( ; i < 16; i++) {
            str = (i == 7) ? "  " : " ";
            len += nc_scnprintf(buf + len, size - len, "  %s", str);
        }

        data = save;
        datalen = savelen;

        len += nc_scnprintf(buf + len, size - len, "  |");

        for (i = 0; datalen != 0 && i < 16; data++, datalen--, i++) {
            c = (unsigned char)(isprint(*data) ? *data : '.');
            len += nc_scnprintf(buf + len, size - len, "%c", c);
        }
        len += nc_scnprintf(buf + len, size - len, "|\n");

        off += 16;
    }

    n = nc_write(l->fd, buf, len);
    if (n < 0) {
        l->nerror++;
    }

    errno = errno_save;
}
