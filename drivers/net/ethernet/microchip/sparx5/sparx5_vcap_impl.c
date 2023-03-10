// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver VCAP implementation
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 *
 * The Sparx5 Chip Register Model can be browsed at this location:
 * https://github.com/microchip-ung/sparx-5_reginfo
 */

#include "vcap_api_debugfs.h"
#include "sparx5_main_regs.h"
#include "sparx5_main.h"
#include "sparx5_vcap_impl.h"
#include "sparx5_vcap_ag_api.h"
#include "sparx5_vcap_debugfs.h"

#define SUPER_VCAP_BLK_SIZE 3072 /* addresses per Super VCAP block */
#define STREAMSIZE (64 * 4)  /* bytes in the VCAP cache area */

#define SPARX5_IS2_LOOKUPS 4
#define VCAP_IS2_KEYSEL(_ena, _noneth, _v4_mc, _v4_uc, _v6_mc, _v6_uc, _arp) \
	(ANA_ACL_VCAP_S2_KEY_SEL_KEY_SEL_ENA_SET(_ena) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_NON_ETH_KEY_SEL_SET(_noneth) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_IP4_MC_KEY_SEL_SET(_v4_mc) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_IP4_UC_KEY_SEL_SET(_v4_uc) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_IP6_MC_KEY_SEL_SET(_v6_mc) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_IP6_UC_KEY_SEL_SET(_v6_uc) | \
	 ANA_ACL_VCAP_S2_KEY_SEL_ARP_KEY_SEL_SET(_arp))

#define SPARX5_IS0_LOOKUPS 6
#define VCAP_IS0_KEYSEL(_ena, _etype, _ipv4, _ipv6, _mpls_uc, _mpls_mc, _mlbs) \
	(ANA_CL_ADV_CL_CFG_LOOKUP_ENA_SET(_ena) | \
	ANA_CL_ADV_CL_CFG_ETYPE_CLM_KEY_SEL_SET(_etype) | \
	ANA_CL_ADV_CL_CFG_IP4_CLM_KEY_SEL_SET(_ipv4) | \
	ANA_CL_ADV_CL_CFG_IP6_CLM_KEY_SEL_SET(_ipv6) | \
	ANA_CL_ADV_CL_CFG_MPLS_UC_CLM_KEY_SEL_SET(_mpls_uc) | \
	ANA_CL_ADV_CL_CFG_MPLS_MC_CLM_KEY_SEL_SET(_mpls_mc) | \
	ANA_CL_ADV_CL_CFG_MLBS_CLM_KEY_SEL_SET(_mlbs))

#define SPARX5_ES0_LOOKUPS 1
#define VCAP_ES0_KEYSEL(_key) (REW_RTAG_ETAG_CTRL_ES0_ISDX_KEY_ENA_SET(_key))
#define SPARX5_STAT_ESDX_GRN_PKTS  0x300
#define SPARX5_STAT_ESDX_YEL_PKTS  0x301

#define SPARX5_ES2_LOOKUPS 2
#define VCAP_ES2_KEYSEL(_ena, _arp, _ipv4, _ipv6) \
	(EACL_VCAP_ES2_KEY_SEL_KEY_ENA_SET(_ena) | \
	EACL_VCAP_ES2_KEY_SEL_ARP_KEY_SEL_SET(_arp) | \
	EACL_VCAP_ES2_KEY_SEL_IP4_KEY_SEL_SET(_ipv4) | \
	EACL_VCAP_ES2_KEY_SEL_IP6_KEY_SEL_SET(_ipv6))

static struct sparx5_vcap_inst {
	enum vcap_type vtype; /* type of vcap */
	int vinst; /* instance number within the same type */
	int lookups; /* number of lookups in this vcap type */
	int lookups_per_instance; /* number of lookups in this instance */
	int first_cid; /* first chain id in this vcap */
	int last_cid; /* last chain id in this vcap */
	int count; /* number of available addresses, not in super vcap */
	int map_id; /* id in the super vcap block mapping (if applicable) */
	int blockno; /* starting block in super vcap (if applicable) */
	int blocks; /* number of blocks in super vcap (if applicable) */
	bool ingress; /* is vcap in the ingress path */
} sparx5_vcap_inst_cfg[] = {
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-0 */
		.vinst = 0,
		.map_id = 1,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L0,
		.last_cid = SPARX5_VCAP_CID_IS0_L2 - 1,
		.blockno = 8, /* Maps block 8-9 */
		.blocks = 2,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-1 */
		.vinst = 1,
		.map_id = 2,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L2,
		.last_cid = SPARX5_VCAP_CID_IS0_L4 - 1,
		.blockno = 6, /* Maps block 6-7 */
		.blocks = 2,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS0, /* CLM-2 */
		.vinst = 2,
		.map_id = 3,
		.lookups = SPARX5_IS0_LOOKUPS,
		.lookups_per_instance = SPARX5_IS0_LOOKUPS / 3,
		.first_cid = SPARX5_VCAP_CID_IS0_L4,
		.last_cid = SPARX5_VCAP_CID_IS0_MAX,
		.blockno = 4, /* Maps block 4-5 */
		.blocks = 2,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-0 */
		.vinst = 0,
		.map_id = 4,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L0,
		.last_cid = SPARX5_VCAP_CID_IS2_L2 - 1,
		.blockno = 0, /* Maps block 0-1 */
		.blocks = 2,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-1 */
		.vinst = 1,
		.map_id = 5,
		.lookups = SPARX5_IS2_LOOKUPS,
		.lookups_per_instance = SPARX5_IS2_LOOKUPS / 2,
		.first_cid = SPARX5_VCAP_CID_IS2_L2,
		.last_cid = SPARX5_VCAP_CID_IS2_MAX,
		.blockno = 2, /* Maps block 2-3 */
		.blocks = 2,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_ES0,
		.lookups = SPARX5_ES0_LOOKUPS,
		.lookups_per_instance = SPARX5_ES0_LOOKUPS,
		.first_cid = SPARX5_VCAP_CID_ES0_L0,
		.last_cid = SPARX5_VCAP_CID_ES0_MAX,
		.count = 4096, /* Addresses according to datasheet */
		.ingress = false,
	},
	{
		.vtype = VCAP_TYPE_ES2,
		.lookups = SPARX5_ES2_LOOKUPS,
		.lookups_per_instance = SPARX5_ES2_LOOKUPS,
		.first_cid = SPARX5_VCAP_CID_ES2_L0,
		.last_cid = SPARX5_VCAP_CID_ES2_MAX,
		.count = 12288, /* Addresses according to datasheet */
		.ingress = false,
	},
};

/* These protocols have dedicated keysets in IS0 and a TC dissector */
static u16 sparx5_vcap_is0_known_etypes[] = {
	ETH_P_ALL,
	ETH_P_IP,
	ETH_P_IPV6,
};

/* These protocols have dedicated keysets in IS2 and a TC dissector */
static u16 sparx5_vcap_is2_known_etypes[] = {
	ETH_P_ALL,
	ETH_P_ARP,
	ETH_P_IP,
	ETH_P_IPV6,
};

/* These protocols have dedicated keysets in ES2 and a TC dissector */
static u16 sparx5_vcap_es2_known_etypes[] = {
	ETH_P_ALL,
	ETH_P_ARP,
	ETH_P_IP,
	ETH_P_IPV6,
};

static void sparx5_vcap_type_err(struct sparx5 *sparx5,
				 struct vcap_admin *admin,
				 const char *fname)
{
	pr_err("%s: vcap type: %s not supported\n",
	       fname, sparx5_vcaps[admin->vtype].name);
}

/* Await the super VCAP completion of the current operation */
static void sparx5_vcap_wait_super_update(struct sparx5 *sparx5)
{
	u32 value;

	read_poll_timeout(spx5_rd, value,
			  !VCAP_SUPER_CTRL_UPDATE_SHOT_GET(value), 500, 10000,
			  false, sparx5, VCAP_SUPER_CTRL);
}

/* Await the ES0 VCAP completion of the current operation */
static void sparx5_vcap_wait_es0_update(struct sparx5 *sparx5)
{
	u32 value;

	read_poll_timeout(spx5_rd, value,
			  !VCAP_ES0_CTRL_UPDATE_SHOT_GET(value), 500, 10000,
			  false, sparx5, VCAP_ES0_CTRL);
}

/* Await the ES2 VCAP completion of the current operation */
static void sparx5_vcap_wait_es2_update(struct sparx5 *sparx5)
{
	u32 value;

	read_poll_timeout(spx5_rd, value,
			  !VCAP_ES2_CTRL_UPDATE_SHOT_GET(value), 500, 10000,
			  false, sparx5, VCAP_ES2_CTRL);
}

