/*
 * dvb_frontend.c: DVB frontend tuning interface/thread
 *
 *
 * Copyright (C) 1999-2001 Ralph  Metzler
 *			   Marcus Metzler
 *			   Holger Waechtler
 *				      for convergence integrated media GmbH
 *
 * Copyright (C) 2004 Andrew de Quincey (tuning thread cleanup)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

/* Enables DVBv3 compatibility bits at the headers */
#define __DVB_CORE__

#define pr_fmt(fmt) "dvb_frontend: " fmt

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/semaphore.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/freezer.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/ktime.h>
#include <asm/processor.h>

#include "dvb_frontend.h"
#include "dvbdev.h"
#include <linux/dvb/version.h>

static int dvb_frontend_debug;
static int dvb_shutdown_timeout;
static int dvb_force_auto_inversion;
static int dvb_override_tune_delay;
static int dvb_powerdown_on_sleep = 1;
static int dvb_mfe_wait_time = 5;

module_param_named(frontend_debug, dvb_frontend_debug, int, 0644);
MODULE_PARM_DESC(frontend_debug, "Turn on/off frontend core debugging (default:off).");
module_param(dvb_shutdown_timeout, int, 0644);
MODULE_PARM_DESC(dvb_shutdown_timeout, "wait <shutdown_timeout> seconds after close() before suspending hardware");
module_param(dvb_force_auto_inversion, int, 0644);
MODULE_PARM_DESC(dvb_force_auto_inversion, "0: normal (default), 1: INVERSION_AUTO forced always");
module_param(dvb_override_tune_delay, int, 0644);
MODULE_PARM_DESC(dvb_override_tune_delay, "0: normal (default), >0 => delay in milliseconds to wait for lock after a tune attempt");
module_param(dvb_powerdown_on_sleep, int, 0644);
MODULE_PARM_DESC(dvb_powerdown_on_sleep, "0: do not power down, 1: turn LNB voltage off on sleep (default)");
module_param(dvb_mfe_wait_time, int, 0644);
MODULE_PARM_DESC(dvb_mfe_wait_time, "Wait up to <mfe_wait_time> seconds on open() for multi-frontend to become available (default:5 seconds)");

#define dprintk(fmt, arg...) \
	printk(KERN_DEBUG pr_fmt("%s: " fmt), __func__, ##arg)

#define FESTATE_IDLE 1
#define FESTATE_RETUNE 2
#define FESTATE_TUNING_FAST 4
#define FESTATE_TUNING_SLOW 8
#define FESTATE_TUNED 16
#define FESTATE_ZIGZAG_FAST 32
#define FESTATE_ZIGZAG_SLOW 64
#define FESTATE_DISEQC 128
#define FESTATE_ERROR 256
#define FESTATE_WAITFORLOCK (FESTATE_TUNING_FAST | FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW | FESTATE_DISEQC)
#define FESTATE_SEARCHING_FAST (FESTATE_TUNING_FAST | FESTATE_ZIGZAG_FAST)
#define FESTATE_SEARCHING_SLOW (FESTATE_TUNING_SLOW | FESTATE_ZIGZAG_SLOW)
#define FESTATE_LOSTLOCK (FESTATE_ZIGZAG_FAST | FESTATE_ZIGZAG_SLOW)

/*
 * FESTATE_IDLE. No tuning parameters have been supplied and the loop is idling.
 * FESTATE_RETUNE. Parameters have been supplied, but we have not yet performed the first tune.
 * FESTATE_TUNING_FAST. Tuning parameters have been supplied and fast zigzag scan is in progress.
 * FESTATE_TUNING_SLOW. Tuning parameters have been supplied. Fast zigzag failed, so we're trying again, but slower.
 * FESTATE_TUNED. The frontend has successfully locked on.
 * FESTATE_ZIGZAG_FAST. The lock has been lost, and a fast zigzag has been initiated to try and regain it.
 * FESTATE_ZIGZAG_SLOW. The lock has been lost. Fast zigzag has been failed, so we're trying again, but slower.
 * FESTATE_DISEQC. A DISEQC command has just been issued.
 * FESTATE_WAITFORLOCK. When we're waiting for a lock.
 * FESTATE_SEARCHING_FAST. When we're searching for a signal using a fast zigzag scan.
 * FESTATE_SEARCHING_SLOW. When we're searching for a signal using a slow zigzag scan.
 * FESTATE_LOSTLOCK. When the lock has been lost, and we're searching it again.
 */

static DEFINE_MUTEX(frontend_mutex);

struct dvb_frontend_private {
	/* thread/frontend values */
	struct dvb_device *dvbdev;
	struct dvb_frontend_parameters parameters_out;
	struct dvb_fe_events events;
	struct semaphore sem;
	struct list_head list_head;
	wait_queue_head_t wait_queue;
	struct task_struct *thread;
	unsigned long release_jiffies;
	unsigned int wakeup;
	enum fe_status status;
	unsigned long tune_mode_flags;
	unsigned int delay;
	unsigned int reinitialise;
	int tone;
	int voltage;

	/* swzigzag values */
	unsigned int state;
	unsigned int bending;
	int lnb_drift;
	unsigned int inversion;
	unsigned int auto_step;
	unsigned int auto_sub_step;
	unsigned int started_auto_step;
	unsigned int min_delay;
	unsigned int max_drift;
	unsigned int step_size;
	int quality;
	unsigned int check_wrapped;
	enum dvbfe_search algo_status;

#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
	struct media_pipeline pipe;
#endif
};

static void dvb_frontend_invoke_release(struct dvb_frontend *fe,
					void (*release)(struct dvb_frontend *fe));

static void __dvb_frontend_free(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	if (fepriv)
		dvb_free_device(fepriv->dvbdev);

	dvb_frontend_invoke_release(fe, fe->ops.release);

	if (fepriv)
		kfree(fepriv);
}

static void dvb_frontend_free(struct kref *ref)
{
	struct dvb_frontend *fe =
		container_of(ref, struct dvb_frontend, refcount);

	__dvb_frontend_free(fe);
}

static void dvb_frontend_put(struct dvb_frontend *fe)
{
	/*
	 * Check if the frontend was registered, as otherwise
	 * kref was not initialized yet.
	 */
	if (fe->frontend_priv)
		kref_put(&fe->refcount, dvb_frontend_free);
	else
		__dvb_frontend_free(fe);
}

static void dvb_frontend_get(struct dvb_frontend *fe)
{
	kref_get(&fe->refcount);
}

static void dvb_frontend_wakeup(struct dvb_frontend *fe);
static int dtv_get_frontend(struct dvb_frontend *fe,
			    struct dtv_frontend_properties *c,
			    struct dvb_frontend_parameters *p_out);
static int
dtv_property_legacy_params_sync(struct dvb_frontend *fe,
				const struct dtv_frontend_properties *c,
				struct dvb_frontend_parameters *p);

static bool has_get_frontend(struct dvb_frontend *fe)
{
	return fe->ops.get_frontend != NULL;
}

/*
 * Due to DVBv3 API calls, a delivery system should be mapped into one of
 * the 4 DVBv3 delivery systems (FE_QPSK, FE_QAM, FE_OFDM or FE_ATSC),
 * otherwise, a DVBv3 call will fail.
 */
enum dvbv3_emulation_type {
	DVBV3_UNKNOWN,
	DVBV3_QPSK,
	DVBV3_QAM,
	DVBV3_OFDM,
	DVBV3_ATSC,
};

static enum dvbv3_emulation_type dvbv3_type(u32 delivery_system)
{
	switch (delivery_system) {
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		return DVBV3_QAM;
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_TURBO:
	case SYS_ISDBS:
	case SYS_DSS:
		return DVBV3_QPSK;
	case SYS_DVBT:
	case SYS_DVBT2:
	case SYS_ISDBT:
	case SYS_DTMB:
		return DVBV3_OFDM;
	case SYS_ATSC:
	case SYS_ATSCMH:
	case SYS_DVBC_ANNEX_B:
		return DVBV3_ATSC;
	case SYS_UNDEFINED:
	case SYS_ISDBC:
	case SYS_DVBH:
	case SYS_DAB:
	default:
		/*
		 * Doesn't know how to emulate those types and/or
		 * there's no frontend driver from this type yet
		 * with some emulation code, so, we're not sure yet how
		 * to handle them, or they're not compatible with a DVBv3 call.
		 */
		return DVBV3_UNKNOWN;
	}
}

static void dvb_frontend_add_event(struct dvb_frontend *fe,
				   enum fe_status status)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_fe_events *events = &fepriv->events;
	struct dvb_frontend_event *e;
	int wp;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if ((status & FE_HAS_LOCK) && has_get_frontend(fe))
		dtv_get_frontend(fe, c, &fepriv->parameters_out);

	mutex_lock(&events->mtx);

	wp = (events->eventw + 1) % MAX_EVENT;
	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_EVENT;
	}

	e = &events->events[events->eventw];
	e->status = status;
	e->parameters = fepriv->parameters_out;

	events->eventw = wp;

	mutex_unlock(&events->mtx);

	wake_up_interruptible (&events->wait_queue);
}

static int dvb_frontend_get_event(struct dvb_frontend *fe,
			    struct dvb_frontend_event *event, int flags)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_fe_events *events = &fepriv->events;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if (events->overflow) {
		events->overflow = 0;
		return -EOVERFLOW;
	}

	if (events->eventw == events->eventr) {
		int ret;

		if (flags & O_NONBLOCK)
			return -EWOULDBLOCK;

		up(&fepriv->sem);

		ret = wait_event_interruptible (events->wait_queue,
						events->eventw != events->eventr);

		if (down_interruptible (&fepriv->sem))
			return -ERESTARTSYS;

		if (ret < 0)
			return ret;
	}

	mutex_lock(&events->mtx);
	*event = events->events[events->eventr];
	events->eventr = (events->eventr + 1) % MAX_EVENT;
	mutex_unlock(&events->mtx);

	return 0;
}

static void dvb_frontend_clear_events(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_fe_events *events = &fepriv->events;

	mutex_lock(&events->mtx);
	events->eventr = events->eventw;
	mutex_unlock(&events->mtx);
}

static void dvb_frontend_init(struct dvb_frontend *fe)
{
	dev_dbg(fe->dvb->device,
			"%s: initialising adapter %i frontend %i (%s)...\n",
			__func__, fe->dvb->num, fe->id, fe->ops.info.name);

	if (fe->ops.init)
		fe->ops.init(fe);
	if (fe->ops.tuner_ops.init) {
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);
		fe->ops.tuner_ops.init(fe);
		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}
}

void dvb_frontend_reinitialise(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	fepriv->reinitialise = 1;
	dvb_frontend_wakeup(fe);
}
EXPORT_SYMBOL(dvb_frontend_reinitialise);

static void dvb_frontend_swzigzag_update_delay(struct dvb_frontend_private *fepriv, int locked)
{
	int q2;
	struct dvb_frontend *fe = fepriv->dvbdev->priv;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if (locked)
		(fepriv->quality) = (fepriv->quality * 220 + 36*256) / 256;
	else
		(fepriv->quality) = (fepriv->quality * 220 + 0) / 256;

	q2 = fepriv->quality - 128;
	q2 *= q2;

	fepriv->delay = fepriv->min_delay + q2 * HZ / (128*128);
}

/**
 * Performs automatic twiddling of frontend parameters.
 *
 * @param fe The frontend concerned.
 * @param check_wrapped Checks if an iteration has completed. DO NOT SET ON THE FIRST ATTEMPT
 * @returns Number of complete iterations that have been performed.
 */
