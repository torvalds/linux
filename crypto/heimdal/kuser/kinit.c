/*
 * Copyright (c) 1997-2007 Kungliga Tekniska Högskolan
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

#include "kuser_locl.h"

#ifdef __APPLE__
#include <Security/Security.h>
#endif

#ifndef NO_NTLM
#include "heimntlm.h"
#endif

int forwardable_flag	= -1;
int proxiable_flag	= -1;
int renewable_flag	= -1;
int renew_flag		= 0;
int pac_flag		= -1;
int validate_flag	= 0;
int version_flag	= 0;
int help_flag		= 0;
int addrs_flag		= -1;
struct getarg_strings extra_addresses;
int anonymous_flag	= 0;
char *lifetime 		= NULL;
char *renew_life	= NULL;
char *server_str	= NULL;
char *cred_cache	= NULL;
char *start_str		= NULL;
static int switch_cache_flags = 1;
struct getarg_strings etype_str;
int use_keytab		= 0;
char *keytab_str	= NULL;
int do_afslog		= -1;
int fcache_version;
char *password_file	= NULL;
char *pk_user_id	= NULL;
int pk_enterprise_flag = 0;
struct hx509_certs_data *ent_user_id = NULL;
char *pk_x509_anchors	= NULL;
int pk_use_enckey	= 0;
static int canonicalize_flag = 0;
static int enterprise_flag = 0;
static int ok_as_delegate_flag = 0;
static int use_referrals_flag = 0;
static int windows_flag = 0;
#ifndef NO_NTLM
static char *ntlm_domain;
#endif


static struct getargs args[] = {
    /*
     * used by MIT
     * a: ~A
     * V: verbose
     * F: ~f
     * P: ~p
     * C: v4 cache name?
     * 5:
     *
     * old flags
     * 4:
     * 9:
     */
    { "afslog", 	0  , arg_flag, &do_afslog,
      NP_("obtain afs tokens", ""), NULL },

    { "cache", 		'c', arg_string, &cred_cache,
      NP_("credentials cache", ""), "cachename" },

    { "forwardable",	0, arg_negative_flag, &forwardable_flag,
      NP_("get tickets not forwardable", ""), NULL },

    { NULL,		'f', arg_flag, &forwardable_flag,
      NP_("get forwardable tickets", ""), NULL },

    { "keytab",         't', arg_string, &keytab_str,
      NP_("keytab to use", ""), "keytabname" },

    { "lifetime",	'l', arg_string, &lifetime,
      NP_("lifetime of tickets", ""), "time" },

    { "proxiable",	'p', arg_flag, &proxiable_flag,
      NP_("get proxiable tickets", ""), NULL },

    { "renew",          'R', arg_flag, &renew_flag,
      NP_("renew TGT", ""), NULL },

    { "renewable",	0,   arg_flag, &renewable_flag,
      NP_("get renewable tickets", ""), NULL },

    { "renewable-life",	'r', arg_string, &renew_life,
      NP_("renewable lifetime of tickets", ""), "time" },

    { "server", 	'S', arg_string, &server_str,
      NP_("server to get ticket for", ""), "principal" },

    { "start-time",	's', arg_string, &start_str,
      NP_("when ticket gets valid", ""), "time" },

    { "use-keytab",     'k', arg_flag, &use_keytab,
      NP_("get key from keytab", ""), NULL },

    { "validate",	'v', arg_flag, &validate_flag,
      NP_("validate TGT", ""), NULL },

    { "enctypes",	'e', arg_strings, &etype_str,
      NP_("encryption types to use", ""), "enctypes" },

    { "fcache-version", 0,   arg_integer, &fcache_version,
      NP_("file cache version to create", ""), NULL },

    { "addresses",	'A',   arg_negative_flag,	&addrs_flag,
      NP_("request a ticket with no addresses", ""), NULL },

    { "extra-addresses",'a', arg_strings,	&extra_addresses,
      NP_("include these extra addresses", ""), "addresses" },

    { "anonymous",	0,   arg_flag,	&anonymous_flag,
      NP_("request an anonymous ticket", ""), NULL },

    { "request-pac",	0,   arg_flag,	&pac_flag,
      NP_("request a Windows PAC", ""), NULL },

    { "password-file",	0,   arg_string, &password_file,
      NP_("read the password from a file", ""), NULL },

    { "canonicalize",0,   arg_flag, &canonicalize_flag,
      NP_("canonicalize client principal", ""), NULL },

    { "enterprise",0,   arg_flag, &enterprise_flag,
      NP_("parse principal as a KRB5-NT-ENTERPRISE name", ""), NULL },
