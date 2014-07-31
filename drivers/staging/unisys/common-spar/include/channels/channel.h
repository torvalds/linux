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

#include "commontypes.h"

#define SIGNATURE_16(A, B) ((A) | (B<<8))
#define SIGNATURE_32(A, B, C, D) \
	(SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) \
	(SIGNATURE_32(A, B, C, D) | ((u64)(SIGNATURE_32(E, F, G, H)) << 32))

#ifndef lengthof
#define lengthof(TYPE, MEMBER) (sizeof(((TYPE *)0)->MEMBER))
#endif
#ifndef COVERQ
#define COVERQ(v, d)  (((v)+(d)-1) / (d))
#endif
#ifndef COVER
#define COVER(v, d)   ((d)*COVERQ(v, d))
#endif

#define ULTRA_CHANNEL_PROTOCOL_SIGNATURE  SIGNATURE_32('E', 'C', 'N', 'L')

typedef enum {
	CHANNELSRV_UNINITIALIZED = 0,	/* channel is in an undefined state */
	CHANNELSRV_READY = 1	/* channel has been initialized by server */
} CHANNEL_SERVERSTATE;

typedef enum {
	CHANNELCLI_DETACHED = 0,
	CHANNELCLI_DISABLED = 1,	/* client can see channel but is NOT
					 * allowed to use it unless given TBD
					 * explicit request (should actually be
					 * < DETACHED) */
	CHANNELCLI_ATTACHING = 2,	/* legacy EFI client request
					 * for EFI server to attach */
	CHANNELCLI_ATTACHED = 3,	/* idle, but client may want
					 * to use channel any time */
	CHANNELCLI_BUSY = 4,	/* client either wants to use or is
				 * using channel */
	CHANNELCLI_OWNED = 5	/* "no worries" state - client can
				 * access channel anytime */
} CHANNEL_CLIENTSTATE;
static inline const u8 *
ULTRA_CHANNELCLI_STRING(u32 v)
{
	switch (v) {
	case CHANNELCLI_DETACHED:
		return (const u8 *) ("DETACHED");
	case CHANNELCLI_DISABLED:
		return (const u8 *) ("DISABLED");
	case CHANNELCLI_ATTACHING:
		return (const u8 *) ("ATTACHING");
	case CHANNELCLI_ATTACHED:
		return (const u8 *) ("ATTACHED");
	case CHANNELCLI_BUSY:
		return (const u8 *) ("BUSY");
	case CHANNELCLI_OWNED:
		return (const u8 *) ("OWNED");
	default:
		break;
	}
	return (const u8 *) ("?");
}

#define ULTRA_CHANNELSRV_IS_READY(x)     ((x) == CHANNELSRV_READY)
#define ULTRA_CHANNEL_SERVER_READY(pChannel) \
	(ULTRA_CHANNELSRV_IS_READY(readl(&(pChannel)->SrvState)))

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

#define ULTRA_CHANNEL_CLIENT_CHK_TRANSITION(old, new, chanId, logCtx,	\
					    file, line)			\
	do {								\
		if (!ULTRA_VALID_CHANNELCLI_TRANSITION(old, new))	\
			UltraLogEvent(logCtx,				\
				      CHANNELSTATE_DIAG_EVENTID_TRANSITERR, \
				      CHANNELSTATE_DIAG_SEVERITY, \
				      CHANNELSTATE_DIAG_SUBSYS,		\
				      __func__, __LINE__,		\
				      "%s Channel StateTransition INVALID! (%s) %s(%d)-->%s(%d) @%s:%d\n", \
				      chanId, "CliState<x>",		\
				      ULTRA_CHANNELCLI_STRING(old),	\
				      old,				\
				      ULTRA_CHANNELCLI_STRING(new),	\
				      new,				\
				      PathName_Last_N_Nodes((u8 *)file, 4), \
				      line);				\
	} while (0)

