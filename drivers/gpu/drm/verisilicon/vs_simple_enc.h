/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020 VeriSilicon Holdings Co., Ltd.
 */

#ifndef __VS_SIMPLE_ENC_H_
#define __VS_SIMPLE_ENC_H_

struct simple_encoder_priv {
	unsigned char encoder_type;
};

struct dss_data {
	u32 mask;
	u32 value;
};

struct simple_encoder {
	struct drm_encoder encoder;
	struct device *dev;
	const struct simple_encoder_priv *priv;
	struct regmap *dss_regmap;
	struct dss_data *dss_regdatas;
};

extern struct platform_driver simple_encoder_driver;
#endif /* __VS_SIMPLE_ENC_H_ */
