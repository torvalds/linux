/*
 * OMAP3/OMAP4 Voltage Management Routines
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 * Lesly A M <x0080970@ti.com>
 *
 * Copyright (C) 2008, 2011 Nokia Corporation
 * Kalle Jokiniemi
 * Paul Walmsley
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <plat/common.h>

#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"
#include "control.h"

#include "voltage.h"
#include "powerdomain.h"

#include "vc.h"
#include "vp.h"

static LIST_HEAD(voltdm_list);

static int __init _config_common_vdd_data(struct voltagedomain *voltdm)
{
	char *sys_ck_name;
	struct clk *sys_ck;
	u32 sys_clk_speed, timeout_val, waittime;
	struct omap_vdd_info *vdd = voltdm->vdd;

	/*
	 * XXX Clockfw should handle this, or this should be in a
	 * struct record
	 */
	if (cpu_is_omap24xx() || cpu_is_omap34xx())
		sys_ck_name = "sys_ck";
	else if (cpu_is_omap44xx())
		sys_ck_name = "sys_clkin_ck";
	else
		return -EINVAL;

	/*
	 * Sys clk rate is require to calculate vp timeout value and
	 * smpswaittimemin and smpswaittimemax.
	 */
	sys_ck = clk_get(NULL, sys_ck_name);
	if (IS_ERR(sys_ck)) {
		pr_warning("%s: Could not get the sys clk to calculate"
			"various vdd_%s params\n", __func__, voltdm->name);
		return -EINVAL;
	}
	sys_clk_speed = clk_get_rate(sys_ck);
	clk_put(sys_ck);
	/* Divide to avoid overflow */
	sys_clk_speed /= 1000;

	/* Generic voltage parameters */
	vdd->volt_scale = omap_vp_forceupdate_scale;
	voltdm->vp->enabled = false;

	vdd->vp_rt_data.vpconfig_erroroffset =
		(voltdm->pmic->vp_erroroffset <<
		 voltdm->vp->common->vpconfig_erroroffset_shift);

	timeout_val = (sys_clk_speed * voltdm->pmic->vp_timeout_us) / 1000;
	vdd->vp_rt_data.vlimitto_timeout = timeout_val;
	vdd->vp_rt_data.vlimitto_vddmin = voltdm->pmic->vp_vddmin;
	vdd->vp_rt_data.vlimitto_vddmax = voltdm->pmic->vp_vddmax;

	waittime = ((voltdm->pmic->step_size / voltdm->pmic->slew_rate) *
				sys_clk_speed) / 1000;
	vdd->vp_rt_data.vstepmin_smpswaittimemin = waittime;
	vdd->vp_rt_data.vstepmax_smpswaittimemax = waittime;
	vdd->vp_rt_data.vstepmin_stepmin = voltdm->pmic->vp_vstepmin;
	vdd->vp_rt_data.vstepmax_stepmax = voltdm->pmic->vp_vstepmax;

	return 0;
}

static int __init omap_vdd_data_configure(struct voltagedomain *voltdm)
{
	int ret = -EINVAL;

	if (!voltdm->pmic) {
		pr_err("%s: PMIC info requried to configure vdd_%s not"
			"populated.Hence cannot initialize vdd_%s\n",
			__func__, voltdm->name, voltdm->name);
		goto ovdc_out;
	}

	if (IS_ERR_VALUE(_config_common_vdd_data(voltdm)))
		goto ovdc_out;

	ret = 0;

ovdc_out:
	return ret;
}

/* Public functions */
/**
 * omap_voltage_get_nom_volt() - Gets the current non-auto-compensated voltage
 * @voltdm:	pointer to the VDD for which current voltage info is needed
 *
 * API to get the current non-auto-compensated voltage for a VDD.
 * Returns 0 in case of error else returns the current voltage for the VDD.
 */
unsigned long omap_voltage_get_nom_volt(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return 0;
	}

	vdd = voltdm->vdd;

	return vdd->curr_volt;
}

/**
 * omap_voltage_scale_vdd() - API to scale voltage of a particular
 *				voltage domain.
 * @voltdm:	pointer to the VDD which is to be scaled.
 * @target_volt:	The target voltage of the voltage domain
 *
 * This API should be called by the kernel to do the voltage scaling
 * for a particular voltage domain during dvfs or any other situation.
 */
int omap_voltage_scale_vdd(struct voltagedomain *voltdm,
		unsigned long target_volt)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return -EINVAL;
	}

	vdd = voltdm->vdd;

	if (!vdd->volt_scale) {
		pr_err("%s: No voltage scale API registered for vdd_%s\n",
			__func__, voltdm->name);
		return -ENODATA;
	}

	return vdd->volt_scale(voltdm, target_volt);
}

