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
 *    Kevin Tian <kevin.tian@intel.com>
 *    Zhi Wang <zhi.a.wang@intel.com>
 *
 * Contributors:
 *    Min he <min.he@intel.com>
 *
 */

#include "i915_drv.h"
#include "gvt.h"
#include "trace.h"

/* common offset among interrupt control registers */
#define regbase_to_isr(base)	(base)
#define regbase_to_imr(base)	(base + 0x4)
#define regbase_to_iir(base)	(base + 0x8)
#define regbase_to_ier(base)	(base + 0xC)

#define iir_to_regbase(iir)    (iir - 0x8)
#define ier_to_regbase(ier)    (ier - 0xC)

#define get_event_virt_handler(irq, e)	(irq->events[e].v_handler)
#define get_irq_info(irq, e)		(irq->events[e].info)

#define irq_to_gvt(irq) \
	container_of(irq, struct intel_gvt, irq)

static void update_upstream_irq(struct intel_vgpu *vgpu,
		struct intel_gvt_irq_info *info);

static const char * const irq_name[INTEL_GVT_EVENT_MAX] = {
	[RCS_MI_USER_INTERRUPT] = "Render CS MI USER INTERRUPT",
	[RCS_DEBUG] = "Render EU debug from SVG",
	[RCS_MMIO_SYNC_FLUSH] = "Render MMIO sync flush status",
	[RCS_CMD_STREAMER_ERR] = "Render CS error interrupt",
	[RCS_PIPE_CONTROL] = "Render PIPE CONTROL notify",
	[RCS_WATCHDOG_EXCEEDED] = "Render CS Watchdog counter exceeded",
	[RCS_PAGE_DIRECTORY_FAULT] = "Render page directory faults",
	[RCS_AS_CONTEXT_SWITCH] = "Render AS Context Switch Interrupt",

	[VCS_MI_USER_INTERRUPT] = "Video CS MI USER INTERRUPT",
	[VCS_MMIO_SYNC_FLUSH] = "Video MMIO sync flush status",
	[VCS_CMD_STREAMER_ERR] = "Video CS error interrupt",
	[VCS_MI_FLUSH_DW] = "Video MI FLUSH DW notify",
	[VCS_WATCHDOG_EXCEEDED] = "Video CS Watchdog counter exceeded",
	[VCS_PAGE_DIRECTORY_FAULT] = "Video page directory faults",
	[VCS_AS_CONTEXT_SWITCH] = "Video AS Context Switch Interrupt",
	[VCS2_MI_USER_INTERRUPT] = "VCS2 Video CS MI USER INTERRUPT",
	[VCS2_MI_FLUSH_DW] = "VCS2 Video MI FLUSH DW notify",
	[VCS2_AS_CONTEXT_SWITCH] = "VCS2 Context Switch Interrupt",

	[BCS_MI_USER_INTERRUPT] = "Blitter CS MI USER INTERRUPT",
	[BCS_MMIO_SYNC_FLUSH] = "Billter MMIO sync flush status",
	[BCS_CMD_STREAMER_ERR] = "Blitter CS error interrupt",
	[BCS_MI_FLUSH_DW] = "Blitter MI FLUSH DW notify",
	[BCS_PAGE_DIRECTORY_FAULT] = "Blitter page directory faults",
	[BCS_AS_CONTEXT_SWITCH] = "Blitter AS Context Switch Interrupt",

	[VECS_MI_FLUSH_DW] = "Video Enhanced Streamer MI FLUSH DW notify",
	[VECS_AS_CONTEXT_SWITCH] = "VECS Context Switch Interrupt",

	[PIPE_A_FIFO_UNDERRUN] = "Pipe A FIFO underrun",
	[PIPE_A_CRC_ERR] = "Pipe A CRC error",
	[PIPE_A_CRC_DONE] = "Pipe A CRC done",
	[PIPE_A_VSYNC] = "Pipe A vsync",
	[PIPE_A_LINE_COMPARE] = "Pipe A line compare",
	[PIPE_A_ODD_FIELD] = "Pipe A odd field",
	[PIPE_A_EVEN_FIELD] = "Pipe A even field",
	[PIPE_A_VBLANK] = "Pipe A vblank",
	[PIPE_B_FIFO_UNDERRUN] = "Pipe B FIFO underrun",
	[PIPE_B_CRC_ERR] = "Pipe B CRC error",
	[PIPE_B_CRC_DONE] = "Pipe B CRC done",
	[PIPE_B_VSYNC] = "Pipe B vsync",
	[PIPE_B_LINE_COMPARE] = "Pipe B line compare",
	[PIPE_B_ODD_FIELD] = "Pipe B odd field",
	[PIPE_B_EVEN_FIELD] = "Pipe B even field",
	[PIPE_B_VBLANK] = "Pipe B vblank",
	[PIPE_C_VBLANK] = "Pipe C vblank",
	[DPST_PHASE_IN] = "DPST phase in event",
	[DPST_HISTOGRAM] = "DPST histogram event",
	[GSE] = "GSE",
	[DP_A_HOTPLUG] = "DP A Hotplug",
	[AUX_CHANNEL_A] = "AUX Channel A",
	[PERF_COUNTER] = "Performance counter",
	[POISON] = "Poison",
	[GTT_FAULT] = "GTT fault",
	[PRIMARY_A_FLIP_DONE] = "Primary Plane A flip done",
	[PRIMARY_B_FLIP_DONE] = "Primary Plane B flip done",
	[PRIMARY_C_FLIP_DONE] = "Primary Plane C flip done",
	[SPRITE_A_FLIP_DONE] = "Sprite Plane A flip done",
	[SPRITE_B_FLIP_DONE] = "Sprite Plane B flip done",
	[SPRITE_C_FLIP_DONE] = "Sprite Plane C flip done",

	[PCU_THERMAL] = "PCU Thermal Event",
	[PCU_PCODE2DRIVER_MAILBOX] = "PCU pcode2driver mailbox event",

	[FDI_RX_INTERRUPTS_TRANSCODER_A] = "FDI RX Interrupts Combined A",
	[AUDIO_CP_CHANGE_TRANSCODER_A] = "Audio CP Change Transcoder A",
	[AUDIO_CP_REQUEST_TRANSCODER_A] = "Audio CP Request Transcoder A",
	[FDI_RX_INTERRUPTS_TRANSCODER_B] = "FDI RX Interrupts Combined B",
	[AUDIO_CP_CHANGE_TRANSCODER_B] = "Audio CP Change Transcoder B",
	[AUDIO_CP_REQUEST_TRANSCODER_B] = "Audio CP Request Transcoder B",
	[FDI_RX_INTERRUPTS_TRANSCODER_C] = "FDI RX Interrupts Combined C",
	[AUDIO_CP_CHANGE_TRANSCODER_C] = "Audio CP Change Transcoder C",
	[AUDIO_CP_REQUEST_TRANSCODER_C] = "Audio CP Request Transcoder C",
	[ERR_AND_DBG] = "South Error and Debug Interrupts Combined",
	[GMBUS] = "Gmbus",
	[SDVO_B_HOTPLUG] = "SDVO B hotplug",
	[CRT_HOTPLUG] = "CRT Hotplug",
	[DP_B_HOTPLUG] = "DisplayPort/HDMI/DVI B Hotplug",
	[DP_C_HOTPLUG] = "DisplayPort/HDMI/DVI C Hotplug",
	[DP_D_HOTPLUG] = "DisplayPort/HDMI/DVI D Hotplug",
	[AUX_CHANNEL_B] = "AUX Channel B",
	[AUX_CHANNEL_C] = "AUX Channel C",
	[AUX_CHANNEL_D] = "AUX Channel D",
	[AUDIO_POWER_STATE_CHANGE_B] = "Audio Power State change Port B",
	[AUDIO_POWER_STATE_CHANGE_C] = "Audio Power State change Port C",
	[AUDIO_POWER_STATE_CHANGE_D] = "Audio Power State change Port D",

	[INTEL_GVT_EVENT_RESERVED] = "RESERVED EVENTS!!!",
};

