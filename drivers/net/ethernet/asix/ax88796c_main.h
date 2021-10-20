/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010 ASIX Electronics Corporation
 * Copyright (c) 2020 Samsung Electronics
 *
 * ASIX AX88796C SPI Fast Ethernet Linux driver
 */

#ifndef _AX88796C_MAIN_H
#define _AX88796C_MAIN_H

#include <linux/netdevice.h>
#include <linux/mii.h>

#include "ax88796c_spi.h"

/* These identify the driver base version and may not be removed. */
#define DRV_NAME	"ax88796c"
#define ADP_NAME	"ASIX AX88796C SPI Ethernet Adapter"

#define TX_QUEUE_HIGH_WATER		45	/* Tx queue high water mark */
#define TX_QUEUE_LOW_WATER		20	/* Tx queue low water mark */

#define AX88796C_REGDUMP_LEN		256
#define AX88796C_PHY_REGDUMP_LEN	14
#define AX88796C_PHY_ID			0x10

#define TX_OVERHEAD			8
#define TX_EOP_SIZE			4

#define AX_MCAST_FILTER_SIZE		8
#define AX_MAX_MCAST			64
#define AX_MAX_CLK                      80000000
#define TX_HDR_SOP_DICF			0x8000
#define TX_HDR_SOP_CPHI			0x4000
#define TX_HDR_SOP_INT			0x2000
#define TX_HDR_SOP_MDEQ			0x1000
#define TX_HDR_SOP_PKTLEN		0x07FF
#define TX_HDR_SOP_SEQNUM		0xF800
#define TX_HDR_SOP_PKTLENBAR		0x07FF

#define TX_HDR_SEG_FS			0x8000
#define TX_HDR_SEG_LS			0x4000
#define TX_HDR_SEG_SEGNUM		0x3800
#define TX_HDR_SEG_SEGLEN		0x0700
#define TX_HDR_SEG_EOFST		0xC000
#define TX_HDR_SEG_SOFST		0x3800
#define TX_HDR_SEG_SEGLENBAR		0x07FF

#define TX_HDR_EOP_SEQNUM		0xF800
#define TX_HDR_EOP_PKTLEN		0x07FF
#define TX_HDR_EOP_SEQNUMBAR		0xF800
#define TX_HDR_EOP_PKTLENBAR		0x07FF

/* Rx header fields mask */
#define RX_HDR1_MCBC			0x8000
#define RX_HDR1_STUFF_PKT		0x4000
#define RX_HDR1_MII_ERR			0x2000
#define RX_HDR1_CRC_ERR			0x1000
#define RX_HDR1_PKT_LEN			0x07FF

#define RX_HDR2_SEQ_NUM			0xF800
#define RX_HDR2_PKT_LEN_BAR		0x7FFF

#define RX_HDR3_PE			0x8000
#define RX_HDR3_L3_TYPE_IPV4V6		0x6000
#define RX_HDR3_L3_TYPE_IP		0x4000
#define RX_HDR3_L3_TYPE_IPV6		0x2000
#define RX_HDR3_L4_TYPE_ICMPV6		0x1400
#define RX_HDR3_L4_TYPE_TCP		0x1000
#define RX_HDR3_L4_TYPE_IGMP		0x0c00
#define RX_HDR3_L4_TYPE_ICMP		0x0800
#define RX_HDR3_L4_TYPE_UDP		0x0400
#define RX_HDR3_L3_ERR			0x0200
#define RX_HDR3_L4_ERR			0x0100
#define RX_HDR3_PRIORITY(x)		((x) << 4)
#define RX_HDR3_STRIP			0x0008
#define RX_HDR3_VLAN_ID			0x0007

struct ax88796c_pcpu_stats {
	u64_stats_t rx_packets;
	u64_stats_t rx_bytes;
	u64_stats_t tx_packets;
	u64_stats_t tx_bytes;
	struct u64_stats_sync syncp;
	u32 rx_dropped;
	u32 tx_dropped;
	u32 rx_frame_errors;
	u32 rx_crc_errors;
};

struct ax88796c_device {
	struct spi_device	*spi;
	struct net_device	*ndev;
	struct ax88796c_pcpu_stats __percpu *stats;

	struct work_struct	ax_work;

	struct mutex		spi_lock; /* device access */

	struct sk_buff_head	tx_wait_q;

	struct axspi_data	ax_spi;

	struct mii_bus		*mdiobus;
	struct phy_device	*phydev;

	int			msg_enable;

