/*
 * Copyright (C) 2011 Texas Instruments
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
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

/*
 * We have 4 levels of cache for the dispc settings. First two are in SW and
 * the latter two in HW.
 *
 * +--------------------+
 * |overlay/manager_info|
 * +--------------------+
 *          v
 *        apply()
 *          v
 * +--------------------+
 * |       info         |
 * +--------------------+
 *          v
 *      write_regs()
 *          v
 * +--------------------+
 * |  shadow registers  |
 * +--------------------+
 *          v
 * VFP or lcd/digit_enable
 *          v
 * +--------------------+
 * |      registers     |
 * +--------------------+
 */

struct ovl_priv_data {
	/* If true, cache changed, but not written to shadow registers. Set
	 * in apply(), cleared when registers written. */
	bool dirty;
	/* If true, shadow registers contain changed values not yet in real
	 * registers. Set when writing to shadow registers, cleared at
	 * VSYNC/EVSYNC */
	bool shadow_dirty;

	struct omap_overlay_info info;

	enum omap_channel channel;

	u32 fifo_low;
	u32 fifo_high;

	bool extra_info_dirty;
	bool shadow_extra_info_dirty;

	bool enabled;

};

struct mgr_priv_data {
	/* If true, cache changed, but not written to shadow registers. Set
	 * in apply(), cleared when registers written. */
	bool dirty;
	/* If true, shadow registers contain changed values not yet in real
	 * registers. Set when writing to shadow registers, cleared at
	 * VSYNC/EVSYNC */
	bool shadow_dirty;

	struct omap_overlay_manager_info info;

	/* If true, GO bit is up and shadow registers cannot be written.
	 * Never true for manual update displays */
	bool busy;

	/* If true, dispc output is enabled */
	bool updating;

	/* If true, a display is enabled using this manager */
	bool enabled;
};

static struct {
	struct ovl_priv_data ovl_priv_data_array[MAX_DSS_OVERLAYS];
	struct mgr_priv_data mgr_priv_data_array[MAX_DSS_MANAGERS];

	bool irq_enabled;
} dss_data;

/* protects dss_data */
static spinlock_t data_lock;
/* lock for blocking functions */
static DEFINE_MUTEX(apply_lock);

static void dss_register_vsync_isr(void);

static struct ovl_priv_data *get_ovl_priv(struct omap_overlay *ovl)
{
	return &dss_data.ovl_priv_data_array[ovl->id];
}

static struct mgr_priv_data *get_mgr_priv(struct omap_overlay_manager *mgr)
{
	return &dss_data.mgr_priv_data_array[mgr->id];
}

void dss_apply_init(void)
{
	spin_lock_init(&data_lock);
}

static bool ovl_manual_update(struct omap_overlay *ovl)
{
	return ovl->manager->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static bool mgr_manual_update(struct omap_overlay_manager *mgr)
{
	return mgr->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static bool need_isr(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i;

	for (i = 0; i < num_mgrs; ++i) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;
		struct omap_overlay *ovl;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled)
			continue;

		if (mgr_manual_update(mgr)) {
			/* to catch FRAMEDONE */
			if (mp->updating)
				return true;
		} else {
			/* to catch GO bit going down */
			if (mp->busy)
				return true;

			/* to write new values to registers */
			if (mp->dirty)
				return true;

			list_for_each_entry(ovl, &mgr->overlays, list) {
				struct ovl_priv_data *op;

				op = get_ovl_priv(ovl);

				if (!op->enabled)
					continue;

				/* to write new values to registers */
				if (op->dirty || op->extra_info_dirty)
					return true;
			}
		}
	}

	return false;
}

static bool need_go(struct omap_overlay_manager *mgr)
{
	struct omap_overlay *ovl;
	struct mgr_priv_data *mp;
	struct ovl_priv_data *op;

	mp = get_mgr_priv(mgr);

	if (mp->shadow_dirty)
		return true;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);
		if (op->shadow_dirty || op->shadow_extra_info_dirty)
			return true;
	}

	return false;
}

int dss_mgr_wait_for_go(struct omap_overlay_manager *mgr)
{
	unsigned long timeout = msecs_to_jiffies(500);
	struct mgr_priv_data *mp;
	u32 irq;
	int r;
	int i;
	struct omap_dss_device *dssdev = mgr->device;

	if (!dssdev || dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	if (mgr_manual_update(mgr))
		return 0;

	irq = dispc_mgr_get_vsync_irq(mgr->id);

	mp = get_mgr_priv(mgr);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = mp->dirty;
		shadow_dirty = mp->shadow_dirty;
		spin_unlock_irqrestore(&data_lock, flags);

		if (!dirty && !shadow_dirty) {
			r = 0;
			break;
		}

		/* 4 iterations is the worst case:
		 * 1 - initial iteration, dirty = true (between VFP and VSYNC)
		 * 2 - first VSYNC, dirty = true
		 * 3 - dirty = false, shadow_dirty = true
		 * 4 - shadow_dirty = false */
		if (i++ == 3) {
			DSSERR("mgr(%d)->wait_for_go() not finishing\n",
					mgr->id);
			r = 0;
			break;
		}

		r = omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);
		if (r == -ERESTARTSYS)
			break;

		if (r) {
			DSSERR("mgr(%d)->wait_for_go() timeout\n", mgr->id);
			break;
		}
	}

	return r;
}

