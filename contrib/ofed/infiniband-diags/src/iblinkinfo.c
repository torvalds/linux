/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2007 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2008 Lawrence Livermore National Lab.  All rights reserved.
 * Copyright (c) 2010,2011 Mellanox Technologies LTD.  All rights reserved.
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <inttypes.h>

#include <complib/cl_nodenamemap.h>
#include <infiniband/ibnetdisc.h>

#include "ibdiag_common.h"

#define DIFF_FLAG_PORT_CONNECTION  0x01
#define DIFF_FLAG_PORT_STATE       0x02
#define DIFF_FLAG_LID              0x04
#define DIFF_FLAG_NODE_DESCRIPTION 0x08

#define DIFF_FLAG_DEFAULT (DIFF_FLAG_PORT_CONNECTION | DIFF_FLAG_PORT_STATE)

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;
static char *load_cache_file = NULL;
static char *diff_cache_file = NULL;
static unsigned diffcheck_flags = DIFF_FLAG_DEFAULT;
static char *filterdownports_cache_file = NULL;
static ibnd_fabric_t *filterdownports_fabric = NULL;

static uint64_t guid = 0;
static char *guid_str = NULL;
static char *dr_path = NULL;
static int all = 0;

static int down_links_only = 0;
static int line_mode = 0;
static int add_sw_settings = 0;
static int only_flag = 0;
static int only_type = 0;

int filterdownport_check(ibnd_node_t * node, ibnd_port_t * port)
{
	ibnd_node_t *fsw;
	ibnd_port_t *fport;
	int fistate;

	fsw = ibnd_find_node_guid(filterdownports_fabric, node->guid);

	if (!fsw)
		return 0;

	if (port->portnum > fsw->numports)
		return 0;

	fport = fsw->ports[port->portnum];

	if (!fport)
		return 0;

	fistate = mad_get_field(fport->info, 0, IB_PORT_STATE_F);

	return (fistate == IB_LINK_DOWN) ? 1 : 0;
}

