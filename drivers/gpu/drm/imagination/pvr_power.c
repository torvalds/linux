// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw.h"
#include "pvr_fw_startstop.h"
#include "pvr_power.h"
#include "pvr_queue.h"
#include "pvr_rogue_fwif.h"

#include <drm/drm_drv.h>
#include <drm/drm_managed.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/pwrseq/consumer.h>
#include <linux/reset.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#define POWER_SYNC_TIMEOUT_US (1000000) /* 1s */

#define WATCHDOG_TIME_MS (500)

/**
 * pvr_device_lost() - Mark GPU device as lost
 * @pvr_dev: Target PowerVR device.
 *
 * This will cause the DRM device to be unplugged.
 */
void
pvr_device_lost(struct pvr_device *pvr_dev)
{
	if (!pvr_dev->lost) {
		pvr_dev->lost = true;
		drm_dev_unplug(from_pvr_device(pvr_dev));
	}
}

static int
pvr_power_send_command(struct pvr_device *pvr_dev, struct rogue_fwif_kccb_cmd *pow_cmd)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	u32 slot_nr;
	u32 value;
	int err;

	WRITE_ONCE(*fw_dev->power_sync, 0);

	err = pvr_kccb_send_cmd_powered(pvr_dev, pow_cmd, &slot_nr);
	if (err)
		return err;

	/* Wait for FW to acknowledge. */
	return readl_poll_timeout(pvr_dev->fw_dev.power_sync, value, value != 0, 100,
				  POWER_SYNC_TIMEOUT_US);
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

	return pvr_fw_stop(pvr_dev);
}

static int
pvr_power_fw_enable(struct pvr_device *pvr_dev)
{
	int err;

	err = pvr_fw_start(pvr_dev);
	if (err)
		return err;

	err = pvr_wait_for_fw_boot(pvr_dev);
	if (err) {
		drm_err(from_pvr_device(pvr_dev), "Firmware failed to boot\n");
		pvr_fw_stop(pvr_dev);
		return err;
	}

	queue_delayed_work(pvr_dev->sched_wq, &pvr_dev->watchdog.work,
			   msecs_to_jiffies(WATCHDOG_TIME_MS));

	return 0;
}

bool
pvr_power_is_idle(struct pvr_device *pvr_dev)
{
	/*
	 * FW power state can be out of date if a KCCB command has been submitted but the FW hasn't
	 * started processing it yet. So also check the KCCB status.
	 */
	enum rogue_fwif_pow_state pow_state = READ_ONCE(pvr_dev->fw_dev.fwif_sysdata->pow_state);
	bool kccb_idle = pvr_kccb_is_idle(pvr_dev);

	return (pow_state == ROGUE_FWIF_POW_IDLE) && kccb_idle;
}

