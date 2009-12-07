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
#include "VmbusPrivate.h"

static int IVmbusChannelOpen(struct hv_device *device, u32 SendBufferSize,
			     u32 RecvRingBufferSize, void *UserData,
			     u32 UserDataLen,
			     void (*ChannelCallback)(void *context),
			     void *Context)
{
	return VmbusChannelOpen(device->context, SendBufferSize,
				RecvRingBufferSize, UserData, UserDataLen,
				ChannelCallback, Context);
}

static void IVmbusChannelClose(struct hv_device *device)
{
	VmbusChannelClose(device->context);
}

static int IVmbusChannelSendPacket(struct hv_device *device, const void *Buffer,
				   u32 BufferLen, u64 RequestId, u32 Type,
				   u32 Flags)
{
	return VmbusChannelSendPacket(device->context, Buffer, BufferLen,
				      RequestId, Type, Flags);
}

static int IVmbusChannelSendPacketPageBuffer(struct hv_device *device,
				struct hv_page_buffer PageBuffers[],
				u32 PageCount, void *Buffer,
				u32 BufferLen, u64 RequestId)
{
	return VmbusChannelSendPacketPageBuffer(device->context, PageBuffers,
						PageCount, Buffer, BufferLen,
						RequestId);
}

static int IVmbusChannelSendPacketMultiPageBuffer(struct hv_device *device,
				struct hv_multipage_buffer *MultiPageBuffer,
				void *Buffer, u32 BufferLen, u64 RequestId)
{
	return VmbusChannelSendPacketMultiPageBuffer(device->context,
						     MultiPageBuffer, Buffer,
						     BufferLen, RequestId);
}

static int IVmbusChannelRecvPacket(struct hv_device *device, void *Buffer,
				   u32 BufferLen, u32 *BufferActualLen,
				   u64 *RequestId)
{
	return VmbusChannelRecvPacket(device->context, Buffer, BufferLen,
				      BufferActualLen, RequestId);
}

static int IVmbusChannelRecvPacketRaw(struct hv_device *device, void *Buffer,
				      u32 BufferLen, u32 *BufferActualLen,
				      u64 *RequestId)
{
	return VmbusChannelRecvPacketRaw(device->context, Buffer, BufferLen,
					 BufferActualLen, RequestId);
}

static int IVmbusChannelEstablishGpadl(struct hv_device *device, void *Buffer,
				       u32 BufferLen, u32 *GpadlHandle)
{
	return VmbusChannelEstablishGpadl(device->context, Buffer, BufferLen,
					  GpadlHandle);
}

static int IVmbusChannelTeardownGpadl(struct hv_device *device, u32 GpadlHandle)
{
	return VmbusChannelTeardownGpadl(device->context, GpadlHandle);

}

void GetChannelInterface(struct vmbus_channel_interface *iface)
{
	iface->Open = IVmbusChannelOpen;
	iface->Close	= IVmbusChannelClose;
	iface->SendPacket = IVmbusChannelSendPacket;
	iface->SendPacketPageBuffer = IVmbusChannelSendPacketPageBuffer;
	iface->SendPacketMultiPageBuffer =
					IVmbusChannelSendPacketMultiPageBuffer;
	iface->RecvPacket = IVmbusChannelRecvPacket;
	iface->RecvPacketRaw	= IVmbusChannelRecvPacketRaw;
	iface->EstablishGpadl = IVmbusChannelEstablishGpadl;
	iface->TeardownGpadl = IVmbusChannelTeardownGpadl;
	iface->GetInfo = GetChannelInfo;
}

void GetChannelInfo(struct hv_device *device, struct hv_device_info *info)
{
	struct vmbus_channel_debug_info debugInfo;

	if (!device->context)
		return;

	VmbusChannelGetDebugInfo(device->context, &debugInfo);

	info->ChannelId = debugInfo.RelId;
	info->ChannelState = debugInfo.State;
	memcpy(&info->ChannelType, &debugInfo.InterfaceType,
	       sizeof(struct hv_guid));
	memcpy(&info->ChannelInstance, &debugInfo.InterfaceInstance,
	       sizeof(struct hv_guid));

	info->MonitorId = debugInfo.MonitorId;

	info->ServerMonitorPending = debugInfo.ServerMonitorPending;
	info->ServerMonitorLatency = debugInfo.ServerMonitorLatency;
	info->ServerMonitorConnectionId = debugInfo.ServerMonitorConnectionId;

	info->ClientMonitorPending = debugInfo.ClientMonitorPending;
	info->ClientMonitorLatency = debugInfo.ClientMonitorLatency;
	info->ClientMonitorConnectionId = debugInfo.ClientMonitorConnectionId;

	info->Inbound.InterruptMask = debugInfo.Inbound.CurrentInterruptMask;
	info->Inbound.ReadIndex = debugInfo.Inbound.CurrentReadIndex;
	info->Inbound.WriteIndex = debugInfo.Inbound.CurrentWriteIndex;
	info->Inbound.BytesAvailToRead = debugInfo.Inbound.BytesAvailToRead;
	info->Inbound.BytesAvailToWrite = debugInfo.Inbound.BytesAvailToWrite;

	info->Outbound.InterruptMask = debugInfo.Outbound.CurrentInterruptMask;
	info->Outbound.ReadIndex = debugInfo.Outbound.CurrentReadIndex;
	info->Outbound.WriteIndex = debugInfo.Outbound.CurrentWriteIndex;
	info->Outbound.BytesAvailToRead = debugInfo.Outbound.BytesAvailToRead;
	info->Outbound.BytesAvailToWrite = debugInfo.Outbound.BytesAvailToWrite;
}
