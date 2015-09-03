/*
 * Atmel MACB Ethernet Controller driver
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _MACB_H
#define _MACB_H

#define MACB_GREGS_NBR 16
#define MACB_GREGS_VERSION 2
#define MACB_MAX_QUEUES 8

/* MACB register offsets */
#define MACB_NCR		0x0000 /* Network Control */
#define MACB_NCFGR		0x0004 /* Network Config */
#define MACB_NSR		0x0008 /* Network Status */
#define MACB_TAR		0x000c /* AT91RM9200 only */
#define MACB_TCR		0x0010 /* AT91RM9200 only */
#define MACB_TSR		0x0014 /* Transmit Status */
#define MACB_RBQP		0x0018 /* RX Q Base Address */
#define MACB_TBQP		0x001c /* TX Q Base Address */
#define MACB_RSR		0x0020 /* Receive Status */
#define MACB_ISR		0x0024 /* Interrupt Status */
#define MACB_IER		0x0028 /* Interrupt Enable */
#define MACB_IDR		0x002c /* Interrupt Disable */
#define MACB_IMR		0x0030 /* Interrupt Mask */
#define MACB_MAN		0x0034 /* PHY Maintenance */
#define MACB_PTR		0x0038
#define MACB_PFR		0x003c
#define MACB_FTO		0x0040
#define MACB_SCF		0x0044
#define MACB_MCF		0x0048
#define MACB_FRO		0x004c
#define MACB_FCSE		0x0050
#define MACB_ALE		0x0054
#define MACB_DTF		0x0058
#define MACB_LCOL		0x005c
#define MACB_EXCOL		0x0060
#define MACB_TUND		0x0064
#define MACB_CSE		0x0068
#define MACB_RRE		0x006c
#define MACB_ROVR		0x0070
#define MACB_RSE		0x0074
#define MACB_ELE		0x0078
#define MACB_RJA		0x007c
#define MACB_USF		0x0080
#define MACB_STE		0x0084
#define MACB_RLE		0x0088
#define MACB_TPF		0x008c
#define MACB_HRB		0x0090
#define MACB_HRT		0x0094
#define MACB_SA1B		0x0098
#define MACB_SA1T		0x009c
#define MACB_SA2B		0x00a0
#define MACB_SA2T		0x00a4
#define MACB_SA3B		0x00a8
#define MACB_SA3T		0x00ac
#define MACB_SA4B		0x00b0
#define MACB_SA4T		0x00b4
#define MACB_TID		0x00b8
#define MACB_TPQ		0x00bc
#define MACB_USRIO		0x00c0
#define MACB_WOL		0x00c4
#define MACB_MID		0x00fc

