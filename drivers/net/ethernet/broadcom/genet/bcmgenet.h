/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BCMGENET_H__
#define __BCMGENET_H__

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/phy.h>

/* total number of Buffer Descriptors, same for Rx/Tx */
#define TOTAL_DESC				256

/* which ring is descriptor based */
#define DESC_INDEX				16

/* Body(1500) + EH_SIZE(14) + VLANTAG(4) + BRCMTAG(6) + FCS(4) = 1528.
 * 1536 is multiple of 256 bytes
 */
#define ENET_BRCM_TAG_LEN	6
#define ENET_PAD		8
#define ENET_MAX_MTU_SIZE	(ETH_DATA_LEN + ETH_HLEN + VLAN_HLEN + \
				 ENET_BRCM_TAG_LEN + ETH_FCS_LEN + ENET_PAD)
#define DMA_MAX_BURST_LENGTH    0x10

/* misc. configuration */
#define CLEAR_ALL_HFB			0xFF
#define DMA_FC_THRESH_HI		(TOTAL_DESC >> 4)
#define DMA_FC_THRESH_LO		5

/* 64B receive/transmit status block */
struct status_64 {
	u32	length_status;		/* length and peripheral status */
	u32	ext_status;		/* Extended status*/
	u32	rx_csum;		/* partial rx checksum */
	u32	unused1[9];		/* unused */
	u32	tx_csum_info;		/* Tx checksum info. */
	u32	unused2[3];		/* unused */
};

/* Rx status bits */
#define STATUS_RX_EXT_MASK		0x1FFFFF
#define STATUS_RX_CSUM_MASK		0xFFFF
#define STATUS_RX_CSUM_OK		0x10000
#define STATUS_RX_CSUM_FR		0x20000
#define STATUS_RX_PROTO_TCP		0
#define STATUS_RX_PROTO_UDP		1
#define STATUS_RX_PROTO_ICMP		2
#define STATUS_RX_PROTO_OTHER		3
#define STATUS_RX_PROTO_MASK		3
#define STATUS_RX_PROTO_SHIFT		18
#define STATUS_FILTER_INDEX_MASK	0xFFFF
/* Tx status bits */
#define STATUS_TX_CSUM_START_MASK	0X7FFF
#define STATUS_TX_CSUM_START_SHIFT	16
#define STATUS_TX_CSUM_PROTO_UDP	0x8000
#define STATUS_TX_CSUM_OFFSET_MASK	0x7FFF
#define STATUS_TX_CSUM_LV		0x80000000

/* DMA Descriptor */
#define DMA_DESC_LENGTH_STATUS	0x00	/* in bytes of data in buffer */
#define DMA_DESC_ADDRESS_LO	0x04	/* lower bits of PA */
#define DMA_DESC_ADDRESS_HI	0x08	/* upper 32 bits of PA, GENETv4+ */

/* Rx/Tx common counter group */
struct bcmgenet_pkt_counters {
	u32	cnt_64;		/* RO Received/Transmited 64 bytes packet */
	u32	cnt_127;	/* RO Rx/Tx 127 bytes packet */
	u32	cnt_255;	/* RO Rx/Tx 65-255 bytes packet */
	u32	cnt_511;	/* RO Rx/Tx 256-511 bytes packet */
	u32	cnt_1023;	/* RO Rx/Tx 512-1023 bytes packet */
	u32	cnt_1518;	/* RO Rx/Tx 1024-1518 bytes packet */
	u32	cnt_mgv;	/* RO Rx/Tx 1519-1522 good VLAN packet */
	u32	cnt_2047;	/* RO Rx/Tx 1522-2047 bytes packet*/
	u32	cnt_4095;	/* RO Rx/Tx 2048-4095 bytes packet*/
	u32	cnt_9216;	/* RO Rx/Tx 4096-9216 bytes packet*/
};

