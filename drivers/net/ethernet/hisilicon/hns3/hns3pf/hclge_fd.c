// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2026 Hisilicon Limited.

#include <linux/ethtool.h>
#include <net/flow_offload.h>
#include <net/vxlan.h>
#include "hclge_fd.h"
#include "hclge_main.h"

static const struct key_info meta_data_key_info[] = {
	{ PACKET_TYPE_ID, 6 },
	{ IP_FRAGEMENT, 1 },
	{ ROCE_TYPE, 1 },
	{ NEXT_KEY, 5 },
	{ VLAN_NUMBER, 2 },
	{ SRC_VPORT, 12 },
	{ DST_VPORT, 12 },
	{ TUNNEL_PACKET, 1 },
};

static const struct key_info tuple_key_info[] = {
	{ OUTER_DST_MAC, 48, KEY_OPT_MAC, -1, -1 },
	{ OUTER_SRC_MAC, 48, KEY_OPT_MAC, -1, -1 },
	{ OUTER_VLAN_TAG_FST, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_VLAN_TAG_SEC, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_ETH_TYPE, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_L2_RSV, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_IP_TOS, 8, KEY_OPT_U8, -1, -1 },
	{ OUTER_IP_PROTO, 8, KEY_OPT_U8, -1, -1 },
	{ OUTER_SRC_IP, 32, KEY_OPT_IP, -1, -1 },
	{ OUTER_DST_IP, 32, KEY_OPT_IP, -1, -1 },
	{ OUTER_L3_RSV, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_SRC_PORT, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_DST_PORT, 16, KEY_OPT_LE16, -1, -1 },
	{ OUTER_L4_RSV, 32, KEY_OPT_LE32, -1, -1 },
	{ OUTER_TUN_VNI, 24, KEY_OPT_VNI,
	  offsetof(struct hclge_fd_rule, tuples.outer_tun_vni),
	  offsetof(struct hclge_fd_rule, tuples_mask.outer_tun_vni) },
	{ OUTER_TUN_FLOW_ID, 8, KEY_OPT_U8, -1, -1 },
	{ INNER_DST_MAC, 48, KEY_OPT_MAC,
	  offsetof(struct hclge_fd_rule, tuples.dst_mac),
	  offsetof(struct hclge_fd_rule, tuples_mask.dst_mac) },
	{ INNER_SRC_MAC, 48, KEY_OPT_MAC,
	  offsetof(struct hclge_fd_rule, tuples.src_mac),
	  offsetof(struct hclge_fd_rule, tuples_mask.src_mac) },
	{ INNER_VLAN_TAG_FST, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.vlan_tag1),
	  offsetof(struct hclge_fd_rule, tuples_mask.vlan_tag1) },
	{ INNER_VLAN_TAG_SEC, 16, KEY_OPT_LE16, -1, -1 },
	{ INNER_ETH_TYPE, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.ether_proto),
	  offsetof(struct hclge_fd_rule, tuples_mask.ether_proto) },
	{ INNER_L2_RSV, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.l2_user_def),
	  offsetof(struct hclge_fd_rule, tuples_mask.l2_user_def) },
	{ INNER_IP_TOS, 8, KEY_OPT_U8,
	  offsetof(struct hclge_fd_rule, tuples.ip_tos),
	  offsetof(struct hclge_fd_rule, tuples_mask.ip_tos) },
	{ INNER_IP_PROTO, 8, KEY_OPT_U8,
	  offsetof(struct hclge_fd_rule, tuples.ip_proto),
	  offsetof(struct hclge_fd_rule, tuples_mask.ip_proto) },
	{ INNER_SRC_IP, 32, KEY_OPT_IP,
	  offsetof(struct hclge_fd_rule, tuples.src_ip),
	  offsetof(struct hclge_fd_rule, tuples_mask.src_ip) },
	{ INNER_DST_IP, 32, KEY_OPT_IP,
	  offsetof(struct hclge_fd_rule, tuples.dst_ip),
	  offsetof(struct hclge_fd_rule, tuples_mask.dst_ip) },
	{ INNER_L3_RSV, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.l3_user_def),
	  offsetof(struct hclge_fd_rule, tuples_mask.l3_user_def) },
	{ INNER_SRC_PORT, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.src_port),
	  offsetof(struct hclge_fd_rule, tuples_mask.src_port) },
	{ INNER_DST_PORT, 16, KEY_OPT_LE16,
	  offsetof(struct hclge_fd_rule, tuples.dst_port),
	  offsetof(struct hclge_fd_rule, tuples_mask.dst_port) },
	{ INNER_L4_RSV, 32, KEY_OPT_LE32,
	  offsetof(struct hclge_fd_rule, tuples.l4_user_def),
	  offsetof(struct hclge_fd_rule, tuples_mask.l4_user_def) },
};

static void hclge_sync_fd_state(struct hclge_dev *hdev)
{
	if (hlist_empty(&hdev->fd_rule_list))
		hdev->fd_active_type = HCLGE_FD_RULE_NONE;
}

static void hclge_fd_inc_rule_cnt(struct hclge_dev *hdev, u16 location)
{
	if (!test_bit(location, hdev->fd_bmap)) {
		set_bit(location, hdev->fd_bmap);
		hdev->hclge_fd_rule_num++;
	}
}

static void hclge_fd_dec_rule_cnt(struct hclge_dev *hdev, u16 location)
{
	if (test_bit(location, hdev->fd_bmap)) {
		clear_bit(location, hdev->fd_bmap);
		hdev->hclge_fd_rule_num--;
	}
}

static void hclge_fd_free_node(struct hclge_dev *hdev,
			       struct hclge_fd_rule *rule)
{
	hlist_del(&rule->rule_node);
	kfree(rule);
	hclge_sync_fd_state(hdev);
}

static void hclge_update_fd_rule_node(struct hclge_dev *hdev,
				      struct hclge_fd_rule *old_rule,
				      struct hclge_fd_rule *new_rule,
				      enum HCLGE_FD_NODE_STATE state)
{
	switch (state) {
	case HCLGE_FD_TO_ADD:
	case HCLGE_FD_ACTIVE:
		/* 1) if the new state is TO_ADD, just replace the old rule
		 * with the same location, no matter its state, because the
		 * new rule will be configured to the hardware.
		 * 2) if the new state is ACTIVE, it means the new rule
		 * has been configured to the hardware, so just replace
		 * the old rule node with the same location.
		 * 3) for it doesn't add a new node to the list, so it's
		 * unnecessary to update the rule number and fd_bmap.
		 */
		new_rule->rule_node.next = old_rule->rule_node.next;
		new_rule->rule_node.pprev = old_rule->rule_node.pprev;
		memcpy(old_rule, new_rule, sizeof(*old_rule));
		kfree(new_rule);
		break;
	case HCLGE_FD_DELETED:
		hclge_fd_dec_rule_cnt(hdev, old_rule->location);
		hclge_fd_free_node(hdev, old_rule);
		break;
	case HCLGE_FD_TO_DEL:
		/* if new request is TO_DEL, and old rule is existent
		 * 1) the state of old rule is TO_DEL, we need do nothing,
		 * because we delete rule by location, other rule content
		 * is unnecessary.
		 * 2) the state of old rule is ACTIVE, we need to change its
		 * state to TO_DEL, so the rule will be deleted when periodic
		 * task being scheduled.
		 * 3) the state of old rule is TO_ADD, it means the rule hasn't
		 * been added to hardware, so we just delete the rule node from
		 * fd_rule_list directly.
		 */
		if (old_rule->state == HCLGE_FD_TO_ADD) {
			hclge_fd_dec_rule_cnt(hdev, old_rule->location);
			hclge_fd_free_node(hdev, old_rule);
			return;
		}
		old_rule->state = HCLGE_FD_TO_DEL;
		break;
	}
}

static struct hclge_fd_rule *hclge_find_fd_rule(struct hlist_head *hlist,
						u16 location,
						struct hclge_fd_rule **parent)
{
	struct hclge_fd_rule *rule;
	struct hlist_node *node;

	hlist_for_each_entry_safe(rule, node, hlist, rule_node) {
		if (rule->location == location)
			return rule;
		else if (rule->location > location)
			return NULL;
		/* record the parent node, use to keep the nodes in fd_rule_list
		 * in ascend order.
		 */
		*parent = rule;
	}

	return NULL;
}

/* insert fd rule node in ascend order according to rule->location */
static void hclge_fd_insert_rule_node(struct hlist_head *hlist,
				      struct hclge_fd_rule *rule,
				      struct hclge_fd_rule *parent)
{
	INIT_HLIST_NODE(&rule->rule_node);

	if (parent)
		hlist_add_behind(&rule->rule_node, &parent->rule_node);
	else
		hlist_add_head(&rule->rule_node, hlist);
}

static int hclge_fd_set_user_def_cmd(struct hclge_dev *hdev,
				     struct hclge_fd_user_def_cfg *cfg)
{
	struct hclge_fd_user_def_cfg_cmd *req;
	struct hclge_desc desc;
	u16 data = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_FD_USER_DEF_OP, false);

	req = (struct hclge_fd_user_def_cfg_cmd *)desc.data;

	hnae3_set_bit(data, HCLGE_FD_USER_DEF_EN_B, cfg[0].ref_cnt > 0);
	hnae3_set_field(data, HCLGE_FD_USER_DEF_OFT_M,
			HCLGE_FD_USER_DEF_OFT_S, cfg[0].offset);
	req->ol2_cfg = cpu_to_le16(data);

	data = 0;
	hnae3_set_bit(data, HCLGE_FD_USER_DEF_EN_B, cfg[1].ref_cnt > 0);
	hnae3_set_field(data, HCLGE_FD_USER_DEF_OFT_M,
			HCLGE_FD_USER_DEF_OFT_S, cfg[1].offset);
	req->ol3_cfg = cpu_to_le16(data);

	data = 0;
	hnae3_set_bit(data, HCLGE_FD_USER_DEF_EN_B, cfg[2].ref_cnt > 0);
	hnae3_set_field(data, HCLGE_FD_USER_DEF_OFT_M,
			HCLGE_FD_USER_DEF_OFT_S, cfg[2].offset);
	req->ol4_cfg = cpu_to_le16(data);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"failed to set fd user def data, ret= %d\n", ret);
	return ret;
}

static void hclge_sync_fd_user_def_cfg(struct hclge_dev *hdev, bool locked)
{
	int ret;

	if (!test_and_clear_bit(HCLGE_STATE_FD_USER_DEF_CHANGED, &hdev->state))
		return;

	if (!locked)
		spin_lock_bh(&hdev->fd_rule_lock);

	ret = hclge_fd_set_user_def_cmd(hdev, hdev->fd_cfg.user_def_cfg);
	if (ret)
		set_bit(HCLGE_STATE_FD_USER_DEF_CHANGED, &hdev->state);

	if (!locked)
		spin_unlock_bh(&hdev->fd_rule_lock);
}

static int hclge_fd_check_user_def_refcnt(struct hclge_dev *hdev,
					  struct hclge_fd_rule *rule)
{
	struct hlist_head *hlist = &hdev->fd_rule_list;
	struct hclge_fd_rule *fd_rule, *parent = NULL;
	struct hclge_fd_user_def_info *info, *old_info;
	struct hclge_fd_user_def_cfg *cfg;

	if (!rule || rule->rule_type != HCLGE_FD_EP_ACTIVE ||
	    rule->ep.user_def.layer == HCLGE_FD_USER_DEF_NONE)
		return 0;

	/* for valid layer is start from 1, so need minus 1 to get the cfg */
	cfg = &hdev->fd_cfg.user_def_cfg[rule->ep.user_def.layer - 1];
	info = &rule->ep.user_def;

	if (!cfg->ref_cnt || cfg->offset == info->offset)
		return 0;

	if (cfg->ref_cnt > 1)
		goto error;

	fd_rule = hclge_find_fd_rule(hlist, rule->location, &parent);
	if (fd_rule) {
		old_info = &fd_rule->ep.user_def;
		if (info->layer == old_info->layer)
			return 0;
	}

error:
	dev_err(&hdev->pdev->dev,
		"No available offset for layer%d fd rule, each layer only support one user def offset.\n",
		info->layer + 1);
	return -ENOSPC;
}

static void hclge_fd_inc_user_def_refcnt(struct hclge_dev *hdev,
					 struct hclge_fd_rule *rule)
{
	struct hclge_fd_user_def_cfg *cfg;

