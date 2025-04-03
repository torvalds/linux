// SPDX-License-Identifier: GPL-2.0

/***************************************************************************
 *	driver for Agilent 82357A/B usb to gpib adapters		   *
 *    copyright		   : (C) 2004 by Frank Mori Hess		   *
 ***************************************************************************/

#define _GNU_SOURCE

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt
#define DRV_NAME KBUILD_MODNAME

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "agilent_82357a.h"
#include "gpibP.h"
#include "tms9914.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GPIB driver for Agilent 82357A/B usb adapters");

#define MAX_NUM_82357A_INTERFACES 128
static struct usb_interface *agilent_82357a_driver_interfaces[MAX_NUM_82357A_INTERFACES];
static DEFINE_MUTEX(agilent_82357a_hotplug_lock); // protect board insertion and removal

static unsigned int agilent_82357a_update_status(struct gpib_board *board,
						unsigned int clear_mask);

static int agilent_82357a_take_control_internal(struct gpib_board *board, int synchronous);

static void agilent_82357a_bulk_complete(struct urb *urb)
{
	struct agilent_82357a_urb_ctx *context = urb->context;

	up(&context->complete);
}

static void agilent_82357a_timeout_handler(struct timer_list *t)
{
	struct agilent_82357a_priv *a_priv = from_timer(a_priv, t, bulk_timer);
	struct agilent_82357a_urb_ctx *context = &a_priv->context;

	context->timed_out = 1;
	up(&context->complete);
}

static int agilent_82357a_send_bulk_msg(struct agilent_82357a_priv *a_priv, void *data,
					int data_length, int *actual_data_length,
					int timeout_msecs)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int out_pipe;
	struct agilent_82357a_urb_ctx *context = &a_priv->context;

	*actual_data_length = 0;
	retval = mutex_lock_interruptible(&a_priv->bulk_alloc_lock);
	if (retval)
		return retval;
	if (!a_priv->bus_interface) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -ENODEV;
	}
	if (a_priv->bulk_urb) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -EAGAIN;
	}
	a_priv->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!a_priv->bulk_urb) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -ENOMEM;
	}
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	out_pipe = usb_sndbulkpipe(usb_dev, a_priv->bulk_out_endpoint);
	sema_init(&context->complete, 0);
	context->timed_out = 0;
	usb_fill_bulk_urb(a_priv->bulk_urb, usb_dev, out_pipe, data, data_length,
			  &agilent_82357a_bulk_complete, context);

	if (timeout_msecs)
		mod_timer(&a_priv->bulk_timer, jiffies + msecs_to_jiffies(timeout_msecs));

	retval = usb_submit_urb(a_priv->bulk_urb, GFP_KERNEL);
	if (retval) {
		dev_err(&usb_dev->dev, "failed to submit bulk out urb, retval=%i\n", retval);
		mutex_unlock(&a_priv->bulk_alloc_lock);
		goto cleanup;
	}
	mutex_unlock(&a_priv->bulk_alloc_lock);
	if (down_interruptible(&context->complete)) {
		retval = -ERESTARTSYS;
		goto cleanup;
	}
	if (context->timed_out)	{
		retval = -ETIMEDOUT;
	} else {
		retval = a_priv->bulk_urb->status;
		*actual_data_length = a_priv->bulk_urb->actual_length;
	}
cleanup:
	if (timeout_msecs) {
		if (timer_pending(&a_priv->bulk_timer))
			del_timer_sync(&a_priv->bulk_timer);
	}
	mutex_lock(&a_priv->bulk_alloc_lock);
	if (a_priv->bulk_urb) {
		usb_kill_urb(a_priv->bulk_urb);
		usb_free_urb(a_priv->bulk_urb);
		a_priv->bulk_urb = NULL;
	}
	mutex_unlock(&a_priv->bulk_alloc_lock);
	return retval;
}

static int agilent_82357a_receive_bulk_msg(struct agilent_82357a_priv *a_priv, void *data,
					   int data_length, int *actual_data_length,
					   int timeout_msecs)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int in_pipe;
	struct agilent_82357a_urb_ctx *context = &a_priv->context;

	*actual_data_length = 0;
	retval = mutex_lock_interruptible(&a_priv->bulk_alloc_lock);
	if (retval)
		return retval;
	if (!a_priv->bus_interface) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -ENODEV;
	}
	if (a_priv->bulk_urb) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -EAGAIN;
	}
	a_priv->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!a_priv->bulk_urb) {
		mutex_unlock(&a_priv->bulk_alloc_lock);
		return -ENOMEM;
	}
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	in_pipe = usb_rcvbulkpipe(usb_dev, AGILENT_82357_BULK_IN_ENDPOINT);
	sema_init(&context->complete, 0);
	context->timed_out = 0;
	usb_fill_bulk_urb(a_priv->bulk_urb, usb_dev, in_pipe, data, data_length,
			  &agilent_82357a_bulk_complete, context);

	if (timeout_msecs)
		mod_timer(&a_priv->bulk_timer, jiffies + msecs_to_jiffies(timeout_msecs));

	retval = usb_submit_urb(a_priv->bulk_urb, GFP_KERNEL);
	if (retval) {
		dev_err(&usb_dev->dev, "failed to submit bulk in urb, retval=%i\n", retval);
		mutex_unlock(&a_priv->bulk_alloc_lock);
		goto cleanup;
	}
	mutex_unlock(&a_priv->bulk_alloc_lock);
	if (down_interruptible(&context->complete)) {
		retval = -ERESTARTSYS;
		goto cleanup;
	}
	if (context->timed_out)	{
		retval = -ETIMEDOUT;
		goto cleanup;
	}
	retval = a_priv->bulk_urb->status;
	*actual_data_length = a_priv->bulk_urb->actual_length;
cleanup:
	if (timeout_msecs)
		del_timer_sync(&a_priv->bulk_timer);

	mutex_lock(&a_priv->bulk_alloc_lock);
	if (a_priv->bulk_urb) {
		usb_kill_urb(a_priv->bulk_urb);
		usb_free_urb(a_priv->bulk_urb);
		a_priv->bulk_urb = NULL;
	}
	mutex_unlock(&a_priv->bulk_alloc_lock);
	return retval;
}

static int agilent_82357a_receive_control_msg(struct agilent_82357a_priv *a_priv, __u8 request,
					      __u8 requesttype, __u16 value,  __u16 index,
					      void *data, __u16 size, int timeout_msecs)
{
	struct usb_device *usb_dev;
	int retval;
	unsigned int in_pipe;

	retval = mutex_lock_interruptible(&a_priv->control_alloc_lock);
	if (retval)
		return retval;
	if (!a_priv->bus_interface) {
		mutex_unlock(&a_priv->control_alloc_lock);
		return -ENODEV;
	}
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	in_pipe = usb_rcvctrlpipe(usb_dev, AGILENT_82357_CONTROL_ENDPOINT);
	retval = usb_control_msg(usb_dev, in_pipe, request, requesttype, value, index, data,
				 size, timeout_msecs);
	mutex_unlock(&a_priv->control_alloc_lock);
	return retval;
}