/* Initializing a VCAP address range */
static void _sparx5_vcap_range_init(struct sparx5 *sparx5,
				    struct vcap_admin *admin,
				    u32 addr, u32 count)
{
	u32 size = count - 1;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
	case VCAP_TYPE_IS2:
		spx5_wr(VCAP_SUPER_CFG_MV_NUM_POS_SET(0) |
			VCAP_SUPER_CFG_MV_SIZE_SET(size),
			sparx5, VCAP_SUPER_CFG);
		spx5_wr(VCAP_SUPER_CTRL_UPDATE_CMD_SET(VCAP_CMD_INITIALIZE) |
			VCAP_SUPER_CTRL_UPDATE_ENTRY_DIS_SET(0) |
			VCAP_SUPER_CTRL_UPDATE_ACTION_DIS_SET(0) |
			VCAP_SUPER_CTRL_UPDATE_CNT_DIS_SET(0) |
			VCAP_SUPER_CTRL_UPDATE_ADDR_SET(addr) |
			VCAP_SUPER_CTRL_CLEAR_CACHE_SET(true) |
			VCAP_SUPER_CTRL_UPDATE_SHOT_SET(true),
			sparx5, VCAP_SUPER_CTRL);
		sparx5_vcap_wait_super_update(sparx5);
		break;
	case VCAP_TYPE_ES0:
		spx5_wr(VCAP_ES0_CFG_MV_NUM_POS_SET(0) |
				VCAP_ES0_CFG_MV_SIZE_SET(size),
			sparx5, VCAP_ES0_CFG);
		spx5_wr(VCAP_ES0_CTRL_UPDATE_CMD_SET(VCAP_CMD_INITIALIZE) |
				VCAP_ES0_CTRL_UPDATE_ENTRY_DIS_SET(0) |
				VCAP_ES0_CTRL_UPDATE_ACTION_DIS_SET(0) |
				VCAP_ES0_CTRL_UPDATE_CNT_DIS_SET(0) |
				VCAP_ES0_CTRL_UPDATE_ADDR_SET(addr) |
				VCAP_ES0_CTRL_CLEAR_CACHE_SET(true) |
				VCAP_ES0_CTRL_UPDATE_SHOT_SET(true),
			sparx5, VCAP_ES0_CTRL);
		sparx5_vcap_wait_es0_update(sparx5);
		break;
	case VCAP_TYPE_ES2:
		spx5_wr(VCAP_ES2_CFG_MV_NUM_POS_SET(0) |
			VCAP_ES2_CFG_MV_SIZE_SET(size),
			sparx5, VCAP_ES2_CFG);
		spx5_wr(VCAP_ES2_CTRL_UPDATE_CMD_SET(VCAP_CMD_INITIALIZE) |
			VCAP_ES2_CTRL_UPDATE_ENTRY_DIS_SET(0) |
			VCAP_ES2_CTRL_UPDATE_ACTION_DIS_SET(0) |
			VCAP_ES2_CTRL_UPDATE_CNT_DIS_SET(0) |
			VCAP_ES2_CTRL_UPDATE_ADDR_SET(addr) |
			VCAP_ES2_CTRL_CLEAR_CACHE_SET(true) |
			VCAP_ES2_CTRL_UPDATE_SHOT_SET(true),
			sparx5, VCAP_ES2_CTRL);
		sparx5_vcap_wait_es2_update(sparx5);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

/* Initializing VCAP rule data area */
static void sparx5_vcap_block_init(struct sparx5 *sparx5,
				   struct vcap_admin *admin)
{
	_sparx5_vcap_range_init(sparx5, admin, admin->first_valid_addr,
				admin->last_valid_addr -
					admin->first_valid_addr);
}

/* Get the keyset name from the sparx5 VCAP model */
static const char *sparx5_vcap_keyset_name(struct net_device *ndev,
					   enum vcap_keyfield_set keyset)
{
	struct sparx5_port *port = netdev_priv(ndev);

	return vcap_keyset_name(port->sparx5->vcap_ctrl, keyset);
}

/* Check if this is the first lookup of IS0 */
static bool sparx5_vcap_is0_is_first_chain(struct vcap_rule *rule)
{
	return (rule->vcap_chain_id >= SPARX5_VCAP_CID_IS0_L0 &&
		rule->vcap_chain_id < SPARX5_VCAP_CID_IS0_L1) ||
		((rule->vcap_chain_id >= SPARX5_VCAP_CID_IS0_L2 &&
		  rule->vcap_chain_id < SPARX5_VCAP_CID_IS0_L3)) ||
		((rule->vcap_chain_id >= SPARX5_VCAP_CID_IS0_L4 &&
		  rule->vcap_chain_id < SPARX5_VCAP_CID_IS0_L5));
}

/* Check if this is the first lookup of IS2 */
static bool sparx5_vcap_is2_is_first_chain(struct vcap_rule *rule)
{
	return (rule->vcap_chain_id >= SPARX5_VCAP_CID_IS2_L0 &&
		rule->vcap_chain_id < SPARX5_VCAP_CID_IS2_L1) ||
		((rule->vcap_chain_id >= SPARX5_VCAP_CID_IS2_L2 &&
		  rule->vcap_chain_id < SPARX5_VCAP_CID_IS2_L3));
}

static bool sparx5_vcap_es2_is_first_chain(struct vcap_rule *rule)
{
	return (rule->vcap_chain_id >= SPARX5_VCAP_CID_ES2_L0 &&
		rule->vcap_chain_id < SPARX5_VCAP_CID_ES2_L1);
}

/* Set the narrow range ingress port mask on a rule */
static void sparx5_vcap_add_ingress_range_port_mask(struct vcap_rule *rule,
						    struct net_device *ndev)
{
	struct sparx5_port *port = netdev_priv(ndev);
	u32 port_mask;
	u32 range;

	range = port->portno / BITS_PER_TYPE(u32);
	/* Port bit set to match-any */
	port_mask = ~BIT(port->portno % BITS_PER_TYPE(u32));
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK_SEL, 0, 0xf);
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK_RNG, range, 0xf);
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK, 0, port_mask);
}

/* Set the wide range ingress port mask on a rule */
static void sparx5_vcap_add_wide_port_mask(struct vcap_rule *rule,
					   struct net_device *ndev)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct vcap_u72_key port_mask;
	u32 range;

	/* Port bit set to match-any */
	memset(port_mask.value, 0, sizeof(port_mask.value));
	memset(port_mask.mask, 0xff, sizeof(port_mask.mask));
	range = port->portno / BITS_PER_BYTE;
	port_mask.mask[range] = ~BIT(port->portno % BITS_PER_BYTE);
	vcap_rule_add_key_u72(rule, VCAP_KF_IF_IGR_PORT_MASK, &port_mask);
}

static void sparx5_vcap_add_egress_range_port_mask(struct vcap_rule *rule,
						   struct net_device *ndev)
{
	struct sparx5_port *port = netdev_priv(ndev);
	u32 port_mask;
	u32 range;

	/* Mask range selects:
	 * 0-2: Physical/Logical egress port number 0-31, 32–63, 64.
	 * 3-5: Virtual Interface Number 0-31, 32-63, 64.
	 * 6: CPU queue Number 0-7.
	 *
	 * Use physical/logical port ranges (0-2)
	 */
	range = port->portno / BITS_PER_TYPE(u32);
	/* Port bit set to match-any */
	port_mask = ~BIT(port->portno % BITS_PER_TYPE(u32));
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_EGR_PORT_MASK_RNG, range, 0xf);
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_EGR_PORT_MASK, 0, port_mask);
}

/* Convert IS0 chain id to vcap lookup id */
static int sparx5_vcap_is0_cid_to_lookup(int cid)
{
	int lookup = 0;

	if (cid >= SPARX5_VCAP_CID_IS0_L1 && cid < SPARX5_VCAP_CID_IS0_L2)
		lookup = 1;
	else if (cid >= SPARX5_VCAP_CID_IS0_L2 && cid < SPARX5_VCAP_CID_IS0_L3)
		lookup = 2;
	else if (cid >= SPARX5_VCAP_CID_IS0_L3 && cid < SPARX5_VCAP_CID_IS0_L4)
		lookup = 3;
	else if (cid >= SPARX5_VCAP_CID_IS0_L4 && cid < SPARX5_VCAP_CID_IS0_L5)
		lookup = 4;
	else if (cid >= SPARX5_VCAP_CID_IS0_L5 && cid < SPARX5_VCAP_CID_IS0_MAX)
		lookup = 5;

	return lookup;
}

