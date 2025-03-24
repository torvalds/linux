// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 * driver for National Instruments usb to gpib adapters
 *    copyright		   : (C) 2004 by Frank Mori Hess
 ***************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "ni_usb_gpib.h"
#include "gpibP.h"
#include "nec7210.h"
#include "tnt4882_registers.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for National Instruments USB devices");

#define MAX_NUM_NI_USB_INTERFACES 128
static struct usb_interface *ni_usb_driver_interfaces[MAX_NUM_NI_USB_INTERFACES];

static int ni_usb_parse_status_block(const u8 *buffer, struct ni_usb_status_block *status);
static int ni_usb_set_interrupt_monitor(gpib_board_t *board, unsigned int monitored_bits);
static void ni_usb_stop(struct ni_usb_priv *ni_priv);

static DEFINE_MUTEX(ni_usb_hotplug_lock);

//calculates a reasonable timeout in that can be passed to usb functions
static inline unsigned long ni_usb_timeout_msecs(unsigned int usec)
{
	if (usec == 0)
		return 0;
	return 2000 + usec / 500;
};

// returns timeout code byte for use in ni-usb-b instructions
static unsigned short ni_usb_timeout_code(unsigned int usec)
{
	if (usec == 0)
		return 0xf0;
	else if (usec <= 10)
		return 0xf1;
	else if (usec <= 30)
		return 0xf2;
	else if (usec <= 100)
		return 0xf3;
	else if (usec <= 300)
		return 0xf4;
	else if (usec <= 1000)
		return 0xf5;
	else if (usec <= 3000)
		return 0xf6;
	else if (usec <= 10000)
		return 0xf7;
	else if (usec <= 30000)
		return 0xf8;
	else if (usec <= 100000)
		return 0xf9;
	else if (usec <= 300000)
		return 0xfa;
	else if (usec <= 1000000)
		return 0xfb;
	else if (usec <= 3000000)
		return 0xfc;
	else if (usec <= 10000000)
		return 0xfd;
	else if (usec <= 30000000)
		return 0xfe;
	else if (usec <= 100000000)
		return 0xff;
	else if	 (usec <= 300000000)
		return 0x01;
	/* NI driver actually uses 0xff for timeout T1000s, which is a bug in their code.
	 * I've verified on a usb-b that a code of 0x2 is correct for a 1000 sec timeout
	 */
	else if (usec <= 1000000000)
		return 0x02;
	pr_err("%s: bug? usec is greater than 1e9\n", __func__);
	return 0xf0;
}

static void ni_usb_bulk_complete(struct urb *urb)
{
	struct ni_usb_urb_ctx *context = urb->context;

//	printk("debug: %s: status=0x%x, error_count=%i, actual_length=%i\n",  __func__,
//		urb->status, urb->error_count, urb->actual_length);
	complete(&context->complete);
}

static void ni_usb_timeout_handler(struct timer_list *t)
{
	struct ni_usb_priv *ni_priv = from_timer(ni_priv, t, bulk_timer);
	struct ni_usb_urb_ctx *context = &ni_priv->context;

	context->timed_out = 1;
	complete(&context->complete);
};

// I'm using nonblocking loosely here, it only means -EAGAIN can be returned in certain cases
static int ni_usb_nonblocking_send_bulk_msg(struct ni_usb_priv *ni_priv, void *data,
					    int data_length, int *actual_data_length,
					    int timeout_msecs)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int out_pipe;
	struct ni_usb_urb_ctx *context = &ni_priv->context;

	*actual_data_length = 0;
	mutex_lock(&ni_priv->bulk_transfer_lock);
	if (!ni_priv->bus_interface) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -ENODEV;
	}
	if (ni_priv->bulk_urb) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -EAGAIN;
	}
	ni_priv->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ni_priv->bulk_urb) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -ENOMEM;
	}
	usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	out_pipe = usb_sndbulkpipe(usb_dev, ni_priv->bulk_out_endpoint);
	init_completion(&context->complete);
	context->timed_out = 0;
	usb_fill_bulk_urb(ni_priv->bulk_urb, usb_dev, out_pipe, data, data_length,
			  &ni_usb_bulk_complete, context);

	if (timeout_msecs)
		mod_timer(&ni_priv->bulk_timer, jiffies + msecs_to_jiffies(timeout_msecs));

	retval = usb_submit_urb(ni_priv->bulk_urb, GFP_KERNEL);
	if (retval) {
		del_timer_sync(&ni_priv->bulk_timer);
		usb_free_urb(ni_priv->bulk_urb);
		ni_priv->bulk_urb = NULL;
		dev_err(&usb_dev->dev, "%s: failed to submit bulk out urb, retval=%i\n",
			__func__, retval);
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return retval;
	}
	mutex_unlock(&ni_priv->bulk_transfer_lock);
	wait_for_completion(&context->complete);    // wait for ni_usb_bulk_complete
	if (context->timed_out) {
		usb_kill_urb(ni_priv->bulk_urb);
		dev_err(&usb_dev->dev, "%s: killed urb due to timeout\n", __func__);
		retval = -ETIMEDOUT;
	} else {
		retval = ni_priv->bulk_urb->status;
	}

	del_timer_sync(&ni_priv->bulk_timer);
	*actual_data_length = ni_priv->bulk_urb->actual_length;
	mutex_lock(&ni_priv->bulk_transfer_lock);
	usb_free_urb(ni_priv->bulk_urb);
	ni_priv->bulk_urb = NULL;
	mutex_unlock(&ni_priv->bulk_transfer_lock);
	return retval;
}

static int ni_usb_send_bulk_msg(struct ni_usb_priv *ni_priv, void *data, int data_length,
				int *actual_data_length, int timeout_msecs)
{
	int retval;
	int timeout_msecs_remaining = timeout_msecs;

	retval = ni_usb_nonblocking_send_bulk_msg(ni_priv, data, data_length, actual_data_length,
						  timeout_msecs_remaining);
	while (retval == -EAGAIN && (timeout_msecs == 0 || timeout_msecs_remaining > 0)) {
		usleep_range(1000, 1500);
		retval = ni_usb_nonblocking_send_bulk_msg(ni_priv, data, data_length,
							  actual_data_length,
							  timeout_msecs_remaining);
		if (timeout_msecs != 0)
			--timeout_msecs_remaining;
	}
	if (timeout_msecs != 0 && timeout_msecs_remaining <= 0)
		return -ETIMEDOUT;
	return retval;
}

// I'm using nonblocking loosely here, it only means -EAGAIN can be returned in certain cases
static int ni_usb_nonblocking_receive_bulk_msg(struct ni_usb_priv *ni_priv,
					       void *data, int data_length,
					       int *actual_data_length, int timeout_msecs,
					       int interruptible)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int in_pipe;
	struct ni_usb_urb_ctx *context = &ni_priv->context;

	*actual_data_length = 0;
	mutex_lock(&ni_priv->bulk_transfer_lock);
	if (!ni_priv->bus_interface) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -ENODEV;
	}
	if (ni_priv->bulk_urb) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -EAGAIN;
	}
	ni_priv->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ni_priv->bulk_urb) {
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return -ENOMEM;
	}
	usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	in_pipe = usb_rcvbulkpipe(usb_dev, ni_priv->bulk_in_endpoint);
	init_completion(&context->complete);
	context->timed_out = 0;
	usb_fill_bulk_urb(ni_priv->bulk_urb, usb_dev, in_pipe, data, data_length,
			  &ni_usb_bulk_complete, context);

	if (timeout_msecs)
		mod_timer(&ni_priv->bulk_timer, jiffies + msecs_to_jiffies(timeout_msecs));

	//printk("%s: submitting urb\n", __func__);
	retval = usb_submit_urb(ni_priv->bulk_urb, GFP_KERNEL);
	if (retval) {
		del_timer_sync(&ni_priv->bulk_timer);
		usb_free_urb(ni_priv->bulk_urb);
		ni_priv->bulk_urb = NULL;
		dev_err(&usb_dev->dev, "%s: failed to submit bulk out urb, retval=%i\n",
			__func__, retval);
		mutex_unlock(&ni_priv->bulk_transfer_lock);
		return retval;
	}
	mutex_unlock(&ni_priv->bulk_transfer_lock);
	if (interruptible) {
		if (wait_for_completion_interruptible(&context->complete)) {
			/* If we got interrupted by a signal while
			 * waiting for the usb gpib to respond, we
			 * should send a stop command so it will
			 * finish up with whatever it was doing and
			 * send its response now.
			 */
			ni_usb_stop(ni_priv);
			retval = -ERESTARTSYS;
			/* now do an uninterruptible wait, it shouldn't take long
			 *	for the board to respond now.
			 */
			wait_for_completion(&context->complete);
		}
	} else {
		wait_for_completion(&context->complete);
	}
	if (context->timed_out) {
		usb_kill_urb(ni_priv->bulk_urb);
		dev_err(&usb_dev->dev, "%s: killed urb due to timeout\n", __func__);
		retval = -ETIMEDOUT;
	} else {
		if (ni_priv->bulk_urb->status)
			retval = ni_priv->bulk_urb->status;
	}
	del_timer_sync(&ni_priv->bulk_timer);
	*actual_data_length = ni_priv->bulk_urb->actual_length;
	mutex_lock(&ni_priv->bulk_transfer_lock);
	usb_free_urb(ni_priv->bulk_urb);
	ni_priv->bulk_urb = NULL;
	mutex_unlock(&ni_priv->bulk_transfer_lock);
	return retval;
}

static int ni_usb_receive_bulk_msg(struct ni_usb_priv *ni_priv, void *data,
				   int data_length, int *actual_data_length, int timeout_msecs,
				   int interruptible)
{
	int retval;
	int timeout_msecs_remaining = timeout_msecs;

	retval = ni_usb_nonblocking_receive_bulk_msg(ni_priv, data, data_length,
						     actual_data_length, timeout_msecs_remaining,
						     interruptible);
	while (retval == -EAGAIN && (timeout_msecs == 0 || timeout_msecs_remaining > 0)) {
		usleep_range(1000, 1500);
		retval = ni_usb_nonblocking_receive_bulk_msg(ni_priv, data, data_length,
							     actual_data_length,
							     timeout_msecs_remaining,
							     interruptible);
		if (timeout_msecs != 0)
			--timeout_msecs_remaining;
	}
	if (timeout_msecs && timeout_msecs_remaining <= 0)
		return -ETIMEDOUT;
	return retval;
}

