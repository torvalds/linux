// SPDX-License-Identifier: GPL-2.0
/*
  USB Driver layer for GSM modems

  Copyright (C) 2005  Matthias Urlichs <smurf@smurf.noris.de>

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

/*
 * Generate DTR/RTS signals on the port using the SET_CONTROL_LINE_STATE request
 * in CDC ACM.
 */
static int usb_wwan_send_setup(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct usb_wwan_port_private *portdata;
	int val = 0;
	int ifnum;
	int res;

	portdata = usb_get_serial_port_data(port);

	if (portdata->dtr_state)
		val |= 0x01;
	if (portdata->rts_state)
		val |= 0x02;

	ifnum = serial->interface->cur_altsetting->desc.bInterfaceNumber;

	res = usb_autopm_get_interface(serial->interface);
	if (res)
		return res;

	res = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				0x22, 0x21, val, ifnum, NULL, 0,
				USB_CTRL_SET_TIMEOUT);

	usb_autopm_put_interface(port->serial->interface);

	return res;
}

void usb_wwan_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_wwan_port_private *portdata;
	struct usb_wwan_intf_private *intfdata;

	intfdata = usb_get_serial_data(port->serial);

	if (!intfdata->use_send_setup)
		return;

	portdata = usb_get_serial_port_data(port);
	/* FIXME: locking */
	portdata->rts_state = on;
	portdata->dtr_state = on;

	usb_wwan_send_setup(port);
}
EXPORT_SYMBOL(usb_wwan_dtr_rts);

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
	intfdata = usb_get_serial_data(port->serial);

	if (!intfdata->use_send_setup)
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
	return usb_wwan_send_setup(port);
}
EXPORT_SYMBOL(usb_wwan_tiocmset);

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
	intfdata = usb_get_serial_data(port->serial);

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
				dev_err(&port->dev,
					"%s: submit urb %d failed: %d\n",
					__func__, i, err);
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

		/* don't resubmit on fatal errors */
		if (status == -ESHUTDOWN || status == -ENOENT)
			return;
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
		if (err != -EPERM && err != -ENODEV) {
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
	unsigned long flags;
	int i;

	port = urb->context;
	intfdata = usb_get_serial_data(port->serial);

	usb_serial_port_softint(port);
	usb_autopm_put_interface_async(port->serial->interface);
	portdata = usb_get_serial_port_data(port);
	spin_lock_irqsave(&intfdata->susp_lock, flags);
	intfdata->in_flight--;
	spin_unlock_irqrestore(&intfdata->susp_lock, flags);

	for (i = 0; i < N_OUT_URB; ++i) {
		if (portdata->out_urbs[i] == urb) {
			smp_mb__before_atomic();
			clear_bit(i, &portdata->out_busy);
			break;
		}
	}
}

unsigned int usb_wwan_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_wwan_port_private *portdata;
	int i;
	unsigned int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i = 0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		if (this_urb && !test_bit(i, &portdata->out_busy))
			data_len += OUT_BUFLEN;
	}

	dev_dbg(&port->dev, "%s: %u\n", __func__, data_len);
	return data_len;
}
EXPORT_SYMBOL(usb_wwan_write_room);

unsigned int usb_wwan_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_wwan_port_private *portdata;
	int i;
	unsigned int data_len = 0;
	struct urb *this_urb;

	portdata = usb_get_serial_port_data(port);

	for (i = 0; i < N_OUT_URB; i++) {
		this_urb = portdata->out_urbs[i];
		/* FIXME: This locking is insufficient as this_urb may
		   go unused during the test */
		if (this_urb && test_bit(i, &portdata->out_busy))
			data_len += this_urb->transfer_buffer_length;
	}
	dev_dbg(&port->dev, "%s: %u\n", __func__, data_len);
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
	intfdata = usb_get_serial_data(serial);

	if (port->interrupt_in_urb) {
		err = usb_submit_urb(port->interrupt_in_urb, GFP_KERNEL);
		if (err) {
			dev_err(&port->dev, "%s: submit int urb failed: %d\n",
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
			dev_err(&port->dev,
				"%s: submit read urb %d failed: %d\n",
				__func__, i, err);
		}
	}

	spin_lock_irq(&intfdata->susp_lock);
	if (++intfdata->open_ports == 1)
		serial->interface->needs_remote_wakeup = 1;
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
	struct usb_wwan_intf_private *intfdata = usb_get_serial_data(serial);
	struct urb *urb;

	portdata = usb_get_serial_port_data(port);

	/*
	 * Need to take susp_lock to make sure port is not already being
	 * resumed, but no need to hold it due to initialized
	 */
	spin_lock_irq(&intfdata->susp_lock);
	if (--intfdata->open_ports == 0)
		serial->interface->needs_remote_wakeup = 0;
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

	usb_autopm_get_interface_no_resume(serial->interface);
}
EXPORT_SYMBOL(usb_wwan_close);