static int dvb_frontend_swzigzag_autotune(struct dvb_frontend *fe, int check_wrapped)
{
	int autoinversion;
	int ready = 0;
	int fe_set_err = 0;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache, tmp;
	int original_inversion = c->inversion;
	u32 original_frequency = c->frequency;

	/* are we using autoinversion? */
	autoinversion = ((!(fe->ops.info.caps & FE_CAN_INVERSION_AUTO)) &&
			 (c->inversion == INVERSION_AUTO));

	/* setup parameters correctly */
	while(!ready) {
		/* calculate the lnb_drift */
		fepriv->lnb_drift = fepriv->auto_step * fepriv->step_size;

		/* wrap the auto_step if we've exceeded the maximum drift */
		if (fepriv->lnb_drift > fepriv->max_drift) {
			fepriv->auto_step = 0;
			fepriv->auto_sub_step = 0;
			fepriv->lnb_drift = 0;
		}

		/* perform inversion and +/- zigzag */
		switch(fepriv->auto_sub_step) {
		case 0:
			/* try with the current inversion and current drift setting */
			ready = 1;
			break;

		case 1:
			if (!autoinversion) break;

			fepriv->inversion = (fepriv->inversion == INVERSION_OFF) ? INVERSION_ON : INVERSION_OFF;
			ready = 1;
			break;

		case 2:
			if (fepriv->lnb_drift == 0) break;

			fepriv->lnb_drift = -fepriv->lnb_drift;
			ready = 1;
			break;

		case 3:
			if (fepriv->lnb_drift == 0) break;
			if (!autoinversion) break;

			fepriv->inversion = (fepriv->inversion == INVERSION_OFF) ? INVERSION_ON : INVERSION_OFF;
			fepriv->lnb_drift = -fepriv->lnb_drift;
			ready = 1;
			break;

		default:
			fepriv->auto_step++;
			fepriv->auto_sub_step = -1; /* it'll be incremented to 0 in a moment */
			break;
		}

		if (!ready) fepriv->auto_sub_step++;
	}

	/* if this attempt would hit where we started, indicate a complete
	 * iteration has occurred */
	if ((fepriv->auto_step == fepriv->started_auto_step) &&
	    (fepriv->auto_sub_step == 0) && check_wrapped) {
		return 1;
	}

	dev_dbg(fe->dvb->device, "%s: drift:%i inversion:%i auto_step:%i " \
			"auto_sub_step:%i started_auto_step:%i\n",
			__func__, fepriv->lnb_drift, fepriv->inversion,
			fepriv->auto_step, fepriv->auto_sub_step,
			fepriv->started_auto_step);

	/* set the frontend itself */
	c->frequency += fepriv->lnb_drift;
	if (autoinversion)
		c->inversion = fepriv->inversion;
	tmp = *c;
	if (fe->ops.set_frontend)
		fe_set_err = fe->ops.set_frontend(fe);
	*c = tmp;
	if (fe_set_err < 0) {
		fepriv->state = FESTATE_ERROR;
		return fe_set_err;
	}

	c->frequency = original_frequency;
	c->inversion = original_inversion;

	fepriv->auto_sub_step++;
	return 0;
}

static void dvb_frontend_swzigzag(struct dvb_frontend *fe)
{
	enum fe_status s = FE_NONE;
	int retval = 0;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache, tmp;

	/* if we've got no parameters, just keep idling */
	if (fepriv->state & FESTATE_IDLE) {
		fepriv->delay = 3*HZ;
		fepriv->quality = 0;
		return;
	}

	/* in SCAN mode, we just set the frontend when asked and leave it alone */
	if (fepriv->tune_mode_flags & FE_TUNE_MODE_ONESHOT) {
		if (fepriv->state & FESTATE_RETUNE) {
			tmp = *c;
			if (fe->ops.set_frontend)
				retval = fe->ops.set_frontend(fe);
			*c = tmp;
			if (retval < 0)
				fepriv->state = FESTATE_ERROR;
			else
				fepriv->state = FESTATE_TUNED;
		}
		fepriv->delay = 3*HZ;
		fepriv->quality = 0;
		return;
	}

	/* get the frontend status */
	if (fepriv->state & FESTATE_RETUNE) {
		s = 0;
	} else {
		if (fe->ops.read_status)
			fe->ops.read_status(fe, &s);
		if (s != fepriv->status) {
			dvb_frontend_add_event(fe, s);
			fepriv->status = s;
		}
	}

	/* if we're not tuned, and we have a lock, move to the TUNED state */
	if ((fepriv->state & FESTATE_WAITFORLOCK) && (s & FE_HAS_LOCK)) {
		dvb_frontend_swzigzag_update_delay(fepriv, s & FE_HAS_LOCK);
		fepriv->state = FESTATE_TUNED;

		/* if we're tuned, then we have determined the correct inversion */
		if ((!(fe->ops.info.caps & FE_CAN_INVERSION_AUTO)) &&
		    (c->inversion == INVERSION_AUTO)) {
			c->inversion = fepriv->inversion;
		}
		return;
	}

	/* if we are tuned already, check we're still locked */
	if (fepriv->state & FESTATE_TUNED) {
		dvb_frontend_swzigzag_update_delay(fepriv, s & FE_HAS_LOCK);

		/* we're tuned, and the lock is still good... */
		if (s & FE_HAS_LOCK) {
			return;
		} else { /* if we _WERE_ tuned, but now don't have a lock */
			fepriv->state = FESTATE_ZIGZAG_FAST;
			fepriv->started_auto_step = fepriv->auto_step;
			fepriv->check_wrapped = 0;
		}
	}

	/* don't actually do anything if we're in the LOSTLOCK state,
	 * the frontend is set to FE_CAN_RECOVER, and the max_drift is 0 */
	if ((fepriv->state & FESTATE_LOSTLOCK) &&
	    (fe->ops.info.caps & FE_CAN_RECOVER) && (fepriv->max_drift == 0)) {
		dvb_frontend_swzigzag_update_delay(fepriv, s & FE_HAS_LOCK);
		return;
	}

	/* don't do anything if we're in the DISEQC state, since this
	 * might be someone with a motorized dish controlled by DISEQC.
	 * If its actually a re-tune, there will be a SET_FRONTEND soon enough.	*/
	if (fepriv->state & FESTATE_DISEQC) {
		dvb_frontend_swzigzag_update_delay(fepriv, s & FE_HAS_LOCK);
		return;
	}

	/* if we're in the RETUNE state, set everything up for a brand
	 * new scan, keeping the current inversion setting, as the next
	 * tune is _very_ likely to require the same */
	if (fepriv->state & FESTATE_RETUNE) {
		fepriv->lnb_drift = 0;
		fepriv->auto_step = 0;
		fepriv->auto_sub_step = 0;
		fepriv->started_auto_step = 0;
		fepriv->check_wrapped = 0;
	}

	/* fast zigzag. */
	if ((fepriv->state & FESTATE_SEARCHING_FAST) || (fepriv->state & FESTATE_RETUNE)) {
		fepriv->delay = fepriv->min_delay;

		/* perform a tune */
		retval = dvb_frontend_swzigzag_autotune(fe,
							fepriv->check_wrapped);
		if (retval < 0) {
			return;
		} else if (retval) {
			/* OK, if we've run out of trials at the fast speed.
			 * Drop back to slow for the _next_ attempt */
			fepriv->state = FESTATE_SEARCHING_SLOW;
			fepriv->started_auto_step = fepriv->auto_step;
			return;
		}
		fepriv->check_wrapped = 1;

		/* if we've just retuned, enter the ZIGZAG_FAST state.
		 * This ensures we cannot return from an
		 * FE_SET_FRONTEND ioctl before the first frontend tune
		 * occurs */
		if (fepriv->state & FESTATE_RETUNE) {
			fepriv->state = FESTATE_TUNING_FAST;
		}
	}

	/* slow zigzag */
	if (fepriv->state & FESTATE_SEARCHING_SLOW) {
		dvb_frontend_swzigzag_update_delay(fepriv, s & FE_HAS_LOCK);

		/* Note: don't bother checking for wrapping; we stay in this
		 * state until we get a lock */
		dvb_frontend_swzigzag_autotune(fe, 0);
	}
}

static int dvb_frontend_is_exiting(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	if (fe->exit != DVB_FE_NO_EXIT)
		return 1;

	if (fepriv->dvbdev->writers == 1)
		if (time_after_eq(jiffies, fepriv->release_jiffies +
				  dvb_shutdown_timeout * HZ))
			return 1;

	return 0;
}

static int dvb_frontend_should_wakeup(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	if (fepriv->wakeup) {
		fepriv->wakeup = 0;
		return 1;
	}
	return dvb_frontend_is_exiting(fe);
}

static void dvb_frontend_wakeup(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	fepriv->wakeup = 1;
	wake_up_interruptible(&fepriv->wait_queue);
}

