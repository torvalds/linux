/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_RULE_H_
#define MLX5HWS_RULE_H_

enum {
	MLX5HWS_STE_CTRL_SZ = 20,
	MLX5HWS_ACTIONS_SZ = 12,
	MLX5HWS_MATCH_TAG_SZ = 32,
	MLX5HWS_JUMBO_TAG_SZ = 44,
};

enum mlx5hws_rule_status {
	MLX5HWS_RULE_STATUS_UNKNOWN,
	MLX5HWS_RULE_STATUS_CREATING,
	MLX5HWS_RULE_STATUS_CREATED,
	MLX5HWS_RULE_STATUS_DELETING,
	MLX5HWS_RULE_STATUS_DELETED,
	MLX5HWS_RULE_STATUS_FAILING,
	MLX5HWS_RULE_STATUS_FAILED,
};

enum mlx5hws_rule_move_state {
	MLX5HWS_RULE_RESIZE_STATE_IDLE,
	MLX5HWS_RULE_RESIZE_STATE_WRITING,
	MLX5HWS_RULE_RESIZE_STATE_DELETING,
};

enum mlx5hws_rule_jumbo_match_tag_offset {
	MLX5HWS_RULE_JUMBO_MATCH_TAG_OFFSET_DW0 = 8,
};

struct mlx5hws_rule_match_tag {
	union {
		u8 jumbo[MLX5HWS_JUMBO_TAG_SZ];
		struct {
			u8 reserved[MLX5HWS_ACTIONS_SZ];
			u8 match[MLX5HWS_MATCH_TAG_SZ];
		};
	};
};

struct mlx5hws_rule_resize_info {
	struct mlx5hws_pool *action_ste_pool[2];
	u32 rtc_0;
	u32 rtc_1;
	u32 rule_idx;
	u8 state;
	u8 max_stes;
	u8 ctrl_seg[MLX5HWS_WQE_SZ_GTA_CTRL]; /* Ctrl segment of STE: 48 bytes */
	u8 data_seg[MLX5HWS_WQE_SZ_GTA_DATA]; /* Data segment of STE: 64 bytes */
};

struct mlx5hws_rule {
	struct mlx5hws_matcher *matcher;
	union {
		struct mlx5hws_rule_match_tag tag;
		struct mlx5hws_rule_resize_info *resize_info;
	};
	u32 rtc_0; /* The RTC into which the STE was inserted */
	u32 rtc_1; /* The RTC into which the STE was inserted */
	int action_ste_idx; /* STE array index */
	u8 status; /* enum mlx5hws_rule_status */
	u8 action_ste_selector; /* For rule update - which action STE is in use */
	u8 pending_wqes;
	bool skip_delete; /* For complex rules - another rule with same tag
			   * still exists, so don't actually delete this rule.
			   */
};

void mlx5hws_rule_free_action_ste(struct mlx5hws_rule *rule);

int mlx5hws_rule_move_hws_remove(struct mlx5hws_rule *rule,
				 void *queue, void *user_data);

int mlx5hws_rule_move_hws_add(struct mlx5hws_rule *rule,
			      struct mlx5hws_rule_attr *attr);

bool mlx5hws_rule_move_in_progress(struct mlx5hws_rule *rule);

void mlx5hws_rule_clear_resize_info(struct mlx5hws_rule *rule);

#endif /* MLX5HWS_RULE_H_ */