#define ULTRA_CHANNEL_CLIENT_TRANSITION(pChan, chanId,			\
					newstate, logCtx)		\
	do {								\
		ULTRA_CHANNEL_CLIENT_CHK_TRANSITION(			\
			readl(&(((CHANNEL_HEADER __iomem *) \
				 (pChan))->CliStateOS)),		\
			newstate,					\
			chanId, logCtx, __FILE__, __LINE__);		\
		UltraLogEvent(logCtx, CHANNELSTATE_DIAG_EVENTID_TRANSITOK, \
			CHANNELSTATE_DIAG_SEVERITY, \
			      CHANNELSTATE_DIAG_SUBSYS,			\
			      __func__, __LINE__,			\
			      "%s Channel StateTransition (%s) %s(%d)-->%s(%d) @%s:%d\n", \
			      chanId, "CliStateOS",			\
			      ULTRA_CHANNELCLI_STRING( \
				      readl(&((CHANNEL_HEADER __iomem *) \
					      (pChan))->CliStateOS)),	\
			      readl(&((CHANNEL_HEADER __iomem *) \
				      (pChan))->CliStateOS),		\
			      ULTRA_CHANNELCLI_STRING(newstate),	\
			      newstate,					\
			      PathName_Last_N_Nodes(__FILE__, 4), __LINE__); \
		writel(newstate, &((CHANNEL_HEADER __iomem *) \
				   (pChan))->CliStateOS);		\
		MEMORYBARRIER;						\
	} while (0)

#define ULTRA_CHANNEL_CLIENT_ACQUIRE_OS(pChan, chanId, logCtx)	\
	ULTRA_channel_client_acquire_os(pChan, chanId, logCtx,		\
					(char *)__FILE__, __LINE__,	\
					(char *)__func__)
#define ULTRA_CHANNEL_CLIENT_RELEASE_OS(pChan, chanId, logCtx)	\
	ULTRA_channel_client_release_os(pChan, chanId, logCtx,	\
		(char *)__FILE__, __LINE__, (char *)__func__)

/* Values for ULTRA_CHANNEL_PROTOCOL.CliErrorBoot: */
/* throttling invalid boot channel statetransition error due to client
 * disabled */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_DISABLED    0x01

/* throttling invalid boot channel statetransition error due to client
 * not attached */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_NOTATTACHED 0x02

/* throttling invalid boot channel statetransition error due to busy channel */
#define ULTRA_CLIERRORBOOT_THROTTLEMSG_BUSY        0x04

/* Values for ULTRA_CHANNEL_PROTOCOL.CliErrorOS: */
/* throttling invalid guest OS channel statetransition error due to
 * client disabled */
#define ULTRA_CLIERROROS_THROTTLEMSG_DISABLED      0x01

/* throttling invalid guest OS channel statetransition error due to
 * client not attached */
#define ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED   0x02

/* throttling invalid guest OS channel statetransition error due to
 * busy channel */
#define ULTRA_CLIERROROS_THROTTLEMSG_BUSY          0x04

/* Values for ULTRA_CHANNEL_PROTOCOL.Features: This define exists so
* that windows guest can look at the FeatureFlags in the io channel,
* and configure the windows driver to use interrupts or not based on
* this setting.  This flag is set in uislib after the
* ULTRA_VHBA_init_channel is called.  All feature bits for all
* channels should be defined here.  The io channel feature bits are
* defined right here */
#define ULTRA_IO_DRIVER_ENABLES_INTS (0x1ULL << 1)
#define ULTRA_IO_CHANNEL_IS_POLLING (0x1ULL << 3)
#define ULTRA_IO_IOVM_IS_OK_WITH_DRIVER_DISABLING_INTS (0x1ULL << 4)
#define ULTRA_IO_DRIVER_DISABLES_INTS (0x1ULL << 5)
#define ULTRA_IO_DRIVER_SUPPORTS_ENHANCED_RCVBUF_CHECKING (0x1ULL << 6)