	u16			seq_num;

	u8			multi_filter[AX_MCAST_FILTER_SIZE];

	int			link;
	int			speed;
	int			duplex;
	int			pause;
	int			asym_pause;
	int			flowctrl;
		#define AX_FC_NONE		0
		#define AX_FC_RX		BIT(0)
		#define AX_FC_TX		BIT(1)
		#define AX_FC_ANEG		BIT(2)

	u32			priv_flags;
		#define AX_CAP_COMP		BIT(0)
		#define AX_PRIV_FLAGS_MASK	(AX_CAP_COMP)

	unsigned long		flags;
		#define EVENT_INTR		BIT(0)
		#define EVENT_TX		BIT(1)
		#define EVENT_SET_MULTI		BIT(2)

};

#define to_ax88796c_device(ndev) ((struct ax88796c_device *)netdev_priv(ndev))

enum skb_state {
	illegal = 0,
	tx_done,
	rx_done,
	rx_err,
};

struct skb_data {
	enum skb_state state;
	size_t len;
};

/* A88796C register definition */
	/* Definition of PAGE0 */
#define P0_PSR		(0x00)
	#define PSR_DEV_READY		BIT(7)
	#define PSR_RESET		(0 << 15)
	#define PSR_RESET_CLR		BIT(15)
#define P0_BOR		(0x02)
#define P0_FER		(0x04)
	#define FER_IPALM		BIT(0)
	#define FER_DCRC		BIT(1)
	#define FER_RH3M		BIT(2)
	#define FER_HEADERSWAP		BIT(7)
	#define FER_WSWAP		BIT(8)
	#define FER_BSWAP		BIT(9)
	#define FER_INTHI		BIT(10)
	#define FER_INTLO		(0 << 10)
	#define FER_IRQ_PULL		BIT(11)
	#define FER_RXEN		BIT(14)
	#define FER_TXEN		BIT(15)
#define P0_ISR		(0x06)
	#define ISR_RXPKT		BIT(0)
	#define ISR_MDQ			BIT(4)
	#define ISR_TXT			BIT(5)
	#define ISR_TXPAGES		BIT(6)
	#define ISR_TXERR		BIT(8)
	#define ISR_LINK		BIT(9)
#define P0_IMR		(0x08)
	#define IMR_RXPKT		BIT(0)
	#define IMR_MDQ			BIT(4)
	#define IMR_TXT			BIT(5)
	#define IMR_TXPAGES		BIT(6)
	#define IMR_TXERR		BIT(8)
	#define IMR_LINK		BIT(9)
	#define IMR_MASKALL		(0xFFFF)
	#define IMR_DEFAULT		(IMR_TXERR)
#define P0_WFCR		(0x0A)
	#define WFCR_PMEIND		BIT(0) /* PME indication */
	#define WFCR_PMETYPE		BIT(1) /* PME I/O type */
	#define WFCR_PMEPOL		BIT(2) /* PME polarity */
	#define WFCR_PMERST		BIT(3) /* Reset PME */
	#define WFCR_SLEEP		BIT(4) /* Enable sleep mode */
	#define WFCR_WAKEUP		BIT(5) /* Enable wakeup mode */
	#define WFCR_WAITEVENT		BIT(6) /* Reserved */
	#define WFCR_CLRWAKE		BIT(7) /* Clear wakeup */
	#define WFCR_LINKCH		BIT(8) /* Enable link change */
	#define WFCR_MAGICP		BIT(9) /* Enable magic packet */
	#define WFCR_WAKEF		BIT(10) /* Enable wakeup frame */
	#define WFCR_PMEEN		BIT(11) /* Enable PME pin */
	#define WFCR_LINKCHS		BIT(12) /* Link change status */
	#define WFCR_MAGICPS		BIT(13) /* Magic packet status */
	#define WFCR_WAKEFS		BIT(14) /* Wakeup frame status */
	#define WFCR_PMES		BIT(15) /* PME pin status */
