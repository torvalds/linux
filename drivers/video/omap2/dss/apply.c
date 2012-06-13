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
 *       set_info()
 *          v
 * +--------------------+
 * |     user_info      |
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

	bool user_info_dirty;
	struct omap_overlay_info user_info;

	bool info_dirty;
	struct omap_overlay_info info;

	bool shadow_info_dirty;

	bool extra_info_dirty;
	bool shadow_extra_info_dirty;

	bool enabled;
	enum omap_channel channel;
	u32 fifo_low, fifo_high;

	/*
	 * True if overlay is to be enabled. Used to check and calculate configs
	 * for the overlay before it is enabled in the HW.
	 */
	bool enabling;
};

struct mgr_priv_data {

	bool user_info_dirty;
	struct omap_overlay_manager_info user_info;

	bool info_dirty;
	struct omap_overlay_manager_info info;

	bool shadow_info_dirty;

	/* If true, GO bit is up and shadow registers cannot be written.
	 * Never true for manual update displays */
	bool busy;

	/* If true, dispc output is enabled */
	bool updating;

	/* If true, a display is enabled using this manager */
	bool enabled;

	bool extra_info_dirty;
	bool shadow_extra_info_dirty;

	struct omap_video_timings timings;
};

static struct {
	struct ovl_priv_data ovl_priv_data_array[MAX_DSS_OVERLAYS];
	struct mgr_priv_data mgr_priv_data_array[MAX_DSS_MANAGERS];

	bool fifo_merge_dirty;
	bool fifo_merge;

	bool irq_enabled;
} dss_data;

/* protects dss_data */
static spinlock_t data_lock;
/* lock for blocking functions */
static DEFINE_MUTEX(apply_lock);
static DECLARE_COMPLETION(extra_updated_completion);

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
	const int num_ovls = dss_feat_get_num_ovls();
	int i;

	spin_lock_init(&data_lock);

	for (i = 0; i < num_ovls; ++i) {
		struct ovl_priv_data *op;

		op = &dss_data.ovl_priv_data_array[i];

		op->info.global_alpha = 255;

		switch (i) {
		case 0:
			op->info.zorder = 0;
			break;
		case 1:
			op->info.zorder =
				dss_has_feature(FEAT_ALPHA_FREE_ZORDER) ? 3 : 0;
			break;
		case 2:
			op->info.zorder =
				dss_has_feature(FEAT_ALPHA_FREE_ZORDER) ? 2 : 0;
			break;
		case 3:
			op->info.zorder =
				dss_has_feature(FEAT_ALPHA_FREE_ZORDER) ? 1 : 0;
			break;
		}

		op->user_info = op->info;
	}
}

static bool ovl_manual_update(struct omap_overlay *ovl)
{
	return ovl->manager->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static bool mgr_manual_update(struct omap_overlay_manager *mgr)
{
	return mgr->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static int dss_check_settings_low(struct omap_overlay_manager *mgr,
		bool applying)
{
	struct omap_overlay_info *oi;
	struct omap_overlay_manager_info *mi;
	struct omap_overlay *ovl;
	struct omap_overlay_info *ois[MAX_DSS_OVERLAYS];
	struct ovl_priv_data *op;
	struct mgr_priv_data *mp;

	mp = get_mgr_priv(mgr);

	if (!mp->enabled)
		return 0;

	if (applying && mp->user_info_dirty)
		mi = &mp->user_info;
	else
		mi = &mp->info;

	/* collect the infos to be tested into the array */
	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);

		if (!op->enabled && !op->enabling)
			oi = NULL;
		else if (applying && op->user_info_dirty)
			oi = &op->user_info;
		else
			oi = &op->info;

		ois[ovl->id] = oi;
	}

	return dss_mgr_check(mgr, mi, &mp->timings, ois);
}

/*
 * check manager and overlay settings using overlay_info from data->info
 */
static int dss_check_settings(struct omap_overlay_manager *mgr)
{
	return dss_check_settings_low(mgr, false);
}

/*
 * check manager and overlay settings using overlay_info from ovl->info if
 * dirty and from data->info otherwise
 */
