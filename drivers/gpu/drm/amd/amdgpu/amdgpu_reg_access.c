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

void amdgpu_reg_access_init(struct amdgpu_device *adev)
{
	spin_lock_init(&adev->reg.smc.lock);
	adev->reg.smc.rreg = NULL;
	adev->reg.smc.wreg = NULL;

	spin_lock_init(&adev->reg.uvd_ctx.lock);
	adev->reg.uvd_ctx.rreg = NULL;
	adev->reg.uvd_ctx.wreg = NULL;

	spin_lock_init(&adev->reg.didt.lock);
	adev->reg.didt.rreg = NULL;
	adev->reg.didt.wreg = NULL;

	spin_lock_init(&adev->reg.gc_cac.lock);
	adev->reg.gc_cac.rreg = NULL;
	adev->reg.gc_cac.wreg = NULL;

	spin_lock_init(&adev->reg.se_cac.lock);
	adev->reg.se_cac.rreg = NULL;
	adev->reg.se_cac.wreg = NULL;

	spin_lock_init(&adev->reg.audio_endpt.lock);
	adev->reg.audio_endpt.rreg = NULL;
	adev->reg.audio_endpt.wreg = NULL;

	spin_lock_init(&adev->reg.pcie.lock);
	adev->reg.pcie.rreg = NULL;
	adev->reg.pcie.wreg = NULL;
	adev->reg.pcie.rreg_ext = NULL;
	adev->reg.pcie.wreg_ext = NULL;
	adev->reg.pcie.rreg64 = NULL;
	adev->reg.pcie.wreg64 = NULL;
	adev->reg.pcie.rreg64_ext = NULL;
	adev->reg.pcie.wreg64_ext = NULL;
	adev->reg.pcie.port_rreg = NULL;
	adev->reg.pcie.port_wreg = NULL;
}

uint32_t amdgpu_reg_smc_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.smc.rreg) {
		dev_err_once(adev->dev, "SMC register read not supported\n");
		return 0;
	}
	return adev->reg.smc.rreg(adev, reg);
}

void amdgpu_reg_smc_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	if (!adev->reg.smc.wreg) {
		dev_err_once(adev->dev, "SMC register write not supported\n");
		return;
	}
	adev->reg.smc.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_uvd_ctx_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.uvd_ctx.rreg) {
		dev_err_once(adev->dev,
			     "UVD_CTX register read not supported\n");
		return 0;
	}
	return adev->reg.uvd_ctx.rreg(adev, reg);
}

void amdgpu_reg_uvd_ctx_wr32(struct amdgpu_device *adev, uint32_t reg,
			     uint32_t v)
{
	if (!adev->reg.uvd_ctx.wreg) {
		dev_err_once(adev->dev,
			     "UVD_CTX register write not supported\n");
		return;
	}
	adev->reg.uvd_ctx.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_didt_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.didt.rreg) {
		dev_err_once(adev->dev, "DIDT register read not supported\n");
		return 0;
	}
	return adev->reg.didt.rreg(adev, reg);
}

void amdgpu_reg_didt_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	if (!adev->reg.didt.wreg) {
		dev_err_once(adev->dev, "DIDT register write not supported\n");
		return;
	}
	adev->reg.didt.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_gc_cac_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.gc_cac.rreg) {
		dev_err_once(adev->dev, "GC_CAC register read not supported\n");
		return 0;
	}
	return adev->reg.gc_cac.rreg(adev, reg);
}

void amdgpu_reg_gc_cac_wr32(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v)
{
	if (!adev->reg.gc_cac.wreg) {
		dev_err_once(adev->dev,
			     "GC_CAC register write not supported\n");
		return;
	}
	adev->reg.gc_cac.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_se_cac_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.se_cac.rreg) {
		dev_err_once(adev->dev, "SE_CAC register read not supported\n");
		return 0;
	}
	return adev->reg.se_cac.rreg(adev, reg);
}