#ifdef PKINIT
    { "pk-enterprise",	0,	arg_flag,	&pk_enterprise_flag,
      NP_("use enterprise name from certificate", ""), NULL },

    { "pk-user",	'C',	arg_string,	&pk_user_id,
      NP_("principal's public/private/certificate identifier", ""), "id" },

    { "x509-anchors",	'D',  arg_string, &pk_x509_anchors,
      NP_("directory with CA certificates", ""), "directory" },

    { "pk-use-enckey",	0,  arg_flag, &pk_use_enckey,
      NP_("Use RSA encrypted reply (instead of DH)", ""), NULL },
#endif
#ifndef NO_NTLM
    { "ntlm-domain",	0,  arg_string, &ntlm_domain,
      NP_("NTLM domain", ""), "domain" },
#endif

    { "change-default",  0,  arg_negative_flag, &switch_cache_flags,
      NP_("switch the default cache to the new credentials cache", ""), NULL },

    { "ok-as-delegate",	0,  arg_flag, &ok_as_delegate_flag,
      NP_("honor ok-as-delegate on tickets", ""), NULL },

    { "use-referrals",	0,  arg_flag, &use_referrals_flag,
      NP_("only use referrals, no dns canalisation", ""), NULL },

    { "windows",	0,  arg_flag, &windows_flag,
      NP_("get windows behavior", ""), NULL },

    { "version", 	0,   arg_flag, &version_flag, NULL, NULL },
    { "help",		0,   arg_flag, &help_flag, NULL, NULL }
};

static void
usage (int ret)
{
    arg_printusage_i18n (args,
			 sizeof(args)/sizeof(*args),
			 N_("Usage: ", ""),
			 NULL,
			 "[principal [command]]",
			 getarg_i18n);
    exit (ret);
}

static krb5_error_code
get_server(krb5_context context,
	   krb5_principal client,
	   const char *server,
	   krb5_principal *princ)
{
    krb5_const_realm realm;
    if(server)
	return krb5_parse_name(context, server, princ);

    realm = krb5_principal_get_realm(context, client);
    return krb5_make_principal(context, princ, realm,
			       KRB5_TGS_NAME, realm, NULL);
}