/* GEM register offsets. */
#define GEM_NCFGR		0x0004 /* Network Config */
#define GEM_USRIO		0x000c /* User IO */
#define GEM_DMACFG		0x0010 /* DMA Configuration */
#define GEM_JML			0x0048 /* Jumbo Max Length */
#define GEM_HRB			0x0080 /* Hash Bottom */
#define GEM_HRT			0x0084 /* Hash Top */
#define GEM_SA1B		0x0088 /* Specific1 Bottom */
#define GEM_SA1T		0x008C /* Specific1 Top */
#define GEM_SA2B		0x0090 /* Specific2 Bottom */
#define GEM_SA2T		0x0094 /* Specific2 Top */
#define GEM_SA3B		0x0098 /* Specific3 Bottom */
#define GEM_SA3T		0x009C /* Specific3 Top */
#define GEM_SA4B		0x00A0 /* Specific4 Bottom */
#define GEM_SA4T		0x00A4 /* Specific4 Top */
#define GEM_OTX			0x0100 /* Octets transmitted */
#define GEM_OCTTXL		0x0100 /* Octets transmitted [31:0] */
#define GEM_OCTTXH		0x0104 /* Octets transmitted [47:32] */
#define GEM_TXCNT		0x0108 /* Frames Transmitted counter */
#define GEM_TXBCCNT		0x010c /* Broadcast Frames counter */
#define GEM_TXMCCNT		0x0110 /* Multicast Frames counter */
#define GEM_TXPAUSECNT		0x0114 /* Pause Frames Transmitted Counter */
#define GEM_TX64CNT		0x0118 /* 64 byte Frames TX counter */
#define GEM_TX65CNT		0x011c /* 65-127 byte Frames TX counter */
#define GEM_TX128CNT		0x0120 /* 128-255 byte Frames TX counter */
#define GEM_TX256CNT		0x0124 /* 256-511 byte Frames TX counter */
#define GEM_TX512CNT		0x0128 /* 512-1023 byte Frames TX counter */
#define GEM_TX1024CNT		0x012c /* 1024-1518 byte Frames TX counter */
#define GEM_TX1519CNT		0x0130 /* 1519+ byte Frames TX counter */
#define GEM_TXURUNCNT		0x0134 /* TX under run error counter */
#define GEM_SNGLCOLLCNT		0x0138 /* Single Collision Frame Counter */
#define GEM_MULTICOLLCNT	0x013c /* Multiple Collision Frame Counter */
#define GEM_EXCESSCOLLCNT	0x0140 /* Excessive Collision Frame Counter */
#define GEM_LATECOLLCNT		0x0144 /* Late Collision Frame Counter */
#define GEM_TXDEFERCNT		0x0148 /* Deferred Transmission Frame Counter */
#define GEM_TXCSENSECNT		0x014c /* Carrier Sense Error Counter */
#define GEM_ORX			0x0150 /* Octets received */
#define GEM_OCTRXL		0x0150 /* Octets received [31:0] */
#define GEM_OCTRXH		0x0154 /* Octets received [47:32] */
#define GEM_RXCNT		0x0158 /* Frames Received Counter */
#define GEM_RXBROADCNT		0x015c /* Broadcast Frames Received Counter */
#define GEM_RXMULTICNT		0x0160 /* Multicast Frames Received Counter */
#define GEM_RXPAUSECNT		0x0164 /* Pause Frames Received Counter */
#define GEM_RX64CNT		0x0168 /* 64 byte Frames RX Counter */
#define GEM_RX65CNT		0x016c /* 65-127 byte Frames RX Counter */
#define GEM_RX128CNT		0x0170 /* 128-255 byte Frames RX Counter */
#define GEM_RX256CNT		0x0174 /* 256-511 byte Frames RX Counter */
#define GEM_RX512CNT		0x0178 /* 512-1023 byte Frames RX Counter */
#define GEM_RX1024CNT		0x017c /* 1024-1518 byte Frames RX Counter */
#define GEM_RX1519CNT		0x0180 /* 1519+ byte Frames RX Counter */
#define GEM_RXUNDRCNT		0x0184 /* Undersize Frames Received Counter */
#define GEM_RXOVRCNT		0x0188 /* Oversize Frames Received Counter */
#define GEM_RXJABCNT		0x018c /* Jabbers Received Counter */
#define GEM_RXFCSCNT		0x0190 /* Frame Check Sequence Error Counter */
#define GEM_RXLENGTHCNT		0x0194 /* Length Field Error Counter */
#define GEM_RXSYMBCNT		0x0198 /* Symbol Error Counter */
#define GEM_RXALIGNCNT		0x019c /* Alignment Error Counter */
#define GEM_RXRESERRCNT		0x01a0 /* Receive Resource Error Counter */
#define GEM_RXORCNT		0x01a4 /* Receive Overrun Counter */
#define GEM_RXIPCCNT		0x01a8 /* IP header Checksum Error Counter */
#define GEM_RXTCPCCNT		0x01ac /* TCP Checksum Error Counter */
#define GEM_RXUDPCCNT		0x01b0 /* UDP Checksum Error Counter */
#define GEM_DCFG1		0x0280 /* Design Config 1 */
#define GEM_DCFG2		0x0284 /* Design Config 2 */
#define GEM_DCFG3		0x0288 /* Design Config 3 */
#define GEM_DCFG4		0x028c /* Design Config 4 */
#define GEM_DCFG5		0x0290 /* Design Config 5 */
#define GEM_DCFG6		0x0294 /* Design Config 6 */
#define GEM_DCFG7		0x0298 /* Design Config 7 */

#define GEM_ISR(hw_q)		(0x0400 + ((hw_q) << 2))
#define GEM_TBQP(hw_q)		(0x0440 + ((hw_q) << 2))
#define GEM_RBQP(hw_q)		(0x0480 + ((hw_q) << 2))
#define GEM_IER(hw_q)		(0x0600 + ((hw_q) << 2))
#define GEM_IDR(hw_q)		(0x0620 + ((hw_q) << 2))
#define GEM_IMR(hw_q)		(0x0640 + ((hw_q) << 2))

/* Bitfields in NCR */
#define MACB_LB_OFFSET		0 /* reserved */
#define MACB_LB_SIZE		1
#define MACB_LLB_OFFSET		1 /* Loop back local */
#define MACB_LLB_SIZE		1
#define MACB_RE_OFFSET		2 /* Receive enable */
#define MACB_RE_SIZE		1
#define MACB_TE_OFFSET		3 /* Transmit enable */
#define MACB_TE_SIZE		1
#define MACB_MPE_OFFSET		4 /* Management port enable */
#define MACB_MPE_SIZE		1
#define MACB_CLRSTAT_OFFSET	5 /* Clear stats regs */
#define MACB_CLRSTAT_SIZE	1
#define MACB_INCSTAT_OFFSET	6 /* Incremental stats regs */
#define MACB_INCSTAT_SIZE	1
#define MACB_WESTAT_OFFSET	7 /* Write enable stats regs */
#define MACB_WESTAT_SIZE	1
#define MACB_BP_OFFSET		8 /* Back pressure */
#define MACB_BP_SIZE		1
#define MACB_TSTART_OFFSET	9 /* Start transmission */
#define MACB_TSTART_SIZE	1
#define MACB_THALT_OFFSET	10 /* Transmit halt */
#define MACB_THALT_SIZE		1
#define MACB_NCR_TPF_OFFSET	11 /* Transmit pause frame */
#define MACB_NCR_TPF_SIZE	1
#define MACB_TZQ_OFFSET		12 /* Transmit zero quantum pause frame */
#define MACB_TZQ_SIZE		1

