/*
 * Copyright(c) 2011-2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Ke Yu
 *    Kevin Tian <kevin.tian@intel.com>
 *    Dexuan Cui
 *
 * Contributors:
 *    Tina Zhang <tina.zhang@intel.com>
 *    Min He <min.he@intel.com>
 *    Niu Bing <bing.niu@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"

/**
 * intel_vgpu_gpa_to_mmio_offset - translate a GPA to MMIO offset
 * @vgpu: a vGPU
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
int intel_vgpu_gpa_to_mmio_offset(struct intel_vgpu *vgpu, u64 gpa)
{
	u64 gttmmio_gpa = *(u64 *)(vgpu_cfg_space(vgpu) + PCI_BASE_ADDRESS_0) &
			  ~GENMASK(3, 0);
	return gpa - gttmmio_gpa;
}

#define reg_is_mmio(gvt, reg)  \
	(reg >= 0 && reg < gvt->device_info.mmio_size)

#define reg_is_gtt(gvt, reg)   \
	(reg >= gvt->device_info.gtt_start_offset \
	 && reg < gvt->device_info.gtt_start_offset + gvt_ggtt_sz(gvt))

/**
 * intel_vgpu_emulate_mmio_read - emulate MMIO read
 * @vgpu: a vGPU
 * @pa: guest physical address
 * @p_data: data return buffer
 * @bytes: access data length
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
int intel_vgpu_emulate_mmio_read(struct intel_vgpu *vgpu, uint64_t pa,
		void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_mmio_info *mmio;
	unsigned int offset = 0;
	int ret = -EINVAL;

	mutex_lock(&gvt->lock);

	if (atomic_read(&vgpu->gtt.n_write_protected_guest_page)) {
		struct intel_vgpu_guest_page *gp;

		gp = intel_vgpu_find_guest_page(vgpu, pa >> PAGE_SHIFT);
		if (gp) {
			ret = intel_gvt_hypervisor_read_gpa(vgpu, pa,
					p_data, bytes);
			if (ret) {
				gvt_err("vgpu%d: guest page read error %d, "
					"gfn 0x%lx, pa 0x%llx, var 0x%x, len %d\n",
					vgpu->id, ret,
					gp->gfn, pa, *(u32 *)p_data, bytes);
			}
			mutex_unlock(&gvt->lock);
			return ret;
		}
	}

	offset = intel_vgpu_gpa_to_mmio_offset(vgpu, pa);

	if (WARN_ON(bytes > 8))
		goto err;

	if (reg_is_gtt(gvt, offset)) {
		if (WARN_ON(!IS_ALIGNED(offset, 4) && !IS_ALIGNED(offset, 8)))
			goto err;
		if (WARN_ON(bytes != 4 && bytes != 8))
			goto err;
		if (WARN_ON(!reg_is_gtt(gvt, offset + bytes - 1)))
			goto err;

		ret = intel_vgpu_emulate_gtt_mmio_read(vgpu, offset,
				p_data, bytes);
		if (ret)
			goto err;
		mutex_unlock(&gvt->lock);
		return ret;
	}

	if (WARN_ON_ONCE(!reg_is_mmio(gvt, offset))) {
		ret = intel_gvt_hypervisor_read_gpa(vgpu, pa, p_data, bytes);
		mutex_unlock(&gvt->lock);
		return ret;
	}

	if (WARN_ON(!reg_is_mmio(gvt, offset + bytes - 1)))
		goto err;

	if (!intel_gvt_mmio_is_unalign(gvt, offset)) {
		if (WARN_ON(!IS_ALIGNED(offset, bytes)))
			goto err;
	}

	mmio = intel_gvt_find_mmio_info(gvt, rounddown(offset, 4));
	if (mmio) {
		if (!intel_gvt_mmio_is_unalign(gvt, mmio->offset)) {
			if (WARN_ON(offset + bytes > mmio->offset + mmio->size))
				goto err;
			if (WARN_ON(mmio->offset != offset))
				goto err;
		}
		ret = mmio->read(vgpu, offset, p_data, bytes);
	} else {
		ret = intel_vgpu_default_mmio_read(vgpu, offset, p_data, bytes);

		if (!vgpu->mmio.disable_warn_untrack) {
			gvt_err("vgpu%d: read untracked MMIO %x(%dB) val %x\n",
				vgpu->id, offset, bytes, *(u32 *)p_data);

			if (offset == 0x206c) {
				gvt_err("------------------------------------------\n");
				gvt_err("vgpu%d: likely triggers a gfx reset\n",
					vgpu->id);
				gvt_err("------------------------------------------\n");
				vgpu->mmio.disable_warn_untrack = true;
			}
		}
	}

	if (ret)
		goto err;

	intel_gvt_mmio_set_accessed(gvt, offset);
	mutex_unlock(&gvt->lock);
	return 0;
err:
	gvt_err("vgpu%d: fail to emulate MMIO read %08x len %d\n",
			vgpu->id, offset, bytes);
	mutex_unlock(&gvt->lock);
	return ret;
}

/**
 * intel_vgpu_emulate_mmio_write - emulate MMIO write
 * @vgpu: a vGPU
 * @pa: guest physical address
 * @p_data: write data buffer
 * @bytes: access data length
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
int intel_vgpu_emulate_mmio_write(struct intel_vgpu *vgpu, uint64_t pa,
		void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_mmio_info *mmio;
	unsigned int offset = 0;
	u32 old_vreg = 0, old_sreg = 0;
	int ret = -EINVAL;

	mutex_lock(&gvt->lock);

	if (atomic_read(&vgpu->gtt.n_write_protected_guest_page)) {
		struct intel_vgpu_guest_page *gp;

		gp = intel_vgpu_find_guest_page(vgpu, pa >> PAGE_SHIFT);
		if (gp) {
			ret = gp->handler(gp, pa, p_data, bytes);
			if (ret) {
				gvt_err("vgpu%d: guest page write error %d, "
					"gfn 0x%lx, pa 0x%llx, var 0x%x, len %d\n",
					vgpu->id, ret,
					gp->gfn, pa, *(u32 *)p_data, bytes);
			}
			mutex_unlock(&gvt->lock);
			return ret;
		}
	}

	offset = intel_vgpu_gpa_to_mmio_offset(vgpu, pa);

	if (WARN_ON(bytes > 8))
		goto err;

	if (reg_is_gtt(gvt, offset)) {
		if (WARN_ON(!IS_ALIGNED(offset, 4) && !IS_ALIGNED(offset, 8)))
			goto err;
		if (WARN_ON(bytes != 4 && bytes != 8))
			goto err;
		if (WARN_ON(!reg_is_gtt(gvt, offset + bytes - 1)))
			goto err;

		ret = intel_vgpu_emulate_gtt_mmio_write(vgpu, offset,
				p_data, bytes);
		if (ret)
			goto err;
		mutex_unlock(&gvt->lock);
		return ret;
	}

	if (WARN_ON_ONCE(!reg_is_mmio(gvt, offset))) {
		ret = intel_gvt_hypervisor_write_gpa(vgpu, pa, p_data, bytes);
		mutex_unlock(&gvt->lock);
		return ret;
	}

	mmio = intel_gvt_find_mmio_info(gvt, rounddown(offset, 4));
	if (!mmio && !vgpu->mmio.disable_warn_untrack)
		gvt_err("vgpu%d: write untracked MMIO %x len %d val %x\n",
				vgpu->id, offset, bytes, *(u32 *)p_data);

	if (!intel_gvt_mmio_is_unalign(gvt, offset)) {
		if (WARN_ON(!IS_ALIGNED(offset, bytes)))
			goto err;
	}

	if (mmio) {
		u64 ro_mask = mmio->ro_mask;

		if (!intel_gvt_mmio_is_unalign(gvt, mmio->offset)) {
			if (WARN_ON(offset + bytes > mmio->offset + mmio->size))
				goto err;
			if (WARN_ON(mmio->offset != offset))
				goto err;
		}

		if (intel_gvt_mmio_has_mode_mask(gvt, mmio->offset)) {
			old_vreg = vgpu_vreg(vgpu, offset);
			old_sreg = vgpu_sreg(vgpu, offset);
		}

		if (!ro_mask) {
			ret = mmio->write(vgpu, offset, p_data, bytes);
		} else {
			/* Protect RO bits like HW */
			u64 data = 0;

			/* all register bits are RO. */
			if (ro_mask == ~(u64)0) {
				gvt_err("vgpu%d: try to write RO reg %x\n",
						vgpu->id, offset);
				ret = 0;
				goto out;
			}
			/* keep the RO bits in the virtual register */
			memcpy(&data, p_data, bytes);
			data &= ~mmio->ro_mask;
			data |= vgpu_vreg(vgpu, offset) & mmio->ro_mask;
			ret = mmio->write(vgpu, offset, &data, bytes);
		}

		/* higher 16bits of mode ctl regs are mask bits for change */
		if (intel_gvt_mmio_has_mode_mask(gvt, mmio->offset)) {
			u32 mask = vgpu_vreg(vgpu, offset) >> 16;

			vgpu_vreg(vgpu, offset) = (old_vreg & ~mask)
				| (vgpu_vreg(vgpu, offset) & mask);
			vgpu_sreg(vgpu, offset) = (old_sreg & ~mask)
				| (vgpu_sreg(vgpu, offset) & mask);
		}
	} else
		ret = intel_vgpu_default_mmio_write(vgpu, offset, p_data,
				bytes);
	if (ret)
		goto err;
