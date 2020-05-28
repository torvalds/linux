// SPDX-License-Identifier: GPL-2.0
/*
 * UART driver for the Greybus "generic" UART module.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Heavily based on drivers/usb/class/cdc-acm.c and
 * drivers/usb/serial/usb-serial.c.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/idr.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/greybus.h>

#include "gbphy.h"

#define GB_NUM_MINORS	16	/* 16 is more than enough */
#define GB_NAME		"ttyGB"

#define GB_UART_WRITE_FIFO_SIZE		PAGE_SIZE
#define GB_UART_WRITE_ROOM_MARGIN	1	/* leave some space in fifo */
#define GB_UART_FIRMWARE_CREDITS	4096
#define GB_UART_CREDIT_WAIT_TIMEOUT_MSEC	10000

struct gb_tty_line_coding {
	__le32	rate;
	__u8	format;
	__u8	parity;
	__u8	data_bits;
	__u8	flow_control;
};

struct gb_tty {
	struct gbphy_device *gbphy_dev;
	struct tty_port port;
	void *buffer;
	size_t buffer_payload_max;
	struct gb_connection *connection;
	u16 cport_id;
	unsigned int minor;
	unsigned char clocal;
	bool disconnected;
	spinlock_t read_lock;
	spinlock_t write_lock;
	struct async_icount iocount;
	struct async_icount oldcount;
	wait_queue_head_t wioctl;
	struct mutex mutex;
	u8 ctrlin;	/* input control lines */
	u8 ctrlout;	/* output control lines */
	struct gb_tty_line_coding line_coding;
	struct work_struct tx_work;
	struct kfifo write_fifo;
	bool close_pending;
	unsigned int credits;
	struct completion credits_complete;
};

static struct tty_driver *gb_tty_driver;
static DEFINE_IDR(tty_minors);
static DEFINE_MUTEX(table_lock);

static int gb_uart_receive_data_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_tty *gb_tty = gb_connection_get_data(connection);
	struct tty_port *port = &gb_tty->port;
	struct gb_message *request = op->request;
	struct gb_uart_recv_data_request *receive_data;
	u16 recv_data_size;
	int count;
	unsigned long tty_flags = TTY_NORMAL;

	if (request->payload_size < sizeof(*receive_data)) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"short receive-data request received (%zu < %zu)\n",
			request->payload_size, sizeof(*receive_data));
		return -EINVAL;
	}

	receive_data = op->request->payload;
	recv_data_size = le16_to_cpu(receive_data->size);

	if (recv_data_size != request->payload_size - sizeof(*receive_data)) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"malformed receive-data request received (%u != %zu)\n",
			recv_data_size,
			request->payload_size - sizeof(*receive_data));
		return -EINVAL;
	}

	if (!recv_data_size)
		return -EINVAL;

	if (receive_data->flags) {
		if (receive_data->flags & GB_UART_RECV_FLAG_BREAK)
			tty_flags = TTY_BREAK;
		else if (receive_data->flags & GB_UART_RECV_FLAG_PARITY)
			tty_flags = TTY_PARITY;
		else if (receive_data->flags & GB_UART_RECV_FLAG_FRAMING)
			tty_flags = TTY_FRAME;

		/* overrun is special, not associated with a char */
		if (receive_data->flags & GB_UART_RECV_FLAG_OVERRUN)
			tty_insert_flip_char(port, 0, TTY_OVERRUN);
	}
	count = tty_insert_flip_string_fixed_flag(port, receive_data->data,
						  tty_flags, recv_data_size);
	if (count != recv_data_size) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"UART: RX 0x%08x bytes only wrote 0x%08x\n",
			recv_data_size, count);
	}
	if (count)
		tty_flip_buffer_push(port);
	return 0;
}

static int gb_uart_serial_state_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_tty *gb_tty = gb_connection_get_data(connection);
	struct gb_message *request = op->request;
	struct gb_uart_serial_state_request *serial_state;

	if (request->payload_size < sizeof(*serial_state)) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"short serial-state event received (%zu < %zu)\n",
			request->payload_size, sizeof(*serial_state));
		return -EINVAL;
	}

	serial_state = request->payload;
	gb_tty->ctrlin = serial_state->control;

	return 0;
}

