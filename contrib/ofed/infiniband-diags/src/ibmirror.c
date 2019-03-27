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
#include <getopt.h>
#include <netinet/in.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "ibdiag_common.h"

#define IB_MLX_VENDOR_CLASS		10

/* Vendor specific Attribute IDs */
#define IB_MLX_IS3_GENERAL_INFO		0x17

#define MAX_SWITCH_PORTS         (36+1)
static char mtx_ports[MAX_SWITCH_PORTS] = {0};
static char mrx_ports[MAX_SWITCH_PORTS] = {0};
static char str[4096];
static uint8_t buf[256];

#define ATTRID_PM_ROUTE   0xff30
#define ATTRID_PM_FILTER  0xff31
#define ATTRID_PM_PORTS   0xff32
#define ATTRID_LOSSY_CFG  0xff80

enum mirror_type {
	MT_DISABLED        = 0,
	MT_MIRROR_NATIVE   = 2,
	MT_DROP            = 5,
	MT_MIRROR_ENCAP    = 6,
	MT_MIRROR_DROP     = 7
};

enum mirror_port {
	MP_DISABLED          = 0,
	MP_MIRROR_FILTER     = 1,
	MP_MIRROR_ALWAYS     = 2,
	MP_MIRROR_FILTER_NOT = 3,
	MT_MIRROR_AS_RX      = 1
};

#define PM_ENCAP_ETHERTYPE 0x1123

struct ibmad_port *srcport;

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
	uint16_t ignore_buffer_mask;
	uint16_t ignore_credit_mask;
} lossy_config_t;

static int mirror_query, mirror_dport, mirror_dlid, mirror_clear, mirror_sl, lossy_set;
static int set_mtx, set_mrx, packet_size = 0xfff;

static int parse_ports(char *ports_str, char *ports_array)
{
	int num, i;
	char *str = strdup(ports_str);
	char *token = strtok(str, ",");
	for (i = 0; i < MAX_SWITCH_PORTS && token; i++) {
		num = strtoul(token, NULL, 0);
		if (num > 0 && num < MAX_SWITCH_PORTS)
			ports_array[num] = 1;

		token = strtok(NULL, ",");
	}
	free(str);
	return 0;
}

void port_mirror_route(ib_portid_t * portid, int query, int clear)
{
	int mirror_type;

	memset(&buf, 0, sizeof(buf));

	if (clear) {
		if (!smp_set_via(buf, portid, ATTRID_PM_ROUTE, 0, 0, srcport))
			IBEXIT("Clear port mirror route set failed");
		return;
	}

	if (query) {
		if (!smp_query_via(buf, portid, ATTRID_PM_ROUTE, 0, 0, srcport))
			IBEXIT("Read port mirror route get failed");
		mad_decode_field(buf, IB_PMR_MT_F, &mirror_type);
		if (mirror_type == MT_MIRROR_ENCAP && mirror_dlid == 0)
			mad_decode_field(buf, IB_PMR_LRH_DLID_F, &mirror_dlid);
		if (mirror_type == MT_MIRROR_NATIVE && mirror_dport == 0)
			mad_decode_field(buf, IB_PMR_NM_PORT_F, &mirror_dport);
		goto Exit;
	}

	/* Port Mirror Route */
	mad_set_field(buf, 0, IB_PMR_ENCAP_RAW_ETH_TYPE_F, PM_ENCAP_ETHERTYPE);

	if (mirror_dlid == 0) {
		/* Can not truncate mirrored packets in local mode */
		mad_set_field(buf, 0, IB_PMR_MAX_MIRROR_LEN_F, 0xfff);
		mad_set_field(buf, 0, IB_PMR_MT_F, MT_MIRROR_NATIVE);
		mad_set_field(buf, 0, IB_PMR_NM_PORT_F, mirror_dport);
	}
	else { /* remote mirror */
		/* convert size to dwords */
		packet_size = packet_size / 4 + 1;
		mad_set_field(buf, 0, IB_PMR_MAX_MIRROR_LEN_F, packet_size);
		mad_set_field(buf, 0, IB_PMR_MT_F, MT_MIRROR_ENCAP);
		mad_set_field(buf, 0, IB_PMR_LRH_SL_F, mirror_sl);
		mad_set_field(buf, 0, IB_PMR_LRH_DLID_F, mirror_dlid);
		mad_set_field(buf, 0, IB_PMR_LRH_SLID_F, portid->lid);
	}

	if (!smp_set_via(buf, portid, ATTRID_PM_ROUTE, 0, 0, srcport))
		IBEXIT("port mirror route set failed");

Exit:
	mad_dump_portmirror_route(str, sizeof str, buf, sizeof buf);
	printf("Port Mirror Route\n%s", str);
}

