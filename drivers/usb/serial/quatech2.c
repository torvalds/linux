/*
 * usb-serial driver for Quatech USB 2 devices
 *
 *
 *  These devices all have only 1 bulk in and 1 bulk out that is shared
 *  for all serial ports.
 *
 */

#include <asm/unaligned.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/serial_reg.h>
#include <linux/uaccess.h>

static bool debug;

/* default urb timeout for usb operations */
#define QT2_USB_TIMEOUT USB_CTRL_SET_TIMEOUT

#define QT_OPEN_CLOSE_CHANNEL       0xca
#define QT_SET_GET_DEVICE           0xc2
#define QT_SET_GET_REGISTER         0xc0
#define QT_GET_SET_PREBUF_TRIG_LVL  0xcc
#define QT_SET_ATF                  0xcd
#define QT_TRANSFER_IN              0xc0
#define QT_HW_FLOW_CONTROL_MASK     0xc5
#define QT_SW_FLOW_CONTROL_MASK     0xc6
#define QT2_BREAK_CONTROL	    0xc8
#define QT2_GET_SET_UART            0xc1
#define QT2_FLUSH_DEVICE	    0xc4
#define QT2_GET_SET_QMCR            0xe1
#define QT2_QMCR_RS232              0x40
#define QT2_QMCR_RS422              0x10

#define  SERIAL_CRTSCTS ((UART_MCR_RTS << 8) | UART_MSR_CTS)

#define  SERIAL_EVEN_PARITY         (UART_LCR_PARITY | UART_LCR_EPAR)

/* status bytes for the device */
#define QT2_CONTROL_BYTE    0x1b
#define QT2_LINE_STATUS     0x00  /* following 1 byte is line status */
#define QT2_MODEM_STATUS    0x01  /* following 1 byte is modem status */
#define QT2_XMIT_HOLD       0x02  /* following 2 bytes are ?? */
#define QT2_CHANGE_PORT     0x03  /* following 1 byte is port to change to */
#define QT2_REC_FLUSH       0x04  /* no following info */
#define QT2_XMIT_FLUSH      0x05  /* no following info */
#define QT2_CONTROL_ESCAPE  0xff  /* pass through previous 2 control bytes */

#define  MAX_BAUD_RATE              921600
#define  DEFAULT_BAUD_RATE          9600

#define QT2_WRITE_BUFFER_SIZE   512  /* size of write buffer */
#define QT2_WRITE_CONTROL_SIZE  5    /* control bytes used for a write */

/* Version Information */
#define DRIVER_VERSION "v0.1"
#define DRIVER_DESC "Quatech 2nd gen USB to Serial Driver"

#define	USB_VENDOR_ID_QUATECH	0x061d
#define QUATECH_SSU2_100	0xC120	/* RS232 single port */
#define QUATECH_DSU2_100	0xC140	/* RS232 dual port */
#define QUATECH_DSU2_400	0xC150	/* RS232/422/485 dual port */
#define QUATECH_QSU2_100	0xC160	/* RS232 four port */
#define QUATECH_QSU2_400	0xC170	/* RS232/422/485 four port */
#define QUATECH_ESU2_100	0xC1A0	/* RS232 eight port */
#define QUATECH_ESU2_400	0xC180	/* RS232/422/485 eight port */

struct qt2_device_detail {
	int product_id;
	int num_ports;
};

#define QT_DETAILS(prod, ports)	\
	.product_id = (prod),   \
	.num_ports = (ports)

static const struct qt2_device_detail qt2_device_details[] = {
	{QT_DETAILS(QUATECH_SSU2_100, 1)},
	{QT_DETAILS(QUATECH_DSU2_400, 2)},
	{QT_DETAILS(QUATECH_DSU2_100, 2)},
	{QT_DETAILS(QUATECH_QSU2_400, 4)},
	{QT_DETAILS(QUATECH_QSU2_100, 4)},
	{QT_DETAILS(QUATECH_ESU2_400, 8)},
	{QT_DETAILS(QUATECH_ESU2_100, 8)},
	{QT_DETAILS(0, 0)}	/* Terminating entry */
};

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_SSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU2_400)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU2_400)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU2_400)},
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

struct qt2_serial_private {
	unsigned char current_port;  /* current port for incoming data */

	struct urb	*read_urb;   /* shared among all ports */
	char		read_buffer[512];
};

