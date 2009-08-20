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
#ifndef __HV_API_H
#define __HV_API_H

/*
 * The below CPUID leaves are present if VersionAndFeatures.HypervisorPresent
 * is set by CPUID(HvCpuIdFunctionVersionAndFeatures).
 */
enum hv_cpuid_function {
	HvCpuIdFunctionVersionAndFeatures		= 0x00000001,
	HvCpuIdFunctionHvVendorAndMaxFunction		= 0x40000000,
	HvCpuIdFunctionHvInterface			= 0x40000001,

	/*
	 * The remaining functions depend on the value of
	 * HvCpuIdFunctionInterface
	 */
	HvCpuIdFunctionMsHvVersion			= 0x40000002,
	HvCpuIdFunctionMsHvFeatures			= 0x40000003,
	HvCpuIdFunctionMsHvEnlightenmentInformation	= 0x40000004,
	HvCpuIdFunctionMsHvImplementationLimits		= 0x40000005,
};

/* Define the virtual APIC registers */
#define HV_X64_MSR_EOI			(0x40000070)
#define HV_X64_MSR_ICR			(0x40000071)
#define HV_X64_MSR_TPR			(0x40000072)
#define HV_X64_MSR_APIC_ASSIST_PAGE	(0x40000073)

/* Define version of the synthetic interrupt controller. */
#define HV_SYNIC_VERSION		(1)

/* Define synthetic interrupt controller model specific registers. */
#define HV_X64_MSR_SCONTROL		(0x40000080)
#define HV_X64_MSR_SVERSION		(0x40000081)
#define HV_X64_MSR_SIEFP		(0x40000082)
#define HV_X64_MSR_SIMP			(0x40000083)
#define HV_X64_MSR_EOM			(0x40000084)
#define HV_X64_MSR_SINT0		(0x40000090)
#define HV_X64_MSR_SINT1		(0x40000091)
#define HV_X64_MSR_SINT2		(0x40000092)
#define HV_X64_MSR_SINT3		(0x40000093)
#define HV_X64_MSR_SINT4		(0x40000094)
#define HV_X64_MSR_SINT5		(0x40000095)
#define HV_X64_MSR_SINT6		(0x40000096)
#define HV_X64_MSR_SINT7		(0x40000097)
#define HV_X64_MSR_SINT8		(0x40000098)
#define HV_X64_MSR_SINT9		(0x40000099)
#define HV_X64_MSR_SINT10		(0x4000009A)
#define HV_X64_MSR_SINT11		(0x4000009B)
#define HV_X64_MSR_SINT12		(0x4000009C)
#define HV_X64_MSR_SINT13		(0x4000009D)
#define HV_X64_MSR_SINT14		(0x4000009E)
#define HV_X64_MSR_SINT15		(0x4000009F)

/* Define the expected SynIC version. */
#define HV_SYNIC_VERSION_1		(0x1)

/* Define synthetic interrupt controller message constants. */
#define HV_MESSAGE_SIZE			(256)
#define HV_MESSAGE_PAYLOAD_BYTE_COUNT	(240)
#define HV_MESSAGE_PAYLOAD_QWORD_COUNT	(30)
#define HV_ANY_VP			(0xFFFFFFFF)

/* Define synthetic interrupt controller flag constants. */
#define HV_EVENT_FLAGS_COUNT		(256 * 8)
#define HV_EVENT_FLAGS_BYTE_COUNT	(256)
#define HV_EVENT_FLAGS_DWORD_COUNT	(256 / sizeof(u32))

/* Define hypervisor message types. */
enum hv_message_type {
	HvMessageTypeNone			= 0x00000000,

	/* Memory access messages. */
	HvMessageTypeUnmappedGpa		= 0x80000000,
	HvMessageTypeGpaIntercept		= 0x80000001,

	/* Timer notification messages. */
	HvMessageTimerExpired			= 0x80000010,

	/* Error messages. */
	HvMessageTypeInvalidVpRegisterValue	= 0x80000020,
	HvMessageTypeUnrecoverableException	= 0x80000021,
	HvMessageTypeUnsupportedFeature		= 0x80000022,

	/* Trace buffer complete messages. */
	HvMessageTypeEventLogBufferComplete	= 0x80000040,

