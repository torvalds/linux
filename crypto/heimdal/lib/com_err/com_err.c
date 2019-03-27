/*
 * Copyright (c) 1997 - 2002 Kungliga Tekniska Högskolan
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <roken.h>
#include "com_err.h"

struct et_list *_et_list = NULL;


KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
error_message (long code)
{
    static char msg[128];
    const char *p = com_right(_et_list, code);
    if (p == NULL) {
	if (code < 0)
	    snprintf(msg, sizeof(msg), "Unknown error %ld", code);
	else
	    p = strerror(code);
    }
    if (p != NULL && *p != '\0') {
	strlcpy(msg, p, sizeof(msg));
    } else
	snprintf(msg, sizeof(msg), "Unknown error %ld", code);
    return msg;
}

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
init_error_table(const char **msgs, long base, int count)
{
    initialize_error_table_r(&_et_list, msgs, count, base);
    return 0;
}

static void KRB5_CALLCONV
default_proc (const char *whoami, long code, const char *fmt, va_list args)
    __attribute__((__format__(__printf__, 3, 0)));

static void KRB5_CALLCONV
default_proc (const char *whoami, long code, const char *fmt, va_list args)
{
    if (whoami)
      fprintf(stderr, "%s: ", whoami);
    if (code)
      fprintf(stderr, "%s ", error_message(code));
    if (fmt)
      vfprintf(stderr, fmt, args);
    fprintf(stderr, "\r\n");	/* ??? */
}

static errf com_err_hook = default_proc;

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
com_err_va (const char *whoami,
	    long code,
	    const char *fmt,
	    va_list args)
{
    (*com_err_hook) (whoami, code, fmt, args);
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
com_err (const char *whoami,
	 long code,
	 const char *fmt,
	 ...)
{
    va_list ap;
    va_start(ap, fmt);
    com_err_va (whoami, code, fmt, ap);
    va_end(ap);
}

KRB5_LIB_FUNCTION errf KRB5_LIB_CALL
set_com_err_hook (errf new)
{
    errf old = com_err_hook;

    if (new)
	com_err_hook = new;
    else
	com_err_hook = default_proc;

    return old;
}

KRB5_LIB_FUNCTION errf KRB5_LIB_CALL
reset_com_err_hook (void)
{
    return set_com_err_hook(NULL);
}

#define ERRCODE_RANGE   8       /* # of bits to shift table number */
#define BITS_PER_CHAR   6       /* # bits to shift per character in name */

static const char char_set[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_";

static char buf[6];

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
error_table_name(int num)
{
    int ch;
    int i;
    char *p;

    /* num = aa aaa abb bbb bcc ccc cdd ddd d?? ??? ??? */
    p = buf;
    num >>= ERRCODE_RANGE;
    /* num = ?? ??? ??? aaa aaa bbb bbb ccc ccc ddd ddd */
    num &= 077777777;
    /* num = 00 000 000 aaa aaa bbb bbb ccc ccc ddd ddd */
    for (i = 4; i >= 0; i--) {
        ch = (num >> BITS_PER_CHAR * i) & ((1 << BITS_PER_CHAR) - 1);
        if (ch != 0)
            *p++ = char_set[ch-1];
    }
    *p = '\0';
    return(buf);
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
add_to_error_table(struct et_list *new_table)
{
    struct et_list *et;

    for (et = _et_list; et; et = et->next) {
	if (et->table->base == new_table->table->base)
	    return;
    }

    new_table->next = _et_list;
    _et_list = new_table;
}
