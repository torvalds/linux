// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
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

#include "visorchannel.h"

/* {2B3C2D10-7EF5-4ad8-B966-3448B7386B3D} */
#define VISOR_CONTROLVM_CHANNEL_GUID \
	GUID_INIT(0x2b3c2d10, 0x7ef5, 0x4ad8, \
		  0xb9, 0x66, 0x34, 0x48, 0xb7, 0x38, 0x6b, 0x3d)

#define CONTROLVM_MESSAGE_MAX 64

/*
 * Must increment this whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software. Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define VISOR_CONTROLVM_CHANNEL_VERSIONID 1

/* Defines for various channel queues */
#define CONTROLVM_QUEUE_REQUEST		0
#define CONTROLVM_QUEUE_RESPONSE	1
#define CONTROLVM_QUEUE_EVENT		2
#define CONTROLVM_QUEUE_ACK		3

/* Max num of messages stored during IOVM creation to be reused after crash */
#define CONTROLVM_CRASHMSG_MAX 2

/*
 * struct visor_segment_state
 * @enabled:   May enter other states.
 * @active:    Assigned to active partition.
 * @alive:     Configure message sent to service/server.
 * @revoked:   Similar to partition state ShuttingDown.
 * @allocated: Memory (device/port number) has been selected by Command.
 * @known:     Has been introduced to the service/guest partition.
 * @ready:     Service/Guest partition has responded to introduction.
 * @operating: Resource is configured and operating.
 * @reserved:  Natural alignment.
 *
 * Note: Don't use high bit unless we need to switch to ushort which is
 * non-compliant.
 */
struct visor_segment_state  {
	u16 enabled:1;
	u16 active:1;
	u16 alive:1;
	u16 revoked:1;
	u16 allocated:1;
	u16 known:1;
	u16 ready:1;
	u16 operating:1;
	u16 reserved:8;
} __packed;

static const struct visor_segment_state segment_state_running = {
	1, 1, 1, 0, 1, 1, 1, 1
};

static const struct visor_segment_state segment_state_paused = {
	1, 1, 1, 0, 1, 1, 1, 0
};

static const struct visor_segment_state segment_state_standby = {
	1, 1, 0, 0, 1, 1, 1, 0
};

/*
 * enum controlvm_id
 * @CONTROLVM_INVALID:
 * @CONTROLVM_BUS_CREATE:		CP --> SP, GP.
 * @CONTROLVM_BUS_DESTROY:		CP --> SP, GP.
 * @CONTROLVM_BUS_CONFIGURE:		CP --> SP.
 * @CONTROLVM_BUS_CHANGESTATE:		CP --> SP, GP.
 * @CONTROLVM_BUS_CHANGESTATE_EVENT:	SP, GP --> CP.
 * @CONTROLVM_DEVICE_CREATE:		CP --> SP, GP.
 * @CONTROLVM_DEVICE_DESTROY:		CP --> SP, GP.
 * @CONTROLVM_DEVICE_CONFIGURE:		CP --> SP.
 * @CONTROLVM_DEVICE_CHANGESTATE:	CP --> SP, GP.
 * @CONTROLVM_DEVICE_CHANGESTATE_EVENT:	SP, GP --> CP.
 * @CONTROLVM_DEVICE_RECONFIGURE:	CP --> Boot.
 * @CONTROLVM_CHIPSET_INIT:		CP --> SP, GP.
 * @CONTROLVM_CHIPSET_STOP:		CP --> SP, GP.
 * @CONTROLVM_CHIPSET_READY:		CP --> SP.
 * @CONTROLVM_CHIPSET_SELFTEST:		CP --> SP.
 *
 * Ids for commands that may appear in either queue of a ControlVm channel.
 *
 * Commands that are initiated by the command partition (CP), by an IO or
 * console service partition (SP), or by a guest partition (GP) are:
 * - issued on the RequestQueue queue (q #0) in the ControlVm channel
 * - responded to on the ResponseQueue queue (q #1) in the ControlVm channel
 *
 * Events that are initiated by an IO or console service partition (SP) or
 * by a guest partition (GP) are:
 * - issued on the EventQueue queue (q #2) in the ControlVm channel
 * - responded to on the EventAckQueue queue (q #3) in the ControlVm channel
 */
