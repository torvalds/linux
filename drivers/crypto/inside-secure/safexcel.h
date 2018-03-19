/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __SAFEXCEL_H__
#define __SAFEXCEL_H__

#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/skcipher.h>

#define EIP197_HIA_VERSION_LE			0xca35
#define EIP197_HIA_VERSION_BE			0x35ca

/* Static configuration */
#define EIP197_DEFAULT_RING_SIZE		400
#define EIP197_MAX_TOKENS			5
#define EIP197_MAX_RINGS			4
#define EIP197_FETCH_COUNT			1
#define EIP197_MAX_BATCH_SZ			64

#define EIP197_GFP_FLAGS(base)	((base).flags & CRYPTO_TFM_REQ_MAY_SLEEP ? \
				 GFP_KERNEL : GFP_ATOMIC)

/* Register base offsets */
#define EIP197_HIA_AIC(priv)		((priv)->base + (priv)->offsets.hia_aic)
#define EIP197_HIA_AIC_G(priv)		((priv)->base + (priv)->offsets.hia_aic_g)
#define EIP197_HIA_AIC_R(priv)		((priv)->base + (priv)->offsets.hia_aic_r)
#define EIP197_HIA_AIC_xDR(priv)	((priv)->base + (priv)->offsets.hia_aic_xdr)
#define EIP197_HIA_DFE(priv)		((priv)->base + (priv)->offsets.hia_dfe)
#define EIP197_HIA_DFE_THR(priv)	((priv)->base + (priv)->offsets.hia_dfe_thr)
#define EIP197_HIA_DSE(priv)		((priv)->base + (priv)->offsets.hia_dse)
#define EIP197_HIA_DSE_THR(priv)	((priv)->base + (priv)->offsets.hia_dse_thr)
#define EIP197_HIA_GEN_CFG(priv)	((priv)->base + (priv)->offsets.hia_gen_cfg)
#define EIP197_PE(priv)			((priv)->base + (priv)->offsets.pe)

/* EIP197 base offsets */
#define EIP197_HIA_AIC_BASE		0x90000
#define EIP197_HIA_AIC_G_BASE		0x90000
#define EIP197_HIA_AIC_R_BASE		0x90800
#define EIP197_HIA_AIC_xDR_BASE		0x80000
#define EIP197_HIA_DFE_BASE		0x8c000
#define EIP197_HIA_DFE_THR_BASE		0x8c040
#define EIP197_HIA_DSE_BASE		0x8d000
#define EIP197_HIA_DSE_THR_BASE		0x8d040
#define EIP197_HIA_GEN_CFG_BASE		0xf0000
#define EIP197_PE_BASE			0xa0000

/* EIP97 base offsets */
#define EIP97_HIA_AIC_BASE		0x0
#define EIP97_HIA_AIC_G_BASE		0x0
#define EIP97_HIA_AIC_R_BASE		0x0
#define EIP97_HIA_AIC_xDR_BASE		0x0
#define EIP97_HIA_DFE_BASE		0xf000
#define EIP97_HIA_DFE_THR_BASE		0xf200
#define EIP97_HIA_DSE_BASE		0xf400
#define EIP97_HIA_DSE_THR_BASE		0xf600
#define EIP97_HIA_GEN_CFG_BASE		0x10000
#define EIP97_PE_BASE			0x10000

/* CDR/RDR register offsets */
#define EIP197_HIA_xDR_OFF(priv, r)		(EIP197_HIA_AIC_xDR(priv) + (r) * 0x1000)
#define EIP197_HIA_CDR(priv, r)			(EIP197_HIA_xDR_OFF(priv, r))
#define EIP197_HIA_RDR(priv, r)			(EIP197_HIA_xDR_OFF(priv, r) + 0x800)
#define EIP197_HIA_xDR_RING_BASE_ADDR_LO	0x0000
#define EIP197_HIA_xDR_RING_BASE_ADDR_HI	0x0004
#define EIP197_HIA_xDR_RING_SIZE		0x0018
#define EIP197_HIA_xDR_DESC_SIZE		0x001c
#define EIP197_HIA_xDR_CFG			0x0020
#define EIP197_HIA_xDR_DMA_CFG			0x0024
#define EIP197_HIA_xDR_THRESH			0x0028
#define EIP197_HIA_xDR_PREP_COUNT		0x002c
#define EIP197_HIA_xDR_PROC_COUNT		0x0030
#define EIP197_HIA_xDR_PREP_PNTR		0x0034
#define EIP197_HIA_xDR_PROC_PNTR		0x0038
#define EIP197_HIA_xDR_STAT			0x003c

