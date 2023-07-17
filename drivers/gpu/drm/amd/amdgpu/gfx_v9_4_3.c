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
#include <linux/firmware.h>

#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "soc15.h"
#include "soc15_common.h"
#include "vega10_enum.h"

#include "gc/gc_9_4_3_offset.h"
#include "gc/gc_9_4_3_sh_mask.h"

#include "gfx_v9_4_3.h"

#define RLCG_UCODE_LOADING_START_ADDRESS 0x00002000L

static uint64_t gfx_v9_4_3_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	uint64_t clock;

	amdgpu_gfx_off_ctrl(adev, false);
	mutex_lock(&adev->gfx.gpu_clock_mutex);
	WREG32_SOC15(GC, 0, regRLC_CAPTURE_GPU_CLOCK_COUNT, 1);
	clock = (uint64_t)RREG32_SOC15(GC, 0, regRLC_GPU_CLOCK_COUNT_LSB) |
		((uint64_t)RREG32_SOC15(GC, 0, regRLC_GPU_CLOCK_COUNT_MSB) << 32ULL);
	mutex_unlock(&adev->gfx.gpu_clock_mutex);
	amdgpu_gfx_off_ctrl(adev, true);

	return clock;
}

static void gfx_v9_4_3_select_se_sh(struct amdgpu_device *adev,
				    u32 se_num,
				    u32 sh_num,
				    u32 instance)
{
	u32 data;

	if (instance == 0xffffffff)
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(0, GRBM_GFX_INDEX,
				     INSTANCE_INDEX, instance);

	if (se_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
				     SE_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SE_INDEX, se_num);

	if (sh_num == 0xffffffff)
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX,
				     SH_BROADCAST_WRITES, 1);
	else
		data = REG_SET_FIELD(data, GRBM_GFX_INDEX, SH_INDEX, sh_num);

	WREG32_SOC15_RLC_SHADOW_EX(reg, GC, 0, regGRBM_GFX_INDEX, data);
}

static uint32_t wave_read_ind(struct amdgpu_device *adev, uint32_t simd, uint32_t wave, uint32_t address)
{
	WREG32_SOC15_RLC(GC, 0, regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(address << SQ_IND_INDEX__INDEX__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK));
	return RREG32_SOC15(GC, 0, regSQ_IND_DATA);
}

static void wave_read_regs(struct amdgpu_device *adev, uint32_t simd,
			   uint32_t wave, uint32_t thread,
			   uint32_t regno, uint32_t num, uint32_t *out)
{
	WREG32_SOC15_RLC(GC, 0, regSQ_IND_INDEX,
		(wave << SQ_IND_INDEX__WAVE_ID__SHIFT) |
		(simd << SQ_IND_INDEX__SIMD_ID__SHIFT) |
		(regno << SQ_IND_INDEX__INDEX__SHIFT) |
		(thread << SQ_IND_INDEX__THREAD_ID__SHIFT) |
		(SQ_IND_INDEX__FORCE_READ_MASK) |
		(SQ_IND_INDEX__AUTO_INCR_MASK));
	while (num--)
		*(out++) = RREG32_SOC15(GC, 0, regSQ_IND_DATA);
}

static void gfx_v9_4_3_read_wave_data(struct amdgpu_device *adev,
				      uint32_t simd, uint32_t wave,
				      uint32_t *dst, int *no_fields)
{
	/* type 1 wave data */
	dst[(*no_fields)++] = 1;
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_STATUS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_PC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_LO);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_EXEC_HI);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_HW_ID);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_INST_DW1);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_GPR_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_LDS_ALLOC);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_TRAPSTS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_STS);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_IB_DBG0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_M0);
	dst[(*no_fields)++] = wave_read_ind(adev, simd, wave, ixSQ_WAVE_MODE);
}

static void gfx_v9_4_3_read_wave_sgprs(struct amdgpu_device *adev, uint32_t simd,
				       uint32_t wave, uint32_t start,
				       uint32_t size, uint32_t *dst)
{
	wave_read_regs(adev, simd, wave, 0,
		       start + SQIND_WAVE_SGPRS_OFFSET, size, dst);
}

