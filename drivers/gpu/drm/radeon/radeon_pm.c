/*
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Rafał Miłecki <zajec5@gmail.com>
 *          Alex Deucher <alexdeucher@gmail.com>
 */
#include "drmP.h"
#include "radeon.h"
#include "avivod.h"

#define RADEON_IDLE_LOOP_MS 100
#define RADEON_RECLOCK_DELAY_MS 200
#define RADEON_WAIT_VBLANK_TIMEOUT 200
#define RADEON_WAIT_IDLE_TIMEOUT 200

static void radeon_pm_set_clocks_locked(struct radeon_device *rdev);
static void radeon_pm_set_clocks(struct radeon_device *rdev);
static void radeon_pm_idle_work_handler(struct work_struct *work);
static int radeon_debugfs_pm_init(struct radeon_device *rdev);

static const char *pm_state_names[4] = {
	"PM_STATE_DISABLED",
	"PM_STATE_MINIMUM",
	"PM_STATE_PAUSED",
	"PM_STATE_ACTIVE"
};

static const char *pm_state_types[5] = {
	"Default",
	"Powersave",
	"Battery",
	"Balanced",
	"Performance",
};

static void radeon_print_power_mode_info(struct radeon_device *rdev)
{
	int i, j;
	bool is_default;

	DRM_INFO("%d Power State(s)\n", rdev->pm.num_power_states);
	for (i = 0; i < rdev->pm.num_power_states; i++) {
		if (rdev->pm.default_power_state_index == i)
			is_default = true;
		else
			is_default = false;
		DRM_INFO("State %d %s %s\n", i,
			 pm_state_types[rdev->pm.power_state[i].type],
			 is_default ? "(default)" : "");
		if ((rdev->flags & RADEON_IS_PCIE) && !(rdev->flags & RADEON_IS_IGP))
			DRM_INFO("\t%d PCIE Lanes\n", rdev->pm.power_state[i].non_clock_info.pcie_lanes);
		if (rdev->pm.power_state[i].flags & RADEON_PM_SINGLE_DISPLAY_ONLY)
			DRM_INFO("\tSingle display only\n");
		DRM_INFO("\t%d Clock Mode(s)\n", rdev->pm.power_state[i].num_clock_modes);
		for (j = 0; j < rdev->pm.power_state[i].num_clock_modes; j++) {
			if (rdev->flags & RADEON_IS_IGP)
				DRM_INFO("\t\t%d engine: %d\n",
					 j,
					 rdev->pm.power_state[i].clock_info[j].sclk * 10);
			else
				DRM_INFO("\t\t%d engine/memory: %d/%d\n",
					 j,
					 rdev->pm.power_state[i].clock_info[j].sclk * 10,
					 rdev->pm.power_state[i].clock_info[j].mclk * 10);
		}
	}
}

void radeon_sync_with_vblank(struct radeon_device *rdev)
{
	if (rdev->pm.active_crtcs) {
		rdev->pm.vblank_sync = false;
		wait_event_timeout(
			rdev->irq.vblank_queue, rdev->pm.vblank_sync,
			msecs_to_jiffies(RADEON_WAIT_VBLANK_TIMEOUT));
	}
}

int radeon_pm_init(struct radeon_device *rdev)
{
	rdev->pm.state = PM_STATE_DISABLED;
	rdev->pm.planned_action = PM_ACTION_NONE;
	rdev->pm.can_upclock = true;
	rdev->pm.can_downclock = true;

	if (rdev->bios) {
		if (rdev->is_atom_bios)
			radeon_atombios_get_power_modes(rdev);
		else
			radeon_combios_get_power_modes(rdev);
		radeon_print_power_mode_info(rdev);
	}

	if (radeon_debugfs_pm_init(rdev)) {
		DRM_ERROR("Failed to register debugfs file for PM!\n");
	}

	INIT_DELAYED_WORK(&rdev->pm.idle_work, radeon_pm_idle_work_handler);

	if (radeon_dynpm != -1 && radeon_dynpm) {
		rdev->pm.state = PM_STATE_PAUSED;
		DRM_INFO("radeon: dynamic power management enabled\n");
	}

	DRM_INFO("radeon: power management initialized\n");

	return 0;
}

void radeon_pm_fini(struct radeon_device *rdev)
{
	if (rdev->pm.i2c_bus)
		radeon_i2c_destroy(rdev->pm.i2c_bus);
}