void print_port(ibnd_node_t * node, ibnd_port_t * port, char *out_prefix)
{
	char width[64], speed[64], state[64], physstate[64];
	char remote_guid_str[256];
	char remote_str[256];
	char link_str[256];
	char width_msg[256];
	char speed_msg[256];
	char ext_port_str[256];
	int iwidth, ispeed, fdr10, espeed, istate, iphystate, cap_mask;
	int n = 0;
	uint8_t *info = NULL;

	if (!port)
		return;

	iwidth = mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
	ispeed = mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
	fdr10 = mad_get_field(port->ext_info, 0,
			      IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F) & FDR10;

	if (port->node->type == IB_NODE_SWITCH) {
		if (port->node->ports[0])
			info = (uint8_t *)&port->node->ports[0]->info;
	}
	else
		info = (uint8_t *)&port->info;

	if (info) {
		cap_mask = mad_get_field(info, 0, IB_PORT_CAPMASK_F);
		if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
			espeed = mad_get_field(port->info, 0,
					       IB_PORT_LINK_SPEED_EXT_ACTIVE_F);
		else
			espeed = 0;
	} else {
		ispeed = 0;
		iwidth = 0;
		espeed = 0;
	}

	istate = mad_get_field(port->info, 0, IB_PORT_STATE_F);
	iphystate = mad_get_field(port->info, 0, IB_PORT_PHYS_STATE_F);

	remote_guid_str[0] = '\0';
	remote_str[0] = '\0';
	link_str[0] = '\0';
	width_msg[0] = '\0';
	speed_msg[0] = '\0';

	if (istate == IB_LINK_DOWN
	    && filterdownports_fabric
	    && filterdownport_check(node, port))
		return;

	/* C14-24.2.1 states that a down port allows for invalid data to be
	 * returned for all PortInfo components except PortState and
	 * PortPhysicalState */
	if (istate != IB_LINK_DOWN) {
		if (!espeed) {
			if (fdr10)
				sprintf(speed, "10.0 Gbps (FDR10)");
			else
				mad_dump_val(IB_PORT_LINK_SPEED_ACTIVE_F, speed,
					     64, &ispeed);
		} else
			mad_dump_val(IB_PORT_LINK_SPEED_EXT_ACTIVE_F, speed,
				     64, &espeed);

		n = snprintf(link_str, 256, "(%3s %18s %6s/%8s)",
		     mad_dump_val(IB_PORT_LINK_WIDTH_ACTIVE_F, width, 64,
				  &iwidth),
		     speed,
		     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
		     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
				  &iphystate));
	} else {
		n = snprintf(link_str, 256, "(              %6s/%8s)",
		     mad_dump_val(IB_PORT_STATE_F, state, 64, &istate),
		     mad_dump_val(IB_PORT_PHYS_STATE_F, physstate, 64,
				  &iphystate));
	}

	/* again default values due to C14-24.2.1 */
	if (add_sw_settings && istate != IB_LINK_DOWN) {
		snprintf(link_str + n, 256 - n,
			" (HOQ:%d VL_Stall:%d)",
			mad_get_field(port->info, 0,
				IB_PORT_HOQ_LIFE_F),
			mad_get_field(port->info, 0,
				IB_PORT_VL_STALL_COUNT_F));
	}

	if (port->remoteport) {
		char *remap =
		    remap_node_name(node_name_map, port->remoteport->node->guid,
				    port->remoteport->node->nodedesc);

		if (port->remoteport->ext_portnum)
			snprintf(ext_port_str, 256, "%d",
				 port->remoteport->ext_portnum);
		else
			ext_port_str[0] = '\0';

		get_max_msg(width_msg, speed_msg, 256, port);

		if (line_mode) {
			snprintf(remote_guid_str, 256,
				 "0x%016" PRIx64 " ",
				 port->remoteport->guid);
		}

		snprintf(remote_str, 256, "%s%6d %4d[%2s] \"%s\" (%s %s)\n",
			 remote_guid_str, port->remoteport->base_lid ?
			 port->remoteport->base_lid :
			 port->remoteport->node->smalid,
			 port->remoteport->portnum, ext_port_str, remap,
			 width_msg, speed_msg);
		free(remap);
	} else {
		if (istate == IB_LINK_DOWN)
			snprintf(remote_str, 256, "           [  ] \"\" ( )\n");
		else
			snprintf(remote_str, 256, "    \"Port not available\"\n");
	}

	if (port->ext_portnum)
		snprintf(ext_port_str, 256, "%d", port->ext_portnum);
	else
		ext_port_str[0] = '\0';

	if (line_mode) {
		char *remap = remap_node_name(node_name_map, node->guid,
					      node->nodedesc);
		printf("%s0x%016" PRIx64 " \"%30s\" ",
		       out_prefix ? out_prefix : "",
		       port->guid, remap);
		free(remap);
	} else
		printf("%s      ", out_prefix ? out_prefix : "");

	if (port->node->type != IB_NODE_SWITCH) {
		if (!line_mode)
			printf("0x%016" PRIx64 " ", port->guid);

		printf("%6d %4d[%2s] ==%s==>  %s",
			port->base_lid,
			port->portnum, ext_port_str, link_str, remote_str);
	} else
		printf("%6d %4d[%2s] ==%s==>  %s",
			node->smalid, port->portnum, ext_port_str,
			link_str, remote_str);
}

static inline const char *nodetype_str(ibnd_node_t * node)
{
	switch (node->type) {
	case IB_NODE_SWITCH:
		return "Switch";
	case IB_NODE_CA:
		return "CA";
	case IB_NODE_ROUTER:
		return "Router";
	}
	return "??";
}

void print_node_header(ibnd_node_t *node, int *out_header_flag,
			char *out_prefix)
{
	uint64_t guid = 0;
	if ((!out_header_flag || !(*out_header_flag)) && !line_mode) {
		char *remap =
			remap_node_name(node_name_map, node->guid, node->nodedesc);
		if (node->type == IB_NODE_SWITCH) {
			if (node->ports[0])
				guid = node->ports[0]->guid;
			else /* if (node->info) */
				guid = mad_get_field64(node->info, 0, IB_NODE_PORT_GUID_F);

			printf("%s%s: 0x%016" PRIx64 " %s:\n",
				out_prefix ? out_prefix : "",
				nodetype_str(node),
				guid,
				remap);
		} else
			printf("%s%s: %s:\n",
				out_prefix ? out_prefix : "",
				nodetype_str(node), remap);
		(*out_header_flag)++;
		free(remap);
	}
}

