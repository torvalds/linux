/*
 * Copyright (c) 1997 - 2000 Kungliga Tekniska Högskolan
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

#ifndef __HPROP_H__
#define __HPROP_H__

#include "headers.h"

struct prop_data{
    krb5_context context;
    krb5_auth_context auth_context;
    int sock;
};

#define HPROP_VERSION "hprop-0.0"
#define HPROP_NAME "hprop"
#define HPROP_KEYTAB "HDB:"
#define HPROP_PORT 754

#ifndef NEVERDATE
#define NEVERDATE ((1U << 31) - 1)
#endif

krb5_error_code v5_prop(krb5_context, HDB*, hdb_entry_ex*, void*);
int mit_prop_dump(void*, const char*);

struct v4_principal {
    char name[64];
    char instance[64];
    DES_cblock key;
    int kvno;
    int mkvno;
    time_t exp_date;
    time_t mod_date;
    char mod_name[64];
    char mod_instance[64];
    int max_life;
};

int v4_prop(void*, struct v4_principal*);
int v4_prop_dump(void *arg, const char*);

#endif /* __HPROP_H__ */
