/*
 * Copyright (c) 1997 - 2008 Kungliga Tekniska Högskolan
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

#ifndef HEIMDAL_SMALLER

static krb5_error_code
make_pa_enc_timestamp(krb5_context context, PA_DATA *pa,
		      krb5_enctype etype, krb5_keyblock *key)
{
    PA_ENC_TS_ENC p;
    unsigned char *buf;
    size_t buf_size;
    size_t len = 0;
    EncryptedData encdata;
    krb5_error_code ret;
    int32_t usec;
    int usec2;
    krb5_crypto crypto;

    krb5_us_timeofday (context, &p.patimestamp, &usec);
    usec2         = usec;
    p.pausec      = &usec2;

    ASN1_MALLOC_ENCODE(PA_ENC_TS_ENC, buf, buf_size, &p, &len, ret);
    if (ret)
	return ret;
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");
    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret) {
	free(buf);
	return ret;
    }
    ret = krb5_encrypt_EncryptedData(context,
				     crypto,
				     KRB5_KU_PA_ENC_TIMESTAMP,
				     buf,
				     len,
				     0,
				     &encdata);
    free(buf);
    krb5_crypto_destroy(context, crypto);
    if (ret)
	return ret;

    ASN1_MALLOC_ENCODE(EncryptedData, buf, buf_size, &encdata, &len, ret);
    free_EncryptedData(&encdata);
    if (ret)
	return ret;
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");
    pa->padata_type = KRB5_PADATA_ENC_TIMESTAMP;
    pa->padata_value.length = len;
    pa->padata_value.data = buf;
    return 0;
}

static krb5_error_code
add_padata(krb5_context context,
	   METHOD_DATA *md,
	   krb5_principal client,
	   krb5_key_proc key_proc,
	   krb5_const_pointer keyseed,
	   krb5_enctype *enctypes,
	   unsigned netypes,
	   krb5_salt *salt)
{
    krb5_error_code ret;
    PA_DATA *pa2;
    krb5_salt salt2;
    krb5_enctype *ep;
    size_t i;

    if(salt == NULL) {
	/* default to standard salt */
	ret = krb5_get_pw_salt (context, client, &salt2);
	if (ret)
	    return ret;
	salt = &salt2;
    }
    if (!enctypes) {
	enctypes = context->etypes;
	netypes = 0;
	for (ep = enctypes; *ep != ETYPE_NULL; ep++)
	    netypes++;
    }
    pa2 = realloc (md->val, (md->len + netypes) * sizeof(*md->val));
    if (pa2 == NULL) {
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    md->val = pa2;

    for (i = 0; i < netypes; ++i) {
	krb5_keyblock *key;

	ret = (*key_proc)(context, enctypes[i], *salt, keyseed, &key);
	if (ret)
	    continue;
	ret = make_pa_enc_timestamp (context, &md->val[md->len],
				     enctypes[i], key);
	krb5_free_keyblock (context, key);
	if (ret)
	    return ret;
	++md->len;
    }
    if(salt == &salt2)
	krb5_free_salt(context, salt2);
    return 0;
}

