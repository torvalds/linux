/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Analog Devices Inc.
 */

#ifndef MAX_DES_H
#define MAX_DES_H

#include <media/v4l2-mediabus.h>

#include "max_serdes.h"

#define MAX_DES_DT_VC(dt, vc) (((vc) & 0x3) << 6 | ((dt) & 0x3f))

struct max_des_remap {
	u8 from_dt;
	u8 from_vc;
	u8 to_dt;
	u8 to_vc;
	u8 phy;
};

struct max_des_link {
	unsigned int index;
	bool enabled;
	enum max_serdes_gmsl_version version;
	struct max_serdes_i2c_xlate ser_xlate;
	struct i2c_adapter *adapter;
};

struct max_des_pipe_mode {
	bool dbl8;
	bool dbl10;
	bool dbl12;
	bool dbl8mode;
	bool dbl10mode;
};

struct max_des_pipe {
	unsigned int index;
	unsigned int stream_id;
	unsigned int link_id;
	unsigned int phy_id;
	struct max_des_remap *remaps;
	unsigned int num_remaps;
	struct max_serdes_vc_remap *vc_remaps;
	unsigned int num_vc_remaps;
	struct max_des_pipe_mode mode;
	bool enabled;
};

struct max_des_phy_mode {
	bool alt_mem_map8;
	bool alt2_mem_map8;
	bool alt_mem_map10;
	bool alt_mem_map12;
};

struct max_des_phy {
	unsigned int index;
	u64 link_frequency;
	struct v4l2_mbus_config_mipi_csi2 mipi;
	enum v4l2_mbus_type bus_type;
	struct max_des_phy_mode mode;
	bool enabled;
};

struct max_des;

struct max_des_info {
	unsigned int num_phys;
	unsigned int num_pipes;
	unsigned int num_links;
	unsigned int num_remaps_per_pipe;
	unsigned int versions;
	unsigned int modes;
	bool fix_tx_ids;
	bool use_atr;
	bool needs_single_link_version;
	bool needs_unique_stream_id;

	struct max_serdes_phys_configs phys_configs;
	struct max_serdes_tpg_entries tpg_entries;
	enum max_serdes_gmsl_mode tpg_mode;
	unsigned int tpg_patterns;
};

struct max_des_ops {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	int (*reg_read)(struct max_des *des, unsigned int reg, unsigned int *val);
	int (*reg_write)(struct max_des *des, unsigned int reg, unsigned int val);
#endif
	int (*log_status)(struct max_des *des);
	int (*log_pipe_status)(struct max_des *des, struct max_des_pipe *pipe);
	int (*log_phy_status)(struct max_des *des, struct max_des_phy *phy);
	int (*set_enable)(struct max_des *des, bool enable);
	int (*set_tpg)(struct max_des *des, const struct max_serdes_tpg_entry *entry);
	int (*init)(struct max_des *des);
	int (*init_phy)(struct max_des *des, struct max_des_phy *phy);
	int (*set_phy_mode)(struct max_des *des, struct max_des_phy *phy,
			    struct max_des_phy_mode *mode);
	int (*set_phy_enable)(struct max_des *des, struct max_des_phy *phy,
			      bool active);
	int (*set_pipe_stream_id)(struct max_des *des, struct max_des_pipe *pipe,
				  unsigned int stream_id);
	int (*set_pipe_link)(struct max_des *des, struct max_des_pipe *pipe,
			     struct max_des_link *link);
	int (*set_pipe_phy)(struct max_des *des, struct max_des_pipe *pipe,
			    struct max_des_phy *phy);
	int (*set_pipe_tunnel_phy)(struct max_des *des, struct max_des_pipe *pipe,
				   struct max_des_phy *phy);
	int (*set_pipe_enable)(struct max_des *des, struct max_des_pipe *pipe,
			       bool enable);
	int (*set_pipe_remap)(struct max_des *des, struct max_des_pipe *pipe,
			      unsigned int i, struct max_des_remap *remap);
	int (*set_pipe_remaps_enable)(struct max_des *des, struct max_des_pipe *pipe,
				      unsigned int mask);
	int (*set_pipe_vc_remap)(struct max_des *des, struct max_des_pipe *pipe,
				 unsigned int i, struct max_serdes_vc_remap *vc_remap);
	int (*set_pipe_vc_remaps_enable)(struct max_des *des, struct max_des_pipe *pipe,
					 unsigned int mask);
	int (*set_pipe_mode)(struct max_des *des, struct max_des_pipe *pipe,
			     struct max_des_pipe_mode *mode);
	int (*set_pipe_tunnel_enable)(struct max_des *des, struct max_des_pipe *pipe,
				      bool enable);
	int (*init_link)(struct max_des *des, struct max_des_link *link);
	int (*select_links)(struct max_des *des, unsigned int mask);
	int (*set_link_version)(struct max_des *des, struct max_des_link *link,
				enum max_serdes_gmsl_version version);
};

struct max_des_priv;

struct max_des {
	struct max_des_priv *priv;

	const struct max_des_info *info;
	const struct max_des_ops *ops;

	struct max_des_phy *phys;
	struct max_des_pipe *pipes;
	struct max_des_link *links;
	const struct max_serdes_tpg_entry *tpg_entry;
	enum max_serdes_tpg_pattern tpg_pattern;

	unsigned int phys_config;
	enum max_serdes_gmsl_mode mode;
	bool active;
};

int max_des_probe(struct i2c_client *client, struct max_des *des);

int max_des_remove(struct max_des *des);

int max_des_phy_hw_data_lanes(struct max_des *des, struct max_des_phy *phy);

#endif // MAX_DES_H
