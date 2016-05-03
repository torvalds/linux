/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED 2

#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/interrupt.h>	/*  for struct tasklet_struct */
#include <linux/ip.h>

#include <net/ieee80211_radiotap.h>
#include <net/cfg80211.h>

struct rtw_queue {
	struct	list_head	queue;
	spinlock_t		lock;
};

static inline struct list_head *get_list_head(struct rtw_queue *queue)
{
	return &queue->queue;
}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return (netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3)));
}

static inline u32 CHKBIT(u32 x)
{
	WARN_ON(x >= 32);
	if (x >= 32)
		return 0;
	return BIT(x);
}

extern unsigned char MCS_rate_2R23A[16];

extern unsigned char	MCS_rate_2R23A[16];
extern unsigned char	MCS_rate_1R23A[16];

void	_rtw_init_queue23a(struct rtw_queue *pqueue);

/* Macros for handling unaligned memory accesses */

#define RTW_GET_BE24(a) ((((u32) (a)[0]) << 16) | (((u32) (a)[1]) << 8) | \
			 ((u32) (a)[2]))

#endif
