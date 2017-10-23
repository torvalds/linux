/* Copyright (c) 2013-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _EMAC_H_
#define _EMAC_H_

#include <linux/irqreturn.h>
#include <linux/netdevice.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include "emac-mac.h"
#include "emac-phy.h"
#include "emac-sgmii.h"

/* EMAC base register offsets */
#define EMAC_DMA_MAS_CTRL		0x1400
#define EMAC_IRQ_MOD_TIM_INIT		0x1408
#define EMAC_BLK_IDLE_STS		0x140c
#define EMAC_PHY_LINK_DELAY		0x141c
#define EMAC_SYS_ALIV_CTRL		0x1434
#define EMAC_MAC_CTRL			0x1480
#define EMAC_MAC_IPGIFG_CTRL		0x1484
#define EMAC_MAC_STA_ADDR0		0x1488
#define EMAC_MAC_STA_ADDR1		0x148c
#define EMAC_HASH_TAB_REG0		0x1490
#define EMAC_HASH_TAB_REG1		0x1494
#define EMAC_MAC_HALF_DPLX_CTRL		0x1498
#define EMAC_MAX_FRAM_LEN_CTRL		0x149c
#define EMAC_WOL_CTRL0			0x14a0
#define EMAC_RSS_KEY0			0x14b0
#define EMAC_H1TPD_BASE_ADDR_LO		0x14e0
#define EMAC_H2TPD_BASE_ADDR_LO		0x14e4
#define EMAC_H3TPD_BASE_ADDR_LO		0x14e8
#define EMAC_INTER_SRAM_PART9		0x1534
#define EMAC_DESC_CTRL_0		0x1540
#define EMAC_DESC_CTRL_1		0x1544
#define EMAC_DESC_CTRL_2		0x1550
#define EMAC_DESC_CTRL_10		0x1554
#define EMAC_DESC_CTRL_12		0x1558
#define EMAC_DESC_CTRL_13		0x155c
#define EMAC_DESC_CTRL_3		0x1560
#define EMAC_DESC_CTRL_4		0x1564
#define EMAC_DESC_CTRL_5		0x1568
#define EMAC_DESC_CTRL_14		0x156c
#define EMAC_DESC_CTRL_15		0x1570
#define EMAC_DESC_CTRL_16		0x1574
#define EMAC_DESC_CTRL_6		0x1578
#define EMAC_DESC_CTRL_8		0x1580
#define EMAC_DESC_CTRL_9		0x1584
#define EMAC_DESC_CTRL_11		0x1588
#define EMAC_TXQ_CTRL_0			0x1590
#define EMAC_TXQ_CTRL_1			0x1594
#define EMAC_TXQ_CTRL_2			0x1598
#define EMAC_RXQ_CTRL_0			0x15a0
#define EMAC_RXQ_CTRL_1			0x15a4
#define EMAC_RXQ_CTRL_2			0x15a8
#define EMAC_RXQ_CTRL_3			0x15ac
#define EMAC_BASE_CPU_NUMBER		0x15b8
#define EMAC_DMA_CTRL			0x15c0
#define EMAC_MAILBOX_0			0x15e0
#define EMAC_MAILBOX_5			0x15e4
#define EMAC_MAILBOX_6			0x15e8
#define EMAC_MAILBOX_13			0x15ec
#define EMAC_MAILBOX_2			0x15f4
#define EMAC_MAILBOX_3			0x15f8
#define EMAC_INT_STATUS			0x1600
#define EMAC_INT_MASK			0x1604
#define EMAC_MAILBOX_11			0x160c
#define EMAC_AXI_MAST_CTRL		0x1610
#define EMAC_MAILBOX_12			0x1614
#define EMAC_MAILBOX_9			0x1618
#define EMAC_MAILBOX_10			0x161c
#define EMAC_ATHR_HEADER_CTRL		0x1620
#define EMAC_RXMAC_STATC_REG0		0x1700
#define EMAC_RXMAC_STATC_REG22		0x1758
#define EMAC_TXMAC_STATC_REG0		0x1760
#define EMAC_TXMAC_STATC_REG24		0x17c0
#define EMAC_CLK_GATE_CTRL		0x1814
#define EMAC_CORE_HW_VERSION		0x1974
#define EMAC_MISC_CTRL			0x1990
#define EMAC_MAILBOX_7			0x19e0
#define EMAC_MAILBOX_8			0x19e4
#define EMAC_IDT_TABLE0			0x1b00
#define EMAC_RXMAC_STATC_REG23		0x1bc8
#define EMAC_RXMAC_STATC_REG24		0x1bcc
#define EMAC_TXMAC_STATC_REG25		0x1bd0
#define EMAC_MAILBOX_15			0x1bd4
#define EMAC_MAILBOX_16			0x1bd8
#define EMAC_INT1_MASK			0x1bf0
#define EMAC_INT1_STATUS		0x1bf4
#define EMAC_INT2_MASK			0x1bf8
#define EMAC_INT2_STATUS		0x1bfc
#define EMAC_INT3_MASK			0x1c00
#define EMAC_INT3_STATUS		0x1c04