enum controlvm_id {
	CONTROLVM_INVALID = 0,
	/*
	 * SWITCH commands required Parameter: SwitchNumber.
	 * BUS commands required Parameter: BusNumber
	 */
	CONTROLVM_BUS_CREATE = 0x101,
	CONTROLVM_BUS_DESTROY = 0x102,
	CONTROLVM_BUS_CONFIGURE = 0x104,
	CONTROLVM_BUS_CHANGESTATE = 0x105,
	CONTROLVM_BUS_CHANGESTATE_EVENT = 0x106,
	/* DEVICE commands required Parameter: BusNumber, DeviceNumber */
	CONTROLVM_DEVICE_CREATE = 0x201,
	CONTROLVM_DEVICE_DESTROY = 0x202,
	CONTROLVM_DEVICE_CONFIGURE = 0x203,
	CONTROLVM_DEVICE_CHANGESTATE = 0x204,
	CONTROLVM_DEVICE_CHANGESTATE_EVENT = 0x205,
	CONTROLVM_DEVICE_RECONFIGURE = 0x206,
	/* CHIPSET commands */
	CONTROLVM_CHIPSET_INIT = 0x301,
	CONTROLVM_CHIPSET_STOP = 0x302,
	CONTROLVM_CHIPSET_READY = 0x304,
	CONTROLVM_CHIPSET_SELFTEST = 0x305,
};

/*
 * struct irq_info
 * @reserved1:	     Natural alignment purposes
 * @recv_irq_handle: Specifies interrupt handle. It is used to retrieve the
 *		     corresponding interrupt pin from Monitor; and the interrupt
 *		     pin is used to connect to the corresponding interrupt.
 *		     Used by IOPart-GP only.
 * @recv_irq_vector: Specifies interrupt vector. It, interrupt pin, and shared
 *		     are used to connect to the corresponding interrupt.
 *		     Used by IOPart-GP only.
 * @recv_irq_shared: Specifies if the recvInterrupt is shared.  It, interrupt
 *		     pin and vector are used to connect to 0 = not shared;
 *		     1 = shared the corresponding interrupt.
 *		     Used by IOPart-GP only.
 * @reserved:	     Natural alignment purposes
 */
struct irq_info {
	u64 reserved1;
	u64 recv_irq_handle;
	u32 recv_irq_vector;
	u8 recv_irq_shared;
	u8 reserved[3];
} __packed;

/*
 * struct efi_visor_indication
 * @boot_to_fw_ui: Stop in UEFI UI
 * @clear_nvram:   Clear NVRAM
 * @clear_cmos:	   Clear CMOS
 * @boot_to_tool:  Run install tool
 * @reserved:	   Natural alignment
 */
struct efi_visor_indication  {
	u64 boot_to_fw_ui:1;
	u64 clear_nvram:1;
	u64 clear_cmos:1;
	u64 boot_to_tool:1;
	/* Remaining bits are available */
	u64 reserved:60;
} __packed;

enum visor_chipset_feature {
	VISOR_CHIPSET_FEATURE_REPLY = 0x00000001,
	VISOR_CHIPSET_FEATURE_PARA_HOTPLUG = 0x00000002,
};

