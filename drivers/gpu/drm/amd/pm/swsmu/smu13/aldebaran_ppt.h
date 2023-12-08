/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#ifndef __ALDEBARAN_PPT_H__
#define __ALDEBARAN_PPT_H__

#define ALDEBARAN_UMD_PSTATE_GFXCLK_LEVEL         0x3
#define ALDEBARAN_UMD_PSTATE_SOCCLK_LEVEL         0x3
#define ALDEBARAN_UMD_PSTATE_MCLK_LEVEL           0x2

#define MAX_DPM_NUMBER 16
#define ALDEBARAN_MAX_PCIE_CONF 2

struct aldebaran_dpm_level {
	bool            enabled;
	uint32_t        value;
	uint32_t        param1;
};

struct aldebaran_dpm_state {
	uint32_t  soft_min_level;
	uint32_t  soft_max_level;
	uint32_t  hard_min_level;
	uint32_t  hard_max_level;
};

struct aldebaran_single_dpm_table {
	uint32_t                count;
	struct aldebaran_dpm_state dpm_state;
	struct aldebaran_dpm_level dpm_levels[MAX_DPM_NUMBER];
};

struct aldebaran_pcie_table {
	uint16_t count;
	uint8_t  pcie_gen[ALDEBARAN_MAX_PCIE_CONF];
	uint8_t  pcie_lane[ALDEBARAN_MAX_PCIE_CONF];
	uint32_t lclk[ALDEBARAN_MAX_PCIE_CONF];
};

struct aldebaran_dpm_table {
	struct aldebaran_single_dpm_table  soc_table;
	struct aldebaran_single_dpm_table  gfx_table;
	struct aldebaran_single_dpm_table  mem_table;
	struct aldebaran_single_dpm_table  eclk_table;
	struct aldebaran_single_dpm_table  vclk_table;
	struct aldebaran_single_dpm_table  dclk_table;
	struct aldebaran_single_dpm_table  fclk_table;
	struct aldebaran_pcie_table        pcie_table;
};

extern void aldebaran_set_ppt_funcs(struct smu_context *smu);

#endif
