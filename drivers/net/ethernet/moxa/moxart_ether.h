/* MOXA ART Ethernet (RTL8201CP) driver.
 *
 * Copyright (C) 2013 Jonas Jensen
 *
 * Jonas Jensen <jonas.jensen@gmail.com>
 *
 * Based on code from
 * Moxa Technology Co., Ltd. <www.moxa.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _MOXART_ETHERNET_H
#define _MOXART_ETHERNET_H

#define TX_REG_OFFSET_DESC0	0
#define TX_REG_OFFSET_DESC1	4
#define TX_REG_OFFSET_DESC2	8
#define TX_REG_DESC_SIZE	16

#define RX_REG_OFFSET_DESC0	0
#define RX_REG_OFFSET_DESC1	4
#define RX_REG_OFFSET_DESC2	8
#define RX_REG_DESC_SIZE	16

#define TX_DESC0_PKT_LATE_COL	0x1		/* abort, late collision */
#define TX_DESC0_RX_PKT_EXS_COL	0x2		/* abort, >16 collisions */
#define TX_DESC0_DMA_OWN	0x80000000	/* owned by controller */
#define TX_DESC1_BUF_SIZE_MASK	0x7ff
#define TX_DESC1_LTS		0x8000000	/* last TX packet */
#define TX_DESC1_FTS		0x10000000	/* first TX packet */
#define TX_DESC1_FIFO_COMPLETE	0x20000000
#define TX_DESC1_INTR_COMPLETE	0x40000000
#define TX_DESC1_END		0x80000000
#define TX_DESC2_ADDRESS_PHYS	0
#define TX_DESC2_ADDRESS_VIRT	4

#define RX_DESC0_FRAME_LEN	0
#define RX_DESC0_FRAME_LEN_MASK	0x7FF
#define RX_DESC0_MULTICAST	0x10000
#define RX_DESC0_BROADCAST	0x20000
#define RX_DESC0_ERR		0x40000
#define RX_DESC0_CRC_ERR	0x80000
#define RX_DESC0_FTL		0x100000
#define RX_DESC0_RUNT		0x200000	/* packet less than 64 bytes */
#define RX_DESC0_ODD_NB		0x400000	/* receive odd nibbles */
#define RX_DESC0_LRS		0x10000000	/* last receive segment */
#define RX_DESC0_FRS		0x20000000	/* first receive segment */
#define RX_DESC0_DMA_OWN	0x80000000
#define RX_DESC1_BUF_SIZE_MASK	0x7FF
#define RX_DESC1_END		0x80000000
#define RX_DESC2_ADDRESS_PHYS	0
#define RX_DESC2_ADDRESS_VIRT	4

#define TX_DESC_NUM		64
#define TX_DESC_NUM_MASK	(TX_DESC_NUM-1)
#define TX_NEXT(N)		(((N) + 1) & (TX_DESC_NUM_MASK))
#define TX_BUF_SIZE		1600
#define TX_BUF_SIZE_MAX		(TX_DESC1_BUF_SIZE_MASK+1)

#define RX_DESC_NUM		64
#define RX_DESC_NUM_MASK	(RX_DESC_NUM-1)
#define RX_NEXT(N)		(((N) + 1) & (RX_DESC_NUM_MASK))
#define RX_BUF_SIZE		1600
#define RX_BUF_SIZE_MAX		(RX_DESC1_BUF_SIZE_MASK+1)

