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
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/hiddev.h>

#include "hv_api.h"
#include "vmbus.h"
#include "vmbus_api.h"
#include "channel.h"
#include "logging.h"

#include "mousevsc_api.h"
#include "vmbus_packet_format.h"
#include "vmbus_hid_protocol.h"


enum pipe_prot_msg_type {
	PipeMessageInvalid = 0,
	PipeMessageData,
	PipeMessageMaximum
};


struct pipe_prt_msg {
	enum pipe_prot_msg_type PacketType;
	u32                DataSize;
	char               Data[1];
};

/*
 * Data types
 */
struct  mousevsc_prt_msg {
	enum pipe_prot_msg_type   PacketType;
	u32                  DataSize;
	union {
		SYNTHHID_PROTOCOL_REQUEST	Request;
		SYNTHHID_PROTOCOL_RESPONSE	Response;
		SYNTHHID_DEVICE_INFO_ACK	Ack;
	} u;
};

/*
 * Represents an mousevsc device
 */
struct mousevsc_dev {
	struct hv_device	*Device;
	/* 0 indicates the device is being destroyed */
	atomic_t		RefCount;
	int			NumOutstandingRequests;
	unsigned char   	bInitializeComplete;
	struct mousevsc_prt_msg	ProtocolReq;
	struct mousevsc_prt_msg	ProtocolResp;
	/* Synchronize the request/response if needed */
	wait_queue_head_t	ProtocolWaitEvent;
	wait_queue_head_t	DeviceInfoWaitEvent;
	int			protocol_wait_condition;
	int			device_wait_condition;
	int			DeviceInfoStatus;

	struct hid_descriptor	*HidDesc;
	unsigned char		*ReportDesc;
	u32			ReportDescSize;
	struct input_dev_info	DeviceAttr;
};


/*
 * Globals
 */
static const char* gDriverName = "mousevsc";

/* {CFA8B69E-5B4A-4cc0-B98B-8BA1A1F3F95A} */
static const struct hv_guid gMousevscDeviceType = {
	.data = {0x9E, 0xB6, 0xA8, 0xCF, 0x4A, 0x5B, 0xc0, 0x4c,
		 0xB9, 0x8B, 0x8B, 0xA1, 0xA1, 0xF3, 0xF9, 0x5A}
};

/*
 * Internal routines
 */
static int MousevscOnDeviceAdd(struct hv_device *Device, void *AdditionalInfo);

static int MousevscOnDeviceRemove(struct hv_device *Device);

static void MousevscOnCleanup(struct hv_driver *Device);

static void MousevscOnChannelCallback(void *Context);

static int MousevscConnectToVsp(struct hv_device *Device);

static void MousevscOnReceive(struct hv_device *Device,
			      struct vmpacket_descriptor *Packet);

static inline struct mousevsc_dev *AllocInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = kzalloc(sizeof(struct mousevsc_dev), GFP_KERNEL);

	if (!inputDevice)
		return NULL;

	/*
	 * Set to 2 to allow both inbound and outbound traffics
	 * (ie GetInputDevice() and MustGetInputDevice()) to proceed.
	 */
	atomic_cmpxchg(&inputDevice->RefCount, 0, 2);

	inputDevice->Device = Device;
	Device->ext = inputDevice;

	return inputDevice;
}

static inline void FreeInputDevice(struct mousevsc_dev *Device)
{
	WARN_ON(atomic_read(&Device->RefCount) == 0);
	kfree(Device);
}

/*
 * Get the inputdevice object if exists and its refcount > 1
 */
static inline struct mousevsc_dev* GetInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = (struct mousevsc_dev*)Device->ext;

//	printk(KERN_ERR "-------------------------> REFCOUNT = %d",
//	       inputDevice->RefCount);

	if (inputDevice && atomic_read(&inputDevice->RefCount) > 1)
		atomic_inc(&inputDevice->RefCount);
	else
		inputDevice = NULL;

	return inputDevice;
}

/*
 * Get the inputdevice object iff exists and its refcount > 0
 */
static inline struct mousevsc_dev* MustGetInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = (struct mousevsc_dev*)Device->ext;

	if (inputDevice && atomic_read(&inputDevice->RefCount))
		atomic_inc(&inputDevice->RefCount);
	else
		inputDevice = NULL;

	return inputDevice;
}

