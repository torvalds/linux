/*
 * Copyright (c) 2000-2002, Boris Popov
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
 * $Id: view.c,v 1.9 2002/02/20 09:26:42 bp Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/endian.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/iconv.h>
#include <err.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <sysexits.h>

#include <cflib.h>

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>
#include <netsmb/smb_rap.h>

#include "common.h"

static char *shtype[] = {
	"disk",
	"printer",
	"comm",		/* Communications device */
	"pipe",		/* IPC Inter process communication */
	"unknown"
};

int
cmd_view(int argc, char *argv[])
{
	struct smb_ctx sctx, *ctx = &sctx;
	struct smb_share_info_1 *rpbuf, *ep;
	char *cp;
	u_int32_t remark;
	u_int16_t type;
	int error, opt, bufsize, i, entries, total;
	

	if (argc < 2)
		view_usage();
	if (smb_ctx_init(ctx, argc, argv, SMBL_VC, SMBL_VC, SMB_ST_ANY) != 0)
		exit(1);
	if (smb_ctx_readrc(ctx) != 0)
		exit(1);
	if (smb_rc)
		rc_close(smb_rc);
	while ((opt = getopt(argc, argv, STDPARAM_OPT)) != EOF) {
		switch(opt){
		    case STDPARAM_ARGS:
			error = smb_ctx_opt(ctx, opt, optarg);
			if (error)
				exit(1);
			break;
		    default:
			view_usage();
			/*NOTREACHED*/
		}
	}
#ifdef APPLE
	if (loadsmbvfs())
		errx(EX_OSERR, "SMB filesystem is not available");
#endif
	smb_ctx_setshare(ctx, "IPC$", SMB_ST_ANY);
	if (smb_ctx_resolve(ctx) != 0)
		exit(1);
	error = smb_ctx_lookup(ctx, SMBL_SHARE, SMBLK_CREATE);
	if (error) {
		smb_error("could not login to server %s", error, ctx->ct_ssn.ioc_srvname);
		exit(1);
	}
	printf("Share        Type       Comment\n");
	printf("-------------------------------\n");
	bufsize = 0xffe0; /* samba notes win2k bug with 65535 */
	rpbuf = malloc(bufsize);
	error = smb_rap_NetShareEnum(ctx, 1, rpbuf, bufsize, &entries, &total);
	if (error &&
	    error != (SMB_ERROR_MORE_DATA | SMB_RAP_ERROR)) {
		smb_error("unable to list resources", error);
		exit(1);
	}
	for (ep = rpbuf, i = 0; i < entries; i++, ep++) {
		type = le16toh(ep->shi1_type);
		remark = le32toh(ep->shi1_remark);
		remark &= 0xFFFF;

		cp = (char*)rpbuf + remark;
		printf("%-12s %-10s %s\n", ep->shi1_netname,
		    shtype[min(type, sizeof shtype / sizeof(char *) - 1)],
		    remark ? nls_str_toloc(cp, cp) : "");
	}
	printf("\n%d shares listed from %d available\n", entries, total);
	free(rpbuf);
	return 0;
}


void
view_usage(void)
{
	printf(
	"usage: smbutil view [connection options] //[user@]server\n"
	);
	exit(1);
}

