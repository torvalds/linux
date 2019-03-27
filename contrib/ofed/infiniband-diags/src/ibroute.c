/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009-2011 Mellanox Technologies LTD.  All rights reserved.
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

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <complib/cl_nodenamemap.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

static int brief, dump_all, multicast;

static char *node_name_map_file = NULL;
static nn_map_t *node_name_map = NULL;

/*******************************************/

char *check_switch(ib_portid_t * portid, unsigned int *nports, uint64_t * guid,
		   uint8_t * sw, char *nd)
{
	uint8_t ni[IB_SMP_DATA_SIZE] = { 0 };
	int type;

	DEBUG("checking node type");
	if (!smp_query_via(ni, portid, IB_ATTR_NODE_INFO, 0, 0, srcport)) {
		xdump(stderr, "nodeinfo\n", ni, sizeof ni);
		return "node info failed: valid addr?";
	}

	if (!smp_query_via(nd, portid, IB_ATTR_NODE_DESC, 0, 0, srcport))
		return "node desc failed";

	mad_decode_field(ni, IB_NODE_TYPE_F, &type);
	if (type != IB_NODE_SWITCH)
		return "not a switch";

	DEBUG("Gathering information about switch");
	mad_decode_field(ni, IB_NODE_NPORTS_F, nports);
	mad_decode_field(ni, IB_NODE_GUID_F, guid);

	if (!smp_query_via(sw, portid, IB_ATTR_SWITCH_INFO, 0, 0, srcport))
		return "switch info failed: is a switch node?";

	return 0;
}

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

char *dump_multicast_tables(ib_portid_t * portid, unsigned startlid,
			    unsigned endlid)
{
	char nd[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t sw[IB_SMP_DATA_SIZE] = { 0 };
	char str[512];
	char *s;
	uint64_t nodeguid;
	uint32_t mod;
	unsigned block, i, j, e, nports, cap, chunks, startblock, lastblock,
	    top;
	char *mapnd = NULL;
	int n = 0;

	if ((s = check_switch(portid, &nports, &nodeguid, sw, nd)))
		return s;

	mad_decode_field(sw, IB_SW_MCAST_FDB_CAP_F, &cap);
	mad_decode_field(sw, IB_SW_MCAST_FDB_TOP_F, &top);

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
			     &status, srcport)) {
				fprintf(stderr, "SubnGet() failed"
						"; MAD status 0x%x AM 0x%x\n",
						status, mod);
				return NULL;
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
	return 0;
}

