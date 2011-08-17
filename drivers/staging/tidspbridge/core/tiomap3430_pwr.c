/*
 * tiomap_pwr.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Implementation of DSP wake/sleep routines.
 *
 * Copyright (C) 2007-2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

#include <plat/dsp.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/dbdefs.h>
#include <dspbridge/drv.h>
#include <dspbridge/io_sm.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/brddefs.h>
#include <dspbridge/dev.h>
#include <dspbridge/io.h>

/* ------------------------------------ Hardware Abstraction Layer */
#include <hw_defs.h>
#include <hw_mmu.h>

#include <dspbridge/pwr.h>

/*  ----------------------------------- Bridge Driver */
#include <dspbridge/dspdeh.h>
#include <dspbridge/wdt.h>

/*  ----------------------------------- specific to this file */
#include "_tiomap.h"
#include "_tiomap_pwr.h"
#include <mach-omap2/prm-regbits-34xx.h>
#include <mach-omap2/cm-regbits-34xx.h>

#define PWRSTST_TIMEOUT          200

/*
 *  ======== handle_constraints_set ========
 *  	Sets new DSP constraint
 */
int handle_constraints_set(struct bridge_dev_context *dev_context,
				  void *pargs)
{
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 *constraint_val;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	constraint_val = (u32 *) (pargs);
	/* Read the target value requested by DSP */
	dev_dbg(bridge, "OPP: %s opp requested = 0x%x\n", __func__,
		(u32) *(constraint_val + 1));

	/* Set the new opp value */
	if (pdata->dsp_set_min_opp)
		(*pdata->dsp_set_min_opp) ((u32) *(constraint_val + 1));
#endif /* #ifdef CONFIG_TIDSPBRIDGE_DVFS */
	return 0;
}

/*
 *  ======== handle_hibernation_from_dsp ========
 *  	Handle Hibernation requested from DSP
 */
int handle_hibernation_from_dsp(struct bridge_dev_context *dev_context)
{
	int status = 0;
#ifdef CONFIG_PM
	u16 timeout = PWRSTST_TIMEOUT / 10;
	u32 pwr_state;
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 opplevel;
	struct io_mgr *hio_mgr;
#endif
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	pwr_state = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
						OMAP_POWERSTATEST_MASK;
	/* Wait for DSP to move into OFF state */
	while ((pwr_state != PWRDM_POWER_OFF) && --timeout) {
		if (msleep_interruptible(10)) {
			pr_err("Waiting for DSP OFF mode interrupted\n");
			return -EPERM;
		}
		pwr_state = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD,
					OMAP2_PM_PWSTST) & OMAP_POWERSTATEST_MASK;
	}
	if (timeout == 0) {
		pr_err("%s: Timed out waiting for DSP off mode\n", __func__);
		status = -ETIMEDOUT;
		return status;
	} else {

		/* Save mailbox settings */
		omap_mbox_save_ctx(dev_context->mbox);

		/* Turn off DSP Peripheral clocks and DSP Load monitor timer */
		status = dsp_clock_disable_all(dev_context->dsp_per_clks);

		/* Disable wdt on hibernation. */
		dsp_wdt_enable(false);

		if (!status) {
			/* Update the Bridger Driver state */
			dev_context->brd_state = BRD_DSP_HIBERNATION;
#ifdef CONFIG_TIDSPBRIDGE_DVFS
			status =
			    dev_get_io_mgr(dev_context->dev_obj, &hio_mgr);
			if (!hio_mgr) {
				status = DSP_EHANDLE;
				return status;
			}
			io_sh_msetting(hio_mgr, SHM_GETOPP, &opplevel);

			/*
			 * Set the OPP to low level before moving to OFF
			 * mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp) (VDD1_OPP1);
			status = 0;
#endif /* CONFIG_TIDSPBRIDGE_DVFS */
		}
	}
#endif
	return status;
}

/*
 *  ======== sleep_dsp ========
 *  	Put DSP in low power consuming state.
 */