/*
 * struct controlvm_message_header
 * @id:		       See CONTROLVM_ID.
 * @message_size:      Includes size of this struct + size of message.
 * @segment_index:     Index of segment containing Vm message/information.
 * @completion_status: Error status code or result of  message completion.
 * @struct flags:
 *	@failed:	     =1 in a response to signify failure.
 *	@response_expected:  =1 in all messages that expect a response.
 *	@server:	     =1 in all bus & device-related messages where the
 *			     message receiver is to act as the bus or device
 *			     server.
 *	@test_message:	     =1 for testing use only (Control and Command
 *			     ignore this).
 *	@partial_completion: =1 if there are forthcoming responses/acks
 *                           associated with this message.
 *      @preserve:	     =1 this is to let us know to preserve channel
 *			     contents.
 *	@writer_in_diag:     =1 the DiagWriter is active in the Diagnostic
 *			     Partition.
 *	@reserve:	     Natural alignment.
 * @reserved:	       Natural alignment.
 * @message_handle:    Identifies the particular message instance.
 * @payload_vm_offset: Offset of payload area from start of this instance.
 * @payload_max_bytes: Maximum bytes allocated in payload area of ControlVm
 *		       segment.
 * @payload_bytes:     Actual number of bytes of payload area to copy between
 *		       IO/Command. If non-zero, there is a payload to copy.
 *
 * This is the common structure that is at the beginning of every
 * ControlVm message (both commands and responses) in any ControlVm
 * queue.  Commands are easily distinguished from responses by
 * looking at the flags.response field.
 */
struct controlvm_message_header  {
	u32 id;
	/*
	 * For requests, indicates the message type. For responses, indicates
	 * the type of message we are responding to.
	 */
	u32 message_size;
	u32 segment_index;
	u32 completion_status;
	struct  {
		u32 failed:1;
		u32 response_expected:1;
		u32 server:1;
		u32 test_message:1;
		u32 partial_completion:1;
		u32 preserve:1;
		u32 writer_in_diag:1;
		u32 reserve:25;
	} __packed flags;
	u32 reserved;
	u64 message_handle;
	u64 payload_vm_offset;
	u32 payload_max_bytes;
	u32 payload_bytes;
} __packed;

/*
 * struct controlvm_packet_device_create - For CONTROLVM_DEVICE_CREATE
 * @bus_no:	    Bus # (0..n-1) from the msg receiver's end.
 * @dev_no:	    Bus-relative (0..n-1) device number.
 * @channel_addr:   Guest physical address of the channel, which can be
 *		    dereferenced by the receiver of this ControlVm command.
 * @channel_bytes:  Specifies size of the channel in bytes.
 * @data_type_uuid: Specifies format of data in channel.
 * @dev_inst_uuid:  Instance guid for the device.
 * @irq_info intr:  Specifies interrupt information.
 */
struct controlvm_packet_device_create  {
	u32 bus_no;
	u32 dev_no;
	u64 channel_addr;
	u64 channel_bytes;
	guid_t data_type_guid;
	guid_t dev_inst_guid;
	struct irq_info intr;
} __packed;

/*
 * struct controlvm_packet_device_configure - For CONTROLVM_DEVICE_CONFIGURE
 * @bus_no: Bus number (0..n-1) from the msg receiver's perspective.
 * @dev_no: Bus-relative (0..n-1) device number.
 */
struct controlvm_packet_device_configure  {
	u32 bus_no;
	u32 dev_no;
} __packed;

/* Total 128 bytes */
struct controlvm_message_device_create {
	struct controlvm_message_header header;
	struct controlvm_packet_device_create packet;
} __packed;

/* Total 56 bytes */
struct controlvm_message_device_configure  {
	struct controlvm_message_header header;
	struct controlvm_packet_device_configure packet;
} __packed;