static int ni_usb_receive_control_msg(struct ni_usb_priv *ni_priv, __u8 request,
				      __u8 requesttype, __u16 value, __u16 index,
				      void *data, __u16 size, int timeout_msecs)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int in_pipe;

	mutex_lock(&ni_priv->control_transfer_lock);
	if (!ni_priv->bus_interface) {
		mutex_unlock(&ni_priv->control_transfer_lock);
		return -ENODEV;
	}
	usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	in_pipe = usb_rcvctrlpipe(usb_dev, 0);
	retval = usb_control_msg(usb_dev, in_pipe, request, requesttype, value, index, data,
				 size, timeout_msecs);
	mutex_unlock(&ni_priv->control_transfer_lock);
	return retval;
}

static void ni_usb_soft_update_status(gpib_board_t *board, unsigned int ni_usb_ibsta,
				      unsigned int clear_mask)
{
	static const unsigned int ni_usb_ibsta_mask = SRQI | ATN | CIC | REM | LACS | TACS | LOK;

	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	unsigned int need_monitoring_bits = ni_usb_ibsta_monitor_mask;
	unsigned long flags;

	board->status &= ~clear_mask;
	board->status &= ~ni_usb_ibsta_mask;
	board->status |= ni_usb_ibsta & ni_usb_ibsta_mask;
	//FIXME should generate events on DTAS and DCAS

	spin_lock_irqsave(&board->spinlock, flags);
/* remove set status bits from monitored set why ?***/
	ni_priv->monitored_ibsta_bits &= ~ni_usb_ibsta;
	need_monitoring_bits &= ~ni_priv->monitored_ibsta_bits; /* mm - monitored set */
	spin_unlock_irqrestore(&board->spinlock, flags);
	dev_dbg(&usb_dev->dev, "%s: need_monitoring_bits=0x%x\n", __func__, need_monitoring_bits);

	if (need_monitoring_bits & ~ni_usb_ibsta)
		ni_usb_set_interrupt_monitor(board, ni_usb_ibsta_monitor_mask);
	else if (need_monitoring_bits & ni_usb_ibsta)
		wake_up_interruptible(&board->wait);

	dev_dbg(&usb_dev->dev, "%s: ni_usb_ibsta=0x%x\n", __func__, ni_usb_ibsta);
}

static int ni_usb_parse_status_block(const u8 *buffer, struct ni_usb_status_block *status)
{
	u16 count;

	status->id = buffer[0];
	status->ibsta = (buffer[1] << 8) | buffer[2];
	status->error_code = buffer[3];
	count = buffer[4] | (buffer[5] << 8);
	count = ~count;
	count++;
	status->count = count;
	return 8;
};

static void ni_usb_dump_raw_block(const u8 *raw_data, int length)
{
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 8, 1, raw_data, length, true);
}

static int ni_usb_parse_register_read_block(const u8 *raw_data, unsigned int *results,
					    int num_results)
{
	int i = 0;
	int j;
	int unexpected = 0;
	static const int results_per_chunk = 3;

	for (j = 0; j < num_results;) {
		int k;

		if (raw_data[i++] != NIUSB_REGISTER_READ_DATA_START_ID) {
			pr_err("%s: parse error: wrong start id\n", __func__);
			unexpected = 1;
		}
		for (k = 0; k < results_per_chunk && j < num_results; ++k)
			results[j++] = raw_data[i++];
	}
	while (i % 4)
		i++;
	if (raw_data[i++] != NIUSB_REGISTER_READ_DATA_END_ID) {
		pr_err("%s: parse error: wrong end id\n", __func__);
		unexpected = 1;
	}
	if (raw_data[i++] % results_per_chunk != num_results % results_per_chunk) {
		pr_err("%s: parse error: wrong count=%i for NIUSB_REGISTER_READ_DATA_END\n",
		       __func__, (int)raw_data[i - 1]);
		unexpected = 1;
	}
	while (i % 4) {
		if (raw_data[i++] != 0) {
			pr_err("%s: unexpected data: raw_data[%i]=0x%x, expected 0\n",
			       __func__, i - 1, (int)raw_data[i - 1]);
			unexpected = 1;
		}
	}
	if (unexpected)
		ni_usb_dump_raw_block(raw_data, i);
	return i;
}

static int ni_usb_parse_termination_block(const u8 *buffer)
{
	int i = 0;

	if (buffer[i++] != NIUSB_TERM_ID ||
	    buffer[i++] != 0x0 ||
	    buffer[i++] != 0x0 ||
	    buffer[i++] != 0x0) {
		pr_err("%s: received unexpected termination block\n", __func__);
		pr_err(" expected: 0x%x 0x%x 0x%x 0x%x\n",
		       NIUSB_TERM_ID, 0x0, 0x0, 0x0);
		pr_err(" received: 0x%x 0x%x 0x%x 0x%x\n",
		       buffer[i - 4], buffer[i - 3], buffer[i - 2], buffer[i - 1]);
	}
	return i;
};

static int parse_board_ibrd_readback(const u8 *raw_data, struct ni_usb_status_block *status,
				     u8 *parsed_data, int parsed_data_length,
				     int *actual_bytes_read)
{
	static const int ibrd_data_block_length = 0xf;
	static const int ibrd_extended_data_block_length = 0x1e;
	int data_block_length = 0;
	int i = 0;
	int j = 0;
	int k;
	unsigned int adr1_bits;
	int num_data_blocks = 0;
	struct ni_usb_status_block register_write_status;
	int unexpected = 0;

	while (raw_data[i] == NIUSB_IBRD_DATA_ID || raw_data[i] == NIUSB_IBRD_EXTENDED_DATA_ID) {
		if (raw_data[i] == NIUSB_IBRD_DATA_ID) {
			data_block_length = ibrd_data_block_length;
		} else if (raw_data[i] == NIUSB_IBRD_EXTENDED_DATA_ID) {
			data_block_length = ibrd_extended_data_block_length;
			if (raw_data[++i] !=  0)	{
				pr_err("%s: unexpected data: raw_data[%i]=0x%x, expected 0\n",
				       __func__, i, (int)raw_data[i]);
				unexpected = 1;
			}
		} else {
			pr_err("%s: logic bug!\n", __func__);
			return -EINVAL;
		}
		++i;
		for (k = 0; k < data_block_length; k++) {
			if (j < parsed_data_length)
				parsed_data[j++] = raw_data[i++];
			else
				++i;
		}
		++num_data_blocks;
	}
	i += ni_usb_parse_status_block(&raw_data[i], status);
	if (status->id != NIUSB_IBRD_STATUS_ID) {
		pr_err("%s: bug: status->id=%i, != ibrd_status_id\n", __func__, status->id);
		return -EIO;
	}
	adr1_bits = raw_data[i++];
	if (num_data_blocks) {
		*actual_bytes_read = (num_data_blocks - 1) * data_block_length + raw_data[i++];
	} else {
		++i;
		*actual_bytes_read = 0;
	}
	if (*actual_bytes_read > j)
		pr_err("%s: bug: discarded data. actual_bytes_read=%i, j=%i\n",
		       __func__, *actual_bytes_read, j);
	for (k = 0; k < 2; k++)
		if (raw_data[i++] != 0) {
			pr_err("%s: unexpected data: raw_data[%i]=0x%x, expected 0\n",
			       __func__, i - 1, (int)raw_data[i - 1]);
			unexpected = 1;
		}
	i += ni_usb_parse_status_block(&raw_data[i], &register_write_status);
	if (register_write_status.id != NIUSB_REG_WRITE_ID) {
		pr_err("%s: unexpected data: register write status id=0x%x, expected 0x%x\n",
		       __func__, register_write_status.id, NIUSB_REG_WRITE_ID);
		unexpected = 1;
	}
	if (raw_data[i++] != 2) {
		pr_err("%s: unexpected data: register write count=%i, expected 2\n",
		       __func__, (int)raw_data[i - 1]);
		unexpected = 1;
	}
	for (k = 0; k < 3; k++)
		if (raw_data[i++] != 0) {
			pr_err("%s: unexpected data: raw_data[%i]=0x%x, expected 0\n",
			       __func__, i - 1, (int)raw_data[i - 1]);
			unexpected = 1;
		}
	i += ni_usb_parse_termination_block(&raw_data[i]);
	if (unexpected)
		ni_usb_dump_raw_block(raw_data, i);
	return i;
}

static	int ni_usb_parse_reg_write_status_block(const u8 *raw_data,
						struct ni_usb_status_block *status,
						int *writes_completed)
{
	int i = 0;

	i += ni_usb_parse_status_block(raw_data, status);
	*writes_completed = raw_data[i++];
	while (i % 4)
		i++;
	return i;
}

static int ni_usb_write_registers(struct ni_usb_priv *ni_priv,
				  const struct ni_usb_register *writes, int num_writes,
				  unsigned int *ibsta)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	u8 *out_data, *in_data;
	int out_data_length;
	static const int in_data_length = 0x20;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	int j;
	struct ni_usb_status_block status;
	static const int bytes_per_write = 3;
	int reg_writes_completed;

	out_data_length = num_writes * bytes_per_write + 0x10;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)	{
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}
	i += ni_usb_bulk_register_write_header(&out_data[i], num_writes);
	for (j = 0; j < num_writes; j++)
		i += ni_usb_bulk_register_write(&out_data[i], writes[j]);
	while (i % 4)
		out_data[i++] = 0x00;
	i += ni_usb_bulk_termination(&out_data[i]);
	if (i > out_data_length)
		dev_err(&usb_dev->dev, "%s: bug! buffer overrun\n", __func__);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read, 1000, 0);
	if (retval || bytes_read != 16) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		ni_usb_dump_raw_block(in_data, bytes_read);
		kfree(in_data);
		return retval;
	}

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	ni_usb_parse_reg_write_status_block(in_data, &status, &reg_writes_completed);
	//FIXME parse extra 09 status bits and termination
	kfree(in_data);
	if (status.id != NIUSB_REG_WRITE_ID) {
		dev_err(&usb_dev->dev, "%s: parse error, id=0x%x != NIUSB_REG_WRITE_ID\n",
			__func__, status.id);
		return -EIO;
	}
	if (status.error_code) {
		dev_err(&usb_dev->dev, "%s: nonzero error code 0x%x\n",
			__func__, status.error_code);
		return -EIO;
	}
	if (reg_writes_completed != num_writes) {
		dev_err(&usb_dev->dev, "%s: reg_writes_completed=%i, num_writes=%i\n",
			__func__, reg_writes_completed, num_writes);
		return -EIO;
	}
	if (ibsta)
		*ibsta = status.ibsta;
	return 0;
}

