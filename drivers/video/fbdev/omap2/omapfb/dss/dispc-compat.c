// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 */

#define DSS_SUBSYS_NAME "APPLY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>

#include <video/omapfb_dss.h>

#include "dss.h"
#include "dss_features.h"
#include "dispc-compat.h"

#define DISPC_IRQ_MASK_ERROR            (DISPC_IRQ_GFX_FIFO_UNDERFLOW | \
					 DISPC_IRQ_OCP_ERR | \
					 DISPC_IRQ_VID1_FIFO_UNDERFLOW | \
					 DISPC_IRQ_VID2_FIFO_UNDERFLOW | \
					 DISPC_IRQ_SYNC_LOST | \
					 DISPC_IRQ_SYNC_LOST_DIGIT)

#define DISPC_MAX_NR_ISRS		8

struct omap_dispc_isr_data {
	omap_dispc_isr_t	isr;
	void			*arg;
	u32			mask;
};

struct dispc_irq_stats {
	unsigned long last_reset;
	unsigned irq_count;
	unsigned irqs[32];
};

static struct {
	spinlock_t irq_lock;
	u32 irq_error_mask;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];
	u32 error_irqs;
	struct work_struct error_work;

#ifdef CONFIG_FB_OMAP2_DSS_COLLECT_IRQ_STATS
	spinlock_t irq_stats_lock;
	struct dispc_irq_stats irq_stats;
#endif
} dispc_compat;


#ifdef CONFIG_FB_OMAP2_DSS_COLLECT_IRQ_STATS
static void dispc_dump_irqs(struct seq_file *s)
{
	unsigned long flags;
	struct dispc_irq_stats stats;

	spin_lock_irqsave(&dispc_compat.irq_stats_lock, flags);

	stats = dispc_compat.irq_stats;
	memset(&dispc_compat.irq_stats, 0, sizeof(dispc_compat.irq_stats));
	dispc_compat.irq_stats.last_reset = jiffies;

	spin_unlock_irqrestore(&dispc_compat.irq_stats_lock, flags);

	seq_printf(s, "period %u ms\n",
			jiffies_to_msecs(jiffies - stats.last_reset));

	seq_printf(s, "irqs %d\n", stats.irq_count);
#define PIS(x) \
	seq_printf(s, "%-20s %10d\n", #x, stats.irqs[ffs(DISPC_IRQ_##x)-1])

	PIS(FRAMEDONE);
	PIS(VSYNC);
	PIS(EVSYNC_EVEN);
	PIS(EVSYNC_ODD);
	PIS(ACBIAS_COUNT_STAT);
	PIS(PROG_LINE_NUM);
	PIS(GFX_FIFO_UNDERFLOW);
	PIS(GFX_END_WIN);
	PIS(PAL_GAMMA_MASK);
	PIS(OCP_ERR);
	PIS(VID1_FIFO_UNDERFLOW);
	PIS(VID1_END_WIN);
	PIS(VID2_FIFO_UNDERFLOW);
	PIS(VID2_END_WIN);
	if (dss_feat_get_num_ovls() > 3) {
		PIS(VID3_FIFO_UNDERFLOW);
		PIS(VID3_END_WIN);
	}
	PIS(SYNC_LOST);
	PIS(SYNC_LOST_DIGIT);
	PIS(WAKEUP);
	if (dss_has_feature(FEAT_MGR_LCD2)) {
		PIS(FRAMEDONE2);
		PIS(VSYNC2);
		PIS(ACBIAS_COUNT_STAT2);
		PIS(SYNC_LOST2);
	}
	if (dss_has_feature(FEAT_MGR_LCD3)) {
		PIS(FRAMEDONE3);
		PIS(VSYNC3);
		PIS(ACBIAS_COUNT_STAT3);
		PIS(SYNC_LOST3);
	}
#undef PIS
}
#endif

/* dispc.irq_lock has to be locked by the caller */
static void _omap_dispc_set_irqs(void)
{
	u32 mask;
	int i;
	struct omap_dispc_isr_data *isr_data;

	mask = dispc_compat.irq_error_mask;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];

		if (isr_data->isr == NULL)
			continue;

		mask |= isr_data->mask;
	}

	dispc_write_irqenable(mask);
}

int omap_dispc_register_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	int ret;
	unsigned long flags;
	struct omap_dispc_isr_data *isr_data;

	if (isr == NULL)
		return -EINVAL;

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);

	/* check for duplicate entry */
	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];
		if (isr_data->isr == isr && isr_data->arg == arg &&
				isr_data->mask == mask) {
			ret = -EINVAL;
			goto err;
		}
	}

	isr_data = NULL;
	ret = -EBUSY;

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];

		if (isr_data->isr != NULL)
			continue;

		isr_data->isr = isr;
		isr_data->arg = arg;
		isr_data->mask = mask;
		ret = 0;

		break;
	}

	if (ret)
		goto err;

	_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return 0;