static int gb_uart_receive_credits_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_tty *gb_tty = gb_connection_get_data(connection);
	struct gb_message *request = op->request;
	struct gb_uart_receive_credits_request *credit_request;
	unsigned long flags;
	unsigned int incoming_credits;
	int ret = 0;

	if (request->payload_size < sizeof(*credit_request)) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"short receive_credits event received (%zu < %zu)\n",
			request->payload_size,
			sizeof(*credit_request));
		return -EINVAL;
	}

	credit_request = request->payload;
	incoming_credits = le16_to_cpu(credit_request->count);

	spin_lock_irqsave(&gb_tty->write_lock, flags);
	gb_tty->credits += incoming_credits;
	if (gb_tty->credits > GB_UART_FIRMWARE_CREDITS) {
		gb_tty->credits -= incoming_credits;
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&gb_tty->write_lock, flags);

	if (ret) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"invalid number of incoming credits: %d\n",
			incoming_credits);
		return ret;
	}

	if (!gb_tty->close_pending)
		schedule_work(&gb_tty->tx_work);

	/*
	 * the port the tty layer may be waiting for credits
	 */
	tty_port_tty_wakeup(&gb_tty->port);

	if (gb_tty->credits == GB_UART_FIRMWARE_CREDITS)
		complete(&gb_tty->credits_complete);

	return ret;
}

static int gb_uart_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_tty *gb_tty = gb_connection_get_data(connection);
	int type = op->type;
	int ret;

	switch (type) {
	case GB_UART_TYPE_RECEIVE_DATA:
		ret = gb_uart_receive_data_handler(op);
		break;
	case GB_UART_TYPE_SERIAL_STATE:
		ret = gb_uart_serial_state_handler(op);
		break;
	case GB_UART_TYPE_RECEIVE_CREDITS:
		ret = gb_uart_receive_credits_handler(op);
		break;
	default:
		dev_err(&gb_tty->gbphy_dev->dev,
			"unsupported unsolicited request: 0x%02x\n", type);
		ret = -EINVAL;
	}

	return ret;
}

static void  gb_uart_tx_write_work(struct work_struct *work)
{
	struct gb_uart_send_data_request *request;
	struct gb_tty *gb_tty;
	unsigned long flags;
	unsigned int send_size;
	int ret;

	gb_tty = container_of(work, struct gb_tty, tx_work);
	request = gb_tty->buffer;

	while (1) {
		if (gb_tty->close_pending)
			break;

		spin_lock_irqsave(&gb_tty->write_lock, flags);
		send_size = gb_tty->buffer_payload_max;
		if (send_size > gb_tty->credits)
			send_size = gb_tty->credits;

		send_size = kfifo_out_peek(&gb_tty->write_fifo,
					   &request->data[0],
					   send_size);
		if (!send_size) {
			spin_unlock_irqrestore(&gb_tty->write_lock, flags);
			break;
		}

		gb_tty->credits -= send_size;
		spin_unlock_irqrestore(&gb_tty->write_lock, flags);

		request->size = cpu_to_le16(send_size);
		ret = gb_operation_sync(gb_tty->connection,
					GB_UART_TYPE_SEND_DATA,
					request, sizeof(*request) + send_size,
					NULL, 0);
		if (ret) {
			dev_err(&gb_tty->gbphy_dev->dev,
				"send data error: %d\n", ret);
			spin_lock_irqsave(&gb_tty->write_lock, flags);
			gb_tty->credits += send_size;
			spin_unlock_irqrestore(&gb_tty->write_lock, flags);
			if (!gb_tty->close_pending)
				schedule_work(work);
			return;
		}

		spin_lock_irqsave(&gb_tty->write_lock, flags);
		ret = kfifo_out(&gb_tty->write_fifo, &request->data[0],
				send_size);
		spin_unlock_irqrestore(&gb_tty->write_lock, flags);

		tty_port_tty_wakeup(&gb_tty->port);
	}
}

