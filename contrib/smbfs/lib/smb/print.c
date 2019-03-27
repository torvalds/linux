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
 * $Id: print.c,v 1.4 2001/04/16 04:33:01 bp Exp $
 */
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

/*#include <netnb/netbios.h>*/

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>
#include <cflib.h>

int
smb_smb_open_print_file(struct smb_ctx *ctx, int setuplen, int mode,
	char *ident, smbfh *fhp)
{
	struct smb_rq *rqp;
	struct mbdata *mbp;
	int error;

	error = smb_rq_init(ctx, SMB_COM_OPEN_PRINT_FILE, 2, &rqp);
	if (error)
		return error;
	mbp = smb_rq_getrequest(rqp);
	mb_put_uint16le(mbp, setuplen);
	mb_put_uint16le(mbp, mode);
	smb_rq_wend(rqp);
	mb_put_uint8(mbp, SMB_DT_ASCII);
	smb_rq_dstring(mbp, ident);
	error = smb_rq_simple(rqp);
	if (!error) {
		mbp = smb_rq_getreply(rqp);
		mb_get_uint16(mbp, fhp);
	}
	smb_rq_done(rqp);
	return error;
}

int
smb_smb_close_print_file(struct smb_ctx *ctx, smbfh fh)
{
	struct smb_rq *rqp;
	struct mbdata *mbp;
	int error;

	error = smb_rq_init(ctx, SMB_COM_CLOSE_PRINT_FILE, 0, &rqp);
	if (error)
		return error;
	mbp = smb_rq_getrequest(rqp);
	mb_put_mem(mbp, (char*)&fh, 2);
	smb_rq_wend(rqp);
	error = smb_rq_simple(rqp);
	smb_rq_done(rqp);
	return error;
}