void port_mirror_ports(ib_portid_t * portid, int query, int clear)
{
	int p, rqf, tqf, rqv, tqv;

	memset(&buf, 0, sizeof(buf));

	if (clear) {
		if (!smp_set_via(buf, portid, ATTRID_PM_PORTS, 0, 0, srcport))
			IBEXIT("Clear port mirror ports set failed");
		return;
	}

	if (query) {
		if (!smp_query_via(buf, portid, ATTRID_PM_PORTS, 0, 0, srcport))
			IBEXIT("Read port mirror ports get failed");
		goto Exit;
	}

	/* Port Mirror Ports */
	rqf = IB_PMP_RQ_1_F;
	tqf = IB_PMP_TQ_1_F;

	for (p = 1; p < MAX_SWITCH_PORTS; p++) {
		rqv = mrx_ports[p] ? MP_MIRROR_ALWAYS : MP_DISABLED;
		tqv = mtx_ports[p] ? MP_MIRROR_ALWAYS : MT_MIRROR_AS_RX;
		mad_set_field(buf, 0, rqf, rqv);
		mad_set_field(buf, 0, tqf, tqv);
		rqf += 2;
		tqf += 2;
	}

	if (!smp_set_via(buf, portid, ATTRID_PM_PORTS, 0, 0, srcport))
		IBEXIT("port mirror ports set failed");

Exit:
	mad_dump_portmirror_ports(str, sizeof str, buf, sizeof buf);
	printf("Port Mirror Ports\n%s", str);
}

int get_out_port(ib_portid_t* portid)
{
	int block;
	int offset;

	if (mirror_dlid) {
		block = mirror_dlid / IB_SMP_DATA_SIZE;
		offset = mirror_dlid - block * IB_SMP_DATA_SIZE;
		/* get out port from lft */
		if (!smp_query_via(buf, portid, IB_ATTR_LINEARFORWTBL, block, 0, srcport))
			IBEXIT("linear forwarding table get failed");
		block = mirror_dlid / IB_SMP_DATA_SIZE;
		offset = mirror_dlid - block * IB_SMP_DATA_SIZE;
		return buf[offset];
	}
	else
		return mirror_dport;
}

int get_peer(ib_portid_t* portid, int outport, int* peerlid, int* peerport)
{
	ib_portid_t selfportid = { 0 };
	ib_portid_t peerportid = { 0 };
	int selfport = 0;

	/* set peerportid for peer port */
	memcpy(&peerportid, portid, sizeof(peerportid));
	peerportid.drpath.cnt = 1;
	peerportid.drpath.p[1] = outport;
	if (ib_resolve_self_via(&selfportid, &selfport, 0, srcport) < 0)
		IBEXIT("failed to resolve self portid");
	peerportid.drpath.drslid = (uint16_t) selfportid.lid;
	peerportid.drpath.drdlid = 0xffff;
	if (!smp_query_via(buf, &peerportid, IB_ATTR_PORT_INFO, 0, 0, srcport))
		IBEXIT("get peer portinfo failed - unable to configure lossy\n");

	mad_decode_field(buf, IB_PORT_LID_F, peerlid);
	mad_decode_field(buf, IB_PORT_LOCAL_PORT_F, peerport);

	return 0;
}

int get_mirror_vl(ib_portid_t* portid, int outport)
{
	ib_slvl_table_t * p_slvl_tbl;
	int portnum;
	int vl;

	/* hack; assume all sl2vl mappings are the same for any in port and outport */
	portnum = (1 << 8) | outport;

	/* get sl2vl mapping */
	if (!smp_query_via(buf, portid, IB_ATTR_SLVL_TABLE, portnum, 0, srcport))
		IBEXIT("slvl query failed");

	p_slvl_tbl = (ib_slvl_table_t *) buf;
	vl = ib_slvl_table_get(p_slvl_tbl, mirror_sl);
	printf("mirror_sl %d, mirror_vl %d\n", mirror_sl, vl);
	return vl;
}