/* register offsets */
#define EIP197_HIA_DFE_CFG			0x0000
#define EIP197_HIA_DFE_THR_CTRL			0x0000
#define EIP197_HIA_DFE_THR_STAT			0x0004
#define EIP197_HIA_DSE_CFG			0x0000
#define EIP197_HIA_DSE_THR_CTRL			0x0000
#define EIP197_HIA_DSE_THR_STAT			0x0004
#define EIP197_HIA_RA_PE_CTRL			0x0010
#define EIP197_HIA_RA_PE_STAT			0x0014
#define EIP197_HIA_AIC_R_OFF(r)			((r) * 0x1000)
#define EIP197_HIA_AIC_R_ENABLE_CTRL(r)		(0xe008 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ENABLED_STAT(r)	(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ACK(r)			(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ENABLE_CLR(r)		(0xe014 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_G_ENABLE_CTRL		0xf808
#define EIP197_HIA_AIC_G_ENABLED_STAT		0xf810
#define EIP197_HIA_AIC_G_ACK			0xf810
#define EIP197_HIA_MST_CTRL			0xfff4
#define EIP197_HIA_OPTIONS			0xfff8
#define EIP197_HIA_VERSION			0xfffc
#define EIP197_PE_IN_DBUF_THRES			0x0000
#define EIP197_PE_IN_TBUF_THRES			0x0100
#define EIP197_PE_ICE_SCRATCH_RAM		0x0800
#define EIP197_PE_ICE_PUE_CTRL			0x0c80
#define EIP197_PE_ICE_SCRATCH_CTRL		0x0d04
#define EIP197_PE_ICE_FPP_CTRL			0x0d80
#define EIP197_PE_ICE_RAM_CTRL			0x0ff0
#define EIP197_PE_EIP96_FUNCTION_EN		0x1004
#define EIP197_PE_EIP96_CONTEXT_CTRL		0x1008
#define EIP197_PE_EIP96_CONTEXT_STAT		0x100c
#define EIP197_PE_OUT_DBUF_THRES		0x1c00
#define EIP197_PE_OUT_TBUF_THRES		0x1d00
#define EIP197_MST_CTRL				0xfff4

/* EIP197-specific registers, no indirection */
#define EIP197_CLASSIFICATION_RAMS		0xe0000
#define EIP197_TRC_CTRL				0xf0800
#define EIP197_TRC_LASTRES			0xf0804
#define EIP197_TRC_REGINDEX			0xf0808
#define EIP197_TRC_PARAMS			0xf0820
#define EIP197_TRC_FREECHAIN			0xf0824
#define EIP197_TRC_PARAMS2			0xf0828
#define EIP197_TRC_ECCCTRL			0xf0830
#define EIP197_TRC_ECCSTAT			0xf0834
#define EIP197_TRC_ECCADMINSTAT			0xf0838
#define EIP197_TRC_ECCDATASTAT			0xf083c
#define EIP197_TRC_ECCDATA			0xf0840
#define EIP197_CS_RAM_CTRL			0xf7ff0

/* EIP197_HIA_xDR_DESC_SIZE */
#define EIP197_xDR_DESC_MODE_64BIT		BIT(31)

/* EIP197_HIA_xDR_DMA_CFG */
#define EIP197_HIA_xDR_WR_RES_BUF		BIT(22)
#define EIP197_HIA_xDR_WR_CTRL_BUF		BIT(23)
#define EIP197_HIA_xDR_WR_OWN_BUF		BIT(24)
#define EIP197_HIA_xDR_CFG_WR_CACHE(n)		(((n) & 0x7) << 25)
#define EIP197_HIA_xDR_CFG_RD_CACHE(n)		(((n) & 0x7) << 29)