void print_node(ibnd_node_t * node, void *user_data)
{
	int i = 0;
	int head_print = 0;
	char *out_prefix = (char *)user_data;

	for (i = 1; i <= node->numports; i++) {
		ibnd_port_t *port = node->ports[i];
		if (!port)
			continue;
		if (!down_links_only ||
		    mad_get_field(port->info, 0,
				  IB_PORT_STATE_F) == IB_LINK_DOWN) {
			print_node_header(node, &head_print, out_prefix);
			print_port(node, port, out_prefix);
		}
	}
}

struct iter_diff_data {
        uint32_t diff_flags;
        ibnd_fabric_t *fabric1;
        ibnd_fabric_t *fabric2;
        char *fabric1_prefix;
        char *fabric2_prefix;
};

void diff_node_ports(ibnd_node_t * fabric1_node, ibnd_node_t * fabric2_node,
		       int *head_print, struct iter_diff_data *data)
{
	int i = 0;

	for (i = 1; i <= fabric1_node->numports; i++) {
		ibnd_port_t *fabric1_port, *fabric2_port;
		int output_diff = 0;

		fabric1_port = fabric1_node->ports[i];
		fabric2_port = fabric2_node->ports[i];

		if (!fabric1_port && !fabric2_port)
			continue;

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION) {
			if ((fabric1_port && !fabric2_port)
			    || (!fabric1_port && fabric2_port)
			    || (fabric1_port->remoteport
				&& !fabric2_port->remoteport)
			    || (!fabric1_port->remoteport
				&& fabric2_port->remoteport)
			    || (fabric1_port->remoteport
				&& fabric2_port->remoteport
				&& fabric1_port->remoteport->guid !=
				fabric2_port->remoteport->guid))
				output_diff++;
		}

		/* if either fabric1_port or fabric2_port NULL, should be
		 * handled by port connection diff code
		 */
		if (data->diff_flags & DIFF_FLAG_PORT_STATE
		    && fabric1_port
		    && fabric2_port) {
			int state1, state2;

			state1 = mad_get_field(fabric1_port->info, 0,
					       IB_PORT_STATE_F);
			state2 = mad_get_field(fabric2_port->info, 0,
					       IB_PORT_STATE_F);

			if (state1 != state2)
				output_diff++;
		}

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    && data->diff_flags & DIFF_FLAG_LID
		    && fabric1_port && fabric2_port
		    && fabric1_port->remoteport && fabric2_port->remoteport
		    && fabric1_port->remoteport->base_lid != fabric2_port->remoteport->base_lid)
			output_diff++;

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    && data->diff_flags & DIFF_FLAG_NODE_DESCRIPTION
		    && fabric1_port && fabric2_port
		    && fabric1_port->remoteport && fabric2_port->remoteport
		    && memcmp(fabric1_port->remoteport->node->nodedesc,
			      fabric2_port->remoteport->node->nodedesc,
			      IB_SMP_DATA_SIZE))
			output_diff++;

		if (output_diff && fabric1_port) {
			print_node_header(fabric1_node,
					    head_print,
					    NULL);
			print_port(fabric1_node,
				   fabric1_port,
				   data->fabric1_prefix);
		}

		if (output_diff && fabric2_port) {
			print_node_header(fabric1_node,
					    head_print,
					    NULL);
			print_port(fabric2_node,
				   fabric2_port,
				   data->fabric2_prefix);
		}
	}
}