	if (!rule || rule->rule_type != HCLGE_FD_EP_ACTIVE ||
	    rule->ep.user_def.layer == HCLGE_FD_USER_DEF_NONE)
		return;

	cfg = &hdev->fd_cfg.user_def_cfg[rule->ep.user_def.layer - 1];
	if (!cfg->ref_cnt) {
		cfg->offset = rule->ep.user_def.offset;
		set_bit(HCLGE_STATE_FD_USER_DEF_CHANGED, &hdev->state);
	}
	cfg->ref_cnt++;
}

static void hclge_fd_dec_user_def_refcnt(struct hclge_dev *hdev,
					 struct hclge_fd_rule *rule)
{
	struct hclge_fd_user_def_cfg *cfg;

	if (!rule || rule->rule_type != HCLGE_FD_EP_ACTIVE ||
	    rule->ep.user_def.layer == HCLGE_FD_USER_DEF_NONE)
		return;

	cfg = &hdev->fd_cfg.user_def_cfg[rule->ep.user_def.layer - 1];
	if (!cfg->ref_cnt)
		return;

	cfg->ref_cnt--;
	if (!cfg->ref_cnt) {
		cfg->offset = 0;
		set_bit(HCLGE_STATE_FD_USER_DEF_CHANGED, &hdev->state);
	}
}

static void hclge_update_fd_list(struct hclge_dev *hdev,
				 enum HCLGE_FD_NODE_STATE state, u16 location,
				 struct hclge_fd_rule *new_rule)
{
	struct hlist_head *hlist = &hdev->fd_rule_list;
	struct hclge_fd_rule *fd_rule, *parent = NULL;

	fd_rule = hclge_find_fd_rule(hlist, location, &parent);
	if (fd_rule) {
		hclge_fd_dec_user_def_refcnt(hdev, fd_rule);
		if (state == HCLGE_FD_ACTIVE)
			hclge_fd_inc_user_def_refcnt(hdev, new_rule);
		hclge_sync_fd_user_def_cfg(hdev, true);

		hclge_update_fd_rule_node(hdev, fd_rule, new_rule, state);
		return;
	}

	/* it's unlikely to fail here, because we have checked the rule
	 * exist before.
	 */
	if (unlikely(state == HCLGE_FD_TO_DEL || state == HCLGE_FD_DELETED)) {
		dev_warn(&hdev->pdev->dev,
			 "failed to delete fd rule %u, it's inexistent\n",
			 location);
		return;
	}

	hclge_fd_inc_user_def_refcnt(hdev, new_rule);
	hclge_sync_fd_user_def_cfg(hdev, true);

	hclge_fd_insert_rule_node(hlist, new_rule, parent);
	hclge_fd_inc_rule_cnt(hdev, new_rule->location);

	if (state == HCLGE_FD_TO_ADD) {
		set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);
		hclge_task_schedule(hdev, 0);
	}
}

static int hclge_get_fd_mode(struct hclge_dev *hdev, u8 *fd_mode)
{
	struct hclge_get_fd_mode_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_FD_MODE_CTRL, true);

	req = (struct hclge_get_fd_mode_cmd *)desc.data;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "get fd mode fail, ret=%d\n", ret);
		return ret;
	}

	*fd_mode = req->mode;

	return ret;
}

static int hclge_get_fd_allocation(struct hclge_dev *hdev,
				   u32 *stage1_entry_num,
				   u32 *stage2_entry_num,
				   u16 *stage1_counter_num,
				   u16 *stage2_counter_num)
{
	struct hclge_get_fd_allocation_cmd *req;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_FD_GET_ALLOCATION, true);

	req = (struct hclge_get_fd_allocation_cmd *)desc.data;

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret) {
		dev_err(&hdev->pdev->dev, "query fd allocation fail, ret=%d\n",
			ret);
		return ret;
	}

	*stage1_entry_num = le32_to_cpu(req->stage1_entry_num);
	*stage2_entry_num = le32_to_cpu(req->stage2_entry_num);
	*stage1_counter_num = le16_to_cpu(req->stage1_counter_num);
	*stage2_counter_num = le16_to_cpu(req->stage2_counter_num);

	return ret;
}

static int hclge_set_fd_key_config(struct hclge_dev *hdev,
				   enum HCLGE_FD_STAGE stage_num)
{
	struct hclge_set_fd_key_config_cmd *req;
	struct hclge_fd_key_cfg *stage;
	struct hclge_desc desc;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_FD_KEY_CONFIG, false);

	req = (struct hclge_set_fd_key_config_cmd *)desc.data;
	stage = &hdev->fd_cfg.key_cfg[stage_num];
	req->stage = stage_num;
	req->key_select = stage->key_sel;
	req->inner_sipv6_word_en = stage->inner_sipv6_word_en;
	req->inner_dipv6_word_en = stage->inner_dipv6_word_en;
	req->outer_sipv6_word_en = stage->outer_sipv6_word_en;
	req->outer_dipv6_word_en = stage->outer_dipv6_word_en;
	req->tuple_mask = cpu_to_le32(~stage->tuple_active);
	req->meta_data_mask = cpu_to_le32(~stage->meta_data_active);

	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "set fd key fail, ret=%d\n", ret);

	return ret;
}

static void hclge_fd_disable_user_def(struct hclge_dev *hdev)
{
	struct hclge_fd_user_def_cfg *cfg = hdev->fd_cfg.user_def_cfg;

	spin_lock_bh(&hdev->fd_rule_lock);
	memset(cfg, 0, sizeof(hdev->fd_cfg.user_def_cfg));
	spin_unlock_bh(&hdev->fd_rule_lock);

	hclge_fd_set_user_def_cmd(hdev, cfg);
}

int hclge_init_fd_config(struct hclge_dev *hdev)
{
#define LOW_2_WORDS		0x03
	struct hclge_fd_key_cfg *key_cfg;
	int ret;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return 0;

	ret = hclge_get_fd_mode(hdev, &hdev->fd_cfg.fd_mode);
	if (ret)
		return ret;

	switch (hdev->fd_cfg.fd_mode) {
	case HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1:
		hdev->fd_cfg.max_key_length = MAX_KEY_LENGTH;
		break;
	case HCLGE_FD_MODE_DEPTH_4K_WIDTH_200B_STAGE_1:
		hdev->fd_cfg.max_key_length = MAX_KEY_LENGTH / 2;
		break;
	default:
		dev_err(&hdev->pdev->dev,
			"Unsupported flow director mode %u\n",
			hdev->fd_cfg.fd_mode);
		return -EOPNOTSUPP;
	}

	key_cfg = &hdev->fd_cfg.key_cfg[HCLGE_FD_STAGE_1];
	key_cfg->key_sel = HCLGE_FD_KEY_BASE_ON_TUPLE;
	key_cfg->inner_sipv6_word_en = LOW_2_WORDS;
	key_cfg->inner_dipv6_word_en = LOW_2_WORDS;
	key_cfg->outer_sipv6_word_en = 0;
	key_cfg->outer_dipv6_word_en = 0;

	key_cfg->tuple_active = BIT(INNER_VLAN_TAG_FST) | BIT(INNER_ETH_TYPE) |
				BIT(INNER_IP_PROTO) | BIT(INNER_IP_TOS) |
				BIT(INNER_SRC_IP) | BIT(INNER_DST_IP) |
				BIT(INNER_SRC_PORT) | BIT(INNER_DST_PORT);

	/* If use max 400bit key, we can support tuples for ether type */
	if (hdev->fd_cfg.fd_mode == HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1) {
		key_cfg->tuple_active |= BIT(INNER_DST_MAC) |
					 BIT(INNER_SRC_MAC) |
					 BIT(OUTER_TUN_VNI);
		if (hdev->ae_dev->dev_version >= HNAE3_DEVICE_VERSION_V3)
			key_cfg->tuple_active |= HCLGE_FD_TUPLE_USER_DEF_TUPLES;
	}

	/* roce_type is used to filter roce frames
	 * dst_vport is used to specify the rule
	 */
	key_cfg->meta_data_active = BIT(ROCE_TYPE) | BIT(DST_VPORT);

	ret = hclge_get_fd_allocation(hdev,
				      &hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1],
				      &hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_2],
				      &hdev->fd_cfg.cnt_num[HCLGE_FD_STAGE_1],
				      &hdev->fd_cfg.cnt_num[HCLGE_FD_STAGE_2]);
	if (ret)
		return ret;

	return hclge_set_fd_key_config(hdev, HCLGE_FD_STAGE_1);
}

static int hclge_fd_tcam_config(struct hclge_dev *hdev, u8 stage, bool sel_x,
				int loc, u8 *key, bool is_add)
{
	struct hclge_fd_tcam_config_1_cmd *req1;
	struct hclge_fd_tcam_config_2_cmd *req2;
	struct hclge_fd_tcam_config_3_cmd *req3;
	struct hclge_desc desc[3];
	int ret;

	hclge_cmd_setup_basic_desc(&desc[0], HCLGE_OPC_FD_TCAM_OP, false);
	desc[0].flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[1], HCLGE_OPC_FD_TCAM_OP, false);
	desc[1].flag |= cpu_to_le16(HCLGE_COMM_CMD_FLAG_NEXT);
	hclge_cmd_setup_basic_desc(&desc[2], HCLGE_OPC_FD_TCAM_OP, false);

	req1 = (struct hclge_fd_tcam_config_1_cmd *)desc[0].data;
	req2 = (struct hclge_fd_tcam_config_2_cmd *)desc[1].data;
	req3 = (struct hclge_fd_tcam_config_3_cmd *)desc[2].data;

	req1->stage = stage;
	req1->xy_sel = sel_x ? 1 : 0;
	hnae3_set_bit(req1->port_info, HCLGE_FD_EPORT_SW_EN_B, 0);
	req1->index = cpu_to_le32(loc);
	req1->entry_vld = sel_x ? is_add : 0;

	if (key) {
		memcpy(req1->tcam_data, &key[0], sizeof(req1->tcam_data));
		memcpy(req2->tcam_data, &key[sizeof(req1->tcam_data)],
		       sizeof(req2->tcam_data));
		memcpy(req3->tcam_data, &key[sizeof(req1->tcam_data) +
		       sizeof(req2->tcam_data)], sizeof(req3->tcam_data));
	}

	ret = hclge_cmd_send(&hdev->hw, desc, 3);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"config tcam key fail, ret=%d\n",
			ret);

	return ret;
}

static int hclge_fd_ad_config(struct hclge_dev *hdev, u8 stage, int loc,
			      struct hclge_fd_ad_data *action)
{
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(hdev->pdev);
	struct hclge_fd_ad_config_cmd *req;
	struct hclge_desc desc;
	u64 ad_data = 0;
	int ret;

	hclge_cmd_setup_basic_desc(&desc, HCLGE_OPC_FD_AD_OP, false);

	req = (struct hclge_fd_ad_config_cmd *)desc.data;
	req->index = cpu_to_le32(loc);
	req->stage = stage;

	hnae3_set_bit(ad_data, HCLGE_FD_AD_WR_RULE_ID_B,
		      action->write_rule_id_to_bd);
	hnae3_set_field(ad_data, HCLGE_FD_AD_RULE_ID_M, HCLGE_FD_AD_RULE_ID_S,
			action->rule_id);
	if (test_bit(HNAE3_DEV_SUPPORT_FD_FORWARD_TC_B, ae_dev->caps)) {
		hnae3_set_bit(ad_data, HCLGE_FD_AD_TC_OVRD_B,
			      action->override_tc);
		hnae3_set_field(ad_data, HCLGE_FD_AD_TC_SIZE_M,
				HCLGE_FD_AD_TC_SIZE_S, (u32)action->tc_size);
	}
	hnae3_set_bit(ad_data, HCLGE_FD_AD_QID_H_B,
		      action->queue_id >= HCLGE_TQP_MAX_SIZE_DEV_V2 ? 1 : 0);
	hnae3_set_bit(ad_data, HCLGE_FD_AD_COUNTER_NUM_H_B,
		      action->counter_id >= HCLGE_FD_COUNTER_MAX_SIZE_DEV_V2 ?
		      1 : 0);
	ad_data <<= 32;
	hnae3_set_bit(ad_data, HCLGE_FD_AD_DROP_B, action->drop_packet);
	hnae3_set_bit(ad_data, HCLGE_FD_AD_DIRECT_QID_B,
		      action->forward_to_direct_queue);
	hnae3_set_field(ad_data, HCLGE_FD_AD_QID_L_M, HCLGE_FD_AD_QID_L_S,
			action->queue_id);
	hnae3_set_bit(ad_data, HCLGE_FD_AD_USE_COUNTER_B, action->use_counter);
	hnae3_set_field(ad_data, HCLGE_FD_AD_COUNTER_NUM_L_M,
			HCLGE_FD_AD_COUNTER_NUM_L_S, action->counter_id);
	hnae3_set_bit(ad_data, HCLGE_FD_AD_NXT_STEP_B, action->use_next_stage);
	hnae3_set_field(ad_data, HCLGE_FD_AD_NXT_KEY_M, HCLGE_FD_AD_NXT_KEY_S,
			action->next_input_key);