/*
 * struct controlvm_message_packet - This is the format for a message in any
 *                                   ControlVm queue.
 * @struct create_bus:		For CONTROLVM_BUS_CREATE.
 *	@bus_no:	     Bus # (0..n-1) from the msg receiver's perspective.
 *	@dev_count:	     Indicates the max number of devices on this bus.
 *	@channel_addr:	     Guest physical address of the channel, which can be
 *			     dereferenced by the receiver of this ControlVM
 *			     command.
 *	@channel_bytes:	     Size of the channel.
 *	@bus_data_type_uuid: Indicates format of data in bus channel.
 *	@bus_inst_uuid:	     Instance uuid for the bus.
 *
 * @struct destroy_bus:		For CONTROLVM_BUS_DESTROY.
 *	@bus_no: Bus # (0..n-1) from the msg receiver's perspective.
 *	@reserved: Natural alignment purposes.
 *
 * @struct configure_bus:	For CONTROLVM_BUS_CONFIGURE.
 *	@bus_no:	      Bus # (0..n-1) from the receiver's perspective.
 *	@reserved1:	      For alignment purposes.
 *	@guest_handle:	      This is used to convert guest physical address to
 *			      physical address.
 *	@recv_bus_irq_handle: Specifies interrupt info. It is used by SP to
 *			      register to receive interrupts from the CP. This
 *			      interrupt is used for bus level notifications.
 *			      The corresponding sendBusInterruptHandle is kept
 *			      in CP.
 *
 * @struct create_device:	For CONTROLVM_DEVICE_CREATE.
 *
 * @struct destroy_device:	For CONTROLVM_DEVICE_DESTROY.
 *	@bus_no: Bus # (0..n-1) from the msg receiver's perspective.
 *	@dev_no: Bus-relative (0..n-1) device number.
 *
 * @struct configure_device:	For CONTROLVM_DEVICE_CONFIGURE.
 *
 * @struct reconfigure_device:	For CONTROLVM_DEVICE_RECONFIGURE.
 *	@bus_no: Bus # (0..n-1) from the msg receiver's perspective.
 *	@dev_no: Bus-relative (0..n-1) device number.
 *
 * @struct bus_change_state:	For CONTROLVM_BUS_CHANGESTATE.
 *	@bus_no:
 *	@struct state:
 *	@reserved: Natural alignment purposes.
 *
 * @struct device_change_state:	For CONTROLVM_DEVICE_CHANGESTATE.
 *	@bus_no:
 *	@dev_no:
 *	@struct state:
 *	@struct flags:
 *		@phys_device: =1 if message is for a physical device.
 *		@reserved:    Natural alignment.
 *		@reserved1:   Natural alignment.
 *	@reserved:    Natural alignment purposes.
 *
 * @struct device_change_state_event:	For CONTROLVM_DEVICE_CHANGESTATE_EVENT.
 *	@bus_no:
 *	@dev_no:
 *	@struct state:
 *	@reserved:     Natural alignment purposes.
 *
 * @struct init_chipset:	For CONTROLVM_CHIPSET_INIT.
 *	@bus_count:	  Indicates the max number of busses.
 *	@switch_count:    Indicates the max number of switches.
 *	@enum features:
 *	@platform_number:
 *
 * @struct chipset_selftest:	For CONTROLVM_CHIPSET_SELFTEST.
 *      @options: Reserved.
 *      @test:	  Bit 0 set to run embedded selftest.
 *
 * @addr:   A physical address of something, that can be dereferenced by the
 *	    receiver of this ControlVm command.
 *
 * @handle: A handle of something (depends on command id).
 */
struct controlvm_message_packet  {
	union  {
		struct  {
			u32 bus_no;
			u32 dev_count;
			u64 channel_addr;
			u64 channel_bytes;
			guid_t bus_data_type_guid;
			guid_t bus_inst_guid;
		} __packed create_bus;
		struct  {
			u32 bus_no;
			u32 reserved;
		} __packed destroy_bus;
		struct  {
			u32 bus_no;
			u32 reserved1;
			u64 guest_handle;
			u64 recv_bus_irq_handle;
		} __packed configure_bus;
		struct controlvm_packet_device_create create_device;
		struct  {
			u32 bus_no;
			u32 dev_no;
		} __packed destroy_device;
		struct controlvm_packet_device_configure configure_device;
		struct  {
			u32 bus_no;
			u32 dev_no;
		} __packed reconfigure_device;
		struct  {
			u32 bus_no;
			struct visor_segment_state state;
			u8 reserved[2];
		} __packed bus_change_state;
		struct  {
			u32 bus_no;
			u32 dev_no;
			struct visor_segment_state state;
			struct  {
				u32 phys_device:1;
				u32 reserved:31;
				u32 reserved1;
			} __packed flags;
			u8 reserved[2];
		} __packed device_change_state;
		struct  {
			u32 bus_no;
			u32 dev_no;
			struct visor_segment_state state;
			u8 reserved[6];
		} __packed device_change_state_event;
		struct  {
			u32 bus_count;
			u32 switch_count;
			enum visor_chipset_feature features;
			u32 platform_number;
		} __packed init_chipset;
		struct  {
			u32 options;
			u32 test;
		} __packed chipset_selftest;
		u64 addr;
		u64 handle;
	};
} __packed;

