/*
 * Copyright (c) 2006,2007 The Regents of the University of California.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2013 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2013 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 *
 * Produced at Lawrence Livermore National Laboratory.
 * Written by Ira Weiny <weiny2@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <iba/ib_types.h>
#include <complib/cl_nodenamemap.h>

#include "ibdiag_common.h"
#include "ibdiag_sa.h"

#ifndef IB_PR_COMPMASK_SERVICEID
#define IB_PR_COMPMASK_SERVICEID (IB_PR_COMPMASK_SERVICEID_MSB | \
				  IB_PR_COMPMASK_SERVICEID_LSB)
#endif

#define UMAD_SA_CAP_MASK2_IS_MCAST_TOP_SUP (1 << 3)

struct query_params {
	uint64_t service_id;
	ib_gid_t sgid, dgid, gid, mgid;
	uint16_t slid, dlid, mlid;
	uint32_t flow_label;
	int hop_limit;
	uint8_t tclass;
	int reversible, numb_path;
	uint16_t pkey;
	int qos_class, sl;
	uint8_t mtu, rate, pkt_life;
	uint32_t qkey;
	uint8_t scope;
	uint8_t join_state;
	int proxy_join;
	ib_class_port_info_t cpi;
	uint8_t with_grh;
	ibmad_gid_t sa_dgid;
};

struct query_cmd {
	const char *name, *alias;
	uint16_t query_type;
	const char *usage;
	int (*handler) (const struct query_cmd * q, struct sa_handle * h,
			struct query_params * p, int argc, char *argv[]);
};

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

/**
 * Declare some globals because I don't want this to be too complex.
 */
#define MAX_PORTS (8)
#define DEFAULT_SA_TIMEOUT_MS (1000)

enum {
	ALL,
	LID_ONLY,
	UNIQUE_LID_ONLY,
	GUID_ONLY,
	ALL_DESC,
	NAME_OF_LID,
	NAME_OF_GUID,
} node_print_desc = ALL;

char *requested_name = NULL;
uint16_t requested_lid = 0;
int requested_lid_flag = 0;
uint64_t requested_guid = 0;
int requested_guid_flag = 0;

static unsigned valid_gid(ib_gid_t * gid)
{
	ib_gid_t zero_gid;
	memset(&zero_gid, 0, sizeof zero_gid);
	return memcmp(&zero_gid, gid, sizeof(*gid));
}

static void print_node_desc(ib_node_record_t * node_record)
{
	ib_node_info_t *p_ni = &(node_record->node_info);
	ib_node_desc_t *p_nd = &(node_record->node_desc);
	char *name;

	if (p_ni->node_type == IB_NODE_TYPE_CA) {
		name = remap_node_name(node_name_map,
					node_record->node_info.node_guid,
					(char *)p_nd->description);
		printf("%6d  \"%s\"\n", cl_ntoh16(node_record->lid), name);
		free(name);
	}
}

static void dump_node_record(void *data, struct query_params *p)
{
	ib_node_record_t *nr = data;
	ib_node_info_t *ni = &nr->node_info;
	char *name = remap_node_name(node_name_map,
				       cl_ntoh64(ni->node_guid),
				       (char *)nr->node_desc.description);

	printf("NodeRecord dump:\n"
	       "\t\tlid.....................%u\n"
	       "\t\treserved................0x%X\n"
	       "\t\tbase_version............0x%X\n"
	       "\t\tclass_version...........0x%X\n"
	       "\t\tnode_type...............%s\n"
	       "\t\tnum_ports...............%u\n"
	       "\t\tsys_guid................0x%016" PRIx64 "\n"
	       "\t\tnode_guid...............0x%016" PRIx64 "\n"
	       "\t\tport_guid...............0x%016" PRIx64 "\n"
	       "\t\tpartition_cap...........0x%X\n"
	       "\t\tdevice_id...............0x%X\n"
	       "\t\trevision................0x%X\n"
	       "\t\tport_num................%u\n"
	       "\t\tvendor_id...............0x%X\n"
	       "\t\tNodeDescription.........%s\n",
	       cl_ntoh16(nr->lid), cl_ntoh16(nr->resv),
	       ni->base_version, ni->class_version,
	       ib_get_node_type_str(ni->node_type), ni->num_ports,
	       cl_ntoh64(ni->sys_guid), cl_ntoh64(ni->node_guid),
	       cl_ntoh64(ni->port_guid), cl_ntoh16(ni->partition_cap),
	       cl_ntoh16(ni->device_id), cl_ntoh32(ni->revision),
	       ib_node_info_get_local_port_num(ni),
	       cl_ntoh32(ib_node_info_get_vendor_id(ni)),
	       name);

	free(name);
}

static void print_node_record(ib_node_record_t * node_record)
{
	ib_node_info_t *p_ni = &node_record->node_info;
	ib_node_desc_t *p_nd = &node_record->node_desc;
	char *name;

	switch (node_print_desc) {
	case LID_ONLY:
	case UNIQUE_LID_ONLY:
		printf("%u\n", cl_ntoh16(node_record->lid));
		return;
	case GUID_ONLY:
		printf("0x%016" PRIx64 "\n", cl_ntoh64(p_ni->port_guid));
		return;
	case NAME_OF_LID:
	case NAME_OF_GUID:
		name = remap_node_name(node_name_map,
				       cl_ntoh64(p_ni->node_guid),
				       (char *)p_nd->description);
		printf("%s\n", name);
		free(name);
		return;
	case ALL:
	default:
		break;
	}

	dump_node_record(node_record, 0);
}

static void dump_path_record(void *data, struct query_params *p)
{
	char gid_str[INET6_ADDRSTRLEN];
	char gid_str2[INET6_ADDRSTRLEN];
	ib_path_rec_t *p_pr = data;
	printf("PathRecord dump:\n"
	       "\t\tservice_id..............0x%016" PRIx64 "\n"
	       "\t\tdgid....................%s\n"
	       "\t\tsgid....................%s\n"
	       "\t\tdlid....................%u\n"
	       "\t\tslid....................%u\n"
	       "\t\thop_flow_raw............0x%X\n"
	       "\t\ttclass..................0x%X\n"
	       "\t\tnum_path_revers.........0x%X\n"
	       "\t\tpkey....................0x%X\n"
	       "\t\tqos_class...............0x%X\n"
	       "\t\tsl......................0x%X\n"
	       "\t\tmtu.....................0x%X\n"
	       "\t\trate....................0x%X\n"
	       "\t\tpkt_life................0x%X\n"
	       "\t\tpreference..............0x%X\n"
	       "\t\tresv2...................0x%02X%02X%02X%02X%02X%02X\n",
	       cl_ntoh64(p_pr->service_id),
	       inet_ntop(AF_INET6, p_pr->dgid.raw, gid_str, sizeof gid_str),
	       inet_ntop(AF_INET6, p_pr->sgid.raw, gid_str2, sizeof gid_str2),
	       cl_ntoh16(p_pr->dlid), cl_ntoh16(p_pr->slid),
	       cl_ntoh32(p_pr->hop_flow_raw), p_pr->tclass, p_pr->num_path,
	       cl_ntoh16(p_pr->pkey), ib_path_rec_qos_class(p_pr),
	       ib_path_rec_sl(p_pr), p_pr->mtu, p_pr->rate, p_pr->pkt_life,
	       p_pr->preference,
	       p_pr->resv2[0], p_pr->resv2[1], p_pr->resv2[2],
	       p_pr->resv2[3], p_pr->resv2[4], p_pr->resv2[5]);
}

static void dump_class_port_info(ib_class_port_info_t *cpi)
{
	char gid_str[INET6_ADDRSTRLEN];
	char gid_str2[INET6_ADDRSTRLEN];

	printf("SA ClassPortInfo:\n"
	       "\t\tBase version.............%d\n"
	       "\t\tClass version............%d\n"
	       "\t\tCapability mask..........0x%04X\n"
	       "\t\tCapability mask 2........0x%08X\n"
	       "\t\tResponse time value......0x%02X\n"
	       "\t\tRedirect GID.............%s\n"
	       "\t\tRedirect TC/SL/FL........0x%08X\n"
	       "\t\tRedirect LID.............%u\n"
	       "\t\tRedirect PKey............0x%04X\n"
	       "\t\tRedirect QP..............0x%08X\n"
	       "\t\tRedirect QKey............0x%08X\n"
	       "\t\tTrap GID.................%s\n"
	       "\t\tTrap TC/SL/FL............0x%08X\n"
	       "\t\tTrap LID.................%u\n"
	       "\t\tTrap PKey................0x%04X\n"
	       "\t\tTrap HL/QP...............0x%08X\n"
	       "\t\tTrap QKey................0x%08X\n",
	       cpi->base_ver, cpi->class_ver, cl_ntoh16(cpi->cap_mask),
	       ib_class_cap_mask2(cpi), ib_class_resp_time_val(cpi),
	       inet_ntop(AF_INET6, &(cpi->redir_gid), gid_str, sizeof gid_str),
	       cl_ntoh32(cpi->redir_tc_sl_fl), cl_ntoh16(cpi->redir_lid),
	       cl_ntoh16(cpi->redir_pkey), cl_ntoh32(cpi->redir_qp),
	       cl_ntoh32(cpi->redir_qkey),
	       inet_ntop(AF_INET6, &(cpi->trap_gid), gid_str2, sizeof gid_str2),
	       cl_ntoh32(cpi->trap_tc_sl_fl), cl_ntoh16(cpi->trap_lid),
	       cl_ntoh16(cpi->trap_pkey), cl_ntoh32(cpi->trap_hop_qp),
	       cl_ntoh32(cpi->trap_qkey));
}

