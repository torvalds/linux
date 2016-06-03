/*
 * Copyright © 2013 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Brad Volkin <bradley.d.volkin@intel.com>
 *
 */

#include "i915_drv.h"

/**
 * DOC: batch buffer command parser
 *
 * Motivation:
 * Certain OpenGL features (e.g. transform feedback, performance monitoring)
 * require userspace code to submit batches containing commands such as
 * MI_LOAD_REGISTER_IMM to access various registers. Unfortunately, some
 * generations of the hardware will noop these commands in "unsecure" batches
 * (which includes all userspace batches submitted via i915) even though the
 * commands may be safe and represent the intended programming model of the
 * device.
 *
 * The software command parser is similar in operation to the command parsing
 * done in hardware for unsecure batches. However, the software parser allows
 * some operations that would be noop'd by hardware, if the parser determines
 * the operation is safe, and submits the batch as "secure" to prevent hardware
 * parsing.
 *
 * Threats:
 * At a high level, the hardware (and software) checks attempt to prevent
 * granting userspace undue privileges. There are three categories of privilege.
 *
 * First, commands which are explicitly defined as privileged or which should
 * only be used by the kernel driver. The parser generally rejects such
 * commands, though it may allow some from the drm master process.
 *
 * Second, commands which access registers. To support correct/enhanced
 * userspace functionality, particularly certain OpenGL extensions, the parser
 * provides a whitelist of registers which userspace may safely access (for both
 * normal and drm master processes).
 *
 * Third, commands which access privileged memory (i.e. GGTT, HWS page, etc).
 * The parser always rejects such commands.
 *
 * The majority of the problematic commands fall in the MI_* range, with only a
 * few specific commands on each ring (e.g. PIPE_CONTROL and MI_FLUSH_DW).
 *
 * Implementation:
 * Each ring maintains tables of commands and registers which the parser uses in
 * scanning batch buffers submitted to that ring.
 *
 * Since the set of commands that the parser must check for is significantly
 * smaller than the number of commands supported, the parser tables contain only
 * those commands required by the parser. This generally works because command
 * opcode ranges have standard command length encodings. So for commands that
 * the parser does not need to check, it can easily skip them. This is
 * implemented via a per-ring length decoding vfunc.
 *
 * Unfortunately, there are a number of commands that do not follow the standard
 * length encoding for their opcode range, primarily amongst the MI_* commands.
 * To handle this, the parser provides a way to define explicit "skip" entries
 * in the per-ring command tables.
 *
 * Other command table entries map fairly directly to high level categories
 * mentioned above: rejected, master-only, register whitelist. The parser
 * implements a number of checks, including the privileged memory checks, via a
 * general bitmasking mechanism.
 */

#define STD_MI_OPCODE_MASK  0xFF800000
#define STD_3D_OPCODE_MASK  0xFFFF0000
#define STD_2D_OPCODE_MASK  0xFFC00000
#define STD_MFX_OPCODE_MASK 0xFFFF0000

#define CMD(op, opm, f, lm, fl, ...)				\
	{							\
		.flags = (fl) | ((f) ? CMD_DESC_FIXED : 0),	\
		.cmd = { (op), (opm) },				\
		.length = { (lm) },				\
		__VA_ARGS__					\
	}

/* Convenience macros to compress the tables */
#define SMI STD_MI_OPCODE_MASK
#define S3D STD_3D_OPCODE_MASK
#define S2D STD_2D_OPCODE_MASK
#define SMFX STD_MFX_OPCODE_MASK
#define F true
#define S CMD_DESC_SKIP
#define R CMD_DESC_REJECT
#define W CMD_DESC_REGISTER
#define B CMD_DESC_BITMASK
#define M CMD_DESC_MASTER

/*            Command                          Mask   Fixed Len   Action
	      ---------------------------------------------------------- */