err:
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_register_isr);

int omap_dispc_unregister_isr(omap_dispc_isr_t isr, void *arg, u32 mask)
{
	int i;
	unsigned long flags;
	int ret = -EINVAL;
	struct omap_dispc_isr_data *isr_data;

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &dispc_compat.registered_isr[i];
		if (isr_data->isr != isr || isr_data->arg != arg ||
				isr_data->mask != mask)
			continue;

		/* found the correct isr */

		isr_data->isr = NULL;
		isr_data->arg = NULL;
		isr_data->mask = 0;

		ret = 0;
		break;
	}

	if (ret == 0)
		_omap_dispc_set_irqs();

	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	return ret;
}
EXPORT_SYMBOL(omap_dispc_unregister_isr);

static void print_irq_status(u32 status)
{
	if ((status & dispc_compat.irq_error_mask) == 0)
		return;

#define PIS(x) (status & DISPC_IRQ_##x) ? (#x " ") : ""

	pr_debug("DISPC IRQ: 0x%x: %s%s%s%s%s%s%s%s%s\n",
		status,
		PIS(OCP_ERR),
		PIS(GFX_FIFO_UNDERFLOW),
		PIS(VID1_FIFO_UNDERFLOW),
		PIS(VID2_FIFO_UNDERFLOW),
		dss_feat_get_num_ovls() > 3 ? PIS(VID3_FIFO_UNDERFLOW) : "",
		PIS(SYNC_LOST),
		PIS(SYNC_LOST_DIGIT),
		dss_has_feature(FEAT_MGR_LCD2) ? PIS(SYNC_LOST2) : "",
		dss_has_feature(FEAT_MGR_LCD3) ? PIS(SYNC_LOST3) : "");
#undef PIS
}

/* Called from dss.c. Note that we don't touch clocks here,
 * but we presume they are on because we got an IRQ. However,
 * an irq handler may turn the clocks off, so we may not have
 * clock later in the function. */
static irqreturn_t omap_dispc_irq_handler(int irq, void *arg)
{
	int i;
	u32 irqstatus, irqenable;
	u32 handledirqs = 0;
	u32 unhandled_errors;
	struct omap_dispc_isr_data *isr_data;
	struct omap_dispc_isr_data registered_isr[DISPC_MAX_NR_ISRS];

	spin_lock(&dispc_compat.irq_lock);

	irqstatus = dispc_read_irqstatus();
	irqenable = dispc_read_irqenable();

	/* IRQ is not for us */
	if (!(irqstatus & irqenable)) {
		spin_unlock(&dispc_compat.irq_lock);
		return IRQ_NONE;
	}

#ifdef CONFIG_FB_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock(&dispc_compat.irq_stats_lock);
	dispc_compat.irq_stats.irq_count++;
	dss_collect_irq_stats(irqstatus, dispc_compat.irq_stats.irqs);
	spin_unlock(&dispc_compat.irq_stats_lock);
#endif

	print_irq_status(irqstatus);

	/* Ack the interrupt. Do it here before clocks are possibly turned
	 * off */
	dispc_clear_irqstatus(irqstatus);
	/* flush posted write */
	dispc_read_irqstatus();

	/* make a copy and unlock, so that isrs can unregister
	 * themselves */
	memcpy(registered_isr, dispc_compat.registered_isr,
			sizeof(registered_isr));

	spin_unlock(&dispc_compat.irq_lock);

	for (i = 0; i < DISPC_MAX_NR_ISRS; i++) {
		isr_data = &registered_isr[i];

		if (!isr_data->isr)
			continue;

		if (isr_data->mask & irqstatus) {
			isr_data->isr(isr_data->arg, irqstatus);
			handledirqs |= isr_data->mask;
		}
	}

	spin_lock(&dispc_compat.irq_lock);

	unhandled_errors = irqstatus & ~handledirqs & dispc_compat.irq_error_mask;

	if (unhandled_errors) {
		dispc_compat.error_irqs |= unhandled_errors;

		dispc_compat.irq_error_mask &= ~unhandled_errors;
		_omap_dispc_set_irqs();

		schedule_work(&dispc_compat.error_work);
	}

	spin_unlock(&dispc_compat.irq_lock);

	return IRQ_HANDLED;
}

