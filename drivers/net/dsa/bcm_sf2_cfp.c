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

struct cfp_udf_slice_layout {
	u8 slices[UDFS_PER_SLICE];
	u32 mask_value;
	u32 base_offset;
};

struct cfp_udf_layout {
	struct cfp_udf_slice_layout udfs[UDF_NUM_SLICES];
};

static const u8 zero_slice[UDFS_PER_SLICE] = { };

/* UDF slices layout for a TCPv4/UDPv4 specification */
static const struct cfp_udf_layout udf_tcpip4_layout = {
	.udfs = {
		[1] = {
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
			.base_offset = CORE_UDF_0_A_0_8_PORT_0 + UDF_SLICE_OFFSET,
		},
	},
};

/* UDF slices layout for a TCPv6/UDPv6 specification */
static const struct cfp_udf_layout udf_tcpip6_layout = {
	.udfs = {
		[0] = {
			.slices = {
				/* End of L2, byte offset 8, src IP[0:15] */
				CFG_UDF_EOL2 | 4,
				/* End of L2, byte offset 10, src IP[16:31] */
				CFG_UDF_EOL2 | 5,
				/* End of L2, byte offset 12, src IP[32:47] */
				CFG_UDF_EOL2 | 6,
				/* End of L2, byte offset 14, src IP[48:63] */
				CFG_UDF_EOL2 | 7,
				/* End of L2, byte offset 16, src IP[64:79] */
				CFG_UDF_EOL2 | 8,
				/* End of L2, byte offset 18, src IP[80:95] */
				CFG_UDF_EOL2 | 9,
				/* End of L2, byte offset 20, src IP[96:111] */
				CFG_UDF_EOL2 | 10,
				/* End of L2, byte offset 22, src IP[112:127] */
				CFG_UDF_EOL2 | 11,
				/* End of L3, byte offset 0, src port */
				CFG_UDF_EOL3 | 0,
			},
			.mask_value = L3_FRAMING_MASK | IPPROTO_MASK | IP_FRAG,
			.base_offset = CORE_UDF_0_B_0_8_PORT_0,
		},
		[3] = {
			.slices = {
				/* End of L2, byte offset 24, dst IP[0:15] */
				CFG_UDF_EOL2 | 12,
				/* End of L2, byte offset 26, dst IP[16:31] */
				CFG_UDF_EOL2 | 13,
				/* End of L2, byte offset 28, dst IP[32:47] */
				CFG_UDF_EOL2 | 14,
				/* End of L2, byte offset 30, dst IP[48:63] */
				CFG_UDF_EOL2 | 15,
				/* End of L2, byte offset 32, dst IP[64:79] */
				CFG_UDF_EOL2 | 16,
				/* End of L2, byte offset 34, dst IP[80:95] */
				CFG_UDF_EOL2 | 17,
				/* End of L2, byte offset 36, dst IP[96:111] */
				CFG_UDF_EOL2 | 18,
				/* End of L2, byte offset 38, dst IP[112:127] */
				CFG_UDF_EOL2 | 19,
				/* End of L3, byte offset 2, dst port */
				CFG_UDF_EOL3 | 1,
			},
			.mask_value = L3_FRAMING_MASK | IPPROTO_MASK | IP_FRAG,
			.base_offset = CORE_UDF_0_D_0_11_PORT_0,
		},
	},
};

static inline unsigned int bcm_sf2_get_num_udf_slices(const u8 *layout)
{
	unsigned int i, count = 0;

	for (i = 0; i < UDFS_PER_SLICE; i++) {
		if (layout[i] != 0)
			count++;
	}

	return count;
}

static inline u32 udf_upper_bits(unsigned int num_udf)
{
	return GENMASK(num_udf - 1, 0) >> (UDFS_PER_SLICE - 1);
}

static inline u32 udf_lower_bits(unsigned int num_udf)
{
	return (u8)GENMASK(num_udf - 1, 0);
}

static unsigned int bcm_sf2_get_slice_number(const struct cfp_udf_layout *l,
					     unsigned int start)
{
	const struct cfp_udf_slice_layout *slice_layout;
	unsigned int slice_idx;

	for (slice_idx = start; slice_idx < UDF_NUM_SLICES; slice_idx++) {
		slice_layout = &l->udfs[slice_idx];
		if (memcmp(slice_layout->slices, zero_slice,
			   sizeof(zero_slice)))
			break;
	}

	return slice_idx;
}

