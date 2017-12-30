// SPDX-License-Identifier: GPL-2.0+
/*
*  Digi AccelePort USB-4 and USB-2 Serial Converters
*
*  Copyright 2000 by Digi International
*
*  Shamelessly based on Brian Warner's keyspan_pda.c and Greg Kroah-Hartman's
*  usb-serial driver.
*
*  Peter Berger (pberger@brimson.com)
*  Al Borchers (borchers@steinerpoint.com)
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include <linux/usb/serial.h>

/* Defines */

#define DRIVER_AUTHOR "Peter Berger <pberger@brimson.com>, Al Borchers <borchers@steinerpoint.com>"
#define DRIVER_DESC "Digi AccelePort USB-2/USB-4 Serial Converter driver"

/* port output buffer length -- must be <= transfer buffer length - 2 */
/* so we can be sure to send the full buffer in one urb */
#define DIGI_OUT_BUF_SIZE		8

/* port input buffer length -- must be >= transfer buffer length - 3 */
/* so we can be sure to hold at least one full buffer from one urb */
#define DIGI_IN_BUF_SIZE		64

/* retry timeout while sleeping */
#define DIGI_RETRY_TIMEOUT		(HZ/10)

/* timeout while waiting for tty output to drain in close */
/* this delay is used twice in close, so the total delay could */
/* be twice this value */
#define DIGI_CLOSE_TIMEOUT		(5*HZ)


/* AccelePort USB Defines */

/* ids */
#define DIGI_VENDOR_ID			0x05c5
#define DIGI_2_ID			0x0002	/* USB-2 */
#define DIGI_4_ID			0x0004	/* USB-4 */

/* commands
 * "INB": can be used on the in-band endpoint
 * "OOB": can be used on the out-of-band endpoint
 */
#define DIGI_CMD_SET_BAUD_RATE			0	/* INB, OOB */
#define DIGI_CMD_SET_WORD_SIZE			1	/* INB, OOB */
#define DIGI_CMD_SET_PARITY			2	/* INB, OOB */
#define DIGI_CMD_SET_STOP_BITS			3	/* INB, OOB */
#define DIGI_CMD_SET_INPUT_FLOW_CONTROL		4	/* INB, OOB */
#define DIGI_CMD_SET_OUTPUT_FLOW_CONTROL	5	/* INB, OOB */
#define DIGI_CMD_SET_DTR_SIGNAL			6	/* INB, OOB */
#define DIGI_CMD_SET_RTS_SIGNAL			7	/* INB, OOB */
#define DIGI_CMD_READ_INPUT_SIGNALS		8	/*      OOB */
#define DIGI_CMD_IFLUSH_FIFO			9	/*      OOB */
#define DIGI_CMD_RECEIVE_ENABLE			10	/* INB, OOB */
#define DIGI_CMD_BREAK_CONTROL			11	/* INB, OOB */
#define DIGI_CMD_LOCAL_LOOPBACK			12	/* INB, OOB */
#define DIGI_CMD_TRANSMIT_IDLE			13	/* INB, OOB */
#define DIGI_CMD_READ_UART_REGISTER		14	/*      OOB */
#define DIGI_CMD_WRITE_UART_REGISTER		15	/* INB, OOB */
#define DIGI_CMD_AND_UART_REGISTER		16	/* INB, OOB */
#define DIGI_CMD_OR_UART_REGISTER		17	/* INB, OOB */
#define DIGI_CMD_SEND_DATA			18	/* INB      */
#define DIGI_CMD_RECEIVE_DATA			19	/* INB      */
#define DIGI_CMD_RECEIVE_DISABLE		20	/* INB      */
#define DIGI_CMD_GET_PORT_TYPE			21	/*      OOB */

/* baud rates */
#define DIGI_BAUD_50				0
#define DIGI_BAUD_75				1
#define DIGI_BAUD_110				2
#define DIGI_BAUD_150				3
#define DIGI_BAUD_200				4
#define DIGI_BAUD_300				5
#define DIGI_BAUD_600				6
#define DIGI_BAUD_1200				7
#define DIGI_BAUD_1800				8
#define DIGI_BAUD_2400				9
#define DIGI_BAUD_4800				10
#define DIGI_BAUD_7200				11
#define DIGI_BAUD_9600				12
#define DIGI_BAUD_14400				13
#define DIGI_BAUD_19200				14
#define DIGI_BAUD_28800				15
#define DIGI_BAUD_38400				16
#define DIGI_BAUD_57600				17
#define DIGI_BAUD_76800				18
#define DIGI_BAUD_115200			19
#define DIGI_BAUD_153600			20
#define DIGI_BAUD_230400			21
#define DIGI_BAUD_460800			22

/* arguments */
#define DIGI_WORD_SIZE_5			0
#define DIGI_WORD_SIZE_6			1
#define DIGI_WORD_SIZE_7			2
#define DIGI_WORD_SIZE_8			3

#define DIGI_PARITY_NONE			0
#define DIGI_PARITY_ODD				1
#define DIGI_PARITY_EVEN			2
#define DIGI_PARITY_MARK			3
#define DIGI_PARITY_SPACE			4

#define DIGI_STOP_BITS_1			0
#define DIGI_STOP_BITS_2			1

#define DIGI_INPUT_FLOW_CONTROL_XON_XOFF	1
#define DIGI_INPUT_FLOW_CONTROL_RTS		2
#define DIGI_INPUT_FLOW_CONTROL_DTR		4

#define DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF	1
#define DIGI_OUTPUT_FLOW_CONTROL_CTS		2
#define DIGI_OUTPUT_FLOW_CONTROL_DSR		4

#define DIGI_DTR_INACTIVE			0
#define DIGI_DTR_ACTIVE				1
#define DIGI_DTR_INPUT_FLOW_CONTROL		2

#define DIGI_RTS_INACTIVE			0
#define DIGI_RTS_ACTIVE				1
#define DIGI_RTS_INPUT_FLOW_CONTROL		2
#define DIGI_RTS_TOGGLE				3

#define DIGI_FLUSH_TX				1
#define DIGI_FLUSH_RX				2
#define DIGI_RESUME_TX				4 /* clears xoff condition */

#define DIGI_TRANSMIT_NOT_IDLE			0
#define DIGI_TRANSMIT_IDLE			1

#define DIGI_DISABLE				0
#define DIGI_ENABLE				1

#define DIGI_DEASSERT				0
#define DIGI_ASSERT				1

