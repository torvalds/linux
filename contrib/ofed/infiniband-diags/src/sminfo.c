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
#include <inttypes.h>
#include <getopt.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

static uint8_t sminfo[1024] = { 0 };

struct ibmad_port *srcport;

int strdata, xdata = 1, bindata;

enum {
	SMINFO_NOTACT,
	SMINFO_DISCOVER,
	SMINFO_STANDBY,
	SMINFO_MASTER,

	SMINFO_STATE_LAST,
};

char *statestr[] = {
	"SMINFO_NOTACT",
	"SMINFO_DISCOVER",
	"SMINFO_STANDBY",
	"SMINFO_MASTER",
};

#define STATESTR(s)	(((unsigned)(s)) < SMINFO_STATE_LAST ? statestr[s] : "???")

static unsigned act;
static int prio, state = SMINFO_STANDBY;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'a':
		act = strtoul(optarg, 0, 0);
		break;
	case 's':
		state = strtoul(optarg, 0, 0);
		break;
	case 'p':
		prio = strtoul(optarg, 0, 0);
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
	int mod = 0;
	ib_portid_t portid = { 0 };
	uint8_t *p;
	uint64_t guid = 0, key = 0;

	const struct ibdiag_opt opts[] = {
		{"state", 's', 1, "<0-3>", "set SM state"},
		{"priority", 'p', 1, "<0-15>", "set SM priority"},
		{"activity", 'a', 1, NULL, "set activity count"},
		{0}
	};
	char usage_args[] = "<sm_lid|sm_dr_path> [modifier]";

	ibdiag_process_opts(argc, argv, NULL, "sK", opts, process_opt,
			    usage_args, NULL);

	argc -= optind;
	argv += optind;

	if (argc > 1)
		mod = atoi(argv[1]);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	if (argc) {
		if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[0],
				       ibd_dest_type, 0, srcport) < 0)
			IBEXIT("can't resolve destination port %s", argv[0]);
	} else {
		if (resolve_sm_portid(ibd_ca, ibd_ca_port, &portid) < 0)
			IBEXIT("can't resolve sm port %s", argv[0]);
	}

	mad_encode_field(sminfo, IB_SMINFO_GUID_F, &guid);
	mad_encode_field(sminfo, IB_SMINFO_ACT_F, &act);
	mad_encode_field(sminfo, IB_SMINFO_KEY_F, &key);
	mad_encode_field(sminfo, IB_SMINFO_PRIO_F, &prio);
	mad_encode_field(sminfo, IB_SMINFO_STATE_F, &state);

	if (mod) {
		if (!(p = smp_set_via(sminfo, &portid, IB_ATTR_SMINFO, mod,
				      ibd_timeout, srcport)))
			IBEXIT("query");
	} else if (!(p = smp_query_via(sminfo, &portid, IB_ATTR_SMINFO, 0,
				       ibd_timeout, srcport)))
		IBEXIT("query");

	mad_decode_field(sminfo, IB_SMINFO_GUID_F, &guid);
	mad_decode_field(sminfo, IB_SMINFO_ACT_F, &act);
	mad_decode_field(sminfo, IB_SMINFO_KEY_F, &key);
	mad_decode_field(sminfo, IB_SMINFO_PRIO_F, &prio);
	mad_decode_field(sminfo, IB_SMINFO_STATE_F, &state);

	printf("sminfo: sm lid %d sm guid 0x%" PRIx64
	       ", activity count %u priority %d state %d %s\n", portid.lid,
	       guid, act, prio, state, STATESTR(state));

	mad_rpc_close_port(srcport);
	exit(0);
}