int dss_mgr_wait_for_go_ovl(struct omap_overlay *ovl)
{
	unsigned long timeout = msecs_to_jiffies(500);
	struct ovl_priv_data *op;
	struct omap_dss_device *dssdev;
	u32 irq;
	int r;
	int i;

	if (!ovl->manager)
		return 0;

	dssdev = ovl->manager->device;

	if (!dssdev || dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	if (ovl_manual_update(ovl))
		return 0;

	irq = dispc_mgr_get_vsync_irq(ovl->manager->id);

	op = get_ovl_priv(ovl);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = op->dirty;
		shadow_dirty = op->shadow_dirty;
		spin_unlock_irqrestore(&data_lock, flags);

		if (!dirty && !shadow_dirty) {
			r = 0;
			break;
		}

		/* 4 iterations is the worst case:
		 * 1 - initial iteration, dirty = true (between VFP and VSYNC)
		 * 2 - first VSYNC, dirty = true
		 * 3 - dirty = false, shadow_dirty = true
		 * 4 - shadow_dirty = false */
		if (i++ == 3) {
			DSSERR("ovl(%d)->wait_for_go() not finishing\n",
					ovl->id);
			r = 0;
			break;
		}

		r = omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);
		if (r == -ERESTARTSYS)
			break;

		if (r) {
			DSSERR("ovl(%d)->wait_for_go() timeout\n", ovl->id);
			break;
		}
	}

	return r;
}

static void dss_ovl_write_regs(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	struct omap_overlay_info *oi;
	bool ilace, replication;
	struct mgr_priv_data *mp;
	int r;

	DSSDBGF("%d", ovl->id);

	if (!op->enabled || !op->dirty)
		return;

	oi = &op->info;

	replication = dss_use_replication(ovl->manager->device, oi->color_mode);

	ilace = ovl->manager->device->type == OMAP_DISPLAY_TYPE_VENC;

	dispc_ovl_set_channel_out(ovl->id, op->channel);

	r = dispc_ovl_setup(ovl->id, oi, ilace, replication);
	if (r) {
		/*
		 * We can't do much here, as this function can be called from
		 * vsync interrupt.
		 */
		DSSERR("dispc_ovl_setup failed for ovl %d\n", ovl->id);

		/* This will leave fifo configurations in a nonoptimal state */
		op->enabled = false;
		dispc_ovl_enable(ovl->id, false);
		return;
	}

	dispc_ovl_set_fifo_threshold(ovl->id, op->fifo_low, op->fifo_high);

	mp = get_mgr_priv(ovl->manager);

	op->dirty = false;
	if (mp->updating)
		op->shadow_dirty = true;
}

static void dss_ovl_write_regs_extra(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	struct mgr_priv_data *mp;

	DSSDBGF("%d", ovl->id);

	if (!op->extra_info_dirty)
		return;

	/* note: write also when op->enabled == false, so that the ovl gets
	 * disabled */

	dispc_ovl_enable(ovl->id, op->enabled);

	mp = get_mgr_priv(ovl->manager);

	op->extra_info_dirty = false;
	if (mp->updating)
		op->shadow_extra_info_dirty = true;
}

static void dss_mgr_write_regs(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	struct omap_overlay *ovl;

	DSSDBGF("%d", mgr->id);

	if (!mp->enabled)
		return;

	WARN_ON(mp->busy);

	/* Commit overlay settings */
	list_for_each_entry(ovl, &mgr->overlays, list) {
		dss_ovl_write_regs(ovl);
		dss_ovl_write_regs_extra(ovl);
	}

	if (mp->dirty) {
		dispc_mgr_setup(mgr->id, &mp->info);

		mp->dirty = false;
		if (mp->updating)
			mp->shadow_dirty = true;
	}
}

static void dss_write_regs(void)
{
	const int num_mgrs = omap_dss_get_num_overlay_managers();
	int i;

	for (i = 0; i < num_mgrs; ++i) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled || mgr_manual_update(mgr) || mp->busy)
			continue;

		dss_mgr_write_regs(mgr);

		if (need_go(mgr)) {
			mp->busy = true;

			if (!dss_data.irq_enabled && need_isr())
				dss_register_vsync_isr();

			dispc_mgr_go(mgr->id);
		}
	}
}

