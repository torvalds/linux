/*
 * otg_fsm.c - ChipIdea USB IP core OTG FSM driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Jun Li
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * This file mainly handles OTG fsm, it includes OTG fsm operations
 * for HNP and SRP.
 */

#include <linux/usb/otg.h>
#include <linux/usb/gadget.h>
#include <linux/usb/hcd.h>
#include <linux/usb/chipidea.h>
#include <linux/regulator/consumer.h>

#include "ci.h"
#include "bits.h"
#include "otg.h"
#include "otg_fsm.h"

static struct ci_otg_fsm_timer *otg_timer_initializer
(struct ci_hdrc *ci, void (*function)(void *, unsigned long),
			unsigned long expires, unsigned long data)
{
	struct ci_otg_fsm_timer *timer;

	timer = devm_kzalloc(ci->dev, sizeof(struct ci_otg_fsm_timer),
								GFP_KERNEL);
	if (!timer)
		return NULL;
	timer->function = function;
	timer->expires = expires;
	timer->data = data;
	return timer;
}

/*
 * Add timer to active timer list
 */
static void ci_otg_add_timer(struct ci_hdrc *ci, enum ci_otg_fsm_timer_index t)
{
	struct ci_otg_fsm_timer *tmp_timer;
	struct ci_otg_fsm_timer *timer = ci->fsm_timer->timer_list[t];
	struct list_head *active_timers = &ci->fsm_timer->active_timers;

	if (t >= NUM_CI_OTG_FSM_TIMERS)
		return;

	/*
	 * Check if the timer is already in the active list,
	 * if so update timer count
	 */
	list_for_each_entry(tmp_timer, active_timers, list)
		if (tmp_timer == timer) {
			timer->count = timer->expires;
			return;
		}

	timer->count = timer->expires;
	list_add_tail(&timer->list, active_timers);

	/* Enable 1ms irq */
	if (!(hw_read_otgsc(ci, OTGSC_1MSIE)))
		hw_write_otgsc(ci, OTGSC_1MSIE, OTGSC_1MSIE);
}

/*
 * Remove timer from active timer list
 */
static void ci_otg_del_timer(struct ci_hdrc *ci, enum ci_otg_fsm_timer_index t)
{
	struct ci_otg_fsm_timer *tmp_timer, *del_tmp;
	struct ci_otg_fsm_timer *timer = ci->fsm_timer->timer_list[t];
	struct list_head *active_timers = &ci->fsm_timer->active_timers;

	if (t >= NUM_CI_OTG_FSM_TIMERS)
		return;

	list_for_each_entry_safe(tmp_timer, del_tmp, active_timers, list)
		if (tmp_timer == timer)
			list_del(&timer->list);

	/* Disable 1ms irq if there is no any active timer */
	if (list_empty(active_timers))
		hw_write_otgsc(ci, OTGSC_1MSIE, 0);
}

/* The timeout callback function to set time out bit */
static void set_tmout(void *ptr, unsigned long indicator)
{
	*(int *)indicator = 1;
}

static void set_tmout_and_fsm(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	set_tmout(ci, indicator);

	disable_irq_nosync(ci->irq);
	queue_work(ci->wq, &ci->work);
}

static void a_wait_vfall_tmout_func(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	set_tmout(ci, indicator);
	/* Disable port power */
	hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_PP, 0);
	/* Clear exsiting DP irq */
	hw_write_otgsc(ci, OTGSC_DPIS, OTGSC_DPIS);
	/* Enable data pulse irq */
	hw_write_otgsc(ci, OTGSC_DPIE, OTGSC_DPIE);
	disable_irq_nosync(ci->irq);
	queue_work(ci->wq, &ci->work);
}

static void b_ase0_brst_tmout_func(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	set_tmout(ci, indicator);
	if (!hw_read_otgsc(ci, OTGSC_BSV))
		ci->fsm.b_sess_vld = 0;

	disable_irq_nosync(ci->irq);
	queue_work(ci->wq, &ci->work);
}

static void b_ssend_srp_tmout_func(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	set_tmout(ci, indicator);

	/* only vbus fall below B_sess_vld in b_idle state */
	if (ci->transceiver->state == OTG_STATE_B_IDLE) {
		disable_irq_nosync(ci->irq);
		queue_work(ci->wq, &ci->work);
	}
}

static void b_sess_vld_tmout_func(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	/* Check if A detached */
	if (!(hw_read_otgsc(ci, OTGSC_BSV))) {
		ci->fsm.b_sess_vld = 0;
		ci_otg_add_timer(ci, B_SSEND_SRP);
		disable_irq_nosync(ci->irq);
		queue_work(ci->wq, &ci->work);
	}
}

