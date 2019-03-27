/*
 * Copyright (c) 2003, PADL Software Pty Ltd.
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
 * 3. Neither the name of PADL Software nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PADL SOFTWARE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL PADL SOFTWARE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

#ifndef GSSAPI_CFX_H_
#define GSSAPI_CFX_H_ 1

/*
 * Implementation of draft-ietf-krb-wg-gssapi-cfx-01.txt
 */

typedef struct gss_cfx_mic_token_desc_struct {
	u_char TOK_ID[2]; /* 04 04 */
	u_char Flags;
	u_char Filler[5];
	u_char SND_SEQ[8];
} gss_cfx_mic_token_desc, *gss_cfx_mic_token;

typedef struct gss_cfx_wrap_token_desc_struct {
	u_char TOK_ID[2]; /* 04 05 */
	u_char Flags;
	u_char Filler;
	u_char EC[2];
	u_char RRC[2];
	u_char SND_SEQ[8];
} gss_cfx_wrap_token_desc, *gss_cfx_wrap_token;

typedef struct gss_cfx_delete_token_desc_struct {
	u_char TOK_ID[2]; /* 05 04 */
	u_char Flags;
	u_char Filler[5];
	u_char SND_SEQ[8];
} gss_cfx_delete_token_desc, *gss_cfx_delete_token;

#endif /* GSSAPI_CFX_H_ */
