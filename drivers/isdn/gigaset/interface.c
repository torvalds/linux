/*
 * interface to user space for the gigaset driver
 *
 * Copyright (c) 2004 by Hansjoerg Lipp <hjlipp@web.de>
 *
 * =====================================================================
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License as
 *    published by the Free Software Foundation; either version 2 of
 *    the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"
#include <linux/gigaset_dev.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

/*** our ioctls ***/

static int if_lock(struct cardstate *cs, int *arg)
{
	int cmd = *arg;

	gig_dbg(DEBUG_IF, "%u: if_lock (%d)", cs->minor_index, cmd);

	if (cmd > 1)
		return -EINVAL;

	if (cmd < 0) {
		*arg = atomic_read(&cs->mstate) == MS_LOCKED; //FIXME remove?
		return 0;
	}

	if (!cmd && atomic_read(&cs->mstate) == MS_LOCKED
	    && atomic_read(&cs->connected)) {
		cs->ops->set_modem_ctrl(cs, 0, TIOCM_DTR|TIOCM_RTS);
		cs->ops->baud_rate(cs, B115200);
		cs->ops->set_line_ctrl(cs, CS8);
		cs->control_state = TIOCM_DTR|TIOCM_RTS;
	}

	cs->waiting = 1;
	if (!gigaset_add_event(cs, &cs->at_state, EV_IF_LOCK,
			       NULL, cmd, NULL)) {
		cs->waiting = 0;
		return -ENOMEM;
	}

	gig_dbg(DEBUG_CMD, "scheduling IF_LOCK");
	gigaset_schedule_event(cs);

	wait_event(cs->waitqueue, !cs->waiting);

	if (cs->cmd_result >= 0) {
		*arg = cs->cmd_result;
		return 0;
	}

	return cs->cmd_result;
}

static int if_version(struct cardstate *cs, unsigned arg[4])
{
	static const unsigned version[4] = GIG_VERSION;
	static const unsigned compat[4] = GIG_COMPAT;
	unsigned cmd = arg[0];

	gig_dbg(DEBUG_IF, "%u: if_version (%d)", cs->minor_index, cmd);

	switch (cmd) {
	case GIGVER_DRIVER:
		memcpy(arg, version, sizeof version);
		return 0;
	case GIGVER_COMPAT:
		memcpy(arg, compat, sizeof compat);
		return 0;
	case GIGVER_FWBASE:
		cs->waiting = 1;
		if (!gigaset_add_event(cs, &cs->at_state, EV_IF_VER,
				       NULL, 0, arg)) {
			cs->waiting = 0;
			return -ENOMEM;
		}

		gig_dbg(DEBUG_CMD, "scheduling IF_VER");
		gigaset_schedule_event(cs);

		wait_event(cs->waitqueue, !cs->waiting);

		if (cs->cmd_result >= 0)
			return 0;

		return cs->cmd_result;
	default:
		return -EINVAL;
	}
}

static int if_config(struct cardstate *cs, int *arg)
{
	gig_dbg(DEBUG_IF, "%u: if_config (%d)", cs->minor_index, *arg);

	if (*arg != 1)
		return -EINVAL;

	if (atomic_read(&cs->mstate) != MS_LOCKED)
		return -EBUSY;

	*arg = 0;
	return gigaset_enterconfigmode(cs);
}

/*** the terminal driver ***/
/* stolen from usbserial and some other tty drivers */

static int  if_open(struct tty_struct *tty, struct file *filp);
static void if_close(struct tty_struct *tty, struct file *filp);
static int  if_ioctl(struct tty_struct *tty, struct file *file,
		     unsigned int cmd, unsigned long arg);
static int  if_write_room(struct tty_struct *tty);
static int  if_chars_in_buffer(struct tty_struct *tty);
static void if_throttle(struct tty_struct *tty);
static void if_unthrottle(struct tty_struct *tty);
static void if_set_termios(struct tty_struct *tty, struct termios *old);
static int  if_tiocmget(struct tty_struct *tty, struct file *file);
static int  if_tiocmset(struct tty_struct *tty, struct file *file,
			unsigned int set, unsigned int clear);
static int  if_write(struct tty_struct *tty,
		     const unsigned char *buf, int count);

static struct tty_operations if_ops = {
	.open =			if_open,
	.close =		if_close,
	.ioctl =		if_ioctl,
	.write =		if_write,
	.write_room =		if_write_room,
	.chars_in_buffer =	if_chars_in_buffer,
	.set_termios =		if_set_termios,
	.throttle =		if_throttle,
	.unthrottle =		if_unthrottle,
#if 0
	.break_ctl =		serial_break,
#endif
	.tiocmget =		if_tiocmget,
	.tiocmset =		if_tiocmset,
};