int lossy_config(ib_portid_t* portid, int query, int clear)
{
	int outport;
	int peerport;
	int attr_mod;
	uint8_t mirror_vl;
	ib_portid_t peerportid = { 0 };
	ib_portid_t * p_portid;
	lossy_config_t local_lossy_cfg;
	lossy_config_t peer_lossy_cfg;
	lossy_config_t lossy_cfg;

	outport = get_out_port(portid);
	if (outport == 0)
		IBEXIT("get_out_port failed, mirror_dlid and mirror_dport are 0");

	get_peer(portid, outport, &peerportid.lid, &peerport);

	printf("local lid %d / port %d\n", portid->lid, outport);
	printf("peer  lid %d / port %d\n", peerportid.lid, peerport);

	mirror_vl = get_mirror_vl(portid, outport);

	/* read local lossy configuration */
	if (!smp_query_via(buf, portid, ATTRID_LOSSY_CFG, outport, 0, srcport))
		IBEXIT("get lossy config from lid %d port %d failed - not supported\n",
			portid->lid, outport);
	memcpy(&local_lossy_cfg, buf, sizeof(local_lossy_cfg));

	/* read peer lossy configuration */
	if (!smp_query_via(buf, &peerportid, ATTRID_LOSSY_CFG, peerport, 0, srcport))
		IBEXIT("get lossy config from lid %d port %d failed - not supported\n",
			peerportid.lid, peerport);
	memcpy(&peer_lossy_cfg, buf, sizeof(peer_lossy_cfg));

	if (query) {
		printf("local port lid %d port %d ignore_buffer 0x%04x, ignore_credit 0x%04x\n",
			portid->lid, outport,
			ntohs(local_lossy_cfg.ignore_buffer_mask), ntohs(local_lossy_cfg.ignore_credit_mask));
		printf("peer  port lid %d port %d ignore_buffer 0x%04x, ignore_credit 0x%04x\n",
			peerportid.lid, peerport,
			ntohs(peer_lossy_cfg.ignore_buffer_mask), ntohs(peer_lossy_cfg.ignore_credit_mask));
		return 0;
	}

	/* VLs 15 14 13 12 11 10 9 8 7 6 5 4 3 2 1  15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 */
	/*                ignore Buf Overrun             ignore Credits                 */
	/* when mirror activated set ignore buffer overrun on peer port */
	/* when mirror is de-activated clear ignore credits on local port */
	memset(&buf, 0, sizeof(buf));
	if (clear) {
		p_portid = portid;
		attr_mod = outport;
	} else {
		/* set buffer overrun on peer port */
		p_portid = &peerportid;
		attr_mod = peerport;
		lossy_cfg.ignore_buffer_mask = htons(1<<mirror_vl);
		lossy_cfg.ignore_credit_mask = 0;
		memcpy(&buf, &lossy_cfg, sizeof(lossy_cfg));
	}
	if (!smp_set_via(buf, p_portid, ATTRID_LOSSY_CFG, attr_mod, 0, srcport))
		IBEXIT("%s lossy config on lid %d failed\n", clear?"clear":"set", p_portid->lid);

	/* when mirror activated set ignore credit on local port */
	/* when mirror de-activated clear buffer overrun on peer */
	memset(&buf, 0, sizeof(buf));
	if (clear) {
		p_portid = &peerportid;
		attr_mod = peerport;
	} else {
		/* set ignore credit on local port */
		p_portid = portid;
		attr_mod = outport;
		lossy_cfg.ignore_credit_mask = htons(1<<mirror_vl);
		lossy_cfg.ignore_buffer_mask = 0;
		memcpy(&buf, &lossy_cfg, sizeof(lossy_cfg));
	}
	if (!smp_set_via(buf, p_portid, ATTRID_LOSSY_CFG, attr_mod, 0, srcport))
		IBEXIT("%s lossy config on lid %d failed\n", clear?"clear":"set", p_portid->lid);

	return 0;
}

int mirror_config(ib_portid_t* portid, int query, int clear)
{
	port_mirror_route(portid, query, clear);
	/* port_mirror_filter(portid, query, clear); */
	port_mirror_ports(portid, query, clear);

	return 0;
}

