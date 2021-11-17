/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2017 Marvell
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 */

#ifndef __SAFEXCEL_H__
#define __SAFEXCEL_H__

#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>
#include <crypto/skcipher.h>
#include <linux/types.h>

#define EIP197_HIA_VERSION_BE			0xca35
#define EIP197_HIA_VERSION_LE			0x35ca
#define EIP97_VERSION_LE			0x9e61
#define EIP196_VERSION_LE			0x3bc4
#define EIP197_VERSION_LE			0x3ac5
#define EIP96_VERSION_LE			0x9f60
#define EIP201_VERSION_LE			0x36c9
#define EIP206_VERSION_LE			0x31ce
#define EIP207_VERSION_LE			0x30cf
#define EIP197_REG_LO16(reg)			(reg & 0xffff)
#define EIP197_REG_HI16(reg)			((reg >> 16) & 0xffff)
#define EIP197_VERSION_MASK(reg)		((reg >> 16) & 0xfff)
#define EIP197_VERSION_SWAP(reg)		(((reg & 0xf0) << 4) | \
						((reg >> 4) & 0xf0) | \
						((reg >> 12) & 0xf))

/* EIP197 HIA OPTIONS ENCODING */
#define EIP197_HIA_OPT_HAS_PE_ARB		BIT(29)

/* EIP206 OPTIONS ENCODING */
#define EIP206_OPT_ICE_TYPE(n)			((n>>8)&3)
#define EIP206_OPT_OCE_TYPE(n)			((n>>10)&3)

/* EIP197 OPTIONS ENCODING */
#define EIP197_OPT_HAS_TRC			BIT(31)

/* Static configuration */
#define EIP197_DEFAULT_RING_SIZE		400
#define EIP197_EMB_TOKENS			4 /* Pad CD to 16 dwords */
#define EIP197_MAX_TOKENS			16
#define EIP197_MAX_RINGS			4
#define EIP197_FETCH_DEPTH			2
#define EIP197_MAX_BATCH_SZ			64
#define EIP197_MAX_RING_AIC			14

#define EIP197_GFP_FLAGS(base)	((base).flags & CRYPTO_TFM_REQ_MAY_SLEEP ? \
				 GFP_KERNEL : GFP_ATOMIC)

/* Custom on-stack requests (for invalidation) */
#define EIP197_SKCIPHER_REQ_SIZE	sizeof(struct skcipher_request) + \
					sizeof(struct safexcel_cipher_req)
#define EIP197_AHASH_REQ_SIZE		sizeof(struct ahash_request) + \
					sizeof(struct safexcel_ahash_req)
#define EIP197_AEAD_REQ_SIZE		sizeof(struct aead_request) + \
					sizeof(struct safexcel_cipher_req)
#define EIP197_REQUEST_ON_STACK(name, type, size) \
	char __##name##_desc[size] CRYPTO_MINALIGN_ATTR; \
	struct type##_request *name = (void *)__##name##_desc

/* Xilinx dev board base offsets */
#define EIP197_XLX_GPIO_BASE		0x200000
#define EIP197_XLX_IRQ_BLOCK_ID_ADDR	0x2000
#define EIP197_XLX_IRQ_BLOCK_ID_VALUE	0x1fc2
#define EIP197_XLX_USER_INT_ENB_MSK	0x2004
#define EIP197_XLX_USER_INT_ENB_SET	0x2008
#define EIP197_XLX_USER_INT_ENB_CLEAR	0x200c
#define EIP197_XLX_USER_INT_BLOCK	0x2040
#define EIP197_XLX_USER_INT_PEND	0x2048
#define EIP197_XLX_USER_VECT_LUT0_ADDR	0x2080
#define EIP197_XLX_USER_VECT_LUT0_IDENT	0x03020100
#define EIP197_XLX_USER_VECT_LUT1_ADDR	0x2084
#define EIP197_XLX_USER_VECT_LUT1_IDENT	0x07060504
#define EIP197_XLX_USER_VECT_LUT2_ADDR	0x2088
#define EIP197_XLX_USER_VECT_LUT2_IDENT	0x0b0a0908
#define EIP197_XLX_USER_VECT_LUT3_ADDR	0x208c
#define EIP197_XLX_USER_VECT_LUT3_IDENT	0x0f0e0d0c