static void agilent_82357a_dump_raw_block(const u8 *raw_data, int length)
{
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_NONE, 8, 1, raw_data, length, true);
}

static int agilent_82357a_write_registers(struct agilent_82357a_priv *a_priv,
					  const struct agilent_82357a_register_pairlet *writes,
					  int num_writes)
{
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	int retval;
	u8 *out_data, *in_data;
	int out_data_length, in_data_length;
	int bytes_written, bytes_read;
	int i = 0;
	int j;
	static const int bytes_per_write = 2;
	static const int header_length = 2;
	static const int max_writes = 31;

	if (num_writes > max_writes) {
		dev_err(&usb_dev->dev, "bug! num_writes=%i too large\n", num_writes);
		return -EIO;
	}
	out_data_length = num_writes * bytes_per_write + header_length;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;

	out_data[i++] = DATA_PIPE_CMD_WR_REGS;
	out_data[i++] = num_writes;
	for (j = 0; j < num_writes; j++)	{
		out_data[i++] = writes[j].address;
		out_data[i++] = writes[j].value;
	}

	retval = mutex_lock_interruptible(&a_priv->bulk_transfer_lock);
	if (retval) {
		kfree(out_data);
		return retval;
	}
	retval = agilent_82357a_send_bulk_msg(a_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval) {
		dev_err(&usb_dev->dev, "send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			retval, bytes_written, i);
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return retval;
	}
	in_data_length = 0x20;
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return -ENOMEM;
	}
	retval = agilent_82357a_receive_bulk_msg(a_priv, in_data, in_data_length,
						 &bytes_read, 1000);
	mutex_unlock(&a_priv->bulk_transfer_lock);

	if (retval) {
		dev_err(&usb_dev->dev, "receive_bulk_msg returned %i, bytes_read=%i\n",
			retval, bytes_read);
		agilent_82357a_dump_raw_block(in_data, bytes_read);
		kfree(in_data);
		return -EIO;
	}
	if (in_data[0] != (0xff & ~DATA_PIPE_CMD_WR_REGS)) {
		dev_err(&usb_dev->dev, "bulk command=0x%x != ~DATA_PIPE_CMD_WR_REGS\n", in_data[0]);
		return -EIO;
	}
	if (in_data[1])	{
		dev_err(&usb_dev->dev, "nonzero error code 0x%x in DATA_PIPE_CMD_WR_REGS response\n",
			in_data[1]);
		return -EIO;
	}
	kfree(in_data);
	return 0;
}

static int agilent_82357a_read_registers(struct agilent_82357a_priv *a_priv,
					 struct agilent_82357a_register_pairlet *reads,
					 int num_reads, int blocking)
{
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	int retval;
	u8 *out_data, *in_data;
	int out_data_length, in_data_length;
	int bytes_written, bytes_read;
	int i = 0;
	int j;
	static const int header_length = 2;
	static const int max_reads = 62;

	if (num_reads > max_reads) {
		dev_err(&usb_dev->dev, "bug! num_reads=%i too large\n", num_reads);
		return -EIO;
	}
	out_data_length = num_reads + header_length;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;

	out_data[i++] = DATA_PIPE_CMD_RD_REGS;
	out_data[i++] = num_reads;
	for (j = 0; j < num_reads; j++)
		out_data[i++] = reads[j].address;

	if (blocking) {
		retval = mutex_lock_interruptible(&a_priv->bulk_transfer_lock);
		if (retval) {
			kfree(out_data);
			return retval;
		}
	} else {
		retval = mutex_trylock(&a_priv->bulk_transfer_lock);
		if (retval == 0) {
			kfree(out_data);
			return -EAGAIN;
		}
	}
	retval = agilent_82357a_send_bulk_msg(a_priv, out_data, i, &bytes_written, 1000);
	kfree(out_data);
	if (retval) {
		dev_err(&usb_dev->dev, "send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			retval, bytes_written, i);
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return retval;
	}
	in_data_length = 0x20;
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return -ENOMEM;
	}
	retval = agilent_82357a_receive_bulk_msg(a_priv, in_data, in_data_length,
						 &bytes_read, 10000);
	mutex_unlock(&a_priv->bulk_transfer_lock);

	if (retval) {
		dev_err(&usb_dev->dev, "receive_bulk_msg returned %i, bytes_read=%i\n",
			retval, bytes_read);
		agilent_82357a_dump_raw_block(in_data, bytes_read);
		kfree(in_data);
		return -EIO;
	}
	i = 0;
	if (in_data[i++] != (0xff & ~DATA_PIPE_CMD_RD_REGS)) {
		dev_err(&usb_dev->dev, "bulk command=0x%x != ~DATA_PIPE_CMD_RD_REGS\n",	in_data[0]);
		return -EIO;
	}
	if (in_data[i++]) {
		dev_err(&usb_dev->dev, "nonzero error code 0x%x in DATA_PIPE_CMD_RD_REGS response\n",
			in_data[1]);
		return -EIO;
	}
	for (j = 0; j < num_reads; j++)
		reads[j].value = in_data[i++];
	kfree(in_data);
	return 0;
}

static int agilent_82357a_abort(struct agilent_82357a_priv *a_priv, int flush)
{
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	int retval = 0;
	int receive_control_retval;
	u16 wIndex = 0;
	u8 *status_data;
	static const unsigned int status_data_len = 2;

	status_data = kmalloc(status_data_len, GFP_KERNEL);
	if (!status_data)
		return -ENOMEM;

	if (flush)
		wIndex |= XA_FLUSH;
	receive_control_retval = agilent_82357a_receive_control_msg(a_priv,
								    agilent_82357a_control_request,
								    USB_DIR_IN | USB_TYPE_VENDOR |
								    USB_RECIP_DEVICE, XFER_ABORT,
								    wIndex, status_data,
								    status_data_len, 100);
	if (receive_control_retval < 0)	{
		dev_err(&usb_dev->dev, "82357a_receive_control_msg() returned %i\n",
			receive_control_retval);
		retval = -EIO;
		goto cleanup;
	}
	if (status_data[0] != (~XFER_ABORT & 0xff)) {
		dev_err(&usb_dev->dev, "major code=0x%x != ~XFER_ABORT\n", status_data[0]);
		retval = -EIO;
		goto cleanup;
	}
	switch (status_data[1])	{
	case UGP_SUCCESS:
		retval = 0;
		break;
	case UGP_ERR_FLUSHING:
		if (flush) {
			retval = 0;
			break;
		}
		fallthrough;
	case UGP_ERR_FLUSHING_ALREADY:
	default:
		dev_err(&usb_dev->dev, "abort returned error code=0x%x\n", status_data[1]);
		retval = -EIO;
		break;
	}

cleanup:
	kfree(status_data);
	return retval;
}

// interface functions
int agilent_82357a_command(struct gpib_board *board, uint8_t *buffer, size_t length,
			   size_t *bytes_written);