/* RSV, Receive Status Vector */
struct bcmgenet_rx_counters {
	struct  bcmgenet_pkt_counters pkt_cnt;
	u32	pkt;		/* RO (0x428) Received pkt count*/
	u32	bytes;		/* RO Received byte count */
	u32	mca;		/* RO # of Received multicast pkt */
	u32	bca;		/* RO # of Receive broadcast pkt */
	u32	fcs;		/* RO # of Received FCS error  */
	u32	cf;		/* RO # of Received control frame pkt*/
	u32	pf;		/* RO # of Received pause frame pkt */
	u32	uo;		/* RO # of unknown op code pkt */
	u32	aln;		/* RO # of alignment error count */
	u32	flr;		/* RO # of frame length out of range count */
	u32	cde;		/* RO # of code error pkt */
	u32	fcr;		/* RO # of carrier sense error pkt */
	u32	ovr;		/* RO # of oversize pkt*/
	u32	jbr;		/* RO # of jabber count */
	u32	mtue;		/* RO # of MTU error pkt*/
	u32	pok;		/* RO # of Received good pkt */
	u32	uc;		/* RO # of unicast pkt */
	u32	ppp;		/* RO # of PPP pkt */
	u32	rcrc;		/* RO (0x470),# of CRC match pkt */
};

/* TSV, Transmit Status Vector */
struct bcmgenet_tx_counters {
	struct bcmgenet_pkt_counters pkt_cnt;
	u32	pkts;		/* RO (0x4a8) Transmited pkt */
	u32	mca;		/* RO # of xmited multicast pkt */
	u32	bca;		/* RO # of xmited broadcast pkt */
	u32	pf;		/* RO # of xmited pause frame count */
	u32	cf;		/* RO # of xmited control frame count */
	u32	fcs;		/* RO # of xmited FCS error count */
	u32	ovr;		/* RO # of xmited oversize pkt */
	u32	drf;		/* RO # of xmited deferral pkt */
	u32	edf;		/* RO # of xmited Excessive deferral pkt*/
	u32	scl;		/* RO # of xmited single collision pkt */
	u32	mcl;		/* RO # of xmited multiple collision pkt*/
	u32	lcl;		/* RO # of xmited late collision pkt */
	u32	ecl;		/* RO # of xmited excessive collision pkt*/
	u32	frg;		/* RO # of xmited fragments pkt*/
	u32	ncl;		/* RO # of xmited total collision count */
	u32	jbr;		/* RO # of xmited jabber count*/
	u32	bytes;		/* RO # of xmited byte count */
	u32	pok;		/* RO # of xmited good pkt */
	u32	uc;		/* RO (0x0x4f0)# of xmited unitcast pkt */
};

struct bcmgenet_mib_counters {
	struct bcmgenet_rx_counters rx;
	struct bcmgenet_tx_counters tx;
	u32	rx_runt_cnt;
	u32	rx_runt_fcs;
	u32	rx_runt_fcs_align;
	u32	rx_runt_bytes;
	u32	rbuf_ovflow_cnt;
	u32	rbuf_err_cnt;
	u32	mdf_err_cnt;
	u32	alloc_rx_buff_failed;
	u32	rx_dma_failed;
	u32	tx_dma_failed;
};

#define UMAC_HD_BKP_CTRL		0x004
#define	 HD_FC_EN			(1 << 0)
#define  HD_FC_BKOFF_OK			(1 << 1)
#define  IPG_CONFIG_RX_SHIFT		2
#define  IPG_CONFIG_RX_MASK		0x1F

#define UMAC_CMD			0x008
#define  CMD_TX_EN			(1 << 0)
#define  CMD_RX_EN			(1 << 1)
#define  UMAC_SPEED_10			0
#define  UMAC_SPEED_100			1
#define  UMAC_SPEED_1000		2
#define  UMAC_SPEED_2500		3
#define  CMD_SPEED_SHIFT		2
#define  CMD_SPEED_MASK			3
#define  CMD_PROMISC			(1 << 4)
#define  CMD_PAD_EN			(1 << 5)
#define  CMD_CRC_FWD			(1 << 6)
#define  CMD_PAUSE_FWD			(1 << 7)
#define  CMD_RX_PAUSE_IGNORE		(1 << 8)
#define  CMD_TX_ADDR_INS		(1 << 9)
#define  CMD_HD_EN			(1 << 10)
#define  CMD_SW_RESET			(1 << 13)
#define  CMD_LCL_LOOP_EN		(1 << 15)
#define  CMD_AUTO_CONFIG		(1 << 22)
#define  CMD_CNTL_FRM_EN		(1 << 23)
#define  CMD_NO_LEN_CHK			(1 << 24)
#define  CMD_RMT_LOOP_EN		(1 << 25)
#define  CMD_PRBL_EN			(1 << 27)
#define  CMD_TX_PAUSE_IGNORE		(1 << 28)
#define  CMD_TX_RX_EN			(1 << 29)
#define  CMD_RUNT_FILTER_DIS		(1 << 30)