/* EMAC_DMA_MAS_CTRL */
#define DEV_ID_NUM_BMSK                                     0x7f000000
#define DEV_ID_NUM_SHFT                                             24
#define DEV_REV_NUM_BMSK                                      0xff0000
#define DEV_REV_NUM_SHFT                                            16
#define INT_RD_CLR_EN                                           0x4000
#define IRQ_MODERATOR2_EN                                        0x800
#define IRQ_MODERATOR_EN                                         0x400
#define LPW_CLK_SEL                                               0x80
#define LPW_STATE                                                 0x20
#define LPW_MODE                                                  0x10
#define SOFT_RST                                                   0x1

/* EMAC_IRQ_MOD_TIM_INIT */
#define IRQ_MODERATOR2_INIT_BMSK                            0xffff0000
#define IRQ_MODERATOR2_INIT_SHFT                                    16
#define IRQ_MODERATOR_INIT_BMSK                                 0xffff
#define IRQ_MODERATOR_INIT_SHFT                                      0

/* EMAC_INT_STATUS */
#define DIS_INT                                                BIT(31)
#define PTP_INT                                                BIT(30)
#define RFD4_UR_INT                                            BIT(29)
#define TX_PKT_INT3                                            BIT(26)
#define TX_PKT_INT2                                            BIT(25)
#define TX_PKT_INT1                                            BIT(24)
#define RX_PKT_INT3                                            BIT(19)
#define RX_PKT_INT2                                            BIT(18)
#define RX_PKT_INT1                                            BIT(17)
#define RX_PKT_INT0                                            BIT(16)
#define TX_PKT_INT                                             BIT(15)
#define TXQ_TO_INT                                             BIT(14)
#define GPHY_WAKEUP_INT                                        BIT(13)
#define GPHY_LINK_DOWN_INT                                     BIT(12)
#define GPHY_LINK_UP_INT                                       BIT(11)
#define DMAW_TO_INT                                            BIT(10)
#define DMAR_TO_INT                                             BIT(9)
#define TXF_UR_INT                                              BIT(8)
#define RFD3_UR_INT                                             BIT(7)
#define RFD2_UR_INT                                             BIT(6)
#define RFD1_UR_INT                                             BIT(5)
#define RFD0_UR_INT                                             BIT(4)
#define RXF_OF_INT                                              BIT(3)
#define SW_MAN_INT                                              BIT(2)

/* EMAC_MAILBOX_6 */
#define RFD2_PROC_IDX_BMSK                                   0xfff0000
#define RFD2_PROC_IDX_SHFT                                          16
#define RFD2_PROD_IDX_BMSK                                       0xfff
#define RFD2_PROD_IDX_SHFT                                           0

/* EMAC_CORE_HW_VERSION */
#define MAJOR_BMSK                                          0xf0000000
#define MAJOR_SHFT                                                  28
#define MINOR_BMSK                                           0xfff0000
#define MINOR_SHFT                                                  16
#define STEP_BMSK                                               0xffff
#define STEP_SHFT                                                    0

/* EMAC_EMAC_WRAPPER_CSR1 */
#define TX_INDX_FIFO_SYNC_RST                                  BIT(23)
#define TX_TS_FIFO_SYNC_RST                                    BIT(22)
#define RX_TS_FIFO2_SYNC_RST                                   BIT(21)
#define RX_TS_FIFO1_SYNC_RST                                   BIT(20)
#define TX_TS_ENABLE                                           BIT(16)
#define DIS_1588_CLKS                                          BIT(11)
#define FREQ_MODE                                               BIT(9)
#define ENABLE_RRD_TIMESTAMP                                    BIT(3)

/* EMAC_EMAC_WRAPPER_CSR2 */
#define HDRIVE_BMSK                                             0x3000
#define HDRIVE_SHFT                                                 12
#define SLB_EN                                                  BIT(9)
#define PLB_EN                                                  BIT(8)
#define WOL_EN                                                  BIT(3)
#define PHY_RESET                                               BIT(0)

#define EMAC_DEV_ID                                             0x0040

/* SGMII v2 per lane registers */
#define SGMII_LN_RSM_START             0x029C

/* SGMII v2 PHY common registers */
#define SGMII_PHY_CMN_CTRL            0x0408
#define SGMII_PHY_CMN_RESET_CTRL      0x0410

