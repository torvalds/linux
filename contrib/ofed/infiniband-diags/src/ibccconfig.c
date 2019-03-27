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
#include <errno.h>
#include <netinet/in.h>
#include <limits.h>
#include <ctype.h>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <infiniband/mad.h>

#include "ibdiag_common.h"

struct ibmad_port *srcport;

static ibmad_gid_t dgid;
static int with_grh;

static op_fn_t congestion_key_info;
static op_fn_t switch_congestion_setting;
static op_fn_t switch_port_congestion_setting;
static op_fn_t ca_congestion_setting;
static op_fn_t congestion_control_table;

static const match_rec_t match_tbl[] = {
	{"CongestionKeyInfo", "CK", congestion_key_info, 0,
	 "<cckey> <cckeyprotectbit> <cckeyleaseperiod> <cckeyviolations>"},
	{"SwitchCongestionSetting", "SS", switch_congestion_setting, 0,
	 "<controlmap> <victimmask> <creditmask> <threshold> <packetsize> "
	 "<csthreshold> <csreturndelay> <markingrate>"},
	{"SwitchPortCongestionSetting", "SP", switch_port_congestion_setting, 1,
	 "<valid> <control_type> <threshold> <packet_size> <cong_parm_marking_rate>"},
	{"CACongestionSetting", "CS", ca_congestion_setting, 0,
	 "<port_control> <control_map> <ccti_timer> <ccti_increase> "
	 "<trigger_threshold> <ccti_min>"},
	{"CongestionControlTable", "CT", congestion_control_table, 0,
	 "<cctilimit> <index> <cctentry> <cctentry> ..."},
	{0}
};

uint64_t cckey = 0;

/*******************************************/
static char *parselonglongint(char *arg, uint64_t *val)
{
	char *endptr = NULL;

	errno = 0;
	*val = strtoull(arg, &endptr, 0);
	if ((endptr && *endptr != '\0')
	    || errno != 0) {
		if (errno == ERANGE)
			return "value out of range";
		return "invalid integer input";
	}

	return NULL;
}

static char *parseint(char *arg, uint32_t *val, int hexonly)
{
	char *endptr = NULL;

	errno = 0;
	*val = strtoul(arg, &endptr, hexonly ? 16 : 0);
	if ((endptr && *endptr != '\0')
	    || errno != 0) {
		if (errno == ERANGE)
			return "value out of range";
		return "invalid integer input";
	}

	return NULL;
}

static char *congestion_key_info(ib_portid_t * dest, char **argv, int argc)
{
	uint8_t rcv[IB_CC_DATA_SZ] = { 0 };
	uint8_t payload[IB_CC_DATA_SZ] = { 0 };
	uint64_t cc_key;
	uint32_t cc_keyprotectbit;
	uint32_t cc_keyleaseperiod;
	uint32_t cc_keyviolations;
	char *errstr;

	if (argc != 4)
		return "invalid number of parameters for CongestionKeyInfo";

	if ((errstr = parselonglongint(argv[0], &cc_key)))
		return errstr;
	if ((errstr = parseint(argv[1], &cc_keyprotectbit, 0)))
		return errstr;
	if ((errstr = parseint(argv[2], &cc_keyleaseperiod, 0)))
		return errstr;
	if ((errstr = parseint(argv[3], &cc_keyviolations, 0)))
		return errstr;

	if (cc_keyprotectbit != 0 && cc_keyprotectbit != 1)
		return "invalid cc_keyprotectbit value";

	if (cc_keyleaseperiod > USHRT_MAX)
		return "invalid cc_keyleaseperiod value";

	if (cc_keyviolations > USHRT_MAX)
		return "invalid cc_keyviolations value";

	mad_set_field64(payload,
			0,
			IB_CC_CONGESTION_KEY_INFO_CC_KEY_F,
			cc_key);

	mad_encode_field(payload,
			 IB_CC_CONGESTION_KEY_INFO_CC_KEY_PROTECT_BIT_F,
			 &cc_keyprotectbit);

	mad_encode_field(payload,
			 IB_CC_CONGESTION_KEY_INFO_CC_KEY_LEASE_PERIOD_F,
			 &cc_keyleaseperiod);

	/* spec says "setting the counter to a value other than zero results
	 * in the counter being left unchanged.  So if user wants no change,
	 * they gotta input non-zero
	 */
        mad_encode_field(payload,
			 IB_CC_CONGESTION_KEY_INFO_CC_KEY_VIOLATIONS_F,
			 &cc_keyviolations);
	
	if (!cc_config_status_via(payload, rcv, dest, IB_CC_ATTR_CONGESTION_KEY_INFO,
				  0, 0, NULL, srcport, cckey))
		return "congestion key info config failed";

	return NULL;
}


