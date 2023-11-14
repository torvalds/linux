// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 *
 */

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_lmem.h"

#include "i915_drv.h"
#include "i915_irq.h"
#include "i915_reg.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_types.h"
#include "intel_dsb.h"
#include "intel_dsb_regs.h"
#include "intel_vblank.h"
#include "intel_vrr.h"
#include "skl_watermark.h"

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

	int dewake_scanline;
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
#define   DSB_BYTE_EN			0xf
#define   DSB_BYTE_EN_SHIFT		20
#define   DSB_REG_VALUE_MASK		0xfffff
#define DSB_OPCODE_WAIT_USEC		0x2
#define DSB_OPCODE_WAIT_SCANLINE	0x3
#define DSB_OPCODE_WAIT_VBLANKS		0x4
#define DSB_OPCODE_WAIT_DSL_IN		0x5
#define DSB_OPCODE_WAIT_DSL_OUT		0x6
#define   DSB_SCANLINE_UPPER_SHIFT	20
#define   DSB_SCANLINE_LOWER_SHIFT	0
#define DSB_OPCODE_INTERRUPT		0x7
#define DSB_OPCODE_INDEXED_WRITE	0x9
/* see DSB_REG_VALUE_MASK */
#define DSB_OPCODE_POLL			0xA
/* see DSB_REG_VALUE_MASK */

static bool assert_dsb_has_room(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);

	/* each instruction is 2 dwords */
	return !drm_WARN(&i915->drm, dsb->free_pos > dsb->size - 2,
			 "[CRTC:%d:%s] DSB %d buffer overflow\n",
			 crtc->base.base.id, crtc->base.name, dsb->id);
}

static void intel_dsb_dump(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *i915 = to_i915(crtc->base.dev);
	const u32 *buf = dsb->cmd_buf;
	int i;

	drm_dbg_kms(&i915->drm, "[CRTC:%d:%s] DSB %d commands {\n",
		    crtc->base.base.id, crtc->base.name, dsb->id);
	for (i = 0; i < ALIGN(dsb->free_pos, 64 / 4); i += 4)
		drm_dbg_kms(&i915->drm,
			    " 0x%08x: 0x%08x 0x%08x 0x%08x 0x%08x\n",
			    i * 4, buf[i], buf[i+1], buf[i+2], buf[i+3]);
	drm_dbg_kms(&i915->drm, "}\n");
}

static bool is_dsb_busy(struct drm_i915_private *i915, enum pipe pipe,
			enum dsb_id id)
{
	return intel_de_read_fw(i915, DSB_CTRL(pipe, id)) & DSB_STATUS_BUSY;
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

	/*
	 * Nothing emitted yet? Must check before looking
	 * at the actual data since i915_gem_object_create_internal()
	 * does *not* give you zeroed memory!
	 */
	if (dsb->free_pos == 0)
		return false;

	prev_opcode = buf[dsb->ins_start_offset + 1] & ~DSB_REG_VALUE_MASK;
	prev_reg = buf[dsb->ins_start_offset + 1] & DSB_REG_VALUE_MASK;

	return prev_opcode == opcode && prev_reg == i915_mmio_reg_offset(reg);
}

static bool intel_dsb_prev_ins_is_mmio_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	/* only full byte-enables can be converted to indexed writes */
	return intel_dsb_prev_ins_is_write(dsb,
					   DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT |
					   DSB_BYTE_EN << DSB_BYTE_EN_SHIFT,
					   reg);
}

