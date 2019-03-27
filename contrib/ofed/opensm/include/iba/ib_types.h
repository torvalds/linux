/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *
 */

#if !defined(__IB_TYPES_H__)
#define __IB_TYPES_H__

#include <string.h>
#include <complib/cl_types.h>
#include <complib/cl_byteswap.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#if defined( __WIN__ )
#if defined( EXPORT_AL_SYMBOLS )
#define OSM_EXPORT	__declspec(dllexport)
#else
#define OSM_EXPORT	__declspec(dllimport)
#endif
#define OSM_API __stdcall
#define OSM_CDECL __cdecl
#else
#define OSM_EXPORT	extern
#define OSM_API
#define OSM_CDECL
#define __ptr64
#endif
/****h* IBA Base/Constants
* NAME
*	Constants
*
* DESCRIPTION
*	The following constants are used throughout the IBA code base.
*
*	Definitions are from the InfiniBand Architecture Specification v1.3.1
*
*********/
/****d* IBA Base: Constants/MAD_BLOCK_SIZE
* NAME
*	MAD_BLOCK_SIZE
*
* DESCRIPTION
*	Size of a non-RMPP MAD datagram.
*
* SOURCE
*/
#define MAD_BLOCK_SIZE						256
/**********/
/****d* IBA Base: Constants/MAD_RMPP_HDR_SIZE
* NAME
*	MAD_RMPP_HDR_SIZE
*
* DESCRIPTION
*	Size of an RMPP header, including the common MAD header.
*
* SOURCE
*/
#define MAD_RMPP_HDR_SIZE					36
/**********/
/****d* IBA Base: Constants/MAD_RMPP_DATA_SIZE
* NAME
*	MAD_RMPP_DATA_SIZE
*
* DESCRIPTION
*	Size of an RMPP transaction data section.
*
* SOURCE
*/
#define MAD_RMPP_DATA_SIZE		(MAD_BLOCK_SIZE - MAD_RMPP_HDR_SIZE)
/**********/
/****d* IBA Base: Constants/MAD_BLOCK_GRH_SIZE
* NAME
*	MAD_BLOCK_GRH_SIZE
*
* DESCRIPTION
*	Size of a MAD datagram, including the GRH.
*
* SOURCE
*/
#define MAD_BLOCK_GRH_SIZE					296
/**********/
/****d* IBA Base: Constants/IB_LID_PERMISSIVE
* NAME
*	IB_LID_PERMISSIVE
*
* DESCRIPTION
*	Permissive LID
*
* SOURCE
*/
#define IB_LID_PERMISSIVE					0xFFFF
/**********/
/****d* IBA Base: Constants/IB_DEFAULT_PKEY
* NAME
*	IB_DEFAULT_PKEY
*
* DESCRIPTION
*	P_Key value for the default partition.
*
* SOURCE
*/
#define IB_DEFAULT_PKEY						0xFFFF
/**********/
/****d* IBA Base: Constants/IB_QP1_WELL_KNOWN_Q_KEY
* NAME
*	IB_QP1_WELL_KNOWN_Q_KEY
*
* DESCRIPTION
*	Well-known Q_Key for QP1 privileged mode access (15.4.2).
*
* SOURCE
*/
#define IB_QP1_WELL_KNOWN_Q_KEY				CL_HTON32(0x80010000)
/*********/
#define IB_QP0						0
#define IB_QP1						CL_HTON32(1)
#define IB_QP_PRIVILEGED_Q_KEY				CL_HTON32(0x80000000)
/****d* IBA Base: Constants/IB_LID_UCAST_START
* NAME
*	IB_LID_UCAST_START
*
* DESCRIPTION
*	Lowest valid unicast LID value.
*
* SOURCE
*/
#define IB_LID_UCAST_START_HO				0x0001
#define IB_LID_UCAST_START				(CL_HTON16(IB_LID_UCAST_START_HO))
/**********/
/****d* IBA Base: Constants/IB_LID_UCAST_END
* NAME
*	IB_LID_UCAST_END
*
* DESCRIPTION
*	Highest valid unicast LID value.
*
* SOURCE
*/
#define IB_LID_UCAST_END_HO				0xBFFF
#define IB_LID_UCAST_END				(CL_HTON16(IB_LID_UCAST_END_HO))
/**********/
/****d* IBA Base: Constants/IB_LID_MCAST_START
* NAME
*	IB_LID_MCAST_START
*
* DESCRIPTION
*	Lowest valid multicast LID value.
*
* SOURCE
*/
#define IB_LID_MCAST_START_HO				0xC000
#define IB_LID_MCAST_START				(CL_HTON16(IB_LID_MCAST_START_HO))
/**********/
/****d* IBA Base: Constants/IB_LID_MCAST_END
* NAME
*	IB_LID_MCAST_END
*
* DESCRIPTION
*	Highest valid multicast LID value.
*
* SOURCE
*/
#define IB_LID_MCAST_END_HO				0xFFFE
#define IB_LID_MCAST_END				(CL_HTON16(IB_LID_MCAST_END_HO))
/**********/
/****d* IBA Base: Constants/IB_DEFAULT_SUBNET_PREFIX
* NAME
*	IB_DEFAULT_SUBNET_PREFIX
*
* DESCRIPTION
*	Default subnet GID prefix.
*
* SOURCE
*/
#define IB_DEFAULT_SUBNET_PREFIX			(CL_HTON64(0xFE80000000000000ULL))
#define IB_DEFAULT_SUBNET_PREFIX_HO			(0xFE80000000000000ULL)
/**********/
/****d* IBA Base: Constants/IB_NODE_NUM_PORTS_MAX
* NAME
*	IB_NODE_NUM_PORTS_MAX
*
* DESCRIPTION
*	Maximum number of ports in a single node (14.2.5.7).
* SOURCE
*/
#define IB_NODE_NUM_PORTS_MAX				0xFE
/**********/
/****d* IBA Base: Constants/IB_INVALID_PORT_NUM
* NAME
*	IB_INVALID_PORT_NUM
*
* DESCRIPTION
*	Value used to indicate an invalid port number (14.2.5.10).
*
* SOURCE
*/
#define IB_INVALID_PORT_NUM				0xFF
/*********/
/****d* IBA Base: Constants/IB_SUBNET_PATH_HOPS_MAX
* NAME
*	IB_SUBNET_PATH_HOPS_MAX
*
* DESCRIPTION
*	Maximum number of directed route switch hops in a subnet (14.2.1.2).
*
* SOURCE
*/
#define IB_SUBNET_PATH_HOPS_MAX				64
/*********/
/****d* IBA Base: Constants/IB_HOPLIMIT_MAX
* NAME
*	IB_HOPLIMIT_MAX
*
* DESCRIPTION
*       Maximum number of router hops allowed.
*
* SOURCE
*/
#define IB_HOPLIMIT_MAX					255
/*********/
/****d* IBA Base: Constants/IB_MC_SCOPE_*
* NAME
*	IB_MC_SCOPE_*
*
* DESCRIPTION
*	Scope component definitions from IBA 1.2.1 (Table 3 p. 148)
*/
#define IB_MC_SCOPE_LINK_LOCAL		0x2
#define IB_MC_SCOPE_SITE_LOCAL		0x5
#define IB_MC_SCOPE_ORG_LOCAL		0x8
#define IB_MC_SCOPE_GLOBAL		0xE
/*********/
/****d* IBA Base: Constants/IB_PKEY_MAX_BLOCKS
* NAME
*	IB_PKEY_MAX_BLOCKS
*
* DESCRIPTION
*	Maximum number of PKEY blocks (14.2.5.7).
*
* SOURCE
*/
#define IB_PKEY_MAX_BLOCKS				2048
/*********/
/****d* IBA Base: Constants/IB_MCAST_MAX_BLOCK_ID
* NAME
*	IB_MCAST_MAX_BLOCK_ID
*
* DESCRIPTION
*	Maximum number of Multicast port mask blocks
*
* SOURCE
*/
#define IB_MCAST_MAX_BLOCK_ID				511
/*********/
/****d* IBA Base: Constants/IB_MCAST_BLOCK_ID_MASK_HO
* NAME
*	IB_MCAST_BLOCK_ID_MASK_HO
*
* DESCRIPTION
*	Mask (host order) to recover the Multicast block ID.
*
* SOURCE
*/
#define IB_MCAST_BLOCK_ID_MASK_HO			0x000001FF
/*********/
/****d* IBA Base: Constants/IB_MCAST_BLOCK_SIZE
* NAME
*	IB_MCAST_BLOCK_SIZE
*
* DESCRIPTION
*	Number of port mask entries in a multicast forwarding table block.
*
* SOURCE
*/
#define IB_MCAST_BLOCK_SIZE				32
/*********/
/****d* IBA Base: Constants/IB_MCAST_MASK_SIZE
* NAME
*	IB_MCAST_MASK_SIZE
*
* DESCRIPTION
*	Number of port mask bits in each entry in the multicast forwarding table.
*
* SOURCE
*/
#define IB_MCAST_MASK_SIZE				16
/*********/
/****d* IBA Base: Constants/IB_MCAST_POSITION_MASK_HO
* NAME
*	IB_MCAST_POSITION_MASK_HO
*
* DESCRIPTION
*	Mask (host order) to recover the multicast block position.
*
* SOURCE
*/
#define IB_MCAST_POSITION_MASK_HO			0xF0000000
/*********/
/****d* IBA Base: Constants/IB_MCAST_POSITION_MAX
* NAME
*	IB_MCAST_POSITION_MAX
*
* DESCRIPTION
*	Maximum value for the multicast block position.
*
* SOURCE
*/
#define IB_MCAST_POSITION_MAX				0xF
/*********/
/****d* IBA Base: Constants/IB_MCAST_POSITION_SHIFT
* NAME
*	IB_MCAST_POSITION_SHIFT
*
* DESCRIPTION
*	Shift value to normalize the multicast block position value.
*
* SOURCE
*/
#define IB_MCAST_POSITION_SHIFT				28
/*********/
/****d* IBA Base: Constants/IB_PKEY_ENTRIES_MAX
* NAME
*	IB_PKEY_ENTRIES_MAX
*
* DESCRIPTION
*	Maximum number of PKEY entries per port (14.2.5.7).
*
* SOURCE
*/
#define IB_PKEY_ENTRIES_MAX (IB_PKEY_MAX_BLOCKS * IB_NUM_PKEY_ELEMENTS_IN_BLOCK)
/*********/
/****d* IBA Base: Constants/IB_PKEY_BASE_MASK
* NAME
*	IB_PKEY_BASE_MASK
*
* DESCRIPTION
*	Masks for the base P_Key value given a P_Key Entry.
*
* SOURCE
*/
#define IB_PKEY_BASE_MASK				(CL_HTON16(0x7FFF))
/*********/
/****d* IBA Base: Constants/IB_PKEY_TYPE_MASK
* NAME
*	IB_PKEY_TYPE_MASK
*
* DESCRIPTION
*	Masks for the P_Key membership type given a P_Key Entry.
*
* SOURCE
*/
#define IB_PKEY_TYPE_MASK				(CL_HTON16(0x8000))
/*********/
/****d* IBA Base: Constants/IB_DEFAULT_PARTIAL_PKEY
* NAME
*	IB_DEFAULT_PARTIAL_PKEY
*
* DESCRIPTION
*	0x7FFF in network order
*
* SOURCE
*/
#define IB_DEFAULT_PARTIAL_PKEY				(CL_HTON16(0x7FFF))
/**********/
/****d* IBA Base: Constants/IB_MCLASS_SUBN_LID
* NAME
*	IB_MCLASS_SUBN_LID
*
* DESCRIPTION
*	Subnet Management Class, Subnet Manager LID routed (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_SUBN_LID				0x01
/**********/
/****d* IBA Base: Constants/IB_MCLASS_SUBN_DIR
* NAME
*	IB_MCLASS_SUBN_DIR
*
* DESCRIPTION
*	Subnet Management Class, Subnet Manager directed route (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_SUBN_DIR				0x81
/**********/
/****d* IBA Base: Constants/IB_MCLASS_SUBN_ADM
* NAME
*	IB_MCLASS_SUBN_ADM
*
* DESCRIPTION
*	Management Class, Subnet Administration (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_SUBN_ADM				0x03
/**********/
/****d* IBA Base: Constants/IB_MCLASS_PERF
* NAME
*	IB_MCLASS_PERF
*
* DESCRIPTION
*	Management Class, Performance Management (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_PERF					0x04
/**********/
/****d* IBA Base: Constants/IB_MCLASS_BM
* NAME
*	IB_MCLASS_BM
*
* DESCRIPTION
*	Management Class, Baseboard Management (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_BM					0x05
/**********/
/****d* IBA Base: Constants/IB_MCLASS_DEV_MGMT
* NAME
*	IB_MCLASS_DEV_MGMT
*
* DESCRIPTION
*	Management Class, Device Management (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_DEV_MGMT				0x06
/**********/
/****d* IBA Base: Constants/IB_MCLASS_COMM_MGMT
* NAME
*	IB_MCLASS_COMM_MGMT
*
* DESCRIPTION
*	Management Class, Communication Management (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_COMM_MGMT				0x07
/**********/
/****d* IBA Base: Constants/IB_MCLASS_SNMP
* NAME
*	IB_MCLASS_SNMP
*
* DESCRIPTION
*	Management Class, SNMP Tunneling (13.4.4)
*
* SOURCE
*/
#define IB_MCLASS_SNMP					0x08
/**********/
/****d* IBA Base: Constants/IB_MCLASS_VENDOR_LOW_RANGE_MIN
* NAME
*	IB_MCLASS_VENDOR_LOW_RANGE_MIN
*
* DESCRIPTION
*	Management Class, Vendor Specific Low Range Start
*
* SOURCE
*/
#define IB_MCLASS_VENDOR_LOW_RANGE_MIN			0x09
/**********/
/****d* IBA Base: Constants/IB_MCLASS_VENDOR_LOW_RANGE_MAX
* NAME
*	IB_MCLASS_VENDOR_LOW_RANGE_MAX
*
* DESCRIPTION
*	Management Class, Vendor Specific Low Range End
*
* SOURCE
*/
#define IB_MCLASS_VENDOR_LOW_RANGE_MAX			0x0F
/**********/
/****d* IBA Base: Constants/IB_MCLASS_DEV_ADM
* NAME
*	IB_MCLASS_DEV_ADM
*
* DESCRIPTION
*	Management Class, Device Administration
*
* SOURCE
*/
#define IB_MCLASS_DEV_ADM				0x10
/**********/
/****d* IBA Base: Constants/IB_MCLASS_BIS
* NAME
*	IB_MCLASS_BIS
*
* DESCRIPTION
*	Management Class, BIS
*
* SOURCE
*/
#define IB_MCLASS_BIS					0x12
/**********/
/****d* IBA Base: Constants/IB_MCLASS_CC
* NAME
*	IB_MCLASS_CC
*
* DESCRIPTION
*	Management Class, Congestion Control (A10.4.1)
*
* SOURCE
*/
#define IB_MCLASS_CC					0x21
/**********/
/****d* IBA Base: Constants/IB_MCLASS_VENDOR_HIGH_RANGE_MIN
* NAME
*	IB_MCLASS_VENDOR_HIGH_RANGE_MIN
*
* DESCRIPTION
*	Management Class, Vendor Specific High Range Start
*
* SOURCE
*/
#define IB_MCLASS_VENDOR_HIGH_RANGE_MIN			0x30
/**********/
/****d* IBA Base: Constants/IB_MCLASS_VENDOR_HIGH_RANGE_MAX
* NAME
*	IB_MCLASS_VENDOR_HIGH_RANGE_MAX
*
* DESCRIPTION
*	Management Class, Vendor Specific High Range End
*
* SOURCE
*/
#define IB_MCLASS_VENDOR_HIGH_RANGE_MAX			0x4F
/**********/
/****f* IBA Base: Types/ib_class_is_vendor_specific_low
* NAME
*	ib_class_is_vendor_specific_low
*
* DESCRIPTION
*	Indicates if the Class Code if a vendor specific class from
*  the low range
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_class_is_vendor_specific_low(IN const uint8_t class_code)
{
	return ((class_code >= IB_MCLASS_VENDOR_LOW_RANGE_MIN) &&
		(class_code <= IB_MCLASS_VENDOR_LOW_RANGE_MAX));
}

/*
* PARAMETERS
*	class_code
*		[in] The Management Datagram Class Code
*
* RETURN VALUE
*	TRUE if the class is in the Low range of Vendor Specific MADs
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
* IB_MCLASS_VENDOR_LOW_RANGE_MIN, IB_MCLASS_VENDOR_LOW_RANGE_MAX
*********/

/****f* IBA Base: Types/ib_class_is_vendor_specific_high
* NAME
*	ib_class_is_vendor_specific_high
*
* DESCRIPTION
*	Indicates if the Class Code if a vendor specific class from
*  the high range
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_class_is_vendor_specific_high(IN const uint8_t class_code)
{
	return ((class_code >= IB_MCLASS_VENDOR_HIGH_RANGE_MIN) &&
		(class_code <= IB_MCLASS_VENDOR_HIGH_RANGE_MAX));
}

/*
* PARAMETERS
*	class_code
*		[in] The Management Datagram Class Code
*
* RETURN VALUE
*	TRUE if the class is in the High range of Vendor Specific MADs
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
* IB_MCLASS_VENDOR_HIGH_RANGE_MIN, IB_MCLASS_VENDOR_HIGH_RANGE_MAX
*********/

/****f* IBA Base: Types/ib_class_is_vendor_specific
* NAME
*	ib_class_is_vendor_specific
*
* DESCRIPTION
*	Indicates if the Class Code if a vendor specific class
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_class_is_vendor_specific(IN const uint8_t class_code)
{
	return (ib_class_is_vendor_specific_low(class_code) ||
		ib_class_is_vendor_specific_high(class_code));
}

/*
* PARAMETERS
*	class_code
*		[in] The Management Datagram Class Code
*
* RETURN VALUE
*	TRUE if the class is a Vendor Specific MAD
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*  ib_class_is_vendor_specific_low, ib_class_is_vendor_specific_high
*********/

/****f* IBA Base: Types/ib_class_is_rmpp
* NAME
*	ib_class_is_rmpp
*
* DESCRIPTION
*	Indicates if the Class Code supports RMPP
*
* SYNOPSIS
*/
static inline boolean_t OSM_API ib_class_is_rmpp(IN const uint8_t class_code)
{
	return ((class_code == IB_MCLASS_SUBN_ADM) ||
		(class_code == IB_MCLASS_DEV_MGMT) ||
		(class_code == IB_MCLASS_DEV_ADM) ||
		(class_code == IB_MCLASS_BIS) ||
		ib_class_is_vendor_specific_high(class_code));
}

/*
* PARAMETERS
*	class_code
*		[in] The Management Datagram Class Code
*
* RETURN VALUE
*	TRUE if the class supports RMPP
*	FALSE otherwise.
*
* NOTES
*
*********/

/*
 *	MAD methods
 */

/****d* IBA Base: Constants/IB_MAX_METHOD
* NAME
*	IB_MAX_METHOD
*
* DESCRIPTION
*	Total number of methods available to a class, not including the R-bit.
*
* SOURCE
*/
#define IB_MAX_METHODS						128
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_RESP_MASK
* NAME
*	IB_MAD_METHOD_RESP_MASK
*
* DESCRIPTION
*	Response mask to extract 'R' bit from the method field. (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_RESP_MASK				0x80
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_GET
* NAME
*	IB_MAD_METHOD_GET
*
* DESCRIPTION
*	Get() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_GET					0x01
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_SET
* NAME
*	IB_MAD_METHOD_SET
*
* DESCRIPTION
*	Set() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_SET					0x02
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_GET_RESP
* NAME
*	IB_MAD_METHOD_GET_RESP
*
* DESCRIPTION
*	GetResp() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_GET_RESP				0x81
/**********/

#define IB_MAD_METHOD_DELETE				0x15

/****d* IBA Base: Constants/IB_MAD_METHOD_GETTABLE
* NAME
*	IB_MAD_METHOD_GETTABLE
*
* DESCRIPTION
*	SubnAdmGetTable() Method (15.2.2)
*
* SOURCE
*/
#define IB_MAD_METHOD_GETTABLE				0x12
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_GETTABLE_RESP
* NAME
*	IB_MAD_METHOD_GETTABLE_RESP
*
* DESCRIPTION
*	SubnAdmGetTableResp() Method (15.2.2)
*
* SOURCE
*/
#define IB_MAD_METHOD_GETTABLE_RESP			0x92

/**********/

#define IB_MAD_METHOD_GETTRACETABLE			0x13
#define IB_MAD_METHOD_GETMULTI				0x14
#define IB_MAD_METHOD_GETMULTI_RESP			0x94

/****d* IBA Base: Constants/IB_MAD_METHOD_SEND
* NAME
*	IB_MAD_METHOD_SEND
*
* DESCRIPTION
*	Send() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_SEND					0x03
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_TRAP
* NAME
*	IB_MAD_METHOD_TRAP
*
* DESCRIPTION
*	Trap() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_TRAP					0x05
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_REPORT
* NAME
*	IB_MAD_METHOD_REPORT
*
* DESCRIPTION
*	Report() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_REPORT				0x06
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_REPORT_RESP
* NAME
*	IB_MAD_METHOD_REPORT_RESP
*
* DESCRIPTION
*	ReportResp() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_REPORT_RESP			0x86
/**********/

/****d* IBA Base: Constants/IB_MAD_METHOD_TRAP_REPRESS
* NAME
*	IB_MAD_METHOD_TRAP_REPRESS
*
* DESCRIPTION
*	TrapRepress() Method (13.4.5)
*
* SOURCE
*/
#define IB_MAD_METHOD_TRAP_REPRESS			0x07
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_BUSY
* NAME
*	IB_MAD_STATUS_BUSY
*
* DESCRIPTION
*	Temporarily busy, MAD discarded (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_BUSY				(CL_HTON16(0x0001))
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_REDIRECT
* NAME
*	IB_MAD_STATUS_REDIRECT
*
* DESCRIPTION
*	QP Redirection required (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_REDIRECT				(CL_HTON16(0x0002))
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_UNSUP_CLASS_VER
* NAME
*	IB_MAD_STATUS_UNSUP_CLASS_VER
*
* DESCRIPTION
*	Unsupported class version (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_UNSUP_CLASS_VER			(CL_HTON16(0x0004))
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_UNSUP_METHOD
* NAME
*	IB_MAD_STATUS_UNSUP_METHOD
*
* DESCRIPTION
*	Unsupported method (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_UNSUP_METHOD			(CL_HTON16(0x0008))
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_UNSUP_METHOD_ATTR
* NAME
*	IB_MAD_STATUS_UNSUP_METHOD_ATTR
*
* DESCRIPTION
*	Unsupported method/attribute combination (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_UNSUP_METHOD_ATTR			(CL_HTON16(0x000C))
/**********/

/****d* IBA Base: Constants/IB_MAD_STATUS_INVALID_FIELD
* NAME
*	IB_MAD_STATUS_INVALID_FIELD
*
* DESCRIPTION
*	Attribute contains one or more invalid fields (13.4.7)
*
* SOURCE
*/
#define IB_MAD_STATUS_INVALID_FIELD			(CL_HTON16(0x001C))
/**********/

#define IB_MAD_STATUS_CLASS_MASK			(CL_HTON16(0xFF00))

#define IB_SA_MAD_STATUS_SUCCESS			(CL_HTON16(0x0000))
#define IB_SA_MAD_STATUS_NO_RESOURCES			(CL_HTON16(0x0100))
#define IB_SA_MAD_STATUS_REQ_INVALID			(CL_HTON16(0x0200))
#define IB_SA_MAD_STATUS_NO_RECORDS			(CL_HTON16(0x0300))
#define IB_SA_MAD_STATUS_TOO_MANY_RECORDS		(CL_HTON16(0x0400))
#define IB_SA_MAD_STATUS_INVALID_GID			(CL_HTON16(0x0500))
#define IB_SA_MAD_STATUS_INSUF_COMPS			(CL_HTON16(0x0600))
#define IB_SA_MAD_STATUS_DENIED				(CL_HTON16(0x0700))
#define IB_SA_MAD_STATUS_PRIO_SUGGESTED			(CL_HTON16(0x0800))

#define IB_DM_MAD_STATUS_NO_IOC_RESP			(CL_HTON16(0x0100))
#define IB_DM_MAD_STATUS_NO_SVC_ENTRIES			(CL_HTON16(0x0200))
#define IB_DM_MAD_STATUS_IOC_FAILURE			(CL_HTON16(0x8000))

/****d* IBA Base: Constants/IB_MAD_ATTR_CLASS_PORT_INFO
* NAME
*	IB_MAD_ATTR_CLASS_PORT_INFO
*
* DESCRIPTION
*	ClassPortInfo attribute (13.4.8)
*
* SOURCE
*/
#define IB_MAD_ATTR_CLASS_PORT_INFO			(CL_HTON16(0x0001))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_NOTICE
* NAME
*	IB_MAD_ATTR_NOTICE
*
* DESCRIPTION
*	Notice attribute (13.4.8)
*
* SOURCE
*/
#define IB_MAD_ATTR_NOTICE					(CL_HTON16(0x0002))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_INFORM_INFO
* NAME
*	IB_MAD_ATTR_INFORM_INFO
*
* DESCRIPTION
*	InformInfo attribute (13.4.8)
*
* SOURCE
*/
#define IB_MAD_ATTR_INFORM_INFO				(CL_HTON16(0x0003))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_NODE_DESC
* NAME
*	IB_MAD_ATTR_NODE_DESC
*
* DESCRIPTION
*	NodeDescription attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_NODE_DESC				(CL_HTON16(0x0010))

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_SMPL_CTRL
* NAME
*	IB_MAD_ATTR_PORT_SMPL_CTRL
*
* DESCRIPTION
*	PortSamplesControl attribute (16.1.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_SMPL_CTRL			(CL_HTON16(0x0010))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_NODE_INFO
* NAME
*	IB_MAD_ATTR_NODE_INFO
*
* DESCRIPTION
*	NodeInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_NODE_INFO				(CL_HTON16(0x0011))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_SMPL_RSLT
* NAME
*	IB_MAD_ATTR_PORT_SMPL_RSLT
*
* DESCRIPTION
*	PortSamplesResult attribute (16.1.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_SMPL_RSLT			(CL_HTON16(0x0011))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SWITCH_INFO
* NAME
*	IB_MAD_ATTR_SWITCH_INFO
*
* DESCRIPTION
*	SwitchInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SWITCH_INFO				(CL_HTON16(0x0012))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_CNTRS
* NAME
*	IB_MAD_ATTR_PORT_CNTRS
*
* DESCRIPTION
*	PortCounters attribute (16.1.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_CNTRS				(CL_HTON16(0x0012))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_CNTRS_EXT
* NAME
*       IB_MAD_ATTR_PORT_CNTRS_EXT
*
* DESCRIPTION
*       PortCountersExtended attribute (16.1.4)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_CNTRS_EXT			(CL_HTON16(0x001D))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_XMIT_DATA_SL
* NAME
*	IB_MAD_ATTR_PORT_XMIT_DATA_SL
*
* DESCRIPTION
*	PortXmitDataSL attribute (A13.6.4)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_XMIT_DATA_SL			(CL_HTON16(0x0036))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_RCV_DATA_SL
* NAME
*	IB_MAD_ATTR_PORT_RCV_DATA_SL
*
* DESCRIPTION
*	PortRcvDataSL attribute (A13.6.4)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_RCV_DATA_SL			(CL_HTON16(0x0037))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_GUID_INFO
* NAME
*	IB_MAD_ATTR_GUID_INFO
*
* DESCRIPTION
*	GUIDInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_GUID_INFO				(CL_HTON16(0x0014))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORT_INFO
* NAME
*	IB_MAD_ATTR_PORT_INFO
*
* DESCRIPTION
*	PortInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORT_INFO				(CL_HTON16(0x0015))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_P_KEY_TABLE
* NAME
*	IB_MAD_ATTR_P_KEY_TABLE
*
* DESCRIPTION
*	PartitionTable attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_P_KEY_TABLE				(CL_HTON16(0x0016))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SLVL_TABLE
* NAME
*	IB_MAD_ATTR_SLVL_TABLE
*
* DESCRIPTION
*	SL VL Mapping Table attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SLVL_TABLE				(CL_HTON16(0x0017))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_VL_ARBITRATION
* NAME
*	IB_MAD_ATTR_VL_ARBITRATION
*
* DESCRIPTION
*	VL Arbitration Table attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_VL_ARBITRATION			(CL_HTON16(0x0018))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_LIN_FWD_TBL
* NAME
*	IB_MAD_ATTR_LIN_FWD_TBL
*
* DESCRIPTION
*	Switch linear forwarding table
*
* SOURCE
*/
#define IB_MAD_ATTR_LIN_FWD_TBL				(CL_HTON16(0x0019))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_RND_FWD_TBL
* NAME
*	IB_MAD_ATTR_RND_FWD_TBL
*
* DESCRIPTION
*	Switch random forwarding table
*
* SOURCE
*/
#define IB_MAD_ATTR_RND_FWD_TBL				(CL_HTON16(0x001A))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_MCAST_FWD_TBL
* NAME
*	IB_MAD_ATTR_MCAST_FWD_TBL
*
* DESCRIPTION
*	Switch multicast forwarding table
*
* SOURCE
*/
#define IB_MAD_ATTR_MCAST_FWD_TBL			(CL_HTON16(0x001B))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_NODE_RECORD
* NAME
*	IB_MAD_ATTR_NODE_RECORD
*
* DESCRIPTION
*	NodeRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_NODE_RECORD				(CL_HTON16(0x0011))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PORTINFO_RECORD
* NAME
*	IB_MAD_ATTR_PORTINFO_RECORD
*
* DESCRIPTION
*	PortInfoRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_PORTINFO_RECORD			(CL_HTON16(0x0012))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SWITCH_INFO_RECORD
* NAME
*       IB_MAD_ATTR_SWITCH_INFO_RECORD
*
* DESCRIPTION
*       SwitchInfoRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SWITCH_INFO_RECORD			(CL_HTON16(0x0014))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_LINK_RECORD
* NAME
*	IB_MAD_ATTR_LINK_RECORD
*
* DESCRIPTION
*	LinkRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_LINK_RECORD				(CL_HTON16(0x0020))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SM_INFO
* NAME
*	IB_MAD_ATTR_SM_INFO
*
* DESCRIPTION
*	SMInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SM_INFO				(CL_HTON16(0x0020))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SMINFO_RECORD
* NAME
*	IB_MAD_ATTR_SMINFO_RECORD
*
* DESCRIPTION
*	SMInfoRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SMINFO_RECORD			(CL_HTON16(0x0018))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_GUIDINFO_RECORD
* NAME
*       IB_MAD_ATTR_GUIDINFO_RECORD
*
* DESCRIPTION
*       GuidInfoRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_GUIDINFO_RECORD			(CL_HTON16(0x0030))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_VENDOR_DIAG
* NAME
*	IB_MAD_ATTR_VENDOR_DIAG
*
* DESCRIPTION
*	VendorDiag attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_VENDOR_DIAG				(CL_HTON16(0x0030))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_LED_INFO
* NAME
*	IB_MAD_ATTR_LED_INFO
*
* DESCRIPTION
*	LedInfo attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_LED_INFO				(CL_HTON16(0x0031))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO
* NAME
*	IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO
*
* DESCRIPTION
*	Vendor specific SM attribute (14.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO		(CL_HTON16(0xFF90))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SERVICE_RECORD
* NAME
*	IB_MAD_ATTR_SERVICE_RECORD
*
* DESCRIPTION
*	ServiceRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SERVICE_RECORD			(CL_HTON16(0x0031))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_LFT_RECORD
* NAME
*	IB_MAD_ATTR_LFT_RECORD
*
* DESCRIPTION
*	LinearForwardingTableRecord attribute (15.2.5.6)
*
* SOURCE
*/
#define IB_MAD_ATTR_LFT_RECORD				(CL_HTON16(0x0015))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_MFT_RECORD
* NAME
*       IB_MAD_ATTR_MFT_RECORD
*
* DESCRIPTION
*       MulticastForwardingTableRecord attribute (15.2.5.8)
*
* SOURCE
*/
#define IB_MAD_ATTR_MFT_RECORD				(CL_HTON16(0x0017))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PKEYTBL_RECORD
* NAME
*	IB_MAD_ATTR_PKEYTBL_RECORD
*
* DESCRIPTION
*	PKEY Table Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_PKEY_TBL_RECORD			(CL_HTON16(0x0033))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PATH_RECORD
* NAME
*	IB_MAD_ATTR_PATH_RECORD
*
* DESCRIPTION
*	PathRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_PATH_RECORD				(CL_HTON16(0x0035))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_VLARB_RECORD
* NAME
*	IB_MAD_ATTR_VLARB_RECORD
*
* DESCRIPTION
*	VL Arbitration Table Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_VLARB_RECORD			(CL_HTON16(0x0036))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SLVL_RECORD
* NAME
*	IB_MAD_ATTR_SLVL_RECORD
*
* DESCRIPTION
*	SLtoVL Mapping Table Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SLVL_RECORD				(CL_HTON16(0x0013))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_MCMEMBER_RECORD
* NAME
*	IB_MAD_ATTR_MCMEMBER_RECORD
*
* DESCRIPTION
*	MCMemberRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_MCMEMBER_RECORD			(CL_HTON16(0x0038))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_TRACE_RECORD
* NAME
*	IB_MAD_ATTR_TRACE_RECORD
*
* DESCRIPTION
*	TraceRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_TRACE_RECORD			(CL_HTON16(0x0039))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_MULTIPATH_RECORD
* NAME
*	IB_MAD_ATTR_MULTIPATH_RECORD
*
* DESCRIPTION
*	MultiPathRecord attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_MULTIPATH_RECORD			(CL_HTON16(0x003A))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SVC_ASSOCIATION_RECORD
* NAME
*	IB_MAD_ATTR_SVC_ASSOCIATION_RECORD
*
* DESCRIPTION
*	Service Association Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SVC_ASSOCIATION_RECORD		(CL_HTON16(0x003B))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_INFORM_INFO_RECORD
* NAME
*	IB_MAD_ATTR_INFORM_INFO_RECORD
*
* DESCRIPTION
*	InformInfo Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_INFORM_INFO_RECORD			(CL_HTON16(0x00F3))

/****d* IBA Base: Constants/IB_MAD_ATTR_IO_UNIT_INFO
* NAME
*	IB_MAD_ATTR_IO_UNIT_INFO
*
* DESCRIPTION
*	IOUnitInfo attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_IO_UNIT_INFO			(CL_HTON16(0x0010))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_IO_CONTROLLER_PROFILE
* NAME
*	IB_MAD_ATTR_IO_CONTROLLER_PROFILE
*
* DESCRIPTION
*	IOControllerProfile attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_IO_CONTROLLER_PROFILE	(CL_HTON16(0x0011))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SERVICE_ENTRIES
* NAME
*	IB_MAD_ATTR_SERVICE_ENTRIES
*
* DESCRIPTION
*	ServiceEntries attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_SERVICE_ENTRIES			(CL_HTON16(0x0012))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_DIAGNOSTIC_TIMEOUT
* NAME
*	IB_MAD_ATTR_DIAGNOSTIC_TIMEOUT
*
* DESCRIPTION
*	DiagnosticTimeout attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_DIAGNOSTIC_TIMEOUT		(CL_HTON16(0x0020))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_PREPARE_TO_TEST
* NAME
*	IB_MAD_ATTR_PREPARE_TO_TEST
*
* DESCRIPTION
*	PrepareToTest attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_PREPARE_TO_TEST			(CL_HTON16(0x0021))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_TEST_DEVICE_ONCE
* NAME
*	IB_MAD_ATTR_TEST_DEVICE_ONCE
*
* DESCRIPTION
*	TestDeviceOnce attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_TEST_DEVICE_ONCE		(CL_HTON16(0x0022))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_TEST_DEVICE_LOOP
* NAME
*	IB_MAD_ATTR_TEST_DEVICE_LOOP
*
* DESCRIPTION
*	TestDeviceLoop attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_TEST_DEVICE_LOOP		(CL_HTON16(0x0023))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_DIAG_CODE
* NAME
*	IB_MAD_ATTR_DIAG_CODE
*
* DESCRIPTION
*	DiagCode attribute (16.3.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_DIAG_CODE				(CL_HTON16(0x0024))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SVC_ASSOCIATION_RECORD
* NAME
*	IB_MAD_ATTR_SVC_ASSOCIATION_RECORD
*
* DESCRIPTION
*	Service Association Record attribute (15.2.5)
*
* SOURCE
*/
#define IB_MAD_ATTR_SVC_ASSOCIATION_RECORD	(CL_HTON16(0x003B))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_CONG_INFO
* NAME
*	IB_MAD_ATTR_CONG_INFO
*
* DESCRIPTION
*	CongestionInfo attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_CONG_INFO				(CL_HTON16(0x0011))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_CONG_KEY_INFO
* NAME
*	IB_MAD_ATTR_CONG_KEY_INFO
*
* DESCRIPTION
*	CongestionKeyInfo attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_CONG_KEY_INFO			(CL_HTON16(0x0012))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_CONG_LOG
* NAME
*	IB_MAD_ATTR_CONG_LOG
*
* DESCRIPTION
*	CongestionLog attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_CONG_LOG				(CL_HTON16(0x0013))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SW_CONG_SETTING
* NAME
*	IB_MAD_ATTR_SW_CONG_SETTING
*
* DESCRIPTION
*	SwitchCongestionSetting attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_SW_CONG_SETTING			(CL_HTON16(0x0014))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_SW_PORT_CONG_SETTING
* NAME
*	IB_MAD_ATTR_SW_PORT_CONG_SETTING
*
* DESCRIPTION
*	SwitchPortCongestionSetting attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_SW_PORT_CONG_SETTING		(CL_HTON16(0x0015))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_CA_CONG_SETTING
* NAME
*	IB_MAD_ATTR_CA_CONG_SETTING
*
* DESCRIPTION
*	CACongestionSetting attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_CA_CONG_SETTING			(CL_HTON16(0x0016))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_CC_TBL
* NAME
*	IB_MAD_ATTR_CC_TBL
*
* DESCRIPTION
*	CongestionControlTable attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_CC_TBL				(CL_HTON16(0x0017))
/**********/

/****d* IBA Base: Constants/IB_MAD_ATTR_TIME_STAMP
* NAME
*	IB_MAD_ATTR_TIME_STAMP
*
* DESCRIPTION
*	TimeStamp attribute (A10.4.3)
*
* SOURCE
*/
#define IB_MAD_ATTR_TIME_STAMP				(CL_HTON16(0x0018))
/**********/

/****d* IBA Base: Constants/IB_NODE_TYPE_CA
* NAME
*	IB_NODE_TYPE_CA
*
* DESCRIPTION
*	Encoded generic node type used in MAD attributes (13.4.8.2)
*
* SOURCE
*/
#define IB_NODE_TYPE_CA						0x01
/**********/

/****d* IBA Base: Constants/IB_NODE_TYPE_SWITCH
* NAME
*	IB_NODE_TYPE_SWITCH
*
* DESCRIPTION
*	Encoded generic node type used in MAD attributes (13.4.8.2)
*
* SOURCE
*/
#define IB_NODE_TYPE_SWITCH					0x02
/**********/

/****d* IBA Base: Constants/IB_NODE_TYPE_ROUTER
* NAME
*	IB_NODE_TYPE_ROUTER
*
* DESCRIPTION
*	Encoded generic node type used in MAD attributes (13.4.8.2)
*
* SOURCE
*/
#define IB_NODE_TYPE_ROUTER					0x03
/**********/

/****d* IBA Base: Constants/IB_NOTICE_PRODUCER_TYPE_CA
* NAME
*	IB_NOTICE_PRODUCER_TYPE_CA
*
* DESCRIPTION
*	Encoded generic producer type used in Notice attribute (13.4.8.2)
*
* SOURCE
*/
#define IB_NOTICE_PRODUCER_TYPE_CA			(CL_HTON32(0x000001))
/**********/

/****d* IBA Base: Constants/IB_NOTICE_PRODUCER_TYPE_SWITCH
* NAME
*	IB_NOTICE_PRODUCER_TYPE_SWITCH
*
* DESCRIPTION
*	Encoded generic producer type used in Notice attribute (13.4.8.2)
*
* SOURCE
*/
#define IB_NOTICE_PRODUCER_TYPE_SWITCH			(CL_HTON32(0x000002))
/**********/

/****d* IBA Base: Constants/IB_NOTICE_PRODUCER_TYPE_ROUTER
* NAME
*	IB_NOTICE_PRODUCER_TYPE_ROUTER
*
* DESCRIPTION
*	Encoded generic producer type used in Notice attribute (13.4.8.2)
*
* SOURCE
*/
#define IB_NOTICE_PRODUCER_TYPE_ROUTER			(CL_HTON32(0x000003))
/**********/

/****d* IBA Base: Constants/IB_NOTICE_PRODUCER_TYPE_CLASS_MGR
* NAME
*	IB_NOTICE_PRODUCER_TYPE_CLASS_MGR
*
* DESCRIPTION
*	Encoded generic producer type used in Notice attribute (13.4.8.2)
*
* SOURCE
*/
#define IB_NOTICE_PRODUCER_TYPE_CLASS_MGR		(CL_HTON32(0x000004))
/**********/

/****d* IBA Base: Constants/IB_MTU_LEN_TYPE
* NAME
*	IB_MTU_LEN_TYPE
*
* DESCRIPTION
*	Encoded path MTU.
*		1: 256
*		2: 512
*		3: 1024
*		4: 2048
*		5: 4096
*		others: reserved
*
* SOURCE
*/
#define IB_MTU_LEN_256							1
#define IB_MTU_LEN_512							2
#define IB_MTU_LEN_1024							3
#define IB_MTU_LEN_2048							4
#define IB_MTU_LEN_4096							5

#define IB_MIN_MTU    IB_MTU_LEN_256
#define IB_MAX_MTU    IB_MTU_LEN_4096

/**********/

/****d* IBA Base: Constants/IB_PATH_SELECTOR_TYPE
* NAME
*	IB_PATH_SELECTOR_TYPE
*
* DESCRIPTION
*	Path selector.
*		0: greater than specified
*		1: less than specified
*		2: exactly the specified
*		3: largest available
*
* SOURCE
*/
#define IB_PATH_SELECTOR_GREATER_THAN		0
#define IB_PATH_SELECTOR_LESS_THAN		1
#define IB_PATH_SELECTOR_EXACTLY		2
#define IB_PATH_SELECTOR_LARGEST		3
/**********/

/****d* IBA Base: Constants/IB_SMINFO_STATE_NOTACTIVE
* NAME
*	IB_SMINFO_STATE_NOTACTIVE
*
* DESCRIPTION
*	Encoded state value used in the SMInfo attribute.
*
* SOURCE
*/
#define IB_SMINFO_STATE_NOTACTIVE			0
/**********/

/****d* IBA Base: Constants/IB_SMINFO_STATE_DISCOVERING
* NAME
*	IB_SMINFO_STATE_DISCOVERING
*
* DESCRIPTION
*	Encoded state value used in the SMInfo attribute.
*
* SOURCE
*/
#define IB_SMINFO_STATE_DISCOVERING			1
/**********/

/****d* IBA Base: Constants/IB_SMINFO_STATE_STANDBY
* NAME
*	IB_SMINFO_STATE_STANDBY
*
* DESCRIPTION
*	Encoded state value used in the SMInfo attribute.
*
* SOURCE
*/
#define IB_SMINFO_STATE_STANDBY				2
/**********/

/****d* IBA Base: Constants/IB_SMINFO_STATE_MASTER
* NAME
*	IB_SMINFO_STATE_MASTER
*
* DESCRIPTION
*	Encoded state value used in the SMInfo attribute.
*
* SOURCE
*/
#define IB_SMINFO_STATE_MASTER				3
/**********/

/****d* IBA Base: Constants/IB_PATH_REC_SL_MASK
* NAME
*	IB_PATH_REC_SL_MASK
*
* DESCRIPTION
*	Mask for the sl field for path record
*
* SOURCE
*/
#define IB_PATH_REC_SL_MASK				0x000F

/****d* IBA Base: Constants/IB_MULTIPATH_REC_SL_MASK
* NAME
*	IB_MULTIPATH_REC_SL_MASK
*
* DESCRIPTION
*	Mask for the sl field for MultiPath record
*
* SOURCE
*/
#define IB_MULTIPATH_REC_SL_MASK			0x000F

/****d* IBA Base: Constants/IB_PATH_REC_QOS_CLASS_MASK
* NAME
*	IB_PATH_REC_QOS_CLASS_MASK
*
* DESCRIPTION
*	Mask for the QoS class field for path record
*
* SOURCE
*/
#define IB_PATH_REC_QOS_CLASS_MASK			0xFFF0

/****d* IBA Base: Constants/IB_MULTIPATH_REC_QOS_CLASS_MASK
* NAME
*	IB_MULTIPATH_REC_QOS_CLASS_MASK
*
* DESCRIPTION
*	Mask for the QoS class field for MultiPath record
*
* SOURCE
*/
#define IB_MULTIPATH_REC_QOS_CLASS_MASK			0xFFF0

/****d* IBA Base: Constants/IB_PATH_REC_SELECTOR_MASK
* NAME
*	IB_PATH_REC_SELECTOR_MASK
*
* DESCRIPTION
*	Mask for the selector field for path record MTU, rate,
*	and packet lifetime.
*
* SOURCE
*/
#define IB_PATH_REC_SELECTOR_MASK			0xC0

/****d* IBA Base: Constants/IB_MULTIPATH_REC_SELECTOR_MASK
* NAME
*       IB_MULTIPATH_REC_SELECTOR_MASK
*
* DESCRIPTION
*       Mask for the selector field for multipath record MTU, rate,
*       and packet lifetime.
*
* SOURCE
*/
#define IB_MULTIPATH_REC_SELECTOR_MASK                       0xC0
/**********/

/****d* IBA Base: Constants/IB_PATH_REC_BASE_MASK
* NAME
*	IB_PATH_REC_BASE_MASK
*
* DESCRIPTION
*	Mask for the base value field for path record MTU, rate,
*	and packet lifetime.
*
* SOURCE
*/
#define IB_PATH_REC_BASE_MASK				0x3F
/**********/

/****d* IBA Base: Constants/IB_MULTIPATH_REC_BASE_MASK
* NAME
*       IB_MULTIPATH_REC_BASE_MASK
*
* DESCRIPTION
*       Mask for the base value field for multipath record MTU, rate,
*       and packet lifetime.
*
* SOURCE
*/
#define IB_MULTIPATH_REC_BASE_MASK                      0x3F
/**********/

/****h* IBA Base/Type Definitions
* NAME
*	Type Definitions
*
* DESCRIPTION
*	Definitions are from the InfiniBand Architecture Specification v1.2
*
*********/

/****d* IBA Base: Types/ib_net16_t
* NAME
*	ib_net16_t
*
* DESCRIPTION
*	Defines the network ordered type for 16-bit values.
*
* SOURCE
*/
typedef uint16_t ib_net16_t;
/**********/

/****d* IBA Base: Types/ib_net32_t
* NAME
*	ib_net32_t
*
* DESCRIPTION
*	Defines the network ordered type for 32-bit values.
*
* SOURCE
*/
typedef uint32_t ib_net32_t;
/**********/

/****d* IBA Base: Types/ib_net64_t
* NAME
*	ib_net64_t
*
* DESCRIPTION
*	Defines the network ordered type for 64-bit values.
*
* SOURCE
*/
typedef uint64_t ib_net64_t;
/**********/

/****d* IBA Base: Types/ib_gid_prefix_t
* NAME
*	ib_gid_prefix_t
*
* DESCRIPTION
*
* SOURCE
*/
typedef ib_net64_t ib_gid_prefix_t;
/**********/

/****d* IBA Base: Constants/ib_link_states_t
* NAME
*	ib_link_states_t
*
* DESCRIPTION
*	Defines the link states of a port.
*
* SOURCE
*/
#define IB_LINK_NO_CHANGE 0
#define IB_LINK_DOWN      1
#define IB_LINK_INIT	  2
#define IB_LINK_ARMED     3
#define IB_LINK_ACTIVE    4
#define IB_LINK_ACT_DEFER 5
/**********/

static const char *const __ib_node_type_str[] = {
	"UNKNOWN",
	"Channel Adapter",
	"Switch",
	"Router"
};

/****f* IBA Base: Types/ib_get_node_type_str
* NAME
*	ib_get_node_type_str
*
* DESCRIPTION
*	Returns a string for the specified node type.
*	14.2.5.3 NodeInfo
*
* SYNOPSIS
*/
static inline const char *OSM_API ib_get_node_type_str(IN uint8_t node_type)
{
	if (node_type > IB_NODE_TYPE_ROUTER)
		node_type = 0;
	return (__ib_node_type_str[node_type]);
}

/*
* PARAMETERS
*	node_type
*		[in] Encoded node type as returned in the NodeInfo attribute.

* RETURN VALUES
*	Pointer to the node type string.
*
* NOTES
*
* SEE ALSO
* ib_node_info_t
*********/

static const char *const __ib_producer_type_str[] = {
	"UNKNOWN",
	"Channel Adapter",
	"Switch",
	"Router",
	"Class Manager"
};

/****f* IBA Base: Types/ib_get_producer_type_str
* NAME
*	ib_get_producer_type_str
*
* DESCRIPTION
*	Returns a string for the specified producer type
*	13.4.8.2 Notice
*	13.4.8.3 InformInfo
*
* SYNOPSIS
*/
static inline const char *OSM_API
ib_get_producer_type_str(IN ib_net32_t producer_type)
{
	if (cl_ntoh32(producer_type) >
	    CL_NTOH32(IB_NOTICE_PRODUCER_TYPE_CLASS_MGR))
		producer_type = 0;
	return (__ib_producer_type_str[cl_ntoh32(producer_type)]);
}

/*
* PARAMETERS
*	producer_type
*		[in] Encoded producer type from the Notice attribute

* RETURN VALUES
*	Pointer to the producer type string.
*
* NOTES
*
* SEE ALSO
* ib_notice_get_prod_type
*********/

static const char *const __ib_port_state_str[] = {
	"No State Change (NOP)",
	"DOWN",
	"INIT",
	"ARMED",
	"ACTIVE",
	"ACTDEFER",
	"UNKNOWN"
};

/****f* IBA Base: Types/ib_get_port_state_str
* NAME
*	ib_get_port_state_str
*
* DESCRIPTION
*	Returns a string for the specified port state.
*
* SYNOPSIS
*/
static inline const char *OSM_API ib_get_port_state_str(IN uint8_t port_state)
{
	if (port_state > IB_LINK_ACTIVE)
		port_state = IB_LINK_ACTIVE + 1;
	return (__ib_port_state_str[port_state]);
}

/*
* PARAMETERS
*	port_state
*		[in] Encoded port state as returned in the PortInfo attribute.

* RETURN VALUES
*	Pointer to the port state string.
*
* NOTES
*
* SEE ALSO
* ib_port_info_t
*********/

/****f* IBA Base: Types/ib_get_port_state_from_str
* NAME
*	ib_get_port_state_from_str
*
* DESCRIPTION
*	Returns a string for the specified port state.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_get_port_state_from_str(IN char *p_port_state_str)
{
	if (!strncmp(p_port_state_str, "No State Change (NOP)", 12))
		return (0);
	else if (!strncmp(p_port_state_str, "DOWN", 4))
		return (1);
	else if (!strncmp(p_port_state_str, "INIT", 4))
		return (2);
	else if (!strncmp(p_port_state_str, "ARMED", 5))
		return (3);
	else if (!strncmp(p_port_state_str, "ACTIVE", 6))
		return (4);
	else if (!strncmp(p_port_state_str, "ACTDEFER", 8))
		return (5);
	return (6);
}

/*
* PARAMETERS
*	p_port_state_str
*		[in] A string matching one returned by ib_get_port_state_str
*
* RETURN VALUES
*	The appropriate code.
*
* NOTES
*
* SEE ALSO
*	ib_port_info_t
*********/

/****d* IBA Base: Constants/Join States
* NAME
*	Join States
*
* DESCRIPTION
*	Defines the join state flags for multicast group management.
*
* SOURCE
*/
#define IB_JOIN_STATE_FULL		1
#define IB_JOIN_STATE_NON		2
#define IB_JOIN_STATE_SEND_ONLY		4
/**********/

/****f* IBA Base: Types/ib_pkey_get_base
* NAME
*	ib_pkey_get_base
*
* DESCRIPTION
*	Returns the base P_Key value with the membership bit stripped.
*
* SYNOPSIS
*/
static inline ib_net16_t OSM_API ib_pkey_get_base(IN const ib_net16_t pkey)
{
	return ((ib_net16_t) (pkey & IB_PKEY_BASE_MASK));
}

/*
* PARAMETERS
*	pkey
*		[in] P_Key value
*
* RETURN VALUE
*	Returns the base P_Key value with the membership bit stripped.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_pkey_is_full_member
* NAME
*	ib_pkey_is_full_member
*
* DESCRIPTION
*	Indicates if the port is a full member of the partition.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API ib_pkey_is_full_member(IN const ib_net16_t pkey)
{
	return ((pkey & IB_PKEY_TYPE_MASK) == IB_PKEY_TYPE_MASK);
}

/*
* PARAMETERS
*	pkey
*		[in] P_Key value
*
* RETURN VALUE
*	TRUE if the port is a full member of the partition.
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
* ib_pkey_get_base, ib_net16_t
*********/

/****f* IBA Base: Types/ib_pkey_is_invalid
* NAME
*	ib_pkey_is_invalid
*
* DESCRIPTION
*	Returns TRUE if the given P_Key is an invalid P_Key
*  C10-116: the CI shall regard a P_Key as invalid if its low-order
*           15 bits are all zero...
*
* SYNOPSIS
*/
static inline boolean_t OSM_API ib_pkey_is_invalid(IN const ib_net16_t pkey)
{
	return ib_pkey_get_base(pkey) == 0x0000 ? TRUE : FALSE;
}

/*
* PARAMETERS
*	pkey
*		[in] P_Key value
*
* RETURN VALUE
*	Returns the base P_Key value with the membership bit stripped.
*
* NOTES
*
* SEE ALSO
*********/

/****d* IBA Base: Types/ib_gid_t
* NAME
*	ib_gid_t
*
* DESCRIPTION
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef union _ib_gid {
	uint8_t raw[16];
	struct _ib_gid_unicast {
		ib_gid_prefix_t prefix;
		ib_net64_t interface_id;
	} PACK_SUFFIX unicast;
	struct _ib_gid_multicast {
		uint8_t header[2];
		uint8_t raw_group_id[14];
	} PACK_SUFFIX multicast;
} PACK_SUFFIX ib_gid_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	raw
*		GID represented as an unformated byte array.
*
*	unicast
*		Typical unicast representation with subnet prefix and
*		port GUID.
*
*	multicast
*		Representation for multicast use.
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_gid_is_multicast
* NAME
*	ib_gid_is_multicast
*
* DESCRIPTION
*       Returns a boolean indicating whether a GID is a multicast GID.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API ib_gid_is_multicast(IN const ib_gid_t * p_gid)
{
	return (p_gid->raw[0] == 0xFF);
}

/****f* IBA Base: Types/ib_gid_get_scope
* NAME
*	ib_gid_get_scope
*
* DESCRIPTION
*	Returns scope of (assumed) multicast GID.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API ib_mgid_get_scope(IN const ib_gid_t * p_gid)
{
	return (p_gid->raw[1] & 0x0F);
}

/****f* IBA Base: Types/ib_gid_set_scope
* NAME
*	ib_gid_set_scope
*
* DESCRIPTION
*	Sets scope of (assumed) multicast GID.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_mgid_set_scope(IN ib_gid_t * const p_gid, IN const uint8_t scope)
{
	p_gid->raw[1] &= 0xF0;
	p_gid->raw[1] |= scope & 0x0F;
}

/****f* IBA Base: Types/ib_gid_set_default
* NAME
*	ib_gid_set_default
*
* DESCRIPTION
*	Sets a GID to the default value.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_gid_set_default(IN ib_gid_t * const p_gid, IN const ib_net64_t interface_id)
{
	p_gid->unicast.prefix = IB_DEFAULT_SUBNET_PREFIX;
	p_gid->unicast.interface_id = interface_id;
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
*	interface_id
*		[in] Manufacturer assigned EUI64 value of a port.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* IBA Base: Types/ib_gid_get_subnet_prefix
* NAME
*	ib_gid_get_subnet_prefix
*
* DESCRIPTION
*	Gets the subnet prefix from a GID.
*
* SYNOPSIS
*/
static inline ib_net64_t OSM_API
ib_gid_get_subnet_prefix(IN const ib_gid_t * const p_gid)
{
	return (p_gid->unicast.prefix);
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
* RETURN VALUES
*	64-bit subnet prefix value.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* IBA Base: Types/ib_gid_is_link_local
* NAME
*	ib_gid_is_link_local
*
* DESCRIPTION
*	Returns TRUE if the unicast GID scoping indicates link local,
*	FALSE otherwise.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_gid_is_link_local(IN const ib_gid_t * const p_gid)
{
	return ((ib_gid_get_subnet_prefix(p_gid) &
		 CL_HTON64(0xFFC0000000000000ULL)) == IB_DEFAULT_SUBNET_PREFIX);
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
* RETURN VALUES
*	Returns TRUE if the unicast GID scoping indicates link local,
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* IBA Base: Types/ib_gid_is_site_local
* NAME
*	ib_gid_is_site_local
*
* DESCRIPTION
*	Returns TRUE if the unicast GID scoping indicates site local,
*	FALSE otherwise.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_gid_is_site_local(IN const ib_gid_t * const p_gid)
{
	return ((ib_gid_get_subnet_prefix(p_gid) &
		 CL_HTON64(0xFFFFFFFFFFFF0000ULL)) ==
		CL_HTON64(0xFEC0000000000000ULL));
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
* RETURN VALUES
*	Returns TRUE if the unicast GID scoping indicates site local,
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* IBA Base: Types/ib_gid_get_guid
* NAME
*	ib_gid_get_guid
*
* DESCRIPTION
*	Gets the guid from a GID.
*
* SYNOPSIS
*/
static inline ib_net64_t OSM_API
ib_gid_get_guid(IN const ib_gid_t * const p_gid)
{
	return (p_gid->unicast.interface_id);
}

/*
* PARAMETERS
*	p_gid
*		[in] Pointer to the GID object.
*
* RETURN VALUES
*	64-bit GUID value.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****s* IBA Base: Types/ib_path_rec_t
* NAME
*	ib_path_rec_t
*
* DESCRIPTION
*	Path records encapsulate the properties of a given
*	route between two end-points on a subnet.
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_path_rec {
	ib_net64_t service_id;
	ib_gid_t dgid;
	ib_gid_t sgid;
	ib_net16_t dlid;
	ib_net16_t slid;
	ib_net32_t hop_flow_raw;
	uint8_t tclass;
	uint8_t num_path;
	ib_net16_t pkey;
	ib_net16_t qos_class_sl;
	uint8_t mtu;
	uint8_t rate;
	uint8_t pkt_life;
	uint8_t preference;
	uint8_t resv2[6];
} PACK_SUFFIX ib_path_rec_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	service_id
*		Service ID for QoS.
*
*	dgid
*		GID of destination port.
*
*	sgid
*		GID of source port.
*
*	dlid
*		LID of destination port.
*
*	slid
*		LID of source port.
*
*	hop_flow_raw
*		Global routing parameters: hop count, flow label and raw bit.
*
*	tclass
*		Another global routing parameter.
*
*	num_path
*		Reversible path - 1 bit to say if path is reversible.
*		num_path [6:0] In queries, maximum number of paths to return.
*		In responses, undefined.
*
*	pkey
*		Partition key (P_Key) to use on this path.
*
*	qos_class_sl
*		QoS class and service level to use on this path.
*
*	mtu
*		MTU and MTU selector fields to use on this path
*
*	rate
*		Rate and rate selector fields to use on this path.
*
*	pkt_life
*		Packet lifetime
*
*	preference
*		Indicates the relative merit of this path versus other path
*		records returned from the SA.  Lower numbers are better.
*
*	resv2
*		Reserved bytes.
* SEE ALSO
*********/

/* Path Record Component Masks */
#define  IB_PR_COMPMASK_SERVICEID_MSB     (CL_HTON64(((uint64_t)1)<<0))
#define  IB_PR_COMPMASK_SERVICEID_LSB     (CL_HTON64(((uint64_t)1)<<1))
#define  IB_PR_COMPMASK_DGID              (CL_HTON64(((uint64_t)1)<<2))
#define  IB_PR_COMPMASK_SGID              (CL_HTON64(((uint64_t)1)<<3))
#define  IB_PR_COMPMASK_DLID              (CL_HTON64(((uint64_t)1)<<4))
#define  IB_PR_COMPMASK_SLID              (CL_HTON64(((uint64_t)1)<<5))
#define  IB_PR_COMPMASK_RAWTRAFFIC        (CL_HTON64(((uint64_t)1)<<6))
#define  IB_PR_COMPMASK_RESV0             (CL_HTON64(((uint64_t)1)<<7))
#define  IB_PR_COMPMASK_FLOWLABEL         (CL_HTON64(((uint64_t)1)<<8))
#define  IB_PR_COMPMASK_HOPLIMIT          (CL_HTON64(((uint64_t)1)<<9))
#define  IB_PR_COMPMASK_TCLASS            (CL_HTON64(((uint64_t)1)<<10))
#define  IB_PR_COMPMASK_REVERSIBLE        (CL_HTON64(((uint64_t)1)<<11))
#define  IB_PR_COMPMASK_NUMBPATH          (CL_HTON64(((uint64_t)1)<<12))
#define  IB_PR_COMPMASK_PKEY              (CL_HTON64(((uint64_t)1)<<13))
#define  IB_PR_COMPMASK_QOS_CLASS         (CL_HTON64(((uint64_t)1)<<14))
#define  IB_PR_COMPMASK_SL                (CL_HTON64(((uint64_t)1)<<15))
#define  IB_PR_COMPMASK_MTUSELEC          (CL_HTON64(((uint64_t)1)<<16))
#define  IB_PR_COMPMASK_MTU               (CL_HTON64(((uint64_t)1)<<17))
#define  IB_PR_COMPMASK_RATESELEC         (CL_HTON64(((uint64_t)1)<<18))
#define  IB_PR_COMPMASK_RATE              (CL_HTON64(((uint64_t)1)<<19))
#define  IB_PR_COMPMASK_PKTLIFETIMESELEC  (CL_HTON64(((uint64_t)1)<<20))
#define  IB_PR_COMPMASK_PKTLIFETIME       (CL_HTON64(((uint64_t)1)<<21))

#define  IB_PR_COMPMASK_SERVICEID (IB_PR_COMPMASK_SERVICEID_MSB | \
				   IB_PR_COMPMASK_SERVICEID_LSB)

/* Link Record Component Masks */
#define IB_LR_COMPMASK_FROM_LID           (CL_HTON64(((uint64_t)1)<<0))
#define IB_LR_COMPMASK_FROM_PORT          (CL_HTON64(((uint64_t)1)<<1))
#define IB_LR_COMPMASK_TO_PORT            (CL_HTON64(((uint64_t)1)<<2))
#define IB_LR_COMPMASK_TO_LID             (CL_HTON64(((uint64_t)1)<<3))

/* VL Arbitration Record Masks */
#define IB_VLA_COMPMASK_LID               (CL_HTON64(((uint64_t)1)<<0))
#define IB_VLA_COMPMASK_OUT_PORT          (CL_HTON64(((uint64_t)1)<<1))
#define IB_VLA_COMPMASK_BLOCK             (CL_HTON64(((uint64_t)1)<<2))

/* SLtoVL Mapping Record Masks */
#define IB_SLVL_COMPMASK_LID              (CL_HTON64(((uint64_t)1)<<0))
#define IB_SLVL_COMPMASK_IN_PORT          (CL_HTON64(((uint64_t)1)<<1))
#define IB_SLVL_COMPMASK_OUT_PORT         (CL_HTON64(((uint64_t)1)<<2))

/* P_Key Table Record Masks */
#define IB_PKEY_COMPMASK_LID              (CL_HTON64(((uint64_t)1)<<0))
#define IB_PKEY_COMPMASK_BLOCK            (CL_HTON64(((uint64_t)1)<<1))
#define IB_PKEY_COMPMASK_PORT             (CL_HTON64(((uint64_t)1)<<2))

/* Switch Info Record Masks */
#define IB_SWIR_COMPMASK_LID		  (CL_HTON64(((uint64_t)1)<<0))
#define IB_SWIR_COMPMASK_RESERVED1	  (CL_HTON64(((uint64_t)1)<<1))

/* LFT Record Masks */
#define IB_LFTR_COMPMASK_LID              (CL_HTON64(((uint64_t)1)<<0))
#define IB_LFTR_COMPMASK_BLOCK            (CL_HTON64(((uint64_t)1)<<1))

/* MFT Record Masks */
#define IB_MFTR_COMPMASK_LID		  (CL_HTON64(((uint64_t)1)<<0))
#define IB_MFTR_COMPMASK_POSITION	  (CL_HTON64(((uint64_t)1)<<1))
#define IB_MFTR_COMPMASK_RESERVED1	  (CL_HTON64(((uint64_t)1)<<2))
#define IB_MFTR_COMPMASK_BLOCK		  (CL_HTON64(((uint64_t)1)<<3))
#define IB_MFTR_COMPMASK_RESERVED2	  (CL_HTON64(((uint64_t)1)<<4))

/* NodeInfo Record Masks */
#define IB_NR_COMPMASK_LID                (CL_HTON64(((uint64_t)1)<<0))
#define IB_NR_COMPMASK_RESERVED1          (CL_HTON64(((uint64_t)1)<<1))
#define IB_NR_COMPMASK_BASEVERSION        (CL_HTON64(((uint64_t)1)<<2))
#define IB_NR_COMPMASK_CLASSVERSION       (CL_HTON64(((uint64_t)1)<<3))
#define IB_NR_COMPMASK_NODETYPE           (CL_HTON64(((uint64_t)1)<<4))
#define IB_NR_COMPMASK_NUMPORTS           (CL_HTON64(((uint64_t)1)<<5))
#define IB_NR_COMPMASK_SYSIMAGEGUID       (CL_HTON64(((uint64_t)1)<<6))
#define IB_NR_COMPMASK_NODEGUID           (CL_HTON64(((uint64_t)1)<<7))
#define IB_NR_COMPMASK_PORTGUID           (CL_HTON64(((uint64_t)1)<<8))
#define IB_NR_COMPMASK_PARTCAP            (CL_HTON64(((uint64_t)1)<<9))
#define IB_NR_COMPMASK_DEVID              (CL_HTON64(((uint64_t)1)<<10))
#define IB_NR_COMPMASK_REV                (CL_HTON64(((uint64_t)1)<<11))
#define IB_NR_COMPMASK_PORTNUM            (CL_HTON64(((uint64_t)1)<<12))
#define IB_NR_COMPMASK_VENDID             (CL_HTON64(((uint64_t)1)<<13))
#define IB_NR_COMPMASK_NODEDESC           (CL_HTON64(((uint64_t)1)<<14))

/* Service Record Component Masks Sec 15.2.5.14 Ver 1.1*/
#define IB_SR_COMPMASK_SID                (CL_HTON64(((uint64_t)1)<<0))
#define IB_SR_COMPMASK_SGID               (CL_HTON64(((uint64_t)1)<<1))
#define IB_SR_COMPMASK_SPKEY              (CL_HTON64(((uint64_t)1)<<2))
#define IB_SR_COMPMASK_RES1               (CL_HTON64(((uint64_t)1)<<3))
#define IB_SR_COMPMASK_SLEASE             (CL_HTON64(((uint64_t)1)<<4))
#define IB_SR_COMPMASK_SKEY               (CL_HTON64(((uint64_t)1)<<5))
#define IB_SR_COMPMASK_SNAME              (CL_HTON64(((uint64_t)1)<<6))
#define IB_SR_COMPMASK_SDATA8_0           (CL_HTON64(((uint64_t)1)<<7))
#define IB_SR_COMPMASK_SDATA8_1           (CL_HTON64(((uint64_t)1)<<8))
#define IB_SR_COMPMASK_SDATA8_2           (CL_HTON64(((uint64_t)1)<<9))
#define IB_SR_COMPMASK_SDATA8_3           (CL_HTON64(((uint64_t)1)<<10))
#define IB_SR_COMPMASK_SDATA8_4           (CL_HTON64(((uint64_t)1)<<11))
#define IB_SR_COMPMASK_SDATA8_5           (CL_HTON64(((uint64_t)1)<<12))
#define IB_SR_COMPMASK_SDATA8_6           (CL_HTON64(((uint64_t)1)<<13))
#define IB_SR_COMPMASK_SDATA8_7           (CL_HTON64(((uint64_t)1)<<14))
#define IB_SR_COMPMASK_SDATA8_8           (CL_HTON64(((uint64_t)1)<<15))
#define IB_SR_COMPMASK_SDATA8_9           (CL_HTON64(((uint64_t)1)<<16))
#define IB_SR_COMPMASK_SDATA8_10       (CL_HTON64(((uint64_t)1)<<17))
#define IB_SR_COMPMASK_SDATA8_11       (CL_HTON64(((uint64_t)1)<<18))
#define IB_SR_COMPMASK_SDATA8_12       (CL_HTON64(((uint64_t)1)<<19))
#define IB_SR_COMPMASK_SDATA8_13       (CL_HTON64(((uint64_t)1)<<20))
#define IB_SR_COMPMASK_SDATA8_14       (CL_HTON64(((uint64_t)1)<<21))
#define IB_SR_COMPMASK_SDATA8_15       (CL_HTON64(((uint64_t)1)<<22))
#define IB_SR_COMPMASK_SDATA16_0       (CL_HTON64(((uint64_t)1)<<23))
#define IB_SR_COMPMASK_SDATA16_1       (CL_HTON64(((uint64_t)1)<<24))
#define IB_SR_COMPMASK_SDATA16_2       (CL_HTON64(((uint64_t)1)<<25))
#define IB_SR_COMPMASK_SDATA16_3       (CL_HTON64(((uint64_t)1)<<26))
#define IB_SR_COMPMASK_SDATA16_4       (CL_HTON64(((uint64_t)1)<<27))
#define IB_SR_COMPMASK_SDATA16_5       (CL_HTON64(((uint64_t)1)<<28))
#define IB_SR_COMPMASK_SDATA16_6       (CL_HTON64(((uint64_t)1)<<29))
#define IB_SR_COMPMASK_SDATA16_7       (CL_HTON64(((uint64_t)1)<<30))
#define IB_SR_COMPMASK_SDATA32_0       (CL_HTON64(((uint64_t)1)<<31))
#define IB_SR_COMPMASK_SDATA32_1       (CL_HTON64(((uint64_t)1)<<32))
#define IB_SR_COMPMASK_SDATA32_2       (CL_HTON64(((uint64_t)1)<<33))
#define IB_SR_COMPMASK_SDATA32_3       (CL_HTON64(((uint64_t)1)<<34))
#define IB_SR_COMPMASK_SDATA64_0       (CL_HTON64(((uint64_t)1)<<35))
#define IB_SR_COMPMASK_SDATA64_1       (CL_HTON64(((uint64_t)1)<<36))

/* Port Info Record Component Masks */
#define IB_PIR_COMPMASK_LID              (CL_HTON64(((uint64_t)1)<<0))
#define IB_PIR_COMPMASK_PORTNUM          (CL_HTON64(((uint64_t)1)<<1))
#define IB_PIR_COMPMASK_OPTIONS		 (CL_HTON64(((uint64_t)1)<<2))
#define IB_PIR_COMPMASK_MKEY             (CL_HTON64(((uint64_t)1)<<3))
#define IB_PIR_COMPMASK_GIDPRE           (CL_HTON64(((uint64_t)1)<<4))
#define IB_PIR_COMPMASK_BASELID          (CL_HTON64(((uint64_t)1)<<5))
#define IB_PIR_COMPMASK_SMLID            (CL_HTON64(((uint64_t)1)<<6))
#define IB_PIR_COMPMASK_CAPMASK          (CL_HTON64(((uint64_t)1)<<7))
#define IB_PIR_COMPMASK_DIAGCODE         (CL_HTON64(((uint64_t)1)<<8))
#define IB_PIR_COMPMASK_MKEYLEASEPRD     (CL_HTON64(((uint64_t)1)<<9))
#define IB_PIR_COMPMASK_LOCALPORTNUM     (CL_HTON64(((uint64_t)1)<<10))
#define IB_PIR_COMPMASK_LINKWIDTHENABLED (CL_HTON64(((uint64_t)1)<<11))
#define IB_PIR_COMPMASK_LNKWIDTHSUPPORT  (CL_HTON64(((uint64_t)1)<<12))
#define IB_PIR_COMPMASK_LNKWIDTHACTIVE   (CL_HTON64(((uint64_t)1)<<13))
#define IB_PIR_COMPMASK_LNKSPEEDSUPPORT  (CL_HTON64(((uint64_t)1)<<14))
#define IB_PIR_COMPMASK_PORTSTATE        (CL_HTON64(((uint64_t)1)<<15))
#define IB_PIR_COMPMASK_PORTPHYSTATE     (CL_HTON64(((uint64_t)1)<<16))
#define IB_PIR_COMPMASK_LINKDWNDFLTSTATE (CL_HTON64(((uint64_t)1)<<17))
#define IB_PIR_COMPMASK_MKEYPROTBITS     (CL_HTON64(((uint64_t)1)<<18))
#define IB_PIR_COMPMASK_RESV2            (CL_HTON64(((uint64_t)1)<<19))
#define IB_PIR_COMPMASK_LMC              (CL_HTON64(((uint64_t)1)<<20))
#define IB_PIR_COMPMASK_LINKSPEEDACTIVE  (CL_HTON64(((uint64_t)1)<<21))
#define IB_PIR_COMPMASK_LINKSPEEDENABLE  (CL_HTON64(((uint64_t)1)<<22))
#define IB_PIR_COMPMASK_NEIGHBORMTU      (CL_HTON64(((uint64_t)1)<<23))
#define IB_PIR_COMPMASK_MASTERSMSL       (CL_HTON64(((uint64_t)1)<<24))
#define IB_PIR_COMPMASK_VLCAP            (CL_HTON64(((uint64_t)1)<<25))
#define IB_PIR_COMPMASK_INITTYPE         (CL_HTON64(((uint64_t)1)<<26))
#define IB_PIR_COMPMASK_VLHIGHLIMIT      (CL_HTON64(((uint64_t)1)<<27))
#define IB_PIR_COMPMASK_VLARBHIGHCAP     (CL_HTON64(((uint64_t)1)<<28))
#define IB_PIR_COMPMASK_VLARBLOWCAP      (CL_HTON64(((uint64_t)1)<<29))
#define IB_PIR_COMPMASK_INITTYPEREPLY    (CL_HTON64(((uint64_t)1)<<30))
#define IB_PIR_COMPMASK_MTUCAP           (CL_HTON64(((uint64_t)1)<<31))
#define IB_PIR_COMPMASK_VLSTALLCNT       (CL_HTON64(((uint64_t)1)<<32))
#define IB_PIR_COMPMASK_HOQLIFE          (CL_HTON64(((uint64_t)1)<<33))
#define IB_PIR_COMPMASK_OPVLS            (CL_HTON64(((uint64_t)1)<<34))
#define IB_PIR_COMPMASK_PARENFIN         (CL_HTON64(((uint64_t)1)<<35))
#define IB_PIR_COMPMASK_PARENFOUT        (CL_HTON64(((uint64_t)1)<<36))
#define IB_PIR_COMPMASK_FILTERRAWIN      (CL_HTON64(((uint64_t)1)<<37))
#define IB_PIR_COMPMASK_FILTERRAWOUT     (CL_HTON64(((uint64_t)1)<<38))
#define IB_PIR_COMPMASK_MKEYVIO          (CL_HTON64(((uint64_t)1)<<39))
#define IB_PIR_COMPMASK_PKEYVIO          (CL_HTON64(((uint64_t)1)<<40))
#define IB_PIR_COMPMASK_QKEYVIO          (CL_HTON64(((uint64_t)1)<<41))
#define IB_PIR_COMPMASK_GUIDCAP          (CL_HTON64(((uint64_t)1)<<42))
#define IB_PIR_COMPMASK_CLIENTREREG	 (CL_HTON64(((uint64_t)1)<<43))
#define IB_PIR_COMPMASK_RESV3            (CL_HTON64(((uint64_t)1)<<44))
#define IB_PIR_COMPMASK_SUBNTO           (CL_HTON64(((uint64_t)1)<<45))
#define IB_PIR_COMPMASK_RESV4            (CL_HTON64(((uint64_t)1)<<46))
#define IB_PIR_COMPMASK_RESPTIME         (CL_HTON64(((uint64_t)1)<<47))
#define IB_PIR_COMPMASK_LOCALPHYERR      (CL_HTON64(((uint64_t)1)<<48))
#define IB_PIR_COMPMASK_OVERRUNERR       (CL_HTON64(((uint64_t)1)<<49))
#define IB_PIR_COMPMASK_MAXCREDHINT	 (CL_HTON64(((uint64_t)1)<<50))
#define IB_PIR_COMPMASK_RESV5		 (CL_HTON64(((uint64_t)1)<<51))
#define IB_PIR_COMPMASK_LINKRTLAT	 (CL_HTON64(((uint64_t)1)<<52))
#define IB_PIR_COMPMASK_CAPMASK2	 (CL_HTON64(((uint64_t)1)<<53))
#define IB_PIR_COMPMASK_LINKSPDEXTACT	 (CL_HTON64(((uint64_t)1)<<54))
#define IB_PIR_COMPMASK_LINKSPDEXTSUPP	 (CL_HTON64(((uint64_t)1)<<55))
#define IB_PIR_COMPMASK_RESV7		 (CL_HTON64(((uint64_t)1)<<56))
#define IB_PIR_COMPMASK_LINKSPDEXTENAB	 (CL_HTON64(((uint64_t)1)<<57))

/* Multicast Member Record Component Masks */
#define IB_MCR_COMPMASK_GID         (CL_HTON64(((uint64_t)1)<<0))
#define IB_MCR_COMPMASK_MGID        (CL_HTON64(((uint64_t)1)<<0))
#define IB_MCR_COMPMASK_PORT_GID    (CL_HTON64(((uint64_t)1)<<1))
#define IB_MCR_COMPMASK_QKEY        (CL_HTON64(((uint64_t)1)<<2))
#define IB_MCR_COMPMASK_MLID        (CL_HTON64(((uint64_t)1)<<3))
#define IB_MCR_COMPMASK_MTU_SEL     (CL_HTON64(((uint64_t)1)<<4))
#define IB_MCR_COMPMASK_MTU         (CL_HTON64(((uint64_t)1)<<5))
#define IB_MCR_COMPMASK_TCLASS      (CL_HTON64(((uint64_t)1)<<6))
#define IB_MCR_COMPMASK_PKEY        (CL_HTON64(((uint64_t)1)<<7))
#define IB_MCR_COMPMASK_RATE_SEL    (CL_HTON64(((uint64_t)1)<<8))
#define IB_MCR_COMPMASK_RATE        (CL_HTON64(((uint64_t)1)<<9))
#define IB_MCR_COMPMASK_LIFE_SEL    (CL_HTON64(((uint64_t)1)<<10))
#define IB_MCR_COMPMASK_LIFE        (CL_HTON64(((uint64_t)1)<<11))
#define IB_MCR_COMPMASK_SL          (CL_HTON64(((uint64_t)1)<<12))
#define IB_MCR_COMPMASK_FLOW        (CL_HTON64(((uint64_t)1)<<13))
#define IB_MCR_COMPMASK_HOP         (CL_HTON64(((uint64_t)1)<<14))
#define IB_MCR_COMPMASK_SCOPE       (CL_HTON64(((uint64_t)1)<<15))
#define IB_MCR_COMPMASK_JOIN_STATE  (CL_HTON64(((uint64_t)1)<<16))
#define IB_MCR_COMPMASK_PROXY       (CL_HTON64(((uint64_t)1)<<17))

/* GUID Info Record Component Masks */
#define IB_GIR_COMPMASK_LID		(CL_HTON64(((uint64_t)1)<<0))
#define IB_GIR_COMPMASK_BLOCKNUM	(CL_HTON64(((uint64_t)1)<<1))
#define IB_GIR_COMPMASK_RESV1		(CL_HTON64(((uint64_t)1)<<2))
#define IB_GIR_COMPMASK_RESV2		(CL_HTON64(((uint64_t)1)<<3))
#define IB_GIR_COMPMASK_GID0		(CL_HTON64(((uint64_t)1)<<4))
#define IB_GIR_COMPMASK_GID1		(CL_HTON64(((uint64_t)1)<<5))
#define IB_GIR_COMPMASK_GID2		(CL_HTON64(((uint64_t)1)<<6))
#define IB_GIR_COMPMASK_GID3		(CL_HTON64(((uint64_t)1)<<7))
#define IB_GIR_COMPMASK_GID4		(CL_HTON64(((uint64_t)1)<<8))
#define IB_GIR_COMPMASK_GID5		(CL_HTON64(((uint64_t)1)<<9))
#define IB_GIR_COMPMASK_GID6		(CL_HTON64(((uint64_t)1)<<10))
#define IB_GIR_COMPMASK_GID7		(CL_HTON64(((uint64_t)1)<<11))

/* MultiPath Record Component Masks */
#define IB_MPR_COMPMASK_RAWTRAFFIC	(CL_HTON64(((uint64_t)1)<<0))
#define IB_MPR_COMPMASK_RESV0		(CL_HTON64(((uint64_t)1)<<1))
#define IB_MPR_COMPMASK_FLOWLABEL	(CL_HTON64(((uint64_t)1)<<2))
#define IB_MPR_COMPMASK_HOPLIMIT	(CL_HTON64(((uint64_t)1)<<3))
#define IB_MPR_COMPMASK_TCLASS		(CL_HTON64(((uint64_t)1)<<4))
#define IB_MPR_COMPMASK_REVERSIBLE	(CL_HTON64(((uint64_t)1)<<5))
#define IB_MPR_COMPMASK_NUMBPATH	(CL_HTON64(((uint64_t)1)<<6))
#define IB_MPR_COMPMASK_PKEY		(CL_HTON64(((uint64_t)1)<<7))
#define IB_MPR_COMPMASK_QOS_CLASS	(CL_HTON64(((uint64_t)1)<<8))
#define IB_MPR_COMPMASK_SL		(CL_HTON64(((uint64_t)1)<<9))
#define IB_MPR_COMPMASK_MTUSELEC	(CL_HTON64(((uint64_t)1)<<10))
#define IB_MPR_COMPMASK_MTU		(CL_HTON64(((uint64_t)1)<<11))
#define IB_MPR_COMPMASK_RATESELEC	(CL_HTON64(((uint64_t)1)<<12))
#define IB_MPR_COMPMASK_RATE		(CL_HTON64(((uint64_t)1)<<13))
#define IB_MPR_COMPMASK_PKTLIFETIMESELEC (CL_HTON64(((uint64_t)1)<<14))
#define IB_MPR_COMPMASK_PKTLIFETIME	(CL_HTON64(((uint64_t)1)<<15))
#define IB_MPR_COMPMASK_SERVICEID_MSB	(CL_HTON64(((uint64_t)1)<<16))
#define IB_MPR_COMPMASK_INDEPSELEC	(CL_HTON64(((uint64_t)1)<<17))
#define IB_MPR_COMPMASK_RESV3		(CL_HTON64(((uint64_t)1)<<18))
#define IB_MPR_COMPMASK_SGIDCOUNT	(CL_HTON64(((uint64_t)1)<<19))
#define IB_MPR_COMPMASK_DGIDCOUNT	(CL_HTON64(((uint64_t)1)<<20))
#define IB_MPR_COMPMASK_SERVICEID_LSB	(CL_HTON64(((uint64_t)1)<<21))

#define IB_MPR_COMPMASK_SERVICEID (IB_MPR_COMPMASK_SERVICEID_MSB | \
				   IB_MPR_COMPMASK_SERVICEID_LSB)

/* SMInfo Record Component Masks */
#define IB_SMIR_COMPMASK_LID		(CL_HTON64(((uint64_t)1)<<0))
#define IB_SMIR_COMPMASK_RESV0		(CL_HTON64(((uint64_t)1)<<1))
#define IB_SMIR_COMPMASK_GUID		(CL_HTON64(((uint64_t)1)<<2))
#define IB_SMIR_COMPMASK_SMKEY		(CL_HTON64(((uint64_t)1)<<3))
#define IB_SMIR_COMPMASK_ACTCOUNT	(CL_HTON64(((uint64_t)1)<<4))
#define IB_SMIR_COMPMASK_PRIORITY	(CL_HTON64(((uint64_t)1)<<5))
#define IB_SMIR_COMPMASK_SMSTATE	(CL_HTON64(((uint64_t)1)<<6))

/* InformInfo Record Component Masks */
#define IB_IIR_COMPMASK_SUBSCRIBERGID	(CL_HTON64(((uint64_t)1)<<0))
#define IB_IIR_COMPMASK_ENUM		(CL_HTON64(((uint64_t)1)<<1))
#define IB_IIR_COMPMASK_RESV0		(CL_HTON64(((uint64_t)1)<<2))
#define IB_IIR_COMPMASK_GID		(CL_HTON64(((uint64_t)1)<<3))
#define IB_IIR_COMPMASK_LIDRANGEBEGIN	(CL_HTON64(((uint64_t)1)<<4))
#define IB_IIR_COMPMASK_LIDRANGEEND	(CL_HTON64(((uint64_t)1)<<5))
#define IB_IIR_COMPMASK_RESV1		(CL_HTON64(((uint64_t)1)<<6))
#define IB_IIR_COMPMASK_ISGENERIC	(CL_HTON64(((uint64_t)1)<<7))
#define IB_IIR_COMPMASK_SUBSCRIBE	(CL_HTON64(((uint64_t)1)<<8))
#define IB_IIR_COMPMASK_TYPE		(CL_HTON64(((uint64_t)1)<<9))
#define IB_IIR_COMPMASK_TRAPNUMB	(CL_HTON64(((uint64_t)1)<<10))
#define IB_IIR_COMPMASK_DEVICEID	(CL_HTON64(((uint64_t)1)<<10))
#define IB_IIR_COMPMASK_QPN		(CL_HTON64(((uint64_t)1)<<11))
#define IB_IIR_COMPMASK_RESV2		(CL_HTON64(((uint64_t)1)<<12))
#define IB_IIR_COMPMASK_RESPTIME	(CL_HTON64(((uint64_t)1)<<13))
#define IB_IIR_COMPMASK_RESV3		(CL_HTON64(((uint64_t)1)<<14))
#define IB_IIR_COMPMASK_PRODTYPE	(CL_HTON64(((uint64_t)1)<<15))
#define IB_IIR_COMPMASK_VENDID		(CL_HTON64(((uint64_t)1)<<15))

/****f* IBA Base: Types/ib_path_rec_init_local
* NAME
*	ib_path_rec_init_local
*
* DESCRIPTION
*	Initializes a subnet local path record.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_path_rec_init_local(IN ib_path_rec_t * const p_rec,
		       IN ib_gid_t * const p_dgid,
		       IN ib_gid_t * const p_sgid,
		       IN ib_net16_t dlid,
		       IN ib_net16_t slid,
		       IN uint8_t num_path,
		       IN ib_net16_t pkey,
		       IN uint8_t sl,
		       IN uint16_t qos_class,
		       IN uint8_t mtu_selector,
		       IN uint8_t mtu,
		       IN uint8_t rate_selector,
		       IN uint8_t rate,
		       IN uint8_t pkt_life_selector,
		       IN uint8_t pkt_life, IN uint8_t preference)
{
	p_rec->dgid = *p_dgid;
	p_rec->sgid = *p_sgid;
	p_rec->dlid = dlid;
	p_rec->slid = slid;
	p_rec->num_path = num_path;
	p_rec->pkey = pkey;
	p_rec->qos_class_sl = cl_hton16((sl & IB_PATH_REC_SL_MASK) |
					(qos_class << 4));
	p_rec->mtu = (uint8_t) ((mtu & IB_PATH_REC_BASE_MASK) |
				(uint8_t) (mtu_selector << 6));
	p_rec->rate = (uint8_t) ((rate & IB_PATH_REC_BASE_MASK) |
				 (uint8_t) (rate_selector << 6));
	p_rec->pkt_life = (uint8_t) ((pkt_life & IB_PATH_REC_BASE_MASK) |
				     (uint8_t) (pkt_life_selector << 6));
	p_rec->preference = preference;

	/* Clear global routing fields for local path records */
	p_rec->hop_flow_raw = 0;
	p_rec->tclass = 0;
	p_rec->service_id = 0;

	memset(p_rec->resv2, 0, sizeof(p_rec->resv2));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
*	dgid
*		[in] GID of destination port.
*
*	sgid
*		[in] GID of source port.
*
*	dlid
*		[in] LID of destination port.
*
*	slid
*		[in] LID of source port.
*
*	num_path
*		[in] Reversible path - 1 bit to say if path is reversible.
*		num_path [6:0] In queries, maximum number of paths to return.
*		In responses, undefined.
*
*	pkey
*		[in] Partition key (P_Key) to use on this path.
*
*	qos_class
*		[in] QoS class to use on this path.  Lower 12-bits are valid.
*
*	sl
*		[in] Service level to use on this path.  Lower 4-bits are valid.
*
*	mtu_selector
*		[in] Encoded MTU selector value to use on this path
*
*	mtu
*		[in] Encoded MTU to use on this path
*
*	rate_selector
*		[in] Encoded rate selector value to use on this path.
*
*	rate
*		[in] Encoded rate to use on this path.
*
*	pkt_life_selector
*		[in] Encoded Packet selector value lifetime for this path.
*
*	pkt_life
*		[in] Encoded Packet lifetime for this path.
*
*	preference
*		[in] Indicates the relative merit of this path versus other path
*		records returned from the SA.  Lower numbers are better.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	ib_gid_t
*********/

/****f* IBA Base: Types/ib_path_rec_num_path
* NAME
*	ib_path_rec_num_path
*
* DESCRIPTION
*	Get max number of paths to return.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_num_path(IN const ib_path_rec_t * const p_rec)
{
	return (p_rec->num_path & 0x7F);
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Maximum number of paths to return for each unique SGID_DGID combination.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_set_sl
* NAME
*	ib_path_rec_set_sl
*
* DESCRIPTION
*	Set path service level.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_path_rec_set_sl(IN ib_path_rec_t * const p_rec, IN const uint8_t sl)
{
	p_rec->qos_class_sl =
	    (p_rec->qos_class_sl & CL_HTON16(IB_PATH_REC_QOS_CLASS_MASK)) |
	    cl_hton16(sl & IB_PATH_REC_SL_MASK);
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
*	sl
*		[in] Service level to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_sl
* NAME
*	ib_path_rec_sl
*
* DESCRIPTION
*	Get path service level.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_sl(IN const ib_path_rec_t * const p_rec)
{
	return (uint8_t)(cl_ntoh16(p_rec->qos_class_sl) & IB_PATH_REC_SL_MASK);
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	SL.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_set_qos_class
* NAME
*	ib_path_rec_set_qos_class
*
* DESCRIPTION
*	Set path QoS class.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_path_rec_set_qos_class(IN ib_path_rec_t * const p_rec,
			  IN const uint16_t qos_class)
{
	p_rec->qos_class_sl =
	    (p_rec->qos_class_sl & CL_HTON16(IB_PATH_REC_SL_MASK)) |
	    cl_hton16(qos_class << 4);
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
*	qos_class
*		[in] QoS class to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_qos_class
* NAME
*	ib_path_rec_qos_class
*
* DESCRIPTION
*	Get QoS class.
*
* SYNOPSIS
*/
static inline uint16_t OSM_API
ib_path_rec_qos_class(IN const ib_path_rec_t * const p_rec)
{
	return (cl_ntoh16(p_rec->qos_class_sl) >> 4);
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	QoS class of the path record.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_mtu
* NAME
*	ib_path_rec_mtu
*
* DESCRIPTION
*	Get encoded path MTU.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_mtu(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->mtu & IB_PATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path MTU.
*		1: 256
*		2: 512
*		3: 1024
*		4: 2048
*		5: 4096
*		others: reserved
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_mtu_sel
* NAME
*	ib_path_rec_mtu_sel
*
* DESCRIPTION
*	Get encoded path MTU selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_mtu_sel(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) ((p_rec->mtu & IB_PATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path MTU selector value (for queries).
*		0: greater than MTU specified
*		1: less than MTU specified
*		2: exactly the MTU specified
*		3: largest MTU available
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_rate
* NAME
*	ib_path_rec_rate
*
* DESCRIPTION
*	Get encoded path rate.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_rate(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->rate & IB_PATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path rate.
*		2: 2.5 Gb/sec.
*		3: 10 Gb/sec.
*		4: 30 Gb/sec.
*		5: 5 Gb/sec.
*		6: 20 Gb/sec.
*		7: 40 Gb/sec.
*		8: 60 Gb/sec.
*		9: 80 Gb/sec.
*		10: 120 Gb/sec.
*		11: 14 Gb/sec.
*		12: 56 Gb/sec.
*		13: 112 Gb/sec.
*		14: 168 Gb/sec.
*		15: 25 Gb/sec.
*		16: 100 Gb/sec.
*		17: 200 Gb/sec.
*		18: 300 Gb/sec.
*		others: reserved
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_rate_sel
* NAME
*	ib_path_rec_rate_sel
*
* DESCRIPTION
*	Get encoded path rate selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_rate_sel(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) ((p_rec->rate & IB_PATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path rate selector value (for queries).
*		0: greater than rate specified
*		1: less than rate specified
*		2: exactly the rate specified
*		3: largest rate available
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_pkt_life
* NAME
*	ib_path_rec_pkt_life
*
* DESCRIPTION
*	Get encoded path pkt_life.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_pkt_life(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->pkt_life & IB_PATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path pkt_life = 4.096 usec * 2 ** PacketLifeTime.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_pkt_life_sel
* NAME
*	ib_path_rec_pkt_life_sel
*
* DESCRIPTION
*	Get encoded path pkt_lifetime selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_pkt_life_sel(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) ((p_rec->pkt_life & IB_PATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Encoded path pkt_lifetime selector value (for queries).
*		0: greater than rate specified
*		1: less than rate specified
*		2: exactly the rate specified
*		3: smallest packet lifetime available
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_flow_lbl
* NAME
*	ib_path_rec_flow_lbl
*
* DESCRIPTION
*	Get flow label.
*
* SYNOPSIS
*/
static inline uint32_t OSM_API
ib_path_rec_flow_lbl(IN const ib_path_rec_t * const p_rec)
{
	return (((cl_ntoh32(p_rec->hop_flow_raw) >> 8) & 0x000FFFFF));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Flow label of the path record.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****f* IBA Base: Types/ib_path_rec_hop_limit
* NAME
*	ib_path_rec_hop_limit
*
* DESCRIPTION
*	Get hop limit.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_rec_hop_limit(IN const ib_path_rec_t * const p_rec)
{
	return ((uint8_t) (cl_ntoh32(p_rec->hop_flow_raw) & 0x000000FF));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the path record object.
*
* RETURN VALUES
*	Hop limit of the path record.
*
* NOTES
*
* SEE ALSO
*	ib_path_rec_t
*********/

/****s* IBA Base: Constants/IB_CLASS_CAP_TRAP
* NAME
*	IB_CLASS_CAP_TRAP
*
* DESCRIPTION
*	ClassPortInfo CapabilityMask bits.  This bit will be set
*	if the class supports Trap() MADs (13.4.8.1).
*
* SEE ALSO
*	ib_class_port_info_t, IB_CLASS_CAP_GETSET, IB_CLASS_CAP_CAPMASK2
*
* SOURCE
*/
#define IB_CLASS_CAP_TRAP					0x0001
/*********/

/****s* IBA Base: Constants/IB_CLASS_CAP_GETSET
* NAME
*	IB_CLASS_CAP_GETSET
*
* DESCRIPTION
*	ClassPortInfo CapabilityMask bits.  This bit will be set
*	if the class supports Get(Notice) and Set(Notice) MADs (13.4.8.1).
*
* SEE ALSO
*	ib_class_port_info_t, IB_CLASS_CAP_TRAP, IB_CLASS_CAP_CAPMASK2
*
* SOURCE
*/
#define IB_CLASS_CAP_GETSET					0x0002
/*********/

/****s* IBA Base: Constants/IB_CLASS_CAP_CAPMASK2
* NAME
*	IB_CLASS_CAP_CAPMASK2
*
* DESCRIPTION
*	ClassPortInfo CapabilityMask bits.
*	This bit will be set of the class supports additional class specific
*	capabilities (CapabilityMask2) (13.4.8.1).
*
* SEE ALSO
*	ib_class_port_info_t, IB_CLASS_CAP_TRAP, IB_CLASS_CAP_GETSET
*
* SOURCE
*/
#define IB_CLASS_CAP_CAPMASK2					0x0004
/*********/

/****s* IBA Base: Constants/IB_CLASS_ENH_PORT0_CC_MASK
* NAME
*	IB_CLASS_ENH_PORT0_CC_MASK
*
* DESCRIPTION
*	ClassPortInfo CapabilityMask bits.
*	Switch only: This bit will be set if the EnhancedPort0
*	supports CA Congestion Control (A10.4.3.1).
*
* SEE ALSO
*	ib_class_port_info_t
*
* SOURCE
*/
#define IB_CLASS_ENH_PORT0_CC_MASK			0x0100
/*********/

/****s* IBA Base: Constants/IB_CLASS_RESP_TIME_MASK
* NAME
*	IB_CLASS_RESP_TIME_MASK
*
* DESCRIPTION
*	Mask bits to extract the response time value from the
*	cap_mask2_resp_time field of ib_class_port_info_t.
*
* SEE ALSO
*	ib_class_port_info_t
*
* SOURCE
*/
#define IB_CLASS_RESP_TIME_MASK				0x1F
/*********/

/****s* IBA Base: Constants/IB_CLASS_CAPMASK2_SHIFT
* NAME
*	IB_CLASS_CAPMASK2_SHIFT
*
* DESCRIPTION
*	Number of bits to shift to extract the capability mask2
*	from the cap_mask2_resp_time field of ib_class_port_info_t.
*
* SEE ALSO
*	ib_class_port_info_t
*
* SOURCE
*/
#define IB_CLASS_CAPMASK2_SHIFT				5
/*********/

/****s* IBA Base: Types/ib_class_port_info_t
* NAME
*	ib_class_port_info_t
*
* DESCRIPTION
*	IBA defined ClassPortInfo attribute (13.4.8.1)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_class_port_info {
	uint8_t base_ver;
	uint8_t class_ver;
	ib_net16_t cap_mask;
	ib_net32_t cap_mask2_resp_time;
	ib_gid_t redir_gid;
	ib_net32_t redir_tc_sl_fl;
	ib_net16_t redir_lid;
	ib_net16_t redir_pkey;
	ib_net32_t redir_qp;
	ib_net32_t redir_qkey;
	ib_gid_t trap_gid;
	ib_net32_t trap_tc_sl_fl;
	ib_net16_t trap_lid;
	ib_net16_t trap_pkey;
	ib_net32_t trap_hop_qp;
	ib_net32_t trap_qkey;
} PACK_SUFFIX ib_class_port_info_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	base_ver
*		Maximum supported MAD Base Version.
*
*	class_ver
*		Maximum supported management class version.
*
*	cap_mask
*		Supported capabilities of this management class.
*
*	cap_mask2_resp_time
*		Maximum expected response time and additional
*		supported capabilities of this management class.
*
*	redir_gid
*		GID to use for redirection, or zero
*
*	redir_tc_sl_fl
*		Traffic class, service level and flow label the requester
*		should use if the service is redirected.
*
*	redir_lid
*		LID used for redirection, or zero
*
*	redir_pkey
*		P_Key used for redirection
*
*	redir_qp
*		QP number used for redirection
*
*	redir_qkey
*		Q_Key associated with the redirected QP.  This shall be the
*		well known Q_Key value.
*
*	trap_gid
*		GID value used for trap messages from this service.
*
*	trap_tc_sl_fl
*		Traffic class, service level and flow label used for
*		trap messages originated by this service.
*
*	trap_lid
*		LID used for trap messages, or zero
*
*	trap_pkey
*		P_Key used for trap messages
*
*	trap_hop_qp
*		Hop limit (upper 8 bits) and QP number used for trap messages
*
*	trap_qkey
*		Q_Key associated with the trap messages QP.
*
* SEE ALSO
*	IB_CLASS_CAP_GETSET, IB_CLASS_CAP_TRAP
*
*********/

#define IB_PM_ALL_PORT_SELECT			(CL_HTON16(((uint16_t)1)<<8))
#define IB_PM_EXT_WIDTH_SUPPORTED		(CL_HTON16(((uint16_t)1)<<9))
#define IB_PM_EXT_WIDTH_NOIETF_SUP		(CL_HTON16(((uint16_t)1)<<10))
#define IB_PM_SAMPLES_ONLY_SUP			(CL_HTON16(((uint16_t)1)<<11))
#define IB_PM_PC_XMIT_WAIT_SUP			(CL_HTON16(((uint16_t)1)<<12))
#define IS_PM_INH_LMTD_PKEY_MC_CONSTR_ERR	(CL_HTON16(((uint16_t)1)<<13))
#define IS_PM_RSFEC_COUNTERS_SUP		(CL_HTON16(((uint16_t)1)<<14))
#define IB_PM_IS_QP1_DROP_SUP			(CL_HTON16(((uint16_t)1)<<15))
/* CapabilityMask2 */
#define IB_PM_IS_PM_KEY_SUPPORTED		(CL_HTON32(((uint32_t)1)<<0))
#define IB_PM_IS_ADDL_PORT_CTRS_EXT_SUP		(CL_HTON32(((uint32_t)1)<<1))

/****f* IBA Base: Types/ib_class_set_resp_time_val
* NAME
*	ib_class_set_resp_time_val
*
* DESCRIPTION
*	Set maximum expected response time.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_class_set_resp_time_val(IN ib_class_port_info_t * const p_cpi,
			   IN const uint8_t val)
{
	p_cpi->cap_mask2_resp_time =
	    (p_cpi->cap_mask2_resp_time & CL_HTON32(~IB_CLASS_RESP_TIME_MASK)) |
	    cl_hton32(val & IB_CLASS_RESP_TIME_MASK);
}

/*
* PARAMETERS
*	p_cpi
*		[in] Pointer to the class port info object.
*
*	val
*		[in] Response time value to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_class_port_info_t
*********/

/****f* IBA Base: Types/ib_class_resp_time_val
* NAME
*	ib_class_resp_time_val
*
* DESCRIPTION
*	Get response time value.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_class_resp_time_val(IN ib_class_port_info_t * const p_cpi)
{
	return (uint8_t)(cl_ntoh32(p_cpi->cap_mask2_resp_time) &
			 IB_CLASS_RESP_TIME_MASK);
}

/*
* PARAMETERS
*	p_cpi
*		[in] Pointer to the class port info object.
*
* RETURN VALUES
*	Response time value.
*
* NOTES
*
* SEE ALSO
*	ib_class_port_info_t
*********/

/****f* IBA Base: Types/ib_class_set_cap_mask2
* NAME
*	ib_class_set_cap_mask2
*
* DESCRIPTION
*	Set ClassPortInfo:CapabilityMask2.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_class_set_cap_mask2(IN ib_class_port_info_t * const p_cpi,
		       IN const uint32_t cap_mask2)
{
	p_cpi->cap_mask2_resp_time = (p_cpi->cap_mask2_resp_time &
		CL_HTON32(IB_CLASS_RESP_TIME_MASK)) |
		cl_hton32(cap_mask2 << IB_CLASS_CAPMASK2_SHIFT);
}

/*
* PARAMETERS
*	p_cpi
*		[in] Pointer to the class port info object.
*
*	cap_mask2
*		[in] CapabilityMask2 value to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_class_port_info_t
*********/

/****f* IBA Base: Types/ib_class_cap_mask2
* NAME
*	ib_class_cap_mask2
*
* DESCRIPTION
*	Get ClassPortInfo:CapabilityMask2.
*
* SYNOPSIS
*/
static inline uint32_t OSM_API
ib_class_cap_mask2(IN const ib_class_port_info_t * const p_cpi)
{
	return (cl_ntoh32(p_cpi->cap_mask2_resp_time) >> IB_CLASS_CAPMASK2_SHIFT);
}

/*
* PARAMETERS
*	p_cpi
*		[in] Pointer to the class port info object.
*
* RETURN VALUES
*	CapabilityMask2 of the ClassPortInfo.
*
* NOTES
*
* SEE ALSO
*	ib_class_port_info_t
*********/

/****s* IBA Base: Types/ib_sm_info_t
* NAME
*	ib_sm_info_t
*
* DESCRIPTION
*	SMInfo structure (14.2.5.13).
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_sm_info {
	ib_net64_t guid;
	ib_net64_t sm_key;
	ib_net32_t act_count;
	uint8_t pri_state;
} PACK_SUFFIX ib_sm_info_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	guid
*		Port GUID for this SM.
*
*	sm_key
*		SM_Key of this SM.
*
*	act_count
*		Activity counter used as a heartbeat.
*
*	pri_state
*		Priority and State information
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_sminfo_get_priority
* NAME
*	ib_sminfo_get_priority
*
* DESCRIPTION
*	Returns the priority value.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_sminfo_get_priority(IN const ib_sm_info_t * const p_smi)
{
	return ((uint8_t) ((p_smi->pri_state & 0xF0) >> 4));
}

/*
* PARAMETERS
*	p_smi
*		[in] Pointer to the SMInfo Attribute.
*
* RETURN VALUES
*	Returns the priority value.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_sminfo_get_state
* NAME
*	ib_sminfo_get_state
*
* DESCRIPTION
*	Returns the state value.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_sminfo_get_state(IN const ib_sm_info_t * const p_smi)
{
	return ((uint8_t) (p_smi->pri_state & 0x0F));
}

/*
* PARAMETERS
*	p_smi
*		[in] Pointer to the SMInfo Attribute.
*
* RETURN VALUES
*	Returns the state value.
*
* NOTES
*
* SEE ALSO
*********/

/****s* IBA Base: Types/ib_mad_t
* NAME
*	ib_mad_t
*
* DESCRIPTION
*	IBA defined MAD header (13.4.3)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_mad {
	uint8_t base_ver;
	uint8_t mgmt_class;
	uint8_t class_ver;
	uint8_t method;
	ib_net16_t status;
	ib_net16_t class_spec;
	ib_net64_t trans_id;
	ib_net16_t attr_id;
	ib_net16_t resv;
	ib_net32_t attr_mod;
} PACK_SUFFIX ib_mad_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	base_ver
*		MAD base format.
*
*	mgmt_class
*		Class of operation.
*
*	class_ver
*		Version of MAD class-specific format.
*
*	method
*		Method to perform, including 'R' bit.
*
*	status
*		Status of operation.
*
*	class_spec
*		Reserved for subnet management.
*
*	trans_id
*		Transaction ID.
*
*	attr_id
*		Attribute ID.
*
*	resv
*		Reserved field.
*
*	attr_mod
*		Attribute modifier.
*
* SEE ALSO
*********/

/****s* IBA Base: Types/ib_rmpp_mad_t
* NAME
*	ib_rmpp_mad_t
*
* DESCRIPTION
*	IBA defined MAD RMPP header (13.6.2.1)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_rmpp_mad {
	ib_mad_t common_hdr;
	uint8_t rmpp_version;
	uint8_t rmpp_type;
	uint8_t rmpp_flags;
	uint8_t rmpp_status;
	ib_net32_t seg_num;
	ib_net32_t paylen_newwin;
} PACK_SUFFIX ib_rmpp_mad_t;
#include <complib/cl_packoff.h>
/*
* SEE ALSO
*	ib_mad_t
*********/

/****f* IBA Base: Types/ib_mad_init_new
* NAME
*	ib_mad_init_new
*
* DESCRIPTION
*	Initializes a MAD common header.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_mad_init_new(IN ib_mad_t * const p_mad,
		IN const uint8_t mgmt_class,
		IN const uint8_t class_ver,
		IN const uint8_t method,
		IN const ib_net64_t trans_id,
		IN const ib_net16_t attr_id, IN const ib_net32_t attr_mod)
{
	CL_ASSERT(p_mad);
	p_mad->base_ver = 1;
	p_mad->mgmt_class = mgmt_class;
	p_mad->class_ver = class_ver;
	p_mad->method = method;
	p_mad->status = 0;
	p_mad->class_spec = 0;
	p_mad->trans_id = trans_id;
	p_mad->attr_id = attr_id;
	p_mad->resv = 0;
	p_mad->attr_mod = attr_mod;
}

/*
* PARAMETERS
*	p_mad
*		[in] Pointer to the MAD common header.
*
*	mgmt_class
*		[in] Class of operation.
*
*	class_ver
*		[in] Version of MAD class-specific format.
*
*	method
*		[in] Method to perform, including 'R' bit.
*
*	trans_Id
*		[in] Transaction ID.
*
*	attr_id
*		[in] Attribute ID.
*
*	attr_mod
*		[in] Attribute modifier.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*	ib_mad_t
*********/

/****f* IBA Base: Types/ib_mad_init_response
* NAME
*	ib_mad_init_response
*
* DESCRIPTION
*	Initializes a MAD common header as a response.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_mad_init_response(IN const ib_mad_t * const p_req_mad,
		     IN ib_mad_t * const p_mad, IN const ib_net16_t status)
{
	CL_ASSERT(p_req_mad);
	CL_ASSERT(p_mad);
	*p_mad = *p_req_mad;
	p_mad->status = status;
	if (p_mad->method == IB_MAD_METHOD_SET)
		p_mad->method = IB_MAD_METHOD_GET;
	p_mad->method |= IB_MAD_METHOD_RESP_MASK;
}

/*
* PARAMETERS
*	p_req_mad
*		[in] Pointer to the MAD common header in the original request MAD.
*
*	p_mad
*		[in] Pointer to the MAD common header to initialize.
*
*	status
*		[in] MAD Status value to return;
*
* RETURN VALUES
*	None.
*
* NOTES
*	p_req_mad and p_mad may point to the same MAD.
*
* SEE ALSO
*	ib_mad_t
*********/

/****f* IBA Base: Types/ib_mad_is_response
* NAME
*	ib_mad_is_response
*
* DESCRIPTION
*	Returns TRUE if the MAD is a response ('R' bit set)
*	or if the MAD is a TRAP REPRESS,
*	FALSE otherwise.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_mad_is_response(IN const ib_mad_t * const p_mad)
{
	CL_ASSERT(p_mad);
	return (p_mad->method & IB_MAD_METHOD_RESP_MASK ||
		p_mad->method == IB_MAD_METHOD_TRAP_REPRESS);
}

/*
* PARAMETERS
*	p_mad
*		[in] Pointer to the MAD.
*
* RETURN VALUES
*	Returns TRUE if the MAD is a response ('R' bit set),
*	FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	ib_mad_t
*********/

#define IB_RMPP_TYPE_DATA		1
#define IB_RMPP_TYPE_ACK		2
#define IB_RMPP_TYPE_STOP		3
#define IB_RMPP_TYPE_ABORT		4

#define IB_RMPP_NO_RESP_TIME		0x1F
#define IB_RMPP_FLAG_ACTIVE		0x01
#define IB_RMPP_FLAG_FIRST		0x02
#define IB_RMPP_FLAG_LAST		0x04

#define IB_RMPP_STATUS_SUCCESS		0
#define IB_RMPP_STATUS_RESX		1	/* resources exhausted */
#define IB_RMPP_STATUS_T2L		118	/* time too long */
#define IB_RMPP_STATUS_BAD_LEN		119	/* incon. last and payload len */
#define IB_RMPP_STATUS_BAD_SEG		120	/* incon. first and segment no */
#define IB_RMPP_STATUS_BADT		121	/* bad rmpp type */
#define IB_RMPP_STATUS_W2S		122	/* newwindowlast too small */
#define IB_RMPP_STATUS_S2B		123	/* segment no too big */
#define IB_RMPP_STATUS_BAD_STATUS	124	/* illegal status */
#define IB_RMPP_STATUS_UNV		125	/* unsupported version */
#define IB_RMPP_STATUS_TMR		126	/* too many retries */
#define IB_RMPP_STATUS_UNSPEC		127	/* unspecified */

/****f* IBA Base: Types/ib_rmpp_is_flag_set
* NAME
*	ib_rmpp_is_flag_set
*
* DESCRIPTION
*	Returns TRUE if the MAD has the given RMPP flag set.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_rmpp_is_flag_set(IN const ib_rmpp_mad_t * const p_rmpp_mad,
		    IN const uint8_t flag)
{
	CL_ASSERT(p_rmpp_mad);
	return ((p_rmpp_mad->rmpp_flags & flag) == flag);
}

/*
* PARAMETERS
*	ib_rmpp_mad_t
*		[in] Pointer to a MAD with an RMPP header.
*
*	flag
*		[in] The RMPP flag being examined.
*
* RETURN VALUES
*	Returns TRUE if the MAD has the given RMPP flag set.
*
* NOTES
*
* SEE ALSO
*	ib_mad_t, ib_rmpp_mad_t
*********/

static inline void OSM_API
ib_rmpp_set_resp_time(IN ib_rmpp_mad_t * const p_rmpp_mad,
		      IN const uint8_t resp_time)
{
	CL_ASSERT(p_rmpp_mad);
	p_rmpp_mad->rmpp_flags |= (resp_time << 3);
}

static inline uint8_t OSM_API
ib_rmpp_get_resp_time(IN const ib_rmpp_mad_t * const p_rmpp_mad)
{
	CL_ASSERT(p_rmpp_mad);
	return ((uint8_t) (p_rmpp_mad->rmpp_flags >> 3));
}

/****d* IBA Base: Constants/IB_SMP_DIRECTION
* NAME
*	IB_SMP_DIRECTION
*
* DESCRIPTION
*	The Direction bit for directed route SMPs.
*
* SOURCE
*/
#define IB_SMP_DIRECTION_HO		0x8000
#define IB_SMP_DIRECTION		(CL_HTON16(IB_SMP_DIRECTION_HO))
/**********/

/****d* IBA Base: Constants/IB_SMP_STATUS_MASK
* NAME
*	IB_SMP_STATUS_MASK
*
* DESCRIPTION
*	Mask value for extracting status from a directed route SMP.
*
* SOURCE
*/
#define IB_SMP_STATUS_MASK_HO		0x7FFF
#define IB_SMP_STATUS_MASK		(CL_HTON16(IB_SMP_STATUS_MASK_HO))
/**********/

/****s* IBA Base: Types/ib_smp_t
* NAME
*	ib_smp_t
*
* DESCRIPTION
*	IBA defined SMP. (14.2.1.2)
*
* SYNOPSIS
*/
#define IB_SMP_DATA_SIZE 64
#include <complib/cl_packon.h>
typedef struct _ib_smp {
	uint8_t base_ver;
	uint8_t mgmt_class;
	uint8_t class_ver;
	uint8_t method;
	ib_net16_t status;
	uint8_t hop_ptr;
	uint8_t hop_count;
	ib_net64_t trans_id;
	ib_net16_t attr_id;
	ib_net16_t resv;
	ib_net32_t attr_mod;
	ib_net64_t m_key;
	ib_net16_t dr_slid;
	ib_net16_t dr_dlid;
	uint32_t resv1[7];
	uint8_t data[IB_SMP_DATA_SIZE];
	uint8_t initial_path[IB_SUBNET_PATH_HOPS_MAX];
	uint8_t return_path[IB_SUBNET_PATH_HOPS_MAX];
} PACK_SUFFIX ib_smp_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	base_ver
*		MAD base format.
*
*	mgmt_class
*		Class of operation.
*
*	class_ver
*		Version of MAD class-specific format.
*
*	method
*		Method to perform, including 'R' bit.
*
*	status
*		Status of operation.
*
*	hop_ptr
*		Hop pointer for directed route MADs.
*
*	hop_count
*		Hop count for directed route MADs.
*
*	trans_Id
*		Transaction ID.
*
*	attr_id
*		Attribute ID.
*
*	resv
*		Reserved field.
*
*	attr_mod
*		Attribute modifier.
*
*	m_key
*		Management key value.
*
*	dr_slid
*		Directed route source LID.
*
*	dr_dlid
*		Directed route destination LID.
*
*	resv0
*		Reserved for 64 byte alignment.
*
*	data
*		MAD data payload.
*
*	initial_path
*		Outbound port list.
*
*	return_path
*		Inbound port list.
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_smp_get_status
* NAME
*	ib_smp_get_status
*
* DESCRIPTION
*	Returns the SMP status value in network order.
*
* SYNOPSIS
*/
static inline ib_net16_t OSM_API
ib_smp_get_status(IN const ib_smp_t * const p_smp)
{
	return ((ib_net16_t) (p_smp->status & IB_SMP_STATUS_MASK));
}

/*
* PARAMETERS
*	p_smp
*		[in] Pointer to the SMP packet.
*
* RETURN VALUES
*	Returns the SMP status value in network order.
*
* NOTES
*
* SEE ALSO
*	ib_smp_t
*********/

/****f* IBA Base: Types/ib_smp_is_response
* NAME
*	ib_smp_is_response
*
* DESCRIPTION
*	Returns TRUE if the SMP is a response MAD, FALSE otherwise.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_smp_is_response(IN const ib_smp_t * const p_smp)
{
	return (ib_mad_is_response((const ib_mad_t *)p_smp));
}

/*
* PARAMETERS
*	p_smp
*		[in] Pointer to the SMP packet.
*
* RETURN VALUES
*	Returns TRUE if the SMP is a response MAD, FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*	ib_smp_t
*********/

/****f* IBA Base: Types/ib_smp_is_d
* NAME
*	ib_smp_is_d
*
* DESCRIPTION
*	Returns TRUE if the SMP 'D' (direction) bit is set.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API ib_smp_is_d(IN const ib_smp_t * const p_smp)
{
	return ((p_smp->status & IB_SMP_DIRECTION) == IB_SMP_DIRECTION);
}

/*
* PARAMETERS
*	p_smp
*		[in] Pointer to the SMP packet.
*
* RETURN VALUES
*	Returns TRUE if the SMP 'D' (direction) bit is set.
*
* NOTES
*
* SEE ALSO
*	ib_smp_t
*********/

/****f* IBA Base: Types/ib_smp_init_new
* NAME
*	ib_smp_init_new
*
* DESCRIPTION
*	Initializes a MAD common header.
*
* TODO
*	This is too big for inlining, but leave it here for now
*	since there is not yet another convenient spot.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_smp_init_new(IN ib_smp_t * const p_smp,
		IN const uint8_t method,
		IN const ib_net64_t trans_id,
		IN const ib_net16_t attr_id,
		IN const ib_net32_t attr_mod,
		IN const uint8_t hop_count,
		IN const ib_net64_t m_key,
		IN const uint8_t * path_out,
		IN const ib_net16_t dr_slid, IN const ib_net16_t dr_dlid)
{
	CL_ASSERT(p_smp);
	CL_ASSERT(hop_count < IB_SUBNET_PATH_HOPS_MAX);
	p_smp->base_ver = 1;
	p_smp->mgmt_class = IB_MCLASS_SUBN_DIR;
	p_smp->class_ver = 1;
	p_smp->method = method;
	p_smp->status = 0;
	p_smp->hop_ptr = 0;
	p_smp->hop_count = hop_count;
	p_smp->trans_id = trans_id;
	p_smp->attr_id = attr_id;
	p_smp->resv = 0;
	p_smp->attr_mod = attr_mod;
	p_smp->m_key = m_key;
	p_smp->dr_slid = dr_slid;
	p_smp->dr_dlid = dr_dlid;

	memset(p_smp->resv1, 0,
	       sizeof(p_smp->resv1) +
	       sizeof(p_smp->data) +
	       sizeof(p_smp->initial_path) + sizeof(p_smp->return_path));

	/* copy the path */
	memcpy(&p_smp->initial_path, path_out, sizeof(p_smp->initial_path));
}

/*
* PARAMETERS
*	p_smp
*		[in] Pointer to the SMP packet.
*
*	method
*		[in] Method to perform, including 'R' bit.
*
*	trans_Id
*		[in] Transaction ID.
*
*	attr_id
*		[in] Attribute ID.
*
*	attr_mod
*		[in] Attribute modifier.
*
*	hop_count
*		[in] Number of hops in the path.
*
*	m_key
*		[in] Management key for this SMP.
*
*	path_out
*		[in] Port array for outbound path.
*
*
* RETURN VALUES
*	None.
*
* NOTES
*	Payload area is initialized to zero.
*
*
* SEE ALSO
*	ib_mad_t
*********/

/****f* IBA Base: Types/ib_smp_get_payload_ptr
* NAME
*	ib_smp_get_payload_ptr
*
* DESCRIPTION
*	Gets a pointer to the SMP payload area.
*
* SYNOPSIS
*/
static inline void *OSM_API
ib_smp_get_payload_ptr(IN const ib_smp_t * const p_smp)
{
	return ((void *)p_smp->data);
}

/*
* PARAMETERS
*	p_smp
*		[in] Pointer to the SMP packet.
*
* RETURN VALUES
*	Pointer to SMP payload area.
*
* NOTES
*
* SEE ALSO
*	ib_mad_t
*********/

/****s* IBA Base: Types/ib_node_info_t
* NAME
*	ib_node_info_t
*
* DESCRIPTION
*	IBA defined NodeInfo. (14.2.5.3)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_node_info {
	uint8_t base_version;
	uint8_t class_version;
	uint8_t node_type;
	uint8_t num_ports;
	ib_net64_t sys_guid;
	ib_net64_t node_guid;
	ib_net64_t port_guid;
	ib_net16_t partition_cap;
	ib_net16_t device_id;
	ib_net32_t revision;
	ib_net32_t port_num_vendor_id;
} PACK_SUFFIX ib_node_info_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_sa_mad_t
* NAME
*	ib_sa_mad_t
*
* DESCRIPTION
*	IBA defined SA MAD format. (15.2.1)
*
* SYNOPSIS
*/
#define IB_SA_DATA_SIZE 200

#include <complib/cl_packon.h>
typedef struct _ib_sa_mad {
	uint8_t base_ver;
	uint8_t mgmt_class;
	uint8_t class_ver;
	uint8_t method;
	ib_net16_t status;
	ib_net16_t resv;
	ib_net64_t trans_id;
	ib_net16_t attr_id;
	ib_net16_t resv1;
	ib_net32_t attr_mod;
	uint8_t rmpp_version;
	uint8_t rmpp_type;
	uint8_t rmpp_flags;
	uint8_t rmpp_status;
	ib_net32_t seg_num;
	ib_net32_t paylen_newwin;
	ib_net64_t sm_key;
	ib_net16_t attr_offset;
	ib_net16_t resv3;
	ib_net64_t comp_mask;
	uint8_t data[IB_SA_DATA_SIZE];
} PACK_SUFFIX ib_sa_mad_t;
#include <complib/cl_packoff.h>
/**********/
#define IB_SA_MAD_HDR_SIZE (sizeof(ib_sa_mad_t) - IB_SA_DATA_SIZE)

static inline uint32_t OSM_API ib_get_attr_size(IN const ib_net16_t attr_offset)
{
	return (((uint32_t) cl_ntoh16(attr_offset)) << 3);
}

static inline ib_net16_t OSM_API ib_get_attr_offset(IN const uint32_t attr_size)
{
	return (cl_hton16((uint16_t) (attr_size >> 3)));
}

/****f* IBA Base: Types/ib_sa_mad_get_payload_ptr
* NAME
*	ib_sa_mad_get_payload_ptr
*
* DESCRIPTION
*	Gets a pointer to the SA MAD's payload area.
*
* SYNOPSIS
*/
static inline void *OSM_API
ib_sa_mad_get_payload_ptr(IN const ib_sa_mad_t * const p_sa_mad)
{
	return ((void *)p_sa_mad->data);
}

/*
* PARAMETERS
*	p_sa_mad
*		[in] Pointer to the SA MAD packet.
*
* RETURN VALUES
*	Pointer to SA MAD payload area.
*
* NOTES
*
* SEE ALSO
*	ib_mad_t
*********/

#define IB_NODE_INFO_PORT_NUM_MASK		(CL_HTON32(0xFF000000))
#define IB_NODE_INFO_VEND_ID_MASK		(CL_HTON32(0x00FFFFFF))
#if CPU_LE
#define IB_NODE_INFO_PORT_NUM_SHIFT 0
#else
#define IB_NODE_INFO_PORT_NUM_SHIFT 24
#endif

/****f* IBA Base: Types/ib_node_info_get_local_port_num
* NAME
*	ib_node_info_get_local_port_num
*
* DESCRIPTION
*	Gets the local port number from the NodeInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_node_info_get_local_port_num(IN const ib_node_info_t * const p_ni)
{
	return ((uint8_t) ((p_ni->port_num_vendor_id &
			    IB_NODE_INFO_PORT_NUM_MASK)
			   >> IB_NODE_INFO_PORT_NUM_SHIFT));
}

/*
* PARAMETERS
*	p_ni
*		[in] Pointer to a NodeInfo attribute.
*
* RETURN VALUES
*	Local port number that returned the attribute.
*
* NOTES
*
* SEE ALSO
*	ib_node_info_t
*********/

/****f* IBA Base: Types/ib_node_info_get_vendor_id
* NAME
*	ib_node_info_get_vendor_id
*
* DESCRIPTION
*	Gets the VendorID from the NodeInfo attribute.
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_node_info_get_vendor_id(IN const ib_node_info_t * const p_ni)
{
	return ((ib_net32_t) (p_ni->port_num_vendor_id &
			      IB_NODE_INFO_VEND_ID_MASK));
}

/*
* PARAMETERS
*	p_ni
*		[in] Pointer to a NodeInfo attribute.
*
* RETURN VALUES
*	VendorID that returned the attribute.
*
* NOTES
*
* SEE ALSO
*	ib_node_info_t
*********/

#define IB_NODE_DESCRIPTION_SIZE 64

#include <complib/cl_packon.h>
typedef struct _ib_node_desc {
	// Node String is an array of UTF-8 characters
	// that describe the node in text format
	// Note that this string is NOT NULL TERMINATED!
	uint8_t description[IB_NODE_DESCRIPTION_SIZE];
} PACK_SUFFIX ib_node_desc_t;
#include <complib/cl_packoff.h>

#include <complib/cl_packon.h>
typedef struct _ib_node_record_t {
	ib_net16_t lid;
	ib_net16_t resv;
	ib_node_info_t node_info;
	ib_node_desc_t node_desc;
	uint8_t pad[4];
} PACK_SUFFIX ib_node_record_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_port_info_t
* NAME
*	ib_port_info_t
*
* DESCRIPTION
*	IBA defined PortInfo. (14.2.5.6)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_info {
	ib_net64_t m_key;
	ib_net64_t subnet_prefix;
	ib_net16_t base_lid;
	ib_net16_t master_sm_base_lid;
	ib_net32_t capability_mask;
	ib_net16_t diag_code;
	ib_net16_t m_key_lease_period;
	uint8_t local_port_num;
	uint8_t link_width_enabled;
	uint8_t link_width_supported;
	uint8_t link_width_active;
	uint8_t state_info1;	/* LinkSpeedSupported and PortState */
	uint8_t state_info2;	/* PortPhysState and LinkDownDefaultState */
	uint8_t mkey_lmc;	/* M_KeyProtectBits and LMC */
	uint8_t link_speed;	/* LinkSpeedEnabled and LinkSpeedActive */
	uint8_t mtu_smsl;
	uint8_t vl_cap;		/* VLCap and InitType */
	uint8_t vl_high_limit;
	uint8_t vl_arb_high_cap;
	uint8_t vl_arb_low_cap;
	uint8_t mtu_cap;
	uint8_t vl_stall_life;
	uint8_t vl_enforce;
	ib_net16_t m_key_violations;
	ib_net16_t p_key_violations;
	ib_net16_t q_key_violations;
	uint8_t guid_cap;
	uint8_t subnet_timeout;	/* cli_rereg(1b), mcast_pkey_trap_suppr(1b), reserv(1b), timeout(5b) */
	uint8_t resp_time_value; /* reserv(3b), rtv(5b) */
	uint8_t error_threshold; /* local phy errors(4b), overrun errors(4b) */
	ib_net16_t max_credit_hint;
	ib_net32_t link_rt_latency; /* reserv(8b), link round trip lat(24b) */
	ib_net16_t capability_mask2;
	uint8_t link_speed_ext;	/* LinkSpeedExtActive and LinkSpeedExtSupported */
	uint8_t link_speed_ext_enabled; /* reserv(3b), LinkSpeedExtEnabled(5b) */
} PACK_SUFFIX ib_port_info_t;
#include <complib/cl_packoff.h>
/************/

#define IB_PORT_STATE_MASK			0x0F
#define IB_PORT_LMC_MASK			0x07
#define IB_PORT_LMC_MAX				0x07
#define IB_PORT_MPB_MASK			0xC0
#define IB_PORT_MPB_SHIFT			6
#define IB_PORT_LINK_SPEED_SHIFT		4
#define IB_PORT_LINK_SPEED_SUPPORTED_MASK	0xF0
#define IB_PORT_LINK_SPEED_ACTIVE_MASK		0xF0
#define IB_PORT_LINK_SPEED_ENABLED_MASK		0x0F
#define IB_PORT_PHYS_STATE_MASK			0xF0
#define IB_PORT_PHYS_STATE_SHIFT		4
#define IB_PORT_PHYS_STATE_NO_CHANGE		0
#define IB_PORT_PHYS_STATE_SLEEP		1
#define IB_PORT_PHYS_STATE_POLLING		2
#define IB_PORT_PHYS_STATE_DISABLED		3
#define IB_PORT_PHYS_STATE_PORTCONFTRAIN	4
#define IB_PORT_PHYS_STATE_LINKUP	        5
#define IB_PORT_PHYS_STATE_LINKERRRECOVER	6
#define IB_PORT_PHYS_STATE_PHYTEST	        7
#define IB_PORT_LNKDWNDFTSTATE_MASK		0x0F

#define IB_PORT_CAP_RESV0         (CL_HTON32(0x00000001))
#define IB_PORT_CAP_IS_SM         (CL_HTON32(0x00000002))
#define IB_PORT_CAP_HAS_NOTICE    (CL_HTON32(0x00000004))
#define IB_PORT_CAP_HAS_TRAP      (CL_HTON32(0x00000008))
#define IB_PORT_CAP_HAS_IPD       (CL_HTON32(0x00000010))
#define IB_PORT_CAP_HAS_AUTO_MIG  (CL_HTON32(0x00000020))
#define IB_PORT_CAP_HAS_SL_MAP    (CL_HTON32(0x00000040))
#define IB_PORT_CAP_HAS_NV_MKEY   (CL_HTON32(0x00000080))
#define IB_PORT_CAP_HAS_NV_PKEY   (CL_HTON32(0x00000100))
#define IB_PORT_CAP_HAS_LED_INFO  (CL_HTON32(0x00000200))
#define IB_PORT_CAP_SM_DISAB      (CL_HTON32(0x00000400))
#define IB_PORT_CAP_HAS_SYS_IMG_GUID  (CL_HTON32(0x00000800))
#define IB_PORT_CAP_HAS_PKEY_SW_EXT_PORT_TRAP (CL_HTON32(0x00001000))
#define IB_PORT_CAP_HAS_CABLE_INFO  (CL_HTON32(0x00002000))
#define IB_PORT_CAP_HAS_EXT_SPEEDS  (CL_HTON32(0x00004000))
#define IB_PORT_CAP_HAS_CAP_MASK2 (CL_HTON32(0x00008000))
#define IB_PORT_CAP_HAS_COM_MGT   (CL_HTON32(0x00010000))
#define IB_PORT_CAP_HAS_SNMP      (CL_HTON32(0x00020000))
#define IB_PORT_CAP_REINIT        (CL_HTON32(0x00040000))
#define IB_PORT_CAP_HAS_DEV_MGT   (CL_HTON32(0x00080000))
#define IB_PORT_CAP_HAS_VEND_CLS  (CL_HTON32(0x00100000))
#define IB_PORT_CAP_HAS_DR_NTC    (CL_HTON32(0x00200000))
#define IB_PORT_CAP_HAS_CAP_NTC   (CL_HTON32(0x00400000))
#define IB_PORT_CAP_HAS_BM        (CL_HTON32(0x00800000))
#define IB_PORT_CAP_HAS_LINK_RT_LATENCY (CL_HTON32(0x01000000))
#define IB_PORT_CAP_HAS_CLIENT_REREG (CL_HTON32(0x02000000))
#define IB_PORT_CAP_HAS_OTHER_LOCAL_CHANGES_NTC (CL_HTON32(0x04000000))
#define IB_PORT_CAP_HAS_LINK_SPEED_WIDTH_PAIRS_TBL (CL_HTON32(0x08000000))
#define IB_PORT_CAP_HAS_VEND_MADS (CL_HTON32(0x10000000))
#define IB_PORT_CAP_HAS_MCAST_PKEY_TRAP_SUPPRESS (CL_HTON32(0x20000000))
#define IB_PORT_CAP_HAS_MCAST_FDB_TOP (CL_HTON32(0x40000000))
#define IB_PORT_CAP_HAS_HIER_INFO (CL_HTON32(0x80000000))

#define IB_PORT_CAP2_IS_SET_NODE_DESC_SUPPORTED (CL_HTON16(0x0001))
#define IB_PORT_CAP2_IS_PORT_INFO_EXT_SUPPORTED (CL_HTON16(0x0002))
#define IB_PORT_CAP2_IS_VIRT_SUPPORTED (CL_HTON16(0x0004))
#define IB_PORT_CAP2_IS_SWITCH_PORT_STATE_TBL_SUPP (CL_HTON16(0x0008))
#define IB_PORT_CAP2_IS_LINK_WIDTH_2X_SUPPORTED (CL_HTON16(0x0010))

/****s* IBA Base: Types/ib_port_info_ext_t
* NAME
*	ib_port_info_ext_t
*
* DESCRIPTION
*	IBA defined PortInfoExtended. (14.2.5.19)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_info_ext {
	ib_net32_t cap_mask;
	ib_net16_t fec_mode_active;
	ib_net16_t fdr_fec_mode_sup;
	ib_net16_t fdr_fec_mode_enable;
	ib_net16_t edr_fec_mode_sup;
	ib_net16_t edr_fec_mode_enable;
	uint8_t reserved[50];
} PACK_SUFFIX ib_port_info_ext_t;
#include <complib/cl_packoff.h>
/************/

#define IB_PORT_EXT_NO_FEC_MODE_ACTIVE		    0
#define IB_PORT_EXT_FIRE_CODE_FEC_MODE_ACTIVE	    (CL_HTON16(0x0001))
#define IB_PORT_EXT_RS_FEC_MODE_ACTIVE		    (CL_HTON16(0x0002))
#define IB_PORT_EXT_LOW_LATENCY_RS_FEC_MODE_ACTIVE  (CL_HTON16(0x0003))

#define IB_PORT_EXT_CAP_IS_FEC_MODE_SUPPORTED (CL_HTON32(0x00000001))
/****f* IBA Base: Types/ib_port_info_get_port_state
* NAME
*	ib_port_info_get_port_state
*
* DESCRIPTION
*	Returns the port state.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_port_state(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->state_info1 & IB_PORT_STATE_MASK));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Port state.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_port_state
* NAME
*	ib_port_info_set_port_state
*
* DESCRIPTION
*	Sets the port state.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_port_state(IN ib_port_info_t * const p_pi,
			    IN const uint8_t port_state)
{
	p_pi->state_info1 = (uint8_t) ((p_pi->state_info1 & 0xF0) | port_state);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	port_state
*		[in] Port state value to set.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_vl_cap
* NAME
*	ib_port_info_get_vl_cap
*
* DESCRIPTION
*	Gets the VL Capability of a port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_vl_cap(IN const ib_port_info_t * const p_pi)
{
	return ((p_pi->vl_cap >> 4) & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	VL_CAP field
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_init_type
* NAME
*	ib_port_info_get_init_type
*
* DESCRIPTION
*	Gets the init type of a port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_init_type(IN const ib_port_info_t * const p_pi)
{
	return (uint8_t) (p_pi->vl_cap & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	InitType field
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_op_vls
* NAME
*	ib_port_info_get_op_vls
*
* DESCRIPTION
*	Gets the operational VLs on a port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_op_vls(IN const ib_port_info_t * const p_pi)
{
	return ((p_pi->vl_enforce >> 4) & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	OP_VLS field
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_op_vls
* NAME
*	ib_port_info_set_op_vls
*
* DESCRIPTION
*	Sets the operational VLs on a port.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_op_vls(IN ib_port_info_t * const p_pi, IN const uint8_t op_vls)
{
	p_pi->vl_enforce =
	    (uint8_t) ((p_pi->vl_enforce & 0x0F) | (op_vls << 4));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	op_vls
*		[in] Encoded operation VLs value.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_state_no_change
* NAME
*	ib_port_info_set_state_no_change
*
* DESCRIPTION
*	Sets the port state fields to the value for "no change".
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_state_no_change(IN ib_port_info_t * const p_pi)
{
	ib_port_info_set_port_state(p_pi, IB_LINK_NO_CHANGE);
	p_pi->state_info2 = 0;
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_sup
* NAME
*	ib_port_info_get_link_speed_sup
*
* DESCRIPTION
*	Returns the encoded value for the link speed supported.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_sup(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) ((p_pi->state_info1 &
			    IB_PORT_LINK_SPEED_SUPPORTED_MASK) >>
			   IB_PORT_LINK_SPEED_SHIFT));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the encoded value for the link speed supported.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_link_speed_sup
* NAME
*	ib_port_info_set_link_speed_sup
*
* DESCRIPTION
*	Given an integer of the supported link speed supported.
*	Set the appropriate bits in state_info1
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_link_speed_sup(IN uint8_t const speed,
				IN ib_port_info_t * p_pi)
{
	p_pi->state_info1 =
	    (~IB_PORT_LINK_SPEED_SUPPORTED_MASK & p_pi->state_info1) |
	    (IB_PORT_LINK_SPEED_SUPPORTED_MASK &
	     (speed << IB_PORT_LINK_SPEED_SHIFT));
}

/*
* PARAMETERS
*	speed
*		[in] Supported Speeds Code.
*
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_port_phys_state
* NAME
*	ib_port_info_get_port_phys_state
*
* DESCRIPTION
*	Returns the encoded value for the port physical state.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_port_phys_state(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) ((p_pi->state_info2 &
			    IB_PORT_PHYS_STATE_MASK) >>
			   IB_PORT_PHYS_STATE_SHIFT));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the encoded value for the port physical state.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_port_phys_state
* NAME
*	ib_port_info_set_port_phys_state
*
* DESCRIPTION
*	Given an integer of the port physical state,
*	Set the appropriate bits in state_info2
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_port_phys_state(IN uint8_t const phys_state,
				 IN ib_port_info_t * p_pi)
{
	p_pi->state_info2 =
	    (~IB_PORT_PHYS_STATE_MASK & p_pi->state_info2) |
	    (IB_PORT_PHYS_STATE_MASK &
	     (phys_state << IB_PORT_PHYS_STATE_SHIFT));
}

/*
* PARAMETERS
*	phys_state
*		[in] port physical state.
*
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_down_def_state
* NAME
*	ib_port_info_get_link_down_def_state
*
* DESCRIPTION
*	Returns the link down default state.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_down_def_state(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->state_info2 & IB_PORT_LNKDWNDFTSTATE_MASK));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	link down default state of the port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_link_down_def_state
* NAME
*	ib_port_info_set_link_down_def_state
*
* DESCRIPTION
*	Sets the link down default state of the port.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_link_down_def_state(IN ib_port_info_t * const p_pi,
				     IN const uint8_t link_dwn_state)
{
	p_pi->state_info2 =
	    (uint8_t) ((p_pi->state_info2 & 0xF0) | link_dwn_state);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	link_dwn_state
*		[in] Link down default state of the port.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_active
* NAME
*	ib_port_info_get_link_speed_active
*
* DESCRIPTION
*	Returns the Link Speed Active value assigned to this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_active(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) ((p_pi->link_speed &
			    IB_PORT_LINK_SPEED_ACTIVE_MASK) >>
			   IB_PORT_LINK_SPEED_SHIFT));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the link speed active value assigned to this port.
*
* NOTES
*
* SEE ALSO
*********/

#define IB_LINK_WIDTH_ACTIVE_1X			1
#define IB_LINK_WIDTH_ACTIVE_4X			2
#define IB_LINK_WIDTH_ACTIVE_8X			4
#define IB_LINK_WIDTH_ACTIVE_12X 		8
#define IB_LINK_WIDTH_ACTIVE_2X			16
#define IB_LINK_SPEED_ACTIVE_EXTENDED		0
#define IB_LINK_SPEED_ACTIVE_2_5		1
#define IB_LINK_SPEED_ACTIVE_5			2
#define IB_LINK_SPEED_ACTIVE_10			4
#define IB_LINK_SPEED_EXT_ACTIVE_NONE		0
#define IB_LINK_SPEED_EXT_ACTIVE_14		1
#define IB_LINK_SPEED_EXT_ACTIVE_25		2
#define IB_LINK_SPEED_EXT_DISABLE		30
#define IB_LINK_SPEED_EXT_SET_LSES		31

/* following v1 ver1.3 p984 */
#define IB_PATH_RECORD_RATE_2_5_GBS		2
#define IB_PATH_RECORD_RATE_10_GBS		3
#define IB_PATH_RECORD_RATE_30_GBS		4
#define IB_PATH_RECORD_RATE_5_GBS		5
#define IB_PATH_RECORD_RATE_20_GBS		6
#define IB_PATH_RECORD_RATE_40_GBS		7
#define IB_PATH_RECORD_RATE_60_GBS		8
#define IB_PATH_RECORD_RATE_80_GBS		9
#define IB_PATH_RECORD_RATE_120_GBS		10
#define IB_PATH_RECORD_RATE_14_GBS		11
#define IB_PATH_RECORD_RATE_56_GBS		12
#define IB_PATH_RECORD_RATE_112_GBS		13
#define IB_PATH_RECORD_RATE_168_GBS		14
#define IB_PATH_RECORD_RATE_25_GBS		15
#define IB_PATH_RECORD_RATE_100_GBS		16
#define IB_PATH_RECORD_RATE_200_GBS		17
#define IB_PATH_RECORD_RATE_300_GBS		18
#define IB_PATH_RECORD_RATE_28_GBS		19
#define IB_PATH_RECORD_RATE_50_GBS		20

#define IB_MIN_RATE    IB_PATH_RECORD_RATE_2_5_GBS
#define IB_MAX_RATE    IB_PATH_RECORD_RATE_50_GBS

static inline uint8_t OSM_API
ib_port_info_get_link_speed_ext_active(IN const ib_port_info_t * const p_pi);

/****f* IBA Base: Types/ib_port_info_compute_rate
* NAME
*	ib_port_info_compute_rate
*
* DESCRIPTION
*	Returns the encoded value for the path rate.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_compute_rate(IN const ib_port_info_t * const p_pi,
			  IN const int extended)
{
	uint8_t rate = 0;

	if (extended) {
		switch (ib_port_info_get_link_speed_ext_active(p_pi)) {
		case IB_LINK_SPEED_EXT_ACTIVE_14:
			switch (p_pi->link_width_active) {
			case IB_LINK_WIDTH_ACTIVE_1X:
				rate = IB_PATH_RECORD_RATE_14_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_4X:
				rate = IB_PATH_RECORD_RATE_56_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_8X:
				rate = IB_PATH_RECORD_RATE_112_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_12X:
				rate = IB_PATH_RECORD_RATE_168_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_2X:
				rate = IB_PATH_RECORD_RATE_28_GBS;
				break;

			default:
				rate = IB_PATH_RECORD_RATE_14_GBS;
				break;
			}
			break;
		case IB_LINK_SPEED_EXT_ACTIVE_25:
			switch (p_pi->link_width_active) {
			case IB_LINK_WIDTH_ACTIVE_1X:
				rate = IB_PATH_RECORD_RATE_25_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_4X:
				rate = IB_PATH_RECORD_RATE_100_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_8X:
				rate = IB_PATH_RECORD_RATE_200_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_12X:
				rate = IB_PATH_RECORD_RATE_300_GBS;
				break;

			case IB_LINK_WIDTH_ACTIVE_2X:
				rate = IB_PATH_RECORD_RATE_50_GBS;
				break;

			default:
				rate = IB_PATH_RECORD_RATE_25_GBS;
				break;
			}
			break;
		/* IB_LINK_SPEED_EXT_ACTIVE_NONE and any others */
		default:
			break;
		}
		if (rate)
			return rate;
	}

	switch (ib_port_info_get_link_speed_active(p_pi)) {
	case IB_LINK_SPEED_ACTIVE_2_5:
		switch (p_pi->link_width_active) {
		case IB_LINK_WIDTH_ACTIVE_1X:
			rate = IB_PATH_RECORD_RATE_2_5_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_4X:
			rate = IB_PATH_RECORD_RATE_10_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_8X:
			rate = IB_PATH_RECORD_RATE_20_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_12X:
			rate = IB_PATH_RECORD_RATE_30_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_2X:
			rate = IB_PATH_RECORD_RATE_5_GBS;
			break;

		default:
			rate = IB_PATH_RECORD_RATE_2_5_GBS;
			break;
		}
		break;
	case IB_LINK_SPEED_ACTIVE_5:
		switch (p_pi->link_width_active) {
		case IB_LINK_WIDTH_ACTIVE_1X:
			rate = IB_PATH_RECORD_RATE_5_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_4X:
			rate = IB_PATH_RECORD_RATE_20_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_8X:
			rate = IB_PATH_RECORD_RATE_40_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_12X:
			rate = IB_PATH_RECORD_RATE_60_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_2X:
			rate = IB_PATH_RECORD_RATE_10_GBS;
			break;

		default:
			rate = IB_PATH_RECORD_RATE_5_GBS;
			break;
		}
		break;
	case IB_LINK_SPEED_ACTIVE_10:
		switch (p_pi->link_width_active) {
		case IB_LINK_WIDTH_ACTIVE_1X:
			rate = IB_PATH_RECORD_RATE_10_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_4X:
			rate = IB_PATH_RECORD_RATE_40_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_8X:
			rate = IB_PATH_RECORD_RATE_80_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_12X:
			rate = IB_PATH_RECORD_RATE_120_GBS;
			break;

		case IB_LINK_WIDTH_ACTIVE_2X:
			rate = IB_PATH_RECORD_RATE_20_GBS;
			break;

		default:
			rate = IB_PATH_RECORD_RATE_10_GBS;
			break;
		}
		break;
	default:
		rate = IB_PATH_RECORD_RATE_2_5_GBS;
		break;
	}

	return rate;
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	extended
*		[in] Indicates whether or not to use extended link speeds.
*
* RETURN VALUES
*	Returns the encoded value for the link speed supported.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_path_get_ipd
* NAME
*	ib_path_get_ipd
*
* DESCRIPTION
*	Returns the encoded value for the inter packet delay.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_path_get_ipd(IN uint8_t local_link_width_supported, IN uint8_t path_rec_rate)
{
	uint8_t ipd = 0;

	switch (local_link_width_supported) {
		/* link_width_supported = 1: 1x */
	case 1:
		break;

		/* link_width_supported = 3: 1x or 4x */
	case 3:
		switch (path_rec_rate & 0x3F) {
		case IB_PATH_RECORD_RATE_2_5_GBS:
			ipd = 3;
			break;
		default:
			break;
		}
		break;

		/* link_width_supported = 11: 1x or 4x or 12x */
	case 11:
		switch (path_rec_rate & 0x3F) {
		case IB_PATH_RECORD_RATE_2_5_GBS:
			ipd = 11;
			break;
		case IB_PATH_RECORD_RATE_10_GBS:
			ipd = 2;
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}

	return ipd;
}

/*
* PARAMETERS
*	local_link_width_supported
*		[in] link with supported for this port
*
*	path_rec_rate
*		[in] rate field of the path record
*
* RETURN VALUES
*	Returns the ipd
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_mtu_cap
* NAME
*	ib_port_info_get_mtu_cap
*
* DESCRIPTION
*	Returns the encoded value for the maximum MTU supported by this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_mtu_cap(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->mtu_cap & 0x0F));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the encooded value for the maximum MTU supported by this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_neighbor_mtu
* NAME
*	ib_port_info_get_neighbor_mtu
*
* DESCRIPTION
*	Returns the encoded value for the neighbor MTU supported by this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_neighbor_mtu(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) ((p_pi->mtu_smsl & 0xF0) >> 4));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the encoded value for the neighbor MTU at this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_neighbor_mtu
* NAME
*	ib_port_info_set_neighbor_mtu
*
* DESCRIPTION
*	Sets the Neighbor MTU value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_neighbor_mtu(IN ib_port_info_t * const p_pi,
			      IN const uint8_t mtu)
{
	CL_ASSERT(mtu <= 5);
	CL_ASSERT(mtu != 0);
	p_pi->mtu_smsl = (uint8_t) ((p_pi->mtu_smsl & 0x0F) | (mtu << 4));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	mtu
*		[in] Encoded MTU value to set
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_master_smsl
* NAME
*	ib_port_info_get_master_smsl
*
* DESCRIPTION
*	Returns the encoded value for the Master SMSL at this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_master_smsl(IN const ib_port_info_t * const p_pi)
{
	return (uint8_t) (p_pi->mtu_smsl & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the encoded value for the Master SMSL at this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_master_smsl
* NAME
*	ib_port_info_set_master_smsl
*
* DESCRIPTION
*	Sets the Master SMSL value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_master_smsl(IN ib_port_info_t * const p_pi,
			     IN const uint8_t smsl)
{
	p_pi->mtu_smsl = (uint8_t) ((p_pi->mtu_smsl & 0xF0) | smsl);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	mtu
*		[in] Encoded Master SMSL value to set
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_timeout
* NAME
*	ib_port_info_set_timeout
*
* DESCRIPTION
*	Sets the encoded subnet timeout value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_timeout(IN ib_port_info_t * const p_pi,
			 IN const uint8_t timeout)
{
	CL_ASSERT(timeout <= 0x1F);
	p_pi->subnet_timeout =
	    (uint8_t) ((p_pi->subnet_timeout & 0xE0) | (timeout & 0x1F));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	timeout
*		[in] Encoded timeout value to set
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_client_rereg
* NAME
*	ib_port_info_set_client_rereg
*
* DESCRIPTION
*	Sets the encoded client reregistration bit value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_client_rereg(IN ib_port_info_t * const p_pi,
			      IN const uint8_t client_rereg)
{
	CL_ASSERT(client_rereg <= 0x1);
	p_pi->subnet_timeout =
	    (uint8_t) ((p_pi->subnet_timeout & 0x7F) | (client_rereg << 7));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	client_rereg
*		[in] Client reregistration value to set (either 1 or 0).
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_mcast_pkey_trap_suppress
* NAME
*	ib_port_info_set_mcast_pkey_trap_suppress
*
* DESCRIPTION
*	Sets the encoded multicast pkey trap suppression enabled bit value
*	in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_mcast_pkey_trap_suppress(IN ib_port_info_t * const p_pi,
					  IN const uint8_t trap_suppress)
{
	CL_ASSERT(trap_suppress <= 0x1);
	p_pi->subnet_timeout =
	    (uint8_t) ((p_pi->subnet_timeout & 0xBF) | (trap_suppress << 6));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	trap_suppress
*		[in] Multicast pkey trap suppression enabled value to set
*		     (either 1 or 0).
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_timeout
* NAME
*	ib_port_info_get_timeout
*
* DESCRIPTION
*	Gets the encoded subnet timeout value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_timeout(IN ib_port_info_t const *p_pi)
{
	return (p_pi->subnet_timeout & 0x1F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	The encoded timeout value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_ext_active
* NAME
*	ib_port_info_get_link_speed_ext_active
*
* DESCRIPTION
*	Gets the encoded LinkSpeedExtActive value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_ext_active(IN const ib_port_info_t * const p_pi)
{
	return ((p_pi->link_speed_ext & 0xF0) >> 4);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	The encoded LinkSpeedExtActive value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_ext_sup
* NAME
*	ib_port_info_get_link_speed_ext_sup
*
* DESCRIPTION
*	Returns the encoded value for the link speed extended supported.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_ext_sup(IN const ib_port_info_t * const p_pi)
{
	return (p_pi->link_speed_ext & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	The encoded LinkSpeedExtSupported value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_ext_enabled
* NAME
*	ib_port_info_get_link_speed_ext_enabled
*
* DESCRIPTION
*	Gets the encoded LinkSpeedExtEnabled value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_ext_enabled(IN const ib_port_info_t * const p_pi)
{
        return (p_pi->link_speed_ext_enabled & 0x1F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	The encoded LinkSpeedExtEnabled value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_link_speed_ext_enabled
* NAME
*	ib_port_info_set_link_speed_ext_enabled
*
* DESCRIPTION
*	Sets the link speed extended enabled value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_link_speed_ext_enabled(IN ib_port_info_t * const p_pi,
					IN const uint8_t link_speed_ext_enabled)
{
	CL_ASSERT(link_speed_ext_enabled <= 0x1F);
	p_pi->link_speed_ext_enabled = link_speed_ext_enabled & 0x1F;
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	link_speed_ext_enabled
*		[in] link speed extehded enabled value to set.
*
* RETURN VALUES
*	The encoded LinkSpeedExtEnabled value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_resp_time_value
* NAME
*	ib_port_info_get_resp_time_value
*
* DESCRIPTION
*	Gets the encoded resp time value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_resp_time_value(IN const ib_port_info_t * const p_pi)
{
	return (p_pi->resp_time_value & 0x1F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	The encoded resp time value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_client_rereg
* NAME
*	ib_port_info_get_client_rereg
*
* DESCRIPTION
*	Gets the encoded client reregistration bit value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_client_rereg(IN ib_port_info_t const *p_pi)
{
	return ((p_pi->subnet_timeout & 0x80) >> 7);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Client reregistration value (either 1 or 0).
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_mcast_pkey_trap_suppress
* NAME
*	ib_port_info_get_mcast_pkey_trap_suppress
*
* DESCRIPTION
*	Gets the encoded multicast pkey trap suppression enabled bit value
*	in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_mcast_pkey_trap_suppress(IN ib_port_info_t const *p_pi)
{
	return ((p_pi->subnet_timeout & 0x40) >> 6);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Multicast PKey trap suppression enabled value (either 1 or 0).
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_hoq_lifetime
* NAME
*	ib_port_info_set_hoq_lifetime
*
* DESCRIPTION
*	Sets the Head of Queue Lifetime for which a packet can live in the head
*  of VL queue
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_hoq_lifetime(IN ib_port_info_t * const p_pi,
			      IN const uint8_t hoq_life)
{
	p_pi->vl_stall_life = (uint8_t) ((hoq_life & 0x1f) |
					 (p_pi->vl_stall_life & 0xe0));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	hoq_life
*		[in] Encoded lifetime value to set
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_hoq_lifetime
* NAME
*	ib_port_info_get_hoq_lifetime
*
* DESCRIPTION
*	Gets the Head of Queue Lifetime for which a packet can live in the head
*  of VL queue
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_hoq_lifetime(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->vl_stall_life & 0x1f));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*     Encoded lifetime value
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_vl_stall_count
* NAME
*	ib_port_info_set_vl_stall_count
*
* DESCRIPTION
*	Sets the VL Stall Count which define the number of contiguous
*  HLL (hoq) drops that will put the VL into stalled mode.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_vl_stall_count(IN ib_port_info_t * const p_pi,
				IN const uint8_t vl_stall_count)
{
	p_pi->vl_stall_life = (uint8_t) ((p_pi->vl_stall_life & 0x1f) |
					 ((vl_stall_count << 5) & 0xe0));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	vl_stall_count
*		[in] value to set
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_vl_stall_count
* NAME
*	ib_port_info_get_vl_stall_count
*
* DESCRIPTION
*	Gets the VL Stall Count which define the number of contiguous
*  HLL (hoq) drops that will put the VL into stalled mode
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_vl_stall_count(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->vl_stall_life & 0xe0) >> 5);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*     vl stall count
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_lmc
* NAME
*	ib_port_info_get_lmc
*
* DESCRIPTION
*	Returns the LMC value assigned to this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_lmc(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->mkey_lmc & IB_PORT_LMC_MASK));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the LMC value assigned to this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_lmc
* NAME
*	ib_port_info_set_lmc
*
* DESCRIPTION
*	Sets the LMC value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_lmc(IN ib_port_info_t * const p_pi, IN const uint8_t lmc)
{
	CL_ASSERT(lmc <= IB_PORT_LMC_MAX);
	p_pi->mkey_lmc = (uint8_t) ((p_pi->mkey_lmc & 0xF8) | lmc);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	lmc
*		[in] LMC value to set, must be less than 7.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_link_speed_enabled
* NAME
*	ib_port_info_get_link_speed_enabled
*
* DESCRIPTION
*	Returns the link speed enabled value assigned to this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_link_speed_enabled(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) (p_pi->link_speed & IB_PORT_LINK_SPEED_ENABLED_MASK));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Port state.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_link_speed_enabled
* NAME
*	ib_port_info_set_link_speed_enabled
*
* DESCRIPTION
*	Sets the link speed enabled value in the PortInfo attribute.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_link_speed_enabled(IN ib_port_info_t * const p_pi,
				    IN const uint8_t link_speed_enabled)
{
	p_pi->link_speed =
	    (uint8_t) ((p_pi->link_speed & 0xF0) | link_speed_enabled);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	link_speed_enabled
*		[in] link speed enabled value to set.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_mpb
* NAME
*	ib_port_info_get_mpb
*
* DESCRIPTION
*	Returns the M_Key protect bits assigned to this port.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_mpb(IN const ib_port_info_t * const p_pi)
{
	return ((uint8_t) ((p_pi->mkey_lmc & IB_PORT_MPB_MASK) >>
			   IB_PORT_MPB_SHIFT));
}

/*
* PARAMETERS
*	p_ni
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the M_Key protect bits assigned to this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_mpb
* NAME
*	ib_port_info_set_mpb
*
* DESCRIPTION
*	Set the M_Key protect bits of this port.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_mpb(IN ib_port_info_t * p_pi, IN uint8_t mpb)
{
	p_pi->mkey_lmc =
	    (~IB_PORT_MPB_MASK & p_pi->mkey_lmc) |
	    (IB_PORT_MPB_MASK & (mpb << IB_PORT_MPB_SHIFT));
}

/*
* PARAMETERS
*	mpb
*		[in] M_Key protect bits
*	p_ni
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_local_phy_err_thd
* NAME
*	ib_port_info_get_local_phy_err_thd
*
* DESCRIPTION
*	Returns the Phy Link Threshold
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_local_phy_err_thd(IN const ib_port_info_t * const p_pi)
{
	return (uint8_t) ((p_pi->error_threshold & 0xF0) >> 4);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the Phy Link error threshold assigned to this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_overrun_err_thd
* NAME
*	ib_port_info_get_local_overrun_err_thd
*
* DESCRIPTION
*	Returns the Credits Overrun Errors Threshold
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_port_info_get_overrun_err_thd(IN const ib_port_info_t * const p_pi)
{
	return (uint8_t) (p_pi->error_threshold & 0x0F);
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	Returns the Credits Overrun errors threshold assigned to this port.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_phy_and_overrun_err_thd
* NAME
*	ib_port_info_set_phy_and_overrun_err_thd
*
* DESCRIPTION
*	Sets the Phy Link and Credits Overrun Errors Threshold
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_phy_and_overrun_err_thd(IN ib_port_info_t * const p_pi,
					 IN uint8_t phy_threshold,
					 IN uint8_t overrun_threshold)
{
	p_pi->error_threshold =
	    (uint8_t) (((phy_threshold & 0x0F) << 4) |
		       (overrun_threshold & 0x0F));
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
*	phy_threshold
*		[in] Physical Link Errors Threshold above which Trap 129 is generated
*
*  overrun_threshold
*     [in] Credits overrun Errors Threshold above which Trap 129 is generated
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_get_m_key
* NAME
*	ib_port_info_get_m_key
*
* DESCRIPTION
*	Gets the M_Key
*
* SYNOPSIS
*/
static inline ib_net64_t OSM_API
ib_port_info_get_m_key(IN const ib_port_info_t * const p_pi)
{
	return p_pi->m_key;
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*
* RETURN VALUES
*	M_Key.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_port_info_set_m_key
* NAME
*	ib_port_info_set_m_key
*
* DESCRIPTION
*	Sets the M_Key value
*
* SYNOPSIS
*/
static inline void OSM_API
ib_port_info_set_m_key(IN ib_port_info_t * const p_pi, IN ib_net64_t m_key)
{
	p_pi->m_key = m_key;
}

/*
* PARAMETERS
*	p_pi
*		[in] Pointer to a PortInfo attribute.
*	m_key
*		[in] M_Key value.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/


/****s* IBA Base: Types/ib_mlnx_ext_port_info_t
* NAME
*	ib_mlnx_ext_port_info_t
*
* DESCRIPTION
*	Mellanox ExtendedPortInfo (Vendor specific SM class attribute).
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_mlnx_ext_port_info {
	uint8_t resvd1[3];
	uint8_t state_change_enable;
	uint8_t resvd2[3];
	uint8_t link_speed_supported;
	uint8_t resvd3[3];
	uint8_t link_speed_enabled;
	uint8_t resvd4[3];
	uint8_t link_speed_active;
	uint8_t resvd5[48];
} PACK_SUFFIX ib_mlnx_ext_port_info_t;
#include <complib/cl_packoff.h>
/************/

#define FDR10 0x01

typedef uint8_t ib_svc_name_t[64];

#include <complib/cl_packon.h>
typedef struct _ib_service_record {
	ib_net64_t service_id;
	ib_gid_t service_gid;
	ib_net16_t service_pkey;
	ib_net16_t resv;
	ib_net32_t service_lease;
	uint8_t service_key[16];
	ib_svc_name_t service_name;
	uint8_t service_data8[16];
	ib_net16_t service_data16[8];
	ib_net32_t service_data32[4];
	ib_net64_t service_data64[2];
} PACK_SUFFIX ib_service_record_t;
#include <complib/cl_packoff.h>

#include <complib/cl_packon.h>
typedef struct _ib_portinfo_record {
	ib_net16_t lid;
	uint8_t port_num;
	uint8_t options;
	ib_port_info_t port_info;
	uint8_t pad[4];
} PACK_SUFFIX ib_portinfo_record_t;
#include <complib/cl_packoff.h>

#include <complib/cl_packon.h>
typedef struct _ib_portinfoext_record {
	ib_net16_t lid;
	uint8_t port_num;
	uint8_t options;
	ib_port_info_ext_t port_info_ext;
} PACK_SUFFIX ib_portinfoext_record_t;
#include <complib/cl_packoff.h>

#include <complib/cl_packon.h>
typedef struct _ib_link_record {
	ib_net16_t from_lid;
	uint8_t from_port_num;
	uint8_t to_port_num;
	ib_net16_t to_lid;
	uint8_t pad[2];
} PACK_SUFFIX ib_link_record_t;
#include <complib/cl_packoff.h>

#include <complib/cl_packon.h>
typedef struct _ib_sminfo_record {
	ib_net16_t lid;
	uint16_t resv0;
	ib_sm_info_t sm_info;
	uint8_t pad[7];
} PACK_SUFFIX ib_sminfo_record_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_lft_record_t
* NAME
*	ib_lft_record_t
*
* DESCRIPTION
*	IBA defined LinearForwardingTableRecord (15.2.5.6)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_lft_record {
	ib_net16_t lid;
	ib_net16_t block_num;
	uint32_t resv0;
	uint8_t lft[64];
} PACK_SUFFIX ib_lft_record_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_mft_record_t
* NAME
*	ib_mft_record_t
*
* DESCRIPTION
*	IBA defined MulticastForwardingTableRecord (15.2.5.8)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_mft_record {
	ib_net16_t lid;
	ib_net16_t position_block_num;
	uint32_t resv0;
	ib_net16_t mft[IB_MCAST_BLOCK_SIZE];
} PACK_SUFFIX ib_mft_record_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_switch_info_t
* NAME
*	ib_switch_info_t
*
* DESCRIPTION
*	IBA defined SwitchInfo. (14.2.5.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_switch_info {
	ib_net16_t lin_cap;
	ib_net16_t rand_cap;
	ib_net16_t mcast_cap;
	ib_net16_t lin_top;
	uint8_t def_port;
	uint8_t def_mcast_pri_port;
	uint8_t def_mcast_not_port;
	uint8_t life_state;
	ib_net16_t lids_per_port;
	ib_net16_t enforce_cap;
	uint8_t flags;
	uint8_t resvd;
	ib_net16_t mcast_top;
} PACK_SUFFIX ib_switch_info_t;
#include <complib/cl_packoff.h>
/************/

#include <complib/cl_packon.h>
typedef struct _ib_switch_info_record {
	ib_net16_t lid;
	uint16_t resv0;
	ib_switch_info_t switch_info;
} PACK_SUFFIX ib_switch_info_record_t;
#include <complib/cl_packoff.h>

#define IB_SWITCH_PSC 0x04

/****f* IBA Base: Types/ib_switch_info_get_state_change
* NAME
*	ib_switch_info_get_state_change
*
* DESCRIPTION
*	Returns the value of the state change flag.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_switch_info_get_state_change(IN const ib_switch_info_t * const p_si)
{
	return ((p_si->life_state & IB_SWITCH_PSC) == IB_SWITCH_PSC);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*
* RETURN VALUES
*	Returns the value of the state change flag.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_switch_info_clear_state_change
* NAME
*	ib_switch_info_clear_state_change
*
* DESCRIPTION
*	Clears the switch's state change bit.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_switch_info_clear_state_change(IN ib_switch_info_t * const p_si)
{
	p_si->life_state = (uint8_t) (p_si->life_state & 0xFB);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_switch_info_state_change_set
* NAME
*	ib_switch_info_state_change_set
*
* DESCRIPTION
*	Clears the switch's state change bit.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_switch_info_state_change_set(IN ib_switch_info_t * const p_si)
{
	p_si->life_state = (uint8_t) ((p_si->life_state & ~IB_SWITCH_PSC) | IB_SWITCH_PSC);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_switch_info_get_opt_sl2vlmapping
* NAME
*	ib_switch_info_get_state_opt_sl2vlmapping
*
* DESCRIPTION
*       Returns the value of the optimized SLtoVLMapping programming flag.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_switch_info_get_opt_sl2vlmapping(IN const ib_switch_info_t * const p_si)
{
        return ((p_si->life_state & 0x01) == 0x01);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*
* RETURN VALUES
*	Returns the value of the optimized SLtoVLMapping programming flag.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_switch_info_set_life_time
* NAME
*	ib_switch_info_set_life_time
*
* DESCRIPTION
*	Sets the value of LifeTimeValue.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_switch_info_set_life_time(IN ib_switch_info_t * const p_si,
			     IN const uint8_t life_time_val)
{
	p_si->life_state = (p_si->life_state & 0x1f) |
			   (life_time_val << 3);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*	life_time_val
*		[in] LiveTimeValue.
*
* RETURN VALUES
*	None.
*
* NOTES
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_switch_info_is_enhanced_port0
* NAME
*	ib_switch_info_is_enhanced_port0
*
* DESCRIPTION
*	Returns TRUE if the enhancedPort0 bit is on (meaning the switch
*  port zero supports enhanced functions).
*  Returns FALSE otherwise.
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_switch_info_is_enhanced_port0(IN const ib_switch_info_t * const p_si)
{
	return ((p_si->flags & 0x08) == 0x08);
}

/*
* PARAMETERS
*	p_si
*		[in] Pointer to a SwitchInfo attribute.
*
* RETURN VALUES
*	Returns TRUE if the switch supports enhanced port 0. FALSE otherwise.
*
* NOTES
*
* SEE ALSO
*********/

/****s* IBA Base: Types/ib_guid_info_t
* NAME
*	ib_guid_info_t
*
* DESCRIPTION
*	IBA defined GuidInfo. (14.2.5.5)
*
* SYNOPSIS
*/
#define	GUID_TABLE_MAX_ENTRIES		8

#include <complib/cl_packon.h>
typedef struct _ib_guid_info {
	ib_net64_t guid[GUID_TABLE_MAX_ENTRIES];
} PACK_SUFFIX ib_guid_info_t;
#include <complib/cl_packoff.h>
/************/

#include <complib/cl_packon.h>
typedef struct _ib_guidinfo_record {
	ib_net16_t lid;
	uint8_t block_num;
	uint8_t resv;
	uint32_t reserved;
	ib_guid_info_t guid_info;
} PACK_SUFFIX ib_guidinfo_record_t;
#include <complib/cl_packoff.h>

#define IB_MULTIPATH_MAX_GIDS 11	/* Support max that can fit into first MAD (for now) */

#include <complib/cl_packon.h>
typedef struct _ib_multipath_rec_t {
	ib_net32_t hop_flow_raw;
	uint8_t tclass;
	uint8_t num_path;
	ib_net16_t pkey;
	ib_net16_t qos_class_sl;
	uint8_t mtu;
	uint8_t rate;
	uint8_t pkt_life;
	uint8_t service_id_8msb;
	uint8_t independence;	/* formerly resv2 */
	uint8_t sgid_count;
	uint8_t dgid_count;
	uint8_t service_id_56lsb[7];
	ib_gid_t gids[IB_MULTIPATH_MAX_GIDS];
} PACK_SUFFIX ib_multipath_rec_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*       hop_flow_raw
*               Global routing parameters: hop count, flow label and raw bit.
*
*       tclass
*               Another global routing parameter.
*
*       num_path
*     Reversible path - 1 bit to say if path is reversible.
*               num_path [6:0] In queries, maximum number of paths to return.
*               In responses, undefined.
*
*       pkey
*               Partition key (P_Key) to use on this path.
*
*       qos_class_sl
*               QoS class and service level to use on this path.
*
*       mtu
*               MTU and MTU selector fields to use on this path
*       rate
*               Rate and rate selector fields to use on this path.
*
*       pkt_life
*               Packet lifetime
*
*	service_id_8msb
*		8 most significant bits of Service ID
*
*	service_id_56lsb
*		56 least significant bits of Service ID
*
*       preference
*               Indicates the relative merit of this path versus other path
*               records returned from the SA.  Lower numbers are better.
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_multipath_rec_num_path
* NAME
*       ib_multipath_rec_num_path
*
* DESCRIPTION
*       Get max number of paths to return.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_num_path(IN const ib_multipath_rec_t * const p_rec)
{
	return (p_rec->num_path & 0x7F);
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Maximum number of paths to return for each unique SGID_DGID combination.
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_set_sl
* NAME
*	ib_multipath_rec_set_sl
*
* DESCRIPTION
*	Set path service level.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_multipath_rec_set_sl(
	IN ib_multipath_rec_t* const p_rec,
	IN const uint8_t sl )
{
	p_rec->qos_class_sl =
		(p_rec->qos_class_sl & CL_HTON16(IB_MULTIPATH_REC_QOS_CLASS_MASK)) |
			cl_hton16(sl & IB_MULTIPATH_REC_SL_MASK);
}
/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the MultiPath record object.
*
*	sl
*		[in] Service level to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_sl
* NAME
*       ib_multipath_rec_sl
*
* DESCRIPTION
*       Get multipath service level.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_sl(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t) ((cl_ntoh16(p_rec->qos_class_sl)) & IB_MULTIPATH_REC_SL_MASK));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*	SL.
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_set_qos_class
* NAME
*	ib_multipath_rec_set_qos_class
*
* DESCRIPTION
*	Set path QoS class.
*
* SYNOPSIS
*/
static inline void	OSM_API
ib_multipath_rec_set_qos_class(
	IN ib_multipath_rec_t* const p_rec,
	IN const uint16_t qos_class )
{
	p_rec->qos_class_sl =
		(p_rec->qos_class_sl & CL_HTON16(IB_MULTIPATH_REC_SL_MASK)) |
			cl_hton16(qos_class << 4);
}
/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the MultiPath record object.
*
*	qos_class
*		[in] QoS class to set.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_qos_class
* NAME
*	ib_multipath_rec_qos_class
*
* DESCRIPTION
*	Get QoS class.
*
* SYNOPSIS
*/
static inline uint16_t	OSM_API
ib_multipath_rec_qos_class(
	IN	const	ib_multipath_rec_t* const	p_rec )
{
	return (cl_ntoh16( p_rec->qos_class_sl ) >> 4);
}
/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the MultiPath record object.
*
* RETURN VALUES
*	QoS class of the MultiPath record.
*
* NOTES
*
* SEE ALSO
*	ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_mtu
* NAME
*       ib_multipath_rec_mtu
*
* DESCRIPTION
*       Get encoded path MTU.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_mtu(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->mtu & IB_MULTIPATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Encoded path MTU.
*               1: 256
*               2: 512
*               3: 1024
*               4: 2048
*               5: 4096
*               others: reserved
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_mtu_sel
* NAME
*       ib_multipath_rec_mtu_sel
*
* DESCRIPTION
*       Get encoded multipath MTU selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_mtu_sel(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t) ((p_rec->mtu & IB_MULTIPATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Encoded path MTU selector value (for queries).
*               0: greater than MTU specified
*               1: less than MTU specified
*               2: exactly the MTU specified
*               3: largest MTU available
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_rate
* NAME
*	ib_multipath_rec_rate
*
* DESCRIPTION
*	Get encoded multipath rate.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_rate(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->rate & IB_MULTIPATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the multipath record object.
*
* RETURN VALUES
*	Encoded multipath rate.
*		2: 2.5 Gb/sec.
*		3: 10 Gb/sec.
*		4: 30 Gb/sec.
*		5: 5 Gb/sec.
*		6: 20 Gb/sec.
*		7: 40 Gb/sec.
*		8: 60 Gb/sec.
*		9: 80 Gb/sec.
*		10: 120 Gb/sec.
*		11: 14 Gb/sec.
*		12: 56 Gb/sec.
*		13: 112 Gb/sec.
*		14: 168 Gb/sec.
*		15: 25 Gb/sec.
*		16: 100 Gb/sec.
*		17: 200 Gb/sec.
*		18: 300 Gb/sec.
*               others: reserved
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_rate_sel
* NAME
*       ib_multipath_rec_rate_sel
*
* DESCRIPTION
*       Get encoded multipath rate selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_rate_sel(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t)
		((p_rec->rate & IB_MULTIPATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Encoded path rate selector value (for queries).
*               0: greater than rate specified
*               1: less than rate specified
*               2: exactly the rate specified
*               3: largest rate available
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_pkt_life
* NAME
*       ib_multipath_rec_pkt_life
*
* DESCRIPTION
*       Get encoded multipath pkt_life.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_pkt_life(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t) (p_rec->pkt_life & IB_MULTIPATH_REC_BASE_MASK));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Encoded multipath pkt_life = 4.096 usec * 2 ** PacketLifeTime.
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_pkt_life_sel
* NAME
*       ib_multipath_rec_pkt_life_sel
*
* DESCRIPTION
*       Get encoded multipath pkt_lifetime selector.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_multipath_rec_pkt_life_sel(IN const ib_multipath_rec_t * const p_rec)
{
	return ((uint8_t)
		((p_rec->pkt_life & IB_MULTIPATH_REC_SELECTOR_MASK) >> 6));
}

/*
* PARAMETERS
*       p_rec
*               [in] Pointer to the multipath record object.
*
* RETURN VALUES
*       Encoded path pkt_lifetime selector value (for queries).
*               0: greater than rate specified
*               1: less than rate specified
*               2: exactly the rate specified
*               3: smallest packet lifetime available
*
* NOTES
*
* SEE ALSO
*       ib_multipath_rec_t
*********/

/****f* IBA Base: Types/ib_multipath_rec_service_id
* NAME
*	ib_multipath_rec_service_id
*
* DESCRIPTION
*	Get multipath service id.
*
* SYNOPSIS
*/
static inline ib_net64_t OSM_API
ib_multipath_rec_service_id(IN const ib_multipath_rec_t * const p_rec)
{
	union {
		ib_net64_t sid;
		uint8_t sid_arr[8];
	} sid_union;
	sid_union.sid_arr[0] = p_rec->service_id_8msb;
	memcpy(&sid_union.sid_arr[1], p_rec->service_id_56lsb, 7);
	return sid_union.sid;
}

/*
* PARAMETERS
*	p_rec
*		[in] Pointer to the multipath record object.
*
* RETURN VALUES
*	Service ID
*
* NOTES
*
* SEE ALSO
*	ib_multipath_rec_t
*********/

#define IB_NUM_PKEY_ELEMENTS_IN_BLOCK		32
/****s* IBA Base: Types/ib_pkey_table_t
* NAME
*	ib_pkey_table_t
*
* DESCRIPTION
*	IBA defined PKey table. (14.2.5.7)
*
* SYNOPSIS
*/

#include <complib/cl_packon.h>
typedef struct _ib_pkey_table {
	ib_net16_t pkey_entry[IB_NUM_PKEY_ELEMENTS_IN_BLOCK];
} PACK_SUFFIX ib_pkey_table_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_pkey_table_record_t
* NAME
*	ib_pkey_table_record_t
*
* DESCRIPTION
*	IBA defined P_Key Table Record for SA Query. (15.2.5.11)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_pkey_table_record {
	ib_net16_t lid;		// for CA: lid of port, for switch lid of port 0
	ib_net16_t block_num;
	uint8_t port_num;	// for switch: port number, for CA: reserved
	uint8_t reserved1;
	uint16_t reserved2;
	ib_pkey_table_t pkey_tbl;
} PACK_SUFFIX ib_pkey_table_record_t;
#include <complib/cl_packoff.h>
/************/

#define IB_DROP_VL 15
#define IB_MAX_NUM_VLS 16
/****s* IBA Base: Types/ib_slvl_table_t
* NAME
*	ib_slvl_table_t
*
* DESCRIPTION
*	IBA defined SL2VL Mapping Table Attribute. (14.2.5.8)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_slvl_table {
	uint8_t raw_vl_by_sl[IB_MAX_NUM_VLS / 2];
} PACK_SUFFIX ib_slvl_table_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_slvl_table_record_t
* NAME
*	ib_slvl_table_record_t
*
* DESCRIPTION
*	IBA defined SL to VL Mapping Table Record for SA Query. (15.2.5.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_slvl_table_record {
	ib_net16_t lid;		// for CA: lid of port, for switch lid of port 0
	uint8_t in_port_num;	// reserved for CAs
	uint8_t out_port_num;	// reserved for CAs
	uint32_t resv;
	ib_slvl_table_t slvl_tbl;
} PACK_SUFFIX ib_slvl_table_record_t;
#include <complib/cl_packoff.h>
/************/

/****f* IBA Base: Types/ib_slvl_table_set
* NAME
*	ib_slvl_table_set
*
* DESCRIPTION
*	Set slvl table entry.
*
* SYNOPSIS
*/
static inline void OSM_API
ib_slvl_table_set(IN ib_slvl_table_t * p_slvl_tbl,
		  IN uint8_t sl_index, IN uint8_t vl)
{
	uint8_t idx = sl_index / 2;
	CL_ASSERT(vl <= 15);
	CL_ASSERT(sl_index <= 15);

	if (sl_index % 2)
		/* this is an odd sl. Need to update the ls bits */
		p_slvl_tbl->raw_vl_by_sl[idx] =
		    (p_slvl_tbl->raw_vl_by_sl[idx] & 0xF0) | vl;
	else
		/* this is an even sl. Need to update the ms bits */
		p_slvl_tbl->raw_vl_by_sl[idx] =
		    (vl << 4) | (p_slvl_tbl->raw_vl_by_sl[idx] & 0x0F);
}

/*
* PARAMETERS
*	p_slvl_tbl
*		[in] pointer to ib_slvl_table_t object.
*
*	sl_index
*		[in] the sl index in the table to be updated.
*
*	vl
*		[in] the vl value to update for that sl.
*
* RETURN VALUES
*	None
*
* NOTES
*
* SEE ALSO
*	ib_slvl_table_t
*********/

/****f* IBA Base: Types/ib_slvl_table_get
* NAME
*	ib_slvl_table_get
*
* DESCRIPTION
*	Get slvl table entry.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_slvl_table_get(IN const ib_slvl_table_t * p_slvl_tbl, IN uint8_t sl_index)
{
	uint8_t idx = sl_index / 2;
	CL_ASSERT(sl_index <= 15);

	if (sl_index % 2)
		/* this is an odd sl. Need to return the ls bits. */
		return (p_slvl_tbl->raw_vl_by_sl[idx] & 0x0F);
	else
		/* this is an even sl. Need to return the ms bits. */
		return ((p_slvl_tbl->raw_vl_by_sl[idx] & 0xF0) >> 4);
}

/*
* PARAMETERS
*	p_slvl_tbl
*		[in] pointer to ib_slvl_table_t object.
*
*	sl_index
*		[in] the sl index in the table whose value should be returned.
*
* RETURN VALUES
*	vl for the requested sl_index.
*
* NOTES
*
* SEE ALSO
*	ib_slvl_table_t
*********/

/****s* IBA Base: Types/ib_vl_arb_element_t
* NAME
*	ib_vl_arb_element_t
*
* DESCRIPTION
*	IBA defined VL Arbitration Table Element. (14.2.5.9)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_vl_arb_element {
	uint8_t vl;
	uint8_t weight;
} PACK_SUFFIX ib_vl_arb_element_t;
#include <complib/cl_packoff.h>
/************/

#define IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK 32

/****s* IBA Base: Types/ib_vl_arb_table_t
* NAME
*	ib_vl_arb_table_t
*
* DESCRIPTION
*	IBA defined VL Arbitration Table. (14.2.5.9)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_vl_arb_table {
	ib_vl_arb_element_t vl_entry[IB_NUM_VL_ARB_ELEMENTS_IN_BLOCK];
} PACK_SUFFIX ib_vl_arb_table_t;
#include <complib/cl_packoff.h>
/************/

/****s* IBA Base: Types/ib_vl_arb_table_record_t
* NAME
*	ib_vl_arb_table_record_t
*
* DESCRIPTION
*	IBA defined VL Arbitration Table Record for SA Query. (15.2.5.9)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_vl_arb_table_record {
	ib_net16_t lid;		// for CA: lid of port, for switch lid of port 0
	uint8_t port_num;
	uint8_t block_num;
	uint32_t reserved;
	ib_vl_arb_table_t vl_arb_tbl;
} PACK_SUFFIX ib_vl_arb_table_record_t;
#include <complib/cl_packoff.h>
/************/

/*
 *	Global route header information received with unreliable datagram messages
 */
#include <complib/cl_packon.h>
typedef struct _ib_grh {
	ib_net32_t ver_class_flow;
	ib_net16_t resv1;
	uint8_t resv2;
	uint8_t hop_limit;
	ib_gid_t src_gid;
	ib_gid_t dest_gid;
} PACK_SUFFIX ib_grh_t;
#include <complib/cl_packoff.h>

/****f* IBA Base: Types/ib_grh_get_ver_class_flow
* NAME
*	ib_grh_get_ver_class_flow
*
* DESCRIPTION
*	Get encoded version, traffic class and flow label in grh
*
* SYNOPSIS
*/
static inline void OSM_API
ib_grh_get_ver_class_flow(IN const ib_net32_t ver_class_flow,
			  OUT uint8_t * const p_ver,
			  OUT uint8_t * const p_tclass,
			  OUT uint32_t * const p_flow_lbl)
{
	ib_net32_t tmp_ver_class_flow;

	if (p_ver)
		*p_ver = (uint8_t) (ver_class_flow & 0x0f);

	tmp_ver_class_flow = ver_class_flow >> 4;

	if (p_tclass)
		*p_tclass = (uint8_t) (tmp_ver_class_flow & 0xff);

	tmp_ver_class_flow = tmp_ver_class_flow >> 8;

	if (p_flow_lbl)
		*p_flow_lbl = tmp_ver_class_flow & 0xfffff;
}

/*
* PARAMETERS
*	ver_class_flow
*		[in] the version, traffic class and flow label info.
*
* RETURN VALUES
*	p_ver
*		[out] pointer to the version info.
*
*	p_tclass
*		[out] pointer to the traffic class info.
*
*	p_flow_lbl
*		[out] pointer to the flow label info
*
* NOTES
*
* SEE ALSO
*	ib_grh_t
*********/

/****f* IBA Base: Types/ib_grh_set_ver_class_flow
* NAME
*	ib_grh_set_ver_class_flow
*
* DESCRIPTION
*	Set encoded version, traffic class and flow label in grh
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_grh_set_ver_class_flow(IN const uint8_t ver,
			  IN const uint8_t tclass, IN const uint32_t flow_lbl)
{
	ib_net32_t ver_class_flow;

	ver_class_flow = flow_lbl;
	ver_class_flow = ver_class_flow << 8;
	ver_class_flow = ver_class_flow | tclass;
	ver_class_flow = ver_class_flow << 4;
	ver_class_flow = ver_class_flow | ver;
	return (ver_class_flow);
}

/*
* PARAMETERS
*	ver
*		[in] the version info.
*
*	tclass
*		[in] the traffic class info.
*
*	flow_lbl
*		[in] the flow label info
*
* RETURN VALUES
*	ver_class_flow
*		[out] the version, traffic class and flow label info.
*
* NOTES
*
* SEE ALSO
*	ib_grh_t
*********/

/****s* IBA Base: Types/ib_member_rec_t
* NAME
*	ib_member_rec_t
*
* DESCRIPTION
*	Multicast member record, used to create, join, and leave multicast
*	groups.
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_member_rec {
	ib_gid_t mgid;
	ib_gid_t port_gid;
	ib_net32_t qkey;
	ib_net16_t mlid;
	uint8_t mtu;
	uint8_t tclass;
	ib_net16_t pkey;
	uint8_t rate;
	uint8_t pkt_life;
	ib_net32_t sl_flow_hop;
	uint8_t scope_state;
	uint8_t proxy_join:1;
	uint8_t reserved[2];
	uint8_t pad[4];
} PACK_SUFFIX ib_member_rec_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	mgid
*		Multicast GID address for this multicast group.
*
*	port_gid
*		Valid GID of the endpoint joining this multicast group.
*
*	qkey
*		Q_Key to be sued by this multicast group.
*
*	mlid
*		Multicast LID for this multicast group.
*
*	mtu
*		MTU and MTU selector fields to use on this path
*
*	tclass
*		Another global routing parameter.
*
*	pkey
*		Partition key (P_Key) to use for this member.
*
*	rate
*		Rate and rate selector fields to use on this path.
*
*	pkt_life
*		Packet lifetime
*
*	sl_flow_hop
*		Global routing parameters: service level, hop count, and flow label.
*
*	scope_state
*		MGID scope and JoinState of multicast request.
*
*	proxy_join
*		Enables others in the Partition to proxy add/remove from the group
*
* SEE ALSO
*********/

/****f* IBA Base: Types/ib_member_get_sl_flow_hop
* NAME
*	ib_member_get_sl_flow_hop
*
* DESCRIPTION
*	Get encoded sl, flow label, and hop limit
*
* SYNOPSIS
*/
static inline void OSM_API
ib_member_get_sl_flow_hop(IN const ib_net32_t sl_flow_hop,
			  OUT uint8_t * const p_sl,
			  OUT uint32_t * const p_flow_lbl,
			  OUT uint8_t * const p_hop)
{
	uint32_t tmp;

	tmp = cl_ntoh32(sl_flow_hop);
	if (p_hop)
		*p_hop = (uint8_t) tmp;
	tmp >>= 8;

	if (p_flow_lbl)
		*p_flow_lbl = (uint32_t) (tmp & 0xfffff);
	tmp >>= 20;

	if (p_sl)
		*p_sl = (uint8_t) tmp;
}

/*
* PARAMETERS
*	sl_flow_hop
*		[in] the sl, flow label, and hop limit of MC Group
*
* RETURN VALUES
*	p_sl
*		[out] pointer to the service level
*
*	p_flow_lbl
*		[out] pointer to the flow label info
*
*	p_hop
*		[out] pointer to the hop count limit.
*
* NOTES
*
* SEE ALSO
*	ib_member_rec_t
*********/

/****f* IBA Base: Types/ib_member_set_sl_flow_hop
* NAME
*	ib_member_set_sl_flow_hop
*
* DESCRIPTION
*	Set encoded sl, flow label, and hop limit
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_member_set_sl_flow_hop(IN const uint8_t sl,
			  IN const uint32_t flow_label,
			  IN const uint8_t hop_limit)
{
	uint32_t tmp;

	tmp = (sl << 28) | ((flow_label & 0xfffff) << 8) | hop_limit;
	return cl_hton32(tmp);
}

/*
* PARAMETERS
*	sl
*		[in] the service level.
*
*	flow_lbl
*		[in] the flow label info
*
*	hop_limit
*		[in] the hop limit.
*
* RETURN VALUES
*	sl_flow_hop
*		[out] the encoded sl, flow label, and hop limit
*
* NOTES
*
* SEE ALSO
*	ib_member_rec_t
*********/

/****f* IBA Base: Types/ib_member_get_scope_state
* NAME
*	ib_member_get_scope_state
*
* DESCRIPTION
*	Get encoded MGID scope and JoinState
*
* SYNOPSIS
*/
static inline void OSM_API
ib_member_get_scope_state(IN const uint8_t scope_state,
			  OUT uint8_t * const p_scope,
			  OUT uint8_t * const p_state)
{
	uint8_t tmp_scope_state;

	if (p_state)
		*p_state = (uint8_t) (scope_state & 0x0f);

	tmp_scope_state = scope_state >> 4;

	if (p_scope)
		*p_scope = (uint8_t) (tmp_scope_state & 0x0f);

}

/*
* PARAMETERS
*	scope_state
*		[in] the scope and state
*
* RETURN VALUES
*	p_scope
*		[out] pointer to the MGID scope
*
*	p_state
*		[out] pointer to the join state
*
* NOTES
*
* SEE ALSO
*	ib_member_rec_t
*********/

/****f* IBA Base: Types/ib_member_set_scope_state
* NAME
*	ib_member_set_scope_state
*
* DESCRIPTION
*	Set encoded version, MGID scope and JoinState
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_member_set_scope_state(IN const uint8_t scope, IN const uint8_t state)
{
	uint8_t scope_state;

	scope_state = scope;
	scope_state = scope_state << 4;
	scope_state = scope_state | state;
	return (scope_state);
}

/*
* PARAMETERS
*	scope
*		[in] the MGID scope
*
*	state
*		[in] the JoinState
*
* RETURN VALUES
*	scope_state
*		[out] the encoded one
*
* NOTES
*
* SEE ALSO
*	ib_member_rec_t
*********/

/****f* IBA Base: Types/ib_member_set_join_state
* NAME
*	ib_member_set_join_state
*
* DESCRIPTION
*	Set JoinState
*
* SYNOPSIS
*/
static inline void OSM_API
ib_member_set_join_state(IN OUT ib_member_rec_t * p_mc_rec,
			 IN const uint8_t state)
{
	/* keep the scope as it is */
	p_mc_rec->scope_state = (p_mc_rec->scope_state & 0xF0) | (0x0f & state);
}

/*
* PARAMETERS
*	p_mc_rec
*		[in] pointer to the member record
*
*	state
*		[in] the JoinState
*
* RETURN VALUES
*	NONE
*
* NOTES
*
* SEE ALSO
*	ib_member_rec_t
*********/

/*
 * Join State Codes:
 */
#define IB_MC_REC_STATE_FULL_MEMBER 0x01
#define IB_MC_REC_STATE_NON_MEMBER 0x02
#define IB_MC_REC_STATE_SEND_ONLY_NON_MEMBER 0x04

/*
 *	Generic MAD notice types
 */
#define IB_NOTICE_TYPE_FATAL				0x00
#define IB_NOTICE_TYPE_URGENT				0x01
#define IB_NOTICE_TYPE_SECURITY				0x02
#define IB_NOTICE_TYPE_SUBN_MGMT			0x03
#define IB_NOTICE_TYPE_INFO				0x04
#define IB_NOTICE_TYPE_EMPTY				0x7F

#define SM_GID_IN_SERVICE_TRAP				64
#define SM_GID_OUT_OF_SERVICE_TRAP			65
#define SM_MGID_CREATED_TRAP				66
#define SM_MGID_DESTROYED_TRAP				67
#define SM_UNPATH_TRAP					68
#define SM_REPATH_TRAP					69
#define SM_LINK_STATE_CHANGED_TRAP			128
#define SM_LINK_INTEGRITY_THRESHOLD_TRAP		129
#define SM_BUFFER_OVERRUN_THRESHOLD_TRAP		130
#define SM_WATCHDOG_TIMER_EXPIRED_TRAP			131
#define SM_LOCAL_CHANGES_TRAP				144
#define SM_SYS_IMG_GUID_CHANGED_TRAP			145
#define SM_BAD_MKEY_TRAP				256
#define SM_BAD_PKEY_TRAP				257
#define SM_BAD_QKEY_TRAP				258
#define SM_BAD_SWITCH_PKEY_TRAP				259

#include <complib/cl_packon.h>
typedef struct _ib_mad_notice_attr	// Total Size calc  Accumulated
{
	uint8_t generic_type;	// 1                1
	union _notice_g_or_v {
		struct _notice_generic	// 5                6
		{
			uint8_t prod_type_msb;
			ib_net16_t prod_type_lsb;
			ib_net16_t trap_num;
		} PACK_SUFFIX generic;
		struct _notice_vend {
			uint8_t vend_id_msb;
			ib_net16_t vend_id_lsb;
			ib_net16_t dev_id;
		} PACK_SUFFIX vend;
	} g_or_v;
	ib_net16_t issuer_lid;	// 2                 8
	ib_net16_t toggle_count;	// 2                 10
	union _data_details	// 54                64
	{
		struct _raw_data {
			uint8_t details[54];
		} PACK_SUFFIX raw_data;
		struct _ntc_64_67 {
			uint8_t res[6];
			ib_gid_t gid;	// the Node or Multicast Group that came in/out
		} PACK_SUFFIX ntc_64_67;
		struct _ntc_128 {
			ib_net16_t sw_lid;	// the sw lid of which link state changed
		} PACK_SUFFIX ntc_128;
		struct _ntc_129_131 {
			ib_net16_t pad;
			ib_net16_t lid;	// lid and port number of the violation
			uint8_t port_num;
		} PACK_SUFFIX ntc_129_131;
		struct _ntc_144 {
			ib_net16_t pad1;
			ib_net16_t lid;             // lid where change occured
			uint8_t    pad2;            // reserved
			uint8_t    local_changes;   // 7b reserved 1b local changes
			ib_net32_t new_cap_mask;    // new capability mask
			ib_net16_t change_flgs;     // 10b reserved 6b change flags
			ib_net16_t cap_mask2;
		} PACK_SUFFIX ntc_144;
		struct _ntc_145 {
			ib_net16_t pad1;
			ib_net16_t lid;	// lid where sys guid changed
			ib_net16_t pad2;
			ib_net64_t new_sys_guid;	// new system image guid
		} PACK_SUFFIX ntc_145;
		struct _ntc_256 {	// total: 54
			ib_net16_t pad1;	// 2
			ib_net16_t lid;	// 2
			ib_net16_t dr_slid;	// 2
			uint8_t method;	// 1
			uint8_t pad2;	// 1
			ib_net16_t attr_id;	// 2
			ib_net32_t attr_mod;	// 4
			ib_net64_t mkey;	// 8
			uint8_t pad3;	// 1
			uint8_t dr_trunc_hop;	// 1
			uint8_t dr_rtn_path[30];	// 30
		} PACK_SUFFIX ntc_256;
		struct _ntc_257_258	// violation of p/q_key // 49
		{
			ib_net16_t pad1;	// 2
			ib_net16_t lid1;	// 2
			ib_net16_t lid2;	// 2
			ib_net32_t key;	// 4
			ib_net32_t qp1;	// 4b sl, 4b pad, 24b qp1
			ib_net32_t qp2;	// 8b pad, 24b qp2
			ib_gid_t gid1;	// 16
			ib_gid_t gid2;	// 16
		} PACK_SUFFIX ntc_257_258;
		struct _ntc_259	// pkey violation from switch 51
		{
			ib_net16_t data_valid;	// 2
			ib_net16_t lid1;	// 2
			ib_net16_t lid2;	// 2
			ib_net16_t pkey;	// 2
			ib_net32_t sl_qp1; // 4b sl, 4b pad, 24b qp1
			ib_net32_t qp2; // 8b pad, 24b qp2
			ib_gid_t gid1;	// 16
			ib_gid_t gid2;	// 16
			ib_net16_t sw_lid;	// 2
			uint8_t port_no;	// 1
		} PACK_SUFFIX ntc_259;
		struct _ntc_bkey_259	// bkey violation
		{
			ib_net16_t lidaddr;
			uint8_t method;
			uint8_t reserved;
			ib_net16_t attribute_id;
			ib_net32_t attribute_modifier;
			ib_net32_t qp;		// qp is low 24 bits
			ib_net64_t bkey;
			ib_gid_t gid;
		} PACK_SUFFIX ntc_bkey_259;
		struct _ntc_cckey_0	// CC key violation
		{
			ib_net16_t slid;     // source LID from offending packet LRH
			uint8_t method;      // method, from common MAD header
			uint8_t resv0;
			ib_net16_t attribute_id; // Attribute ID, from common MAD header
			ib_net16_t resv1;
			ib_net32_t attribute_modifier; // Attribute Modif, from common MAD header
			ib_net32_t qp;       // 8b pad, 24b dest QP from BTH
			ib_net64_t cc_key;   // CC key of the offending packet
			ib_gid_t source_gid; // GID from GRH of the offending packet
			uint8_t padding[14]; // Padding - ignored on read
		} PACK_SUFFIX ntc_cckey_0;
	} data_details;
	ib_gid_t issuer_gid;	// 16          80
} PACK_SUFFIX ib_mad_notice_attr_t;
#include <complib/cl_packoff.h>

/**
 * Trap 259 masks
 */
#define TRAP_259_MASK_SL (CL_HTON32(0xF0000000))
#define TRAP_259_MASK_QP (CL_HTON32(0x00FFFFFF))

/**
 * Trap 144 masks
 */
#define TRAP_144_MASK_OTHER_LOCAL_CHANGES      0x01
#define TRAP_144_MASK_CAPABILITY_MASK2_CHANGE  (CL_HTON16(0x0020))
#define TRAP_144_MASK_HIERARCHY_INFO_CHANGE    (CL_HTON16(0x0010))
#define TRAP_144_MASK_SM_PRIORITY_CHANGE       (CL_HTON16(0x0008))
#define TRAP_144_MASK_LINK_SPEED_ENABLE_CHANGE (CL_HTON16(0x0004))
#define TRAP_144_MASK_LINK_WIDTH_ENABLE_CHANGE (CL_HTON16(0x0002))
#define TRAP_144_MASK_NODE_DESCRIPTION_CHANGE  (CL_HTON16(0x0001))

/****f* IBA Base: Types/ib_notice_is_generic
* NAME
*	ib_notice_is_generic
*
* DESCRIPTION
*	Check if the notice is generic
*
* SYNOPSIS
*/
static inline boolean_t OSM_API
ib_notice_is_generic(IN const ib_mad_notice_attr_t * p_ntc)
{
	return (p_ntc->generic_type & 0x80);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
* RETURN VALUES
*	TRUE if notice MAD is generic
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_get_type
* NAME
*	ib_notice_get_type
*
* DESCRIPTION
*	Get the notice type
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_notice_get_type(IN const ib_mad_notice_attr_t * p_ntc)
{
	return p_ntc->generic_type & 0x7f;
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to  the notice MAD attribute
*
* RETURN VALUES
*	TRUE if mad is generic
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_get_prod_type
* NAME
*	ib_notice_get_prod_type
*
* DESCRIPTION
*	Get the notice Producer Type of Generic Notice
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_notice_get_prod_type(IN const ib_mad_notice_attr_t * p_ntc)
{
	uint32_t pt;

	pt = cl_ntoh16(p_ntc->g_or_v.generic.prod_type_lsb) |
	    (p_ntc->g_or_v.generic.prod_type_msb << 16);
	return cl_hton32(pt);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
* RETURN VALUES
*	The producer type
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_set_prod_type
* NAME
*	ib_notice_set_prod_type
*
* DESCRIPTION
*	Set the notice Producer Type of Generic Notice
*
* SYNOPSIS
*/
static inline void OSM_API
ib_notice_set_prod_type(IN ib_mad_notice_attr_t * p_ntc,
			IN ib_net32_t prod_type_val)
{
	uint32_t ptv = cl_ntoh32(prod_type_val);
	p_ntc->g_or_v.generic.prod_type_lsb =
	    cl_hton16((uint16_t) (ptv & 0x0000ffff));
	p_ntc->g_or_v.generic.prod_type_msb =
	    (uint8_t) ((ptv & 0x00ff0000) >> 16);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
*  prod_type
*     [in] The producer Type code
*
* RETURN VALUES
*	None
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_set_prod_type_ho
* NAME
*	ib_notice_set_prod_type_ho
*
* DESCRIPTION
*	Set the notice Producer Type of Generic Notice given Host Order
*
* SYNOPSIS
*/
static inline void OSM_API
ib_notice_set_prod_type_ho(IN ib_mad_notice_attr_t * p_ntc,
			   IN uint32_t prod_type_val_ho)
{
	p_ntc->g_or_v.generic.prod_type_lsb =
	    cl_hton16((uint16_t) (prod_type_val_ho & 0x0000ffff));
	p_ntc->g_or_v.generic.prod_type_msb =
	    (uint8_t) ((prod_type_val_ho & 0x00ff0000) >> 16);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
*	prod_type
*		[in] The producer Type code in host order
*
* RETURN VALUES
*	None
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_get_vend_id
* NAME
*	ib_notice_get_vend_id
*
* DESCRIPTION
*	Get the Vendor Id of Vendor type Notice
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_notice_get_vend_id(IN const ib_mad_notice_attr_t * p_ntc)
{
	uint32_t vi;

	vi = cl_ntoh16(p_ntc->g_or_v.vend.vend_id_lsb) |
	    (p_ntc->g_or_v.vend.vend_id_msb << 16);
	return cl_hton32(vi);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
* RETURN VALUES
*	The Vendor Id of Vendor type Notice
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_set_vend_id
* NAME
*	ib_notice_set_vend_id
*
* DESCRIPTION
*	Set the notice Producer Type of Generic Notice
*
* SYNOPSIS
*/
static inline void OSM_API
ib_notice_set_vend_id(IN ib_mad_notice_attr_t * p_ntc, IN ib_net32_t vend_id)
{
	uint32_t vi = cl_ntoh32(vend_id);
	p_ntc->g_or_v.vend.vend_id_lsb =
	    cl_hton16((uint16_t) (vi & 0x0000ffff));
	p_ntc->g_or_v.vend.vend_id_msb = (uint8_t) ((vi & 0x00ff0000) >> 16);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
*	vend_id
*		[in] The producer Type code
*
* RETURN VALUES
*	None
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

/****f* IBA Base: Types/ib_notice_set_vend_id_ho
* NAME
*	ib_notice_set_vend_id_ho
*
* DESCRIPTION
*	Set the notice Producer Type of Generic Notice given a host order value
*
* SYNOPSIS
*/
static inline void OSM_API
ib_notice_set_vend_id_ho(IN ib_mad_notice_attr_t * p_ntc,
			 IN uint32_t vend_id_ho)
{
	p_ntc->g_or_v.vend.vend_id_lsb =
	    cl_hton16((uint16_t) (vend_id_ho & 0x0000ffff));
	p_ntc->g_or_v.vend.vend_id_msb =
	    (uint8_t) ((vend_id_ho & 0x00ff0000) >> 16);
}

/*
* PARAMETERS
*	p_ntc
*		[in] Pointer to the notice MAD attribute
*
*	vend_id_ho
*		[in] The producer Type code in host order
*
* RETURN VALUES
*	None
*
* SEE ALSO
*	ib_mad_notice_attr_t
*********/

#include <complib/cl_packon.h>
typedef struct _ib_inform_info {
	ib_gid_t gid;
	ib_net16_t lid_range_begin;
	ib_net16_t lid_range_end;
	ib_net16_t reserved1;
	uint8_t is_generic;
	uint8_t subscribe;
	ib_net16_t trap_type;
	union _inform_g_or_v {
		struct _inform_generic {
			ib_net16_t trap_num;
			ib_net32_t qpn_resp_time_val;
			uint8_t reserved2;
			uint8_t node_type_msb;
			ib_net16_t node_type_lsb;
		} PACK_SUFFIX generic;
		struct _inform_vend {
			ib_net16_t dev_id;
			ib_net32_t qpn_resp_time_val;
			uint8_t reserved2;
			uint8_t vendor_id_msb;
			ib_net16_t vendor_id_lsb;
		} PACK_SUFFIX vend;
	} PACK_SUFFIX g_or_v;
} PACK_SUFFIX ib_inform_info_t;
#include <complib/cl_packoff.h>

/****f* IBA Base: Types/ib_inform_info_get_qpn_resp_time
* NAME
*	ib_inform_info_get_qpn_resp_time
*
* DESCRIPTION
*	Get QPN of the inform info
*
* SYNOPSIS
*/
static inline void OSM_API
ib_inform_info_get_qpn_resp_time(IN const ib_net32_t qpn_resp_time_val,
				 OUT ib_net32_t * const p_qpn,
				 OUT uint8_t * const p_resp_time_val)
{
	uint32_t tmp = cl_ntoh32(qpn_resp_time_val);

	if (p_qpn)
		*p_qpn = cl_hton32((tmp & 0xffffff00) >> 8);
	if (p_resp_time_val)
		*p_resp_time_val = (uint8_t) (tmp & 0x0000001f);
}

/*
* PARAMETERS
*	qpn_resp_time_val
*		[in] the  qpn and resp time val from the mad
*
* RETURN VALUES
*	p_qpn
*		[out] pointer to the qpn
*
*	p_state
*		[out] pointer to the resp time val
*
* NOTES
*
* SEE ALSO
*	ib_inform_info_t
*********/

/****f* IBA Base: Types/ib_inform_info_set_qpn
* NAME
*	ib_inform_info_set_qpn
*
* DESCRIPTION
*	Set the QPN of the inform info
*
* SYNOPSIS
*/
static inline void OSM_API
ib_inform_info_set_qpn(IN ib_inform_info_t * p_ii, IN ib_net32_t const qpn)
{
	uint32_t tmp = cl_ntoh32(p_ii->g_or_v.generic.qpn_resp_time_val);
	uint32_t qpn_h = cl_ntoh32(qpn);

	p_ii->g_or_v.generic.qpn_resp_time_val =
	    cl_hton32((tmp & 0x000000ff) | ((qpn_h << 8) & 0xffffff00)
	    );
}

/*
* PARAMETERS
*
* NOTES
*
* SEE ALSO
*	ib_inform_info_t
*********/

/****f* IBA Base: Types/ib_inform_info_get_prod_type
* NAME
*	ib_inform_info_get_prod_type
*
* DESCRIPTION
*	Get Producer Type of the Inform Info
*	13.4.8.3 InformInfo
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_inform_info_get_prod_type(IN const ib_inform_info_t * p_inf)
{
	uint32_t nt;

	nt = cl_ntoh16(p_inf->g_or_v.generic.node_type_lsb) |
	    (p_inf->g_or_v.generic.node_type_msb << 16);
	return cl_hton32(nt);
}

/*
* PARAMETERS
*	p_inf
*		[in] pointer to an inform info
*
* RETURN VALUES
*     The producer type
*
* NOTES
*
* SEE ALSO
*	ib_inform_info_t
*********/

/****f* IBA Base: Types/ib_inform_info_get_vend_id
* NAME
*	ib_inform_info_get_vend_id
*
* DESCRIPTION
*	Get Node Type of the Inform Info
*
* SYNOPSIS
*/
static inline ib_net32_t OSM_API
ib_inform_info_get_vend_id(IN const ib_inform_info_t * p_inf)
{
	uint32_t vi;

	vi = cl_ntoh16(p_inf->g_or_v.vend.vendor_id_lsb) |
	    (p_inf->g_or_v.vend.vendor_id_msb << 16);
	return cl_hton32(vi);
}

/*
* PARAMETERS
*	p_inf
*		[in] pointer to an inform info
*
* RETURN VALUES
*     The node type
*
* NOTES
*
* SEE ALSO
*	ib_inform_info_t
*********/

/****s* IBA Base: Types/ib_inform_info_record_t
* NAME
*	ib_inform_info_record_t
*
* DESCRIPTION
*	IBA defined InformInfo Record. (15.2.5.12)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_inform_info_record {
	ib_gid_t subscriber_gid;
	ib_net16_t subscriber_enum;
	uint8_t reserved[6];
	ib_inform_info_t inform_info;
	uint8_t pad[4];
} PACK_SUFFIX ib_inform_info_record_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_perfmgt_mad_t
* NAME
*	ib_perfmgt_mad_t
*
* DESCRIPTION
*	IBA defined Perf Management MAD (16.3.1)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_perfmgt_mad {
	ib_mad_t header;
	uint8_t resv[40];
#define	IB_PM_DATA_SIZE		192
	uint8_t data[IB_PM_DATA_SIZE];
} PACK_SUFFIX ib_perfmgt_mad_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	header
*		Common MAD header.
*
*	resv
*		Reserved.
*
*	data
*		Performance Management payload.  The structure and content of this field
*		depends upon the method, attr_id, and attr_mod fields in the header.
*
* SEE ALSO
* ib_mad_t
*********/

/****s* IBA Base: Types/ib_port_counters
* NAME
*	ib_port_counters_t
*
* DESCRIPTION
*	IBA defined PortCounters Attribute. (16.1.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_counters {
	uint8_t reserved;
	uint8_t port_select;
	ib_net16_t counter_select;
	ib_net16_t symbol_err_cnt;
	uint8_t link_err_recover;
	uint8_t link_downed;
	ib_net16_t rcv_err;
	ib_net16_t rcv_rem_phys_err;
	ib_net16_t rcv_switch_relay_err;
	ib_net16_t xmit_discards;
	uint8_t xmit_constraint_err;
	uint8_t rcv_constraint_err;
	uint8_t counter_select2;
	uint8_t link_int_buffer_overrun;
	ib_net16_t qp1_dropped;
	ib_net16_t vl15_dropped;
	ib_net32_t xmit_data;
	ib_net32_t rcv_data;
	ib_net32_t xmit_pkts;
	ib_net32_t rcv_pkts;
	ib_net32_t xmit_wait;
} PACK_SUFFIX ib_port_counters_t;
#include <complib/cl_packoff.h>

#define PC_LINK_INT(integ_buf_over) ((integ_buf_over & 0xF0) >> 4)
#define PC_BUF_OVERRUN(integ_buf_over) (integ_buf_over & 0x0F)

/****s* IBA Base: Types/ib_port_counters_ext
* NAME
*	ib_port_counters_ext_t
*
* DESCRIPTION
*	IBA defined PortCounters Extended Attribute. (16.1.4.11)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_counters_ext {
	uint8_t reserved;
	uint8_t port_select;
	ib_net16_t counter_select;
	ib_net32_t counter_select2;
	ib_net64_t xmit_data;
	ib_net64_t rcv_data;
	ib_net64_t xmit_pkts;
	ib_net64_t rcv_pkts;
	ib_net64_t unicast_xmit_pkts;
	ib_net64_t unicast_rcv_pkts;
	ib_net64_t multicast_xmit_pkts;
	ib_net64_t multicast_rcv_pkts;
	ib_net64_t symbol_err_cnt;
	ib_net64_t link_err_recover;
	ib_net64_t link_downed;
	ib_net64_t rcv_err;
	ib_net64_t rcv_rem_phys_err;
	ib_net64_t rcv_switch_relay_err;
	ib_net64_t xmit_discards;
	ib_net64_t xmit_constraint_err;
	ib_net64_t rcv_constraint_err;
	ib_net64_t link_integrity_err;
	ib_net64_t buffer_overrun;
	ib_net64_t vl15_dropped;
	ib_net64_t xmit_wait;
	ib_net64_t qp1_dropped;
} PACK_SUFFIX ib_port_counters_ext_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_port_samples_control
* NAME
*	ib_port_samples_control_t
*
* DESCRIPTION
*	IBA defined PortSamplesControl Attribute. (16.1.3.2)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_samples_control {
	uint8_t op_code;
	uint8_t port_select;
	uint8_t tick;
	uint8_t counter_width;	/* 5 bits res : 3bits counter_width */
	ib_net32_t counter_mask;	/* 2 bits res : 3 bits counter_mask : 27 bits counter_masks_1to9 */
	ib_net16_t counter_mask_10to14;	/* 1 bits res : 15 bits counter_masks_10to14 */
	uint8_t sample_mech;
	uint8_t sample_status;	/* 6 bits res : 2 bits sample_status */
	ib_net64_t option_mask;
	ib_net64_t vendor_mask;
	ib_net32_t sample_start;
	ib_net32_t sample_interval;
	ib_net16_t tag;
	ib_net16_t counter_select0;
	ib_net16_t counter_select1;
	ib_net16_t counter_select2;
	ib_net16_t counter_select3;
	ib_net16_t counter_select4;
	ib_net16_t counter_select5;
	ib_net16_t counter_select6;
	ib_net16_t counter_select7;
	ib_net16_t counter_select8;
	ib_net16_t counter_select9;
	ib_net16_t counter_select10;
	ib_net16_t counter_select11;
	ib_net16_t counter_select12;
	ib_net16_t counter_select13;
	ib_net16_t counter_select14;
} PACK_SUFFIX ib_port_samples_control_t;
#include <complib/cl_packoff.h>

/****d* IBA Base: Types/CounterSelect values
* NAME
*       Counter select values
*
* DESCRIPTION
*	Mandatory counter select values (16.1.3.3)
*
* SYNOPSIS
*/
#define IB_CS_PORT_XMIT_DATA (CL_HTON16(0x0001))
#define IB_CS_PORT_RCV_DATA  (CL_HTON16(0x0002))
#define IB_CS_PORT_XMIT_PKTS (CL_HTON16(0x0003))
#define IB_CS_PORT_RCV_PKTS  (CL_HTON16(0x0004))
#define IB_CS_PORT_XMIT_WAIT (CL_HTON16(0x0005))

/****s* IBA Base: Types/ib_port_samples_result
* NAME
*	ib_port_samples_result_t
*
* DESCRIPTION
*	IBA defined PortSamplesControl Attribute. (16.1.3.2)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_samples_result {
	ib_net16_t tag;
	ib_net16_t sample_status;	/* 14 bits res : 2 bits sample_status */
	ib_net32_t counter0;
	ib_net32_t counter1;
	ib_net32_t counter2;
	ib_net32_t counter3;
	ib_net32_t counter4;
	ib_net32_t counter5;
	ib_net32_t counter6;
	ib_net32_t counter7;
	ib_net32_t counter8;
	ib_net32_t counter9;
	ib_net32_t counter10;
	ib_net32_t counter11;
	ib_net32_t counter12;
	ib_net32_t counter13;
	ib_net32_t counter14;
} PACK_SUFFIX ib_port_samples_result_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_port_xmit_data_sl
* NAME
*	ib_port_xmit_data_sl_t
*
* DESCRIPTION
*       IBA defined PortXmitDataSL Attribute. (A13.6.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_xmit_data_sl {
	uint8_t reserved;
	uint8_t port_select;
	ib_net16_t counter_select;
	ib_net32_t port_xmit_data_sl[16];
	uint8_t resv[124];
} PACK_SUFFIX ib_port_xmit_data_sl_t;
#include <complib/cl_packoff.h>

/****s* IBA Base: Types/ib_port_rcv_data_sl
* NAME
*	ib_port_rcv_data_sl_t
*
* DESCRIPTION
*	IBA defined PortRcvDataSL Attribute. (A13.6.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_port_rcv_data_sl {
	uint8_t reserved;
	uint8_t port_select;
	ib_net16_t counter_select;
	ib_net32_t port_rcv_data_sl[16];
	uint8_t resv[124];
} PACK_SUFFIX ib_port_rcv_data_sl_t;
#include <complib/cl_packoff.h>

/****d* IBA Base: Types/DM_SVC_NAME
* NAME
*	DM_SVC_NAME
*
* DESCRIPTION
*	IBA defined Device Management service name (16.3)
*
* SYNOPSIS
*/
#define	DM_SVC_NAME				"DeviceManager.IBTA"
/*
* SEE ALSO
*********/

/****s* IBA Base: Types/ib_dm_mad_t
* NAME
*	ib_dm_mad_t
*
* DESCRIPTION
*	IBA defined Device Management MAD (16.3.1)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_dm_mad {
	ib_mad_t header;
	uint8_t resv[40];
#define	IB_DM_DATA_SIZE		192
	uint8_t data[IB_DM_DATA_SIZE];
} PACK_SUFFIX ib_dm_mad_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	header
*		Common MAD header.
*
*	resv
*		Reserved.
*
*	data
*		Device Management payload.  The structure and content of this field
*		depend upon the method, attr_id, and attr_mod fields in the header.
*
* SEE ALSO
* ib_mad_t
*********/

/****s* IBA Base: Types/ib_iou_info_t
* NAME
*	ib_iou_info_t
*
* DESCRIPTION
*	IBA defined IO Unit information structure (16.3.3.3)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_iou_info {
	ib_net16_t change_id;
	uint8_t max_controllers;
	uint8_t diag_rom;
#define	IB_DM_CTRL_LIST_SIZE	128
	uint8_t controller_list[IB_DM_CTRL_LIST_SIZE];
#define	IOC_NOT_INSTALLED		0x0
#define	IOC_INSTALLED			0x1
//              Reserved values                         0x02-0xE
#define	SLOT_DOES_NOT_EXIST		0xF
} PACK_SUFFIX ib_iou_info_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	change_id
*		Value incremented, with rollover, by any change to the controller_list.
*
*	max_controllers
*		Number of slots in controller_list.
*
*	diag_rom
*		A byte containing two fields: DiagDeviceID and OptionROM.
*		These fields may be read using the ib_iou_info_diag_dev_id
*		and ib_iou_info_option_rom functions.
*
*	controller_list
*		A series of 4-bit nibbles, with each nibble representing a slot
*		in the IO Unit.  Individual nibbles may be read using the
*		ioc_at_slot function.
*
* SEE ALSO
* ib_dm_mad_t, ib_iou_info_diag_dev_id, ib_iou_info_option_rom, ioc_at_slot
*********/

/****f* IBA Base: Types/ib_iou_info_diag_dev_id
* NAME
*	ib_iou_info_diag_dev_id
*
* DESCRIPTION
*	Returns the DiagDeviceID.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_iou_info_diag_dev_id(IN const ib_iou_info_t * const p_iou_info)
{
	return ((uint8_t) (p_iou_info->diag_rom >> 6 & 1));
}

/*
* PARAMETERS
*	p_iou_info
*		[in] Pointer to the IO Unit information structure.
*
* RETURN VALUES
*	DiagDeviceID field of the IO Unit information.
*
* NOTES
*
* SEE ALSO
*	ib_iou_info_t
*********/

/****f* IBA Base: Types/ib_iou_info_option_rom
* NAME
*	ib_iou_info_option_rom
*
* DESCRIPTION
*	Returns the OptionROM.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ib_iou_info_option_rom(IN const ib_iou_info_t * const p_iou_info)
{
	return ((uint8_t) (p_iou_info->diag_rom >> 7));
}

/*
* PARAMETERS
*	p_iou_info
*		[in] Pointer to the IO Unit information structure.
*
* RETURN VALUES
*	OptionROM field of the IO Unit information.
*
* NOTES
*
* SEE ALSO
*	ib_iou_info_t
*********/

/****f* IBA Base: Types/ioc_at_slot
* NAME
*	ioc_at_slot
*
* DESCRIPTION
*	Returns the IOC value at the specified slot.
*
* SYNOPSIS
*/
static inline uint8_t OSM_API
ioc_at_slot(IN const ib_iou_info_t * const p_iou_info, IN uint8_t slot)
{
	if (slot >= IB_DM_CTRL_LIST_SIZE)
		return SLOT_DOES_NOT_EXIST;
	else
		return (int8_t)
		    ((slot % 2) ?
		     ((p_iou_info->controller_list[slot / 2] & 0xf0) >> 4) :
		     (p_iou_info->controller_list[slot / 2] & 0x0f));
}

/*
* PARAMETERS
*	p_iou_info
*		[in] Pointer to the IO Unit information structure.
*
*	slot
*		[in] Pointer to the IO Unit information structure.
*
* RETURN VALUES
*	OptionROM field of the IO Unit information.
*
* NOTES
*
* SEE ALSO
*	ib_iou_info_t
*********/

/****s* IBA Base: Types/ib_ioc_profile_t
* NAME
*	ib_ioc_profile_t
*
* DESCRIPTION
*	IBA defined IO Controller profile structure (16.3.3.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_ioc_profile {
	ib_net64_t ioc_guid;
	ib_net32_t vend_id;
	ib_net32_t dev_id;
	ib_net16_t dev_ver;
	ib_net16_t resv2;
	ib_net32_t subsys_vend_id;
	ib_net32_t subsys_id;
	ib_net16_t io_class;
	ib_net16_t io_subclass;
	ib_net16_t protocol;
	ib_net16_t protocol_ver;
	ib_net32_t resv3;
	ib_net16_t send_msg_depth;
	uint8_t resv4;
	uint8_t rdma_read_depth;
	ib_net32_t send_msg_size;
	ib_net32_t rdma_size;
	uint8_t ctrl_ops_cap;
#define	CTRL_OPS_CAP_ST		0x01
#define	CTRL_OPS_CAP_SF		0x02
#define	CTRL_OPS_CAP_RT		0x04
#define	CTRL_OPS_CAP_RF		0x08
#define	CTRL_OPS_CAP_WT		0x10
#define	CTRL_OPS_CAP_WF		0x20
#define	CTRL_OPS_CAP_AT		0x40
#define	CTRL_OPS_CAP_AF		0x80
	uint8_t resv5;
	uint8_t num_svc_entries;
#define	MAX_NUM_SVC_ENTRIES	0xff
	uint8_t resv6[9];
#define	CTRL_ID_STRING_LEN	64
	char id_string[CTRL_ID_STRING_LEN];
} PACK_SUFFIX ib_ioc_profile_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	ioc_guid
*		An EUI-64 GUID used to uniquely identify the IO controller.
*
*	vend_id
*		IO controller vendor ID, IEEE format.
*
*	dev_id
*		A number assigned by the vendor to identify the type of controller.
*
*	dev_ver
*		A number assigned by the vendor to identify the divice version.
*
*	subsys_vend_id
*		ID of the vendor of the enclosure, if any, in which the IO controller
*		resides in IEEE format; otherwise zero.
*
*	subsys_id
*		A number identifying the subsystem where the controller resides.
*
*	io_class
*		0x0000 - 0xfffe = reserved for IO classes encompased by InfiniBand
*		Architecture.  0xffff = Vendor specific.
*
*	io_subclass
*		0x0000 - 0xfffe = reserved for IO subclasses encompased by InfiniBand
*		Architecture.  0xffff = Vendor specific.  This shall be set to 0xfff
*		if the io_class component is 0xffff.
*
*	protocol
*		0x0000 - 0xfffe = reserved for IO subclasses encompased by InfiniBand
*		Architecture.  0xffff = Vendor specific.  This shall be set to 0xfff
*		if the io_class component is 0xffff.
*
*	protocol_ver
*		Protocol specific.
*
*	send_msg_depth
*		Maximum depth of the send message queue.
*
*	rdma_read_depth
*		Maximum depth of the per-channel RDMA read queue.
*
*	send_msg_size
*		Maximum size of send messages.
*
*	ctrl_ops_cap
*		Supported operation types of this IO controller.  A bit set to one
*		for affirmation of supported capability.
*
*	num_svc_entries
*		Number of entries in the service entries table.
*
*	id_string
*		UTF-8 encoded string for identifying the controller to an operator.
*
* SEE ALSO
* ib_dm_mad_t
*********/

static inline uint32_t OSM_API
ib_ioc_profile_get_vend_id(IN const ib_ioc_profile_t * const p_ioc_profile)
{
	return (cl_ntoh32(p_ioc_profile->vend_id) >> 8);
}

static inline void OSM_API
ib_ioc_profile_set_vend_id(IN ib_ioc_profile_t * const p_ioc_profile,
			   IN const uint32_t vend_id)
{
	p_ioc_profile->vend_id = (cl_hton32(vend_id) << 8);
}

/****s* IBA Base: Types/ib_svc_entry_t
* NAME
*	ib_svc_entry_t
*
* DESCRIPTION
*	IBA defined IO Controller service entry structure (16.3.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_svc_entry {
#define	MAX_SVC_ENTRY_NAME_LEN		40
	char name[MAX_SVC_ENTRY_NAME_LEN];
	ib_net64_t id;
} PACK_SUFFIX ib_svc_entry_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	name
*		UTF-8 encoded, null-terminated name of the service.
*
*	id
*		An identifier of the associated Service.
*
* SEE ALSO
* ib_svc_entries_t
*********/

/****s* IBA Base: Types/ib_svc_entries_t
* NAME
*	ib_svc_entries_t
*
* DESCRIPTION
*	IBA defined IO Controller service entry array (16.3.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_svc_entries {
#define	SVC_ENTRY_COUNT			4
	ib_svc_entry_t service_entry[SVC_ENTRY_COUNT];
} PACK_SUFFIX ib_svc_entries_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	service_entry
*		An array of IO controller service entries.
*
* SEE ALSO
* ib_dm_mad_t, ib_svc_entry_t
*********/

static inline void OSM_API
ib_dm_get_slot_lo_hi(IN const ib_net32_t slot_lo_hi,
		     OUT uint8_t * const p_slot,
		     OUT uint8_t * const p_lo, OUT uint8_t * const p_hi)
{
	ib_net32_t tmp_slot_lo_hi = CL_NTOH32(slot_lo_hi);

	if (p_slot)
		*p_slot = (uint8_t) ((tmp_slot_lo_hi >> 16) & 0x0f);
	if (p_hi)
		*p_hi = (uint8_t) ((tmp_slot_lo_hi >> 8) & 0xff);
	if (p_lo)
		*p_lo = (uint8_t) ((tmp_slot_lo_hi >> 0) & 0xff);
}

/*
 *	IBA defined information describing an I/O controller
 */
#include <complib/cl_packon.h>
typedef struct _ib_ioc_info {
	ib_net64_t module_guid;
	ib_net64_t iou_guid;
	ib_ioc_profile_t ioc_profile;
	ib_net64_t access_key;
	uint16_t initiators_conf;
	uint8_t resv[38];
} PACK_SUFFIX ib_ioc_info_t;
#include <complib/cl_packoff.h>

/*
 *	The following definitions are shared between the Access Layer and VPD
 */
typedef struct _ib_ca *__ptr64 ib_ca_handle_t;
typedef struct _ib_pd *__ptr64 ib_pd_handle_t;
typedef struct _ib_rdd *__ptr64 ib_rdd_handle_t;
typedef struct _ib_mr *__ptr64 ib_mr_handle_t;
typedef struct _ib_mw *__ptr64 ib_mw_handle_t;
typedef struct _ib_qp *__ptr64 ib_qp_handle_t;
typedef struct _ib_eec *__ptr64 ib_eec_handle_t;
typedef struct _ib_cq *__ptr64 ib_cq_handle_t;
typedef struct _ib_av *__ptr64 ib_av_handle_t;
typedef struct _ib_mcast *__ptr64 ib_mcast_handle_t;

/* Currently for windows branch, use the extended version of ib special verbs struct
	in order to be compliant with Infinicon ib_types; later we'll change it to support
	OpenSM ib_types.h */

#ifndef __WIN__
/****d* Access Layer/ib_api_status_t
* NAME
*	ib_api_status_t
*
* DESCRIPTION
*	Function return codes indicating the success or failure of an API call.
*	Note that success is indicated by the return value IB_SUCCESS, which
*	is always zero.
*
* NOTES
*	IB_VERBS_PROCESSING_DONE is used by UVP library to terminate a verbs call
*	in the pre-ioctl step itself.
*
* SYNOPSIS
*/
typedef enum _ib_api_status_t {
	IB_SUCCESS,
	IB_INSUFFICIENT_RESOURCES,
	IB_INSUFFICIENT_MEMORY,
	IB_INVALID_PARAMETER,
	IB_INVALID_SETTING,
	IB_NOT_FOUND,
	IB_TIMEOUT,
	IB_CANCELED,
	IB_INTERRUPTED,
	IB_INVALID_PERMISSION,
	IB_UNSUPPORTED,
	IB_OVERFLOW,
	IB_MAX_MCAST_QPS_REACHED,
	IB_INVALID_QP_STATE,
	IB_INVALID_EEC_STATE,
	IB_INVALID_APM_STATE,
	IB_INVALID_PORT_STATE,
	IB_INVALID_STATE,
	IB_RESOURCE_BUSY,
	IB_INVALID_PKEY,
	IB_INVALID_LKEY,
	IB_INVALID_RKEY,
	IB_INVALID_MAX_WRS,
	IB_INVALID_MAX_SGE,
	IB_INVALID_CQ_SIZE,
	IB_INVALID_SERVICE_TYPE,
	IB_INVALID_GID,
	IB_INVALID_LID,
	IB_INVALID_GUID,
	IB_INVALID_CA_HANDLE,
	IB_INVALID_AV_HANDLE,
	IB_INVALID_CQ_HANDLE,
	IB_INVALID_EEC_HANDLE,
	IB_INVALID_QP_HANDLE,
	IB_INVALID_PD_HANDLE,
	IB_INVALID_MR_HANDLE,
	IB_INVALID_MW_HANDLE,
	IB_INVALID_RDD_HANDLE,
	IB_INVALID_MCAST_HANDLE,
	IB_INVALID_CALLBACK,
	IB_INVALID_AL_HANDLE,	/* InfiniBand Access Layer */
	IB_INVALID_HANDLE,	/* InfiniBand Access Layer */
	IB_ERROR,		/* InfiniBand Access Layer */
	IB_REMOTE_ERROR,	/* Infiniband Access Layer */
	IB_VERBS_PROCESSING_DONE,	/* See Notes above         */
	IB_INVALID_WR_TYPE,
	IB_QP_IN_TIMEWAIT,
	IB_EE_IN_TIMEWAIT,
	IB_INVALID_PORT,
	IB_NOT_DONE,
	IB_UNKNOWN_ERROR	/* ALWAYS LAST ENUM VALUE! */
} ib_api_status_t;
/*****/

OSM_EXPORT const char *ib_error_str[];

/****f* IBA Base: Types/ib_get_err_str
* NAME
*	ib_get_err_str
*
* DESCRIPTION
*	Returns a string for the specified status value.
*
* SYNOPSIS
*/
static inline const char *OSM_API ib_get_err_str(IN ib_api_status_t status)
{
	if (status > IB_UNKNOWN_ERROR)
		status = IB_UNKNOWN_ERROR;
	return (ib_error_str[status]);
}

/*
* PARAMETERS
*	status
*		[in] status value
*
* RETURN VALUES
*	Pointer to the status description string.
*
* NOTES
*
* SEE ALSO
*********/

/****d* Verbs/ib_async_event_t
* NAME
*	ib_async_event_t -- Async event types
*
* DESCRIPTION
*	This type indicates the reason the async callback was called.
*	The context in the ib_event_rec_t indicates the resource context
*	that associated with the callback.  For example, for IB_AE_CQ_ERROR
*	the context provided during the ib_create_cq is returned in the event.
*
* SYNOPSIS
*/
typedef enum _ib_async_event_t {
	IB_AE_SQ_ERROR = 1,
	IB_AE_SQ_DRAINED,
	IB_AE_RQ_ERROR,
	IB_AE_CQ_ERROR,
	IB_AE_QP_FATAL,
	IB_AE_QP_COMM,
	IB_AE_QP_APM,
	IB_AE_EEC_FATAL,
	IB_AE_EEC_COMM,
	IB_AE_EEC_APM,
	IB_AE_LOCAL_FATAL,
	IB_AE_PKEY_TRAP,
	IB_AE_QKEY_TRAP,
	IB_AE_MKEY_TRAP,
	IB_AE_PORT_TRAP,
	IB_AE_SYSIMG_GUID_TRAP,
	IB_AE_BUF_OVERRUN,
	IB_AE_LINK_INTEGRITY,
	IB_AE_FLOW_CTRL_ERROR,
	IB_AE_BKEY_TRAP,
	IB_AE_QP_APM_ERROR,
	IB_AE_EEC_APM_ERROR,
	IB_AE_WQ_REQ_ERROR,
	IB_AE_WQ_ACCESS_ERROR,
	IB_AE_PORT_ACTIVE,
	IB_AE_PORT_DOWN,
	IB_AE_UNKNOWN		/* ALWAYS LAST ENUM VALUE */
} ib_async_event_t;
/*
* VALUES
*	IB_AE_SQ_ERROR
*		An error occurred when accessing the send queue of the QP or EEC.
*		This event is optional.
*
*	IB_AE_SQ_DRAINED
*		The send queue of the specified QP has completed the outstanding
*		messages in progress when the state change was requested and, if
*		applicable, has received all acknowledgements for those messages.
*
*	IB_AE_RQ_ERROR
*		An error occurred when accessing the receive queue of the QP or EEC.
*		This event is optional.
*
*	IB_AE_CQ_ERROR
*		An error occurred when writing an entry to the CQ.
*
*	IB_AE_QP_FATAL
*		A catastrophic error occurred while accessing or processing the
*		work queue that prevents reporting of completions.
*
*	IB_AE_QP_COMM
*		The first packet has arrived for the receive work queue where the
*		QP is still in the RTR state.
*
*	IB_AE_QP_APM
*		If alternate path migration is supported, this event indicates that
*		the QP connection has migrated to the alternate path.
*
*	IB_AE_EEC_FATAL
*		If reliable datagram service is supported, this event indicates that
*		a catastrophic error occurred while accessing or processing the EEC
*		that prevents reporting of completions.
*
*	IB_AE_EEC_COMM
*		If reliable datagram service is supported, this event indicates that
*		the first packet has arrived for the receive work queue where the
*		EEC is still in the RTR state.
*
*	IB_AE_EEC_APM
*		If reliable datagram service and alternate path migration is supported,
*		this event indicates that the EEC connection has migrated to the
*		alternate path.
*
*	IB_AE_LOCAL_FATAL
*		A catastrophic HCA error occurred which cannot be attributed to
*		any resource; behavior is indeterminate.
*
*	IB_AE_PKEY_TRAP
*		A PKEY violation was detected.  This event is optional.
*
*	IB_AE_QKEY_TRAP
*		A QKEY violation was detected.  This event is optional.
*
*	IB_AE_MKEY_TRAP
*		A MKEY violation was detected.  This event is optional.
*
*	IB_AE_PORT_TRAP
*		A port capability change was detected.  This event is optional.
*
*	IB_AE_SYSIMG_GUID_TRAP
*		If the system image GUID is supported, this event indicates that
*		the system image GUID of this HCA has been changed.  This event
*		is optional.
*
*	IB_AE_BUF_OVERRUN
*		The number of consecutive flow control update periods with at least
*		one overrun error in each period has exceeded the threshold specified
*		in the port info attributes.  This event is optional.
*
*	IB_AE_LINK_INTEGRITY
*		The detection of excessively frequent local physical errors has
*		exceeded the threshold specified in the port info attributes.  This
*		event is optional.
*
*	IB_AE_FLOW_CTRL_ERROR
*		An HCA watchdog timer monitoring the arrival of flow control updates
*		has expired without receiving an update.  This event is optional.
*
*	IB_AE_BKEY_TRAP
*		An BKEY violation was detected.  This event is optional.
*
*	IB_AE_QP_APM_ERROR
*		If alternate path migration is supported, this event indicates that
*		an incoming path migration request to this QP was not accepted.
*
*	IB_AE_EEC_APM_ERROR
*		If reliable datagram service and alternate path migration is supported,
*		this event indicates that an incoming path migration request to this
*		EEC was not accepted.
*
*	IB_AE_WQ_REQ_ERROR
*		An OpCode violation was detected at the responder.
*
*	IB_AE_WQ_ACCESS_ERROR
*		An access violation was detected at the responder.
*
*	IB_AE_PORT_ACTIVE
*		If the port active event is supported, this event is generated
*		when the link becomes active: IB_LINK_ACTIVE.
*
*	IB_AE_PORT_DOWN
*		The link is declared unavailable: IB_LINK_INIT, IB_LINK_ARMED,
*		IB_LINK_DOWN.
*
*	IB_AE_UNKNOWN
*		An unknown error occurred which cannot be attributed to any
*		resource; behavior is indeterminate.
*
*****/

OSM_EXPORT const char *ib_async_event_str[];

/****f* IBA Base: Types/ib_get_async_event_str
* NAME
*	ib_get_async_event_str
*
* DESCRIPTION
*	Returns a string for the specified asynchronous event.
*
* SYNOPSIS
*/
static inline const char *OSM_API
ib_get_async_event_str(IN ib_async_event_t event)
{
	if (event > IB_AE_UNKNOWN)
		event = IB_AE_UNKNOWN;
	return (ib_async_event_str[event]);
}

/*
* PARAMETERS
*	event
*		[in] event value
*
* RETURN VALUES
*	Pointer to the asynchronous event description string.
*
* NOTES
*
* SEE ALSO
*********/

/****s* Verbs/ib_event_rec_t
* NAME
*	ib_event_rec_t -- Async event notification record
*
* DESCRIPTION
*	When an async event callback is made, this structure is passed to indicate
*	the type of event, the source of event that caused it, and the context
*	associated with this event.
*
*	context -- Context of the resource that caused the event.
*		-- ca_context if this is a port/adapter event.
*		-- qp_context if the source is a QP event
*		-- cq_context if the source is a CQ event.
*		-- ee_context if the source is an EE event.
*
* SYNOPSIS
*/
typedef struct _ib_event_rec {
	void *context;
	ib_async_event_t type;
	/* HCA vendor specific event information. */
	uint64_t vendor_specific;
	/* The following structures are valid only for trap types. */
	union _trap {
		struct {
			uint16_t lid;
			ib_net64_t port_guid;
			uint8_t port_num;
			/*
			 * The following structure is valid only for
			 * P_KEY, Q_KEY, and M_KEY violation traps.
			 */
			struct {
				uint8_t sl;
				uint16_t src_lid;
				uint16_t dest_lid;
				union _key {
					uint16_t pkey;
					uint32_t qkey;
					uint64_t mkey;
				} key;
				uint32_t src_qp;
				uint32_t dest_qp;
				ib_gid_t src_gid;
				ib_gid_t dest_gid;
			} violation;
		} info;
		ib_net64_t sysimg_guid;
	} trap;
} ib_event_rec_t;
/*******/

/****d* Access Layer/ib_atomic_t
* NAME
*	ib_atomic_t
*
* DESCRIPTION
*	Indicates atomicity levels supported by an adapter.
*
* SYNOPSIS
*/
typedef enum _ib_atomic_t {
	IB_ATOMIC_NONE,
	IB_ATOMIC_LOCAL,
	IB_ATOMIC_GLOBAL
} ib_atomic_t;
/*
* VALUES
*	IB_ATOMIC_NONE
*		Atomic operations not supported.
*
*	IB_ATOMIC_LOCAL
*		Atomic operations guaranteed between QPs of a single CA.
*
*	IB_ATOMIC_GLOBAL
*		Atomic operations are guaranteed between CA and any other entity
*		in the system.
*****/

/****s* Access Layer/ib_port_cap_t
* NAME
*	ib_port_cap_t
*
* DESCRIPTION
*	Indicates which management agents are currently available on the specified
*	port.
*
* SYNOPSIS
*/
typedef struct _ib_port_cap {
	boolean_t cm;
	boolean_t snmp;
	boolean_t dev_mgmt;
	boolean_t vend;
	boolean_t sm;
	boolean_t sm_disable;
	boolean_t qkey_ctr;
	boolean_t pkey_ctr;
	boolean_t notice;
	boolean_t trap;
	boolean_t apm;
	boolean_t slmap;
	boolean_t pkey_nvram;
	boolean_t mkey_nvram;
	boolean_t sysguid;
	boolean_t dr_notice;
	boolean_t boot_mgmt;
	boolean_t capm_notice;
	boolean_t reinit;
	boolean_t ledinfo;
	boolean_t port_active;
} ib_port_cap_t;
/*****/

/****d* Access Layer/ib_init_type_t
* NAME
*	ib_init_type_t
*
* DESCRIPTION
*	If supported by the HCA, the type of initialization requested by
*	this port before SM moves it to the active or armed state.  If the
*	SM implements reinitialization, it shall set these bits to indicate
*	the type of initialization performed prior to activating the port.
*	Otherwise, these bits shall be set to 0.
*
* SYNOPSIS
*/
typedef uint8_t ib_init_type_t;
#define IB_INIT_TYPE_NO_LOAD			0x01
#define IB_INIT_TYPE_PRESERVE_CONTENT		0x02
#define IB_INIT_TYPE_PRESERVE_PRESENCE		0x04
#define IB_INIT_TYPE_DO_NOT_RESUSCITATE		0x08
/*****/

/****s* Access Layer/ib_port_attr_mod_t
* NAME
*	ib_port_attr_mod_t
*
* DESCRIPTION
*	Port attributes that may be modified.
*
* SYNOPSIS
*/
typedef struct _ib_port_attr_mod {
	ib_port_cap_t cap;
	uint16_t pkey_ctr;
	uint16_t qkey_ctr;
	ib_init_type_t init_type;
	ib_net64_t system_image_guid;
} ib_port_attr_mod_t;
/*
* SEE ALSO
*	ib_port_cap_t
*****/

/****s* Access Layer/ib_port_attr_t
* NAME
*	ib_port_attr_t
*
* DESCRIPTION
*	Information about a port on a given channel adapter.
*
* SYNOPSIS
*/
typedef struct _ib_port_attr {
	ib_net64_t port_guid;
	uint8_t port_num;
	uint8_t mtu;
	uint64_t max_msg_size;
	ib_net16_t lid;
	uint8_t lmc;
	/*
	 * LinkWidthSupported as defined in PortInfo.  Required to calculate
	 * inter-packet delay (a.k.a. static rate).
	 */
	uint8_t link_width_supported;
	uint16_t max_vls;
	ib_net16_t sm_lid;
	uint8_t sm_sl;
	uint8_t link_state;
	ib_init_type_t init_type_reply;	/* Optional */
	/*
	 * subnet_timeout:
	 * The maximum expected subnet propagation delay to reach any port on
	 * the subnet.  This value also determines the rate at which traps can
	 * be generated from this node.
	 *
	 * timeout = 4.096 microseconds * 2^subnet_timeout
	 */
	uint8_t subnet_timeout;
	ib_port_cap_t cap;
	uint16_t pkey_ctr;
	uint16_t qkey_ctr;
	uint16_t num_gids;
	uint16_t num_pkeys;
	/*
	 * Pointers at the end of the structure to allow doing a simple
	 * memory comparison of contents up to the first pointer.
	 */
	ib_gid_t *p_gid_table;
	ib_net16_t *p_pkey_table;
} ib_port_attr_t;
/*
* SEE ALSO
*	uint8_t, ib_port_cap_t, ib_link_states_t
*****/

/****s* Access Layer/ib_ca_attr_t
* NAME
*	ib_ca_attr_t
*
* DESCRIPTION
*	Information about a channel adapter.
*
* SYNOPSIS
*/
typedef struct _ib_ca_attr {
	ib_net64_t ca_guid;
	uint32_t vend_id;
	uint16_t dev_id;
	uint16_t revision;
	uint64_t fw_ver;
	/*
	 * Total size of the ca attributes in bytes
	 */
	uint32_t size;
	uint32_t max_qps;
	uint32_t max_wrs;
	uint32_t max_sges;
	uint32_t max_rd_sges;
	uint32_t max_cqs;
	uint32_t max_cqes;
	uint32_t max_pds;
	uint32_t init_regions;
	uint64_t init_region_size;
	uint32_t init_windows;
	uint32_t max_addr_handles;
	uint32_t max_partitions;
	ib_atomic_t atomicity;
	uint8_t max_qp_resp_res;
	uint8_t max_eec_resp_res;
	uint8_t max_resp_res;
	uint8_t max_qp_init_depth;
	uint8_t max_eec_init_depth;
	uint32_t max_eecs;
	uint32_t max_rdds;
	uint32_t max_ipv6_qps;
	uint32_t max_ether_qps;
	uint32_t max_mcast_grps;
	uint32_t max_mcast_qps;
	uint32_t max_qps_per_mcast_grp;
	uint32_t max_fmr;
	uint32_t max_map_per_fmr;
	/*
	 * local_ack_delay:
	 * Specifies the maximum time interval between the local CA receiving
	 * a message and the transmission of the associated ACK or NAK.
	 *
	 * timeout = 4.096 microseconds * 2^local_ack_delay
	 */
	uint8_t local_ack_delay;
	boolean_t bad_pkey_ctr_support;
	boolean_t bad_qkey_ctr_support;
	boolean_t raw_mcast_support;
	boolean_t apm_support;
	boolean_t av_port_check;
	boolean_t change_primary_port;
	boolean_t modify_wr_depth;
	boolean_t current_qp_state_support;
	boolean_t shutdown_port_capability;
	boolean_t init_type_support;
	boolean_t port_active_event_support;
	boolean_t system_image_guid_support;
	boolean_t hw_agents;
	ib_net64_t system_image_guid;
	uint32_t num_page_sizes;
	uint8_t num_ports;
	uint32_t *p_page_size;
	ib_port_attr_t *p_port_attr;
} ib_ca_attr_t;
/*
* FIELDS
*	ca_guid
*		GUID for this adapter.
*
*	vend_id
*		IEEE vendor ID for this adapter
*
*	dev_id
*		Device ID of this adapter. (typically from PCI device ID)
*
*	revision
*		Revision ID of this adapter
*
*	fw_ver
*		Device Firmware version.
*
*	size
*		Total size in bytes for the HCA attributes.  This size includes total
*		size required for all the variable members of the structure.  If a
*		vendor requires to pass vendor specific fields beyond this structure,
*		the HCA vendor can choose to report a larger size.  If a vendor is
*		reporting extended vendor specific features, they should also provide
*		appropriate access functions to aid with the required interpretation.
*
*	max_qps
*		Maximum number of QP's supported by this HCA.
*
*	max_wrs
*		Maximum number of work requests supported by this HCA.
*
*	max_sges
*		Maximum number of scatter gather elements supported per work request.
*
*	max_rd_sges
*		Maximum number of scatter gather elements supported for READ work
*		requests for a Reliable Datagram QP.  This value must be zero if RD
*		service is not supported.
*
*	max_cqs
*		Maximum number of Completion Queues supported.
*
*	max_cqes
*		Maximum number of CQ elements supported per CQ.
*
*	max_pds
*		Maximum number of protection domains supported.
*
*	init_regions
*		Initial number of memory regions supported.  These are only informative
*		values.  HCA vendors can extended and grow these limits on demand.
*
*	init_region_size
*		Initial limit on the size of the registered memory region.
*
*	init_windows
*		Initial number of window entries supported.
*
*	max_addr_handles
*		Maximum number of address handles supported.
*
*	max_partitions
*		Maximum number of partitions supported.
*
*	atomicity
*		Indicates level of atomic operations supported by this HCA.
*
*	max_qp_resp_res
*	max_eec_resp_res
*		Maximum limit on number of responder resources for incoming RDMA
*		operations, on QPs and EEC's respectively.
*
*	max_resp_res
*		Maximum number of responder resources per HCA, with this HCA used as
*		the target.
*
*	max_qp_init_depth
*	max_eec_init_depth
*		Maximimum initiator depth per QP or EEC for initiating RDMA reads and
*		atomic operations.
*
*	max_eecs
*		Maximimum number of EEC's supported by the HCA.
*
*	max_rdds
*		Maximum number of Reliable datagram domains supported.
*
*	max_ipv6_qps
*	max_ether_qps
*		Maximum number of IPV6 and raw ether QP's supported by this HCA.
*
*	max_mcast_grps
*		Maximum number of multicast groups supported.
*
*	max_mcast_qps
*		Maximum number of QP's that can support multicast operations.
*
*	max_qps_per_mcast_grp
*		Maximum number of multicast QP's per multicast group.
*
*	local_ack_delay
*		Specifies the maximum time interval between the local CA receiving
*		a message and the transmission of the associated ACK or NAK.
*		timeout = 4.096 microseconds * 2^local_ack_delay
*
*	bad_pkey_ctr_support
*	bad_qkey_ctr_support
*		Indicates support for the bad pkey and qkey counters.
*
*	raw_mcast_support
*		Indicates support for raw packet multicast.
*
*	apm_support
*		Indicates support for Automatic Path Migration.
*
*	av_port_check
*		Indicates ability to check port number in address handles.
*
*	change_primary_port
*		Indicates ability to change primary port for a QP or EEC during a
*		SQD->RTS transition.
*
*	modify_wr_depth
*		Indicates ability to modify QP depth during a modify QP operation.
*		Check the verb specification for permitted states.
*
*	current_qp_state_support
*		Indicates ability of the HCA to support the current QP state modifier
*		during a modify QP operation.
*
*	shutdown_port_capability
*		Shutdown port capability support indicator.
*
*	init_type_support
*		Indicates init_type_reply and ability to set init_type is supported.
*
*	port_active_event_support
*		Port active event support indicator.
*
*	system_image_guid_support
*		System image GUID support indicator.
*
*	hw_agents
*		Indicates SMA is implemented in HW.
*
*	system_image_guid
*		Optional system image GUID.  This field is valid only if the
*		system_image_guid_support flag is set.
*
*	num_page_sizes
*		Indicates support for different page sizes supported by the HCA.
*		The variable size array can be obtained from p_page_size.
*
*	num_ports
*		Number of physical ports supported on this HCA.
*
*	p_page_size
*		Array holding different page size supported.
*
*	p_port_attr
*		Array holding port attributes.
*
* NOTES
*	This structure contains the attributes of a channel adapter.  Users must
*	call ib_copy_ca_attr to copy the contents of this structure to a new
*	memory region.
*
* SEE ALSO
*	ib_port_attr_t, ib_atomic_t, ib_copy_ca_attr
*****/

/****f* Access layer/ib_copy_ca_attr
* NAME
*	ib_copy_ca_attr
*
* DESCRIPTION
*	Copies CA attributes.
*
* SYNOPSIS
*/
ib_ca_attr_t *ib_copy_ca_attr(IN ib_ca_attr_t * const p_dest,
			      IN const ib_ca_attr_t * const p_src);
/*
* PARAMETERS
*	p_dest
*		Pointer to the buffer that is the destination of the copy.
*
*	p_src
*		Pointer to the CA attributes to copy.
*
* RETURN VALUE
*	Pointer to the copied CA attributes.
*
* NOTES
*	The buffer pointed to by the p_dest parameter must be at least the size
*	specified in the size field of the buffer pointed to by p_src.
*
* SEE ALSO
*	ib_ca_attr_t, ib_dup_ca_attr, ib_free_ca_attr
*****/

/****s* Access Layer/ib_av_attr_t
* NAME
*	ib_av_attr_t
*
* DESCRIPTION
*	IBA address vector.
*
* SYNOPSIS
*/
typedef struct _ib_av_attr {
	uint8_t port_num;
	uint8_t sl;
	ib_net16_t dlid;
	boolean_t grh_valid;
	ib_grh_t grh;
	uint8_t static_rate;
	uint8_t path_bits;
	struct _av_conn {
		uint8_t path_mtu;
		uint8_t local_ack_timeout;
		uint8_t seq_err_retry_cnt;
		uint8_t rnr_retry_cnt;
	} conn;
} ib_av_attr_t;
/*
* SEE ALSO
*	ib_gid_t
*****/

/****d* Access Layer/ib_qp_type_t
* NAME
*	ib_qp_type_t
*
* DESCRIPTION
*	Indicates the type of queue pair being created.
*
* SYNOPSIS
*/
typedef enum _ib_qp_type {
	IB_QPT_RELIABLE_CONN = 0,	/* Matches CM REQ transport type */
	IB_QPT_UNRELIABLE_CONN = 1,	/* Matches CM REQ transport type */
	IB_QPT_RELIABLE_DGRM = 2,	/* Matches CM REQ transport type */
	IB_QPT_UNRELIABLE_DGRM,
	IB_QPT_QP0,
	IB_QPT_QP1,
	IB_QPT_RAW_IPV6,
	IB_QPT_RAW_ETHER,
	IB_QPT_MAD,		/* InfiniBand Access Layer */
	IB_QPT_QP0_ALIAS,	/* InfiniBand Access Layer */
	IB_QPT_QP1_ALIAS	/* InfiniBand Access Layer */
} ib_qp_type_t;
/*
* VALUES
*	IB_QPT_RELIABLE_CONN
*		Reliable, connected queue pair.
*
*	IB_QPT_UNRELIABLE_CONN
*		Unreliable, connected queue pair.
*
*	IB_QPT_RELIABLE_DGRM
*		Reliable, datagram queue pair.
*
*	IB_QPT_UNRELIABLE_DGRM
*		Unreliable, datagram queue pair.
*
*	IB_QPT_QP0
*		Queue pair 0.
*
*	IB_QPT_QP1
*		Queue pair 1.
*
*	IB_QPT_RAW_DGRM
*		Raw datagram queue pair.
*
*	IB_QPT_RAW_IPV6
*		Raw IP version 6 queue pair.
*
*	IB_QPT_RAW_ETHER
*		Raw Ethernet queue pair.
*
*	IB_QPT_MAD
*		Unreliable, datagram queue pair that will send and receive management
*		datagrams with assistance from the access layer.
*
*	IB_QPT_QP0_ALIAS
*		Alias to queue pair 0.  Aliased QPs can only be created on an aliased
*		protection domain.
*
*	IB_QPT_QP1_ALIAS
*		Alias to queue pair 1.  Aliased QPs can only be created on an aliased
*		protection domain.
*****/

/****d* Access Layer/ib_access_t
* NAME
*	ib_access_t
*
* DESCRIPTION
*	Indicates the type of access is permitted on resources such as QPs,
*	memory regions and memory windows.
*
* SYNOPSIS
*/
typedef uint32_t ib_access_t;
#define IB_AC_RDMA_READ				0x00000001
#define IB_AC_RDMA_WRITE			0x00000002
#define IB_AC_ATOMIC				0x00000004
#define IB_AC_LOCAL_WRITE			0x00000008
#define IB_AC_MW_BIND				0x00000010
/*
* NOTES
*	Users may combine access rights using a bit-wise or operation to specify
*	additional access.  For example: IB_AC_RDMA_READ | IB_AC_RDMA_WRITE grants
*	RDMA read and write access.
*****/

/****d* Access Layer/ib_qp_state_t
* NAME
*	ib_qp_state_t
*
* DESCRIPTION
*	Indicates or sets the state of a queue pair.  The current state of a queue
*	pair is returned through the ib_qp_query call and set via the
*	ib_qp_modify call.
*
* SYNOPSIS
*/
typedef uint32_t ib_qp_state_t;
#define IB_QPS_RESET				0x00000001
#define IB_QPS_INIT				0x00000002
#define IB_QPS_RTR				0x00000004
#define IB_QPS_RTS				0x00000008
#define IB_QPS_SQD				0x00000010
#define IB_QPS_SQD_DRAINING			0x00000030
#define IB_QPS_SQD_DRAINED			0x00000050
#define IB_QPS_SQERR				0x00000080
#define IB_QPS_ERROR				0x00000100
#define IB_QPS_TIME_WAIT			0xDEAD0000	/* InfiniBand Access Layer */
/*****/

/****d* Access Layer/ib_apm_state_t
* NAME
*	ib_apm_state_t
*
* DESCRIPTION
*	The current automatic path migration state of a queue pair
*
* SYNOPSIS
*/
typedef enum _ib_apm_state {
	IB_APM_MIGRATED = 1,
	IB_APM_REARM,
	IB_APM_ARMED
} ib_apm_state_t;
/*****/

/****s* Access Layer/ib_qp_create_t
* NAME
*	ib_qp_create_t
*
* DESCRIPTION
*	Attributes used to initialize a queue pair at creation time.
*
* SYNOPSIS
*/
typedef struct _ib_qp_create {
	ib_qp_type_t qp_type;
	ib_rdd_handle_t h_rdd;
	uint32_t sq_depth;
	uint32_t rq_depth;
	uint32_t sq_sge;
	uint32_t rq_sge;
	ib_cq_handle_t h_sq_cq;
	ib_cq_handle_t h_rq_cq;
	boolean_t sq_signaled;
} ib_qp_create_t;
/*
* FIELDS
*	type
*		Specifies the type of queue pair to create.
*
*	h_rdd
*		A handle to a reliable datagram domain to associate with the queue
*		pair.  This field is ignored if the queue pair is not a reliable
*		datagram type queue pair.
*
*	sq_depth
*		Indicates the requested maximum number of work requests that may be
*		outstanding on the queue pair's send queue.  This value must be less
*		than or equal to the maximum reported by the channel adapter associated
*		with the queue pair.
*
*	rq_depth
*		Indicates the requested maximum number of work requests that may be
*		outstanding on the queue pair's receive queue.  This value must be less
*		than or equal to the maximum reported by the channel adapter associated
*		with the queue pair.
*
*	sq_sge
*		Indicates the maximum number scatter-gather elements that may be
*		given in a send work request.  This value must be less
*		than or equal to the maximum reported by the channel adapter associated
*		with the queue pair.
*
*	rq_sge
*		Indicates the maximum number scatter-gather elements that may be
*		given in a receive work request.  This value must be less
*		than or equal to the maximum reported by the channel adapter associated
*		with the queue pair.
*
*	h_sq_cq
*		A handle to the completion queue that will be used to report send work
*		request completions.  This handle must be NULL if the type is
*		IB_QPT_MAD, IB_QPT_QP0_ALIAS, or IB_QPT_QP1_ALIAS.
*
*	h_rq_cq
*		A handle to the completion queue that will be used to report receive
*		work request completions.  This handle must be NULL if the type is
*		IB_QPT_MAD, IB_QPT_QP0_ALIAS, or IB_QPT_QP1_ALIAS.
*
*	sq_signaled
*		A flag that is used to indicate whether the queue pair will signal
*		an event upon completion of a send work request.  If set to
*		TRUE, send work requests will always generate a completion
*		event.  If set to FALSE, a completion event will only be
*		generated if the send_opt field of the send work request has the
*		IB_SEND_OPT_SIGNALED flag set.
*
* SEE ALSO
*	ib_qp_type_t, ib_qp_attr_t
*****/

/****s* Access Layer/ib_qp_attr_t
* NAME
*	ib_qp_attr_t
*
* DESCRIPTION
*	Queue pair attributes returned through ib_query_qp.
*
* SYNOPSIS
*/
typedef struct _ib_qp_attr {
	ib_pd_handle_t h_pd;
	ib_qp_type_t qp_type;
	ib_access_t access_ctrl;
	uint16_t pkey_index;
	uint32_t sq_depth;
	uint32_t rq_depth;
	uint32_t sq_sge;
	uint32_t rq_sge;
	uint8_t init_depth;
	uint8_t resp_res;
	ib_cq_handle_t h_sq_cq;
	ib_cq_handle_t h_rq_cq;
	ib_rdd_handle_t h_rdd;
	boolean_t sq_signaled;
	ib_qp_state_t state;
	ib_net32_t num;
	ib_net32_t dest_num;
	ib_net32_t qkey;
	ib_net32_t sq_psn;
	ib_net32_t rq_psn;
	uint8_t primary_port;
	uint8_t alternate_port;
	ib_av_attr_t primary_av;
	ib_av_attr_t alternate_av;
	ib_apm_state_t apm_state;
} ib_qp_attr_t;
/*
* FIELDS
*	h_pd
*		This is a handle to a protection domain associated with the queue
*		pair, or NULL if the queue pair is type IB_QPT_RELIABLE_DGRM.
*
* NOTES
*	Other fields are defined by the Infiniband specification.
*
* SEE ALSO
*	ib_qp_type_t, ib_access_t, ib_qp_state_t, ib_av_attr_t, ib_apm_state_t
*****/

/****d* Access Layer/ib_qp_opts_t
* NAME
*	ib_qp_opts_t
*
* DESCRIPTION
*	Optional fields supplied in the modify QP operation.
*
* SYNOPSIS
*/
typedef uint32_t ib_qp_opts_t;
#define IB_MOD_QP_ALTERNATE_AV			0x00000001
#define IB_MOD_QP_PKEY				0x00000002
#define IB_MOD_QP_APM_STATE			0x00000004
#define IB_MOD_QP_PRIMARY_AV			0x00000008
#define IB_MOD_QP_RNR_NAK_TIMEOUT		0x00000010
#define IB_MOD_QP_RESP_RES			0x00000020
#define IB_MOD_QP_INIT_DEPTH			0x00000040
#define IB_MOD_QP_PRIMARY_PORT			0x00000080
#define IB_MOD_QP_ACCESS_CTRL			0x00000100
#define IB_MOD_QP_QKEY				0x00000200
#define IB_MOD_QP_SQ_DEPTH			0x00000400
#define IB_MOD_QP_RQ_DEPTH			0x00000800
#define IB_MOD_QP_CURRENT_STATE			0x00001000
#define IB_MOD_QP_RETRY_CNT			0x00002000
#define IB_MOD_QP_LOCAL_ACK_TIMEOUT		0x00004000
#define IB_MOD_QP_RNR_RETRY_CNT			0x00008000
/*
* SEE ALSO
*	ib_qp_mod_t
*****/

/****s* Access Layer/ib_qp_mod_t
* NAME
*	ib_qp_mod_t
*
* DESCRIPTION
*	Information needed to change the state of a queue pair through the
*	ib_modify_qp call.
*
* SYNOPSIS
*/
typedef struct _ib_qp_mod {
	ib_qp_state_t req_state;
	union _qp_state {
		struct _qp_reset {
			/*
			 * Time, in milliseconds, that the QP needs to spend in
			 * the time wait state before being reused.
			 */
			uint32_t timewait;
		} reset;
		struct _qp_init {
			ib_qp_opts_t opts;
			uint8_t primary_port;
			ib_net32_t qkey;
			uint16_t pkey_index;
			ib_access_t access_ctrl;
		} init;
		struct _qp_rtr {
			ib_net32_t rq_psn;
			ib_net32_t dest_qp;
			ib_av_attr_t primary_av;
			uint8_t resp_res;
			ib_qp_opts_t opts;
			ib_av_attr_t alternate_av;
			ib_net32_t qkey;
			uint16_t pkey_index;
			ib_access_t access_ctrl;
			uint32_t sq_depth;
			uint32_t rq_depth;
			uint8_t rnr_nak_timeout;
		} rtr;
		struct _qp_rts {
			ib_net32_t sq_psn;
			uint8_t retry_cnt;
			uint8_t rnr_retry_cnt;
			uint8_t rnr_nak_timeout;
			uint8_t local_ack_timeout;
			uint8_t init_depth;
			ib_qp_opts_t opts;
			ib_qp_state_t current_state;
			ib_net32_t qkey;
			ib_access_t access_ctrl;
			uint8_t resp_res;
			ib_av_attr_t primary_av;
			ib_av_attr_t alternate_av;
			uint32_t sq_depth;
			uint32_t rq_depth;
			ib_apm_state_t apm_state;
			uint8_t primary_port;
			uint16_t pkey_index;
		} rts;
		struct _qp_sqd {
			boolean_t sqd_event;
		} sqd;
	} state;
} ib_qp_mod_t;
/*
* SEE ALSO
*	ib_qp_state_t, ib_access_t, ib_av_attr_t, ib_apm_state_t
*****/

/****s* Access Layer/ib_eec_attr_t
* NAME
*	ib_eec_attr_t
*
* DESCRIPTION
*	Information about an end-to-end context.
*
* SYNOPSIS
*/
typedef struct _ib_eec_attr {
	ib_qp_state_t state;
	ib_rdd_handle_t h_rdd;
	ib_net32_t local_eecn;
	ib_net32_t sq_psn;
	ib_net32_t rq_psn;
	uint8_t primary_port;
	uint16_t pkey_index;
	uint32_t resp_res;
	ib_net32_t remote_eecn;
	uint32_t init_depth;
	uint32_t dest_num;	// ??? What is this?
	ib_av_attr_t primary_av;
	ib_av_attr_t alternate_av;
	ib_apm_state_t apm_state;
} ib_eec_attr_t;
/*
* SEE ALSO
*	ib_qp_state_t, ib_av_attr_t, ib_apm_state_t
*****/

/****d* Access Layer/ib_eec_opts_t
* NAME
*	ib_eec_opts_t
*
* DESCRIPTION
*	Optional fields supplied in the modify EEC operation.
*
* SYNOPSIS
*/
typedef uint32_t ib_eec_opts_t;
#define IB_MOD_EEC_ALTERNATE_AV			0x00000001
#define IB_MOD_EEC_PKEY				0x00000002
#define IB_MOD_EEC_APM_STATE			0x00000004
#define IB_MOD_EEC_PRIMARY_AV			0x00000008
#define IB_MOD_EEC_RNR				0x00000010
#define IB_MOD_EEC_RESP_RES			0x00000020
#define IB_MOD_EEC_OUTSTANDING			0x00000040
#define IB_MOD_EEC_PRIMARY_PORT			0x00000080
/*
* NOTES
*
*
*****/

/****s* Access Layer/ib_eec_mod_t
* NAME
*	ib_eec_mod_t
*
* DESCRIPTION
*	Information needed to change the state of an end-to-end context through
*	the ib_modify_eec function.
*
* SYNOPSIS
*/
typedef struct _ib_eec_mod {
	ib_qp_state_t req_state;
	union _eec_state {
		struct _eec_init {
			uint8_t primary_port;
			uint16_t pkey_index;
		} init;
		struct _eec_rtr {
			ib_net32_t rq_psn;
			ib_net32_t remote_eecn;
			ib_av_attr_t primary_av;
			uint8_t resp_res;
			ib_eec_opts_t opts;
			ib_av_attr_t alternate_av;
			uint16_t pkey_index;
		} rtr;
		struct _eec_rts {
			ib_net32_t sq_psn;
			uint8_t retry_cnt;
			uint8_t rnr_retry_cnt;
			uint8_t local_ack_timeout;
			uint8_t init_depth;
			ib_eec_opts_t opts;
			ib_av_attr_t alternate_av;
			ib_apm_state_t apm_state;
			ib_av_attr_t primary_av;
			uint16_t pkey_index;
			uint8_t primary_port;
		} rts;
		struct _eec_sqd {
			boolean_t sqd_event;
		} sqd;
	} state;
} ib_eec_mod_t;
/*
* SEE ALSO
*	ib_qp_state_t, ib_av_attr_t, ib_apm_state_t
*****/

/****d* Access Layer/ib_wr_type_t
* NAME
*	ib_wr_type_t
*
* DESCRIPTION
*	Identifies the type of work request posted to a queue pair.
*
* SYNOPSIS
*/
typedef enum _ib_wr_type_t {
	WR_SEND = 1,
	WR_RDMA_WRITE,
	WR_RDMA_READ,
	WR_COMPARE_SWAP,
	WR_FETCH_ADD
} ib_wr_type_t;
/*****/

/****s* Access Layer/ib_local_ds_t
* NAME
*	ib_local_ds_t
*
* DESCRIPTION
*	Local data segment information referenced by send and receive work
*	requests.  This is used to specify local data buffers used as part of a
*	work request.
*
* SYNOPSIS
*/
typedef struct _ib_local_ds {
	void *vaddr;
	uint32_t length;
	uint32_t lkey;
} ib_local_ds_t;
/*****/

/****d* Access Layer/ib_send_opt_t
* NAME
*	ib_send_opt_t
*
* DESCRIPTION
*	Optional flags used when posting send work requests.  These flags
*	indicate specific processing for the send operation.
*
* SYNOPSIS
*/
typedef uint32_t ib_send_opt_t;
#define IB_SEND_OPT_IMMEDIATE		0x00000001
#define IB_SEND_OPT_FENCE		0x00000002
#define IB_SEND_OPT_SIGNALED		0x00000004
#define IB_SEND_OPT_SOLICITED		0x00000008
#define IB_SEND_OPT_INLINE		0x00000010
#define IB_SEND_OPT_LOCAL		0x00000020
#define IB_SEND_OPT_VEND_MASK		0xFFFF0000
/*
* VALUES
*	The following flags determine the behavior of a work request when
*	posted to the send side.
*
*	IB_SEND_OPT_IMMEDIATE
*		Send immediate data with the given request.
*
*	IB_SEND_OPT_FENCE
*		The operation is fenced.  Complete all pending send operations
*		before processing this request.
*
*	IB_SEND_OPT_SIGNALED
*		If the queue pair is configured for signaled completion, then
*		generate a completion queue entry when this request completes.
*
*	IB_SEND_OPT_SOLICITED
*		Set the solicited bit on the last packet of this request.
*
*	IB_SEND_OPT_INLINE
*		Indicates that the requested send data should be copied into a VPD
*		owned data buffer.  This flag permits the user to issue send operations
*		without first needing to register the buffer(s) associated with the
*		send operation.  Verb providers that support this operation may place
*		vendor specific restrictions on the size of send operation that may
*		be performed as inline.
*
*
*  IB_SEND_OPT_LOCAL
*     Indicates that a sent MAD request should be given to the local VPD for
*     processing.  MADs sent using this option are not placed on the wire.
*     This send option is only valid for MAD send operations.
*
*
*	IB_SEND_OPT_VEND_MASK
*		This mask indicates bits reserved in the send options that may be used
*		by the verbs provider to indicate vendor specific options.  Bits set
*		in this area of the send options are ignored by the Access Layer, but
*		may have specific meaning to the underlying VPD.
*
*****/

/****s* Access Layer/ib_send_wr_t
* NAME
*	ib_send_wr_t
*
* DESCRIPTION
*	Information used to submit a work request to the send queue of a queue
*	pair.
*
* SYNOPSIS
*/
typedef struct _ib_send_wr {
	struct _ib_send_wr *p_next;
	uint64_t wr_id;
	ib_wr_type_t wr_type;
	ib_send_opt_t send_opt;
	uint32_t num_ds;
	ib_local_ds_t *ds_array;
	ib_net32_t immediate_data;
	union _send_dgrm {
		struct _send_ud {
			ib_net32_t remote_qp;
			ib_net32_t remote_qkey;
			ib_av_handle_t h_av;
		} ud;
		struct _send_rd {
			ib_net32_t remote_qp;
			ib_net32_t remote_qkey;
			ib_net32_t eecn;
		} rd;
		struct _send_raw_ether {
			ib_net16_t dest_lid;
			uint8_t path_bits;
			uint8_t sl;
			uint8_t max_static_rate;
			ib_net16_t ether_type;
		} raw_ether;
		struct _send_raw_ipv6 {
			ib_net16_t dest_lid;
			uint8_t path_bits;
			uint8_t sl;
			uint8_t max_static_rate;
		} raw_ipv6;
	} dgrm;
	struct _send_remote_ops {
		uint64_t vaddr;
		uint32_t rkey;
		ib_net64_t atomic1;
		ib_net64_t atomic2;
	} remote_ops;
} ib_send_wr_t;
/*
* FIELDS
*	p_next
*		A pointer used to chain work requests together.  This permits multiple
*		work requests to be posted to a queue pair through a single function
*		call.  This value is set to NULL to mark the end of the chain.
*
*	wr_id
*		A 64-bit work request identifier that is returned to the consumer
*		as part of the work completion.
*
*	wr_type
*		The type of work request being submitted to the send queue.
*
*	send_opt
*		Optional send control parameters.
*
*	num_ds
*		Number of local data segments specified by this work request.
*
*	ds_array
*		A reference to an array of local data segments used by the send
*		operation.
*
*	immediate_data
*		32-bit field sent as part of a message send or RDMA write operation.
*		This field is only valid if the send_opt flag IB_SEND_OPT_IMMEDIATE
*		has been set.
*
*	dgrm.ud.remote_qp
*		Identifies the destination queue pair of an unreliable datagram send
*		operation.
*
*	dgrm.ud.remote_qkey
*		The qkey for the destination queue pair.
*
*	dgrm.ud.h_av
*		An address vector that specifies the path information used to route
*		the outbound datagram to the destination queue pair.
*
*	dgrm.rd.remote_qp
*		Identifies the destination queue pair of a reliable datagram send
*		operation.
*
*	dgrm.rd.remote_qkey
*		The qkey for the destination queue pair.
*
*	dgrm.rd.eecn
*		The local end-to-end context number to use with the reliable datagram
*		send operation.
*
*	dgrm.raw_ether.dest_lid
*		The destination LID that will receive this raw ether send.
*
*	dgrm.raw_ether.path_bits
*		path bits...
*
*	dgrm.raw_ether.sl
*		service level...
*
*	dgrm.raw_ether.max_static_rate
*		static rate...
*
*	dgrm.raw_ether.ether_type
*		ether type...
*
*	dgrm.raw_ipv6.dest_lid
*		The destination LID that will receive this raw ether send.
*
*	dgrm.raw_ipv6.path_bits
*		path bits...
*
*	dgrm.raw_ipv6.sl
*		service level...
*
*	dgrm.raw_ipv6.max_static_rate
*		static rate...
*
*	remote_ops.vaddr
*		The registered virtual memory address of the remote memory to access
*		with an RDMA or atomic operation.
*
*	remote_ops.rkey
*		The rkey associated with the specified remote vaddr. This data must
*		be presented exactly as obtained from the remote node. No swapping
*		of data must be performed.
*
*	atomic1
*		The first operand for an atomic operation.
*
*	atomic2
*		The second operand for an atomic operation.
*
* NOTES
*	The format of data sent over the fabric is user-defined and is considered
*	opaque to the access layer.  The sole exception to this are MADs posted
*	to a MAD QP service.  MADs are expected to match the format defined by
*	the Infiniband specification and must be in network-byte order when posted
*	to the MAD QP service.
*
* SEE ALSO
*	ib_wr_type_t, ib_local_ds_t, ib_send_opt_t
*****/

/****s* Access Layer/ib_recv_wr_t
* NAME
*	ib_recv_wr_t
*
* DESCRIPTION
*	Information used to submit a work request to the receive queue of a queue
*	pair.
*
* SYNOPSIS
*/
typedef struct _ib_recv_wr {
	struct _ib_recv_wr *p_next;
	uint64_t wr_id;
	uint32_t num_ds;
	ib_local_ds_t *ds_array;
} ib_recv_wr_t;
/*
* FIELDS
*	p_next
*		A pointer used to chain work requests together.  This permits multiple
*		work requests to be posted to a queue pair through a single function
*		call.  This value is set to NULL to mark the end of the chain.
*
*	wr_id
*		A 64-bit work request identifier that is returned to the consumer
*		as part of the work completion.
*
*	num_ds
*		Number of local data segments specified by this work request.
*
*	ds_array
*		A reference to an array of local data segments used by the send
*		operation.
*
* SEE ALSO
*	ib_local_ds_t
*****/

/****s* Access Layer/ib_bind_wr_t
* NAME
*	ib_bind_wr_t
*
* DESCRIPTION
*	Information used to submit a memory window bind work request to the send
*	queue of a queue pair.
*
* SYNOPSIS
*/
typedef struct _ib_bind_wr {
	uint64_t wr_id;
	ib_send_opt_t send_opt;
	ib_mr_handle_t h_mr;
	ib_access_t access_ctrl;
	uint32_t current_rkey;
	ib_local_ds_t local_ds;
} ib_bind_wr_t;
/*
* FIELDS
*	wr_id
*		A 64-bit work request identifier that is returned to the consumer
*		as part of the work completion.
*
*	send_opt
*		Optional send control parameters.
*
*	h_mr
*		Handle to the memory region to which this window is being bound.
*
*	access_ctrl
*		Access rights for this memory window.
*
*	current_rkey
*		The current rkey assigned to this window for remote access.
*
*	local_ds
*		A reference to a local data segment used by the bind operation.
*
* SEE ALSO
*	ib_send_opt_t, ib_access_t, ib_local_ds_t
*****/

/****d* Access Layer/ib_wc_status_t
* NAME
*	ib_wc_status_t
*
* DESCRIPTION
*	Indicates the status of a completed work request.  These VALUES are
*	returned to the user when retrieving completions.  Note that success is
*	identified as IB_WCS_SUCCESS, which is always zero.
*
* SYNOPSIS
*/
typedef enum _ib_wc_status_t {
	IB_WCS_SUCCESS,
	IB_WCS_LOCAL_LEN_ERR,
	IB_WCS_LOCAL_OP_ERR,
	IB_WCS_LOCAL_EEC_OP_ERR,
	IB_WCS_LOCAL_PROTECTION_ERR,
	IB_WCS_WR_FLUSHED_ERR,
	IB_WCS_MEM_WINDOW_BIND_ERR,
	IB_WCS_REM_ACCESS_ERR,
	IB_WCS_REM_OP_ERR,
	IB_WCS_RNR_RETRY_ERR,
	IB_WCS_TIMEOUT_RETRY_ERR,
	IB_WCS_REM_INVALID_REQ_ERR,
	IB_WCS_REM_INVALID_RD_REQ_ERR,
	IB_WCS_INVALID_EECN,
	IB_WCS_INVALID_EEC_STATE,
	IB_WCS_UNMATCHED_RESPONSE,	/* InfiniBand Access Layer */
	IB_WCS_CANCELED,	/* InfiniBand Access Layer */
	IB_WCS_UNKNOWN		/* Must be last. */
} ib_wc_status_t;
/*
* VALUES
*	IB_WCS_SUCCESS
*		Work request completed successfully.
*
*	IB_WCS_MAD
*		The completed work request was associated with a managmenet datagram
*		that requires post processing.  The MAD will be returned to the user
*		through a callback once all post processing has completed.
*
*	IB_WCS_LOCAL_LEN_ERR
*		Generated for a work request posted to the send queue when the
*		total of the data segment lengths exceeds the message length of the
*		channel.  Generated for a work request posted to the receive queue when
*		the total of the data segment lengths is too small for a
*		valid incoming message.
*
*	IB_WCS_LOCAL_OP_ERR
*		An internal QP consistency error was generated while processing this
*		work request.  This may indicate that the QP was in an incorrect state
*		for the requested operation.
*
*	IB_WCS_LOCAL_EEC_OP_ERR
*		An internal EEC consistency error was generated while processing
*		this work request.  This may indicate that the EEC was in an incorrect
*		state for the requested operation.
*
*	IB_WCS_LOCAL_PROTECTION_ERR
*		The data segments of the locally posted work request did not refer to
*		a valid memory region.  The memory may not have been properly
*		registered for the requested operation.
*
*	IB_WCS_WR_FLUSHED_ERR
*		The work request was flushed from the QP before being completed.
*
*	IB_WCS_MEM_WINDOW_BIND_ERR
*		A memory window bind operation failed due to insufficient access
*		rights.
*
*	IB_WCS_REM_ACCESS_ERR,
*		A protection error was detected at the remote node for a RDMA or atomic
*		operation.
*
*	IB_WCS_REM_OP_ERR,
*		The operation could not be successfully completed at the remote node.
*		This may indicate that the remote QP was in an invalid state or
*		contained an invalid work request.
*
*	IB_WCS_RNR_RETRY_ERR,
*		The RNR retry count was exceeded while trying to send this message.
*
*	IB_WCS_TIMEOUT_RETRY_ERR
*		The local transport timeout counter expired while trying to send this
*		message.
*
*	IB_WCS_REM_INVALID_REQ_ERR,
*		The remote node detected an invalid message on the channel.  This error
*		is usually a result of one of the following:
*			- The operation was not supported on receive queue.
*			- There was insufficient buffers to receive a new RDMA request.
*			- There was insufficient buffers to receive a new atomic operation.
*			- An RDMA request was larger than 2^31 bytes.
*
*	IB_WCS_REM_INVALID_RD_REQ_ERR,
*		Responder detected an invalid RD message.  This may be the result of an
*		invalid qkey or an RDD mismatch.
*
*	IB_WCS_INVALID_EECN
*		An invalid EE context number was detected.
*
*	IB_WCS_INVALID_EEC_STATE
*		The EEC was in an invalid state for the specified request.
*
*	IB_WCS_UNMATCHED_RESPONSE
*		A response MAD was received for which there was no matching send.  The
*		send operation may have been canceled by the user or may have timed
*		out.
*
*	IB_WCS_CANCELED
*		The completed work request was canceled by the user.
*****/

OSM_EXPORT const char *ib_wc_status_str[];

/****f* IBA Base: Types/ib_get_wc_status_str
* NAME
*	ib_get_wc_status_str
*
* DESCRIPTION
*	Returns a string for the specified work completion status.
*
* SYNOPSIS
*/
static inline const char *OSM_API
ib_get_wc_status_str(IN ib_wc_status_t wc_status)
{
	if (wc_status > IB_WCS_UNKNOWN)
		wc_status = IB_WCS_UNKNOWN;
	return (ib_wc_status_str[wc_status]);
}

/*
* PARAMETERS
*	wc_status
*		[in] work completion status value
*
* RETURN VALUES
*	Pointer to the work completion status description string.
*
* NOTES
*
* SEE ALSO
*********/

/****d* Access Layer/ib_wc_type_t
* NAME
*	ib_wc_type_t
*
* DESCRIPTION
*	Indicates the type of work completion.
*
* SYNOPSIS
*/
typedef enum _ib_wc_type_t {
	IB_WC_SEND,
	IB_WC_RDMA_WRITE,
	IB_WC_RECV,
	IB_WC_RDMA_READ,
	IB_WC_MW_BIND,
	IB_WC_FETCH_ADD,
	IB_WC_COMPARE_SWAP,
	IB_WC_RECV_RDMA_WRITE
} ib_wc_type_t;
/*****/

/****d* Access Layer/ib_recv_opt_t
* NAME
*	ib_recv_opt_t
*
* DESCRIPTION
*	Indicates optional fields valid in a receive work completion.
*
* SYNOPSIS
*/
typedef uint32_t ib_recv_opt_t;
#define	IB_RECV_OPT_IMMEDIATE		0x00000001
#define IB_RECV_OPT_FORWARD		0x00000002
#define IB_RECV_OPT_GRH_VALID		0x00000004
#define IB_RECV_OPT_VEND_MASK		0xFFFF0000
/*
* VALUES
*	IB_RECV_OPT_IMMEDIATE
*		Indicates that immediate data is valid for this work completion.
*
*	IB_RECV_OPT_FORWARD
*		Indicates that the received trap should be forwarded to the SM.
*
*	IB_RECV_OPT_GRH_VALID
*		Indicates presence of the global route header. When set, the
*		first 40 bytes received are the GRH.
*
*	IB_RECV_OPT_VEND_MASK
*		This mask indicates bits reserved in the receive options that may be
*		used by the verbs provider to indicate vendor specific options.  Bits
*		set in this area of the receive options are ignored by the Access Layer,
*		but may have specific meaning to the underlying VPD.
*****/

/****s* Access Layer/ib_wc_t
* NAME
*	ib_wc_t
*
* DESCRIPTION
*	Work completion information.
*
* SYNOPSIS
*/
typedef struct _ib_wc {
	struct _ib_wc *p_next;
	uint64_t wr_id;
	ib_wc_type_t wc_type;
	uint32_t length;
	ib_wc_status_t status;
	uint64_t vendor_specific;
	union _wc_recv {
		struct _wc_conn {
			ib_recv_opt_t recv_opt;
			ib_net32_t immediate_data;
		} conn;
		struct _wc_ud {
			ib_recv_opt_t recv_opt;
			ib_net32_t immediate_data;
			ib_net32_t remote_qp;
			uint16_t pkey_index;
			ib_net16_t remote_lid;
			uint8_t remote_sl;
			uint8_t path_bits;
		} ud;
		struct _wc_rd {
			ib_net32_t remote_eecn;
			ib_net32_t remote_qp;
			ib_net16_t remote_lid;
			uint8_t remote_sl;
			uint32_t free_cnt;

		} rd;
		struct _wc_raw_ipv6 {
			ib_net16_t remote_lid;
			uint8_t remote_sl;
			uint8_t path_bits;
		} raw_ipv6;
		struct _wc_raw_ether {
			ib_net16_t remote_lid;
			uint8_t remote_sl;
			uint8_t path_bits;
			ib_net16_t ether_type;
		} raw_ether;
	} recv;
} ib_wc_t;
/*
* FIELDS
*	p_next
*		A pointer used to chain work completions.  This permits multiple
*		work completions to be retrieved from a completion queue through a
*		single function call.  This value is set to NULL to mark the end of
*		the chain.
*
*	wr_id
*		The 64-bit work request identifier that was specified when posting the
*		work request.
*
*	wc_type
*		Indicates the type of work completion.
*
*
*	length
*		The total length of the data sent or received with the work request.
*
*	status
*		The result of the work request.
*
*	vendor_specific
*		HCA vendor specific information returned as part of the completion.
*
*	recv.conn.recv_opt
*		Indicates optional fields valid as part of a work request that
*		completed on a connected (reliable or unreliable) queue pair.
*
*	recv.conn.immediate_data
*		32-bit field received as part of an inbound message on a connected
*		queue pair.  This field is only valid if the recv_opt flag
*		IB_RECV_OPT_IMMEDIATE has been set.
*
*	recv.ud.recv_opt
*		Indicates optional fields valid as part of a work request that
*		completed on an unreliable datagram queue pair.
*
*	recv.ud.immediate_data
*		32-bit field received as part of an inbound message on a unreliable
*		datagram queue pair.  This field is only valid if the recv_opt flag
*		IB_RECV_OPT_IMMEDIATE has been set.
*
*	recv.ud.remote_qp
*		Identifies the source queue pair of a received datagram.
*
*	recv.ud.pkey_index
*		The pkey index for the source queue pair. This is valid only for
*		GSI type QP's.
*
*	recv.ud.remote_lid
*		The source LID of the received datagram.
*
*	recv.ud.remote_sl
*		The service level used by the source of the received datagram.
*
*	recv.ud.path_bits
*		path bits...
*
*	recv.rd.remote_eecn
*		The remote end-to-end context number that sent the received message.
*
*	recv.rd.remote_qp
*		Identifies the source queue pair of a received message.
*
*	recv.rd.remote_lid
*		The source LID of the received message.
*
*	recv.rd.remote_sl
*		The service level used by the source of the received message.
*
*	recv.rd.free_cnt
*		The number of available entries in the completion queue.  Reliable
*		datagrams may complete out of order, so this field may be used to
*		determine the number of additional completions that may occur.
*
*	recv.raw_ipv6.remote_lid
*		The source LID of the received message.
*
*	recv.raw_ipv6.remote_sl
*		The service level used by the source of the received message.
*
*	recv.raw_ipv6.path_bits
*		path bits...
*
*	recv.raw_ether.remote_lid
*		The source LID of the received message.
*
*	recv.raw_ether.remote_sl
*		The service level used by the source of the received message.
*
*	recv.raw_ether.path_bits
*		path bits...
*
*	recv.raw_ether.ether_type
*		ether type...
* NOTES
*	When the work request completes with error, the only values that the
*	consumer can depend on are the wr_id field, and the status of the
*	operation.
*
*	If the consumer is using the same CQ for completions from more than
*	one type of QP (i.e Reliable Connected, Datagram etc), then the consumer
*	must have additional information to decide what fields of the union are
*	valid.
* SEE ALSO
*	ib_wc_type_t, ib_qp_type_t, ib_wc_status_t, ib_recv_opt_t
*****/

/****s* Access Layer/ib_mr_create_t
* NAME
*	ib_mr_create_t
*
* DESCRIPTION
*	Information required to create a registered memory region.
*
* SYNOPSIS
*/
typedef struct _ib_mr_create {
	void *vaddr;
	uint64_t length;
	ib_access_t access_ctrl;
} ib_mr_create_t;
/*
* FIELDS
*	vaddr
*		Starting virtual address of the region being registered.
*
*	length
*		Length of the buffer to register.
*
*	access_ctrl
*		Access rights of the registered region.
*
* SEE ALSO
*	ib_access_t
*****/

/****s* Access Layer/ib_phys_create_t
* NAME
*	ib_phys_create_t
*
* DESCRIPTION
*	Information required to create a physical memory region.
*
* SYNOPSIS
*/
typedef struct _ib_phys_create {
	uint64_t length;
	uint32_t num_bufs;
	uint64_t *buf_array;
	uint32_t buf_offset;
	uint32_t page_size;
	ib_access_t access_ctrl;
} ib_phys_create_t;
/*
*	length
*		The length of the memory region in bytes.
*
*	num_bufs
*		Number of buffers listed in the specified buffer array.
*
*	buf_array
*		An array of physical buffers to be registered as a single memory
*		region.
*
*	buf_offset
*		The offset into the first physical page of the specified memory
*		region to start the virtual address.
*
*	page_size
*		The physical page size of the memory being registered.
*
*	access_ctrl
*		Access rights of the registered region.
*
* SEE ALSO
*	ib_access_t
*****/

/****s* Access Layer/ib_mr_attr_t
* NAME
*	ib_mr_attr_t
*
* DESCRIPTION
*	Attributes of a registered memory region.
*
* SYNOPSIS
*/
typedef struct _ib_mr_attr {
	ib_pd_handle_t h_pd;
	void *local_lb;
	void *local_ub;
	void *remote_lb;
	void *remote_ub;
	ib_access_t access_ctrl;
	uint32_t lkey;
	uint32_t rkey;
} ib_mr_attr_t;
/*
* DESCRIPTION
*	h_pd
*		Handle to the protection domain for this memory region.
*
*	local_lb
*		The virtual address of the lower bound of protection for local
*		memory access.
*
*	local_ub
*		The virtual address of the upper bound of protection for local
*		memory access.
*
*	remote_lb
*		The virtual address of the lower bound of protection for remote
*		memory access.
*
*	remote_ub
*		The virtual address of the upper bound of protection for remote
*		memory access.
*
*	access_ctrl
*		Access rights for the specified memory region.
*
*	lkey
*		The lkey associated with this memory region.
*
*	rkey
*		The rkey associated with this memory region.
*
* NOTES
*	The remote_lb, remote_ub, and rkey are only valid if remote memory access
*	is enabled for this memory region.
*
* SEE ALSO
*	ib_access_t
*****/

/****d* Access Layer/ib_ca_mod_t
* NAME
*	ib_ca_mod_t -- Modify port attributes and error counters
*
* DESCRIPTION
*	Specifies modifications to the port attributes of a channel adapter.
*
* SYNOPSIS
*/
typedef uint32_t ib_ca_mod_t;
#define IB_CA_MOD_IS_CM_SUPPORTED		0x00000001
#define IB_CA_MOD_IS_SNMP_SUPPORTED		0x00000002
#define	IB_CA_MOD_IS_DEV_MGMT_SUPPORTED		0x00000004
#define	IB_CA_MOD_IS_VEND_SUPPORTED		0x00000008
#define	IB_CA_MOD_IS_SM				0x00000010
#define IB_CA_MOD_IS_SM_DISABLED		0x00000020
#define IB_CA_MOD_QKEY_CTR			0x00000040
#define IB_CA_MOD_PKEY_CTR			0x00000080
#define IB_CA_MOD_IS_NOTICE_SUPPORTED		0x00000100
#define IB_CA_MOD_IS_TRAP_SUPPORTED		0x00000200
#define IB_CA_MOD_IS_APM_SUPPORTED		0x00000400
#define IB_CA_MOD_IS_SLMAP_SUPPORTED		0x00000800
#define IB_CA_MOD_IS_PKEY_NVRAM_SUPPORTED	0x00001000
#define IB_CA_MOD_IS_MKEY_NVRAM_SUPPORTED	0x00002000
#define IB_CA_MOD_IS_SYSGUID_SUPPORTED		0x00004000
#define IB_CA_MOD_IS_DR_NOTICE_SUPPORTED	0x00008000
#define IB_CA_MOD_IS_BOOT_MGMT_SUPPORTED	0x00010000
#define IB_CA_MOD_IS_CAPM_NOTICE_SUPPORTED	0x00020000
#define IB_CA_MOD_IS_REINIT_SUPORTED		0x00040000
#define IB_CA_MOD_IS_LEDINFO_SUPPORTED		0x00080000
#define IB_CA_MOD_SHUTDOWN_PORT			0x00100000
#define IB_CA_MOD_INIT_TYPE_VALUE		0x00200000
#define IB_CA_MOD_SYSTEM_IMAGE_GUID		0x00400000
/*
* VALUES
*	IB_CA_MOD_IS_CM_SUPPORTED
*		Indicates if there is a communication manager accessible through
*		the port.
*
*	IB_CA_MOD_IS_SNMP_SUPPORTED
*		Indicates if there is an SNMP agent accessible through the port.
*
*	IB_CA_MOD_IS_DEV_MGMT_SUPPORTED
*		Indicates if there is a device management agent accessible
*		through the port.
*
*	IB_CA_MOD_IS_VEND_SUPPORTED
*		Indicates if there is a vendor supported agent accessible
*		through the port.
*
*	IB_CA_MOD_IS_SM
*		Indicates if there is a subnet manager accessible through
*		the port.
*
*	IB_CA_MOD_IS_SM_DISABLED
*		Indicates if the port has been disabled for configuration by the
*		subnet manager.
*
*	IB_CA_MOD_QKEY_CTR
*		Used to reset the qkey violation counter associated with the
*		port.
*
*	IB_CA_MOD_PKEY_CTR
*		Used to reset the pkey violation counter associated with the
*		port.
*
*	IB_CA_MOD_IS_NOTICE_SUPPORTED
*		Indicates that this CA supports ability to generate Notices for
*		Port State changes. (only applicable to switches)
*
*	IB_CA_MOD_IS_TRAP_SUPPORTED
*		Indicates that this management port supports ability to generate
*		trap messages. (only applicable to switches)
*
*	IB_CA_MOD_IS_APM_SUPPORTED
*		Indicates that this port is capable of performing Automatic
*		Path Migration.
*
*	IB_CA_MOD_IS_SLMAP_SUPPORTED
*		Indicates this port supports SLMAP capability.
*
*	IB_CA_MOD_IS_PKEY_NVRAM_SUPPORTED
*		Indicates that PKEY is supported in NVRAM
*
*	IB_CA_MOD_IS_MKEY_NVRAM_SUPPORTED
*		Indicates that MKEY is supported in NVRAM
*
*	IB_CA_MOD_IS_SYSGUID_SUPPORTED
*		Indicates System Image GUID support.
*
*	IB_CA_MOD_IS_DR_NOTICE_SUPPORTED
*		Indicate support for generating Direct Routed Notices
*
*	IB_CA_MOD_IS_BOOT_MGMT_SUPPORTED
*		Indicates support for Boot Management
*
*	IB_CA_MOD_IS_CAPM_NOTICE_SUPPORTED
*		Indicates capability to generate notices for changes to CAPMASK
*
*	IB_CA_MOD_IS_REINIT_SUPORTED
*		Indicates type of node init supported. Refer to Chapter 14 for
*		Initialization actions.
*
*	IB_CA_MOD_IS_LEDINFO_SUPPORTED
*		Indicates support for LED info.
*
*	IB_CA_MOD_SHUTDOWN_PORT
*		Used to modify the port active indicator.
*
*	IB_CA_MOD_INIT_TYPE_VALUE
*		Used to modify the init_type value for the port.
*
*	IB_CA_MOD_SYSTEM_IMAGE_GUID
*		Used to modify the system image GUID for the port.
*****/

/****d* Access Layer/ib_mr_mod_t
* NAME
*	ib_mr_mod_t
*
* DESCRIPTION
*	Mask used to specify which attributes of a registered memory region are
*	being modified.
*
* SYNOPSIS
*/
typedef uint32_t ib_mr_mod_t;
#define IB_MR_MOD_ADDR					0x00000001
#define IB_MR_MOD_PD					0x00000002
#define IB_MR_MOD_ACCESS				0x00000004
/*
* PARAMETERS
*	IB_MEM_MOD_ADDR
*		The address of the memory region is being modified.
*
*	IB_MEM_MOD_PD
*		The protection domain associated with the memory region is being
*		modified.
*
*	IB_MEM_MOD_ACCESS
*		The access rights the memory region are being modified.
*****/

/****d* IBA Base: Constants/IB_SMINFO_ATTR_MOD_HANDOVER
* NAME
*	IB_SMINFO_ATTR_MOD_HANDOVER
*
* DESCRIPTION
*	Encoded attribute modifier value used on SubnSet(SMInfo) SMPs.
*
* SOURCE
*/
#define IB_SMINFO_ATTR_MOD_HANDOVER		(CL_HTON32(0x000001))
/**********/

/****d* IBA Base: Constants/IB_SMINFO_ATTR_MOD_ACKNOWLEDGE
* NAME
*	IB_SMINFO_ATTR_MOD_ACKNOWLEDGE
*
* DESCRIPTION
*	Encoded attribute modifier value used on SubnSet(SMInfo) SMPs.
*
* SOURCE
*/
#define IB_SMINFO_ATTR_MOD_ACKNOWLEDGE		(CL_HTON32(0x000002))
/**********/

/****d* IBA Base: Constants/IB_SMINFO_ATTR_MOD_DISABLE
* NAME
*	IB_SMINFO_ATTR_MOD_DISABLE
*
* DESCRIPTION
*	Encoded attribute modifier value used on SubnSet(SMInfo) SMPs.
*
* SOURCE
*/
#define IB_SMINFO_ATTR_MOD_DISABLE			(CL_HTON32(0x000003))
/**********/

/****d* IBA Base: Constants/IB_SMINFO_ATTR_MOD_STANDBY
* NAME
*	IB_SMINFO_ATTR_MOD_STANDBY
*
* DESCRIPTION
*	Encoded attribute modifier value used on SubnSet(SMInfo) SMPs.
*
* SOURCE
*/
#define IB_SMINFO_ATTR_MOD_STANDBY			(CL_HTON32(0x000004))
/**********/

/****d* IBA Base: Constants/IB_SMINFO_ATTR_MOD_DISCOVER
* NAME
*	IB_SMINFO_ATTR_MOD_DISCOVER
*
* DESCRIPTION
*	Encoded attribute modifier value used on SubnSet(SMInfo) SMPs.
*
* SOURCE
*/
#define IB_SMINFO_ATTR_MOD_DISCOVER			(CL_HTON32(0x000005))
/**********/

/****s* Access Layer/ib_ci_op_t
* NAME
*	ib_ci_op_t
*
* DESCRIPTION
*	A structure used for vendor specific CA interface communication.
*
* SYNOPSIS
*/
typedef struct _ib_ci_op {
	IN uint32_t command;
	IN OUT void *p_buf OPTIONAL;
	IN uint32_t buf_size;
	IN OUT uint32_t num_bytes_ret;
	IN OUT int32_t status;
} ib_ci_op_t;
/*
* FIELDS
*	command
*		A command code that is understood by the verbs provider.
*
*	p_buf
*		A reference to a buffer containing vendor specific data.  The verbs
*		provider must not access pointers in the p_buf between user-mode and
*		kernel-mode.  Any pointers embedded in the p_buf are invalidated by
*		the user-mode/kernel-mode transition.
*
*	buf_size
*		The size of the buffer in bytes.
*
*	num_bytes_ret
*		The size in bytes of the vendor specific data returned in the buffer.
*		This field is set by the verbs provider.  The verbs provider should
*		verify that the buffer size is sufficient to hold the data being
*		returned.
*
*	status
*		The completion status from the verbs provider.  This field should be
*		initialize to indicate an error to allow detection and cleanup in
*		case a communication error occurs between user-mode and kernel-mode.
*
* NOTES
*	This structure is provided to allow the exchange of vendor specific
*	data between the originator and the verbs provider.  Users of this
*	structure are expected to know the format of data in the p_buf based
*	on the structure command field or the usage context.
*****/

/****s* IBA Base: Types/ib_cc_mad_t
* NAME
*	ib_cc_mad_t
*
* DESCRIPTION
*	IBA defined Congestion Control MAD format. (A10.4.1)
*
* SYNOPSIS
*/
#define IB_CC_LOG_DATA_SIZE 32
#define IB_CC_MGT_DATA_SIZE 192
#define IB_CC_MAD_HDR_SIZE (sizeof(ib_sa_mad_t) - IB_CC_LOG_DATA_SIZE \
						- IB_CC_MGT_DATA_SIZE)

#include <complib/cl_packon.h>
typedef struct _ib_cc_mad {
	ib_mad_t header;
	ib_net64_t cc_key;
	uint8_t log_data[IB_CC_LOG_DATA_SIZE];
	uint8_t mgt_data[IB_CC_MGT_DATA_SIZE];
} PACK_SUFFIX ib_cc_mad_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	header
*		Common MAD header.
*
*	cc_key
*		CC_Key of the Congestion Control MAD.
*
*	log_data
*		Congestion Control log data of the CC MAD.
*
*	mgt_data
*		Congestion Control management data of the CC MAD.
*
* SEE ALSO
* ib_mad_t
*********/

/****f* IBA Base: Types/ib_cc_mad_get_cc_key
* NAME
*	ib_cc_mad_get_cc_key
*
* DESCRIPTION
*	Gets a CC_Key of the CC MAD.
*
* SYNOPSIS
*/
static inline ib_net64_t OSM_API
ib_cc_mad_get_cc_key(IN const ib_cc_mad_t * const p_cc_mad)
{
	return p_cc_mad->cc_key;
}
/*
* PARAMETERS
*	p_cc_mad
*		[in] Pointer to the CC MAD packet.
*
* RETURN VALUES
*	CC_Key of the provided CC MAD packet.
*
* NOTES
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****f* IBA Base: Types/ib_cc_mad_get_log_data_ptr
* NAME
*	ib_cc_mad_get_log_data_ptr
*
* DESCRIPTION
*	Gets a pointer to the CC MAD's log data area.
*
* SYNOPSIS
*/
static inline void * OSM_API
ib_cc_mad_get_log_data_ptr(IN const ib_cc_mad_t * const p_cc_mad)
{
	return ((void *)p_cc_mad->log_data);
}
/*
* PARAMETERS
*	p_cc_mad
*		[in] Pointer to the CC MAD packet.
*
* RETURN VALUES
*	Pointer to CC MAD log data area.
*
* NOTES
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****f* IBA Base: Types/ib_cc_mad_get_mgt_data_ptr
* NAME
*	ib_cc_mad_get_mgt_data_ptr
*
* DESCRIPTION
*	Gets a pointer to the CC MAD's management data area.
*
* SYNOPSIS
*/
static inline void * OSM_API
ib_cc_mad_get_mgt_data_ptr(IN const ib_cc_mad_t * const p_cc_mad)
{
	return ((void *)p_cc_mad->mgt_data);
}
/*
* PARAMETERS
*	p_cc_mad
*		[in] Pointer to the CC MAD packet.
*
* RETURN VALUES
*	Pointer to CC MAD management data area.
*
* NOTES
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_cong_info_t
* NAME
*	ib_cong_info_t
*
* DESCRIPTION
*	IBA defined CongestionInfo attribute (A10.4.3.3)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cong_info {
	uint8_t cong_info;
	uint8_t resv;
	uint8_t ctrl_table_cap;
} PACK_SUFFIX ib_cong_info_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	cong_info
*		Congestion control capabilities of the node.
*
*	ctrl_table_cap
*		Number of 64 entry blocks in the CongestionControlTable.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_cong_key_info_t
* NAME
*	ib_cong_key_info_t
*
* DESCRIPTION
*	IBA defined CongestionKeyInfo attribute (A10.4.3.4)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cong_key_info {
	ib_net64_t cc_key;
	ib_net16_t protect_bit;
	ib_net16_t lease_period;
	ib_net16_t violations;
} PACK_SUFFIX ib_cong_key_info_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	cc_key
*		8-byte CC Key.
*
*	protect_bit
*		Bit 0 is a CC Key Protect Bit, other 15 bits are reserved.
*
*	lease_period
*		How long the CC Key protect bit is to remain non-zero.
*
*	violations
*		Number of received MADs that violated CC Key.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_cong_log_event_sw_t
* NAME
*	ib_cong_log_event_sw_t
*
* DESCRIPTION
*	IBA defined CongestionLogEvent (SW) entry (A10.4.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cong_log_event_sw {
	ib_net16_t slid;
	ib_net16_t dlid;
	ib_net32_t sl;
	ib_net32_t time_stamp;
} PACK_SUFFIX ib_cong_log_event_sw_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	slid
*		Source LID of congestion event.
*
*	dlid
*		Destination LID of congestion event.
*
*	sl
*		4 bits - SL of congestion event.
*		rest of the bits are reserved.
*
*	time_stamp
*		Timestamp of congestion event.
*
* SEE ALSO
*	ib_cc_mad_t, ib_cong_log_t
*********/

/****s* IBA Base: Types/ib_cong_log_event_ca_t
* NAME
*	ib_cong_log_event_ca_t
*
* DESCRIPTION
*	IBA defined CongestionLogEvent (CA) entry (A10.4.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cong_log_event_ca {
	ib_net32_t local_qp_resv0;
	ib_net32_t remote_qp_sl_service_type;
	ib_net16_t remote_lid;
	ib_net16_t resv1;
	ib_net32_t time_stamp;
} PACK_SUFFIX ib_cong_log_event_ca_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*	resv0_local_qp
*		bits [31:8] local QP that reached CN threshold.
*		bits [7:0] reserved.
*
*	remote_qp_sl_service_type
*		bits [31:8] remote QP that is connected to local QP.
*		bits [7:4] SL of the local QP.
*		bits [3:0] Service Type of the local QP.
*
*	remote_lid
*		LID of the remote port that is connected to local QP.
*
*	time_stamp
*		Timestamp when threshold reached.
*
* SEE ALSO
*	ib_cc_mad_t, ib_cong_log_t
*********/

/****s* IBA Base: Types/ib_cong_log_t
* NAME
*	ib_cong_log_t
*
* DESCRIPTION
*	IBA defined CongestionLog attribute (A10.4.3.5)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cong_log {
	uint8_t log_type;
	union _log_details
	{
		struct _log_sw {
			uint8_t cong_flags;
			ib_net16_t event_counter;
			ib_net32_t time_stamp;
			uint8_t port_map[32];
			ib_cong_log_event_sw_t entry_list[15];
		} PACK_SUFFIX log_sw;

		struct _log_ca {
			uint8_t cong_flags;
			ib_net16_t event_counter;
			ib_net16_t event_map;
			ib_net16_t resv;
			ib_net32_t time_stamp;
			ib_cong_log_event_ca_t log_event[13];
		} PACK_SUFFIX log_ca;

	} log_details;
} PACK_SUFFIX ib_cong_log_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	log_{sw,ca}.log_type
*		Log type: 0x1 is for Switch, 0x2 is for CA
*
*	log_{sw,ca}.cong_flags
*		Congestion Flags.
*
*	log_{sw,ca}.event_counter
*		Number of events since log last sent.
*
*	log_{sw,ca}.time_stamp
*		Timestamp when log sent.
*
*	log_sw.port_map
*		If a bit set to 1, then the corresponding port
*		has marked packets with a FECN.
*		bits 0 and 255 - reserved
*		bits [254..1] - ports [254..1].
*
*	log_sw.entry_list
*		Array of 13 most recent congestion log events.
*
*	log_ca.event_map
*		array 16 bits, one for each SL.
*
*	log_ca.log_event
*		Array of 13 most recent congestion log events.
*
* SEE ALSO
*	ib_cc_mad_t, ib_cong_log_event_sw_t, ib_cong_log_event_ca_t
*********/

/****s* IBA Base: Types/ib_sw_cong_setting_t
* NAME
*	ib_sw_cong_setting_t
*
* DESCRIPTION
*	IBA defined SwitchCongestionSetting attribute (A10.4.3.6)
*
* SYNOPSIS
*/
#define IB_CC_PORT_MASK_DATA_SIZE 32
#include <complib/cl_packon.h>
typedef struct _ib_sw_cong_setting {
	ib_net32_t control_map;
	uint8_t victim_mask[IB_CC_PORT_MASK_DATA_SIZE];
	uint8_t credit_mask[IB_CC_PORT_MASK_DATA_SIZE];
	uint8_t threshold_resv;
	uint8_t packet_size;
	ib_net16_t cs_threshold_resv;
	ib_net16_t cs_return_delay;
	ib_net16_t marking_rate;
} PACK_SUFFIX ib_sw_cong_setting_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	control_map
*		Indicates which components of this attribute are valid
*
*	victim_mask
*		If the bit set to 1, then the port corresponding to
*		that bit shall mark packets that encounter congestion
*		with a FECN, whether they are the source or victim
*		of congestion. (See A10.2.1.1.1)
*		  bit 0: port 0 (enhanced port 0 only)
*		  bits [254..1]: ports [254..1]
*		  bit 255: reserved
*
*	credit_mask
*		If the bit set to 1, then the port corresponding
*		to that bit shall apply Credit Starvation.
*		  bit 0: port 0 (enhanced port 0 only)
*		  bits [254..1]: ports [254..1]
*		  bit 255: reserved
*
*	threshold_resv
*		bits [7..4] Indicates how aggressive cong. marking should be
*		bits [3..0] Reserved
*
*	packet_size
*		Any packet less than this size won't be marked with FECN
*
*	cs_threshold_resv
*		bits [15..12] How aggressive Credit Starvation should be
*		bits [11..0] Reserved
*
*	cs_return_delay
*		Value that controls credit return rate.
*
*	marking_rate
*		The value that provides the mean number of packets
*		between marking eligible packets with FECN.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_sw_port_cong_setting_element_t
* NAME
*	ib_sw_port_cong_setting_element_t
*
* DESCRIPTION
*	IBA defined SwitchPortCongestionSettingElement (A10.4.3.7)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_sw_port_cong_setting_element {
	uint8_t valid_ctrl_type_res_threshold;
	uint8_t packet_size;
	ib_net16_t cong_param;
} PACK_SUFFIX ib_sw_port_cong_setting_element_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	valid_ctrl_type_res_threshold
*		bit 7: "Valid"
*			when set to 1, indicates this switch
*			port congestion setting element is valid.
*		bit 6: "Control Type"
*			Indicates which type of attribute is being set:
*			0b = Congestion Control parameters are being set.
*			1b = Credit Starvation parameters are being set.
*		bits [5..4]: reserved
*		bits [3..0]: "Threshold"
*			When Control Type is 0, contains the congestion
*			threshold value (Threshold) for this port.
*			When Control Type is 1, contains the credit
*			starvation threshold (CS_Threshold) value for
*			this port.
*
*	packet_size
*		When Control Type is 0, this field contains the minimum
*		size of packets that may be marked with a FECN.
*		When Control Type is 1, this field is reserved.
*
*	cong_parm
*		When Control Type is 0, this field contains the port
*		marking_rate.
*		When Control Type is 1, this field is reserved.
*
* SEE ALSO
*	ib_cc_mad_t, ib_sw_port_cong_setting_t
*********/

/****d* IBA Base: Types/ib_sw_port_cong_setting_block_t
* NAME
*	ib_sw_port_cong_setting_block_t
*
* DESCRIPTION
*	Defines the SwitchPortCongestionSetting Block (A10.4.3.7).
*
* SOURCE
*/
#define IB_CC_SW_PORT_SETTING_ELEMENTS 32
typedef ib_sw_port_cong_setting_element_t ib_sw_port_cong_setting_block_t[IB_CC_SW_PORT_SETTING_ELEMENTS];
/**********/

/****s* IBA Base: Types/ib_sw_port_cong_setting_t
* NAME
*	ib_sw_port_cong_setting_t
*
* DESCRIPTION
*	IBA defined SwitchPortCongestionSetting attribute (A10.4.3.7)
*
* SYNOPSIS
*/

#include <complib/cl_packon.h>
typedef struct _ib_sw_port_cong_setting {
	ib_sw_port_cong_setting_block_t block;
} PACK_SUFFIX ib_sw_port_cong_setting_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	block
*		SwitchPortCongestionSetting block.
*
* SEE ALSO
*	ib_cc_mad_t, ib_sw_port_cong_setting_element_t
*********/

/****s* IBA Base: Types/ib_ca_cong_entry_t
* NAME
*	ib_ca_cong_entry_t
*
* DESCRIPTION
*	IBA defined CACongestionEntry (A10.4.3.8)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_ca_cong_entry {
	ib_net16_t ccti_timer;
	uint8_t ccti_increase;
	uint8_t trigger_threshold;
	uint8_t ccti_min;
	uint8_t resv0;
	ib_net16_t resv1;
} PACK_SUFFIX ib_ca_cong_entry_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	ccti_timer
*		When the timer expires it will be reset to its specified
*		value, and 1 will be decremented from the CCTI.
*
*	ccti_increase
*		The number to be added to the table Index (CCTI)
*		on the receipt of a BECN.
*
*	trigger_threshold
*		When the CCTI is equal to this value, an event
*		is logged in the CAs cyclic event log.
*
*	ccti_min
*		The minimum value permitted for the CCTI.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_ca_cong_setting_t
* NAME
*	ib_ca_cong_setting_t
*
* DESCRIPTION
*	IBA defined CACongestionSetting attribute (A10.4.3.8)
*
* SYNOPSIS
*/
#define IB_CA_CONG_ENTRY_DATA_SIZE 16
#include <complib/cl_packon.h>
typedef struct _ib_ca_cong_setting {
	ib_net16_t port_control;
	ib_net16_t control_map;
	ib_ca_cong_entry_t entry_list[IB_CA_CONG_ENTRY_DATA_SIZE];
} PACK_SUFFIX ib_ca_cong_setting_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	port_control
*		Congestion attributes for this port:
*		  bit0 = 0: QP based CC
*		  bit0 = 1: SL/Port based CC
*		All other bits are reserved
*
*	control_map
*		An array of sixteen bits, one for each SL. Each bit indicates
*		whether or not the corresponding entry is to be modified.
*
*	entry_list
*		List of 16 CACongestionEntries, one per SL.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_cc_tbl_entry_t
* NAME
*	ib_cc_tbl_entry_t
*
* DESCRIPTION
*	IBA defined CongestionControlTableEntry (A10.4.3.9)
*
* SYNOPSIS
*/
#include <complib/cl_packon.h>
typedef struct _ib_cc_tbl_entry {
	ib_net16_t shift_multiplier;
} PACK_SUFFIX ib_cc_tbl_entry_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	shift_multiplier
*		bits [15..14] - CCT Shift
*		  used when calculating the injection rate delay
*		bits [13..0] - CCT Multiplier
*		  used when calculating the injection rate delay
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_cc_tbl_t
* NAME
*	ib_cc_tbl_t
*
* DESCRIPTION
*	IBA defined CongestionControlTable attribute (A10.4.3.9)
*
* SYNOPSIS
*/
#define IB_CC_TBL_ENTRY_LIST_MAX 64
#include <complib/cl_packon.h>
typedef struct _ib_cc_tbl {
	ib_net16_t ccti_limit;
	ib_net16_t resv;
	ib_cc_tbl_entry_t entry_list[IB_CC_TBL_ENTRY_LIST_MAX];
} PACK_SUFFIX ib_cc_tbl_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	ccti_limit
*		Maximum valid CCTI for this table.
*
*	entry_list
*		List of up to 64 CongestionControlTableEntries.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

/****s* IBA Base: Types/ib_time_stamp_t
* NAME
*	ib_time_stamp_t
*
* DESCRIPTION
*	IBA defined TimeStamp attribute (A10.4.3.10)
*
* SOURCE
*/
#include <complib/cl_packon.h>
typedef struct _ib_time_stamp {
	ib_net32_t value;
} PACK_SUFFIX ib_time_stamp_t;
#include <complib/cl_packoff.h>
/*
* FIELDS
*
*	value
*		Free running clock that provides relative time info
*		for a device. Time is kept in 1.024 usec units.
*
* SEE ALSO
*	ib_cc_mad_t
*********/

END_C_DECLS
#else				/* ndef __WIN__ */
#include <iba/ib_types_extended.h>
#endif
#endif				/* __IB_TYPES_H__ */
