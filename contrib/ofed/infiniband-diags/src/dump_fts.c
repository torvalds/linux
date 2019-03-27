/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009-2011 Mellanox Technologies LTD.  All rights reserved.
 * Copyright (c) 2013 Lawrence Livermore National Security.  All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <getopt.h>
#include <netinet/in.h>
#include <assert.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <complib/cl_nodenamemap.h>

#include <infiniband/ibnetdisc.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

unsigned startlid = 0, endlid = 0;

static int brief, dump_all, multicast;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

#define IB_MLIDS_IN_BLOCK	(IB_SMP_DATA_SIZE/2)

int dump_mlid(char *str, int strlen, unsigned mlid, unsigned nports,
	      uint16_t mft[16][IB_MLIDS_IN_BLOCK])
{
	uint16_t mask;
	unsigned i, chunk, bit, nonzero = 0;

	if (brief) {
		int n = 0;
		unsigned chunks = ALIGN(nports + 1, 16) / 16;
		for (i = 0; i < chunks; i++) {
			mask = ntohs(mft[i][mlid % IB_MLIDS_IN_BLOCK]);
			if (mask)
				nonzero++;
			n += snprintf(str + n, strlen - n, "%04hx", mask);
			if (n >= strlen) {
				n = strlen;
				break;
			}
		}
		if (!nonzero && !dump_all) {
			str[0] = 0;
			return 0;
		}
		return n;
	}
	for (i = 0; i <= nports; i++) {
		chunk = i / 16;
		bit = i % 16;

		mask = ntohs(mft[chunk][mlid % IB_MLIDS_IN_BLOCK]);
		if (mask)
			nonzero++;
		str[i * 2] = (mask & (1 << bit)) ? 'x' : ' ';
		str[i * 2 + 1] = ' ';
	}
	if (!nonzero && !dump_all) {
		str[0] = 0;
		return 0;
	}
	str[i * 2] = 0;
	return i * 2;
}

uint16_t mft[16][IB_MLIDS_IN_BLOCK] = { { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0}, { 0 }, { 0 } };

void dump_multicast_tables(ibnd_node_t * node, unsigned startlid,
			    unsigned endlid, struct ibmad_port * mad_port)
{
	ib_portid_t *portid = &node->path_portid;
	char nd[IB_SMP_DATA_SIZE] = { 0 };
	char str[512];
	char *s;
	uint64_t nodeguid;
	uint32_t mod;
	unsigned block, i, j, e, nports, cap, chunks, startblock, lastblock,
	    top;
	char *mapnd = NULL;
	int n = 0;

	memcpy(nd, node->nodedesc, strlen(node->nodedesc));
	nports = node->numports;
	nodeguid = node->guid;

	mad_decode_field(node->switchinfo, IB_SW_MCAST_FDB_CAP_F, &cap);
	mad_decode_field(node->switchinfo, IB_SW_MCAST_FDB_TOP_F, &top);

	if (!endlid || endlid > IB_MIN_MCAST_LID + cap - 1)
		endlid = IB_MIN_MCAST_LID + cap - 1;
	if (!dump_all && top && top < endlid) {
		if (top < IB_MIN_MCAST_LID - 1)
			IBWARN("illegal top mlid %x", top);
		else
			endlid = top;
	}

	if (!startlid)
		startlid = IB_MIN_MCAST_LID;
	else if (startlid < IB_MIN_MCAST_LID) {
		IBWARN("illegal start mlid %x, set to %x", startlid,
		       IB_MIN_MCAST_LID);
		startlid = IB_MIN_MCAST_LID;
	}

	if (endlid > IB_MAX_MCAST_LID) {
		IBWARN("illegal end mlid %x, truncate to %x", endlid,
		       IB_MAX_MCAST_LID);
		endlid = IB_MAX_MCAST_LID;
	}

	mapnd = remap_node_name(node_name_map, nodeguid, nd);

	printf("Multicast mlids [0x%x-0x%x] of switch %s guid 0x%016" PRIx64
	       " (%s):\n", startlid, endlid, portid2str(portid), nodeguid,
	       mapnd);

	if (brief)
		printf(" MLid       Port Mask\n");
	else {
		if (nports > 9) {
			for (i = 0, s = str; i <= nports; i++) {
				*s++ = (i % 10) ? ' ' : '0' + i / 10;
				*s++ = ' ';
			}
			*s = 0;
			printf("            %s\n", str);
		}
		for (i = 0, s = str; i <= nports; i++)
			s += sprintf(s, "%d ", i % 10);
		printf("     Ports: %s\n", str);
		printf(" MLid\n");
	}
	if (ibverbose)
		printf("Switch multicast mlid capability is %d top is 0x%x\n",
		       cap, top);

	chunks = ALIGN(nports + 1, 16) / 16;

	startblock = startlid / IB_MLIDS_IN_BLOCK;
	lastblock = endlid / IB_MLIDS_IN_BLOCK;
	for (block = startblock; block <= lastblock; block++) {
		for (j = 0; j < chunks; j++) {
			int status;
			mod = (block - IB_MIN_MCAST_LID / IB_MLIDS_IN_BLOCK)
			    | (j << 28);

			DEBUG("reading block %x chunk %d mod %x", block, j,
			      mod);
			if (!smp_query_status_via
			    (mft + j, portid, IB_ATTR_MULTICASTFORWTBL, mod, 0,
			     &status, mad_port)) {
				fprintf(stderr, "SubnGet(MFT) failed on switch "
						"'%s' %s Node GUID 0x%"PRIx64
						" SMA LID %d; MAD status 0x%x "
						"AM 0x%x\n",
						mapnd, portid2str(portid),
						node->guid, node->smalid,
						status, mod);
			}
		}

		i = block * IB_MLIDS_IN_BLOCK;
		e = i + IB_MLIDS_IN_BLOCK;
		if (i < startlid)
			i = startlid;
		if (e > endlid + 1)
			e = endlid + 1;

		for (; i < e; i++) {
			if (dump_mlid(str, sizeof str, i, nports, mft) == 0)
				continue;
			printf("0x%04x      %s\n", i, str);
			n++;
		}
	}

	printf("%d %smlids dumped \n", n, dump_all ? "" : "valid ");

	free(mapnd);
}

