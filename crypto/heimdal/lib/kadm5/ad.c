/*
 * Copyright (c) 2004 Kungliga Tekniska Högskolan
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

#define HAVE_TSASL 1

#include "kadm5_locl.h"
#if 1
#undef OPENLDAP
#undef HAVE_TSASL
#endif
#ifdef OPENLDAP
#include <ldap.h>
#ifdef HAVE_TSASL
#include <tsasl.h>
#endif
#include <resolve.h>
#include <base64.h>
#endif

RCSID("$Id$");

#ifdef OPENLDAP

#define CTX2LP(context) ((LDAP *)((context)->ldap_conn))
#define CTX2BASE(context) ((context)->base_dn)

/*
 * userAccountControl
 */

#define UF_SCRIPT	 			0x00000001
#define UF_ACCOUNTDISABLE			0x00000002
#define UF_UNUSED_0	 			0x00000004
#define UF_HOMEDIR_REQUIRED			0x00000008
#define UF_LOCKOUT	 			0x00000010
#define UF_PASSWD_NOTREQD 			0x00000020
#define UF_PASSWD_CANT_CHANGE 			0x00000040
#define UF_ENCRYPTED_TEXT_PASSWORD_ALLOWED	0x00000080
#define UF_TEMP_DUPLICATE_ACCOUNT       	0x00000100
#define UF_NORMAL_ACCOUNT               	0x00000200
#define UF_UNUSED_1	 			0x00000400
#define UF_INTERDOMAIN_TRUST_ACCOUNT    	0x00000800
#define UF_WORKSTATION_TRUST_ACCOUNT    	0x00001000
#define UF_SERVER_TRUST_ACCOUNT         	0x00002000
#define UF_UNUSED_2	 			0x00004000
#define UF_UNUSED_3	 			0x00008000
#define UF_PASSWD_NOT_EXPIRE			0x00010000
#define UF_MNS_LOGON_ACCOUNT			0x00020000
#define UF_SMARTCARD_REQUIRED			0x00040000
#define UF_TRUSTED_FOR_DELEGATION		0x00080000
#define UF_NOT_DELEGATED			0x00100000
#define UF_USE_DES_KEY_ONLY			0x00200000
#define UF_DONT_REQUIRE_PREAUTH			0x00400000
#define UF_UNUSED_4				0x00800000
#define UF_UNUSED_5				0x01000000
#define UF_UNUSED_6				0x02000000
#define UF_UNUSED_7				0x04000000
#define UF_UNUSED_8				0x08000000
#define UF_UNUSED_9				0x10000000
#define UF_UNUSED_10				0x20000000
#define UF_UNUSED_11				0x40000000
#define UF_UNUSED_12				0x80000000

/*
 *
 */

#ifndef HAVE_TSASL
static int
sasl_interact(LDAP *ld, unsigned flags, void *defaults, void *interact)
{
    return LDAP_SUCCESS;
}
#endif

#if 0
static Sockbuf_IO ldap_tsasl_io = {
    NULL,			/* sbi_setup */
    NULL,			/* sbi_remove */
    NULL,			/* sbi_ctrl */
    NULL,			/* sbi_read */
    NULL,			/* sbi_write */
    NULL			/* sbi_close */
};
#endif

#ifdef HAVE_TSASL
static int
ldap_tsasl_bind_s(LDAP *ld,
		  LDAP_CONST char *dn,
		  LDAPControl **serverControls,
		  LDAPControl **clientControls,
		  const char *host)
{
    char *attrs[] = { "supportedSASLMechanisms", NULL };
    struct tsasl_peer *peer = NULL;
    struct tsasl_buffer in, out;
    struct berval ccred, *scred;
    LDAPMessage *m, *m0;
    const char *mech;
    char **vals;
    int ret, rc;

    ret = tsasl_peer_init(TSASL_FLAGS_INITIATOR | TSASL_FLAGS_CLEAR,
			  "ldap", host, &peer);
    if (ret != TSASL_DONE) {
	rc = LDAP_LOCAL_ERROR;
	goto out;
    }

    rc = ldap_search_s(ld, "", LDAP_SCOPE_BASE, NULL, attrs, 0, &m0);
    if (rc != LDAP_SUCCESS)
	goto out;

    m = ldap_first_entry(ld, m0);
    if (m == NULL) {
	ldap_msgfree(m0);
	goto out;
    }

    vals = ldap_get_values(ld, m, "supportedSASLMechanisms");
    if (vals == NULL) {
	ldap_msgfree(m0);
	goto out;
    }

    ret = tsasl_find_best_mech(peer, vals, &mech);
    if (ret) {
	ldap_msgfree(m0);
	goto out;
    }

    ldap_msgfree(m0);

    ret = tsasl_select_mech(peer, mech);
    if (ret != TSASL_DONE) {
	rc = LDAP_LOCAL_ERROR;
	goto out;
    }

    in.tb_data = NULL;
    in.tb_size = 0;

    do {
	ret = tsasl_request(peer, &in, &out);
	if (in.tb_size != 0) {
	    free(in.tb_data);
	    in.tb_data = NULL;
	    in.tb_size = 0;
	}
	if (ret != TSASL_DONE && ret != TSASL_CONTINUE) {
	    rc = LDAP_AUTH_UNKNOWN;
	    goto out;
	}

	ccred.bv_val = out.tb_data;
	ccred.bv_len = out.tb_size;

	rc = ldap_sasl_bind_s(ld, dn, mech, &ccred,
			      serverControls, clientControls, &scred);
	tsasl_buffer_free(&out);

	if (rc != LDAP_SUCCESS && rc != LDAP_SASL_BIND_IN_PROGRESS) {
	    if(scred && scred->bv_len)
		ber_bvfree(scred);
	    goto out;
	}

	in.tb_data = malloc(scred->bv_len);
	if (in.tb_data == NULL) {
	    rc = LDAP_LOCAL_ERROR;
	    goto out;
	}
	memcpy(in.tb_data, scred->bv_val, scred->bv_len);
	in.tb_size = scred->bv_len;
	ber_bvfree(scred);

    } while (rc == LDAP_SASL_BIND_IN_PROGRESS);

 out:
    if (rc == LDAP_SUCCESS) {
#if 0
	ber_sockbuf_add_io(ld->ld_conns->lconn_sb, &ldap_tsasl_io,
			   LBER_SBIOD_LEVEL_APPLICATION, peer);

#endif
    } else if (peer != NULL)
	tsasl_peer_free(peer);

    return rc;
}
#endif /* HAVE_TSASL */


