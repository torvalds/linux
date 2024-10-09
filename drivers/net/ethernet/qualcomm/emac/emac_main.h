/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2013-2017, The Linux Foundation. All rights reserved.
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
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

#ifndef _QCOM_EMAC_MAIN_H_
#define _QCOM_EMAC_MAIN_H_

#include <asm/byteorder.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
//#include <linux/wakelock.h>

#include "emac_phy.h"

/* Device IDs */
#define EMAC_DEV_ID                0x0040

/* DMA address */
#define DMA_ADDR_HI_MASK           0xffffffff00000000ULL
#define DMA_ADDR_LO_MASK           0x00000000ffffffffULL

#define EMAC_DMA_ADDR_HI(_addr) \
		((u32)(((u64)(_addr) & DMA_ADDR_HI_MASK) >> 32))
#define EMAC_DMA_ADDR_LO(_addr) \
		((u32)((u64)(_addr) & DMA_ADDR_LO_MASK))

/* 4 emac core irq and 1 wol irq */
#define EMAC_NUM_CORE_IRQ	4
#define EMAC_CORE0_IRQ		0
#define EMAC_CORE1_IRQ		1
#define EMAC_CORE2_IRQ		2
#define EMAC_CORE3_IRQ		3
#define EMAC_WOL_IRQ		4
#define EMAC_IRQ_CNT		5
/* mdio/mdc gpios */
#define EMAC_GPIO_CNT		2

#define EMAC_ADPT_RESET_WAIT_TIME	20

/**
 * Requested EMAC votes for BUS bandwidth
 *
 * EMAC_NO_PERF_VOTE      BUS Vote for inactive EMAC session or disconnect
 * EMAC_MAX_PERF_VOTE    Maximum BUS bandwidth vote
 *
 */
enum emac_bus_vote {
	EMAC_NO_PERF_VOTE = 0,
	EMAC_MAX_PERF_VOTE
};

enum emac_vreg_id {
	EMAC_VREG1,
	EMAC_VREG2,
	EMAC_VREG3,
	EMAC_VREG4,
	EMAC_VREG5,
	EMAC_VREG_CNT
};

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

#define KHz(RATE)	((RATE)    * 1000)
#define MHz(RATE)	(KHz(RATE) * 1000)

enum emac_clk_rate {
	EMC_CLK_RATE_2_5MHZ	= KHz(2500),
	EMC_CLK_RATE_19_2MHZ	= KHz(19200),
	EMC_CLK_RATE_25MHZ	= MHz(25),
	EMC_CLK_RATE_125MHZ	= MHz(125),
};

#define EMAC_LINK_SPEED_UNKNOWN         0x0
#define EMAC_LINK_SPEED_10_HALF         0x0001
#define EMAC_LINK_SPEED_10_FULL         0x0002
#define EMAC_LINK_SPEED_100_HALF        0x0004
#define EMAC_LINK_SPEED_100_FULL        0x0008
#define EMAC_LINK_SPEED_1GB_FULL        0x0020

#define EMAC_MAX_SETUP_LNK_CYCLE        100

/* Wake On Lan */
#define EMAC_WOL_PHY                     0x00000001 /* PHY Status Change */
#define EMAC_WOL_MAGIC                   0x00000002 /* Magic Packet */

enum emac_reg_bases {
	EMAC,
	EMAC_CSR,
	EMAC_1588,
	NUM_EMAC_REG_BASES
};

/* DMA Order Settings */
enum emac_dma_order {
	emac_dma_ord_in = 1,
	emac_dma_ord_enh = 2,
	emac_dma_ord_out = 4
};

enum emac_dma_req_block {
	emac_dma_req_128 = 0,
	emac_dma_req_256 = 1,
	emac_dma_req_512 = 2,
	emac_dma_req_1024 = 3,
	emac_dma_req_2048 = 4,
	emac_dma_req_4096 = 5
};

/* IEEE1588 */
enum emac_ptp_clk_mode {
	emac_ptp_clk_mode_oc_two_step,
	emac_ptp_clk_mode_oc_one_step
};

enum emac_ptp_mode {
	emac_ptp_mode_slave,
	emac_ptp_mode_master
};