int sleep_dsp(struct bridge_dev_context *dev_context, u32 dw_cmd,
		     void *pargs)
{
	int status = 0;
#ifdef CONFIG_PM
#ifdef CONFIG_TIDSPBRIDGE_NTFY_PWRERR
	struct deh_mgr *hdeh_mgr;
#endif /* CONFIG_TIDSPBRIDGE_NTFY_PWRERR */
	u16 timeout = PWRSTST_TIMEOUT / 10;
	u32 pwr_state, target_pwr_state;
	struct omap_dsp_platform_data *pdata =
		omap_dspbridge_dev->dev.platform_data;

	/* Check if sleep code is valid */
	if ((dw_cmd != PWR_DEEPSLEEP) && (dw_cmd != PWR_EMERGENCYDEEPSLEEP))
		return -EINVAL;

	switch (dev_context->brd_state) {
	case BRD_RUNNING:
		omap_mbox_save_ctx(dev_context->mbox);
		if (dsp_test_sleepstate == PWRDM_POWER_OFF) {
			sm_interrupt_dsp(dev_context, MBX_PM_DSPHIBERNATE);
			dev_dbg(bridge, "PM: %s - sent hibernate cmd to DSP\n",
				__func__);
			target_pwr_state = PWRDM_POWER_OFF;
		} else {
			sm_interrupt_dsp(dev_context, MBX_PM_DSPRETENTION);
			target_pwr_state = PWRDM_POWER_RET;
		}
		break;
	case BRD_RETENTION:
		omap_mbox_save_ctx(dev_context->mbox);
		if (dsp_test_sleepstate == PWRDM_POWER_OFF) {
			sm_interrupt_dsp(dev_context, MBX_PM_DSPHIBERNATE);
			target_pwr_state = PWRDM_POWER_OFF;
		} else
			return 0;
		break;
	case BRD_HIBERNATION:
	case BRD_DSP_HIBERNATION:
		/* Already in Hibernation, so just return */
		dev_dbg(bridge, "PM: %s - DSP already in hibernation\n",
			__func__);
		return 0;
	case BRD_STOPPED:
		dev_dbg(bridge, "PM: %s - Board in STOP state\n", __func__);
		return 0;
	default:
		dev_dbg(bridge, "PM: %s - Bridge in Illegal state\n", __func__);
		return -EPERM;
	}

	/* Get the PRCM DSP power domain status */
	pwr_state = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD, OMAP2_PM_PWSTST) &
						OMAP_POWERSTATEST_MASK;

	/* Wait for DSP to move into target power state */
	while ((pwr_state != target_pwr_state) && --timeout) {
		if (msleep_interruptible(10)) {
			pr_err("Waiting for DSP to Suspend interrupted\n");
			return -EPERM;
		}
		pwr_state = (*pdata->dsp_prm_read)(OMAP3430_IVA2_MOD,
					OMAP2_PM_PWSTST) & OMAP_POWERSTATEST_MASK;
	}

	if (!timeout) {
		pr_err("%s: Timed out waiting for DSP off mode, state %x\n",
		       __func__, pwr_state);
#ifdef CONFIG_TIDSPBRIDGE_NTFY_PWRERR
		dev_get_deh_mgr(dev_context->dev_obj, &hdeh_mgr);
		bridge_deh_notify(hdeh_mgr, DSP_PWRERROR, 0);
#endif /* CONFIG_TIDSPBRIDGE_NTFY_PWRERR */
		return -ETIMEDOUT;
	} else {
		/* Update the Bridger Driver state */
		if (dsp_test_sleepstate == PWRDM_POWER_OFF)
			dev_context->brd_state = BRD_HIBERNATION;
		else
			dev_context->brd_state = BRD_RETENTION;

		/* Disable wdt on hibernation. */
		dsp_wdt_enable(false);

		/* Turn off DSP Peripheral clocks */
		status = dsp_clock_disable_all(dev_context->dsp_per_clks);
		if (status)
			return status;
#ifdef CONFIG_TIDSPBRIDGE_DVFS
		else if (target_pwr_state == PWRDM_POWER_OFF) {
			/*
			 * Set the OPP to low level before moving to OFF mode
			 */
			if (pdata->dsp_set_min_opp)
				(*pdata->dsp_set_min_opp) (VDD1_OPP1);
		}
#endif /* CONFIG_TIDSPBRIDGE_DVFS */
	}
#endif /* CONFIG_PM */
	return status;
}

/*
 *  ======== wake_dsp ========
 *  	Wake up DSP from sleep.
 */