	req->ad_data = cpu_to_le64(ad_data);
	ret = hclge_cmd_send(&hdev->hw, &desc, 1);
	if (ret)
		dev_err(&hdev->pdev->dev, "fd ad config fail, ret=%d\n", ret);

	return ret;
}

static bool hclge_fd_convert_tuple(u32 tuple_bit, u8 *key_x, u8 *key_y,
				   struct hclge_fd_rule *rule)
{
	int offset, moffset, ip_offset;
	enum HCLGE_FD_KEY_OPT key_opt;
	u16 tmp_x_s, tmp_y_s;
	u32 tmp_x_l, tmp_y_l;
	u8 *p = (u8 *)rule;
	__le32 le_x, le_y;
	int i;

	if (rule->unused_tuple & BIT(tuple_bit))
		return true;

	key_opt = tuple_key_info[tuple_bit].key_opt;
	offset = tuple_key_info[tuple_bit].offset;
	moffset = tuple_key_info[tuple_bit].moffset;

	switch (key_opt) {
	case KEY_OPT_U8:
		calc_x(*key_x, p[offset], p[moffset]);
		calc_y(*key_y, p[offset], p[moffset]);

		return true;
	case KEY_OPT_LE16:
		calc_x(tmp_x_s, *(u16 *)(&p[offset]), *(u16 *)(&p[moffset]));
		calc_y(tmp_y_s, *(u16 *)(&p[offset]), *(u16 *)(&p[moffset]));
		*(__le16 *)key_x = cpu_to_le16(tmp_x_s);
		*(__le16 *)key_y = cpu_to_le16(tmp_y_s);

		return true;
	case KEY_OPT_LE32:
		calc_x(tmp_x_l, *(u32 *)(&p[offset]), *(u32 *)(&p[moffset]));
		calc_y(tmp_y_l, *(u32 *)(&p[offset]), *(u32 *)(&p[moffset]));
		*(__le32 *)key_x = cpu_to_le32(tmp_x_l);
		*(__le32 *)key_y = cpu_to_le32(tmp_y_l);

		return true;
	case KEY_OPT_MAC:
		for (i = 0; i < ETH_ALEN; i++) {
			calc_x(key_x[ETH_ALEN - 1 - i], p[offset + i],
			       p[moffset + i]);
			calc_y(key_y[ETH_ALEN - 1 - i], p[offset + i],
			       p[moffset + i]);
		}

		return true;
	case KEY_OPT_IP:
		ip_offset = IPV4_INDEX * sizeof(u32);
		calc_x(tmp_x_l, *(u32 *)(&p[offset + ip_offset]),
		       *(u32 *)(&p[moffset + ip_offset]));
		calc_y(tmp_y_l, *(u32 *)(&p[offset + ip_offset]),
		       *(u32 *)(&p[moffset + ip_offset]));
		*(__le32 *)key_x = cpu_to_le32(tmp_x_l);
		*(__le32 *)key_y = cpu_to_le32(tmp_y_l);

		return true;
	case KEY_OPT_VNI:
		calc_x(tmp_x_l, *(u32 *)(&p[offset]), *(u32 *)(&p[moffset]));
		calc_y(tmp_y_l, *(u32 *)(&p[offset]), *(u32 *)(&p[moffset]));
		le_x = cpu_to_le32(tmp_x_l);
		le_y = cpu_to_le32(tmp_y_l);
		memcpy(key_x, &le_x, HCLGE_VNI_LENGTH);
		memcpy(key_y, &le_y, HCLGE_VNI_LENGTH);

		return true;
	default:
		return false;
	}
}

static void hclge_fd_convert_meta_data(struct hclge_fd_key_cfg *key_cfg,
				       __le32 *key_x, __le32 *key_y,
				       struct hclge_fd_rule *rule)
{
	u32 tuple_bit, meta_data = 0, tmp_x, tmp_y, port_number;
	u8 cur_pos = 0, tuple_size, shift_bits;
	unsigned int i;

	for (i = 0; i < MAX_META_DATA; i++) {
		tuple_size = meta_data_key_info[i].key_length;
		tuple_bit = key_cfg->meta_data_active & BIT(i);

		switch (tuple_bit) {
		case BIT(ROCE_TYPE):
			hnae3_set_bit(meta_data, cur_pos, NIC_PACKET);
			cur_pos += tuple_size;
			break;
		case BIT(DST_VPORT):
			port_number = hclge_get_port_number(HOST_PORT, 0,
							    rule->vf_id, 0);
			hnae3_set_field(meta_data,
					GENMASK(cur_pos + tuple_size, cur_pos),
					cur_pos, port_number);
			cur_pos += tuple_size;
			break;
		default:
			break;
		}
	}

	calc_x(tmp_x, meta_data, 0xFFFFFFFF);
	calc_y(tmp_y, meta_data, 0xFFFFFFFF);
	shift_bits = sizeof(meta_data) * 8 - cur_pos;

	*key_x = cpu_to_le32(tmp_x << shift_bits);
	*key_y = cpu_to_le32(tmp_y << shift_bits);
}

/* A complete key is combined with meta data key and tuple key.
 * Meta data key is stored at the MSB region, and tuple key is stored at
 * the LSB region, unused bits will be filled 0.
 */
static int hclge_config_key(struct hclge_dev *hdev, u8 stage,
			    struct hclge_fd_rule *rule)
{
	struct hclge_fd_key_cfg *key_cfg = &hdev->fd_cfg.key_cfg[stage];
	u8 key_x[MAX_KEY_BYTES], key_y[MAX_KEY_BYTES];
	u8 *cur_key_x, *cur_key_y;
	u8 meta_data_region;
	u8 tuple_size;
	int ret;
	u32 i;

	memset(key_x, 0, sizeof(key_x));
	memset(key_y, 0, sizeof(key_y));
	cur_key_x = key_x;
	cur_key_y = key_y;

	for (i = 0; i < MAX_TUPLE; i++) {
		bool tuple_valid;

		tuple_size = tuple_key_info[i].key_length / 8;
		if (!(key_cfg->tuple_active & BIT(i)))
			continue;

		tuple_valid = hclge_fd_convert_tuple(i, cur_key_x,
						     cur_key_y, rule);
		if (tuple_valid) {
			cur_key_x += tuple_size;
			cur_key_y += tuple_size;
		}
	}

	meta_data_region = hdev->fd_cfg.max_key_length / 8 -
			MAX_META_DATA_LENGTH / 8;

	hclge_fd_convert_meta_data(key_cfg,
				   (__le32 *)(key_x + meta_data_region),
				   (__le32 *)(key_y + meta_data_region),
				   rule);

	ret = hclge_fd_tcam_config(hdev, stage, false, rule->location, key_y,
				   true);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"fd key_y config fail, loc=%u, ret=%d\n",
			rule->queue_id, ret);
		return ret;
	}

	ret = hclge_fd_tcam_config(hdev, stage, true, rule->location, key_x,
				   true);
	if (ret)
		dev_err(&hdev->pdev->dev,
			"fd key_x config fail, loc=%u, ret=%d\n",
			rule->queue_id, ret);
	return ret;
}

static int hclge_config_action(struct hclge_dev *hdev, u8 stage,
			       struct hclge_fd_rule *rule)
{
	struct hclge_vport *vport = hdev->vport;
	struct hnae3_knic_private_info *kinfo = &vport->nic.kinfo;
	struct hclge_fd_ad_data ad_data;

	memset(&ad_data, 0, sizeof(struct hclge_fd_ad_data));
	ad_data.ad_id = rule->location;

	if (rule->action == HCLGE_FD_ACTION_DROP_PACKET) {
		ad_data.drop_packet = true;
	} else if (rule->action == HCLGE_FD_ACTION_SELECT_TC) {
		ad_data.override_tc = true;
		ad_data.queue_id =
			kinfo->tc_info.tqp_offset[rule->cls_flower.tc];
		ad_data.tc_size =
			ilog2(kinfo->tc_info.tqp_count[rule->cls_flower.tc]);
	} else {
		ad_data.forward_to_direct_queue = true;
		ad_data.queue_id = rule->queue_id;
	}

	if (hdev->fd_cfg.cnt_num[HCLGE_FD_STAGE_1]) {
		ad_data.use_counter = true;
		ad_data.counter_id = rule->vf_id %
				     hdev->fd_cfg.cnt_num[HCLGE_FD_STAGE_1];
	} else {
		ad_data.use_counter = false;
		ad_data.counter_id = 0;
	}

	ad_data.use_next_stage = false;
	ad_data.next_input_key = 0;

	ad_data.write_rule_id_to_bd = true;
	ad_data.rule_id = rule->location;

	return hclge_fd_ad_config(hdev, stage, ad_data.ad_id, &ad_data);
}

static int hclge_fd_check_tcpip4_tuple(struct ethtool_tcpip4_spec *spec,
				       u32 *unused_tuple)
{
	if (!spec || !unused_tuple)
		return -EINVAL;

	*unused_tuple |= BIT(INNER_SRC_MAC) | BIT(INNER_DST_MAC);

	if (!spec->ip4src)
		*unused_tuple |= BIT(INNER_SRC_IP);

	if (!spec->ip4dst)
		*unused_tuple |= BIT(INNER_DST_IP);

	if (!spec->psrc)
		*unused_tuple |= BIT(INNER_SRC_PORT);

	if (!spec->pdst)
		*unused_tuple |= BIT(INNER_DST_PORT);

	if (!spec->tos)
		*unused_tuple |= BIT(INNER_IP_TOS);

	return 0;
}

static int hclge_fd_check_ip4_tuple(struct ethtool_usrip4_spec *spec,
				    u32 *unused_tuple)
{
	if (!spec || !unused_tuple)
		return -EINVAL;

	*unused_tuple |= BIT(INNER_SRC_MAC) | BIT(INNER_DST_MAC) |
		BIT(INNER_SRC_PORT) | BIT(INNER_DST_PORT);

	if (!spec->ip4src)
		*unused_tuple |= BIT(INNER_SRC_IP);

	if (!spec->ip4dst)
		*unused_tuple |= BIT(INNER_DST_IP);

	if (!spec->tos)
		*unused_tuple |= BIT(INNER_IP_TOS);

	if (!spec->proto)
		*unused_tuple |= BIT(INNER_IP_PROTO);

	if (spec->l4_4_bytes)
		return -EOPNOTSUPP;

	if (spec->ip_ver != ETH_RX_NFC_IP4)
		return -EOPNOTSUPP;

	return 0;
}

static int hclge_fd_check_tcpip6_tuple(struct ethtool_tcpip6_spec *spec,
				       u32 *unused_tuple)
{
	if (!spec || !unused_tuple)
		return -EINVAL;

	*unused_tuple |= BIT(INNER_SRC_MAC) | BIT(INNER_DST_MAC);

	/* check whether src/dst ip address used */
	if (ipv6_addr_any((struct in6_addr *)spec->ip6src))
		*unused_tuple |= BIT(INNER_SRC_IP);

	if (ipv6_addr_any((struct in6_addr *)spec->ip6dst))
		*unused_tuple |= BIT(INNER_DST_IP);

	if (!spec->psrc)
		*unused_tuple |= BIT(INNER_SRC_PORT);

	if (!spec->pdst)
		*unused_tuple |= BIT(INNER_DST_PORT);

	if (!spec->tclass)
		*unused_tuple |= BIT(INNER_IP_TOS);

	return 0;
}

