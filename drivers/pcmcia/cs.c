// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs.c -- Kernel Card Services - core services
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <asm/irq.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "cs_internal.h"


/* Module parameters */

MODULE_AUTHOR("David Hinds <dahinds@users.sourceforge.net>");
MODULE_DESCRIPTION("Linux Kernel Card Services");
MODULE_LICENSE("GPL");

#define INT_MODULE_PARM(n, v) static int n = v; module_param(n, int, 0444)

INT_MODULE_PARM(setup_delay,	10);		/* centiseconds */
INT_MODULE_PARM(resume_delay,	20);		/* centiseconds */
INT_MODULE_PARM(shutdown_delay,	3);		/* centiseconds */
INT_MODULE_PARM(vcc_settle,	40);		/* centiseconds */
INT_MODULE_PARM(reset_time,	10);		/* usecs */
INT_MODULE_PARM(unreset_delay,	10);		/* centiseconds */
INT_MODULE_PARM(unreset_check,	10);		/* centiseconds */
INT_MODULE_PARM(unreset_limit,	30);		/* unreset_check's */

/* Access speed for attribute memory windows */
INT_MODULE_PARM(cis_speed,	300);		/* ns */


socket_state_t dead_socket = {
	.csc_mask	= SS_DETECT,
};
EXPORT_SYMBOL(dead_socket);


/* List of all sockets, protected by a rwsem */
LIST_HEAD(pcmcia_socket_list);
EXPORT_SYMBOL(pcmcia_socket_list);

DECLARE_RWSEM(pcmcia_socket_list_rwsem);
EXPORT_SYMBOL(pcmcia_socket_list_rwsem);


struct pcmcia_socket *pcmcia_get_socket(struct pcmcia_socket *skt)
{
	struct device *dev = get_device(&skt->dev);
	if (!dev)
		return NULL;
	return dev_get_drvdata(dev);
}
EXPORT_SYMBOL(pcmcia_get_socket);


void pcmcia_put_socket(struct pcmcia_socket *skt)
{
	put_device(&skt->dev);
}
EXPORT_SYMBOL(pcmcia_put_socket);


static void pcmcia_release_socket(struct device *dev)
{
	struct pcmcia_socket *socket = dev_get_drvdata(dev);

	complete(&socket->socket_released);
}

static int pccardd(void *__skt);

/**
 * pcmcia_register_socket - add a new pcmcia socket device
 * @socket: the &socket to register
 */
