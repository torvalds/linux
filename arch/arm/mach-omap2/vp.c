#include <linux/kernel.h>
#include <linux/init.h>

#include "common.h"

#include "voltage.h"
#include "vp.h"
#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"

static u32 _vp_set_init_voltage(struct voltagedomain *voltdm, u32 volt)
{
	struct omap_vp_instance *vp = voltdm->vp;
	u32 vpconfig;
	char vsel;

	vsel = voltdm->pmic->uv_to_vsel(volt);

	vpconfig = voltdm->read(vp->vpconfig);
	vpconfig &= ~(vp->common->vpconfig_initvoltage_mask |
		      vp->common->vpconfig_forceupdate |
		      vp->common->vpconfig_initvdd);
	vpconfig |= vsel << __ffs(vp->common->vpconfig_initvoltage_mask);
	voltdm->write(vpconfig, vp->vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	voltdm->write((vpconfig | vp->common->vpconfig_initvdd),
		       vp->vpconfig);

	/* Clear initVDD copy trigger bit */
	voltdm->write(vpconfig, vp->vpconfig);

	return vpconfig;
}

/* Generic voltage init functions */
void __init omap_vp_init(struct voltagedomain *voltdm)
{
	struct omap_vp_instance *vp = voltdm->vp;
	u32 val, sys_clk_rate, timeout, waittime;
	u32 vddmin, vddmax, vstepmin, vstepmax;

	if (!voltdm->pmic || !voltdm->pmic->uv_to_vsel) {
		pr_err("%s: No PMIC info for vdd_%s\n", __func__, voltdm->name);
		return;
	}

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	vp->enabled = false;

	/* Divide to avoid overflow */
	sys_clk_rate = voltdm->sys_clk.rate / 1000;

	timeout = (sys_clk_rate * voltdm->pmic->vp_timeout_us) / 1000;
	vddmin = max(voltdm->vp_param->vddmin, voltdm->pmic->vddmin);
	vddmax = min(voltdm->vp_param->vddmax, voltdm->pmic->vddmax);
	vddmin = voltdm->pmic->uv_to_vsel(vddmin);
	vddmax = voltdm->pmic->uv_to_vsel(vddmax);

	waittime = DIV_ROUND_UP(voltdm->pmic->step_size * sys_clk_rate,
				1000 * voltdm->pmic->slew_rate);
	vstepmin = voltdm->pmic->vp_vstepmin;
	vstepmax = voltdm->pmic->vp_vstepmax;

	/*
	 * VP_CONFIG: error gain is not set here, it will be updated
	 * on each scale, based on OPP.
	 */
	val = (voltdm->pmic->vp_erroroffset <<
	       __ffs(voltdm->vp->common->vpconfig_erroroffset_mask)) |
		vp->common->vpconfig_timeouten;
	voltdm->write(val, vp->vpconfig);

	/* VSTEPMIN */
	val = (waittime << vp->common->vstepmin_smpswaittimemin_shift) |
		(vstepmin <<  vp->common->vstepmin_stepmin_shift);
	voltdm->write(val, vp->vstepmin);

	/* VSTEPMAX */
	val = (vstepmax << vp->common->vstepmax_stepmax_shift) |
		(waittime << vp->common->vstepmax_smpswaittimemax_shift);
	voltdm->write(val, vp->vstepmax);

	/* VLIMITTO */
	val = (vddmax << vp->common->vlimitto_vddmax_shift) |
		(vddmin << vp->common->vlimitto_vddmin_shift) |
		(timeout <<  vp->common->vlimitto_timeout_shift);
	voltdm->write(val, vp->vlimitto);
}

int omap_vp_update_errorgain(struct voltagedomain *voltdm,
			     unsigned long target_volt)
{
	struct omap_volt_data *volt_data;

	if (!voltdm->vp)
		return -EINVAL;

	/* Get volt_data corresponding to target_volt */
	volt_data = omap_voltage_get_voltdata(voltdm, target_volt);
	if (IS_ERR(volt_data))
		return -EINVAL;

	/* Setting vp errorgain based on the voltage */
	voltdm->rmw(voltdm->vp->common->vpconfig_errorgain_mask,
		    volt_data->vp_errgain <<
		    __ffs(voltdm->vp->common->vpconfig_errorgain_mask),
		    voltdm->vp->vpconfig);

	return 0;
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
		pr_warn("%s: vdd_%s TRANXDONE timeout exceeded. Voltage change aborted\n",
			__func__, voltdm->name);
		return -ETIMEDOUT;
	}

	vpconfig = _vp_set_init_voltage(voltdm, target_volt);

	/* Force update of voltage */
	voltdm->write(vpconfig | vp->common->vpconfig_forceupdate,
		      voltdm->vp->vpconfig);

	/*
	 * Wait for TransactionDone. Typical latency is <200us.
	 * Depends on SMPSWAITTIMEMIN/MAX and voltage change
	 */
	timeout = 0;
	omap_test_timeout(vp->common->ops->check_txdone(vp->id),
			  VP_TRANXDONE_TIMEOUT, timeout);
	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_err("%s: vdd_%s TRANXDONE timeout exceeded. TRANXDONE never got set after the voltage update\n",
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
		pr_warn("%s: vdd_%s TRANXDONE timeout exceeded while trying to clear the TRANXDONE status\n",
			__func__, voltdm->name);

	/* Clear force bit */
	voltdm->write(vpconfig, vp->vpconfig);

	return 0;
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
	u32 vpconfig, volt;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warn("%s: VDD specified does not exist!\n", __func__);
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

	volt = voltdm_get_voltage(voltdm);
	if (!volt) {
		pr_warn("%s: unable to find current voltage for %s\n",
			__func__, voltdm->name);
		return;
	}

	vpconfig = _vp_set_init_voltage(voltdm, volt);

	/* Enable VP */
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
		pr_warn("%s: VDD specified does not exist!\n", __func__);
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
		pr_warn("%s: Trying to disable VP for vdd_%s when it is already disabled\n",
			__func__, voltdm->name);
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
		pr_warn("%s: vdd_%s idle timedout\n", __func__, voltdm->name);

	vp->enabled = false;

	return;
}
