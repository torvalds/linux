// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MAX 10 BMC HWMON Driver
 *
 * Copyright (C) 2018-2020 Intel Corporation. All rights reserved.
 *
 */
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/mfd/intel-m10-bmc.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

struct m10bmc_sdata {
	unsigned int reg_input;
	unsigned int reg_max;
	unsigned int reg_crit;
	unsigned int reg_hyst;
	unsigned int reg_min;
	unsigned int multiplier;
	const char *label;
};

struct m10bmc_hwmon_board_data {
	const struct m10bmc_sdata *tables[hwmon_max];
	const struct hwmon_channel_info * const *hinfo;
};

struct m10bmc_hwmon {
	struct device *dev;
	struct hwmon_chip_info chip;
	char *hw_name;
	struct intel_m10bmc *m10bmc;
	const struct m10bmc_hwmon_board_data *bdata;
};

static const struct m10bmc_sdata n3000bmc_temp_tbl[] = {
	{ 0x100, 0x104, 0x108, 0x10c, 0x0, 500, "Board Temperature" },
	{ 0x110, 0x114, 0x118, 0x0, 0x0, 500, "FPGA Die Temperature" },
	{ 0x11c, 0x124, 0x120, 0x0, 0x0, 500, "QSFP0 Temperature" },
	{ 0x12c, 0x134, 0x130, 0x0, 0x0, 500, "QSFP1 Temperature" },
	{ 0x168, 0x0, 0x0, 0x0, 0x0, 500, "Retimer A Temperature" },
	{ 0x16c, 0x0, 0x0, 0x0, 0x0, 500, "Retimer A SerDes Temperature" },
	{ 0x170, 0x0, 0x0, 0x0, 0x0, 500, "Retimer B Temperature" },
	{ 0x174, 0x0, 0x0, 0x0, 0x0, 500, "Retimer B SerDes Temperature" },
};

