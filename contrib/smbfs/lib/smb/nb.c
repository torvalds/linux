/*
 * Copyright (c) 2000, 2001 Boris Popov
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by Boris Popov.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: nb.c,v 1.4 2001/04/16 04:33:01 bp Exp $
 * $FreeBSD$
 */
#include <sys/param.h>
#include <sys/socket.h>

#include <ctype.h>
#include <netdb.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <cflib.h>

#include <netsmb/netbios.h>
#include <netsmb/smb_lib.h>
#include <netsmb/nb_lib.h>

int
nb_ctx_create(struct nb_ctx **ctxpp)
{
	struct nb_ctx *ctx;

	ctx = malloc(sizeof(struct nb_ctx));
	if (ctx == NULL)
		return ENOMEM;
	bzero(ctx, sizeof(struct nb_ctx));
	ctx->nb_nmbtcpport = NMB_TCP_PORT;
	ctx->nb_smbtcpport = SMB_TCP_PORT;

	*ctxpp = ctx;
	return 0;
}

void
nb_ctx_done(struct nb_ctx *ctx)
{
	if (ctx == NULL)
		return;
	if (ctx->nb_scope)
		free(ctx->nb_scope);
}

int
nb_ctx_setns(struct nb_ctx *ctx, const char *addr)
{
	if (addr == NULL || addr[0] == 0)
		return EINVAL;
	if (ctx->nb_nsname)
		free(ctx->nb_nsname);
	if ((ctx->nb_nsname = strdup(addr)) == NULL)
		return ENOMEM;
	return 0;
}

int
nb_ctx_setscope(struct nb_ctx *ctx, const char *scope)
{
	size_t slen = strlen(scope);

	if (slen >= 128) {
		smb_error("scope '%s' is too long", 0, scope);
		return ENAMETOOLONG;
	}
	if (ctx->nb_scope)
		free(ctx->nb_scope);
	ctx->nb_scope = malloc(slen + 1);
	if (ctx->nb_scope == NULL)
		return ENOMEM;
	nls_str_upper(ctx->nb_scope, scope);
	return 0;
}

int
nb_ctx_resolve(struct nb_ctx *ctx)
{
	struct sockaddr *sap;
	int error;

	ctx->nb_flags &= ~NBCF_RESOLVED;

	if (ctx->nb_nsname == NULL) {
		ctx->nb_ns.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	} else {
		error = nb_resolvehost_in(ctx->nb_nsname, &sap, ctx->nb_smbtcpport);
		if (error) {
			smb_error("can't resolve %s", error, ctx->nb_nsname);
			return error;
		}
		if (sap->sa_family != AF_INET) {
			smb_error("unsupported address family %d", 0, sap->sa_family);
			return EINVAL;
		}
		bcopy(sap, &ctx->nb_ns, sizeof(ctx->nb_ns));
		free(sap);
	}
	ctx->nb_ns.sin_port = htons(ctx->nb_nmbtcpport);
	ctx->nb_ns.sin_family = AF_INET;
	ctx->nb_ns.sin_len = sizeof(ctx->nb_ns);
	ctx->nb_flags |= NBCF_RESOLVED;
	return 0;
}

/*
 * used level values:
 * 0 - default
 * 1 - server
 */
int
nb_ctx_readrcsection(struct rcfile *rcfile, struct nb_ctx *ctx,
	const char *sname, int level)
{
	char *p;
	int error;

	if (level > 1)
		return EINVAL;
	rc_getint(rcfile, sname, "nbtimeout", &ctx->nb_timo);
	rc_getstringptr(rcfile, sname, "nbns", &p);
	if (p) {
		error = nb_ctx_setns(ctx, p);
		if (error) {
			smb_error("invalid address specified in the section %s", 0, sname);
			return error;
		}
	}
	rc_getstringptr(rcfile, sname, "nbscope", &p);
	if (p)
		nb_ctx_setscope(ctx, p);
	return 0;
}

static const char *nb_err_rcode[] = {
	"bad request/response format",
	"NBNS server failure",
	"no such name",
	"unsupported request",
	"request rejected",
	"name already registered"
};

static const char *nb_err[] = {
	"host not found",
	"too many redirects",
	"invalid response",
	"NETBIOS name too long",
	"no interface to broadcast on and no NBNS server specified"
};

const char *
nb_strerror(int error)
{
	if (error == 0)
		return NULL;
	if (error <= NBERR_ACTIVE)
		return nb_err_rcode[error - 1];
	else if (error >= NBERR_HOSTNOTFOUND && error < NBERR_MAX)
		return nb_err[error - NBERR_HOSTNOTFOUND];
	else
		return NULL;
}

