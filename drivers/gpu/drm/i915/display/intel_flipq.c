// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/pci.h>

#include <drm/drm_print.h>

#include "i915_utils.h"
#include "intel_step.h"
#include "intel_crtc.h"
#include "intel_de.h"
#include "intel_display_core.h"
#include "intel_display_types.h"
#include "intel_flipq.h"
#include "intel_dmc.h"
#include "intel_dmc_regs.h"
#include "intel_dsb.h"
#include "intel_vblank.h"
#include "intel_vrr.h"

/**
 * DOC: DMC Flip Queue
 *
 * A flip queue is a ring buffer implemented by the pipe DMC firmware.
 * The driver inserts entries into the queues to be executed by the
 * pipe DMC at a specified presentation timestamp (PTS).
 *
 * Each pipe DMC provides several queues:
 *
 * - 1 general queue (two DSB buffers executed per entry)
 * - 3 plane queues (one DSB buffer executed per entry)
 * - 1 fast queue (deprecated)
 */

#define for_each_flipq(flipq_id) \
	for ((flipq_id) = INTEL_FLIPQ_PLANE_1; (flipq_id) < MAX_INTEL_FLIPQ; (flipq_id)++)

static int intel_flipq_offset(enum intel_flipq_id flipq_id)
{
	switch (flipq_id) {
	case INTEL_FLIPQ_PLANE_1:
		return 0x008;
	case INTEL_FLIPQ_PLANE_2:
		return 0x108;
	case INTEL_FLIPQ_PLANE_3:
		return 0x208;
	case INTEL_FLIPQ_GENERAL:
		return 0x308;
	case INTEL_FLIPQ_FAST:
		return 0x3c8;
	default:
		MISSING_CASE(flipq_id);
		return 0;
	}
}

static int intel_flipq_size_dw(enum intel_flipq_id flipq_id)
{
	switch (flipq_id) {
	case INTEL_FLIPQ_PLANE_1:
	case INTEL_FLIPQ_PLANE_2:
	case INTEL_FLIPQ_PLANE_3:
		return 64;
	case INTEL_FLIPQ_GENERAL:
	case INTEL_FLIPQ_FAST:
		return 48;
	default:
		MISSING_CASE(flipq_id);
		return 1;
	}
}

static int intel_flipq_elem_size_dw(enum intel_flipq_id flipq_id)
{
	switch (flipq_id) {
	case INTEL_FLIPQ_PLANE_1:
	case INTEL_FLIPQ_PLANE_2:
	case INTEL_FLIPQ_PLANE_3:
		return 4;
	case INTEL_FLIPQ_GENERAL:
	case INTEL_FLIPQ_FAST:
		return 6;
	default:
		MISSING_CASE(flipq_id);
		return 1;
	}
}

static int intel_flipq_size_entries(enum intel_flipq_id flipq_id)
{
	return intel_flipq_size_dw(flipq_id) / intel_flipq_elem_size_dw(flipq_id);
}

static void intel_flipq_crtc_init(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);
	enum intel_flipq_id flipq_id;

	for_each_flipq(flipq_id) {
		struct intel_flipq *flipq = &crtc->flipq[flipq_id];

		flipq->start_mmioaddr = intel_pipedmc_start_mmioaddr(crtc) + intel_flipq_offset(flipq_id);
		flipq->flipq_id = flipq_id;

		drm_dbg_kms(display->drm, "[CRTC:%d:%s] FQ %d: start 0x%x\n",
			    crtc->base.base.id, crtc->base.name,
			    flipq_id, flipq->start_mmioaddr);
	}
}

bool intel_flipq_supported(struct intel_display *display)
{
	if (!display->params.enable_flipq)
		return false;

	if (!display->dmc.dmc)
		return false;

	if (DISPLAY_VER(display) == 20)
		return true;

	/* DMC firmware expects VRR timing generator to be used */
	return DISPLAY_VER(display) >= 30 && intel_vrr_always_use_vrr_tg(display);
}

void intel_flipq_init(struct intel_display *display)
{
	struct intel_crtc *crtc;

	intel_dmc_wait_fw_load(display);

	for_each_intel_crtc(display->drm, crtc)
		intel_flipq_crtc_init(crtc);
}

static int cdclk_factor(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 30)
		return 120;
	else
		return 280;
}

int intel_flipq_exec_time_us(struct intel_display *display)
{
	return intel_dsb_exec_time_us() +
		DIV_ROUND_UP(display->cdclk.hw.cdclk * cdclk_factor(display), 540000) +
		display->sagv.block_time_us;
}

static int intel_flipq_preempt_timeout_ms(struct intel_display *display)
{
	return DIV_ROUND_UP(intel_flipq_exec_time_us(display), 1000);
}