static void bcm_sf2_cfp_udf_set(struct bcm_sf2_priv *priv,
				const struct cfp_udf_layout *layout,
				unsigned int slice_num)
{
	u32 offset = layout->udfs[slice_num].base_offset;
	unsigned int i;

	for (i = 0; i < UDFS_PER_SLICE; i++)
		core_writel(priv, layout->udfs[slice_num].slices[i],
			    offset + i * 4);
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

static int bcm_sf2_cfp_act_pol_set(struct bcm_sf2_priv *priv,
				   unsigned int rule_index,
				   unsigned int port_num,
				   unsigned int queue_num,
				   bool fwd_map_change)
{
	int ret;
	u32 reg;

	/* Replace ARL derived destination with DST_MAP derived, define
	 * which port and queue this should be forwarded to.
	 */
	if (fwd_map_change)
		reg = CHANGE_FWRD_MAP_IB_REP_ARL |
		      BIT(port_num + DST_MAP_IB_SHIFT) |
		      CHANGE_TC | queue_num << NEW_TC_SHIFT;
	else
		reg = 0;

	core_writel(priv, reg, CORE_ACT_POL_DATA0);

	/* Set classification ID that needs to be put in Broadcom tag */
	core_writel(priv, rule_index << CHAIN_ID_SHIFT, CORE_ACT_POL_DATA1);

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

	return 0;
}

static void bcm_sf2_cfp_slice_ipv4(struct bcm_sf2_priv *priv,
				   struct ethtool_tcpip4_spec *v4_spec,
				   unsigned int slice_num,
				   bool mask)
{
	u32 reg, offset;

	/* C-Tag		[31:24]
	 * UDF_n_A8		[23:8]
	 * UDF_n_A7		[7:0]
	 */
	reg = 0;
	if (mask)
		offset = CORE_CFP_MASK_PORT(4);
	else
		offset = CORE_CFP_DATA_PORT(4);
	core_writel(priv, reg, offset);

	/* UDF_n_A7		[31:24]
	 * UDF_n_A6		[23:8]
	 * UDF_n_A5		[7:0]
	 */
	reg = be16_to_cpu(v4_spec->pdst) >> 8;
	if (mask)
		offset = CORE_CFP_MASK_PORT(3);
	else
		offset = CORE_CFP_DATA_PORT(3);
	core_writel(priv, reg, offset);

	/* UDF_n_A5		[31:24]
	 * UDF_n_A4		[23:8]
	 * UDF_n_A3		[7:0]
	 */
	reg = (be16_to_cpu(v4_spec->pdst) & 0xff) << 24 |
	      (u32)be16_to_cpu(v4_spec->psrc) << 8 |
	      (be32_to_cpu(v4_spec->ip4dst) & 0x0000ff00) >> 8;
	if (mask)
		offset = CORE_CFP_MASK_PORT(2);
	else
		offset = CORE_CFP_DATA_PORT(2);
	core_writel(priv, reg, offset);

	/* UDF_n_A3		[31:24]
	 * UDF_n_A2		[23:8]
	 * UDF_n_A1		[7:0]
	 */
	reg = (u32)(be32_to_cpu(v4_spec->ip4dst) & 0xff) << 24 |
	      (u32)(be32_to_cpu(v4_spec->ip4dst) >> 16) << 8 |
	      (be32_to_cpu(v4_spec->ip4src) & 0x0000ff00) >> 8;
	if (mask)
		offset = CORE_CFP_MASK_PORT(1);
	else
		offset = CORE_CFP_DATA_PORT(1);
	core_writel(priv, reg, offset);

	/* UDF_n_A1		[31:24]
	 * UDF_n_A0		[23:8]
	 * Reserved		[7:4]
	 * Slice ID		[3:2]
	 * Slice valid		[1:0]
	 */
	reg = (u32)(be32_to_cpu(v4_spec->ip4src) & 0xff) << 24 |
	      (u32)(be32_to_cpu(v4_spec->ip4src) >> 16) << 8 |
	      SLICE_NUM(slice_num) | SLICE_VALID;
	if (mask)
		offset = CORE_CFP_MASK_PORT(0);
	else
		offset = CORE_CFP_DATA_PORT(0);
	core_writel(priv, reg, offset);
}

static int bcm_sf2_cfp_ipv4_rule_set(struct bcm_sf2_priv *priv, int port,
				     unsigned int port_num,
				     unsigned int queue_num,
				     struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_tcpip4_spec *v4_spec, *v4_m_spec;
	const struct cfp_udf_layout *layout;
	unsigned int slice_num, rule_index;
	u8 ip_proto, ip_frag;
	u8 num_udf;
	u32 reg;
	int ret;

	switch (fs->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
		ip_proto = IPPROTO_TCP;
		v4_spec = &fs->h_u.tcp_ip4_spec;
		v4_m_spec = &fs->m_u.tcp_ip4_spec;
		break;
	case UDP_V4_FLOW:
		ip_proto = IPPROTO_UDP;
		v4_spec = &fs->h_u.udp_ip4_spec;
		v4_m_spec = &fs->m_u.udp_ip4_spec;
		break;
	default:
		return -EINVAL;
	}

	ip_frag = be32_to_cpu(fs->m_ext.data[0]);

	/* Locate the first rule available */
	if (fs->location == RX_CLS_LOC_ANY)
		rule_index = find_first_zero_bit(priv->cfp.used,
						 priv->num_cfp_rules);
	else
		rule_index = fs->location;

	if (rule_index > bcm_sf2_cfp_rule_size(priv))
		return -ENOSPC;

	layout = &udf_tcpip4_layout;
	/* We only use one UDF slice for now */
	slice_num = bcm_sf2_get_slice_number(layout, 0);
	if (slice_num == UDF_NUM_SLICES)
		return -EINVAL;

	num_udf = bcm_sf2_get_num_udf_slices(layout->udfs[slice_num].slices);

	/* Apply the UDF layout for this filter */
	bcm_sf2_cfp_udf_set(priv, layout, slice_num);

	/* Apply to all packets received through this port */
	core_writel(priv, BIT(port), CORE_CFP_DATA_PORT(7));

	/* Source port map match */
	core_writel(priv, 0xff, CORE_CFP_MASK_PORT(7));

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
	core_writel(priv, v4_spec->tos << IPTOS_SHIFT |
		    ip_proto << IPPROTO_SHIFT | ip_frag << IP_FRAG_SHIFT |
		    udf_upper_bits(num_udf),
		    CORE_CFP_DATA_PORT(6));

	/* Mask with the specific layout for IPv4 packets */
	core_writel(priv, layout->udfs[slice_num].mask_value |
		    udf_upper_bits(num_udf), CORE_CFP_MASK_PORT(6));

	/* UDF_Valid[7:0]	[31:24]
	 * S-Tag		[23:8]
	 * C-Tag		[7:0]
	 */
	core_writel(priv, udf_lower_bits(num_udf) << 24, CORE_CFP_DATA_PORT(5));

	/* Mask all but valid UDFs */
	core_writel(priv, udf_lower_bits(num_udf) << 24, CORE_CFP_MASK_PORT(5));

	/* Program the match and the mask */
	bcm_sf2_cfp_slice_ipv4(priv, v4_spec, slice_num, false);
	bcm_sf2_cfp_slice_ipv4(priv, v4_m_spec, SLICE_NUM_MASK, true);

	/* Insert into TCAM now */
	bcm_sf2_cfp_rule_addr_set(priv, rule_index);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret) {
		pr_err("TCAM entry at addr %d failed\n", rule_index);
		return ret;
	}

	/* Insert into Action and policer RAMs now */
	ret = bcm_sf2_cfp_act_pol_set(priv, rule_index, port_num,
				      queue_num, true);
	if (ret)
		return ret;

	/* Turn on CFP for this rule now */
	reg = core_readl(priv, CORE_CFP_CTL_REG);
	reg |= BIT(port);
	core_writel(priv, reg, CORE_CFP_CTL_REG);

	/* Flag the rule as being used and return it */
	set_bit(rule_index, priv->cfp.used);
	set_bit(rule_index, priv->cfp.unique);
	fs->location = rule_index;

	return 0;
}

