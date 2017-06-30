/*
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
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

#ifndef __CHANNEL_H__
#define __CHANNEL_H__

#include <linux/types.h>
#include <linux/io.h>
#include <linux/uuid.h>

#define __SUPERVISOR_CHANNEL_H__

#define SIGNATURE_16(A, B) ((A) | ((B) << 8))
#define SIGNATURE_32(A, B, C, D) \
	(SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
	(SIGNATURE_32(A, B, C, D) | ((u64)(SIGNATURE_32(E, F, G, H)) << 32))

#ifndef COVER
#define COVER(v, d) ((d) * DIV_ROUND_UP(v, d))
#endif

#define VISOR_CHANNEL_SIGNATURE SIGNATURE_32('E', 'C', 'N', 'L')

/*
 * enum channel_serverstate
 * @CHANNELSRV_UNINITIALIZED: Channel is in an undefined state.
 * @CHANNELSRV_READY:	      Channel has been initialized by server.
 */
enum channel_serverstate {
	CHANNELSRV_UNINITIALIZED = 0,
	CHANNELSRV_READY = 1
};

/*
 * enum channel_clientstate
 * @CHANNELCLI_DETACHED:
 * @CHANNELCLI_DISABLED:  Client can see channel but is NOT allowed to use it
 *			  unless given TBD* explicit request
 *			  (should actually be < DETACHED).
 * @CHANNELCLI_ATTACHING: Legacy EFI client request for EFI server to attach.
 * @CHANNELCLI_ATTACHED:  Idle, but client may want to use channel any time.
 * @CHANNELCLI_BUSY:	  Client either wants to use or is using channel.
 * @CHANNELCLI_OWNED:	  "No worries" state - client can access channel
 *			  anytime.
 */
enum channel_clientstate {
	CHANNELCLI_DETACHED = 0,
	CHANNELCLI_DISABLED = 1,
	CHANNELCLI_ATTACHING = 2,
	CHANNELCLI_ATTACHED = 3,
	CHANNELCLI_BUSY = 4,
	CHANNELCLI_OWNED = 5
};

/* Values for VISORA_CHANNEL_PROTOCOL.CliErrorBoot: */

/*
 * Throttling invalid boot channel statetransition error due to client
 * disabled.
 */
#define VISOR_CLIERRORBOOT_THROTTLEMSG_DISABLED 0x01

/*
 * Throttling invalid boot channel statetransition error due to client
 * not attached.
 */
#define VISOR_CLIERRORBOOT_THROTTLEMSG_NOTATTACHED 0x02

/* Throttling invalid boot channel statetransition error due to busy channel */
#define VISOR_CLIERRORBOOT_THROTTLEMSG_BUSY 0x04

/*
 * Values for VISOR_CHANNEL_PROTOCOL.Features: This define exists so that
 * windows guest can look at the FeatureFlags in the io channel, and configure
 * the windows driver to use interrupts or not based on this setting.  This flag
 * is set in uislib after the VISOR_VHBA_init_channel is called.  All feature
 * bits for all channels should be defined here.  The io channel feature bits
 * are defined right here
 */
#define VISOR_DRIVER_ENABLES_INTS (0x1ULL << 1)
#define VISOR_CHANNEL_IS_POLLING (0x1ULL << 3)
#define VISOR_IOVM_OK_DRIVER_DISABLING_INTS (0x1ULL << 4)
#define VISOR_DRIVER_DISABLES_INTS (0x1ULL << 5)
#define VISOR_DRIVER_ENHANCED_RCVBUF_CHECKING (0x1ULL << 6)

/*
 * struct channel_header - Common Channel Header
 * @signature:	       Signature.
 * @legacy_state:      DEPRECATED - being replaced by.
 * @header_size:       sizeof(struct channel_header).
 * @size:	       Total size of this channel in bytes.
 * @features:	       Flags to modify behavior.
 * @chtype:	       Channel type: data, bus, control, etc..
 * @partition_handle:  ID of guest partition.
 * @handle:	       Device number of this channel in client.
 * @ch_space_offset:   Offset in bytes to channel specific area.
 * @version_id:	       Struct channel_header Version ID.
 * @partition_index:   Index of guest partition.
 * @zone_uuid:	       Guid of Channel's zone.
 * @cli_str_offset:    Offset from channel header to null-terminated
 *		       ClientString (0 if ClientString not present).
 * @cli_state_boot:    CHANNEL_CLIENTSTATE of pre-boot EFI client of this
 *		       channel.
 * @cmd_state_cli:     CHANNEL_COMMANDSTATE (overloaded in Windows drivers, see
 *		       ServerStateUp, ServerStateDown, etc).
 * @cli_state_os:      CHANNEL_CLIENTSTATE of Guest OS client of this channel.
 * @ch_characteristic: CHANNEL_CHARACTERISTIC_<xxx>.
 * @cmd_state_srv:     CHANNEL_COMMANDSTATE (overloaded in Windows drivers, see
 *		       ServerStateUp, ServerStateDown, etc).
 * @srv_state:	       CHANNEL_SERVERSTATE.
 * @cli_error_boot:    Bits to indicate err states for boot clients, so err
 *		       messages can be throttled.
 * @cli_error_os:      Bits to indicate err states for OS clients, so err
 *		       messages can be throttled.
 * @filler:	       Pad out to 128 byte cacheline.
 * @recover_channel:   Please add all new single-byte values below here.
 */