/* Convert IS2 chain id to vcap lookup id */
static int sparx5_vcap_is2_cid_to_lookup(int cid)
{
	int lookup = 0;

	if (cid >= SPARX5_VCAP_CID_IS2_L1 && cid < SPARX5_VCAP_CID_IS2_L2)
		lookup = 1;
	else if (cid >= SPARX5_VCAP_CID_IS2_L2 && cid < SPARX5_VCAP_CID_IS2_L3)
		lookup = 2;
	else if (cid >= SPARX5_VCAP_CID_IS2_L3 && cid < SPARX5_VCAP_CID_IS2_MAX)
		lookup = 3;

	return lookup;
}

/* Convert ES2 chain id to vcap lookup id */
static int sparx5_vcap_es2_cid_to_lookup(int cid)
{
	int lookup = 0;

	if (cid >= SPARX5_VCAP_CID_ES2_L1)
		lookup = 1;

	return lookup;
}

/* Add ethernet type IS0 keyset to a list */
static void
sparx5_vcap_is0_get_port_etype_keysets(struct vcap_keyset_list *keysetlist,
				       u32 value)
{
	switch (ANA_CL_ADV_CL_CFG_ETYPE_CLM_KEY_SEL_GET(value)) {
	case VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL_7TUPLE);
		break;
	case VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL_5TUPLE_IP4);
		break;
	}
}

/* Return the list of keysets for the vcap port configuration */
static int sparx5_vcap_is0_get_port_keysets(struct net_device *ndev,
					    int lookup,
					    struct vcap_keyset_list *keysetlist,
					    u16 l3_proto)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	int portno = port->portno;
	u32 value;

	value = spx5_rd(sparx5, ANA_CL_ADV_CL_CFG(portno, lookup));

	/* Collect all keysets for the port in a list */
	if (l3_proto == ETH_P_ALL)
		sparx5_vcap_is0_get_port_etype_keysets(keysetlist, value);

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IP)
		switch (ANA_CL_ADV_CL_CFG_IP4_CLM_KEY_SEL_GET(value)) {
		case VCAP_IS0_PS_ETYPE_DEFAULT:
			sparx5_vcap_is0_get_port_etype_keysets(keysetlist,
							       value);
			break;
		case VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE:
			vcap_keyset_list_add(keysetlist,
					     VCAP_KFS_NORMAL_7TUPLE);
			break;
		case VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4:
			vcap_keyset_list_add(keysetlist,
					     VCAP_KFS_NORMAL_5TUPLE_IP4);
			break;
		}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IPV6)
		switch (ANA_CL_ADV_CL_CFG_IP6_CLM_KEY_SEL_GET(value)) {
		case VCAP_IS0_PS_ETYPE_DEFAULT:
			sparx5_vcap_is0_get_port_etype_keysets(keysetlist,
							       value);
			break;
		case VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE:
			vcap_keyset_list_add(keysetlist,
					     VCAP_KFS_NORMAL_7TUPLE);
			break;
		case VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4:
			vcap_keyset_list_add(keysetlist,
					     VCAP_KFS_NORMAL_5TUPLE_IP4);
			break;
		}

	if (l3_proto != ETH_P_IP && l3_proto != ETH_P_IPV6)
		sparx5_vcap_is0_get_port_etype_keysets(keysetlist, value);
	return 0;
}

/* Return the list of keysets for the vcap port configuration */
static int sparx5_vcap_is2_get_port_keysets(struct net_device *ndev,
					    int lookup,
					    struct vcap_keyset_list *keysetlist,
					    u16 l3_proto)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	int portno = port->portno;
	u32 value;

	value = spx5_rd(sparx5, ANA_ACL_VCAP_S2_KEY_SEL(portno, lookup));

	/* Collect all keysets for the port in a list */
	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_ARP) {
		switch (ANA_ACL_VCAP_S2_KEY_SEL_ARP_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_ARP_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_IS2_PS_ARP_ARP:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_ARP);
			break;
		}
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IP) {
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP4_UC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV4_UC_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_IS2_PS_IPV4_UC_IP4_TCP_UDP_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
			break;
		case VCAP_IS2_PS_IPV4_UC_IP_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		}

		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP4_MC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV4_MC_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_IS2_PS_IPV4_MC_IP4_TCP_UDP_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
			break;
		case VCAP_IS2_PS_IPV4_MC_IP_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		}
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IPV6) {
		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP6_UC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV6_UC_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_IS2_PS_IPV6_UC_IP_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		case VCAP_IS2_PS_IPV6_UC_IP6_STD:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_STD);
			break;
		case VCAP_IS2_PS_IPV6_UC_IP4_TCP_UDP_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
			break;
		}

		switch (ANA_ACL_VCAP_S2_KEY_SEL_IP6_MC_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_IPV6_MC_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_IS2_PS_IPV6_MC_IP_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		case VCAP_IS2_PS_IPV6_MC_IP6_STD:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_STD);
			break;
		case VCAP_IS2_PS_IPV6_MC_IP4_TCP_UDP_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
			break;
		case VCAP_IS2_PS_IPV6_MC_IP6_VID:
			/* Not used */
			break;
		}
	}

	if (l3_proto != ETH_P_ARP && l3_proto != ETH_P_IP &&
	    l3_proto != ETH_P_IPV6) {
		switch (ANA_ACL_VCAP_S2_KEY_SEL_NON_ETH_KEY_SEL_GET(value)) {
		case VCAP_IS2_PS_NONETH_MAC_ETYPE:
			/* IS2 non-classified frames generate MAC_ETYPE */
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		}
	}
	return 0;
}

/* Return the keysets for the vcap port IP4 traffic class configuration */
static void
sparx5_vcap_es2_get_port_ipv4_keysets(struct vcap_keyset_list *keysetlist,
				      u32 value)
{
	switch (EACL_VCAP_ES2_KEY_SEL_IP4_KEY_SEL_GET(value)) {
	case VCAP_ES2_PS_IPV4_MAC_ETYPE:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
		break;
	case VCAP_ES2_PS_IPV4_IP_7TUPLE:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
		break;
	case VCAP_ES2_PS_IPV4_IP4_TCP_UDP_VID:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
		break;
	case VCAP_ES2_PS_IPV4_IP4_TCP_UDP_OTHER:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
		vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
		break;
	case VCAP_ES2_PS_IPV4_IP4_VID:
		/* Not used */
		break;
	case VCAP_ES2_PS_IPV4_IP4_OTHER:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
		break;
	}
}

/* Return the list of keysets for the vcap port configuration */
static int sparx5_vcap_es0_get_port_keysets(struct net_device *ndev,
					    struct vcap_keyset_list *keysetlist,
					    u16 l3_proto)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	int portno = port->portno;
	u32 value;

	value = spx5_rd(sparx5, REW_RTAG_ETAG_CTRL(portno));

	/* Collect all keysets for the port in a list */
	switch (REW_RTAG_ETAG_CTRL_ES0_ISDX_KEY_ENA_GET(value)) {
	case VCAP_ES0_PS_NORMAL_SELECTION:
	case VCAP_ES0_PS_FORCE_ISDX_LOOKUPS:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_ISDX);
		break;
	default:
		break;
	}
	return 0;
}

/* Return the list of keysets for the vcap port configuration */
static int sparx5_vcap_es2_get_port_keysets(struct net_device *ndev,
					    int lookup,
					    struct vcap_keyset_list *keysetlist,
					    u16 l3_proto)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	int portno = port->portno;
	u32 value;

	value = spx5_rd(sparx5, EACL_VCAP_ES2_KEY_SEL(portno, lookup));

	/* Collect all keysets for the port in a list */
	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_ARP) {
		switch (EACL_VCAP_ES2_KEY_SEL_ARP_KEY_SEL_GET(value)) {
		case VCAP_ES2_PS_ARP_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_ES2_PS_ARP_ARP:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_ARP);
			break;
		}
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IP)
		sparx5_vcap_es2_get_port_ipv4_keysets(keysetlist, value);

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IPV6) {
		switch (EACL_VCAP_ES2_KEY_SEL_IP6_KEY_SEL_GET(value)) {
		case VCAP_ES2_PS_IPV6_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE_VID:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			break;
		case VCAP_ES2_PS_IPV6_IP_7TUPLE_STD:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP_7TUPLE);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_STD);
			break;
		case VCAP_ES2_PS_IPV6_IP6_VID:
			/* Not used */
			break;
		case VCAP_ES2_PS_IPV6_IP6_STD:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_STD);
			break;
		case VCAP_ES2_PS_IPV6_IP4_DOWNGRADE:
			sparx5_vcap_es2_get_port_ipv4_keysets(keysetlist,
							      value);
			break;
		}
	}

	if (l3_proto != ETH_P_ARP && l3_proto != ETH_P_IP &&
	    l3_proto != ETH_P_IPV6) {
		vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
	}
	return 0;
}

