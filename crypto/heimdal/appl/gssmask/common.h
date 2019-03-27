/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/*
 * pthread support is disable because the pthread
 * test have no "application pthread libflags" variable,
 * when this is fixed pthread support can be enabled again.
 */
#undef ENABLE_PTHREAD_SUPPORT

#include <sys/param.h>
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <assert.h>
#include <krb5.h>
#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>
#include <gssapi/gssapi_spnego.h>
#include <unistd.h>

#include <roken.h>
#include <getarg.h>

#include "protocol.h"

krb5_error_code store_string(krb5_storage *, const char *);


#define ret16(_client, num)					\
    do {							\
        if (krb5_ret_int16((_client)->sock, &(num)) != 0)	\
	    errx(1, "krb5_ret_int16 " #num);		\
    } while(0)

#define ret32(_client, num)					\
    do {							\
        if (krb5_ret_int32((_client)->sock, &(num)) != 0)	\
	    errx(1, "krb5_ret_int32 " #num);		\
    } while(0)

#define retdata(_client, data)					\
    do {							\
        if (krb5_ret_data((_client)->sock, &(data)) != 0)	\
	    errx(1, "krb5_ret_data " #data);		\
    } while(0)

#define retstring(_client, data)					\
    do {							\
        if (krb5_ret_string((_client)->sock, &(data)) != 0)	\
	    errx(1, "krb5_ret_data " #data);		\
    } while(0)


#define put32(_client, num)					\
    do {							\
        if (krb5_store_int32((_client)->sock, num) != 0)	\
	    errx(1, "krb5_store_int32 " #num);	\
    } while(0)

#define putdata(_client, data)					\
    do {							\
        if (krb5_store_data((_client)->sock, data) != 0)	\
	    errx(1, "krb5_store_data " #data);	\
    } while(0)

#define putstring(_client, str)					\
    do {							\
        if (store_string((_client)->sock, str) != 0)		\
	    errx(1, "krb5_store_str " #str);			\
    } while(0)

char *** permutate_all(struct getarg_strings *, size_t *);
