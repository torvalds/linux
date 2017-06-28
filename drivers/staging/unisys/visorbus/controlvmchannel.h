/* Copyright (C) 2010 - 2015 UNISYS CORPORATION
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
#include "channel.h"

/* {2B3C2D10-7EF5-4ad8-B966-3448B7386B3D} */
#define SPAR_CONTROLVM_CHANNEL_PROTOCOL_UUID	\
		UUID_LE(0x2b3c2d10, 0x7ef5, 0x4ad8, \
			0xb9, 0x66, 0x34, 0x48, 0xb7, 0x38, 0x6b, 0x3d)

#define ULTRA_CONTROLVM_CHANNEL_PROTOCOL_SIGNATURE \
	ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define CONTROLVM_MESSAGE_MAX 64

/* Must increment this whenever you insert or delete fields within
 * this channel struct.  Also increment whenever you change the meaning
 * of fields within this channel struct so as to break pre-existing
 * software.  Note that you can usually add fields to the END of the
 * channel struct withOUT needing to increment this.
 */
#define ULTRA_CONTROLVM_CHANNEL_PROTOCOL_VERSIONID  1

#define SPAR_CONTROLVM_CHANNEL_OK_CLIENT(ch)           \
	spar_check_channel_client(ch, \
		SPAR_CONTROLVM_CHANNEL_PROTOCOL_UUID, \
		"controlvm", \
		sizeof(struct spar_controlvm_channel_protocol), \
		ULTRA_CONTROLVM_CHANNEL_PROTOCOL_VERSIONID, \
		ULTRA_CONTROLVM_CHANNEL_PROTOCOL_SIGNATURE)

/* Defines for various channel queues */
#define CONTROLVM_QUEUE_REQUEST		0
#define CONTROLVM_QUEUE_RESPONSE	1
#define CONTROLVM_QUEUE_EVENT		2
#define CONTROLVM_QUEUE_ACK		3

/* Max num of messages stored during IOVM creation to be reused after crash */
#define CONTROLVM_CRASHMSG_MAX		2

struct spar_segment_state  {
	/* Bit 0: May enter other states */
	u16 enabled:1;
	/* Bit 1: Assigned to active partition */
	u16 active:1;
	/* Bit 2: Configure message sent to service/server */
	u16 alive:1;
	/* Bit 3: similar to partition state ShuttingDown */
	u16 revoked:1;
	/* Bit 4: memory (device/port number) has been selected by Command */
	u16 allocated:1;
	/* Bit 5: has been introduced to the service/guest partition */
	u16 known:1;
	/* Bit 6: service/Guest partition has responded to introduction */
	u16 ready:1;
	/* Bit 7: resource is configured and operating */
	u16 operating:1;
/* Note: don't use high bit unless we need to switch to ushort
 * which is non-compliant
 */
};

static const struct spar_segment_state segment_state_running = {
	1, 1, 1, 0, 1, 1, 1, 1
};

static const struct spar_segment_state segment_state_paused = {
	1, 1, 1, 0, 1, 1, 1, 0
};

static const struct spar_segment_state segment_state_standby = {
	1, 1, 0, 0, 1, 1, 1, 0
};

/* Ids for commands that may appear in either queue of a ControlVm channel.
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
	/* SWITCH commands required Parameter: SwitchNumber */
	/* BUS commands required Parameter: BusNumber */
	CONTROLVM_BUS_CREATE = 0x101,			/* CP --> SP, GP */
	CONTROLVM_BUS_DESTROY = 0x102,			/* CP --> SP, GP */
	CONTROLVM_BUS_CONFIGURE = 0x104,		/* CP --> SP */
	CONTROLVM_BUS_CHANGESTATE = 0x105,		/* CP --> SP, GP */
	CONTROLVM_BUS_CHANGESTATE_EVENT = 0x106,	/* SP, GP --> CP */
/* DEVICE commands required Parameter: BusNumber, DeviceNumber */

	CONTROLVM_DEVICE_CREATE = 0x201,		/* CP --> SP, GP */
	CONTROLVM_DEVICE_DESTROY = 0x202,		/* CP --> SP, GP */
	CONTROLVM_DEVICE_CONFIGURE = 0x203,		/* CP --> SP */
	CONTROLVM_DEVICE_CHANGESTATE = 0x204,		/* CP --> SP, GP */
	CONTROLVM_DEVICE_CHANGESTATE_EVENT = 0x205,	/* SP, GP --> CP */
	CONTROLVM_DEVICE_RECONFIGURE = 0x206,		/* CP --> Boot */