static void intel_flipq_preempt(struct intel_crtc *crtc, bool preempt)
{
	struct intel_display *display = to_intel_display(crtc);

	intel_de_rmw(display, PIPEDMC_FQ_CTRL(crtc->pipe),
		     PIPEDMC_FQ_CTRL_PREEMPT, preempt ? PIPEDMC_FQ_CTRL_PREEMPT : 0);

	if (preempt &&
	    intel_de_wait_for_clear(display,
				    PIPEDMC_FQ_STATUS(crtc->pipe),
				    PIPEDMC_FQ_STATUS_BUSY,
				    intel_flipq_preempt_timeout_ms(display)))
		drm_err(display->drm, "[CRTC:%d:%s] flip queue preempt timeout\n",
			crtc->base.base.id, crtc->base.name);
}

static int intel_flipq_current_head(struct intel_crtc *crtc, enum intel_flipq_id flipq_id)
{
	struct intel_display *display = to_intel_display(crtc);

	return intel_de_read(display, PIPEDMC_FPQ_CHP(crtc->pipe, flipq_id));
}

static void intel_flipq_write_tail(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	intel_de_write(display, PIPEDMC_FPQ_ATOMIC_TP(crtc->pipe),
		       PIPEDMC_FPQ_PLANEQ_3_TP(crtc->flipq[INTEL_FLIPQ_PLANE_3].tail) |
		       PIPEDMC_FPQ_PLANEQ_2_TP(crtc->flipq[INTEL_FLIPQ_PLANE_2].tail) |
		       PIPEDMC_FPQ_PLANEQ_1_TP(crtc->flipq[INTEL_FLIPQ_PLANE_1].tail) |
		       PIPEDMC_FPQ_FASTQ_TP(crtc->flipq[INTEL_FLIPQ_FAST].tail) |
		       PIPEDMC_FPQ_GENERALQ_TP(crtc->flipq[INTEL_FLIPQ_GENERAL].tail));
}

static void intel_flipq_sw_dmc_wake(struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	intel_de_write(display, PIPEDMC_FPQ_CTL1(crtc->pipe), PIPEDMC_SW_DMC_WAKE);
}

static int intel_flipq_exec_time_lines(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);

	return intel_usecs_to_scanlines(&crtc_state->hw.adjusted_mode,
					intel_flipq_exec_time_us(display));
}

void intel_flipq_dump(struct intel_crtc *crtc,
		      enum intel_flipq_id flipq_id)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_flipq *flipq = &crtc->flipq[flipq_id];
	u32 tmp;

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] FQ %d @ 0x%x: ",
		    crtc->base.base.id, crtc->base.name, flipq_id,
		    flipq->start_mmioaddr);
	for (int i = 0 ; i < intel_flipq_size_dw(flipq_id); i++) {
		printk(KERN_CONT " 0x%08x",
		       intel_de_read(display, PIPEDMC_FQ_RAM(flipq->start_mmioaddr, i)));
		if (i % intel_flipq_elem_size_dw(flipq_id) == intel_flipq_elem_size_dw(flipq_id) - 1)
			printk(KERN_CONT "\n");
	}

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] FQ %d: chp=0x%x, hp=0x%x\n",
		    crtc->base.base.id, crtc->base.name, flipq_id,
		    intel_de_read(display, PIPEDMC_FPQ_CHP(crtc->pipe, flipq_id)),
		    intel_de_read(display, PIPEDMC_FPQ_HP(crtc->pipe, flipq_id)));

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] FQ %d: current head %d\n",
		    crtc->base.base.id, crtc->base.name, flipq_id,
		    intel_flipq_current_head(crtc, flipq_id));

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] flip queue timestamp: 0x%x\n",
		    crtc->base.base.id, crtc->base.name,
		    intel_de_read(display, PIPEDMC_FPQ_TS(crtc->pipe)));

	tmp = intel_de_read(display, PIPEDMC_FPQ_ATOMIC_TP(crtc->pipe));

	drm_dbg_kms(display->drm,
		    "[CRTC:%d:%s] flip queue atomic tails: P3 %d, P2 %d, P1 %d, G %d, F %d\n",
		    crtc->base.base.id, crtc->base.name,
		    REG_FIELD_GET(PIPEDMC_FPQ_PLANEQ_3_TP_MASK, tmp),
		    REG_FIELD_GET(PIPEDMC_FPQ_PLANEQ_2_TP_MASK, tmp),
		    REG_FIELD_GET(PIPEDMC_FPQ_PLANEQ_1_TP_MASK, tmp),
		    REG_FIELD_GET(PIPEDMC_FPQ_GENERALQ_TP_MASK, tmp),
		    REG_FIELD_GET(PIPEDMC_FPQ_FASTQ_TP_MASK, tmp));
}

