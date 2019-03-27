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
#include <config.h>
#endif				/* HAVE_CONFIG_H */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <complib/cl_nodenamemap.h>
#include <infiniband/ibnetdisc.h>

#include "ibdiag_common.h"

#define LIST_CA_NODE	 (1 << IB_NODE_CA)
#define LIST_SWITCH_NODE (1 << IB_NODE_SWITCH)
#define LIST_ROUTER_NODE (1 << IB_NODE_ROUTER)

#define DIFF_FLAG_SWITCH           0x01
#define DIFF_FLAG_CA               0x02
#define DIFF_FLAG_ROUTER           0x04
#define DIFF_FLAG_PORT_CONNECTION  0x08
#define DIFF_FLAG_LID              0x10
#define DIFF_FLAG_NODE_DESCRIPTION 0x20

#define DIFF_FLAG_DEFAULT (DIFF_FLAG_SWITCH | DIFF_FLAG_CA | DIFF_FLAG_ROUTER \
			   | DIFF_FLAG_PORT_CONNECTION)

static FILE *f;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;
static char *cache_file = NULL;
static char *load_cache_file = NULL;
static char *diff_cache_file = NULL;
static unsigned diffcheck_flags = DIFF_FLAG_DEFAULT;

static int report_max_hops = 0;
static int full_info;

/**
 * Define our own conversion functions to maintain compatibility with the old
 * ibnetdiscover which did not use the ibmad conversion functions.
 */
char *dump_linkspeed_compat(uint32_t speed)
{
	switch (speed) {
	case 1:
		return ("SDR");
		break;
	case 2:
		return ("DDR");
		break;
	case 4:
		return ("QDR");
		break;
	}
	return ("???");
}

char *dump_linkspeedext_compat(uint32_t espeed, uint32_t speed, uint32_t fdr10)
{
	switch (espeed) {
	case 0:
		if (fdr10 & FDR10)
			return ("FDR10");
		else
			return dump_linkspeed_compat(speed);
		break;
	case 1:
		return ("FDR");
		break;
	case 2:
		return ("EDR");
		break;
	}
	return ("???");
}

char *dump_linkwidth_compat(uint32_t width)
{
	switch (width) {
	case 1:
		return ("1x");
		break;
	case 2:
		return ("4x");
		break;
	case 4:
		return ("8x");
		break;
	case 8:
		return ("12x");
		break;
	case 16:
		return ("2x");
		break;
	}
	return ("??");
}

static inline const char *ports_nt_str_compat(ibnd_node_t * node)
{
	switch (node->type) {
	case IB_NODE_SWITCH:
		return "SW";
	case IB_NODE_CA:
		return "CA";
	case IB_NODE_ROUTER:
		return "RT";
	}
	return "??";
}

char *node_name(ibnd_node_t * node)
{
	static char buf[256];

	switch (node->type) {
	case IB_NODE_SWITCH:
		sprintf(buf, "\"%s", "S");
		break;
	case IB_NODE_CA:
		sprintf(buf, "\"%s", "H");
		break;
	case IB_NODE_ROUTER:
		sprintf(buf, "\"%s", "R");
		break;
	default:
		sprintf(buf, "\"%s", "?");
		break;
	}
	sprintf(buf + 2, "-%016" PRIx64 "\"", node->guid);

	return buf;
}

void list_node(ibnd_node_t * node, void *user_data)
{
	char *node_type;
	char *nodename = remap_node_name(node_name_map, node->guid,
					 node->nodedesc);

	switch (node->type) {
	case IB_NODE_SWITCH:
		node_type = "Switch";
		break;
	case IB_NODE_CA:
		node_type = "Ca";
		break;
	case IB_NODE_ROUTER:
		node_type = "Router";
		break;
	default:
		node_type = "???";
		break;
	}
	fprintf(f,
		"%s\t : 0x%016" PRIx64
		" ports %d devid 0x%x vendid 0x%x \"%s\"\n", node_type,
		node->guid, node->numports, mad_get_field(node->info, 0,
							  IB_NODE_DEVID_F),
		mad_get_field(node->info, 0, IB_NODE_VENDORID_F), nodename);

	free(nodename);
}

void list_nodes(ibnd_fabric_t * fabric, int list)
{
	if (list & LIST_CA_NODE)
		ibnd_iter_nodes_type(fabric, list_node, IB_NODE_CA, NULL);
	if (list & LIST_SWITCH_NODE)
		ibnd_iter_nodes_type(fabric, list_node, IB_NODE_SWITCH, NULL);
	if (list & LIST_ROUTER_NODE)
		ibnd_iter_nodes_type(fabric, list_node, IB_NODE_ROUTER, NULL);
}

