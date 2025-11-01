// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/ethtool.h>

#include "hinic3_cmdq.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_rss.h"

static void hinic3_fillout_indir_tbl(struct net_device *netdev, u16 *indir)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	u16 i, num_qps;

	num_qps = nic_dev->q_params.num_qps;
	for (i = 0; i < L2NIC_RSS_INDIR_SIZE; i++)
		indir[i] = ethtool_rxfh_indir_default(i, num_qps);
}

static int hinic3_rss_cfg(struct hinic3_hwdev *hwdev, u8 rss_en, u16 num_qps)
{
	struct mgmt_msg_params msg_params = {};
	struct l2nic_cmd_cfg_rss rss_cfg = {};
	int err;

	rss_cfg.func_id = hinic3_global_func_id(hwdev);
	rss_cfg.rss_en = rss_en;
	rss_cfg.rq_priority_number = 0;
	rss_cfg.num_qps = num_qps;

	mgmt_msg_params_init_default(&msg_params, &rss_cfg, sizeof(rss_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_RSS, &msg_params);
	if (err || rss_cfg.msg_head.status) {
		dev_err(hwdev->dev, "Failed to set rss cfg, err: %d, status: 0x%x\n",
			err, rss_cfg.msg_head.status);
		return -EINVAL;
	}

	return 0;
}

static void hinic3_init_rss_parameters(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->rss_hash_type = HINIC3_RSS_HASH_ENGINE_TYPE_XOR;
	nic_dev->rss_type.tcp_ipv6_ext = 1;
	nic_dev->rss_type.ipv6_ext = 1;
	nic_dev->rss_type.tcp_ipv6 = 1;
	nic_dev->rss_type.ipv6 = 1;
	nic_dev->rss_type.tcp_ipv4 = 1;
	nic_dev->rss_type.ipv4 = 1;
	nic_dev->rss_type.udp_ipv6 = 1;
	nic_dev->rss_type.udp_ipv4 = 1;
}

static void decide_num_qps(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	unsigned int dev_cpus;

	dev_cpus = netif_get_num_default_rss_queues();
	nic_dev->q_params.num_qps = min(dev_cpus, nic_dev->max_qps);
}

static int alloc_rss_resource(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	nic_dev->rss_hkey = kmalloc_array(L2NIC_RSS_KEY_SIZE,
					  sizeof(nic_dev->rss_hkey[0]),
					  GFP_KERNEL);
	if (!nic_dev->rss_hkey)
		return -ENOMEM;

	netdev_rss_key_fill(nic_dev->rss_hkey, L2NIC_RSS_KEY_SIZE);

	nic_dev->rss_indir = kcalloc(L2NIC_RSS_INDIR_SIZE, sizeof(u16),
				     GFP_KERNEL);
	if (!nic_dev->rss_indir) {
		kfree(nic_dev->rss_hkey);
		nic_dev->rss_hkey = NULL;
		return -ENOMEM;
	}

	return 0;
}

static int hinic3_rss_set_indir_tbl(struct hinic3_hwdev *hwdev,
				    const u16 *indir_table)
{
	struct l2nic_cmd_rss_set_indir_tbl *indir_tbl;
	struct hinic3_cmd_buf *cmd_buf;
	__le64 out_param;
	int err;
	u32 i;

	cmd_buf = hinic3_alloc_cmd_buf(hwdev);
	if (!cmd_buf) {
		dev_err(hwdev->dev, "Failed to allocate cmd buf\n");
		return -ENOMEM;
	}

	cmd_buf->size = cpu_to_le16(sizeof(struct l2nic_cmd_rss_set_indir_tbl));
	indir_tbl = cmd_buf->buf;
	memset(indir_tbl, 0, sizeof(*indir_tbl));

	for (i = 0; i < L2NIC_RSS_INDIR_SIZE; i++)
		indir_tbl->entry[i] = cpu_to_le16(indir_table[i]);

	hinic3_cmdq_buf_swab32(indir_tbl, sizeof(*indir_tbl));

	err = hinic3_cmdq_direct_resp(hwdev, MGMT_MOD_L2NIC,
				      L2NIC_UCODE_CMD_SET_RSS_INDIR_TBL,
				      cmd_buf, &out_param);
	if (err || out_param) {
		dev_err(hwdev->dev, "Failed to set rss indir table\n");
		err = -EFAULT;
	}

	hinic3_free_cmd_buf(hwdev, cmd_buf);

	return err;
}

static int hinic3_set_rss_type(struct hinic3_hwdev *hwdev,
			       struct hinic3_rss_type rss_type)
{
	struct l2nic_cmd_set_rss_ctx_tbl ctx_tbl = {};
	struct mgmt_msg_params msg_params = {};
	u32 ctx;
	int err;

	ctx_tbl.func_id = hinic3_global_func_id(hwdev);
	ctx = L2NIC_RSS_TYPE_SET(1, VALID) |
	      L2NIC_RSS_TYPE_SET(rss_type.ipv4, IPV4) |
	      L2NIC_RSS_TYPE_SET(rss_type.ipv6, IPV6) |
	      L2NIC_RSS_TYPE_SET(rss_type.ipv6_ext, IPV6_EXT) |
	      L2NIC_RSS_TYPE_SET(rss_type.tcp_ipv4, TCP_IPV4) |
	      L2NIC_RSS_TYPE_SET(rss_type.tcp_ipv6, TCP_IPV6) |
	      L2NIC_RSS_TYPE_SET(rss_type.tcp_ipv6_ext, TCP_IPV6_EXT) |
	      L2NIC_RSS_TYPE_SET(rss_type.udp_ipv4, UDP_IPV4) |
	      L2NIC_RSS_TYPE_SET(rss_type.udp_ipv6, UDP_IPV6);
	ctx_tbl.context = ctx;

	mgmt_msg_params_init_default(&msg_params, &ctx_tbl, sizeof(ctx_tbl));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_SET_RSS_CTX_TBL, &msg_params);

	if (ctx_tbl.msg_head.status == MGMT_STATUS_CMD_UNSUPPORTED) {
		return MGMT_STATUS_CMD_UNSUPPORTED;
	} else if (err || ctx_tbl.msg_head.status) {
		dev_err(hwdev->dev, "mgmt Failed to set rss context offload, err: %d, status: 0x%x\n",
			err, ctx_tbl.msg_head.status);
		return -EINVAL;
	}

	return 0;
}