struct qt2_port_private {
	bool is_open;
	u8   device_port;

	spinlock_t urb_lock;
	bool       urb_in_use;
	struct urb *write_urb;
	char       write_buffer[QT2_WRITE_BUFFER_SIZE];

	spinlock_t  lock;
	u8          shadowLSR;
	u8          shadowMSR;

	wait_queue_head_t   delta_msr_wait; /* Used for TIOCMIWAIT */
	struct async_icount icount;

	struct usb_serial_port *port;
};

static void qt2_update_lsr(struct usb_serial_port *port, unsigned char *ch);
static void qt2_update_msr(struct usb_serial_port *port, unsigned char *ch);
static void qt2_write_bulk_callback(struct urb *urb);
static void qt2_read_bulk_callback(struct urb *urb);

static void qt2_release(struct usb_serial *serial)
{
	int i;

	kfree(usb_get_serial_data(serial));

	for (i = 0; i < serial->num_ports; i++)
		kfree(usb_get_serial_port_data(serial->port[i]));
}

static inline int calc_baud_divisor(int baudrate)
{
	int divisor, rem;

	divisor = MAX_BAUD_RATE / baudrate;
	rem = MAX_BAUD_RATE % baudrate;
	/* Round to nearest divisor */
	if (((rem * 2) >= baudrate) && (baudrate != 110))
		divisor++;

	return divisor;
}

static inline int qt2_set_port_config(struct usb_device *dev,
				      unsigned char port_number,
				      u16 baudrate, u16 lcr)
{
	int divisor = calc_baud_divisor(baudrate);
	u16 index = ((u16) (lcr << 8) | (u16) (port_number));

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       QT2_GET_SET_UART, 0x40,
			       divisor, index, NULL, 0, QT2_USB_TIMEOUT);
}

static inline int qt2_control_msg(struct usb_device *dev,
				  u8 request, u16 data, u16 index)
{
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       request, 0x40, data, index,
			       NULL, 0, QT2_USB_TIMEOUT);
}

static inline int qt2_setdevice(struct usb_device *dev, u8 *data)
{
	u16 x = ((u16) (data[1] << 8) | (u16) (data[0]));

	return qt2_control_msg(dev, QT_SET_GET_DEVICE, x, 0);
}


static inline int qt2_getdevice(struct usb_device *dev, u8 *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       QT_SET_GET_DEVICE, 0xc0, 0, 0,
			       data, 3, QT2_USB_TIMEOUT);
}

static inline int qt2_getregister(struct usb_device *dev,
				  u8 uart,
				  u8 reg,
				  u8 *data)
{
	return usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			       QT_SET_GET_REGISTER, 0xc0, reg,
			       uart, data, sizeof(*data), QT2_USB_TIMEOUT);

}

static inline int qt2_setregister(struct usb_device *dev,
				  u8 uart, u8 reg, u16 data)
{
	u16 value = (data << 8) | reg;

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			       QT_SET_GET_REGISTER, 0x40, value, uart,
			       NULL, 0, QT2_USB_TIMEOUT);
}

static inline int update_mctrl(struct qt2_port_private *port_priv,
			       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = port_priv->port;
	struct usb_device *dev = port->serial->dev;
	unsigned urb_value;
	int status;

	if (((set | clear) & (TIOCM_DTR | TIOCM_RTS)) == 0) {
		dev_dbg(&port->dev,
			"update_mctrl - DTR|RTS not being set|cleared\n");
		return 0;	/* no change */
	}

	clear &= ~set;	/* 'set' takes precedence over 'clear' */
	urb_value = 0;
	if (set & TIOCM_DTR)
		urb_value |= UART_MCR_DTR;
	if (set & TIOCM_RTS)
		urb_value |= UART_MCR_RTS;

	status = qt2_setregister(dev, port_priv->device_port, UART_MCR,
				 urb_value);
	if (status < 0)
		dev_err(&port->dev,
			"update_mctrl - Error from MODEM_CTRL urb: %i\n",
			status);
	return status;
}

static int qt2_calc_num_ports(struct usb_serial *serial)
{
	struct qt2_device_detail d;
	int i;

	for (i = 0; d = qt2_device_details[i], d.product_id != 0; i++) {
		if (d.product_id == le16_to_cpu(serial->dev->descriptor.idProduct))
			return d.num_ports;
	}

	/* we didn't recognize the device */
	dev_err(&serial->dev->dev,
		 "don't know the number of ports, assuming 1\n");

	return 1;
}

