/*
 * Copyright(c) 2015 EZchip Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
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
#define NPS_ENET_REG_BUF_INT_CAUSE	0x8C4
#define NPS_ENET_REG_GE_MAC_CFG_0	0x1000
#define NPS_ENET_REG_GE_MAC_CFG_1	0x1004
#define NPS_ENET_REG_GE_MAC_CFG_2	0x1008
#define NPS_ENET_REG_GE_MAC_CFG_3	0x100C
#define NPS_ENET_REG_GE_RST		0x1400
#define NPS_ENET_REG_PHASE_FIFO_CTL	0x1404

/* Tx control register */
struct nps_enet_tx_ctl {
	union {
		/* ct: SW sets to indicate frame ready in Tx buffer for
		 *     transmission. HW resets to when transmission done
		 * et: Transmit error
		 * nt: Length in bytes of Tx frame loaded to Tx buffer
		 */
		struct {
			u32
			__reserved_1:16,
			ct:1,
			et:1,
			__reserved_2:3,
			nt:11;
		};

		u32 value;
	};
};

/* Rx control register */
struct nps_enet_rx_ctl {
	union {
		/* cr:  HW sets to indicate frame ready in Rx buffer.
		 *      SW resets to indicate host read received frame
		 *      and new frames can be written to Rx buffer
		 * er:  Rx error indication
		 * crc: Rx CRC error indication
		 * nr:  Length in bytes of Rx frame loaded by MAC to Rx buffer
		 */
		struct {
			u32
			__reserved_1:16,
			cr:1,
			er:1,
			crc:1,
			__reserved_2:2,
			nr:11;
		};

		u32 value;
	};
};

/* Interrupt enable for data buffer events register */
struct nps_enet_buf_int_enable {
	union {
		/* tx_done: Interrupt generation in the case when new frame
		 *          is ready in Rx buffer
		 * rx_rdy:  Interrupt generation in the case when current frame
		 *          was read from TX buffer
		 */
		struct {
			u32
			__reserved:30,
			tx_done:1,
			rx_rdy:1;
		};

		u32 value;
	};
};

/* Interrupt cause for data buffer events register */
struct nps_enet_buf_int_cause {
	union {
		/* tx_done: Interrupt in the case when current frame was
		 *          read from TX buffer.
		 * rx_rdy:  Interrupt in the case when new frame is ready
		 *          in RX buffer.
		 */
		struct {
			u32
			__reserved:30,
			tx_done:1,
			rx_rdy:1;
		};

		u32 value;
	};
};

/* Gbps Eth MAC Configuration 0 register */
struct nps_enet_ge_mac_cfg_0 {
	union {
		/* tx_pr_len:          Transmit preamble length in bytes
		 * tx_ifg_nib:         Tx idle pattern
		 * nib_mode:           Nibble (4-bit) Mode
		 * rx_pr_check_en:     Receive preamble Check Enable
		 * tx_ifg:             Transmit inter-Frame Gap
		 * rx_ifg:             Receive inter-Frame Gap
		 * tx_fc_retr:         Transmit Flow Control Retransmit Mode
		 * rx_length_check_en: Receive Length Check Enable
		 * rx_crc_ignore:      Results of the CRC check are ignored
		 * rx_crc_strip:       MAC strips the CRC from received frames
		 * rx_fc_en:           Receive Flow Control Enable
		 * tx_crc_en:          Transmit CRC Enabled
		 * tx_pad_en:          Transmit Padding Enable
		 * tx_cf_en:           Transmit Flow Control Enable
		 * tx_en:              Transmit Enable
		 * rx_en:              Receive Enable
		 */
		struct {
			u32
			tx_pr_len:4,
			tx_ifg_nib:4,
			nib_mode:1,
			rx_pr_check_en:1,
			tx_ifg:6,
			rx_ifg:4,
			tx_fc_retr:3,
			rx_length_check_en:1,
			rx_crc_ignore:1,
			rx_crc_strip:1,
			rx_fc_en:1,
			tx_crc_en:1,
			tx_pad_en:1,
			tx_fc_en:1,
			tx_en:1,
			rx_en:1;
		};