void intel_flipq_reset(struct intel_display *display, enum pipe pipe)
{
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);
	enum intel_flipq_id flipq_id;

	intel_de_write(display, PIPEDMC_FQ_CTRL(pipe), 0);

	intel_de_write(display, PIPEDMC_SCANLINECMPLOWER(pipe), 0);
	intel_de_write(display, PIPEDMC_SCANLINECMPUPPER(pipe), 0);

	for_each_flipq(flipq_id) {
		struct intel_flipq *flipq = &crtc->flipq[flipq_id];

		intel_de_write(display, PIPEDMC_FPQ_HP(pipe, flipq_id), 0);
		intel_de_write(display, PIPEDMC_FPQ_CHP(pipe, flipq_id), 0);

		flipq->tail = 0;
	}

	intel_de_write(display, PIPEDMC_FPQ_ATOMIC_TP(pipe), 0);
}

static enum pipedmc_event_id flipq_event_id(struct intel_display *display)
{
	if (DISPLAY_VER(display) >= 30)
		return PIPEDMC_EVENT_FULL_FQ_WAKE_TRIGGER;
	else
		return PIPEDMC_EVENT_SCANLINE_INRANGE_FQ_TRIGGER;
}

void intel_flipq_enable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	/* FIXME what to do with VRR? */
	int scanline = intel_mode_vblank_start(&crtc_state->hw.adjusted_mode) -
		intel_flipq_exec_time_lines(crtc_state);

	if (DISPLAY_VER(display) >= 30) {
		u32 start_mmioaddr = intel_pipedmc_start_mmioaddr(crtc);

		/* undocumented magic DMC variables */
		intel_de_write(display, PTL_PIPEDMC_EXEC_TIME_LINES(start_mmioaddr),
			       intel_flipq_exec_time_lines(crtc_state));
		intel_de_write(display, PTL_PIPEDMC_END_OF_EXEC_GB(start_mmioaddr),
			       100);
	}

	intel_de_write(display, PIPEDMC_SCANLINECMPUPPER(crtc->pipe),
		       PIPEDMC_SCANLINE_UPPER(scanline));
	intel_de_write(display, PIPEDMC_SCANLINECMPLOWER(crtc->pipe),
		       PIPEDMC_SCANLINEINRANGECMP_EN |
		       PIPEDMC_SCANLINE_LOWER(scanline - 2));

	intel_pipedmc_enable_event(crtc, flipq_event_id(display));

	intel_de_write(display, PIPEDMC_FQ_CTRL(crtc->pipe), PIPEDMC_FQ_CTRL_ENABLE);
}

void intel_flipq_disable(const struct intel_crtc_state *crtc_state)
{
	struct intel_display *display = to_intel_display(crtc_state);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	intel_flipq_preempt(crtc, true);

	intel_de_write(display, PIPEDMC_FQ_CTRL(crtc->pipe), 0);

	intel_pipedmc_disable_event(crtc, flipq_event_id(display));

	intel_de_write(display, PIPEDMC_SCANLINECMPLOWER(crtc->pipe), 0);
	intel_de_write(display, PIPEDMC_SCANLINECMPUPPER(crtc->pipe), 0);
}

static bool assert_flipq_has_room(struct intel_crtc *crtc,
				  enum intel_flipq_id flipq_id)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_flipq *flipq = &crtc->flipq[flipq_id];
	int head, size = intel_flipq_size_entries(flipq_id);

	head = intel_flipq_current_head(crtc, flipq_id);

	return !drm_WARN(display->drm,
			 (flipq->tail + size - head) % size >= size - 1,
			 "[CRTC:%d:%s] FQ %d overflow (head %d, tail %d, size %d)\n",
			 crtc->base.base.id, crtc->base.name, flipq_id,
			 head, flipq->tail, size);
}

static void intel_flipq_write(struct intel_display *display,
			      struct intel_flipq *flipq, u32 data, int i)
{
	intel_de_write(display, PIPEDMC_FQ_RAM(flipq->start_mmioaddr, flipq->tail *
					       intel_flipq_elem_size_dw(flipq->flipq_id) + i), data);
}

static void lnl_flipq_add(struct intel_display *display,
			  struct intel_flipq *flipq,
			  unsigned int pts,
			  enum intel_dsb_id dsb_id,
			  struct intel_dsb *dsb)
{
	int i = 0;

