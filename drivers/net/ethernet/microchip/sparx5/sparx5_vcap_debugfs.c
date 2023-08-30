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

static const char *sparx5_vcap_is0_etype_str(u32 value)
{
	switch (value) {
	case VCAP_IS0_PS_ETYPE_DEFAULT:
		return "default";
	case VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE:
		return "normal_7tuple";
	case VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4:
		return "normal_5tuple_ip4";
	case VCAP_IS0_PS_ETYPE_MLL:
		return "mll";
	case VCAP_IS0_PS_ETYPE_LL_FULL:
		return "ll_full";
	case VCAP_IS0_PS_ETYPE_PURE_5TUPLE_IP4:
		return "pure_5tuple_ip4";
	case VCAP_IS0_PS_ETYPE_ETAG:
		return "etag";
	case VCAP_IS0_PS_ETYPE_NO_LOOKUP:
		return "no lookup";
	default:
		return "unknown";
	}
}

static const char *sparx5_vcap_is0_mpls_str(u32 value)
{
	switch (value) {
	case VCAP_IS0_PS_MPLS_FOLLOW_ETYPE:
		return "follow_etype";
	case VCAP_IS0_PS_MPLS_NORMAL_7TUPLE:
		return "normal_7tuple";
	case VCAP_IS0_PS_MPLS_NORMAL_5TUPLE_IP4:
		return "normal_5tuple_ip4";
	case VCAP_IS0_PS_MPLS_MLL:
		return "mll";
	case VCAP_IS0_PS_MPLS_LL_FULL:
		return "ll_full";
	case VCAP_IS0_PS_MPLS_PURE_5TUPLE_IP4:
		return "pure_5tuple_ip4";
	case VCAP_IS0_PS_MPLS_ETAG:
		return "etag";
	case VCAP_IS0_PS_MPLS_NO_LOOKUP:
		return "no lookup";
	default:
		return "unknown";
	}
}

static const char *sparx5_vcap_is0_mlbs_str(u32 value)
{
	switch (value) {
	case VCAP_IS0_PS_MLBS_FOLLOW_ETYPE:
		return "follow_etype";
	case VCAP_IS0_PS_MLBS_NO_LOOKUP:
		return "no lookup";
	default:
		return "unknown";
	}
}

static void sparx5_vcap_is0_port_keys(struct sparx5 *sparx5,
				      struct vcap_admin *admin,
				      struct sparx5_port *port,
				      struct vcap_output_print *out)
{
	int lookup;
	u32 value, val;

	out->prf(out->dst, "  port[%02d] (%s): ", port->portno,
		 netdev_name(port->ndev));
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		out->prf(out->dst, "\n    Lookup %d: ", lookup);

		/* Get lookup state */
		value = spx5_rd(sparx5,
				ANA_CL_ADV_CL_CFG(port->portno, lookup));
		out->prf(out->dst, "\n      state: ");
		if (ANA_CL_ADV_CL_CFG_LOOKUP_ENA_GET(value))
			out->prf(out->dst, "on");
		else
			out->prf(out->dst, "off");
		val = ANA_CL_ADV_CL_CFG_ETYPE_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      etype: %s",
			 sparx5_vcap_is0_etype_str(val));
		val = ANA_CL_ADV_CL_CFG_IP4_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      ipv4: %s",
			 sparx5_vcap_is0_etype_str(val));
		val = ANA_CL_ADV_CL_CFG_IP6_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      ipv6: %s",
			 sparx5_vcap_is0_etype_str(val));
		val = ANA_CL_ADV_CL_CFG_MPLS_UC_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      mpls_uc: %s",
			 sparx5_vcap_is0_mpls_str(val));
		val = ANA_CL_ADV_CL_CFG_MPLS_MC_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      mpls_mc: %s",
			 sparx5_vcap_is0_mpls_str(val));
		val = ANA_CL_ADV_CL_CFG_MLBS_CLM_KEY_SEL_GET(value);
		out->prf(out->dst, "\n      mlbs: %s",
			 sparx5_vcap_is0_mlbs_str(val));
	}
	out->prf(out->dst, "\n");
}

