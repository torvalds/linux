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

#ifndef VEGA10_PROCESSPPTABLES_H
#define VEGA10_PROCESSPPTABLES_H

#include "hwmgr.h"

enum Vega10_I2CLineID {
	Vega10_I2CLineID_DDC1 = 0x90,
	Vega10_I2CLineID_DDC2 = 0x91,
	Vega10_I2CLineID_DDC3 = 0x92,
	Vega10_I2CLineID_DDC4 = 0x93,
	Vega10_I2CLineID_DDC5 = 0x94,
	Vega10_I2CLineID_DDC6 = 0x95,
	Vega10_I2CLineID_SCLSDA = 0x96,
	Vega10_I2CLineID_DDCVGA = 0x97
};

#define Vega10_I2C_DDC1DATA          0
#define Vega10_I2C_DDC1CLK           1
#define Vega10_I2C_DDC2DATA          2
#define Vega10_I2C_DDC2CLK           3
#define Vega10_I2C_DDC3DATA          4
#define Vega10_I2C_DDC3CLK           5
#define Vega10_I2C_SDA               40
#define Vega10_I2C_SCL               41
#define Vega10_I2C_DDC4DATA          65
#define Vega10_I2C_DDC4CLK           66
#define Vega10_I2C_DDC5DATA          0x48
#define Vega10_I2C_DDC5CLK           0x49
#define Vega10_I2C_DDC6DATA          0x4a
#define Vega10_I2C_DDC6CLK           0x4b
#define Vega10_I2C_DDCVGADATA        0x4c
#define Vega10_I2C_DDCVGACLK         0x4d

extern const struct pp_table_func vega10_pptable_funcs;
extern int vega10_get_number_of_powerplay_table_entries(struct pp_hwmgr *hwmgr);
extern int vega10_get_powerplay_table_entry(struct pp_hwmgr *hwmgr, uint32_t entry_index,
		struct pp_power_state *power_state, int (*call_back_func)(struct pp_hwmgr *, void *,
				struct pp_power_state *, void *, uint32_t));
extern int vega10_baco_set_cap(struct pp_hwmgr *hwmgr);
#endif