int pcmcia_register_socket(struct pcmcia_socket *socket)
{
	struct task_struct *tsk;
	int ret;

	if (!socket || !socket->ops || !socket->dev.parent || !socket->resource_ops)
		return -EINVAL;

	dev_dbg(&socket->dev, "pcmcia_register_socket(0x%p)\n", socket->ops);

	/* try to obtain a socket number [yes, it gets ugly if we
	 * register more than 2^sizeof(unsigned int) pcmcia
	 * sockets... but the socket number is deprecated
	 * anyways, so I don't care] */
	down_write(&pcmcia_socket_list_rwsem);
	if (list_empty(&pcmcia_socket_list))
		socket->sock = 0;
	else {
		unsigned int found, i = 1;
		struct pcmcia_socket *tmp;
		do {
			found = 1;
			list_for_each_entry(tmp, &pcmcia_socket_list, socket_list) {
				if (tmp->sock == i)
					found = 0;
			}
			i++;
		} while (!found);
		socket->sock = i - 1;
	}
	list_add_tail(&socket->socket_list, &pcmcia_socket_list);
	up_write(&pcmcia_socket_list_rwsem);

#ifndef CONFIG_CARDBUS
	/*
	 * If we do not support Cardbus, ensure that
	 * the Cardbus socket capability is disabled.
	 */
	socket->features &= ~SS_CAP_CARDBUS;
#endif

	/* set proper values in socket->dev */
	dev_set_drvdata(&socket->dev, socket);
	socket->dev.class = &pcmcia_socket_class;
	dev_set_name(&socket->dev, "pcmcia_socket%u", socket->sock);

	/* base address = 0, map = 0 */
	socket->cis_mem.flags = 0;
	socket->cis_mem.speed = cis_speed;

	INIT_LIST_HEAD(&socket->cis_cache);

	init_completion(&socket->socket_released);
	init_completion(&socket->thread_done);
	mutex_init(&socket->skt_mutex);
	mutex_init(&socket->ops_mutex);
	spin_lock_init(&socket->thread_lock);

	if (socket->resource_ops->init) {
		mutex_lock(&socket->ops_mutex);
		ret = socket->resource_ops->init(socket);
		mutex_unlock(&socket->ops_mutex);
		if (ret)
			goto err;
	}

	tsk = kthread_run(pccardd, socket, "pccardd");
	if (IS_ERR(tsk)) {
		ret = PTR_ERR(tsk);
		goto err;
	}

	wait_for_completion(&socket->thread_done);
	if (!socket->thread) {
		dev_warn(&socket->dev,
			 "PCMCIA: warning: socket thread did not start\n");
		return -EIO;
	}

	pcmcia_parse_events(socket, SS_DETECT);

	/*
	 * Let's try to get the PCMCIA module for 16-bit PCMCIA support.
	 * If it fails, it doesn't matter -- we still have 32-bit CardBus
	 * support to offer, so this is not a failure mode.
	 */
	request_module_nowait("pcmcia");

	return 0;

 err:
	down_write(&pcmcia_socket_list_rwsem);
	list_del(&socket->socket_list);
	up_write(&pcmcia_socket_list_rwsem);
	return ret;
} /* pcmcia_register_socket */
EXPORT_SYMBOL(pcmcia_register_socket);


/**
 * pcmcia_unregister_socket - remove a pcmcia socket device
 * @socket: the &socket to unregister
 */
void pcmcia_unregister_socket(struct pcmcia_socket *socket)
{
	if (!socket)
		return;

	dev_dbg(&socket->dev, "pcmcia_unregister_socket(0x%p)\n", socket->ops);

	if (socket->thread)
		kthread_stop(socket->thread);

	/* remove from our own list */
	down_write(&pcmcia_socket_list_rwsem);
	list_del(&socket->socket_list);
	up_write(&pcmcia_socket_list_rwsem);

	/* wait for sysfs to drop all references */
	if (socket->resource_ops->exit) {
		mutex_lock(&socket->ops_mutex);
		socket->resource_ops->exit(socket);
		mutex_unlock(&socket->ops_mutex);
	}
	wait_for_completion(&socket->socket_released);
} /* pcmcia_unregister_socket */
EXPORT_SYMBOL(pcmcia_unregister_socket);


static int socket_reset(struct pcmcia_socket *skt)
{
	int status, i;

	dev_dbg(&skt->dev, "reset\n");

	skt->socket.flags |= SS_OUTPUT_ENA | SS_RESET;
	skt->ops->set_socket(skt, &skt->socket);
	udelay((long)reset_time);

	skt->socket.flags &= ~SS_RESET;
	skt->ops->set_socket(skt, &skt->socket);

	msleep(unreset_delay * 10);
	for (i = 0; i < unreset_limit; i++) {
		skt->ops->get_status(skt, &status);

		if (!(status & SS_DETECT))
			return -ENODEV;

		if (status & SS_READY)
			return 0;

		msleep(unreset_check * 10);
	}

	dev_err(&skt->dev, "time out after reset\n");
	return -ETIMEDOUT;
}

/*
 * socket_setup() and socket_shutdown() are called by the main event handler
 * when card insertion and removal events are received.
 * socket_setup() turns on socket power and resets the socket, in two stages.
 * socket_shutdown() unconfigures a socket and turns off socket power.
 */
