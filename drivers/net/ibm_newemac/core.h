/*
 * drivers/net/ibm_newemac/core.h
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
#ifndef __IBM_NEWEMAC_CORE_H
#define __IBM_NEWEMAC_CORE_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>

#include <asm/of_platform.h>
#include <asm/io.h>
#include <asm/dcr.h>

#include "emac.h"
#include "phy.h"
#include "zmii.h"
#include "rgmii.h"
#include "mal.h"
#include "tah.h"
#include "debug.h"

#define NUM_TX_BUFF			CONFIG_IBM_NEW_EMAC_TXB
#define NUM_RX_BUFF			CONFIG_IBM_NEW_EMAC_RXB

/* Simple sanity check */
#if NUM_TX_BUFF > 256 || NUM_RX_BUFF > 256
#error Invalid number of buffer descriptors (greater than 256)
#endif

#define EMAC_MIN_MTU			46

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
	EMAC_DMA_ALIGN(CONFIG_IBM_NEW_EMAC_RX_SKB_HEADROOM)

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
struct emac_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 tx_packets;
	u64 tx_bytes;
	u64 rx_packets_csum;
	u64 tx_packets_csum;
};

/* Error statistics */
struct emac_error_stats {
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

#define EMAC_ETHTOOL_STATS_COUNT	((sizeof(struct emac_stats) + \
					  sizeof(struct emac_error_stats)) \
					 / sizeof(u64))

struct emac_instance {
	struct net_device		*ndev;
	struct resource			rsrc_regs;
	struct emac_regs		__iomem *emacp;
	struct of_device		*ofdev;
	struct device_node		**blist; /* bootlist entry */

	/* MAL linkage */
	u32				mal_ph;
	struct of_device		*mal_dev;
	u32				mal_rx_chan;
	u32				mal_tx_chan;
	struct mal_instance		*mal;
	struct mal_commac		commac;

	/* PHY infos */
	u32				phy_mode;
	u32				phy_map;
	u32				phy_address;
	u32				phy_feat_exc;
	struct mii_phy			phy;
	struct mutex			link_lock;
	struct delayed_work		link_work;
	int				link_polling;

	/* Shared MDIO if any */
	u32				mdio_ph;
	struct of_device		*mdio_dev;
	struct emac_instance		*mdio_instance;
	struct mutex			mdio_lock;

	/* ZMII infos if any */
	u32				zmii_ph;
	u32				zmii_port;
	struct of_device		*zmii_dev;

	/* RGMII infos if any */
	u32				rgmii_ph;
	u32				rgmii_port;
	struct of_device		*rgmii_dev;

	/* TAH infos if any */
	u32				tah_ph;
	u32				tah_port;
	struct of_device		*tah_dev;

	/* IRQs */
	int				wol_irq;
	int				emac_irq;

	/* OPB bus frequency in Mhz */
	u32				opb_bus_freq;

	/* Cell index within an ASIC (for clk mgmnt) */
	u32				cell_index;

	/* Max supported MTU */
	u32				max_mtu;

	/* Feature bits (from probe table) */
	unsigned int			features;

	/* Tx and Rx fifo sizes & other infos in bytes */
	u32				tx_fifo_size;
	u32				tx_fifo_size_gige;
	u32				rx_fifo_size;
	u32				rx_fifo_size_gige;
	u32				fifo_entry_size;
	u32				mal_burst_size; /* move to MAL ? */

	/* Descriptor management
	 */
	struct mal_descriptor		*tx_desc;
	int				tx_cnt;
	int				tx_slot;
	int				ack_slot;

	struct mal_descriptor		*rx_desc;
	int				rx_slot;
	struct sk_buff			*rx_sg_skb;	/* 1 */
	int 				rx_skb_size;
	int				rx_sync_size;

	struct sk_buff			*tx_skb[NUM_TX_BUFF];
	struct sk_buff			*rx_skb[NUM_RX_BUFF];

	/* Stats
	 */
	struct emac_error_stats		estats;
	struct net_device_stats		nstats;
	struct emac_stats 		stats;

	/* Misc
	 */
	int				reset_failed;
	int				stop_timeout;	/* in us */
	int				no_mcast;
	int				mcast_pending;
	struct work_struct		reset_work;
	spinlock_t			lock;
};

/*
 * Features of various EMAC implementations
 */

/*
 * No flow control on 40x according to the original driver
 */
#define EMAC_FTR_NO_FLOW_CONTROL_40x	0x00000001
/*
 * Cell is an EMAC4
 */
#define EMAC_FTR_EMAC4			0x00000002
/*
 * For the 440SPe, AMCC inexplicably changed the polarity of
 * the "operation complete" bit in the MII control register.
 */
#define EMAC_FTR_STACR_OC_INVERT	0x00000004
/*
 * Set if we have a TAH.
 */
#define EMAC_FTR_HAS_TAH		0x00000008
/*
 * Set if we have a ZMII.
 */
#define EMAC_FTR_HAS_ZMII		0x00000010
/*
 * Set if we have a RGMII.
 */
#define EMAC_FTR_HAS_RGMII		0x00000020
/*
 * Set if we have axon-type STACR
 */
#define EMAC_FTR_HAS_AXON_STACR		0x00000040


/* Right now, we don't quite handle the always/possible masks on the
 * most optimal way as we don't have a way to say something like
 * always EMAC4. Patches welcome.
 */
enum {
	EMAC_FTRS_ALWAYS	= 0,

	EMAC_FTRS_POSSIBLE	=
#ifdef CONFIG_IBM_NEW_EMAC_EMAC4
	    EMAC_FTR_EMAC4	| EMAC_FTR_HAS_AXON_STACR	|
	    EMAC_FTR_STACR_OC_INVERT	|
#endif
#ifdef CONFIG_IBM_NEW_EMAC_TAH
	    EMAC_FTR_HAS_TAH	|
#endif
#ifdef CONFIG_IBM_NEW_EMAC_ZMII
	    EMAC_FTR_HAS_ZMII	|
#endif
#ifdef CONFIG_IBM_NEW_EMAC_RGMII
	    EMAC_FTR_HAS_RGMII	|
#endif
	    0,
};

static inline int emac_has_feature(struct emac_instance *dev,
				   unsigned long feature)
{
	return (EMAC_FTRS_ALWAYS & feature) ||
	       (EMAC_FTRS_POSSIBLE & dev->features & feature);
}


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

#endif /* __IBM_NEWEMAC_CORE_H */
