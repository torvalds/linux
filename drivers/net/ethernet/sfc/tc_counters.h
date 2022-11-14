/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_TC_COUNTERS_H
#define EFX_TC_COUNTERS_H
#include "net_driver.h"

#include "mcdi_pcol.h" /* for MAE_COUNTER_TYPE_* */

enum efx_tc_counter_type {
	EFX_TC_COUNTER_TYPE_AR = MAE_COUNTER_TYPE_AR,
	EFX_TC_COUNTER_TYPE_CT = MAE_COUNTER_TYPE_CT,
	EFX_TC_COUNTER_TYPE_OR = MAE_COUNTER_TYPE_OR,
	EFX_TC_COUNTER_TYPE_MAX
};

extern const struct efx_channel_type efx_tc_channel_type;

#endif /* EFX_TC_COUNTERS_H */