static int send_line_coding(struct gb_tty *tty)
{
	struct gb_uart_set_line_coding_request request;

	memcpy(&request, &tty->line_coding,
	       sizeof(tty->line_coding));
	return gb_operation_sync(tty->connection, GB_UART_TYPE_SET_LINE_CODING,
				 &request, sizeof(request), NULL, 0);
}

static int send_control(struct gb_tty *gb_tty, u8 control)
{
	struct gb_uart_set_control_line_state_request request;

	request.control = control;
	return gb_operation_sync(gb_tty->connection,
				 GB_UART_TYPE_SET_CONTROL_LINE_STATE,
				 &request, sizeof(request), NULL, 0);
}

static int send_break(struct gb_tty *gb_tty, u8 state)
{
	struct gb_uart_set_break_request request;

	if ((state != 0) && (state != 1)) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"invalid break state of %d\n", state);
		return -EINVAL;
	}

	request.state = state;
	return gb_operation_sync(gb_tty->connection, GB_UART_TYPE_SEND_BREAK,
				 &request, sizeof(request), NULL, 0);
}

static int gb_uart_wait_for_all_credits(struct gb_tty *gb_tty)
{
	int ret;

	if (gb_tty->credits == GB_UART_FIRMWARE_CREDITS)
		return 0;

	ret = wait_for_completion_timeout(&gb_tty->credits_complete,
			msecs_to_jiffies(GB_UART_CREDIT_WAIT_TIMEOUT_MSEC));
	if (!ret) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"time out waiting for credits\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int gb_uart_flush(struct gb_tty *gb_tty, u8 flags)
{
	struct gb_uart_serial_flush_request request;

	request.flags = flags;
	return gb_operation_sync(gb_tty->connection, GB_UART_TYPE_FLUSH_FIFOS,
				 &request, sizeof(request), NULL, 0);
}

static struct gb_tty *get_gb_by_minor(unsigned int minor)
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

	count =  kfifo_in_spinlocked(&gb_tty->write_fifo, buf, count,
				     &gb_tty->write_lock);
	if (count && !gb_tty->close_pending)
		schedule_work(&gb_tty->tx_work);

	return count;
}

static int gb_tty_write_room(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned long flags;
	int room;

	spin_lock_irqsave(&gb_tty->write_lock, flags);
	room = kfifo_avail(&gb_tty->write_fifo);
	spin_unlock_irqrestore(&gb_tty->write_lock, flags);

	room -= GB_UART_WRITE_ROOM_MARGIN;
	if (room < 0)
		return 0;

	return room;
}

static int gb_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned long flags;
	int chars;

	spin_lock_irqsave(&gb_tty->write_lock, flags);
	chars = kfifo_len(&gb_tty->write_fifo);
	if (gb_tty->credits < GB_UART_FIRMWARE_CREDITS)
		chars += GB_UART_FIRMWARE_CREDITS - gb_tty->credits;
	spin_unlock_irqrestore(&gb_tty->write_lock, flags);

	return chars;
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
	struct gb_tty_line_coding newline;
	u8 newctrl = gb_tty->ctrlout;

	newline.rate = cpu_to_le32(tty_get_baud_rate(tty));
	newline.format = termios->c_cflag & CSTOPB ?
				GB_SERIAL_2_STOP_BITS : GB_SERIAL_1_STOP_BITS;
	newline.parity = termios->c_cflag & PARENB ?
				(termios->c_cflag & PARODD ? 1 : 2) +
				(termios->c_cflag & CMSPAR ? 2 : 0) : 0;

	switch (termios->c_cflag & CSIZE) {
	case CS5:
		newline.data_bits = 5;
		break;
	case CS6:
		newline.data_bits = 6;
		break;
	case CS7:
		newline.data_bits = 7;
		break;
	case CS8:
	default:
		newline.data_bits = 8;
		break;
	}

	/* FIXME: needs to clear unsupported bits in the termios */
	gb_tty->clocal = ((termios->c_cflag & CLOCAL) != 0);

	if (C_BAUD(tty) == B0) {
		newline.rate = gb_tty->line_coding.rate;
		newctrl &= ~(GB_UART_CTRL_DTR | GB_UART_CTRL_RTS);
	} else if (termios_old && (termios_old->c_cflag & CBAUD) == B0) {
		newctrl |= (GB_UART_CTRL_DTR | GB_UART_CTRL_RTS);
	}

	if (newctrl != gb_tty->ctrlout) {
		gb_tty->ctrlout = newctrl;
		send_control(gb_tty, newctrl);
	}

	if (C_CRTSCTS(tty) && C_BAUD(tty) != B0)
		newline.flow_control = GB_SERIAL_AUTO_RTSCTS_EN;
	else
		newline.flow_control = 0;

	if (memcmp(&gb_tty->line_coding, &newline, sizeof(newline))) {
		memcpy(&gb_tty->line_coding, &newline, sizeof(newline));
		send_line_coding(gb_tty);
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
	u8 newctrl = gb_tty->ctrlout;

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
	unsigned char stop_char;
	int retval;

	if (I_IXOFF(tty)) {
		stop_char = STOP_CHAR(tty);
		retval = gb_tty_write(tty, &stop_char, 1);
		if (retval <= 0)
			return;
	}

	if (tty->termios.c_cflag & CRTSCTS) {
		gb_tty->ctrlout &= ~GB_UART_CTRL_RTS;
		retval = send_control(gb_tty, gb_tty->ctrlout);
	}
}

