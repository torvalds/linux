/*
 * Copyright (c) 2004-2014 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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
 */

#ifndef _UMAD_SM_H
#define _UMAD_SM_H

#include <infiniband/umad_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

enum {
	UMAD_SMP_DIRECTION		= 0x8000,
};

/* Subnet management attributes */
enum {
	UMAD_SM_ATTR_NODE_DESC			= 0x0010,
	UMAD_SM_ATTR_NODE_INFO			= 0x0011,
	UMAD_SM_ATTR_SWITCH_INFO		= 0x0012,
	UMAD_SM_ATTR_GUID_INFO			= 0x0014,
	UMAD_SM_ATTR_PORT_INFO			= 0x0015,
	UMAD_SM_ATTR_PKEY_TABLE			= 0x0016,
	UMAD_SM_ATTR_SLVL_TABLE			= 0x0017,
	UMAD_SM_ATTR_VL_ARB_TABLE		= 0x0018,
	UMAD_SM_ATTR_LINEAR_FT			= 0x0019,
	UMAD_SM_ATTR_RANDOM_FT			= 0x001A,
	UMAD_SM_ATTR_MCAST_FT			= 0x001B,
	UMAD_SM_ATTR_LINK_SPD_WIDTH_TABLE	= 0x001C,
	UMAD_SM_ATTR_VENDOR_MADS_TABLE		= 0x001D,
	UMAD_SM_ATTR_HIERARCHY_INFO		= 0x001E,
	UMAD_SM_ATTR_SM_INFO			= 0x0020,
	UMAD_SM_ATTR_VENDOR_DIAG		= 0x0030,
	UMAD_SM_ATTR_LED_INFO			= 0x0031,
	UMAD_SM_ATTR_CABLE_INFO			= 0x0032,
	UMAD_SM_ATTR_PORT_INFO_EXT		= 0x0033,
	UMAD_SM_ATTR_VENDOR_MASK		= 0xFF00,
	UMAD_SM_ATTR_MLNX_EXT_PORT_INFO		= 0xFF90
};

enum {
	UMAD_SM_GID_IN_SERVICE_TRAP		= 64,
	UMAD_SM_GID_OUT_OF_SERVICE_TRAP		= 65,
	UMAD_SM_MGID_CREATED_TRAP		= 66,
	UMAD_SM_MGID_DESTROYED_TRAP		= 67,
	UMAD_SM_UNPATH_TRAP			= 68,
	UMAD_SM_REPATH_TRAP			= 69,
	UMAD_SM_LINK_STATE_CHANGED_TRAP		= 128,
	UMAD_SM_LINK_INTEGRITY_THRESHOLD_TRAP	= 129,
	UMAD_SM_BUFFER_OVERRUN_THRESHOLD_TRAP	= 130,
	UMAD_SM_WATCHDOG_TIMER_EXPIRED_TRAP	= 131,
	UMAD_SM_LOCAL_CHANGES_TRAP		= 144,
	UMAD_SM_SYS_IMG_GUID_CHANGED_TRAP	= 145,
	UMAD_SM_BAD_MKEY_TRAP			= 256,
	UMAD_SM_BAD_PKEY_TRAP			= 257,
	UMAD_SM_BAD_QKEY_TRAP			= 258,
	UMAD_SM_BAD_SWITCH_PKEY_TRAP		= 259
};

enum {
	UMAD_LEN_SMP_DATA		= 64,
	UMAD_SMP_MAX_HOPS		= 64
};

struct umad_smp {
	uint8_t	 base_version;
	uint8_t	 mgmt_class;
	uint8_t	 class_version;
	uint8_t	 method;
	__be16   status;
	uint8_t  hop_ptr;
	uint8_t  hop_cnt;
	__be64   tid;
	__be16   attr_id;
	__be16   resv;
	__be32   attr_mod;
	__be64   mkey;
	__be16   dr_slid;
	__be16   dr_dlid;
	uint8_t  reserved[28];
	uint8_t  data[UMAD_LEN_SMP_DATA];
	uint8_t  initial_path[UMAD_SMP_MAX_HOPS];
	uint8_t  return_path[UMAD_SMP_MAX_HOPS];
};

END_C_DECLS
#endif				/* _UMAD_SM_H */
