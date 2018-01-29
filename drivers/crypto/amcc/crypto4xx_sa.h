/**
 * AMCC SoC PPC4xx Crypto Driver
 *
 * Copyright (c) 2008 Applied Micro Circuits Corporation.
 * All rights reserved. James Hsiao <jhsiao@amcc.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file defines the security context
 * associate format.
 */

#ifndef __CRYPTO4XX_SA_H__
#define __CRYPTO4XX_SA_H__

#define AES_IV_SIZE				16

/**
 * Contents of Dynamic Security Association (SA) with all possible fields
 */
union dynamic_sa_contents {
	struct {
		u32 arc4_state_ptr:1;
		u32 arc4_ij_ptr:1;
		u32 state_ptr:1;
		u32 iv3:1;
		u32 iv2:1;
		u32 iv1:1;
		u32 iv0:1;
		u32 seq_num_mask3:1;
		u32 seq_num_mask2:1;
		u32 seq_num_mask1:1;
		u32 seq_num_mask0:1;
		u32 seq_num1:1;
		u32 seq_num0:1;
		u32 spi:1;
		u32 outer_size:5;
		u32 inner_size:5;
		u32 key_size:4;
		u32 cmd_size:4;
	} bf;
	u32 w;
} __attribute__((packed));

#define DIR_OUTBOUND				0
#define DIR_INBOUND				1
#define SA_OP_GROUP_BASIC			0
#define SA_OPCODE_ENCRYPT			0
#define SA_OPCODE_DECRYPT			0
#define SA_OPCODE_ENCRYPT_HASH			1
#define SA_OPCODE_HASH_DECRYPT			1
#define SA_OPCODE_HASH				3
#define SA_CIPHER_ALG_DES			0
#define SA_CIPHER_ALG_3DES			1
#define SA_CIPHER_ALG_ARC4			2
#define SA_CIPHER_ALG_AES			3
#define SA_CIPHER_ALG_KASUMI			4
#define SA_CIPHER_ALG_NULL			15

#define SA_HASH_ALG_MD5				0
#define SA_HASH_ALG_SHA1			1
#define SA_HASH_ALG_GHASH			12
#define SA_HASH_ALG_CBC_MAC			14
#define SA_HASH_ALG_NULL			15
#define SA_HASH_ALG_SHA1_DIGEST_SIZE		20

#define SA_LOAD_HASH_FROM_SA			0
#define SA_LOAD_HASH_FROM_STATE			2
#define SA_NOT_LOAD_HASH			3
#define SA_LOAD_IV_FROM_SA			0
#define SA_LOAD_IV_FROM_INPUT			1
#define SA_LOAD_IV_FROM_STATE			2
#define SA_LOAD_IV_GEN_IV			3

#define SA_PAD_TYPE_CONSTANT			2
#define SA_PAD_TYPE_ZERO			3
#define SA_PAD_TYPE_TLS				5
#define SA_PAD_TYPE_DTLS			5
#define SA_NOT_SAVE_HASH			0
#define SA_SAVE_HASH				1
#define SA_NOT_SAVE_IV				0
#define SA_SAVE_IV				1
#define SA_HEADER_PROC				1
#define SA_NO_HEADER_PROC			0

union sa_command_0 {
	struct {
		u32 scatter:1;
		u32 gather:1;
		u32 save_hash_state:1;
		u32 save_iv:1;
		u32 load_hash_state:2;
		u32 load_iv:2;
		u32 digest_len:4;
		u32 hdr_proc:1;
		u32 extend_pad:1;
		u32 stream_cipher_pad:1;
		u32 rsv:1;
		u32 hash_alg:4;
		u32 cipher_alg:4;
		u32 pad_type:2;
		u32 op_group:2;
		u32 dir:1;
		u32 opcode:3;
	} bf;
	u32 w;
} __attribute__((packed));

#define CRYPTO_MODE_ECB				0
#define CRYPTO_MODE_CBC				1
#define CRYPTO_MODE_OFB				2
#define CRYPTO_MODE_CFB				3
#define CRYPTO_MODE_CTR				4

#define CRYPTO_FEEDBACK_MODE_NO_FB		0
#define CRYPTO_FEEDBACK_MODE_64BIT_OFB		0
#define CRYPTO_FEEDBACK_MODE_8BIT_CFB		1
#define CRYPTO_FEEDBACK_MODE_1BIT_CFB		2
#define CRYPTO_FEEDBACK_MODE_128BIT_CFB		3

#define SA_AES_KEY_LEN_128			2
#define SA_AES_KEY_LEN_192			3
#define SA_AES_KEY_LEN_256			4

#define SA_REV2					1
/**
 * The follow defines bits sa_command_1
 * In Basic hash mode  this bit define simple hash or hmac.
 * In IPsec mode, this bit define muting control.
 */
#define SA_HASH_MODE_HASH			0
#define SA_HASH_MODE_HMAC			1
#define SA_MC_ENABLE				0
#define SA_MC_DISABLE				1
#define SA_NOT_COPY_HDR				0
#define SA_COPY_HDR				1
#define SA_NOT_COPY_PAD				0
#define SA_COPY_PAD				1
#define SA_NOT_COPY_PAYLOAD			0
#define SA_COPY_PAYLOAD				1
#define SA_EXTENDED_SN_OFF			0
#define SA_EXTENDED_SN_ON			1
#define SA_SEQ_MASK_OFF				0
#define SA_SEQ_MASK_ON				1

