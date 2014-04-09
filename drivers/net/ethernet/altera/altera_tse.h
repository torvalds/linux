/*
 * Altera Triple-Speed Ethernet MAC driver
 *
 * Copyright (C) 2008-2013 Altera Corporation
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *
 * Original driver contributed by SLS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _ALTERA_TSE_H_
#define _ALTERA_TSE_H_

#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/bitops.h>

/************************************************************************/
/*                                                                      */
/* Altera Triple Speed Ethernet MAC IP related definitions              */
/*                                                                      */
/************************************************************************/

#define ALT_TSE_SW_RESET_WATCHDOG_CNTR		10000

/* MAC Command_Config Register Bit Definitions
 */
#define MAC_CMDCFG_TX_ENA		BIT(0)
#define MAC_CMDCFG_RX_ENA		BIT(1)
#define MAC_CMDCFG_ETH_SPEED		BIT(3)
#define MAC_CMDCFG_PROMIS_EN		BIT(4)
#define MAC_CMDCFG_PAD_EN		BIT(5)
#define MAC_CMDCFG_CRC_FWD		BIT(6)
#define MAC_CMDCFG_TX_ADDR_INS		BIT(9)
#define MAC_CMDCFG_HD_ENA		BIT(10)
#define MAC_CMDCFG_SW_RESET		BIT(13)
#define MAC_CMDCFG_LOOP_ENA		BIT(15)
#define MAC_CMDCFG_TX_ADDR_SEL(x)	(((x) & 0x7) << 16)
#define MAC_CMDCFG_CNTL_FRM_ENA		BIT(23)
#define MAC_CMDCFG_ENA_10		BIT(25)
#define MAC_CMDCFG_RX_ERR_DISC		BIT(26)
#define MAC_CMDCFG_CNT_RESET		BIT(31)

/* MDIO registers within MAC register Space
 */
struct alt_tse_mdio {
	unsigned int control;	/* PHY device operation control register */
	unsigned int status;	/* PHY device operation status register */
	unsigned int phy_id1;	/* Bits 31:16 of PHY identifier. */
	unsigned int phy_id2;	/* Bits 15:0 of PHY identifier. */
	unsigned int auto_negotiation_advertisement;/* Auto-negotiation
						      advertisement register.
						     */
	unsigned int remote_partner_base_page_ability;

	unsigned int reg6;
	unsigned int reg7;
	unsigned int reg8;
	unsigned int reg9;
	unsigned int rega;
	unsigned int regb;
	unsigned int regc;
	unsigned int regd;
	unsigned int rege;
	unsigned int regf;
	unsigned int reg10;
	unsigned int reg11;
	unsigned int reg12;
	unsigned int reg13;
	unsigned int reg14;
	unsigned int reg15;
	unsigned int reg16;
	unsigned int reg17;
	unsigned int reg18;
	unsigned int reg19;
	unsigned int reg1a;
	unsigned int reg1b;
	unsigned int reg1c;
	unsigned int reg1d;
	unsigned int reg1e;
	unsigned int reg1f;
};

/* MAC register Space
 */
struct alt_tse_mac {
	/* Bits 15:0: MegaCore function revision (0x0800). Bit 31:16: Customer
	   specific revision
	 */
	unsigned int	megacore_revision;
	/* Provides a memory location for user applications to test the device
	  memory operation.
	 */
	unsigned int	scratch_pad;
	/* The host processor uses this register to control and configure the
	  MAC block.
	 */
	unsigned int	command_config;
	/* 32-bit primary MAC address word 0 bits 0 to 31 of the primary
	  MAC address.
	 */
	unsigned int	mac_addr_0;
	/* 32-bit primary MAC address word 1 bits 32 to 47 of the primary
	  MAC address.
	 */
	unsigned int	mac_addr_1;
	/* 14-bit maximum frame length. The MAC receive logic */
	unsigned int	frm_length;
	/* The pause quanta is used in each pause frame sent to a remote
	   Ethernet device, in increments of 512 Ethernet bit times.
	 */
	unsigned int	pause_quanta;
	/* 12-bit receive FIFO section-empty threshold. */
	unsigned int	rx_section_empty;
	/* 12-bit receive FIFO section-full threshold */
	unsigned int	rx_section_full;
	/* 12-bit transmit FIFO section-empty threshold. */
	unsigned int	tx_section_empty;
	/* 12-bit transmit FIFO section-full threshold. */
	unsigned int	tx_section_full;
	/* 12-bit receive FIFO almost-empty threshold */
	unsigned int	rx_almost_empty;
	/* 12-bit receive FIFO almost-full threshold. */
	unsigned int	rx_almost_full;
	/* 12-bit transmit FIFO almost-empty threshold */
	unsigned int	tx_almost_empty;
	/* 12-bit transmit FIFO almost-full threshold */
	unsigned int	tx_almost_full;
	/* MDIO address of PHY Device 0. Bits 0 to 4 hold a 5-bit PHY address.*/
	unsigned int	mdio_phy0_addr;
	/* MDIO address of PHY Device 1. Bits 0 to 4 hold a 5-bit PHY address.*/
	unsigned int	mdio_phy1_addr;

