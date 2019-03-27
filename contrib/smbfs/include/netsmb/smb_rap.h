/*
 * Copyright (c) 2000-2001, Boris Popov
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
 * $Id: smb_rap.h,v 1.3 2001/04/10 05:37:22 bp Exp $
 */
#ifndef _NETSMB_SMB_RAP_H_
#define _NETSMB_SMB_RAP_H_

struct smb_rap {
	char *		r_sparam;
	char *		r_nparam;
	char *		r_sdata;
	char *		r_ndata;
	char *		r_pbuf;		/* rq parameters */
	int		r_plen;		/* rq param len */
	char *		r_npbuf;
	char *		r_dbuf;		/* rq data */
	int		r_dlen;		/* rq data len */
	char *		r_ndbuf;
	u_int32_t	r_result;
	char *		r_rcvbuf;
	int		r_rcvbuflen;
	int		r_entries;
};

struct smb_share_info_1 {
	char		shi1_netname[13];
	char		shi1_pad;
	u_int16_t	shi1_type;
	u_int32_t	shi1_remark;		/* char * */
};

__BEGIN_DECLS

int  smb_rap_create(int, const char *, const char *, struct smb_rap **);
void smb_rap_done(struct smb_rap *);
int  smb_rap_request(struct smb_rap *, struct smb_ctx *);
int  smb_rap_setNparam(struct smb_rap *, long);
int  smb_rap_setPparam(struct smb_rap *, void *);
int  smb_rap_error(struct smb_rap *, int);

int  smb_rap_NetShareEnum(struct smb_ctx *, int, void *, int, int *, int *);

__END_DECLS

#endif /* _NETSMB_SMB_RAP_H_ */
