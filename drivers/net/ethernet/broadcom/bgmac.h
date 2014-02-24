#ifndef _BGMAC_H
#define _BGMAC_H

#define pr_fmt(fmt)		KBUILD_MODNAME ": " fmt

#define bgmac_err(bgmac, fmt, ...) \
	dev_err(&(bgmac)->core->dev, fmt, ##__VA_ARGS__)
#define bgmac_warn(bgmac, fmt, ...) \
	dev_warn(&(bgmac)->core->dev, fmt,  ##__VA_ARGS__)
#define bgmac_info(bgmac, fmt, ...) \
	dev_info(&(bgmac)->core->dev, fmt,  ##__VA_ARGS__)
#define bgmac_dbg(bgmac, fmt, ...) \
	dev_dbg(&(bgmac)->core->dev, fmt, ##__VA_ARGS__)

#include <linux/bcma/bcma.h>
#include <linux/netdevice.h>

#define BGMAC_DEV_CTL				0x000
#define  BGMAC_DC_TSM				0x00000002
#define  BGMAC_DC_CFCO				0x00000004
#define  BGMAC_DC_RLSS				0x00000008
#define  BGMAC_DC_MROR				0x00000010
#define  BGMAC_DC_FCM_MASK			0x00000060
#define  BGMAC_DC_FCM_SHIFT			5
#define  BGMAC_DC_NAE				0x00000080
#define  BGMAC_DC_TF				0x00000100
#define  BGMAC_DC_RDS_MASK			0x00030000
#define  BGMAC_DC_RDS_SHIFT			16
#define  BGMAC_DC_TDS_MASK			0x000c0000
#define  BGMAC_DC_TDS_SHIFT			18
#define BGMAC_DEV_STATUS			0x004		/* Configuration of the interface */
#define  BGMAC_DS_RBF				0x00000001
#define  BGMAC_DS_RDF				0x00000002
#define  BGMAC_DS_RIF				0x00000004
#define  BGMAC_DS_TBF				0x00000008
#define  BGMAC_DS_TDF				0x00000010
#define  BGMAC_DS_TIF				0x00000020
#define  BGMAC_DS_PO				0x00000040
#define  BGMAC_DS_MM_MASK			0x00000300	/* Mode of the interface */
#define  BGMAC_DS_MM_SHIFT			8
#define BGMAC_BIST_STATUS			0x00c
#define BGMAC_INT_STATUS			0x020		/* Interrupt status */
#define  BGMAC_IS_MRO				0x00000001
#define  BGMAC_IS_MTO				0x00000002
#define  BGMAC_IS_TFD				0x00000004
#define  BGMAC_IS_LS				0x00000008
#define  BGMAC_IS_MDIO				0x00000010
#define  BGMAC_IS_MR				0x00000020
#define  BGMAC_IS_MT				0x00000040
#define  BGMAC_IS_TO				0x00000080
#define  BGMAC_IS_DESC_ERR			0x00000400	/* Descriptor error */
#define  BGMAC_IS_DATA_ERR			0x00000800	/* Data error */
#define  BGMAC_IS_DESC_PROT_ERR			0x00001000	/* Descriptor protocol error */
#define  BGMAC_IS_RX_DESC_UNDERF		0x00002000	/* Receive descriptor underflow */
#define  BGMAC_IS_RX_F_OVERF			0x00004000	/* Receive FIFO overflow */
#define  BGMAC_IS_TX_F_UNDERF			0x00008000	/* Transmit FIFO underflow */
#define  BGMAC_IS_RX				0x00010000	/* Interrupt for RX queue 0 */
#define  BGMAC_IS_TX0				0x01000000	/* Interrupt for TX queue 0 */
#define  BGMAC_IS_TX1				0x02000000	/* Interrupt for TX queue 1 */
#define  BGMAC_IS_TX2				0x04000000	/* Interrupt for TX queue 2 */
#define  BGMAC_IS_TX3				0x08000000	/* Interrupt for TX queue 3 */
#define  BGMAC_IS_TX_MASK			0x0f000000
#define  BGMAC_IS_INTMASK			0x0f01fcff
#define  BGMAC_IS_ERRMASK			0x0000fc00
#define BGMAC_INT_MASK				0x024		/* Interrupt mask */
#define BGMAC_GP_TIMER				0x028
#define BGMAC_INT_RECV_LAZY			0x100
#define  BGMAC_IRL_TO_MASK			0x00ffffff
#define  BGMAC_IRL_FC_MASK			0xff000000
#define  BGMAC_IRL_FC_SHIFT			24		/* Shift the number of interrupts triggered per received frame */
#define BGMAC_FLOW_CTL_THRESH			0x104		/* Flow control thresholds */
#define BGMAC_WRRTHRESH				0x108
#define BGMAC_GMAC_IDLE_CNT_THRESH		0x10c
#define BGMAC_PHY_ACCESS			0x180		/* PHY access address */
#define  BGMAC_PA_DATA_MASK			0x0000ffff
#define  BGMAC_PA_ADDR_MASK			0x001f0000
#define  BGMAC_PA_ADDR_SHIFT			16
#define  BGMAC_PA_REG_MASK			0x1f000000
#define  BGMAC_PA_REG_SHIFT			24
#define  BGMAC_PA_WRITE				0x20000000
#define  BGMAC_PA_START				0x40000000
#define BGMAC_PHY_CNTL				0x188		/* PHY control address */
#define  BGMAC_PC_EPA_MASK			0x0000001f
#define  BGMAC_PC_MCT_MASK			0x007f0000
#define  BGMAC_PC_MCT_SHIFT			16
#define  BGMAC_PC_MTE				0x00800000
#define BGMAC_TXQ_CTL				0x18c
#define  BGMAC_TXQ_CTL_DBT_MASK			0x00000fff
#define  BGMAC_TXQ_CTL_DBT_SHIFT		0
#define BGMAC_RXQ_CTL				0x190
#define  BGMAC_RXQ_CTL_DBT_MASK			0x00000fff
#define  BGMAC_RXQ_CTL_DBT_SHIFT		0
#define  BGMAC_RXQ_CTL_PTE			0x00001000
#define  BGMAC_RXQ_CTL_MDP_MASK			0x3f000000
#define  BGMAC_RXQ_CTL_MDP_SHIFT		24
#define BGMAC_GPIO_SELECT			0x194
#define BGMAC_GPIO_OUTPUT_EN			0x198

/* For 0x1e0 see BCMA_CLKCTLST. Below are BGMAC specific bits */
#define  BGMAC_BCMA_CLKCTLST_MISC_PLL_REQ	0x00000100
#define  BGMAC_BCMA_CLKCTLST_MISC_PLL_ST	0x01000000

#define BGMAC_HW_WAR				0x1e4
#define BGMAC_PWR_CTL				0x1e8
#define BGMAC_DMA_BASE0				0x200		/* Tx and Rx controller */
#define BGMAC_DMA_BASE1				0x240		/* Tx controller only */
#define BGMAC_DMA_BASE2				0x280		/* Tx controller only */
#define BGMAC_DMA_BASE3				0x2C0		/* Tx controller only */
#define BGMAC_TX_GOOD_OCTETS			0x300
#define BGMAC_TX_GOOD_OCTETS_HIGH		0x304
#define BGMAC_TX_GOOD_PKTS			0x308
#define BGMAC_TX_OCTETS				0x30c
#define BGMAC_TX_OCTETS_HIGH			0x310
#define BGMAC_TX_PKTS				0x314
#define BGMAC_TX_BROADCAST_PKTS			0x318
#define BGMAC_TX_MULTICAST_PKTS			0x31c
#define BGMAC_TX_LEN_64				0x320
#define BGMAC_TX_LEN_65_TO_127			0x324
#define BGMAC_TX_LEN_128_TO_255			0x328
#define BGMAC_TX_LEN_256_TO_511			0x32c
#define BGMAC_TX_LEN_512_TO_1023		0x330
#define BGMAC_TX_LEN_1024_TO_1522		0x334
#define BGMAC_TX_LEN_1523_TO_2047		0x338
#define BGMAC_TX_LEN_2048_TO_4095		0x33c
#define BGMAC_TX_LEN_4095_TO_8191		0x340
#define BGMAC_TX_LEN_8192_TO_MAX		0x344
#define BGMAC_TX_JABBER_PKTS			0x348		/* Error */
#define BGMAC_TX_OVERSIZE_PKTS			0x34c		/* Error */
#define BGMAC_TX_FRAGMENT_PKTS			0x350
#define BGMAC_TX_UNDERRUNS			0x354		/* Error */
#define BGMAC_TX_TOTAL_COLS			0x358
#define BGMAC_TX_SINGLE_COLS			0x35c
#define BGMAC_TX_MULTIPLE_COLS			0x360
#define BGMAC_TX_EXCESSIVE_COLS			0x364		/* Error */
#define BGMAC_TX_LATE_COLS			0x368		/* Error */
#define BGMAC_TX_DEFERED			0x36c
#define BGMAC_TX_CARRIER_LOST			0x370
#define BGMAC_TX_PAUSE_PKTS			0x374
#define BGMAC_TX_UNI_PKTS			0x378
#define BGMAC_TX_Q0_PKTS			0x37c
#define BGMAC_TX_Q0_OCTETS			0x380
#define BGMAC_TX_Q0_OCTETS_HIGH			0x384
#define BGMAC_TX_Q1_PKTS			0x388
#define BGMAC_TX_Q1_OCTETS			0x38c
#define BGMAC_TX_Q1_OCTETS_HIGH			0x390
#define BGMAC_TX_Q2_PKTS			0x394
#define BGMAC_TX_Q2_OCTETS			0x398
#define BGMAC_TX_Q2_OCTETS_HIGH			0x39c
#define BGMAC_TX_Q3_PKTS			0x3a0
#define BGMAC_TX_Q3_OCTETS			0x3a4
#define BGMAC_TX_Q3_OCTETS_HIGH			0x3a8
#define BGMAC_RX_GOOD_OCTETS			0x3b0
#define BGMAC_RX_GOOD_OCTETS_HIGH		0x3b4
#define BGMAC_RX_GOOD_PKTS			0x3b8
#define BGMAC_RX_OCTETS				0x3bc
#define BGMAC_RX_OCTETS_HIGH			0x3c0
#define BGMAC_RX_PKTS				0x3c4
#define BGMAC_RX_BROADCAST_PKTS			0x3c8
#define BGMAC_RX_MULTICAST_PKTS			0x3cc
#define BGMAC_RX_LEN_64				0x3d0
#define BGMAC_RX_LEN_65_TO_127			0x3d4
#define BGMAC_RX_LEN_128_TO_255			0x3d8
#define BGMAC_RX_LEN_256_TO_511			0x3dc
#define BGMAC_RX_LEN_512_TO_1023		0x3e0
#define BGMAC_RX_LEN_1024_TO_1522		0x3e4
#define BGMAC_RX_LEN_1523_TO_2047		0x3e8
#define BGMAC_RX_LEN_2048_TO_4095		0x3ec
#define BGMAC_RX_LEN_4095_TO_8191		0x3f0
#define BGMAC_RX_LEN_8192_TO_MAX		0x3f4
#define BGMAC_RX_JABBER_PKTS			0x3f8		/* Error */
#define BGMAC_RX_OVERSIZE_PKTS			0x3fc		/* Error */
#define BGMAC_RX_FRAGMENT_PKTS			0x400
#define BGMAC_RX_MISSED_PKTS			0x404		/* Error */
#define BGMAC_RX_CRC_ALIGN_ERRS			0x408		/* Error */
#define BGMAC_RX_UNDERSIZE			0x40c		/* Error */
#define BGMAC_RX_CRC_ERRS			0x410		/* Error */
#define BGMAC_RX_ALIGN_ERRS			0x414		/* Error */
#define BGMAC_RX_SYMBOL_ERRS			0x418		/* Error */
#define BGMAC_RX_PAUSE_PKTS			0x41c
#define BGMAC_RX_NONPAUSE_PKTS			0x420
#define BGMAC_RX_SACHANGES			0x424
#define BGMAC_RX_UNI_PKTS			0x428
#define BGMAC_UNIMAC_VERSION			0x800
#define BGMAC_HDBKP_CTL				0x804
#define BGMAC_CMDCFG				0x808		/* Configuration */
#define  BGMAC_CMDCFG_TE			0x00000001	/* Set to activate TX */
#define  BGMAC_CMDCFG_RE			0x00000002	/* Set to activate RX */
#define  BGMAC_CMDCFG_ES_MASK			0x0000000c	/* Ethernet speed see gmac_speed */
#define   BGMAC_CMDCFG_ES_10			0x00000000
#define   BGMAC_CMDCFG_ES_100			0x00000004
#define   BGMAC_CMDCFG_ES_1000			0x00000008
#define   BGMAC_CMDCFG_ES_2500			0x0000000C
#define  BGMAC_CMDCFG_PROM			0x00000010	/* Set to activate promiscuous mode */
#define  BGMAC_CMDCFG_PAD_EN			0x00000020
#define  BGMAC_CMDCFG_CF			0x00000040
#define  BGMAC_CMDCFG_PF			0x00000080
#define  BGMAC_CMDCFG_RPI			0x00000100	/* Unset to enable 802.3x tx flow control */
#define  BGMAC_CMDCFG_TAI			0x00000200
#define  BGMAC_CMDCFG_HD			0x00000400	/* Set if in half duplex mode */
#define  BGMAC_CMDCFG_HD_SHIFT			10
#define  BGMAC_CMDCFG_SR_REV0			0x00000800	/* Set to reset mode, for other revs */
#define  BGMAC_CMDCFG_SR_REV4			0x00002000	/* Set to reset mode, only for core rev 4 */
#define  BGMAC_CMDCFG_SR(rev)  ((rev == 4) ? BGMAC_CMDCFG_SR_REV4 : BGMAC_CMDCFG_SR_REV0)
#define  BGMAC_CMDCFG_ML			0x00008000	/* Set to activate mac loopback mode */
#define  BGMAC_CMDCFG_AE			0x00400000
#define  BGMAC_CMDCFG_CFE			0x00800000
#define  BGMAC_CMDCFG_NLC			0x01000000
#define  BGMAC_CMDCFG_RL			0x02000000
#define  BGMAC_CMDCFG_RED			0x04000000
#define  BGMAC_CMDCFG_PE			0x08000000
#define  BGMAC_CMDCFG_TPI			0x10000000
#define  BGMAC_CMDCFG_AT			0x20000000
#define BGMAC_MACADDR_HIGH			0x80c		/* High 4 octets of own mac address */
#define BGMAC_MACADDR_LOW			0x810		/* Low 2 octets of own mac address */
#define BGMAC_RXMAX_LENGTH			0x814		/* Max receive frame length with vlan tag */
#define BGMAC_PAUSEQUANTA			0x818
#define BGMAC_MAC_MODE				0x844
#define BGMAC_OUTERTAG				0x848
#define BGMAC_INNERTAG				0x84c
#define BGMAC_TXIPG				0x85c
#define BGMAC_PAUSE_CTL				0xb30
#define BGMAC_TX_FLUSH				0xb34
#define BGMAC_RX_STATUS				0xb38
#define BGMAC_TX_STATUS				0xb3c

/* BCMA GMAC core specific IO Control (BCMA_IOCTL) flags */
#define BGMAC_BCMA_IOCTL_SW_CLKEN		0x00000004	/* PHY Clock Enable */
#define BGMAC_BCMA_IOCTL_SW_RESET		0x00000008	/* PHY Reset */

/* BCMA GMAC core specific IO status (BCMA_IOST) flags */
#define BGMAC_BCMA_IOST_ATTACHED		0x00000800

#define BGMAC_NUM_MIB_TX_REGS	\
		(((BGMAC_TX_Q3_OCTETS_HIGH - BGMAC_TX_GOOD_OCTETS) / 4) + 1)
#define BGMAC_NUM_MIB_RX_REGS	\
		(((BGMAC_RX_UNI_PKTS - BGMAC_RX_GOOD_OCTETS) / 4) + 1)

#define BGMAC_DMA_TX_CTL			0x00
#define  BGMAC_DMA_TX_ENABLE			0x00000001
#define  BGMAC_DMA_TX_SUSPEND			0x00000002
#define  BGMAC_DMA_TX_LOOPBACK			0x00000004
#define  BGMAC_DMA_TX_FLUSH			0x00000010
#define  BGMAC_DMA_TX_MR_MASK			0x000000C0	/* Multiple outstanding reads */
#define  BGMAC_DMA_TX_MR_SHIFT			6
#define   BGMAC_DMA_TX_MR_1			0
#define   BGMAC_DMA_TX_MR_2			1
#define  BGMAC_DMA_TX_PARITY_DISABLE		0x00000800
#define  BGMAC_DMA_TX_ADDREXT_MASK		0x00030000
#define  BGMAC_DMA_TX_ADDREXT_SHIFT		16
#define  BGMAC_DMA_TX_BL_MASK			0x001C0000	/* BurstLen bits */
#define  BGMAC_DMA_TX_BL_SHIFT			18
#define   BGMAC_DMA_TX_BL_16			0
#define   BGMAC_DMA_TX_BL_32			1
#define   BGMAC_DMA_TX_BL_64			2
#define   BGMAC_DMA_TX_BL_128			3
#define   BGMAC_DMA_TX_BL_256			4
#define   BGMAC_DMA_TX_BL_512			5
#define   BGMAC_DMA_TX_BL_1024			6
#define  BGMAC_DMA_TX_PC_MASK			0x00E00000	/* Prefetch control */
#define  BGMAC_DMA_TX_PC_SHIFT			21
#define   BGMAC_DMA_TX_PC_0			0
#define   BGMAC_DMA_TX_PC_4			1
#define   BGMAC_DMA_TX_PC_8			2
#define   BGMAC_DMA_TX_PC_16			3
#define  BGMAC_DMA_TX_PT_MASK			0x03000000	/* Prefetch threshold */
#define  BGMAC_DMA_TX_PT_SHIFT			24
#define   BGMAC_DMA_TX_PT_1			0
#define   BGMAC_DMA_TX_PT_2			1
#define   BGMAC_DMA_TX_PT_4			2
#define   BGMAC_DMA_TX_PT_8			3
#define BGMAC_DMA_TX_INDEX			0x04
#define BGMAC_DMA_TX_RINGLO			0x08
#define BGMAC_DMA_TX_RINGHI			0x0C
#define BGMAC_DMA_TX_STATUS			0x10
#define  BGMAC_DMA_TX_STATDPTR			0x00001FFF
#define  BGMAC_DMA_TX_STAT			0xF0000000
#define   BGMAC_DMA_TX_STAT_DISABLED		0x00000000
#define   BGMAC_DMA_TX_STAT_ACTIVE		0x10000000
#define   BGMAC_DMA_TX_STAT_IDLEWAIT		0x20000000
#define   BGMAC_DMA_TX_STAT_STOPPED		0x30000000
#define   BGMAC_DMA_TX_STAT_SUSP		0x40000000
#define BGMAC_DMA_TX_ERROR			0x14
#define  BGMAC_DMA_TX_ERRDPTR			0x0001FFFF
#define  BGMAC_DMA_TX_ERR			0xF0000000
#define   BGMAC_DMA_TX_ERR_NOERR		0x00000000
#define   BGMAC_DMA_TX_ERR_PROT			0x10000000
#define   BGMAC_DMA_TX_ERR_UNDERRUN		0x20000000
#define   BGMAC_DMA_TX_ERR_TRANSFER		0x30000000
#define   BGMAC_DMA_TX_ERR_DESCREAD		0x40000000
#define   BGMAC_DMA_TX_ERR_CORE			0x50000000
#define BGMAC_DMA_RX_CTL			0x20
#define  BGMAC_DMA_RX_ENABLE			0x00000001
#define  BGMAC_DMA_RX_FRAME_OFFSET_MASK		0x000000FE
#define  BGMAC_DMA_RX_FRAME_OFFSET_SHIFT	1
#define  BGMAC_DMA_RX_DIRECT_FIFO		0x00000100
#define  BGMAC_DMA_RX_OVERFLOW_CONT		0x00000400
#define  BGMAC_DMA_RX_PARITY_DISABLE		0x00000800
#define  BGMAC_DMA_RX_MR_MASK			0x000000C0	/* Multiple outstanding reads */
#define  BGMAC_DMA_RX_MR_SHIFT			6
#define   BGMAC_DMA_TX_MR_1			0
#define   BGMAC_DMA_TX_MR_2			1
#define  BGMAC_DMA_RX_ADDREXT_MASK		0x00030000
#define  BGMAC_DMA_RX_ADDREXT_SHIFT		16
#define  BGMAC_DMA_RX_BL_MASK			0x001C0000	/* BurstLen bits */
#define  BGMAC_DMA_RX_BL_SHIFT			18
#define   BGMAC_DMA_RX_BL_16			0
#define   BGMAC_DMA_RX_BL_32			1
#define   BGMAC_DMA_RX_BL_64			2
#define   BGMAC_DMA_RX_BL_128			3
#define   BGMAC_DMA_RX_BL_256			4
#define   BGMAC_DMA_RX_BL_512			5
#define   BGMAC_DMA_RX_BL_1024			6
#define  BGMAC_DMA_RX_PC_MASK			0x00E00000	/* Prefetch control */
#define  BGMAC_DMA_RX_PC_SHIFT			21
#define   BGMAC_DMA_RX_PC_0			0
#define   BGMAC_DMA_RX_PC_4			1
#define   BGMAC_DMA_RX_PC_8			2
#define   BGMAC_DMA_RX_PC_16			3
#define  BGMAC_DMA_RX_PT_MASK			0x03000000	/* Prefetch threshold */
#define  BGMAC_DMA_RX_PT_SHIFT			24
#define   BGMAC_DMA_RX_PT_1			0
#define   BGMAC_DMA_RX_PT_2			1
#define   BGMAC_DMA_RX_PT_4			2
#define   BGMAC_DMA_RX_PT_8			3
#define BGMAC_DMA_RX_INDEX			0x24
#define BGMAC_DMA_RX_RINGLO			0x28
#define BGMAC_DMA_RX_RINGHI			0x2C
#define BGMAC_DMA_RX_STATUS			0x30
#define  BGMAC_DMA_RX_STATDPTR			0x00001FFF
#define  BGMAC_DMA_RX_STAT			0xF0000000
#define   BGMAC_DMA_RX_STAT_DISABLED		0x00000000
#define   BGMAC_DMA_RX_STAT_ACTIVE		0x10000000
#define   BGMAC_DMA_RX_STAT_IDLEWAIT		0x20000000
#define   BGMAC_DMA_RX_STAT_STOPPED		0x30000000
#define   BGMAC_DMA_RX_STAT_SUSP		0x40000000
#define BGMAC_DMA_RX_ERROR			0x34
#define  BGMAC_DMA_RX_ERRDPTR			0x0001FFFF
#define  BGMAC_DMA_RX_ERR			0xF0000000
#define   BGMAC_DMA_RX_ERR_NOERR		0x00000000
#define   BGMAC_DMA_RX_ERR_PROT			0x10000000
#define   BGMAC_DMA_RX_ERR_UNDERRUN		0x20000000
#define   BGMAC_DMA_RX_ERR_TRANSFER		0x30000000
#define   BGMAC_DMA_RX_ERR_DESCREAD		0x40000000
#define   BGMAC_DMA_RX_ERR_CORE			0x50000000

#define BGMAC_DESC_CTL0_EOT			0x10000000	/* End of ring */
#define BGMAC_DESC_CTL0_IOC			0x20000000	/* IRQ on complete */
#define BGMAC_DESC_CTL0_SOF			0x40000000	/* Start of frame */
#define BGMAC_DESC_CTL0_EOF			0x80000000	/* End of frame */
#define BGMAC_DESC_CTL1_LEN			0x00001FFF

#define BGMAC_PHY_NOREGS			0x1E
#define BGMAC_PHY_MASK				0x1F

#define BGMAC_MAX_TX_RINGS			4
#define BGMAC_MAX_RX_RINGS			1

#define BGMAC_TX_RING_SLOTS			128
#define BGMAC_RX_RING_SLOTS			512 - 1		/* Why -1? Well, Broadcom does that... */

#define BGMAC_RX_HEADER_LEN			28		/* Last 24 bytes are unused. Well... */
#define BGMAC_RX_FRAME_OFFSET			30		/* There are 2 unused bytes between header and real data */
#define BGMAC_RX_MAX_FRAME_SIZE			1536		/* Copied from b44/tg3 */
#define BGMAC_RX_BUF_SIZE			(BGMAC_RX_FRAME_OFFSET + BGMAC_RX_MAX_FRAME_SIZE)

#define BGMAC_BFL_ENETROBO			0x0010		/* has ephy roboswitch spi */
#define BGMAC_BFL_ENETADM			0x0080		/* has ADMtek switch */
#define BGMAC_BFL_ENETVLAN			0x0100		/* can do vlan */

#define BGMAC_CHIPCTL_1_IF_TYPE_MASK		0x00000030
#define BGMAC_CHIPCTL_1_IF_TYPE_RMII		0x00000000
#define BGMAC_CHIPCTL_1_IF_TYPE_MII		0x00000010
#define BGMAC_CHIPCTL_1_IF_TYPE_RGMII		0x00000020
#define BGMAC_CHIPCTL_1_SW_TYPE_MASK		0x000000C0
#define BGMAC_CHIPCTL_1_SW_TYPE_EPHY		0x00000000
#define BGMAC_CHIPCTL_1_SW_TYPE_EPHYMII		0x00000040
#define BGMAC_CHIPCTL_1_SW_TYPE_EPHYRMII	0x00000080
#define BGMAC_CHIPCTL_1_SW_TYPE_RGMII		0x000000C0
#define BGMAC_CHIPCTL_1_RXC_DLL_BYPASS		0x00010000

#define BGMAC_WEIGHT	64

#define ETHER_MAX_LEN   1518

struct bgmac_slot_info {
	struct sk_buff *skb;
	dma_addr_t dma_addr;
};

struct bgmac_dma_desc {
	__le32 ctl0;
	__le32 ctl1;
	__le32 addr_low;
	__le32 addr_high;
} __packed;

enum bgmac_dma_ring_type {
	BGMAC_DMA_RING_TX,
	BGMAC_DMA_RING_RX,
};

/**
 * bgmac_dma_ring - contains info about DMA ring (either TX or RX one)
 * @start: index of the first slot containing data
 * @end: index of a slot that can *not* be read (yet)
 *
 * Be really aware of the specific @end meaning. It's an index of a slot *after*
 * the one containing data that can be read. If @start equals @end the ring is
 * empty.
 */
struct bgmac_dma_ring {
	u16 num_slots;
	u16 start;
	u16 end;

	u16 mmio_base;
	struct bgmac_dma_desc *cpu_base;
	dma_addr_t dma_base;
	u32 index_base; /* Used for unaligned rings only, otherwise 0 */
	bool unaligned;

	struct bgmac_slot_info slots[BGMAC_RX_RING_SLOTS];
};

struct bgmac_rx_header {
	__le16 len;
	__le16 flags;
	__le16 pad[12];
};

struct bgmac {
	struct bcma_device *core;
	struct bcma_device *cmn; /* Reference to CMN core for BCM4706 */
	struct net_device *net_dev;
	struct napi_struct napi;
	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;

	/* DMA */
	struct bgmac_dma_ring tx_ring[BGMAC_MAX_TX_RINGS];
	struct bgmac_dma_ring rx_ring[BGMAC_MAX_RX_RINGS];

	/* Stats */
	bool stats_grabbed;
	u32 mib_tx_regs[BGMAC_NUM_MIB_TX_REGS];
	u32 mib_rx_regs[BGMAC_NUM_MIB_RX_REGS];

	/* Int */
	u32 int_mask;
	u32 int_status;

	/* Current MAC state */
	int mac_speed;
	int mac_duplex;

	u8 phyaddr;
	bool has_robosw;

	bool loopback;
};

static inline u32 bgmac_read(struct bgmac *bgmac, u16 offset)
{
	return bcma_read32(bgmac->core, offset);
}

static inline void bgmac_write(struct bgmac *bgmac, u16 offset, u32 value)
{
	bcma_write32(bgmac->core, offset, value);
}

static inline void bgmac_maskset(struct bgmac *bgmac, u16 offset, u32 mask,
				   u32 set)
{
	bgmac_write(bgmac, offset, (bgmac_read(bgmac, offset) & mask) | set);
}

static inline void bgmac_mask(struct bgmac *bgmac, u16 offset, u32 mask)
{
	bgmac_maskset(bgmac, offset, mask, 0);
}

static inline void bgmac_set(struct bgmac *bgmac, u16 offset, u32 set)
{
	bgmac_maskset(bgmac, offset, ~0, set);
}

#endif /* _BGMAC_H */
