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

#ifndef _GVT_INTERRUPT_H_
#define _GVT_INTERRUPT_H_

enum intel_gvt_event_type {
	RCS_MI_USER_INTERRUPT = 0,
	RCS_DEBUG,
	RCS_MMIO_SYNC_FLUSH,
	RCS_CMD_STREAMER_ERR,
	RCS_PIPE_CONTROL,
	RCS_L3_PARITY_ERR,
	RCS_WATCHDOG_EXCEEDED,
	RCS_PAGE_DIRECTORY_FAULT,
	RCS_AS_CONTEXT_SWITCH,
	RCS_MONITOR_BUFF_HALF_FULL,

	VCS_MI_USER_INTERRUPT,
	VCS_MMIO_SYNC_FLUSH,
	VCS_CMD_STREAMER_ERR,
	VCS_MI_FLUSH_DW,
	VCS_WATCHDOG_EXCEEDED,
	VCS_PAGE_DIRECTORY_FAULT,
	VCS_AS_CONTEXT_SWITCH,

	VCS2_MI_USER_INTERRUPT,
	VCS2_MI_FLUSH_DW,
	VCS2_AS_CONTEXT_SWITCH,

	BCS_MI_USER_INTERRUPT,
	BCS_MMIO_SYNC_FLUSH,
	BCS_CMD_STREAMER_ERR,
	BCS_MI_FLUSH_DW,
	BCS_PAGE_DIRECTORY_FAULT,
	BCS_AS_CONTEXT_SWITCH,

	VECS_MI_USER_INTERRUPT,
	VECS_MI_FLUSH_DW,
	VECS_AS_CONTEXT_SWITCH,

	PIPE_A_FIFO_UNDERRUN,
	PIPE_B_FIFO_UNDERRUN,
	PIPE_A_CRC_ERR,
	PIPE_B_CRC_ERR,
	PIPE_A_CRC_DONE,
	PIPE_B_CRC_DONE,
	PIPE_A_ODD_FIELD,
	PIPE_B_ODD_FIELD,
	PIPE_A_EVEN_FIELD,
	PIPE_B_EVEN_FIELD,
	PIPE_A_LINE_COMPARE,
	PIPE_B_LINE_COMPARE,
	PIPE_C_LINE_COMPARE,
	PIPE_A_VBLANK,
	PIPE_B_VBLANK,
	PIPE_C_VBLANK,
	PIPE_A_VSYNC,
	PIPE_B_VSYNC,
	PIPE_C_VSYNC,
	PRIMARY_A_FLIP_DONE,
	PRIMARY_B_FLIP_DONE,
	PRIMARY_C_FLIP_DONE,
	SPRITE_A_FLIP_DONE,
	SPRITE_B_FLIP_DONE,
	SPRITE_C_FLIP_DONE,

	PCU_THERMAL,
	PCU_PCODE2DRIVER_MAILBOX,

	DPST_PHASE_IN,
	DPST_HISTOGRAM,
	GSE,
	DP_A_HOTPLUG,
	AUX_CHANNEL_A,
	PERF_COUNTER,
	POISON,
	GTT_FAULT,
	ERROR_INTERRUPT_COMBINED,

	FDI_RX_INTERRUPTS_TRANSCODER_A,
	AUDIO_CP_CHANGE_TRANSCODER_A,
	AUDIO_CP_REQUEST_TRANSCODER_A,
	FDI_RX_INTERRUPTS_TRANSCODER_B,
	AUDIO_CP_CHANGE_TRANSCODER_B,
	AUDIO_CP_REQUEST_TRANSCODER_B,
	FDI_RX_INTERRUPTS_TRANSCODER_C,
	AUDIO_CP_CHANGE_TRANSCODER_C,
	AUDIO_CP_REQUEST_TRANSCODER_C,
	ERR_AND_DBG,
	GMBUS,
	SDVO_B_HOTPLUG,
	CRT_HOTPLUG,
	DP_B_HOTPLUG,
	DP_C_HOTPLUG,
	DP_D_HOTPLUG,
	AUX_CHANNEL_B,
	AUX_CHANNEL_C,
	AUX_CHANNEL_D,
	AUDIO_POWER_STATE_CHANGE_B,
	AUDIO_POWER_STATE_CHANGE_C,
	AUDIO_POWER_STATE_CHANGE_D,