static void dump_portinfo_record(void *data, struct query_params *p)
{
	ib_portinfo_record_t *p_pir = data;
	const ib_port_info_t *const p_pi = &p_pir->port_info;

	printf("PortInfoRecord dump:\n"
	       "\t\tEndPortLid..............%u\n"
	       "\t\tPortNum.................%u\n"
	       "\t\tbase_lid................%u\n"
	       "\t\tmaster_sm_base_lid......%u\n"
	       "\t\tcapability_mask.........0x%X\n",
	       cl_ntoh16(p_pir->lid), p_pir->port_num,
	       cl_ntoh16(p_pi->base_lid), cl_ntoh16(p_pi->master_sm_base_lid),
	       cl_ntoh32(p_pi->capability_mask));
}

static void dump_one_portinfo_record(void *data, struct query_params *p)
{
	ib_portinfo_record_t *pir = data;
	ib_port_info_t *pi = &pir->port_info;

	printf("PortInfoRecord dump:\n"
	       "\tRID\n"
	       "\t\tEndPortLid..............%u\n"
	       "\t\tPortNum.................%u\n"
	       "\t\tOptions.................0x%x\n"
	       "\tPortInfo dump:\n",
	       cl_ntoh16(pir->lid), pir->port_num, pir->options);
	dump_portinfo(pi, 2);
}

static void dump_one_mcmember_record(void *data, struct query_params *p)
{
	char mgid[INET6_ADDRSTRLEN], gid[INET6_ADDRSTRLEN];
	ib_member_rec_t *mr = data;
	uint32_t flow;
	uint8_t sl, hop, scope, join;
	ib_member_get_sl_flow_hop(mr->sl_flow_hop, &sl, &flow, &hop);
	ib_member_get_scope_state(mr->scope_state, &scope, &join);
	printf("MCMember Record dump:\n"
	       "\t\tMGID....................%s\n"
	       "\t\tPortGid.................%s\n"
	       "\t\tqkey....................0x%x\n"
	       "\t\tmlid....................0x%x\n"
	       "\t\tmtu.....................0x%x\n"
	       "\t\tTClass..................0x%x\n"
	       "\t\tpkey....................0x%x\n"
	       "\t\trate....................0x%x\n"
	       "\t\tpkt_life................0x%x\n"
	       "\t\tSL......................0x%x\n"
	       "\t\tFlowLabel...............0x%x\n"
	       "\t\tHopLimit................0x%x\n"
	       "\t\tScope...................0x%x\n"
	       "\t\tJoinState...............0x%x\n"
	       "\t\tProxyJoin...............0x%x\n",
	       inet_ntop(AF_INET6, mr->mgid.raw, mgid, sizeof(mgid)),
	       inet_ntop(AF_INET6, mr->port_gid.raw, gid, sizeof(gid)),
	       cl_ntoh32(mr->qkey), cl_ntoh16(mr->mlid), mr->mtu, mr->tclass,
	       cl_ntoh16(mr->pkey), mr->rate, mr->pkt_life, sl,
	       flow, hop, scope, join, mr->proxy_join);
}

static void dump_multicast_group_record(void *data, struct query_params *p)
{
	char gid_str[INET6_ADDRSTRLEN];
	ib_member_rec_t *p_mcmr = data;
	uint8_t sl;
	ib_member_get_sl_flow_hop(p_mcmr->sl_flow_hop, &sl, NULL, NULL);
	printf("MCMemberRecord group dump:\n"
	       "\t\tMGID....................%s\n"
	       "\t\tMlid....................0x%X\n"
	       "\t\tMtu.....................0x%X\n"
	       "\t\tpkey....................0x%X\n"
	       "\t\tRate....................0x%X\n"
	       "\t\tSL......................0x%X\n",
	       inet_ntop(AF_INET6, p_mcmr->mgid.raw, gid_str, sizeof gid_str),
	       cl_ntoh16(p_mcmr->mlid),
	       p_mcmr->mtu, cl_ntoh16(p_mcmr->pkey), p_mcmr->rate, sl);
}

static void dump_multicast_member_record(ib_member_rec_t *p_mcmr,
					 struct sa_query_result *nr_result,
					 struct query_params *params)
{
	char gid_str[INET6_ADDRSTRLEN];
	char gid_str2[INET6_ADDRSTRLEN];
	uint16_t mlid = cl_ntoh16(p_mcmr->mlid);
	unsigned i = 0;
	char *node_name = strdup("<unknown>");

	/* go through the node records searching for a port guid which matches
	 * this port gid interface id.
	 * This gives us a node name to print, if available.
	 */
	for (i = 0; i < nr_result->result_cnt; i++) {
		ib_node_record_t *nr = sa_get_query_rec(nr_result->p_result_madw, i);
		if (nr->node_info.port_guid ==
		    p_mcmr->port_gid.unicast.interface_id) {
			if(node_name != NULL)
				free(node_name);
			node_name = remap_node_name(node_name_map,
						nr->node_info.node_guid,
						(char *)nr->node_desc.description);
			break;
		}
	}

	if (requested_name) {
		if (strtol(requested_name, NULL, 0) == mlid)
			printf("\t\tPortGid.................%s (%s)\n",
			       inet_ntop(AF_INET6, p_mcmr->port_gid.raw,
					 gid_str, sizeof gid_str), node_name);
	} else {
		printf("MCMemberRecord member dump:\n"
		       "\t\tMGID....................%s\n"
		       "\t\tMlid....................0x%X\n"
		       "\t\tPortGid.................%s\n"
		       "\t\tScopeState..............0x%X\n"
		       "\t\tProxyJoin...............0x%X\n"
		       "\t\tNodeDescription.........%s\n",
		       inet_ntop(AF_INET6, p_mcmr->mgid.raw, gid_str,
				 sizeof gid_str),
		       cl_ntoh16(p_mcmr->mlid),
		       inet_ntop(AF_INET6, p_mcmr->port_gid.raw,
				 gid_str2, sizeof gid_str2),
		       p_mcmr->scope_state, p_mcmr->proxy_join, node_name);
	}
	free(node_name);
}

