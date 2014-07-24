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

/*++
 *
 * Module Name:
 *
 * diagchannel.h
 *
 * Abstract:
 *
 * This file defines the DiagChannel protocol.  This protocol is used to aid in
 * preserving event data sent by external applications.  This protocol provides
 * a region for event data to reside in.  This data will eventually be sent to
 * the Boot Partition where it will be committed to memory and/or disk.  This
 * file contains platform-independent data that can be built using any
 * Supervisor build environment (Windows, Linux, EFI).
 *
*/

#ifndef _DIAG_CHANNEL_H_
#define _DIAG_CHANNEL_H_

#include "commontypes.h"
#include "channel.h"

/* {EEA7A573-DB82-447c-8716-EFBEAAAE4858} */
#define ULTRA_DIAG_CHANNEL_PROTOCOL_GUID \
		UUID_LE(0xeea7a573, 0xdb82, 0x447c, \
				0x87, 0x16, 0xef, 0xbe, 0xaa, 0xae, 0x48, 0x58)

static const uuid_le UltraDiagChannelProtocolGuid =
	ULTRA_DIAG_CHANNEL_PROTOCOL_GUID;

/* {E850F968-3263-4484-8CA5-2A35D087A5A8} */
#define ULTRA_DIAG_ROOT_CHANNEL_PROTOCOL_GUID \
		UUID_LE(0xe850f968, 0x3263, 0x4484, \
				0x8c, 0xa5, 0x2a, 0x35, 0xd0, 0x87, 0xa5, 0xa8)

#define ULTRA_DIAG_CHANNEL_PROTOCOL_SIGNATURE  ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment this whenever you insert or delete fields within this channel
* struct.  Also increment whenever you change the meaning of fields within this
* channel struct so as to break pre-existing software.  Note that you can
* usually add fields to the END of the channel struct withOUT needing to
* increment this. */
#define ULTRA_DIAG_CHANNEL_PROTOCOL_VERSIONID 2

