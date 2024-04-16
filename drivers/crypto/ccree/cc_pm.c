// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include "cc_driver.h"
#include "cc_buffer_mgr.h"
#include "cc_request_mgr.h"
#include "cc_sram_mgr.h"
#include "cc_hash.h"
#include "cc_pm.h"
#include "cc_fips.h"

#define POWER_DOWN_ENABLE 0x01
#define POWER_DOWN_DISABLE 0x00

static int cc_pm_suspend(struct device *dev)
{
	struct cc_drvdata *drvdata = dev_get_drvdata(dev);

	dev_dbg(dev, "set HOST_POWER_DOWN_EN\n");
	fini_cc_regs(drvdata);
	cc_iowrite(drvdata, CC_REG(HOST_POWER_DOWN_EN), POWER_DOWN_ENABLE);
	clk_disable_unprepare(drvdata->clk);
	return 0;
}

static int cc_pm_resume(struct device *dev)
{
	int rc;
	struct cc_drvdata *drvdata = dev_get_drvdata(dev);

	dev_dbg(dev, "unset HOST_POWER_DOWN_EN\n");
	/* Enables the device source clk */
	rc = clk_prepare_enable(drvdata->clk);
	if (rc) {
		dev_err(dev, "failed getting clock back on. We're toast.\n");
		return rc;
	}
	/* wait for Cryptocell reset completion */
	if (!cc_wait_for_reset_completion(drvdata)) {
		dev_err(dev, "Cryptocell reset not completed");
		clk_disable_unprepare(drvdata->clk);
		return -EBUSY;
	}

	cc_iowrite(drvdata, CC_REG(HOST_POWER_DOWN_EN), POWER_DOWN_DISABLE);
	rc = init_cc_regs(drvdata);
	if (rc) {
		dev_err(dev, "init_cc_regs (%x)\n", rc);
		clk_disable_unprepare(drvdata->clk);
		return rc;
	}
	/* check if tee fips error occurred during power down */
	cc_tee_handle_fips_error(drvdata);

	cc_init_hash_sram(drvdata);

	return 0;
}

const struct dev_pm_ops ccree_pm = {
	SET_RUNTIME_PM_OPS(cc_pm_suspend, cc_pm_resume, NULL)
};

int cc_pm_get(struct device *dev)
{
	int rc = pm_runtime_get_sync(dev);
	if (rc < 0) {
		pm_runtime_put_noidle(dev);
		return rc;
	}

	return 0;
}

void cc_pm_put_suspend(struct device *dev)
{
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
}
