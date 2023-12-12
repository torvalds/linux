// SPDX-License-Identifier: GPL-2.0
/*
 * AMD Platform Management Framework Driver - Smart PC Capabilities
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *          Patil Rajesh Reddy <Patil.Reddy@amd.com>
 */

#include <acpi/button.h>
#include <linux/power_supply.h>
#include <linux/units.h>
#include "pmf.h"

#ifdef CONFIG_AMD_PMF_DEBUG
static const char *ta_slider_as_str(unsigned int state)
{
	switch (state) {
	case TA_BEST_PERFORMANCE:
		return "PERFORMANCE";
	case TA_BETTER_PERFORMANCE:
		return "BALANCED";
	case TA_BEST_BATTERY:
		return "POWER_SAVER";
	default:
		return "Unknown TA Slider State";
	}
}

void amd_pmf_dump_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	dev_dbg(dev->dev, "==== TA inputs START ====\n");
	dev_dbg(dev->dev, "Slider State: %s\n", ta_slider_as_str(in->ev_info.power_slider));
	dev_dbg(dev->dev, "Power Source: %s\n", amd_pmf_source_as_str(in->ev_info.power_source));
	dev_dbg(dev->dev, "Battery Percentage: %u\n", in->ev_info.bat_percentage);
	dev_dbg(dev->dev, "Designed Battery Capacity: %u\n", in->ev_info.bat_design);
	dev_dbg(dev->dev, "Fully Charged Capacity: %u\n", in->ev_info.full_charge_capacity);
	dev_dbg(dev->dev, "Drain Rate: %d\n", in->ev_info.drain_rate);
	dev_dbg(dev->dev, "Socket Power: %u\n", in->ev_info.socket_power);
	dev_dbg(dev->dev, "Skin Temperature: %u\n", in->ev_info.skin_temperature);
	dev_dbg(dev->dev, "Avg C0 Residency: %u\n", in->ev_info.avg_c0residency);
	dev_dbg(dev->dev, "Max C0 Residency: %u\n", in->ev_info.max_c0residency);
	dev_dbg(dev->dev, "GFX Busy: %u\n", in->ev_info.gfx_busy);
	dev_dbg(dev->dev, "LID State: %s\n", in->ev_info.lid_state ? "close" : "open");
	dev_dbg(dev->dev, "==== TA inputs END ====\n");
}
#else
void amd_pmf_dump_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in) {}
#endif

static void amd_pmf_get_smu_info(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	u16 max, avg = 0;
	int i;

	memset(dev->buf, 0, sizeof(dev->m_table));
	amd_pmf_send_cmd(dev, SET_TRANSFER_TABLE, 0, 7, NULL);
	memcpy(&dev->m_table, dev->buf, sizeof(dev->m_table));

	in->ev_info.socket_power = dev->m_table.apu_power + dev->m_table.dgpu_power;
	in->ev_info.skin_temperature = dev->m_table.skin_temp;

	/* Get the avg and max C0 residency of all the cores */
	max = dev->m_table.avg_core_c0residency[0];
	for (i = 0; i < ARRAY_SIZE(dev->m_table.avg_core_c0residency); i++) {
		avg += dev->m_table.avg_core_c0residency[i];
		if (dev->m_table.avg_core_c0residency[i] > max)
			max = dev->m_table.avg_core_c0residency[i];
	}

	avg = DIV_ROUND_CLOSEST(avg, ARRAY_SIZE(dev->m_table.avg_core_c0residency));
	in->ev_info.avg_c0residency = avg;
	in->ev_info.max_c0residency = max;
	in->ev_info.gfx_busy = dev->m_table.avg_gfx_activity;
}

static const char * const pmf_battery_supply_name[] = {
	"BATT",
	"BAT0",
};

static int amd_pmf_get_battery_prop(enum power_supply_property prop)
{
	union power_supply_propval value;
	struct power_supply *psy;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pmf_battery_supply_name); i++) {
		psy = power_supply_get_by_name(pmf_battery_supply_name[i]);
		if (!psy)
			continue;

		ret = power_supply_get_property(psy, prop, &value);
		if (ret) {
			power_supply_put(psy);
			return ret;
		}
	}

	return value.intval;
}

static int amd_pmf_get_battery_info(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	int val;

	val = amd_pmf_get_battery_prop(POWER_SUPPLY_PROP_PRESENT);
	if (val < 0)
		return val;
	if (val != 1)
		return -ENODEV;

	in->ev_info.bat_percentage = amd_pmf_get_battery_prop(POWER_SUPPLY_PROP_CAPACITY);
	/* all values in mWh metrics */
	in->ev_info.bat_design = amd_pmf_get_battery_prop(POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN) /
		MILLIWATT_PER_WATT;
	in->ev_info.full_charge_capacity = amd_pmf_get_battery_prop(POWER_SUPPLY_PROP_ENERGY_FULL) /
		MILLIWATT_PER_WATT;
	in->ev_info.drain_rate = amd_pmf_get_battery_prop(POWER_SUPPLY_PROP_POWER_NOW) /
		MILLIWATT_PER_WATT;

	return 0;
}

static int amd_pmf_get_slider_info(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	int val;

	switch (dev->current_profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		val = TA_BEST_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_BALANCED:
		val = TA_BETTER_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
		val = TA_BEST_BATTERY;
		break;
	default:
		dev_err(dev->dev, "Unknown Platform Profile.\n");
		return -EOPNOTSUPP;
	}
	in->ev_info.power_slider = val;

	return 0;
}

void amd_pmf_populate_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	/* TA side lid open is 1 and close is 0, hence the ! here */
	in->ev_info.lid_state = !acpi_lid_open();
	in->ev_info.power_source = amd_pmf_get_power_source();
	amd_pmf_get_smu_info(dev, in);
	amd_pmf_get_battery_info(dev, in);
	amd_pmf_get_slider_info(dev, in);
}
