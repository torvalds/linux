/*
 * UART driver for the Greybus "generic" UART module.
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 *
 * Heavily based on drivers/usb/class/cdc-acm.c and
 * drivers/usb/serial/usb-serial.c.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>

#include "greybus.h"
#include "module.h"

#define GB_NUM_MINORS	255	/* 255 is enough for anyone... */
#define GB_NAME		"ttyGB"

/* Version of the Greybus PWM protocol we support */
#define GB_UART_VERSION_MAJOR		0x00
#define GB_UART_VERSION_MINOR		0x01

/* Greybus UART request types */
#define GB_UART_REQ_INVALID			0x00
#define GB_UART_REQ_PROTOCOL_VERSION		0x01
#define GB_UART_REQ_SEND_DATA			0x02
#define GB_UART_REQ_RECEIVE_DATA		0x03	/* Unsolicited data */
#define GB_UART_REQ_SET_LINE_CODING		0x04
#define GB_UART_REQ_SET_CONTROL_LINE_STATE	0x05
#define GB_UART_REQ_SET_BREAK			0x06
#define GB_UART_REQ_SERIAL_STATE		0x07	/* Unsolicited data */
#define GB_UART_TYPE_RESPONSE			0x80	/* OR'd with rest */

struct gb_uart_proto_version_response {
	__u8	status;
	__u8	major;
	__u8	minor;
};

struct gb_uart_send_data_request {
	__le16	size;
	__u8	data[0];
};

struct gb_serial_line_coding {
	__le32	rate;
	__u8	format;
#define GB_SERIAL_1_STOP_BITS		0
#define GB_SERIAL_1_5_STOP_BITS		1
#define GB_SERIAL_2_STOP_BITS		2

	__u8	parity;
#define GB_SERIAL_NO_PARITY		0
#define GB_SERIAL_ODD_PARITY		1
#define GB_SERIAL_EVEN_PARITY		2
#define GB_SERIAL_MARK_PARITY		3
#define GB_SERIAL_SPACE_PARITY		4

	__u8	data;
} __attribute__ ((packed));

struct gb_uart_set_line_coding_request {
	struct gb_serial_line_coding	line_coding;
};

/* output control lines */
#define GB_UART_CTRL_DTR		0x01
#define GB_UART_CTRL_RTS		0x02

struct gb_uart_set_control_line_state_request {
	__le16	control;
};

struct gb_uart_set_break_request {
	__u8	state;
};

/* input control lines and line errors */
#define GB_UART_CTRL_DCD		0x01
#define GB_UART_CTRL_DSR		0x02
#define GB_UART_CTRL_BRK		0x04
#define GB_UART_CTRL_RI			0x08

#define GB_UART_CTRL_FRAMING		0x10
#define GB_UART_CTRL_PARITY		0x20
#define GB_UART_CTRL_OVERRUN		0x40

struct gb_uart_serial_state_request {
	__u16	control;
};

struct gb_uart_simple_response {
	__u8	status;
};

struct gb_tty {
	struct tty_port port;
	struct gb_connection *connection;
	u16 cport_id;
	unsigned int minor;
	unsigned char clocal;
	unsigned int throttled:1;
	unsigned int throttle_req:1;
	bool disconnected;
	int writesize;		// FIXME - set this somehow.
	spinlock_t read_lock;
	spinlock_t write_lock;
	struct async_icount iocount;
	struct async_icount oldcount;
	wait_queue_head_t wioctl;
	struct mutex mutex;
	u8 version_major;
	u8 version_minor;
	unsigned int ctrlin;	/* input control lines */
	unsigned int ctrlout;	/* output control lines */
	struct gb_serial_line_coding line_coding;
};


static struct tty_driver *gb_tty_driver;
static DEFINE_IDR(tty_minors);
static DEFINE_MUTEX(table_lock);
static atomic_t reference_count = ATOMIC_INIT(0);