static void b_data_pulse_end(void *ptr, unsigned long indicator)
{
	struct ci_hdrc *ci = (struct ci_hdrc *)ptr;

	ci->fsm.b_srp_done = 1;
	ci->fsm.b_bus_req = 0;
	if (ci->fsm.power_up)
		ci->fsm.power_up = 0;

	hw_write_otgsc(ci, OTGSC_HABA, 0);

	disable_irq_nosync(ci->irq);
	queue_work(ci->wq, &ci->work);
}

/* Initialize timers */
static int ci_otg_init_timers(struct ci_hdrc *ci)
{
	struct otg_fsm *fsm = &ci->fsm;

	/* FSM used timers */
	ci->fsm_timer->timer_list[A_WAIT_VRISE] =
		otg_timer_initializer(ci, &set_tmout_and_fsm, TA_WAIT_VRISE,
			(unsigned long)&fsm->a_wait_vrise_tmout);
	if (ci->fsm_timer->timer_list[A_WAIT_VRISE] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[A_WAIT_VFALL] =
		otg_timer_initializer(ci, &a_wait_vfall_tmout_func,
		TA_WAIT_VFALL, (unsigned long)&fsm->a_wait_vfall_tmout);
	if (ci->fsm_timer->timer_list[A_WAIT_VFALL] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[A_WAIT_BCON] =
		otg_timer_initializer(ci, &set_tmout_and_fsm, TA_WAIT_BCON,
				(unsigned long)&fsm->a_wait_bcon_tmout);
	if (ci->fsm_timer->timer_list[A_WAIT_BCON] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[A_AIDL_BDIS] =
		otg_timer_initializer(ci, &set_tmout_and_fsm, TA_AIDL_BDIS,
				(unsigned long)&fsm->a_aidl_bdis_tmout);
	if (ci->fsm_timer->timer_list[A_AIDL_BDIS] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[A_BIDL_ADIS] =
		otg_timer_initializer(ci, &set_tmout_and_fsm, TA_BIDL_ADIS,
				(unsigned long)&fsm->a_bidl_adis_tmout);
	if (ci->fsm_timer->timer_list[A_BIDL_ADIS] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_ASE0_BRST] =
		otg_timer_initializer(ci, &b_ase0_brst_tmout_func, TB_ASE0_BRST,
					(unsigned long)&fsm->b_ase0_brst_tmout);
	if (ci->fsm_timer->timer_list[B_ASE0_BRST] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_SE0_SRP] =
		otg_timer_initializer(ci, &set_tmout_and_fsm, TB_SE0_SRP,
					(unsigned long)&fsm->b_se0_srp);
	if (ci->fsm_timer->timer_list[B_SE0_SRP] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_SSEND_SRP] =
		otg_timer_initializer(ci, &b_ssend_srp_tmout_func, TB_SSEND_SRP,
					(unsigned long)&fsm->b_ssend_srp);
	if (ci->fsm_timer->timer_list[B_SSEND_SRP] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_SRP_FAIL] =
		otg_timer_initializer(ci, &set_tmout, TB_SRP_FAIL,
				(unsigned long)&fsm->b_srp_done);
	if (ci->fsm_timer->timer_list[B_SRP_FAIL] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_DATA_PLS] =
		otg_timer_initializer(ci, &b_data_pulse_end, TB_DATA_PLS, 0);
	if (ci->fsm_timer->timer_list[B_DATA_PLS] == NULL)
		return -ENOMEM;

	ci->fsm_timer->timer_list[B_SESS_VLD] =	otg_timer_initializer(ci,
					&b_sess_vld_tmout_func, TB_SESS_VLD, 0);
	if (ci->fsm_timer->timer_list[B_SESS_VLD] == NULL)
		return -ENOMEM;

	return 0;
}

/* -------------------------------------------------------------*/
/* Operations that will be called from OTG Finite State Machine */
/* -------------------------------------------------------------*/
static void ci_otg_fsm_add_timer(struct otg_fsm *fsm, enum otg_fsm_timer t)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	if (t < NUM_OTG_FSM_TIMERS)
		ci_otg_add_timer(ci, t);
	return;
}

static void ci_otg_fsm_del_timer(struct otg_fsm *fsm, enum otg_fsm_timer t)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	if (t < NUM_OTG_FSM_TIMERS)
		ci_otg_del_timer(ci, t);
	return;
}

/*
 * A-device drive vbus: turn on vbus regulator and enable port power
 * Data pulse irq should be disabled while vbus is on.
 */
static void ci_otg_drv_vbus(struct otg_fsm *fsm, int on)
{
	int ret;
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	if (on) {
		/* Enable power power */
		hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_PP,
							PORTSC_PP);
		if (ci->platdata->reg_vbus) {
			ret = regulator_enable(ci->platdata->reg_vbus);
			if (ret) {
				dev_err(ci->dev,
				"Failed to enable vbus regulator, ret=%d\n",
				ret);
				return;
			}
		}
		/* Disable data pulse irq */
		hw_write_otgsc(ci, OTGSC_DPIE, 0);

		fsm->a_srp_det = 0;
		fsm->power_up = 0;
	} else {
		if (ci->platdata->reg_vbus)
			regulator_disable(ci->platdata->reg_vbus);

		fsm->a_bus_drop = 1;
		fsm->a_bus_req = 0;
	}
}

/*
 * Control data line by Run Stop bit.
 */
static void ci_otg_loc_conn(struct otg_fsm *fsm, int on)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	if (on)
		hw_write(ci, OP_USBCMD, USBCMD_RS, USBCMD_RS);
	else
		hw_write(ci, OP_USBCMD, USBCMD_RS, 0);
}

