/*
 * Copyright (c) 2012 Mellanox Technologies LTD.  All rights reserved.
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
#include <getopt.h>
#include <netinet/in.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

#define IS3_DEVICE_ID			47396

#define IB_MLX_VENDOR_CLASS		10
/* Vendor specific Attribute IDs */
#define IB_MLX_IS3_GENERAL_INFO		0x17
#define IB_MLX_IS3_CONFIG_SPACE_ACCESS	0x50
#define IB_MLX_IS4_COUNTER_GROUP_INFO   0x90
#define IB_MLX_IS4_CONFIG_COUNTER_GROUP 0x91
/* Config space addresses */
#define IB_MLX_IS3_PORT_XMIT_WAIT	0x10013C


struct ibmad_port *srcport;

static ibmad_gid_t dgid;
static int with_grh;

typedef struct {
	uint16_t hw_revision;
	uint16_t device_id;
	uint8_t reserved[24];
	uint32_t uptime;
} is3_hw_info_t;

typedef struct {
	uint8_t resv1;
	uint8_t major;
	uint8_t minor;
	uint8_t sub_minor;
	uint32_t build_id;
	uint8_t month;
	uint8_t day;
	uint16_t year;
	uint16_t resv2;
	uint16_t hour;
	uint8_t psid[16];
	uint32_t ini_file_version;
} is3_fw_info_t;

typedef struct {
	uint32_t ext_major;
	uint32_t ext_minor;
	uint32_t ext_sub_minor;
	uint32_t reserved[4];
} is4_fw_ext_info_t;

typedef struct {
	uint8_t resv1;
	uint8_t major;
	uint8_t minor;
	uint8_t sub_minor;
	uint8_t resv2[28];
} is3_sw_info_t;

typedef struct {
	uint8_t reserved[8];
	is3_hw_info_t hw_info;
	is3_fw_info_t fw_info;
	is3_sw_info_t sw_info;
} is3_general_info_t;

typedef struct {
	uint8_t reserved[8];
	is3_hw_info_t hw_info;
	is3_fw_info_t fw_info;
	is4_fw_ext_info_t ext_fw_info;
	is3_sw_info_t sw_info;
} is4_general_info_t;

typedef struct {
	uint8_t reserved[8];
	struct is3_record {
		uint32_t address;
		uint32_t data;
		uint32_t mask;
	} record[18];
} is3_config_space_t;

#define COUNTER_GROUPS_NUM 2

typedef struct {
	uint8_t reserved1[8];
	uint8_t reserved[3];
	uint8_t num_of_counter_groups;
	uint32_t group_masks[COUNTER_GROUPS_NUM];
} is4_counter_group_info_t;

typedef struct {
	uint8_t reserved[3];
	uint8_t group_select;
} is4_group_select_t;

typedef struct {
	uint8_t reserved1[8];
	uint8_t reserved[4];
	is4_group_select_t group_selects[COUNTER_GROUPS_NUM];
} is4_config_counter_groups_t;

static uint16_t ext_fw_info_device[][2] = {
	{0x0245, 0x0245},	/* Switch-X */
	{0xc738, 0xc73b},	/* Switch-X */
	{0xcb20, 0xcb20},	/* Switch-IB */
	{0xcf08, 0xcf08},	/* Switch-IB2*/
	{0x01b3, 0x01b3},	/* IS-4 */
	{0x1003, 0x1017},	/* Connect-X */
	{0x1b02, 0x1b02},	/* Bull SwitchX */
	{0x1b50, 0x1b50},	/* Bull SwitchX */
	{0x1ba0, 0x1ba0},	/* Bull SwitchIB */
	{0x1bd0, 0x1bd5},	/* Bull SwitchIB and SwitchIB2 */
	{0x1b33, 0x1b33},	/* Bull ConnectX3 */
	{0x1b73, 0x1b73},	/* Bull ConnectX3 */
	{0x1b40, 0x1b41},	/* Bull ConnectX3 */
	{0x1b60, 0x1b61},	/* Bull ConnectX3 */
	{0x1b83, 0x1b83},	/* Bull ConnectIB */
	{0x1b93, 0x1b94},	/* Bull ConnectIB */
	{0x1bb4, 0x1bb5},	/* Bull ConnectX4 */
	{0x1bc4, 0x1bc4},	/* Bull ConnectX4 */
	{0x0000, 0x0000}};