static int
check_ldap(kadm5_ad_context *context, int ret)
{
    switch (ret) {
    case LDAP_SUCCESS:
	return 0;
    case LDAP_SERVER_DOWN: {
	LDAP *lp = CTX2LP(context);
	ldap_unbind(lp);
	context->ldap_conn = NULL;
	free(context->base_dn);
	context->base_dn = NULL;
	return 1;
    }
    default:
	return 1;
    }
}

/*
 *
 */

static void
laddattr(char ***al, int *attrlen, char *attr)
{
    char **a;
    a = realloc(*al, (*attrlen + 2) * sizeof(**al));
    if (a == NULL)
	return;
    a[*attrlen] = attr;
    a[*attrlen + 1] = NULL;
    (*attrlen)++;
    *al = a;
}

static kadm5_ret_t
_kadm5_ad_connect(void *server_handle)
{
    kadm5_ad_context *context = server_handle;
    struct {
	char *server;
	int port;
    } *s, *servers = NULL;
    int i, num_servers = 0;

    if (context->ldap_conn)
	return 0;

    {
	struct dns_reply *r;
	struct resource_record *rr;
	char *domain;

	asprintf(&domain, "_ldap._tcp.%s", context->realm);
	if (domain == NULL) {
	    krb5_set_error_message(context->context, KADM5_NO_SRV, "malloc");
	    return KADM5_NO_SRV;
	}

	r = dns_lookup(domain, "SRV");
	free(domain);
	if (r == NULL) {
	    krb5_set_error_message(context->context, KADM5_NO_SRV, "Didn't find ldap dns");
	    return KADM5_NO_SRV;
	}

	for (rr = r->head ; rr != NULL; rr = rr->next) {
	    if (rr->type != rk_ns_t_srv)
		continue;
	    s = realloc(servers, sizeof(*servers) * (num_servers + 1));
	    if (s == NULL) {
		krb5_set_error_message(context->context, KADM5_RPC_ERROR, "malloc");
		dns_free_data(r);
		goto fail;
	    }
	    servers = s;
	    num_servers++;
	    servers[num_servers - 1].port =  rr->u.srv->port;
	    servers[num_servers - 1].server =  strdup(rr->u.srv->target);
	}
	dns_free_data(r);
    }

    if (num_servers == 0) {
	krb5_set_error_message(context->context, KADM5_NO_SRV, "No AD server found in DNS");
	return KADM5_NO_SRV;
    }

    for (i = 0; i < num_servers; i++) {
	int lret, version = LDAP_VERSION3;
	LDAP *lp;

	lp = ldap_init(servers[i].server, servers[i].port);
	if (lp == NULL)
	    continue;

	if (ldap_set_option(lp, LDAP_OPT_PROTOCOL_VERSION, &version)) {
	    ldap_unbind(lp);
	    continue;
	}

	if (ldap_set_option(lp, LDAP_OPT_REFERRALS, LDAP_OPT_OFF)) {
	    ldap_unbind(lp);
	    continue;
	}

#ifdef HAVE_TSASL
	lret = ldap_tsasl_bind_s(lp, NULL, NULL, NULL, servers[i].server);

#else
	lret = ldap_sasl_interactive_bind_s(lp, NULL, NULL, NULL, NULL,
					    LDAP_SASL_QUIET,
					    sasl_interact, NULL);
#endif
	if (lret != LDAP_SUCCESS) {
	    krb5_set_error_message(context->context, 0,
				   "Couldn't contact any AD servers: %s",
				   ldap_err2string(lret));
	    ldap_unbind(lp);
	    continue;
	}

	context->ldap_conn = lp;
	break;
    }
    if (i >= num_servers) {
	goto fail;
    }

    {
	LDAPMessage *m, *m0;
	char **attr = NULL;
	int attrlen = 0;
	char **vals;
	int ret;

	laddattr(&attr, &attrlen, "defaultNamingContext");

	ret = ldap_search_s(CTX2LP(context), "", LDAP_SCOPE_BASE,
			    "objectclass=*", attr, 0, &m);
	free(attr);
	if (check_ldap(context, ret))
	    goto fail;

	if (ldap_count_entries(CTX2LP(context), m) > 0) {
	    m0 = ldap_first_entry(CTX2LP(context), m);
	    if (m0 == NULL) {
		krb5_set_error_message(context->context, KADM5_RPC_ERROR,
				       "Error in AD ldap responce");
		ldap_msgfree(m);
		goto fail;
	    }
	    vals = ldap_get_values(CTX2LP(context),
				   m0, "defaultNamingContext");
	    if (vals == NULL) {
		krb5_set_error_message(context->context, KADM5_RPC_ERROR,
				       "No naming context found");
		goto fail;
	    }
	    context->base_dn = strdup(vals[0]);
	} else
	    goto fail;
	ldap_msgfree(m);
    }

    for (i = 0; i < num_servers; i++)
	free(servers[i].server);
    free(servers);

    return 0;

 fail:
    for (i = 0; i < num_servers; i++)
	free(servers[i].server);
    free(servers);

    if (context->ldap_conn) {
	ldap_unbind(CTX2LP(context));
	context->ldap_conn = NULL;
    }
    return KADM5_RPC_ERROR;
}