void out_ids(ibnd_node_t * node, int group, char *chname, char *out_prefix)
{
	uint64_t sysimgguid =
	    mad_get_field64(node->info, 0, IB_NODE_SYSTEM_GUID_F);

	fprintf(f, "\n%svendid=0x%x\n", out_prefix ? out_prefix : "",
		mad_get_field(node->info, 0, IB_NODE_VENDORID_F));
	fprintf(f, "%sdevid=0x%x\n", out_prefix ? out_prefix : "",
		mad_get_field(node->info, 0, IB_NODE_DEVID_F));
	if (sysimgguid)
		fprintf(f, "%ssysimgguid=0x%" PRIx64,
			out_prefix ? out_prefix : "", sysimgguid);
	if (group && node->chassis && node->chassis->chassisnum) {
		fprintf(f, "\t\t# Chassis %d", node->chassis->chassisnum);
		if (chname)
			fprintf(f, " (%s)", clean_nodedesc(chname));
		if (ibnd_is_xsigo_tca(node->guid) && node->ports[1] &&
		    node->ports[1]->remoteport)
			fprintf(f, " slot %d",
				node->ports[1]->remoteport->portnum);
	}
	if (sysimgguid ||
	    (group && node->chassis && node->chassis->chassisnum))
		fprintf(f, "\n");
}

uint64_t out_chassis(ibnd_fabric_t * fabric, unsigned char chassisnum)
{
	uint64_t guid;

	fprintf(f, "\nChassis %u", chassisnum);
	guid = ibnd_get_chassis_guid(fabric, chassisnum);
	if (guid)
		fprintf(f, " (guid 0x%" PRIx64 ")", guid);
	fprintf(f, "\n");
	return guid;
}

void out_switch_detail(ibnd_node_t * node, char *sw_prefix)
{
	char *nodename = NULL;

	nodename = remap_node_name(node_name_map, node->guid, node->nodedesc);

	fprintf(f, "%sSwitch\t%d %s\t\t# \"%s\" %s port 0 lid %d lmc %d",
		sw_prefix ? sw_prefix : "", node->numports, node_name(node),
		nodename, node->smaenhsp0 ? "enhanced" : "base",
		node->smalid, node->smalmc);

	free(nodename);
}

void out_switch(ibnd_node_t * node, int group, char *chname, char *id_prefix,
		char *sw_prefix)
{
	char *str;
	char str2[256];

	out_ids(node, group, chname, id_prefix);
	fprintf(f, "%sswitchguid=0x%" PRIx64,
		id_prefix ? id_prefix : "", node->guid);
	fprintf(f, "(%" PRIx64 ")",
		mad_get_field64(node->info, 0, IB_NODE_PORT_GUID_F));
	if (group) {
		fprintf(f, "\t# ");
		str = ibnd_get_chassis_type(node);
		if (str)
			fprintf(f, "%s ", str);
		str = ibnd_get_chassis_slot_str(node, str2, 256);
		if (str)
			fprintf(f, "%s", str);
	}
	fprintf(f, "\n");

	out_switch_detail(node, sw_prefix);
	fprintf(f, "\n");
}

void out_ca_detail(ibnd_node_t * node, char *ca_prefix)
{
	char *node_type;

	switch (node->type) {
	case IB_NODE_CA:
		node_type = "Ca";
		break;
	case IB_NODE_ROUTER:
		node_type = "Rt";
		break;
	default:
		node_type = "???";
		break;
	}

	fprintf(f, "%s%s\t%d %s\t\t# \"%s\"", ca_prefix ? ca_prefix : "",
		node_type, node->numports, node_name(node),
		clean_nodedesc(node->nodedesc));
}

void out_ca(ibnd_node_t * node, int group, char *chname, char *id_prefix,
	    char *ca_prefix)
{
	char *node_type;

	out_ids(node, group, chname, id_prefix);
	switch (node->type) {
	case IB_NODE_CA:
		node_type = "ca";
		break;
	case IB_NODE_ROUTER:
		node_type = "rt";
		break;
	default:
		node_type = "???";
		break;
	}

	fprintf(f, "%s%sguid=0x%" PRIx64 "\n",
		id_prefix ? id_prefix : "", node_type, node->guid);
	out_ca_detail(node, ca_prefix);
	if (group && ibnd_is_xsigo_hca(node->guid))
		fprintf(f, " (scp)");
	fprintf(f, "\n");
}

#define OUT_BUFFER_SIZE 16
static char *out_ext_port(ibnd_port_t * port, int group)
{
	static char mapping[OUT_BUFFER_SIZE];

	if (group && port->ext_portnum != 0) {
		snprintf(mapping, OUT_BUFFER_SIZE,
			 "[ext %d]", port->ext_portnum);
		return (mapping);
	}

	return (NULL);
}