/* EIP197_HIA_CDR_THRESH */
#define EIP197_HIA_CDR_THRESH_PROC_PKT(n)	(n)
#define EIP197_HIA_CDR_THRESH_PROC_MODE		BIT(22)
#define EIP197_HIA_CDR_THRESH_PKT_MODE		BIT(23)
#define EIP197_HIA_CDR_THRESH_TIMEOUT(n)	((n) << 24) /* x256 clk cycles */

/* EIP197_HIA_RDR_THRESH */
#define EIP197_HIA_RDR_THRESH_PROC_PKT(n)	(n)
#define EIP197_HIA_RDR_THRESH_PKT_MODE		BIT(23)
#define EIP197_HIA_RDR_THRESH_TIMEOUT(n)	((n) << 24) /* x256 clk cycles */

/* EIP197_HIA_xDR_PREP_COUNT */
#define EIP197_xDR_PREP_CLR_COUNT		BIT(31)

/* EIP197_HIA_xDR_PROC_COUNT */
#define EIP197_xDR_PROC_xD_PKT_OFFSET		24
#define EIP197_xDR_PROC_xD_PKT_MASK		GENMASK(6, 0)
#define EIP197_xDR_PROC_xD_COUNT(n)		((n) << 2)
#define EIP197_xDR_PROC_xD_PKT(n)		((n) << 24)
#define EIP197_xDR_PROC_CLR_COUNT		BIT(31)

/* EIP197_HIA_xDR_STAT */
#define EIP197_xDR_DMA_ERR			BIT(0)
#define EIP197_xDR_PREP_CMD_THRES		BIT(1)
#define EIP197_xDR_ERR				BIT(2)
#define EIP197_xDR_THRESH			BIT(4)
#define EIP197_xDR_TIMEOUT			BIT(5)

#define EIP197_HIA_RA_PE_CTRL_RESET		BIT(31)
#define EIP197_HIA_RA_PE_CTRL_EN		BIT(30)

/* EIP197_HIA_AIC_R_ENABLE_CTRL */
#define EIP197_CDR_IRQ(n)			BIT((n) * 2)
#define EIP197_RDR_IRQ(n)			BIT((n) * 2 + 1)

/* EIP197_HIA_DFE/DSE_CFG */
#define EIP197_HIA_DxE_CFG_MIN_DATA_SIZE(n)	((n) << 0)
#define EIP197_HIA_DxE_CFG_DATA_CACHE_CTRL(n)	(((n) & 0x7) << 4)
#define EIP197_HIA_DxE_CFG_MAX_DATA_SIZE(n)	((n) << 8)
#define EIP197_HIA_DSE_CFG_ALWAYS_BUFFERABLE	GENMASK(15, 14)
#define EIP197_HIA_DxE_CFG_MIN_CTRL_SIZE(n)	((n) << 16)
#define EIP197_HIA_DxE_CFG_CTRL_CACHE_CTRL(n)	(((n) & 0x7) << 20)
#define EIP197_HIA_DxE_CFG_MAX_CTRL_SIZE(n)	((n) << 24)
#define EIP197_HIA_DFE_CFG_DIS_DEBUG		(BIT(31) | BIT(29))
#define EIP197_HIA_DSE_CFG_EN_SINGLE_WR		BIT(29)
#define EIP197_HIA_DSE_CFG_DIS_DEBUG		BIT(31)

/* EIP197_HIA_DFE/DSE_THR_CTRL */
#define EIP197_DxE_THR_CTRL_EN			BIT(30)
#define EIP197_DxE_THR_CTRL_RESET_PE		BIT(31)

/* EIP197_HIA_AIC_G_ENABLED_STAT */
#define EIP197_G_IRQ_DFE(n)			BIT((n) << 1)
#define EIP197_G_IRQ_DSE(n)			BIT(((n) << 1) + 1)
#define EIP197_G_IRQ_RING			BIT(16)
#define EIP197_G_IRQ_PE(n)			BIT((n) + 20)

/* EIP197_HIA_MST_CTRL */
#define RD_CACHE_3BITS				0x5
#define WR_CACHE_3BITS				0x3
#define RD_CACHE_4BITS				(RD_CACHE_3BITS << 1 | BIT(0))
#define WR_CACHE_4BITS				(WR_CACHE_3BITS << 1 | BIT(0))
#define EIP197_MST_CTRL_RD_CACHE(n)		(((n) & 0xf) << 0)
#define EIP197_MST_CTRL_WD_CACHE(n)		(((n) & 0xf) << 4)
#define EIP197_MST_CTRL_BYTE_SWAP		BIT(24)
#define EIP197_MST_CTRL_NO_BYTE_SWAP		BIT(25)