static void qt2_set_termios(struct tty_struct *tty,
			    struct usb_serial_port *port,
			    struct ktermios *old_termios)
{
	struct usb_device *dev = port->serial->dev;
	struct qt2_port_private *port_priv;
	struct ktermios *termios = tty->termios;
	u16 baud;
	unsigned int cflag = termios->c_cflag;
	u16 new_lcr = 0;
	int status;

	port_priv = usb_get_serial_port_data(port);

	if (cflag & PARENB) {
		if (cflag & PARODD)
			new_lcr |= UART_LCR_PARITY;
		else
			new_lcr |= SERIAL_EVEN_PARITY;
	}

	switch (cflag & CSIZE) {
	case CS5:
		new_lcr |= UART_LCR_WLEN5;
		break;
	case CS6:
		new_lcr |= UART_LCR_WLEN6;
		break;
	case CS7:
		new_lcr |= UART_LCR_WLEN7;
		break;
	default:
	case CS8:
		new_lcr |= UART_LCR_WLEN8;
		break;
	}

	baud = tty_get_baud_rate(tty);
	if (!baud)
		baud = 9600;

	status = qt2_set_port_config(dev, port_priv->device_port, baud,
				     new_lcr);
	if (status < 0)
		dev_err(&port->dev, "%s - qt2_set_port_config failed: %i\n",
			__func__, status);

	if (cflag & CRTSCTS)
		status = qt2_control_msg(dev, QT_HW_FLOW_CONTROL_MASK,
					 SERIAL_CRTSCTS,
					 port_priv->device_port);
	else
		status = qt2_control_msg(dev, QT_HW_FLOW_CONTROL_MASK,
					 0, port_priv->device_port);
	if (status < 0)
		dev_err(&port->dev, "%s - set HW flow control failed: %i\n",
			__func__, status);

	if (I_IXOFF(tty) || I_IXON(tty)) {
		u16 x = ((u16) (START_CHAR(tty) << 8) | (u16) (STOP_CHAR(tty)));

		status = qt2_control_msg(dev, QT_SW_FLOW_CONTROL_MASK,
					 x, port_priv->device_port);
	} else
		status = qt2_control_msg(dev, QT_SW_FLOW_CONTROL_MASK,
					 0, port_priv->device_port);

	if (status < 0)
		dev_err(&port->dev, "%s - set SW flow control failed: %i\n",
			__func__, status);

}

static int qt2_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct qt2_serial_private *serial_priv;
	struct qt2_port_private *port_priv;
	u8 *data;
	u16 device_port;
	int status;
	unsigned long flags;

	device_port = (u16) (port->number - port->serial->minor);

	serial = port->serial;

	port_priv = usb_get_serial_port_data(port);
	serial_priv = usb_get_serial_data(serial);

	/* set the port to RS232 mode */
	status = qt2_control_msg(serial->dev, QT2_GET_SET_QMCR,
				 QT2_QMCR_RS232, device_port);
	if (status < 0) {
		dev_err(&port->dev,
			"%s failed to set RS232 mode for port %i error %i\n",
			__func__, device_port, status);
		return status;
	}

	data = kzalloc(2, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* open the port */
	status = usb_control_msg(serial->dev,
				 usb_rcvctrlpipe(serial->dev, 0),
				 QT_OPEN_CLOSE_CHANNEL,
				 0xc0, 0,
				 device_port, data, 2, QT2_USB_TIMEOUT);

	if (status < 0) {
		dev_err(&port->dev, "%s - open port failed %i", __func__,
			status);
		kfree(data);
		return status;
	}

	spin_lock_irqsave(&port_priv->lock, flags);
	port_priv->shadowLSR = data[0];
	port_priv->shadowMSR = data[1];
	spin_unlock_irqrestore(&port_priv->lock, flags);

	kfree(data);

	/* set to default speed and 8bit word size */
	status = qt2_set_port_config(serial->dev, device_port,
				     DEFAULT_BAUD_RATE, UART_LCR_WLEN8);
	if (status < 0) {
		dev_err(&port->dev,
			"%s - initial setup failed for port %i (%i)\n",
			__func__, port->number, device_port);
		return status;
	}

	port_priv->is_open = true;
	port_priv->device_port = (u8) device_port;

	if (tty)
		qt2_set_termios(tty, port, tty->termios);

	return 0;

}

