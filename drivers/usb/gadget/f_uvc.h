/*
 *	f_uvc.h  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 */

#ifndef _F_UVC_H_
#define _F_UVC_H_

#include <linux/usb/composite.h>
#include <linux/usb/video.h>

int uvc_bind_config(struct usb_configuration *c,
		    const struct uvc_descriptor_header * const *fs_control,
		    const struct uvc_descriptor_header * const *hs_control,
		    const struct uvc_descriptor_header * const *fs_streaming,
		    const struct uvc_descriptor_header * const *hs_streaming,
		    const struct uvc_descriptor_header * const *ss_streaming);

#endif /* _F_UVC_H_ */

