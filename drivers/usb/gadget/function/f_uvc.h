// SPDX-License-Identifier: GPL-2.0+
/*
 *	f_uvc.h  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef _F_UVC_H_
#define _F_UVC_H_

struct uvc_device;

void uvc_function_setup_continue(struct uvc_device *uvc);

void uvc_function_connect(struct uvc_device *uvc);

void uvc_function_disconnect(struct uvc_device *uvc);

#endif /* _F_UVC_H_ */