void diff_node_iter(ibnd_node_t * fabric1_node, void *iter_user_data)
{
	struct iter_diff_data *data = iter_user_data;
	ibnd_node_t *fabric2_node;
	int head_print = 0;

	DEBUG("DEBUG: fabric1_node %p\n", fabric1_node);

	fabric2_node = ibnd_find_node_guid(data->fabric2, fabric1_node->guid);
	if (!fabric2_node)
		print_node(fabric1_node, data->fabric1_prefix);
	else if (data->diff_flags &
		 (DIFF_FLAG_PORT_CONNECTION | DIFF_FLAG_PORT_STATE
		  | DIFF_FLAG_LID | DIFF_FLAG_NODE_DESCRIPTION)) {

		if ((fabric1_node->type == IB_NODE_SWITCH
		     && data->diff_flags & DIFF_FLAG_LID
		     && fabric1_node->smalid != fabric2_node->smalid) ||
		    (data->diff_flags & DIFF_FLAG_NODE_DESCRIPTION
		     && memcmp(fabric1_node->nodedesc, fabric2_node->nodedesc,
			       IB_SMP_DATA_SIZE))) {
			print_node_header(fabric1_node,
					    NULL,
					    data->fabric1_prefix);
			print_node_header(fabric2_node,
					    NULL,
					    data->fabric2_prefix);
			head_print++;
		}

		if (fabric1_node->numports != fabric2_node->numports) {
			print_node_header(fabric1_node,
					    &head_print,
					    NULL);
			printf("%snumports = %d\n", data->fabric1_prefix,
			       fabric1_node->numports);
			printf("%snumports = %d\n", data->fabric2_prefix,
			       fabric2_node->numports);
			return;
		}

		diff_node_ports(fabric1_node, fabric2_node,
				  &head_print, data);
	}
}

int diff_node(ibnd_node_t * node, ibnd_fabric_t * orig_fabric,
		ibnd_fabric_t * new_fabric)
{
	struct iter_diff_data iter_diff_data;

	iter_diff_data.diff_flags = diffcheck_flags;
	iter_diff_data.fabric1 = orig_fabric;
	iter_diff_data.fabric2 = new_fabric;
	iter_diff_data.fabric1_prefix = "< ";
	iter_diff_data.fabric2_prefix = "> ";
	if (node)
		diff_node_iter(node, &iter_diff_data);
	else {
		if (only_flag)
			ibnd_iter_nodes_type(orig_fabric, diff_node_iter,
					     only_type, &iter_diff_data);
		else
			ibnd_iter_nodes(orig_fabric, diff_node_iter,
					&iter_diff_data);
	}

	/* Do opposite diff to find existence of node types
	 * in new_fabric but not in orig_fabric.
	 *
	 * In this diff, we don't need to check port connections,
	 * port state, lids, or node descriptions since it has already
	 * been done (i.e. checks are only done when guid exists on both
	 * orig and new).
	 */
	iter_diff_data.diff_flags = diffcheck_flags & ~DIFF_FLAG_PORT_CONNECTION;
	iter_diff_data.diff_flags &= ~DIFF_FLAG_PORT_STATE;
	iter_diff_data.diff_flags &= ~DIFF_FLAG_LID;
	iter_diff_data.diff_flags &= ~DIFF_FLAG_NODE_DESCRIPTION;
	iter_diff_data.fabric1 = new_fabric;
	iter_diff_data.fabric2 = orig_fabric;
	iter_diff_data.fabric1_prefix = "> ";
	iter_diff_data.fabric2_prefix = "< ";
	if (node)
		diff_node_iter(node, &iter_diff_data);
	else {
		if (only_flag)
			ibnd_iter_nodes_type(new_fabric, diff_node_iter,
					     only_type, &iter_diff_data);
		else
			ibnd_iter_nodes(new_fabric, diff_node_iter,
					&iter_diff_data);
	}

	return 0;
}