static int hclge_fd_check_ip6_tuple(struct ethtool_usrip6_spec *spec,
				    u32 *unused_tuple)
{
	if (!spec || !unused_tuple)
		return -EINVAL;

	*unused_tuple |= BIT(INNER_SRC_MAC) | BIT(INNER_DST_MAC) |
			BIT(INNER_SRC_PORT) | BIT(INNER_DST_PORT);

	/* check whether src/dst ip address used */
	if (ipv6_addr_any((struct in6_addr *)spec->ip6src))
		*unused_tuple |= BIT(INNER_SRC_IP);

	if (ipv6_addr_any((struct in6_addr *)spec->ip6dst))
		*unused_tuple |= BIT(INNER_DST_IP);

	if (!spec->l4_proto)
		*unused_tuple |= BIT(INNER_IP_PROTO);

	if (!spec->tclass)
		*unused_tuple |= BIT(INNER_IP_TOS);

	if (spec->l4_4_bytes)
		return -EOPNOTSUPP;

	return 0;
}

static int hclge_fd_check_ether_tuple(struct ethhdr *spec, u32 *unused_tuple)
{
	if (!spec || !unused_tuple)
		return -EINVAL;

	*unused_tuple |= BIT(INNER_SRC_IP) | BIT(INNER_DST_IP) |
		BIT(INNER_SRC_PORT) | BIT(INNER_DST_PORT) |
		BIT(INNER_IP_TOS) | BIT(INNER_IP_PROTO);

	if (is_zero_ether_addr(spec->h_source))
		*unused_tuple |= BIT(INNER_SRC_MAC);

	if (is_zero_ether_addr(spec->h_dest))
		*unused_tuple |= BIT(INNER_DST_MAC);

	if (!spec->h_proto)
		*unused_tuple |= BIT(INNER_ETH_TYPE);

	return 0;
}

static int hclge_fd_check_ext_tuple(struct hclge_dev *hdev,
				    struct ethtool_rx_flow_spec *fs,
				    u32 *unused_tuple)
{
	if (fs->flow_type & FLOW_EXT) {
		if (fs->h_ext.vlan_etype) {
			dev_err(&hdev->pdev->dev, "vlan-etype is not supported!\n");
			return -EOPNOTSUPP;
		}

		if (!fs->h_ext.vlan_tci)
			*unused_tuple |= BIT(INNER_VLAN_TAG_FST);

		if (fs->m_ext.vlan_tci &&
		    be16_to_cpu(fs->h_ext.vlan_tci) >= VLAN_N_VID) {
			dev_err(&hdev->pdev->dev,
				"failed to config vlan_tci, invalid vlan_tci: %u, max is %d.\n",
				ntohs(fs->h_ext.vlan_tci), VLAN_N_VID - 1);
			return -EINVAL;
		}
	} else {
		*unused_tuple |= BIT(INNER_VLAN_TAG_FST);
	}

	if (fs->flow_type & FLOW_MAC_EXT) {
		if (hdev->fd_cfg.fd_mode !=
		    HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1) {
			dev_err(&hdev->pdev->dev,
				"FLOW_MAC_EXT is not supported in current fd mode!\n");
			return -EOPNOTSUPP;
		}

		if (is_zero_ether_addr(fs->h_ext.h_dest))
			*unused_tuple |= BIT(INNER_DST_MAC);
		else
			*unused_tuple &= ~BIT(INNER_DST_MAC);
	}

	return 0;
}

static int hclge_fd_get_user_def_layer(u32 flow_type, u32 *unused_tuple,
				       struct hclge_fd_user_def_info *info)
{
	switch (flow_type) {
	case ETHER_FLOW:
		info->layer = HCLGE_FD_USER_DEF_L2;
		*unused_tuple &= ~BIT(INNER_L2_RSV);
		break;
	case IP_USER_FLOW:
	case IPV6_USER_FLOW:
		info->layer = HCLGE_FD_USER_DEF_L3;
		*unused_tuple &= ~BIT(INNER_L3_RSV);
		break;
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		info->layer = HCLGE_FD_USER_DEF_L4;
		*unused_tuple &= ~BIT(INNER_L4_RSV);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static bool hclge_fd_is_user_def_all_masked(struct ethtool_rx_flow_spec *fs)
{
	return be32_to_cpu(fs->m_ext.data[1] | fs->m_ext.data[0]) == 0;
}

static int hclge_fd_parse_user_def_field(struct hclge_dev *hdev,
					 struct ethtool_rx_flow_spec *fs,
					 u32 *unused_tuple,
					 struct hclge_fd_user_def_info *info)
{
	u32 tuple_active = hdev->fd_cfg.key_cfg[HCLGE_FD_STAGE_1].tuple_active;
	u32 flow_type = fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT);
	u16 data, offset, data_mask, offset_mask;
	int ret;

	info->layer = HCLGE_FD_USER_DEF_NONE;
	*unused_tuple |= HCLGE_FD_TUPLE_USER_DEF_TUPLES;

	if (!(fs->flow_type & FLOW_EXT) || hclge_fd_is_user_def_all_masked(fs))
		return 0;

	/* user-def data from ethtool is 64 bit value, the bit0~15 is used
	 * for data, and bit32~47 is used for offset.
	 */
	data = be32_to_cpu(fs->h_ext.data[1]) & HCLGE_FD_USER_DEF_DATA;
	data_mask = be32_to_cpu(fs->m_ext.data[1]) & HCLGE_FD_USER_DEF_DATA;
	offset = be32_to_cpu(fs->h_ext.data[0]) & HCLGE_FD_USER_DEF_OFFSET;
	offset_mask = be32_to_cpu(fs->m_ext.data[0]) & HCLGE_FD_USER_DEF_OFFSET;

	if (!(tuple_active & HCLGE_FD_TUPLE_USER_DEF_TUPLES)) {
		dev_err(&hdev->pdev->dev, "user-def bytes are not supported\n");
		return -EOPNOTSUPP;
	}

	if (offset > HCLGE_FD_MAX_USER_DEF_OFFSET) {
		dev_err(&hdev->pdev->dev,
			"user-def offset[%u] should be no more than %u\n",
			offset, HCLGE_FD_MAX_USER_DEF_OFFSET);
		return -EINVAL;
	}

	if (offset_mask != HCLGE_FD_USER_DEF_OFFSET_UNMASK) {
		dev_err(&hdev->pdev->dev, "user-def offset can't be masked\n");
		return -EINVAL;
	}

	ret = hclge_fd_get_user_def_layer(flow_type, unused_tuple, info);
	if (ret) {
		dev_err(&hdev->pdev->dev,
			"unsupported flow type for user-def bytes, ret = %d\n",
			ret);
		return ret;
	}

	info->data = data;
	info->data_mask = data_mask;
	info->offset = offset;

	return 0;
}

static int hclge_fd_check_spec(struct hclge_dev *hdev,
			       struct ethtool_rx_flow_spec *fs,
			       u32 *unused_tuple,
			       struct hclge_fd_user_def_info *info)
{
	u32 flow_type;
	int ret;

	if (fs->location >= hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1]) {
		dev_err(&hdev->pdev->dev,
			"failed to config fd rules, invalid rule location: %u, max is %u\n.",
			fs->location,
			hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1] - 1);
		return -EINVAL;
	}

	ret = hclge_fd_parse_user_def_field(hdev, fs, unused_tuple, info);
	if (ret)
		return ret;

	flow_type = fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT);
	switch (flow_type) {
	case SCTP_V4_FLOW:
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		ret = hclge_fd_check_tcpip4_tuple(&fs->h_u.tcp_ip4_spec,
						  unused_tuple);
		break;
	case IP_USER_FLOW:
		ret = hclge_fd_check_ip4_tuple(&fs->h_u.usr_ip4_spec,
					       unused_tuple);
		break;
	case SCTP_V6_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		ret = hclge_fd_check_tcpip6_tuple(&fs->h_u.tcp_ip6_spec,
						  unused_tuple);
		break;
	case IPV6_USER_FLOW:
		ret = hclge_fd_check_ip6_tuple(&fs->h_u.usr_ip6_spec,
					       unused_tuple);
		break;
	case ETHER_FLOW:
		if (hdev->fd_cfg.fd_mode !=
			HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1) {
			dev_err(&hdev->pdev->dev,
				"ETHER_FLOW is not supported in current fd mode!\n");
			return -EOPNOTSUPP;
		}

		ret = hclge_fd_check_ether_tuple(&fs->h_u.ether_spec,
						 unused_tuple);
		break;
	default:
		dev_err(&hdev->pdev->dev,
			"unsupported protocol type, protocol type = %#x\n",
			flow_type);
		return -EOPNOTSUPP;
	}

	if (ret) {
		dev_err(&hdev->pdev->dev,
			"failed to check flow union tuple, ret = %d\n",
			ret);
		return ret;
	}

	return hclge_fd_check_ext_tuple(hdev, fs, unused_tuple);
}

static void hclge_fd_get_tcpip4_tuple(struct ethtool_rx_flow_spec *fs,
				      struct hclge_fd_rule *rule, u8 ip_proto)
{
	rule->tuples.src_ip[IPV4_INDEX] =
			be32_to_cpu(fs->h_u.tcp_ip4_spec.ip4src);
	rule->tuples_mask.src_ip[IPV4_INDEX] =
			be32_to_cpu(fs->m_u.tcp_ip4_spec.ip4src);

	rule->tuples.dst_ip[IPV4_INDEX] =
			be32_to_cpu(fs->h_u.tcp_ip4_spec.ip4dst);
	rule->tuples_mask.dst_ip[IPV4_INDEX] =
			be32_to_cpu(fs->m_u.tcp_ip4_spec.ip4dst);

	rule->tuples.src_port = be16_to_cpu(fs->h_u.tcp_ip4_spec.psrc);
	rule->tuples_mask.src_port = be16_to_cpu(fs->m_u.tcp_ip4_spec.psrc);

	rule->tuples.dst_port = be16_to_cpu(fs->h_u.tcp_ip4_spec.pdst);
	rule->tuples_mask.dst_port = be16_to_cpu(fs->m_u.tcp_ip4_spec.pdst);

	rule->tuples.ip_tos = fs->h_u.tcp_ip4_spec.tos;
	rule->tuples_mask.ip_tos = fs->m_u.tcp_ip4_spec.tos;

	rule->tuples.ether_proto = ETH_P_IP;
	rule->tuples_mask.ether_proto = 0xFFFF;

	rule->tuples.ip_proto = ip_proto;
	rule->tuples_mask.ip_proto = 0xFF;
}

static void hclge_fd_get_ip4_tuple(struct ethtool_rx_flow_spec *fs,
				   struct hclge_fd_rule *rule)
{
	rule->tuples.src_ip[IPV4_INDEX] =
			be32_to_cpu(fs->h_u.usr_ip4_spec.ip4src);
	rule->tuples_mask.src_ip[IPV4_INDEX] =
			be32_to_cpu(fs->m_u.usr_ip4_spec.ip4src);

	rule->tuples.dst_ip[IPV4_INDEX] =
			be32_to_cpu(fs->h_u.usr_ip4_spec.ip4dst);
	rule->tuples_mask.dst_ip[IPV4_INDEX] =
			be32_to_cpu(fs->m_u.usr_ip4_spec.ip4dst);

	rule->tuples.ip_tos = fs->h_u.usr_ip4_spec.tos;
	rule->tuples_mask.ip_tos = fs->m_u.usr_ip4_spec.tos;

	rule->tuples.ip_proto = fs->h_u.usr_ip4_spec.proto;
	rule->tuples_mask.ip_proto = fs->m_u.usr_ip4_spec.proto;

	rule->tuples.ether_proto = ETH_P_IP;
	rule->tuples_mask.ether_proto = 0xFFFF;
}

static void hclge_fd_get_tcpip6_tuple(struct ethtool_rx_flow_spec *fs,
				      struct hclge_fd_rule *rule, u8 ip_proto)
{
	ipv6_addr_be32_to_cpu(rule->tuples.src_ip,
			      fs->h_u.tcp_ip6_spec.ip6src);
	ipv6_addr_be32_to_cpu(rule->tuples_mask.src_ip,
			      fs->m_u.tcp_ip6_spec.ip6src);

	ipv6_addr_be32_to_cpu(rule->tuples.dst_ip,
			      fs->h_u.tcp_ip6_spec.ip6dst);
	ipv6_addr_be32_to_cpu(rule->tuples_mask.dst_ip,
			      fs->m_u.tcp_ip6_spec.ip6dst);

	rule->tuples.src_port = be16_to_cpu(fs->h_u.tcp_ip6_spec.psrc);
	rule->tuples_mask.src_port = be16_to_cpu(fs->m_u.tcp_ip6_spec.psrc);

	rule->tuples.dst_port = be16_to_cpu(fs->h_u.tcp_ip6_spec.pdst);
	rule->tuples_mask.dst_port = be16_to_cpu(fs->m_u.tcp_ip6_spec.pdst);