static int agilent_82357a_read(struct gpib_board *board, uint8_t *buffer, size_t length, int *end,
			       size_t *nbytes)
{
	int retval;
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	u8 *out_data, *in_data;
	int out_data_length, in_data_length;
	int bytes_written, bytes_read;
	int i = 0;
	u8 trailing_flags;
	unsigned long start_jiffies = jiffies;
	int msec_timeout;

	*nbytes = 0;
	*end = 0;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	out_data_length = 0x9;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = DATA_PIPE_CMD_READ;
	out_data[i++] = 0;	//primary address when ARF_NO_ADDR is not set
	out_data[i++] = 0;	//secondary address when ARF_NO_ADDR is not set
	out_data[i] = ARF_NO_ADDRESS | ARF_END_ON_EOI;
	if (a_priv->eos_mode & REOS)
		out_data[i] |= ARF_END_ON_EOS_CHAR;
	++i;
	out_data[i++] = length & 0xff;
	out_data[i++] = (length >> 8) & 0xff;
	out_data[i++] = (length >> 16) & 0xff;
	out_data[i++] = (length >> 24) & 0xff;
	out_data[i++] = a_priv->eos_char;
	msec_timeout = (board->usec_timeout + 999) / 1000;
	retval = mutex_lock_interruptible(&a_priv->bulk_transfer_lock);
	if (retval) {
		kfree(out_data);
		return retval;
	}
	retval = agilent_82357a_send_bulk_msg(a_priv, out_data, i, &bytes_written, msec_timeout);
	kfree(out_data);
	if (retval || bytes_written != i) {
		dev_err(&usb_dev->dev, "send_bulk_msg returned %i, bytes_written=%i, i=%i\n",
			retval, bytes_written, i);
		mutex_unlock(&a_priv->bulk_transfer_lock);
		if (retval < 0)
			return retval;
		return -EIO;
	}
	in_data_length = length + 1;
	in_data = kmalloc(in_data_length, GFP_KERNEL);
	if (!in_data) {
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return -ENOMEM;
	}
	if (board->usec_timeout != 0)
		msec_timeout -= jiffies_to_msecs(jiffies - start_jiffies) - 1;
	if (msec_timeout >= 0) {
		retval = agilent_82357a_receive_bulk_msg(a_priv, in_data, in_data_length,
							 &bytes_read, msec_timeout);
	} else {
		retval = -ETIMEDOUT;
		bytes_read = 0;
	}
	if (retval == -ETIMEDOUT) {
		int extra_bytes_read;
		int extra_bytes_retval;

		agilent_82357a_abort(a_priv, 1);
		extra_bytes_retval = agilent_82357a_receive_bulk_msg(a_priv, in_data + bytes_read,
								     in_data_length - bytes_read,
								     &extra_bytes_read, 100);
		bytes_read += extra_bytes_read;
		if (extra_bytes_retval)	{
			dev_err(&usb_dev->dev, "extra_bytes_retval=%i, bytes_read=%i\n",
				extra_bytes_retval, bytes_read);
			agilent_82357a_abort(a_priv, 0);
		}
	} else if (retval) {
		dev_err(&usb_dev->dev, "receive_bulk_msg returned %i, bytes_read=%i\n",
			retval, bytes_read);
		agilent_82357a_abort(a_priv, 0);
	}
	mutex_unlock(&a_priv->bulk_transfer_lock);
	if (bytes_read > length + 1) {
		bytes_read = length + 1;
		dev_warn(&usb_dev->dev, "bytes_read > length? truncating");
	}

	if (bytes_read >= 1) {
		memcpy(buffer, in_data, bytes_read - 1);
		trailing_flags = in_data[bytes_read - 1];
		*nbytes = bytes_read - 1;
		if (trailing_flags & (ATRF_EOI | ATRF_EOS))
			*end = 1;
	}
	kfree(in_data);

	/* Fix for a bug in 9914A that does not return the contents of ADSR
	 *  when the board is in listener active state and ATN is not asserted.
	 *  Set ATN here to obtain a valid board level ibsta
	 */
	agilent_82357a_take_control_internal(board, 0);

	//FIXME check trailing flags for error
	return retval;
}

static ssize_t agilent_82357a_generic_write(struct gpib_board *board,
					    uint8_t *buffer, size_t length,
					    int send_commands, int send_eoi,
					    size_t *bytes_written)
{
	int retval;
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	u8 *out_data = NULL;
	u8 *status_data = NULL;
	int out_data_length;
	int raw_bytes_written;
	int i = 0, j;
	int msec_timeout;
	unsigned short bsr, adsr;
	struct agilent_82357a_register_pairlet read_reg;

	*bytes_written = 0;
	if (!a_priv->bus_interface)
		return -ENODEV;

	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	out_data_length = length + 0x8;
	out_data = kmalloc(out_data_length, GFP_KERNEL);
	if (!out_data)
		return -ENOMEM;
	out_data[i++] = DATA_PIPE_CMD_WRITE;
	out_data[i++] = 0; // primary address when AWF_NO_ADDRESS is not set
	out_data[i++] = 0; // secondary address when AWF_NO_ADDRESS is not set
	out_data[i] = AWF_NO_ADDRESS | AWF_NO_FAST_TALKER_FIRST_BYTE;
	if (send_commands)
		out_data[i] |= AWF_ATN | AWF_NO_FAST_TALKER;
	if (send_eoi)
		out_data[i] |= AWF_SEND_EOI;
	++i;
	out_data[i++] = length & 0xff;
	out_data[i++] = (length >> 8) & 0xff;
	out_data[i++] = (length >> 16) & 0xff;
	out_data[i++] = (length >> 24) & 0xff;
	for (j = 0; j < length; j++)
		out_data[i++] = buffer[j];

	clear_bit(AIF_WRITE_COMPLETE_BN, &a_priv->interrupt_flags);

	msec_timeout = (board->usec_timeout + 999) / 1000;
	retval = mutex_lock_interruptible(&a_priv->bulk_transfer_lock);
	if (retval) {
		kfree(out_data);
		return retval;
	}
	retval = agilent_82357a_send_bulk_msg(a_priv, out_data, i, &raw_bytes_written,
					      msec_timeout);
	kfree(out_data);
	if (retval || raw_bytes_written != i) {
		agilent_82357a_abort(a_priv, 0);
		dev_err(&usb_dev->dev, "send_bulk_msg returned %i, raw_bytes_written=%i, i=%i\n",
			retval, raw_bytes_written, i);
		mutex_unlock(&a_priv->bulk_transfer_lock);
		if (retval < 0)
			return retval;
		return -EIO;
	}

	retval = wait_event_interruptible(board->wait,
					  test_bit(AIF_WRITE_COMPLETE_BN,
						   &a_priv->interrupt_flags) ||
					  test_bit(TIMO_NUM, &board->status));
	if (retval) {
		dev_dbg(&usb_dev->dev, "wait write complete interrupted\n");
		agilent_82357a_abort(a_priv, 0);
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return -ERESTARTSYS;
	}

	if (test_bit(AIF_WRITE_COMPLETE_BN, &a_priv->interrupt_flags) == 0) {
		dev_dbg(&usb_dev->dev, "write timed out ibs %i, tmo %i\n",
			test_bit(TIMO_NUM, &board->status), msec_timeout);

		agilent_82357a_abort(a_priv, 0);

		mutex_unlock(&a_priv->bulk_transfer_lock);

		read_reg.address = BSR;
		retval = agilent_82357a_read_registers(a_priv, &read_reg, 1, 1);
		if (retval) {
			dev_err(&usb_dev->dev, "read_registers() returned error\n");
			return -ETIMEDOUT;
		}

		bsr = read_reg.value;
		dev_dbg(&usb_dev->dev, "write aborted bsr 0x%x\n", bsr);

		if (send_commands) {/* check for no listeners */
			if ((bsr & BSR_ATN_BIT) && !(bsr & (BSR_NDAC_BIT | BSR_NRFD_BIT))) {
				dev_dbg(&usb_dev->dev, "No listener on command\n");
				clear_bit(TIMO_NUM, &board->status);
				return -ENOTCONN; // no listener on bus
			}
		} else {
			read_reg.address = ADSR;
			retval = agilent_82357a_read_registers(a_priv, &read_reg, 1, 1);
			if (retval) {
				dev_err(&usb_dev->dev, "read_registers() returned error\n");
				return -ETIMEDOUT;
			}
			adsr = read_reg.value;
			if ((adsr & HR_TA) && !(bsr & (BSR_NDAC_BIT | BSR_NRFD_BIT))) {
				dev_dbg(&usb_dev->dev, "No listener on write\n");
				clear_bit(TIMO_NUM, &board->status);
				return -ECOMM;
			}
		}

		return -ETIMEDOUT;
	}

	status_data = kmalloc(STATUS_DATA_LEN, GFP_KERNEL);
	if (!status_data) {
		mutex_unlock(&a_priv->bulk_transfer_lock);
		return -ENOMEM;
	}

	retval = agilent_82357a_receive_control_msg(a_priv, agilent_82357a_control_request,
						    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
						    XFER_STATUS, 0, status_data, STATUS_DATA_LEN,
						    100);
	mutex_unlock(&a_priv->bulk_transfer_lock);
	if (retval < 0)	{
		dev_err(&usb_dev->dev, "receive_control_msg() returned %i\n", retval);
		kfree(status_data);
		return -EIO;
	}
	*bytes_written	= (u32)status_data[2];
	*bytes_written |= (u32)status_data[3] << 8;
	*bytes_written |= (u32)status_data[4] << 16;
	*bytes_written |= (u32)status_data[5] << 24;

	kfree(status_data);
	return 0;
}