out:
	intel_gvt_mmio_set_accessed(gvt, offset);
	mutex_unlock(&gvt->lock);
	return 0;
err:
	gvt_err("vgpu%d: fail to emulate MMIO write %08x len %d\n",
			vgpu->id, offset, bytes);
	mutex_unlock(&gvt->lock);
	return ret;
}


/**
 * intel_vgpu_reset_mmio - reset virtual MMIO space
 * @vgpu: a vGPU
 *
 */
void intel_vgpu_reset_mmio(struct intel_vgpu *vgpu)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;

	memcpy(vgpu->mmio.vreg, gvt->firmware.mmio, info->mmio_size);
	memcpy(vgpu->mmio.sreg, gvt->firmware.mmio, info->mmio_size);

	vgpu_vreg(vgpu, GEN6_GT_THREAD_STATUS_REG) = 0;

	/* set the bit 0:2(Core C-State ) to C0 */
	vgpu_vreg(vgpu, GEN6_GT_CORE_STATUS) = 0;
}

/**
 * intel_vgpu_init_mmio - init MMIO  space
 * @vgpu: a vGPU
 *
 * Returns:
 * Zero on success, negative error code if failed
 */
int intel_vgpu_init_mmio(struct intel_vgpu *vgpu)
{
	const struct intel_gvt_device_info *info = &vgpu->gvt->device_info;

	vgpu->mmio.vreg = vzalloc(info->mmio_size * 2);
	if (!vgpu->mmio.vreg)
		return -ENOMEM;

	vgpu->mmio.sreg = vgpu->mmio.vreg + info->mmio_size;

	intel_vgpu_reset_mmio(vgpu);

	return 0;
}

/**
 * intel_vgpu_clean_mmio - clean MMIO space
 * @vgpu: a vGPU
 *
 */
void intel_vgpu_clean_mmio(struct intel_vgpu *vgpu)
{
	vfree(vgpu->mmio.vreg);
	vgpu->mmio.vreg = vgpu->mmio.sreg = NULL;
}
