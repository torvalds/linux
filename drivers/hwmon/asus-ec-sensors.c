// SPDX-License-Identifier: GPL-2.0+
/*
 * HWMON driver for ASUS motherboards that publish some sensor values
 * via the embedded controller registers.
 *
 * Copyright (C) 2021 Eugene Shalygin <eugene.shalygin@gmail.com>

 * EC provides:
 * - Chipset temperature
 * - CPU temperature
 * - Motherboard temperature
 * - T_Sensor temperature
 * - VRM temperature
 * - Water In temperature
 * - Water Out temperature
 * - CPU Optional fan RPM
 * - Chipset fan RPM
 * - VRM Heat Sink fan RPM
 * - Water Flow fan RPM
 * - CPU current
 * - CPU core voltage
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/dev_printk.h>
#include <linux/dmi.h>
#include <linux/hwmon.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sort.h>
#include <linux/units.h>

#include <asm/unaligned.h>

static char *mutex_path_override;

/* Writing to this EC register switches EC bank */
#define ASUS_EC_BANK_REGISTER	0xff
#define SENSOR_LABEL_LEN	16

/*
 * Arbitrary set max. allowed bank number. Required for sorting banks and
 * currently is overkill with just 2 banks used at max, but for the sake
 * of alignment let's set it to a higher value.
 */
#define ASUS_EC_MAX_BANK	3

#define ACPI_LOCK_DELAY_MS	500

/* ACPI mutex for locking access to the EC for the firmware */
#define ASUS_HW_ACCESS_MUTEX_ASMX	"\\AMW0.ASMX"

#define ASUS_HW_ACCESS_MUTEX_RMTW_ASMX	"\\RMTW.ASMX"

#define ASUS_HW_ACCESS_MUTEX_SB_PCI0_SBRG_SIO1_MUT0 "\\_SB_.PCI0.SBRG.SIO1.MUT0"

#define MAX_IDENTICAL_BOARD_VARIATIONS	3

/* Moniker for the ACPI global lock (':' is not allowed in ASL identifiers) */
#define ACPI_GLOBAL_LOCK_PSEUDO_PATH	":GLOBAL_LOCK"

typedef union {
	u32 value;
	struct {
		u8 index;
		u8 bank;
		u8 size;
		u8 dummy;
	} components;
} sensor_address;

#define MAKE_SENSOR_ADDRESS(size, bank, index) {                               \
		.value = (size << 16) + (bank << 8) + index                    \
	}

static u32 hwmon_attributes[hwmon_max] = {
	[hwmon_chip] = HWMON_C_REGISTER_TZ,
	[hwmon_temp] = HWMON_T_INPUT | HWMON_T_LABEL,
	[hwmon_in] = HWMON_I_INPUT | HWMON_I_LABEL,
	[hwmon_curr] = HWMON_C_INPUT | HWMON_C_LABEL,
	[hwmon_fan] = HWMON_F_INPUT | HWMON_F_LABEL,
};

struct ec_sensor_info {
	char label[SENSOR_LABEL_LEN];
	enum hwmon_sensor_types type;
	sensor_address addr;
};

#define EC_SENSOR(sensor_label, sensor_type, size, bank, index) {              \
		.label = sensor_label, .type = sensor_type,                    \
		.addr = MAKE_SENSOR_ADDRESS(size, bank, index),                \
	}

enum ec_sensors {
	/* chipset temperature [℃] */
	ec_sensor_temp_chipset,
	/* CPU temperature [℃] */
	ec_sensor_temp_cpu,
	/* CPU package temperature [℃] */
	ec_sensor_temp_cpu_package,
	/* motherboard temperature [℃] */
	ec_sensor_temp_mb,
	/* "T_Sensor" temperature sensor reading [℃] */
	ec_sensor_temp_t_sensor,
	/* VRM temperature [℃] */
	ec_sensor_temp_vrm,
	/* CPU Core voltage [mV] */
	ec_sensor_in_cpu_core,
	/* CPU_Opt fan [RPM] */
	ec_sensor_fan_cpu_opt,
	/* VRM heat sink fan [RPM] */
	ec_sensor_fan_vrm_hs,
	/* Chipset fan [RPM] */
	ec_sensor_fan_chipset,
	/* Water flow sensor reading [RPM] */
	ec_sensor_fan_water_flow,
	/* CPU current [A] */
	ec_sensor_curr_cpu,
	/* "Water_In" temperature sensor reading [℃] */
	ec_sensor_temp_water_in,
	/* "Water_Out" temperature sensor reading [℃] */
	ec_sensor_temp_water_out,
	/* "Water_Block_In" temperature sensor reading [℃] */
	ec_sensor_temp_water_block_in,
	/* "Water_Block_Out" temperature sensor reading [℃] */
	ec_sensor_temp_water_block_out,
	/* "T_sensor_2" temperature sensor reading [℃] */
	ec_sensor_temp_t_sensor_2,
	/* "Extra_1" temperature sensor reading [℃] */
	ec_sensor_temp_sensor_extra_1,
	/* "Extra_2" temperature sensor reading [℃] */
	ec_sensor_temp_sensor_extra_2,
	/* "Extra_3" temperature sensor reading [℃] */
	ec_sensor_temp_sensor_extra_3,
};

