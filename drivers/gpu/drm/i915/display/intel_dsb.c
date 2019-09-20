// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "intel_display_types.h"

#define DSB_BUF_SIZE    (2 * PAGE_SIZE)

/* DSB opcodes. */
#define DSB_OPCODE_SHIFT		24
#define DSB_OPCODE_MMIO_WRITE		0x1
#define DSB_OPCODE_INDEXED_WRITE	0x9
#define DSB_BYTE_EN			0xF
#define DSB_BYTE_EN_SHIFT		20
#define DSB_REG_VALUE_MASK		0xfffff

static inline bool is_dsb_busy(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	return DSB_STATUS & I915_READ(DSB_CTRL(pipe, dsb->id));
}

static inline bool intel_dsb_enable_engine(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dsb_ctrl;

	dsb_ctrl = I915_READ(DSB_CTRL(pipe, dsb->id));
	if (DSB_STATUS & dsb_ctrl) {
		DRM_DEBUG_KMS("DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl |= DSB_ENABLE;
	I915_WRITE(DSB_CTRL(pipe, dsb->id), dsb_ctrl);

	POSTING_READ(DSB_CTRL(pipe, dsb->id));
	return true;
}

static inline bool intel_dsb_disable_engine(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dsb_ctrl;

	dsb_ctrl = I915_READ(DSB_CTRL(pipe, dsb->id));
	if (DSB_STATUS & dsb_ctrl) {
		DRM_DEBUG_KMS("DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl &= ~DSB_ENABLE;
	I915_WRITE(DSB_CTRL(pipe, dsb->id), dsb_ctrl);

	POSTING_READ(DSB_CTRL(pipe, dsb->id));
	return true;
}

struct intel_dsb *
intel_dsb_get(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_dsb *dsb = &crtc->dsb;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	intel_wakeref_t wakeref;

	if (!HAS_DSB(i915))
		return dsb;

	if (atomic_add_return(1, &dsb->refcount) != 1)
		return dsb;

	dsb->id = DSB1;
	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	obj = i915_gem_object_create_internal(i915, DSB_BUF_SIZE);
	if (IS_ERR(obj)) {
		DRM_ERROR("Gem object creation failed\n");
		goto err;
	}

	mutex_lock(&i915->drm.struct_mutex);
	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, PIN_MAPPABLE);
	mutex_unlock(&i915->drm.struct_mutex);
	if (IS_ERR(vma)) {
		DRM_ERROR("Vma creation failed\n");
		i915_gem_object_put(obj);
		atomic_dec(&dsb->refcount);
		goto err;
	}

	dsb->cmd_buf = i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
	if (IS_ERR(dsb->cmd_buf)) {
		DRM_ERROR("Command buffer creation failed\n");
		i915_vma_unpin_and_release(&vma, 0);
		dsb->cmd_buf = NULL;
		atomic_dec(&dsb->refcount);
		goto err;
	}
	dsb->vma = vma;

err:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	return dsb;
}

void intel_dsb_put(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (!HAS_DSB(i915))
		return;

	if (WARN_ON(atomic_read(&dsb->refcount) == 0))
		return;

	if (atomic_dec_and_test(&dsb->refcount)) {
		mutex_lock(&i915->drm.struct_mutex);
		i915_gem_object_unpin_map(dsb->vma->obj);
		i915_vma_unpin_and_release(&dsb->vma, 0);
		mutex_unlock(&i915->drm.struct_mutex);
		dsb->cmd_buf = NULL;
		dsb->free_pos = 0;
		dsb->ins_start_offset = 0;
	}
}

void intel_dsb_indexed_reg_write(struct intel_dsb *dsb, i915_reg_t reg,
				 u32 val)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 *buf = dsb->cmd_buf;
	u32 reg_val;

	if (!buf) {
		I915_WRITE(reg, val);
		return;
	}

	if (WARN_ON(dsb->free_pos >= DSB_BUF_SIZE)) {
		DRM_DEBUG_KMS("DSB buffer overflow\n");
		return;
	}

	/*
	 * For example the buffer will look like below for 3 dwords for auto
	 * increment register:
	 * +--------------------------------------------------------+
	 * | size = 3 | offset &| value1 | value2 | value3 | zero   |
	 * |          | opcode  |        |        |        |        |
	 * +--------------------------------------------------------+
	 * +          +         +        +        +        +        +
	 * 0          4         8        12       16       20       24
	 * Byte
	 *
	 * As every instruction is 8 byte aligned the index of dsb instruction
	 * will start always from even number while dealing with u32 array. If
	 * we are writing odd no of dwords, Zeros will be added in the end for
	 * padding.
	 */
	reg_val = buf[dsb->ins_start_offset + 1] & DSB_REG_VALUE_MASK;
	if (reg_val != i915_mmio_reg_offset(reg)) {
		/* Every instruction should be 8 byte aligned. */
		dsb->free_pos = ALIGN(dsb->free_pos, 2);

		dsb->ins_start_offset = dsb->free_pos;

		/* Update the size. */
		buf[dsb->free_pos++] = 1;

		/* Update the opcode and reg. */
		buf[dsb->free_pos++] = (DSB_OPCODE_INDEXED_WRITE  <<
					DSB_OPCODE_SHIFT) |
					i915_mmio_reg_offset(reg);

		/* Update the value. */
		buf[dsb->free_pos++] = val;
	} else {
		/* Update the new value. */
		buf[dsb->free_pos++] = val;

		/* Update the size. */
		buf[dsb->ins_start_offset]++;
	}

	/* if number of data words is odd, then the last dword should be 0.*/
	if (dsb->free_pos & 0x1)
		buf[dsb->free_pos] = 0;
}

void intel_dsb_reg_write(struct intel_dsb *dsb, i915_reg_t reg, u32 val)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 *buf = dsb->cmd_buf;

	if (!buf) {
		I915_WRITE(reg, val);
		return;
	}

	if (WARN_ON(dsb->free_pos >= DSB_BUF_SIZE)) {
		DRM_DEBUG_KMS("DSB buffer overflow\n");
		return;
	}

	dsb->ins_start_offset = dsb->free_pos;
	buf[dsb->free_pos++] = val;
	buf[dsb->free_pos++] = (DSB_OPCODE_MMIO_WRITE  << DSB_OPCODE_SHIFT) |
			       (DSB_BYTE_EN << DSB_BYTE_EN_SHIFT) |
			       i915_mmio_reg_offset(reg);
}
