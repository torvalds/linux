// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "i915_drv.h"
#include "intel_display_types.h"

#define DSB_BUF_SIZE    (2 * PAGE_SIZE)

/**
 * DOC: DSB
 *
 * A DSB (Display State Buffer) is a queue of MMIO instructions in the memory
 * which can be offloaded to DSB HW in Display Controller. DSB HW is a DMA
 * engine that can be programmed to download the DSB from memory.
 * It allows driver to batch submit display HW programming. This helps to
 * reduce loading time and CPU activity, thereby making the context switch
 * faster. DSB Support added from Gen12 Intel graphics based platform.
 *
 * DSB's can access only the pipe, plane, and transcoder Data Island Packet
 * registers.
 *
 * DSB HW can support only register writes (both indexed and direct MMIO
 * writes). There are no registers reads possible with DSB HW engine.
 */

/* DSB opcodes. */
#define DSB_OPCODE_SHIFT		24
#define DSB_OPCODE_MMIO_WRITE		0x1
#define DSB_OPCODE_INDEXED_WRITE	0x9
#define DSB_BYTE_EN			0xF
#define DSB_BYTE_EN_SHIFT		20
#define DSB_REG_VALUE_MASK		0xfffff

static bool is_dsb_busy(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	return DSB_STATUS & intel_de_read(dev_priv, DSB_CTRL(pipe, dsb->id));
}