static void dump_service_record(void *data, struct query_params *p)
{
	char gid[INET6_ADDRSTRLEN];
	char buf_service_key[35];
	char buf_service_name[65];
	ib_service_record_t *p_sr = data;

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

	printf("ServiceRecord dump:\n"
	       "\t\tServiceID...............0x%016" PRIx64 "\n"
	       "\t\tServiceGID..............%s\n"
	       "\t\tServiceP_Key............0x%X\n"
	       "\t\tServiceLease............0x%X\n"
	       "\t\tServiceKey..............%s\n"
	       "\t\tServiceName.............%s\n"
	       "\t\tServiceData8.1..........0x%X\n"
	       "\t\tServiceData8.2..........0x%X\n"
	       "\t\tServiceData8.3..........0x%X\n"
	       "\t\tServiceData8.4..........0x%X\n"
	       "\t\tServiceData8.5..........0x%X\n"
	       "\t\tServiceData8.6..........0x%X\n"
	       "\t\tServiceData8.7..........0x%X\n"
	       "\t\tServiceData8.8..........0x%X\n"
	       "\t\tServiceData8.9..........0x%X\n"
	       "\t\tServiceData8.10.........0x%X\n"
	       "\t\tServiceData8.11.........0x%X\n"
	       "\t\tServiceData8.12.........0x%X\n"
	       "\t\tServiceData8.13.........0x%X\n"
	       "\t\tServiceData8.14.........0x%X\n"
	       "\t\tServiceData8.15.........0x%X\n"
	       "\t\tServiceData8.16.........0x%X\n"
	       "\t\tServiceData16.1.........0x%X\n"
	       "\t\tServiceData16.2.........0x%X\n"
	       "\t\tServiceData16.3.........0x%X\n"
	       "\t\tServiceData16.4.........0x%X\n"
	       "\t\tServiceData16.5.........0x%X\n"
	       "\t\tServiceData16.6.........0x%X\n"
	       "\t\tServiceData16.7.........0x%X\n"
	       "\t\tServiceData16.8.........0x%X\n"
	       "\t\tServiceData32.1.........0x%X\n"
	       "\t\tServiceData32.2.........0x%X\n"
	       "\t\tServiceData32.3.........0x%X\n"
	       "\t\tServiceData32.4.........0x%X\n"
	       "\t\tServiceData64.1.........0x%016" PRIx64 "\n"
	       "\t\tServiceData64.2.........0x%016" PRIx64 "\n",
	       cl_ntoh64(p_sr->service_id),
	       inet_ntop(AF_INET6, p_sr->service_gid.raw, gid, sizeof gid),
	       cl_ntoh16(p_sr->service_pkey), cl_ntoh32(p_sr->service_lease),
	       (show_keys ? buf_service_key : NOT_DISPLAYED_STR),
               buf_service_name,
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

static void dump_sm_info_record(void *data, struct query_params *p)
{
	ib_sminfo_record_t *p_smr = data;
	const ib_sm_info_t *const p_smi = &p_smr->sm_info;
	uint8_t priority, state;
	priority = ib_sminfo_get_priority(p_smi);
	state = ib_sminfo_get_state(p_smi);

	printf("SMInfoRecord dump:\n"
	       "\t\tRID\n"
	       "\t\tLID...................%u\n"
	       "\t\tSMInfo dump:\n"
	       "\t\tGUID..................0x%016" PRIx64 "\n"
	       "\t\tSM_Key................0x%016" PRIx64 "\n"
	       "\t\tActCount..............%u\n"
	       "\t\tPriority..............%u\n"
	       "\t\tSMState...............%u\n",
	       cl_ntoh16(p_smr->lid),
	       cl_ntoh64(p_smr->sm_info.guid),
	       cl_ntoh64(p_smr->sm_info.sm_key),
	       cl_ntoh32(p_smr->sm_info.act_count),
	       priority, state);
}

static void dump_switch_info_record(void *data, struct query_params *p)
{
	ib_switch_info_record_t *p_sir = data;
	uint32_t sa_cap_mask2 = ib_class_cap_mask2(&p->cpi);

	printf("SwitchInfoRecord dump:\n"
		"\t\tRID\n"
		"\t\tLID.....................................%u\n"
		"\t\tSwitchInfo dump:\n"
		"\t\tLinearFDBCap............................0x%X\n"
		"\t\tRandomFDBCap............................0x%X\n"
		"\t\tMulticastFDBCap.........................0x%X\n"
		"\t\tLinearFDBTop............................0x%X\n"
		"\t\tDefaultPort.............................%u\n"
		"\t\tDefaultMulticastPrimaryPort.............%u\n"
		"\t\tDefaultMulticastNotPrimaryPort..........%u\n"
		"\t\tLifeTimeValue/PortStateChange/OpSL2VL...0x%X\n"
		"\t\tLIDsPerPort.............................0x%X\n"
		"\t\tPartitionEnforcementCap.................0x%X\n"
		"\t\tflags...................................0x%X\n",
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
	if (sa_cap_mask2 & UMAD_SA_CAP_MASK2_IS_MCAST_TOP_SUP)
		printf("\t\tMulticastFDBTop.........................0x%X\n",
		       cl_ntoh16(p_sir->switch_info.mcast_top));
}

static void dump_inform_info_record(void *data, struct query_params *p)
{
	char gid_str[INET6_ADDRSTRLEN];
	char gid_str2[INET6_ADDRSTRLEN];
	ib_inform_info_record_t *p_iir = data;
	uint32_t qpn;
	uint8_t resp_time_val;

	ib_inform_info_get_qpn_resp_time(p_iir->inform_info.g_or_v.
					 generic.qpn_resp_time_val, &qpn,
					 &resp_time_val);
	if (p_iir->inform_info.is_generic) {
		printf("InformInfoRecord dump:\n"
		       "\t\tRID\n"
		       "\t\tSubscriberGID...........%s\n"
		       "\t\tSubscriberEnum..........0x%X\n"
		       "\t\tInformInfo dump:\n"
		       "\t\tgid.....................%s\n"
		       "\t\tlid_range_begin.........%u\n"
		       "\t\tlid_range_end...........%u\n"
		       "\t\tis_generic..............0x%X\n"
		       "\t\tsubscribe...............0x%X\n"
		       "\t\ttrap_type...............0x%X\n"
		       "\t\ttrap_num................%u\n",
		       inet_ntop(AF_INET6, p_iir->subscriber_gid.raw, gid_str,
				 sizeof gid_str),
		       cl_ntoh16(p_iir->subscriber_enum),
		       inet_ntop(AF_INET6, p_iir->inform_info.gid.raw, gid_str2,
				 sizeof gid_str2),
		       cl_ntoh16(p_iir->inform_info.lid_range_begin),
		       cl_ntoh16(p_iir->inform_info.lid_range_end),
		       p_iir->inform_info.is_generic,
		       p_iir->inform_info.subscribe,
		       cl_ntoh16(p_iir->inform_info.trap_type),
		       cl_ntoh16(p_iir->inform_info.g_or_v.generic.trap_num));
		if (show_keys) {
			printf("\t\tqpn.....................0x%06X\n",
			       cl_ntoh32(qpn));
		} else {
			printf("\t\tqpn....................."
			       NOT_DISPLAYED_STR "\n");
		}
		printf("\t\tresp_time_val...........0x%X\n"
		       "\t\tnode_type...............0x%06X\n",
		       resp_time_val,
		       cl_ntoh32(ib_inform_info_get_prod_type
				 (&p_iir->inform_info)));
	} else {
		printf("InformInfoRecord dump:\n"
		       "\t\tRID\n"
		       "\t\tSubscriberGID...........%s\n"
		       "\t\tSubscriberEnum..........0x%X\n"
		       "\t\tInformInfo dump:\n"
		       "\t\tgid.....................%s\n"
		       "\t\tlid_range_begin.........%u\n"
		       "\t\tlid_range_end...........%u\n"
		       "\t\tis_generic..............0x%X\n"
		       "\t\tsubscribe...............0x%X\n"
		       "\t\ttrap_type...............0x%X\n"
		       "\t\tdev_id..................0x%X\n",
		       inet_ntop(AF_INET6, p_iir->subscriber_gid.raw, gid_str,
				 sizeof gid_str),
		       cl_ntoh16(p_iir->subscriber_enum),
		       inet_ntop(AF_INET6, p_iir->inform_info.gid.raw,
				 gid_str2, sizeof gid_str2),
		       cl_ntoh16(p_iir->inform_info.lid_range_begin),
		       cl_ntoh16(p_iir->inform_info.lid_range_end),
		       p_iir->inform_info.is_generic,
		       p_iir->inform_info.subscribe,
		       cl_ntoh16(p_iir->inform_info.trap_type),
		       cl_ntoh16(p_iir->inform_info.g_or_v.vend.dev_id));
		if (show_keys) {
			printf("\t\tqpn.....................0x%06X\n",
			       cl_ntoh32(qpn));
		} else {
			printf("\t\tqpn....................."
			       NOT_DISPLAYED_STR "\n");
		}
		printf("\t\tresp_time_val...........0x%X\n"
		       "\t\tvendor_id...............0x%06X\n",
		       resp_time_val,
		       cl_ntoh32(ib_inform_info_get_prod_type
				 (&p_iir->inform_info)));
	}
}

static void dump_one_link_record(void *data, struct query_params *p)
{
	ib_link_record_t *lr = data;
	printf("LinkRecord dump:\n"
	       "\t\tFromLID....................%u\n"
	       "\t\tFromPort...................%u\n"
	       "\t\tToPort.....................%u\n"
	       "\t\tToLID......................%u\n",
	       cl_ntoh16(lr->from_lid), lr->from_port_num,
	       lr->to_port_num, cl_ntoh16(lr->to_lid));
}

static void dump_one_slvl_record(void *data, struct query_params *p)
{
	ib_slvl_table_record_t *slvl = data;
	ib_slvl_table_t *t = &slvl->slvl_tbl;
	printf("SL2VLTableRecord dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tInPort.....................%u\n"
	       "\t\tOutPort....................%u\n"
	       "\t\tSL: 0| 1| 2| 3| 4| 5| 6| 7| 8| 9|10|11|12|13|14|15|\n"
	       "\t\tVL:%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u"
	       "|%2u|%2u|%2u|\n",
	       cl_ntoh16(slvl->lid), slvl->in_port_num, slvl->out_port_num,
	       ib_slvl_table_get(t, 0), ib_slvl_table_get(t, 1),
	       ib_slvl_table_get(t, 2), ib_slvl_table_get(t, 3),
	       ib_slvl_table_get(t, 4), ib_slvl_table_get(t, 5),
	       ib_slvl_table_get(t, 6), ib_slvl_table_get(t, 7),
	       ib_slvl_table_get(t, 8), ib_slvl_table_get(t, 9),
	       ib_slvl_table_get(t, 10), ib_slvl_table_get(t, 11),
	       ib_slvl_table_get(t, 12), ib_slvl_table_get(t, 13),
	       ib_slvl_table_get(t, 14), ib_slvl_table_get(t, 15));
}

static void dump_one_vlarb_record(void *data, struct query_params *p)
{
	ib_vl_arb_table_record_t *vlarb = data;
	ib_vl_arb_element_t *e = vlarb->vl_arb_tbl.vl_entry;
	int i;
	printf("VLArbTableRecord dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tPort.......................%u\n"
	       "\t\tBlock......................%u\n",
	       cl_ntoh16(vlarb->lid), vlarb->port_num, vlarb->block_num);
	for (i = 0; i < 32; i += 16)
		printf("\t\tVL    :%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|"
		       "%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|\n"
		       "\t\tWeight:%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|"
		       "%2u|%2u|%2u|%2u|%2u|%2u|%2u|%2u|\n",
		       e[i + 0].vl, e[i + 1].vl, e[i + 2].vl, e[i + 3].vl,
		       e[i + 4].vl, e[i + 5].vl, e[i + 6].vl, e[i + 7].vl,
		       e[i + 8].vl, e[i + 9].vl, e[i + 10].vl, e[i + 11].vl,
		       e[i + 12].vl, e[i + 13].vl, e[i + 14].vl, e[i + 15].vl,
		       e[i + 0].weight, e[i + 1].weight, e[i + 2].weight,
		       e[i + 3].weight, e[i + 4].weight, e[i + 5].weight,
		       e[i + 6].weight, e[i + 7].weight, e[i + 8].weight,
		       e[i + 9].weight, e[i + 10].weight, e[i + 11].weight,
		       e[i + 12].weight, e[i + 13].weight, e[i + 14].weight,
		       e[i + 15].weight);
}

static void dump_one_pkey_tbl_record(void *data, struct query_params *params)
{
	ib_pkey_table_record_t *pktr = data;
	ib_net16_t *p = pktr->pkey_tbl.pkey_entry;
	int i;
	printf("PKeyTableRecord dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tPort.......................%u\n"
	       "\t\tBlock......................%u\n"
	       "\t\tPKey Table:\n",
	       cl_ntoh16(pktr->lid), pktr->port_num, pktr->block_num);
	for (i = 0; i < 32; i += 8)
		printf("\t\t0x%04x 0x%04x 0x%04x 0x%04x"
		       " 0x%04x 0x%04x 0x%04x 0x%04x\n",
		       cl_ntoh16(p[i + 0]), cl_ntoh16(p[i + 1]),
		       cl_ntoh16(p[i + 2]), cl_ntoh16(p[i + 3]),
		       cl_ntoh16(p[i + 4]), cl_ntoh16(p[i + 5]),
		       cl_ntoh16(p[i + 6]), cl_ntoh16(p[i + 7]));
	printf("\n");
}

static void dump_one_lft_record(void *data, struct query_params *p)
{
	ib_lft_record_t *lftr = data;
	unsigned block = cl_ntoh16(lftr->block_num);
	int i;
	printf("LFT Record dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tBlock......................%u\n"
	       "\t\tLFT:\n\t\tLID\tPort Number\n", cl_ntoh16(lftr->lid), block);
	for (i = 0; i < 64; i++)
		printf("\t\t%u\t%u\n", block * 64 + i, lftr->lft[i]);
	printf("\n");
}

static void dump_one_guidinfo_record(void *data, struct query_params *p)
{
	ib_guidinfo_record_t *gir = data;
	printf("GUIDInfo Record dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tBlock......................%u\n"
	       "\t\tGUID 0.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 1.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 2.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 3.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 4.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 5.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 6.....................0x%016" PRIx64 "\n"
	       "\t\tGUID 7.....................0x%016" PRIx64 "\n",
	       cl_ntoh16(gir->lid), gir->block_num,
	       cl_ntoh64(gir->guid_info.guid[0]),
	       cl_ntoh64(gir->guid_info.guid[1]),
	       cl_ntoh64(gir->guid_info.guid[2]),
	       cl_ntoh64(gir->guid_info.guid[3]),
	       cl_ntoh64(gir->guid_info.guid[4]),
	       cl_ntoh64(gir->guid_info.guid[5]),
	       cl_ntoh64(gir->guid_info.guid[6]),
	       cl_ntoh64(gir->guid_info.guid[7]));
}

static void dump_one_mft_record(void *data, struct query_params *p)
{
	ib_mft_record_t *mftr = data;
	unsigned position = cl_ntoh16(mftr->position_block_num) >> 12;
	unsigned block = cl_ntoh16(mftr->position_block_num) &
	    IB_MCAST_BLOCK_ID_MASK_HO;
	int i;
	unsigned offset;

	printf("MFT Record dump:\n"
	       "\t\tLID........................%u\n"
	       "\t\tPosition...................%u\n"
	       "\t\tBlock......................%u\n"
	       "\t\tMFT:\n\t\tMLID\tPort Mask\n",
	       cl_ntoh16(mftr->lid), position, block);
	offset = IB_LID_MCAST_START_HO + block * 32;
	for (i = 0; i < IB_MCAST_BLOCK_SIZE; i++)
		printf("\t\t0x%04x\t0x%04x\n",
		       offset + i, cl_ntoh16(mftr->mft[i]));
	printf("\n");
}

static void dump_results(struct sa_query_result *r,
			 void (*dump_func) (void *, struct query_params *),
			 struct query_params *p)
{
	unsigned i;
	for (i = 0; i < r->result_cnt; i++) {
		void *data = sa_get_query_rec(r->p_result_madw, i);
		dump_func(data, p);
	}
}

/**
 * Get any record(s)
 */
static int get_any_records(struct sa_handle * h,
			   uint16_t attr_id, uint32_t attr_mod,
			   ib_net64_t comp_mask, void *attr,
			   size_t attr_size,
			   struct sa_query_result *result)
{
	int ret = sa_query(h, IB_MAD_METHOD_GET_TABLE, attr_id, attr_mod,
			   cl_ntoh64(comp_mask), ibd_sakey, attr, attr_size, result);
	if (ret) {
		fprintf(stderr, "Query SA failed: %s\n", strerror(ret));
		return ret;
	}

	if (result->status != IB_SA_MAD_STATUS_SUCCESS) {
		sa_report_err(result->status);
		return EIO;
	}

	return ret;
}

static int get_and_dump_any_records(struct sa_handle * h, uint16_t attr_id,
				    uint32_t attr_mod, ib_net64_t comp_mask,
				    void *attr,
				    size_t attr_size,
				    void (*dump_func) (void *,
				    		       struct query_params *),
				    struct query_params *p)
{
	struct sa_query_result result;
	int ret = get_any_records(h, attr_id, attr_mod, comp_mask, attr,
				  attr_size, &result);
	if (ret)
		return ret;

	dump_results(&result, dump_func, p);
	sa_free_result_mad(&result);
	return 0;
}

/**
 * Get all the records available for requested query type.
 */
static int get_all_records(struct sa_handle * h, uint16_t attr_id,
			   struct sa_query_result *result)
{
	return get_any_records(h, attr_id, 0, 0, NULL, 0, result);
}

static int get_and_dump_all_records(struct sa_handle * h, uint16_t attr_id,
				    void (*dump_func) (void *,
						       struct query_params *p),
				    struct query_params *p)
{
	struct sa_query_result result;
	int ret = get_all_records(h, attr_id, &result);
	if (ret)
		return ret;

	dump_results(&result, dump_func, p);
	sa_free_result_mad(&result);
	return ret;
}

/**
 * return the lid from the node descriptor (name) supplied
 */
static int get_lid_from_name(struct sa_handle * h, const char *name, uint16_t * lid)
{
	ib_node_record_t *node_record = NULL;
	unsigned i;
	int ret;
	struct sa_query_result result;

	ret = get_all_records(h, IB_SA_ATTR_NODERECORD, &result);
	if (ret)
		return ret;

	ret = EHOSTDOWN;
	for (i = 0; i < result.result_cnt; i++) {
		node_record = sa_get_query_rec(result.p_result_madw, i);
		if (name
		    && strncmp(name, (char *)node_record->node_desc.description,
			       sizeof(node_record->node_desc.description)) ==
		    0) {
			*lid = cl_ntoh16(node_record->lid);
			ret = 0;
			break;
		}
	}
	sa_free_result_mad(&result);
	return ret;
}

static uint16_t get_lid(struct sa_handle * h, const char *name)
{
	int rc = 0;
	uint16_t rc_lid = 0;

	if (!name)
		return 0;
	if (isalpha(name[0])) {
		if ((rc = get_lid_from_name(h, name, &rc_lid)) != 0) {
			fprintf(stderr, "Failed to find lid for \"%s\": %s\n",
				name, strerror(rc));
			exit(rc);
		}
	} else {
		long val;
		errno = 0;
		val = strtol(name, NULL, 0);
		if (errno != 0 || val <= 0 || val > UINT16_MAX) {
			fprintf(stderr, "Invalid lid specified: \"%s\"\n", name);
			exit(EINVAL);
		}
		rc_lid = (uint16_t)val;
	}

	return rc_lid;
}

static int parse_iir_subscriber_gid(char *str, ib_inform_info_record_t *ir)
{
       int rc = inet_pton(AF_INET6,str,&(ir->subscriber_gid.raw));
       if(rc < 1){
          fprintf(stderr, "Invalid SubscriberGID specified: \"%s\"\n",str);
          exit(EINVAL);
       }
       return rc;
}

static int parse_lid_and_ports(struct sa_handle * h,
			       char *str, int *lid, int *port1, int *port2)
{
	char *p, *e;

	if (port1)
		*port1 = -1;
	if (port2)
		*port2 = -1;

	p = strchr(str, '/');
	if (p)
		*p = '\0';
	if (lid)
		*lid = get_lid(h, str);

	if (!p)
		return 0;
	str = p + 1;
	p = strchr(str, '/');
	if (p)
		*p = '\0';
	if (port1) {
		*port1 = strtoul(str, &e, 0);
		if (e == str)
			*port1 = -1;
	}

	if (!p)
		return 0;
	str = p + 1;
	if (port2) {
		*port2 = strtoul(str, &e, 0);
		if (e == str)
			*port2 = -1;
	}

	return 0;
}

/*
 * Get the portinfo records available with IsSM or IsSMdisabled CapabilityMask bit on.
 */
static int get_issm_records(struct sa_handle * h, ib_net32_t capability_mask,
			    struct sa_query_result *result)
{
	ib_portinfo_record_t attr;

	memset(&attr, 0, sizeof(attr));
	attr.port_info.capability_mask = capability_mask;

	return get_any_records(h, IB_SA_ATTR_PORTINFORECORD, 1 << 31,
			       IB_PIR_COMPMASK_CAPMASK, &attr, sizeof(attr), result);
}

static int print_node_records(struct sa_handle * h, struct query_params *p)
{
	unsigned i;
	int ret;
	struct sa_query_result result;

	ret = get_all_records(h, IB_SA_ATTR_NODERECORD, &result);
	if (ret)
		return ret;

	if (node_print_desc == ALL_DESC) {
		printf("   LID \"name\"\n");
		printf("================\n");
	}
	for (i = 0; i < result.result_cnt; i++) {
		ib_node_record_t *node_record;
		node_record = sa_get_query_rec(result.p_result_madw, i);
		if (node_print_desc == ALL_DESC) {
			print_node_desc(node_record);
		} else if (node_print_desc == NAME_OF_LID) {
			if (requested_lid == cl_ntoh16(node_record->lid))
				print_node_record(node_record);
		} else if (node_print_desc == NAME_OF_GUID) {
			ib_node_info_t *p_ni = &(node_record->node_info);

			if (requested_guid == cl_ntoh64(p_ni->port_guid))
				print_node_record(node_record);
		} else {
			ib_node_info_t *p_ni = &(node_record->node_info);
			ib_node_desc_t *p_nd = &(node_record->node_desc);
			char *name;

			name = remap_node_name (node_name_map,
						cl_ntoh64(p_ni->node_guid),
						(char *)p_nd->description);

			if (!requested_name ||
			    (strncmp(requested_name,
				     (char *)node_record->node_desc.description,
				     sizeof(node_record->
					    node_desc.description)) == 0) ||
			    (strncmp(requested_name,
				     name,
				     sizeof(node_record->
					    node_desc.description)) == 0)) {
				print_node_record(node_record);
				if (node_print_desc == UNIQUE_LID_ONLY) {
					sa_free_result_mad(&result);
					exit(0);
				}
			}

			free(name);
		}
	}
	sa_free_result_mad(&result);
	return ret;
}

static int sm_pr_query(struct sa_handle * h, ibmad_gid_t *gid, int srclid, int destlid) {

	ib_path_rec_t pr;
	ib_net64_t comp_mask = 0;
	struct sa_query_result result;
	int ret;
	ib_path_rec_t *p_pr;

	memset(&pr, 0, sizeof(pr));
	CHECK_AND_SET_VAL(srclid, 16, 0, pr.slid, PR, SLID);
	CHECK_AND_SET_VAL(destlid, 16, 0, pr.dlid, PR, DLID);

	ret = get_any_records(h, IB_SA_ATTR_PATHRECORD, 0, comp_mask, &pr, sizeof(pr), &result);
	if (ret)
		return ret;

	p_pr = sa_get_query_rec(result.p_result_madw, 0);
	memcpy(gid, &p_pr->dgid, 16);
	sa_free_result_mad(&result);
	return ret;
}

static int query_path_records(const struct query_cmd *q, struct sa_handle * h,
			      struct query_params *p, int argc, char *argv[])
{
	ib_path_rec_t pr;
	ib_net64_t comp_mask = 0;
	uint32_t flow = 0;
	uint16_t qos_class = 0;
	uint8_t reversible = 0;

	memset(&pr, 0, sizeof(pr));
	CHECK_AND_SET_VAL(p->service_id, 64, 0, pr.service_id, PR, SERVICEID);
	CHECK_AND_SET_GID(p->sgid, pr.sgid, PR, SGID);
	CHECK_AND_SET_GID(p->dgid, pr.dgid, PR, DGID);
	CHECK_AND_SET_VAL(p->slid, 16, 0, pr.slid, PR, SLID);
	CHECK_AND_SET_VAL(p->dlid, 16, 0, pr.dlid, PR, DLID);
	CHECK_AND_SET_VAL(p->hop_limit, 32, -1, pr.hop_flow_raw, PR, HOPLIMIT);
	CHECK_AND_SET_VAL((p->flow_label << 8), 32, 0, flow, PR, FLOWLABEL);
	pr.hop_flow_raw =
		(pr.hop_flow_raw & cl_hton32(~0x0FFFFF00)) | flow;
	CHECK_AND_SET_VAL(p->tclass, 8, 0, pr.tclass, PR, TCLASS);
	CHECK_AND_SET_VAL(p->reversible, 8, -1, reversible, PR, REVERSIBLE);
	CHECK_AND_SET_VAL(p->numb_path, 8, -1, pr.num_path, PR, NUMBPATH);
	pr.num_path |= reversible << 7;
	CHECK_AND_SET_VAL(p->pkey, 16, 0, pr.pkey, PR, PKEY);
	CHECK_AND_SET_VAL(p->sl, 16, -1, pr.qos_class_sl, PR, SL);
	CHECK_AND_SET_VAL((p->qos_class << 4), 16, -1, qos_class, PR, QOS_CLASS);
	pr.qos_class_sl = (pr.qos_class_sl & CL_HTON16(IB_PATH_REC_SL_MASK)) |
			  qos_class;
	CHECK_AND_SET_VAL_AND_SEL(p->mtu, pr.mtu, PR, MTU, SELEC);
	CHECK_AND_SET_VAL_AND_SEL(p->rate, pr.rate, PR, RATE, SELEC);
	CHECK_AND_SET_VAL_AND_SEL(p->pkt_life, pr.pkt_life, PR, PKTLIFETIME,
				  SELEC);

	return get_and_dump_any_records(h, IB_SA_ATTR_PATHRECORD, 0, comp_mask,
					&pr, sizeof(pr), dump_path_record, p);
}

static int print_issm_records(struct sa_handle * h, struct query_params *p)
{
	struct sa_query_result result;
	int ret = 0;

	/* First, get IsSM records */
	ret = get_issm_records(h, IB_PORT_CAP_IS_SM, &result);
	if (ret != 0)
		return (ret);

	printf("IsSM ports\n");
	dump_results(&result, dump_portinfo_record, p);
	sa_free_result_mad(&result);

	/* Now, get IsSMdisabled records */
	ret = get_issm_records(h, IB_PORT_CAP_SM_DISAB, &result);
	if (ret != 0)
		return (ret);

	printf("\nIsSMdisabled ports\n");
	dump_results(&result, dump_portinfo_record, p);
	sa_free_result_mad(&result);

	return (ret);
}

static int print_multicast_member_records(struct sa_handle * h,
					struct query_params *params)
{
	struct sa_query_result mc_group_result;
	struct sa_query_result nr_result;
	int ret;
	unsigned i;

	ret = get_all_records(h, IB_SA_ATTR_MCRECORD, &mc_group_result);
	if (ret)
		return ret;

	ret = get_all_records(h, IB_SA_ATTR_NODERECORD, &nr_result);
	if (ret)
		goto return_mc;

	for (i = 0; i < mc_group_result.result_cnt; i++) {
		ib_member_rec_t *rec = (ib_member_rec_t *)
				sa_get_query_rec(mc_group_result.p_result_madw,
					      i);
		dump_multicast_member_record(rec, &nr_result, params);
	}

	sa_free_result_mad(&nr_result);

return_mc:
	sa_free_result_mad(&mc_group_result);

	return ret;
}

static int print_multicast_group_records(struct sa_handle * h,
					 struct query_params *p)
{
	return get_and_dump_all_records(h, IB_SA_ATTR_MCRECORD,
					dump_multicast_group_record, p);
}

static int query_class_port_info(const struct query_cmd *q, struct sa_handle * h,
				 struct query_params *p, int argc, char *argv[])
{
	dump_class_port_info(&p->cpi);
	return (0);
}

static int query_node_records(const struct query_cmd *q, struct sa_handle * h,
			      struct query_params *p, int argc, char *argv[])
{
	ib_node_record_t nr;
	ib_net64_t comp_mask = 0;
	int lid = 0;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, NULL, NULL);

	memset(&nr, 0, sizeof(nr));
	CHECK_AND_SET_VAL(lid, 16, 0, nr.lid, NR, LID);

	return get_and_dump_any_records(h, IB_SA_ATTR_NODERECORD, 0, comp_mask,
					&nr, sizeof(nr), dump_node_record, p);
}

static int query_portinfo_records(const struct query_cmd *q,
				  struct sa_handle * h, struct query_params *p,
				  int argc, char *argv[])
{
	ib_portinfo_record_t pir;
	ib_net64_t comp_mask = 0;
	int lid = 0, port = -1, options = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &port, &options);

	memset(&pir, 0, sizeof(pir));
	CHECK_AND_SET_VAL(lid, 16, 0, pir.lid, PIR, LID);
	CHECK_AND_SET_VAL(port, 8, -1, pir.port_num, PIR, PORTNUM);
	CHECK_AND_SET_VAL(options, 8, -1, pir.options, PIR, OPTIONS);

	return get_and_dump_any_records(h, IB_SA_ATTR_PORTINFORECORD, 0,
					comp_mask, &pir, sizeof(pir),
					dump_one_portinfo_record, p);
}

static int query_mcmember_records(const struct query_cmd *q,
				  struct sa_handle * h, struct query_params *p,
				  int argc, char *argv[])
{
	ib_member_rec_t mr;
	ib_net64_t comp_mask = 0;
	uint32_t flow = 0;
	uint8_t sl = 0, hop = 0, scope = 0;

	memset(&mr, 0, sizeof(mr));
	CHECK_AND_SET_GID(p->mgid, mr.mgid, MCR, MGID);
	CHECK_AND_SET_GID(p->gid, mr.port_gid, MCR, PORT_GID);
	CHECK_AND_SET_VAL(p->mlid, 16, 0, mr.mlid, MCR, MLID);
	CHECK_AND_SET_VAL(p->qkey, 32, 0, mr.qkey, MCR, QKEY);
	CHECK_AND_SET_VAL_AND_SEL(p->mtu, mr.mtu, MCR, MTU, _SEL);
	CHECK_AND_SET_VAL_AND_SEL(p->rate, mr.rate, MCR, RATE, _SEL);
	CHECK_AND_SET_VAL_AND_SEL(p->pkt_life, mr.pkt_life, MCR, LIFE, _SEL);
	CHECK_AND_SET_VAL(p->tclass, 8, 0, mr.tclass, MCR, TCLASS);
	CHECK_AND_SET_VAL(p->pkey, 16, 0, mr.pkey, MCR, PKEY);
	CHECK_AND_SET_VAL(p->sl, 8, -1, sl, MCR, SL);
	CHECK_AND_SET_VAL(p->flow_label, 32, 0, flow, MCR, FLOW);
	CHECK_AND_SET_VAL(p->hop_limit, 8, 0, hop, MCR, HOP);
	/* pass flow in host order as expected by function */
	mr.sl_flow_hop = ib_member_set_sl_flow_hop(sl, cl_ntoh32(flow), hop);
	CHECK_AND_SET_VAL(p->scope, 8, 0, scope, MCR, SCOPE);
	CHECK_AND_SET_VAL(p->join_state, 8, 0, mr.scope_state, MCR, JOIN_STATE);
	mr.scope_state |= scope << 4;
	CHECK_AND_SET_VAL(p->proxy_join, 8, -1, mr.proxy_join, MCR, PROXY);

	return get_and_dump_any_records(h, IB_SA_ATTR_MCRECORD, 0, comp_mask,
					&mr, sizeof(mr), dump_one_mcmember_record, p);
}

static int query_service_records(const struct query_cmd *q, struct sa_handle * h,
				 struct query_params *p, int argc, char *argv[])
{
	return get_and_dump_all_records(h, IB_SA_ATTR_SERVICERECORD,
					dump_service_record, p);
}

static int query_sm_info_records(const struct query_cmd *q,
				 struct sa_handle * h, struct query_params *p,
				 int argc, char *argv[])
{
	ib_sminfo_record_t smir;
	ib_net64_t comp_mask = 0;
	int lid = 0;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, NULL, NULL);

	memset(&smir, 0, sizeof(smir));
	CHECK_AND_SET_VAL(lid, 16, 0, smir.lid, SMIR, LID);

	return get_and_dump_any_records(h, IB_SA_ATTR_SMINFORECORD, 0,
					comp_mask, &smir, sizeof(smir),
					dump_sm_info_record, p);
}