/* CHIPSET commands */
	CONTROLVM_CHIPSET_INIT = 0x301,			/* CP --> SP, GP */
	CONTROLVM_CHIPSET_STOP = 0x302,			/* CP --> SP, GP */
	CONTROLVM_CHIPSET_READY = 0x304,		/* CP --> SP */
	CONTROLVM_CHIPSET_SELFTEST = 0x305,		/* CP --> SP */

};

struct irq_info {
	u64 reserved1;

	 /* specifies interrupt handle. It is used to retrieve the
	  *   corresponding interrupt pin from Monitor; and the
	  *   interrupt pin is used to connect to the corresponding
	  *   interrupt.  Used by IOPart-GP only.
	  */
	u64 recv_irq_handle;

	 /* specifies interrupt vector. It, interrupt pin, and shared are
	  *   used to connect to the corresponding interrupt.  Used by
	  *   IOPart-GP only.
	  */
	u32 recv_irq_vector;

    /* specifies if the recvInterrupt is shared.  It, interrupt pin
     * and vector are used to connect to 0 = not shared; 1 = shared.
     * the corresponding interrupt.  Used by IOPart-GP only.
     */
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
	u64 boot_to_fw_ui:1;		/* Bit 0: Stop in uefi ui */
	u64 clear_nvram:1;		/* Bit 1: Clear NVRAM */
	u64 clear_cmos:1;		/* Bit 2: Clear CMOS */
	u64 boot_to_tool:1;		/* Bit 3: Run install tool */
	/* remaining bits are available */
};

enum ultra_chipset_feature {
	ULTRA_CHIPSET_FEATURE_REPLY = 0x00000001,
	ULTRA_CHIPSET_FEATURE_PARA_HOTPLUG = 0x00000002,
};

/* This is the common structure that is at the beginning of every
 *  ControlVm message (both commands and responses) in any ControlVm
 *  queue.  Commands are easily distinguished from responses by
 *  looking at the flags.response field.
 */
struct controlvm_message_header  {
	u32 id;		/* See CONTROLVM_ID. */
	/* For requests, indicates the message type. */
	/* For responses, indicates the type of message we are responding to. */

	/* Includes size of this struct + size of message */
	u32 message_size;
	/* Index of segment containing Vm message/information */
	u32 segment_index;
	/* Error status code or result of  message completion */
	u32 completion_status;
	struct  {
		/* =1 in a response to signify failure */
		u32 failed:1;
		/* =1 in all messages that expect a response */
		u32 response_expected:1;
		/* =1 in all bus & device-related messages where the message
		 * receiver is to act as the bus or device server
		 */
		u32 server:1;
		/* =1 for testing use only (Control and Command ignore this */
		u32 test_message:1;
		/* =1 if there are forthcoming responses/acks associated
		 * with this message
		 */
		u32 partial_completion:1;
		/* =1 this is to let us know to preserve channel contents */
		u32 preserve:1;
		/* =1 the DiagWriter is active in the Diagnostic Partition */
		u32 writer_in_diag:1;
	} flags;
	/* Natural alignment */
	u32 reserved;
	/* Identifies the particular message instance */
	u64 message_handle;
	/* request instances with the corresponding response instance. */
	/* Offset of payload area from start of this instance */
	u64 payload_vm_offset;
	/* Maximum bytes allocated in payload area of ControlVm segment */
	u32 payload_max_bytes;
	/* Actual number of bytes of payload area to copy between IO/Command */
	u32 payload_bytes;
	/* if non-zero, there is a payload to copy. */
};

struct controlvm_packet_device_create  {
	u32 bus_no;		/* bus # (0..n-1) from the msg receiver's end */
	u32 dev_no;		/* bus-relative (0..n-1) device number */
	/* Guest physical address of the channel, which can be dereferenced by
	 * the receiver of this ControlVm command
	 */
	u64 channel_addr;
	u64 channel_bytes;	/* specifies size of the channel in bytes */
	uuid_le data_type_uuid;	/* specifies format of data in channel */
	uuid_le dev_inst_uuid;	/* instance guid for the device */
	struct irq_info intr;	/* specifies interrupt information */
};	/* for CONTROLVM_DEVICE_CREATE */