static int agilent_82357a_write(struct gpib_board *board, uint8_t *buffer,
				size_t length, int send_eoi, size_t *bytes_written)
{
	return agilent_82357a_generic_write(board, buffer, length, 0, send_eoi, bytes_written);
}

int agilent_82357a_command(struct gpib_board *board, uint8_t *buffer, size_t length,
			   size_t *bytes_written)
{
	return agilent_82357a_generic_write(board, buffer, length, 1, 0, bytes_written);
}

int agilent_82357a_take_control_internal(struct gpib_board *board, int synchronous)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	struct agilent_82357a_register_pairlet write;
	int retval;

	write.address = AUXCR;
	if (synchronous)
		write.value = AUX_TCS;
	else
		write.value = AUX_TCA;
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");

	return retval;
}

static int agilent_82357a_take_control(struct gpib_board *board, int synchronous)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	const int timeout = 10;
	int i;

	if (!a_priv->bus_interface)
		return -ENODEV;

/* It looks like the 9914 does not handle tcs properly.
 *  See comment above tms9914_take_control_workaround() in
 *  drivers/gpib/tms9914/tms9914_aux.c
 */
	if (synchronous)
		return -ETIMEDOUT;

	agilent_82357a_take_control_internal(board, synchronous);
	// busy wait until ATN is asserted
	for (i = 0; i < timeout; ++i) {
		agilent_82357a_update_status(board, 0);
		if (test_bit(ATN_NUM, &board->status))
			break;
		udelay(1);
	}
	if (i == timeout)
		return -ETIMEDOUT;
	return 0;
}

static int agilent_82357a_go_to_standby(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet write;
	int retval;

	if (!a_priv->bus_interface)
		return -ENODEV;

	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	write.address = AUXCR;
	write.value = AUX_GTS;
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
	return 0;
}

//FIXME should change prototype to return int
static void agilent_82357a_request_system_control(struct gpib_board *board,
						  int request_control)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet writes[2];
	int retval;
	int i = 0;

	if (!a_priv->bus_interface)
		return; // -ENODEV;

	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	/* 82357B needs bit to be set in 9914 AUXCR register */
	writes[i].address = AUXCR;
	if (request_control) {
		writes[i].value = AUX_RQC;
		a_priv->hw_control_bits |= SYSTEM_CONTROLLER;
	} else {
		writes[i].value = AUX_RLC;
		a_priv->is_cic = 0;
		a_priv->hw_control_bits &= ~SYSTEM_CONTROLLER;
	}
	++i;
	writes[i].address = HW_CONTROL;
	writes[i].value = a_priv->hw_control_bits;
	++i;
	retval = agilent_82357a_write_registers(a_priv, writes, i);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
	return;// retval;
}

static void agilent_82357a_interface_clear(struct gpib_board *board, int assert)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet write;
	int retval;

	if (!a_priv->bus_interface)
		return; // -ENODEV;

	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	write.address = AUXCR;
	write.value = AUX_SIC;
	if (assert) {
		write.value |= AUX_CS;
		a_priv->is_cic = 1;
	}
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
}

static void agilent_82357a_remote_enable(struct gpib_board *board, int enable)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet write;
	int retval;

	if (!a_priv->bus_interface)
		return; //-ENODEV;

	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	write.address = AUXCR;
	write.value = AUX_SRE;
	if (enable)
		write.value |= AUX_CS;
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
	a_priv->ren_state = enable;
	return;// 0;
}

static int agilent_82357a_enable_eos(struct gpib_board *board, uint8_t eos_byte,
				     int compare_8_bits)
{
	struct agilent_82357a_priv *a_priv = board->private_data;

	if (!a_priv->bus_interface)
		return -ENODEV;
	if (compare_8_bits == 0)
		return -EOPNOTSUPP;

	a_priv->eos_char = eos_byte;
	a_priv->eos_mode = REOS | BIN;
	return 0;
}

static void agilent_82357a_disable_eos(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;

	a_priv->eos_mode &= ~REOS;
}