/* Helper defines for probe function */
#define EIP197_IRQ_NUMBER(i, is_pci)	(i + is_pci)

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
#define EIP197_GLOBAL(priv)		((priv)->base + (priv)->offsets.global)

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
#define EIP197_GLOBAL_BASE		0xf0000

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
#define EIP97_GLOBAL_BASE		0x10000

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
#define EIP197_HIA_DFE_CFG(n)			(0x0000 + (128 * (n)))
#define EIP197_HIA_DFE_THR_CTRL(n)		(0x0000 + (128 * (n)))
#define EIP197_HIA_DFE_THR_STAT(n)		(0x0004 + (128 * (n)))
#define EIP197_HIA_DSE_CFG(n)			(0x0000 + (128 * (n)))
#define EIP197_HIA_DSE_THR_CTRL(n)		(0x0000 + (128 * (n)))
#define EIP197_HIA_DSE_THR_STAT(n)		(0x0004 + (128 * (n)))
#define EIP197_HIA_RA_PE_CTRL(n)		(0x0010 + (8   * (n)))
#define EIP197_HIA_RA_PE_STAT			0x0014
#define EIP197_HIA_AIC_R_OFF(r)			((r) * 0x1000)
#define EIP197_HIA_AIC_R_ENABLE_CTRL(r)		(0xe008 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ENABLED_STAT(r)	(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ACK(r)			(0xe010 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_ENABLE_CLR(r)		(0xe014 - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_R_VERSION(r)		(0xe01c - EIP197_HIA_AIC_R_OFF(r))
#define EIP197_HIA_AIC_G_ENABLE_CTRL		0xf808
#define EIP197_HIA_AIC_G_ENABLED_STAT		0xf810
#define EIP197_HIA_AIC_G_ACK			0xf810
#define EIP197_HIA_MST_CTRL			0xfff4
#define EIP197_HIA_OPTIONS			0xfff8
#define EIP197_HIA_VERSION			0xfffc
#define EIP197_PE_IN_DBUF_THRES(n)		(0x0000 + (0x2000 * (n)))
#define EIP197_PE_IN_TBUF_THRES(n)		(0x0100 + (0x2000 * (n)))
#define EIP197_PE_ICE_SCRATCH_RAM(n)		(0x0800 + (0x2000 * (n)))
#define EIP197_PE_ICE_PUE_CTRL(n)		(0x0c80 + (0x2000 * (n)))
#define EIP197_PE_ICE_PUTF_CTRL(n)		(0x0d00 + (0x2000 * (n)))
#define EIP197_PE_ICE_SCRATCH_CTRL(n)		(0x0d04 + (0x2000 * (n)))
#define EIP197_PE_ICE_FPP_CTRL(n)		(0x0d80 + (0x2000 * (n)))
#define EIP197_PE_ICE_PPTF_CTRL(n)		(0x0e00 + (0x2000 * (n)))
#define EIP197_PE_ICE_RAM_CTRL(n)		(0x0ff0 + (0x2000 * (n)))
#define EIP197_PE_ICE_VERSION(n)		(0x0ffc + (0x2000 * (n)))
#define EIP197_PE_EIP96_TOKEN_CTRL(n)		(0x1000 + (0x2000 * (n)))
#define EIP197_PE_EIP96_FUNCTION_EN(n)		(0x1004 + (0x2000 * (n)))
#define EIP197_PE_EIP96_CONTEXT_CTRL(n)		(0x1008 + (0x2000 * (n)))
#define EIP197_PE_EIP96_CONTEXT_STAT(n)		(0x100c + (0x2000 * (n)))
#define EIP197_PE_EIP96_TOKEN_CTRL2(n)		(0x102c + (0x2000 * (n)))
#define EIP197_PE_EIP96_FUNCTION2_EN(n)		(0x1030 + (0x2000 * (n)))
#define EIP197_PE_EIP96_OPTIONS(n)		(0x13f8 + (0x2000 * (n)))
#define EIP197_PE_EIP96_VERSION(n)		(0x13fc + (0x2000 * (n)))
#define EIP197_PE_OCE_VERSION(n)		(0x1bfc + (0x2000 * (n)))
#define EIP197_PE_OUT_DBUF_THRES(n)		(0x1c00 + (0x2000 * (n)))
#define EIP197_PE_OUT_TBUF_THRES(n)		(0x1d00 + (0x2000 * (n)))
#define EIP197_PE_PSE_VERSION(n)		(0x1efc + (0x2000 * (n)))
#define EIP197_PE_DEBUG(n)			(0x1ff4 + (0x2000 * (n)))
#define EIP197_PE_OPTIONS(n)			(0x1ff8 + (0x2000 * (n)))
#define EIP197_PE_VERSION(n)			(0x1ffc + (0x2000 * (n)))
#define EIP197_MST_CTRL				0xfff4
#define EIP197_OPTIONS				0xfff8
#define EIP197_VERSION				0xfffc

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
#define EIP197_STRC_CONFIG			0xf43f0
#define EIP197_FLUE_CACHEBASE_LO(n)		(0xf6000 + (32 * (n)))
#define EIP197_FLUE_CACHEBASE_HI(n)		(0xf6004 + (32 * (n)))
#define EIP197_FLUE_CONFIG(n)			(0xf6010 + (32 * (n)))
#define EIP197_FLUE_OFFSETS			0xf6808
#define EIP197_FLUE_ARC4_OFFSET			0xf680c
#define EIP197_FLUE_IFC_LUT(n)			(0xf6820 + (4 * (n)))
#define EIP197_CS_RAM_CTRL			0xf7ff0

