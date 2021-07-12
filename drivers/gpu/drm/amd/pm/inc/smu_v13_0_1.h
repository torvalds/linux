/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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
#ifndef __SMU_V13_0_1_H__
#define __SMU_V13_0_1_H__

#include "amdgpu_smu.h"

#define SMU13_0_1_DRIVER_IF_VERSION_INV 0xFFFFFFFF
#define SMU13_0_1_DRIVER_IF_VERSION_YELLOW_CARP 0x3

/* MP Apertures */
#define MP0_Public			0x03800000
#define MP0_SRAM			0x03900000
#define MP1_Public			0x03b00000
#define MP1_SRAM			0x03c00004

/* address block */
#define smnMP1_FIRMWARE_FLAGS		0x3010024


#if defined(SWSMU_CODE_LAYER_L2) || defined(SWSMU_CODE_LAYER_L3)

int smu_v13_0_1_check_fw_status(struct smu_context *smu);

int smu_v13_0_1_check_fw_version(struct smu_context *smu);

int smu_v13_0_1_fini_smc_tables(struct smu_context *smu);

int smu_v13_0_1_get_vbios_bootup_values(struct smu_context *smu);

int smu_v13_0_1_set_default_dpm_tables(struct smu_context *smu);

int smu_v13_0_1_set_driver_table_location(struct smu_context *smu);

int smu_v13_0_1_gfx_off_control(struct smu_context *smu, bool enable);
#endif
#endif