#define UMAC_MAC0			0x00C
#define UMAC_MAC1			0x010
#define UMAC_MAX_FRAME_LEN		0x014

#define UMAC_EEE_CTRL			0x064
#define  EN_LPI_RX_PAUSE		(1 << 0)
#define  EN_LPI_TX_PFC			(1 << 1)
#define  EN_LPI_TX_PAUSE		(1 << 2)
#define  EEE_EN				(1 << 3)
#define  RX_FIFO_CHECK			(1 << 4)
#define  EEE_TX_CLK_DIS			(1 << 5)
#define  DIS_EEE_10M			(1 << 6)
#define  LP_IDLE_PREDICTION_MODE	(1 << 7)

#define UMAC_EEE_LPI_TIMER		0x068
#define UMAC_EEE_WAKE_TIMER		0x06C
#define UMAC_EEE_REF_COUNT		0x070
#define  EEE_REFERENCE_COUNT_MASK	0xffff

#define UMAC_TX_FLUSH			0x334

#define UMAC_MIB_START			0x400

#define UMAC_MDIO_CMD			0x614
#define  MDIO_START_BUSY		(1 << 29)
#define  MDIO_READ_FAIL			(1 << 28)
#define  MDIO_RD			(2 << 26)
#define  MDIO_WR			(1 << 26)
#define  MDIO_PMD_SHIFT			21
#define  MDIO_PMD_MASK			0x1F
#define  MDIO_REG_SHIFT			16
#define  MDIO_REG_MASK			0x1F

#define UMAC_RBUF_OVFL_CNT		0x61C

#define UMAC_MPD_CTRL			0x620
#define  MPD_EN				(1 << 0)
#define  MPD_PW_EN			(1 << 27)
#define  MPD_MSEQ_LEN_SHIFT		16
#define  MPD_MSEQ_LEN_MASK		0xFF

#define UMAC_MPD_PW_MS			0x624
#define UMAC_MPD_PW_LS			0x628
#define UMAC_RBUF_ERR_CNT		0x634
#define UMAC_MDF_ERR_CNT		0x638
#define UMAC_MDF_CTRL			0x650
#define UMAC_MDF_ADDR			0x654
#define UMAC_MIB_CTRL			0x580
#define  MIB_RESET_RX			(1 << 0)
#define  MIB_RESET_RUNT			(1 << 1)
#define  MIB_RESET_TX			(1 << 2)

#define RBUF_CTRL			0x00
#define  RBUF_64B_EN			(1 << 0)
#define  RBUF_ALIGN_2B			(1 << 1)
#define  RBUF_BAD_DIS			(1 << 2)

#define RBUF_STATUS			0x0C
#define  RBUF_STATUS_WOL		(1 << 0)
#define  RBUF_STATUS_MPD_INTR_ACTIVE	(1 << 1)
#define  RBUF_STATUS_ACPI_INTR_ACTIVE	(1 << 2)

#define RBUF_CHK_CTRL			0x14
#define  RBUF_RXCHK_EN			(1 << 0)
#define  RBUF_SKIP_FCS			(1 << 4)

#define RBUF_ENERGY_CTRL		0x9c
#define  RBUF_EEE_EN			(1 << 0)
#define  RBUF_PM_EN			(1 << 1)

#define RBUF_TBUF_SIZE_CTRL		0xb4

#define RBUF_HFB_CTRL_V1		0x38
#define  RBUF_HFB_FILTER_EN_SHIFT	16
#define  RBUF_HFB_FILTER_EN_MASK	0xffff0000
#define  RBUF_HFB_EN			(1 << 0)
#define  RBUF_HFB_256B			(1 << 1)
#define  RBUF_ACPI_EN			(1 << 2)

