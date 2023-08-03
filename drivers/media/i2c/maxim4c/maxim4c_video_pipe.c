// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim Quad GMSL Deserializer Video Pipe driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * Author: Cai Wenzhong <cwz@rock-chips.com>
 *
 */
#include "maxim4c_api.h"

static int maxim4c_video_pipe_select(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	u16 reg_addr = 0;
	int i = 0, shift = 0;
	int ret = 0;

	// video pipe selection
	reg_mask = 0;
	reg_value = 0;
	for (i = 0; i < MAXIM4C_PIPE_O_ID_MAX; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];

		shift = (i % 2) ? 4 : 0;
		if (video_pipe_cfg->pipe_enable) {
			reg_mask |= (0xF << shift);
			reg_value |= ((video_pipe_cfg->pipe_idx & 0x3) << (0 + shift));
			reg_value |= ((video_pipe_cfg->link_idx & 0x3) << (2 + shift));
		}

		if ((i % 2 == 1) && (reg_mask != 0)) {
			reg_addr = 0x00F0 + (i / 2);

			ret |= maxim4c_i2c_update_byte(client,
					reg_addr, MAXIM4C_I2C_REG_ADDR_16BITS,
					reg_mask, reg_value);

			// Prepare for next register
			reg_mask = 0;
			reg_value = 0;
		}
	}

	return ret;
}

static int maxim4c_video_pipe_run_init_seq(maxim4c_t *maxim4c)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	struct maxim4c_i2c_init_seq *init_seq = NULL;
	int i = 0;
	int ret = 0;

	// video pipe init sequence
	for (i = 0; i < MAXIM4C_PIPE_O_ID_MAX; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];
		init_seq = &video_pipe_cfg->pipe_init_seq;
		ret = maxim4c_i2c_run_init_seq(client, init_seq);
		if (ret) {
			dev_err(dev, "pipe id = %d init sequence error\n", i);
			return ret;
		}
	}

	// video pipe parallel mode init sequence
	init_seq = &video_pipe->parallel_init_seq;
	ret = maxim4c_i2c_run_init_seq(client, init_seq);
	if (ret) {
		dev_err(dev, "pipe parallel init sequence error\n");
		return ret;
	}

	return 0;
}

static int maxim4c_video_pipe_config_parse_dt(struct device *dev,
		maxim4c_video_pipe_t *video_pipe,
		struct device_node *parent_node)
{
	struct device_node *node = NULL;
	struct device_node *init_seq_node = NULL;
	struct maxim4c_i2c_init_seq *init_seq = NULL;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	const char *pipe_cfg_name = "video-pipe-config";
	u32 sub_idx = 0, pipe_id = 0;
	u32 value = 0;
	int ret = 0;

	node = NULL;
	sub_idx = 0;
	while ((node = of_get_next_child(parent_node, node))) {
		if (!strncasecmp(node->name,
					pipe_cfg_name,
					strlen(pipe_cfg_name))) {
			if (sub_idx >= MAXIM4C_PIPE_O_ID_MAX) {
				dev_err(dev, "Too many matching %s node\n",
					pipe_cfg_name);

				of_node_put(node);
				break;
			}

			if (!of_device_is_available(node)) {
				dev_info(dev, "%pOF is disabled\n", node);

				sub_idx++;

				continue;
			}

			/* Video Pipe: pipe id */
			ret = of_property_read_u32(node, "pipe-id", &pipe_id);
			if (ret) {
				// if pipe_id is error, parse next node
				dev_err(dev, "Can not get pipe-id property!");

				sub_idx++;

				continue;
			}
			if (pipe_id >= MAXIM4C_PIPE_O_ID_MAX) {
				// if pipe_id is error, parse next node
				dev_err(dev, "Error pipe-id = %d!", pipe_id);

				sub_idx++;

				continue;
			}

			video_pipe_cfg = &video_pipe->pipe_cfg[pipe_id];

			/* Video Pipe: pipe enable */
			video_pipe_cfg->pipe_enable = 1;
			video_pipe->pipe_enable_mask |= BIT(pipe_id);

			dev_info(dev, "video pipe id = %d: pipe enable = %d\n",
				pipe_id, video_pipe_cfg->pipe_enable);

			/* Video Pipe: other config */
			ret = of_property_read_u32(node, "pipe-idx", &value);
			if (ret == 0) {
				dev_info(dev, "pipe-idx property: %d", value);
				video_pipe_cfg->pipe_idx = value;
			}

			ret = of_property_read_u32(node, "link-idx", &value);
			if (ret == 0) {
				dev_info(dev, "link-idx property: %d", value);
				video_pipe_cfg->link_idx = value;
			}

			init_seq_node = of_get_child_by_name(node, "pipe-init-sequence");
			if (!IS_ERR_OR_NULL(init_seq_node)) {
				dev_info(dev, "load pipe-init-sequence\n");

				init_seq = &video_pipe_cfg->pipe_init_seq;
				maxim4c_i2c_load_init_seq(dev, init_seq_node, init_seq);

				of_node_put(init_seq_node);
			}

			sub_idx++;
		}
	}

