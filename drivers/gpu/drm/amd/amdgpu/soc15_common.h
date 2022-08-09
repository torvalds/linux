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

#ifndef __SOC15_COMMON_H__
#define __SOC15_COMMON_H__

/* Register Access Macros */
#define SOC15_REG_OFFSET(ip, inst, reg)	(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg)

#define __WREG32_SOC15_RLC__(reg, value, flag, hwip) \
	((amdgpu_sriov_vf(adev) && adev->gfx.rlc.funcs && adev->gfx.rlc.funcs->sriov_wreg) ? \
	 adev->gfx.rlc.funcs->sriov_wreg(adev, reg, value, flag, hwip) : \
	 WREG32(reg, value))

#define __RREG32_SOC15_RLC__(reg, flag, hwip) \
	((amdgpu_sriov_vf(adev) && adev->gfx.rlc.funcs && adev->gfx.rlc.funcs->sriov_rreg) ? \
	 adev->gfx.rlc.funcs->sriov_rreg(adev, reg, flag, hwip) : \
	 RREG32(reg))

#define WREG32_FIELD15(ip, idx, reg, field, val)	\
	 __WREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg,	\
				(__RREG32_SOC15_RLC__( \
					adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg, \
					0, ip##_HWIP) & \
				~REG_FIELD_MASK(reg, field)) | (val) << REG_FIELD_SHIFT(reg, field), \
			      0, ip##_HWIP)

#define RREG32_SOC15(ip, inst, reg) \
	__RREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg, \
			 0, ip##_HWIP)

#define RREG32_SOC15_IP(ip, reg) __RREG32_SOC15_RLC__(reg, 0, ip##_HWIP)

#define RREG32_SOC15_IP_NO_KIQ(ip, reg) __RREG32_SOC15_RLC__(reg, AMDGPU_REGS_NO_KIQ, ip##_HWIP)

#define RREG32_SOC15_NO_KIQ(ip, inst, reg) \
	__RREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg, \
			 AMDGPU_REGS_NO_KIQ, ip##_HWIP)

#define RREG32_SOC15_OFFSET(ip, inst, reg, offset) \
	 __RREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset, 0, ip##_HWIP)

#define WREG32_SOC15(ip, inst, reg, value) \
	 __WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg), \
			  value, 0, ip##_HWIP)