/* in band status codes */
#define DIGI_OVERRUN_ERROR			4
#define DIGI_PARITY_ERROR			8
#define DIGI_FRAMING_ERROR			16
#define DIGI_BREAK_ERROR			32

/* out of band status */
#define DIGI_NO_ERROR				0
#define DIGI_BAD_FIRST_PARAMETER		1
#define DIGI_BAD_SECOND_PARAMETER		2
#define DIGI_INVALID_LINE			3
#define DIGI_INVALID_OPCODE			4

/* input signals */
#define DIGI_READ_INPUT_SIGNALS_SLOT		1
#define DIGI_READ_INPUT_SIGNALS_ERR		2
#define DIGI_READ_INPUT_SIGNALS_BUSY		4
#define DIGI_READ_INPUT_SIGNALS_PE		8
#define DIGI_READ_INPUT_SIGNALS_CTS		16
#define DIGI_READ_INPUT_SIGNALS_DSR		32
#define DIGI_READ_INPUT_SIGNALS_RI		64
#define DIGI_READ_INPUT_SIGNALS_DCD		128


/* Structures */

struct digi_serial {
	spinlock_t ds_serial_lock;
	struct usb_serial_port *ds_oob_port;	/* out-of-band port */
	int ds_oob_port_num;			/* index of out-of-band port */
	int ds_device_started;
};

struct digi_port {
	spinlock_t dp_port_lock;
	int dp_port_num;
	int dp_out_buf_len;
	unsigned char dp_out_buf[DIGI_OUT_BUF_SIZE];
	int dp_write_urb_in_use;
	unsigned int dp_modem_signals;
	int dp_transmit_idle;
	wait_queue_head_t dp_transmit_idle_wait;
	int dp_throttled;
	int dp_throttle_restart;
	wait_queue_head_t dp_flush_wait;
	wait_queue_head_t dp_close_wait;	/* wait queue for close */
	struct work_struct dp_wakeup_work;
	struct usb_serial_port *dp_port;
};


/* Local Function Declarations */

static void digi_wakeup_write_lock(struct work_struct *work);
static int digi_write_oob_command(struct usb_serial_port *port,
	unsigned char *buf, int count, int interruptible);
static int digi_write_inb_command(struct usb_serial_port *port,
	unsigned char *buf, int count, unsigned long timeout);
static int digi_set_modem_signals(struct usb_serial_port *port,
	unsigned int modem_signals, int interruptible);
static int digi_transmit_idle(struct usb_serial_port *port,
	unsigned long timeout);
static void digi_rx_throttle(struct tty_struct *tty);
static void digi_rx_unthrottle(struct tty_struct *tty);
static void digi_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios);
static void digi_break_ctl(struct tty_struct *tty, int break_state);
static int digi_tiocmget(struct tty_struct *tty);
static int digi_tiocmset(struct tty_struct *tty, unsigned int set,
		unsigned int clear);
static int digi_write(struct tty_struct *tty, struct usb_serial_port *port,
		const unsigned char *buf, int count);
static void digi_write_bulk_callback(struct urb *urb);
static int digi_write_room(struct tty_struct *tty);
static int digi_chars_in_buffer(struct tty_struct *tty);
static int digi_open(struct tty_struct *tty, struct usb_serial_port *port);
static void digi_close(struct usb_serial_port *port);
static void digi_dtr_rts(struct usb_serial_port *port, int on);
static int digi_startup_device(struct usb_serial *serial);
static int digi_startup(struct usb_serial *serial);
static void digi_disconnect(struct usb_serial *serial);
static void digi_release(struct usb_serial *serial);
static int digi_port_probe(struct usb_serial_port *port);
static int digi_port_remove(struct usb_serial_port *port);
static void digi_read_bulk_callback(struct urb *urb);
static int digi_read_inb_callback(struct urb *urb);
static int digi_read_oob_callback(struct urb *urb);


static const struct usb_device_id id_table_combined[] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_2_ID) },
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_4_ID) },
	{ }						/* Terminating entry */
};

static const struct usb_device_id id_table_2[] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_2_ID) },
	{ }						/* Terminating entry */
};

static const struct usb_device_id id_table_4[] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_4_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, id_table_combined);

/* device info needed for the Digi serial converter */

static struct usb_serial_driver digi_acceleport_2_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"digi_2",
	},
	.description =			"Digi 2 port USB adapter",
	.id_table =			id_table_2,
	.num_ports =			3,
	.num_bulk_in =			4,
	.num_bulk_out =			4,
	.open =				digi_open,
	.close =			digi_close,
	.dtr_rts =			digi_dtr_rts,
	.write =			digi_write,
	.write_room =			digi_write_room,
	.write_bulk_callback = 		digi_write_bulk_callback,
	.read_bulk_callback =		digi_read_bulk_callback,
	.chars_in_buffer =		digi_chars_in_buffer,
	.throttle =			digi_rx_throttle,
	.unthrottle =			digi_rx_unthrottle,
	.set_termios =			digi_set_termios,
	.break_ctl =			digi_break_ctl,
	.tiocmget =			digi_tiocmget,
	.tiocmset =			digi_tiocmset,
	.attach =			digi_startup,
	.disconnect =			digi_disconnect,
	.release =			digi_release,
	.port_probe =			digi_port_probe,
	.port_remove =			digi_port_remove,
};

static struct usb_serial_driver digi_acceleport_4_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"digi_4",
	},
	.description =			"Digi 4 port USB adapter",
	.id_table =			id_table_4,
	.num_ports =			4,
	.num_bulk_in =			5,
	.num_bulk_out =			5,
	.open =				digi_open,
	.close =			digi_close,
	.write =			digi_write,
	.write_room =			digi_write_room,
	.write_bulk_callback = 		digi_write_bulk_callback,
	.read_bulk_callback =		digi_read_bulk_callback,
	.chars_in_buffer =		digi_chars_in_buffer,
	.throttle =			digi_rx_throttle,
	.unthrottle =			digi_rx_unthrottle,
	.set_termios =			digi_set_termios,
	.break_ctl =			digi_break_ctl,
	.tiocmget =			digi_tiocmget,
	.tiocmset =			digi_tiocmset,
	.attach =			digi_startup,
	.disconnect =			digi_disconnect,
	.release =			digi_release,
	.port_probe =			digi_port_probe,
	.port_remove =			digi_port_remove,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&digi_acceleport_2_device, &digi_acceleport_4_device, NULL
};

/* Functions */

