// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "lan966x_vcap_ag_api.h"
#include "vcap_api.h"
#include "vcap_api_client.h"

static void lan966x_vcap_is1_port_keys(struct lan966x_port *port,
				       struct vcap_admin *admin,
				       struct vcap_output_print *out)
{
	struct lan966x *lan966x = port->lan966x;
	u32 val;

	out->prf(out->dst, "  port[%d] (%s): ", port->chip_port,
		 netdev_name(port->dev));

	val = lan_rd(lan966x, ANA_VCAP_CFG(port->chip_port));
	out->prf(out->dst, "\n    state: ");
	if (ANA_VCAP_CFG_S1_ENA_GET(val))
		out->prf(out->dst, "on");
	else
		out->prf(out->dst, "off");

	for (int l = 0; l < admin->lookups; ++l) {
		out->prf(out->dst, "\n    Lookup %d: ", l);

		out->prf(out->dst, "\n      other: ");
		switch (ANA_VCAP_S1_CFG_KEY_OTHER_CFG_GET(val)) {
		case VCAP_IS1_PS_OTHER_NORMAL:
			out->prf(out->dst, "normal");
			break;
		case VCAP_IS1_PS_OTHER_7TUPLE:
			out->prf(out->dst, "7tuple");
			break;
		case VCAP_IS1_PS_OTHER_DBL_VID:
			out->prf(out->dst, "dbl_vid");
			break;
		case VCAP_IS1_PS_OTHER_DMAC_VID:
			out->prf(out->dst, "dmac_vid");
			break;
		default:
			out->prf(out->dst, "-");
			break;
		}

		out->prf(out->dst, "\n      ipv4: ");
		switch (ANA_VCAP_S1_CFG_KEY_IP4_CFG_GET(val)) {
		case VCAP_IS1_PS_IPV4_NORMAL:
			out->prf(out->dst, "normal");
			break;
		case VCAP_IS1_PS_IPV4_7TUPLE:
			out->prf(out->dst, "7tuple");
			break;
		case VCAP_IS1_PS_IPV4_5TUPLE_IP4:
			out->prf(out->dst, "5tuple_ipv4");
			break;
		case VCAP_IS1_PS_IPV4_DBL_VID:
			out->prf(out->dst, "dbl_vid");
			break;
		case VCAP_IS1_PS_IPV4_DMAC_VID:
			out->prf(out->dst, "dmac_vid");
			break;
		default:
			out->prf(out->dst, "-");
			break;
		}

		out->prf(out->dst, "\n      ipv6: ");
		switch (ANA_VCAP_S1_CFG_KEY_IP6_CFG_GET(val)) {
		case VCAP_IS1_PS_IPV6_NORMAL:
			out->prf(out->dst, "normal");
			break;
		case VCAP_IS1_PS_IPV6_7TUPLE:
			out->prf(out->dst, "7tuple");
			break;
		case VCAP_IS1_PS_IPV6_5TUPLE_IP4:
			out->prf(out->dst, "5tuple_ip4");
			break;
		case VCAP_IS1_PS_IPV6_NORMAL_IP6:
			out->prf(out->dst, "normal_ip6");
			break;
		case VCAP_IS1_PS_IPV6_5TUPLE_IP6:
			out->prf(out->dst, "5tuple_ip6");
			break;
		case VCAP_IS1_PS_IPV6_DBL_VID:
			out->prf(out->dst, "dbl_vid");
			break;
		case VCAP_IS1_PS_IPV6_DMAC_VID:
			out->prf(out->dst, "dmac_vid");
			break;
		default:
			out->prf(out->dst, "-");
			break;
		}

		out->prf(out->dst, "\n      rt: ");
		switch (ANA_VCAP_S1_CFG_KEY_RT_CFG_GET(val)) {
		case VCAP_IS1_PS_RT_NORMAL:
			out->prf(out->dst, "normal");
			break;
		case VCAP_IS1_PS_RT_7TUPLE:
			out->prf(out->dst, "7tuple");
			break;
		case VCAP_IS1_PS_RT_DBL_VID:
			out->prf(out->dst, "dbl_vid");
			break;
		case VCAP_IS1_PS_RT_DMAC_VID:
			out->prf(out->dst, "dmac_vid");
			break;
		case VCAP_IS1_PS_RT_FOLLOW_OTHER:
			out->prf(out->dst, "follow_other");
			break;
		default:
			out->prf(out->dst, "-");
			break;
		}
	}

