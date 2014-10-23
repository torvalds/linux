/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __CONTROLVMCHANNEL_H__
#define __CONTROLVMCHANNEL_H__

#include <linux/uuid.h>
#include "channel.h"
#include "controlframework.h"

typedef u64 GUEST_PHYSICAL_ADDRESS;

enum { INVALID_GUEST_FIRMWARE, SAMPLE_GUEST_FIRMWARE,
	    TIANO32_GUEST_FIRMWARE, TIANO64_GUEST_FIRMWARE
};

/* {2B3C2D10-7EF5-4ad8-B966-3448B7386B3D} */
#define SPAR_CONTROLVM_CHANNEL_PROTOCOL_UUID	\
		UUID_LE(0x2b3c2d10, 0x7ef5, 0x4ad8, \
				0xb9, 0x66, 0x34, 0x48, 0xb7, 0x38, 0x6b, 0x3d)

static const uuid_le spar_controlvm_channel_protocol_uuid =
	SPAR_CONTROLVM_CHANNEL_PROTOCOL_UUID;

#define ULTRA_CONTROLVM_CHANNEL_PROTOCOL_SIGNATURE \
	ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define CONTROLVM_MESSAGE_MAX     64

/* Must increment this whenever you insert or delete fields within
* this channel struct.  Also increment whenever you change the meaning
* of fields within this channel struct so as to break pre-existing
* software.  Note that you can usually add fields to the END of the
* channel struct withOUT needing to increment this. */
#define ULTRA_CONTROLVM_CHANNEL_PROTOCOL_VERSIONID  1

#define SPAR_CONTROLVM_CHANNEL_OK_CLIENT(ch)           \
	spar_check_channel_client(ch, \
				  spar_controlvm_channel_protocol_uuid, \
				  "controlvm", \
				  sizeof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL), \
				  ULTRA_CONTROLVM_CHANNEL_PROTOCOL_VERSIONID, \
				  ULTRA_CONTROLVM_CHANNEL_PROTOCOL_SIGNATURE)

#define MY_DEVICE_INDEX 0
#define MAX_MACDATA_LEN 8 /* number of bytes for MAC address in config packet */
#define MAX_SERIAL_NUM	32

#define DISK_ZERO_PUN_NUMBER	1  /* Target ID on the SCSI bus for LUN 0 */
#define DISK_ZERO_LUN_NUMBER	3  /* Logical Unit Number */

/* Defines for various channel queues... */
#define CONTROLVM_QUEUE_REQUEST		0
#define CONTROLVM_QUEUE_RESPONSE	1
#define	CONTROLVM_QUEUE_EVENT		2
#define CONTROLVM_QUEUE_ACK		3

/* Max number of messages stored during IOVM creation to be reused
 * after crash */
#define CONTROLVM_CRASHMSG_MAX		2

/** Ids for commands that may appear in either queue of a ControlVm channel.
 *
 *  Commands that are initiated by the command partition (CP), by an IO or
 *  console service partition (SP), or by a guest partition (GP)are:
 *  - issued on the RequestQueue queue (q #0) in the ControlVm channel
 *  - responded to on the ResponseQueue queue (q #1) in the ControlVm channel
 *
 *  Events that are initiated by an IO or console service partition (SP) or
 *  by a guest partition (GP) are:
 *  - issued on the EventQueue queue (q #2) in the ControlVm channel
 *  - responded to on the EventAckQueue queue (q #3) in the ControlVm channel
 */
enum controlvm_id {
	CONTROLVM_INVALID = 0,
	/* SWITCH commands required Parameter: SwitchNumber  */
	/* BUS commands required Parameter: BusNumber  */
	CONTROLVM_BUS_CREATE = 0x101,	/* CP --> SP, GP */
	CONTROLVM_BUS_DESTROY = 0x102,	/* CP --> SP, GP */
	CONTROLVM_BUS_CONFIGURE = 0x104,	/* CP --> SP */
	CONTROLVM_BUS_CHANGESTATE = 0x105,	/* CP --> SP, GP */
	CONTROLVM_BUS_CHANGESTATE_EVENT = 0x106, /* SP, GP --> CP */
/* DEVICE commands required Parameter: BusNumber, DeviceNumber  */