/*
 *  Cond Wait Interruptible Timeout Irqrestore
 *
 *  Do spin_unlock_irqrestore and interruptible_sleep_on_timeout
 *  so that wake ups are not lost if they occur between the unlock
 *  and the sleep.  In other words, spin_unlock_irqrestore and
 *  interruptible_sleep_on_timeout are "atomic" with respect to
 *  wake ups.  This is used to implement condition variables.
 *
 *  interruptible_sleep_on_timeout is deprecated and has been replaced
 *  with the equivalent code.
 */

static long cond_wait_interruptible_timeout_irqrestore(
	wait_queue_head_t *q, long timeout,
	spinlock_t *lock, unsigned long flags)
__releases(lock)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(q, &wait, TASK_INTERRUPTIBLE);
	spin_unlock_irqrestore(lock, flags);
	timeout = schedule_timeout(timeout);
	finish_wait(q, &wait);

	return timeout;
}


/*
 *  Digi Wakeup Write
 *
 *  Wake up port, line discipline, and tty processes sleeping
 *  on writes.
 */

static void digi_wakeup_write_lock(struct work_struct *work)
{
	struct digi_port *priv =
			container_of(work, struct digi_port, dp_wakeup_work);
	struct usb_serial_port *port = priv->dp_port;
	unsigned long flags;

	spin_lock_irqsave(&priv->dp_port_lock, flags);
	tty_port_tty_wakeup(&port->port);
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
}

/*
 *  Digi Write OOB Command
 *
 *  Write commands on the out of band port.  Commands are 4
 *  bytes each, multiple commands can be sent at once, and
 *  no command will be split across USB packets.  Returns 0
 *  if successful, -EINTR if interrupted while sleeping and
 *  the interruptible flag is true, or a negative error
 *  returned by usb_submit_urb.
 */

static int digi_write_oob_command(struct usb_serial_port *port,
	unsigned char *buf, int count, int interruptible)
{
	int ret = 0;
	int len;
	struct usb_serial_port *oob_port = (struct usb_serial_port *)((struct digi_serial *)(usb_get_serial_data(port->serial)))->ds_oob_port;
	struct digi_port *oob_priv = usb_get_serial_port_data(oob_port);
	unsigned long flags = 0;

	dev_dbg(&port->dev,
		"digi_write_oob_command: TOP: port=%d, count=%d\n",
		oob_priv->dp_port_num, count);

	spin_lock_irqsave(&oob_priv->dp_port_lock, flags);
	while (count > 0) {
		while (oob_priv->dp_write_urb_in_use) {
			cond_wait_interruptible_timeout_irqrestore(
				&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
				&oob_priv->dp_port_lock, flags);
			if (interruptible && signal_pending(current))
				return -EINTR;
			spin_lock_irqsave(&oob_priv->dp_port_lock, flags);
		}

		/* len must be a multiple of 4, so commands are not split */
		len = min(count, oob_port->bulk_out_size);
		if (len > 4)
			len &= ~3;
		memcpy(oob_port->write_urb->transfer_buffer, buf, len);
		oob_port->write_urb->transfer_buffer_length = len;
		ret = usb_submit_urb(oob_port->write_urb, GFP_ATOMIC);
		if (ret == 0) {
			oob_priv->dp_write_urb_in_use = 1;
			count -= len;
			buf += len;
		}
	}
	spin_unlock_irqrestore(&oob_priv->dp_port_lock, flags);
	if (ret)
		dev_err(&port->dev, "%s: usb_submit_urb failed, ret=%d\n",
			__func__, ret);
	return ret;

}


/*
 *  Digi Write In Band Command
 *
 *  Write commands on the given port.  Commands are 4
 *  bytes each, multiple commands can be sent at once, and
 *  no command will be split across USB packets.  If timeout
 *  is non-zero, write in band command will return after
 *  waiting unsuccessfully for the URB status to clear for
 *  timeout ticks.  Returns 0 if successful, or a negative
 *  error returned by digi_write.
 */

static int digi_write_inb_command(struct usb_serial_port *port,
	unsigned char *buf, int count, unsigned long timeout)
{
	int ret = 0;
	int len;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned long flags = 0;

	dev_dbg(&port->dev, "digi_write_inb_command: TOP: port=%d, count=%d\n",
		priv->dp_port_num, count);

	if (timeout)
		timeout += jiffies;
	else
		timeout = ULONG_MAX;

	spin_lock_irqsave(&priv->dp_port_lock, flags);
	while (count > 0 && ret == 0) {
		while (priv->dp_write_urb_in_use &&
		       time_before(jiffies, timeout)) {
			cond_wait_interruptible_timeout_irqrestore(
				&port->write_wait, DIGI_RETRY_TIMEOUT,
				&priv->dp_port_lock, flags);
			if (signal_pending(current))
				return -EINTR;
			spin_lock_irqsave(&priv->dp_port_lock, flags);
		}

		/* len must be a multiple of 4 and small enough to */
		/* guarantee the write will send buffered data first, */
		/* so commands are in order with data and not split */
		len = min(count, port->bulk_out_size-2-priv->dp_out_buf_len);
		if (len > 4)
			len &= ~3;

		/* write any buffered data first */
		if (priv->dp_out_buf_len > 0) {
			data[0] = DIGI_CMD_SEND_DATA;
			data[1] = priv->dp_out_buf_len;
			memcpy(data + 2, priv->dp_out_buf,
				priv->dp_out_buf_len);
			memcpy(data + 2 + priv->dp_out_buf_len, buf, len);
			port->write_urb->transfer_buffer_length
				= priv->dp_out_buf_len + 2 + len;
		} else {
			memcpy(data, buf, len);
			port->write_urb->transfer_buffer_length = len;
		}

		ret = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (ret == 0) {
			priv->dp_write_urb_in_use = 1;
			priv->dp_out_buf_len = 0;
			count -= len;
			buf += len;
		}

	}
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);

	if (ret)
		dev_err(&port->dev,
			"%s: usb_submit_urb failed, ret=%d, port=%d\n",
			__func__, ret, priv->dp_port_num);
	return ret;
}


/*
 *  Digi Set Modem Signals
 *
 *  Sets or clears DTR and RTS on the port, according to the
 *  modem_signals argument.  Use TIOCM_DTR and TIOCM_RTS flags
 *  for the modem_signals argument.  Returns 0 if successful,
 *  -EINTR if interrupted while sleeping, or a non-zero error
 *  returned by usb_submit_urb.
 */

