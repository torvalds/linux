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
	return vmbus_open(device->context, sendbuffer_size,
				recv_ringbuffer_size, userdata, userdatalen,
				channel_callback, context);
}

static void ivmbus_close(struct hv_device *device)
{
	vmbus_close(device->context);
}

static int ivmbus_sendpacket(struct hv_device *device, const void *buffer,
				   u32 bufferlen, u64 requestid, u32 type,
				   u32 flags)
{
	return vmbus_sendpacket(device->context, buffer, bufferlen,
				      requestid, type, flags);
}

static int ivmbus_sendpacket_pagebuffer(struct hv_device *device,
				struct hv_page_buffer pagebuffers[],
				u32 pagecount, void *buffer,
				u32 bufferlen, u64 requestid)
{
	return vmbus_sendpacket_pagebuffer(device->context, pagebuffers,
						pagecount, buffer, bufferlen,
						requestid);
}

static int ivmbus_sendpacket_multipagebuffer(struct hv_device *device,
				struct hv_multipage_buffer *multi_pagebuffer,
				void *buffer, u32 bufferlen, u64 requestid)
{
	return vmbus_sendpacket_multipagebuffer(device->context,
						     multi_pagebuffer, buffer,
						     bufferlen, requestid);
}

static int ivmbus_recvpacket(struct hv_device *device, void *buffer,
				   u32 bufferlen, u32 *buffer_actuallen,
				   u64 *requestid)
{
	return vmbus_recvpacket(device->context, buffer, bufferlen,
				      buffer_actuallen, requestid);
}

static int ivmbus_recvpacket_raw(struct hv_device *device, void *buffer,
				      u32 bufferlen, u32 *buffer_actuallen,
				      u64 *requestid)
{
	return vmbus_recvpacket_raw(device->context, buffer, bufferlen,
					 buffer_actuallen, requestid);
}

static int ivmbus_establish_gpadl(struct hv_device *device, void *buffer,
				       u32 bufferlen, u32 *gpadl_handle)
{
	return vmbus_establish_gpadl(device->context, buffer, bufferlen,
					  gpadl_handle);
}

static int ivmbus_teardown_gpadl(struct hv_device *device,
				      u32 gpadl_handle)
{
	return vmbus_teardown_gpadl(device->context, gpadl_handle);

}

/* vmbus interface function pointer table */
const struct vmbus_channel_interface vmbus_ops = {
	.Open = ivmbus_open,
	.Close = ivmbus_close,
	.SendPacket = ivmbus_sendpacket,
	.SendPacketPageBuffer = ivmbus_sendpacket_pagebuffer,
	.SendPacketMultiPageBuffer = ivmbus_sendpacket_multipagebuffer,
	.RecvPacket = ivmbus_recvpacket,
	.RecvPacketRaw	= ivmbus_recvpacket_raw,
	.EstablishGpadl = ivmbus_establish_gpadl,
	.TeardownGpadl = ivmbus_teardown_gpadl,
};