struct emac_hw_stats {
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
	u64 rx_jubbers;         /* jubbers */

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

enum emac_hw_flags {
	EMAC_FLAG_HW_PROMISC_EN,
	EMAC_FLAG_HW_VLANSTRIP_EN,
	EMAC_FLAG_HW_MULTIALL_EN,
	EMAC_FLAG_HW_LOOPBACK_EN,
	EMAC_FLAG_HW_PTP_CAP,
	EMAC_FLAG_HW_PTP_EN,
	EMAC_FLAG_HW_TS_RX_EN,
	EMAC_FLAG_HW_TS_TX_EN,
};

enum emac_adapter_flags {
	EMAC_FLAG_ADPT_STATE_RESETTING,
	EMAC_FLAG_ADPT_STATE_DOWN,
	EMAC_FLAG_ADPT_STATE_WATCH_DOG,
	EMAC_FLAG_ADPT_TASK_REINIT_REQ,
	EMAC_FLAG_ADPT_TASK_LSC_REQ,
	EMAC_FLAG_ADPT_TASK_CHK_SGMII_REQ,
};

/* emac shorthand bitops macros */
#define TEST_FLAG(OBJ, FLAG)	test_bit(EMAC_FLAG_ ## FLAG,  &((OBJ)->flags))
#define SET_FLAG(OBJ,  FLAG)	set_bit(EMAC_FLAG_ ## FLAG,   &((OBJ)->flags))
#define CLR_FLAG(OBJ,  FLAG)	clear_bit(EMAC_FLAG_ ## FLAG, &((OBJ)->flags))
#define TEST_N_SET_FLAG(OBJ, FLAG) \
			test_and_set_bit(EMAC_FLAG_ ## FLAG,  &((OBJ)->flags))

struct emac_hw {
	void __iomem *reg_addr[NUM_EMAC_REG_BASES];

	u16     devid;
	u16     revid;

	/* ring parameter */
	u8      tpd_burst;
	u8      rfd_burst;
	u8      dmaw_dly_cnt;
	u8      dmar_dly_cnt;
	enum emac_dma_req_block   dmar_block;
	enum emac_dma_req_block   dmaw_block;
	enum emac_dma_order       dma_order;

	/* RSS parameter */
	u8      rss_hstype;
	u8      rss_base_cpu;
	u16     rss_idt_size;
	u32     rss_idt[32];
	u8      rss_key[40];
	bool    rss_initialized;

	/* 1588 parameter */
	enum emac_ptp_clk_mode  ptp_clk_mode;
	enum emac_ptp_mode      ptp_mode;
	u32                     ptp_intr_mask;
	spinlock_t              ptp_lock; /* sync access to ptp hw */
	u32                     tstamp_rx_offset;
	u32                     tstamp_tx_offset;
	void                    *frac_ns_adj_tbl;
	u32                     frac_ns_adj_tbl_sz;
	s32                     frac_ns_adj;

	u32                 irq_mod;
	u32                 preamble;
	unsigned long       flags;
};

/* RSS hstype Definitions */
#define EMAC_RSS_HSTYP_IPV4_EN           0x00000001
#define EMAC_RSS_HSTYP_TCP4_EN           0x00000002
#define EMAC_RSS_HSTYP_IPV6_EN           0x00000004
#define EMAC_RSS_HSTYP_TCP6_EN           0x00000008
#define EMAC_RSS_HSTYP_ALL_EN (\
		EMAC_RSS_HSTYP_IPV4_EN   |\
		EMAC_RSS_HSTYP_TCP4_EN   |\
		EMAC_RSS_HSTYP_IPV6_EN   |\
		EMAC_RSS_HSTYP_TCP6_EN)

/******************************************************************************/
/* Logging functions and macros */
#define emac_err(_adpt, _format, ...) \
	netdev_err((_adpt)->netdev, _format, ##__VA_ARGS__)

#define emac_info(_adpt, _mlevel, _netdev, _format, ...) \
	netif_info(_adpt, _mlevel, _netdev, _format, ##__VA_ARGS__)

#define emac_warn(_adpt, _mlevel, _netdev, _format, ...) \
	netif_warn(_adpt, _mlevel, _netdev, _format, ##__VA_ARGS__)

#define emac_dbg(_adpt, _mlevel, _netdev, _format, ...) \
	netif_dbg(_adpt, _mlevel, _netdev, _format, ##__VA_ARGS__)

#define EMAC_DEF_RX_BUF_SIZE            1536
#define EMAC_MAX_JUMBO_PKT_SIZE         (9 * 1024)
#define EMAC_MAX_TX_OFFLOAD_THRESH      (9 * 1024)

#define EMAC_MAX_ETH_FRAME_SIZE         EMAC_MAX_JUMBO_PKT_SIZE
#define EMAC_MIN_ETH_FRAME_SIZE         68

#define EMAC_MAX_TX_QUEUES      4
#define EMAC_DEF_TX_QUEUES      1
#define EMAC_ACTIVE_TXQ         0

#define EMAC_MAX_RX_QUEUES      4
#define EMAC_DEF_RX_QUEUES      1

#define EMAC_MIN_TX_DESCS       128
#define EMAC_MIN_RX_DESCS       128

#define EMAC_MAX_TX_DESCS       16383
#define EMAC_MAX_RX_DESCS       2047

#define EMAC_DEF_TX_DESCS       512
#define EMAC_DEF_RX_DESCS       256

#define EMAC_DEF_RX_IRQ_MOD     250
#define EMAC_DEF_TX_IRQ_MOD     250

#define EMAC_WATCHDOG_TIME      (5 * HZ)

/* RRD */
/* general parameter format of rrd */
struct emac_sw_rrdes_general {
	/* dword 0 */
	u32  xsum:16;
	u32  nor:4;       /* number of RFD */
	u32  si:12;       /* start index of rfd-ring */
	/* dword 1 */
	u32  hash;
	/* dword 2 */
	u32  cvlan_tag:16; /* vlan-tag */
	u32  reserved:8;
	u32  ptp_timestamp:1;
	u32  rss_cpu:3;   /* CPU number used by RSS */
	u32  rss_flag:4;  /* rss_flag 0, TCP(IPv6) flag for RSS hash algrithm
			   * rss_flag 1, IPv6 flag for RSS hash algrithm
			   * rss_flag 2, TCP(IPv4) flag for RSS hash algrithm
			   * rss_flag 3, IPv4 flag for RSS hash algrithm
			   */
	/* dword 3 */
	u32  pkt_len:14;  /* length of the packet */
	u32  l4f:1;       /* L4(TCP/UDP) checksum failed */
	u32  ipf:1;       /* IP checksum failed */
	u32  cvlan_flag:1; /* vlan tagged */
	u32  pid:3;
	u32  res:1;       /* received error summary */
	u32  crc:1;       /* crc error */
	u32  fae:1;       /* frame alignment error */
	u32  trunc:1;     /* truncated packet, larger than MTU */
	u32  runt:1;      /* runt packet */
	u32  icmp:1;      /* incomplete packet due to insufficient rx-desc*/
	u32  bar:1;       /* broadcast address received */
	u32  mar:1;       /* multicast address received */
	u32  type:1;      /* ethernet type */
	u32  fov:1;       /* fifo overflow */
	u32  lene:1;      /* length error */
	u32  update:1;    /* update */

	/* dword 4 */
	u32 ts_low:30;
	u32 __unused__:2;
	/* dword 5 */
	u32 ts_high;
};

/* EMAC Errors in emac_sw_rrdesc.dfmt.dw[3] */
#define EMAC_RRDES_L4F BIT(14)
#define EMAC_RRDES_IPF BIT(15)
#define EMAC_RRDES_CRC BIT(21)
#define EMAC_RRDES_FAE BIT(22)
#define EMAC_RRDES_TRN BIT(23)
#define EMAC_RRDES_RNT BIT(24)
#define EMAC_RRDES_INC BIT(25)
#define EMAC_RRDES_FOV BIT(29)
#define EMAC_RRDES_LEN BIT(30)

union emac_sw_rrdesc {
	struct emac_sw_rrdes_general genr;

	/* dword flat format */
	struct {
		u32 dw[6];
	} dfmt;
};

/* RFD */
/* general parameter format of rfd */
struct emac_sw_rfdes_general {
	u64   addr;
};

union emac_sw_rfdesc {
	struct emac_sw_rfdes_general genr;

	/* dword flat format */
	struct {
		u32 dw[2];
	} dfmt;
};

/* TPD */
/* general parameter format of tpd */
struct emac_sw_tpdes_general {
	/* dword 0 */
	u32  buffer_len:16; /* include 4-byte CRC */
	u32  svlan_tag:16;
	/* dword 1 */
	u32  l4hdr_offset:8; /* l4 header offset to the 1st byte of packet */
	u32  c_csum:1;
	u32  ip_csum:1;
	u32  tcp_csum:1;
	u32  udp_csum:1;
	u32  lso:1;
	u32  lso_v2:1;
	u32  svtagged:1;   /* vlan-id tagged already */
	u32  ins_svtag:1;  /* insert vlan tag */
	u32  ipv4:1;       /* ipv4 packet */
	u32  type:1;       /* type of packet (ethernet_ii(0) or snap(1)) */
	u32  reserve:12;
	u32  epad:1;       /* even byte padding when this packet */
	u32  last_frag:1;  /* last fragment(buffer) of the packet */
	/* dword 2 */
	u32  addr_lo;
	/* dword 3 */
	u32  cvlan_tag:16;
	u32  cvtagged:1;
	u32  ins_cvtag:1;
	u32  addr_hi:13;
	u32  tstmp_sav:1;
};

/* custom checksum parameter format of tpd */
struct emac_sw_tpdes_checksum {
	/* dword 0 */
	u32  buffer_len:16;
	u32  svlan_tag:16;
	/* dword 1 */
	u32  payld_offset:8; /* payload offset to the 1st byte of packet */
	u32  c_csum:1;       /* do custom checksum offload */
	u32  ip_csum:1;      /* do ip(v4) header checksum offload */
	u32  tcp_csum:1;     /* do tcp checksum offload, both ipv4 and ipv6 */
	u32  udp_csum:1;     /* do udp checksum offload, both ipv4 and ipv6 */
	u32  lso:1;
	u32  lso_v2:1;
	u32  svtagged:1;     /* vlan-id tagged already */
	u32  ins_svtag:1;    /* insert vlan tag */
	u32  ipv4:1;         /* ipv4 packet */
	u32  type:1;         /* type of packet (ethernet_ii(0) or snap(1)) */
	u32  cxsum_offset:8; /* checksum offset to the 1st byte of packet */
	u32  reserve:4;
	u32  epad:1;         /* even byte padding when this packet */
	u32  last_frag:1;    /* last fragment(buffer) of the packet */
	/* dword 2 */
	u32  addr_lo;
	/* dword 3 */
	u32  cvlan_tag:16;
	u32  cvtagged:1;
	u32  ins_cvtag:1;
	u32  addr_hi:14;
};

/* tcp large send format (v1/v2) of tpd */
struct emac_sw_tpdes_tso {
	/* dword 0 */
	u32  buffer_len:16; /* include 4-byte CRC */
	u32  svlan_tag:16;
	/* dword 1 */
	u32  tcphdr_offset:8; /* tcp hdr offset to the 1st byte of packet */
	u32  c_csum:1;
	u32  ip_csum:1;
	u32  tcp_csum:1;
	u32  udp_csum:1;
	u32  lso:1;        /* do tcp large send (ipv4 only) */
	u32  lso_v2:1;     /* must be 0 in this format */
	u32  svtagged:1;   /* vlan-id tagged already */
	u32  ins_svtag:1;  /* insert vlan tag */
	u32  ipv4:1;       /* ipv4 packet */
	u32  type:1;       /* type of packet (ethernet_ii(1) or snap(0)) */
	u32  mss:13;       /* mss if do tcp large send */
	u32  last_frag:1;  /* last fragment(buffer) of the packet */
	/* dword 2 & 3 */
	u64  pkt_len:32;   /* packet length in ext tpd */
	u64  reserve:32;
};

union emac_sw_tpdesc {
	struct emac_sw_tpdes_general   genr;
	struct emac_sw_tpdes_checksum  csum;
	struct emac_sw_tpdes_tso       tso;

	/* dword flat format */
	struct {
		u32 dw[4];
	} dfmt;
};

#define EMAC_RRD(_que, _size, _i) \
	((_que)->rrd.rrdesc + ((_size) * (_i)))

#define EMAC_RFD(_que, _size, _i) \
	((_que)->rfd.rfdesc + ((_size) * (_i)))

#define EMAC_TPD(_que, _size, _i) \
	((_que)->tpd.tpdesc + ((_size) * (_i)))

#define EMAC_TPD_LAST_FRAGMENT  0x80000000
#define EMAC_TPD_TSTAMP_SAVE    0x80000000

/* emac_irq_per_dev per-device (per-adapter) irq properties.
 * @idx:	index of this irq entry in the adapter irq array.
 * @irq:	irq number.
 * @mask	mask to use over status register.
 */
struct emac_irq_per_dev {
	int idx;
	unsigned int irq;
	u32 mask;
};

/* emac_irq_common irq properties which are common to all devices of this driver
 * @name	name in configuration (devicetree).
 * @handler	ISR.
 * @status_reg	status register offset.
 * @mask_reg	mask   register offset.
 * @init_mask	initial value for mask to use over status register.
 * @irqflags	request_irq() flags.
 */
struct emac_irq_common {
	char *name;
	irq_handler_t handler;

	u32 status_reg;
	u32 mask_reg;
	u32 init_mask;

	unsigned long irqflags;
};

/* emac_irq_cmn_tbl a table of common irq properties to all devices of this
 * driver.
 */
extern const struct emac_irq_common emac_irq_cmn_tbl[];

struct emac_clk {
	struct clk		*clk;
	bool			enabled;
};

struct emac_regulator {
	struct regulator *vreg;
	int	voltage_uv;
	bool	enabled;
};

/* emac_ring_header represents a single, contiguous block of DMA space
 * mapped for the three descriptor rings (tpd, rfd, rrd)
 */
struct emac_ring_header {
	void           *desc;  /* virtual address */
	dma_addr_t      dma;    /* physical address */
	unsigned int    size;   /* length in bytes */
	unsigned int    used;
};

/* emac_buffer is wrapper around a pointer to a socket buffer
 * so a DMA handle can be stored along with the skb
 */
struct emac_buffer {
	struct sk_buff *skb;      /* socket buffer */
	u16             length;   /* rx buffer length */
	dma_addr_t      dma;
};

/* receive free descriptor (rfd) ring */
struct emac_rfd_ring {
	struct emac_buffer      *rfbuff;
	u32 __iomem             *rfdesc;  /* virtual address */
	dma_addr_t               rfdma;   /* physical address */
	u64 size;          /* length in bytes */
	u32 count;         /* number of descriptors in the ring */
	u32 produce_idx;
	u32 process_idx;
	u32 consume_idx;   /* unused */
};

/* receive return descriptor (rrd) ring */
struct emac_rrd_ring {
	u32 __iomem         *rrdesc;    /* virtual address */
	dma_addr_t           rrdma;     /* physical address */
	u64 size;          /* length in bytes */
	u32 count;         /* number of descriptors in the ring */
	u32 produce_idx;   /* unused */
	u32 consume_idx;
};

/* rx queue */
struct emac_rx_queue {
	struct device          *dev;      /* device for dma mapping */
	struct net_device      *netdev;   /* netdev ring belongs to */
	struct emac_rrd_ring    rrd;
	struct emac_rfd_ring    rfd;
	struct napi_struct      napi;

	u16 que_idx;       /* index in multi rx queues*/
	u16 produce_reg;
	u32 produce_mask;
	u8 produce_shft;

	u16 process_reg;
	u32 process_mask;
	u8 process_shft;

	u16 consume_reg;
	u32 consume_mask;
	u8 consume_shft;

	u32 intr;
	struct emac_irq_per_dev *irq;
};

#define GET_RFD_BUFFER(_rque, _i)    (&((_rque)->rfd.rfbuff[(_i)]))

/* transimit packet descriptor (tpd) ring */
struct emac_tpd_ring {
	struct emac_buffer *tpbuff;
	u32 __iomem        *tpdesc;   /* virtual address */
	dma_addr_t          tpdma;    /* physical address */

	u64 size;    /* length in bytes */
	u32 count;   /* number of descriptors in the ring */
	u32 produce_idx;
	u32 consume_idx;
	u32 last_produce_idx;
};

#define EMAC_HWTXTSTAMP_FIFO_DEPTH          8
#define EMAC_TX_POLL_HWTXTSTAMP_THRESHOLD   EMAC_HWTXTSTAMP_FIFO_DEPTH

/* HW tx timestamp */
struct emac_hwtxtstamp {
	u32 ts_idx;
	u32 sec;
	u32 ns;
};

struct emac_tx_tstamp_stats {
	u32 tx;
	u32 rx;
	u32 deliver;
	u32 drop;
	u32 lost;
	u32 timeout;
	u32 sched;
	u32 poll;
	u32 tx_poll;
};

/* tx queue */
struct emac_tx_queue {
	struct device         *dev;     /* device for dma mapping */
	struct net_device     *netdev;  /* netdev ring belongs to */
	struct emac_tpd_ring   tpd;

	u16 que_idx;       /* needed for multiqueue queue management */
	u16 max_packets;   /* max packets per interrupt */
	u16 produce_reg;
	u32 produce_mask;
	u8 produce_shft;

	u16 consume_reg;
	u32 consume_mask;
	u8 consume_shft;
};

#define GET_TPD_BUFFER(_tque, _i)    (&((_tque)->tpd.tpbuff[(_i)]))

/* driver private data structure */
struct emac_adapter {
	struct net_device		*netdev;
	struct mii_bus			*mii_bus;
	struct phy_device		*phydev;
	struct emac_phy			phy;
	struct emac_hw			hw;
	struct emac_hw_stats		hw_stats;
	int irq_status;

	struct emac_irq_per_dev		irq[EMAC_IRQ_CNT];
	unsigned int			gpio[EMAC_GPIO_CNT];
	struct emac_clk			clk[EMAC_CLK_CNT];
	struct emac_regulator		vreg[EMAC_VREG_CNT];

	/* dma parameters */
	u64                             dma_mask;
	struct device_dma_parameters    dma_parms;

	/* All Descriptor memory */
	struct emac_ring_header ring_header;
	struct emac_tx_queue tx_queue[EMAC_MAX_TX_QUEUES];
	struct emac_rx_queue rx_queue[EMAC_MAX_RX_QUEUES];
	u16 num_txques;
	u16 num_rxques;

	u32 num_txdescs;
	u32 num_rxdescs;
	u8 rrdesc_size; /* in quad words */
	u8 rfdesc_size; /* in quad words */
	u8 tpdesc_size; /* in quad words */

	u32 rxbuf_size;

	/* True == use single-pause-frame mode. */
	bool				single_pause_mode;

	/* tx timestamping queue */
	struct sk_buff_head         hwtxtstamp_pending_queue;
	struct sk_buff_head         hwtxtstamp_ready_queue;
	struct work_struct          hwtxtstamp_task;
	spinlock_t                  hwtxtstamp_lock; /* lock for hwtxtstamp */
	struct emac_tx_tstamp_stats hwtxtstamp_stats;

	struct work_struct work_thread;
	struct timer_list  emac_timer;
	unsigned long	link_jiffies;

	bool            tstamp_en;
	u32             wol;
	u16             msg_enable;
	unsigned long   flags;
	struct pinctrl	*pinctrl;
	struct pinctrl_state	*mdio_pins_clk_active;
	struct pinctrl_state	*mdio_pins_clk_sleep;
	struct pinctrl_state	*mdio_pins_data_active;
	struct pinctrl_state	*mdio_pins_data_sleep;
	struct pinctrl_state	*ephy_pins_active;
	struct pinctrl_state	*ephy_pins_sleep;
	int	(*gpio_on)(struct emac_adapter *adpt, bool mdio, bool ephy);
	int	(*gpio_off)(struct emac_adapter *adpt, bool mdio, bool ephy);
	struct wakeup_source *link_wlock;

	u32       bus_cl_hdl;
	struct msm_bus_scale_pdata *bus_scale_table;
};

static inline struct emac_adapter *emac_hw_get_adap(struct emac_hw *hw)
{
	return container_of(hw, struct emac_adapter, hw);
}

static inline
struct emac_adapter *emac_irq_get_adpt(struct emac_irq_per_dev *irq)
{
	struct emac_irq_per_dev *irq_0 = irq - irq->idx;
	/* why using __builtin_offsetof() and not container_of() ?
	 * container_of(irq_0, struct emac_adapter, irq) fails to compile
	 * because emac->irq is of array type.
	 */
	return (struct emac_adapter *)
		((char *)irq_0 - __builtin_offsetof(struct emac_adapter, irq));
}

/* default to trying for four seconds */
#define EMAC_TRY_LINK_TIMEOUT     (4 * 1000)

#define EMAC_HW_CTRL_RESET_MAC         0x00000001

void emac_set_ethtool_ops(struct net_device *netdev);
int emac_reinit_locked(struct emac_adapter *adpt);
void emac_update_hw_stats(struct emac_adapter *adpt);
int emac_resize_rings(struct net_device *netdev);
int emac_mac_up(struct emac_adapter *adpt);
void emac_mac_down(struct emac_adapter *adpt, u32 ctrl);
int emac_clk_set_rate(struct emac_adapter *adpt, enum emac_clk_id id,
		      enum emac_clk_rate rate);
void emac_task_schedule(struct emac_adapter *adpt);
void emac_check_lsc(struct emac_adapter *adpt);
void emac_wol_gpio_irq(struct emac_adapter *adpt, bool enable);

static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle, flag);

	if (ret)
		memset(ret, 0, size);
	return ret;
}

#endif /* _QCOM_EMAC_MAIN_H_ */