struct controlvm_packet_device_configure  {
	/* bus # (0..n-1) from the msg receiver's perspective */
	u32 bus_no;
	/* Control uses header SegmentIndex field to access bus number... */
	u32 dev_no;	      /* bus-relative (0..n-1) device number */
} ;	/* for CONTROLVM_DEVICE_CONFIGURE */

struct controlvm_message_device_create {
	struct controlvm_message_header header;
	struct controlvm_packet_device_create packet;
};	/* total 128 bytes */

struct controlvm_message_device_configure  {
	struct controlvm_message_header header;
	struct controlvm_packet_device_configure packet;
};	/* total 56 bytes */

/* This is the format for a message in any ControlVm queue. */
struct controlvm_message_packet  {
	union  {
		struct  {
	/* bus # (0..n-1) from the msg receiver's perspective */
			u32 bus_no;
	/* indicates the max number of devices on this bus */
			u32 dev_count;
	/* Guest physical address of the channel, which can be
	 * dereferenced by the receiver of this ControlVm command
	 */
			u64 channel_addr;
			u64 channel_bytes;	/* size of the channel */
	/* indicates format of data in bus channel*/
			uuid_le bus_data_type_uuid;
			uuid_le bus_inst_uuid;	/* instance uuid for the bus */
		} create_bus;	/* for CONTROLVM_BUS_CREATE */
		struct  {
	/* bus # (0..n-1) from the msg receiver's perspective */
			u32 bus_no;
			u32 reserved;	/* Natural alignment purposes */
		} destroy_bus;	/* for CONTROLVM_BUS_DESTROY */
		struct  {
	/* bus # (0..n-1) from the receiver's perspective */
			u32 bus_no;
			u32 reserved1;	/* for alignment purposes */
	/* This is used to convert guest physical address to physical address */
			u64 guest_handle;
			u64 recv_bus_irq_handle;
				/* specifies interrupt info. It is used by SP
				 * to register to receive interrupts from the
				 * CP. This interrupt is used for bus level
				 * notifications.  The corresponding
				 * sendBusInterruptHandle is kept in CP.
				 */
		} configure_bus;	/* for CONTROLVM_BUS_CONFIGURE */
		/* for CONTROLVM_DEVICE_CREATE */
		struct controlvm_packet_device_create create_device;
		struct  {
		/* bus # (0..n-1) from the msg receiver's perspective */
			u32 bus_no;
			u32 dev_no;	/* bus-relative (0..n-1) device # */
		} destroy_device;	/* for CONTROLVM_DEVICE_DESTROY */
		/* for CONTROLVM_DEVICE_CONFIGURE */
		struct controlvm_packet_device_configure configure_device;
		struct  {
		/* bus # (0..n-1) from the msg receiver's perspective */
			u32 bus_no;
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
				/* =1 if message is for a physical device */
				u32 phys_device:1;
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
			/* indicates the max number of busses */
			u32 bus_count;
			/* indicates the max number of switches */
			u32 switch_count;
			enum ultra_chipset_feature features;
			u32 platform_number;	/* Platform Number */
		} init_chipset;	/* for CONTROLVM_CHIPSET_INIT */
		struct  {
			u32 options;	/* reserved */
			u32 test;	/* bit 0 set to run embedded selftest */
		} chipset_selftest;	/* for CONTROLVM_CHIPSET_SELFTEST */
		/* a physical address of something, that can be dereferenced
		 * by the receiver of this ControlVm command
		 */
		u64 addr;
		/* a handle of something (depends on command id) */
		u64 handle;
	};
};

/* All messages in any ControlVm queue have this layout. */
struct controlvm_message {
	struct controlvm_message_header hdr;
	struct controlvm_message_packet cmd;
};