static void bcm_sf2_cfp_slice_ipv6(struct bcm_sf2_priv *priv,
				   const __be32 *ip6_addr, const __be16 port,
				   unsigned int slice_num,
				   bool mask)
{
	u32 reg, tmp, val, offset;

	/* C-Tag		[31:24]
	 * UDF_n_B8		[23:8]	(port)
	 * UDF_n_B7 (upper)	[7:0]	(addr[15:8])
	 */
	reg = be32_to_cpu(ip6_addr[3]);
	val = (u32)be16_to_cpu(port) << 8 | ((reg >> 8) & 0xff);
	if (mask)
		offset = CORE_CFP_MASK_PORT(4);
	else
		offset = CORE_CFP_DATA_PORT(4);
	core_writel(priv, val, offset);

	/* UDF_n_B7 (lower)	[31:24]	(addr[7:0])
	 * UDF_n_B6		[23:8] (addr[31:16])
	 * UDF_n_B5 (upper)	[7:0] (addr[47:40])
	 */
	tmp = be32_to_cpu(ip6_addr[2]);
	val = (u32)(reg & 0xff) << 24 | (u32)(reg >> 16) << 8 |
	      ((tmp >> 8) & 0xff);
	if (mask)
		offset = CORE_CFP_MASK_PORT(3);
	else
		offset = CORE_CFP_DATA_PORT(3);
	core_writel(priv, val, offset);

	/* UDF_n_B5 (lower)	[31:24] (addr[39:32])
	 * UDF_n_B4		[23:8] (addr[63:48])
	 * UDF_n_B3 (upper)	[7:0] (addr[79:72])
	 */
	reg = be32_to_cpu(ip6_addr[1]);
	val = (u32)(tmp & 0xff) << 24 | (u32)(tmp >> 16) << 8 |
	      ((reg >> 8) & 0xff);
	if (mask)
		offset = CORE_CFP_MASK_PORT(2);
	else
		offset = CORE_CFP_DATA_PORT(2);
	core_writel(priv, val, offset);

	/* UDF_n_B3 (lower)	[31:24] (addr[71:64])
	 * UDF_n_B2		[23:8] (addr[95:80])
	 * UDF_n_B1 (upper)	[7:0] (addr[111:104])
	 */
	tmp = be32_to_cpu(ip6_addr[0]);
	val = (u32)(reg & 0xff) << 24 | (u32)(reg >> 16) << 8 |
	      ((tmp >> 8) & 0xff);
	if (mask)
		offset = CORE_CFP_MASK_PORT(1);
	else
		offset = CORE_CFP_DATA_PORT(1);
	core_writel(priv, val, offset);

	/* UDF_n_B1 (lower)	[31:24] (addr[103:96])
	 * UDF_n_B0		[23:8] (addr[127:112])
	 * Reserved		[7:4]
	 * Slice ID		[3:2]
	 * Slice valid		[1:0]
	 */
	reg = (u32)(tmp & 0xff) << 24 | (u32)(tmp >> 16) << 8 |
	       SLICE_NUM(slice_num) | SLICE_VALID;
	if (mask)
		offset = CORE_CFP_MASK_PORT(0);
	else
		offset = CORE_CFP_DATA_PORT(0);
	core_writel(priv, reg, offset);
}