static inline void PutInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = (struct mousevsc_dev*)Device->ext;

	atomic_dec(&inputDevice->RefCount);
}

/*
 * Drop ref count to 1 to effectively disable GetInputDevice()
 */
static inline struct mousevsc_dev* ReleaseInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = (struct mousevsc_dev*)Device->ext;

	/* Busy wait until the ref drop to 2, then set it to 1  */
	while (atomic_cmpxchg(&inputDevice->RefCount, 2, 1) != 2)
		udelay(100);

	return inputDevice;
}

/*
 * Drop ref count to 0. No one can use InputDevice object.
 */
static inline struct mousevsc_dev* FinalReleaseInputDevice(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;

	inputDevice = (struct mousevsc_dev*)Device->ext;

	/* Busy wait until the ref drop to 1, then set it to 0  */
	while (atomic_cmpxchg(&inputDevice->RefCount, 1, 0) != 1)
		udelay(100);

	Device->ext = NULL;
	return inputDevice;
}

/*
 *
 * Name:
 *	MousevscInitialize()
 *
 * Description:
 *	Main entry point
 *
 */
int mouse_vsc_initialize(struct hv_driver *Driver)
{
	struct mousevsc_drv_obj *inputDriver =
		(struct mousevsc_drv_obj *)Driver;
	int ret = 0;

	Driver->name = gDriverName;
	memcpy(&Driver->dev_type, &gMousevscDeviceType,
	       sizeof(struct hv_guid));

	/* Setup the dispatch table */
	inputDriver->Base.dev_add		= MousevscOnDeviceAdd;
	inputDriver->Base.dev_rm = MousevscOnDeviceRemove;
	inputDriver->Base.cleanup		= MousevscOnCleanup;

	inputDriver->OnOpen			= NULL;
	inputDriver->OnClose			= NULL;

	return ret;
}

/*
 *
 * Name:
 *	MousevscOnDeviceAdd()
 *
 * Description:
 *	Callback when the device belonging to this driver is added
 *
 */
int
MousevscOnDeviceAdd(struct hv_device *Device, void *AdditionalInfo)
{
	int ret = 0;
	struct mousevsc_dev *inputDevice;
	struct mousevsc_drv_obj *inputDriver;
	struct input_dev_info deviceInfo;

	inputDevice = AllocInputDevice(Device);

	if (!inputDevice) {
		ret = -1;
		goto Cleanup;
	}

	inputDevice->bInitializeComplete = false;

	/* Open the channel */
	ret = vmbus_open(Device->channel,
		INPUTVSC_SEND_RING_BUFFER_SIZE,
		INPUTVSC_RECV_RING_BUFFER_SIZE,
		NULL,
		0,
		MousevscOnChannelCallback,
		Device
		);

	if (ret != 0) {
		pr_err("unable to open channel: %d", ret);
		return -1;
	}

	pr_info("InputVsc channel open: %d", ret);

	ret = MousevscConnectToVsp(Device);

	if (ret != 0) {
		pr_err("unable to connect channel: %d", ret);

		vmbus_close(Device->channel);
		return ret;
	}

	inputDriver = (struct mousevsc_drv_obj *)inputDevice->Device->drv;

	deviceInfo.VendorID = inputDevice->DeviceAttr.VendorID;
	deviceInfo.ProductID = inputDevice->DeviceAttr.ProductID;
	deviceInfo.VersionNumber = inputDevice->DeviceAttr.VersionNumber;
	strcpy(deviceInfo.Name, "Microsoft Vmbus HID-compliant Mouse");

	/* Send the device info back up */
	inputDriver->OnDeviceInfo(Device, &deviceInfo);

	/* Send the report desc back up */
	/* workaround SA-167 */
	if (inputDevice->ReportDesc[14] == 0x25)
		inputDevice->ReportDesc[14] = 0x29;

	inputDriver->OnReportDescriptor(Device, inputDevice->ReportDesc, inputDevice->ReportDescSize);

	inputDevice->bInitializeComplete = true;

Cleanup:
	return ret;
}