#pragma pack(push, 1)		/* both GCC and VC now allow this pragma */
/* Common Channel Header */
typedef struct _CHANNEL_HEADER {
	u64 Signature;		/* Signature */
	u32 LegacyState;	/* DEPRECATED - being replaced by */
	/* /              SrvState, CliStateBoot, and CliStateOS below */
	u32 HeaderSize;		/* sizeof(CHANNEL_HEADER) */
	u64 Size;		/* Total size of this channel in bytes */
	u64 Features;		/* Flags to modify behavior */
	uuid_le Type;		/* Channel type: data, bus, control, etc. */
	u64 PartitionHandle;	/* ID of guest partition */
	u64 Handle;		/* Device number of this channel in client */
	u64 oChannelSpace;	/* Offset in bytes to channel specific area */
	u32 VersionId;		/* CHANNEL_HEADER Version ID */
	u32 PartitionIndex;	/* Index of guest partition */
	uuid_le ZoneGuid;		/* Guid of Channel's zone */
	u32 oClientString;	/* offset from channel header to
				 * nul-terminated ClientString (0 if
				 * ClientString not present) */
	u32 CliStateBoot;	/* CHANNEL_CLIENTSTATE of pre-boot
				 * EFI client of this channel */
	u32 CmdStateCli;	/* CHANNEL_COMMANDSTATE (overloaded in
				 * Windows drivers, see ServerStateUp,
				 * ServerStateDown, etc) */
	u32 CliStateOS;		/* CHANNEL_CLIENTSTATE of Guest OS
				 * client of this channel */
	u32 ChannelCharacteristics;	/* CHANNEL_CHARACTERISTIC_<xxx> */
	u32 CmdStateSrv;	/* CHANNEL_COMMANDSTATE (overloaded in
				 * Windows drivers, see ServerStateUp,
				 * ServerStateDown, etc) */
	u32 SrvState;		/* CHANNEL_SERVERSTATE */
	u8 CliErrorBoot;	/* bits to indicate err states for
				 * boot clients, so err messages can
				 * be throttled */
	u8 CliErrorOS;		/* bits to indicate err states for OS
				 * clients, so err messages can be
				 * throttled */
	u8 Filler[1];		/* Pad out to 128 byte cacheline */
	/* Please add all new single-byte values below here */
	u8 RecoverChannel;
} CHANNEL_HEADER, *pCHANNEL_HEADER, ULTRA_CHANNEL_PROTOCOL;

#define ULTRA_CHANNEL_ENABLE_INTS (0x1ULL << 0)

/* Subheader for the Signal Type variation of the Common Channel */
typedef struct _SIGNAL_QUEUE_HEADER {
	/* 1st cache line */
	u32 VersionId;		/* SIGNAL_QUEUE_HEADER Version ID */
	u32 Type;		/* Queue type: storage, network */
	u64 Size;		/* Total size of this queue in bytes */
	u64 oSignalBase;	/* Offset to signal queue area */
	u64 FeatureFlags;	/* Flags to modify behavior */
	u64 NumSignalsSent;	/* Total # of signals placed in this queue */
	u64 NumOverflows;	/* Total # of inserts failed due to
				 * full queue */
	u32 SignalSize;		/* Total size of a signal for this queue */
	u32 MaxSignalSlots;	/* Max # of slots in queue, 1 slot is
				 * always empty */
	u32 MaxSignals;		/* Max # of signals in queue
				 * (MaxSignalSlots-1) */
	u32 Head;		/* Queue head signal # */
	/* 2nd cache line */
	u64 NumSignalsReceived;	/* Total # of signals removed from this queue */
	u32 Tail;		/* Queue tail signal # (on separate
				 * cache line) */
	u32 Reserved1;		/* Reserved field */
	u64 Reserved2;		/* Resrved field */
	u64 ClientQueue;
	u64 NumInterruptsReceived;	/* Total # of Interrupts received.  This
					 * is incremented by the ISR in the
					 * guest windows driver */
	u64 NumEmptyCnt;	/* Number of times that visor_signal_remove
				 * is called and returned Empty
				 * Status. */
	u32 ErrorFlags;		/* Error bits set during SignalReinit
				 * to denote trouble with client's
				 * fields */
	u8 Filler[12];		/* Pad out to 64 byte cacheline */
} SIGNAL_QUEUE_HEADER, *pSIGNAL_QUEUE_HEADER;

#pragma pack(pop)

