#include <linux/kernel.h>
#include <linux/init.h>

#include <plat/common.h>

#include "voltage.h"
#include "vp.h"
#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"

static void vp_latch_vsel(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp = voltdm->vp;
	u32 vpconfig;
	unsigned long uvdc;
	char vsel;

	uvdc = omap_voltage_get_nom_volt(voltdm);
	if (!uvdc) {
		pr_warning("%s: unable to find current voltage for vdd_%s\n",
			__func__, voltdm->name);
		return;
	}

	if (!voltdm->pmic || !voltdm->pmic->uv_to_vsel) {
		pr_warning("%s: PMIC function to convert voltage in uV to"
			" vsel not registered\n", __func__);
		return;
	}

	vsel = voltdm->pmic->uv_to_vsel(uvdc);

	vpconfig = voltdm->read(vp->vpconfig);
	vpconfig &= ~(vp->common->vpconfig_initvoltage_mask |
			vp->common->vpconfig_initvdd);
	vpconfig |= vsel << __ffs(vp->common->vpconfig_initvoltage_mask);
	voltdm->write(vpconfig, vp->vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	voltdm->write((vpconfig | vp->common->vpconfig_initvdd),
		       vp->vpconfig);

	/* Clear initVDD copy trigger bit */
	voltdm->write(vpconfig, vp->vpconfig);
}

/* Generic voltage init functions */
void __init omap_vp_init(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp = voltdm->vp;
	struct omap_vdd_info *vdd = voltdm->vdd;
	u32 vp_val, sys_clk_rate, timeout_val, waittime;

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	vp->enabled = false;

	/* Divide to avoid overflow */
	sys_clk_rate = voltdm->sys_clk.rate / 1000;

	vdd->vp_rt_data.vpconfig_erroroffset =
		(voltdm->pmic->vp_erroroffset <<
		 __ffs(voltdm->vp->common->vpconfig_erroroffset_mask));

	timeout_val = (sys_clk_rate * voltdm->pmic->vp_timeout_us) / 1000;
	vdd->vp_rt_data.vlimitto_timeout = timeout_val;
	vdd->vp_rt_data.vlimitto_vddmin = voltdm->pmic->vp_vddmin;
	vdd->vp_rt_data.vlimitto_vddmax = voltdm->pmic->vp_vddmax;

	waittime = ((voltdm->pmic->step_size / voltdm->pmic->slew_rate) *
		    sys_clk_rate) / 1000;
	vdd->vp_rt_data.vstepmin_smpswaittimemin = waittime;
	vdd->vp_rt_data.vstepmax_smpswaittimemax = waittime;
	vdd->vp_rt_data.vstepmin_stepmin = voltdm->pmic->vp_vstepmin;
	vdd->vp_rt_data.vstepmax_stepmax = voltdm->pmic->vp_vstepmax;

	vp_val = vdd->vp_rt_data.vpconfig_erroroffset |
		(vdd->vp_rt_data.vpconfig_errorgain <<
		 __ffs(vp->common->vpconfig_errorgain_mask)) |
		vp->common->vpconfig_timeouten;
	voltdm->write(vp_val, vp->vpconfig);

	vp_val = ((vdd->vp_rt_data.vstepmin_smpswaittimemin <<
		   vp->common->vstepmin_smpswaittimemin_shift) |
		  (vdd->vp_rt_data.vstepmin_stepmin <<
		   vp->common->vstepmin_stepmin_shift));
	voltdm->write(vp_val, vp->vstepmin);

	vp_val = ((vdd->vp_rt_data.vstepmax_smpswaittimemax <<
		   vp->common->vstepmax_smpswaittimemax_shift) |
		  (vdd->vp_rt_data.vstepmax_stepmax <<
		   vp->common->vstepmax_stepmax_shift));
	voltdm->write(vp_val, vp->vstepmax);

	vp_val = ((vdd->vp_rt_data.vlimitto_vddmax <<
		   vp->common->vlimitto_vddmax_shift) |
		  (vdd->vp_rt_data.vlimitto_vddmin <<
		   vp->common->vlimitto_vddmin_shift) |
		  (vdd->vp_rt_data.vlimitto_timeout <<
		   vp->common->vlimitto_timeout_shift));
	voltdm->write(vp_val, vp->vlimitto);
}

/* VP force update method of voltage scaling */
int omap_vp_forceupdate_scale(struct voltagedomain *voltdm,
			      unsigned long target_volt)
{
	struct omap_vp_instance *vp = voltdm->vp;
	u32 vpconfig;
	u8 target_vsel, current_vsel;
	int ret, timeout = 0;

	ret = omap_vc_pre_scale(voltdm, target_volt, &target_vsel, &current_vsel);
	if (ret)
		return ret;

	/*
	 * Clear all pending TransactionDone interrupt/status. Typical latency
	 * is <3us
	 */
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vp->common->ops->clear_txdone(vp->id);
		if (!vp->common->ops->check_txdone(vp->id))
			break;
		udelay(1);
	}
	if (timeout >= VP_TRANXDONE_TIMEOUT) {
		pr_warning("%s: vdd_%s TRANXDONE timeout exceeded."
			"Voltage change aborted", __func__, voltdm->name);
		return -ETIMEDOUT;
	}

	/* Configure for VP-Force Update */
	vpconfig = voltdm->read(vp->vpconfig);
	vpconfig &= ~(vp->common->vpconfig_initvdd |
			vp->common->vpconfig_forceupdate |
			vp->common->vpconfig_initvoltage_mask);
	vpconfig |= ((target_vsel <<
		      __ffs(vp->common->vpconfig_initvoltage_mask)));
	voltdm->write(vpconfig, vp->vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	vpconfig |= vp->common->vpconfig_initvdd;
	voltdm->write(vpconfig, vp->vpconfig);

	/* Force update of voltage */
	vpconfig |= vp->common->vpconfig_forceupdate;
	voltdm->write(vpconfig, vp->vpconfig);

	/*
	 * Wait for TransactionDone. Typical latency is <200us.
	 * Depends on SMPSWAITTIMEMIN/MAX and voltage change
	 */
	timeout = 0;
	omap_test_timeout(vp->common->ops->check_txdone(vp->id),
			  VP_TRANXDONE_TIMEOUT, timeout);
	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_err("%s: vdd_%s TRANXDONE timeout exceeded."
			"TRANXDONE never got set after the voltage update\n",
			__func__, voltdm->name);

	omap_vc_post_scale(voltdm, target_volt, target_vsel, current_vsel);

	/*
	 * Disable TransactionDone interrupt , clear all status, clear
	 * control registers
	 */
	timeout = 0;
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vp->common->ops->clear_txdone(vp->id);
		if (!vp->common->ops->check_txdone(vp->id))
			break;
		udelay(1);
	}

	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_warning("%s: vdd_%s TRANXDONE timeout exceeded while trying"
			"to clear the TRANXDONE status\n",
			__func__, voltdm->name);

	vpconfig = voltdm->read(vp->vpconfig);
	/* Clear initVDD copy trigger bit */
	vpconfig &= ~vp->common->vpconfig_initvdd;
	voltdm->write(vpconfig, vp->vpconfig);
	/* Clear force bit */
	vpconfig &= ~vp->common->vpconfig_forceupdate;
	voltdm->write(vpconfig, vp->vpconfig);

	return 0;
}