	rule->tuples.ether_proto = ETH_P_IPV6;
	rule->tuples_mask.ether_proto = 0xFFFF;

	rule->tuples.ip_tos = fs->h_u.tcp_ip6_spec.tclass;
	rule->tuples_mask.ip_tos = fs->m_u.tcp_ip6_spec.tclass;

	rule->tuples.ip_proto = ip_proto;
	rule->tuples_mask.ip_proto = 0xFF;
}

static void hclge_fd_get_ip6_tuple(struct ethtool_rx_flow_spec *fs,
				   struct hclge_fd_rule *rule)
{
	ipv6_addr_be32_to_cpu(rule->tuples.src_ip,
			      fs->h_u.usr_ip6_spec.ip6src);
	ipv6_addr_be32_to_cpu(rule->tuples_mask.src_ip,
			      fs->m_u.usr_ip6_spec.ip6src);

	ipv6_addr_be32_to_cpu(rule->tuples.dst_ip,
			      fs->h_u.usr_ip6_spec.ip6dst);
	ipv6_addr_be32_to_cpu(rule->tuples_mask.dst_ip,
			      fs->m_u.usr_ip6_spec.ip6dst);

	rule->tuples.ip_proto = fs->h_u.usr_ip6_spec.l4_proto;
	rule->tuples_mask.ip_proto = fs->m_u.usr_ip6_spec.l4_proto;

	rule->tuples.ip_tos = fs->h_u.tcp_ip6_spec.tclass;
	rule->tuples_mask.ip_tos = fs->m_u.tcp_ip6_spec.tclass;

	rule->tuples.ether_proto = ETH_P_IPV6;
	rule->tuples_mask.ether_proto = 0xFFFF;
}

static void hclge_fd_get_ether_tuple(struct ethtool_rx_flow_spec *fs,
				     struct hclge_fd_rule *rule)
{
	ether_addr_copy(rule->tuples.src_mac, fs->h_u.ether_spec.h_source);
	ether_addr_copy(rule->tuples_mask.src_mac, fs->m_u.ether_spec.h_source);

	ether_addr_copy(rule->tuples.dst_mac, fs->h_u.ether_spec.h_dest);
	ether_addr_copy(rule->tuples_mask.dst_mac, fs->m_u.ether_spec.h_dest);

	rule->tuples.ether_proto = be16_to_cpu(fs->h_u.ether_spec.h_proto);
	rule->tuples_mask.ether_proto = be16_to_cpu(fs->m_u.ether_spec.h_proto);
}

static void hclge_fd_get_user_def_tuple(struct hclge_fd_user_def_info *info,
					struct hclge_fd_rule *rule)
{
	switch (info->layer) {
	case HCLGE_FD_USER_DEF_L2:
		rule->tuples.l2_user_def = info->data;
		rule->tuples_mask.l2_user_def = info->data_mask;
		break;
	case HCLGE_FD_USER_DEF_L3:
		rule->tuples.l3_user_def = info->data;
		rule->tuples_mask.l3_user_def = info->data_mask;
		break;
	case HCLGE_FD_USER_DEF_L4:
		rule->tuples.l4_user_def = (u32)info->data << 16;
		rule->tuples_mask.l4_user_def = (u32)info->data_mask << 16;
		break;
	default:
		break;
	}

	rule->ep.user_def = *info;
}

static int hclge_fd_get_tuple(struct ethtool_rx_flow_spec *fs,
			      struct hclge_fd_rule *rule,
			      struct hclge_fd_user_def_info *info)
{
	u32 flow_type = fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT);

	switch (flow_type) {
	case SCTP_V4_FLOW:
		hclge_fd_get_tcpip4_tuple(fs, rule, IPPROTO_SCTP);
		break;
	case TCP_V4_FLOW:
		hclge_fd_get_tcpip4_tuple(fs, rule, IPPROTO_TCP);
		break;
	case UDP_V4_FLOW:
		hclge_fd_get_tcpip4_tuple(fs, rule, IPPROTO_UDP);
		break;
	case IP_USER_FLOW:
		hclge_fd_get_ip4_tuple(fs, rule);
		break;
	case SCTP_V6_FLOW:
		hclge_fd_get_tcpip6_tuple(fs, rule, IPPROTO_SCTP);
		break;
	case TCP_V6_FLOW:
		hclge_fd_get_tcpip6_tuple(fs, rule, IPPROTO_TCP);
		break;
	case UDP_V6_FLOW:
		hclge_fd_get_tcpip6_tuple(fs, rule, IPPROTO_UDP);
		break;
	case IPV6_USER_FLOW:
		hclge_fd_get_ip6_tuple(fs, rule);
		break;
	case ETHER_FLOW:
		hclge_fd_get_ether_tuple(fs, rule);
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (fs->flow_type & FLOW_EXT) {
		rule->tuples.vlan_tag1 = be16_to_cpu(fs->h_ext.vlan_tci);
		rule->tuples_mask.vlan_tag1 = be16_to_cpu(fs->m_ext.vlan_tci);
		hclge_fd_get_user_def_tuple(info, rule);
	}

	if (fs->flow_type & FLOW_MAC_EXT) {
		ether_addr_copy(rule->tuples.dst_mac, fs->h_ext.h_dest);
		ether_addr_copy(rule->tuples_mask.dst_mac, fs->m_ext.h_dest);
	}

	return 0;
}

static int hclge_fd_config_rule(struct hclge_dev *hdev,
				struct hclge_fd_rule *rule)
{
	int ret;

	ret = hclge_config_action(hdev, HCLGE_FD_STAGE_1, rule);
	if (ret)
		return ret;

	return hclge_config_key(hdev, HCLGE_FD_STAGE_1, rule);
}

static int hclge_add_fd_entry_common(struct hclge_dev *hdev,
				     struct hclge_fd_rule *rule)
{
	int ret;

	spin_lock_bh(&hdev->fd_rule_lock);

	if (hdev->fd_active_type != rule->rule_type &&
	    (hdev->fd_active_type == HCLGE_FD_TC_FLOWER_ACTIVE ||
	     hdev->fd_active_type == HCLGE_FD_EP_ACTIVE)) {
		dev_err(&hdev->pdev->dev,
			"mode conflict(new type %d, active type %d), please delete existent rules first\n",
			rule->rule_type, hdev->fd_active_type);
		spin_unlock_bh(&hdev->fd_rule_lock);
		return -EINVAL;
	}

	ret = hclge_fd_check_user_def_refcnt(hdev, rule);
	if (ret)
		goto out;

	ret = hclge_clear_arfs_rules(hdev);
	if (ret)
		goto out;

	ret = hclge_fd_config_rule(hdev, rule);
	if (ret)
		goto out;

	rule->state = HCLGE_FD_ACTIVE;
	hdev->fd_active_type = rule->rule_type;
	hclge_update_fd_list(hdev, rule->state, rule->location, rule);

out:
	spin_unlock_bh(&hdev->fd_rule_lock);
	return ret;
}

bool hclge_is_cls_flower_active(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	return hdev->fd_active_type == HCLGE_FD_TC_FLOWER_ACTIVE;
}

static int hclge_fd_parse_ring_cookie(struct hclge_dev *hdev, u64 ring_cookie,
				      u16 *vport_id, u8 *action, u16 *queue_id)
{
	struct hclge_vport *vport = hdev->vport;

	if (ring_cookie == RX_CLS_FLOW_DISC) {
		*action = HCLGE_FD_ACTION_DROP_PACKET;
	} else {
		u32 ring = ethtool_get_flow_spec_ring(ring_cookie);
		u8 vf = ethtool_get_flow_spec_ring_vf(ring_cookie);
		u16 tqps;

		/* To keep consistent with user's configuration, minus 1 when
		 * printing 'vf', because vf id from ethtool is added 1 for vf.
		 */
		if (vf > hdev->num_req_vfs) {
			dev_err(&hdev->pdev->dev,
				"Error: vf id (%u) should be less than %u\n",
				vf - 1U, hdev->num_req_vfs);
			return -EINVAL;
		}

		*vport_id = vf ? hdev->vport[vf].vport_id : vport->vport_id;
		tqps = hdev->vport[vf].nic.kinfo.num_tqps;

		if (ring >= tqps) {
			dev_err(&hdev->pdev->dev,
				"Error: queue id (%u) > max tqp num (%u)\n",
				ring, tqps - 1U);
			return -EINVAL;
		}

		*action = HCLGE_FD_ACTION_SELECT_QUEUE;
		*queue_id = ring;
	}

	return 0;
}

int hclge_add_fd_entry(struct hnae3_handle *handle, struct ethtool_rxnfc *cmd)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_user_def_info info;
	u16 dst_vport_id = 0, q_index = 0;
	struct ethtool_rx_flow_spec *fs;
	struct hclge_fd_rule *rule;
	u32 unused = 0;
	u8 action;
	int ret;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev)) {
		dev_err(&hdev->pdev->dev,
			"flow table director is not supported\n");
		return -EOPNOTSUPP;
	}

	if (!hdev->fd_en) {
		dev_err(&hdev->pdev->dev,
			"please enable flow director first\n");
		return -EOPNOTSUPP;
	}

	fs = (struct ethtool_rx_flow_spec *)&cmd->fs;

	ret = hclge_fd_check_spec(hdev, fs, &unused, &info);
	if (ret)
		return ret;

	ret = hclge_fd_parse_ring_cookie(hdev, fs->ring_cookie, &dst_vport_id,
					 &action, &q_index);
	if (ret)
		return ret;

	rule = kzalloc_obj(*rule);
	if (!rule)
		return -ENOMEM;

	ret = hclge_fd_get_tuple(fs, rule, &info);
	if (ret) {
		kfree(rule);
		return ret;
	}

	rule->flow_type = fs->flow_type;
	rule->location = fs->location;
	rule->unused_tuple = unused;
	rule->vf_id = dst_vport_id;
	rule->queue_id = q_index;
	rule->action = action;
	rule->rule_type = HCLGE_FD_EP_ACTIVE;

	ret = hclge_add_fd_entry_common(hdev, rule);
	if (ret)
		kfree(rule);

	return ret;
}

int hclge_del_fd_entry(struct hnae3_handle *handle, struct ethtool_rxnfc *cmd)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct ethtool_rx_flow_spec *fs;
	int ret;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return -EOPNOTSUPP;

	fs = (struct ethtool_rx_flow_spec *)&cmd->fs;

	if (fs->location >= hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1])
		return -EINVAL;

	spin_lock_bh(&hdev->fd_rule_lock);
	if (hdev->fd_active_type == HCLGE_FD_TC_FLOWER_ACTIVE ||
	    !test_bit(fs->location, hdev->fd_bmap)) {
		dev_err(&hdev->pdev->dev,
			"Delete fail, rule %u is inexistent\n", fs->location);
		spin_unlock_bh(&hdev->fd_rule_lock);
		return -ENOENT;
	}

	ret = hclge_fd_tcam_config(hdev, HCLGE_FD_STAGE_1, true, fs->location,
				   NULL, false);
	if (ret)
		goto out;

	hclge_update_fd_list(hdev, HCLGE_FD_DELETED, fs->location, NULL);

out:
	spin_unlock_bh(&hdev->fd_rule_lock);
	return ret;
}

static void hclge_clear_fd_rules_in_list(struct hclge_dev *hdev,
					 bool clear_list)
{
	struct hclge_fd_rule *rule;
	struct hlist_node *node;
	u16 location;

	spin_lock_bh(&hdev->fd_rule_lock);

	for_each_set_bit(location, hdev->fd_bmap,
			 hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1])
		hclge_fd_tcam_config(hdev, HCLGE_FD_STAGE_1, true, location,
				     NULL, false);

	if (clear_list) {
		hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list,
					  rule_node) {
			hlist_del(&rule->rule_node);
			kfree(rule);
		}
		hdev->fd_active_type = HCLGE_FD_RULE_NONE;
		hdev->hclge_fd_rule_num = 0;
		bitmap_zero(hdev->fd_bmap,
			    hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1]);
	}

	spin_unlock_bh(&hdev->fd_rule_lock);
}

void hclge_del_all_fd_entries(struct hclge_dev *hdev)
{
	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return;

	hclge_clear_fd_rules_in_list(hdev, true);
	hclge_fd_disable_user_def(hdev);
}