void radeon_pm_compute_clocks(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_crtc *crtc;
	struct radeon_crtc *radeon_crtc;

	if (rdev->pm.state == PM_STATE_DISABLED)
		return;

	mutex_lock(&rdev->pm.mutex);

	rdev->pm.active_crtcs = 0;
	rdev->pm.active_crtc_count = 0;
	list_for_each_entry(crtc,
		&ddev->mode_config.crtc_list, head) {
		radeon_crtc = to_radeon_crtc(crtc);
		if (radeon_crtc->enabled) {
			rdev->pm.active_crtcs |= (1 << radeon_crtc->crtc_id);
			rdev->pm.active_crtc_count++;
		}
	}

	if (rdev->pm.active_crtc_count > 1) {
		if (rdev->pm.state == PM_STATE_ACTIVE) {
			cancel_delayed_work(&rdev->pm.idle_work);

			rdev->pm.state = PM_STATE_PAUSED;
			rdev->pm.planned_action = PM_ACTION_UPCLOCK;
			radeon_pm_set_clocks(rdev);

			DRM_DEBUG("radeon: dynamic power management deactivated\n");
		}
	} else if (rdev->pm.active_crtc_count == 1) {
		/* TODO: Increase clocks if needed for current mode */

		if (rdev->pm.state == PM_STATE_MINIMUM) {
			rdev->pm.state = PM_STATE_ACTIVE;
			rdev->pm.planned_action = PM_ACTION_UPCLOCK;
			radeon_pm_set_clocks(rdev);

			queue_delayed_work(rdev->wq, &rdev->pm.idle_work,
				msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
		} else if (rdev->pm.state == PM_STATE_PAUSED) {
			rdev->pm.state = PM_STATE_ACTIVE;
			queue_delayed_work(rdev->wq, &rdev->pm.idle_work,
				msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
			DRM_DEBUG("radeon: dynamic power management activated\n");
		}
	} else { /* count == 0 */
		if (rdev->pm.state != PM_STATE_MINIMUM) {
			cancel_delayed_work(&rdev->pm.idle_work);

			rdev->pm.state = PM_STATE_MINIMUM;
			rdev->pm.planned_action = PM_ACTION_MINIMUM;
			radeon_pm_set_clocks(rdev);
		}
	}

	mutex_unlock(&rdev->pm.mutex);
}

bool radeon_pm_debug_check_in_vbl(struct radeon_device *rdev, bool finish)
{
	u32 stat_crtc = 0;
	bool in_vbl = true;

	if (ASIC_IS_DCE4(rdev)) {
		if (rdev->pm.active_crtcs & (1 << 0)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC0_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 1)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC1_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 2)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC2_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 3)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC3_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 4)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC4_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 5)) {
			stat_crtc = RREG32(EVERGREEN_CRTC_STATUS + EVERGREEN_CRTC5_REGISTER_OFFSET);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
	} else if (ASIC_IS_AVIVO(rdev)) {
		if (rdev->pm.active_crtcs & (1 << 0)) {
			stat_crtc = RREG32(D1CRTC_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 1)) {
			stat_crtc = RREG32(D2CRTC_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
	} else {
		if (rdev->pm.active_crtcs & (1 << 0)) {
			stat_crtc = RREG32(RADEON_CRTC_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 1)) {
			stat_crtc = RREG32(RADEON_CRTC2_STATUS);
			if (!(stat_crtc & 1))
				in_vbl = false;
		}
	}
	if (in_vbl == false)
		DRM_INFO("not in vbl for pm change %08x at %s\n", stat_crtc,
			 finish ? "exit" : "entry");
	return in_vbl;
}
static void radeon_pm_set_clocks_locked(struct radeon_device *rdev)
{
	/*radeon_fence_wait_last(rdev);*/

	radeon_set_power_state(rdev);
	rdev->pm.planned_action = PM_ACTION_NONE;
}

static void radeon_pm_set_clocks(struct radeon_device *rdev)
{
	int i;

	radeon_get_power_state(rdev, rdev->pm.planned_action);
	mutex_lock(&rdev->cp.mutex);

	/* wait for GPU idle */
	rdev->pm.gui_idle = false;
	rdev->irq.gui_idle = true;
	radeon_irq_set(rdev);
	wait_event_interruptible_timeout(
		rdev->irq.idle_queue, rdev->pm.gui_idle,
		msecs_to_jiffies(RADEON_WAIT_IDLE_TIMEOUT));
	rdev->irq.gui_idle = false;
	radeon_irq_set(rdev);

	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->pm.active_crtcs & (1 << i)) {
			rdev->pm.req_vblank |= (1 << i);
			drm_vblank_get(rdev->ddev, i);
		}
	}
	radeon_pm_set_clocks_locked(rdev);
	for (i = 0; i < rdev->num_crtc; i++) {
		if (rdev->pm.req_vblank & (1 << i)) {
			rdev->pm.req_vblank &= ~(1 << i);
			drm_vblank_put(rdev->ddev, i);
		}
	}

	/* update display watermarks based on new power state */
	radeon_update_bandwidth_info(rdev);
	if (rdev->pm.active_crtc_count)
		radeon_bandwidth_update(rdev);

	mutex_unlock(&rdev->cp.mutex);
}

