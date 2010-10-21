/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include "osd.h"
#include "vmbus_private.h"

static int ivmbus_open(struct hv_device *device, u32 sendbuffer_size,
			     u32 recv_ringbuffer_size, void *userdata,
			     u32 userdatalen,
			     void (*channel_callback)(void *context),
			     void *context)
{
	return vmbus_open(device->channel, sendbuffer_size,
				recv_ringbuffer_size, userdata, userdatalen,
				channel_callback, context);
}

static void ivmbus_close(struct hv_device *device)
{
	vmbus_close(device->channel);
}

/* vmbus interface function pointer table */
const struct vmbus_channel_interface vmbus_ops = {
	.Open = ivmbus_open,
	.Close = ivmbus_close,
};