struct channel_header {
	u64 signature;
	u32 legacy_state;
	/* SrvState, CliStateBoot, and CliStateOS below */
	u32 header_size;
	u64 size;
	u64 features;
	uuid_le chtype;
	u64 partition_handle;
	u64 handle;
	u64 ch_space_offset;
	u32 version_id;
	u32 partition_index;
	uuid_le zone_uuid;
	u32 cli_str_offset;
	u32 cli_state_boot;
	u32 cmd_state_cli;
	u32 cli_state_os;
	u32 ch_characteristic;
	u32 cmd_state_srv;
	u32 srv_state;
	u8 cli_error_boot;
	u8 cli_error_os;
	u8 filler[1];
	u8 recover_channel;
} __packed;

#define VISOR_CHANNEL_ENABLE_INTS (0x1ULL << 0)

/*
 * struct signal_queue_header - Subheader for the Signal Type variation of the
 *                              Common Channel.
 * @version:	      SIGNAL_QUEUE_HEADER Version ID.
 * @chtype:	      Queue type: storage, network.
 * @size:	      Total size of this queue in bytes.
 * @sig_base_offset:  Offset to signal queue area.
 * @features:	      Flags to modify behavior.
 * @num_sent:	      Total # of signals placed in this queue.
 * @num_overflows:    Total # of inserts failed due to full queue.
 * @signal_size:      Total size of a signal for this queue.
 * @max_slots:        Max # of slots in queue, 1 slot is always empty.
 * @max_signals:      Max # of signals in queue (MaxSignalSlots-1).
 * @head:	      Queue head signal #.
 * @num_received:     Total # of signals removed from this queue.
 * @tail:	      Queue tail signal.
 * @reserved1:	      Reserved field.
 * @reserved2:	      Reserved field.
 * @client_queue:
 * @num_irq_received: Total # of Interrupts received. This is incremented by the
 *		      ISR in the guest windows driver.
 * @num_empty:	      Number of times that visor_signal_remove is called and
 *		      returned Empty Status.
 * @errorflags:	      Error bits set during SignalReinit to denote trouble with
 *		      client's fields.
 * @filler:	      Pad out to 64 byte cacheline.
 */
struct signal_queue_header {
	/* 1st cache line */
	u32 version;
	u32 chtype;
	u64 size;
	u64 sig_base_offset;
	u64 features;
	u64 num_sent;
	u64 num_overflows;
	u32 signal_size;
	u32 max_slots;
	u32 max_signals;
	u32 head;
	/* 2nd cache line */
	u64 num_received;
	u32 tail;
	u32 reserved1;
	u64 reserved2;
	u64 client_queue;
	u64 num_irq_received;
	u64 num_empty;
	u32 errorflags;
	u8 filler[12];
} __packed;

/*
 * Generic function useful for validating any type of channel when it is
 * received by the client that will be accessing the channel.
 * Note that <logCtx> is only needed for callers in the EFI environment, and
 * is used to pass the EFI_DIAG_CAPTURE_PROTOCOL needed to log messages.
 */
static inline int
visor_check_channel(struct channel_header *ch,
		    uuid_le expected_uuid,
		    char *chname,
		    u64 expected_min_bytes,
		    u32 expected_version,
		    u64 expected_signature)
{
	if (uuid_le_cmp(expected_uuid, NULL_UUID_LE) != 0) {
		/* caller wants us to verify type GUID */
		if (uuid_le_cmp(ch->chtype, expected_uuid) != 0) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=type expected=%pUL actual=%pUL\n",
			       chname, &expected_uuid,
			       &expected_uuid, &ch->chtype);
			return 0;
		}
	}
	/* verify channel size */
	if (expected_min_bytes > 0) {
		if (ch->size < expected_min_bytes) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=size expected=0x%-8.8Lx actual=0x%-8.8Lx\n",
			       chname, &expected_uuid,
			       (unsigned long long)expected_min_bytes,
			       ch->size);
			return 0;
		}
	}
	/* verify channel version */
	if (expected_version > 0) {
		if (ch->version_id != expected_version) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=version expected=0x%-8.8lx actual=0x%-8.8x\n",
			       chname, &expected_uuid,
			       (unsigned long)expected_version,
			       ch->version_id);
			return 0;
		}
	}
	/* verify channel signature */
	if (expected_signature > 0) {
		if (ch->signature != expected_signature) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=signature expected=0x%-8.8Lx actual=0x%-8.8Lx\n",
			       chname, &expected_uuid,
			       expected_signature, ch->signature);
			return 0;
		}
	}
	return 1;
}

/* CHANNEL Guids */
/* {414815ed-c58c-11da-95a9-00e08161165f} */
#define VISOR_VHBA_CHANNEL_UUID \
	UUID_LE(0x414815ed, 0xc58c, 0x11da, \
		0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le visor_vhba_channel_uuid = VISOR_VHBA_CHANNEL_UUID;
#define VISOR_VHBA_CHANNEL_UUID_STR \
	"414815ed-c58c-11da-95a9-00e08161165f"
/* {8cd5994d-c58e-11da-95a9-00e08161165f} */
#define VISOR_VNIC_CHANNEL_UUID \
	UUID_LE(0x8cd5994d, 0xc58e, 0x11da, \
		0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le visor_vnic_channel_uuid = VISOR_VNIC_CHANNEL_UUID;
#define VISOR_VNIC_CHANNEL_UUID_STR \
	"8cd5994d-c58e-11da-95a9-00e08161165f"
/* {72120008-4AAB-11DC-8530-444553544200} */
#define VISOR_SIOVM_UUID \
	UUID_LE(0x72120008, 0x4AAB, 0x11DC, \
		0x85, 0x30, 0x44, 0x45, 0x53, 0x54, 0x42, 0x00)
static const uuid_le visor_siovm_uuid = VISOR_SIOVM_UUID;

#endif
