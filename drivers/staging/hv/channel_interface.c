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

static int IVmbusChannelOpen(struct hv_device *device, u32 sendbuffer_size,
			     u32 recv_ringbuffer_size, void *userdata,
			     u32 userdatalen,
			     void (*channel_callback)(void *context),
			     void *context)
{
	return vmbus_open(device->context, sendbuffer_size,
				recv_ringbuffer_size, userdata, userdatalen,
				channel_callback, context);
}

static void IVmbusChannelClose(struct hv_device *device)
{
	vmbus_close(device->context);
}

static int IVmbusChannelSendPacket(struct hv_device *device, const void *buffer,
				   u32 bufferlen, u64 requestid, u32 type,
				   u32 flags)
{
	return vmbus_sendpacket(device->context, buffer, bufferlen,
				      requestid, type, flags);
}

static int IVmbusChannelSendPacketPageBuffer(struct hv_device *device,
				struct hv_page_buffer pagebuffers[],
				u32 pagecount, void *buffer,
				u32 bufferlen, u64 requestid)
{
	return vmbus_sendpacket_pagebuffer(device->context, pagebuffers,
						pagecount, buffer, bufferlen,
						requestid);
}

static int IVmbusChannelSendPacketMultiPageBuffer(struct hv_device *device,
				struct hv_multipage_buffer *multi_pagebuffer,
				void *buffer, u32 bufferlen, u64 requestid)
{
	return vmbus_sendpacket_multipagebuffer(device->context,
						     multi_pagebuffer, buffer,
						     bufferlen, requestid);
}

static int IVmbusChannelRecvPacket(struct hv_device *device, void *buffer,
				   u32 bufferlen, u32 *buffer_actuallen,
				   u64 *requestid)
{
	return vmbus_recvpacket(device->context, buffer, bufferlen,
				      buffer_actuallen, requestid);
}

static int IVmbusChannelRecvPacketRaw(struct hv_device *device, void *buffer,
				      u32 bufferlen, u32 *buffer_actuallen,
				      u64 *requestid)
{
	return vmbus_recvpacket_raw(device->context, buffer, bufferlen,
					 buffer_actuallen, requestid);
}

static int IVmbusChannelEstablishGpadl(struct hv_device *device, void *buffer,
				       u32 bufferlen, u32 *gpadl_handle)
{
	return vmbus_establish_gpadl(device->context, buffer, bufferlen,
					  gpadl_handle);
}

static int IVmbusChannelTeardownGpadl(struct hv_device *device,
				      u32 gpadl_handle)
{
	return vmbus_teardown_gpadl(device->context, gpadl_handle);

}


void GetChannelInfo(struct hv_device *device, struct hv_device_info *info)
{
	struct vmbus_channel_debug_info debug_info;

	if (!device->context)
		return;

	vmbus_get_debug_info(device->context, &debug_info);

	info->ChannelId = debug_info.RelId;
	info->ChannelState = debug_info.State;
	memcpy(&info->ChannelType, &debug_info.InterfaceType,
	       sizeof(struct hv_guid));
	memcpy(&info->ChannelInstance, &debug_info.InterfaceInstance,
	       sizeof(struct hv_guid));

	info->MonitorId = debug_info.MonitorId;

	info->ServerMonitorPending = debug_info.ServerMonitorPending;
	info->ServerMonitorLatency = debug_info.ServerMonitorLatency;
	info->ServerMonitorConnectionId = debug_info.ServerMonitorConnectionId;

	info->ClientMonitorPending = debug_info.ClientMonitorPending;
	info->ClientMonitorLatency = debug_info.ClientMonitorLatency;
	info->ClientMonitorConnectionId = debug_info.ClientMonitorConnectionId;

	info->Inbound.InterruptMask = debug_info.Inbound.CurrentInterruptMask;
	info->Inbound.ReadIndex = debug_info.Inbound.CurrentReadIndex;
	info->Inbound.WriteIndex = debug_info.Inbound.CurrentWriteIndex;
	info->Inbound.BytesAvailToRead = debug_info.Inbound.BytesAvailToRead;
	info->Inbound.BytesAvailToWrite = debug_info.Inbound.BytesAvailToWrite;

	info->Outbound.InterruptMask = debug_info.Outbound.CurrentInterruptMask;
	info->Outbound.ReadIndex = debug_info.Outbound.CurrentReadIndex;
	info->Outbound.WriteIndex = debug_info.Outbound.CurrentWriteIndex;
	info->Outbound.BytesAvailToRead = debug_info.Outbound.BytesAvailToRead;
	info->Outbound.BytesAvailToWrite =
		debug_info.Outbound.BytesAvailToWrite;
}


/* vmbus interface function pointer table */
const struct vmbus_channel_interface vmbus_ops = {
	.Open = IVmbusChannelOpen,
	.Close = IVmbusChannelClose,
	.SendPacket = IVmbusChannelSendPacket,
	.SendPacketPageBuffer = IVmbusChannelSendPacketPageBuffer,
	.SendPacketMultiPageBuffer = IVmbusChannelSendPacketMultiPageBuffer,
	.RecvPacket = IVmbusChannelRecvPacket,
	.RecvPacketRaw	= IVmbusChannelRecvPacketRaw,
	.EstablishGpadl = IVmbusChannelEstablishGpadl,
	.TeardownGpadl = IVmbusChannelTeardownGpadl,
	.GetInfo = GetChannelInfo,
};
