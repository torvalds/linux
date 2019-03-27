/*
 * Copyright (c) 2000, Boris Popov
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
 * $Id: login.c,v 1.6 2001/08/22 03:33:38 bp Exp $
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <err.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <sysexits.h>

#include <cflib.h>

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>

#include "common.h"


int
cmd_login(int argc, char *argv[])
{
	struct smb_ctx sctx, *ctx = &sctx;
	int error, opt, setprimary = 0, level;

	if (argc < 2)
		login_usage();
	if (smb_ctx_init(ctx, argc, argv, SMBL_VC, SMBL_SHARE, SMB_ST_ANY) != 0)
		exit(1);
	if (smb_ctx_readrc(ctx) != 0)
		exit(1);
	if (smb_rc)
		rc_close(smb_rc);
	while ((opt = getopt(argc, argv, STDPARAM_OPT"D")) != EOF) {
		switch(opt){
		    case STDPARAM_ARGS:
			error = smb_ctx_opt(ctx, opt, optarg);
			if (error)
				exit(1);
			break;
		    case 'D':
			setprimary = 1;
			break;
		    default:
			login_usage();
			/*NOTREACHED*/
		}
	}
#ifdef APPLE
	if (loadsmbvfs())
		errx(EX_OSERR, "SMB filesystem is not available");
#endif
	if (smb_ctx_resolve(ctx) != 0)
		exit(1);
	level = ctx->ct_parsedlevel;
	error = smb_ctx_lookup(ctx, level, 0);
	if (error == 0) {
		smb_error("connection already exists", error);
		exit(0);
	}
	error = smb_ctx_lookup(ctx, level, SMBLK_CREATE);
	if (error) {
		smb_error("could not login to server %s", error, ctx->ct_ssn.ioc_srvname);
		exit(1);
	}
	switch (level) {
	    case SMBL_VC:
		opt = SMBV_PERMANENT;
		break;
	    case SMBL_SHARE:
		opt = SMBS_PERMANENT;
		break;
	    default:
		smb_error("unknown connection level %d", 0, level);
		exit(1);
	}
	error = smb_ctx_setflags(ctx, level, opt, opt);
	if (error && error != EACCES) {
		smb_error("Can't make connection permanent", error);
		exit(1);
	}
	printf("Connected to %s%s%s\n", ctx->ct_ssn.ioc_user,
	    level == SMBL_SHARE ? "@" : "",
	    level == SMBL_SHARE ? ctx->ct_sh.ioc_share : "");
	return 0;
}

int
cmd_logout(int argc, char *argv[])
{
	struct smb_ctx sctx, *ctx = &sctx;
	int error, opt, level;

	if (argc < 2)
		logout_usage();
	if (smb_ctx_init(ctx, argc, argv, SMBL_VC, SMBL_SHARE, SMB_ST_ANY) != 0)
		exit(1);
	if (smb_ctx_readrc(ctx) != 0)
		exit(1);
	if (smb_rc)
		rc_close(smb_rc);
	while ((opt = getopt(argc, argv, STDPARAM_OPT)) != EOF){
		switch (opt) {
		    case STDPARAM_ARGS:
			error = smb_ctx_opt(ctx, opt, optarg);
			if (error)
				exit(1);
			break;
		    default:
			logout_usage();
			/*NOTREACHED*/
		}
	}
#ifdef APPLE
	error = loadsmbvfs();
	if (error)
		errx(EX_OSERR, "SMB filesystem is not available");
#endif
	ctx->ct_ssn.ioc_opt &= ~SMBVOPT_CREATE;
	ctx->ct_sh.ioc_opt &= ~SMBSOPT_CREATE;
	if (smb_ctx_resolve(ctx) != 0)
		exit(1);
	level = ctx->ct_parsedlevel;
	error = smb_ctx_lookup(ctx, level, 0);
	if (error == ENOENT) {
/*		ctx->ct_ssn.ioc_opt |= SMBCOPT_SINGLE;
		error = smb_ctx_login(ctx);
		if (error == ENOENT) {
			ctx->ct_ssn.ioc_opt |= SMBCOPT_PRIVATE;
			error = smb_ctx_login(ctx);
			if (error == ENOENT) {
				ctx->ct_ssn.ioc_opt &= ~SMBCOPT_SINGLE;
				error = smb_ctx_login(ctx);
			}
		}*/
		if (error) {
			smb_error("There is no connection to %s", error, ctx->ct_ssn.ioc_srvname);
			exit(1);
		}
	}
	if (error)
		exit(1);
	switch (level) {
	    case SMBL_VC:
		opt = SMBV_PERMANENT;
		break;
	    case SMBL_SHARE:
		opt = SMBS_PERMANENT;
		break;
	    default:
		smb_error("unknown connection level %d", 0, level);
		exit(1);
	}
	error = smb_ctx_setflags(ctx, level, opt, 0);
	if (error && error != EACCES) {
		smb_error("Can't release connection", error);
		exit(1);
	}
	printf("Connection unmarked as permanent and will be closed when possible\n");
	exit(0);
}

void
login_usage(void)
{
	printf(
	"usage: smbutil login [-E cs1:cs2] [-I host] [-L locale] [-M crights:srights]\n"
	"               [-N cowner:cgroup/sowner:sgroup] [-P]\n"
	"               [-R retrycount] [-T timeout]\n"
	"               [-W workgroup] //user@server\n");
	exit(1);
}

void
logout_usage(void)
{
	printf("usage: smbutil logout [user@server]\n");
	exit(1);
}