	INTEL_GVT_EVENT_RESERVED,
	INTEL_GVT_EVENT_MAX,
};

struct intel_gvt_irq;
struct intel_gvt;

typedef void (*gvt_event_virt_handler_t)(struct intel_gvt_irq *irq,
	enum intel_gvt_event_type event, struct intel_vgpu *vgpu);

struct intel_gvt_irq_ops {
	void (*init_irq)(struct intel_gvt_irq *irq);
	void (*check_pending_irq)(struct intel_vgpu *vgpu);
};

/* the list of physical interrupt control register groups */
enum intel_gvt_irq_type {
	INTEL_GVT_IRQ_INFO_GT,
	INTEL_GVT_IRQ_INFO_DPY,
	INTEL_GVT_IRQ_INFO_PCH,
	INTEL_GVT_IRQ_INFO_PM,

	INTEL_GVT_IRQ_INFO_MASTER,
	INTEL_GVT_IRQ_INFO_GT0,
	INTEL_GVT_IRQ_INFO_GT1,
	INTEL_GVT_IRQ_INFO_GT2,
	INTEL_GVT_IRQ_INFO_GT3,
	INTEL_GVT_IRQ_INFO_DE_PIPE_A,
	INTEL_GVT_IRQ_INFO_DE_PIPE_B,
	INTEL_GVT_IRQ_INFO_DE_PIPE_C,
	INTEL_GVT_IRQ_INFO_DE_PORT,
	INTEL_GVT_IRQ_INFO_DE_MISC,
	INTEL_GVT_IRQ_INFO_AUD,
	INTEL_GVT_IRQ_INFO_PCU,

	INTEL_GVT_IRQ_INFO_MAX,
};

#define INTEL_GVT_IRQ_BITWIDTH	32

/* device specific interrupt bit definitions */
struct intel_gvt_irq_info {
	char *name;
	i915_reg_t reg_base;
	enum intel_gvt_event_type bit_to_event[INTEL_GVT_IRQ_BITWIDTH];
	unsigned long warned;
	int group;
	DECLARE_BITMAP(downstream_irq_bitmap, INTEL_GVT_IRQ_BITWIDTH);
	bool has_upstream_irq;
};

/* per-event information */
struct intel_gvt_event_info {
	int bit;				/* map to register bit */
	int policy;				/* forwarding policy */
	struct intel_gvt_irq_info *info;	/* register info */
	gvt_event_virt_handler_t v_handler;	/* for v_event */
};

struct intel_gvt_irq_map {
	int up_irq_group;
	int up_irq_bit;
	int down_irq_group;
	u32 down_irq_bitmask;
};

struct intel_gvt_vblank_timer {
	struct hrtimer timer;
	u64 period;
};

/* structure containing device specific IRQ state */
struct intel_gvt_irq {
	struct intel_gvt_irq_ops *ops;
	struct intel_gvt_irq_info *info[INTEL_GVT_IRQ_INFO_MAX];
	DECLARE_BITMAP(irq_info_bitmap, INTEL_GVT_IRQ_INFO_MAX);
	struct intel_gvt_event_info events[INTEL_GVT_EVENT_MAX];
	DECLARE_BITMAP(pending_events, INTEL_GVT_EVENT_MAX);
	struct intel_gvt_irq_map *irq_map;
	struct intel_gvt_vblank_timer vblank_timer;
};

int intel_gvt_init_irq(struct intel_gvt *gvt);
void intel_gvt_clean_irq(struct intel_gvt *gvt);

void intel_vgpu_trigger_virtual_event(struct intel_vgpu *vgpu,
	enum intel_gvt_event_type event);

int intel_vgpu_reg_iir_handler(struct intel_vgpu *vgpu, unsigned int reg,
	void *p_data, unsigned int bytes);
int intel_vgpu_reg_ier_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes);
int intel_vgpu_reg_master_irq_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes);
int intel_vgpu_reg_imr_handler(struct intel_vgpu *vgpu,
	unsigned int reg, void *p_data, unsigned int bytes);

#endif /* _GVT_INTERRUPT_H_ */