static int is_ext_fw_info_supported(uint16_t device_id) {
	int i;
	for (i = 0; ext_fw_info_device[i][0]; i++)
		if (ext_fw_info_device[i][0] <= device_id &&
		    device_id <= ext_fw_info_device[i][1])
			return 1;
	return 0;
}

static int do_vendor(ib_portid_t *portid, struct ibmad_port *srcport,
		     uint8_t class, uint8_t method, uint16_t attr_id,
		     uint32_t attr_mod, void *data)
{
	ib_vendor_call_t call;

	memset(&call, 0, sizeof(call));
	call.mgmt_class = class;
	call.method = method;
	call.timeout = ibd_timeout;
	call.attrid = attr_id;
	call.mod = attr_mod;

	if (!ib_vendor_call_via(data, portid, &call, srcport)) {
		fprintf(stderr,"vendstat: method %u, attribute %u failure\n", method, attr_id);
		return -1;
	}
	return 0;
}

static int do_config_space_records(ib_portid_t *portid, unsigned set,
				    is3_config_space_t *cs, unsigned records)
{
	unsigned i;

	if (records > 18)
		records = 18;
	for (i = 0; i < records; i++) {
		cs->record[i].address = htonl(cs->record[i].address);
		cs->record[i].data = htonl(cs->record[i].data);
		cs->record[i].mask = htonl(cs->record[i].mask);
	}

	if (do_vendor(portid, srcport, IB_MLX_VENDOR_CLASS,
		      set ? IB_MAD_METHOD_SET : IB_MAD_METHOD_GET,
		      IB_MLX_IS3_CONFIG_SPACE_ACCESS, 2 << 22 | records << 16,
		      cs)) {
		fprintf(stderr,"cannot %s config space records\n", set ? "set" : "get");
		return -1;
	}
	for (i = 0; i < records; i++) {
		printf("Config space record at 0x%x: 0x%x\n",
		       ntohl(cs->record[i].address),
		       ntohl(cs->record[i].data & cs->record[i].mask));
	}
	return 0;
}

static int counter_groups_info(ib_portid_t * portid, int port)
{
	char buf[1024];
	is4_counter_group_info_t *cg_info;
	int i, num_cg;

	/* Counter Group Info */
	memset(&buf, 0, sizeof(buf));
	if (do_vendor(portid, srcport, IB_MLX_VENDOR_CLASS, IB_MAD_METHOD_GET,
		      IB_MLX_IS4_COUNTER_GROUP_INFO, port, buf)) {
		fprintf(stderr,"counter group info query failure\n");
		return -1;
	}
	cg_info = (is4_counter_group_info_t *) & buf;
	num_cg = cg_info->num_of_counter_groups;
	printf("counter_group_info:\n");
	printf("%d counter groups\n", num_cg);
	for (i = 0; i < num_cg; i++)
		printf("group%d mask %#x\n", i, ntohl(cg_info->group_masks[i]));
	return 0;
}

/* Group0 counter config values */
#define IS4_G0_PortXmtDataSL_0_7  0
#define IS4_G0_PortXmtDataSL_8_15 1
#define IS4_G0_PortRcvDataSL_0_7  2

/* Group1 counter config values */
#define IS4_G1_PortXmtDataSL_8_15 1
#define IS4_G1_PortRcvDataSL_0_7  2
#define IS4_G1_PortRcvDataSL_8_15 8

static int cg0, cg1;