// interface functions
static int ni_usb_read(gpib_board_t *board, uint8_t *buffer, size_t length,
		       int *end, size_t *bytes_read)
{
	int retval, parse_retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x20;
	int in_data_length;
	int usb_bytes_written = 0, usb_bytes_read = 0;
	int i = 0;
	int complement_count;
	int actual_length;
	struct ni_usb_status_block status;
	static const int max_read_length = 0xffff;
	struct ni_usb_register reg;

	*bytes_read = 0;
	if (length > max_read_length)	{
		length = max_read_length;
		dev_err(&usb_dev->dev, "%s: read length too long\n", __func__);
	}
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = 0x0a;
	out_data[i++] = ni_priv->eos_mode >> 8;
	out_data[i++] = ni_priv->eos_char;
	out_data[i++] = ni_usb_timeout_code(board->usec_timeout);
	complement_count = length - 1;
	complement_count = ~complement_count;
	out_data[i++] = complement_count & 0xff;
	out_data[i++] = (complement_count >> 8) & 0xff;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	i += ni_usb_bulk_register_write_header(&out_data[i], 2);
	reg.device = NIUSB_SUBDEV_TNT4882;
	reg.address = nec7210_to_tnt4882_offset(AUXMR);
	reg.value = AUX_HLDI;
	i += ni_usb_bulk_register_write(&out_data[i], reg);
	reg.value = AUX_CLEAR_END;
	i += ni_usb_bulk_register_write(&out_data[i], reg);
	while (i % 4)	// pad with zeros to 4-byte boundary
		out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &usb_bytes_written, 1000);
	kfree(out_data);
	if (retval || usb_bytes_written != i) {
		if (retval == 0)
			retval = -EIO;
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, usb_bytes_written=%i, i=%i\n",
			__func__, retval, usb_bytes_written, i);
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		return retval;
	}

	in_data_length = (length / 30 + 1) * 0x20 + 0x20;
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		return -ENOMEM;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &usb_bytes_read,
					 ni_usb_timeout_msecs(board->usec_timeout), 1);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if (retval == -ERESTARTSYS) {
	} else if (retval) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, usb_bytes_read=%i\n",
			__func__, retval, usb_bytes_read);
		kfree(in_data);
		return retval;
	}
	parse_retval = parse_board_ibrd_readback(in_data, &status, buffer, length, &actual_length);
	if (parse_retval != usb_bytes_read) {
		if (parse_retval >= 0)
			parse_retval = -EIO;
		dev_err(&usb_dev->dev, "%s: retval=%i usb_bytes_read=%i\n",
			__func__, parse_retval, usb_bytes_read);
		kfree(in_data);
		return parse_retval;
	}
	if (actual_length != length - status.count) {
		dev_err(&usb_dev->dev, "%s: actual_length=%i expected=%li\n",
			__func__, actual_length, (long)(length - status.count));
		ni_usb_dump_raw_block(in_data, usb_bytes_read);
	}
	kfree(in_data);
	switch (status.error_code) {
	case NIUSB_NO_ERROR:
		retval = 0;
		break;
	case NIUSB_ABORTED_ERROR:
		/* this is expected if ni_usb_receive_bulk_msg got
		 * interrupted by a signal and returned -ERESTARTSYS
		 */
		break;
	case NIUSB_ATN_STATE_ERROR:
		retval = -EIO;
		dev_err(&usb_dev->dev, "%s: read when ATN set\n", __func__);
		break;
	case NIUSB_ADDRESSING_ERROR:
		retval = -EIO;
		break;
	case NIUSB_TIMEOUT_ERROR:
		retval = -ETIMEDOUT;
		break;
	case NIUSB_EOSMODE_ERROR:
		dev_err(&usb_dev->dev, "%s: driver bug, we should have been able to avoid NIUSB_EOSMODE_ERROR.\n",
			__func__);
		retval = -EINVAL;
		break;
	default:
		dev_err(&usb_dev->dev, "%s: unknown error code=%i\n", __func__, status.error_code);
		retval = -EIO;
		break;
	}
	ni_usb_soft_update_status(board, status.ibsta, 0);
	if (status.ibsta & END)
		*end = 1;
	else
		*end = 0;
	*bytes_read = actual_length;
	return retval;
}

static int ni_usb_write(gpib_board_t *board, uint8_t *buffer, size_t length,
			int send_eoi, size_t *bytes_written)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	int out_data_length;
	static const int in_data_length = 0x10;
	int usb_bytes_written = 0, usb_bytes_read = 0;
	int i = 0, j;
	int complement_count;
	struct ni_usb_status_block status;
	static const int max_write_length = 0xffff;

	*bytes_written = 0;
	if (length > max_write_length) {
		length = max_write_length;
		send_eoi = 0;
		dev_err(&usb_dev->dev, "%s: write length too long\n", __func__);
	}
	out_data_length = length + 0x10;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = 0x0d;
	complement_count = length - 1;
	complement_count = ~complement_count;
	out_data[i++] = complement_count & 0xff;
	out_data[i++] = (complement_count >> 8) & 0xff;
	out_data[i++] = ni_usb_timeout_code(board->usec_timeout);
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	if (send_eoi)
		out_data[i++] = 0x8;
	else
		out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	for (j = 0; j < length; j++)
		out_data[i++] = buffer[j];
	while (i % 4)	// pad with zeros to 4-byte boundary
		out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &usb_bytes_written,
				      ni_usb_timeout_msecs(board->usec_timeout));
	kfree(out_data);
	if (retval || usb_bytes_written != i)	{
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, usb_bytes_written=%i, i=%i\n",
			__func__, retval, usb_bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		return -ENOMEM;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &usb_bytes_read,
					 ni_usb_timeout_msecs(board->usec_timeout), 1);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if ((retval && retval != -ERESTARTSYS) || usb_bytes_read != 12) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, usb_bytes_read=%i\n",
			__func__, retval, usb_bytes_read);
		kfree(in_data);
		return retval;
	}
	ni_usb_parse_status_block(in_data, &status);
	kfree(in_data);
	switch	(status.error_code) {
	case NIUSB_NO_ERROR:
		retval = 0;
		break;
	case NIUSB_ABORTED_ERROR:
		/* this is expected if ni_usb_receive_bulk_msg got
		 * interrupted by a signal and returned -ERESTARTSYS
		 */
		break;
	case NIUSB_ADDRESSING_ERROR:
		dev_err(&usb_dev->dev, "%s: Addressing error retval %d error code=%i\n",
			__func__, retval, status.error_code);
		retval = -ENXIO;
		break;
	case NIUSB_NO_LISTENER_ERROR:
		retval = -ECOMM;
		break;
	case NIUSB_TIMEOUT_ERROR:
		retval = -ETIMEDOUT;
		break;
	default:
		dev_err(&usb_dev->dev, "%s: unknown error code=%i\n",
			__func__, status.error_code);
		retval = -EPIPE;
		break;
	}
	ni_usb_soft_update_status(board, status.ibsta, 0);
	*bytes_written = length - status.count;
	return retval;
}

static int ni_usb_command_chunk(gpib_board_t *board, uint8_t *buffer, size_t length,
				size_t *command_bytes_written)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	int out_data_length;
	static const int in_data_length = 0x10;
	int bytes_written = 0, bytes_read = 0;
	int i = 0, j;
	unsigned int complement_count;
	struct ni_usb_status_block status;
	// usb-b gives error 4 if you try to send more than 16 command bytes at once
	static const int max_command_length = 0x10;

	*command_bytes_written = 0;
	if (length > max_command_length)
		length = max_command_length;
	out_data_length = length + 0x10;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = 0x0c;
	complement_count = length - 1;
	complement_count = ~complement_count;
	out_data[i++] = complement_count;
	out_data[i++] = 0x0;
	out_data[i++] = ni_usb_timeout_code(board->usec_timeout);
	for (j = 0; j < length; j++)
		out_data[i++] = buffer[j];
	while (i % 4)	// pad with zeros to 4-byte boundary
		out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written,
				      ni_usb_timeout_msecs(board->usec_timeout));
	kfree(out_data);
	if (retval || bytes_written != i) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		return -ENOMEM;
	}

	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read,
					 ni_usb_timeout_msecs(board->usec_timeout), 1);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if ((retval && retval != -ERESTARTSYS) || bytes_read != 12) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		kfree(in_data);
		return retval;
	}
	ni_usb_parse_status_block(in_data, &status);
	kfree(in_data);
	*command_bytes_written = length - status.count;
	switch (status.error_code) {
	case NIUSB_NO_ERROR:
		break;
	case NIUSB_ABORTED_ERROR:
		/* this is expected if ni_usb_receive_bulk_msg got
		 * interrupted by a signal and returned -ERESTARTSYS
		 */
		break;
	case NIUSB_NO_BUS_ERROR:
		return -ENOTCONN;
	case NIUSB_EOSMODE_ERROR:
		dev_err(&usb_dev->dev, "%s: got eosmode error.	Driver bug?\n", __func__);
		return -EIO;
	case NIUSB_TIMEOUT_ERROR:
		return -ETIMEDOUT;
	default:
		dev_err(&usb_dev->dev, "%s: unknown error code=%i\n", __func__, status.error_code);
		return -EIO;
	}
	ni_usb_soft_update_status(board, status.ibsta, 0);
	return 0;
}

static int ni_usb_command(gpib_board_t *board, uint8_t *buffer, size_t length,
			  size_t *bytes_written)
{
	size_t count;
	int retval;

	*bytes_written = 0;
	while (*bytes_written < length) {
		retval = ni_usb_command_chunk(board, buffer + *bytes_written,
					      length - *bytes_written, &count);
		*bytes_written += count;
		if (retval < 0)
			return retval;
	}
	return 0;
}

