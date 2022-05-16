/*
 * Intel Wireless WiMAX Connection 2400m
 * Firmware uploader's USB specifics
 *
 *
 * Copyright (C) 2007-2008 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * Intel Corporation <linux-wimax@intel.com>
 * Yanir Lubetkin <yanirx.lubetkin@intel.com>
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *  - Initial implementation
 *
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *  - bus generic/specific split
 *
 * THE PROCEDURE
 *
 * See fw.c for the generic description of this procedure.
 *
 * This file implements only the USB specifics. It boils down to how
 * to send a command and waiting for an acknowledgement from the
 * device.
 *
 * This code (and process) is single threaded. It assumes it is the
 * only thread poking around (guaranteed by fw.c).
 *
 * COMMAND EXECUTION
 *
 * A write URB is posted with the buffer to the bulk output endpoint.
 *
 * ACK RECEPTION
 *
 * We just post a URB to the notification endpoint and wait for
 * data. We repeat until we get all the data we expect (as indicated
 * by the call from the bus generic code).
 *
 * The data is not read from the bulk in endpoint for boot mode.
 *
 * ROADMAP
 *
 * i2400mu_bus_bm_cmd_send
 *   i2400m_bm_cmd_prepare...
 *   i2400mu_tx_bulk_out
 *
 * i2400mu_bus_bm_wait_for_ack
 *   i2400m_notif_submit
 */
#include <linux/usb.h>
#include <linux/gfp.h>
#include "i2400m-usb.h"


#define D_SUBMODULE fw
#include "usb-debug-levels.h"


/*
 * Synchronous write to the device
 *
 * Takes care of updating EDC counts and thus, handle device errors.
 */
static
ssize_t i2400mu_tx_bulk_out(struct i2400mu *i2400mu, void *buf, size_t buf_size)
{
	int result;
	struct device *dev = &i2400mu->usb_iface->dev;
	int len;
	struct usb_endpoint_descriptor *epd;
	int pipe, do_autopm = 1;

	result = usb_autopm_get_interface(i2400mu->usb_iface);
	if (result < 0) {
		dev_err(dev, "BM-CMD: can't get autopm: %d\n", result);
		do_autopm = 0;
	}
	epd = usb_get_epd(i2400mu->usb_iface, i2400mu->endpoint_cfg.bulk_out);
	pipe = usb_sndbulkpipe(i2400mu->usb_dev, epd->bEndpointAddress);
retry:
	result = usb_bulk_msg(i2400mu->usb_dev, pipe, buf, buf_size, &len, 200);
	switch (result) {
	case 0:
		if (len != buf_size) {
			dev_err(dev, "BM-CMD: short write (%u B vs %zu "
				"expected)\n", len, buf_size);
			result = -EIO;
			break;
		}
		result = len;
		break;
	case -EPIPE:
		/*
		 * Stall -- maybe the device is choking with our
		 * requests. Clear it and give it some time. If they
		 * happen to often, it might be another symptom, so we
		 * reset.
		 *
		 * No error handling for usb_clear_halt(0; if it
		 * works, the retry works; if it fails, this switch
		 * does the error handling for us.
		 */
		if (edc_inc(&i2400mu->urb_edc,
			    10 * EDC_MAX_ERRORS, EDC_ERROR_TIMEFRAME)) {
			dev_err(dev, "BM-CMD: too many stalls in "
				"URB; resetting device\n");
			usb_queue_reset_device(i2400mu->usb_iface);
		} else {
			usb_clear_halt(i2400mu->usb_dev, pipe);
			msleep(10);	/* give the device some time */
			goto retry;
		}
		fallthrough;
	case -EINVAL:			/* while removing driver */
	case -ENODEV:			/* dev disconnect ... */
	case -ENOENT:			/* just ignore it */
	case -ESHUTDOWN:		/* and exit */
	case -ECONNRESET:
		result = -ESHUTDOWN;
		break;
	case -ETIMEDOUT:			/* bah... */
		break;
	default:				/* any other? */
		if (edc_inc(&i2400mu->urb_edc,
			    EDC_MAX_ERRORS, EDC_ERROR_TIMEFRAME)) {
				dev_err(dev, "BM-CMD: maximum errors in "
					"URB exceeded; resetting device\n");
				usb_queue_reset_device(i2400mu->usb_iface);
				result = -ENODEV;
				break;
		}
		dev_err(dev, "BM-CMD: URB error %d, retrying\n",
			result);
		goto retry;
	}
	if (do_autopm)
		usb_autopm_put_interface(i2400mu->usb_iface);
	return result;
}


