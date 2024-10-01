// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2024 Google LLC
 */

#include "gve.h"
#include "gve_adminq.h"

static
int gve_fill_ethtool_flow_spec(struct ethtool_rx_flow_spec *fsp,
			       struct gve_adminq_queried_flow_rule *rule)
{
	struct gve_adminq_flow_rule *flow_rule = &rule->flow_rule;
	static const u16 flow_type_lut[] = {
		[GVE_FLOW_TYPE_TCPV4]	= TCP_V4_FLOW,
		[GVE_FLOW_TYPE_UDPV4]	= UDP_V4_FLOW,
		[GVE_FLOW_TYPE_SCTPV4]	= SCTP_V4_FLOW,
		[GVE_FLOW_TYPE_AHV4]	= AH_V4_FLOW,
		[GVE_FLOW_TYPE_ESPV4]	= ESP_V4_FLOW,
		[GVE_FLOW_TYPE_TCPV6]	= TCP_V6_FLOW,
		[GVE_FLOW_TYPE_UDPV6]	= UDP_V6_FLOW,
		[GVE_FLOW_TYPE_SCTPV6]	= SCTP_V6_FLOW,
		[GVE_FLOW_TYPE_AHV6]	= AH_V6_FLOW,
		[GVE_FLOW_TYPE_ESPV6]	= ESP_V6_FLOW,
	};

	if (be16_to_cpu(flow_rule->flow_type) >= ARRAY_SIZE(flow_type_lut))
		return -EINVAL;

	fsp->flow_type = flow_type_lut[be16_to_cpu(flow_rule->flow_type)];

	memset(&fsp->h_u, 0, sizeof(fsp->h_u));
	memset(&fsp->h_ext, 0, sizeof(fsp->h_ext));
	memset(&fsp->m_u, 0, sizeof(fsp->m_u));
	memset(&fsp->m_ext, 0, sizeof(fsp->m_ext));