static void socket_shutdown(struct pcmcia_socket *s)
{
	int status;

	dev_dbg(&s->dev, "shutdown\n");

	if (s->callback)
		s->callback->remove(s);

	mutex_lock(&s->ops_mutex);
	s->state &= SOCKET_INUSE | SOCKET_PRESENT;
	msleep(shutdown_delay * 10);
	s->state &= SOCKET_INUSE;

	/* Blank out the socket state */
	s->socket = dead_socket;
	s->ops->init(s);
	s->ops->set_socket(s, &s->socket);
	s->lock_count = 0;
	kfree(s->fake_cis);
	s->fake_cis = NULL;
	s->functions = 0;

	/* From here on we can be sure that only we (that is, the
	 * pccardd thread) accesses this socket, and all (16-bit)
	 * PCMCIA interactions are gone. Therefore, release
	 * ops_mutex so that we don't get a sysfs-related lockdep
	 * warning.
	 */
	mutex_unlock(&s->ops_mutex);

#ifdef CONFIG_CARDBUS
	cb_free(s);
#endif

	/* give socket some time to power down */
	msleep(100);

	s->ops->get_status(s, &status);
	if (status & SS_POWERON) {
		dev_err(&s->dev,
			"*** DANGER *** unable to remove socket power\n");
	}

	s->state &= ~SOCKET_INUSE;
}

static int socket_setup(struct pcmcia_socket *skt, int initial_delay)
{
	int status, i;

	dev_dbg(&skt->dev, "setup\n");

	skt->ops->get_status(skt, &status);
	if (!(status & SS_DETECT))
		return -ENODEV;

	msleep(initial_delay * 10);

	for (i = 0; i < 100; i++) {
		skt->ops->get_status(skt, &status);
		if (!(status & SS_DETECT))
			return -ENODEV;

		if (!(status & SS_PENDING))
			break;

		msleep(100);
	}

	if (status & SS_PENDING) {
		dev_err(&skt->dev, "voltage interrogation timed out\n");
		return -ETIMEDOUT;
	}

	if (status & SS_CARDBUS) {
		if (!(skt->features & SS_CAP_CARDBUS)) {
			dev_err(&skt->dev, "cardbus cards are not supported\n");
			return -EINVAL;
		}
		skt->state |= SOCKET_CARDBUS;
	} else
		skt->state &= ~SOCKET_CARDBUS;

	/*
	 * Decode the card voltage requirements, and apply power to the card.
	 */
	if (status & SS_3VCARD)
		skt->socket.Vcc = skt->socket.Vpp = 33;
	else if (!(status & SS_XVCARD))
		skt->socket.Vcc = skt->socket.Vpp = 50;
	else {
		dev_err(&skt->dev, "unsupported voltage key\n");
		return -EIO;
	}

	if (skt->power_hook)
		skt->power_hook(skt, HOOK_POWER_PRE);

	skt->socket.flags = 0;
	skt->ops->set_socket(skt, &skt->socket);

	/*
	 * Wait "vcc_settle" for the supply to stabilise.
	 */
	msleep(vcc_settle * 10);

	skt->ops->get_status(skt, &status);
	if (!(status & SS_POWERON)) {
		dev_err(&skt->dev, "unable to apply power\n");
		return -EIO;
	}

	status = socket_reset(skt);

	if (skt->power_hook)
		skt->power_hook(skt, HOOK_POWER_POST);

	return status;
}

/*
 * Handle card insertion.  Setup the socket, reset the card,
 * and then tell the rest of PCMCIA that a card is present.
 */