/* All messages in any ControlVm queue have this layout. */
struct controlvm_message {
	struct controlvm_message_header hdr;
	struct controlvm_message_packet cmd;
} __packed;

/*
 * struct visor_controlvm_channel
 * @struct header:
 * @gp_controlvm:			Guest phys addr of this channel.
 * @gp_partition_tables:		Guest phys addr of partition tables.
 * @gp_diag_guest:			Guest phys addr of diagnostic channel.
 * @gp_boot_romdisk:			Guest phys addr of (read* only) Boot
 *					ROM disk.
 * @gp_boot_ramdisk:			Guest phys addr of writable Boot RAM
 *					disk.
 * @gp_acpi_table:			Guest phys addr of acpi table.
 * @gp_control_channel:			Guest phys addr of control channel.
 * @gp_diag_romdisk:			Guest phys addr of diagnostic ROM disk.
 * @gp_nvram:				Guest phys addr of NVRAM channel.
 * @request_payload_offset:		Offset to request payload area.
 * @event_payload_offset:		Offset to event payload area.
 * @request_payload_bytes:		Bytes available in request payload area.
 * @event_payload_bytes:		Bytes available in event payload area.
 * @control_channel_bytes:
 * @nvram_channel_bytes:		Bytes in PartitionNvram segment.
 * @message_bytes:			sizeof(CONTROLVM_MESSAGE).
 * @message_count:			CONTROLVM_MESSAGE_MAX.
 * @gp_smbios_table:			Guest phys addr of SMBIOS tables.
 * @gp_physical_smbios_table:		Guest phys addr of SMBIOS table.
 * @gp_reserved:			VISOR_MAX_GUESTS_PER_SERVICE.
 * @virtual_guest_firmware_image_base:	Guest physical address of EFI firmware
 *					image base.
 * @virtual_guest_firmware_entry_point:	Guest physical address of EFI firmware
 *					entry point.
 * @virtual_guest_firmware_image_size:	Guest EFI firmware image size.
 * @virtual_guest_firmware_boot_base:	GPA = 1MB where EFI firmware image is
 *					copied to.
 * @virtual_guest_image_base:
 * @virtual_guest_image_size:
 * @prototype_control_channel_offset:
 * @virtual_guest_partition_handle:
 * @restore_action:			Restore Action field to restore the
 *					guest partition.
 * @dump_action:			For Windows guests it shows if the
 *					visordisk is in dump mode.
 * @nvram_fail_count:
 * @saved_crash_message_count:		= CONTROLVM_CRASHMSG_MAX.
 * @saved_crash_message_offset:		Offset to request payload area needed
 *					for crash dump.
 * @installation_error:			Type of error encountered during
 *					installation.
 * @installation_text_id:		Id of string to display.
 * @installation_remaining_steps:	Number of remaining installation steps
 *					(for progress bars).
 * @tool_action:			VISOR_TOOL_ACTIONS Installation Action
 *					field.
 * @reserved: Alignment.
 * @struct efi_visor_ind:
 * @sp_reserved:
 * @reserved2:				Force signals to begin on 128-byte
 *					cache line.
 * @struct request_queue:		Guest partition uses this queue to send
 *					requests to Control.
 * @struct response_queue:		Control uses this queue to respond to
 *					service or guest partition request.
 * @struct event_queue:			Control uses this queue to send events
 *					to guest partition.
 * @struct event_ack_queue:		Service or guest partition uses this
 *					queue to ack Control events.
 * @struct request_msg:			Request fixed-size message pool -
 *					does not include payload.
 * @struct response_msg:		Response fixed-size message pool -
 *					does not include payload.
 * @struct event_msg:			Event fixed-size message pool -
 *					does not include payload.
 * @struct event_ack_msg:		Ack fixed-size message pool -
 *					does not include payload.
 * @struct saved_crash_msg:		Message stored during IOVM creation to
 *					be reused after crash.
 */
