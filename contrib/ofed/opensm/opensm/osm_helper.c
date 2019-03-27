/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
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

/*
 * Abstract:
 *    Implementation of opensm helper functions.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_HELPER_C
#include <opensm/osm_helper.h>
#include <opensm/osm_log.h>

#define LINE_LENGTH 256

#define ARR_SIZE(a) (sizeof(a)/sizeof((a)[0]))

/* we use two tables - one for queries and one for responses */
static const char *ib_sa_method_str[] = {
	"RESERVED",		/* 0 */
	"SubnAdmGet",		/* 1 */
	"SubnAdmSet",		/* 2 */
	"RESERVED",		/* 3 */
	"RESERVED",		/* 4 */
	"RESERVED",		/* 5 */
	"SubnAdmReport",	/* 6 */
	"RESERVED",		/* 7 */
	"RESERVED",		/* 8 */
	"RESERVED",		/* 9 */
	"RESERVED",		/* A */
	"RESERVED",		/* B */
	"RESERVED",		/* C */
	"RESERVED",		/* D */
	"RESERVED",		/* E */
	"RESERVED",		/* F */
	"RESERVED",		/* 10 */
	"RESERVED",		/* 11 */
	"SubnAdmGetTable",	/* 12 */
	"SubnAdmGetTraceTable",	/* 13 */
	"SubnAdmGetMulti",	/* 14 */
	"SubnAdmDelete",	/* 15 */
	"UNKNOWN"		/* 16 */
};

#define OSM_SA_METHOD_STR_UNKNOWN_VAL (ARR_SIZE(ib_sa_method_str) - 1)

static const char *ib_sa_resp_method_str[] = {
	"RESERVED",		/* 80 */
	"SubnAdmGetResp",	/* 81 */
	"RESERVED (SetResp?)",	/* 82 */
	"RESERVED",		/* 83 */
	"RESERVED",		/* 84 */
	"RESERVED",		/* 85 */
	"SubnAdmReportResp",	/* 86 */
	"RESERVED",		/* 87 */
	"RESERVED",		/* 88 */
	"RESERVED",		/* 89 */
	"RESERVED",		/* 8A */
	"RESERVED",		/* 8B */
	"RESERVED",		/* 8C */
	"RESERVED",		/* 8D */
	"RESERVED",		/* 8E */
	"RESERVED",		/* 8F */
	"RESERVED",		/* 90 */
	"RESERVED",		/* 91 */
	"SubnAdmGetTableResp",	/* 92 */
	"RESERVED",		/* 93 */
	"SubnAdmGetMultiResp",	/* 94 */
	"SubnAdmDeleteResp",	/* 95 */
	"UNKNOWN"
};

static const char *ib_sm_method_str[] = {
	"RESERVED0",		/* 0 */
	"SubnGet",		/* 1 */
	"SubnSet",		/* 2 */
	"RESERVED3",		/* 3 */
	"RESERVED4",		/* 4 */
	"SubnTrap",		/* 5 */
	"RESERVED6",		/* 6 */
	"SubnTrapRepress",	/* 7 */
	"RESERVED8",		/* 8 */
	"RESERVED9",		/* 9 */
	"RESERVEDA",		/* A */
	"RESERVEDB",		/* B */
	"RESERVEDC",		/* C */
	"RESERVEDD",		/* D */
	"RESERVEDE",		/* E */
	"RESERVEDF",		/* F */
	"RESERVED10",		/* 10 */
	"SubnGetResp",		/* 11 */
	"RESERVED12",		/* 12 */
	"RESERVED13",		/* 13 */
	"RESERVED14",		/* 14 */
	"RESERVED15",		/* 15 */
	"RESERVED16",		/* 16 */
	"RESERVED17",		/* 17 */
	"RESERVED18",		/* 18 */
	"RESERVED19",		/* 19 */
	"RESERVED1A",		/* 1A */
	"RESERVED1B",		/* 1B */
	"RESERVED1C",		/* 1C */
	"RESERVED1D",		/* 1D */
	"RESERVED1E",		/* 1E */
	"RESERVED1F",		/* 1F */
	"UNKNOWN"		/* 20 */
};

#define OSM_SM_METHOD_STR_UNKNOWN_VAL (ARR_SIZE(ib_sm_method_str) - 1)

static const char *ib_sm_attr_str[] = {
	"RESERVED",		/* 0 */
	"ClassPortInfo",	/* 1 */
	"Notice",		/* 2 */
	"InformInfo",		/* 3 */
	"RESERVED",		/* 4 */
	"RESERVED",		/* 5 */
	"RESERVED",		/* 6 */
	"RESERVED",		/* 7 */
	"RESERVED",		/* 8 */
	"RESERVED",		/* 9 */
	"RESERVED",		/* A */
	"RESERVED",		/* B */
	"RESERVED",		/* C */
	"RESERVED",		/* D */
	"RESERVED",		/* E */
	"RESERVED",		/* F */
	"NodeDescription",	/* 10 */
	"NodeInfo",		/* 11 */
	"SwitchInfo",		/* 12 */
	"UNKNOWN",		/* 13 */
	"GUIDInfo",		/* 14 */
	"PortInfo",		/* 15 */
	"P_KeyTable",		/* 16 */
	"SLtoVLMappingTable",	/* 17 */
	"VLArbitrationTable",	/* 18 */
	"LinearForwardingTable",	/* 19 */
	"RandomForwardingTable",	/* 1A */
	"MulticastForwardingTable",	/* 1B */
	"UNKNOWN",		/* 1C */
	"UNKNOWN",		/* 1D */
	"UNKNOWN",		/* 1E */
	"UNKNOWN",		/* 1F */
	"SMInfo",		/* 20 */
	"UNKNOWN"		/* 21 - always highest value */
};

#define OSM_SM_ATTR_STR_UNKNOWN_VAL (ARR_SIZE(ib_sm_attr_str) - 1)

static const char *ib_sa_attr_str[] = {
	"RESERVED",		/* 0 */
	"ClassPortInfo",	/* 1 */
	"Notice",		/* 2 */
	"InformInfo",		/* 3 */
	"RESERVED",		/* 4 */
	"RESERVED",		/* 5 */
	"RESERVED",		/* 6 */
	"RESERVED",		/* 7 */
	"RESERVED",		/* 8 */
	"RESERVED",		/* 9 */
	"RESERVED",		/* A */
	"RESERVED",		/* B */
	"RESERVED",		/* C */
	"RESERVED",		/* D */
	"RESERVED",		/* E */
	"RESERVED",		/* F */
	"RESERVED",		/* 10 */
	"NodeRecord",		/* 11 */
	"PortInfoRecord",	/* 12 */
	"SLtoVLMappingTableRecord",	/* 13 */
	"SwitchInfoRecord",	/* 14 */
	"LinearForwardingTableRecord",	/* 15 */
	"RandomForwardingTableRecord",	/* 16 */
	"MulticastForwardingTableRecord",	/* 17 */
	"SMInfoRecord",		/* 18 */
	"RESERVED",		/* 19 */
	"RandomForwardingTable",	/* 1A */
	"MulticastForwardingTable",	/* 1B */
	"UNKNOWN",		/* 1C */
	"UNKNOWN",		/* 1D */
	"UNKNOWN",		/* 1E */
	"UNKNOWN",		/* 1F */
	"LinkRecord",		/* 20 */
	"UNKNOWN",		/* 21 */
	"UNKNOWN",		/* 22 */
	"UNKNOWN",		/* 23 */
	"UNKNOWN",		/* 24 */
	"UNKNOWN",		/* 25 */
	"UNKNOWN",		/* 26 */
	"UNKNOWN",		/* 27 */
	"UNKNOWN",		/* 28 */
	"UNKNOWN",		/* 29 */
	"UNKNOWN",		/* 2A */
	"UNKNOWN",		/* 2B */
	"UNKNOWN",		/* 2C */
	"UNKNOWN",		/* 2D */
	"UNKNOWN",		/* 2E */
	"UNKNOWN",		/* 2F */
	"GuidInfoRecord",	/* 30 */
	"ServiceRecord",	/* 31 */
	"UNKNOWN",		/* 32 */
	"P_KeyTableRecord",	/* 33 */
	"UNKNOWN",		/* 34 */
	"PathRecord",		/* 35 */
	"VLArbitrationTableRecord",	/* 36 */
	"UNKNOWN",		/* 37 */
	"MCMemberRecord",	/* 38 */
	"TraceRecord",		/* 39 */
	"MultiPathRecord",	/* 3A */
	"ServiceAssociationRecord",	/* 3B */
	"UNKNOWN",		/* 3C */
	"UNKNOWN",		/* 3D */
	"UNKNOWN",		/* 3E */
	"UNKNOWN",		/* 3F */
	"UNKNOWN",		/* 40 */
	"UNKNOWN",		/* 41 */
	"UNKNOWN",		/* 42 */
	"UNKNOWN",		/* 43 */
	"UNKNOWN",		/* 44 */
	"UNKNOWN",		/* 45 */
	"UNKNOWN",		/* 46 */
	"UNKNOWN",		/* 47 */
	"UNKNOWN",		/* 48 */
	"UNKNOWN",		/* 49 */
	"UNKNOWN",		/* 4A */
	"UNKNOWN",		/* 4B */
	"UNKNOWN",		/* 4C */
	"UNKNOWN",		/* 4D */
	"UNKNOWN",		/* 4E */
	"UNKNOWN",		/* 4F */
	"UNKNOWN",		/* 50 */
	"UNKNOWN",		/* 51 */
	"UNKNOWN",		/* 52 */
	"UNKNOWN",		/* 53 */
	"UNKNOWN",		/* 54 */
	"UNKNOWN",		/* 55 */
	"UNKNOWN",		/* 56 */
	"UNKNOWN",		/* 57 */
	"UNKNOWN",		/* 58 */
	"UNKNOWN",		/* 59 */
	"UNKNOWN",		/* 5A */
	"UNKNOWN",		/* 5B */
	"UNKNOWN",		/* 5C */
	"UNKNOWN",		/* 5D */
	"UNKNOWN",		/* 5E */
	"UNKNOWN",		/* 5F */
	"UNKNOWN",		/* 60 */
	"UNKNOWN",		/* 61 */
	"UNKNOWN",		/* 62 */
	"UNKNOWN",		/* 63 */
	"UNKNOWN",		/* 64 */
	"UNKNOWN",		/* 65 */
	"UNKNOWN",		/* 66 */
	"UNKNOWN",		/* 67 */
	"UNKNOWN",		/* 68 */
	"UNKNOWN",		/* 69 */
	"UNKNOWN",		/* 6A */
	"UNKNOWN",		/* 6B */
	"UNKNOWN",		/* 6C */
	"UNKNOWN",		/* 6D */
	"UNKNOWN",		/* 6E */
	"UNKNOWN",		/* 6F */
	"UNKNOWN",		/* 70 */
	"UNKNOWN",		/* 71 */
	"UNKNOWN",		/* 72 */
	"UNKNOWN",		/* 73 */
	"UNKNOWN",		/* 74 */
	"UNKNOWN",		/* 75 */
	"UNKNOWN",		/* 76 */
	"UNKNOWN",		/* 77 */
	"UNKNOWN",		/* 78 */
	"UNKNOWN",		/* 79 */
	"UNKNOWN",		/* 7A */
	"UNKNOWN",		/* 7B */
	"UNKNOWN",		/* 7C */
	"UNKNOWN",		/* 7D */
	"UNKNOWN",		/* 7E */
	"UNKNOWN",		/* 7F */
	"UNKNOWN",		/* 80 */
	"UNKNOWN",		/* 81 */
	"UNKNOWN",		/* 82 */
	"UNKNOWN",		/* 83 */
	"UNKNOWN",		/* 84 */
	"UNKNOWN",		/* 85 */
	"UNKNOWN",		/* 86 */
	"UNKNOWN",		/* 87 */
	"UNKNOWN",		/* 88 */
	"UNKNOWN",		/* 89 */
	"UNKNOWN",		/* 8A */
	"UNKNOWN",		/* 8B */
	"UNKNOWN",		/* 8C */
	"UNKNOWN",		/* 8D */
	"UNKNOWN",		/* 8E */
	"UNKNOWN",		/* 8F */
	"UNKNOWN",		/* 90 */
	"UNKNOWN",		/* 91 */
	"UNKNOWN",		/* 92 */
	"UNKNOWN",		/* 93 */
	"UNKNOWN",		/* 94 */
	"UNKNOWN",		/* 95 */
	"UNKNOWN",		/* 96 */
	"UNKNOWN",		/* 97 */
	"UNKNOWN",		/* 98 */
	"UNKNOWN",		/* 99 */
	"UNKNOWN",		/* 9A */
	"UNKNOWN",		/* 9B */
	"UNKNOWN",		/* 9C */
	"UNKNOWN",		/* 9D */
	"UNKNOWN",		/* 9E */
	"UNKNOWN",		/* 9F */
	"UNKNOWN",		/* A0 */
	"UNKNOWN",		/* A1 */
	"UNKNOWN",		/* A2 */
	"UNKNOWN",		/* A3 */
	"UNKNOWN",		/* A4 */
	"UNKNOWN",		/* A5 */
	"UNKNOWN",		/* A6 */
	"UNKNOWN",		/* A7 */
	"UNKNOWN",		/* A8 */
	"UNKNOWN",		/* A9 */
	"UNKNOWN",		/* AA */
	"UNKNOWN",		/* AB */
	"UNKNOWN",		/* AC */
	"UNKNOWN",		/* AD */
	"UNKNOWN",		/* AE */
	"UNKNOWN",		/* AF */
	"UNKNOWN",		/* B0 */
	"UNKNOWN",		/* B1 */
	"UNKNOWN",		/* B2 */
	"UNKNOWN",		/* B3 */
	"UNKNOWN",		/* B4 */
	"UNKNOWN",		/* B5 */
	"UNKNOWN",		/* B6 */
	"UNKNOWN",		/* B7 */
	"UNKNOWN",		/* B8 */
	"UNKNOWN",		/* B9 */
	"UNKNOWN",		/* BA */
	"UNKNOWN",		/* BB */
	"UNKNOWN",		/* BC */
	"UNKNOWN",		/* BD */
	"UNKNOWN",		/* BE */
	"UNKNOWN",		/* BF */
	"UNKNOWN",		/* C0 */
	"UNKNOWN",		/* C1 */
	"UNKNOWN",		/* C2 */
	"UNKNOWN",		/* C3 */
	"UNKNOWN",		/* C4 */
	"UNKNOWN",		/* C5 */
	"UNKNOWN",		/* C6 */
	"UNKNOWN",		/* C7 */
	"UNKNOWN",		/* C8 */
	"UNKNOWN",		/* C9 */
	"UNKNOWN",		/* CA */
	"UNKNOWN",		/* CB */
	"UNKNOWN",		/* CC */
	"UNKNOWN",		/* CD */
	"UNKNOWN",		/* CE */
	"UNKNOWN",		/* CF */
	"UNKNOWN",		/* D0 */
	"UNKNOWN",		/* D1 */
	"UNKNOWN",		/* D2 */
	"UNKNOWN",		/* D3 */
	"UNKNOWN",		/* D4 */
	"UNKNOWN",		/* D5 */
	"UNKNOWN",		/* D6 */
	"UNKNOWN",		/* D7 */
	"UNKNOWN",		/* D8 */
	"UNKNOWN",		/* D9 */
	"UNKNOWN",		/* DA */
	"UNKNOWN",		/* DB */
	"UNKNOWN",		/* DC */
	"UNKNOWN",		/* DD */
	"UNKNOWN",		/* DE */
	"UNKNOWN",		/* DF */
	"UNKNOWN",		/* E0 */
	"UNKNOWN",		/* E1 */
	"UNKNOWN",		/* E2 */
	"UNKNOWN",		/* E3 */
	"UNKNOWN",		/* E4 */
	"UNKNOWN",		/* E5 */
	"UNKNOWN",		/* E6 */
	"UNKNOWN",		/* E7 */
	"UNKNOWN",		/* E8 */
	"UNKNOWN",		/* E9 */
	"UNKNOWN",		/* EA */
	"UNKNOWN",		/* EB */
	"UNKNOWN",		/* EC */
	"UNKNOWN",		/* ED */
	"UNKNOWN",		/* EE */
	"UNKNOWN",		/* EF */
	"UNKNOWN",		/* F0 */
	"UNKNOWN",		/* F1 */
	"UNKNOWN",		/* F2 */
	"InformInfoRecord",	/* F3 */
	"UNKNOWN"		/* F4 - always highest value */
};

