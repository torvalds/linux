/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
 * Copyright (c) 2011 Lawrence Livermore National Lab.  All rights reserved.
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
#include <getopt.h>
#include <netinet/in.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <infiniband/mad.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

static ibmad_gid_t dgid;
static int with_grh;

static op_fn_t class_port_info;
static op_fn_t congestion_info;
static op_fn_t congestion_key_info;
static op_fn_t congestion_log;
static op_fn_t switch_congestion_setting;
static op_fn_t switch_port_congestion_setting;
static op_fn_t ca_congestion_setting;
static op_fn_t congestion_control_table;
static op_fn_t timestamp_dump;

static const match_rec_t match_tbl[] = {
	{"ClassPortInfo", "CP", class_port_info, 0, ""},
	{"CongestionInfo", "CI", congestion_info, 0, ""},
	{"CongestionKeyInfo", "CK", congestion_key_info, 0, ""},
	{"CongestionLog", "CL", congestion_log, 0, ""},
	{"SwitchCongestionSetting", "SS", switch_congestion_setting, 0, ""},
	{"SwitchPortCongestionSetting", "SP", switch_port_congestion_setting, 1, ""},
	{"CACongestionSetting", "CS", ca_congestion_setting, 0, ""},
	{"CongestionControlTable", "CT", congestion_control_table, 0, ""},
	{"Timestamp", "TI", timestamp_dump, 0, ""},
	{0}
};

uint64_t cckey = 0;

/*******************************************/
static char *class_port_info(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };

	if (!cc_query_status_via(data, dest, CLASS_PORT_INFO,
				 0, 0, NULL, srcport, cckey))
		return "class port info query failed";

	mad_dump_classportinfo(buf, sizeof buf, data, sizeof data);

	printf("# ClassPortInfo: %s\n%s", portid2str(dest), buf);
	return NULL;
}

static char *congestion_info(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };

	if (!cc_query_status_via(data, dest, IB_CC_ATTR_CONGESTION_INFO,
				 0, 0, NULL, srcport, cckey))
		return "congestion info query failed";

	mad_dump_cc_congestioninfo(buf, sizeof buf, data, sizeof data);

	printf("# CongestionInfo: %s\n%s", portid2str(dest), buf);
	return NULL;
}

static char *congestion_key_info(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };
	
	if (!cc_query_status_via(data, dest, IB_CC_ATTR_CONGESTION_KEY_INFO,
				 0, 0, NULL, srcport, cckey))
		return "congestion key info query failed";

	mad_dump_cc_congestionkeyinfo(buf, sizeof buf, data, sizeof data);

	printf("# CongestionKeyInfo: %s\n%s", portid2str(dest), buf);
	return NULL;
}

static char *congestion_log(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_LOG_DATA_SZ] = { 0 };
	char emptybuf[16] = { 0 };
	int i, type;

	if (!cc_query_status_via(data, dest, IB_CC_ATTR_CONGESTION_LOG,
				 0, 0, NULL, srcport, cckey))
		return "congestion log query failed";

	mad_decode_field((uint8_t *)data, IB_CC_CONGESTION_LOG_LOGTYPE_F, &type);

	if (type != 1 && type != 2)
		return "unrecognized log type";

	mad_dump_cc_congestionlog(buf, sizeof buf, data, sizeof data);

	printf("# CongestionLog: %s\n%s", portid2str(dest), buf);

	if (type == 1) {
		mad_dump_cc_congestionlogswitch(buf, sizeof buf, data, sizeof data);
		printf("%s\n", buf);
		for (i = 0; i < 15; i++) {
			/* output only if entry not 0 */
			if (memcmp(data + 40 + i * 12, emptybuf, 12)) {
				mad_dump_cc_congestionlogentryswitch(buf, sizeof buf,
								     data + 40 + i * 12,
								     12);
				printf("%s\n", buf);
			}
		}
	}
	else {
		/* XXX: Q3/2010 errata lists first entry offset at 80, but we assume
		 * will be updated to 96 once CurrentTimeStamp field is word aligned.
		 * In addition, assume max 13 log events instead of 16.  Due to 
		 * errata changes increasing size of CA log event, 16 log events is
		 * no longer possible to fit in max MAD size.
		 */
		mad_dump_cc_congestionlogca(buf, sizeof buf, data, sizeof data);
		printf("%s\n", buf);
		for (i = 0; i < 13; i++) {
			/* output only if entry not 0 */
			if (memcmp(data + 12 + i * 16, emptybuf, 16)) {
				mad_dump_cc_congestionlogentryca(buf, sizeof buf,
								 data + 12 + i * 16,
								 16);
				printf("%s\n", buf);
			}
		}
	}

	return NULL;
}

