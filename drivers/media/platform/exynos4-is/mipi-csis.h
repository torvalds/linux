/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung S5P/EXYNOS4 SoC series MIPI-CSI receiver driver
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */
#ifndef S5P_MIPI_CSIS_H_
#define S5P_MIPI_CSIS_H_

#define CSIS_DRIVER_NAME	"s5p-mipi-csis"
#define CSIS_SUBDEV_NAME	CSIS_DRIVER_NAME
#define CSIS_MAX_ENTITIES	2
#define CSIS0_MAX_LANES		4
#define CSIS1_MAX_LANES		2

#define CSIS_PAD_SINK		0
#define CSIS_PAD_SOURCE		1
#define CSIS_PADS_NUM		2

#define S5PCSIS_DEF_PIX_WIDTH	640
#define S5PCSIS_DEF_PIX_HEIGHT	480

#endif
