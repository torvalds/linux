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

#include "kadm5_locl.h"

RCSID("$Id$");

/*
 * free all the memory used by (len, keys)
 */

void
_kadm5_free_keys (krb5_context context,
		  int len, Key *keys)
{
    hdb_free_keys(context, len, keys);
}

/*
 * null-ify `len', `keys'
 */

void
_kadm5_init_keys (Key *keys, int len)
{
    int i;

    for (i = 0; i < len; ++i) {
	keys[i].mkvno               = NULL;
	keys[i].salt                = NULL;
	keys[i].key.keyvalue.length = 0;
	keys[i].key.keyvalue.data   = NULL;
    }
}

/*
 * return 1 if any key in `keys1, len1' exists in `keys2, len2'
 */

int
_kadm5_exists_keys(Key *keys1, int len1, Key *keys2, int len2)
{
    int i, j;

    for (i = 0; i < len1; ++i) {
	for (j = 0; j < len2; j++) {
	    if ((keys1[i].salt != NULL && keys2[j].salt == NULL)
		|| (keys1[i].salt == NULL && keys2[j].salt != NULL))
		continue;

	    if (keys1[i].salt != NULL) {
		if (keys1[i].salt->type != keys2[j].salt->type)
		    continue;
		if (keys1[i].salt->salt.length != keys2[j].salt->salt.length)
		    continue;
		if (memcmp (keys1[i].salt->salt.data, keys2[j].salt->salt.data,
			    keys1[i].salt->salt.length) != 0)
		    continue;
	    }
	    if (keys1[i].key.keytype != keys2[j].key.keytype)
		continue;
	    if (keys1[i].key.keyvalue.length != keys2[j].key.keyvalue.length)
		continue;
	    if (memcmp (keys1[i].key.keyvalue.data, keys2[j].key.keyvalue.data,
			keys1[i].key.keyvalue.length) != 0)
		continue;

	    return 1;
	}
    }
    return 0;
}