#define SENSOR_TEMP_CHIPSET BIT(ec_sensor_temp_chipset)
#define SENSOR_TEMP_CPU BIT(ec_sensor_temp_cpu)
#define SENSOR_TEMP_CPU_PACKAGE BIT(ec_sensor_temp_cpu_package)
#define SENSOR_TEMP_MB BIT(ec_sensor_temp_mb)
#define SENSOR_TEMP_T_SENSOR BIT(ec_sensor_temp_t_sensor)
#define SENSOR_TEMP_VRM BIT(ec_sensor_temp_vrm)
#define SENSOR_IN_CPU_CORE BIT(ec_sensor_in_cpu_core)
#define SENSOR_FAN_CPU_OPT BIT(ec_sensor_fan_cpu_opt)
#define SENSOR_FAN_VRM_HS BIT(ec_sensor_fan_vrm_hs)
#define SENSOR_FAN_CHIPSET BIT(ec_sensor_fan_chipset)
#define SENSOR_FAN_WATER_FLOW BIT(ec_sensor_fan_water_flow)
#define SENSOR_CURR_CPU BIT(ec_sensor_curr_cpu)
#define SENSOR_TEMP_WATER_IN BIT(ec_sensor_temp_water_in)
#define SENSOR_TEMP_WATER_OUT BIT(ec_sensor_temp_water_out)
#define SENSOR_TEMP_WATER_BLOCK_IN BIT(ec_sensor_temp_water_block_in)
#define SENSOR_TEMP_WATER_BLOCK_OUT BIT(ec_sensor_temp_water_block_out)
#define SENSOR_TEMP_T_SENSOR_2 BIT(ec_sensor_temp_t_sensor_2)
#define SENSOR_TEMP_SENSOR_EXTRA_1 BIT(ec_sensor_temp_sensor_extra_1)
#define SENSOR_TEMP_SENSOR_EXTRA_2 BIT(ec_sensor_temp_sensor_extra_2)
#define SENSOR_TEMP_SENSOR_EXTRA_3 BIT(ec_sensor_temp_sensor_extra_3)

enum board_family {
	family_unknown,
	family_amd_400_series,
	family_amd_500_series,
	family_amd_600_series,
	family_intel_300_series,
	family_intel_600_series
};

/* All the known sensors for ASUS EC controllers */
static const struct ec_sensor_info sensors_family_amd_400[] = {
	[ec_sensor_temp_chipset] =
		EC_SENSOR("Chipset", hwmon_temp, 1, 0x00, 0x3a),
	[ec_sensor_temp_cpu] =
		EC_SENSOR("CPU", hwmon_temp, 1, 0x00, 0x3b),
	[ec_sensor_temp_mb] =
		EC_SENSOR("Motherboard", hwmon_temp, 1, 0x00, 0x3c),
	[ec_sensor_temp_t_sensor] =
		EC_SENSOR("T_Sensor", hwmon_temp, 1, 0x00, 0x3d),
	[ec_sensor_temp_vrm] =
		EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x3e),
	[ec_sensor_in_cpu_core] =
		EC_SENSOR("CPU Core", hwmon_in, 2, 0x00, 0xa2),
	[ec_sensor_fan_cpu_opt] =
		EC_SENSOR("CPU_Opt", hwmon_fan, 2, 0x00, 0xbc),
	[ec_sensor_fan_vrm_hs] =
		EC_SENSOR("VRM HS", hwmon_fan, 2, 0x00, 0xb2),
	[ec_sensor_fan_chipset] =
		/* no chipset fans in this generation */
		EC_SENSOR("Chipset", hwmon_fan, 0, 0x00, 0x00),
	[ec_sensor_fan_water_flow] =
		EC_SENSOR("Water_Flow", hwmon_fan, 2, 0x00, 0xb4),
	[ec_sensor_curr_cpu] =
		EC_SENSOR("CPU", hwmon_curr, 1, 0x00, 0xf4),
	[ec_sensor_temp_water_in] =
		EC_SENSOR("Water_In", hwmon_temp, 1, 0x01, 0x0d),
	[ec_sensor_temp_water_out] =
		EC_SENSOR("Water_Out", hwmon_temp, 1, 0x01, 0x0b),
};