void out_switch_port(ibnd_port_t * port, int group, char *out_prefix)
{
	char *ext_port_str = NULL;
	char *rem_nodename = NULL;
	uint32_t iwidth = mad_get_field(port->info, 0,
					IB_PORT_LINK_WIDTH_ACTIVE_F);
	uint32_t ispeed = mad_get_field(port->info, 0,
					IB_PORT_LINK_SPEED_ACTIVE_F);
	uint32_t vlcap = mad_get_field(port->info, 0,
				       IB_PORT_VL_CAP_F);
	uint32_t fdr10 = mad_get_field(port->ext_info, 0,
				       IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F);
	uint32_t cap_mask, espeed;

	DEBUG("port %p:%d remoteport %p\n", port, port->portnum,
	      port->remoteport);
	fprintf(f, "%s[%d]", out_prefix ? out_prefix : "", port->portnum);

	ext_port_str = out_ext_port(port, group);
	if (ext_port_str)
		fprintf(f, "%s", ext_port_str);

	rem_nodename = remap_node_name(node_name_map,
				       port->remoteport->node->guid,
				       port->remoteport->node->nodedesc);

	ext_port_str = out_ext_port(port->remoteport, group);

	if (!port->node->ports[0]) {
		cap_mask = 0;
		ispeed = 0;
		espeed = 0;
	} else {
		cap_mask = mad_get_field(port->node->ports[0]->info, 0,
					 IB_PORT_CAPMASK_F);
		if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
			espeed = mad_get_field(port->info, 0,
					       IB_PORT_LINK_SPEED_EXT_ACTIVE_F);
		else
			espeed = 0;
	}
	fprintf(f, "\t%s[%d]%s",
		node_name(port->remoteport->node), port->remoteport->portnum,
		ext_port_str ? ext_port_str : "");
	if (port->remoteport->node->type != IB_NODE_SWITCH)
		fprintf(f, "(%" PRIx64 ") ", port->remoteport->guid);
	fprintf(f, "\t\t# \"%s\" lid %d %s%s",
		rem_nodename,
		port->remoteport->node->type == IB_NODE_SWITCH ?
		port->remoteport->node->smalid :
		port->remoteport->base_lid,
		dump_linkwidth_compat(iwidth),
		(ispeed != 4 && !espeed) ?
			dump_linkspeed_compat(ispeed) :
			dump_linkspeedext_compat(espeed, ispeed, fdr10));

	if (full_info) {
		fprintf(f, " s=%d w=%d v=%d", ispeed, iwidth, vlcap);
		if (espeed)
			fprintf(f, " e=%d", espeed);
	}

	if (ibnd_is_xsigo_tca(port->remoteport->guid))
		fprintf(f, " slot %d", port->portnum);
	else if (ibnd_is_xsigo_hca(port->remoteport->guid))
		fprintf(f, " (scp)");
	fprintf(f, "\n");

	free(rem_nodename);
}

void out_ca_port(ibnd_port_t * port, int group, char *out_prefix)
{
	char *str = NULL;
	char *rem_nodename = NULL;
	uint32_t iwidth = mad_get_field(port->info, 0,
					IB_PORT_LINK_WIDTH_ACTIVE_F);
	uint32_t ispeed = mad_get_field(port->info, 0,
					IB_PORT_LINK_SPEED_ACTIVE_F);
	uint32_t vlcap = mad_get_field(port->info, 0,
				       IB_PORT_VL_CAP_F);
	uint32_t fdr10 = mad_get_field(port->ext_info, 0,
				       IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F);
	uint32_t cap_mask, espeed;

	fprintf(f, "%s[%d]", out_prefix ? out_prefix : "", port->portnum);
	if (port->node->type != IB_NODE_SWITCH)
		fprintf(f, "(%" PRIx64 ") ", port->guid);
	fprintf(f, "\t%s[%d]",
		node_name(port->remoteport->node), port->remoteport->portnum);
	str = out_ext_port(port->remoteport, group);
	if (str)
		fprintf(f, "%s", str);
	if (port->remoteport->node->type != IB_NODE_SWITCH)
		fprintf(f, " (%" PRIx64 ") ", port->remoteport->guid);

	rem_nodename = remap_node_name(node_name_map,
				       port->remoteport->node->guid,
				       port->remoteport->node->nodedesc);

	cap_mask = mad_get_field(port->info, 0, IB_PORT_CAPMASK_F);
	if (cap_mask & CL_NTOH32(IB_PORT_CAP_HAS_EXT_SPEEDS))
		espeed = mad_get_field(port->info, 0,
				       IB_PORT_LINK_SPEED_EXT_ACTIVE_F);
	else
		espeed = 0;

	fprintf(f, "\t\t# lid %d lmc %d \"%s\" lid %d %s%s",
		port->base_lid, port->lmc, rem_nodename,
		port->remoteport->node->type == IB_NODE_SWITCH ?
		port->remoteport->node->smalid :
		port->remoteport->base_lid,
		dump_linkwidth_compat(iwidth),
		(ispeed != 4 && !espeed) ?
			dump_linkspeed_compat(ispeed) :
			dump_linkspeedext_compat(espeed, ispeed, fdr10));

	if (full_info) {
		fprintf(f, " s=%d w=%d v=%d", ispeed, iwidth, vlcap);
		if (espeed)
			fprintf(f, " e=%d", espeed);
	}

	fprintf(f, "\n");

	free(rem_nodename);
}

