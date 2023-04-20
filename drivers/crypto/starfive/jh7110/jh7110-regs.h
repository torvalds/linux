/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __JH7110_REGS_H__
#define __JH7110_REGS_H__

#include <crypto/aes.h>
#include <crypto/sha.h>

#define JH7110_ALG_CR_OFFSET					0x0
#define JH7110_ALG_FIFO_OFFSET					0x4
#define JH7110_IE_MASK_OFFSET					0x8
#define JH7110_IE_FLAG_OFFSET					0xc
#define JH7110_DMA_IN_LEN_OFFSET				0x10
#define JH7110_DMA_OUT_LEN_OFFSET				0x14

#define JH7110_AES_REGS_OFFSET					0x100
#define JH7110_SHA_REGS_OFFSET					0x300
#define JH7110_CRYPTO_REGS_OFFSET				0x400

union jh7110_alg_cr {
	u32 v;
	struct {
		u32 start				:1;
		u32 aes_dma_en			:1;
		u32 des_dma_en			:1;
		u32 sha_dma_en			:1;
		u32 alg_done			:1;
		u32 rsvd_0				:3;
		u32 clear				:1;
		u32 rsvd_1				:23;
	};
};

union jh7110_ie_mask {
	u32 v;
	struct {
		u32 aes_ie_mask			:1;
		u32 des_ie_mask			:1;
		u32 sha_ie_mask			:1;
		u32 crypto_ie_mask		:1;
		u32 rsvd_0				:28;
	};
};

union jh7110_ie_flag {
	u32 v;
	struct {
		u32 aes_ie_done			:1;
		u32 des_ie_done			:1;
		u32 sha_ie_done			:1;
		u32 crypto_ie_done		:1;
		u32 rsvd_0				:28;
	};
};

#define JH7110_CRYPTO_CACR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x0)
#define JH7110_CRYPTO_CASR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x4)
#define JH7110_CRYPTO_CAAR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x8)
#define JH7110_CRYPTO_CAER_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x108)
#define JH7110_CRYPTO_CANR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x208)
#define JH7110_CRYPTO_CAAFR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x308)
#define JH7110_CRYPTO_CAEFR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x30c)
#define JH7110_CRYPTO_CANFR_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x310)
#define JH7110_FIFO_COUNTER_OFFSET				(JH7110_CRYPTO_REGS_OFFSET + 0x314)

// R^2 mod N and N0'
#define CRYPTO_CMD_PRE						0x0
// (A + A) mod N, ==> A
#define CRYPTO_CMD_AAN						0x1
// A ^ E mod N   ==> A
#define CRYPTO_CMD_AMEN						0x2
// A + E mod N   ==> A
#define CRYPTO_CMD_AAEN						0x3
// A - E mod N   ==> A
#define CRYPTO_CMD_ADEN						0x4
// A * R mod N   ==> A
#define CRYPTO_CMD_ARN						0x5
// A * E * R mod N ==> A
#define CRYPTO_CMD_AERN						0x6
// A * A * R mod N ==> A
#define CRYPTO_CMD_AARN						0x7
// ECC2P      ==> A
#define CRYPTO_CMD_ECC2P					0x8
// ECCPQ      ==> A
#define CRYPTO_CMD_ECCPQ					0x9

union jh7110_crypto_cacr {
	u32 v;
	struct {
		u32 start			:1;
		u32 reset			:1;
		u32 ie				:1;
		u32 rsvd_0			:1;
		u32 fifo_mode			:1;
		u32 not_r2			:1;
		u32 ecc_sub			:1;
		u32 pre_expf			:1;

		u32 cmd				:4;
		u32 rsvd_1			:1;
		u32 ctrl_dummy			:1;
		u32 ctrl_false			:1;
		u32 cln_done			:1;

		u32 opsize			:6;
		u32 rsvd_2			:2;

		u32 exposize			:6;
		u32 rsvd_3			:1;
		u32 bigendian			:1;
	};
};

union jh7110_crypto_casr {
	u32 v;
	struct {
#define JH7110_PKA_DONE_FLAGS					BIT(0)
		u32 done			:1;
		u32 rsvd_0			:31;
	};
};

