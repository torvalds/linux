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

#ifndef __CHANNEL_H__
#define __CHANNEL_H__

#include <linux/types.h>
#include <linux/io.h>
#include <linux/uuid.h>

/*
* Whenever this file is changed a corresponding change must be made in
* the Console/ServicePart/visordiag_early/supervisor_channel.h file
* which is needed for Linux kernel compiles. These two files must be
* in sync.
*/

/* define the following to prevent include nesting in kernel header
 * files of similar abbreviated content
 */
#define __SUPERVISOR_CHANNEL_H__

#define SIGNATURE_16(A, B) ((A) | (B << 8))
#define SIGNATURE_32(A, B, C, D) \
	(SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
	(SIGNATURE_32(A, B, C, D) | ((u64)(SIGNATURE_32(E, F, G, H)) << 32))

#ifndef lengthof
#define lengthof(TYPE, MEMBER) (sizeof(((TYPE *)0)->MEMBER))
#endif
#ifndef COVERQ
#define COVERQ(v, d)  (((v) + (d) - 1) / (d))
#endif
#ifndef COVER
#define COVER(v, d)   ((d) * COVERQ(v, d))
#endif

#define ULTRA_CHANNEL_PROTOCOL_SIGNATURE  SIGNATURE_32('E', 'C', 'N', 'L')

enum channel_serverstate {
	CHANNELSRV_UNINITIALIZED = 0,	/* channel is in an undefined state */
	CHANNELSRV_READY = 1	/* channel has been initialized by server */
};

enum channel_clientstate {
	CHANNELCLI_DETACHED = 0,
	CHANNELCLI_DISABLED = 1,	/* client can see channel but is NOT
					 * allowed to use it unless given TBD
					 * explicit request (should actually be
					 * < DETACHED)
					 */
	CHANNELCLI_ATTACHING = 2,	/* legacy EFI client request
					 * for EFI server to attach
					 */
	CHANNELCLI_ATTACHED = 3,	/* idle, but client may want
					 * to use channel any time
					 */
	CHANNELCLI_BUSY = 4,	/* client either wants to use or is
				 * using channel
				 */
	CHANNELCLI_OWNED = 5	/* "no worries" state - client can */
				/* access channel anytime */
};

static inline const u8 *
ULTRA_CHANNELCLI_STRING(u32 v)
{
	switch (v) {
	case CHANNELCLI_DETACHED:
		return (const u8 *)("DETACHED");
	case CHANNELCLI_DISABLED:
		return (const u8 *)("DISABLED");
	case CHANNELCLI_ATTACHING:
		return (const u8 *)("ATTACHING");
	case CHANNELCLI_ATTACHED:
		return (const u8 *)("ATTACHED");
	case CHANNELCLI_BUSY:
		return (const u8 *)("BUSY");
	case CHANNELCLI_OWNED:
		return (const u8 *)("OWNED");
	default:
		break;
	}
	return (const u8 *)("?");
}

#define SPAR_CHANNEL_SERVER_READY(ch) \
	(readl(&(ch)->srv_state) == CHANNELSRV_READY)

#define ULTRA_VALID_CHANNELCLI_TRANSITION(o, n)				\
	(((((o) == CHANNELCLI_DETACHED) && ((n) == CHANNELCLI_DISABLED)) || \
	  (((o) == CHANNELCLI_ATTACHING) && ((n) == CHANNELCLI_DISABLED)) || \
	  (((o) == CHANNELCLI_ATTACHED) && ((n) == CHANNELCLI_DISABLED)) || \
	  (((o) == CHANNELCLI_ATTACHING) && ((n) == CHANNELCLI_DETACHED)) || \
	  (((o) == CHANNELCLI_ATTACHED) && ((n) == CHANNELCLI_DETACHED)) || \
	  (((o) == CHANNELCLI_DETACHED) && ((n) == CHANNELCLI_ATTACHING)) || \
	  (((o) == CHANNELCLI_ATTACHING) && ((n) == CHANNELCLI_ATTACHED)) || \
	  (((o) == CHANNELCLI_DETACHED) && ((n) == CHANNELCLI_ATTACHED)) || \
	  (((o) == CHANNELCLI_BUSY) && ((n) == CHANNELCLI_ATTACHED)) ||	\
	  (((o) == CHANNELCLI_ATTACHED) && ((n) == CHANNELCLI_BUSY)) ||	\
	  (((o) == CHANNELCLI_DETACHED) && ((n) == CHANNELCLI_OWNED)) || \
	  (((o) == CHANNELCLI_DISABLED) && ((n) == CHANNELCLI_OWNED)) || \
	  (((o) == CHANNELCLI_ATTACHING) && ((n) == CHANNELCLI_OWNED)) || \
	  (((o) == CHANNELCLI_ATTACHED) && ((n) == CHANNELCLI_OWNED)) || \
	  (((o) == CHANNELCLI_BUSY) && ((n) == CHANNELCLI_OWNED)) || (0)) \
	 ? (1) : (0))

/* Values for ULTRA_CHANNEL_PROTOCOL.CliErrorBoot: */
/* throttling invalid boot channel statetransition error due to client
 * disabled
 */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_DISABLED    0x01

/* throttling invalid boot channel statetransition error due to client
 * not attached
 */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_NOTATTACHED 0x02

/* throttling invalid boot channel statetransition error due to busy channel */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_BUSY        0x04

/* Values for ULTRA_CHANNEL_PROTOCOL.CliErrorOS: */
/* throttling invalid guest OS channel statetransition error due to
 * client disabled
 */
#define ULTRA_CLIERROROS_THROTTLEMSG_DISABLED      0x01

/* throttling invalid guest OS channel statetransition error due to
 * client not attached
 */
#define ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED   0x02

/* throttling invalid guest OS channel statetransition error due to
 * busy channel
 */
#define ULTRA_CLIERROROS_THROTTLEMSG_BUSY          0x04

/* Values for ULTRA_CHANNEL_PROTOCOL.Features: This define exists so
 * that windows guest can look at the FeatureFlags in the io channel,
 * and configure the windows driver to use interrupts or not based on
 * this setting.  This flag is set in uislib after the
 * ULTRA_VHBA_init_channel is called.  All feature bits for all
 * channels should be defined here.  The io channel feature bits are
 * defined right here
 */
#define ULTRA_IO_DRIVER_ENABLES_INTS (0x1ULL << 1)
#define ULTRA_IO_CHANNEL_IS_POLLING (0x1ULL << 3)
#define ULTRA_IO_IOVM_IS_OK_WITH_DRIVER_DISABLING_INTS (0x1ULL << 4)
#define ULTRA_IO_DRIVER_DISABLES_INTS (0x1ULL << 5)
#define ULTRA_IO_DRIVER_SUPPORTS_ENHANCED_RCVBUF_CHECKING (0x1ULL << 6)

/* Common Channel Header */
struct channel_header {
	u64 signature;		/* Signature */
	u32 legacy_state;	/* DEPRECATED - being replaced by */
			/* SrvState, CliStateBoot, and CliStateOS below */
	u32 header_size;	/* sizeof(struct channel_header) */
	u64 size;		/* Total size of this channel in bytes */
	u64 features;		/* Flags to modify behavior */
	uuid_le chtype;		/* Channel type: data, bus, control, etc. */
	u64 partition_handle;	/* ID of guest partition */
	u64 handle;		/* Device number of this channel in client */
	u64 ch_space_offset;	/* Offset in bytes to channel specific area */
	u32 version_id;		/* struct channel_header Version ID */
	u32 partition_index;	/* Index of guest partition */
	uuid_le zone_uuid;	/* Guid of Channel's zone */
	u32 cli_str_offset;	/* offset from channel header to
				 * nul-terminated ClientString (0 if
				 * ClientString not present)
				 */
	u32 cli_state_boot;	/* CHANNEL_CLIENTSTATE of pre-boot
				 * EFI client of this channel
				 */
	u32 cmd_state_cli;	/* CHANNEL_COMMANDSTATE (overloaded in
				 * Windows drivers, see ServerStateUp,
				 * ServerStateDown, etc)
				 */
	u32 cli_state_os;	/* CHANNEL_CLIENTSTATE of Guest OS
				 * client of this channel
				 */
	u32 ch_characteristic;	/* CHANNEL_CHARACTERISTIC_<xxx> */
	u32 cmd_state_srv;	/* CHANNEL_COMMANDSTATE (overloaded in
				 * Windows drivers, see ServerStateUp,
				 * ServerStateDown, etc)
				 */
	u32 srv_state;		/* CHANNEL_SERVERSTATE */
	u8 cli_error_boot;	/* bits to indicate err states for
				 * boot clients, so err messages can
				 * be throttled
				 */
	u8 cli_error_os;	/* bits to indicate err states for OS
				 * clients, so err messages can be
				 * throttled
				 */
	u8 filler[1];		/* Pad out to 128 byte cacheline */
	/* Please add all new single-byte values below here */
	u8 recover_channel;
} __packed;

#define ULTRA_CHANNEL_ENABLE_INTS (0x1ULL << 0)

/* Subheader for the Signal Type variation of the Common Channel */
struct signal_queue_header {
	/* 1st cache line */
	u32 version;		/* SIGNAL_QUEUE_HEADER Version ID */
	u32 chtype;		/* Queue type: storage, network */
	u64 size;		/* Total size of this queue in bytes */
	u64 sig_base_offset;	/* Offset to signal queue area */
	u64 features;		/* Flags to modify behavior */
	u64 num_sent;		/* Total # of signals placed in this queue */
	u64 num_overflows;	/* Total # of inserts failed due to
				 * full queue
				 */
	u32 signal_size;	/* Total size of a signal for this queue */
	u32 max_slots;		/* Max # of slots in queue, 1 slot is
				 * always empty
				 */
	u32 max_signals;	/* Max # of signals in queue
				 * (MaxSignalSlots-1)
				 */
	u32 head;		/* Queue head signal # */
	/* 2nd cache line */
	u64 num_received;	/* Total # of signals removed from this queue */
	u32 tail;		/* Queue tail signal */
	u32 reserved1;		/* Reserved field */
	u64 reserved2;		/* Reserved field */
	u64 client_queue;
	u64 num_irq_received;	/* Total # of Interrupts received.  This
				 * is incremented by the ISR in the
				 * guest windows driver
				 */
	u64 num_empty;		/* Number of times that visor_signal_remove
				 * is called and returned Empty Status.
				 */
	u32 errorflags;		/* Error bits set during SignalReinit
				 * to denote trouble with client's
				 * fields
				 */
	u8 filler[12];		/* Pad out to 64 byte cacheline */
} __packed;

#define spar_signal_init(chan, QHDRFLD, QDATAFLD, QDATATYPE, ver, typ)	\
	do {								\
		memset(&chan->QHDRFLD, 0, sizeof(chan->QHDRFLD));	\
		chan->QHDRFLD.version = ver;				\
		chan->QHDRFLD.chtype = typ;				\
		chan->QHDRFLD.size = sizeof(chan->QDATAFLD);		\
		chan->QHDRFLD.signal_size = sizeof(QDATATYPE);		\
		chan->QHDRFLD.sig_base_offset = (u64)(chan->QDATAFLD) -	\
			(u64)(&chan->QHDRFLD);				\
		chan->QHDRFLD.max_slots =				\
			sizeof(chan->QDATAFLD) / sizeof(QDATATYPE);	\
		chan->QHDRFLD.max_signals = chan->QHDRFLD.max_slots - 1;\
	} while (0)

/* Generic function useful for validating any type of channel when it is
 * received by the client that will be accessing the channel.
 * Note that <logCtx> is only needed for callers in the EFI environment, and
 * is used to pass the EFI_DIAG_CAPTURE_PROTOCOL needed to log messages.
 */
static inline int
spar_check_channel_client(void __iomem *ch,
			  uuid_le expected_uuid,
			  char *chname,
			  u64 expected_min_bytes,
			  u32 expected_version,
			  u64 expected_signature)
{
	if (uuid_le_cmp(expected_uuid, NULL_UUID_LE) != 0) {
		uuid_le guid;

		memcpy_fromio(&guid,
			      &((struct channel_header __iomem *)(ch))->chtype,
			      sizeof(guid));
		/* caller wants us to verify type GUID */
		if (uuid_le_cmp(guid, expected_uuid) != 0) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=type expected=%pUL actual=%pUL\n",
			       chname, &expected_uuid,
			       &expected_uuid, &guid);
			return 0;
		}
	}
	if (expected_min_bytes > 0) {	/* verify channel size */
		unsigned long long bytes =
				readq(&((struct channel_header __iomem *)
					(ch))->size);
		if (bytes < expected_min_bytes) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=size expected=0x%-8.8Lx actual=0x%-8.8Lx\n",
			       chname, &expected_uuid,
			       (unsigned long long)expected_min_bytes, bytes);
			return 0;
		}
	}
	if (expected_version > 0) {	/* verify channel version */
		unsigned long ver = readl(&((struct channel_header __iomem *)
				    (ch))->version_id);
		if (ver != expected_version) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=version expected=0x%-8.8lx actual=0x%-8.8lx\n",
			       chname, &expected_uuid,
			       (unsigned long)expected_version, ver);
			return 0;
		}
	}
	if (expected_signature > 0) {	/* verify channel signature */
		unsigned long long sig =
				readq(&((struct channel_header __iomem *)
					(ch))->signature);
		if (sig != expected_signature) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=signature expected=0x%-8.8llx actual=0x%-8.8llx\n",
			       chname, &expected_uuid,
			       expected_signature, sig);
			return 0;
		}
	}
	return 1;
}

