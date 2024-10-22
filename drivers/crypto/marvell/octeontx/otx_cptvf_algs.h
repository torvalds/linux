/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTX CPT driver
 *
 * Copyright (C) 2019 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OTX_CPT_ALGS_H
#define __OTX_CPT_ALGS_H

#include <crypto/hash.h>
#include "otx_cpt_common.h"

#define OTX_CPT_MAX_ENC_KEY_SIZE    32
#define OTX_CPT_MAX_HASH_KEY_SIZE   64
#define OTX_CPT_MAX_KEY_SIZE (OTX_CPT_MAX_ENC_KEY_SIZE + \
			      OTX_CPT_MAX_HASH_KEY_SIZE)
enum otx_cpt_request_type {
	OTX_CPT_ENC_DEC_REQ            = 0x1,
	OTX_CPT_AEAD_ENC_DEC_REQ       = 0x2,
	OTX_CPT_AEAD_ENC_DEC_NULL_REQ  = 0x3,
	OTX_CPT_PASSTHROUGH_REQ	       = 0x4
};

enum otx_cpt_major_opcodes {
	OTX_CPT_MAJOR_OP_MISC = 0x01,
	OTX_CPT_MAJOR_OP_FC   = 0x33,
	OTX_CPT_MAJOR_OP_HMAC = 0x35,
};

enum otx_cpt_req_type {
		OTX_CPT_AE_CORE_REQ,
		OTX_CPT_SE_CORE_REQ
};

enum otx_cpt_cipher_type {
	OTX_CPT_CIPHER_NULL = 0x0,
	OTX_CPT_DES3_CBC = 0x1,
	OTX_CPT_DES3_ECB = 0x2,
	OTX_CPT_AES_CBC  = 0x3,
	OTX_CPT_AES_ECB  = 0x4,
	OTX_CPT_AES_CFB  = 0x5,
	OTX_CPT_AES_CTR  = 0x6,
	OTX_CPT_AES_GCM  = 0x7,
	OTX_CPT_AES_XTS  = 0x8
};

enum otx_cpt_mac_type {
	OTX_CPT_MAC_NULL = 0x0,
	OTX_CPT_MD5      = 0x1,
	OTX_CPT_SHA1     = 0x2,
	OTX_CPT_SHA224   = 0x3,
	OTX_CPT_SHA256   = 0x4,
	OTX_CPT_SHA384   = 0x5,
	OTX_CPT_SHA512   = 0x6,
	OTX_CPT_GMAC     = 0x7
};

enum otx_cpt_aes_key_len {
	OTX_CPT_AES_128_BIT = 0x1,
	OTX_CPT_AES_192_BIT = 0x2,
	OTX_CPT_AES_256_BIT = 0x3
};

union otx_cpt_encr_ctrl {
	__be64 flags;
	u64 cflags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 enc_cipher:4;
		u64 reserved1:1;
		u64 aes_key:2;
		u64 iv_source:1;
		u64 mac_type:4;
		u64 reserved2:3;
		u64 auth_input_type:1;
		u64 mac_len:8;
		u64 reserved3:8;
		u64 encr_offset:16;
		u64 iv_offset:8;
		u64 auth_offset:8;
#else
		u64 auth_offset:8;
		u64 iv_offset:8;
		u64 encr_offset:16;
		u64 reserved3:8;
		u64 mac_len:8;
		u64 auth_input_type:1;
		u64 reserved2:3;
		u64 mac_type:4;
		u64 iv_source:1;
		u64 aes_key:2;
		u64 reserved1:1;
		u64 enc_cipher:4;
#endif
	} e;
};

struct otx_cpt_cipher {
	const char *name;
	u8 value;
};

struct otx_cpt_enc_context {
	union otx_cpt_encr_ctrl enc_ctrl;
	u8 encr_key[32];
	u8 encr_iv[16];
};

union otx_cpt_fchmac_ctx {
	struct {
		u8 ipad[64];
		u8 opad[64];
	} e;
	struct {
		u8 hmac_calc[64]; /* HMAC calculated */
		u8 hmac_recv[64]; /* HMAC received */
	} s;
};

struct otx_cpt_fc_ctx {
	struct otx_cpt_enc_context enc;
	union otx_cpt_fchmac_ctx hmac;
};

struct otx_cpt_enc_ctx {
	u32 key_len;
	u8 enc_key[OTX_CPT_MAX_KEY_SIZE];
	u8 cipher_type;
	u8 key_type;
};

struct otx_cpt_des3_ctx {
	u32 key_len;
	u8 des3_key[OTX_CPT_MAX_KEY_SIZE];
};

union otx_cpt_offset_ctrl_word {
	__be64 flags;
	u64 cflags;
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

struct otx_cpt_req_ctx {
	struct otx_cpt_req_info cpt_req;
	union otx_cpt_offset_ctrl_word ctrl_word;
	struct otx_cpt_fc_ctx fctx;
};

struct otx_cpt_sdesc {
	struct shash_desc shash;
};

struct otx_cpt_aead_ctx {
	u8 key[OTX_CPT_MAX_KEY_SIZE];
	struct crypto_shash *hashalg;
	struct otx_cpt_sdesc *sdesc;
	u8 *ipad;
	u8 *opad;
	u32 enc_key_len;
	u32 auth_key_len;
	u8 cipher_type;
	u8 mac_type;
	u8 key_type;
	u8 is_trunc_hmac;
};
int otx_cpt_crypto_init(struct pci_dev *pdev, struct module *mod,
			enum otx_cptpf_type pf_type,
			enum otx_cptvf_type engine_type,
			int num_queues, int num_devices);
void otx_cpt_crypto_exit(struct pci_dev *pdev, struct module *mod,
			 enum otx_cptvf_type engine_type);

#endif /* __OTX_CPT_ALGS_H */
