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
#include <linux/amd-pmf-io.h>
#include <linux/power_supply.h>
#include <linux/units.h>
#include "pmf.h"

#ifdef CONFIG_AMD_PMF_DEBUG
static const char *platform_type_as_str(u16 platform_type)
{
	switch (platform_type) {
	case CLAMSHELL:
		return "CLAMSHELL";
	case FLAT:
		return "FLAT";
	case TENT:
		return "TENT";
	case STAND:
		return "STAND";
	case TABLET:
		return "TABLET";
	case BOOK:
		return "BOOK";
	case PRESENTATION:
		return "PRESENTATION";
	case PULL_FWD:
		return "PULL_FWD";
	default:
		return "UNKNOWN";
	}
}

static const char *laptop_placement_as_str(u16 device_state)
{
	switch (device_state) {
	case ON_TABLE:
		return "ON_TABLE";
	case ON_LAP_MOTION:
		return "ON_LAP_MOTION";
	case IN_BAG:
		return "IN_BAG";
	case OUT_OF_BAG:
		return "OUT_OF_BAG";
	default:
		return "UNKNOWN";
	}
}

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
	dev_dbg(dev->dev, "User Presence: %s\n", in->ev_info.user_present ? "Present" : "Away");
	dev_dbg(dev->dev, "Ambient Light: %d\n", in->ev_info.ambient_light);
	dev_dbg(dev->dev, "Platform type: %s\n", platform_type_as_str(in->ev_info.platform_type));
	dev_dbg(dev->dev, "Laptop placement: %s\n",
		laptop_placement_as_str(in->ev_info.device_state));
	dev_dbg(dev->dev, "Custom BIOS input1: %u\n", in->ev_info.bios_input1);
	dev_dbg(dev->dev, "Custom BIOS input2: %u\n", in->ev_info.bios_input2);
	dev_dbg(dev->dev, "==== TA inputs END ====\n");
}
#else
void amd_pmf_dump_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in) {}
#endif

static void amd_pmf_get_custom_bios_inputs(struct amd_pmf_dev *pdev,
					   struct ta_pmf_enact_table *in)
{
	if (!pdev->req.pending_req)
		return;

	switch (pdev->req.pending_req) {
	case BIT(NOTIFY_CUSTOM_BIOS_INPUT1):
		in->ev_info.bios_input1 = pdev->req.custom_policy[APMF_SMARTPC_CUSTOM_BIOS_INPUT1];
		break;
	case BIT(NOTIFY_CUSTOM_BIOS_INPUT2):
		in->ev_info.bios_input2 = pdev->req.custom_policy[APMF_SMARTPC_CUSTOM_BIOS_INPUT2];
		break;
	default:
		dev_dbg(pdev->dev, "Invalid preq for BIOS input: 0x%x\n", pdev->req.pending_req);
	}

	/* Clear pending requests after handling */
	memset(&pdev->req, 0, sizeof(pdev->req));
}

static void amd_pmf_get_c0_residency(u16 *core_res, size_t size, struct ta_pmf_enact_table *in)
{
	u16 max, avg = 0;
	int i;

	/* Get the avg and max C0 residency of all the cores */
	max = *core_res;
	for (i = 0; i < size; i++) {
		avg += core_res[i];
		if (core_res[i] > max)
			max = core_res[i];
	}
	avg = DIV_ROUND_CLOSEST(avg, size);
	in->ev_info.avg_c0residency = avg;
	in->ev_info.max_c0residency = max;
}

static void amd_pmf_get_smu_info(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	/* Get the updated metrics table data */
	memset(dev->buf, 0, dev->mtable_size);
	amd_pmf_send_cmd(dev, SET_TRANSFER_TABLE, 0, 7, NULL);

	switch (dev->cpu_id) {
	case AMD_CPU_ID_PS:
		memcpy(&dev->m_table, dev->buf, dev->mtable_size);
		in->ev_info.socket_power = dev->m_table.apu_power + dev->m_table.dgpu_power;
		in->ev_info.skin_temperature = dev->m_table.skin_temp;
		in->ev_info.gfx_busy = dev->m_table.avg_gfx_activity;
		amd_pmf_get_c0_residency(dev->m_table.avg_core_c0residency,
					 ARRAY_SIZE(dev->m_table.avg_core_c0residency), in);
		break;
	case PCI_DEVICE_ID_AMD_1AH_M20H_ROOT:
	case PCI_DEVICE_ID_AMD_1AH_M60H_ROOT:
		memcpy(&dev->m_table_v2, dev->buf, dev->mtable_size);
		in->ev_info.socket_power = dev->m_table_v2.apu_power + dev->m_table_v2.dgpu_power;
		in->ev_info.skin_temperature = dev->m_table_v2.skin_temp;
		in->ev_info.gfx_busy = dev->m_table_v2.gfx_activity;
		amd_pmf_get_c0_residency(dev->m_table_v2.core_c0residency,
					 ARRAY_SIZE(dev->m_table_v2.core_c0residency), in);
		break;
	default:
		dev_err(dev->dev, "Unsupported CPU id: 0x%x", dev->cpu_id);
	}
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
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		val = TA_BEST_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_BALANCED:
		val = TA_BETTER_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
	case PLATFORM_PROFILE_QUIET:
		val = TA_BEST_BATTERY;
		break;
	default:
		dev_err(dev->dev, "Unknown Platform Profile.\n");
		return -EOPNOTSUPP;
	}
	in->ev_info.power_slider = val;

	return 0;
}

static void amd_pmf_get_sensor_info(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	struct amd_sfh_info sfh_info;

	/* Get the latest information from SFH */
	in->ev_info.user_present = false;

	/* Get ALS data */
	if (!amd_get_sfh_info(&sfh_info, MT_ALS))
		in->ev_info.ambient_light = sfh_info.ambient_light;
	else
		dev_dbg(dev->dev, "ALS is not enabled/detected\n");

	/* get HPD data */
	if (!amd_get_sfh_info(&sfh_info, MT_HPD)) {
		if (sfh_info.user_present == SFH_USER_PRESENT)
			in->ev_info.user_present = true;
	} else {
		dev_dbg(dev->dev, "HPD is not enabled/detected\n");
	}

	/* Get SRA (Secondary Accelerometer) data */
	if (!amd_get_sfh_info(&sfh_info, MT_SRA)) {
		in->ev_info.platform_type = sfh_info.platform_type;
		in->ev_info.device_state = sfh_info.laptop_placement;
	} else {
		dev_dbg(dev->dev, "SRA is not enabled/detected\n");
	}
}

void amd_pmf_populate_ta_inputs(struct amd_pmf_dev *dev, struct ta_pmf_enact_table *in)
{
	/* TA side lid open is 1 and close is 0, hence the ! here */
	in->ev_info.lid_state = !acpi_lid_open();
	in->ev_info.power_source = amd_pmf_get_power_source();
	amd_pmf_get_smu_info(dev, in);
	amd_pmf_get_battery_info(dev, in);
	amd_pmf_get_slider_info(dev, in);
	amd_pmf_get_sensor_info(dev, in);
	amd_pmf_get_custom_bios_inputs(dev, in);
}