static int dvb_frontend_thread(void *data)
{
	struct dvb_frontend *fe = data;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	enum fe_status s = FE_NONE;
	enum dvbfe_algo algo;
	bool re_tune = false;
	bool semheld = false;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	fepriv->check_wrapped = 0;
	fepriv->quality = 0;
	fepriv->delay = 3*HZ;
	fepriv->status = 0;
	fepriv->wakeup = 0;
	fepriv->reinitialise = 0;

	dvb_frontend_init(fe);

	set_freezable();
	while (1) {
		up(&fepriv->sem);	    /* is locked when we enter the thread... */
restart:
		wait_event_interruptible_timeout(fepriv->wait_queue,
			dvb_frontend_should_wakeup(fe) || kthread_should_stop()
				|| freezing(current),
			fepriv->delay);

		if (kthread_should_stop() || dvb_frontend_is_exiting(fe)) {
			/* got signal or quitting */
			if (!down_interruptible(&fepriv->sem))
				semheld = true;
			fe->exit = DVB_FE_NORMAL_EXIT;
			break;
		}

		if (try_to_freeze())
			goto restart;

		if (down_interruptible(&fepriv->sem))
			break;

		if (fepriv->reinitialise) {
			dvb_frontend_init(fe);
			if (fe->ops.set_tone && fepriv->tone != -1)
				fe->ops.set_tone(fe, fepriv->tone);
			if (fe->ops.set_voltage && fepriv->voltage != -1)
				fe->ops.set_voltage(fe, fepriv->voltage);
			fepriv->reinitialise = 0;
		}

		/* do an iteration of the tuning loop */
		if (fe->ops.get_frontend_algo) {
			algo = fe->ops.get_frontend_algo(fe);
			switch (algo) {
			case DVBFE_ALGO_HW:
				dev_dbg(fe->dvb->device, "%s: Frontend ALGO = DVBFE_ALGO_HW\n", __func__);

				if (fepriv->state & FESTATE_RETUNE) {
					dev_dbg(fe->dvb->device, "%s: Retune requested, FESTATE_RETUNE\n", __func__);
					re_tune = true;
					fepriv->state = FESTATE_TUNED;
				} else {
					re_tune = false;
				}

				if (fe->ops.tune)
					fe->ops.tune(fe, re_tune, fepriv->tune_mode_flags, &fepriv->delay, &s);

				if (s != fepriv->status && !(fepriv->tune_mode_flags & FE_TUNE_MODE_ONESHOT)) {
					dev_dbg(fe->dvb->device, "%s: state changed, adding current state\n", __func__);
					dvb_frontend_add_event(fe, s);
					fepriv->status = s;
				}
				break;
			case DVBFE_ALGO_SW:
				dev_dbg(fe->dvb->device, "%s: Frontend ALGO = DVBFE_ALGO_SW\n", __func__);
				dvb_frontend_swzigzag(fe);
				break;
			case DVBFE_ALGO_CUSTOM:
				dev_dbg(fe->dvb->device, "%s: Frontend ALGO = DVBFE_ALGO_CUSTOM, state=%d\n", __func__, fepriv->state);
				if (fepriv->state & FESTATE_RETUNE) {
					dev_dbg(fe->dvb->device, "%s: Retune requested, FESTAT_RETUNE\n", __func__);
					fepriv->state = FESTATE_TUNED;
				}
				/* Case where we are going to search for a carrier
				 * User asked us to retune again for some reason, possibly
				 * requesting a search with a new set of parameters
				 */
				if (fepriv->algo_status & DVBFE_ALGO_SEARCH_AGAIN) {
					if (fe->ops.search) {
						fepriv->algo_status = fe->ops.search(fe);
						/* We did do a search as was requested, the flags are
						 * now unset as well and has the flags wrt to search.
						 */
					} else {
						fepriv->algo_status &= ~DVBFE_ALGO_SEARCH_AGAIN;
					}
				}
				/* Track the carrier if the search was successful */
				if (fepriv->algo_status != DVBFE_ALGO_SEARCH_SUCCESS) {
					fepriv->algo_status |= DVBFE_ALGO_SEARCH_AGAIN;
					fepriv->delay = HZ / 2;
				}
				dtv_property_legacy_params_sync(fe, c, &fepriv->parameters_out);
				fe->ops.read_status(fe, &s);
				if (s != fepriv->status) {
					dvb_frontend_add_event(fe, s); /* update event list */
					fepriv->status = s;
					if (!(s & FE_HAS_LOCK)) {
						fepriv->delay = HZ / 10;
						fepriv->algo_status |= DVBFE_ALGO_SEARCH_AGAIN;
					} else {
						fepriv->delay = 60 * HZ;
					}
				}
				break;
			default:
				dev_dbg(fe->dvb->device, "%s: UNDEFINED ALGO !\n", __func__);
				break;
			}
		} else {
			dvb_frontend_swzigzag(fe);
		}
	}

	if (dvb_powerdown_on_sleep) {
		if (fe->ops.set_voltage)
			fe->ops.set_voltage(fe, SEC_VOLTAGE_OFF);
		if (fe->ops.tuner_ops.sleep) {
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 1);
			fe->ops.tuner_ops.sleep(fe);
			if (fe->ops.i2c_gate_ctrl)
				fe->ops.i2c_gate_ctrl(fe, 0);
		}
		if (fe->ops.sleep)
			fe->ops.sleep(fe);
	}

	fepriv->thread = NULL;
	if (kthread_should_stop())
		fe->exit = DVB_FE_DEVICE_REMOVED;
	else
		fe->exit = DVB_FE_NO_EXIT;
	mb();

	if (semheld)
		up(&fepriv->sem);
	dvb_frontend_wakeup(fe);
	return 0;
}

static void dvb_frontend_stop(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if (fe->exit != DVB_FE_DEVICE_REMOVED)
		fe->exit = DVB_FE_NORMAL_EXIT;
	mb();

	if (!fepriv->thread)
		return;

	kthread_stop(fepriv->thread);

	sema_init(&fepriv->sem, 1);
	fepriv->state = FESTATE_IDLE;

	/* paranoia check in case a signal arrived */
	if (fepriv->thread)
		dev_warn(fe->dvb->device,
				"dvb_frontend_stop: warning: thread %p won't exit\n",
				fepriv->thread);
}

/*
 * Sleep for the amount of time given by add_usec parameter
 *
 * This needs to be as precise as possible, as it affects the detection of
 * the dish tone command at the satellite subsystem. The precision is improved
 * by using a scheduled msleep followed by udelay for the remainder.
 */
void dvb_frontend_sleep_until(ktime_t *waketime, u32 add_usec)
{
	s32 delta;

	*waketime = ktime_add_us(*waketime, add_usec);
	delta = ktime_us_delta(ktime_get_boottime(), *waketime);
	if (delta > 2500) {
		msleep((delta - 1500) / 1000);
		delta = ktime_us_delta(ktime_get_boottime(), *waketime);
	}
	if (delta > 0)
		udelay(delta);
}
EXPORT_SYMBOL(dvb_frontend_sleep_until);

static int dvb_frontend_start(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct task_struct *fe_thread;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if (fepriv->thread) {
		if (fe->exit == DVB_FE_NO_EXIT)
			return 0;
		else
			dvb_frontend_stop (fe);
	}

	if (signal_pending(current))
		return -EINTR;
	if (down_interruptible (&fepriv->sem))
		return -EINTR;

	fepriv->state = FESTATE_IDLE;
	fe->exit = DVB_FE_NO_EXIT;
	fepriv->thread = NULL;
	mb();

	fe_thread = kthread_run(dvb_frontend_thread, fe,
		"kdvb-ad-%i-fe-%i", fe->dvb->num,fe->id);
	if (IS_ERR(fe_thread)) {
		ret = PTR_ERR(fe_thread);
		dev_warn(fe->dvb->device,
				"dvb_frontend_start: failed to start kthread (%d)\n",
				ret);
		up(&fepriv->sem);
		return ret;
	}
	fepriv->thread = fe_thread;
	return 0;
}

static void dvb_frontend_get_frequency_limits(struct dvb_frontend *fe,
					u32 *freq_min, u32 *freq_max)
{
	*freq_min = max(fe->ops.info.frequency_min, fe->ops.tuner_ops.info.frequency_min);

	if (fe->ops.info.frequency_max == 0)
		*freq_max = fe->ops.tuner_ops.info.frequency_max;
	else if (fe->ops.tuner_ops.info.frequency_max == 0)
		*freq_max = fe->ops.info.frequency_max;
	else
		*freq_max = min(fe->ops.info.frequency_max, fe->ops.tuner_ops.info.frequency_max);

	if (*freq_min == 0 || *freq_max == 0)
		dev_warn(fe->dvb->device, "DVB: adapter %i frontend %u frequency limits undefined - fix the driver\n",
				fe->dvb->num, fe->id);
}

static int dvb_frontend_check_parameters(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	u32 freq_min;
	u32 freq_max;

	/* range check: frequency */
	dvb_frontend_get_frequency_limits(fe, &freq_min, &freq_max);
	if ((freq_min && c->frequency < freq_min) ||
	    (freq_max && c->frequency > freq_max)) {
		dev_warn(fe->dvb->device, "DVB: adapter %i frontend %i frequency %u out of range (%u..%u)\n",
				fe->dvb->num, fe->id, c->frequency,
				freq_min, freq_max);
		return -EINVAL;
	}

	/* range check: symbol rate */
	switch (c->delivery_system) {
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_TURBO:
	case SYS_DVBC_ANNEX_A:
	case SYS_DVBC_ANNEX_C:
		if ((fe->ops.info.symbol_rate_min &&
		     c->symbol_rate < fe->ops.info.symbol_rate_min) ||
		    (fe->ops.info.symbol_rate_max &&
		     c->symbol_rate > fe->ops.info.symbol_rate_max)) {
			dev_warn(fe->dvb->device, "DVB: adapter %i frontend %i symbol rate %u out of range (%u..%u)\n",
					fe->dvb->num, fe->id, c->symbol_rate,
					fe->ops.info.symbol_rate_min,
					fe->ops.info.symbol_rate_max);
			return -EINVAL;
		}
	default:
		break;
	}

	return 0;
}

static int dvb_frontend_clear_cache(struct dvb_frontend *fe)
{
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i;
	u32 delsys;

	delsys = c->delivery_system;
	memset(c, 0, offsetof(struct dtv_frontend_properties, strength));
	c->delivery_system = delsys;

	dev_dbg(fe->dvb->device, "%s: Clearing cache for delivery system %d\n",
			__func__, c->delivery_system);

	c->transmission_mode = TRANSMISSION_MODE_AUTO;
	c->bandwidth_hz = 0;	/* AUTO */
	c->guard_interval = GUARD_INTERVAL_AUTO;
	c->hierarchy = HIERARCHY_AUTO;
	c->symbol_rate = 0;
	c->code_rate_HP = FEC_AUTO;
	c->code_rate_LP = FEC_AUTO;
	c->fec_inner = FEC_AUTO;
	c->rolloff = ROLLOFF_AUTO;
	c->voltage = SEC_VOLTAGE_OFF;
	c->sectone = SEC_TONE_OFF;
	c->pilot = PILOT_AUTO;

	c->isdbt_partial_reception = 0;
	c->isdbt_sb_mode = 0;
	c->isdbt_sb_subchannel = 0;
	c->isdbt_sb_segment_idx = 0;
	c->isdbt_sb_segment_count = 0;
	c->isdbt_layer_enabled = 0;
	for (i = 0; i < 3; i++) {
		c->layer[i].fec = FEC_AUTO;
		c->layer[i].modulation = QAM_AUTO;
		c->layer[i].interleaving = 0;
		c->layer[i].segment_count = 0;
	}

	c->stream_id = NO_STREAM_ID_FILTER;

	switch (c->delivery_system) {
	case SYS_DVBS:
	case SYS_DVBS2:
	case SYS_TURBO:
		c->modulation = QPSK;   /* implied for DVB-S in legacy API */
		c->rolloff = ROLLOFF_35;/* implied for DVB-S */
		break;
	case SYS_ATSC:
		c->modulation = VSB_8;
		break;
	case SYS_ISDBS:
		c->symbol_rate = 28860000;
		c->rolloff = ROLLOFF_35;
		c->bandwidth_hz = c->symbol_rate / 100 * 135;
		break;
	default:
		c->modulation = QAM_AUTO;
		break;
	}

	c->lna = LNA_AUTO;

	return 0;
}

#define _DTV_CMD(n, s, b) \
[n] = { \
	.name = #n, \
	.cmd  = n, \
	.set  = s,\
	.buffer = b \
}

struct dtv_cmds_h {
	char	*name;		/* A display name for debugging purposes */

	__u32	cmd;		/* A unique ID */

	/* Flags */
	__u32	set:1;		/* Either a set or get property */
	__u32	buffer:1;	/* Does this property use the buffer? */
	__u32	reserved:30;	/* Align */
};

static struct dtv_cmds_h dtv_cmds[DTV_MAX_COMMAND + 1] = {
	_DTV_CMD(DTV_TUNE, 1, 0),
	_DTV_CMD(DTV_CLEAR, 1, 0),

	/* Set */
	_DTV_CMD(DTV_FREQUENCY, 1, 0),
	_DTV_CMD(DTV_BANDWIDTH_HZ, 1, 0),
	_DTV_CMD(DTV_MODULATION, 1, 0),
	_DTV_CMD(DTV_INVERSION, 1, 0),
	_DTV_CMD(DTV_DISEQC_MASTER, 1, 1),
	_DTV_CMD(DTV_SYMBOL_RATE, 1, 0),
	_DTV_CMD(DTV_INNER_FEC, 1, 0),
	_DTV_CMD(DTV_VOLTAGE, 1, 0),
	_DTV_CMD(DTV_TONE, 1, 0),
	_DTV_CMD(DTV_PILOT, 1, 0),
	_DTV_CMD(DTV_ROLLOFF, 1, 0),
	_DTV_CMD(DTV_DELIVERY_SYSTEM, 1, 0),
	_DTV_CMD(DTV_HIERARCHY, 1, 0),
	_DTV_CMD(DTV_CODE_RATE_HP, 1, 0),
	_DTV_CMD(DTV_CODE_RATE_LP, 1, 0),
	_DTV_CMD(DTV_GUARD_INTERVAL, 1, 0),
	_DTV_CMD(DTV_TRANSMISSION_MODE, 1, 0),
	_DTV_CMD(DTV_INTERLEAVING, 1, 0),

