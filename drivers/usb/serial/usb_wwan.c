/*
  USB Driver layer for GSM modems

  Copyright (C) 2005  Matthias Urlichs <smurf@smurf.noris.de>

  This driver is free software; you can redistribute it and/or modify
  it under the terms of Version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  Portions copied from the Keyspan driver by Hugh Blemings <hugh@blemings.org>

  History: see the git log.

  Work sponsored by: Sigos GmbH, Germany <info@sigos.de>

  This driver exists because the "normal" serial driver doesn't work too well
  with GSM modems. Issues:
  - data loss -- one single Receive URB is not nearly enough
  - controlling the baud rate doesn't make sense
*/

#define DRIVER_AUTHOR "Matthias Urlichs <smurf@smurf.noris.de>"
#define DRIVER_DESC "USB Driver for GSM modems"

#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial.h>
#include "usb-wwan.h"

void usb_wwan_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;

	intfdata = port->serial->private;

	if (!intfdata->send_setup)
		return;

	portdata = usb_get_serial_port_data(port);
	/* FIXME: locking */
	portdata->rts_state = on;
	portdata->dtr_state = on;

	intfdata->send_setup(port);
}
EXPORT_SYMBOL(usb_wwan_dtr_rts);

void usb_wwan_set_termios(struct tty_struct *tty,
			  struct usb_serial_port *port,
			  struct ktermios *old_termios)
{
	struct usb_wwan_intf_private *intfdata = port->serial->private;

	/* Doesn't support option setting */
	tty_termios_copy_hw(&tty->termios, old_termios);

	if (intfdata->send_setup)
		intfdata->send_setup(port);
}
EXPORT_SYMBOL(usb_wwan_set_termios);

int usb_wwan_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned int value;
	struct usb_wwan_port_private *portdata;

	portdata = usb_get_serial_port_data(port);

	value = ((portdata->rts_state) ? TIOCM_RTS : 0) |
	    ((portdata->dtr_state) ? TIOCM_DTR : 0) |
	    ((portdata->cts_state) ? TIOCM_CTS : 0) |
	    ((portdata->dsr_state) ? TIOCM_DSR : 0) |
	    ((portdata->dcd_state) ? TIOCM_CAR : 0) |
	    ((portdata->ri_state) ? TIOCM_RNG : 0);

	return value;
}
EXPORT_SYMBOL(usb_wwan_tiocmget);

int usb_wwan_tiocmset(struct tty_struct *tty,
		      unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;

	portdata = usb_get_serial_port_data(port);
	intfdata = port->serial->private;

	if (!intfdata->send_setup)
		return -EINVAL;

	/* FIXME: what locks portdata fields ? */
	if (set & TIOCM_RTS)
		portdata->rts_state = 1;
	if (set & TIOCM_DTR)
		portdata->dtr_state = 1;

	if (clear & TIOCM_RTS)
		portdata->rts_state = 0;
	if (clear & TIOCM_DTR)
		portdata->dtr_state = 0;
	return intfdata->send_setup(port);
}
EXPORT_SYMBOL(usb_wwan_tiocmset);

static int get_serial_info(struct usb_serial_port *port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));
	tmp.line            = port->serial->minor;
	tmp.port            = port->number;
	tmp.baud_base       = tty_get_baud_rate(port->port.tty);
	tmp.close_delay	    = port->port.close_delay / 10;
	tmp.closing_wait    = port->port.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				 ASYNC_CLOSING_WAIT_NONE :
				 port->port.closing_wait / 10;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct usb_serial_port *port,
			   struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	unsigned int closing_wait, close_delay;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	close_delay = new_serial.close_delay * 10;
	closing_wait = new_serial.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE : new_serial.closing_wait * 10;

	mutex_lock(&port->port.mutex);

	if (!capable(CAP_SYS_ADMIN)) {
		if ((close_delay != port->port.close_delay) ||
		    (closing_wait != port->port.closing_wait))
			retval = -EPERM;
		else
			retval = -EOPNOTSUPP;
	} else {
		port->port.close_delay  = close_delay;
		port->port.closing_wait = closing_wait;
	}

	mutex_unlock(&port->port.mutex);
	return retval;
}

