/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (C) 2023 Microchip Technology Inc. and its subsidiaries.
 * Microchip VCAP TC
 */

#ifndef __VCAP_TC__
#define __VCAP_TC__

struct vcap_tc_flower_parse_usage {
	struct flow_cls_offload *fco;
	struct flow_rule *frule;
	struct vcap_rule *vrule;
	struct vcap_admin *admin;
	u16 l3_proto;
	u8 l4_proto;
	u16 tpid;
	unsigned int used_keys;
};

int vcap_tc_flower_handler_ethaddr_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_ipv4_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_ipv6_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_portnum_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_cvlan_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_vlan_usage(struct vcap_tc_flower_parse_usage *st,
				      enum vcap_key_field vid_key,
				      enum vcap_key_field pcp_key);
int vcap_tc_flower_handler_tcp_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_arp_usage(struct vcap_tc_flower_parse_usage *st);
int vcap_tc_flower_handler_ip_usage(struct vcap_tc_flower_parse_usage *st);

#endif /* __VCAP_TC__ */