#define NTTIME_EPOCH 0x019DB1DED53E8000LL

static time_t
nt2unixtime(const char *str)
{
    unsigned long long t;
    t = strtoll(str, NULL, 10);
    t = ((t - NTTIME_EPOCH) / (long long)10000000);
    if (t > (((time_t)(~(long long)0)) >> 1))
	return 0;
    return (time_t)t;
}

static long long
unix2nttime(time_t unix_time)
{
    long long wt;
    wt = unix_time * (long long)10000000 + (long long)NTTIME_EPOCH;
    return wt;
}

/* XXX create filter in a better way */

static int
ad_find_entry(kadm5_ad_context *context,
	      const char *fqdn,
	      const char *pn,
	      char **name)
{
    LDAPMessage *m, *m0;
    char *attr[] = { "distinguishedName", NULL };
    char *filter;
    int ret;

    if (name)
	*name = NULL;

    if (fqdn)
	asprintf(&filter,
		 "(&(objectClass=computer)(|(dNSHostName=%s)(servicePrincipalName=%s)))",
		 fqdn, pn);
    else if(pn)
	asprintf(&filter, "(&(objectClass=account)(userPrincipalName=%s))", pn);
    else
	return KADM5_RPC_ERROR;

    ret = ldap_search_s(CTX2LP(context), CTX2BASE(context),
			LDAP_SCOPE_SUBTREE,
			filter, attr, 0, &m);
    free(filter);
    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;

    if (ldap_count_entries(CTX2LP(context), m) > 0) {
	char **vals;
	m0 = ldap_first_entry(CTX2LP(context), m);
	vals = ldap_get_values(CTX2LP(context), m0, "distinguishedName");
	if (vals == NULL || vals[0] == NULL) {
	    ldap_msgfree(m);
	    return KADM5_RPC_ERROR;
	}
	if (name)
	    *name = strdup(vals[0]);
	ldap_msgfree(m);
    } else
	return KADM5_UNK_PRINC;

    return 0;
}

#endif /* OPENLDAP */

static kadm5_ret_t
ad_get_cred(kadm5_ad_context *context, const char *password)
{
    kadm5_ret_t ret;
    krb5_ccache cc;
    char *service;

    if (context->ccache)
	return 0;

    asprintf(&service, "%s/%s@%s", KRB5_TGS_NAME,
	     context->realm, context->realm);
    if (service == NULL)
	return ENOMEM;

    ret = _kadm5_c_get_cred_cache(context->context,
				  context->client_name,
				  service,
				  password, krb5_prompter_posix,
				  NULL, NULL, &cc);
    free(service);
    if(ret)
	return ret; /* XXX */
    context->ccache = cc;
    return 0;
}

static kadm5_ret_t
kadm5_ad_chpass_principal(void *server_handle,
			  krb5_principal principal,
			  const char *password)
{
    kadm5_ad_context *context = server_handle;
    krb5_data result_code_string, result_string;
    int result_code;
    kadm5_ret_t ret;

    ret = ad_get_cred(context, NULL);
    if (ret)
	return ret;

    krb5_data_zero (&result_code_string);
    krb5_data_zero (&result_string);

    ret = krb5_set_password_using_ccache (context->context,
					  context->ccache,
					  password,
					  principal,
					  &result_code,
					  &result_code_string,
					  &result_string);

    krb5_data_free (&result_code_string);
    krb5_data_free (&result_string);

    /* XXX do mapping here on error codes */

    return ret;
}