static int dss_check_settings_apply(struct omap_overlay_manager *mgr)
{
	return dss_check_settings_low(mgr, true);
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
			if (mp->info_dirty)
				return true;

			/* to set GO bit */
			if (mp->shadow_info_dirty)
				return true;

			/*
			 * NOTE: we don't check extra_info flags for disabled
			 * managers, once the manager is enabled, the extra_info
			 * related manager changes will be taken in by HW.
			 */

			/* to write new values to registers */
			if (mp->extra_info_dirty)
				return true;

			/* to set GO bit */
			if (mp->shadow_extra_info_dirty)
				return true;

			list_for_each_entry(ovl, &mgr->overlays, list) {
				struct ovl_priv_data *op;

				op = get_ovl_priv(ovl);

				/*
				 * NOTE: we check extra_info flags even for
				 * disabled overlays, as extra_infos need to be
				 * always written.
				 */

				/* to write new values to registers */
				if (op->extra_info_dirty)
					return true;

				/* to set GO bit */
				if (op->shadow_extra_info_dirty)
					return true;

				if (!op->enabled)
					continue;

				/* to write new values to registers */
				if (op->info_dirty)
					return true;

				/* to set GO bit */
				if (op->shadow_info_dirty)
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

	if (mp->shadow_info_dirty || mp->shadow_extra_info_dirty)
		return true;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);
		if (op->shadow_info_dirty || op->shadow_extra_info_dirty)
			return true;
	}

	return false;
}

/* returns true if an extra_info field is currently being updated */
static bool extra_info_update_ongoing(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i;

	for (i = 0; i < num_mgrs; ++i) {
		struct omap_overlay_manager *mgr;
		struct omap_overlay *ovl;
		struct mgr_priv_data *mp;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled)
			continue;

		if (!mp->updating)
			continue;

		if (mp->extra_info_dirty || mp->shadow_extra_info_dirty)
			return true;

		list_for_each_entry(ovl, &mgr->overlays, list) {
			struct ovl_priv_data *op = get_ovl_priv(ovl);

			if (op->extra_info_dirty || op->shadow_extra_info_dirty)
				return true;
		}
	}

	return false;
}

/* wait until no extra_info updates are pending */
static void wait_pending_extra_info_updates(void)
{
	bool updating;
	unsigned long flags;
	unsigned long t;
	int r;

	spin_lock_irqsave(&data_lock, flags);

	updating = extra_info_update_ongoing();

	if (!updating) {
		spin_unlock_irqrestore(&data_lock, flags);
		return;
	}

	init_completion(&extra_updated_completion);

	spin_unlock_irqrestore(&data_lock, flags);

	t = msecs_to_jiffies(500);
	r = wait_for_completion_timeout(&extra_updated_completion, t);
	if (r == 0)
		DSSWARN("timeout in wait_pending_extra_info_updates\n");
	else if (r < 0)
		DSSERR("wait_pending_extra_info_updates failed: %d\n", r);
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

	r = dispc_runtime_get();
	if (r)
		return r;

	irq = dispc_mgr_get_vsync_irq(mgr->id);

	mp = get_mgr_priv(mgr);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = mp->info_dirty;
		shadow_dirty = mp->shadow_info_dirty;
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

	dispc_runtime_put();

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

	r = dispc_runtime_get();
	if (r)
		return r;

	irq = dispc_mgr_get_vsync_irq(ovl->manager->id);

	op = get_ovl_priv(ovl);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = op->info_dirty;
		shadow_dirty = op->shadow_info_dirty;
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

	dispc_runtime_put();

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

	if (!op->enabled || !op->info_dirty)
		return;

	oi = &op->info;

	mp = get_mgr_priv(ovl->manager);

	replication = dss_use_replication(ovl->manager->device, oi->color_mode);

	ilace = ovl->manager->device->type == OMAP_DISPLAY_TYPE_VENC;

	r = dispc_ovl_setup(ovl->id, oi, ilace, replication, &mp->timings);
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

	op->info_dirty = false;
	if (mp->updating)
		op->shadow_info_dirty = true;
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
	dispc_ovl_set_channel_out(ovl->id, op->channel);
	dispc_ovl_set_fifo_threshold(ovl->id, op->fifo_low, op->fifo_high);

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

	if (mp->info_dirty) {
		dispc_mgr_setup(mgr->id, &mp->info);

		mp->info_dirty = false;
		if (mp->updating)
			mp->shadow_info_dirty = true;
	}
}

static void dss_mgr_write_regs_extra(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);

	DSSDBGF("%d", mgr->id);

	if (!mp->extra_info_dirty)
		return;

	dispc_mgr_set_timings(mgr->id, &mp->timings);

	mp->extra_info_dirty = false;
	if (mp->updating)
		mp->shadow_extra_info_dirty = true;
}

static void dss_write_regs_common(void)
{
	const int num_mgrs = omap_dss_get_num_overlay_managers();
	int i;

	if (!dss_data.fifo_merge_dirty)
		return;

	for (i = 0; i < num_mgrs; ++i) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (mp->enabled) {
			if (dss_data.fifo_merge_dirty) {
				dispc_enable_fifomerge(dss_data.fifo_merge);
				dss_data.fifo_merge_dirty = false;
			}

			if (mp->updating)
				mp->shadow_info_dirty = true;
		}
	}
}