static int query_switchinfo_records(const struct query_cmd *q,
				struct sa_handle * h, struct query_params *p,
				int argc, char *argv[])
{
	ib_switch_info_record_t swir;
	ib_net64_t comp_mask = 0;
	int lid = 0;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, NULL, NULL);

	memset(&swir, 0, sizeof(swir));
	CHECK_AND_SET_VAL(lid, 16, 0, swir.lid, SWIR, LID);

	return get_and_dump_any_records(h, IB_SA_ATTR_SWITCHINFORECORD, 0,
					comp_mask, &swir, sizeof(swir),
					dump_switch_info_record, p);
}

static int query_inform_info_records(const struct query_cmd *q,
				    struct sa_handle * h, struct query_params *p,
				    int argc, char *argv[])
{
       int rc = 0;
       ib_inform_info_record_t ir;
       ib_net64_t comp_mask = 0;
       memset(&ir, 0, sizeof(ir));

       if (argc > 0) {
           comp_mask = IB_IIR_COMPMASK_SUBSCRIBERGID;
           if((rc = parse_iir_subscriber_gid(argv[0], &ir)) < 1)
                 return rc;
       }

       return get_and_dump_any_records(h, IB_SA_ATTR_INFORMINFORECORD, 0, comp_mask,
				       &ir, sizeof(ir), dump_inform_info_record, p);

}