static int bcm_sf2_cfp_ipv6_rule_set(struct bcm_sf2_priv *priv, int port,
				     unsigned int port_num,
				     unsigned int queue_num,
				     struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_tcpip6_spec *v6_spec, *v6_m_spec;
	unsigned int slice_num, rule_index[2];
	const struct cfp_udf_layout *layout;
	u8 ip_proto, ip_frag;
	int ret = 0;
	u8 num_udf;
	u32 reg;

	switch (fs->flow_type & ~FLOW_EXT) {
	case TCP_V6_FLOW:
		ip_proto = IPPROTO_TCP;
		v6_spec = &fs->h_u.tcp_ip6_spec;
		v6_m_spec = &fs->m_u.tcp_ip6_spec;
		break;
	case UDP_V6_FLOW:
		ip_proto = IPPROTO_UDP;
		v6_spec = &fs->h_u.udp_ip6_spec;
		v6_m_spec = &fs->m_u.udp_ip6_spec;
		break;
	default:
		return -EINVAL;
	}

	ip_frag = be32_to_cpu(fs->m_ext.data[0]);

	layout = &udf_tcpip6_layout;
	slice_num = bcm_sf2_get_slice_number(layout, 0);
	if (slice_num == UDF_NUM_SLICES)
		return -EINVAL;

	num_udf = bcm_sf2_get_num_udf_slices(layout->udfs[slice_num].slices);

	/* Negotiate two indexes, one for the second half which we are chained
	 * from, which is what we will return to user-space, and a second one
	 * which is used to store its first half. That first half does not
	 * allow any choice of placement, so it just needs to find the next
	 * available bit. We return the second half as fs->location because
	 * that helps with the rule lookup later on since the second half is
	 * chained from its first half, we can easily identify IPv6 CFP rules
	 * by looking whether they carry a CHAIN_ID.
	 *
	 * We also want the second half to have a lower rule_index than its
	 * first half because the HW search is by incrementing addresses.
	 */
	if (fs->location == RX_CLS_LOC_ANY)
		rule_index[1] = find_first_zero_bit(priv->cfp.used,
						    priv->num_cfp_rules);
	else
		rule_index[1] = fs->location;
	if (rule_index[1] > bcm_sf2_cfp_rule_size(priv))
		return -ENOSPC;

	/* Flag it as used (cleared on error path) such that we can immediately
	 * obtain a second one to chain from.
	 */
	set_bit(rule_index[1], priv->cfp.used);

	rule_index[0] = find_first_zero_bit(priv->cfp.used,
					    priv->num_cfp_rules);
	if (rule_index[0] > bcm_sf2_cfp_rule_size(priv)) {
		ret = -ENOSPC;
		goto out_err;
	}

	/* Apply the UDF layout for this filter */
	bcm_sf2_cfp_udf_set(priv, layout, slice_num);

	/* Apply to all packets received through this port */
	core_writel(priv, BIT(port), CORE_CFP_DATA_PORT(7));

	/* Source port map match */
	core_writel(priv, 0xff, CORE_CFP_MASK_PORT(7));

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
	reg = 1 << L3_FRAMING_SHIFT | ip_proto << IPPROTO_SHIFT |
		ip_frag << IP_FRAG_SHIFT | udf_upper_bits(num_udf);
	core_writel(priv, reg, CORE_CFP_DATA_PORT(6));

	/* Mask with the specific layout for IPv6 packets including
	 * UDF_Valid[8]
	 */
	reg = layout->udfs[slice_num].mask_value | udf_upper_bits(num_udf);
	core_writel(priv, reg, CORE_CFP_MASK_PORT(6));

	/* UDF_Valid[7:0]	[31:24]
	 * S-Tag		[23:8]
	 * C-Tag		[7:0]
	 */
	core_writel(priv, udf_lower_bits(num_udf) << 24, CORE_CFP_DATA_PORT(5));

	/* Mask all but valid UDFs */
	core_writel(priv, udf_lower_bits(num_udf) << 24, CORE_CFP_MASK_PORT(5));

	/* Slice the IPv6 source address and port */
	bcm_sf2_cfp_slice_ipv6(priv, v6_spec->ip6src, v6_spec->psrc,
				slice_num, false);
	bcm_sf2_cfp_slice_ipv6(priv, v6_m_spec->ip6src, v6_m_spec->psrc,
				SLICE_NUM_MASK, true);

	/* Insert into TCAM now because we need to insert a second rule */
	bcm_sf2_cfp_rule_addr_set(priv, rule_index[0]);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret) {
		pr_err("TCAM entry at addr %d failed\n", rule_index[0]);
		goto out_err;
	}

	/* Insert into Action and policer RAMs now */
	ret = bcm_sf2_cfp_act_pol_set(priv, rule_index[0], port_num,
				      queue_num, false);
	if (ret)
		goto out_err;

	/* Now deal with the second slice to chain this rule */
	slice_num = bcm_sf2_get_slice_number(layout, slice_num + 1);
	if (slice_num == UDF_NUM_SLICES) {
		ret = -EINVAL;
		goto out_err;
	}

	num_udf = bcm_sf2_get_num_udf_slices(layout->udfs[slice_num].slices);

	/* Apply the UDF layout for this filter */
	bcm_sf2_cfp_udf_set(priv, layout, slice_num);

	/* Chained rule, source port match is coming from the rule we are
	 * chained from.
	 */
	core_writel(priv, 0, CORE_CFP_DATA_PORT(7));
	core_writel(priv, 0, CORE_CFP_MASK_PORT(7));

	/*
	 * CHAIN ID		[31:24] chain to previous slice
	 * Reserved		[23:20]
	 * UDF_Valid[11:8]	[19:16]
	 * UDF_Valid[7:0]	[15:8]
	 * UDF_n_D11		[7:0]
	 */
	reg = rule_index[0] << 24 | udf_upper_bits(num_udf) << 16 |
		udf_lower_bits(num_udf) << 8;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(6));

	/* Mask all except chain ID, UDF Valid[8] and UDF Valid[7:0] */
	reg = XCESS_ADDR_MASK << 24 | udf_upper_bits(num_udf) << 16 |
		udf_lower_bits(num_udf) << 8;
	core_writel(priv, reg, CORE_CFP_MASK_PORT(6));

	/* Don't care */
	core_writel(priv, 0, CORE_CFP_DATA_PORT(5));

	/* Mask all */
	core_writel(priv, 0, CORE_CFP_MASK_PORT(5));

	bcm_sf2_cfp_slice_ipv6(priv, v6_spec->ip6dst, v6_spec->pdst, slice_num,
			       false);
	bcm_sf2_cfp_slice_ipv6(priv, v6_m_spec->ip6dst, v6_m_spec->pdst,
			       SLICE_NUM_MASK, true);

	/* Insert into TCAM now */
	bcm_sf2_cfp_rule_addr_set(priv, rule_index[1]);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret) {
		pr_err("TCAM entry at addr %d failed\n", rule_index[1]);
		goto out_err;
	}

	/* Insert into Action and policer RAMs now, set chain ID to
	 * the one we are chained to
	 */
	ret = bcm_sf2_cfp_act_pol_set(priv, rule_index[1], port_num,
				      queue_num, true);
	if (ret)
		goto out_err;

	/* Turn on CFP for this rule now */
	reg = core_readl(priv, CORE_CFP_CTL_REG);
	reg |= BIT(port);
	core_writel(priv, reg, CORE_CFP_CTL_REG);

	/* Flag the second half rule as being used now, return it as the
	 * location, and flag it as unique while dumping rules
	 */
	set_bit(rule_index[0], priv->cfp.used);
	set_bit(rule_index[1], priv->cfp.unique);
	fs->location = rule_index[1];

	return ret;