static void radeon_pm_idle_work_handler(struct work_struct *work)
{
	struct radeon_device *rdev;
	rdev = container_of(work, struct radeon_device,
				pm.idle_work.work);

	mutex_lock(&rdev->pm.mutex);
	if (rdev->pm.state == PM_STATE_ACTIVE) {
		unsigned long irq_flags;
		int not_processed = 0;

		read_lock_irqsave(&rdev->fence_drv.lock, irq_flags);
		if (!list_empty(&rdev->fence_drv.emited)) {
			struct list_head *ptr;
			list_for_each(ptr, &rdev->fence_drv.emited) {
				/* count up to 3, that's enought info */
				if (++not_processed >= 3)
					break;
			}
		}
		read_unlock_irqrestore(&rdev->fence_drv.lock, irq_flags);

		if (not_processed >= 3) { /* should upclock */
			if (rdev->pm.planned_action == PM_ACTION_DOWNCLOCK) {
				rdev->pm.planned_action = PM_ACTION_NONE;
			} else if (rdev->pm.planned_action == PM_ACTION_NONE &&
				   rdev->pm.can_upclock) {
				rdev->pm.planned_action =
					PM_ACTION_UPCLOCK;
				rdev->pm.action_timeout = jiffies +
				msecs_to_jiffies(RADEON_RECLOCK_DELAY_MS);
			}
		} else if (not_processed == 0) { /* should downclock */
			if (rdev->pm.planned_action == PM_ACTION_UPCLOCK) {
				rdev->pm.planned_action = PM_ACTION_NONE;
			} else if (rdev->pm.planned_action == PM_ACTION_NONE &&
				   rdev->pm.can_downclock) {
				rdev->pm.planned_action =
					PM_ACTION_DOWNCLOCK;
				rdev->pm.action_timeout = jiffies +
				msecs_to_jiffies(RADEON_RECLOCK_DELAY_MS);
			}
		}

		if (rdev->pm.planned_action != PM_ACTION_NONE &&
		    jiffies > rdev->pm.action_timeout) {
			radeon_pm_set_clocks(rdev);
		}
	}
	mutex_unlock(&rdev->pm.mutex);

	queue_delayed_work(rdev->wq, &rdev->pm.idle_work,
					msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
}

/*
 * Debugfs info
 */
#if defined(CONFIG_DEBUG_FS)

static int radeon_debugfs_pm_info(struct seq_file *m, void *data)
{
	struct drm_info_node *node = (struct drm_info_node *) m->private;
	struct drm_device *dev = node->minor->dev;
	struct radeon_device *rdev = dev->dev_private;

	seq_printf(m, "state: %s\n", pm_state_names[rdev->pm.state]);
	seq_printf(m, "default engine clock: %u0 kHz\n", rdev->clock.default_sclk);
	seq_printf(m, "current engine clock: %u0 kHz\n", radeon_get_engine_clock(rdev));
	seq_printf(m, "default memory clock: %u0 kHz\n", rdev->clock.default_mclk);
	if (rdev->asic->get_memory_clock)
		seq_printf(m, "current memory clock: %u0 kHz\n", radeon_get_memory_clock(rdev));
	if (rdev->asic->get_pcie_lanes)
		seq_printf(m, "PCIE lanes: %d\n", radeon_get_pcie_lanes(rdev));

	return 0;
}

static struct drm_info_list radeon_pm_info_list[] = {
	{"radeon_pm_info", radeon_debugfs_pm_info, 0, NULL},
};
#endif

static int radeon_debugfs_pm_init(struct radeon_device *rdev)
{
#if defined(CONFIG_DEBUG_FS)
	return radeon_debugfs_add_files(rdev, radeon_pm_info_list, ARRAY_SIZE(radeon_pm_info_list));
#else
	return 0;
#endif
}