static int socket_insert(struct pcmcia_socket *skt)
{
	int ret;

	dev_dbg(&skt->dev, "insert\n");

	mutex_lock(&skt->ops_mutex);
	if (skt->state & SOCKET_INUSE) {
		mutex_unlock(&skt->ops_mutex);
		return -EINVAL;
	}
	skt->state |= SOCKET_INUSE;

	ret = socket_setup(skt, setup_delay);
	if (ret == 0) {
		skt->state |= SOCKET_PRESENT;

		dev_notice(&skt->dev, "pccard: %s card inserted into slot %d\n",
			   (skt->state & SOCKET_CARDBUS) ? "CardBus" : "PCMCIA",
			   skt->sock);

#ifdef CONFIG_CARDBUS
		if (skt->state & SOCKET_CARDBUS) {
			cb_alloc(skt);
			skt->state |= SOCKET_CARDBUS_CONFIG;
		}
#endif
		dev_dbg(&skt->dev, "insert done\n");
		mutex_unlock(&skt->ops_mutex);

		if (!(skt->state & SOCKET_CARDBUS) && (skt->callback))
			skt->callback->add(skt);
	} else {
		mutex_unlock(&skt->ops_mutex);
		socket_shutdown(skt);
	}

	return ret;
}

static int socket_suspend(struct pcmcia_socket *skt)
{
	if ((skt->state & SOCKET_SUSPEND) && !(skt->state & SOCKET_IN_RESUME))
		return -EBUSY;

	mutex_lock(&skt->ops_mutex);
	/* store state on first suspend, but not after spurious wakeups */
	if (!(skt->state & SOCKET_IN_RESUME))
		skt->suspended_state = skt->state;

	skt->socket = dead_socket;
	skt->ops->set_socket(skt, &skt->socket);
	if (skt->ops->suspend)
		skt->ops->suspend(skt);
	skt->state |= SOCKET_SUSPEND;
	skt->state &= ~SOCKET_IN_RESUME;
	mutex_unlock(&skt->ops_mutex);
	return 0;
}

static int socket_early_resume(struct pcmcia_socket *skt)
{
	mutex_lock(&skt->ops_mutex);
	skt->socket = dead_socket;
	skt->ops->init(skt);
	skt->ops->set_socket(skt, &skt->socket);
	if (skt->state & SOCKET_PRESENT)
		skt->resume_status = socket_setup(skt, resume_delay);
	skt->state |= SOCKET_IN_RESUME;
	mutex_unlock(&skt->ops_mutex);
	return 0;
}

static int socket_late_resume(struct pcmcia_socket *skt)
{
	int ret = 0;

	mutex_lock(&skt->ops_mutex);
	skt->state &= ~(SOCKET_SUSPEND | SOCKET_IN_RESUME);
	mutex_unlock(&skt->ops_mutex);

	if (!(skt->state & SOCKET_PRESENT)) {
		ret = socket_insert(skt);
		if (ret == -ENODEV)
			ret = 0;
		return ret;
	}

	if (skt->resume_status) {
		socket_shutdown(skt);
		return 0;
	}

	if (skt->suspended_state != skt->state) {
		dev_dbg(&skt->dev,
			"suspend state 0x%x != resume state 0x%x\n",
			skt->suspended_state, skt->state);

		socket_shutdown(skt);
		return socket_insert(skt);
	}

	if (!(skt->state & SOCKET_CARDBUS) && (skt->callback))
		ret = skt->callback->early_resume(skt);
	return ret;
}

/*
 * Finalize the resume. In case of a cardbus socket, we have
 * to rebind the devices as we can't be certain that it has been
 * replaced, or not.
 */
static int socket_complete_resume(struct pcmcia_socket *skt)
{
	int ret = 0;
#ifdef CONFIG_CARDBUS
	if (skt->state & SOCKET_CARDBUS) {
		/* We can't be sure the CardBus card is the same
		 * as the one previously inserted. Therefore, remove
		 * and re-add... */
		cb_free(skt);
		ret = cb_alloc(skt);
		if (ret)
			cb_free(skt);
	}
#endif
	return ret;
}

/*
 * Resume a socket.  If a card is present, verify its CIS against
 * our cached copy.  If they are different, the card has been
 * replaced, and we need to tell the drivers.
 */