static int config_counter_groups(ib_portid_t * portid, int port)
{
	char buf[1024];
	is4_config_counter_groups_t *cg_config;

	/* configure counter groups for groups 0 and 1 */
	memset(&buf, 0, sizeof(buf));
	cg_config = (is4_config_counter_groups_t *) & buf;

	printf("counter_groups_config: configuring group0 %d group1 %d\n", cg0,
	       cg1);
	cg_config->group_selects[0].group_select = (uint8_t) cg0;
	cg_config->group_selects[1].group_select = (uint8_t) cg1;

	if (do_vendor(portid, srcport, IB_MLX_VENDOR_CLASS, IB_MAD_METHOD_SET,
		      IB_MLX_IS4_CONFIG_COUNTER_GROUP, port, buf)) {
		fprintf(stderr, "config counter group set failure\n");
		return -1;
	}
	/* get config counter groups */
	memset(&buf, 0, sizeof(buf));

	if (do_vendor(portid, srcport, IB_MLX_VENDOR_CLASS, IB_MAD_METHOD_GET,
		      IB_MLX_IS4_CONFIG_COUNTER_GROUP, port, buf)) {
		fprintf(stderr, "config counter group query failure\n");
		return -1;
	}
	return 0;
}

static int general_info, xmit_wait, counter_group_info, config_counter_group;
static is3_config_space_t write_cs, read_cs;
static unsigned write_cs_records, read_cs_records;