static void qt2_close(struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct qt2_serial_private *serial_priv;
	struct qt2_port_private *port_priv;
	unsigned long flags;
	int i;

	serial = port->serial;
	serial_priv = usb_get_serial_data(serial);
	port_priv = usb_get_serial_port_data(port);

	port_priv->is_open = false;

	spin_lock_irqsave(&port_priv->urb_lock, flags);
	if (port_priv->write_urb->status == -EINPROGRESS)
		usb_kill_urb(port_priv->write_urb);
	port_priv->urb_in_use = false;
	spin_unlock_irqrestore(&port_priv->urb_lock, flags);

	/* flush the port transmit buffer */
	i = usb_control_msg(serial->dev,
			    usb_rcvctrlpipe(serial->dev, 0),
			    QT2_FLUSH_DEVICE, 0x40, 1,
			    port_priv->device_port, NULL, 0, QT2_USB_TIMEOUT);

	if (i < 0)
		dev_err(&port->dev, "%s - transmit buffer flush failed: %i\n",
			__func__, i);

	/* flush the port receive buffer */
	i = usb_control_msg(serial->dev,
			    usb_rcvctrlpipe(serial->dev, 0),
			    QT2_FLUSH_DEVICE, 0x40, 0,
			    port_priv->device_port, NULL, 0, QT2_USB_TIMEOUT);

	if (i < 0)
		dev_err(&port->dev, "%s - receive buffer flush failed: %i\n",
			__func__, i);

	/* close the port */
	i = usb_control_msg(serial->dev,
			    usb_sndctrlpipe(serial->dev, 0),
			    QT_OPEN_CLOSE_CHANNEL,
			    0x40, 0,
			    port_priv->device_port, NULL, 0, QT2_USB_TIMEOUT);

	if (i < 0)
		dev_err(&port->dev, "%s - close port failed %i\n",
			__func__, i);

}

static void qt2_disconnect(struct usb_serial *serial)
{
	struct qt2_serial_private *serial_priv = usb_get_serial_data(serial);
	struct qt2_port_private *port_priv;
	int i;

	if (serial_priv->read_urb->status == -EINPROGRESS)
		usb_kill_urb(serial_priv->read_urb);

	usb_free_urb(serial_priv->read_urb);

	for (i = 0; i < serial->num_ports; i++) {
		port_priv = usb_get_serial_port_data(serial->port[i]);

		if (port_priv->write_urb->status == -EINPROGRESS)
			usb_kill_urb(port_priv->write_urb);
		usb_free_urb(port_priv->write_urb);
	}
}

static int get_serial_info(struct usb_serial_port *port,
			   struct serial_struct __user *retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));
	tmp.line		= port->serial->minor;
	tmp.port		= 0;
	tmp.irq			= 0;
	tmp.flags		= ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size	= port->bulk_out_size;
	tmp.baud_base		= 9600;
	tmp.close_delay		= 5*HZ;
	tmp.closing_wait	= 30*HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int wait_modem_info(struct usb_serial_port *port, unsigned int arg)
{
	struct qt2_port_private *priv = usb_get_serial_port_data(port);
	struct async_icount prev, cur;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	prev = priv->icount;
	spin_unlock_irqrestore(&priv->lock, flags);

	while (1) {
		wait_event_interruptible(priv->delta_msr_wait,
					 ((priv->icount.rng != prev.rng) ||
					  (priv->icount.dsr != prev.dsr) ||
					  (priv->icount.dcd != prev.dcd) ||
					  (priv->icount.cts != prev.cts)));

		if (signal_pending(current))
			return -ERESTARTSYS;

		spin_lock_irqsave(&priv->lock, flags);
		cur = priv->icount;
		spin_unlock_irqrestore(&priv->lock, flags);

		if ((prev.rng == cur.rng) &&
		    (prev.dsr == cur.dsr) &&
		    (prev.dcd == cur.dcd) &&
		    (prev.cts == cur.cts))
			return -EIO;

		if ((arg & TIOCM_RNG && (prev.rng != cur.rng)) ||
		    (arg & TIOCM_DSR && (prev.dsr != cur.dsr)) ||
		    (arg & TIOCM_CD && (prev.dcd != cur.dcd)) ||
		    (arg & TIOCM_CTS && (prev.cts != cur.cts)))
			return 0;
	}
	return 0;
}