static void dss_write_regs(void)
{
	const int num_mgrs = omap_dss_get_num_overlay_managers();
	int i;

	dss_write_regs_common();

	for (i = 0; i < num_mgrs; ++i) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;
		int r;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled || mgr_manual_update(mgr) || mp->busy)
			continue;

		r = dss_check_settings(mgr);
		if (r) {
			DSSERR("cannot write registers for manager %s: "
					"illegal configuration\n", mgr->name);
			continue;
		}

		dss_mgr_write_regs(mgr);
		dss_mgr_write_regs_extra(mgr);
	}
}

static void dss_set_go_bits(void)
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

		if (!need_go(mgr))
			continue;

		mp->busy = true;

		if (!dss_data.irq_enabled && need_isr())
			dss_register_vsync_isr();

		dispc_mgr_go(mgr->id);
	}

}

static void mgr_clear_shadow_dirty(struct omap_overlay_manager *mgr)
{
	struct omap_overlay *ovl;
	struct mgr_priv_data *mp;
	struct ovl_priv_data *op;

	mp = get_mgr_priv(mgr);
	mp->shadow_info_dirty = false;
	mp->shadow_extra_info_dirty = false;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);
		op->shadow_info_dirty = false;
		op->shadow_extra_info_dirty = false;
	}
}

void dss_mgr_start_update(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;
	int r;

	spin_lock_irqsave(&data_lock, flags);

	WARN_ON(mp->updating);

	r = dss_check_settings(mgr);
	if (r) {
		DSSERR("cannot start manual update: illegal configuration\n");
		spin_unlock_irqrestore(&data_lock, flags);
		return;
	}

	dss_mgr_write_regs(mgr);
	dss_mgr_write_regs_extra(mgr);

	dss_write_regs_common();

	mp->updating = true;

	if (!dss_data.irq_enabled && need_isr())
		dss_register_vsync_isr();

	dispc_mgr_enable(mgr->id, true);

	mgr_clear_shadow_dirty(mgr);

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

static void dss_apply_irq_handler(void *data, u32 mask)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i;
	bool extra_updating;

	spin_lock(&data_lock);

	/* clear busy, updating flags, shadow_dirty flags */
	for (i = 0; i < num_mgrs; i++) {
		struct omap_overlay_manager *mgr;
		struct mgr_priv_data *mp;
		bool was_updating;

		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled)
			continue;

		was_updating = mp->updating;
		mp->updating = dispc_mgr_is_enabled(i);

		if (!mgr_manual_update(mgr)) {
			bool was_busy = mp->busy;
			mp->busy = dispc_mgr_go_busy(i);

			if (was_busy && !mp->busy)
				mgr_clear_shadow_dirty(mgr);
		}
	}

	dss_write_regs();
	dss_set_go_bits();

	extra_updating = extra_info_update_ongoing();
	if (!extra_updating)
		complete_all(&extra_updated_completion);

	if (!need_isr())
		dss_unregister_vsync_isr();

	spin_unlock(&data_lock);
}

static void omap_dss_mgr_apply_ovl(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;

	op = get_ovl_priv(ovl);

	if (!op->user_info_dirty)
		return;

	op->user_info_dirty = false;
	op->info_dirty = true;
	op->info = op->user_info;
}

static void omap_dss_mgr_apply_mgr(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp;

	mp = get_mgr_priv(mgr);

	if (!mp->user_info_dirty)
		return;

	mp->user_info_dirty = false;
	mp->info_dirty = true;
	mp->info = mp->user_info;
}

int omap_dss_mgr_apply(struct omap_overlay_manager *mgr)
{
	unsigned long flags;
	struct omap_overlay *ovl;
	int r;

	DSSDBG("omap_dss_mgr_apply(%s)\n", mgr->name);

	spin_lock_irqsave(&data_lock, flags);

	r = dss_check_settings_apply(mgr);
	if (r) {
		spin_unlock_irqrestore(&data_lock, flags);
		DSSERR("failed to apply settings: illegal configuration.\n");
		return r;
	}

	/* Configure overlays */
	list_for_each_entry(ovl, &mgr->overlays, list)
		omap_dss_mgr_apply_ovl(ovl);

	/* Configure manager */
	omap_dss_mgr_apply_mgr(mgr);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

static void dss_apply_ovl_enable(struct omap_overlay *ovl, bool enable)
{
	struct ovl_priv_data *op;

	op = get_ovl_priv(ovl);

	if (op->enabled == enable)
		return;

	op->enabled = enable;
	op->extra_info_dirty = true;
}

static void dss_apply_ovl_fifo_thresholds(struct omap_overlay *ovl,
		u32 fifo_low, u32 fifo_high)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);

	if (op->fifo_low == fifo_low && op->fifo_high == fifo_high)
		return;

	op->fifo_low = fifo_low;
	op->fifo_high = fifo_high;
	op->extra_info_dirty = true;
}

