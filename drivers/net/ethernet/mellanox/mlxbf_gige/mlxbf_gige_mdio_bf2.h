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

#ifndef __MLXBF_GIGE_MDIO_BF2_H__
#define __MLXBF_GIGE_MDIO_BF2_H__

#include <linux/bitfield.h>

#define MLXBF2_GIGE_MDIO_GW_OFFSET	0x0
#define MLXBF2_GIGE_MDIO_CFG_OFFSET	0x4

/* MDIO GW register bits */
#define MLXBF2_GIGE_MDIO_GW_AD_MASK	GENMASK(15, 0)
#define MLXBF2_GIGE_MDIO_GW_DEVAD_MASK	GENMASK(20, 16)
#define MLXBF2_GIGE_MDIO_GW_PARTAD_MASK	GENMASK(25, 21)
#define MLXBF2_GIGE_MDIO_GW_OPCODE_MASK	GENMASK(27, 26)
#define MLXBF2_GIGE_MDIO_GW_ST1_MASK	GENMASK(28, 28)
#define MLXBF2_GIGE_MDIO_GW_BUSY_MASK	GENMASK(30, 30)

#define MLXBF2_GIGE_MDIO_GW_AD_SHIFT     0
#define MLXBF2_GIGE_MDIO_GW_DEVAD_SHIFT  16
#define MLXBF2_GIGE_MDIO_GW_PARTAD_SHIFT 21
#define MLXBF2_GIGE_MDIO_GW_OPCODE_SHIFT 26
#define MLXBF2_GIGE_MDIO_GW_ST1_SHIFT    28
#define MLXBF2_GIGE_MDIO_GW_BUSY_SHIFT   30

/* MDIO config register bits */
#define MLXBF2_GIGE_MDIO_CFG_MDIO_MODE_MASK		GENMASK(1, 0)
#define MLXBF2_GIGE_MDIO_CFG_MDIO3_3_MASK		GENMASK(2, 2)
#define MLXBF2_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK	GENMASK(4, 4)
#define MLXBF2_GIGE_MDIO_CFG_MDC_PERIOD_MASK		GENMASK(15, 8)
#define MLXBF2_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK		GENMASK(23, 16)
#define MLXBF2_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK		GENMASK(31, 24)

#define MLXBF2_GIGE_MDIO_CFG_VAL (FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDIO_MODE_MASK, 1) | \
				 FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDIO3_3_MASK, 1) | \
				 FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDIO_FULL_DRIVE_MASK, 1) | \
				 FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDIO_IN_SAMP_MASK, 6) | \
				 FIELD_PREP(MLXBF2_GIGE_MDIO_CFG_MDIO_OUT_SAMP_MASK, 13))

#endif /* __MLXBF_GIGE_MDIO_BF2_H__ */