#define RBUF_HFB_LEN_V1			0x3C
#define  RBUF_FLTR_LEN_MASK		0xFF
#define  RBUF_FLTR_LEN_SHIFT		8

#define TBUF_CTRL			0x00
#define TBUF_BP_MC			0x0C
#define TBUF_ENERGY_CTRL		0x14
#define  TBUF_EEE_EN			(1 << 0)
#define  TBUF_PM_EN			(1 << 1)

#define TBUF_CTRL_V1			0x80
#define TBUF_BP_MC_V1			0xA0

#define HFB_CTRL			0x00
#define HFB_FLT_ENABLE_V3PLUS		0x04
#define HFB_FLT_LEN_V2			0x04
#define HFB_FLT_LEN_V3PLUS		0x1C

/* uniMac intrl2 registers */
#define INTRL2_CPU_STAT			0x00
#define INTRL2_CPU_SET			0x04
#define INTRL2_CPU_CLEAR		0x08
#define INTRL2_CPU_MASK_STATUS		0x0C
#define INTRL2_CPU_MASK_SET		0x10
#define INTRL2_CPU_MASK_CLEAR		0x14

/* INTRL2 instance 0 definitions */
#define UMAC_IRQ_SCB			(1 << 0)
#define UMAC_IRQ_EPHY			(1 << 1)
#define UMAC_IRQ_PHY_DET_R		(1 << 2)
#define UMAC_IRQ_PHY_DET_F		(1 << 3)
#define UMAC_IRQ_LINK_UP		(1 << 4)
#define UMAC_IRQ_LINK_DOWN		(1 << 5)
#define UMAC_IRQ_UMAC			(1 << 6)
#define UMAC_IRQ_UMAC_TSV		(1 << 7)
#define UMAC_IRQ_TBUF_UNDERRUN		(1 << 8)
#define UMAC_IRQ_RBUF_OVERFLOW		(1 << 9)
#define UMAC_IRQ_HFB_SM			(1 << 10)
#define UMAC_IRQ_HFB_MM			(1 << 11)
#define UMAC_IRQ_MPD_R			(1 << 12)
#define UMAC_IRQ_RXDMA_MBDONE		(1 << 13)
#define UMAC_IRQ_RXDMA_PDONE		(1 << 14)
#define UMAC_IRQ_RXDMA_BDONE		(1 << 15)
#define UMAC_IRQ_TXDMA_MBDONE		(1 << 16)
#define UMAC_IRQ_TXDMA_PDONE		(1 << 17)
#define UMAC_IRQ_TXDMA_BDONE		(1 << 18)
/* Only valid for GENETv3+ */
#define UMAC_IRQ_MDIO_DONE		(1 << 23)
#define UMAC_IRQ_MDIO_ERROR		(1 << 24)

/* Register block offsets */
#define GENET_SYS_OFF			0x0000
#define GENET_GR_BRIDGE_OFF		0x0040
#define GENET_EXT_OFF			0x0080
#define GENET_INTRL2_0_OFF		0x0200
#define GENET_INTRL2_1_OFF		0x0240
#define GENET_RBUF_OFF			0x0300
#define GENET_UMAC_OFF			0x0800

/* SYS block offsets and register definitions */
#define SYS_REV_CTRL			0x00
#define SYS_PORT_CTRL			0x04
#define  PORT_MODE_INT_EPHY		0
#define  PORT_MODE_INT_GPHY		1
#define  PORT_MODE_EXT_EPHY		2
#define  PORT_MODE_EXT_GPHY		3
#define  PORT_MODE_EXT_RVMII_25		(4 | BIT(4))
#define  PORT_MODE_EXT_RVMII_50		4
#define  LED_ACT_SOURCE_MAC		(1 << 9)

#define SYS_RBUF_FLUSH_CTRL		0x08
#define SYS_TBUF_FLUSH_CTRL		0x0C
#define RBUF_FLUSH_CTRL_V1		0x04

