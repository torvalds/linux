/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/* Microsemi Ocelot Switch driver
 * Copyright (c) 2019 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_ACE_H_
#define _MSCC_OCELOT_ACE_H_

#include "ocelot.h"
#include <net/sch_generic.h>
#include <net/pkt_cls.h>

struct ocelot_ipv4 {
	u8 addr[4];
};

enum ocelot_vcap_bit {
	OCELOT_VCAP_BIT_ANY,
	OCELOT_VCAP_BIT_0,
	OCELOT_VCAP_BIT_1
};

struct ocelot_vcap_u8 {
	u8 value[1];
	u8 mask[1];
};

struct ocelot_vcap_u16 {
	u8 value[2];
	u8 mask[2];
};

struct ocelot_vcap_u24 {
	u8 value[3];
	u8 mask[3];
};

struct ocelot_vcap_u32 {
	u8 value[4];
	u8 mask[4];
};

struct ocelot_vcap_u40 {
	u8 value[5];
	u8 mask[5];
};

struct ocelot_vcap_u48 {
	u8 value[6];
	u8 mask[6];
};

struct ocelot_vcap_u64 {
	u8 value[8];
	u8 mask[8];
};

struct ocelot_vcap_u128 {
	u8 value[16];
	u8 mask[16];
};

struct ocelot_vcap_vid {
	u16 value;
	u16 mask;
};

struct ocelot_vcap_ipv4 {
	struct ocelot_ipv4 value;
	struct ocelot_ipv4 mask;
};

struct ocelot_vcap_udp_tcp {
	u16 value;
	u16 mask;
};

enum ocelot_ace_type {
	OCELOT_ACE_TYPE_ANY,
	OCELOT_ACE_TYPE_ETYPE,
	OCELOT_ACE_TYPE_LLC,
	OCELOT_ACE_TYPE_SNAP,
	OCELOT_ACE_TYPE_ARP,
	OCELOT_ACE_TYPE_IPV4,
	OCELOT_ACE_TYPE_IPV6
};

struct ocelot_ace_vlan {
	struct ocelot_vcap_vid vid;    /* VLAN ID (12 bit) */
	struct ocelot_vcap_u8  pcp;    /* PCP (3 bit) */
	enum ocelot_vcap_bit dei;    /* DEI */
	enum ocelot_vcap_bit tagged; /* Tagged/untagged frame */
};

struct ocelot_ace_frame_etype {
	struct ocelot_vcap_u48 dmac;
	struct ocelot_vcap_u48 smac;
	struct ocelot_vcap_u16 etype;
	struct ocelot_vcap_u16 data; /* MAC data */
};

struct ocelot_ace_frame_llc {
	struct ocelot_vcap_u48 dmac;
	struct ocelot_vcap_u48 smac;

	/* LLC header: DSAP at byte 0, SSAP at byte 1, Control at byte 2 */
	struct ocelot_vcap_u32 llc;
};

struct ocelot_ace_frame_snap {
	struct ocelot_vcap_u48 dmac;
	struct ocelot_vcap_u48 smac;

	/* SNAP header: Organization Code at byte 0, Type at byte 3 */
	struct ocelot_vcap_u40 snap;
};

struct ocelot_ace_frame_arp {
	struct ocelot_vcap_u48 smac;
	enum ocelot_vcap_bit arp;	/* Opcode ARP/RARP */
	enum ocelot_vcap_bit req;	/* Opcode request/reply */
	enum ocelot_vcap_bit unknown;    /* Opcode unknown */
	enum ocelot_vcap_bit smac_match; /* Sender MAC matches SMAC */
	enum ocelot_vcap_bit dmac_match; /* Target MAC matches DMAC */

	/**< Protocol addr. length 4, hardware length 6 */
	enum ocelot_vcap_bit length;

	enum ocelot_vcap_bit ip;       /* Protocol address type IP */
	enum  ocelot_vcap_bit ethernet; /* Hardware address type Ethernet */
	struct ocelot_vcap_ipv4 sip;     /* Sender IP address */
	struct ocelot_vcap_ipv4 dip;     /* Target IP address */
};