void amdgpu_reg_se_cac_wr32(struct amdgpu_device *adev, uint32_t reg,
			    uint32_t v)
{
	if (!adev->reg.se_cac.wreg) {
		dev_err_once(adev->dev,
			     "SE_CAC register write not supported\n");
		return;
	}
	adev->reg.se_cac.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_audio_endpt_rd32(struct amdgpu_device *adev, uint32_t block,
				     uint32_t reg)
{
	if (!adev->reg.audio_endpt.rreg) {
		dev_err_once(adev->dev,
			     "AUDIO_ENDPT register read not supported\n");
		return 0;
	}
	return adev->reg.audio_endpt.rreg(adev, block, reg);
}

void amdgpu_reg_audio_endpt_wr32(struct amdgpu_device *adev, uint32_t block,
				 uint32_t reg, uint32_t v)
{
	if (!adev->reg.audio_endpt.wreg) {
		dev_err_once(adev->dev,
			     "AUDIO_ENDPT register write not supported\n");
		return;
	}
	adev->reg.audio_endpt.wreg(adev, block, reg, v);
}

uint32_t amdgpu_reg_pcie_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.pcie.rreg) {
		dev_err_once(adev->dev, "PCIE register read not supported\n");
		return 0;
	}
	return adev->reg.pcie.rreg(adev, reg);
}

void amdgpu_reg_pcie_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	if (!adev->reg.pcie.wreg) {
		dev_err_once(adev->dev, "PCIE register write not supported\n");
		return;
	}
	adev->reg.pcie.wreg(adev, reg, v);
}

uint32_t amdgpu_reg_pcie_ext_rd32(struct amdgpu_device *adev, uint64_t reg)
{
	if (!adev->reg.pcie.rreg_ext) {
		dev_err_once(adev->dev, "PCIE EXT register read not supported\n");
		return 0;
	}
	return adev->reg.pcie.rreg_ext(adev, reg);
}

void amdgpu_reg_pcie_ext_wr32(struct amdgpu_device *adev, uint64_t reg,
			      uint32_t v)
{
	if (!adev->reg.pcie.wreg_ext) {
		dev_err_once(adev->dev, "PCIE EXT register write not supported\n");
		return;
	}
	adev->reg.pcie.wreg_ext(adev, reg, v);
}

uint64_t amdgpu_reg_pcie_rd64(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.pcie.rreg64) {
		dev_err_once(adev->dev, "PCIE 64-bit register read not supported\n");
		return 0;
	}
	return adev->reg.pcie.rreg64(adev, reg);
}

void amdgpu_reg_pcie_wr64(struct amdgpu_device *adev, uint32_t reg, uint64_t v)
{
	if (!adev->reg.pcie.wreg64) {
		dev_err_once(adev->dev, "PCIE 64-bit register write not supported\n");
		return;
	}
	adev->reg.pcie.wreg64(adev, reg, v);
}

uint64_t amdgpu_reg_pcie_ext_rd64(struct amdgpu_device *adev, uint64_t reg)
{
	if (!adev->reg.pcie.rreg64_ext) {
		dev_err_once(adev->dev, "PCIE EXT 64-bit register read not supported\n");
		return 0;
	}
	return adev->reg.pcie.rreg64_ext(adev, reg);
}

void amdgpu_reg_pcie_ext_wr64(struct amdgpu_device *adev, uint64_t reg,
			      uint64_t v)
{
	if (!adev->reg.pcie.wreg64_ext) {
		dev_err_once(adev->dev, "PCIE EXT 64-bit register write not supported\n");
		return;
	}
	adev->reg.pcie.wreg64_ext(adev, reg, v);
}

uint32_t amdgpu_reg_pciep_rd32(struct amdgpu_device *adev, uint32_t reg)
{
	if (!adev->reg.pcie.port_rreg) {
		dev_err_once(adev->dev, "PCIEP register read not supported\n");
		return 0;
	}
	return adev->reg.pcie.port_rreg(adev, reg);
}