int dump_lid(char *str, int str_len, int lid, int valid,
		ibnd_fabric_t *fabric,
		int * last_port_lid, int * base_port_lid,
		uint64_t * portguid)
{
	char nd[IB_SMP_DATA_SIZE] = { 0 };

	ibnd_port_t *port = NULL;

	char ntype[50], sguid[30];
	uint64_t nodeguid;
	int baselid, lmc, type;
	char *mapnd = NULL;
	int rc;

	if (brief) {
		str[0] = 0;
		return 0;
	}

	if (lid <= *last_port_lid) {
		if (!valid)
			return snprintf(str, str_len,
					": (path #%d - illegal port)",
					lid - *base_port_lid);
		else if (!*portguid)
			return snprintf(str, str_len,
					": (path #%d out of %d)",
					lid - *base_port_lid + 1,
					*last_port_lid - *base_port_lid + 1);
		else {
			return snprintf(str, str_len,
					": (path #%d out of %d: portguid %s)",
					lid - *base_port_lid + 1,
					*last_port_lid - *base_port_lid + 1,
					mad_dump_val(IB_NODE_PORT_GUID_F, sguid,
						     sizeof sguid, portguid));
		}
	}

	if (!valid)
		return snprintf(str, str_len, ": (illegal port)");

	*portguid = 0;

	port = ibnd_find_port_lid(fabric, lid);
	if (!port) {
		return snprintf(str, str_len, ": (node info not available fabric scan)");
	}

	nodeguid = port->node->guid;
	*portguid = port->guid;
	type = port->node->type;

	baselid = port->base_lid;
	lmc = port->lmc;

	memcpy(nd, port->node->nodedesc, strlen(port->node->nodedesc));

	if (lmc > 0) {
		*base_port_lid = baselid;
		*last_port_lid = baselid + (1 << lmc) - 1;
	}

	mapnd = remap_node_name(node_name_map, nodeguid, nd);
 
	rc = snprintf(str, str_len, ": (%s portguid %s: '%s')",
		      mad_dump_val(IB_NODE_TYPE_F, ntype, sizeof ntype,
				   &type), mad_dump_val(IB_NODE_PORT_GUID_F,
							sguid, sizeof sguid,
							portguid),
		      mapnd);

	free(mapnd);
	return rc;
}

void dump_unicast_tables(ibnd_node_t * node, int startlid, int endlid,
			struct ibmad_port *mad_port, ibnd_fabric_t *fabric)
{
	ib_portid_t * portid = &node->path_portid;
	char lft[IB_SMP_DATA_SIZE] = { 0 };
	char nd[IB_SMP_DATA_SIZE] = { 0 };
	char str[200];
	uint64_t nodeguid;
	int block, i, e, top;
	unsigned nports;
	int n = 0, startblock, endblock;
	char *mapnd = NULL;
	int last_port_lid = 0, base_port_lid = 0;
	uint64_t portguid = 0;

	mad_decode_field(node->switchinfo, IB_SW_LINEAR_FDB_TOP_F, &top);
	nodeguid = node->guid;
	nports = node->numports;
	memcpy(nd, node->nodedesc, strlen(node->nodedesc));

	if (!endlid || endlid > top)
		endlid = top;

	if (endlid > IB_MAX_UCAST_LID) {
		IBWARN("illegal lft top %d, truncate to %d", endlid,
		       IB_MAX_UCAST_LID);
		endlid = IB_MAX_UCAST_LID;
	}

	mapnd = remap_node_name(node_name_map, nodeguid, nd);

