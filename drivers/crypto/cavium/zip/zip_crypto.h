/***********************license start************************************
 * Copyright (c) 2003-2017 Cavium, Inc.
 * All rights reserved.
 *
 * License: one of 'Cavium License' or 'GNU General Public License Version 2'
 *
 * This file is provided under the terms of the Cavium License (see below)
 * or under the terms of GNU General Public License, Version 2, as
 * published by the Free Software Foundation. When using or redistributing
 * this file, you may do so under either license.
 *
 * Cavium License:  Redistribution and use in source and binary forms, with
 * or without modification, are permitted provided that the following
 * conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 *  * Neither the name of Cavium Inc. nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * This Software, including technical data, may be subject to U.S. export
 * control laws, including the U.S. Export Administration Act and its
 * associated regulations, and may be subject to export or import
 * regulations in other countries.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS
 * OR WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH
 * RESPECT TO THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY
 * REPRESENTATION OR DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT
 * DEFECTS, AND CAVIUM SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY)
 * WARRANTIES OF TITLE, MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A
 * PARTICULAR PURPOSE, LACK OF VIRUSES, ACCURACY OR COMPLETENESS, QUIET
 * ENJOYMENT, QUIET POSSESSION OR CORRESPONDENCE TO DESCRIPTION. THE
 * ENTIRE  RISK ARISING OUT OF USE OR PERFORMANCE OF THE SOFTWARE LIES
 * WITH YOU.
 ***********************license end**************************************/

#ifndef __ZIP_CRYPTO_H__
#define __ZIP_CRYPTO_H__

#include <linux/crypto.h>
#include <crypto/internal/scompress.h>
#include "common.h"
#include "zip_deflate.h"
#include "zip_inflate.h"

struct zip_kernel_ctx {
	struct zip_operation zip_comp;
	struct zip_operation zip_decomp;
};

int  zip_alloc_comp_ctx_deflate(struct crypto_tfm *tfm);
int  zip_alloc_comp_ctx_lzs(struct crypto_tfm *tfm);
void zip_free_comp_ctx(struct crypto_tfm *tfm);
int  zip_comp_compress(struct crypto_tfm *tfm,
		       const u8 *src, unsigned int slen,
		       u8 *dst, unsigned int *dlen);
int  zip_comp_decompress(struct crypto_tfm *tfm,
			 const u8 *src, unsigned int slen,
			 u8 *dst, unsigned int *dlen);

void *zip_alloc_scomp_ctx_deflate(struct crypto_scomp *tfm);
void *zip_alloc_scomp_ctx_lzs(struct crypto_scomp *tfm);
void  zip_free_scomp_ctx(struct crypto_scomp *tfm, void *zip_ctx);
int   zip_scomp_compress(struct crypto_scomp *tfm,
			 const u8 *src, unsigned int slen,
			 u8 *dst, unsigned int *dlen, void *ctx);
int   zip_scomp_decompress(struct crypto_scomp *tfm,
			   const u8 *src, unsigned int slen,
			   u8 *dst, unsigned int *dlen, void *ctx);
#endif