	switch (fsp->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		fsp->h_u.tcp_ip4_spec.ip4src = flow_rule->key.src_ip[0];
		fsp->h_u.tcp_ip4_spec.ip4dst = flow_rule->key.dst_ip[0];
		fsp->h_u.tcp_ip4_spec.psrc = flow_rule->key.src_port;
		fsp->h_u.tcp_ip4_spec.pdst = flow_rule->key.dst_port;
		fsp->h_u.tcp_ip4_spec.tos = flow_rule->key.tos;
		fsp->m_u.tcp_ip4_spec.ip4src = flow_rule->mask.src_ip[0];
		fsp->m_u.tcp_ip4_spec.ip4dst = flow_rule->mask.dst_ip[0];
		fsp->m_u.tcp_ip4_spec.psrc = flow_rule->mask.src_port;
		fsp->m_u.tcp_ip4_spec.pdst = flow_rule->mask.dst_port;
		fsp->m_u.tcp_ip4_spec.tos = flow_rule->mask.tos;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		fsp->h_u.ah_ip4_spec.ip4src = flow_rule->key.src_ip[0];
		fsp->h_u.ah_ip4_spec.ip4dst = flow_rule->key.dst_ip[0];
		fsp->h_u.ah_ip4_spec.spi = flow_rule->key.spi;
		fsp->h_u.ah_ip4_spec.tos = flow_rule->key.tos;
		fsp->m_u.ah_ip4_spec.ip4src = flow_rule->mask.src_ip[0];
		fsp->m_u.ah_ip4_spec.ip4dst = flow_rule->mask.dst_ip[0];
		fsp->m_u.ah_ip4_spec.spi = flow_rule->mask.spi;
		fsp->m_u.ah_ip4_spec.tos = flow_rule->mask.tos;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(fsp->h_u.tcp_ip6_spec.ip6src, &flow_rule->key.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.tcp_ip6_spec.ip6dst, &flow_rule->key.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.tcp_ip6_spec.psrc = flow_rule->key.src_port;
		fsp->h_u.tcp_ip6_spec.pdst = flow_rule->key.dst_port;
		fsp->h_u.tcp_ip6_spec.tclass = flow_rule->key.tclass;
		memcpy(fsp->m_u.tcp_ip6_spec.ip6src, &flow_rule->mask.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.tcp_ip6_spec.ip6dst, &flow_rule->mask.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.tcp_ip6_spec.psrc = flow_rule->mask.src_port;
		fsp->m_u.tcp_ip6_spec.pdst = flow_rule->mask.dst_port;
		fsp->m_u.tcp_ip6_spec.tclass = flow_rule->mask.tclass;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(fsp->h_u.ah_ip6_spec.ip6src, &flow_rule->key.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->h_u.ah_ip6_spec.ip6dst, &flow_rule->key.dst_ip,
		       sizeof(struct in6_addr));
		fsp->h_u.ah_ip6_spec.spi = flow_rule->key.spi;
		fsp->h_u.ah_ip6_spec.tclass = flow_rule->key.tclass;
		memcpy(fsp->m_u.ah_ip6_spec.ip6src, &flow_rule->mask.src_ip,
		       sizeof(struct in6_addr));
		memcpy(fsp->m_u.ah_ip6_spec.ip6dst, &flow_rule->mask.dst_ip,
		       sizeof(struct in6_addr));
		fsp->m_u.ah_ip6_spec.spi = flow_rule->mask.spi;
		fsp->m_u.ah_ip6_spec.tclass = flow_rule->mask.tclass;
		break;
	default:
		return -EINVAL;
	}

	fsp->ring_cookie = be16_to_cpu(flow_rule->action);

	return 0;
}

static int gve_generate_flow_rule(struct gve_priv *priv, struct ethtool_rx_flow_spec *fsp,
				  struct gve_adminq_flow_rule *rule)
{
	static const u16 flow_type_lut[] = {
		[TCP_V4_FLOW]	= GVE_FLOW_TYPE_TCPV4,
		[UDP_V4_FLOW]	= GVE_FLOW_TYPE_UDPV4,
		[SCTP_V4_FLOW]	= GVE_FLOW_TYPE_SCTPV4,
		[AH_V4_FLOW]	= GVE_FLOW_TYPE_AHV4,
		[ESP_V4_FLOW]	= GVE_FLOW_TYPE_ESPV4,
		[TCP_V6_FLOW]	= GVE_FLOW_TYPE_TCPV6,
		[UDP_V6_FLOW]	= GVE_FLOW_TYPE_UDPV6,
		[SCTP_V6_FLOW]	= GVE_FLOW_TYPE_SCTPV6,
		[AH_V6_FLOW]	= GVE_FLOW_TYPE_AHV6,
		[ESP_V6_FLOW]	= GVE_FLOW_TYPE_ESPV6,
	};
	u32 flow_type;

	if (fsp->ring_cookie == RX_CLS_FLOW_DISC)
		return -EOPNOTSUPP;

	if (fsp->ring_cookie >= priv->rx_cfg.num_queues)
		return -EINVAL;

	rule->action = cpu_to_be16(fsp->ring_cookie);

	flow_type = fsp->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS);
	if (!flow_type || flow_type >= ARRAY_SIZE(flow_type_lut))
		return -EINVAL;

	rule->flow_type = cpu_to_be16(flow_type_lut[flow_type]);

	switch (flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case SCTP_V4_FLOW:
		rule->key.src_ip[0] = fsp->h_u.tcp_ip4_spec.ip4src;
		rule->key.dst_ip[0] = fsp->h_u.tcp_ip4_spec.ip4dst;
		rule->key.src_port = fsp->h_u.tcp_ip4_spec.psrc;
		rule->key.dst_port = fsp->h_u.tcp_ip4_spec.pdst;
		rule->mask.src_ip[0] = fsp->m_u.tcp_ip4_spec.ip4src;
		rule->mask.dst_ip[0] = fsp->m_u.tcp_ip4_spec.ip4dst;
		rule->mask.src_port = fsp->m_u.tcp_ip4_spec.psrc;
		rule->mask.dst_port = fsp->m_u.tcp_ip4_spec.pdst;
		break;
	case AH_V4_FLOW:
	case ESP_V4_FLOW:
		rule->key.src_ip[0] = fsp->h_u.tcp_ip4_spec.ip4src;
		rule->key.dst_ip[0] = fsp->h_u.tcp_ip4_spec.ip4dst;
		rule->key.spi = fsp->h_u.ah_ip4_spec.spi;
		rule->mask.src_ip[0] = fsp->m_u.tcp_ip4_spec.ip4src;
		rule->mask.dst_ip[0] = fsp->m_u.tcp_ip4_spec.ip4dst;
		rule->mask.spi = fsp->m_u.ah_ip4_spec.spi;
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V6_FLOW:
		memcpy(&rule->key.src_ip, fsp->h_u.tcp_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&rule->key.dst_ip, fsp->h_u.tcp_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		rule->key.src_port = fsp->h_u.tcp_ip6_spec.psrc;
		rule->key.dst_port = fsp->h_u.tcp_ip6_spec.pdst;
		memcpy(&rule->mask.src_ip, fsp->m_u.tcp_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&rule->mask.dst_ip, fsp->m_u.tcp_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		rule->mask.src_port = fsp->m_u.tcp_ip6_spec.psrc;
		rule->mask.dst_port = fsp->m_u.tcp_ip6_spec.pdst;
		break;
	case AH_V6_FLOW:
	case ESP_V6_FLOW:
		memcpy(&rule->key.src_ip, fsp->h_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&rule->key.dst_ip, fsp->h_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		rule->key.spi = fsp->h_u.ah_ip6_spec.spi;
		memcpy(&rule->mask.src_ip, fsp->m_u.usr_ip6_spec.ip6src,
		       sizeof(struct in6_addr));
		memcpy(&rule->mask.dst_ip, fsp->m_u.usr_ip6_spec.ip6dst,
		       sizeof(struct in6_addr));
		rule->key.spi = fsp->h_u.ah_ip6_spec.spi;
		break;
	default:
		/* not doing un-parsed flow types */
		return -EINVAL;
	}

	return 0;
}

int gve_get_flow_rule_entry(struct gve_priv *priv, struct ethtool_rxnfc *cmd)
{
	struct gve_adminq_queried_flow_rule *rules_cache = priv->flow_rules_cache.rules_cache;
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;
	u32 *cache_num = &priv->flow_rules_cache.rules_cache_num;
	struct gve_adminq_queried_flow_rule *rule = NULL;
	int err = 0;
	u32 i;

	if (!priv->max_flow_rules)
		return -EOPNOTSUPP;

	if (!priv->flow_rules_cache.rules_cache_synced ||
	    fsp->location < be32_to_cpu(rules_cache[0].location) ||
	    fsp->location > be32_to_cpu(rules_cache[*cache_num - 1].location)) {
		err = gve_adminq_query_flow_rules(priv, GVE_FLOW_RULE_QUERY_RULES, fsp->location);
		if (err)
			return err;

		priv->flow_rules_cache.rules_cache_synced = true;
	}

	for (i = 0; i < *cache_num; i++) {
		if (fsp->location == be32_to_cpu(rules_cache[i].location)) {
			rule = &rules_cache[i];
			break;
		}
	}

	if (!rule)
		return -EINVAL;

	err = gve_fill_ethtool_flow_spec(fsp, rule);

	return err;
}

int gve_get_flow_rule_ids(struct gve_priv *priv, struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	__be32 *rule_ids_cache = priv->flow_rules_cache.rule_ids_cache;
	u32 *cache_num = &priv->flow_rules_cache.rule_ids_cache_num;
	u32 starting_rule_id = 0;
	u32 i = 0, j = 0;
	int err = 0;

	if (!priv->max_flow_rules)
		return -EOPNOTSUPP;

	do {
		err = gve_adminq_query_flow_rules(priv, GVE_FLOW_RULE_QUERY_IDS,
						  starting_rule_id);
		if (err)
			return err;

		for (i = 0; i < *cache_num; i++) {
			if (j >= cmd->rule_cnt)
				return -EMSGSIZE;

			rule_locs[j++] = be32_to_cpu(rule_ids_cache[i]);
			starting_rule_id = be32_to_cpu(rule_ids_cache[i]) + 1;
		}
	} while (*cache_num != 0);
	cmd->data = priv->max_flow_rules;

	return err;
}

int gve_add_flow_rule(struct gve_priv *priv, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = &cmd->fs;
	struct gve_adminq_flow_rule *rule = NULL;
	int err;

	if (!priv->max_flow_rules)
		return -EOPNOTSUPP;

	rule = kvzalloc(sizeof(*rule), GFP_KERNEL);
	if (!rule)
		return -ENOMEM;

	err = gve_generate_flow_rule(priv, fsp, rule);
	if (err)
		goto out;

	err = gve_adminq_add_flow_rule(priv, rule, fsp->location);

out:
	kvfree(rule);
	if (err)
		dev_err(&priv->pdev->dev, "Failed to add the flow rule: %u", fsp->location);

	return err;
}

int gve_del_flow_rule(struct gve_priv *priv, struct ethtool_rxnfc *cmd)
{
	struct ethtool_rx_flow_spec *fsp = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (!priv->max_flow_rules)
		return -EOPNOTSUPP;

	return gve_adminq_del_flow_rule(priv, fsp->location);
}