static void gfx_v9_4_3_read_wave_vgprs(struct amdgpu_device *adev, uint32_t simd,
				       uint32_t wave, uint32_t thread,
				       uint32_t start, uint32_t size,
				       uint32_t *dst)
{
	wave_read_regs(adev, simd, wave, thread,
		       start + SQIND_WAVE_VGPRS_OFFSET, size, dst);
}

static void gfx_v9_4_3_select_me_pipe_q(struct amdgpu_device *adev,
					u32 me, u32 pipe, u32 q, u32 vm)
{
	soc15_grbm_select(adev, me, pipe, q, vm);
}

static bool gfx_v9_4_3_is_rlc_enabled(struct amdgpu_device *adev)
{
	uint32_t rlc_setting;

	/* if RLC is not enabled, do nothing */
	rlc_setting = RREG32_SOC15(GC, 0, regRLC_CNTL);
	if (!(rlc_setting & RLC_CNTL__RLC_ENABLE_F32_MASK))
		return false;

	return true;
}

static void gfx_v9_4_3_set_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;
	unsigned i;

	data = RLC_SAFE_MODE__CMD_MASK;
	data |= (1 << RLC_SAFE_MODE__MESSAGE__SHIFT);
	WREG32_SOC15(GC, 0, regRLC_SAFE_MODE, data);

	/* wait for RLC_SAFE_MODE */
	for (i = 0; i < adev->usec_timeout; i++) {
		if (!REG_GET_FIELD(RREG32_SOC15(GC, 0, regRLC_SAFE_MODE), RLC_SAFE_MODE, CMD))
			break;
		udelay(1);
	}
}

static void gfx_v9_4_3_unset_safe_mode(struct amdgpu_device *adev)
{
	uint32_t data;

	data = RLC_SAFE_MODE__CMD_MASK;
	WREG32_SOC15(GC, 0, regRLC_SAFE_MODE, data);
}

static int gfx_v9_4_3_rlc_init(struct amdgpu_device *adev)
{
	/* init spm vmid with 0xf */
	if (adev->gfx.rlc.funcs->update_spm_vmid)
		adev->gfx.rlc.funcs->update_spm_vmid(adev, 0xf);

	return 0;
}

static void gfx_v9_4_3_wait_for_rlc_serdes(struct amdgpu_device *adev)
{
	u32 i, j, k;
	u32 mask;

	mutex_lock(&adev->grbm_idx_mutex);
	for (i = 0; i < adev->gfx.config.max_shader_engines; i++) {
		for (j = 0; j < adev->gfx.config.max_sh_per_se; j++) {
			gfx_v9_4_3_select_se_sh(adev, i, j, 0xffffffff);
			for (k = 0; k < adev->usec_timeout; k++) {
				if (RREG32_SOC15(GC, 0, regRLC_SERDES_CU_MASTER_BUSY) == 0)
					break;
				udelay(1);
			}
			if (k == adev->usec_timeout) {
				gfx_v9_4_3_select_se_sh(adev, 0xffffffff,
						      0xffffffff, 0xffffffff);
				mutex_unlock(&adev->grbm_idx_mutex);
				DRM_INFO("Timeout wait for RLC serdes %u,%u\n",
					 i, j);
				return;
			}
		}
	}
	gfx_v9_4_3_select_se_sh(adev, 0xffffffff, 0xffffffff, 0xffffffff);
	mutex_unlock(&adev->grbm_idx_mutex);

	mask = RLC_SERDES_NONCU_MASTER_BUSY__SE_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__GC_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC0_MASTER_BUSY_MASK |
		RLC_SERDES_NONCU_MASTER_BUSY__TC1_MASTER_BUSY_MASK;
	for (k = 0; k < adev->usec_timeout; k++) {
		if ((RREG32_SOC15(GC, 0, regRLC_SERDES_NONCU_MASTER_BUSY) & mask) == 0)
			break;
		udelay(1);
	}
}

