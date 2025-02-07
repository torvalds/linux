// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/pm_runtime.h>

#include "iris_firmware.h"
#include "iris_core.h"
#include "iris_hfi_common.h"
#include "iris_vpu_common.h"

int iris_hfi_core_init(struct iris_core *core)
{
	const struct iris_hfi_command_ops *hfi_ops = core->hfi_ops;
	int ret;

	ret = hfi_ops->sys_init(core);
	if (ret)
		return ret;

	ret = hfi_ops->sys_image_version(core);
	if (ret)
		return ret;

	return hfi_ops->sys_interframe_powercollapse(core);
}

irqreturn_t iris_hfi_isr(int irq, void *data)
{
	disable_irq_nosync(irq);

	return IRQ_WAKE_THREAD;
}

irqreturn_t iris_hfi_isr_handler(int irq, void *data)
{
	struct iris_core *core = data;

	if (!core)
		return IRQ_NONE;

	mutex_lock(&core->lock);
	pm_runtime_mark_last_busy(core->dev);
	iris_vpu_clear_interrupt(core);
	mutex_unlock(&core->lock);

	core->hfi_response_ops->hfi_response_handler(core);

	if (!iris_vpu_watchdog(core, core->intr_status))
		enable_irq(irq);

	return IRQ_HANDLED;
}

int iris_hfi_pm_suspend(struct iris_core *core)
{
	int ret;

	ret = iris_vpu_prepare_pc(core);
	if (ret) {
		pm_runtime_mark_last_busy(core->dev);
		ret = -EAGAIN;
		goto error;
	}

	ret = iris_set_hw_state(core, false);
	if (ret)
		goto error;

	iris_vpu_power_off(core);

	return 0;

error:
	dev_err(core->dev, "failed to suspend\n");

	return ret;
}

int iris_hfi_pm_resume(struct iris_core *core)
{
	const struct iris_hfi_command_ops *ops = core->hfi_ops;
	int ret;

	ret = iris_vpu_power_on(core);
	if (ret)
		goto error;

	ret = iris_set_hw_state(core, true);
	if (ret)
		goto err_power_off;

	ret = iris_vpu_boot_firmware(core);
	if (ret)
		goto err_suspend_hw;

	ret = ops->sys_interframe_powercollapse(core);
	if (ret)
		goto err_suspend_hw;

	return 0;

err_suspend_hw:
	iris_set_hw_state(core, false);
err_power_off:
	iris_vpu_power_off(core);
error:
	dev_err(core->dev, "failed to resume\n");

	return -EBUSY;
}
