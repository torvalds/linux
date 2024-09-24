// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"
#include "lan966x_vcap_ag_api.h"
#include "vcap_api.h"
#include "vcap_api_client.h"
#include "vcap_api_debugfs.h"

#define STREAMSIZE (64 * 4)

#define LAN966X_IS1_LOOKUPS 3
#define LAN966X_IS2_LOOKUPS 2
#define LAN966X_ES0_LOOKUPS 1

#define LAN966X_STAT_ESDX_GRN_BYTES 0x300
#define LAN966X_STAT_ESDX_GRN_PKTS 0x301
#define LAN966X_STAT_ESDX_YEL_BYTES 0x302
#define LAN966X_STAT_ESDX_YEL_PKTS 0x303

static struct lan966x_vcap_inst {
	enum vcap_type vtype; /* type of vcap */
	int tgt_inst; /* hardware instance number */
	int lookups; /* number of lookups in this vcap type */
	int first_cid; /* first chain id in this vcap */
	int last_cid; /* last chain id in this vcap */
	int count; /* number of available addresses */
	bool ingress; /* is vcap in the ingress path */
} lan966x_vcap_inst_cfg[] = {
	{
		.vtype = VCAP_TYPE_ES0,
		.tgt_inst = 0,
		.lookups = LAN966X_ES0_LOOKUPS,
		.first_cid = LAN966X_VCAP_CID_ES0_L0,
		.last_cid = LAN966X_VCAP_CID_ES0_MAX,
		.count = 64,
	},
	{
		.vtype = VCAP_TYPE_IS1, /* IS1-0 */
		.tgt_inst = 1,
		.lookups = LAN966X_IS1_LOOKUPS,
		.first_cid = LAN966X_VCAP_CID_IS1_L0,
		.last_cid = LAN966X_VCAP_CID_IS1_MAX,
		.count = 768,
		.ingress = true,
	},
	{
		.vtype = VCAP_TYPE_IS2, /* IS2-0 */
		.tgt_inst = 2,
		.lookups = LAN966X_IS2_LOOKUPS,
		.first_cid = LAN966X_VCAP_CID_IS2_L0,
		.last_cid = LAN966X_VCAP_CID_IS2_MAX,
		.count = 256,
		.ingress = true,
	},
};

struct lan966x_vcap_cmd_cb {
	struct lan966x *lan966x;
	u32 instance;
};

static u32 lan966x_vcap_read_update_ctrl(const struct lan966x_vcap_cmd_cb *cb)
{
	return lan_rd(cb->lan966x, VCAP_UPDATE_CTRL(cb->instance));
}

static void lan966x_vcap_wait_update(struct lan966x *lan966x, int instance)
{
	const struct lan966x_vcap_cmd_cb cb = { .lan966x = lan966x,
						.instance = instance };
	u32 val;

	readx_poll_timeout(lan966x_vcap_read_update_ctrl, &cb, val,
			   (val & VCAP_UPDATE_CTRL_UPDATE_SHOT) == 0, 10,
			   100000);
}

static void __lan966x_vcap_range_init(struct lan966x *lan966x,
				      struct vcap_admin *admin,
				      u32 addr,
				      u32 count)
{
	lan_wr(VCAP_MV_CFG_MV_NUM_POS_SET(0) |
	       VCAP_MV_CFG_MV_SIZE_SET(count - 1),
	       lan966x, VCAP_MV_CFG(admin->tgt_inst));

	lan_wr(VCAP_UPDATE_CTRL_UPDATE_CMD_SET(VCAP_CMD_INITIALIZE) |
	       VCAP_UPDATE_CTRL_UPDATE_ENTRY_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ACTION_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_CNT_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ADDR_SET(addr) |
	       VCAP_UPDATE_CTRL_CLEAR_CACHE_SET(true) |
	       VCAP_UPDATE_CTRL_UPDATE_SHOT_SET(1),
	       lan966x, VCAP_UPDATE_CTRL(admin->tgt_inst));

	lan966x_vcap_wait_update(lan966x, admin->tgt_inst);
}