static int
renew_validate(krb5_context context,
	       int renew,
	       int validate,
	       krb5_ccache cache,
	       const char *server,
	       krb5_deltat life)
{
    krb5_error_code ret;
    krb5_creds in, *out = NULL;
    krb5_kdc_flags flags;

    memset(&in, 0, sizeof(in));

    ret = krb5_cc_get_principal(context, cache, &in.client);
    if(ret) {
	krb5_warn(context, ret, "krb5_cc_get_principal");
	return ret;
    }
    ret = get_server(context, in.client, server, &in.server);
    if(ret) {
	krb5_warn(context, ret, "get_server");
	goto out;
    }

    if (renew) {
	/*
	 * no need to check the error here, it's only to be
	 * friendly to the user
	 */
	krb5_get_credentials(context, KRB5_GC_CACHED, cache, &in, &out);
    }

    flags.i = 0;
    flags.b.renewable         = flags.b.renew = renew;
    flags.b.validate          = validate;

    if (forwardable_flag != -1)
	flags.b.forwardable       = forwardable_flag;
    else if (out)
	flags.b.forwardable 	  = out->flags.b.forwardable;

    if (proxiable_flag != -1)
	flags.b.proxiable         = proxiable_flag;
    else if (out)
	flags.b.proxiable 	  = out->flags.b.proxiable;

    if (anonymous_flag)
	flags.b.request_anonymous = anonymous_flag;
    if(life)
	in.times.endtime = time(NULL) + life;

    if (out) {
	krb5_free_creds (context, out);
	out = NULL;
    }


    ret = krb5_get_kdc_cred(context,
			    cache,
			    flags,
			    NULL,
			    NULL,
			    &in,
			    &out);
    if(ret) {
	krb5_warn(context, ret, "krb5_get_kdc_cred");
	goto out;
    }
    ret = krb5_cc_initialize(context, cache, in.client);
    if(ret) {
	krb5_free_creds (context, out);
	krb5_warn(context, ret, "krb5_cc_initialize");
	goto out;
    }
    ret = krb5_cc_store_cred(context, cache, out);

    if(ret == 0 && server == NULL) {
	/* only do this if it's a general renew-my-tgt request */
#ifndef NO_AFS
	if(do_afslog && k_hasafs())
	    krb5_afslog(context, cache, NULL, NULL);
#endif
    }

    krb5_free_creds (context, out);
    if(ret) {
	krb5_warn(context, ret, "krb5_cc_store_cred");
	goto out;
    }
out:
    krb5_free_cred_contents(context, &in);
    return ret;
}

#ifndef NO_NTLM

static krb5_error_code
store_ntlmkey(krb5_context context, krb5_ccache id,
	      const char *domain, struct ntlm_buf *buf)
{
    krb5_error_code ret;
    krb5_data data;
    char *name;

    asprintf(&name, "ntlm-key-%s", domain);
    if (name == NULL) {
	krb5_clear_error_message(context);
	return ENOMEM;
    }

    data.length = buf->length;
    data.data = buf->data;

    ret = krb5_cc_set_config(context, id, NULL, name, &data);
    free(name);
    return ret;
}
#endif