	CONTROLVM_DEVICE_CREATE = 0x201,	/* CP --> SP, GP */
	CONTROLVM_DEVICE_DESTROY = 0x202,	/* CP --> SP, GP */
	CONTROLVM_DEVICE_CONFIGURE = 0x203,	/* CP --> SP */
	CONTROLVM_DEVICE_CHANGESTATE = 0x204,	/* CP --> SP, GP */
	CONTROLVM_DEVICE_CHANGESTATE_EVENT = 0x205, /* SP, GP --> CP */
	CONTROLVM_DEVICE_RECONFIGURE = 0x206,	/* CP --> Boot */
/* DISK commands required Parameter: BusNumber, DeviceNumber  */
	CONTROLVM_DISK_CREATE = 0x221,	/* CP --> SP */
	CONTROLVM_DISK_DESTROY = 0x222,	/* CP --> SP */
	CONTROLVM_DISK_CONFIGURE = 0x223,	/* CP --> SP */
	CONTROLVM_DISK_CHANGESTATE = 0x224,	/* CP --> SP */
/* CHIPSET commands */
	CONTROLVM_CHIPSET_INIT = 0x301,	/* CP --> SP, GP */
	CONTROLVM_CHIPSET_STOP = 0x302,	/* CP --> SP, GP */
	CONTROLVM_CHIPSET_SHUTDOWN = 0x303,	/* CP --> SP */
	CONTROLVM_CHIPSET_READY = 0x304,	/* CP --> SP */
	CONTROLVM_CHIPSET_SELFTEST = 0x305,	/* CP --> SP */

};

struct irq_info {
	 /**< specifies interrupt info. It is used to send interrupts
	  *   for this channel. The peer at the end of this channel
	  *   who has registered an interrupt (using recv fields
	  *   above) will receive the interrupt. Passed as a parameter
	  *   to Issue_VMCALL_IO_QUEUE_TRANSITION, which generates the
	  *   interrupt.  Currently this is used by IOPart-SP to wake
	  *   up GP when Data Channel transitions from empty to
	  *   non-empty.*/
	u64 send_irq_handle;

	 /**< specifies interrupt handle. It is used to retrieve the
	  *   corresponding interrupt pin from Monitor; and the
	  *   interrupt pin is used to connect to the corresponding
	  *   intrrupt.  Used by IOPart-GP only. */
	u64 recv_irq_handle;

	 /**< specifies interrupt vector. It, interrupt pin, and shared are
	  *   used to connect to the corresponding interrupt.  Used by
	  *   IOPart-GP only. */
	u32 recv_irq_vector;

    /**< specifies if the recvInterrupt is shared.  It, interrupt pin
     *   and vector are used to connect to 0 = not shared; 1 = shared.
     *   the corresponding interrupt.  Used by IOPart-GP only. */
	u8 recv_irq_shared;
	u8 reserved[3];	/* Natural alignment purposes */
};

struct pci_id {
	u16 domain;
	u8 bus;
	u8 slot;
	u8 func;
	u8 reserved[3];	/* Natural alignment purposes */
};

struct efi_spar_indication  {
	u64 boot_to_fw_ui:1;	/* Bit 0: Stop in uefi ui */
	u64 clear_nvram:1;	/* Bit 1: Clear NVRAM */
	u64 clear_cmos:1;	/* Bit 2: Clear CMOS */
	u64 boot_to_tool:1;	/* Bit 3: Run install tool */
	/* remaining bits are available */
};

enum ultra_chipset_feature {
	ULTRA_CHIPSET_FEATURE_REPLY = 0x00000001,
	ULTRA_CHIPSET_FEATURE_PARA_HOTPLUG = 0x00000002,
	ULTRA_CHIPSET_FEATURE_PCIVBUS = 0x00000004
};

/** This is the common structure that is at the beginning of every
 *  ControlVm message (both commands and responses) in any ControlVm
 *  queue.  Commands are easily distinguished from responses by
 *  looking at the flags.response field.
 */
