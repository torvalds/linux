/*
 * ibm_emac_core.h
 *
 * Ethernet driver for the built in ethernet on the IBM 405 PowerPC
 * processor.
 *
 *      Armin Kuster akuster@mvista.com
 *      Sept, 2001
 *
 *      Orignial driver
 *         Johnnie Peters
 *         jpeters@mvista.com
 *
 * Copyright 2000 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _IBM_EMAC_CORE_H_
#define _IBM_EMAC_CORE_H_

#include <linux/netdevice.h>
#include <asm/ocp.h>
#include <asm/mmu.h>		/* For phys_addr_t */

#include "ibm_emac.h"
#include "ibm_emac_phy.h"
#include "ibm_emac_rgmii.h"
#include "ibm_emac_zmii.h"
#include "ibm_emac_mal.h"
#include "ibm_emac_tah.h"

#ifndef CONFIG_IBM_EMAC_TXB
#define NUM_TX_BUFF		64
#define NUM_RX_BUFF		64
#else
#define NUM_TX_BUFF		CONFIG_IBM_EMAC_TXB
#define NUM_RX_BUFF		CONFIG_IBM_EMAC_RXB
#endif

/* This does 16 byte alignment, exactly what we need.
 * The packet length includes FCS, but we don't want to
 * include that when passing upstream as it messes up
 * bridging applications.
 */
#ifndef CONFIG_IBM_EMAC_SKBRES
#define SKB_RES 2
#else
#define SKB_RES CONFIG_IBM_EMAC_SKBRES
#endif

/* Note about alignement. alloc_skb() returns a cache line
 * aligned buffer. However, dev_alloc_skb() will add 16 more
 * bytes and "reserve" them, so our buffer will actually end
 * on a half cache line. What we do is to use directly
 * alloc_skb, allocate 16 more bytes to match the total amount
 * allocated by dev_alloc_skb(), but we don't reserve.
 */
#define MAX_NUM_BUF_DESC	255
#define DESC_BUF_SIZE		4080	/* max 4096-16 */
#define DESC_BUF_SIZE_REG	(DESC_BUF_SIZE / 16)

/* Transmitter timeout. */
#define TX_TIMEOUT		(2*HZ)

/* MDIO latency delay */
#define MDIO_DELAY		250

/* Power managment shift registers */
#define IBM_CPM_EMMII	0	/* Shift value for MII */
#define IBM_CPM_EMRX	1	/* Shift value for recv */
#define IBM_CPM_EMTX	2	/* Shift value for MAC */
#define IBM_CPM_EMAC(x)	(((x)>>IBM_CPM_EMMII) | ((x)>>IBM_CPM_EMRX) | ((x)>>IBM_CPM_EMTX))

#define ENET_HEADER_SIZE	14
#define ENET_FCS_SIZE		4
#define ENET_DEF_MTU_SIZE	1500
#define ENET_DEF_BUF_SIZE	(ENET_DEF_MTU_SIZE + ENET_HEADER_SIZE + ENET_FCS_SIZE)
#define EMAC_MIN_FRAME		64
#define EMAC_MAX_FRAME		9018
#define EMAC_MIN_MTU		(EMAC_MIN_FRAME - ENET_HEADER_SIZE - ENET_FCS_SIZE)
#define EMAC_MAX_MTU		(EMAC_MAX_FRAME - ENET_HEADER_SIZE - ENET_FCS_SIZE)

#ifdef CONFIG_IBM_EMAC_ERRMSG
void emac_serr_dump_0(struct net_device *dev);
void emac_serr_dump_1(struct net_device *dev);
void emac_err_dump(struct net_device *dev, int em0isr);
void emac_phy_dump(struct net_device *);
void emac_desc_dump(struct net_device *);
void emac_mac_dump(struct net_device *);
void emac_mal_dump(struct net_device *);
#else
#define emac_serr_dump_0(dev) do { } while (0)
#define emac_serr_dump_1(dev) do { } while (0)
#define emac_err_dump(dev,x) do { } while (0)
#define emac_phy_dump(dev) do { } while (0)
#define emac_desc_dump(dev) do { } while (0)
#define emac_mac_dump(dev) do { } while (0)
#define emac_mal_dump(dev) do { } while (0)
#endif

struct ocp_enet_private {
	struct sk_buff *tx_skb[NUM_TX_BUFF];
	struct sk_buff *rx_skb[NUM_RX_BUFF];
	struct mal_descriptor *tx_desc;
	struct mal_descriptor *rx_desc;
	struct mal_descriptor *rx_dirty;
	struct net_device_stats stats;
	int tx_cnt;
	int rx_slot;
	int dirty_rx;
	int tx_slot;
	int ack_slot;
	int rx_buffer_size;

	struct mii_phy phy_mii;
	int mii_phy_addr;
	int want_autoneg;
	int timer_ticks;
	struct timer_list link_timer;
	struct net_device *mdio_dev;

	struct ocp_device *rgmii_dev;
	int rgmii_input;

	struct ocp_device *zmii_dev;
	int zmii_input;

	struct ibm_ocp_mal *mal;
	int mal_tx_chan, mal_rx_chan;
	struct mal_commac commac;

	struct ocp_device *tah_dev;

	int opened;
	int going_away;
	int wol_irq;
	emac_t *emacp;
	struct ocp_device *ocpdev;
	struct net_device *ndev;
	spinlock_t lock;
};
#endif				/* _IBM_EMAC_CORE_H_ */