out_err:
	clear_bit(rule_index[1], priv->cfp.used);
	return ret;
}

static int bcm_sf2_cfp_rule_set(struct dsa_switch *ds, int port,
				struct ethtool_rx_flow_spec *fs)
{
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	s8 cpu_port = ds->ports[port].cpu_dp->index;
	__u64 ring_cookie = fs->ring_cookie;
	unsigned int queue_num, port_num;
	int ret = -EINVAL;

	/* Check for unsupported extensions */
	if ((fs->flow_type & FLOW_EXT) && (fs->m_ext.vlan_etype ||
	     fs->m_ext.data[1]))
		return -EINVAL;

	if (fs->location != RX_CLS_LOC_ANY &&
	    test_bit(fs->location, priv->cfp.used))
		return -EBUSY;

	if (fs->location != RX_CLS_LOC_ANY &&
	    fs->location > bcm_sf2_cfp_rule_size(priv))
		return -EINVAL;

	/* This rule is a Wake-on-LAN filter and we must specifically
	 * target the CPU port in order for it to be working.
	 */
	if (ring_cookie == RX_CLS_FLOW_WAKE)
		ring_cookie = cpu_port * SF2_NUM_EGRESS_QUEUES;

	/* We do not support discarding packets, check that the
	 * destination port is enabled and that we are within the
	 * number of ports supported by the switch
	 */
	port_num = ring_cookie / SF2_NUM_EGRESS_QUEUES;

	if (ring_cookie == RX_CLS_FLOW_DISC ||
	    !(dsa_is_user_port(ds, port_num) ||
	      dsa_is_cpu_port(ds, port_num)) ||
	    port_num >= priv->hw_params.num_ports)
		return -EINVAL;
	/*
	 * We have a small oddity where Port 6 just does not have a
	 * valid bit here (so we substract by one).
	 */
	queue_num = ring_cookie % SF2_NUM_EGRESS_QUEUES;
	if (port_num >= 7)
		port_num -= 1;

