/*
 * Broadcom Starfighter 2 DSA switch CFP support
 *
 * Copyright (C) 2016, Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/list.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <net/dsa.h>
#include <linux/bitmap.h>

#include "bcm_sf2.h"
#include "bcm_sf2_regs.h"

struct cfp_udf_layout {
	u8 slices[UDF_NUM_SLICES];
	u32 mask_value;

};

/* UDF slices layout for a TCPv4/UDPv4 specification */
static const struct cfp_udf_layout udf_tcpip4_layout = {
	.slices = {
		/* End of L2, byte offset 12, src IP[0:15] */
		CFG_UDF_EOL2 | 6,
		/* End of L2, byte offset 14, src IP[16:31] */
		CFG_UDF_EOL2 | 7,
		/* End of L2, byte offset 16, dst IP[0:15] */
		CFG_UDF_EOL2 | 8,
		/* End of L2, byte offset 18, dst IP[16:31] */
		CFG_UDF_EOL2 | 9,
		/* End of L3, byte offset 0, src port */
		CFG_UDF_EOL3 | 0,
		/* End of L3, byte offset 2, dst port */
		CFG_UDF_EOL3 | 1,
		0, 0, 0
	},
	.mask_value = L3_FRAMING_MASK | IPPROTO_MASK | IP_FRAG,
};

static inline unsigned int bcm_sf2_get_num_udf_slices(const u8 *layout)
{
	unsigned int i, count = 0;

	for (i = 0; i < UDF_NUM_SLICES; i++) {
		if (layout[i] != 0)
			count++;
	}

	return count;
}

static void bcm_sf2_cfp_udf_set(struct bcm_sf2_priv *priv,
				unsigned int slice_num,
				const u8 *layout)
{
	u32 offset = CORE_UDF_0_A_0_8_PORT_0 + slice_num * UDF_SLICE_OFFSET;
	unsigned int i;

	for (i = 0; i < UDF_NUM_SLICES; i++)
		core_writel(priv, layout[i], offset + i * 4);
}

