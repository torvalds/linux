/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __TIDSS_IRQ_H__
#define __TIDSS_IRQ_H__

#include <linux/types.h>

#include "tidss_drv.h"

/*
 * The IRQ status from various DISPC IRQ registers are packed into a single
 * value, where the bits are defined as follows:
 *
 * bit group |dev|wb |mrg0|mrg1|mrg2|mrg3|plane0-3| <unused> |
 * bit use   |D  |fou|FEOL|FEOL|FEOL|FEOL|  UUUU  |          |
 * bit number|0  |1-3|4-7 |8-11|  12-19  | 20-23  |  24-31   |
 *
 * device bits:	D = Unused
 * WB bits:	f = frame done wb, o = wb buffer overflow,
 *		u = wb buffer uncomplete
 * vp bits:	F = frame done, E = vsync even, O = vsync odd, L = sync lost
 * plane bits:	U = fifo underflow
 */

#define DSS_IRQ_DEVICE_FRAMEDONEWB		BIT(1)
#define DSS_IRQ_DEVICE_WBBUFFEROVERFLOW		BIT(2)
#define DSS_IRQ_DEVICE_WBUNCOMPLETEERROR	BIT(3)
#define DSS_IRQ_DEVICE_WB_MASK			GENMASK(3, 1)

#define DSS_IRQ_VP_BIT_N(ch, bit)	(4 + 4 * (ch) + (bit))
#define DSS_IRQ_PLANE_BIT_N(plane, bit) \
	(DSS_IRQ_VP_BIT_N(TIDSS_MAX_PORTS, 0) + 1 * (plane) + (bit))

#define DSS_IRQ_VP_BIT(ch, bit)	BIT(DSS_IRQ_VP_BIT_N((ch), (bit)))
#define DSS_IRQ_PLANE_BIT(plane, bit) \
	BIT(DSS_IRQ_PLANE_BIT_N((plane), (bit)))

static inline dispc_irq_t DSS_IRQ_VP_MASK(u32 ch)
{
	return GENMASK(DSS_IRQ_VP_BIT_N((ch), 3), DSS_IRQ_VP_BIT_N((ch), 0));
}

static inline dispc_irq_t DSS_IRQ_PLANE_MASK(u32 plane)
{
	return GENMASK(DSS_IRQ_PLANE_BIT_N((plane), 0),
		       DSS_IRQ_PLANE_BIT_N((plane), 0));
}

#define DSS_IRQ_VP_FRAME_DONE(ch)	DSS_IRQ_VP_BIT((ch), 0)
#define DSS_IRQ_VP_VSYNC_EVEN(ch)	DSS_IRQ_VP_BIT((ch), 1)
#define DSS_IRQ_VP_VSYNC_ODD(ch)	DSS_IRQ_VP_BIT((ch), 2)
#define DSS_IRQ_VP_SYNC_LOST(ch)	DSS_IRQ_VP_BIT((ch), 3)

#define DSS_IRQ_PLANE_FIFO_UNDERFLOW(plane)	DSS_IRQ_PLANE_BIT((plane), 0)

struct drm_crtc;
struct drm_device;

struct tidss_device;

void tidss_irq_enable_vblank(struct drm_crtc *crtc);
void tidss_irq_disable_vblank(struct drm_crtc *crtc);

int tidss_irq_install(struct drm_device *ddev, unsigned int irq);
void tidss_irq_uninstall(struct drm_device *ddev);

void tidss_irq_resume(struct tidss_device *tidss);

#endif