static bool intel_dsb_prev_ins_is_indexed_write(struct intel_dsb *dsb, i915_reg_t reg)
{
	return intel_dsb_prev_ins_is_write(dsb,
					   DSB_OPCODE_INDEXED_WRITE << DSB_OPCODE_SHIFT,
					   reg);
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

static u32 intel_dsb_mask_to_byte_en(u32 mask)
{
	return (!!(mask & 0xff000000) << 3 |
		!!(mask & 0x00ff0000) << 2 |
		!!(mask & 0x0000ff00) << 1 |
		!!(mask & 0x000000ff) << 0);
}

/* Note: mask implemented via byte enables! */
void intel_dsb_reg_write_masked(struct intel_dsb *dsb,
				i915_reg_t reg, u32 mask, u32 val)
{
	intel_dsb_emit(dsb, val,
		       (DSB_OPCODE_MMIO_WRITE << DSB_OPCODE_SHIFT) |
		       (intel_dsb_mask_to_byte_en(mask) << DSB_BYTE_EN_SHIFT) |
		       i915_mmio_reg_offset(reg));
}

void intel_dsb_noop(struct intel_dsb *dsb, int count)
{
	int i;

	for (i = 0; i < count; i++)
		intel_dsb_emit(dsb, 0,
			       DSB_OPCODE_NOOP << DSB_OPCODE_SHIFT);
}

void intel_dsb_nonpost_start(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	enum pipe pipe = crtc->pipe;

	intel_dsb_reg_write_masked(dsb, DSB_CTRL(pipe, dsb->id),
				   DSB_NON_POSTED, DSB_NON_POSTED);
	intel_dsb_noop(dsb, 4);
}

void intel_dsb_nonpost_end(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	enum pipe pipe = crtc->pipe;

	intel_dsb_reg_write_masked(dsb, DSB_CTRL(pipe, dsb->id),
				   DSB_NON_POSTED, 0);
	intel_dsb_noop(dsb, 4);
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
	struct intel_crtc *crtc = dsb->crtc;

	/*
	 * DSB_FORCE_DEWAKE remains active even after DSB is
	 * disabled, so make sure to clear it (if set during
	 * intel_dsb_commit()).
	 */
	intel_dsb_reg_write_masked(dsb, DSB_PMCTRL_2(crtc->pipe, dsb->id),
				   DSB_FORCE_DEWAKE, 0);

	intel_dsb_align_tail(dsb);

	i915_gem_object_flush_map(dsb->vma->obj);
}

static int intel_dsb_dewake_scanline(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915 = to_i915(crtc_state->uapi.crtc->dev);
	const struct drm_display_mode *adjusted_mode = &crtc_state->hw.adjusted_mode;
	unsigned int latency = skl_watermark_max_latency(i915);
	int vblank_start;

	if (crtc_state->vrr.enable) {
		vblank_start = intel_vrr_vmin_vblank_start(crtc_state);
	} else {
		vblank_start = adjusted_mode->crtc_vblank_start;

		if (adjusted_mode->flags & DRM_MODE_FLAG_INTERLACE)
			vblank_start = DIV_ROUND_UP(vblank_start, 2);
	}

	return max(0, vblank_start - intel_usecs_to_scanlines(adjusted_mode, latency));
}

static void _intel_dsb_commit(struct intel_dsb *dsb, u32 ctrl,
			      int dewake_scanline)
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

	intel_de_write_fw(dev_priv, DSB_CTRL(pipe, dsb->id),
			  ctrl | DSB_ENABLE);

	intel_de_write_fw(dev_priv, DSB_HEAD(pipe, dsb->id),
			  i915_ggtt_offset(dsb->vma));

	if (dewake_scanline >= 0) {
		int diff, hw_dewake_scanline;

		hw_dewake_scanline = intel_crtc_scanline_to_hw(crtc, dewake_scanline);

		intel_de_write_fw(dev_priv, DSB_PMCTRL(pipe, dsb->id),
				  DSB_ENABLE_DEWAKE |
				  DSB_SCANLINE_FOR_DEWAKE(hw_dewake_scanline));

		/*
		 * Force DEwake immediately if we're already past
		 * or close to racing past the target scanline.
		 */
		diff = dewake_scanline - intel_get_crtc_scanline(crtc);
		intel_de_write_fw(dev_priv, DSB_PMCTRL_2(pipe, dsb->id),
				  (diff >= 0 && diff < 5 ? DSB_FORCE_DEWAKE : 0) |
				  DSB_BLOCK_DEWAKE_EXTENSION);
	}

	intel_de_write_fw(dev_priv, DSB_TAIL(pipe, dsb->id),
			  i915_ggtt_offset(dsb->vma) + tail);
}

/**
 * intel_dsb_commit() - Trigger workload execution of DSB.
 * @dsb: DSB context
 * @wait_for_vblank: wait for vblank before executing
 *
 * This function is used to do actual write to hardware using DSB.
 */
void intel_dsb_commit(struct intel_dsb *dsb,
		      bool wait_for_vblank)
{
	_intel_dsb_commit(dsb,
			  wait_for_vblank ? DSB_WAIT_FOR_VBLANK : 0,
			  wait_for_vblank ? dsb->dewake_scanline : -1);
}

void intel_dsb_wait(struct intel_dsb *dsb)
{
	struct intel_crtc *crtc = dsb->crtc;
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	enum pipe pipe = crtc->pipe;

	if (wait_for(!is_dsb_busy(dev_priv, pipe, dsb->id), 1)) {
		u32 offset = i915_ggtt_offset(dsb->vma);

		intel_de_write_fw(dev_priv, DSB_CTRL(pipe, dsb->id),
				  DSB_ENABLE | DSB_HALT);

		drm_err(&dev_priv->drm,
			"[CRTC:%d:%s] DSB %d timed out waiting for idle (current head=0x%x, head=0x%x, tail=0x%x)\n",
			crtc->base.base.id, crtc->base.name, dsb->id,
			intel_de_read_fw(dev_priv, DSB_CURRENT_HEAD(pipe, dsb->id)) - offset,
			intel_de_read_fw(dev_priv, DSB_HEAD(pipe, dsb->id)) - offset,
			intel_de_read_fw(dev_priv, DSB_TAIL(pipe, dsb->id)) - offset);

		intel_dsb_dump(dsb);
	}

	/* Attempt to reset it */
	dsb->free_pos = 0;
	dsb->ins_start_offset = 0;
	intel_de_write_fw(dev_priv, DSB_CTRL(pipe, dsb->id), 0);
}

/**
 * intel_dsb_prepare() - Allocate, pin and map the DSB command buffer.
 * @crtc_state: the CRTC state
 * @max_cmds: number of commands we need to fit into command buffer
 *
 * This function prepare the command buffer which is used to store dsb
 * instructions with data.
 *
 * Returns:
 * DSB context, NULL on failure
 */
struct intel_dsb *intel_dsb_prepare(const struct intel_crtc_state *crtc_state,
				    unsigned int max_cmds)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
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

	if (HAS_LMEM(i915)) {
		obj = i915_gem_object_create_lmem(i915, PAGE_ALIGN(size),
						  I915_BO_ALLOC_CONTIGUOUS);
		if (IS_ERR(obj))
			goto out_put_rpm;
	} else {
		obj = i915_gem_object_create_internal(i915, PAGE_ALIGN(size));
		if (IS_ERR(obj))
			goto out_put_rpm;

		i915_gem_object_set_cache_coherency(obj, I915_CACHE_NONE);
	}

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
	dsb->dewake_scanline = intel_dsb_dewake_scanline(crtc_state);

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
