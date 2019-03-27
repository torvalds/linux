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
 * $Id: print.c,v 1.4 2001/01/28 07:35:01 bp Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sysexits.h>

#include <cflib.h>

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>

#include "common.h"

int
cmd_print(int argc, char *argv[])
{
	struct smb_ctx sctx, *ctx = &sctx;
	smbfh fh;
	off_t offset;
	char buf[8192];
	char *filename;
	char fnamebuf[256];
	int error, opt, i, file, count;

	if (argc < 2)
		view_usage();
	if (smb_ctx_init(ctx, argc, argv, SMBL_SHARE, SMBL_SHARE, SMB_ST_PRINTER) != 0)
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
	if (optind + 1 >= argc)
		print_usage();
	filename = argv[optind + 1];

	if (strcmp(filename, "-") == 0) {
		file = 0;	/* stdin */
		filename = "stdin";
	} else {
		file = open(filename, O_RDONLY, 0);
		if (file < 0) {
			smb_error("could not open file %s\n", errno, filename);
			exit(1);
		}
	}

	if (smb_ctx_resolve(ctx) != 0)
		exit(1);
	error = smb_ctx_lookup(ctx, SMBL_SHARE, SMBLK_CREATE);
	if (error) {
		smb_error("could not login to server %s", error, ctx->ct_ssn.ioc_srvname);
		exit(1);
	}
	snprintf(fnamebuf, sizeof(fnamebuf), "%s_%s_%s", ctx->ct_ssn.ioc_user,
	    ctx->ct_ssn.ioc_srvname, filename);
	error = smb_smb_open_print_file(ctx, 0, 1, fnamebuf, &fh);
	if (error) {
		smb_error("could not open print job", error);
		exit(1);
	}
	offset = 0;
	error = 0;
	for(;;) {
		count = read(file, buf, sizeof(buf));
		if (count == 0)
			break;
		if (count < 0) {
			error = errno;
			smb_error("error reading input file\n", error);
			break;
		}
		i = smb_write(ctx, fh, offset, count, buf);
		if (i < 0) {
			error = errno;
			smb_error("error writing spool file\n", error);
			break;
		}
		if (i != count) {
			smb_error("incomplete write to spool file\n", 0);
			error = EIO;
			break;
		}
		offset += count;
	}
	close(file);
	error = smb_smb_close_print_file(ctx, fh);
	if (error)
		smb_error("an error while closing spool file\n", error);
	return error ? 1 : 0;
}


void
print_usage(void)
{
	printf(
	"usage: smbutil print [connection options] //user@server/share\n"
	);
	exit(1);
}