static int socket_resume(struct pcmcia_socket *skt)
{
	int err;
	if (!(skt->state & SOCKET_SUSPEND))
		return -EBUSY;

	socket_early_resume(skt);
	err = socket_late_resume(skt);
	if (!err)
		err = socket_complete_resume(skt);
	return err;
}

static void socket_remove(struct pcmcia_socket *skt)
{
	dev_notice(&skt->dev, "pccard: card ejected from slot %d\n", skt->sock);
	socket_shutdown(skt);
}

/*
 * Process a socket card detect status change.
 *
 * If we don't have a card already present, delay the detect event for
 * about 20ms (to be on the safe side) before reading the socket status.
 *
 * Some i82365-based systems send multiple SS_DETECT events during card
 * insertion, and the "card present" status bit seems to bounce.  This
 * will probably be true with GPIO-based card detection systems after
 * the product has aged.
 */
static void socket_detect_change(struct pcmcia_socket *skt)
{
	if (!(skt->state & SOCKET_SUSPEND)) {
		int status;

		if (!(skt->state & SOCKET_PRESENT))
			msleep(20);

		skt->ops->get_status(skt, &status);
		if ((skt->state & SOCKET_PRESENT) &&
		     !(status & SS_DETECT))
			socket_remove(skt);
		if (!(skt->state & SOCKET_PRESENT) &&
		    (status & SS_DETECT))
			socket_insert(skt);
	}
}

static int pccardd(void *__skt)
{
	struct pcmcia_socket *skt = __skt;
	int ret;

	skt->thread = current;
	skt->socket = dead_socket;
	skt->ops->init(skt);
	skt->ops->set_socket(skt, &skt->socket);

	/* register with the device core */
	ret = device_register(&skt->dev);
	if (ret) {
		dev_warn(&skt->dev, "PCMCIA: unable to register socket\n");
		skt->thread = NULL;
		complete(&skt->thread_done);
		put_device(&skt->dev);
		return 0;
	}
	ret = pccard_sysfs_add_socket(&skt->dev);
	if (ret)
		dev_warn(&skt->dev, "err %d adding socket attributes\n", ret);

	complete(&skt->thread_done);

	/* wait for userspace to catch up */
	msleep(250);

	set_freezable();
	for (;;) {
		unsigned long flags;
		unsigned int events;
		unsigned int sysfs_events;

		spin_lock_irqsave(&skt->thread_lock, flags);
		events = skt->thread_events;
		skt->thread_events = 0;
		sysfs_events = skt->sysfs_events;
		skt->sysfs_events = 0;
		spin_unlock_irqrestore(&skt->thread_lock, flags);

		mutex_lock(&skt->skt_mutex);
		if (events & SS_DETECT)
			socket_detect_change(skt);

		if (sysfs_events) {
			if (sysfs_events & PCMCIA_UEVENT_EJECT)
				socket_remove(skt);
			if (sysfs_events & PCMCIA_UEVENT_INSERT)
				socket_insert(skt);
			if ((sysfs_events & PCMCIA_UEVENT_SUSPEND) &&
				!(skt->state & SOCKET_CARDBUS)) {
				if (skt->callback)
					ret = skt->callback->suspend(skt);
				else
					ret = 0;
				if (!ret) {
					socket_suspend(skt);
					msleep(100);
				}
			}
			if ((sysfs_events & PCMCIA_UEVENT_RESUME) &&
				!(skt->state & SOCKET_CARDBUS)) {
				ret = socket_resume(skt);
				if (!ret && skt->callback)
					skt->callback->resume(skt);
			}
			if ((sysfs_events & PCMCIA_UEVENT_REQUERY) &&
				!(skt->state & SOCKET_CARDBUS)) {
				if (!ret && skt->callback)
					skt->callback->requery(skt);
			}
		}
		mutex_unlock(&skt->skt_mutex);

		if (events || sysfs_events)
			continue;

		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;

		schedule();

		try_to_freeze();
	}
	/* make sure we are running before we exit */
	__set_current_state(TASK_RUNNING);

	/* shut down socket, if a device is still present */
	if (skt->state & SOCKET_PRESENT) {
		mutex_lock(&skt->skt_mutex);
		socket_remove(skt);
		mutex_unlock(&skt->skt_mutex);
	}

	/* remove from the device core */
	pccard_sysfs_remove_socket(&skt->dev);
	device_unregister(&skt->dev);

	return 0;
}

