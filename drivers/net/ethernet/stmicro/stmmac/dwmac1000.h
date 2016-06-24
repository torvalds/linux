/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/
#ifndef __DWMAC1000_H__
#define __DWMAC1000_H__

#include <linux/phy.h>
#include "common.h"

#define GMAC_CONTROL		0x00000000	/* Configuration */
#define GMAC_FRAME_FILTER	0x00000004	/* Frame Filter */
#define GMAC_HASH_HIGH		0x00000008	/* Multicast Hash Table High */
#define GMAC_HASH_LOW		0x0000000c	/* Multicast Hash Table Low */
#define GMAC_MII_ADDR		0x00000010	/* MII Address */
#define GMAC_MII_DATA		0x00000014	/* MII Data */
#define GMAC_FLOW_CTRL		0x00000018	/* Flow Control */
#define GMAC_VLAN_TAG		0x0000001c	/* VLAN Tag */
#define GMAC_VERSION		0x00000020	/* GMAC CORE Version */
#define GMAC_DEBUG		0x00000024	/* GMAC debug register */
#define GMAC_WAKEUP_FILTER	0x00000028	/* Wake-up Frame Filter */

#define GMAC_INT_STATUS		0x00000038	/* interrupt status register */
#define GMAC_INT_STATUS_PMT	BIT(3)
#define GMAC_INT_STATUS_MMCIS	BIT(4)
#define GMAC_INT_STATUS_MMCRIS	BIT(5)
#define GMAC_INT_STATUS_MMCTIS	BIT(6)
#define GMAC_INT_STATUS_MMCCSUM	BIT(7)
#define GMAC_INT_STATUS_TSTAMP	BIT(9)
#define GMAC_INT_STATUS_LPIIS	BIT(10)

/* interrupt mask register */
#define	GMAC_INT_MASK		0x0000003c
#define	GMAC_INT_DISABLE_RGMII		BIT(0)
#define	GMAC_INT_DISABLE_PCSLINK	BIT(1)
#define	GMAC_INT_DISABLE_PCSAN		BIT(2)
#define	GMAC_INT_DISABLE_PMT		BIT(3)
#define	GMAC_INT_DISABLE_TIMESTAMP	BIT(9)
#define	GMAC_INT_DISABLE_PCS	(GMAC_INT_DISABLE_RGMII | \
				 GMAC_INT_DISABLE_PCSLINK | \
				 GMAC_INT_DISABLE_PCSAN)
#define	GMAC_INT_DEFAULT_MASK	(GMAC_INT_DISABLE_TIMESTAMP | \
				 GMAC_INT_DISABLE_PCS)

/* PMT Control and Status */
#define GMAC_PMT		0x0000002c
enum power_event {
	pointer_reset = 0x80000000,
	global_unicast = 0x00000200,
	wake_up_rx_frame = 0x00000040,
	magic_frame = 0x00000020,
	wake_up_frame_en = 0x00000004,
	magic_pkt_en = 0x00000002,
	power_down = 0x00000001,
};

/* Energy Efficient Ethernet (EEE)
 *
 * LPI status, timer and control register offset
 */
#define LPI_CTRL_STATUS	0x0030
#define LPI_TIMER_CTRL	0x0034

/* LPI control and status defines */
#define LPI_CTRL_STATUS_LPITXA	0x00080000	/* Enable LPI TX Automate */
#define LPI_CTRL_STATUS_PLSEN	0x00040000	/* Enable PHY Link Status */
#define LPI_CTRL_STATUS_PLS	0x00020000	/* PHY Link Status */
#define LPI_CTRL_STATUS_LPIEN	0x00010000	/* LPI Enable */
#define LPI_CTRL_STATUS_RLPIST	0x00000200	/* Receive LPI state */
#define LPI_CTRL_STATUS_TLPIST	0x00000100	/* Transmit LPI state */
#define LPI_CTRL_STATUS_RLPIEX	0x00000008	/* Receive LPI Exit */
#define LPI_CTRL_STATUS_RLPIEN	0x00000004	/* Receive LPI Entry */
#define LPI_CTRL_STATUS_TLPIEX	0x00000002	/* Transmit LPI Exit */
#define LPI_CTRL_STATUS_TLPIEN	0x00000001	/* Transmit LPI Entry */