int usb_wwan_ioctl(struct tty_struct *tty,
		   unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;

	dev_dbg(&port->dev, "%s cmd 0x%04x\n", __func__, cmd);

	switch (cmd) {
	case TIOCGSERIAL:
		return get_serial_info(port,
				       (struct serial_struct __user *) arg);
	case TIOCSSERIAL:
		return set_serial_info(port,
				       (struct serial_struct __user *) arg);
	default:
		break;
	}

	dev_dbg(&port->dev, "%s arg not supported\n", __func__);

	return -ENOIOCTLCMD;
}
EXPORT_SYMBOL(usb_wwan_ioctl);

/* Write */
int usb_wwan_write(struct tty_struct *tty, struct usb_serial_port *port,
		   const unsigned char *buf, int count)
{
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;
	int i;
	int left, todo;
	struct urb *this_urb = NULL;	/* spurious */
	int err;
	unsigned long flags;

	portdata = usb_get_serial_port_data(port);
	intfdata = port->serial->private;

	dev_dbg(&port->dev, "%s: write (%d chars)\n", __func__, count);

	i = 0;
	left = count;
	for (i = 0; left > 0 && i < N_OUT_URB; i++) {
		todo = left;
		if (todo > OUT_BUFLEN)
			todo = OUT_BUFLEN;

		this_urb = portdata->out_urbs[i];
		if (test_and_set_bit(i, &portdata->out_busy)) {
			if (time_before(jiffies,
					portdata->tx_start_time[i] + 10 * HZ))
				continue;
			usb_unlink_urb(this_urb);
			continue;
		}
		dev_dbg(&port->dev, "%s: endpoint %d buf %d\n", __func__,
			usb_pipeendpoint(this_urb->pipe), i);

		err = usb_autopm_get_interface_async(port->serial->interface);
		if (err < 0) {
			clear_bit(i, &portdata->out_busy);
			break;
		}

		/* send the data */
		memcpy(this_urb->transfer_buffer, buf, todo);
		this_urb->transfer_buffer_length = todo;

		spin_lock_irqsave(&intfdata->susp_lock, flags);
		if (intfdata->suspended) {
			usb_anchor_urb(this_urb, &portdata->delayed);
			spin_unlock_irqrestore(&intfdata->susp_lock, flags);
		} else {
			intfdata->in_flight++;
			spin_unlock_irqrestore(&intfdata->susp_lock, flags);
			err = usb_submit_urb(this_urb, GFP_ATOMIC);
			if (err) {
				dev_dbg(&port->dev,
					"usb_submit_urb %p (write bulk) failed (%d)\n",
					this_urb, err);
				clear_bit(i, &portdata->out_busy);
				spin_lock_irqsave(&intfdata->susp_lock, flags);
				intfdata->in_flight--;
				spin_unlock_irqrestore(&intfdata->susp_lock,
						       flags);
				usb_autopm_put_interface_async(port->serial->interface);
				break;
			}
		}

		portdata->tx_start_time[i] = jiffies;
		buf += todo;
		left -= todo;
	}

	count -= left;
	dev_dbg(&port->dev, "%s: wrote (did %d)\n", __func__, count);
	return count;
}
EXPORT_SYMBOL(usb_wwan_write);