/* SGMII v2 PHY registers per lane */
#define SGMII_PHY_LN_OFFSET          0x0400
#define SGMII_PHY_LN_LANE_STATUS     0x00DC
#define SGMII_PHY_LN_BIST_GEN0       0x008C
#define SGMII_PHY_LN_BIST_GEN1       0x0090
#define SGMII_PHY_LN_BIST_GEN2       0x0094
#define SGMII_PHY_LN_BIST_GEN3       0x0098
#define SGMII_PHY_LN_CDR_CTRL1       0x005C

enum emac_clk_id {
	EMAC_CLK_AXI,
	EMAC_CLK_CFG_AHB,
	EMAC_CLK_HIGH_SPEED,
	EMAC_CLK_MDIO,
	EMAC_CLK_TX,
	EMAC_CLK_RX,
	EMAC_CLK_SYS,
	EMAC_CLK_CNT
};

#define EMAC_LINK_SPEED_UNKNOWN                                    0x0
#define EMAC_LINK_SPEED_10_HALF                                 BIT(0)
#define EMAC_LINK_SPEED_10_FULL                                 BIT(1)
#define EMAC_LINK_SPEED_100_HALF                                BIT(2)
#define EMAC_LINK_SPEED_100_FULL                                BIT(3)
#define EMAC_LINK_SPEED_1GB_FULL                                BIT(5)

#define EMAC_MAX_SETUP_LNK_CYCLE                                   100

struct emac_stats {
	/* rx */
	u64 rx_ok;              /* good packets */
	u64 rx_bcast;           /* good broadcast packets */
	u64 rx_mcast;           /* good multicast packets */
	u64 rx_pause;           /* pause packet */
	u64 rx_ctrl;            /* control packets other than pause frame. */
	u64 rx_fcs_err;         /* packets with bad FCS. */
	u64 rx_len_err;         /* packets with length mismatch */
	u64 rx_byte_cnt;        /* good bytes count (without FCS) */
	u64 rx_runt;            /* runt packets */
	u64 rx_frag;            /* fragment count */
	u64 rx_sz_64;	        /* packets that are 64 bytes */
	u64 rx_sz_65_127;       /* packets that are 65-127 bytes */
	u64 rx_sz_128_255;      /* packets that are 128-255 bytes */
	u64 rx_sz_256_511;      /* packets that are 256-511 bytes */
	u64 rx_sz_512_1023;     /* packets that are 512-1023 bytes */
	u64 rx_sz_1024_1518;    /* packets that are 1024-1518 bytes */
	u64 rx_sz_1519_max;     /* packets that are 1519-MTU bytes*/
	u64 rx_sz_ov;           /* packets that are >MTU bytes (truncated) */
	u64 rx_rxf_ov;          /* packets dropped due to RX FIFO overflow */
	u64 rx_align_err;       /* alignment errors */
	u64 rx_bcast_byte_cnt;  /* broadcast packets byte count (without FCS) */
	u64 rx_mcast_byte_cnt;  /* multicast packets byte count (without FCS) */
	u64 rx_err_addr;        /* packets dropped due to address filtering */
	u64 rx_crc_align;       /* CRC align errors */
	u64 rx_jabbers;         /* jabbers */

	/* tx */
	u64 tx_ok;              /* good packets */
	u64 tx_bcast;           /* good broadcast packets */
	u64 tx_mcast;           /* good multicast packets */
	u64 tx_pause;           /* pause packets */
	u64 tx_exc_defer;       /* packets with excessive deferral */
	u64 tx_ctrl;            /* control packets other than pause frame */
	u64 tx_defer;           /* packets that are deferred. */
	u64 tx_byte_cnt;        /* good bytes count (without FCS) */
	u64 tx_sz_64;           /* packets that are 64 bytes */
	u64 tx_sz_65_127;       /* packets that are 65-127 bytes */
	u64 tx_sz_128_255;      /* packets that are 128-255 bytes */
	u64 tx_sz_256_511;      /* packets that are 256-511 bytes */
	u64 tx_sz_512_1023;     /* packets that are 512-1023 bytes */
	u64 tx_sz_1024_1518;    /* packets that are 1024-1518 bytes */
	u64 tx_sz_1519_max;     /* packets that are 1519-MTU bytes */
	u64 tx_1_col;           /* packets single prior collision */
	u64 tx_2_col;           /* packets with multiple prior collisions */
	u64 tx_late_col;        /* packets with late collisions */
	u64 tx_abort_col;       /* packets aborted due to excess collisions */
	u64 tx_underrun;        /* packets aborted due to FIFO underrun */
	u64 tx_rd_eop;          /* count of reads beyond EOP */
	u64 tx_len_err;         /* packets with length mismatch */
	u64 tx_trunc;           /* packets truncated due to size >MTU */
	u64 tx_bcast_byte;      /* broadcast packets byte count (without FCS) */
	u64 tx_mcast_byte;      /* multicast packets byte count (without FCS) */
	u64 tx_col;             /* collisions */