/* EIP197_HIA_xDR_DESC_SIZE */
#define EIP197_xDR_DESC_MODE_64BIT		BIT(31)
#define EIP197_CDR_DESC_MODE_ADCP		BIT(30)

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

/* EIP197_HIA_OPTIONS */
#define EIP197_N_RINGS_OFFSET			0
#define EIP197_N_RINGS_MASK			GENMASK(3, 0)
#define EIP197_N_PES_OFFSET			4
#define EIP197_N_PES_MASK			GENMASK(4, 0)
#define EIP97_N_PES_MASK			GENMASK(2, 0)
#define EIP197_HWDATAW_OFFSET			25
#define EIP197_HWDATAW_MASK			GENMASK(3, 0)
#define EIP97_HWDATAW_MASK			GENMASK(2, 0)
#define EIP197_CFSIZE_OFFSET			9
#define EIP197_CFSIZE_ADJUST			4
#define EIP97_CFSIZE_OFFSET			8
#define EIP197_CFSIZE_MASK			GENMASK(2, 0)
#define EIP97_CFSIZE_MASK			GENMASK(3, 0)
#define EIP197_RFSIZE_OFFSET			12
#define EIP197_RFSIZE_ADJUST			4
#define EIP97_RFSIZE_OFFSET			12
#define EIP197_RFSIZE_MASK			GENMASK(2, 0)
#define EIP97_RFSIZE_MASK			GENMASK(3, 0)

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
#define EIP197_HIA_DFE_CFG_DIS_DEBUG		GENMASK(31, 29)
#define EIP197_HIA_DSE_CFG_EN_SINGLE_WR		BIT(29)
#define EIP197_HIA_DSE_CFG_DIS_DEBUG		GENMASK(31, 30)

/* EIP197_HIA_DFE/DSE_THR_CTRL */
#define EIP197_DxE_THR_CTRL_EN			BIT(30)
#define EIP197_DxE_THR_CTRL_RESET_PE		BIT(31)

/* EIP197_PE_ICE_PUE/FPP_CTRL */
#define EIP197_PE_ICE_UENG_START_OFFSET(n)	((n) << 16)
#define EIP197_PE_ICE_UENG_INIT_ALIGN_MASK	0x7ff0
#define EIP197_PE_ICE_UENG_DEBUG_RESET		BIT(3)

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
#define EIP197_MST_CTRL_TX_MAX_CMD(n)		(((n) & 0xf) << 20)
#define EIP197_MST_CTRL_BYTE_SWAP		BIT(24)
#define EIP197_MST_CTRL_NO_BYTE_SWAP		BIT(25)
#define EIP197_MST_CTRL_BYTE_SWAP_BITS          GENMASK(25, 24)

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

/* EIP197_PE_EIP96_TOKEN_CTRL */
#define EIP197_PE_EIP96_TOKEN_CTRL_CTX_UPDATES		BIT(16)
#define EIP197_PE_EIP96_TOKEN_CTRL_NO_TOKEN_WAIT	BIT(17)
#define EIP197_PE_EIP96_TOKEN_CTRL_ENABLE_TIMEOUT	BIT(22)

/* EIP197_PE_EIP96_FUNCTION_EN */
#define EIP197_FUNCTION_ALL			0xffffffff

/* EIP197_PE_EIP96_CONTEXT_CTRL */
#define EIP197_CONTEXT_SIZE(n)			(n)
#define EIP197_ADDRESS_MODE			BIT(8)
#define EIP197_CONTROL_MODE			BIT(9)

/* EIP197_PE_EIP96_TOKEN_CTRL2 */
#define EIP197_PE_EIP96_TOKEN_CTRL2_CTX_DONE	BIT(3)

/* EIP197_PE_DEBUG */
#define EIP197_DEBUG_OCE_BYPASS			BIT(1)

