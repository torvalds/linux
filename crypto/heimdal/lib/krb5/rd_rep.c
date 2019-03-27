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

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_rd_rep(krb5_context context,
	    krb5_auth_context auth_context,
	    const krb5_data *inbuf,
	    krb5_ap_rep_enc_part **repl)
{
    krb5_error_code ret;
    AP_REP ap_rep;
    size_t len;
    krb5_data data;
    krb5_crypto crypto;

    krb5_data_zero (&data);

    ret = decode_AP_REP(inbuf->data, inbuf->length, &ap_rep, &len);
    if (ret)
	return ret;
    if (ap_rep.pvno != 5) {
	ret = KRB5KRB_AP_ERR_BADVERSION;
	krb5_clear_error_message (context);
	goto out;
    }
    if (ap_rep.msg_type != krb_ap_rep) {
	ret = KRB5KRB_AP_ERR_MSG_TYPE;
	krb5_clear_error_message (context);
	goto out;
    }

    ret = krb5_crypto_init(context, auth_context->keyblock, 0, &crypto);
    if (ret)
	goto out;
    ret = krb5_decrypt_EncryptedData (context,
				      crypto,
				      KRB5_KU_AP_REQ_ENC_PART,
				      &ap_rep.enc_part,
				      &data);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	goto out;

    *repl = malloc(sizeof(**repl));
    if (*repl == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto out;
    }
    ret = decode_EncAPRepPart(data.data, data.length, *repl, &len);
    if (ret) {
	krb5_set_error_message(context, ret, N_("Failed to decode EncAPRepPart", ""));
	return ret;
    }

    if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_TIME) {
	if ((*repl)->ctime != auth_context->authenticator->ctime ||
	    (*repl)->cusec != auth_context->authenticator->cusec)
	{
	    krb5_free_ap_rep_enc_part(context, *repl);
	    *repl = NULL;
	    ret = KRB5KRB_AP_ERR_MUT_FAIL;
	    krb5_clear_error_message (context);
	    goto out;
	}
    }
    if ((*repl)->seq_number)
	krb5_auth_con_setremoteseqnumber(context, auth_context,
					 *((*repl)->seq_number));
    if ((*repl)->subkey)
	krb5_auth_con_setremotesubkey(context, auth_context, (*repl)->subkey);

 out:
    krb5_data_free (&data);
    free_AP_REP (&ap_rep);
    return ret;
}

KRB5_LIB_FUNCTION void KRB5_LIB_CALL
krb5_free_ap_rep_enc_part (krb5_context context,
			   krb5_ap_rep_enc_part *val)
{
    if (val) {
	free_EncAPRepPart (val);
	free (val);
    }
}