static int if_open(struct tty_struct *tty, struct file *filp)
{
	struct cardstate *cs;
	unsigned long flags;

	gig_dbg(DEBUG_IF, "%d+%d: %s()",
		tty->driver->minor_start, tty->index, __func__);

	tty->driver_data = NULL;

	cs = gigaset_get_cs_by_tty(tty);
	if (!cs)
		return -ENODEV;

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?
	tty->driver_data = cs;

	++cs->open_count;

	if (cs->open_count == 1) {
		spin_lock_irqsave(&cs->lock, flags);
		cs->tty = tty;
		spin_unlock_irqrestore(&cs->lock, flags);
		tty->low_latency = 1; //FIXME test
	}

	mutex_unlock(&cs->mutex);
	return 0;
}

static void if_close(struct tty_struct *tty, struct file *filp)
{
	struct cardstate *cs;
	unsigned long flags;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	mutex_lock(&cs->mutex);

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else {
		if (!--cs->open_count) {
			spin_lock_irqsave(&cs->lock, flags);
			cs->tty = NULL;
			spin_unlock_irqrestore(&cs->lock, flags);
		}
	}

	mutex_unlock(&cs->mutex);
}

static int if_ioctl(struct tty_struct *tty, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	struct cardstate *cs;
	int retval = -ENODEV;
	int int_arg;
	unsigned char buf[6];
	unsigned version[4];

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s(0x%x)", cs->minor_index, __func__, cmd);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else {
		retval = 0;
		switch (cmd) {
		case GIGASET_REDIR:
			retval = get_user(int_arg, (int __user *) arg);
			if (retval >= 0)
				retval = if_lock(cs, &int_arg);
			if (retval >= 0)
				retval = put_user(int_arg, (int __user *) arg);
			break;
		case GIGASET_CONFIG:
			retval = get_user(int_arg, (int __user *) arg);
			if (retval >= 0)
				retval = if_config(cs, &int_arg);
			if (retval >= 0)
				retval = put_user(int_arg, (int __user *) arg);
			break;
		case GIGASET_BRKCHARS:
			//FIXME test if MS_LOCKED
			if (!atomic_read(&cs->connected)) {
				gig_dbg(DEBUG_ANY,
				    "can't communicate with unplugged device");
				retval = -ENODEV;
				break;
			}
			retval = copy_from_user(&buf,
					(const unsigned char __user *) arg, 6)
				? -EFAULT : 0;
			if (retval >= 0) {
				gigaset_dbg_buffer(DEBUG_IF, "GIGASET_BRKCHARS",
						6, (const unsigned char *) arg);
				retval = cs->ops->brkchars(cs, buf);
			}
			break;
		case GIGASET_VERSION:
			retval = copy_from_user(version,
					(unsigned __user *) arg, sizeof version)
				? -EFAULT : 0;
			if (retval >= 0)
				retval = if_version(cs, version);
			if (retval >= 0)
				retval = copy_to_user((unsigned __user *) arg,
						      version, sizeof version)
					? -EFAULT : 0;
			break;
		default:
			gig_dbg(DEBUG_ANY, "%s: arg not supported - 0x%04x",
				__func__, cmd);
			retval = -ENOIOCTLCMD;
		}
	}

	mutex_unlock(&cs->mutex);

	return retval;
}

static int if_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct cardstate *cs;
	int retval;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	// FIXME read from device?
	retval = cs->control_state & (TIOCM_RTS|TIOCM_DTR);

	mutex_unlock(&cs->mutex);

	return retval;
}

static int if_tiocmset(struct tty_struct *tty, struct file *file,
		       unsigned int set, unsigned int clear)
{
	struct cardstate *cs;
	int retval;
	unsigned mc;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s(0x%x, 0x%x)",
		cs->minor_index, __func__, set, clear);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	if (!atomic_read(&cs->connected)) {
		gig_dbg(DEBUG_ANY, "can't communicate with unplugged device");
		retval = -ENODEV;
	} else {
		mc = (cs->control_state | set) & ~clear & (TIOCM_RTS|TIOCM_DTR);
		retval = cs->ops->set_modem_ctrl(cs, cs->control_state, mc);
		cs->control_state = mc;
	}

	mutex_unlock(&cs->mutex);

	return retval;
}

static int if_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct cardstate *cs;
	int retval = -ENODEV;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else if (atomic_read(&cs->mstate) != MS_LOCKED) {
		warn("can't write to unlocked device");
		retval = -EBUSY;
	} else if (!atomic_read(&cs->connected)) {
		gig_dbg(DEBUG_ANY, "can't write to unplugged device");
		retval = -EBUSY; //FIXME
	} else {
		retval = cs->ops->write_cmd(cs, buf, count,
					    &cs->if_wake_tasklet);
	}

	mutex_unlock(&cs->mutex);

	return retval;
}