static const struct ec_sensor_info sensors_family_amd_500[] = {
	[ec_sensor_temp_chipset] =
		EC_SENSOR("Chipset", hwmon_temp, 1, 0x00, 0x3a),
	[ec_sensor_temp_cpu] = EC_SENSOR("CPU", hwmon_temp, 1, 0x00, 0x3b),
	[ec_sensor_temp_mb] =
		EC_SENSOR("Motherboard", hwmon_temp, 1, 0x00, 0x3c),
	[ec_sensor_temp_t_sensor] =
		EC_SENSOR("T_Sensor", hwmon_temp, 1, 0x00, 0x3d),
	[ec_sensor_temp_vrm] = EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x3e),
	[ec_sensor_in_cpu_core] =
		EC_SENSOR("CPU Core", hwmon_in, 2, 0x00, 0xa2),
	[ec_sensor_fan_cpu_opt] =
		EC_SENSOR("CPU_Opt", hwmon_fan, 2, 0x00, 0xb0),
	[ec_sensor_fan_vrm_hs] = EC_SENSOR("VRM HS", hwmon_fan, 2, 0x00, 0xb2),
	[ec_sensor_fan_chipset] =
		EC_SENSOR("Chipset", hwmon_fan, 2, 0x00, 0xb4),
	[ec_sensor_fan_water_flow] =
		EC_SENSOR("Water_Flow", hwmon_fan, 2, 0x00, 0xbc),
	[ec_sensor_curr_cpu] = EC_SENSOR("CPU", hwmon_curr, 1, 0x00, 0xf4),
	[ec_sensor_temp_water_in] =
		EC_SENSOR("Water_In", hwmon_temp, 1, 0x01, 0x00),
	[ec_sensor_temp_water_out] =
		EC_SENSOR("Water_Out", hwmon_temp, 1, 0x01, 0x01),
	[ec_sensor_temp_water_block_in] =
		EC_SENSOR("Water_Block_In", hwmon_temp, 1, 0x01, 0x02),
	[ec_sensor_temp_water_block_out] =
		EC_SENSOR("Water_Block_Out", hwmon_temp, 1, 0x01, 0x03),
	[ec_sensor_temp_sensor_extra_1] =
		EC_SENSOR("Extra_1", hwmon_temp, 1, 0x01, 0x09),
	[ec_sensor_temp_t_sensor_2] =
		EC_SENSOR("T_sensor_2", hwmon_temp, 1, 0x01, 0x0a),
	[ec_sensor_temp_sensor_extra_2] =
		EC_SENSOR("Extra_2", hwmon_temp, 1, 0x01, 0x0b),
	[ec_sensor_temp_sensor_extra_3] =
		EC_SENSOR("Extra_3", hwmon_temp, 1, 0x01, 0x0c),
};

static const struct ec_sensor_info sensors_family_amd_600[] = {
	[ec_sensor_temp_cpu] = EC_SENSOR("CPU", hwmon_temp, 1, 0x00, 0x30),
	[ec_sensor_temp_cpu_package] = EC_SENSOR("CPU Package", hwmon_temp, 1, 0x00, 0x31),
	[ec_sensor_temp_mb] =
	EC_SENSOR("Motherboard", hwmon_temp, 1, 0x00, 0x32),
	[ec_sensor_temp_vrm] =
		EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x33),
	[ec_sensor_temp_water_in] =
		EC_SENSOR("Water_In", hwmon_temp, 1, 0x01, 0x00),
	[ec_sensor_temp_water_out] =
		EC_SENSOR("Water_Out", hwmon_temp, 1, 0x01, 0x01),
};

static const struct ec_sensor_info sensors_family_intel_300[] = {
	[ec_sensor_temp_chipset] =
		EC_SENSOR("Chipset", hwmon_temp, 1, 0x00, 0x3a),
	[ec_sensor_temp_cpu] = EC_SENSOR("CPU", hwmon_temp, 1, 0x00, 0x3b),
	[ec_sensor_temp_mb] =
		EC_SENSOR("Motherboard", hwmon_temp, 1, 0x00, 0x3c),
	[ec_sensor_temp_t_sensor] =
		EC_SENSOR("T_Sensor", hwmon_temp, 1, 0x00, 0x3d),
	[ec_sensor_temp_vrm] = EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x3e),
	[ec_sensor_fan_cpu_opt] =
		EC_SENSOR("CPU_Opt", hwmon_fan, 2, 0x00, 0xb0),
	[ec_sensor_fan_vrm_hs] = EC_SENSOR("VRM HS", hwmon_fan, 2, 0x00, 0xb2),
	[ec_sensor_fan_water_flow] =
		EC_SENSOR("Water_Flow", hwmon_fan, 2, 0x00, 0xbc),
	[ec_sensor_temp_water_in] =
		EC_SENSOR("Water_In", hwmon_temp, 1, 0x01, 0x00),
	[ec_sensor_temp_water_out] =
		EC_SENSOR("Water_Out", hwmon_temp, 1, 0x01, 0x01),
};

static const struct ec_sensor_info sensors_family_intel_600[] = {
	[ec_sensor_temp_t_sensor] =
		EC_SENSOR("T_Sensor", hwmon_temp, 1, 0x00, 0x3d),
	[ec_sensor_temp_vrm] = EC_SENSOR("VRM", hwmon_temp, 1, 0x00, 0x3e),
};

