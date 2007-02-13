/*
 * Copyright(c) 2005 - 2006 Attansic Corporation. All rights reserved.
 * Copyright(c) 2006 Chris Snook <csnook@redhat.com>
 * Copyright(c) 2006 Jay Cliburn <jcliburn@gmail.com>
 *
 * Derived from Intel e1000 driver
 * Copyright(c) 1999 - 2005 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _ATL1_H_
#define _ATL1_H_

#include <linux/types.h>
#include <linux/if_vlan.h>

#include "atl1_hw.h"

/* function prototypes needed by multiple files */
s32 atl1_up(struct atl1_adapter *adapter);
void atl1_down(struct atl1_adapter *adapter);
int atl1_reset(struct atl1_adapter *adapter);
s32 atl1_setup_ring_resources(struct atl1_adapter *adapter);
void atl1_free_ring_resources(struct atl1_adapter *adapter);

extern char atl1_driver_name[];
extern char atl1_driver_version[];
extern const struct ethtool_ops atl1_ethtool_ops;

struct atl1_adapter;

#define ATL1_MAX_INTR		3

#define ATL1_DEFAULT_TPD	256
#define ATL1_MAX_TPD		1024
#define ATL1_MIN_TPD		64
#define ATL1_DEFAULT_RFD	512
#define ATL1_MIN_RFD		128
#define ATL1_MAX_RFD		2048

#define ATL1_GET_DESC(R, i, type)	(&(((type *)((R)->desc))[i]))
#define ATL1_RFD_DESC(R, i)	ATL1_GET_DESC(R, i, struct rx_free_desc)
#define ATL1_TPD_DESC(R, i)	ATL1_GET_DESC(R, i, struct tx_packet_desc)
#define ATL1_RRD_DESC(R, i)	ATL1_GET_DESC(R, i, struct rx_return_desc)

/*
 * Some workarounds require millisecond delays and are run during interrupt
 * context.  Most notably, when establishing link, the phy may need tweaking
 * but cannot process phy register reads/writes faster than millisecond
 * intervals...and we establish link due to a "link status change" interrupt.
 */

/*
 * wrapper around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct atl1_buffer {
	struct sk_buff *skb;
	u16 length;
	u16 alloced;
	dma_addr_t dma;
};

#define MAX_TX_BUF_LEN		0x3000	/* 12KB */

struct atl1_tpd_ring {
	void *desc;		/* pointer to the descriptor ring memory */
	dma_addr_t dma;		/* physical adress of the descriptor ring */
	u16 size;		/* length of descriptor ring in bytes */
	u16 count;		/* number of descriptors in the ring */
	u16 hw_idx;		/* hardware index */
	atomic_t next_to_clean;
	atomic_t next_to_use;
	struct atl1_buffer *buffer_info;
};

struct atl1_rfd_ring {
	void *desc;
	dma_addr_t dma;
	u16 size;
	u16 count;
	atomic_t next_to_use;
	u16 next_to_clean;
	struct atl1_buffer *buffer_info;
};

struct atl1_rrd_ring {
	void *desc;
	dma_addr_t dma;
	unsigned int size;
	u16 count;
	u16 next_to_use;
	atomic_t next_to_clean;
};

struct atl1_ring_header {
	void *desc;		/* pointer to the descriptor ring memory */
	dma_addr_t dma;		/* physical adress of the descriptor ring */
	unsigned int size;	/* length of descriptor ring in bytes */
};

struct atl1_cmb {
	struct coals_msg_block *cmb;
	dma_addr_t dma;
};

struct atl1_smb {
	struct stats_msg_block *smb;
	dma_addr_t dma;
};

/* Statistics counters */
struct atl1_sft_stats {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 multicast;
	u64 collisions;
	u64 rx_errors;
	u64 rx_length_errors;
	u64 rx_crc_errors;
	u64 rx_frame_errors;
	u64 rx_fifo_errors;
	u64 rx_missed_errors;
	u64 tx_errors;
	u64 tx_fifo_errors;
	u64 tx_aborted_errors;
	u64 tx_window_errors;
	u64 tx_carrier_errors;

	u64 tx_pause;		/* num Pause packet transmitted. */
	u64 excecol;		/* num tx packets aborted due to excessive collisions. */
	u64 deffer;		/* num deferred tx packets */
	u64 scc;		/* num packets subsequently transmitted successfully w/ single prior collision. */
	u64 mcc;		/* num packets subsequently transmitted successfully w/ multiple prior collisions. */
	u64 latecol;		/* num tx packets  w/ late collisions. */
	u64 tx_underun;		/* num tx packets aborted due to transmit FIFO underrun, or TRD FIFO underrun */
	u64 tx_trunc;		/* num tx packets truncated due to size exceeding MTU, regardless whether truncated by Selene or not. (The name doesn't really reflect the meaning in this case.) */
	u64 rx_pause;		/* num Pause packets received. */
	u64 rx_rrd_ov;
	u64 rx_trunc;
};