/* parse like it's a hypothetical 256 bit hex code */
static char *parse256(char *arg, uint8_t *buf)
{
	int numdigits = 0;
	int startindex;
	char *ptr;
	int i;

	if (!strncmp(arg, "0x", 2) || !strncmp(arg, "0X", 2))
		arg += 2;

	for (ptr = arg; *ptr; ptr++) {
		if (!isxdigit(*ptr))
			return "invalid hex digit read";
		numdigits++;
	}

	if (numdigits > 64)
		return "hex code too long";

	/* we need to imagine that this is like a 256-bit int stored
	 * in big endian.  So we need to find the first index
	 * point where the user's input would start in our array.
	 */
	startindex = 32 - ((numdigits - 1) / 2) - 1;

	for (i = startindex; i <= 31; i++) {
		char tmp[3] = { 0 };
		uint32_t tmpint;
		char *errstr;

		/* I can't help but think there is a strtoX that
		 * will do this for me, but I can't find it.
		 */
		if (i == startindex && numdigits % 2) {
			memcpy(tmp, arg, 1);
			arg++;
		}
		else {
			memcpy(tmp, arg, 2);
			arg += 2;
		}

		if ((errstr = parseint(tmp, &tmpint, 1)))
			return errstr;
		buf[i] = tmpint;
	}

	return NULL;
}

static char *parsecct(char *arg, uint32_t *shift, uint32_t *multiplier)
{
	char buf[1024] = { 0 };
	char *errstr;
	char *ptr;

	strcpy(buf, arg);

	if (!(ptr = strchr(buf, ':')))
		return "ccts are formatted shift:multiplier";

	*ptr = '\0';
	ptr++;

	if ((errstr = parseint(buf, shift, 0)))
		return errstr;

	if ((errstr = parseint(ptr, multiplier, 0)))
		return errstr;

	return NULL;	
}

static char *switch_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	uint8_t rcv[IB_CC_DATA_SZ] = { 0 };
	uint8_t payload[IB_CC_DATA_SZ] = { 0 };
	uint32_t control_map;
	uint8_t victim_mask[32] = { 0 };
	uint8_t credit_mask[32] = { 0 };
	uint32_t threshold;
	uint32_t packet_size;
	uint32_t cs_threshold;
	uint32_t cs_returndelay_s;
	uint32_t cs_returndelay_m;
	uint32_t cs_returndelay;
	uint32_t marking_rate;
	char *errstr;

	if (argc != 8)
		return "invalid number of parameters for SwitchCongestionSetting";

	if ((errstr = parseint(argv[0], &control_map, 0)))
		return errstr;

	if ((errstr = parse256(argv[1], victim_mask)))
		return errstr;

	if ((errstr = parse256(argv[2], credit_mask)))
		return errstr;

	if ((errstr = parseint(argv[3], &threshold, 0)))
		return errstr;

	if ((errstr = parseint(argv[4], &packet_size, 0)))
		return errstr;

	if ((errstr = parseint(argv[5], &cs_threshold, 0)))
		return errstr;

	if ((errstr = parsecct(argv[6], &cs_returndelay_s, &cs_returndelay_m)))
		return errstr;

	cs_returndelay = cs_returndelay_m;
	cs_returndelay |= (cs_returndelay_s << 14);

	if ((errstr = parseint(argv[7], &marking_rate, 0)))
		return errstr;

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_CONTROL_MAP_F,
			 &control_map);

	mad_set_array(payload,
		      0,
		      IB_CC_SWITCH_CONGESTION_SETTING_VICTIM_MASK_F,
		      victim_mask);

	mad_set_array(payload,
		      0,
		      IB_CC_SWITCH_CONGESTION_SETTING_CREDIT_MASK_F,
		      credit_mask);

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_THRESHOLD_F,
			 &threshold);

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_PACKET_SIZE_F,
			 &packet_size);

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_CS_THRESHOLD_F,
			 &cs_threshold);

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_CS_RETURN_DELAY_F,
			 &cs_returndelay);

	mad_encode_field(payload,
			 IB_CC_SWITCH_CONGESTION_SETTING_MARKING_RATE_F,
			 &marking_rate);

	if (!cc_config_status_via(payload, rcv, dest, IB_CC_ATTR_SWITCH_CONGESTION_SETTING,
				  0, 0, NULL, srcport, cckey))
		return "switch congestion setting config failed";

	return NULL;
}

