/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: ib_smi.h 1389 2004-12-27 22:56:47Z roland $
 */

#if !defined( IB_SMI_H )
#define IB_SMI_H

#include <ib_mad.h>

#define IB_LID_PERMISSIVE			0xFFFF

#define IB_SMP_DATA_SIZE			64
#define IB_SMP_MAX_PATH_HOPS			64

struct ib_smp {
	u8	base_version;
	u8	mgmt_class;
	u8	class_version;
	u8	method;
	u16	status;
	u8	hop_ptr;
	u8	hop_cnt;
	u64	tid;
	u16	attr_id;
	u16	resv;
	u32	attr_mod;
	u64	mkey;
	u16	dr_slid;
	u16	dr_dlid;
	u8	reserved[28];
	u8	data[IB_SMP_DATA_SIZE];
	u8	initial_path[IB_SMP_MAX_PATH_HOPS];
	u8	return_path[IB_SMP_MAX_PATH_HOPS];
} __attribute__ ((packed));

#define IB_SMP_DIRECTION			__constant_htons(0x8000)

/* Subnet management attributes */
#define IB_SMP_ATTR_NOTICE			__constant_htons(0x0002)
#define IB_SMP_ATTR_NODE_DESC			__constant_htons(0x0010)
#define IB_SMP_ATTR_NODE_INFO			__constant_htons(0x0011)
#define IB_SMP_ATTR_SWITCH_INFO			__constant_htons(0x0012)
#define IB_SMP_ATTR_GUID_INFO			__constant_htons(0x0014)
#define IB_SMP_ATTR_PORT_INFO			__constant_htons(0x0015)
#define IB_SMP_ATTR_PKEY_TABLE			__constant_htons(0x0016)
#define IB_SMP_ATTR_SL_TO_VL_TABLE		__constant_htons(0x0017)
#define IB_SMP_ATTR_VL_ARB_TABLE		__constant_htons(0x0018)
#define IB_SMP_ATTR_LINEAR_FORWARD_TABLE	__constant_htons(0x0019)
#define IB_SMP_ATTR_RANDOM_FORWARD_TABLE	__constant_htons(0x001A)
#define IB_SMP_ATTR_MCAST_FORWARD_TABLE		__constant_htons(0x001B)
#define IB_SMP_ATTR_SM_INFO			__constant_htons(0x0020)
#define IB_SMP_ATTR_VENDOR_DIAG			__constant_htons(0x0030)
#define IB_SMP_ATTR_LED_INFO			__constant_htons(0x0031)
#define IB_SMP_ATTR_VENDOR_MASK			__constant_htons(0xFF00)

static inline u8
ib_get_smp_direction(struct ib_smp *smp)
{
	return ((smp->status & IB_SMP_DIRECTION) == IB_SMP_DIRECTION);
}

#endif /* IB_SMI_H */