static void gb_tty_unthrottle(struct tty_struct *tty)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned char start_char;
	int retval;

	if (I_IXOFF(tty)) {
		start_char = START_CHAR(tty);
		retval = gb_tty_write(tty, &start_char, 1);
		if (retval <= 0)
			return;
	}

	if (tty->termios.c_cflag & CRTSCTS) {
		gb_tty->ctrlout |= GB_UART_CTRL_RTS;
		retval = send_control(gb_tty, gb_tty->ctrlout);
	}
}

static int get_serial_info(struct tty_struct *tty,
			   struct serial_struct *ss)
{
	struct gb_tty *gb_tty = tty->driver_data;

	ss->type = PORT_16550A;
	ss->line = gb_tty->minor;
	ss->xmit_fifo_size = 16;
	ss->baud_base = 9600;
	ss->close_delay = gb_tty->port.close_delay / 10;
	ss->closing_wait =
		gb_tty->port.closing_wait == ASYNC_CLOSING_WAIT_NONE ?
		ASYNC_CLOSING_WAIT_NONE : gb_tty->port.closing_wait / 10;
	return 0;
}

static int set_serial_info(struct tty_struct *tty,
			   struct serial_struct *ss)
{
	struct gb_tty *gb_tty = tty->driver_data;
	unsigned int closing_wait;
	unsigned int close_delay;
	int retval = 0;

	close_delay = ss->close_delay * 10;
	closing_wait = ss->closing_wait == ASYNC_CLOSING_WAIT_NONE ?
			ASYNC_CLOSING_WAIT_NONE : ss->closing_wait * 10;

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

static int gb_tty_get_icount(struct tty_struct *tty,
			     struct serial_icounter_struct *icount)
{
	struct gb_tty *gb_tty = tty->driver_data;

	icount->dsr = gb_tty->iocount.dsr;
	icount->rng = gb_tty->iocount.rng;
	icount->dcd = gb_tty->iocount.dcd;
	icount->frame = gb_tty->iocount.frame;
	icount->overrun = gb_tty->iocount.overrun;
	icount->parity = gb_tty->iocount.parity;
	icount->brk = gb_tty->iocount.brk;

	return 0;
}

static int gb_tty_ioctl(struct tty_struct *tty, unsigned int cmd,
			unsigned long arg)
{
	struct gb_tty *gb_tty = tty->driver_data;

	switch (cmd) {
	case TIOCMIWAIT:
		return wait_serial_change(gb_tty, arg);
	}

	return -ENOIOCTLCMD;
}

static void gb_tty_dtr_rts(struct tty_port *port, int on)
{
	struct gb_tty *gb_tty;
	u8 newctrl;

	gb_tty = container_of(port, struct gb_tty, port);
	newctrl = gb_tty->ctrlout;

	if (on)
		newctrl |= (GB_UART_CTRL_DTR | GB_UART_CTRL_RTS);
	else
		newctrl &= ~(GB_UART_CTRL_DTR | GB_UART_CTRL_RTS);

	gb_tty->ctrlout = newctrl;
	send_control(gb_tty, newctrl);
}

static int gb_tty_port_activate(struct tty_port *port,
				struct tty_struct *tty)
{
	struct gb_tty *gb_tty;

