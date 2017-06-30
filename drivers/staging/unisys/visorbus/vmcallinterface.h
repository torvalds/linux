/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
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

/*
 * enum vmcall_monitor_interface_method_tuple - VMCALL identification tuples.
 * @VMCALL_CONTROLVM_ADDR: Used by all guests, not just IO.
 *
 * Note: When a new VMCALL is added:
 * - The 1st 2 hex digits correspond to one of the VMCALL_MONITOR_INTERFACE
 *   types.
 * - The next 2 hex digits are the nth relative instance of within a type.
 * E.G. for VMCALL_VIRTPART_RECYCLE_PART,
 * - The 0x02 identifies it as a VMCALL_VIRTPART type.
 * - The 0x01 identifies it as the 1st instance of a VMCALL_VIRTPART type of
 *   VMCALL.
 */
enum vmcall_monitor_interface_method_tuple {
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

/*
 * struct vmcall_io_controlvm_addr_params - Structure for IO VMCALLS. Has
 *					    parameters to VMCALL_CONTROLVM_ADDR
 *					    interface.
 * @address:	   The Guest-relative physical address of the ControlVm channel.
 *		   This VMCall fills this in with the appropriate address.
 *		   Contents provided by this VMCALL (OUT).
 * @channel_bytes: The size of the ControlVm channel in bytes This VMCall fills
 *		   this in with the appropriate address. Contents provided by
 *		   this VMCALL (OUT).
 * @unused:	   Unused Bytes in the 64-Bit Aligned Struct.
 */
struct vmcall_io_controlvm_addr_params {
	u64 address;
	u32 channel_bytes;
	u8 unused[4];
} __packed;

/* __VMCALLINTERFACE_H__ */
#endif