#define REG_INTERRUPT_STATUS	0
#define REG_INTERRUPT_MASK	4
#define REG_MAC_MS_ADDRESS	8
#define REG_MAC_LS_ADDRESS	12
#define REG_MCAST_HASH_TABLE0	16
#define REG_MCAST_HASH_TABLE1	20
#define REG_TX_POLL_DEMAND	24
#define REG_RX_POLL_DEMAND	28
#define REG_TXR_BASE_ADDRESS	32
#define REG_RXR_BASE_ADDRESS	36
#define REG_INT_TIMER_CTRL	40
#define REG_APOLL_TIMER_CTRL	44
#define REG_DMA_BLEN_CTRL	48
#define REG_RESERVED1		52
#define REG_MAC_CTRL		136
#define REG_MAC_STATUS		140
#define REG_PHY_CTRL		144
#define REG_PHY_WRITE_DATA	148
#define REG_FLOW_CTRL		152
#define REG_BACK_PRESSURE	156
#define REG_RESERVED2		160
#define REG_TEST_SEED		196
#define REG_DMA_FIFO_STATE	200
#define REG_TEST_MODE		204
#define REG_RESERVED3		208
#define REG_TX_COL_COUNTER	212
#define REG_RPF_AEP_COUNTER	216
#define REG_XM_PG_COUNTER	220
#define REG_RUNT_TLC_COUNTER	224
#define REG_CRC_FTL_COUNTER	228
#define REG_RLC_RCC_COUNTER	232
#define REG_BROC_COUNTER	236
#define REG_MULCA_COUNTER	240
#define REG_RP_COUNTER		244
#define REG_XP_COUNTER		248

#define REG_PHY_CTRL_OFFSET	0x0
#define REG_PHY_STATUS		0x1
#define REG_PHY_ID1		0x2
#define REG_PHY_ID2		0x3
#define REG_PHY_ANA		0x4
#define REG_PHY_ANLPAR		0x5
#define REG_PHY_ANE		0x6
#define REG_PHY_ECTRL1		0x10
#define REG_PHY_QPDS		0x11
#define REG_PHY_10BOP		0x12
#define REG_PHY_ECTRL2		0x13
#define REG_PHY_FTMAC100_WRITE	0x8000000
#define REG_PHY_FTMAC100_READ	0x4000000

/* REG_INTERRUPT_STATUS */
#define RPKT_FINISH		BIT(0)	/* DMA data received */
#define NORXBUF			BIT(1)	/* receive buffer unavailable */
#define XPKT_FINISH		BIT(2)	/* DMA moved data to TX FIFO */
#define NOTXBUF			BIT(3)	/* transmit buffer unavailable */
#define XPKT_OK_INT_STS		BIT(4)	/* transmit to ethernet success */
#define XPKT_LOST_INT_STS	BIT(5)	/* transmit ethernet lost (collision) */
#define RPKT_SAV		BIT(6)	/* FIFO receive success */
#define RPKT_LOST_INT_STS	BIT(7)	/* FIFO full, receive failed */
#define AHB_ERR			BIT(8)	/* AHB error */
#define PHYSTS_CHG		BIT(9)	/* PHY link status change */

/* REG_INTERRUPT_MASK */
#define RPKT_FINISH_M		BIT(0)
#define NORXBUF_M		BIT(1)
#define XPKT_FINISH_M		BIT(2)
#define NOTXBUF_M		BIT(3)
#define XPKT_OK_M		BIT(4)
#define XPKT_LOST_M		BIT(5)
#define RPKT_SAV_M		BIT(6)
#define RPKT_LOST_M		BIT(7)
#define AHB_ERR_M		BIT(8)
#define PHYSTS_CHG_M		BIT(9)

/* REG_MAC_MS_ADDRESS */
#define MAC_MADR_MASK		0xffff	/* 2 MSB MAC address */

/* REG_INT_TIMER_CTRL */
#define TXINT_TIME_SEL		BIT(15)	/* TX cycle time period */
#define TXINT_THR_MASK		0x7000
#define TXINT_CNT_MASK		0xf00
#define RXINT_TIME_SEL		BIT(7)	/* RX cycle time period */
#define RXINT_THR_MASK		0x70
#define RXINT_CNT_MASK		0xF

/* REG_APOLL_TIMER_CTRL */
#define TXPOLL_TIME_SEL		BIT(12)	/* TX poll time period */
#define TXPOLL_CNT_MASK		0xf00
#define TXPOLL_CNT_SHIFT_BIT	8
#define RXPOLL_TIME_SEL		BIT(4)	/* RX poll time period */
#define RXPOLL_CNT_MASK		0xF
#define RXPOLL_CNT_SHIFT_BIT	0

