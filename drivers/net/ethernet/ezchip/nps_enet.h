/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2015 EZchip Technologies.
 */

#ifndef _NPS_ENET_H
#define _NPS_ENET_H

/* default values */
#define NPS_ENET_NAPI_POLL_WEIGHT		0x2
#define NPS_ENET_MAX_FRAME_LENGTH		0x3FFF
#define NPS_ENET_GE_MAC_CFG_0_TX_FC_RETR	0x7
#define NPS_ENET_GE_MAC_CFG_0_RX_IFG		0x5
#define NPS_ENET_GE_MAC_CFG_0_TX_IFG		0xC
#define NPS_ENET_GE_MAC_CFG_0_TX_PR_LEN		0x7
#define NPS_ENET_GE_MAC_CFG_2_STAT_EN		0x3
#define NPS_ENET_GE_MAC_CFG_3_RX_IFG_TH		0x14
#define NPS_ENET_GE_MAC_CFG_3_MAX_LEN		0x3FFC
#define NPS_ENET_ENABLE				1
#define NPS_ENET_DISABLE			0

/* register definitions  */
#define NPS_ENET_REG_TX_CTL		0x800
#define NPS_ENET_REG_TX_BUF		0x808
#define NPS_ENET_REG_RX_CTL		0x810
#define NPS_ENET_REG_RX_BUF		0x818
#define NPS_ENET_REG_BUF_INT_ENABLE	0x8C0
#define NPS_ENET_REG_GE_MAC_CFG_0	0x1000
#define NPS_ENET_REG_GE_MAC_CFG_1	0x1004
#define NPS_ENET_REG_GE_MAC_CFG_2	0x1008
#define NPS_ENET_REG_GE_MAC_CFG_3	0x100C
#define NPS_ENET_REG_GE_RST		0x1400
#define NPS_ENET_REG_PHASE_FIFO_CTL	0x1404

/* Tx control register masks and shifts */
#define TX_CTL_NT_MASK 0x7FF
#define TX_CTL_NT_SHIFT 0
#define TX_CTL_ET_MASK 0x4000
#define TX_CTL_ET_SHIFT 14
#define TX_CTL_CT_MASK 0x8000
#define TX_CTL_CT_SHIFT 15

/* Rx control register masks and shifts */
#define RX_CTL_NR_MASK 0x7FF
#define RX_CTL_NR_SHIFT 0
#define RX_CTL_CRC_MASK 0x2000
#define RX_CTL_CRC_SHIFT 13
#define RX_CTL_ER_MASK 0x4000
#define RX_CTL_ER_SHIFT 14
#define RX_CTL_CR_MASK 0x8000
#define RX_CTL_CR_SHIFT 15

/* Interrupt enable for data buffer events register masks and shifts */
#define RX_RDY_MASK 0x1
#define RX_RDY_SHIFT 0
#define TX_DONE_MASK 0x2
#define TX_DONE_SHIFT 1

/* Gbps Eth MAC Configuration 0 register masks and shifts */
#define CFG_0_RX_EN_MASK 0x1
#define CFG_0_RX_EN_SHIFT 0
#define CFG_0_TX_EN_MASK 0x2
#define CFG_0_TX_EN_SHIFT 1
#define CFG_0_TX_FC_EN_MASK 0x4
#define CFG_0_TX_FC_EN_SHIFT 2
#define CFG_0_TX_PAD_EN_MASK 0x8
#define CFG_0_TX_PAD_EN_SHIFT 3
#define CFG_0_TX_CRC_EN_MASK 0x10
#define CFG_0_TX_CRC_EN_SHIFT 4
#define CFG_0_RX_FC_EN_MASK 0x20
#define CFG_0_RX_FC_EN_SHIFT 5
#define CFG_0_RX_CRC_STRIP_MASK 0x40
#define CFG_0_RX_CRC_STRIP_SHIFT 6
#define CFG_0_RX_CRC_IGNORE_MASK 0x80
#define CFG_0_RX_CRC_IGNORE_SHIFT 7
#define CFG_0_RX_LENGTH_CHECK_EN_MASK 0x100
#define CFG_0_RX_LENGTH_CHECK_EN_SHIFT 8
#define CFG_0_TX_FC_RETR_MASK 0xE00
#define CFG_0_TX_FC_RETR_SHIFT 9
#define CFG_0_RX_IFG_MASK 0xF000
#define CFG_0_RX_IFG_SHIFT 12
#define CFG_0_TX_IFG_MASK 0x3F0000
#define CFG_0_TX_IFG_SHIFT 16
#define CFG_0_RX_PR_CHECK_EN_MASK 0x400000
#define CFG_0_RX_PR_CHECK_EN_SHIFT 22
#define CFG_0_NIB_MODE_MASK 0x800000
#define CFG_0_NIB_MODE_SHIFT 23
#define CFG_0_TX_IFG_NIB_MASK 0xF000000
#define CFG_0_TX_IFG_NIB_SHIFT 24
#define CFG_0_TX_PR_LEN_MASK 0xF0000000
#define CFG_0_TX_PR_LEN_SHIFT 28