static int query_link_records(const struct query_cmd *q, struct sa_handle * h,
			      struct query_params *p, int argc, char *argv[])
{
	ib_link_record_t lr;
	ib_net64_t comp_mask = 0;
	int from_lid = 0, to_lid = 0, from_port = -1, to_port = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &from_lid, &from_port, NULL);

	if (argc > 1)
		parse_lid_and_ports(h, argv[1], &to_lid, &to_port, NULL);

	memset(&lr, 0, sizeof(lr));
	CHECK_AND_SET_VAL(from_lid, 16, 0, lr.from_lid, LR, FROM_LID);
	CHECK_AND_SET_VAL(from_port, 8, -1, lr.from_port_num, LR, FROM_PORT);
	CHECK_AND_SET_VAL(to_lid, 16, 0, lr.to_lid, LR, TO_LID);
	CHECK_AND_SET_VAL(to_port, 8, -1, lr.to_port_num, LR, TO_PORT);

	return get_and_dump_any_records(h, IB_SA_ATTR_LINKRECORD, 0, comp_mask,
					&lr, sizeof(lr), dump_one_link_record, p);
}

static int query_sl2vl_records(const struct query_cmd *q, struct sa_handle * h,
			       struct query_params *p, int argc, char *argv[])
{
	ib_slvl_table_record_t slvl;
	ib_net64_t comp_mask = 0;
	int lid = 0, in_port = -1, out_port = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &in_port, &out_port);

	memset(&slvl, 0, sizeof(slvl));
	CHECK_AND_SET_VAL(lid, 16, 0, slvl.lid, SLVL, LID);
	CHECK_AND_SET_VAL(in_port, 8, -1, slvl.in_port_num, SLVL, IN_PORT);
	CHECK_AND_SET_VAL(out_port, 8, -1, slvl.out_port_num, SLVL, OUT_PORT);

	return get_and_dump_any_records(h, IB_SA_ATTR_SL2VLTABLERECORD, 0,
					comp_mask, &slvl, sizeof(slvl),
					dump_one_slvl_record, p);
}

