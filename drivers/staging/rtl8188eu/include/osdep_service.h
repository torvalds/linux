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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __OSDEP_SERVICE_H_
#define __OSDEP_SERVICE_H_

#include <basic_types.h>

#define _FAIL		0
#define _SUCCESS	1
#define RTW_RX_HANDLED	2

#include <linux/spinlock.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/circ_buf.h>
#include <linux/uaccess.h>
#include <asm/byteorder.h>
#include <linux/atomic.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/sem.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>	/*  Necessary because we use the proc fs */
#include <linux/interrupt.h>	/*  for struct tasklet_struct */
#include <linux/ip.h>
#include <linux/kthread.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>

struct	__queue	{
	struct	list_head	queue;
	spinlock_t lock;
};

static inline struct list_head *get_list_head(struct __queue *queue)
{
	return &(queue->queue);
}

static inline int rtw_netif_queue_stopped(struct net_device *pnetdev)
{
	return  netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 0)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 1)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 2)) &&
		netif_tx_queue_stopped(netdev_get_tx_queue(pnetdev, 3));
}

int RTW_STATUS_CODE(int error_code);

#define rtw_update_mem_stat(flag, sz) do {} while (0)
u8 *_rtw_malloc(u32 sz);
#define rtw_malloc(sz)			_rtw_malloc((sz))

void *rtw_malloc2d(int h, int w, int size);

u32  _rtw_down_sema(struct semaphore *sema);

void _rtw_init_queue(struct __queue *pqueue);

s32  rtw_get_passing_time_ms(u32 start);

struct rtw_netdev_priv_indicator {
	void *priv;
	u32 sizeof_priv;
};
struct net_device *rtw_alloc_etherdev_with_old_priv(int sizeof_priv,
						    void *old_priv);

#define rtw_netdev_priv(netdev)					\
	(((struct rtw_netdev_priv_indicator *)netdev_priv(netdev))->priv)
void rtw_free_netdev(struct net_device *netdev);

#define NDEV_FMT "%s"
#define NDEV_ARG(ndev) ndev->name
#define ADPT_FMT "%s"
#define ADPT_ARG(adapter) adapter->pnetdev->name
#define FUNC_NDEV_FMT "%s(%s)"
#define FUNC_NDEV_ARG(ndev) __func__, ndev->name
#define FUNC_ADPT_FMT "%s(%s)"
#define FUNC_ADPT_ARG(adapter) __func__, adapter->pnetdev->name

u64 rtw_modular64(u64 x, u64 y);

/* Macros for handling unaligned memory accesses */

#define RTW_GET_BE24(a) ((((u32)(a)[0]) << 16) | (((u32) (a)[1]) << 8) | \
			 ((u32)(a)[2]))

void rtw_buf_free(u8 **buf, u32 *buf_len);
void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len);
#endif