	printf("Unicast lids [0x%x-0x%x] of switch %s guid 0x%016" PRIx64
	       " (%s):\n", startlid, endlid, portid2str(portid), nodeguid,
	       mapnd);

	DEBUG("Switch top is 0x%x\n", top);

	printf("  Lid  Out   Destination\n");
	printf("       Port     Info \n");
	startblock = startlid / IB_SMP_DATA_SIZE;
	endblock = ALIGN(endlid, IB_SMP_DATA_SIZE) / IB_SMP_DATA_SIZE;
	for (block = startblock; block < endblock; block++) {
		int status;
		DEBUG("reading block %d", block);
		if (!smp_query_status_via(lft, portid, IB_ATTR_LINEARFORWTBL, block,
				   0, &status, mad_port)) {
			fprintf(stderr, "SubnGet(LFT) failed on switch "
					"'%s' %s Node GUID 0x%"PRIx64
					" SMA LID %d; MAD status 0x%x AM 0x%x\n",
					mapnd, portid2str(portid),
					node->guid, node->smalid,
					status, block);
		}
		i = block * IB_SMP_DATA_SIZE;
		e = i + IB_SMP_DATA_SIZE;
		if (i < startlid)
			i = startlid;
		if (e > endlid + 1)
			e = endlid + 1;

		for (; i < e; i++) {
			unsigned outport = lft[i % IB_SMP_DATA_SIZE];
			unsigned valid = (outport <= nports);

			if (!valid && !dump_all)
				continue;
			dump_lid(str, sizeof str, i, valid, fabric,
				&last_port_lid, &base_port_lid, &portguid);
			printf("0x%04x %03u %s\n", i, outport & 0xff, str);
			n++;
		}
	}

	printf("%d %slids dumped \n", n, dump_all ? "" : "valid ");
	free(mapnd);
}

void dump_node(ibnd_node_t *node, struct ibmad_port *mad_port,
		ibnd_fabric_t *fabric)
{
	if (multicast)
		dump_multicast_tables(node, startlid, endlid, mad_port);
	else
		dump_unicast_tables(node, startlid, endlid,
						mad_port, fabric);
}

void process_switch(ibnd_node_t * node, void *fabric)
{
	dump_node(node, srcport, (ibnd_fabric_t *)fabric);
}

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'a':
		dump_all++;
		break;
	case 'M':
		multicast++;
		break;
	case 'n':
		brief++;
		break;
	case 1:
		node_name_map_file = strdup(optarg);
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int rc = 0;
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };

	struct ibnd_config config = { 0 };
	ibnd_fabric_t *fabric = NULL;

	const struct ibdiag_opt opts[] = {
		{"all", 'a', 0, NULL, "show all lids, even invalid entries"},
		{"no_dests", 'n', 0, NULL,
		 "do not try to resolve destinations"},
		{"Multicast", 'M', 0, NULL, "show multicast forwarding tables"},
		{"node-name-map", 1, 1, "<file>", "node name map file"},
		{0}
	};
	char usage_args[] = "[<dest dr_path|lid|guid> [<startlid> [<endlid>]]]";
	const char *usage_examples[] = {
		" -- Unicast examples:",
		"-a\t# same, but dump all lids, even with invalid out ports",
		"-n\t# simple dump format - no destination resolving",
		"10\t# dump lids starting from 10",
		"0x10 0x20\t# dump lid range",
		" -- Multicast examples:",
		"-M\t# dump all non empty mlids of switch with lid 4",
		"-M 0xc010 0xc020\t# same, but with range",
		"-M -n\t# simple dump format",
		NULL,
	};

	ibdiag_process_opts(argc, argv, &config, "KGDLs", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc > 0)
		startlid = strtoul(argv[0], 0, 0);
	if (argc > 1)
		endlid = strtoul(argv[1], 0, 0);

	node_name_map = open_node_name_map(node_name_map_file);

	if (ibd_timeout)
		config.timeout_ms = ibd_timeout;

	config.flags = ibd_ibnetdisc_flags;
	config.mkey = ibd_mkey;

	if ((fabric = ibnd_discover_fabric(ibd_ca, ibd_ca_port, NULL,
						&config)) != NULL) {

		srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
		if (!srcport) {
			fprintf(stderr,
				"Failed to open '%s' port '%d'\n", ibd_ca, ibd_ca_port);
			rc = -1;
			goto Exit;
		}
		smp_mkey_set(srcport, ibd_mkey);

		if (ibd_timeout) {
			mad_rpc_set_timeout(srcport, ibd_timeout);
		}

		ibnd_iter_nodes_type(fabric, process_switch, IB_NODE_SWITCH, fabric);

		mad_rpc_close_port(srcport);

	} else {
		fprintf(stderr, "Failed to discover fabric\n");
		rc = -1;
	}
Exit:
	ibnd_destroy_fabric(fabric);

	close_node_name_map(node_name_map);
	exit(rc);
}