static int digi_set_modem_signals(struct usb_serial_port *port,
	unsigned int modem_signals, int interruptible)
{

	int ret;
	struct digi_port *port_priv = usb_get_serial_port_data(port);
	struct usb_serial_port *oob_port = (struct usb_serial_port *) ((struct digi_serial *)(usb_get_serial_data(port->serial)))->ds_oob_port;
	struct digi_port *oob_priv = usb_get_serial_port_data(oob_port);
	unsigned char *data = oob_port->write_urb->transfer_buffer;
	unsigned long flags = 0;


	dev_dbg(&port->dev,
		"digi_set_modem_signals: TOP: port=%d, modem_signals=0x%x\n",
		port_priv->dp_port_num, modem_signals);

	spin_lock_irqsave(&oob_priv->dp_port_lock, flags);
	spin_lock(&port_priv->dp_port_lock);

	while (oob_priv->dp_write_urb_in_use) {
		spin_unlock(&port_priv->dp_port_lock);
		cond_wait_interruptible_timeout_irqrestore(
			&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
			&oob_priv->dp_port_lock, flags);
		if (interruptible && signal_pending(current))
			return -EINTR;
		spin_lock_irqsave(&oob_priv->dp_port_lock, flags);
		spin_lock(&port_priv->dp_port_lock);
	}
	data[0] = DIGI_CMD_SET_DTR_SIGNAL;
	data[1] = port_priv->dp_port_num;
	data[2] = (modem_signals & TIOCM_DTR) ?
					DIGI_DTR_ACTIVE : DIGI_DTR_INACTIVE;
	data[3] = 0;
	data[4] = DIGI_CMD_SET_RTS_SIGNAL;
	data[5] = port_priv->dp_port_num;
	data[6] = (modem_signals & TIOCM_RTS) ?
					DIGI_RTS_ACTIVE : DIGI_RTS_INACTIVE;
	data[7] = 0;

	oob_port->write_urb->transfer_buffer_length = 8;

	ret = usb_submit_urb(oob_port->write_urb, GFP_ATOMIC);
	if (ret == 0) {
		oob_priv->dp_write_urb_in_use = 1;
		port_priv->dp_modem_signals =
			(port_priv->dp_modem_signals&~(TIOCM_DTR|TIOCM_RTS))
			| (modem_signals&(TIOCM_DTR|TIOCM_RTS));
	}
	spin_unlock(&port_priv->dp_port_lock);
	spin_unlock_irqrestore(&oob_priv->dp_port_lock, flags);
	if (ret)
		dev_err(&port->dev, "%s: usb_submit_urb failed, ret=%d\n",
			__func__, ret);
	return ret;
}

/*
 *  Digi Transmit Idle
 *
 *  Digi transmit idle waits, up to timeout ticks, for the transmitter
 *  to go idle.  It returns 0 if successful or a negative error.
 *
 *  There are race conditions here if more than one process is calling
 *  digi_transmit_idle on the same port at the same time.  However, this
 *  is only called from close, and only one process can be in close on a
 *  port at a time, so its ok.
 */

static int digi_transmit_idle(struct usb_serial_port *port,
	unsigned long timeout)
{
	int ret;
	unsigned char buf[2];
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->dp_port_lock, flags);
	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);

	buf[0] = DIGI_CMD_TRANSMIT_IDLE;
	buf[1] = 0;

	timeout += jiffies;

	ret = digi_write_inb_command(port, buf, 2, timeout - jiffies);
	if (ret != 0)
		return ret;

	spin_lock_irqsave(&priv->dp_port_lock, flags);

	while (time_before(jiffies, timeout) && !priv->dp_transmit_idle) {
		cond_wait_interruptible_timeout_irqrestore(
			&priv->dp_transmit_idle_wait, DIGI_RETRY_TIMEOUT,
			&priv->dp_port_lock, flags);
		if (signal_pending(current))
			return -EINTR;
		spin_lock_irqsave(&priv->dp_port_lock, flags);
	}
	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
	return 0;

}


static void digi_rx_throttle(struct tty_struct *tty)
{
	unsigned long flags;
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);

	/* stop receiving characters by not resubmitting the read urb */
	spin_lock_irqsave(&priv->dp_port_lock, flags);
	priv->dp_throttled = 1;
	priv->dp_throttle_restart = 0;
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
}


static void digi_rx_unthrottle(struct tty_struct *tty)
{
	int ret = 0;
	unsigned long flags;
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);

	spin_lock_irqsave(&priv->dp_port_lock, flags);

	/* restart read chain */
	if (priv->dp_throttle_restart)
		ret = usb_submit_urb(port->read_urb, GFP_ATOMIC);

	/* turn throttle off */
	priv->dp_throttled = 0;
	priv->dp_throttle_restart = 0;

	spin_unlock_irqrestore(&priv->dp_port_lock, flags);

	if (ret)
		dev_err(&port->dev,
			"%s: usb_submit_urb failed, ret=%d, port=%d\n",
			__func__, ret, priv->dp_port_num);
}