static inline struct intel_gvt_irq_info *regbase_to_irq_info(
		struct intel_gvt *gvt,
		unsigned int reg)
{
	struct intel_gvt_irq *irq = &gvt->irq;
	int i;

	for_each_set_bit(i, irq->irq_info_bitmap, INTEL_GVT_IRQ_INFO_MAX) {
		if (i915_mmio_reg_offset(irq->info[i]->reg_base) == reg)
			return irq->info[i];
	}

	return NULL;
}

/**
 * intel_vgpu_reg_imr_handler - Generic IMR register emulation write handler
 * @vgpu: a vGPU
 * @reg: register offset written by guest
 * @p_data: register data written by guest
 * @bytes: register data length
 *
 * This function is used to emulate the generic IMR register bit change
 * behavior.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_reg_imr_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_irq_ops *ops = gvt->irq.ops;
	u32 imr = *(u32 *)p_data;

	trace_write_ir(vgpu->id, "IMR", reg, imr, vgpu_vreg(vgpu, reg),
		       (vgpu_vreg(vgpu, reg) ^ imr));

	vgpu_vreg(vgpu, reg) = imr;

	ops->check_pending_irq(vgpu);

	return 0;
}

/**
 * intel_vgpu_reg_master_irq_handler - master IRQ write emulation handler
 * @vgpu: a vGPU
 * @reg: register offset written by guest
 * @p_data: register data written by guest
 * @bytes: register data length
 *
 * This function is used to emulate the master IRQ register on gen8+.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_reg_master_irq_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_irq_ops *ops = gvt->irq.ops;
	u32 ier = *(u32 *)p_data;
	u32 virtual_ier = vgpu_vreg(vgpu, reg);

	trace_write_ir(vgpu->id, "MASTER_IRQ", reg, ier, virtual_ier,
		       (virtual_ier ^ ier));

	/*
	 * GEN8_MASTER_IRQ is a special irq register,
	 * only bit 31 is allowed to be modified
	 * and treated as an IER bit.
	 */
	ier &= GEN8_MASTER_IRQ_CONTROL;
	virtual_ier &= GEN8_MASTER_IRQ_CONTROL;
	vgpu_vreg(vgpu, reg) &= ~GEN8_MASTER_IRQ_CONTROL;
	vgpu_vreg(vgpu, reg) |= ier;

	ops->check_pending_irq(vgpu);

	return 0;
}