static char *switch_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };
	
	if (!cc_query_status_via(data, dest, IB_CC_ATTR_SWITCH_CONGESTION_SETTING,
				 0, 0, NULL, srcport, cckey))
		return "switch congestion setting query failed";

	mad_dump_cc_switchcongestionsetting(buf, sizeof buf, data, sizeof data);

	printf("# SwitchCongestionSetting: %s\n%s", portid2str(dest), buf);
	return NULL;
}

static char *switch_port_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };
	int type, numports, maxblocks, i, j;
	int portnum = 0;
	int outputcount = 0;

	if (argc > 0)
		portnum = strtol(argv[0], 0, 0);

	/* Figure out number of ports first */
	if (!smp_query_via(data, dest, IB_ATTR_NODE_INFO, 0, 0, srcport))
		return "node info query failed";

	mad_decode_field((uint8_t *)data, IB_NODE_TYPE_F, &type);
	mad_decode_field((uint8_t *)data, IB_NODE_NPORTS_F, &numports);

	if (type != IB_NODE_SWITCH)
		return "destination not a switch";

	printf("# SwitchPortCongestionSetting: %s\n", portid2str(dest));

	if (portnum) {
		if (portnum > numports)
			return "invalid port number specified";

		memset(data, '\0', sizeof data);
		if (!cc_query_status_via(data, dest, IB_CC_ATTR_SWITCH_PORT_CONGESTION_SETTING,
					 portnum / 32, 0, NULL, srcport, cckey))
			return "switch port congestion setting query failed";

		mad_dump_cc_switchportcongestionsettingelement(buf, sizeof buf,
							       data + ((portnum % 32) * 4),
							       4);
		printf("%s", buf);
		return NULL;
	}

	/* else get all port info */

	maxblocks = numports / 32 + 1;

	for (i = 0; i < maxblocks; i++) {
		memset(data, '\0', sizeof data);
		if (!cc_query_status_via(data, dest, IB_CC_ATTR_SWITCH_PORT_CONGESTION_SETTING,
					 i, 0, NULL, srcport, cckey))
			return "switch port congestion setting query failed";

		for (j = 0; j < 32 && outputcount <= numports; j++) {
			printf("Port:............................%u\n", i * 32 + j);
			mad_dump_cc_switchportcongestionsettingelement(buf, sizeof buf,
								       data + j * 4,
								       4);
			printf("%s\n", buf);
			outputcount++;
		}
	}

	return NULL;
}

static char *ca_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };
	int i;
	
	if (!cc_query_status_via(data, dest, IB_CC_ATTR_CA_CONGESTION_SETTING,
				 0, 0, NULL, srcport, cckey))
		return "ca congestion setting query failed";

	mad_dump_cc_cacongestionsetting(buf, sizeof buf, data, sizeof data);

	printf("# CACongestionSetting: %s\n%s\n", portid2str(dest), buf);

	for (i = 0; i < 16; i++) {
		printf("SL:..............................%u\n", i);
		mad_dump_cc_cacongestionentry(buf, sizeof buf,
					      data + 4 + i * 8,
					      8);
		printf("%s\n", buf);
	}
	return NULL;
}