	_DTV_CMD(DTV_ISDBT_PARTIAL_RECEPTION, 1, 0),
	_DTV_CMD(DTV_ISDBT_SOUND_BROADCASTING, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SUBCHANNEL_ID, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SEGMENT_IDX, 1, 0),
	_DTV_CMD(DTV_ISDBT_SB_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYER_ENABLED, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERA_TIME_INTERLEAVING, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERB_TIME_INTERLEAVING, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_FEC, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_MODULATION, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_SEGMENT_COUNT, 1, 0),
	_DTV_CMD(DTV_ISDBT_LAYERC_TIME_INTERLEAVING, 1, 0),

	_DTV_CMD(DTV_STREAM_ID, 1, 0),
	_DTV_CMD(DTV_DVBT2_PLP_ID_LEGACY, 1, 0),
	_DTV_CMD(DTV_LNA, 1, 0),

	/* Get */
	_DTV_CMD(DTV_DISEQC_SLAVE_REPLY, 0, 1),
	_DTV_CMD(DTV_API_VERSION, 0, 0),

	_DTV_CMD(DTV_ENUM_DELSYS, 0, 0),

	_DTV_CMD(DTV_ATSCMH_PARADE_ID, 1, 0),
	_DTV_CMD(DTV_ATSCMH_RS_FRAME_ENSEMBLE, 1, 0),

	_DTV_CMD(DTV_ATSCMH_FIC_VER, 0, 0),
	_DTV_CMD(DTV_ATSCMH_NOG, 0, 0),
	_DTV_CMD(DTV_ATSCMH_TNOG, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SGN, 0, 0),
	_DTV_CMD(DTV_ATSCMH_PRC, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_FRAME_MODE, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_CODE_MODE_PRI, 0, 0),
	_DTV_CMD(DTV_ATSCMH_RS_CODE_MODE_SEC, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_BLOCK_MODE, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_A, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_B, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_C, 0, 0),
	_DTV_CMD(DTV_ATSCMH_SCCC_CODE_MODE_D, 0, 0),

	/* Statistics API */
	_DTV_CMD(DTV_STAT_SIGNAL_STRENGTH, 0, 0),
	_DTV_CMD(DTV_STAT_CNR, 0, 0),
	_DTV_CMD(DTV_STAT_PRE_ERROR_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_PRE_TOTAL_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_POST_ERROR_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_POST_TOTAL_BIT_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_ERROR_BLOCK_COUNT, 0, 0),
	_DTV_CMD(DTV_STAT_TOTAL_BLOCK_COUNT, 0, 0),
};

/* Synchronise the legacy tuning parameters into the cache, so that demodulator
 * drivers can use a single set_frontend tuning function, regardless of whether
 * it's being used for the legacy or new API, reducing code and complexity.
 */
static int dtv_property_cache_sync(struct dvb_frontend *fe,
				   struct dtv_frontend_properties *c,
				   const struct dvb_frontend_parameters *p)
{
	c->frequency = p->frequency;
	c->inversion = p->inversion;

	switch (dvbv3_type(c->delivery_system)) {
	case DVBV3_QPSK:
		dev_dbg(fe->dvb->device, "%s: Preparing QPSK req\n", __func__);
		c->symbol_rate = p->u.qpsk.symbol_rate;
		c->fec_inner = p->u.qpsk.fec_inner;
		break;
	case DVBV3_QAM:
		dev_dbg(fe->dvb->device, "%s: Preparing QAM req\n", __func__);
		c->symbol_rate = p->u.qam.symbol_rate;
		c->fec_inner = p->u.qam.fec_inner;
		c->modulation = p->u.qam.modulation;
		break;
	case DVBV3_OFDM:
		dev_dbg(fe->dvb->device, "%s: Preparing OFDM req\n", __func__);

		switch (p->u.ofdm.bandwidth) {
		case BANDWIDTH_10_MHZ:
			c->bandwidth_hz = 10000000;
			break;
		case BANDWIDTH_8_MHZ:
			c->bandwidth_hz = 8000000;
			break;
		case BANDWIDTH_7_MHZ:
			c->bandwidth_hz = 7000000;
			break;
		case BANDWIDTH_6_MHZ:
			c->bandwidth_hz = 6000000;
			break;
		case BANDWIDTH_5_MHZ:
			c->bandwidth_hz = 5000000;
			break;
		case BANDWIDTH_1_712_MHZ:
			c->bandwidth_hz = 1712000;
			break;
		case BANDWIDTH_AUTO:
			c->bandwidth_hz = 0;
		}

		c->code_rate_HP = p->u.ofdm.code_rate_HP;
		c->code_rate_LP = p->u.ofdm.code_rate_LP;
		c->modulation = p->u.ofdm.constellation;
		c->transmission_mode = p->u.ofdm.transmission_mode;
		c->guard_interval = p->u.ofdm.guard_interval;
		c->hierarchy = p->u.ofdm.hierarchy_information;
		break;
	case DVBV3_ATSC:
		dev_dbg(fe->dvb->device, "%s: Preparing ATSC req\n", __func__);
		c->modulation = p->u.vsb.modulation;
		if (c->delivery_system == SYS_ATSCMH)
			break;
		if ((c->modulation == VSB_8) || (c->modulation == VSB_16))
			c->delivery_system = SYS_ATSC;
		else
			c->delivery_system = SYS_DVBC_ANNEX_B;
		break;
	case DVBV3_UNKNOWN:
		dev_err(fe->dvb->device,
				"%s: doesn't know how to handle a DVBv3 call to delivery system %i\n",
				__func__, c->delivery_system);
		return -EINVAL;
	}

	return 0;
}

/* Ensure the cached values are set correctly in the frontend
 * legacy tuning structures, for the advanced tuning API.
 */
static int
dtv_property_legacy_params_sync(struct dvb_frontend *fe,
				const struct dtv_frontend_properties *c,
				struct dvb_frontend_parameters *p)
{
	p->frequency = c->frequency;
	p->inversion = c->inversion;

	switch (dvbv3_type(c->delivery_system)) {
	case DVBV3_UNKNOWN:
		dev_err(fe->dvb->device,
				"%s: doesn't know how to handle a DVBv3 call to delivery system %i\n",
				__func__, c->delivery_system);
		return -EINVAL;
	case DVBV3_QPSK:
		dev_dbg(fe->dvb->device, "%s: Preparing QPSK req\n", __func__);
		p->u.qpsk.symbol_rate = c->symbol_rate;
		p->u.qpsk.fec_inner = c->fec_inner;
		break;
	case DVBV3_QAM:
		dev_dbg(fe->dvb->device, "%s: Preparing QAM req\n", __func__);
		p->u.qam.symbol_rate = c->symbol_rate;
		p->u.qam.fec_inner = c->fec_inner;
		p->u.qam.modulation = c->modulation;
		break;
	case DVBV3_OFDM:
		dev_dbg(fe->dvb->device, "%s: Preparing OFDM req\n", __func__);
		switch (c->bandwidth_hz) {
		case 10000000:
			p->u.ofdm.bandwidth = BANDWIDTH_10_MHZ;
			break;
		case 8000000:
			p->u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
			break;
		case 7000000:
			p->u.ofdm.bandwidth = BANDWIDTH_7_MHZ;
			break;
		case 6000000:
			p->u.ofdm.bandwidth = BANDWIDTH_6_MHZ;
			break;
		case 5000000:
			p->u.ofdm.bandwidth = BANDWIDTH_5_MHZ;
			break;
		case 1712000:
			p->u.ofdm.bandwidth = BANDWIDTH_1_712_MHZ;
			break;
		case 0:
		default:
			p->u.ofdm.bandwidth = BANDWIDTH_AUTO;
		}
		p->u.ofdm.code_rate_HP = c->code_rate_HP;
		p->u.ofdm.code_rate_LP = c->code_rate_LP;
		p->u.ofdm.constellation = c->modulation;
		p->u.ofdm.transmission_mode = c->transmission_mode;
		p->u.ofdm.guard_interval = c->guard_interval;
		p->u.ofdm.hierarchy_information = c->hierarchy;
		break;
	case DVBV3_ATSC:
		dev_dbg(fe->dvb->device, "%s: Preparing VSB req\n", __func__);
		p->u.vsb.modulation = c->modulation;
		break;
	}
	return 0;
}

/**
 * dtv_get_frontend - calls a callback for retrieving DTV parameters
 * @fe:		struct dvb_frontend pointer
 * @c:		struct dtv_frontend_properties pointer (DVBv5 cache)
 * @p_out	struct dvb_frontend_parameters pointer (DVBv3 FE struct)
 *
 * This routine calls either the DVBv3 or DVBv5 get_frontend call.
 * If c is not null, it will update the DVBv5 cache struct pointed by it.
 * If p_out is not null, it will update the DVBv3 params pointed by it.
 */
static int dtv_get_frontend(struct dvb_frontend *fe,
			    struct dtv_frontend_properties *c,
			    struct dvb_frontend_parameters *p_out)
{
	int r;

	if (fe->ops.get_frontend) {
		r = fe->ops.get_frontend(fe, c);
		if (unlikely(r < 0))
			return r;
		if (p_out)
			dtv_property_legacy_params_sync(fe, c, p_out);
		return 0;
	}

	/* As everything is in cache, get_frontend fops are always supported */
	return 0;
}

static int dvb_frontend_handle_ioctl(struct file *file,
				     unsigned int cmd, void *parg);

static int dtv_property_process_get(struct dvb_frontend *fe,
				    const struct dtv_frontend_properties *c,
				    struct dtv_property *tvp,
				    struct file *file)
{
	int ncaps;

	switch(tvp->cmd) {
	case DTV_ENUM_DELSYS:
		ncaps = 0;
		while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
			tvp->u.buffer.data[ncaps] = fe->ops.delsys[ncaps];
			ncaps++;
		}
		tvp->u.buffer.len = ncaps;
		break;
	case DTV_FREQUENCY:
		tvp->u.data = c->frequency;
		break;
	case DTV_MODULATION:
		tvp->u.data = c->modulation;
		break;
	case DTV_BANDWIDTH_HZ:
		tvp->u.data = c->bandwidth_hz;
		break;
	case DTV_INVERSION:
		tvp->u.data = c->inversion;
		break;
	case DTV_SYMBOL_RATE:
		tvp->u.data = c->symbol_rate;
		break;
	case DTV_INNER_FEC:
		tvp->u.data = c->fec_inner;
		break;
	case DTV_PILOT:
		tvp->u.data = c->pilot;
		break;
	case DTV_ROLLOFF:
		tvp->u.data = c->rolloff;
		break;
	case DTV_DELIVERY_SYSTEM:
		tvp->u.data = c->delivery_system;
		break;
	case DTV_VOLTAGE:
		tvp->u.data = c->voltage;
		break;
	case DTV_TONE:
		tvp->u.data = c->sectone;
		break;
	case DTV_API_VERSION:
		tvp->u.data = (DVB_API_VERSION << 8) | DVB_API_VERSION_MINOR;
		break;
	case DTV_CODE_RATE_HP:
		tvp->u.data = c->code_rate_HP;
		break;
	case DTV_CODE_RATE_LP:
		tvp->u.data = c->code_rate_LP;
		break;
	case DTV_GUARD_INTERVAL:
		tvp->u.data = c->guard_interval;
		break;
	case DTV_TRANSMISSION_MODE:
		tvp->u.data = c->transmission_mode;
		break;
	case DTV_HIERARCHY:
		tvp->u.data = c->hierarchy;
		break;
	case DTV_INTERLEAVING:
		tvp->u.data = c->interleaving;
		break;