/* Generic function useful for validating any type of channel when it is about
 * to be initialized by the server of the channel.
 * Note that <logCtx> is only needed for callers in the EFI environment, and
 * is used to pass the EFI_DIAG_CAPTURE_PROTOCOL needed to log messages.
 */
static inline int spar_check_channel_server(uuid_le typeuuid, char *name,
					    u64 expected_min_bytes,
					    u64 actual_bytes)
{
	if (expected_min_bytes > 0)	/* verify channel size */
		if (actual_bytes < expected_min_bytes) {
			pr_err("Channel mismatch on channel=%s(%pUL) field=size expected=0x%-8.8llx actual=0x%-8.8llx\n",
			       name, &typeuuid, expected_min_bytes,
			       actual_bytes);
			return 0;
		}
	return 1;
}

/* Given a file pathname <s> (with '/' or '\' separating directory nodes),
 * returns a pointer to the beginning of a node within that pathname such
 * that the number of nodes from that pointer to the end of the string is
 * NOT more than <n>.  Note that if the pathname has less than <n> nodes
 * in it, the return pointer will be to the beginning of the string.
 */
static inline u8 *
pathname_last_n_nodes(u8 *s, unsigned int n)
{
	u8 *p = s;
	unsigned int node_count = 0;

	while (*p != '\0') {
		if ((*p == '/') || (*p == '\\'))
			node_count++;
		p++;
	}
	if (node_count <= n)
		return s;
	while (n > 0) {
		p--;
		if (p == s)
			break;	/* should never happen, unless someone
				 * is changing the string while we are
				 * looking at it!!
				 */
		if ((*p == '/') || (*p == '\\'))
			n--;
	}
	return p + 1;
}