	gb_tty = container_of(port, struct gb_tty, port);

	return gbphy_runtime_get_sync(gb_tty->gbphy_dev);
}

static void gb_tty_port_shutdown(struct tty_port *port)
{
	struct gb_tty *gb_tty;
	unsigned long flags;
	int ret;

	gb_tty = container_of(port, struct gb_tty, port);

	gb_tty->close_pending = true;

	cancel_work_sync(&gb_tty->tx_work);

	spin_lock_irqsave(&gb_tty->write_lock, flags);
	kfifo_reset_out(&gb_tty->write_fifo);
	spin_unlock_irqrestore(&gb_tty->write_lock, flags);

	if (gb_tty->credits == GB_UART_FIRMWARE_CREDITS)
		goto out;

	ret = gb_uart_flush(gb_tty, GB_SERIAL_FLAG_FLUSH_TRANSMITTER);
	if (ret) {
		dev_err(&gb_tty->gbphy_dev->dev,
			"error flushing transmitter: %d\n", ret);
	}

	gb_uart_wait_for_all_credits(gb_tty);

out:
	gb_tty->close_pending = false;

	gbphy_runtime_put_autosuspend(gb_tty->gbphy_dev);
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
	.get_icount =		gb_tty_get_icount,
	.set_serial =		set_serial_info,
	.get_serial =		get_serial_info,
};

static const struct tty_port_operations gb_port_ops = {
	.dtr_rts =		gb_tty_dtr_rts,
	.activate =		gb_tty_port_activate,
	.shutdown =		gb_tty_port_shutdown,
};

static int gb_uart_probe(struct gbphy_device *gbphy_dev,
			 const struct gbphy_device_id *id)
{
	struct gb_connection *connection;
	size_t max_payload;
	struct gb_tty *gb_tty;
	struct device *tty_dev;
	int retval;
	int minor;

	gb_tty = kzalloc(sizeof(*gb_tty), GFP_KERNEL);
	if (!gb_tty)
		return -ENOMEM;

	connection = gb_connection_create(gbphy_dev->bundle,
					  le16_to_cpu(gbphy_dev->cport_desc->id),
					  gb_uart_request_handler);
	if (IS_ERR(connection)) {
		retval = PTR_ERR(connection);
		goto exit_tty_free;
	}

	max_payload = gb_operation_get_payload_size_max(connection);
	if (max_payload < sizeof(struct gb_uart_send_data_request)) {
		retval = -EINVAL;
		goto exit_connection_destroy;
	}

	gb_tty->buffer_payload_max = max_payload -
			sizeof(struct gb_uart_send_data_request);

	gb_tty->buffer = kzalloc(gb_tty->buffer_payload_max, GFP_KERNEL);
	if (!gb_tty->buffer) {
		retval = -ENOMEM;
		goto exit_connection_destroy;
	}

	INIT_WORK(&gb_tty->tx_work, gb_uart_tx_write_work);

	retval = kfifo_alloc(&gb_tty->write_fifo, GB_UART_WRITE_FIFO_SIZE,
			     GFP_KERNEL);
	if (retval)
		goto exit_buf_free;

	gb_tty->credits = GB_UART_FIRMWARE_CREDITS;
	init_completion(&gb_tty->credits_complete);

	minor = alloc_minor(gb_tty);
	if (minor < 0) {
		if (minor == -ENOSPC) {
			dev_err(&gbphy_dev->dev,
				"no more free minor numbers\n");
			retval = -ENODEV;
		} else {
			retval = minor;
		}
		goto exit_kfifo_free;
	}

	gb_tty->minor = minor;
	spin_lock_init(&gb_tty->write_lock);
	spin_lock_init(&gb_tty->read_lock);
	init_waitqueue_head(&gb_tty->wioctl);
	mutex_init(&gb_tty->mutex);

	tty_port_init(&gb_tty->port);
	gb_tty->port.ops = &gb_port_ops;

	gb_tty->connection = connection;
	gb_tty->gbphy_dev = gbphy_dev;
	gb_connection_set_data(connection, gb_tty);
	gb_gbphy_set_data(gbphy_dev, gb_tty);

	retval = gb_connection_enable_tx(connection);
	if (retval)
		goto exit_release_minor;

	send_control(gb_tty, gb_tty->ctrlout);

	/* initialize the uart to be 9600n81 */
	gb_tty->line_coding.rate = cpu_to_le32(9600);
	gb_tty->line_coding.format = GB_SERIAL_1_STOP_BITS;
	gb_tty->line_coding.parity = GB_SERIAL_NO_PARITY;
	gb_tty->line_coding.data_bits = 8;
	send_line_coding(gb_tty);

	retval = gb_connection_enable(connection);
	if (retval)
		goto exit_connection_disable;

	tty_dev = tty_port_register_device(&gb_tty->port, gb_tty_driver, minor,
					   &gbphy_dev->dev);
	if (IS_ERR(tty_dev)) {
		retval = PTR_ERR(tty_dev);
		goto exit_connection_disable;
	}

	gbphy_runtime_put_autosuspend(gbphy_dev);
	return 0;

exit_connection_disable:
	gb_connection_disable(connection);
exit_release_minor:
	release_minor(gb_tty);
exit_kfifo_free:
	kfifo_free(&gb_tty->write_fifo);
exit_buf_free:
	kfree(gb_tty->buffer);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_tty_free:
	kfree(gb_tty);

	return retval;
}

static void gb_uart_remove(struct gbphy_device *gbphy_dev)
{
	struct gb_tty *gb_tty = gb_gbphy_get_data(gbphy_dev);
	struct gb_connection *connection = gb_tty->connection;
	struct tty_struct *tty;
	int ret;

	ret = gbphy_runtime_get_sync(gbphy_dev);
	if (ret)
		gbphy_runtime_get_noresume(gbphy_dev);

	mutex_lock(&gb_tty->mutex);
	gb_tty->disconnected = true;

	wake_up_all(&gb_tty->wioctl);
	mutex_unlock(&gb_tty->mutex);

	tty = tty_port_tty_get(&gb_tty->port);
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}

