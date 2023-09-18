/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_OUTPUT_H__
#define __LSDC_OUTPUT_H__

#include "lsdc_drv.h"

int ls7a1000_output_init(struct drm_device *ddev,
			 struct lsdc_display_pipe *dispipe,
			 struct i2c_adapter *ddc,
			 unsigned int index);

int ls7a2000_output_init(struct drm_device *ldev,
			 struct lsdc_display_pipe *dispipe,
			 struct i2c_adapter *ddc,
			 unsigned int index);

#endif