static void dss_apply_fifo_merge(bool use_fifo_merge)
{
	if (dss_data.fifo_merge == use_fifo_merge)
		return;

	dss_data.fifo_merge = use_fifo_merge;
	dss_data.fifo_merge_dirty = true;
}

static void dss_ovl_setup_fifo(struct omap_overlay *ovl,
		bool use_fifo_merge)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	u32 fifo_low, fifo_high;

	if (!op->enabled && !op->enabling)
		return;

	dispc_ovl_compute_fifo_thresholds(ovl->id, &fifo_low, &fifo_high,
			use_fifo_merge, ovl_manual_update(ovl));

	dss_apply_ovl_fifo_thresholds(ovl, fifo_low, fifo_high);
}

static void dss_mgr_setup_fifos(struct omap_overlay_manager *mgr,
		bool use_fifo_merge)
{
	struct omap_overlay *ovl;
	struct mgr_priv_data *mp;

	mp = get_mgr_priv(mgr);

	if (!mp->enabled)
		return;

	list_for_each_entry(ovl, &mgr->overlays, list)
		dss_ovl_setup_fifo(ovl, use_fifo_merge);
}

static void dss_setup_fifos(bool use_fifo_merge)
{
	const int num_mgrs = omap_dss_get_num_overlay_managers();
	struct omap_overlay_manager *mgr;
	int i;

	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		dss_mgr_setup_fifos(mgr, use_fifo_merge);
	}
}

static int get_num_used_managers(void)
{
	const int num_mgrs = omap_dss_get_num_overlay_managers();
	struct omap_overlay_manager *mgr;
	struct mgr_priv_data *mp;
	int i;
	int enabled_mgrs;

	enabled_mgrs = 0;

	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->enabled)
			continue;

		enabled_mgrs++;
	}

	return enabled_mgrs;
}

static int get_num_used_overlays(void)
{
	const int num_ovls = omap_dss_get_num_overlays();
	struct omap_overlay *ovl;
	struct ovl_priv_data *op;
	struct mgr_priv_data *mp;
	int i;
	int enabled_ovls;

	enabled_ovls = 0;

	for (i = 0; i < num_ovls; ++i) {
		ovl = omap_dss_get_overlay(i);
		op = get_ovl_priv(ovl);

		if (!op->enabled && !op->enabling)
			continue;

		mp = get_mgr_priv(ovl->manager);

		if (!mp->enabled)
			continue;

		enabled_ovls++;
	}

	return enabled_ovls;
}

static bool get_use_fifo_merge(void)
{
	int enabled_mgrs = get_num_used_managers();
	int enabled_ovls = get_num_used_overlays();

	if (!dss_has_feature(FEAT_FIFO_MERGE))
		return false;

	/*
	 * In theory the only requirement for fifomerge is enabled_ovls <= 1.
	 * However, if we have two managers enabled and set/unset the fifomerge,
	 * we need to set the GO bits in particular sequence for the managers,
	 * and wait in between.
	 *
	 * This is rather difficult as new apply calls can happen at any time,
	 * so we simplify the problem by requiring also that enabled_mgrs <= 1.
	 * In practice this shouldn't matter, because when only one overlay is
	 * enabled, most likely only one output is enabled.
	 */

	return enabled_mgrs <= 1 && enabled_ovls <= 1;
}

int dss_mgr_enable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;
	int r;
	bool fifo_merge;

	mutex_lock(&apply_lock);

	if (mp->enabled)
		goto out;

	spin_lock_irqsave(&data_lock, flags);

	mp->enabled = true;

	r = dss_check_settings(mgr);
	if (r) {
		DSSERR("failed to enable manager %d: check_settings failed\n",
				mgr->id);
		goto err;
	}

	/* step 1: setup fifos/fifomerge before enabling the manager */

	fifo_merge = get_use_fifo_merge();
	dss_setup_fifos(fifo_merge);
	dss_apply_fifo_merge(fifo_merge);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	/* wait until fifo config is in */
	wait_pending_extra_info_updates();

	/* step 2: enable the manager */
	spin_lock_irqsave(&data_lock, flags);

	if (!mgr_manual_update(mgr))
		mp->updating = true;

	spin_unlock_irqrestore(&data_lock, flags);

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, true);