static bool intel_dsb_enable_engine(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dsb_ctrl;

	dsb_ctrl = intel_de_read(dev_priv, DSB_CTRL(pipe, dsb->id));
	if (DSB_STATUS & dsb_ctrl) {
		drm_dbg_kms(&dev_priv->drm, "DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl |= DSB_ENABLE;
	intel_de_write(dev_priv, DSB_CTRL(pipe, dsb->id), dsb_ctrl);

	intel_de_posting_read(dev_priv, DSB_CTRL(pipe, dsb->id));
	return true;
}

static bool intel_dsb_disable_engine(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 dsb_ctrl;

	dsb_ctrl = intel_de_read(dev_priv, DSB_CTRL(pipe, dsb->id));
	if (DSB_STATUS & dsb_ctrl) {
		drm_dbg_kms(&dev_priv->drm, "DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl &= ~DSB_ENABLE;
	intel_de_write(dev_priv, DSB_CTRL(pipe, dsb->id), dsb_ctrl);

	intel_de_posting_read(dev_priv, DSB_CTRL(pipe, dsb->id));
	return true;
}

/**
 * intel_dsb_get() - Allocate DSB context and return a DSB instance.
 * @crtc: intel_crtc structure to get pipe info.
 *
 * This function provides handle of a DSB instance, for the further DSB
 * operations.
 *
 * Returns: address of Intel_dsb instance requested for.
 * Failure: Returns the same DSB instance, but without a command buffer.
 */

struct intel_dsb *
intel_dsb_get(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *i915 = to_i915(dev);
	struct intel_dsb *dsb = &crtc->dsb;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *buf;
	intel_wakeref_t wakeref;

	if (!HAS_DSB(i915))
		return dsb;

	if (dsb->refcount++ != 0)
		return dsb;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	obj = i915_gem_object_create_internal(i915, DSB_BUF_SIZE);
	if (IS_ERR(obj)) {
		drm_err(&i915->drm, "Gem object creation failed\n");
		goto out;
	}

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		drm_err(&i915->drm, "Vma creation failed\n");
		i915_gem_object_put(obj);
		goto out;
	}

	buf = i915_gem_object_pin_map(vma->obj, I915_MAP_WC);
	if (IS_ERR(buf)) {
		drm_err(&i915->drm, "Command buffer creation failed\n");
		goto out;
	}

	dsb->id = DSB1;
	dsb->vma = vma;
	dsb->cmd_buf = buf;

out:
	/*
	 * On error dsb->cmd_buf will continue to be NULL, making the writes
	 * pass-through. Leave the dangling ref to be removed later by the
	 * corresponding intel_dsb_put(): the important error message will
	 * already be logged above.
	 */

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	return dsb;
}

/**
 * intel_dsb_put() - To destroy DSB context.
 * @dsb: intel_dsb structure.
 *
 * This function destroys the DSB context allocated by a dsb_get(), by
 * unpinning and releasing the VMA object associated with it.
 */

void intel_dsb_put(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	if (!HAS_DSB(i915))
		return;

	if (drm_WARN_ON(&i915->drm, dsb->refcount == 0))
		return;

	if (--dsb->refcount == 0) {
		i915_vma_unpin_and_release(&dsb->vma, I915_VMA_RELEASE_MAP);
		dsb->cmd_buf = NULL;
		dsb->free_pos = 0;
		dsb->ins_start_offset = 0;
	}
}

/**
 * intel_dsb_indexed_reg_write() -Write to the DSB context for auto
 * increment register.
 * @dsb: intel_dsb structure.
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB for auto-increment register. During command buffer overflow,
 * a warning is thrown and rest all erroneous condition register programming
 * is done through mmio write.
 */

void intel_dsb_indexed_reg_write(struct intel_dsb *dsb, i915_reg_t reg,
				 u32 val)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 *buf = dsb->cmd_buf;
	u32 reg_val;

	if (!buf) {
		intel_de_write(dev_priv, reg, val);
		return;
	}

	if (drm_WARN_ON(&dev_priv->drm, dsb->free_pos >= DSB_BUF_SIZE)) {
		drm_dbg_kms(&dev_priv->drm, "DSB buffer overflow\n");
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

/**
 * intel_dsb_reg_write() -Write to the DSB context for normal
 * register.
 * @dsb: intel_dsb structure.
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB. During command buffer overflow, a warning  is thrown
 * and rest all erroneous condition register programming is done
 * through mmio write.
 */
void intel_dsb_reg_write(struct intel_dsb *dsb, i915_reg_t reg, u32 val)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 *buf = dsb->cmd_buf;

	if (!buf) {
		intel_de_write(dev_priv, reg, val);
		return;
	}

	if (drm_WARN_ON(&dev_priv->drm, dsb->free_pos >= DSB_BUF_SIZE)) {
		drm_dbg_kms(&dev_priv->drm, "DSB buffer overflow\n");
		return;
	}

	dsb->ins_start_offset = dsb->free_pos;
	buf[dsb->free_pos++] = val;
	buf[dsb->free_pos++] = (DSB_OPCODE_MMIO_WRITE  << DSB_OPCODE_SHIFT) |
			       (DSB_BYTE_EN << DSB_BYTE_EN_SHIFT) |
			       i915_mmio_reg_offset(reg);
}

/**
 * intel_dsb_commit() - Trigger workload execution of DSB.
 * @dsb: intel_dsb structure.
 *
 * This function is used to do actual write to hardware using DSB.
 * On errors, fall back to MMIO. Also this function help to reset the context.
 */
void intel_dsb_commit(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = container_of(dsb, typeof(*crtc), dsb);
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	u32 tail;

	if (!dsb->free_pos)
		return;

	if (!intel_dsb_enable_engine(dsb))
		goto reset;

	if (is_dsb_busy(dsb)) {
		drm_err(&dev_priv->drm,
			"HEAD_PTR write failed - dsb engine is busy.\n");
		goto reset;
	}
	intel_de_write(dev_priv, DSB_HEAD(pipe, dsb->id),
		       i915_ggtt_offset(dsb->vma));

	tail = ALIGN(dsb->free_pos * 4, CACHELINE_BYTES);
	if (tail > dsb->free_pos * 4)
		memset(&dsb->cmd_buf[dsb->free_pos], 0,
		       (tail - dsb->free_pos * 4));

	if (is_dsb_busy(dsb)) {
		drm_err(&dev_priv->drm,
			"TAIL_PTR write failed - dsb engine is busy.\n");
		goto reset;
	}
	drm_dbg_kms(&dev_priv->drm,
		    "DSB execution started - head 0x%x, tail 0x%x\n",
		    i915_ggtt_offset(dsb->vma), tail);
	intel_de_write(dev_priv, DSB_TAIL(pipe, dsb->id),
		       i915_ggtt_offset(dsb->vma) + tail);
	if (wait_for(!is_dsb_busy(dsb), 1)) {
		drm_err(&dev_priv->drm,
			"Timed out waiting for DSB workload completion.\n");
		goto reset;
	}

reset:
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	intel_dsb_disable_engine(dsb);
}