static int lan966x_vcap_is1_cid_to_lookup(int cid)
{
	int lookup = 0;

	if (cid >= LAN966X_VCAP_CID_IS1_L1 &&
	    cid < LAN966X_VCAP_CID_IS1_L2)
		lookup = 1;
	else if (cid >= LAN966X_VCAP_CID_IS1_L2 &&
		 cid < LAN966X_VCAP_CID_IS1_MAX)
		lookup = 2;

	return lookup;
}

static int lan966x_vcap_is2_cid_to_lookup(int cid)
{
	if (cid >= LAN966X_VCAP_CID_IS2_L1 &&
	    cid < LAN966X_VCAP_CID_IS2_MAX)
		return 1;

	return 0;
}

/* Return the list of keysets for the vcap port configuration */
static int
lan966x_vcap_is1_get_port_keysets(struct net_device *ndev, int lookup,
				  struct vcap_keyset_list *keysetlist,
				  u16 l3_proto)
{
	struct lan966x_port *port = netdev_priv(ndev);
	struct lan966x *lan966x = port->lan966x;
	u32 val;

	val = lan_rd(lan966x, ANA_VCAP_S1_CFG(port->chip_port, lookup));

	/* Collect all keysets for the port in a list */
	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IP) {
		switch (ANA_VCAP_S1_CFG_KEY_IP4_CFG_GET(val)) {
		case VCAP_IS1_PS_IPV4_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_7TUPLE);
			break;
		case VCAP_IS1_PS_IPV4_5TUPLE_IP4:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_5TUPLE_IP4);
			break;
		case VCAP_IS1_PS_IPV4_NORMAL:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL);
			break;
		}
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IPV6) {
		switch (ANA_VCAP_S1_CFG_KEY_IP6_CFG_GET(val)) {
		case VCAP_IS1_PS_IPV6_NORMAL:
		case VCAP_IS1_PS_IPV6_NORMAL_IP6:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL_IP6);
			break;
		case VCAP_IS1_PS_IPV6_5TUPLE_IP6:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_5TUPLE_IP6);
			break;
		case VCAP_IS1_PS_IPV6_7TUPLE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_7TUPLE);
			break;
		case VCAP_IS1_PS_IPV6_5TUPLE_IP4:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_5TUPLE_IP4);
			break;
		case VCAP_IS1_PS_IPV6_DMAC_VID:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_DMAC_VID);
			break;
		}
	}

	switch (ANA_VCAP_S1_CFG_KEY_OTHER_CFG_GET(val)) {
	case VCAP_IS1_PS_OTHER_7TUPLE:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_7TUPLE);
		break;
	case VCAP_IS1_PS_OTHER_NORMAL:
		vcap_keyset_list_add(keysetlist, VCAP_KFS_NORMAL);
		break;
	}

	return 0;
}

static int
lan966x_vcap_is2_get_port_keysets(struct net_device *dev, int lookup,
				  struct vcap_keyset_list *keysetlist,
				  u16 l3_proto)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	bool found = false;
	u32 val;

	val = lan_rd(lan966x, ANA_VCAP_S2_CFG(port->chip_port));

	/* Collect all keysets for the port in a list */
	if (l3_proto == ETH_P_ALL)
		vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_SNAP) {
		if (ANA_VCAP_S2_CFG_SNAP_DIS_GET(val) & (BIT(0) << lookup))
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_LLC);
		else
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_SNAP);

		found = true;
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_CFM) {
		if (ANA_VCAP_S2_CFG_OAM_DIS_GET(val) & (BIT(0) << lookup))
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
		else
			vcap_keyset_list_add(keysetlist, VCAP_KFS_OAM);

		found = true;
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_ARP) {
		if (ANA_VCAP_S2_CFG_ARP_DIS_GET(val) & (BIT(0) << lookup))
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
		else
			vcap_keyset_list_add(keysetlist, VCAP_KFS_ARP);

		found = true;
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IP) {
		if (ANA_VCAP_S2_CFG_IP_OTHER_DIS_GET(val) & (BIT(0) << lookup))
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
		else
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);

		if (ANA_VCAP_S2_CFG_IP_TCPUDP_DIS_GET(val) & (BIT(0) << lookup))
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
		else
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);

		found = true;
	}

	if (l3_proto == ETH_P_ALL || l3_proto == ETH_P_IPV6) {
		switch (ANA_VCAP_S2_CFG_IP6_CFG_GET(val) & (0x3 << lookup)) {
		case VCAP_IS2_PS_IPV6_TCPUDP_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_OTHER);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_TCP_UDP);
			break;
		case VCAP_IS2_PS_IPV6_STD:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP6_STD);
			break;
		case VCAP_IS2_PS_IPV6_IP4_TCPUDP_IP4_OTHER:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_OTHER);
			vcap_keyset_list_add(keysetlist, VCAP_KFS_IP4_TCP_UDP);
			break;
		case VCAP_IS2_PS_IPV6_MAC_ETYPE:
			vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);
			break;
		}

		found = true;
	}

	if (!found)
		vcap_keyset_list_add(keysetlist, VCAP_KFS_MAC_ETYPE);

	return 0;
}

