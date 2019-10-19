/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#ifndef __OMAP2_DSS_DISPC_COMPAT_H
#define __OMAP2_DSS_DISPC_COMPAT_H

void dispc_mgr_enable_sync(enum omap_channel channel);
void dispc_mgr_disable_sync(enum omap_channel channel);

int omap_dispc_wait_for_irq_interruptible_timeout(u32 irqmask,
		unsigned long timeout);

int dss_dispc_initialize_irq(void);
void dss_dispc_uninitialize_irq(void);

#endif