/* Shortcuts for common combinations */
#define SENSOR_SET_TEMP_CHIPSET_CPU_MB                                         \
	(SENSOR_TEMP_CHIPSET | SENSOR_TEMP_CPU | SENSOR_TEMP_MB)
#define SENSOR_SET_TEMP_WATER (SENSOR_TEMP_WATER_IN | SENSOR_TEMP_WATER_OUT)
#define SENSOR_SET_WATER_BLOCK                                                 \
	(SENSOR_TEMP_WATER_BLOCK_IN | SENSOR_TEMP_WATER_BLOCK_OUT)

struct ec_board_info {
	unsigned long sensors;
	/*
	 * Defines which mutex to use for guarding access to the state and the
	 * hardware. Can be either a full path to an AML mutex or the
	 * pseudo-path ACPI_GLOBAL_LOCK_PSEUDO_PATH to use the global ACPI lock,
	 * or left empty to use a regular mutex object, in which case access to
	 * the hardware is not guarded.
	 */
	const char *mutex_path;
	enum board_family family;
};

static const struct ec_board_info board_info_prime_x470_pro = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CPU_OPT |
		SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE,
	.mutex_path = ACPI_GLOBAL_LOCK_PSEUDO_PATH,
	.family = family_amd_400_series,
};

static const struct ec_board_info board_info_prime_x570_pro = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB | SENSOR_TEMP_VRM |
		SENSOR_TEMP_T_SENSOR | SENSOR_FAN_CHIPSET,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_pro_art_x570_creator_wifi = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB | SENSOR_TEMP_VRM |
		SENSOR_TEMP_T_SENSOR | SENSOR_FAN_CPU_OPT |
		SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_pro_art_b550_creator = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_FAN_CPU_OPT,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_pro_ws_x570_ace = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB | SENSOR_TEMP_VRM |
		SENSOR_TEMP_T_SENSOR | SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_crosshair_x670e_hero = {
	.sensors = SENSOR_TEMP_CPU | SENSOR_TEMP_CPU_PACKAGE |
		SENSOR_TEMP_MB | SENSOR_TEMP_VRM |
		SENSOR_SET_TEMP_WATER,
	.mutex_path = ACPI_GLOBAL_LOCK_PSEUDO_PATH,
	.family = family_amd_600_series,
};

static const struct ec_board_info board_info_crosshair_viii_dark_hero = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_TEMP_VRM | SENSOR_SET_TEMP_WATER |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_WATER_FLOW |
		SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_crosshair_viii_hero = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_TEMP_VRM | SENSOR_SET_TEMP_WATER |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_CHIPSET |
		SENSOR_FAN_WATER_FLOW | SENSOR_CURR_CPU |
		SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_maximus_xi_hero = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_TEMP_VRM | SENSOR_SET_TEMP_WATER |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_WATER_FLOW,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_intel_300_series,
};

static const struct ec_board_info board_info_crosshair_viii_impact = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CHIPSET | SENSOR_CURR_CPU |
		SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_b550_e_gaming = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CPU_OPT,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_b550_i_gaming = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_VRM_HS | SENSOR_CURR_CPU |
		SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_x570_e_gaming = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM |
		SENSOR_FAN_CHIPSET | SENSOR_CURR_CPU |
		SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_x570_e_gaming_wifi_ii = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_CURR_CPU |
		SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_x570_f_gaming = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB |
		SENSOR_TEMP_T_SENSOR | SENSOR_FAN_CHIPSET,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_x570_i_gaming = {
	.sensors = SENSOR_TEMP_CHIPSET | SENSOR_TEMP_VRM |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_FAN_VRM_HS | SENSOR_FAN_CHIPSET |
		SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_amd_500_series,
};

static const struct ec_board_info board_info_strix_z390_f_gaming = {
	.sensors = SENSOR_TEMP_CHIPSET | SENSOR_TEMP_VRM |
		SENSOR_TEMP_T_SENSOR |
		SENSOR_FAN_CPU_OPT,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_ASMX,
	.family = family_intel_300_series,
};

static const struct ec_board_info board_info_strix_z690_a_gaming_wifi_d4 = {
	.sensors = SENSOR_TEMP_T_SENSOR | SENSOR_TEMP_VRM,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_RMTW_ASMX,
	.family = family_intel_600_series,
};

static const struct ec_board_info board_info_zenith_ii_extreme = {
	.sensors = SENSOR_SET_TEMP_CHIPSET_CPU_MB | SENSOR_TEMP_T_SENSOR |
		SENSOR_TEMP_VRM | SENSOR_SET_TEMP_WATER |
		SENSOR_FAN_CPU_OPT | SENSOR_FAN_CHIPSET | SENSOR_FAN_VRM_HS |
		SENSOR_FAN_WATER_FLOW | SENSOR_CURR_CPU | SENSOR_IN_CPU_CORE |
		SENSOR_SET_WATER_BLOCK |
		SENSOR_TEMP_T_SENSOR_2 | SENSOR_TEMP_SENSOR_EXTRA_1 |
		SENSOR_TEMP_SENSOR_EXTRA_2 | SENSOR_TEMP_SENSOR_EXTRA_3,
	.mutex_path = ASUS_HW_ACCESS_MUTEX_SB_PCI0_SBRG_SIO1_MUT0,
	.family = family_amd_500_series,
};