/* EIP197_STRC_CONFIG */
#define EIP197_STRC_CONFIG_INIT			BIT(31)
#define EIP197_STRC_CONFIG_LARGE_REC(s)		(s<<8)
#define EIP197_STRC_CONFIG_SMALL_REC(s)		(s<<0)

/* EIP197_FLUE_CONFIG */
#define EIP197_FLUE_CONFIG_MAGIC		0xc7000004

/* Context Control */
struct safexcel_context_record {
	__le32 control0;
	__le32 control1;

	__le32 data[40];
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
#define CONTEXT_CONTROL_TYPE_HASH_ENCRYPT_OUT	0xe
#define CONTEXT_CONTROL_TYPE_HASH_DECRYPT_IN	0xf
#define CONTEXT_CONTROL_RESTART_HASH		BIT(4)
#define CONTEXT_CONTROL_NO_FINISH_HASH		BIT(5)
#define CONTEXT_CONTROL_SIZE(n)			((n) << 8)
#define CONTEXT_CONTROL_KEY_EN			BIT(16)
#define CONTEXT_CONTROL_CRYPTO_ALG_DES		(0x0 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_3DES		(0x2 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES128	(0x5 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES192	(0x6 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_AES256	(0x7 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_CHACHA20	(0x8 << 17)
#define CONTEXT_CONTROL_CRYPTO_ALG_SM4		(0xd << 17)
#define CONTEXT_CONTROL_DIGEST_INITIAL		(0x0 << 21)
#define CONTEXT_CONTROL_DIGEST_PRECOMPUTED	(0x1 << 21)
#define CONTEXT_CONTROL_DIGEST_XCM		(0x2 << 21)
#define CONTEXT_CONTROL_DIGEST_HMAC		(0x3 << 21)
#define CONTEXT_CONTROL_CRYPTO_ALG_MD5		(0x0 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_CRC32	(0x0 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA1		(0x2 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA224	(0x4 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA256	(0x3 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA384	(0x6 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA512	(0x5 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_GHASH	(0x4 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_XCBC128	(0x1 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_XCBC192	(0x2 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_XCBC256	(0x3 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SM3		(0x7 << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA3_256	(0xb << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA3_224	(0xc << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA3_512	(0xd << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_SHA3_384	(0xe << 23)
#define CONTEXT_CONTROL_CRYPTO_ALG_POLY1305	(0xf << 23)
#define CONTEXT_CONTROL_INV_FR			(0x5 << 24)
#define CONTEXT_CONTROL_INV_TR			(0x6 << 24)

/* control1 */
#define CONTEXT_CONTROL_CRYPTO_MODE_ECB		(0 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_CBC		(1 << 0)
#define CONTEXT_CONTROL_CHACHA20_MODE_256_32	(2 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_OFB		(4 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_CFB		(5 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_CTR_LOAD	(6 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_XTS		(7 << 0)
#define CONTEXT_CONTROL_CRYPTO_MODE_XCM		((6 << 0) | BIT(17))
#define CONTEXT_CONTROL_CHACHA20_MODE_CALC_OTK	(12 << 0)
#define CONTEXT_CONTROL_IV0			BIT(5)
#define CONTEXT_CONTROL_IV1			BIT(6)
#define CONTEXT_CONTROL_IV2			BIT(7)
#define CONTEXT_CONTROL_IV3			BIT(8)
#define CONTEXT_CONTROL_DIGEST_CNT		BIT(9)
#define CONTEXT_CONTROL_COUNTER_MODE		BIT(10)
#define CONTEXT_CONTROL_CRYPTO_STORE		BIT(12)
#define CONTEXT_CONTROL_HASH_STORE		BIT(19)

#define EIP197_XCM_MODE_GCM			1
#define EIP197_XCM_MODE_CCM			2

#define EIP197_AEAD_TYPE_IPSEC_ESP		2
#define EIP197_AEAD_TYPE_IPSEC_ESP_GMAC		3
#define EIP197_AEAD_IPSEC_IV_SIZE		8
#define EIP197_AEAD_IPSEC_NONCE_SIZE		4
#define EIP197_AEAD_IPSEC_COUNTER_SIZE		4
#define EIP197_AEAD_IPSEC_CCM_NONCE_SIZE	3

/* The hash counter given to the engine in the context has a granularity of
 * 64 bits.
 */
#define EIP197_COUNTER_BLOCK_SIZE		64

/* EIP197_CS_RAM_CTRL */
#define EIP197_TRC_ENABLE_0			BIT(4)
#define EIP197_TRC_ENABLE_1			BIT(5)
#define EIP197_TRC_ENABLE_2			BIT(6)
#define EIP197_TRC_ENABLE_MASK			GENMASK(6, 4)
#define EIP197_CS_BANKSEL_MASK			GENMASK(14, 12)
#define EIP197_CS_BANKSEL_OFS			12

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
#define EIP197_MIN_DSIZE			1024
#define EIP197_MIN_ASIZE			8
#define EIP197_CS_TRC_REC_WC			64
#define EIP197_CS_RC_SIZE			(4 * sizeof(u32))
#define EIP197_CS_RC_NEXT(x)			(x)
#define EIP197_CS_RC_PREV(x)			((x) << 10)
#define EIP197_RC_NULL				0x3ff

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