static const struct drm_i915_cmd_descriptor common_cmds[] = {
	CMD(  MI_NOOP,                          SMI,    F,  1,      S  ),
	CMD(  MI_USER_INTERRUPT,                SMI,    F,  1,      R  ),
	CMD(  MI_WAIT_FOR_EVENT,                SMI,    F,  1,      M  ),
	CMD(  MI_ARB_CHECK,                     SMI,    F,  1,      S  ),
	CMD(  MI_REPORT_HEAD,                   SMI,    F,  1,      S  ),
	CMD(  MI_SUSPEND_FLUSH,                 SMI,    F,  1,      S  ),
	CMD(  MI_SEMAPHORE_MBOX,                SMI,   !F,  0xFF,   R  ),
	CMD(  MI_STORE_DWORD_INDEX,             SMI,   !F,  0xFF,   R  ),
	CMD(  MI_LOAD_REGISTER_IMM(1),          SMI,   !F,  0xFF,   W,
	      .reg = { .offset = 1, .mask = 0x007FFFFC, .step = 2 }    ),
	CMD(  MI_STORE_REGISTER_MEM,            SMI,    F,  3,     W | B,
	      .reg = { .offset = 1, .mask = 0x007FFFFC },
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_LOAD_REGISTER_MEM,             SMI,    F,  3,     W | B,
	      .reg = { .offset = 1, .mask = 0x007FFFFC },
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	/*
	 * MI_BATCH_BUFFER_START requires some special handling. It's not
	 * really a 'skip' action but it doesn't seem like it's worth adding
	 * a new action. See i915_parse_cmds().
	 */
	CMD(  MI_BATCH_BUFFER_START,            SMI,   !F,  0xFF,   S  ),
};

static const struct drm_i915_cmd_descriptor render_cmds[] = {
	CMD(  MI_FLUSH,                         SMI,    F,  1,      S  ),
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_PREDICATE,                     SMI,    F,  1,      S  ),
	CMD(  MI_TOPOLOGY_FILTER,               SMI,    F,  1,      S  ),
	CMD(  MI_SET_APPID,                     SMI,    F,  1,      S  ),
	CMD(  MI_DISPLAY_FLIP,                  SMI,   !F,  0xFF,   R  ),
	CMD(  MI_SET_CONTEXT,                   SMI,   !F,  0xFF,   R  ),
	CMD(  MI_URB_CLEAR,                     SMI,   !F,  0xFF,   S  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0xFF,   R  ),
	CMD(  MI_CLFLUSH,                       SMI,   !F,  0x3FF,  B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_REPORT_PERF_COUNT,             SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 1,
			.mask = MI_REPORT_PERF_COUNT_GGTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  GFX_OP_3DSTATE_VF_STATISTICS,     S3D,    F,  1,      S  ),
	CMD(  PIPELINE_SELECT,                  S3D,    F,  1,      S  ),
	CMD(  MEDIA_VFE_STATE,			S3D,   !F,  0xFFFF, B,
	      .bits = {{
			.offset = 2,
			.mask = MEDIA_VFE_STATE_MMIO_ACCESS_MASK,
			.expected = 0,
	      }},						       ),
	CMD(  GPGPU_OBJECT,                     S3D,   !F,  0xFF,   S  ),
	CMD(  GPGPU_WALKER,                     S3D,   !F,  0xFF,   S  ),
	CMD(  GFX_OP_3DSTATE_SO_DECL_LIST,      S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_PIPE_CONTROL(5),           S3D,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 1,
			.mask = (PIPE_CONTROL_MMIO_WRITE | PIPE_CONTROL_NOTIFY),
			.expected = 0,
	      },
	      {
			.offset = 1,
		        .mask = (PIPE_CONTROL_GLOBAL_GTT_IVB |
				 PIPE_CONTROL_STORE_DATA_INDEX),
			.expected = 0,
			.condition_offset = 1,
			.condition_mask = PIPE_CONTROL_POST_SYNC_OP_MASK,
	      }},						       ),
};

static const struct drm_i915_cmd_descriptor hsw_render_cmds[] = {
	CMD(  MI_SET_PREDICATE,                 SMI,    F,  1,      S  ),
	CMD(  MI_RS_CONTROL,                    SMI,    F,  1,      S  ),
	CMD(  MI_URB_ATOMIC_ALLOC,              SMI,    F,  1,      S  ),
	CMD(  MI_SET_APPID,                     SMI,    F,  1,      S  ),
	CMD(  MI_RS_CONTEXT,                    SMI,    F,  1,      S  ),
	CMD(  MI_LOAD_SCAN_LINES_INCL,          SMI,   !F,  0x3F,   M  ),
	CMD(  MI_LOAD_SCAN_LINES_EXCL,          SMI,   !F,  0x3F,   R  ),
	CMD(  MI_LOAD_REGISTER_REG,             SMI,   !F,  0xFF,   R  ),
	CMD(  MI_RS_STORE_DATA_IMM,             SMI,   !F,  0xFF,   S  ),
	CMD(  MI_LOAD_URB_MEM,                  SMI,   !F,  0xFF,   S  ),
	CMD(  MI_STORE_URB_MEM,                 SMI,   !F,  0xFF,   S  ),
	CMD(  GFX_OP_3DSTATE_DX9_CONSTANTF_VS,  S3D,   !F,  0x7FF,  S  ),
	CMD(  GFX_OP_3DSTATE_DX9_CONSTANTF_PS,  S3D,   !F,  0x7FF,  S  ),

	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_VS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_GS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_HS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_DS,  S3D,   !F,  0x1FF,  S  ),
	CMD(  GFX_OP_3DSTATE_BINDING_TABLE_EDIT_PS,  S3D,   !F,  0x1FF,  S  ),
};

static const struct drm_i915_cmd_descriptor video_cmds[] = {
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_SET_APPID,                     SMI,    F,  1,      S  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      },
	      {
			.offset = 0,
			.mask = MI_FLUSH_DW_STORE_INDEX,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	/*
	 * MFX_WAIT doesn't fit the way we handle length for most commands.
	 * It has a length field but it uses a non-standard length bias.
	 * It is always 1 dword though, so just treat it as fixed length.
	 */
	CMD(  MFX_WAIT,                         SMFX,   F,  1,      S  ),
};