#define DMI_EXACT_MATCH_ASUS_BOARD_NAME(name, board_info)                      \
	{                                                                      \
		.matches = {                                                   \
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR,                      \
					"ASUSTeK COMPUTER INC."),              \
			DMI_EXACT_MATCH(DMI_BOARD_NAME, name),                 \
		},                                                             \
		.driver_data = (void *)board_info,                              \
	}

static const struct dmi_system_id dmi_table[] = {
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("PRIME X470-PRO",
					&board_info_prime_x470_pro),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("PRIME X570-PRO",
					&board_info_prime_x570_pro),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ProArt X570-CREATOR WIFI",
					&board_info_pro_art_x570_creator_wifi),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ProArt B550-CREATOR",
					&board_info_pro_art_b550_creator),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("Pro WS X570-ACE",
					&board_info_pro_ws_x570_ace),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII DARK HERO",
					&board_info_crosshair_viii_dark_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII FORMULA",
					&board_info_crosshair_viii_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII HERO",
					&board_info_crosshair_viii_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII HERO (WI-FI)",
					&board_info_crosshair_viii_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR X670E HERO",
					&board_info_crosshair_x670e_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG MAXIMUS XI HERO",
					&board_info_maximus_xi_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG MAXIMUS XI HERO (WI-FI)",
					&board_info_maximus_xi_hero),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG CROSSHAIR VIII IMPACT",
					&board_info_crosshair_viii_impact),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX B550-E GAMING",
					&board_info_strix_b550_e_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX B550-I GAMING",
					&board_info_strix_b550_i_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX X570-E GAMING",
					&board_info_strix_x570_e_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX X570-E GAMING WIFI II",
					&board_info_strix_x570_e_gaming_wifi_ii),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX X570-F GAMING",
					&board_info_strix_x570_f_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX X570-I GAMING",
					&board_info_strix_x570_i_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX Z390-F GAMING",
					&board_info_strix_z390_f_gaming),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG STRIX Z690-A GAMING WIFI D4",
					&board_info_strix_z690_a_gaming_wifi_d4),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG ZENITH II EXTREME",
					&board_info_zenith_ii_extreme),
	DMI_EXACT_MATCH_ASUS_BOARD_NAME("ROG ZENITH II EXTREME ALPHA",
					&board_info_zenith_ii_extreme),
	{},
};

struct ec_sensor {
	unsigned int info_index;
	s32 cached_value;
};

struct lock_data {
	union {
		acpi_handle aml;
		/* global lock handle */
		u32 glk;
	} mutex;
	bool (*lock)(struct lock_data *data);
	bool (*unlock)(struct lock_data *data);
};

/*
 * The next function pairs implement options for locking access to the
 * state and the EC
 */
static bool lock_via_acpi_mutex(struct lock_data *data)
{
	/*
	 * ASUS DSDT does not specify that access to the EC has to be guarded,
	 * but firmware does access it via ACPI
	 */
	return ACPI_SUCCESS(acpi_acquire_mutex(data->mutex.aml,
					       NULL, ACPI_LOCK_DELAY_MS));
}

static bool unlock_acpi_mutex(struct lock_data *data)
{
	return ACPI_SUCCESS(acpi_release_mutex(data->mutex.aml, NULL));
}

static bool lock_via_global_acpi_lock(struct lock_data *data)
{
	return ACPI_SUCCESS(acpi_acquire_global_lock(ACPI_LOCK_DELAY_MS,
						     &data->mutex.glk));
}

static bool unlock_global_acpi_lock(struct lock_data *data)
{
	return ACPI_SUCCESS(acpi_release_global_lock(data->mutex.glk));
}

struct ec_sensors_data {
	const struct ec_board_info *board_info;
	const struct ec_sensor_info *sensors_info;
	struct ec_sensor *sensors;
	/* EC registers to read from */
	u16 *registers;
	u8 *read_buffer;
	/* sorted list of unique register banks */
	u8 banks[ASUS_EC_MAX_BANK + 1];
	/* in jiffies */
	unsigned long last_updated;
	struct lock_data lock_data;
	/* number of board EC sensors */
	u8 nr_sensors;
	/*
	 * number of EC registers to read
	 * (sensor might span more than 1 register)
	 */
	u8 nr_registers;
	/* number of unique register banks */
	u8 nr_banks;
};

static u8 register_bank(u16 reg)
{
	return reg >> 8;
}

static u8 register_index(u16 reg)
{
	return reg & 0x00ff;
}

static bool is_sensor_data_signed(const struct ec_sensor_info *si)
{
	/*
	 * guessed from WMI functions in DSDT code for boards
	 * of the X470 generation
	 */
	return si->type == hwmon_temp;
}