	u32 rsvd2[5];
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
} __packed;

/*
 * The EIP(1)97 only needs to fetch the descriptor part of
 * the result descriptor, not the result token part!
 */
#define EIP197_RD64_FETCH_SIZE		(sizeof(struct safexcel_result_desc) /\
					 sizeof(u32))
#define EIP197_RD64_RESULT_SIZE		(sizeof(struct result_data_desc) /\
					 sizeof(u32))

struct safexcel_token {
	u32 packet_length:17;
	u8 stat:2;
	u16 instructions:9;
	u8 opcode:4;
} __packed;

#define EIP197_TOKEN_HASH_RESULT_VERIFY		BIT(16)

#define EIP197_TOKEN_CTX_OFFSET(x)		(x)
#define EIP197_TOKEN_DIRECTION_EXTERNAL		BIT(11)
#define EIP197_TOKEN_EXEC_IF_SUCCESSFUL		(0x1 << 12)

#define EIP197_TOKEN_STAT_LAST_HASH		BIT(0)
#define EIP197_TOKEN_STAT_LAST_PACKET		BIT(1)
#define EIP197_TOKEN_OPCODE_DIRECTION		0x0
#define EIP197_TOKEN_OPCODE_INSERT		0x2
#define EIP197_TOKEN_OPCODE_NOOP		EIP197_TOKEN_OPCODE_INSERT
#define EIP197_TOKEN_OPCODE_RETRIEVE		0x4
#define EIP197_TOKEN_OPCODE_INSERT_REMRES	0xa
#define EIP197_TOKEN_OPCODE_VERIFY		0xd
#define EIP197_TOKEN_OPCODE_CTX_ACCESS		0xe
#define EIP197_TOKEN_OPCODE_BYPASS		GENMASK(3, 0)

static inline void eip197_noop_token(struct safexcel_token *token)
{
	token->opcode = EIP197_TOKEN_OPCODE_NOOP;
	token->packet_length = BIT(2);
	token->stat = 0;
	token->instructions = 0;
}

/* Instructions */
#define EIP197_TOKEN_INS_INSERT_HASH_DIGEST	0x1c
#define EIP197_TOKEN_INS_ORIGIN_IV0		0x14
#define EIP197_TOKEN_INS_ORIGIN_TOKEN		0x1b
#define EIP197_TOKEN_INS_ORIGIN_LEN(x)		((x) << 5)
#define EIP197_TOKEN_INS_TYPE_OUTPUT		BIT(5)
#define EIP197_TOKEN_INS_TYPE_HASH		BIT(6)
#define EIP197_TOKEN_INS_TYPE_CRYPTO		BIT(7)
#define EIP197_TOKEN_INS_LAST			BIT(8)

/* Processing Engine Control Data  */
struct safexcel_control_data_desc {
	u32 packet_length:17;
	u16 options:13;
	u8 type:2;

	u16 application_id;
	u16 rsvd;

	u32 context_lo;
	u32 context_hi;

	u32 control0;
	u32 control1;

	u32 token[EIP197_EMB_TOKENS];
} __packed;

#define EIP197_OPTION_MAGIC_VALUE	BIT(0)
#define EIP197_OPTION_64BIT_CTX		BIT(1)
#define EIP197_OPTION_RC_AUTO		(0x2 << 3)
#define EIP197_OPTION_CTX_CTRL_IN_CMD	BIT(8)
#define EIP197_OPTION_2_TOKEN_IV_CMD	GENMASK(11, 10)
#define EIP197_OPTION_4_TOKEN_IV_CMD	GENMASK(11, 9)

#define EIP197_TYPE_BCLA		0x0
#define EIP197_TYPE_EXTENDED		0x3
#define EIP197_CONTEXT_SMALL		0x2
#define EIP197_CONTEXT_SIZE_MASK	0x3

