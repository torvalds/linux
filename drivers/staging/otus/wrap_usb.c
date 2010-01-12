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
/*  Module Name : wrap_usb.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for USB management        */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <net/iw_handler.h>

extern void zfLnxInitUsbTxQ(zdev_t *dev);
extern void zfLnxInitUsbRxQ(zdev_t *dev);
extern u32_t zfLnxSubmitRegInUrb(zdev_t *dev);
u32_t zfLnxUsbOut(zdev_t *dev, u8_t *hdr, u16_t hdrlen, u8_t *snap,
		u16_t snapLen, u8_t *tail, u16_t tailLen, zbuf_t *buf,
		u16_t offset);
u32_t zfLnxUsbWriteReg(zdev_t *dev, u32_t *cmd, u16_t cmdLen);

void zfwUsbRegisterCallBack(zdev_t *dev, struct zfCbUsbFuncTbl *zfUsbFunc)
{
	struct usbdrv_private *macp = dev->ml_priv;

	macp->usbCbFunctions.zfcbUsbRecv = zfUsbFunc->zfcbUsbRecv;
	macp->usbCbFunctions.zfcbUsbRegIn = zfUsbFunc->zfcbUsbRegIn;
	macp->usbCbFunctions.zfcbUsbOutComplete = zfUsbFunc->zfcbUsbOutComplete;

	return;
}

u32_t zfwUsbGetFreeTxQSize(zdev_t *dev)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u32_t		freeTxQSize;
	unsigned long irqFlag;
	/* zmw_declare_for_critical_section();	*/

	/* zmw_enter_critical_section(dev);	*/
	spin_lock_irqsave(&macp->cs_lock, irqFlag);

	freeTxQSize = ZM_MAX_TX_BUF_NUM - macp->TxBufCnt;

	/* zmw_leave_critical_section(dev);	*/
	spin_unlock_irqrestore(&macp->cs_lock, irqFlag);

	return freeTxQSize;
}

u32_t zfwUsbGetMaxTxQSize(zdev_t *dev)
{
	return ZM_MAX_TX_BUF_NUM;
}

u32_t zfwUsbEnableIntEpt(zdev_t *dev, u8_t endpt)
{
	/* Initialize USB TxQ */
	zfLnxInitUsbTxQ(dev);

	/* Initialize USB RxQ */
	zfLnxInitUsbRxQ(dev);

	/* Initialize USB Register In URB */
	/* zfwUsbSubmitRegIn(dev);	*/
	/* Initialize USB Register In URB */
	zfLnxSubmitRegInUrb(dev);

	return 0;
}

int zfwUsbEnableRxEpt(zdev_t *dev, u8_t endpt)
{
	return 0;
}

u32_t zfwUsbSubmitControl(zdev_t *dev, u8_t req, u16_t value, u16_t index,
			 void *data, u32_t size)
{
	int result = 0;
	u32_t ret = 0;
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t *buf;

	if (size > 0) {
		buf = kmalloc(size, GFP_KERNEL);
		memcpy(buf, (u8_t *)data, size);
	} else
		buf = NULL;

#if 0
	printk(KERN_ERR "req = 0x%02x\n", req);
	printk(KERN_ERR "value = 0x%04x\n", value);
	printk(KERN_ERR "index = 0x%04x\n", index);
	printk(KERN_ERR "data = 0x%lx\n", (u32_t) data);
	printk(KERN_ERR "size = %ld\n", size);
#endif

	result = usb_control_msg(macp->udev, usb_sndctrlpipe(macp->udev, 0),
			req, USB_DIR_OUT | 0x40, value, index, buf, size, HZ);

	if (result < 0) {
		printk(KERN_ERR "zfwUsbSubmitControl() failed, result = 0x%x\n",
			result);
		ret = 1;
	}
	kfree(buf);

	return ret;
}

void zfwUsbCmd(zdev_t *dev, u8_t endpt, u32_t *cmd, u16_t cmdLen)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u32_t ret;

	/* MPUsbCommand(dev, endpt, cmd, cmdLen);	*/
	ret = zfLnxUsbWriteReg(dev, cmd, cmdLen);

	/*
	 * if zfLnxUsbWriteReg() return error, free and allocate urb,
	 * resend again
	 */
	if (ret != 0) {
		usb_free_urb(macp->RegOutUrb);
		macp->RegOutUrb = usb_alloc_urb(0, GFP_ATOMIC);
		ret = zfLnxUsbWriteReg(dev, cmd, cmdLen);
	}
}

u32_t zfwUsbSend(zdev_t *dev, u8_t endpt, u8_t *hdr, u16_t hdrlen, u8_t *snap,
			u16_t snapLen,	u8_t *tail, u16_t tailLen,
			zbuf_t *buf, u16_t offset)
{
	u32_t status;

#ifdef ZM_CONFIG_BIG_ENDIAN
	u32_t ii = 0;
	u16_t *pc = NULL;

	pc = (u16_t *)hdr;
	for (ii = 0; ii < (hdrlen >> 1); ii++)
		pc[ii] = cpu_to_le16(pc[ii]);

	pc = (u16_t *)snap;
	for (ii = 0; ii < (snapLen >> 1); ii++)
		pc[ii] = cpu_to_le16(pc[ii]);

	pc = (u16_t *)tail;
	for (ii = 0; ii < (tailLen>>1); ii++)
		pc[ii] = cpu_to_le16(pc[ii]);
#endif

	status = zfLnxUsbOut(dev, hdr, hdrlen, snap, snapLen, tail, tailLen,
				buf, offset);
	if (status == 0)
		return 0;
	else
		return 1;
}

/* Leave an empty line below to remove warning message on some compiler */