	spinlock_t lock;	/* prevent multiple simultaneous readers */
};

/* RSS hstype Definitions */
#define EMAC_RSS_HSTYP_IPV4_EN				    0x00000001
#define EMAC_RSS_HSTYP_TCP4_EN				    0x00000002
#define EMAC_RSS_HSTYP_IPV6_EN				    0x00000004
#define EMAC_RSS_HSTYP_TCP6_EN				    0x00000008
#define EMAC_RSS_HSTYP_ALL_EN (\
		EMAC_RSS_HSTYP_IPV4_EN   |\
		EMAC_RSS_HSTYP_TCP4_EN   |\
		EMAC_RSS_HSTYP_IPV6_EN   |\
		EMAC_RSS_HSTYP_TCP6_EN)

#define EMAC_VLAN_TO_TAG(_vlan, _tag) \
		(_tag =  ((((_vlan) >> 8) & 0xFF) | (((_vlan) & 0xFF) << 8)))

#define EMAC_TAG_TO_VLAN(_tag, _vlan) \
		(_vlan = ((((_tag) >> 8) & 0xFF) | (((_tag) & 0xFF) << 8)))

#define EMAC_DEF_RX_BUF_SIZE					  1536
#define EMAC_MAX_JUMBO_PKT_SIZE				    (9 * 1024)
#define EMAC_MAX_TX_OFFLOAD_THRESH			    (9 * 1024)

#define EMAC_MAX_ETH_FRAME_SIZE		       EMAC_MAX_JUMBO_PKT_SIZE
#define EMAC_MIN_ETH_FRAME_SIZE					    68

#define EMAC_DEF_TX_QUEUES					     1
#define EMAC_DEF_RX_QUEUES					     1

#define EMAC_MIN_TX_DESCS					   128
#define EMAC_MIN_RX_DESCS					   128

#define EMAC_MAX_TX_DESCS					 16383
#define EMAC_MAX_RX_DESCS					  2047

#define EMAC_DEF_TX_DESCS					   512
#define EMAC_DEF_RX_DESCS					   256

#define EMAC_DEF_RX_IRQ_MOD					   250
#define EMAC_DEF_TX_IRQ_MOD					   250

#define EMAC_WATCHDOG_TIME				      (5 * HZ)

/* by default check link every 4 seconds */
#define EMAC_TRY_LINK_TIMEOUT				      (4 * HZ)

/* emac_irq per-device (per-adapter) irq properties.
 * @irq:	irq number.
 * @mask	mask to use over status register.
 */
struct emac_irq {
	unsigned int	irq;
	u32		mask;
};

/* The device's main data structure */
struct emac_adapter {
	struct net_device		*netdev;
	struct mii_bus			*mii_bus;
	struct phy_device		*phydev;

	void __iomem			*base;
	void __iomem			*csr;

	struct emac_sgmii		phy;
	struct emac_stats		stats;

	struct emac_irq			irq;
	struct clk			*clk[EMAC_CLK_CNT];

	/* All Descriptor memory */
	struct emac_ring_header		ring_header;
	struct emac_tx_queue		tx_q;
	struct emac_rx_queue		rx_q;
	unsigned int			tx_desc_cnt;
	unsigned int			rx_desc_cnt;
	unsigned int			rrd_size; /* in quad words */
	unsigned int			rfd_size; /* in quad words */
	unsigned int			tpd_size; /* in quad words */

	unsigned int			rxbuf_size;

	/* Flow control / pause frames support. If automatic=True, do whatever
	 * the PHY does. Otherwise, use tx_flow_control and rx_flow_control.
	 */
	bool				automatic;
	bool				tx_flow_control;
	bool				rx_flow_control;

	/* True == use single-pause-frame mode. */
	bool				single_pause_mode;

	/* Ring parameter */
	u8				tpd_burst;
	u8				rfd_burst;
	unsigned int			dmaw_dly_cnt;
	unsigned int			dmar_dly_cnt;
	enum emac_dma_req_block		dmar_block;
	enum emac_dma_req_block		dmaw_block;
	enum emac_dma_order		dma_order;

	u32				irq_mod;
	u32				preamble;

	struct work_struct		work_thread;

	u16				msg_enable;

	struct mutex			reset_lock;
};

int emac_reinit_locked(struct emac_adapter *adpt);
void emac_reg_update32(void __iomem *addr, u32 mask, u32 val);

void emac_set_ethtool_ops(struct net_device *netdev);
void emac_update_hw_stats(struct emac_adapter *adpt);

#endif /* _EMAC_H_ */
