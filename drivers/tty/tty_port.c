/*
 * Tty port functions
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/module.h>

void tty_port_init(struct tty_port *port)
{
	memset(port, 0, sizeof(*port));
	tty_buffer_init(port);
	init_waitqueue_head(&port->open_wait);
	init_waitqueue_head(&port->close_wait);
	init_waitqueue_head(&port->delta_msr_wait);
	mutex_init(&port->mutex);
	mutex_init(&port->buf_mutex);
	spin_lock_init(&port->lock);
	port->close_delay = (50 * HZ) / 100;
	port->closing_wait = (3000 * HZ) / 100;
	kref_init(&port->kref);
}
EXPORT_SYMBOL(tty_port_init);

/**
 * tty_port_link_device - link tty and tty_port
 * @port: tty_port of the device
 * @driver: tty_driver for this device
 * @index: index of the tty
 *
 * Provide the tty layer wit ha link from a tty (specified by @index) to a
 * tty_port (@port). Use this only if neither tty_port_register_device nor
 * tty_port_install is used in the driver. If used, this has to be called before
 * tty_register_driver.
 */
void tty_port_link_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index)
{
	if (WARN_ON(index >= driver->num))
		return;
	driver->ports[index] = port;
}
EXPORT_SYMBOL_GPL(tty_port_link_device);

/**
 * tty_port_register_device - register tty device
 * @port: tty_port of the device
 * @driver: tty_driver for this device
 * @index: index of the tty
 * @device: parent if exists, otherwise NULL
 *
 * It is the same as tty_register_device except the provided @port is linked to
 * a concrete tty specified by @index. Use this or tty_port_install (or both).
 * Call tty_port_link_device as a last resort.
 */
struct device *tty_port_register_device(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device)
{
	tty_port_link_device(port, driver, index);
	return tty_register_device(driver, index, device);
}
EXPORT_SYMBOL_GPL(tty_port_register_device);

/**
 * tty_port_register_device_attr - register tty device
 * @port: tty_port of the device
 * @driver: tty_driver for this device
 * @index: index of the tty
 * @device: parent if exists, otherwise NULL
 * @drvdata: Driver data to be set to device.
 * @attr_grp: Attribute group to be set on device.
 *
 * It is the same as tty_register_device_attr except the provided @port is
 * linked to a concrete tty specified by @index. Use this or tty_port_install
 * (or both). Call tty_port_link_device as a last resort.
 */
struct device *tty_port_register_device_attr(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *device, void *drvdata,
		const struct attribute_group **attr_grp)
{
	tty_port_link_device(port, driver, index);
	return tty_register_device_attr(driver, index, device, drvdata,
			attr_grp);
}
EXPORT_SYMBOL_GPL(tty_port_register_device_attr);

