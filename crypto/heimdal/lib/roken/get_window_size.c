/*
 * Copyright (c) 1995, 1996, 1997, 1998 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#if 0 /* Where were those needed? /confused */
#ifdef HAVE_SYS_PROC_H
#include <sys/proc.h>
#endif

#ifdef HAVE_SYS_TTY_H
#include <sys/tty.h>
#endif
#endif

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include "roken.h"

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
get_window_size(int fd, int *lines, int *columns)
{
    char *s;

#if defined(TIOCGWINSZ)
    {
	struct winsize ws;
        int ret;
	ret = ioctl(fd, TIOCGWINSZ, &ws);
	if (ret != -1) {
	    if (lines)
		*lines = ws.ws_row;
	    if (columns)
		*columns = ws.ws_col;
	    return 0;
	}
    }
#elif defined(TIOCGSIZE)
    {
	struct ttysize ts;
        int ret;
	ret = ioctl(fd, TIOCGSIZE, &ts);
	if (ret != -1) {
	    if (lines)
		*lines = ts.ws_lines;
	    if (columns)
		*columns = ts.ts_cols;
	    return 0;
 	}
    }
#elif defined(HAVE__SCRSIZE)
    {
	int dst[2];

 	_scrsize(dst);
	if (lines)
	    *lines = dst[1];
	if (columns)
	    *columns = dst[0];
	return 0;
    }
#elif defined(_WIN32)
    {
        intptr_t fh = 0;
        CONSOLE_SCREEN_BUFFER_INFO sb_info;

        fh = _get_osfhandle(fd);
        if (fh != (intptr_t) INVALID_HANDLE_VALUE &&
            GetConsoleScreenBufferInfo((HANDLE) fh, &sb_info)) {
            if (lines)
                *lines = 1 + sb_info.srWindow.Bottom - sb_info.srWindow.Top;
            if (columns)
                *columns = 1 + sb_info.srWindow.Right - sb_info.srWindow.Left;

            return 0;
        }
    }
#endif
    if (columns) {
    	if ((s = getenv("COLUMNS")))
	    *columns = atoi(s);
	else
	    return -1;
    }
    if (lines) {
	if ((s = getenv("LINES")))
	    *lines = atoi(s);
	else
	    return -1;
    }
    return 0;
}
