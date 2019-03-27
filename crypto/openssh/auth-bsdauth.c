/* $OpenBSD: auth-bsdauth.c,v 1.15 2018/07/09 21:35:50 markus Exp $ */
/*
 * Copyright (c) 2001 Markus Friedl.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
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

#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

#include <stdarg.h>

#ifdef BSD_AUTH
#include "xmalloc.h"
#include "sshkey.h"
#include "sshbuf.h"
#include "hostfile.h"
#include "auth.h"
#include "log.h"
#ifdef GSSAPI
#include "ssh-gss.h"
#endif
#include "monitor_wrap.h"

static void *
bsdauth_init_ctx(Authctxt *authctxt)
{
	return authctxt;
}

int
bsdauth_query(void *ctx, char **name, char **infotxt,
   u_int *numprompts, char ***prompts, u_int **echo_on)
{
	Authctxt *authctxt = ctx;
	char *challenge = NULL;

	*infotxt = NULL;
	*numprompts = 0;
	*prompts = NULL;
	*echo_on = NULL;

	if (authctxt->as != NULL) {
		debug2("bsdauth_query: try reuse session");
		challenge = auth_getitem(authctxt->as, AUTHV_CHALLENGE);
		if (challenge == NULL) {
			auth_close(authctxt->as);
			authctxt->as = NULL;
		}
	}

	if (challenge == NULL) {
		debug2("bsdauth_query: new bsd auth session");
		debug3("bsdauth_query: style %s",
		    authctxt->style ? authctxt->style : "<default>");
		authctxt->as = auth_userchallenge(authctxt->user,
		    authctxt->style, "auth-ssh", &challenge);
		if (authctxt->as == NULL)
			challenge = NULL;
		debug2("bsdauth_query: <%s>", challenge ? challenge : "empty");
	}

	if (challenge == NULL)
		return -1;

	*name = xstrdup("");
	*infotxt = xstrdup("");
	*numprompts = 1;
	*prompts = xcalloc(*numprompts, sizeof(char *));
	*echo_on = xcalloc(*numprompts, sizeof(u_int));
	(*prompts)[0] = xstrdup(challenge);

	return 0;
}

int
bsdauth_respond(void *ctx, u_int numresponses, char **responses)
{
	Authctxt *authctxt = ctx;
	int authok;

	if (!authctxt->valid)
		return -1;

	if (authctxt->as == NULL)
		error("bsdauth_respond: no bsd auth session");

	if (numresponses != 1)
		return -1;

	authok = auth_userresponse(authctxt->as, responses[0], 0);
	authctxt->as = NULL;
	debug3("bsdauth_respond: <%s> = <%d>", responses[0], authok);

	return (authok == 0) ? -1 : 0;
}

static void
bsdauth_free_ctx(void *ctx)
{
	Authctxt *authctxt = ctx;

	if (authctxt && authctxt->as) {
		auth_close(authctxt->as);
		authctxt->as = NULL;
	}
}

KbdintDevice bsdauth_device = {
	"bsdauth",
	bsdauth_init_ctx,
	bsdauth_query,
	bsdauth_respond,
	bsdauth_free_ctx
};

KbdintDevice mm_bsdauth_device = {
	"bsdauth",
	bsdauth_init_ctx,
	mm_bsdauth_query,
	mm_bsdauth_respond,
	bsdauth_free_ctx
};
#endif