/* board specific private data structure */
#define ATL1_REGS_LEN	8

/* Structure containing variables used by the shared code */
struct atl1_hw {
	u8 __iomem *hw_addr;
	struct atl1_adapter *back;
	enum atl1_dma_order dma_ord;
	enum atl1_dma_rcb rcb_value;
	enum atl1_dma_req_block dmar_block;
	enum atl1_dma_req_block dmaw_block;
	u8 preamble_len;
	u8 max_retry;		/* Retransmission maximum, after which the packet will be discarded */
	u8 jam_ipg;		/* IPG to start JAM for collision based flow control in half-duplex mode. In units of 8-bit time */
	u8 ipgt;		/* Desired back to back inter-packet gap. The default is 96-bit time */
	u8 min_ifg;		/* Minimum number of IFG to enforce in between RX frames. Frame gap below such IFP is dropped */
	u8 ipgr1;		/* 64bit Carrier-Sense window */
	u8 ipgr2;		/* 96-bit IPG window */
	u8 tpd_burst;		/* Number of TPD to prefetch in cache-aligned burst. Each TPD is 16 bytes long */
	u8 rfd_burst;		/* Number of RFD to prefetch in cache-aligned burst. Each RFD is 12 bytes long */
	u8 rfd_fetch_gap;
	u8 rrd_burst;		/* Threshold number of RRDs that can be retired in a burst. Each RRD is 16 bytes long */
	u8 tpd_fetch_th;
	u8 tpd_fetch_gap;
	u16 tx_jumbo_task_th;
	u16 txf_burst;		/* Number of data bytes to read in a cache-aligned burst. Each SRAM entry is
				   8 bytes long */
	u16 rx_jumbo_th;	/* Jumbo packet size for non-VLAN packet. VLAN packets should add 4 bytes */
	u16 rx_jumbo_lkah;
	u16 rrd_ret_timer;	/* RRD retirement timer. Decrement by 1 after every 512ns passes. */
	u16 lcol;		/* Collision Window */

	u16 cmb_tpd;
	u16 cmb_rrd;
	u16 cmb_rx_timer;
	u16 cmb_tx_timer;
	u32 smb_timer;
	u16 media_type;
	u16 autoneg_advertised;
	u16 pci_cmd_word;

	u16 mii_autoneg_adv_reg;
	u16 mii_1000t_ctrl_reg;

	u32 mem_rang;
	u32 txcw;
	u32 max_frame_size;
	u32 min_frame_size;
	u32 mc_filter_type;
	u32 num_mc_addrs;
	u32 collision_delta;
	u32 tx_packet_delta;
	u16 phy_spd_default;

	u16 dev_rev;
	u8 revision_id;

	/* spi flash */
	u8 flash_vendor;

	u8 dma_fairness;
	u8 mac_addr[ETH_ALEN];
	u8 perm_mac_addr[ETH_ALEN];

	/* bool phy_preamble_sup; */
	bool phy_configured;
};

struct atl1_adapter {
	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;
	struct atl1_sft_stats soft_stats;

	struct vlan_group *vlgrp;
	u32 rx_buffer_len;
	u32 wol;
	u16 link_speed;
	u16 link_duplex;
	spinlock_t lock;
	atomic_t irq_sem;
	struct work_struct tx_timeout_task;
	struct work_struct link_chg_task;
	struct work_struct pcie_dma_to_rst_task;
	struct timer_list watchdog_timer;
	struct timer_list phy_config_timer;
	bool phy_timer_pending;

	bool mac_disabled;

	/* All descriptor rings' memory */
	struct atl1_ring_header ring_header;

	/* TX */
	struct atl1_tpd_ring tpd_ring;
	spinlock_t mb_lock;

	/* RX */
	struct atl1_rfd_ring rfd_ring;
	struct atl1_rrd_ring rrd_ring;
	u64 hw_csum_err;
	u64 hw_csum_good;

	u32 gorcl;
	u64 gorcl_old;

	/* Interrupt Moderator timer ( 2us resolution) */
	u16 imt;
	/* Interrupt Clear timer (2us resolution) */
	u16 ict;

	/* MII interface info */
	struct mii_if_info mii;

	/* structs defined in atl1_hw.h */
	u32 bd_number;		/* board number */
	bool pci_using_64;
	struct atl1_hw hw;
	struct atl1_smb smb;
	struct atl1_cmb cmb;

	u32 pci_state[16];
};

#endif	/* _ATL1_H_ */
