// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2026 Raspberry Pi */

#include <linux/clk.h>

#include <drm/drm_print.h>

#include "v3d_drv.h"
#include "v3d_regs.h"

static int
v3d_resume_sms(struct v3d_dev *v3d)
{
	if (v3d->ver < V3D_GEN_71)
		return 0;

	V3D_SMS_WRITE(V3D_SMS_TEE_CS, V3D_SMS_CLEAR_POWER_OFF);

	if (wait_for((V3D_GET_FIELD(V3D_SMS_READ(V3D_SMS_TEE_CS),
				    V3D_SMS_STATE) == V3D_SMS_IDLE), 100)) {
		drm_err(&v3d->drm, "Failed to power up SMS\n");
		return -ETIMEDOUT;
	}

	v3d_reset_sms(v3d);

	return 0;
}

static int
v3d_suspend_sms(struct v3d_dev *v3d)
{
	if (v3d->ver < V3D_GEN_71)
		return 0;

	V3D_SMS_WRITE(V3D_SMS_TEE_CS, V3D_SMS_POWER_OFF);

	if (wait_for((V3D_GET_FIELD(V3D_SMS_READ(V3D_SMS_TEE_CS),
				    V3D_SMS_STATE) == V3D_SMS_POWER_OFF_STATE), 100)) {
		drm_err(&v3d->drm, "Failed to power off SMS\n");
		return -ETIMEDOUT;
	}

	return 0;
}

int v3d_power_suspend(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct v3d_dev *v3d = to_v3d_dev(drm);
	int ret;

	v3d_irq_disable(v3d);

	v3d_clean_caches(v3d);

	ret = v3d_suspend_sms(v3d);
	if (ret) {
		v3d_irq_enable(v3d);
		return ret;
	}

	clk_disable_unprepare(v3d->clk);

	return 0;
}

int v3d_power_resume(struct device *dev)
{
	struct drm_device *drm = dev_get_drvdata(dev);
	struct v3d_dev *v3d = to_v3d_dev(drm);
	int ret;

	ret = clk_prepare_enable(v3d->clk);
	if (ret)
		return ret;

	ret = v3d_resume_sms(v3d);
	if (ret) {
		clk_disable_unprepare(v3d->clk);
		return ret;
	}

	v3d_init_hw_state(v3d);
	v3d_mmu_set_page_table(v3d);
	v3d_irq_enable(v3d);

	return 0;
}