static int ni_usb_take_control(gpib_board_t *board, int synchronous)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x10;
	static const int  in_data_length = 0x10;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	struct ni_usb_status_block status;

	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = NIUSB_IBCAC_ID;
	if (synchronous)
		out_data[i++] = 0x1;
	else
		out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval || bytes_written != i) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read, 1000, 1);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if ((retval && retval != -ERESTARTSYS) || bytes_read != 12) {
		if (retval == 0)
			retval = -EIO;
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		kfree(in_data);
		return retval;
	}
	ni_usb_parse_status_block(in_data, &status);
	kfree(in_data);
	ni_usb_soft_update_status(board, status.ibsta, 0);
	return retval;
}

static int ni_usb_go_to_standby(gpib_board_t *board)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x10;
	static const int  in_data_length = 0x20;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	struct ni_usb_status_block status;

	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;

	out_data[i++] = NIUSB_IBGTS_ID;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);

	mutex_lock(&ni_priv->addressed_transfer_lock);

	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval || bytes_written != i) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read, 1000, 0);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if (retval || bytes_read != 12) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		kfree(in_data);
		return retval;
	}
	ni_usb_parse_status_block(in_data, &status);
	kfree(in_data);
	if (status.id != NIUSB_IBGTS_ID)
		dev_err(&usb_dev->dev, "%s: bug: status.id 0x%x != INUSB_IBGTS_ID\n",
			__func__, status.id);
	ni_usb_soft_update_status(board, status.ibsta, 0);
	return 0;
}

static void ni_usb_request_system_control(gpib_board_t *board, int request_control)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[4];
	unsigned int ibsta;

	if (request_control) {
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = CMDR;
		writes[i].value = SETSC;
		i++;
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
		writes[i].value = AUX_CIFC;
		i++;
	} else {
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
		writes[i].value = AUX_CREN;
		i++;
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
		writes[i].value = AUX_CIFC;
		i++;
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
		writes[i].value = AUX_DSC;
		i++;
		writes[i].device = NIUSB_SUBDEV_TNT4882;
		writes[i].address = CMDR;
		writes[i].value = CLRSC;
		i++;
	}
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return; // retval;
	}
	if (!request_control)
		ni_priv->ren_state = 0;
	ni_usb_soft_update_status(board, ibsta, 0);
	return; // 0;
}

//FIXME maybe the interface should have a "pulse interface clear" function that can return an error?
static void ni_usb_interface_clear(gpib_board_t *board, int assert)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x10;
	static const int  in_data_length = 0x10;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	struct ni_usb_status_block status;

	// FIXME: we are going to pulse when assert is true, and ignore otherwise
	if (assert == 0)
		return;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)	{
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return;
	}
	out_data[i++] = NIUSB_IBSIC_ID;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);
	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval || bytes_written != i) {
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return;
	}
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data)
		return;

	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read, 1000, 0);
	if (retval || bytes_read != 12) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		kfree(in_data);
		return;
	}
	ni_usb_parse_status_block(in_data, &status);
	kfree(in_data);
	ni_usb_soft_update_status(board, status.ibsta, 0);
}

static void ni_usb_remote_enable(gpib_board_t *board, int enable)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	struct ni_usb_register reg;
	unsigned int ibsta;

	reg.device = NIUSB_SUBDEV_TNT4882;
	reg.address = nec7210_to_tnt4882_offset(AUXMR);
	if (enable)
		reg.value = AUX_SREN;
	else
		reg.value = AUX_CREN;
	retval = ni_usb_write_registers(ni_priv, &reg, 1, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return; //retval;
	}
	ni_priv->ren_state = enable;
	ni_usb_soft_update_status(board, ibsta, 0);
	return;// 0;
}

static int ni_usb_enable_eos(gpib_board_t *board, uint8_t eos_byte, int compare_8_bits)
{
	struct ni_usb_priv *ni_priv = board->private_data;

	ni_priv->eos_char = eos_byte;
	ni_priv->eos_mode |= REOS;
	if (compare_8_bits)
		ni_priv->eos_mode |= BIN;
	else
		ni_priv->eos_mode &= ~BIN;
	return 0;
}

static void ni_usb_disable_eos(gpib_board_t *board)
{
	struct ni_usb_priv *ni_priv = board->private_data;
	/* adapter gets unhappy if you don't zero all the bits
	 *	for the eos mode and eos char (returns error 4 on reads).
	 */
	ni_priv->eos_mode = 0;
	ni_priv->eos_char = 0;
}

static unsigned int ni_usb_update_status(gpib_board_t *board, unsigned int clear_mask)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	static const int buffer_length = 8;
	u8 *buffer;
	struct ni_usb_status_block status;

	//printk("%s: receive control pipe is %i\n", __func__, pipe);
	buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!buffer)
		return board->status;

	retval = ni_usb_receive_control_msg(ni_priv, NI_USB_WAIT_REQUEST, USB_DIR_IN |
					    USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					    0x200, 0x0, buffer, buffer_length, 1000);
	if (retval != buffer_length) {
		dev_err(&usb_dev->dev, "%s: usb_control_msg returned %i\n", __func__, retval);
		kfree(buffer);
		return board->status;
	}
	ni_usb_parse_status_block(buffer, &status);
	kfree(buffer);
	ni_usb_soft_update_status(board, status.ibsta, clear_mask);
	return board->status;
}

// tells ni-usb to immediately stop an ongoing i/o operation
static void ni_usb_stop(struct ni_usb_priv *ni_priv)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	static const int buffer_length = 8;
	u8 *buffer;
	struct ni_usb_status_block status;

	//printk("%s: receive control pipe is %i\n", __func__, pipe);
	buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!buffer)
		return;

	retval = ni_usb_receive_control_msg(ni_priv, NI_USB_STOP_REQUEST, USB_DIR_IN |
					    USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					    0x0, 0x0, buffer, buffer_length, 1000);
	if (retval != buffer_length) {
		dev_err(&usb_dev->dev, "%s: usb_control_msg returned %i\n", __func__, retval);
		kfree(buffer);
		return;
	}
	ni_usb_parse_status_block(buffer, &status);
	kfree(buffer);
}

static int ni_usb_primary_address(gpib_board_t *board, unsigned int address)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[2];
	unsigned int ibsta;

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(ADR);
	writes[i].value = address;
	i++;
	writes[i].device = NIUSB_SUBDEV_UNKNOWN2;
	writes[i].address = 0x0;
	writes[i].value = address;
	i++;
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return 0;
}

static int ni_usb_write_sad(struct ni_usb_register *writes, int address, int enable)
{
	unsigned int adr_bits, admr_bits;
	int i = 0;

	adr_bits = HR_ARS;
	admr_bits = HR_TRM0 | HR_TRM1;
	if (enable) {
		adr_bits |= address;
		admr_bits |= HR_ADM1;
	} else {
		adr_bits |= HR_DT | HR_DL;
		admr_bits |= HR_ADM0;
	}
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(ADR);
	writes[i].value = adr_bits;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(ADMR);
	writes[i].value = admr_bits;
	i++;
	writes[i].device = NIUSB_SUBDEV_UNKNOWN2;
	writes[i].address = 0x1;
	writes[i].value = enable ? MSA(address) : 0x0;
	i++;
	return i;
}

static int ni_usb_secondary_address(gpib_board_t *board, unsigned int address, int enable)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[3];
	unsigned int ibsta;

	i += ni_usb_write_sad(writes, address, enable);
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return 0;
}

static int ni_usb_parallel_poll(gpib_board_t *board, uint8_t *result)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x10;
	static const int  in_data_length = 0x20;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	int j = 0;
	struct ni_usb_status_block status;

	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;

	out_data[i++] = NIUSB_IBRPP_ID;
	out_data[i++] = 0xf0;	//FIXME: this should be the parallel poll timeout code
	out_data[i++] = 0x0;
	out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);
	/*FIXME: 1000 should use parallel poll timeout (not supported yet)*/
	retval = ni_usb_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);

	kfree(out_data);
	if (retval || bytes_written != i) {
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			__func__, retval, bytes_written, i);
		return retval;
	}
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data)
		return -ENOMEM;

	/*FIXME: should use parallel poll timeout (not supported yet)*/
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length,
					 &bytes_read, 1000, 1);

	if (retval && retval != -ERESTARTSYS)	{
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		kfree(in_data);
		return retval;
	}
	j += ni_usb_parse_status_block(in_data, &status);
	*result = in_data[j++];
	kfree(in_data);
	ni_usb_soft_update_status(board, status.ibsta, 0);
	return retval;
}

static void ni_usb_parallel_poll_configure(gpib_board_t *board, uint8_t config)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[1];
	unsigned int ibsta;

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = PPR | config;
	i++;
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return;// retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return;// 0;
}

static void ni_usb_parallel_poll_response(gpib_board_t *board, int ist)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[1];
	unsigned int ibsta;

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	if (ist)
		writes[i].value = AUX_SPPF;
	else
		writes[i].value = AUX_CPPF;
	i++;
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return;// retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return;// 0;
}

static void ni_usb_serial_poll_response(gpib_board_t *board, u8 status)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[1];
	unsigned int ibsta;

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(SPMR);
	writes[i].value = status;
	i++;
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return;// retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return;// 0;
}

static uint8_t ni_usb_serial_poll_status(gpib_board_t *board)
{
	return 0;
}

static void ni_usb_return_to_local(gpib_board_t *board)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int i = 0;
	struct ni_usb_register writes[1];
	unsigned int ibsta;

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_RTL;
	i++;
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return;// retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return;// 0;
}