static unsigned int agilent_82357a_update_status(struct gpib_board *board,
						 unsigned int clear_mask)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet address_status, bus_status;
	int retval;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	board->status &= ~clear_mask;
	if (a_priv->is_cic)
		set_bit(CIC_NUM, &board->status);
	else
		clear_bit(CIC_NUM, &board->status);
	address_status.address = ADSR;
	retval = agilent_82357a_read_registers(a_priv, &address_status, 1, 0);
	if (retval) {
		if (retval != -EAGAIN)
			dev_err(&usb_dev->dev, "read_registers() returned error\n");
		return board->status;
	}
	// check for remote/local
	if (address_status.value & HR_REM)
		set_bit(REM_NUM, &board->status);
	else
		clear_bit(REM_NUM, &board->status);
	// check for lockout
	if (address_status.value & HR_LLO)
		set_bit(LOK_NUM, &board->status);
	else
		clear_bit(LOK_NUM, &board->status);
	// check for ATN
	if (address_status.value & HR_ATN)
		set_bit(ATN_NUM, &board->status);
	else
		clear_bit(ATN_NUM, &board->status);
	// check for talker/listener addressed
	if (address_status.value & HR_TA)
		set_bit(TACS_NUM, &board->status);
	else
		clear_bit(TACS_NUM, &board->status);
	if (address_status.value & HR_LA)
		set_bit(LACS_NUM, &board->status);
	else
		clear_bit(LACS_NUM, &board->status);

	bus_status.address = BSR;
	retval = agilent_82357a_read_registers(a_priv, &bus_status, 1, 0);
	if (retval) {
		if (retval != -EAGAIN)
			dev_err(&usb_dev->dev, "read_registers() returned error\n");
		return board->status;
	}
	if (bus_status.value & BSR_SRQ_BIT)
		set_bit(SRQI_NUM, &board->status);
	else
		clear_bit(SRQI_NUM, &board->status);

	return board->status;
}

static int agilent_82357a_primary_address(struct gpib_board *board, unsigned int address)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	struct agilent_82357a_register_pairlet write;
	int retval;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	// put primary address in address0
	write.address = ADR;
	write.value = address & ADDRESS_MASK;
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return retval;
	}
	return retval;
}

static int agilent_82357a_secondary_address(struct gpib_board *board,
					    unsigned int address, int enable)
{
	if (enable)
		return	-EOPNOTSUPP;
	return 0;
}

static int agilent_82357a_parallel_poll(struct gpib_board *board, uint8_t *result)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet writes[2];
	struct agilent_82357a_register_pairlet read;
	int retval;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	// execute parallel poll
	writes[0].address = AUXCR;
	writes[0].value = AUX_CS | AUX_RPP;
	writes[1].address = HW_CONTROL;
	writes[1].value = a_priv->hw_control_bits & ~NOT_PARALLEL_POLL;
	retval = agilent_82357a_write_registers(a_priv, writes, 2);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return retval;
	}
	udelay(2);	//silly, since usb write will take way longer
	read.address = CPTR;
	retval = agilent_82357a_read_registers(a_priv, &read, 1, 1);
	if (retval) {
		dev_err(&usb_dev->dev, "read_registers() returned error\n");
		return retval;
	}
	*result = read.value;
	// clear parallel poll state
	writes[0].address = HW_CONTROL;
	writes[0].value = a_priv->hw_control_bits | NOT_PARALLEL_POLL;
	writes[1].address = AUXCR;
	writes[1].value = AUX_RPP;
	retval = agilent_82357a_write_registers(a_priv, writes, 2);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return retval;
	}
	return 0;
}

static void agilent_82357a_parallel_poll_configure(struct gpib_board *board, uint8_t config)
{
	//board can only be system controller
	return;// 0;
}

static void agilent_82357a_parallel_poll_response(struct gpib_board *board, int ist)
{
	//board can only be system controller
	return;// 0;
}

static void agilent_82357a_serial_poll_response(struct gpib_board *board, uint8_t status)
{
	//board can only be system controller
	return;// 0;
}

static uint8_t agilent_82357a_serial_poll_status(struct gpib_board *board)
{
	//board can only be system controller
	return 0;
}

static void agilent_82357a_return_to_local(struct gpib_board *board)
{
	//board can only be system controller
	return;// 0;
}

static int agilent_82357a_line_status(const struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet bus_status;
	int retval;
	int status = VALID_ALL;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	bus_status.address = BSR;
	retval = agilent_82357a_read_registers(a_priv, &bus_status, 1, 0);
	if (retval) {
		if (retval != -EAGAIN)
			dev_err(&usb_dev->dev, "read_registers() returned error\n");
		return retval;
	}
	if (bus_status.value & BSR_REN_BIT)
		status |= BUS_REN;
	if (bus_status.value & BSR_IFC_BIT)
		status |= BUS_IFC;
	if (bus_status.value & BSR_SRQ_BIT)
		status |= BUS_SRQ;
	if (bus_status.value & BSR_EOI_BIT)
		status |= BUS_EOI;
	if (bus_status.value & BSR_NRFD_BIT)
		status |= BUS_NRFD;
	if (bus_status.value & BSR_NDAC_BIT)
		status |= BUS_NDAC;
	if (bus_status.value & BSR_DAV_BIT)
		status |= BUS_DAV;
	if (bus_status.value & BSR_ATN_BIT)
		status |= BUS_ATN;
	return status;
}

static unsigned short nanosec_to_fast_talker_bits(unsigned int *nanosec)
{
	static const int nanosec_per_bit = 21;
	static const int max_value = 0x72;
	static const int min_value = 0x11;
	unsigned short bits;

	bits = (*nanosec + nanosec_per_bit / 2) / nanosec_per_bit;
	if (bits < min_value)
		bits = min_value;
	if (bits > max_value)
		bits = max_value;
	*nanosec = bits * nanosec_per_bit;
	return bits;
}

static int agilent_82357a_t1_delay(struct gpib_board *board, unsigned int nanosec)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	struct agilent_82357a_register_pairlet write;
	int retval;

	if (!a_priv->bus_interface)
		return -ENODEV;
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	write.address = FAST_TALKER_T1;
	write.value = nanosec_to_fast_talker_bits(&nanosec);
	retval = agilent_82357a_write_registers(a_priv, &write, 1);
	if (retval)
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
	return nanosec;
}

static void agilent_82357a_interrupt_complete(struct urb *urb)
{
	struct gpib_board *board = urb->context;
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	int retval;
	u8 *transfer_buffer = urb->transfer_buffer;
	unsigned long interrupt_flags;

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
		retval = usb_submit_urb(a_priv->interrupt_urb, GFP_ATOMIC);
		if (retval)
			dev_err(&usb_dev->dev, "failed to resubmit interrupt urb\n");
		return;
	}

	interrupt_flags = transfer_buffer[0];
	if (test_bit(AIF_READ_COMPLETE_BN, &interrupt_flags))
		set_bit(AIF_READ_COMPLETE_BN, &a_priv->interrupt_flags);
	if (test_bit(AIF_WRITE_COMPLETE_BN, &interrupt_flags))
		set_bit(AIF_WRITE_COMPLETE_BN, &a_priv->interrupt_flags);
	if (test_bit(AIF_SRQ_BN, &interrupt_flags))
		set_bit(SRQI_NUM, &board->status);

	wake_up_interruptible(&board->wait);

	retval = usb_submit_urb(a_priv->interrupt_urb, GFP_ATOMIC);
	if (retval)
		dev_err(&usb_dev->dev, "failed to resubmit interrupt urb\n");
}

