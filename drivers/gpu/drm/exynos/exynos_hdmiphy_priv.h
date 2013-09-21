/*
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Authors:
 *	Rahul Sharma <rahul.sharma@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;	either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef _EXYNOS_HDMI_PRIV_H_
#define _EXYNOS_HDMI_PRIV_H_

struct hdmiphy_context {
	/* hdmiphy resources */
	void __iomem		*regs;
	struct exynos_hdmiphy_ops	*ops;
	struct hdmiphy_config	*confs;
	unsigned int		nr_confs;
	struct hdmiphy_config	*current_conf;
};

struct hdmiphy_config {
	int pixel_clock;
	u8 conf[HDMIPHY_REG_COUNT];
};

struct hdmiphy_drv_data {
	struct hdmiphy_config *confs;
	unsigned int count;
};

#endif