	/* ISDB-T Support here */
	case DTV_ISDBT_PARTIAL_RECEPTION:
		tvp->u.data = c->isdbt_partial_reception;
		break;
	case DTV_ISDBT_SOUND_BROADCASTING:
		tvp->u.data = c->isdbt_sb_mode;
		break;
	case DTV_ISDBT_SB_SUBCHANNEL_ID:
		tvp->u.data = c->isdbt_sb_subchannel;
		break;
	case DTV_ISDBT_SB_SEGMENT_IDX:
		tvp->u.data = c->isdbt_sb_segment_idx;
		break;
	case DTV_ISDBT_SB_SEGMENT_COUNT:
		tvp->u.data = c->isdbt_sb_segment_count;
		break;
	case DTV_ISDBT_LAYER_ENABLED:
		tvp->u.data = c->isdbt_layer_enabled;
		break;
	case DTV_ISDBT_LAYERA_FEC:
		tvp->u.data = c->layer[0].fec;
		break;
	case DTV_ISDBT_LAYERA_MODULATION:
		tvp->u.data = c->layer[0].modulation;
		break;
	case DTV_ISDBT_LAYERA_SEGMENT_COUNT:
		tvp->u.data = c->layer[0].segment_count;
		break;
	case DTV_ISDBT_LAYERA_TIME_INTERLEAVING:
		tvp->u.data = c->layer[0].interleaving;
		break;
	case DTV_ISDBT_LAYERB_FEC:
		tvp->u.data = c->layer[1].fec;
		break;
	case DTV_ISDBT_LAYERB_MODULATION:
		tvp->u.data = c->layer[1].modulation;
		break;
	case DTV_ISDBT_LAYERB_SEGMENT_COUNT:
		tvp->u.data = c->layer[1].segment_count;
		break;
	case DTV_ISDBT_LAYERB_TIME_INTERLEAVING:
		tvp->u.data = c->layer[1].interleaving;
		break;
	case DTV_ISDBT_LAYERC_FEC:
		tvp->u.data = c->layer[2].fec;
		break;
	case DTV_ISDBT_LAYERC_MODULATION:
		tvp->u.data = c->layer[2].modulation;
		break;
	case DTV_ISDBT_LAYERC_SEGMENT_COUNT:
		tvp->u.data = c->layer[2].segment_count;
		break;
	case DTV_ISDBT_LAYERC_TIME_INTERLEAVING:
		tvp->u.data = c->layer[2].interleaving;
		break;

	/* Multistream support */
	case DTV_STREAM_ID:
	case DTV_DVBT2_PLP_ID_LEGACY:
		tvp->u.data = c->stream_id;
		break;

	/* ATSC-MH */
	case DTV_ATSCMH_FIC_VER:
		tvp->u.data = fe->dtv_property_cache.atscmh_fic_ver;
		break;
	case DTV_ATSCMH_PARADE_ID:
		tvp->u.data = fe->dtv_property_cache.atscmh_parade_id;
		break;
	case DTV_ATSCMH_NOG:
		tvp->u.data = fe->dtv_property_cache.atscmh_nog;
		break;
	case DTV_ATSCMH_TNOG:
		tvp->u.data = fe->dtv_property_cache.atscmh_tnog;
		break;
	case DTV_ATSCMH_SGN:
		tvp->u.data = fe->dtv_property_cache.atscmh_sgn;
		break;
	case DTV_ATSCMH_PRC:
		tvp->u.data = fe->dtv_property_cache.atscmh_prc;
		break;
	case DTV_ATSCMH_RS_FRAME_MODE:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_frame_mode;
		break;
	case DTV_ATSCMH_RS_FRAME_ENSEMBLE:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_frame_ensemble;
		break;
	case DTV_ATSCMH_RS_CODE_MODE_PRI:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_code_mode_pri;
		break;
	case DTV_ATSCMH_RS_CODE_MODE_SEC:
		tvp->u.data = fe->dtv_property_cache.atscmh_rs_code_mode_sec;
		break;
	case DTV_ATSCMH_SCCC_BLOCK_MODE:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_block_mode;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_A:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_a;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_B:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_b;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_C:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_c;
		break;
	case DTV_ATSCMH_SCCC_CODE_MODE_D:
		tvp->u.data = fe->dtv_property_cache.atscmh_sccc_code_mode_d;
		break;

	case DTV_LNA:
		tvp->u.data = c->lna;
		break;

	/* Fill quality measures */
	case DTV_STAT_SIGNAL_STRENGTH:
		tvp->u.st = c->strength;
		break;
	case DTV_STAT_CNR:
		tvp->u.st = c->cnr;
		break;
	case DTV_STAT_PRE_ERROR_BIT_COUNT:
		tvp->u.st = c->pre_bit_error;
		break;
	case DTV_STAT_PRE_TOTAL_BIT_COUNT:
		tvp->u.st = c->pre_bit_count;
		break;
	case DTV_STAT_POST_ERROR_BIT_COUNT:
		tvp->u.st = c->post_bit_error;
		break;
	case DTV_STAT_POST_TOTAL_BIT_COUNT:
		tvp->u.st = c->post_bit_count;
		break;
	case DTV_STAT_ERROR_BLOCK_COUNT:
		tvp->u.st = c->block_error;
		break;
	case DTV_STAT_TOTAL_BLOCK_COUNT:
		tvp->u.st = c->block_count;
		break;
	default:
		dev_dbg(fe->dvb->device,
			"%s: FE property %d doesn't exist\n",
			__func__, tvp->cmd);
		return -EINVAL;
	}

	if (!dtv_cmds[tvp->cmd].buffer)
		dev_dbg(fe->dvb->device,
			"%s: GET cmd 0x%08x (%s) = 0x%08x\n",
			__func__, tvp->cmd, dtv_cmds[tvp->cmd].name,
			tvp->u.data);
	else
		dev_dbg(fe->dvb->device,
			"%s: GET cmd 0x%08x (%s) len %d: %*ph\n",
			__func__,
			tvp->cmd, dtv_cmds[tvp->cmd].name,
			tvp->u.buffer.len,
			tvp->u.buffer.len, tvp->u.buffer.data);

	return 0;
}

static int dtv_set_frontend(struct dvb_frontend *fe);

static bool is_dvbv3_delsys(u32 delsys)
{
	return (delsys == SYS_DVBT) || (delsys == SYS_DVBC_ANNEX_A) ||
	       (delsys == SYS_DVBS) || (delsys == SYS_ATSC);
}

/**
 * emulate_delivery_system - emulate a DVBv5 delivery system with a DVBv3 type
 * @fe:			struct frontend;
 * @delsys:			DVBv5 type that will be used for emulation
 *
 * Provides emulation for delivery systems that are compatible with the old
 * DVBv3 call. Among its usages, it provices support for ISDB-T, and allows
 * using a DVB-S2 only frontend just like it were a DVB-S, if the frontent
 * parameters are compatible with DVB-S spec.
 */
static int emulate_delivery_system(struct dvb_frontend *fe, u32 delsys)
{
	int i;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	c->delivery_system = delsys;

	/*
	 * If the call is for ISDB-T, put it into full-seg, auto mode, TV
	 */
	if (c->delivery_system == SYS_ISDBT) {
		dev_dbg(fe->dvb->device,
			"%s: Using defaults for SYS_ISDBT\n",
			__func__);

		if (!c->bandwidth_hz)
			c->bandwidth_hz = 6000000;

		c->isdbt_partial_reception = 0;
		c->isdbt_sb_mode = 0;
		c->isdbt_sb_subchannel = 0;
		c->isdbt_sb_segment_idx = 0;
		c->isdbt_sb_segment_count = 0;
		c->isdbt_layer_enabled = 7;
		for (i = 0; i < 3; i++) {
			c->layer[i].fec = FEC_AUTO;
			c->layer[i].modulation = QAM_AUTO;
			c->layer[i].interleaving = 0;
			c->layer[i].segment_count = 0;
		}
	}
	dev_dbg(fe->dvb->device, "%s: change delivery system on cache to %d\n",
		__func__, c->delivery_system);

	return 0;
}

/**
 * dvbv5_set_delivery_system - Sets the delivery system for a DVBv5 API call
 * @fe:			frontend struct
 * @desired_system:	delivery system requested by the user
 *
 * A DVBv5 call know what's the desired system it wants. So, set it.
 *
 * There are, however, a few known issues with early DVBv5 applications that
 * are also handled by this logic:
 *
 * 1) Some early apps use SYS_UNDEFINED as the desired delivery system.
 *    This is an API violation, but, as we don't want to break userspace,
 *    convert it to the first supported delivery system.
 * 2) Some apps might be using a DVBv5 call in a wrong way, passing, for
 *    example, SYS_DVBT instead of SYS_ISDBT. This is because early usage of
 *    ISDB-T provided backward compat with DVB-T.
 */
static int dvbv5_set_delivery_system(struct dvb_frontend *fe,
				     u32 desired_system)
{
	int ncaps;
	u32 delsys = SYS_UNDEFINED;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	enum dvbv3_emulation_type type;

	/*
	 * It was reported that some old DVBv5 applications were
	 * filling delivery_system with SYS_UNDEFINED. If this happens,
	 * assume that the application wants to use the first supported
	 * delivery system.
	 */
	if (desired_system == SYS_UNDEFINED)
		desired_system = fe->ops.delsys[0];

	/*
	 * This is a DVBv5 call. So, it likely knows the supported
	 * delivery systems. So, check if the desired delivery system is
	 * supported
	 */
	ncaps = 0;
	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		if (fe->ops.delsys[ncaps] == desired_system) {
			c->delivery_system = desired_system;
			dev_dbg(fe->dvb->device,
					"%s: Changing delivery system to %d\n",
					__func__, desired_system);
			return 0;
		}
		ncaps++;
	}

	/*
	 * The requested delivery system isn't supported. Maybe userspace
	 * is requesting a DVBv3 compatible delivery system.
	 *
	 * The emulation only works if the desired system is one of the
	 * delivery systems supported by DVBv3 API
	 */
	if (!is_dvbv3_delsys(desired_system)) {
		dev_dbg(fe->dvb->device,
			"%s: Delivery system %d not supported.\n",
			__func__, desired_system);
		return -EINVAL;
	}

	type = dvbv3_type(desired_system);

	/*
	* Get the last non-DVBv3 delivery system that has the same type
	* of the desired system
	*/
	ncaps = 0;
	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		if (dvbv3_type(fe->ops.delsys[ncaps]) == type)
			delsys = fe->ops.delsys[ncaps];
		ncaps++;
	}

	/* There's nothing compatible with the desired delivery system */
	if (delsys == SYS_UNDEFINED) {
		dev_dbg(fe->dvb->device,
			"%s: Delivery system %d not supported on emulation mode.\n",
			__func__, desired_system);
		return -EINVAL;
	}

	dev_dbg(fe->dvb->device,
		"%s: Using delivery system %d emulated as if it were %d\n",
		__func__, delsys, desired_system);

	return emulate_delivery_system(fe, desired_system);
}