	out->prf(out->dst, "\n");
}

static void lan966x_vcap_is2_port_keys(struct lan966x_port *port,
				       struct vcap_admin *admin,
				       struct vcap_output_print *out)
{
	struct lan966x *lan966x = port->lan966x;
	u32 val;

	out->prf(out->dst, "  port[%d] (%s): ", port->chip_port,
		 netdev_name(port->dev));

	val = lan_rd(lan966x, ANA_VCAP_S2_CFG(port->chip_port));
	out->prf(out->dst, "\n    state: ");
	if (ANA_VCAP_S2_CFG_ENA_GET(val))
		out->prf(out->dst, "on");
	else
		out->prf(out->dst, "off");

	for (int l = 0; l < admin->lookups; ++l) {
		out->prf(out->dst, "\n    Lookup %d: ", l);

		out->prf(out->dst, "\n      snap: ");
		if (ANA_VCAP_S2_CFG_SNAP_DIS_GET(val) & (BIT(0) << l))
			out->prf(out->dst, "mac_llc");
		else
			out->prf(out->dst, "mac_snap");

		out->prf(out->dst, "\n      oam: ");
		if (ANA_VCAP_S2_CFG_OAM_DIS_GET(val) & (BIT(0) << l))
			out->prf(out->dst, "mac_etype");
		else
			out->prf(out->dst, "mac_oam");

		out->prf(out->dst, "\n      arp: ");
		if (ANA_VCAP_S2_CFG_ARP_DIS_GET(val) & (BIT(0) << l))
			out->prf(out->dst, "mac_etype");
		else
			out->prf(out->dst, "mac_arp");

		out->prf(out->dst, "\n      ipv4_other: ");
		if (ANA_VCAP_S2_CFG_IP_OTHER_DIS_GET(val) & (BIT(0) << l))
			out->prf(out->dst, "mac_etype");
		else
			out->prf(out->dst, "ip4_other");

		out->prf(out->dst, "\n      ipv4_tcp_udp: ");
		if (ANA_VCAP_S2_CFG_IP_TCPUDP_DIS_GET(val) & (BIT(0) << l))
			out->prf(out->dst, "mac_etype");
		else
			out->prf(out->dst, "ipv4_tcp_udp");

		out->prf(out->dst, "\n      ipv6: ");
		switch (ANA_VCAP_S2_CFG_IP6_CFG_GET(val) & (0x3 << l)) {
		case VCAP_IS2_PS_IPV6_TCPUDP_OTHER:
			out->prf(out->dst, "ipv6_tcp_udp ipv6_tcp_udp");
			break;
		case VCAP_IS2_PS_IPV6_STD:
			out->prf(out->dst, "ipv6_std");
			break;
		case VCAP_IS2_PS_IPV6_IP4_TCPUDP_IP4_OTHER:
			out->prf(out->dst, "ipv4_tcp_udp ipv4_tcp_udp");
			break;
		case VCAP_IS2_PS_IPV6_MAC_ETYPE:
			out->prf(out->dst, "mac_etype");
			break;
		}
	}

	out->prf(out->dst, "\n");
}

int lan966x_vcap_port_info(struct net_device *dev,
			   struct vcap_admin *admin,
			   struct vcap_output_print *out)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	const struct vcap_info *vcap;
	struct vcap_control *vctrl;

	vctrl = lan966x->vcap_ctrl;
	vcap = &vctrl->vcaps[admin->vtype];

	out->prf(out->dst, "%s:\n", vcap->name);
	switch (admin->vtype) {
	case VCAP_TYPE_IS2:
		lan966x_vcap_is2_port_keys(port, admin, out);
		break;
	case VCAP_TYPE_IS1:
		lan966x_vcap_is1_port_keys(port, admin, out);
		break;
	default:
		out->prf(out->dst, "  no info\n");
		break;
	}

	return 0;
}
