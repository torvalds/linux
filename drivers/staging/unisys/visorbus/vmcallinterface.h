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

#ifdef __GNUC__
#include "iovmcall_gnuc.h"
#endif	/*  */
#include "diagchannel.h"

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

#ifdef __GNUC__
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
#endif

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

#endif /* __IOMONINTF_H__ */
