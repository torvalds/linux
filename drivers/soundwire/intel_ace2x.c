// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
// Copyright(c) 2023 Intel Corporation. All rights reserved.

/*
 * Soundwire Intel ops for LunarLake
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/soundwire/sdw_registers.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "bus.h"
#include "intel.h"

const struct sdw_intel_hw_ops sdw_intel_lnl_hw_ops = {
};
EXPORT_SYMBOL_NS(sdw_intel_lnl_hw_ops, SOUNDWIRE_INTEL);
