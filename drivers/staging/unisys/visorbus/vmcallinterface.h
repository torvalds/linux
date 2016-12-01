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
	/* visorbus driver files */
	VISOR_BUS_PC = 0xF0,
	VISOR_BUS_PC_visorbus_main_c = 0xFF,
	VISOR_BUS_PC_visorchipset_c = 0xFE,
};

enum event_pc {			/* POSTCODE event identifier tuples */
	BUS_CREATE_ENTRY_PC = 0x001,
	BUS_CREATE_FAILURE_PC = 0x002,
	BUS_CREATE_EXIT_PC = 0x003,
	BUS_CONFIGURE_ENTRY_PC = 0x004,
	BUS_CONFIGURE_FAILURE_PC = 0x005,
	BUS_CONFIGURE_EXIT_PC = 0x006,
	CHIPSET_INIT_ENTRY_PC = 0x007,
	CHIPSET_INIT_SUCCESS_PC = 0x008,
	CHIPSET_INIT_FAILURE_PC = 0x009,
	CHIPSET_INIT_EXIT_PC = 0x00A,
	CONTROLVM_INIT_FAILURE_PC = 0x00B,
	DEVICE_CREATE_ENTRY_PC = 0x00C,
	DEVICE_CREATE_FAILURE_PC = 0x00D,
	DEVICE_CREATE_SUCCESS_PC = 0x00E,
	DEVICE_CREATE_EXIT_PC = 0x00F,
	DEVICE_ADD_PC = 0x010,
	DEVICE_REGISTER_FAILURE_PC = 0x011,
	DEVICE_CHANGESTATE_FAILURE_PC = 0x012,
	DRIVER_ENTRY_PC = 0x013,
	DRIVER_EXIT_PC = 0x014,
	MALLOC_FAILURE_PC = 0x015,
	CRASH_DEV_ENTRY_PC = 0x016,
	CRASH_DEV_EXIT_PC = 0x017,
	CRASH_DEV_RD_BUS_FAILURE_PC = 0x018,
	CRASH_DEV_RD_DEV_FAILURE_PC = 0x019,
	CRASH_DEV_BUS_NULL_FAILURE_PC = 0x01A,
	CRASH_DEV_DEV_NULL_FAILURE_PC = 0x01B,
	CRASH_DEV_CTRL_RD_FAILURE_PC = 0x01C,
	CRASH_DEV_COUNT_FAILURE_PC = 0x01D,
	SAVE_MSG_BUS_FAILURE_PC = 0x01E,
	SAVE_MSG_DEV_FAILURE_PC = 0x01F,
};

/* Write a 64-bit value to the hypervisor's log file
 * POSTCODE_LINUX generates a value in the form 0xAABBBCCCDDDDEEEE where
 *	A is an identifier for the file logging the postcode
 *	B is an identifier for the event logging the postcode
 *	C is the line logging the postcode
 *	D is additional information the caller wants to log
 *	E is additional information the caller wants to log
 * Please also note that the resulting postcode is in hex, so if you are
 * searching for the __LINE__ number, convert it first to decimal.  The line
 * number combined with driver and type of call, will allow you to track down
 * exactly what line an error occurred on, or where the last driver
 * entered/exited from.
 */

#define POSTCODE_LINUX(EVENT_PC, pc16bit1, pc16bit2, severity)		\
do {									\
	unsigned long long post_code_temp;				\
	post_code_temp = (((u64)CURRENT_FILE_PC) << 56) |		\
		(((u64)EVENT_PC) << 44) |				\
		((((u64)__LINE__) & 0xFFF) << 32) |			\
		((((u64)pc16bit1) & 0xFFFF) << 16) |			\
		(((u64)pc16bit2) & 0xFFFF);				\
	unisys_extended_vmcall(VMCALL_POST_CODE_LOGEVENT, severity,     \
			       MDS_APPOS, post_code_temp);              \
} while (0)

#endif /* __IOMONINTF_H__ */