/* EIP197_PE_IN_DBUF/TBUF_THRES */
#define EIP197_PE_IN_xBUF_THRES_MIN(n)		((n) << 8)
#define EIP197_PE_IN_xBUF_THRES_MAX(n)		((n) << 12)

/* EIP197_PE_OUT_DBUF_THRES */
#define EIP197_PE_OUT_DBUF_THRES_MIN(n)		((n) << 0)
#define EIP197_PE_OUT_DBUF_THRES_MAX(n)		((n) << 4)

/* EIP197_PE_ICE_SCRATCH_CTRL */
#define EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_TIMER		BIT(2)
#define EIP197_PE_ICE_SCRATCH_CTRL_TIMER_EN		BIT(3)
#define EIP197_PE_ICE_SCRATCH_CTRL_CHANGE_ACCESS	BIT(24)
#define EIP197_PE_ICE_SCRATCH_CTRL_SCRATCH_ACCESS	BIT(25)

/* EIP197_PE_ICE_SCRATCH_RAM */
#define EIP197_NUM_OF_SCRATCH_BLOCKS		32

/* EIP197_PE_ICE_PUE/FPP_CTRL */
#define EIP197_PE_ICE_x_CTRL_SW_RESET			BIT(0)
#define EIP197_PE_ICE_x_CTRL_CLR_ECC_NON_CORR		BIT(14)
#define EIP197_PE_ICE_x_CTRL_CLR_ECC_CORR		BIT(15)

/* EIP197_PE_ICE_RAM_CTRL */
#define EIP197_PE_ICE_RAM_CTRL_PUE_PROG_EN	BIT(0)
#define EIP197_PE_ICE_RAM_CTRL_FPP_PROG_EN	BIT(1)

/* EIP197_PE_EIP96_FUNCTION_EN */
#define EIP197_FUNCTION_RSVD			(BIT(6) | BIT(15) | BIT(20) | BIT(23))
#define EIP197_PROTOCOL_HASH_ONLY		BIT(0)
#define EIP197_PROTOCOL_ENCRYPT_ONLY		BIT(1)
#define EIP197_PROTOCOL_HASH_ENCRYPT		BIT(2)
#define EIP197_PROTOCOL_HASH_DECRYPT		BIT(3)
#define EIP197_PROTOCOL_ENCRYPT_HASH		BIT(4)
#define EIP197_PROTOCOL_DECRYPT_HASH		BIT(5)
#define EIP197_ALG_ARC4				BIT(7)
#define EIP197_ALG_AES_ECB			BIT(8)
#define EIP197_ALG_AES_CBC			BIT(9)
#define EIP197_ALG_AES_CTR_ICM			BIT(10)
#define EIP197_ALG_AES_OFB			BIT(11)
#define EIP197_ALG_AES_CFB			BIT(12)
#define EIP197_ALG_DES_ECB			BIT(13)
#define EIP197_ALG_DES_CBC			BIT(14)
#define EIP197_ALG_DES_OFB			BIT(16)
#define EIP197_ALG_DES_CFB			BIT(17)
#define EIP197_ALG_3DES_ECB			BIT(18)
#define EIP197_ALG_3DES_CBC			BIT(19)
#define EIP197_ALG_3DES_OFB			BIT(21)
#define EIP197_ALG_3DES_CFB			BIT(22)
#define EIP197_ALG_MD5				BIT(24)
#define EIP197_ALG_HMAC_MD5			BIT(25)
#define EIP197_ALG_SHA1				BIT(26)
#define EIP197_ALG_HMAC_SHA1			BIT(27)
#define EIP197_ALG_SHA2				BIT(28)
#define EIP197_ALG_HMAC_SHA2			BIT(29)
#define EIP197_ALG_AES_XCBC_MAC			BIT(30)
#define EIP197_ALG_GCM_HASH			BIT(31)

/* EIP197_PE_EIP96_CONTEXT_CTRL */
#define EIP197_CONTEXT_SIZE(n)			(n)
#define EIP197_ADDRESS_MODE			BIT(8)
#define EIP197_CONTROL_MODE			BIT(9)