static krb5_error_code
init_as_req (krb5_context context,
	     KDCOptions opts,
	     krb5_creds *creds,
	     const krb5_addresses *addrs,
	     const krb5_enctype *etypes,
	     const krb5_preauthtype *ptypes,
	     const krb5_preauthdata *preauth,
	     krb5_key_proc key_proc,
	     krb5_const_pointer keyseed,
	     unsigned nonce,
	     AS_REQ *a)
{
    krb5_error_code ret;
    krb5_salt salt;

    memset(a, 0, sizeof(*a));

    a->pvno = 5;
    a->msg_type = krb_as_req;
    a->req_body.kdc_options = opts;
    a->req_body.cname = malloc(sizeof(*a->req_body.cname));
    if (a->req_body.cname == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto fail;
    }
    a->req_body.sname = malloc(sizeof(*a->req_body.sname));
    if (a->req_body.sname == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto fail;
    }
    ret = _krb5_principal2principalname (a->req_body.cname, creds->client);
    if (ret)
	goto fail;
    ret = _krb5_principal2principalname (a->req_body.sname, creds->server);
    if (ret)
	goto fail;
    ret = copy_Realm(&creds->client->realm, &a->req_body.realm);
    if (ret)
	goto fail;

    if(creds->times.starttime) {
	a->req_body.from = malloc(sizeof(*a->req_body.from));
	if (a->req_body.from == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	*a->req_body.from = creds->times.starttime;
    }
    if(creds->times.endtime){
	ALLOC(a->req_body.till, 1);
	*a->req_body.till = creds->times.endtime;
    }
    if(creds->times.renew_till){
	a->req_body.rtime = malloc(sizeof(*a->req_body.rtime));
	if (a->req_body.rtime == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	*a->req_body.rtime = creds->times.renew_till;
    }
    a->req_body.nonce = nonce;
    ret = _krb5_init_etype(context,
			   KRB5_PDU_AS_REQUEST,
			   &a->req_body.etype.len,
			   &a->req_body.etype.val,
			   etypes);
    if (ret)
	goto fail;

    /*
     * This means no addresses
     */

    if (addrs && addrs->len == 0) {
	a->req_body.addresses = NULL;
    } else {
	a->req_body.addresses = malloc(sizeof(*a->req_body.addresses));
	if (a->req_body.addresses == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}

	if (addrs)
	    ret = krb5_copy_addresses(context, addrs, a->req_body.addresses);
	else {
	    ret = krb5_get_all_client_addrs (context, a->req_body.addresses);
	    if(ret == 0 && a->req_body.addresses->len == 0) {
		free(a->req_body.addresses);
		a->req_body.addresses = NULL;
	    }
	}
	if (ret)
	    return ret;
    }

    a->req_body.enc_authorization_data = NULL;
    a->req_body.additional_tickets = NULL;

    if(preauth != NULL) {
	size_t i;
	ALLOC(a->padata, 1);
	if(a->padata == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	a->padata->val = NULL;
	a->padata->len = 0;
	for(i = 0; i < preauth->len; i++) {
	    if(preauth->val[i].type == KRB5_PADATA_ENC_TIMESTAMP){
		size_t j;

		for(j = 0; j < preauth->val[i].info.len; j++) {
		    krb5_salt *sp = &salt;
		    if(preauth->val[i].info.val[j].salttype)
			salt.salttype = *preauth->val[i].info.val[j].salttype;
		    else
			salt.salttype = KRB5_PW_SALT;
		    if(preauth->val[i].info.val[j].salt)
			salt.saltvalue = *preauth->val[i].info.val[j].salt;
		    else
			if(salt.salttype == KRB5_PW_SALT)
			    sp = NULL;
			else
			    krb5_data_zero(&salt.saltvalue);
		    ret = add_padata(context, a->padata, creds->client,
				     key_proc, keyseed,
				     &preauth->val[i].info.val[j].etype, 1,
				     sp);
		    if (ret == 0)
			break;
		}
	    }
	}
    } else
    /* not sure this is the way to use `ptypes' */
    if (ptypes == NULL || *ptypes == KRB5_PADATA_NONE)
	a->padata = NULL;
    else if (*ptypes ==  KRB5_PADATA_ENC_TIMESTAMP) {
	ALLOC(a->padata, 1);
	if (a->padata == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto fail;
	}
	a->padata->len = 0;
	a->padata->val = NULL;

	/* make a v5 salted pa-data */
	add_padata(context, a->padata, creds->client,
		   key_proc, keyseed, a->req_body.etype.val,
		   a->req_body.etype.len, NULL);

	/* make a v4 salted pa-data */
	salt.salttype = KRB5_PW_SALT;
	krb5_data_zero(&salt.saltvalue);
	add_padata(context, a->padata, creds->client,
		   key_proc, keyseed, a->req_body.etype.val,
		   a->req_body.etype.len, &salt);
    } else {
	ret = KRB5_PREAUTH_BAD_TYPE;
	krb5_set_error_message (context, ret,
				N_("pre-auth type %d not supported", ""),
			       *ptypes);
	goto fail;
    }
    return 0;
fail:
    free_AS_REQ(a);
    return ret;
}

static int
set_ptypes(krb5_context context,
	   KRB_ERROR *error,
	   const krb5_preauthtype **ptypes,
	   krb5_preauthdata **preauth)
{
    static krb5_preauthdata preauth2;
    static krb5_preauthtype ptypes2[] = { KRB5_PADATA_ENC_TIMESTAMP, KRB5_PADATA_NONE };

    if(error->e_data) {
	METHOD_DATA md;
	size_t i;
	decode_METHOD_DATA(error->e_data->data,
			   error->e_data->length,
			   &md,
			   NULL);
	for(i = 0; i < md.len; i++){
	    switch(md.val[i].padata_type){
	    case KRB5_PADATA_ENC_TIMESTAMP:
		*ptypes = ptypes2;
		break;
	    case KRB5_PADATA_ETYPE_INFO:
		*preauth = &preauth2;
		ALLOC_SEQ(*preauth, 1);
		(*preauth)->val[0].type = KRB5_PADATA_ENC_TIMESTAMP;
		decode_ETYPE_INFO(md.val[i].padata_value.data,
				  md.val[i].padata_value.length,
				  &(*preauth)->val[0].info,
				  NULL);
		break;
	    default:
		break;
	    }
	}
	free_METHOD_DATA(&md);
    } else {
	*ptypes = ptypes2;
    }
    return(1);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_cred(krb5_context context,
		 krb5_flags options,
		 const krb5_addresses *addrs,
		 const krb5_enctype *etypes,
		 const krb5_preauthtype *ptypes,
		 const krb5_preauthdata *preauth,
		 krb5_key_proc key_proc,
		 krb5_const_pointer keyseed,
		 krb5_decrypt_proc decrypt_proc,
		 krb5_const_pointer decryptarg,
		 krb5_creds *creds,
		 krb5_kdc_rep *ret_as_reply)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_error_code ret;
    AS_REQ a;
    krb5_kdc_rep rep;
    krb5_data req, resp;
    size_t len = 0;
    krb5_salt salt;
    krb5_keyblock *key;
    size_t size;
    KDCOptions opts;
    PA_DATA *pa;
    krb5_enctype etype;
    krb5_preauthdata *my_preauth = NULL;
    unsigned nonce;
    int done;

    opts = int2KDCOptions(options);

    krb5_generate_random_block (&nonce, sizeof(nonce));
    nonce &= 0xffffffff;

    do {
	done = 1;
	ret = init_as_req (context,
			   opts,
			   creds,
			   addrs,
			   etypes,
			   ptypes,
			   preauth,
			   key_proc,
			   keyseed,
			   nonce,
			   &a);
	if (my_preauth) {
	    free_ETYPE_INFO(&my_preauth->val[0].info);
	    free (my_preauth->val);
	    my_preauth = NULL;
	}
	if (ret)
	    return ret;

	ASN1_MALLOC_ENCODE(AS_REQ, req.data, req.length, &a, &len, ret);
	free_AS_REQ(&a);
	if (ret)
	    return ret;
	if(len != req.length)
	    krb5_abortx(context, "internal error in ASN.1 encoder");

	ret = krb5_sendto_kdc (context, &req, &creds->client->realm, &resp);
	krb5_data_free(&req);
	if (ret)
	    return ret;

	memset (&rep, 0, sizeof(rep));
	ret = decode_AS_REP(resp.data, resp.length, &rep.kdc_rep, &size);
	if(ret) {
	    /* let's try to parse it as a KRB-ERROR */
	    KRB_ERROR error;
	    int ret2;

	    ret2 = krb5_rd_error(context, &resp, &error);
	    if(ret2 && resp.data && ((char*)resp.data)[0] == 4)
		ret = KRB5KRB_AP_ERR_V4_REPLY;
	    krb5_data_free(&resp);
	    if (ret2 == 0) {
		ret = krb5_error_from_rd_error(context, &error, creds);
		/* if no preauth was set and KDC requires it, give it
                   one more try */
		if (!ptypes && !preauth
		    && ret == KRB5KDC_ERR_PREAUTH_REQUIRED
#if 0
			|| ret == KRB5KDC_ERR_BADOPTION
#endif
		    && set_ptypes(context, &error, &ptypes, &my_preauth)) {
		    done = 0;
		    preauth = my_preauth;
		    krb5_free_error_contents(context, &error);
		    krb5_clear_error_message(context);
		    continue;
		}
		if(ret_as_reply)
		    ret_as_reply->error = error;
		else
		    free_KRB_ERROR (&error);
		return ret;
	    }
	    return ret;
	}
	krb5_data_free(&resp);
    } while(!done);

    pa = NULL;
    etype = rep.kdc_rep.enc_part.etype;
    if(rep.kdc_rep.padata){
	int i = 0;
	pa = krb5_find_padata(rep.kdc_rep.padata->val, rep.kdc_rep.padata->len,
			      KRB5_PADATA_PW_SALT, &i);
	if(pa == NULL) {
	    i = 0;
	    pa = krb5_find_padata(rep.kdc_rep.padata->val,
				  rep.kdc_rep.padata->len,
				  KRB5_PADATA_AFS3_SALT, &i);
	}
    }
    if(pa) {
	salt.salttype = (krb5_salttype)pa->padata_type;
	salt.saltvalue = pa->padata_value;

	ret = (*key_proc)(context, etype, salt, keyseed, &key);
    } else {
	/* make a v5 salted pa-data */
	ret = krb5_get_pw_salt (context, creds->client, &salt);

	if (ret)
	    goto out;
	ret = (*key_proc)(context, etype, salt, keyseed, &key);
	krb5_free_salt(context, salt);
    }
    if (ret)
	goto out;

    {
	unsigned flags = EXTRACT_TICKET_TIMESYNC;
	if (opts.request_anonymous)
	    flags |= EXTRACT_TICKET_ALLOW_SERVER_MISMATCH;

	ret = _krb5_extract_ticket(context,
				   &rep,
				   creds,
				   key,
				   keyseed,
				   KRB5_KU_AS_REP_ENC_PART,
				   NULL,
				   nonce,
				   flags,
				   decrypt_proc,
				   decryptarg);
    }
    memset (key->keyvalue.data, 0, key->keyvalue.length);
    krb5_free_keyblock_contents (context, key);
    free (key);

out:
    if (ret == 0 && ret_as_reply)
	*ret_as_reply = rep;
    else
	krb5_free_kdc_rep (context, &rep);
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_in_tkt(krb5_context context,
		krb5_flags options,
		const krb5_addresses *addrs,
		const krb5_enctype *etypes,
		const krb5_preauthtype *ptypes,
		krb5_key_proc key_proc,
		krb5_const_pointer keyseed,
		krb5_decrypt_proc decrypt_proc,
		krb5_const_pointer decryptarg,
		krb5_creds *creds,
		krb5_ccache ccache,
		krb5_kdc_rep *ret_as_reply)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    krb5_error_code ret;

    ret = krb5_get_in_cred (context,
			    options,
			    addrs,
			    etypes,
			    ptypes,
			    NULL,
			    key_proc,
			    keyseed,
			    decrypt_proc,
			    decryptarg,
			    creds,
			    ret_as_reply);
    if(ret)
	return ret;
    if (ccache)
	ret = krb5_cc_store_cred (context, ccache, creds);
    return ret;
}

#endif /* HEIMDAL_SMALLER */