out:
	mutex_unlock(&apply_lock);

	return 0;

err:
	mp->enabled = false;
	spin_unlock_irqrestore(&data_lock, flags);
	mutex_unlock(&apply_lock);
	return r;
}

void dss_mgr_disable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;
	bool fifo_merge;

	mutex_lock(&apply_lock);

	if (!mp->enabled)
		goto out;

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, false);

	spin_lock_irqsave(&data_lock, flags);

	mp->updating = false;
	mp->enabled = false;

	fifo_merge = get_use_fifo_merge();
	dss_setup_fifos(fifo_merge);
	dss_apply_fifo_merge(fifo_merge);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	wait_pending_extra_info_updates();
out:
	mutex_unlock(&apply_lock);
}

int dss_mgr_set_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;
	int r;

	r = dss_mgr_simple_check(mgr, info);
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	mp->user_info = *info;
	mp->user_info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_mgr_get_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = mp->user_info;

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

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

static void dss_apply_mgr_timings(struct omap_overlay_manager *mgr,
		struct omap_video_timings *timings)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);

	mp->timings = *timings;
	mp->extra_info_dirty = true;
}

void dss_mgr_set_timings(struct omap_overlay_manager *mgr,
		struct omap_video_timings *timings)
{
	unsigned long flags;

	mutex_lock(&apply_lock);

	spin_lock_irqsave(&data_lock, flags);

	dss_apply_mgr_timings(mgr, timings);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	wait_pending_extra_info_updates();

	mutex_unlock(&apply_lock);
}

int dss_ovl_set_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	int r;

	r = dss_ovl_simple_check(ovl, info);
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	op->user_info = *info;
	op->user_info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_ovl_get_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = op->user_info;

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

	op->channel = mgr->id;
	op->extra_info_dirty = true;

	ovl->manager = mgr;
	list_add_tail(&ovl->list, &mgr->overlays);

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

	op->channel = -1;

	ovl->manager = NULL;
	list_del(&ovl->list);

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
	bool fifo_merge;
	int r;

	mutex_lock(&apply_lock);

	if (op->enabled) {
		r = 0;
		goto err1;
	}

	if (ovl->manager == NULL || ovl->manager->device == NULL) {
		r = -EINVAL;
		goto err1;
	}

	spin_lock_irqsave(&data_lock, flags);

	op->enabling = true;

	r = dss_check_settings(ovl->manager);
	if (r) {
		DSSERR("failed to enable overlay %d: check_settings failed\n",
				ovl->id);
		goto err2;
	}

	/* step 1: configure fifos/fifomerge for currently enabled ovls */

	fifo_merge = get_use_fifo_merge();
	dss_setup_fifos(fifo_merge);
	dss_apply_fifo_merge(fifo_merge);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	/* wait for fifo configs to go in */
	wait_pending_extra_info_updates();

	/* step 2: enable the overlay */
	spin_lock_irqsave(&data_lock, flags);

	op->enabling = false;
	dss_apply_ovl_enable(ovl, true);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	/* wait for overlay to be enabled */
	wait_pending_extra_info_updates();

	mutex_unlock(&apply_lock);

	return 0;
err2:
	op->enabling = false;
	spin_unlock_irqrestore(&data_lock, flags);
err1:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_ovl_disable(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op = get_ovl_priv(ovl);
	unsigned long flags;
	bool fifo_merge;
	int r;

	mutex_lock(&apply_lock);

	if (!op->enabled) {
		r = 0;
		goto err;
	}

	if (ovl->manager == NULL || ovl->manager->device == NULL) {
		r = -EINVAL;
		goto err;
	}

	/* step 1: disable the overlay */
	spin_lock_irqsave(&data_lock, flags);

	dss_apply_ovl_enable(ovl, false);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	/* wait for the overlay to be disabled */
	wait_pending_extra_info_updates();

	/* step 2: configure fifos/fifomerge */
	spin_lock_irqsave(&data_lock, flags);

	fifo_merge = get_use_fifo_merge();
	dss_setup_fifos(fifo_merge);
	dss_apply_fifo_merge(fifo_merge);

	dss_write_regs();
	dss_set_go_bits();

	spin_unlock_irqrestore(&data_lock, flags);

	/* wait for fifo config to go in */
	wait_pending_extra_info_updates();

	mutex_unlock(&apply_lock);

	return 0;

err:
	mutex_unlock(&apply_lock);
	return r;
}