/* Get the port keyset for the vcap lookup */
int sparx5_vcap_get_port_keyset(struct net_device *ndev,
				struct vcap_admin *admin,
				int cid,
				u16 l3_proto,
				struct vcap_keyset_list *kslist)
{
	int lookup, err = -EINVAL;
	struct sparx5_port *port;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		lookup = sparx5_vcap_is0_cid_to_lookup(cid);
		err = sparx5_vcap_is0_get_port_keysets(ndev, lookup, kslist,
						       l3_proto);
		break;
	case VCAP_TYPE_IS2:
		lookup = sparx5_vcap_is2_cid_to_lookup(cid);
		err = sparx5_vcap_is2_get_port_keysets(ndev, lookup, kslist,
						       l3_proto);
		break;
	case VCAP_TYPE_ES0:
		err = sparx5_vcap_es0_get_port_keysets(ndev, kslist, l3_proto);
		break;
	case VCAP_TYPE_ES2:
		lookup = sparx5_vcap_es2_cid_to_lookup(cid);
		err = sparx5_vcap_es2_get_port_keysets(ndev, lookup, kslist,
						       l3_proto);
		break;
	default:
		port = netdev_priv(ndev);
		sparx5_vcap_type_err(port->sparx5, admin, __func__);
		break;
	}
	return err;
}

/* Check if the ethertype is supported by the vcap port classification */
bool sparx5_vcap_is_known_etype(struct vcap_admin *admin, u16 etype)
{
	const u16 *known_etypes;
	int size, idx;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		known_etypes = sparx5_vcap_is0_known_etypes;
		size = ARRAY_SIZE(sparx5_vcap_is0_known_etypes);
		break;
	case VCAP_TYPE_IS2:
		known_etypes = sparx5_vcap_is2_known_etypes;
		size = ARRAY_SIZE(sparx5_vcap_is2_known_etypes);
		break;
	case VCAP_TYPE_ES0:
		return true;
	case VCAP_TYPE_ES2:
		known_etypes = sparx5_vcap_es2_known_etypes;
		size = ARRAY_SIZE(sparx5_vcap_es2_known_etypes);
		break;
	default:
		return false;
	}
	for (idx = 0; idx < size; ++idx)
		if (known_etypes[idx] == etype)
			return true;
	return false;
}

/* API callback used for validating a field keyset (check the port keysets) */
static enum vcap_keyfield_set
sparx5_vcap_validate_keyset(struct net_device *ndev,
			    struct vcap_admin *admin,
			    struct vcap_rule *rule,
			    struct vcap_keyset_list *kslist,
			    u16 l3_proto)
{
	struct vcap_keyset_list keysetlist = {};
	enum vcap_keyfield_set keysets[10] = {};
	struct sparx5_port *port;
	int idx, jdx, lookup;

	if (!kslist || kslist->cnt == 0)
		return VCAP_KFS_NO_VALUE;

	keysetlist.max = ARRAY_SIZE(keysets);
	keysetlist.keysets = keysets;

	/* Get a list of currently configured keysets in the lookups */
	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		lookup = sparx5_vcap_is0_cid_to_lookup(rule->vcap_chain_id);
		sparx5_vcap_is0_get_port_keysets(ndev, lookup, &keysetlist,
						 l3_proto);
		break;
	case VCAP_TYPE_IS2:
		lookup = sparx5_vcap_is2_cid_to_lookup(rule->vcap_chain_id);
		sparx5_vcap_is2_get_port_keysets(ndev, lookup, &keysetlist,
						 l3_proto);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_get_port_keysets(ndev, &keysetlist, l3_proto);
		break;
	case VCAP_TYPE_ES2:
		lookup = sparx5_vcap_es2_cid_to_lookup(rule->vcap_chain_id);
		sparx5_vcap_es2_get_port_keysets(ndev, lookup, &keysetlist,
						 l3_proto);
		break;
	default:
		port = netdev_priv(ndev);
		sparx5_vcap_type_err(port->sparx5, admin, __func__);
		break;
	}

	/* Check if there is a match and return the match */
	for (idx = 0; idx < kslist->cnt; ++idx)
		for (jdx = 0; jdx < keysetlist.cnt; ++jdx)
			if (kslist->keysets[idx] == keysets[jdx])
				return kslist->keysets[idx];

	pr_err("%s:%d: %s not supported in port key selection\n",
	       __func__, __LINE__,
	       sparx5_vcap_keyset_name(ndev, kslist->keysets[0]));

	return -ENOENT;
}

static void sparx5_vcap_ingress_add_default_fields(struct net_device *ndev,
						   struct vcap_admin *admin,
						   struct vcap_rule *rule)
{
	const struct vcap_field *field;
	bool is_first;

	/* Add ingress port mask matching the net device */
	field = vcap_lookup_keyfield(rule, VCAP_KF_IF_IGR_PORT_MASK);
	if (field && field->width == SPX5_PORTS)
		sparx5_vcap_add_wide_port_mask(rule, ndev);
	else if (field && field->width == BITS_PER_TYPE(u32))
		sparx5_vcap_add_ingress_range_port_mask(rule, ndev);
	else
		pr_err("%s:%d: %s: could not add an ingress port mask for: %s\n",
		       __func__, __LINE__, netdev_name(ndev),
		       sparx5_vcap_keyset_name(ndev, rule->keyset));

	if (admin->vtype == VCAP_TYPE_IS0)
		is_first = sparx5_vcap_is0_is_first_chain(rule);
	else
		is_first = sparx5_vcap_is2_is_first_chain(rule);

	/* Add key that selects the first/second lookup */
	if (is_first)
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_1);
	else
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_0);
}

static void sparx5_vcap_es0_add_default_fields(struct net_device *ndev,
					       struct vcap_admin *admin,
					       struct vcap_rule *rule)
{
	struct sparx5_port *port = netdev_priv(ndev);

	vcap_rule_add_key_u32(rule, VCAP_KF_IF_EGR_PORT_NO, port->portno, ~0);
	/* Match untagged frames if there was no VLAN key */
	vcap_rule_add_key_u32(rule, VCAP_KF_8021Q_TPID, SPX5_TPID_SEL_UNTAGGED,
			      ~0);
}

static void sparx5_vcap_es2_add_default_fields(struct net_device *ndev,
					       struct vcap_admin *admin,
					       struct vcap_rule *rule)
{
	const struct vcap_field *field;
	bool is_first;

	/* Add egress port mask matching the net device */
	field = vcap_lookup_keyfield(rule, VCAP_KF_IF_EGR_PORT_MASK);
	if (field)
		sparx5_vcap_add_egress_range_port_mask(rule, ndev);

	/* Add key that selects the first/second lookup */
	is_first = sparx5_vcap_es2_is_first_chain(rule);

	if (is_first)
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_1);
	else
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_0);
}

/* API callback used for adding default fields to a rule */
static void sparx5_vcap_add_default_fields(struct net_device *ndev,
					   struct vcap_admin *admin,
					   struct vcap_rule *rule)
{
	struct sparx5_port *port;

	/* add the lookup bit */
	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
	case VCAP_TYPE_IS2:
		sparx5_vcap_ingress_add_default_fields(ndev, admin, rule);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_add_default_fields(ndev, admin, rule);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_add_default_fields(ndev, admin, rule);
		break;
	default:
		port = netdev_priv(ndev);
		sparx5_vcap_type_err(port->sparx5, admin, __func__);
		break;
	}
}

/* API callback used for erasing the vcap cache area (not the register area) */
static void sparx5_vcap_cache_erase(struct vcap_admin *admin)
{
	memset(admin->cache.keystream, 0, STREAMSIZE);
	memset(admin->cache.maskstream, 0, STREAMSIZE);
	memset(admin->cache.actionstream, 0, STREAMSIZE);
	memset(&admin->cache.counter, 0, sizeof(admin->cache.counter));
}

