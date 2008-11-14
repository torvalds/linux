/*

    linux/usb.h compatibility header

    Copyright (C) 2003 Bernd Porr, Bernd.Porr@cn.stir.ac.uk

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __COMPAT_LINUX_USB_H_
#define __COMPAT_LINUX_USB_H_

#include <linux/version.h>
#include <linux/time.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#include <linux/kernel.h>

#define USB_ALLOC_URB(x) usb_alloc_urb(x)
#define USB_SUBMIT_URB(x) usb_submit_urb(x)
#define URB_ISO_ASAP USB_ISO_ASAP
#define PROBE_ERR_RETURN(x) NULL
#define usb_get_dev(x) (x)
#define usb_put_dev(x)
#define interface_to_usbdev(intf) NULL
#else
#define USB_ALLOC_URB(x) usb_alloc_urb(x,GFP_KERNEL)
#define USB_SUBMIT_URB(x) usb_submit_urb(x,GFP_ATOMIC)
#define PROBE_ERR_RETURN(x) (x)
#endif

#include <linux/usb.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,12)
static inline int USB_CONTROL_MSG(struct usb_device *dev, unsigned int pipe,
	__u8 request, __u8 requesttype, __u16 value, __u16 index,
	void *data, __u16 size, int millisec_timeout)
{
	return usb_control_msg(dev, pipe, request, requesttype, value, index,
		data, size, msecs_to_jiffies(millisec_timeout));
}
static inline int USB_BULK_MSG(struct usb_device *usb_dev, unsigned int pipe,
	void *data, int len, int *actual_length, int millisec_timeout)
{
	return usb_bulk_msg(usb_dev, pipe, data, len, actual_length,
		msecs_to_jiffies(millisec_timeout));
}
#else
#define USB_CONTROL_MSG usb_control_msg
#define USB_BULK_MSG usb_bulk_msg
#endif

/*
 * Determine whether we need the "owner" member of struct usb_driver and
 * define COMEDI_HAVE_USB_DRIVER_OWNER if we need it.
 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,4,19) \
       && LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
#define COMEDI_HAVE_USB_DRIVER_OWNER
#endif

#endif