static enum vcap_keyfield_set
lan966x_vcap_validate_keyset(struct net_device *dev,
			     struct vcap_admin *admin,
			     struct vcap_rule *rule,
			     struct vcap_keyset_list *kslist,
			     u16 l3_proto)
{
	struct vcap_keyset_list keysetlist = {};
	enum vcap_keyfield_set keysets[10] = {};
	int lookup;
	int err;

	if (!kslist || kslist->cnt == 0)
		return VCAP_KFS_NO_VALUE;

	keysetlist.max = ARRAY_SIZE(keysets);
	keysetlist.keysets = keysets;

	switch (admin->vtype) {
	case VCAP_TYPE_IS1:
		lookup = lan966x_vcap_is1_cid_to_lookup(rule->vcap_chain_id);
		err = lan966x_vcap_is1_get_port_keysets(dev, lookup, &keysetlist,
							l3_proto);
		break;
	case VCAP_TYPE_IS2:
		lookup = lan966x_vcap_is2_cid_to_lookup(rule->vcap_chain_id);
		err = lan966x_vcap_is2_get_port_keysets(dev, lookup, &keysetlist,
							l3_proto);
		break;
	case VCAP_TYPE_ES0:
		return kslist->keysets[0];
	default:
		pr_err("vcap type: %s not supported\n",
		       lan966x_vcaps[admin->vtype].name);
		return VCAP_KFS_NO_VALUE;
	}

	if (err)
		return VCAP_KFS_NO_VALUE;

	/* Check if there is a match and return the match */
	for (int i = 0; i < kslist->cnt; ++i)
		for (int j = 0; j < keysetlist.cnt; ++j)
			if (kslist->keysets[i] == keysets[j])
				return kslist->keysets[i];

	return VCAP_KFS_NO_VALUE;
}

static bool lan966x_vcap_is2_is_first_chain(struct vcap_rule *rule)
{
	return (rule->vcap_chain_id >= LAN966X_VCAP_CID_IS2_L0 &&
		rule->vcap_chain_id < LAN966X_VCAP_CID_IS2_L1);
}

static void lan966x_vcap_is1_add_default_fields(struct lan966x_port *port,
						struct vcap_admin *admin,
						struct vcap_rule *rule)
{
	u32 value, mask;
	u32 lookup;

	if (vcap_rule_get_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK,
				  &value, &mask))
		vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK, 0,
				      ~BIT(port->chip_port));

	lookup = lan966x_vcap_is1_cid_to_lookup(rule->vcap_chain_id);
	vcap_rule_add_key_u32(rule, VCAP_KF_LOOKUP_INDEX, lookup, 0x3);
}

static void lan966x_vcap_is2_add_default_fields(struct lan966x_port *port,
						struct vcap_admin *admin,
						struct vcap_rule *rule)
{
	u32 value, mask;

	if (vcap_rule_get_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK,
				  &value, &mask))
		vcap_rule_add_key_u32(rule, VCAP_KF_IF_IGR_PORT_MASK, 0,
				      ~BIT(port->chip_port));

	if (lan966x_vcap_is2_is_first_chain(rule))
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_1);
	else
		vcap_rule_add_key_bit(rule, VCAP_KF_LOOKUP_FIRST_IS,
				      VCAP_BIT_0);
}