union sa_command_1 {
	struct {
		u32 crypto_mode31:1;
		u32 save_arc4_state:1;
		u32 arc4_stateful:1;
		u32 key_len:5;
		u32 hash_crypto_offset:8;
		u32 sa_rev:2;
		u32 byte_offset:1;
		u32 hmac_muting:1;
		u32 feedback_mode:2;
		u32 crypto_mode9_8:2;
		u32 extended_seq_num:1;
		u32 seq_num_mask:1;
		u32 mutable_bit_proc:1;
		u32 ip_version:1;
		u32 copy_pad:1;
		u32 copy_payload:1;
		u32 copy_hdr:1;
		u32 rsv1:1;
	} bf;
	u32 w;
} __attribute__((packed));

struct dynamic_sa_ctl {
	union dynamic_sa_contents sa_contents;
	union sa_command_0 sa_command_0;
	union sa_command_1 sa_command_1;
} __attribute__((packed));

/**
 * State Record for Security Association (SA)
 */
struct  sa_state_record {
	__le32 save_iv[4];
	__le32 save_hash_byte_cnt[2];
	union {
		u32 save_digest[16]; /* for MD5/SHA */
		__le32 save_digest_le32[16]; /* GHASH / CBC */
	};
} __attribute__((packed));

/**
 * Security Association (SA) for AES128
 *
 */
struct dynamic_sa_aes128 {
	struct dynamic_sa_ctl	ctrl;
	__le32 key[4];
	__le32 iv[4]; /* for CBC, OFC, and CFB mode */
	u32 state_ptr;
	u32 reserved;
} __attribute__((packed));

#define SA_AES128_LEN		(sizeof(struct dynamic_sa_aes128)/4)
#define SA_AES128_CONTENTS	0x3e000042

/*
 * Security Association (SA) for AES192
 */
struct dynamic_sa_aes192 {
	struct dynamic_sa_ctl ctrl;
	__le32 key[6];
	__le32 iv[4]; /* for CBC, OFC, and CFB mode */
	u32 state_ptr;
	u32 reserved;
} __attribute__((packed));

#define SA_AES192_LEN		(sizeof(struct dynamic_sa_aes192)/4)
#define SA_AES192_CONTENTS	0x3e000062

/**
 * Security Association (SA) for AES256
 */
struct dynamic_sa_aes256 {
	struct dynamic_sa_ctl ctrl;
	__le32 key[8];
	__le32 iv[4]; /* for CBC, OFC, and CFB mode */
	u32 state_ptr;
	u32 reserved;
} __attribute__((packed));

#define SA_AES256_LEN		(sizeof(struct dynamic_sa_aes256)/4)
#define SA_AES256_CONTENTS	0x3e000082
#define SA_AES_CONTENTS		0x3e000002

/**
 * Security Association (SA) for AES128 CCM
 */
struct dynamic_sa_aes128_ccm {
	struct dynamic_sa_ctl ctrl;
	__le32 key[4];
	__le32 iv[4];
	u32 state_ptr;
	u32 reserved;
} __packed;
#define SA_AES128_CCM_LEN	(sizeof(struct dynamic_sa_aes128_ccm)/4)
#define SA_AES128_CCM_CONTENTS	0x3e000042
#define SA_AES_CCM_CONTENTS	0x3e000002

/**
 * Security Association (SA) for AES128_GCM
 */
struct dynamic_sa_aes128_gcm {
	struct dynamic_sa_ctl ctrl;
	__le32 key[4];
	__le32 inner_digest[4];
	__le32 iv[4];
	u32 state_ptr;
	u32 reserved;
} __packed;

#define SA_AES128_GCM_LEN	(sizeof(struct dynamic_sa_aes128_gcm)/4)
#define SA_AES128_GCM_CONTENTS	0x3e000442
#define SA_AES_GCM_CONTENTS	0x3e000402

/**
 * Security Association (SA) for HASH160: HMAC-SHA1
 */
struct dynamic_sa_hash160 {
	struct dynamic_sa_ctl ctrl;
	__le32 inner_digest[5];
	__le32 outer_digest[5];
	u32 state_ptr;
	u32 reserved;
} __attribute__((packed));
#define SA_HASH160_LEN		(sizeof(struct dynamic_sa_hash160)/4)
#define SA_HASH160_CONTENTS     0x2000a502

static inline u32
get_dynamic_sa_offset_state_ptr_field(struct dynamic_sa_ctl *cts)
{
	u32 offset;

	offset = cts->sa_contents.bf.key_size
		+ cts->sa_contents.bf.inner_size
		+ cts->sa_contents.bf.outer_size
		+ cts->sa_contents.bf.spi
		+ cts->sa_contents.bf.seq_num0
		+ cts->sa_contents.bf.seq_num1
		+ cts->sa_contents.bf.seq_num_mask0
		+ cts->sa_contents.bf.seq_num_mask1
		+ cts->sa_contents.bf.seq_num_mask2
		+ cts->sa_contents.bf.seq_num_mask3
		+ cts->sa_contents.bf.iv0
		+ cts->sa_contents.bf.iv1
		+ cts->sa_contents.bf.iv2
		+ cts->sa_contents.bf.iv3;

	return sizeof(struct dynamic_sa_ctl) + offset * 4;
}

static inline __le32 *get_dynamic_sa_key_field(struct dynamic_sa_ctl *cts)
{
	return (__le32 *) ((unsigned long)cts + sizeof(struct dynamic_sa_ctl));
}

static inline __le32 *get_dynamic_sa_inner_digest(struct dynamic_sa_ctl *cts)
{
	return (__le32 *) ((unsigned long)cts +
		sizeof(struct dynamic_sa_ctl) +
		cts->sa_contents.bf.key_size * 4);
}

#endif
