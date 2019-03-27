/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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
 * Free ticket and content
 *
 * @param context a Kerberos 5 context
 * @param ticket ticket to free
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_ticket(krb5_context context,
		 krb5_ticket *ticket)
{
    free_EncTicketPart(&ticket->ticket);
    krb5_free_principal(context, ticket->client);
    krb5_free_principal(context, ticket->server);
    free(ticket);
    return 0;
}

/**
 * Copy ticket and content
 *
 * @param context a Kerberos 5 context
 * @param from ticket to copy
 * @param to new copy of ticket, free with krb5_free_ticket()
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_ticket(krb5_context context,
		 const krb5_ticket *from,
		 krb5_ticket **to)
{
    krb5_error_code ret;
    krb5_ticket *tmp;

    *to = NULL;
    tmp = malloc(sizeof(*tmp));
    if(tmp == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    if((ret = copy_EncTicketPart(&from->ticket, &tmp->ticket))){
	free(tmp);
	return ret;
    }
    ret = krb5_copy_principal(context, from->client, &tmp->client);
    if(ret){
	free_EncTicketPart(&tmp->ticket);
	free(tmp);
	return ret;
    }
    ret = krb5_copy_principal(context, from->server, &tmp->server);
    if(ret){
	krb5_free_principal(context, tmp->client);
	free_EncTicketPart(&tmp->ticket);
	free(tmp);
	return ret;
    }
    *to = tmp;
    return 0;
}

/**
 * Return client principal in ticket
 *
 * @param context a Kerberos 5 context
 * @param ticket ticket to copy
 * @param client client principal, free with krb5_free_principal()
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_client(krb5_context context,
		       const krb5_ticket *ticket,
		       krb5_principal *client)
{
    return krb5_copy_principal(context, ticket->client, client);
}

/**
 * Return server principal in ticket
 *
 * @param context a Kerberos 5 context
 * @param ticket ticket to copy
 * @param server server principal, free with krb5_free_principal()
 *
 * @return Returns 0 to indicate success.  Otherwise an kerberos et
 * error code is returned, see krb5_get_error_message().
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_server(krb5_context context,
		       const krb5_ticket *ticket,
		       krb5_principal *server)
{
    return krb5_copy_principal(context, ticket->server, server);
}

/**
 * Return end time of ticket
 *
 * @param context a Kerberos 5 context
 * @param ticket ticket to copy
 *
 * @return end time of ticket
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION time_t KRB5_LIB_CALL
krb5_ticket_get_endtime(krb5_context context,
			const krb5_ticket *ticket)
{
    return ticket->ticket.endtime;
}

/**
 * Get the flags from the Kerberos ticket
 *
 * @param context Kerberos context
 * @param ticket Kerberos ticket
 *
 * @return ticket flags
 *
 * @ingroup krb5_ticket
 */
KRB5_LIB_FUNCTION unsigned long KRB5_LIB_CALL
krb5_ticket_get_flags(krb5_context context,
		      const krb5_ticket *ticket)
{
    return TicketFlags2int(ticket->ticket.flags);
}

static int
find_type_in_ad(krb5_context context,
		int type,
		krb5_data *data,
		krb5_boolean *found,
		krb5_boolean failp,
		krb5_keyblock *sessionkey,
		const AuthorizationData *ad,
		int level)
{
    krb5_error_code ret = 0;
    size_t i;

    if (level > 9) {
	ret = ENOENT; /* XXX */
	krb5_set_error_message(context, ret,
			       N_("Authorization data nested deeper "
				  "then %d levels, stop searching", ""),
			       level);
	goto out;
    }

    /*
     * Only copy out the element the first time we get to it, we need
     * to run over the whole authorization data fields to check if
     * there are any container clases we need to care about.
     */
    for (i = 0; i < ad->len; i++) {
	if (!*found && ad->val[i].ad_type == type) {
	    ret = der_copy_octet_string(&ad->val[i].ad_data, data);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("malloc: out of memory", ""));
		goto out;
	    }
	    *found = TRUE;
	    continue;
	}
	switch (ad->val[i].ad_type) {
	case KRB5_AUTHDATA_IF_RELEVANT: {
	    AuthorizationData child;
	    ret = decode_AuthorizationData(ad->val[i].ad_data.data,
					   ad->val[i].ad_data.length,
					   &child,
					   NULL);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("Failed to decode "
					  "IF_RELEVANT with %d", ""),
				       (int)ret);
		goto out;
	    }
	    ret = find_type_in_ad(context, type, data, found, FALSE,
				  sessionkey, &child, level + 1);
	    free_AuthorizationData(&child);
	    if (ret)
		goto out;
	    break;
	}
