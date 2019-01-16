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
#ifndef __VEGA20_PPT_H__
#define __VEGA20_PPT_H__

#define VEGA20_UMD_PSTATE_GFXCLK_LEVEL         0x3
#define VEGA20_UMD_PSTATE_MCLK_LEVEL           0x2

#define MAX_REGULAR_DPM_NUMBER 16
#define MAX_PCIE_CONF 2

struct vega20_dpm_level {
        bool            enabled;
        uint32_t        value;
        uint32_t        param1;
};

struct vega20_dpm_state {
        uint32_t  soft_min_level;
        uint32_t  soft_max_level;
        uint32_t  hard_min_level;
        uint32_t  hard_max_level;
};

struct vega20_single_dpm_table {
        uint32_t                count;
        struct vega20_dpm_state dpm_state;
        struct vega20_dpm_level dpm_levels[MAX_REGULAR_DPM_NUMBER];
};

struct vega20_pcie_table {
        uint16_t count;
        uint8_t  pcie_gen[MAX_PCIE_CONF];
        uint8_t  pcie_lane[MAX_PCIE_CONF];
        uint32_t lclk[MAX_PCIE_CONF];
};

struct vega20_dpm_table {
	struct vega20_single_dpm_table  soc_table;
        struct vega20_single_dpm_table  gfx_table;
        struct vega20_single_dpm_table  mem_table;
        struct vega20_single_dpm_table  eclk_table;
        struct vega20_single_dpm_table  vclk_table;
        struct vega20_single_dpm_table  dclk_table;
        struct vega20_single_dpm_table  dcef_table;
        struct vega20_single_dpm_table  pixel_table;
        struct vega20_single_dpm_table  display_table;
        struct vega20_single_dpm_table  phy_table;
        struct vega20_single_dpm_table  fclk_table;
        struct vega20_pcie_table        pcie_table;
};

extern void vega20_set_ppt_funcs(struct smu_context *smu);

#endif