static int request_operation(struct gb_connection *connection, int type,
			     void *response, int response_size)
{
	struct gb_operation *operation;
	struct gb_uart_simple_response *fake_request;
	u8 *local_response;
	int ret;

	local_response = kmalloc(response_size, GFP_KERNEL);
	if (!local_response)
		return -ENOMEM;

	operation = gb_operation_create(connection, type, 0, response_size);
	if (!operation) {
		kfree(local_response);
		return -ENOMEM;
	}

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("version operation failed (%d)\n", ret);
		goto out;
	}

	/*
	 * We only want to look at the status, and all requests have the same
	 * layout for where the status is, so cast this to a random request so
	 * we can see the status easier.
	 */
	fake_request = (struct gb_uart_simple_response *)local_response;
	if (fake_request->status) {
		gb_connection_err(connection, "response %hhu",
			fake_request->status);
		ret = -EIO;
	} else {
		/* Good request, so copy to the caller's buffer */
		if (response_size && response)
			memcpy(response, local_response, response_size);
	}
out:
	gb_operation_destroy(operation);
	kfree(local_response);

	return ret;
}

/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int get_version(struct gb_tty *tty)
{
	struct gb_uart_proto_version_response version_request;
	int retval;

	retval = request_operation(tty->connection,
				   GB_UART_REQ_PROTOCOL_VERSION,
				   &version_request, sizeof(version_request));
	if (retval)
		return retval;

	if (version_request.major > GB_UART_VERSION_MAJOR) {
		pr_err("unsupported major version (%hhu > %hhu)\n",
			version_request.major, GB_UART_VERSION_MAJOR);
		return -ENOTSUPP;
	}

	tty->version_major = version_request.major;
	tty->version_minor = version_request.minor;
	return 0;
}