static int agilent_82357a_setup_urbs(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev;
	int int_pipe;
	int retval;

	retval = mutex_lock_interruptible(&a_priv->interrupt_alloc_lock);
	if (retval)
		return retval;
	if (!a_priv->bus_interface) {
		retval = -ENODEV;
		goto setup_exit;
	}

	a_priv->interrupt_buffer = kmalloc(INTERRUPT_BUF_LEN, GFP_KERNEL);
	if (!a_priv->interrupt_buffer) {
		retval = -ENOMEM;
		goto setup_exit;
	}
	a_priv->interrupt_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!a_priv->interrupt_urb) {
		retval = -ENOMEM;
		goto setup_exit;
	}
	usb_dev = interface_to_usbdev(a_priv->bus_interface);
	int_pipe = usb_rcvintpipe(usb_dev, a_priv->interrupt_in_endpoint);
	usb_fill_int_urb(a_priv->interrupt_urb, usb_dev, int_pipe, a_priv->interrupt_buffer,
			 INTERRUPT_BUF_LEN, &agilent_82357a_interrupt_complete, board, 1);
	retval = usb_submit_urb(a_priv->interrupt_urb, GFP_KERNEL);
	if (retval) {
		usb_free_urb(a_priv->interrupt_urb);
		a_priv->interrupt_urb = NULL;
		dev_err(&usb_dev->dev, "failed to submit first interrupt urb, retval=%i\n", retval);
		goto setup_exit;
	}
	mutex_unlock(&a_priv->interrupt_alloc_lock);
	return 0;

setup_exit:
	kfree(a_priv->interrupt_buffer);
	mutex_unlock(&a_priv->interrupt_alloc_lock);
	return retval;
}

static void agilent_82357a_cleanup_urbs(struct agilent_82357a_priv *a_priv)
{
	if (a_priv && a_priv->bus_interface) {
		if (a_priv->interrupt_urb)
			usb_kill_urb(a_priv->interrupt_urb);
		if (a_priv->bulk_urb)
			usb_kill_urb(a_priv->bulk_urb);
	}
};

static void agilent_82357a_release_urbs(struct agilent_82357a_priv *a_priv)
{
	if (a_priv) {
		usb_free_urb(a_priv->interrupt_urb);
		a_priv->interrupt_urb = NULL;
		kfree(a_priv->interrupt_buffer);
	}
}

static int agilent_82357a_allocate_private(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv;

	board->private_data = kzalloc(sizeof(struct agilent_82357a_priv), GFP_KERNEL);
	if (!board->private_data)
		return -ENOMEM;
	a_priv = board->private_data;
	mutex_init(&a_priv->bulk_transfer_lock);
	mutex_init(&a_priv->bulk_alloc_lock);
	mutex_init(&a_priv->control_alloc_lock);
	mutex_init(&a_priv->interrupt_alloc_lock);
	return 0;
}

static void agilent_82357a_free_private(struct gpib_board *board)
{
	kfree(board->private_data);
	board->private_data = NULL;
}

#define INIT_NUM_REG_WRITES 18
static int agilent_82357a_init(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	struct agilent_82357a_register_pairlet hw_control;
	struct agilent_82357a_register_pairlet writes[INIT_NUM_REG_WRITES];
	int retval;
	unsigned int nanosec;

	writes[0].address = LED_CONTROL;
	writes[0].value = FAIL_LED_ON;
	writes[1].address = RESET_TO_POWERUP;
	writes[1].value = RESET_SPACEBALL;
	retval = agilent_82357a_write_registers(a_priv, writes, 2);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return -EIO;
	}
	set_current_state(TASK_INTERRUPTIBLE);
	if (schedule_timeout(usec_to_jiffies(2000)))
		return -ERESTARTSYS;
	writes[0].address = AUXCR;
	writes[0].value = AUX_NBAF;
	writes[1].address = AUXCR;
	writes[1].value = AUX_HLDE;
	writes[2].address = AUXCR;
	writes[2].value = AUX_TON;
	writes[3].address = AUXCR;
	writes[3].value = AUX_LON;
	writes[4].address = AUXCR;
	writes[4].value = AUX_RSV2;
	writes[5].address = AUXCR;
	writes[5].value = AUX_INVAL;
	writes[6].address = AUXCR;
	writes[6].value = AUX_RPP;
	writes[7].address = AUXCR;
	writes[7].value = AUX_STDL;
	writes[8].address = AUXCR;
	writes[8].value = AUX_VSTDL;
	writes[9].address = FAST_TALKER_T1;
	nanosec = board->t1_nano_sec;
	writes[9].value = nanosec_to_fast_talker_bits(&nanosec);
	board->t1_nano_sec = nanosec;
	writes[10].address = ADR;
	writes[10].value = board->pad & ADDRESS_MASK;
	writes[11].address = PPR;
	writes[11].value = 0;
	writes[12].address = SPMR;
	writes[12].value = 0;
	writes[13].address = PROTOCOL_CONTROL;
	writes[13].value = WRITE_COMPLETE_INTERRUPT_EN;
	writes[14].address = IMR0;
	writes[14].value = HR_BOIE | HR_BIIE;
	writes[15].address = IMR1;
	writes[15].value = HR_SRQIE;
	// turn off reset state
	writes[16].address = AUXCR;
	writes[16].value = AUX_CHIP_RESET;
	writes[17].address = LED_CONTROL;
	writes[17].value = FIRMWARE_LED_CONTROL;
	retval = agilent_82357a_write_registers(a_priv, writes, INIT_NUM_REG_WRITES);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return -EIO;
	}
	hw_control.address = HW_CONTROL;
	retval = agilent_82357a_read_registers(a_priv, &hw_control, 1, 1);
	if (retval) {
		dev_err(&usb_dev->dev, "read_registers() returned error\n");
		return -EIO;
	}
	a_priv->hw_control_bits = (hw_control.value & ~0x7) | NOT_TI_RESET | NOT_PARALLEL_POLL;

	return 0;
}

static inline int agilent_82357a_device_match(struct usb_interface *interface,
					      const gpib_board_config_t *config)
{
	struct usb_device * const usbdev = interface_to_usbdev(interface);

	if (gpib_match_device_path(&interface->dev, config->device_path) == 0)
		return 0;
	if (config->serial_number &&
	    strcmp(usbdev->serial, config->serial_number) != 0)
		return 0;

	return 1;
}