	gb_connection_disable_rx(connection);
	tty_unregister_device(gb_tty_driver, gb_tty->minor);

	/* FIXME - free transmit / receive buffers */

	gb_connection_disable(connection);
	tty_port_destroy(&gb_tty->port);
	gb_connection_destroy(connection);
	release_minor(gb_tty);
	kfifo_free(&gb_tty->write_fifo);
	kfree(gb_tty->buffer);
	kfree(gb_tty);
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
	gb_tty_driver->init_termios.c_cflag = B9600 | CS8 |
		CREAD | HUPCL | CLOCAL;
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
	tty_unregister_driver(gb_tty_driver);
	put_tty_driver(gb_tty_driver);
	idr_destroy(&tty_minors);
}

static const struct gbphy_device_id gb_uart_id_table[] = {
	{ GBPHY_PROTOCOL(GREYBUS_PROTOCOL_UART) },
	{ },
};
MODULE_DEVICE_TABLE(gbphy, gb_uart_id_table);

static struct gbphy_driver uart_driver = {
	.name		= "uart",
	.probe		= gb_uart_probe,
	.remove		= gb_uart_remove,
	.id_table	= gb_uart_id_table,
};

static int gb_uart_driver_init(void)
{
	int ret;

	ret = gb_tty_init();
	if (ret)
		return ret;

	ret = gb_gbphy_register(&uart_driver);
	if (ret) {
		gb_tty_exit();
		return ret;
	}

	return 0;
}
module_init(gb_uart_driver_init);

static void gb_uart_driver_exit(void)
{
	gb_gbphy_deregister(&uart_driver);
	gb_tty_exit();
}

module_exit(gb_uart_driver_exit);
MODULE_LICENSE("GPL v2");