static int if_write_room(struct tty_struct *tty)
{
	struct cardstate *cs;
	int retval = -ENODEV;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else if (atomic_read(&cs->mstate) != MS_LOCKED) {
		warn("can't write to unlocked device");
		retval = -EBUSY; //FIXME
	} else if (!atomic_read(&cs->connected)) {
		gig_dbg(DEBUG_ANY, "can't write to unplugged device");
		retval = -EBUSY; //FIXME
	} else
		retval = cs->ops->write_room(cs);

	mutex_unlock(&cs->mutex);

	return retval;
}

static int if_chars_in_buffer(struct tty_struct *tty)
{
	struct cardstate *cs;
	int retval = -ENODEV;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return -ENODEV;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	if (mutex_lock_interruptible(&cs->mutex))
		return -ERESTARTSYS; // FIXME -EINTR?

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else if (atomic_read(&cs->mstate) != MS_LOCKED) {
		warn("can't write to unlocked device");
		retval = -EBUSY;
	} else if (!atomic_read(&cs->connected)) {
		gig_dbg(DEBUG_ANY, "can't write to unplugged device");
		retval = -EBUSY; //FIXME
	} else
		retval = cs->ops->chars_in_buffer(cs);

	mutex_unlock(&cs->mutex);

	return retval;
}

static void if_throttle(struct tty_struct *tty)
{
	struct cardstate *cs;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	mutex_lock(&cs->mutex);

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else {
		//FIXME
	}

	mutex_unlock(&cs->mutex);
}

static void if_unthrottle(struct tty_struct *tty)
{
	struct cardstate *cs;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	mutex_lock(&cs->mutex);

	if (!cs->open_count)
		warn("%s: device not opened", __func__);
	else {
		//FIXME
	}

	mutex_unlock(&cs->mutex);
}

static void if_set_termios(struct tty_struct *tty, struct termios *old)
{
	struct cardstate *cs;
	unsigned int iflag;
	unsigned int cflag;
	unsigned int old_cflag;
	unsigned int control_state, new_state;

	cs = (struct cardstate *) tty->driver_data;
	if (!cs) {
		err("cs==NULL in %s", __func__);
		return;
	}

	gig_dbg(DEBUG_IF, "%u: %s()", cs->minor_index, __func__);

	mutex_lock(&cs->mutex);

	if (!cs->open_count) {
		warn("%s: device not opened", __func__);
		goto out;
	}

	if (!atomic_read(&cs->connected)) {
		gig_dbg(DEBUG_ANY, "can't communicate with unplugged device");
		goto out;
	}

	// stolen from mct_u232.c
	iflag = tty->termios->c_iflag;
	cflag = tty->termios->c_cflag;
	old_cflag = old ? old->c_cflag : cflag; //FIXME?
	gig_dbg(DEBUG_IF, "%u: iflag %x cflag %x old %x",
		cs->minor_index, iflag, cflag, old_cflag);

	/* get a local copy of the current port settings */
	control_state = cs->control_state;

	/*
	 * Update baud rate.
	 * Do not attempt to cache old rates and skip settings,
	 * disconnects screw such tricks up completely.
	 * Premature optimization is the root of all evil.
	 */

	/* reassert DTR and (maybe) RTS on transition from B0 */
	if ((old_cflag & CBAUD) == B0) {
		new_state = control_state | TIOCM_DTR;
		/* don't set RTS if using hardware flow control */
		if (!(old_cflag & CRTSCTS))
			new_state |= TIOCM_RTS;
		gig_dbg(DEBUG_IF, "%u: from B0 - set DTR%s",
			cs->minor_index,
			(new_state & TIOCM_RTS) ? " only" : "/RTS");
		cs->ops->set_modem_ctrl(cs, control_state, new_state);
		control_state = new_state;
	}

	cs->ops->baud_rate(cs, cflag & CBAUD);

	if ((cflag & CBAUD) == B0) {
		/* Drop RTS and DTR */
		gig_dbg(DEBUG_IF, "%u: to B0 - drop DTR/RTS", cs->minor_index);
		new_state = control_state & ~(TIOCM_DTR | TIOCM_RTS);
		cs->ops->set_modem_ctrl(cs, control_state, new_state);
		control_state = new_state;
	}

	/*
	 * Update line control register (LCR)
	 */

	cs->ops->set_line_ctrl(cs, cflag);

#if 0
	//FIXME this hangs M101 [ts 2005-03-09]
	//FIXME do we need this?
	/*
	 * Set flow control: well, I do not really now how to handle DTR/RTS.
	 * Just do what we have seen with SniffUSB on Win98.
	 */
	/* Drop DTR/RTS if no flow control otherwise assert */
	gig_dbg(DEBUG_IF, "%u: control_state %x",
		cs->minor_index, control_state);
	new_state = control_state;
	if ((iflag & IXOFF) || (iflag & IXON) || (cflag & CRTSCTS))
		new_state |= TIOCM_DTR | TIOCM_RTS;
	else
		new_state &= ~(TIOCM_DTR | TIOCM_RTS);
	if (new_state != control_state) {
		gig_dbg(DEBUG_IF, "%u: new_state %x",
			cs->minor_index, new_state);
		gigaset_set_modem_ctrl(cs, control_state, new_state);
		control_state = new_state;
	}
#endif

	/* save off the modified port settings */
	cs->control_state = control_state;

out:
	mutex_unlock(&cs->mutex);
}