static char *congestion_control_table(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };
	int limit, outputcount = 0;
	int i, j;
	
	if (!cc_query_status_via(data, dest, IB_CC_ATTR_CONGESTION_CONTROL_TABLE,
				 0, 0, NULL, srcport, cckey))
		return "congestion control table query failed";

	mad_decode_field((uint8_t *)data, IB_CC_CONGESTION_CONTROL_TABLE_CCTI_LIMIT_F, &limit);

	mad_dump_cc_congestioncontroltable(buf, sizeof buf, data, sizeof data);

	printf("# CongestionControlTable: %s\n%s\n", portid2str(dest), buf);

	if (!limit)
		return NULL;

	for (i = 0; i < (limit/64) + 1; i++) {

		/* first query done */
		if (i)
			if (!cc_query_status_via(data, dest, IB_CC_ATTR_CONGESTION_CONTROL_TABLE,
					  i, 0, NULL, srcport, cckey))
				return "congestion control table query failed";

		for (j = 0; j < 64 && outputcount <= limit; j++) {
			printf("Entry:...........................%u\n", i*64 + j);
			mad_dump_cc_congestioncontroltableentry(buf, sizeof buf,
								data + 4 + j * 2,
								sizeof data - 4 - j * 2);
			printf("%s\n", buf);
			outputcount++;
		}
	}
	return NULL;
}

static char *timestamp_dump(ib_portid_t * dest, char **argv, int argc)
{
	char buf[2048];
	char data[IB_CC_DATA_SZ] = { 0 };

	if (!cc_query_status_via(data, dest, IB_CC_ATTR_TIMESTAMP,
				 0, 0, NULL, srcport, cckey))
		return "timestamp query failed";

	mad_dump_cc_timestamp(buf, sizeof buf, data, sizeof data);

	printf("# Timestamp: %s\n%s", portid2str(dest), buf);
	return NULL;
}

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'c':
		cckey = (uint64_t) strtoull(optarg, 0, 0);
		break;
	case 25:
		if (!inet_pton(AF_INET6, optarg, &dgid)) {
			fprintf(stderr, "dgid format is wrong!\n");
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
	char usage_args[1024];
	int mgmt_classes[3] = { IB_SMI_CLASS, IB_SA_CLASS, IB_CC_CLASS };
	ib_portid_t portid = { 0 };
	char *err;
	op_fn_t *fn;
	const match_rec_t *r;
	int n;

	const struct ibdiag_opt opts[] = {
		{"cckey", 'c', 1, "<key>", "CC key"},
		{"dgid", 25, 1, NULL, "remote gid (IPv6 format)"},
		{0}
	};
	const char *usage_examples[] = {
		"CongestionInfo 3\t\t\t# Congestion Info by lid",
		"SwitchPortCongestionSetting 3\t# Query all Switch Port Congestion Settings",
		"SwitchPortCongestionSetting 3 1\t# Query Switch Port Congestion Setting for port 1",
		NULL
	};

	n = sprintf(usage_args, "[-c key] <op> <lid|guid>\n"
		    "\nSupported ops (and aliases, case insensitive):\n");
	for (r = match_tbl; r->name; r++) {
		n += snprintf(usage_args + n, sizeof(usage_args) - n,
			      "  %s (%s) <lid|guid>%s\n", r->name,
			      r->alias ? r->alias : "",
			      r->opt_portnum ? " [<portnum>]" : "");
		if (n >= sizeof(usage_args))
			exit(-1);
	}

	ibdiag_process_opts(argc, argv, NULL, "DK", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc < 2)
		ibdiag_show_usage();

	if (!(fn = match_op(match_tbl, argv[0])))
		IBEXIT("operation '%s' not supported", argv[0]);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	smp_mkey_set(srcport, ibd_mkey);

	if (with_grh && ibd_dest_type != IB_DEST_LID)
		IBEXIT("When using GRH, LID should be provided");
	if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[1],
			       ibd_dest_type, ibd_sm_id, srcport) < 0)
		IBEXIT("can't resolve destination %s", argv[1]);
	if (with_grh) {
		portid.grh_present = 1;
		memcpy(&portid.gid, &dgid, sizeof(portid.gid));
	}
	if ((err = fn(&portid, argv + 2, argc - 2)))
		IBEXIT("operation %s: %s", argv[0], err);

	mad_rpc_close_port(srcport);
	exit(0);
}