/*
 * Send a boot-mode command over the bulk-out pipe
 *
 * Command can be a raw command, which requires no preparation (and
 * which might not even be following the command format). Checks that
 * the right amount of data was transferred.
 *
 * To satisfy USB requirements (no onstack, vmalloc or in data segment
 * buffers), we copy the command to i2400m->bm_cmd_buf and send it from
 * there.
 *
 * @flags: pass thru from i2400m_bm_cmd()
 * @return: cmd_size if ok, < 0 errno code on error.
 */
ssize_t i2400mu_bus_bm_cmd_send(struct i2400m *i2400m,
				const struct i2400m_bootrom_header *_cmd,
				size_t cmd_size, int flags)
{
	ssize_t result;
	struct device *dev = i2400m_dev(i2400m);
	struct i2400mu *i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	int opcode = _cmd == NULL ? -1 : i2400m_brh_get_opcode(_cmd);
	struct i2400m_bootrom_header *cmd;
	size_t cmd_size_a = ALIGN(cmd_size, 16);	/* USB restriction */

	d_fnstart(8, dev, "(i2400m %p cmd %p size %zu)\n",
		  i2400m, _cmd, cmd_size);
	result = -E2BIG;
	if (cmd_size > I2400M_BM_CMD_BUF_SIZE)
		goto error_too_big;
	if (_cmd != i2400m->bm_cmd_buf)
		memmove(i2400m->bm_cmd_buf, _cmd, cmd_size);
	cmd = i2400m->bm_cmd_buf;
	if (cmd_size_a > cmd_size)			/* Zero pad space */
		memset(i2400m->bm_cmd_buf + cmd_size, 0, cmd_size_a - cmd_size);
	if ((flags & I2400M_BM_CMD_RAW) == 0) {
		if (WARN_ON(i2400m_brh_get_response_required(cmd) == 0))
			dev_warn(dev, "SW BUG: response_required == 0\n");
		i2400m_bm_cmd_prepare(cmd);
	}
	result = i2400mu_tx_bulk_out(i2400mu, i2400m->bm_cmd_buf, cmd_size);
	if (result < 0) {
		dev_err(dev, "boot-mode cmd %d: cannot send: %zd\n",
			opcode, result);
		goto error_cmd_send;
	}
	if (result != cmd_size) {		/* all was transferred? */
		dev_err(dev, "boot-mode cmd %d: incomplete transfer "
			"(%zd vs %zu submitted)\n",  opcode, result, cmd_size);
		result = -EIO;
		goto error_cmd_size;
	}
error_cmd_size:
error_cmd_send:
error_too_big:
	d_fnend(8, dev, "(i2400m %p cmd %p size %zu) = %zd\n",
		i2400m, _cmd, cmd_size, result);
	return result;
}


static
void __i2400mu_bm_notif_cb(struct urb *urb)
{
	complete(urb->context);
}


/*
 * submit a read to the notification endpoint
 *
 * @i2400m: device descriptor
 * @urb: urb to use
 * @completion: completion variable to complete when done
 *
 * Data is always read to i2400m->bm_ack_buf
 */
static
int i2400mu_notif_submit(struct i2400mu *i2400mu, struct urb *urb,
			 struct completion *completion)
{
	struct i2400m *i2400m = &i2400mu->i2400m;
	struct usb_endpoint_descriptor *epd;
	int pipe;

	epd = usb_get_epd(i2400mu->usb_iface,
			  i2400mu->endpoint_cfg.notification);
	pipe = usb_rcvintpipe(i2400mu->usb_dev, epd->bEndpointAddress);
	usb_fill_int_urb(urb, i2400mu->usb_dev, pipe,
			 i2400m->bm_ack_buf, I2400M_BM_ACK_BUF_SIZE,
			 __i2400mu_bm_notif_cb, completion,
			 epd->bInterval);
	return usb_submit_urb(urb, GFP_KERNEL);
}


