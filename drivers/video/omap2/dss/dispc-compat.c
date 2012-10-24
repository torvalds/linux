/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "APPLY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"
#include "dispc-compat.h"

static void dispc_mgr_disable_isr(void *data, u32 mask)
{
	struct completion *compl = data;
	complete(compl);
}

static void dispc_mgr_enable_lcd_out(enum omap_channel channel)
{
	dispc_mgr_enable(channel, true);
}

static void dispc_mgr_disable_lcd_out(enum omap_channel channel)
{
	DECLARE_COMPLETION_ONSTACK(framedone_compl);
	int r;
	u32 irq;

	if (dispc_mgr_is_enabled(channel) == false)
		return;

	/*
	 * When we disable LCD output, we need to wait for FRAMEDONE to know
	 * that DISPC has finished with the LCD output.
	 */

	irq = dispc_mgr_get_framedone_irq(channel);

	r = omap_dispc_register_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq);
	if (r)
		DSSERR("failed to register FRAMEDONE isr\n");

	dispc_mgr_enable(channel, false);

	/* if we couldn't register for framedone, just sleep and exit */
	if (r) {
		msleep(100);
		return;
	}

	if (!wait_for_completion_timeout(&framedone_compl,
				msecs_to_jiffies(100)))
		DSSERR("timeout waiting for FRAME DONE\n");

	r = omap_dispc_unregister_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq);
	if (r)
		DSSERR("failed to unregister FRAMEDONE isr\n");
}

static void dispc_digit_out_enable_isr(void *data, u32 mask)
{
	struct completion *compl = data;

	/* ignore any sync lost interrupts */
	if (mask & (DISPC_IRQ_EVSYNC_EVEN | DISPC_IRQ_EVSYNC_ODD))
		complete(compl);
}

static void dispc_mgr_enable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(vsync_compl);
	int r;
	u32 irq_mask;

	if (dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT) == true)
		return;

	/*
	 * Digit output produces some sync lost interrupts during the first
	 * frame when enabling. Those need to be ignored, so we register for the
	 * sync lost irq to prevent the error handler from triggering.
	 */

	irq_mask = dispc_mgr_get_vsync_irq(OMAP_DSS_CHANNEL_DIGIT) |
		dispc_mgr_get_sync_lost_irq(OMAP_DSS_CHANNEL_DIGIT);

	r = omap_dispc_register_isr(dispc_digit_out_enable_isr, &vsync_compl,
			irq_mask);
	if (r) {
		DSSERR("failed to register %x isr\n", irq_mask);
		return;
	}

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, true);

	/* wait for the first evsync */
	if (!wait_for_completion_timeout(&vsync_compl, msecs_to_jiffies(100)))
		DSSERR("timeout waiting for digit out to start\n");

	r = omap_dispc_unregister_isr(dispc_digit_out_enable_isr, &vsync_compl,
			irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

static void dispc_mgr_disable_digit_out(void)
{
	DECLARE_COMPLETION_ONSTACK(framedone_compl);
	int r, i;
	u32 irq_mask;
	int num_irqs;

	if (dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT) == false)
		return;

	/*
	 * When we disable the digit output, we need to wait for FRAMEDONE to
	 * know that DISPC has finished with the output.
	 */

	irq_mask = dispc_mgr_get_framedone_irq(OMAP_DSS_CHANNEL_DIGIT);
	num_irqs = 1;

	if (!irq_mask) {
		/*
		 * omap 2/3 don't have framedone irq for TV, so we need to use
		 * vsyncs for this.
		 */

		irq_mask = dispc_mgr_get_vsync_irq(OMAP_DSS_CHANNEL_DIGIT);
		/*
		 * We need to wait for both even and odd vsyncs. Note that this
		 * is not totally reliable, as we could get a vsync interrupt
		 * before we disable the output, which leads to timeout in the
		 * wait_for_completion.
		 */
		num_irqs = 2;
	}

	r = omap_dispc_register_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq_mask);
	if (r)
		DSSERR("failed to register %x isr\n", irq_mask);

	dispc_mgr_enable(OMAP_DSS_CHANNEL_DIGIT, false);

	/* if we couldn't register the irq, just sleep and exit */
	if (r) {
		msleep(100);
		return;
	}

	for (i = 0; i < num_irqs; ++i) {
		if (!wait_for_completion_timeout(&framedone_compl,
					msecs_to_jiffies(100)))
			DSSERR("timeout waiting for digit out to stop\n");
	}

	r = omap_dispc_unregister_isr(dispc_mgr_disable_isr, &framedone_compl,
			irq_mask);
	if (r)
		DSSERR("failed to unregister %x isr\n", irq_mask);
}

void dispc_mgr_enable_sync(enum omap_channel channel)
{
	if (dss_mgr_is_lcd(channel))
		dispc_mgr_enable_lcd_out(channel);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		dispc_mgr_enable_digit_out();
	else
		WARN_ON(1);
}

void dispc_mgr_disable_sync(enum omap_channel channel)
{
	if (dss_mgr_is_lcd(channel))
		dispc_mgr_disable_lcd_out(channel);
	else if (channel == OMAP_DSS_CHANNEL_DIGIT)
		dispc_mgr_disable_digit_out();
	else
		WARN_ON(1);
}