static int qt2_get_icount(struct tty_struct *tty,
			  struct serial_icounter_struct *icount)
{
	struct usb_serial_port *port = tty->driver_data;
	struct qt2_port_private *priv = usb_get_serial_port_data(port);
	struct async_icount cnow = priv->icount;

	icount->cts = cnow.cts;
	icount->dsr = cnow.dsr;
	icount->rng = cnow.rng;
	icount->dcd = cnow.dcd;
	icount->rx = cnow.rx;
	icount->tx = cnow.tx;
	icount->frame = cnow.frame;
	icount->overrun = cnow.overrun;
	icount->parity = cnow.parity;
	icount->brk = cnow.brk;
	icount->buf_overrun = cnow.buf_overrun;

	return 0;
}

static int qt2_ioctl(struct tty_struct *tty,
		     unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;

	switch (cmd) {
	case TIOCGSERIAL:
		return get_serial_info(port,
				       (struct serial_struct __user *)arg);

	case TIOCMIWAIT:
		return wait_modem_info(port, arg);

	default:
		break;
	}

	return -ENOIOCTLCMD;
}

static void qt2_process_status(struct usb_serial_port *port, unsigned char *ch)
{
	switch (*ch) {
	case QT2_LINE_STATUS:
		qt2_update_lsr(port, ch + 1);
		break;
	case QT2_MODEM_STATUS:
		qt2_update_msr(port, ch + 1);
		break;
	}
}

/* not needed, kept to document functionality */
static void qt2_process_xmit_empty(struct usb_serial_port *port,
				   unsigned char *ch)
{
	int bytes_written;

	bytes_written = (int)(*ch) + (int)(*(ch + 1) << 4);
}

/* not needed, kept to document functionality */
static void qt2_process_flush(struct usb_serial_port *port, unsigned char *ch)
{
	return;
}

void qt2_process_read_urb(struct urb *urb)
{
	struct usb_serial *serial;
	struct qt2_serial_private *serial_priv;
	struct usb_serial_port *port;
	struct qt2_port_private *port_priv;
	struct tty_struct *tty;
	bool escapeflag;
	unsigned char *ch;
	int i;
	unsigned char newport;
	int len = urb->actual_length;

	if (!len)
		return;

	ch = urb->transfer_buffer;
	tty = NULL;
	serial = urb->context;
	serial_priv = usb_get_serial_data(serial);
	port = serial->port[serial_priv->current_port];
	port_priv = usb_get_serial_port_data(port);

	if (port_priv->is_open)
		tty = tty_port_tty_get(&port->port);

	for (i = 0; i < urb->actual_length; i++) {
		ch = (unsigned char *)urb->transfer_buffer + i;
		if ((i <= (len - 3)) &&
		    (*ch == QT2_CONTROL_BYTE) &&
		    (*(ch + 1) == QT2_CONTROL_BYTE)) {
			escapeflag = false;
			switch (*(ch + 2)) {
			case QT2_LINE_STATUS:
			case QT2_MODEM_STATUS:
				if (i > (len - 4)) {
					dev_warn(&port->dev,
						 "%s - status message too short\n",
						__func__);
					break;
				}
				qt2_process_status(port, ch + 2);
				i += 3;
				escapeflag = true;
				break;
			case QT2_XMIT_HOLD:
				if (i > (len - 5)) {
					dev_warn(&port->dev,
						 "%s - xmit_empty message too short\n",
						 __func__);
					break;
				}
				qt2_process_xmit_empty(port, ch + 3);
				i += 4;
				escapeflag = true;
				break;
			case QT2_CHANGE_PORT:
				if (i > (len - 4)) {
					dev_warn(&port->dev,
						 "%s - change_port message too short\n",
						 __func__);
					break;
				}
				if (tty) {
					tty_flip_buffer_push(tty);
					tty_kref_put(tty);
				}

				newport = *(ch + 3);

				if (newport > serial->num_ports) {
					dev_err(&port->dev,
						"%s - port change to invalid port: %i\n",
						__func__, newport);
					break;
				}

				serial_priv->current_port = newport;
				port = serial->port[serial_priv->current_port];
				port_priv = usb_get_serial_port_data(port);
				if (port_priv->is_open)
					tty = tty_port_tty_get(&port->port);
				else
					tty = NULL;
				i += 3;
				escapeflag = true;
				break;
			case QT2_REC_FLUSH:
			case QT2_XMIT_FLUSH:
				qt2_process_flush(port, ch + 2);
				i += 2;
				escapeflag = true;
				break;
			case QT2_CONTROL_ESCAPE:
				tty_buffer_request_room(tty, 2);
				tty_insert_flip_string(tty, ch, 2);
				i += 2;
				escapeflag = true;
				break;
			default:
				dev_warn(&port->dev,
					 "%s - unsupported command %i\n",
					 __func__, *(ch + 2));
				break;
			}
			if (escapeflag)
				continue;
		}

		if (tty) {
			tty_buffer_request_room(tty, 1);
			tty_insert_flip_string(tty, ch, 1);
		}
	}

	if (tty) {
		tty_flip_buffer_push(tty);
		tty_kref_put(tty);
	}
}