static int send_data(struct gb_tty *tty, u16 size, const u8 *data)
{
	struct gb_connection *connection = tty->connection;
	struct gb_operation *operation;
	struct gb_uart_send_data_request *request;
	struct gb_uart_simple_response *response;
	int retval;

	if (!data || !size)
		return 0;

	operation = gb_operation_create(connection, GB_UART_REQ_SEND_DATA,
					sizeof(*request) + size,
					sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->size = cpu_to_le16(size);
	memcpy(&request->data[0], data, size);

	/* Synchronous operation--no callback */
	retval = gb_operation_request_send(operation, NULL);
	if (retval) {
		dev_err(&connection->dev,
			"send data operation failed (%d)\n", retval);
		goto out;
	}

	response = operation->response.payload;
	if (response->status) {
		gb_connection_err(connection, "send data response %hhu",
				  response->status);
		retval = -EIO;
	}
out:
	gb_operation_destroy(operation);

	return retval;
}

static int send_line_coding(struct gb_tty *tty,
			    struct gb_serial_line_coding *line_coding)
{
	struct gb_connection *connection = tty->connection;
	struct gb_operation *operation;
	struct gb_uart_set_line_coding_request *request;
	struct gb_uart_simple_response *response;
	int retval;

	operation = gb_operation_create(connection, GB_UART_REQ_SET_LINE_CODING,
					sizeof(*request),
					sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	memcpy(&request->line_coding, line_coding, sizeof(*line_coding));

	/* Synchronous operation--no callback */
	retval = gb_operation_request_send(operation, NULL);
	if (retval) {
		dev_err(&connection->dev,
			"send line coding operation failed (%d)\n", retval);
		goto out;
	}

	response = operation->response.payload;
	if (response->status) {
		gb_connection_err(connection, "send line coding response %hhu",
				  response->status);
		retval = -EIO;
	}
out:
	gb_operation_destroy(operation);

	return retval;
}

static int send_control(struct gb_tty *tty, u16 control)
{
	struct gb_connection *connection = tty->connection;
	struct gb_operation *operation;
	struct gb_uart_set_control_line_state_request *request;
	struct gb_uart_simple_response *response;
	int retval;

	operation = gb_operation_create(connection,
					GB_UART_REQ_SET_CONTROL_LINE_STATE,
					sizeof(*request),
					sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->control = cpu_to_le16(control);

	/* Synchronous operation--no callback */
	retval = gb_operation_request_send(operation, NULL);
	if (retval) {
		dev_err(&connection->dev,
			"send control operation failed (%d)\n", retval);
		goto out;
	}

	response = operation->response.payload;
	if (response->status) {
		gb_connection_err(connection, "send control response %hhu",
				  response->status);
		retval = -EIO;
	}
out:
	gb_operation_destroy(operation);

	return retval;
}

static int send_break(struct gb_tty *tty, u8 state)
{
	struct gb_connection *connection = tty->connection;
	struct gb_operation *operation;
	struct gb_uart_set_break_request *request;
	struct gb_uart_simple_response *response;
	int retval;

	if ((state != 0) && (state != 1)) {
		dev_err(&connection->dev, "invalid break state of %d\n", state);
		return -EINVAL;
	}

	operation = gb_operation_create(connection, GB_UART_REQ_SET_BREAK,
					sizeof(*request),
					sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->state = state;

	/* Synchronous operation--no callback */
	retval = gb_operation_request_send(operation, NULL);
	if (retval) {
		dev_err(&connection->dev,
			"send break operation failed (%d)\n", retval);
		goto out;
	}

	response = operation->response.payload;
	if (response->status) {
		gb_connection_err(connection, "send break response %hhu",
				  response->status);
		retval = -EIO;
	}
out:
	gb_operation_destroy(operation);

	return retval;
}


static struct gb_tty *get_gb_by_minor(unsigned minor)
{
	struct gb_tty *gb_tty;

	mutex_lock(&table_lock);
	gb_tty = idr_find(&tty_minors, minor);
	if (gb_tty) {
		mutex_lock(&gb_tty->mutex);
		if (gb_tty->disconnected) {
			mutex_unlock(&gb_tty->mutex);
			gb_tty = NULL;
		} else {
			tty_port_get(&gb_tty->port);
			mutex_unlock(&gb_tty->mutex);
		}
	}
	mutex_unlock(&table_lock);
	return gb_tty;
}

static int alloc_minor(struct gb_tty *gb_tty)
{
	int minor;

	mutex_lock(&table_lock);
	minor = idr_alloc(&tty_minors, gb_tty, 0, GB_NUM_MINORS, GFP_KERNEL);
	mutex_unlock(&table_lock);
	if (minor >= 0)
		gb_tty->minor = minor;
	return minor;
}

static void release_minor(struct gb_tty *gb_tty)
{
	int minor = gb_tty->minor;

	gb_tty->minor = 0;	/* Maybe should use an invalid value instead */
	mutex_lock(&table_lock);
	idr_remove(&tty_minors, minor);
	mutex_unlock(&table_lock);
}

static int gb_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct gb_tty *gb_tty;
	int retval;

	gb_tty = get_gb_by_minor(tty->index);
	if (!gb_tty)
		return -ENODEV;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error;

	tty->driver_data = gb_tty;
	return 0;
error:
	tty_port_put(&gb_tty->port);
	return retval;
}

static int gb_tty_open(struct tty_struct *tty, struct file *file)
{
	struct gb_tty *gb_tty = tty->driver_data;

	return tty_port_open(&gb_tty->port, tty, file);
}

static void gb_tty_close(struct tty_struct *tty, struct file *file)
{
	struct gb_tty *gb_tty = tty->driver_data;

	tty_port_close(&gb_tty->port, tty, file);
}

static void gb_tty_cleanup(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;

	tty_port_put(&gb_tty->port);
}

static void gb_tty_hangup(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;

	tty_port_hangup(&gb_tty->port);
}

static int gb_tty_write(struct tty_struct *tty, const unsigned char *buf,
			int count)
{
	struct gb_tty *gb_tty = tty->driver_data;

	return send_data(gb_tty, count, buf);
}

static int gb_tty_write_room(struct tty_struct *tty)
{
//	struct gb_tty *gb_tty = tty->driver_data;

	// FIXME - how much do we want to say we have room for?
	return 0;
}

static int gb_tty_chars_in_buffer(struct tty_struct *tty)
{
//	struct gb_tty *gb_tty = tty->driver_data;

	// FIXME - how many left to send?
	return 0;
}

static int gb_tty_break_ctl(struct tty_struct *tty, int state)
{
	struct gb_tty *gb_tty = tty->driver_data;

	return send_break(gb_tty, state ? 1 : 0);
}

static void gb_tty_set_termios(struct tty_struct *tty,
			       struct ktermios *termios_old)
{
	struct gb_tty *gb_tty = tty->driver_data;
	struct ktermios *termios = &tty->termios;
	struct gb_serial_line_coding newline;
	int newctrl = gb_tty->ctrlout;

	newline.rate = cpu_to_le32(tty_get_baud_rate(tty));
	newline.format = termios->c_cflag & CSTOPB ? 2 : 0;
	newline.parity = termios->c_cflag & PARENB ?
				(termios->c_cflag & PARODD ? 1 : 2) +
				(termios->c_cflag & CMSPAR ? 2 : 0) : 0;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		newline.data = 5;
		break;
	case CS6:
		newline.data = 6;
		break;
	case CS7:
		newline.data = 7;
		break;
	case CS8:
	default:
		newline.data = 8;
		break;
	}

	/* FIXME: needs to clear unsupported bits in the termios */
	gb_tty->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (C_BAUD(tty) == B0) {
		newline.rate = gb_tty->line_coding.rate;
		newctrl &= GB_UART_CTRL_DTR;
	} else if (termios_old && (termios_old->c_cflag & CBAUD) == B0) {
		newctrl |= GB_UART_CTRL_DTR;
	}

	if (newctrl != gb_tty->ctrlout) {
		gb_tty->ctrlout = newctrl;
		send_control(gb_tty, newctrl);
	}

	if (memcpy(&gb_tty->line_coding, &newline, sizeof(newline))) {
		memcpy(&gb_tty->line_coding, &newline, sizeof(newline));
		send_line_coding(gb_tty, &gb_tty->line_coding);
	}
}

static int gb_tty_tiocmget(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;

	return (gb_tty->ctrlout & GB_UART_CTRL_DTR ? TIOCM_DTR : 0) |
	       (gb_tty->ctrlout & GB_UART_CTRL_RTS ? TIOCM_RTS : 0) |
	       (gb_tty->ctrlin  & GB_UART_CTRL_DSR ? TIOCM_DSR : 0) |
	       (gb_tty->ctrlin  & GB_UART_CTRL_RI  ? TIOCM_RI  : 0) |
	       (gb_tty->ctrlin  & GB_UART_CTRL_DCD ? TIOCM_CD  : 0) |
	       TIOCM_CTS;
}

static int gb_tty_tiocmset(struct tty_struct *tty, unsigned int set,
			   unsigned int clear)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned int newctrl = gb_tty->ctrlout;

	set = (set & TIOCM_DTR ? GB_UART_CTRL_DTR : 0) |
	      (set & TIOCM_RTS ? GB_UART_CTRL_RTS : 0);
	clear = (clear & TIOCM_DTR ? GB_UART_CTRL_DTR : 0) |
		(clear & TIOCM_RTS ? GB_UART_CTRL_RTS : 0);

	newctrl = (newctrl & ~clear) | set;
	if (gb_tty->ctrlout == newctrl)
		return 0;

	gb_tty->ctrlout = newctrl;
	return send_control(gb_tty, newctrl);
}

static void gb_tty_throttle(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;

	spin_lock_irq(&gb_tty->read_lock);
	gb_tty->throttle_req = 1;
	spin_unlock_irq(&gb_tty->read_lock);
}

static void gb_tty_unthrottle(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned int was_throttled;

	spin_lock_irq(&gb_tty->read_lock);
	was_throttled = gb_tty->throttled;
	gb_tty->throttle_req = 0;
	gb_tty->throttled = 0;
	spin_unlock_irq(&gb_tty->read_lock);

	if (was_throttled) {
		// FIXME - send more data
	}
}

static int get_serial_info(struct gb_tty *gb_tty,
			   struct serial_struct __user *info)
{
	struct serial_struct tmp;

	if (!info)
		return -EINVAL;

	memset(&tmp, 0, sizeof(tmp));
	tmp.flags = ASYNC_LOW_LATENCY;
	tmp.xmit_fifo_size = gb_tty->writesize;
	tmp.baud_base = 0;	// FIXME
	tmp.close_delay = gb_tty->port.close_delay / 10;
	tmp.closing_wait = gb_tty->port.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
				ASYNC_CLOSING_WAIT_NONE : gb_tty->port.closing_wait / 10;

	if (copy_to_user(info, &tmp, sizeof(tmp)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct gb_tty *gb_tty,
			   struct serial_struct __user *newinfo)
{
	struct serial_struct new_serial;
	unsigned int closing_wait;
	unsigned int close_delay;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	close_delay = new_serial.close_delay * 10;
	closing_wait = new_serial.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE : new_serial.closing_wait * 10;

	mutex_lock(&gb_tty->port.mutex);
	if (!capable(CAP_SYS_ADMIN)) {
		if ((close_delay != gb_tty->port.close_delay) ||
		    (closing_wait != gb_tty->port.closing_wait))
			retval = -EPERM;
		else
			retval = -EOPNOTSUPP;
	} else {
		gb_tty->port.close_delay = close_delay;
		gb_tty->port.closing_wait = closing_wait;
	}
	mutex_unlock(&gb_tty->port.mutex);
	return retval;
}

static int wait_serial_change(struct gb_tty *gb_tty, unsigned long arg)
{
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);
	struct async_icount old;
	struct async_icount new;

	if (!(arg & (TIOCM_DSR | TIOCM_RI | TIOCM_CD)))
		return -EINVAL;

	do {
		spin_lock_irq(&gb_tty->read_lock);
		old = gb_tty->oldcount;
		new = gb_tty->iocount;
		gb_tty->oldcount = new;
		spin_unlock_irq(&gb_tty->read_lock);

		if ((arg & TIOCM_DSR) && (old.dsr != new.dsr))
			break;
		if ((arg & TIOCM_CD) && (old.dcd != new.dcd))
			break;
		if ((arg & TIOCM_RI) && (old.rng != new.rng))
			break;

		add_wait_queue(&gb_tty->wioctl, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		remove_wait_queue(&gb_tty->wioctl, &wait);
		if (gb_tty->disconnected) {
			if (arg & TIOCM_CD)
				break;
			retval = -ENODEV;
		} else if (signal_pending(current)) {
			retval = -ERESTARTSYS;
		}
	} while (!retval);

	return retval;
}

static int get_serial_usage(struct gb_tty *gb_tty,
			    struct serial_icounter_struct __user *count)
{
	struct serial_icounter_struct icount;
	int retval = 0;

	memset(&icount, 0, sizeof(icount));
	icount.dsr = gb_tty->iocount.dsr;
	icount.rng = gb_tty->iocount.rng;
	icount.dcd = gb_tty->iocount.dcd;
	icount.frame = gb_tty->iocount.frame;
	icount.overrun = gb_tty->iocount.overrun;
	icount.parity = gb_tty->iocount.parity;
	icount.brk = gb_tty->iocount.brk;

	if (copy_to_user(count, &icount, sizeof(icount)) > 0)
		retval = -EFAULT;

	return retval;
}

static int gb_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg)
{
	struct gb_tty *gb_tty = tty->driver_data;

	switch (cmd) {
	case TIOCGSERIAL:
		return get_serial_info(gb_tty,
				       (struct serial_struct __user *)arg);
	case TIOCSSERIAL:
		return set_serial_info(gb_tty,
				       (struct serial_struct __user *)arg);
	case TIOCMIWAIT:
		return wait_serial_change(gb_tty, arg);
	case TIOCGICOUNT:
		return get_serial_usage(gb_tty,
					(struct serial_icounter_struct __user *)arg);
	}

	return -ENOIOCTLCMD;
}


static const struct tty_operations gb_ops = {
	.install =		gb_tty_install,
	.open =			gb_tty_open,
	.close =		gb_tty_close,
	.cleanup =		gb_tty_cleanup,
	.hangup =		gb_tty_hangup,
	.write =		gb_tty_write,
	.write_room =		gb_tty_write_room,
	.ioctl =		gb_tty_ioctl,
	.throttle =		gb_tty_throttle,
	.unthrottle =		gb_tty_unthrottle,
	.chars_in_buffer =	gb_tty_chars_in_buffer,
	.break_ctl =		gb_tty_break_ctl,
	.set_termios =		gb_tty_set_termios,
	.tiocmget =		gb_tty_tiocmget,
	.tiocmset =		gb_tty_tiocmset,
};


static int gb_tty_init(void);
static void gb_tty_exit(void);

static int gb_uart_connection_init(struct gb_connection *connection)
{
	struct gb_tty *gb_tty;
	struct device *tty_dev;
	int retval;
	int minor;

	/* First time here, initialize the tty structures */
	if (atomic_inc_return(&reference_count) == 1) {
		retval = gb_tty_init();
		if (retval) {
			atomic_dec(&reference_count);
			return retval;
		}
	}

	gb_tty = kzalloc(sizeof(*gb_tty), GFP_KERNEL);
	if (!gb_tty)
		return -ENOMEM;
	gb_tty->connection = connection;

	/* Check for compatible protocol version */
	retval = get_version(gb_tty);
	if (retval)
		goto error_version;

	minor = alloc_minor(gb_tty);
	if (minor < 0) {
		if (minor == -ENOSPC) {
			dev_err(&connection->dev,
				"no more free minor numbers\n");
			return -ENODEV;
		}
		return minor;
	}

	gb_tty->minor = minor;
	spin_lock_init(&gb_tty->write_lock);
	spin_lock_init(&gb_tty->read_lock);
	init_waitqueue_head(&gb_tty->wioctl);
	mutex_init(&gb_tty->mutex);

	connection->private = gb_tty;

	send_control(gb_tty, gb_tty->ctrlout);

	/* initialize the uart to be 9600n81 */
	gb_tty->line_coding.rate = cpu_to_le32(9600);
	gb_tty->line_coding.format = GB_SERIAL_1_STOP_BITS;
	gb_tty->line_coding.parity = GB_SERIAL_NO_PARITY;
	gb_tty->line_coding.data = 8;
	send_line_coding(gb_tty, &gb_tty->line_coding);

	tty_dev = tty_port_register_device(&gb_tty->port, gb_tty_driver, minor,
					   &connection->dev);
	if (IS_ERR(tty_dev)) {
		retval = PTR_ERR(tty_dev);
		goto error;
	}

	return 0;
error:
	release_minor(gb_tty);
error_version:
	connection->private = NULL;
	kfree(gb_tty);
	return retval;
}

static void gb_uart_connection_exit(struct gb_connection *connection)
{
	struct gb_tty *gb_tty = connection->private;
	struct tty_struct *tty;

	if (!gb_tty)
		return;

	mutex_lock(&gb_tty->mutex);
	gb_tty->disconnected = true;

	wake_up_all(&gb_tty->wioctl);
	connection->private = NULL;
	mutex_unlock(&gb_tty->mutex);

	tty = tty_port_tty_get(&gb_tty->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}
	/* FIXME - stop all traffic */

	tty_unregister_device(gb_tty_driver, gb_tty->minor);

	/* FIXME - free transmit / recieve buffers */

	tty_port_put(&gb_tty->port);

	kfree(gb_tty);

	/* If last device is gone, tear down the tty structures */
	if (atomic_dec_return(&reference_count) == 0)
		gb_tty_exit();
}

static int gb_tty_init(void)
{
	int retval = 0;

	gb_tty_driver = tty_alloc_driver(GB_NUM_MINORS, 0);
	if (IS_ERR(gb_tty_driver)) {
		pr_err("Can not allocate tty driver\n");
		retval = -ENOMEM;
		goto fail_unregister_dev;
	}

	gb_tty_driver->driver_name = "gb";
	gb_tty_driver->name = GB_NAME;
	gb_tty_driver->major = 0;
	gb_tty_driver->minor_start = 0;
	gb_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	gb_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	gb_tty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	gb_tty_driver->init_termios = tty_std_termios;
	gb_tty_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty_set_operations(gb_tty_driver, &gb_ops);

	retval = tty_register_driver(gb_tty_driver);
	if (retval) {
		pr_err("Can not register tty driver: %d\n", retval);
		goto fail_put_gb_tty;
	}

	return 0;

fail_put_gb_tty:
	put_tty_driver(gb_tty_driver);
fail_unregister_dev:
	return retval;
}

static void gb_tty_exit(void)
{
	int major = MAJOR(gb_tty_driver->major);
	int minor = gb_tty_driver->minor_start;

	tty_unregister_driver(gb_tty_driver);
	put_tty_driver(gb_tty_driver);
	unregister_chrdev_region(MKDEV(major, minor), GB_NUM_MINORS);
}

static struct gb_protocol uart_protocol = {
	.id			= GREYBUS_PROTOCOL_UART,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_uart_connection_init,
	.connection_exit	= gb_uart_connection_exit,
	.request_recv		= NULL,	/* FIXME we have 2 types of requests!!! */
};

bool gb_uart_protocol_init(void)
{
	return gb_protocol_register(&uart_protocol);
}

void gb_uart_protocol_exit(void)
{
	gb_protocol_deregister(&uart_protocol);
}
