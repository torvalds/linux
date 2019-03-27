/*
 * Copyright (c) 1997 - 2004 Kungliga Tekniska Högskolan
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

static krb5_error_code
add_addrs(krb5_context context,
	  krb5_addresses *addr,
	  struct addrinfo *ai)
{
    krb5_error_code ret;
    unsigned n, i;
    void *tmp;
    struct addrinfo *a;

    n = 0;
    for (a = ai; a != NULL; a = a->ai_next)
	++n;

    tmp = realloc(addr->val, (addr->len + n) * sizeof(*addr->val));
    if (tmp == NULL && (addr->len + n) != 0) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto fail;
    }
    addr->val = tmp;
    for (i = addr->len; i < (addr->len + n); ++i) {
	addr->val[i].addr_type = 0;
	krb5_data_zero(&addr->val[i].address);
    }
    i = addr->len;
    for (a = ai; a != NULL; a = a->ai_next) {
	krb5_address ad;

	ret = krb5_sockaddr2address (context, a->ai_addr, &ad);
	if (ret == 0) {
	    if (krb5_address_search(context, &ad, addr))
		krb5_free_address(context, &ad);
	    else
		addr->val[i++] = ad;
	}
	else if (ret == KRB5_PROG_ATYPE_NOSUPP)
	    krb5_clear_error_message (context);
	else
	    goto fail;
	addr->len = i;
    }
    return 0;
fail:
    krb5_free_addresses (context, addr);
    return ret;
}

/**
 * Forward credentials for client to host hostname , making them
 * forwardable if forwardable, and returning the blob of data to sent
 * in out_data.  If hostname == NULL, pick it from server.
 *
 * @param context A kerberos 5 context.
 * @param auth_context the auth context with the key to encrypt the out_data.
 * @param hostname the host to forward the tickets too.
 * @param client the client to delegate from.
 * @param server the server to delegate the credential too.
 * @param ccache credential cache to use.
 * @param forwardable make the forwarded ticket forwabledable.
 * @param out_data the resulting credential.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_fwd_tgt_creds (krb5_context	context,
		    krb5_auth_context	auth_context,
		    const char		*hostname,
		    krb5_principal	client,
		    krb5_principal	server,
		    krb5_ccache		ccache,
		    int			forwardable,
		    krb5_data		*out_data)
{
    krb5_flags flags = 0;
    krb5_creds creds;
    krb5_error_code ret;
    krb5_const_realm client_realm;

    flags |= KDC_OPT_FORWARDED;

    if (forwardable)
	flags |= KDC_OPT_FORWARDABLE;

    if (hostname == NULL &&
	krb5_principal_get_type(context, server) == KRB5_NT_SRV_HST) {
	const char *inst = krb5_principal_get_comp_string(context, server, 0);
	const char *host = krb5_principal_get_comp_string(context, server, 1);

	if (inst != NULL &&
	    strcmp(inst, "host") == 0 &&
	    host != NULL &&
	    krb5_principal_get_comp_string(context, server, 2) == NULL)
	    hostname = host;
    }

    client_realm = krb5_principal_get_realm(context, client);

    memset (&creds, 0, sizeof(creds));
    creds.client = client;

    ret = krb5_make_principal(context,
			      &creds.server,
			      client_realm,
			      KRB5_TGS_NAME,
			      client_realm,
			      NULL);
    if (ret)
	return ret;

    ret = krb5_get_forwarded_creds (context,
				    auth_context,
				    ccache,
				    flags,
				    hostname,
				    &creds,
				    out_data);
    return ret;
}

/**
 * Gets tickets forwarded to hostname. If the tickets that are
 * forwarded are address-less, the forwarded tickets will also be
 * address-less.
 *
 * If the ticket have any address, hostname will be used for figure
 * out the address to forward the ticket too. This since this might
 * use DNS, its insecure and also doesn't represent configured all
 * addresses of the host. For example, the host might have two
 * adresses, one IPv4 and one IPv6 address where the later is not
 * published in DNS. This IPv6 address might be used communications
 * and thus the resulting ticket useless.
 *
 * @param context A kerberos 5 context.
 * @param auth_context the auth context with the key to encrypt the out_data.
 * @param ccache credential cache to use
 * @param flags the flags to control the resulting ticket flags
 * @param hostname the host to forward the tickets too.
 * @param in_creds the in client and server ticket names.  The client
 * and server components forwarded to the remote host.
 * @param out_data the resulting credential.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_credential
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_get_forwarded_creds (krb5_context	    context,
			  krb5_auth_context auth_context,
			  krb5_ccache       ccache,
			  krb5_flags        flags,
			  const char        *hostname,
			  krb5_creds        *in_creds,
			  krb5_data         *out_data)
{
    krb5_error_code ret;
    krb5_creds *out_creds;
    krb5_addresses addrs, *paddrs;
    KRB_CRED cred;
    KrbCredInfo *krb_cred_info;
    EncKrbCredPart enc_krb_cred_part;
    size_t len;
    unsigned char *buf;
    size_t buf_size;
    krb5_kdc_flags kdc_flags;
    krb5_crypto crypto;
    struct addrinfo *ai;
    krb5_creds *ticket;

    paddrs = NULL;
    addrs.len = 0;
    addrs.val = NULL;

    ret = krb5_get_credentials(context, 0, ccache, in_creds, &ticket);
    if(ret == 0) {
	if (ticket->addresses.len)
	    paddrs = &addrs;
	krb5_free_creds (context, ticket);
    } else {
	krb5_boolean noaddr;
	krb5_appdefault_boolean(context, NULL,
				krb5_principal_get_realm(context,
							 in_creds->client),
				"no-addresses", KRB5_ADDRESSLESS_DEFAULT,
				&noaddr);
	if (!noaddr)
	    paddrs = &addrs;
    }

    /*
     * If tickets have addresses, get the address of the remote host.
     */

    if (paddrs != NULL) {

	ret = getaddrinfo (hostname, NULL, NULL, &ai);
	if (ret) {
	    krb5_error_code ret2 = krb5_eai_to_heim_errno(ret, errno);
	    krb5_set_error_message(context, ret2,
				   N_("resolving host %s failed: %s",
				      "hostname, error"),
				  hostname, gai_strerror(ret));
	    return ret2;
	}

	ret = add_addrs (context, &addrs, ai);
	freeaddrinfo (ai);
	if (ret)
	    return ret;
    }

    kdc_flags.b = int2KDCOptions(flags);

    ret = krb5_get_kdc_cred (context,
			     ccache,
			     kdc_flags,
			     paddrs,
			     NULL,
			     in_creds,
			     &out_creds);
    krb5_free_addresses (context, &addrs);
    if (ret)
	return ret;

    memset (&cred, 0, sizeof(cred));
    cred.pvno = 5;
    cred.msg_type = krb_cred;
    ALLOC_SEQ(&cred.tickets, 1);
    if (cred.tickets.val == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto out2;
    }
    ret = decode_Ticket(out_creds->ticket.data,
			out_creds->ticket.length,
			cred.tickets.val, &len);
    if (ret)
	goto out3;

    memset (&enc_krb_cred_part, 0, sizeof(enc_krb_cred_part));
    ALLOC_SEQ(&enc_krb_cred_part.ticket_info, 1);
    if (enc_krb_cred_part.ticket_info.val == NULL) {
	ret = ENOMEM;
	krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	goto out4;
    }

    if (auth_context->flags & KRB5_AUTH_CONTEXT_DO_TIME) {
	krb5_timestamp sec;
	int32_t usec;

	krb5_us_timeofday (context, &sec, &usec);

	ALLOC(enc_krb_cred_part.timestamp, 1);
	if (enc_krb_cred_part.timestamp == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto out4;
	}
	*enc_krb_cred_part.timestamp = sec;
	ALLOC(enc_krb_cred_part.usec, 1);
	if (enc_krb_cred_part.usec == NULL) {
	    ret = ENOMEM;
	    krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
	    goto out4;
	}
	*enc_krb_cred_part.usec      = usec;
    } else {
	enc_krb_cred_part.timestamp = NULL;
	enc_krb_cred_part.usec = NULL;
    }

    if (auth_context->local_address && auth_context->local_port && paddrs) {

	ret = krb5_make_addrport (context,
				  &enc_krb_cred_part.s_address,
				  auth_context->local_address,
				  auth_context->local_port);
	if (ret)
	    goto out4;
    }

    if (auth_context->remote_address) {
	if (auth_context->remote_port) {
	    krb5_boolean noaddr;
	    krb5_const_realm srealm;

	    srealm = krb5_principal_get_realm(context, out_creds->server);
	    /* Is this correct, and should we use the paddrs == NULL
               trick here as well? Having an address-less ticket may
               indicate that we don't know our own global address, but
               it does not necessary mean that we don't know the
               server's. */
	    krb5_appdefault_boolean(context, NULL, srealm, "no-addresses",
				    FALSE, &noaddr);
	    if (!noaddr) {
		ret = krb5_make_addrport (context,
					  &enc_krb_cred_part.r_address,
					  auth_context->remote_address,
					  auth_context->remote_port);
		if (ret)
		    goto out4;
	    }
	} else {
	    ALLOC(enc_krb_cred_part.r_address, 1);
	    if (enc_krb_cred_part.r_address == NULL) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret,
				       N_("malloc: out of memory", ""));
		goto out4;
	    }

	    ret = krb5_copy_address (context, auth_context->remote_address,
				     enc_krb_cred_part.r_address);
	    if (ret)
		goto out4;
	}
    }

    /* fill ticket_info.val[0] */

    enc_krb_cred_part.ticket_info.len = 1;

    krb_cred_info = enc_krb_cred_part.ticket_info.val;

    copy_EncryptionKey (&out_creds->session, &krb_cred_info->key);
    ALLOC(krb_cred_info->prealm, 1);
    copy_Realm (&out_creds->client->realm, krb_cred_info->prealm);
    ALLOC(krb_cred_info->pname, 1);
    copy_PrincipalName(&out_creds->client->name, krb_cred_info->pname);
    ALLOC(krb_cred_info->flags, 1);
    *krb_cred_info->flags          = out_creds->flags.b;
    ALLOC(krb_cred_info->authtime, 1);
    *krb_cred_info->authtime       = out_creds->times.authtime;
    ALLOC(krb_cred_info->starttime, 1);
    *krb_cred_info->starttime      = out_creds->times.starttime;
    ALLOC(krb_cred_info->endtime, 1);
    *krb_cred_info->endtime        = out_creds->times.endtime;
    ALLOC(krb_cred_info->renew_till, 1);
    *krb_cred_info->renew_till = out_creds->times.renew_till;
    ALLOC(krb_cred_info->srealm, 1);
    copy_Realm (&out_creds->server->realm, krb_cred_info->srealm);
    ALLOC(krb_cred_info->sname, 1);
    copy_PrincipalName (&out_creds->server->name, krb_cred_info->sname);
    ALLOC(krb_cred_info->caddr, 1);
    copy_HostAddresses (&out_creds->addresses, krb_cred_info->caddr);

    krb5_free_creds (context, out_creds);

    /* encode EncKrbCredPart */

    ASN1_MALLOC_ENCODE(EncKrbCredPart, buf, buf_size,
		       &enc_krb_cred_part, &len, ret);
    free_EncKrbCredPart (&enc_krb_cred_part);
    if (ret) {
	free_KRB_CRED(&cred);
	return ret;
    }
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");

    /**
     * Some older of the MIT gssapi library used clear-text tickets
     * (warped inside AP-REQ encryption), use the krb5_auth_context
     * flag KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED to support those
     * tickets. The session key is used otherwise to encrypt the
     * forwarded ticket.
     */

    if (auth_context->flags & KRB5_AUTH_CONTEXT_CLEAR_FORWARDED_CRED) {
	cred.enc_part.etype = ENCTYPE_NULL;
	cred.enc_part.kvno = NULL;
	cred.enc_part.cipher.data = buf;
	cred.enc_part.cipher.length = buf_size;
    } else {
	/*
	 * Here older versions then 0.7.2 of Heimdal used the local or
	 * remote subkey. That is wrong, the session key should be
	 * used. Heimdal 0.7.2 and newer have code to try both in the
	 * receiving end.
	 */

	ret = krb5_crypto_init(context, auth_context->keyblock, 0, &crypto);
	if (ret) {
	    free(buf);
	    free_KRB_CRED(&cred);
	    return ret;
	}
	ret = krb5_encrypt_EncryptedData (context,
					  crypto,
					  KRB5_KU_KRB_CRED,
					  buf,
					  len,
					  0,
					  &cred.enc_part);
	free(buf);
	krb5_crypto_destroy(context, crypto);
	if (ret) {
	    free_KRB_CRED(&cred);
	    return ret;
	}
    }

    ASN1_MALLOC_ENCODE(KRB_CRED, buf, buf_size, &cred, &len, ret);
    free_KRB_CRED (&cred);
    if (ret)
	return ret;
    if(buf_size != len)
	krb5_abortx(context, "internal error in ASN.1 encoder");
    out_data->length = len;
    out_data->data   = buf;
    return 0;
 out4:
    free_EncKrbCredPart(&enc_krb_cred_part);
 out3:
    free_KRB_CRED(&cred);
 out2:
    krb5_free_creds (context, out_creds);
    return ret;
}
