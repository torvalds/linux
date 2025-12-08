// SPDX-License-Identifier: MIT
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
 */

#include <linux/delay.h>

#include "amdgpu.h"
#include "amdgpu_reset.h"
#include "amdgpu_trace.h"
#include "amdgpu_virt.h"
#include "amdgpu_reg_access.h"

#define AMDGPU_PCIE_INDEX_FALLBACK (0x38 >> 2)
#define AMDGPU_PCIE_INDEX_HI_FALLBACK (0x44 >> 2)
#define AMDGPU_PCIE_DATA_FALLBACK (0x3C >> 2)

/*
 * register access helper functions.
 */

/**
 * amdgpu_device_rreg - read a memory mapped IO or indirect register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @acc_flags: access flags which require special behavior
 *
 * Returns the 32 bit value from the offset specified.
 */
uint32_t amdgpu_device_rreg(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t acc_flags)
{
	uint32_t ret;

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if ((reg * 4) < adev->rmmio_size) {
		if (!(acc_flags & AMDGPU_REGS_NO_KIQ) &&
		    amdgpu_sriov_runtime(adev) &&
		    down_read_trylock(&adev->reset_domain->sem)) {
			ret = amdgpu_kiq_rreg(adev, reg, 0);
			up_read(&adev->reset_domain->sem);
		} else {
			ret = readl(((void __iomem *)adev->rmmio) + (reg * 4));
		}
	} else {
		ret = adev->pcie_rreg(adev, reg * 4);
	}

	trace_amdgpu_device_rreg(adev->pdev->device, reg, ret);

	return ret;
}

/*
 * MMIO register read with bytes helper functions
 * @offset:bytes offset from MMIO start
 */

/**
 * amdgpu_mm_rreg8 - read a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @offset: byte aligned register offset
 *
 * Returns the 8 bit value from the offset specified.
 */
uint8_t amdgpu_mm_rreg8(struct amdgpu_device *adev, uint32_t offset)
{
	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if (offset < adev->rmmio_size)
		return (readb(adev->rmmio + offset));
	BUG();
}

/**
 * amdgpu_device_xcc_rreg - read a memory mapped IO or indirect register with specific XCC
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @acc_flags: access flags which require special behavior
 * @xcc_id: xcc accelerated compute core id
 *
 * Returns the 32 bit value from the offset specified.
 */
uint32_t amdgpu_device_xcc_rreg(struct amdgpu_device *adev, uint32_t reg,
				uint32_t acc_flags, uint32_t xcc_id)
{
	uint32_t ret, rlcg_flag;

	if (amdgpu_device_skip_hw_access(adev))
		return 0;

	if ((reg * 4) < adev->rmmio_size) {
		if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_runtime(adev) &&
		    adev->gfx.rlc.rlcg_reg_access_supported &&
		    amdgpu_virt_get_rlcg_reg_access_flag(
			    adev, acc_flags, GC_HWIP, false, &rlcg_flag)) {
			ret = amdgpu_virt_rlcg_reg_rw(adev, reg, 0, rlcg_flag,
						      GET_INST(GC, xcc_id));
		} else if (!(acc_flags & AMDGPU_REGS_NO_KIQ) &&
			   amdgpu_sriov_runtime(adev) &&
			   down_read_trylock(&adev->reset_domain->sem)) {
			ret = amdgpu_kiq_rreg(adev, reg, xcc_id);
			up_read(&adev->reset_domain->sem);
		} else {
			ret = readl(((void __iomem *)adev->rmmio) + (reg * 4));
		}
	} else {
		ret = adev->pcie_rreg(adev, reg * 4);
	}

	return ret;
}

/*
 * MMIO register write with bytes helper functions
 * @offset:bytes offset from MMIO start
 * @value: the value want to be written to the register
 */

