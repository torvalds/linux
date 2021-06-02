/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Actions Semi Owl SoCs Ethernet MAC driver
 *
 * Copyright (c) 2012 Actions Semi Inc.
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef __OWL_EMAC_H__
#define __OWL_EMAC_H__

#define OWL_EMAC_DRVNAME			"owl-emac"

#define OWL_EMAC_POLL_DELAY_USEC		5
#define OWL_EMAC_MDIO_POLL_TIMEOUT_USEC		1000
#define OWL_EMAC_RESET_POLL_TIMEOUT_USEC	2000
#define OWL_EMAC_TX_TIMEOUT			(2 * HZ)

#define OWL_EMAC_MTU_MIN			ETH_MIN_MTU
#define OWL_EMAC_MTU_MAX			ETH_DATA_LEN
#define OWL_EMAC_RX_FRAME_MAX_LEN		(ETH_FRAME_LEN + ETH_FCS_LEN)
#define OWL_EMAC_SKB_ALIGN			4
#define OWL_EMAC_SKB_RESERVE			18

#define OWL_EMAC_MAX_MULTICAST_ADDRS		14
#define OWL_EMAC_SETUP_FRAME_LEN		192

#define OWL_EMAC_RX_RING_SIZE			64
#define OWL_EMAC_TX_RING_SIZE			32

/* Bus mode register */
#define OWL_EMAC_REG_MAC_CSR0			0x0000
#define OWL_EMAC_BIT_MAC_CSR0_SWR		BIT(0)	/* Software reset */

/* Transmit/receive poll demand registers */
#define OWL_EMAC_REG_MAC_CSR1			0x0008
#define OWL_EMAC_VAL_MAC_CSR1_TPD		0x01
#define OWL_EMAC_REG_MAC_CSR2			0x0010
#define OWL_EMAC_VAL_MAC_CSR2_RPD		0x01

/* Receive/transmit descriptor list base address registers */
#define OWL_EMAC_REG_MAC_CSR3			0x0018
#define OWL_EMAC_REG_MAC_CSR4			0x0020

/* Status register */
#define OWL_EMAC_REG_MAC_CSR5			0x0028
#define OWL_EMAC_MSK_MAC_CSR5_TS		GENMASK(22, 20)	/* Transmit process state */
#define OWL_EMAC_OFF_MAC_CSR5_TS		20
#define OWL_EMAC_VAL_MAC_CSR5_TS_DATA		0x03	/* Transferring data HOST -> FIFO */
#define OWL_EMAC_VAL_MAC_CSR5_TS_CDES		0x07	/* Closing transmit descriptor */
#define OWL_EMAC_MSK_MAC_CSR5_RS		GENMASK(19, 17)	/* Receive process state */
#define OWL_EMAC_OFF_MAC_CSR5_RS		17
#define OWL_EMAC_VAL_MAC_CSR5_RS_FDES		0x01	/* Fetching receive descriptor */
#define OWL_EMAC_VAL_MAC_CSR5_RS_CDES		0x05	/* Closing receive descriptor */
#define OWL_EMAC_VAL_MAC_CSR5_RS_DATA		0x07	/* Transferring data FIFO -> HOST */
#define OWL_EMAC_BIT_MAC_CSR5_NIS		BIT(16)	/* Normal interrupt summary */
#define OWL_EMAC_BIT_MAC_CSR5_AIS		BIT(15)	/* Abnormal interrupt summary */
#define OWL_EMAC_BIT_MAC_CSR5_ERI		BIT(14)	/* Early receive interrupt */
#define OWL_EMAC_BIT_MAC_CSR5_GTE		BIT(11)	/* General-purpose timer expiration */
#define OWL_EMAC_BIT_MAC_CSR5_ETI		BIT(10)	/* Early transmit interrupt */
#define OWL_EMAC_BIT_MAC_CSR5_RPS		BIT(8)	/* Receive process stopped */
#define OWL_EMAC_BIT_MAC_CSR5_RU		BIT(7)	/* Receive buffer unavailable */
#define OWL_EMAC_BIT_MAC_CSR5_RI		BIT(6)	/* Receive interrupt */
#define OWL_EMAC_BIT_MAC_CSR5_UNF		BIT(5)	/* Transmit underflow */
#define OWL_EMAC_BIT_MAC_CSR5_LCIS		BIT(4)	/* Link change status */
#define OWL_EMAC_BIT_MAC_CSR5_LCIQ		BIT(3)	/* Link change interrupt */
#define OWL_EMAC_BIT_MAC_CSR5_TU		BIT(2)	/* Transmit buffer unavailable */
#define OWL_EMAC_BIT_MAC_CSR5_TPS		BIT(1)	/* Transmit process stopped */
#define OWL_EMAC_BIT_MAC_CSR5_TI		BIT(0)	/* Transmit interrupt */

