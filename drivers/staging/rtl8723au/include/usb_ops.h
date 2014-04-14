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

#define rtw_usb_control_msg(dev, pipe, request, requesttype, value,	\
			    index, data, size, timeout_ms)		\
	usb_control_msg((dev), (pipe), (request), (requesttype),	\
			(value), (index), (data), (size), (timeout_ms))
#define rtw_usb_bulk_msg(usb_dev, pipe, data, len, actual_length, timeout_ms) \
	usb_bulk_msg((usb_dev), (pipe), (data), (len), (actual_length),	\
		     (timeout_ms))

void rtl8723au_set_hw_type(struct rtw_adapter *padapter);
#define hal_set_hw_type rtl8723au_set_hw_type

void rtl8723au_set_intf_ops(struct _io_ops *pops);
#define usb_set_intf_ops rtl8723au_set_intf_ops

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

#define USB_HIGH_SPEED_BULK_SIZE	512
#define USB_FULL_SPEED_BULK_SIZE	64

static inline u8 rtw_usb_bulk_size_boundary(struct rtw_adapter *padapter,
					    int buf_len)
{
	u8 rst = true;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed)
		rst = (0 == (buf_len) % USB_HIGH_SPEED_BULK_SIZE) ?
		      true : false;
	else
		rst = (0 == (buf_len) % USB_FULL_SPEED_BULK_SIZE) ?
		      true : false;
	return rst;
}


#endif /* __USB_OPS_H_ */
