/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __NX_CSBCPB_H__
#define __NX_CSBCPB_H__

struct cop_symcpb_aes_ecb {
	u8 key[32];
	u8 __rsvd[80];
} __packed;

struct cop_symcpb_aes_cbc {
	u8 iv[16];
	u8 key[32];
	u8 cv[16];
	u32 spbc;
	u8 __rsvd[44];
} __packed;

struct cop_symcpb_aes_gca {
	u8 in_pat[16];
	u8 key[32];
	u8 out_pat[16];
	u32 spbc;
	u8 __rsvd[44];
} __packed;

struct cop_symcpb_aes_gcm {
	u8 in_pat_or_aad[16];
	u8 iv_or_cnt[16];
	u64 bit_length_aad;
	u64 bit_length_data;
	u8 in_s0[16];
	u8 key[32];
	u8 __rsvd1[16];
	u8 out_pat_or_mac[16];
	u8 out_s0[16];
	u8 out_cnt[16];
	u32 spbc;
	u8 __rsvd2[12];
} __packed;

struct cop_symcpb_aes_ctr {
	u8 iv[16];
	u8 key[32];
	u8 cv[16];
	u32 spbc;
	u8 __rsvd2[44];
} __packed;

struct cop_symcpb_aes_cca {
	u8 b0[16];
	u8 b1[16];
	u8 key[16];
	u8 out_pat_or_b0[16];
	u32 spbc;
	u8 __rsvd[44];
} __packed;

struct cop_symcpb_aes_ccm {
	u8 in_pat_or_b0[16];
	u8 iv_or_ctr[16];
	u8 in_s0[16];
	u8 key[16];
	u8 __rsvd1[48];
	u8 out_pat_or_mac[16];
	u8 out_s0[16];
	u8 out_ctr[16];
	u32 spbc;
	u8 __rsvd2[12];
} __packed;

struct cop_symcpb_aes_xcbc {
	u8 cv[16];
	u8 key[16];
	u8 __rsvd1[16];
	u8 out_cv_mac[16];
	u32 spbc;
	u8 __rsvd2[44];
} __packed;

struct cop_symcpb_sha256 {
	u64 message_bit_length;
	u64 __rsvd1;
	u8 input_partial_digest[32];
	u8 message_digest[32];
	u32 spbc;
	u8 __rsvd2[44];
} __packed;

struct cop_symcpb_sha512 {
	u64 message_bit_length_hi;
	u64 message_bit_length_lo;
	u8 input_partial_digest[64];
	u8 __rsvd1[32];
	u8 message_digest[64];
	u32 spbc;
	u8 __rsvd2[76];
} __packed;

#define NX_FDM_INTERMEDIATE		0x01
#define NX_FDM_CONTINUATION		0x02
#define NX_FDM_ENDE_ENCRYPT		0x80

#define NX_CPB_FDM(c)			((c)->cpb.hdr.fdm)
#define NX_CPB_KS_DS(c)			((c)->cpb.hdr.ks_ds)

#define NX_CPB_KEY_SIZE(c)		(NX_CPB_KS_DS(c) >> 4)
#define NX_CPB_SET_KEY_SIZE(c, x)	NX_CPB_KS_DS(c) |= ((x) << 4)
#define NX_CPB_SET_DIGEST_SIZE(c, x)	NX_CPB_KS_DS(c) |= (x)

struct cop_symcpb_header {
	u8 mode;
	u8 fdm;
	u8 ks_ds;
	u8 pad_byte;
	u8 __rsvd[12];
} __packed;

struct cop_parameter_block {
	struct cop_symcpb_header hdr;
	union {
		struct cop_symcpb_aes_ecb  aes_ecb;
		struct cop_symcpb_aes_cbc  aes_cbc;
		struct cop_symcpb_aes_gca  aes_gca;
		struct cop_symcpb_aes_gcm  aes_gcm;
		struct cop_symcpb_aes_cca  aes_cca;
		struct cop_symcpb_aes_ccm  aes_ccm;
		struct cop_symcpb_aes_ctr  aes_ctr;
		struct cop_symcpb_aes_xcbc aes_xcbc;
		struct cop_symcpb_sha256   sha256;
		struct cop_symcpb_sha512   sha512;
	};
} __packed;

#define NX_CSB_VALID_BIT	0x80

/* co-processor status block */
struct cop_status_block {
	u8 valid;
	u8 crb_seq_number;
	u8 completion_code;
	u8 completion_extension;
	u32 processed_byte_count;
	u64 address;
} __packed;

/* Nest accelerator workbook section 4.4 */
struct nx_csbcpb {
	unsigned char __rsvd[112];
	struct cop_status_block csb;
	struct cop_parameter_block cpb;
} __packed;

/* nx_csbcpb related definitions */
#define NX_MODE_AES_ECB			0
#define NX_MODE_AES_CBC			1
#define NX_MODE_AES_GMAC		2
#define NX_MODE_AES_GCA			3
#define NX_MODE_AES_GCM			4
#define NX_MODE_AES_CCA			5
#define NX_MODE_AES_CCM			6
#define NX_MODE_AES_CTR			7
#define NX_MODE_AES_XCBC_MAC		20
#define NX_MODE_SHA			0
#define NX_MODE_SHA_HMAC		1
#define NX_MODE_AES_CBC_HMAC_ETA	8
#define NX_MODE_AES_CBC_HMAC_ATE	9
#define NX_MODE_AES_CBC_HMAC_EAA	10
#define NX_MODE_AES_CTR_HMAC_ETA	12
#define NX_MODE_AES_CTR_HMAC_ATE	13
#define NX_MODE_AES_CTR_HMAC_EAA	14

#define NX_FDM_CI_FULL		0
#define NX_FDM_CI_FIRST		1
#define NX_FDM_CI_LAST		2
#define NX_FDM_CI_MIDDLE	3

#define NX_FDM_PR_NONE		0
#define NX_FDM_PR_PAD		1

#define NX_KS_AES_128		1
#define NX_KS_AES_192		2
#define NX_KS_AES_256		3

#define NX_DS_SHA256		2
#define NX_DS_SHA512		3

#define NX_FC_AES		0
#define NX_FC_SHA		2
#define NX_FC_AES_HMAC		6

#define NX_MAX_FC		(NX_FC_AES_HMAC + 1)
#define NX_MAX_MODE		(NX_MODE_AES_XCBC_MAC + 1)

#define HCOP_FC_AES          NX_FC_AES
#define HCOP_FC_SHA          NX_FC_SHA
#define HCOP_FC_AES_HMAC     NX_FC_AES_HMAC

/* indices into the array of algorithm properties */
#define NX_PROPS_AES_128		0
#define NX_PROPS_AES_192		1
#define NX_PROPS_AES_256		2
#define NX_PROPS_SHA256			1
#define NX_PROPS_SHA512			2

#endif
