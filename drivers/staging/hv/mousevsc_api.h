/*
 *  Copyright 2009 Citrix Systems, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  For clarity, the licensor of this program does not intend that a
 *  "derivative work" include code which compiles header information from
 *  this program. 
 *
 *  This code has been modified from its original by 
 *  Hank Janssen <hjanssen@microsoft.com>
 *
 */

#ifndef _INPUTVSC_API_H_
#define _INPUTVSC_API_H_

#include "vmbus_api.h"

/*
 * Defines
 */
#define INPUTVSC_SEND_RING_BUFFER_SIZE		10*PAGE_SIZE
#define INPUTVSC_RECV_RING_BUFFER_SIZE		10*PAGE_SIZE


/*
 * Data types
 */
struct input_dev_info {
	unsigned short VendorID;
	unsigned short ProductID;
	unsigned short VersionNumber;
	char	       Name[128];
};

/* Represents the input vsc driver */
struct mousevsc_drv_obj {
	struct hv_driver Base; // Must be the first field
	/*
	 * This is set by the caller to allow us to callback when 
	 * we receive a packet from the "wire"
	 */
	void (*OnDeviceInfo)(struct hv_device *dev, 
			     struct input_dev_info* info);
	void (*OnInputReport)(struct hv_device *dev, void* packet, u32 len);
	void (*OnReportDescriptor)(struct hv_device *dev, 
				   void* packet, u32 len);
	/* Specific to this driver */
	int (*OnOpen)(struct hv_device *Device);
	int (*OnClose)(struct hv_device *Device);
	void *Context;
};


/*
 * Interface
 */
int mouse_vsc_initialize(struct hv_driver *drv);

#endif // _INPUTVSC_API_H_