static krb5_error_code
get_new_tickets(krb5_context context,
		krb5_principal principal,
		krb5_ccache ccache,
		krb5_deltat ticket_life,
		int interactive)
{
    krb5_error_code ret;
    krb5_get_init_creds_opt *opt;
    krb5_creds cred;
    char passwd[256];
    krb5_deltat start_time = 0;
    krb5_deltat renew = 0;
    const char *renewstr = NULL;
    krb5_enctype *enctype = NULL;
    krb5_ccache tempccache;
#ifndef NO_NTLM
    struct ntlm_buf ntlmkey;
    memset(&ntlmkey, 0, sizeof(ntlmkey));
#endif
    passwd[0] = '\0';

    if (password_file) {
	FILE *f;

	if (strcasecmp("STDIN", password_file) == 0)
	    f = stdin;
	else
	    f = fopen(password_file, "r");
	if (f == NULL)
	    krb5_errx(context, 1, "Failed to open the password file %s",
		      password_file);

	if (fgets(passwd, sizeof(passwd), f) == NULL)
	    krb5_errx(context, 1,
		      N_("Failed to read password from file %s", ""),
		      password_file);
	if (f != stdin)
	    fclose(f);
	passwd[strcspn(passwd, "\n")] = '\0';
    }

#ifdef __APPLE__
    if (passwd[0] == '\0') {
	const char *realm;
	OSStatus osret;
	UInt32 length;
	void *buffer;
	char *name;

	realm = krb5_principal_get_realm(context, principal);

	ret = krb5_unparse_name_flags(context, principal,
				      KRB5_PRINCIPAL_UNPARSE_NO_REALM, &name);
	if (ret)
	    goto nopassword;

	osret = SecKeychainFindGenericPassword(NULL, strlen(realm), realm,
					       strlen(name), name,
					       &length, &buffer, NULL);
	free(name);
	if (osret == noErr && length < sizeof(passwd) - 1) {
	    memcpy(passwd, buffer, length);
	    passwd[length] = '\0';
	}
    nopassword:
	do { } while(0);
    }
#endif

    memset(&cred, 0, sizeof(cred));

    ret = krb5_get_init_creds_opt_alloc (context, &opt);
    if (ret)
	krb5_err(context, 1, ret, "krb5_get_init_creds_opt_alloc");

    krb5_get_init_creds_opt_set_default_flags(context, "kinit",
	krb5_principal_get_realm(context, principal), opt);

    if(forwardable_flag != -1)
	krb5_get_init_creds_opt_set_forwardable (opt, forwardable_flag);
    if(proxiable_flag != -1)
	krb5_get_init_creds_opt_set_proxiable (opt, proxiable_flag);
    if(anonymous_flag)
	krb5_get_init_creds_opt_set_anonymous (opt, anonymous_flag);
    if (pac_flag != -1)
	krb5_get_init_creds_opt_set_pac_request(context, opt,
						pac_flag ? TRUE : FALSE);
    if (canonicalize_flag)
	krb5_get_init_creds_opt_set_canonicalize(context, opt, TRUE);
    if (pk_enterprise_flag || enterprise_flag || canonicalize_flag || windows_flag)
	krb5_get_init_creds_opt_set_win2k(context, opt, TRUE);
    if (pk_user_id || ent_user_id || anonymous_flag) {
	ret = krb5_get_init_creds_opt_set_pkinit(context, opt,
						 principal,
						 pk_user_id,
						 pk_x509_anchors,
						 NULL,
						 NULL,
						 pk_use_enckey ? 2 : 0 |
						 anonymous_flag ? 4 : 0,
						 krb5_prompter_posix,
						 NULL,
						 passwd);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_get_init_creds_opt_set_pkinit");
	if (ent_user_id)
	    krb5_get_init_creds_opt_set_pkinit_user_certs(context, opt, ent_user_id);
    }

    if (addrs_flag != -1)
	krb5_get_init_creds_opt_set_addressless(context, opt,
						addrs_flag ? FALSE : TRUE);

    if (renew_life == NULL && renewable_flag)
	renewstr = "1 month";
    if (renew_life)
	renewstr = renew_life;
    if (renewstr) {
	renew = parse_time (renewstr, "s");
	if (renew < 0)
	    errx (1, "unparsable time: %s", renewstr);

	krb5_get_init_creds_opt_set_renew_life (opt, renew);
    }

    if(ticket_life != 0)
	krb5_get_init_creds_opt_set_tkt_life (opt, ticket_life);

    if(start_str) {
	int tmp = parse_time (start_str, "s");
	if (tmp < 0)
	    errx (1, N_("unparsable time: %s", ""), start_str);

	start_time = tmp;
    }

    if(etype_str.num_strings) {
	int i;

	enctype = malloc(etype_str.num_strings * sizeof(*enctype));
	if(enctype == NULL)
	    errx(1, "out of memory");
	for(i = 0; i < etype_str.num_strings; i++) {
	    ret = krb5_string_to_enctype(context,
					 etype_str.strings[i],
					 &enctype[i]);
	    if(ret)
		errx(1, "unrecognized enctype: %s", etype_str.strings[i]);
	}
	krb5_get_init_creds_opt_set_etype_list(opt, enctype,
					       etype_str.num_strings);
    }

    if(use_keytab || keytab_str) {
	krb5_keytab kt;
	if(keytab_str)
	    ret = krb5_kt_resolve(context, keytab_str, &kt);
	else
	    ret = krb5_kt_default(context, &kt);
	if (ret)
	    krb5_err (context, 1, ret, "resolving keytab");
	ret = krb5_get_init_creds_keytab (context,
					  &cred,
					  principal,
					  kt,
					  start_time,
					  server_str,
					  opt);
	krb5_kt_close(context, kt);
    } else if (pk_user_id || ent_user_id || anonymous_flag) {
	ret = krb5_get_init_creds_password (context,
					    &cred,
					    principal,
					    passwd,
					    krb5_prompter_posix,
					    NULL,
					    start_time,
					    server_str,
					    opt);
    } else if (!interactive) {
	krb5_warnx(context, "Not interactive, failed to get initial ticket");
	krb5_get_init_creds_opt_free(context, opt);
	return 0;
    } else {

	if (passwd[0] == '\0') {
	    char *p, *prompt;

	    krb5_unparse_name (context, principal, &p);
	    asprintf (&prompt, N_("%s's Password: ", ""), p);
	    free (p);

	    if (UI_UTIL_read_pw_string(passwd, sizeof(passwd)-1, prompt, 0)){
		memset(passwd, 0, sizeof(passwd));
		exit(1);
	    }
	    free (prompt);
	}


	ret = krb5_get_init_creds_password (context,
					    &cred,
					    principal,
					    passwd,
					    krb5_prompter_posix,
					    NULL,
					    start_time,
					    server_str,
					    opt);
    }
    krb5_get_init_creds_opt_free(context, opt);
#ifndef NO_NTLM
    if (ntlm_domain && passwd[0])
	heim_ntlm_nt_key(passwd, &ntlmkey);
#endif
    memset(passwd, 0, sizeof(passwd));

    switch(ret){
    case 0:
	break;
    case KRB5_LIBOS_PWDINTR: /* don't print anything if it was just C-c:ed */
	exit(1);
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
    case KRB5KRB_AP_ERR_MODIFIED:
    case KRB5KDC_ERR_PREAUTH_FAILED:
	krb5_errx(context, 1, N_("Password incorrect", ""));
	break;
    case KRB5KRB_AP_ERR_V4_REPLY:
	krb5_errx(context, 1, N_("Looks like a Kerberos 4 reply", ""));
	break;
    default:
	krb5_err(context, 1, ret, "krb5_get_init_creds");
    }

    if(ticket_life != 0) {
	if(abs(cred.times.endtime - cred.times.starttime - ticket_life) > 30) {
	    char life[64];
	    unparse_time_approx(cred.times.endtime - cred.times.starttime,
				life, sizeof(life));
	    krb5_warnx(context, N_("NOTICE: ticket lifetime is %s", ""), life);
	}
    }
    if(renew_life) {
	if(abs(cred.times.renew_till - cred.times.starttime - renew) > 30) {
	    char life[64];
	    unparse_time_approx(cred.times.renew_till - cred.times.starttime,
				life, sizeof(life));
	    krb5_warnx(context,
		       N_("NOTICE: ticket renewable lifetime is %s", ""),
		       life);
	}
    }

    ret = krb5_cc_new_unique(context, krb5_cc_get_type(context, ccache),
			     NULL, &tempccache);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_new_unique");

    ret = krb5_cc_initialize (context, tempccache, cred.client);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_initialize");

    ret = krb5_cc_store_cred (context, tempccache, &cred);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_store_cred");

    krb5_free_cred_contents (context, &cred);

    ret = krb5_cc_move(context, tempccache, ccache);
    if (ret)
	krb5_err (context, 1, ret, "krb5_cc_move");

    if (switch_cache_flags)
	krb5_cc_switch(context, ccache);

#ifndef NO_NTLM
    if (ntlm_domain && ntlmkey.data)
	store_ntlmkey(context, ccache, ntlm_domain, &ntlmkey);
#endif

    if (ok_as_delegate_flag || windows_flag || use_referrals_flag) {
	unsigned char d = 0;
	krb5_data data;

	if (ok_as_delegate_flag || windows_flag)
	    d |= 1;
	if (use_referrals_flag || windows_flag)
	    d |= 2;

	data.length = 1;
	data.data = &d;

	krb5_cc_set_config(context, ccache, NULL, "realm-config", &data);
    }


    if (enctype)
	free(enctype);

    return 0;
}

