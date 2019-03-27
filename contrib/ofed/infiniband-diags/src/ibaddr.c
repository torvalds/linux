/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
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
#include <getopt.h>
#include <arpa/inet.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

static int ib_resolve_addr(ib_portid_t * portid, int portnum, int show_lid,
			   int show_gid)
{
	char gid_str[INET6_ADDRSTRLEN];
	uint8_t portinfo[IB_SMP_DATA_SIZE] = { 0 };
	uint8_t nodeinfo[IB_SMP_DATA_SIZE] = { 0 };
	uint64_t guid, prefix;
	ibmad_gid_t gid;
	int lmc;

	if (!smp_query_via(nodeinfo, portid, IB_ATTR_NODE_INFO, 0, 0, srcport))
		return -1;

	if (!smp_query_via(portinfo, portid, IB_ATTR_PORT_INFO, portnum, 0,
			   srcport))
		return -1;

	mad_decode_field(portinfo, IB_PORT_LID_F, &portid->lid);
	mad_decode_field(portinfo, IB_PORT_GID_PREFIX_F, &prefix);
	mad_decode_field(portinfo, IB_PORT_LMC_F, &lmc);
	mad_decode_field(nodeinfo, IB_NODE_PORT_GUID_F, &guid);

	mad_encode_field(gid, IB_GID_PREFIX_F, &prefix);
	mad_encode_field(gid, IB_GID_GUID_F, &guid);

	if (show_gid) {
		printf("GID %s ", inet_ntop(AF_INET6, gid, gid_str,
					    sizeof gid_str));
	}

	if (show_lid > 0)
		printf("LID start 0x%x end 0x%x", portid->lid,
		       portid->lid + (1 << lmc) - 1);
	else if (show_lid < 0)
		printf("LID start %u end %u", portid->lid,
		       portid->lid + (1 << lmc) - 1);
	printf("\n");
	return 0;
}

static int show_lid, show_gid;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'g':
		show_gid = 1;
		break;
	case 'l':
		show_lid++;
		break;
	case 'L':
		show_lid = -100;
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
	int port = 0;

	const struct ibdiag_opt opts[] = {
		{"gid_show", 'g', 0, NULL, "show gid address only"},
		{"lid_show", 'l', 0, NULL, "show lid range only"},
		{"Lid_show", 'L', 0, NULL, "show lid range (in decimal) only"},
		{0}
	};
	char usage_args[] = "[<lid|dr_path|guid>]";
	const char *usage_examples[] = {
		"\t\t# local port's address",
		"32\t\t# show lid range and gid of lid 32",
		"-G 0x8f1040023\t# same but using guid address",
		"-l 32\t\t# show lid range only",
		"-L 32\t\t# show decimal lid range only",
		"-g 32\t\t# show gid address only",
		NULL
	};

	ibdiag_process_opts(argc, argv, NULL, "KL", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc > 1)
		port = strtoul(argv[1], 0, 0);

	if (!show_lid && !show_gid)
		show_lid = show_gid = 1;

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	if (argc) {
		if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[0],
				       ibd_dest_type, ibd_sm_id, srcport) < 0)
			IBEXIT("can't resolve destination port %s", argv[0]);
	} else {
		if (resolve_self(ibd_ca, ibd_ca_port, &portid, &port, NULL) < 0)
			IBEXIT("can't resolve self port %s", argv[0]);
	}

	if (ib_resolve_addr(&portid, port, show_lid, show_gid) < 0)
		IBEXIT("can't resolve requested address");

	mad_rpc_close_port(srcport);
	exit(0);
}
