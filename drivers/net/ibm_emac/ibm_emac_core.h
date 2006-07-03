/*
 * drivers/net/ibm_emac/ibm_emac_core.h
 *
 * Driver for PowerPC 4xx on-chip ethernet controller.
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Armin Kuster <akuster@mvista.com>
 * 	Johnnie Peters <jpeters@mvista.com>
 *      Copyright 2000, 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __IBM_EMAC_CORE_H_
#define __IBM_EMAC_CORE_H_

#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <asm/ocp.h>

#include "ibm_emac.h"
#include "ibm_emac_phy.h"
#include "ibm_emac_zmii.h"
#include "ibm_emac_rgmii.h"
#include "ibm_emac_mal.h"
#include "ibm_emac_tah.h"

#define NUM_TX_BUFF			CONFIG_IBM_EMAC_TXB
#define NUM_RX_BUFF			CONFIG_IBM_EMAC_RXB

/* Simple sanity check */
#if NUM_TX_BUFF > 256 || NUM_RX_BUFF > 256
#error Invalid number of buffer descriptors (greater than 256)
#endif

// XXX
#define EMAC_MIN_MTU			46
#define EMAC_MAX_MTU			9000

/* Maximum L2 header length (VLAN tagged, no FCS) */
#define EMAC_MTU_OVERHEAD		(6 * 2 + 2 + 4)

/* RX BD size for the given MTU */
static inline int emac_rx_size(int mtu)
{
	if (mtu > ETH_DATA_LEN)
		return MAL_MAX_RX_SIZE;
	else
		return mal_rx_size(ETH_DATA_LEN + EMAC_MTU_OVERHEAD);
}

#define EMAC_DMA_ALIGN(x)		ALIGN((x), dma_get_cache_alignment())

#define EMAC_RX_SKB_HEADROOM		\
	EMAC_DMA_ALIGN(CONFIG_IBM_EMAC_RX_SKB_HEADROOM)

/* Size of RX skb for the given MTU */
static inline int emac_rx_skb_size(int mtu)
{
	int size = max(mtu + EMAC_MTU_OVERHEAD, emac_rx_size(mtu));
	return EMAC_DMA_ALIGN(size + 2) + EMAC_RX_SKB_HEADROOM;
}

/* RX DMA sync size */
static inline int emac_rx_sync_size(int mtu)
{
	return EMAC_DMA_ALIGN(emac_rx_size(mtu) + 2);
}

/* Driver statistcs is split into two parts to make it more cache friendly:
 *   - normal statistics (packet count, etc)
 *   - error statistics
 *
 * When statistics is requested by ethtool, these parts are concatenated,
 * normal one goes first.
 *
 * Please, keep these structures in sync with emac_stats_keys.
 */

/* Normal TX/RX Statistics */
struct ibm_emac_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets_csum;
	u64 tx_packets_csum;
};

/* Error statistics */
struct ibm_emac_error_stats {
	u64 tx_undo;

	/* Software RX Errors */
	u64 rx_dropped_stack;
	u64 rx_dropped_oom;
	u64 rx_dropped_error;
	u64 rx_dropped_resize;
	u64 rx_dropped_mtu;
	u64 rx_stopped;
	/* BD reported RX errors */
	u64 rx_bd_errors;
	u64 rx_bd_overrun;
	u64 rx_bd_bad_packet;
	u64 rx_bd_runt_packet;
	u64 rx_bd_short_event;
	u64 rx_bd_alignment_error;
	u64 rx_bd_bad_fcs;
	u64 rx_bd_packet_too_long;
	u64 rx_bd_out_of_range;
	u64 rx_bd_in_range;
	/* EMAC IRQ reported RX errors */
	u64 rx_parity;
	u64 rx_fifo_overrun;
	u64 rx_overrun;
	u64 rx_bad_packet;
	u64 rx_runt_packet;
	u64 rx_short_event;
	u64 rx_alignment_error;
	u64 rx_bad_fcs;
	u64 rx_packet_too_long;
	u64 rx_out_of_range;
	u64 rx_in_range;

	/* Software TX Errors */
	u64 tx_dropped;
	/* BD reported TX errors */
	u64 tx_bd_errors;
	u64 tx_bd_bad_fcs;
	u64 tx_bd_carrier_loss;
	u64 tx_bd_excessive_deferral;
	u64 tx_bd_excessive_collisions;
	u64 tx_bd_late_collision;
	u64 tx_bd_multple_collisions;
	u64 tx_bd_single_collision;
	u64 tx_bd_underrun;
	u64 tx_bd_sqe;
	/* EMAC IRQ reported TX errors */
	u64 tx_parity;
	u64 tx_underrun;
	u64 tx_sqe;
	u64 tx_errors;
};

#define EMAC_ETHTOOL_STATS_COUNT	((sizeof(struct ibm_emac_stats) + \
					  sizeof(struct ibm_emac_error_stats)) \
					 / sizeof(u64))

struct ocp_enet_private {
	struct net_device		*ndev;		/* 0 */
	struct emac_regs		__iomem *emacp;
	
	struct mal_descriptor		*tx_desc;
	int				tx_cnt;
	int				tx_slot;
	int				ack_slot;

	struct mal_descriptor		*rx_desc;
	int				rx_slot;
	struct sk_buff			*rx_sg_skb;	/* 1 */
	int 				rx_skb_size;
	int				rx_sync_size;

	struct ibm_emac_stats 		stats;
	struct ocp_device		*tah_dev;

	struct ibm_ocp_mal		*mal;
	struct mal_commac		commac;

	struct sk_buff			*tx_skb[NUM_TX_BUFF];
	struct sk_buff			*rx_skb[NUM_RX_BUFF];

	struct ocp_device		*zmii_dev;
	int				zmii_input;
	struct ocp_enet_private		*mdio_dev;
	struct ocp_device		*rgmii_dev;
	int				rgmii_input;

	struct ocp_def			*def;

	struct mii_phy			phy;
	struct timer_list		link_timer;
	int				reset_failed;

	int				stop_timeout;	/* in us */

	struct ibm_emac_error_stats	estats;
	struct net_device_stats		nstats;

	struct device*			ldev;
};

/* Ethtool get_regs complex data.
 * We want to get not just EMAC registers, but also MAL, ZMII, RGMII, TAH 
 * when available.
 * 
 * Returned BLOB consists of the ibm_emac_ethtool_regs_hdr, 
 * MAL registers, EMAC registers and optional ZMII, RGMII, TAH registers.
 * Each register component is preceded with emac_ethtool_regs_subhdr.
 * Order of the optional headers follows their relative bit posititions 
 * in emac_ethtool_regs_hdr.components
 */
#define EMAC_ETHTOOL_REGS_ZMII		0x00000001
#define EMAC_ETHTOOL_REGS_RGMII		0x00000002
#define EMAC_ETHTOOL_REGS_TAH		0x00000004

struct emac_ethtool_regs_hdr {
	u32 components;
};

struct emac_ethtool_regs_subhdr {
	u32 version;
	u32 index;
};

#endif				/* __IBM_EMAC_CORE_H_ */
