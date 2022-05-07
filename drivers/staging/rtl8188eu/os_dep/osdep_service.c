// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _OSDEP_SERVICE_C_

#include <osdep_service.h>
#include <osdep_intf.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <linux/vmalloc.h>
#include <rtw_ioctl_set.h>

u8 *_rtw_malloc(u32 sz)
{
	return kmalloc(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
}

void _rtw_init_queue(struct __queue *pqueue)
{
	INIT_LIST_HEAD(&pqueue->queue);
	spin_lock_init(&pqueue->lock);
}

struct net_device *rtw_alloc_etherdev_with_old_priv(void *old_priv)
{
	struct net_device *netdev;
	struct rtw_netdev_priv_indicator *pnpi;

	netdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
	if (!netdev)
		return NULL;

	pnpi = netdev_priv(netdev);
	pnpi->priv = old_priv;

	return netdev;
}

void rtw_free_netdev(struct net_device *netdev)
{
	struct rtw_netdev_priv_indicator *pnpi;

	if (!netdev)
		return;

	pnpi = netdev_priv(netdev);

	if (!pnpi->priv)
		return;

	vfree(pnpi->priv);
	free_netdev(netdev);
}

void rtw_buf_free(u8 **buf, u32 *buf_len)
{
	*buf_len = 0;
	kfree(*buf);
	*buf = NULL;
}

void rtw_buf_update(u8 **buf, u32 *buf_len, u8 *src, u32 src_len)
{
	u32 dup_len = 0;
	u8 *ori = NULL;
	u8 *dup = NULL;

	if (!buf || !buf_len)
		return;

	if (!src || !src_len)
		goto keep_ori;

	/* duplicate src */
	dup = rtw_malloc(src_len);
	if (dup) {
		dup_len = src_len;
		memcpy(dup, src, dup_len);
	}

keep_ori:
	ori = *buf;

	/* replace buf with dup */
	*buf_len = 0;
	*buf = dup;
	*buf_len = dup_len;

	/* free ori */
	kfree(ori);
}