/* GMAC HW ADDR regs */
#define GMAC_ADDR_HIGH(reg)	(((reg > 15) ? 0x00000800 : 0x00000040) + \
				(reg * 8))
#define GMAC_ADDR_LOW(reg)	(((reg > 15) ? 0x00000804 : 0x00000044) + \
				(reg * 8))
#define GMAC_MAX_PERFECT_ADDRESSES	1

#define GMAC_PCS_BASE		0x000000c0	/* PCS register base */
#define GMAC_RGSMIIIS		0x000000d8	/* RGMII/SMII status */

/* SGMII/RGMII status register */
#define GMAC_RGSMIIIS_LNKMODE		BIT(0)
#define GMAC_RGSMIIIS_SPEED		GENMASK(2, 1)
#define GMAC_RGSMIIIS_SPEED_SHIFT	1
#define GMAC_RGSMIIIS_LNKSTS		BIT(3)
#define GMAC_RGSMIIIS_JABTO		BIT(4)
#define GMAC_RGSMIIIS_FALSECARDET	BIT(5)
#define GMAC_RGSMIIIS_SMIDRXS		BIT(16)
/* LNKMOD */
#define GMAC_RGSMIIIS_LNKMOD_MASK	0x1
/* LNKSPEED */
#define GMAC_RGSMIIIS_SPEED_125		0x2
#define GMAC_RGSMIIIS_SPEED_25		0x1
#define GMAC_RGSMIIIS_SPEED_2_5		0x0

/* GMAC Configuration defines */
#define GMAC_CONTROL_2K 0x08000000	/* IEEE 802.3as 2K packets */
#define GMAC_CONTROL_TC	0x01000000	/* Transmit Conf. in RGMII/SGMII */
#define GMAC_CONTROL_WD	0x00800000	/* Disable Watchdog on receive */
#define GMAC_CONTROL_JD	0x00400000	/* Jabber disable */
#define GMAC_CONTROL_BE	0x00200000	/* Frame Burst Enable */
#define GMAC_CONTROL_JE	0x00100000	/* Jumbo frame */
enum inter_frame_gap {
	GMAC_CONTROL_IFG_88 = 0x00040000,
	GMAC_CONTROL_IFG_80 = 0x00020000,
	GMAC_CONTROL_IFG_40 = 0x000e0000,
};
#define GMAC_CONTROL_DCRS	0x00010000	/* Disable carrier sense */
#define GMAC_CONTROL_PS		0x00008000	/* Port Select 0:GMI 1:MII */
#define GMAC_CONTROL_FES	0x00004000	/* Speed 0:10 1:100 */
#define GMAC_CONTROL_DO		0x00002000	/* Disable Rx Own */
#define GMAC_CONTROL_LM		0x00001000	/* Loop-back mode */
#define GMAC_CONTROL_DM		0x00000800	/* Duplex Mode */
#define GMAC_CONTROL_IPC	0x00000400	/* Checksum Offload */
#define GMAC_CONTROL_DR		0x00000200	/* Disable Retry */
#define GMAC_CONTROL_LUD	0x00000100	/* Link up/down */
#define GMAC_CONTROL_ACS	0x00000080	/* Auto Pad/FCS Stripping */
#define GMAC_CONTROL_DC		0x00000010	/* Deferral Check */
#define GMAC_CONTROL_TE		0x00000008	/* Transmitter Enable */
#define GMAC_CONTROL_RE		0x00000004	/* Receiver Enable */

#define GMAC_CORE_INIT (GMAC_CONTROL_JD | GMAC_CONTROL_PS | GMAC_CONTROL_ACS | \
			GMAC_CONTROL_BE | GMAC_CONTROL_DCRS)