#if 0 /* XXX test */
	case KRB5_AUTHDATA_KDC_ISSUED: {
	    AD_KDCIssued child;

	    ret = decode_AD_KDCIssued(ad->val[i].ad_data.data,
				      ad->val[i].ad_data.length,
				      &child,
				      NULL);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("Failed to decode "
					  "AD_KDCIssued with %d", ""),
				       ret);
		goto out;
	    }
	    if (failp) {
		krb5_boolean valid;
		krb5_data buf;
		size_t len;

		ASN1_MALLOC_ENCODE(AuthorizationData, buf.data, buf.length,
				   &child.elements, &len, ret);
		if (ret) {
		    free_AD_KDCIssued(&child);
		    krb5_clear_error_message(context);
		    goto out;
		}
		if(buf.length != len)
		    krb5_abortx(context, "internal error in ASN.1 encoder");

		ret = krb5_c_verify_checksum(context, sessionkey, 19, &buf,
					     &child.ad_checksum, &valid);
		krb5_data_free(&buf);
		if (ret) {
		    free_AD_KDCIssued(&child);
		    goto out;
		}
		if (!valid) {
		    krb5_clear_error_message(context);
		    ret = ENOENT;
		    free_AD_KDCIssued(&child);
		    goto out;
		}
	    }
	    ret = find_type_in_ad(context, type, data, found, failp, sessionkey,
				  &child.elements, level + 1);
	    free_AD_KDCIssued(&child);
	    if (ret)
		goto out;
	    break;
	}
#endif
	case KRB5_AUTHDATA_AND_OR:
	    if (!failp)
		break;
	    ret = ENOENT; /* XXX */
	    krb5_set_error_message(context, ret,
				   N_("Authorization data contains "
				      "AND-OR element that is unknown to the "
				      "application", ""));
	    goto out;
	default:
	    if (!failp)
		break;
	    ret = ENOENT; /* XXX */
	    krb5_set_error_message(context, ret,
				   N_("Authorization data contains "
				      "unknown type (%d) ", ""),
				   ad->val[i].ad_type);
	    goto out;
	}
    }
out:
    if (ret) {
	if (*found) {
	    krb5_data_free(data);
	    *found = 0;
	}
    }
    return ret;
}

/**
 * Extract the authorization data type of type from the ticket. Store
 * the field in data. This function is to use for kerberos
 * applications.
 *
 * @param context a Kerberos 5 context
 * @param ticket Kerberos ticket
 * @param type type to fetch
 * @param data returned data, free with krb5_data_free()
 *
 * @ingroup krb5
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_ticket_get_authorization_data_type(krb5_context context,
					krb5_ticket *ticket,
					int type,
					krb5_data *data)
{
    AuthorizationData *ad;
    krb5_error_code ret;
    krb5_boolean found = FALSE;

    krb5_data_zero(data);

    ad = ticket->ticket.authorization_data;
    if (ticket->ticket.authorization_data == NULL) {
	krb5_set_error_message(context, ENOENT,
			       N_("Ticket have not authorization data", ""));
	return ENOENT; /* XXX */
    }

    ret = find_type_in_ad(context, type, data, &found, TRUE,
			  &ticket->ticket.key, ad, 0);
    if (ret)
	return ret;
    if (!found) {
	krb5_set_error_message(context, ENOENT,
			       N_("Ticket have not "
				  "authorization data of type %d", ""),
			       type);
	return ENOENT; /* XXX */
    }
    return 0;
}