int
MousevscConnectToVsp(struct hv_device *Device)
{
	int ret=0;
	struct mousevsc_dev *inputDevice;
	struct mousevsc_prt_msg *request;
	struct mousevsc_prt_msg *response;

	inputDevice = GetInputDevice(Device);

	if (!inputDevice) {
		pr_err("unable to get input device...device being destroyed?");
		return -1;
	}

	init_waitqueue_head(&inputDevice->ProtocolWaitEvent);
	init_waitqueue_head(&inputDevice->DeviceInfoWaitEvent);

	request = &inputDevice->ProtocolReq;

	/*
	 * Now, initiate the vsc/vsp initialization protocol on the open channel
	 */
	memset(request, sizeof(struct mousevsc_prt_msg), 0);

	request->PacketType = PipeMessageData;
	request->DataSize = sizeof(SYNTHHID_PROTOCOL_REQUEST);

	request->u.Request.Header.Type = SynthHidProtocolRequest;
	request->u.Request.Header.Size = sizeof(unsigned long);
	request->u.Request.VersionRequested.AsDWord =
		SYNTHHID_INPUT_VERSION_DWORD;

	pr_info("SYNTHHID_PROTOCOL_REQUEST...");

	ret = vmbus_sendpacket(Device->channel, request,
					sizeof(struct pipe_prt_msg) -
					sizeof(unsigned char) +
					sizeof(SYNTHHID_PROTOCOL_REQUEST),
					(unsigned long)request,
					VM_PKT_DATA_INBAND,
					VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if ( ret != 0) {
		pr_err("unable to send SYNTHHID_PROTOCOL_REQUEST");
		goto Cleanup;
	}

	inputDevice->protocol_wait_condition = 0;
	wait_event_timeout(inputDevice->ProtocolWaitEvent, inputDevice->protocol_wait_condition, msecs_to_jiffies(1000));
	if (inputDevice->protocol_wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto Cleanup;
	}

	response = &inputDevice->ProtocolResp;

	if (!response->u.Response.Approved) {
		pr_err("SYNTHHID_PROTOCOL_REQUEST failed (version %d)",
		       SYNTHHID_INPUT_VERSION_DWORD);
		ret = -1;
		goto Cleanup;
	}

	inputDevice->device_wait_condition = 0;
	wait_event_timeout(inputDevice->DeviceInfoWaitEvent, inputDevice->device_wait_condition, msecs_to_jiffies(1000));
	if (inputDevice->device_wait_condition == 0) {
		ret = -ETIMEDOUT;
		goto Cleanup;
	}

	/*
	 * We should have gotten the device attr, hid desc and report
	 * desc at this point
	 */
	if (!inputDevice->DeviceInfoStatus)
		pr_info("**** input channel up and running!! ****");
	else
		ret = -1;

Cleanup:
	PutInputDevice(Device);

	return ret;
}


/*
 *
 * Name:
 *	MousevscOnDeviceRemove()
 *
 * Description:
 *	Callback when the our device is being removed
 *
 */
int
MousevscOnDeviceRemove(struct hv_device *Device)
{
	struct mousevsc_dev *inputDevice;
	int ret=0;

	pr_info("disabling input device (%p)...",
		    Device->ext);

	inputDevice = ReleaseInputDevice(Device);


	/*
	 * At this point, all outbound traffic should be disable. We only
	 * allow inbound traffic (responses) to proceed
	 *
	 * so that outstanding requests can be completed.
	 */
	while (inputDevice->NumOutstandingRequests) {
		pr_info("waiting for %d requests to complete...", inputDevice->NumOutstandingRequests);

		udelay(100);
	}

	pr_info("removing input device (%p)...", Device->ext);

	inputDevice = FinalReleaseInputDevice(Device);

	pr_info("input device (%p) safe to remove", inputDevice);

	/* Close the channel */
	vmbus_close(Device->channel);

	FreeInputDevice(inputDevice);

	return ret;
}


/*
 *
 * Name:
 *	MousevscOnCleanup()
 *
 * Description:
 *	Perform any cleanup when the driver is removed
 */
static void MousevscOnCleanup(struct hv_driver *drv)
{
}


static void
MousevscOnSendCompletion(struct hv_device *Device,
			 struct vmpacket_descriptor *Packet)
{
	struct mousevsc_dev *inputDevice;
	void *request;

	inputDevice = MustGetInputDevice(Device);
	if (!inputDevice) {
		pr_err("unable to get input device...device being destroyed?");
		return;
	}

	request = (void*)(unsigned long *) Packet->trans_id;

	if (request == &inputDevice->ProtocolReq) {
		/* FIXME */
		/* Shouldn't we be doing something here? */
	}

	PutInputDevice(Device);
}

void
MousevscOnReceiveDeviceInfo(
	struct mousevsc_dev* InputDevice,
	SYNTHHID_DEVICE_INFO* DeviceInfo
	)
{
	int ret = 0;
	struct hid_descriptor *desc;
	struct mousevsc_prt_msg ack;

	/* Assume success for now */
	InputDevice->DeviceInfoStatus = 0;

	/* Save the device attr */
	memcpy(&InputDevice->DeviceAttr, &DeviceInfo->HidDeviceAttributes, sizeof(struct input_dev_info));

	/* Save the hid desc */
	desc = (struct hid_descriptor *)DeviceInfo->HidDescriptorInformation;
	WARN_ON(desc->bLength > 0);

	InputDevice->HidDesc = kzalloc(desc->bLength, GFP_KERNEL);

	if (!InputDevice->HidDesc) {
		pr_err("unable to allocate hid descriptor - size %d", desc->bLength);
		goto Cleanup;
	}

	memcpy(InputDevice->HidDesc, desc, desc->bLength);

	/* Save the report desc */
	InputDevice->ReportDescSize = desc->desc[0].wDescriptorLength;
	InputDevice->ReportDesc = kzalloc(InputDevice->ReportDescSize,
					  GFP_KERNEL);

	if (!InputDevice->ReportDesc) {
		pr_err("unable to allocate report descriptor - size %d",
			   InputDevice->ReportDescSize);
		goto Cleanup;
	}

	memcpy(InputDevice->ReportDesc,
	       ((unsigned char *)desc) + desc->bLength,
	       desc->desc[0].wDescriptorLength);

	/* Send the ack */
	memset(&ack, sizeof(struct mousevsc_prt_msg), 0);

	ack.PacketType = PipeMessageData;
	ack.DataSize = sizeof(SYNTHHID_DEVICE_INFO_ACK);

	ack.u.Ack.Header.Type = SynthHidInitialDeviceInfoAck;
	ack.u.Ack.Header.Size = 1;
	ack.u.Ack.Reserved = 0;

	ret = vmbus_sendpacket(InputDevice->Device->channel,
			&ack,
			sizeof(struct pipe_prt_msg) - sizeof(unsigned char) + sizeof(SYNTHHID_DEVICE_INFO_ACK),
			(unsigned long)&ack,
			VM_PKT_DATA_INBAND,
			VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0) {
		pr_err("unable to send SYNTHHID_DEVICE_INFO_ACK - ret %d",
			   ret);
		goto Cleanup;
	}

	InputDevice->device_wait_condition = 1;
	wake_up(&InputDevice->DeviceInfoWaitEvent);

	return;

Cleanup:
	if (InputDevice->HidDesc) {
		kfree(InputDevice->HidDesc);
		InputDevice->HidDesc = NULL;
	}

	if (InputDevice->ReportDesc) {
		kfree(InputDevice->ReportDesc);
		InputDevice->ReportDesc = NULL;
	}

	InputDevice->DeviceInfoStatus = -1;
	InputDevice->device_wait_condition = 1;
	wake_up(&InputDevice->DeviceInfoWaitEvent);
}


void
MousevscOnReceiveInputReport(
	struct mousevsc_dev* InputDevice,
	SYNTHHID_INPUT_REPORT *InputReport
	)
{
	struct mousevsc_drv_obj *inputDriver;

	if (!InputDevice->bInitializeComplete) {
		pr_info("Initialization incomplete...ignoring InputReport msg");
		return;
	}

	inputDriver = (struct mousevsc_drv_obj *)InputDevice->Device->drv;

	inputDriver->OnInputReport(InputDevice->Device,
				   InputReport->ReportBuffer,
				   InputReport->Header.Size);
}

void
MousevscOnReceive(struct hv_device *Device, struct vmpacket_descriptor *Packet)
{
	struct pipe_prt_msg *pipeMsg;
	SYNTHHID_MESSAGE *hidMsg;
	struct mousevsc_dev *inputDevice;

	inputDevice = MustGetInputDevice(Device);
	if (!inputDevice) {
		pr_err("unable to get input device...device being destroyed?");
		return;
	}

	pipeMsg = (struct pipe_prt_msg *)((unsigned long)Packet + (Packet->offset8 << 3));

	if (pipeMsg->PacketType != PipeMessageData) {
		pr_err("unknown pipe msg type - type %d len %d",
			   pipeMsg->PacketType, pipeMsg->DataSize);
		PutInputDevice(Device);
		return ;
	}

	hidMsg = (SYNTHHID_MESSAGE*)&pipeMsg->Data[0];

	switch (hidMsg->Header.Type) {
	case SynthHidProtocolResponse:
		memcpy(&inputDevice->ProtocolResp, pipeMsg, pipeMsg->DataSize+sizeof(struct pipe_prt_msg) - sizeof(unsigned char));
		inputDevice->protocol_wait_condition = 1;
		wake_up(&inputDevice->ProtocolWaitEvent);
		break;

	case SynthHidInitialDeviceInfo:
		WARN_ON(pipeMsg->DataSize >= sizeof(struct input_dev_info));

		/*
		 * Parse out the device info into device attr,
		 * hid desc and report desc
		 */
		MousevscOnReceiveDeviceInfo(inputDevice,
					    (SYNTHHID_DEVICE_INFO*)&pipeMsg->Data[0]);
		break;
	case SynthHidInputReport:
		MousevscOnReceiveInputReport(inputDevice,
					     (SYNTHHID_INPUT_REPORT*)&pipeMsg->Data[0]);

		break;
	default:
		pr_err("unsupported hid msg type - type %d len %d",
		       hidMsg->Header.Type, hidMsg->Header.Size);
		break;
	}

	PutInputDevice(Device);
}

void MousevscOnChannelCallback(void *Context)
{
	const int packetSize = 0x100;
	int ret = 0;
	struct hv_device *device = (struct hv_device *)Context;
	struct mousevsc_dev *inputDevice;

	u32 bytesRecvd;
	u64 requestId;
	unsigned char packet[packetSize];
	struct vmpacket_descriptor *desc;
	unsigned char	*buffer = packet;
	int	bufferlen = packetSize;

	inputDevice = MustGetInputDevice(device);

	if (!inputDevice) {
		pr_err("unable to get input device...device being destroyed?");
		return;
	}

	do {
		ret = vmbus_recvpacket_raw(device->channel, buffer, bufferlen, &bytesRecvd, &requestId);

		if (ret == 0) {
			if (bytesRecvd > 0) {
				desc = (struct vmpacket_descriptor *)buffer;

				switch (desc->type) {
					case VM_PKT_COMP:
						MousevscOnSendCompletion(device,
									 desc);
						break;

					case VM_PKT_DATA_INBAND:
						MousevscOnReceive(device, desc);
						break;

					default:
						pr_err("unhandled packet type %d, tid %llx len %d\n",
							   desc->type,
							   requestId,
							   bytesRecvd);
						break;
				}

				/* reset */
				if (bufferlen > packetSize) {
					kfree(buffer);

					buffer = packet;
					bufferlen = packetSize;
				}
			} else {
				/*
				 * pr_debug("nothing else to read...");
				 * reset
				 */
				if (bufferlen > packetSize) {
					kfree(buffer);

					buffer = packet;
					bufferlen = packetSize;
				}
				break;
			}
		} else if (ret == -2) {
			/* Handle large packet */
			bufferlen = bytesRecvd;
			buffer = kzalloc(bytesRecvd, GFP_KERNEL);

			if (buffer == NULL) {
				buffer = packet;
				bufferlen = packetSize;

				/* Try again next time around */
				pr_err("unable to allocate buffer of size %d!",
				       bytesRecvd);
				break;
			}
		}
	} while (1);

	PutInputDevice(device);

	return;
}