#define OSM_SA_ATTR_STR_UNKNOWN_VAL (ARR_SIZE(ib_sa_attr_str) - 1)

static int ordered_rates[] = {
	0, 0,	/*  0, 1 - reserved */
	1,	/*  2 - 2.5 Gbps */
	3,	/*  3 - 10  Gbps */
	6,	/*  4 - 30  Gbps */
	2,	/*  5 - 5   Gbps */
	5,	/*  6 - 20  Gbps */
	9,	/*  7 - 40  Gbps */
	10,	/*  8 - 60  Gbps */
	13,	/*  9 - 80  Gbps */
	14,	/* 10 - 120 Gbps */
	4,	/* 11 -  14 Gbps (17 Gbps equiv) */
	12,	/* 12 -  56 Gbps (68 Gbps equiv) */
	16,	/* 13 - 112 Gbps (136 Gbps equiv) */
	17,	/* 14 - 168 Gbps (204 Gbps equiv) */
	7,	/* 15 -  25 Gbps (31.25 Gbps equiv) */
	15,	/* 16 - 100 Gbps (125 Gbps equiv) */
	18,	/* 17 - 200 Gbps (250 Gbps equiv) */
	19,	/* 18 - 300 Gbps (375 Gbps equiv) */
	8,	/* 19 -  28 Gbps (35 Gbps equiv) */
	11,	/* 20 -  50 Gbps (62.5 Gbps equiv) */
};

int sprint_uint8_arr(char *buf, size_t size,
		     const uint8_t * arr, size_t len)
{
	int n;
	unsigned int i;
	for (i = 0, n = 0; i < len; i++) {
		n += snprintf(buf + n, size - n, "%s%u", i == 0 ? "" : ",",
			      arr[i]);
		if (n >= size)
			break;
	}
	return n;
}

const char *ib_get_sa_method_str(IN uint8_t method)
{
	if (method & 0x80) {
		method = method & 0x7f;
		if (method > OSM_SA_METHOD_STR_UNKNOWN_VAL)
			method = OSM_SA_METHOD_STR_UNKNOWN_VAL;
		/* it is a response - use the response table */
		return ib_sa_resp_method_str[method];
	} else {
		if (method > OSM_SA_METHOD_STR_UNKNOWN_VAL)
			method = OSM_SA_METHOD_STR_UNKNOWN_VAL;
		return ib_sa_method_str[method];
	}
}

const char *ib_get_sm_method_str(IN uint8_t method)
{
	if (method & 0x80)
		method = (method & 0x0F) | 0x10;
	if (method > OSM_SM_METHOD_STR_UNKNOWN_VAL)
		method = OSM_SM_METHOD_STR_UNKNOWN_VAL;
	return ib_sm_method_str[method];
}

const char *ib_get_sm_attr_str(IN ib_net16_t attr)
{
	uint16_t host_attr = cl_ntoh16(attr);

	if (attr == IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO)
		return "MLNXExtendedPortInfo";

	if (host_attr > OSM_SM_ATTR_STR_UNKNOWN_VAL)
		host_attr = OSM_SM_ATTR_STR_UNKNOWN_VAL;

	return ib_sm_attr_str[host_attr];
}

const char *ib_get_sa_attr_str(IN ib_net16_t attr)
{
	uint16_t host_attr = cl_ntoh16(attr);

	if (host_attr > OSM_SA_ATTR_STR_UNKNOWN_VAL)
		host_attr = OSM_SA_ATTR_STR_UNKNOWN_VAL;

	return ib_sa_attr_str[host_attr];
}

const char *ib_get_trap_str(ib_net16_t trap_num)
{
	switch (cl_ntoh16(trap_num)) {
	case SM_GID_IN_SERVICE_TRAP:	/* 64 */
		return "GID in service";
	case SM_GID_OUT_OF_SERVICE_TRAP: /* 65 */
		return "GID out of service";
	case SM_MGID_CREATED_TRAP:	/* 66 */
		return "New mcast group created";
	case SM_MGID_DESTROYED_TRAP:	/* 67 */
		return "Mcast group deleted";
	case SM_UNPATH_TRAP:		/* 68 */
		return "UnPath, Path no longer valid";
	case SM_REPATH_TRAP:		/* 69 */
		return "RePath, Path recomputed";
	case SM_LINK_STATE_CHANGED_TRAP: /* 128 */
		return "Link state change";
	case SM_LINK_INTEGRITY_THRESHOLD_TRAP: /* 129 */
		return "Local Link integrity threshold reached";
	case SM_BUFFER_OVERRUN_THRESHOLD_TRAP: /* 130 */
		return "Excessive Buffer Overrun Threshold reached";
	case SM_WATCHDOG_TIMER_EXPIRED_TRAP:   /* 131 */
		return "Flow Control Update watchdog timer expired";
	case SM_LOCAL_CHANGES_TRAP:	/* 144 */
		return
		    "CapabilityMask, NodeDescription, Link [Width|Speed] Enabled, SM priority changed";
	case SM_SYS_IMG_GUID_CHANGED_TRAP: /* 145 */
		return "System Image GUID changed";
	case SM_BAD_MKEY_TRAP:		/* 256 */
		return "Bad M_Key";
	case SM_BAD_PKEY_TRAP:		/* 257 */
		return "Bad P_Key";
	case SM_BAD_QKEY_TRAP:		/* 258 */
		return "Bad Q_Key";
	case SM_BAD_SWITCH_PKEY_TRAP:	/* 259 */
		return "Bad P_Key (switch external port)";
	default:
		break;
	}
	return "Unknown";
}

const ib_gid_t ib_zero_gid = { {0} };

static ib_api_status_t dbg_do_line(IN char **pp_local, IN uint32_t buf_size,
				   IN const char *p_prefix_str,
				   IN const char *p_new_str,
				   IN uint32_t * p_total_len)
{
	char line[LINE_LENGTH];
	uint32_t len;

	sprintf(line, "%s%s", p_prefix_str, p_new_str);
	len = (uint32_t) strlen(line);
	*p_total_len += len;
	if (*p_total_len + sizeof('\0') > buf_size)
		return IB_INSUFFICIENT_MEMORY;

	strcpy(*pp_local, line);
	*pp_local += len;
	return IB_SUCCESS;
}

static void dbg_get_capabilities_str(IN char *p_buf, IN uint32_t buf_size,
				     IN const char *p_prefix_str,
				     IN const ib_port_info_t * p_pi)
{
	uint32_t total_len = 0;
	char *p_local = p_buf;

	strcpy(p_local, "Capability Mask:\n");
	p_local += strlen(p_local);

	if (p_pi->capability_mask & IB_PORT_CAP_RESV0) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_RESV0\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_IS_SM) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_IS_SM\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_NOTICE) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_NOTICE\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_TRAP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_TRAP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_IPD) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_IPD\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_AUTO_MIG) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_AUTO_MIG\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_SL_MAP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_SL_MAP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_NV_MKEY) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_NV_MKEY\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_NV_PKEY) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_NV_PKEY\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_LED_INFO) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_LED_INFO\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_SM_DISAB) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_SM_DISAB\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_SYS_IMG_GUID) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_SYS_IMG_GUID\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_PKEY_SW_EXT_PORT_TRAP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_PKEY_SW_EXT_PORT_TRAP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_CABLE_INFO) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_CABLE_INFO\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_EXT_SPEEDS) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_EXT_SPEEDS\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_MASK2) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_CAP_MASK2\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_COM_MGT) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_COM_MGT\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_SNMP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_SNMP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_REINIT) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_REINIT\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_DEV_MGT) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_DEV_MGT\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_VEND_CLS) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_VEND_CLS\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_DR_NTC) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_DR_NTC\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_NTC) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_CAP_NTC\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_BM) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_BM\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_LINK_RT_LATENCY) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_LINK_RT_LATENCY\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_CLIENT_REREG) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_CLIENT_REREG\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_OTHER_LOCAL_CHANGES_NTC) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_OTHER_LOCAL_CHANGES_NTC\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_LINK_SPEED_WIDTH_PAIRS_TBL) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_LINK_SPEED_WIDTH_PAIRS_TBL\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_VEND_MADS) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_VEND_MADS\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_MCAST_PKEY_TRAP_SUPPRESS) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_MCAST_PKEY_TRAP_SUPPRESS\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_MCAST_FDB_TOP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_MCAST_FDB_TOP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask & IB_PORT_CAP_HAS_HIER_INFO) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP_HAS_HIER_INFO\n",
				&total_len) != IB_SUCCESS)
			return;
	}
}

static void dbg_get_capabilities2_str(IN char *p_buf, IN uint32_t buf_size,
				      IN const char *p_prefix_str,
				      IN const ib_port_info_t * p_pi)
{
	uint32_t total_len = 0;
	char *p_local = p_buf;

	strcpy(p_local, "Capability Mask2:\n");
	p_local += strlen(p_local);

	if (p_pi->capability_mask2 & IB_PORT_CAP2_IS_SET_NODE_DESC_SUPPORTED) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP2_IS_SET_NODE_DESC_SUPPORTED\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask2 & IB_PORT_CAP2_IS_PORT_INFO_EXT_SUPPORTED) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP2_IS_PORT_INFO_EXT_SUPPORTED\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask2 & IB_PORT_CAP2_IS_VIRT_SUPPORTED) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP2_IS_VIRT_SUPPORTED\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask2 & IB_PORT_CAP2_IS_SWITCH_PORT_STATE_TBL_SUPP) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP2_IS_SWITCH_PORT_STATE_TBL_SUPP\n",
				&total_len) != IB_SUCCESS)
			return;
	}
	if (p_pi->capability_mask2 & IB_PORT_CAP2_IS_LINK_WIDTH_2X_SUPPORTED) {
		if (dbg_do_line(&p_local, buf_size, p_prefix_str,
				"IB_PORT_CAP2_IS_LINK_WIDTH_2X_SUPPORTED\n",
				&total_len) != IB_SUCCESS)
			return;
	}
}

static void osm_dump_port_info_to_buf(IN ib_net64_t node_guid,
				      IN ib_net64_t port_guid,
				      IN uint8_t port_num,
				      IN const ib_port_info_t * p_pi,
				      OUT char * buf)
{
	if (!buf || !p_pi)
		return;
	else {
		sprintf(buf,
			"PortInfo dump:\n"
			"\t\t\t\tport number..............%u\n"
			"\t\t\t\tnode_guid................0x%016" PRIx64 "\n"
			"\t\t\t\tport_guid................0x%016" PRIx64 "\n"
			"\t\t\t\tm_key....................0x%016" PRIx64 "\n"
			"\t\t\t\tsubnet_prefix............0x%016" PRIx64 "\n"
			"\t\t\t\tbase_lid.................%u\n"
			"\t\t\t\tmaster_sm_base_lid.......%u\n"
			"\t\t\t\tcapability_mask..........0x%X\n"
			"\t\t\t\tdiag_code................0x%X\n"
			"\t\t\t\tm_key_lease_period.......0x%X\n"
			"\t\t\t\tlocal_port_num...........%u\n"
			"\t\t\t\tlink_width_enabled.......0x%X\n"
			"\t\t\t\tlink_width_supported.....0x%X\n"
			"\t\t\t\tlink_width_active........0x%X\n"
			"\t\t\t\tlink_speed_supported.....0x%X\n"
			"\t\t\t\tport_state...............%s\n"
			"\t\t\t\tstate_info2..............0x%X\n"
			"\t\t\t\tm_key_protect_bits.......0x%X\n"
			"\t\t\t\tlmc......................0x%X\n"
			"\t\t\t\tlink_speed...............0x%X\n"
			"\t\t\t\tmtu_smsl.................0x%X\n"
			"\t\t\t\tvl_cap_init_type.........0x%X\n"
			"\t\t\t\tvl_high_limit............0x%X\n"
			"\t\t\t\tvl_arb_high_cap..........0x%X\n"
			"\t\t\t\tvl_arb_low_cap...........0x%X\n"
			"\t\t\t\tinit_rep_mtu_cap.........0x%X\n"
			"\t\t\t\tvl_stall_life............0x%X\n"
			"\t\t\t\tvl_enforce...............0x%X\n"
			"\t\t\t\tm_key_violations.........0x%X\n"
			"\t\t\t\tp_key_violations.........0x%X\n"
			"\t\t\t\tq_key_violations.........0x%X\n"
			"\t\t\t\tguid_cap.................0x%X\n"
			"\t\t\t\tclient_reregister........0x%X\n"
			"\t\t\t\tmcast_pkey_trap_suppr....0x%X\n"
			"\t\t\t\tsubnet_timeout...........0x%X\n"
			"\t\t\t\tresp_time_value..........0x%X\n"
			"\t\t\t\terror_threshold..........0x%X\n"
			"\t\t\t\tmax_credit_hint..........0x%X\n"
			"\t\t\t\tlink_round_trip_latency..0x%X\n"
			"\t\t\t\tcapability_mask2.........0x%X\n"
			"\t\t\t\tlink_speed_ext_active....0x%X\n"
			"\t\t\t\tlink_speed_ext_supported.0x%X\n"
			"\t\t\t\tlink_speed_ext_enabled...0x%X\n",
			port_num, cl_ntoh64(node_guid), cl_ntoh64(port_guid),
			cl_ntoh64(p_pi->m_key), cl_ntoh64(p_pi->subnet_prefix),
			cl_ntoh16(p_pi->base_lid),
			cl_ntoh16(p_pi->master_sm_base_lid),
			cl_ntoh32(p_pi->capability_mask),
			cl_ntoh16(p_pi->diag_code),
			cl_ntoh16(p_pi->m_key_lease_period),
			p_pi->local_port_num, p_pi->link_width_enabled,
			p_pi->link_width_supported, p_pi->link_width_active,
			ib_port_info_get_link_speed_sup(p_pi),
			ib_get_port_state_str(ib_port_info_get_port_state
					      (p_pi)), p_pi->state_info2,
			ib_port_info_get_mpb(p_pi), ib_port_info_get_lmc(p_pi),
			p_pi->link_speed, p_pi->mtu_smsl, p_pi->vl_cap,
			p_pi->vl_high_limit, p_pi->vl_arb_high_cap,
			p_pi->vl_arb_low_cap, p_pi->mtu_cap,
			p_pi->vl_stall_life, p_pi->vl_enforce,
			cl_ntoh16(p_pi->m_key_violations),
			cl_ntoh16(p_pi->p_key_violations),
			cl_ntoh16(p_pi->q_key_violations), p_pi->guid_cap,
			ib_port_info_get_client_rereg(p_pi),
			ib_port_info_get_mcast_pkey_trap_suppress(p_pi),
			ib_port_info_get_timeout(p_pi),
			ib_port_info_get_resp_time_value(p_pi),
			p_pi->error_threshold, cl_ntoh16(p_pi->max_credit_hint),
			cl_ntoh32(p_pi->link_rt_latency),
			cl_ntoh16(p_pi->capability_mask2),
			ib_port_info_get_link_speed_ext_active(p_pi),
			ib_port_info_get_link_speed_ext_sup(p_pi),
			p_pi->link_speed_ext_enabled);
	}
}