/* Context Control */
struct safexcel_context_record {
	u32 control0;
	u32 control1;

	__le32 data[12];
} __packed;

/* control0 */
#define CONTEXT_CONTROL_TYPE_NULL_OUT		0x0
#define CONTEXT_CONTROL_TYPE_NULL_IN		0x1
#define CONTEXT_CONTROL_TYPE_HASH_OUT		0x2
#define CONTEXT_CONTROL_TYPE_HASH_IN		0x3
#define CONTEXT_CONTROL_TYPE_CRYPTO_OUT		0x4
#define CONTEXT_CONTROL_TYPE_CRYPTO_IN		0x5
#define CONTEXT_CONTROL_TYPE_ENCRYPT_HASH_OUT	0x6
#define CONTEXT_CONTROL_TYPE_DECRYPT_HASH_IN	0x7
#define CONTEXT_CONTROL_TYPE_HASH_ENCRYPT_OUT	0x14
#define CONTEXT_CONTROL_TYPE_HASH_DECRYPT_OUT	0x15
#define CONTEXT_CONTROL_RESTART_HASH		BIT(4)
#define CONTEXT_CONTROL_NO_FINISH_HASH		BIT(5)
#define CONTEXT_CONTROL_SIZE(n)			((n) << 8)
#define CONTEXT_CONTROL_KEY_EN			BIT(16)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES128	(0x5 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES192	(0x6 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES256	(0x7 << 17)
#define CONTEXT_CONTROL_DIGEST_PRECOMPUTED	(0x1 << 21)
#define CONTEXT_CONTROL_DIGEST_HMAC		(0x3 << 21)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA1		(0x2 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA224	(0x4 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA256	(0x3 << 23)
#define CONTEXT_CONTROL_INV_FR			(0x5 << 24)
#define CONTEXT_CONTROL_INV_TR			(0x6 << 24)

/* control1 */
#define CONTEXT_CONTROL_CRYPTO_MODE_ECB		(0 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_CBC		(1 << 0)
#define CONTEXT_CONTROL_IV0			BIT(5)
#define CONTEXT_CONTROL_IV1			BIT(6)
#define CONTEXT_CONTROL_IV2			BIT(7)
#define CONTEXT_CONTROL_IV3			BIT(8)
#define CONTEXT_CONTROL_DIGEST_CNT		BIT(9)
#define CONTEXT_CONTROL_COUNTER_MODE		BIT(10)
#define CONTEXT_CONTROL_HASH_STORE		BIT(19)

/* EIP197_CS_RAM_CTRL */
#define EIP197_TRC_ENABLE_0			BIT(4)
#define EIP197_TRC_ENABLE_1			BIT(5)
#define EIP197_TRC_ENABLE_2			BIT(6)
#define EIP197_TRC_ENABLE_MASK			GENMASK(6, 4)

/* EIP197_TRC_PARAMS */
#define EIP197_TRC_PARAMS_SW_RESET		BIT(0)
#define EIP197_TRC_PARAMS_DATA_ACCESS		BIT(2)
#define EIP197_TRC_PARAMS_HTABLE_SZ(x)		((x) << 4)
#define EIP197_TRC_PARAMS_BLK_TIMER_SPEED(x)	((x) << 10)
#define EIP197_TRC_PARAMS_RC_SZ_LARGE(n)	((n) << 18)

/* EIP197_TRC_FREECHAIN */
#define EIP197_TRC_FREECHAIN_HEAD_PTR(p)	(p)
#define EIP197_TRC_FREECHAIN_TAIL_PTR(p)	((p) << 16)

/* EIP197_TRC_PARAMS2 */
#define EIP197_TRC_PARAMS2_HTABLE_PTR(p)	(p)
#define EIP197_TRC_PARAMS2_RC_SZ_SMALL(n)	((n) << 18)

/* Cache helpers */
#define EIP197_CS_RC_MAX			52
#define EIP197_CS_RC_SIZE			(4 * sizeof(u32))
#define EIP197_CS_RC_NEXT(x)			(x)
#define EIP197_CS_RC_PREV(x)			((x) << 10)
#define EIP197_RC_NULL				0x3ff
#define EIP197_CS_TRC_REC_WC			59
#define EIP197_CS_TRC_LG_REC_WC			73

