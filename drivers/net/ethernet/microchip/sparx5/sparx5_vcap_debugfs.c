// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver VCAP debugFS implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/types.h>
#include <linux/list.h>

#include "sparx5_vcap_debugfs.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"
#include "sparx5_vcap_ag_api.h"

static void sparx5_vcap_port_keys(struct sparx5 *sparx5,
				  struct vcap_admin *admin,
				  struct sparx5_port *port,
				  struct vcap_output_print *out)
{
	int lookup;
	u32 value;

	out->prf(out->dst, "  port[%02d] (%s): ", port->portno,
	   netdev_name(port->ndev));
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		out->prf(out->dst, "\n    Lookup %d: ", lookup);

		/* Get lookup state */
		value = spx5_rd(sparx5, ANA_ACL_VCAP_S2_CFG(port->portno));
		out->prf(out->dst, "\n      state: ");
		if (ANA_ACL_VCAP_S2_CFG_SEC_ENA_GET(value))
			out->prf(out->dst, "on");
		else
			out->prf(out->dst, "off");

		/* Get key selection state */
		value = spx5_rd(sparx5,
				ANA_ACL_VCAP_S2_KEY_SEL(port->portno, lookup));

		out->prf(out->dst, "\n      noneth: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_NON_ETH_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_NONETH_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_NONETH_CUSTOM_1:
			out->prf(out->dst, "custom1");
			break;
		case VCAP_IS2_PS_NONETH_CUSTOM_2:
			out->prf(out->dst, "custom2");
			break;
		case VCAP_IS2_PS_NONETH_NO_LOOKUP:
			out->prf(out->dst, "none");
			break;
		}
		out->prf(out->dst, "\n      ipv4_mc: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP4_MC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV4_MC_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_IPV4_MC_IP4_TCP_UDP_OTHER:
			out->prf(out->dst, "ip4_tcp_udp ip4_other");
			break;
		case VCAP_IS2_PS_IPV4_MC_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		case VCAP_IS2_PS_IPV4_MC_IP4_VID:
			out->prf(out->dst, "ip4_vid");
			break;
		}
		out->prf(out->dst, "\n      ipv4_uc: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP4_UC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV4_UC_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_IPV4_UC_IP4_TCP_UDP_OTHER:
			out->prf(out->dst, "ip4_tcp_udp ip4_other");
			break;
		case VCAP_IS2_PS_IPV4_UC_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		}
		out->prf(out->dst, "\n      ipv6_mc: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP6_MC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV6_MC_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_IPV6_MC_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		case VCAP_IS2_PS_IPV6_MC_IP6_VID:
			out->prf(out->dst, "ip6_vid");
			break;
		case VCAP_IS2_PS_IPV6_MC_IP6_STD:
			out->prf(out->dst, "ip6_std");
			break;
		case VCAP_IS2_PS_IPV6_MC_IP4_TCP_UDP_OTHER:
			out->prf(out->dst, "ip4_tcp_udp ipv4_other");
			break;
		}
		out->prf(out->dst, "\n      ipv6_uc: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP6_UC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV6_UC_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_IPV6_UC_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		case VCAP_IS2_PS_IPV6_UC_IP6_STD:
			out->prf(out->dst, "ip6_std");
			break;
		case VCAP_IS2_PS_IPV6_UC_IP4_TCP_UDP_OTHER:
			out->prf(out->dst, "ip4_tcp_udp ip4_other");
			break;
		}
		out->prf(out->dst, "\n      arp: ");
		switch (ANA_ACL_VCAP_S2_KEY_SEL_ARP_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_ARP_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_IS2_PS_ARP_ARP:
			out->prf(out->dst, "arp");
			break;
		}
	}
	out->prf(out->dst, "\n");
}

static void sparx5_vcap_port_stickies(struct sparx5 *sparx5,
				      struct vcap_admin *admin,
				      struct vcap_output_print *out)
{
	int lookup;
	u32 value;

	out->prf(out->dst, "  Sticky bits: ");
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		out->prf(out->dst, "\n    Lookup %d: ", lookup);
		/* Get lookup sticky bits */
		value = spx5_rd(sparx5, ANA_ACL_SEC_LOOKUP_STICKY(lookup));

		if (ANA_ACL_SEC_LOOKUP_STICKY_KEY_SEL_CLM_STICKY_GET(value))
			out->prf(out->dst, " sel_clm");
		if (ANA_ACL_SEC_LOOKUP_STICKY_KEY_SEL_IRLEG_STICKY_GET(value))
			out->prf(out->dst, " sel_irleg");
		if (ANA_ACL_SEC_LOOKUP_STICKY_KEY_SEL_ERLEG_STICKY_GET(value))
			out->prf(out->dst, " sel_erleg");
		if (ANA_ACL_SEC_LOOKUP_STICKY_KEY_SEL_PORT_STICKY_GET(value))
			out->prf(out->dst, " sel_port");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_CUSTOM2_STICKY_GET(value))
			out->prf(out->dst, " custom2");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_CUSTOM1_STICKY_GET(value))
			out->prf(out->dst, " custom1");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_OAM_STICKY_GET(value))
			out->prf(out->dst, " oam");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP6_VID_STICKY_GET(value))
			out->prf(out->dst, " ip6_vid");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP6_STD_STICKY_GET(value))
			out->prf(out->dst, " ip6_std");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP6_TCPUDP_STICKY_GET(value))
			out->prf(out->dst, " ip6_tcpudp");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP_7TUPLE_STICKY_GET(value))
			out->prf(out->dst, " ip_7tuple");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_VID_STICKY_GET(value))
			out->prf(out->dst, " ip4_vid");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_TCPUDP_STICKY_GET(value))
			out->prf(out->dst, " ip4_tcpudp");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_OTHER_STICKY_GET(value))
			out->prf(out->dst, " ip4_other");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_ARP_STICKY_GET(value))
			out->prf(out->dst, " arp");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_MAC_SNAP_STICKY_GET(value))
			out->prf(out->dst, " mac_snap");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_MAC_LLC_STICKY_GET(value))
			out->prf(out->dst, " mac_llc");
		if (ANA_ACL_SEC_LOOKUP_STICKY_SEC_TYPE_MAC_ETYPE_STICKY_GET(value))
			out->prf(out->dst, " mac_etype");
		/* Clear stickies */
		spx5_wr(value, sparx5, ANA_ACL_SEC_LOOKUP_STICKY(lookup));
	}
	out->prf(out->dst, "\n");
}

/* Provide port information via a callback interface */
int sparx5_port_info(struct net_device *ndev,
		     struct vcap_admin *admin,
		     struct vcap_output_print *out)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	const struct vcap_info *vcap;
	struct vcap_control *vctrl;

	vctrl = sparx5->vcap_ctrl;
	vcap = &vctrl->vcaps[admin->vtype];
	out->prf(out->dst, "%s:\n", vcap->name);
	sparx5_vcap_port_keys(sparx5, admin, port, out);
	sparx5_vcap_port_stickies(sparx5, admin, out);
	return 0;
}