static int query_vlarb_records(const struct query_cmd *q, struct sa_handle * h,
			       struct query_params *p, int argc, char *argv[])
{
	ib_vl_arb_table_record_t vlarb;
	ib_net64_t comp_mask = 0;
	int lid = 0, port = -1, block = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &port, &block);

	memset(&vlarb, 0, sizeof(vlarb));
	CHECK_AND_SET_VAL(lid, 16, 0, vlarb.lid, VLA, LID);
	CHECK_AND_SET_VAL(port, 8, -1, vlarb.port_num, VLA, OUT_PORT);
	CHECK_AND_SET_VAL(block, 8, -1, vlarb.block_num, VLA, BLOCK);

	return get_and_dump_any_records(h, IB_SA_ATTR_VLARBTABLERECORD, 0,
					comp_mask, &vlarb, sizeof(vlarb),
					dump_one_vlarb_record, p);
}

static int query_pkey_tbl_records(const struct query_cmd *q,
				  struct sa_handle * h, struct query_params *p,
				  int argc, char *argv[])
{
	ib_pkey_table_record_t pktr;
	ib_net64_t comp_mask = 0;
	int lid = 0, port = -1, block = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &port, &block);

	memset(&pktr, 0, sizeof(pktr));
	CHECK_AND_SET_VAL(lid, 16, 0, pktr.lid, PKEY, LID);
	CHECK_AND_SET_VAL(port, 8, -1, pktr.port_num, PKEY, PORT);
	CHECK_AND_SET_VAL(block, 16, -1, pktr.block_num, PKEY, BLOCK);

	return get_and_dump_any_records(h, IB_SA_ATTR_PKEYTABLERECORD, 0,
					comp_mask, &pktr, sizeof(pktr),
					dump_one_pkey_tbl_record, p);
}

static int query_lft_records(const struct query_cmd *q, struct sa_handle * h,
			     struct query_params *p, int argc, char *argv[])
{
	ib_lft_record_t lftr;
	ib_net64_t comp_mask = 0;
	int lid = 0, block = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &block, NULL);

	memset(&lftr, 0, sizeof(lftr));
	CHECK_AND_SET_VAL(lid, 16, 0, lftr.lid, LFTR, LID);
	CHECK_AND_SET_VAL(block, 16, -1, lftr.block_num, LFTR, BLOCK);

	return get_and_dump_any_records(h, IB_SA_ATTR_LFTRECORD, 0, comp_mask,
					&lftr, sizeof(lftr), dump_one_lft_record, p);
}

static int query_guidinfo_records(const struct query_cmd *q, struct sa_handle * h,
				  struct query_params *p, int argc, char *argv[])
{
	ib_guidinfo_record_t gir;
	ib_net64_t comp_mask = 0;
	int lid = 0, block = -1;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &block, NULL);

	memset(&gir, 0, sizeof(gir));
	CHECK_AND_SET_VAL(lid, 16, 0, gir.lid, GIR, LID);
	CHECK_AND_SET_VAL(block, 8, -1, gir.block_num, GIR, BLOCKNUM);

	return get_and_dump_any_records(h, IB_SA_ATTR_GUIDINFORECORD, 0,
					comp_mask, &gir, sizeof(gir),
					dump_one_guidinfo_record, p);
}

static int query_mft_records(const struct query_cmd *q, struct sa_handle * h,
			     struct query_params *p, int argc, char *argv[])
{
	ib_mft_record_t mftr;
	ib_net64_t comp_mask = 0;
	int lid = 0, block = -1, position = -1;
	uint16_t pos = 0;

	if (argc > 0)
		parse_lid_and_ports(h, argv[0], &lid, &position, &block);

	memset(&mftr, 0, sizeof(mftr));
	CHECK_AND_SET_VAL(lid, 16, 0, mftr.lid, MFTR, LID);
	CHECK_AND_SET_VAL(block, 16, -1, mftr.position_block_num, MFTR, BLOCK);
	mftr.position_block_num &= cl_hton16(IB_MCAST_BLOCK_ID_MASK_HO);
	CHECK_AND_SET_VAL(position, 8, -1, pos, MFTR, POSITION);
	mftr.position_block_num |= cl_hton16(pos << 12);

	return get_and_dump_any_records(h, IB_SA_ATTR_MFTRECORD, 0, comp_mask,
					&mftr, sizeof(mftr), dump_one_mft_record, p);
}

static int query_sa_cpi(struct sa_handle *h, struct query_params *query_params)
{
	ib_class_port_info_t *cpi;
	struct sa_query_result result;
	int ret = sa_query(h, IB_MAD_METHOD_GET, CLASS_PORT_INFO, 0, 0,
		       ibd_sakey, NULL, 0, &result);
	if (ret) {
		fprintf(stderr, "Query SA failed: %s\n", strerror(ret));
		return ret;
	}

	if (result.status != IB_SA_MAD_STATUS_SUCCESS) {
		sa_report_err(result.status);
		ret = EIO;
		goto Exit;
	}
	cpi = sa_get_query_rec(result.p_result_madw, 0);
	memcpy(&query_params->cpi, cpi, sizeof(query_params->cpi));
Exit:
	sa_free_result_mad(&result);
	return (0);
}