static void sparx5_vcap_is2_port_keys(struct sparx5 *sparx5,
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
		if (ANA_ACL_VCAP_S2_CFG_SEC_ENA_GET(value) & BIT(lookup))
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
			out->prf(out->dst, "ip4_tcp_udp ip4_other");
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

static void sparx5_vcap_is2_port_stickies(struct sparx5 *sparx5,
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

static void sparx5_vcap_es0_port_keys(struct sparx5 *sparx5,
				      struct vcap_admin *admin,
				      struct sparx5_port *port,
				      struct vcap_output_print *out)
{
	u32 value;

	out->prf(out->dst, "  port[%02d] (%s): ", port->portno,
		 netdev_name(port->ndev));
	out->prf(out->dst, "\n    Lookup 0: ");

	/* Get lookup state */
	value = spx5_rd(sparx5, REW_ES0_CTRL);
	out->prf(out->dst, "\n      state: ");
	if (REW_ES0_CTRL_ES0_LU_ENA_GET(value))
		out->prf(out->dst, "on");
	else
		out->prf(out->dst, "off");

	out->prf(out->dst, "\n      keyset: ");
	value = spx5_rd(sparx5, REW_RTAG_ETAG_CTRL(port->portno));
	switch (REW_RTAG_ETAG_CTRL_ES0_ISDX_KEY_ENA_GET(value)) {
	case VCAP_ES0_PS_NORMAL_SELECTION:
		out->prf(out->dst, "normal");
		break;
	case VCAP_ES0_PS_FORCE_ISDX_LOOKUPS:
		out->prf(out->dst, "isdx");
		break;
	case VCAP_ES0_PS_FORCE_VID_LOOKUPS:
		out->prf(out->dst, "vid");
		break;
	case VCAP_ES0_PS_RESERVED:
		out->prf(out->dst, "reserved");
		break;
	}
	out->prf(out->dst, "\n");
}

static void sparx5_vcap_es2_port_keys(struct sparx5 *sparx5,
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
		value = spx5_rd(sparx5, EACL_VCAP_ES2_KEY_SEL(port->portno,
							      lookup));
		out->prf(out->dst, "\n      state: ");
		if (EACL_VCAP_ES2_KEY_SEL_KEY_ENA_GET(value))
			out->prf(out->dst, "on");
		else
			out->prf(out->dst, "off");

		out->prf(out->dst, "\n      arp: ");
		switch (EACL_VCAP_ES2_KEY_SEL_ARP_KEY_SEL_GET(value)) {
		case VCAP_ES2_PS_ARP_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_ES2_PS_ARP_ARP:
			out->prf(out->dst, "arp");
			break;
		}
		out->prf(out->dst, "\n      ipv4: ");
		switch (EACL_VCAP_ES2_KEY_SEL_IP4_KEY_SEL_GET(value)) {
		case VCAP_ES2_PS_IPV4_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_ES2_PS_IPV4_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		case VCAP_ES2_PS_IPV4_IP4_TCP_UDP_VID:
			out->prf(out->dst, "ip4_tcp_udp ip4_vid");
			break;
		case VCAP_ES2_PS_IPV4_IP4_TCP_UDP_OTHER:
			out->prf(out->dst, "ip4_tcp_udp ip4_other");
			break;
		case VCAP_ES2_PS_IPV4_IP4_VID:
			out->prf(out->dst, "ip4_vid");
			break;
		case VCAP_ES2_PS_IPV4_IP4_OTHER:
			out->prf(out->dst, "ip4_other");
			break;
		}
		out->prf(out->dst, "\n      ipv6: ");
		switch (EACL_VCAP_ES2_KEY_SEL_IP6_KEY_SEL_GET(value)) {
		case VCAP_ES2_PS_IPV6_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE:
			out->prf(out->dst, "ip_7tuple");
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE_VID:
			out->prf(out->dst, "ip_7tuple ip6_vid");
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE_STD:
			out->prf(out->dst, "ip_7tuple ip6_std");
			break;
		case VCAP_ES2_PS_IPV6_IP6_VID:
			out->prf(out->dst, "ip6_vid");
			break;
		case VCAP_ES2_PS_IPV6_IP6_STD:
			out->prf(out->dst, "ip6_std");
			break;
		case VCAP_ES2_PS_IPV6_IP4_DOWNGRADE:
			out->prf(out->dst, "ip4_downgrade");
			break;
		}
	}
	out->prf(out->dst, "\n");
}

static void sparx5_vcap_es2_port_stickies(struct sparx5 *sparx5,
					  struct vcap_admin *admin,
					  struct vcap_output_print *out)
{
	int lookup;
	u32 value;

	out->prf(out->dst, "  Sticky bits: ");
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		value = spx5_rd(sparx5, EACL_SEC_LOOKUP_STICKY(lookup));
		out->prf(out->dst, "\n    Lookup %d: ", lookup);
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP_7TUPLE_STICKY_GET(value))
			out->prf(out->dst, " ip_7tuple");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP6_VID_STICKY_GET(value))
			out->prf(out->dst, " ip6_vid");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP6_STD_STICKY_GET(value))
			out->prf(out->dst, " ip6_std");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_TCPUDP_STICKY_GET(value))
			out->prf(out->dst, " ip4_tcp_udp");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_VID_STICKY_GET(value))
			out->prf(out->dst, " ip4_vid");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_IP4_OTHER_STICKY_GET(value))
			out->prf(out->dst, " ip4_other");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_ARP_STICKY_GET(value))
			out->prf(out->dst, " arp");
		if (EACL_SEC_LOOKUP_STICKY_SEC_TYPE_MAC_ETYPE_STICKY_GET(value))
			out->prf(out->dst, " mac_etype");
		/* Clear stickies */
		spx5_wr(value, sparx5, EACL_SEC_LOOKUP_STICKY(lookup));
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
	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		sparx5_vcap_is0_port_keys(sparx5, admin, port, out);
		break;
	case VCAP_TYPE_IS2:
		sparx5_vcap_is2_port_keys(sparx5, admin, port, out);
		sparx5_vcap_is2_port_stickies(sparx5, admin, out);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_port_keys(sparx5, admin, port, out);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_port_keys(sparx5, admin, port, out);
		sparx5_vcap_es2_port_stickies(sparx5, admin, out);
		break;
	default:
		out->prf(out->dst, "  no info\n");
		break;
	}
	return 0;
}
