/*
 * Copyright (c) 2008 Lawrence Livermore National Security
 * Copyright (c) 2008-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define _GNU_SOURCE
#include <getopt.h>

#include <infiniband/mad.h>
#include <iba/ib_types.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;
/* for local link integrity */
int error_port = 1;

static uint16_t get_node_type(ib_portid_t * port)
{
	uint16_t node_type = IB_NODE_TYPE_CA;
	uint8_t data[IB_SMP_DATA_SIZE] = { 0 };

	if (smp_query_via(data, port, IB_ATTR_NODE_INFO, 0, 0, srcport))
		node_type = (uint16_t) mad_get_field(data, 0, IB_NODE_TYPE_F);
	return node_type;
}

static uint32_t get_cap_mask(ib_portid_t * port)
{
	uint8_t data[IB_SMP_DATA_SIZE] = { 0 };
	uint32_t cap_mask = 0;

	if (smp_query_via(data, port, IB_ATTR_PORT_INFO, 0, 0, srcport))
		cap_mask = (uint32_t) mad_get_field(data, 0, IB_PORT_CAPMASK_F);
	return cap_mask;
}

static void build_trap145(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	n->generic_type = 0x80 | IB_NOTICE_TYPE_INFO;
	n->g_or_v.generic.prod_type_lsb = cl_hton16(get_node_type(port));
	n->g_or_v.generic.trap_num = cl_hton16(145);
	n->issuer_lid = cl_hton16((uint16_t) port->lid);
	n->data_details.ntc_145.new_sys_guid = cl_hton64(0x1234567812345678);
}

static void build_trap144_local(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	n->generic_type = 0x80 | IB_NOTICE_TYPE_INFO;
	n->g_or_v.generic.prod_type_lsb = cl_hton16(get_node_type(port));
	n->g_or_v.generic.trap_num = cl_hton16(144);
	n->issuer_lid = cl_hton16((uint16_t) port->lid);
	n->data_details.ntc_144.lid = n->issuer_lid;
	n->data_details.ntc_144.new_cap_mask = cl_hton32(get_cap_mask(port));
	n->data_details.ntc_144.local_changes =
	    TRAP_144_MASK_OTHER_LOCAL_CHANGES;
}

static void build_trap144_nodedesc(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	build_trap144_local(n, port);
	n->data_details.ntc_144.change_flgs =
	    TRAP_144_MASK_NODE_DESCRIPTION_CHANGE;
}

static void build_trap144_linkspeed(ib_mad_notice_attr_t * n,
				    ib_portid_t * port)
{
	build_trap144_local(n, port);
	n->data_details.ntc_144.change_flgs =
	    TRAP_144_MASK_LINK_SPEED_ENABLE_CHANGE;
}

static void build_trap129(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	n->generic_type = 0x80 | IB_NOTICE_TYPE_URGENT;
	n->g_or_v.generic.prod_type_lsb = cl_hton16(get_node_type(port));
	n->g_or_v.generic.trap_num = cl_hton16(129);
	n->issuer_lid = cl_hton16((uint16_t) port->lid);
	n->data_details.ntc_129_131.lid = n->issuer_lid;
	n->data_details.ntc_129_131.pad = 0;
	n->data_details.ntc_129_131.port_num = (uint8_t) error_port;
}

static void build_trap256_local(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	n->generic_type = 0x80 | IB_NOTICE_TYPE_SECURITY;
	n->g_or_v.generic.prod_type_lsb = cl_hton16(get_node_type(port));
	n->g_or_v.generic.trap_num = cl_hton16(256);
	n->issuer_lid = cl_hton16((uint16_t) port->lid);
	n->data_details.ntc_256.lid = n->issuer_lid;
	n->data_details.ntc_256.dr_slid = 0xffff;
	n->data_details.ntc_256.method = 1;
	n->data_details.ntc_256.attr_id = cl_ntoh16(0x15);
	n->data_details.ntc_256.attr_mod = cl_ntoh32(0x12);
	n->data_details.ntc_256.mkey = cl_ntoh64(0x1234567812345678);
}

static void build_trap256_lid(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	build_trap256_local(n, port);
	n->data_details.ntc_256.dr_trunc_hop = 0;
}

static void build_trap256_dr(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	build_trap256_local(n, port);
	n->data_details.ntc_256.dr_trunc_hop = 0x80 | 0x4;
	n->data_details.ntc_256.dr_rtn_path[0] = 5;
	n->data_details.ntc_256.dr_rtn_path[1] = 6;
	n->data_details.ntc_256.dr_rtn_path[2] = 7;
	n->data_details.ntc_256.dr_rtn_path[3] = 8;
}