	switch (flipq->flipq_id) {
	case INTEL_FLIPQ_GENERAL:
		intel_flipq_write(display, flipq, pts, i++);
		intel_flipq_write(display, flipq, intel_dsb_head(dsb), i++);
		intel_flipq_write(display, flipq, LNL_FQ_INTERRUPT |
				  LNL_FQ_DSB_ID(dsb_id) |
				  LNL_FQ_DSB_SIZE(intel_dsb_size(dsb) / 64), i++);
		intel_flipq_write(display, flipq, 0, i++);
		intel_flipq_write(display, flipq, 0, i++); /* head for second DSB */
		intel_flipq_write(display, flipq, 0, i++); /* DSB engine + size for second DSB */
		break;
	case INTEL_FLIPQ_PLANE_1:
	case INTEL_FLIPQ_PLANE_2:
	case INTEL_FLIPQ_PLANE_3:
		intel_flipq_write(display, flipq, pts, i++);
		intel_flipq_write(display, flipq, intel_dsb_head(dsb), i++);
		intel_flipq_write(display, flipq, LNL_FQ_INTERRUPT |
				  LNL_FQ_DSB_ID(dsb_id) |
				  LNL_FQ_DSB_SIZE(intel_dsb_size(dsb) / 64), i++);
		intel_flipq_write(display, flipq, 0, i++);
		break;
	default:
		MISSING_CASE(flipq->flipq_id);
		return;
	}
}

static void ptl_flipq_add(struct intel_display *display,
			  struct intel_flipq *flipq,
			  unsigned int pts,
			  enum intel_dsb_id dsb_id,
			  struct intel_dsb *dsb)
{
	int i = 0;

	switch (flipq->flipq_id) {
	case INTEL_FLIPQ_GENERAL:
		intel_flipq_write(display, flipq, pts, i++);
		intel_flipq_write(display, flipq, 0, i++);
		intel_flipq_write(display, flipq, PTL_FQ_INTERRUPT |
				  PTL_FQ_DSB_ID(dsb_id) |
				  PTL_FQ_DSB_SIZE(intel_dsb_size(dsb) / 64), i++);
		intel_flipq_write(display, flipq, intel_dsb_head(dsb), i++);
		intel_flipq_write(display, flipq, 0, i++); /* DSB engine + size for second DSB */
		intel_flipq_write(display, flipq, 0, i++); /* head for second DSB */
		break;
	case INTEL_FLIPQ_PLANE_1:
	case INTEL_FLIPQ_PLANE_2:
	case INTEL_FLIPQ_PLANE_3:
		intel_flipq_write(display, flipq, pts, i++);
		intel_flipq_write(display, flipq, 0, i++);
		intel_flipq_write(display, flipq, PTL_FQ_INTERRUPT |
				  PTL_FQ_DSB_ID(dsb_id) |
				  PTL_FQ_DSB_SIZE(intel_dsb_size(dsb) / 64), i++);
		intel_flipq_write(display, flipq, intel_dsb_head(dsb), i++);
		break;
	default:
		MISSING_CASE(flipq->flipq_id);
		return;
	}
}

void intel_flipq_add(struct intel_crtc *crtc,
		     enum intel_flipq_id flipq_id,
		     unsigned int pts,
		     enum intel_dsb_id dsb_id,
		     struct intel_dsb *dsb)
{
	struct intel_display *display = to_intel_display(crtc);
	struct intel_flipq *flipq = &crtc->flipq[flipq_id];

	if (!assert_flipq_has_room(crtc, flipq_id))
		return;

	pts += intel_de_read(display, PIPEDMC_FPQ_TS(crtc->pipe));

	intel_flipq_preempt(crtc, true);

	if (DISPLAY_VER(display) >= 30)
		ptl_flipq_add(display, flipq,  pts, dsb_id, dsb);
	else
		lnl_flipq_add(display, flipq,  pts, dsb_id, dsb);

	flipq->tail = (flipq->tail + 1) % intel_flipq_size_entries(flipq->flipq_id);
	intel_flipq_write_tail(crtc);

	intel_flipq_preempt(crtc, false);

	intel_flipq_sw_dmc_wake(crtc);
}

/* Wa_18034343758 */
static bool need_dmc_halt_wa(struct intel_display *display)
{
	return DISPLAY_VER(display) == 20 ||
		(display->platform.pantherlake &&
		 IS_DISPLAY_STEP(display, STEP_A0, STEP_B0));
}

void intel_flipq_wait_dmc_halt(struct intel_dsb *dsb, struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	if (need_dmc_halt_wa(display))
		intel_dsb_wait_usec(dsb, 2);
}

void intel_flipq_unhalt_dmc(struct intel_dsb *dsb, struct intel_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc);

	if (need_dmc_halt_wa(display))
		intel_dsb_reg_write(dsb, PIPEDMC_CTL(crtc->pipe), 0);
}