	/* Platform-specific processor intercept messages. */
	HvMessageTypeX64IoPortIntercept		= 0x80010000,
	HvMessageTypeX64MsrIntercept		= 0x80010001,
	HvMessageTypeX64CpuidIntercept		= 0x80010002,
	HvMessageTypeX64ExceptionIntercept	= 0x80010003,
	HvMessageTypeX64ApicEoi			= 0x80010004,
	HvMessageTypeX64LegacyFpError		= 0x80010005
};

/* Define the number of synthetic interrupt sources. */
#define HV_SYNIC_SINT_COUNT		(16)
#define HV_SYNIC_STIMER_COUNT		(4)

/* Define invalid partition identifier. */
#define HV_PARTITION_ID_INVALID		((u64)0x0)

/* Define connection identifier type. */
union hv_connection_id {
	u32 Asu32;
	struct {
		u32 Id:24;
		u32 Reserved:8;
	} u;
};

/* Define port identifier type. */
union hv_port_id {
	u32 Asu32;
	struct {
		u32 Id:24;
		u32 Reserved:8;
	} u ;
};

/* Define port type. */
enum hv_port_type {
	HvPortTypeMessage	= 1,
	HvPortTypeEvent		= 2,
	HvPortTypeMonitor	= 3
};

/* Define port information structure. */
struct hv_port_info {
	enum hv_port_type PortType;
	u32 Padding;
	union {
		struct {
			u32 TargetSint;
			u32 TargetVp;
			u64 RsvdZ;
		} MessagePortInfo;
		struct {
			u32 TargetSint;
			u32 TargetVp;
			u16 BaseFlagNumber;
			u16 FlagCount;
			u32 RsvdZ;
		} EventPortInfo;
		struct {
			u64 MonitorAddress;
			u64 RsvdZ;
		} MonitorPortInfo;
	};
};

struct hv_connection_info {
	enum hv_port_type PortType;
	u32 Padding;
	union {
		struct {
			u64 RsvdZ;
		} MessageConnectionInfo;
		struct {
			u64 RsvdZ;
		} EventConnectionInfo;
		struct {
			u64 MonitorAddress;
		} MonitorConnectionInfo;
	};
};

/* Define synthetic interrupt controller message flags. */
union hv_message_flags {
	u8 Asu8;
	struct {
		u8 MessagePending:1;
		u8 Reserved:7;
	};
};

/* Define synthetic interrupt controller message header. */
struct hv_message_header {
	enum hv_message_type MessageType;
	u8 PayloadSize;
	union hv_message_flags MessageFlags;
	u8 Reserved[2];
	union {
		u64 Sender;
		union hv_port_id Port;
	};
};

/* Define timer message payload structure. */
struct hv_timer_message_payload {
	u32 TimerIndex;
	u32 Reserved;
	u64 ExpirationTime;	/* When the timer expired */
	u64 DeliveryTime;	/* When the message was delivered */
};

/* Define synthetic interrupt controller message format. */
struct hv_message {
	struct hv_message_header Header;
	union {
		u64 Payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
	} u ;
};

/* Define the number of message buffers associated with each port. */
#define HV_PORT_MESSAGE_BUFFER_COUNT	(16)

/* Define the synthetic interrupt message page layout. */
struct hv_message_page {
	struct hv_message SintMessage[HV_SYNIC_SINT_COUNT];
};

/* Define the synthetic interrupt controller event flags format. */
union hv_synic_event_flags {
	u8 Flags8[HV_EVENT_FLAGS_BYTE_COUNT];
	u32 Flags32[HV_EVENT_FLAGS_DWORD_COUNT];
};

/* Define the synthetic interrupt flags page layout. */
struct hv_synic_event_flags_page {
	union hv_synic_event_flags SintEventFlags[HV_SYNIC_SINT_COUNT];
};

/* Define SynIC control register. */
union hv_synic_scontrol {
	u64 AsUINT64;
	struct {
		u64 Enable:1;
		u64 Reserved:63;
	};
};

/* Define synthetic interrupt source. */
union hv_synic_sint {
	u64 AsUINT64;
	struct {
		u64 Vector:8;
		u64 Reserved1:8;
		u64 Masked:1;
		u64 AutoEoi:1;
		u64 Reserved2:46;
	};
};

/* Define the format of the SIMP register */
union hv_synic_simp {
	u64 AsUINT64;
	struct {
		u64 SimpEnabled:1;
		u64 Preserved:11;
		u64 BaseSimpGpa:52;
	};
};

/* Define the format of the SIEFP register */
union hv_synic_siefp {
	u64 AsUINT64;
	struct {
		u64 SiefpEnabled:1;
		u64 Preserved:11;
		u64 BaseSiefpGpa:52;
	};
};