static krb5_error_code
check_server_referral(krb5_context context,
		      krb5_kdc_rep *rep,
		      unsigned flags,
		      krb5_const_principal requested,
		      krb5_const_principal returned,
		      krb5_keyblock * key)
{
    krb5_error_code ret;
    PA_ServerReferralData ref;
    krb5_crypto session;
    EncryptedData ed;
    size_t len;
    krb5_data data;
    PA_DATA *pa;
    int i = 0, cmp;

    if (rep->kdc_rep.padata == NULL)
	goto noreferral;

    pa = krb5_find_padata(rep->kdc_rep.padata->val,
			  rep->kdc_rep.padata->len,
			  KRB5_PADATA_SERVER_REFERRAL, &i);
    if (pa == NULL)
	goto noreferral;

    memset(&ed, 0, sizeof(ed));
    memset(&ref, 0, sizeof(ref));

    ret = decode_EncryptedData(pa->padata_value.data,
			       pa->padata_value.length,
			       &ed, &len);
    if (ret)
	return ret;
    if (len != pa->padata_value.length) {
	free_EncryptedData(&ed);
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("Referral EncryptedData wrong for realm %s",
				  "realm"), requested->realm);
	return KRB5KRB_AP_ERR_MODIFIED;
    }

    ret = krb5_crypto_init(context, key, 0, &session);
    if (ret) {
	free_EncryptedData(&ed);
	return ret;
    }

    ret = krb5_decrypt_EncryptedData(context, session,
				     KRB5_KU_PA_SERVER_REFERRAL,
				     &ed, &data);
    free_EncryptedData(&ed);
    krb5_crypto_destroy(context, session);
    if (ret)
	return ret;

    ret = decode_PA_ServerReferralData(data.data, data.length, &ref, &len);
    if (ret) {
	krb5_data_free(&data);
	return ret;
    }
    krb5_data_free(&data);

    if (strcmp(requested->realm, returned->realm) != 0) {
	free_PA_ServerReferralData(&ref);
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("server ref realm mismatch, "
				  "requested realm %s got back %s", ""),
			       requested->realm, returned->realm);
	return KRB5KRB_AP_ERR_MODIFIED;
    }

    if (krb5_principal_is_krbtgt(context, returned)) {
	const char *realm = returned->name.name_string.val[1];

	if (ref.referred_realm == NULL
	    || strcmp(*ref.referred_realm, realm) != 0)
	{
	    free_PA_ServerReferralData(&ref);
	    krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
				   N_("tgt returned with wrong ref", ""));
	    return KRB5KRB_AP_ERR_MODIFIED;
	}
    } else if (krb5_principal_compare(context, returned, requested) == 0) {
	free_PA_ServerReferralData(&ref);
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("req princ no same as returned", ""));
	return KRB5KRB_AP_ERR_MODIFIED;
    }

    if (ref.requested_principal_name) {
	cmp = _krb5_principal_compare_PrincipalName(context,
						    requested,
						    ref.requested_principal_name);
	if (!cmp) {
	    free_PA_ServerReferralData(&ref);
	    krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
				   N_("referred principal not same "
				      "as requested", ""));
	    return KRB5KRB_AP_ERR_MODIFIED;
	}
    } else if (flags & EXTRACT_TICKET_AS_REQ) {
	free_PA_ServerReferralData(&ref);
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("Requested principal missing on AS-REQ", ""));
	return KRB5KRB_AP_ERR_MODIFIED;
    }

    free_PA_ServerReferralData(&ref);

    return ret;
noreferral:
    /*
     * Expect excact match or that we got a krbtgt
     */
    if (krb5_principal_compare(context, requested, returned) != TRUE &&
	(krb5_realm_compare(context, requested, returned) != TRUE &&
	 krb5_principal_is_krbtgt(context, returned) != TRUE))
    {
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("Not same server principal returned "
				  "as requested", ""));
	return KRB5KRB_AP_ERR_MODIFIED;
    }
    return 0;
}


/*
 * Verify referral data
 */


