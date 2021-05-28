/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */

#ifndef __HISI_SEC_V2_CRYPTO_H
#define __HISI_SEC_V2_CRYPTO_H

#define SEC_IV_SIZE		24
#define SEC_MAX_KEY_SIZE	64
#define SEC_COMM_SCENE		0
#define SEC_MIN_BLOCK_SZ	1

enum sec_calg {
	SEC_CALG_3DES = 0x1,
	SEC_CALG_AES  = 0x2,
	SEC_CALG_SM4  = 0x3,
};

enum sec_hash_alg {
	SEC_A_HMAC_SHA1   = 0x10,
	SEC_A_HMAC_SHA256 = 0x11,
	SEC_A_HMAC_SHA512 = 0x15,
};

enum sec_mac_len {
	SEC_HMAC_SHA1_MAC   = 20,
	SEC_HMAC_SHA256_MAC = 32,
	SEC_HMAC_SHA512_MAC = 64,
};

enum sec_cmode {
	SEC_CMODE_ECB    = 0x0,
	SEC_CMODE_CBC    = 0x1,
	SEC_CMODE_CFB    = 0x2,
	SEC_CMODE_OFB    = 0x3,
	SEC_CMODE_CTR    = 0x4,
	SEC_CMODE_XTS    = 0x7,
};

enum sec_ckey_type {
	SEC_CKEY_128BIT = 0x0,
	SEC_CKEY_192BIT = 0x1,
	SEC_CKEY_256BIT = 0x2,
	SEC_CKEY_3DES_3KEY = 0x1,
	SEC_CKEY_3DES_2KEY = 0x3,
};

enum sec_bd_type {
	SEC_BD_TYPE1 = 0x1,
	SEC_BD_TYPE2 = 0x2,
	SEC_BD_TYPE3 = 0x3,
};

enum sec_auth {
	SEC_NO_AUTH = 0x0,
	SEC_AUTH_TYPE1 = 0x1,
	SEC_AUTH_TYPE2 = 0x2,
};

enum sec_cipher_dir {
	SEC_CIPHER_ENC = 0x1,
	SEC_CIPHER_DEC = 0x2,
};

enum sec_addr_type {
	SEC_PBUF = 0x0,
	SEC_SGL  = 0x1,
	SEC_PRP  = 0x2,
};

struct bd_status {
	u64 tag;
	u8 done;
	u8 err_type;
	u16 flag;
};

enum {
	AUTHPAD_PAD,
	AUTHPAD_NOPAD,
};

enum {
	AIGEN_GEN,
	AIGEN_NOGEN,
};

struct sec_sqe_type2 {
	/*
	 * mac_len: 0~4 bits
	 * a_key_len: 5~10 bits
	 * a_alg: 11~16 bits
	 */
	__le32 mac_key_alg;

	/*
	 * c_icv_len: 0~5 bits
	 * c_width: 6~8 bits
	 * c_key_len: 9~11 bits
	 * c_mode: 12~15 bits
	 */
	__le16 icvw_kmode;

	/* c_alg: 0~3 bits */
	__u8 c_alg;
	__u8 rsvd4;

	/*
	 * a_len: 0~23 bits
	 * iv_offset_l: 24~31 bits
	 */
	__le32 alen_ivllen;

	/*
	 * c_len: 0~23 bits
	 * iv_offset_h: 24~31 bits
	 */
	__le32 clen_ivhlen;

	__le16 auth_src_offset;
	__le16 cipher_src_offset;
	__le16 cs_ip_header_offset;
	__le16 cs_udp_header_offset;
	__le16 pass_word_len;
	__le16 dk_len;
	__u8 salt3;
	__u8 salt2;
	__u8 salt1;
	__u8 salt0;

	__le16 tag;
	__le16 rsvd5;

	/*
	 * c_pad_type: 0~3 bits
	 * c_pad_len: 4~11 bits
	 * c_pad_data_type: 12~15 bits
	 */
	__le16 cph_pad;

	/* c_pad_len_field: 0~1 bits */
	__le16 c_pad_len_field;

	__le64 long_a_data_len;
	__le64 a_ivin_addr;
	__le64 a_key_addr;
	__le64 mac_addr;
	__le64 c_ivin_addr;
	__le64 c_key_addr;

	__le64 data_src_addr;
	__le64 data_dst_addr;

	/*
	 * done: 0 bit
	 * icv: 1~3 bits
	 * csc: 4~6 bits
	 * flag: 7-10 bits
	 * dif_check: 11~13 bits
	 */
	__le16 done_flag;

	__u8 error_type;
	__u8 warning_type;
	__u8 mac_i3;
	__u8 mac_i2;
	__u8 mac_i1;
	__u8 mac_i0;
	__le16 check_sum_i;
	__u8 tls_pad_len_i;
	__u8 rsvd12;
	__le32 counter;
};

struct sec_sqe {
	/*
	 * type:	0~3 bits
	 * cipher:	4~5 bits
	 * auth:	6~7 bit s
	 */
	__u8 type_cipher_auth;

	/*
	 * seq:	0 bit
	 * de:	1~2 bits
	 * scene:	3~6 bits
	 * src_addr_type: ~7 bit, with sdm_addr_type 0-1 bits
	 */
	__u8 sds_sa_type;

	/*
	 * src_addr_type: 0~1 bits, not used now,
	 * if support PRP, set this field, or set zero.
	 * dst_addr_type: 2~4 bits
	 * mac_addr_type: 5~7 bits
	 */
	__u8 sdm_addr_type;
	__u8 rsvd0;