/* GMAC Frame Filter defines */
#define GMAC_FRAME_FILTER_PR	0x00000001	/* Promiscuous Mode */
#define GMAC_FRAME_FILTER_HUC	0x00000002	/* Hash Unicast */
#define GMAC_FRAME_FILTER_HMC	0x00000004	/* Hash Multicast */
#define GMAC_FRAME_FILTER_DAIF	0x00000008	/* DA Inverse Filtering */
#define GMAC_FRAME_FILTER_PM	0x00000010	/* Pass all multicast */
#define GMAC_FRAME_FILTER_DBF	0x00000020	/* Disable Broadcast frames */
#define GMAC_FRAME_FILTER_SAIF	0x00000100	/* Inverse Filtering */
#define GMAC_FRAME_FILTER_SAF	0x00000200	/* Source Address Filter */
#define GMAC_FRAME_FILTER_HPF	0x00000400	/* Hash or perfect Filter */
#define GMAC_FRAME_FILTER_RA	0x80000000	/* Receive all mode */
/* GMII ADDR  defines */
#define GMAC_MII_ADDR_WRITE	0x00000002	/* MII Write */
#define GMAC_MII_ADDR_BUSY	0x00000001	/* MII Busy */
/* GMAC FLOW CTRL defines */
#define GMAC_FLOW_CTRL_PT_MASK	0xffff0000	/* Pause Time Mask */
#define GMAC_FLOW_CTRL_PT_SHIFT	16
#define GMAC_FLOW_CTRL_UP	0x00000008	/* Unicast pause frame enable */
#define GMAC_FLOW_CTRL_RFE	0x00000004	/* Rx Flow Control Enable */
#define GMAC_FLOW_CTRL_TFE	0x00000002	/* Tx Flow Control Enable */
#define GMAC_FLOW_CTRL_FCB_BPA	0x00000001	/* Flow Control Busy ... */

/* DEBUG Register defines */
/* MTL TxStatus FIFO */
#define GMAC_DEBUG_TXSTSFSTS	BIT(25)	/* MTL TxStatus FIFO Full Status */
#define GMAC_DEBUG_TXFSTS	BIT(24) /* MTL Tx FIFO Not Empty Status */
#define GMAC_DEBUG_TWCSTS	BIT(22) /* MTL Tx FIFO Write Controller */
/* MTL Tx FIFO Read Controller Status */
#define GMAC_DEBUG_TRCSTS_MASK	GENMASK(21, 20)
#define GMAC_DEBUG_TRCSTS_SHIFT	20
#define GMAC_DEBUG_TRCSTS_IDLE	0
#define GMAC_DEBUG_TRCSTS_READ	1
#define GMAC_DEBUG_TRCSTS_TXW	2
#define GMAC_DEBUG_TRCSTS_WRITE	3
#define GMAC_DEBUG_TXPAUSED	BIT(19) /* MAC Transmitter in PAUSE */
/* MAC Transmit Frame Controller Status */
#define GMAC_DEBUG_TFCSTS_MASK	GENMASK(18, 17)
#define GMAC_DEBUG_TFCSTS_SHIFT	17
#define GMAC_DEBUG_TFCSTS_IDLE	0
#define GMAC_DEBUG_TFCSTS_WAIT	1
#define GMAC_DEBUG_TFCSTS_GEN_PAUSE	2
#define GMAC_DEBUG_TFCSTS_XFER	3
/* MAC GMII or MII Transmit Protocol Engine Status */
#define GMAC_DEBUG_TPESTS	BIT(16)
#define GMAC_DEBUG_RXFSTS_MASK	GENMASK(9, 8) /* MTL Rx FIFO Fill-level */
#define GMAC_DEBUG_RXFSTS_SHIFT	8
#define GMAC_DEBUG_RXFSTS_EMPTY	0
#define GMAC_DEBUG_RXFSTS_BT	1
#define GMAC_DEBUG_RXFSTS_AT	2
#define GMAC_DEBUG_RXFSTS_FULL	3
#define GMAC_DEBUG_RRCSTS_MASK	GENMASK(6, 5) /* MTL Rx FIFO Read Controller */
#define GMAC_DEBUG_RRCSTS_SHIFT	5
#define GMAC_DEBUG_RRCSTS_IDLE	0
#define GMAC_DEBUG_RRCSTS_RDATA	1
#define GMAC_DEBUG_RRCSTS_RSTAT	2
#define GMAC_DEBUG_RRCSTS_FLUSH	3
#define GMAC_DEBUG_RWCSTS	BIT(4) /* MTL Rx FIFO Write Controller Active */
/* MAC Receive Frame Controller FIFO Status */
#define GMAC_DEBUG_RFCFCSTS_MASK	GENMASK(2, 1)
#define GMAC_DEBUG_RFCFCSTS_SHIFT	1
/* MAC GMII or MII Receive Protocol Engine Status */
#define GMAC_DEBUG_RPESTS	BIT(0)