int tty_port_alloc_xmit_buf(struct tty_port *port)
{
	/* We may sleep in get_zeroed_page() */
	mutex_lock(&port->buf_mutex);
	if (port->xmit_buf == NULL)
		port->xmit_buf = (unsigned char *)get_zeroed_page(GFP_KERNEL);
	mutex_unlock(&port->buf_mutex);
	if (port->xmit_buf == NULL)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL(tty_port_alloc_xmit_buf);

void tty_port_free_xmit_buf(struct tty_port *port)
{
	mutex_lock(&port->buf_mutex);
	if (port->xmit_buf != NULL) {
		free_page((unsigned long)port->xmit_buf);
		port->xmit_buf = NULL;
	}
	mutex_unlock(&port->buf_mutex);
}
EXPORT_SYMBOL(tty_port_free_xmit_buf);

static void tty_port_destructor(struct kref *kref)
{
	struct tty_port *port = container_of(kref, struct tty_port, kref);
	if (port->xmit_buf)
		free_page((unsigned long)port->xmit_buf);
	tty_buffer_free_all(port);
	if (port->ops && port->ops->destruct)
		port->ops->destruct(port);
	else
		kfree(port);
}

void tty_port_put(struct tty_port *port)
{
	if (port)
		kref_put(&port->kref, tty_port_destructor);
}
EXPORT_SYMBOL(tty_port_put);

/**
 *	tty_port_tty_get	-	get a tty reference
 *	@port: tty port
 *
 *	Return a refcount protected tty instance or NULL if the port is not
 *	associated with a tty (eg due to close or hangup)
 */

struct tty_struct *tty_port_tty_get(struct tty_port *port)
{
	unsigned long flags;
	struct tty_struct *tty;

	spin_lock_irqsave(&port->lock, flags);
	tty = tty_kref_get(port->tty);
	spin_unlock_irqrestore(&port->lock, flags);
	return tty;
}
EXPORT_SYMBOL(tty_port_tty_get);

/**
 *	tty_port_tty_set	-	set the tty of a port
 *	@port: tty port
 *	@tty: the tty
 *
 *	Associate the port and tty pair. Manages any internal refcounts.
 *	Pass NULL to deassociate a port
 */

void tty_port_tty_set(struct tty_port *port, struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (port->tty)
		tty_kref_put(port->tty);
	port->tty = tty_kref_get(tty);
	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL(tty_port_tty_set);

static void tty_port_shutdown(struct tty_port *port)
{
	mutex_lock(&port->mutex);
	if (port->ops->shutdown && !port->console &&
		test_and_clear_bit(ASYNCB_INITIALIZED, &port->flags))
			port->ops->shutdown(port);
	mutex_unlock(&port->mutex);
}

/**
 *	tty_port_hangup		-	hangup helper
 *	@port: tty port
 *
 *	Perform port level tty hangup flag and count changes. Drop the tty
 *	reference.
 */

void tty_port_hangup(struct tty_port *port)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	port->count = 0;
	port->flags &= ~ASYNC_NORMAL_ACTIVE;
	if (port->tty) {
		set_bit(TTY_IO_ERROR, &port->tty->flags);
		tty_kref_put(port->tty);
	}
	port->tty = NULL;
	spin_unlock_irqrestore(&port->lock, flags);
	wake_up_interruptible(&port->open_wait);
	wake_up_interruptible(&port->delta_msr_wait);
	tty_port_shutdown(port);
}
EXPORT_SYMBOL(tty_port_hangup);

/**
 *	tty_port_carrier_raised	-	carrier raised check
 *	@port: tty port
 *
 *	Wrapper for the carrier detect logic. For the moment this is used
 *	to hide some internal details. This will eventually become entirely
 *	internal to the tty port.
 */

int tty_port_carrier_raised(struct tty_port *port)
{
	if (port->ops->carrier_raised == NULL)
		return 1;
	return port->ops->carrier_raised(port);
}
EXPORT_SYMBOL(tty_port_carrier_raised);

/**
 *	tty_port_raise_dtr_rts	-	Raise DTR/RTS
 *	@port: tty port
 *
 *	Wrapper for the DTR/RTS raise logic. For the moment this is used
 *	to hide some internal details. This will eventually become entirely
 *	internal to the tty port.
 */

void tty_port_raise_dtr_rts(struct tty_port *port)
{
	if (port->ops->dtr_rts)
		port->ops->dtr_rts(port, 1);
}
EXPORT_SYMBOL(tty_port_raise_dtr_rts);

/**
 *	tty_port_lower_dtr_rts	-	Lower DTR/RTS
 *	@port: tty port
 *
 *	Wrapper for the DTR/RTS raise logic. For the moment this is used
 *	to hide some internal details. This will eventually become entirely
 *	internal to the tty port.
 */

void tty_port_lower_dtr_rts(struct tty_port *port)
{
	if (port->ops->dtr_rts)
		port->ops->dtr_rts(port, 0);
}
EXPORT_SYMBOL(tty_port_lower_dtr_rts);

/**
 *	tty_port_block_til_ready	-	Waiting logic for tty open
 *	@port: the tty port being opened
 *	@tty: the tty device being bound
 *	@filp: the file pointer of the opener
 *
 *	Implement the core POSIX/SuS tty behaviour when opening a tty device.
 *	Handles:
 *		- hangup (both before and during)
 *		- non blocking open
 *		- rts/dtr/dcd
 *		- signals
 *		- port flags and counts
 *
 *	The passed tty_port must implement the carrier_raised method if it can
 *	do carrier detect and the dtr_rts method if it supports software
 *	management of these lines. Note that the dtr/rts raise is done each
 *	iteration as a hangup may have previously dropped them while we wait.
 */

int tty_port_block_til_ready(struct tty_port *port,
				struct tty_struct *tty, struct file *filp)
{
	int do_clocal = 0, retval;
	unsigned long flags;
	DEFINE_WAIT(wait);

	/* block if port is in the process of being closed */
	if (tty_hung_up_p(filp) || port->flags & ASYNC_CLOSING) {
		wait_event_interruptible_tty(tty, port->close_wait,
				!(port->flags & ASYNC_CLOSING));
		if (port->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
	}

	/* if non-blocking mode is set we can pass directly to open unless
	   the port has just hung up or is in another error state */
	if (tty->flags & (1 << TTY_IO_ERROR)) {
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (filp->f_flags & O_NONBLOCK) {
		/* Indicate we are open */
		if (tty->termios.c_cflag & CBAUD)
			tty_port_raise_dtr_rts(port);
		port->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (C_CLOCAL(tty))
		do_clocal = 1;

	/* Block waiting until we can proceed. We may need to wait for the
	   carrier, but we must also wait for any close that is in progress
	   before the next open may complete */

	retval = 0;

	/* The port lock protects the port counts */
	spin_lock_irqsave(&port->lock, flags);
	if (!tty_hung_up_p(filp))
		port->count--;
	port->blocked_open++;
	spin_unlock_irqrestore(&port->lock, flags);

	while (1) {
		/* Indicate we are open */
		if (tty->termios.c_cflag & CBAUD)
			tty_port_raise_dtr_rts(port);

		prepare_to_wait(&port->open_wait, &wait, TASK_INTERRUPTIBLE);
		/* Check for a hangup or uninitialised port.
							Return accordingly */
		if (tty_hung_up_p(filp) || !(port->flags & ASYNC_INITIALIZED)) {
			if (port->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		/*
		 * Probe the carrier. For devices with no carrier detect
		 * tty_port_carrier_raised will always return true.
		 * Never ask drivers if CLOCAL is set, this causes troubles
		 * on some hardware.
		 */
		if (!(port->flags & ASYNC_CLOSING) &&
				(do_clocal || tty_port_carrier_raised(port)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		tty_unlock(tty);
		schedule();
		tty_lock(tty);
	}
	finish_wait(&port->open_wait, &wait);

	/* Update counts. A parallel hangup will have set count to zero and
	   we must not mess that up further */
	spin_lock_irqsave(&port->lock, flags);
	if (!tty_hung_up_p(filp))
		port->count++;
	port->blocked_open--;
	if (retval == 0)
		port->flags |= ASYNC_NORMAL_ACTIVE;
	spin_unlock_irqrestore(&port->lock, flags);
	return retval;
}
EXPORT_SYMBOL(tty_port_block_til_ready);

int tty_port_close_start(struct tty_port *port,
				struct tty_struct *tty, struct file *filp)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&port->lock, flags);
		return 0;
	}

	if (tty->count == 1 && port->count != 1) {
		printk(KERN_WARNING
		    "tty_port_close_start: tty->count = 1 port count = %d.\n",
								port->count);
		port->count = 1;
	}
	if (--port->count < 0) {
		printk(KERN_WARNING "tty_port_close_start: count = %d\n",
								port->count);
		port->count = 0;
	}

	if (port->count) {
		spin_unlock_irqrestore(&port->lock, flags);
		if (port->ops->drop)
			port->ops->drop(port);
		return 0;
	}
	set_bit(ASYNCB_CLOSING, &port->flags);
	tty->closing = 1;
	spin_unlock_irqrestore(&port->lock, flags);
	/* Don't block on a stalled port, just pull the chain */
	if (tty->flow_stopped)
		tty_driver_flush_buffer(tty);
	if (test_bit(ASYNCB_INITIALIZED, &port->flags) &&
			port->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent_from_close(tty, port->closing_wait);
	if (port->drain_delay) {
		unsigned int bps = tty_get_baud_rate(tty);
		long timeout;

		if (bps > 1200)
			timeout = max_t(long,
				(HZ * 10 * port->drain_delay) / bps, HZ / 10);
		else
			timeout = 2 * HZ;
		schedule_timeout_interruptible(timeout);
	}
	/* Flush the ldisc buffering */
	tty_ldisc_flush(tty);

	/* Drop DTR/RTS if HUPCL is set. This causes any attached modem to
	   hang up the line */
	if (tty->termios.c_cflag & HUPCL)
		tty_port_lower_dtr_rts(port);

	/* Don't call port->drop for the last reference. Callers will want
	   to drop the last active reference in ->shutdown() or the tty
	   shutdown path */
	return 1;
}
EXPORT_SYMBOL(tty_port_close_start);

void tty_port_close_end(struct tty_port *port, struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);
	tty->closing = 0;

	if (port->blocked_open) {
		spin_unlock_irqrestore(&port->lock, flags);
		if (port->close_delay) {
			msleep_interruptible(
				jiffies_to_msecs(port->close_delay));
		}
		spin_lock_irqsave(&port->lock, flags);
		wake_up_interruptible(&port->open_wait);
	}
	port->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible(&port->close_wait);
	spin_unlock_irqrestore(&port->lock, flags);
}
EXPORT_SYMBOL(tty_port_close_end);

void tty_port_close(struct tty_port *port, struct tty_struct *tty,
							struct file *filp)
{
	if (tty_port_close_start(port, tty, filp) == 0)
		return;
	tty_port_shutdown(port);
	set_bit(TTY_IO_ERROR, &tty->flags);
	tty_port_close_end(port, tty);
	tty_port_tty_set(port, NULL);
}
EXPORT_SYMBOL(tty_port_close);

/**
 * tty_port_install - generic tty->ops->install handler
 * @port: tty_port of the device
 * @driver: tty_driver for this device
 * @tty: tty to be installed
 *
 * It is the same as tty_standard_install except the provided @port is linked
 * to a concrete tty specified by @tty. Use this or tty_port_register_device
 * (or both). Call tty_port_link_device as a last resort.
 */
int tty_port_install(struct tty_port *port, struct tty_driver *driver,
		struct tty_struct *tty)
{
	tty->port = port;
	return tty_standard_install(driver, tty);
}
EXPORT_SYMBOL_GPL(tty_port_install);

int tty_port_open(struct tty_port *port, struct tty_struct *tty,
							struct file *filp)
{
	spin_lock_irq(&port->lock);
	if (!tty_hung_up_p(filp))
		++port->count;
	spin_unlock_irq(&port->lock);
	tty_port_tty_set(port, tty);

	/*
	 * Do the device-specific open only if the hardware isn't
	 * already initialized. Serialize open and shutdown using the
	 * port mutex.
	 */

	mutex_lock(&port->mutex);

	if (!test_bit(ASYNCB_INITIALIZED, &port->flags)) {
		clear_bit(TTY_IO_ERROR, &tty->flags);
		if (port->ops->activate) {
			int retval = port->ops->activate(port, tty);
			if (retval) {
				mutex_unlock(&port->mutex);
				return retval;
			}
		}
		set_bit(ASYNCB_INITIALIZED, &port->flags);
	}
	mutex_unlock(&port->mutex);
	return tty_port_block_til_ready(port, tty, filp);
}

EXPORT_SYMBOL(tty_port_open);