struct controlvm_message_header  {
	u32 id;		/* See CONTROLVM_ID. */
	/* For requests, indicates the message type. */
	/* For responses, indicates the type of message we are responding to. */

	u32 message_size;	/* Includes size of this struct + size
				 * of message */
	u32 segment_index;	/* Index of segment containing Vm
				 * message/information */
	u32 completion_status;	/* Error status code or result of
				 * message completion */
	struct  {
		u32 failed:1;		   /**< =1 in a response to * signify
					    * failure */
		u32 response_expected:1;   /**< =1 in all messages that expect a
					   * response (Control ignores this
					   * bit) */
		u32 server:1;		   /**< =1 in all bus & device-related
					    * messages where the message
					    * receiver is to act as the bus or
					    * device server */
		u32 test_message:1;	   /**< =1 for testing use only
					    * (Control and Command ignore this
					    * bit) */
		u32 partial_completion:1;  /**< =1 if there are forthcoming
					   * responses/acks associated
					   * with this message */
		u32 preserve:1;	       /**< =1 this is to let us know to
					* preserve channel contents
					* (for running guests)*/
		u32 writer_in_diag:1;	/**< =1 the DiagWriter is active in the
					 * Diagnostic Partition*/
	} flags;
	u32 reserved;		/* Natural alignment */
	u64 message_handle;	/* Identifies the particular message instance,
				 * and is used to match particular */
	/* request instances with the corresponding response instance. */
	u64 payload_vm_offset;	/* Offset of payload area from start of this
				 * instance of ControlVm segment */
	u32 payload_max_bytes;	/* Maximum bytes allocated in payload
				 * area of ControlVm segment */
	u32 payload_bytes;	/* Actual number of bytes of payload
				 * area to copy between IO/Command; */
	/* if non-zero, there is a payload to copy. */
};

typedef struct _CONTROLVM_PACKET_DEVICE_CREATE  {
	u32 busNo;	   /**< bus # (0..n-1) from the msg receiver's
			    * perspective */

	    /* Control uses header SegmentIndex field to access bus number... */
	u32 devNo;	   /**< bus-relative (0..n-1) device number */
	u64 channelAddr;  /**< Guest physical address of the channel, which
			*   can be dereferenced by the receiver
			*   of this ControlVm command */
	u64 channelBytes; /**< specifies size of the channel in bytes */
	uuid_le dataTypeGuid;/**< specifies format of data in channel */
	uuid_le devInstGuid; /**< instance guid for the device */
	struct irq_info intr; /**< specifies interrupt information */
} CONTROLVM_PACKET_DEVICE_CREATE;	/* for CONTROLVM_DEVICE_CREATE */

typedef struct _CONTROLVM_PACKET_DEVICE_CONFIGURE  {
	u32 busNo;	      /**< bus # (0..n-1) from the msg
			       * receiver's perspective */

	    /* Control uses header SegmentIndex field to access bus number... */
	u32 devNo;	      /**< bus-relative (0..n-1) device number */
} CONTROLVM_PACKET_DEVICE_CONFIGURE;	/* for CONTROLVM_DEVICE_CONFIGURE */

typedef struct _CONTROLVM_MESSAGE_DEVICE_CREATE  {
	struct controlvm_message_header Header;
	CONTROLVM_PACKET_DEVICE_CREATE Packet;
} CONTROLVM_MESSAGE_DEVICE_CREATE;	/* total 128 bytes */

typedef struct _CONTROLVM_MESSAGE_DEVICE_CONFIGURE  {
	struct controlvm_message_header Header;
	CONTROLVM_PACKET_DEVICE_CONFIGURE Packet;
} CONTROLVM_MESSAGE_DEVICE_CONFIGURE;	/* total 56 bytes */