/*--- DMA BLOCK defines ---*/
/* DMA Bus Mode register defines */
#define DMA_BUS_MODE_DA		0x00000002	/* Arbitration scheme */
#define DMA_BUS_MODE_DSL_MASK	0x0000007c	/* Descriptor Skip Length */
#define DMA_BUS_MODE_DSL_SHIFT	2		/*   (in DWORDS)      */
/* Programmable burst length (passed thorugh platform)*/
#define DMA_BUS_MODE_PBL_MASK	0x00003f00	/* Programmable Burst Len */
#define DMA_BUS_MODE_PBL_SHIFT	8
#define DMA_BUS_MODE_ATDS	0x00000080	/* Alternate Descriptor Size */

enum rx_tx_priority_ratio {
	double_ratio = 0x00004000,	/* 2:1 */
	triple_ratio = 0x00008000,	/* 3:1 */
	quadruple_ratio = 0x0000c000,	/* 4:1 */
};

#define DMA_BUS_MODE_FB		0x00010000	/* Fixed burst */
#define DMA_BUS_MODE_MB		0x04000000	/* Mixed burst */
#define DMA_BUS_MODE_RPBL_MASK	0x003e0000	/* Rx-Programmable Burst Len */
#define DMA_BUS_MODE_RPBL_SHIFT	17
#define DMA_BUS_MODE_USP	0x00800000
#define DMA_BUS_MODE_MAXPBL	0x01000000
#define DMA_BUS_MODE_AAL	0x02000000

/* DMA CRS Control and Status Register Mapping */
#define DMA_HOST_TX_DESC	  0x00001048	/* Current Host Tx descriptor */
#define DMA_HOST_RX_DESC	  0x0000104c	/* Current Host Rx descriptor */
/*  DMA Bus Mode register defines */
#define DMA_BUS_PR_RATIO_MASK	  0x0000c000	/* Rx/Tx priority ratio */
#define DMA_BUS_PR_RATIO_SHIFT	  14
#define DMA_BUS_FB	  	  0x00010000	/* Fixed Burst */

/* DMA operation mode defines (start/stop tx/rx are placed in common header)*/
/* Disable Drop TCP/IP csum error */
#define DMA_CONTROL_DT		0x04000000
#define DMA_CONTROL_RSF		0x02000000	/* Receive Store and Forward */
#define DMA_CONTROL_DFF		0x01000000	/* Disaable flushing */
/* Threshold for Activating the FC */
enum rfa {
	act_full_minus_1 = 0x00800000,
	act_full_minus_2 = 0x00800200,
	act_full_minus_3 = 0x00800400,
	act_full_minus_4 = 0x00800600,
};
/* Threshold for Deactivating the FC */
enum rfd {
	deac_full_minus_1 = 0x00400000,
	deac_full_minus_2 = 0x00400800,
	deac_full_minus_3 = 0x00401000,
	deac_full_minus_4 = 0x00401800,
};
#define DMA_CONTROL_TSF	0x00200000	/* Transmit  Store and Forward */

