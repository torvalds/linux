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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/suspend.h>
#include <linux/jiffies.h>
#include <asm/processor.h>
#include <asm/semaphore.h>

#include "dvb_frontend.h"
#include "dvbdev.h"

// #define DEBUG_LOCKLOSS 1

static int dvb_frontend_debug;
static int dvb_shutdown_timeout = 5;
static int dvb_force_auto_inversion;
static int dvb_override_tune_delay;
static int dvb_powerdown_on_sleep = 1;

module_param_named(frontend_debug, dvb_frontend_debug, int, 0644);
MODULE_PARM_DESC(frontend_debug, "Turn on/off frontend core debugging (default:off).");
module_param(dvb_shutdown_timeout, int, 0444);
MODULE_PARM_DESC(dvb_shutdown_timeout, "wait <shutdown_timeout> seconds after close() before suspending hardware");
module_param(dvb_force_auto_inversion, int, 0444);
MODULE_PARM_DESC(dvb_force_auto_inversion, "0: normal (default), 1: INVERSION_AUTO forced always");
module_param(dvb_override_tune_delay, int, 0444);
MODULE_PARM_DESC(dvb_override_tune_delay, "0: normal (default), >0 => delay in milliseconds to wait for lock after a tune attempt");
module_param(dvb_powerdown_on_sleep, int, 0444);
MODULE_PARM_DESC(dvb_powerdown_on_sleep, "0: do not power down, 1: turn LNB volatage off on sleep (default)");

#define dprintk if (dvb_frontend_debug) printk

#define FESTATE_IDLE 1
#define FESTATE_RETUNE 2
#define FESTATE_TUNING_FAST 4
#define FESTATE_TUNING_SLOW 8
#define FESTATE_TUNED 16
#define FESTATE_ZIGZAG_FAST 32
#define FESTATE_ZIGZAG_SLOW 64
#define FESTATE_DISEQC 128
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

static DECLARE_MUTEX(frontend_mutex);

struct dvb_frontend_private {

	struct dvb_device *dvbdev;
	struct dvb_frontend_parameters parameters;
	struct dvb_fe_events events;
	struct semaphore sem;
	struct list_head list_head;
	wait_queue_head_t wait_queue;
	pid_t thread_pid;
	unsigned long release_jiffies;
	int state;
	int bending;
	int lnb_drift;
	int inversion;
	int auto_step;
	int auto_sub_step;
	int started_auto_step;
	int min_delay;
	int max_drift;
	int step_size;
	int exit;
	int wakeup;
	fe_status_t status;
	fe_sec_tone_mode_t tone;
};


static void dvb_frontend_add_event(struct dvb_frontend *fe, fe_status_t status)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_fe_events *events = &fepriv->events;
	struct dvb_frontend_event *e;
	int wp;

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&events->sem))
		return;

	wp = (events->eventw + 1) % MAX_EVENT;

	if (wp == events->eventr) {
		events->overflow = 1;
		events->eventr = (events->eventr + 1) % MAX_EVENT;
	}

	e = &events->events[events->eventw];

	memcpy (&e->parameters, &fepriv->parameters,
		sizeof (struct dvb_frontend_parameters));

	if (status & FE_HAS_LOCK)
		if (fe->ops->get_frontend)
			fe->ops->get_frontend(fe, &e->parameters);

	events->eventw = wp;

	up (&events->sem);

	e->status = status;

	wake_up_interruptible (&events->wait_queue);
}

static int dvb_frontend_get_event(struct dvb_frontend *fe,
			    struct dvb_frontend_event *event, int flags)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	struct dvb_fe_events *events = &fepriv->events;

	dprintk ("%s\n", __FUNCTION__);

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

	if (down_interruptible (&events->sem))
		return -ERESTARTSYS;

	memcpy (event, &events->events[events->eventr],
		sizeof(struct dvb_frontend_event));

	events->eventr = (events->eventr + 1) % MAX_EVENT;

	up (&events->sem);

	return 0;
}

static void dvb_frontend_init(struct dvb_frontend *fe)
{
	dprintk ("DVB: initialising frontend %i (%s)...\n",
		 fe->dvb->num,
		 fe->ops->info.name);

	if (fe->ops->init)
		fe->ops->init(fe);
}