int dump_lid(char *str, int strlen, int lid, int valid)
{
	char nd[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t ni[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t pi[IB_SMP_DATA_SIZE] = { 0 };
	ib_portid_t lidport = { 0 };
	static int last_port_lid, base_port_lid;
	char ntype[50], sguid[30];
	static uint64_t portguid;
	uint64_t nodeguid;
	int baselid, lmc, type;
	char *mapnd = NULL;
	int rc;

	if (brief) {
		str[0] = 0;
		return 0;
	}

	if (lid <= last_port_lid) {
		if (!valid)
			return snprintf(str, strlen,
					": (path #%d - illegal port)",
					lid - base_port_lid);
		else if (!portguid)
			return snprintf(str, strlen,
					": (path #%d out of %d)",
					lid - base_port_lid + 1,
					last_port_lid - base_port_lid + 1);
		else {
			return snprintf(str, strlen,
					": (path #%d out of %d: portguid %s)",
					lid - base_port_lid + 1,
					last_port_lid - base_port_lid + 1,
					mad_dump_val(IB_NODE_PORT_GUID_F, sguid,
						     sizeof sguid, &portguid));
		}
	}

	if (!valid)
		return snprintf(str, strlen, ": (illegal port)");

	portguid = 0;
	lidport.lid = lid;

	if (!smp_query_via(nd, &lidport, IB_ATTR_NODE_DESC, 0, 100, srcport) ||
	    !smp_query_via(pi, &lidport, IB_ATTR_PORT_INFO, 0, 100, srcport) ||
	    !smp_query_via(ni, &lidport, IB_ATTR_NODE_INFO, 0, 100, srcport))
		return snprintf(str, strlen, ": (unknown node and type)");

	mad_decode_field(ni, IB_NODE_GUID_F, &nodeguid);
	mad_decode_field(ni, IB_NODE_PORT_GUID_F, &portguid);
	mad_decode_field(ni, IB_NODE_TYPE_F, &type);

	mad_decode_field(pi, IB_PORT_LID_F, &baselid);
	mad_decode_field(pi, IB_PORT_LMC_F, &lmc);

	if (lmc > 0) {
		base_port_lid = baselid;
		last_port_lid = baselid + (1 << lmc) - 1;
	}

	mapnd = remap_node_name(node_name_map, nodeguid, nd);
 
	rc = snprintf(str, strlen, ": (%s portguid %s: '%s')",
		      mad_dump_val(IB_NODE_TYPE_F, ntype, sizeof ntype,
				   &type), mad_dump_val(IB_NODE_PORT_GUID_F,
							sguid, sizeof sguid,
							&portguid),
		      mapnd);

	free(mapnd);
	return rc;
}

char *dump_unicast_tables(ib_portid_t * portid, int startlid, int endlid)
{
	char lft[IB_SMP_DATA_SIZE] = { 0 };
	char nd[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t sw[IB_SMP_DATA_SIZE] = { 0 };
	char str[200], *s;
	uint64_t nodeguid;
	int block, i, e, top;
	unsigned nports;
	int n = 0, startblock, endblock;
	char *mapnd = NULL;

	if ((s = check_switch(portid, &nports, &nodeguid, sw, nd)))
		return s;

	mad_decode_field(sw, IB_SW_LINEAR_FDB_TOP_F, &top);

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
				   0, &status, srcport)) {
			fprintf(stderr, "SubnGet() failed"
					"; MAD status 0x%x AM 0x%x\n",
					status, block);
			return NULL;
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
			dump_lid(str, sizeof str, i, valid);
			printf("0x%04x %03u %s\n", i, outport & 0xff, str);
			n++;
		}
	}

	printf("%d %slids dumped \n", n, dump_all ? "" : "valid ");
	free(mapnd);
	return 0;
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
	int mgmt_classes[3] =
	    { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS };
	ib_portid_t portid = { 0 };
	unsigned startlid = 0, endlid = 0;
	char *err;

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
		"4\t# dump all lids with valid out ports of switch with lid 4",
		"-a 4\t# same, but dump all lids, even with invalid out ports",
		"-n 4\t# simple dump format - no destination resolving",
		"4 10\t# dump lids starting from 10",
		"4 0x10 0x20\t# dump lid range",
		"-G 0x08f1040023\t# resolve switch by GUID",
		"-D 0,1\t# resolve switch by direct path",
		" -- Multicast examples:",
		"-M 4\t# dump all non empty mlids of switch with lid 4",
		"-M 4 0xc010 0xc020\t# same, but with range",
		"-M -n 4\t# simple dump format",
		NULL,
	};

	ibdiag_process_opts(argc, argv, NULL, "K", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (!argc)
		ibdiag_show_usage();

	if (argc > 1)
		startlid = strtoul(argv[1], 0, 0);
	if (argc > 2)
		endlid = strtoul(argv[2], 0, 0);

	node_name_map = open_node_name_map(node_name_map_file);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[0],
			       ibd_dest_type, ibd_sm_id, srcport) < 0)
		IBEXIT("can't resolve destination port %s", argv[0]);

	if (multicast)
		err = dump_multicast_tables(&portid, startlid, endlid);
	else
		err = dump_unicast_tables(&portid, startlid, endlid);

	if (err)
		IBEXIT("dump tables: %s", err);

	mad_rpc_close_port(srcport);
	close_node_name_map(node_name_map);
	exit(0);
}