/* Bitfields in NCFGR */
#define MACB_SPD_OFFSET		0 /* Speed */
#define MACB_SPD_SIZE		1
#define MACB_FD_OFFSET		1 /* Full duplex */
#define MACB_FD_SIZE		1
#define MACB_BIT_RATE_OFFSET	2 /* Discard non-VLAN frames */
#define MACB_BIT_RATE_SIZE	1
#define MACB_JFRAME_OFFSET	3 /* reserved */
#define MACB_JFRAME_SIZE	1
#define MACB_CAF_OFFSET		4 /* Copy all frames */
#define MACB_CAF_SIZE		1
#define MACB_NBC_OFFSET		5 /* No broadcast */
#define MACB_NBC_SIZE		1
#define MACB_NCFGR_MTI_OFFSET	6 /* Multicast hash enable */
#define MACB_NCFGR_MTI_SIZE	1
#define MACB_UNI_OFFSET		7 /* Unicast hash enable */
#define MACB_UNI_SIZE		1
#define MACB_BIG_OFFSET		8 /* Receive 1536 byte frames */
#define MACB_BIG_SIZE		1
#define MACB_EAE_OFFSET		9 /* External address match enable */
#define MACB_EAE_SIZE		1
#define MACB_CLK_OFFSET		10
#define MACB_CLK_SIZE		2
#define MACB_RTY_OFFSET		12 /* Retry test */
#define MACB_RTY_SIZE		1
#define MACB_PAE_OFFSET		13 /* Pause enable */
#define MACB_PAE_SIZE		1
#define MACB_RM9200_RMII_OFFSET	13 /* AT91RM9200 only */
#define MACB_RM9200_RMII_SIZE	1  /* AT91RM9200 only */
#define MACB_RBOF_OFFSET	14 /* Receive buffer offset */
#define MACB_RBOF_SIZE		2
#define MACB_RLCE_OFFSET	16 /* Length field error frame discard */
#define MACB_RLCE_SIZE		1
#define MACB_DRFCS_OFFSET	17 /* FCS remove */
#define MACB_DRFCS_SIZE		1
#define MACB_EFRHD_OFFSET	18
#define MACB_EFRHD_SIZE		1
#define MACB_IRXFCS_OFFSET	19
#define MACB_IRXFCS_SIZE	1

/* GEM specific NCFGR bitfields. */
#define GEM_GBE_OFFSET		10 /* Gigabit mode enable */
#define GEM_GBE_SIZE		1
#define GEM_CLK_OFFSET		18 /* MDC clock division */
#define GEM_CLK_SIZE		3
#define GEM_DBW_OFFSET		21 /* Data bus width */
#define GEM_DBW_SIZE		2
#define GEM_RXCOEN_OFFSET	24
#define GEM_RXCOEN_SIZE		1

/* Constants for data bus width. */
#define GEM_DBW32		0 /* 32 bit AMBA AHB data bus width */
#define GEM_DBW64		1 /* 64 bit AMBA AHB data bus width */
#define GEM_DBW128		2 /* 128 bit AMBA AHB data bus width */

/* Bitfields in DMACFG. */
#define GEM_FBLDO_OFFSET	0 /* fixed burst length for DMA */
#define GEM_FBLDO_SIZE		5
#define GEM_ENDIA_DESC_OFFSET	6 /* endian swap mode for management descriptor access */
#define GEM_ENDIA_DESC_SIZE	1
#define GEM_ENDIA_PKT_OFFSET	7 /* endian swap mode for packet data access */
#define GEM_ENDIA_PKT_SIZE	1
#define GEM_RXBMS_OFFSET	8 /* RX packet buffer memory size select */
#define GEM_RXBMS_SIZE		2
#define GEM_TXPBMS_OFFSET	10 /* TX packet buffer memory size select */
#define GEM_TXPBMS_SIZE		1
#define GEM_TXCOEN_OFFSET	11 /* TX IP/TCP/UDP checksum gen offload */
#define GEM_TXCOEN_SIZE		1
#define GEM_RXBS_OFFSET		16 /* DMA receive buffer size */
#define GEM_RXBS_SIZE		8
#define GEM_DDRP_OFFSET		24 /* disc_when_no_ahb */
#define GEM_DDRP_SIZE		1


/* Bitfields in NSR */
#define MACB_NSR_LINK_OFFSET	0 /* pcs_link_state */
#define MACB_NSR_LINK_SIZE	1
#define MACB_MDIO_OFFSET	1 /* status of the mdio_in pin */
#define MACB_MDIO_SIZE		1
#define MACB_IDLE_OFFSET	2 /* The PHY management logic is idle */
#define MACB_IDLE_SIZE		1

/* Bitfields in TSR */
#define MACB_UBR_OFFSET		0 /* Used bit read */
#define MACB_UBR_SIZE		1
#define MACB_COL_OFFSET		1 /* Collision occurred */
#define MACB_COL_SIZE		1
#define MACB_TSR_RLE_OFFSET	2 /* Retry limit exceeded */
#define MACB_TSR_RLE_SIZE	1
#define MACB_TGO_OFFSET		3 /* Transmit go */
#define MACB_TGO_SIZE		1
#define MACB_BEX_OFFSET		4 /* TX frame corruption due to AHB error */
#define MACB_BEX_SIZE		1
#define MACB_RM9200_BNQ_OFFSET	4 /* AT91RM9200 only */
#define MACB_RM9200_BNQ_SIZE	1 /* AT91RM9200 only */
#define MACB_COMP_OFFSET	5 /* Trnasmit complete */
#define MACB_COMP_SIZE		1
#define MACB_UND_OFFSET		6 /* Trnasmit under run */
#define MACB_UND_SIZE		1

