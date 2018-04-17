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
	u64 gttmmio_gpa = intel_vgpu_get_bar_gpa(vgpu, PCI_BASE_ADDRESS_0);
	return gpa - gttmmio_gpa;
}

#define reg_is_mmio(gvt, reg)  \
	(reg >= 0 && reg < gvt->device_info.mmio_size)

#define reg_is_gtt(gvt, reg)   \
	(reg >= gvt->device_info.gtt_start_offset \
	 && reg < gvt->device_info.gtt_start_offset + gvt_ggtt_sz(gvt))

static bool vgpu_gpa_is_aperture(struct intel_vgpu *vgpu, uint64_t gpa)
{
	u64 aperture_gpa = intel_vgpu_get_bar_gpa(vgpu, PCI_BASE_ADDRESS_2);
	u64 aperture_sz = vgpu_aperture_sz(vgpu);

	return gpa >= aperture_gpa && gpa < aperture_gpa + aperture_sz;
}

static int vgpu_aperture_rw(struct intel_vgpu *vgpu, uint64_t gpa,
			    void *pdata, unsigned int size, bool is_read)
{
	u64 aperture_gpa = intel_vgpu_get_bar_gpa(vgpu, PCI_BASE_ADDRESS_2);
	u64 offset = gpa - aperture_gpa;

	if (!vgpu_gpa_is_aperture(vgpu, gpa + size - 1)) {
		gvt_vgpu_err("Aperture rw out of range, offset %llx, size %d\n",
			     offset, size);
		return -EINVAL;
	}

	if (!vgpu->gm.aperture_va) {
		gvt_vgpu_err("BAR is not enabled\n");
		return -ENXIO;
	}

	if (is_read)
		memcpy(pdata, vgpu->gm.aperture_va + offset, size);
	else
		memcpy(vgpu->gm.aperture_va + offset, pdata, size);
	return 0;
}