void dss_mgr_start_update(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	WARN_ON(mp->updating);

	dss_mgr_write_regs(mgr);

	mp->updating = true;

	if (!dss_data.irq_enabled && need_isr())
		dss_register_vsync_isr();

	dispc_mgr_enable(mgr->id, true);

	spin_unlock_irqrestore(&data_lock, flags);
}

static void dss_apply_irq_handler(void *data, u32 mask);

static void dss_register_vsync_isr(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	u32 mask;
	int r, i;

	mask = 0;
	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_vsync_irq(i);

	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_framedone_irq(i);

	r = omap_dispc_register_isr(dss_apply_irq_handler, NULL, mask);
	WARN_ON(r);

	dss_data.irq_enabled = true;
}

static void dss_unregister_vsync_isr(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	u32 mask;
	int r, i;

	mask = 0;
	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_vsync_irq(i);

	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_framedone_irq(i);

	r = omap_dispc_unregister_isr(dss_apply_irq_handler, NULL, mask);
	WARN_ON(r);

	dss_data.irq_enabled = false;
}

static void mgr_clear_shadow_dirty(struct omap_overlay_manager *mgr)
{
	struct omap_overlay *ovl;
	struct mgr_priv_data *mp;
	struct ovl_priv_data *op;

	mp = get_mgr_priv(mgr);
	mp->shadow_dirty = false;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);
		op->shadow_dirty = false;
		op->shadow_extra_info_dirty = false;
	}
}

static void dss_apply_irq_handler(void *data, u32 mask)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i;

	spin_lock(&data_lock);

	/* clear busy, updating flags, shadow_dirty flags */
	for (i = 0; i < num_mgrs; i++) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled)
			continue;

		mp->updating = dispc_mgr_is_enabled(i);

		if (!mgr_manual_update(mgr)) {
			mp->busy = dispc_mgr_go_busy(i);

			if (!mp->busy)
				mgr_clear_shadow_dirty(mgr);
		} else {
			if (!mp->updating)
				mgr_clear_shadow_dirty(mgr);
		}
	}

	dss_write_regs();

	if (!need_isr())
		dss_unregister_vsync_isr();

	spin_unlock(&data_lock);
}

static void omap_dss_mgr_apply_ovl(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;

	op = get_ovl_priv(ovl);

	if (ovl->manager_changed) {
		ovl->manager_changed = false;
		ovl->info_dirty  = true;
	}

	if (!ovl->info_dirty)
		return;

	ovl->info_dirty = false;
	op->dirty = true;
	op->info = ovl->info;

	op->channel = ovl->manager->id;
}

static void omap_dss_mgr_apply_mgr(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp;

	mp = get_mgr_priv(mgr);

	if (mgr->device_changed) {
		mgr->device_changed = false;
		mgr->info_dirty  = true;
	}

	if (!mgr->info_dirty)
		return;

	mgr->info_dirty = false;
	mp->dirty = true;
	mp->info = mgr->info;
}

static void omap_dss_mgr_apply_ovl_fifos(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;
	struct omap_dss_device *dssdev;
	u32 size, burst_size;

	op = get_ovl_priv(ovl);

	dssdev = ovl->manager->device;

	size = dispc_ovl_get_fifo_size(ovl->id);

	burst_size = dispc_ovl_get_burst_size(ovl->id);

	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_DPI:
	case OMAP_DISPLAY_TYPE_DBI:
	case OMAP_DISPLAY_TYPE_SDI:
	case OMAP_DISPLAY_TYPE_VENC:
	case OMAP_DISPLAY_TYPE_HDMI:
		default_get_overlay_fifo_thresholds(ovl->id, size,
				burst_size, &op->fifo_low,
				&op->fifo_high);
		break;
#ifdef CONFIG_OMAP2_DSS_DSI
	case OMAP_DISPLAY_TYPE_DSI:
		dsi_get_overlay_fifo_thresholds(ovl->id, size,
				burst_size, &op->fifo_low,
				&op->fifo_high);
		break;
#endif
	default:
		BUG();
	}
}

int omap_dss_mgr_apply(struct omap_overlay_manager *mgr)
{
	int r;
	unsigned long flags;
	struct omap_overlay *ovl;

	DSSDBG("omap_dss_mgr_apply(%s)\n", mgr->name);

	r = dispc_runtime_get();
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	/* Configure overlays */
	list_for_each_entry(ovl, &mgr->overlays, list)
		omap_dss_mgr_apply_ovl(ovl);

	/* Configure manager */
	omap_dss_mgr_apply_mgr(mgr);

	/* Configure overlay fifos */
	list_for_each_entry(ovl, &mgr->overlays, list)
		omap_dss_mgr_apply_ovl_fifos(ovl);

	dss_write_regs();

	spin_unlock_irqrestore(&data_lock, flags);

	dispc_runtime_put();

	return r;
}