static void lan966x_vcap_es0_add_default_fields(struct lan966x_port *port,
						struct vcap_admin *admin,
						struct vcap_rule *rule)
{
	vcap_rule_add_key_u32(rule, VCAP_KF_IF_EGR_PORT_NO,
			      port->chip_port, GENMASK(4, 0));
}

static void lan966x_vcap_add_default_fields(struct net_device *dev,
					    struct vcap_admin *admin,
					    struct vcap_rule *rule)
{
	struct lan966x_port *port = netdev_priv(dev);

	switch (admin->vtype) {
	case VCAP_TYPE_IS1:
		lan966x_vcap_is1_add_default_fields(port, admin, rule);
		break;
	case VCAP_TYPE_IS2:
		lan966x_vcap_is2_add_default_fields(port, admin, rule);
		break;
	case VCAP_TYPE_ES0:
		lan966x_vcap_es0_add_default_fields(port, admin, rule);
		break;
	default:
		pr_err("vcap type: %s not supported\n",
		       lan966x_vcaps[admin->vtype].name);
		break;
	}
}

static void lan966x_vcap_cache_erase(struct vcap_admin *admin)
{
	memset(admin->cache.keystream, 0, STREAMSIZE);
	memset(admin->cache.maskstream, 0, STREAMSIZE);
	memset(admin->cache.actionstream, 0, STREAMSIZE);
	memset(&admin->cache.counter, 0, sizeof(admin->cache.counter));
}

/* The ESDX counter is only used/incremented if the frame has been classified
 * with an ISDX > 0 (e.g by a rule in IS0).  This is not mentioned in the
 * datasheet.
 */
static void lan966x_es0_read_esdx_counter(struct lan966x *lan966x,
					  struct vcap_admin *admin, u32 id)
{
	u32 counter;

	id = id & 0xff; /* counter limit */
	mutex_lock(&lan966x->stats_lock);
	lan_wr(SYS_STAT_CFG_STAT_VIEW_SET(id), lan966x, SYS_STAT_CFG);
	counter = lan_rd(lan966x, SYS_CNT(LAN966X_STAT_ESDX_GRN_PKTS)) +
		  lan_rd(lan966x, SYS_CNT(LAN966X_STAT_ESDX_YEL_PKTS));
	mutex_unlock(&lan966x->stats_lock);
	if (counter)
		admin->cache.counter = counter;
}

static void lan966x_es0_write_esdx_counter(struct lan966x *lan966x,
					   struct vcap_admin *admin, u32 id)
{
	id = id & 0xff; /* counter limit */

	mutex_lock(&lan966x->stats_lock);
	lan_wr(SYS_STAT_CFG_STAT_VIEW_SET(id), lan966x, SYS_STAT_CFG);
	lan_wr(0, lan966x, SYS_CNT(LAN966X_STAT_ESDX_GRN_BYTES));
	lan_wr(admin->cache.counter, lan966x,
	       SYS_CNT(LAN966X_STAT_ESDX_GRN_PKTS));
	lan_wr(0, lan966x, SYS_CNT(LAN966X_STAT_ESDX_YEL_BYTES));
	lan_wr(0, lan966x, SYS_CNT(LAN966X_STAT_ESDX_YEL_PKTS));
	mutex_unlock(&lan966x->stats_lock);
}

static void lan966x_vcap_cache_write(struct net_device *dev,
				     struct vcap_admin *admin,
				     enum vcap_selection sel,
				     u32 start,
				     u32 count)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	u32 *keystr, *mskstr, *actstr;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	switch (sel) {
	case VCAP_SEL_ENTRY:
		for (int i = 0; i < count; ++i) {
			lan_wr(keystr[i] & mskstr[i], lan966x,
			       VCAP_ENTRY_DAT(admin->tgt_inst, i));
			lan_wr(~mskstr[i], lan966x,
			       VCAP_MASK_DAT(admin->tgt_inst, i));
		}
		break;
	case VCAP_SEL_ACTION:
		for (int i = 0; i < count; ++i)
			lan_wr(actstr[i], lan966x,
			       VCAP_ACTION_DAT(admin->tgt_inst, i));
		break;
	case VCAP_SEL_COUNTER:
		admin->cache.sticky = admin->cache.counter > 0;
		lan_wr(admin->cache.counter, lan966x,
		       VCAP_CNT_DAT(admin->tgt_inst, 0));

		if (admin->vtype == VCAP_TYPE_ES0)
			lan966x_es0_write_esdx_counter(lan966x, admin, start);
		break;
	default:
		break;
	}
}