	/* Bit[15:0]—16-bit holdoff quanta */
	unsigned int	holdoff_quant;

	/* only if 100/1000 BaseX PCS, reserved otherwise */
	unsigned int	reservedx44[5];

	/* Minimum IPG between consecutive transmit frame in terms of bytes */
	unsigned int	tx_ipg_length;

	/* IEEE 802.3 oEntity Managed Object Support */
	unsigned int	aMACID_1;	/*The MAC addresses*/
	unsigned int	aMACID_2;
	/* Number of frames transmitted without error including pause frames.*/
	unsigned int	aFramesTransmittedOK;
	/* Number of frames received without error including pause frames.*/
	unsigned int	aFramesReceivedOK;
	/* Number of frames received with a CRC error.*/
	unsigned int	aFramesCheckSequenceErrors;
	/* Frame received with an alignment error.*/
	unsigned int	aAlignmentErrors;
	/* Sum of payload and padding octets of frames transmitted without
	  error.
	 */
	unsigned int	aOctetsTransmittedOK;
	/*Sum of payload and padding octets of frames received without error.*/
	unsigned int	aOctetsReceivedOK;

	/* IEEE 802.3 oPausedEntity Managed Object Support */
	unsigned int	aTxPAUSEMACCtrlFrames;	/*Number of transmitted pause
						  frames.
						 */
	unsigned int	aRxPAUSEMACCtrlFrames;	/*Number of Received pause
						  frames.
						 */

	/* IETF MIB (MIB-II) Object Support */
	unsigned int	ifInErrors; /*Number of frames received with error*/
	unsigned int	ifOutErrors; /*Number of frames transmitted with error*/
	unsigned int	ifInUcastPkts; /*Number of valid received unicast
					 frames.
					*/
	unsigned int	ifInMulticastPkts; /*Number of valid received multicasts
					     frames (without pause).
				tse_priv->mac_dev->command_config.image	    */
	unsigned int	ifInBroadcastPkts;	/*Number of valid received
						  broadcast frames.
						 */
	unsigned int	ifOutDiscards;
	unsigned int	ifOutUcastPkts;
	unsigned int	ifOutMulticastPkts;
	unsigned int	ifOutBroadcastPkts;

	/* IETF RMON MIB Object Support */
	unsigned int	etherStatsDropEvents;	/* Counts the number of dropped
						  packets due to internal errors
						  of the MAC client.
						 */
	unsigned int	etherStatsOctets;	/* Total number of bytes
						  received. Good and bad
						  frames.
						 */
	unsigned int	etherStatsPkts;		/* Total number of packets
						  received. Counts good and bad
						  packets.
						 */
	unsigned int	etherStatsUndersizePkts; /* Number of packets received
						    with less than 64 bytes.
						  */
	unsigned int	etherStatsOversizePkts;	/* Number of each well-formed
						   packet that exceeds the valid
						   maximum programmed frame
						   length
						 */
	unsigned int	etherStatsPkts64Octets;	/*Number of received packet with
						  64 bytes
						 */
	/* Frames (good and bad) with 65 to 127 bytes */
	unsigned int	etherStatsPkts65to127Octets;
	/* Frames (good and bad) with 128 to 255 bytes */
	unsigned int	etherStatsPkts128to255Octets;
	/* Frames (good and bad) with 256 to 511 bytes */
	unsigned int	etherStatsPkts256to511Octets;
	/* Frames (good and bad) with 512 to 1023 bytes */
	unsigned int	etherStatsPkts512to1023Octets;
	/* Frames (good and bad) with 1024 to 1518 bytes */
	unsigned int	etherStatsPkts1024to1518Octets;