static void gfx_v9_4_3_enable_gui_idle_interrupt(struct amdgpu_device *adev,
					       bool enable)
{
	u32 tmp;

	/* These interrupts should be enabled to drive DS clock */

	tmp = RREG32_SOC15(GC, 0, regCP_INT_CNTL_RING0);

	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_BUSY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CNTX_EMPTY_INT_ENABLE, enable ? 1 : 0);
	tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, CMP_BUSY_INT_ENABLE, enable ? 1 : 0);
	if (adev->gfx.num_gfx_rings)
		tmp = REG_SET_FIELD(tmp, CP_INT_CNTL_RING0, GFX_IDLE_INT_ENABLE, enable ? 1 : 0);

	WREG32_SOC15(GC, 0, regCP_INT_CNTL_RING0, tmp);
}

static void gfx_v9_4_3_rlc_stop(struct amdgpu_device *adev)
{
	WREG32_FIELD15_PREREG(GC, 0, RLC_CNTL, RLC_ENABLE_F32, 0);
	gfx_v9_4_3_enable_gui_idle_interrupt(adev, false);
	gfx_v9_4_3_wait_for_rlc_serdes(adev);
}

static void gfx_v9_4_3_rlc_reset(struct amdgpu_device *adev)
{
	WREG32_FIELD15_PREREG(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 1);
	udelay(50);
	WREG32_FIELD15_PREREG(GC, 0, GRBM_SOFT_RESET, SOFT_RESET_RLC, 0);
	udelay(50);
}

static void gfx_v9_4_3_rlc_start(struct amdgpu_device *adev)
{
#ifdef AMDGPU_RLC_DEBUG_RETRY
	u32 rlc_ucode_ver;
#endif

	WREG32_FIELD15_PREREG(GC, 0, RLC_CNTL, RLC_ENABLE_F32, 1);
	udelay(50);

	/* carrizo do enable cp interrupt after cp inited */
	if (!(adev->flags & AMD_IS_APU)) {
		gfx_v9_4_3_enable_gui_idle_interrupt(adev, true);
		udelay(50);
	}

#ifdef AMDGPU_RLC_DEBUG_RETRY
	/* RLC_GPM_GENERAL_6 : RLC Ucode version */
	rlc_ucode_ver = RREG32_SOC15(GC, 0, regRLC_GPM_GENERAL_6);
	if (rlc_ucode_ver == 0x108) {
		dev_info(adev->dev,
			 "Using rlc debug ucode. regRLC_GPM_GENERAL_6 ==0x08%x / fw_ver == %i \n",
			 rlc_ucode_ver, adev->gfx.rlc_fw_version);
		/* RLC_GPM_TIMER_INT_3 : Timer interval in RefCLK cycles,
		 * default is 0x9C4 to create a 100us interval */
		WREG32_SOC15(GC, 0, regRLC_GPM_TIMER_INT_3, 0x9C4);
		/* RLC_GPM_GENERAL_12 : Minimum gap between wptr and rptr
		 * to disable the page fault retry interrupts, default is
		 * 0x100 (256) */
		WREG32_SOC15(GC, 0, regRLC_GPM_GENERAL_12, 0x100);
	}
#endif
}

static int gfx_v9_4_3_rlc_load_microcode(struct amdgpu_device *adev)
{
	const struct rlc_firmware_header_v2_0 *hdr;
	const __le32 *fw_data;
	unsigned i, fw_size;

	if (!adev->gfx.rlc_fw)
		return -EINVAL;

	hdr = (const struct rlc_firmware_header_v2_0 *)adev->gfx.rlc_fw->data;
	amdgpu_ucode_print_rlc_hdr(&hdr->header);

	fw_data = (const __le32 *)(adev->gfx.rlc_fw->data +
			   le32_to_cpu(hdr->header.ucode_array_offset_bytes));
	fw_size = le32_to_cpu(hdr->header.ucode_size_bytes) / 4;

	WREG32_SOC15(GC, 0, regRLC_GPM_UCODE_ADDR,
			RLCG_UCODE_LOADING_START_ADDRESS);
	for (i = 0; i < fw_size; i++) {
		if (amdgpu_emu_mode == 1 && i % 100 == 0) {
			dev_info(adev->dev, "Write RLC ucode data %u DWs\n", i);
			msleep(1);
		}
		WREG32_SOC15(GC, 0, regRLC_GPM_UCODE_DATA, le32_to_cpup(fw_data++));
	}
	WREG32_SOC15(GC, 0, regRLC_GPM_UCODE_ADDR, adev->gfx.rlc_fw_version);

	return 0;
}