static void failsafe_emulate_mmio_rw(struct intel_vgpu *vgpu, uint64_t pa,
		void *p_data, unsigned int bytes, bool read)
{
	struct intel_gvt *gvt = NULL;
	void *pt = NULL;
	unsigned int offset = 0;

	if (!vgpu || !p_data)
		return;

	gvt = vgpu->gvt;
	mutex_lock(&gvt->lock);
	offset = intel_vgpu_gpa_to_mmio_offset(vgpu, pa);
	if (reg_is_mmio(gvt, offset)) {
		if (read)
			intel_vgpu_default_mmio_read(vgpu, offset, p_data,
					bytes);
		else
			intel_vgpu_default_mmio_write(vgpu, offset, p_data,
					bytes);
	} else if (reg_is_gtt(gvt, offset) &&
			vgpu->gtt.ggtt_mm->virtual_page_table) {
		offset -= gvt->device_info.gtt_start_offset;
		pt = vgpu->gtt.ggtt_mm->virtual_page_table + offset;
		if (read)
			memcpy(p_data, pt, bytes);
		else
			memcpy(pt, p_data, bytes);

	} else if (atomic_read(&vgpu->gtt.n_write_protected_guest_page)) {
		struct intel_vgpu_guest_page *gp;

		/* Since we enter the failsafe mode early during guest boot,
		 * guest may not have chance to set up its ppgtt table, so
		 * there should not be any wp pages for guest. Keep the wp
		 * related code here in case we need to handle it in furture.
		 */
		gp = intel_vgpu_find_guest_page(vgpu, pa >> PAGE_SHIFT);
		if (gp) {
			/* remove write protection to prevent furture traps */
			intel_vgpu_clean_guest_page(vgpu, gp);
			if (read)
				intel_gvt_hypervisor_read_gpa(vgpu, pa,
						p_data, bytes);
			else
				intel_gvt_hypervisor_write_gpa(vgpu, pa,
						p_data, bytes);
		}
	}
	mutex_unlock(&gvt->lock);
}

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
	unsigned int offset = 0;
	int ret = -EINVAL;


	if (vgpu->failsafe) {
		failsafe_emulate_mmio_rw(vgpu, pa, p_data, bytes, true);
		return 0;
	}
	mutex_lock(&gvt->lock);

	if (vgpu_gpa_is_aperture(vgpu, pa)) {
		ret = vgpu_aperture_rw(vgpu, pa, p_data, bytes, true);
		mutex_unlock(&gvt->lock);
		return ret;
	}

	if (atomic_read(&vgpu->gtt.n_write_protected_guest_page)) {
		struct intel_vgpu_guest_page *gp;

		gp = intel_vgpu_find_guest_page(vgpu, pa >> PAGE_SHIFT);
		if (gp) {
			ret = intel_gvt_hypervisor_read_gpa(vgpu, pa,
					p_data, bytes);
			if (ret) {
				gvt_vgpu_err("guest page read error %d, "
					"gfn 0x%lx, pa 0x%llx, var 0x%x, len %d\n",
					ret, gp->gfn, pa, *(u32 *)p_data,
					bytes);
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

	ret = intel_vgpu_mmio_reg_rw(vgpu, offset, p_data, bytes, true);
	if (ret < 0)
		goto err;

	intel_gvt_mmio_set_accessed(gvt, offset);
	mutex_unlock(&gvt->lock);
	return 0;
err:
	gvt_vgpu_err("fail to emulate MMIO read %08x len %d\n",
			offset, bytes);
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
	unsigned int offset = 0;
	int ret = -EINVAL;

	if (vgpu->failsafe) {
		failsafe_emulate_mmio_rw(vgpu, pa, p_data, bytes, false);
		return 0;
	}

	mutex_lock(&gvt->lock);

	if (vgpu_gpa_is_aperture(vgpu, pa)) {
		ret = vgpu_aperture_rw(vgpu, pa, p_data, bytes, false);
		mutex_unlock(&gvt->lock);
		return ret;
	}

	if (atomic_read(&vgpu->gtt.n_write_protected_guest_page)) {
		struct intel_vgpu_guest_page *gp;

		gp = intel_vgpu_find_guest_page(vgpu, pa >> PAGE_SHIFT);
		if (gp) {
			ret = gp->handler(gp, pa, p_data, bytes);
			if (ret) {
				gvt_err("guest page write error %d, "
					"gfn 0x%lx, pa 0x%llx, "
					"var 0x%x, len %d\n",
					ret, gp->gfn, pa,
					*(u32 *)p_data, bytes);
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

	ret = intel_vgpu_mmio_reg_rw(vgpu, offset, p_data, bytes, false);
	if (ret < 0)
		goto err;

	intel_gvt_mmio_set_accessed(gvt, offset);
	mutex_unlock(&gvt->lock);
	return 0;
err:
	gvt_vgpu_err("fail to emulate MMIO write %08x len %d\n", offset,
		     bytes);
	mutex_unlock(&gvt->lock);
	return ret;
}


/**
 * intel_vgpu_reset_mmio - reset virtual MMIO space
 * @vgpu: a vGPU
 *
 */
void intel_vgpu_reset_mmio(struct intel_vgpu *vgpu, bool dmlr)
{
	struct intel_gvt *gvt = vgpu->gvt;
	const struct intel_gvt_device_info *info = &gvt->device_info;
	void  *mmio = gvt->firmware.mmio;

	if (dmlr) {
		memcpy(vgpu->mmio.vreg, mmio, info->mmio_size);
		memcpy(vgpu->mmio.sreg, mmio, info->mmio_size);

		vgpu_vreg(vgpu, GEN6_GT_THREAD_STATUS_REG) = 0;

		/* set the bit 0:2(Core C-State ) to C0 */
		vgpu_vreg(vgpu, GEN6_GT_CORE_STATUS) = 0;

		vgpu->mmio.disable_warn_untrack = false;
	} else {
#define GVT_GEN8_MMIO_RESET_OFFSET		(0x44200)
		/* only reset the engine related, so starting with 0x44200
		 * interrupt include DE,display mmio related will not be
		 * touched
		 */
		memcpy(vgpu->mmio.vreg, mmio, GVT_GEN8_MMIO_RESET_OFFSET);
		memcpy(vgpu->mmio.sreg, mmio, GVT_GEN8_MMIO_RESET_OFFSET);
	}

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

	intel_vgpu_reset_mmio(vgpu, true);

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