struct iter_user_data {
	int group;
	int skip_chassis_nodes;
};

static void switch_iter_func(ibnd_node_t * node, void *iter_user_data)
{
	ibnd_port_t *port;
	int p = 0;
	struct iter_user_data *data = (struct iter_user_data *)iter_user_data;

	DEBUG("SWITCH: node %p\n", node);

	/* skip chassis based switches if flagged */
	if (data->skip_chassis_nodes && node->chassis
	    && node->chassis->chassisnum)
		return;

	out_switch(node, data->group, NULL, NULL, NULL);
	for (p = 1; p <= node->numports; p++) {
		port = node->ports[p];
		if (port && port->remoteport)
			out_switch_port(port, data->group, NULL);
	}
}

static void ca_iter_func(ibnd_node_t * node, void *iter_user_data)
{
	ibnd_port_t *port;
	int p = 0;
	struct iter_user_data *data = (struct iter_user_data *)iter_user_data;

	DEBUG("CA: node %p\n", node);
	/* Now, skip chassis based CAs */
	if (data->group && node->chassis && node->chassis->chassisnum)
		return;
	out_ca(node, data->group, NULL, NULL, NULL);

	for (p = 1; p <= node->numports; p++) {
		port = node->ports[p];
		if (port && port->remoteport)
			out_ca_port(port, data->group, NULL);
	}
}

static void router_iter_func(ibnd_node_t * node, void *iter_user_data)
{
	ibnd_port_t *port;
	int p = 0;
	struct iter_user_data *data = (struct iter_user_data *)iter_user_data;

	DEBUG("RT: node %p\n", node);
	/* Now, skip chassis based RTs */
	if (data->group && node->chassis && node->chassis->chassisnum)
		return;
	out_ca(node, data->group, NULL, NULL, NULL);
	for (p = 1; p <= node->numports; p++) {
		port = node->ports[p];
		if (port && port->remoteport)
			out_ca_port(port, data->group, NULL);
	}
}