#define JH7110_AES_AESDIO0R					(JH7110_AES_REGS_OFFSET + 0x0)
#define JH7110_AES_KEY0						(JH7110_AES_REGS_OFFSET + 0x4)
#define JH7110_AES_KEY1						(JH7110_AES_REGS_OFFSET + 0x8)
#define JH7110_AES_KEY2						(JH7110_AES_REGS_OFFSET + 0xC)
#define JH7110_AES_KEY3						(JH7110_AES_REGS_OFFSET + 0x10)
#define JH7110_AES_KEY4						(JH7110_AES_REGS_OFFSET + 0x14)
#define JH7110_AES_KEY5						(JH7110_AES_REGS_OFFSET + 0x18)
#define JH7110_AES_KEY6						(JH7110_AES_REGS_OFFSET + 0x1C)
#define JH7110_AES_KEY7						(JH7110_AES_REGS_OFFSET + 0x20)
#define JH7110_AES_CSR						(JH7110_AES_REGS_OFFSET + 0x24)
#define JH7110_AES_IV0						(JH7110_AES_REGS_OFFSET + 0x28)
#define JH7110_AES_IV1						(JH7110_AES_REGS_OFFSET + 0x2C)
#define JH7110_AES_IV2						(JH7110_AES_REGS_OFFSET + 0x30)
#define JH7110_AES_IV3						(JH7110_AES_REGS_OFFSET + 0x34)
#define JH7110_AES_NONCE0					(JH7110_AES_REGS_OFFSET + 0x3C)
#define JH7110_AES_NONCE1					(JH7110_AES_REGS_OFFSET + 0x40)
#define JH7110_AES_NONCE2					(JH7110_AES_REGS_OFFSET + 0x44)
#define JH7110_AES_NONCE3					(JH7110_AES_REGS_OFFSET + 0x48)
#define JH7110_AES_ALEN0					(JH7110_AES_REGS_OFFSET + 0x4C)
#define JH7110_AES_ALEN1					(JH7110_AES_REGS_OFFSET + 0x50)
#define JH7110_AES_MLEN0					(JH7110_AES_REGS_OFFSET + 0x54)
#define JH7110_AES_MLEN1					(JH7110_AES_REGS_OFFSET + 0x58)
#define JH7110_AES_IVLEN					(JH7110_AES_REGS_OFFSET + 0x5C)

union jh7110_aes_csr {
	u32 v;
	struct {
		u32 cmode			:1;
#define JH7110_AES_KEYMODE_128					0x0
#define JH7110_AES_KEYMODE_192					0x1
#define JH7110_AES_KEYMODE_256					0x2
		u32 keymode			:2;
#define JH7110_AES_BUSY						BIT(3)
		u32 busy			:1;
		u32 done			:1;
#define JH7110_AES_KEY_DONE					BIT(5)
		u32 krdy			:1;
		u32 aesrst			:1;
		u32 aesie			:1;

#define JH7110_AES_CCM_START					BIT(8)
		u32 ccm_start			:1;
#define JH7110_AES_MODE_ECB					0x0
#define JH7110_AES_MODE_CBC					0x1
#define JH7110_AES_MODE_CFB					0x2
#define JH7110_AES_MODE_OFB					0x3
#define JH7110_AES_MODE_CTR					0x4
#define JH7110_AES_MODE_CCM					0x5
#define JH7110_AES_MODE_GCM					0x6
		u32 mode			:3;
#define JH7110_AES_GCM_START					BIT(12)
		u32 gcm_start			:1;
#define JH7110_AES_GCM_DONE					BIT(13)
		u32 gcm_done			:1;
		u32 delay_aes			:1;
		u32 vaes_start			:1;

		u32 rsvd_0			:8;

#define JH7110_AES_MODE_XFB_1					0x0
#define JH7110_AES_MODE_XFB_128					0x5
		u32 stream_mode			:3;
		u32 rsvd_1			:5;
	};
};

#define JH7110_SHA_SHACSR					(JH7110_SHA_REGS_OFFSET + 0x0)
#define JH7110_SHA_SHAWDR					(JH7110_SHA_REGS_OFFSET + 0x4)
#define JH7110_SHA_SHARDR					(JH7110_SHA_REGS_OFFSET + 0x8)
#define JH7110_SHA_SHAWSR					(JH7110_SHA_REGS_OFFSET + 0xC)
#define JH7110_SHA_SHAWLEN3					(JH7110_SHA_REGS_OFFSET + 0x10)
#define JH7110_SHA_SHAWLEN2					(JH7110_SHA_REGS_OFFSET + 0x14)
#define JH7110_SHA_SHAWLEN1					(JH7110_SHA_REGS_OFFSET + 0x18)
#define JH7110_SHA_SHAWLEN0					(JH7110_SHA_REGS_OFFSET + 0x1C)
#define JH7110_SHA_SHAWKR					(JH7110_SHA_REGS_OFFSET + 0x20)
#define JH7110_SHA_SHAWKLEN					(JH7110_SHA_REGS_OFFSET + 0x24)

union jh7110_sha_shacsr {
	u32 v;
	struct {
		u32 start			:1;
		u32 reset			:1;
		u32 ie				:1;
		u32 firstb			:1;
#define JH7110_SHA_SM3						0x0
#define JH7110_SHA_SHA0						0x1
#define JH7110_SHA_SHA1						0x2
#define JH7110_SHA_SHA224					0x3
#define JH7110_SHA_SHA256					0x4
#define JH7110_SHA_SHA384					0x5
#define JH7110_SHA_SHA512					0x6
#define JH7110_SHA_MODE_MASK					0x7
		u32 mode			:3;
		u32 rsvd_0			:1;

		u32 final			:1;
		u32 rsvd_1			:2;
#define JH7110_SHA_HMAC_FLAGS					0x800
		u32 hmac			:1;
		u32 rsvd_2			:1;
#define JH7110_SHA_KEY_DONE					BIT(13)
		u32 key_done			:1;
		u32 key_flag			:1;
#define JH7110_SHA_HMAC_DONE					BIT(15)
		u32 hmac_done			:1;
#define JH7110_SHA_BUSY						BIT(16)
		u32 busy			:1;
		u32 shadone			:1;
		u32 rsvd_3			:14;
	};
};

#endif