int hclge_restore_fd_entries(struct hnae3_handle *handle)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_rule *rule;
	struct hlist_node *node;

	/* Return ok here, because reset error handling will check this
	 * return value. If error is returned here, the reset process will
	 * fail.
	 */
	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return 0;

	/* if fd is disabled, should not restore it when reset */
	if (!hdev->fd_en)
		return 0;

	spin_lock_bh(&hdev->fd_rule_lock);
	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		if (rule->state == HCLGE_FD_ACTIVE)
			rule->state = HCLGE_FD_TO_ADD;
	}
	spin_unlock_bh(&hdev->fd_rule_lock);
	set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);

	return 0;
}

int hclge_get_fd_rule_cnt(struct hnae3_handle *handle,
			  struct ethtool_rxnfc *cmd)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev) ||
	    hclge_is_cls_flower_active(handle))
		return -EOPNOTSUPP;

	cmd->rule_cnt = hdev->hclge_fd_rule_num;
	cmd->data = hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1];

	return 0;
}

static void hclge_fd_get_tcpip4_info(struct hclge_fd_rule *rule,
				     struct ethtool_tcpip4_spec *spec,
				     struct ethtool_tcpip4_spec *spec_mask)
{
	spec->ip4src = cpu_to_be32(rule->tuples.src_ip[IPV4_INDEX]);
	spec_mask->ip4src = rule->unused_tuple & BIT(INNER_SRC_IP) ?
			0 : cpu_to_be32(rule->tuples_mask.src_ip[IPV4_INDEX]);

	spec->ip4dst = cpu_to_be32(rule->tuples.dst_ip[IPV4_INDEX]);
	spec_mask->ip4dst = rule->unused_tuple & BIT(INNER_DST_IP) ?
			0 : cpu_to_be32(rule->tuples_mask.dst_ip[IPV4_INDEX]);

	spec->psrc = cpu_to_be16(rule->tuples.src_port);
	spec_mask->psrc = rule->unused_tuple & BIT(INNER_SRC_PORT) ?
			0 : cpu_to_be16(rule->tuples_mask.src_port);

	spec->pdst = cpu_to_be16(rule->tuples.dst_port);
	spec_mask->pdst = rule->unused_tuple & BIT(INNER_DST_PORT) ?
			0 : cpu_to_be16(rule->tuples_mask.dst_port);

	spec->tos = rule->tuples.ip_tos;
	spec_mask->tos = rule->unused_tuple & BIT(INNER_IP_TOS) ?
			0 : rule->tuples_mask.ip_tos;
}

static void hclge_fd_get_ip4_info(struct hclge_fd_rule *rule,
				  struct ethtool_usrip4_spec *spec,
				  struct ethtool_usrip4_spec *spec_mask)
{
	spec->ip4src = cpu_to_be32(rule->tuples.src_ip[IPV4_INDEX]);
	spec_mask->ip4src = rule->unused_tuple & BIT(INNER_SRC_IP) ?
			0 : cpu_to_be32(rule->tuples_mask.src_ip[IPV4_INDEX]);

	spec->ip4dst = cpu_to_be32(rule->tuples.dst_ip[IPV4_INDEX]);
	spec_mask->ip4dst = rule->unused_tuple & BIT(INNER_DST_IP) ?
			0 : cpu_to_be32(rule->tuples_mask.dst_ip[IPV4_INDEX]);

	spec->tos = rule->tuples.ip_tos;
	spec_mask->tos = rule->unused_tuple & BIT(INNER_IP_TOS) ?
			0 : rule->tuples_mask.ip_tos;

	spec->proto = rule->tuples.ip_proto;
	spec_mask->proto = rule->unused_tuple & BIT(INNER_IP_PROTO) ?
			0 : rule->tuples_mask.ip_proto;

	spec->ip_ver = ETH_RX_NFC_IP4;
}

static void hclge_fd_get_tcpip6_info(struct hclge_fd_rule *rule,
				     struct ethtool_tcpip6_spec *spec,
				     struct ethtool_tcpip6_spec *spec_mask)
{
	ipv6_addr_cpu_to_be32(spec->ip6src, rule->tuples.src_ip);
	ipv6_addr_cpu_to_be32(spec->ip6dst, rule->tuples.dst_ip);
	if (rule->unused_tuple & BIT(INNER_SRC_IP))
		memset(spec_mask->ip6src, 0, sizeof(spec_mask->ip6src));
	else
		ipv6_addr_cpu_to_be32(spec_mask->ip6src,
				      rule->tuples_mask.src_ip);

	if (rule->unused_tuple & BIT(INNER_DST_IP))
		memset(spec_mask->ip6dst, 0, sizeof(spec_mask->ip6dst));
	else
		ipv6_addr_cpu_to_be32(spec_mask->ip6dst,
				      rule->tuples_mask.dst_ip);

	spec->tclass = rule->tuples.ip_tos;
	spec_mask->tclass = rule->unused_tuple & BIT(INNER_IP_TOS) ?
			0 : rule->tuples_mask.ip_tos;

	spec->psrc = cpu_to_be16(rule->tuples.src_port);
	spec_mask->psrc = rule->unused_tuple & BIT(INNER_SRC_PORT) ?
			0 : cpu_to_be16(rule->tuples_mask.src_port);

	spec->pdst = cpu_to_be16(rule->tuples.dst_port);
	spec_mask->pdst = rule->unused_tuple & BIT(INNER_DST_PORT) ?
			0 : cpu_to_be16(rule->tuples_mask.dst_port);
}

static void hclge_fd_get_ip6_info(struct hclge_fd_rule *rule,
				  struct ethtool_usrip6_spec *spec,
				  struct ethtool_usrip6_spec *spec_mask)
{
	ipv6_addr_cpu_to_be32(spec->ip6src, rule->tuples.src_ip);
	ipv6_addr_cpu_to_be32(spec->ip6dst, rule->tuples.dst_ip);
	if (rule->unused_tuple & BIT(INNER_SRC_IP))
		memset(spec_mask->ip6src, 0, sizeof(spec_mask->ip6src));
	else
		ipv6_addr_cpu_to_be32(spec_mask->ip6src,
				      rule->tuples_mask.src_ip);

	if (rule->unused_tuple & BIT(INNER_DST_IP))
		memset(spec_mask->ip6dst, 0, sizeof(spec_mask->ip6dst));
	else
		ipv6_addr_cpu_to_be32(spec_mask->ip6dst,
				      rule->tuples_mask.dst_ip);

	spec->tclass = rule->tuples.ip_tos;
	spec_mask->tclass = rule->unused_tuple & BIT(INNER_IP_TOS) ?
			0 : rule->tuples_mask.ip_tos;

	spec->l4_proto = rule->tuples.ip_proto;
	spec_mask->l4_proto = rule->unused_tuple & BIT(INNER_IP_PROTO) ?
			0 : rule->tuples_mask.ip_proto;
}

static void hclge_fd_get_ether_info(struct hclge_fd_rule *rule,
				    struct ethhdr *spec,
				    struct ethhdr *spec_mask)
{
	ether_addr_copy(spec->h_source, rule->tuples.src_mac);
	ether_addr_copy(spec->h_dest, rule->tuples.dst_mac);

	if (rule->unused_tuple & BIT(INNER_SRC_MAC))
		eth_zero_addr(spec_mask->h_source);
	else
		ether_addr_copy(spec_mask->h_source, rule->tuples_mask.src_mac);

	if (rule->unused_tuple & BIT(INNER_DST_MAC))
		eth_zero_addr(spec_mask->h_dest);
	else
		ether_addr_copy(spec_mask->h_dest, rule->tuples_mask.dst_mac);

	spec->h_proto = cpu_to_be16(rule->tuples.ether_proto);
	spec_mask->h_proto = rule->unused_tuple & BIT(INNER_ETH_TYPE) ?
			0 : cpu_to_be16(rule->tuples_mask.ether_proto);
}

static void hclge_fd_get_user_def_info(struct ethtool_rx_flow_spec *fs,
				       struct hclge_fd_rule *rule)
{
	if ((rule->unused_tuple & HCLGE_FD_TUPLE_USER_DEF_TUPLES) ==
	    HCLGE_FD_TUPLE_USER_DEF_TUPLES) {
		fs->h_ext.data[0] = 0;
		fs->h_ext.data[1] = 0;
		fs->m_ext.data[0] = 0;
		fs->m_ext.data[1] = 0;
	} else {
		fs->h_ext.data[0] = cpu_to_be32(rule->ep.user_def.offset);
		fs->h_ext.data[1] = cpu_to_be32(rule->ep.user_def.data);
		fs->m_ext.data[0] =
				cpu_to_be32(HCLGE_FD_USER_DEF_OFFSET_UNMASK);
		fs->m_ext.data[1] = cpu_to_be32(rule->ep.user_def.data_mask);
	}
}

static void hclge_fd_get_ext_info(struct ethtool_rx_flow_spec *fs,
				  struct hclge_fd_rule *rule)
{
	if (fs->flow_type & FLOW_EXT) {
		fs->h_ext.vlan_tci = cpu_to_be16(rule->tuples.vlan_tag1);
		fs->m_ext.vlan_tci =
				rule->unused_tuple & BIT(INNER_VLAN_TAG_FST) ?
				0 : cpu_to_be16(rule->tuples_mask.vlan_tag1);

		hclge_fd_get_user_def_info(fs, rule);
	}

	if (fs->flow_type & FLOW_MAC_EXT) {
		ether_addr_copy(fs->h_ext.h_dest, rule->tuples.dst_mac);
		if (rule->unused_tuple & BIT(INNER_DST_MAC))
			eth_zero_addr(fs->m_u.ether_spec.h_dest);
		else
			ether_addr_copy(fs->m_u.ether_spec.h_dest,
					rule->tuples_mask.dst_mac);
	}
}

static struct hclge_fd_rule *hclge_get_fd_rule(struct hclge_dev *hdev,
					       u16 location)
{
	struct hclge_fd_rule *rule = NULL;
	struct hlist_node *node2;

	hlist_for_each_entry_safe(rule, node2, &hdev->fd_rule_list, rule_node) {
		if (rule->location == location)
			return rule;
		else if (rule->location > location)
			return NULL;
	}

	return NULL;
}

static void hclge_fd_get_ring_cookie(struct ethtool_rx_flow_spec *fs,
				     struct hclge_fd_rule *rule)
{
	if (rule->action == HCLGE_FD_ACTION_DROP_PACKET) {
		fs->ring_cookie = RX_CLS_FLOW_DISC;
	} else {
		u64 vf_id;

		fs->ring_cookie = rule->queue_id;
		vf_id = rule->vf_id;
		vf_id <<= ETHTOOL_RX_FLOW_SPEC_RING_VF_OFF;
		fs->ring_cookie |= vf_id;
	}
}

int hclge_get_fd_rule_info(struct hnae3_handle *handle,
			   struct ethtool_rxnfc *cmd)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_fd_rule *rule = NULL;
	struct hclge_dev *hdev = vport->back;
	struct ethtool_rx_flow_spec *fs;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return -EOPNOTSUPP;

	fs = (struct ethtool_rx_flow_spec *)&cmd->fs;

	spin_lock_bh(&hdev->fd_rule_lock);

	rule = hclge_get_fd_rule(hdev, fs->location);
	if (!rule) {
		spin_unlock_bh(&hdev->fd_rule_lock);
		return -ENOENT;
	}

	fs->flow_type = rule->flow_type;
	switch (fs->flow_type & ~(FLOW_EXT | FLOW_MAC_EXT)) {
	case SCTP_V4_FLOW:
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
		hclge_fd_get_tcpip4_info(rule, &fs->h_u.tcp_ip4_spec,
					 &fs->m_u.tcp_ip4_spec);
		break;
	case IP_USER_FLOW:
		hclge_fd_get_ip4_info(rule, &fs->h_u.usr_ip4_spec,
				      &fs->m_u.usr_ip4_spec);
		break;
	case SCTP_V6_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		hclge_fd_get_tcpip6_info(rule, &fs->h_u.tcp_ip6_spec,
					 &fs->m_u.tcp_ip6_spec);
		break;
	case IPV6_USER_FLOW:
		hclge_fd_get_ip6_info(rule, &fs->h_u.usr_ip6_spec,
				      &fs->m_u.usr_ip6_spec);
		break;
	/* The flow type of fd rule has been checked before adding in to rule
	 * list. As other flow types have been handled, it must be ETHER_FLOW
	 * for the default case
	 */
	default:
		hclge_fd_get_ether_info(rule, &fs->h_u.ether_spec,
					&fs->m_u.ether_spec);
		break;
	}

	hclge_fd_get_ext_info(fs, rule);

	hclge_fd_get_ring_cookie(fs, rule);

	spin_unlock_bh(&hdev->fd_rule_lock);

	return 0;
}