static int process_opt(void *context, int ch, char *optarg)
{
	switch (ch) {
	case 'p':
		mirror_dport = strtoul(optarg, NULL, 0);
		break;
	case 'S':
		packet_size = strtoul(optarg, NULL, 0);
		break;
	case 'l':
		mirror_sl = strtoul(optarg, NULL, 0);
		break;
	case 'L':
		mirror_dlid = strtoul(optarg, NULL, 0);
		break;
	case 'R':
		set_mrx = 1;
		if (-1 == parse_ports(optarg, mrx_ports))
			return -1;
		break;
	case 'T':
		set_mtx = 1;
		if (-1 == parse_ports(optarg, mtx_ports))
			return -1;
		break;
	case 'D':
		mirror_clear = 1;
		break;
	case 'Q':
		mirror_query = 1;
		break;
	case 'y':
		lossy_set = 1;
		break;
	default:
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int mgmt_classes[4] = { IB_SMI_CLASS, IB_SMI_DIRECT_CLASS, IB_SA_CLASS,
		IB_MLX_VENDOR_CLASS
	};
	ib_portid_t portid = { 0 };
	int port = 0;
	ib_vendor_call_t call;
	is3_general_info_t *gi;
	uint32_t fw_ver;
	char op_str[32];

	const struct ibdiag_opt opts[] = {
		{"dport", 'p', 1, "<port>", "set mirror destination port"},
		{"dlid", 'L', 1, "<dlid>", "set mirror destination LID"},
		{"sl", 'l', 1, "<sl>", "set mirror SL"},
		{"size", 'S', 1, "<size>", "set packet size"},
		{"rxports", 'R', 1, NULL, "mirror receive port list"},
		{"txports", 'T', 1, NULL, "mirror transmit port list"},
		{"clear", 'D', 0, NULL, "clear ports mirroring"},
		{"query", 'Q', 0, NULL, "read mirror configuration"},
		{"lossy", 'y', 0, NULL, "set lossy configuration on out port"},
		{0}
	};

	char usage_args[] = "<lid>";
	const char *usage_examples[] = {
		"-R 1,2,3 -T 2,5 -l1 -L25 -S100 <lid>\t# configure mirror ports",
		"-D <lid> \t# clear mirror configuration",
		"-Q <lid>\t# read mirror configuration",
		NULL
	};

	ibdiag_process_opts(argc, argv, NULL, "GDLs", opts, process_opt,
			    usage_args, usage_examples);

	argc -= optind;
	argv += optind;

	if (argc == 0)
		ibdiag_show_usage();

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 4);
	if (!srcport)
		IBEXIT("Failed to open '%s' port '%d'", ibd_ca, ibd_ca_port);

	if (argc) {
		if (ib_resolve_portid_str_via(&portid, argv[0], ibd_dest_type,
					      ibd_sm_id, srcport) < 0)
			IBEXIT("can't resolve destination port %s", argv[0]);
	}


	memset(&buf, 0, sizeof(buf));
	memset(&call, 0, sizeof(call));
	call.mgmt_class = IB_MLX_VENDOR_CLASS;
	call.method = IB_MAD_METHOD_GET;
	call.timeout = ibd_timeout;
	call.attrid = IB_MLX_IS3_GENERAL_INFO;
	if (!ib_vendor_call_via(&buf, &portid, &call, srcport))
		IBEXIT("failed to read vendor info");
	gi = (is3_general_info_t *) & buf;
	if (ntohs(gi->hw_info.device_id) != 0x1b3)
		IBEXIT("device id 0x%x does not support mirroring", ntohs(gi->hw_info.device_id));

	fw_ver = gi->fw_info.major * 100000 + gi->fw_info.minor * 1000 + gi->fw_info.sub_minor;
	printf("FW version %08d\n", fw_ver);
	if (lossy_set && fw_ver < 704000)
		IBEXIT("FW version %d.%d.%d does not support lossy config",
			gi->fw_info.major, gi->fw_info.minor, gi->fw_info.sub_minor);

	if (ibdebug) {
		printf( "switch_lid = %d\n"
			"mirror_clear = %d\n"
			"mirror_dlid = %d\n"
			"mirror_sl = %d\n"
			"mirror_port = %d\n",
			portid.lid, mirror_clear, mirror_dlid,
			mirror_sl, mirror_dport);

		for (port = 1; port < MAX_SWITCH_PORTS; port++) {
			if (mtx_ports[port])
				printf("TX:	%d\n",port);
			else if(mrx_ports[port])
				printf("RX:	%d\n",port);
		}
	}

	if (mirror_clear)
		strcpy(op_str, "Clear");
	else if (mirror_query)
		strcpy(op_str, "Read");
	else if (!mirror_dport && !mirror_dlid)
		IBEXIT("Mirror remote LID and local port are zero");
	else if (!set_mtx && !set_mrx)
		IBEXIT("Mirror Rx and Tx ports not selected");
	else
		strcpy(op_str, "Set");

	printf("\n%s Mirror Configuration\n", op_str);
	mirror_config(&portid, mirror_query, mirror_clear);

	if (lossy_set) {
		printf("%s Lossy Configuration\n", op_str);
		lossy_config(&portid, mirror_query, mirror_clear);
	}

	mad_rpc_close_port(srcport);
	exit(0);
}