int dump_topology(int group, ibnd_fabric_t * fabric)
{
	ibnd_node_t *node;
	ibnd_port_t *port;
	int i = 0, p = 0;
	time_t t = time(0);
	uint64_t chguid;
	char *chname = NULL;
	struct iter_user_data iter_user_data;

	fprintf(f, "#\n# Topology file: generated on %s#\n", ctime(&t));
	if (report_max_hops)
		fprintf(f, "# Reported max hops discovered: %u\n"
			"# Total MADs used: %u\n",
			fabric->maxhops_discovered, fabric->total_mads_used);
	fprintf(f, "# Initiated from node %016" PRIx64 " port %016" PRIx64 "\n",
		fabric->from_node->guid,
		mad_get_field64(fabric->from_node->info, 0,
				IB_NODE_PORT_GUID_F));

	/* Make pass on switches */
	if (group) {
		ibnd_chassis_t *ch = NULL;

		/* Chassis based switches first */
		for (ch = fabric->chassis; ch; ch = ch->next) {
			int n = 0;

			if (!ch->chassisnum)
				continue;
			chguid = out_chassis(fabric, ch->chassisnum);
			chname = NULL;
			if (ibnd_is_xsigo_guid(chguid)) {
				for (node = ch->nodes; node;
				     node = node->next_chassis_node) {
					if (ibnd_is_xsigo_hca(node->guid)) {
						chname = node->nodedesc;
						fprintf(f, "Hostname: %s\n",
							clean_nodedesc
							(node->nodedesc));
					}
				}
			}

			fprintf(f, "\n# Spine Nodes");
			for (n = 1; n <= SPINES_MAX_NUM; n++) {
				if (ch->spinenode[n]) {
					out_switch(ch->spinenode[n], group,
						   chname, NULL, NULL);
					for (p = 1;
					     p <= ch->spinenode[n]->numports;
					     p++) {
						port =
						    ch->spinenode[n]->ports[p];
						if (port && port->remoteport)
							out_switch_port(port,
									group,
									NULL);
					}
				}
			}
			fprintf(f, "\n# Line Nodes");
			for (n = 1; n <= LINES_MAX_NUM; n++) {
				if (ch->linenode[n]) {
					out_switch(ch->linenode[n], group,
						   chname, NULL, NULL);
					for (p = 1;
					     p <= ch->linenode[n]->numports;
					     p++) {
						port =
						    ch->linenode[n]->ports[p];
						if (port && port->remoteport)
							out_switch_port(port,
									group,
									NULL);
					}
				}
			}

			fprintf(f, "\n# Chassis Switches");
			for (node = ch->nodes; node;
			     node = node->next_chassis_node) {
				if (node->type == IB_NODE_SWITCH) {
					out_switch(node, group, chname, NULL,
						   NULL);
					for (p = 1; p <= node->numports; p++) {
						port = node->ports[p];
						if (port && port->remoteport)
							out_switch_port(port,
									group,
									NULL);
					}
				}

			}

			fprintf(f, "\n# Chassis CAs");
			for (node = ch->nodes; node;
			     node = node->next_chassis_node) {
				if (node->type == IB_NODE_CA) {
					out_ca(node, group, chname, NULL, NULL);
					for (p = 1; p <= node->numports; p++) {
						port = node->ports[p];
						if (port && port->remoteport)
							out_ca_port(port, group,
								    NULL);
					}
				}
			}

		}

	} else {		/* !group */
		iter_user_data.group = group;
		iter_user_data.skip_chassis_nodes = 0;
		ibnd_iter_nodes_type(fabric, switch_iter_func, IB_NODE_SWITCH,
				     &iter_user_data);
	}

	chname = NULL;
	if (group) {
		iter_user_data.group = group;
		iter_user_data.skip_chassis_nodes = 1;

		fprintf(f, "\nNon-Chassis Nodes\n");

		ibnd_iter_nodes_type(fabric, switch_iter_func, IB_NODE_SWITCH,
				     &iter_user_data);
	}

	iter_user_data.group = group;
	iter_user_data.skip_chassis_nodes = 0;
	/* Make pass on CAs */
	ibnd_iter_nodes_type(fabric, ca_iter_func, IB_NODE_CA, &iter_user_data);

	/* Make pass on routers */
	ibnd_iter_nodes_type(fabric, router_iter_func, IB_NODE_ROUTER,
			     &iter_user_data);

	return i;
}

void dump_ports_report(ibnd_node_t * node, void *user_data)
{
	int p = 0;
	ibnd_port_t *port = NULL;
	char *nodename = NULL;
	char *rem_nodename = NULL;

	/* for each port */
	for (p = node->numports, port = node->ports[p]; p > 0;
	     port = node->ports[--p]) {
		uint32_t iwidth, ispeed, fdr10, espeed, cap_mask;
		uint8_t *info = NULL;
		if (port == NULL)
			continue;
		iwidth =
		    mad_get_field(port->info, 0, IB_PORT_LINK_WIDTH_ACTIVE_F);
		ispeed =
		    mad_get_field(port->info, 0, IB_PORT_LINK_SPEED_ACTIVE_F);
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
		fdr10 = mad_get_field(port->ext_info, 0,
				      IB_MLNX_EXT_PORT_LINK_SPEED_ACTIVE_F);
		nodename = remap_node_name(node_name_map,
					   port->node->guid,
					   port->node->nodedesc);
		fprintf(stdout, "%2s %5d %2d 0x%016" PRIx64 " %s %s",
			ports_nt_str_compat(node),
			node->type ==
			IB_NODE_SWITCH ? node->smalid : port->base_lid,
			port->portnum, port->guid,
			dump_linkwidth_compat(iwidth),
			(ispeed != 4 && !espeed) ?
				dump_linkspeed_compat(ispeed) :
				dump_linkspeedext_compat(espeed, ispeed, fdr10));
		if (port->remoteport) {
			rem_nodename = remap_node_name(node_name_map,
					      port->remoteport->node->guid,
					      port->remoteport->node->nodedesc);
			fprintf(stdout,
				" - %2s %5d %2d 0x%016" PRIx64
				" ( '%s' - '%s' )\n",
				ports_nt_str_compat(port->remoteport->node),
				port->remoteport->node->type == IB_NODE_SWITCH ?
				port->remoteport->node->smalid :
				port->remoteport->base_lid,
				port->remoteport->portnum,
				port->remoteport->guid, nodename, rem_nodename);
			free(rem_nodename);
		} else
			fprintf(stdout, "%36s'%s'\n", "", nodename);

		free(nodename);
	}
}