	/* Any frame length from 1519 to the maximum length configured in the
	   frm_length register, if it is greater than 1518.
	 */
	unsigned int	etherStatsPkts1519toXOctets;
	/* Too long frames with CRC error. */
	unsigned int	etherStatsJabbers;
	/* Too short frames with CRC error. */
	unsigned int	etherStatsFragments;

	unsigned int	reservedxE4;

	/* FIFO control register. */
	unsigned int	tx_cmd_stat;
	unsigned int	rx_cmd_stat;

	/* Extended Statistics Counters */
	unsigned int	msb_aOctetsTransmittedOK;
	unsigned int	msb_aOctetsReceivedOK;
	unsigned int	msb_etherStatsOctets;

	unsigned int	reserved1;

	/* Multicast address resolution table, mapped in the controller address
	  space.
	 */
	unsigned int	hash_table[64];

	/* Registers 0 to 31 within PHY device 0/1 connected to the MDIO PHY
	  management interface.
	 */
	struct alt_tse_mdio	mdio_phy0;
	struct alt_tse_mdio	mdio_phy1;

	/*4 Supplemental MAC Addresses*/
	unsigned int	supp_mac_addr_0_0;
	unsigned int	supp_mac_addr_0_1;
	unsigned int	supp_mac_addr_1_0;
	unsigned int	supp_mac_addr_1_1;
	unsigned int	supp_mac_addr_2_0;
	unsigned int	supp_mac_addr_2_1;
	unsigned int	supp_mac_addr_3_0;
	unsigned int	supp_mac_addr_3_1;

	unsigned int	reserved2[8];

	/* IEEE 1588v2 Feature */
	unsigned int	tx_period;
	unsigned int	tx_adjust_fns;
	unsigned int	tx_adjust_ns;
	unsigned int	rx_period;
	unsigned int	rx_adjust_fns;
	unsigned int	rx_adjust_ns;

	unsigned int	reservedx320[42];
};

/* Transmit and Receive Command Registers Bit Definitions
 */
#define ALT_TSE_TX_CMD_STAT_OMIT_CRC	BIT(17)
#define ALT_TSE_TX_CMD_STAT_TX_SHIFT16	BIT(18)
#define ALT_TSE_RX_CMD_STAT_RX_SHIFT16	BIT(25)

/* This structure is private to each device.
 */
struct alt_tse_private {
	struct net_device *dev;
	struct device *device;

	/* NAPI struct for NAPI interface */
	struct napi_struct napi;
	struct tse_regs *regs;

	unsigned int max_frame_size;
	unsigned int max_data_size;

	/* Rx bufers queue */
	dma_addr_t *rx_skbuff_dma;
	struct sk_buff **rx_skbuff;
	unsigned int rx_desc_num;
	unsigned int cur_rx_desc;
	unsigned int dirty_rx_desc;

	/* Tx buffers queue */
	dma_addr_t *tx_skbuff_dma;
	struct sk_buff **tx_skbuff;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	unsigned int dma_tx_size;

	unsigned int enable_sup_addr;
	unsigned int ena_hash;

	/* Interrupts */
	unsigned int tx_irq;
	unsigned int rx_irq;

	/* RX/TX FIFO depths */
	unsigned int tx_fifo_depth;
	unsigned int rx_fifo_depth;

	spinlock_t mac_cfg_lock;
	spinlock_t tx_lock;

	/* PHY */
	unsigned int mii_id;
	int phy_addr;		/* PHY's MDIO address, -1 for autodetection */
	phy_interface_t phy_iface;
	struct mii_bus *mdio;
	struct phy_device *phydev;
	int oldspeed;
	int oldduplex;
	int oldlink;

	u32 msg_enable;
};

/************************************************************************/
/*                                                                      */
/* Altera Modular SGDMA related definitions                             */
/*                                                                      */
/************************************************************************/

/* mSGDMA standard descriptor format
 */
struct msgdma_desc {
	u32 read_addr;	/* data buffer source address */
	u32 write_addr;	/* data buffer destination address */
	u32 len;	/* the number of bytes to transfer per descriptor */
	u32 control;	/* characteristics of the transfer */
};

/* mSGDMA extended descriptor format
 */
struct msgdma_extended_desc {
	u32 read_addr_lo;	/* data buffer source address low bits */
	u32 write_addr_lo;	/* data buffer destination address low bits */
	u32 len;		/* the number of bytes to transfer
				   per descriptor
				 */
	u32 burst_seq_num;	/* bit 31:24 write burst
				   bit 23:16 read burst
				   bit 15:0  sequence number
				 */
	u32 stride;		/* bit 31:16 write stride
				   bit 15:0  read stride
				 */
	u32 read_addr_hi;	/* data buffer source address high bits */
	u32 write_addr_hi;	/* data buffer destination address high bits */
	u32 control;		/* characteristics of the transfer */
};

