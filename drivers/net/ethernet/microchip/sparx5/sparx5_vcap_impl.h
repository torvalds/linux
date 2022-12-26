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

#define SPARX5_VCAP_CID_IS2_L0 VCAP_CID_INGRESS_STAGE2_L0 /* IS2 lookup 0 */
#define SPARX5_VCAP_CID_IS2_L1 VCAP_CID_INGRESS_STAGE2_L1 /* IS2 lookup 1 */
#define SPARX5_VCAP_CID_IS2_L2 VCAP_CID_INGRESS_STAGE2_L2 /* IS2 lookup 2 */
#define SPARX5_VCAP_CID_IS2_L3 VCAP_CID_INGRESS_STAGE2_L3 /* IS2 lookup 3 */
#define SPARX5_VCAP_CID_IS2_MAX \
	(VCAP_CID_INGRESS_STAGE2_L3 + VCAP_CID_LOOKUP_SIZE - 1) /* IS2 Max */

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

/* Get the port keyset for the vcap lookup */
int sparx5_vcap_get_port_keyset(struct net_device *ndev,
				struct vcap_admin *admin,
				int cid,
				u16 l3_proto,
				struct vcap_keyset_list *kslist);

#endif /* __SPARX5_VCAP_IMPL_H__ */