struct iter_diff_data {
	uint32_t diff_flags;
	ibnd_fabric_t *fabric1;
	ibnd_fabric_t *fabric2;
	char *fabric1_prefix;
	char *fabric2_prefix;
	void (*out_header) (ibnd_node_t *, int, char *, char *, char *);
	void (*out_header_detail) (ibnd_node_t *, char *);
	void (*out_port) (ibnd_port_t *, int, char *);
};

static void diff_iter_out_header(ibnd_node_t * node,
				 struct iter_diff_data *data,
				 int *out_header_flag)
{
	if (!(*out_header_flag)) {
		(*data->out_header) (node, 0, NULL, NULL, NULL);
		(*out_header_flag)++;
	}
}

static void diff_ports(ibnd_node_t * fabric1_node, ibnd_node_t * fabric2_node,
		       int *out_header_flag, struct iter_diff_data *data)
{
	ibnd_port_t *fabric1_port;
	ibnd_port_t *fabric2_port;
	int p;

	for (p = 1; p <= fabric1_node->numports; p++) {
		int fabric1_out = 0, fabric2_out = 0;

		fabric1_port = fabric1_node->ports[p];
		fabric2_port = fabric2_node->ports[p];

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION) {
			if ((fabric1_port && !fabric2_port)
			    || ((fabric1_port && fabric2_port)
				&& (fabric1_port->remoteport
				    && !fabric2_port->remoteport)))
				fabric1_out++;
			else if ((!fabric1_port && fabric2_port)
				 || ((fabric1_port && fabric2_port)
				     && (!fabric1_port->remoteport
					 && fabric2_port->remoteport)))
				fabric2_out++;
			else if ((fabric1_port && fabric2_port)
				 && ((fabric1_port->guid != fabric2_port->guid)
				     ||
				     ((fabric1_port->remoteport
				       && fabric2_port->remoteport)
				      && (fabric1_port->remoteport->guid !=
					  fabric2_port->remoteport->guid)))) {
				fabric1_out++;
				fabric2_out++;
			}
		}

		if ((data->diff_flags & DIFF_FLAG_LID)
		    && fabric1_port && fabric2_port
		    && fabric1_port->base_lid != fabric2_port->base_lid) {
			fabric1_out++;
			fabric2_out++;
		}

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    && data->diff_flags & DIFF_FLAG_NODE_DESCRIPTION
		    && fabric1_port && fabric2_port
		    && fabric1_port->remoteport && fabric2_port->remoteport
		    && memcmp(fabric1_port->remoteport->node->nodedesc,
			      fabric2_port->remoteport->node->nodedesc,
			      IB_SMP_DATA_SIZE)) {
			fabric1_out++;
			fabric2_out++;
		}

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    && data->diff_flags & DIFF_FLAG_NODE_DESCRIPTION
		    && fabric1_port && fabric2_port
		    && fabric1_port->remoteport && fabric2_port->remoteport
		    && memcmp(fabric1_port->remoteport->node->nodedesc,
			      fabric2_port->remoteport->node->nodedesc,
			      IB_SMP_DATA_SIZE)) {
			fabric1_out++;
			fabric2_out++;
		}

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    && data->diff_flags & DIFF_FLAG_LID
		    && fabric1_port && fabric2_port
		    && fabric1_port->remoteport && fabric2_port->remoteport
		    && fabric1_port->remoteport->base_lid != fabric2_port->remoteport->base_lid) {
			fabric1_out++;
			fabric2_out++;
		}

		if (fabric1_out) {
			diff_iter_out_header(fabric1_node, data,
					     out_header_flag);
			(*data->out_port) (fabric1_port, 0,
					   data->fabric1_prefix);
		}
		if (fabric2_out) {
			diff_iter_out_header(fabric1_node, data,
					     out_header_flag);
			(*data->out_port) (fabric2_port, 0,
					   data->fabric2_prefix);
		}
	}
}