/**
 * omap_vp_get_curr_volt() - API to get the current vp voltage.
 * @voltdm:	pointer to the VDD.
 *
 * This API returns the current voltage for the specified voltage processor
 */
unsigned long omap_vp_get_curr_volt(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp = voltdm->vp;
	u8 curr_vsel;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return 0;
	}

	if (!voltdm->read) {
		pr_err("%s: No read API for reading vdd_%s regs\n",
			__func__, voltdm->name);
		return 0;
	}

	curr_vsel = voltdm->read(vp->voltage);

	if (!voltdm->pmic || !voltdm->pmic->vsel_to_uv) {
		pr_warning("%s: PMIC function to convert vsel to voltage"
			"in uV not registerd\n", __func__);
		return 0;
	}

	return voltdm->pmic->vsel_to_uv(curr_vsel);
}

/**
 * omap_vp_enable() - API to enable a particular VP
 * @voltdm:	pointer to the VDD whose VP is to be enabled.
 *
 * This API enables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_vp_enable(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp;
	u32 vpconfig;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vp = voltdm->vp;
	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	/* If VP is already enabled, do nothing. Return */
	if (vp->enabled)
		return;

	vp_latch_vsel(voltdm);

	/* Enable VP */
	vpconfig = voltdm->read(vp->vpconfig);
	vpconfig |= vp->common->vpconfig_vpenable;
	voltdm->write(vpconfig, vp->vpconfig);
	vp->enabled = true;
}

/**
 * omap_vp_disable() - API to disable a particular VP
 * @voltdm:	pointer to the VDD whose VP is to be disabled.
 *
 * This API disables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_vp_disable(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp;
	u32 vpconfig;
	int timeout;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vp = voltdm->vp;
	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	/* If VP is already disabled, do nothing. Return */
	if (!vp->enabled) {
		pr_warning("%s: Trying to disable VP for vdd_%s when"
			"it is already disabled\n", __func__, voltdm->name);
		return;
	}

	/* Disable VP */
	vpconfig = voltdm->read(vp->vpconfig);
	vpconfig &= ~vp->common->vpconfig_vpenable;
	voltdm->write(vpconfig, vp->vpconfig);

	/*
	 * Wait for VP idle Typical latency is <2us. Maximum latency is ~100us
	 */
	omap_test_timeout((voltdm->read(vp->vstatus)),
			  VP_IDLE_TIMEOUT, timeout);

	if (timeout >= VP_IDLE_TIMEOUT)
		pr_warning("%s: vdd_%s idle timedout\n",
			__func__, voltdm->name);

	vp->enabled = false;

	return;
}