	switch (fs->flow_type & ~FLOW_EXT) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		ret = bcm_sf2_cfp_ipv4_rule_set(priv, port, port_num,
						queue_num, fs);
		break;
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		ret = bcm_sf2_cfp_ipv6_rule_set(priv, port, port_num,
						queue_num, fs);
		break;
	default:
		break;
	}

	return ret;
}

static int bcm_sf2_cfp_rule_del_one(struct bcm_sf2_priv *priv, int port,
				    u32 loc, u32 *next_loc)
{
	int ret;
	u32 reg;

	/* Indicate which rule we want to read */
	bcm_sf2_cfp_rule_addr_set(priv, loc);

	ret =  bcm_sf2_cfp_op(priv, OP_SEL_READ | TCAM_SEL);
	if (ret)
		return ret;

	/* Check if this is possibly an IPv6 rule that would
	 * indicate we need to delete its companion rule
	 * as well
	 */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(6));
	if (next_loc)
		*next_loc = (reg >> 24) & CHAIN_ID_MASK;

	/* Clear its valid bits */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(0));
	reg &= ~SLICE_VALID;
	core_writel(priv, reg, CORE_CFP_DATA_PORT(0));

	/* Write back this entry into the TCAM now */
	ret = bcm_sf2_cfp_op(priv, OP_SEL_WRITE | TCAM_SEL);
	if (ret)
		return ret;

	clear_bit(loc, priv->cfp.used);
	clear_bit(loc, priv->cfp.unique);

	return 0;
}