struct ocelot_ace_frame_ipv4 {
	enum ocelot_vcap_bit ttl;      /* TTL zero */
	enum ocelot_vcap_bit fragment; /* Fragment */
	enum ocelot_vcap_bit options;  /* Header options */
	struct ocelot_vcap_u8 ds;
	struct ocelot_vcap_u8 proto;      /* Protocol */
	struct ocelot_vcap_ipv4 sip;      /* Source IP address */
	struct ocelot_vcap_ipv4 dip;      /* Destination IP address */
	struct ocelot_vcap_u48 data;      /* Not UDP/TCP: IP data */
	struct ocelot_vcap_udp_tcp sport; /* UDP/TCP: Source port */
	struct ocelot_vcap_udp_tcp dport; /* UDP/TCP: Destination port */
	enum ocelot_vcap_bit tcp_fin;
	enum ocelot_vcap_bit tcp_syn;
	enum ocelot_vcap_bit tcp_rst;
	enum ocelot_vcap_bit tcp_psh;
	enum ocelot_vcap_bit tcp_ack;
	enum ocelot_vcap_bit tcp_urg;
	enum ocelot_vcap_bit sip_eq_dip;     /* SIP equals DIP  */
	enum ocelot_vcap_bit sport_eq_dport; /* SPORT equals DPORT  */
	enum ocelot_vcap_bit seq_zero;       /* TCP sequence number is zero */
};

struct ocelot_ace_frame_ipv6 {
	struct ocelot_vcap_u8 proto; /* IPv6 protocol */
	struct ocelot_vcap_u128 sip; /* IPv6 source (byte 0-7 ignored) */
	enum ocelot_vcap_bit ttl;  /* TTL zero */
	struct ocelot_vcap_u8 ds;
	struct ocelot_vcap_u48 data; /* Not UDP/TCP: IP data */
	struct ocelot_vcap_udp_tcp sport;
	struct ocelot_vcap_udp_tcp dport;
	enum ocelot_vcap_bit tcp_fin;
	enum ocelot_vcap_bit tcp_syn;
	enum ocelot_vcap_bit tcp_rst;
	enum ocelot_vcap_bit tcp_psh;
	enum ocelot_vcap_bit tcp_ack;
	enum ocelot_vcap_bit tcp_urg;
	enum ocelot_vcap_bit sip_eq_dip;     /* SIP equals DIP  */
	enum ocelot_vcap_bit sport_eq_dport; /* SPORT equals DPORT  */
	enum ocelot_vcap_bit seq_zero;       /* TCP sequence number is zero */
};

enum ocelot_ace_action {
	OCELOT_ACL_ACTION_DROP,
	OCELOT_ACL_ACTION_TRAP,
};

struct ocelot_ace_stats {
	u64 bytes;
	u64 pkts;
	u64 used;
};

struct ocelot_ace_rule {
	struct list_head list;
	struct ocelot *ocelot;

	u16 prio;
	u32 id;

	enum ocelot_ace_action action;
	struct ocelot_ace_stats stats;
	u16 ingress_port_mask;

	enum ocelot_vcap_bit dmac_mc;
	enum ocelot_vcap_bit dmac_bc;
	struct ocelot_ace_vlan vlan;

	enum ocelot_ace_type type;
	union {
		/* ocelot_ACE_TYPE_ANY: No specific fields */
		struct ocelot_ace_frame_etype etype;
		struct ocelot_ace_frame_llc llc;
		struct ocelot_ace_frame_snap snap;
		struct ocelot_ace_frame_arp arp;
		struct ocelot_ace_frame_ipv4 ipv4;
		struct ocelot_ace_frame_ipv6 ipv6;
	} frame;
};

struct ocelot_acl_block {
	struct list_head rules;
	struct ocelot *ocelot;
	int count;
};

int ocelot_ace_rule_offload_add(struct ocelot_ace_rule *rule);
int ocelot_ace_rule_offload_del(struct ocelot_ace_rule *rule);
int ocelot_ace_rule_stats_update(struct ocelot_ace_rule *rule);

int ocelot_ace_init(struct ocelot *ocelot);
void ocelot_ace_deinit(void);

int ocelot_setup_tc_block_flower_bind(struct ocelot_port_private *priv,
				      struct flow_block_offload *f);
void ocelot_setup_tc_block_flower_unbind(struct ocelot_port_private *priv,
					 struct flow_block_offload *f);

#endif /* _MSCC_OCELOT_ACE_H_ */
