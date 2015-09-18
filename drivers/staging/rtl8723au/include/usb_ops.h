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
#ifndef __USB_OPS_H_
#define __USB_OPS_H_

#include <osdep_service.h>
#include <drv_types.h>
#include <osdep_intf.h>
#include <usb_ops_linux.h>

#define REALTEK_USB_VENQT_READ		0xC0
#define REALTEK_USB_VENQT_WRITE		0x40
#define REALTEK_USB_VENQT_CMD_REQ	0x05
#define REALTEK_USB_VENQT_CMD_IDX	0x00

enum {
	VENDOR_WRITE = 0x00,
	VENDOR_READ = 0x01,
};

#define ALIGNMENT_UNIT				16
#define MAX_VENDOR_REQ_CMD_SIZE	254		/* 8188cu SIE Support */
#define MAX_USB_IO_CTL_SIZE	(MAX_VENDOR_REQ_CMD_SIZE +ALIGNMENT_UNIT)

void rtl8723au_set_hw_type(struct rtw_adapter *padapter);

void rtl8723au_recv_tasklet(void *priv);

void rtl8723au_xmit_tasklet(void *priv);

/* Increase and check if the continual_urb_error of this @param dvobjprive is
 * larger than MAX_CONTINUAL_URB_ERR. Return result
 */
static inline int rtw_inc_and_chk_continual_urb_error(struct dvobj_priv *dvobj)
{
	int ret = false;
	int value;

	value = atomic_inc_return(&dvobj->continual_urb_error);
	if (value > MAX_CONTINUAL_URB_ERR) {
		DBG_8723A("[dvobj:%p][ERROR] continual_urb_error:%d > %d\n",
			  dvobj, value, MAX_CONTINUAL_URB_ERR);
		ret = true;
	}
	return ret;
}

/* Set the continual_urb_error of this @param dvobjprive to 0 */
static inline void rtw_reset_continual_urb_error(struct dvobj_priv *dvobj)
{
	atomic_set(&dvobj->continual_urb_error, 0);
}

bool rtl8723au_chip_configure(struct rtw_adapter *padapter);

#endif /* __USB_OPS_H_ */