/* This is the format for a message in any ControlVm queue. */
struct controlvm_message_packet  {
	union  {
		struct  {
			u32 bus_no;	/* bus # (0..n-1) from the msg
					 * receiver's perspective */
			u32 dev_count;	/* indicates the max number of
					 * devices on this bus */
			u64 channel_addr;	/* Guest physical address of
						 * the channel, which can be
						 * dereferenced by the receiver
						 * of this ControlVm command */
			u64 channel_bytes;	/* size of the channel */
			uuid_le bus_data_type_uuid;	/* indicates format of
							 * data in bus channel*/
			uuid_le bus_inst_uuid;	/* instance uuid for the bus */
		} create_bus;	/* for CONTROLVM_BUS_CREATE */
		struct  {
			u32 bus_no;	/* bus # (0..n-1) from the msg
					 * receiver's perspective */
			u32 reserved;	/* Natural alignment purposes */
		} destroy_bus;	/* for CONTROLVM_BUS_DESTROY */
		struct  {
			u32 bus_no;	/* bus # (0..n-1) from the receiver's
					 * perspective */
			u32 reserved1;	/* for alignment purposes */
			u64 guest_handle;	/* This is used to convert
						 * guest physical address to
						 * physical address */
			u64 recv_bus_irq_handle;
				/* specifies interrupt info. It is used by SP
				 * to register to receive interrupts from the
				 * CP. This interrupt is used for bus level
				 * notifications.  The corresponding
				 * sendBusInterruptHandle is kept in CP. */
		} configure_bus;	/* for CONTROLVM_BUS_CONFIGURE */
		/* for CONTROLVM_DEVICE_CREATE */
		CONTROLVM_PACKET_DEVICE_CREATE create_device;
		struct  {
			u32 bus_no;	/* bus # (0..n-1) from the msg
					 * receiver's perspective */
			u32 dev_no;	/* bus-relative (0..n-1) device # */
		} destroy_device;	/* for CONTROLVM_DEVICE_DESTROY */
		/* for CONTROLVM_DEVICE_CONFIGURE */
		CONTROLVM_PACKET_DEVICE_CONFIGURE configure_device;
		struct  {
			u32 bus_no;	/* bus # (0..n-1) from the msg
					 * receiver's perspective */
			u32 dev_no;	/* bus-relative (0..n-1) device # */
		} reconfigure_device;	/* for CONTROLVM_DEVICE_RECONFIGURE */
		struct  {
			u32 bus_no;
			struct spar_segment_state state;
			u8 reserved[2];	/* Natural alignment purposes */
		} bus_change_state;	/* for CONTROLVM_BUS_CHANGESTATE */
		struct  {
			u32 bus_no;
			u32 dev_no;
			struct spar_segment_state state;
			struct  {
				u32 phys_device:1;	/* =1 if message is for
							 * a physical device */
			} flags;
			u8 reserved[2];	/* Natural alignment purposes */
		} device_change_state;	/* for CONTROLVM_DEVICE_CHANGESTATE */
		struct  {
			u32 bus_no;
			u32 dev_no;
			struct spar_segment_state state;
			u8 reserved[6];	/* Natural alignment purposes */
		} device_change_state_event;
			/* for CONTROLVM_DEVICE_CHANGESTATE_EVENT */
		struct  {
			u32 bus_count;	/* indicates the max number of busses */
			u32 switch_count; /* indicates the max number of
					   * switches if a service partition */
			enum ultra_chipset_feature features;
			u32 platform_number;	/* Platform Number */
		} init_chipset;	/* for CONTROLVM_CHIPSET_INIT */
		struct  {
			u32 options;	/* reserved */
			u32 test;	/* bit 0 set to run embedded selftest */
		} chipset_selftest;	/* for CONTROLVM_CHIPSET_SELFTEST */
		u64 addr;	/* a physical address of something, that can be
				 * dereferenced by the receiver of this
				 * ControlVm command (depends on command id) */
		u64 handle;	/* a handle of something (depends on command
				 * id) */
	};
};

/* All messages in any ControlVm queue have this layout. */
typedef struct _CONTROLVM_MESSAGE  {
	struct controlvm_message_header hdr;
	struct controlvm_message_packet cmd;
} CONTROLVM_MESSAGE;

typedef struct _DEVICE_MAP  {
	GUEST_PHYSICAL_ADDRESS DeviceChannelAddress;
	u64 DeviceChannelSize;
	u32 CA_Index;
	u32 Reserved;		/* natural alignment */
	u64 Reserved2;		/* Align structure on 32-byte boundary */
} DEVICE_MAP;

