/*
 * Blackfin On-Chip MAC Driver
 *
 * Copyright 2004-2007 Analog Devices Inc.
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef _BFIN_MAC_H_
#define _BFIN_MAC_H_

#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timer.h>
#include <linux/etherdevice.h>
#include <linux/bfin_mac.h>

/*
 * Disable hardware checksum for bug #5600 if writeback cache is
 * enabled. Otherwize, corrupted RX packet will be sent up stack
 * without error mark.
 */
#ifndef CONFIG_BFIN_EXTMEM_WRITEBACK
#define BFIN_MAC_CSUM_OFFLOAD
#endif

#define TX_RECLAIM_JIFFIES (HZ / 5)
#define BFIN_MAC_RX_IRQ_DISABLED	1

struct dma_descriptor {
	struct dma_descriptor *next_dma_desc;
	unsigned long start_addr;
	unsigned short config;
	unsigned short x_count;
};

struct status_area_rx {
#if defined(BFIN_MAC_CSUM_OFFLOAD)
	unsigned short ip_hdr_csum;	/* ip header checksum */
	/* ip payload(udp or tcp or others) checksum */
	unsigned short ip_payload_csum;
#endif
	unsigned long status_word;	/* the frame status word */
};

struct status_area_tx {
	unsigned long status_word;	/* the frame status word */
};

/* use two descriptors for a packet */
struct net_dma_desc_rx {
	struct net_dma_desc_rx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	struct status_area_rx status;
};

/* use two descriptors for a packet */
struct net_dma_desc_tx {
	struct net_dma_desc_tx *next;
	struct sk_buff *skb;
	struct dma_descriptor desc_a;
	struct dma_descriptor desc_b;
	unsigned char packet[1560];
	struct status_area_tx status;
};

struct bfin_mac_local {
	spinlock_t lock;

	int wol;		/* Wake On Lan */
	int irq_wake_requested;
	struct timer_list tx_reclaim_timer;
	struct net_device *ndev;
	struct napi_struct napi;
	unsigned long flags;

	/* Data for EMAC_VLAN1 regs */
	u16 vlan1_mask, vlan2_mask;

	/* MII and PHY stuffs */
	int old_link;          /* used by bf537_adjust_link */
	int old_speed;
	int old_duplex;

	struct mii_bus *mii_bus;

#if defined(CONFIG_BFIN_MAC_USE_HWSTAMP)
	u32 addend;
	unsigned int shift;
	s32 max_ppb;
	struct hwtstamp_config stamp_cfg;
	struct ptp_clock_info caps;
	struct ptp_clock *clock;
	int phc_index;
	spinlock_t phc_lock; /* protects time lo/hi registers */
#endif
};

int bfin_get_ether_addr(char *addr);

#endif