static int bcm_sf2_cfp_op(struct bcm_sf2_priv *priv, unsigned int op)
{
	unsigned int timeout = 1000;
	u32 reg;

	reg = core_readl(priv, CORE_CFP_ACC);
	reg &= ~(OP_SEL_MASK | RAM_SEL_MASK);
	reg |= OP_STR_DONE | op;
	core_writel(priv, reg, CORE_CFP_ACC);

	do {
		reg = core_readl(priv, CORE_CFP_ACC);
		if (!(reg & OP_STR_DONE))
			break;

		cpu_relax();
	} while (timeout--);

	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

static inline void bcm_sf2_cfp_rule_addr_set(struct bcm_sf2_priv *priv,
					     unsigned int addr)
{
	u32 reg;

	WARN_ON(addr >= priv->num_cfp_rules);

	reg = core_readl(priv, CORE_CFP_ACC);
	reg &= ~(XCESS_ADDR_MASK << XCESS_ADDR_SHIFT);
	reg |= addr << XCESS_ADDR_SHIFT;
	core_writel(priv, reg, CORE_CFP_ACC);
}

static inline unsigned int bcm_sf2_cfp_rule_size(struct bcm_sf2_priv *priv)
{
	/* Entry #0 is reserved */
	return priv->num_cfp_rules - 1;
}

static int bcm_sf2_cfp_rule_set(struct dsa_switch *ds, int port,
				struct ethtool_rx_flow_spec *fs)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	struct ethtool_tcpip4_spec *v4_spec;
	const struct cfp_udf_layout *layout;
	unsigned int slice_num, rule_index;
	unsigned int queue_num, port_num;
	u8 ip_proto, ip_frag;
	u8 num_udf;
	u32 reg;
	int ret;

	/* Check for unsupported extensions */
	if ((fs->flow_type & FLOW_EXT) &&
	    (fs->m_ext.vlan_etype || fs->m_ext.data[1]))
		return -EINVAL;

	if (fs->location != RX_CLS_LOC_ANY &&
	    test_bit(fs->location, priv->cfp.used))
		return -EBUSY;

	if (fs->location != RX_CLS_LOC_ANY &&
	    fs->location > bcm_sf2_cfp_rule_size(priv))
		return -EINVAL;

	ip_frag = be32_to_cpu(fs->m_ext.data[0]);

	/* We do not support discarding packets, check that the
	 * destination port is enabled and that we are within the
	 * number of ports supported by the switch
	 */
	port_num = fs->ring_cookie / SF2_NUM_EGRESS_QUEUES;

	if (fs->ring_cookie == RX_CLS_FLOW_DISC ||
	    !(BIT(port_num) & ds->enabled_port_mask) ||
	    port_num >= priv->hw_params.num_ports)
		return -EINVAL;

	switch (fs->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		ip_proto = IPPROTO_TCP;
		v4_spec = &fs->h_u.tcp_ip4_spec;
		break;
	case UDP_V4_FLOW:
		ip_proto = IPPROTO_UDP;
		v4_spec = &fs->h_u.udp_ip4_spec;
		break;
	default:
		return -EINVAL;
	}

	/* We only use one UDF slice for now */
	slice_num = 1;
	layout = &udf_tcpip4_layout;
	num_udf = bcm_sf2_get_num_udf_slices(layout->slices);

	/* Apply the UDF layout for this filter */
	bcm_sf2_cfp_udf_set(priv, slice_num, layout->slices);

	/* Apply to all packets received through this port */
	core_writel(priv, BIT(port), CORE_CFP_DATA_PORT(7));

	/* S-Tag status		[31:30]
	 * C-Tag status		[29:28]
	 * L2 framing		[27:26]
	 * L3 framing		[25:24]
	 * IP ToS		[23:16]
	 * IP proto		[15:08]
	 * IP Fragm		[7]
	 * Non 1st frag		[6]
	 * IP Authen		[5]
	 * TTL range		[4:3]
	 * PPPoE session	[2]
	 * Reserved		[1]
	 * UDF_Valid[8]		[0]
	 */
	core_writel(priv, v4_spec->tos << 16 | ip_proto << 8 | ip_frag << 7,
		    CORE_CFP_DATA_PORT(6));

	/* UDF_Valid[7:0]	[31:24]
	 * S-Tag		[23:8]
	 * C-Tag		[7:0]
	 */
	core_writel(priv, GENMASK(num_udf - 1, 0) << 24, CORE_CFP_DATA_PORT(5));

	/* C-Tag		[31:24]
	 * UDF_n_A8		[23:8]
	 * UDF_n_A7		[7:0]
	 */
	core_writel(priv, 0, CORE_CFP_DATA_PORT(4));

	/* UDF_n_A7		[31:24]
	 * UDF_n_A6		[23:8]
	 * UDF_n_A5		[7:0]
	 */
	core_writel(priv, be16_to_cpu(v4_spec->pdst) >> 8,
		    CORE_CFP_DATA_PORT(3));

	/* UDF_n_A5		[31:24]
	 * UDF_n_A4		[23:8]
	 * UDF_n_A3		[7:0]
	 */
	reg = (be16_to_cpu(v4_spec->pdst) & 0xff) << 24 |
	      (u32)be16_to_cpu(v4_spec->psrc) << 8 |
	      (be32_to_cpu(v4_spec->ip4dst) & 0x0000ff00) >> 8;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(2));

	/* UDF_n_A3		[31:24]
	 * UDF_n_A2		[23:8]
	 * UDF_n_A1		[7:0]
	 */
	reg = (u32)(be32_to_cpu(v4_spec->ip4dst) & 0xff) << 24 |
	      (u32)(be32_to_cpu(v4_spec->ip4dst) >> 16) << 8 |
	      (be32_to_cpu(v4_spec->ip4src) & 0x0000ff00) >> 8;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(1));

	/* UDF_n_A1		[31:24]
	 * UDF_n_A0		[23:8]
	 * Reserved		[7:4]
	 * Slice ID		[3:2]
	 * Slice valid		[1:0]
	 */
	reg = (u32)(be32_to_cpu(v4_spec->ip4src) & 0xff) << 24 |
	      (u32)(be32_to_cpu(v4_spec->ip4src) >> 16) << 8 |
	      SLICE_NUM(slice_num) | SLICE_VALID;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(0));

	/* Source port map match */
	core_writel(priv, 0xff, CORE_CFP_MASK_PORT(7));

	/* Mask with the specific layout for IPv4 packets */
	core_writel(priv, layout->mask_value, CORE_CFP_MASK_PORT(6));

	/* Mask all but valid UDFs */
	core_writel(priv, GENMASK(num_udf - 1, 0) << 24, CORE_CFP_MASK_PORT(5));

	/* Mask all */
	core_writel(priv, 0, CORE_CFP_MASK_PORT(4));

	/* All other UDFs should be matched with the filter */
	core_writel(priv, 0xff, CORE_CFP_MASK_PORT(3));
	core_writel(priv, 0xffffffff, CORE_CFP_MASK_PORT(2));
	core_writel(priv, 0xffffffff, CORE_CFP_MASK_PORT(1));
	core_writel(priv, 0xffffff0f, CORE_CFP_MASK_PORT(0));

	/* Locate the first rule available */
	if (fs->location == RX_CLS_LOC_ANY)
		rule_index = find_first_zero_bit(priv->cfp.used,
						 bcm_sf2_cfp_rule_size(priv));
	else
		rule_index = fs->location;

	/* Insert into TCAM now */
	bcm_sf2_cfp_rule_addr_set(priv, rule_index);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret) {
		pr_err("TCAM entry at addr %d failed\n", rule_index);
		return ret;
	}

	/* Replace ARL derived destination with DST_MAP derived, define
	 * which port and queue this should be forwarded to.
	 *
	 * We have a small oddity where Port 6 just does not have a
	 * valid bit here (so we subtract by one).
	 */
	queue_num = fs->ring_cookie % SF2_NUM_EGRESS_QUEUES;
	if (port_num >= 7)
		port_num -= 1;

	reg = CHANGE_FWRD_MAP_IB_REP_ARL | BIT(port_num + DST_MAP_IB_SHIFT) |
		CHANGE_TC | queue_num << NEW_TC_SHIFT;

	core_writel(priv, reg, CORE_ACT_POL_DATA0);

	/* Set classification ID that needs to be put in Broadcom tag */
	core_writel(priv, rule_index << CHAIN_ID_SHIFT,
		    CORE_ACT_POL_DATA1);

	core_writel(priv, 0, CORE_ACT_POL_DATA2);

	/* Configure policer RAM now */
	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | ACT_POL_RAM);
	if (ret) {
		pr_err("Policer entry at %d failed\n", rule_index);
		return ret;
	}

	/* Disable the policer */
	core_writel(priv, POLICER_MODE_DISABLE, CORE_RATE_METER0);

	/* Now the rate meter */
	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | RATE_METER_RAM);
	if (ret) {
		pr_err("Meter entry at %d failed\n", rule_index);
		return ret;
	}

	/* Turn on CFP for this rule now */
	reg = core_readl(priv, CORE_CFP_CTL_REG);
	reg |= BIT(port);
	core_writel(priv, reg, CORE_CFP_CTL_REG);

	/* Flag the rule as being used and return it */
	set_bit(rule_index, priv->cfp.used);
	fs->location = rule_index;

	return 0;
}