static void lan966x_vcap_cache_read(struct net_device *dev,
				    struct vcap_admin *admin,
				    enum vcap_selection sel,
				    u32 start,
				    u32 count)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	int instance = admin->tgt_inst;
	u32 *keystr, *mskstr, *actstr;

	keystr = &admin->cache.keystream[start];
	mskstr = &admin->cache.maskstream[start];
	actstr = &admin->cache.actionstream[start];

	if (sel & VCAP_SEL_ENTRY) {
		for (int i = 0; i < count; ++i) {
			keystr[i] =
				lan_rd(lan966x, VCAP_ENTRY_DAT(instance, i));
			mskstr[i] =
				~lan_rd(lan966x, VCAP_MASK_DAT(instance, i));
		}
	}

	if (sel & VCAP_SEL_ACTION)
		for (int i = 0; i < count; ++i)
			actstr[i] =
				lan_rd(lan966x, VCAP_ACTION_DAT(instance, i));

	if (sel & VCAP_SEL_COUNTER) {
		admin->cache.counter =
			lan_rd(lan966x, VCAP_CNT_DAT(instance, 0));
		admin->cache.sticky = admin->cache.counter > 0;

		if (admin->vtype == VCAP_TYPE_ES0)
			lan966x_es0_read_esdx_counter(lan966x, admin, start);
	}
}

static void lan966x_vcap_range_init(struct net_device *dev,
				    struct vcap_admin *admin,
				    u32 addr,
				    u32 count)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;

	__lan966x_vcap_range_init(lan966x, admin, addr, count);
}

static void lan966x_vcap_update(struct net_device *dev,
				struct vcap_admin *admin,
				enum vcap_command cmd,
				enum vcap_selection sel,
				u32 addr)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
	bool clear;

	clear = (cmd == VCAP_CMD_INITIALIZE);

	lan_wr(VCAP_MV_CFG_MV_NUM_POS_SET(0) |
	       VCAP_MV_CFG_MV_SIZE_SET(0),
	       lan966x, VCAP_MV_CFG(admin->tgt_inst));

	lan_wr(VCAP_UPDATE_CTRL_UPDATE_CMD_SET(cmd) |
	       VCAP_UPDATE_CTRL_UPDATE_ENTRY_DIS_SET((VCAP_SEL_ENTRY & sel) == 0) |
	       VCAP_UPDATE_CTRL_UPDATE_ACTION_DIS_SET((VCAP_SEL_ACTION & sel) == 0) |
	       VCAP_UPDATE_CTRL_UPDATE_CNT_DIS_SET((VCAP_SEL_COUNTER & sel) == 0) |
	       VCAP_UPDATE_CTRL_UPDATE_ADDR_SET(addr) |
	       VCAP_UPDATE_CTRL_CLEAR_CACHE_SET(clear) |
	       VCAP_UPDATE_CTRL_UPDATE_SHOT,
	       lan966x, VCAP_UPDATE_CTRL(admin->tgt_inst));

	lan966x_vcap_wait_update(lan966x, admin->tgt_inst);
}

static void lan966x_vcap_move(struct net_device *dev,
			      struct vcap_admin *admin,
			      u32 addr, int offset, int count)
{
	struct lan966x_port *port = netdev_priv(dev);
	struct lan966x *lan966x = port->lan966x;
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

	lan_wr(VCAP_MV_CFG_MV_NUM_POS_SET(mv_num_pos) |
	       VCAP_MV_CFG_MV_SIZE_SET(mv_size),
	       lan966x, VCAP_MV_CFG(admin->tgt_inst));

	lan_wr(VCAP_UPDATE_CTRL_UPDATE_CMD_SET(cmd) |
	       VCAP_UPDATE_CTRL_UPDATE_ENTRY_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ACTION_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_CNT_DIS_SET(0) |
	       VCAP_UPDATE_CTRL_UPDATE_ADDR_SET(addr) |
	       VCAP_UPDATE_CTRL_CLEAR_CACHE_SET(false) |
	       VCAP_UPDATE_CTRL_UPDATE_SHOT,
	       lan966x, VCAP_UPDATE_CTRL(admin->tgt_inst));

	lan966x_vcap_wait_update(lan966x, admin->tgt_inst);
}