#define SignalInit(chan, QHDRFLD, QDATAFLD, QDATATYPE, ver, typ)	\
	do {								\
		MEMSET(&chan->QHDRFLD, 0, sizeof(chan->QHDRFLD));	\
		chan->QHDRFLD.VersionId = ver;				\
		chan->QHDRFLD.Type = typ;				\
		chan->QHDRFLD.Size = sizeof(chan->QDATAFLD);		\
		chan->QHDRFLD.SignalSize = sizeof(QDATATYPE);		\
		chan->QHDRFLD.oSignalBase = (u64)(chan->QDATAFLD)-	\
			(u64)(&chan->QHDRFLD);				\
		chan->QHDRFLD.MaxSignalSlots =				\
			sizeof(chan->QDATAFLD)/sizeof(QDATATYPE);	\
		chan->QHDRFLD.MaxSignals = chan->QHDRFLD.MaxSignalSlots-1; \
	} while (0)

/* Generic function useful for validating any type of channel when it is
 * received by the client that will be accessing the channel.
 * Note that <logCtx> is only needed for callers in the EFI environment, and
 * is used to pass the EFI_DIAG_CAPTURE_PROTOCOL needed to log messages.
 */
static inline int
ULTRA_check_channel_client(void __iomem *pChannel,
			   uuid_le expectedTypeGuid,
			   char *channelName,
			   u64 expectedMinBytes,
			   u32 expectedVersionId,
			   u64 expectedSignature,
			   char *fileName, int lineNumber, void *logCtx)
{
	if (uuid_le_cmp(expectedTypeGuid, NULL_UUID_LE) != 0)
		/* caller wants us to verify type GUID */
		if (MEMCMP_IO(&(((CHANNEL_HEADER __iomem *) (pChannel))->Type),
			   &expectedTypeGuid, sizeof(uuid_le)) != 0) {
			CHANNEL_GUID_MISMATCH(expectedTypeGuid, channelName,
					      "type", expectedTypeGuid,
					      ((CHANNEL_HEADER __iomem *)
					       (pChannel))->Type, fileName,
					      lineNumber, logCtx);
			return 0;
		}
	if (expectedMinBytes > 0)	/* caller wants us to verify
					 * channel size */
		if (readq(&((CHANNEL_HEADER __iomem *)
			   (pChannel))->Size) < expectedMinBytes) {
			CHANNEL_U64_MISMATCH(expectedTypeGuid, channelName,
					     "size", expectedMinBytes,
					     readq(&((CHANNEL_HEADER __iomem *)
						     (pChannel))->Size),
					     fileName,
					     lineNumber, logCtx);
			return 0;
		}
	if (expectedVersionId > 0)	/* caller wants us to verify
					 * channel version */
		if (readl(&((CHANNEL_HEADER __iomem *) (pChannel))->VersionId)
		    != expectedVersionId) {
			CHANNEL_U32_MISMATCH(expectedTypeGuid, channelName,
					     "version", expectedVersionId,
					     readl(&((CHANNEL_HEADER __iomem *)
						     (pChannel))->VersionId),
					     fileName, lineNumber, logCtx);
			return 0;
		}
	if (expectedSignature > 0)	/* caller wants us to verify
					 * channel signature */
		if (readq(&((CHANNEL_HEADER __iomem *) (pChannel))->Signature)
		    != expectedSignature) {
			CHANNEL_U64_MISMATCH(expectedTypeGuid, channelName,
					     "signature", expectedSignature,
					     readq(&((CHANNEL_HEADER __iomem *)
						     (pChannel))->Signature),
					     fileName,
					     lineNumber, logCtx);
			return 0;
		}
	return 1;
}

/* Generic function useful for validating any type of channel when it is about
 * to be initialized by the server of the channel.
 * Note that <logCtx> is only needed for callers in the EFI environment, and
 * is used to pass the EFI_DIAG_CAPTURE_PROTOCOL needed to log messages.
 */
static inline int
ULTRA_check_channel_server(uuid_le typeGuid,
			   char *channelName,
			   u64 expectedMinBytes,
			   u64 actualBytes,
			   char *fileName, int lineNumber, void *logCtx)
{
	if (expectedMinBytes > 0)	/* caller wants us to verify
					 * channel size */
		if (actualBytes < expectedMinBytes) {
			CHANNEL_U64_MISMATCH(typeGuid, channelName, "size",
					     expectedMinBytes, actualBytes,
					     fileName, lineNumber, logCtx);
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
PathName_Last_N_Nodes(u8 *s, unsigned int n)
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
				 * looking at it!! */
		if ((*p == '/') || (*p == '\\'))
			n--;
	}
	return p + 1;
}