/* Operation mode register */
#define OWL_EMAC_REG_MAC_CSR6			0x0030
#define OWL_EMAC_BIT_MAC_CSR6_RA		BIT(30)	/* Receive all */
#define OWL_EMAC_BIT_MAC_CSR6_TTM		BIT(22)	/* Transmit threshold mode */
#define OWL_EMAC_BIT_MAC_CSR6_SF		BIT(21)	/* Store and forward */
#define OWL_EMAC_MSK_MAC_CSR6_SPEED		GENMASK(17, 16)	/* Eth speed selection */
#define OWL_EMAC_OFF_MAC_CSR6_SPEED		16
#define OWL_EMAC_VAL_MAC_CSR6_SPEED_100M	0x00
#define OWL_EMAC_VAL_MAC_CSR6_SPEED_10M		0x02
#define OWL_EMAC_BIT_MAC_CSR6_ST		BIT(13)	/* Start/stop transmit command */
#define OWL_EMAC_BIT_MAC_CSR6_LP		BIT(10)	/* Loopback mode */
#define OWL_EMAC_BIT_MAC_CSR6_FD		BIT(9)	/* Full duplex mode */
#define OWL_EMAC_BIT_MAC_CSR6_PM		BIT(7)	/* Pass all multicast */
#define OWL_EMAC_BIT_MAC_CSR6_PR		BIT(6)	/* Promiscuous mode */
#define OWL_EMAC_BIT_MAC_CSR6_IF		BIT(4)	/* Inverse filtering */
#define OWL_EMAC_BIT_MAC_CSR6_PB		BIT(3)	/* Pass bad frames */
#define OWL_EMAC_BIT_MAC_CSR6_HO		BIT(2)	/* Hash only filtering mode */
#define OWL_EMAC_BIT_MAC_CSR6_SR		BIT(1)	/* Start/stop receive command */
#define OWL_EMAC_BIT_MAC_CSR6_HP		BIT(0)	/* Hash/perfect receive filtering mode */
#define OWL_EMAC_MSK_MAC_CSR6_STSR	       (OWL_EMAC_BIT_MAC_CSR6_ST | \
						OWL_EMAC_BIT_MAC_CSR6_SR)