static const struct query_cmd query_cmds[] = {
	{"ClassPortInfo", "CPI", CLASS_PORT_INFO,
	 NULL, query_class_port_info},
	{"NodeRecord", "NR", IB_SA_ATTR_NODERECORD,
	 "[lid]", query_node_records},
	{"PortInfoRecord", "PIR", IB_SA_ATTR_PORTINFORECORD,
	 "[[lid]/[port]/[options]]", query_portinfo_records},
	{"SL2VLTableRecord", "SL2VL", IB_SA_ATTR_SL2VLTABLERECORD,
	 "[[lid]/[in_port]/[out_port]]", query_sl2vl_records},
	{"PKeyTableRecord", "PKTR", IB_SA_ATTR_PKEYTABLERECORD,
	 "[[lid]/[port]/[block]]", query_pkey_tbl_records},
	{"VLArbitrationTableRecord", "VLAR", IB_SA_ATTR_VLARBTABLERECORD,
	 "[[lid]/[port]/[block]]", query_vlarb_records},
	{"InformInfoRecord", "IIR", IB_SA_ATTR_INFORMINFORECORD,
	 "[subscriber_gid]", query_inform_info_records},
	{"LinkRecord", "LR", IB_SA_ATTR_LINKRECORD,
	 "[[from_lid]/[from_port]] [[to_lid]/[to_port]]", query_link_records},
	{"ServiceRecord", "SR", IB_SA_ATTR_SERVICERECORD,
	 NULL, query_service_records},
	{"PathRecord", "PR", IB_SA_ATTR_PATHRECORD,
	 NULL, query_path_records},
	{"MCMemberRecord", "MCMR", IB_SA_ATTR_MCRECORD,
	 NULL, query_mcmember_records},
	{"LFTRecord", "LFTR", IB_SA_ATTR_LFTRECORD,
	 "[[lid]/[block]]", query_lft_records},
	{"MFTRecord", "MFTR", IB_SA_ATTR_MFTRECORD,
	 "[[mlid]/[position]/[block]]", query_mft_records},
	{"GUIDInfoRecord", "GIR", IB_SA_ATTR_GUIDINFORECORD,
	 "[[lid]/[block]]", query_guidinfo_records},
	{"SwitchInfoRecord", "SWIR", IB_SA_ATTR_SWITCHINFORECORD,
	 "[lid]", query_switchinfo_records},
	{"SMInfoRecord", "SMIR", IB_SA_ATTR_SMINFORECORD,
	 "[lid]", query_sm_info_records},
	{0}
};

static const struct query_cmd *find_query(const char *name)
{
	const struct query_cmd *q;

	for (q = query_cmds; q->name; q++)
		if (!strcasecmp(name, q->name) ||
		    (q->alias && !strcasecmp(name, q->alias)))
			return q;

	return NULL;
}

static const struct query_cmd *find_query_by_type(uint16_t type)
{
	const struct query_cmd *q;

	for (q = query_cmds; q->name; q++)
		if (q->query_type == type)
			return q;

	return NULL;
}

enum saquery_command {
	SAQUERY_CMD_QUERY,
	SAQUERY_CMD_NODE_RECORD,
	SAQUERY_CMD_CLASS_PORT_INFO,
	SAQUERY_CMD_ISSM,
	SAQUERY_CMD_MCGROUPS,
	SAQUERY_CMD_MCMEMBERS,
};

static enum saquery_command command = SAQUERY_CMD_QUERY;
static uint16_t query_type;
static char *src_lid, *dst_lid;