/* Ext block register offsets and definitions */
#define EXT_EXT_PWR_MGMT		0x00
#define  EXT_PWR_DOWN_BIAS		(1 << 0)
#define  EXT_PWR_DOWN_DLL		(1 << 1)
#define  EXT_PWR_DOWN_PHY		(1 << 2)
#define  EXT_PWR_DN_EN_LD		(1 << 3)
#define  EXT_ENERGY_DET			(1 << 4)
#define  EXT_IDDQ_FROM_PHY		(1 << 5)
#define  EXT_PHY_RESET			(1 << 8)
#define  EXT_ENERGY_DET_MASK		(1 << 12)

#define EXT_RGMII_OOB_CTRL		0x0C
#define  RGMII_LINK			(1 << 4)
#define  OOB_DISABLE			(1 << 5)
#define  RGMII_MODE_EN			(1 << 6)
#define  ID_MODE_DIS			(1 << 16)

#define EXT_GPHY_CTRL			0x1C
#define  EXT_CFG_IDDQ_BIAS		(1 << 0)
#define  EXT_CFG_PWR_DOWN		(1 << 1)
#define  EXT_GPHY_RESET			(1 << 5)

/* DMA rings size */
#define DMA_RING_SIZE			(0x40)
#define DMA_RINGS_SIZE			(DMA_RING_SIZE * (DESC_INDEX + 1))

/* DMA registers common definitions */
#define DMA_RW_POINTER_MASK		0x1FF
#define DMA_P_INDEX_DISCARD_CNT_MASK	0xFFFF
#define DMA_P_INDEX_DISCARD_CNT_SHIFT	16
#define DMA_BUFFER_DONE_CNT_MASK	0xFFFF
#define DMA_BUFFER_DONE_CNT_SHIFT	16
#define DMA_P_INDEX_MASK		0xFFFF
#define DMA_C_INDEX_MASK		0xFFFF

/* DMA ring size register */
#define DMA_RING_SIZE_MASK		0xFFFF
#define DMA_RING_SIZE_SHIFT		16
#define DMA_RING_BUFFER_SIZE_MASK	0xFFFF

/* DMA interrupt threshold register */
#define DMA_INTR_THRESHOLD_MASK		0x00FF

/* DMA XON/XOFF register */
#define DMA_XON_THREHOLD_MASK		0xFFFF
#define DMA_XOFF_THRESHOLD_MASK		0xFFFF
#define DMA_XOFF_THRESHOLD_SHIFT	16

/* DMA flow period register */
#define DMA_FLOW_PERIOD_MASK		0xFFFF
#define DMA_MAX_PKT_SIZE_MASK		0xFFFF
#define DMA_MAX_PKT_SIZE_SHIFT		16


/* DMA control register */
#define DMA_EN				(1 << 0)
#define DMA_RING_BUF_EN_SHIFT		0x01
#define DMA_RING_BUF_EN_MASK		0xFFFF
#define DMA_TSB_SWAP_EN			(1 << 20)

/* DMA status register */
#define DMA_DISABLED			(1 << 0)
#define DMA_DESC_RAM_INIT_BUSY		(1 << 1)

/* DMA SCB burst size register */
#define DMA_SCB_BURST_SIZE_MASK		0x1F

/* DMA activity vector register */
#define DMA_ACTIVITY_VECTOR_MASK	0x1FFFF

/* DMA backpressure mask register */
#define DMA_BACKPRESSURE_MASK		0x1FFFF
#define DMA_PFC_ENABLE			(1 << 31)

/* DMA backpressure status register */
#define DMA_BACKPRESSURE_STATUS_MASK	0x1FFFF

/* DMA override register */
#define DMA_LITTLE_ENDIAN_MODE		(1 << 0)
#define DMA_REGISTER_MODE		(1 << 1)

/* DMA timeout register */
#define DMA_TIMEOUT_MASK		0xFFFF
#define DMA_TIMEOUT_VAL			5000	/* micro seconds */

/* TDMA rate limiting control register */
#define DMA_RATE_LIMIT_EN_MASK		0xFFFF

/* TDMA arbitration control register */
#define DMA_ARBITER_MODE_MASK		0x03
#define DMA_RING_BUF_PRIORITY_MASK	0x1F
#define DMA_RING_BUF_PRIORITY_SHIFT	5
#define DMA_PRIO_REG_INDEX(q)		((q) / 6)
#define DMA_PRIO_REG_SHIFT(q)		(((q) % 6) * DMA_RING_BUF_PRIORITY_SHIFT)
#define DMA_RATE_ADJ_MASK		0xFF