/*
 * Read an ack from  the notification endpoint
 *
 * @i2400m:
 * @_ack: pointer to where to store the read data
 * @ack_size: how many bytes we should read
 *
 * Returns: < 0 errno code on error; otherwise, amount of received bytes.
 *
 * Submits a notification read, appends the read data to the given ack
 * buffer and then repeats (until @ack_size bytes have been
 * received).
 */
ssize_t i2400mu_bus_bm_wait_for_ack(struct i2400m *i2400m,
				    struct i2400m_bootrom_header *_ack,
				    size_t ack_size)
{
	ssize_t result = -ENOMEM;
	struct device *dev = i2400m_dev(i2400m);
	struct i2400mu *i2400mu = container_of(i2400m, struct i2400mu, i2400m);
	struct urb notif_urb;
	void *ack = _ack;
	size_t offset, len;
	long val;
	int do_autopm = 1;
	DECLARE_COMPLETION_ONSTACK(notif_completion);

	d_fnstart(8, dev, "(i2400m %p ack %p size %zu)\n",
		  i2400m, ack, ack_size);
	BUG_ON(_ack == i2400m->bm_ack_buf);
	result = usb_autopm_get_interface(i2400mu->usb_iface);
	if (result < 0) {
		dev_err(dev, "BM-ACK: can't get autopm: %d\n", (int) result);
		do_autopm = 0;
	}
	usb_init_urb(&notif_urb);	/* ready notifications */
	usb_get_urb(&notif_urb);
	offset = 0;
	while (offset < ack_size) {
		init_completion(&notif_completion);
		result = i2400mu_notif_submit(i2400mu, &notif_urb,
					      &notif_completion);
		if (result < 0)
			goto error_notif_urb_submit;
		val = wait_for_completion_interruptible_timeout(
			&notif_completion, HZ);
		if (val == 0) {
			result = -ETIMEDOUT;
			usb_kill_urb(&notif_urb);	/* Timedout */
			goto error_notif_wait;
		}
		if (val == -ERESTARTSYS) {
			result = -EINTR;		/* Interrupted */
			usb_kill_urb(&notif_urb);
			goto error_notif_wait;
		}
		result = notif_urb.status;		/* How was the ack? */
		switch (result) {
		case 0:
			break;
		case -EINVAL:			/* while removing driver */
		case -ENODEV:			/* dev disconnect ... */
		case -ENOENT:			/* just ignore it */
		case -ESHUTDOWN:		/* and exit */
		case -ECONNRESET:
			result = -ESHUTDOWN;
			goto error_dev_gone;
		default:				/* any other? */
			usb_kill_urb(&notif_urb);	/* Timedout */
			if (edc_inc(&i2400mu->urb_edc,
				    EDC_MAX_ERRORS, EDC_ERROR_TIMEFRAME))
				goto error_exceeded;
			dev_err(dev, "BM-ACK: URB error %d, "
				"retrying\n", notif_urb.status);
			continue;	/* retry */
		}
		if (notif_urb.actual_length == 0) {
			d_printf(6, dev, "ZLP received, retrying\n");
			continue;
		}
		/* Got data, append it to the buffer */
		len = min(ack_size - offset, (size_t) notif_urb.actual_length);
		memcpy(ack + offset, i2400m->bm_ack_buf, len);
		offset += len;
	}
	result = offset;
error_notif_urb_submit:
error_notif_wait:
error_dev_gone:
out:
	if (do_autopm)
		usb_autopm_put_interface(i2400mu->usb_iface);
	d_fnend(8, dev, "(i2400m %p ack %p size %zu) = %ld\n",
		i2400m, ack, ack_size, (long) result);
	usb_put_urb(&notif_urb);
	return result;

error_exceeded:
	dev_err(dev, "bm: maximum errors in notification URB exceeded; "
		"resetting device\n");
	usb_queue_reset_device(i2400mu->usb_iface);
	goto out;
}