/* Basic Command Descriptor format */
struct safexcel_command_desc {
	u32 particle_size:17;
	u8 rsvd0:5;
	u8 last_seg:1;
	u8 first_seg:1;
	u8 additional_cdata_size:8;

	u32 rsvd1;

	u32 data_lo;
	u32 data_hi;

	u32 atok_lo;
	u32 atok_hi;

	struct safexcel_control_data_desc control_data;
} __packed;

#define EIP197_CD64_FETCH_SIZE		(sizeof(struct safexcel_command_desc) /\
					sizeof(u32))

/*
 * Internal structures & functions
 */

#define EIP197_FW_TERMINAL_NOPS		2
#define EIP197_FW_START_POLLCNT		16
#define EIP197_FW_PUE_READY		0x14
#define EIP197_FW_FPP_READY		0x18

enum eip197_fw {
	FW_IFPP = 0,
	FW_IPUE,
	FW_NB
};

struct safexcel_desc_ring {
	void *base;
	void *shbase;
	void *base_end;
	void *shbase_end;
	dma_addr_t base_dma;
	dma_addr_t shbase_dma;

	/* write and read pointers */
	void *write;
	void *shwrite;
	void *read;

	/* descriptor element offset */
	unsigned int offset;
	unsigned int shoffset;
};

enum safexcel_alg_type {
	SAFEXCEL_ALG_TYPE_SKCIPHER,
	SAFEXCEL_ALG_TYPE_AEAD,
	SAFEXCEL_ALG_TYPE_AHASH,
};

struct safexcel_config {
	u32 pes;
	u32 rings;

	u32 cd_size;
	u32 cd_offset;
	u32 cdsh_offset;

	u32 rd_size;
	u32 rd_offset;
	u32 res_offset;
};

struct safexcel_work_data {
	struct work_struct work;
	struct safexcel_crypto_priv *priv;
	int ring;
};

struct safexcel_ring {
	spinlock_t lock;

	struct workqueue_struct *workqueue;
	struct safexcel_work_data work_data;

	/* command/result rings */
	struct safexcel_desc_ring cdr;
	struct safexcel_desc_ring rdr;

	/* result ring crypto API request */
	struct crypto_async_request **rdr_req;

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

	/* irq of this ring */
	int irq;
};

/* EIP integration context flags */
enum safexcel_eip_version {
	/* Platform (EIP integration context) specifier */
	EIP97IES_MRVL,
	EIP197B_MRVL,
	EIP197D_MRVL,
	EIP197_DEVBRD
};

/* Priority we use for advertising our algorithms */
#define SAFEXCEL_CRA_PRIORITY		300

/* SM3 digest result for zero length message */
#define EIP197_SM3_ZEROM_HASH	"\x1A\xB2\x1D\x83\x55\xCF\xA1\x7F" \
				"\x8E\x61\x19\x48\x31\xE8\x1A\x8F" \
				"\x22\xBE\xC8\xC7\x28\xFE\xFB\x74" \
				"\x7E\xD0\x35\xEB\x50\x82\xAA\x2B"

/* EIP algorithm presence flags */
enum safexcel_eip_algorithms {
	SAFEXCEL_ALG_BC0      = BIT(5),
	SAFEXCEL_ALG_SM4      = BIT(6),
	SAFEXCEL_ALG_SM3      = BIT(7),
	SAFEXCEL_ALG_CHACHA20 = BIT(8),
	SAFEXCEL_ALG_POLY1305 = BIT(9),
	SAFEXCEL_SEQMASK_256   = BIT(10),
	SAFEXCEL_SEQMASK_384   = BIT(11),
	SAFEXCEL_ALG_AES      = BIT(12),
	SAFEXCEL_ALG_AES_XFB  = BIT(13),
	SAFEXCEL_ALG_DES      = BIT(15),
	SAFEXCEL_ALG_DES_XFB  = BIT(16),
	SAFEXCEL_ALG_ARC4     = BIT(18),
	SAFEXCEL_ALG_AES_XTS  = BIT(20),
	SAFEXCEL_ALG_WIRELESS = BIT(21),
	SAFEXCEL_ALG_MD5      = BIT(22),
	SAFEXCEL_ALG_SHA1     = BIT(23),
	SAFEXCEL_ALG_SHA2_256 = BIT(25),
	SAFEXCEL_ALG_SHA2_512 = BIT(26),
	SAFEXCEL_ALG_XCBC_MAC = BIT(27),
	SAFEXCEL_ALG_CBC_MAC_ALL = BIT(29),
	SAFEXCEL_ALG_GHASH    = BIT(30),
	SAFEXCEL_ALG_SHA3     = BIT(31),
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
	u32 global;
};