/* Tx/Rx Dma Descriptor common bits*/
#define DMA_BUFLENGTH_MASK		0x0fff
#define DMA_BUFLENGTH_SHIFT		16
#define DMA_OWN				0x8000
#define DMA_EOP				0x4000
#define DMA_SOP				0x2000
#define DMA_WRAP			0x1000
/* Tx specific Dma descriptor bits */
#define DMA_TX_UNDERRUN			0x0200
#define DMA_TX_APPEND_CRC		0x0040
#define DMA_TX_OW_CRC			0x0020
#define DMA_TX_DO_CSUM			0x0010
#define DMA_TX_QTAG_SHIFT		7

/* Rx Specific Dma descriptor bits */
#define DMA_RX_CHK_V3PLUS		0x8000
#define DMA_RX_CHK_V12			0x1000
#define DMA_RX_BRDCAST			0x0040
#define DMA_RX_MULT			0x0020
#define DMA_RX_LG			0x0010
#define DMA_RX_NO			0x0008
#define DMA_RX_RXER			0x0004
#define DMA_RX_CRC_ERROR		0x0002
#define DMA_RX_OV			0x0001
#define DMA_RX_FI_MASK			0x001F
#define DMA_RX_FI_SHIFT			0x0007
#define DMA_DESC_ALLOC_MASK		0x00FF

#define DMA_ARBITER_RR			0x00
#define DMA_ARBITER_WRR			0x01
#define DMA_ARBITER_SP			0x02

struct enet_cb {
	struct sk_buff      *skb;
	void __iomem *bd_addr;
	DEFINE_DMA_UNMAP_ADDR(dma_addr);
	DEFINE_DMA_UNMAP_LEN(dma_len);
};

/* power management mode */
enum bcmgenet_power_mode {
	GENET_POWER_CABLE_SENSE = 0,
	GENET_POWER_PASSIVE,
	GENET_POWER_WOL_MAGIC,
};

struct bcmgenet_priv;

/* We support both runtime GENET detection and compile-time
 * to optimize code-paths for a given hardware
 */
enum bcmgenet_version {
	GENET_V1 = 1,
	GENET_V2,
	GENET_V3,
	GENET_V4
};

#define GENET_IS_V1(p)	((p)->version == GENET_V1)
#define GENET_IS_V2(p)	((p)->version == GENET_V2)
#define GENET_IS_V3(p)	((p)->version == GENET_V3)
#define GENET_IS_V4(p)	((p)->version == GENET_V4)

/* Hardware flags */
#define GENET_HAS_40BITS	(1 << 0)
#define GENET_HAS_EXT		(1 << 1)
#define GENET_HAS_MDIO_INTR	(1 << 2)

/* BCMGENET hardware parameters, keep this structure nicely aligned
 * since it is going to be used in hot paths
 */
struct bcmgenet_hw_params {
	u8		tx_queues;
	u8		rx_queues;
	u8		bds_cnt;
	u8		bp_in_en_shift;
	u32		bp_in_mask;
	u8		hfb_filter_cnt;
	u8		qtag_mask;
	u16		tbuf_offset;
	u32		hfb_offset;
	u32		hfb_reg_offset;
	u32		rdma_offset;
	u32		tdma_offset;
	u32		words_per_bd;
	u32		flags;
};

struct bcmgenet_tx_ring {
	spinlock_t	lock;		/* ring lock */
	unsigned int	index;		/* ring index */
	unsigned int	queue;		/* queue index */
	struct enet_cb	*cbs;		/* tx ring buffer control block*/
	unsigned int	size;		/* size of each tx ring */
	unsigned int	c_index;	/* last consumer index of each ring*/
	unsigned int	free_bds;	/* # of free bds for each ring */
	unsigned int	write_ptr;	/* Tx ring write pointer SW copy */
	unsigned int	prod_index;	/* Tx ring producer index SW copy */
	unsigned int	cb_ptr;		/* Tx ring initial CB ptr */
	unsigned int	end_ptr;	/* Tx ring end CB ptr */
	void (*int_enable)(struct bcmgenet_priv *priv,
			   struct bcmgenet_tx_ring *);
	void (*int_disable)(struct bcmgenet_priv *priv,
			    struct bcmgenet_tx_ring *);
};