/**
 * dvbv3_set_delivery_system - Sets the delivery system for a DVBv3 API call
 * @fe:	frontend struct
 *
 * A DVBv3 call doesn't know what's the desired system it wants. It also
 * doesn't allow to switch between different types. Due to that, userspace
 * should use DVBv5 instead.
 * However, in order to avoid breaking userspace API, limited backward
 * compatibility support is provided.
 *
 * There are some delivery systems that are incompatible with DVBv3 calls.
 *
 * This routine should work fine for frontends that support just one delivery
 * system.
 *
 * For frontends that support multiple frontends:
 * 1) It defaults to use the first supported delivery system. There's an
 *    userspace application that allows changing it at runtime;
 *
 * 2) If the current delivery system is not compatible with DVBv3, it gets
 *    the first one that it is compatible.
 *
 * NOTE: in order for this to work with applications like Kaffeine that
 *	uses a DVBv5 call for DVB-S2 and a DVBv3 call to go back to
 *	DVB-S, drivers that support both DVB-S and DVB-S2 should have the
 *	SYS_DVBS entry before the SYS_DVBS2, otherwise it won't switch back
 *	to DVB-S.
 */
static int dvbv3_set_delivery_system(struct dvb_frontend *fe)
{
	int ncaps;
	u32 delsys = SYS_UNDEFINED;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	/* If not set yet, defaults to the first supported delivery system */
	if (c->delivery_system == SYS_UNDEFINED)
		c->delivery_system = fe->ops.delsys[0];

	/*
	 * Trivial case: just use the current one, if it already a DVBv3
	 * delivery system
	 */
	if (is_dvbv3_delsys(c->delivery_system)) {
		dev_dbg(fe->dvb->device,
				"%s: Using delivery system to %d\n",
				__func__, c->delivery_system);
		return 0;
	}

	/*
	 * Seek for the first delivery system that it is compatible with a
	 * DVBv3 standard
	 */
	ncaps = 0;
	while (ncaps < MAX_DELSYS && fe->ops.delsys[ncaps]) {
		if (dvbv3_type(fe->ops.delsys[ncaps]) != DVBV3_UNKNOWN) {
			delsys = fe->ops.delsys[ncaps];
			break;
		}
		ncaps++;
	}
	if (delsys == SYS_UNDEFINED) {
		dev_dbg(fe->dvb->device,
			"%s: Couldn't find a delivery system that works with FE_SET_FRONTEND\n",
			__func__);
		return -EINVAL;
	}
	return emulate_delivery_system(fe, delsys);
}

/**
 * dtv_property_process_set -  Sets a single DTV property
 * @fe:		Pointer to &struct dvb_frontend
 * @file:	Pointer to &struct file
 * @cmd:	Digital TV command
 * @data:	An unsigned 32-bits number
 *
 * This routine assigns the property
 * value to the corresponding member of
 * &struct dtv_frontend_properties
 *
 * Returns:
 * Zero on success, negative errno on failure.
 */
static int dtv_property_process_set(struct dvb_frontend *fe,
					struct file *file,
					u32 cmd, u32 data)
{
	int r = 0;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;

	/** Dump DTV command name and value*/
	if (!cmd || cmd > DTV_MAX_COMMAND)
		dev_warn(fe->dvb->device, "%s: SET cmd 0x%08x undefined\n",
				 __func__, cmd);
	else
		dev_dbg(fe->dvb->device,
				"%s: SET cmd 0x%08x (%s) to 0x%08x\n",
				__func__, cmd, dtv_cmds[cmd].name, data);
	switch (cmd) {
	case DTV_CLEAR:
		/*
		 * Reset a cache of data specific to the frontend here. This does
		 * not effect hardware.
		 */
		dvb_frontend_clear_cache(fe);
		break;
	case DTV_TUNE:
		/*
		 * Use the cached Digital TV properties to tune the
		 * frontend
		 */
		dev_dbg(fe->dvb->device,
			"%s: Setting the frontend from property cache\n",
			__func__);

		r = dtv_set_frontend(fe);
		break;
	case DTV_FREQUENCY:
		c->frequency = data;
		break;
	case DTV_MODULATION:
		c->modulation = data;
		break;
	case DTV_BANDWIDTH_HZ:
		c->bandwidth_hz = data;
		break;
	case DTV_INVERSION:
		c->inversion = data;
		break;
	case DTV_SYMBOL_RATE:
		c->symbol_rate = data;
		break;
	case DTV_INNER_FEC:
		c->fec_inner = data;
		break;
	case DTV_PILOT:
		c->pilot = data;
		break;
	case DTV_ROLLOFF:
		c->rolloff = data;
		break;
	case DTV_DELIVERY_SYSTEM:
		r = dvbv5_set_delivery_system(fe, data);
		break;
	case DTV_VOLTAGE:
		c->voltage = data;
		r = dvb_frontend_handle_ioctl(file, FE_SET_VOLTAGE,
			(void *)c->voltage);
		break;
	case DTV_TONE:
		c->sectone = data;
		r = dvb_frontend_handle_ioctl(file, FE_SET_TONE,
			(void *)c->sectone);
		break;
	case DTV_CODE_RATE_HP:
		c->code_rate_HP = data;
		break;
	case DTV_CODE_RATE_LP:
		c->code_rate_LP = data;
		break;
	case DTV_GUARD_INTERVAL:
		c->guard_interval = data;
		break;
	case DTV_TRANSMISSION_MODE:
		c->transmission_mode = data;
		break;
	case DTV_HIERARCHY:
		c->hierarchy = data;
		break;
	case DTV_INTERLEAVING:
		c->interleaving = data;
		break;

	/* ISDB-T Support here */
	case DTV_ISDBT_PARTIAL_RECEPTION:
		c->isdbt_partial_reception = data;
		break;
	case DTV_ISDBT_SOUND_BROADCASTING:
		c->isdbt_sb_mode = data;
		break;
	case DTV_ISDBT_SB_SUBCHANNEL_ID:
		c->isdbt_sb_subchannel = data;
		break;
	case DTV_ISDBT_SB_SEGMENT_IDX:
		c->isdbt_sb_segment_idx = data;
		break;
	case DTV_ISDBT_SB_SEGMENT_COUNT:
		c->isdbt_sb_segment_count = data;
		break;
	case DTV_ISDBT_LAYER_ENABLED:
		c->isdbt_layer_enabled = data;
		break;
	case DTV_ISDBT_LAYERA_FEC:
		c->layer[0].fec = data;
		break;
	case DTV_ISDBT_LAYERA_MODULATION:
		c->layer[0].modulation = data;
		break;
	case DTV_ISDBT_LAYERA_SEGMENT_COUNT:
		c->layer[0].segment_count = data;
		break;
	case DTV_ISDBT_LAYERA_TIME_INTERLEAVING:
		c->layer[0].interleaving = data;
		break;
	case DTV_ISDBT_LAYERB_FEC:
		c->layer[1].fec = data;
		break;
	case DTV_ISDBT_LAYERB_MODULATION:
		c->layer[1].modulation = data;
		break;
	case DTV_ISDBT_LAYERB_SEGMENT_COUNT:
		c->layer[1].segment_count = data;
		break;
	case DTV_ISDBT_LAYERB_TIME_INTERLEAVING:
		c->layer[1].interleaving = data;
		break;
	case DTV_ISDBT_LAYERC_FEC:
		c->layer[2].fec = data;
		break;
	case DTV_ISDBT_LAYERC_MODULATION:
		c->layer[2].modulation = data;
		break;
	case DTV_ISDBT_LAYERC_SEGMENT_COUNT:
		c->layer[2].segment_count = data;
		break;
	case DTV_ISDBT_LAYERC_TIME_INTERLEAVING:
		c->layer[2].interleaving = data;
		break;

	/* Multistream support */
	case DTV_STREAM_ID:
	case DTV_DVBT2_PLP_ID_LEGACY:
		c->stream_id = data;
		break;

	/* ATSC-MH */
	case DTV_ATSCMH_PARADE_ID:
		fe->dtv_property_cache.atscmh_parade_id = data;
		break;
	case DTV_ATSCMH_RS_FRAME_ENSEMBLE:
		fe->dtv_property_cache.atscmh_rs_frame_ensemble = data;
		break;

	case DTV_LNA:
		c->lna = data;
		if (fe->ops.set_lna)
			r = fe->ops.set_lna(fe);
		if (r < 0)
			c->lna = LNA_AUTO;
		break;

	default:
		return -EINVAL;
	}

	return r;
}

static int dvb_frontend_ioctl(struct file *file, unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	int err;

	dev_dbg(fe->dvb->device, "%s: (%d)\n", __func__, _IOC_NR(cmd));
	if (down_interruptible(&fepriv->sem))
		return -ERESTARTSYS;

	if (fe->exit != DVB_FE_NO_EXIT) {
		up(&fepriv->sem);
		return -ENODEV;
	}

	/*
	 * If the frontend is opened in read-only mode, only the ioctls
	 * that don't interfere with the tune logic should be accepted.
	 * That allows an external application to monitor the DVB QoS and
	 * statistics parameters.
	 *
	 * That matches all _IOR() ioctls, except for two special cases:
	 *   - FE_GET_EVENT is part of the tuning logic on a DVB application;
	 *   - FE_DISEQC_RECV_SLAVE_REPLY is part of DiSEqC 2.0
	 *     setup
	 * So, those two ioctls should also return -EPERM, as otherwise
	 * reading from them would interfere with a DVB tune application
	 */
	if ((file->f_flags & O_ACCMODE) == O_RDONLY
	    && (_IOC_DIR(cmd) != _IOC_READ
		|| cmd == FE_GET_EVENT
		|| cmd == FE_DISEQC_RECV_SLAVE_REPLY)) {
		up(&fepriv->sem);
		return -EPERM;
	}

	err = dvb_frontend_handle_ioctl(file, cmd, parg);

	up(&fepriv->sem);
	return err;
}

