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

#ifndef __VMCALLINTERFACE_H__
#define __VMCALLINTERFACE_H__

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
	VMCALL_CONTROLVM_ADDR = 0x0501,
};

enum vmcall_result {
	VMCALL_RESULT_SUCCESS = 0,
	VMCALL_RESULT_INVALID_PARAM = 1,
	VMCALL_RESULT_DATA_UNAVAILABLE = 2,
	VMCALL_RESULT_FAILURE_UNAVAILABLE = 3,
	VMCALL_RESULT_DEVICE_ERROR = 4,
	VMCALL_RESULT_DEVICE_NOT_READY = 5
};

/* Structures for IO VMCALLs */
/* Parameters to VMCALL_CONTROLVM_ADDR interface */
struct vmcall_io_controlvm_addr_params {
	/* The Guest-relative physical address of the ControlVm channel. */
	/* This VMCall fills this in with the appropriate address. */
	u64 address;	/* contents provided by this VMCALL (OUT) */
	/* the size of the ControlVm channel in bytes This VMCall fills this */
	/* in with the appropriate address. */
	u32 channel_bytes;	/* contents provided by this VMCALL (OUT) */
	u8 unused[4];		/* Unused Bytes in the 64-Bit Aligned Struct */
} __packed;

#endif /* __VMCALLINTERFACE_H__ */