/*
 * Yenta (at least) probes interrupts before registering the socket and
 * starting the handler thread.
 */
void pcmcia_parse_events(struct pcmcia_socket *s, u_int events)
{
	unsigned long flags;
	dev_dbg(&s->dev, "parse_events: events %08x\n", events);
	if (s->thread) {
		spin_lock_irqsave(&s->thread_lock, flags);
		s->thread_events |= events;
		spin_unlock_irqrestore(&s->thread_lock, flags);

		wake_up_process(s->thread);
	}
} /* pcmcia_parse_events */
EXPORT_SYMBOL(pcmcia_parse_events);

/**
 * pcmcia_parse_uevents() - tell pccardd to issue manual commands
 * @s:		the PCMCIA socket we wan't to command
 * @events:	events to pass to pccardd
 *
 * userspace-issued insert, eject, suspend and resume commands must be
 * handled by pccardd to avoid any sysfs-related deadlocks. Valid events
 * are PCMCIA_UEVENT_EJECT (for eject), PCMCIA_UEVENT__INSERT (for insert),
 * PCMCIA_UEVENT_RESUME (for resume), PCMCIA_UEVENT_SUSPEND (for suspend)
 * and PCMCIA_UEVENT_REQUERY (for re-querying the PCMCIA card).
 */
void pcmcia_parse_uevents(struct pcmcia_socket *s, u_int events)
{
	unsigned long flags;
	dev_dbg(&s->dev, "parse_uevents: events %08x\n", events);
	if (s->thread) {
		spin_lock_irqsave(&s->thread_lock, flags);
		s->sysfs_events |= events;
		spin_unlock_irqrestore(&s->thread_lock, flags);

		wake_up_process(s->thread);
	}
}
EXPORT_SYMBOL(pcmcia_parse_uevents);


/* register pcmcia_callback */
int pccard_register_pcmcia(struct pcmcia_socket *s, struct pcmcia_callback *c)
{
	int ret = 0;

	/* s->skt_mutex also protects s->callback */
	mutex_lock(&s->skt_mutex);

	if (c) {
		/* registration */
		if (s->callback) {
			ret = -EBUSY;
			goto err;
		}

		s->callback = c;

		if ((s->state & (SOCKET_PRESENT|SOCKET_CARDBUS)) == SOCKET_PRESENT)
			s->callback->add(s);
	} else
		s->callback = NULL;
 err:
	mutex_unlock(&s->skt_mutex);

	return ret;
}
EXPORT_SYMBOL(pccard_register_pcmcia);


/* I'm not sure which "reset" function this is supposed to use,
 * but for now, it uses the low-level interface's reset, not the
 * CIS register.
 */

int pcmcia_reset_card(struct pcmcia_socket *skt)
{
	int ret;

	dev_dbg(&skt->dev, "resetting socket\n");

	mutex_lock(&skt->skt_mutex);
	do {
		if (!(skt->state & SOCKET_PRESENT)) {
			dev_dbg(&skt->dev, "can't reset, not present\n");
			ret = -ENODEV;
			break;
		}
		if (skt->state & SOCKET_SUSPEND) {
			dev_dbg(&skt->dev, "can't reset, suspended\n");
			ret = -EBUSY;
			break;
		}
		if (skt->state & SOCKET_CARDBUS) {
			dev_dbg(&skt->dev, "can't reset, is cardbus\n");
			ret = -EPERM;
			break;
		}

		if (skt->callback)
			skt->callback->suspend(skt);
		mutex_lock(&skt->ops_mutex);
		ret = socket_reset(skt);
		mutex_unlock(&skt->ops_mutex);
		if ((ret == 0) && (skt->callback))
			skt->callback->resume(skt);

		ret = 0;
	} while (0);
	mutex_unlock(&skt->skt_mutex);

	return ret;
} /* reset_card */
EXPORT_SYMBOL(pcmcia_reset_card);


