/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
#ifndef _RTL8822BU_HAL_H_
#define _RTL8822BU_HAL_H_

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

	/* undefine MAX_RECVBUF_SZ from rtl8822b_hal.h  */
	#ifdef MAX_RECVBUF_SZ
		#undef MAX_RECVBUF_SZ
	#endif

	/* recv_buffer must be large than usb agg size */
	#ifndef MAX_RECVBUF_SZ
		#ifdef PLATFORM_OS_CE
			#define MAX_RECVBUF_SZ (8192+1024)
		#else /* !PLATFORM_OS_CE */
			#ifndef CONFIG_MINIMAL_MEMORY_USAGE
				#define MAX_RECVBUF_SZ (32768)
			#else
				#define MAX_RECVBUF_SZ (4000)
			#endif
		#endif /* PLATFORM_OS_CE */
	#endif /* !MAX_RECVBUF_SZ */

	/* rtl8822bu_ops.c */
	void rtl8822bu_set_hal_ops(PADAPTER padapter);
	void rtl8822bu_set_hw_type(struct dvobj_priv *pdvobj);

	/* rtl8822bu_io.c */
	void rtl8822bu_set_intf_ops(struct _io_ops *pops);

#endif /* CONFIG_USB_HCI */


#endif /* _RTL8822BU_HAL_H_ */