enum safexcel_flags {
	EIP197_TRC_CACHE	= BIT(0),
	SAFEXCEL_HW_EIP197	= BIT(1),
	EIP197_PE_ARB		= BIT(2),
	EIP197_ICE		= BIT(3),
	EIP197_SIMPLE_TRC	= BIT(4),
	EIP197_OCE		= BIT(5),
};

struct safexcel_hwconfig {
	enum safexcel_eip_algorithms algo_flags;
	int hwver;
	int hiaver;
	int ppver;
	int icever;
	int pever;
	int ocever;
	int psever;
	int hwdataw;
	int hwcfsize;
	int hwrfsize;
	int hwnumpes;
	int hwnumrings;
	int hwnumraic;
};

struct safexcel_crypto_priv {
	void __iomem *base;
	struct device *dev;
	struct clk *clk;
	struct clk *reg_clk;
	struct safexcel_config config;

	enum safexcel_eip_version version;
	struct safexcel_register_offsets offsets;
	struct safexcel_hwconfig hwconfig;
	u32 flags;

	/* context DMA pool */
	struct dma_pool *context_pool;

	atomic_t ring_used;

	struct safexcel_ring *ring;
};

struct safexcel_context {
	int (*send)(struct crypto_async_request *req, int ring,
		    int *commands, int *results);
	int (*handle_result)(struct safexcel_crypto_priv *priv, int ring,
			     struct crypto_async_request *req, bool *complete,
			     int *ret);
	struct safexcel_context_record *ctxr;
	struct safexcel_crypto_priv *priv;
	dma_addr_t ctxr_dma;

	union {
		__le32 le[SHA3_512_BLOCK_SIZE / 4];
		__be32 be[SHA3_512_BLOCK_SIZE / 4];
		u32 word[SHA3_512_BLOCK_SIZE / 4];
		u8 byte[SHA3_512_BLOCK_SIZE];
	} ipad, opad;

	int ring;
	bool needs_inv;
	bool exit_inv;
};

#define HASH_CACHE_SIZE			SHA512_BLOCK_SIZE

struct safexcel_ahash_export_state {
	u64 len;
	u64 processed;

	u32 digest;

	u32 state[SHA512_DIGEST_SIZE / sizeof(u32)];
	u8 cache[HASH_CACHE_SIZE];
};

/*
 * Template structure to describe the algorithms in order to register them.
 * It also has the purpose to contain our private structure and is actually
 * the only way I know in this framework to avoid having global pointers...
 */
struct safexcel_alg_template {
	struct safexcel_crypto_priv *priv;
	enum safexcel_alg_type type;
	enum safexcel_eip_algorithms algo_mask;
	union {
		struct skcipher_alg skcipher;
		struct aead_alg aead;
		struct ahash_alg ahash;
	} alg;
};

struct safexcel_inv_result {
	struct completion completion;
	int error;
};

void safexcel_dequeue(struct safexcel_crypto_priv *priv, int ring);
int safexcel_rdesc_check_errors(struct safexcel_crypto_priv *priv,
				void *rdp);
void safexcel_complete(struct safexcel_crypto_priv *priv, int ring);
int safexcel_invalidate_cache(struct crypto_async_request *async,
			      struct safexcel_crypto_priv *priv,
			      dma_addr_t ctxr_dma, int ring);
int safexcel_init_ring_descriptors(struct safexcel_crypto_priv *priv,
				   struct safexcel_desc_ring *cdr,
				   struct safexcel_desc_ring *rdr);
int safexcel_select_ring(struct safexcel_crypto_priv *priv);
void *safexcel_ring_next_rptr(struct safexcel_crypto_priv *priv,
			      struct safexcel_desc_ring *ring);
void *safexcel_ring_first_rptr(struct safexcel_crypto_priv *priv, int  ring);
void safexcel_ring_rollback_wptr(struct safexcel_crypto_priv *priv,
				 struct safexcel_desc_ring *ring);
struct safexcel_command_desc *safexcel_add_cdesc(struct safexcel_crypto_priv *priv,
						 int ring_id,
						 bool first, bool last,
						 dma_addr_t data, u32 len,
						 u32 full_data_len,
						 dma_addr_t context,
						 struct safexcel_token **atoken);
struct safexcel_result_desc *safexcel_add_rdesc(struct safexcel_crypto_priv *priv,
						 int ring_id,
						bool first, bool last,
						dma_addr_t data, u32 len);