static int pcmcia_socket_uevent(const struct device *dev,
				struct kobj_uevent_env *env)
{
	const struct pcmcia_socket *s = container_of(dev, struct pcmcia_socket, dev);

	if (add_uevent_var(env, "SOCKET_NO=%u", s->sock))
		return -ENOMEM;

	return 0;
}


static struct completion pcmcia_unload;

static void pcmcia_release_socket_class(const struct class *data)
{
	complete(&pcmcia_unload);
}


#ifdef CONFIG_PM

static int __pcmcia_pm_op(struct device *dev,
			  int (*callback) (struct pcmcia_socket *skt))
{
	struct pcmcia_socket *s = container_of(dev, struct pcmcia_socket, dev);
	int ret;

	mutex_lock(&s->skt_mutex);
	ret = callback(s);
	mutex_unlock(&s->skt_mutex);

	return ret;
}

static int pcmcia_socket_dev_suspend_noirq(struct device *dev)
{
	return __pcmcia_pm_op(dev, socket_suspend);
}

static int pcmcia_socket_dev_resume_noirq(struct device *dev)
{
	return __pcmcia_pm_op(dev, socket_early_resume);
}

static int __used pcmcia_socket_dev_resume(struct device *dev)
{
	return __pcmcia_pm_op(dev, socket_late_resume);
}

static void __used pcmcia_socket_dev_complete(struct device *dev)
{
	WARN(__pcmcia_pm_op(dev, socket_complete_resume),
		"failed to complete resume");
}

static const struct dev_pm_ops pcmcia_socket_pm_ops = {
	/* dev_resume may be called with IRQs enabled */
	SET_SYSTEM_SLEEP_PM_OPS(NULL,
				pcmcia_socket_dev_resume)

	/* late suspend must be called with IRQs disabled */
	.suspend_noirq = pcmcia_socket_dev_suspend_noirq,
	.freeze_noirq = pcmcia_socket_dev_suspend_noirq,
	.poweroff_noirq = pcmcia_socket_dev_suspend_noirq,

	/* early resume must be called with IRQs disabled */
	.resume_noirq = pcmcia_socket_dev_resume_noirq,
	.thaw_noirq = pcmcia_socket_dev_resume_noirq,
	.restore_noirq = pcmcia_socket_dev_resume_noirq,
	.complete = pcmcia_socket_dev_complete,
};

#define PCMCIA_SOCKET_CLASS_PM_OPS (&pcmcia_socket_pm_ops)

#else /* CONFIG_PM */

#define PCMCIA_SOCKET_CLASS_PM_OPS NULL

#endif /* CONFIG_PM */

const struct class pcmcia_socket_class = {
	.name = "pcmcia_socket",
	.dev_uevent = pcmcia_socket_uevent,
	.dev_release = pcmcia_release_socket,
	.class_release = pcmcia_release_socket_class,
	.pm = PCMCIA_SOCKET_CLASS_PM_OPS,
};
EXPORT_SYMBOL(pcmcia_socket_class);


static int __init init_pcmcia_cs(void)
{
	init_completion(&pcmcia_unload);
	return class_register(&pcmcia_socket_class);
}

static void __exit exit_pcmcia_cs(void)
{
	class_unregister(&pcmcia_socket_class);
	wait_for_completion(&pcmcia_unload);
}

subsys_initcall(init_pcmcia_cs);
module_exit(exit_pcmcia_cs);