static const struct m10bmc_sdata n3000bmc_in_tbl[] = {
	{ 0x128, 0x0, 0x0, 0x0, 0x0, 1, "QSFP0 Supply Voltage" },
	{ 0x138, 0x0, 0x0, 0x0, 0x0, 1, "QSFP1 Supply Voltage" },
	{ 0x13c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Voltage" },
	{ 0x144, 0x0, 0x0, 0x0, 0x0, 1, "12V Backplane Voltage" },
	{ 0x14c, 0x0, 0x0, 0x0, 0x0, 1, "1.2V Voltage" },
	{ 0x150, 0x0, 0x0, 0x0, 0x0, 1, "12V AUX Voltage" },
	{ 0x158, 0x0, 0x0, 0x0, 0x0, 1, "1.8V Voltage" },
	{ 0x15c, 0x0, 0x0, 0x0, 0x0, 1, "3.3V Voltage" },
};

static const struct m10bmc_sdata n3000bmc_curr_tbl[] = {
	{ 0x140, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Current" },
	{ 0x148, 0x0, 0x0, 0x0, 0x0, 1, "12V Backplane Current" },
	{ 0x154, 0x0, 0x0, 0x0, 0x0, 1, "12V AUX Current" },
};

static const struct m10bmc_sdata n3000bmc_power_tbl[] = {
	{ 0x160, 0x0, 0x0, 0x0, 0x0, 1000, "Board Power" },
};

static const struct hwmon_channel_info * const n3000bmc_hinfo[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct m10bmc_sdata d5005bmc_temp_tbl[] = {
	{ 0x100, 0x104, 0x108, 0x10c, 0x0, 500, "Board Inlet Air Temperature" },
	{ 0x110, 0x114, 0x118, 0x0, 0x0, 500, "FPGA Core Temperature" },
	{ 0x11c, 0x120, 0x124, 0x128, 0x0, 500, "Board Exhaust Air Temperature" },
	{ 0x12c, 0x130, 0x134, 0x0, 0x0, 500, "FPGA Transceiver Temperature" },
	{ 0x138, 0x13c, 0x140, 0x144, 0x0, 500, "RDIMM0 Temperature" },
	{ 0x148, 0x14c, 0x150, 0x154, 0x0, 500, "RDIMM1 Temperature" },
	{ 0x158, 0x15c, 0x160, 0x164, 0x0, 500, "RDIMM2 Temperature" },
	{ 0x168, 0x16c, 0x170, 0x174, 0x0, 500, "RDIMM3 Temperature" },
	{ 0x178, 0x17c, 0x180, 0x0, 0x0, 500, "QSFP0 Temperature" },
	{ 0x188, 0x18c, 0x190, 0x0, 0x0, 500, "QSFP1 Temperature" },
	{ 0x1a0, 0x1a4, 0x1a8, 0x0, 0x0, 500, "3.3v Temperature" },
	{ 0x1bc, 0x1c0, 0x1c4, 0x0, 0x0, 500, "VCCERAM Temperature" },
	{ 0x1d8, 0x1dc, 0x1e0, 0x0, 0x0, 500, "VCCR Temperature" },
	{ 0x1f4, 0x1f8, 0x1fc, 0x0, 0x0, 500, "VCCT Temperature" },
	{ 0x210, 0x214, 0x218, 0x0, 0x0, 500, "1.8v Temperature" },
	{ 0x22c, 0x230, 0x234, 0x0, 0x0, 500, "12v Backplane Temperature" },
	{ 0x248, 0x24c, 0x250, 0x0, 0x0, 500, "12v AUX Temperature" },
};

static const struct m10bmc_sdata d5005bmc_in_tbl[] = {
	{ 0x184, 0x0, 0x0, 0x0, 0x0, 1, "QSFP0 Supply Voltage" },
	{ 0x194, 0x0, 0x0, 0x0, 0x0, 1, "QSFP1 Supply Voltage" },
	{ 0x198, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Voltage" },
	{ 0x1ac, 0x1b0, 0x1b4, 0x0, 0x0, 1, "3.3v Voltage" },
	{ 0x1c8, 0x1cc, 0x1d0, 0x0, 0x0, 1, "VCCERAM Voltage" },
	{ 0x1e4, 0x1e8, 0x1ec, 0x0, 0x0, 1, "VCCR Voltage" },
	{ 0x200, 0x204, 0x208, 0x0, 0x0, 1, "VCCT Voltage" },
	{ 0x21c, 0x220, 0x224, 0x0, 0x0, 1, "1.8v Voltage" },
	{ 0x238, 0x0, 0x0, 0x0, 0x23c, 1, "12v Backplane Voltage" },
	{ 0x254, 0x0, 0x0, 0x0, 0x258, 1, "12v AUX Voltage" },
};

static const struct m10bmc_sdata d5005bmc_curr_tbl[] = {
	{ 0x19c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Current" },
	{ 0x1b8, 0x0, 0x0, 0x0, 0x0, 1, "3.3v Current" },
	{ 0x1d4, 0x0, 0x0, 0x0, 0x0, 1, "VCCERAM Current" },
	{ 0x1f0, 0x0, 0x0, 0x0, 0x0, 1, "VCCR Current" },
	{ 0x20c, 0x0, 0x0, 0x0, 0x0, 1, "VCCT Current" },
	{ 0x228, 0x0, 0x0, 0x0, 0x0, 1, "1.8v Current" },
	{ 0x240, 0x244, 0x0, 0x0, 0x0, 1, "12v Backplane Current" },
	{ 0x25c, 0x260, 0x0, 0x0, 0x0, 1, "12v AUX Current" },
};

static const struct m10bmc_hwmon_board_data n3000bmc_hwmon_bdata = {
	.tables = {
		[hwmon_temp] = n3000bmc_temp_tbl,
		[hwmon_in] = n3000bmc_in_tbl,
		[hwmon_curr] = n3000bmc_curr_tbl,
		[hwmon_power] = n3000bmc_power_tbl,
	},

	.hinfo = n3000bmc_hinfo,
};

static const struct hwmon_channel_info * const d5005bmc_hinfo[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_CRIT |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_CRIT |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_CRIT |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_CRIT |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_CRIT |
			   HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_MIN | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_LABEL),
	NULL
};

static const struct m10bmc_hwmon_board_data d5005bmc_hwmon_bdata = {
	.tables = {
		[hwmon_temp] = d5005bmc_temp_tbl,
		[hwmon_in] = d5005bmc_in_tbl,
		[hwmon_curr] = d5005bmc_curr_tbl,
	},

	.hinfo = d5005bmc_hinfo,
};

static const struct m10bmc_sdata n5010bmc_temp_tbl[] = {
	{ 0x100, 0x0, 0x104, 0x0, 0x0, 1000, "Board Local Temperature" },
	{ 0x108, 0x0, 0x10c, 0x0, 0x0, 1000, "FPGA 1 Temperature" },
	{ 0x110, 0x0, 0x114, 0x0, 0x0, 1000, "FPGA 2 Temperature" },
	{ 0x118, 0x0, 0x0, 0x0, 0x0, 1000, "Card Top Temperature" },
	{ 0x11c, 0x0, 0x0, 0x0, 0x0, 1000, "Card Bottom Temperature" },
	{ 0x128, 0x0, 0x0, 0x0, 0x0, 1000, "FPGA 1.2V Temperature" },
	{ 0x134, 0x0, 0x0, 0x0, 0x0, 1000, "FPGA 5V Temperature" },
	{ 0x140, 0x0, 0x0, 0x0, 0x0, 1000, "FPGA 0.9V Temperature" },
	{ 0x14c, 0x0, 0x0, 0x0, 0x0, 1000, "FPGA 0.85V Temperature" },
	{ 0x158, 0x0, 0x0, 0x0, 0x0, 1000, "AUX 12V Temperature" },
	{ 0x164, 0x0, 0x0, 0x0, 0x0, 1000, "Backplane 12V Temperature" },
	{ 0x1a8, 0x0, 0x0, 0x0, 0x0, 1000, "QSFP28-1 Temperature" },
	{ 0x1ac, 0x0, 0x0, 0x0, 0x0, 1000, "QSFP28-2 Temperature" },
	{ 0x1b0, 0x0, 0x0, 0x0, 0x0, 1000, "QSFP28-3 Temperature" },
	{ 0x1b4, 0x0, 0x0, 0x0, 0x0, 1000, "QSFP28-4 Temperature" },
	{ 0x1b8, 0x0, 0x0, 0x0, 0x0, 1000, "CVL1 Internal Temperature" },
	{ 0x1bc, 0x0, 0x0, 0x0, 0x0, 1000, "CVL2 Internal Temperature" },
};

static const struct m10bmc_sdata n5010bmc_in_tbl[] = {
	{ 0x120, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 1.2V Voltage" },
	{ 0x12c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 5V Voltage" },
	{ 0x138, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 0.9V Voltage" },
	{ 0x144, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 0.85V Voltage" },
	{ 0x150, 0x0, 0x0, 0x0, 0x0, 1, "AUX 12V Voltage" },
	{ 0x15c, 0x0, 0x0, 0x0, 0x0, 1, "Backplane 12V Voltage" },
	{ 0x16c, 0x0, 0x0, 0x0, 0x0, 1, "DDR4 1.2V Voltage" },
	{ 0x17c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 1.8V Voltage" },
	{ 0x184, 0x0, 0x0, 0x0, 0x0, 1, "QDR 1.3V Voltage" },
	{ 0x18c, 0x0, 0x0, 0x0, 0x0, 1, "CVL1 0.8V Voltage" },
	{ 0x194, 0x0, 0x0, 0x0, 0x0, 1, "CVL1 1.05V Voltage" },
	{ 0x19c, 0x0, 0x0, 0x0, 0x0, 1, "CVL2 1.05V Voltage" },
	{ 0x1a4, 0x0, 0x0, 0x0, 0x0, 1, "CVL2 0.8V Voltage" },
};

static const struct m10bmc_sdata n5010bmc_curr_tbl[] = {
	{ 0x124, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 1.2V Current" },
	{ 0x130, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 5V Current" },
	{ 0x13c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 0.9V Current" },
	{ 0x148, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 0.85V Current" },
	{ 0x154, 0x0, 0x0, 0x0, 0x0, 1, "AUX 12V Current" },
	{ 0x160, 0x0, 0x0, 0x0, 0x0, 1, "Backplane 12V Current" },
	{ 0x168, 0x0, 0x0, 0x0, 0x0, 1, "DDR4 1.2V Current" },
	{ 0x178, 0x0, 0x0, 0x0, 0x0, 1, "FPGA 1.8V Current" },
	{ 0x180, 0x0, 0x0, 0x0, 0x0, 1, "QDR 1.3V Current" },
	{ 0x188, 0x0, 0x0, 0x0, 0x0, 1, "CVL1 0.8V Current" },
	{ 0x190, 0x0, 0x0, 0x0, 0x0, 1, "CVL1 1.05V Current" },
	{ 0x198, 0x0, 0x0, 0x0, 0x0, 1, "CVL2 1.05V Current" },
	{ 0x1a0, 0x0, 0x0, 0x0, 0x0, 1, "CVL2 0.8V Current" },
};

static const struct hwmon_channel_info * const n5010bmc_hinfo[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_CRIT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct m10bmc_hwmon_board_data n5010bmc_hwmon_bdata = {
	.tables = {
		[hwmon_temp] = n5010bmc_temp_tbl,
		[hwmon_in] = n5010bmc_in_tbl,
		[hwmon_curr] = n5010bmc_curr_tbl,
	},

	.hinfo = n5010bmc_hinfo,
};

static const struct m10bmc_sdata n6000bmc_temp_tbl[] = {
	{ 0x444, 0x448, 0x44c, 0x0, 0x0, 500, "FPGA E-TILE Temperature #1" },
	{ 0x450, 0x454, 0x458, 0x0, 0x0, 500, "FPGA E-TILE Temperature #2" },
	{ 0x45c, 0x460, 0x464, 0x0, 0x0, 500, "FPGA E-TILE Temperature #3" },
	{ 0x468, 0x46c, 0x470, 0x0, 0x0, 500, "FPGA E-TILE Temperature #4" },
	{ 0x474, 0x478, 0x47c, 0x0, 0x0, 500, "FPGA P-TILE Temperature" },
	{ 0x484, 0x488, 0x48c, 0x0, 0x0, 500, "FPGA FABRIC Digital Temperature #1" },
	{ 0x490, 0x494, 0x498, 0x0, 0x0, 500, "FPGA FABRIC Digital Temperature #2" },
	{ 0x49c, 0x4a0, 0x4a4, 0x0, 0x0, 500, "FPGA FABRIC Digital Temperature #3" },
	{ 0x4a8, 0x4ac, 0x4b0, 0x0, 0x0, 500, "FPGA FABRIC Digital Temperature #4" },
	{ 0x4b4, 0x4b8, 0x4bc, 0x0, 0x0, 500, "FPGA FABRIC Digital Temperature #5" },
	{ 0x4c0, 0x4c4, 0x4c8, 0x0, 0x0, 500, "FPGA FABRIC Remote Digital Temperature #1" },
	{ 0x4cc, 0x4d0, 0x4d4, 0x0, 0x0, 500, "FPGA FABRIC Remote Digital Temperature #2" },
	{ 0x4d8, 0x4dc, 0x4e0, 0x0, 0x0, 500, "FPGA FABRIC Remote Digital Temperature #3" },
	{ 0x4e4, 0x4e8, 0x4ec, 0x0, 0x0, 500, "FPGA FABRIC Remote Digital Temperature #4" },
	{ 0x4f0, 0x4f4, 0x4f8, 0x52c, 0x0, 500, "Board Top Near FPGA Temperature" },
	{ 0x4fc, 0x500, 0x504, 0x52c, 0x0, 500, "Board Bottom Near CVL Temperature" },
	{ 0x508, 0x50c, 0x510, 0x52c, 0x0, 500, "Board Top East Near VRs Temperature" },
	{ 0x514, 0x518, 0x51c, 0x52c, 0x0, 500, "CVL Die Temperature" },
	{ 0x520, 0x524, 0x528, 0x52c, 0x0, 500, "Board Rear Side Temperature" },
	{ 0x530, 0x534, 0x538, 0x52c, 0x0, 500, "Board Front Side Temperature" },
	{ 0x53c, 0x540, 0x544, 0x0, 0x0, 500, "QSFP1 Case Temperature" },
	{ 0x548, 0x54c, 0x550, 0x0, 0x0, 500, "QSFP2 Case Temperature" },
	{ 0x554, 0x0, 0x0, 0x0, 0x0, 500, "FPGA Core Voltage Phase 0 VR Temperature" },
	{ 0x560, 0x0, 0x0, 0x0, 0x0, 500, "FPGA Core Voltage Phase 1 VR Temperature" },
	{ 0x56c, 0x0, 0x0, 0x0, 0x0, 500, "FPGA Core Voltage Phase 2 VR Temperature" },
	{ 0x578, 0x0, 0x0, 0x0, 0x0, 500, "FPGA Core Voltage VR Controller Temperature" },
	{ 0x584, 0x0, 0x0, 0x0, 0x0, 500, "FPGA VCCH VR Temperature" },
	{ 0x590, 0x0, 0x0, 0x0, 0x0, 500, "FPGA VCC_1V2 VR Temperature" },
	{ 0x59c, 0x0, 0x0, 0x0, 0x0, 500, "FPGA VCCH, VCC_1V2 VR Controller Temperature" },
	{ 0x5a8, 0x0, 0x0, 0x0, 0x0, 500, "3V3 VR Temperature" },
	{ 0x5b4, 0x0, 0x0, 0x0, 0x0, 500, "CVL Core Voltage VR Temperature" },
	{ 0x5c4, 0x5c8, 0x5cc, 0x5c0, 0x0, 500, "FPGA P-Tile Temperature [Remote]" },
	{ 0x5d0, 0x5d4, 0x5d8, 0x5c0, 0x0, 500, "FPGA E-Tile Temperature [Remote]" },
	{ 0x5dc, 0x5e0, 0x5e4, 0x5c0, 0x0, 500, "FPGA SDM Temperature [Remote]" },
	{ 0x5e8, 0x5ec, 0x5f0, 0x5c0, 0x0, 500, "FPGA Corner Temperature [Remote]" },
};

static const struct m10bmc_sdata n6000bmc_in_tbl[] = {
	{ 0x5f4, 0x0, 0x0, 0x0, 0x0, 1, "Inlet 12V PCIe Rail Voltage" },
	{ 0x60c, 0x0, 0x0, 0x0, 0x0, 1, "Inlet 12V Aux Rail Voltage" },
	{ 0x624, 0x0, 0x0, 0x0, 0x0, 1, "Inlet 3V3 PCIe Rail Voltage" },
	{ 0x63c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Voltage Rail Voltage" },
	{ 0x644, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCCH Rail Voltage" },
	{ 0x64c, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCC_1V2 Rail Voltage" },
	{ 0x654, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCCH_GXER_1V1, VCCA_1V8 Voltage" },
	{ 0x664, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCCIO_1V2 Voltage" },
	{ 0x674, 0x0, 0x0, 0x0, 0x0, 1, "CVL Non Core Rails Inlet Voltage" },
	{ 0x684, 0x0, 0x0, 0x0, 0x0, 1, "MAX10 & Board CLK PWR 3V3 Inlet Voltage" },
	{ 0x694, 0x0, 0x0, 0x0, 0x0, 1, "CVL Core Voltage Rail Voltage" },
	{ 0x6ac, 0x0, 0x0, 0x0, 0x0, 1, "Board 3V3 VR Voltage" },
	{ 0x6b4, 0x0, 0x0, 0x0, 0x0, 1, "QSFP 3V3 Rail Voltage" },
	{ 0x6c4, 0x0, 0x0, 0x0, 0x0, 1, "QSFP (Primary) Supply Rail Voltage" },
	{ 0x6c8, 0x0, 0x0, 0x0, 0x0, 1, "QSFP (Secondary) Supply Rail Voltage" },
	{ 0x6cc, 0x0, 0x0, 0x0, 0x0, 1, "VCCCLK_GXER_2V5 Voltage" },
	{ 0x6d0, 0x0, 0x0, 0x0, 0x0, 1, "AVDDH_1V1_CVL Voltage" },
	{ 0x6d4, 0x0, 0x0, 0x0, 0x0, 1, "VDDH_1V8_CVL Voltage" },
	{ 0x6d8, 0x0, 0x0, 0x0, 0x0, 1, "VCCA_PLL Voltage" },
	{ 0x6e0, 0x0, 0x0, 0x0, 0x0, 1, "VCCRT_GXER_0V9 Voltage" },
	{ 0x6e8, 0x0, 0x0, 0x0, 0x0, 1, "VCCRT_GXPL_0V9 Voltage" },
	{ 0x6f0, 0x0, 0x0, 0x0, 0x0, 1, "VCCH_GXPL_1V8 Voltage" },
	{ 0x6f4, 0x0, 0x0, 0x0, 0x0, 1, "VCCPT_1V8 Voltage" },
	{ 0x6fc, 0x0, 0x0, 0x0, 0x0, 1, "VCC_3V3_M10 Voltage" },
	{ 0x700, 0x0, 0x0, 0x0, 0x0, 1, "VCC_1V8_M10 Voltage" },
	{ 0x704, 0x0, 0x0, 0x0, 0x0, 1, "VCC_1V2_EMIF1_2_3 Voltage" },
	{ 0x70c, 0x0, 0x0, 0x0, 0x0, 1, "VCC_1V2_EMIF4_5 Voltage" },
	{ 0x714, 0x0, 0x0, 0x0, 0x0, 1, "VCCA_1V8 Voltage" },
	{ 0x718, 0x0, 0x0, 0x0, 0x0, 1, "VCCH_GXER_1V1 Voltage" },
	{ 0x71c, 0x0, 0x0, 0x0, 0x0, 1, "AVDD_ETH_0V9_CVL Voltage" },
	{ 0x720, 0x0, 0x0, 0x0, 0x0, 1, "AVDD_PCIE_0V9_CVL Voltage" },
};

static const struct m10bmc_sdata n6000bmc_curr_tbl[] = {
	{ 0x600, 0x604, 0x608, 0x0, 0x0, 1, "Inlet 12V PCIe Rail Current" },
	{ 0x618, 0x61c, 0x620, 0x0, 0x0, 1, "Inlet 12V Aux Rail Current" },
	{ 0x630, 0x634, 0x638, 0x0, 0x0, 1, "Inlet 3V3 PCIe Rail Current" },
	{ 0x640, 0x0, 0x0, 0x0, 0x0, 1, "FPGA Core Voltage Rail Current" },
	{ 0x648, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCCH Rail Current" },
	{ 0x650, 0x0, 0x0, 0x0, 0x0, 1, "FPGA VCC_1V2 Rail Current" },
	{ 0x658, 0x65c, 0x660, 0x0, 0x0, 1, "FPGA VCCH_GXER_1V1, VCCA_1V8 Current" },
	{ 0x668, 0x66c, 0x670, 0x0, 0x0, 1, "FPGA VCCIO_1V2 Current" },
	{ 0x678, 0x67c, 0x680, 0x0, 0x0, 1, "CVL Non Core Rails Inlet Current" },
	{ 0x688, 0x68c, 0x690, 0x0, 0x0, 1, "MAX10 & Board CLK PWR 3V3 Inlet Current" },
	{ 0x698, 0x0, 0x0, 0x0, 0x0, 1, "CVL Core Voltage Rail Current" },
	{ 0x6b0, 0x0, 0x0, 0x0, 0x0, 1, "Board 3V3 VR Current" },
	{ 0x6b8, 0x6bc, 0x6c0, 0x0, 0x0, 1, "QSFP 3V3 Rail Current" },
};

static const struct m10bmc_sdata n6000bmc_power_tbl[] = {
	{ 0x724, 0x0, 0x0, 0x0, 0x0, 1000, "Board Power" },
};

static const struct hwmon_channel_info * const n6000bmc_hinfo[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_CRIT |
			   HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST |
			   HWMON_T_CRIT | HWMON_T_CRIT_HYST | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_CRIT |
			   HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	NULL
};

static const struct m10bmc_hwmon_board_data n6000bmc_hwmon_bdata = {
	.tables = {
		[hwmon_temp] = n6000bmc_temp_tbl,
		[hwmon_in] = n6000bmc_in_tbl,
		[hwmon_curr] = n6000bmc_curr_tbl,
		[hwmon_power] = n6000bmc_power_tbl,
	},

	.hinfo = n6000bmc_hinfo,
};

static umode_t
m10bmc_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
			u32 attr, int channel)
{
	return 0444;
}

static const struct m10bmc_sdata *
find_sensor_data(struct m10bmc_hwmon *hw, enum hwmon_sensor_types type,
		 int channel)
{
	const struct m10bmc_sdata *tbl;

	tbl = hw->bdata->tables[type];
	if (!tbl)
		return ERR_PTR(-EOPNOTSUPP);

	return &tbl[channel];
}

static int do_sensor_read(struct m10bmc_hwmon *hw,
			  const struct m10bmc_sdata *data,
			  unsigned int regoff, long *val)
{
	unsigned int regval;
	int ret;

	ret = m10bmc_sys_read(hw->m10bmc, regoff, &regval);
	if (ret)
		return ret;

	/*
	 * BMC Firmware will return 0xdeadbeef if the sensor value is invalid
	 * at that time. This usually happens on sensor channels which connect
	 * to external pluggable modules, e.g. QSFP temperature and voltage.
	 * When the QSFP is unplugged from cage, driver will get 0xdeadbeef
	 * from their registers.
	 */
	if (regval == 0xdeadbeef)
		return -ENODATA;

	*val = regval * data->multiplier;

	return 0;
}

static int m10bmc_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *val)
{
	struct m10bmc_hwmon *hw = dev_get_drvdata(dev);
	unsigned int reg = 0, reg_hyst = 0;
	const struct m10bmc_sdata *data;
	long hyst, value;
	int ret;

	data = find_sensor_data(hw, type, channel);
	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			reg = data->reg_input;
			break;
		case hwmon_temp_max_hyst:
			reg_hyst = data->reg_hyst;
			fallthrough;
		case hwmon_temp_max:
			reg = data->reg_max;
			break;
		case hwmon_temp_crit_hyst:
			reg_hyst = data->reg_hyst;
			fallthrough;
		case hwmon_temp_crit:
			reg = data->reg_crit;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			reg = data->reg_input;
			break;
		case hwmon_in_max:
			reg = data->reg_max;
			break;
		case hwmon_in_crit:
			reg = data->reg_crit;
			break;
		case hwmon_in_min:
			reg = data->reg_min;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			reg = data->reg_input;
			break;
		case hwmon_curr_max:
			reg = data->reg_max;
			break;
		case hwmon_curr_crit:
			reg = data->reg_crit;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			reg = data->reg_input;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	if (!reg)
		return -EOPNOTSUPP;

	ret = do_sensor_read(hw, data, reg, &value);
	if (ret)
		return ret;

	if (reg_hyst) {
		ret = do_sensor_read(hw, data, reg_hyst, &hyst);
		if (ret)
			return ret;

		value -= hyst;
	}

	*val = value;

	return 0;
}

static int m10bmc_hwmon_read_string(struct device *dev,
				    enum hwmon_sensor_types type,
				    u32 attr, int channel, const char **str)
{
	struct m10bmc_hwmon *hw = dev_get_drvdata(dev);
	const struct m10bmc_sdata *data;

	data = find_sensor_data(hw, type, channel);
	if (IS_ERR(data))
		return PTR_ERR(data);

	*str = data->label;

	return 0;
}

static const struct hwmon_ops m10bmc_hwmon_ops = {
	.is_visible = m10bmc_hwmon_is_visible,
	.read = m10bmc_hwmon_read,
	.read_string = m10bmc_hwmon_read_string,
};

static int m10bmc_hwmon_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct intel_m10bmc *m10bmc = dev_get_drvdata(pdev->dev.parent);
	struct device *hwmon_dev, *dev = &pdev->dev;
	struct m10bmc_hwmon *hw;

	hw = devm_kzalloc(dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	hw->dev = dev;
	hw->m10bmc = m10bmc;
	hw->bdata = (const struct m10bmc_hwmon_board_data *)id->driver_data;

	hw->chip.info = hw->bdata->hinfo;
	hw->chip.ops = &m10bmc_hwmon_ops;

	hw->hw_name = devm_hwmon_sanitize_name(dev, id->name);
	if (IS_ERR(hw->hw_name))
		return PTR_ERR(hw->hw_name);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, hw->hw_name,
							 hw, &hw->chip, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct platform_device_id intel_m10bmc_hwmon_ids[] = {
	{
		.name = "n3000bmc-hwmon",
		.driver_data = (unsigned long)&n3000bmc_hwmon_bdata,
	},
	{
		.name = "d5005bmc-hwmon",
		.driver_data = (unsigned long)&d5005bmc_hwmon_bdata,
	},
	{
		.name = "n5010bmc-hwmon",
		.driver_data = (unsigned long)&n5010bmc_hwmon_bdata,
	},
	{
		.name = "n6000bmc-hwmon",
		.driver_data = (unsigned long)&n6000bmc_hwmon_bdata,
	},
	{ }
};

static struct platform_driver intel_m10bmc_hwmon_driver = {
	.probe = m10bmc_hwmon_probe,
	.driver = {
		.name = "intel-m10-bmc-hwmon",
	},
	.id_table = intel_m10bmc_hwmon_ids,
};
module_platform_driver(intel_m10bmc_hwmon_driver);

MODULE_DEVICE_TABLE(platform, intel_m10bmc_hwmon_ids);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel MAX 10 BMC hardware monitor");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(INTEL_M10_BMC_CORE);