/* Result data */
struct result_data_desc {
	u32 packet_length:17;
	u32 error_code:15;

	u8 bypass_length:4;
	u8 e15:1;
	u16 rsvd0;
	u8 hash_bytes:1;
	u8 hash_length:6;
	u8 generic_bytes:1;
	u8 checksum:1;
	u8 next_header:1;
	u8 length:1;

	u16 application_id;
	u16 rsvd1;

	u32 rsvd2;
} __packed;


/* Basic Result Descriptor format */
struct safexcel_result_desc {
	u32 particle_size:17;
	u8 rsvd0:3;
	u8 descriptor_overflow:1;
	u8 buffer_overflow:1;
	u8 last_seg:1;
	u8 first_seg:1;
	u16 result_size:8;

	u32 rsvd1;

	u32 data_lo;
	u32 data_hi;

	struct result_data_desc result_data;
} __packed;

struct safexcel_token {
	u32 packet_length:17;
	u8 stat:2;
	u16 instructions:9;
	u8 opcode:4;
} __packed;

#define EIP197_TOKEN_STAT_LAST_HASH		BIT(0)
#define EIP197_TOKEN_STAT_LAST_PACKET		BIT(1)
#define EIP197_TOKEN_OPCODE_DIRECTION		0x0
#define EIP197_TOKEN_OPCODE_INSERT		0x2
#define EIP197_TOKEN_OPCODE_NOOP		EIP197_TOKEN_OPCODE_INSERT
#define EIP197_TOKEN_OPCODE_BYPASS		GENMASK(3, 0)

static inline void eip197_noop_token(struct safexcel_token *token)
{
	token->opcode = EIP197_TOKEN_OPCODE_NOOP;
	token->packet_length = BIT(2);
}

/* Instructions */
#define EIP197_TOKEN_INS_INSERT_HASH_DIGEST	0x1c
#define EIP197_TOKEN_INS_TYPE_OUTPUT		BIT(5)
#define EIP197_TOKEN_INS_TYPE_HASH		BIT(6)
#define EIP197_TOKEN_INS_TYPE_CRYTO		BIT(7)
#define EIP197_TOKEN_INS_LAST			BIT(8)

/* Processing Engine Control Data  */
struct safexcel_control_data_desc {
	u32 packet_length:17;
	u16 options:13;
	u8 type:2;

	u16 application_id;
	u16 rsvd;

	u8 refresh:2;
	u32 context_lo:30;
	u32 context_hi;

	u32 control0;
	u32 control1;

	u32 token[EIP197_MAX_TOKENS];
} __packed;

#define EIP197_OPTION_MAGIC_VALUE	BIT(0)
#define EIP197_OPTION_64BIT_CTX		BIT(1)
#define EIP197_OPTION_CTX_CTRL_IN_CMD	BIT(8)
#define EIP197_OPTION_4_TOKEN_IV_CMD	GENMASK(11, 9)

#define EIP197_TYPE_EXTENDED		0x3

/* Basic Command Descriptor format */
struct safexcel_command_desc {
	u32 particle_size:17;
	u8 rsvd0:5;
	u8 last_seg:1;
	u8 first_seg:1;
	u16 additional_cdata_size:8;

	u32 rsvd1;

	u32 data_lo;
	u32 data_hi;

	struct safexcel_control_data_desc control_data;
} __packed;

/*
 * Internal structures & functions
 */

enum eip197_fw {
	FW_IFPP = 0,
	FW_IPUE,
	FW_NB
};

struct safexcel_ring {
	void *base;
	void *base_end;
	dma_addr_t base_dma;

	/* write and read pointers */
	void *write;
	void *read;

	/* number of elements used in the ring */
	unsigned nr;
	unsigned offset;
};

enum safexcel_alg_type {
	SAFEXCEL_ALG_TYPE_SKCIPHER,
	SAFEXCEL_ALG_TYPE_AHASH,
};

struct safexcel_request {
	struct list_head list;
	struct crypto_async_request *req;
};

struct safexcel_config {
	u32 rings;

	u32 cd_size;
	u32 cd_offset;

	u32 rd_size;
	u32 rd_offset;
};

struct safexcel_work_data {
	struct work_struct work;
	struct safexcel_crypto_priv *priv;
	int ring;
};