static void digi_set_termios(struct tty_struct *tty,
		struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct digi_port *priv = usb_get_serial_port_data(port);
	struct device *dev = &port->dev;
	unsigned int iflag = tty->termios.c_iflag;
	unsigned int cflag = tty->termios.c_cflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int old_cflag = old_termios->c_cflag;
	unsigned char buf[32];
	unsigned int modem_signals;
	int arg, ret;
	int i = 0;
	speed_t baud;

	dev_dbg(dev,
		"digi_set_termios: TOP: port=%d, iflag=0x%x, old_iflag=0x%x, cflag=0x%x, old_cflag=0x%x\n",
		priv->dp_port_num, iflag, old_iflag, cflag, old_cflag);

	/* set baud rate */
	baud = tty_get_baud_rate(tty);
	if (baud != tty_termios_baud_rate(old_termios)) {
		arg = -1;

		/* reassert DTR and (maybe) RTS on transition from B0 */
		if ((old_cflag & CBAUD) == B0) {
			/* don't set RTS if using hardware flow control */
			/* and throttling input */
			modem_signals = TIOCM_DTR;
			if (!C_CRTSCTS(tty) || !tty_throttled(tty))
				modem_signals |= TIOCM_RTS;
			digi_set_modem_signals(port, modem_signals, 1);
		}
		switch (baud) {
		/* drop DTR and RTS on transition to B0 */
		case 0: digi_set_modem_signals(port, 0, 1); break;
		case 50: arg = DIGI_BAUD_50; break;
		case 75: arg = DIGI_BAUD_75; break;
		case 110: arg = DIGI_BAUD_110; break;
		case 150: arg = DIGI_BAUD_150; break;
		case 200: arg = DIGI_BAUD_200; break;
		case 300: arg = DIGI_BAUD_300; break;
		case 600: arg = DIGI_BAUD_600; break;
		case 1200: arg = DIGI_BAUD_1200; break;
		case 1800: arg = DIGI_BAUD_1800; break;
		case 2400: arg = DIGI_BAUD_2400; break;
		case 4800: arg = DIGI_BAUD_4800; break;
		case 9600: arg = DIGI_BAUD_9600; break;
		case 19200: arg = DIGI_BAUD_19200; break;
		case 38400: arg = DIGI_BAUD_38400; break;
		case 57600: arg = DIGI_BAUD_57600; break;
		case 115200: arg = DIGI_BAUD_115200; break;
		case 230400: arg = DIGI_BAUD_230400; break;
		case 460800: arg = DIGI_BAUD_460800; break;
		default:
			arg = DIGI_BAUD_9600;
			baud = 9600;
			break;
		}
		if (arg != -1) {
			buf[i++] = DIGI_CMD_SET_BAUD_RATE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}
	}
	/* set parity */
	tty->termios.c_cflag &= ~CMSPAR;

	if ((cflag&(PARENB|PARODD)) != (old_cflag&(PARENB|PARODD))) {
		if (cflag&PARENB) {
			if (cflag&PARODD)
				arg = DIGI_PARITY_ODD;
			else
				arg = DIGI_PARITY_EVEN;
		} else {
			arg = DIGI_PARITY_NONE;
		}
		buf[i++] = DIGI_CMD_SET_PARITY;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;
	}
	/* set word size */
	if ((cflag&CSIZE) != (old_cflag&CSIZE)) {
		arg = -1;
		switch (cflag&CSIZE) {
		case CS5: arg = DIGI_WORD_SIZE_5; break;
		case CS6: arg = DIGI_WORD_SIZE_6; break;
		case CS7: arg = DIGI_WORD_SIZE_7; break;
		case CS8: arg = DIGI_WORD_SIZE_8; break;
		default:
			dev_dbg(dev,
				"digi_set_termios: can't handle word size %d\n",
				(cflag&CSIZE));
			break;
		}

		if (arg != -1) {
			buf[i++] = DIGI_CMD_SET_WORD_SIZE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}

	}

	/* set stop bits */
	if ((cflag&CSTOPB) != (old_cflag&CSTOPB)) {

		if ((cflag&CSTOPB))
			arg = DIGI_STOP_BITS_2;
		else
			arg = DIGI_STOP_BITS_1;

		buf[i++] = DIGI_CMD_SET_STOP_BITS;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set input flow control */
	if ((iflag&IXOFF) != (old_iflag&IXOFF)
	    || (cflag&CRTSCTS) != (old_cflag&CRTSCTS)) {
		arg = 0;
		if (iflag&IXOFF)
			arg |= DIGI_INPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_INPUT_FLOW_CONTROL_XON_XOFF;

		if (cflag&CRTSCTS) {
			arg |= DIGI_INPUT_FLOW_CONTROL_RTS;

			/* On USB-4 it is necessary to assert RTS prior */
			/* to selecting RTS input flow control.  */
			buf[i++] = DIGI_CMD_SET_RTS_SIGNAL;
			buf[i++] = priv->dp_port_num;
			buf[i++] = DIGI_RTS_ACTIVE;
			buf[i++] = 0;

		} else {
			arg &= ~DIGI_INPUT_FLOW_CONTROL_RTS;
		}
		buf[i++] = DIGI_CMD_SET_INPUT_FLOW_CONTROL;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;
	}

	/* set output flow control */
	if ((iflag & IXON) != (old_iflag & IXON)
	    || (cflag & CRTSCTS) != (old_cflag & CRTSCTS)) {
		arg = 0;
		if (iflag & IXON)
			arg |= DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;

		if (cflag & CRTSCTS) {
			arg |= DIGI_OUTPUT_FLOW_CONTROL_CTS;
		} else {
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_CTS;
		}

		buf[i++] = DIGI_CMD_SET_OUTPUT_FLOW_CONTROL;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;
	}

	/* set receive enable/disable */
	if ((cflag & CREAD) != (old_cflag & CREAD)) {
		if (cflag & CREAD)
			arg = DIGI_ENABLE;
		else
			arg = DIGI_DISABLE;

		buf[i++] = DIGI_CMD_RECEIVE_ENABLE;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;
	}
	ret = digi_write_oob_command(port, buf, i, 1);
	if (ret != 0)
		dev_dbg(dev, "digi_set_termios: write oob failed, ret=%d\n", ret);
	tty_encode_baud_rate(tty, baud, baud);
}


static void digi_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	unsigned char buf[4];

	buf[0] = DIGI_CMD_BREAK_CONTROL;
	buf[1] = 2;				/* length */
	buf[2] = break_state ? 1 : 0;
	buf[3] = 0;				/* pad */
	digi_write_inb_command(port, buf, 4, 0);
}


static int digi_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&priv->dp_port_lock, flags);
	val = priv->dp_modem_signals;
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
	return val;
}


static int digi_tiocmset(struct tty_struct *tty,
					unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned int val;
	unsigned long flags;

	spin_lock_irqsave(&priv->dp_port_lock, flags);
	val = (priv->dp_modem_signals & ~clear) | set;
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
	return digi_set_modem_signals(port, val, 1);
}