static int bcm_sf2_cfp_rule_del(struct bcm_sf2_priv *priv, int port,
				u32 loc)
{
	u32 next_loc = 0;
	int ret;

	/* Refuse deleting unused rules, and those that are not unique since
	 * that could leave IPv6 rules with one of the chained rule in the
	 * table.
	 */
	if (!test_bit(loc, priv->cfp.unique) || loc == 0)
		return -EINVAL;

	ret = bcm_sf2_cfp_rule_del_one(priv, port, loc, &next_loc);
	if (ret)
		return ret;

	/* If this was an IPv6 rule, delete is companion rule too */
	if (next_loc)
		ret = bcm_sf2_cfp_rule_del_one(priv, port, next_loc, NULL);

	return ret;
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

static int bcm_sf2_cfp_unslice_ipv4(struct bcm_sf2_priv *priv,
				    struct ethtool_tcpip4_spec *v4_spec,
				    bool mask)
{
	u32 reg, offset, ipv4;
	u16 src_dst_port;

	if (mask)
		offset = CORE_CFP_MASK_PORT(3);
	else
		offset = CORE_CFP_DATA_PORT(3);

	reg = core_readl(priv, offset);
	/* src port [15:8] */
	src_dst_port = reg << 8;

	if (mask)
		offset = CORE_CFP_MASK_PORT(2);
	else
		offset = CORE_CFP_DATA_PORT(2);

	reg = core_readl(priv, offset);
	/* src port [7:0] */
	src_dst_port |= (reg >> 24);

	v4_spec->pdst = cpu_to_be16(src_dst_port);
	v4_spec->psrc = cpu_to_be16((u16)(reg >> 8));

	/* IPv4 dst [15:8] */
	ipv4 = (reg & 0xff) << 8;

	if (mask)
		offset = CORE_CFP_MASK_PORT(1);
	else
		offset = CORE_CFP_DATA_PORT(1);

	reg = core_readl(priv, offset);
	/* IPv4 dst [31:16] */
	ipv4 |= ((reg >> 8) & 0xffff) << 16;
	/* IPv4 dst [7:0] */
	ipv4 |= (reg >> 24) & 0xff;
	v4_spec->ip4dst = cpu_to_be32(ipv4);

	/* IPv4 src [15:8] */
	ipv4 = (reg & 0xff) << 8;

	if (mask)
		offset = CORE_CFP_MASK_PORT(0);
	else
		offset = CORE_CFP_DATA_PORT(0);
	reg = core_readl(priv, offset);

	/* Once the TCAM is programmed, the mask reflects the slice number
	 * being matched, don't bother checking it when reading back the
	 * mask spec
	 */
	if (!mask && !(reg & SLICE_VALID))
		return -EINVAL;

	/* IPv4 src [7:0] */
	ipv4 |= (reg >> 24) & 0xff;
	/* IPv4 src [31:16] */
	ipv4 |= ((reg >> 8) & 0xffff) << 16;
	v4_spec->ip4src = cpu_to_be32(ipv4);

	return 0;
}

static int bcm_sf2_cfp_ipv4_rule_get(struct bcm_sf2_priv *priv, int port,
				     struct ethtool_rx_flow_spec *fs)
{
	struct ethtool_tcpip4_spec *v4_spec = NULL, *v4_m_spec = NULL;
	u32 reg;
	int ret;

	reg = core_readl(priv, CORE_CFP_DATA_PORT(6));

	switch ((reg & IPPROTO_MASK) >> IPPROTO_SHIFT) {
	case IPPROTO_TCP:
		fs->flow_type = TCP_V4_FLOW;
		v4_spec = &fs->h_u.tcp_ip4_spec;
		v4_m_spec = &fs->m_u.tcp_ip4_spec;
		break;
	case IPPROTO_UDP:
		fs->flow_type = UDP_V4_FLOW;
		v4_spec = &fs->h_u.udp_ip4_spec;
		v4_m_spec = &fs->m_u.udp_ip4_spec;
		break;
	default:
		return -EINVAL;
	}

	fs->m_ext.data[0] = cpu_to_be32((reg >> IP_FRAG_SHIFT) & 1);
	v4_spec->tos = (reg >> IPTOS_SHIFT) & IPTOS_MASK;

	ret = bcm_sf2_cfp_unslice_ipv4(priv, v4_spec, false);
	if (ret)
		return ret;

	return bcm_sf2_cfp_unslice_ipv4(priv, v4_m_spec, true);
}

static int bcm_sf2_cfp_unslice_ipv6(struct bcm_sf2_priv *priv,
				     __be32 *ip6_addr, __be16 *port,
				     bool mask)
{
	u32 reg, tmp, offset;

	/* C-Tag		[31:24]
	 * UDF_n_B8		[23:8] (port)
	 * UDF_n_B7 (upper)	[7:0] (addr[15:8])
	 */
	if (mask)
		offset = CORE_CFP_MASK_PORT(4);
	else
		offset = CORE_CFP_DATA_PORT(4);
	reg = core_readl(priv, offset);
	*port = cpu_to_be32(reg) >> 8;
	tmp = (u32)(reg & 0xff) << 8;

	/* UDF_n_B7 (lower)	[31:24] (addr[7:0])
	 * UDF_n_B6		[23:8] (addr[31:16])
	 * UDF_n_B5 (upper)	[7:0] (addr[47:40])
	 */
	if (mask)
		offset = CORE_CFP_MASK_PORT(3);
	else
		offset = CORE_CFP_DATA_PORT(3);
	reg = core_readl(priv, offset);
	tmp |= (reg >> 24) & 0xff;
	tmp |= (u32)((reg >> 8) << 16);
	ip6_addr[3] = cpu_to_be32(tmp);
	tmp = (u32)(reg & 0xff) << 8;

	/* UDF_n_B5 (lower)	[31:24] (addr[39:32])
	 * UDF_n_B4		[23:8] (addr[63:48])
	 * UDF_n_B3 (upper)	[7:0] (addr[79:72])
	 */
	if (mask)
		offset = CORE_CFP_MASK_PORT(2);
	else
		offset = CORE_CFP_DATA_PORT(2);
	reg = core_readl(priv, offset);
	tmp |= (reg >> 24) & 0xff;
	tmp |= (u32)((reg >> 8) << 16);
	ip6_addr[2] = cpu_to_be32(tmp);
	tmp = (u32)(reg & 0xff) << 8;

	/* UDF_n_B3 (lower)	[31:24] (addr[71:64])
	 * UDF_n_B2		[23:8] (addr[95:80])
	 * UDF_n_B1 (upper)	[7:0] (addr[111:104])
	 */
	if (mask)
		offset = CORE_CFP_MASK_PORT(1);
	else
		offset = CORE_CFP_DATA_PORT(1);
	reg = core_readl(priv, offset);
	tmp |= (reg >> 24) & 0xff;
	tmp |= (u32)((reg >> 8) << 16);
	ip6_addr[1] = cpu_to_be32(tmp);
	tmp = (u32)(reg & 0xff) << 8;

	/* UDF_n_B1 (lower)	[31:24] (addr[103:96])
	 * UDF_n_B0		[23:8] (addr[127:112])
	 * Reserved		[7:4]
	 * Slice ID		[3:2]
	 * Slice valid		[1:0]
	 */
	if (mask)
		offset = CORE_CFP_MASK_PORT(0);
	else
		offset = CORE_CFP_DATA_PORT(0);
	reg = core_readl(priv, offset);
	tmp |= (reg >> 24) & 0xff;
	tmp |= (u32)((reg >> 8) << 16);
	ip6_addr[0] = cpu_to_be32(tmp);

	if (!mask && !(reg & SLICE_VALID))
		return -EINVAL;

	return 0;
}

static int bcm_sf2_cfp_ipv6_rule_get(struct bcm_sf2_priv *priv, int port,
				     struct ethtool_rx_flow_spec *fs,
				     u32 next_loc)
{
	struct ethtool_tcpip6_spec *v6_spec = NULL, *v6_m_spec = NULL;
	u32 reg;
	int ret;

	/* UDPv6 and TCPv6 both use ethtool_tcpip6_spec so we are fine
	 * assuming tcp_ip6_spec here being an union.
	 */
	v6_spec = &fs->h_u.tcp_ip6_spec;
	v6_m_spec = &fs->m_u.tcp_ip6_spec;

	/* Read the second half first */
	ret = bcm_sf2_cfp_unslice_ipv6(priv, v6_spec->ip6dst, &v6_spec->pdst,
				       false);
	if (ret)
		return ret;

	ret = bcm_sf2_cfp_unslice_ipv6(priv, v6_m_spec->ip6dst,
				       &v6_m_spec->pdst, true);
	if (ret)
		return ret;

	/* Read last to avoid next entry clobbering the results during search
	 * operations. We would not have the port enabled for this rule, so
	 * don't bother checking it.
	 */
	(void)core_readl(priv, CORE_CFP_DATA_PORT(7));

	/* The slice number is valid, so read the rule we are chained from now
	 * which is our first half.
	 */
	bcm_sf2_cfp_rule_addr_set(priv, next_loc);
	ret = bcm_sf2_cfp_op(priv, OP_SEL_READ | TCAM_SEL);
	if (ret)
		return ret;

	reg = core_readl(priv, CORE_CFP_DATA_PORT(6));

	switch ((reg & IPPROTO_MASK) >> IPPROTO_SHIFT) {
	case IPPROTO_TCP:
		fs->flow_type = TCP_V6_FLOW;
		break;
	case IPPROTO_UDP:
		fs->flow_type = UDP_V6_FLOW;
		break;
	default:
		return -EINVAL;
	}

	ret = bcm_sf2_cfp_unslice_ipv6(priv, v6_spec->ip6src, &v6_spec->psrc,
				       false);
	if (ret)
		return ret;

	return bcm_sf2_cfp_unslice_ipv6(priv, v6_m_spec->ip6src,
					&v6_m_spec->psrc, true);
}

static int bcm_sf2_cfp_rule_get(struct bcm_sf2_priv *priv, int port,
				struct ethtool_rxnfc *nfc)
{
	u32 reg, ipv4_or_chain_id;
	unsigned int queue_num;
	int ret;

	bcm_sf2_cfp_rule_addr_set(priv, nfc->fs.location);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_READ | ACT_POL_RAM);
	if (ret)
		return ret;

	reg = core_readl(priv, CORE_ACT_POL_DATA0);

	ret = bcm_sf2_cfp_op(priv, OP_SEL_READ | TCAM_SEL);
	if (ret)
		return ret;

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

	/* Extract the L3_FRAMING or CHAIN_ID */
	reg = core_readl(priv, CORE_CFP_DATA_PORT(6));

	/* With IPv6 rules this would contain a non-zero chain ID since
	 * we reserve entry 0 and it cannot be used. So if we read 0 here
	 * this means an IPv4 rule.
	 */
	ipv4_or_chain_id = (reg >> L3_FRAMING_SHIFT) & 0xff;
	if (ipv4_or_chain_id == 0)
		ret = bcm_sf2_cfp_ipv4_rule_get(priv, port, &nfc->fs);
	else
		ret = bcm_sf2_cfp_ipv6_rule_get(priv, port, &nfc->fs,
						ipv4_or_chain_id);
	if (ret)
		return ret;

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

	for_each_set_bit_from(index, priv->cfp.unique, priv->num_cfp_rules) {
		rule_locs[rules_cnt] = index;
		rules_cnt++;
	}

	/* Put the TCAM size here */
	nfc->data = bcm_sf2_cfp_rule_size(priv);
	nfc->rule_cnt = rules_cnt;

	return 0;
}