typedef struct _GUEST_DEVICES  {
	DEVICE_MAP VideoChannel;
	DEVICE_MAP KeyboardChannel;
	DEVICE_MAP NetworkChannel;
	DEVICE_MAP StorageChannel;
	DEVICE_MAP ConsoleChannel;
	u32 PartitionIndex;
	u32 Pad;
} GUEST_DEVICES;

typedef struct _ULTRA_CONTROLVM_CHANNEL_PROTOCOL  {
	 struct channel_header Header;
	 GUEST_PHYSICAL_ADDRESS gpControlVm;	/* guest physical address of
						 * this channel */
	 GUEST_PHYSICAL_ADDRESS gpPartitionTables; /* guest physical address of
						    * partition tables */
	 GUEST_PHYSICAL_ADDRESS gpDiagGuest;	/* guest physical address of
						 * diagnostic channel */
	 GUEST_PHYSICAL_ADDRESS gpBootRomDisk;	/* guest phys addr of (read
						 * only) Boot ROM disk */
	 GUEST_PHYSICAL_ADDRESS gpBootRamDisk;	/* guest phys addr of writable
						 * Boot RAM disk */
	 GUEST_PHYSICAL_ADDRESS gpAcpiTable;	/* guest phys addr of acpi
						 * table */
	 GUEST_PHYSICAL_ADDRESS gpControlChannel; /* guest phys addr of control
						   * channel */
	 GUEST_PHYSICAL_ADDRESS gpDiagRomDisk;	/* guest phys addr of diagnostic
						 * ROM disk */
	 GUEST_PHYSICAL_ADDRESS gpNvram;	/* guest phys addr of NVRAM
						 * channel */
	 u64 RequestPayloadOffset;	/* Offset to request payload area */
	 u64 EventPayloadOffset;	/* Offset to event payload area */
	 u32 RequestPayloadBytes;	/* Bytes available in request payload
					 * area */
	 u32 EventPayloadBytes;	/* Bytes available in event payload area */
	 u32 ControlChannelBytes;
	 u32 NvramChannelBytes;	/* Bytes in PartitionNvram segment */
	 u32 MessageBytes;	/* sizeof(CONTROLVM_MESSAGE) */
	 u32 MessageCount;	/* CONTROLVM_MESSAGE_MAX */
	 GUEST_PHYSICAL_ADDRESS gpSmbiosTable;	/* guest phys addr of SMBIOS
						 * tables */
	 GUEST_PHYSICAL_ADDRESS gpPhysicalSmbiosTable;	/* guest phys addr of
							 * SMBIOS table  */
	 /* ULTRA_MAX_GUESTS_PER_SERVICE */
	 GUEST_DEVICES gpObsoleteGuestDevices[16];

	 /* guest physical address of EFI firmware image base  */
	 GUEST_PHYSICAL_ADDRESS VirtualGuestFirmwareImageBase;

	 /* guest physical address of EFI firmware entry point  */
	 GUEST_PHYSICAL_ADDRESS VirtualGuestFirmwareEntryPoint;

	 /* guest EFI firmware image size  */
	 u64 VirtualGuestFirmwareImageSize;

	 /* GPA = 1MB where EFI firmware image is copied to  */
	 GUEST_PHYSICAL_ADDRESS VirtualGuestFirmwareBootBase;
	 GUEST_PHYSICAL_ADDRESS VirtualGuestImageBase;
	 GUEST_PHYSICAL_ADDRESS VirtualGuestImageSize;
	 u64 PrototypeControlChannelOffset;
	 GUEST_PHYSICAL_ADDRESS VirtualGuestPartitionHandle;

	 u16 RestoreAction;	/* Restore Action field to restore the guest
				 * partition */
	u16 DumpAction;		/* For Windows guests it shows if the visordisk
				 * is running in dump mode */
	u16 NvramFailCount;
	u16 SavedCrashMsgCount;	/* = CONTROLVM_CRASHMSG_MAX */
	u32 SavedCrashMsgOffset;	/* Offset to request payload area needed
					 * for crash dump */
	u32 InstallationError;	/* Type of error encountered during
				 * installation */
	u32 InstallationTextId;	/* Id of string to display */
	u16 InstallationRemainingSteps;	/* Number of remaining installation
					 * steps (for progress bars) */
	u8 ToolAction;		/* ULTRA_TOOL_ACTIONS Installation Action
				 * field */
	u8 Reserved;		/* alignment */
	struct efi_spar_indication EfiSparIndication;
	struct efi_spar_indication EfiSparIndicationSupported;
	u32 SPReserved;
	u8 Reserved2[28];	/* Force signals to begin on 128-byte cache
				 * line */
	struct signal_queue_header RequestQueue;/* Service or guest partition
						 * uses this queue to send
						 * requests to Control */
	struct signal_queue_header ResponseQueue;/* Control uses this queue to
						 * respond to service or guest
						 * partition requests */
	struct signal_queue_header EventQueue;	/* Control uses this queue to
						 * send events to service or
						 * guest partition */
	struct signal_queue_header EventAckQueue;/* Service or guest partition
						 * uses this queue to ack
						 * Control events */

	 /* Request fixed-size message pool - does not include payload */
	 CONTROLVM_MESSAGE RequestMsg[CONTROLVM_MESSAGE_MAX];

	 /* Response fixed-size message pool - does not include payload */
	 CONTROLVM_MESSAGE ResponseMsg[CONTROLVM_MESSAGE_MAX];

	 /* Event fixed-size message pool - does not include payload */
	 CONTROLVM_MESSAGE EventMsg[CONTROLVM_MESSAGE_MAX];

	 /* Ack fixed-size message pool - does not include payload */
	 CONTROLVM_MESSAGE EventAckMsg[CONTROLVM_MESSAGE_MAX];

	 /* Message stored during IOVM creation to be reused after crash */
	 CONTROLVM_MESSAGE SavedCrashMsg[CONTROLVM_CRASHMSG_MAX];
} ULTRA_CONTROLVM_CHANNEL_PROTOCOL;