/* device context */
struct bcmgenet_priv {
	void __iomem *base;
	enum bcmgenet_version version;
	struct net_device *dev;
	u32 int0_mask;
	u32 int1_mask;

	/* NAPI for descriptor based rx */
	struct napi_struct napi ____cacheline_aligned;

	/* transmit variables */
	void __iomem *tx_bds;
	struct enet_cb *tx_cbs;
	unsigned int num_tx_bds;

	struct bcmgenet_tx_ring tx_rings[DESC_INDEX + 1];

	/* receive variables */
	void __iomem *rx_bds;
	void __iomem *rx_bd_assign_ptr;
	int rx_bd_assign_index;
	struct enet_cb *rx_cbs;
	unsigned int num_rx_bds;
	unsigned int rx_buf_len;
	unsigned int rx_read_ptr;
	unsigned int rx_c_index;

	/* other misc variables */
	struct bcmgenet_hw_params *hw_params;

	/* MDIO bus variables */
	wait_queue_head_t wq;
	struct phy_device *phydev;
	struct device_node *phy_dn;
	struct mii_bus *mii_bus;
	u16 gphy_rev;
	struct clk *clk_eee;
	bool clk_eee_enabled;

	/* PHY device variables */
	int old_link;
	int old_speed;
	int old_duplex;
	int old_pause;
	phy_interface_t phy_interface;
	int phy_addr;
	int ext_phy;

	/* Interrupt variables */
	struct work_struct bcmgenet_irq_work;
	int irq0;
	int irq1;
	unsigned int irq0_stat;
	unsigned int irq1_stat;
	int wol_irq;
	bool wol_irq_disabled;

	/* HW descriptors/checksum variables */
	bool desc_64b_en;
	bool desc_rxchk_en;
	bool crc_fwd_en;

	unsigned int dma_rx_chk_bit;

	u32 msg_enable;

	struct clk *clk;
	struct platform_device *pdev;

	/* WOL */
	struct clk *clk_wol;
	u32 wolopts;

	struct bcmgenet_mib_counters mib;

	struct ethtool_eee eee;
};

#define GENET_IO_MACRO(name, offset)					\
static inline u32 bcmgenet_##name##_readl(struct bcmgenet_priv *priv,	\
					u32 off)			\
{									\
	return __raw_readl(priv->base + offset + off);			\
}									\
static inline void bcmgenet_##name##_writel(struct bcmgenet_priv *priv,	\
					u32 val, u32 off)		\
{									\
	__raw_writel(val, priv->base + offset + off);			\
}

GENET_IO_MACRO(ext, GENET_EXT_OFF);
GENET_IO_MACRO(umac, GENET_UMAC_OFF);
GENET_IO_MACRO(sys, GENET_SYS_OFF);

/* interrupt l2 registers accessors */
GENET_IO_MACRO(intrl2_0, GENET_INTRL2_0_OFF);
GENET_IO_MACRO(intrl2_1, GENET_INTRL2_1_OFF);

/* HFB register accessors  */
GENET_IO_MACRO(hfb, priv->hw_params->hfb_offset);

/* GENET v2+ HFB control and filter len helpers */
GENET_IO_MACRO(hfb_reg, priv->hw_params->hfb_reg_offset);

/* RBUF register accessors */
GENET_IO_MACRO(rbuf, GENET_RBUF_OFF);

/* MDIO routines */
int bcmgenet_mii_init(struct net_device *dev);
int bcmgenet_mii_config(struct net_device *dev, bool init);
void bcmgenet_mii_exit(struct net_device *dev);
void bcmgenet_mii_reset(struct net_device *dev);
void bcmgenet_mii_setup(struct net_device *dev);

/* Wake-on-LAN routines */
void bcmgenet_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol);
int bcmgenet_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol);
int bcmgenet_wol_power_down_cfg(struct bcmgenet_priv *priv,
				enum bcmgenet_power_mode mode);
void bcmgenet_wol_power_up_cfg(struct bcmgenet_priv *priv,
			       enum bcmgenet_power_mode mode);

#endif /* __BCMGENET_H__ */