static void update_delay(int *quality, int *delay, int min_delay, int locked)
{
	    int q2;

	    dprintk ("%s\n", __FUNCTION__);

	    if (locked)
		      (*quality) = (*quality * 220 + 36*256) / 256;
	    else
		      (*quality) = (*quality * 220 + 0) / 256;

	    q2 = *quality - 128;
	    q2 *= q2;

	    *delay = min_delay + q2 * HZ / (128*128);
}

/**
 * Performs automatic twiddling of frontend parameters.
 *
 * @param fe The frontend concerned.
 * @param check_wrapped Checks if an iteration has completed. DO NOT SET ON THE FIRST ATTEMPT
 * @returns Number of complete iterations that have been performed.
 */
static int dvb_frontend_autotune(struct dvb_frontend *fe, int check_wrapped)
{
	int autoinversion;
	int ready = 0;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	int original_inversion = fepriv->parameters.inversion;
	u32 original_frequency = fepriv->parameters.frequency;

	/* are we using autoinversion? */
	autoinversion = ((!(fe->ops->info.caps & FE_CAN_INVERSION_AUTO)) &&
			 (fepriv->parameters.inversion == INVERSION_AUTO));

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

	dprintk("%s: drift:%i inversion:%i auto_step:%i "
		"auto_sub_step:%i started_auto_step:%i\n",
		__FUNCTION__, fepriv->lnb_drift, fepriv->inversion,
		fepriv->auto_step, fepriv->auto_sub_step, fepriv->started_auto_step);

	/* set the frontend itself */
	fepriv->parameters.frequency += fepriv->lnb_drift;
	if (autoinversion)
		fepriv->parameters.inversion = fepriv->inversion;
	if (fe->ops->set_frontend)
		fe->ops->set_frontend(fe, &fepriv->parameters);

	fepriv->parameters.frequency = original_frequency;
	fepriv->parameters.inversion = original_inversion;

	fepriv->auto_sub_step++;
	return 0;
}