static const struct drm_i915_cmd_descriptor vecs_cmds[] = {
	CMD(  MI_ARB_ON_OFF,                    SMI,    F,  1,      R  ),
	CMD(  MI_SET_APPID,                     SMI,    F,  1,      S  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      },
	      {
			.offset = 0,
			.mask = MI_FLUSH_DW_STORE_INDEX,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  MI_CONDITIONAL_BATCH_BUFFER_END,  SMI,   !F,  0xFF,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
};

static const struct drm_i915_cmd_descriptor blt_cmds[] = {
	CMD(  MI_DISPLAY_FLIP,                  SMI,   !F,  0xFF,   R  ),
	CMD(  MI_STORE_DWORD_IMM,               SMI,   !F,  0x3FF,  B,
	      .bits = {{
			.offset = 0,
			.mask = MI_GLOBAL_GTT,
			.expected = 0,
	      }},						       ),
	CMD(  MI_UPDATE_GTT,                    SMI,   !F,  0x3F,   R  ),
	CMD(  MI_FLUSH_DW,                      SMI,   !F,  0x3F,   B,
	      .bits = {{
			.offset = 0,
			.mask = MI_FLUSH_DW_NOTIFY,
			.expected = 0,
	      },
	      {
			.offset = 1,
			.mask = MI_FLUSH_DW_USE_GTT,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      },
	      {
			.offset = 0,
			.mask = MI_FLUSH_DW_STORE_INDEX,
			.expected = 0,
			.condition_offset = 0,
			.condition_mask = MI_FLUSH_DW_OP_MASK,
	      }},						       ),
	CMD(  COLOR_BLT,                        S2D,   !F,  0x3F,   S  ),
	CMD(  SRC_COPY_BLT,                     S2D,   !F,  0x3F,   S  ),
};

static const struct drm_i915_cmd_descriptor hsw_blt_cmds[] = {
	CMD(  MI_LOAD_SCAN_LINES_INCL,          SMI,   !F,  0x3F,   M  ),
	CMD(  MI_LOAD_SCAN_LINES_EXCL,          SMI,   !F,  0x3F,   R  ),
};

#undef CMD
#undef SMI
#undef S3D
#undef S2D
#undef SMFX
#undef F
#undef S
#undef R
#undef W
#undef B
#undef M

static const struct drm_i915_cmd_table gen7_render_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ render_cmds, ARRAY_SIZE(render_cmds) },
};

static const struct drm_i915_cmd_table hsw_render_ring_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ render_cmds, ARRAY_SIZE(render_cmds) },
	{ hsw_render_cmds, ARRAY_SIZE(hsw_render_cmds) },
};

static const struct drm_i915_cmd_table gen7_video_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ video_cmds, ARRAY_SIZE(video_cmds) },
};

static const struct drm_i915_cmd_table hsw_vebox_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ vecs_cmds, ARRAY_SIZE(vecs_cmds) },
};

static const struct drm_i915_cmd_table gen7_blt_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ blt_cmds, ARRAY_SIZE(blt_cmds) },
};

static const struct drm_i915_cmd_table hsw_blt_ring_cmds[] = {
	{ common_cmds, ARRAY_SIZE(common_cmds) },
	{ blt_cmds, ARRAY_SIZE(blt_cmds) },
	{ hsw_blt_cmds, ARRAY_SIZE(hsw_blt_cmds) },
};

/*
 * Register whitelists, sorted by increasing register offset.
 */

/*
 * An individual whitelist entry granting access to register addr.  If
 * mask is non-zero the argument of immediate register writes will be
 * AND-ed with mask, and the command will be rejected if the result
 * doesn't match value.
 *
 * Registers with non-zero mask are only allowed to be written using
 * LRI.
 */
struct drm_i915_reg_descriptor {
	i915_reg_t addr;
	u32 mask;
	u32 value;
};

/* Convenience macro for adding 32-bit registers. */
#define REG32(_reg, ...) \
	{ .addr = (_reg), __VA_ARGS__ }

/*
 * Convenience macro for adding 64-bit registers.
 *
 * Some registers that userspace accesses are 64 bits. The register
 * access commands only allow 32-bit accesses. Hence, we have to include
 * entries for both halves of the 64-bit registers.
 */