struct visor_controlvm_channel {
	struct channel_header header;
	u64 gp_controlvm;
	u64 gp_partition_tables;
	u64 gp_diag_guest;
	u64 gp_boot_romdisk;
	u64 gp_boot_ramdisk;
	u64 gp_acpi_table;
	u64 gp_control_channel;
	u64 gp_diag_romdisk;
	u64 gp_nvram;
	u64 request_payload_offset;
	u64 event_payload_offset;
	u32 request_payload_bytes;
	u32 event_payload_bytes;
	u32 control_channel_bytes;
	u32 nvram_channel_bytes;
	u32 message_bytes;
	u32 message_count;
	u64 gp_smbios_table;
	u64 gp_physical_smbios_table;
	char gp_reserved[2688];
	u64 virtual_guest_firmware_image_base;
	u64 virtual_guest_firmware_entry_point;
	u64 virtual_guest_firmware_image_size;
	u64 virtual_guest_firmware_boot_base;
	u64 virtual_guest_image_base;
	u64 virtual_guest_image_size;
	u64 prototype_control_channel_offset;
	u64 virtual_guest_partition_handle;
	u16 restore_action;
	u16 dump_action;
	u16 nvram_fail_count;
	u16 saved_crash_message_count;
	u32 saved_crash_message_offset;
	u32 installation_error;
	u32 installation_text_id;
	u16 installation_remaining_steps;
	u8 tool_action;
	u8 reserved;
	struct efi_visor_indication efi_visor_ind;
	u32 sp_reserved;
	u8 reserved2[28];
	struct signal_queue_header request_queue;
	struct signal_queue_header response_queue;
	struct signal_queue_header event_queue;
	struct signal_queue_header event_ack_queue;
	struct controlvm_message request_msg[CONTROLVM_MESSAGE_MAX];
	struct controlvm_message response_msg[CONTROLVM_MESSAGE_MAX];
	struct controlvm_message event_msg[CONTROLVM_MESSAGE_MAX];
	struct controlvm_message event_ack_msg[CONTROLVM_MESSAGE_MAX];
	struct controlvm_message saved_crash_msg[CONTROLVM_CRASHMSG_MAX];
} __packed;

/*
 * struct visor_controlvm_parameters_header
 *
 * The following header will be located at the beginning of PayloadVmOffset for
 * various ControlVm commands. The receiver of a ControlVm command with a
 * PayloadVmOffset will dereference this address and then use connection_offset,
 * initiator_offset, and target_offset to get the location of UTF-8 formatted
 * strings that can be parsed to obtain command-specific information. The value
 * of total_length should equal PayloadBytes. The format of the strings at
 * PayloadVmOffset will take different forms depending on the message.
 */
struct visor_controlvm_parameters_header {
	u32 total_length;
	u32 header_length;
	u32 connection_offset;
	u32 connection_length;
	u32 initiator_offset;
	u32 initiator_length;
	u32 target_offset;
	u32 target_length;
	u32 client_offset;
	u32 client_length;
	u32 name_offset;
	u32 name_length;
	guid_t id;
	u32 revision;
	/* Natural alignment */
	u32 reserved;
} __packed;