		u32 value;
	};
};

/* Gbps Eth MAC Configuration 1 register */
struct nps_enet_ge_mac_cfg_1 {
	union {
		/* octet_3: MAC address octet 3
		 * octet_2: MAC address octet 2
		 * octet_1: MAC address octet 1
		 * octet_0: MAC address octet 0
		 */
		struct {
			u32
			octet_3:8,
			octet_2:8,
			octet_1:8,
			octet_0:8;
		};

		u32 value;
	};
};

/* Gbps Eth MAC Configuration 2 register */
struct nps_enet_ge_mac_cfg_2 {
	union {
		/* transmit_flush_en: MAC flush enable
		 * stat_en:           RMON statistics interface enable
		 * disc_da:           Discard frames with DA different
		 *                    from MAC address
		 * disc_bc:           Discard broadcast frames
		 * disc_mc:           Discard multicast frames
		 * octet_5:           MAC address octet 5
		 * octet_4:           MAC address octet 4
		 */
		struct {
			u32
			transmit_flush_en:1,
			__reserved_1:5,
			stat_en:2,
			__reserved_2:1,
			disc_da:1,
			disc_bc:1,
			disc_mc:1,
			__reserved_3:4,
			octet_5:8,
			octet_4:8;
		};

		u32 value;
	};
};

/* Gbps Eth MAC Configuration 3 register */
struct nps_enet_ge_mac_cfg_3 {
	union {
		/* ext_oob_cbfc_sel:  Selects one of the 4 profiles for
		 *                    extended OOB in-flow-control indication
		 * max_len:           Maximum receive frame length in bytes
		 * tx_cbfc_en:        Enable transmission of class-based
		 *                    flow control packets
		 * rx_ifg_th:         Threshold for IFG status reporting via OOB
		 * cf_timeout:        Configurable time to decrement FC counters
		 * cf_drop:           Drop control frames
		 * redirect_cbfc_sel: Selects one of CBFC redirect profiles
		 * rx_cbfc_redir_en:  Enable Rx class-based flow
		 *                    control redirect
		 * rx_cbfc_en:        Enable Rx class-based flow control
		 * tm_hd_mode:        TM header mode
		 */
		struct {
			u32
			ext_oob_cbfc_sel:2,
			max_len:14,
			tx_cbfc_en:1,
			rx_ifg_th:5,
			cf_timeout:4,
			cf_drop:1,
			redirect_cbfc_sel:2,
			rx_cbfc_redir_en:1,
			rx_cbfc_en:1,
			tm_hd_mode:1;
		};

		u32 value;
	};
};

/* GE MAC, PCS reset control register */
struct nps_enet_ge_rst {
	union {
		/* gmac_0: GE MAC reset
		 * spcs_0: SGMII PCS reset
		 */
		struct {
			u32
			__reserved_1:23,
			gmac_0:1,
			__reserved_2:7,
			spcs_0:1;
		};

		u32 value;
	};
};

/* Tx phase sync FIFO control register */
struct nps_enet_phase_fifo_ctl {
	union {
		/* init: initialize serdes TX phase sync FIFO pointers
		 * rst:  reset serdes TX phase sync FIFO
		 */
		struct {
			u32
			__reserved:30,
			init:1,
			rst:1;
		};

		u32 value;
	};
};

/**
 * struct nps_enet_priv - Storage of ENET's private information.
 * @regs_base:      Base address of ENET memory-mapped control registers.
 * @irq:            For RX/TX IRQ number.
 * @tx_packet_sent: SW indication if frame is being sent.
 * @tx_skb:         socket buffer of sent frame.
 * @napi:           Structure for NAPI.
 */
struct nps_enet_priv {
	void __iomem *regs_base;
	s32 irq;
	bool tx_packet_sent;
	struct sk_buff *tx_skb;
	struct napi_struct napi;
	struct nps_enet_ge_mac_cfg_2 ge_mac_cfg_2;
	struct nps_enet_ge_mac_cfg_3 ge_mac_cfg_3;
};

/**
 * nps_reg_set - Sets ENET register with provided value.
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
 * nps_reg_get - Gets value of specified ENET register.
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
