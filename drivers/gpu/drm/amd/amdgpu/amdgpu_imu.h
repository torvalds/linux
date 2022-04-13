/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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

#ifndef __AMDGPU_IMU_H__
#define __AMDGPU_IMU_H__

struct amdgpu_imu_funcs {
    int (*init_microcode)(struct amdgpu_device *adev);
    int (*load_microcode)(struct amdgpu_device *adev);
    void (*setup_imu)(struct amdgpu_device *adev);
    int (*start_imu)(struct amdgpu_device *adev);
    void (*program_rlc_ram)(struct amdgpu_device *adev);
};

struct imu_rlc_ram_golden {
    u32 hwip;
    u32 instance;
    u32 segment;
    u32 reg;
    u32 data;
    u32 addr_mask;
};

#define IMU_RLC_RAM_GOLDEN_VALUE(ip, inst, reg, data, addr_mask) \
    { ip##_HWIP, inst, reg##_BASE_IDX, reg, data, addr_mask }

struct amdgpu_imu {
    const struct amdgpu_imu_funcs *funcs;
};

#endif