int hclge_get_all_rules(struct hnae3_handle *handle,
			struct ethtool_rxnfc *cmd, u32 *rule_locs)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_rule *rule;
	struct hlist_node *node2;
	u32 cnt = 0;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return -EOPNOTSUPP;

	cmd->data = hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1];

	spin_lock_bh(&hdev->fd_rule_lock);
	hlist_for_each_entry_safe(rule, node2,
				  &hdev->fd_rule_list, rule_node) {
		if (cnt == cmd->rule_cnt) {
			spin_unlock_bh(&hdev->fd_rule_lock);
			return -EMSGSIZE;
		}

		if (rule->state == HCLGE_FD_TO_DEL)
			continue;

		rule_locs[cnt] = rule->location;
		cnt++;
	}

	spin_unlock_bh(&hdev->fd_rule_lock);

	cmd->rule_cnt = cnt;

	return 0;
}

static void hclge_fd_get_flow_tuples(const struct flow_keys *fkeys,
				     struct hclge_fd_rule_tuples *tuples)
{
#define flow_ip6_src fkeys->addrs.v6addrs.src.in6_u.u6_addr32
#define flow_ip6_dst fkeys->addrs.v6addrs.dst.in6_u.u6_addr32

	tuples->ether_proto = be16_to_cpu(fkeys->basic.n_proto);
	tuples->ip_proto = fkeys->basic.ip_proto;
	tuples->dst_port = be16_to_cpu(fkeys->ports.dst);

	if (fkeys->basic.n_proto == htons(ETH_P_IP)) {
		tuples->src_ip[3] = be32_to_cpu(fkeys->addrs.v4addrs.src);
		tuples->dst_ip[3] = be32_to_cpu(fkeys->addrs.v4addrs.dst);
	} else {
		int i;

		for (i = 0; i < IPV6_ADDR_WORDS; i++) {
			tuples->src_ip[i] = be32_to_cpu(flow_ip6_src[i]);
			tuples->dst_ip[i] = be32_to_cpu(flow_ip6_dst[i]);
		}
	}
}

/* traverse all rules, check whether an existed rule has the same tuples */
static struct hclge_fd_rule *
hclge_fd_search_flow_keys(struct hclge_dev *hdev,
			  const struct hclge_fd_rule_tuples *tuples)
{
	struct hclge_fd_rule *rule = NULL;
	struct hlist_node *node;

	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		if (!memcmp(tuples, &rule->tuples, sizeof(*tuples)))
			return rule;
	}

	return NULL;
}

static void hclge_fd_build_arfs_rule(const struct hclge_fd_rule_tuples *tuples,
				     struct hclge_fd_rule *rule)
{
	rule->unused_tuple = BIT(INNER_SRC_MAC) | BIT(INNER_DST_MAC) |
			     BIT(INNER_VLAN_TAG_FST) | BIT(INNER_IP_TOS) |
			     BIT(INNER_SRC_PORT);
	rule->action = 0;
	rule->vf_id = 0;
	rule->rule_type = HCLGE_FD_ARFS_ACTIVE;
	rule->state = HCLGE_FD_TO_ADD;
	if (tuples->ether_proto == ETH_P_IP) {
		if (tuples->ip_proto == IPPROTO_TCP)
			rule->flow_type = TCP_V4_FLOW;
		else
			rule->flow_type = UDP_V4_FLOW;
	} else {
		if (tuples->ip_proto == IPPROTO_TCP)
			rule->flow_type = TCP_V6_FLOW;
		else
			rule->flow_type = UDP_V6_FLOW;
	}
	memcpy(&rule->tuples, tuples, sizeof(rule->tuples));
	memset(&rule->tuples_mask, 0xFF, sizeof(rule->tuples_mask));
}

int hclge_add_fd_entry_by_arfs(struct hnae3_handle *handle, u16 queue_id,
			       u16 flow_id, struct flow_keys *fkeys)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_fd_rule_tuples new_tuples = {};
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_rule *rule;
	u16 bit_id;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return -EOPNOTSUPP;

	/* when there is already fd rule existed add by user,
	 * arfs should not work
	 */
	spin_lock_bh(&hdev->fd_rule_lock);
	if (hdev->fd_active_type != HCLGE_FD_ARFS_ACTIVE &&
	    hdev->fd_active_type != HCLGE_FD_RULE_NONE) {
		spin_unlock_bh(&hdev->fd_rule_lock);
		return -EOPNOTSUPP;
	}

	hclge_fd_get_flow_tuples(fkeys, &new_tuples);

	/* check is there flow director filter existed for this flow,
	 * if not, create a new filter for it;
	 * if filter exist with different queue id, modify the filter;
	 * if filter exist with same queue id, do nothing
	 */
	rule = hclge_fd_search_flow_keys(hdev, &new_tuples);
	if (!rule) {
		bit_id = find_first_zero_bit(hdev->fd_bmap, MAX_FD_FILTER_NUM);
		if (bit_id >= hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1]) {
			spin_unlock_bh(&hdev->fd_rule_lock);
			return -ENOSPC;
		}

		rule = kzalloc_obj(*rule, GFP_ATOMIC);
		if (!rule) {
			spin_unlock_bh(&hdev->fd_rule_lock);
			return -ENOMEM;
		}

		rule->location = bit_id;
		rule->arfs.flow_id = flow_id;
		rule->queue_id = queue_id;
		hclge_fd_build_arfs_rule(&new_tuples, rule);
		hclge_update_fd_list(hdev, rule->state, rule->location, rule);
		hdev->fd_active_type = HCLGE_FD_ARFS_ACTIVE;
	} else if (rule->queue_id != queue_id) {
		rule->queue_id = queue_id;
		rule->state = HCLGE_FD_TO_ADD;
		set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);
		hclge_task_schedule(hdev, 0);
	}
	spin_unlock_bh(&hdev->fd_rule_lock);
	return rule->location;
}

void hclge_rfs_filter_expire(struct hclge_dev *hdev)
{
#ifdef CONFIG_RFS_ACCEL
	struct hnae3_handle *handle = &hdev->vport[0].nic;
	struct hclge_fd_rule *rule;
	struct hlist_node *node;

	spin_lock_bh(&hdev->fd_rule_lock);
	if (hdev->fd_active_type != HCLGE_FD_ARFS_ACTIVE) {
		spin_unlock_bh(&hdev->fd_rule_lock);
		return;
	}
	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		if (rule->state != HCLGE_FD_ACTIVE)
			continue;
		if (rps_may_expire_flow(handle->netdev, rule->queue_id,
					rule->arfs.flow_id, rule->location)) {
			rule->state = HCLGE_FD_TO_DEL;
			set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);
		}
	}
	spin_unlock_bh(&hdev->fd_rule_lock);
#endif
}

/* make sure being called after lock up with fd_rule_lock */
int hclge_clear_arfs_rules(struct hclge_dev *hdev)
{
#ifdef CONFIG_RFS_ACCEL
	struct hclge_fd_rule *rule;
	struct hlist_node *node;
	int ret;

	if (hdev->fd_active_type != HCLGE_FD_ARFS_ACTIVE)
		return 0;

	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		switch (rule->state) {
		case HCLGE_FD_TO_DEL:
		case HCLGE_FD_ACTIVE:
			ret = hclge_fd_tcam_config(hdev, HCLGE_FD_STAGE_1, true,
						   rule->location, NULL, false);
			if (ret)
				return ret;
			fallthrough;
		case HCLGE_FD_TO_ADD:
			hclge_fd_dec_rule_cnt(hdev, rule->location);
			hlist_del(&rule->rule_node);
			kfree(rule);
			break;
		default:
			break;
		}
	}
	hclge_sync_fd_state(hdev);

#endif
	return 0;
}

static void hclge_get_cls_key_basic(const struct flow_rule *flow,
				    struct hclge_fd_rule *rule)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_BASIC)) {
		struct flow_match_basic match;
		u16 ethtype_key, ethtype_mask;

		flow_rule_match_basic(flow, &match);
		ethtype_key = ntohs(match.key->n_proto);
		ethtype_mask = ntohs(match.mask->n_proto);

		if (ethtype_key == ETH_P_ALL) {
			ethtype_key = 0;
			ethtype_mask = 0;
		}
		rule->tuples.ether_proto = ethtype_key;
		rule->tuples_mask.ether_proto = ethtype_mask;
		rule->tuples.ip_proto = match.key->ip_proto;
		rule->tuples_mask.ip_proto = match.mask->ip_proto;
	} else {
		rule->unused_tuple |= BIT(INNER_IP_PROTO);
		rule->unused_tuple |= BIT(INNER_ETH_TYPE);
	}
}

static void hclge_get_cls_key_mac(const struct flow_rule *flow,
				  struct hclge_fd_rule *rule)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_ETH_ADDRS)) {
		struct flow_match_eth_addrs match;

		flow_rule_match_eth_addrs(flow, &match);
		ether_addr_copy(rule->tuples.dst_mac, match.key->dst);
		ether_addr_copy(rule->tuples_mask.dst_mac, match.mask->dst);
		ether_addr_copy(rule->tuples.src_mac, match.key->src);
		ether_addr_copy(rule->tuples_mask.src_mac, match.mask->src);
		if (is_zero_ether_addr(match.mask->dst))
			rule->unused_tuple |= BIT(INNER_DST_MAC);
		if (is_zero_ether_addr(match.mask->src))
			rule->unused_tuple |= BIT(INNER_SRC_MAC);
	} else {
		rule->unused_tuple |= BIT(INNER_DST_MAC);
		rule->unused_tuple |= BIT(INNER_SRC_MAC);
	}
}

static void hclge_get_cls_key_vlan(const struct flow_rule *flow,
				   struct hclge_fd_rule *rule)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(flow, &match);
		rule->tuples.vlan_tag1 = match.key->vlan_id |
				(match.key->vlan_priority << VLAN_PRIO_SHIFT);
		rule->tuples_mask.vlan_tag1 = match.mask->vlan_id |
				(match.mask->vlan_priority << VLAN_PRIO_SHIFT);
	} else {
		rule->unused_tuple |= BIT(INNER_VLAN_TAG_FST);
	}
}

static int hclge_get_cls_key_ip(const struct flow_rule *flow,
				struct hclge_fd_rule *rule,
				struct netlink_ext_ack *extack)
{
	u16 addr_type = 0;

	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_CONTROL)) {
		struct flow_match_control match;

		flow_rule_match_control(flow, &match);
		addr_type = match.key->addr_type;

		if (flow_rule_has_control_flags(match.mask->flags, extack))
			return -EOPNOTSUPP;
	}

	if (addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		struct flow_match_ipv4_addrs match;

		flow_rule_match_ipv4_addrs(flow, &match);
		rule->tuples.src_ip[IPV4_INDEX] = be32_to_cpu(match.key->src);
		rule->tuples_mask.src_ip[IPV4_INDEX] =
						be32_to_cpu(match.mask->src);
		rule->tuples.dst_ip[IPV4_INDEX] = be32_to_cpu(match.key->dst);
		rule->tuples_mask.dst_ip[IPV4_INDEX] =
						be32_to_cpu(match.mask->dst);
		if (!match.mask->src)
			rule->unused_tuple |= BIT(INNER_SRC_IP);
		if (!match.mask->dst)
			rule->unused_tuple |= BIT(INNER_DST_IP);
	} else if (addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		struct flow_match_ipv6_addrs match;

		flow_rule_match_ipv6_addrs(flow, &match);
		ipv6_addr_be32_to_cpu(rule->tuples.src_ip,
				      match.key->src.s6_addr32);
		ipv6_addr_be32_to_cpu(rule->tuples_mask.src_ip,
				      match.mask->src.s6_addr32);
		ipv6_addr_be32_to_cpu(rule->tuples.dst_ip,
				      match.key->dst.s6_addr32);
		ipv6_addr_be32_to_cpu(rule->tuples_mask.dst_ip,
				      match.mask->dst.s6_addr32);
		if (ipv6_addr_any(&match.mask->src))
			rule->unused_tuple |= BIT(INNER_SRC_IP);
		if (ipv6_addr_any(&match.mask->dst))
			rule->unused_tuple |= BIT(INNER_DST_IP);
	} else {
		rule->unused_tuple |= BIT(INNER_SRC_IP);
		rule->unused_tuple |= BIT(INNER_DST_IP);
	}

	return 0;
}