static int digi_write(struct tty_struct *tty, struct usb_serial_port *port,
					const unsigned char *buf, int count)
{

	int ret, data_len, new_len;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned long flags = 0;

	dev_dbg(&port->dev,
		"digi_write: TOP: port=%d, count=%d, in_interrupt=%ld\n",
		priv->dp_port_num, count, in_interrupt());

	/* copy user data (which can sleep) before getting spin lock */
	count = min(count, port->bulk_out_size-2);
	count = min(64, count);

	/* be sure only one write proceeds at a time */
	/* there are races on the port private buffer */
	spin_lock_irqsave(&priv->dp_port_lock, flags);

	/* wait for urb status clear to submit another urb */
	if (priv->dp_write_urb_in_use) {
		/* buffer data if count is 1 (probably put_char) if possible */
		if (count == 1 && priv->dp_out_buf_len < DIGI_OUT_BUF_SIZE) {
			priv->dp_out_buf[priv->dp_out_buf_len++] = *buf;
			new_len = 1;
		} else {
			new_len = 0;
		}
		spin_unlock_irqrestore(&priv->dp_port_lock, flags);
		return new_len;
	}

	/* allow space for any buffered data and for new data, up to */
	/* transfer buffer size - 2 (for command and length bytes) */
	new_len = min(count, port->bulk_out_size-2-priv->dp_out_buf_len);
	data_len = new_len + priv->dp_out_buf_len;

	if (data_len == 0) {
		spin_unlock_irqrestore(&priv->dp_port_lock, flags);
		return 0;
	}

	port->write_urb->transfer_buffer_length = data_len+2;

	*data++ = DIGI_CMD_SEND_DATA;
	*data++ = data_len;

	/* copy in buffered data first */
	memcpy(data, priv->dp_out_buf, priv->dp_out_buf_len);
	data += priv->dp_out_buf_len;

	/* copy in new data */
	memcpy(data, buf, new_len);

	ret = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (ret == 0) {
		priv->dp_write_urb_in_use = 1;
		ret = new_len;
		priv->dp_out_buf_len = 0;
	}

	/* return length of new data written, or error */
	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
	if (ret < 0)
		dev_err_console(port,
			"%s: usb_submit_urb failed, ret=%d, port=%d\n",
			__func__, ret, priv->dp_port_num);
	dev_dbg(&port->dev, "digi_write: returning %d\n", ret);
	return ret;

}

static void digi_write_bulk_callback(struct urb *urb)
{

	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial;
	struct digi_port *priv;
	struct digi_serial *serial_priv;
	int ret = 0;
	int status = urb->status;

	/* port and serial sanity check */
	if (port == NULL || (priv = usb_get_serial_port_data(port)) == NULL) {
		pr_err("%s: port or port->private is NULL, status=%d\n",
			__func__, status);
		return;
	}
	serial = port->serial;
	if (serial == NULL || (serial_priv = usb_get_serial_data(serial)) == NULL) {
		dev_err(&port->dev,
			"%s: serial or serial->private is NULL, status=%d\n",
			__func__, status);
		return;
	}

	/* handle oob callback */
	if (priv->dp_port_num == serial_priv->ds_oob_port_num) {
		dev_dbg(&port->dev, "digi_write_bulk_callback: oob callback\n");
		spin_lock(&priv->dp_port_lock);
		priv->dp_write_urb_in_use = 0;
		wake_up_interruptible(&port->write_wait);
		spin_unlock(&priv->dp_port_lock);
		return;
	}

	/* try to send any buffered data on this port */
	spin_lock(&priv->dp_port_lock);
	priv->dp_write_urb_in_use = 0;
	if (priv->dp_out_buf_len > 0) {
		*((unsigned char *)(port->write_urb->transfer_buffer))
			= (unsigned char)DIGI_CMD_SEND_DATA;
		*((unsigned char *)(port->write_urb->transfer_buffer) + 1)
			= (unsigned char)priv->dp_out_buf_len;
		port->write_urb->transfer_buffer_length =
						priv->dp_out_buf_len + 2;
		memcpy(port->write_urb->transfer_buffer + 2, priv->dp_out_buf,
			priv->dp_out_buf_len);
		ret = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (ret == 0) {
			priv->dp_write_urb_in_use = 1;
			priv->dp_out_buf_len = 0;
		}
	}
	/* wake up processes sleeping on writes immediately */
	tty_port_tty_wakeup(&port->port);
	/* also queue up a wakeup at scheduler time, in case we */
	/* lost the race in write_chan(). */
	schedule_work(&priv->dp_wakeup_work);

	spin_unlock(&priv->dp_port_lock);
	if (ret && ret != -EPERM)
		dev_err_console(port,
			"%s: usb_submit_urb failed, ret=%d, port=%d\n",
			__func__, ret, priv->dp_port_num);
}

static int digi_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);
	int room;
	unsigned long flags = 0;

	spin_lock_irqsave(&priv->dp_port_lock, flags);

	if (priv->dp_write_urb_in_use)
		room = 0;
	else
		room = port->bulk_out_size - 2 - priv->dp_out_buf_len;

	spin_unlock_irqrestore(&priv->dp_port_lock, flags);
	dev_dbg(&port->dev, "digi_write_room: port=%d, room=%d\n", priv->dp_port_num, room);
	return room;

}

static int digi_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct digi_port *priv = usb_get_serial_port_data(port);

	if (priv->dp_write_urb_in_use) {
		dev_dbg(&port->dev, "digi_chars_in_buffer: port=%d, chars=%d\n",
			priv->dp_port_num, port->bulk_out_size - 2);
		/* return(port->bulk_out_size - 2); */
		return 256;
	} else {
		dev_dbg(&port->dev, "digi_chars_in_buffer: port=%d, chars=%d\n",
			priv->dp_port_num, priv->dp_out_buf_len);
		return priv->dp_out_buf_len;
	}

}

static void digi_dtr_rts(struct usb_serial_port *port, int on)
{
	/* Adjust DTR and RTS */
	digi_set_modem_signals(port, on * (TIOCM_DTR|TIOCM_RTS), 1);
}

static int digi_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	int ret;
	unsigned char buf[32];
	struct digi_port *priv = usb_get_serial_port_data(port);
	struct ktermios not_termios;

	/* be sure the device is started up */
	if (digi_startup_device(port->serial) != 0)
		return -ENXIO;

	/* read modem signals automatically whenever they change */
	buf[0] = DIGI_CMD_READ_INPUT_SIGNALS;
	buf[1] = priv->dp_port_num;
	buf[2] = DIGI_ENABLE;
	buf[3] = 0;

	/* flush fifos */
	buf[4] = DIGI_CMD_IFLUSH_FIFO;
	buf[5] = priv->dp_port_num;
	buf[6] = DIGI_FLUSH_TX | DIGI_FLUSH_RX;
	buf[7] = 0;

	ret = digi_write_oob_command(port, buf, 8, 1);
	if (ret != 0)
		dev_dbg(&port->dev, "digi_open: write oob failed, ret=%d\n", ret);

	/* set termios settings */
	if (tty) {
		not_termios.c_cflag = ~tty->termios.c_cflag;
		not_termios.c_iflag = ~tty->termios.c_iflag;
		digi_set_termios(tty, port, &not_termios);
	}
	return 0;
}