/**
 * omap_voltage_reset() - Resets the voltage of a particular voltage domain
 *			to that of the current OPP.
 * @voltdm:	pointer to the VDD whose voltage is to be reset.
 *
 * This API finds out the correct voltage the voltage domain is supposed
 * to be at and resets the voltage to that level. Should be used especially
 * while disabling any voltage compensation modules.
 */
void omap_voltage_reset(struct voltagedomain *voltdm)
{
	unsigned long target_uvdc;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	target_uvdc = omap_voltage_get_nom_volt(voltdm);
	if (!target_uvdc) {
		pr_err("%s: unable to find current voltage for vdd_%s\n",
			__func__, voltdm->name);
		return;
	}

	omap_voltage_scale_vdd(voltdm, target_uvdc);
}

/**
 * omap_voltage_get_volttable() - API to get the voltage table associated with a
 *				particular voltage domain.
 * @voltdm:	pointer to the VDD for which the voltage table is required
 * @volt_data:	the voltage table for the particular vdd which is to be
 *		populated by this API
 *
 * This API populates the voltage table associated with a VDD into the
 * passed parameter pointer. Returns the count of distinct voltages
 * supported by this vdd.
 *
 */
void omap_voltage_get_volttable(struct voltagedomain *voltdm,
		struct omap_volt_data **volt_data)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = voltdm->vdd;

	*volt_data = vdd->volt_data;
}

/**
 * omap_voltage_get_voltdata() - API to get the voltage table entry for a
 *				particular voltage
 * @voltdm:	pointer to the VDD whose voltage table has to be searched
 * @volt:	the voltage to be searched in the voltage table
 *
 * This API searches through the voltage table for the required voltage
 * domain and tries to find a matching entry for the passed voltage volt.
 * If a matching entry is found volt_data is populated with that entry.
 * This API searches only through the non-compensated voltages int the
 * voltage table.
 * Returns pointer to the voltage table entry corresponding to volt on
 * success. Returns -ENODATA if no voltage table exisits for the passed voltage
 * domain or if there is no matching entry.
 */
struct omap_volt_data *omap_voltage_get_voltdata(struct voltagedomain *voltdm,
		unsigned long volt)
{
	struct omap_vdd_info *vdd;
	int i;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	vdd = voltdm->vdd;

	if (!vdd->volt_data) {
		pr_warning("%s: voltage table does not exist for vdd_%s\n",
			__func__, voltdm->name);
		return ERR_PTR(-ENODATA);
	}

	for (i = 0; vdd->volt_data[i].volt_nominal != 0; i++) {
		if (vdd->volt_data[i].volt_nominal == volt)
			return &vdd->volt_data[i];
	}

	pr_notice("%s: Unable to match the current voltage with the voltage"
		"table for vdd_%s\n", __func__, voltdm->name);

	return ERR_PTR(-ENODATA);
}

/**
 * omap_voltage_register_pmic() - API to register PMIC specific data
 * @voltdm:	pointer to the VDD for which the PMIC specific data is
 *		to be registered
 * @pmic:	the structure containing pmic info
 *
 * This API is to be called by the SOC/PMIC file to specify the
 * pmic specific info as present in omap_voltdm_pmic structure.
 */
int omap_voltage_register_pmic(struct voltagedomain *voltdm,
			       struct omap_voltdm_pmic *pmic)
{
	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return -EINVAL;
	}

	voltdm->pmic = pmic;

	return 0;
}

/**
 * omap_change_voltscale_method() - API to change the voltage scaling method.
 * @voltdm:	pointer to the VDD whose voltage scaling method
 *		has to be changed.
 * @voltscale_method:	the method to be used for voltage scaling.
 *
 * This API can be used by the board files to change the method of voltage
 * scaling between vpforceupdate and vcbypass. The parameter values are
 * defined in voltage.h
 */
void omap_change_voltscale_method(struct voltagedomain *voltdm,
		int voltscale_method)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = voltdm->vdd;

	switch (voltscale_method) {
	case VOLTSCALE_VPFORCEUPDATE:
		vdd->volt_scale = omap_vp_forceupdate_scale;
		return;
	case VOLTSCALE_VCBYPASS:
		vdd->volt_scale = omap_vc_bypass_scale;
		return;
	default:
		pr_warning("%s: Trying to change the method of voltage scaling"
			"to an unsupported one!\n", __func__);
	}
}

/**
 * omap_voltage_late_init() - Init the various voltage parameters
 *
 * This API is to be called in the later stages of the
 * system boot to init the voltage controller and
 * voltage processors.
 */