static void dispc_error_worker(struct work_struct *work)
{
	int i;
	u32 errors;
	unsigned long flags;
	static const unsigned fifo_underflow_bits[] = {
		DISPC_IRQ_GFX_FIFO_UNDERFLOW,
		DISPC_IRQ_VID1_FIFO_UNDERFLOW,
		DISPC_IRQ_VID2_FIFO_UNDERFLOW,
		DISPC_IRQ_VID3_FIFO_UNDERFLOW,
	};

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);
	errors = dispc_compat.error_irqs;
	dispc_compat.error_irqs = 0;
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	dispc_runtime_get();

	for (i = 0; i < omap_dss_get_num_overlays(); ++i) {
		struct omap_overlay *ovl;
		unsigned bit;

		ovl = omap_dss_get_overlay(i);
		bit = fifo_underflow_bits[i];

		if (bit & errors) {
			DSSERR("FIFO UNDERFLOW on %s, disabling the overlay\n",
					ovl->name);
			ovl->disable(ovl);
			msleep(50);
		}
	}

	for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
		struct omap_overlay_manager *mgr;
		unsigned bit;

		mgr = omap_dss_get_overlay_manager(i);
		bit = dispc_mgr_get_sync_lost_irq(i);

		if (bit & errors) {
			int j;

			DSSERR("SYNC_LOST on channel %s, restarting the output "
					"with video overlays disabled\n",
					mgr->name);

			dss_mgr_disable(mgr);

			for (j = 0; j < omap_dss_get_num_overlays(); ++j) {
				struct omap_overlay *ovl;
				ovl = omap_dss_get_overlay(j);

				if (ovl->id != OMAP_DSS_GFX &&
						ovl->manager == mgr)
					ovl->disable(ovl);
			}

			dss_mgr_enable(mgr);
		}
	}

	if (errors & DISPC_IRQ_OCP_ERR) {
		DSSERR("OCP_ERR\n");
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			struct omap_overlay_manager *mgr;

			mgr = omap_dss_get_overlay_manager(i);
			dss_mgr_disable(mgr);
		}
	}

	spin_lock_irqsave(&dispc_compat.irq_lock, flags);
	dispc_compat.irq_error_mask |= errors;
	_omap_dispc_set_irqs();
	spin_unlock_irqrestore(&dispc_compat.irq_lock, flags);

	dispc_runtime_put();
}

int dss_dispc_initialize_irq(void)
{
	int r;

#ifdef CONFIG_FB_OMAP2_DSS_COLLECT_IRQ_STATS
	spin_lock_init(&dispc_compat.irq_stats_lock);
	dispc_compat.irq_stats.last_reset = jiffies;
	dss_debugfs_create_file("dispc_irq", dispc_dump_irqs);
#endif

	spin_lock_init(&dispc_compat.irq_lock);

	memset(dispc_compat.registered_isr, 0,
			sizeof(dispc_compat.registered_isr));

	dispc_compat.irq_error_mask = DISPC_IRQ_MASK_ERROR;
	if (dss_has_feature(FEAT_MGR_LCD2))
		dispc_compat.irq_error_mask |= DISPC_IRQ_SYNC_LOST2;
	if (dss_has_feature(FEAT_MGR_LCD3))
		dispc_compat.irq_error_mask |= DISPC_IRQ_SYNC_LOST3;
	if (dss_feat_get_num_ovls() > 3)
		dispc_compat.irq_error_mask |= DISPC_IRQ_VID3_FIFO_UNDERFLOW;

	/*
	 * there's SYNC_LOST_DIGIT waiting after enabling the DSS,
	 * so clear it
	 */
	dispc_clear_irqstatus(dispc_read_irqstatus());

	INIT_WORK(&dispc_compat.error_work, dispc_error_worker);

	_omap_dispc_set_irqs();

	r = dispc_request_irq(omap_dispc_irq_handler, &dispc_compat);
	if (r) {
		DSSERR("dispc_request_irq failed\n");
		return r;
	}

	return 0;
}

void dss_dispc_uninitialize_irq(void)
{
	dispc_free_irq(&dispc_compat);
}

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

	if (!dispc_mgr_is_enabled(channel))
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

	if (dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT))
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

	if (!dispc_mgr_is_enabled(OMAP_DSS_CHANNEL_DIGIT))
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

static inline void dispc_irq_wait_handler(void *data, u32 mask)
{
	complete((struct completion *)data);
}

int omap_dispc_wait_for_irq_interruptible_timeout(u32 irqmask,
		unsigned long timeout)
{

	int r;
	long time_left;
	DECLARE_COMPLETION_ONSTACK(completion);

	r = omap_dispc_register_isr(dispc_irq_wait_handler, &completion,
			irqmask);

	if (r)
		return r;

	time_left = wait_for_completion_interruptible_timeout(&completion,
			timeout);

	omap_dispc_unregister_isr(dispc_irq_wait_handler, &completion, irqmask);

	if (time_left == 0)
		return -ETIMEDOUT;

	if (time_left == -ERESTARTSYS)
		return -ERESTARTSYS;

	return 0;
}