static inline int
ULTRA_channel_client_acquire_os(void __iomem *pChannel, u8 *chanId,
				void *logCtx, char *file, int line, char *func)
{
	CHANNEL_HEADER __iomem *pChan = pChannel;

	if (readl(&pChan->CliStateOS) == CHANNELCLI_DISABLED) {
		if ((readb(&pChan->CliErrorOS)
		     & ULTRA_CLIERROROS_THROTTLEMSG_DISABLED) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&pChan->CliErrorOS) |
			       ULTRA_CLIERROROS_THROTTLEMSG_DISABLED,
			       &pChan->CliErrorOS);
			/* throttle until acquire successful */

			UltraLogEvent(logCtx,
				      CHANNELSTATE_DIAG_EVENTID_TRANSITERR,
				      CHANNELSTATE_DIAG_SEVERITY,
				      CHANNELSTATE_DIAG_SUBSYS, func, line,
				      "%s Channel StateTransition INVALID! - acquire failed because OS client DISABLED @%s:%d\n",
				      chanId, PathName_Last_N_Nodes(
					      (u8 *) file, 4), line);
		}
		return 0;
	}
	if ((readl(&pChan->CliStateOS) != CHANNELCLI_OWNED)
	    && (readl(&pChan->CliStateBoot) == CHANNELCLI_DISABLED)) {
		/* Our competitor is DISABLED, so we can transition to OWNED */
		UltraLogEvent(logCtx, CHANNELSTATE_DIAG_EVENTID_TRANSITOK,
			      CHANNELSTATE_DIAG_SEVERITY,
			      CHANNELSTATE_DIAG_SUBSYS, func, line,
			      "%s Channel StateTransition (%s) %s(%d)-->%s(%d) @%s:%d\n",
			      chanId, "CliStateOS",
			      ULTRA_CHANNELCLI_STRING(
				      readl(&pChan->CliStateOS)),
			      readl(&pChan->CliStateOS),
			      ULTRA_CHANNELCLI_STRING(CHANNELCLI_OWNED),
			      CHANNELCLI_OWNED,
			      PathName_Last_N_Nodes((u8 *) file, 4), line);
		writel(CHANNELCLI_OWNED, &pChan->CliStateOS);
		MEMORYBARRIER;
	}
	if (readl(&pChan->CliStateOS) == CHANNELCLI_OWNED) {
		if (readb(&pChan->CliErrorOS) != 0) {
			/* we are in an error msg throttling state;
			 * come out of it */
			UltraLogEvent(logCtx,
				      CHANNELSTATE_DIAG_EVENTID_TRANSITOK,
				      CHANNELSTATE_DIAG_SEVERITY,
				      CHANNELSTATE_DIAG_SUBSYS, func, line,
				      "%s Channel OS client acquire now successful @%s:%d\n",
				      chanId, PathName_Last_N_Nodes((u8 *) file,
								    4), line);
			writeb(0, &pChan->CliErrorOS);
		}
		return 1;
	}

	/* We have to do it the "hard way".  We transition to BUSY,
	* and can use the channel iff our competitor has not also
	* transitioned to BUSY. */
	if (readl(&pChan->CliStateOS) != CHANNELCLI_ATTACHED) {
		if ((readb(&pChan->CliErrorOS)
		     & ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&pChan->CliErrorOS) |
			       ULTRA_CLIERROROS_THROTTLEMSG_NOTATTACHED,
			       &pChan->CliErrorOS);
			/* throttle until acquire successful */
			UltraLogEvent(logCtx,
				      CHANNELSTATE_DIAG_EVENTID_TRANSITERR,
				      CHANNELSTATE_DIAG_SEVERITY,
				      CHANNELSTATE_DIAG_SUBSYS, func, line,
				      "%s Channel StateTransition INVALID! - acquire failed because OS client NOT ATTACHED (state=%s(%d)) @%s:%d\n",
				      chanId,
				      ULTRA_CHANNELCLI_STRING(
					      readl(&pChan->CliStateOS)),
				      readl(&pChan->CliStateOS),
				      PathName_Last_N_Nodes((u8 *) file, 4),
				      line);
		}
		return 0;
	}
	writel(CHANNELCLI_BUSY, &pChan->CliStateOS);
	MEMORYBARRIER;
	if (readl(&pChan->CliStateBoot) == CHANNELCLI_BUSY) {
		if ((readb(&pChan->CliErrorOS)
		     & ULTRA_CLIERROROS_THROTTLEMSG_BUSY) == 0) {
			/* we are NOT throttling this message */
			writeb(readb(&pChan->CliErrorOS) |
			       ULTRA_CLIERROROS_THROTTLEMSG_BUSY,
			       &pChan->CliErrorOS);
			/* throttle until acquire successful */
			UltraLogEvent(logCtx,
				      CHANNELSTATE_DIAG_EVENTID_TRANSITBUSY,
				      CHANNELSTATE_DIAG_SEVERITY,
				      CHANNELSTATE_DIAG_SUBSYS, func, line,
				      "%s Channel StateTransition failed - host OS acquire failed because boot BUSY @%s:%d\n",
				      chanId, PathName_Last_N_Nodes((u8 *) file,
								    4), line);
		}
		/* reset busy */
		writel(CHANNELCLI_ATTACHED, &pChan->CliStateOS);
		MEMORYBARRIER;
		return 0;
	}
	if (readb(&pChan->CliErrorOS) != 0) {
		/* we are in an error msg throttling state; come out of it */
		UltraLogEvent(logCtx, CHANNELSTATE_DIAG_EVENTID_TRANSITOK,
			      CHANNELSTATE_DIAG_SEVERITY,
			      CHANNELSTATE_DIAG_SUBSYS, func, line,
			      "%s Channel OS client acquire now successful @%s:%d\n",
			      chanId, PathName_Last_N_Nodes((u8 *) file, 4),
			      line);
		writeb(0, &pChan->CliErrorOS);
	}
	return 1;
}