static const struct ec_sensor_info *
get_sensor_info(const struct ec_sensors_data *state, int index)
{
	return state->sensors_info + state->sensors[index].info_index;
}

static int find_ec_sensor_index(const struct ec_sensors_data *ec,
				enum hwmon_sensor_types type, int channel)
{
	unsigned int i;

	for (i = 0; i < ec->nr_sensors; i++) {
		if (get_sensor_info(ec, i)->type == type) {
			if (channel == 0)
				return i;
			channel--;
		}
	}
	return -ENOENT;
}

static int bank_compare(const void *a, const void *b)
{
	return *((const s8 *)a) - *((const s8 *)b);
}

static void setup_sensor_data(struct ec_sensors_data *ec)
{
	struct ec_sensor *s = ec->sensors;
	bool bank_found;
	int i, j;
	u8 bank;

	ec->nr_banks = 0;
	ec->nr_registers = 0;

	for_each_set_bit(i, &ec->board_info->sensors,
			 BITS_PER_TYPE(ec->board_info->sensors)) {
		s->info_index = i;
		s->cached_value = 0;
		ec->nr_registers +=
			ec->sensors_info[s->info_index].addr.components.size;
		bank_found = false;
		bank = ec->sensors_info[s->info_index].addr.components.bank;
		for (j = 0; j < ec->nr_banks; j++) {
			if (ec->banks[j] == bank) {
				bank_found = true;
				break;
			}
		}
		if (!bank_found) {
			ec->banks[ec->nr_banks++] = bank;
		}
		s++;
	}
	sort(ec->banks, ec->nr_banks, 1, bank_compare, NULL);
}

static void fill_ec_registers(struct ec_sensors_data *ec)
{
	const struct ec_sensor_info *si;
	unsigned int i, j, register_idx = 0;

	for (i = 0; i < ec->nr_sensors; ++i) {
		si = get_sensor_info(ec, i);
		for (j = 0; j < si->addr.components.size; ++j, ++register_idx) {
			ec->registers[register_idx] =
				(si->addr.components.bank << 8) +
				si->addr.components.index + j;
		}
	}
}

static int setup_lock_data(struct device *dev)
{
	const char *mutex_path;
	int status;
	struct ec_sensors_data *state = dev_get_drvdata(dev);

	mutex_path = mutex_path_override ?
		mutex_path_override : state->board_info->mutex_path;

	if (!mutex_path || !strlen(mutex_path)) {
		dev_err(dev, "Hardware access guard mutex name is empty");
		return -EINVAL;
	}
	if (!strcmp(mutex_path, ACPI_GLOBAL_LOCK_PSEUDO_PATH)) {
		state->lock_data.mutex.glk = 0;
		state->lock_data.lock = lock_via_global_acpi_lock;
		state->lock_data.unlock = unlock_global_acpi_lock;
	} else {
		status = acpi_get_handle(NULL, (acpi_string)mutex_path,
					 &state->lock_data.mutex.aml);
		if (ACPI_FAILURE(status)) {
			dev_err(dev,
				"Failed to get hardware access guard AML mutex '%s': error %d",
				mutex_path, status);
			return -ENOENT;
		}
		state->lock_data.lock = lock_via_acpi_mutex;
		state->lock_data.unlock = unlock_acpi_mutex;
	}
	return 0;
}

static int asus_ec_bank_switch(u8 bank, u8 *old)
{
	int status = 0;

	if (old) {
		status = ec_read(ASUS_EC_BANK_REGISTER, old);
	}
	if (status || (old && (*old == bank)))
		return status;
	return ec_write(ASUS_EC_BANK_REGISTER, bank);
}

static int asus_ec_block_read(const struct device *dev,
			      struct ec_sensors_data *ec)
{
	int ireg, ibank, status;
	u8 bank, reg_bank, prev_bank;

	bank = 0;
	status = asus_ec_bank_switch(bank, &prev_bank);
	if (status) {
		dev_warn(dev, "EC bank switch failed");
		return status;
	}

	if (prev_bank) {
		/* oops... somebody else is working with the EC too */
		dev_warn(dev,
			"Concurrent access to the ACPI EC detected.\nRace condition possible.");
	}

	/* read registers minimizing bank switches. */
	for (ibank = 0; ibank < ec->nr_banks; ibank++) {
		if (bank != ec->banks[ibank]) {
			bank = ec->banks[ibank];
			if (asus_ec_bank_switch(bank, NULL)) {
				dev_warn(dev, "EC bank switch to %d failed",
					 bank);
				break;
			}
		}
		for (ireg = 0; ireg < ec->nr_registers; ireg++) {
			reg_bank = register_bank(ec->registers[ireg]);
			if (reg_bank < bank) {
				continue;
			}
			ec_read(register_index(ec->registers[ireg]),
				ec->read_buffer + ireg);
		}
	}

	status = asus_ec_bank_switch(prev_bank, NULL);
	return status;
}