static struct urb *usb_wwan_setup_urb(struct usb_serial_port *port,
				      int endpoint,
				      int dir, void *ctx, char *buf, int len,
				      void (*callback) (struct urb *))
{
	struct usb_serial *serial = port->serial;
	struct usb_wwan_intf_private *intfdata = usb_get_serial_data(serial);
	struct urb *urb;

	urb = usb_alloc_urb(0, GFP_KERNEL);	/* No ISO */
	if (!urb)
		return NULL;

	usb_fill_bulk_urb(urb, serial->dev,
			  usb_sndbulkpipe(serial->dev, endpoint) | dir,
			  buf, len, callback, ctx);

	if (intfdata->use_zlp && dir == USB_DIR_OUT)
		urb->transfer_flags |= URB_ZERO_PACKET;

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

void usb_wwan_port_remove(struct usb_serial_port *port)
{
	int i;
	struct usb_wwan_port_private *portdata;

	portdata = usb_get_serial_port_data(port);
	usb_set_serial_port_data(port, NULL);

	for (i = 0; i < N_IN_URB; i++) {
		usb_free_urb(portdata->in_urbs[i]);
		free_page((unsigned long)portdata->in_buffer[i]);
	}
	for (i = 0; i < N_OUT_URB; i++) {
		usb_free_urb(portdata->out_urbs[i]);
		kfree(portdata->out_buffer[i]);
	}

	kfree(portdata);
}
EXPORT_SYMBOL(usb_wwan_port_remove);

#ifdef CONFIG_PM
static void stop_urbs(struct usb_serial *serial)
{
	int i, j;
	struct usb_serial_port *port;
	struct usb_wwan_port_private *portdata;

	for (i = 0; i < serial->num_ports; ++i) {
		port = serial->port[i];
		portdata = usb_get_serial_port_data(port);
		if (!portdata)
			continue;
		for (j = 0; j < N_IN_URB; j++)
			usb_kill_urb(portdata->in_urbs[j]);
		for (j = 0; j < N_OUT_URB; j++)
			usb_kill_urb(portdata->out_urbs[j]);
		usb_kill_urb(port->interrupt_in_urb);
	}
}

int usb_wwan_suspend(struct usb_serial *serial, pm_message_t message)
{
	struct usb_wwan_intf_private *intfdata = usb_get_serial_data(serial);

	spin_lock_irq(&intfdata->susp_lock);
	if (PMSG_IS_AUTO(message)) {
		if (intfdata->in_flight) {
			spin_unlock_irq(&intfdata->susp_lock);
			return -EBUSY;
		}
	}
	intfdata->suspended = 1;
	spin_unlock_irq(&intfdata->susp_lock);

	stop_urbs(serial);

	return 0;
}
EXPORT_SYMBOL(usb_wwan_suspend);

/* Caller must hold susp_lock. */
static int usb_wwan_submit_delayed_urbs(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct usb_wwan_intf_private *data = usb_get_serial_data(serial);
	struct usb_wwan_port_private *portdata;
	struct urb *urb;
	int err_count = 0;
	int err;

	portdata = usb_get_serial_port_data(port);

	for (;;) {
		urb = usb_get_from_anchor(&portdata->delayed);
		if (!urb)
			break;

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err) {
			dev_err(&port->dev, "%s: submit urb failed: %d\n",
					__func__, err);
			err_count++;
			unbusy_queued_urb(urb, portdata);
			usb_autopm_put_interface_async(serial->interface);
			continue;
		}
		data->in_flight++;
	}

	if (err_count)
		return -EIO;

	return 0;
}

int usb_wwan_resume(struct usb_serial *serial)
{
	int i, j;
	struct usb_serial_port *port;
	struct usb_wwan_intf_private *intfdata = usb_get_serial_data(serial);
	struct usb_wwan_port_private *portdata;
	struct urb *urb;
	int err;
	int err_count = 0;

	spin_lock_irq(&intfdata->susp_lock);
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];

		if (!tty_port_initialized(&port->port))
			continue;

		portdata = usb_get_serial_port_data(port);

		if (port->interrupt_in_urb) {
			err = usb_submit_urb(port->interrupt_in_urb,
					GFP_ATOMIC);
			if (err) {
				dev_err(&port->dev,
					"%s: submit int urb failed: %d\n",
					__func__, err);
				err_count++;
			}
		}

		err = usb_wwan_submit_delayed_urbs(port);
		if (err)
			err_count++;

		for (j = 0; j < N_IN_URB; j++) {
			urb = portdata->in_urbs[j];
			err = usb_submit_urb(urb, GFP_ATOMIC);
			if (err < 0) {
				dev_err(&port->dev,
					"%s: submit read urb %d failed: %d\n",
					__func__, i, err);
				err_count++;
			}
		}
	}
	intfdata->suspended = 0;
	spin_unlock_irq(&intfdata->susp_lock);

	if (err_count)
		return -EIO;

	return 0;
}
EXPORT_SYMBOL(usb_wwan_resume);
#endif

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL v2");
