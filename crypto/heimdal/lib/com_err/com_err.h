/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

/* $Id$ */

/* MIT compatible com_err library */

#ifndef __COM_ERR_H__
#define __COM_ERR_H__

#include <com_right.h>
#include <stdarg.h>

#if !defined(__GNUC__) && !defined(__attribute__)
#define __attribute__(X)
#endif

typedef void (KRB5_CALLCONV *errf) (const char *, long, const char *, va_list);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
error_message (long);

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
init_error_table (const char**, long, int);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
com_err_va (const char *, long, const char *, va_list)
    __attribute__((format(printf, 3, 0)));

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
com_err (const char *, long, const char *, ...)
    __attribute__((format(printf, 3, 4)));

KRB5_LIB_FUNCTION errf KRB5_LIB_CALL
set_com_err_hook (errf);

KRB5_LIB_FUNCTION errf KRB5_LIB_CALL
reset_com_err_hook (void);

KRB5_LIB_FUNCTION const char * KRB5_LIB_CALL
error_table_name  (int num);

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
add_to_error_table (struct et_list *new_table);

#endif /* __COM_ERR_H__ */
