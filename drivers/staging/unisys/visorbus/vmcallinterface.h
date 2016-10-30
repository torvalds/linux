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

#ifndef __IOMONINTF_H__
#define __IOMONINTF_H__

/*
* This file contains all structures needed to support the VMCALLs for IO
* Virtualization.  The VMCALLs are provided by Monitor and used by IO code
* running on IO Partitions.
*/
static inline unsigned long
__unisys_vmcall_gnuc(unsigned long tuple, unsigned long reg_ebx,
		     unsigned long reg_ecx)
{
	unsigned long result = 0;
	unsigned int cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;

	cpuid(0x00000001, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	if (!(cpuid_ecx & 0x80000000))
		return -EPERM;

	__asm__ __volatile__(".byte 0x00f, 0x001, 0x0c1" : "=a"(result) :
		"a"(tuple), "b"(reg_ebx), "c"(reg_ecx));
	return result;
}

static inline unsigned long
__unisys_extended_vmcall_gnuc(unsigned long long tuple,
			      unsigned long long reg_ebx,
			      unsigned long long reg_ecx,
			      unsigned long long reg_edx)
{
	unsigned long result = 0;
	unsigned int cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;

	cpuid(0x00000001, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	if (!(cpuid_ecx & 0x80000000))
		return -EPERM;

	__asm__ __volatile__(".byte 0x00f, 0x001, 0x0c1" : "=a"(result) :
		"a"(tuple), "b"(reg_ebx), "c"(reg_ecx), "d"(reg_edx));
	return result;
}

#ifdef VMCALL_IO_CONTROLVM_ADDR
#undef VMCALL_IO_CONTROLVM_ADDR
#endif	/*  */

/* define subsystem number for AppOS, used in uislib driver  */
#define MDS_APPOS 0x4000000000000000L	/* subsystem = 62 - AppOS */
enum vmcall_monitor_interface_method_tuple { /* VMCALL identification tuples  */
	    /* Note: when a new VMCALL is added:
	     * - the 1st 2 hex digits correspond to one of the
	     *   VMCALL_MONITOR_INTERFACE types and
	     * - the next 2 hex digits are the nth relative instance of within a
	     *   type
	     * E.G. for VMCALL_VIRTPART_RECYCLE_PART,
	     * - the 0x02 identifies it as a VMCALL_VIRTPART type and
	     * - the 0x01 identifies it as the 1st instance of a VMCALL_VIRTPART
	     *   type of VMCALL
	     */
	/* used by all Guests, not just IO */
	VMCALL_IO_CONTROLVM_ADDR = 0x0501,
	/* Allow caller to query virtual time offset */
	VMCALL_QUERY_GUEST_VIRTUAL_TIME_OFFSET = 0x0708,
	/* LOGEVENT Post Code (RDX) with specified subsystem mask */
	/* (RCX - monitor_subsystems.h) and severity (RDX) */
	VMCALL_POST_CODE_LOGEVENT = 0x070B,
	/* Allow ULTRA_SERVICE_CAPABILITY_TIME capable guest to make VMCALL */
	VMCALL_UPDATE_PHYSICAL_TIME = 0x0a02
};

#define VMCALL_SUCCESS 0
#define VMCALL_SUCCESSFUL(result)	(result == 0)

#define unisys_vmcall(tuple, reg_ebx, reg_ecx) \
	__unisys_vmcall_gnuc(tuple, reg_ebx, reg_ecx)
#define unisys_extended_vmcall(tuple, reg_ebx, reg_ecx, reg_edx) \
	__unisys_extended_vmcall_gnuc(tuple, reg_ebx, reg_ecx, reg_edx)
#define ISSUE_IO_VMCALL(method, param, result) \
	(result = unisys_vmcall(method, (param) & 0xFFFFFFFF,	\
				(param) >> 32))
#define ISSUE_IO_EXTENDED_VMCALL(method, param1, param2, param3) \
	unisys_extended_vmcall(method, param1, param2, param3)

    /* The following uses VMCALL_POST_CODE_LOGEVENT interface but is currently
     * not used much
     */
#define ISSUE_IO_VMCALL_POSTCODE_SEVERITY(postcode, severity)		\
	ISSUE_IO_EXTENDED_VMCALL(VMCALL_POST_CODE_LOGEVENT, severity,	\
				 MDS_APPOS, postcode)

/* Structures for IO VMCALLs */

/* Parameters to VMCALL_IO_CONTROLVM_ADDR interface */
struct vmcall_io_controlvm_addr_params {
	/* The Guest-relative physical address of the ControlVm channel. */
	/* This VMCall fills this in with the appropriate address. */
	u64 address;	/* contents provided by this VMCALL (OUT) */
	/* the size of the ControlVm channel in bytes This VMCall fills this */
	/* in with the appropriate address. */
	u32 channel_bytes;	/* contents provided by this VMCALL (OUT) */
	u8 unused[4];		/* Unused Bytes in the 64-Bit Aligned Struct */
} __packed;

/******* INFO ON ISSUE_POSTCODE_LINUX() BELOW *******/
enum driver_pc {		/* POSTCODE driver identifier tuples */
	/* visorchipset driver files */
	VISOR_CHIPSET_PC = 0xA0,
	VISOR_CHIPSET_PC_controlvm_c = 0xA1,
	VISOR_CHIPSET_PC_controlvm_cm2 = 0xA2,
	VISOR_CHIPSET_PC_controlvm_direct_c = 0xA3,
	VISOR_CHIPSET_PC_file_c = 0xA4,
	VISOR_CHIPSET_PC_parser_c = 0xA5,
	VISOR_CHIPSET_PC_testing_c = 0xA6,
	VISOR_CHIPSET_PC_visorchipset_main_c = 0xA7,
	VISOR_CHIPSET_PC_visorswitchbus_c = 0xA8,
	/* visorbus driver files */
	VISOR_BUS_PC = 0xB0,
	VISOR_BUS_PC_businst_attr_c = 0xB1,
	VISOR_BUS_PC_channel_attr_c = 0xB2,
	VISOR_BUS_PC_devmajorminor_attr_c = 0xB3,
	VISOR_BUS_PC_visorbus_main_c = 0xB4,
	/* visorclientbus driver files */
	VISOR_CLIENT_BUS_PC = 0xC0,
	VISOR_CLIENT_BUS_PC_visorclientbus_main_c = 0xC1,
	/* virt hba driver files */
	VIRT_HBA_PC = 0xC2,
	VIRT_HBA_PC_virthba_c = 0xC3,
	/* virtpci driver files */
	VIRT_PCI_PC = 0xC4,
	VIRT_PCI_PC_virtpci_c = 0xC5,
	/* virtnic driver files */
	VIRT_NIC_PC = 0xC6,
	VIRT_NIC_P_virtnic_c = 0xC7,
	/* uislib driver files */
	UISLIB_PC = 0xD0,
	UISLIB_PC_uislib_c = 0xD1,
	UISLIB_PC_uisqueue_c = 0xD2,
	/* 0xD3 RESERVED */
	UISLIB_PC_uisutils_c = 0xD4,
};

enum event_pc {			/* POSTCODE event identifier tuples */
	ATTACH_PORT_ENTRY_PC = 0x001,
	ATTACH_PORT_FAILURE_PC = 0x002,
	ATTACH_PORT_SUCCESS_PC = 0x003,
	BUS_FAILURE_PC = 0x004,
	BUS_CREATE_ENTRY_PC = 0x005,
	BUS_CREATE_FAILURE_PC = 0x006,
	BUS_CREATE_EXIT_PC = 0x007,
	BUS_CONFIGURE_ENTRY_PC = 0x008,
	BUS_CONFIGURE_FAILURE_PC = 0x009,
	BUS_CONFIGURE_EXIT_PC = 0x00A,
	CHIPSET_INIT_ENTRY_PC = 0x00B,
	CHIPSET_INIT_SUCCESS_PC = 0x00C,
	CHIPSET_INIT_FAILURE_PC = 0x00D,
	CHIPSET_INIT_EXIT_PC = 0x00E,
	CREATE_WORKQUEUE_PC = 0x00F,
	CREATE_WORKQUEUE_FAILED_PC = 0x0A0,
	CONTROLVM_INIT_FAILURE_PC = 0x0A1,
	DEVICE_CREATE_ENTRY_PC = 0x0A2,
	DEVICE_CREATE_FAILURE_PC = 0x0A3,
	DEVICE_CREATE_SUCCESS_PC = 0x0A4,
	DEVICE_CREATE_EXIT_PC = 0x0A5,
	DEVICE_ADD_PC = 0x0A6,
	DEVICE_REGISTER_FAILURE_PC = 0x0A7,
	DEVICE_CHANGESTATE_ENTRY_PC = 0x0A8,
	DEVICE_CHANGESTATE_FAILURE_PC = 0x0A9,
	DEVICE_CHANGESTATE_EXIT_PC = 0x0AA,
	DRIVER_ENTRY_PC = 0x0AB,
	DRIVER_EXIT_PC = 0x0AC,
	MALLOC_FAILURE_PC = 0x0AD,
	QUEUE_DELAYED_WORK_PC = 0x0AE,
	/* 0x0B7 RESERVED */
	VBUS_CHANNEL_ENTRY_PC = 0x0B8,
	VBUS_CHANNEL_FAILURE_PC = 0x0B9,
	VBUS_CHANNEL_EXIT_PC = 0x0BA,
	VHBA_CREATE_ENTRY_PC = 0x0BB,
	VHBA_CREATE_FAILURE_PC = 0x0BC,
	VHBA_CREATE_EXIT_PC = 0x0BD,
	VHBA_CREATE_SUCCESS_PC = 0x0BE,
	VHBA_COMMAND_HANDLER_PC = 0x0BF,
	VHBA_PROBE_ENTRY_PC = 0x0C0,
	VHBA_PROBE_FAILURE_PC = 0x0C1,
	VHBA_PROBE_EXIT_PC = 0x0C2,
	VNIC_CREATE_ENTRY_PC = 0x0C3,
	VNIC_CREATE_FAILURE_PC = 0x0C4,
	VNIC_CREATE_SUCCESS_PC = 0x0C5,
	VNIC_PROBE_ENTRY_PC = 0x0C6,
	VNIC_PROBE_FAILURE_PC = 0x0C7,
	VNIC_PROBE_EXIT_PC = 0x0C8,
	VPCI_CREATE_ENTRY_PC = 0x0C9,
	VPCI_CREATE_FAILURE_PC = 0x0CA,
	VPCI_CREATE_EXIT_PC = 0x0CB,
	VPCI_PROBE_ENTRY_PC = 0x0CC,
	VPCI_PROBE_FAILURE_PC = 0x0CD,
	VPCI_PROBE_EXIT_PC = 0x0CE,
	CRASH_DEV_ENTRY_PC = 0x0CF,
	CRASH_DEV_EXIT_PC = 0x0D0,
	CRASH_DEV_HADDR_NULL = 0x0D1,
	CRASH_DEV_CONTROLVM_NULL = 0x0D2,
	CRASH_DEV_RD_BUS_FAIULRE_PC = 0x0D3,
	CRASH_DEV_RD_DEV_FAIULRE_PC = 0x0D4,
	CRASH_DEV_BUS_NULL_FAILURE_PC = 0x0D5,
	CRASH_DEV_DEV_NULL_FAILURE_PC = 0x0D6,
	CRASH_DEV_CTRL_RD_FAILURE_PC = 0x0D7,
	CRASH_DEV_COUNT_FAILURE_PC = 0x0D8,
	SAVE_MSG_BUS_FAILURE_PC = 0x0D9,
	SAVE_MSG_DEV_FAILURE_PC = 0x0DA,
	CALLHOME_INIT_FAILURE_PC = 0x0DB
};

#define POSTCODE_SEVERITY_ERR DIAG_SEVERITY_ERR
#define POSTCODE_SEVERITY_WARNING DIAG_SEVERITY_WARNING
/* TODO-> Info currently doesn't show, so we set info=warning */
#define POSTCODE_SEVERITY_INFO DIAG_SEVERITY_PRINT

/* example call of POSTCODE_LINUX_2(VISOR_CHIPSET_PC, POSTCODE_SEVERITY_ERR);
 * Please also note that the resulting postcode is in hex, so if you are
 * searching for the __LINE__ number, convert it first to decimal.  The line
 * number combined with driver and type of call, will allow you to track down
 * exactly what line an error occurred on, or where the last driver
 * entered/exited from.
 */

/* BASE FUNCTIONS */
#define POSTCODE_LINUX_A(DRIVER_PC, EVENT_PC, pc32bit, severity)	\
do {									\
	unsigned long long post_code_temp;				\
	post_code_temp = (((u64)DRIVER_PC) << 56) | (((u64)EVENT_PC) << 44) | \
		((((u64)__LINE__) & 0xFFF) << 32) |			\
		(((u64)pc32bit) & 0xFFFFFFFF);				\
	ISSUE_IO_VMCALL_POSTCODE_SEVERITY(post_code_temp, severity);	\
} while (0)

#define POSTCODE_LINUX_B(DRIVER_PC, EVENT_PC, pc16bit1, pc16bit2, severity) \
do {									\
	unsigned long long post_code_temp;				\
	post_code_temp = (((u64)DRIVER_PC) << 56) | (((u64)EVENT_PC) << 44) | \
		((((u64)__LINE__) & 0xFFF) << 32) |			\
		((((u64)pc16bit1) & 0xFFFF) << 16) |			\
		(((u64)pc16bit2) & 0xFFFF);				\
	ISSUE_IO_VMCALL_POSTCODE_SEVERITY(post_code_temp, severity);	\
} while (0)

/* MOST COMMON */
#define POSTCODE_LINUX_2(EVENT_PC, severity)				\
	POSTCODE_LINUX_A(CURRENT_FILE_PC, EVENT_PC, 0x0000, severity)

#define POSTCODE_LINUX_3(EVENT_PC, pc32bit, severity)			\
	POSTCODE_LINUX_A(CURRENT_FILE_PC, EVENT_PC, pc32bit, severity)

#define POSTCODE_LINUX_4(EVENT_PC, pc16bit1, pc16bit2, severity)	\
	POSTCODE_LINUX_B(CURRENT_FILE_PC, EVENT_PC, pc16bit1,		\
			 pc16bit2, severity)

#endif /* __IOMONINTF_H__ */
