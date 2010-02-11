/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*                                                                      */
/*  Module Name : wrap_mis.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for misc functions        */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <net/iw_handler.h>

/* extern struct zsWdsStruct wds[ZM_WDS_PORT_NUMBER];	*/
extern struct zsVapStruct vap[ZM_VAP_PORT_NUMBER];
extern u16_t zfLnxGetVapId(zdev_t *dev);

/* Simply return 0xffff if VAP function is not supported */
u16_t zfwGetVapId(zdev_t *dev)
{
	return zfLnxGetVapId(dev);
}

void zfwSleep(zdev_t *dev, u32_t ms)
{
	if (in_interrupt() == 0)
		mdelay(ms);
	else {
		int ii;
		int iter = 100000 * ms;

		for (ii = 0; ii < iter; ii++) {
		}
	}
}

#ifdef ZM_HALPLUS_LOCK
asmlinkage struct zsWlanDev *zfwGetWlanDev(zdev_t *dev)
{
	struct usbdrv_private *macp = dev->ml_priv;
	return macp->wd;
}

asmlinkage void zfwEnterCriticalSection(zdev_t *dev)
{
	struct usbdrv_private *macp = dev->ml_priv;
	spin_lock_irqsave(&macp->cs_lock, macp->hal_irqFlag);
}

asmlinkage void zfwLeaveCriticalSection(zdev_t *dev)
{
	struct usbdrv_private *macp = dev->ml_priv;
	spin_unlock_irqrestore(&macp->cs_lock, macp->hal_irqFlag);
}

asmlinkage u8_t zfwBufReadByte(zdev_t *dev, zbuf_t *buf, u16_t offset)
{
	return *(u8_t *)((u8_t *)buf->data+offset);
}

asmlinkage u16_t zfwBufReadHalfWord(zdev_t *dev, zbuf_t *buf, u16_t offset)
{
	return zmw_cpu_to_le16(*(u16_t *)((u8_t *)buf->data+offset));
}

asmlinkage void zfwBufWriteByte(zdev_t *dev, zbuf_t *buf, u16_t offset,
				u8_t value)
{
	*(u8_t *)((u8_t *)buf->data+offset) = value;
}

asmlinkage void zfwBufWriteHalfWord(zdev_t *dev, zbuf_t *buf, u16_t offset,
					u16_t value)
{
	*(u16_t *)((u8_t *)buf->data+offset) = zmw_cpu_to_le16(value);
}

asmlinkage u8_t *zfwGetBuffer(zdev_t *dev, zbuf_t *buf)
{
	return (u8_t *)(buf->data);
}
#endif

/* Leave an empty line below to remove warning message on some compiler */