/* Bitfields in RSR */
#define MACB_BNA_OFFSET		0 /* Buffer not available */
#define MACB_BNA_SIZE		1
#define MACB_REC_OFFSET		1 /* Frame received */
#define MACB_REC_SIZE		1
#define MACB_OVR_OFFSET		2 /* Receive overrun */
#define MACB_OVR_SIZE		1

/* Bitfields in ISR/IER/IDR/IMR */
#define MACB_MFD_OFFSET		0 /* Management frame sent */
#define MACB_MFD_SIZE		1
#define MACB_RCOMP_OFFSET	1 /* Receive complete */
#define MACB_RCOMP_SIZE		1
#define MACB_RXUBR_OFFSET	2 /* RX used bit read */
#define MACB_RXUBR_SIZE		1
#define MACB_TXUBR_OFFSET	3 /* TX used bit read */
#define MACB_TXUBR_SIZE		1
#define MACB_ISR_TUND_OFFSET	4 /* Enable TX buffer under run interrupt */
#define MACB_ISR_TUND_SIZE	1
#define MACB_ISR_RLE_OFFSET	5 /* EN retry exceeded/late coll interrupt */
#define MACB_ISR_RLE_SIZE	1
#define MACB_TXERR_OFFSET	6 /* EN TX frame corrupt from error interrupt */
#define MACB_TXERR_SIZE		1
#define MACB_TCOMP_OFFSET	7 /* Enable transmit complete interrupt */
#define MACB_TCOMP_SIZE		1
#define MACB_ISR_LINK_OFFSET	9 /* Enable link change interrupt */
#define MACB_ISR_LINK_SIZE	1
#define MACB_ISR_ROVR_OFFSET	10 /* Enable receive overrun interrupt */
#define MACB_ISR_ROVR_SIZE	1
#define MACB_HRESP_OFFSET	11 /* Enable hrsep not OK interrupt */
#define MACB_HRESP_SIZE		1
#define MACB_PFR_OFFSET		12 /* Enable pause frame w/ quantum interrupt */
#define MACB_PFR_SIZE		1
#define MACB_PTZ_OFFSET		13 /* Enable pause time zero interrupt */
#define MACB_PTZ_SIZE		1

/* Bitfields in MAN */
#define MACB_DATA_OFFSET	0 /* data */
#define MACB_DATA_SIZE		16
#define MACB_CODE_OFFSET	16 /* Must be written to 10 */
#define MACB_CODE_SIZE		2
#define MACB_REGA_OFFSET	18 /* Register address */
#define MACB_REGA_SIZE		5
#define MACB_PHYA_OFFSET	23 /* PHY address */
#define MACB_PHYA_SIZE		5
#define MACB_RW_OFFSET		28 /* Operation. 10 is read. 01 is write. */
#define MACB_RW_SIZE		2
#define MACB_SOF_OFFSET		30 /* Must be written to 1 for Clause 22 */
#define MACB_SOF_SIZE		2

/* Bitfields in USRIO (AVR32) */
#define MACB_MII_OFFSET				0
#define MACB_MII_SIZE				1
#define MACB_EAM_OFFSET				1
#define MACB_EAM_SIZE				1
#define MACB_TX_PAUSE_OFFSET			2
#define MACB_TX_PAUSE_SIZE			1
#define MACB_TX_PAUSE_ZERO_OFFSET		3
#define MACB_TX_PAUSE_ZERO_SIZE			1

/* Bitfields in USRIO (AT91) */
#define MACB_RMII_OFFSET			0
#define MACB_RMII_SIZE				1
#define GEM_RGMII_OFFSET			0 /* GEM gigabit mode */
#define GEM_RGMII_SIZE				1
#define MACB_CLKEN_OFFSET			1
#define MACB_CLKEN_SIZE				1

/* Bitfields in WOL */
#define MACB_IP_OFFSET				0
#define MACB_IP_SIZE				16
#define MACB_MAG_OFFSET				16
#define MACB_MAG_SIZE				1
#define MACB_ARP_OFFSET				17
#define MACB_ARP_SIZE				1
#define MACB_SA1_OFFSET				18
#define MACB_SA1_SIZE				1
#define MACB_WOL_MTI_OFFSET			19
#define MACB_WOL_MTI_SIZE			1

/* Bitfields in MID */
#define MACB_IDNUM_OFFSET			16
#define MACB_IDNUM_SIZE				12
#define MACB_REV_OFFSET				0
#define MACB_REV_SIZE				16

/* Bitfields in DCFG1. */
#define GEM_IRQCOR_OFFSET			23
#define GEM_IRQCOR_SIZE				1
#define GEM_DBWDEF_OFFSET			25
#define GEM_DBWDEF_SIZE				3

/* Bitfields in DCFG2. */
#define GEM_RX_PKT_BUFF_OFFSET			20
#define GEM_RX_PKT_BUFF_SIZE			1
#define GEM_TX_PKT_BUFF_OFFSET			21
#define GEM_TX_PKT_BUFF_SIZE			1

/* Constants for CLK */
#define MACB_CLK_DIV8				0
#define MACB_CLK_DIV16				1
#define MACB_CLK_DIV32				2
#define MACB_CLK_DIV64				3

/* GEM specific constants for CLK. */
#define GEM_CLK_DIV8				0
#define GEM_CLK_DIV16				1
#define GEM_CLK_DIV32				2
#define GEM_CLK_DIV48				3
#define GEM_CLK_DIV64				4
#define GEM_CLK_DIV96				5