static void usb_wwan_indat_callback(struct urb *urb)
{
	int err;
	int endpoint;
	struct usb_serial_port *port;
	struct device *dev;
	unsigned char *data = urb->transfer_buffer;
	int status = urb->status;

	endpoint = usb_pipeendpoint(urb->pipe);
	port = urb->context;
	dev = &port->dev;

	if (status) {
		dev_dbg(dev, "%s: nonzero status: %d on endpoint %02x.\n",
			__func__, status, endpoint);
	} else {
		if (urb->actual_length) {
			tty_insert_flip_string(&port->port, data,
					urb->actual_length);
			tty_flip_buffer_push(&port->port);
		} else
			dev_dbg(dev, "%s: empty read urb received\n", __func__);
	}
	/* Resubmit urb so we continue receiving */
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		if (err != -EPERM) {
			dev_err(dev, "%s: resubmit read urb failed. (%d)\n",
				__func__, err);
			/* busy also in error unless we are killed */
			usb_mark_last_busy(port->serial->dev);
		}
	} else {
		usb_mark_last_busy(port->serial->dev);
	}
}

static void usb_wwan_outdat_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;
	int i;

	port = urb->context;
	intfdata = port->serial->private;

	usb_serial_port_softint(port);
	usb_autopm_put_interface_async(port->serial->interface);
	portdata = usb_get_serial_port_data(port);
	spin_lock(&intfdata->susp_lock);
	intfdata->in_flight--;
	spin_unlock(&intfdata->susp_lock);

	for (i = 0; i < N_OUT_URB; ++i) {
		if (portdata->out_urbs[i] == urb) {
			smp_mb__before_clear_bit();
			clear_bit(i, &portdata->out_busy);
			break;
		}
	}
}

int usb_wwan_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_wwan_port_private *portdata;
	int i;
	int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i = 0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		if (this_urb && !test_bit(i, &portdata->out_busy))
			data_len += OUT_BUFLEN;
	}

	dev_dbg(&port->dev, "%s: %d\n", __func__, data_len);
	return data_len;
}
EXPORT_SYMBOL(usb_wwan_write_room);

int usb_wwan_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_wwan_port_private *portdata;
	int i;
	int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i = 0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		/* FIXME: This locking is insufficient as this_urb may
		   go unused during the test */
		if (this_urb && test_bit(i, &portdata->out_busy))
			data_len += this_urb->transfer_buffer_length;
	}
	dev_dbg(&port->dev, "%s: %d\n", __func__, data_len);
	return data_len;
}
EXPORT_SYMBOL(usb_wwan_chars_in_buffer);

int usb_wwan_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;
	struct usb_serial *serial = port->serial;
	int i, err;
	struct urb *urb;

	portdata = usb_get_serial_port_data(port);
	intfdata = serial->private;

	if (port->interrupt_in_urb) {
		err = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (err) {
			dev_dbg(&port->dev, "%s: submit int urb failed: %d\n",
				__func__, err);
		}
	}

	/* Start reading from the IN endpoint */
	for (i = 0; i < N_IN_URB; i++) {
		urb = portdata->in_urbs[i];
		if (!urb)
			continue;
		err = usb_submit_urb(urb, GFP_KERNEL);
		if (err) {
			dev_dbg(&port->dev, "%s: submit urb %d failed (%d) %d\n",
				__func__, i, err, urb->transfer_buffer_length);
		}
	}

	if (intfdata->send_setup)
		intfdata->send_setup(port);

	serial->interface->needs_remote_wakeup = 1;
	spin_lock_irq(&intfdata->susp_lock);
	portdata->opened = 1;
	spin_unlock_irq(&intfdata->susp_lock);
	/* this balances a get in the generic USB serial code */
	usb_autopm_put_interface(serial->interface);

	return 0;
}
EXPORT_SYMBOL(usb_wwan_open);

static void unbusy_queued_urb(struct urb *urb,
					struct usb_wwan_port_private *portdata)
{
	int i;

	for (i = 0; i < N_OUT_URB; i++) {
		if (urb == portdata->out_urbs[i]) {
			clear_bit(i, &portdata->out_busy);
			break;
		}
	}
}

