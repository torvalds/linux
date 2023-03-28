/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#ifndef __SPARX5_VCAP_IMPL_H__
#define __SPARX5_VCAP_IMPL_H__

#include <linux/types.h>
#include <linux/list.h>

#include "vcap_api.h"
#include "vcap_api_client.h"

#define SPARX5_VCAP_CID_IS0_L0 VCAP_CID_INGRESS_L0 /* IS0/CLM lookup 0 */
#define SPARX5_VCAP_CID_IS0_L1 VCAP_CID_INGRESS_L1 /* IS0/CLM lookup 1 */
#define SPARX5_VCAP_CID_IS0_L2 VCAP_CID_INGRESS_L2 /* IS0/CLM lookup 2 */
#define SPARX5_VCAP_CID_IS0_L3 VCAP_CID_INGRESS_L3 /* IS0/CLM lookup 3 */
#define SPARX5_VCAP_CID_IS0_L4 VCAP_CID_INGRESS_L4 /* IS0/CLM lookup 4 */
#define SPARX5_VCAP_CID_IS0_L5 VCAP_CID_INGRESS_L5 /* IS0/CLM lookup 5 */
#define SPARX5_VCAP_CID_IS0_MAX \
	(VCAP_CID_INGRESS_L5 + VCAP_CID_LOOKUP_SIZE - 1) /* IS0/CLM Max */

#define SPARX5_VCAP_CID_IS2_L0 VCAP_CID_INGRESS_STAGE2_L0 /* IS2 lookup 0 */
#define SPARX5_VCAP_CID_IS2_L1 VCAP_CID_INGRESS_STAGE2_L1 /* IS2 lookup 1 */
#define SPARX5_VCAP_CID_IS2_L2 VCAP_CID_INGRESS_STAGE2_L2 /* IS2 lookup 2 */
#define SPARX5_VCAP_CID_IS2_L3 VCAP_CID_INGRESS_STAGE2_L3 /* IS2 lookup 3 */
#define SPARX5_VCAP_CID_IS2_MAX \
	(VCAP_CID_INGRESS_STAGE2_L3 + VCAP_CID_LOOKUP_SIZE - 1) /* IS2 Max */

#define SPARX5_VCAP_CID_ES0_L0 VCAP_CID_EGRESS_L0 /* ES0 lookup 0 */
#define SPARX5_VCAP_CID_ES0_MAX (VCAP_CID_EGRESS_L1 - 1) /* ES0 Max */

#define SPARX5_VCAP_CID_ES2_L0 VCAP_CID_EGRESS_STAGE2_L0 /* ES2 lookup 0 */
#define SPARX5_VCAP_CID_ES2_L1 VCAP_CID_EGRESS_STAGE2_L1 /* ES2 lookup 1 */
#define SPARX5_VCAP_CID_ES2_MAX \
	(VCAP_CID_EGRESS_STAGE2_L1 + VCAP_CID_LOOKUP_SIZE - 1) /* ES2 Max */

/* IS0 port keyset selection control */

/* IS0 ethernet, IPv4, IPv6 traffic type keyset generation */
enum vcap_is0_port_sel_etype {
	VCAP_IS0_PS_ETYPE_DEFAULT, /* None or follow depending on class */
	VCAP_IS0_PS_ETYPE_MLL,
	VCAP_IS0_PS_ETYPE_SGL_MLBS,
	VCAP_IS0_PS_ETYPE_DBL_MLBS,
	VCAP_IS0_PS_ETYPE_TRI_MLBS,
	VCAP_IS0_PS_ETYPE_TRI_VID,
	VCAP_IS0_PS_ETYPE_LL_FULL,
	VCAP_IS0_PS_ETYPE_NORMAL_SRC,
	VCAP_IS0_PS_ETYPE_NORMAL_DST,
	VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE,
	VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4,
	VCAP_IS0_PS_ETYPE_PURE_5TUPLE_IP4,
	VCAP_IS0_PS_ETYPE_DBL_VID_IDX,
	VCAP_IS0_PS_ETYPE_ETAG,
	VCAP_IS0_PS_ETYPE_NO_LOOKUP,
};

/* IS0 MPLS traffic type keyset generation */
enum vcap_is0_port_sel_mpls_uc_mc {
	VCAP_IS0_PS_MPLS_FOLLOW_ETYPE,
	VCAP_IS0_PS_MPLS_MLL,
	VCAP_IS0_PS_MPLS_SGL_MLBS,
	VCAP_IS0_PS_MPLS_DBL_MLBS,
	VCAP_IS0_PS_MPLS_TRI_MLBS,
	VCAP_IS0_PS_MPLS_TRI_VID,
	VCAP_IS0_PS_MPLS_LL_FULL,
	VCAP_IS0_PS_MPLS_NORMAL_SRC,
	VCAP_IS0_PS_MPLS_NORMAL_DST,
	VCAP_IS0_PS_MPLS_NORMAL_7TUPLE,
	VCAP_IS0_PS_MPLS_NORMAL_5TUPLE_IP4,
	VCAP_IS0_PS_MPLS_PURE_5TUPLE_IP4,
	VCAP_IS0_PS_MPLS_DBL_VID_IDX,
	VCAP_IS0_PS_MPLS_ETAG,
	VCAP_IS0_PS_MPLS_NO_LOOKUP,
};

/* IS0 MBLS traffic type keyset generation */
enum vcap_is0_port_sel_mlbs {
	VCAP_IS0_PS_MLBS_FOLLOW_ETYPE,
	VCAP_IS0_PS_MLBS_SGL_MLBS,
	VCAP_IS0_PS_MLBS_DBL_MLBS,
	VCAP_IS0_PS_MLBS_TRI_MLBS,
	VCAP_IS0_PS_MLBS_NO_LOOKUP = 17,
};