static int process_opt(void *context, int ch, char *optarg)
{
	struct ibnd_config *cfg = context;
	char *p;

	switch (ch) {
	case 1:
		node_name_map_file = strdup(optarg);
		break;
	case 2:
		load_cache_file = strdup(optarg);
		break;
	case 3:
		diff_cache_file = strdup(optarg);
		break;
	case 4:
		diffcheck_flags = 0;
		p = strtok(optarg, ",");
		while (p) {
			if (!strcasecmp(p, "port"))
				diffcheck_flags |= DIFF_FLAG_PORT_CONNECTION;
			else if (!strcasecmp(p, "state"))
				diffcheck_flags |= DIFF_FLAG_PORT_STATE;
			else if (!strcasecmp(p, "lid"))
				diffcheck_flags |= DIFF_FLAG_LID;
			else if (!strcasecmp(p, "nodedesc"))
				diffcheck_flags |= DIFF_FLAG_NODE_DESCRIPTION;
			else {
				fprintf(stderr, "invalid diff check key: %s\n",
					p);
				return -1;
			}
			p = strtok(NULL, ",");
		}
		break;
	case 5:
		filterdownports_cache_file = strdup(optarg);
		break;
	case 6:
		only_flag = 1;
		only_type = IB_NODE_SWITCH;
		break;
	case 7:
		only_flag = 1;
		only_type = IB_NODE_CA;
		break;
	case 'S':
	case 'G':
		guid_str = optarg;
		guid = (uint64_t) strtoull(guid_str, 0, 0);
		break;
	case 'D':
		dr_path = strdup(optarg);
		break;
	case 'a':
		all = 1;
		break;
	case 'n':
		cfg->max_hops = strtoul(optarg, NULL, 0);
		break;
	case 'd':
		down_links_only = 1;
		break;
	case 'l':
		line_mode = 1;
		break;
	case 'p':
		add_sw_settings = 1;
		break;
	case 'R':		/* nop */
		break;
	case 'o':
		cfg->max_smps = strtoul(optarg, NULL, 0);
		break;
	default:
		return -1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct ibnd_config config = { 0 };
	int rc = 0;
	int resolved = -1;
	ibnd_fabric_t *fabric = NULL;
	ibnd_fabric_t *diff_fabric = NULL;
	struct ibmad_port *ibmad_port;
	ib_portid_t port_id = { 0 };
	uint8_t ni[IB_SMP_DATA_SIZE] = { 0 };
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };

	const struct ibdiag_opt opts[] = {
		{"node-name-map", 1, 1, "<file>", "node name map file"},
		{"switch", 'S', 1, "<port_guid>",
		 "start partial scan at the port specified by <port_guid> (hex format)"},
		{"port-guid", 'G', 1, "<port_guid>",
		 "(same as -S)"},
		{"Direct", 'D', 1, "<dr_path>",
		 "start partial scan at the port specified by <dr_path>"},
		{"all", 'a', 0, NULL,
		 "print all nodes found in a partial fabric scan"},
		{"hops", 'n', 1, "<hops>",
		 "Number of hops to include away from specified node"},
		{"down", 'd', 0, NULL, "print only down links"},
		{"line", 'l', 0, NULL,
		 "(line mode) print all information for each link on a single line"},
		{"additional", 'p', 0, NULL,
		 "print additional port settings (PktLifeTime, HoqLife, VLStallCount)"},
		{"load-cache", 2, 1, "<file>",
		 "filename of ibnetdiscover cache to load"},
		{"diff", 3, 1, "<file>",
		 "filename of ibnetdiscover cache to diff"},
		{"diffcheck", 4, 1, "<key(s)>",
		 "specify checks to execute for --diff"},
		{"filterdownports", 5, 1, "<file>",
		 "filename of ibnetdiscover cache to filter downports"},
		{"outstanding_smps", 'o', 1, NULL,
		 "specify the number of outstanding SMP's which should be "
		 "issued during the scan"},
		{"switches-only", 6, 0, NULL,
		 "Output only switches"},
		{"cas-only", 7, 0, NULL,
		 "Output only CAs"},
		{0}
	};
	char usage_args[] = "";

	ibdiag_process_opts(argc, argv, &config, "aDdGgKLlnpRS", opts,
			    process_opt, usage_args, NULL);

	argc -= optind;
	argv += optind;

	ibmad_port = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!ibmad_port) {
		fprintf(stderr, "Failed to open %s port %d\n", ibd_ca,
			ibd_ca_port);
		exit(1);
	}

	smp_mkey_set(ibmad_port, ibd_mkey);

	if (ibd_timeout) {
		mad_rpc_set_timeout(ibmad_port, ibd_timeout);
		config.timeout_ms = ibd_timeout;
	}

	config.flags = ibd_ibnetdisc_flags;
	config.mkey = ibd_mkey;

	node_name_map = open_node_name_map(node_name_map_file);

	if (dr_path && load_cache_file) {
		mad_rpc_close_port(ibmad_port);
		fprintf(stderr, "Cannot specify cache and direct route path\n");
		exit(1);
	}

	if (dr_path) {
		/* only scan part of the fabric */
		if ((resolved =
		     resolve_portid_str(ibd_ca, ibd_ca_port, &port_id, dr_path,
					IB_DEST_DRPATH, NULL, ibmad_port)) < 0)
			IBWARN("Failed to resolve %s; attempting full scan",
			       dr_path);
	} else if (guid_str) {
		if ((resolved =
		     resolve_portid_str(ibd_ca, ibd_ca_port, &port_id,
				        guid_str, IB_DEST_GUID, NULL,
					ibmad_port)) < 0)
			IBWARN("Failed to resolve %s; attempting full scan\n",
			       guid_str);
	}

	if (!smp_query_via(ni, &port_id, IB_ATTR_NODE_INFO, 0,
				   ibd_timeout, ibmad_port)){
			mad_rpc_close_port(ibmad_port);
			fprintf(stderr,
				"Failed to get local Node Info\n");
			exit(1);
	}
	mad_rpc_close_port(ibmad_port);

	if (diff_cache_file &&
	    !(diff_fabric = ibnd_load_fabric(diff_cache_file, 0)))
		IBEXIT("loading cached fabric for diff failed\n");

	if (filterdownports_cache_file &&
	    !(filterdownports_fabric = ibnd_load_fabric(filterdownports_cache_file, 0)))
		IBEXIT("loading cached fabric for filterdownports failed\n");

	if (load_cache_file) {
		if ((fabric = ibnd_load_fabric(load_cache_file, 0)) == NULL) {
			fprintf(stderr, "loading cached fabric failed\n");
			exit(1);
		}
	} else {
		if (resolved >= 0) {
			if (!config.max_hops)
				config.max_hops = 1;
			if (!(fabric =
			    ibnd_discover_fabric(ibd_ca, ibd_ca_port, &port_id, &config)))
				IBWARN("Partial fabric scan failed;"
				       " attempting full scan\n");
		}

		if (!fabric &&
		    !(fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL, &config))) {
			fprintf(stderr, "discover failed\n");
			rc = 1;
			goto close_port;
		}
	}

	if (!all && guid_str) {
		ibnd_port_t *p = ibnd_find_port_guid(fabric, guid);
		if (p && (!only_flag || p->node->type == only_type)) {
			ibnd_node_t *n = p->node;
			if (diff_fabric)
				diff_node(n, diff_fabric, fabric);
			else
				print_node(n, NULL);
		}
		else
			fprintf(stderr, "Failed to find port: %s\n", guid_str);
	} else if (!all && dr_path) {
		ibnd_port_t *p = NULL;
		mad_decode_field(ni, IB_NODE_PORT_GUID_F, &(guid));

		p = ibnd_find_port_guid(fabric, guid);
		if (p && (!only_flag || p->node->type == only_type)) {
			ibnd_node_t *n = p->node;
			if (diff_fabric)
				diff_node(n, diff_fabric, fabric);
			else
				print_node(n, NULL);
		}
		else
			fprintf(stderr, "Failed to find port: %s\n", dr_path);
	} else {
		if (diff_fabric)
			diff_node(NULL, diff_fabric, fabric);
		else {
			if (only_flag)
				ibnd_iter_nodes_type(fabric, print_node,
						     only_type, NULL);
			else
				ibnd_iter_nodes(fabric, print_node, NULL);
		}
	}

	ibnd_destroy_fabric(fabric);
	if (diff_fabric)
		ibnd_destroy_fabric(diff_fabric);

close_port:
	close_node_name_map(node_name_map);
	exit(rc);
}