/* Gbps Eth MAC Configuration 1 register masks and shifts */
#define CFG_1_OCTET_0_MASK 0x000000FF
#define CFG_1_OCTET_0_SHIFT 0
#define CFG_1_OCTET_1_MASK 0x0000FF00
#define CFG_1_OCTET_1_SHIFT 8
#define CFG_1_OCTET_2_MASK 0x00FF0000
#define CFG_1_OCTET_2_SHIFT 16
#define CFG_1_OCTET_3_MASK 0xFF000000
#define CFG_1_OCTET_3_SHIFT 24

/* Gbps Eth MAC Configuration 2 register masks and shifts */
#define CFG_2_OCTET_4_MASK 0x000000FF
#define CFG_2_OCTET_4_SHIFT 0
#define CFG_2_OCTET_5_MASK 0x0000FF00
#define CFG_2_OCTET_5_SHIFT 8
#define CFG_2_DISK_MC_MASK 0x00100000
#define CFG_2_DISK_MC_SHIFT 20
#define CFG_2_DISK_BC_MASK 0x00200000
#define CFG_2_DISK_BC_SHIFT 21
#define CFG_2_DISK_DA_MASK 0x00400000
#define CFG_2_DISK_DA_SHIFT 22
#define CFG_2_STAT_EN_MASK 0x3000000
#define CFG_2_STAT_EN_SHIFT 24
#define CFG_2_TRANSMIT_FLUSH_EN_MASK 0x80000000
#define CFG_2_TRANSMIT_FLUSH_EN_SHIFT 31

/* Gbps Eth MAC Configuration 3 register masks and shifts */
#define CFG_3_TM_HD_MODE_MASK 0x1
#define CFG_3_TM_HD_MODE_SHIFT 0
#define CFG_3_RX_CBFC_EN_MASK 0x2
#define CFG_3_RX_CBFC_EN_SHIFT 1
#define CFG_3_RX_CBFC_REDIR_EN_MASK 0x4
#define CFG_3_RX_CBFC_REDIR_EN_SHIFT 2
#define CFG_3_REDIRECT_CBFC_SEL_MASK 0x18
#define CFG_3_REDIRECT_CBFC_SEL_SHIFT 3
#define CFG_3_CF_DROP_MASK 0x20
#define CFG_3_CF_DROP_SHIFT 5
#define CFG_3_CF_TIMEOUT_MASK 0x3C0
#define CFG_3_CF_TIMEOUT_SHIFT 6
#define CFG_3_RX_IFG_TH_MASK 0x7C00
#define CFG_3_RX_IFG_TH_SHIFT 10
#define CFG_3_TX_CBFC_EN_MASK 0x8000
#define CFG_3_TX_CBFC_EN_SHIFT 15
#define CFG_3_MAX_LEN_MASK 0x3FFF0000
#define CFG_3_MAX_LEN_SHIFT 16
#define CFG_3_EXT_OOB_CBFC_SEL_MASK 0xC0000000
#define CFG_3_EXT_OOB_CBFC_SEL_SHIFT 30

/* GE MAC, PCS reset control register masks and shifts */
#define RST_SPCS_MASK 0x1
#define RST_SPCS_SHIFT 0
#define RST_GMAC_0_MASK 0x100
#define RST_GMAC_0_SHIFT 8

/* Tx phase sync FIFO control register masks and shifts */
#define PHASE_FIFO_CTL_RST_MASK 0x1
#define PHASE_FIFO_CTL_RST_SHIFT 0
#define PHASE_FIFO_CTL_INIT_MASK 0x2
#define PHASE_FIFO_CTL_INIT_SHIFT 1

/**
 * struct nps_enet_priv - Storage of ENET's private information.
 * @regs_base:      Base address of ENET memory-mapped control registers.
 * @irq:            For RX/TX IRQ number.
 * @tx_skb:         socket buffer of sent frame.
 * @napi:           Structure for NAPI.
 */
struct nps_enet_priv {
	void __iomem *regs_base;
	s32 irq;
	struct sk_buff *tx_skb;
	struct napi_struct napi;
	u32 ge_mac_cfg_2_value;
	u32 ge_mac_cfg_3_value;
};

/**
 * nps_enet_reg_set - Sets ENET register with provided value.
 * @priv:       Pointer to EZchip ENET private data structure.
 * @reg:        Register offset from base address.
 * @value:      Value to set in register.
 */
static inline void nps_enet_reg_set(struct nps_enet_priv *priv,
				    s32 reg, s32 value)
{
	iowrite32be(value, priv->regs_base + reg);
}

/**
 * nps_enet_reg_get - Gets value of specified ENET register.
 * @priv:       Pointer to EZchip ENET private data structure.
 * @reg:        Register offset from base address.
 *
 * returns:     Value of requested register.
 */
static inline u32 nps_enet_reg_get(struct nps_enet_priv *priv, s32 reg)
{
	return ioread32be(priv->regs_base + reg);
}

#endif /* _NPS_ENET_H */
