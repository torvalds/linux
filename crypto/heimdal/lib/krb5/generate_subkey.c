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

#include "krb5_locl.h"

/**
 * Generate subkey, from keyblock
 *
 * @param context kerberos context
 * @param key session key
 * @param etype encryption type of subkey, if ETYPE_NULL, use key's enctype
 * @param subkey returned new, free with krb5_free_keyblock().
 *
 * @return 0 on success or a Kerberos 5 error code
 *
* @ingroup krb5_crypto
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_generate_subkey_extended(krb5_context context,
			      const krb5_keyblock *key,
			      krb5_enctype etype,
			      krb5_keyblock **subkey)
{
    krb5_error_code ret;

    ALLOC(*subkey, 1);
    if (*subkey == NULL) {
	krb5_set_error_message(context, ENOMEM,N_("malloc: out of memory", ""));
	return ENOMEM;
    }

    if (etype == ETYPE_NULL)
	etype = key->keytype; /* use session key etype */

    /* XXX should we use the session key as input to the RF? */
    ret = krb5_generate_random_keyblock(context, etype, *subkey);
    if (ret != 0) {
	free(*subkey);
	*subkey = NULL;
    }

    return ret;
}