static int bcm_sf2_cfp_rule_del(struct bcm_sf2_priv *priv, int port,
				u32 loc)
{
	int ret;
	u32 reg;

	/* Refuse deletion of unused rules, and the default reserved rule */
	if (!test_bit(loc, priv->cfp.used) || loc == 0)
		return -EINVAL;

	/* Indicate which rule we want to read */
	bcm_sf2_cfp_rule_addr_set(priv, loc);

	ret =  bcm_sf2_cfp_op(priv, OP_SEL_READ | TCAM_SEL);
	if (ret)
		return ret;

	/* Clear its valid bits */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(0));
	reg &= ~SLICE_VALID;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(0));

	/* Write back this entry into the TCAM now */
	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret)
		return ret;

	clear_bit(loc, priv->cfp.used);

	return 0;
}

static void bcm_sf2_invert_masks(struct ethtool_rx_flow_spec *flow)
{
	unsigned int i;

	for (i = 0; i < sizeof(flow->m_u); i++)
		flow->m_u.hdata[i] ^= 0xff;

	flow->m_ext.vlan_etype ^= cpu_to_be16(~0);
	flow->m_ext.vlan_tci ^= cpu_to_be16(~0);
	flow->m_ext.data[0] ^= cpu_to_be32(~0);
	flow->m_ext.data[1] ^= cpu_to_be32(~0);
}

