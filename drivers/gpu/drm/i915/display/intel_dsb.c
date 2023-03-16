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
#include "intel_dsb_regs.h"

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
	struct intel_crtc *crtc;

	/*
	 * maximum number of dwords the buffer will hold.
	 */
	unsigned int size;

	/*
	 * free_pos will point the first free dword and
	 * help in calculating tail of command buffer.
	 */
	unsigned int free_pos;

	/*
	 * ins_start_offset will help to store start dword of the dsb
	 * instuction and help in identifying the batch of auto-increment
	 * register.
	 */
	unsigned int ins_start_offset;
};

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
#define DSB_OPCODE_NOOP			0x0
#define DSB_OPCODE_MMIO_WRITE		0x1
#define DSB_OPCODE_WAIT_USEC		0x2
#define DSB_OPCODE_WAIT_LINES		0x3
#define DSB_OPCODE_WAIT_VBLANKS		0x4
#define DSB_OPCODE_WAIT_DSL_IN		0x5
#define DSB_OPCODE_WAIT_DSL_OUT		0x6
#define DSB_OPCODE_INTERRUPT		0x7
#define DSB_OPCODE_INDEXED_WRITE	0x9
#define DSB_OPCODE_POLL			0xA
#define DSB_BYTE_EN			0xF
#define DSB_BYTE_EN_SHIFT		20
#define DSB_REG_VALUE_MASK		0xfffff

static bool assert_dsb_has_room(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/* each instruction is 2 dwords */
	return !drm_WARN(&i915->drm, dsb->free_pos > dsb->size - 2,
			 "[CRTC:%d:%s] DSB %d buffer overflow\n",
			 crtc->base.base.id, crtc->base.name, dsb->id);
}

static bool is_dsb_busy(struct drm_i915_private *i915, enum pipe pipe,
			enum dsb_id id)
{
	return intel_de_read(i915, DSB_CTRL(pipe, id)) & DSB_STATUS_BUSY;
}

static void intel_dsb_emit(struct intel_dsb *dsb, u32 ldw, u32 udw)
{
	u32 *buf = dsb->cmd_buf;

	if (!assert_dsb_has_room(dsb))
		return;

	/* Every instruction should be 8 byte aligned. */
	dsb->free_pos = ALIGN(dsb->free_pos, 2);

	dsb->ins_start_offset = dsb->free_pos;

	buf[dsb->free_pos++] = ldw;
	buf[dsb->free_pos++] = udw;
}

static bool intel_dsb_prev_ins_is_write(struct intel_dsb *dsb,
					u32 opcode, i915_reg_t reg)
{
	const u32 *buf = dsb->cmd_buf;
	u32 prev_opcode, prev_reg;

	prev_opcode = buf[dsb->ins_start_offset + 1] >> DSB_OPCODE_SHIFT;
	prev_reg = buf[dsb->ins_start_offset + 1] & DSB_REG_VALUE_MASK;

	return prev_opcode == opcode && prev_reg == i915_mmio_reg_offset(reg);
}

static bool intel_dsb_prev_ins_is_mmio_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	return intel_dsb_prev_ins_is_write(dsb, DSB_OPCODE_MMIO_WRITE, reg);
}

static bool intel_dsb_prev_ins_is_indexed_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	return intel_dsb_prev_ins_is_write(dsb, DSB_OPCODE_INDEXED_WRITE, reg);
}

/**
 * intel_dsb_reg_write() - Emit register wriite to the DSB context
 * @dsb: DSB context
 * @reg: register address.
 * @val: value.
 *
 * This function is used for writing register-value pair in command
 * buffer of DSB.
 */
void intel_dsb_reg_write(struct intel_dsb *dsb,
			 i915_reg_t reg, u32 val)
{
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
	if (!intel_dsb_prev_ins_is_mmio_write(dsb, reg) &&
	    !intel_dsb_prev_ins_is_indexed_write(dsb, reg)) {
		intel_dsb_emit(dsb, val,
			       (DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT) |
			       (DSB_BYTE_EN << DSB_BYTE_EN_SHIFT) |
			       i915_mmio_reg_offset(reg));
	} else {
		u32 *buf = dsb->cmd_buf;

		if (!assert_dsb_has_room(dsb))
			return;

		/* convert to indexed write? */
		if (intel_dsb_prev_ins_is_mmio_write(dsb, reg)) {
			u32 prev_val = buf[dsb->ins_start_offset + 0];

			buf[dsb->ins_start_offset + 0] = 1; /* count */
			buf[dsb->ins_start_offset + 1] =
				(DSB_OPCODE_INDEXED_WRITE << DSB_OPCODE_SHIFT) |
				i915_mmio_reg_offset(reg);
			buf[dsb->ins_start_offset + 2] = prev_val;

			dsb->free_pos++;
		}

		buf[dsb->free_pos++] = val;
		/* Update the count */
		buf[dsb->ins_start_offset]++;

		/* if number of data words is odd, then the last dword should be 0.*/
		if (dsb->free_pos & 0x1)
			buf[dsb->free_pos] = 0;
	}
}