static int agilent_82357a_attach(struct gpib_board *board, const gpib_board_config_t *config)
{
	int retval;
	int i;
	unsigned int product_id;
	struct agilent_82357a_priv *a_priv;
	struct usb_device *usb_dev;

	if (mutex_lock_interruptible(&agilent_82357a_hotplug_lock))
		return -ERESTARTSYS;

	retval = agilent_82357a_allocate_private(board);
	if (retval < 0) {
		mutex_unlock(&agilent_82357a_hotplug_lock);
		return retval;
	}
	a_priv = board->private_data;
	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i) {
		if (agilent_82357a_driver_interfaces[i] &&
		    !usb_get_intfdata(agilent_82357a_driver_interfaces[i]) &&
		    agilent_82357a_device_match(agilent_82357a_driver_interfaces[i], config)) {
			a_priv->bus_interface = agilent_82357a_driver_interfaces[i];
			usb_set_intfdata(agilent_82357a_driver_interfaces[i], board);
			usb_dev = interface_to_usbdev(a_priv->bus_interface);
			break;
		}
	}
	if (i == MAX_NUM_82357A_INTERFACES) {
		dev_err(board->gpib_dev,
			"No supported adapters found, have you loaded its firmware?\n");
		retval = -ENODEV;
		goto attach_fail;
	}
	product_id = le16_to_cpu(interface_to_usbdev(a_priv->bus_interface)->descriptor.idProduct);
	switch (product_id) {
	case USB_DEVICE_ID_AGILENT_82357A:
		a_priv->bulk_out_endpoint = AGILENT_82357A_BULK_OUT_ENDPOINT;
		a_priv->interrupt_in_endpoint = AGILENT_82357A_INTERRUPT_IN_ENDPOINT;
		break;
	case USB_DEVICE_ID_AGILENT_82357B:
		a_priv->bulk_out_endpoint = AGILENT_82357B_BULK_OUT_ENDPOINT;
		a_priv->interrupt_in_endpoint = AGILENT_82357B_INTERRUPT_IN_ENDPOINT;
		break;
	default:
		dev_err(&usb_dev->dev, "bug, unhandled product_id in switch?\n");
		retval = -EIO;
		goto attach_fail;
	}

	retval = agilent_82357a_setup_urbs(board);
	if (retval < 0)
		goto attach_fail;

	timer_setup(&a_priv->bulk_timer, agilent_82357a_timeout_handler, 0);

	board->t1_nano_sec = 800;

	retval = agilent_82357a_init(board);

	if (retval < 0)	{
		agilent_82357a_cleanup_urbs(a_priv);
		agilent_82357a_release_urbs(a_priv);
		goto attach_fail;
	}

	dev_info(&usb_dev->dev, "bus %d dev num %d attached to gpib%d, interface %i\n",
		 usb_dev->bus->busnum, usb_dev->devnum, board->minor, i);
	mutex_unlock(&agilent_82357a_hotplug_lock);
	return retval;

attach_fail:
	agilent_82357a_free_private(board);
	mutex_unlock(&agilent_82357a_hotplug_lock);
	return retval;
}

static int agilent_82357a_go_idle(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv = board->private_data;
	struct usb_device *usb_dev = interface_to_usbdev(a_priv->bus_interface);
	struct agilent_82357a_register_pairlet writes[0x20];
	int retval;

	// turn on tms9914 reset state
	writes[0].address = AUXCR;
	writes[0].value = AUX_CS | AUX_CHIP_RESET;
	a_priv->hw_control_bits &= ~NOT_TI_RESET;
	writes[1].address = HW_CONTROL;
	writes[1].value = a_priv->hw_control_bits;
	writes[2].address = PROTOCOL_CONTROL;
	writes[2].value = 0;
	writes[3].address = IMR0;
	writes[3].value = 0;
	writes[4].address = IMR1;
	writes[4].value = 0;
	writes[5].address = LED_CONTROL;
	writes[5].value = 0;
	retval = agilent_82357a_write_registers(a_priv, writes, 6);
	if (retval) {
		dev_err(&usb_dev->dev, "write_registers() returned error\n");
		return -EIO;
	}
	return 0;
}

static void agilent_82357a_detach(struct gpib_board *board)
{
	struct agilent_82357a_priv *a_priv;

	mutex_lock(&agilent_82357a_hotplug_lock);

	a_priv = board->private_data;
	if (a_priv) {
		if (a_priv->bus_interface) {
			agilent_82357a_go_idle(board);
			usb_set_intfdata(a_priv->bus_interface, NULL);
		}
		mutex_lock(&a_priv->control_alloc_lock);
		mutex_lock(&a_priv->bulk_alloc_lock);
		mutex_lock(&a_priv->interrupt_alloc_lock);
		agilent_82357a_cleanup_urbs(a_priv);
		agilent_82357a_release_urbs(a_priv);
		agilent_82357a_free_private(board);
	}
	mutex_unlock(&agilent_82357a_hotplug_lock);
}

static gpib_interface_t agilent_82357a_gpib_interface = {
	.name = "agilent_82357a",
	.attach = agilent_82357a_attach,
	.detach = agilent_82357a_detach,
	.read = agilent_82357a_read,
	.write = agilent_82357a_write,
	.command = agilent_82357a_command,
	.take_control = agilent_82357a_take_control,
	.go_to_standby = agilent_82357a_go_to_standby,
	.request_system_control = agilent_82357a_request_system_control,
	.interface_clear = agilent_82357a_interface_clear,
	.remote_enable = agilent_82357a_remote_enable,
	.enable_eos = agilent_82357a_enable_eos,
	.disable_eos = agilent_82357a_disable_eos,
	.parallel_poll = agilent_82357a_parallel_poll,
	.parallel_poll_configure = agilent_82357a_parallel_poll_configure,
	.parallel_poll_response = agilent_82357a_parallel_poll_response,
	.local_parallel_poll_mode = NULL, // XXX
	.line_status = agilent_82357a_line_status,
	.update_status = agilent_82357a_update_status,
	.primary_address = agilent_82357a_primary_address,
	.secondary_address = agilent_82357a_secondary_address,
	.serial_poll_response = agilent_82357a_serial_poll_response,
	.serial_poll_status = agilent_82357a_serial_poll_status,
	.t1_delay = agilent_82357a_t1_delay,
	.return_to_local = agilent_82357a_return_to_local,
	.no_7_bit_eos = 1,
	.skip_check_for_command_acceptors = 1
};