#ifdef OPENLDAP
static const char *
get_fqdn(krb5_context context, const krb5_principal p)
{
    const char *s, *hosttypes[] = { "host", "ldap", "gc", "cifs", "dns" };
    int i;

    s = krb5_principal_get_comp_string(context, p, 0);
    if (p == NULL)
	return NULL;

    for (i = 0; i < sizeof(hosttypes)/sizeof(hosttypes[0]); i++) {
	if (strcasecmp(s, hosttypes[i]) == 0)
	    return krb5_principal_get_comp_string(context, p, 1);
    }
    return 0;
}
#endif


static kadm5_ret_t
kadm5_ad_create_principal(void *server_handle,
			  kadm5_principal_ent_t entry,
			  uint32_t mask,
			  const char *password)
{
    kadm5_ad_context *context = server_handle;

    /*
     * KADM5_PRINC_EXPIRE_TIME
     *
     * return 0 || KADM5_DUP;
     */

#ifdef OPENLDAP
    LDAPMod *attrs[8], rattrs[7], *a;
    char *useraccvals[2] = { NULL, NULL },
	*samvals[2], *dnsvals[2], *spnvals[5], *upnvals[2], *tv[2];
    char *ocvals_spn[] = { "top", "person", "organizationalPerson",
			   "user", "computer", NULL};
    char *p, *realmless_p, *p_msrealm = NULL, *dn = NULL;
    const char *fqdn;
    char *s, *samname = NULL, *short_spn = NULL;
    int ret, i;
    int32_t uf_flags = 0;

    if ((mask & KADM5_PRINCIPAL) == 0)
	return KADM5_BAD_MASK;

    for (i = 0; i < sizeof(rattrs)/sizeof(rattrs[0]); i++)
	attrs[i] = &rattrs[i];
    attrs[i] = NULL;

    ret = ad_get_cred(context, NULL);
    if (ret)
	return ret;

    ret = _kadm5_ad_connect(server_handle);
    if (ret)
	return ret;

    fqdn = get_fqdn(context->context, entry->principal);

    ret = krb5_unparse_name(context->context, entry->principal, &p);
    if (ret)
	return ret;

    if (ad_find_entry(context, fqdn, p, NULL) == 0) {
	free(p);
	return KADM5_DUP;
    }

    if (mask & KADM5_ATTRIBUTES) {
	if (entry->attributes & KRB5_KDB_DISALLOW_ALL_TIX)
	    uf_flags |= UF_ACCOUNTDISABLE|UF_LOCKOUT;
	if ((entry->attributes & KRB5_KDB_REQUIRES_PRE_AUTH) == 0)
	    uf_flags |= UF_DONT_REQUIRE_PREAUTH;
	if (entry->attributes & KRB5_KDB_REQUIRES_HW_AUTH)
	    uf_flags |= UF_SMARTCARD_REQUIRED;
    }

    realmless_p = strdup(p);
    if (realmless_p == NULL) {
	ret = ENOMEM;
	goto out;
    }
    s = strrchr(realmless_p, '@');
    if (s)
	*s = '\0';

    if (fqdn) {
	/* create computer account */
	asprintf(&samname, "%s$", fqdn);
	if (samname == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	s = strchr(samname, '.');
	if (s) {
	    s[0] = '$';
	    s[1] = '\0';
	}

	short_spn = strdup(p);
	if (short_spn == NULL) {
	    errno = ENOMEM;
	    goto out;
	}
	s = strchr(short_spn, '.');
	if (s) {
	    *s = '\0';
	} else {
	    free(short_spn);
	    short_spn = NULL;
	}

	p_msrealm = strdup(p);
	if (p_msrealm == NULL) {
	    errno = ENOMEM;
	    goto out;
	}
	s = strrchr(p_msrealm, '@');
	if (s) {
	    *s = '/';
	} else {
	    free(p_msrealm);
	    p_msrealm = NULL;
	}

	asprintf(&dn, "cn=%s, cn=Computers, %s", fqdn, CTX2BASE(context));
	if (dn == NULL) {
	    ret = ENOMEM;
	    goto out;
	}

	a = &rattrs[0];
	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "objectClass";
	a->mod_values = ocvals_spn;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "userAccountControl";
	a->mod_values = useraccvals;
	asprintf(&useraccvals[0], "%d",
		 uf_flags |
		 UF_PASSWD_NOT_EXPIRE |
		 UF_WORKSTATION_TRUST_ACCOUNT);
	useraccvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "sAMAccountName";
	a->mod_values = samvals;
	samvals[0] = samname;
	samvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "dNSHostName";
	a->mod_values = dnsvals;
	dnsvals[0] = (char *)fqdn;
	dnsvals[1] = NULL;
	a++;

	/* XXX  add even more spn's */
	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "servicePrincipalName";
	a->mod_values = spnvals;
	i = 0;
	spnvals[i++] = p;
	spnvals[i++] = realmless_p;
	if (short_spn)
	    spnvals[i++] = short_spn;
	if (p_msrealm)
	    spnvals[i++] = p_msrealm;
	spnvals[i++] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "userPrincipalName";
	a->mod_values = upnvals;
	upnvals[0] = p;
	upnvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "accountExpires";
	a->mod_values = tv;
	tv[0] = "9223372036854775807"; /* "never" */
	tv[1] = NULL;
	a++;

    } else {
	/* create user account */

	a = &rattrs[0];
	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "userAccountControl";
	a->mod_values = useraccvals;
	asprintf(&useraccvals[0], "%d",
		 uf_flags |
		 UF_PASSWD_NOT_EXPIRE);
	useraccvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "sAMAccountName";
	a->mod_values = samvals;
	samvals[0] = realmless_p;
	samvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "userPrincipalName";
	a->mod_values = upnvals;
	upnvals[0] = p;
	upnvals[1] = NULL;
	a++;

	a->mod_op = LDAP_MOD_ADD;
	a->mod_type = "accountExpires";
	a->mod_values = tv;
	tv[0] = "9223372036854775807"; /* "never" */
	tv[1] = NULL;
	a++;
    }

    attrs[a - &rattrs[0]] = NULL;

    ret = ldap_add_s(CTX2LP(context), dn, attrs);

 out:
    if (useraccvals[0])
	free(useraccvals[0]);
    if (realmless_p)
	free(realmless_p);
    if (samname)
	free(samname);
    if (short_spn)
	free(short_spn);
    if (p_msrealm)
	free(p_msrealm);
    free(p);

    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;

    return 0;
#else
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_delete_principal(void *server_handle, krb5_principal principal)
{
    kadm5_ad_context *context = server_handle;
#ifdef OPENLDAP
    char *p, *dn = NULL;
    const char *fqdn;
    int ret;

    ret = ad_get_cred(context, NULL);
    if (ret)
	return ret;

    ret = _kadm5_ad_connect(server_handle);
    if (ret)
	return ret;

    fqdn = get_fqdn(context->context, principal);

    ret = krb5_unparse_name(context->context, principal, &p);
    if (ret)
	return ret;

    if (ad_find_entry(context, fqdn, p, &dn) != 0) {
	free(p);
	return KADM5_UNK_PRINC;
    }

    ret = ldap_delete_s(CTX2LP(context), dn);

    free(dn);
    free(p);

    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;
    return 0;
#else
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_destroy(void *server_handle)
{
    kadm5_ad_context *context = server_handle;

    if (context->ccache)
	krb5_cc_destroy(context->context, context->ccache);

#ifdef OPENLDAP
    {
	LDAP *lp = CTX2LP(context);
	if (lp)
	    ldap_unbind(lp);
	if (context->base_dn)
	    free(context->base_dn);
    }
#endif
    free(context->realm);
    free(context->client_name);
    krb5_free_principal(context->context, context->caller);
    if(context->my_context)
	krb5_free_context(context->context);
    return 0;
}

static kadm5_ret_t
kadm5_ad_flush(void *server_handle)
{
    kadm5_ad_context *context = server_handle;
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
}

static kadm5_ret_t
kadm5_ad_get_principal(void *server_handle,
		       krb5_principal principal,
		       kadm5_principal_ent_t entry,
		       uint32_t mask)
{
    kadm5_ad_context *context = server_handle;
#ifdef OPENLDAP
    LDAPMessage *m, *m0;
    char **attr = NULL;
    int attrlen = 0;
    char *filter, *p, *q, *u;
    int ret;

    /*
     * principal
     * KADM5_PRINCIPAL | KADM5_KVNO | KADM5_ATTRIBUTES
     */

    /*
     * return 0 || KADM5_DUP;
     */

    memset(entry, 0, sizeof(*entry));

    if (mask & KADM5_KVNO)
	laddattr(&attr, &attrlen, "msDS-KeyVersionNumber");

    if (mask & KADM5_PRINCIPAL) {
	laddattr(&attr, &attrlen, "userPrincipalName");
	laddattr(&attr, &attrlen, "servicePrincipalName");
    }
    laddattr(&attr, &attrlen, "objectClass");
    laddattr(&attr, &attrlen, "lastLogon");
    laddattr(&attr, &attrlen, "badPwdCount");
    laddattr(&attr, &attrlen, "badPasswordTime");
    laddattr(&attr, &attrlen, "pwdLastSet");
    laddattr(&attr, &attrlen, "accountExpires");
    laddattr(&attr, &attrlen, "userAccountControl");

    krb5_unparse_name_short(context->context, principal, &p);
    krb5_unparse_name(context->context, principal, &u);

    /* replace @ in domain part with a / */
    q = strrchr(p, '@');
    if (q && (p != q && *(q - 1) != '\\'))
	*q = '/';

    asprintf(&filter,
	     "(|(userPrincipalName=%s)(servicePrincipalName=%s)(servicePrincipalName=%s))",
	     u, p, u);
    free(p);
    free(u);

    ret = ldap_search_s(CTX2LP(context), CTX2BASE(context),
			LDAP_SCOPE_SUBTREE,
			filter, attr, 0, &m);
    free(attr);
    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;

    if (ldap_count_entries(CTX2LP(context), m) > 0) {
	char **vals;
	m0 = ldap_first_entry(CTX2LP(context), m);
	if (m0 == NULL) {
	    ldap_msgfree(m);
	    goto fail;
	}
#if 0
	vals = ldap_get_values(CTX2LP(context), m0, "servicePrincipalName");
	if (vals)
	    printf("servicePrincipalName %s\n", vals[0]);
	vals = ldap_get_values(CTX2LP(context), m0, "userPrincipalName");
	if (vals)
	    printf("userPrincipalName %s\n", vals[0]);
	vals = ldap_get_values(CTX2LP(context), m0, "userAccountControl");
	if (vals)
	    printf("userAccountControl %s\n", vals[0]);
#endif
	entry->princ_expire_time = 0;
	if (mask & KADM5_PRINC_EXPIRE_TIME) {
	    vals = ldap_get_values(CTX2LP(context), m0, "accountExpires");
	    if (vals)
		entry->princ_expire_time = nt2unixtime(vals[0]);
	}
	entry->last_success = 0;
	if (mask & KADM5_LAST_SUCCESS) {
	    vals = ldap_get_values(CTX2LP(context), m0, "lastLogon");
	    if (vals)
		entry->last_success = nt2unixtime(vals[0]);
	}
	if (mask & KADM5_LAST_FAILED) {
	    vals = ldap_get_values(CTX2LP(context), m0, "badPasswordTime");
	    if (vals)
		entry->last_failed = nt2unixtime(vals[0]);
	}
	if (mask & KADM5_LAST_PWD_CHANGE) {
	    vals = ldap_get_values(CTX2LP(context), m0, "pwdLastSet");
	    if (vals)
		entry->last_pwd_change = nt2unixtime(vals[0]);
	}
	if (mask & KADM5_FAIL_AUTH_COUNT) {
	    vals = ldap_get_values(CTX2LP(context), m0, "badPwdCount");
	    if (vals)
		entry->fail_auth_count = atoi(vals[0]);
	}
 	if (mask & KADM5_ATTRIBUTES) {
	    vals = ldap_get_values(CTX2LP(context), m0, "userAccountControl");
	    if (vals) {
		uint32_t i;
		i = atoi(vals[0]);
		if (i & (UF_ACCOUNTDISABLE|UF_LOCKOUT))
		    entry->attributes |= KRB5_KDB_DISALLOW_ALL_TIX;
		if ((i & UF_DONT_REQUIRE_PREAUTH) == 0)
		    entry->attributes |= KRB5_KDB_REQUIRES_PRE_AUTH;
		if (i & UF_SMARTCARD_REQUIRED)
		    entry->attributes |= KRB5_KDB_REQUIRES_HW_AUTH;
		if ((i & UF_WORKSTATION_TRUST_ACCOUNT) == 0)
		    entry->attributes |= KRB5_KDB_DISALLOW_SVR;
	    }
	}
	if (mask & KADM5_KVNO) {
	    vals = ldap_get_values(CTX2LP(context), m0,
				   "msDS-KeyVersionNumber");
	    if (vals)
		entry->kvno = atoi(vals[0]);
	    else
		entry->kvno = 0;
	}
	ldap_msgfree(m);
    } else {
	return KADM5_UNK_PRINC;
    }

    if (mask & KADM5_PRINCIPAL)
	krb5_copy_principal(context->context, principal, &entry->principal);

    return 0;
 fail:
    return KADM5_RPC_ERROR;
#else
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_get_principals(void *server_handle,
			const char *expression,
			char ***principals,
			int *count)
{
    kadm5_ad_context *context = server_handle;

    /*
     * KADM5_PRINCIPAL | KADM5_KVNO | KADM5_ATTRIBUTES
     */

#ifdef OPENLDAP
    kadm5_ret_t ret;

    ret = ad_get_cred(context, NULL);
    if (ret)
	return ret;

    ret = _kadm5_ad_connect(server_handle);
    if (ret)
	return ret;

    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#else
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_get_privs(void *server_handle, uint32_t*privs)
{
    kadm5_ad_context *context = server_handle;
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
}

static kadm5_ret_t
kadm5_ad_modify_principal(void *server_handle,
			  kadm5_principal_ent_t entry,
			  uint32_t mask)
{
    kadm5_ad_context *context = server_handle;

    /*
     * KADM5_ATTRIBUTES
     * KRB5_KDB_DISALLOW_ALL_TIX (| KADM5_KVNO)
     */

#ifdef OPENLDAP
    LDAPMessage *m = NULL, *m0;
    kadm5_ret_t ret;
    char **attr = NULL;
    int attrlen = 0;
    char *p = NULL, *s = NULL, *q;
    char **vals;
    LDAPMod *attrs[4], rattrs[3], *a;
    char *uaf[2] = { NULL, NULL };
    char *kvno[2] = { NULL, NULL };
    char *tv[2] = { NULL, NULL };
    char *filter, *dn;
    int i;

    for (i = 0; i < sizeof(rattrs)/sizeof(rattrs[0]); i++)
	attrs[i] = &rattrs[i];
    attrs[i] = NULL;
    a = &rattrs[0];

    ret = _kadm5_ad_connect(server_handle);
    if (ret)
	return ret;

    if (mask & KADM5_KVNO)
	laddattr(&attr, &attrlen, "msDS-KeyVersionNumber");
    if (mask & KADM5_PRINC_EXPIRE_TIME)
	laddattr(&attr, &attrlen, "accountExpires");
    if (mask & KADM5_ATTRIBUTES)
	laddattr(&attr, &attrlen, "userAccountControl");
    laddattr(&attr, &attrlen, "distinguishedName");

    krb5_unparse_name(context->context, entry->principal, &p);

    s = strdup(p);

    q = strrchr(s, '@');
    if (q && (p != q && *(q - 1) != '\\'))
	*q = '\0';

    asprintf(&filter,
	     "(|(userPrincipalName=%s)(servicePrincipalName=%s))",
	     s, s);
    free(p);
    free(s);

    ret = ldap_search_s(CTX2LP(context), CTX2BASE(context),
			LDAP_SCOPE_SUBTREE,
			filter, attr, 0, &m);
    free(attr);
    free(filter);
    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;

    if (ldap_count_entries(CTX2LP(context), m) <= 0) {
	ret = KADM5_RPC_ERROR;
	goto out;
    }

    m0 = ldap_first_entry(CTX2LP(context), m);

    if (mask & KADM5_ATTRIBUTES) {
	int32_t i;

	vals = ldap_get_values(CTX2LP(context), m0, "userAccountControl");
	if (vals == NULL) {
	    ret = KADM5_RPC_ERROR;
	    goto out;
	}

	i = atoi(vals[0]);
	if (i == 0)
	    return KADM5_RPC_ERROR;

	if (entry->attributes & KRB5_KDB_DISALLOW_ALL_TIX)
	    i |= (UF_ACCOUNTDISABLE|UF_LOCKOUT);
	else
	    i &= ~(UF_ACCOUNTDISABLE|UF_LOCKOUT);
	if (entry->attributes & KRB5_KDB_REQUIRES_PRE_AUTH)
	    i &= ~UF_DONT_REQUIRE_PREAUTH;
	else
	    i |= UF_DONT_REQUIRE_PREAUTH;
	if (entry->attributes & KRB5_KDB_REQUIRES_HW_AUTH)
	    i |= UF_SMARTCARD_REQUIRED;
	else
	    i &= UF_SMARTCARD_REQUIRED;
	if (entry->attributes & KRB5_KDB_DISALLOW_SVR)
	    i &= ~UF_WORKSTATION_TRUST_ACCOUNT;
	else
	    i |= UF_WORKSTATION_TRUST_ACCOUNT;

	asprintf(&uaf[0], "%d", i);

	a->mod_op = LDAP_MOD_REPLACE;
	a->mod_type = "userAccountControl";
	a->mod_values = uaf;
	a++;
    }

    if (mask & KADM5_KVNO) {
	vals = ldap_get_values(CTX2LP(context), m0, "msDS-KeyVersionNumber");
	if (vals == NULL) {
	    entry->kvno = 0;
	} else {
	    asprintf(&kvno[0], "%d", entry->kvno);

	    a->mod_op = LDAP_MOD_REPLACE;
	    a->mod_type = "msDS-KeyVersionNumber";
	    a->mod_values = kvno;
	    a++;
	}
    }

    if (mask & KADM5_PRINC_EXPIRE_TIME) {
	long long wt;
	vals = ldap_get_values(CTX2LP(context), m0, "accountExpires");
	if (vals == NULL) {
	    ret = KADM5_RPC_ERROR;
	    goto out;
	}

	wt = unix2nttime(entry->princ_expire_time);

	asprintf(&tv[0], "%llu", wt);

	a->mod_op = LDAP_MOD_REPLACE;
	a->mod_type = "accountExpires";
	a->mod_values = tv;
	a++;
    }

    vals = ldap_get_values(CTX2LP(context), m0, "distinguishedName");
    if (vals == NULL) {
	ret = KADM5_RPC_ERROR;
	goto out;
    }
    dn = vals[0];

    attrs[a - &rattrs[0]] = NULL;

    ret = ldap_modify_s(CTX2LP(context), dn, attrs);
    if (check_ldap(context, ret))
	return KADM5_RPC_ERROR;

 out:
    if (m)
	ldap_msgfree(m);
    if (uaf[0])
	free(uaf[0]);
    if (kvno[0])
	free(kvno[0]);
    if (tv[0])
	free(tv[0]);
    return ret;
#else
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_randkey_principal(void *server_handle,
			   krb5_principal principal,
			   krb5_keyblock **keys,
			   int *n_keys)
{
    kadm5_ad_context *context = server_handle;

    /*
     * random key
     */

#ifdef OPENLDAP
    krb5_data result_code_string, result_string;
    int result_code, plen;
    kadm5_ret_t ret;
    char *password;

    *keys = NULL;
    *n_keys = 0;

    {
	char p[64];
	krb5_generate_random_block(p, sizeof(p));
	plen = base64_encode(p, sizeof(p), &password);
	if (plen < 0)
	    return ENOMEM;
    }

    ret = ad_get_cred(context, NULL);
    if (ret) {
	free(password);
	return ret;
    }

    krb5_data_zero (&result_code_string);
    krb5_data_zero (&result_string);

    ret = krb5_set_password_using_ccache (context->context,
					  context->ccache,
					  password,
					  principal,
					  &result_code,
					  &result_code_string,
					  &result_string);

    krb5_data_free (&result_code_string);
    krb5_data_free (&result_string);

    if (ret == 0) {

	*keys = malloc(sizeof(**keys) * 1);
	if (*keys == NULL) {
	    ret = ENOMEM;
	    goto out;
	}
	*n_keys = 1;

	ret = krb5_string_to_key(context->context,
				 ENCTYPE_ARCFOUR_HMAC_MD5,
				 password,
				 principal,
				 &(*keys)[0]);
	memset(password, 0, sizeof(password));
	if (ret) {
	    free(*keys);
	    *keys = NULL;
	    *n_keys = 0;
	    goto out;
	}
    }
    memset(password, 0, plen);
    free(password);
 out:
    return ret;
#else
    *keys = NULL;
    *n_keys = 0;

    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
#endif
}

static kadm5_ret_t
kadm5_ad_rename_principal(void *server_handle,
			  krb5_principal from,
			  krb5_principal to)
{
    kadm5_ad_context *context = server_handle;
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
}

static kadm5_ret_t
kadm5_ad_chpass_principal_with_key(void *server_handle,
				   krb5_principal princ,
				   int n_key_data,
				   krb5_key_data *key_data)
{
    kadm5_ad_context *context = server_handle;
    krb5_set_error_message(context->context, KADM5_RPC_ERROR, "Function not implemented");
    return KADM5_RPC_ERROR;
}

static void
set_funcs(kadm5_ad_context *c)
{
#define SET(C, F) (C)->funcs.F = kadm5_ad_ ## F
    SET(c, chpass_principal);
    SET(c, chpass_principal_with_key);
    SET(c, create_principal);
    SET(c, delete_principal);
    SET(c, destroy);
    SET(c, flush);
    SET(c, get_principal);
    SET(c, get_principals);
    SET(c, get_privs);
    SET(c, modify_principal);
    SET(c, randkey_principal);
    SET(c, rename_principal);
}

kadm5_ret_t
kadm5_ad_init_with_password_ctx(krb5_context context,
				const char *client_name,
				const char *password,
				const char *service_name,
				kadm5_config_params *realm_params,
				unsigned long struct_version,
				unsigned long api_version,
				void **server_handle)
{
    kadm5_ret_t ret;
    kadm5_ad_context *ctx;

    ctx = malloc(sizeof(*ctx));
    if(ctx == NULL)
	return ENOMEM;
    memset(ctx, 0, sizeof(*ctx));
    set_funcs(ctx);

    ctx->context = context;
    krb5_add_et_list (context, initialize_kadm5_error_table_r);

    ret = krb5_parse_name(ctx->context, client_name, &ctx->caller);
    if(ret) {
	free(ctx);
	return ret;
    }

    if(realm_params->mask & KADM5_CONFIG_REALM) {
	ret = 0;
	ctx->realm = strdup(realm_params->realm);
	if (ctx->realm == NULL)
	    ret = ENOMEM;
    } else
	ret = krb5_get_default_realm(ctx->context, &ctx->realm);
    if (ret) {
	free(ctx);
	return ret;
    }

    ctx->client_name = strdup(client_name);

    if(password != NULL && *password != '\0')
	ret = ad_get_cred(ctx, password);
    else
	ret = ad_get_cred(ctx, NULL);
    if(ret) {
	kadm5_ad_destroy(ctx);
	return ret;
    }

#ifdef OPENLDAP
    ret = _kadm5_ad_connect(ctx);
    if (ret) {
	kadm5_ad_destroy(ctx);
	return ret;
    }
#endif

    *server_handle = ctx;
    return 0;
}

kadm5_ret_t
kadm5_ad_init_with_password(const char *client_name,
			    const char *password,
			    const char *service_name,
			    kadm5_config_params *realm_params,
			    unsigned long struct_version,
			    unsigned long api_version,
			    void **server_handle)
{
    krb5_context context;
    kadm5_ret_t ret;
    kadm5_ad_context *ctx;

    ret = krb5_init_context(&context);
    if (ret)
	return ret;
    ret = kadm5_ad_init_with_password_ctx(context,
					  client_name,
					  password,
					  service_name,
					  realm_params,
					  struct_version,
					  api_version,
					  server_handle);
    if(ret) {
	krb5_free_context(context);
	return ret;
    }
    ctx = *server_handle;
    ctx->my_context = 1;
    return 0;
}
