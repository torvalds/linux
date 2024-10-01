/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "smuio_v13_0_6.h"
#include "smuio/smuio_13_0_6_offset.h"
#include "smuio/smuio_13_0_6_sh_mask.h"

static u32 smuio_v13_0_6_get_rom_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, regROM_INDEX);
}

static u32 smuio_v13_0_6_get_rom_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, regROM_DATA);
}

const struct amdgpu_smuio_funcs smuio_v13_0_6_funcs = {
	.get_rom_index_offset = smuio_v13_0_6_get_rom_index_offset,
	.get_rom_data_offset = smuio_v13_0_6_get_rom_data_offset,
};