static void digi_close(struct usb_serial_port *port)
{
	DEFINE_WAIT(wait);
	int ret;
	unsigned char buf[32];
	struct digi_port *priv = usb_get_serial_port_data(port);

	mutex_lock(&port->serial->disc_mutex);
	/* if disconnected, just clear flags */
	if (port->serial->disconnected)
		goto exit;

	/* FIXME: Transmit idle belongs in the wait_unti_sent path */
	digi_transmit_idle(port, DIGI_CLOSE_TIMEOUT);

	/* disable input flow control */
	buf[0] = DIGI_CMD_SET_INPUT_FLOW_CONTROL;
	buf[1] = priv->dp_port_num;
	buf[2] = DIGI_DISABLE;
	buf[3] = 0;

	/* disable output flow control */
	buf[4] = DIGI_CMD_SET_OUTPUT_FLOW_CONTROL;
	buf[5] = priv->dp_port_num;
	buf[6] = DIGI_DISABLE;
	buf[7] = 0;

	/* disable reading modem signals automatically */
	buf[8] = DIGI_CMD_READ_INPUT_SIGNALS;
	buf[9] = priv->dp_port_num;
	buf[10] = DIGI_DISABLE;
	buf[11] = 0;

	/* disable receive */
	buf[12] = DIGI_CMD_RECEIVE_ENABLE;
	buf[13] = priv->dp_port_num;
	buf[14] = DIGI_DISABLE;
	buf[15] = 0;

	/* flush fifos */
	buf[16] = DIGI_CMD_IFLUSH_FIFO;
	buf[17] = priv->dp_port_num;
	buf[18] = DIGI_FLUSH_TX | DIGI_FLUSH_RX;
	buf[19] = 0;

	ret = digi_write_oob_command(port, buf, 20, 0);
	if (ret != 0)
		dev_dbg(&port->dev, "digi_close: write oob failed, ret=%d\n",
									ret);
	/* wait for final commands on oob port to complete */
	prepare_to_wait(&priv->dp_flush_wait, &wait,
			TASK_INTERRUPTIBLE);
	schedule_timeout(DIGI_CLOSE_TIMEOUT);
	finish_wait(&priv->dp_flush_wait, &wait);

	/* shutdown any outstanding bulk writes */
	usb_kill_urb(port->write_urb);
exit:
	spin_lock_irq(&priv->dp_port_lock);
	priv->dp_write_urb_in_use = 0;
	wake_up_interruptible(&priv->dp_close_wait);
	spin_unlock_irq(&priv->dp_port_lock);
	mutex_unlock(&port->serial->disc_mutex);
}


/*
 *  Digi Startup Device
 *
 *  Starts reads on all ports.  Must be called AFTER startup, with
 *  urbs initialized.  Returns 0 if successful, non-zero error otherwise.
 */

static int digi_startup_device(struct usb_serial *serial)
{
	int i, ret = 0;
	struct digi_serial *serial_priv = usb_get_serial_data(serial);
	struct usb_serial_port *port;

	/* be sure this happens exactly once */
	spin_lock(&serial_priv->ds_serial_lock);
	if (serial_priv->ds_device_started) {
		spin_unlock(&serial_priv->ds_serial_lock);
		return 0;
	}
	serial_priv->ds_device_started = 1;
	spin_unlock(&serial_priv->ds_serial_lock);

	/* start reading from each bulk in endpoint for the device */
	/* set USB_DISABLE_SPD flag for write bulk urbs */
	for (i = 0; i < serial->type->num_ports + 1; i++) {
		port = serial->port[i];
		ret = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (ret != 0) {
			dev_err(&port->dev,
				"%s: usb_submit_urb failed, ret=%d, port=%d\n",
				__func__, ret, i);
			break;
		}
	}
	return ret;
}

static int digi_port_init(struct usb_serial_port *port, unsigned port_num)
{
	struct digi_port *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->dp_port_lock);
	priv->dp_port_num = port_num;
	init_waitqueue_head(&priv->dp_transmit_idle_wait);
	init_waitqueue_head(&priv->dp_flush_wait);
	init_waitqueue_head(&priv->dp_close_wait);
	INIT_WORK(&priv->dp_wakeup_work, digi_wakeup_write_lock);
	priv->dp_port = port;

	init_waitqueue_head(&port->write_wait);

	usb_set_serial_port_data(port, priv);

	return 0;
}

static int digi_startup(struct usb_serial *serial)
{
	struct digi_serial *serial_priv;
	int ret;

	serial_priv = kzalloc(sizeof(*serial_priv), GFP_KERNEL);
	if (!serial_priv)
		return -ENOMEM;

	spin_lock_init(&serial_priv->ds_serial_lock);
	serial_priv->ds_oob_port_num = serial->type->num_ports;
	serial_priv->ds_oob_port = serial->port[serial_priv->ds_oob_port_num];

	ret = digi_port_init(serial_priv->ds_oob_port,
						serial_priv->ds_oob_port_num);
	if (ret) {
		kfree(serial_priv);
		return ret;
	}

	usb_set_serial_data(serial, serial_priv);

	return 0;
}


static void digi_disconnect(struct usb_serial *serial)
{
	int i;

	/* stop reads and writes on all ports */
	for (i = 0; i < serial->type->num_ports + 1; i++) {
		usb_kill_urb(serial->port[i]->read_urb);
		usb_kill_urb(serial->port[i]->write_urb);
	}
}


static void digi_release(struct usb_serial *serial)
{
	struct digi_serial *serial_priv;
	struct digi_port *priv;

	serial_priv = usb_get_serial_data(serial);

	priv = usb_get_serial_port_data(serial_priv->ds_oob_port);
	kfree(priv);

	kfree(serial_priv);
}

static int digi_port_probe(struct usb_serial_port *port)
{
	return digi_port_init(port, port->port_number);
}

static int digi_port_remove(struct usb_serial_port *port)
{
	struct digi_port *priv;

	priv = usb_get_serial_port_data(port);
	kfree(priv);

	return 0;
}

static void digi_read_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct digi_port *priv;
	struct digi_serial *serial_priv;
	int ret;
	int status = urb->status;

	/* port sanity check, do not resubmit if port is not valid */
	if (port == NULL)
		return;
	priv = usb_get_serial_port_data(port);
	if (priv == NULL) {
		dev_err(&port->dev, "%s: port->private is NULL, status=%d\n",
			__func__, status);
		return;
	}
	if (port->serial == NULL ||
		(serial_priv = usb_get_serial_data(port->serial)) == NULL) {
		dev_err(&port->dev, "%s: serial is bad or serial->private "
			"is NULL, status=%d\n", __func__, status);
		return;
	}

	/* do not resubmit urb if it has any status error */
	if (status) {
		dev_err(&port->dev,
			"%s: nonzero read bulk status: status=%d, port=%d\n",
			__func__, status, priv->dp_port_num);
		return;
	}

	/* handle oob or inb callback, do not resubmit if error */
	if (priv->dp_port_num == serial_priv->ds_oob_port_num) {
		if (digi_read_oob_callback(urb) != 0)
			return;
	} else {
		if (digi_read_inb_callback(urb) != 0)
			return;
	}

	/* continue read */
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret != 0 && ret != -EPERM) {
		dev_err(&port->dev,
			"%s: failed resubmitting urb, ret=%d, port=%d\n",
			__func__, ret, priv->dp_port_num);
	}

}

