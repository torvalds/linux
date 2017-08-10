/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef PP_ATOMFWCTRL_H
#define PP_ATOMFWCTRL_H

#include "hwmgr.h"

typedef enum atom_smu9_syspll0_clock_id BIOS_CLKID;

#define GetIndexIntoMasterCmdTable(FieldName) \
	(((char*)(&((struct atom_master_list_of_command_functions_v2_1*)0)->FieldName)-(char*)0)/sizeof(uint16_t))
#define GetIndexIntoMasterDataTable(FieldName) \
	(((char*)(&((struct atom_master_list_of_data_tables_v2_1*)0)->FieldName)-(char*)0)/sizeof(uint16_t))

#define PP_ATOMFWCTRL_MAX_VOLTAGE_ENTRIES 32

struct pp_atomfwctrl_voltage_table_entry {
	uint16_t value;
	uint32_t  smio_low;
};

struct pp_atomfwctrl_voltage_table {
	uint32_t count;
	uint32_t mask_low;
	uint32_t phase_delay;
	uint8_t psi0_enable;
	uint8_t psi1_enable;
	uint8_t max_vid_step;
	uint8_t telemetry_offset;
	uint8_t telemetry_slope;
	struct pp_atomfwctrl_voltage_table_entry entries[PP_ATOMFWCTRL_MAX_VOLTAGE_ENTRIES];
};

struct pp_atomfwctrl_gpio_pin_assignment {
	uint16_t us_gpio_pin_aindex;
	uint8_t uc_gpio_pin_bit_shift;
};

struct pp_atomfwctrl_clock_dividers_soc15 {
	uint32_t   ulClock;           /* the actual clock */
	uint32_t   ulDid;             /* DFS divider */
	uint32_t   ulPll_fb_mult;     /* Feedback Multiplier:  bit 8:0 int, bit 15:12 post_div, bit 31:16 frac */
	uint32_t   ulPll_ss_fbsmult;  /* Spread FB Multiplier: bit 8:0 int, bit 31:16 frac */
	uint16_t   usPll_ss_slew_frac;
	uint8_t    ucPll_ss_enable;
	uint8_t    ucReserve;
	uint32_t   ulReserve[2];
};

struct pp_atomfwctrl_avfs_parameters {
	uint32_t   ulMaxVddc;
	uint32_t   ulMinVddc;

	uint32_t   ulMeanNsigmaAcontant0;
	uint32_t   ulMeanNsigmaAcontant1;
	uint32_t   ulMeanNsigmaAcontant2;
	uint16_t   usMeanNsigmaDcTolSigma;
	uint16_t   usMeanNsigmaPlatformMean;
	uint16_t   usMeanNsigmaPlatformSigma;
	uint32_t   ulGbVdroopTableCksoffA0;
	uint32_t   ulGbVdroopTableCksoffA1;
	uint32_t   ulGbVdroopTableCksoffA2;
	uint32_t   ulGbVdroopTableCksonA0;
	uint32_t   ulGbVdroopTableCksonA1;
	uint32_t   ulGbVdroopTableCksonA2;

	uint32_t   ulGbFuseTableCksoffM1;
	uint32_t   ulGbFuseTableCksoffM2;
	uint32_t   ulGbFuseTableCksoffB;

	uint32_t   ulGbFuseTableCksonM1;
	uint32_t   ulGbFuseTableCksonM2;
	uint32_t   ulGbFuseTableCksonB;

	uint8_t    ucEnableGbVdroopTableCkson;
	uint8_t    ucEnableGbFuseTableCkson;
	uint16_t   usPsmAgeComfactor;

	uint32_t   ulDispclk2GfxclkM1;
	uint32_t   ulDispclk2GfxclkM2;
	uint32_t   ulDispclk2GfxclkB;
	uint32_t   ulDcefclk2GfxclkM1;
	uint32_t   ulDcefclk2GfxclkM2;
	uint32_t   ulDcefclk2GfxclkB;
	uint32_t   ulPixelclk2GfxclkM1;
	uint32_t   ulPixelclk2GfxclkM2;
	uint32_t   ulPixelclk2GfxclkB;
	uint32_t   ulPhyclk2GfxclkM1;
	uint32_t   ulPhyclk2GfxclkM2;
	uint32_t   ulPhyclk2GfxclkB;
};

struct pp_atomfwctrl_gpio_parameters {
	uint8_t   ucAcDcGpio;
	uint8_t   ucAcDcPolarity;
	uint8_t   ucVR0HotGpio;
	uint8_t   ucVR0HotPolarity;
	uint8_t   ucVR1HotGpio;
	uint8_t   ucVR1HotPolarity;
	uint8_t   ucFwCtfGpio;
	uint8_t   ucFwCtfPolarity;
};

struct pp_atomfwctrl_bios_boot_up_values {
	uint32_t   ulRevision;
	uint32_t   ulGfxClk;
	uint32_t   ulUClk;
	uint32_t   ulSocClk;
	uint32_t   ulDCEFClk;
	uint16_t   usVddc;
	uint16_t   usVddci;
	uint16_t   usMvddc;
	uint16_t   usVddGfx;
};

int pp_atomfwctrl_get_gpu_pll_dividers_vega10(struct pp_hwmgr *hwmgr,
		uint32_t clock_type, uint32_t clock_value,
		struct pp_atomfwctrl_clock_dividers_soc15 *dividers);
int pp_atomfwctrl_enter_self_refresh(struct pp_hwmgr *hwmgr);
bool pp_atomfwctrl_get_pp_assign_pin(struct pp_hwmgr *hwmgr, const uint32_t pin_id,
		struct pp_atomfwctrl_gpio_pin_assignment *gpio_pin_assignment);

int pp_atomfwctrl_get_voltage_table_v4(struct pp_hwmgr *hwmgr, uint8_t voltage_type,
		uint8_t voltage_mode, struct pp_atomfwctrl_voltage_table *voltage_table);
bool pp_atomfwctrl_is_voltage_controlled_by_gpio_v4(struct pp_hwmgr *hwmgr,
		uint8_t voltage_type, uint8_t voltage_mode);

int pp_atomfwctrl_get_avfs_information(struct pp_hwmgr *hwmgr,
		struct pp_atomfwctrl_avfs_parameters *param);
int pp_atomfwctrl_get_gpio_information(struct pp_hwmgr *hwmgr,
		struct pp_atomfwctrl_gpio_parameters *param);

int pp_atomfwctrl_get_vbios_bootup_values(struct pp_hwmgr *hwmgr,
			struct pp_atomfwctrl_bios_boot_up_values *boot_values);

#endif