/* Offsets for VM channel attributes... */
#define VM_CH_REQ_QUEUE_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, RequestQueue)
#define VM_CH_RESP_QUEUE_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, ResponseQueue)
#define VM_CH_EVENT_QUEUE_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, EventQueue)
#define VM_CH_ACK_QUEUE_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, EventAckQueue)
#define VM_CH_REQ_MSG_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, RequestMsg)
#define VM_CH_RESP_MSG_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, ResponseMsg)
#define VM_CH_EVENT_MSG_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, EventMsg)
#define VM_CH_ACK_MSG_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, EventAckMsg)
#define VM_CH_CRASH_MSG_OFFSET \
	offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, SavedCrashMsg)

/* The following header will be located at the beginning of PayloadVmOffset for
 *  various ControlVm commands. The receiver of a ControlVm command with a
 *  PayloadVmOffset will dereference this address and then use ConnectionOffset,
 *  InitiatorOffset, and TargetOffset to get the location of UTF-8 formatted
 *  strings that can be parsed to obtain command-specific information. The value
 *  of TotalLength should equal PayloadBytes.  The format of the strings at
 *  PayloadVmOffset will take different forms depending on the message.  See the
 *  following Wiki page for more information:
 *  https://ustr-linux-1.na.uis.unisys.com/spar/index.php/ControlVm_Parameters_Area
 */
typedef struct _ULTRA_CONTROLVM_PARAMETERS_HEADER  {
	u32 TotalLength;
	u32 HeaderLength;
	u32 ConnectionOffset;
	u32 ConnectionLength;
	u32 InitiatorOffset;
	u32 InitiatorLength;
	u32 TargetOffset;
	u32 TargetLength;
	u32 ClientOffset;
	u32 ClientLength;
	u32 NameOffset;
	u32 NameLength;
	uuid_le Id;
	u32 Revision;
	u32 Reserved;		/* Natural alignment */
} ULTRA_CONTROLVM_PARAMETERS_HEADER;

#endif				/* __CONTROLVMCHANNEL_H__ */