struct spar_controlvm_channel_protocol {
	struct channel_header header;
	u64 gp_controlvm;	/* guest phys addr of this channel */
	u64 gp_partition_tables;/* guest phys addr of partition tables */
	u64 gp_diag_guest;	/* guest phys addr of diagnostic channel */
	u64 gp_boot_romdisk;/* guest phys addr of (read* only) Boot ROM disk */
	u64 gp_boot_ramdisk;/* guest phys addr of writable Boot RAM disk */
	u64 gp_acpi_table;	/* guest phys addr of acpi table */
	u64 gp_control_channel;/* guest phys addr of control channel */
	u64 gp_diag_romdisk;/* guest phys addr of diagnostic ROM disk */
	u64 gp_nvram;	/* guest phys addr of NVRAM channel */
	u64 request_payload_offset;	/* Offset to request payload area */
	u64 event_payload_offset;	/* Offset to event payload area */
	/* Bytes available in request payload area */
	u32 request_payload_bytes;
	u32 event_payload_bytes;/* Bytes available in event payload area */
	u32 control_channel_bytes;
	u32 nvram_channel_bytes;	/* Bytes in PartitionNvram segment */
	u32 message_bytes;		/* sizeof(CONTROLVM_MESSAGE) */
	u32 message_count;		/* CONTROLVM_MESSAGE_MAX */
	u64 gp_smbios_table;		/* guest phys addr of SMBIOS tables */
	u64 gp_physical_smbios_table;	/* guest phys addr of SMBIOS table  */
	/* ULTRA_MAX_GUESTS_PER_SERVICE */
	char gp_reserved[2688];

	/* guest physical address of EFI firmware image base  */
	u64 virtual_guest_firmware_image_base;

	/* guest physical address of EFI firmware entry point  */
	u64 virtual_guest_firmware_entry_point;

	/* guest EFI firmware image size  */
	u64 virtual_guest_firmware_image_size;

	/* GPA = 1MB where EFI firmware image is copied to  */
	u64 virtual_guest_firmware_boot_base;
	u64 virtual_guest_image_base;
	u64 virtual_guest_image_size;
	u64 prototype_control_channel_offset;
	u64 virtual_guest_partition_handle;
	/* Restore Action field to restore the guest partition */
	u16 restore_action;
	/* For Windows guests it shows if the visordisk is in dump mode */
	u16 dump_action;
	u16 nvram_fail_count;
	u16 saved_crash_message_count;	/* = CONTROLVM_CRASHMSG_MAX */
	/* Offset to request payload area needed for crash dump */
	u32 saved_crash_message_offset;
	/* Type of error encountered during installation */
	u32 installation_error;
	u32 installation_text_id;	/* Id of string to display */
	/* Number of remaining installation  steps (for progress bars) */
	u16 installation_remaining_steps;
	/* ULTRA_TOOL_ACTIONS Installation Action field */
	u8 tool_action;
	u8 reserved;		/* alignment */
	struct efi_spar_indication efi_spar_ind;
	struct efi_spar_indication efi_spar_ind_supported;
	u32 sp_reserved;
	/* Force signals to begin on 128-byte cache line */
	u8 reserved2[28];
	/* guest partition uses this queue to send requests to Control */
	struct signal_queue_header request_queue;
	/* Control uses this queue to respond to service or guest
	 * partition requests
	 */
	struct signal_queue_header response_queue;
	/* Control uses this queue to send events to guest partition */
	struct signal_queue_header event_queue;
	/* Service or guest partition  uses this queue to ack Control events */
	struct signal_queue_header event_ack_queue;
	 /* Request fixed-size message pool - does not include payload */
	 struct controlvm_message request_msg[CONTROLVM_MESSAGE_MAX];

	 /* Response fixed-size message pool - does not include payload */
	 struct controlvm_message response_msg[CONTROLVM_MESSAGE_MAX];

	 /* Event fixed-size message pool - does not include payload */
	 struct controlvm_message event_msg[CONTROLVM_MESSAGE_MAX];

	 /* Ack fixed-size message pool - does not include payload */
	 struct controlvm_message event_ack_msg[CONTROLVM_MESSAGE_MAX];

	 /* Message stored during IOVM creation to be reused after crash */
	 struct controlvm_message saved_crash_msg[CONTROLVM_CRASHMSG_MAX];
};

/* The following header will be located at the beginning of PayloadVmOffset for
 * various ControlVm commands. The receiver of a ControlVm command with a
 * PayloadVmOffset will dereference this address and then use connection_offset,
 * initiator_offset, and target_offset to get the location of UTF-8 formatted
 * strings that can be parsed to obtain command-specific information. The value
 * of total_length should equal PayloadBytes. The format of the strings at
 * PayloadVmOffset will take different forms depending on the message.
 */
struct spar_controlvm_parameters_header {
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
	uuid_le id;
	u32 revision;
	u32 reserved;		/* Natural alignment */
};

/* General Errors------------------------------------------------------[0-99] */
#define CONTROLVM_RESP_SUCCESS                                  0
#define CONTROLVM_RESP_ALREADY_DONE                             1
#define CONTROLVM_RESP_IOREMAP_FAILED                           2
#define CONTROLVM_RESP_KMALLOC_FAILED                           3
#define CONTROLVM_RESP_ID_UNKNOWN                               4
#define CONTROLVM_RESP_ID_INVALID_FOR_CLIENT                    5

