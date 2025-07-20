// SPDX-License-Identifier: BSD-3-Clause
/* rfc3962 Advanced Encryption Standard (AES) Encryption for Kerberos 5
 *
 * Parts borrowed from net/sunrpc/auth_gss/.
 */
/*
 * COPYRIGHT (c) 2008
 * The Regents of the University of Michigan
 * ALL RIGHTS RESERVED
 *
 * Permission is granted to use, copy, create derivative works
 * and redistribute this software and such derivative works
 * for any purpose, so long as the name of The University of
 * Michigan is not used in any advertising or publicity
 * pertaining to the use of distribution of this software
 * without specific, written prior authorization.  If the
 * above copyright notice or any other identification of the
 * University of Michigan is included in any copy of any
 * portion of this software, then the disclaimer below must
 * also be included.
 *
 * THIS SOFTWARE IS PROVIDED AS IS, WITHOUT REPRESENTATION
 * FROM THE UNIVERSITY OF MICHIGAN AS TO ITS FITNESS FOR ANY
 * PURPOSE, AND WITHOUT WARRANTY BY THE UNIVERSITY OF
 * MICHIGAN OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING
 * WITHOUT LIMITATION THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE
 * REGENTS OF THE UNIVERSITY OF MICHIGAN SHALL NOT BE LIABLE
 * FOR ANY DAMAGES, INCLUDING SPECIAL, INDIRECT, INCIDENTAL, OR
 * CONSEQUENTIAL DAMAGES, WITH RESPECT TO ANY CLAIM ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OF THE SOFTWARE, EVEN
 * IF IT HAS BEEN OR IS HEREAFTER ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES.
 */

/*
 * Copyright (C) 1998 by the FundsXpress, INC.
 *
 * All rights reserved.
 *
 * Export of this software from the United States of America may require
 * a specific license from the United States Government.  It is the
 * responsibility of any person or organization contemplating export to
 * obtain such a license before exporting.
 *
 * WITHIN THAT CONSTRAINT, permission to use, copy, modify, and
 * distribute this software and its documentation for any purpose and
 * without fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright notice and
 * this permission notice appear in supporting documentation, and that
 * the name of FundsXpress. not be used in advertising or publicity pertaining
 * to distribution of the software without specific, written prior
 * permission.  FundsXpress makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is" without express
 * or implied warranty.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "internal.h"

const struct krb5_enctype krb5_aes128_cts_hmac_sha1_96 = {
	.etype		= KRB5_ENCTYPE_AES128_CTS_HMAC_SHA1_96,
	.ctype		= KRB5_CKSUMTYPE_HMAC_SHA1_96_AES128,
	.name		= "aes128-cts-hmac-sha1-96",
	.encrypt_name	= "krb5enc(hmac(sha1),cts(cbc(aes)))",
	.cksum_name	= "hmac(sha1)",
	.hash_name	= "sha1",
	.derivation_enc	= "cts(cbc(aes))",
	.key_bytes	= 16,
	.key_len	= 16,
	.Kc_len		= 16,
	.Ke_len		= 16,
	.Ki_len		= 16,
	.block_len	= 16,
	.conf_len	= 16,
	.cksum_len	= 12,
	.hash_len	= 20,
	.prf_len	= 16,
	.keyed_cksum	= true,
	.random_to_key	= NULL, /* Identity */
	.profile	= &rfc3961_simplified_profile,
};

const struct krb5_enctype krb5_aes256_cts_hmac_sha1_96 = {
	.etype		= KRB5_ENCTYPE_AES256_CTS_HMAC_SHA1_96,
	.ctype		= KRB5_CKSUMTYPE_HMAC_SHA1_96_AES256,
	.name		= "aes256-cts-hmac-sha1-96",
	.encrypt_name	= "krb5enc(hmac(sha1),cts(cbc(aes)))",
	.cksum_name	= "hmac(sha1)",
	.hash_name	= "sha1",
	.derivation_enc	= "cts(cbc(aes))",
	.key_bytes	= 32,
	.key_len	= 32,
	.Kc_len		= 32,
	.Ke_len		= 32,
	.Ki_len		= 32,
	.block_len	= 16,
	.conf_len	= 16,
	.cksum_len	= 12,
	.hash_len	= 20,
	.prf_len	= 16,
	.keyed_cksum	= true,
	.random_to_key	= NULL, /* Identity */
	.profile	= &rfc3961_simplified_profile,
};