void osm_dump_port_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			IN ib_net64_t port_guid, IN uint8_t port_num,
			IN const ib_port_info_t * p_pi,
			IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_port_info_to_buf(node_guid, port_guid,
					  port_num, p_pi, buf);

		osm_log(p_log, log_level, "%s", buf);

		/*  show the capabilities masks */
		if (p_pi->capability_mask) {
			dbg_get_capabilities_str(buf, BUF_SIZE, "\t\t\t\t",
						 p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
		if ((p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_MASK2) &&
		    p_pi->capability_mask2) {
			dbg_get_capabilities2_str(buf, BUF_SIZE, "\t\t\t\t",
						  p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
	}
}

void osm_dump_port_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			   IN ib_net64_t port_guid, IN uint8_t port_num,
			   IN const ib_port_info_t * p_pi, IN const int file_id,
			   IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_port_info_to_buf(node_guid, port_guid,
					  port_num, p_pi, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);

		/*  show the capabilities masks */
		if (p_pi->capability_mask) {
			dbg_get_capabilities_str(buf, BUF_SIZE, "\t\t\t\t",
						 p_pi);
			osm_log_v2(p_log, log_level, file_id, "%s", buf);
		}
		if ((p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_MASK2) &&
		    p_pi->capability_mask2) {
			dbg_get_capabilities2_str(buf, BUF_SIZE, "\t\t\t\t",
						  p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
	}
}

static void osm_dump_mlnx_ext_port_info_to_buf(IN ib_net64_t node_guid,
					       IN ib_net64_t port_guid, IN uint8_t port_num,
					       IN const ib_mlnx_ext_port_info_t * p_pi,
					       OUT char * buf)
{
	if (!buf || !p_pi)
		return;
	else {
		sprintf(buf,
                        "MLNX ExtendedPortInfo dump:\n"
                        "\t\t\t\tport number..............%u\n"
                        "\t\t\t\tnode_guid................0x%016" PRIx64 "\n"
                        "\t\t\t\tport_guid................0x%016" PRIx64 "\n"
                        "\t\t\t\tStateChangeEnable........0x%X\n"
                        "\t\t\t\tLinkSpeedSupported.......0x%X\n"
                        "\t\t\t\tLinkSpeedEnabled.........0x%X\n"
                        "\t\t\t\tLinkSpeedActive..........0x%X\n",
                        port_num, cl_ntoh64(node_guid), cl_ntoh64(port_guid),
                        p_pi->state_change_enable, p_pi->link_speed_supported,
                        p_pi->link_speed_enabled, p_pi->link_speed_active);
	}
}

void osm_dump_mlnx_ext_port_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
				 IN ib_net64_t port_guid, IN uint8_t port_num,
				 IN const ib_mlnx_ext_port_info_t * p_pi,
				 IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_mlnx_ext_port_info_to_buf(node_guid, port_guid,
						   port_num, p_pi, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_mlnx_ext_port_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
				    IN ib_net64_t port_guid, IN uint8_t port_num,
				    IN const ib_mlnx_ext_port_info_t * p_pi,
				    IN const int file_id, IN osm_log_level_t log_level)
{
        if (osm_log_is_active_v2(p_log, log_level, file_id)) {
                char buf[BUF_SIZE];

		osm_dump_mlnx_ext_port_info_to_buf(node_guid, port_guid,
						   port_num, p_pi, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
        }
}

static void osm_dump_portinfo_record_to_buf(IN const ib_portinfo_record_t * p_pir,
					    OUT char * buf)
{
	if (!buf || !p_pir)
		return;
	else {
		const ib_port_info_t *p_pi = &p_pir->port_info;

		sprintf(buf,
			"PortInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tEndPortLid...............%u\n"
			"\t\t\t\tPortNum..................%u\n"
			"\t\t\t\tOptions..................0x%X\n"
			"\t\t\t\tPortInfo dump:\n"
			"\t\t\t\tm_key....................0x%016" PRIx64 "\n"
			"\t\t\t\tsubnet_prefix............0x%016" PRIx64 "\n"
			"\t\t\t\tbase_lid.................%u\n"
			"\t\t\t\tmaster_sm_base_lid.......%u\n"
			"\t\t\t\tcapability_mask..........0x%X\n"
			"\t\t\t\tdiag_code................0x%X\n"
			"\t\t\t\tm_key_lease_period.......0x%X\n"
			"\t\t\t\tlocal_port_num...........%u\n"
			"\t\t\t\tlink_width_enabled.......0x%X\n"
			"\t\t\t\tlink_width_supported.....0x%X\n"
			"\t\t\t\tlink_width_active........0x%X\n"
			"\t\t\t\tlink_speed_supported.....0x%X\n"
			"\t\t\t\tport_state...............%s\n"
			"\t\t\t\tstate_info2..............0x%X\n"
			"\t\t\t\tm_key_protect_bits.......0x%X\n"
			"\t\t\t\tlmc......................0x%X\n"
			"\t\t\t\tlink_speed...............0x%X\n"
			"\t\t\t\tmtu_smsl.................0x%X\n"
			"\t\t\t\tvl_cap_init_type.........0x%X\n"
			"\t\t\t\tvl_high_limit............0x%X\n"
			"\t\t\t\tvl_arb_high_cap..........0x%X\n"
			"\t\t\t\tvl_arb_low_cap...........0x%X\n"
			"\t\t\t\tinit_rep_mtu_cap.........0x%X\n"
			"\t\t\t\tvl_stall_life............0x%X\n"
			"\t\t\t\tvl_enforce...............0x%X\n"
			"\t\t\t\tm_key_violations.........0x%X\n"
			"\t\t\t\tp_key_violations.........0x%X\n"
			"\t\t\t\tq_key_violations.........0x%X\n"
			"\t\t\t\tguid_cap.................0x%X\n"
			"\t\t\t\tclient_reregister........0x%X\n"
			"\t\t\t\tmcast_pkey_trap_suppr....0x%X\n"
			"\t\t\t\tsubnet_timeout...........0x%X\n"
			"\t\t\t\tresp_time_value..........0x%X\n"
			"\t\t\t\terror_threshold..........0x%X\n"
			"\t\t\t\tmax_credit_hint..........0x%X\n"
			"\t\t\t\tlink_round_trip_latency..0x%X\n"
			"\t\t\t\tcapability_mask2.........0x%X\n"
			"\t\t\t\tlink_speed_ext_active....0x%X\n"
			"\t\t\t\tlink_speed_ext_supported.0x%X\n"
			"\t\t\t\tlink_speed_ext_enabled...0x%X\n",
			cl_ntoh16(p_pir->lid), p_pir->port_num, p_pir->options,
			cl_ntoh64(p_pi->m_key), cl_ntoh64(p_pi->subnet_prefix),
			cl_ntoh16(p_pi->base_lid),
			cl_ntoh16(p_pi->master_sm_base_lid),
			cl_ntoh32(p_pi->capability_mask),
			cl_ntoh16(p_pi->diag_code),
			cl_ntoh16(p_pi->m_key_lease_period),
			p_pi->local_port_num, p_pi->link_width_enabled,
			p_pi->link_width_supported, p_pi->link_width_active,
			ib_port_info_get_link_speed_sup(p_pi),
			ib_get_port_state_str(ib_port_info_get_port_state
					      (p_pi)), p_pi->state_info2,
			ib_port_info_get_mpb(p_pi), ib_port_info_get_lmc(p_pi),
			p_pi->link_speed, p_pi->mtu_smsl, p_pi->vl_cap,
			p_pi->vl_high_limit, p_pi->vl_arb_high_cap,
			p_pi->vl_arb_low_cap, p_pi->mtu_cap,
			p_pi->vl_stall_life, p_pi->vl_enforce,
			cl_ntoh16(p_pi->m_key_violations),
			cl_ntoh16(p_pi->p_key_violations),
			cl_ntoh16(p_pi->q_key_violations), p_pi->guid_cap,
			ib_port_info_get_client_rereg(p_pi),
			ib_port_info_get_mcast_pkey_trap_suppress(p_pi),
			ib_port_info_get_timeout(p_pi),
			ib_port_info_get_resp_time_value(p_pi),
			p_pi->error_threshold, cl_ntoh16(p_pi->max_credit_hint),
			cl_ntoh32(p_pi->link_rt_latency),
			cl_ntoh16(p_pi->capability_mask2),
			ib_port_info_get_link_speed_ext_active(p_pi),
			ib_port_info_get_link_speed_ext_sup(p_pi),
			p_pi->link_speed_ext_enabled);
	}
}

void osm_dump_portinfo_record(IN osm_log_t * p_log,
			      IN const ib_portinfo_record_t * p_pir,
			      IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];
		const ib_port_info_t *p_pi = &p_pir->port_info;

		osm_dump_portinfo_record_to_buf(p_pir, buf);

		osm_log(p_log, log_level, "%s", buf);

		/*  show the capabilities masks */
		if (p_pi->capability_mask) {
			dbg_get_capabilities_str(buf, BUF_SIZE, "\t\t\t\t",
						 p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
		if ((p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_MASK2) &&
		    p_pi->capability_mask2) {
			dbg_get_capabilities2_str(buf, BUF_SIZE, "\t\t\t\t",
						  p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
	}
}

void osm_dump_portinfo_record_v2(IN osm_log_t * p_log,
				 IN const ib_portinfo_record_t * p_pir,
				 IN const int file_id,
				 IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];
		const ib_port_info_t *p_pi = &p_pir->port_info;

		osm_dump_portinfo_record_to_buf(p_pir, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);

		/*  show the capabilities masks */
		if (p_pi->capability_mask) {
			dbg_get_capabilities_str(buf, BUF_SIZE, "\t\t\t\t",
						 p_pi);
			osm_log_v2(p_log, log_level, file_id, "%s", buf);
		}
		if ((p_pi->capability_mask & IB_PORT_CAP_HAS_CAP_MASK2) &&
		    p_pi->capability_mask2) {
			dbg_get_capabilities2_str(buf, BUF_SIZE, "\t\t\t\t",
						  p_pi);
			osm_log(p_log, log_level, "%s", buf);
		}
	}
}

static void osm_dump_guid_info_to_buf(IN ib_net64_t node_guid,
				      IN ib_net64_t port_guid,
				      IN uint8_t block_num,
				      IN const ib_guid_info_t * p_gi,
				      OUT char * buf)
{
	if (!buf || !p_gi)
		return;
	else {
		sprintf(buf,
			"GUIDInfo dump:\n"
			"\t\t\t\tblock number............%u\n"
			"\t\t\t\tnode_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tport_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 0..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 1..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 2..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 3..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 4..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 5..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 6..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 7..................0x%016" PRIx64 "\n",
			block_num, cl_ntoh64(node_guid), cl_ntoh64(port_guid),
			cl_ntoh64(p_gi->guid[0]), cl_ntoh64(p_gi->guid[1]),
			cl_ntoh64(p_gi->guid[2]), cl_ntoh64(p_gi->guid[3]),
			cl_ntoh64(p_gi->guid[4]), cl_ntoh64(p_gi->guid[5]),
			cl_ntoh64(p_gi->guid[6]), cl_ntoh64(p_gi->guid[7]));
	}
}

void osm_dump_guid_info(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			IN ib_net64_t port_guid, IN uint8_t block_num,
			IN const ib_guid_info_t * p_gi,
			IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_guid_info_to_buf(node_guid, port_guid,
					  block_num, p_gi, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_guid_info_v2(IN osm_log_t * p_log, IN ib_net64_t node_guid,
			   IN ib_net64_t port_guid, IN uint8_t block_num,
			   IN const ib_guid_info_t * p_gi,
			   IN const int file_id,
			   IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_guid_info_to_buf(node_guid, port_guid,
					  block_num, p_gi, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_guidinfo_record_to_buf(IN const ib_guidinfo_record_t * p_gir,
					    OUT char * buf)
{
	if (!buf || !p_gir)
		return;
	else {
		const ib_guid_info_t *p_gi = &p_gir->guid_info;

		sprintf(buf,
			"GUIDInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tLid.....................%u\n"
			"\t\t\t\tBlockNum................0x%X\n"
			"\t\t\t\tReserved................0x%X\n"
			"\t\t\t\tGUIDInfo dump:\n"
			"\t\t\t\tReserved................0x%X\n"
			"\t\t\t\tGUID 0..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 1..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 2..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 3..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 4..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 5..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 6..................0x%016" PRIx64 "\n"
			"\t\t\t\tGUID 7..................0x%016" PRIx64 "\n",
			cl_ntoh16(p_gir->lid), p_gir->block_num, p_gir->resv,
			cl_ntoh32(p_gir->reserved),
			cl_ntoh64(p_gi->guid[0]), cl_ntoh64(p_gi->guid[1]),
			cl_ntoh64(p_gi->guid[2]), cl_ntoh64(p_gi->guid[3]),
			cl_ntoh64(p_gi->guid[4]), cl_ntoh64(p_gi->guid[5]),
			cl_ntoh64(p_gi->guid[6]), cl_ntoh64(p_gi->guid[7]));
	}
}
void osm_dump_guidinfo_record(IN osm_log_t * p_log,
			      IN const ib_guidinfo_record_t * p_gir,
			      IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_guidinfo_record_to_buf(p_gir, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_guidinfo_record_v2(IN osm_log_t * p_log,
				 IN const ib_guidinfo_record_t * p_gir,
				 IN const int file_id,
				 IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_guidinfo_record_to_buf(p_gir, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_node_info_to_buf(IN const ib_node_info_t * p_ni,
				      OUT char * buf)
{
	if (!buf || !p_ni)
		return;
	else {
		sprintf(buf,
			"NodeInfo dump:\n"
			"\t\t\t\tbase_version............0x%X\n"
			"\t\t\t\tclass_version...........0x%X\n"
			"\t\t\t\tnode_type...............%s\n"
			"\t\t\t\tnum_ports...............%u\n"
			"\t\t\t\tsys_guid................0x%016" PRIx64 "\n"
			"\t\t\t\tnode_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tport_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tpartition_cap...........0x%X\n"
			"\t\t\t\tdevice_id...............0x%X\n"
			"\t\t\t\trevision................0x%X\n"
			"\t\t\t\tport_num................%u\n"
			"\t\t\t\tvendor_id...............0x%X\n",
			p_ni->base_version, p_ni->class_version,
			ib_get_node_type_str(p_ni->node_type), p_ni->num_ports,
			cl_ntoh64(p_ni->sys_guid), cl_ntoh64(p_ni->node_guid),
			cl_ntoh64(p_ni->port_guid),
			cl_ntoh16(p_ni->partition_cap),
			cl_ntoh16(p_ni->device_id), cl_ntoh32(p_ni->revision),
			ib_node_info_get_local_port_num(p_ni),
			cl_ntoh32(ib_node_info_get_vendor_id(p_ni)));
	}
}

void osm_dump_node_info(IN osm_log_t * p_log, IN const ib_node_info_t * p_ni,
			IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_node_info_to_buf(p_ni, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_node_info_v2(IN osm_log_t * p_log, IN const ib_node_info_t * p_ni,
			   IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_node_info_to_buf(p_ni, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_node_record_to_buf(IN const ib_node_record_t * p_nr,
					OUT char * buf)
{
	if (!buf || !p_nr)
		return;
	else {
		char desc[sizeof(p_nr->node_desc.description) + 1];
		const ib_node_info_t *p_ni = &p_nr->node_info;

		memcpy(desc, p_nr->node_desc.description,
		       sizeof(p_nr->node_desc.description));
		desc[sizeof(desc) - 1] = '\0';
		sprintf(buf,
			"Node Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tLid.....................%u\n"
			"\t\t\t\tReserved................0x%X\n"
			"\t\t\t\tNodeInfo dump:\n"
			"\t\t\t\tbase_version............0x%X\n"
			"\t\t\t\tclass_version...........0x%X\n"
			"\t\t\t\tnode_type...............%s\n"
			"\t\t\t\tnum_ports...............%u\n"
			"\t\t\t\tsys_guid................0x%016" PRIx64 "\n"
			"\t\t\t\tnode_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tport_guid...............0x%016" PRIx64 "\n"
			"\t\t\t\tpartition_cap...........0x%X\n"
			"\t\t\t\tdevice_id...............0x%X\n"
			"\t\t\t\trevision................0x%X\n"
			"\t\t\t\tport_num................%u\n"
			"\t\t\t\tvendor_id...............0x%X\n"
			"\t\t\t\tNodeDescription\n"
			"\t\t\t\t%s\n",
			cl_ntoh16(p_nr->lid), cl_ntoh16(p_nr->resv),
			p_ni->base_version, p_ni->class_version,
			ib_get_node_type_str(p_ni->node_type), p_ni->num_ports,
			cl_ntoh64(p_ni->sys_guid), cl_ntoh64(p_ni->node_guid),
			cl_ntoh64(p_ni->port_guid),
			cl_ntoh16(p_ni->partition_cap),
			cl_ntoh16(p_ni->device_id), cl_ntoh32(p_ni->revision),
			ib_node_info_get_local_port_num(p_ni),
			cl_ntoh32(ib_node_info_get_vendor_id(p_ni)), desc);
	}
}

void osm_dump_node_record(IN osm_log_t * p_log,
			  IN const ib_node_record_t * p_nr,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_node_record_to_buf(p_nr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_node_record_v2(IN osm_log_t * p_log,
			     IN const ib_node_record_t * p_nr,
			     IN const int file_id,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_node_record_to_buf(p_nr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_path_record_to_buf(IN const ib_path_rec_t * p_pr,
					OUT char * buf)
{
	if (!buf || !p_pr)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];

		sprintf(buf,
			"PathRecord dump:\n"
			"\t\t\t\tservice_id..............0x%016" PRIx64 "\n"
			"\t\t\t\tdgid....................%s\n"
			"\t\t\t\tsgid....................%s\n"
			"\t\t\t\tdlid....................%u\n"
			"\t\t\t\tslid....................%u\n"
			"\t\t\t\thop_flow_raw............0x%X\n"
			"\t\t\t\ttclass..................0x%X\n"
			"\t\t\t\tnum_path_revers.........0x%X\n"
			"\t\t\t\tpkey....................0x%X\n"
			"\t\t\t\tqos_class...............0x%X\n"
			"\t\t\t\tsl......................0x%X\n"
			"\t\t\t\tmtu.....................0x%X\n"
			"\t\t\t\trate....................0x%X\n"
			"\t\t\t\tpkt_life................0x%X\n"
			"\t\t\t\tpreference..............0x%X\n"
			"\t\t\t\tresv2...................0x%02X%02X%02X%02X%02X%02X\n",
			cl_ntoh64(p_pr->service_id),
			inet_ntop(AF_INET6, p_pr->dgid.raw, gid_str,
				  sizeof gid_str),
			inet_ntop(AF_INET6, p_pr->sgid.raw, gid_str2,
				  sizeof gid_str2),
			cl_ntoh16(p_pr->dlid), cl_ntoh16(p_pr->slid),
			cl_ntoh32(p_pr->hop_flow_raw), p_pr->tclass,
			p_pr->num_path, cl_ntoh16(p_pr->pkey),
			ib_path_rec_qos_class(p_pr), ib_path_rec_sl(p_pr),
			p_pr->mtu, p_pr->rate, p_pr->pkt_life, p_pr->preference,
			p_pr->resv2[0], p_pr->resv2[1], p_pr->resv2[2],
			p_pr->resv2[3], p_pr->resv2[4], p_pr->resv2[5]);
	}
}

void osm_dump_path_record(IN osm_log_t * p_log, IN const ib_path_rec_t * p_pr,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_path_record_to_buf(p_pr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_path_record_v2(IN osm_log_t * p_log, IN const ib_path_rec_t * p_pr,
			     IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_path_record_to_buf(p_pr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_multipath_record_to_buf(IN const ib_multipath_rec_t * p_mpr,
					     OUT char * buf)
{
	if (!buf || !p_mpr)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char buf_line[1024];
		ib_gid_t const *p_gid = p_mpr->gids;
		int i, n = 0;

		if (p_mpr->sgid_count) {
			for (i = 0; i < p_mpr->sgid_count; i++) {
				n += sprintf(buf_line + n,
					     "\t\t\t\tsgid%02d.................."
					     "%s\n", i + 1,
					     inet_ntop(AF_INET6, p_gid->raw,
						       gid_str,
						       sizeof gid_str));
				p_gid++;
			}
		}
		if (p_mpr->dgid_count) {
			for (i = 0; i < p_mpr->dgid_count; i++) {
				n += sprintf(buf_line + n,
					     "\t\t\t\tdgid%02d.................."
					     "%s\n", i + 1,
					     inet_ntop(AF_INET6, p_gid->raw,
						       gid_str,
						       sizeof gid_str));
				p_gid++;
			}
		}
		sprintf(buf,
			"MultiPath Record dump:\n"
			"\t\t\t\thop_flow_raw............0x%X\n"
			"\t\t\t\ttclass..................0x%X\n"
			"\t\t\t\tnum_path_revers.........0x%X\n"
			"\t\t\t\tpkey....................0x%X\n"
			"\t\t\t\tqos_class...............0x%X\n"
			"\t\t\t\tsl......................0x%X\n"
			"\t\t\t\tmtu.....................0x%X\n"
			"\t\t\t\trate....................0x%X\n"
			"\t\t\t\tpkt_life................0x%X\n"
			"\t\t\t\tindependence............0x%X\n"
			"\t\t\t\tsgid_count..............0x%X\n"
			"\t\t\t\tdgid_count..............0x%X\n"
			"\t\t\t\tservice_id..............0x%016" PRIx64 "\n"
			"%s\n",
			cl_ntoh32(p_mpr->hop_flow_raw), p_mpr->tclass,
			p_mpr->num_path, cl_ntoh16(p_mpr->pkey),
			ib_multipath_rec_qos_class(p_mpr),
			ib_multipath_rec_sl(p_mpr), p_mpr->mtu, p_mpr->rate,
			p_mpr->pkt_life, p_mpr->independence,
			p_mpr->sgid_count, p_mpr->dgid_count,
			cl_ntoh64(ib_multipath_rec_service_id(p_mpr)),
			buf_line);
	}
}

void osm_dump_multipath_record(IN osm_log_t * p_log,
			       IN const ib_multipath_rec_t * p_mpr,
			       IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_multipath_record_to_buf(p_mpr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_multipath_record_v2(IN osm_log_t * p_log,
				  IN const ib_multipath_rec_t * p_mpr,
				  IN const int file_id,
				  IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_multipath_record_to_buf(p_mpr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_mc_record_to_buf(IN const ib_member_rec_t * p_mcmr,
				      OUT char * buf)
{
	if(!buf || !p_mcmr)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];

		sprintf(buf,
			"MCMember Record dump:\n"
			"\t\t\t\tMGID....................%s\n"
			"\t\t\t\tPortGid.................%s\n"
			"\t\t\t\tqkey....................0x%X\n"
			"\t\t\t\tmlid....................0x%X\n"
			"\t\t\t\tmtu.....................0x%X\n"
			"\t\t\t\tTClass..................0x%X\n"
			"\t\t\t\tpkey....................0x%X\n"
			"\t\t\t\trate....................0x%X\n"
			"\t\t\t\tpkt_life................0x%X\n"
			"\t\t\t\tSLFlowLabelHopLimit.....0x%X\n"
			"\t\t\t\tScopeState..............0x%X\n"
			"\t\t\t\tProxyJoin...............0x%X\n",
			inet_ntop(AF_INET6, p_mcmr->mgid.raw, gid_str,
				  sizeof gid_str),
			inet_ntop(AF_INET6, p_mcmr->port_gid.raw, gid_str2,
				  sizeof gid_str2),
			cl_ntoh32(p_mcmr->qkey), cl_ntoh16(p_mcmr->mlid),
			p_mcmr->mtu, p_mcmr->tclass, cl_ntoh16(p_mcmr->pkey),
			p_mcmr->rate, p_mcmr->pkt_life,
			cl_ntoh32(p_mcmr->sl_flow_hop),
			p_mcmr->scope_state, p_mcmr->proxy_join);
	}
}

void osm_dump_mc_record(IN osm_log_t * p_log, IN const ib_member_rec_t * p_mcmr,
			IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_mc_record_to_buf(p_mcmr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_mc_record_v2(IN osm_log_t * p_log, IN const ib_member_rec_t * p_mcmr,
			   IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_mc_record_to_buf(p_mcmr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_service_record_to_buf(IN const ib_service_record_t * p_sr,
					   OUT char * buf)
{
	if (!buf || !p_sr)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char buf_service_key[35];
		char buf_service_name[65];

		sprintf(buf_service_key,
			"0x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
			p_sr->service_key[0], p_sr->service_key[1],
			p_sr->service_key[2], p_sr->service_key[3],
			p_sr->service_key[4], p_sr->service_key[5],
			p_sr->service_key[6], p_sr->service_key[7],
			p_sr->service_key[8], p_sr->service_key[9],
			p_sr->service_key[10], p_sr->service_key[11],
			p_sr->service_key[12], p_sr->service_key[13],
			p_sr->service_key[14], p_sr->service_key[15]);
		strncpy(buf_service_name, (char *)p_sr->service_name, 64);
		buf_service_name[64] = '\0';

		sprintf(buf,
			"Service Record dump:\n"
			"\t\t\t\tServiceID...............0x%016" PRIx64 "\n"
			"\t\t\t\tServiceGID..............%s\n"
			"\t\t\t\tServiceP_Key............0x%X\n"
			"\t\t\t\tServiceLease............0x%X\n"
			"\t\t\t\tServiceKey..............%s\n"
			"\t\t\t\tServiceName.............%s\n"
			"\t\t\t\tServiceData8.1..........0x%X\n"
			"\t\t\t\tServiceData8.2..........0x%X\n"
			"\t\t\t\tServiceData8.3..........0x%X\n"
			"\t\t\t\tServiceData8.4..........0x%X\n"
			"\t\t\t\tServiceData8.5..........0x%X\n"
			"\t\t\t\tServiceData8.6..........0x%X\n"
			"\t\t\t\tServiceData8.7..........0x%X\n"
			"\t\t\t\tServiceData8.8..........0x%X\n"
			"\t\t\t\tServiceData8.9..........0x%X\n"
			"\t\t\t\tServiceData8.10.........0x%X\n"
			"\t\t\t\tServiceData8.11.........0x%X\n"
			"\t\t\t\tServiceData8.12.........0x%X\n"
			"\t\t\t\tServiceData8.13.........0x%X\n"
			"\t\t\t\tServiceData8.14.........0x%X\n"
			"\t\t\t\tServiceData8.15.........0x%X\n"
			"\t\t\t\tServiceData8.16.........0x%X\n"
			"\t\t\t\tServiceData16.1.........0x%X\n"
			"\t\t\t\tServiceData16.2.........0x%X\n"
			"\t\t\t\tServiceData16.3.........0x%X\n"
			"\t\t\t\tServiceData16.4.........0x%X\n"
			"\t\t\t\tServiceData16.5.........0x%X\n"
			"\t\t\t\tServiceData16.6.........0x%X\n"
			"\t\t\t\tServiceData16.7.........0x%X\n"
			"\t\t\t\tServiceData16.8.........0x%X\n"
			"\t\t\t\tServiceData32.1.........0x%X\n"
			"\t\t\t\tServiceData32.2.........0x%X\n"
			"\t\t\t\tServiceData32.3.........0x%X\n"
			"\t\t\t\tServiceData32.4.........0x%X\n"
			"\t\t\t\tServiceData64.1.........0x%016" PRIx64 "\n"
			"\t\t\t\tServiceData64.2.........0x%016" PRIx64 "\n",
			cl_ntoh64(p_sr->service_id),
			inet_ntop(AF_INET6, p_sr->service_gid.raw, gid_str,
				  sizeof gid_str),
			cl_ntoh16(p_sr->service_pkey),
			cl_ntoh32(p_sr->service_lease),
			buf_service_key, buf_service_name,
			p_sr->service_data8[0], p_sr->service_data8[1],
			p_sr->service_data8[2], p_sr->service_data8[3],
			p_sr->service_data8[4], p_sr->service_data8[5],
			p_sr->service_data8[6], p_sr->service_data8[7],
			p_sr->service_data8[8], p_sr->service_data8[9],
			p_sr->service_data8[10], p_sr->service_data8[11],
			p_sr->service_data8[12], p_sr->service_data8[13],
			p_sr->service_data8[14], p_sr->service_data8[15],
			cl_ntoh16(p_sr->service_data16[0]),
			cl_ntoh16(p_sr->service_data16[1]),
			cl_ntoh16(p_sr->service_data16[2]),
			cl_ntoh16(p_sr->service_data16[3]),
			cl_ntoh16(p_sr->service_data16[4]),
			cl_ntoh16(p_sr->service_data16[5]),
			cl_ntoh16(p_sr->service_data16[6]),
			cl_ntoh16(p_sr->service_data16[7]),
			cl_ntoh32(p_sr->service_data32[0]),
			cl_ntoh32(p_sr->service_data32[1]),
			cl_ntoh32(p_sr->service_data32[2]),
			cl_ntoh32(p_sr->service_data32[3]),
			cl_ntoh64(p_sr->service_data64[0]),
			cl_ntoh64(p_sr->service_data64[1]));
	}
}

void osm_dump_service_record(IN osm_log_t * p_log,
			     IN const ib_service_record_t * p_sr,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_service_record_to_buf(p_sr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_service_record_v2(IN osm_log_t * p_log,
				IN const ib_service_record_t * p_sr,
				IN const int file_id,
				IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_service_record_to_buf(p_sr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_inform_info_to_buf_generic(IN const ib_inform_info_t * p_ii,
						OUT char * buf)
{
	if (!buf || !p_ii)
		return;
	else {
		uint32_t qpn;
		uint8_t resp_time_val;
		char gid_str[INET6_ADDRSTRLEN];

		ib_inform_info_get_qpn_resp_time(p_ii->g_or_v.generic.
						 qpn_resp_time_val, &qpn,
						 &resp_time_val);
		sprintf(buf,
			"InformInfo dump:\n"
			"\t\t\t\tgid.....................%s\n"
			"\t\t\t\tlid_range_begin.........%u\n"
			"\t\t\t\tlid_range_end...........%u\n"
			"\t\t\t\tis_generic..............0x%X\n"
			"\t\t\t\tsubscribe...............0x%X\n"
			"\t\t\t\ttrap_type...............0x%X\n"
			"\t\t\t\ttrap_num................%u\n"
			"\t\t\t\tqpn.....................0x%06X\n"
			"\t\t\t\tresp_time_val...........0x%X\n"
			"\t\t\t\tnode_type...............0x%06X\n" "",
			inet_ntop(AF_INET6, p_ii->gid.raw, gid_str,
				  sizeof gid_str),
			cl_ntoh16(p_ii->lid_range_begin),
			cl_ntoh16(p_ii->lid_range_end),
			p_ii->is_generic, p_ii->subscribe,
			cl_ntoh16(p_ii->trap_type),
			cl_ntoh16(p_ii->g_or_v.generic.trap_num),
			cl_ntoh32(qpn), resp_time_val,
			cl_ntoh32(ib_inform_info_get_prod_type(p_ii)));
	}
}

static void osm_dump_inform_info_to_buf(IN const ib_inform_info_t * p_ii,
					OUT char * buf)
{
	if (!buf || !p_ii)
		return;
	else {
		uint32_t qpn;
		uint8_t resp_time_val;
		char gid_str[INET6_ADDRSTRLEN];

		ib_inform_info_get_qpn_resp_time(p_ii->g_or_v.generic.
						 qpn_resp_time_val, &qpn,
						 &resp_time_val);
		sprintf(buf,
			"InformInfo dump:\n"
			"\t\t\t\tgid.....................%s\n"
			"\t\t\t\tlid_range_begin.........%u\n"
			"\t\t\t\tlid_range_end...........%u\n"
			"\t\t\t\tis_generic..............0x%X\n"
			"\t\t\t\tsubscribe...............0x%X\n"
			"\t\t\t\ttrap_type...............0x%X\n"
			"\t\t\t\tdev_id..................0x%X\n"
			"\t\t\t\tqpn.....................0x%06X\n"
			"\t\t\t\tresp_time_val...........0x%X\n"
			"\t\t\t\tvendor_id...............0x%06X\n" "",
			inet_ntop(AF_INET6, p_ii->gid.raw, gid_str,
				  sizeof gid_str),
			cl_ntoh16(p_ii->lid_range_begin),
			cl_ntoh16(p_ii->lid_range_end),
			p_ii->is_generic, p_ii->subscribe,
			cl_ntoh16(p_ii->trap_type),
			cl_ntoh16(p_ii->g_or_v.vend.dev_id),
			cl_ntoh32(qpn), resp_time_val,
			cl_ntoh32(ib_inform_info_get_prod_type(p_ii)));
	}
}

void osm_dump_inform_info(IN osm_log_t * p_log,
			  IN const ib_inform_info_t * p_ii,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		if (p_ii->is_generic)
			osm_dump_inform_info_to_buf_generic(p_ii, buf);
		else
			osm_dump_inform_info_to_buf(p_ii, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_inform_info_v2(IN osm_log_t * p_log,
			     IN const ib_inform_info_t * p_ii,
			     IN const int file_id,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		if (p_ii->is_generic)
			osm_dump_inform_info_to_buf_generic(p_ii, buf);
		else
			osm_dump_inform_info_to_buf(p_ii, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_inform_info_record_to_buf_generic(IN const ib_inform_info_record_t * p_iir,
						       OUT char * buf)
{
	if (!buf || p_iir)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];
		uint32_t qpn;
		uint8_t resp_time_val;

		ib_inform_info_get_qpn_resp_time(p_iir->inform_info.g_or_v.
						 generic.qpn_resp_time_val,
						 &qpn, &resp_time_val);
		sprintf(buf,
			"InformInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tSubscriberGID...........%s\n"
			"\t\t\t\tSubscriberEnum..........0x%X\n"
			"\t\t\t\tInformInfo dump:\n"
			"\t\t\t\tgid.....................%s\n"
			"\t\t\t\tlid_range_begin.........%u\n"
			"\t\t\t\tlid_range_end...........%u\n"
			"\t\t\t\tis_generic..............0x%X\n"
			"\t\t\t\tsubscribe...............0x%X\n"
			"\t\t\t\ttrap_type...............0x%X\n"
			"\t\t\t\ttrap_num................%u\n"
			"\t\t\t\tqpn.....................0x%06X\n"
			"\t\t\t\tresp_time_val...........0x%X\n"
			"\t\t\t\tnode_type...............0x%06X\n" "",
			inet_ntop(AF_INET6, p_iir->subscriber_gid.raw,
				  gid_str, sizeof gid_str),
			cl_ntoh16(p_iir->subscriber_enum),
			inet_ntop(AF_INET6, p_iir->inform_info.gid.raw,
				  gid_str2, sizeof gid_str2),
			cl_ntoh16(p_iir->inform_info.lid_range_begin),
			cl_ntoh16(p_iir->inform_info.lid_range_end),
			p_iir->inform_info.is_generic,
			p_iir->inform_info.subscribe,
			cl_ntoh16(p_iir->inform_info.trap_type),
			cl_ntoh16(p_iir->inform_info.g_or_v.generic.
				  trap_num), cl_ntoh32(qpn),
			resp_time_val,
			cl_ntoh32(ib_inform_info_get_prod_type
				  (&p_iir->inform_info)));
	}
}

static void osm_dump_inform_info_record_to_buf(IN const ib_inform_info_record_t * p_iir,
					       OUT char * buf)
{
	if(!buf || p_iir)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];
		uint32_t qpn;
		uint8_t resp_time_val;

		ib_inform_info_get_qpn_resp_time(p_iir->inform_info.g_or_v.
						 generic.qpn_resp_time_val,
						 &qpn, &resp_time_val);
		sprintf(buf,
			"InformInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tSubscriberGID...........%s\n"
			"\t\t\t\tSubscriberEnum..........0x%X\n"
			"\t\t\t\tInformInfo dump:\n"
			"\t\t\t\tgid.....................%s\n"
			"\t\t\t\tlid_range_begin.........%u\n"
			"\t\t\t\tlid_range_end...........%u\n"
			"\t\t\t\tis_generic..............0x%X\n"
			"\t\t\t\tsubscribe...............0x%X\n"
			"\t\t\t\ttrap_type...............0x%X\n"
			"\t\t\t\tdev_id..................0x%X\n"
			"\t\t\t\tqpn.....................0x%06X\n"
			"\t\t\t\tresp_time_val...........0x%X\n"
			"\t\t\t\tvendor_id...............0x%06X\n" "",
			inet_ntop(AF_INET6, p_iir->subscriber_gid.raw,
				  gid_str, sizeof gid_str),
			cl_ntoh16(p_iir->subscriber_enum),
			inet_ntop(AF_INET6, p_iir->inform_info.gid.raw,
				  gid_str2, sizeof gid_str2),
			cl_ntoh16(p_iir->inform_info.lid_range_begin),
			cl_ntoh16(p_iir->inform_info.lid_range_end),
			p_iir->inform_info.is_generic,
			p_iir->inform_info.subscribe,
			cl_ntoh16(p_iir->inform_info.trap_type),
			cl_ntoh16(p_iir->inform_info.g_or_v.vend.
				  dev_id), cl_ntoh32(qpn),
			resp_time_val,
			cl_ntoh32(ib_inform_info_get_prod_type
				  (&p_iir->inform_info)));
	}
}

void osm_dump_inform_info_record(IN osm_log_t * p_log,
				 IN const ib_inform_info_record_t * p_iir,
				 IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		if (p_iir->inform_info.is_generic)
			osm_dump_inform_info_record_to_buf_generic(p_iir, buf);
		else
			osm_dump_inform_info_record_to_buf(p_iir, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_inform_info_record_v2(IN osm_log_t * p_log,
				    IN const ib_inform_info_record_t * p_iir,
				    IN const int file_id,
				    IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		if (p_iir->inform_info.is_generic)
			osm_dump_inform_info_record_to_buf_generic(p_iir, buf);
		else
			osm_dump_inform_info_record_to_buf(p_iir, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_link_record_to_buf(IN const ib_link_record_t * p_lr,
					OUT char * buf)
{
	if (!buf || !p_lr)
		return;
	else {
		sprintf(buf,
			"Link Record dump:\n"
			"\t\t\t\tfrom_lid................%u\n"
			"\t\t\t\tfrom_port_num...........%u\n"
			"\t\t\t\tto_port_num.............%u\n"
			"\t\t\t\tto_lid..................%u\n",
			cl_ntoh16(p_lr->from_lid),
			p_lr->from_port_num,
			p_lr->to_port_num, cl_ntoh16(p_lr->to_lid));
	}
}

void osm_dump_link_record(IN osm_log_t * p_log,
			  IN const ib_link_record_t * p_lr,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_link_record_to_buf(p_lr, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_link_record_v2(IN osm_log_t * p_log,
			     IN const ib_link_record_t * p_lr,
			     IN const int file_id,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_link_record_to_buf(p_lr, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_switch_info_to_buf(IN const ib_switch_info_t * p_si,
					OUT char * buf)
{
	if (!buf || !p_si)
		return;
	else {
		sprintf(buf,
			"SwitchInfo dump:\n"
			"\t\t\t\tlin_cap.................0x%X\n"
			"\t\t\t\trand_cap................0x%X\n"
			"\t\t\t\tmcast_cap...............0x%X\n"
			"\t\t\t\tlin_top.................0x%X\n"
			"\t\t\t\tdef_port................%u\n"
			"\t\t\t\tdef_mcast_pri_port......%u\n"
			"\t\t\t\tdef_mcast_not_port......%u\n"
			"\t\t\t\tlife_state..............0x%X\n"
			"\t\t\t\tlids_per_port...........%u\n"
			"\t\t\t\tpartition_enf_cap.......0x%X\n"
			"\t\t\t\tflags...................0x%X\n"
			"\t\t\t\tmcast_top...............0x%X\n",
			cl_ntoh16(p_si->lin_cap), cl_ntoh16(p_si->rand_cap),
			cl_ntoh16(p_si->mcast_cap), cl_ntoh16(p_si->lin_top),
			p_si->def_port, p_si->def_mcast_pri_port,
			p_si->def_mcast_not_port, p_si->life_state,
			cl_ntoh16(p_si->lids_per_port),
			cl_ntoh16(p_si->enforce_cap), p_si->flags,
			cl_ntoh16(p_si->mcast_top));
	}
}

void osm_dump_switch_info(IN osm_log_t * p_log,
			  IN const ib_switch_info_t * p_si,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_switch_info_to_buf(p_si, buf);

		osm_log(p_log, OSM_LOG_VERBOSE, "%s", buf);
	}
}

void osm_dump_switch_info_v2(IN osm_log_t * p_log,
			     IN const ib_switch_info_t * p_si,
			     IN const int file_id,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_switch_info_to_buf(p_si, buf);

		osm_log_v2(p_log, OSM_LOG_VERBOSE, file_id, "%s", buf);
	}
}

static void osm_dump_switch_info_record_to_buf(IN const ib_switch_info_record_t * p_sir,
					       OUT char * buf)
{
	if (!buf || !p_sir)
		return;
	else {
		sprintf(buf,
			"SwitchInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tlid.....................%u\n"
			"\t\t\t\tSwitchInfo dump:\n"
			"\t\t\t\tlin_cap.................0x%X\n"
			"\t\t\t\trand_cap................0x%X\n"
			"\t\t\t\tmcast_cap...............0x%X\n"
			"\t\t\t\tlin_top.................0x%X\n"
			"\t\t\t\tdef_port................%u\n"
			"\t\t\t\tdef_mcast_pri_port......%u\n"
			"\t\t\t\tdef_mcast_not_port......%u\n"
			"\t\t\t\tlife_state..............0x%X\n"
			"\t\t\t\tlids_per_port...........%u\n"
			"\t\t\t\tpartition_enf_cap.......0x%X\n"
			"\t\t\t\tflags...................0x%X\n",
			cl_ntoh16(p_sir->lid),
			cl_ntoh16(p_sir->switch_info.lin_cap),
			cl_ntoh16(p_sir->switch_info.rand_cap),
			cl_ntoh16(p_sir->switch_info.mcast_cap),
			cl_ntoh16(p_sir->switch_info.lin_top),
			p_sir->switch_info.def_port,
			p_sir->switch_info.def_mcast_pri_port,
			p_sir->switch_info.def_mcast_not_port,
			p_sir->switch_info.life_state,
			cl_ntoh16(p_sir->switch_info.lids_per_port),
			cl_ntoh16(p_sir->switch_info.enforce_cap),
			p_sir->switch_info.flags);
	}
}

void osm_dump_switch_info_record(IN osm_log_t * p_log,
				 IN const ib_switch_info_record_t * p_sir,
				 IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_switch_info_record_to_buf(p_sir, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_switch_info_record_v2(IN osm_log_t * p_log,
				    IN const ib_switch_info_record_t * p_sir,
				    IN const int file_id,
				    IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_switch_info_record_to_buf(p_sir, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_pkey_block_to_buf(IN uint64_t port_guid,
				       IN uint16_t block_num,
				       IN uint8_t port_num,
				       IN const ib_pkey_table_t * p_pkey_tbl,
				       OUT char * buf)
{
	if (!buf || !p_pkey_tbl)
		return;
	else {
		char buf_line[1024];
		int i, n;

		for (i = 0, n = 0; i < 32; i++)
			n += sprintf(buf_line + n, " 0x%04x |",
				     cl_ntoh16(p_pkey_tbl->pkey_entry[i]));

		sprintf(buf,
			"P_Key table dump:\n"
			"\t\t\tport_guid...........0x%016" PRIx64 "\n"
			"\t\t\tblock_num...........0x%X\n"
			"\t\t\tport_num............%u\n\tP_Key Table: %s\n",
			cl_ntoh64(port_guid), block_num, port_num, buf_line);
	}
}

void osm_dump_pkey_block(IN osm_log_t * p_log, IN uint64_t port_guid,
			 IN uint16_t block_num, IN uint8_t port_num,
			 IN const ib_pkey_table_t * p_pkey_tbl,
			 IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_pkey_block_to_buf(port_guid, block_num, port_num,
					   p_pkey_tbl, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_pkey_block_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
			    IN uint16_t block_num, IN uint8_t port_num,
			    IN const ib_pkey_table_t * p_pkey_tbl,
			    IN const int file_id,
			    IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_pkey_block_to_buf(port_guid, block_num,
					   port_num, p_pkey_tbl, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_slvl_map_table_to_buf(IN uint64_t port_guid,
					   IN uint8_t in_port_num,
					   IN uint8_t out_port_num,
					   IN const ib_slvl_table_t * p_slvl_tbl,
					   OUT char * buf)
{
	if (!buf || !p_slvl_tbl)
		return;
	else {
		char buf_line1[1024], buf_line2[1024];
		int n;
		uint8_t i;

		for (i = 0, n = 0; i < 16; i++)
			n += sprintf(buf_line1 + n, " %-2u |", i);
		for (i = 0, n = 0; i < 16; i++)
			n += sprintf(buf_line2 + n, "0x%01X |",
				     ib_slvl_table_get(p_slvl_tbl, i));
		sprintf(buf,
			"SLtoVL dump:\n"
			"\t\t\tport_guid............0x%016" PRIx64 "\n"
			"\t\t\tin_port_num..........%u\n"
			"\t\t\tout_port_num.........%u\n\tSL: | %s\n\tVL: | %s\n",
			cl_ntoh64(port_guid), in_port_num, out_port_num,
			buf_line1, buf_line2);
	}
}

void osm_dump_slvl_map_table(IN osm_log_t * p_log, IN uint64_t port_guid,
			     IN uint8_t in_port_num, IN uint8_t out_port_num,
			     IN const ib_slvl_table_t * p_slvl_tbl,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_slvl_map_table_to_buf(port_guid, in_port_num,
					       out_port_num, p_slvl_tbl, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_slvl_map_table_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
				IN uint8_t in_port_num, IN uint8_t out_port_num,
				IN const ib_slvl_table_t * p_slvl_tbl,
				IN const int file_id,
				IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_slvl_map_table_to_buf(port_guid, in_port_num,
					       out_port_num, p_slvl_tbl, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_vl_arb_table_to_buf(IN uint64_t port_guid,
					 IN uint8_t block_num,
					 IN uint8_t port_num,
					 IN const ib_vl_arb_table_t * p_vla_tbl,
					 OUT char * buf)
{
	if (!buf || !p_vla_tbl)
		return;
	else {
		char buf_line1[1024], buf_line2[1024];
		int i, n;

		for (i = 0, n = 0; i < 32; i++)
			n += sprintf(buf_line1 + n, " 0x%01X |",
				     p_vla_tbl->vl_entry[i].vl);
		for (i = 0, n = 0; i < 32; i++)
			n += sprintf(buf_line2 + n, " 0x%01X |",
				     p_vla_tbl->vl_entry[i].weight);
		sprintf(buf,
			"VLArb dump:\n" "\t\t\tport_guid...........0x%016"
			PRIx64 "\n" "\t\t\tblock_num...........0x%X\n"
			"\t\t\tport_num............%u\n\tVL    : | %s\n\tWEIGHT:| %s\n",
			cl_ntoh64(port_guid), block_num, port_num, buf_line1,
			buf_line2);
	}
}

void osm_dump_vl_arb_table(IN osm_log_t * p_log, IN uint64_t port_guid,
			   IN uint8_t block_num, IN uint8_t port_num,
			   IN const ib_vl_arb_table_t * p_vla_tbl,
			   IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_vl_arb_table_to_buf(port_guid, block_num,
					     port_num, p_vla_tbl, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_vl_arb_table_v2(IN osm_log_t * p_log, IN uint64_t port_guid,
			      IN uint8_t block_num, IN uint8_t port_num,
			      IN const ib_vl_arb_table_t * p_vla_tbl,
			      IN const int file_id,
			      IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_vl_arb_table_to_buf(port_guid, block_num,
					     port_num, p_vla_tbl, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_sm_info_to_buf(IN const ib_sm_info_t * p_smi,
				    OUT char * buf)
{
	if (!buf || !p_smi)
		return;
	else {
		sprintf(buf,
			"SMInfo dump:\n"
			"\t\t\t\tguid....................0x%016" PRIx64 "\n"
			"\t\t\t\tsm_key..................0x%016" PRIx64 "\n"
			"\t\t\t\tact_count...............%u\n"
			"\t\t\t\tpriority................%u\n"
			"\t\t\t\tsm_state................%u\n",
			cl_ntoh64(p_smi->guid), cl_ntoh64(p_smi->sm_key),
			cl_ntoh32(p_smi->act_count),
			ib_sminfo_get_priority(p_smi),
			ib_sminfo_get_state(p_smi));
	}
}

void osm_dump_sm_info(IN osm_log_t * p_log, IN const ib_sm_info_t * p_smi,
		      IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_sm_info_to_buf(p_smi, buf);

		osm_log(p_log, OSM_LOG_DEBUG, "%s", buf);
	}
}

void osm_dump_sm_info_v2(IN osm_log_t * p_log, IN const ib_sm_info_t * p_smi,
			 IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_sm_info_to_buf(p_smi, buf);

		osm_log_v2(p_log, OSM_LOG_DEBUG, file_id, "%s", buf);
	}
}

static void osm_dump_sm_info_record_to_buf(IN const ib_sminfo_record_t * p_smir,
					   OUT char * buf)
{
	if (!buf || !p_smir)
		return;
	else {
		sprintf(buf,
			"SMInfo Record dump:\n"
			"\t\t\t\tRID\n"
			"\t\t\t\tLid.....................%u\n"
			"\t\t\t\tReserved................0x%X\n"
			"\t\t\t\tSMInfo dump:\n"
			"\t\t\t\tguid....................0x%016" PRIx64 "\n"
			"\t\t\t\tsm_key..................0x%016" PRIx64 "\n"
			"\t\t\t\tact_count...............%u\n"
			"\t\t\t\tpriority................%u\n"
			"\t\t\t\tsm_state................%u\n",
			cl_ntoh16(p_smir->lid), cl_ntoh16(p_smir->resv0),
			cl_ntoh64(p_smir->sm_info.guid),
			cl_ntoh64(p_smir->sm_info.sm_key),
			cl_ntoh32(p_smir->sm_info.act_count),
			ib_sminfo_get_priority(&p_smir->sm_info),
			ib_sminfo_get_state(&p_smir->sm_info));
	}
}

void osm_dump_sm_info_record(IN osm_log_t * p_log,
			     IN const ib_sminfo_record_t * p_smir,
			     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_sm_info_record_to_buf(p_smir, buf);

		osm_log(p_log, OSM_LOG_DEBUG, "%s", buf);
	}
}

void osm_dump_sm_info_record_v2(IN osm_log_t * p_log,
				IN const ib_sminfo_record_t * p_smir,
				IN const int file_id,
				IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_sm_info_record_to_buf(p_smir, buf);

		osm_log_v2(p_log, OSM_LOG_DEBUG, file_id, "%s", buf);
	}
}

static void osm_dump_notice_to_buf_generic(IN const ib_mad_notice_attr_t * p_ntci,
					   OUT char * log_buf)
{
	if (!log_buf || !p_ntci)
		return;
	else {
		char gid_str[INET6_ADDRSTRLEN];
		char gid_str2[INET6_ADDRSTRLEN];
		char buff[1024];
		int n;

		buff[0] = '\0';

		/* immediate data based on the trap */
		switch (cl_ntoh16(p_ntci->g_or_v.generic.trap_num)) {
		case SM_GID_IN_SERVICE_TRAP:	/* 64 */
		case SM_GID_OUT_OF_SERVICE_TRAP: /* 65 */
		case SM_MGID_CREATED_TRAP:	/* 66 */
		case SM_MGID_DESTROYED_TRAP:	/* 67 */
			sprintf(buff,
				"\t\t\t\tsrc_gid..................%s\n",
				inet_ntop(AF_INET6, p_ntci->data_details.
					  ntc_64_67.gid.raw, gid_str,
					  sizeof gid_str));
			break;
		case SM_LINK_STATE_CHANGED_TRAP: /* 128 */
			sprintf(buff,
				"\t\t\t\tsw_lid...................%u\n",
				cl_ntoh16(p_ntci->data_details.ntc_128.sw_lid));
			break;
		case SM_LINK_INTEGRITY_THRESHOLD_TRAP: /* 129 */
		case SM_BUFFER_OVERRUN_THRESHOLD_TRAP: /* 130 */
		case SM_WATCHDOG_TIMER_EXPIRED_TRAP:   /* 131 */
			sprintf(buff,
				"\t\t\t\tlid......................%u\n"
				"\t\t\t\tport_num.................%u\n",
				cl_ntoh16(p_ntci->data_details.
					  ntc_129_131.lid),
				p_ntci->data_details.ntc_129_131.port_num);
			break;
		case SM_LOCAL_CHANGES_TRAP:	/* 144 */
			sprintf(buff,
				"\t\t\t\tlid......................%u\n"
				"\t\t\t\tlocal_changes............%u\n"
				"\t\t\t\tnew_cap_mask.............0x%08x\n"
				"\t\t\t\tchange_flags.............0x%x\n"
				"\t\t\t\tcap_mask2................0x%x\n",
				cl_ntoh16(p_ntci->data_details.ntc_144.lid),
				p_ntci->data_details.ntc_144.local_changes,
				cl_ntoh32(p_ntci->data_details.ntc_144.
					  new_cap_mask),
				cl_ntoh16(p_ntci->data_details.ntc_144.
					  change_flgs),
				cl_ntoh16(p_ntci->data_details.ntc_144.
					  cap_mask2));
			break;
		case SM_SYS_IMG_GUID_CHANGED_TRAP: /* 145 */
			sprintf(buff,
				"\t\t\t\tlid......................%u\n"
				"\t\t\t\tnew_sys_guid.............0x%016"
				PRIx64 "\n",
				cl_ntoh16(p_ntci->data_details.ntc_145.
					  lid),
				cl_ntoh64(p_ntci->data_details.ntc_145.
					  new_sys_guid));
			break;
		case SM_BAD_MKEY_TRAP:	/* 256 */
			n = sprintf(buff,
				    "\t\t\t\tlid......................%u\n"
				    "\t\t\t\tdrslid...................%u\n"
				    "\t\t\t\tmethod...................0x%x\n"
				    "\t\t\t\tattr_id..................0x%x\n"
				    "\t\t\t\tattr_mod.................0x%x\n"
				    "\t\t\t\tm_key....................0x%016"
				    PRIx64 "\n"
				    "\t\t\t\tdr_notice................%d\n"
				    "\t\t\t\tdr_path_truncated........%d\n"
				    "\t\t\t\tdr_hop_count.............%u\n",
				    cl_ntoh16(p_ntci->data_details.ntc_256.lid),
				    cl_ntoh16(p_ntci->data_details.ntc_256.
					      dr_slid),
				    p_ntci->data_details.ntc_256.method,
				    cl_ntoh16(p_ntci->data_details.ntc_256.
					      attr_id),
				    cl_ntoh32(p_ntci->data_details.ntc_256.
					      attr_mod),
				    cl_ntoh64(p_ntci->data_details.ntc_256.
					      mkey),
				    p_ntci->data_details.ntc_256.
				    dr_trunc_hop >> 7,
				    p_ntci->data_details.ntc_256.
				    dr_trunc_hop >> 6,
				    p_ntci->data_details.ntc_256.
				    dr_trunc_hop & 0x3f);
			n += snprintf(buff + n, sizeof(buff) - n,
				      "Directed Path Dump of %u hop path:"
				      "\n\t\t\t\tPath = ",
				      p_ntci->data_details.ntc_256.
				      dr_trunc_hop & 0x3f);
			n += sprint_uint8_arr(buff + n, sizeof(buff) - n,
					      p_ntci->data_details.ntc_256.
					      dr_rtn_path,
					      (p_ntci->data_details.ntc_256.
					       dr_trunc_hop & 0x3f) + 1);
			if (n >= sizeof(buff)) {
				n = sizeof(buff) - 2;
				break;
			}
			snprintf(buff + n, sizeof(buff) - n, "\n");
			break;
		case SM_BAD_PKEY_TRAP:	/* 257 */
		case SM_BAD_QKEY_TRAP:	/* 258 */
			sprintf(buff,
				"\t\t\t\tlid1.....................%u\n"
				"\t\t\t\tlid2.....................%u\n"
				"\t\t\t\tkey......................0x%x\n"
				"\t\t\t\tsl.......................%d\n"
				"\t\t\t\tqp1......................0x%x\n"
				"\t\t\t\tqp2......................0x%x\n"
				"\t\t\t\tgid1.....................%s\n"
				"\t\t\t\tgid2.....................%s\n",
				cl_ntoh16(p_ntci->data_details.ntc_257_258.
					  lid1),
				cl_ntoh16(p_ntci->data_details.ntc_257_258.
					  lid2),
				cl_ntoh32(p_ntci->data_details.ntc_257_258.key),
				cl_ntoh32(p_ntci->data_details.ntc_257_258.
					  qp1) >> 28,
				cl_ntoh32(p_ntci->data_details.ntc_257_258.
					  qp1) & 0xffffff,
				cl_ntoh32(p_ntci->data_details.ntc_257_258.
					  qp2) & 0xffffff,
				inet_ntop(AF_INET6, p_ntci->data_details.
					  ntc_257_258.gid1.raw, gid_str,
					  sizeof gid_str),
				inet_ntop(AF_INET6, p_ntci->data_details.
					  ntc_257_258.gid2.raw, gid_str2,
					  sizeof gid_str2));
			break;
		case SM_BAD_SWITCH_PKEY_TRAP:	/* 259 */
			sprintf(buff,
				"\t\t\t\tdata_valid...............0x%x\n"
				"\t\t\t\tlid1.....................%u\n"
				"\t\t\t\tlid2.....................%u\n"
				"\t\t\t\tpkey.....................0x%x\n"
				"\t\t\t\tsl.......................%d\n"
				"\t\t\t\tqp1......................0x%x\n"
				"\t\t\t\tqp2......................0x%x\n"
				"\t\t\t\tgid1.....................%s\n"
				"\t\t\t\tgid2.....................%s\n"
				"\t\t\t\tsw_lid...................%u\n"
				"\t\t\t\tport_no..................%u\n",
				cl_ntoh16(p_ntci->data_details.ntc_259.
					  data_valid),
				cl_ntoh16(p_ntci->data_details.ntc_259.lid1),
				cl_ntoh16(p_ntci->data_details.ntc_259.lid2),
				cl_ntoh16(p_ntci->data_details.ntc_259.pkey),
				cl_ntoh32(p_ntci->data_details.ntc_259.
					  sl_qp1) >> 24,
				cl_ntoh32(p_ntci->data_details.ntc_259.
					  sl_qp1) & 0xffffff,
				cl_ntoh32(p_ntci->data_details.ntc_259.qp2),
				inet_ntop(AF_INET6, p_ntci->data_details.
					  ntc_259.gid1.raw, gid_str,
					  sizeof gid_str),
				inet_ntop(AF_INET6, p_ntci->data_details.
					  ntc_259.gid2.raw, gid_str2,
					  sizeof gid_str2),
				cl_ntoh16(p_ntci->data_details.ntc_259.sw_lid),
				p_ntci->data_details.ntc_259.port_no);
			break;
		}

		sprintf(log_buf,
			"Generic Notice dump:\n"
			"\t\t\t\ttype.....................%u\n"
			"\t\t\t\tprod_type................%u (%s)\n"
			"\t\t\t\ttrap_num.................%u\n%s",
			ib_notice_get_type(p_ntci),
			cl_ntoh32(ib_notice_get_prod_type(p_ntci)),
			ib_get_producer_type_str(ib_notice_get_prod_type
						 (p_ntci)),
			cl_ntoh16(p_ntci->g_or_v.generic.trap_num), buff);
	}
}

static void osm_dump_notice_to_buf(IN const ib_mad_notice_attr_t * p_ntci,
				   OUT char * buf)
{
	if (!buf || !p_ntci)
		return;
	else {
		sprintf(buf,
			"Vendor Notice dump:\n"
			"\t\t\t\ttype.....................%u\n"
			"\t\t\t\tvendor...................%u\n"
			"\t\t\t\tdevice_id................%u\n",
			cl_ntoh16(ib_notice_get_type(p_ntci)),
			cl_ntoh32(ib_notice_get_vend_id(p_ntci)),
			cl_ntoh16(p_ntci->g_or_v.vend.dev_id));
	}
}

void osm_dump_notice(IN osm_log_t * p_log,
		     IN const ib_mad_notice_attr_t * p_ntci,
		     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		if (ib_notice_is_generic(p_ntci))
			osm_dump_notice_to_buf_generic(p_ntci, buf);
		else
			osm_dump_notice_to_buf(p_ntci, buf);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_notice_v2(IN osm_log_t * p_log,
			IN const ib_mad_notice_attr_t * p_ntci,
			IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		if (ib_notice_is_generic(p_ntci))
			osm_dump_notice_to_buf_generic(p_ntci, buf);
		else
			osm_dump_notice_to_buf(p_ntci, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_dr_smp_to_buf(IN const ib_smp_t * p_smp, OUT char * buf,
				   IN size_t buf_size)
{
	if (!buf || !p_smp)
		return;
	else {
		unsigned n;

		n = sprintf(buf,
			    "SMP dump:\n"
			    "\t\t\t\tbase_ver................0x%X\n"
			    "\t\t\t\tmgmt_class..............0x%X\n"
			    "\t\t\t\tclass_ver...............0x%X\n"
			    "\t\t\t\tmethod..................0x%X (%s)\n",
			    p_smp->base_ver, p_smp->mgmt_class,
			    p_smp->class_ver, p_smp->method,
			    ib_get_sm_method_str(p_smp->method));

		if (p_smp->mgmt_class == IB_MCLASS_SUBN_DIR) {
			n += snprintf(buf + n, buf_size - n,
				      "\t\t\t\tD bit...................0x%X\n"
				      "\t\t\t\tstatus..................0x%X\n",
				      ib_smp_is_d(p_smp),
				      cl_ntoh16(ib_smp_get_status(p_smp)));
		} else {
			n += snprintf(buf + n, buf_size - n,
				      "\t\t\t\tstatus..................0x%X\n",
				      cl_ntoh16(p_smp->status));
		}

		n += snprintf(buf + n, buf_size - n,
			      "\t\t\t\thop_ptr.................0x%X\n"
			      "\t\t\t\thop_count...............0x%X\n"
			      "\t\t\t\ttrans_id................0x%" PRIx64 "\n"
			      "\t\t\t\tattr_id.................0x%X (%s)\n"
			      "\t\t\t\tresv....................0x%X\n"
			      "\t\t\t\tattr_mod................0x%X\n"
			      "\t\t\t\tm_key...................0x%016" PRIx64
			      "\n", p_smp->hop_ptr, p_smp->hop_count,
			      cl_ntoh64(p_smp->trans_id),
			      cl_ntoh16(p_smp->attr_id),
			      ib_get_sm_attr_str(p_smp->attr_id),
			      cl_ntoh16(p_smp->resv),
			      cl_ntoh32(p_smp->attr_mod),
			      cl_ntoh64(p_smp->m_key));

		if (p_smp->mgmt_class == IB_MCLASS_SUBN_DIR) {
			uint32_t i;
			n += snprintf(buf + n, buf_size - n,
				      "\t\t\t\tdr_slid.................%u\n"
				      "\t\t\t\tdr_dlid.................%u\n",
				      cl_ntoh16(p_smp->dr_slid),
				      cl_ntoh16(p_smp->dr_dlid));

			n += snprintf(buf + n, buf_size - n,
				      "\n\t\t\t\tInitial path: ");
			n += sprint_uint8_arr(buf + n, buf_size - n,
					      p_smp->initial_path,
					      p_smp->hop_count + 1);

			n += snprintf(buf + n, buf_size - n,
				      "\n\t\t\t\tReturn path:  ");
			n += sprint_uint8_arr(buf + n, buf_size - n,
					      p_smp->return_path,
					      p_smp->hop_count + 1);

			n += snprintf(buf + n, buf_size - n,
				      "\n\t\t\t\tReserved:     ");
			for (i = 0; i < 7; i++) {
				n += snprintf(buf + n, buf_size - n,
					      "[%0X]", p_smp->resv1[i]);
			}
			n += snprintf(buf + n, buf_size - n, "\n");

			for (i = 0; i < 64; i += 16) {
				n += snprintf(buf + n, buf_size - n,
					      "\n\t\t\t\t%02X %02X %02X %02X "
					      "%02X %02X %02X %02X"
					      "   %02X %02X %02X %02X %02X %02X %02X %02X\n",
					      p_smp->data[i],
					      p_smp->data[i + 1],
					      p_smp->data[i + 2],
					      p_smp->data[i + 3],
					      p_smp->data[i + 4],
					      p_smp->data[i + 5],
					      p_smp->data[i + 6],
					      p_smp->data[i + 7],
					      p_smp->data[i + 8],
					      p_smp->data[i + 9],
					      p_smp->data[i + 10],
					      p_smp->data[i + 11],
					      p_smp->data[i + 12],
					      p_smp->data[i + 13],
					      p_smp->data[i + 14],
					      p_smp->data[i + 15]);
			}
		} else {
			/* not a Direct Route so provide source and destination lids */
			n += snprintf(buf + n, buf_size - n,
				      "\t\t\t\tMAD IS LID ROUTED\n");
		}
	}
}

void osm_dump_dr_smp(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
		     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_dr_smp_to_buf(p_smp, buf, BUF_SIZE);

		osm_log(p_log, log_level, "%s", buf);
	}
}

void osm_dump_dr_smp_v2(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_dr_smp_to_buf(p_smp, buf, BUF_SIZE);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_sa_mad_to_buf(IN const ib_sa_mad_t * p_mad, OUT char * buf)
{
	if (!buf || !p_mad)
		return;
	else {
		/* make sure the mad is valid */
		if (p_mad == NULL) {
			sprintf(buf, "NULL MAD POINTER\n");
			return;
		}

		sprintf(buf,
			"SA MAD dump:\n"
			"\t\t\t\tbase_ver................0x%X\n"
			"\t\t\t\tmgmt_class..............0x%X\n"
			"\t\t\t\tclass_ver...............0x%X\n"
			"\t\t\t\tmethod..................0x%X (%s)\n"
			"\t\t\t\tstatus..................0x%X\n"
			"\t\t\t\tresv....................0x%X\n"
			"\t\t\t\ttrans_id................0x%" PRIx64 "\n"
			"\t\t\t\tattr_id.................0x%X (%s)\n"
			"\t\t\t\tresv1...................0x%X\n"
			"\t\t\t\tattr_mod................0x%X\n"
			"\t\t\t\trmpp_version............0x%X\n"
			"\t\t\t\trmpp_type...............0x%X\n"
			"\t\t\t\trmpp_flags..............0x%X\n"
			"\t\t\t\trmpp_status.............0x%X\n"
			"\t\t\t\tseg_num.................0x%X\n"
			"\t\t\t\tpayload_len/new_win.....0x%X\n"
			"\t\t\t\tsm_key..................0x%016" PRIx64 "\n"
			"\t\t\t\tattr_offset.............0x%X\n"
			"\t\t\t\tresv2...................0x%X\n"
			"\t\t\t\tcomp_mask...............0x%016" PRIx64 "\n",
			p_mad->base_ver, p_mad->mgmt_class, p_mad->class_ver,
			p_mad->method, ib_get_sa_method_str(p_mad->method),
			cl_ntoh16(p_mad->status), cl_ntoh16(p_mad->resv),
			cl_ntoh64(p_mad->trans_id), cl_ntoh16(p_mad->attr_id),
			ib_get_sa_attr_str(p_mad->attr_id),
			cl_ntoh16(p_mad->resv1), cl_ntoh32(p_mad->attr_mod),
			p_mad->rmpp_version, p_mad->rmpp_type,
			p_mad->rmpp_flags, p_mad->rmpp_status,
			cl_ntoh32(p_mad->seg_num),
			cl_ntoh32(p_mad->paylen_newwin),
			cl_ntoh64(p_mad->sm_key), cl_ntoh16(p_mad->attr_offset),
			cl_ntoh16(p_mad->resv3), cl_ntoh64(p_mad->comp_mask));

		strcat(buf, "\n");
	}
}

void osm_dump_sa_mad(IN osm_log_t * p_log, IN const ib_sa_mad_t * p_mad,
		     IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_sa_mad_to_buf(p_mad, buf);

		osm_log(p_log, log_level, "%s\n", buf);
	}
}

void osm_dump_sa_mad_v2(IN osm_log_t * p_log, IN const ib_sa_mad_t * p_mad,
			IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_sa_mad_to_buf(p_mad, buf);

		osm_log_v2(p_log, log_level, file_id, "%s", buf);
	}
}

static void osm_dump_dr_path_to_buf(IN const osm_dr_path_t * p_path,
				    OUT char * buf, IN size_t buf_size)
{
	if (!buf || !p_path)
		return;
	else {
		unsigned n = 0;

		n = sprintf(buf, "Directed Path Dump of %u hop path: "
			    "Path = ", p_path->hop_count);

		sprint_uint8_arr(buf + n, buf_size - n, p_path->path,
				 p_path->hop_count + 1);
	}
}

void osm_dump_dr_path(IN osm_log_t * p_log, IN const osm_dr_path_t * p_path,
		      IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_dr_path_to_buf(p_path, buf, BUF_SIZE);

		osm_log(p_log, log_level, "%s\n", buf);
	}
}

void osm_dump_dr_path_v2(IN osm_log_t * p_log, IN const osm_dr_path_t * p_path,
			 IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_dr_path_to_buf(p_path, buf, BUF_SIZE);

		osm_log_v2(p_log, log_level, file_id, "%s\n", buf);
	}
}

static void osm_dump_smp_dr_path_to_buf(IN const ib_smp_t * p_smp,
					OUT char * buf, IN size_t buf_size)
{
	if (!buf || !p_smp)
		return;
	else {
		unsigned n;

		n = sprintf(buf, "Received SMP on a %u hop path: "
			    "Initial path = ", p_smp->hop_count);
		n += sprint_uint8_arr(buf + n, buf_size - n,
				      p_smp->initial_path,
				      p_smp->hop_count + 1);

		n += snprintf(buf + n, buf_size - n, ", Return path  = ");
		n += sprint_uint8_arr(buf + n, buf_size - n,
				      p_smp->return_path, p_smp->hop_count + 1);
	}
}

void osm_dump_smp_dr_path(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			  IN osm_log_level_t log_level)
{
	if (osm_log_is_active(p_log, log_level)) {
		char buf[BUF_SIZE];

		osm_dump_smp_dr_path_to_buf(p_smp, buf, BUF_SIZE);

		osm_log(p_log, log_level, "%s\n", buf);
	}
}

void osm_dump_smp_dr_path_v2(IN osm_log_t * p_log, IN const ib_smp_t * p_smp,
			     IN const int file_id, IN osm_log_level_t log_level)
{
	if (osm_log_is_active_v2(p_log, log_level, file_id)) {
		char buf[BUF_SIZE];

		osm_dump_smp_dr_path_to_buf(p_smp, buf, BUF_SIZE);

		osm_log_v2(p_log, log_level, file_id, "%s\n", buf);
	}
}

void osm_dump_dr_path_as_buf(IN size_t max_len,
			     IN const osm_dr_path_t * p_path,
			     OUT char* buf)
{
	sprint_uint8_arr(buf, max_len, p_path->path, p_path->hop_count + 1);
}

static const char *sm_signal_str[] = {
	"OSM_SIGNAL_NONE",	/* 0 */
	"OSM_SIGNAL_SWEEP",	/* 1 */
	"OSM_SIGNAL_IDLE_TIME_PROCESS_REQUEST",	/* 2 */
	"OSM_SIGNAL_PERFMGR_SWEEP",	/* 3 */
	"OSM_SIGNAL_GUID_PROCESS_REQUEST",	/* 4 */
	"UNKNOWN SIGNAL!!"	/* 5 */
};

const char *osm_get_sm_signal_str(IN osm_signal_t signal)
{
	if (signal > OSM_SIGNAL_MAX)
		signal = OSM_SIGNAL_MAX;
	return sm_signal_str[signal];
}

static const char *disp_msg_str[] = {
	"OSM_MSG_NONE",
	"OSM_MSG_MAD_NODE_INFO",
	"OSM_MSG_MAD_PORT_INFO",
	"OSM_MSG_MAD_SWITCH_INFO",
	"OSM_MSG_MAD_GUID_INFO",
	"OSM_MSG_MAD_NODE_DESC",
	"OSM_MSG_MAD_NODE_RECORD",
	"OSM_MSG_MAD_PORTINFO_RECORD",
	"OSM_MSG_MAD_SERVICE_RECORD",
	"OSM_MSG_MAD_PATH_RECORD",
	"OSM_MSG_MAD_MCMEMBER_RECORD",
	"OSM_MSG_MAD_LINK_RECORD",
	"OSM_MSG_MAD_SMINFO_RECORD",
	"OSM_MSG_MAD_CLASS_PORT_INFO",
	"OSM_MSG_MAD_INFORM_INFO",
	"OSM_MSG_MAD_LFT_RECORD",
	"OSM_MSG_MAD_LFT",
	"OSM_MSG_MAD_SM_INFO",
	"OSM_MSG_MAD_NOTICE",
	"OSM_MSG_LIGHT_SWEEP_FAIL",
	"OSM_MSG_MAD_MFT",
	"OSM_MSG_MAD_PKEY_TBL_RECORD",
	"OSM_MSG_MAD_VL_ARB_RECORD",
	"OSM_MSG_MAD_SLVL_TBL_RECORD",
	"OSM_MSG_MAD_PKEY",
	"OSM_MSG_MAD_VL_ARB",
	"OSM_MSG_MAD_SLVL",
	"OSM_MSG_MAD_GUIDINFO_RECORD",
	"OSM_MSG_MAD_INFORM_INFO_RECORD",
	"OSM_MSG_MAD_SWITCH_INFO_RECORD",
	"OSM_MSG_MAD_MFT_RECORD",
#if defined (VENDOR_RMPP_SUPPORT) && defined (DUAL_SIDED_RMPP)
	"OSM_MSG_MAD_MULTIPATH_RECORD",
#endif
	"OSM_MSG_MAD_PORT_COUNTERS",
	"OSM_MSG_MAD_MLNX_EXT_PORT_INFO",
	"UNKNOWN!!"
};

const char *osm_get_disp_msg_str(IN cl_disp_msgid_t msg)
{
	if (msg >= OSM_MSG_MAX)
		msg = OSM_MSG_MAX-1;
	return disp_msg_str[msg];
}

static const char *port_state_str_fixed_width[] = {
	"NOC",
	"DWN",
	"INI",
	"ARM",
	"ACT",
	"???"
};

const char *osm_get_port_state_str_fixed_width(IN uint8_t port_state)
{
	if (port_state > IB_LINK_ACTIVE)
		port_state = IB_LINK_ACTIVE + 1;
	return port_state_str_fixed_width[port_state];
}

static const char *node_type_str_fixed_width[] = {
	"??",
	"CA",
	"SW",
	"RT",
};

const char *osm_get_node_type_str_fixed_width(IN uint8_t node_type)
{
	if (node_type > IB_NODE_TYPE_ROUTER)
		node_type = 0;
	return node_type_str_fixed_width[node_type];
}

const char *osm_get_manufacturer_str(IN uint64_t guid_ho)
{
	/* note that the max vendor string length is 11 */
	static const char *intel_str = "Intel";
	static const char *mellanox_str = "Mellanox";
	static const char *redswitch_str = "Redswitch";
	static const char *silverstorm_str = "SilverStorm";
	static const char *topspin_str = "Topspin";
	static const char *fujitsu_str = "Fujitsu";
	static const char *voltaire_str = "Voltaire";
	static const char *yotta_str = "YottaYotta";
	static const char *pathscale_str = "PathScale";
	static const char *ibm_str = "IBM";
	static const char *divergenet_str = "DivergeNet";
	static const char *flextronics_str = "Flextronics";
	static const char *agilent_str = "Agilent";
	static const char *obsidian_str = "Obsidian";
	static const char *baymicro_str = "BayMicro";
	static const char *lsilogic_str = "LSILogic";
	static const char *ddn_str = "DataDirect";
	static const char *panta_str = "Panta";
	static const char *hp_str = "HP";
	static const char *rioworks_str = "Rioworks";
	static const char *sun_str = "Sun";
	static const char *leafntwks_str = "3LeafNtwks";
	static const char *xsigo_str = "Xsigo";
	static const char *dell_str = "Dell";
	static const char *supermicro_str = "SuperMicro";
	static const char *openib_str = "OpenIB";
	static const char *unknown_str = "Unknown";
	static const char *bull_str = "Bull";

	switch ((uint32_t) (guid_ho >> (5 * 8))) {
	case OSM_VENDOR_ID_INTEL:
		return intel_str;
	case OSM_VENDOR_ID_MELLANOX:
	case OSM_VENDOR_ID_MELLANOX2:
	case OSM_VENDOR_ID_MELLANOX3:
	case OSM_VENDOR_ID_MELLANOX4:
	case OSM_VENDOR_ID_MELLANOX5:
		return mellanox_str;
	case OSM_VENDOR_ID_REDSWITCH:
		return redswitch_str;
	case OSM_VENDOR_ID_SILVERSTORM:
		return silverstorm_str;
	case OSM_VENDOR_ID_TOPSPIN:
		return topspin_str;
	case OSM_VENDOR_ID_FUJITSU:
	case OSM_VENDOR_ID_FUJITSU2:
		return fujitsu_str;
	case OSM_VENDOR_ID_VOLTAIRE:
		return voltaire_str;
	case OSM_VENDOR_ID_YOTTAYOTTA:
		return yotta_str;
	case OSM_VENDOR_ID_PATHSCALE:
		return pathscale_str;
	case OSM_VENDOR_ID_IBM:
	case OSM_VENDOR_ID_IBM2:
		return ibm_str;
	case OSM_VENDOR_ID_DIVERGENET:
		return divergenet_str;
	case OSM_VENDOR_ID_FLEXTRONICS:
		return flextronics_str;
	case OSM_VENDOR_ID_AGILENT:
		return agilent_str;
	case OSM_VENDOR_ID_OBSIDIAN:
		return obsidian_str;
	case OSM_VENDOR_ID_BAYMICRO:
		return baymicro_str;
	case OSM_VENDOR_ID_LSILOGIC:
		return lsilogic_str;
	case OSM_VENDOR_ID_DDN:
		return ddn_str;
	case OSM_VENDOR_ID_PANTA:
		return panta_str;
	case OSM_VENDOR_ID_HP:
	case OSM_VENDOR_ID_HP2:
	case OSM_VENDOR_ID_HP3:
	case OSM_VENDOR_ID_HP4:
		return hp_str;
	case OSM_VENDOR_ID_RIOWORKS:
		return rioworks_str;
	case OSM_VENDOR_ID_SUN:
	case OSM_VENDOR_ID_SUN2:
		return sun_str;
	case OSM_VENDOR_ID_3LEAFNTWKS:
		return leafntwks_str;
	case OSM_VENDOR_ID_XSIGO:
		return xsigo_str;
	case OSM_VENDOR_ID_DELL:
		return dell_str;
	case OSM_VENDOR_ID_SUPERMICRO:
		return supermicro_str;
	case OSM_VENDOR_ID_OPENIB:
		return openib_str;
	case OSM_VENDOR_ID_BULL:
		return bull_str;
	default:
		return unknown_str;
	}
}

static const char *mtu_str_fixed_width[] = {
	"??? ",
	"256 ",
	"512 ",
	"1024",
	"2048",
	"4096"
};

const char *osm_get_mtu_str(IN uint8_t mtu)
{
	if (mtu > IB_MTU_LEN_4096)
		return mtu_str_fixed_width[0];
	else
		return mtu_str_fixed_width[mtu];
}

static const char *lwa_str_fixed_width[] = {
	"???",
	"1x ",
	"4x ",
	"???",
	"8x ",
	"???",
	"???",
	"???",
	"12x",
	"???",
	"???",
	"???",
	"???",
	"???",
	"???",
	"???",
	"2x "
};

const char *osm_get_lwa_str(IN uint8_t lwa)
{
	if (lwa > 16)
		return lwa_str_fixed_width[0];
	else
		return lwa_str_fixed_width[lwa];
}

static const char *lsa_str_fixed_width[] = {
	"Ext ",
	"2.5 ",
	"5   ",
	"????",
	"10  "
};

static const char *lsea_str_fixed_width[] = {
	"Std ",
	"14  ",
	"25  "
};

const char *osm_get_lsa_str(IN uint8_t lsa, IN uint8_t lsea, IN uint8_t state,
			    IN uint8_t fdr10)
{
	if (lsa > IB_LINK_SPEED_ACTIVE_10 || state == IB_LINK_DOWN)
		return lsa_str_fixed_width[3];
	if (lsea == IB_LINK_SPEED_EXT_ACTIVE_NONE) {
		if (fdr10)
			return "FDR10";
		else
			return lsa_str_fixed_width[lsa];
	}
	if (lsea > IB_LINK_SPEED_EXT_ACTIVE_25)
		return lsa_str_fixed_width[3];
	return lsea_str_fixed_width[lsea];
}

static const char *sm_mgr_signal_str[] = {
	"OSM_SM_SIGNAL_NONE",	/* 0 */
	"OSM_SM_SIGNAL_DISCOVERY_COMPLETED",	/* 1 */
	"OSM_SM_SIGNAL_POLLING_TIMEOUT",	/* 2 */
	"OSM_SM_SIGNAL_DISCOVER",	/* 3 */
	"OSM_SM_SIGNAL_DISABLE",	/* 4 */
	"OSM_SM_SIGNAL_HANDOVER",	/* 5 */
	"OSM_SM_SIGNAL_HANDOVER_SENT",	/* 6 */
	"OSM_SM_SIGNAL_ACKNOWLEDGE",	/* 7 */
	"OSM_SM_SIGNAL_STANDBY",	/* 8 */
	"OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED",	/* 9 */
	"OSM_SM_SIGNAL_WAIT_FOR_HANDOVER",	/* 10 */
	"UNKNOWN STATE!!"	/* 11 */
};

const char *osm_get_sm_mgr_signal_str(IN osm_sm_signal_t signal)
{
	if (signal > OSM_SM_SIGNAL_MAX)
		signal = OSM_SM_SIGNAL_MAX;
	return sm_mgr_signal_str[signal];
}

static const char *sm_mgr_state_str[] = {
	"NOTACTIVE",		/* 0 */
	"DISCOVERING",		/* 1 */
	"STANDBY",		/* 2 */
	"MASTER",		/* 3 */
	"UNKNOWN STATE!!"	/* 4 */
};

const char *osm_get_sm_mgr_state_str(IN uint16_t state)
{
	return state < ARR_SIZE(sm_mgr_state_str) ?
	    sm_mgr_state_str[state] :
	    sm_mgr_state_str[ARR_SIZE(sm_mgr_state_str) - 1];
}

int ib_mtu_is_valid(IN const int mtu)
{
	if (mtu < IB_MIN_MTU || mtu > IB_MAX_MTU)
		return 0;
	return 1;
}

int ib_rate_is_valid(IN const int rate)
{
	if (rate < IB_MIN_RATE || rate > IB_MAX_RATE)
		return 0;
	return 1;
}

int ib_path_compare_rates(IN const int rate1, IN const int rate2)
{
	int orate1 = 0, orate2 = 0;

	CL_ASSERT(rate1 >= IB_MIN_RATE && rate1 <= IB_MAX_RATE);
	CL_ASSERT(rate2 >= IB_MIN_RATE && rate2 <= IB_MAX_RATE);

	if (rate1 <= IB_MAX_RATE)
		orate1 = ordered_rates[rate1];
	if (rate2 <= IB_MAX_RATE)
		orate2 = ordered_rates[rate2];
	if (orate1 < orate2)
		return -1;
	if (orate1 == orate2)
		return 0;
	return 1;
}

static int find_ordered_rate(IN const int rate)
{
	int i;

	for (i = IB_MIN_RATE; i <= IB_MAX_RATE; i++) {
		if (ordered_rates[i] == rate)
			return i;
	}
	return 0;
}

int ib_path_rate_get_prev(IN const int rate)
{
	int orate;

	CL_ASSERT(rate >= IB_MIN_RATE && rate <= IB_MAX_RATE);

	if (rate <= IB_MIN_RATE)
		return 0;
	if (rate > IB_MAX_RATE)
		return 0;
	orate = ordered_rates[rate];
	orate--;
	return find_ordered_rate(orate);
}

int ib_path_rate_get_next(IN const int rate)
{
	int orate;

	CL_ASSERT(rate >= IB_MIN_RATE && rate <= IB_MAX_RATE);

	if (rate < IB_MIN_RATE)
		return 0;
	if (rate >= IB_MAX_RATE)
		return 0;
	orate = ordered_rates[rate];
	orate++;
	return find_ordered_rate(orate);
}