static void build_trap257_258(ib_mad_notice_attr_t * n, ib_portid_t * port,
			      uint16_t trap_num)
{
	n->generic_type = 0x80 | IB_NOTICE_TYPE_SECURITY;
	n->g_or_v.generic.prod_type_lsb = cl_hton16(get_node_type(port));
	n->g_or_v.generic.trap_num = cl_hton16(trap_num);
	n->issuer_lid = cl_hton16((uint16_t) port->lid);
	n->data_details.ntc_257_258.lid1 = cl_hton16(1);
	n->data_details.ntc_257_258.lid2 = cl_hton16(2);
	n->data_details.ntc_257_258.key = cl_hton32(0x12345678);
	n->data_details.ntc_257_258.qp1 = cl_hton32(0x010101);
	n->data_details.ntc_257_258.qp2 = cl_hton32(0x020202);
	n->data_details.ntc_257_258.gid1.unicast.prefix = cl_ntoh64(0xf8c0000000000001);
	n->data_details.ntc_257_258.gid1.unicast.interface_id = cl_ntoh64(0x1111222233334444);
	n->data_details.ntc_257_258.gid2.unicast.prefix = cl_ntoh64(0xf8c0000000000001);
	n->data_details.ntc_257_258.gid2.unicast.interface_id = cl_ntoh64(0x5678567812341234);
}

static void build_trap257(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	build_trap257_258(n, port, 257);
}

static void build_trap258(ib_mad_notice_attr_t * n, ib_portid_t * port)
{
	build_trap257_258(n, port, 258);
}

static int send_trap(void (*build) (ib_mad_notice_attr_t *, ib_portid_t *))
{
	ib_portid_t sm_port;
	ib_portid_t selfportid;
	int selfport;
	ib_rpc_t trap_rpc;
	ib_mad_notice_attr_t notice;

	if (resolve_self(ibd_ca, ibd_ca_port, &selfportid, &selfport, NULL))
		IBEXIT("can't resolve self");

	if (resolve_sm_portid(ibd_ca, ibd_ca_port, &sm_port))
		IBEXIT("can't resolve SM destination port");

	memset(&trap_rpc, 0, sizeof(trap_rpc));
	trap_rpc.mgtclass = IB_SMI_CLASS;
	trap_rpc.method = IB_MAD_METHOD_TRAP;
	trap_rpc.trid = mad_trid();
	trap_rpc.attr.id = NOTICE;
	trap_rpc.datasz = IB_SMP_DATA_SIZE;
	trap_rpc.dataoffs = IB_SMP_DATA_OFFS;

	memset(&notice, 0, sizeof(notice));
	build(&notice, &selfportid);

	return mad_send_via(&trap_rpc, &sm_port, NULL, &notice, srcport);
}

typedef struct _trap_def {
	char *trap_name;
	void (*build_func) (ib_mad_notice_attr_t *, ib_portid_t *);
} trap_def_t;

static const trap_def_t traps[] = {
	{"node_desc_change", build_trap144_nodedesc},
	{"link_speed_enabled_change", build_trap144_linkspeed},
	{"local_link_integrity", build_trap129},
	{"sys_image_guid_change", build_trap145},
	{"mkey_lid", build_trap256_lid},
	{"mkey_dr", build_trap256_dr},
	{"pkey", build_trap257},
	{"qkey", build_trap258},
	{NULL, NULL}
};

int process_send_trap(char *trap_name)
{
	int i;

	for (i = 0; traps[i].trap_name; i++)
		if (strcmp(traps[i].trap_name, trap_name) == 0)
			return send_trap(traps[i].build_func);
	ibdiag_show_usage();
	return 1;
}

int main(int argc, char **argv)
{
	char usage_args[1024];
	int mgmt_classes[2] = { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS };
	char *trap_name = NULL;
	int i, n, rc;

	n = sprintf(usage_args, "[<trap_name>] [<error_port>]\n"
		    "\nArgument <trap_name> can be one of the following:\n");
	for (i = 0; traps[i].trap_name; i++) {
		n += snprintf(usage_args + n, sizeof(usage_args) - n,
			      "  %s\n", traps[i].trap_name);
		if (n >= sizeof(usage_args))
			exit(-1);
	}
	snprintf(usage_args + n, sizeof(usage_args) - n,
		 "\n  default behavior is to send \"%s\"", traps[0].trap_name);

	ibdiag_process_opts(argc, argv, NULL, "DGKL", NULL, NULL,
			    usage_args, NULL);

	argc -= optind;
	argv += optind;

	trap_name = argv[0] ? argv[0] : traps[0].trap_name;

	if (argc > 1)
		error_port = atoi(argv[1]);

	madrpc_show_errors(1);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 2);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	rc = process_send_trap(trap_name);
	mad_rpc_close_port(srcport);
	return rc;
}