#define P0_PSCR		(0x0C)
	#define PSCR_PS_MASK		(0xFFF0)
	#define PSCR_PS_D0		(0)
	#define PSCR_PS_D1		BIT(0)
	#define PSCR_PS_D2		BIT(1)
	#define PSCR_FPS		BIT(3) /* Enable fiber mode PS */
	#define PSCR_SWPS		BIT(4) /* Enable software */
						 /* PS control */
	#define PSCR_WOLPS		BIT(5) /* Enable WOL PS */
	#define PSCR_SWWOL		BIT(6) /* Enable software select */
						 /* WOL PS */
	#define PSCR_PHYOSC		BIT(7) /* Internal PHY OSC control */
	#define PSCR_FOFEF		BIT(8) /* Force PHY generate FEF */
	#define PSCR_FOF		BIT(9) /* Force PHY in fiber mode */
	#define PSCR_PHYPD		BIT(10) /* PHY power down. */
						  /* Active high */
	#define PSCR_PHYRST		BIT(11) /* PHY reset signal. */
						  /* Active low */
	#define PSCR_PHYCSIL		BIT(12) /* PHY cable energy detect */
	#define PSCR_PHYCOFF		BIT(13) /* PHY cable off */
	#define PSCR_PHYLINK		BIT(14) /* PHY link status */
	#define PSCR_EEPOK		BIT(15) /* EEPROM load complete */
#define P0_MACCR	(0x0E)
	#define MACCR_RXEN		BIT(0) /* Enable RX */
	#define MACCR_DUPLEX_FULL	BIT(1) /* 1: Full, 0: Half */
	#define MACCR_SPEED_100		BIT(2) /* 1: 100Mbps, 0: 10Mbps */
	#define MACCR_RXFC_ENABLE	BIT(3)
	#define MACCR_RXFC_MASK		0xFFF7
	#define MACCR_TXFC_ENABLE	BIT(4)
	#define MACCR_TXFC_MASK		0xFFEF
	#define MACCR_PSI		BIT(6) /* Software Cable-Off */
					       /* Power Saving Interrupt */
	#define MACCR_PF		BIT(7)
	#define MACCR_PMM_BITS		8
	#define MACCR_PMM_MASK		(0x1F00)
	#define MACCR_PMM_RESET		BIT(8)
	#define MACCR_PMM_WAIT		(2 << 8)
	#define MACCR_PMM_READY		(3 << 8)
	#define MACCR_PMM_D1		(4 << 8)
	#define MACCR_PMM_D2		(5 << 8)
	#define MACCR_PMM_WAKE		(7 << 8)
	#define MACCR_PMM_D1_WAKE	(8 << 8)
	#define MACCR_PMM_D2_WAKE	(9 << 8)
	#define MACCR_PMM_SLEEP		(10 << 8)
	#define MACCR_PMM_PHY_RESET	(11 << 8)
	#define MACCR_PMM_SOFT_D1	(16 << 8)
	#define MACCR_PMM_SOFT_D2	(17 << 8)
#define P0_TFBFCR	(0x10)
	#define TFBFCR_SCHE_FREE_PAGE	0xE07F
	#define TFBFCR_FREE_PAGE_BITS	0x07
	#define TFBFCR_FREE_PAGE_LATCH	BIT(6)
	#define TFBFCR_SET_FREE_PAGE(x)	(((x) & 0x3F) << TFBFCR_FREE_PAGE_BITS)
	#define TFBFCR_TX_PAGE_SET	BIT(13)
	#define TFBFCR_MANU_ENTX	BIT(15)
	#define TX_FREEBUF_MASK		0x003F
	#define TX_DPTSTART		0x4000

#define P0_TSNR		(0x12)
	#define TXNR_TXB_ERR		BIT(5)
	#define TXNR_TXB_IDLE		BIT(6)
	#define TSNR_PKT_CNT(x)		(((x) & 0x3F) << 8)
	#define TXNR_TXB_REINIT		BIT(14)
	#define TSNR_TXB_START		BIT(15)
#define P0_RTDPR	(0x14)
#define P0_RXBCR1	(0x16)
	#define RXBCR1_RXB_DISCARD	BIT(14)
	#define RXBCR1_RXB_START	BIT(15)
#define P0_RXBCR2	(0x18)
	#define RXBCR2_PKT_MASK		(0xFF)
	#define RXBCR2_RXPC_MASK	(0x7F)
	#define RXBCR2_RXB_READY	BIT(13)
	#define RXBCR2_RXB_IDLE		BIT(14)
	#define RXBCR2_RXB_REINIT	BIT(15)
#define P0_RTWCR	(0x1A)
	#define RTWCR_RXWC_MASK		(0x3FFF)
	#define RTWCR_RX_LATCH		BIT(15)
#define P0_RCPHR	(0x1C)

	/* Definition of PAGE1 */
#define P1_RPPER	(0x22)
	#define RPPER_RXEN		BIT(0)