static int process_opt(void *context, int ch, char *optarg)
{
	int ret;
	switch (ch) {
	case 'N':
		general_info = 1;
		break;
	case 'w':
		xmit_wait = 1;
		break;
	case 'i':
		counter_group_info = 1;
		break;
	case 'c':
		config_counter_group = 1;
		ret = sscanf(optarg, "%d,%d", &cg0, &cg1);
		if (ret != 2)
			return -1;
		break;
	case 'R':
		if (read_cs_records >= 18)
			break;
		ret = sscanf(optarg, "%x,%x",
			     &read_cs.record[read_cs_records].address,
			     &read_cs.record[read_cs_records].mask);
		if (ret < 1)
			return -1;
		else if (ret == 1)
			read_cs.record[read_cs_records].mask = 0xffffffff;
		read_cs_records++;
		break;
	case 'W':
		if (write_cs_records >= 18)
			break;
		ret = sscanf(optarg, "%x,%x,%x",
			     &write_cs.record[write_cs_records].address,
			     &write_cs.record[write_cs_records].data,
			     &write_cs.record[write_cs_records].mask);
		if (ret < 2)
			return -1;
		else if (ret == 2)
			write_cs.record[write_cs_records].mask = 0xffffffff;
		write_cs_records++;
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
	int mgmt_classes[2] = { IB_SA_CLASS, IB_MLX_VENDOR_CLASS };
	ib_portid_t portid = { 0 };
	int port = 0;
	char buf[1024];
	uint32_t fw_ver_major = 0;
	uint32_t fw_ver_minor = 0;
	uint32_t fw_ver_sub_minor = 0;
	uint8_t sw_ver_major = 0, sw_ver_minor = 0, sw_ver_sub_minor = 0;
	is3_general_info_t *gi_is3;
	is4_general_info_t *gi_is4;
	const struct ibdiag_opt opts[] = {
		{"N", 'N', 0, NULL, "show IS3 or IS4 general information"},
		{"w", 'w', 0, NULL, "show IS3 port xmit wait counters"},
		{"i", 'i', 0, NULL, "show IS4 counter group info"},
		{"c", 'c', 1, "<num,num>", "configure IS4 counter groups"},
		{"Read", 'R', 1, "<addr,mask>", "Read configuration space record at addr"},
		{"Write", 'W', 1, "<addr,val,mask>", "Write configuration space record at addr"},
		{"dgid", 25, 1, NULL, "remote gid (IPv6 format)"},
		{0}
	};

	char usage_args[] = "<lid|guid> [port]";
	const char *usage_examples[] = {
		"-N 6\t\t# read IS3 or IS4 general information",
		"-w 6\t\t# read IS3 port xmit wait counters",
		"-i 6 12\t# read IS4 port 12 counter group info",
		"-c 0,1 6 12\t# configure IS4 port 12 counter groups for PortXmitDataSL",
		"-c 2,8 6 12\t# configure IS4 port 12 counter groups for PortRcvDataSL",
		NULL
	};

	ibdiag_process_opts(argc, argv, NULL, "DKy", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc > 1)
		port = strtoul(argv[1], 0, 0);

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 2);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	if (argc) {
		if (with_grh && ibd_dest_type != IB_DEST_LID) {
			mad_rpc_close_port(srcport);
			IBEXIT("When using GRH, LID should be provided");
		}
		if (resolve_portid_str(ibd_ca, ibd_ca_port, &portid, argv[0],
				       ibd_dest_type, ibd_sm_id, srcport) < 0) {
			mad_rpc_close_port(srcport);
			IBEXIT("can't resolve destination port %s", argv[0]);
		}
		if (with_grh) {
			portid.grh_present = 1;
			memcpy(&portid.gid, &dgid, sizeof(portid.gid));
		}
	} else {
		if (resolve_self(ibd_ca, ibd_ca_port, &portid, &port, 0) < 0) {
			mad_rpc_close_port(srcport);
			IBEXIT("can't resolve self port %s", argv[0]);
		}
	}

	if (counter_group_info) {
		counter_groups_info(&portid, port);
		mad_rpc_close_port(srcport);
		exit(0);
	}

	if (config_counter_group) {
		config_counter_groups(&portid, port);
		mad_rpc_close_port(srcport);
		exit(0);
	}

	if (read_cs_records || write_cs_records) {
		if (read_cs_records)
			do_config_space_records(&portid, 0, &read_cs,
						read_cs_records);
		if (write_cs_records)
			do_config_space_records(&portid, 1, &write_cs,
						write_cs_records);
		mad_rpc_close_port(srcport);
		exit(0);
	}

	/* These are Mellanox specific vendor MADs */
	/* but vendors change the VendorId so how know for sure ? */
	/* Only General Info and Port Xmit Wait Counters */
	/* queries are currently supported */
	if (!general_info && !xmit_wait) {
		mad_rpc_close_port(srcport);
		IBEXIT("at least one of -N and -w must be specified");
	}
	/* Would need a list of these and it might not be complete */
	/* so for right now, punt on this */

	/* vendor ClassPortInfo is required attribute if class supported */
	memset(&buf, 0, sizeof(buf));
	if (do_vendor(&portid, srcport, IB_MLX_VENDOR_CLASS, IB_MAD_METHOD_GET,
		      CLASS_PORT_INFO, 0, buf)) {
		mad_rpc_close_port(srcport);
		IBEXIT("classportinfo query");
	}
	memset(&buf, 0, sizeof(buf));
	gi_is3 = (is3_general_info_t *) &buf;
	if (do_vendor(&portid, srcport, IB_MLX_VENDOR_CLASS, IB_MAD_METHOD_GET,
		      IB_MLX_IS3_GENERAL_INFO, 0, gi_is3)) {
		mad_rpc_close_port(srcport);
		IBEXIT("generalinfo query");
	}

	if (is_ext_fw_info_supported(ntohs(gi_is3->hw_info.device_id))) {
		gi_is4 = (is4_general_info_t *) &buf;
		fw_ver_major = ntohl(gi_is4->ext_fw_info.ext_major);
		fw_ver_minor = ntohl(gi_is4->ext_fw_info.ext_minor);
		fw_ver_sub_minor = ntohl(gi_is4->ext_fw_info.ext_sub_minor);
		sw_ver_major = gi_is4->sw_info.major;
		sw_ver_minor = gi_is4->sw_info.minor;
		sw_ver_sub_minor = gi_is4->sw_info.sub_minor;
	} else {
		fw_ver_major = gi_is3->fw_info.major;
		fw_ver_minor = gi_is3->fw_info.minor;
		fw_ver_sub_minor = gi_is3->fw_info.sub_minor;
		sw_ver_major = gi_is3->sw_info.major;
		sw_ver_minor = gi_is3->sw_info.minor;
		sw_ver_sub_minor = gi_is3->sw_info.sub_minor;
	}

	if (general_info) {
		/* dump IS3 or IS4 general info here */
		printf("hw_dev_rev:  0x%04x\n", ntohs(gi_is3->hw_info.hw_revision));
		printf("hw_dev_id:   0x%04x\n", ntohs(gi_is3->hw_info.device_id));
		printf("hw_uptime:   0x%08x\n", ntohl(gi_is3->hw_info.uptime));
		printf("fw_version:  %02d.%02d.%02d\n",
		       fw_ver_major, fw_ver_minor, fw_ver_sub_minor);
		printf("fw_build_id: 0x%04x\n", ntohl(gi_is3->fw_info.build_id));
		printf("fw_date:     %02x/%02x/%04x\n",
		       gi_is3->fw_info.month, gi_is3->fw_info.day,
		       ntohs(gi_is3->fw_info.year));
		printf("fw_psid:     '%s'\n", gi_is3->fw_info.psid);
		printf("fw_ini_ver:  %d\n",
		       ntohl(gi_is3->fw_info.ini_file_version));
		printf("sw_version:  %02d.%02d.%02d\n", sw_ver_major,
		       sw_ver_minor, sw_ver_sub_minor);
	}

	if (xmit_wait) {
		is3_config_space_t *cs;
		unsigned i;

		if (ntohs(gi_is3->hw_info.device_id) != IS3_DEVICE_ID) {
			mad_rpc_close_port(srcport);
			IBEXIT("Unsupported device ID 0x%x",
				ntohs(gi_is3->hw_info.device_id));
		}
		memset(&buf, 0, sizeof(buf));
		/* Set record addresses for each port */
		cs = (is3_config_space_t *) & buf;
		for (i = 0; i < 16; i++)
			cs->record[i].address =
			    htonl(IB_MLX_IS3_PORT_XMIT_WAIT + ((i + 1) << 12));
		if (do_vendor(&portid, srcport, IB_MLX_VENDOR_CLASS,
			      IB_MAD_METHOD_GET, IB_MLX_IS3_CONFIG_SPACE_ACCESS,
			      2 << 22 | 16 << 16, cs)) {
			mad_rpc_close_port(srcport);
			IBEXIT("vendstat");
		}
		for (i = 0; i < 16; i++)
			if (cs->record[i].data)	/* PortXmitWait is 32 bit counter */
				printf("Port %d: PortXmitWait 0x%x\n", i + 4, ntohl(cs->record[i].data));	/* port 4 is first port */

		/* Last 8 ports is another query */
		memset(&buf, 0, sizeof(buf));
		/* Set record addresses for each port */
		cs = (is3_config_space_t *) & buf;
		for (i = 0; i < 8; i++)
			cs->record[i].address =
			    htonl(IB_MLX_IS3_PORT_XMIT_WAIT + ((i + 17) << 12));
		if (do_vendor(&portid, srcport, IB_MLX_VENDOR_CLASS,
			      IB_MAD_METHOD_GET, IB_MLX_IS3_CONFIG_SPACE_ACCESS,
			      2 << 22 | 8 << 16, cs)) {
			mad_rpc_close_port(srcport);
			IBEXIT("vendstat");
		}

		for (i = 0; i < 8; i++)
			if (cs->record[i].data)	/* PortXmitWait is 32 bit counter */
				printf("Port %d: PortXmitWait 0x%x\n",
				       i < 4 ? i + 21 : i - 3,
				       ntohl(cs->record[i].data));
	}

	mad_rpc_close_port(srcport);
	exit(0);
}