void usb_wwan_close(struct usb_serial_port *port)
{
	int i;
	struct usb_serial *serial = port->serial;
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata = port->serial->private;
	struct urb *urb;

	portdata = usb_get_serial_port_data(port);

	/* Stop reading/writing urbs */
	spin_lock_irq(&intfdata->susp_lock);
	portdata->opened = 0;
	spin_unlock_irq(&intfdata->susp_lock);

	for (;;) {
		urb = usb_get_from_anchor(&portdata->delayed);
		if (!urb)
			break;
		unbusy_queued_urb(urb, portdata);
		usb_autopm_put_interface_async(serial->interface);
	}

	for (i = 0; i < N_IN_URB; i++)
		usb_kill_urb(portdata->in_urbs[i]);
	for (i = 0; i < N_OUT_URB; i++)
		usb_kill_urb(portdata->out_urbs[i]);
	usb_kill_urb(port->interrupt_in_urb);

	/* balancing - important as an error cannot be handled*/
	usb_autopm_get_interface_no_resume(serial->interface);
	serial->interface->needs_remote_wakeup = 0;
}
EXPORT_SYMBOL(usb_wwan_close);

/* Helper functions used by usb_wwan_setup_urbs */
static struct urb *usb_wwan_setup_urb(struct usb_serial_port *port,
				      int endpoint,
				      int dir, void *ctx, char *buf, int len,
				      void (*callback) (struct urb *))
{
	struct usb_serial *serial = port->serial;
	struct urb *urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);	/* No ISO */
	if (urb == NULL) {
		dev_dbg(&serial->interface->dev,
			"%s: alloc for endpoint %d failed.\n", __func__,
			endpoint);
		return NULL;
	}

	/* Fill URB using supplied data. */
	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev, endpoint) | dir,
			  buf, len, callback, ctx);

	return urb;
}

int usb_wwan_port_probe(struct usb_serial_port *port)
{
	struct usb_wwan_port_private *portdata;
	struct urb *urb;
	u8 *buffer;
	int i;

	if (!port->bulk_in_size || !port->bulk_out_size)
		return -ENODEV;

	portdata = kzalloc(sizeof(*portdata), GFP_KERNEL);
	if (!portdata)
		return -ENOMEM;

	init_usb_anchor(&portdata->delayed);

	for (i = 0; i < N_IN_URB; i++) {
		buffer = (u8 *)__get_free_page(GFP_KERNEL);
		if (!buffer)
			goto bail_out_error;
		portdata->in_buffer[i] = buffer;

		urb = usb_wwan_setup_urb(port, port->bulk_in_endpointAddress,
						USB_DIR_IN, port,
						buffer, IN_BUFLEN,
						usb_wwan_indat_callback);
		portdata->in_urbs[i] = urb;
	}

	for (i = 0; i < N_OUT_URB; i++) {
		buffer = kmalloc(OUT_BUFLEN, GFP_KERNEL);
		if (!buffer)
			goto bail_out_error2;
		portdata->out_buffer[i] = buffer;

		urb = usb_wwan_setup_urb(port, port->bulk_out_endpointAddress,
						USB_DIR_OUT, port,
						buffer, OUT_BUFLEN,
						usb_wwan_outdat_callback);
		portdata->out_urbs[i] = urb;
	}

	usb_set_serial_port_data(port, portdata);

	return 0;

bail_out_error2:
	for (i = 0; i < N_OUT_URB; i++) {
		usb_free_urb(portdata->out_urbs[i]);
		kfree(portdata->out_buffer[i]);
	}
bail_out_error:
	for (i = 0; i < N_IN_URB; i++) {
		usb_free_urb(portdata->in_urbs[i]);
		free_page((unsigned long)portdata->in_buffer[i]);
	}
	kfree(portdata);

	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(usb_wwan_port_probe);

int usb_wwan_port_remove(struct usb_serial_port *port)
{
	int i;
	struct usb_wwan_port_private *portdata;

	portdata = usb_get_serial_port_data(port);
	usb_set_serial_port_data(port, NULL);

	/* Stop reading/writing urbs and free them */
	for (i = 0; i < N_IN_URB; i++) {
		usb_kill_urb(portdata->in_urbs[i]);
		usb_free_urb(portdata->in_urbs[i]);
		free_page((unsigned long)portdata->in_buffer[i]);
	}
	for (i = 0; i < N_OUT_URB; i++) {
		usb_kill_urb(portdata->out_urbs[i]);
		usb_free_urb(portdata->out_urbs[i]);
		kfree(portdata->out_buffer[i]);
	}

	/* Now free port private data */
	kfree(portdata);
	return 0;
}
EXPORT_SYMBOL(usb_wwan_port_remove);

#ifdef CONFIG_PM
static void stop_read_write_urbs(struct usb_serial *serial)
{
	int i, j;
	struct usb_serial_port *port;
	struct usb_wwan_port_private *portdata;

	/* Stop reading/writing urbs */
	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);
		if (!portdata)
			continue;
		for (j = 0; j < N_IN_URB; j++)
			usb_kill_urb(portdata->in_urbs[j]);
		for (j = 0; j < N_OUT_URB; j++)
			usb_kill_urb(portdata->out_urbs[j]);
	}
}

