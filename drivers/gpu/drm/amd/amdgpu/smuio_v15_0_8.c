/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "smuio_v15_0_8.h"
#include "smuio/smuio_15_0_8_offset.h"
#include "smuio/smuio_15_0_8_sh_mask.h"

#define SMUIO_MCM_CONFIG__HOST_GPU_XGMI_MASK	0x00000001L
#define SMUIO_MCM_CONFIG__ETHERNET_SWITCH_MASK	0x00000008L
#define SMUIO_MCM_CONFIG__CUSTOM_HBM_MASK	0x00000001L

static u32 smuio_v15_0_8_get_rom_index_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, regROM_INDEX);
}

static u32 smuio_v15_0_8_get_rom_data_offset(struct amdgpu_device *adev)
{
	return SOC15_REG_OFFSET(SMUIO, 0, regROM_DATA);
}

static void smuio_v15_0_8_update_rom_clock_gating(struct amdgpu_device *adev, bool enable)
{
	return;
}

static u64 smuio_v15_0_8_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	u64 clock;
	u64 clock_counter_lo, clock_counter_hi_pre, clock_counter_hi_after;

	preempt_disable();
	clock_counter_hi_pre = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_UPPER);
	clock_counter_lo = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_LOWER);
	/* the clock counter may be udpated during polling the counters */
	clock_counter_hi_after = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_UPPER);
	if (clock_counter_hi_pre != clock_counter_hi_after)
		clock_counter_lo = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_LOWER);
	preempt_enable();

	clock = clock_counter_lo | (clock_counter_hi_after << 32ULL);

	return clock;
}

static void smuio_v15_0_8_get_clock_gating_state(struct amdgpu_device *adev, u64 *flags)
{
	u32 data;

	/* CGTT_ROM_CLK_CTRL0 is not available for APU */
	if (adev->flags & AMD_IS_APU)
		return;

	data = RREG32_SOC15(SMUIO, 0, regCGTT_ROM_CLK_CTRL0);
	if (!(data & CGTT_ROM_CLK_CTRL0__SOFT_OVERRIDE0_MASK))
		*flags |= AMD_CG_SUPPORT_ROM_MGCG;
}

/**
 * smuio_v15_0_8_get_die_id - query die id from FCH.
 *
 * @adev: amdgpu device pointer
 *
 * Returns die id
 */
static u32 smuio_v15_0_8_get_die_id(struct amdgpu_device *adev)
{
	u32 data, die_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	die_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, DIE_ID);

	return die_id;
}

/**
 * smuio_v15_0_8_get_socket_id - query socket id from FCH
 *
 * @adev: amdgpu device pointer
 *
 * Returns socket id
 */
static u32 smuio_v15_0_8_get_socket_id(struct amdgpu_device *adev)
{
	u32 data, socket_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	socket_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, SOCKET_ID);

	return socket_id;
}

/**
 * smuio_v15_0_8_is_host_gpu_xgmi_supported - detect xgmi interface between cpu and gpu/s.
 *
 * @adev: amdgpu device pointer
 *
 * Returns true on success or false otherwise.
 */
static bool smuio_v15_0_8_is_host_gpu_xgmi_supported(struct amdgpu_device *adev)
{
	u32 data;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	data = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, TOPOLOGY_ID);
	/* data[4:0]
	 * bit 0 == 0 host-gpu interface is PCIE
	 * bit 0 == 1 host-gpu interface is Alternate Protocal
	 * for AMD, this is XGMI
	 */
	data &= SMUIO_MCM_CONFIG__HOST_GPU_XGMI_MASK;

	return data ? true : false;
}

#if 0
/*
 * smuio_v15_0_8_is_connected_with_ethernet_switch - detect systems connected with ethernet switch
 *
 * @adev: amdgpu device pointer
 *
 * Returns true on success or false otherwise.
 */
static bool smuio_v15_0_8_is_connected_with_ethernet_switch(struct amdgpu_device *adev)
{
	u32 data;

	if (!(adev->flags & AMD_IS_APU))
		return false;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	data = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, TOPOLOGY_ID);
	/* data[4:0]
	 * bit 3 == 0 systems connected with ethernet switch
	 */
	data &= SMUIO_MCM_CONFIG__ETHERNET_SWITCH_MASK;

	return data ? false : true;
}
#endif

static enum amdgpu_pkg_type smuio_v15_0_8_get_pkg_type(struct amdgpu_device *adev)
{
	enum amdgpu_pkg_type pkg_type;
	u32 data;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	data = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, PKG_TYPE);

	/* data [3:0]
	 bit 2 and bit 3 identifies the pkg type */
	switch (data & 0xC) {
	case 0x0:
		pkg_type = AMDGPU_PKG_TYPE_BB;
		break;
	case 0x8:
		pkg_type = AMDGPU_PKG_TYPE_CEM;
		break;
	default:
		pkg_type = AMDGPU_PKG_TYPE_UNKNOWN;
		break;
	}

	return pkg_type;
}

#if 0
static bool smuio_v15_0_8_is_custom_hbm_supported(struct amdgpu_device *adev)
{
	u32 data;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	data = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, PKG_TYPE);

	/* data [3:0]
	 * bit 0 identifies custom HBM module */
	data &= SMUIO_MCM_CONFIG__CUSTOM_HBM_MASK;

	return data ? true : false;
}
#endif

const struct amdgpu_smuio_funcs smuio_v15_0_8_funcs = {
	.get_rom_index_offset = smuio_v15_0_8_get_rom_index_offset,
	.get_rom_data_offset = smuio_v15_0_8_get_rom_data_offset,
	.get_gpu_clock_counter = smuio_v15_0_8_get_gpu_clock_counter,
	.get_die_id = smuio_v15_0_8_get_die_id,
	.get_socket_id = smuio_v15_0_8_get_socket_id,
	.is_host_gpu_xgmi_supported = smuio_v15_0_8_is_host_gpu_xgmi_supported,
	.update_rom_clock_gating = smuio_v15_0_8_update_rom_clock_gating,
	.get_clock_gating_state = smuio_v15_0_8_get_clock_gating_state,
	.get_pkg_type = smuio_v15_0_8_get_pkg_type,
};