static int dtv_set_frontend(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	struct dvb_frontend_tune_settings fetunesettings;
	u32 rolloff = 0;

	if (dvb_frontend_check_parameters(fe) < 0)
		return -EINVAL;

	/*
	 * Initialize output parameters to match the values given by
	 * the user. FE_SET_FRONTEND triggers an initial frontend event
	 * with status = 0, which copies output parameters to userspace.
	 */
	dtv_property_legacy_params_sync(fe, c, &fepriv->parameters_out);

	/*
	 * Be sure that the bandwidth will be filled for all
	 * non-satellite systems, as tuners need to know what
	 * low pass/Nyquist half filter should be applied, in
	 * order to avoid inter-channel noise.
	 *
	 * ISDB-T and DVB-T/T2 already sets bandwidth.
	 * ATSC and DVB-C don't set, so, the core should fill it.
	 *
	 * On DVB-C Annex A and C, the bandwidth is a function of
	 * the roll-off and symbol rate. Annex B defines different
	 * roll-off factors depending on the modulation. Fortunately,
	 * Annex B is only used with 6MHz, so there's no need to
	 * calculate it.
	 *
	 * While not officially supported, a side effect of handling it at
	 * the cache level is that a program could retrieve the bandwidth
	 * via DTV_BANDWIDTH_HZ, which may be useful for test programs.
	 */
	switch (c->delivery_system) {
	case SYS_ATSC:
	case SYS_DVBC_ANNEX_B:
		c->bandwidth_hz = 6000000;
		break;
	case SYS_DVBC_ANNEX_A:
		rolloff = 115;
		break;
	case SYS_DVBC_ANNEX_C:
		rolloff = 113;
		break;
	case SYS_DVBS:
	case SYS_TURBO:
	case SYS_ISDBS:
		rolloff = 135;
		break;
	case SYS_DVBS2:
		switch (c->rolloff) {
		case ROLLOFF_20:
			rolloff = 120;
			break;
		case ROLLOFF_25:
			rolloff = 125;
			break;
		default:
		case ROLLOFF_35:
			rolloff = 135;
		}
		break;
	default:
		break;
	}
	if (rolloff)
		c->bandwidth_hz = mult_frac(c->symbol_rate, rolloff, 100);

	/* force auto frequency inversion if requested */
	if (dvb_force_auto_inversion)
		c->inversion = INVERSION_AUTO;

	/*
	 * without hierarchical coding code_rate_LP is irrelevant,
	 * so we tolerate the otherwise invalid FEC_NONE setting
	 */
	if (c->hierarchy == HIERARCHY_NONE && c->code_rate_LP == FEC_NONE)
		c->code_rate_LP = FEC_AUTO;

	/* get frontend-specific tuning settings */
	memset(&fetunesettings, 0, sizeof(struct dvb_frontend_tune_settings));
	if (fe->ops.get_tune_settings && (fe->ops.get_tune_settings(fe, &fetunesettings) == 0)) {
		fepriv->min_delay = (fetunesettings.min_delay_ms * HZ) / 1000;
		fepriv->max_drift = fetunesettings.max_drift;
		fepriv->step_size = fetunesettings.step_size;
	} else {
		/* default values */
		switch (c->delivery_system) {
		case SYS_DVBS:
		case SYS_DVBS2:
		case SYS_ISDBS:
		case SYS_TURBO:
		case SYS_DVBC_ANNEX_A:
		case SYS_DVBC_ANNEX_C:
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = c->symbol_rate / 16000;
			fepriv->max_drift = c->symbol_rate / 2000;
			break;
		case SYS_DVBT:
		case SYS_DVBT2:
		case SYS_ISDBT:
		case SYS_DTMB:
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = fe->ops.info.frequency_stepsize * 2;
			fepriv->max_drift = (fe->ops.info.frequency_stepsize * 2) + 1;
			break;
		default:
			/*
			 * FIXME: This sounds wrong! if freqency_stepsize is
			 * defined by the frontend, why not use it???
			 */
			fepriv->min_delay = HZ / 20;
			fepriv->step_size = 0; /* no zigzag */
			fepriv->max_drift = 0;
			break;
		}
	}
	if (dvb_override_tune_delay > 0)
		fepriv->min_delay = (dvb_override_tune_delay * HZ) / 1000;

	fepriv->state = FESTATE_RETUNE;

	/* Request the search algorithm to search */
	fepriv->algo_status |= DVBFE_ALGO_SEARCH_AGAIN;

	dvb_frontend_clear_events(fe);
	dvb_frontend_add_event(fe, 0);
	dvb_frontend_wakeup(fe);
	fepriv->status = 0;

	return 0;
}


