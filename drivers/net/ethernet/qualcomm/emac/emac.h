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

/* EMAC base register offsets */
#define EMAC_DMA_MAS_CTRL                                     0x001400
#define EMAC_IRQ_MOD_TIM_INIT                                 0x001408
#define EMAC_BLK_IDLE_STS                                     0x00140c
#define EMAC_PHY_LINK_DELAY                                   0x00141c
#define EMAC_SYS_ALIV_CTRL                                    0x001434
#define EMAC_MAC_IPGIFG_CTRL                                  0x001484
#define EMAC_MAC_STA_ADDR0                                    0x001488
#define EMAC_MAC_STA_ADDR1                                    0x00148c
#define EMAC_HASH_TAB_REG0                                    0x001490
#define EMAC_HASH_TAB_REG1                                    0x001494
#define EMAC_MAC_HALF_DPLX_CTRL                               0x001498
#define EMAC_MAX_FRAM_LEN_CTRL                                0x00149c
#define EMAC_INT_STATUS                                       0x001600
#define EMAC_INT_MASK                                         0x001604
#define EMAC_RXMAC_STATC_REG0                                 0x001700
#define EMAC_RXMAC_STATC_REG22                                0x001758
#define EMAC_TXMAC_STATC_REG0                                 0x001760
#define EMAC_TXMAC_STATC_REG24                                0x0017c0
#define EMAC_CORE_HW_VERSION                                  0x001974
#define EMAC_IDT_TABLE0                                       0x001b00
#define EMAC_RXMAC_STATC_REG23                                0x001bc8
#define EMAC_RXMAC_STATC_REG24                                0x001bcc
#define EMAC_TXMAC_STATC_REG25                                0x001bd0
#define EMAC_INT1_MASK                                        0x001bf0
#define EMAC_INT1_STATUS                                      0x001bf4
#define EMAC_INT2_MASK                                        0x001bf8
#define EMAC_INT2_STATUS                                      0x001bfc
#define EMAC_INT3_MASK                                        0x001c00
#define EMAC_INT3_STATUS                                      0x001c04

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

/* Wake On Lan */
#define EMAC_WOL_PHY                     0x00000001 /* PHY Status Change */
#define EMAC_WOL_MAGIC                   0x00000002 /* Magic Packet */

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

	struct emac_phy			phy;
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
irqreturn_t emac_isr(int irq, void *data);

#endif /* _EMAC_H_ */