/* REG_DMA_BLEN_CTRL */
#define RX_THR_EN		BIT(9)	/* RX FIFO threshold arbitration */
#define RXFIFO_HTHR_MASK	0x1c0
#define RXFIFO_LTHR_MASK	0x38
#define INCR16_EN		BIT(2)	/* AHB bus INCR16 burst command */
#define INCR8_EN		BIT(1)	/* AHB bus INCR8 burst command */
#define INCR4_EN		BIT(0)	/* AHB bus INCR4 burst command */

/* REG_MAC_CTRL */
#define RX_BROADPKT		BIT(17)	/* receive broadcast packets */
#define RX_MULTIPKT		BIT(16)	/* receive all multicast packets */
#define FULLDUP			BIT(15)	/* full duplex */
#define CRC_APD			BIT(14)	/* append CRC to transmitted packet */
#define RCV_ALL			BIT(12)	/* ignore incoming packet destination */
#define RX_FTL			BIT(11)	/* accept packets larger than 1518 B */
#define RX_RUNT			BIT(10)	/* accept packets smaller than 64 B */
#define HT_MULTI_EN		BIT(9)	/* accept on hash and mcast pass */
#define RCV_EN			BIT(8)	/* receiver enable */
#define ENRX_IN_HALFTX		BIT(6)	/* enable receive in half duplex mode */
#define XMT_EN			BIT(5)	/* transmit enable */
#define CRC_DIS			BIT(4)	/* disable CRC check when receiving */
#define LOOP_EN			BIT(3)	/* internal loop-back */
#define SW_RST			BIT(2)	/* software reset, last 64 AHB clocks */
#define RDMA_EN			BIT(1)	/* enable receive DMA chan */
#define XDMA_EN			BIT(0)	/* enable transmit DMA chan */

/* REG_MAC_STATUS */
#define COL_EXCEED		BIT(11)	/* more than 16 collisions */
#define LATE_COL		BIT(10)	/* transmit late collision detected */
#define XPKT_LOST		BIT(9)	/* transmit to ethernet lost */
#define XPKT_OK			BIT(8)	/* transmit to ethernet success */
#define RUNT_MAC_STS		BIT(7)	/* receive runt detected */
#define FTL_MAC_STS		BIT(6)	/* receive frame too long detected */
#define CRC_ERR_MAC_STS		BIT(5)
#define RPKT_LOST		BIT(4)	/* RX FIFO full, receive failed */
#define RPKT_SAVE		BIT(3)	/* RX FIFO receive success */
#define COL			BIT(2)	/* collision, incoming packet dropped */
#define MCPU_BROADCAST		BIT(1)
#define MCPU_MULTICAST		BIT(0)

/* REG_PHY_CTRL */
#define MIIWR			BIT(27)	/* init write sequence (auto cleared)*/
#define MIIRD			BIT(26)
#define REGAD_MASK		0x3e00000
#define PHYAD_MASK		0x1f0000
#define MIIRDATA_MASK		0xffff

/* REG_PHY_WRITE_DATA */
#define MIIWDATA_MASK		0xffff

/* REG_FLOW_CTRL */
#define PAUSE_TIME_MASK		0xffff0000
#define FC_HIGH_MASK		0xf000
#define FC_LOW_MASK		0xf00
#define RX_PAUSE		BIT(4)	/* receive pause frame */
#define TX_PAUSED		BIT(3)	/* transmit pause due to receive */
#define FCTHR_EN		BIT(2)	/* enable threshold mode. */
#define TX_PAUSE		BIT(1)	/* transmit pause frame */
#define FC_EN			BIT(0)	/* flow control mode enable */

/* REG_BACK_PRESSURE */
#define BACKP_LOW_MASK		0xf00
#define BACKP_JAM_LEN_MASK	0xf0
#define BACKP_MODE		BIT(1)	/* address mode */
#define BACKP_ENABLE		BIT(0)

/* REG_TEST_SEED */
#define TEST_SEED_MASK		0x3fff