static int bcm_sf2_cfp_rule_get(struct bcm_sf2_priv *priv, int port,
				struct ethtool_rxnfc *nfc, bool search)
{
	struct ethtool_tcpip4_spec *v4_spec;
	unsigned int queue_num;
	u16 src_dst_port;
	u32 reg, ipv4;
	int ret;

	if (!search) {
		bcm_sf2_cfp_rule_addr_set(priv, nfc->fs.location);

		ret = bcm_sf2_cfp_op(priv, OP_SEL_READ | ACT_POL_RAM);
		if (ret)
			return ret;

		reg = core_readl(priv, CORE_ACT_POL_DATA0);

		ret = bcm_sf2_cfp_op(priv, OP_SEL_READ | TCAM_SEL);
		if (ret)
			return ret;
	} else {
		reg = core_readl(priv, CORE_ACT_POL_DATA0);
	}

	/* Extract the destination port */
	nfc->fs.ring_cookie = fls((reg >> DST_MAP_IB_SHIFT) &
				  DST_MAP_IB_MASK) - 1;

	/* There is no Port 6, so we compensate for that here */
	if (nfc->fs.ring_cookie >= 6)
		nfc->fs.ring_cookie++;
	nfc->fs.ring_cookie *= SF2_NUM_EGRESS_QUEUES;

	/* Extract the destination queue */
	queue_num = (reg >> NEW_TC_SHIFT) & NEW_TC_MASK;
	nfc->fs.ring_cookie += queue_num;

	/* Extract the IP protocol */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(6));
	switch ((reg & IPPROTO_MASK) >> IPPROTO_SHIFT) {
	case IPPROTO_TCP:
		nfc->fs.flow_type = TCP_V4_FLOW;
		v4_spec = &nfc->fs.h_u.tcp_ip4_spec;
		break;
	case IPPROTO_UDP:
		nfc->fs.flow_type = UDP_V4_FLOW;
		v4_spec = &nfc->fs.h_u.udp_ip4_spec;
		break;
	default:
		/* Clear to exit the search process */
		if (search)
			core_readl(priv, CORE_CFP_DATA_PORT(7));
		return -EINVAL;
	}

	v4_spec->tos = (reg >> 16) & IPPROTO_MASK;
	nfc->fs.m_ext.data[0] = cpu_to_be32((reg >> 7) & 1);

	reg = core_readl(priv, CORE_CFP_DATA_PORT(3));
	/* src port [15:8] */
	src_dst_port = reg << 8;

	reg = core_readl(priv, CORE_CFP_DATA_PORT(2));
	/* src port [7:0] */
	src_dst_port |= (reg >> 24);

	v4_spec->pdst = cpu_to_be16(src_dst_port);
	nfc->fs.m_u.tcp_ip4_spec.pdst = cpu_to_be16(~0);
	v4_spec->psrc = cpu_to_be16((u16)(reg >> 8));
	nfc->fs.m_u.tcp_ip4_spec.psrc = cpu_to_be16(~0);

	/* IPv4 dst [15:8] */
	ipv4 = (reg & 0xff) << 8;
	reg = core_readl(priv, CORE_CFP_DATA_PORT(1));
	/* IPv4 dst [31:16] */
	ipv4 |= ((reg >> 8) & 0xffff) << 16;
	/* IPv4 dst [7:0] */
	ipv4 |= (reg >> 24) & 0xff;
	v4_spec->ip4dst = cpu_to_be32(ipv4);
	nfc->fs.m_u.tcp_ip4_spec.ip4dst = cpu_to_be32(~0);

	/* IPv4 src [15:8] */
	ipv4 = (reg & 0xff) << 8;
	reg = core_readl(priv, CORE_CFP_DATA_PORT(0));

	if (!(reg & SLICE_VALID))
		return -EINVAL;

	/* IPv4 src [7:0] */
	ipv4 |= (reg >> 24) & 0xff;
	/* IPv4 src [31:16] */
	ipv4 |= ((reg >> 8) & 0xffff) << 16;
	v4_spec->ip4src = cpu_to_be32(ipv4);
	nfc->fs.m_u.tcp_ip4_spec.ip4src = cpu_to_be32(~0);

	/* Read last to avoid next entry clobbering the results during search
	 * operations
	 */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(7));
	if (!(reg & 1 << port))
		return -EINVAL;

	bcm_sf2_invert_masks(&nfc->fs);

	/* Put the TCAM size here */
	nfc->data = bcm_sf2_cfp_rule_size(priv);

	return 0;
}