	/*
	 * nonce_len(type2): 0~3 bits
	 * huk(type2): 4 bit
	 * key_s(type2): 5 bit
	 * ci_gen: 6~7 bits
	 */
	__u8 huk_key_ci;

	/*
	 * ai_gen: 0~1 bits
	 * a_pad(type2): 2~3 bits
	 * c_s(type2): 4~5 bits
	 */
	__u8 ai_apd_cs;

	/*
	 * rhf(type2): 0 bit
	 * c_key_type: 1~2 bits
	 * a_key_type: 3~4 bits
	 * write_frame_len(type2): 5~7 bits
	 */
	__u8 rca_key_frm;

	/*
	 * cal_iv_addr_en(type2): 0 bit
	 * tls_up(type2): 1 bit
	 * inveld: 7 bit
	 */
	__u8 iv_tls_ld;

	/* Just using type2 BD now */
	struct sec_sqe_type2 type2;
};

struct bd3_auth_ivin {
	__le64 a_ivin_addr;
	__le32 rsvd0;
	__le32 rsvd1;
} __packed __aligned(4);

struct bd3_skip_data {
	__le32 rsvd0;

	/*
	 * gran_num: 0~15 bits
	 * reserved: 16~31 bits
	 */
	__le32 gran_num;

	/*
	 * src_skip_data_len: 0~24 bits
	 * reserved: 25~31 bits
	 */
	__le32 src_skip_data_len;

	/*
	 * dst_skip_data_len: 0~24 bits
	 * reserved: 25~31 bits
	 */
	__le32 dst_skip_data_len;
};

struct bd3_stream_scene {
	__le64 c_ivin_addr;
	__le64 long_a_data_len;

	/*
	 * auth_pad: 0~1 bits
	 * stream_protocol: 2~4 bits
	 * reserved: 5~7 bits
	 */
	__u8 stream_auth_pad;
	__u8 plaintext_type;
	__le16 pad_len_1p3;
} __packed __aligned(4);

struct bd3_no_scene {
	__le64 c_ivin_addr;
	__le32 rsvd0;
	__le32 rsvd1;
	__le32 rsvd2;
} __packed __aligned(4);

struct bd3_check_sum {
	__u8 rsvd0;
	__u8 hac_sva_status;
	__le16 check_sum_i;
};

struct bd3_tls_type_back {
	__u8 tls_1p3_type_back;
	__u8 hac_sva_status;
	__le16 pad_len_1p3_back;
};

struct sec_sqe3 {
	/*
	 * type: 0~3 bit
	 * bd_invalid: 4 bit
	 * scene: 5~8 bit
	 * de: 9~10 bit
	 * src_addr_type: 11~13 bit
	 * dst_addr_type: 14~16 bit
	 * mac_addr_type: 17~19 bit
	 * reserved: 20~31 bits
	 */
	__le32 bd_param;

	/*
	 * cipher: 0~1 bits
	 * ci_gen: 2~3 bit
	 * c_icv_len: 4~9 bit
	 * c_width: 10~12 bits
	 * c_key_len: 13~15 bits
	 */
	__le16 c_icv_key;

	/*
	 * c_mode : 0~3 bits
	 * c_alg : 4~7 bits
	 */
	__u8 c_mode_alg;

	/*
	 * nonce_len : 0~3 bits
	 * huk : 4 bits
	 * cal_iv_addr_en : 5 bits
	 * seq : 6 bits
	 * reserved : 7 bits
	 */
	__u8 huk_iv_seq;

	__le64 tag;
	__le64 data_src_addr;
	__le64 a_key_addr;
	union {
		struct bd3_auth_ivin auth_ivin;
		struct bd3_skip_data skip_data;
	};

	__le64 c_key_addr;

	/*
	 * auth: 0~1 bits
	 * ai_gen: 2~3 bits
	 * mac_len: 4~8 bits
	 * akey_len: 9~14 bits
	 * a_alg: 15~20 bits
	 * key_sel: 21~24 bits
	 * updata_key: 25 bits
	 * reserved: 26~31 bits
	 */
	__le32 auth_mac_key;
	__le32 salt;
	__le16 auth_src_offset;
	__le16 cipher_src_offset;

	/*
	 * auth_len: 0~23 bit
	 * auth_key_offset: 24~31 bits
	 */
	__le32 a_len_key;

	/*
	 * cipher_len: 0~23 bit
	 * auth_ivin_offset: 24~31 bits
	 */
	__le32 c_len_ivin;
	__le64 data_dst_addr;
	__le64 mac_addr;
	union {
		struct bd3_stream_scene stream_scene;
		struct bd3_no_scene no_scene;
	};

	/*
	 * done: 0 bit
	 * icv: 1~3 bit
	 * csc: 4~6 bit
	 * flag: 7~10 bit
	 * reserved: 11~15 bit
	 */
	__le16 done_flag;
	__u8 error_type;
	__u8 warning_type;
	union {
		__le32 mac_i;
		__le32 kek_key_addr_l;
	};
	union {
		__le32 kek_key_addr_h;
		struct bd3_check_sum check_sum;
		struct bd3_tls_type_back tls_type_back;
	};
	__le32 counter;
} __packed __aligned(4);

int sec_register_to_crypto(struct hisi_qm *qm);
void sec_unregister_from_crypto(struct hisi_qm *qm);
#endif