/**
 * intel_vgpu_reg_ier_handler - Generic IER write emulation handler
 * @vgpu: a vGPU
 * @reg: register offset written by guest
 * @p_data: register data written by guest
 * @bytes: register data length
 *
 * This function is used to emulate the generic IER register behavior.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_reg_ier_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_irq_ops *ops = gvt->irq.ops;
	struct intel_gvt_irq_info *info;
	u32 ier = *(u32 *)p_data;

	trace_write_ir(vgpu->id, "IER", reg, ier, vgpu_vreg(vgpu, reg),
		       (vgpu_vreg(vgpu, reg) ^ ier));

	vgpu_vreg(vgpu, reg) = ier;

	info = regbase_to_irq_info(gvt, ier_to_regbase(reg));
	if (WARN_ON(!info))
		return -EINVAL;

	if (info->has_upstream_irq)
		update_upstream_irq(vgpu, info);

	ops->check_pending_irq(vgpu);

	return 0;
}

/**
 * intel_vgpu_reg_iir_handler - Generic IIR write emulation handler
 * @vgpu: a vGPU
 * @reg: register offset written by guest
 * @p_data: register data written by guest
 * @bytes: register data length
 *
 * This function is used to emulate the generic IIR register behavior.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 *
 */
int intel_vgpu_reg_iir_handler(struct intel_vgpu *vgpu, unsigned int reg,
	void *p_data, unsigned int bytes)
{
	struct intel_gvt_irq_info *info = regbase_to_irq_info(vgpu->gvt,
		iir_to_regbase(reg));
	u32 iir = *(u32 *)p_data;

	trace_write_ir(vgpu->id, "IIR", reg, iir, vgpu_vreg(vgpu, reg),
		       (vgpu_vreg(vgpu, reg) ^ iir));

	if (WARN_ON(!info))
		return -EINVAL;

	vgpu_vreg(vgpu, reg) &= ~iir;

	if (info->has_upstream_irq)
		update_upstream_irq(vgpu, info);
	return 0;
}