/* Interrupt enable register */
#define OWL_EMAC_REG_MAC_CSR7			0x0038
#define OWL_EMAC_BIT_MAC_CSR7_NIE		BIT(16)	/* Normal interrupt summary enable */
#define OWL_EMAC_BIT_MAC_CSR7_AIE		BIT(15)	/* Abnormal interrupt summary enable */
#define OWL_EMAC_BIT_MAC_CSR7_ERE		BIT(14)	/* Early receive interrupt enable */
#define OWL_EMAC_BIT_MAC_CSR7_GTE		BIT(11)	/* General-purpose timer overflow */
#define OWL_EMAC_BIT_MAC_CSR7_ETE		BIT(10)	/* Early transmit interrupt enable */
#define OWL_EMAC_BIT_MAC_CSR7_RSE		BIT(8)	/* Receive stopped enable */
#define OWL_EMAC_BIT_MAC_CSR7_RUE		BIT(7)	/* Receive buffer unavailable enable */
#define OWL_EMAC_BIT_MAC_CSR7_RIE		BIT(6)	/* Receive interrupt enable */
#define OWL_EMAC_BIT_MAC_CSR7_UNE		BIT(5)	/* Underflow interrupt enable */
#define OWL_EMAC_BIT_MAC_CSR7_TUE		BIT(2)	/* Transmit buffer unavailable enable */
#define OWL_EMAC_BIT_MAC_CSR7_TSE		BIT(1)	/* Transmit stopped enable */
#define OWL_EMAC_BIT_MAC_CSR7_TIE		BIT(0)	/* Transmit interrupt enable */
#define OWL_EMAC_BIT_MAC_CSR7_ALL_NOT_TUE      (OWL_EMAC_BIT_MAC_CSR7_ERE | \
						OWL_EMAC_BIT_MAC_CSR7_GTE | \
						OWL_EMAC_BIT_MAC_CSR7_ETE | \
						OWL_EMAC_BIT_MAC_CSR7_RSE | \
						OWL_EMAC_BIT_MAC_CSR7_RUE | \
						OWL_EMAC_BIT_MAC_CSR7_RIE | \
						OWL_EMAC_BIT_MAC_CSR7_UNE | \
						OWL_EMAC_BIT_MAC_CSR7_TSE | \
						OWL_EMAC_BIT_MAC_CSR7_TIE)

/* Missed frames and overflow counter register */
#define OWL_EMAC_REG_MAC_CSR8			0x0040
/* MII management and serial ROM register */
#define OWL_EMAC_REG_MAC_CSR9			0x0048

/* MII serial management register */
#define OWL_EMAC_REG_MAC_CSR10			0x0050
#define OWL_EMAC_BIT_MAC_CSR10_SB		BIT(31)	/* Start transfer or busy */
#define OWL_EMAC_MSK_MAC_CSR10_CLKDIV		GENMASK(30, 28)	/* Clock divider */
#define OWL_EMAC_OFF_MAC_CSR10_CLKDIV		28
#define OWL_EMAC_VAL_MAC_CSR10_CLKDIV_128	0x04
#define OWL_EMAC_VAL_MAC_CSR10_OPCODE_WR	0x01	/* Register write command */
#define OWL_EMAC_OFF_MAC_CSR10_OPCODE		26	/* Operation mode */
#define OWL_EMAC_VAL_MAC_CSR10_OPCODE_DCG	0x00	/* Disable clock generation */
#define OWL_EMAC_VAL_MAC_CSR10_OPCODE_WR	0x01	/* Register write command */
#define OWL_EMAC_VAL_MAC_CSR10_OPCODE_RD	0x02	/* Register read command */
#define OWL_EMAC_VAL_MAC_CSR10_OPCODE_CDS	0x03	/* Clock divider set */
#define OWL_EMAC_MSK_MAC_CSR10_PHYADD		GENMASK(25, 21)	/* Physical layer address */
#define OWL_EMAC_OFF_MAC_CSR10_PHYADD		21
#define OWL_EMAC_MSK_MAC_CSR10_REGADD		GENMASK(20, 16)	/* Register address */
#define OWL_EMAC_OFF_MAC_CSR10_REGADD		16
#define OWL_EMAC_MSK_MAC_CSR10_DATA		GENMASK(15, 0)	/* Register data */

/* General-purpose timer and interrupt mitigation control register */
#define OWL_EMAC_REG_MAC_CSR11			0x0058
#define OWL_EMAC_OFF_MAC_CSR11_TT		27	/* Transmit timer */
#define OWL_EMAC_OFF_MAC_CSR11_NTP		24	/* No. of transmit packets */
#define OWL_EMAC_OFF_MAC_CSR11_RT		20	/* Receive timer */
#define OWL_EMAC_OFF_MAC_CSR11_NRP		17	/* No. of receive packets */