static int dvb_frontend_is_exiting(struct dvb_frontend *fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	if (fepriv->exit)
		return 1;

	if (fepriv->dvbdev->writers == 1)
		if (time_after(jiffies, fepriv->release_jiffies +
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

/*
 * FIXME: use linux/kthread.h
 */
static int dvb_frontend_thread(void *data)
{
	struct dvb_frontend *fe = data;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	unsigned long timeout;
	char name [15];
	int quality = 0, delay = 3*HZ;
	fe_status_t s;
	int check_wrapped = 0;

	dprintk("%s\n", __FUNCTION__);

	snprintf (name, sizeof(name), "kdvb-fe-%i", fe->dvb->num);

        lock_kernel();
        daemonize(name);
        sigfillset(&current->blocked);
        unlock_kernel();

	fepriv->status = 0;
	dvb_frontend_init(fe);
	fepriv->wakeup = 0;

	while (1) {
		up(&fepriv->sem);	    /* is locked when we enter the thread... */

		timeout = wait_event_interruptible_timeout(fepriv->wait_queue,
							   dvb_frontend_should_wakeup(fe),
							   delay);
		if (0 != dvb_frontend_is_exiting(fe)) {
			/* got signal or quitting */
			break;
		}

		try_to_freeze();

		if (down_interruptible(&fepriv->sem))
			break;

		/* if we've got no parameters, just keep idling */
		if (fepriv->state & FESTATE_IDLE) {
			delay = 3*HZ;
			quality = 0;
			continue;
		}

		/* get the frontend status */
		if (fepriv->state & FESTATE_RETUNE) {
			s = 0;
		} else {
			if (fe->ops->read_status)
				fe->ops->read_status(fe, &s);
			if (s != fepriv->status) {
				dvb_frontend_add_event(fe, s);
				fepriv->status = s;
			}
		}
		/* if we're not tuned, and we have a lock, move to the TUNED state */
		if ((fepriv->state & FESTATE_WAITFORLOCK) && (s & FE_HAS_LOCK)) {
			update_delay(&quality, &delay, fepriv->min_delay, s & FE_HAS_LOCK);
			fepriv->state = FESTATE_TUNED;

			/* if we're tuned, then we have determined the correct inversion */
			if ((!(fe->ops->info.caps & FE_CAN_INVERSION_AUTO)) &&
			    (fepriv->parameters.inversion == INVERSION_AUTO)) {
				fepriv->parameters.inversion = fepriv->inversion;
			}
			continue;
		}

		/* if we are tuned already, check we're still locked */
		if (fepriv->state & FESTATE_TUNED) {
			update_delay(&quality, &delay, fepriv->min_delay, s & FE_HAS_LOCK);

			/* we're tuned, and the lock is still good... */
			if (s & FE_HAS_LOCK)
				continue;
			else { /* if we _WERE_ tuned, but now don't have a lock */
#ifdef DEBUG_LOCKLOSS
				/* first of all try setting the tone again if it was on - this
				 * sometimes works around problems with noisy power supplies */
				if (fe->ops->set_tone && (fepriv->tone == SEC_TONE_ON)) {
					fe->ops->set_tone(fe, fepriv->tone);
					mdelay(100);
					s = 0;
					fe->ops->read_status(fe, &s);
					if (s & FE_HAS_LOCK) {
						printk("DVB%i: Lock was lost, but regained by setting "
						       "the tone. This may indicate your power supply "
						       "is noisy/slightly incompatable with this DVB-S "
						       "adapter\n", fe->dvb->num);
						fepriv->state = FESTATE_TUNED;
						continue;
					}
				}
#endif
				/* some other reason for losing the lock - start zigzagging */
				fepriv->state = FESTATE_ZIGZAG_FAST;
				fepriv->started_auto_step = fepriv->auto_step;
				check_wrapped = 0;
			}
		}

		/* don't actually do anything if we're in the LOSTLOCK state,
		 * the frontend is set to FE_CAN_RECOVER, and the max_drift is 0 */
		if ((fepriv->state & FESTATE_LOSTLOCK) &&
		    (fe->ops->info.caps & FE_CAN_RECOVER) && (fepriv->max_drift == 0)) {
			update_delay(&quality, &delay, fepriv->min_delay, s & FE_HAS_LOCK);
			continue;
		}

		/* don't do anything if we're in the DISEQC state, since this
		 * might be someone with a motorized dish controlled by DISEQC.
		 * If its actually a re-tune, there will be a SET_FRONTEND soon enough.	*/
		if (fepriv->state & FESTATE_DISEQC) {
			update_delay(&quality, &delay, fepriv->min_delay, s & FE_HAS_LOCK);
			continue;
		}

		/* if we're in the RETUNE state, set everything up for a brand
		 * new scan, keeping the current inversion setting, as the next
		 * tune is _very_ likely to require the same */
		if (fepriv->state & FESTATE_RETUNE) {
			fepriv->lnb_drift = 0;
			fepriv->auto_step = 0;
			fepriv->auto_sub_step = 0;
			fepriv->started_auto_step = 0;
			check_wrapped = 0;
		}

		/* fast zigzag. */
		if ((fepriv->state & FESTATE_SEARCHING_FAST) || (fepriv->state & FESTATE_RETUNE)) {
			delay = fepriv->min_delay;

			/* peform a tune */
			if (dvb_frontend_autotune(fe, check_wrapped)) {
				/* OK, if we've run out of trials at the fast speed.
				 * Drop back to slow for the _next_ attempt */
				fepriv->state = FESTATE_SEARCHING_SLOW;
				fepriv->started_auto_step = fepriv->auto_step;
				continue;
			}
			check_wrapped = 1;

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
			update_delay(&quality, &delay, fepriv->min_delay, s & FE_HAS_LOCK);

			/* Note: don't bother checking for wrapping; we stay in this
			 * state until we get a lock */
			dvb_frontend_autotune(fe, 0);
		}
	}

	if (dvb_shutdown_timeout) {
		if (dvb_powerdown_on_sleep)
			if (fe->ops->set_voltage)
				fe->ops->set_voltage(fe, SEC_VOLTAGE_OFF);
		if (fe->ops->sleep)
			fe->ops->sleep(fe);
	}

	fepriv->thread_pid = 0;
	mb();

	dvb_frontend_wakeup(fe);
	return 0;
}

static void dvb_frontend_stop(struct dvb_frontend *fe)
{
	unsigned long ret;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dprintk ("%s\n", __FUNCTION__);

	fepriv->exit = 1;
	mb();

	if (!fepriv->thread_pid)
		return;

	/* check if the thread is really alive */
	if (kill_proc(fepriv->thread_pid, 0, 1) == -ESRCH) {
		printk("dvb_frontend_stop: thread PID %d already died\n",
				fepriv->thread_pid);
		/* make sure the mutex was not held by the thread */
		init_MUTEX (&fepriv->sem);
		return;
	}

	/* wake up the frontend thread, so it notices that fe->exit == 1 */
	dvb_frontend_wakeup(fe);

	/* wait until the frontend thread has exited */
	ret = wait_event_interruptible(fepriv->wait_queue,0 == fepriv->thread_pid);
	if (-ERESTARTSYS != ret) {
		fepriv->state = FESTATE_IDLE;
		return;
	}
	fepriv->state = FESTATE_IDLE;

	/* paranoia check in case a signal arrived */
	if (fepriv->thread_pid)
		printk("dvb_frontend_stop: warning: thread PID %d won't exit\n",
				fepriv->thread_pid);
}

static int dvb_frontend_start(struct dvb_frontend *fe)
{
	int ret;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dprintk ("%s\n", __FUNCTION__);

	if (fepriv->thread_pid) {
		if (!fepriv->exit)
			return 0;
		else
			dvb_frontend_stop (fe);
	}

	if (signal_pending(current))
		return -EINTR;
	if (down_interruptible (&fepriv->sem))
		return -EINTR;

	fepriv->state = FESTATE_IDLE;
	fepriv->exit = 0;
	fepriv->thread_pid = 0;
	mb();

	ret = kernel_thread (dvb_frontend_thread, fe, 0);

	if (ret < 0) {
		printk("dvb_frontend_start: failed to start kernel_thread (%d)\n", ret);
		up(&fepriv->sem);
		return ret;
	}
	fepriv->thread_pid = ret;

	return 0;
}

static int dvb_frontend_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, void *parg)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	int err = -EOPNOTSUPP;

	dprintk ("%s\n", __FUNCTION__);

	if (!fe || fepriv->exit)
		return -ENODEV;

	if ((file->f_flags & O_ACCMODE) == O_RDONLY &&
	    (_IOC_DIR(cmd) != _IOC_READ || cmd == FE_GET_EVENT ||
	     cmd == FE_DISEQC_RECV_SLAVE_REPLY))
		return -EPERM;

	if (down_interruptible (&fepriv->sem))
		return -ERESTARTSYS;

	switch (cmd) {
	case FE_GET_INFO: {
		struct dvb_frontend_info* info = parg;
		memcpy(info, &fe->ops->info, sizeof(struct dvb_frontend_info));

		/* Force the CAN_INVERSION_AUTO bit on. If the frontend doesn't
		 * do it, it is done for it. */
		info->caps |= FE_CAN_INVERSION_AUTO;
		err = 0;
		break;
	}

	case FE_READ_STATUS: {
		fe_status_t* status = parg;

		/* if retune was requested but hasn't occured yet, prevent
		 * that user get signal state from previous tuning */
		if(fepriv->state == FESTATE_RETUNE) {
			err=0;
			*status = 0;
			break;
		}

		if (fe->ops->read_status)
			err = fe->ops->read_status(fe, status);
		break;
	}
	case FE_READ_BER:
		if (fe->ops->read_ber)
			err = fe->ops->read_ber(fe, (__u32*) parg);
		break;

	case FE_READ_SIGNAL_STRENGTH:
		if (fe->ops->read_signal_strength)
			err = fe->ops->read_signal_strength(fe, (__u16*) parg);
		break;

	case FE_READ_SNR:
		if (fe->ops->read_snr)
			err = fe->ops->read_snr(fe, (__u16*) parg);
		break;

	case FE_READ_UNCORRECTED_BLOCKS:
		if (fe->ops->read_ucblocks)
			err = fe->ops->read_ucblocks(fe, (__u32*) parg);
		break;


	case FE_DISEQC_RESET_OVERLOAD:
		if (fe->ops->diseqc_reset_overload) {
			err = fe->ops->diseqc_reset_overload(fe);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_MASTER_CMD:
		if (fe->ops->diseqc_send_master_cmd) {
			err = fe->ops->diseqc_send_master_cmd(fe, (struct dvb_diseqc_master_cmd*) parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_SEND_BURST:
		if (fe->ops->diseqc_send_burst) {
			err = fe->ops->diseqc_send_burst(fe, (fe_sec_mini_cmd_t) parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_SET_TONE:
		if (fe->ops->set_tone) {
			err = fe->ops->set_tone(fe, (fe_sec_tone_mode_t) parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
			fepriv->tone = (fe_sec_tone_mode_t) parg;
		}
		break;

	case FE_SET_VOLTAGE:
		if (fe->ops->set_voltage) {
			err = fe->ops->set_voltage(fe, (fe_sec_voltage_t) parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISHNETWORK_SEND_LEGACY_CMD:
		if (fe->ops->dishnetwork_send_legacy_command) {
			err = fe->ops->dishnetwork_send_legacy_command(fe, (unsigned int) parg);
			fepriv->state = FESTATE_DISEQC;
			fepriv->status = 0;
		}
		break;

	case FE_DISEQC_RECV_SLAVE_REPLY:
		if (fe->ops->diseqc_recv_slave_reply)
			err = fe->ops->diseqc_recv_slave_reply(fe, (struct dvb_diseqc_slave_reply*) parg);
		break;

	case FE_ENABLE_HIGH_LNB_VOLTAGE:
		if (fe->ops->enable_high_lnb_voltage)
			err = fe->ops->enable_high_lnb_voltage(fe, (int) parg);
		break;

	case FE_SET_FRONTEND: {
		struct dvb_frontend_tune_settings fetunesettings;

		memcpy (&fepriv->parameters, parg,
			sizeof (struct dvb_frontend_parameters));

		memset(&fetunesettings, 0, sizeof(struct dvb_frontend_tune_settings));
		memcpy(&fetunesettings.parameters, parg,
		       sizeof (struct dvb_frontend_parameters));

		/* force auto frequency inversion if requested */
		if (dvb_force_auto_inversion) {
			fepriv->parameters.inversion = INVERSION_AUTO;
			fetunesettings.parameters.inversion = INVERSION_AUTO;
		}
		if (fe->ops->info.type == FE_OFDM) {
			/* without hierachical coding code_rate_LP is irrelevant,
			 * so we tolerate the otherwise invalid FEC_NONE setting */
			if (fepriv->parameters.u.ofdm.hierarchy_information == HIERARCHY_NONE &&
			    fepriv->parameters.u.ofdm.code_rate_LP == FEC_NONE)
				fepriv->parameters.u.ofdm.code_rate_LP = FEC_AUTO;
		}

		/* get frontend-specific tuning settings */
		if (fe->ops->get_tune_settings && (fe->ops->get_tune_settings(fe, &fetunesettings) == 0)) {
			fepriv->min_delay = (fetunesettings.min_delay_ms * HZ) / 1000;
			fepriv->max_drift = fetunesettings.max_drift;
			fepriv->step_size = fetunesettings.step_size;
		} else {
			/* default values */
			switch(fe->ops->info.type) {
			case FE_QPSK:
				fepriv->min_delay = HZ/20;
				fepriv->step_size = fepriv->parameters.u.qpsk.symbol_rate / 16000;
				fepriv->max_drift = fepriv->parameters.u.qpsk.symbol_rate / 2000;
				break;

			case FE_QAM:
				fepriv->min_delay = HZ/20;
				fepriv->step_size = 0; /* no zigzag */
				fepriv->max_drift = 0;
				break;

			case FE_OFDM:
				fepriv->min_delay = HZ/20;
				fepriv->step_size = fe->ops->info.frequency_stepsize * 2;
				fepriv->max_drift = (fe->ops->info.frequency_stepsize * 2) + 1;
				break;
			case FE_ATSC:
				printk("dvb-core: FE_ATSC not handled yet.\n");
				break;
			}
		}
		if (dvb_override_tune_delay > 0)
			fepriv->min_delay = (dvb_override_tune_delay * HZ) / 1000;

		fepriv->state = FESTATE_RETUNE;
		dvb_frontend_wakeup(fe);
		dvb_frontend_add_event(fe, 0);
		fepriv->status = 0;
		err = 0;
		break;
	}

	case FE_GET_EVENT:
		err = dvb_frontend_get_event (fe, parg, file->f_flags);
		break;

	case FE_GET_FRONTEND:
		if (fe->ops->get_frontend) {
			memcpy (parg, &fepriv->parameters, sizeof (struct dvb_frontend_parameters));
			err = fe->ops->get_frontend(fe, (struct dvb_frontend_parameters*) parg);
		}
		break;
	};

	up (&fepriv->sem);
	return err;
}

static unsigned int dvb_frontend_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dprintk ("%s\n", __FUNCTION__);

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
	int ret;

	dprintk ("%s\n", __FUNCTION__);

	if ((ret = dvb_generic_open (inode, file)) < 0)
		return ret;

	if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
		ret = dvb_frontend_start (fe);
		if (ret)
			dvb_generic_release (inode, file);

		/*  empty event queue */
		fepriv->events.eventr = fepriv->events.eventw = 0;
	}

	return ret;
}

static int dvb_frontend_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct dvb_frontend *fe = dvbdev->priv;
	struct dvb_frontend_private *fepriv = fe->frontend_priv;

	dprintk ("%s\n", __FUNCTION__);

	if ((file->f_flags & O_ACCMODE) != O_RDONLY)
		fepriv->release_jiffies = jiffies;

	return dvb_generic_release (inode, file);
}

static struct file_operations dvb_frontend_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= dvb_generic_ioctl,
	.poll		= dvb_frontend_poll,
	.open		= dvb_frontend_open,
	.release	= dvb_frontend_release
};

int dvb_register_frontend(struct dvb_adapter* dvb,
			  struct dvb_frontend* fe)
{
	struct dvb_frontend_private *fepriv;
	static const struct dvb_device dvbdev_template = {
		.users = ~0,
		.writers = 1,
		.readers = (~0)-1,
		.fops = &dvb_frontend_fops,
		.kernel_ioctl = dvb_frontend_ioctl
	};

	dprintk ("%s\n", __FUNCTION__);

	if (down_interruptible (&frontend_mutex))
		return -ERESTARTSYS;

	fe->frontend_priv = kmalloc(sizeof(struct dvb_frontend_private), GFP_KERNEL);
	if (fe->frontend_priv == NULL) {
		up(&frontend_mutex);
		return -ENOMEM;
	}
	fepriv = fe->frontend_priv;
	memset(fe->frontend_priv, 0, sizeof(struct dvb_frontend_private));

	init_MUTEX (&fepriv->sem);
	init_waitqueue_head (&fepriv->wait_queue);
	init_waitqueue_head (&fepriv->events.wait_queue);
	init_MUTEX (&fepriv->events.sem);
	fe->dvb = dvb;
	fepriv->inversion = INVERSION_OFF;
	fepriv->tone = SEC_TONE_OFF;

	printk ("DVB: registering frontend %i (%s)...\n",
		fe->dvb->num,
		fe->ops->info.name);

	dvb_register_device (fe->dvb, &fepriv->dvbdev, &dvbdev_template,
			     fe, DVB_DEVICE_FRONTEND);

	up (&frontend_mutex);
	return 0;
}
EXPORT_SYMBOL(dvb_register_frontend);

int dvb_unregister_frontend(struct dvb_frontend* fe)
{
	struct dvb_frontend_private *fepriv = fe->frontend_priv;
	dprintk ("%s\n", __FUNCTION__);

	down (&frontend_mutex);
	dvb_unregister_device (fepriv->dvbdev);
	dvb_frontend_stop (fe);
	if (fe->ops->release)
		fe->ops->release(fe);
	else
		printk("dvb_frontend: Demodulator (%s) does not have a release callback!\n", fe->ops->info.name);
	/* fe is invalid now */
	kfree(fepriv);
	up (&frontend_mutex);
	return 0;
}
EXPORT_SYMBOL(dvb_unregister_frontend);
