/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_ALGS_H
#define __OTX2_CPT_ALGS_H

#include <crypto/hash.h>
#include <crypto/skcipher.h>
#include <crypto/aead.h>
#include "otx2_cpt_common.h"

#define OTX2_CPT_MAX_ENC_KEY_SIZE    32
#define OTX2_CPT_MAX_HASH_KEY_SIZE   64
#define OTX2_CPT_MAX_KEY_SIZE (OTX2_CPT_MAX_ENC_KEY_SIZE + \
			       OTX2_CPT_MAX_HASH_KEY_SIZE)
enum otx2_cpt_request_type {
	OTX2_CPT_ENC_DEC_REQ            = 0x1,
	OTX2_CPT_AEAD_ENC_DEC_REQ       = 0x2,
	OTX2_CPT_AEAD_ENC_DEC_NULL_REQ  = 0x3,
	OTX2_CPT_PASSTHROUGH_REQ	= 0x4
};

enum otx2_cpt_major_opcodes {
	OTX2_CPT_MAJOR_OP_MISC = 0x01,
	OTX2_CPT_MAJOR_OP_FC   = 0x33,
	OTX2_CPT_MAJOR_OP_HMAC = 0x35,
};

enum otx2_cpt_cipher_type {
	OTX2_CPT_CIPHER_NULL = 0x0,
	OTX2_CPT_DES3_CBC = 0x1,
	OTX2_CPT_DES3_ECB = 0x2,
	OTX2_CPT_AES_CBC  = 0x3,
	OTX2_CPT_AES_ECB  = 0x4,
	OTX2_CPT_AES_CFB  = 0x5,
	OTX2_CPT_AES_CTR  = 0x6,
	OTX2_CPT_AES_GCM  = 0x7,
	OTX2_CPT_AES_XTS  = 0x8
};

enum otx2_cpt_mac_type {
	OTX2_CPT_MAC_NULL = 0x0,
	OTX2_CPT_MD5      = 0x1,
	OTX2_CPT_SHA1     = 0x2,
	OTX2_CPT_SHA224   = 0x3,
	OTX2_CPT_SHA256   = 0x4,
	OTX2_CPT_SHA384   = 0x5,
	OTX2_CPT_SHA512   = 0x6,
	OTX2_CPT_GMAC     = 0x7
};

enum otx2_cpt_aes_key_len {
	OTX2_CPT_AES_128_BIT = 0x1,
	OTX2_CPT_AES_192_BIT = 0x2,
	OTX2_CPT_AES_256_BIT = 0x3
};

union otx2_cpt_encr_ctrl {
	u64 u;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 enc_cipher:4;
		u64 reserved_59:1;
		u64 aes_key:2;
		u64 iv_source:1;
		u64 mac_type:4;
		u64 reserved_49_51:3;
		u64 auth_input_type:1;
		u64 mac_len:8;
		u64 reserved_32_39:8;
		u64 encr_offset:16;
		u64 iv_offset:8;
		u64 auth_offset:8;
#else
		u64 auth_offset:8;
		u64 iv_offset:8;
		u64 encr_offset:16;
		u64 reserved_32_39:8;
		u64 mac_len:8;
		u64 auth_input_type:1;
		u64 reserved_49_51:3;
		u64 mac_type:4;
		u64 iv_source:1;
		u64 aes_key:2;
		u64 reserved_59:1;
		u64 enc_cipher:4;
#endif
	} e;
};

struct otx2_cpt_cipher {
	const char *name;
	u8 value;
};

struct otx2_cpt_fc_enc_ctx {
	union otx2_cpt_encr_ctrl enc_ctrl;
	u8 encr_key[32];
	u8 encr_iv[16];
};

union otx2_cpt_fc_hmac_ctx {
	struct {
		u8 ipad[64];
		u8 opad[64];
	} e;
	struct {
		u8 hmac_calc[64]; /* HMAC calculated */
		u8 hmac_recv[64]; /* HMAC received */
	} s;
};

struct otx2_cpt_fc_ctx {
	struct otx2_cpt_fc_enc_ctx enc;
	union otx2_cpt_fc_hmac_ctx hmac;
};

struct otx2_cpt_enc_ctx {
	u32 key_len;
	u8 enc_key[OTX2_CPT_MAX_KEY_SIZE];
	u8 cipher_type;
	u8 key_type;
	u8 enc_align_len;
	struct crypto_skcipher *fbk_cipher;
};

union otx2_cpt_offset_ctrl {
	u64 flags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 reserved:32;
		u64 enc_data_offset:16;
		u64 iv_offset:8;
		u64 auth_offset:8;
#else
		u64 auth_offset:8;
		u64 iv_offset:8;
		u64 enc_data_offset:16;
		u64 reserved:32;
#endif
	} e;
};

struct otx2_cpt_req_ctx {
	struct otx2_cpt_req_info cpt_req;
	union otx2_cpt_offset_ctrl ctrl_word;
	struct otx2_cpt_fc_ctx fctx;
	union {
		struct skcipher_request sk_fbk_req;
		struct aead_request fbk_req;
	};
};

struct otx2_cpt_sdesc {
	struct shash_desc shash;
};

struct otx2_cpt_aead_ctx {
	u8 key[OTX2_CPT_MAX_KEY_SIZE];
	struct crypto_shash *hashalg;
	struct otx2_cpt_sdesc *sdesc;
	struct crypto_aead *fbk_cipher;
	u8 *ipad;
	u8 *opad;
	u32 enc_key_len;
	u32 auth_key_len;
	u8 cipher_type;
	u8 mac_type;
	u8 key_type;
	u8 is_trunc_hmac;
	u8 enc_align_len;
};
int otx2_cpt_crypto_init(struct pci_dev *pdev, struct module *mod,
			 int num_queues, int num_devices);
void otx2_cpt_crypto_exit(struct pci_dev *pdev, struct module *mod);

#endif /* __OTX2_CPT_ALGS_H */