static int ni_usb_line_status(const gpib_board_t *board)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	u8 *out_data, *in_data;
	static const int out_data_length = 0x20;
	static const int  in_data_length = 0x20;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	unsigned int bsr_bits;
	int line_status = ValidALL;
	// NI windows driver reads 0xd(HSSEL), 0xc (ARD0), 0x1f (BSR)

	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;

	/* line status gets called during ibwait */
	retval = mutex_trylock(&ni_priv->addressed_transfer_lock);

	if (retval == 0) {
		kfree(out_data);
		return -EBUSY;
	}
	i += ni_usb_bulk_register_read_header(&out_data[i], 1);
	i += ni_usb_bulk_register_read(&out_data[i], NIUSB_SUBDEV_TNT4882, BSR);
	while (i % 4)
		out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);
	retval = ni_usb_nonblocking_send_bulk_msg(ni_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval || bytes_written != i) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		if (retval != -EAGAIN)
			dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
				__func__, retval, bytes_written, i);
		return retval;
	}

	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&ni_priv->addressed_transfer_lock);
		dev_err(&usb_dev->dev, "%s: kmalloc failed\n", __func__);
		return -ENOMEM;
	}
	retval = ni_usb_nonblocking_receive_bulk_msg(ni_priv, in_data, in_data_length,
						     &bytes_read, 1000, 0);

	mutex_unlock(&ni_priv->addressed_transfer_lock);

	if (retval) {
		if (retval != -EAGAIN)
			dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
				__func__, retval, bytes_read);
		kfree(in_data);
		return retval;
	}

	ni_usb_parse_register_read_block(in_data, &bsr_bits, 1);
	kfree(in_data);
	if (bsr_bits & BCSR_REN_BIT)
		line_status |= BusREN;
	if (bsr_bits & BCSR_IFC_BIT)
		line_status |= BusIFC;
	if (bsr_bits & BCSR_SRQ_BIT)
		line_status |= BusSRQ;
	if (bsr_bits & BCSR_EOI_BIT)
		line_status |= BusEOI;
	if (bsr_bits & BCSR_NRFD_BIT)
		line_status |= BusNRFD;
	if (bsr_bits & BCSR_NDAC_BIT)
		line_status |= BusNDAC;
	if (bsr_bits & BCSR_DAV_BIT)
		line_status |= BusDAV;
	if (bsr_bits & BCSR_ATN_BIT)
		line_status |= BusATN;
	return line_status;
}

static int ni_usb_setup_t1_delay(struct ni_usb_register *reg, unsigned int nano_sec,
				 unsigned int *actual_ns)
{
	int i = 0;

	*actual_ns = 2000;

	reg[i].device = NIUSB_SUBDEV_TNT4882;
	reg[i].address = nec7210_to_tnt4882_offset(AUXMR);
	if (nano_sec <= 1100)	{
		reg[i].value = AUXRI | USTD | SISB;
		*actual_ns = 1100;
	} else {
		reg[i].value = AUXRI | SISB;
	}
	i++;
	reg[i].device = NIUSB_SUBDEV_TNT4882;
	reg[i].address = nec7210_to_tnt4882_offset(AUXMR);
	if (nano_sec <= 500)	{
		reg[i].value = AUXRB | HR_TRI;
		*actual_ns = 500;
	} else {
		reg[i].value = AUXRB;
	}
	i++;
	reg[i].device = NIUSB_SUBDEV_TNT4882;
	reg[i].address = KEYREG;
	if (nano_sec <= 350) {
		reg[i].value = MSTD;
		*actual_ns = 350;
	} else {
		reg[i].value = 0x0;
	}
	i++;
	return i;
}

static unsigned int ni_usb_t1_delay(gpib_board_t *board, unsigned int nano_sec)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	struct ni_usb_register writes[3];
	unsigned int ibsta;
	unsigned int actual_ns;
	int i;

	i = ni_usb_setup_t1_delay(writes, nano_sec, &actual_ns);
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return -1;	//FIXME should change return type to int for error reporting
	}
	board->t1_nano_sec = actual_ns;
	ni_usb_soft_update_status(board, ibsta, 0);
	return actual_ns;
}

static int ni_usb_allocate_private(gpib_board_t *board)
{
	struct ni_usb_priv *ni_priv;

	board->private_data = kmalloc(sizeof(struct ni_usb_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	ni_priv = board->private_data;
	memset(ni_priv, 0, sizeof(struct ni_usb_priv));
	mutex_init(&ni_priv->bulk_transfer_lock);
	mutex_init(&ni_priv->control_transfer_lock);
	mutex_init(&ni_priv->interrupt_transfer_lock);
	mutex_init(&ni_priv->addressed_transfer_lock);
	return 0;
}

static void ni_usb_free_private(struct ni_usb_priv *ni_priv)
{
	usb_free_urb(ni_priv->interrupt_urb);
	kfree(ni_priv);
}

#define NUM_INIT_WRITES 26
static int ni_usb_setup_init(gpib_board_t *board, struct ni_usb_register *writes)
{
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	unsigned int mask, actual_ns;
	int i = 0;

	writes[i].device = NIUSB_SUBDEV_UNKNOWN3;
	writes[i].address = 0x10;
	writes[i].value = 0x0;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = CMDR;
	writes[i].value = SOFT_RESET;
	i++;
	writes[i].device =  NIUSB_SUBDEV_TNT4882;
	writes[i].address =  nec7210_to_tnt4882_offset(AUXMR);
	mask = AUXRA | HR_HLDA;
	if (ni_priv->eos_mode & BIN)
		mask |= HR_BIN;
	writes[i].value = mask;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = AUXCR;
	writes[i].value = mask;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = HSSEL;
	writes[i].value = TNT_ONE_CHIP_BIT;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_CR;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = IMR0;
	writes[i].value = TNT_IMR0_ALWAYS_BITS;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(IMR1);
	writes[i].value = 0x0;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address =  nec7210_to_tnt4882_offset(IMR2);
	writes[i].value = 0x0;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = IMR3;
	writes[i].value = 0x0;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_HLDI;
	i++;

	i += ni_usb_setup_t1_delay(&writes[i], board->t1_nano_sec, &actual_ns);

	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUXRG | NTNL_BIT;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = CMDR;
	if (board->master)
		mask = SETSC; // set system controller
	else
		mask = CLRSC; // clear system controller
	writes[i].value = mask;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_CIFC;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(ADR);
	writes[i].value = board->pad;
	i++;
	writes[i].device = NIUSB_SUBDEV_UNKNOWN2;
	writes[i].address = 0x0;
	writes[i].value = board->pad;
	i++;

	i += ni_usb_write_sad(&writes[i], board->sad, board->sad >= 0);

	writes[i].device = NIUSB_SUBDEV_UNKNOWN2;
	writes[i].address = 0x2; // could this be a timeout ?
	writes[i].value = 0xfd;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = 0xf; // undocumented address
	writes[i].value = 0x11;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_PON;
	i++;
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_CPPF;
	i++;
	if (i > NUM_INIT_WRITES) {
		dev_err(&usb_dev->dev, "%s: bug!, buffer overrun, i=%i\n", __func__, i);
		return 0;
	}
	return i;
}

static int ni_usb_init(gpib_board_t *board)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	struct ni_usb_register *writes;
	unsigned int ibsta;
	int writes_len;

	writes = kmalloc_array(NUM_INIT_WRITES, sizeof(*writes), GFP_KERNEL);
	if (!writes)
		return -ENOMEM;

	writes_len = ni_usb_setup_init(board, writes);
	if (writes_len)
		retval = ni_usb_write_registers(ni_priv, writes, writes_len, &ibsta);
	else
		return -EFAULT;
	kfree(writes);
	if (retval) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return retval;
	}
	ni_usb_soft_update_status(board, ibsta, 0);
	return 0;
}

static void ni_usb_interrupt_complete(struct urb *urb)
{
	gpib_board_t *board = urb->context;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	struct ni_usb_status_block status;
	unsigned long flags;

//	printk("debug: %s: status=0x%x, error_count=%i, actual_length=%i\n", __func__,
//		urb->status, urb->error_count, urb->actual_length);

	switch (urb->status) {
		/* success */
	case 0:
		break;
		/* unlinked, don't resubmit */
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	default: /* other error, resubmit */
		retval = usb_submit_urb(ni_priv->interrupt_urb, GFP_ATOMIC);
		if (retval)
			dev_err(&usb_dev->dev, "%s: failed to resubmit interrupt urb\n", __func__);
		return;
	}

	ni_usb_parse_status_block(urb->transfer_buffer, &status);
//	printk("debug: ibsta=0x%x\n", status.ibsta);

	spin_lock_irqsave(&board->spinlock, flags);
	ni_priv->monitored_ibsta_bits &= ~status.ibsta;
//	printk("debug: monitored_ibsta_bits=0x%x\n", ni_priv->monitored_ibsta_bits);
	spin_unlock_irqrestore(&board->spinlock, flags);

	wake_up_interruptible(&board->wait);

	retval = usb_submit_urb(ni_priv->interrupt_urb, GFP_ATOMIC);
	if (retval)
		dev_err(&usb_dev->dev, "%s: failed to resubmit interrupt urb\n", __func__);
}

static int ni_usb_set_interrupt_monitor(gpib_board_t *board, unsigned int monitored_bits)
{
	int retval;
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	static const int buffer_length = 8;
	u8 *buffer;
	struct ni_usb_status_block status;
	unsigned long flags;
	//printk("%s: receive control pipe is %i\n", __func__, pipe);
	buffer = kmalloc(buffer_length, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	spin_lock_irqsave(&board->spinlock, flags);
	ni_priv->monitored_ibsta_bits = ni_usb_ibsta_monitor_mask & monitored_bits;
//	dev_err(&usb_dev->dev, "debug: %s: monitored_ibsta_bits=0x%x\n",
//	__func__, ni_priv->monitored_ibsta_bits);
	spin_unlock_irqrestore(&board->spinlock, flags);
	retval = ni_usb_receive_control_msg(ni_priv, NI_USB_WAIT_REQUEST, USB_DIR_IN |
					    USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					    0x300, ni_usb_ibsta_monitor_mask & monitored_bits,
					    buffer, buffer_length, 1000);
	if (retval != buffer_length) {
		dev_err(&usb_dev->dev, "%s: usb_control_msg returned %i\n", __func__, retval);
		kfree(buffer);
		return -1;
	}
	ni_usb_parse_status_block(buffer, &status);
	kfree(buffer);
	return 0;
}

static int ni_usb_setup_urbs(gpib_board_t *board)
{
	struct ni_usb_priv *ni_priv = board->private_data;
	struct usb_device *usb_dev;
	int int_pipe;
	int retval;

	if (ni_priv->interrupt_in_endpoint < 0)
		return 0;

	mutex_lock(&ni_priv->interrupt_transfer_lock);
	if (!ni_priv->bus_interface) {
		mutex_unlock(&ni_priv->interrupt_transfer_lock);
		return -ENODEV;
	}
	ni_priv->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ni_priv->interrupt_urb) {
		mutex_unlock(&ni_priv->interrupt_transfer_lock);
		return -ENOMEM;
	}
	usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int_pipe = usb_rcvintpipe(usb_dev, ni_priv->interrupt_in_endpoint);
	usb_fill_int_urb(ni_priv->interrupt_urb, usb_dev, int_pipe, ni_priv->interrupt_buffer,
			 sizeof(ni_priv->interrupt_buffer), &ni_usb_interrupt_complete, board, 1);
	retval = usb_submit_urb(ni_priv->interrupt_urb, GFP_KERNEL);
	mutex_unlock(&ni_priv->interrupt_transfer_lock);
	if (retval) {
		dev_err(&usb_dev->dev, "%s: failed to submit first interrupt urb, retval=%i\n",
			__func__, retval);
		return retval;
	}
	return 0;
}