static inline int
spar_channel_client_acquire_os(void __iomem *ch, u8 *id)
{
	struct channel_header __iomem *hdr = ch;

	if (readl(&hdr->cli_state_os) == CHANNELCLI_DISABLED) {
		if ((readb(&hdr->cli_error_os)
		     & ULTRA_CLIERROROS_THROTTLEMSG_DISABLED) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&hdr->cli_error_os) |
			       ULTRA_CLIERROROS_THROTTLEMSG_DISABLED,
			       &hdr->cli_error_os);
			/* throttle until acquire successful */

			pr_info("%s Channel StateTransition INVALID! - acquire failed because OS client DISABLED\n",
				id);
		}
		return 0;
	}
	if ((readl(&hdr->cli_state_os) != CHANNELCLI_OWNED) &&
	    (readl(&hdr->cli_state_boot) == CHANNELCLI_DISABLED)) {
		/* Our competitor is DISABLED, so we can transition to OWNED */
		pr_info("%s Channel StateTransition (%s) %s(%d)-->%s(%d)\n",
			id, "cli_state_os",
			ULTRA_CHANNELCLI_STRING(readl(&hdr->cli_state_os)),
			readl(&hdr->cli_state_os),
			ULTRA_CHANNELCLI_STRING(CHANNELCLI_OWNED),
			CHANNELCLI_OWNED);
		writel(CHANNELCLI_OWNED, &hdr->cli_state_os);
		mb(); /* required for channel synch */
	}
	if (readl(&hdr->cli_state_os) == CHANNELCLI_OWNED) {
		if (readb(&hdr->cli_error_os)) {
			/* we are in an error msg throttling state;
			 * come out of it
			 */
			pr_info("%s Channel OS client acquire now successful\n",
				id);
			writeb(0, &hdr->cli_error_os);
		}
		return 1;
	}

	/* We have to do it the "hard way".  We transition to BUSY,
	 * and can use the channel iff our competitor has not also
	 * transitioned to BUSY.
	 */
	if (readl(&hdr->cli_state_os) != CHANNELCLI_ATTACHED) {
		if ((readb(&hdr->cli_error_os)
		     & ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&hdr->cli_error_os) |
			       ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED,
			       &hdr->cli_error_os);
			/* throttle until acquire successful */
			pr_info("%s Channel StateTransition INVALID! - acquire failed because OS client NOT ATTACHED (state=%s(%d))\n",
				id, ULTRA_CHANNELCLI_STRING(
						readl(&hdr->cli_state_os)),
				readl(&hdr->cli_state_os));
		}
		return 0;
	}
	writel(CHANNELCLI_BUSY, &hdr->cli_state_os);
	mb(); /* required for channel synch */
	if (readl(&hdr->cli_state_boot) == CHANNELCLI_BUSY) {
		if ((readb(&hdr->cli_error_os)
		     & ULTRA_CLIERROROS_THROTTLEMSG_BUSY) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&hdr->cli_error_os) |
			       ULTRA_CLIERROROS_THROTTLEMSG_BUSY,
			       &hdr->cli_error_os);
			/* throttle until acquire successful */
			pr_info("%s Channel StateTransition failed - host OS acquire failed because boot BUSY\n",
				id);
		}
		/* reset busy */
		writel(CHANNELCLI_ATTACHED, &hdr->cli_state_os);
		mb(); /* required for channel synch */
		return 0;
	}
	if (readb(&hdr->cli_error_os)) {
		/* we are in an error msg throttling state; come out of it */
		pr_info("%s Channel OS client acquire now successful\n", id);
		writeb(0, &hdr->cli_error_os);
	}
	return 1;
}

