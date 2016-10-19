/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _OSDEP_SERVICE_C_

#include <osdep_service.h>
#include <osdep_intf.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <linux/vmalloc.h>
#include <rtw_ioctl_set.h>

/*
* Translate the OS dependent @param error_code to OS independent RTW_STATUS_CODE
* @return: one of RTW_STATUS_CODE
*/
inline int RTW_STATUS_CODE(int error_code)
{
	if (error_code >= 0)
		return _SUCCESS;
	return _FAIL;
}

u8 *_rtw_malloc(u32 sz)
{
	return kmalloc(sz, in_interrupt() ? GFP_ATOMIC : GFP_KERNEL);
}

void *rtw_malloc2d(int h, int w, int size)
{
	int j;

	void **a = kzalloc(h*sizeof(void *) + h*w*size, GFP_KERNEL);
	if (!a) {
		pr_info("%s: alloc memory fail!\n", __func__);
		return NULL;
	}

	for (j = 0; j < h; j++)
		a[j] = ((char *)(a+h)) + j*w*size;

	return a;
}

void	_rtw_init_queue(struct __queue *pqueue)
{
	INIT_LIST_HEAD(&(pqueue->queue));
	spin_lock_init(&(pqueue->lock));
}

struct net_device *rtw_alloc_etherdev_with_old_priv(void *old_priv)
{
	struct net_device *pnetdev;
	struct rtw_netdev_priv_indicator *pnpi;

	pnetdev = alloc_etherdev_mq(sizeof(struct rtw_netdev_priv_indicator), 4);
	if (!pnetdev)
		goto RETURN;

	pnpi = netdev_priv(pnetdev);
	pnpi->priv = old_priv;

RETURN:
	return pnetdev;
}

void rtw_free_netdev(struct net_device *netdev)
{
	struct rtw_netdev_priv_indicator *pnpi;

	if (!netdev)
		goto RETURN;

	pnpi = netdev_priv(netdev);

	if (!pnpi->priv)
		goto RETURN;

	vfree(pnpi->priv);
	free_netdev(netdev);

RETURN:
	return;
}

u64 rtw_modular64(u64 x, u64 y)
{
	return do_div(x, y);
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