/**
 * amdgpu_mm_wreg8 - read a memory mapped IO register
 *
 * @adev: amdgpu_device pointer
 * @offset: byte aligned register offset
 * @value: 8 bit value to write
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_mm_wreg8(struct amdgpu_device *adev, uint32_t offset, uint8_t value)
{
	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (offset < adev->rmmio_size)
		writeb(value, adev->rmmio + offset);
	else
		BUG();
}

/**
 * amdgpu_device_wreg - write to a memory mapped IO or indirect register
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @v: 32 bit value to write to the register
 * @acc_flags: access flags which require special behavior
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_device_wreg(struct amdgpu_device *adev, uint32_t reg, uint32_t v,
			uint32_t acc_flags)
{
	if (amdgpu_device_skip_hw_access(adev))
		return;

	if ((reg * 4) < adev->rmmio_size) {
		if (!(acc_flags & AMDGPU_REGS_NO_KIQ) &&
		    amdgpu_sriov_runtime(adev) &&
		    down_read_trylock(&adev->reset_domain->sem)) {
			amdgpu_kiq_wreg(adev, reg, v, 0);
			up_read(&adev->reset_domain->sem);
		} else {
			writel(v, ((void __iomem *)adev->rmmio) + (reg * 4));
		}
	} else {
		adev->pcie_wreg(adev, reg * 4, v);
	}

	trace_amdgpu_device_wreg(adev->pdev->device, reg, v);
}

/**
 * amdgpu_mm_wreg_mmio_rlc -  write register either with direct/indirect mmio or with RLC path if in range
 *
 * @adev: amdgpu_device pointer
 * @reg: mmio/rlc register
 * @v: value to write
 * @xcc_id: xcc accelerated compute core id
 *
 * this function is invoked only for the debugfs register access
 */
void amdgpu_mm_wreg_mmio_rlc(struct amdgpu_device *adev, uint32_t reg,
			     uint32_t v, uint32_t xcc_id)
{
	if (amdgpu_device_skip_hw_access(adev))
		return;

	if (amdgpu_sriov_fullaccess(adev) && adev->gfx.rlc.funcs &&
	    adev->gfx.rlc.funcs->is_rlcg_access_range) {
		if (adev->gfx.rlc.funcs->is_rlcg_access_range(adev, reg))
			return amdgpu_sriov_wreg(adev, reg, v, 0, 0, xcc_id);
	} else if ((reg * 4) >= adev->rmmio_size) {
		adev->pcie_wreg(adev, reg * 4, v);
	} else {
		writel(v, ((void __iomem *)adev->rmmio) + (reg * 4));
	}
}

/**
 * amdgpu_device_xcc_wreg - write to a memory mapped IO or indirect register with specific XCC
 *
 * @adev: amdgpu_device pointer
 * @reg: dword aligned register offset
 * @v: 32 bit value to write to the register
 * @acc_flags: access flags which require special behavior
 * @xcc_id: xcc accelerated compute core id
 *
 * Writes the value specified to the offset specified.
 */
void amdgpu_device_xcc_wreg(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v, uint32_t acc_flags, uint32_t xcc_id)
{
	uint32_t rlcg_flag;

	if (amdgpu_device_skip_hw_access(adev))
		return;

	if ((reg * 4) < adev->rmmio_size) {
		if (amdgpu_sriov_vf(adev) && !amdgpu_sriov_runtime(adev) &&
		    adev->gfx.rlc.rlcg_reg_access_supported &&
		    amdgpu_virt_get_rlcg_reg_access_flag(
			    adev, acc_flags, GC_HWIP, true, &rlcg_flag)) {
			amdgpu_virt_rlcg_reg_rw(adev, reg, v, rlcg_flag,
						GET_INST(GC, xcc_id));
		} else if (!(acc_flags & AMDGPU_REGS_NO_KIQ) &&
			   amdgpu_sriov_runtime(adev) &&
			   down_read_trylock(&adev->reset_domain->sem)) {
			amdgpu_kiq_wreg(adev, reg, v, xcc_id);
			up_read(&adev->reset_domain->sem);
		} else {
			writel(v, ((void __iomem *)adev->rmmio) + (reg * 4));
		}
	} else {
		adev->pcie_wreg(adev, reg * 4, v);
	}
}

/**
 * amdgpu_device_indirect_rreg - read an indirect register
 *
 * @adev: amdgpu_device pointer
 * @reg_addr: indirect register address to read from
 *
 * Returns the value of indirect register @reg_addr
 */