static int hinic3_rss_cfg_hash_type(struct hinic3_hwdev *hwdev, u8 opcode,
				    enum hinic3_rss_hash_type *type)
{
	struct l2nic_cmd_cfg_rss_engine hash_type_cmd = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	hash_type_cmd.func_id = hinic3_global_func_id(hwdev);
	hash_type_cmd.opcode = opcode;

	if (opcode == MGMT_MSG_CMD_OP_SET)
		hash_type_cmd.hash_engine = *type;

	mgmt_msg_params_init_default(&msg_params, &hash_type_cmd,
				     sizeof(hash_type_cmd));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_RSS_HASH_ENGINE,
				       &msg_params);
	if (err || hash_type_cmd.msg_head.status) {
		dev_err(hwdev->dev, "Failed to %s hash engine, err: %d, status: 0x%x\n",
			opcode == MGMT_MSG_CMD_OP_SET ? "set" : "get",
			err, hash_type_cmd.msg_head.status);
		return -EIO;
	}

	if (opcode == MGMT_MSG_CMD_OP_GET)
		*type = hash_type_cmd.hash_engine;

	return 0;
}

static int hinic3_rss_set_hash_type(struct hinic3_hwdev *hwdev,
				    enum hinic3_rss_hash_type type)
{
	return hinic3_rss_cfg_hash_type(hwdev, MGMT_MSG_CMD_OP_SET, &type);
}