#define WREG32_SOC15_IP(ip, reg, value) \
	 __WREG32_SOC15_RLC__(reg, value, 0, ip##_HWIP)

#define WREG32_SOC15_IP_NO_KIQ(ip, reg, value) \
	 __WREG32_SOC15_RLC__(reg, value, AMDGPU_REGS_NO_KIQ, ip##_HWIP)

#define WREG32_SOC15_NO_KIQ(ip, inst, reg, value) \
	__WREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg, \
			     value, AMDGPU_REGS_NO_KIQ, ip##_HWIP)

#define WREG32_SOC15_OFFSET(ip, inst, reg, offset, value) \
	 __WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset, \
			  value, 0, ip##_HWIP)

#define SOC15_WAIT_ON_RREG(ip, inst, reg, expected_value, mask) \
({	int ret = 0;						\
	do {							\
		uint32_t old_ = 0;				\
		uint32_t tmp_ = RREG32(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg); \
		uint32_t loop = adev->usec_timeout;		\
		ret = 0;					\
		while ((tmp_ & (mask)) != (expected_value)) {	\
			if (old_ != tmp_) {			\
				loop = adev->usec_timeout;	\
				old_ = tmp_;			\
			} else					\
				udelay(1);			\
			tmp_ = RREG32(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg); \
			loop--;					\
			if (!loop) {				\
				DRM_WARN("Register(%d) [%s] failed to reach value 0x%08x != 0x%08x\n", \
					  inst, #reg, (unsigned)expected_value, (unsigned)(tmp_ & (mask))); \
				ret = -ETIMEDOUT;		\
				break;				\
			}					\
		}						\
	} while (0);						\
	ret;							\
})

#define WREG32_RLC(reg, value) \
	__WREG32_SOC15_RLC__(reg, value, AMDGPU_REGS_RLC, GC_HWIP)

#define WREG32_RLC_EX(prefix, reg, value) \
	do {							\
		if (amdgpu_sriov_fullaccess(adev)) {    \
			uint32_t i = 0;	\
			uint32_t retries = 50000;	\
			uint32_t r0 = adev->reg_offset[GC_HWIP][0][prefix##SCRATCH_REG0_BASE_IDX] + prefix##SCRATCH_REG0;	\
			uint32_t r1 = adev->reg_offset[GC_HWIP][0][prefix##SCRATCH_REG1_BASE_IDX] + prefix##SCRATCH_REG1;	\
			uint32_t spare_int = adev->reg_offset[GC_HWIP][0][prefix##RLC_SPARE_INT_BASE_IDX] + prefix##RLC_SPARE_INT;	\
			WREG32(r0, value);	\
			WREG32(r1, (reg | 0x80000000));	\
			WREG32(spare_int, 0x1);	\
			for (i = 0; i < retries; i++) {	\
				u32 tmp = RREG32(r1);	\
				if (!(tmp & 0x80000000))	\
					break;	\
				udelay(10);	\
			}	\
			if (i >= retries)	\
				pr_err("timeout: rlcg program reg:0x%05x failed !\n", reg);	\
		} else {	\
			WREG32(reg, value); \
		}	\
	} while (0)

/* shadow the registers in the callback function */
#define WREG32_SOC15_RLC_SHADOW(ip, inst, reg, value) \
	__WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg), value, AMDGPU_REGS_RLC, GC_HWIP)

/* for GC only */
#define RREG32_RLC(reg) \
	__RREG32_SOC15_RLC__(reg, AMDGPU_REGS_RLC, GC_HWIP)

#define WREG32_RLC_NO_KIQ(reg, value, hwip) \
	__WREG32_SOC15_RLC__(reg, value, AMDGPU_REGS_NO_KIQ | AMDGPU_REGS_RLC, hwip)

#define RREG32_RLC_NO_KIQ(reg, hwip) \
	__RREG32_SOC15_RLC__(reg, AMDGPU_REGS_NO_KIQ | AMDGPU_REGS_RLC, hwip)

#define WREG32_SOC15_RLC_SHADOW_EX(prefix, ip, inst, reg, value) \
	do {							\
		uint32_t target_reg = adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg;\
		if (amdgpu_sriov_fullaccess(adev)) {    \
			uint32_t r2 = adev->reg_offset[GC_HWIP][0][prefix##SCRATCH_REG1_BASE_IDX] + prefix##SCRATCH_REG2;	\
			uint32_t r3 = adev->reg_offset[GC_HWIP][0][prefix##SCRATCH_REG1_BASE_IDX] + prefix##SCRATCH_REG3;	\
			uint32_t grbm_cntl = adev->reg_offset[GC_HWIP][0][prefix##GRBM_GFX_CNTL_BASE_IDX] + prefix##GRBM_GFX_CNTL;   \
			uint32_t grbm_idx = adev->reg_offset[GC_HWIP][0][prefix##GRBM_GFX_INDEX_BASE_IDX] + prefix##GRBM_GFX_INDEX;   \
			if (target_reg == grbm_cntl) \
				WREG32(r2, value);	\
			else if (target_reg == grbm_idx) \
				WREG32(r3, value);	\
			WREG32(target_reg, value);	\
		} else {	\
			WREG32(target_reg, value); \
		}	\
	} while (0)

#define RREG32_SOC15_RLC(ip, inst, reg) \
	__RREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg, AMDGPU_REGS_RLC, ip##_HWIP)

#define WREG32_SOC15_RLC(ip, inst, reg, value) \
	do {							\
		uint32_t target_reg = adev->reg_offset[ip##_HWIP][0][reg##_BASE_IDX] + reg;\
		__WREG32_SOC15_RLC__(target_reg, value, AMDGPU_REGS_RLC, ip##_HWIP); \
	} while (0)

#define WREG32_SOC15_RLC_EX(prefix, ip, inst, reg, value) \
	do {							\
			uint32_t target_reg = adev->reg_offset[GC_HWIP][0][reg##_BASE_IDX] + reg;\
			WREG32_RLC_EX(prefix, target_reg, value); \
	} while (0)

#define WREG32_FIELD15_RLC(ip, idx, reg, field, val)   \
	__WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg), \
			     (__RREG32_SOC15_RLC__(adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg, \
						   AMDGPU_REGS_RLC, ip##_HWIP) & \
			      ~REG_FIELD_MASK(reg, field)) | (val) << REG_FIELD_SHIFT(reg, field), \
			     AMDGPU_REGS_RLC, ip##_HWIP)

#define WREG32_SOC15_OFFSET_RLC(ip, inst, reg, offset, value) \
	__WREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset, value, AMDGPU_REGS_RLC, ip##_HWIP)

#define RREG32_SOC15_OFFSET_RLC(ip, inst, reg, offset) \
	__RREG32_SOC15_RLC__((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset, AMDGPU_REGS_RLC, ip##_HWIP)

#endif