static char *switch_port_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	uint8_t rcv[IB_CC_DATA_SZ] = { 0 };
	uint8_t payload[IB_CC_DATA_SZ] = { 0 };
	uint8_t data[IB_CC_DATA_SZ] = { 0 };
	uint32_t portnum;
	uint32_t valid;
	uint32_t control_type;
	uint32_t threshold;
	uint32_t packet_size;
	uint32_t cong_parm_marking_rate;
	uint32_t type;
	uint32_t numports;
	uint8_t *ptr;
	char *errstr;

	if (argc != 6)
		return "invalid number of parameters for SwitchPortCongestion";

	if ((errstr = parseint(argv[0], &portnum, 0)))
		return errstr;

	if ((errstr = parseint(argv[1], &valid, 0)))
		return errstr;

	if ((errstr = parseint(argv[2], &control_type, 0)))
		return errstr;

	if ((errstr = parseint(argv[3], &threshold, 0)))
		return errstr;

	if ((errstr = parseint(argv[4], &packet_size, 0)))
		return errstr;

	if ((errstr = parseint(argv[5], &cong_parm_marking_rate, 0)))
		return errstr;

	/* Figure out number of ports first */
	if (!smp_query_via(data, dest, IB_ATTR_NODE_INFO, 0, 0, srcport))
		return "node info config failed";

	mad_decode_field((uint8_t *)data, IB_NODE_TYPE_F, &type);
	mad_decode_field((uint8_t *)data, IB_NODE_NPORTS_F, &numports);

	if (type != IB_NODE_SWITCH)
		return "destination not a switch";

	if (portnum > numports)
		return "invalid port number specified";

	/* We are modifying only 1 port, so get the current config */
	if (!cc_query_status_via(payload, dest, IB_CC_ATTR_SWITCH_PORT_CONGESTION_SETTING,
				 portnum / 32, 0, NULL, srcport, cckey))
		return "switch port congestion setting query failed";

	ptr = payload + (((portnum % 32) * 4));

	mad_encode_field(ptr,
			 IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_VALID_F,
			 &valid);

	mad_encode_field(ptr,
			 IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_CONTROL_TYPE_F,
			 &control_type);

	mad_encode_field(ptr,
			 IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_THRESHOLD_F,
			 &threshold);

	mad_encode_field(ptr,
			 IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_PACKET_SIZE_F,
			 &packet_size);

	mad_encode_field(ptr,
			 IB_CC_SWITCH_PORT_CONGESTION_SETTING_ELEMENT_CONG_PARM_MARKING_RATE_F,
			 &cong_parm_marking_rate);

	if (!cc_config_status_via(payload, rcv, dest, IB_CC_ATTR_SWITCH_PORT_CONGESTION_SETTING,
				  portnum / 32, 0, NULL, srcport, cckey))
		return "switch port congestion setting config failed";

	return NULL;
}

static char *ca_congestion_setting(ib_portid_t * dest, char **argv, int argc)
{
	uint8_t rcv[IB_CC_DATA_SZ] = { 0 };
	uint8_t payload[IB_CC_DATA_SZ] = { 0 };
	uint32_t port_control;
	uint32_t control_map;
	uint32_t ccti_timer;
	uint32_t ccti_increase;
	uint32_t trigger_threshold;
	uint32_t ccti_min;
	char *errstr;
	int i;

	if (argc != 6)
		return "invalid number of parameters for CACongestionSetting";

	if ((errstr = parseint(argv[0], &port_control, 0)))
		return errstr;
	
	if ((errstr = parseint(argv[1], &control_map, 0)))
		return errstr;

	if ((errstr = parseint(argv[2], &ccti_timer, 0)))
		return errstr;

	if ((errstr = parseint(argv[3], &ccti_increase, 0)))
		return errstr;

	if ((errstr = parseint(argv[4], &trigger_threshold, 0)))
		return errstr;

	if ((errstr = parseint(argv[5], &ccti_min, 0)))
		return errstr;

	mad_encode_field(payload,
			 IB_CC_CA_CONGESTION_SETTING_PORT_CONTROL_F,
			 &port_control);

	mad_encode_field(payload,
			 IB_CC_CA_CONGESTION_SETTING_CONTROL_MAP_F,
			 &control_map);

	for (i = 0; i < 16; i++) {
		uint8_t *ptr;

		if (!(control_map & (0x1 << i)))
			continue;

		ptr = payload + 2 + 2 + i * 8;

		mad_encode_field(ptr,
				 IB_CC_CA_CONGESTION_ENTRY_CCTI_TIMER_F,
				 &ccti_timer);

		mad_encode_field(ptr,
				 IB_CC_CA_CONGESTION_ENTRY_CCTI_INCREASE_F,
				 &ccti_increase);

		mad_encode_field(ptr,
				 IB_CC_CA_CONGESTION_ENTRY_TRIGGER_THRESHOLD_F,
				 &trigger_threshold);

		mad_encode_field(ptr,
				 IB_CC_CA_CONGESTION_ENTRY_CCTI_MIN_F,
				 &ccti_min);
	}
			 
	if (!cc_config_status_via(payload, rcv, dest, IB_CC_ATTR_CA_CONGESTION_SETTING,
				  0, 0, NULL, srcport, cckey))
		return "ca congestion setting config failed";

	return NULL;
}

