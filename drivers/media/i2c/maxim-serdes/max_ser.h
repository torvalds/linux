/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Analog Devices Inc.
 */

#ifndef MAX_SER_H
#define MAX_SER_H

#include <linux/i2c.h>

#include <media/v4l2-mediabus.h>

#include "max_serdes.h"

#define MAX_SER_REG0				0x0
#define MAX_SER_REG0_DEV_ADDR			GENMASK(7, 1)

#define MAX_SER_CTRL0				0x10
#define MAX_SER_CTRL0_RESET_ALL			BIT(7)

#define MAX_SER_CFGI_INFOFR_TR3			0x7b
#define MAX_SER_CFGL_SPI_TR3			0x83
#define MAX_SER_CFGC_CC_TR3			0x8b
#define MAX_SER_CFGC_GPIO_TR3			0x93
#define MAX_SER_CFGL_IIC_X_TR3			0xa3
#define MAX_SER_CFGL_IIC_Y_TR3			0xab

struct max_ser_phy {
	unsigned int index;
	struct v4l2_mbus_config_mipi_csi2 mipi;
	bool enabled;
	bool active;
};

struct max_ser_pipe_mode {
	unsigned int soft_bpp;
	unsigned int bpp;
	bool dbl8;
	bool dbl10;
	bool dbl12;
};

struct max_ser_pipe {
	unsigned int index;
	unsigned int phy_id;
	unsigned int stream_id;
	unsigned int *dts;
	unsigned int num_dts;
	unsigned int vcs;
	struct max_ser_pipe_mode mode;
	bool enabled;
};

struct max_ser;

struct max_ser_ops {
	unsigned int modes;
	unsigned int num_pipes;
	unsigned int num_dts_per_pipe;
	unsigned int num_phys;
	unsigned int num_i2c_xlates;
	unsigned int num_vc_remaps;

	struct max_serdes_phys_configs phys_configs;
	struct max_serdes_tpg_entries tpg_entries;
	enum max_serdes_gmsl_mode tpg_mode;
	unsigned int tpg_patterns;

#ifdef CONFIG_VIDEO_ADV_DEBUG
	int (*reg_read)(struct max_ser *ser, unsigned int reg, unsigned int *val);
	int (*reg_write)(struct max_ser *ser, unsigned int reg, unsigned int val);
#endif
	int (*log_status)(struct max_ser *ser);
	int (*log_pipe_status)(struct max_ser *ser, struct max_ser_pipe *pipe);
	int (*log_phy_status)(struct max_ser *ser, struct max_ser_phy *phy);
	int (*init)(struct max_ser *ser);
	int (*set_i2c_xlate)(struct max_ser *ser, unsigned int i,
			     struct max_serdes_i2c_xlate *i2c_xlate);
	int (*set_tunnel_enable)(struct max_ser *ser, bool enable);
	int (*set_tpg)(struct max_ser *ser, const struct max_serdes_tpg_entry *entry);
	int (*init_phy)(struct max_ser *ser, struct max_ser_phy *phy);
	int (*set_phy_active)(struct max_ser *ser, struct max_ser_phy *phy,
			      bool enable);
	int (*set_pipe_enable)(struct max_ser *ser, struct max_ser_pipe *pipe,
			       bool enable);
	int (*set_pipe_dt)(struct max_ser *ser, struct max_ser_pipe *pipe,
			   unsigned int i, unsigned int dt);
	int (*set_pipe_dt_en)(struct max_ser *ser, struct max_ser_pipe *pipe,
			      unsigned int i, bool enable);
	int (*set_pipe_vcs)(struct max_ser *ser, struct max_ser_pipe *pipe,
			    unsigned int vcs);
	int (*set_pipe_mode)(struct max_ser *ser, struct max_ser_pipe *pipe,
			     struct max_ser_pipe_mode *mode);
	int (*set_vc_remap)(struct max_ser *ser, unsigned int i,
			    struct max_serdes_vc_remap *vc_remap);
	int (*set_vc_remaps_enable)(struct max_ser *ser, unsigned int mask);
	int (*set_pipe_stream_id)(struct max_ser *ser, struct max_ser_pipe *pipe,
				  unsigned int stream_id);
	unsigned int (*get_pipe_stream_id)(struct max_ser *ser, struct max_ser_pipe *pipe);
	int (*set_pipe_phy)(struct max_ser *ser, struct max_ser_pipe *pipe,
			    struct max_ser_phy *phy);
};

struct max_ser_priv;

struct max_ser {
	struct max_ser_priv *priv;

	const struct max_ser_ops *ops;

	struct max_serdes_i2c_xlate *i2c_xlates;

	struct max_ser_phy *phys;
	struct max_ser_pipe *pipes;
	const struct max_serdes_tpg_entry *tpg_entry;
	enum max_serdes_tpg_pattern tpg_pattern;

	struct max_serdes_vc_remap *vc_remaps;
	unsigned int num_vc_remaps;

	unsigned int phys_config;
	unsigned int active;
	enum max_serdes_gmsl_mode mode;
};

int max_ser_probe(struct i2c_client *client, struct max_ser *ser);

int max_ser_remove(struct max_ser *ser);

int max_ser_set_double_bpps(struct v4l2_subdev *sd, u32 double_bpps);
unsigned int max_ser_get_supported_modes(struct v4l2_subdev *sd);
int max_ser_set_mode(struct v4l2_subdev *sd, enum max_serdes_gmsl_mode mode);
bool max_ser_supports_vc_remap(struct v4l2_subdev *sd);
int max_ser_set_stream_id(struct v4l2_subdev *sd, unsigned int stream_id);
int max_ser_get_stream_id(struct v4l2_subdev *sd, unsigned int *stream_id);
int max_ser_set_vc_remaps(struct v4l2_subdev *sd, struct max_serdes_vc_remap *vc_remaps,
			  int num_vc_remaps);

int max_ser_reset(struct i2c_adapter *adapter, u8 addr);
int max_ser_wait(struct i2c_adapter *adapter, u8 addr);
int max_ser_wait_for_multiple(struct i2c_adapter *adapter, u8 *addrs,
			      unsigned int num_addrs, u8 *current_addr);

int max_ser_change_address(struct i2c_adapter *adapter, u8 addr, u8 new_addr);
int max_ser_fix_tx_ids(struct i2c_adapter *adapter, u8 addr);

#endif // MAX_SER_H