static inline s32 get_sensor_value(const struct ec_sensor_info *si, u8 *data)
{
	if (is_sensor_data_signed(si)) {
		switch (si->addr.components.size) {
		case 1:
			return (s8)*data;
		case 2:
			return (s16)get_unaligned_be16(data);
		case 4:
			return (s32)get_unaligned_be32(data);
		default:
			return 0;
		}
	} else {
		switch (si->addr.components.size) {
		case 1:
			return *data;
		case 2:
			return get_unaligned_be16(data);
		case 4:
			return get_unaligned_be32(data);
		default:
			return 0;
		}
	}
}

static void update_sensor_values(struct ec_sensors_data *ec, u8 *data)
{
	const struct ec_sensor_info *si;
	struct ec_sensor *s, *sensor_end;

	sensor_end = ec->sensors + ec->nr_sensors;
	for (s = ec->sensors; s != sensor_end; s++) {
		si = ec->sensors_info + s->info_index;
		s->cached_value = get_sensor_value(si, data);
		data += si->addr.components.size;
	}
}

static int update_ec_sensors(const struct device *dev,
			     struct ec_sensors_data *ec)
{
	int status;

	if (!ec->lock_data.lock(&ec->lock_data)) {
		dev_warn(dev, "Failed to acquire mutex");
		return -EBUSY;
	}

	status = asus_ec_block_read(dev, ec);

	if (!status) {
		update_sensor_values(ec, ec->read_buffer);
	}

	if (!ec->lock_data.unlock(&ec->lock_data))
		dev_err(dev, "Failed to release mutex");

	return status;
}

static long scale_sensor_value(s32 value, int data_type)
{
	switch (data_type) {
	case hwmon_curr:
	case hwmon_temp:
		return value * MILLI;
	default:
		return value;
	}
}

static int get_cached_value_or_update(const struct device *dev,
				      int sensor_index,
				      struct ec_sensors_data *state, s32 *value)
{
	if (time_after(jiffies, state->last_updated + HZ)) {
		if (update_ec_sensors(dev, state)) {
			dev_err(dev, "update_ec_sensors() failure\n");
			return -EIO;
		}

		state->last_updated = jiffies;
	}

	*value = state->sensors[sensor_index].cached_value;
	return 0;
}

/*
 * Now follow the functions that implement the hwmon interface
 */

static int asus_ec_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			      u32 attr, int channel, long *val)
{
	int ret;
	s32 value = 0;

	struct ec_sensors_data *state = dev_get_drvdata(dev);
	int sidx = find_ec_sensor_index(state, type, channel);

	if (sidx < 0) {
		return sidx;
	}

	ret = get_cached_value_or_update(dev, sidx, state, &value);
	if (!ret) {
		*val = scale_sensor_value(value,
					  get_sensor_info(state, sidx)->type);
	}

	return ret;
}

static int asus_ec_hwmon_read_string(struct device *dev,
				     enum hwmon_sensor_types type, u32 attr,
				     int channel, const char **str)
{
	struct ec_sensors_data *state = dev_get_drvdata(dev);
	int sensor_index = find_ec_sensor_index(state, type, channel);
	*str = get_sensor_info(state, sensor_index)->label;

	return 0;
}

static umode_t asus_ec_hwmon_is_visible(const void *drvdata,
					enum hwmon_sensor_types type, u32 attr,
					int channel)
{
	const struct ec_sensors_data *state = drvdata;

	return find_ec_sensor_index(state, type, channel) >= 0 ? S_IRUGO : 0;
}

static int
asus_ec_hwmon_add_chan_info(struct hwmon_channel_info *asus_ec_hwmon_chan,
			     struct device *dev, int num,
			     enum hwmon_sensor_types type, u32 config)
{
	int i;
	u32 *cfg = devm_kcalloc(dev, num + 1, sizeof(*cfg), GFP_KERNEL);

	if (!cfg)
		return -ENOMEM;

	asus_ec_hwmon_chan->type = type;
	asus_ec_hwmon_chan->config = cfg;
	for (i = 0; i < num; i++, cfg++)
		*cfg = config;

	return 0;
}

static const struct hwmon_ops asus_ec_hwmon_ops = {
	.is_visible = asus_ec_hwmon_is_visible,
	.read = asus_ec_hwmon_read,
	.read_string = asus_ec_hwmon_read_string,
};

static struct hwmon_chip_info asus_ec_chip_info = {
	.ops = &asus_ec_hwmon_ops,
};

static const struct ec_board_info *get_board_info(void)
{
	const struct dmi_system_id *dmi_entry;

	dmi_entry = dmi_first_match(dmi_table);
	return dmi_entry ? dmi_entry->driver_data : NULL;
}