static krb5_error_code
check_client_referral(krb5_context context,
		      krb5_kdc_rep *rep,
		      krb5_const_principal requested,
		      krb5_const_principal mapped,
		      krb5_keyblock const * key)
{
    krb5_error_code ret;
    PA_ClientCanonicalized canon;
    krb5_crypto crypto;
    krb5_data data;
    PA_DATA *pa;
    size_t len;
    int i = 0;

    if (rep->kdc_rep.padata == NULL)
	goto noreferral;

    pa = krb5_find_padata(rep->kdc_rep.padata->val,
			  rep->kdc_rep.padata->len,
			  KRB5_PADATA_CLIENT_CANONICALIZED, &i);
    if (pa == NULL)
	goto noreferral;

    ret = decode_PA_ClientCanonicalized(pa->padata_value.data,
					pa->padata_value.length,
					&canon, &len);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to decode ClientCanonicalized "
				  "from realm %s", ""), requested->realm);
	return ret;
    }

    ASN1_MALLOC_ENCODE(PA_ClientCanonicalizedNames, data.data, data.length,
		       &canon.names, &len, ret);
    if (ret) {
	free_PA_ClientCanonicalized(&canon);
	return ret;
    }
    if (data.length != len)
	krb5_abortx(context, "internal asn.1 error");

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret) {
	free(data.data);
	free_PA_ClientCanonicalized(&canon);
	return ret;
    }

    ret = krb5_verify_checksum(context, crypto, KRB5_KU_CANONICALIZED_NAMES,
			       data.data, data.length,
			       &canon.canon_checksum);
    krb5_crypto_destroy(context, crypto);
    free(data.data);
    if (ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed to verify client canonicalized "
				  "data from realm %s", ""),
			       requested->realm);
	free_PA_ClientCanonicalized(&canon);
	return ret;
    }

    if (!_krb5_principal_compare_PrincipalName(context,
					       requested,
					       &canon.names.requested_name))
    {
	free_PA_ClientCanonicalized(&canon);
	krb5_set_error_message(context, KRB5_PRINC_NOMATCH,
			       N_("Requested name doesn't match"
				  " in client referral", ""));
	return KRB5_PRINC_NOMATCH;
    }
    if (!_krb5_principal_compare_PrincipalName(context,
					       mapped,
					       &canon.names.mapped_name))
    {
	free_PA_ClientCanonicalized(&canon);
	krb5_set_error_message(context, KRB5_PRINC_NOMATCH,
			       N_("Mapped name doesn't match"
				  " in client referral", ""));
	return KRB5_PRINC_NOMATCH;
    }

    return 0;

noreferral:
    if (krb5_principal_compare(context, requested, mapped) == FALSE) {
	krb5_set_error_message(context, KRB5KRB_AP_ERR_MODIFIED,
			       N_("Not same client principal returned "
				  "as requested", ""));
	return KRB5KRB_AP_ERR_MODIFIED;
    }
    return 0;
}


static krb5_error_code KRB5_CALLCONV
decrypt_tkt (krb5_context context,
	     krb5_keyblock *key,
	     krb5_key_usage usage,
	     krb5_const_pointer decrypt_arg,
	     krb5_kdc_rep *dec_rep)
{
    krb5_error_code ret;
    krb5_data data;
    size_t size;
    krb5_crypto crypto;

    ret = krb5_crypto_init(context, key, 0, &crypto);
    if (ret)
	return ret;

    ret = krb5_decrypt_EncryptedData (context,
				      crypto,
				      usage,
				      &dec_rep->kdc_rep.enc_part,
				      &data);
    krb5_crypto_destroy(context, crypto);

    if (ret)
	return ret;

    ret = decode_EncASRepPart(data.data,
			      data.length,
			      &dec_rep->enc_part,
			      &size);
    if (ret)
	ret = decode_EncTGSRepPart(data.data,
				   data.length,
				   &dec_rep->enc_part,
				   &size);
    krb5_data_free (&data);
    if (ret) {
        krb5_set_error_message(context, ret,
			       N_("Failed to decode encpart in ticket", ""));
	return ret;
    }
    return 0;
}