/* IS2 port keyset selection control */

/* IS2 non-ethernet traffic type keyset generation */
enum vcap_is2_port_sel_noneth {
	VCAP_IS2_PS_NONETH_MAC_ETYPE,
	VCAP_IS2_PS_NONETH_CUSTOM_1,
	VCAP_IS2_PS_NONETH_CUSTOM_2,
	VCAP_IS2_PS_NONETH_NO_LOOKUP
};

/* IS2 IPv4 unicast traffic type keyset generation */
enum vcap_is2_port_sel_ipv4_uc {
	VCAP_IS2_PS_IPV4_UC_MAC_ETYPE,
	VCAP_IS2_PS_IPV4_UC_IP4_TCP_UDP_OTHER,
	VCAP_IS2_PS_IPV4_UC_IP_7TUPLE,
};

/* IS2 IPv4 multicast traffic type keyset generation */
enum vcap_is2_port_sel_ipv4_mc {
	VCAP_IS2_PS_IPV4_MC_MAC_ETYPE,
	VCAP_IS2_PS_IPV4_MC_IP4_TCP_UDP_OTHER,
	VCAP_IS2_PS_IPV4_MC_IP_7TUPLE,
	VCAP_IS2_PS_IPV4_MC_IP4_VID,
};

/* IS2 IPv6 unicast traffic type keyset generation */
enum vcap_is2_port_sel_ipv6_uc {
	VCAP_IS2_PS_IPV6_UC_MAC_ETYPE,
	VCAP_IS2_PS_IPV6_UC_IP_7TUPLE,
	VCAP_IS2_PS_IPV6_UC_IP6_STD,
	VCAP_IS2_PS_IPV6_UC_IP4_TCP_UDP_OTHER,
};

/* IS2 IPv6 multicast traffic type keyset generation */
enum vcap_is2_port_sel_ipv6_mc {
	VCAP_IS2_PS_IPV6_MC_MAC_ETYPE,
	VCAP_IS2_PS_IPV6_MC_IP_7TUPLE,
	VCAP_IS2_PS_IPV6_MC_IP6_VID,
	VCAP_IS2_PS_IPV6_MC_IP6_STD,
	VCAP_IS2_PS_IPV6_MC_IP4_TCP_UDP_OTHER,
};

/* IS2 ARP traffic type keyset generation */
enum vcap_is2_port_sel_arp {
	VCAP_IS2_PS_ARP_MAC_ETYPE,
	VCAP_IS2_PS_ARP_ARP,
};

/* ES0 port keyset selection control */

/* ES0 Egress port traffic type classification */
enum vcap_es0_port_sel {
	VCAP_ES0_PS_NORMAL_SELECTION,
	VCAP_ES0_PS_FORCE_ISDX_LOOKUPS,
	VCAP_ES0_PS_FORCE_VID_LOOKUPS,
	VCAP_ES0_PS_RESERVED,
};

/* ES2 port keyset selection control */

/* ES2 IPv4 traffic type keyset generation */
enum vcap_es2_port_sel_ipv4 {
	VCAP_ES2_PS_IPV4_MAC_ETYPE,
	VCAP_ES2_PS_IPV4_IP_7TUPLE,
	VCAP_ES2_PS_IPV4_IP4_TCP_UDP_VID,
	VCAP_ES2_PS_IPV4_IP4_TCP_UDP_OTHER,
	VCAP_ES2_PS_IPV4_IP4_VID,
	VCAP_ES2_PS_IPV4_IP4_OTHER,
};

/* ES2 IPv6 traffic type keyset generation */
enum vcap_es2_port_sel_ipv6 {
	VCAP_ES2_PS_IPV6_MAC_ETYPE,
	VCAP_ES2_PS_IPV6_IP_7TUPLE,
	VCAP_ES2_PS_IPV6_IP_7TUPLE_VID,
	VCAP_ES2_PS_IPV6_IP_7TUPLE_STD,
	VCAP_ES2_PS_IPV6_IP6_VID,
	VCAP_ES2_PS_IPV6_IP6_STD,
	VCAP_ES2_PS_IPV6_IP4_DOWNGRADE,
};

/* ES2 ARP traffic type keyset generation */
enum vcap_es2_port_sel_arp {
	VCAP_ES2_PS_ARP_MAC_ETYPE,
	VCAP_ES2_PS_ARP_ARP,
};

/* Selects TPID for ES0 matching */
enum SPX5_TPID_SEL {
	SPX5_TPID_SEL_UNTAGGED,
	SPX5_TPID_SEL_8100,
	SPX5_TPID_SEL_UNUSED_0,
	SPX5_TPID_SEL_UNUSED_1,
	SPX5_TPID_SEL_88A8,
	SPX5_TPID_SEL_TPIDCFG_1,
	SPX5_TPID_SEL_TPIDCFG_2,
	SPX5_TPID_SEL_TPIDCFG_3,
};

/* Get the port keyset for the vcap lookup */
int sparx5_vcap_get_port_keyset(struct net_device *ndev,
				struct vcap_admin *admin,
				int cid,
				u16 l3_proto,
				struct vcap_keyset_list *kslist);

/* Check if the ethertype is supported by the vcap port classification */
bool sparx5_vcap_is_known_etype(struct vcap_admin *admin, u16 etype);

#endif /* __SPARX5_VCAP_IMPL_H__ */