/* MAC address low/high registers */
#define OWL_EMAC_REG_MAC_CSR16			0x0080
#define OWL_EMAC_REG_MAC_CSR17			0x0088

/* Pause time & cache thresholds register */
#define OWL_EMAC_REG_MAC_CSR18			0x0090
#define OWL_EMAC_OFF_MAC_CSR18_CPTL		24	/* Cache pause threshold level */
#define OWL_EMAC_OFF_MAC_CSR18_CRTL		16	/* Cache restart threshold level */
#define OWL_EMAC_OFF_MAC_CSR18_PQT		0	/* Flow control pause quanta time */

/* FIFO pause & restart threshold register */
#define OWL_EMAC_REG_MAC_CSR19			0x0098
#define OWL_EMAC_OFF_MAC_CSR19_FPTL		16	/* FIFO pause threshold level */
#define OWL_EMAC_OFF_MAC_CSR19_FRTL		0	/* FIFO restart threshold level */

/* Flow control setup & status register */
#define OWL_EMAC_REG_MAC_CSR20			0x00A0
#define OWL_EMAC_BIT_MAC_CSR20_FCE		BIT(31)	/* Flow Control Enable */
#define OWL_EMAC_BIT_MAC_CSR20_TUE		BIT(30)	/* Transmit Un-pause frames Enable */
#define OWL_EMAC_BIT_MAC_CSR20_TPE		BIT(29)	/* Transmit Pause frames Enable */
#define OWL_EMAC_BIT_MAC_CSR20_RPE		BIT(28)	/* Receive Pause frames Enable */
#define OWL_EMAC_BIT_MAC_CSR20_BPE		BIT(27)	/* Back pressure (half-duplex) Enable */

/* MII control register */
#define OWL_EMAC_REG_MAC_CTRL			0x00B0
#define OWL_EMAC_BIT_MAC_CTRL_RRSB		BIT(8)	/* RMII_REFCLK select bit */
#define OWL_EMAC_OFF_MAC_CTRL_SSDC		4	/* SMII SYNC delay cycle */
#define OWL_EMAC_BIT_MAC_CTRL_RCPS		BIT(1)	/* REF_CLK phase select */
#define OWL_EMAC_BIT_MAC_CTRL_RSIS		BIT(0)	/* RMII/SMII interface select */

/* Receive descriptor status field */
#define OWL_EMAC_BIT_RDES0_OWN			BIT(31)	/* Ownership bit */
#define OWL_EMAC_BIT_RDES0_FF			BIT(30)	/* Filtering fail */
#define OWL_EMAC_MSK_RDES0_FL			GENMASK(29, 16)	/* Frame length */
#define OWL_EMAC_OFF_RDES0_FL			16
#define OWL_EMAC_BIT_RDES0_ES			BIT(15)	/* Error summary */
#define OWL_EMAC_BIT_RDES0_DE			BIT(14)	/* Descriptor error */
#define OWL_EMAC_BIT_RDES0_RF			BIT(11)	/* Runt frame */
#define OWL_EMAC_BIT_RDES0_MF			BIT(10)	/* Multicast frame */
#define OWL_EMAC_BIT_RDES0_FS			BIT(9)	/* First descriptor */
#define OWL_EMAC_BIT_RDES0_LS			BIT(8)	/* Last descriptor */
#define OWL_EMAC_BIT_RDES0_TL			BIT(7)	/* Frame too long */
#define OWL_EMAC_BIT_RDES0_CS			BIT(6)	/* Collision seen */
#define OWL_EMAC_BIT_RDES0_FT			BIT(5)	/* Frame type */
#define OWL_EMAC_BIT_RDES0_RE			BIT(3)	/* Report on MII error */
#define OWL_EMAC_BIT_RDES0_DB			BIT(2)	/* Dribbling bit */
#define OWL_EMAC_BIT_RDES0_CE			BIT(1)	/* CRC error */
#define OWL_EMAC_BIT_RDES0_ZERO			BIT(0)	/* Legal frame length indicator */