static struct intel_gvt_irq_map gen8_irq_map[] = {
	{ INTEL_GVT_IRQ_INFO_MASTER, 0, INTEL_GVT_IRQ_INFO_GT0, 0xffff },
	{ INTEL_GVT_IRQ_INFO_MASTER, 1, INTEL_GVT_IRQ_INFO_GT0, 0xffff0000 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 2, INTEL_GVT_IRQ_INFO_GT1, 0xffff },
	{ INTEL_GVT_IRQ_INFO_MASTER, 3, INTEL_GVT_IRQ_INFO_GT1, 0xffff0000 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 4, INTEL_GVT_IRQ_INFO_GT2, 0xffff },
	{ INTEL_GVT_IRQ_INFO_MASTER, 6, INTEL_GVT_IRQ_INFO_GT3, 0xffff },
	{ INTEL_GVT_IRQ_INFO_MASTER, 16, INTEL_GVT_IRQ_INFO_DE_PIPE_A, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 17, INTEL_GVT_IRQ_INFO_DE_PIPE_B, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 18, INTEL_GVT_IRQ_INFO_DE_PIPE_C, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 20, INTEL_GVT_IRQ_INFO_DE_PORT, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 22, INTEL_GVT_IRQ_INFO_DE_MISC, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 23, INTEL_GVT_IRQ_INFO_PCH, ~0 },
	{ INTEL_GVT_IRQ_INFO_MASTER, 30, INTEL_GVT_IRQ_INFO_PCU, ~0 },
	{ -1, -1, ~0 },
};

static void update_upstream_irq(struct intel_vgpu *vgpu,
		struct intel_gvt_irq_info *info)
{
	struct intel_gvt_irq *irq = &vgpu->gvt->irq;
	struct intel_gvt_irq_map *map = irq->irq_map;
	struct intel_gvt_irq_info *up_irq_info = NULL;
	u32 set_bits = 0;
	u32 clear_bits = 0;
	int bit;
	u32 val = vgpu_vreg(vgpu,
			regbase_to_iir(i915_mmio_reg_offset(info->reg_base)))
		& vgpu_vreg(vgpu,
			regbase_to_ier(i915_mmio_reg_offset(info->reg_base)));

	if (!info->has_upstream_irq)
		return;

	for (map = irq->irq_map; map->up_irq_bit != -1; map++) {
		if (info->group != map->down_irq_group)
			continue;

		if (!up_irq_info)
			up_irq_info = irq->info[map->up_irq_group];
		else
			WARN_ON(up_irq_info != irq->info[map->up_irq_group]);

		bit = map->up_irq_bit;

		if (val & map->down_irq_bitmask)
			set_bits |= (1 << bit);
		else
			clear_bits |= (1 << bit);
	}

	if (WARN_ON(!up_irq_info))
		return;

	if (up_irq_info->group == INTEL_GVT_IRQ_INFO_MASTER) {
		u32 isr = i915_mmio_reg_offset(up_irq_info->reg_base);

		vgpu_vreg(vgpu, isr) &= ~clear_bits;
		vgpu_vreg(vgpu, isr) |= set_bits;
	} else {
		u32 iir = regbase_to_iir(
			i915_mmio_reg_offset(up_irq_info->reg_base));
		u32 imr = regbase_to_imr(
			i915_mmio_reg_offset(up_irq_info->reg_base));

		vgpu_vreg(vgpu, iir) |= (set_bits & ~vgpu_vreg(vgpu, imr));
	}

	if (up_irq_info->has_upstream_irq)
		update_upstream_irq(vgpu, up_irq_info);
}