static void hclge_get_cls_key_port(const struct flow_rule *flow,
				   struct hclge_fd_rule *rule)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(flow, &match);

		rule->tuples.src_port = be16_to_cpu(match.key->src);
		rule->tuples_mask.src_port = be16_to_cpu(match.mask->src);
		rule->tuples.dst_port = be16_to_cpu(match.key->dst);
		rule->tuples_mask.dst_port = be16_to_cpu(match.mask->dst);
	} else {
		rule->unused_tuple |= BIT(INNER_SRC_PORT);
		rule->unused_tuple |= BIT(INNER_DST_PORT);
	}
}

static int hclge_get_cls_enc_keyid(struct hclge_dev *hdev,
				   const struct flow_rule *flow,
				   struct hclge_fd_rule *rule,
				   struct netlink_ext_ack *extack)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_ENC_KEYID)) {
		struct flow_match_enc_keyid match;

		flow_rule_match_enc_keyid(flow, &match);

		/* vni is only 24 bits and must be greater than 0,
		 * and it can not be masked.
		 */
		if (be32_to_cpu(match.mask->keyid) !=
		    HCLGE_FD_VXLAN_VNI_UNMASK ||
		    be32_to_cpu(match.key->keyid) >= VXLAN_N_VID ||
		    !match.key->keyid) {
			NL_SET_ERR_MSG_MOD(extack, "invalid enc_keyid");
			return -EINVAL;
		}

		rule->tuples.outer_tun_vni = be32_to_cpu(match.key->keyid);
		rule->tuples_mask.outer_tun_vni =
						be32_to_cpu(match.mask->keyid);
	} else {
		rule->unused_tuple |= BIT(OUTER_TUN_VNI);
	}

	return 0;
}

static int hclge_get_cls_key_ip_tos(const struct flow_rule *flow,
				    struct hclge_fd_rule *rule,
				    struct netlink_ext_ack *extack)
{
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_IP)) {
		struct flow_match_ip match;

		flow_rule_match_ip(flow, &match);

		if (match.mask->ttl) {
			NL_SET_ERR_MSG_MOD(extack, "unsupported TTL");
			return -EOPNOTSUPP;
		}

		rule->tuples.ip_tos = match.key->tos;
		rule->tuples_mask.ip_tos = match.mask->tos;
		if (!rule->tuples_mask.ip_tos)
			rule->unused_tuple |= BIT(INNER_IP_TOS);
	} else {
		rule->unused_tuple |= BIT(INNER_IP_TOS);
	}

	return 0;
}

static int hclge_get_tc_flower_action(struct hclge_dev *hdev,
				      struct flow_cls_offload *cls_flower,
				      struct hclge_fd_rule *rule)
{
	struct flow_rule *flow = flow_cls_offload_flow_rule(cls_flower);
	struct netlink_ext_ack *extack = cls_flower->common.extack;
	struct hnae3_handle *handle = &hdev->vport[0].nic;
	struct flow_action *action = &flow->action;
	struct flow_action_entry *act;
	int tc;

	if (!flow_action_has_entries(&flow->action)) {
		tc = tc_classid_to_hwtc(handle->netdev, cls_flower->classid);
		if (tc < 0 || tc > hdev->tc_max) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "invalid traffic class: %d", tc);
			return -EINVAL;
		}

		rule->action = HCLGE_FD_ACTION_SELECT_TC;
		rule->cls_flower.tc = tc;
		return 0;
	}

	act = &action->entries[0];
	switch (act->id) {
	case FLOW_ACTION_RX_QUEUE_MAPPING:
		if (act->rx_queue >= handle->kinfo.num_tqps) {
			NL_SET_ERR_MSG_FMT_MOD(extack,
					       "queue id (%u) should be less than %u",
					       act->rx_queue,
					       handle->kinfo.num_tqps);
			return -EINVAL;
		}

		rule->queue_id = act->rx_queue;
		rule->action = HCLGE_FD_ACTION_SELECT_QUEUE;
		return 0;
	case FLOW_ACTION_DROP:
		rule->action = HCLGE_FD_ACTION_DROP_PACKET;
		return 0;
	default:
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "unsupported action(%d)", act->id);
		return -EOPNOTSUPP;
	}
}

static int hclge_parse_cls_flower(struct hclge_dev *hdev,
				  struct flow_cls_offload *cls_flower,
				  struct hclge_fd_rule *rule)
{
	struct flow_rule *flow = flow_cls_offload_flow_rule(cls_flower);
	struct netlink_ext_ack *extack = cls_flower->common.extack;
	int ret;

	/* not support any user def tuples */
	rule->unused_tuple |= HCLGE_FD_TUPLE_USER_DEF_TUPLES;

	hclge_get_cls_key_basic(flow, rule);
	hclge_get_cls_key_mac(flow, rule);
	hclge_get_cls_key_vlan(flow, rule);

	ret = hclge_get_cls_key_ip(flow, rule, extack);
	if (ret)
		return ret;

	hclge_get_cls_key_port(flow, rule);
	ret = hclge_get_cls_key_ip_tos(flow, rule, extack);
	if (ret)
		return ret;

	return hclge_get_cls_enc_keyid(hdev, flow, rule, extack);
}

static int hclge_check_cls_flower(struct hclge_dev *hdev,
				  struct flow_cls_offload *cls_flower)
{
	struct flow_rule *flow = flow_cls_offload_flow_rule(cls_flower);
	struct netlink_ext_ack *extack = cls_flower->common.extack;
	struct flow_dissector *dissector = flow->match.dissector;
	u32 prio = cls_flower->common.prio;
	u64 support_keys;

	if (prio == 0 ||
	    prio > hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1]) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "prio %u should be in range[1, %u]",
				       prio,
				       hdev->fd_cfg.rule_num[HCLGE_FD_STAGE_1]);
		return -EINVAL;
	}

	if (test_bit(prio - 1, hdev->fd_bmap)) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "prio %u is already used", prio);
		return -EINVAL;
	}

	support_keys = BIT_ULL(FLOW_DISSECTOR_KEY_CONTROL) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_BASIC) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_VLAN) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_IPV4_ADDRS) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_IPV6_ADDRS) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_PORTS) |
		       BIT_ULL(FLOW_DISSECTOR_KEY_IP);

	if (hdev->fd_cfg.fd_mode == HCLGE_FD_MODE_DEPTH_2K_WIDTH_400B_STAGE_1)
		support_keys |= BIT_ULL(FLOW_DISSECTOR_KEY_ETH_ADDRS) |
				BIT_ULL(FLOW_DISSECTOR_KEY_ENC_KEYID);

	if (dissector->used_keys & ~support_keys) {
		NL_SET_ERR_MSG_FMT_MOD(extack, "unsupported key set: %#llx",
				       dissector->used_keys);
		return -EOPNOTSUPP;
	}

	/* driver will parses classid into an action */
	if (cls_flower->classid && flow_action_has_entries(&flow->action)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "cannot specify both classid and action");
		return -EOPNOTSUPP;
	}

	if (!flow_action_has_entries(&flow->action) && !cls_flower->classid) {
		NL_SET_ERR_MSG_MOD(extack,
				   "must specify either classid or action");
		return -EINVAL;
	}

	if (flow_action_has_entries(&flow->action) &&
	    !flow_offload_has_one_action(&flow->action)) {
		NL_SET_ERR_MSG_MOD(extack, "unsupported multiple actions");
		return -EOPNOTSUPP;
	}

	return 0;
}

int hclge_add_cls_flower(struct hnae3_handle *handle,
			 struct flow_cls_offload *cls_flower)
{
	struct netlink_ext_ack *extack = cls_flower->common.extack;
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_rule *rule;
	int ret;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev)) {
		NL_SET_ERR_MSG_MOD(extack, "cls flower is not supported");
		return -EOPNOTSUPP;
	}

	ret = hclge_check_cls_flower(hdev, cls_flower);
	if (ret)
		return ret;

	rule = kzalloc_obj(*rule);
	if (!rule)
		return -ENOMEM;

	ret = hclge_parse_cls_flower(hdev, cls_flower, rule);
	if (ret) {
		kfree(rule);
		return ret;
	}

	ret = hclge_get_tc_flower_action(hdev, cls_flower, rule);
	if (ret) {
		kfree(rule);
		return ret;
	}

	rule->location = cls_flower->common.prio - 1;
	rule->vf_id = 0;
	rule->cls_flower.cookie = cls_flower->cookie;
	rule->rule_type = HCLGE_FD_TC_FLOWER_ACTIVE;

	ret = hclge_add_fd_entry_common(hdev, rule);
	if (ret)
		kfree(rule);

	return ret;
}

static struct hclge_fd_rule *hclge_find_cls_flower(struct hclge_dev *hdev,
						   unsigned long cookie)
{
	struct hclge_fd_rule *rule;
	struct hlist_node *node;

	hlist_for_each_entry_safe(rule, node, &hdev->fd_rule_list, rule_node) {
		if (rule->cls_flower.cookie == cookie)
			return rule;
	}

	return NULL;
}

int hclge_del_cls_flower(struct hnae3_handle *handle,
			 struct flow_cls_offload *cls_flower)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;
	struct hclge_fd_rule *rule;
	int ret;

	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return -EOPNOTSUPP;

	spin_lock_bh(&hdev->fd_rule_lock);

	rule = hclge_find_cls_flower(hdev, cls_flower->cookie);
	if (!rule) {
		spin_unlock_bh(&hdev->fd_rule_lock);
		return -EINVAL;
	}

	ret = hclge_fd_tcam_config(hdev, HCLGE_FD_STAGE_1, true, rule->location,
				   NULL, false);
	if (ret) {
		/* if tcam config fail, set rule state to TO_DEL,
		 * so the rule will be deleted when periodic
		 * task being scheduled.
		 */
		hclge_update_fd_list(hdev, HCLGE_FD_TO_DEL,
				     rule->location, NULL);
		set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);
		spin_unlock_bh(&hdev->fd_rule_lock);
		return ret;
	}

	hclge_update_fd_list(hdev, HCLGE_FD_DELETED, rule->location, NULL);
	spin_unlock_bh(&hdev->fd_rule_lock);

	return 0;
}

static void hclge_sync_fd_list(struct hclge_dev *hdev, struct hlist_head *hlist)
{
	struct hclge_fd_rule *rule;
	struct hlist_node *node;
	int ret = 0;

	if (!test_and_clear_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state))
		return;

	spin_lock_bh(&hdev->fd_rule_lock);

	hlist_for_each_entry_safe(rule, node, hlist, rule_node) {
		switch (rule->state) {
		case HCLGE_FD_TO_ADD:
			ret = hclge_fd_config_rule(hdev, rule);
			if (ret)
				goto out;
			rule->state = HCLGE_FD_ACTIVE;
			break;
		case HCLGE_FD_TO_DEL:
			ret = hclge_fd_tcam_config(hdev, HCLGE_FD_STAGE_1, true,
						   rule->location, NULL, false);
			if (ret)
				goto out;
			hclge_fd_dec_rule_cnt(hdev, rule->location);
			hclge_fd_free_node(hdev, rule);
			break;
		default:
			break;
		}
	}

out:
	if (ret)
		set_bit(HCLGE_STATE_FD_TBL_CHANGED, &hdev->state);

	spin_unlock_bh(&hdev->fd_rule_lock);
}

void hclge_sync_fd_table(struct hclge_dev *hdev)
{
	if (!hnae3_ae_dev_fd_supported(hdev->ae_dev))
		return;

	if (test_and_clear_bit(HCLGE_STATE_FD_CLEAR_ALL, &hdev->state)) {
		bool clear_list = hdev->fd_active_type == HCLGE_FD_ARFS_ACTIVE;

		hclge_clear_fd_rules_in_list(hdev, clear_list);
	}

	hclge_sync_fd_user_def_cfg(hdev, false);

	hclge_sync_fd_list(hdev, &hdev->fd_rule_list);
}

void hclge_enable_fd(struct hnae3_handle *handle, bool enable)
{
	struct hclge_vport *vport = hclge_get_vport(handle);
	struct hclge_dev *hdev = vport->back;

	hdev->fd_en = enable;

	if (!enable)
		set_bit(HCLGE_STATE_FD_CLEAR_ALL, &hdev->state);
	else
		hclge_restore_fd_entries(handle);

	hclge_task_schedule(hdev, 0);
}
