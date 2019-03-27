/*
 * Copyright (c) 2004, 2005, 2010 Intel Corporation.  All rights reserved.
 * Copyright (c) 2013 Lawrence Livermore National Security. All rights reserved.
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

#include <stdio.h>
#include <infiniband/endian.h>
#include <infiniband/umad.h>
#include <infiniband/umad_types.h>
#include <infiniband/umad_sm.h>
#include <infiniband/umad_sa.h>
#include <infiniband/umad_cm.h>
#include "umad_str.h"

const char * umad_class_str(uint8_t mgmt_class)
{
	switch (mgmt_class) {
		case UMAD_CLASS_SUBN_LID_ROUTED:
		case UMAD_CLASS_SUBN_DIRECTED_ROUTE:
			return("Subn");
		case UMAD_CLASS_SUBN_ADM:
			return("SubnAdm");
		case UMAD_CLASS_PERF_MGMT:
			return("Perf");
		case UMAD_CLASS_BM:
			return("BM");
		case UMAD_CLASS_DEVICE_MGMT:
			return("DevMgt");
		case UMAD_CLASS_CM:
			return("ComMgt");
		case UMAD_CLASS_SNMP:
			return("SNMP");
		case UMAD_CLASS_DEVICE_ADM:
			return("DevAdm");
		case UMAD_CLASS_BOOT_MGMT:
			return("BootMgt");
		case UMAD_CLASS_BIS:
			return("BIS");
		case UMAD_CLASS_CONG_MGMT:
			return("CongestionManagment");
		default:
			break;
	}

	if ((UMAD_CLASS_VENDOR_RANGE1_START <= mgmt_class
		&& mgmt_class <= UMAD_CLASS_VENDOR_RANGE1_END)
	    || (UMAD_CLASS_VENDOR_RANGE2_START <= mgmt_class
		&& mgmt_class <= UMAD_CLASS_VENDOR_RANGE2_END))
		return("Vendor");

	if (UMAD_CLASS_APPLICATION_START <= mgmt_class
	    && mgmt_class <= UMAD_CLASS_APPLICATION_END) {
		return("Application");
	}
	return ("<unknown>");
}

static const char * umad_common_method_str(uint8_t method)
{
	switch(method) {
		case UMAD_METHOD_GET:
			return ("Get");
		case UMAD_METHOD_SET:
			return ("Set");
		case UMAD_METHOD_GET_RESP:
			return ("GetResp");
		case UMAD_METHOD_SEND:
			return ("Send");
		case UMAD_METHOD_TRAP:
			return ("Trap");
		case UMAD_METHOD_REPORT:
			return ("Report");
		case UMAD_METHOD_REPORT_RESP:
			return ("ReportResp");
		case UMAD_METHOD_TRAP_REPRESS:
			return ("TrapRepress");
		default:
			return ("<unknown");
	}
}

static const char * umad_sa_method_str(uint8_t method)
{
	switch(method) {
		case UMAD_SA_METHOD_GET_TABLE:
			return ("GetTable");
		case UMAD_SA_METHOD_GET_TABLE_RESP:
			return ("GetTableResp");
		case UMAD_SA_METHOD_DELETE:
			return ("Delete");
		case UMAD_SA_METHOD_DELETE_RESP:
			return ("DeleteResp");
		case UMAD_SA_METHOD_GET_MULTI:
			return ("GetMulti");
		case UMAD_SA_METHOD_GET_MULTI_RESP:
			return ("GetMultiResp");
		case UMAD_SA_METHOD_GET_TRACE_TABLE:
			return ("GetTraceTable");
		default:
			return (umad_common_method_str(method));
	}
}

const char * umad_method_str(uint8_t mgmt_class, uint8_t method)
{
	if (mgmt_class == UMAD_CLASS_SUBN_ADM)
		return(umad_sa_method_str(method));

	return (umad_common_method_str(method));
}

const char * umad_common_mad_status_str(__be16 _status)
{
	uint16_t status = be16toh(_status);

	if (status & UMAD_STATUS_BUSY)
		return ("Busy");

	if (status & UMAD_STATUS_REDIRECT)
		return ("Redirection required");

	switch(status & UMAD_STATUS_INVALID_FIELD_MASK) {
		case UMAD_STATUS_BAD_VERSION:
			return ("Bad Version");
		case UMAD_STATUS_METHOD_NOT_SUPPORTED:
			return ("Method not supported");
		case UMAD_STATUS_ATTR_NOT_SUPPORTED:
			return ("Method/Attribute combo not supported");
		case UMAD_STATUS_INVALID_ATTR_VALUE:
			return ("Invalid attribute/modifier field");
	}
	return ("Success");
}

const char * umad_sa_mad_status_str(__be16 _status)
{
	uint16_t status = be16toh(_status);
	switch((status & UMAD_STATUS_CLASS_MASK) >> 8) {
		case UMAD_SA_STATUS_SUCCESS:
			return ("Success");
		case UMAD_SA_STATUS_NO_RESOURCES:
			return ("No Resources");
		case UMAD_SA_STATUS_REQ_INVALID:
			return ("Request Invalid");
		case UMAD_SA_STATUS_NO_RECORDS:
			return ("No Records");
		case UMAD_SA_STATUS_TOO_MANY_RECORDS:
			return ("Too Many Records");
		case UMAD_SA_STATUS_INVALID_GID:
			return ("Invalid GID");
		case UMAD_SA_STATUS_INSUF_COMPS:
			return ("Insufficient Components");
		case UMAD_SA_STATUS_REQ_DENIED:
			return ("Request Denied");
		case UMAD_SA_STATUS_PRI_SUGGESTED:
			return ("Priority Suggested");
	}
	return ("Undefined Error");
}

static const char *umad_common_attr_str(__be16 attr_id)
{
	switch(be16toh(attr_id)) {
		case UMAD_ATTR_CLASS_PORT_INFO:
			return "Class Port Info";
		case UMAD_ATTR_NOTICE:
			return "Notice";
		case UMAD_ATTR_INFORM_INFO:
			return "Inform Info";
		default:
			return "<unknown>";
	}
}

static const char * umad_sm_attr_str(__be16 attr_id)
{
	switch(be16toh(attr_id)) {
		case UMAD_SM_ATTR_NODE_DESC:
			return ("NodeDescription");
		case UMAD_SM_ATTR_NODE_INFO:
			return ("NodeInfo");
		case UMAD_SM_ATTR_SWITCH_INFO:
			return ("SwitchInfo");
		case UMAD_SM_ATTR_GUID_INFO:
			return ("GUIDInfo");
		case UMAD_SM_ATTR_PORT_INFO:
			return ("PortInfo");
		case UMAD_SM_ATTR_PKEY_TABLE:
			return ("P_KeyTable");
		case UMAD_SM_ATTR_SLVL_TABLE:
			return ("SLtoVLMappingTable");
		case UMAD_SM_ATTR_VL_ARB_TABLE:
			return ("VLArbitrationTable");
		case UMAD_SM_ATTR_LINEAR_FT:
			return ("LinearForwardingTable");
		case UMAD_SM_ATTR_RANDOM_FT:
			return ("RandomForwardingTable");
		case UMAD_SM_ATTR_MCAST_FT:
			return ("MulticastForwardingTable");
		case UMAD_SM_ATTR_SM_INFO:
			return ("SMInfo");
		case UMAD_SM_ATTR_VENDOR_DIAG:
			return ("VendorDiag");
		case UMAD_SM_ATTR_LED_INFO:
			return ("LedInfo");
		case UMAD_SM_ATTR_LINK_SPD_WIDTH_TABLE:
			return ("LinkSpeedWidthPairsTable");
		case UMAD_SM_ATTR_VENDOR_MADS_TABLE:
			return ("VendorSpecificMadsTable");
		case UMAD_SM_ATTR_HIERARCHY_INFO:
			return ("HierarchyInfo");
		case UMAD_SM_ATTR_CABLE_INFO:
			return ("CableInfo");
		case UMAD_SM_ATTR_PORT_INFO_EXT:
			return ("PortInfoExtended");
		default:
			return (umad_common_attr_str(attr_id));
	}
	return ("<unknown>");
}

static const char * umad_sa_attr_str(__be16 attr_id)
{
	switch(be16toh(attr_id)) {
		case UMAD_SA_ATTR_NODE_REC:
			return ("NodeRecord");
		case UMAD_SA_ATTR_PORT_INFO_REC:
			return ("PortInfoRecord");
		case UMAD_SA_ATTR_SLVL_REC:
			return ("SLtoVLMappingTableRecord");
		case UMAD_SA_ATTR_SWITCH_INFO_REC:
			return ("SwitchInfoRecord");
		case UMAD_SA_ATTR_LINEAR_FT_REC:
			return ("LinearForwardingTableRecord");
		case UMAD_SA_ATTR_RANDOM_FT_REC:
			return ("RandomForwardingTableRecord");
		case UMAD_SA_ATTR_MCAST_FT_REC:
			return ("MulticastForwardingTableRecord");
		case UMAD_SA_ATTR_SM_INFO_REC:
			return ("SMInfoRecord");
		case UMAD_SA_ATTR_INFORM_INFO_REC:
			return ("InformInfoRecord");
		case UMAD_SA_ATTR_LINK_REC:
			return ("LinkRecord");
		case UMAD_SA_ATTR_GUID_INFO_REC:
			return ("GuidInfoRecord");
		case UMAD_SA_ATTR_SERVICE_REC:
			return ("ServiceRecord");
		case UMAD_SA_ATTR_PKEY_TABLE_REC:
			return ("P_KeyTableRecord");
		case UMAD_SA_ATTR_PATH_REC:
			return ("PathRecord");
		case UMAD_SA_ATTR_VL_ARB_REC:
			return ("VLArbitrationTableRecord");
		case UMAD_SA_ATTR_MCMEMBER_REC:
			return ("MCMemberRecord");
		case UMAD_SA_ATTR_TRACE_REC:
			return ("TraceRecord");
		case UMAD_SA_ATTR_MULTI_PATH_REC:
			return ("MultiPathRecord");
		case UMAD_SA_ATTR_SERVICE_ASSOC_REC:
			return ("ServiceAssociationRecord");
		case UMAD_SA_ATTR_LINK_SPD_WIDTH_TABLE_REC:
			return ("LinkSpeedWidthPairsTableRecord");
		case UMAD_SA_ATTR_HIERARCHY_INFO_REC:
			return ("HierarchyInfoRecord");
		case UMAD_SA_ATTR_CABLE_INFO_REC:
			return ("CableInfoRecord");
		case UMAD_SA_ATTR_PORT_INFO_EXT_REC:
			return ("PortInfoExtendedRecord");
		default:
			return (umad_common_attr_str(attr_id));
	}
	return ("<unknown>");
}

static const char * umad_cm_attr_str(__be16 attr_id)
{
	switch(be16toh(attr_id)) {
		case UMAD_CM_ATTR_REQ:
			return "ConnectRequest";
		case UMAD_CM_ATTR_MRA:
			return "MsgRcptAck";
		case UMAD_CM_ATTR_REJ:
			return "ConnectReject";
		case UMAD_CM_ATTR_REP:
			return "ConnectReply";
		case UMAD_CM_ATTR_RTU:
			return "ReadyToUse";
		case UMAD_CM_ATTR_DREQ:
			return "DisconnectRequest";
		case UMAD_CM_ATTR_DREP:
			return "DisconnectReply";
		case UMAD_CM_ATTR_SIDR_REQ:
			return "ServiceIDResReq";
		case UMAD_CM_ATTR_SIDR_REP:
			return "ServiceIDResReqResp";
		case UMAD_CM_ATTR_LAP:
			return "LoadAlternatePath";
		case UMAD_CM_ATTR_APR:
			return "AlternatePathResponse";
		case UMAD_CM_ATTR_SAP:
			return "SuggestAlternatePath";
		case UMAD_CM_ATTR_SPR:
			return "SuggestPathResponse";
		default:
			return (umad_common_attr_str(attr_id));
	}
	return ("<unknown>");
}

const char * umad_attribute_str(uint8_t mgmt_class, __be16 attr_id)
{
	switch (mgmt_class) {
		case UMAD_CLASS_SUBN_LID_ROUTED:
		case UMAD_CLASS_SUBN_DIRECTED_ROUTE:
			return(umad_sm_attr_str(attr_id));
		case UMAD_CLASS_SUBN_ADM:
			return(umad_sa_attr_str(attr_id));
		case UMAD_CLASS_CM:
			return(umad_cm_attr_str(attr_id));
	}

	return (umad_common_attr_str(attr_id));
}