static void init_irq_map(struct intel_gvt_irq *irq)
{
	struct intel_gvt_irq_map *map;
	struct intel_gvt_irq_info *up_info, *down_info;
	int up_bit;

	for (map = irq->irq_map; map->up_irq_bit != -1; map++) {
		up_info = irq->info[map->up_irq_group];
		up_bit = map->up_irq_bit;
		down_info = irq->info[map->down_irq_group];

		set_bit(up_bit, up_info->downstream_irq_bitmap);
		down_info->has_upstream_irq = true;

		gvt_dbg_irq("[up] grp %d bit %d -> [down] grp %d bitmask %x\n",
			up_info->group, up_bit,
			down_info->group, map->down_irq_bitmask);
	}
}

/* =======================vEvent injection===================== */
static int inject_virtual_interrupt(struct intel_vgpu *vgpu)
{
	return intel_gvt_hypervisor_inject_msi(vgpu);
}

static void propagate_event(struct intel_gvt_irq *irq,
	enum intel_gvt_event_type event, struct intel_vgpu *vgpu)
{
	struct intel_gvt_irq_info *info;
	unsigned int reg_base;
	int bit;

	info = get_irq_info(irq, event);
	if (WARN_ON(!info))
		return;

	reg_base = i915_mmio_reg_offset(info->reg_base);
	bit = irq->events[event].bit;

	if (!test_bit(bit, (void *)&vgpu_vreg(vgpu,
					regbase_to_imr(reg_base)))) {
		trace_propagate_event(vgpu->id, irq_name[event], bit);
		set_bit(bit, (void *)&vgpu_vreg(vgpu,
					regbase_to_iir(reg_base)));
	}
}

/* =======================vEvent Handlers===================== */
static void handle_default_event_virt(struct intel_gvt_irq *irq,
	enum intel_gvt_event_type event, struct intel_vgpu *vgpu)
{
	if (!vgpu->irq.irq_warn_once[event]) {
		gvt_dbg_core("vgpu%d: IRQ receive event %d (%s)\n",
			vgpu->id, event, irq_name[event]);
		vgpu->irq.irq_warn_once[event] = true;
	}
	propagate_event(irq, event, vgpu);
}

/* =====================GEN specific logic======================= */
/* GEN8 interrupt routines. */

#define DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(regname, regbase) \
static struct intel_gvt_irq_info gen8_##regname##_info = { \
	.name = #regname"-IRQ", \
	.reg_base = (regbase), \
	.bit_to_event = {[0 ... INTEL_GVT_IRQ_BITWIDTH-1] = \
		INTEL_GVT_EVENT_RESERVED}, \
}

DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(gt0, GEN8_GT_ISR(0));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(gt1, GEN8_GT_ISR(1));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(gt2, GEN8_GT_ISR(2));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(gt3, GEN8_GT_ISR(3));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(de_pipe_a, GEN8_DE_PIPE_ISR(PIPE_A));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(de_pipe_b, GEN8_DE_PIPE_ISR(PIPE_B));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(de_pipe_c, GEN8_DE_PIPE_ISR(PIPE_C));
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(de_port, GEN8_DE_PORT_ISR);
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(de_misc, GEN8_DE_MISC_ISR);
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(pcu, GEN8_PCU_ISR);
DEFINE_GVT_GEN8_INTEL_GVT_IRQ_INFO(master, GEN8_MASTER_IRQ);

static struct intel_gvt_irq_info gvt_base_pch_info = {
	.name = "PCH-IRQ",
	.reg_base = SDEISR,
	.bit_to_event = {[0 ... INTEL_GVT_IRQ_BITWIDTH-1] =
		INTEL_GVT_EVENT_RESERVED},
};