static void sparx5_vcap_is0_cache_write(struct sparx5 *sparx5,
					struct vcap_admin *admin,
					enum vcap_selection sel,
					u32 start,
					u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	switch (sel) {
	case VCAP_SEL_ENTRY:
		for (idx = 0; idx < count; ++idx) {
			/* Avoid 'match-off' by setting value & mask */
			spx5_wr(keystr[idx] & mskstr[idx], sparx5,
				VCAP_SUPER_VCAP_ENTRY_DAT(idx));
			spx5_wr(~mskstr[idx], sparx5,
				VCAP_SUPER_VCAP_MASK_DAT(idx));
		}
		break;
	case VCAP_SEL_ACTION:
		for (idx = 0; idx < count; ++idx)
			spx5_wr(actstr[idx], sparx5,
				VCAP_SUPER_VCAP_ACTION_DAT(idx));
		break;
	case VCAP_SEL_ALL:
		pr_err("%s:%d: cannot write all streams at once\n",
		       __func__, __LINE__);
		break;
	default:
		break;
	}

	if (sel & VCAP_SEL_COUNTER)
		spx5_wr(admin->cache.counter, sparx5,
			VCAP_SUPER_VCAP_CNT_DAT(0));
}

static void sparx5_vcap_is2_cache_write(struct sparx5 *sparx5,
					struct vcap_admin *admin,
					enum vcap_selection sel,
					u32 start,
					u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	switch (sel) {
	case VCAP_SEL_ENTRY:
		for (idx = 0; idx < count; ++idx) {
			/* Avoid 'match-off' by setting value & mask */
			spx5_wr(keystr[idx] & mskstr[idx], sparx5,
				VCAP_SUPER_VCAP_ENTRY_DAT(idx));
			spx5_wr(~mskstr[idx], sparx5,
				VCAP_SUPER_VCAP_MASK_DAT(idx));
		}
		break;
	case VCAP_SEL_ACTION:
		for (idx = 0; idx < count; ++idx)
			spx5_wr(actstr[idx], sparx5,
				VCAP_SUPER_VCAP_ACTION_DAT(idx));
		break;
	case VCAP_SEL_ALL:
		pr_err("%s:%d: cannot write all streams at once\n",
		       __func__, __LINE__);
		break;
	default:
		break;
	}
	if (sel & VCAP_SEL_COUNTER) {
		start = start & 0xfff; /* counter limit */
		if (admin->vinst == 0)
			spx5_wr(admin->cache.counter, sparx5,
				ANA_ACL_CNT_A(start));
		else
			spx5_wr(admin->cache.counter, sparx5,
				ANA_ACL_CNT_B(start));
		spx5_wr(admin->cache.sticky, sparx5,
			VCAP_SUPER_VCAP_CNT_DAT(0));
	}
}

/* Use ESDX counters located in the XQS */
static void sparx5_es0_write_esdx_counter(struct sparx5 *sparx5,
					  struct vcap_admin *admin, u32 id)
{
	mutex_lock(&sparx5->queue_stats_lock);
	spx5_wr(XQS_STAT_CFG_STAT_VIEW_SET(id), sparx5, XQS_STAT_CFG);
	spx5_wr(admin->cache.counter, sparx5,
		XQS_CNT(SPARX5_STAT_ESDX_GRN_PKTS));
	spx5_wr(0, sparx5, XQS_CNT(SPARX5_STAT_ESDX_YEL_PKTS));
	mutex_unlock(&sparx5->queue_stats_lock);
}

static void sparx5_vcap_es0_cache_write(struct sparx5 *sparx5,
					struct vcap_admin *admin,
					enum vcap_selection sel,
					u32 start,
					u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	switch (sel) {
	case VCAP_SEL_ENTRY:
		for (idx = 0; idx < count; ++idx) {
			/* Avoid 'match-off' by setting value & mask */
			spx5_wr(keystr[idx] & mskstr[idx], sparx5,
				VCAP_ES0_VCAP_ENTRY_DAT(idx));
			spx5_wr(~mskstr[idx], sparx5,
				VCAP_ES0_VCAP_MASK_DAT(idx));
		}
		break;
	case VCAP_SEL_ACTION:
		for (idx = 0; idx < count; ++idx)
			spx5_wr(actstr[idx], sparx5,
				VCAP_ES0_VCAP_ACTION_DAT(idx));
		break;
	case VCAP_SEL_ALL:
		pr_err("%s:%d: cannot write all streams at once\n",
		       __func__, __LINE__);
		break;
	default:
		break;
	}
	if (sel & VCAP_SEL_COUNTER) {
		spx5_wr(admin->cache.counter, sparx5, VCAP_ES0_VCAP_CNT_DAT(0));
		sparx5_es0_write_esdx_counter(sparx5, admin, start);
	}
}

static void sparx5_vcap_es2_cache_write(struct sparx5 *sparx5,
					struct vcap_admin *admin,
					enum vcap_selection sel,
					u32 start,
					u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	switch (sel) {
	case VCAP_SEL_ENTRY:
		for (idx = 0; idx < count; ++idx) {
			/* Avoid 'match-off' by setting value & mask */
			spx5_wr(keystr[idx] & mskstr[idx], sparx5,
				VCAP_ES2_VCAP_ENTRY_DAT(idx));
			spx5_wr(~mskstr[idx], sparx5,
				VCAP_ES2_VCAP_MASK_DAT(idx));
		}
		break;
	case VCAP_SEL_ACTION:
		for (idx = 0; idx < count; ++idx)
			spx5_wr(actstr[idx], sparx5,
				VCAP_ES2_VCAP_ACTION_DAT(idx));
		break;
	case VCAP_SEL_ALL:
		pr_err("%s:%d: cannot write all streams at once\n",
		       __func__, __LINE__);
		break;
	default:
		break;
	}
	if (sel & VCAP_SEL_COUNTER) {
		start = start & 0x7ff; /* counter limit */
		spx5_wr(admin->cache.counter, sparx5, EACL_ES2_CNT(start));
		spx5_wr(admin->cache.sticky, sparx5, VCAP_ES2_VCAP_CNT_DAT(0));
	}
}