#define P1_MRCR		(0x28)
#define P1_MDR		(0x2A)
#define P1_RMPR		(0x2C)
#define P1_TMPR		(0x2E)
#define P1_RXBSPCR	(0x30)
	#define RXBSPCR_STUF_WORD_CNT(x)	(((x) & 0x7000) >> 12)
	#define RXBSPCR_STUF_ENABLE		BIT(15)
#define P1_MCR		(0x32)
	#define MCR_SBP			BIT(8)
	#define MCR_SM			BIT(9)
	#define MCR_CRCENLAN		BIT(11)
	#define MCR_STP			BIT(12)
	/* Definition of PAGE2 */
#define P2_CIR		(0x42)
#define P2_PCR		(0x44)
	#define PCR_POLL_EN		BIT(0)
	#define PCR_POLL_FLOWCTRL	BIT(1)
	#define PCR_POLL_BMCR		BIT(2)
	#define PCR_PHYID(x)		((x) << 8)
#define P2_PHYSR	(0x46)
#define P2_MDIODR	(0x48)
#define P2_MDIOCR	(0x4A)
	#define MDIOCR_RADDR(x)		((x) & 0x1F)
	#define MDIOCR_FADDR(x)		(((x) & 0x1F) << 8)
	#define MDIOCR_VALID		BIT(13)
	#define MDIOCR_READ		BIT(14)
	#define MDIOCR_WRITE		BIT(15)
#define P2_LCR0		(0x4C)
	#define LCR_LED0_EN		BIT(0)
	#define LCR_LED0_100MODE	BIT(1)
	#define LCR_LED0_DUPLEX		BIT(2)
	#define LCR_LED0_LINK		BIT(3)
	#define LCR_LED0_ACT		BIT(4)
	#define LCR_LED0_COL		BIT(5)
	#define LCR_LED0_10MODE		BIT(6)
	#define LCR_LED0_DUPCOL		BIT(7)
	#define LCR_LED1_EN		BIT(8)
	#define LCR_LED1_100MODE	BIT(9)
	#define LCR_LED1_DUPLEX		BIT(10)
	#define LCR_LED1_LINK		BIT(11)
	#define LCR_LED1_ACT		BIT(12)
	#define LCR_LED1_COL		BIT(13)
	#define LCR_LED1_10MODE		BIT(14)
	#define LCR_LED1_DUPCOL		BIT(15)
#define P2_LCR1		(0x4E)
	#define LCR_LED2_MASK		(0xFF00)
	#define LCR_LED2_EN		BIT(0)
	#define LCR_LED2_100MODE	BIT(1)
	#define LCR_LED2_DUPLEX		BIT(2)
	#define LCR_LED2_LINK		BIT(3)
	#define LCR_LED2_ACT		BIT(4)
	#define LCR_LED2_COL		BIT(5)
	#define LCR_LED2_10MODE		BIT(6)
	#define LCR_LED2_DUPCOL		BIT(7)
#define P2_IPGCR	(0x50)
#define P2_CRIR		(0x52)
#define P2_FLHWCR	(0x54)
#define P2_RXCR		(0x56)
	#define RXCR_PRO		BIT(0)
	#define RXCR_AMALL		BIT(1)
	#define RXCR_SEP		BIT(2)
	#define RXCR_AB			BIT(3)
	#define RXCR_AM			BIT(4)
	#define RXCR_AP			BIT(5)
	#define RXCR_ARP		BIT(6)
#define P2_JLCR		(0x58)
#define P2_MPLR		(0x5C)

	/* Definition of PAGE3 */
#define P3_MACASR0	(0x62)
	#define P3_MACASR(x)		(P3_MACASR0 + 2 * (x))
	#define MACASR_LOWBYTE_MASK	0x00FF
	#define MACASR_HIGH_BITS	0x08
#define P3_MACASR1	(0x64)
#define P3_MACASR2	(0x66)
#define P3_MFAR01	(0x68)
#define P3_MFAR_BASE	(0x68)
	#define P3_MFAR(x)		(P3_MFAR_BASE + 2 * (x))

#define P3_MFAR23	(0x6A)
#define P3_MFAR45	(0x6C)
#define P3_MFAR67	(0x6E)
#define P3_VID0FR	(0x70)
#define P3_VID1FR	(0x72)
#define P3_EECSR	(0x74)
#define P3_EEDR		(0x76)
#define P3_EECR		(0x78)
	#define EECR_ADDR_MASK		(0x00FF)
	#define EECR_READ_ACT		BIT(8)
	#define EECR_WRITE_ACT		BIT(9)
	#define EECR_WRITE_DISABLE	BIT(10)
	#define EECR_WRITE_ENABLE	BIT(11)
	#define EECR_EE_READY		BIT(13)
	#define EECR_RELOAD		BIT(14)
	#define EECR_RESET		BIT(15)
