/* $OpenBSD: gss-serv-krb5.c,v 1.9 2018/07/09 21:37:55 markus Exp $ */

/*
 * Copyright (c) 2001-2003 Simon Wilkinson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR `AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "includes.h"

#ifdef GSSAPI
#ifdef KRB5

#include <sys/types.h>

#include <stdarg.h>
#include <string.h>

#include "xmalloc.h"
#include "sshkey.h"
#include "hostfile.h"
#include "auth.h"
#include "log.h"
#include "misc.h"
#include "servconf.h"

#include "ssh-gss.h"

extern ServerOptions options;

#ifdef HEIMDAL
# include <krb5.h>
#endif
#ifdef HAVE_GSSAPI_KRB5_H
# include <gssapi_krb5.h>
#elif HAVE_GSSAPI_GSSAPI_KRB5_H
# include <gssapi/gssapi_krb5.h>
#endif

static krb5_context krb_context = NULL;

/* Initialise the krb5 library, for the stuff that GSSAPI won't do */

static int
ssh_gssapi_krb5_init(void)
{
	krb5_error_code problem;

	if (krb_context != NULL)
		return 1;

	problem = krb5_init_context(&krb_context);
	if (problem) {
		logit("Cannot initialize krb5 context");
		return 0;
	}

	return 1;
}

/* Check if this user is OK to login. This only works with krb5 - other
 * GSSAPI mechanisms will need their own.
 * Returns true if the user is OK to log in, otherwise returns 0
 */

static int
ssh_gssapi_krb5_userok(ssh_gssapi_client *client, char *name)
{
	krb5_principal princ;
	int retval;
	const char *errmsg;

	if (ssh_gssapi_krb5_init() == 0)
		return 0;

	if ((retval = krb5_parse_name(krb_context, client->exportedname.value,
	    &princ))) {
		errmsg = krb5_get_error_message(krb_context, retval);
		logit("krb5_parse_name(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return 0;
	}
	if (krb5_kuserok(krb_context, princ, name)) {
		retval = 1;
		logit("Authorized to %s, krb5 principal %s (krb5_kuserok)",
		    name, (char *)client->displayname.value);
	} else
		retval = 0;

	krb5_free_principal(krb_context, princ);
	return retval;
}


/* This writes out any forwarded credentials from the structure populated
 * during userauth. Called after we have setuid to the user */

static void
ssh_gssapi_krb5_storecreds(ssh_gssapi_client *client)
{
	krb5_ccache ccache;
	krb5_error_code problem;
	krb5_principal princ;
	OM_uint32 maj_status, min_status;
	int len;
	const char *errmsg;

	if (client->creds == NULL) {
		debug("No credentials stored");
		return;
	}

	if (ssh_gssapi_krb5_init() == 0)
		return;

#ifdef HEIMDAL
# ifdef HAVE_KRB5_CC_NEW_UNIQUE
	if ((problem = krb5_cc_new_unique(krb_context, krb5_fcc_ops.prefix,
	    NULL, &ccache)) != 0) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_cc_new_unique(): %.100s", errmsg);
# else
	if ((problem = krb5_cc_gen_new(krb_context, &krb5_fcc_ops, &ccache))) {
	    logit("krb5_cc_gen_new(): %.100s",
		krb5_get_err_text(krb_context, problem));
# endif
		krb5_free_error_message(krb_context, errmsg);
		return;
	}
#else
	if ((problem = ssh_krb5_cc_gen(krb_context, &ccache))) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("ssh_krb5_cc_gen(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return;
	}
#endif	/* #ifdef HEIMDAL */

	if ((problem = krb5_parse_name(krb_context,
	    client->exportedname.value, &princ))) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_parse_name(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		return;
	}

	if ((problem = krb5_cc_initialize(krb_context, ccache, princ))) {
		errmsg = krb5_get_error_message(krb_context, problem);
		logit("krb5_cc_initialize(): %.100s", errmsg);
		krb5_free_error_message(krb_context, errmsg);
		krb5_free_principal(krb_context, princ);
		krb5_cc_destroy(krb_context, ccache);
		return;
	}

	krb5_free_principal(krb_context, princ);

	if ((maj_status = gss_krb5_copy_ccache(&min_status,
	    client->creds, ccache))) {
		logit("gss_krb5_copy_ccache() failed");
		krb5_cc_destroy(krb_context, ccache);
		return;
	}

	client->store.filename = xstrdup(krb5_cc_get_name(krb_context, ccache));
	client->store.envvar = "KRB5CCNAME";
	len = strlen(client->store.filename) + 6;
	client->store.envval = xmalloc(len);
	snprintf(client->store.envval, len, "FILE:%s", client->store.filename);

#ifdef USE_PAM
	if (options.use_pam)
		do_pam_putenv(client->store.envvar, client->store.envval);
#endif

	krb5_cc_close(krb_context, ccache);

	return;
}

ssh_gssapi_mech gssapi_kerberos_mech = {
	"toWM5Slw5Ew8Mqkay+al2g==",
	"Kerberos",
	{9, "\x2A\x86\x48\x86\xF7\x12\x01\x02\x02"},
	NULL,
	&ssh_gssapi_krb5_userok,
	NULL,
	&ssh_gssapi_krb5_storecreds
};

#endif /* KRB5 */

#endif /* GSSAPI */