static inline void
ULTRA_channel_client_release_os(void __iomem *pChannel, u8 *chanId,
				void *logCtx, char *file, int line, char *func)
{
	CHANNEL_HEADER __iomem *pChan = pChannel;
	if (readb(&pChan->CliErrorOS) != 0) {
		/* we are in an error msg throttling state; come out of it */
		UltraLogEvent(logCtx, CHANNELSTATE_DIAG_EVENTID_TRANSITOK,
			      CHANNELSTATE_DIAG_SEVERITY,
			      CHANNELSTATE_DIAG_SUBSYS, func, line,
			      "%s Channel OS client error state cleared @%s:%d\n",
			      chanId, PathName_Last_N_Nodes((u8 *) file, 4),
			      line);
		writeb(0, &pChan->CliErrorOS);
	}
	if (readl(&pChan->CliStateOS) == CHANNELCLI_OWNED)
		return;
	if (readl(&pChan->CliStateOS) != CHANNELCLI_BUSY) {
		UltraLogEvent(logCtx, CHANNELSTATE_DIAG_EVENTID_TRANSITERR,
			      CHANNELSTATE_DIAG_SEVERITY,
			      CHANNELSTATE_DIAG_SUBSYS, func, line,
			      "%s Channel StateTransition INVALID! - release failed because OS client NOT BUSY (state=%s(%d)) @%s:%d\n",
			      chanId,
			      ULTRA_CHANNELCLI_STRING(
				      readl(&pChan->CliStateOS)),
			      readl(&pChan->CliStateOS),
			      PathName_Last_N_Nodes((u8 *) file, 4), line);
		/* return; */
	}
	writel(CHANNELCLI_ATTACHED, &pChan->CliStateOS); /* release busy */
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

unsigned char visor_signal_insert(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
				  void *pSignal);

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

unsigned char visor_signal_remove(CHANNEL_HEADER __iomem *pChannel, u32 Queue,
				  void *pSignal);

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
unsigned int SignalRemoveAll(pCHANNEL_HEADER pChannel, u32 Queue,
			     void *pSignal);

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
unsigned char visor_signalqueue_empty(CHANNEL_HEADER __iomem *pChannel,
				      u32 Queue);

#endif