enum safexcel_eip_version {
	EIP97,
	EIP197,
};

struct safexcel_register_offsets {
	u32 hia_aic;
	u32 hia_aic_g;
	u32 hia_aic_r;
	u32 hia_aic_xdr;
	u32 hia_dfe;
	u32 hia_dfe_thr;
	u32 hia_dse;
	u32 hia_dse_thr;
	u32 hia_gen_cfg;
	u32 pe;
};

struct safexcel_crypto_priv {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
	struct clk *reg_clk;
	struct safexcel_config config;

	enum safexcel_eip_version version;
	struct safexcel_register_offsets offsets;

	/* context DMA pool */
	struct dma_pool *context_pool;

	atomic_t ring_used;

	struct {
		spinlock_t lock;
		spinlock_t egress_lock;

		struct list_head list;
		struct workqueue_struct *workqueue;
		struct safexcel_work_data work_data;

		/* command/result rings */
		struct safexcel_ring cdr;
		struct safexcel_ring rdr;

		/* queue */
		struct crypto_queue queue;
		spinlock_t queue_lock;

		/* Number of requests in the engine. */
		int requests;

		/* The ring is currently handling at least one request */
		bool busy;

		/* Store for current requests when bailing out of the dequeueing
		 * function when no enough resources are available.
		 */
		struct crypto_async_request *req;
		struct crypto_async_request *backlog;
	} ring[EIP197_MAX_RINGS];
};

struct safexcel_context {
	int (*send)(struct crypto_async_request *req, int ring,
		    struct safexcel_request *request, int *commands,
		    int *results);
	int (*handle_result)(struct safexcel_crypto_priv *priv, int ring,
			     struct crypto_async_request *req, bool *complete,
			     int *ret);
	struct safexcel_context_record *ctxr;
	dma_addr_t ctxr_dma;

	int ring;
	bool needs_inv;
	bool exit_inv;
};

/*
 * Template structure to describe the algorithms in order to register them.
 * It also has the purpose to contain our private structure and is actually
 * the only way I know in this framework to avoid having global pointers...
 */
struct safexcel_alg_template {
	struct safexcel_crypto_priv *priv;
	enum safexcel_alg_type type;
	union {
		struct skcipher_alg skcipher;
		struct ahash_alg ahash;
	} alg;
};

struct safexcel_inv_result {
	struct completion completion;
	int error;
};

void safexcel_dequeue(struct safexcel_crypto_priv *priv, int ring);
void safexcel_complete(struct safexcel_crypto_priv *priv, int ring);
int safexcel_invalidate_cache(struct crypto_async_request *async,
			      struct safexcel_crypto_priv *priv,
			      dma_addr_t ctxr_dma, int ring,
			      struct safexcel_request *request);
int safexcel_init_ring_descriptors(struct safexcel_crypto_priv *priv,
				   struct safexcel_ring *cdr,
				   struct safexcel_ring *rdr);
int safexcel_select_ring(struct safexcel_crypto_priv *priv);
void *safexcel_ring_next_rptr(struct safexcel_crypto_priv *priv,
			      struct safexcel_ring *ring);
void safexcel_ring_rollback_wptr(struct safexcel_crypto_priv *priv,
				 struct safexcel_ring *ring);
struct safexcel_command_desc *safexcel_add_cdesc(struct safexcel_crypto_priv *priv,
						 int ring_id,
						 bool first, bool last,
						 dma_addr_t data, u32 len,
						 u32 full_data_len,
						 dma_addr_t context);
struct safexcel_result_desc *safexcel_add_rdesc(struct safexcel_crypto_priv *priv,
						 int ring_id,
						bool first, bool last,
						dma_addr_t data, u32 len);
void safexcel_inv_complete(struct crypto_async_request *req, int error);

/* available algorithms */
extern struct safexcel_alg_template safexcel_alg_ecb_aes;
extern struct safexcel_alg_template safexcel_alg_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_sha1;
extern struct safexcel_alg_template safexcel_alg_sha224;
extern struct safexcel_alg_template safexcel_alg_sha256;
extern struct safexcel_alg_template safexcel_alg_hmac_sha1;
extern struct safexcel_alg_template safexcel_alg_hmac_sha256;

#endif