static void ni_usb_cleanup_urbs(struct ni_usb_priv *ni_priv)
{
	if (ni_priv && ni_priv->bus_interface) {
		if (ni_priv->interrupt_urb)
			usb_kill_urb(ni_priv->interrupt_urb);
		if (ni_priv->bulk_urb)
			usb_kill_urb(ni_priv->bulk_urb);
	}
}

static int ni_usb_b_read_serial_number(struct ni_usb_priv *ni_priv)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	u8 *out_data;
	u8 *in_data;
	static const int out_data_length = 0x20;
	static const int  in_data_length = 0x20;
	int bytes_written = 0, bytes_read = 0;
	int i = 0;
	static const int num_reads = 4;
	unsigned int results[4];
	int j;
	unsigned int serial_number;

//	printk("%s: %s\n", __func__);
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data)
		return -ENOMEM;

	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data) {
		kfree(in_data);
		return -ENOMEM;
	}
	i += ni_usb_bulk_register_read_header(&out_data[i], num_reads);
	i += ni_usb_bulk_register_read(&out_data[i], NIUSB_SUBDEV_UNKNOWN3, SERIAL_NUMBER_1_REG);
	i += ni_usb_bulk_register_read(&out_data[i], NIUSB_SUBDEV_UNKNOWN3, SERIAL_NUMBER_2_REG);
	i += ni_usb_bulk_register_read(&out_data[i], NIUSB_SUBDEV_UNKNOWN3, SERIAL_NUMBER_3_REG);
	i += ni_usb_bulk_register_read(&out_data[i], NIUSB_SUBDEV_UNKNOWN3, SERIAL_NUMBER_4_REG);
	while (i % 4)
		out_data[i++] = 0x0;
	i += ni_usb_bulk_termination(&out_data[i]);
	retval = ni_usb_send_bulk_msg(ni_priv, out_data, out_data_length, &bytes_written, 1000);
	if (retval) {
		dev_err(&usb_dev->dev, "%s: ni_usb_send_bulk_msg returned %i, bytes_written=%i, i=%li\n",
			__func__,
			retval, bytes_written, (long)out_data_length);
		goto serial_out;
	}
	retval = ni_usb_receive_bulk_msg(ni_priv, in_data, in_data_length, &bytes_read, 1000, 0);
	if (retval) {
		dev_err(&usb_dev->dev, "%s: ni_usb_receive_bulk_msg returned %i, bytes_read=%i\n",
			__func__, retval, bytes_read);
		ni_usb_dump_raw_block(in_data, bytes_read);
		goto serial_out;
	}
	if (ARRAY_SIZE(results) < num_reads) {
		dev_err(&usb_dev->dev, "Setup bug\n");
		retval = -EINVAL;
		goto serial_out;
	}
	ni_usb_parse_register_read_block(in_data, results, num_reads);
	serial_number = 0;
	for (j = 0; j < num_reads; ++j)
		serial_number |= (results[j] & 0xff) << (8 * j);
	dev_info(&usb_dev->dev, "%s: board serial number is 0x%x\n", __func__, serial_number);
	retval = 0;
serial_out:
	kfree(in_data);
	kfree(out_data);
	return retval;
}

static int ni_usb_hs_wait_for_ready(struct ni_usb_priv *ni_priv)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	static const int buffer_size = 0x10;
	static const int timeout = 50;
	static const int msec_sleep_duration = 100;
	int i;	int retval;
	int j;
	int unexpected = 0;
	unsigned int serial_number;
	u8 *buffer;

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;

	retval = ni_usb_receive_control_msg(ni_priv, NI_USB_SERIAL_NUMBER_REQUEST,
					    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
					    0x0, 0x0, buffer, buffer_size, 1000);
	if (retval < 0) {
		dev_err(&usb_dev->dev, "%s: usb_control_msg request 0x%x returned %i\n",
			__func__, NI_USB_SERIAL_NUMBER_REQUEST, retval);
		goto ready_out;
	}
	j = 0;
	if (buffer[j] != NI_USB_SERIAL_NUMBER_REQUEST) {
		dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x%x\n",
			__func__, j, (int)buffer[j], NI_USB_SERIAL_NUMBER_REQUEST);
		unexpected = 1;
	}
	if (unexpected)
		ni_usb_dump_raw_block(buffer, retval);
	// NI-USB-HS+ pads the serial with 0x0 to make 16 bytes
	if (retval != 5 && retval != 16) {
		dev_err(&usb_dev->dev, "%s: received unexpected number of bytes = %i, expected 5 or 16\n",
			__func__, retval);
		ni_usb_dump_raw_block(buffer, retval);
	}
	serial_number = 0;
	serial_number |= buffer[++j];
	serial_number |= (buffer[++j] << 8);
	serial_number |= (buffer[++j] << 16);
	serial_number |= (buffer[++j] << 24);
	dev_info(&usb_dev->dev, "%s: board serial number is 0x%x\n", __func__, serial_number);
	for (i = 0; i < timeout; ++i) {
		int ready = 0;

		retval = ni_usb_receive_control_msg(ni_priv, NI_USB_POLL_READY_REQUEST,
						    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						    0x0, 0x0, buffer, buffer_size, 100);
		if (retval < 0) {
			dev_err(&usb_dev->dev, "%s: usb_control_msg request 0x%x returned %i\n",
				__func__, NI_USB_POLL_READY_REQUEST, retval);
			goto ready_out;
		}
		j = 0;
		unexpected = 0;
		if (buffer[j] != NI_USB_POLL_READY_REQUEST) { // [0]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x%x\n",
				__func__, j, (int)buffer[j], NI_USB_POLL_READY_REQUEST);
			unexpected = 1;
		}
		++j;
		if (buffer[j] != 0x1 && buffer[j] != 0x0) { // [1] HS+ sends 0x0
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x1 or 0x0\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		if (buffer[++j] != 0x0) { // [2]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x%x\n",
				__func__, j, (int)buffer[j], 0x0);
			unexpected = 1;
		}
		++j;
		// MC usb-488 (and sometimes NI-USB-HS?) sends 0x8 here; MC usb-488A sends 0x7 here
		// NI-USB-HS+ sends 0x0
		if (buffer[j] != 0x1 && buffer[j] != 0x8 && buffer[j] != 0x7 && buffer[j] != 0x0) {
			// [3]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x0, 0x1, 0x7 or 0x8\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		++j;
		// NI-USB-HS+ sends 0 here
		if (buffer[j] != 0x30 && buffer[j] != 0x0) { // [4]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x0 or 0x30\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		++j;
		// MC usb-488 (and sometimes NI-USB-HS?) and NI-USB-HS+ sends 0x0 here
		if (buffer[j] != 0x1 && buffer[j] != 0x0) { // [5]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x1 or 0x0\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		if (buffer[++j] != 0x0) { // [6]
			ready = 1;
			// NI-USB-HS+ sends 0xf here
			if (buffer[j] != 0x2 && buffer[j] != 0xe && buffer[j] != 0xf &&
			    buffer[j] != 0x16)	{
				dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x2, 0xe, 0xf or 0x16\n",
					__func__, j, (int)buffer[j]);
				unexpected = 1;
			}
		}
		if (buffer[++j] != 0x0) { // [7]
			ready = 1;
			// MC usb-488 sends 0x5 here; MC usb-488A sends 0x6 here
			if (buffer[j] != 0x3 && buffer[j] != 0x5 && buffer[j] != 0x6 &&
			    buffer[j] != 0x8)	{
				dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x3 or 0x5, 0x6 or 0x08\n",
					__func__, j, (int)buffer[j]);
				unexpected = 1;
			}
		}
		++j;
		if (buffer[j] != 0x0 && buffer[j] != 0x2) { // [8] MC usb-488 sends 0x2 here
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x0 or 0x2\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		++j;
		// MC usb-488A and NI-USB-HS sends 0x3 here; NI-USB-HS+ sends 0x30 here
		if (buffer[j] != 0x0 && buffer[j] != 0x3 && buffer[j] != 0x30) { // [9]
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x0, 0x3 or 0x30\n",
				__func__, j, (int)buffer[j]);
			unexpected = 1;
		}
		if (buffer[++j] != 0x0) {
			ready = 1;
			if (buffer[j] != 0x96 && buffer[j] != 0x7 && buffer[j] != 0x6e) {
// [10] MC usb-488 sends 0x7 here
				dev_err(&usb_dev->dev, "%s: unexpected data: buffer[%i]=0x%x, expected 0x96, 0x07 or 0x6e\n",
					__func__, j, (int)buffer[j]);
				unexpected = 1;
			}
		}
		if (unexpected)
			ni_usb_dump_raw_block(buffer, retval);
		if (ready)
			break;
		retval = msleep_interruptible(msec_sleep_duration);
		if (retval) {
			dev_err(&usb_dev->dev, "ni_usb_gpib: msleep interrupted\n");
			retval = -ERESTARTSYS;
			goto ready_out;
		}
	}
	retval = 0;

ready_out:
	kfree(buffer);
	dev_dbg(&usb_dev->dev, "%s: exit retval=%d\n", __func__, retval);
	return retval;
}

/* This does some extra init for HS+ models, as observed on Windows.  One of the
 * control requests causes the LED to stop blinking.
 * I'm not sure what the other 2 requests do.  None of these requests are actually required
 * for the adapter to work, maybe they do some init for the analyzer interface
 * (which we don't use).
 */
