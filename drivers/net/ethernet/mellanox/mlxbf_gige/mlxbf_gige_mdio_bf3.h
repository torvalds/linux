/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */

/* MDIO support for Mellanox Gigabit Ethernet driver
 *
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef __MLXBF_GIGE_MDIO_BF3_H__
#define __MLXBF_GIGE_MDIO_BF3_H__

#include <linux/bitfield.h>

#define MLXBF3_GIGE_MDIO_GW_OFFSET	0x80
#define MLXBF3_GIGE_MDIO_DATA_READ	0x8c
#define MLXBF3_GIGE_MDIO_CFG_REG0	0x100
#define MLXBF3_GIGE_MDIO_CFG_REG1	0x104
#define MLXBF3_GIGE_MDIO_CFG_REG2	0x108

/* MDIO GW register bits */
#define MLXBF3_GIGE_MDIO_GW_ST1_MASK	GENMASK(1, 1)
#define MLXBF3_GIGE_MDIO_GW_OPCODE_MASK	GENMASK(3, 2)
#define MLXBF3_GIGE_MDIO_GW_PARTAD_MASK	GENMASK(8, 4)
#define MLXBF3_GIGE_MDIO_GW_DEVAD_MASK	GENMASK(13, 9)
/* For BlueField-3, this field is only used for mdio write */
#define MLXBF3_GIGE_MDIO_GW_DATA_MASK	GENMASK(29, 14)
#define MLXBF3_GIGE_MDIO_GW_BUSY_MASK	GENMASK(30, 30)

#define MLXBF3_GIGE_MDIO_GW_DATA_READ_MASK GENMASK(15, 0)

#define MLXBF3_GIGE_MDIO_GW_ST1_SHIFT    1
#define MLXBF3_GIGE_MDIO_GW_OPCODE_SHIFT 2
#define MLXBF3_GIGE_MDIO_GW_PARTAD_SHIFT 4
#define MLXBF3_GIGE_MDIO_GW_DEVAD_SHIFT	 9
#define MLXBF3_GIGE_MDIO_GW_DATA_SHIFT   14
#define MLXBF3_GIGE_MDIO_GW_BUSY_SHIFT   30

#define MLXBF3_GIGE_MDIO_GW_DATA_READ_SHIFT 0

/* MDIO config register bits */
#define MLXBF3_GIGE_MDIO_CFG_MDIO_MODE_MASK		GENMASK(1, 0)
#define MLXBF3_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK	GENMASK(2, 2)
#define MLXBF3_GIGE_MDIO_CFG_MDC_PERIOD_MASK		GENMASK(7, 0)
#define MLXBF3_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK		GENMASK(7, 0)
#define MLXBF3_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK		GENMASK(15, 8)

#endif /* __MLXBF_GIGE_MDIO_BF3_H__ */