static void gen8_check_pending_irq(struct intel_vgpu *vgpu)
{
	struct intel_gvt_irq *irq = &vgpu->gvt->irq;
	int i;

	if (!(vgpu_vreg(vgpu, i915_mmio_reg_offset(GEN8_MASTER_IRQ)) &
				GEN8_MASTER_IRQ_CONTROL))
		return;

	for_each_set_bit(i, irq->irq_info_bitmap, INTEL_GVT_IRQ_INFO_MAX) {
		struct intel_gvt_irq_info *info = irq->info[i];
		u32 reg_base;

		if (!info->has_upstream_irq)
			continue;

		reg_base = i915_mmio_reg_offset(info->reg_base);
		if ((vgpu_vreg(vgpu, regbase_to_iir(reg_base))
				& vgpu_vreg(vgpu, regbase_to_ier(reg_base))))
			update_upstream_irq(vgpu, info);
	}

	if (vgpu_vreg(vgpu, i915_mmio_reg_offset(GEN8_MASTER_IRQ))
			& ~GEN8_MASTER_IRQ_CONTROL)
		inject_virtual_interrupt(vgpu);
}

static void gen8_init_irq(
		struct intel_gvt_irq *irq)
{
	struct intel_gvt *gvt = irq_to_gvt(irq);

#define SET_BIT_INFO(s, b, e, i)		\
	do {					\
		s->events[e].bit = b;		\
		s->events[e].info = s->info[i];	\
		s->info[i]->bit_to_event[b] = e;\
	} while (0)

#define SET_IRQ_GROUP(s, g, i) \
	do { \
		s->info[g] = i; \
		(i)->group = g; \
		set_bit(g, s->irq_info_bitmap); \
	} while (0)

	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_MASTER, &gen8_master_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_GT0, &gen8_gt0_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_GT1, &gen8_gt1_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_GT2, &gen8_gt2_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_GT3, &gen8_gt3_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_DE_PIPE_A, &gen8_de_pipe_a_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_DE_PIPE_B, &gen8_de_pipe_b_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_DE_PIPE_C, &gen8_de_pipe_c_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_DE_PORT, &gen8_de_port_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_DE_MISC, &gen8_de_misc_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_PCU, &gen8_pcu_info);
	SET_IRQ_GROUP(irq, INTEL_GVT_IRQ_INFO_PCH, &gvt_base_pch_info);

	/* GEN8 level 2 interrupts. */

	/* GEN8 interrupt GT0 events */
	SET_BIT_INFO(irq, 0, RCS_MI_USER_INTERRUPT, INTEL_GVT_IRQ_INFO_GT0);
	SET_BIT_INFO(irq, 4, RCS_PIPE_CONTROL, INTEL_GVT_IRQ_INFO_GT0);
	SET_BIT_INFO(irq, 8, RCS_AS_CONTEXT_SWITCH, INTEL_GVT_IRQ_INFO_GT0);

	SET_BIT_INFO(irq, 16, BCS_MI_USER_INTERRUPT, INTEL_GVT_IRQ_INFO_GT0);
	SET_BIT_INFO(irq, 20, BCS_MI_FLUSH_DW, INTEL_GVT_IRQ_INFO_GT0);
	SET_BIT_INFO(irq, 24, BCS_AS_CONTEXT_SWITCH, INTEL_GVT_IRQ_INFO_GT0);

	/* GEN8 interrupt GT1 events */
	SET_BIT_INFO(irq, 0, VCS_MI_USER_INTERRUPT, INTEL_GVT_IRQ_INFO_GT1);
	SET_BIT_INFO(irq, 4, VCS_MI_FLUSH_DW, INTEL_GVT_IRQ_INFO_GT1);
	SET_BIT_INFO(irq, 8, VCS_AS_CONTEXT_SWITCH, INTEL_GVT_IRQ_INFO_GT1);

	if (HAS_BSD2(gvt->dev_priv)) {
		SET_BIT_INFO(irq, 16, VCS2_MI_USER_INTERRUPT,
			INTEL_GVT_IRQ_INFO_GT1);
		SET_BIT_INFO(irq, 20, VCS2_MI_FLUSH_DW,
			INTEL_GVT_IRQ_INFO_GT1);
		SET_BIT_INFO(irq, 24, VCS2_AS_CONTEXT_SWITCH,
			INTEL_GVT_IRQ_INFO_GT1);
	}

	/* GEN8 interrupt GT3 events */
	SET_BIT_INFO(irq, 0, VECS_MI_USER_INTERRUPT, INTEL_GVT_IRQ_INFO_GT3);
	SET_BIT_INFO(irq, 4, VECS_MI_FLUSH_DW, INTEL_GVT_IRQ_INFO_GT3);
	SET_BIT_INFO(irq, 8, VECS_AS_CONTEXT_SWITCH, INTEL_GVT_IRQ_INFO_GT3);

	SET_BIT_INFO(irq, 0, PIPE_A_VBLANK, INTEL_GVT_IRQ_INFO_DE_PIPE_A);
	SET_BIT_INFO(irq, 0, PIPE_B_VBLANK, INTEL_GVT_IRQ_INFO_DE_PIPE_B);
	SET_BIT_INFO(irq, 0, PIPE_C_VBLANK, INTEL_GVT_IRQ_INFO_DE_PIPE_C);

	/* GEN8 interrupt DE PORT events */
	SET_BIT_INFO(irq, 0, AUX_CHANNEL_A, INTEL_GVT_IRQ_INFO_DE_PORT);
	SET_BIT_INFO(irq, 3, DP_A_HOTPLUG, INTEL_GVT_IRQ_INFO_DE_PORT);

	/* GEN8 interrupt DE MISC events */
	SET_BIT_INFO(irq, 0, GSE, INTEL_GVT_IRQ_INFO_DE_MISC);

	/* PCH events */
	SET_BIT_INFO(irq, 17, GMBUS, INTEL_GVT_IRQ_INFO_PCH);
	SET_BIT_INFO(irq, 19, CRT_HOTPLUG, INTEL_GVT_IRQ_INFO_PCH);
	SET_BIT_INFO(irq, 21, DP_B_HOTPLUG, INTEL_GVT_IRQ_INFO_PCH);
	SET_BIT_INFO(irq, 22, DP_C_HOTPLUG, INTEL_GVT_IRQ_INFO_PCH);
	SET_BIT_INFO(irq, 23, DP_D_HOTPLUG, INTEL_GVT_IRQ_INFO_PCH);

	if (IS_BROADWELL(gvt->dev_priv)) {
		SET_BIT_INFO(irq, 25, AUX_CHANNEL_B, INTEL_GVT_IRQ_INFO_PCH);
		SET_BIT_INFO(irq, 26, AUX_CHANNEL_C, INTEL_GVT_IRQ_INFO_PCH);
		SET_BIT_INFO(irq, 27, AUX_CHANNEL_D, INTEL_GVT_IRQ_INFO_PCH);

		SET_BIT_INFO(irq, 4, PRIMARY_A_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_A);
		SET_BIT_INFO(irq, 5, SPRITE_A_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_A);

		SET_BIT_INFO(irq, 4, PRIMARY_B_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_B);
		SET_BIT_INFO(irq, 5, SPRITE_B_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_B);

		SET_BIT_INFO(irq, 4, PRIMARY_C_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_C);
		SET_BIT_INFO(irq, 5, SPRITE_C_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_C);
	} else if (IS_SKYLAKE(gvt->dev_priv)
			|| IS_KABYLAKE(gvt->dev_priv)
			|| IS_BROXTON(gvt->dev_priv)) {
		SET_BIT_INFO(irq, 25, AUX_CHANNEL_B, INTEL_GVT_IRQ_INFO_DE_PORT);
		SET_BIT_INFO(irq, 26, AUX_CHANNEL_C, INTEL_GVT_IRQ_INFO_DE_PORT);
		SET_BIT_INFO(irq, 27, AUX_CHANNEL_D, INTEL_GVT_IRQ_INFO_DE_PORT);

		SET_BIT_INFO(irq, 3, PRIMARY_A_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_A);
		SET_BIT_INFO(irq, 3, PRIMARY_B_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_B);
		SET_BIT_INFO(irq, 3, PRIMARY_C_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_C);

		SET_BIT_INFO(irq, 4, SPRITE_A_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_A);
		SET_BIT_INFO(irq, 4, SPRITE_B_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_B);
		SET_BIT_INFO(irq, 4, SPRITE_C_FLIP_DONE, INTEL_GVT_IRQ_INFO_DE_PIPE_C);
	}

	/* GEN8 interrupt PCU events */
	SET_BIT_INFO(irq, 24, PCU_THERMAL, INTEL_GVT_IRQ_INFO_PCU);
	SET_BIT_INFO(irq, 25, PCU_PCODE2DRIVER_MAILBOX, INTEL_GVT_IRQ_INFO_PCU);
}