void dss_mgr_enable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	mutex_lock(&apply_lock);

	spin_lock_irqsave(&data_lock, flags);

	mp->enabled = true;

	dss_write_regs();

	if (!mgr_manual_update(mgr))
		mp->updating = true;

	spin_unlock_irqrestore(&data_lock, flags);

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, true);

	mutex_unlock(&apply_lock);
}

void dss_mgr_disable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	mutex_lock(&apply_lock);

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, false);

	spin_lock_irqsave(&data_lock, flags);

	mp->updating = false;
	mp->enabled = false;

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);
}

int dss_mgr_set_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	mgr->info = *info;
	mgr->info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_mgr_get_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = mgr->info;

	spin_unlock_irqrestore(&data_lock, flags);
}

int dss_mgr_set_device(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&apply_lock);

	if (dssdev->manager) {
		DSSERR("display '%s' already has a manager '%s'\n",
			       dssdev->name, dssdev->manager->name);
		r = -EINVAL;
		goto err;
	}

	if ((mgr->supported_displays & dssdev->type) == 0) {
		DSSERR("display '%s' does not support manager '%s'\n",
			       dssdev->name, mgr->name);
		r = -EINVAL;
		goto err;
	}

	dssdev->manager = mgr;
	mgr->device = dssdev;
	mgr->device_changed = true;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_mgr_unset_device(struct omap_overlay_manager *mgr)
{
	int r;

	mutex_lock(&apply_lock);

	if (!mgr->device) {
		DSSERR("failed to unset display, display not set.\n");
		r = -EINVAL;
		goto err;
	}

	/*
	 * Don't allow currently enabled displays to have the overlay manager
	 * pulled out from underneath them
	 */
	if (mgr->device->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	mgr->device->manager = NULL;
	mgr->device = NULL;
	mgr->device_changed = true;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}



int dss_ovl_set_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	ovl->info = *info;
	ovl->info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_ovl_get_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = ovl->info;

	spin_unlock_irqrestore(&data_lock, flags);
}

int dss_ovl_set_manager(struct omap_overlay *ovl,
		struct omap_overlay_manager *mgr)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	int r;

	if (!mgr)
		return -EINVAL;

	mutex_lock(&apply_lock);

	if (ovl->manager) {
		DSSERR("overlay '%s' already has a manager '%s'\n",
				ovl->name, ovl->manager->name);
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	if (op->enabled) {
		spin_unlock_irqrestore(&data_lock, flags);
		DSSERR("overlay has to be disabled to change the manager\n");
		r = -EINVAL;
		goto err;
	}

	ovl->manager = mgr;
	list_add_tail(&ovl->list, &mgr->overlays);
	ovl->manager_changed = true;

	spin_unlock_irqrestore(&data_lock, flags);

	/* XXX: When there is an overlay on a DSI manual update display, and
	 * the overlay is first disabled, then moved to tv, and enabled, we
	 * seem to get SYNC_LOST_DIGIT error.
	 *
	 * Waiting doesn't seem to help, but updating the manual update display
	 * after disabling the overlay seems to fix this. This hints that the
	 * overlay is perhaps somehow tied to the LCD output until the output
	 * is updated.
	 *
	 * Userspace workaround for this is to update the LCD after disabling
	 * the overlay, but before moving the overlay to TV.
	 */

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_ovl_unset_manager(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (!ovl->manager) {
		DSSERR("failed to detach overlay: manager not set\n");
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	if (op->enabled) {
		spin_unlock_irqrestore(&data_lock, flags);
		DSSERR("overlay has to be disabled to unset the manager\n");
		r = -EINVAL;
		goto err;
	}

	ovl->manager = NULL;
	list_del(&ovl->list);
	ovl->manager_changed = true;

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

bool dss_ovl_is_enabled(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	bool e;

	spin_lock_irqsave(&data_lock, flags);

	e = op->enabled;

	spin_unlock_irqrestore(&data_lock, flags);

	return e;
}

int dss_ovl_enable(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (ovl->manager == NULL || ovl->manager->device == NULL) {
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	op->enabled = true;
	op->extra_info_dirty = true;

	dss_write_regs();

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_ovl_disable(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	int r;

	mutex_lock(&apply_lock);

	if (ovl->manager == NULL || ovl->manager->device == NULL) {
		r = -EINVAL;
		goto err;
	}

	spin_lock_irqsave(&data_lock, flags);

	op->enabled = false;
	op->extra_info_dirty = true;

	dss_write_regs();

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);

	return 0;

err:
	mutex_unlock(&apply_lock);
	return r;
}