static inline void
spar_channel_client_release_os(void __iomem *ch, u8 *id)
{
	struct channel_header __iomem *hdr = ch;

	if (readb(&hdr->cli_error_os)) {
		/* we are in an error msg throttling state; come out of it */
		pr_info("%s Channel OS client error state cleared\n", id);
		writeb(0, &hdr->cli_error_os);
	}
	if (readl(&hdr->cli_state_os) == CHANNELCLI_OWNED)
		return;
	if (readl(&hdr->cli_state_os) != CHANNELCLI_BUSY) {
		pr_info("%s Channel StateTransition INVALID! - release failed because OS client NOT BUSY (state=%s(%d))\n",
			id, ULTRA_CHANNELCLI_STRING(
					readl(&hdr->cli_state_os)),
			readl(&hdr->cli_state_os));
		/* return; */
	}
	writel(CHANNELCLI_ATTACHED, &hdr->cli_state_os); /* release busy */
}

/*
* Routine Description:
* Tries to insert the prebuilt signal pointed to by pSignal into the nth
* Queue of the Channel pointed to by pChannel
*
* Parameters:
* pChannel: (IN) points to the IO Channel
* Queue: (IN) nth Queue of the IO Channel
* pSignal: (IN) pointer to the signal
*
* Assumptions:
* - pChannel, Queue and pSignal are valid.
* - If insertion fails due to a full queue, the caller will determine the
* retry policy (e.g. wait & try again, report an error, etc.).
*
* Return value: 1 if the insertion succeeds, 0 if the queue was
* full.
*/