int safexcel_ring_first_rdr_index(struct safexcel_crypto_priv *priv,
				  int ring);
int safexcel_ring_rdr_rdesc_index(struct safexcel_crypto_priv *priv,
				  int ring,
				  struct safexcel_result_desc *rdesc);
void safexcel_rdr_req_set(struct safexcel_crypto_priv *priv,
			  int ring,
			  struct safexcel_result_desc *rdesc,
			  struct crypto_async_request *req);
inline struct crypto_async_request *
safexcel_rdr_req_get(struct safexcel_crypto_priv *priv, int ring);
void safexcel_inv_complete(struct crypto_async_request *req, int error);
int safexcel_hmac_setkey(struct safexcel_context *base, const u8 *key,
			 unsigned int keylen, const char *alg,
			 unsigned int state_sz);

/* available algorithms */
extern struct safexcel_alg_template safexcel_alg_ecb_des;
extern struct safexcel_alg_template safexcel_alg_cbc_des;
extern struct safexcel_alg_template safexcel_alg_ecb_des3_ede;
extern struct safexcel_alg_template safexcel_alg_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_ecb_aes;
extern struct safexcel_alg_template safexcel_alg_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_cfb_aes;
extern struct safexcel_alg_template safexcel_alg_ofb_aes;
extern struct safexcel_alg_template safexcel_alg_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_md5;
extern struct safexcel_alg_template safexcel_alg_sha1;
extern struct safexcel_alg_template safexcel_alg_sha224;
extern struct safexcel_alg_template safexcel_alg_sha256;
extern struct safexcel_alg_template safexcel_alg_sha384;
extern struct safexcel_alg_template safexcel_alg_sha512;
extern struct safexcel_alg_template safexcel_alg_hmac_md5;
extern struct safexcel_alg_template safexcel_alg_hmac_sha1;
extern struct safexcel_alg_template safexcel_alg_hmac_sha224;
extern struct safexcel_alg_template safexcel_alg_hmac_sha256;
extern struct safexcel_alg_template safexcel_alg_hmac_sha384;
extern struct safexcel_alg_template safexcel_alg_hmac_sha512;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_cbc_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_ctr_aes;
extern struct safexcel_alg_template safexcel_alg_xts_aes;
extern struct safexcel_alg_template safexcel_alg_gcm;
extern struct safexcel_alg_template safexcel_alg_ccm;
extern struct safexcel_alg_template safexcel_alg_crc32;
extern struct safexcel_alg_template safexcel_alg_cbcmac;
extern struct safexcel_alg_template safexcel_alg_xcbcmac;
extern struct safexcel_alg_template safexcel_alg_cmac;
extern struct safexcel_alg_template safexcel_alg_chacha20;
extern struct safexcel_alg_template safexcel_alg_chachapoly;
extern struct safexcel_alg_template safexcel_alg_chachapoly_esp;
extern struct safexcel_alg_template safexcel_alg_sm3;
extern struct safexcel_alg_template safexcel_alg_hmac_sm3;
extern struct safexcel_alg_template safexcel_alg_ecb_sm4;
extern struct safexcel_alg_template safexcel_alg_cbc_sm4;
extern struct safexcel_alg_template safexcel_alg_ofb_sm4;
extern struct safexcel_alg_template safexcel_alg_cfb_sm4;
extern struct safexcel_alg_template safexcel_alg_ctr_sm4;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_sm4;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sm3_cbc_sm4;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_ctr_sm4;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sm3_ctr_sm4;
extern struct safexcel_alg_template safexcel_alg_sha3_224;
extern struct safexcel_alg_template safexcel_alg_sha3_256;
extern struct safexcel_alg_template safexcel_alg_sha3_384;
extern struct safexcel_alg_template safexcel_alg_sha3_512;
extern struct safexcel_alg_template safexcel_alg_hmac_sha3_224;
extern struct safexcel_alg_template safexcel_alg_hmac_sha3_256;
extern struct safexcel_alg_template safexcel_alg_hmac_sha3_384;
extern struct safexcel_alg_template safexcel_alg_hmac_sha3_512;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha1_cbc_des;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_cbc_des3_ede;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha256_cbc_des;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha224_cbc_des;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha512_cbc_des;
extern struct safexcel_alg_template safexcel_alg_authenc_hmac_sha384_cbc_des;
extern struct safexcel_alg_template safexcel_alg_rfc4106_gcm;
extern struct safexcel_alg_template safexcel_alg_rfc4543_gcm;
extern struct safexcel_alg_template safexcel_alg_rfc4309_ccm;

#endif