int wake_dsp(struct bridge_dev_context *dev_context, void *pargs)
{
	int status = 0;
#ifdef CONFIG_PM

	/* Check the board state, if it is not 'SLEEP' then return */
	if (dev_context->brd_state == BRD_RUNNING ||
	    dev_context->brd_state == BRD_STOPPED) {
		/* The Device is in 'RET' or 'OFF' state and Bridge state is not
		 * 'SLEEP', this means state inconsistency, so return */
		return 0;
	}

	/* Send a wakeup message to DSP */
	sm_interrupt_dsp(dev_context, MBX_PM_DSPWAKEUP);

	/* Set the device state to RUNNIG */
	dev_context->brd_state = BRD_RUNNING;
#endif /* CONFIG_PM */
	return status;
}

/*
 *  ======== dsp_peripheral_clk_ctrl ========
 *  	Enable/Disable the DSP peripheral clocks as needed..
 */
int dsp_peripheral_clk_ctrl(struct bridge_dev_context *dev_context,
				   void *pargs)
{
	u32 ext_clk = 0;
	u32 ext_clk_id = 0;
	u32 ext_clk_cmd = 0;
	u32 clk_id_index = MBX_PM_MAX_RESOURCES;
	u32 tmp_index;
	u32 dsp_per_clks_before;
	int status = 0;

	dsp_per_clks_before = dev_context->dsp_per_clks;

	ext_clk = (u32) *((u32 *) pargs);
	ext_clk_id = ext_clk & MBX_PM_CLK_IDMASK;

	/* process the power message -- TODO, keep it in a separate function */
	for (tmp_index = 0; tmp_index < MBX_PM_MAX_RESOURCES; tmp_index++) {
		if (ext_clk_id == bpwr_clkid[tmp_index]) {
			clk_id_index = tmp_index;
			break;
		}
	}
	/* TODO -- Assert may be a too hard restriction here.. May be we should
	 * just return with failure when the CLK ID does not match */
	/* DBC_ASSERT(clk_id_index < MBX_PM_MAX_RESOURCES); */
	if (clk_id_index == MBX_PM_MAX_RESOURCES) {
		/* return with a more meaningfull error code */
		return -EPERM;
	}
	ext_clk_cmd = (ext_clk >> MBX_PM_CLK_CMDSHIFT) & MBX_PM_CLK_CMDMASK;
	switch (ext_clk_cmd) {
	case BPWR_DISABLE_CLOCK:
		status = dsp_clk_disable(bpwr_clks[clk_id_index].clk);
		dsp_clk_wakeup_event_ctrl(bpwr_clks[clk_id_index].clk_id,
					  false);
		if (!status) {
			(dev_context->dsp_per_clks) &=
				(~((u32) (1 << bpwr_clks[clk_id_index].clk)));
		}
		break;
	case BPWR_ENABLE_CLOCK:
		status = dsp_clk_enable(bpwr_clks[clk_id_index].clk);
		dsp_clk_wakeup_event_ctrl(bpwr_clks[clk_id_index].clk_id, true);
		if (!status)
			(dev_context->dsp_per_clks) |=
				(1 << bpwr_clks[clk_id_index].clk);
		break;
	default:
		dev_dbg(bridge, "%s: Unsupported CMD\n", __func__);
		/* unsupported cmd */
		/* TODO -- provide support for AUTOIDLE Enable/Disable
		 * commands */
	}
	return status;
}

/*
 *  ========pre_scale_dsp========
 *  Sends prescale notification to DSP
 *
 */
int pre_scale_dsp(struct bridge_dev_context *dev_context, void *pargs)
{
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 level;
	u32 voltage_domain;

	voltage_domain = *((u32 *) pargs);
	level = *((u32 *) pargs + 1);

	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
		__func__, voltage_domain, level);
	if ((dev_context->brd_state == BRD_HIBERNATION) ||
	    (dev_context->brd_state == BRD_RETENTION) ||
	    (dev_context->brd_state == BRD_DSP_HIBERNATION)) {
		dev_dbg(bridge, "OPP: %s IVA in sleep. No message to DSP\n");
		return 0;
	} else if ((dev_context->brd_state == BRD_RUNNING)) {
		/* Send a prenotificatio to DSP */
		dev_dbg(bridge, "OPP: %s sent notification to DSP\n", __func__);
		sm_interrupt_dsp(dev_context, MBX_PM_SETPOINT_PRENOTIFY);
		return 0;
	} else {
		return -EPERM;
	}