/*
 * Generate SOF by host.
 * This is controlled through suspend/resume the port.
 * In host mode, controller will automatically send SOF.
 * Suspend will block the data on the port.
 */
static void ci_otg_loc_sof(struct otg_fsm *fsm, int on)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	if (on)
		hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_FPR,
							PORTSC_FPR);
	else
		hw_write(ci, OP_PORTSC, PORTSC_W1C_BITS | PORTSC_SUSP,
							PORTSC_SUSP);
}

/*
 * Start SRP pulsing by data-line pulsing,
 * no v-bus pulsing followed
 */
static void ci_otg_start_pulse(struct otg_fsm *fsm)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	/* Hardware Assistant Data pulse */
	hw_write_otgsc(ci, OTGSC_HADP, OTGSC_HADP);

	ci_otg_add_timer(ci, B_DATA_PLS);
}

static int ci_otg_start_host(struct otg_fsm *fsm, int on)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	mutex_unlock(&fsm->lock);
	if (on) {
		ci_role_stop(ci);
		ci_role_start(ci, CI_ROLE_HOST);
	} else {
		ci_role_stop(ci);
		hw_device_reset(ci, USBMODE_CM_DC);
		ci_role_start(ci, CI_ROLE_GADGET);
	}
	mutex_lock(&fsm->lock);
	return 0;
}

static int ci_otg_start_gadget(struct otg_fsm *fsm, int on)
{
	struct ci_hdrc	*ci = container_of(fsm, struct ci_hdrc, fsm);

	mutex_unlock(&fsm->lock);
	if (on)
		usb_gadget_vbus_connect(&ci->gadget);
	else
		usb_gadget_vbus_disconnect(&ci->gadget);
	mutex_lock(&fsm->lock);

	return 0;
}

static struct otg_fsm_ops ci_otg_ops = {
	.drv_vbus = ci_otg_drv_vbus,
	.loc_conn = ci_otg_loc_conn,
	.loc_sof = ci_otg_loc_sof,
	.start_pulse = ci_otg_start_pulse,
	.add_timer = ci_otg_fsm_add_timer,
	.del_timer = ci_otg_fsm_del_timer,
	.start_host = ci_otg_start_host,
	.start_gadget = ci_otg_start_gadget,
};

int ci_hdrc_otg_fsm_init(struct ci_hdrc *ci)
{
	int retval = 0;
	struct usb_otg *otg;

	otg = devm_kzalloc(ci->dev,
			sizeof(struct usb_otg), GFP_KERNEL);
	if (!otg) {
		dev_err(ci->dev,
		"Failed to allocate usb_otg structure for ci hdrc otg!\n");
		return -ENOMEM;
	}

	otg->phy = ci->transceiver;
	otg->gadget = &ci->gadget;
	ci->fsm.otg = otg;
	ci->transceiver->otg = ci->fsm.otg;
	ci->fsm.power_up = 1;
	ci->fsm.id = hw_read_otgsc(ci, OTGSC_ID) ? 1 : 0;
	ci->transceiver->state = OTG_STATE_UNDEFINED;
	ci->fsm.ops = &ci_otg_ops;

	mutex_init(&ci->fsm.lock);

	ci->fsm_timer = devm_kzalloc(ci->dev,
			sizeof(struct ci_otg_fsm_timer_list), GFP_KERNEL);
	if (!ci->fsm_timer) {
		dev_err(ci->dev,
		"Failed to allocate timer structure for ci hdrc otg!\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&ci->fsm_timer->active_timers);
	retval = ci_otg_init_timers(ci);
	if (retval) {
		dev_err(ci->dev, "Couldn't init OTG timers\n");
		return retval;
	}

	/* Enable A vbus valid irq */
	hw_write_otgsc(ci, OTGSC_AVVIE, OTGSC_AVVIE);

	if (ci->fsm.id) {
		ci->fsm.b_ssend_srp =
			hw_read_otgsc(ci, OTGSC_BSV) ? 0 : 1;
		ci->fsm.b_sess_vld =
			hw_read_otgsc(ci, OTGSC_BSV) ? 1 : 0;
		/* Enable BSV irq */
		hw_write_otgsc(ci, OTGSC_BSVIE, OTGSC_BSVIE);
	}

	return 0;
}