static void qt2_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port;
	struct qt2_port_private *port_priv;

	port = urb->context;
	port_priv = usb_get_serial_port_data(port);

	spin_lock(&port_priv->urb_lock);

	port_priv->urb_in_use = false;
	usb_serial_port_softint(port);

	spin_unlock(&port_priv->urb_lock);

}

static void qt2_read_bulk_callback(struct urb *urb)
{
	struct usb_serial *serial = urb->context;
	int status;

	if (urb->status) {
		dev_warn(&serial->dev->dev,
			 "%s - non-zero urb status: %i\n", __func__,
			 urb->status);
		return;
	}

	qt2_process_read_urb(urb);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status != 0)
		dev_err(&serial->dev->dev,
			"%s - resubmit read urb failed: %i\n",
			__func__, status);
}

static int qt2_setup_urbs(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	struct usb_serial_port *port0;
	struct qt2_serial_private *serial_priv;
	struct qt2_port_private *port_priv;
	int pcount, status;

	port0 = serial->port[0];

	serial_priv = usb_get_serial_data(serial);
	serial_priv->read_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!serial_priv->read_urb) {
		dev_err(&serial->dev->dev, "No free urbs available\n");
		return -ENOMEM;
	}

	usb_fill_bulk_urb(serial_priv->read_urb, serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port0->bulk_in_endpointAddress),
			  serial_priv->read_buffer,
			  sizeof(serial_priv->read_buffer),
			  qt2_read_bulk_callback, serial);

	/* setup write_urb for each port */
	for (pcount = 0; pcount < serial->num_ports; pcount++) {

		port = serial->port[pcount];
		port_priv = usb_get_serial_port_data(port);

		port_priv->write_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port_priv->write_urb) {
			dev_err(&serial->dev->dev,
				"failed to alloc write_urb for port %i\n",
				pcount);
			return -ENOMEM;
		}

		usb_fill_bulk_urb(port_priv->write_urb,
				  serial->dev,
				  usb_sndbulkpipe(serial->dev,
						  port0->
						  bulk_out_endpointAddress),
				  port_priv->write_buffer,
				  sizeof(port_priv->write_buffer),
				  qt2_write_bulk_callback, port);
	}

	status = usb_submit_urb(serial_priv->read_urb, GFP_KERNEL);
	if (status != 0) {
		dev_err(&serial->dev->dev,
			"%s - submit read urb failed %i\n", __func__, status);
		return status;
	}

	return 0;

}

static int qt2_attach(struct usb_serial *serial)
{
	struct qt2_serial_private *serial_priv;
	struct qt2_port_private *port_priv;
	int status, pcount;

	/* power on unit */
	status = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 0xc2, 0x40, 0x8000, 0, NULL, 0,
				 QT2_USB_TIMEOUT);
	if (status < 0) {
		dev_err(&serial->dev->dev,
			"%s - failed to power on unit: %i\n", __func__, status);
		return status;
	}

	serial_priv = kzalloc(sizeof(*serial_priv), GFP_KERNEL);
	if (!serial_priv) {
		dev_err(&serial->dev->dev, "%s - Out of memory\n", __func__);
		return -ENOMEM;
	}

	usb_set_serial_data(serial, serial_priv);

	for (pcount = 0; pcount < serial->num_ports; pcount++) {
		port_priv = kzalloc(sizeof(*port_priv), GFP_KERNEL);
		if (!port_priv) {
			dev_err(&serial->dev->dev,
				"%s- kmalloc(%Zd) failed.\n", __func__,
				sizeof(*port_priv));
			pcount--;
			status = -ENOMEM;
			goto attach_failed;
		}

		spin_lock_init(&port_priv->lock);
		spin_lock_init(&port_priv->urb_lock);
		init_waitqueue_head(&port_priv->delta_msr_wait);

		port_priv->port = serial->port[pcount];

		usb_set_serial_port_data(serial->port[pcount], port_priv);
	}

	status = qt2_setup_urbs(serial);
	if (status != 0)
		goto attach_failed;

	return 0;