/* Definitions for the monitored notification facility */
union hv_monitor_trigger_group {
	u64 AsUINT64;
	struct {
		u32 Pending;
		u32 Armed;
	};
};

struct hv_monitor_parameter {
	union hv_connection_id ConnectionId;
	u16 FlagNumber;
	u16 RsvdZ;
};

union hv_monitor_trigger_state {
	u32 Asu32;

	struct {
		u32 GroupEnable:4;
		u32 RsvdZ:28;
	};
};

/* struct hv_monitor_page Layout */
/* ------------------------------------------------------ */
/* | 0   | TriggerState (4 bytes) | Rsvd1 (4 bytes)     | */
/* | 8   | TriggerGroup[0]                              | */
/* | 10  | TriggerGroup[1]                              | */
/* | 18  | TriggerGroup[2]                              | */
/* | 20  | TriggerGroup[3]                              | */
/* | 28  | Rsvd2[0]                                     | */
/* | 30  | Rsvd2[1]                                     | */
/* | 38  | Rsvd2[2]                                     | */
/* | 40  | NextCheckTime[0][0]    | NextCheckTime[0][1] | */
/* | ...                                                | */
/* | 240 | Latency[0][0..3]                             | */
/* | 340 | Rsvz3[0]                                     | */
/* | 440 | Parameter[0][0]                              | */
/* | 448 | Parameter[0][1]                              | */
/* | ...                                                | */
/* | 840 | Rsvd4[0]                                     | */
/* ------------------------------------------------------ */
struct hv_monitor_page {
	union hv_monitor_trigger_state TriggerState;
	u32 RsvdZ1;

	union hv_monitor_trigger_group TriggerGroup[4];
	u64 RsvdZ2[3];

	s32 NextCheckTime[4][32];

	u16 Latency[4][32];
	u64 RsvdZ3[32];

	struct hv_monitor_parameter Parameter[4][32];

	u8 RsvdZ4[1984];
};

/* Declare the various hypercall operations. */
enum hv_call_code {
	HvCallPostMessage	= 0x005c,
	HvCallSignalEvent	= 0x005d,
};

/* Definition of the HvPostMessage hypercall input structure. */
struct hv_input_post_message {
	union hv_connection_id ConnectionId;
	u32 Reserved;
	enum hv_message_type MessageType;
	u32 PayloadSize;
	u64 Payload[HV_MESSAGE_PAYLOAD_QWORD_COUNT];
};

/* Definition of the HvSignalEvent hypercall input structure. */
struct hv_input_signal_event {
	union hv_connection_id ConnectionId;
	u16 FlagNumber;
	u16 RsvdZ;
};

/*
 * Versioning definitions used for guests reporting themselves to the
 * hypervisor, and visa versa.
 */

/* Version info reported by guest OS's */
enum hv_guest_os_vendor {
	HvGuestOsVendorMicrosoft	= 0x0001
};

enum hv_guest_os_microsoft_ids {
	HvGuestOsMicrosoftUndefined	= 0x00,
	HvGuestOsMicrosoftMSDOS		= 0x01,
	HvGuestOsMicrosoftWindows3x	= 0x02,
	HvGuestOsMicrosoftWindows9x	= 0x03,
	HvGuestOsMicrosoftWindowsNT	= 0x04,
	HvGuestOsMicrosoftWindowsCE	= 0x05
};

/*
 * Declare the MSR used to identify the guest OS.
 */
#define HV_X64_MSR_GUEST_OS_ID	0x40000000

union hv_x64_msr_guest_os_id_contents {
	u64 AsUINT64;
	struct {
		u64 BuildNumber:16;
		u64 ServiceVersion:8; /* Service Pack, etc. */
		u64 MinorVersion:8;
		u64 MajorVersion:8;
		u64 OsId:8; /* enum hv_guest_os_microsoft_ids (if Vendor=MS) */
		u64 VendorId:16; /* enum hv_guest_os_vendor */
	};
};

/*
 * Declare the MSR used to setup pages used to communicate with the hypervisor.
 */
#define HV_X64_MSR_HYPERCALL	0x40000001

union hv_x64_msr_hypercall_contents {
	u64 AsUINT64;
	struct {
		u64 Enable:1;
		u64 Reserved:11;
		u64 GuestPhysicalAddress:52;
	};
};

#endif