int __init omap_voltage_late_init(void)
{
	struct voltagedomain *voltdm;

	if (list_empty(&voltdm_list)) {
		pr_err("%s: Voltage driver support not added\n",
			__func__);
		return -EINVAL;
	}

	list_for_each_entry(voltdm, &voltdm_list, node) {
		if (!voltdm->scalable)
			continue;

		if (voltdm->vc) {
			voltdm->vdd->volt_scale = omap_vc_bypass_scale;
			omap_vc_init_channel(voltdm);
		}

		if (voltdm->vdd) {
			if (omap_vdd_data_configure(voltdm))
				continue;
			omap_vp_init(voltdm);
		}
	}

	return 0;
}

static struct voltagedomain *_voltdm_lookup(const char *name)
{
	struct voltagedomain *voltdm, *temp_voltdm;

	voltdm = NULL;

	list_for_each_entry(temp_voltdm, &voltdm_list, node) {
		if (!strcmp(name, temp_voltdm->name)) {
			voltdm = temp_voltdm;
			break;
		}
	}

	return voltdm;
}

/**
 * voltdm_add_pwrdm - add a powerdomain to a voltagedomain
 * @voltdm: struct voltagedomain * to add the powerdomain to
 * @pwrdm: struct powerdomain * to associate with a voltagedomain
 *
 * Associate the powerdomain @pwrdm with a voltagedomain @voltdm.  This
 * enables the use of voltdm_for_each_pwrdm().  Returns -EINVAL if
 * presented with invalid pointers; -ENOMEM if memory could not be allocated;
 * or 0 upon success.
 */
int voltdm_add_pwrdm(struct voltagedomain *voltdm, struct powerdomain *pwrdm)
{
	if (!voltdm || !pwrdm)
		return -EINVAL;

	pr_debug("voltagedomain: associating powerdomain %s with voltagedomain "
		 "%s\n", pwrdm->name, voltdm->name);

	list_add(&pwrdm->voltdm_node, &voltdm->pwrdm_list);

	return 0;
}

/**
 * voltdm_for_each_pwrdm - call function for each pwrdm in a voltdm
 * @voltdm: struct voltagedomain * to iterate over
 * @fn: callback function *
 *
 * Call the supplied function @fn for each powerdomain in the
 * voltagedomain @voltdm.  Returns -EINVAL if presented with invalid
 * pointers; or passes along the last return value of the callback
 * function, which should be 0 for success or anything else to
 * indicate failure.
 */
int voltdm_for_each_pwrdm(struct voltagedomain *voltdm,
			  int (*fn)(struct voltagedomain *voltdm,
				    struct powerdomain *pwrdm))
{
	struct powerdomain *pwrdm;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	list_for_each_entry(pwrdm, &voltdm->pwrdm_list, voltdm_node)
		ret = (*fn)(voltdm, pwrdm);

	return ret;
}

/**
 * voltdm_for_each - call function on each registered voltagedomain
 * @fn: callback function *
 *
 * Call the supplied function @fn for each registered voltagedomain.
 * The callback function @fn can return anything but 0 to bail out
 * early from the iterator.  Returns the last return value of the
 * callback function, which should be 0 for success or anything else
 * to indicate failure; or -EINVAL if the function pointer is null.
 */
int voltdm_for_each(int (*fn)(struct voltagedomain *voltdm, void *user),
		    void *user)
{
	struct voltagedomain *temp_voltdm;
	int ret = 0;

	if (!fn)
		return -EINVAL;

	list_for_each_entry(temp_voltdm, &voltdm_list, node) {
		ret = (*fn)(temp_voltdm, user);
		if (ret)
			break;
	}

	return ret;
}

static int _voltdm_register(struct voltagedomain *voltdm)
{
	if (!voltdm || !voltdm->name)
		return -EINVAL;

	INIT_LIST_HEAD(&voltdm->pwrdm_list);
	list_add(&voltdm->node, &voltdm_list);

	pr_debug("voltagedomain: registered %s\n", voltdm->name);

	return 0;
}

/**
 * voltdm_lookup - look up a voltagedomain by name, return a pointer
 * @name: name of voltagedomain
 *
 * Find a registered voltagedomain by its name @name.  Returns a pointer
 * to the struct voltagedomain if found, or NULL otherwise.
 */
struct voltagedomain *voltdm_lookup(const char *name)
{
	struct voltagedomain *voltdm ;

	if (!name)
		return NULL;

	voltdm = _voltdm_lookup(name);

	return voltdm;
}

/**
 * voltdm_init - set up the voltagedomain layer
 * @voltdm_list: array of struct voltagedomain pointers to register
 *
 * Loop through the array of voltagedomains @voltdm_list, registering all
 * that are available on the current CPU. If voltdm_list is supplied
 * and not null, all of the referenced voltagedomains will be
 * registered.  No return value.
 */
void voltdm_init(struct voltagedomain **voltdms)
{
	struct voltagedomain **v;

	if (voltdms) {
		for (v = voltdms; *v; v++)
			_voltdm_register(*v);
	}
}