attach_failed:
	for (/* empty */; pcount >= 0; pcount--) {
		port_priv = usb_get_serial_port_data(serial->port[pcount]);
		kfree(port_priv);
	}
	kfree(serial_priv);
	return status;
}

static int qt2_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_device *dev = port->serial->dev;
	struct qt2_port_private *port_priv = usb_get_serial_port_data(port);
	u8 *d;
	int r;

	d = kzalloc(2, GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	r = qt2_getregister(dev, port_priv->device_port, UART_MCR, d);
	if (r < 0)
		goto mget_out;

	r = qt2_getregister(dev, port_priv->device_port, UART_MSR, d + 1);
	if (r < 0)
		goto mget_out;

	r = (d[0] & UART_MCR_DTR ? TIOCM_DTR : 0) |
	    (d[0] & UART_MCR_RTS ? TIOCM_RTS : 0) |
	    (d[1] & UART_MSR_CTS ? TIOCM_CTS : 0) |
	    (d[1] & UART_MSR_DCD ? TIOCM_CAR : 0) |
	    (d[1] & UART_MSR_RI ? TIOCM_RI : 0) |
	    (d[1] & UART_MSR_DSR ? TIOCM_DSR : 0);

mget_out:
	kfree(d);
	return r;
}

static int qt2_tiocmset(struct tty_struct *tty,
			unsigned int set, unsigned int clear)
{
	struct qt2_port_private *port_priv;

	port_priv = usb_get_serial_port_data(tty->driver_data);
	return update_mctrl(port_priv, set, clear);
}

static void qt2_break_ctl(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct qt2_port_private *port_priv;
	int status;
	u16 val;

	port_priv = usb_get_serial_port_data(port);

	if (!port_priv->is_open) {
		dev_err(&port->dev,
			"%s - port is not open\n", __func__);
		return;
	}

	val = (break_state == -1) ? 1 : 0;

	status = qt2_control_msg(port->serial->dev, QT2_BREAK_CONTROL,
				 val, port_priv->device_port);
	if (status < 0)
		dev_warn(&port->dev,
			 "%s - failed to send control message: %i\n", __func__,
			 status);
}



static void qt2_dtr_rts(struct usb_serial_port *port, int on)
{
	struct usb_device *dev = port->serial->dev;
	struct qt2_port_private *port_priv = usb_get_serial_port_data(port);

	mutex_lock(&port->serial->disc_mutex);
	if (!port->serial->disconnected) {
		/* Disable flow control */
		if (!on && qt2_setregister(dev, port_priv->device_port,
					   UART_MCR, 0) < 0)
			dev_warn(&port->dev, "error from flowcontrol urb\n");
		/* drop RTS and DTR */
		if (on)
			update_mctrl(port_priv, TIOCM_DTR | TIOCM_RTS, 0);
		else
			update_mctrl(port_priv, 0, TIOCM_DTR | TIOCM_RTS);
	}
	mutex_unlock(&port->serial->disc_mutex);
}

static void qt2_update_msr(struct usb_serial_port *port, unsigned char *ch)
{
	struct qt2_port_private *port_priv;
	u8 newMSR = (u8) *ch;
	unsigned long flags;

	port_priv = usb_get_serial_port_data(port);

	spin_lock_irqsave(&port_priv->lock, flags);
	port_priv->shadowMSR = newMSR;
	spin_unlock_irqrestore(&port_priv->lock, flags);

	if (newMSR & UART_MSR_ANY_DELTA) {
		/* update input line counters */
		if (newMSR & UART_MSR_DCTS)
			port_priv->icount.cts++;

		if (newMSR & UART_MSR_DDSR)
			port_priv->icount.dsr++;

		if (newMSR & UART_MSR_DDCD)
			port_priv->icount.dcd++;

		if (newMSR & UART_MSR_TERI)
			port_priv->icount.rng++;

		wake_up_interruptible(&port_priv->delta_msr_wait);
	}
}

