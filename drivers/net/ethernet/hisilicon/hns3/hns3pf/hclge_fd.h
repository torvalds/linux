/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (c) 2026 Hisilicon Limited. */

#ifndef __HCLGE_FD_H
#define __HCLGE_FD_H

struct hnae3_handle;
struct hclge_dev;

int hclge_init_fd_config(struct hclge_dev *hdev);
int hclge_add_fd_entry(struct hnae3_handle *handle, struct ethtool_rxnfc *cmd);
int hclge_del_fd_entry(struct hnae3_handle *handle, struct ethtool_rxnfc *cmd);
void hclge_del_all_fd_entries(struct hclge_dev *hdev);
int hclge_restore_fd_entries(struct hnae3_handle *handle);
int hclge_get_fd_rule_cnt(struct hnae3_handle *handle,
			  struct ethtool_rxnfc *cmd);
int hclge_get_fd_rule_info(struct hnae3_handle *handle,
			   struct ethtool_rxnfc *cmd);
int hclge_get_all_rules(struct hnae3_handle *handle,
			struct ethtool_rxnfc *cmd, u32 *rule_locs);
void hclge_enable_fd(struct hnae3_handle *handle, bool enable);
int hclge_add_fd_entry_by_arfs(struct hnae3_handle *handle, u16 queue_id,
			       u16 flow_id, struct flow_keys *fkeys);
int hclge_add_cls_flower(struct hnae3_handle *handle,
			 struct flow_cls_offload *cls_flower);
int hclge_del_cls_flower(struct hnae3_handle *handle,
			 struct flow_cls_offload *cls_flower);
bool hclge_is_cls_flower_active(struct hnae3_handle *handle);
int hclge_clear_arfs_rules(struct hclge_dev *hdev);
void hclge_sync_fd_table(struct hclge_dev *hdev);
void hclge_rfs_filter_expire(struct hclge_dev *hdev);

#endif
