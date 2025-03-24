/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */
#ifndef _XE_PMT_H_
#define _XE_PMT_H_

#define SOC_BASE			0x280000

#define BMG_PMT_BASE_OFFSET		0xDB000
#define BMG_DISCOVERY_OFFSET		(SOC_BASE + BMG_PMT_BASE_OFFSET)

#define BMG_TELEMETRY_BASE_OFFSET	0xE0000
#define BMG_TELEMETRY_OFFSET		(SOC_BASE + BMG_TELEMETRY_BASE_OFFSET)

#define SG_REMAP_INDEX1			XE_REG(SOC_BASE + 0x08)
#define   SG_REMAP_BITS			REG_GENMASK(31, 24)

#endif