/* REG_DMA_FIFO_STATE */
#define TX_DMA_REQUEST		BIT(31)
#define RX_DMA_REQUEST		BIT(30)
#define TX_DMA_GRANT		BIT(29)
#define RX_DMA_GRANT		BIT(28)
#define TX_FIFO_EMPTY		BIT(27)
#define RX_FIFO_EMPTY		BIT(26)
#define TX_DMA2_SM_MASK		0x7000
#define TX_DMA1_SM_MASK		0xf00
#define RX_DMA2_SM_MASK		0x70
#define RX_DMA1_SM_MASK		0xF

/* REG_TEST_MODE */
#define SINGLE_PKT		BIT(26)	/* single packet mode */
#define PTIMER_TEST		BIT(25)	/* automatic polling timer test mode */
#define ITIMER_TEST		BIT(24)	/* interrupt timer test mode */
#define TEST_SEED_SELECT	BIT(22)
#define SEED_SELECT		BIT(21)
#define TEST_MODE		BIT(20)
#define TEST_TIME_MASK		0xffc00
#define TEST_EXCEL_MASK		0x3e0

/* REG_TX_COL_COUNTER */
#define TX_MCOL_MASK		0xffff0000
#define TX_MCOL_SHIFT_BIT	16
#define TX_SCOL_MASK		0xffff
#define TX_SCOL_SHIFT_BIT	0

/* REG_RPF_AEP_COUNTER */
#define RPF_MASK		0xffff0000
#define RPF_SHIFT_BIT		16
#define AEP_MASK		0xffff
#define AEP_SHIFT_BIT		0

/* REG_XM_PG_COUNTER */
#define XM_MASK			0xffff0000
#define XM_SHIFT_BIT		16
#define PG_MASK			0xffff
#define PG_SHIFT_BIT		0

/* REG_RUNT_TLC_COUNTER */
#define RUNT_CNT_MASK		0xffff0000
#define RUNT_CNT_SHIFT_BIT	16
#define TLCC_MASK		0xffff
#define TLCC_SHIFT_BIT		0

/* REG_CRC_FTL_COUNTER */
#define CRCER_CNT_MASK		0xffff0000
#define CRCER_CNT_SHIFT_BIT	16
#define FTL_CNT_MASK		0xffff
#define FTL_CNT_SHIFT_BIT	0

/* REG_RLC_RCC_COUNTER */
#define RLC_MASK		0xffff0000
#define RLC_SHIFT_BIT		16
#define RCC_MASK		0xffff
#define RCC_SHIFT_BIT		0

/* REG_PHY_STATUS */
#define AN_COMPLETE		0x20
#define LINK_STATUS		0x4

struct moxart_mac_priv_t {
	void __iomem *base;
	struct net_device_stats stats;
	unsigned int reg_maccr;
	unsigned int reg_imr;
	struct napi_struct napi;
	struct net_device *ndev;

	dma_addr_t rx_base;
	dma_addr_t rx_mapping[RX_DESC_NUM];
	void __iomem *rx_desc_base;
	unsigned char *rx_buf_base;
	unsigned char *rx_buf[RX_DESC_NUM];
	unsigned int rx_head;
	unsigned int rx_buf_size;

	dma_addr_t tx_base;
	dma_addr_t tx_mapping[TX_DESC_NUM];
	void __iomem *tx_desc_base;
	unsigned char *tx_buf_base;
	unsigned char *tx_buf[RX_DESC_NUM];
	unsigned int tx_head;
	unsigned int tx_buf_size;

	spinlock_t txlock;
	unsigned int tx_len[TX_DESC_NUM];
	struct sk_buff *tx_skb[TX_DESC_NUM];
	unsigned int tx_tail;
};

#if TX_BUF_SIZE >= TX_BUF_SIZE_MAX
#error MOXA ART Ethernet device driver TX buffer is too large!
#endif
#if RX_BUF_SIZE >= RX_BUF_SIZE_MAX
#error MOXA ART Ethernet device driver RX buffer is too large!
#endif

#endif