#endif /* #ifdef CONFIG_TIDSPBRIDGE_DVFS */
	return 0;
}

/*
 *  ========post_scale_dsp========
 *  Sends postscale notification to DSP
 *
 */
int post_scale_dsp(struct bridge_dev_context *dev_context,
							void *pargs)
{
	int status = 0;
#ifdef CONFIG_TIDSPBRIDGE_DVFS
	u32 level;
	u32 voltage_domain;
	struct io_mgr *hio_mgr;

	status = dev_get_io_mgr(dev_context->dev_obj, &hio_mgr);
	if (!hio_mgr)
		return -EFAULT;

	voltage_domain = *((u32 *) pargs);
	level = *((u32 *) pargs + 1);
	dev_dbg(bridge, "OPP: %s voltage_domain = %x, level = 0x%x\n",
		__func__, voltage_domain, level);
	if ((dev_context->brd_state == BRD_HIBERNATION) ||
	    (dev_context->brd_state == BRD_RETENTION) ||
	    (dev_context->brd_state == BRD_DSP_HIBERNATION)) {
		/* Update the OPP value in shared memory */
		io_sh_msetting(hio_mgr, SHM_CURROPP, &level);
		dev_dbg(bridge, "OPP: %s IVA in sleep. Wrote to shm\n",
			__func__);
	} else if ((dev_context->brd_state == BRD_RUNNING)) {
		/* Update the OPP value in shared memory */
		io_sh_msetting(hio_mgr, SHM_CURROPP, &level);
		/* Send a post notification to DSP */
		sm_interrupt_dsp(dev_context, MBX_PM_SETPOINT_POSTNOTIFY);
		dev_dbg(bridge, "OPP: %s wrote to shm. Sent post notification "
			"to DSP\n", __func__);
	} else {
		status = -EPERM;
	}
#endif /* #ifdef CONFIG_TIDSPBRIDGE_DVFS */
	return status;
}

void dsp_clk_wakeup_event_ctrl(u32 clock_id, bool enable)
{
	struct cfg_hostres *resources;
	int status = 0;
	u32 iva2_grpsel;
	u32 mpu_grpsel;
	struct dev_object *hdev_object = NULL;
	struct bridge_dev_context *bridge_context = NULL;

	hdev_object = (struct dev_object *)drv_get_first_dev_object();
	if (!hdev_object)
		return;

	status = dev_get_bridge_context(hdev_object, &bridge_context);
	if (!bridge_context)
		return;

	resources = bridge_context->resources;
	if (!resources)
		return;

	switch (clock_id) {
	case BPWR_GP_TIMER5:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT5_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT5_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT5_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT5_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_GP_TIMER6:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT6_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT6_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT6_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT6_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_GP_TIMER7:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT7_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT7_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT7_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT7_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_GP_TIMER8:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_GPT8_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_GPT8_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_GPT8_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_GPT8_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_MCBSP1:
		iva2_grpsel = readl(resources->core_pm_base + 0xA8);
		mpu_grpsel = readl(resources->core_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP1_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP1_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP1_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP1_MASK;
		}
		writel(iva2_grpsel, resources->core_pm_base + 0xA8);
		writel(mpu_grpsel, resources->core_pm_base + 0xA4);
		break;
	case BPWR_MCBSP2:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP2_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP2_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP2_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP2_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_MCBSP3:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP3_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP3_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP3_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP3_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_MCBSP4:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP4_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP4_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP4_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP4_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	case BPWR_MCBSP5:
		iva2_grpsel = readl(resources->per_pm_base + 0xA8);
		mpu_grpsel = readl(resources->per_pm_base + 0xA4);
		if (enable) {
			iva2_grpsel |= OMAP3430_GRPSEL_MCBSP5_MASK;
			mpu_grpsel &= ~OMAP3430_GRPSEL_MCBSP5_MASK;
		} else {
			mpu_grpsel |= OMAP3430_GRPSEL_MCBSP5_MASK;
			iva2_grpsel &= ~OMAP3430_GRPSEL_MCBSP5_MASK;
		}
		writel(iva2_grpsel, resources->per_pm_base + 0xA8);
		writel(mpu_grpsel, resources->per_pm_base + 0xA4);
		break;
	}
}