static int asus_ec_probe(struct platform_device *pdev)
{
	const struct hwmon_channel_info **ptr_asus_ec_ci;
	int nr_count[hwmon_max] = { 0 }, nr_types = 0;
	struct hwmon_channel_info *asus_ec_hwmon_chan;
	const struct ec_board_info *pboard_info;
	const struct hwmon_chip_info *chip_info;
	struct device *dev = &pdev->dev;
	struct ec_sensors_data *ec_data;
	const struct ec_sensor_info *si;
	enum hwmon_sensor_types type;
	struct device *hwdev;
	unsigned int i;
	int status;

	pboard_info = get_board_info();
	if (!pboard_info)
		return -ENODEV;

	ec_data = devm_kzalloc(dev, sizeof(struct ec_sensors_data),
			       GFP_KERNEL);
	if (!ec_data)
		return -ENOMEM;

	dev_set_drvdata(dev, ec_data);
	ec_data->board_info = pboard_info;

	switch (ec_data->board_info->family) {
	case family_amd_400_series:
		ec_data->sensors_info = sensors_family_amd_400;
		break;
	case family_amd_500_series:
		ec_data->sensors_info = sensors_family_amd_500;
		break;
	case family_amd_600_series:
		ec_data->sensors_info = sensors_family_amd_600;
		break;
	case family_intel_300_series:
		ec_data->sensors_info = sensors_family_intel_300;
		break;
	case family_intel_600_series:
		ec_data->sensors_info = sensors_family_intel_600;
		break;
	default:
		dev_err(dev, "Unknown board family: %d",
			ec_data->board_info->family);
		return -EINVAL;
	}

	ec_data->nr_sensors = hweight_long(ec_data->board_info->sensors);
	ec_data->sensors = devm_kcalloc(dev, ec_data->nr_sensors,
					sizeof(struct ec_sensor), GFP_KERNEL);
	if (!ec_data->sensors)
		return -ENOMEM;

	status = setup_lock_data(dev);
	if (status) {
		dev_err(dev, "Failed to setup state/EC locking: %d", status);
		return status;
	}

	setup_sensor_data(ec_data);
	ec_data->registers = devm_kcalloc(dev, ec_data->nr_registers,
					  sizeof(u16), GFP_KERNEL);
	ec_data->read_buffer = devm_kcalloc(dev, ec_data->nr_registers,
					    sizeof(u8), GFP_KERNEL);

	if (!ec_data->registers || !ec_data->read_buffer)
		return -ENOMEM;

	fill_ec_registers(ec_data);

	for (i = 0; i < ec_data->nr_sensors; ++i) {
		si = get_sensor_info(ec_data, i);
		if (!nr_count[si->type])
			++nr_types;
		++nr_count[si->type];
	}

	if (nr_count[hwmon_temp])
		nr_count[hwmon_chip]++, nr_types++;

	asus_ec_hwmon_chan = devm_kcalloc(
		dev, nr_types, sizeof(*asus_ec_hwmon_chan), GFP_KERNEL);
	if (!asus_ec_hwmon_chan)
		return -ENOMEM;

	ptr_asus_ec_ci = devm_kcalloc(dev, nr_types + 1,
				       sizeof(*ptr_asus_ec_ci), GFP_KERNEL);
	if (!ptr_asus_ec_ci)
		return -ENOMEM;

	asus_ec_chip_info.info = ptr_asus_ec_ci;
	chip_info = &asus_ec_chip_info;

	for (type = 0; type < hwmon_max; ++type) {
		if (!nr_count[type])
			continue;

		asus_ec_hwmon_add_chan_info(asus_ec_hwmon_chan, dev,
					     nr_count[type], type,
					     hwmon_attributes[type]);
		*ptr_asus_ec_ci++ = asus_ec_hwmon_chan++;
	}

	dev_info(dev, "board has %d EC sensors that span %d registers",
		 ec_data->nr_sensors, ec_data->nr_registers);

	hwdev = devm_hwmon_device_register_with_info(dev, "asusec",
						     ec_data, chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwdev);
}

MODULE_DEVICE_TABLE(dmi, dmi_table);

static struct platform_driver asus_ec_sensors_platform_driver = {
	.driver = {
		.name	= "asus-ec-sensors",
	},
	.probe = asus_ec_probe,
};

static struct platform_device *asus_ec_sensors_platform_device;

static int __init asus_ec_init(void)
{
	asus_ec_sensors_platform_device =
		platform_create_bundle(&asus_ec_sensors_platform_driver,
				       asus_ec_probe, NULL, 0, NULL, 0);

	if (IS_ERR(asus_ec_sensors_platform_device))
		return PTR_ERR(asus_ec_sensors_platform_device);

	return 0;
}

static void __exit asus_ec_exit(void)
{
	platform_device_unregister(asus_ec_sensors_platform_device);
	platform_driver_unregister(&asus_ec_sensors_platform_driver);
}

module_init(asus_ec_init);
module_exit(asus_ec_exit);

module_param_named(mutex_path, mutex_path_override, charp, 0);
MODULE_PARM_DESC(mutex_path,
		 "Override ACPI mutex path used to guard access to hardware");

MODULE_AUTHOR("Eugene Shalygin <eugene.shalygin@gmail.com>");
MODULE_DESCRIPTION(
	"HWMON driver for sensors accessible via ACPI EC in ASUS motherboards");
MODULE_LICENSE("GPL");