static void diff_iter_func(ibnd_node_t * fabric1_node, void *iter_user_data)
{
	struct iter_diff_data *data = iter_user_data;
	ibnd_node_t *fabric2_node;
	ibnd_port_t *fabric1_port;
	int p;

	DEBUG("DEBUG: fabric1_node %p\n", fabric1_node);

	fabric2_node = ibnd_find_node_guid(data->fabric2, fabric1_node->guid);
	if (!fabric2_node) {
		(*data->out_header) (fabric1_node, 0, NULL,
				     data->fabric1_prefix,
				     data->fabric1_prefix);
		for (p = 1; p <= fabric1_node->numports; p++) {
			fabric1_port = fabric1_node->ports[p];
			if (fabric1_port && fabric1_port->remoteport)
				(*data->out_port) (fabric1_port, 0,
						   data->fabric1_prefix);
		}
	} else if (data->diff_flags &
		   (DIFF_FLAG_PORT_CONNECTION | DIFF_FLAG_LID
		    | DIFF_FLAG_NODE_DESCRIPTION)) {
		int out_header_flag = 0;

		if ((data->diff_flags & DIFF_FLAG_LID
		     && fabric1_node->smalid != fabric2_node->smalid) ||
		    (data->diff_flags & DIFF_FLAG_NODE_DESCRIPTION
		     && memcmp(fabric1_node->nodedesc, fabric2_node->nodedesc,
			       IB_SMP_DATA_SIZE))) {
			(*data->out_header) (fabric1_node, 0, NULL, NULL,
					     data->fabric1_prefix);
			(*data->out_header_detail) (fabric2_node,
						    data->fabric2_prefix);
			fprintf(f, "\n");
			out_header_flag++;
		}

		if (fabric1_node->numports != fabric2_node->numports) {
			diff_iter_out_header(fabric1_node, data,
					     &out_header_flag);
			fprintf(f, "%snumports = %d\n", data->fabric1_prefix,
				fabric1_node->numports);
			fprintf(f, "%snumports = %d\n", data->fabric2_prefix,
				fabric2_node->numports);
			return;
		}

		if (data->diff_flags & DIFF_FLAG_PORT_CONNECTION
		    || data->diff_flags & DIFF_FLAG_LID)
			diff_ports(fabric1_node, fabric2_node, &out_header_flag,
				   data);
	}
}

static int diff_common(ibnd_fabric_t * orig_fabric, ibnd_fabric_t * new_fabric,
		       int node_type, uint32_t diff_flags,
		       void (*out_header) (ibnd_node_t *, int, char *, char *,
					   char *),
		       void (*out_header_detail) (ibnd_node_t *, char *),
		       void (*out_port) (ibnd_port_t *, int, char *))
{
	struct iter_diff_data iter_diff_data;

	iter_diff_data.diff_flags = diff_flags;
	iter_diff_data.fabric1 = orig_fabric;
	iter_diff_data.fabric2 = new_fabric;
	iter_diff_data.fabric1_prefix = "< ";
	iter_diff_data.fabric2_prefix = "> ";
	iter_diff_data.out_header = out_header;
	iter_diff_data.out_header_detail = out_header_detail;
	iter_diff_data.out_port = out_port;
	ibnd_iter_nodes_type(orig_fabric, diff_iter_func, node_type,
			     &iter_diff_data);

	/* Do opposite diff to find existence of node types
	 * in new_fabric but not in orig_fabric.
	 *
	 * In this diff, we don't need to check port connections,
	 * lids, or node descriptions since it has already been
	 * done (i.e. checks are only done when guid exists on both
	 * orig and new).
	 */
	iter_diff_data.diff_flags = diff_flags & ~DIFF_FLAG_PORT_CONNECTION;
	iter_diff_data.diff_flags &= ~DIFF_FLAG_LID;
	iter_diff_data.diff_flags &= ~DIFF_FLAG_NODE_DESCRIPTION;
	iter_diff_data.fabric1 = new_fabric;
	iter_diff_data.fabric2 = orig_fabric;
	iter_diff_data.fabric1_prefix = "> ";
	iter_diff_data.fabric2_prefix = "< ";
	iter_diff_data.out_header = out_header;
	iter_diff_data.out_header_detail = out_header_detail;
	iter_diff_data.out_port = out_port;
	ibnd_iter_nodes_type(new_fabric, diff_iter_func, node_type,
			     &iter_diff_data);

	return 0;
}

int diff(ibnd_fabric_t * orig_fabric, ibnd_fabric_t * new_fabric)
{
	if (diffcheck_flags & DIFF_FLAG_SWITCH)
		diff_common(orig_fabric, new_fabric, IB_NODE_SWITCH,
			    diffcheck_flags, out_switch, out_switch_detail,
			    out_switch_port);

	if (diffcheck_flags & DIFF_FLAG_CA)
		diff_common(orig_fabric, new_fabric, IB_NODE_CA,
			    diffcheck_flags, out_ca, out_ca_detail,
			    out_ca_port);

	if (diffcheck_flags & DIFF_FLAG_ROUTER)
		diff_common(orig_fabric, new_fabric, IB_NODE_ROUTER,
			    diffcheck_flags, out_ca, out_ca_detail,
			    out_ca_port);

	return 0;
}

static int list, group, ports_report;

