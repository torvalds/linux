/*
 * Copyright (c) 1999 Kungliga Tekniska Högskolan
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
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* $Id$ */

#include <string.h>
#include <stdlib.h>
#include <krb5.h>

const char* check_length(krb5_context, krb5_principal, krb5_data *);

/* specify the api-version this library conforms to */

int version = 0;

/* just check the length of the password, this is what the default
   check does, but this lets you specify the minimum length in
   krb5.conf */
const char*
check_length(krb5_context context,
             krb5_principal prinipal,
             krb5_data *password)
{
    int min_length = krb5_config_get_int_default(context, NULL, 6,
						 "password_quality",
						 "min_length",
						 NULL);
    if(password->length < min_length)
	return "Password too short";
    return NULL;
}

#ifdef DICTPATH

/* use cracklib to check password quality; this requires a patch for
   cracklib that can be found at
   ftp://ftp.pdc.kth.se/pub/krb/src/cracklib.patch */

const char*
check_cracklib(krb5_context context,
	       krb5_principal principal,
	       krb5_data *password)
{
    char *s = malloc(password->length + 1);
    char *msg;
    char *strings[2];
    if(s == NULL)
	return NULL; /* XXX */
    strings[0] = principal->name.name_string.val[0]; /* XXX */
    strings[1] = NULL;
    memcpy(s, password->data, password->length);
    s[password->length] = '\0';
    msg = FascistCheck(s, DICTPATH, strings);
    memset(s, 0, password->length);
    free(s);
    return msg;
}
#endif