static time_t
ticket_lifetime(krb5_context context, krb5_ccache cache,
		krb5_principal client, const char *server)
{
    krb5_creds in_cred, *cred;
    krb5_error_code ret;
    time_t timeout;

    memset(&in_cred, 0, sizeof(in_cred));

    ret = krb5_cc_get_principal(context, cache, &in_cred.client);
    if(ret) {
	krb5_warn(context, ret, "krb5_cc_get_principal");
	return 0;
    }
    ret = get_server(context, in_cred.client, server, &in_cred.server);
    if(ret) {
	krb5_free_principal(context, in_cred.client);
	krb5_warn(context, ret, "get_server");
	return 0;
    }

    ret = krb5_get_credentials(context, KRB5_GC_CACHED,
			       cache, &in_cred, &cred);
    krb5_free_principal(context, in_cred.client);
    krb5_free_principal(context, in_cred.server);
    if(ret) {
	krb5_warn(context, ret, "krb5_get_credentials");
	return 0;
    }
    timeout = cred->times.endtime - cred->times.starttime;
    if (timeout < 0)
	timeout = 0;
    krb5_free_creds(context, cred);
    return timeout;
}

struct renew_ctx {
    krb5_context context;
    krb5_ccache  ccache;
    krb5_principal principal;
    krb5_deltat ticket_life;
};