static struct intel_gvt_irq_ops gen8_irq_ops = {
	.init_irq = gen8_init_irq,
	.check_pending_irq = gen8_check_pending_irq,
};

/**
 * intel_vgpu_trigger_virtual_event - Trigger a virtual event for a vGPU
 * @vgpu: a vGPU
 * @event: interrupt event
 *
 * This function is used to trigger a virtual interrupt event for vGPU.
 * The caller provides the event to be triggered, the framework itself
 * will emulate the IRQ register bit change.
 *
 */
void intel_vgpu_trigger_virtual_event(struct intel_vgpu *vgpu,
	enum intel_gvt_event_type event)
{
	struct intel_gvt *gvt = vgpu->gvt;
	struct intel_gvt_irq *irq = &gvt->irq;
	gvt_event_virt_handler_t handler;
	struct intel_gvt_irq_ops *ops = gvt->irq.ops;

	handler = get_event_virt_handler(irq, event);
	WARN_ON(!handler);

	handler(irq, event, vgpu);

	ops->check_pending_irq(vgpu);
}

static void init_events(
	struct intel_gvt_irq *irq)
{
	int i;

	for (i = 0; i < INTEL_GVT_EVENT_MAX; i++) {
		irq->events[i].info = NULL;
		irq->events[i].v_handler = handle_default_event_virt;
	}
}

