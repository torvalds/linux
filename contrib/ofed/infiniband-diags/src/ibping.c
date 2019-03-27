/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
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
#include <signal.h>
#include <getopt.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include <complib/cl_timer.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

static char host_and_domain[IB_VENDOR_RANGE2_DATA_SIZE];
static char last_host[IB_VENDOR_RANGE2_DATA_SIZE];
static ibmad_gid_t dgid;
static int with_grh;

static void get_host_and_domain(char *data, int sz)
{
	char *s = data;
	int n;

	if (gethostname(s, sz) < 0)
		snprintf(s, sz, "?hostname?");

	s[sz - 1] = 0;
	if ((n = strlen(s)) >= sz)
		return;
	s[n] = '.';
	s += n + 1;
	sz -= n + 1;

	if (getdomainname(s, sz) < 0)
		snprintf(s, sz, "?domainname?");
	if (strlen(s) == 0)
		s[-1] = 0;	/* no domain */
}

static char *ibping_serv(void)
{
	void *umad;
	void *mad;
	char *data;

	DEBUG("starting to serve...");

	while ((umad = mad_receive_via(0, -1, srcport))) {

		if (umad_status(umad) == 0) {
			mad = umad_get_mad(umad);
			data = (char *)mad + IB_VENDOR_RANGE2_DATA_OFFS;

			memcpy(data, host_and_domain, IB_VENDOR_RANGE2_DATA_SIZE);

			DEBUG("Pong: %s", data);

			if (mad_respond_via(umad, 0, 0, srcport) < 0)
				DEBUG("respond failed");

		}
		mad_free(umad);
	}

	DEBUG("server out");
	return 0;
}

static int oui = IB_OPENIB_OUI;

static uint64_t ibping(ib_portid_t * portid, int quiet)
{
	char data[IB_VENDOR_RANGE2_DATA_SIZE] = { 0 };
	ib_vendor_call_t call;
	uint64_t start, rtt;

	DEBUG("Ping..");

	start = cl_get_time_stamp();

	call.method = IB_MAD_METHOD_GET;
	call.mgmt_class = IB_VENDOR_OPENIB_PING_CLASS;
	call.attrid = 0;
	call.mod = 0;
	call.oui = oui;
	call.timeout = 0;
	memset(&call.rmpp, 0, sizeof call.rmpp);

	if (!ib_vendor_call_via(data, portid, &call, srcport))
		return ~0ull;

	rtt = cl_get_time_stamp() - start;

	if (!last_host[0])
		memcpy(last_host, data, sizeof last_host);

	if (!quiet)
		printf("Pong from %s (%s): time %" PRIu64 ".%03" PRIu64 " ms\n",
		       data, portid2str(portid), rtt / 1000, rtt % 1000);

	return rtt;
}

static uint64_t minrtt = ~0ull, maxrtt, total_rtt;
static uint64_t start, total_time, replied, lost, ntrans;
static ib_portid_t portid = { 0 };

void report(int sig)
{
	total_time = cl_get_time_stamp() - start;

	DEBUG("out due signal %d", sig);

	printf("\n--- %s (%s) ibping statistics ---\n", last_host,
	       portid2str(&portid));
	printf("%" PRIu64 " packets transmitted, %" PRIu64 " received, %" PRIu64
	       "%% packet loss, time %" PRIu64 " ms\n", ntrans, replied,
	       (lost != 0) ? lost * 100 / ntrans : 0, total_time / 1000);
	printf("rtt min/avg/max = %" PRIu64 ".%03" PRIu64 "/%" PRIu64 ".%03"
	       PRIu64 "/%" PRIu64 ".%03" PRIu64 " ms\n",
	       minrtt == ~0ull ? 0 : minrtt / 1000,
	       minrtt == ~0ull ? 0 : minrtt % 1000,
	       replied ? total_rtt / replied / 1000 : 0,
	       replied ? (total_rtt / replied) % 1000 : 0, maxrtt / 1000,
	       maxrtt % 1000);

	exit(0);
}

static int server = 0, flood = 0;
static unsigned count = ~0;

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'c':
		count = strtoul(optarg, 0, 0);
		break;
	case 'f':
		flood++;
		break;
	case 'o':
		oui = strtoul(optarg, 0, 0);
		break;
	case 'S':
		server++;
		break;
	case 25:
		if (!inet_pton(AF_INET6, optarg, &dgid)) {
			printf("dgid format is wrong!\n");
			ibdiag_show_usage();
			return 1;
		}
		with_grh = 1;
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int mgmt_classes[1] = { IB_SA_CLASS };
	int ping_class = IB_VENDOR_OPENIB_PING_CLASS;
	uint64_t rtt;
	char *err;

	const struct ibdiag_opt opts[] = {
		{"count", 'c', 1, "<num>", "stop after count packets"},
		{"flood", 'f', 0, NULL, "flood destination"},
		{"oui", 'o', 1, NULL, "use specified OUI number"},
		{"Server", 'S', 0, NULL, "start in server mode"},
		{"dgid", 25, 1, NULL, "remote gid (IPv6 format)"},
		{0}
	};
	char usage_args[] = "<dest lid|guid>";

	ibdiag_process_opts(argc, argv, NULL, "DKy", opts, process_opt,
			    usage_args, NULL);

	argc -= optind;
	argv += optind;

	if (!argc && !server)
		ibdiag_show_usage();

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 1);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	if (server) {
		if (mad_register_server_via(ping_class, 0, 0, oui, srcport) < 0)
			IBEXIT("can't serve class %d on this port",
				ping_class);

		get_host_and_domain(host_and_domain, sizeof host_and_domain);

		if ((err = ibping_serv()))
			IBEXIT("ibping to %s: %s", portid2str(&portid), err);
		exit(0);
	}

	if (mad_register_client_via(ping_class, 0, srcport) < 0)
		IBEXIT("can't register ping class %d on this port",
			ping_class);

	if (with_grh && ibd_dest_type != IB_DEST_LID)
		IBEXIT("When using GRH, LID should be provided");
	if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[0],
			       ibd_dest_type, ibd_sm_id, srcport) < 0)
		IBEXIT("can't resolve destination port %s", argv[0]);

	if (with_grh) {
		portid.grh_present = 1;
		memcpy(&portid.gid, &dgid, sizeof(portid.gid));
	}

	signal(SIGINT, report);
	signal(SIGTERM, report);

	start = cl_get_time_stamp();

	while (count-- > 0) {
		ntrans++;
		if ((rtt = ibping(&portid, flood)) == ~0ull) {
			DEBUG("ibping to %s failed", portid2str(&portid));
			lost++;
		} else {
			if (rtt < minrtt)
				minrtt = rtt;
			if (rtt > maxrtt)
				maxrtt = rtt;
			total_rtt += rtt;
			replied++;
		}

		if (!flood)
			sleep(1);
	}

	report(0);

	mad_rpc_close_port(srcport);

	exit(-1);
}