static time_t
renew_func(void *ptr)
{
    struct renew_ctx *ctx = ptr;
    krb5_error_code ret;
    time_t expire;
    int new_tickets = 0;

    if (renewable_flag) {
	ret = renew_validate(ctx->context, renewable_flag, validate_flag,
			     ctx->ccache, server_str, ctx->ticket_life);
	if (ret)
	    new_tickets = 1;
    } else
	new_tickets = 1;

    if (new_tickets)
	get_new_tickets(ctx->context, ctx->principal,
			ctx->ccache, ctx->ticket_life, 0);

#ifndef NO_AFS
    if(do_afslog && k_hasafs())
	krb5_afslog(ctx->context, ctx->ccache, NULL, NULL);
#endif

    expire = ticket_lifetime(ctx->context, ctx->ccache, ctx->principal,
			     server_str) / 2;
    return expire + 1;
}

int
main (int argc, char **argv)
{
    krb5_error_code ret;
    krb5_context context;
    krb5_ccache  ccache;
    krb5_principal principal;
    int optidx = 0;
    krb5_deltat ticket_life = 0;
    int parseflags = 0;

    setprogname (argv[0]);

    setlocale (LC_ALL, "");
    bindtextdomain ("heimdal_kuser", HEIMDAL_LOCALEDIR);
    textdomain("heimdal_kuser");

    ret = krb5_init_context (&context);
    if (ret == KRB5_CONFIG_BADFORMAT)
	errx (1, "krb5_init_context failed to parse configuration file");
    else if (ret)
	errx(1, "krb5_init_context failed: %d", ret);

    if(getarg(args, sizeof(args) / sizeof(args[0]), argc, argv, &optidx))
	usage(1);

    if (help_flag)
	usage (0);

    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    argc -= optidx;
    argv += optidx;

    if (canonicalize_flag || enterprise_flag)
	parseflags |= KRB5_PRINCIPAL_PARSE_ENTERPRISE;

    if (pk_enterprise_flag) {
	ret = krb5_pk_enterprise_cert(context, pk_user_id,
				      argv[0], &principal,
				      &ent_user_id);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_pk_enterprise_certs");

	pk_user_id = NULL;

    } else if (anonymous_flag) {

	ret = krb5_make_principal(context, &principal, argv[0],
				  KRB5_WELLKNOWN_NAME, KRB5_ANON_NAME,
				  NULL);
	if (ret)
	    krb5_err(context, 1, ret, "krb5_make_principal");
	krb5_principal_set_type(context, principal, KRB5_NT_WELLKNOWN);

    } else {
	if (argv[0]) {
	    ret = krb5_parse_name_flags (context, argv[0], parseflags,
					 &principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_parse_name");
	} else {
	    ret = krb5_get_default_principal (context, &principal);
	    if (ret)
		krb5_err (context, 1, ret, "krb5_get_default_principal");
	}
    }

    if(fcache_version)
	krb5_set_fcache_version(context, fcache_version);

    if(renewable_flag == -1)
	/* this seems somewhat pointless, but whatever */
	krb5_appdefault_boolean(context, "kinit",
				krb5_principal_get_realm(context, principal),
				"renewable", FALSE, &renewable_flag);
    if(do_afslog == -1)
	krb5_appdefault_boolean(context, "kinit",
				krb5_principal_get_realm(context, principal),
				"afslog", TRUE, &do_afslog);

    if(cred_cache)
	ret = krb5_cc_resolve(context, cred_cache, &ccache);
    else {
	if(argc > 1) {
	    char s[1024];
	    ret = krb5_cc_new_unique(context, NULL, NULL, &ccache);
	    if(ret)
		krb5_err(context, 1, ret, "creating cred cache");
	    snprintf(s, sizeof(s), "%s:%s",
		     krb5_cc_get_type(context, ccache),
		     krb5_cc_get_name(context, ccache));
	    setenv("KRB5CCNAME", s, 1);
	} else {
	    ret = krb5_cc_cache_match(context, principal, &ccache);
	    if (ret) {
		const char *type;
		ret = krb5_cc_default (context, &ccache);
		if (ret)
		    krb5_err (context, 1, ret, N_("resolving credentials cache", ""));

		/*
		 * Check if the type support switching, and we do,
		 * then do that instead over overwriting the current
		 * default credential
		 */
		type = krb5_cc_get_type(context, ccache);
		if (krb5_cc_support_switch(context, type)) {
		    krb5_cc_close(context, ccache);
		    ret = krb5_cc_new_unique(context, type, NULL, &ccache);
		}
	    }
	}
    }
    if (ret)
	krb5_err (context, 1, ret, N_("resolving credentials cache", ""));

#ifndef NO_AFS
    if(argc > 1 && k_hasafs ())
	k_setpag();
#endif

    if (lifetime) {
	int tmp = parse_time (lifetime, "s");
	if (tmp < 0)
	    errx (1, N_("unparsable time: %s", ""), lifetime);

	ticket_life = tmp;
    }

    if(addrs_flag == 0 && extra_addresses.num_strings > 0)
	krb5_errx(context, 1,
		  N_("specifying both extra addresses and "
		     "no addresses makes no sense", ""));
    {
	int i;
	krb5_addresses addresses;
	memset(&addresses, 0, sizeof(addresses));
	for(i = 0; i < extra_addresses.num_strings; i++) {
	    ret = krb5_parse_address(context, extra_addresses.strings[i],
				     &addresses);
	    if (ret == 0) {
		krb5_add_extra_addresses(context, &addresses);
		krb5_free_addresses(context, &addresses);
	    }
	}
	free_getarg_strings(&extra_addresses);
    }

    if(renew_flag || validate_flag) {
	ret = renew_validate(context, renew_flag, validate_flag,
			     ccache, server_str, ticket_life);
	exit(ret != 0);
    }

    get_new_tickets(context, principal, ccache, ticket_life, 1);

#ifndef NO_AFS
    if(do_afslog && k_hasafs())
	krb5_afslog(context, ccache, NULL, NULL);
#endif
    if(argc > 1) {
	struct renew_ctx ctx;
	time_t timeout;

	timeout = ticket_lifetime(context, ccache, principal, server_str) / 2;

	ctx.context = context;
	ctx.ccache = ccache;
	ctx.principal = principal;
	ctx.ticket_life = ticket_life;

	ret = simple_execvp_timed(argv[1], argv+1,
				  renew_func, &ctx, timeout);
#define EX_NOEXEC	126
#define EX_NOTFOUND	127
	if(ret == EX_NOEXEC)
	    krb5_warnx(context, N_("permission denied: %s", ""), argv[1]);
	else if(ret == EX_NOTFOUND)
	    krb5_warnx(context, N_("command not found: %s", ""), argv[1]);

	krb5_cc_destroy(context, ccache);
#ifndef NO_AFS
	if(k_hasafs())
	    k_unlog();
#endif
    } else {
	krb5_cc_close (context, ccache);
	ret = 0;
    }
    krb5_free_principal(context, principal);
    krb5_free_context (context);
    return ret;
}
