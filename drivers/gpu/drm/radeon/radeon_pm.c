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
		if (rdev->pm.default_power_state == &rdev->pm.power_state[i])
			is_default = true;
		else
			is_default = false;
		DRM_INFO("State %d %s %s\n", i,
			 pm_state_types[rdev->pm.power_state[i].type],
			 is_default ? "(default)" : "");
		if ((rdev->flags & RADEON_IS_PCIE) && !(rdev->flags & RADEON_IS_IGP))
			DRM_INFO("\t%d PCIE Lanes\n", rdev->pm.power_state[i].non_clock_info.pcie_lanes);
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

static struct radeon_power_state * radeon_pick_power_state(struct radeon_device *rdev,
							   enum radeon_pm_state_type type)
{
	int i, j;
	enum radeon_pm_state_type wanted_types[2];
	int wanted_count;

	switch (type) {
	case POWER_STATE_TYPE_DEFAULT:
	default:
		return rdev->pm.default_power_state;
	case POWER_STATE_TYPE_POWERSAVE:
		if (rdev->flags & RADEON_IS_MOBILITY) {
			wanted_types[0] = POWER_STATE_TYPE_POWERSAVE;
			wanted_types[1] = POWER_STATE_TYPE_BATTERY;
			wanted_count = 2;
		} else {
			wanted_types[0] = POWER_STATE_TYPE_PERFORMANCE;
			wanted_count = 1;
		}
		break;
	case POWER_STATE_TYPE_BATTERY:
		if (rdev->flags & RADEON_IS_MOBILITY) {
			wanted_types[0] = POWER_STATE_TYPE_BATTERY;
			wanted_types[1] = POWER_STATE_TYPE_POWERSAVE;
			wanted_count = 2;
		} else {
			wanted_types[0] = POWER_STATE_TYPE_PERFORMANCE;
			wanted_count = 1;
		}
		break;
	case POWER_STATE_TYPE_BALANCED:
	case POWER_STATE_TYPE_PERFORMANCE:
		wanted_types[0] = type;
		wanted_count = 1;
		break;
	}

	for (i = 0; i < wanted_count; i++) {
		for (j = 0; j < rdev->pm.num_power_states; j++) {
			if (rdev->pm.power_state[j].type == wanted_types[i])
				return &rdev->pm.power_state[j];
		}
	}

	return rdev->pm.default_power_state;
}

static struct radeon_pm_clock_info * radeon_pick_clock_mode(struct radeon_device *rdev,
							    struct radeon_power_state *power_state,
							    enum radeon_pm_clock_mode_type type)
{
	switch (type) {
	case POWER_MODE_TYPE_DEFAULT:
	default:
		return power_state->default_clock_mode;
	case POWER_MODE_TYPE_LOW:
		return &power_state->clock_info[0];
	case POWER_MODE_TYPE_MID:
		if (power_state->num_clock_modes > 2)
			return &power_state->clock_info[1];
		else
			return &power_state->clock_info[0];
		break;
	case POWER_MODE_TYPE_HIGH:
		return &power_state->clock_info[power_state->num_clock_modes - 1];
	}

}

static void radeon_get_power_state(struct radeon_device *rdev,
				   enum radeon_pm_action action)
{
	switch (action) {
	case PM_ACTION_MINIMUM:
		rdev->pm.requested_power_state = radeon_pick_power_state(rdev, POWER_STATE_TYPE_BATTERY);
		rdev->pm.requested_clock_mode =
			radeon_pick_clock_mode(rdev, rdev->pm.requested_power_state, POWER_MODE_TYPE_LOW);
		break;
	case PM_ACTION_DOWNCLOCK:
		rdev->pm.requested_power_state = radeon_pick_power_state(rdev, POWER_STATE_TYPE_POWERSAVE);
		rdev->pm.requested_clock_mode =
			radeon_pick_clock_mode(rdev, rdev->pm.requested_power_state, POWER_MODE_TYPE_MID);
		break;
	case PM_ACTION_UPCLOCK:
		rdev->pm.requested_power_state = radeon_pick_power_state(rdev, POWER_STATE_TYPE_DEFAULT);
		rdev->pm.requested_clock_mode =
			radeon_pick_clock_mode(rdev, rdev->pm.requested_power_state, POWER_MODE_TYPE_HIGH);
		break;
	case PM_ACTION_NONE:
	default:
		DRM_ERROR("Requested mode for not defined action\n");
		return;
	}
	DRM_INFO("Requested: e: %d m: %d p: %d\n",
		 rdev->pm.requested_clock_mode->sclk,
		 rdev->pm.requested_clock_mode->mclk,
		 rdev->pm.requested_power_state->non_clock_info.pcie_lanes);
}

