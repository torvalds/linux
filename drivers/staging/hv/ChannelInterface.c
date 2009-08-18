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

#include "VmbusPrivate.h"

static int
IVmbusChannelOpen(
	struct hv_device *Device,
	u32				SendBufferSize,
	u32				RecvRingBufferSize,
	void *				UserData,
	u32				UserDataLen,
	VMBUS_CHANNEL_CALLBACK ChannelCallback,
	void *				Context
	)
{
	return VmbusChannelOpen( (VMBUS_CHANNEL*)Device->context,
								SendBufferSize,
								RecvRingBufferSize,
								UserData,
								UserDataLen,
								ChannelCallback,
								Context);
}


static void
IVmbusChannelClose(
	struct hv_device *Device
	)
{
	VmbusChannelClose((VMBUS_CHANNEL*)Device->context);
}


static int
IVmbusChannelSendPacket(
	struct hv_device *Device,
	const void *			Buffer,
	u32				BufferLen,
	u64				RequestId,
	u32				Type,
	u32				Flags
	)
{
	return VmbusChannelSendPacket((VMBUS_CHANNEL*)Device->context,
									Buffer,
									BufferLen,
									RequestId,
									Type,
									Flags);
}

static int
IVmbusChannelSendPacketPageBuffer(
	struct hv_device *Device,
	PAGE_BUFFER			PageBuffers[],
	u32				PageCount,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
	)
{
	return VmbusChannelSendPacketPageBuffer((VMBUS_CHANNEL*)Device->context,
												PageBuffers,
												PageCount,
												Buffer,
												BufferLen,
												RequestId);
}

static int
IVmbusChannelSendPacketMultiPageBuffer(
	struct hv_device *Device,
	MULTIPAGE_BUFFER	*MultiPageBuffer,
	void *				Buffer,
	u32				BufferLen,
	u64				RequestId
	)
{
	return VmbusChannelSendPacketMultiPageBuffer((VMBUS_CHANNEL*)Device->context,
													MultiPageBuffer,
													Buffer,
													BufferLen,
													RequestId);
}

static int
IVmbusChannelRecvPacket (
	struct hv_device *Device,
	void *				Buffer,
	u32				BufferLen,
	u32*				BufferActualLen,
	u64*				RequestId
	)
{
	return VmbusChannelRecvPacket((VMBUS_CHANNEL*)Device->context,
									Buffer,
									BufferLen,
									BufferActualLen,
									RequestId);
}

static int
IVmbusChannelRecvPacketRaw(
	struct hv_device *Device,
	void *				Buffer,
	u32				BufferLen,
	u32*				BufferActualLen,
	u64*				RequestId
	)
{
	return VmbusChannelRecvPacketRaw((VMBUS_CHANNEL*)Device->context,
										Buffer,
										BufferLen,
										BufferActualLen,
										RequestId);
}

static int
IVmbusChannelEstablishGpadl(
	struct hv_device *Device,
	void *				Buffer,
	u32				BufferLen,
	u32*				GpadlHandle
	)
{
	return VmbusChannelEstablishGpadl((VMBUS_CHANNEL*)Device->context,
										Buffer,
										BufferLen,
										GpadlHandle);
}

static int
IVmbusChannelTeardownGpadl(
   struct hv_device *Device,
   u32				GpadlHandle
	)
{
	return VmbusChannelTeardownGpadl((VMBUS_CHANNEL*)Device->context,
										GpadlHandle);

}

void GetChannelInterface(VMBUS_CHANNEL_INTERFACE *ChannelInterface)
{
	ChannelInterface->Open						= IVmbusChannelOpen;
	ChannelInterface->Close						= IVmbusChannelClose;
	ChannelInterface->SendPacket				= IVmbusChannelSendPacket;
	ChannelInterface->SendPacketPageBuffer		= IVmbusChannelSendPacketPageBuffer;
	ChannelInterface->SendPacketMultiPageBuffer = IVmbusChannelSendPacketMultiPageBuffer;
	ChannelInterface->RecvPacket				= IVmbusChannelRecvPacket;
	ChannelInterface->RecvPacketRaw				= IVmbusChannelRecvPacketRaw;
	ChannelInterface->EstablishGpadl			= IVmbusChannelEstablishGpadl;
	ChannelInterface->TeardownGpadl				= IVmbusChannelTeardownGpadl;
	ChannelInterface->GetInfo					= GetChannelInfo;
}


void GetChannelInfo(struct hv_device *Device, DEVICE_INFO *DeviceInfo)
{
	VMBUS_CHANNEL_DEBUG_INFO debugInfo;

	if (Device->context)
	{
		VmbusChannelGetDebugInfo((VMBUS_CHANNEL*)Device->context, &debugInfo);

		DeviceInfo->ChannelId = debugInfo.RelId;
		DeviceInfo->ChannelState = debugInfo.State;
		memcpy(&DeviceInfo->ChannelType, &debugInfo.InterfaceType, sizeof(GUID));
		memcpy(&DeviceInfo->ChannelInstance, &debugInfo.InterfaceInstance, sizeof(GUID));

		DeviceInfo->MonitorId = debugInfo.MonitorId;

		DeviceInfo->ServerMonitorPending = debugInfo.ServerMonitorPending;
		DeviceInfo->ServerMonitorLatency = debugInfo.ServerMonitorLatency;
		DeviceInfo->ServerMonitorConnectionId = debugInfo.ServerMonitorConnectionId;

		DeviceInfo->ClientMonitorPending = debugInfo.ClientMonitorPending;
		DeviceInfo->ClientMonitorLatency = debugInfo.ClientMonitorLatency;
		DeviceInfo->ClientMonitorConnectionId = debugInfo.ClientMonitorConnectionId;

		DeviceInfo->Inbound.InterruptMask = debugInfo.Inbound.CurrentInterruptMask;
		DeviceInfo->Inbound.ReadIndex = debugInfo.Inbound.CurrentReadIndex;
		DeviceInfo->Inbound.WriteIndex = debugInfo.Inbound.CurrentWriteIndex;
		DeviceInfo->Inbound.BytesAvailToRead = debugInfo.Inbound.BytesAvailToRead;
		DeviceInfo->Inbound.BytesAvailToWrite = debugInfo.Inbound.BytesAvailToWrite;

		DeviceInfo->Outbound.InterruptMask = debugInfo.Outbound.CurrentInterruptMask;
		DeviceInfo->Outbound.ReadIndex = debugInfo.Outbound.CurrentReadIndex;
		DeviceInfo->Outbound.WriteIndex = debugInfo.Outbound.CurrentWriteIndex;
		DeviceInfo->Outbound.BytesAvailToRead = debugInfo.Outbound.BytesAvailToRead;
		DeviceInfo->Outbound.BytesAvailToWrite = debugInfo.Outbound.BytesAvailToWrite;
	}
}
