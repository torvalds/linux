/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2003-2006, Advanced Micro Devices, Inc.
 */

#ifndef _GEODE_AES_H_
#define _GEODE_AES_H_

/* driver logic flags */
#define AES_MODE_ECB 0
#define AES_MODE_CBC 1

#define AES_DIR_DECRYPT 0
#define AES_DIR_ENCRYPT 1

#define AES_FLAGS_HIDDENKEY (1 << 0)

/* Register definitions */

#define AES_CTRLA_REG  0x0000

#define AES_CTRL_START     0x01
#define AES_CTRL_DECRYPT   0x00
#define AES_CTRL_ENCRYPT   0x02
#define AES_CTRL_WRKEY     0x04
#define AES_CTRL_DCA       0x08
#define AES_CTRL_SCA       0x10
#define AES_CTRL_CBC       0x20

#define AES_INTR_REG  0x0008

#define AES_INTRA_PENDING (1 << 16)
#define AES_INTRB_PENDING (1 << 17)

#define AES_INTR_PENDING  (AES_INTRA_PENDING | AES_INTRB_PENDING)
#define AES_INTR_MASK     0x07

#define AES_SOURCEA_REG   0x0010
#define AES_DSTA_REG      0x0014
#define AES_LENA_REG      0x0018
#define AES_WRITEKEY0_REG 0x0030
#define AES_WRITEIV0_REG  0x0040

/*  A very large counter that is used to gracefully bail out of an
 *  operation in case of trouble
 */

#define AES_OP_TIMEOUT    0x50000

struct geode_aes_tfm_ctx {
	u8 key[AES_KEYSIZE_128];
	union {
		struct crypto_skcipher *skcipher;
		struct crypto_cipher *cip;
	} fallback;
	u32 keylen;
};

#endif