/* Receive descriptor control and count field */
#define OWL_EMAC_BIT_RDES1_RER			BIT(25)	/* Receive end of ring */
#define OWL_EMAC_MSK_RDES1_RBS1			GENMASK(10, 0) /* Buffer 1 size */

/* Transmit descriptor status field */
#define OWL_EMAC_BIT_TDES0_OWN			BIT(31)	/* Ownership bit */
#define OWL_EMAC_BIT_TDES0_ES			BIT(15)	/* Error summary */
#define OWL_EMAC_BIT_TDES0_LO			BIT(11)	/* Loss of carrier */
#define OWL_EMAC_BIT_TDES0_NC			BIT(10)	/* No carrier */
#define OWL_EMAC_BIT_TDES0_LC			BIT(9)	/* Late collision */
#define OWL_EMAC_BIT_TDES0_EC			BIT(8)	/* Excessive collisions */
#define OWL_EMAC_MSK_TDES0_CC			GENMASK(6, 3) /* Collision count */
#define OWL_EMAC_BIT_TDES0_UF			BIT(1)	/* Underflow error */
#define OWL_EMAC_BIT_TDES0_DE			BIT(0)	/* Deferred */

/* Transmit descriptor control and count field */
#define OWL_EMAC_BIT_TDES1_IC			BIT(31)	/* Interrupt on completion */
#define OWL_EMAC_BIT_TDES1_LS			BIT(30)	/* Last descriptor */
#define OWL_EMAC_BIT_TDES1_FS			BIT(29)	/* First descriptor */
#define OWL_EMAC_BIT_TDES1_FT1			BIT(28)	/* Filtering type */
#define OWL_EMAC_BIT_TDES1_SET			BIT(27)	/* Setup packet */
#define OWL_EMAC_BIT_TDES1_AC			BIT(26)	/* Add CRC disable */
#define OWL_EMAC_BIT_TDES1_TER			BIT(25)	/* Transmit end of ring */
#define OWL_EMAC_BIT_TDES1_DPD			BIT(23)	/* Disabled padding */
#define OWL_EMAC_BIT_TDES1_FT0			BIT(22)	/* Filtering type */
#define OWL_EMAC_MSK_TDES1_TBS1			GENMASK(10, 0) /* Buffer 1 size */

static const char *const owl_emac_clk_names[] = { "eth", "rmii" };
#define OWL_EMAC_NCLKS ARRAY_SIZE(owl_emac_clk_names)

enum owl_emac_clk_map {
	OWL_EMAC_CLK_ETH = 0,
	OWL_EMAC_CLK_RMII
};

struct owl_emac_addr_list {
	u8 addrs[OWL_EMAC_MAX_MULTICAST_ADDRS][ETH_ALEN];
	int count;
};

/* TX/RX descriptors */
struct owl_emac_ring_desc {
	u32 status;
	u32 control;
	u32 buf_addr;
	u32 reserved;		/* 2nd buffer address is not used */
};

struct owl_emac_ring {
	struct owl_emac_ring_desc *descs;
	dma_addr_t descs_dma;
	struct sk_buff **skbs;
	dma_addr_t *skbs_dma;
	unsigned int size;
	unsigned int head;
	unsigned int tail;
};

struct owl_emac_priv {
	struct net_device *netdev;
	void __iomem *base;

	struct clk_bulk_data clks[OWL_EMAC_NCLKS];
	struct reset_control *reset;

	struct owl_emac_ring rx_ring;
	struct owl_emac_ring tx_ring;

	struct mii_bus *mii;
	struct napi_struct napi;

	phy_interface_t phy_mode;
	unsigned int link;
	int speed;
	int duplex;
	int pause;
	struct owl_emac_addr_list mcaddr_list;

	struct work_struct mac_reset_task;

	u32 msg_enable;		/* Debug message level */
	spinlock_t lock;	/* Sync concurrent ring access */
};

#endif /* __OWL_EMAC_H__ */