enum ttc_control {
	DMA_CONTROL_TTC_64 = 0x00000000,
	DMA_CONTROL_TTC_128 = 0x00004000,
	DMA_CONTROL_TTC_192 = 0x00008000,
	DMA_CONTROL_TTC_256 = 0x0000c000,
	DMA_CONTROL_TTC_40 = 0x00010000,
	DMA_CONTROL_TTC_32 = 0x00014000,
	DMA_CONTROL_TTC_24 = 0x00018000,
	DMA_CONTROL_TTC_16 = 0x0001c000,
};
#define DMA_CONTROL_TC_TX_MASK	0xfffe3fff

#define DMA_CONTROL_EFC		0x00000100
#define DMA_CONTROL_FEF		0x00000080
#define DMA_CONTROL_FUF		0x00000040

/* Receive flow control activation field
 * RFA field in DMA control register, bits 23,10:9
 */
#define DMA_CONTROL_RFA_MASK	0x00800600

/* Receive flow control deactivation field
 * RFD field in DMA control register, bits 22,12:11
 */
#define DMA_CONTROL_RFD_MASK	0x00401800

/* RFD and RFA fields are encoded as follows
 *
 *   Bit Field
 *   0,00 - Full minus 1KB (only valid when rxfifo >= 4KB and EFC enabled)
 *   0,01 - Full minus 2KB (only valid when rxfifo >= 4KB and EFC enabled)
 *   0,10 - Full minus 3KB (only valid when rxfifo >= 4KB and EFC enabled)
 *   0,11 - Full minus 4KB (only valid when rxfifo > 4KB and EFC enabled)
 *   1,00 - Full minus 5KB (only valid when rxfifo > 8KB and EFC enabled)
 *   1,01 - Full minus 6KB (only valid when rxfifo > 8KB and EFC enabled)
 *   1,10 - Full minus 7KB (only valid when rxfifo > 8KB and EFC enabled)
 *   1,11 - Reserved
 *
 * RFD should always be > RFA for a given FIFO size. RFD == RFA may work,
 * but packet throughput performance may not be as expected.
 *
 * Be sure that bit 3 in GMAC Register 6 is set for Unicast Pause frame
 * detection (IEEE Specification Requirement, Annex 31B, 31B.1, Pause
 * Description).
 *
 * Be sure that DZPA (bit 7 in Flow Control Register, GMAC Register 6),
 * is set to 0. This allows pause frames with a quanta of 0 to be sent
 * as an XOFF message to the link peer.
 */

#define RFA_FULL_MINUS_1K	0x00000000
#define RFA_FULL_MINUS_2K	0x00000200
#define RFA_FULL_MINUS_3K	0x00000400
#define RFA_FULL_MINUS_4K	0x00000600
#define RFA_FULL_MINUS_5K	0x00800000
#define RFA_FULL_MINUS_6K	0x00800200
#define RFA_FULL_MINUS_7K	0x00800400

#define RFD_FULL_MINUS_1K	0x00000000
#define RFD_FULL_MINUS_2K	0x00000800
#define RFD_FULL_MINUS_3K	0x00001000
#define RFD_FULL_MINUS_4K	0x00001800
#define RFD_FULL_MINUS_5K	0x00400000
#define RFD_FULL_MINUS_6K	0x00400800
#define RFD_FULL_MINUS_7K	0x00401000

enum rtc_control {
	DMA_CONTROL_RTC_64 = 0x00000000,
	DMA_CONTROL_RTC_32 = 0x00000008,
	DMA_CONTROL_RTC_96 = 0x00000010,
	DMA_CONTROL_RTC_128 = 0x00000018,
};
#define DMA_CONTROL_TC_RX_MASK	0xffffffe7

#define DMA_CONTROL_OSF	0x00000004	/* Operate on second frame */

/* MMC registers offset */
#define GMAC_MMC_CTRL      0x100
#define GMAC_MMC_RX_INTR   0x104
#define GMAC_MMC_TX_INTR   0x108
#define GMAC_MMC_RX_CSUM_OFFLOAD   0x208
#define GMAC_EXTHASH_BASE  0x500

extern const struct stmmac_dma_ops dwmac1000_dma_ops;
#endif /* __DWMAC1000_H__ */
