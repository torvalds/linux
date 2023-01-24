/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __USB_OPS_H_
#define __USB_OPS_H_

#include "osdep_service.h"
#include "drv_types.h"
#include "osdep_intf.h"

#define REALTEK_USB_VENQT_READ		(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define REALTEK_USB_VENQT_WRITE		(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE)
#define REALTEK_USB_VENQT_CMD_REQ	0x05
#define REALTEK_USB_VENQT_CMD_IDX	0x00

#define ALIGNMENT_UNIT			16
#define MAX_VENDOR_REQ_CMD_SIZE	254	/* 8188cu SIE Support */
#define MAX_USB_IO_CTL_SIZE	(MAX_VENDOR_REQ_CMD_SIZE + ALIGNMENT_UNIT)

/*
 * Increase and check if the continual_urb_error of this @param dvobjprivei
 * is larger than MAX_CONTINUAL_URB_ERR
 * @return true:
 * @return false:
 */
static inline bool rtw_inc_and_chk_continual_urb_error(struct dvobj_priv *dvobj)
{
	int value = atomic_inc_return(&dvobj->continual_urb_error);

	if (value > MAX_CONTINUAL_URB_ERR)
		return true;

	return false;
}

/*
* Set the continual_urb_error of this @param dvobjprive to 0
*/
static inline void rtw_reset_continual_urb_error(struct dvobj_priv *dvobj)
{
	atomic_set(&dvobj->continual_urb_error, 0);
}

#define USB_HIGH_SPEED_BULK_SIZE	512
#define USB_FULL_SPEED_BULK_SIZE	64

static inline bool rtw_usb_bulk_size_boundary(struct adapter *padapter, int buf_len)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->pusbdev->speed == USB_SPEED_HIGH)
		return buf_len % USB_HIGH_SPEED_BULK_SIZE == 0;
	else
		return buf_len % USB_FULL_SPEED_BULK_SIZE == 0;
}

#endif /* __USB_OPS_H_ */