/* CONTROLVM_INIT_CHIPSET-------------------------------------------[100-199] */
#define CONTROLVM_RESP_CLIENT_SWITCHCOUNT_NONZERO               100
#define CONTROLVM_RESP_EXPECTED_CHIPSET_INIT                    101

/* Maximum Limit----------------------------------------------------[200-299] */
#define CONTROLVM_RESP_ERROR_MAX_BUSES		201	/* BUS_CREATE */
#define CONTROLVM_RESP_ERROR_MAX_DEVICES        202	/* DEVICE_CREATE */
/* Payload and Parameter Related------------------------------------[400-499] */
#define CONTROLVM_RESP_PAYLOAD_INVALID		400	/* SWITCH_ATTACHEXTPORT,
							 * DEVICE_CONFIGURE
							 */
#define CONTROLVM_RESP_INITIATOR_PARAMETER_INVALID 401  /* Multiple */
#define CONTROLVM_RESP_TARGET_PARAMETER_INVALID    402  /* DEVICE_CONFIGURE */
#define CONTROLVM_RESP_CLIENT_PARAMETER_INVALID    403  /* DEVICE_CONFIGURE */
/* Specified[Packet Structure] Value-------------------------------[500-599] */
#define CONTROLVM_RESP_BUS_INVALID                 500	/* SWITCH_ATTACHINTPORT,
							 * BUS_CONFIGURE,
							 * DEVICE_CREATE,
							 * DEVICE_CONFIG
							 * DEVICE_DESTROY
							 */
#define CONTROLVM_RESP_DEVICE_INVALID           501 /* SWITCH_ATTACHINTPORT */
						    /* DEVICE_CREATE,
						     * DEVICE_CONFIGURE,
						     * DEVICE_DESTROY
						     */
#define CONTROLVM_RESP_CHANNEL_INVALID          502 /* DEVICE_CREATE,
						     * DEVICE_CONFIGURE
						     */
/* Partition Driver Callback Interface----------------------[600-699] */
#define CONTROLVM_RESP_VIRTPCI_DRIVER_FAILURE   604       /* BUS_CREATE,
							   * BUS_DESTROY,
							   * DEVICE_CREATE,
							   * DEVICE_DESTROY
							   */
/* Unable to invoke VIRTPCI callback */
#define CONTROLVM_RESP_VIRTPCI_DRIVER_CALLBACK_ERROR 605  /* BUS_CREATE,
							   * BUS_DESTROY,
							   * DEVICE_CREATE,
							   * DEVICE_DESTROY
							   */
/* VIRTPCI Callback returned error */
#define CONTROLVM_RESP_GENERIC_DRIVER_CALLBACK_ERROR 606
							/* SWITCH_ATTACHEXTPORT,
							 * SWITCH_DETACHEXTPORT
							 * DEVICE_CONFIGURE
							 */

/* generic device callback returned error */
/* Bus Related------------------------------------------------------[700-799] */
#define CONTROLVM_RESP_ERROR_BUS_DEVICE_ATTACHED 700	/* BUS_DESTROY */
/* Channel Related--------------------------------------------------[800-899] */
#define CONTROLVM_RESP_CHANNEL_TYPE_UNKNOWN 800	        /* GET_CHANNELINFO,
							 * DEVICE_DESTROY
							 */
#define CONTROLVM_RESP_CHANNEL_SIZE_TOO_SMALL 801	/* DEVICE_CREATE */
/* Chipset Shutdown Related---------------------------------------[1000-1099] */
#define CONTROLVM_RESP_CHIPSET_SHUTDOWN_FAILED            1000
#define CONTROLVM_RESP_CHIPSET_SHUTDOWN_ALREADY_ACTIVE    1001

/* Chipset Stop Related-------------------------------------------[1100-1199] */
#define CONTROLVM_RESP_CHIPSET_STOP_FAILED_BUS            1100
#define CONTROLVM_RESP_CHIPSET_STOP_FAILED_SWITCH         1101

/* Device Related-------------------------------------------------[1400-1499] */
#define CONTROLVM_RESP_DEVICE_UDEV_TIMEOUT                1400

#endif				/* __CONTROLVMCHANNEL_H__ */