static int process_opt(void *context, int ch, char *optarg)
{
	struct ibnd_config *cfg = context;
	char *p;

	switch (ch) {
	case 1:
		node_name_map_file = strdup(optarg);
		break;
	case 2:
		cache_file = strdup(optarg);
		break;
	case 3:
		load_cache_file = strdup(optarg);
		break;
	case 4:
		diff_cache_file = strdup(optarg);
		break;
	case 5:
		diffcheck_flags = 0;
		p = strtok(optarg, ",");
		while (p) {
			if (!strcasecmp(p, "sw"))
				diffcheck_flags |= DIFF_FLAG_SWITCH;
			else if (!strcasecmp(p, "ca"))
				diffcheck_flags |= DIFF_FLAG_CA;
			else if (!strcasecmp(p, "router"))
				diffcheck_flags |= DIFF_FLAG_ROUTER;
			else if (!strcasecmp(p, "port"))
				diffcheck_flags |= DIFF_FLAG_PORT_CONNECTION;
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
	case 's':
		cfg->show_progress = 1;
		break;
	case 'f':
		full_info = 1;
		break;
	case 'l':
		list = LIST_CA_NODE | LIST_SWITCH_NODE | LIST_ROUTER_NODE;
		break;
	case 'g':
		group = 1;
		break;
	case 'S':
		list = LIST_SWITCH_NODE;
		break;
	case 'H':
		list = LIST_CA_NODE;
		break;
	case 'R':
		list = LIST_ROUTER_NODE;
		break;
	case 'p':
		ports_report = 1;
		break;
	case 'm':
		report_max_hops = 1;
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
	ibnd_fabric_t *fabric = NULL;
	ibnd_fabric_t *diff_fabric = NULL;

	const struct ibdiag_opt opts[] = {
		{"full", 'f', 0, NULL, "show full information (ports' speed and width, vlcap)"},
		{"show", 's', 0, NULL, "show more information"},
		{"list", 'l', 0, NULL, "list of connected nodes"},
		{"grouping", 'g', 0, NULL, "show grouping"},
		{"Hca_list", 'H', 0, NULL, "list of connected CAs"},
		{"Switch_list", 'S', 0, NULL, "list of connected switches"},
		{"Router_list", 'R', 0, NULL, "list of connected routers"},
		{"node-name-map", 1, 1, "<file>", "node name map file"},
		{"cache", 2, 1, "<file>",
		 "filename to cache ibnetdiscover data to"},
		{"load-cache", 3, 1, "<file>",
		 "filename of ibnetdiscover cache to load"},
		{"diff", 4, 1, "<file>",
		 "filename of ibnetdiscover cache to diff"},
		{"diffcheck", 5, 1, "<key(s)>",
		 "specify checks to execute for --diff"},
		{"ports", 'p', 0, NULL, "obtain a ports report"},
		{"max_hops", 'm', 0, NULL,
		 "report max hops discovered by the library"},
		{"outstanding_smps", 'o', 1, NULL,
		 "specify the number of outstanding SMP's which should be "
		 "issued during the scan"},
		{0}
	};
	char usage_args[] = "[topology-file]";

	ibdiag_process_opts(argc, argv, &config, "DGKLs", opts, process_opt,
			    usage_args, NULL);

	f = stdout;

	argc -= optind;
	argv += optind;

	if (ibd_timeout)
		config.timeout_ms = ibd_timeout;

	config.flags = ibd_ibnetdisc_flags;

	if (argc && !(f = fopen(argv[0], "w")))
		IBEXIT("can't open file %s for writing", argv[0]);

	config.mkey = ibd_mkey;

	node_name_map = open_node_name_map(node_name_map_file);

	if (diff_cache_file &&
	    !(diff_fabric = ibnd_load_fabric(diff_cache_file, 0)))
		IBEXIT("loading cached fabric for diff failed\n");

	if (load_cache_file) {
		if ((fabric = ibnd_load_fabric(load_cache_file, 0)) == NULL)
			IBEXIT("loading cached fabric failed\n");
	} else {
		if ((fabric =
		     ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL, &config)) == NULL)
			IBEXIT("discover failed\n");
	}

	if (ports_report)
		ibnd_iter_nodes(fabric, dump_ports_report, NULL);
	else if (list)
		list_nodes(fabric, list);
	else if (diff_fabric)
		diff(diff_fabric, fabric);
	else
		dump_topology(group, fabric);

	if (cache_file)
		if (ibnd_cache_fabric(fabric, cache_file, 0) < 0)
			IBEXIT("caching ibnetdiscover data failed\n");

	ibnd_destroy_fabric(fabric);
	if (diff_fabric)
		ibnd_destroy_fabric(diff_fabric);
	close_node_name_map(node_name_map);
	exit(0);
}