static int gfx_v9_4_3_rlc_resume(struct amdgpu_device *adev)
{
	int r;

	adev->gfx.rlc.funcs->stop(adev);

	/* disable CG */
	WREG32_SOC15(GC, 0, regRLC_CGCG_CGLS_CTRL, 0);

	/* TODO: revisit pg function */
	/* gfx_v9_4_3_init_pg(adev);*/

	if (adev->firmware.load_type != AMDGPU_FW_LOAD_PSP) {
		/* legacy rlc firmware loading */
		r = gfx_v9_4_3_rlc_load_microcode(adev);
		if (r)
			return r;
	}

	adev->gfx.rlc.funcs->start(adev);

	return 0;
}

static void gfx_v9_4_3_update_spm_vmid(struct amdgpu_device *adev, unsigned vmid)
{
	u32 reg, data;

	reg = SOC15_REG_OFFSET(GC, 0, regRLC_SPM_MC_CNTL);
	if (amdgpu_sriov_is_pp_one_vf(adev))
		data = RREG32_NO_KIQ(reg);
	else
		data = RREG32(reg);

	data &= ~RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK;
	data |= (vmid & RLC_SPM_MC_CNTL__RLC_SPM_VMID_MASK) << RLC_SPM_MC_CNTL__RLC_SPM_VMID__SHIFT;

	if (amdgpu_sriov_is_pp_one_vf(adev))
		WREG32_SOC15_NO_KIQ(GC, 0, regRLC_SPM_MC_CNTL, data);
	else
		WREG32_SOC15(GC, 0, regRLC_SPM_MC_CNTL, data);
}

static const struct soc15_reg_rlcg rlcg_access_gc_9_4_3[] = {
	{SOC15_REG_ENTRY(GC, 0, regGRBM_GFX_INDEX)},
	{SOC15_REG_ENTRY(GC, 0, regSQ_IND_INDEX)},
};

static bool gfx_v9_4_3_check_rlcg_range(struct amdgpu_device *adev,
					uint32_t offset,
					struct soc15_reg_rlcg *entries, int arr_size)
{
	int i;
	uint32_t reg;

	if (!entries)
		return false;

	for (i = 0; i < arr_size; i++) {
		const struct soc15_reg_rlcg *entry;

		entry = &entries[i];
		reg = adev->reg_offset[entry->hwip][entry->instance][entry->segment] + entry->reg;
		if (offset == reg)
			return true;
	}

	return false;
}

static bool gfx_v9_4_3_is_rlcg_access_range(struct amdgpu_device *adev, u32 offset)
{
	return gfx_v9_4_3_check_rlcg_range(adev, offset,
					(void *)rlcg_access_gc_9_4_3,
					ARRAY_SIZE(rlcg_access_gc_9_4_3));
}

const struct amdgpu_gfx_funcs gfx_v9_4_3_gfx_funcs = {
	.get_gpu_clock_counter = &gfx_v9_4_3_get_gpu_clock_counter,
	.select_se_sh = &gfx_v9_4_3_select_se_sh,
	.read_wave_data = &gfx_v9_4_3_read_wave_data,
	.read_wave_sgprs = &gfx_v9_4_3_read_wave_sgprs,
	.read_wave_vgprs = &gfx_v9_4_3_read_wave_vgprs,
	.select_me_pipe_q = &gfx_v9_4_3_select_me_pipe_q,
};

const struct amdgpu_rlc_funcs gfx_v9_4_3_rlc_funcs = {
	.is_rlc_enabled = gfx_v9_4_3_is_rlc_enabled,
	.set_safe_mode = gfx_v9_4_3_set_safe_mode,
	.unset_safe_mode = gfx_v9_4_3_unset_safe_mode,
	.init = gfx_v9_4_3_rlc_init,
	.resume = gfx_v9_4_3_rlc_resume,
	.stop = gfx_v9_4_3_rlc_stop,
	.reset = gfx_v9_4_3_rlc_reset,
	.start = gfx_v9_4_3_rlc_start,
	.update_spm_vmid = gfx_v9_4_3_update_spm_vmid,
	.is_rlcg_access_range = gfx_v9_4_3_is_rlcg_access_range,
};