unsigned char spar_signal_insert(struct channel_header __iomem *ch, u32 queue,
				 void *sig);

/*
* Routine Description:
* Removes one signal from Channel pChannel's nth Queue at the
* time of the call and copies it into the memory pointed to by
* pSignal.
*
* Parameters:
* pChannel: (IN) points to the IO Channel
* Queue: (IN) nth Queue of the IO Channel
* pSignal: (IN) pointer to where the signals are to be copied
*
* Assumptions:
* - pChannel and Queue are valid.
* - pSignal points to a memory area large enough to hold queue's SignalSize
*
* Return value: 1 if the removal succeeds, 0 if the queue was
* empty.
*/

unsigned char spar_signal_remove(struct channel_header __iomem *ch, u32 queue,
				 void *sig);

/*
* Routine Description:
* Removes all signals present in Channel pChannel's nth Queue at the
* time of the call and copies them into the memory pointed to by
* pSignal.  Returns the # of signals copied as the value of the routine.
*
* Parameters:
* pChannel: (IN) points to the IO Channel
* Queue: (IN) nth Queue of the IO Channel
* pSignal: (IN) pointer to where the signals are to be copied
*
* Assumptions:
* - pChannel and Queue are valid.
* - pSignal points to a memory area large enough to hold Queue's MaxSignals
* # of signals, each of which is Queue's SignalSize.
*
* Return value:
* # of signals copied.
*/
unsigned int spar_signal_remove_all(struct channel_header *ch, u32 queue,
				    void *sig);

/*
* Routine Description:
* Determine whether a signal queue is empty.
*
* Parameters:
* pChannel: (IN) points to the IO Channel
* Queue: (IN) nth Queue of the IO Channel
*
* Return value:
* 1 if the signal queue is empty, 0 otherwise.
*/
unsigned char spar_signalqueue_empty(struct channel_header __iomem *ch,
				     u32 queue);

#endif
