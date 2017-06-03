/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#include "ssi_config.h"
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <crypto/ctr.h>
#include <linux/pm_runtime.h>
#include "ssi_driver.h"
#include "ssi_buffer_mgr.h"
#include "ssi_request_mgr.h"
#include "ssi_sram_mgr.h"
#include "ssi_sysfs.h"
#include "ssi_ivgen.h"
#include "ssi_hash.h"
#include "ssi_pm.h"
#include "ssi_pm_ext.h"


#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)

#define POWER_DOWN_ENABLE 0x01
#define POWER_DOWN_DISABLE 0x00


int ssi_power_mgr_runtime_suspend(struct device *dev)
{
	struct ssi_drvdata *drvdata =
		(struct ssi_drvdata *)dev_get_drvdata(dev);
	int rc;

	SSI_LOG_DEBUG("ssi_power_mgr_runtime_suspend: set HOST_POWER_DOWN_EN\n");
	WRITE_REGISTER(drvdata->cc_base + CC_REG_OFFSET(HOST_RGF, HOST_POWER_DOWN_EN), POWER_DOWN_ENABLE);
	rc = ssi_request_mgr_runtime_suspend_queue(drvdata);
	if (rc != 0) {
		SSI_LOG_ERR("ssi_request_mgr_runtime_suspend_queue (%x)\n", rc);
		return rc;
	}
	fini_cc_regs(drvdata);

	/* Specific HW suspend code */
	ssi_pm_ext_hw_suspend(dev);
	return 0;
}

int ssi_power_mgr_runtime_resume(struct device *dev)
{
	int rc;
	struct ssi_drvdata *drvdata =
		(struct ssi_drvdata *)dev_get_drvdata(dev);

	SSI_LOG_DEBUG("ssi_power_mgr_runtime_resume , unset HOST_POWER_DOWN_EN\n");
	WRITE_REGISTER(drvdata->cc_base + CC_REG_OFFSET(HOST_RGF, HOST_POWER_DOWN_EN), POWER_DOWN_DISABLE);
	/* Specific HW resume code */
	ssi_pm_ext_hw_resume(dev);

	rc = init_cc_regs(drvdata, false);
	if (rc !=0) {
		SSI_LOG_ERR("init_cc_regs (%x)\n",rc);
		return rc;
	}

	rc = ssi_request_mgr_runtime_resume_queue(drvdata);
	if (rc !=0) {
		SSI_LOG_ERR("ssi_request_mgr_runtime_resume_queue (%x)\n",rc);
		return rc;
	}

	/* must be after the queue resuming as it uses the HW queue*/
	ssi_hash_init_sram_digest_consts(drvdata);
	
	ssi_ivgen_init_sram_pool(drvdata);
	return 0;
}

int ssi_power_mgr_runtime_get(struct device *dev)
{
	int rc = 0;

	if (ssi_request_mgr_is_queue_runtime_suspend(
				(struct ssi_drvdata *)dev_get_drvdata(dev))) {
		rc = pm_runtime_get_sync(dev);
	} else {
		pm_runtime_get_noresume(dev);
	}
	return rc;
}

int ssi_power_mgr_runtime_put_suspend(struct device *dev)
{
	int rc = 0;

	if (!ssi_request_mgr_is_queue_runtime_suspend(
				(struct ssi_drvdata *)dev_get_drvdata(dev))) {
		pm_runtime_mark_last_busy(dev);
		rc = pm_runtime_put_autosuspend(dev);
	}
	else {
		/* Something wrong happens*/
		BUG();
	}
	return rc;

}

#endif



int ssi_power_mgr_init(struct ssi_drvdata *drvdata)
{
	int rc = 0;
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
	struct platform_device *plat_dev = drvdata->plat_dev;
	/* must be before the enabling to avoid resdundent suspending */
	pm_runtime_set_autosuspend_delay(&plat_dev->dev,SSI_SUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&plat_dev->dev);
	/* activate the PM module */
	rc = pm_runtime_set_active(&plat_dev->dev);
	if (rc != 0)
		return rc;
	/* enable the PM module*/
	pm_runtime_enable(&plat_dev->dev);
#endif
	return rc;
}

void ssi_power_mgr_fini(struct ssi_drvdata *drvdata)
{
#if defined (CONFIG_PM_RUNTIME) || defined (CONFIG_PM_SLEEP)
	struct platform_device *plat_dev = drvdata->plat_dev;

	pm_runtime_disable(&plat_dev->dev);
#endif
}
