/*****************************************************************************
 *                                                                           *
 * File: osdep.h                                                             *
 * $Revision: 1.9 $                                                          *
 * $Date: 2005/03/23 07:41:27 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef __CHELSIO_OSDEP_H
#define __CHELSIO_OSDEP_H

#include <linux/version.h>
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <asm/io.h>

#include "cxgb2.h"

#define DRV_NAME "cxgb"
#define PFX      DRV_NAME ": "

#define CH_ERR(fmt, ...)   printk(KERN_ERR PFX fmt, ## __VA_ARGS__)
#define CH_WARN(fmt, ...)  printk(KERN_WARNING PFX fmt, ## __VA_ARGS__)
#define CH_ALERT(fmt, ...) printk(KERN_ALERT PFX fmt, ## __VA_ARGS__)

/*
 * More powerful macro that selectively prints messages based on msg_enable.
 * For info and debugging messages.
 */
#define CH_MSG(adapter, level, category, fmt, ...) do { \
	if ((adapter)->msg_enable & NETIF_MSG_##category) \
		printk(KERN_##level PFX "%s: " fmt, (adapter)->name, \
		       ## __VA_ARGS__); \
} while (0)

#ifdef DEBUG
# define CH_DBG(adapter, category, fmt, ...) \
	CH_MSG(adapter, DEBUG, category, fmt, ## __VA_ARGS__)
#else
# define CH_DBG(fmt, ...)
#endif

/* Additional NETIF_MSG_* categories */
#define NETIF_MSG_MMIO 0x8000000

#define CH_DEVICE(devid, ssid, idx) \
	{ PCI_VENDOR_ID_CHELSIO, devid, PCI_ANY_ID, ssid, 0, 0, idx }

#define SUPPORTED_PAUSE       (1 << 13)
#define SUPPORTED_LOOPBACK    (1 << 15)

#define ADVERTISED_PAUSE      (1 << 13)
#define ADVERTISED_ASYM_PAUSE (1 << 14)

/*
 * Now that we have included the driver's main data structure,
 * we typedef it to something the rest of the system understands.
 */
typedef struct adapter adapter_t;

#define TPI_LOCK(adapter) spin_lock(&(adapter)->tpi_lock)
#define TPI_UNLOCK(adapter) spin_unlock(&(adapter)->tpi_lock)

void t1_elmer0_ext_intr(adapter_t *adapter);
void t1_link_changed(adapter_t *adapter, int port_id, int link_status,
			int speed, int duplex, int fc);

static inline u16 t1_read_reg_2(adapter_t *adapter, u32 reg_addr)
{
	u16 val = readw(adapter->regs + reg_addr);

	CH_DBG(adapter, MMIO, "read register 0x%x value 0x%x\n", reg_addr,
	       val);
	return val;
}

static inline void t1_write_reg_2(adapter_t *adapter, u32 reg_addr, u16 val)
{
	CH_DBG(adapter, MMIO, "setting register 0x%x to 0x%x\n", reg_addr,
	       val);
	writew(val, adapter->regs + reg_addr);
}

static inline u32 t1_read_reg_4(adapter_t *adapter, u32 reg_addr)
{
	u32 val = readl(adapter->regs + reg_addr);

	CH_DBG(adapter, MMIO, "read register 0x%x value 0x%x\n", reg_addr,
	       val);
	return val;
}

static inline void t1_write_reg_4(adapter_t *adapter, u32 reg_addr, u32 val)
{
	CH_DBG(adapter, MMIO, "setting register 0x%x to 0x%x\n", reg_addr,
	       val);
	writel(val, adapter->regs + reg_addr);
}

static inline const char *port_name(adapter_t *adapter, int port_idx)
{
	return adapter->port[port_idx].dev->name;
}

static inline void t1_set_hw_addr(adapter_t *adapter, int port_idx,
				     u8 hw_addr[])
{
	memcpy(adapter->port[port_idx].dev->dev_addr, hw_addr, ETH_ALEN);
}

struct t1_rx_mode {
	struct net_device *dev;
	u32 idx;
	struct dev_mc_list *list;
};

#define t1_rx_mode_promisc(rm)	(rm->dev->flags & IFF_PROMISC)
#define t1_rx_mode_allmulti(rm)	(rm->dev->flags & IFF_ALLMULTI)
#define t1_rx_mode_mc_cnt(rm)	(rm->dev->mc_count)

static inline u8 *t1_get_next_mcaddr(struct t1_rx_mode *rm)
{
	u8 *addr = 0;

	if (rm->idx++ < rm->dev->mc_count) {
		addr = rm->list->dmi_addr;
		rm->list = rm->list->next;
	}
	return addr;
}

#endif
