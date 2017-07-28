/*
 * Copyright (C) 2016 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef _CPTVF_ALGS_H_
#define _CPTVF_ALGS_H_

#include "request_manager.h"

#define MAX_DEVICES 16
#define MAJOR_OP_FC 0x33
#define MAX_ENC_KEY_SIZE 32
#define MAX_HASH_KEY_SIZE 64
#define MAX_KEY_SIZE (MAX_ENC_KEY_SIZE + MAX_HASH_KEY_SIZE)
#define CONTROL_WORD_LEN 8
#define KEY2_OFFSET 48

#define DMA_MODE_FLAG(dma_mode) \
	(((dma_mode) == DMA_GATHER_SCATTER) ? (1 << 7) : 0)

enum req_type {
	AE_CORE_REQ,
	SE_CORE_REQ,
};

enum cipher_type {
	DES3_CBC = 0x1,
	DES3_ECB = 0x2,
	AES_CBC = 0x3,
	AES_ECB = 0x4,
	AES_CFB = 0x5,
	AES_CTR = 0x6,
	AES_GCM = 0x7,
	AES_XTS = 0x8
};

enum aes_type {
	AES_128_BIT = 0x1,
	AES_192_BIT = 0x2,
	AES_256_BIT = 0x3
};

union encr_ctrl {
	u64 flags;
	struct {
#if defined(__BIG_ENDIAN_BITFIELD)
		u64 enc_cipher:4;
		u64 reserved1:1;
		u64 aes_key:2;
		u64 iv_source:1;
		u64 hash_type:4;
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
		u64 hash_type:4;
		u64 iv_source:1;
		u64 aes_key:2;
		u64 reserved1:1;
		u64 enc_cipher:4;
#endif
	} e;
};

struct cvm_cipher {
	const char *name;
	u8 value;
};

struct enc_context {
	union encr_ctrl enc_ctrl;
	u8 encr_key[32];
	u8 encr_iv[16];
};

struct fchmac_context {
	u8 ipad[64];
	u8 opad[64]; /* or OPAD */
};

struct fc_context {
	struct enc_context enc;
	struct fchmac_context hmac;
};

struct cvm_enc_ctx {
	u32 key_len;
	u8 enc_key[MAX_KEY_SIZE];
	u8 cipher_type:4;
	u8 key_type:2;
};

struct cvm_des3_ctx {
	u32 key_len;
	u8 des3_key[MAX_KEY_SIZE];
};

struct cvm_req_ctx {
	struct cpt_request_info cpt_req;
	u64 control_word;
	struct fc_context fctx;
};

int cptvf_do_request(void *cptvf, struct cpt_request_info *req);
#endif /*_CPTVF_ALGS_H_*/