/*
 *  Digi Read INB Callback
 *
 *  Digi Read INB Callback handles reads on the in band ports, sending
 *  the data on to the tty subsystem.  When called we know port and
 *  port->private are not NULL and port->serial has been validated.
 *  It returns 0 if successful, 1 if successful but the port is
 *  throttled, and -1 if the sanity checks failed.
 */

static int digi_read_inb_callback(struct urb *urb)
{
	struct usb_serial_port *port = urb->context;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *buf = urb->transfer_buffer;
	int opcode;
	int len;
	int port_status;
	unsigned char *data;
	int flag, throttled;

	/* short/multiple packet check */
	if (urb->actual_length < 2) {
		dev_warn(&port->dev, "short packet received\n");
		return -1;
	}

	opcode = buf[0];
	len = buf[1];

	if (urb->actual_length != len + 2) {
		dev_err(&port->dev, "malformed packet received: port=%d, opcode=%d, len=%d, actual_length=%u\n",
			priv->dp_port_num, opcode, len, urb->actual_length);
		return -1;
	}

	if (opcode == DIGI_CMD_RECEIVE_DATA && len < 1) {
		dev_err(&port->dev, "malformed data packet received\n");
		return -1;
	}

	spin_lock(&priv->dp_port_lock);

	/* check for throttle; if set, do not resubmit read urb */
	/* indicate the read chain needs to be restarted on unthrottle */
	throttled = priv->dp_throttled;
	if (throttled)
		priv->dp_throttle_restart = 1;

	/* receive data */
	if (opcode == DIGI_CMD_RECEIVE_DATA) {
		port_status = buf[2];
		data = &buf[3];

		/* get flag from port_status */
		flag = 0;

		/* overrun is special, not associated with a char */
		if (port_status & DIGI_OVERRUN_ERROR)
			tty_insert_flip_char(&port->port, 0, TTY_OVERRUN);

		/* break takes precedence over parity, */
		/* which takes precedence over framing errors */
		if (port_status & DIGI_BREAK_ERROR)
			flag = TTY_BREAK;
		else if (port_status & DIGI_PARITY_ERROR)
			flag = TTY_PARITY;
		else if (port_status & DIGI_FRAMING_ERROR)
			flag = TTY_FRAME;

		/* data length is len-1 (one byte of len is port_status) */
		--len;
		if (len > 0) {
			tty_insert_flip_string_fixed_flag(&port->port, data,
					flag, len);
			tty_flip_buffer_push(&port->port);
		}
	}
	spin_unlock(&priv->dp_port_lock);

	if (opcode == DIGI_CMD_RECEIVE_DISABLE)
		dev_dbg(&port->dev, "%s: got RECEIVE_DISABLE\n", __func__);
	else if (opcode != DIGI_CMD_RECEIVE_DATA)
		dev_dbg(&port->dev, "%s: unknown opcode: %d\n", __func__, opcode);

	return throttled ? 1 : 0;

}


/*
 *  Digi Read OOB Callback
 *
 *  Digi Read OOB Callback handles reads on the out of band port.
 *  When called we know port and port->private are not NULL and
 *  the port->serial is valid.  It returns 0 if successful, and
 *  -1 if the sanity checks failed.
 */

static int digi_read_oob_callback(struct urb *urb)
{

	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *buf = urb->transfer_buffer;
	int opcode, line, status, val;
	int i;
	unsigned int rts;

	if (urb->actual_length < 4)
		return -1;

	/* handle each oob command */
	for (i = 0; i < urb->actual_length - 3; i += 4) {
		opcode = buf[i];
		line = buf[i + 1];
		status = buf[i + 2];
		val = buf[i + 3];

		dev_dbg(&port->dev, "digi_read_oob_callback: opcode=%d, line=%d, status=%d, val=%d\n",
			opcode, line, status, val);

		if (status != 0 || line >= serial->type->num_ports)
			continue;

		port = serial->port[line];

		priv = usb_get_serial_port_data(port);
		if (priv == NULL)
			return -1;

		tty = tty_port_tty_get(&port->port);

		rts = 0;
		if (tty)
			rts = C_CRTSCTS(tty);

		if (tty && opcode == DIGI_CMD_READ_INPUT_SIGNALS) {
			spin_lock(&priv->dp_port_lock);
			/* convert from digi flags to termiox flags */
			if (val & DIGI_READ_INPUT_SIGNALS_CTS) {
				priv->dp_modem_signals |= TIOCM_CTS;
				/* port must be open to use tty struct */
				if (rts)
					tty_port_tty_wakeup(&port->port);
			} else {
				priv->dp_modem_signals &= ~TIOCM_CTS;
				/* port must be open to use tty struct */
			}
			if (val & DIGI_READ_INPUT_SIGNALS_DSR)
				priv->dp_modem_signals |= TIOCM_DSR;
			else
				priv->dp_modem_signals &= ~TIOCM_DSR;
			if (val & DIGI_READ_INPUT_SIGNALS_RI)
				priv->dp_modem_signals |= TIOCM_RI;
			else
				priv->dp_modem_signals &= ~TIOCM_RI;
			if (val & DIGI_READ_INPUT_SIGNALS_DCD)
				priv->dp_modem_signals |= TIOCM_CD;
			else
				priv->dp_modem_signals &= ~TIOCM_CD;

			spin_unlock(&priv->dp_port_lock);
		} else if (opcode == DIGI_CMD_TRANSMIT_IDLE) {
			spin_lock(&priv->dp_port_lock);
			priv->dp_transmit_idle = 1;
			wake_up_interruptible(&priv->dp_transmit_idle_wait);
			spin_unlock(&priv->dp_port_lock);
		} else if (opcode == DIGI_CMD_IFLUSH_FIFO) {
			wake_up_interruptible(&priv->dp_flush_wait);
		}
		tty_kref_put(tty);
	}
	return 0;

}

module_usb_serial_driver(serial_drivers, id_table_combined);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