static const struct vcap_operations lan966x_vcap_ops = {
	.validate_keyset = lan966x_vcap_validate_keyset,
	.add_default_fields = lan966x_vcap_add_default_fields,
	.cache_erase = lan966x_vcap_cache_erase,
	.cache_write = lan966x_vcap_cache_write,
	.cache_read = lan966x_vcap_cache_read,
	.init = lan966x_vcap_range_init,
	.update = lan966x_vcap_update,
	.move = lan966x_vcap_move,
	.port_info = lan966x_vcap_port_info,
};

static void lan966x_vcap_admin_free(struct vcap_admin *admin)
{
	if (!admin)
		return;

	kfree(admin->cache.keystream);
	kfree(admin->cache.maskstream);
	kfree(admin->cache.actionstream);
	mutex_destroy(&admin->lock);
	kfree(admin);
}

static struct vcap_admin *
lan966x_vcap_admin_alloc(struct lan966x *lan966x, struct vcap_control *ctrl,
			 const struct lan966x_vcap_inst *cfg)
{
	struct vcap_admin *admin;

	admin = kzalloc(sizeof(*admin), GFP_KERNEL);
	if (!admin)
		return ERR_PTR(-ENOMEM);

	mutex_init(&admin->lock);
	INIT_LIST_HEAD(&admin->list);
	INIT_LIST_HEAD(&admin->rules);
	INIT_LIST_HEAD(&admin->enabled);

	admin->vtype = cfg->vtype;
	admin->vinst = 0;
	admin->ingress = cfg->ingress;
	admin->w32be = true;
	admin->tgt_inst = cfg->tgt_inst;

	admin->lookups = cfg->lookups;
	admin->lookups_per_instance = cfg->lookups;

	admin->first_cid = cfg->first_cid;
	admin->last_cid = cfg->last_cid;

	admin->cache.keystream = kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.maskstream = kzalloc(STREAMSIZE, GFP_KERNEL);
	admin->cache.actionstream = kzalloc(STREAMSIZE, GFP_KERNEL);
	if (!admin->cache.keystream ||
	    !admin->cache.maskstream ||
	    !admin->cache.actionstream) {
		lan966x_vcap_admin_free(admin);
		return ERR_PTR(-ENOMEM);
	}

	return admin;
}

static void lan966x_vcap_block_init(struct lan966x *lan966x,
				    struct vcap_admin *admin,
				    struct lan966x_vcap_inst *cfg)
{
	admin->first_valid_addr = 0;
	admin->last_used_addr = cfg->count;
	admin->last_valid_addr = cfg->count - 1;

	lan_wr(VCAP_CORE_IDX_CORE_IDX_SET(0),
	       lan966x, VCAP_CORE_IDX(admin->tgt_inst));
	lan_wr(VCAP_CORE_MAP_CORE_MAP_SET(1),
	       lan966x, VCAP_CORE_MAP(admin->tgt_inst));

	__lan966x_vcap_range_init(lan966x, admin, admin->first_valid_addr,
				  admin->last_valid_addr -
					admin->first_valid_addr);
}

static void lan966x_vcap_port_key_deselection(struct lan966x *lan966x,
					      struct vcap_admin *admin)
{
	u32 val;

