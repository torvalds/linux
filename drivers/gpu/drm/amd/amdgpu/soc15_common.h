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

#define WREG32_FIELD15(ip, idx, reg, field, val)	\
	WREG32(adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg,	\
	(RREG32(adev->reg_offset[ip##_HWIP][idx][mm##reg##_BASE_IDX] + mm##reg)	\
	& ~REG_FIELD_MASK(reg, field)) | (val) << REG_FIELD_SHIFT(reg, field))

#define RREG32_SOC15(ip, inst, reg) \
	RREG32(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg)

#define RREG32_SOC15_OFFSET(ip, inst, reg, offset) \
	RREG32((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset)

#define WREG32_SOC15(ip, inst, reg, value) \
	WREG32((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg), value)

#define WREG32_SOC15_NO_KIQ(ip, inst, reg, value) \
	WREG32_NO_KIQ((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg), value)

#define WREG32_SOC15_OFFSET(ip, inst, reg, offset, value) \
	WREG32((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) + offset, value)

#define SOC15_WAIT_ON_RREG(ip, inst, reg, expected_value, mask, ret) \
	do {							\
		uint32_t old_ = 0;	\
		uint32_t tmp_ = RREG32(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg); \
		uint32_t loop = adev->usec_timeout;		\
		while ((tmp_ & (mask)) != (expected_value)) {	\
			if (old_ != tmp_) {			\
				loop = adev->usec_timeout;	\
				old_ = tmp_;				\
			} else						\
				udelay(1);				\
			tmp_ = RREG32(adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg); \
			loop--;					\
			if (!loop) {				\
				DRM_WARN("Register(%d) [%s] failed to reach value 0x%08x != 0x%08x\n", \
					  inst, #reg, (unsigned)expected_value, (unsigned)(tmp_ & (mask))); \
				ret = -ETIMEDOUT;		\
				break;				\
			}					\
		}						\
	} while (0)

#define RREG32_SOC15_DPG_MODE(ip, inst, reg, mask, sram_sel) 	\
		({ WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_MASK, mask); \
			WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_CTL,	\
				UVD_DPG_LMA_CTL__MASK_EN_MASK |				\
				((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) \
				<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) | \
				(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT));	\
			RREG32_SOC15(ip, inst, mmUVD_DPG_LMA_DATA); })

#define WREG32_SOC15_DPG_MODE(ip, inst, reg, value, mask, sram_sel)	\
	do {							\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_DATA, value);	\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_MASK, mask);		\
		WREG32_SOC15(ip, inst, mmUVD_DPG_LMA_CTL,	\
			UVD_DPG_LMA_CTL__READ_WRITE_MASK |	\
			((adev->reg_offset[ip##_HWIP][inst][reg##_BASE_IDX] + reg) \
			<< UVD_DPG_LMA_CTL__READ_WRITE_ADDR__SHIFT) |	\
			(sram_sel << UVD_DPG_LMA_CTL__SRAM_SEL__SHIFT)); \
	} while (0)

#endif


