/*
 * API for Atmel Secure Protocol Layers Improved Performances (SPLIP)
 *
 * Copyright (C) 2016 Atmel Corporation
 *
 * Author: Cyrille Pitchen <cyrille.pitchen@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is based on drivers/mtd/spi-nor/fsl-quadspi.c from Freescale.
 */

#ifndef __ATMEL_AUTHENC_H__
#define __ATMEL_AUTHENC_H__

#ifdef CONFIG_CRYPTO_DEV_ATMEL_AUTHENC

#include <crypto/authenc.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include "atmel-sha-regs.h"

struct atmel_aes_dev;
typedef int (*atmel_aes_authenc_fn_t)(struct atmel_aes_dev *, int, bool);

struct atmel_sha_authenc_ctx;

bool atmel_sha_authenc_is_ready(void);
unsigned int atmel_sha_authenc_get_reqsize(void);

struct atmel_sha_authenc_ctx *atmel_sha_authenc_spawn(unsigned long mode);
void atmel_sha_authenc_free(struct atmel_sha_authenc_ctx *auth);
int atmel_sha_authenc_setkey(struct atmel_sha_authenc_ctx *auth,
			     const u8 *key, unsigned int keylen,
			     u32 *flags);

int atmel_sha_authenc_schedule(struct ahash_request *req,
			       struct atmel_sha_authenc_ctx *auth,
			       atmel_aes_authenc_fn_t cb,
			       struct atmel_aes_dev *dd);
int atmel_sha_authenc_init(struct ahash_request *req,
			   struct scatterlist *assoc, unsigned int assoclen,
			   unsigned int textlen,
			   atmel_aes_authenc_fn_t cb,
			   struct atmel_aes_dev *dd);
int atmel_sha_authenc_final(struct ahash_request *req,
			    u32 *digest, unsigned int digestlen,
			    atmel_aes_authenc_fn_t cb,
			    struct atmel_aes_dev *dd);
void  atmel_sha_authenc_abort(struct ahash_request *req);

#endif /* CONFIG_CRYPTO_DEV_ATMEL_AUTHENC */

#endif /* __ATMEL_AUTHENC_H__ */