static void qt2_update_lsr(struct usb_serial_port *port, unsigned char *ch)
{
	struct qt2_port_private *port_priv;
	struct async_icount *icount;
	unsigned long flags;
	u8 newLSR = (u8) *ch;

	port_priv = usb_get_serial_port_data(port);

	if (newLSR & UART_LSR_BI)
		newLSR &= (u8) (UART_LSR_OE | UART_LSR_BI);

	spin_lock_irqsave(&port_priv->lock, flags);
	port_priv->shadowLSR = newLSR;
	spin_unlock_irqrestore(&port_priv->lock, flags);

	icount = &port_priv->icount;

	if (newLSR & UART_LSR_BRK_ERROR_BITS) {

		if (newLSR & UART_LSR_BI)
			icount->brk++;

		if (newLSR & UART_LSR_OE)
			icount->overrun++;

		if (newLSR & UART_LSR_PE)
			icount->parity++;

		if (newLSR & UART_LSR_FE)
			icount->frame++;
	}

}

static int qt2_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct qt2_port_private *port_priv;
	unsigned long flags = 0;
	int r;

	port_priv = usb_get_serial_port_data(port);

	spin_lock_irqsave(&port_priv->urb_lock, flags);

	if (port_priv->urb_in_use)
		r = 0;
	else
		r = QT2_WRITE_BUFFER_SIZE - QT2_WRITE_CONTROL_SIZE;

	spin_unlock_irqrestore(&port_priv->urb_lock, flags);

	return r;
}

static int qt2_write(struct tty_struct *tty,
		     struct usb_serial_port *port,
		     const unsigned char *buf, int count)
{
	struct qt2_port_private *port_priv;
	struct urb *write_urb;
	unsigned char *data;
	unsigned long flags;
	int status;
	int bytes_out = 0;

	port_priv = usb_get_serial_port_data(port);

	if (port_priv->write_urb == NULL) {
		dev_err(&port->dev, "%s - no output urb\n", __func__);
		return 0;
	}
	write_urb = port_priv->write_urb;

	count = min(count, QT2_WRITE_BUFFER_SIZE - QT2_WRITE_CONTROL_SIZE);

	data = write_urb->transfer_buffer;
	spin_lock_irqsave(&port_priv->urb_lock, flags);
	if (port_priv->urb_in_use == true) {
		printk(KERN_INFO "qt2_write - urb is in use\n");
		goto write_out;
	}

	*data++ = QT2_CONTROL_BYTE;
	*data++ = QT2_CONTROL_BYTE;
	*data++ = port_priv->device_port;
	put_unaligned_le16(count, data);
	data += 2;
	memcpy(data, buf, count);

	write_urb->transfer_buffer_length = count + QT2_WRITE_CONTROL_SIZE;

	status = usb_submit_urb(write_urb, GFP_ATOMIC);
	if (status == 0) {
		port_priv->urb_in_use = true;
		bytes_out += count;
	}

write_out:
	spin_unlock_irqrestore(&port_priv->urb_lock, flags);
	return bytes_out;
}


static struct usb_serial_driver qt2_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "quatech-serial",
	},
	.description	     = DRIVER_DESC,
	.id_table	     = id_table,
	.open		     = qt2_open,
	.close		     = qt2_close,
	.write               = qt2_write,
	.write_room          = qt2_write_room,
	.calc_num_ports      = qt2_calc_num_ports,
	.attach              = qt2_attach,
	.release             = qt2_release,
	.disconnect          = qt2_disconnect,
	.dtr_rts             = qt2_dtr_rts,
	.break_ctl           = qt2_break_ctl,
	.tiocmget            = qt2_tiocmget,
	.tiocmset            = qt2_tiocmset,
	.get_icount	     = qt2_get_icount,
	.ioctl               = qt2_ioctl,
	.set_termios         = qt2_set_termios,
};

static struct usb_serial_driver *const serial_drivers[] = {
	&qt2_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