static int dvb_frontend_handle_ioctl(struct file *file,
				     unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	int i, err;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	switch (cmd) {
	case FE_SET_PROPERTY: {
		struct dtv_properties *tvps = parg;
		struct dtv_property *tvp = NULL;

		dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
			__func__, tvps->num);
		dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
			__func__, tvps->props);

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user(tvps->props, tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		for (i = 0; i < tvps->num; i++) {
			err = dtv_property_process_set(fe, file,
							(tvp + i)->cmd,
							(tvp + i)->u.data);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}
		kfree(tvp);
		break;
	}
	case FE_GET_PROPERTY: {
		struct dtv_properties *tvps = parg;
		struct dtv_property *tvp = NULL;
		struct dtv_frontend_properties getp = fe->dtv_property_cache;

		dev_dbg(fe->dvb->device, "%s: properties.num = %d\n",
			__func__, tvps->num);
		dev_dbg(fe->dvb->device, "%s: properties.props = %p\n",
			__func__, tvps->props);

		/*
		 * Put an arbitrary limit on the number of messages that can
		 * be sent at once
		 */
		if (!tvps->num || (tvps->num > DTV_IOCTL_MAX_MSGS))
			return -EINVAL;

		tvp = memdup_user(tvps->props, tvps->num * sizeof(*tvp));
		if (IS_ERR(tvp))
			return PTR_ERR(tvp);

		/*
		 * Let's use our own copy of property cache, in order to
		 * avoid mangling with DTV zigzag logic, as drivers might
		 * return crap, if they don't check if the data is available
		 * before updating the properties cache.
		 */
		if (fepriv->state != FESTATE_IDLE) {
			err = dtv_get_frontend(fe, &getp, NULL);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}
		for (i = 0; i < tvps->num; i++) {
			err = dtv_property_process_get(fe, &getp,
						       tvp + i, file);
			if (err < 0) {
				kfree(tvp);
				return err;
			}
		}

		if (copy_to_user((void __user *)tvps->props, tvp,
				 tvps->num * sizeof(struct dtv_property))) {
			kfree(tvp);
			return -EFAULT;
		}
		kfree(tvp);
		break;
	}

	case FE_GET_INFO: {
		struct dvb_frontend_info* info = parg;

		memcpy(info, &fe->ops.info, sizeof(struct dvb_frontend_info));
		dvb_frontend_get_frequency_limits(fe, &info->frequency_min, &info->frequency_max);

		/*
		 * Associate the 4 delivery systems supported by DVBv3
		 * API with their DVBv5 counterpart. For the other standards,
		 * use the closest type, assuming that it would hopefully
		 * work with a DVBv3 application.
		 * It should be noticed that, on multi-frontend devices with
		 * different types (terrestrial and cable, for example),
		 * a pure DVBv3 application won't be able to use all delivery
		 * systems. Yet, changing the DVBv5 cache to the other delivery
		 * system should be enough for making it work.
		 */
		switch (dvbv3_type(c->delivery_system)) {
		case DVBV3_QPSK:
			info->type = FE_QPSK;
			break;
		case DVBV3_ATSC:
			info->type = FE_ATSC;
			break;
		case DVBV3_QAM:
			info->type = FE_QAM;
			break;
		case DVBV3_OFDM:
			info->type = FE_OFDM;
			break;
		default:
			dev_err(fe->dvb->device,
					"%s: doesn't know how to handle a DVBv3 call to delivery system %i\n",
					__func__, c->delivery_system);
			fe->ops.info.type = FE_OFDM;
		}
		dev_dbg(fe->dvb->device, "%s: current delivery system on cache: %d, V3 type: %d\n",
				 __func__, c->delivery_system, fe->ops.info.type);

		/* Set CAN_INVERSION_AUTO bit on in other than oneshot mode */
		if (!(fepriv->tune_mode_flags & FE_TUNE_MODE_ONESHOT))
			info->caps |= FE_CAN_INVERSION_AUTO;
		err = 0;
		break;
	}

	case FE_READ_STATUS: {
		enum fe_status *status = parg;

		/* if retune was requested but hasn't occurred yet, prevent
		 * that user get signal state from previous tuning */
		if (fepriv->state == FESTATE_RETUNE ||
		    fepriv->state == FESTATE_ERROR) {
			err=0;
			*status = 0;
			break;
		}

		if (fe->ops.read_status)
			err = fe->ops.read_status(fe, status);
		break;
	}

	case FE_DISEQC_RESET_OVERLOAD:
		if (fe->ops.diseqc_reset_overload) {
			err = fe->ops.diseqc_reset_overload(fe);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
		if (fe->ops.diseqc_send_master_cmd) {
			struct dvb_diseqc_master_cmd *cmd = parg;

			if (cmd->msg_len > sizeof(cmd->msg)) {
				err = -EINVAL;
				break;
			}
			err = fe->ops.diseqc_send_master_cmd(fe, cmd);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_BURST:
		if (fe->ops.diseqc_send_burst) {
			err = fe->ops.diseqc_send_burst(fe,
						(enum fe_sec_mini_cmd)parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_SET_TONE:
		if (fe->ops.set_tone) {
			err = fe->ops.set_tone(fe,
					       (enum fe_sec_tone_mode)parg);
			fepriv->tone = (enum fe_sec_tone_mode)parg;
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_SET_VOLTAGE:
		if (fe->ops.set_voltage) {
			err = fe->ops.set_voltage(fe,
						  (enum fe_sec_voltage)parg);
			fepriv->voltage = (enum fe_sec_voltage)parg;
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_RECV_SLAVE_REPLY:
		if (fe->ops.diseqc_recv_slave_reply)
			err = fe->ops.diseqc_recv_slave_reply(fe, parg);
		break;

	case FE_ENABLE_HIGH_LNB_VOLTAGE:
		if (fe->ops.enable_high_lnb_voltage)
			err = fe->ops.enable_high_lnb_voltage(fe, (long) parg);
		break;

	case FE_SET_FRONTEND_TUNE_MODE:
		fepriv->tune_mode_flags = (unsigned long) parg;
		err = 0;
		break;

	/* DEPRECATED dish control ioctls */

	case FE_DISHNETWORK_SEND_LEGACY_CMD:
		if (fe->ops.dishnetwork_send_legacy_command) {
			err = fe->ops.dishnetwork_send_legacy_command(fe,
							 (unsigned long)parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		} else if (fe->ops.set_voltage) {
			/*
			 * NOTE: This is a fallback condition.  Some frontends
			 * (stv0299 for instance) take longer than 8msec to
			 * respond to a set_voltage command.  Those switches
			 * need custom routines to switch properly.  For all
			 * other frontends, the following should work ok.
			 * Dish network legacy switches (as used by Dish500)
			 * are controlled by sending 9-bit command words
			 * spaced 8msec apart.
			 * the actual command word is switch/port dependent
			 * so it is up to the userspace application to send
			 * the right command.
			 * The command must always start with a '0' after
			 * initialization, so parg is 8 bits and does not
			 * include the initialization or start bit
			 */
			unsigned long swcmd = ((unsigned long) parg) << 1;
			ktime_t nexttime;
			ktime_t tv[10];
			int i;
			u8 last = 1;
			if (dvb_frontend_debug)
				dprintk("%s switch command: 0x%04lx\n",
					__func__, swcmd);
			nexttime = ktime_get_boottime();
			if (dvb_frontend_debug)
				tv[0] = nexttime;
			/* before sending a command, initialize by sending
			 * a 32ms 18V to the switch
			 */
			fe->ops.set_voltage(fe, SEC_VOLTAGE_18);
			dvb_frontend_sleep_until(&nexttime, 32000);

			for (i = 0; i < 9; i++) {
				if (dvb_frontend_debug)
					tv[i+1] = ktime_get_boottime();
				if ((swcmd & 0x01) != last) {
					/* set voltage to (last ? 13V : 18V) */
					fe->ops.set_voltage(fe, (last) ? SEC_VOLTAGE_13 : SEC_VOLTAGE_18);
					last = (last) ? 0 : 1;
				}
				swcmd = swcmd >> 1;
				if (i != 8)
					dvb_frontend_sleep_until(&nexttime, 8000);
			}
			if (dvb_frontend_debug) {
				dprintk("%s(%d): switch delay (should be 32k followed by all 8k)\n",
					__func__, fe->dvb->num);
				for (i = 1; i < 10; i++)
					pr_info("%d: %d\n", i,
					(int) ktime_us_delta(tv[i], tv[i-1]));
			}
			err = 0;
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	/* DEPRECATED statistics ioctls */

	case FE_READ_BER:
		if (fe->ops.read_ber) {
			if (fepriv->thread)
				err = fe->ops.read_ber(fe, parg);
			else
				err = -EAGAIN;
		}
		break;

	case FE_READ_SIGNAL_STRENGTH:
		if (fe->ops.read_signal_strength) {
			if (fepriv->thread)
				err = fe->ops.read_signal_strength(fe, parg);
			else
				err = -EAGAIN;
		}
		break;

	case FE_READ_SNR:
		if (fe->ops.read_snr) {
			if (fepriv->thread)
				err = fe->ops.read_snr(fe, parg);
			else
				err = -EAGAIN;
		}
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		if (fe->ops.read_ucblocks) {
			if (fepriv->thread)
				err = fe->ops.read_ucblocks(fe, parg);
			else
				err = -EAGAIN;
		}
		break;

	/* DEPRECATED DVBv3 ioctls */

	case FE_SET_FRONTEND:
		err = dvbv3_set_delivery_system(fe);
		if (err)
			break;

		err = dtv_property_cache_sync(fe, c, parg);
		if (err)
			break;
		err = dtv_set_frontend(fe);
		break;
	case FE_GET_EVENT:
		err = dvb_frontend_get_event (fe, parg, file->f_flags);
		break;

	case FE_GET_FRONTEND: {
		struct dtv_frontend_properties getp = fe->dtv_property_cache;

		/*
		 * Let's use our own copy of property cache, in order to
		 * avoid mangling with DTV zigzag logic, as drivers might
		 * return crap, if they don't check if the data is available
		 * before updating the properties cache.
		 */
		err = dtv_get_frontend(fe, &getp, parg);
		break;
	}

	default:
		return -ENOTSUPP;
	} /* switch */

	return err;
}


static unsigned int dvb_frontend_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dev_dbg_ratelimited(fe->dvb->device, "%s:\n", __func__);

	poll_wait (file, &fepriv->events.wait_queue, wait);

	if (fepriv->events.eventw != fepriv->events.eventr)
		return (POLLIN | POLLRDNORM | POLLPRI);

	return 0;
}

static int dvb_frontend_open(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_adapter *adapter = fe->dvb;
	int ret;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);
	if (fe->exit == DVB_FE_DEVICE_REMOVED)
		return -ENODEV;

	if (adapter->mfe_shared) {
		mutex_lock (&adapter->mfe_lock);

		if (adapter->mfe_dvbdev == NULL)
			adapter->mfe_dvbdev = dvbdev;

		else if (adapter->mfe_dvbdev != dvbdev) {
			struct dvb_device
				*mfedev = adapter->mfe_dvbdev;
			struct dvb_frontend
				*mfe = mfedev->priv;
			struct dvb_frontend_private
				*mfepriv = mfe->frontend_priv;
			int mferetry = (dvb_mfe_wait_time << 1);

			mutex_unlock (&adapter->mfe_lock);
			while (mferetry-- && (mfedev->users != -1 ||
					mfepriv->thread != NULL)) {
				if(msleep_interruptible(500)) {
					if(signal_pending(current))
						return -EINTR;
				}
			}

			mutex_lock (&adapter->mfe_lock);
			if(adapter->mfe_dvbdev != dvbdev) {
				mfedev = adapter->mfe_dvbdev;
				mfe = mfedev->priv;
				mfepriv = mfe->frontend_priv;
				if (mfedev->users != -1 ||
						mfepriv->thread != NULL) {
					mutex_unlock (&adapter->mfe_lock);
					return -EBUSY;
				}
				adapter->mfe_dvbdev = dvbdev;
			}
		}
	}

	if (dvbdev->users == -1 && fe->ops.ts_bus_ctrl) {
		if ((ret = fe->ops.ts_bus_ctrl(fe, 1)) < 0)
			goto err0;

		/* If we took control of the bus, we need to force
		   reinitialization.  This is because many ts_bus_ctrl()
		   functions strobe the RESET pin on the demod, and if the
		   frontend thread already exists then the dvb_init() routine
		   won't get called (which is what usually does initial
		   register configuration). */
		fepriv->reinitialise = 1;
	}

	if ((ret = dvb_generic_open (inode, file)) < 0)
		goto err1;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		/* normal tune mode when opened R/W */
		fepriv->tune_mode_flags &= ~FE_TUNE_MODE_ONESHOT;
		fepriv->tone = -1;
		fepriv->voltage = -1;

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		if (fe->dvb->mdev) {
			mutex_lock(&fe->dvb->mdev->graph_mutex);
			if (fe->dvb->mdev->enable_source)
				ret = fe->dvb->mdev->enable_source(
							   dvbdev->entity,
							   &fepriv->pipe);
			mutex_unlock(&fe->dvb->mdev->graph_mutex);
			if (ret) {
				dev_err(fe->dvb->device,
					"Tuner is busy. Error %d\n", ret);
				goto err2;
			}
		}
#endif
		ret = dvb_frontend_start (fe);
		if (ret)
			goto err3;

		/*  empty event queue */
		fepriv->events.eventr = fepriv->events.eventw = 0;
	}

	dvb_frontend_get(fe);

	if (adapter->mfe_shared)
		mutex_unlock (&adapter->mfe_lock);
	return ret;

err3:
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	if (fe->dvb->mdev) {
		mutex_lock(&fe->dvb->mdev->graph_mutex);
		if (fe->dvb->mdev->disable_source)
			fe->dvb->mdev->disable_source(dvbdev->entity);
		mutex_unlock(&fe->dvb->mdev->graph_mutex);
	}
err2:
#endif
	dvb_generic_release(inode, file);
err1:
	if (dvbdev->users == -1 && fe->ops.ts_bus_ctrl)
		fe->ops.ts_bus_ctrl(fe, 0);
err0:
	if (adapter->mfe_shared)
		mutex_unlock (&adapter->mfe_lock);
	return ret;
}

static int dvb_frontend_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	int ret;

	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		fepriv->release_jiffies = jiffies;
		mb();
	}

	ret = dvb_generic_release (inode, file);

	if (dvbdev->users == -1) {
		wake_up(&fepriv->wait_queue);
#ifdef CONFIG_MEDIA_CONTROLLER_DVB
		if (fe->dvb->mdev) {
			mutex_lock(&fe->dvb->mdev->graph_mutex);
			if (fe->dvb->mdev->disable_source)
				fe->dvb->mdev->disable_source(dvbdev->entity);
			mutex_unlock(&fe->dvb->mdev->graph_mutex);
		}
#endif
		if (fe->exit != DVB_FE_NO_EXIT)
			wake_up(&dvbdev->wait_queue);
		if (fe->ops.ts_bus_ctrl)
			fe->ops.ts_bus_ctrl(fe, 0);
	}

	dvb_frontend_put(fe);

	return ret;
}

static const struct file_operations dvb_frontend_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= dvb_generic_ioctl,
	.poll		= dvb_frontend_poll,
	.open		= dvb_frontend_open,
	.release	= dvb_frontend_release,
	.llseek		= noop_llseek,
};

int dvb_frontend_suspend(struct dvb_frontend *fe)
{
	int ret = 0;

	dev_dbg(fe->dvb->device, "%s: adap=%d fe=%d\n", __func__, fe->dvb->num,
			fe->id);

	if (fe->ops.tuner_ops.suspend)
		ret = fe->ops.tuner_ops.suspend(fe);
	else if (fe->ops.tuner_ops.sleep)
		ret = fe->ops.tuner_ops.sleep(fe);

	if (fe->ops.sleep)
		ret = fe->ops.sleep(fe);

	return ret;
}
EXPORT_SYMBOL(dvb_frontend_suspend);

int dvb_frontend_resume(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	int ret = 0;

	dev_dbg(fe->dvb->device, "%s: adap=%d fe=%d\n", __func__, fe->dvb->num,
			fe->id);

	fe->exit = DVB_FE_DEVICE_RESUME;
	if (fe->ops.init)
		ret = fe->ops.init(fe);

	if (fe->ops.tuner_ops.resume)
		ret = fe->ops.tuner_ops.resume(fe);
	else if (fe->ops.tuner_ops.init)
		ret = fe->ops.tuner_ops.init(fe);

	if (fe->ops.set_tone && fepriv->tone != -1)
		fe->ops.set_tone(fe, fepriv->tone);
	if (fe->ops.set_voltage && fepriv->voltage != -1)
		fe->ops.set_voltage(fe, fepriv->voltage);

	fe->exit = DVB_FE_NO_EXIT;
	fepriv->state = FESTATE_RETUNE;
	dvb_frontend_wakeup(fe);

	return ret;
}
EXPORT_SYMBOL(dvb_frontend_resume);

int dvb_register_frontend(struct dvb_adapter* dvb,
			  struct dvb_frontend* fe)
{
	struct dvb_frontend_private *fepriv;
	const struct dvb_device dvbdev_template = {
		.users = ~0,
		.writers = 1,
		.readers = (~0)-1,
		.fops = &dvb_frontend_fops,
#if defined(CONFIG_MEDIA_CONTROLLER_DVB)
		.name = fe->ops.info.name,
#endif
		.kernel_ioctl = dvb_frontend_ioctl
	};

	dev_dbg(dvb->device, "%s:\n", __func__);

	if (mutex_lock_interruptible(&frontend_mutex))
		return -ERESTARTSYS;

	fe->frontend_priv = kzalloc(sizeof(struct dvb_frontend_private), GFP_KERNEL);
	if (fe->frontend_priv == NULL) {
		mutex_unlock(&frontend_mutex);
		return -ENOMEM;
	}
	fepriv = fe->frontend_priv;

	kref_init(&fe->refcount);

	/*
	 * After initialization, there need to be two references: one
	 * for dvb_unregister_frontend(), and another one for
	 * dvb_frontend_detach().
	 */
	dvb_frontend_get(fe);

	sema_init(&fepriv->sem, 1);
	init_waitqueue_head (&fepriv->wait_queue);
	init_waitqueue_head (&fepriv->events.wait_queue);
	mutex_init(&fepriv->events.mtx);
	fe->dvb = dvb;
	fepriv->inversion = INVERSION_OFF;

	dev_info(fe->dvb->device,
			"DVB: registering adapter %i frontend %i (%s)...\n",
			fe->dvb->num, fe->id, fe->ops.info.name);

	dvb_register_device (fe->dvb, &fepriv->dvbdev, &dvbdev_template,
			     fe, DVB_DEVICE_FRONTEND, 0);

	/*
	 * Initialize the cache to the proper values according with the
	 * first supported delivery system (ops->delsys[0])
	 */

	fe->dtv_property_cache.delivery_system = fe->ops.delsys[0];
	dvb_frontend_clear_cache(fe);

	mutex_unlock(&frontend_mutex);
	return 0;
}
EXPORT_SYMBOL(dvb_register_frontend);

int dvb_unregister_frontend(struct dvb_frontend* fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	dev_dbg(fe->dvb->device, "%s:\n", __func__);

	mutex_lock(&frontend_mutex);
	dvb_frontend_stop(fe);
	dvb_remove_device(fepriv->dvbdev);

	/* fe is invalid now */
	mutex_unlock(&frontend_mutex);
	dvb_frontend_put(fe);
	return 0;
}
EXPORT_SYMBOL(dvb_unregister_frontend);

static void dvb_frontend_invoke_release(struct dvb_frontend *fe,
					void (*release)(struct dvb_frontend *fe))
{
	if (release) {
		release(fe);
#ifdef CONFIG_MEDIA_ATTACH
		dvb_detach(release);
#endif
	}
}

void dvb_frontend_detach(struct dvb_frontend* fe)
{
	dvb_frontend_invoke_release(fe, fe->ops.release_sec);
	dvb_frontend_invoke_release(fe, fe->ops.tuner_ops.release);
	dvb_frontend_invoke_release(fe, fe->ops.analog_ops.release);
	dvb_frontend_invoke_release(fe, fe->ops.detach);
	dvb_frontend_put(fe);
}
EXPORT_SYMBOL(dvb_frontend_detach);
