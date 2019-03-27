/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc.  All rights reserved.
 * Copyright (c) 2006, 2010 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 Mellanox Technologies LTD. All rights reserved.
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
 */
#ifndef _UMAD_SA_H
#define _UMAD_SA_H

#include <infiniband/umad_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

/* SA specific methods */
enum {
	UMAD_SA_CLASS_VERSION		= 2,	/* IB spec version 1.1/1.2 */

	UMAD_SA_METHOD_GET_TABLE	= 0x12,
	UMAD_SA_METHOD_GET_TABLE_RESP	= 0x92,
	UMAD_SA_METHOD_DELETE		= 0x15,
	UMAD_SA_METHOD_DELETE_RESP	= 0x95,
	UMAD_SA_METHOD_GET_MULTI	= 0x14,
	UMAD_SA_METHOD_GET_MULTI_RESP	= 0x94,
	UMAD_SA_METHOD_GET_TRACE_TABLE	= 0x13
};

enum {
	UMAD_SA_STATUS_SUCCESS		= 0,
	UMAD_SA_STATUS_NO_RESOURCES	= 1,
	UMAD_SA_STATUS_REQ_INVALID	= 2,
	UMAD_SA_STATUS_NO_RECORDS	= 3,
	UMAD_SA_STATUS_TOO_MANY_RECORDS	= 4,
	UMAD_SA_STATUS_INVALID_GID	= 5,
	UMAD_SA_STATUS_INSUF_COMPS	= 6,
	UMAD_SA_STATUS_REQ_DENIED	= 7,
	UMAD_SA_STATUS_PRI_SUGGESTED	= 8
};

/* SA attributes */
enum {
	UMAD_SA_ATTR_NODE_REC		= 0x0011,
	UMAD_SA_ATTR_PORT_INFO_REC	= 0x0012,
	UMAD_SA_ATTR_SLVL_REC		= 0x0013,
	UMAD_SA_ATTR_SWITCH_INFO_REC	= 0x0014,
	UMAD_SA_ATTR_LINEAR_FT_REC	= 0x0015,
	UMAD_SA_ATTR_RANDOM_FT_REC	= 0x0016,
	UMAD_SA_ATTR_MCAST_FT_REC	= 0x0017,
	UMAD_SA_ATTR_SM_INFO_REC	= 0x0018,
	UMAD_SA_ATTR_LINK_SPD_WIDTH_TABLE_REC = 0x0019,
	UMAD_SA_ATTR_INFORM_INFO_REC	= 0x00F3,
	UMAD_SA_ATTR_LINK_REC		= 0x0020,
	UMAD_SA_ATTR_GUID_INFO_REC	= 0x0030,
	UMAD_SA_ATTR_SERVICE_REC	= 0x0031,
	UMAD_SA_ATTR_PKEY_TABLE_REC	= 0x0033,
	UMAD_SA_ATTR_PATH_REC		= 0x0035,
	UMAD_SA_ATTR_VL_ARB_REC		= 0x0036,
	UMAD_SA_ATTR_MCMEMBER_REC	= 0x0038,
	UMAD_SA_ATTR_TRACE_REC		= 0x0039,
	UMAD_SA_ATTR_MULTI_PATH_REC	= 0x003A,
	UMAD_SA_ATTR_SERVICE_ASSOC_REC	= 0x003B,
	UMAD_SA_ATTR_HIERARCHY_INFO_REC = 0x003C,
	UMAD_SA_ATTR_CABLE_INFO_REC	= 0x003D,
	UMAD_SA_ATTR_PORT_INFO_EXT_REC	= 0x003E
};

enum {
	UMAD_LEN_SA_DATA		= 200
};

/* CM bits */
enum {
	UMAD_SA_CAP_MASK_IS_SUBNET_OPT_REC_SUP              = (1 << 8),
	UMAD_SA_CAP_MASK_IS_UD_MCAST_SUP                    = (1 << 9),
	UMAD_SA_CAP_MASK_IS_MULTIPATH_SUP                   = (1 << 10),
	UMAD_SA_CAP_MASK_IS_REINIT_SUP                      = (1 << 11),
	UMAD_SA_CAP_MASK_IS_GID_SCOPED_MULTIPATH_SUP        = (1 << 12),
	UMAD_SA_CAP_MASK_IS_PORTINFO_CAP_MASK_MATCH_SUP     = (1 << 13),
	UMAD_SA_CAP_MASK_IS_LINK_SPEED_WIDTH_PAIRS_REC_SUP  = (1 << 14),
	UMAD_SA_CAP_MASK_IS_PA_SERVICES_SUP                 = (1 << 15)
};
/* CM2 bits */
enum {
	UMAD_SA_CAP_MASK2_IS_UNPATH_REPATH_SUP              = (1 << 0),
	UMAD_SA_CAP_MASK2_IS_QOS_SUP                        = (1 << 1),
	UMAD_SA_CAP_MASK2_IS_REV_PATH_PKEY_MEM_BIT_SUP      = (1 << 2),
	UMAD_SA_CAP_MASK2_IS_MCAST_TOP_SUP                  = (1 << 3),
	UMAD_SA_CAP_MASK2_IS_HIERARCHY_INFO_SUP             = (1 << 4),
	UMAD_SA_CAP_MASK2_IS_ADDITIONAL_GUID_SUP            = (1 << 5),
	UMAD_SA_CAP_MASK2_IS_FULL_PORTINFO_REC_SUP          = (1 << 6),
	UMAD_SA_CAP_MASK2_IS_EXT_SPEEDS_SUP                 = (1 << 7),
	UMAD_SA_CAP_MASK2_IS_MCAST_SERVICE_REC_SUP          = (1 << 8),
	UMAD_SA_CAP_MASK2_IS_CABLE_INFO_REC_SUP             = (1 << 9),
	UMAD_SA_CAP_MASK2_IS_PORT_INFO_CAPMASK2_MATCH_SUP   = (1 << 10),
	UMAD_SA_CAP_MASK2_IS_PORT_INFO_EXT_REC_SUP          = (1 << 11)
};

/*
 *  sm_key is not aligned on an 8-byte boundary, so is defined as a byte array
 */
struct umad_sa_packet {
	struct umad_hdr		mad_hdr;
	struct umad_rmpp_hdr	rmpp_hdr;
	uint8_t			sm_key[8]; /* network-byte order */
	__be16			attr_offset;
	__be16			reserved;
	__be64			comp_mask;
	uint8_t 		data[UMAD_LEN_SA_DATA]; /* network-byte order */
};

END_C_DECLS
#endif				/* _UMAD_SA_H */