// Table with the USB-devices: just now only testing IDs
static struct usb_device_id agilent_82357a_driver_device_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_AGILENT, USB_DEVICE_ID_AGILENT_82357A)},
	{USB_DEVICE(USB_VENDOR_ID_AGILENT, USB_DEVICE_ID_AGILENT_82357B)},
	{} /* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, agilent_82357a_driver_device_table);

static int agilent_82357a_driver_probe(struct usb_interface *interface,
				       const struct usb_device_id *id)
{
	int i;
	char *path;
	static const int path_length = 1024;
	struct usb_device *usb_dev;

	if (mutex_lock_interruptible(&agilent_82357a_hotplug_lock))
		return -ERESTARTSYS;
	usb_dev = usb_get_dev(interface_to_usbdev(interface));
	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i) {
		if (!agilent_82357a_driver_interfaces[i]) {
			agilent_82357a_driver_interfaces[i] = interface;
			usb_set_intfdata(interface, NULL);
			dev_dbg(&usb_dev->dev, "set bus interface %i to address 0x%p\n",
				i, interface);
			break;
		}
	}
	if (i == MAX_NUM_82357A_INTERFACES) {
		usb_put_dev(usb_dev);
		mutex_unlock(&agilent_82357a_hotplug_lock);
		dev_err(&usb_dev->dev, "out of space in agilent_82357a_driver_interfaces[]\n");
		return -1;
	}
	path = kmalloc(path_length, GFP_KERNEL);
	if (!path) {
		usb_put_dev(usb_dev);
		mutex_unlock(&agilent_82357a_hotplug_lock);
		return -ENOMEM;
	}
	usb_make_path(usb_dev, path, path_length);
	dev_info(&usb_dev->dev, "probe succeeded for path: %s\n", path);
	kfree(path);
	mutex_unlock(&agilent_82357a_hotplug_lock);
	return 0;
}

static void agilent_82357a_driver_disconnect(struct usb_interface *interface)
{
	int i;
	struct usb_device *usb_dev = interface_to_usbdev(interface);

	mutex_lock(&agilent_82357a_hotplug_lock);

	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i) {
		if (agilent_82357a_driver_interfaces[i] == interface) {
			struct gpib_board *board = usb_get_intfdata(interface);

			if (board) {
				struct agilent_82357a_priv *a_priv = board->private_data;

				if (a_priv) {
					mutex_lock(&a_priv->control_alloc_lock);
					mutex_lock(&a_priv->bulk_alloc_lock);
					mutex_lock(&a_priv->interrupt_alloc_lock);
					agilent_82357a_cleanup_urbs(a_priv);
					a_priv->bus_interface = NULL;
					mutex_unlock(&a_priv->interrupt_alloc_lock);
					mutex_unlock(&a_priv->bulk_alloc_lock);
					mutex_unlock(&a_priv->control_alloc_lock);
				}
			}
			agilent_82357a_driver_interfaces[i] = NULL;
			break;
		}
	}
	if (i == MAX_NUM_82357A_INTERFACES)
		dev_err(&usb_dev->dev, "unable to find interface - bug?\n");
	usb_put_dev(usb_dev);

	mutex_unlock(&agilent_82357a_hotplug_lock);
}

static int agilent_82357a_driver_suspend(struct usb_interface *interface, pm_message_t message)
{
	int i, retval;
	struct usb_device *usb_dev = interface_to_usbdev(interface);

	mutex_lock(&agilent_82357a_hotplug_lock);

	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i) {
		if (agilent_82357a_driver_interfaces[i] == interface)	{
			struct gpib_board *board = usb_get_intfdata(interface);

			if (board) {
				struct agilent_82357a_priv *a_priv = board->private_data;

				if (a_priv) {
					agilent_82357a_abort(a_priv, 0);
					agilent_82357a_abort(a_priv, 0);
					retval = agilent_82357a_go_idle(board);
					if (retval) {
						dev_err(&usb_dev->dev, "failed to go idle, retval=%i\n",
							retval);
						mutex_unlock(&agilent_82357a_hotplug_lock);
						return retval;
					}
					mutex_lock(&a_priv->interrupt_alloc_lock);
					agilent_82357a_cleanup_urbs(a_priv);
					mutex_unlock(&a_priv->interrupt_alloc_lock);
					dev_dbg(&usb_dev->dev,
						"bus %d dev num %d gpib %d, interface %i suspended\n",
						usb_dev->bus->busnum, usb_dev->devnum,
						board->minor, i);
				}
			}
			break;
		}
	}

	mutex_unlock(&agilent_82357a_hotplug_lock);

	return 0;
}

static int agilent_82357a_driver_resume(struct usb_interface *interface)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	struct gpib_board *board;
	int i, retval;

	mutex_lock(&agilent_82357a_hotplug_lock);

	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i)	{
		if (agilent_82357a_driver_interfaces[i] == interface) {
			board = usb_get_intfdata(interface);
			if (board)
				break;
		}
	}
	if (i == MAX_NUM_82357A_INTERFACES)
		goto resume_exit;

	struct agilent_82357a_priv *a_priv = board->private_data;

	if (a_priv) {
		if (a_priv->interrupt_urb) {
			mutex_lock(&a_priv->interrupt_alloc_lock);
			retval = usb_submit_urb(a_priv->interrupt_urb, GFP_KERNEL);
			if (retval) {
				dev_err(&usb_dev->dev, "failed to resubmit interrupt urb in resume, retval=%i\n",
					retval);
				mutex_unlock(&a_priv->interrupt_alloc_lock);
				mutex_unlock(&agilent_82357a_hotplug_lock);
				return retval;
			}
			mutex_unlock(&a_priv->interrupt_alloc_lock);
		}
		retval = agilent_82357a_init(board);
		if (retval < 0) {
			mutex_unlock(&agilent_82357a_hotplug_lock);
			return retval;
		}
		// set/unset system controller
		agilent_82357a_request_system_control(board, board->master);
		// toggle ifc if master
		if (board->master) {
			agilent_82357a_interface_clear(board, 1);
			usleep_range(200, 250);
			agilent_82357a_interface_clear(board, 0);
		}
		// assert/unassert REN
		agilent_82357a_remote_enable(board, a_priv->ren_state);

		dev_dbg(&usb_dev->dev,
			"bus %d dev num %d gpib%d, interface %i resumed\n",
			usb_dev->bus->busnum, usb_dev->devnum, board->minor, i);
	}

resume_exit:
	mutex_unlock(&agilent_82357a_hotplug_lock);

	return 0;
}

static struct usb_driver agilent_82357a_bus_driver = {
	.name = DRV_NAME,
	.probe = agilent_82357a_driver_probe,
	.disconnect = agilent_82357a_driver_disconnect,
	.suspend = agilent_82357a_driver_suspend,
	.resume = agilent_82357a_driver_resume,
	.id_table = agilent_82357a_driver_device_table,
};

static int __init agilent_82357a_init_module(void)
{
	int i;
	int ret;

	for (i = 0; i < MAX_NUM_82357A_INTERFACES; ++i)
		agilent_82357a_driver_interfaces[i] = NULL;

	ret = usb_register(&agilent_82357a_bus_driver);
	if (ret) {
		pr_err("usb_register failed: error = %d\n", ret);
		return ret;
	}

	ret = gpib_register_driver(&agilent_82357a_gpib_interface, THIS_MODULE);
	if (ret) {
		pr_err("gpib_register_driver failed: error = %d\n", ret);
		usb_deregister(&agilent_82357a_bus_driver);
		return ret;
	}

	return 0;
}

static void __exit agilent_82357a_exit_module(void)
{
	gpib_unregister_driver(&agilent_82357a_gpib_interface);
	usb_deregister(&agilent_82357a_bus_driver);
}

module_init(agilent_82357a_init_module);
module_exit(agilent_82357a_exit_module);