static char *congestion_control_table(ib_portid_t * dest, char **argv, int argc)
{
	uint8_t rcv[IB_CC_DATA_SZ] = { 0 };
	uint8_t payload[IB_CC_DATA_SZ] = { 0 };
	uint32_t ccti_limit;
	uint32_t index;
	uint32_t cctshifts[64];
	uint32_t cctmults[64];
	char *errstr;
	int i;

	if (argc < 2 || argc > 66)
		return "invalid number of parameters for CongestionControlTable";

	if ((errstr = parseint(argv[0], &ccti_limit, 0)))
		return errstr;

	if ((errstr = parseint(argv[1], &index, 0)))
		return errstr;

	if (ccti_limit && (ccti_limit + 1) != (index * 64 + (argc - 2)))
		return "invalid number of cct entries input given ccti_limit and index";

	for (i = 0; i < (argc - 2); i++) {
		if ((errstr = parsecct(argv[i + 2], &cctshifts[i], &cctmults[i])))
			return errstr;
	}

	mad_encode_field(payload,
			 IB_CC_CONGESTION_CONTROL_TABLE_CCTI_LIMIT_F,
			 &ccti_limit);

	for (i = 0; i < (argc - 2); i++) {
		mad_encode_field(payload + 4 + i * 2,
				 IB_CC_CONGESTION_CONTROL_TABLE_ENTRY_CCT_SHIFT_F,
				 &cctshifts[i]);

		mad_encode_field(payload + 4 + i * 2,
				 IB_CC_CONGESTION_CONTROL_TABLE_ENTRY_CCT_MULTIPLIER_F,
				 &cctmults[i]);
	}

	if (!cc_config_status_via(payload, rcv, dest, IB_CC_ATTR_CONGESTION_CONTROL_TABLE,
				  index, 0, NULL, srcport, cckey))
		return "congestion control table config failed";	

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
		"SwitchCongestionSetting 2 0x1F 0x1FFFFFFFFF 0x0 0xF 8 0 0:0 1\t# Configure Switch Congestion Settings",
		"CACongestionSetting 1 0 0x3 150 1 0 0\t\t# Configure CA Congestion Settings to SL 0 and SL 1",
		"CACongestionSetting 1 0 0x4 200 1 0 0\t\t# Configure CA Congestion Settings to SL 2",
		"CongestionControlTable 1 63 0 0:0 0:1 ...\t# Configure first block of Congestion Control Table",
		"CongestionControlTable 1 127 0 0:64 0:65 ...\t# Configure second block of Congestion Control Table",
		NULL
	};

	n = sprintf(usage_args, "[-c key] <op> <lid|guid>\n"
		    "\nWARNING -- You should understand what you are "
		    "doing before using this tool.  Misuse of this "
		    "tool could result in a broken fabric.\n"
		    "\nSupported ops (and aliases, case insensitive):\n");
	for (r = match_tbl; r->name; r++) {
		n += snprintf(usage_args + n, sizeof(usage_args) - n,
			      "  %s (%s) <lid|guid>%s%s%s\n", r->name,
			      r->alias ? r->alias : "",
			      r->opt_portnum ? " <portnum>" : "",
			      r->ops_extra ? " " : "",
			      r->ops_extra ? r->ops_extra : "");
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