static bool
pvr_watchdog_kccb_stalled(struct pvr_device *pvr_dev)
{
	/* Check KCCB commands are progressing. */
	u32 kccb_cmds_executed = pvr_dev->fw_dev.fwif_osdata->kccb_cmds_executed;
	bool kccb_is_idle = pvr_kccb_is_idle(pvr_dev);

	if (pvr_dev->watchdog.old_kccb_cmds_executed == kccb_cmds_executed && !kccb_is_idle) {
		pvr_dev->watchdog.kccb_stall_count++;

		/*
		 * If we have commands pending with no progress for 2 consecutive polls then
		 * consider KCCB command processing stalled.
		 */
		if (pvr_dev->watchdog.kccb_stall_count == 2) {
			pvr_dev->watchdog.kccb_stall_count = 0;
			return true;
		}
	} else if (pvr_dev->watchdog.old_kccb_cmds_executed == kccb_cmds_executed) {
		bool has_active_contexts;

		mutex_lock(&pvr_dev->queues.lock);
		has_active_contexts = list_empty(&pvr_dev->queues.active);
		mutex_unlock(&pvr_dev->queues.lock);

		if (has_active_contexts) {
			/* Send a HEALTH_CHECK command so we can verify FW is still alive. */
			struct rogue_fwif_kccb_cmd health_check_cmd;

			health_check_cmd.cmd_type = ROGUE_FWIF_KCCB_CMD_HEALTH_CHECK;

			pvr_kccb_send_cmd_powered(pvr_dev, &health_check_cmd, NULL);
		}
	} else {
		pvr_dev->watchdog.old_kccb_cmds_executed = kccb_cmds_executed;
		pvr_dev->watchdog.kccb_stall_count = 0;
	}

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

	if (!pvr_dev->fw_dev.booted)
		goto out_pm_runtime_put;

	stalled = pvr_watchdog_kccb_stalled(pvr_dev);

	if (stalled) {
		drm_err(from_pvr_device(pvr_dev), "FW stalled, trying hard reset");

		pvr_power_reset(pvr_dev, true);
		/* Device may be lost at this point. */
	}

out_pm_runtime_put:
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

static int pvr_power_init_manual(struct pvr_device *pvr_dev)
{
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	struct reset_control *reset;

	reset = devm_reset_control_get_optional_exclusive(drm_dev->dev, NULL);
	if (IS_ERR(reset))
		return dev_err_probe(drm_dev->dev, PTR_ERR(reset),
				     "failed to get gpu reset line\n");

	pvr_dev->reset = reset;

	return 0;
}

static int pvr_power_on_sequence_manual(struct pvr_device *pvr_dev)
{
	int err;

	err = clk_prepare_enable(pvr_dev->core_clk);
	if (err)
		return err;

	err = clk_prepare_enable(pvr_dev->sys_clk);
	if (err)
		goto err_core_clk_disable;

	err = clk_prepare_enable(pvr_dev->mem_clk);
	if (err)
		goto err_sys_clk_disable;

	/*
	 * According to the hardware manual, a delay of at least 32 clock
	 * cycles is required between de-asserting the clkgen reset and
	 * de-asserting the GPU reset. Assuming a worst-case scenario with
	 * a very high GPU clock frequency, a delay of 1 microsecond is
	 * sufficient to ensure this requirement is met across all
	 * feasible GPU clock speeds.
	 */
	udelay(1);

	err = reset_control_deassert(pvr_dev->reset);
	if (err)
		goto err_mem_clk_disable;

	return 0;

err_mem_clk_disable:
	clk_disable_unprepare(pvr_dev->mem_clk);

err_sys_clk_disable:
	clk_disable_unprepare(pvr_dev->sys_clk);

err_core_clk_disable:
	clk_disable_unprepare(pvr_dev->core_clk);

	return err;
}

static int pvr_power_off_sequence_manual(struct pvr_device *pvr_dev)
{
	int err;

	err = reset_control_assert(pvr_dev->reset);

	clk_disable_unprepare(pvr_dev->mem_clk);
	clk_disable_unprepare(pvr_dev->sys_clk);
	clk_disable_unprepare(pvr_dev->core_clk);

	return err;
}

const struct pvr_power_sequence_ops pvr_power_sequence_ops_manual = {
	.init = pvr_power_init_manual,
	.power_on = pvr_power_on_sequence_manual,
	.power_off = pvr_power_off_sequence_manual,
};

static int pvr_power_init_pwrseq(struct pvr_device *pvr_dev)
{
	struct device *dev = from_pvr_device(pvr_dev)->dev;

	pvr_dev->pwrseq = devm_pwrseq_get(dev, "gpu-power");
	if (IS_ERR(pvr_dev->pwrseq)) {
		/*
		 * This platform requires a sequencer. If we can't get it, we
		 * must return the error (including -EPROBE_DEFER to wait for
		 * the provider to appear)
		 */
		return dev_err_probe(dev, PTR_ERR(pvr_dev->pwrseq),
				     "Failed to get required power sequencer\n");
	}

	return 0;
}

static int pvr_power_on_sequence_pwrseq(struct pvr_device *pvr_dev)
{
	return pwrseq_power_on(pvr_dev->pwrseq);
}

static int pvr_power_off_sequence_pwrseq(struct pvr_device *pvr_dev)
{
	return pwrseq_power_off(pvr_dev->pwrseq);
}

const struct pvr_power_sequence_ops pvr_power_sequence_ops_pwrseq = {
	.init = pvr_power_init_pwrseq,
	.power_on = pvr_power_on_sequence_pwrseq,
	.power_off = pvr_power_off_sequence_pwrseq,
};

int
pvr_power_device_suspend(struct device *dev)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct drm_device *drm_dev = platform_get_drvdata(plat_dev);
	struct pvr_device *pvr_dev = to_pvr_device(drm_dev);
	int err = 0;
	int idx;

	if (!drm_dev_enter(drm_dev, &idx))
		return -EIO;

	if (pvr_dev->fw_dev.booted) {
		err = pvr_power_fw_disable(pvr_dev, false);
		if (err)
			goto err_drm_dev_exit;
	}

	err = pvr_dev->device_data->pwr_ops->power_off(pvr_dev);

err_drm_dev_exit:
	drm_dev_exit(idx);

	return err;
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

	err = pvr_dev->device_data->pwr_ops->power_on(pvr_dev);
	if (err)
		goto err_drm_dev_exit;

	if (pvr_dev->fw_dev.booted) {
		err = pvr_power_fw_enable(pvr_dev);
		if (err)
			goto err_power_off;
	}

	drm_dev_exit(idx);

	return 0;

err_power_off:
	pvr_dev->device_data->pwr_ops->power_off(pvr_dev);

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

static int
pvr_power_clear_error(struct pvr_device *pvr_dev)
{
	struct device *dev = from_pvr_device(pvr_dev)->dev;
	int err;

	/* Ensure the device state is known and nothing is happening past this point */
	pm_runtime_disable(dev);

	/* Attempt to clear the runtime PM error by setting the current state again */
	if (pm_runtime_status_suspended(dev))
		err = pm_runtime_set_suspended(dev);
	else
		err = pm_runtime_set_active(dev);

	if (err) {
		drm_err(from_pvr_device(pvr_dev),
			"%s: Failed to clear runtime PM error (new error %d)\n",
			__func__, err);
	}

	pm_runtime_enable(dev);

	return err;
}

/**
 * pvr_power_get_clear() - Acquire a power reference, correcting any errors
 * @pvr_dev: Device pointer
 *
 * Attempt to acquire a power reference on the device. If the runtime PM
 * is in error state, attempt to clear the error and retry.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error code returned by pvr_power_get() or the runtime PM API.
 */
static int
pvr_power_get_clear(struct pvr_device *pvr_dev)
{
	int err;

	err = pvr_power_get(pvr_dev);
	if (err == 0)
		return err;

	drm_warn(from_pvr_device(pvr_dev),
		 "%s: pvr_power_get returned error %d, attempting recovery\n",
		 __func__, err);

	err = pvr_power_clear_error(pvr_dev);
	if (err)
		return err;

	return pvr_power_get(pvr_dev);
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
	bool queues_disabled = false;
	int err;

	/*
	 * Take a power reference during the reset. This should prevent any interference with the
	 * power state during reset.
	 */
	WARN_ON(pvr_power_get_clear(pvr_dev));

	down_write(&pvr_dev->reset_sem);

	if (pvr_dev->lost) {
		err = -EIO;
		goto err_up_write;
	}

	/* Disable IRQs for the duration of the reset. */
	disable_irq(pvr_dev->irq);

	do {
		if (hard_reset) {
			pvr_queue_device_pre_reset(pvr_dev);
			queues_disabled = true;
		}

		err = pvr_power_fw_disable(pvr_dev, hard_reset);
		if (!err) {
			if (hard_reset) {
				pvr_dev->fw_dev.booted = false;
				WARN_ON(pvr_power_device_suspend(from_pvr_device(pvr_dev)->dev));

				err = pvr_fw_hard_reset(pvr_dev);
				if (err)
					goto err_device_lost;

				err = pvr_power_device_resume(from_pvr_device(pvr_dev)->dev);
				pvr_dev->fw_dev.booted = true;
				if (err)
					goto err_device_lost;
			} else {
				/* Clear the FW faulted flags. */
				pvr_dev->fw_dev.fwif_sysdata->hwr_state_flags &=
					~(ROGUE_FWIF_HWR_FW_FAULT |
					  ROGUE_FWIF_HWR_RESTART_REQUESTED);
			}

			pvr_fw_irq_clear(pvr_dev);

			err = pvr_power_fw_enable(pvr_dev);
		}

		if (err && hard_reset)
			goto err_device_lost;

		if (err && !hard_reset) {
			drm_err(from_pvr_device(pvr_dev), "FW stalled, trying hard reset");
			hard_reset = true;
		}
	} while (err);

	if (queues_disabled)
		pvr_queue_device_post_reset(pvr_dev);

	enable_irq(pvr_dev->irq);

	up_write(&pvr_dev->reset_sem);

	pvr_power_put(pvr_dev);

	return 0;

err_device_lost:
	drm_err(from_pvr_device(pvr_dev), "GPU device lost");
	pvr_device_lost(pvr_dev);

	/* Leave IRQs disabled if the device is lost. */

	if (queues_disabled)
		pvr_queue_device_post_reset(pvr_dev);

err_up_write:
	up_write(&pvr_dev->reset_sem);

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

int pvr_power_domains_init(struct pvr_device *pvr_dev)
{
	struct device *dev = from_pvr_device(pvr_dev)->dev;

	struct device_link **domain_links __free(kfree) = NULL;
	struct device **domain_devs __free(kfree) = NULL;
	int domain_count;
	int link_count;

	char dev_name[2] = "a";
	int err;
	int i;

	domain_count = of_count_phandle_with_args(dev->of_node, "power-domains",
						  "#power-domain-cells");
	if (domain_count < 0)
		return domain_count;

	if (domain_count <= 1)
		return 0;

	link_count = domain_count + (domain_count - 1);

	domain_devs = kcalloc(domain_count, sizeof(*domain_devs), GFP_KERNEL);
	if (!domain_devs)
		return -ENOMEM;

	domain_links = kcalloc(link_count, sizeof(*domain_links), GFP_KERNEL);
	if (!domain_links)
		return -ENOMEM;

	for (i = 0; i < domain_count; i++) {
		struct device *domain_dev;

		dev_name[0] = 'a' + i;
		domain_dev = dev_pm_domain_attach_by_name(dev, dev_name);
		if (IS_ERR_OR_NULL(domain_dev)) {
			err = domain_dev ? PTR_ERR(domain_dev) : -ENODEV;
			goto err_detach;
		}

		domain_devs[i] = domain_dev;
	}

	for (i = 0; i < domain_count; i++) {
		struct device_link *link;

		link = device_link_add(dev, domain_devs[i], DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
		if (!link) {
			err = -ENODEV;
			goto err_unlink;
		}

		domain_links[i] = link;
	}

	for (i = domain_count; i < link_count; i++) {
		struct device_link *link;

		link = device_link_add(domain_devs[i - domain_count + 1],
				       domain_devs[i - domain_count],
				       DL_FLAG_STATELESS | DL_FLAG_PM_RUNTIME);
		if (!link) {
			err = -ENODEV;
			goto err_unlink;
		}

		domain_links[i] = link;
	}

	pvr_dev->power = (struct pvr_device_power){
		.domain_devs = no_free_ptr(domain_devs),
		.domain_links = no_free_ptr(domain_links),
		.domain_count = domain_count,
	};

	return 0;

err_unlink:
	while (--i >= 0)
		device_link_del(domain_links[i]);

	i = domain_count;

err_detach:
	while (--i >= 0)
		dev_pm_domain_detach(domain_devs[i], true);

	return err;
}

void pvr_power_domains_fini(struct pvr_device *pvr_dev)
{
	const int domain_count = pvr_dev->power.domain_count;

	int i = domain_count + (domain_count - 1);

	while (--i >= 0)
		device_link_del(pvr_dev->power.domain_links[i]);

	i = domain_count;

	while (--i >= 0)
		dev_pm_domain_detach(pvr_dev->power.domain_devs[i], true);

	kfree(pvr_dev->power.domain_links);
	kfree(pvr_dev->power.domain_devs);

	pvr_dev->power = (struct pvr_device_power){ 0 };
}
