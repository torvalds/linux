/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef __DC_KMS_H__
#define __DC_KMS_H__

#include <linux/completion.h>

#include <drm/drm_crtc.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>

#include "dc-de.h"
#include "dc-fu.h"
#include "dc-pe.h"

#define DC_CRTC_IRQS	5

struct dc_crtc_irq {
	struct dc_crtc *dc_crtc;
	unsigned int irq;
};

/**
 * struct dc_crtc - DC specific drm_crtc
 *
 * Each display controller contains one content stream and one safety stream.
 * In general, the two streams have the same functionality. One stream is
 * overlaid on the other by @fg. This driver chooses to generate black constant
 * color from the content stream as background color, build plane(s) on the
 * content stream by using layerblend(s) and always generate a constant color
 * from the safety stream. Note that due to the decoupled timing, the safety
 * stream still works to show the constant color properly even when the content
 * stream has completely hung up due to mal-function of this driver.
 */
struct dc_crtc {
	/** @base: base drm_crtc structure */
	struct drm_crtc base;
	/** @de: display engine */
	struct dc_de *de;
	/** @cf_cont: content stream constframe */
	struct dc_cf *cf_cont;
	/** @cf_safe: safety stream constframe */
	struct dc_cf *cf_safe;
	/** @ed_cont: content stream extdst */
	struct dc_ed *ed_cont;
	/** @ed_safe: safety stream extdst */
	struct dc_ed *ed_safe;
	/** @fg: framegen */
	struct dc_fg *fg;
	/**
	 * @irq_dec_framecomplete:
	 *
	 * display engine configuration frame complete interrupt
	 */
	unsigned int irq_dec_framecomplete;
	/**
	 * @irq_dec_seqcomplete:
	 *
	 * display engine configuration sequence complete interrupt
	 */
	unsigned int irq_dec_seqcomplete;
	/**
	 * @irq_dec_shdload:
	 *
	 * display engine configuration shadow load interrupt
	 */
	unsigned int irq_dec_shdload;
	/**
	 * @irq_ed_cont_shdload:
	 *
	 * content stream extdst shadow load interrupt
	 */
	unsigned int irq_ed_cont_shdload;
	/**
	 * @irq_ed_safe_shdload:
	 *
	 * safety stream extdst shadow load interrupt
	 */
	unsigned int irq_ed_safe_shdload;
	/**
	 * @dec_seqcomplete_done:
	 *
	 * display engine configuration sequence completion
	 */
	struct completion dec_seqcomplete_done;
	/**
	 * @dec_shdload_done:
	 *
	 * display engine configuration shadow load completion
	 */
	struct completion dec_shdload_done;
	/**
	 * @ed_cont_shdload_done:
	 *
	 * content stream extdst shadow load completion
	 */
	struct completion ed_cont_shdload_done;
	/**
	 * @ed_safe_shdload_done:
	 *
	 * safety stream extdst shadow load completion
	 */
	struct completion ed_safe_shdload_done;
	/** @event: cached pending vblank event */
	struct drm_pending_vblank_event *event;
	/** @irqs: interrupt list */
	struct dc_crtc_irq irqs[DC_CRTC_IRQS];
};

/**
 * struct dc_plane - DC specific drm_plane
 *
 * Build a plane on content stream with a fetchunit and a layerblend.
 */
struct dc_plane {
	/** @base: base drm_plane structure */
	struct drm_plane base;
	/** @fu: fetchunit */
	struct dc_fu *fu;
	/** @cf: content stream constframe */
	struct dc_cf *cf;
	/** @lb: layerblend */
	struct dc_lb *lb;
	/** @ed: content stream extdst */
	struct dc_ed *ed;
};

#endif /* __DC_KMS_H__ */