static int process_opt(void *context, int ch, char *optarg)
{
	struct query_params *p = context;

	switch (ch) {
	case 1:
		{
			src_lid = strdup(optarg);
			dst_lid = strchr(src_lid, ':');
			if (!dst_lid)
				ibdiag_show_usage();
			*dst_lid++ = '\0';
		}
		p->numb_path = 0x7f;
		query_type = IB_SA_ATTR_PATHRECORD;
		break;
	case 2:
		{
			char *src_addr = strdup(optarg);
			char *dst_addr = strchr(src_addr, '-');
			if (!dst_addr)
				ibdiag_show_usage();
			*dst_addr++ = '\0';
			if (inet_pton(AF_INET6, src_addr, &p->sgid) <= 0)
				ibdiag_show_usage();
			if (inet_pton(AF_INET6, dst_addr, &p->dgid) <= 0)
				ibdiag_show_usage();
			free(src_addr);
		}
		p->numb_path = 0x7f;
		query_type = IB_SA_ATTR_PATHRECORD;
		break;
	case 3:
		node_name_map_file = strdup(optarg);
		break;
	case 4:
		if (!isxdigit(*optarg) && !(optarg = getpass("SM_Key: "))) {
			fprintf(stderr, "cannot get SM_Key\n");
			ibdiag_show_usage();
		}
		ibd_sakey = strtoull(optarg, NULL, 0);
		break;
	case 'p':
		query_type = IB_SA_ATTR_PATHRECORD;
		break;
	case 'D':
		node_print_desc = ALL_DESC;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'c':
		command = SAQUERY_CMD_CLASS_PORT_INFO;
		break;
	case 'S':
		query_type = IB_SA_ATTR_SERVICERECORD;
		break;
	case 'I':
		query_type = IB_SA_ATTR_INFORMINFORECORD;
		break;
	case 'N':
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'L':
		node_print_desc = LID_ONLY;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'l':
		node_print_desc = UNIQUE_LID_ONLY;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'G':
		node_print_desc = GUID_ONLY;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'O':
		node_print_desc = NAME_OF_LID;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 'U':
		node_print_desc = NAME_OF_GUID;
		command = SAQUERY_CMD_NODE_RECORD;
		break;
	case 's':
		command = SAQUERY_CMD_ISSM;
		break;
	case 'g':
		command = SAQUERY_CMD_MCGROUPS;
		break;
	case 'm':
		command = SAQUERY_CMD_MCMEMBERS;
		break;
	case 'x':
		query_type = IB_SA_ATTR_LINKRECORD;
		break;
	case 5:
		p->slid = (uint16_t) strtoul(optarg, NULL, 0);
		break;
	case 6:
		p->dlid = (uint16_t) strtoul(optarg, NULL, 0);
		break;
	case 7:
		p->mlid = (uint16_t) strtoul(optarg, NULL, 0);
		break;
	case 14:
		if (inet_pton(AF_INET6, optarg, &p->sgid) <= 0)
			ibdiag_show_usage();
		break;
	case 15:
		if (inet_pton(AF_INET6, optarg, &p->dgid) <= 0)
			ibdiag_show_usage();
		break;
	case 16:
		if (inet_pton(AF_INET6, optarg, &p->gid) <= 0)
			ibdiag_show_usage();
		break;
	case 17:
		if (inet_pton(AF_INET6, optarg, &p->mgid) <= 0)
			ibdiag_show_usage();
		break;
	case 'r':
		p->reversible = strtoul(optarg, NULL, 0);
		break;
	case 'n':
		p->numb_path = strtoul(optarg, NULL, 0);
		break;
	case 18:
		if (!isxdigit(*optarg) && !(optarg = getpass("P_Key: "))) {
			fprintf(stderr, "cannot get P_Key\n");
			ibdiag_show_usage();
		}
		p->pkey = (uint16_t) strtoul(optarg, NULL, 0);
		break;
	case 'Q':
		p->qos_class = strtoul(optarg, NULL, 0);
		break;
	case 19:
		p->sl = strtoul(optarg, NULL, 0);
		break;
	case 'M':
		p->mtu = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 'R':
		p->rate = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 20:
		p->pkt_life = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 'q':
		if (!isxdigit(*optarg) && !(optarg = getpass("Q_Key: "))) {
			fprintf(stderr, "cannot get Q_Key\n");
			ibdiag_show_usage();
		}
		p->qkey = strtoul(optarg, NULL, 0);
		break;
	case 'T':
		p->tclass = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 'F':
		p->flow_label = strtoul(optarg, NULL, 0);
		break;
	case 'H':
		p->hop_limit = strtoul(optarg, NULL, 0);
		break;
	case 21:
		p->scope = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 'J':
		p->join_state = (uint8_t) strtoul(optarg, NULL, 0);
		break;
	case 'X':
		p->proxy_join = strtoul(optarg, NULL, 0);
		break;
	case 22:
		p->service_id = strtoull(optarg, NULL, 0);
		break;
	case 23:
		p->with_grh = TRUE;
		break;
	case 24:
		p->with_grh = TRUE;
		if (inet_pton(AF_INET6, optarg, &p->sa_dgid) <= 0)
			ibdiag_show_usage();
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	ib_portid_t portid = { 0 };
	int port = 0;
	int sa_cpi_required = 0;
	char usage_args[1024];
	struct sa_handle * h;
	struct query_params params;
	const struct query_cmd *q;
	int status;
	int n;

	const struct ibdiag_opt opts[] = {
		{"p", 'p', 0, NULL, "get PathRecord info"},
		{"N", 'N', 0, NULL, "get NodeRecord info"},
		{"L", 'L', 0, NULL, "return the Lids of the name specified"},
		{"l", 'l', 0, NULL,
		 "return the unique Lid of the name specified"},
		{"G", 'G', 0, NULL, "return the Guids of the name specified"},
		{"O", 'O', 0, NULL, "return name for the Lid specified"},
		{"U", 'U', 0, NULL, "return name for the Guid specified"},
		{"s", 's', 0, NULL, "return the PortInfoRecords with isSM or"
		 " isSMdisabled capability mask bit on"},
		{"g", 'g', 0, NULL, "get multicast group info"},
		{"m", 'm', 0, NULL, "get multicast member info (if multicast"
		 " group specified, list member GIDs only for group specified,"
		 " for example 'saquery -m 0xC000')"},
		{"x", 'x', 0, NULL, "get LinkRecord info"},
		{"c", 'c', 0, NULL, "get the SA's class port info"},
		{"S", 'S', 0, NULL, "get ServiceRecord info"},
		{"I", 'I', 0, NULL, "get InformInfoRecord (subscription) info"},
		{"list", 'D', 0, NULL, "the node desc of the CA's"},
		{"with-grh", 23, 0, NULL, "add GRH to path record query"},
		{"sa-dgid", 24, 1, "<gid>",
		 "Set destination GID (in IPv6 format) in the GRH"},
		{"src-to-dst", 1, 1, "<src:dst>", "get a PathRecord for"
		 " <src:dst> where src and dst are either node names or LIDs"},
		{"sgid-to-dgid", 2, 1, "<sgid-dgid>", "get a PathRecord for"
		 " <sgid-dgid> where sgid and dgid are addresses in IPv6 format"},
		{"node-name-map", 3, 1, "<file>",
		 "specify a node name map file"},
		{"smkey", 4, 1, "<val>",
		 "SA SM_Key value for the query."
		 " If non-numeric value (like 'x') is specified then"
		 " saquery will prompt for a value. "
		 " Default (when not specified here or in ibdiag.conf) is to "
		 " use SM_Key == 0 (or \"untrusted\")"},
		{"slid", 5, 1, "<lid>", "Source LID (PathRecord)"},
		{"dlid", 6, 1, "<lid>", "Destination LID (PathRecord)"},
		{"mlid", 7, 1, "<lid>", "Multicast LID (MCMemberRecord)"},
		{"sgid", 14, 1, "<gid>",
		 "Source GID (IPv6 format) (PathRecord)"},
		{"dgid", 15, 1, "<gid>",
		 "Destination GID (IPv6 format) (PathRecord)"},
		{"gid", 16, 1, "<gid>", "Port GID (MCMemberRecord)"},
		{"mgid", 17, 1, "<gid>", "Multicast GID (MCMemberRecord)"},
		{"reversible", 'r', 1, NULL, "Reversible path (PathRecord)"},
		{"numb_path", 'n', 1, NULL, "Number of paths (PathRecord)"},
		{"pkey", 18, 1, NULL, "P_Key (PathRecord, MCMemberRecord)."
		 " If non-numeric value (like 'x') is specified then"
		 " saquery will prompt for a value"},
		{"qos_class", 'Q', 1, NULL, "QoS Class (PathRecord)"},
		{"sl", 19, 1, NULL,
		 "Service level (PathRecord, MCMemberRecord)"},
		{"mtu", 'M', 1, NULL,
		 "MTU and selector (PathRecord, MCMemberRecord)"},
		{"rate", 'R', 1, NULL,
		 "Rate and selector (PathRecord, MCMemberRecord)"},
		{"pkt_lifetime", 20, 1, NULL,
		 "Packet lifetime and selector (PathRecord, MCMemberRecord)"},
		{"qkey", 'q', 1, NULL, "Q_Key (MCMemberRecord)."
		 " If non-numeric value (like 'x') is specified then"
		 " saquery will prompt for a value"},
		{"tclass", 'T', 1, NULL,
		 "Traffic Class (PathRecord, MCMemberRecord)"},
		{"flow_label", 'F', 1, NULL,
		 "Flow Label (PathRecord, MCMemberRecord)"},
		{"hop_limit", 'H', 1, NULL,
		 "Hop limit (PathRecord, MCMemberRecord)"},
		{"scope", 21, 1, NULL, "Scope (MCMemberRecord)"},
		{"join_state", 'J', 1, NULL, "Join state (MCMemberRecord)"},
		{"proxy_join", 'X', 1, NULL, "Proxy join (MCMemberRecord)"},
		{"service_id", 22, 1, NULL, "ServiceID (PathRecord)"},
		{0}
	};

	memset(&params, 0, sizeof params);
	params.hop_limit = 0;
	params.reversible = -1;
	params.numb_path = -1;
	params.qos_class = -1;
	params.sl = -1;
	params.proxy_join = -1;

	n = sprintf(usage_args, "[query-name] [<name> | <lid> | <guid>]\n"
		    "\nSupported query names (and aliases):\n");
	for (q = query_cmds; q->name; q++) {
		n += snprintf(usage_args + n, sizeof(usage_args) - n,
			      "  %s (%s) %s\n", q->name,
			      q->alias ? q->alias : "",
			      q->usage ? q->usage : "");
		if (n >= sizeof(usage_args))
			exit(-1);
	}
	snprintf(usage_args + n, sizeof(usage_args) - n,
		 "\n  Queries node records by default.");

	q = NULL;
	ibd_timeout = DEFAULT_SA_TIMEOUT_MS;

	ibdiag_process_opts(argc, argv, &params, "DGLsy", opts, process_opt,
			    usage_args, NULL);

	argc -= optind;
	argv += optind;

	if (!query_type && command == SAQUERY_CMD_QUERY) {
		if (!argc || !(q = find_query(argv[0])))
			query_type = IB_SA_ATTR_NODERECORD;
		else {
			query_type = q->query_type;
			argc--;
			argv++;
		}
	}

	if (argc) {
		if (node_print_desc == NAME_OF_LID) {
			requested_lid = (uint16_t) strtoul(argv[0], NULL, 0);
			requested_lid_flag++;
		} else if (node_print_desc == NAME_OF_GUID) {
			requested_guid = strtoul(argv[0], NULL, 0);
			requested_guid_flag++;
		} else
			requested_name = argv[0];
	}

	if ((node_print_desc == LID_ONLY ||
	     node_print_desc == UNIQUE_LID_ONLY ||
	     node_print_desc == GUID_ONLY) && !requested_name) {
		fprintf(stderr, "ERROR: name not specified\n");
		ibdiag_show_usage();
	}

	if (node_print_desc == NAME_OF_LID && !requested_lid_flag) {
		fprintf(stderr, "ERROR: lid not specified\n");
		ibdiag_show_usage();
	}

	if (node_print_desc == NAME_OF_GUID && !requested_guid_flag) {
		fprintf(stderr, "ERROR: guid not specified\n");
		ibdiag_show_usage();
	}

	/* Note: lid cannot be 0; see infiniband spec 4.1.3 */
	if (node_print_desc == NAME_OF_LID && !requested_lid) {
		fprintf(stderr, "ERROR: lid invalid\n");
		ibdiag_show_usage();
	}

	if (umad_init())
		IBEXIT("Failed to initialized umad library");
	h = sa_get_handle();
	if (!h)
		IBPANIC("Failed to bind to the SA");

	if (params.with_grh) {
		ibmad_gid_t gid = { 0 };
		/*
		 * If GRH destination GID is not specified, try to get it by
		 * querying the SA.
		 */
		if (!memcmp(&gid, &params.sa_dgid, sizeof(ibmad_gid_t))) {
			if ((status = resolve_self(ibd_ca, ibd_ca_port, &portid,
						   &port, 0)) < 0) {
				fprintf(stderr, "can't resolve self port %s\n",
					argv[0]);
				goto error;
			}
			if ((status = sm_pr_query(h, &gid, portid.lid,
						  h->dport.lid)) > 0) {
				fprintf(stderr,
					"Failed to query SA:PathRecord\n");
				goto error;
			}
		} else
			memcpy(&gid, &params.sa_dgid, sizeof(ibmad_gid_t));

		if ((status = sa_set_handle(h, 1, &gid)) < 0) {
			fprintf(stderr, "Failed to set GRH\n");
			goto error;
		}
	}

	node_name_map = open_node_name_map(node_name_map_file);

	if (src_lid && *src_lid)
		params.slid = get_lid(h, src_lid);
	if (dst_lid && *dst_lid)
		params.dlid = get_lid(h, dst_lid);

	if (command == SAQUERY_CMD_CLASS_PORT_INFO ||
	    query_type == CLASS_PORT_INFO ||
	    query_type == IB_SA_ATTR_SWITCHINFORECORD)
		sa_cpi_required = 1;

	if (sa_cpi_required && (status = query_sa_cpi(h, &params)) != 0) {
		fprintf(stderr, "Failed to query SA:ClassPortInfo\n");
		goto error;
	}

	switch (command) {
	case SAQUERY_CMD_NODE_RECORD:
		status = print_node_records(h, &params);
		break;
	case SAQUERY_CMD_CLASS_PORT_INFO:
		dump_class_port_info(&params.cpi);
		status = 0;
		break;
	case SAQUERY_CMD_ISSM:
		status = print_issm_records(h, &params);
		break;
	case SAQUERY_CMD_MCGROUPS:
		status = print_multicast_group_records(h, &params);
		break;
	case SAQUERY_CMD_MCMEMBERS:
		status = print_multicast_member_records(h, &params);
		break;
	default:
		if ((!q && !(q = find_query_by_type(query_type)))
		    || !q->handler) {
			fprintf(stderr, "Unknown query type %d\n",
				ntohs(query_type));
			status = EINVAL;
		} else
			status = q->handler(q, h, &params, argc, argv);
		break;
	}

error:
	if (src_lid)
		free(src_lid);
	sa_free_handle(h);
	umad_done();
	close_node_name_map(node_name_map);
	return (status);
}