int bcm_sf2_get_rxnfc(struct dsa_switch *ds, int port,
		      struct ethtool_rxnfc *nfc, u32 *rule_locs)
{
	struct net_device *p = ds->ports[port].cpu_dp->master;
	struct bcm_sf2_priv *priv = bcm_sf2_to_priv(ds);
	int ret = 0;

	mutex_lock(&priv->cfp.lock);

	switch (nfc->cmd) {
	case ETHTOOL_GRXCLSRLCNT:
		/* Subtract the default, unusable rule */
		nfc->rule_cnt = bitmap_weight(priv->cfp.unique,
					      priv->num_cfp_rules) - 1;
		/* We support specifying rule locations */
		nfc->data |= RX_CLS_LOC_SPECIAL;
		break;
	case ETHTOOL_GRXCLSRULE:
		ret = bcm_sf2_cfp_rule_get(priv, port, nfc);
		break;
	case ETHTOOL_GRXCLSRLALL:
		ret = bcm_sf2_cfp_rule_get_all(priv, port, nfc, rule_locs);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&priv->cfp.lock);

	if (ret)
		return ret;

	/* Pass up the commands to the attached master network device */
	if (p->ethtool_ops->get_rxnfc) {
		ret = p->ethtool_ops->get_rxnfc(p, nfc, rule_locs);
		if (ret == -EOPNOTSUPP)
			ret = 0;
	}

	return ret;
}

int bcm_sf2_set_rxnfc(struct dsa_switch *ds, int port,
		      struct ethtool_rxnfc *nfc)
{
	struct net_device *p = ds->ports[port].cpu_dp->master;
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

	if (ret)
		return ret;

	/* Pass up the commands to the attached master network device.
	 * This can fail, so rollback the operation if we need to.
	 */
	if (p->ethtool_ops->set_rxnfc) {
		ret = p->ethtool_ops->set_rxnfc(p, nfc);
		if (ret && ret != -EOPNOTSUPP) {
			mutex_lock(&priv->cfp.lock);
			bcm_sf2_cfp_rule_del(priv, port, nfc->fs.location);
			mutex_unlock(&priv->cfp.lock);
		} else {
			ret = 0;
		}
	}

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