static int ni_usb_hs_plus_extra_init(struct ni_usb_priv *ni_priv)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	u8 *buffer;
	static const int buffer_size = 16;
	int transfer_size;

	buffer = kmalloc(buffer_size, GFP_KERNEL);
	if (!buffer)
		return -ENOMEM;
	do {
		transfer_size = 16;

		retval = ni_usb_receive_control_msg(ni_priv, NI_USB_HS_PLUS_0x48_REQUEST,
						    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						    0x0, 0x0, buffer, transfer_size, 1000);
		if (retval < 0) {
			dev_err(&usb_dev->dev, "%s: usb_control_msg request 0x%x returned %i\n",
				__func__, NI_USB_HS_PLUS_0x48_REQUEST, retval);
			break;
		}
		// expected response data: 48 f3 30 00 00 00 00 00 00 00 00 00 00 00 00 00
		if (buffer[0] != NI_USB_HS_PLUS_0x48_REQUEST)
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[0]=0x%x, expected 0x%x\n",
				__func__, (int)buffer[0], NI_USB_HS_PLUS_0x48_REQUEST);

		transfer_size = 2;

		retval = ni_usb_receive_control_msg(ni_priv, NI_USB_HS_PLUS_LED_REQUEST,
						    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						    0x1, 0x0, buffer, transfer_size, 1000);
		if (retval < 0) {
			dev_err(&usb_dev->dev, "%s: usb_control_msg request 0x%x returned %i\n",
				__func__, NI_USB_HS_PLUS_LED_REQUEST, retval);
			break;
		}
		// expected response data: 4b 00
		if (buffer[0] != NI_USB_HS_PLUS_LED_REQUEST)
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[0]=0x%x, expected 0x%x\n",
				__func__, (int)buffer[0], NI_USB_HS_PLUS_LED_REQUEST);

		transfer_size = 9;

		retval = ni_usb_receive_control_msg(ni_priv, NI_USB_HS_PLUS_0xf8_REQUEST,
						    USB_DIR_IN | USB_TYPE_VENDOR |
						    USB_RECIP_INTERFACE,
						    0x0, 0x1, buffer, transfer_size, 1000);
		if (retval < 0) {
			dev_err(&usb_dev->dev, "%s: usb_control_msg request 0x%x returned %i\n",
				__func__, NI_USB_HS_PLUS_0xf8_REQUEST, retval);
			break;
		}
		// expected response data: f8 01 00 00 00 01 00 00 00
		if (buffer[0] != NI_USB_HS_PLUS_0xf8_REQUEST)
			dev_err(&usb_dev->dev, "%s: unexpected data: buffer[0]=0x%x, expected 0x%x\n",
				__func__, (int)buffer[0], NI_USB_HS_PLUS_0xf8_REQUEST);

	} while (0);

	// cleanup
	kfree(buffer);
	return retval;
}

static inline int ni_usb_device_match(struct usb_interface *interface,
				      const gpib_board_config_t *config)
{
	if (gpib_match_device_path(&interface->dev, config->device_path) == 0)
		return 0;
	return 1;
}

static int ni_usb_attach(gpib_board_t *board, const gpib_board_config_t *config)
{
	int retval;
	int i;
	struct ni_usb_priv *ni_priv;
	int product_id;
	struct usb_device *usb_dev;

	mutex_lock(&ni_usb_hotplug_lock);
	retval = ni_usb_allocate_private(board);
	if (retval < 0)		{
		mutex_unlock(&ni_usb_hotplug_lock);
		return retval;
	}
	ni_priv = board->private_data;
	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++)	{
		if (ni_usb_driver_interfaces[i] &&
		    !usb_get_intfdata(ni_usb_driver_interfaces[i]) &&
		    ni_usb_device_match(ni_usb_driver_interfaces[i], config)) {
			ni_priv->bus_interface = ni_usb_driver_interfaces[i];
			usb_set_intfdata(ni_usb_driver_interfaces[i], board);
			usb_dev = interface_to_usbdev(ni_priv->bus_interface);
			dev_info(&usb_dev->dev,
				 "bus %d dev num %d attached to gpib minor %d, NI usb interface %i\n",
				 usb_dev->bus->busnum, usb_dev->devnum, board->minor, i);
			break;
		}
	}
	if (i == MAX_NUM_NI_USB_INTERFACES) {
		mutex_unlock(&ni_usb_hotplug_lock);
		pr_err("No supported NI usb gpib adapters found, have you loaded its firmware?\n");
		return -ENODEV;
	}
	if (usb_reset_configuration(interface_to_usbdev(ni_priv->bus_interface)))
		dev_err(&usb_dev->dev, "ni_usb_gpib: usb_reset_configuration() failed.\n");

	product_id = le16_to_cpu(usb_dev->descriptor.idProduct);
	ni_priv->product_id = product_id;

	timer_setup(&ni_priv->bulk_timer, ni_usb_timeout_handler, 0);

	switch (product_id) {
	case USB_DEVICE_ID_NI_USB_B:
		ni_priv->bulk_out_endpoint = NIUSB_B_BULK_OUT_ENDPOINT;
		ni_priv->bulk_in_endpoint = NIUSB_B_BULK_IN_ENDPOINT;
		ni_priv->interrupt_in_endpoint = NIUSB_B_INTERRUPT_IN_ENDPOINT;
		ni_usb_b_read_serial_number(ni_priv);
		break;
	case USB_DEVICE_ID_NI_USB_HS:
	case USB_DEVICE_ID_MC_USB_488:
	case USB_DEVICE_ID_KUSB_488A:
		ni_priv->bulk_out_endpoint = NIUSB_HS_BULK_OUT_ENDPOINT;
		ni_priv->bulk_in_endpoint = NIUSB_HS_BULK_IN_ENDPOINT;
		ni_priv->interrupt_in_endpoint = NIUSB_HS_INTERRUPT_IN_ENDPOINT;
		retval = ni_usb_hs_wait_for_ready(ni_priv);
		if (retval < 0) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		break;
	case USB_DEVICE_ID_NI_USB_HS_PLUS:
		ni_priv->bulk_out_endpoint = NIUSB_HS_PLUS_BULK_OUT_ENDPOINT;
		ni_priv->bulk_in_endpoint = NIUSB_HS_PLUS_BULK_IN_ENDPOINT;
		ni_priv->interrupt_in_endpoint = NIUSB_HS_PLUS_INTERRUPT_IN_ENDPOINT;
		retval = ni_usb_hs_wait_for_ready(ni_priv);
		if (retval < 0) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		retval = ni_usb_hs_plus_extra_init(ni_priv);
		if (retval < 0) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		break;
	default:
		mutex_unlock(&ni_usb_hotplug_lock);
		dev_err(&usb_dev->dev, "\tDriver bug: unknown endpoints for usb device id %x\n",
			product_id);
		return -EINVAL;
	}

	retval = ni_usb_setup_urbs(board);
	if (retval < 0) {
		mutex_unlock(&ni_usb_hotplug_lock);
		return retval;
	}
	retval = ni_usb_set_interrupt_monitor(board, 0);
	if (retval < 0) {
		mutex_unlock(&ni_usb_hotplug_lock);
		return retval;
	}

	board->t1_nano_sec = 500;

	retval = ni_usb_init(board);
	if (retval < 0) {
		mutex_unlock(&ni_usb_hotplug_lock);
		return retval;
	}
	retval = ni_usb_set_interrupt_monitor(board, ni_usb_ibsta_monitor_mask);
	if (retval < 0)		{
		mutex_unlock(&ni_usb_hotplug_lock);
		return retval;
	}

	mutex_unlock(&ni_usb_hotplug_lock);
	dev_info(&usb_dev->dev, "%s: attached\n", __func__);
	return retval;
}

static int ni_usb_shutdown_hardware(struct ni_usb_priv *ni_priv)
{
	struct usb_device *usb_dev = interface_to_usbdev(ni_priv->bus_interface);
	int retval;
	int i = 0;
	struct ni_usb_register writes[2];
	static const int writes_length = ARRAY_SIZE(writes);
	unsigned int ibsta;

//	printk("%s: %s\n", __func__);
	writes[i].device = NIUSB_SUBDEV_TNT4882;
	writes[i].address = nec7210_to_tnt4882_offset(AUXMR);
	writes[i].value = AUX_CR;
	i++;
	writes[i].device = NIUSB_SUBDEV_UNKNOWN3;
	writes[i].address = 0x10;
	writes[i].value = 0x0;
	i++;
	if (i > writes_length) {
		dev_err(&usb_dev->dev, "%s: bug!, buffer overrun, i=%i\n", __func__, i);
		return -EINVAL;
	}
	retval = ni_usb_write_registers(ni_priv, writes, i, &ibsta);
	if (retval) {
		dev_err(&usb_dev->dev, "%s: register write failed, retval=%i\n", __func__, retval);
		return retval;
	}
	return 0;
}

static void ni_usb_detach(gpib_board_t *board)
{
	struct ni_usb_priv *ni_priv;

	mutex_lock(&ni_usb_hotplug_lock);
// under windows, software unplug does chip_reset nec7210 aux command,
// then writes 0x0 to address 0x10 of device 3
	ni_priv = board->private_data;
	if (ni_priv) {
		if (ni_priv->bus_interface) {
			ni_usb_set_interrupt_monitor(board, 0);
			ni_usb_shutdown_hardware(ni_priv);
			usb_set_intfdata(ni_priv->bus_interface, NULL);
		}
		mutex_lock(&ni_priv->bulk_transfer_lock);
		mutex_lock(&ni_priv->control_transfer_lock);
		mutex_lock(&ni_priv->interrupt_transfer_lock);
		ni_usb_cleanup_urbs(ni_priv);
		ni_usb_free_private(ni_priv);
	}
	mutex_unlock(&ni_usb_hotplug_lock);
}

