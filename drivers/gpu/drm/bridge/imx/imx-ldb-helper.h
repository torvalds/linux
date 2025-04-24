/* SPDX-License-Identifier: GPL-2.0+ */

/*
 * Copyright 2019,2020,2022 NXP
 */

#ifndef __IMX_LDB_HELPER__
#define __IMX_LDB_HELPER__

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/regmap.h>

#include <drm/drm_atomic.h>
#include <drm/drm_bridge.h>
#include <drm/drm_device.h>
#include <drm/drm_encoder.h>
#include <drm/drm_modeset_helper_vtables.h>

#define LDB_CH0_MODE_EN_TO_DI0		BIT(0)
#define LDB_CH0_MODE_EN_TO_DI1		(3 << 0)
#define LDB_CH0_MODE_EN_MASK		(3 << 0)
#define LDB_CH1_MODE_EN_TO_DI0		BIT(2)
#define LDB_CH1_MODE_EN_TO_DI1		(3 << 2)
#define LDB_CH1_MODE_EN_MASK		(3 << 2)
#define LDB_SPLIT_MODE_EN		BIT(4)
#define LDB_DATA_WIDTH_CH0_24		BIT(5)
#define LDB_BIT_MAP_CH0_JEIDA		BIT(6)
#define LDB_DATA_WIDTH_CH1_24		BIT(7)
#define LDB_BIT_MAP_CH1_JEIDA		BIT(8)
#define LDB_DI0_VS_POL_ACT_LOW		BIT(9)
#define LDB_DI1_VS_POL_ACT_LOW		BIT(10)

#define MAX_LDB_CHAN_NUM		2

enum ldb_channel_link_type {
	LDB_CH_SINGLE_LINK,
	LDB_CH_DUAL_LINK_EVEN_ODD_PIXELS,
	LDB_CH_DUAL_LINK_ODD_EVEN_PIXELS,
};

struct ldb;

struct ldb_channel {
	struct ldb *ldb;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct device_node *np;
	u32 chno;
	bool is_available;
	u32 in_bus_format;
	u32 out_bus_format;
	enum ldb_channel_link_type link_type;
};

struct ldb {
	struct regmap *regmap;
	struct device *dev;
	struct ldb_channel *channel[MAX_LDB_CHAN_NUM];
	unsigned int ctrl_reg;
	u32 ldb_ctrl;
	unsigned int available_ch_cnt;
};

#define bridge_to_ldb_ch(b)	container_of(b, struct ldb_channel, bridge)

bool ldb_channel_is_single_link(struct ldb_channel *ldb_ch);
bool ldb_channel_is_split_link(struct ldb_channel *ldb_ch);

int ldb_bridge_atomic_check_helper(struct drm_bridge *bridge,
				   struct drm_bridge_state *bridge_state,
				   struct drm_crtc_state *crtc_state,
				   struct drm_connector_state *conn_state);

void ldb_bridge_mode_set_helper(struct drm_bridge *bridge,
				const struct drm_display_mode *mode,
				const struct drm_display_mode *adjusted_mode);

void ldb_bridge_enable_helper(struct drm_bridge *bridge);

void ldb_bridge_disable_helper(struct drm_bridge *bridge);

int ldb_bridge_attach_helper(struct drm_bridge *bridge, struct drm_encoder *encoder,
			     enum drm_bridge_attach_flags flags);

int ldb_init_helper(struct ldb *ldb);

int ldb_find_next_bridge_helper(struct ldb *ldb);

void ldb_add_bridge_helper(struct ldb *ldb);

void ldb_remove_bridge_helper(struct ldb *ldb);

#endif /* __IMX_LDB_HELPER__ */