void amdgpu_reg_pciep_wr32(struct amdgpu_device *adev, uint32_t reg, uint32_t v)
{
	if (!adev->reg.pcie.port_wreg) {
		dev_err_once(adev->dev, "PCIEP register write not supported\n");
		return;
	}
	adev->reg.pcie.port_wreg(adev, reg, v);
}

static int amdgpu_reg_get_smn_base_version(struct amdgpu_device *adev)
{
	struct pci_dev *pdev = adev->pdev;
	int id;

	if (amdgpu_sriov_vf(adev))
		return -EOPNOTSUPP;

	id = (pdev->device >> 4) & 0xFFFF;
	if (id == 0x74A || id == 0x74B || id == 0x75A || id == 0x75B)
		return 1;

	return -EOPNOTSUPP;
}

uint64_t amdgpu_reg_get_smn_base64(struct amdgpu_device *adev,
				   enum amd_hw_ip_block_type block,
				   int die_inst)
{
	if (!adev->reg.smn.get_smn_base) {
		int version = amdgpu_reg_get_smn_base_version(adev);
		switch (version) {
		case 1:
			return amdgpu_reg_smn_v1_0_get_base(adev, block,
							    die_inst);
		default:
			dev_err_once(
				adev->dev,
				"SMN base address query not supported for this device\n");
			return 0;
		}
	}
	return adev->reg.smn.get_smn_base(adev, block, die_inst);
}

uint64_t amdgpu_reg_smn_v1_0_get_base(struct amdgpu_device *adev,
				      enum amd_hw_ip_block_type block,
				      int die_inst)
{
	uint64_t smn_base;

	if (die_inst == 0)
		return 0;

	switch (block) {
	case XGMI_HWIP:
	case NBIO_HWIP:
	case MP0_HWIP:
	case UMC_HWIP:
	case DF_HWIP:
		smn_base = ((uint64_t)(die_inst & 0x3) << 32) | (1ULL << 34);
		break;
	default:
		dev_warn_once(
			adev->dev,
			"SMN base address query not supported for this block %d\n",
			block);
		smn_base = 0;
		break;
	}

	return smn_base;
}

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
		ret = amdgpu_reg_pcie_rd32(adev, reg * 4);
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
		ret = amdgpu_reg_pcie_rd32(adev, reg * 4);
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
		amdgpu_reg_pcie_wr32(adev, reg * 4, v);
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
		amdgpu_reg_pcie_wr32(adev, reg * 4, v);
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
		amdgpu_reg_pcie_wr32(adev, reg * 4, v);
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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	r = readl(pcie_data_offset);
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);

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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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

	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);

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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);

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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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

	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);

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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
	pcie_index_offset = (void __iomem *)adev->rmmio + pcie_index * 4;
	pcie_data_offset = (void __iomem *)adev->rmmio + pcie_data * 4;

	writel(reg_addr, pcie_index_offset);
	readl(pcie_index_offset);
	writel(reg_data, pcie_data_offset);
	readl(pcie_data_offset);
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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

	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
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

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
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

	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
}

u32 amdgpu_device_pcie_port_rreg(struct amdgpu_device *adev, u32 reg)
{
	unsigned long flags, address, data;
	u32 r;

	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	r = RREG32(data);
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
	return r;
}

void amdgpu_device_pcie_port_wreg(struct amdgpu_device *adev, u32 reg, u32 v)
{
	unsigned long flags, address, data;

	address = adev->nbio.funcs->get_pcie_port_index_offset(adev);
	data = adev->nbio.funcs->get_pcie_port_data_offset(adev);

	spin_lock_irqsave(&adev->reg.pcie.lock, flags);
	WREG32(address, reg * 4);
	(void)RREG32(address);
	WREG32(data, v);
	(void)RREG32(data);
	spin_unlock_irqrestore(&adev->reg.pcie.lock, flags);
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