int
_krb5_extract_ticket(krb5_context context,
		     krb5_kdc_rep *rep,
		     krb5_creds *creds,
		     krb5_keyblock *key,
		     krb5_const_pointer keyseed,
		     krb5_key_usage key_usage,
		     krb5_addresses *addrs,
		     unsigned nonce,
		     unsigned flags,
		     krb5_decrypt_proc decrypt_proc,
		     krb5_const_pointer decryptarg)
{
    krb5_error_code ret;
    krb5_principal tmp_principal;
    size_t len = 0;
    time_t tmp_time;
    krb5_timestamp sec_now;

    /* decrypt */

    if (decrypt_proc == NULL)
	decrypt_proc = decrypt_tkt;

    ret = (*decrypt_proc)(context, key, key_usage, decryptarg, rep);
    if (ret)
	goto out;

    /* save session key */

    creds->session.keyvalue.length = 0;
    creds->session.keyvalue.data   = NULL;
    creds->session.keytype = rep->enc_part.key.keytype;
    ret = krb5_data_copy (&creds->session.keyvalue,
			  rep->enc_part.key.keyvalue.data,
			  rep->enc_part.key.keyvalue.length);
    if (ret) {
	krb5_clear_error_message(context);
	goto out;
    }

    /* compare client and save */
    ret = _krb5_principalname2krb5_principal (context,
					      &tmp_principal,
					      rep->kdc_rep.cname,
					      rep->kdc_rep.crealm);
    if (ret)
	goto out;

    /* check client referral and save principal */
    /* anonymous here ? */
    if((flags & EXTRACT_TICKET_ALLOW_CNAME_MISMATCH) == 0) {
	ret = check_client_referral(context, rep,
				    creds->client,
				    tmp_principal,
				    &creds->session);
	if (ret) {
	    krb5_free_principal (context, tmp_principal);
	    goto out;
	}
    }
    krb5_free_principal (context, creds->client);
    creds->client = tmp_principal;

    /* check server referral and save principal */
    ret = _krb5_principalname2krb5_principal (context,
					      &tmp_principal,
					      rep->enc_part.sname,
					      rep->enc_part.srealm);
    if (ret)
	goto out;
    if((flags & EXTRACT_TICKET_ALLOW_SERVER_MISMATCH) == 0){
	ret = check_server_referral(context,
				    rep,
				    flags,
				    creds->server,
				    tmp_principal,
				    &creds->session);
	if (ret) {
	    krb5_free_principal (context, tmp_principal);
	    goto out;
	}
    }
    krb5_free_principal(context, creds->server);
    creds->server = tmp_principal;

    /* verify names */
    if(flags & EXTRACT_TICKET_MATCH_REALM){
	const char *srealm = krb5_principal_get_realm(context, creds->server);
	const char *crealm = krb5_principal_get_realm(context, creds->client);

	if (strcmp(rep->enc_part.srealm, srealm) != 0 ||
	    strcmp(rep->enc_part.srealm, crealm) != 0)
	{
	    ret = KRB5KRB_AP_ERR_MODIFIED;
	    krb5_clear_error_message(context);
	    goto out;
	}
    }

    /* compare nonces */

    if (nonce != (unsigned)rep->enc_part.nonce) {
	ret = KRB5KRB_AP_ERR_MODIFIED;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto out;
    }

    /* set kdc-offset */

    krb5_timeofday (context, &sec_now);
    if (rep->enc_part.flags.initial
	&& (flags & EXTRACT_TICKET_TIMESYNC)
	&& context->kdc_sec_offset == 0
	&& krb5_config_get_bool (context, NULL,
				 "libdefaults",
				 "kdc_timesync",
				 NULL)) {
	context->kdc_sec_offset = rep->enc_part.authtime - sec_now;
	krb5_timeofday (context, &sec_now);
    }

    /* check all times */

    if (rep->enc_part.starttime) {
	tmp_time = *rep->enc_part.starttime;
    } else
	tmp_time = rep->enc_part.authtime;

    if (creds->times.starttime == 0
	&& abs(tmp_time - sec_now) > context->max_skew) {
	ret = KRB5KRB_AP_ERR_SKEW;
	krb5_set_error_message (context, ret,
				N_("time skew (%d) larger than max (%d)", ""),
			       abs(tmp_time - sec_now),
			       (int)context->max_skew);
	goto out;
    }

    if (creds->times.starttime != 0
	&& tmp_time != creds->times.starttime) {
	krb5_clear_error_message (context);
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.starttime = tmp_time;

    if (rep->enc_part.renew_till) {
	tmp_time = *rep->enc_part.renew_till;
    } else
	tmp_time = 0;

    if (creds->times.renew_till != 0
	&& tmp_time > creds->times.renew_till) {
	krb5_clear_error_message (context);
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.renew_till = tmp_time;

    creds->times.authtime = rep->enc_part.authtime;

    if (creds->times.endtime != 0
	&& rep->enc_part.endtime > creds->times.endtime) {
	krb5_clear_error_message (context);
	ret = KRB5KRB_AP_ERR_MODIFIED;
	goto out;
    }

    creds->times.endtime  = rep->enc_part.endtime;

    if(rep->enc_part.caddr)
	krb5_copy_addresses (context, rep->enc_part.caddr, &creds->addresses);
    else if(addrs)
	krb5_copy_addresses (context, addrs, &creds->addresses);
    else {
	creds->addresses.len = 0;
	creds->addresses.val = NULL;
    }
    creds->flags.b = rep->enc_part.flags;

    creds->authdata.len = 0;
    creds->authdata.val = NULL;

    /* extract ticket */
    ASN1_MALLOC_ENCODE(Ticket, creds->ticket.data, creds->ticket.length,
		       &rep->kdc_rep.ticket, &len, ret);
    if(ret)
	goto out;
    if (creds->ticket.length != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");
    creds->second_ticket.length = 0;
    creds->second_ticket.data   = NULL;


out:
    memset (rep->enc_part.key.keyvalue.data, 0,
	    rep->enc_part.key.keyvalue.length);
    return ret;
}
