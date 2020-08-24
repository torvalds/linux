/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2015 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTL8822CU_HAL_H_
#define _RTL8822CU_HAL_H_

#ifdef CONFIG_USB_HCI
	#include <drv_types.h>		/* PADAPTER */

	#ifdef CONFIG_USB_HCI
		#ifdef USB_PACKET_OFFSET_SZ
			#define PACKET_OFFSET_SZ (USB_PACKET_OFFSET_SZ)
		#else
			#define PACKET_OFFSET_SZ (8)
		#endif
		#define TXDESC_OFFSET (TXDESC_SIZE + PACKET_OFFSET_SZ)
	#endif

	/* undefine MAX_RECVBUF_SZ from rtl8822c_hal.h  */
	#ifdef MAX_RECVBUF_SZ
		#undef MAX_RECVBUF_SZ
	#endif

	/* recv_buffer must be large than usb agg size */
	#ifndef MAX_RECVBUF_SZ
		#ifndef CONFIG_MINIMAL_MEMORY_USAGE
			#ifdef CONFIG_PLATFORM_NOVATEK_NT72668
				#define MAX_RECVBUF_SZ (15360) /* 15k */
				#elif defined(CONFIG_PLATFORM_HISILICON)
				/* use 16k to workaround for HISILICON platform */
				#define MAX_RECVBUF_SZ (16384)
			#else
				#define MAX_RECVBUF_SZ (32768)
			#endif
		#else
			#define MAX_RECVBUF_SZ (4000)
		#endif
	#endif /* !MAX_RECVBUF_SZ */

	/* rtl8822cu_ops.c */
	void rtl8822cu_set_hal_ops(PADAPTER padapter);
	void rtl8822cu_set_hw_type(struct dvobj_priv *pdvobj);

	/* rtl8822cu_io.c */
	void rtl8822cu_set_intf_ops(struct _io_ops *pops);

#endif /* CONFIG_USB_HCI */


#endif /* _RTL8822CU_HAL_H_ */