/* wakeup tasklet for the write operation */
static void if_wake(unsigned long data)
{
	struct cardstate *cs = (struct cardstate *) data;
	struct tty_struct *tty;

	tty = cs->tty;
	if (!tty)
		return;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup) {
		gig_dbg(DEBUG_IF, "write wakeup call");
		tty->ldisc.write_wakeup(tty);
	}

	wake_up_interruptible(&tty->write_wait);
}

/*** interface to common ***/

void gigaset_if_init(struct cardstate *cs)
{
	struct gigaset_driver *drv;

	drv = cs->driver;
	if (!drv->have_tty)
		return;

	tasklet_init(&cs->if_wake_tasklet, &if_wake, (unsigned long) cs);
	tty_register_device(drv->tty, cs->minor_index, NULL);
}

void gigaset_if_free(struct cardstate *cs)
{
	struct gigaset_driver *drv;

	drv = cs->driver;
	if (!drv->have_tty)
		return;

	tasklet_disable(&cs->if_wake_tasklet);
	tasklet_kill(&cs->if_wake_tasklet);
	tty_unregister_device(drv->tty, cs->minor_index);
}

void gigaset_if_receive(struct cardstate *cs,
			unsigned char *buffer, size_t len)
{
	unsigned long flags;
	struct tty_struct *tty;

	spin_lock_irqsave(&cs->lock, flags);
	if ((tty = cs->tty) == NULL)
		gig_dbg(DEBUG_ANY, "receive on closed device");
	else {
		tty_buffer_request_room(tty, len);
		tty_insert_flip_string(tty, buffer, len);
		tty_flip_buffer_push(tty);
	}
	spin_unlock_irqrestore(&cs->lock, flags);
}
EXPORT_SYMBOL_GPL(gigaset_if_receive);

/* gigaset_if_initdriver
 * Initialize tty interface.
 * parameters:
 *	drv		Driver
 *	procname	Name of the driver (e.g. for /proc/tty/drivers)
 *	devname		Name of the device files (prefix without minor number)
 *	devfsname	Devfs name of the device files without %d
 */
void gigaset_if_initdriver(struct gigaset_driver *drv, const char *procname,
			   const char *devname, const char *devfsname)
{
	unsigned minors = drv->minors;
	int ret;
	struct tty_driver *tty;

	drv->have_tty = 0;

	if ((drv->tty = alloc_tty_driver(minors)) == NULL)
		goto enomem;
	tty = drv->tty;

	tty->magic =		TTY_DRIVER_MAGIC,
	tty->major =		GIG_MAJOR,
	tty->type =		TTY_DRIVER_TYPE_SERIAL,
	tty->subtype =		SERIAL_TYPE_NORMAL,
	tty->flags =		TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS,

	tty->driver_name =	procname;
	tty->name =		devname;
	tty->minor_start =	drv->minor;
	tty->num =		drv->minors;

	tty->owner =		THIS_MODULE;
	tty->devfs_name =	devfsname;

	tty->init_termios          = tty_std_termios; //FIXME
	tty->init_termios.c_cflag  = B9600 | CS8 | CREAD | HUPCL | CLOCAL; //FIXME
	tty_set_operations(tty, &if_ops);

	ret = tty_register_driver(tty);
	if (ret < 0) {
		warn("failed to register tty driver (error %d)", ret);
		goto error;
	}
	gig_dbg(DEBUG_IF, "tty driver initialized");
	drv->have_tty = 1;
	return;

enomem:
	warn("could not allocate tty structures");
error:
	if (drv->tty)
		put_tty_driver(drv->tty);
}

void gigaset_if_freedriver(struct gigaset_driver *drv)
{
	if (!drv->have_tty)
		return;

	drv->have_tty = 0;
	tty_unregister_driver(drv->tty);
	put_tty_driver(drv->tty);
}