static enum hrtimer_restart vblank_timer_fn(struct hrtimer *data)
{
	struct intel_gvt_vblank_timer *vblank_timer;
	struct intel_gvt_irq *irq;
	struct intel_gvt *gvt;

	vblank_timer = container_of(data, struct intel_gvt_vblank_timer, timer);
	irq = container_of(vblank_timer, struct intel_gvt_irq, vblank_timer);
	gvt = container_of(irq, struct intel_gvt, irq);

	intel_gvt_request_service(gvt, INTEL_GVT_REQUEST_EMULATE_VBLANK);
	hrtimer_add_expires_ns(&vblank_timer->timer, vblank_timer->period);
	return HRTIMER_RESTART;
}

/**
 * intel_gvt_clean_irq - clean up GVT-g IRQ emulation subsystem
 * @gvt: a GVT device
 *
 * This function is called at driver unloading stage, to clean up GVT-g IRQ
 * emulation subsystem.
 *
 */
void intel_gvt_clean_irq(struct intel_gvt *gvt)
{
	struct intel_gvt_irq *irq = &gvt->irq;

	hrtimer_cancel(&irq->vblank_timer.timer);
}

#define VBLNAK_TIMER_PERIOD 16000000

/**
 * intel_gvt_init_irq - initialize GVT-g IRQ emulation subsystem
 * @gvt: a GVT device
 *
 * This function is called at driver loading stage, to initialize the GVT-g IRQ
 * emulation subsystem.
 *
 * Returns:
 * Zero on success, negative error code if failed.
 */
int intel_gvt_init_irq(struct intel_gvt *gvt)
{
	struct intel_gvt_irq *irq = &gvt->irq;
	struct intel_gvt_vblank_timer *vblank_timer = &irq->vblank_timer;

	gvt_dbg_core("init irq framework\n");

	irq->ops = &gen8_irq_ops;
	irq->irq_map = gen8_irq_map;

	/* common event initialization */
	init_events(irq);

	/* gen specific initialization */
	irq->ops->init_irq(irq);

	init_irq_map(irq);

	hrtimer_init(&vblank_timer->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	vblank_timer->timer.function = vblank_timer_fn;
	vblank_timer->period = VBLNAK_TIMER_PERIOD;

	return 0;
}