#define P3_TPCR		(0x7A)
	#define TPCR_PATT_MASK		(0xFF)
	#define TPCR_RAND_PKT_EN	BIT(14)
	#define TPCR_FIXED_PKT_EN	BIT(15)
#define P3_TPLR		(0x7C)
	/* Definition of PAGE4 */
#define P4_SPICR	(0x8A)
	#define SPICR_RCEN		BIT(0)
	#define SPICR_QCEN		BIT(1)
	#define SPICR_RBRE		BIT(3)
	#define SPICR_PMM		BIT(4)
	#define SPICR_LOOPBACK		BIT(8)
	#define SPICR_CORE_RES_CLR	BIT(10)
	#define SPICR_SPI_RES_CLR	BIT(11)
#define P4_SPIISMR	(0x8C)

#define P4_COERCR0	(0x92)
	#define COERCR0_RXIPCE		BIT(0)
	#define COERCR0_RXIPVE		BIT(1)
	#define COERCR0_RXV6PE		BIT(2)
	#define COERCR0_RXTCPE		BIT(3)
	#define COERCR0_RXUDPE		BIT(4)
	#define COERCR0_RXICMP		BIT(5)
	#define COERCR0_RXIGMP		BIT(6)
	#define COERCR0_RXICV6		BIT(7)

	#define COERCR0_RXTCPV6		BIT(8)
	#define COERCR0_RXUDPV6		BIT(9)
	#define COERCR0_RXICMV6		BIT(10)
	#define COERCR0_RXIGMV6		BIT(11)
	#define COERCR0_RXICV6V6	BIT(12)

	#define COERCR0_DEFAULT		(COERCR0_RXIPCE | COERCR0_RXV6PE | \
					 COERCR0_RXTCPE | COERCR0_RXUDPE | \
					 COERCR0_RXTCPV6 | COERCR0_RXUDPV6)
#define P4_COERCR1	(0x94)
	#define COERCR1_IPCEDP		BIT(0)
	#define COERCR1_IPVEDP		BIT(1)
	#define COERCR1_V6VEDP		BIT(2)
	#define COERCR1_TCPEDP		BIT(3)
	#define COERCR1_UDPEDP		BIT(4)
	#define COERCR1_ICMPDP		BIT(5)
	#define COERCR1_IGMPDP		BIT(6)
	#define COERCR1_ICV6DP		BIT(7)
	#define COERCR1_RX64TE		BIT(8)
	#define COERCR1_RXPPPE		BIT(9)
	#define COERCR1_TCP6DP		BIT(10)
	#define COERCR1_UDP6DP		BIT(11)
	#define COERCR1_IC6DP		BIT(12)
	#define COERCR1_IG6DP		BIT(13)
	#define COERCR1_ICV66DP		BIT(14)
	#define COERCR1_RPCE		BIT(15)

	#define COERCR1_DEFAULT		(COERCR1_RXPPPE)

#define P4_COETCR0	(0x96)
	#define COETCR0_TXIP		BIT(0)
	#define COETCR0_TXTCP		BIT(1)
	#define COETCR0_TXUDP		BIT(2)
	#define COETCR0_TXICMP		BIT(3)
	#define COETCR0_TXIGMP		BIT(4)
	#define COETCR0_TXICV6		BIT(5)
	#define COETCR0_TXTCPV6		BIT(8)
	#define COETCR0_TXUDPV6		BIT(9)
	#define COETCR0_TXICMV6		BIT(10)
	#define COETCR0_TXIGMV6		BIT(11)
	#define COETCR0_TXICV6V6	BIT(12)

	#define COETCR0_DEFAULT		(COETCR0_TXIP | COETCR0_TXTCP | \
					 COETCR0_TXUDP | COETCR0_TXTCPV6 | \
					 COETCR0_TXUDPV6)
#define P4_COETCR1	(0x98)
	#define COETCR1_TX64TE		BIT(0)
	#define COETCR1_TXPPPE		BIT(1)

#define P4_COECEDR	(0x9A)
#define P4_L2CECR	(0x9C)

	/* Definition of PAGE5 */