static void intel_dsb_align_tail(struct intel_dsb *dsb)
{
	u32 aligned_tail, tail;

	tail = dsb->free_pos * 4;
	aligned_tail = ALIGN(tail, CACHELINE_BYTES);

	if (aligned_tail > tail)
		memset(&dsb->cmd_buf[dsb->free_pos], 0,
		       aligned_tail - tail);

	dsb->free_pos = aligned_tail / 4;
}

void intel_dsb_finish(struct intel_dsb *dsb)
{
	intel_dsb_align_tail(dsb);
}

/**
 * intel_dsb_commit() - Trigger workload execution of DSB.
 * @dsb: DSB context
 * @wait_for_vblank: wait for vblank before executing
 *
 * This function is used to do actual write to hardware using DSB.
 */
void intel_dsb_commit(struct intel_dsb *dsb, bool wait_for_vblank)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;
	u32 tail;

	tail = dsb->free_pos * 4;
	if (drm_WARN_ON(&dev_priv->drm, !IS_ALIGNED(tail, CACHELINE_BYTES)))
		return;

	if (is_dsb_busy(dev_priv, pipe, dsb->id)) {
		drm_err(&dev_priv->drm, "[CRTC:%d:%s] DSB %d is busy\n",
			crtc->base.base.id, crtc->base.name, dsb->id);
		return;
	}

	intel_de_write(dev_priv, DSB_CTRL(pipe, dsb->id),
		       (wait_for_vblank ? DSB_WAIT_FOR_VBLANK : 0) |
		       DSB_ENABLE);
	intel_de_write(dev_priv, DSB_HEAD(pipe, dsb->id),
		       i915_ggtt_offset(dsb->vma));
	intel_de_write(dev_priv, DSB_TAIL(pipe, dsb->id),
		       i915_ggtt_offset(dsb->vma) + tail);
}

void intel_dsb_wait(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (wait_for(!is_dsb_busy(dev_priv, pipe, dsb->id), 1))
		drm_err(&dev_priv->drm,
			"[CRTC:%d:%s] DSB %d timed out waiting for idle\n",
			crtc->base.base.id, crtc->base.name, dsb->id);

	/* Attempt to reset it */
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	intel_de_write(dev_priv, DSB_CTRL(pipe, dsb->id), 0);
}

/**
 * intel_dsb_prepare() - Allocate, pin and map the DSB command buffer.
 * @crtc: the CRTC
 * @max_cmds: number of commands we need to fit into command buffer
 *
 * This function prepare the command buffer which is used to store dsb
 * instructions with data.
 *
 * Returns:
 * DSB context, NULL on failure
 */
struct intel_dsb *intel_dsb_prepare(struct intel_crtc *crtc,
				    unsigned int max_cmds)
{
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	struct drm_i915_gem_object *obj;
	intel_wakeref_t wakeref;
	struct intel_dsb *dsb;
	struct i915_vma *vma;
	unsigned int size;
	u32 *buf;

	if (!HAS_DSB(i915))
		return NULL;

	dsb = kzalloc(sizeof(*dsb), GFP_KERNEL);
	if (!dsb)
		goto out;

	wakeref = intel_runtime_pm_get(&i915->runtime_pm);

	/* ~1 qword per instruction, full cachelines */
	size = ALIGN(max_cmds * 8, CACHELINE_BYTES);

	obj = i915_gem_object_create_internal(i915, PAGE_ALIGN(size));
	if (IS_ERR(obj))
		goto out_put_rpm;

	vma = i915_gem_object_ggtt_pin(obj, NULL, 0, 0, 0);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		goto out_put_rpm;
	}

	buf = i915_gem_object_pin_map_unlocked(vma->obj, I915_MAP_WC);
	if (IS_ERR(buf)) {
		i915_vma_unpin_and_release(&vma, I915_VMA_RELEASE_MAP);
		goto out_put_rpm;
	}

	intel_runtime_pm_put(&i915->runtime_pm, wakeref);

	dsb->id = DSB1;
	dsb->vma = vma;
	dsb->crtc = crtc;
	dsb->cmd_buf = buf;
	dsb->size = size / 4; /* in dwords */
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;

	return dsb;

out_put_rpm:
	intel_runtime_pm_put(&i915->runtime_pm, wakeref);
	kfree(dsb);
out:
	drm_info_once(&i915->drm,
		      "[CRTC:%d:%s] DSB %d queue setup failed, will fallback to MMIO for display HW programming\n",
		      crtc->base.base.id, crtc->base.name, DSB1);

	return NULL;
}

/**
 * intel_dsb_cleanup() - To cleanup DSB context.
 * @dsb: DSB context
 *
 * This function cleanup the DSB context by unpinning and releasing
 * the VMA object associated with it.
 */
void intel_dsb_cleanup(struct intel_dsb *dsb)
{
	i915_vma_unpin_and_release(&dsb->vma, I915_VMA_RELEASE_MAP);
	kfree(dsb);
}