int usb_wwan_suspend(struct usb_serial *serial, pm_message_t message)
{
	struct usb_wwan_intf_private *intfdata = serial->private;

	spin_lock_irq(&intfdata->susp_lock);
	if (PMSG_IS_AUTO(message)) {
		if (intfdata->in_flight) {
			spin_unlock_irq(&intfdata->susp_lock);
			return -EBUSY;
		}
	}
	intfdata->suspended = 1;
	spin_unlock_irq(&intfdata->susp_lock);

	stop_read_write_urbs(serial);

	return 0;
}
EXPORT_SYMBOL(usb_wwan_suspend);

static void play_delayed(struct usb_serial_port *port)
{
	struct usb_wwan_intf_private *data;
	struct usb_wwan_port_private *portdata;
	struct urb *urb;
	int err;

	portdata = usb_get_serial_port_data(port);
	data = port->serial->private;
	while ((urb = usb_get_from_anchor(&portdata->delayed))) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (!err) {
			data->in_flight++;
		} else {
			/* we have to throw away the rest */
			do {
				unbusy_queued_urb(urb, portdata);
				usb_autopm_put_interface_no_suspend(port->serial->interface);
			} while ((urb = usb_get_from_anchor(&portdata->delayed)));
			break;
		}
	}
}

int usb_wwan_resume(struct usb_serial *serial)
{
	int i, j;
	struct usb_serial_port *port;
	struct usb_wwan_intf_private *intfdata = serial->private;
	struct usb_wwan_port_private *portdata;
	struct urb *urb;
	int err = 0;

	spin_lock_irq(&intfdata->susp_lock);
	for (i = 0; i < serial->num_ports; i++) {
		/* walk all ports */
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);

		/* skip closed ports */
		if (!portdata || !portdata->opened)
			continue;

		if (port->interrupt_in_urb) {
			err = usb_submit_urb(port->interrupt_in_urb,
					GFP_ATOMIC);
			if (err) {
				dev_err(&port->dev,
					"%s: submit int urb failed: %d\n",
					__func__, err);
			}
		}

		for (j = 0; j < N_IN_URB; j++) {
			urb = portdata->in_urbs[j];
			err = usb_submit_urb(urb, GFP_ATOMIC);
			if (err < 0) {
				dev_err(&port->dev, "%s: Error %d for bulk URB %d\n",
					__func__, err, i);
				spin_unlock_irq(&intfdata->susp_lock);
				goto err_out;
			}
		}
		play_delayed(port);
	}
	intfdata->suspended = 0;
	spin_unlock_irq(&intfdata->susp_lock);
err_out:
	return err;
}
EXPORT_SYMBOL(usb_wwan_resume);
#endif

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