#define P5_WFTR		(0xA2)
	#define WFTR_2MS		(0x01)
	#define WFTR_4MS		(0x02)
	#define WFTR_8MS		(0x03)
	#define WFTR_16MS		(0x04)
	#define WFTR_32MS		(0x05)
	#define WFTR_64MS		(0x06)
	#define WFTR_128MS		(0x07)
	#define WFTR_256MS		(0x08)
	#define WFTR_512MS		(0x09)
	#define WFTR_1024MS		(0x0A)
	#define WFTR_2048MS		(0x0B)
	#define WFTR_4096MS		(0x0C)
	#define WFTR_8192MS		(0x0D)
	#define WFTR_16384MS		(0x0E)
	#define WFTR_32768MS		(0x0F)
#define P5_WFCCR	(0xA4)
#define P5_WFCR03	(0xA6)
	#define WFCR03_F0_EN		BIT(0)
	#define WFCR03_F1_EN		BIT(4)
	#define WFCR03_F2_EN		BIT(8)
	#define WFCR03_F3_EN		BIT(12)
#define P5_WFCR47	(0xA8)
	#define WFCR47_F4_EN		BIT(0)
	#define WFCR47_F5_EN		BIT(4)
	#define WFCR47_F6_EN		BIT(8)
	#define WFCR47_F7_EN		BIT(12)
#define P5_WF0BMR0	(0xAA)
#define P5_WF0BMR1	(0xAC)
#define P5_WF0CR	(0xAE)
#define P5_WF0OBR	(0xB0)
#define P5_WF1BMR0	(0xB2)
#define P5_WF1BMR1	(0xB4)
#define P5_WF1CR	(0xB6)
#define P5_WF1OBR	(0xB8)
#define P5_WF2BMR0	(0xBA)
#define P5_WF2BMR1	(0xBC)

	/* Definition of PAGE6 */
#define P6_WF2CR	(0xC2)
#define P6_WF2OBR	(0xC4)
#define P6_WF3BMR0	(0xC6)
#define P6_WF3BMR1	(0xC8)
#define P6_WF3CR	(0xCA)
#define P6_WF3OBR	(0xCC)
#define P6_WF4BMR0	(0xCE)
#define P6_WF4BMR1	(0xD0)
#define P6_WF4CR	(0xD2)
#define P6_WF4OBR	(0xD4)
#define P6_WF5BMR0	(0xD6)
#define P6_WF5BMR1	(0xD8)
#define P6_WF5CR	(0xDA)
#define P6_WF5OBR	(0xDC)

/* Definition of PAGE7 */
#define P7_WF6BMR0	(0xE2)
#define P7_WF6BMR1	(0xE4)
#define P7_WF6CR	(0xE6)
#define P7_WF6OBR	(0xE8)
#define P7_WF7BMR0	(0xEA)
#define P7_WF7BMR1	(0xEC)
#define P7_WF7CR	(0xEE)
#define P7_WF7OBR	(0xF0)
#define P7_WFR01	(0xF2)
#define P7_WFR23	(0xF4)
#define P7_WFR45	(0xF6)
#define P7_WFR67	(0xF8)
#define P7_WFPC0	(0xFA)
#define P7_WFPC1	(0xFC)

/* Tx headers structure */
struct tx_sop_header {
	/* bit 15-11: flags, bit 10-0: packet length */
	u16 flags_len;
	/* bit 15-11: sequence number, bit 11-0: packet length bar */
	u16 seq_lenbar;
};

struct tx_segment_header {
	/* bit 15-14: flags, bit 13-11: segment number */
	/* bit 10-0: segment length */
	u16 flags_seqnum_seglen;
	/* bit 15-14: end offset, bit 13-11: start offset */
	/* bit 10-0: segment length bar */
	u16 eo_so_seglenbar;
};

struct tx_eop_header {
	/* bit 15-11: sequence number, bit 10-0: packet length */
	u16 seq_len;
	/* bit 15-11: sequence number bar, bit 10-0: packet length bar */
	u16 seqbar_lenbar;
};

struct tx_pkt_info {
	struct tx_sop_header sop;
	struct tx_segment_header seg;
	struct tx_eop_header eop;
	u16 pkt_len;
	u16 seq_num;
};

/* Rx headers structure */
struct rx_header {
	u16 flags_len;
	u16 seq_lenbar;
	u16 flags;
};

extern unsigned long ax88796c_no_regs_mask[];

#endif /* #ifndef _AX88796C_MAIN_H */