#define ULTRA_DIAG_CHANNEL_OK_CLIENT(pChannel, logCtx)			\
	(ULTRA_check_channel_client(pChannel,				\
				    UltraDiagChannelProtocolGuid,	\
				    "diag",				\
				    sizeof(ULTRA_DIAG_CHANNEL_PROTOCOL), \
				    ULTRA_DIAG_CHANNEL_PROTOCOL_VERSIONID, \
				    ULTRA_DIAG_CHANNEL_PROTOCOL_SIGNATURE, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_DIAG_CHANNEL_OK_SERVER(actualBytes, logCtx)		\
	(ULTRA_check_channel_server(UltraDiagChannelProtocolGuid,	\
				    "diag",				\
				    sizeof(ULTRA_DIAG_CHANNEL_PROTOCOL), \
				    actualBytes, __FILE__, __LINE__, logCtx))
#define MAX_MODULE_NAME_SIZE 128	/* Maximum length of module name... */
#define MAX_ADDITIONAL_INFO_SIZE 256	/* Maximum length of any additional info
					 * accompanying event... */
#define MAX_SUBSYSTEMS 64	/* Maximum number of subsystems allowed in
				 * DiagChannel... */
#define LOW_SUBSYSTEMS 32	/* Half of MAX_SUBSYSTEMS to allow 64-bit
				 * math */
#define SUBSYSTEM_DEBUG 0	/* Standard subsystem for debug events */
#define SUBSYSTEM_DEFAULT 1	/* Default subsystem for legacy calls to
				 * ReportEvent */

/* few useful subsystem mask values */
#define SUBSYSTEM_MASK_DEBUG	0x01	/* Standard subsystem for debug
					 * events */
#define SUBSYSTEM_MASK_DEFAULT  0x02	/* Default subsystem for legacy calls to
					 * ReportEvents */

/* Event parameter "Severity" is overloaded with Cause in byte 2 and Severity in
 * byte 0, bytes 1 and 3 are reserved */
#define SEVERITY_MASK 0x0FF	/* mask out all but the Severity in byte 0 */
#define CAUSE_MASK 0x0FF0000	/* mask out all but the cause in byte 2 */
#define CAUSE_SHIFT_AMT 16	/* shift 2 bytes to place it in byte 2 */

/* SubsystemSeverityFilter */
#define SEVERITY_FILTER_MASK 0x0F /* mask out the Cause half, SeverityFilter is
				   * in the lower nibble */
#define CAUSE_FILTER_MASK 0xF0	/* mask out the Severity half, CauseFilter is in
				 * the upper nibble */
#define CAUSE_FILTER_SHIFT_AMT	4 /* shift amount to place it in lower or upper
				   * nibble */

/* Copied from EFI's EFI_TIME struct in efidef.h.  EFI headers are not allowed
* in some of the Supervisor areas, such as Monitor, so it has been "ported" here
* for use in diagnostic event timestamps... */
typedef struct _DIAG_EFI_TIME  {
	U16 Year;		/* 1998 - 20XX */
	U8 Month;		/* 1 - 12 */
	U8 Day;			/* 1 - 31 */
	U8 Hour;		/* 0 - 23 */
	U8 Minute;		/* 0 - 59 */
	U8 Second;		/* 0 - 59 */
	U8 Pad1;
	U32 Nanosecond;	/* 0 - 999, 999, 999 */
	S16 TimeZone;		/* -1440 to 1440 or 2047 */
	U8 Daylight;
	U8 Pad2;
} DIAG_EFI_TIME;

typedef enum  {
	 ULTRA_COMPONENT_GUEST = 0,
	 ULTRA_COMPONENT_MONITOR = 0x01,
	 ULTRA_COMPONENT_CCM = 0x02,	/* Common Control module */
	 /* RESERVED 0x03 - 0x7 */

	 /* Ultravisor Components */
	 ULTRA_COMPONENT_BOOT = 0x08,
	 ULTRA_COMPONENT_IDLE = 0x09,
	 ULTRA_COMPONENT_CONTROL = 0x0A,
	 ULTRA_COMPONENT_LOGGER = 0x0B,
	 ULTRA_COMPONENT_ACPI = 0X0C,
	 /* RESERVED 0x0D - 0x0F */

	 /* sPAR Components */
	 ULTRA_COMPONENT_COMMAND = 0x10,
	 ULTRA_COMPONENT_IODRIVER = 0x11,
	 ULTRA_COMPONENT_CONSOLE = 0x12,
	 ULTRA_COMPONENT_OPERATIONS = 0x13,
	 ULTRA_COMPONENT_MANAGEMENT = 0x14,
	 ULTRA_COMPONENT_DIAG = 0x15,
	 ULTRA_COMPONENT_HWDIAG = 0x16,
	 ULTRA_COMPONENT_PSERVICES = 0x17,
	 ULTRA_COMPONENT_PDIAG = 0x18
	 /* RESERVED 0x18 - 0x1F */
} ULTRA_COMPONENT_TYPES;

/* Structure: DIAG_CHANNEL_EVENT Purpose: Contains attributes that make up an
 * event to be written to the DIAG_CHANNEL memory.  Attributes: EventId: Id of
 * the diagnostic event to write to memory.  Severity: Severity of the event
 * (Error, Info, etc).  ModuleName: Module/file name where event originated.
 * LineNumber: Line number in module name where event originated.  Timestamp:
 * Date/time when event was received by ReportEvent, and written to DiagChannel.
 * Reserved: Padding to align structure on a 64-byte cache line boundary.
 * AdditionalInfo: Array of characters for additional event info (may be
 * empty).  */
typedef struct _DIAG_CHANNEL_EVENT  {
	U32 EventId;
	U32 Severity;
	U8 ModuleName[MAX_MODULE_NAME_SIZE];
	U32 LineNumber;
	DIAG_EFI_TIME Timestamp;	/* Size = 16 bytes */
	U32 PartitionNumber;	/* Filled in by Diag Switch as pool blocks are
				 * filled */
	U16 VirtualProcessorNumber;
	U16 LogicalProcessorNumber;
	U8 ComponentType;	/* ULTRA_COMPONENT_TYPES */
	U8 Subsystem;
	U16 Reserved0;		/* pad to U64 alignment */
	U32 BlockNumber;	/* filled in by DiagSwitch as pool blocks are
				 * filled */
	U32 BlockNumberHigh;
	U32 EventNumber;	/* filled in by DiagSwitch as pool blocks are
				 * filled */
	U32 EventNumberHigh;

	/* The BlockNumber and EventNumber fields are set only by DiagSwitch
	 * and referenced only by WinDiagDisplay formatting tool as
	 * additional diagnostic information.  Other tools including
	 * WinDiagDisplay currently ignore these 'Reserved' bytes. */
	U8 Reserved[8];
	U8 AdditionalInfo[MAX_ADDITIONAL_INFO_SIZE];

	/* NOTE: Changesto DIAG_CHANNEL_EVENT generally need to be reflected in
	 * existing copies *
	 * - for AppOS at
	 * GuestLinux/visordiag_early/supervisor_diagchannel.h *
	 * - for WinDiagDisplay at
	 * EFI/Ultra/Tools/WinDiagDisplay/WinDiagDisplay/diagstruct.h */
} DIAG_CHANNEL_EVENT;

/* Levels of severity for diagnostic events, in order from lowest severity to
* highest (i.e. fatal errors are the most severe, and should always be logged,
* but info events rarely need to be logged except during debugging).  The values
* DIAG_SEVERITY_ENUM_BEGIN and DIAG_SEVERITY_ENUM_END are not valid severity
* values.  They exist merely to dilineate the list, so that future additions
* won't require changes to the driver (i.e. when checking for out-of-range
* severities in SetSeverity).  The values DIAG_SEVERITY_OVERRIDE and
* DIAG_SEVERITY_SHUTOFF are not valid severity values for logging events but
* they are valid for controlling the amount of event data.  This enum is also
* defined in DotNet\sParFramework\ControlFramework\ControlFramework.cs.  If a
* change is made to this enum, they should also be reflected in that file.  */
typedef enum  { DIAG_SEVERITY_ENUM_BEGIN = 0,
		DIAG_SEVERITY_OVERRIDE = DIAG_SEVERITY_ENUM_BEGIN,
		DIAG_SEVERITY_VERBOSE = DIAG_SEVERITY_OVERRIDE,	/* 0 */
		DIAG_SEVERITY_INFO = DIAG_SEVERITY_VERBOSE + 1,	/* 1 */
		DIAG_SEVERITY_WARNING = DIAG_SEVERITY_INFO + 1,	/* 2 */
		DIAG_SEVERITY_ERR = DIAG_SEVERITY_WARNING + 1,	/* 3 */
		DIAG_SEVERITY_PRINT = DIAG_SEVERITY_ERR + 1,	/* 4 */
		DIAG_SEVERITY_SHUTOFF = DIAG_SEVERITY_PRINT + 1, /* 5 */
		DIAG_SEVERITY_ENUM_END = DIAG_SEVERITY_SHUTOFF,	/* 5 */
		DIAG_SEVERITY_NONFATAL_ERR = DIAG_SEVERITY_ERR,
		DIAG_SEVERITY_FATAL_ERR = DIAG_SEVERITY_PRINT
} DIAG_SEVERITY;

/* Event Cause enums
*
* Levels of cause for diagnostic events, in order from least to greatest cause
* Internal errors are most urgent since ideally they should never exist
* Invalid requests are preventable by avoiding invalid inputs
* Operations errors depend on environmental factors which may impact which
* requests are possible
* Manifest provides intermediate value to capture firmware and configuration
* version information
* Trace provides suplimental debug information in release firmware
* Unknown Log captures unclasified LogEvent calls.
* Debug is the least urgent since it provides suplimental debug information only
* in debug firmware
* Unknown Debug captures unclassified DebugEvent calls.
* This enum is also defined in
* DotNet\sParFramework\ControlFramework\ControlFramework.cs.
* If a change is made to this enum, they should also be reflected in that
* file.  */



/* A cause value "DIAG_CAUSE_FILE_XFER" together with a severity value of
* "DIAG_SEVERITY_PRINT" (=4), is used for transferring text or binary file to
* the Diag partition. This cause-severity combination will be used by Logger
* DiagSwitch to segregate events into block types. The files are transferred in
* 256 byte chunks maximum, in the AdditionalInfo field of the DIAG_CHANNEL_EVENT
* structure. In the file transfer mode, some event fields will have different
* meaning: EventId specifies the file offset, severity specifies the block type,
* ModuleName specifies the filename, LineNumber specifies the number of valid
* data bytes in an event and AdditionalInfo contains up to 256 bytes of data. */

/* The Diag DiagWriter appends event blocks to events.raw as today, and for data
 * blocks uses DIAG_CHANNEL_EVENT
 * PartitionNumber to extract and append 'AdditionalInfo' to filename (specified
 * by ModuleName). */

/* The Dell PDiag uses this new mechanism to stash DSET .zip onto the
 * 'diagnostic' virtual disk.  */
typedef enum  {
	DIAG_CAUSE_UNKNOWN = 0,
	DIAG_CAUSE_UNKNOWN_DEBUG = DIAG_CAUSE_UNKNOWN + 1,	/* 1 */
	DIAG_CAUSE_DEBUG = DIAG_CAUSE_UNKNOWN_DEBUG + 1,	/* 2 */
	DIAG_CAUSE_UNKNOWN_LOG = DIAG_CAUSE_DEBUG + 1,	/* 3 */
	DIAG_CAUSE_TRACE = DIAG_CAUSE_UNKNOWN_LOG + 1,	/* 4 */
	DIAG_CAUSE_MANIFEST = DIAG_CAUSE_TRACE + 1,	/* 5 */
	DIAG_CAUSE_OPERATIONS_ERROR = DIAG_CAUSE_MANIFEST + 1,	/* 6 */
	DIAG_CAUSE_INVALID_REQUEST = DIAG_CAUSE_OPERATIONS_ERROR + 1,	/* 7 */
	DIAG_CAUSE_INTERNAL_ERROR = DIAG_CAUSE_INVALID_REQUEST + 1, /* 8 */
	DIAG_CAUSE_FILE_XFER = DIAG_CAUSE_INTERNAL_ERROR + 1,	/* 9 */
	DIAG_CAUSE_ENUM_END = DIAG_CAUSE_FILE_XFER	/* 9 */
} DIAG_CAUSE;

/* Event Cause category defined into the byte 2 of Severity */
#define CAUSE_DEBUG (DIAG_CAUSE_DEBUG << CAUSE_SHIFT_AMT)
#define CAUSE_TRACE (DIAG_CAUSE_TRACE << CAUSE_SHIFT_AMT)
#define CAUSE_MANIFEST (DIAG_CAUSE_MANIFEST << CAUSE_SHIFT_AMT)
#define CAUSE_OPERATIONS_ERROR (DIAG_CAUSE_OPERATIONS_ERROR << CAUSE_SHIFT_AMT)
#define CAUSE_INVALID_REQUEST (DIAG_CAUSE_INVALID_REQUEST << CAUSE_SHIFT_AMT)
#define CAUSE_INTERNAL_ERROR (DIAG_CAUSE_INTERNAL_ERROR << CAUSE_SHIFT_AMT)
#define CAUSE_FILE_XFER (DIAG_CAUSE_FILE_XFER << CAUSE_SHIFT_AMT)
#define CAUSE_ENUM_END CAUSE_FILE_XFER

/* Combine Cause and Severity categories into one */
#define CAUSE_DEBUG_SEVERITY_VERBOSE \
	(CAUSE_DEBUG | DIAG_SEVERITY_VERBOSE)
#define CAUSE_TRACE_SEVERITY_VERBOSE \
	(CAUSE_TRACE | DIAG_SEVERITY_VERBOSE)
#define CAUSE_MANIFEST_SEVERITY_VERBOSE\
	(CAUSE_MANIFEST | DIAG_SEVERITY_VERBOSE)
#define CAUSE_OPERATIONS_SEVERITY_VERBOSE \
	(CAUSE_OPERATIONS_ERROR | DIAG_SEVERITY_VERBOSE)
#define CAUSE_INVALID_SEVERITY_VERBOSE \
	(CAUSE_INVALID_REQUEST  | DIAG_SEVERITY_VERBOSE)
#define CAUSE_INTERNAL_SEVERITY_VERBOSE \
	(CAUSE_INTERNAL_ERROR   | DIAG_SEVERITY_VERBOSE)

#define CAUSE_DEBUG_SEVERITY_INFO \
	(CAUSE_DEBUG | DIAG_SEVERITY_INFO)
#define CAUSE_TRACE_SEVERITY_INFO \
	(CAUSE_TRACE | DIAG_SEVERITY_INFO)
#define CAUSE_MANIFEST_SEVERITY_INFO \
	(CAUSE_MANIFEST | DIAG_SEVERITY_INFO)
#define CAUSE_OPERATIONS_SEVERITY_INFO \
	(CAUSE_OPERATIONS_ERROR | DIAG_SEVERITY_INFO)
#define CAUSE_INVALID_SEVERITY_INFO \
	(CAUSE_INVALID_REQUEST  | DIAG_SEVERITY_INFO)
#define CAUSE_INTERNAL_SEVERITY_INFO \
	(CAUSE_INTERNAL_ERROR | DIAG_SEVERITY_INFO)

#define CAUSE_DEBUG_SEVERITY_WARN \
	(CAUSE_DEBUG | DIAG_SEVERITY_WARNING)
#define CAUSE_TRACE_SEVERITY_WARN \
	(CAUSE_TRACE | DIAG_SEVERITY_WARNING)
#define CAUSE_MANIFEST_SEVERITY_WARN \
	(CAUSE_MANIFEST | DIAG_SEVERITY_WARNING)
#define CAUSE_OPERATIONS_SEVERITY_WARN \
	(CAUSE_OPERATIONS_ERROR | DIAG_SEVERITY_WARNING)
#define CAUSE_INVALID_SEVERITY_WARN \
	(CAUSE_INVALID_REQUEST | DIAG_SEVERITY_WARNING)
#define CAUSE_INTERNAL_SEVERITY_WARN \
	(CAUSE_INTERNAL_ERROR | DIAG_SEVERITY_WARNING)

#define CAUSE_DEBUG_SEVERITY_ERR \
	(CAUSE_DEBUG | DIAG_SEVERITY_ERR)
#define CAUSE_TRACE_SEVERITY_ERR \
	(CAUSE_TRACE | DIAG_SEVERITY_ERR)
#define CAUSE_MANIFEST_SEVERITY_ERR \
	(CAUSE_MANIFEST | DIAG_SEVERITY_ERR)
#define CAUSE_OPERATIONS_SEVERITY_ERR \
	(CAUSE_OPERATIONS_ERROR | DIAG_SEVERITY_ERR)
#define CAUSE_INVALID_SEVERITY_ERR \
	(CAUSE_INVALID_REQUEST  | DIAG_SEVERITY_ERR)
#define CAUSE_INTERNAL_SEVERITY_ERR \
	(CAUSE_INTERNAL_ERROR   | DIAG_SEVERITY_ERR)

#define CAUSE_DEBUG_SEVERITY_PRINT \
	(CAUSE_DEBUG | DIAG_SEVERITY_PRINT)
#define CAUSE_TRACE_SEVERITY_PRINT \
	(CAUSE_TRACE | DIAG_SEVERITY_PRINT)
#define CAUSE_MANIFEST_SEVERITY_PRINT \
	(CAUSE_MANIFEST | DIAG_SEVERITY_PRINT)
#define CAUSE_OPERATIONS_SEVERITY_PRINT \
	(CAUSE_OPERATIONS_ERROR | DIAG_SEVERITY_PRINT)
#define CAUSE_INVALID_SEVERITY_PRINT \
	(CAUSE_INVALID_REQUEST | DIAG_SEVERITY_PRINT)
#define CAUSE_INTERNAL_SEVERITY_PRINT \
	(CAUSE_INTERNAL_ERROR | DIAG_SEVERITY_PRINT)
#define CAUSE_FILE_XFER_SEVERITY_PRINT \
	(CAUSE_FILE_XFER | DIAG_SEVERITY_PRINT)

/* Structure: DIAG_CHANNEL_PROTOCOL_HEADER
 *
 * Purpose: Contains attributes that make up the header specific to the
 * DIAG_CHANNEL area.
 *
 * Attributes:
 *
 * DiagLock: Diag Channel spinlock.
 *
 *IsChannelInitialized: 1 iff SignalInit was called for this channel; otherwise
 *			0, and assume the channel is not ready for use yet.
 *
 * Reserved: Padding to align the fields in this structure.
 *
 *SubsystemSeverityFilter: Level of severity on a subsystem basis that controls
 *			whether events are logged.  Any event's severity for a
 *			particular subsystem below this level will be discarded.
 */
typedef struct _DIAG_CHANNEL_PROTOCOL_HEADER  {
	volatile U32 DiagLock;
	U8 IsChannelInitialized;
	U8 Reserved[3];
	U8 SubsystemSeverityFilter[64];
} DIAG_CHANNEL_PROTOCOL_HEADER;

/* The Diagram for the Diagnostic Channel: */
/* ----------------------- */
/* | Channel Header        |	Defined by ULTRA_CHANNEL_PROTOCOL */
/* ----------------------- */
/* | Signal Queue Header   |	Defined by SIGNAL_QUEUE_HEADER */
/* ----------------------- */
/* | DiagChannel Header    |	Defined by DIAG_CHANNEL_PROTOCOL_HEADER */
/* ----------------------- */
/* | Channel Event Info    |	Defined by (DIAG_CHANNEL_EVENT * MAX_EVENTS) */
/* ----------------------- */
/* | Reserved              |	Reserved (pad out to 4MB) */
/* ----------------------- */

/* Offsets/sizes for diagnostic channel attributes... */
#define DIAG_CH_QUEUE_HEADER_OFFSET (sizeof(ULTRA_CHANNEL_PROTOCOL))
#define DIAG_CH_QUEUE_HEADER_SIZE (sizeof(SIGNAL_QUEUE_HEADER))
#define DIAG_CH_PROTOCOL_HEADER_OFFSET \
	(DIAG_CH_QUEUE_HEADER_OFFSET + DIAG_CH_QUEUE_HEADER_SIZE)
#define DIAG_CH_PROTOCOL_HEADER_SIZE (sizeof(DIAG_CHANNEL_PROTOCOL_HEADER))
#define DIAG_CH_EVENT_OFFSET \
	(DIAG_CH_PROTOCOL_HEADER_OFFSET + DIAG_CH_PROTOCOL_HEADER_SIZE)
#define DIAG_CH_SIZE (4096 * 1024)

/* For Control and Idle Partitions with larger (8 MB) diagnostic(root)
 * channels */
#define DIAG_CH_LRG_SIZE (2 * DIAG_CH_SIZE)	/* 8 MB */

/*
 * Structure: ULTRA_DIAG_CHANNEL_PROTOCOL
 *
 * Purpose: Contains attributes that make up the DIAG_CHANNEL memory.
 *
 * Attributes:
 *
 * CommonChannelHeader:	Header info common to all channels.
 *
 * QueueHeader: Queue header common to all channels - used to determine where to
 * store event.
 *
 * DiagChannelHeader: Diagnostic channel header info (see
 * DIAG_CHANNEL_PROTOCOL_HEADER comments).
 *
 * Events: Area where diagnostic events (up to MAX_EVENTS) are written.
 *
 *Reserved: Reserved area to allow for correct channel size padding.
*/
typedef struct _ULTRA_DIAG_CHANNEL_PROTOCOL  {
	ULTRA_CHANNEL_PROTOCOL CommonChannelHeader;
	SIGNAL_QUEUE_HEADER QueueHeader;
	DIAG_CHANNEL_PROTOCOL_HEADER DiagChannelHeader;
	DIAG_CHANNEL_EVENT Events[(DIAG_CH_SIZE - DIAG_CH_EVENT_OFFSET) /
				   sizeof(DIAG_CHANNEL_EVENT)];
}
ULTRA_DIAG_CHANNEL_PROTOCOL;

#endif