/* General Errors------------------------------------------------------[0-99] */
#define CONTROLVM_RESP_SUCCESS			   0
#define CONTROLVM_RESP_ALREADY_DONE		   1
#define CONTROLVM_RESP_IOREMAP_FAILED		   2
#define CONTROLVM_RESP_KMALLOC_FAILED		   3
#define CONTROLVM_RESP_ID_UNKNOWN		   4
#define CONTROLVM_RESP_ID_INVALID_FOR_CLIENT	   5
/* CONTROLVM_INIT_CHIPSET-------------------------------------------[100-199] */
#define CONTROLVM_RESP_CLIENT_SWITCHCOUNT_NONZERO  100
#define CONTROLVM_RESP_EXPECTED_CHIPSET_INIT	   101
/* Maximum Limit----------------------------------------------------[200-299] */
/* BUS_CREATE */
#define CONTROLVM_RESP_ERROR_MAX_BUSES		   201
/* DEVICE_CREATE */
#define CONTROLVM_RESP_ERROR_MAX_DEVICES	   202
/* Payload and Parameter Related------------------------------------[400-499] */
/* SWITCH_ATTACHEXTPORT, DEVICE_CONFIGURE */
#define CONTROLVM_RESP_PAYLOAD_INVALID		   400
/* Multiple */
#define CONTROLVM_RESP_INITIATOR_PARAMETER_INVALID 401
/* DEVICE_CONFIGURE */
#define CONTROLVM_RESP_TARGET_PARAMETER_INVALID	   402
/* DEVICE_CONFIGURE */
#define CONTROLVM_RESP_CLIENT_PARAMETER_INVALID	   403
/* Specified[Packet Structure] Value--------------------------------[500-599] */
/* SWITCH_ATTACHINTPORT */
/* BUS_CONFIGURE, DEVICE_CREATE, DEVICE_CONFIG, DEVICE_DESTROY */
#define CONTROLVM_RESP_BUS_INVALID		   500
/* SWITCH_ATTACHINTPORT*/
/* DEVICE_CREATE, DEVICE_CONFIGURE, DEVICE_DESTROY */
#define CONTROLVM_RESP_DEVICE_INVALID		   501
/* DEVICE_CREATE, DEVICE_CONFIGURE */
#define CONTROLVM_RESP_CHANNEL_INVALID		   502
/* Partition Driver Callback Interface------------------------------[600-699] */
/* BUS_CREATE, BUS_DESTROY, DEVICE_CREATE, DEVICE_DESTROY */
#define CONTROLVM_RESP_VIRTPCI_DRIVER_FAILURE	   604
/* Unable to invoke VIRTPCI callback. VIRTPCI Callback returned error. */
/* BUS_CREATE, BUS_DESTROY, DEVICE_CREATE, DEVICE_DESTROY */
#define CONTROLVM_RESP_VIRTPCI_DRIVER_CALLBACK_ERROR   605
/* Generic device callback returned error. */
/* SWITCH_ATTACHEXTPORT, SWITCH_DETACHEXTPORT, DEVICE_CONFIGURE */
#define CONTROLVM_RESP_GENERIC_DRIVER_CALLBACK_ERROR   606
/* Bus Related------------------------------------------------------[700-799] */
/* BUS_DESTROY */
#define CONTROLVM_RESP_ERROR_BUS_DEVICE_ATTACHED       700
/* Channel Related--------------------------------------------------[800-899] */
/* GET_CHANNELINFO, DEVICE_DESTROY */
#define CONTROLVM_RESP_CHANNEL_TYPE_UNKNOWN	       800
/* DEVICE_CREATE */
#define CONTROLVM_RESP_CHANNEL_SIZE_TOO_SMALL	       801
/* Chipset Shutdown Related---------------------------------------[1000-1099] */
#define CONTROLVM_RESP_CHIPSET_SHUTDOWN_FAILED	       1000
#define CONTROLVM_RESP_CHIPSET_SHUTDOWN_ALREADY_ACTIVE 1001
/* Chipset Stop Related-------------------------------------------[1100-1199] */
#define CONTROLVM_RESP_CHIPSET_STOP_FAILED_BUS	       1100
#define CONTROLVM_RESP_CHIPSET_STOP_FAILED_SWITCH      1101
/* Device Related-------------------------------------------------[1400-1499] */
#define CONTROLVM_RESP_DEVICE_UDEV_TIMEOUT	       1400

/* __CONTROLVMCHANNEL_H__ */
#endif