/* mSGDMA descriptor control field bit definitions
 */
#define MSGDMA_DESC_CTL_SET_CH(x)	((x) & 0xff)
#define MSGDMA_DESC_CTL_GEN_SOP		BIT(8)
#define MSGDMA_DESC_CTL_GEN_EOP		BIT(9)
#define MSGDMA_DESC_CTL_PARK_READS	BIT(10)
#define MSGDMA_DESC_CTL_PARK_WRITES	BIT(11)
#define MSGDMA_DESC_CTL_END_ON_EOP	BIT(12)
#define MSGDMA_DESC_CTL_END_ON_LEN	BIT(13)
#define MSGDMA_DESC_CTL_TR_COMP_IRQ	BIT(14)
#define MSGDMA_DESC_CTL_EARLY_IRQ	BIT(15)
#define MSGDMA_DESC_CTL_TR_ERR_IRQ_MSK	(0xff << 16)
#define MSGDMA_DESC_CTL_EARLY_DONE	BIT(24)
/* Writing ‘1’ to the ‘go’ bit commits the entire descriptor into the
 * descriptor FIFO(s)
 */
#define MSGDMA_DESC_CTL_GO		BIT(31)

/* mSGDMA dispatcher control and status register map
 */
struct msgdma_csr {
	u32 status;		/* Read/Clear */
	u32 control;		/* Read/Write */
	u32 rw_fill_level;	/* bit 31:16 - write fill level
				   bit 15:0  - read fill level
				 */
	u32 resp_fill_level;	/* bit 15:0 */
	u32 rw_seq_num;		/* bit 31:16 - write sequence number
				   bit 15:0  - read sequence number
				 */
	u32 pad[3];		/* reserved */
};

/* mSGDMA CSR status register bit definitions
 */
#define MSGDMA_CSR_STAT_BUSY			BIT(0)
#define MSGDMA_CSR_STAT_DESC_BUF_EMPTY		BIT(1)
#define MSGDMA_CSR_STAT_DESC_BUF_FULL		BIT(2)
#define MSGDMA_CSR_STAT_RESP_BUF_EMPTY		BIT(3)
#define MSGDMA_CSR_STAT_RESP_BUF_FULL		BIT(4)
#define MSGDMA_CSR_STAT_STOPPED			BIT(5)
#define MSGDMA_CSR_STAT_RESETTING		BIT(6)
#define MSGDMA_CSR_STAT_STOPPED_ON_ERR		BIT(7)
#define MSGDMA_CSR_STAT_STOPPED_ON_EARLY	BIT(8)
#define MSGDMA_CSR_STAT_IRQ			BIT(9)
#define MSGDMA_CSR_STAT_MASK			0x3FF
#define MSGDMA_CSR_STAT_MASK_WITHOUT_IRQ	0x1FF

/* mSGDMA CSR control register bit definitions
 */
#define MSGDMA_CSR_CTL_STOP			BIT(0)
#define MSGDMA_CSR_CTL_RESET			BIT(1)
#define MSGDMA_CSR_CTL_STOP_ON_ERR		BIT(2)
#define MSGDMA_CSR_CTL_STOP_ON_EARLY		BIT(3)
#define MSGDMA_CSR_CTL_GLOBAL_INTR		BIT(4)
#define MSGDMA_CSR_CTL_STOP_DESCS		BIT(5)

/* mSGDMA response register map
 */
struct msgdma_response {
	u32 bytes_transferred;
	u32 status;
};

/* mSGDMA response register bit definitions
 */
#define MSGDMA_RESP_EARLY_TERM	BIT(8)
#define MSGDMA_RESP_ERR_MASK	0xFF

/* TSE I/O registers map
 */
struct tse_regs {
	struct alt_tse_mac mac;
	struct msgdma_csr tx_csr;		/* MM -> ST */
	struct msgdma_extended_desc tx_desc;	/* MM -> ST */
	struct msgdma_csr rx_csr;		/* MM <- ST */
	struct msgdma_extended_desc rx_desc;	/* MM <- ST */
	struct msgdma_response rx_resp;		/* MM <- ST */
};

/* Function prototypes */

extern void tse_set_ethtool_ops(struct net_device *netdev);

#endif /* _ALTERA_TSE_H_ */
