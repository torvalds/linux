/*****************************************************************************
 *                                                                           *
 * File: cxgb2.h                                                             *
 * $Revision: 1.8 $                                                          *
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

#ifndef __CXGB_LINUX_H__
#define __CXGB_LINUX_H__

#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/version.h>
#include <asm/semaphore.h>
#include <asm/bitops.h>

/* This belongs in if_ether.h */
#define ETH_P_CPL5 0xf

struct cmac;
struct cphy;

struct port_info {
	struct net_device *dev;
	struct cmac *mac;
	struct cphy *phy;
	struct link_config link_config;
	struct net_device_stats netstats;
};

struct cxgbdev;
struct t1_sge;
struct pemc3;
struct pemc4;
struct pemc5;
struct peulp;
struct petp;
struct pecspi;
struct peespi;
struct work_struct;
struct vlan_group;

enum {					/* adapter flags */
	FULL_INIT_DONE        = 0x1,
	USING_MSI             = 0x2,
	TSO_CAPABLE           = 0x4,
	TCP_CSUM_CAPABLE      = 0x8,
	UDP_CSUM_CAPABLE      = 0x10,
	VLAN_ACCEL_CAPABLE    = 0x20,
	RX_CSUM_ENABLED       = 0x40,
};

struct adapter {
	u8 *regs;
	struct pci_dev *pdev;
	unsigned long registered_device_map;
	unsigned long open_device_map;
	unsigned int flags;

	const char *name;
	int msg_enable;
	u32 mmio_len;

	struct work_struct ext_intr_handler_task;
	struct adapter_params params;

	struct vlan_group *vlan_grp;

	/* Terminator modules. */
	struct sge    *sge;
	struct pemc3  *mc3;
	struct pemc4  *mc4;
	struct pemc5  *mc5;
	struct petp   *tp;
	struct pecspi *cspi;
	struct peespi *espi;
	struct peulp  *ulp;

	struct port_info port[MAX_NPORTS];
	struct work_struct stats_update_task;
	struct timer_list stats_update_timer;

	struct semaphore mib_mutex;
	spinlock_t tpi_lock;
	spinlock_t work_lock;

	spinlock_t async_lock ____cacheline_aligned; /* guards async operations */
	u32 slow_intr_mask;
};

#endif
