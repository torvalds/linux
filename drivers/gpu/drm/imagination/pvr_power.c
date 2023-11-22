// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_power.h"
#include "pvr_rogue_fwif.h"

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define POWER_SYNC_TIMEOUT_US (1000000) /* 1s */

#define WATCHDOG_TIME_MS (500)

static int
pvr_power_send_command(struct pvr_device *pvr_dev, struct rogue_fwif_kccb_cmd *pow_cmd)
{
	/* TODO: implement */
	return -ENODEV;
}

static int
pvr_power_request_idle(struct pvr_device *pvr_dev)
{
	struct rogue_fwif_kccb_cmd pow_cmd;

	/* Send FORCED_IDLE request to FW. */
	pow_cmd.cmd_type = ROGUE_FWIF_KCCB_CMD_POW;
	pow_cmd.cmd_data.pow_data.pow_type = ROGUE_FWIF_POW_FORCED_IDLE_REQ;
	pow_cmd.cmd_data.pow_data.power_req_data.pow_request_type = ROGUE_FWIF_POWER_FORCE_IDLE;

	return pvr_power_send_command(pvr_dev, &pow_cmd);
}

static int
pvr_power_request_pwr_off(struct pvr_device *pvr_dev)
{
	struct rogue_fwif_kccb_cmd pow_cmd;

	/* Send POW_OFF request to firmware. */
	pow_cmd.cmd_type = ROGUE_FWIF_KCCB_CMD_POW;
	pow_cmd.cmd_data.pow_data.pow_type = ROGUE_FWIF_POW_OFF_REQ;
	pow_cmd.cmd_data.pow_data.power_req_data.forced = true;

	return pvr_power_send_command(pvr_dev, &pow_cmd);
}

static int
pvr_power_fw_disable(struct pvr_device *pvr_dev, bool hard_reset)
{
	if (!hard_reset) {
		int err;

		cancel_delayed_work_sync(&pvr_dev->watchdog.work);

		err = pvr_power_request_idle(pvr_dev);
		if (err)
			return err;

		err = pvr_power_request_pwr_off(pvr_dev);
		if (err)
			return err;
	}

	/* TODO: stop firmware */
	return -ENODEV;
}

static int
pvr_power_fw_enable(struct pvr_device *pvr_dev)
{
	int err;

	/* TODO: start firmware */
	err = -ENODEV;
	if (err)
		return err;

	queue_delayed_work(pvr_dev->sched_wq, &pvr_dev->watchdog.work,
			   msecs_to_jiffies(WATCHDOG_TIME_MS));

	return 0;
}

bool
pvr_power_is_idle(struct pvr_device *pvr_dev)
{
	/* TODO: implement */
	return true;
}

static bool
pvr_watchdog_kccb_stalled(struct pvr_device *pvr_dev)
{
	/* TODO: implement */
	return false;
}

static void
pvr_watchdog_worker(struct work_struct *work)
{
	struct pvr_device *pvr_dev = container_of(work, struct pvr_device,
						  watchdog.work.work);
	bool stalled;

	if (pvr_dev->lost)
		return;

	if (pm_runtime_get_if_in_use(from_pvr_device(pvr_dev)->dev) <= 0)
		goto out_requeue;

	stalled = pvr_watchdog_kccb_stalled(pvr_dev);

	if (stalled) {
		drm_err(from_pvr_device(pvr_dev), "FW stalled, trying hard reset");

		pvr_power_reset(pvr_dev, true);
		/* Device may be lost at this point. */
	}

	pm_runtime_put(from_pvr_device(pvr_dev)->dev);

out_requeue:
	if (!pvr_dev->lost) {
		queue_delayed_work(pvr_dev->sched_wq, &pvr_dev->watchdog.work,
				   msecs_to_jiffies(WATCHDOG_TIME_MS));
	}
}

/**
 * pvr_watchdog_init() - Initialise watchdog for device
 * @pvr_dev: Target PowerVR device.
 *
 * Returns:
 *  * 0 on success, or
 *  * -%ENOMEM on out of memory.
 */
int
pvr_watchdog_init(struct pvr_device *pvr_dev)
{
	INIT_DELAYED_WORK(&pvr_dev->watchdog.work, pvr_watchdog_worker);

	return 0;
}

int
pvr_power_device_suspend(struct device *dev)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	int idx;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	clk_disable_unprepare(pvr_dev->mem_clk);
	clk_disable_unprepare(pvr_dev->sys_clk);
	clk_disable_unprepare(pvr_dev->core_clk);

	drm_dev_exit(idx);

	return 0;
}

int
pvr_power_device_resume(struct device *dev)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	int idx;
	int err;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	err = clk_prepare_enable(pvr_dev->core_clk);
	if (err)
		goto err_drm_dev_exit;

	err = clk_prepare_enable(pvr_dev->sys_clk);
	if (err)
		goto err_core_clk_disable;

	err = clk_prepare_enable(pvr_dev->mem_clk);
	if (err)
		goto err_sys_clk_disable;

	drm_dev_exit(idx);

	return 0;

err_sys_clk_disable:
	clk_disable_unprepare(pvr_dev->sys_clk);

err_core_clk_disable:
	clk_disable_unprepare(pvr_dev->core_clk);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
}

int
pvr_power_device_idle(struct device *dev)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);

	return pvr_power_is_idle(pvr_dev) ? 0 : -EBUSY;
}

/**
 * pvr_power_reset() - Reset the GPU
 * @pvr_dev: Device pointer
 * @hard_reset: %true for hard reset, %false for soft reset
 *
 * If @hard_reset is %false and the FW processor fails to respond during the reset process, this
 * function will attempt a hard reset.
 *
 * If a hard reset fails then the GPU device is reported as lost.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error code returned by pvr_power_get, pvr_power_fw_disable or pvr_power_fw_enable().
 */
int
pvr_power_reset(struct pvr_device *pvr_dev, bool hard_reset)
{
	/* TODO: Implement hard reset. */
	int err;

	/*
	 * Take a power reference during the reset. This should prevent any interference with the
	 * power state during reset.
	 */
	WARN_ON(pvr_power_get(pvr_dev));

	err = pvr_power_fw_disable(pvr_dev, false);
	if (err)
		goto err_power_put;

	err = pvr_power_fw_enable(pvr_dev);

err_power_put:
	pvr_power_put(pvr_dev);

	return err;
}

/**
 * pvr_watchdog_fini() - Shutdown watchdog for device
 * @pvr_dev: Target PowerVR device.
 */
void
pvr_watchdog_fini(struct pvr_device *pvr_dev)
{
	cancel_delayed_work_sync(&pvr_dev->watchdog.work);
}
