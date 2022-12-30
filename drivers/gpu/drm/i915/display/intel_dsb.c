// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "gem/i915_gem_internal.h"

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsb.h"

struct i915_vma;

enum dsb_id {
	INVALID_DSB = -1,
	DSB1,
	DSB2,
	DSB3,
	MAX_DSB_PER_PIPE
};

struct intel_dsb {
	enum dsb_id id;
	u32 *cmd_buf;
	struct i915_vma *vma;

	/*
	 * free_pos will point the first free entry position
	 * and help in calculating tail of command buffer.
	 */
	int free_pos;

	/*
	 * ins_start_offset will help to store start address of the dsb
	 * instuction and help in identifying the batch of auto-increment
	 * register.
	 */
	u32 ins_start_offset;
};

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

static bool is_dsb_busy(struct drm_i915_private *i915, enum pipe pipe,
			enum dsb_id id)
{
	return DSB_STATUS & intel_de_read(i915, DSB_CTRL(pipe, id));
}

static bool intel_dsb_enable_engine(struct drm_i915_private *i915,
				    enum pipe pipe, enum dsb_id id)
{
	u32 dsb_ctrl;

	dsb_ctrl = intel_de_read(i915, DSB_CTRL(pipe, id));
	if (DSB_STATUS & dsb_ctrl) {
		drm_dbg_kms(&i915->drm, "DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl |= DSB_ENABLE;
	intel_de_write(i915, DSB_CTRL(pipe, id), dsb_ctrl);

	intel_de_posting_read(i915, DSB_CTRL(pipe, id));
	return true;
}

static bool intel_dsb_disable_engine(struct drm_i915_private *i915,
				     enum pipe pipe, enum dsb_id id)
{
	u32 dsb_ctrl;

	dsb_ctrl = intel_de_read(i915, DSB_CTRL(pipe, id));
	if (DSB_STATUS & dsb_ctrl) {
		drm_dbg_kms(&i915->drm, "DSB engine is busy.\n");
		return false;
	}

	dsb_ctrl &= ~DSB_ENABLE;
	intel_de_write(i915, DSB_CTRL(pipe, id), dsb_ctrl);

	intel_de_posting_read(i915, DSB_CTRL(pipe, id));
	return true;
}

/**
 * intel_dsb_indexed_reg_write() -Write to the DSB context for auto
 * increment register.
 * @crtc_state: intel_crtc_state structure
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB for auto-increment register. During command buffer overflow,
 * a warning is thrown and rest all erroneous condition register programming
 * is done through mmio write.
 */

void intel_dsb_indexed_reg_write(const struct intel_crtc_state *crtc_state,
				 i915_reg_t reg, u32 val)
{
	struct intel_dsb *dsb = crtc_state->dsb;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	u32 *buf;
	u32 reg_val;

	if (!dsb) {
		intel_de_write_fw(dev_priv, reg, val);
		return;
	}
	buf = dsb->cmd_buf;
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
 * @crtc_state: intel_crtc_state structure
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB. During command buffer overflow, a warning  is thrown
 * and rest all erroneous condition register programming is done
 * through mmio write.
 */
void intel_dsb_reg_write(const struct intel_crtc_state *crtc_state,
			 i915_reg_t reg, u32 val)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_dsb *dsb;
	u32 *buf;

	dsb = crtc_state->dsb;
	if (!dsb) {
		intel_de_write_fw(dev_priv, reg, val);
		return;
	}

	buf = dsb->cmd_buf;
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
 * @crtc_state: intel_crtc_state structure
 *
 * This function is used to do actual write to hardware using DSB.
 * On errors, fall back to MMIO. Also this function help to reset the context.
 */
void intel_dsb_commit(const struct intel_crtc_state *crtc_state)
{
	struct intel_dsb *dsb = crtc_state->dsb;
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_device *dev = crtc->base.dev;
	struct drm_i915_private *dev_priv = to_i915(dev);
	enum pipe pipe = crtc->pipe;
	u32 tail;

	if (!(dsb && dsb->free_pos))
		return;

	if (!intel_dsb_enable_engine(dev_priv, pipe, dsb->id))
		goto reset;

	if (is_dsb_busy(dev_priv, pipe, dsb->id)) {
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

	if (is_dsb_busy(dev_priv, pipe, dsb->id)) {
		drm_err(&dev_priv->drm,
			"TAIL_PTR write failed - dsb engine is busy.\n");
		goto reset;
	}
	drm_dbg_kms(&dev_priv->drm,
		    "DSB execution started - head 0x%x, tail 0x%x\n",
		    i915_ggtt_offset(dsb->vma), tail);
	intel_de_write(dev_priv, DSB_TAIL(pipe, dsb->id),
		       i915_ggtt_offset(dsb->vma) + tail);
	if (wait_for(!is_dsb_busy(dev_priv, pipe, dsb->id), 1)) {
		drm_err(&dev_priv->drm,
			"Timed out waiting for DSB workload completion.\n");
		goto reset;
	}

reset:
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	intel_dsb_disable_engine(dev_priv, pipe, dsb->id);
}

/**
 * intel_dsb_prepare() - Allocate, pin and map the DSB command buffer.
 * @crtc_state: intel_crtc_state structure to prepare associated dsb instance.
 *
 * This function prepare the command buffer which is used to store dsb
 * instructions with data.
 */
void intel_dsb_prepare(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct intel_dsb *dsb;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *buf;
	intel_wakeref_t wakeref;

	if (!HAS_DSB(i915))
		return;

	dsb = kmalloc(sizeof(*dsb), GFP_KERNEL);
	if (!dsb) {
		drm_err(&i915->drm, "DSB object creation failed\n");
		return;
	}

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	obj = i915_gem_object_create_internal(i915, DSB_BUF_SIZE);
	if (IS_ERR(obj)) {
		kfree(dsb);
		goto out;
	}

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		kfree(dsb);
		goto out;
	}

	buf = i915_gem_object_pin_map_unlocked(vma->obj, I915_MAP_WC);
	if (IS_ERR(buf)) {
		i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
		kfree(dsb);
		goto out;
	}

	dsb->id = DSB1;
	dsb->vma = vma;
	dsb->cmd_buf = buf;
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	crtc_state->dsb = dsb;
out:
	if (!crtc_state->dsb)
		drm_info(&i915->drm,
			 "DSB queue setup failed, will fallback to MMIO for display HW programming\n");

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
}

/**
 * intel_dsb_cleanup() - To cleanup DSB context.
 * @crtc_state: intel_crtc_state structure to cleanup associated dsb instance.
 *
 * This function cleanup the DSB context by unpinning and releasing
 * the VMA object associated with it.
 */
void intel_dsb_cleanup(struct intel_crtc_state *crtc_state)
{
	if (!crtc_state->dsb)
		return;

	i915_vma_unpin_and_release(&crtc_state->dsb->vma, I915_VMA_RELEASE_MAP);
	kfree(crtc_state->dsb);
	crtc_state->dsb = NULL;
}