/* API callback used for writing to the VCAP cache */
static void sparx5_vcap_cache_write(struct net_device *ndev,
				    struct vcap_admin *admin,
				    enum vcap_selection sel,
				    u32 start,
				    u32 count)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		sparx5_vcap_is0_cache_write(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_IS2:
		sparx5_vcap_is2_cache_write(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_cache_write(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_cache_write(sparx5, admin, sel, start, count);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

static void sparx5_vcap_is0_cache_read(struct sparx5 *sparx5,
				       struct vcap_admin *admin,
				       enum vcap_selection sel,
				       u32 start,
				       u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	if (sel & VCAP_SEL_ENTRY) {
		for (idx = 0; idx < count; ++idx) {
			keystr[idx] = spx5_rd(sparx5,
					      VCAP_SUPER_VCAP_ENTRY_DAT(idx));
			mskstr[idx] = ~spx5_rd(sparx5,
					       VCAP_SUPER_VCAP_MASK_DAT(idx));
		}
	}

	if (sel & VCAP_SEL_ACTION)
		for (idx = 0; idx < count; ++idx)
			actstr[idx] = spx5_rd(sparx5,
					      VCAP_SUPER_VCAP_ACTION_DAT(idx));

	if (sel & VCAP_SEL_COUNTER) {
		admin->cache.counter =
			spx5_rd(sparx5, VCAP_SUPER_VCAP_CNT_DAT(0));
		admin->cache.sticky =
			spx5_rd(sparx5, VCAP_SUPER_VCAP_CNT_DAT(0));
	}
}

static void sparx5_vcap_is2_cache_read(struct sparx5 *sparx5,
				       struct vcap_admin *admin,
				       enum vcap_selection sel,
				       u32 start,
				       u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	if (sel & VCAP_SEL_ENTRY) {
		for (idx = 0; idx < count; ++idx) {
			keystr[idx] = spx5_rd(sparx5,
					      VCAP_SUPER_VCAP_ENTRY_DAT(idx));
			mskstr[idx] = ~spx5_rd(sparx5,
					       VCAP_SUPER_VCAP_MASK_DAT(idx));
		}
	}

	if (sel & VCAP_SEL_ACTION)
		for (idx = 0; idx < count; ++idx)
			actstr[idx] = spx5_rd(sparx5,
					      VCAP_SUPER_VCAP_ACTION_DAT(idx));

	if (sel & VCAP_SEL_COUNTER) {
		start = start & 0xfff; /* counter limit */
		if (admin->vinst == 0)
			admin->cache.counter =
				spx5_rd(sparx5, ANA_ACL_CNT_A(start));
		else
			admin->cache.counter =
				spx5_rd(sparx5, ANA_ACL_CNT_B(start));
		admin->cache.sticky =
			spx5_rd(sparx5, VCAP_SUPER_VCAP_CNT_DAT(0));
	}
}

/* Use ESDX counters located in the XQS */
static void sparx5_es0_read_esdx_counter(struct sparx5 *sparx5,
					 struct vcap_admin *admin, u32 id)
{
	u32 counter;

	mutex_lock(&sparx5->queue_stats_lock);
	spx5_wr(XQS_STAT_CFG_STAT_VIEW_SET(id), sparx5, XQS_STAT_CFG);
	counter = spx5_rd(sparx5, XQS_CNT(SPARX5_STAT_ESDX_GRN_PKTS)) +
		spx5_rd(sparx5, XQS_CNT(SPARX5_STAT_ESDX_YEL_PKTS));
	mutex_unlock(&sparx5->queue_stats_lock);
	if (counter)
		admin->cache.counter = counter;
}

static void sparx5_vcap_es0_cache_read(struct sparx5 *sparx5,
				       struct vcap_admin *admin,
				       enum vcap_selection sel,
				       u32 start,
				       u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	if (sel & VCAP_SEL_ENTRY) {
		for (idx = 0; idx < count; ++idx) {
			keystr[idx] =
				spx5_rd(sparx5, VCAP_ES0_VCAP_ENTRY_DAT(idx));
			mskstr[idx] =
				~spx5_rd(sparx5, VCAP_ES0_VCAP_MASK_DAT(idx));
		}
	}

	if (sel & VCAP_SEL_ACTION)
		for (idx = 0; idx < count; ++idx)
			actstr[idx] =
				spx5_rd(sparx5, VCAP_ES0_VCAP_ACTION_DAT(idx));

	if (sel & VCAP_SEL_COUNTER) {
		admin->cache.counter =
			spx5_rd(sparx5, VCAP_ES0_VCAP_CNT_DAT(0));
		admin->cache.sticky = admin->cache.counter;
		sparx5_es0_read_esdx_counter(sparx5, admin, start);
	}
}

static void sparx5_vcap_es2_cache_read(struct sparx5 *sparx5,
				       struct vcap_admin *admin,
				       enum vcap_selection sel,
				       u32 start,
				       u32 count)
{
	u32 *keystr, *mskstr, *actstr;
	int idx;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	if (sel & VCAP_SEL_ENTRY) {
		for (idx = 0; idx < count; ++idx) {
			keystr[idx] =
				spx5_rd(sparx5, VCAP_ES2_VCAP_ENTRY_DAT(idx));
			mskstr[idx] =
				~spx5_rd(sparx5, VCAP_ES2_VCAP_MASK_DAT(idx));
		}
	}

	if (sel & VCAP_SEL_ACTION)
		for (idx = 0; idx < count; ++idx)
			actstr[idx] =
				spx5_rd(sparx5, VCAP_ES2_VCAP_ACTION_DAT(idx));

	if (sel & VCAP_SEL_COUNTER) {
		start = start & 0x7ff; /* counter limit */
		admin->cache.counter =
			spx5_rd(sparx5, EACL_ES2_CNT(start));
		admin->cache.sticky =
			spx5_rd(sparx5, VCAP_ES2_VCAP_CNT_DAT(0));
	}
}

/* API callback used for reading from the VCAP into the VCAP cache */
static void sparx5_vcap_cache_read(struct net_device *ndev,
				   struct vcap_admin *admin,
				   enum vcap_selection sel,
				   u32 start,
				   u32 count)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		sparx5_vcap_is0_cache_read(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_IS2:
		sparx5_vcap_is2_cache_read(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_cache_read(sparx5, admin, sel, start, count);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_cache_read(sparx5, admin, sel, start, count);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

/* API callback used for initializing a VCAP address range */
static void sparx5_vcap_range_init(struct net_device *ndev,
				   struct vcap_admin *admin, u32 addr,
				   u32 count)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;

	_sparx5_vcap_range_init(sparx5, admin, addr, count);
}

static void sparx5_vcap_super_update(struct sparx5 *sparx5,
				     enum vcap_command cmd,
				     enum vcap_selection sel, u32 addr)
{
	bool clear = (cmd == VCAP_CMD_INITIALIZE);

	spx5_wr(VCAP_SUPER_CFG_MV_NUM_POS_SET(0) |
		VCAP_SUPER_CFG_MV_SIZE_SET(0), sparx5, VCAP_SUPER_CFG);
	spx5_wr(VCAP_SUPER_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_SUPER_CTRL_UPDATE_ENTRY_DIS_SET((VCAP_SEL_ENTRY & sel) == 0) |
		VCAP_SUPER_CTRL_UPDATE_ACTION_DIS_SET((VCAP_SEL_ACTION & sel) == 0) |
		VCAP_SUPER_CTRL_UPDATE_CNT_DIS_SET((VCAP_SEL_COUNTER & sel) == 0) |
		VCAP_SUPER_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_SUPER_CTRL_CLEAR_CACHE_SET(clear) |
		VCAP_SUPER_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_SUPER_CTRL);
	sparx5_vcap_wait_super_update(sparx5);
}

static void sparx5_vcap_es0_update(struct sparx5 *sparx5,
				   enum vcap_command cmd,
				   enum vcap_selection sel, u32 addr)
{
	bool clear = (cmd == VCAP_CMD_INITIALIZE);

	spx5_wr(VCAP_ES0_CFG_MV_NUM_POS_SET(0) |
		VCAP_ES0_CFG_MV_SIZE_SET(0), sparx5, VCAP_ES0_CFG);
	spx5_wr(VCAP_ES0_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_ES0_CTRL_UPDATE_ENTRY_DIS_SET((VCAP_SEL_ENTRY & sel) == 0) |
		VCAP_ES0_CTRL_UPDATE_ACTION_DIS_SET((VCAP_SEL_ACTION & sel) == 0) |
		VCAP_ES0_CTRL_UPDATE_CNT_DIS_SET((VCAP_SEL_COUNTER & sel) == 0) |
		VCAP_ES0_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_ES0_CTRL_CLEAR_CACHE_SET(clear) |
		VCAP_ES0_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_ES0_CTRL);
	sparx5_vcap_wait_es0_update(sparx5);
}

static void sparx5_vcap_es2_update(struct sparx5 *sparx5,
				   enum vcap_command cmd,
				   enum vcap_selection sel, u32 addr)
{
	bool clear = (cmd == VCAP_CMD_INITIALIZE);

	spx5_wr(VCAP_ES2_CFG_MV_NUM_POS_SET(0) |
		VCAP_ES2_CFG_MV_SIZE_SET(0), sparx5, VCAP_ES2_CFG);
	spx5_wr(VCAP_ES2_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_ES2_CTRL_UPDATE_ENTRY_DIS_SET((VCAP_SEL_ENTRY & sel) == 0) |
		VCAP_ES2_CTRL_UPDATE_ACTION_DIS_SET((VCAP_SEL_ACTION & sel) == 0) |
		VCAP_ES2_CTRL_UPDATE_CNT_DIS_SET((VCAP_SEL_COUNTER & sel) == 0) |
		VCAP_ES2_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_ES2_CTRL_CLEAR_CACHE_SET(clear) |
		VCAP_ES2_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_ES2_CTRL);
	sparx5_vcap_wait_es2_update(sparx5);
}

/* API callback used for updating the VCAP cache */
static void sparx5_vcap_update(struct net_device *ndev,
			       struct vcap_admin *admin, enum vcap_command cmd,
			       enum vcap_selection sel, u32 addr)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
	case VCAP_TYPE_IS2:
		sparx5_vcap_super_update(sparx5, cmd, sel, addr);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_update(sparx5, cmd, sel, addr);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_update(sparx5, cmd, sel, addr);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

static void sparx5_vcap_super_move(struct sparx5 *sparx5,
				   u32 addr,
				   enum vcap_command cmd,
				   u16 mv_num_pos,
				   u16 mv_size)
{
	spx5_wr(VCAP_SUPER_CFG_MV_NUM_POS_SET(mv_num_pos) |
		VCAP_SUPER_CFG_MV_SIZE_SET(mv_size),
		sparx5, VCAP_SUPER_CFG);
	spx5_wr(VCAP_SUPER_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_SUPER_CTRL_UPDATE_ENTRY_DIS_SET(0) |
		VCAP_SUPER_CTRL_UPDATE_ACTION_DIS_SET(0) |
		VCAP_SUPER_CTRL_UPDATE_CNT_DIS_SET(0) |
		VCAP_SUPER_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_SUPER_CTRL_CLEAR_CACHE_SET(false) |
		VCAP_SUPER_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_SUPER_CTRL);
	sparx5_vcap_wait_super_update(sparx5);
}

static void sparx5_vcap_es0_move(struct sparx5 *sparx5,
				 u32 addr,
				 enum vcap_command cmd,
				 u16 mv_num_pos,
				 u16 mv_size)
{
	spx5_wr(VCAP_ES0_CFG_MV_NUM_POS_SET(mv_num_pos) |
		VCAP_ES0_CFG_MV_SIZE_SET(mv_size),
		sparx5, VCAP_ES0_CFG);
	spx5_wr(VCAP_ES0_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_ES0_CTRL_UPDATE_ENTRY_DIS_SET(0) |
		VCAP_ES0_CTRL_UPDATE_ACTION_DIS_SET(0) |
		VCAP_ES0_CTRL_UPDATE_CNT_DIS_SET(0) |
		VCAP_ES0_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_ES0_CTRL_CLEAR_CACHE_SET(false) |
		VCAP_ES0_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_ES0_CTRL);
	sparx5_vcap_wait_es0_update(sparx5);
}

static void sparx5_vcap_es2_move(struct sparx5 *sparx5,
				 u32 addr,
				 enum vcap_command cmd,
				 u16 mv_num_pos,
				 u16 mv_size)
{
	spx5_wr(VCAP_ES2_CFG_MV_NUM_POS_SET(mv_num_pos) |
		VCAP_ES2_CFG_MV_SIZE_SET(mv_size),
		sparx5, VCAP_ES2_CFG);
	spx5_wr(VCAP_ES2_CTRL_UPDATE_CMD_SET(cmd) |
		VCAP_ES2_CTRL_UPDATE_ENTRY_DIS_SET(0) |
		VCAP_ES2_CTRL_UPDATE_ACTION_DIS_SET(0) |
		VCAP_ES2_CTRL_UPDATE_CNT_DIS_SET(0) |
		VCAP_ES2_CTRL_UPDATE_ADDR_SET(addr) |
		VCAP_ES2_CTRL_CLEAR_CACHE_SET(false) |
		VCAP_ES2_CTRL_UPDATE_SHOT_SET(true),
		sparx5, VCAP_ES2_CTRL);
	sparx5_vcap_wait_es2_update(sparx5);
}

/* API callback used for moving a block of rules in the VCAP */
static void sparx5_vcap_move(struct net_device *ndev, struct vcap_admin *admin,
			     u32 addr, int offset, int count)
{
	struct sparx5_port *port = netdev_priv(ndev);
	struct sparx5 *sparx5 = port->sparx5;
	enum vcap_command cmd;
	u16 mv_num_pos;
	u16 mv_size;

	mv_size = count - 1;
	if (offset > 0) {
		mv_num_pos = offset - 1;
		cmd = VCAP_CMD_MOVE_DOWN;
	} else {
		mv_num_pos = -offset - 1;
		cmd = VCAP_CMD_MOVE_UP;
	}

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
	case VCAP_TYPE_IS2:
		sparx5_vcap_super_move(sparx5, addr, cmd, mv_num_pos, mv_size);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_move(sparx5, addr, cmd, mv_num_pos, mv_size);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_move(sparx5, addr, cmd, mv_num_pos, mv_size);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

static struct vcap_operations sparx5_vcap_ops = {
	.validate_keyset = sparx5_vcap_validate_keyset,
	.add_default_fields = sparx5_vcap_add_default_fields,
	.cache_erase = sparx5_vcap_cache_erase,
	.cache_write = sparx5_vcap_cache_write,
	.cache_read = sparx5_vcap_cache_read,
	.init = sparx5_vcap_range_init,
	.update = sparx5_vcap_update,
	.move = sparx5_vcap_move,
	.port_info = sparx5_port_info,
};

/* Enable IS0 lookups per port and set the keyset generation */
static void sparx5_vcap_is0_port_key_selection(struct sparx5 *sparx5,
					       struct vcap_admin *admin)
{
	int portno, lookup;
	u32 keysel;

	keysel = VCAP_IS0_KEYSEL(false,
				 VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE,
				 VCAP_IS0_PS_ETYPE_NORMAL_5TUPLE_IP4,
				 VCAP_IS0_PS_ETYPE_NORMAL_7TUPLE,
				 VCAP_IS0_PS_MPLS_FOLLOW_ETYPE,
				 VCAP_IS0_PS_MPLS_FOLLOW_ETYPE,
				 VCAP_IS0_PS_MLBS_FOLLOW_ETYPE);
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		for (portno = 0; portno < SPX5_PORTS; ++portno) {
			spx5_wr(keysel, sparx5,
				ANA_CL_ADV_CL_CFG(portno, lookup));
			spx5_rmw(ANA_CL_ADV_CL_CFG_LOOKUP_ENA,
				 ANA_CL_ADV_CL_CFG_LOOKUP_ENA,
				 sparx5,
				 ANA_CL_ADV_CL_CFG(portno, lookup));
		}
	}
}

/* Enable IS2 lookups per port and set the keyset generation */
static void sparx5_vcap_is2_port_key_selection(struct sparx5 *sparx5,
					       struct vcap_admin *admin)
{
	int portno, lookup;
	u32 keysel;

	keysel = VCAP_IS2_KEYSEL(true, VCAP_IS2_PS_NONETH_MAC_ETYPE,
				 VCAP_IS2_PS_IPV4_MC_IP4_TCP_UDP_OTHER,
				 VCAP_IS2_PS_IPV4_UC_IP4_TCP_UDP_OTHER,
				 VCAP_IS2_PS_IPV6_MC_IP_7TUPLE,
				 VCAP_IS2_PS_IPV6_UC_IP_7TUPLE,
				 VCAP_IS2_PS_ARP_ARP);
	for (lookup = 0; lookup < admin->lookups; ++lookup) {
		for (portno = 0; portno < SPX5_PORTS; ++portno) {
			spx5_wr(keysel, sparx5,
				ANA_ACL_VCAP_S2_KEY_SEL(portno, lookup));
		}
	}
	/* IS2 lookups are in bit 0:3 */
	for (portno = 0; portno < SPX5_PORTS; ++portno)
		spx5_rmw(ANA_ACL_VCAP_S2_CFG_SEC_ENA_SET(0xf),
			 ANA_ACL_VCAP_S2_CFG_SEC_ENA,
			 sparx5,
			 ANA_ACL_VCAP_S2_CFG(portno));
}

/* Enable ES0 lookups per port and set the keyset generation */
static void sparx5_vcap_es0_port_key_selection(struct sparx5 *sparx5,
					       struct vcap_admin *admin)
{
	int portno;
	u32 keysel;

	keysel = VCAP_ES0_KEYSEL(VCAP_ES0_PS_FORCE_ISDX_LOOKUPS);
	for (portno = 0; portno < SPX5_PORTS; ++portno)
		spx5_rmw(keysel, REW_RTAG_ETAG_CTRL_ES0_ISDX_KEY_ENA,
			 sparx5, REW_RTAG_ETAG_CTRL(portno));

	spx5_rmw(REW_ES0_CTRL_ES0_LU_ENA_SET(1), REW_ES0_CTRL_ES0_LU_ENA,
		 sparx5, REW_ES0_CTRL);
}

/* Enable ES2 lookups per port and set the keyset generation */
static void sparx5_vcap_es2_port_key_selection(struct sparx5 *sparx5,
					       struct vcap_admin *admin)
{
	int portno, lookup;
	u32 keysel;

	keysel = VCAP_ES2_KEYSEL(true, VCAP_ES2_PS_ARP_MAC_ETYPE,
				 VCAP_ES2_PS_IPV4_IP4_TCP_UDP_OTHER,
				 VCAP_ES2_PS_IPV6_IP_7TUPLE);
	for (lookup = 0; lookup < admin->lookups; ++lookup)
		for (portno = 0; portno < SPX5_PORTS; ++portno)
			spx5_wr(keysel, sparx5,
				EACL_VCAP_ES2_KEY_SEL(portno, lookup));
}

/* Enable lookups per port and set the keyset generation */
static void sparx5_vcap_port_key_selection(struct sparx5 *sparx5,
					   struct vcap_admin *admin)
{
	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		sparx5_vcap_is0_port_key_selection(sparx5, admin);
		break;
	case VCAP_TYPE_IS2:
		sparx5_vcap_is2_port_key_selection(sparx5, admin);
		break;
	case VCAP_TYPE_ES0:
		sparx5_vcap_es0_port_key_selection(sparx5, admin);
		break;
	case VCAP_TYPE_ES2:
		sparx5_vcap_es2_port_key_selection(sparx5, admin);
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

/* Disable lookups per port */
static void sparx5_vcap_port_key_deselection(struct sparx5 *sparx5,
					     struct vcap_admin *admin)
{
	int portno, lookup;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
		for (lookup = 0; lookup < admin->lookups; ++lookup)
			for (portno = 0; portno < SPX5_PORTS; ++portno)
				spx5_rmw(ANA_CL_ADV_CL_CFG_LOOKUP_ENA_SET(0),
					 ANA_CL_ADV_CL_CFG_LOOKUP_ENA,
					 sparx5,
					 ANA_CL_ADV_CL_CFG(portno, lookup));
		break;
	case VCAP_TYPE_IS2:
		for (portno = 0; portno < SPX5_PORTS; ++portno)
			spx5_rmw(ANA_ACL_VCAP_S2_CFG_SEC_ENA_SET(0),
				 ANA_ACL_VCAP_S2_CFG_SEC_ENA,
				 sparx5,
				 ANA_ACL_VCAP_S2_CFG(portno));
		break;
	case VCAP_TYPE_ES0:
		spx5_rmw(REW_ES0_CTRL_ES0_LU_ENA_SET(0),
			 REW_ES0_CTRL_ES0_LU_ENA, sparx5, REW_ES0_CTRL);
		break;
	case VCAP_TYPE_ES2:
		for (lookup = 0; lookup < admin->lookups; ++lookup)
			for (portno = 0; portno < SPX5_PORTS; ++portno)
				spx5_rmw(EACL_VCAP_ES2_KEY_SEL_KEY_ENA_SET(0),
					 EACL_VCAP_ES2_KEY_SEL_KEY_ENA,
					 sparx5,
					 EACL_VCAP_ES2_KEY_SEL(portno, lookup));
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

static void sparx5_vcap_admin_free(struct vcap_admin *admin)
{
	if (!admin)
		return;
	mutex_destroy(&admin->lock);
	kfree(admin->cache.keystream);
	kfree(admin->cache.maskstream);
	kfree(admin->cache.actionstream);
	kfree(admin);
}

/* Allocate a vcap instance with a rule list and a cache area */
static struct vcap_admin *
sparx5_vcap_admin_alloc(struct sparx5 *sparx5, struct vcap_control *ctrl,
			const struct sparx5_vcap_inst *cfg)
{
	struct vcap_admin *admin;

	admin = kzalloc(sizeof(*admin), GFP_KERNEL);
	if (!admin)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&admin->list);
	INIT_LIST_HEAD(&admin->rules);
	INIT_LIST_HEAD(&admin->enabled);
	mutex_init(&admin->lock);
	admin->vtype = cfg->vtype;
	admin->vinst = cfg->vinst;
	admin->ingress = cfg->ingress;
	admin->lookups = cfg->lookups;
	admin->lookups_per_instance = cfg->lookups_per_instance;
	admin->first_cid = cfg->first_cid;
	admin->last_cid = cfg->last_cid;
	admin->cache.keystream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.maskstream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.actionstream =
		kzalloc(STREAMSIZE, GFP_KERNEL);
	if (!admin->cache.keystream || !admin->cache.maskstream ||
	    !admin->cache.actionstream) {
		sparx5_vcap_admin_free(admin);
		return ERR_PTR(-ENOMEM);
	}
	return admin;
}

/* Do block allocations and provide addresses for VCAP instances */
static void sparx5_vcap_block_alloc(struct sparx5 *sparx5,
				    struct vcap_admin *admin,
				    const struct sparx5_vcap_inst *cfg)
{
	int idx, cores;

	switch (admin->vtype) {
	case VCAP_TYPE_IS0:
	case VCAP_TYPE_IS2:
		/* Super VCAP block mapping and address configuration. Block 0
		 * is assigned addresses 0 through 3071, block 1 is assigned
		 * addresses 3072 though 6143, and so on.
		 */
		for (idx = cfg->blockno; idx < cfg->blockno + cfg->blocks;
		     ++idx) {
			spx5_wr(VCAP_SUPER_IDX_CORE_IDX_SET(idx), sparx5,
				VCAP_SUPER_IDX);
			spx5_wr(VCAP_SUPER_MAP_CORE_MAP_SET(cfg->map_id),
				sparx5, VCAP_SUPER_MAP);
		}
		admin->first_valid_addr = cfg->blockno * SUPER_VCAP_BLK_SIZE;
		admin->last_used_addr = admin->first_valid_addr +
			cfg->blocks * SUPER_VCAP_BLK_SIZE;
		admin->last_valid_addr = admin->last_used_addr - 1;
		break;
	case VCAP_TYPE_ES0:
		admin->first_valid_addr = 0;
		admin->last_used_addr = cfg->count;
		admin->last_valid_addr = cfg->count - 1;
		cores = spx5_rd(sparx5, VCAP_ES0_CORE_CNT);
		for (idx = 0; idx < cores; ++idx) {
			spx5_wr(VCAP_ES0_IDX_CORE_IDX_SET(idx), sparx5,
				VCAP_ES0_IDX);
			spx5_wr(VCAP_ES0_MAP_CORE_MAP_SET(1), sparx5,
				VCAP_ES0_MAP);
		}
		break;
	case VCAP_TYPE_ES2:
		admin->first_valid_addr = 0;
		admin->last_used_addr = cfg->count;
		admin->last_valid_addr = cfg->count - 1;
		cores = spx5_rd(sparx5, VCAP_ES2_CORE_CNT);
		for (idx = 0; idx < cores; ++idx) {
			spx5_wr(VCAP_ES2_IDX_CORE_IDX_SET(idx), sparx5,
				VCAP_ES2_IDX);
			spx5_wr(VCAP_ES2_MAP_CORE_MAP_SET(1), sparx5,
				VCAP_ES2_MAP);
		}
		break;
	default:
		sparx5_vcap_type_err(sparx5, admin, __func__);
		break;
	}
}

/* Allocate a vcap control and vcap instances and configure the system */
int sparx5_vcap_init(struct sparx5 *sparx5)
{
	const struct sparx5_vcap_inst *cfg;
	struct vcap_control *ctrl;
	struct vcap_admin *admin;
	struct dentry *dir;
	int err = 0, idx;

	/* Create a VCAP control instance that owns the platform specific VCAP
	 * model with VCAP instances and information about keysets, keys,
	 * actionsets and actions
	 * - Create administrative state for each available VCAP
	 *   - Lists of rules
	 *   - Address information
	 *   - Initialize VCAP blocks
	 *   - Configure port keysets
	 */
	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	sparx5->vcap_ctrl = ctrl;
	/* select the sparx5 VCAP model */
	ctrl->vcaps = sparx5_vcaps;
	ctrl->stats = &sparx5_vcap_stats;
	/* Setup callbacks to allow the API to use the VCAP HW */
	ctrl->ops = &sparx5_vcap_ops;

	INIT_LIST_HEAD(&ctrl->list);
	for (idx = 0; idx < ARRAY_SIZE(sparx5_vcap_inst_cfg); ++idx) {
		cfg = &sparx5_vcap_inst_cfg[idx];
		admin = sparx5_vcap_admin_alloc(sparx5, ctrl, cfg);
		if (IS_ERR(admin)) {
			err = PTR_ERR(admin);
			pr_err("%s:%d: vcap allocation failed: %d\n",
			       __func__, __LINE__, err);
			return err;
		}
		sparx5_vcap_block_alloc(sparx5, admin, cfg);
		sparx5_vcap_block_init(sparx5, admin);
		if (cfg->vinst == 0)
			sparx5_vcap_port_key_selection(sparx5, admin);
		list_add_tail(&admin->list, &ctrl->list);
	}
	dir = vcap_debugfs(sparx5->dev, sparx5->debugfs_root, ctrl);
	for (idx = 0; idx < SPX5_PORTS; ++idx)
		if (sparx5->ports[idx])
			vcap_port_debugfs(sparx5->dev, dir, ctrl,
					  sparx5->ports[idx]->ndev);

	return err;
}

void sparx5_vcap_destroy(struct sparx5 *sparx5)
{
	struct vcap_control *ctrl = sparx5->vcap_ctrl;
	struct vcap_admin *admin, *admin_next;

	if (!ctrl)
		return;

	list_for_each_entry_safe(admin, admin_next, &ctrl->list, list) {
		sparx5_vcap_port_key_deselection(sparx5, admin);
		vcap_del_rules(ctrl, admin);
		list_del(&admin->list);
		sparx5_vcap_admin_free(admin);
	}
	kfree(ctrl);
}