/* Constants for MAN register */
#define MACB_MAN_SOF				1
#define MACB_MAN_WRITE				1
#define MACB_MAN_READ				2
#define MACB_MAN_CODE				2

/* Capability mask bits */
#define MACB_CAPS_ISR_CLEAR_ON_WRITE		0x00000001
#define MACB_CAPS_USRIO_HAS_CLKEN		0x00000002
#define MACB_CAPS_USRIO_DEFAULT_IS_MII		0x00000004
#define MACB_CAPS_NO_GIGABIT_HALF		0x00000008
#define MACB_CAPS_FIFO_MODE			0x10000000
#define MACB_CAPS_GIGABIT_MODE_AVAILABLE	0x20000000
#define MACB_CAPS_SG_DISABLED			0x40000000
#define MACB_CAPS_MACB_IS_GEM			0x80000000
#define MACB_CAPS_JUMBO				0x00000008

/* Bit manipulation macros */
#define MACB_BIT(name)					\
	(1 << MACB_##name##_OFFSET)
#define MACB_BF(name,value)				\
	(((value) & ((1 << MACB_##name##_SIZE) - 1))	\
	 << MACB_##name##_OFFSET)
#define MACB_BFEXT(name,value)\
	(((value) >> MACB_##name##_OFFSET)		\
	 & ((1 << MACB_##name##_SIZE) - 1))
#define MACB_BFINS(name,value,old)			\
	(((old) & ~(((1 << MACB_##name##_SIZE) - 1)	\
		    << MACB_##name##_OFFSET))		\
	 | MACB_BF(name,value))

#define GEM_BIT(name)					\
	(1 << GEM_##name##_OFFSET)
#define GEM_BF(name, value)				\
	(((value) & ((1 << GEM_##name##_SIZE) - 1))	\
	 << GEM_##name##_OFFSET)
#define GEM_BFEXT(name, value)\
	(((value) >> GEM_##name##_OFFSET)		\
	 & ((1 << GEM_##name##_SIZE) - 1))
#define GEM_BFINS(name, value, old)			\
	(((old) & ~(((1 << GEM_##name##_SIZE) - 1)	\
		    << GEM_##name##_OFFSET))		\
	 | GEM_BF(name, value))

/* Register access macros */
#define macb_readl(port,reg)				\
	readl_relaxed((port)->regs + MACB_##reg)
#define macb_writel(port,reg,value)			\
	writel_relaxed((value), (port)->regs + MACB_##reg)
#define gem_readl(port, reg)				\
	readl_relaxed((port)->regs + GEM_##reg)
#define gem_writel(port, reg, value)			\
	writel_relaxed((value), (port)->regs + GEM_##reg)
#define queue_readl(queue, reg)				\
	readl_relaxed((queue)->bp->regs + (queue)->reg)
#define queue_writel(queue, reg, value)			\
	writel_relaxed((value), (queue)->bp->regs + (queue)->reg)

/* Conditional GEM/MACB macros.  These perform the operation to the correct
 * register dependent on whether the device is a GEM or a MACB.  For registers
 * and bitfields that are common across both devices, use macb_{read,write}l
 * to avoid the cost of the conditional.
 */
#define macb_or_gem_writel(__bp, __reg, __value) \
	({ \
		if (macb_is_gem((__bp))) \
			gem_writel((__bp), __reg, __value); \
		else \
			macb_writel((__bp), __reg, __value); \
	})

#define macb_or_gem_readl(__bp, __reg) \
	({ \
		u32 __v; \
		if (macb_is_gem((__bp))) \
			__v = gem_readl((__bp), __reg); \
		else \
			__v = macb_readl((__bp), __reg); \
		__v; \
	})

/* struct macb_dma_desc - Hardware DMA descriptor
 * @addr: DMA address of data buffer
 * @ctrl: Control and status bits
 */
struct macb_dma_desc {
	u32	addr;
	u32	ctrl;
};

/* DMA descriptor bitfields */
#define MACB_RX_USED_OFFSET			0
#define MACB_RX_USED_SIZE			1
#define MACB_RX_WRAP_OFFSET			1
#define MACB_RX_WRAP_SIZE			1
#define MACB_RX_WADDR_OFFSET			2
#define MACB_RX_WADDR_SIZE			30

#define MACB_RX_FRMLEN_OFFSET			0
#define MACB_RX_FRMLEN_SIZE			12
#define MACB_RX_OFFSET_OFFSET			12
#define MACB_RX_OFFSET_SIZE			2
#define MACB_RX_SOF_OFFSET			14
#define MACB_RX_SOF_SIZE			1
#define MACB_RX_EOF_OFFSET			15
#define MACB_RX_EOF_SIZE			1
#define MACB_RX_CFI_OFFSET			16
#define MACB_RX_CFI_SIZE			1
#define MACB_RX_VLAN_PRI_OFFSET			17
#define MACB_RX_VLAN_PRI_SIZE			3
#define MACB_RX_PRI_TAG_OFFSET			20
#define MACB_RX_PRI_TAG_SIZE			1
#define MACB_RX_VLAN_TAG_OFFSET			21
#define MACB_RX_VLAN_TAG_SIZE			1
#define MACB_RX_TYPEID_MATCH_OFFSET		22
#define MACB_RX_TYPEID_MATCH_SIZE		1
#define MACB_RX_SA4_MATCH_OFFSET		23
#define MACB_RX_SA4_MATCH_SIZE			1
#define MACB_RX_SA3_MATCH_OFFSET		24
#define MACB_RX_SA3_MATCH_SIZE			1
#define MACB_RX_SA2_MATCH_OFFSET		25
#define MACB_RX_SA2_MATCH_SIZE			1
#define MACB_RX_SA1_MATCH_OFFSET		26
#define MACB_RX_SA1_MATCH_SIZE			1
#define MACB_RX_EXT_MATCH_OFFSET		28
#define MACB_RX_EXT_MATCH_SIZE			1
#define MACB_RX_UHASH_MATCH_OFFSET		29
#define MACB_RX_UHASH_MATCH_SIZE		1
#define MACB_RX_MHASH_MATCH_OFFSET		30
#define MACB_RX_MHASH_MATCH_SIZE		1
#define MACB_RX_BROADCAST_OFFSET		31
#define MACB_RX_BROADCAST_SIZE			1

#define MACB_RX_FRMLEN_MASK			0xFFF
#define MACB_RX_JFRMLEN_MASK			0x3FFF

/* RX checksum offload disabled: bit 24 clear in NCFGR */
#define GEM_RX_TYPEID_MATCH_OFFSET		22
#define GEM_RX_TYPEID_MATCH_SIZE		2

/* RX checksum offload enabled: bit 24 set in NCFGR */
#define GEM_RX_CSUM_OFFSET			22
#define GEM_RX_CSUM_SIZE			2

#define MACB_TX_FRMLEN_OFFSET			0
#define MACB_TX_FRMLEN_SIZE			11
#define MACB_TX_LAST_OFFSET			15
#define MACB_TX_LAST_SIZE			1
#define MACB_TX_NOCRC_OFFSET			16
#define MACB_TX_NOCRC_SIZE			1
#define MACB_TX_BUF_EXHAUSTED_OFFSET		27
#define MACB_TX_BUF_EXHAUSTED_SIZE		1
#define MACB_TX_UNDERRUN_OFFSET			28
#define MACB_TX_UNDERRUN_SIZE			1
#define MACB_TX_ERROR_OFFSET			29
#define MACB_TX_ERROR_SIZE			1
#define MACB_TX_WRAP_OFFSET			30
#define MACB_TX_WRAP_SIZE			1
#define MACB_TX_USED_OFFSET			31
#define MACB_TX_USED_SIZE			1

#define GEM_TX_FRMLEN_OFFSET			0
#define GEM_TX_FRMLEN_SIZE			14

/* Buffer descriptor constants */
#define GEM_RX_CSUM_NONE			0
#define GEM_RX_CSUM_IP_ONLY			1
#define GEM_RX_CSUM_IP_TCP			2
#define GEM_RX_CSUM_IP_UDP			3

/* limit RX checksum offload to TCP and UDP packets */
#define GEM_RX_CSUM_CHECKED_MASK		2

/* struct macb_tx_skb - data about an skb which is being transmitted
 * @skb: skb currently being transmitted, only set for the last buffer
 *       of the frame
 * @mapping: DMA address of the skb's fragment buffer
 * @size: size of the DMA mapped buffer
 * @mapped_as_page: true when buffer was mapped with skb_frag_dma_map(),
 *                  false when buffer was mapped with dma_map_single()
 */
struct macb_tx_skb {
	struct sk_buff		*skb;
	dma_addr_t		mapping;
	size_t			size;
	bool			mapped_as_page;
};

/* Hardware-collected statistics. Used when updating the network
 * device stats by a periodic timer.
 */
struct macb_stats {
	u32	rx_pause_frames;
	u32	tx_ok;
	u32	tx_single_cols;
	u32	tx_multiple_cols;
	u32	rx_ok;
	u32	rx_fcs_errors;
	u32	rx_align_errors;
	u32	tx_deferred;
	u32	tx_late_cols;
	u32	tx_excessive_cols;
	u32	tx_underruns;
	u32	tx_carrier_errors;
	u32	rx_resource_errors;
	u32	rx_overruns;
	u32	rx_symbol_errors;
	u32	rx_oversize_pkts;
	u32	rx_jabbers;
	u32	rx_undersize_pkts;
	u32	sqe_test_errors;
	u32	rx_length_mismatch;
	u32	tx_pause_frames;
};

struct gem_stats {
	u32	tx_octets_31_0;
	u32	tx_octets_47_32;
	u32	tx_frames;
	u32	tx_broadcast_frames;
	u32	tx_multicast_frames;
	u32	tx_pause_frames;
	u32	tx_64_byte_frames;
	u32	tx_65_127_byte_frames;
	u32	tx_128_255_byte_frames;
	u32	tx_256_511_byte_frames;
	u32	tx_512_1023_byte_frames;
	u32	tx_1024_1518_byte_frames;
	u32	tx_greater_than_1518_byte_frames;
	u32	tx_underrun;
	u32	tx_single_collision_frames;
	u32	tx_multiple_collision_frames;
	u32	tx_excessive_collisions;
	u32	tx_late_collisions;
	u32	tx_deferred_frames;
	u32	tx_carrier_sense_errors;
	u32	rx_octets_31_0;
	u32	rx_octets_47_32;
	u32	rx_frames;
	u32	rx_broadcast_frames;
	u32	rx_multicast_frames;
	u32	rx_pause_frames;
	u32	rx_64_byte_frames;
	u32	rx_65_127_byte_frames;
	u32	rx_128_255_byte_frames;
	u32	rx_256_511_byte_frames;
	u32	rx_512_1023_byte_frames;
	u32	rx_1024_1518_byte_frames;
	u32	rx_greater_than_1518_byte_frames;
	u32	rx_undersized_frames;
	u32	rx_oversize_frames;
	u32	rx_jabbers;
	u32	rx_frame_check_sequence_errors;
	u32	rx_length_field_frame_errors;
	u32	rx_symbol_errors;
	u32	rx_alignment_errors;
	u32	rx_resource_errors;
	u32	rx_overruns;
	u32	rx_ip_header_checksum_errors;
	u32	rx_tcp_checksum_errors;
	u32	rx_udp_checksum_errors;
};

/* Describes the name and offset of an individual statistic register, as
 * returned by `ethtool -S`. Also describes which net_device_stats statistics
 * this register should contribute to.
 */
struct gem_statistic {
	char stat_string[ETH_GSTRING_LEN];
	int offset;
	u32 stat_bits;
};

/* Bitfield defs for net_device_stat statistics */
#define GEM_NDS_RXERR_OFFSET		0
#define GEM_NDS_RXLENERR_OFFSET		1
#define GEM_NDS_RXOVERERR_OFFSET	2
#define GEM_NDS_RXCRCERR_OFFSET		3
#define GEM_NDS_RXFRAMEERR_OFFSET	4
#define GEM_NDS_RXFIFOERR_OFFSET	5
#define GEM_NDS_TXERR_OFFSET		6
#define GEM_NDS_TXABORTEDERR_OFFSET	7
#define GEM_NDS_TXCARRIERERR_OFFSET	8
#define GEM_NDS_TXFIFOERR_OFFSET	9
#define GEM_NDS_COLLISIONS_OFFSET	10

#define GEM_STAT_TITLE(name, title) GEM_STAT_TITLE_BITS(name, title, 0)
#define GEM_STAT_TITLE_BITS(name, title, bits) {	\
	.stat_string = title,				\
	.offset = GEM_##name,				\
	.stat_bits = bits				\
}

/* list of gem statistic registers. The names MUST match the
 * corresponding GEM_* definitions.
 */
static const struct gem_statistic gem_statistics[] = {
	GEM_STAT_TITLE(OCTTXL, "tx_octets"), /* OCTTXH combined with OCTTXL */
	GEM_STAT_TITLE(TXCNT, "tx_frames"),
	GEM_STAT_TITLE(TXBCCNT, "tx_broadcast_frames"),
	GEM_STAT_TITLE(TXMCCNT, "tx_multicast_frames"),
	GEM_STAT_TITLE(TXPAUSECNT, "tx_pause_frames"),
	GEM_STAT_TITLE(TX64CNT, "tx_64_byte_frames"),
	GEM_STAT_TITLE(TX65CNT, "tx_65_127_byte_frames"),
	GEM_STAT_TITLE(TX128CNT, "tx_128_255_byte_frames"),
	GEM_STAT_TITLE(TX256CNT, "tx_256_511_byte_frames"),
	GEM_STAT_TITLE(TX512CNT, "tx_512_1023_byte_frames"),
	GEM_STAT_TITLE(TX1024CNT, "tx_1024_1518_byte_frames"),
	GEM_STAT_TITLE(TX1519CNT, "tx_greater_than_1518_byte_frames"),
	GEM_STAT_TITLE_BITS(TXURUNCNT, "tx_underrun",
			    GEM_BIT(NDS_TXERR)|GEM_BIT(NDS_TXFIFOERR)),
	GEM_STAT_TITLE_BITS(SNGLCOLLCNT, "tx_single_collision_frames",
			    GEM_BIT(NDS_TXERR)|GEM_BIT(NDS_COLLISIONS)),
	GEM_STAT_TITLE_BITS(MULTICOLLCNT, "tx_multiple_collision_frames",
			    GEM_BIT(NDS_TXERR)|GEM_BIT(NDS_COLLISIONS)),
	GEM_STAT_TITLE_BITS(EXCESSCOLLCNT, "tx_excessive_collisions",
			    GEM_BIT(NDS_TXERR)|
			    GEM_BIT(NDS_TXABORTEDERR)|
			    GEM_BIT(NDS_COLLISIONS)),
	GEM_STAT_TITLE_BITS(LATECOLLCNT, "tx_late_collisions",
			    GEM_BIT(NDS_TXERR)|GEM_BIT(NDS_COLLISIONS)),
	GEM_STAT_TITLE(TXDEFERCNT, "tx_deferred_frames"),
	GEM_STAT_TITLE_BITS(TXCSENSECNT, "tx_carrier_sense_errors",
			    GEM_BIT(NDS_TXERR)|GEM_BIT(NDS_COLLISIONS)),
	GEM_STAT_TITLE(OCTRXL, "rx_octets"), /* OCTRXH combined with OCTRXL */
	GEM_STAT_TITLE(RXCNT, "rx_frames"),
	GEM_STAT_TITLE(RXBROADCNT, "rx_broadcast_frames"),
	GEM_STAT_TITLE(RXMULTICNT, "rx_multicast_frames"),
	GEM_STAT_TITLE(RXPAUSECNT, "rx_pause_frames"),
	GEM_STAT_TITLE(RX64CNT, "rx_64_byte_frames"),
	GEM_STAT_TITLE(RX65CNT, "rx_65_127_byte_frames"),
	GEM_STAT_TITLE(RX128CNT, "rx_128_255_byte_frames"),
	GEM_STAT_TITLE(RX256CNT, "rx_256_511_byte_frames"),
	GEM_STAT_TITLE(RX512CNT, "rx_512_1023_byte_frames"),
	GEM_STAT_TITLE(RX1024CNT, "rx_1024_1518_byte_frames"),
	GEM_STAT_TITLE(RX1519CNT, "rx_greater_than_1518_byte_frames"),
	GEM_STAT_TITLE_BITS(RXUNDRCNT, "rx_undersized_frames",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXLENERR)),
	GEM_STAT_TITLE_BITS(RXOVRCNT, "rx_oversize_frames",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXLENERR)),
	GEM_STAT_TITLE_BITS(RXJABCNT, "rx_jabbers",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXLENERR)),
	GEM_STAT_TITLE_BITS(RXFCSCNT, "rx_frame_check_sequence_errors",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXCRCERR)),
	GEM_STAT_TITLE_BITS(RXLENGTHCNT, "rx_length_field_frame_errors",
			    GEM_BIT(NDS_RXERR)),
	GEM_STAT_TITLE_BITS(RXSYMBCNT, "rx_symbol_errors",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXFRAMEERR)),
	GEM_STAT_TITLE_BITS(RXALIGNCNT, "rx_alignment_errors",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXOVERERR)),
	GEM_STAT_TITLE_BITS(RXRESERRCNT, "rx_resource_errors",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXOVERERR)),
	GEM_STAT_TITLE_BITS(RXORCNT, "rx_overruns",
			    GEM_BIT(NDS_RXERR)|GEM_BIT(NDS_RXFIFOERR)),
	GEM_STAT_TITLE_BITS(RXIPCCNT, "rx_ip_header_checksum_errors",
			    GEM_BIT(NDS_RXERR)),
	GEM_STAT_TITLE_BITS(RXTCPCCNT, "rx_tcp_checksum_errors",
			    GEM_BIT(NDS_RXERR)),
	GEM_STAT_TITLE_BITS(RXUDPCCNT, "rx_udp_checksum_errors",
			    GEM_BIT(NDS_RXERR)),
};

#define GEM_STATS_LEN ARRAY_SIZE(gem_statistics)

struct macb;

struct macb_or_gem_ops {
	int	(*mog_alloc_rx_buffers)(struct macb *bp);
	void	(*mog_free_rx_buffers)(struct macb *bp);
	void	(*mog_init_rings)(struct macb *bp);
	int	(*mog_rx)(struct macb *bp, int budget);
};

struct macb_config {
	u32			caps;
	unsigned int		dma_burst_length;
	int	(*clk_init)(struct platform_device *pdev, struct clk **pclk,
			    struct clk **hclk, struct clk **tx_clk);
	int	(*init)(struct platform_device *pdev);
	int	jumbo_max_len;
};

struct macb_queue {
	struct macb		*bp;
	int			irq;

	unsigned int		ISR;
	unsigned int		IER;
	unsigned int		IDR;
	unsigned int		IMR;
	unsigned int		TBQP;

	unsigned int		tx_head, tx_tail;
	struct macb_dma_desc	*tx_ring;
	struct macb_tx_skb	*tx_skb;
	dma_addr_t		tx_ring_dma;
	struct work_struct	tx_error_task;
};

struct macb {
	void __iomem		*regs;

	unsigned int		rx_tail;
	unsigned int		rx_prepared_head;
	struct macb_dma_desc	*rx_ring;
	struct sk_buff		**rx_skbuff;
	void			*rx_buffers;
	size_t			rx_buffer_size;

	unsigned int		num_queues;
	unsigned int		queue_mask;
	struct macb_queue	queues[MACB_MAX_QUEUES];

	spinlock_t		lock;
	struct platform_device	*pdev;
	struct clk		*pclk;
	struct clk		*hclk;
	struct clk		*tx_clk;
	struct net_device	*dev;
	struct napi_struct	napi;
	struct net_device_stats	stats;
	union {
		struct macb_stats	macb;
		struct gem_stats	gem;
	}			hw_stats;

	dma_addr_t		rx_ring_dma;
	dma_addr_t		rx_buffers_dma;

	struct macb_or_gem_ops	macbgem_ops;

	struct mii_bus		*mii_bus;
	struct phy_device	*phy_dev;
	unsigned int 		link;
	unsigned int 		speed;
	unsigned int 		duplex;

	u32			caps;
	unsigned int		dma_burst_length;

	phy_interface_t		phy_interface;

	/* AT91RM9200 transmit */
	struct sk_buff *skb;			/* holds skb until xmit interrupt completes */
	dma_addr_t skb_physaddr;		/* phys addr from pci_map_single */
	int skb_length;				/* saved skb length for pci_unmap_single */
	unsigned int		max_tx_length;

	u64			ethtool_stats[GEM_STATS_LEN];

	unsigned int		rx_frm_len_mask;
	unsigned int		jumbo_max_len;
};

static inline bool macb_is_gem(struct macb *bp)
{
	return !!(bp->caps & MACB_CAPS_MACB_IS_GEM);
}

static inline bool macb_is_gem_hw(void __iomem *addr)
{
	return !!(MACB_BFEXT(IDNUM, readl_relaxed(addr + MACB_MID)) >= 0x2);
}

#endif /* _MACB_H */