static int hinic3_config_rss_hw_resource(struct net_device *netdev,
					 u16 *indir_tbl)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	err = hinic3_rss_set_indir_tbl(nic_dev->hwdev, indir_tbl);
	if (err)
		return err;

	err = hinic3_set_rss_type(nic_dev->hwdev, nic_dev->rss_type);
	if (err)
		return err;

	return hinic3_rss_set_hash_type(nic_dev->hwdev, nic_dev->rss_hash_type);
}

static int hinic3_rss_cfg_hash_key(struct hinic3_hwdev *hwdev, u8 opcode,
				   u8 *key)
{
	struct l2nic_cmd_cfg_rss_hash_key hash_key = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	hash_key.func_id = hinic3_global_func_id(hwdev);
	hash_key.opcode = opcode;

	if (opcode == MGMT_MSG_CMD_OP_SET)
		memcpy(hash_key.key, key, L2NIC_RSS_KEY_SIZE);

	mgmt_msg_params_init_default(&msg_params, &hash_key, sizeof(hash_key));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_L2NIC,
				       L2NIC_CMD_CFG_RSS_HASH_KEY, &msg_params);
	if (err || hash_key.msg_head.status) {
		dev_err(hwdev->dev, "Failed to %s hash key, err: %d, status: 0x%x\n",
			opcode == MGMT_MSG_CMD_OP_SET ? "set" : "get",
			err, hash_key.msg_head.status);
		return -EINVAL;
	}

	if (opcode == MGMT_MSG_CMD_OP_GET)
		memcpy(key, hash_key.key, L2NIC_RSS_KEY_SIZE);

	return 0;
}

static int hinic3_rss_set_hash_key(struct hinic3_hwdev *hwdev, u8 *key)
{
	return hinic3_rss_cfg_hash_key(hwdev, MGMT_MSG_CMD_OP_SET, key);
}

static int hinic3_set_hw_rss_parameters(struct net_device *netdev, u8 rss_en)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	int err;

	err = hinic3_rss_set_hash_key(nic_dev->hwdev, nic_dev->rss_hkey);
	if (err)
		return err;

	hinic3_fillout_indir_tbl(netdev, nic_dev->rss_indir);

	err = hinic3_config_rss_hw_resource(netdev, nic_dev->rss_indir);
	if (err)
		return err;

	err = hinic3_rss_cfg(nic_dev->hwdev, rss_en, nic_dev->q_params.num_qps);
	if (err)
		return err;

	return 0;
}

int hinic3_rss_init(struct net_device *netdev)
{
	return hinic3_set_hw_rss_parameters(netdev, 1);
}

void hinic3_rss_uninit(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	hinic3_rss_cfg(nic_dev->hwdev, 0, 0);
}

void hinic3_clear_rss_config(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);

	kfree(nic_dev->rss_hkey);
	nic_dev->rss_hkey = NULL;

	kfree(nic_dev->rss_indir);
	nic_dev->rss_indir = NULL;
}

void hinic3_try_to_enable_rss(struct net_device *netdev)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	int err;

	nic_dev->max_qps = hinic3_func_max_qnum(hwdev);
	if (nic_dev->max_qps <= 1 ||
	    !hinic3_test_support(nic_dev, HINIC3_NIC_F_RSS))
		goto err_reset_q_params;

	err = alloc_rss_resource(netdev);
	if (err) {
		nic_dev->max_qps = 1;
		goto err_reset_q_params;
	}

	set_bit(HINIC3_RSS_ENABLE, &nic_dev->flags);
	decide_num_qps(netdev);
	hinic3_init_rss_parameters(netdev);
	err = hinic3_set_hw_rss_parameters(netdev, 0);
	if (err) {
		dev_err(hwdev->dev, "Failed to set hardware rss parameters\n");
		hinic3_clear_rss_config(netdev);
		nic_dev->max_qps = 1;
		goto err_reset_q_params;
	}

	return;

err_reset_q_params:
	clear_bit(HINIC3_RSS_ENABLE, &nic_dev->flags);
	nic_dev->q_params.num_qps = nic_dev->max_qps;
}