/* We implement the search doing a TCAM search operation */
static int bcm_sf2_cfp_rule_get_all(struct bcm_sf2_priv *priv,
				    int port, struct ethtool_rxnfc *nfc,
				    u32 *rule_locs)
{
	unsigned int index = 1, rules_cnt = 0;
	int ret;
	u32 reg;

	/* Do not poll on OP_STR_DONE to be self-clearing for search
	 * operations, we cannot use bcm_sf2_cfp_op here because it completes
	 * on clearing OP_STR_DONE which won't clear until the entire search
	 * operation is over.
	 */
	reg = core_readl(priv, CORE_CFP_ACC);
	reg &= ~(XCESS_ADDR_MASK << XCESS_ADDR_SHIFT);
	reg |= index << XCESS_ADDR_SHIFT;
	reg &= ~(OP_SEL_MASK | RAM_SEL_MASK);
	reg |= OP_SEL_SEARCH | TCAM_SEL | OP_STR_DONE;
	core_writel(priv, reg, CORE_CFP_ACC);

	do {
		/* Wait for results to be ready */
		reg = core_readl(priv, CORE_CFP_ACC);

		/* Extract the address we are searching */
		index = reg >> XCESS_ADDR_SHIFT;
		index &= XCESS_ADDR_MASK;

		/* We have a valid search result, so flag it accordingly */
		if (reg & SEARCH_STS) {
			ret = bcm_sf2_cfp_rule_get(priv, port, nfc, true);
			if (ret)
				continue;

			rule_locs[rules_cnt] = index;
			rules_cnt++;
		}

		/* Search is over break out */
		if (!(reg & OP_STR_DONE))
			break;

	} while (index < priv->num_cfp_rules);

	/* Put the TCAM size here */
	nfc->data = bcm_sf2_cfp_rule_size(priv);
	nfc->rule_cnt = rules_cnt;

	return 0;
}

int bcm_sf2_get_rxnfc(struct dsa_switch *ds, int port,
		      struct ethtool_rxnfc *nfc, u32 *rule_locs)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	int ret = 0;

	mutex_lock(&priv->cfp.lock);

	switch (nfc->cmd) {
	case ETHTOOL_GRXCLSRLCNT:
		/* Subtract the default, unusable rule */
		nfc->rule_cnt = bitmap_weight(priv->cfp.used,
					      priv->num_cfp_rules) - 1;
		/* We support specifying rule locations */
		nfc->data |= RX_CLS_LOC_SPECIAL;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = bcm_sf2_cfp_rule_get(priv, port, nfc, false);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = bcm_sf2_cfp_rule_get_all(priv, port, nfc, rule_locs);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&priv->cfp.lock);

	return ret;
}

int bcm_sf2_set_rxnfc(struct dsa_switch *ds, int port,
		      struct ethtool_rxnfc *nfc)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	int ret = 0;

	mutex_lock(&priv->cfp.lock);

	switch (nfc->cmd) {
	case ETHTOOL_SRXCLSRLINS:
		ret = bcm_sf2_cfp_rule_set(ds, port, &nfc->fs);
		break;

	case ETHTOOL_SRXCLSRLDEL:
		ret = bcm_sf2_cfp_rule_del(priv, port, nfc->fs.location);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&priv->cfp.lock);

	return ret;
}

int bcm_sf2_cfp_rst(struct bcm_sf2_priv *priv)
{
	unsigned int timeout = 1000;
	u32 reg;

	reg = core_readl(priv, CORE_CFP_ACC);
	reg |= TCAM_RESET;
	core_writel(priv, reg, CORE_CFP_ACC);

	do {
		reg = core_readl(priv, CORE_CFP_ACC);
		if (!(reg & TCAM_RESET))
			break;

		cpu_relax();
	} while (timeout--);

	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}