#define REG64(_reg) \
	{ .addr = _reg }, \
	{ .addr = _reg ## _UDW }

#define REG64_IDX(_reg, idx) \
	{ .addr = _reg(idx) }, \
	{ .addr = _reg ## _UDW(idx) }

static const struct drm_i915_reg_descriptor gen7_render_regs[] = {
	REG64(GPGPU_THREADS_DISPATCHED),
	REG64(HS_INVOCATION_COUNT),
	REG64(DS_INVOCATION_COUNT),
	REG64(IA_VERTICES_COUNT),
	REG64(IA_PRIMITIVES_COUNT),
	REG64(VS_INVOCATION_COUNT),
	REG64(GS_INVOCATION_COUNT),
	REG64(GS_PRIMITIVES_COUNT),
	REG64(CL_INVOCATION_COUNT),
	REG64(CL_PRIMITIVES_COUNT),
	REG64(PS_INVOCATION_COUNT),
	REG64(PS_DEPTH_COUNT),
	REG64_IDX(RING_TIMESTAMP, RENDER_RING_BASE),
	REG32(OACONTROL), /* Only allowed for LRI and SRM. See below. */
	REG64(MI_PREDICATE_SRC0),
	REG64(MI_PREDICATE_SRC1),
	REG32(GEN7_3DPRIM_END_OFFSET),
	REG32(GEN7_3DPRIM_START_VERTEX),
	REG32(GEN7_3DPRIM_VERTEX_COUNT),
	REG32(GEN7_3DPRIM_INSTANCE_COUNT),
	REG32(GEN7_3DPRIM_START_INSTANCE),
	REG32(GEN7_3DPRIM_BASE_VERTEX),
	REG32(GEN7_GPGPU_DISPATCHDIMX),
	REG32(GEN7_GPGPU_DISPATCHDIMY),
	REG32(GEN7_GPGPU_DISPATCHDIMZ),
	REG64_IDX(GEN7_SO_NUM_PRIMS_WRITTEN, 0),
	REG64_IDX(GEN7_SO_NUM_PRIMS_WRITTEN, 1),
	REG64_IDX(GEN7_SO_NUM_PRIMS_WRITTEN, 2),
	REG64_IDX(GEN7_SO_NUM_PRIMS_WRITTEN, 3),
	REG64_IDX(GEN7_SO_PRIM_STORAGE_NEEDED, 0),
	REG64_IDX(GEN7_SO_PRIM_STORAGE_NEEDED, 1),
	REG64_IDX(GEN7_SO_PRIM_STORAGE_NEEDED, 2),
	REG64_IDX(GEN7_SO_PRIM_STORAGE_NEEDED, 3),
	REG32(GEN7_SO_WRITE_OFFSET(0)),
	REG32(GEN7_SO_WRITE_OFFSET(1)),
	REG32(GEN7_SO_WRITE_OFFSET(2)),
	REG32(GEN7_SO_WRITE_OFFSET(3)),
	REG32(GEN7_L3SQCREG1),
	REG32(GEN7_L3CNTLREG2),
	REG32(GEN7_L3CNTLREG3),
};

static const struct drm_i915_reg_descriptor hsw_render_regs[] = {
	REG64_IDX(HSW_CS_GPR, 0),
	REG64_IDX(HSW_CS_GPR, 1),
	REG64_IDX(HSW_CS_GPR, 2),
	REG64_IDX(HSW_CS_GPR, 3),
	REG64_IDX(HSW_CS_GPR, 4),
	REG64_IDX(HSW_CS_GPR, 5),
	REG64_IDX(HSW_CS_GPR, 6),
	REG64_IDX(HSW_CS_GPR, 7),
	REG64_IDX(HSW_CS_GPR, 8),
	REG64_IDX(HSW_CS_GPR, 9),
	REG64_IDX(HSW_CS_GPR, 10),
	REG64_IDX(HSW_CS_GPR, 11),
	REG64_IDX(HSW_CS_GPR, 12),
	REG64_IDX(HSW_CS_GPR, 13),
	REG64_IDX(HSW_CS_GPR, 14),
	REG64_IDX(HSW_CS_GPR, 15),
	REG32(HSW_SCRATCH1,
	      .mask = ~HSW_SCRATCH1_L3_DATA_ATOMICS_DISABLE,
	      .value = 0),
	REG32(HSW_ROW_CHICKEN3,
	      .mask = ~(HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE << 16 |
                        HSW_ROW_CHICKEN3_L3_GLOBAL_ATOMICS_DISABLE),
	      .value = 0),
};

static const struct drm_i915_reg_descriptor gen7_blt_regs[] = {
	REG32(BCS_SWCTRL),
};

static const struct drm_i915_reg_descriptor ivb_master_regs[] = {
	REG32(FORCEWAKE_MT),
	REG32(DERRMR),
	REG32(GEN7_PIPE_DE_LOAD_SL(PIPE_A)),
	REG32(GEN7_PIPE_DE_LOAD_SL(PIPE_B)),
	REG32(GEN7_PIPE_DE_LOAD_SL(PIPE_C)),
};

static const struct drm_i915_reg_descriptor hsw_master_regs[] = {
	REG32(FORCEWAKE_MT),
	REG32(DERRMR),
};

#undef REG64
#undef REG32

struct drm_i915_reg_table {
	const struct drm_i915_reg_descriptor *regs;
	int num_regs;
	bool master;
};

static const struct drm_i915_reg_table ivb_render_reg_tables[] = {
	{ gen7_render_regs, ARRAY_SIZE(gen7_render_regs), false },
	{ ivb_master_regs, ARRAY_SIZE(ivb_master_regs), true },
};

static const struct drm_i915_reg_table ivb_blt_reg_tables[] = {
	{ gen7_blt_regs, ARRAY_SIZE(gen7_blt_regs), false },
	{ ivb_master_regs, ARRAY_SIZE(ivb_master_regs), true },
};

static const struct drm_i915_reg_table hsw_render_reg_tables[] = {
	{ gen7_render_regs, ARRAY_SIZE(gen7_render_regs), false },
	{ hsw_render_regs, ARRAY_SIZE(hsw_render_regs), false },
	{ hsw_master_regs, ARRAY_SIZE(hsw_master_regs), true },
};

static const struct drm_i915_reg_table hsw_blt_reg_tables[] = {
	{ gen7_blt_regs, ARRAY_SIZE(gen7_blt_regs), false },
	{ hsw_master_regs, ARRAY_SIZE(hsw_master_regs), true },
};

static u32 gen7_render_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;
	u32 subclient =
		(cmd_header & INSTR_SUBCLIENT_MASK) >> INSTR_SUBCLIENT_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_RC_CLIENT) {
		if (subclient == INSTR_MEDIA_SUBCLIENT)
			return 0xFFFF;
		else
			return 0xFF;
	}

	DRM_DEBUG_DRIVER("CMD: Abnormal rcs cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static u32 gen7_bsd_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;
	u32 subclient =
		(cmd_header & INSTR_SUBCLIENT_MASK) >> INSTR_SUBCLIENT_SHIFT;
	u32 op = (cmd_header & INSTR_26_TO_24_MASK) >> INSTR_26_TO_24_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_RC_CLIENT) {
		if (subclient == INSTR_MEDIA_SUBCLIENT) {
			if (op == 6)
				return 0xFFFF;
			else
				return 0xFFF;
		} else
			return 0xFF;
	}

	DRM_DEBUG_DRIVER("CMD: Abnormal bsd cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static u32 gen7_blt_get_cmd_length_mask(u32 cmd_header)
{
	u32 client = (cmd_header & INSTR_CLIENT_MASK) >> INSTR_CLIENT_SHIFT;

	if (client == INSTR_MI_CLIENT)
		return 0x3F;
	else if (client == INSTR_BC_CLIENT)
		return 0xFF;

	DRM_DEBUG_DRIVER("CMD: Abnormal blt cmd length! 0x%08X\n", cmd_header);
	return 0;
}

static bool validate_cmds_sorted(struct intel_engine_cs *engine,
				 const struct drm_i915_cmd_table *cmd_tables,
				 int cmd_table_count)
{
	int i;
	bool ret = true;

	if (!cmd_tables || cmd_table_count == 0)
		return true;

	for (i = 0; i < cmd_table_count; i++) {
		const struct drm_i915_cmd_table *table = &cmd_tables[i];
		u32 previous = 0;
		int j;

		for (j = 0; j < table->count; j++) {
			const struct drm_i915_cmd_descriptor *desc =
				&table->table[j];
			u32 curr = desc->cmd.value & desc->cmd.mask;

			if (curr < previous) {
				DRM_ERROR("CMD: table not sorted ring=%d table=%d entry=%d cmd=0x%08X prev=0x%08X\n",
					  engine->id, i, j, curr, previous);
				ret = false;
			}

			previous = curr;
		}
	}

	return ret;
}

static bool check_sorted(int ring_id,
			 const struct drm_i915_reg_descriptor *reg_table,
			 int reg_count)
{
	int i;
	u32 previous = 0;
	bool ret = true;

	for (i = 0; i < reg_count; i++) {
		u32 curr = i915_mmio_reg_offset(reg_table[i].addr);

		if (curr < previous) {
			DRM_ERROR("CMD: table not sorted ring=%d entry=%d reg=0x%08X prev=0x%08X\n",
				  ring_id, i, curr, previous);
			ret = false;
		}

		previous = curr;
	}

	return ret;
}

static bool validate_regs_sorted(struct intel_engine_cs *engine)
{
	int i;
	const struct drm_i915_reg_table *table;

	for (i = 0; i < engine->reg_table_count; i++) {
		table = &engine->reg_tables[i];
		if (!check_sorted(engine->id, table->regs, table->num_regs))
			return false;
	}

	return true;
}

struct cmd_node {
	const struct drm_i915_cmd_descriptor *desc;
	struct hlist_node node;
};

/*
 * Different command ranges have different numbers of bits for the opcode. For
 * example, MI commands use bits 31:23 while 3D commands use bits 31:16. The
 * problem is that, for example, MI commands use bits 22:16 for other fields
 * such as GGTT vs PPGTT bits. If we include those bits in the mask then when
 * we mask a command from a batch it could hash to the wrong bucket due to
 * non-opcode bits being set. But if we don't include those bits, some 3D
 * commands may hash to the same bucket due to not including opcode bits that
 * make the command unique. For now, we will risk hashing to the same bucket.
 *
 * If we attempt to generate a perfect hash, we should be able to look at bits
 * 31:29 of a command from a batch buffer and use the full mask for that
 * client. The existing INSTR_CLIENT_MASK/SHIFT defines can be used for this.
 */
#define CMD_HASH_MASK STD_MI_OPCODE_MASK

static int init_hash_table(struct intel_engine_cs *engine,
			   const struct drm_i915_cmd_table *cmd_tables,
			   int cmd_table_count)
{
	int i, j;

	hash_init(engine->cmd_hash);

	for (i = 0; i < cmd_table_count; i++) {
		const struct drm_i915_cmd_table *table = &cmd_tables[i];

		for (j = 0; j < table->count; j++) {
			const struct drm_i915_cmd_descriptor *desc =
				&table->table[j];
			struct cmd_node *desc_node =
				kmalloc(sizeof(*desc_node), GFP_KERNEL);

			if (!desc_node)
				return -ENOMEM;

			desc_node->desc = desc;
			hash_add(engine->cmd_hash, &desc_node->node,
				 desc->cmd.value & CMD_HASH_MASK);
		}
	}

	return 0;
}

static void fini_hash_table(struct intel_engine_cs *engine)
{
	struct hlist_node *tmp;
	struct cmd_node *desc_node;
	int i;

	hash_for_each_safe(engine->cmd_hash, i, tmp, desc_node, node) {
		hash_del(&desc_node->node);
		kfree(desc_node);
	}
}

/**
 * i915_cmd_parser_init_ring() - set cmd parser related fields for a ringbuffer
 * @ring: the ringbuffer to initialize
 *
 * Optionally initializes fields related to batch buffer command parsing in the
 * struct intel_engine_cs based on whether the platform requires software
 * command parsing.
 *
 * Return: non-zero if initialization fails
 */
int i915_cmd_parser_init_ring(struct intel_engine_cs *engine)
{
	const struct drm_i915_cmd_table *cmd_tables;
	int cmd_table_count;
	int ret;

	if (!IS_GEN7(engine->dev))
		return 0;

	switch (engine->id) {
	case RCS:
		if (IS_HASWELL(engine->dev)) {
			cmd_tables = hsw_render_ring_cmds;
			cmd_table_count =
				ARRAY_SIZE(hsw_render_ring_cmds);
		} else {
			cmd_tables = gen7_render_cmds;
			cmd_table_count = ARRAY_SIZE(gen7_render_cmds);
		}

		if (IS_HASWELL(engine->dev)) {
			engine->reg_tables = hsw_render_reg_tables;
			engine->reg_table_count = ARRAY_SIZE(hsw_render_reg_tables);
		} else {
			engine->reg_tables = ivb_render_reg_tables;
			engine->reg_table_count = ARRAY_SIZE(ivb_render_reg_tables);
		}

		engine->get_cmd_length_mask = gen7_render_get_cmd_length_mask;
		break;
	case VCS:
		cmd_tables = gen7_video_cmds;
		cmd_table_count = ARRAY_SIZE(gen7_video_cmds);
		engine->get_cmd_length_mask = gen7_bsd_get_cmd_length_mask;
		break;
	case BCS:
		if (IS_HASWELL(engine->dev)) {
			cmd_tables = hsw_blt_ring_cmds;
			cmd_table_count = ARRAY_SIZE(hsw_blt_ring_cmds);
		} else {
			cmd_tables = gen7_blt_cmds;
			cmd_table_count = ARRAY_SIZE(gen7_blt_cmds);
		}

		if (IS_HASWELL(engine->dev)) {
			engine->reg_tables = hsw_blt_reg_tables;
			engine->reg_table_count = ARRAY_SIZE(hsw_blt_reg_tables);
		} else {
			engine->reg_tables = ivb_blt_reg_tables;
			engine->reg_table_count = ARRAY_SIZE(ivb_blt_reg_tables);
		}

		engine->get_cmd_length_mask = gen7_blt_get_cmd_length_mask;
		break;
	case VECS:
		cmd_tables = hsw_vebox_cmds;
		cmd_table_count = ARRAY_SIZE(hsw_vebox_cmds);
		/* VECS can use the same length_mask function as VCS */
		engine->get_cmd_length_mask = gen7_bsd_get_cmd_length_mask;
		break;
	default:
		DRM_ERROR("CMD: cmd_parser_init with unknown ring: %d\n",
			  engine->id);
		BUG();
	}

	BUG_ON(!validate_cmds_sorted(engine, cmd_tables, cmd_table_count));
	BUG_ON(!validate_regs_sorted(engine));

	WARN_ON(!hash_empty(engine->cmd_hash));

	ret = init_hash_table(engine, cmd_tables, cmd_table_count);
	if (ret) {
		DRM_ERROR("CMD: cmd_parser_init failed!\n");
		fini_hash_table(engine);
		return ret;
	}

	engine->needs_cmd_parser = true;

	return 0;
}

/**
 * i915_cmd_parser_fini_ring() - clean up cmd parser related fields
 * @ring: the ringbuffer to clean up
 *
 * Releases any resources related to command parsing that may have been
 * initialized for the specified ring.
 */
void i915_cmd_parser_fini_ring(struct intel_engine_cs *engine)
{
	if (!engine->needs_cmd_parser)
		return;

	fini_hash_table(engine);
}

static const struct drm_i915_cmd_descriptor*
find_cmd_in_table(struct intel_engine_cs *engine,
		  u32 cmd_header)
{
	struct cmd_node *desc_node;

	hash_for_each_possible(engine->cmd_hash, desc_node, node,
			       cmd_header & CMD_HASH_MASK) {
		const struct drm_i915_cmd_descriptor *desc = desc_node->desc;
		u32 masked_cmd = desc->cmd.mask & cmd_header;
		u32 masked_value = desc->cmd.value & desc->cmd.mask;

		if (masked_cmd == masked_value)
			return desc;
	}

	return NULL;
}

/*
 * Returns a pointer to a descriptor for the command specified by cmd_header.
 *
 * The caller must supply space for a default descriptor via the default_desc
 * parameter. If no descriptor for the specified command exists in the ring's
 * command parser tables, this function fills in default_desc based on the
 * ring's default length encoding and returns default_desc.
 */
static const struct drm_i915_cmd_descriptor*
find_cmd(struct intel_engine_cs *engine,
	 u32 cmd_header,
	 struct drm_i915_cmd_descriptor *default_desc)
{
	const struct drm_i915_cmd_descriptor *desc;
	u32 mask;

	desc = find_cmd_in_table(engine, cmd_header);
	if (desc)
		return desc;

	mask = engine->get_cmd_length_mask(cmd_header);
	if (!mask)
		return NULL;

	BUG_ON(!default_desc);
	default_desc->flags = CMD_DESC_SKIP;
	default_desc->length.mask = mask;

	return default_desc;
}

static const struct drm_i915_reg_descriptor *
find_reg(const struct drm_i915_reg_descriptor *table,
	 int count, u32 addr)
{
	int i;

	for (i = 0; i < count; i++) {
		if (i915_mmio_reg_offset(table[i].addr) == addr)
			return &table[i];
	}

	return NULL;
}

static const struct drm_i915_reg_descriptor *
find_reg_in_tables(const struct drm_i915_reg_table *tables,
		   int count, bool is_master, u32 addr)
{
	int i;
	const struct drm_i915_reg_table *table;
	const struct drm_i915_reg_descriptor *reg;

	for (i = 0; i < count; i++) {
		table = &tables[i];
		if (!table->master || is_master) {
			reg = find_reg(table->regs, table->num_regs,
				       addr);
			if (reg != NULL)
				return reg;
		}
	}

	return NULL;
}

static u32 *vmap_batch(struct drm_i915_gem_object *obj,
		       unsigned start, unsigned len)
{
	int i;
	void *addr = NULL;
	struct sg_page_iter sg_iter;
	int first_page = start >> PAGE_SHIFT;
	int last_page = (len + start + 4095) >> PAGE_SHIFT;
	int npages = last_page - first_page;
	struct page **pages;

	pages = drm_malloc_ab(npages, sizeof(*pages));
	if (pages == NULL) {
		DRM_DEBUG_DRIVER("Failed to get space for pages\n");
		goto finish;
	}

	i = 0;
	for_each_sg_page(obj->pages->sgl, &sg_iter, obj->pages->nents, first_page) {
		pages[i++] = sg_page_iter_page(&sg_iter);
		if (i == npages)
			break;
	}

	addr = vmap(pages, i, 0, PAGE_KERNEL);
	if (addr == NULL) {
		DRM_DEBUG_DRIVER("Failed to vmap pages\n");
		goto finish;
	}

finish:
	if (pages)
		drm_free_large(pages);
	return (u32*)addr;
}

/* Returns a vmap'd pointer to dest_obj, which the caller must unmap */
static u32 *copy_batch(struct drm_i915_gem_object *dest_obj,
		       struct drm_i915_gem_object *src_obj,
		       u32 batch_start_offset,
		       u32 batch_len)
{
	int needs_clflush = 0;
	void *src_base, *src;
	void *dst = NULL;
	int ret;

	if (batch_len > dest_obj->base.size ||
	    batch_len + batch_start_offset > src_obj->base.size)
		return ERR_PTR(-E2BIG);

	if (WARN_ON(dest_obj->pages_pin_count == 0))
		return ERR_PTR(-ENODEV);

	ret = i915_gem_obj_prepare_shmem_read(src_obj, &needs_clflush);
	if (ret) {
		DRM_DEBUG_DRIVER("CMD: failed to prepare shadow batch\n");
		return ERR_PTR(ret);
	}

	src_base = vmap_batch(src_obj, batch_start_offset, batch_len);
	if (!src_base) {
		DRM_DEBUG_DRIVER("CMD: Failed to vmap batch\n");
		ret = -ENOMEM;
		goto unpin_src;
	}

	ret = i915_gem_object_set_to_cpu_domain(dest_obj, true);
	if (ret) {
		DRM_DEBUG_DRIVER("CMD: Failed to set shadow batch to CPU\n");
		goto unmap_src;
	}

	dst = vmap_batch(dest_obj, 0, batch_len);
	if (!dst) {
		DRM_DEBUG_DRIVER("CMD: Failed to vmap shadow batch\n");
		ret = -ENOMEM;
		goto unmap_src;
	}

	src = src_base + offset_in_page(batch_start_offset);
	if (needs_clflush)
		drm_clflush_virt_range(src, batch_len);

	memcpy(dst, src, batch_len);

unmap_src:
	vunmap(src_base);
unpin_src:
	i915_gem_object_unpin_pages(src_obj);

	return ret ? ERR_PTR(ret) : dst;
}

/**
 * i915_needs_cmd_parser() - should a given ring use software command parsing?
 * @ring: the ring in question
 *
 * Only certain platforms require software batch buffer command parsing, and
 * only when enabled via module parameter.
 *
 * Return: true if the ring requires software command parsing
 */
bool i915_needs_cmd_parser(struct intel_engine_cs *engine)
{
	if (!engine->needs_cmd_parser)
		return false;

	if (!USES_PPGTT(engine->dev))
		return false;

	return (i915.enable_cmd_parser == 1);
}

static bool check_cmd(const struct intel_engine_cs *engine,
		      const struct drm_i915_cmd_descriptor *desc,
		      const u32 *cmd, u32 length,
		      const bool is_master,
		      bool *oacontrol_set)
{
	if (desc->flags & CMD_DESC_REJECT) {
		DRM_DEBUG_DRIVER("CMD: Rejected command: 0x%08X\n", *cmd);
		return false;
	}

	if ((desc->flags & CMD_DESC_MASTER) && !is_master) {
		DRM_DEBUG_DRIVER("CMD: Rejected master-only command: 0x%08X\n",
				 *cmd);
		return false;
	}

	if (desc->flags & CMD_DESC_REGISTER) {
		/*
		 * Get the distance between individual register offset
		 * fields if the command can perform more than one
		 * access at a time.
		 */
		const u32 step = desc->reg.step ? desc->reg.step : length;
		u32 offset;

		for (offset = desc->reg.offset; offset < length;
		     offset += step) {
			const u32 reg_addr = cmd[offset] & desc->reg.mask;
			const struct drm_i915_reg_descriptor *reg =
				find_reg_in_tables(engine->reg_tables,
						   engine->reg_table_count,
						   is_master,
						   reg_addr);

			if (!reg) {
				DRM_DEBUG_DRIVER("CMD: Rejected register 0x%08X in command: 0x%08X (ring=%d)\n",
						 reg_addr, *cmd, engine->id);
				return false;
			}

			/*
			 * OACONTROL requires some special handling for
			 * writes. We want to make sure that any batch which
			 * enables OA also disables it before the end of the
			 * batch. The goal is to prevent one process from
			 * snooping on the perf data from another process. To do
			 * that, we need to check the value that will be written
			 * to the register. Hence, limit OACONTROL writes to
			 * only MI_LOAD_REGISTER_IMM commands.
			 */
			if (reg_addr == i915_mmio_reg_offset(OACONTROL)) {
				if (desc->cmd.value == MI_LOAD_REGISTER_MEM) {
					DRM_DEBUG_DRIVER("CMD: Rejected LRM to OACONTROL\n");
					return false;
				}

				if (desc->cmd.value == MI_LOAD_REGISTER_IMM(1))
					*oacontrol_set = (cmd[offset + 1] != 0);
			}

			/*
			 * Check the value written to the register against the
			 * allowed mask/value pair given in the whitelist entry.
			 */
			if (reg->mask) {
				if (desc->cmd.value == MI_LOAD_REGISTER_MEM) {
					DRM_DEBUG_DRIVER("CMD: Rejected LRM to masked register 0x%08X\n",
							 reg_addr);
					return false;
				}

				if (desc->cmd.value == MI_LOAD_REGISTER_IMM(1) &&
				    (offset + 2 > length ||
				     (cmd[offset + 1] & reg->mask) != reg->value)) {
					DRM_DEBUG_DRIVER("CMD: Rejected LRI to masked register 0x%08X\n",
							 reg_addr);
					return false;
				}
			}
		}
	}

	if (desc->flags & CMD_DESC_BITMASK) {
		int i;

		for (i = 0; i < MAX_CMD_DESC_BITMASKS; i++) {
			u32 dword;

			if (desc->bits[i].mask == 0)
				break;

			if (desc->bits[i].condition_mask != 0) {
				u32 offset =
					desc->bits[i].condition_offset;
				u32 condition = cmd[offset] &
					desc->bits[i].condition_mask;

				if (condition == 0)
					continue;
			}

			dword = cmd[desc->bits[i].offset] &
				desc->bits[i].mask;

			if (dword != desc->bits[i].expected) {
				DRM_DEBUG_DRIVER("CMD: Rejected command 0x%08X for bitmask 0x%08X (exp=0x%08X act=0x%08X) (ring=%d)\n",
						 *cmd,
						 desc->bits[i].mask,
						 desc->bits[i].expected,
						 dword, engine->id);
				return false;
			}
		}
	}

	return true;
}

#define LENGTH_BIAS 2

/**
 * i915_parse_cmds() - parse a submitted batch buffer for privilege violations
 * @ring: the ring on which the batch is to execute
 * @batch_obj: the batch buffer in question
 * @shadow_batch_obj: copy of the batch buffer in question
 * @batch_start_offset: byte offset in the batch at which execution starts
 * @batch_len: length of the commands in batch_obj
 * @is_master: is the submitting process the drm master?
 *
 * Parses the specified batch buffer looking for privilege violations as
 * described in the overview.
 *
 * Return: non-zero if the parser finds violations or otherwise fails; -EACCES
 * if the batch appears legal but should use hardware parsing
 */
int i915_parse_cmds(struct intel_engine_cs *engine,
		    struct drm_i915_gem_object *batch_obj,
		    struct drm_i915_gem_object *shadow_batch_obj,
		    u32 batch_start_offset,
		    u32 batch_len,
		    bool is_master)
{
	u32 *cmd, *batch_base, *batch_end;
	struct drm_i915_cmd_descriptor default_desc = { 0 };
	bool oacontrol_set = false; /* OACONTROL tracking. See check_cmd() */
	int ret = 0;

	batch_base = copy_batch(shadow_batch_obj, batch_obj,
				batch_start_offset, batch_len);
	if (IS_ERR(batch_base)) {
		DRM_DEBUG_DRIVER("CMD: Failed to copy batch\n");
		return PTR_ERR(batch_base);
	}

	/*
	 * We use the batch length as size because the shadow object is as
	 * large or larger and copy_batch() will write MI_NOPs to the extra
	 * space. Parsing should be faster in some cases this way.
	 */
	batch_end = batch_base + (batch_len / sizeof(*batch_end));

	cmd = batch_base;
	while (cmd < batch_end) {
		const struct drm_i915_cmd_descriptor *desc;
		u32 length;

		if (*cmd == MI_BATCH_BUFFER_END)
			break;

		desc = find_cmd(engine, *cmd, &default_desc);
		if (!desc) {
			DRM_DEBUG_DRIVER("CMD: Unrecognized command: 0x%08X\n",
					 *cmd);
			ret = -EINVAL;
			break;
		}

		/*
		 * If the batch buffer contains a chained batch, return an
		 * error that tells the caller to abort and dispatch the
		 * workload as a non-secure batch.
		 */
		if (desc->cmd.value == MI_BATCH_BUFFER_START) {
			ret = -EACCES;
			break;
		}

		if (desc->flags & CMD_DESC_FIXED)
			length = desc->length.fixed;
		else
			length = ((*cmd & desc->length.mask) + LENGTH_BIAS);

		if ((batch_end - cmd) < length) {
			DRM_DEBUG_DRIVER("CMD: Command length exceeds batch length: 0x%08X length=%u batchlen=%td\n",
					 *cmd,
					 length,
					 batch_end - cmd);
			ret = -EINVAL;
			break;
		}

		if (!check_cmd(engine, desc, cmd, length, is_master,
			       &oacontrol_set)) {
			ret = -EINVAL;
			break;
		}

		cmd += length;
	}

	if (oacontrol_set) {
		DRM_DEBUG_DRIVER("CMD: batch set OACONTROL but did not clear it\n");
		ret = -EINVAL;
	}

	if (cmd >= batch_end) {
		DRM_DEBUG_DRIVER("CMD: Got to the end of the buffer w/o a BBE cmd!\n");
		ret = -EINVAL;
	}

	vunmap(batch_base);

	return ret;
}

/**
 * i915_cmd_parser_get_version() - get the cmd parser version number
 *
 * The cmd parser maintains a simple increasing integer version number suitable
 * for passing to userspace clients to determine what operations are permitted.
 *
 * Return: the current version number of the cmd parser
 */
int i915_cmd_parser_get_version(void)
{
	/*
	 * Command parser version history
	 *
	 * 1. Initial version. Checks batches and reports violations, but leaves
	 *    hardware parsing enabled (so does not allow new use cases).
	 * 2. Allow access to the MI_PREDICATE_SRC0 and
	 *    MI_PREDICATE_SRC1 registers.
	 * 3. Allow access to the GPGPU_THREADS_DISPATCHED register.
	 * 4. L3 atomic chicken bits of HSW_SCRATCH1 and HSW_ROW_CHICKEN3.
	 * 5. GPGPU dispatch compute indirect registers.
	 * 6. TIMESTAMP register and Haswell CS GPR registers
	 */
	return 6;
}