	switch (admin->vtype) {
	case VCAP_TYPE_IS1:
		val = ANA_VCAP_S1_CFG_KEY_IP6_CFG_SET(VCAP_IS1_PS_IPV6_5TUPLE_IP6) |
		      ANA_VCAP_S1_CFG_KEY_IP4_CFG_SET(VCAP_IS1_PS_IPV4_5TUPLE_IP4) |
		      ANA_VCAP_S1_CFG_KEY_OTHER_CFG_SET(VCAP_IS1_PS_OTHER_NORMAL);

		for (int p = 0; p < lan966x->num_phys_ports; ++p) {
			if (!lan966x->ports[p])
				continue;

			for (int l = 0; l < LAN966X_IS1_LOOKUPS; ++l)
				lan_wr(val, lan966x, ANA_VCAP_S1_CFG(p, l));

			lan_rmw(ANA_VCAP_CFG_S1_ENA_SET(true),
				ANA_VCAP_CFG_S1_ENA, lan966x,
				ANA_VCAP_CFG(p));
		}

		break;
	case VCAP_TYPE_IS2:
		for (int p = 0; p < lan966x->num_phys_ports; ++p)
			lan_wr(0, lan966x, ANA_VCAP_S2_CFG(p));

		break;
	case VCAP_TYPE_ES0:
		for (int p = 0; p < lan966x->num_phys_ports; ++p)
			lan_rmw(REW_PORT_CFG_ES0_EN_SET(false),
				REW_PORT_CFG_ES0_EN, lan966x,
				REW_PORT_CFG(p));
		break;
	default:
		pr_err("vcap type: %s not supported\n",
		       lan966x_vcaps[admin->vtype].name);
		break;
	}
}

int lan966x_vcap_init(struct lan966x *lan966x)
{
	struct lan966x_vcap_inst *cfg;
	struct vcap_control *ctrl;
	struct vcap_admin *admin;
	struct dentry *dir;

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->vcaps = lan966x_vcaps;
	ctrl->stats = &lan966x_vcap_stats;
	ctrl->ops = &lan966x_vcap_ops;

	INIT_LIST_HEAD(&ctrl->list);
	for (int i = 0; i < ARRAY_SIZE(lan966x_vcap_inst_cfg); ++i) {
		cfg = &lan966x_vcap_inst_cfg[i];

		admin = lan966x_vcap_admin_alloc(lan966x, ctrl, cfg);
		if (IS_ERR(admin))
			return PTR_ERR(admin);

		lan966x_vcap_block_init(lan966x, admin, cfg);
		lan966x_vcap_port_key_deselection(lan966x, admin);

		list_add_tail(&admin->list, &ctrl->list);
	}

	dir = vcap_debugfs(lan966x->dev, lan966x->debugfs_root, ctrl);
	for (int p = 0; p < lan966x->num_phys_ports; ++p) {
		if (lan966x->ports[p]) {
			vcap_port_debugfs(lan966x->dev, dir, ctrl,
					  lan966x->ports[p]->dev);

			lan_rmw(ANA_VCAP_S2_CFG_ENA_SET(true),
				ANA_VCAP_S2_CFG_ENA, lan966x,
				ANA_VCAP_S2_CFG(lan966x->ports[p]->chip_port));

			lan_rmw(ANA_VCAP_CFG_S1_ENA_SET(true),
				ANA_VCAP_CFG_S1_ENA, lan966x,
				ANA_VCAP_CFG(lan966x->ports[p]->chip_port));

			lan_rmw(REW_PORT_CFG_ES0_EN_SET(true),
				REW_PORT_CFG_ES0_EN, lan966x,
				REW_PORT_CFG(lan966x->ports[p]->chip_port));
		}
	}

	/* Statistics: Use ESDX from ES0 if hit, otherwise no counting */
	lan_rmw(REW_STAT_CFG_STAT_MODE_SET(1),
		REW_STAT_CFG_STAT_MODE, lan966x,
		REW_STAT_CFG);

	lan966x->vcap_ctrl = ctrl;

	return 0;
}

void lan966x_vcap_deinit(struct lan966x *lan966x)
{
	struct vcap_admin *admin, *admin_next;
	struct vcap_control *ctrl;

	ctrl = lan966x->vcap_ctrl;
	if (!ctrl)
		return;

	list_for_each_entry_safe(admin, admin_next, &ctrl->list, list) {
		lan966x_vcap_port_key_deselection(lan966x, admin);
		vcap_del_rules(ctrl, admin);
		list_del(&admin->list);
		lan966x_vcap_admin_free(admin);
	}

	kfree(ctrl);
}