	node = of_get_child_by_name(parent_node, "parallel-mode-config");
	if (!IS_ERR_OR_NULL(node)) {
		if (!of_device_is_available(node)) {
			dev_info(dev, "%pOF is disabled\n", node);

			of_node_put(node);
			return 0;
		}

		init_seq_node = of_get_child_by_name(node, "parallel-init-sequence");
		if (!IS_ERR_OR_NULL(init_seq_node)) {
			dev_info(dev, "load parallel-init-sequence\n");

			init_seq = &video_pipe->parallel_init_seq;
			maxim4c_i2c_load_init_seq(dev, init_seq_node, init_seq);

			of_node_put(init_seq_node);
		}

		of_node_put(node);
	}

	return 0;
}

int maxim4c_video_pipe_parse_dt(maxim4c_t *maxim4c, struct device_node *of_node)
{
	struct device *dev = &maxim4c->client->dev;
	struct device_node *node = NULL;
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	int ret = 0;

	dev_info(dev, "=== maxim4c video pipe parse dt ===\n");

	node = of_get_child_by_name(of_node, "video-pipes");
	if (IS_ERR_OR_NULL(node))
		return -ENODEV;

	if (!of_device_is_available(node)) {
		dev_info(dev, "%pOF is disabled\n", node);
		of_node_put(node);
		return -ENODEV;
	}

	ret = maxim4c_video_pipe_config_parse_dt(dev, video_pipe, node);

	of_node_put(node);

	return ret;
}
EXPORT_SYMBOL(maxim4c_video_pipe_parse_dt);

int maxim4c_video_pipe_mask_enable(maxim4c_t *maxim4c, u8 video_pipe_mask, bool enable)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	int i = 0;
	int ret = 0;

	dev_dbg(dev, "%s, video_pipe_mask = 0x%x, enable = %d\n",
			__func__, video_pipe_mask, enable);

	reg_mask = 0;
	reg_value = 0;
	// video pipe enable
	for (i = 0; i < MAXIM4C_PIPE_O_ID_MAX; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];
		if (video_pipe_cfg->pipe_enable
				&& (video_pipe_mask & BIT(i))) {
			reg_mask |= BIT(i);
			if (enable)
				reg_value |= BIT(i);
		}
	}

	if (reg_mask != 0) {
		ret |= maxim4c_i2c_update_byte(client,
				0x00F4, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_video_pipe_mask_enable);

int maxim4c_video_pipe_linkid_enable(maxim4c_t *maxim4c, u8 link_id, bool enable)
{
	struct i2c_client *client = maxim4c->client;
	struct device *dev = &client->dev;
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	u8 reg_mask = 0, reg_value = 0;
	int i = 0;
	int ret = 0;

	dev_dbg(dev, "%s, link_id = %d, enable = %d\n",
			__func__, link_id, enable);

	reg_mask = 0;
	reg_value = 0;
	// video pipe enable
	for (i = 0; i < MAXIM4C_PIPE_O_ID_MAX; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];
		if (video_pipe_cfg->pipe_enable
				&& (video_pipe_cfg->link_idx == link_id)) {
			reg_mask = BIT(i);
			if (enable)
				reg_value = BIT(i);
		}
	}

	if (reg_mask != 0) {
		ret = maxim4c_i2c_update_byte(client,
				0x00F4, MAXIM4C_I2C_REG_ADDR_16BITS,
				reg_mask, reg_value);
	}

	return ret;
}
EXPORT_SYMBOL(maxim4c_video_pipe_linkid_enable);

void maxim4c_video_pipe_data_init(maxim4c_t *maxim4c)
{
	maxim4c_video_pipe_t *video_pipe = &maxim4c->video_pipe;
	struct maxim4c_pipe_cfg *video_pipe_cfg = NULL;
	int i = 0;

	video_pipe->pipe_enable_mask = 0;
	video_pipe->parallel_init_seq.reg_init_seq = NULL;

	for (i = 0; i < 4; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];

		video_pipe_cfg->pipe_enable = 0;
		video_pipe_cfg->pipe_idx = MAXIM4C_PIPE_I_ID_Z;
		video_pipe_cfg->link_idx = (i % 4);
		video_pipe_cfg->pipe_init_seq.reg_init_seq = NULL;
	}

	for (i = 4; i < MAXIM4C_PIPE_O_ID_MAX; i++) {
		video_pipe_cfg = &video_pipe->pipe_cfg[i];

		video_pipe_cfg->pipe_enable = 0;
		video_pipe_cfg->pipe_idx = MAXIM4C_PIPE_I_ID_X;
		video_pipe_cfg->link_idx = (i % 4);
		video_pipe_cfg->pipe_init_seq.reg_init_seq = NULL;
	}
}
EXPORT_SYMBOL(maxim4c_video_pipe_data_init);

int maxim4c_video_pipe_hw_init(maxim4c_t *maxim4c)
{
	u8 pipe_enable_mask = 0;
	int ret = 0;

	ret = maxim4c_video_pipe_select(maxim4c);
	if (ret)
		return ret;

	pipe_enable_mask = maxim4c->video_pipe.pipe_enable_mask;
	ret = maxim4c_video_pipe_mask_enable(maxim4c, pipe_enable_mask, true);
	if (ret)
		return ret;

	ret = maxim4c_video_pipe_run_init_seq(maxim4c);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(maxim4c_video_pipe_hw_init);