static gpib_interface_t ni_usb_gpib_interface = {
	.name = "ni_usb_b",
	.attach = ni_usb_attach,
	.detach = ni_usb_detach,
	.read = ni_usb_read,
	.write = ni_usb_write,
	.command = ni_usb_command,
	.take_control = ni_usb_take_control,
	.go_to_standby = ni_usb_go_to_standby,
	.request_system_control = ni_usb_request_system_control,
	.interface_clear = ni_usb_interface_clear,
	.remote_enable = ni_usb_remote_enable,
	.enable_eos = ni_usb_enable_eos,
	.disable_eos = ni_usb_disable_eos,
	.parallel_poll = ni_usb_parallel_poll,
	.parallel_poll_configure = ni_usb_parallel_poll_configure,
	.parallel_poll_response = ni_usb_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = ni_usb_line_status,
	.update_status = ni_usb_update_status,
	.primary_address = ni_usb_primary_address,
	.secondary_address = ni_usb_secondary_address,
	.serial_poll_response = ni_usb_serial_poll_response,
	.serial_poll_status = ni_usb_serial_poll_status,
	.t1_delay = ni_usb_t1_delay,
	.return_to_local = ni_usb_return_to_local,
	.skip_check_for_command_acceptors = 1
};

// Table with the USB-devices: just now only testing IDs
static struct usb_device_id ni_usb_driver_device_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_NI, USB_DEVICE_ID_NI_USB_B)},
	{USB_DEVICE(USB_VENDOR_ID_NI, USB_DEVICE_ID_NI_USB_HS)},
	// gpib-usb-hs+ has a second interface for the analyzer, which we ignore
	{USB_DEVICE_INTERFACE_NUMBER(USB_VENDOR_ID_NI, USB_DEVICE_ID_NI_USB_HS_PLUS, 0)},
	{USB_DEVICE(USB_VENDOR_ID_NI, USB_DEVICE_ID_KUSB_488A)},
	{USB_DEVICE(USB_VENDOR_ID_NI, USB_DEVICE_ID_MC_USB_488)},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ni_usb_driver_device_table);

static int ni_usb_driver_probe(struct usb_interface *interface,	const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	int i;
	char *path;
	static const int path_length = 1024;

	mutex_lock(&ni_usb_hotplug_lock);
	usb_get_dev(usb_dev);
	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++) {
		if (!ni_usb_driver_interfaces[i]) {
			ni_usb_driver_interfaces[i] = interface;
			usb_set_intfdata(interface, NULL);
			break;
		}
	}
	if (i == MAX_NUM_NI_USB_INTERFACES) {
		usb_put_dev(usb_dev);
		mutex_unlock(&ni_usb_hotplug_lock);
		dev_err(&usb_dev->dev, "%s: ni_usb_driver_interfaces[] full\n", __func__);
		return -1;
	}
	path = kmalloc(path_length, GFP_KERNEL);
	if (!path) {
		usb_put_dev(usb_dev);
		mutex_unlock(&ni_usb_hotplug_lock);
		return -ENOMEM;
	}
	usb_make_path(usb_dev, path, path_length);
	dev_info(&usb_dev->dev, "ni_usb_gpib: probe succeeded for path: %s\n", path);
	kfree(path);
	mutex_unlock(&ni_usb_hotplug_lock);
	return 0;
}

static void ni_usb_driver_disconnect(struct usb_interface *interface)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	int i;

	mutex_lock(&ni_usb_hotplug_lock);
	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++)	{
		if (ni_usb_driver_interfaces[i] == interface)	{
			gpib_board_t *board = usb_get_intfdata(interface);

			if (board) {
				struct ni_usb_priv *ni_priv = board->private_data;

				if (ni_priv) {
					mutex_lock(&ni_priv->bulk_transfer_lock);
					mutex_lock(&ni_priv->control_transfer_lock);
					mutex_lock(&ni_priv->interrupt_transfer_lock);
					ni_usb_cleanup_urbs(ni_priv);
					ni_priv->bus_interface = NULL;
					mutex_unlock(&ni_priv->interrupt_transfer_lock);
					mutex_unlock(&ni_priv->control_transfer_lock);
					mutex_unlock(&ni_priv->bulk_transfer_lock);
				}
			}
			ni_usb_driver_interfaces[i] = NULL;
			break;
		}
	}
	if (i == MAX_NUM_NI_USB_INTERFACES)
		dev_err(&usb_dev->dev, "%s: unable to find interface in ni_usb_driver_interfaces[]? bug?\n",
			__func__);
	usb_put_dev(usb_dev);
	mutex_unlock(&ni_usb_hotplug_lock);
}

static int ni_usb_driver_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	gpib_board_t *board;
	int i, retval;

	mutex_lock(&ni_usb_hotplug_lock);

	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++)	{
		if (ni_usb_driver_interfaces[i] == interface) {
			board = usb_get_intfdata(interface);
			if (board)
				break;
		}
	}
	if (i == MAX_NUM_NI_USB_INTERFACES) {
		mutex_unlock(&ni_usb_hotplug_lock);
		return 0;
	}

	struct ni_usb_priv *ni_priv = board->private_data;

	if (ni_priv) {
		ni_usb_set_interrupt_monitor(board, 0);
		retval = ni_usb_shutdown_hardware(ni_priv);
		if (retval) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		if (ni_priv->interrupt_urb) {
			mutex_lock(&ni_priv->interrupt_transfer_lock);
			ni_usb_cleanup_urbs(ni_priv);
			mutex_unlock(&ni_priv->interrupt_transfer_lock);
		}
		dev_info(&usb_dev->dev,
			 "bus %d dev num %d  gpib minor %d, ni usb interface %i suspended\n",
			 usb_dev->bus->busnum, usb_dev->devnum, board->minor, i);
	}

	mutex_unlock(&ni_usb_hotplug_lock);
	return 0;
}

static int ni_usb_driver_resume(struct usb_interface *interface)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);

	gpib_board_t *board;
	int i, retval;

	mutex_lock(&ni_usb_hotplug_lock);

	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++)	{
		if (ni_usb_driver_interfaces[i] == interface) {
			board = usb_get_intfdata(interface);
			if (board)
				break;
		}
	}
	if (i == MAX_NUM_NI_USB_INTERFACES) {
		mutex_unlock(&ni_usb_hotplug_lock);
		return 0;
	}

	struct ni_usb_priv *ni_priv = board->private_data;

	if (ni_priv) {
		if (ni_priv->interrupt_urb) {
			mutex_lock(&ni_priv->interrupt_transfer_lock);
			retval = usb_submit_urb(ni_priv->interrupt_urb, GFP_KERNEL);
			if (retval) {
				dev_err(&usb_dev->dev, "%s: failed to resubmit interrupt urb, retval=%i\n",
					__func__, retval);
				mutex_unlock(&ni_priv->interrupt_transfer_lock);
				mutex_unlock(&ni_usb_hotplug_lock);
				return retval;
			}
			mutex_unlock(&ni_priv->interrupt_transfer_lock);
		} else {
			dev_err(&usb_dev->dev, "%s: bug! int urb not set up\n", __func__);
			mutex_unlock(&ni_usb_hotplug_lock);
			return -EINVAL;
		}

		switch (ni_priv->product_id) {
		case USB_DEVICE_ID_NI_USB_B:
			ni_usb_b_read_serial_number(ni_priv);
			break;
		case USB_DEVICE_ID_NI_USB_HS:
		case USB_DEVICE_ID_MC_USB_488:
		case USB_DEVICE_ID_KUSB_488A:
			retval = ni_usb_hs_wait_for_ready(ni_priv);
			if (retval < 0) {
				mutex_unlock(&ni_usb_hotplug_lock);
				return retval;
			}
			break;
		case USB_DEVICE_ID_NI_USB_HS_PLUS:
			retval = ni_usb_hs_wait_for_ready(ni_priv);
			if (retval < 0) {
				mutex_unlock(&ni_usb_hotplug_lock);
				return retval;
			}
			retval = ni_usb_hs_plus_extra_init(ni_priv);
			if (retval < 0) {
				mutex_unlock(&ni_usb_hotplug_lock);
				return retval;
			}
			break;
		default:
			mutex_unlock(&ni_usb_hotplug_lock);
			dev_err(&usb_dev->dev, "\tDriver bug: unknown endpoints for usb device id\n");
			return -EINVAL;
		}

		retval = ni_usb_set_interrupt_monitor(board, 0);
		if (retval < 0) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}

		retval = ni_usb_init(board);
		if (retval < 0) {
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		retval = ni_usb_set_interrupt_monitor(board, ni_usb_ibsta_monitor_mask);
		if (retval < 0)		{
			mutex_unlock(&ni_usb_hotplug_lock);
			return retval;
		}
		if (board->master)
			ni_usb_interface_clear(board, 1); // this is a pulsed action
		if (ni_priv->ren_state)
			ni_usb_remote_enable(board, 1);

		dev_info(&usb_dev->dev,
			 "bus %d dev num %d  gpib minor %d, ni usb interface %i resumed\n",
			 usb_dev->bus->busnum, usb_dev->devnum, board->minor, i);
	}

	mutex_unlock(&ni_usb_hotplug_lock);
	return 0;
}

static struct usb_driver ni_usb_bus_driver = {
	.name = "ni_usb_gpib",
	.probe = ni_usb_driver_probe,
	.disconnect = ni_usb_driver_disconnect,
	.suspend = ni_usb_driver_suspend,
	.resume = ni_usb_driver_resume,
	.id_table = ni_usb_driver_device_table,
};

static int __init ni_usb_init_module(void)
{
	int i;
	int ret;

	pr_info("ni_usb_gpib driver loading\n");
	for (i = 0; i < MAX_NUM_NI_USB_INTERFACES; i++)
		ni_usb_driver_interfaces[i] = NULL;

	ret = usb_register(&ni_usb_bus_driver);
	if (ret) {
		pr_err("ni_usb_gpib: usb_register failed: error = %d\n", ret);
		return ret;
	}

	ret = gpib_register_driver(&ni_usb_gpib_interface, THIS_MODULE);
	if (ret) {
		pr_err("ni_usb_gpib: gpib_register_driver failed: error = %d\n", ret);
		return ret;
	}

	return 0;
}

static void __exit ni_usb_exit_module(void)
{
	pr_info("ni_usb_gpib driver unloading\n");
	gpib_unregister_driver(&ni_usb_gpib_interface);
	usb_deregister(&ni_usb_bus_driver);
}

module_init(ni_usb_init_module);
module_exit(ni_usb_exit_module);