u32 amdgpu_device_indirect_rreg(struct amdgpu_device *adev, u32 reg_addr)
{
	unsigned long flags, pcie_index, pcie_data;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;
	u32 r;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	r = readl(pcie_data_offset);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	return r;
}

u32 amdgpu_device_indirect_rreg_ext(struct amdgpu_device *adev, u64 reg_addr)
{
	unsigned long flags, pcie_index, pcie_index_hi, pcie_data;
	u32 r;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_index_hi_offset;
	void __iomem *pcie_data_offset;

	if (unlikely(!adev->nbio.funcs)) {
		pcie_index = AMDGPU_PCIE_INDEX_FALLBACK;
		pcie_data = AMDGPU_PCIE_DATA_FALLBACK;
	} else {
		pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
		pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);
	}

	if (reg_addr >> 32) {
		if (unlikely(!adev->nbio.funcs))
			pcie_index_hi = AMDGPU_PCIE_INDEX_HI_FALLBACK;
		else
			pcie_index_hi =
				adev->nbio.funcs->get_pcie_index_hi_offset(
					adev);
	} else {
		pcie_index_hi = 0;
	}

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;
	if (pcie_index_hi != 0)
		pcie_index_hi_offset =
			(void __iomem *)adev->rmmio + pcie_index_hi * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	r = readl(pcie_data_offset);

	/* clear the high bits */
	if (pcie_index_hi != 0) {
		writel(0, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	return r;
}

/**
 * amdgpu_device_indirect_rreg64 - read a 64bits indirect register
 *
 * @adev: amdgpu_device pointer
 * @reg_addr: indirect register address to read from
 *
 * Returns the value of indirect register @reg_addr
 */
u64 amdgpu_device_indirect_rreg64(struct amdgpu_device *adev, u32 reg_addr)
{
	unsigned long flags, pcie_index, pcie_data;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;
	u64 r;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	/* read low 32 bits */
	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	r = readl(pcie_data_offset);
	/* read high 32 bits */
	writel(reg_addr + 4, pcie_index_offset);
	readl(pcie_index_offset);
	r |= ((u64)readl(pcie_data_offset) << 32);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	return r;
}

u64 amdgpu_device_indirect_rreg64_ext(struct amdgpu_device *adev, u64 reg_addr)
{
	unsigned long flags, pcie_index, pcie_data;
	unsigned long pcie_index_hi = 0;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_index_hi_offset;
	void __iomem *pcie_data_offset;
	u64 r;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);
	if ((reg_addr >> 32) && (adev->nbio.funcs->get_pcie_index_hi_offset))
		pcie_index_hi =
			adev->nbio.funcs->get_pcie_index_hi_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;
	if (pcie_index_hi != 0)
		pcie_index_hi_offset =
			(void __iomem *)adev->rmmio + pcie_index_hi * 4;

	/* read low 32 bits */
	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	r = readl(pcie_data_offset);
	/* read high 32 bits */
	writel(reg_addr + 4, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	r |= ((u64)readl(pcie_data_offset) << 32);

	/* clear the high bits */
	if (pcie_index_hi != 0) {
		writel(0, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);

	return r;
}

/**
 * amdgpu_device_indirect_wreg - write an indirect register address
 *
 * @adev: amdgpu_device pointer
 * @reg_addr: indirect register offset
 * @reg_data: indirect register data
 *
 */
void amdgpu_device_indirect_wreg(struct amdgpu_device *adev, u32 reg_addr,
				 u32 reg_data)
{
	unsigned long flags, pcie_index, pcie_data;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	writel(reg_data, pcie_data_offset);
	readl(pcie_data_offset);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

void amdgpu_device_indirect_wreg_ext(struct amdgpu_device *adev, u64 reg_addr,
				     u32 reg_data)
{
	unsigned long flags, pcie_index, pcie_index_hi, pcie_data;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_index_hi_offset;
	void __iomem *pcie_data_offset;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);
	if ((reg_addr >> 32) && (adev->nbio.funcs->get_pcie_index_hi_offset))
		pcie_index_hi =
			adev->nbio.funcs->get_pcie_index_hi_offset(adev);
	else
		pcie_index_hi = 0;

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;
	if (pcie_index_hi != 0)
		pcie_index_hi_offset =
			(void __iomem *)adev->rmmio + pcie_index_hi * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	writel(reg_data, pcie_data_offset);
	readl(pcie_data_offset);

	/* clear the high bits */
	if (pcie_index_hi != 0) {
		writel(0, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

/**
 * amdgpu_device_indirect_wreg64 - write a 64bits indirect register address
 *
 * @adev: amdgpu_device pointer
 * @reg_addr: indirect register offset
 * @reg_data: indirect register data
 *
 */
void amdgpu_device_indirect_wreg64(struct amdgpu_device *adev, u32 reg_addr,
				   u64 reg_data)
{
	unsigned long flags, pcie_index, pcie_data;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_data_offset;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	/* write low 32 bits */
	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	writel((u32)(reg_data & 0xffffffffULL), pcie_data_offset);
	readl(pcie_data_offset);
	/* write high 32 bits */
	writel(reg_addr + 4, pcie_index_offset);
	readl(pcie_index_offset);
	writel((u32)(reg_data >> 32), pcie_data_offset);
	readl(pcie_data_offset);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

void amdgpu_device_indirect_wreg64_ext(struct amdgpu_device *adev, u64 reg_addr,
				       u64 reg_data)
{
	unsigned long flags, pcie_index, pcie_data;
	unsigned long pcie_index_hi = 0;
	void __iomem *pcie_index_offset;
	void __iomem *pcie_index_hi_offset;
	void __iomem *pcie_data_offset;

	pcie_index = adev->nbio.funcs->get_pcie_index_offset(adev);
	pcie_data = adev->nbio.funcs->get_pcie_data_offset(adev);
	if ((reg_addr >> 32) && (adev->nbio.funcs->get_pcie_index_hi_offset))
		pcie_index_hi =
			adev->nbio.funcs->get_pcie_index_hi_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;
	if (pcie_index_hi != 0)
		pcie_index_hi_offset =
			(void __iomem *)adev->rmmio + pcie_index_hi * 4;

	/* write low 32 bits */
	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	writel((u32)(reg_data & 0xffffffffULL), pcie_data_offset);
	readl(pcie_data_offset);
	/* write high 32 bits */
	writel(reg_addr + 4, pcie_index_offset);
	readl(pcie_index_offset);
	if (pcie_index_hi != 0) {
		writel((reg_addr >> 32) & 0xff, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}
	writel((u32)(reg_data >> 32), pcie_data_offset);
	readl(pcie_data_offset);

	/* clear the high bits */
	if (pcie_index_hi != 0) {
		writel(0, pcie_index_hi_offset);
		readl(pcie_index_hi_offset);
	}

	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

u32 amdgpu_device_pcie_port_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
	return r;
}

void amdgpu_device_pcie_port_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->pcie_idx_lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	WREG32(data, v);
	(void)RREG32(data);
	spin_unlock_irqrestore(&adev->pcie_idx_lock, flags);
}

uint32_t amdgpu_device_wait_on_rreg(struct amdgpu_device *adev, uint32_t inst,
				    uint32_t reg_addr, char reg_name[],
				    uint32_t expected_value, uint32_t mask)
{
	uint32_t ret = 0;
	uint32_t old_ = 0;
	uint32_t tmp_ = RREG32(reg_addr);
	uint32_t loop = adev->usec_timeout;

	while ((tmp_ & (mask)) != (expected_value)) {
		if (old_ != tmp_) {
			loop = adev->usec_timeout;
			old_ = tmp_;
		} else
			udelay(1);
		tmp_ = RREG32(reg_addr);
		loop--;
		if (!loop) {
			dev_warn(
				adev->dev,
				"Register(%d) [%s] failed to reach value 0x%08x != 0x%08xn",
				inst, reg_name, (uint32_t)expected_value,
				(uint32_t)(tmp_ & (mask)));
			ret = -ETIMEDOUT;
			break;
		}
	}
	return ret;
}