static void radeon_set_power_state(struct radeon_device *rdev)
{
	/* if *_clock_mode are the same, *_power_state are as well */
	if (rdev->pm.requested_clock_mode == rdev->pm.current_clock_mode)
		return;

	DRM_INFO("Setting: e: %d m: %d p: %d\n",
		 rdev->pm.requested_clock_mode->sclk,
		 rdev->pm.requested_clock_mode->mclk,
		 rdev->pm.requested_power_state->non_clock_info.pcie_lanes);
	/* set pcie lanes */
	/* set voltage */
	/* set engine clock */
	radeon_set_engine_clock(rdev, rdev->pm.requested_clock_mode->sclk);
	/* set memory clock */

	rdev->pm.current_power_state = rdev->pm.requested_power_state;
	rdev->pm.current_clock_mode = rdev->pm.requested_clock_mode;
}

int radeon_pm_init(struct radeon_device *rdev)
{
	rdev->pm.state = PM_STATE_DISABLED;
	rdev->pm.planned_action = PM_ACTION_NONE;
	rdev->pm.downclocked = false;

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

void radeon_pm_compute_clocks(struct radeon_device *rdev)
{
	struct drm_device *ddev = rdev->ddev;
	struct drm_connector *connector;
	struct radeon_crtc *radeon_crtc;
	int count = 0;

	if (rdev->pm.state == PM_STATE_DISABLED)
		return;

	mutex_lock(&rdev->pm.mutex);

	rdev->pm.active_crtcs = 0;
	list_for_each_entry(connector,
		&ddev->mode_config.connector_list, head) {
		if (connector->encoder &&
			connector->dpms != DRM_MODE_DPMS_OFF) {
			radeon_crtc = to_radeon_crtc(connector->encoder->crtc);
			rdev->pm.active_crtcs |= (1 << radeon_crtc->crtc_id);
			++count;
		}
	}

	if (count > 1) {
		if (rdev->pm.state == PM_STATE_ACTIVE) {
			cancel_delayed_work(&rdev->pm.idle_work);

			rdev->pm.state = PM_STATE_PAUSED;
			rdev->pm.planned_action = PM_ACTION_UPCLOCK;
			if (rdev->pm.downclocked)
				radeon_pm_set_clocks(rdev);

			DRM_DEBUG("radeon: dynamic power management deactivated\n");
		}
	} else if (count == 1) {
		/* TODO: Increase clocks if needed for current mode */

		if (rdev->pm.state == PM_STATE_MINIMUM) {
			rdev->pm.state = PM_STATE_ACTIVE;
			rdev->pm.planned_action = PM_ACTION_UPCLOCK;
			radeon_pm_set_clocks(rdev);

			queue_delayed_work(rdev->wq, &rdev->pm.idle_work,
				msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
		}
		else if (rdev->pm.state == PM_STATE_PAUSED) {
			rdev->pm.state = PM_STATE_ACTIVE;
			queue_delayed_work(rdev->wq, &rdev->pm.idle_work,
				msecs_to_jiffies(RADEON_IDLE_LOOP_MS));
			DRM_DEBUG("radeon: dynamic power management activated\n");
		}
	}
	else { /* count == 0 */
		if (rdev->pm.state != PM_STATE_MINIMUM) {
			cancel_delayed_work(&rdev->pm.idle_work);

			rdev->pm.state = PM_STATE_MINIMUM;
			rdev->pm.planned_action = PM_ACTION_MINIMUM;
			radeon_pm_set_clocks(rdev);
		}
	}

	mutex_unlock(&rdev->pm.mutex);
}

static bool radeon_pm_debug_check_in_vbl(struct radeon_device *rdev, bool finish)
{
	u32 stat_crtc1 = 0, stat_crtc2 = 0;
	bool in_vbl = true;

	if (ASIC_IS_AVIVO(rdev)) {
		if (rdev->pm.active_crtcs & (1 << 0)) {
			stat_crtc1 = RREG32(D1CRTC_STATUS);
			if (!(stat_crtc1 & 1))
				in_vbl = false;
		}
		if (rdev->pm.active_crtcs & (1 << 1)) {
			stat_crtc2 = RREG32(D2CRTC_STATUS);
			if (!(stat_crtc2 & 1))
				in_vbl = false;
		}
	}
	if (in_vbl == false)
		DRM_INFO("not in vbl for pm change %08x %08x at %s\n", stat_crtc1,
			 stat_crtc2, finish ? "exit" : "entry");
	return in_vbl;
}
static void radeon_pm_set_clocks_locked(struct radeon_device *rdev)
{
	/*radeon_fence_wait_last(rdev);*/
	switch (rdev->pm.planned_action) {
	case PM_ACTION_UPCLOCK:
		rdev->pm.downclocked = false;
		break;
	case PM_ACTION_DOWNCLOCK:
		rdev->pm.downclocked = true;
		break;
	case PM_ACTION_MINIMUM:
		break;
	case PM_ACTION_NONE:
		DRM_ERROR("%s: PM_ACTION_NONE\n", __func__);
		break;
	}

	/* check if we are in vblank */
	radeon_pm_debug_check_in_vbl(rdev, false);
	radeon_set_power_state(rdev);
	radeon_pm_debug_check_in_vbl(rdev, true);
	rdev->pm.planned_action = PM_ACTION_NONE;
}

static void radeon_pm_set_clocks(struct radeon_device *rdev)
{
	radeon_get_power_state(rdev, rdev->pm.planned_action);
	mutex_lock(&rdev->cp.mutex);

	if (rdev->pm.active_crtcs & (1 << 0)) {
		rdev->pm.req_vblank |= (1 << 0);
		drm_vblank_get(rdev->ddev, 0);
	}
	if (rdev->pm.active_crtcs & (1 << 1)) {
		rdev->pm.req_vblank |= (1 << 1);
		drm_vblank_get(rdev->ddev, 1);
	}
	if (rdev->pm.active_crtcs)
		wait_event_interruptible_timeout(
			rdev->irq.vblank_queue, 0,
			msecs_to_jiffies(RADEON_WAIT_VBLANK_TIMEOUT));
	if (rdev->pm.req_vblank & (1 << 0)) {
		rdev->pm.req_vblank &= ~(1 << 0);
		drm_vblank_put(rdev->ddev, 0);
	}
	if (rdev->pm.req_vblank & (1 << 1)) {
		rdev->pm.req_vblank &= ~(1 << 1);
		drm_vblank_put(rdev->ddev, 1);
	}

	radeon_pm_set_clocks_locked(rdev);
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
				rdev->pm.downclocked) {
				rdev->pm.planned_action =
					PM_ACTION_UPCLOCK;
				rdev->pm.action_timeout = jiffies +
				msecs_to_jiffies(RADEON_RECLOCK_DELAY_MS);
			}
		} else if (not_processed == 0) { /* should downclock */
			if (rdev->pm.planned_action == PM_ACTION_UPCLOCK) {
				rdev->pm.planned_action = PM_ACTION_NONE;
			} else if (rdev->pm.planned_action == PM_ACTION_NONE &&
				!rdev->pm.downclocked) {
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
