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

#include <common.h>
RCSID("$Id$");

krb5_error_code
store_string(krb5_storage *sp, const char *str)
{
    size_t len = strlen(str) + 1;
    krb5_error_code ret;

    ret = krb5_store_int32(sp, len);
    if (ret)
	return ret;
    ret = krb5_storage_write(sp, str, len);
    if (ret != len)
	return EINVAL;
    return 0;
}

static void
add_list(char ****list, size_t *listlen, char **str, size_t len)
{
    size_t i;
    *list = erealloc(*list, sizeof(**list) * (*listlen + 1));

    (*list)[*listlen] = ecalloc(len, sizeof(**list));
    for (i = 0; i < len; i++)
	(*list)[*listlen][i] = str[i];
    (*listlen)++;
}

static void
permute(char ****list, size_t *listlen,
	char **str, const int start, const int len)
{
    int i, j;

#define SWAP(s,i,j) { char *t = str[i]; str[i] = str[j]; str[j] = t; }

    for (i = start; i < len - 1; i++) {
	for (j = i+1; j < len; j++) {
	    SWAP(str,i,j);
	    permute(list, listlen, str, i+1, len);
	    SWAP(str,i,j);
	}
    }
    add_list(list, listlen, str, len);
}

char ***
permutate_all(struct getarg_strings *strings, size_t *size)
{
    char **list, ***all = NULL;
    int i;

    *size = 0;

    list = ecalloc(strings->num_strings, sizeof(*list));
    for (i = 0; i < strings->num_strings; i++)
	list[i] = strings->strings[i];

    permute(&all, size, list, 0, strings->num_strings);
    free(list);
    return all;
}
