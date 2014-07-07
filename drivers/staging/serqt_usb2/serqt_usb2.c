/*
 * This code was developed for the Quatech USB line for linux, it used
 * much of the code developed by Greg Kroah-Hartman for USB serial devices
 *
 */

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/uaccess.h>

/* Version Information */
#define DRIVER_VERSION "v2.14"
#define DRIVER_AUTHOR "Tim Gobeli, Quatech, Inc"
#define DRIVER_DESC "Quatech USB to Serial Driver"

#define	USB_VENDOR_ID_QUATECH			0x061d	/* Quatech VID */
#define QUATECH_SSU200	0xC030	/* SSU200 */
#define QUATECH_DSU100	0xC040	/* DSU100 */
#define QUATECH_DSU200	0xC050	/* DSU200 */
#define QUATECH_QSU100	0xC060	/* QSU100 */
#define QUATECH_QSU200	0xC070	/* QSU200 */
#define QUATECH_ESU100A	0xC080	/* ESU100A */
#define QUATECH_ESU100B	0xC081	/* ESU100B */
#define QUATECH_ESU200A	0xC0A0	/* ESU200A */
#define QUATECH_ESU200B	0xC0A1	/* ESU200B */
#define QUATECH_HSU100A	0xC090	/* HSU100A */
#define QUATECH_HSU100B	0xC091	/* HSU100B */
#define QUATECH_HSU100C	0xC092	/* HSU100C */
#define QUATECH_HSU100D	0xC093	/* HSU100D */
#define QUATECH_HSU200A	0xC0B0	/* HSU200A */
#define QUATECH_HSU200B	0xC0B1	/* HSU200B */
#define QUATECH_HSU200C	0xC0B2	/* HSU200C */
#define QUATECH_HSU200D	0xC0B3	/* HSU200D */

#define QT_SET_GET_DEVICE           0xc2
#define QT_OPEN_CLOSE_CHANNEL       0xca
#define QT_GET_SET_PREBUF_TRIG_LVL  0xcc
#define QT_SET_ATF                  0xcd
#define QT_GET_SET_REGISTER         0xc0
#define QT_GET_SET_UART             0xc1
#define QT_HW_FLOW_CONTROL_MASK     0xc5
#define QT_SW_FLOW_CONTROL_MASK     0xc6
#define QT_SW_FLOW_CONTROL_DISABLE  0xc7
#define QT_BREAK_CONTROL            0xc8

#define USBD_TRANSFER_DIRECTION_IN    0xc0
#define USBD_TRANSFER_DIRECTION_OUT   0x40

#define  MAX_BAUD_RATE              460800
#define  MAX_BAUD_REMAINDER         4608

#define  DIV_LATCH_LS               0x00
#define  XMT_HOLD_REGISTER          0x00
#define  XVR_BUFFER_REGISTER        0x00
#define  DIV_LATCH_MS               0x01
#define  FIFO_CONTROL_REGISTER      0x02
#define  LINE_CONTROL_REGISTER      0x03
#define  MODEM_CONTROL_REGISTER     0x04
#define  LINE_STATUS_REGISTER       0x05
#define  MODEM_STATUS_REGISTER      0x06

#define  SERIAL_MCR_DTR             0x01
#define  SERIAL_MCR_RTS             0x02
#define  SERIAL_MCR_LOOP            0x10

#define  SERIAL_MSR_CTS             0x10
#define  SERIAL_MSR_CD              0x80
#define  SERIAL_MSR_RI              0x40
#define  SERIAL_MSR_DSR             0x20
#define  SERIAL_MSR_MASK            0xf0

#define  SERIAL_8_DATA              0x03
#define  SERIAL_7_DATA              0x02
#define  SERIAL_6_DATA              0x01
#define  SERIAL_5_DATA              0x00

#define  SERIAL_ODD_PARITY          0X08
#define  SERIAL_EVEN_PARITY         0X18
#define  SERIAL_TWO_STOPB           0x04
#define  SERIAL_ONE_STOPB           0x00

#define DEFAULT_DIVISOR  0x30	/* gives 9600 baud rate */
#define DEFAULT_LCR SERIAL_8_DATA	/* 8, none , 1 */

#define FULLPWRBIT          0x00000080
#define NEXT_BOARD_POWER_BIT        0x00000004

#define SERIAL_LSR_OE       0x02
#define SERIAL_LSR_PE       0x04
#define SERIAL_LSR_FE       0x08
#define SERIAL_LSR_BI       0x10

#define  SERIAL_MSR_CTS             0x10
#define  SERIAL_MSR_CD              0x80
#define  SERIAL_MSR_RI              0x40
#define  SERIAL_MSR_DSR             0x20
#define  SERIAL_MSR_MASK            0xf0

#define PREFUFF_LEVEL_CONSERVATIVE  128
#define ATC_DISABLED                0x0

#define RR_BITS             0x03	/* for clearing clock bits */
#define DUPMODE_BITS        0xc0
#define CLKS_X4             0x02

#define LOOPMODE_BITS       0x41	/* LOOP1 = b6, LOOP0 = b0 (PORT B) */
#define ALL_LOOPBACK        0x01
#define MODEM_CTRL          0x40
#define RS232_MODE          0x00

static const struct usb_device_id id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_SSU200)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU200)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU200)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU100A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU100B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU200A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU200B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU100A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU100B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU100C)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU100D)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU200A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU200B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU200C)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_HSU200D)},
	{}			/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, id_table);

struct qt_get_device_data {
	__u8 porta;
	__u8 portb;
	__u8 portc;
};

struct qt_open_channel_data {
	__u8 line_status;
	__u8 modem_status;
};

struct quatech_port {
	int port_num;		/* number of the port */
	struct urb *write_urb;	/* write URB for this port */
	struct urb *read_urb;	/* read URB for this port */
	struct urb *int_urb;

	__u8 shadow_lcr;		/* last LCR value received */
	__u8 shadow_mcr;		/* last MCR value received */
	__u8 shadow_msr;		/* last MSR value received */
	__u8 shadow_lsr;		/* last LSR value received */
	char open_ports;

	/* Used for TIOCMIWAIT */
	wait_queue_head_t msr_wait;
	char prev_status, diff_status;

	wait_queue_head_t wait;

	struct async_icount icount;

	struct usb_serial_port *port;	/* owner of this object */
	struct qt_get_device_data device_data;
	struct mutex lock;
	bool read_urb_busy;
	int rx_holding;
	int read_bulk_stopped;
	char close_pending;
};

static int port_paranoia_check(struct usb_serial_port *port,
			       const char *function)
{
	if (!port) {
		pr_debug("%s - port == NULL", function);
		return -1;
	}
	if (!port->serial) {
		pr_debug("%s - port->serial == NULL\n", function);
		return -1;
	}

	return 0;
}

static int serial_paranoia_check(struct usb_serial *serial,
				 const char *function)
{
	if (!serial) {
		pr_debug("%s - serial == NULL\n", function);
		return -1;
	}

	if (!serial->type) {
		pr_debug("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}

static inline struct quatech_port *qt_get_port_private(struct usb_serial_port
						       *port)
{
	return (struct quatech_port *)usb_get_serial_port_data(port);
}

static inline void qt_set_port_private(struct usb_serial_port *port,
				       struct quatech_port *data)
{
	usb_set_serial_port_data(port, (void *)data);
}

static struct usb_serial *get_usb_serial(struct usb_serial_port *port,
					 const char *function)
{
	/* if no port was specified, or it fails a paranoia check */
	if (!port ||
	    port_paranoia_check(port, function) ||
	    serial_paranoia_check(port->serial, function)) {
		/*
		 * then say that we dont have a valid usb_serial thing,
		 * which will end up genrating -ENODEV return values
		 */
		return NULL;
	}

	return port->serial;
}

static void process_line_status(struct quatech_port *qt_port,
			      unsigned char line_status)
{

	qt_port->shadow_lsr =
	    line_status & (SERIAL_LSR_OE | SERIAL_LSR_PE | SERIAL_LSR_FE |
			   SERIAL_LSR_BI);
}

static void process_modem_status(struct quatech_port *qt_port,
			       unsigned char modem_status)
{

	qt_port->shadow_msr = modem_status;
	wake_up_interruptible(&qt_port->wait);
}

static void process_rx_char(struct usb_serial_port *port, unsigned char data)
{
	struct urb *urb = port->read_urb;
	if (urb->actual_length)
		tty_insert_flip_char(&port->port, data, TTY_NORMAL);
}

static void qt_write_bulk_callback(struct urb *urb)
{
	int status;
	struct quatech_port *quatech_port;

	status = urb->status;

	if (status) {
		dev_dbg(&urb->dev->dev,
			"nonzero write bulk status received:%d\n", status);
		return;
	}

	quatech_port = urb->context;

	tty_port_tty_wakeup(&quatech_port->port->port);
}

static void qt_interrupt_callback(struct urb *urb)
{
	/* FIXME */
}

static void qt_status_change_check(struct urb *urb,
				   struct quatech_port *qt_port,
				   struct usb_serial_port *port)
{
	int flag, i;
	unsigned char *data = urb->transfer_buffer;
	unsigned int rx_count = urb->actual_length;

	for (i = 0; i < rx_count; ++i) {
		/* Look ahead code here */
		if ((i <= (rx_count - 3)) && (data[i] == 0x1b)
		    && (data[i + 1] == 0x1b)) {
			flag = 0;
			switch (data[i + 2]) {
			case 0x00:
				if (i > (rx_count - 4)) {
					dev_dbg(&port->dev,
						"Illegal escape seuences in received data\n");
					break;
				}

				process_line_status(qt_port, data[i + 3]);

				i += 3;
				flag = 1;
				break;

			case 0x01:
				if (i > (rx_count - 4)) {
					dev_dbg(&port->dev,
						"Illegal escape seuences in received data\n");
					break;
				}

				process_modem_status(qt_port, data[i + 3]);

				i += 3;
				flag = 1;
				break;

			case 0xff:
				dev_dbg(&port->dev, "No status sequence.\n");

				process_rx_char(port, data[i]);
				process_rx_char(port, data[i + 1]);

				i += 2;
				break;
			}
			if (flag == 1)
				continue;
		}

		if (urb->actual_length)
			tty_insert_flip_char(&port->port, data[i], TTY_NORMAL);

	}
	tty_flip_buffer_push(&port->port);
}

static void qt_read_bulk_callback(struct urb *urb)
{

	struct usb_serial_port *port = urb->context;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port = qt_get_port_private(port);
	int result;

	if (urb->status) {
		qt_port->read_bulk_stopped = 1;
		dev_dbg(&urb->dev->dev,
			"%s - nonzero write bulk status received: %d\n",
			__func__, urb->status);
		return;
	}

	dev_dbg(&port->dev,
		"%s - port->rx_holding = %d\n", __func__, qt_port->rx_holding);

	if (port_paranoia_check(port, __func__) != 0) {
		qt_port->read_bulk_stopped = 1;
		return;
	}

	if (!serial)
		return;

	if (qt_port->close_pending == 1) {
		/* Were closing , stop reading */
		dev_dbg(&port->dev,
			"%s - (qt_port->close_pending == 1\n", __func__);
		qt_port->read_bulk_stopped = 1;
		return;
	}

	/*
	 * rx_holding is asserted by throttle, if we assert it, we're not
	 * receiving any more characters and let the box handle the flow
	 * control
	 */
	if (qt_port->rx_holding == 1) {
		qt_port->read_bulk_stopped = 1;
		return;
	}

	if (urb->status) {
		qt_port->read_bulk_stopped = 1;

		dev_dbg(&port->dev,
			"%s - nonzero read bulk status received: %d\n",
			__func__, urb->status);
		return;
	}

	if (urb->actual_length)
		qt_status_change_check(urb, qt_port, port);

	/* Continue trying to always read  */
	usb_fill_bulk_urb(port->read_urb, serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer,
			  port->read_urb->transfer_buffer_length,
			  qt_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		dev_dbg(&port->dev,
			"%s - failed resubmitting read urb, error %d",
			__func__, result);
	else {
		if (urb->actual_length) {
			tty_flip_buffer_push(&port->port);
			tty_schedule_flip(&port->port);
		}
	}

	schedule_work(&port->work);
}

/*
 * qt_get_device
 *   Issue a GET_DEVICE vendor-specific request on the default control pipe If
 *   successful, fills in the qt_get_device_data structure pointed to by
 *   device_data, otherwise return a negative error number of the problem.
 */

static int qt_get_device(struct usb_serial *serial,
			 struct qt_get_device_data *device_data)
{
	int result;
	unsigned char *transfer_buffer;

	transfer_buffer =
	    kmalloc(sizeof(struct qt_get_device_data), GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 QT_SET_GET_DEVICE, 0xc0, 0, 0,
				 transfer_buffer,
				 sizeof(struct qt_get_device_data), 300);
	if (result > 0)
		memcpy(device_data, transfer_buffer,
		       sizeof(struct qt_get_device_data));
	kfree(transfer_buffer);

	return result;
}

/****************************************************************************
 *  box_set_prebuffer_level
   TELLS BOX WHEN TO ASSERT FLOW CONTROL
 ****************************************************************************/
static int box_set_prebuffer_level(struct usb_serial *serial)
{
	int result;
	__u16 buffer_length;

	buffer_length = PREFUFF_LEVEL_CONSERVATIVE;
	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 QT_GET_SET_PREBUF_TRIG_LVL, 0x40,
				 buffer_length, 0, NULL, 0, 300);
	return result;
}

/****************************************************************************
 *  box_set_atc
   TELLS BOX WHEN TO ASSERT automatic transmitter control
   ****************************************************************************/
static int box_set_atc(struct usb_serial *serial, __u16 n_mode)
{
	int result;
	__u16 buffer_length;

	buffer_length = PREFUFF_LEVEL_CONSERVATIVE;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SET_ATF, 0x40, n_mode, 0, NULL, 0, 300);

	return result;
}

/**
 * qt_set_device
 *   Issue a SET_DEVICE vendor-specific request on the default control pipe If
 *   successful returns the number of bytes written, otherwise it returns a
 *   negative error number of the problem.
 */
static int qt_set_device(struct usb_serial *serial,
			 struct qt_get_device_data *device_data)
{
	int result;
	__u16 length;
	__u16 port_settings;

	port_settings = ((__u16) (device_data->portb));
	port_settings = (port_settings << 8);
	port_settings += ((__u16) (device_data->porta));

	length = sizeof(struct qt_get_device_data);

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 QT_SET_GET_DEVICE, 0x40, port_settings,
				 0, NULL, 0, 300);
	return result;
}

static int qt_open_channel(struct usb_serial *serial, __u16 uart_num,
			   struct qt_open_channel_data *pdevice_data)
{
	int result;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 QT_OPEN_CLOSE_CHANNEL,
				 USBD_TRANSFER_DIRECTION_IN, 1, uart_num,
				 pdevice_data,
				 sizeof(struct qt_open_channel_data), 300);

	return result;

}

static int qt_close_channel(struct usb_serial *serial, __u16 uart_num)
{
	int result;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 QT_OPEN_CLOSE_CHANNEL,
				 USBD_TRANSFER_DIRECTION_OUT, 0, uart_num,
				 NULL, 0, 300);

	return result;

}

/****************************************************************************
* box_get_register
*	issuse a GET_REGISTER vendor-spcific request on the default control pipe
*	If successful, fills in the  p_value with the register value asked for
****************************************************************************/
static int box_get_register(struct usb_serial *serial, unsigned short uart_num,
			  unsigned short register_num, __u8 *p_value)
{
	int result;
	__u16 current_length;

	current_length = sizeof(struct qt_get_device_data);

	result =
	    usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			    QT_GET_SET_REGISTER, 0xC0, register_num,
			    uart_num, (void *)p_value, sizeof(*p_value), 300);

	return result;
}

/****************************************************************************
* box_set_register
*	issuse a GET_REGISTER vendor-spcific request on the default control pipe
*	If successful, fills in the  p_value with the register value asked for
****************************************************************************/
static int box_set_register(struct usb_serial *serial, unsigned short uart_num,
			  unsigned short register_num, unsigned short value)
{
	int result;
	unsigned short reg_and_byte;

	reg_and_byte = value;
	reg_and_byte = reg_and_byte << 8;
	reg_and_byte = reg_and_byte + register_num;

/*
	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 QT_GET_SET_REGISTER, 0xC0, register_num,
				 uart_num, NULL, 0, 300);
*/

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_GET_SET_REGISTER, 0x40, reg_and_byte, uart_num,
			    NULL, 0, 300);

	return result;
}

/*
 * qt_setuart
 * issues a SET_UART vendor-specific request on the default control pipe
 * If successful sets baud rate divisor and LCR value
 */
static int qt_setuart(struct usb_serial *serial, unsigned short uart_num,
		      unsigned short default_divisor, unsigned char default_lcr)
{
	int result;
	unsigned short uart_num_and_lcr;

	uart_num_and_lcr = (default_lcr << 8) + uart_num;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_GET_SET_UART, 0x40, default_divisor,
			    uart_num_and_lcr, NULL, 0, 300);

	return result;
}

static int box_set_hw_flow_ctrl(struct usb_serial *serial, unsigned int index,
			     int b_set)
{
	__u8 mcr = 0;
	__u8 msr = 0, mout_value = 0;
	unsigned int status;

	if (b_set == 1) {
		/* flow control, box will clear RTS line to prevent remote */
		mcr = SERIAL_MCR_RTS;
	} /* device from xmitting more chars */
	else {
		/* no flow control to remote device */
		mcr = 0;

	}
	mout_value = mcr << 8;

	if (b_set == 1) {
		/* flow control, box will inhibit xmit data if CTS line is
		 * asserted */
		msr = SERIAL_MSR_CTS;
	} else {
		/* Box will not inhimbe xmit data due to CTS line */
		msr = 0;
	}
	mout_value |= msr;

	status =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_HW_FLOW_CONTROL_MASK, 0x40, mout_value,
			    index, NULL, 0, 300);
	return status;

}

static int box_set_sw_flow_ctrl(struct usb_serial *serial, __u16 index,
			     unsigned char stop_char, unsigned char start_char)
{
	__u16 n_sw_flow_out;
	int result;

	n_sw_flow_out = start_char << 8;
	n_sw_flow_out = (unsigned short)stop_char;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SW_FLOW_CONTROL_MASK, 0x40, n_sw_flow_out,
			    index, NULL, 0, 300);
	return result;

}

static int box_disable_sw_flow_ctrl(struct usb_serial *serial, __u16 index)
{
	int result;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SW_FLOW_CONTROL_DISABLE, 0x40, 0, index,
			    NULL, 0, 300);
	return result;

}

static int qt_startup(struct usb_serial *serial)
{
	struct device *dev = &serial->dev->dev;
	struct usb_serial_port *port;
	struct quatech_port *qt_port;
	struct qt_get_device_data device_data;
	int i;
	int status;

	/* Now setup per port private data */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		qt_port = kzalloc(sizeof(*qt_port), GFP_KERNEL);
		if (!qt_port) {
			for (--i; i >= 0; i--) {
				port = serial->port[i];
				kfree(usb_get_serial_port_data(port));
				usb_set_serial_port_data(port, NULL);
			}
			return -ENOMEM;
		}
		mutex_init(&qt_port->lock);

		usb_set_serial_port_data(port, qt_port);

	}

	status = qt_get_device(serial, &device_data);
	if (status < 0)
		goto startup_error;

	dev_dbg(dev, "device_data.portb = 0x%x\n", device_data.portb);

	device_data.portb &= ~FULLPWRBIT;
	dev_dbg(dev, "Changing device_data.portb to 0x%x\n", device_data.portb);

	status = qt_set_device(serial, &device_data);
	if (status < 0) {
		dev_dbg(dev, "qt_set_device failed\n");
		goto startup_error;
	}

	status = qt_get_device(serial, &device_data);
	if (status < 0) {
		dev_dbg(dev, "qt_get_device failed\n");
		goto startup_error;
	}

	switch (le16_to_cpu(serial->dev->descriptor.idProduct)) {
	case QUATECH_DSU100:
	case QUATECH_QSU100:
	case QUATECH_ESU100A:
	case QUATECH_ESU100B:
	case QUATECH_HSU100A:
	case QUATECH_HSU100B:
	case QUATECH_HSU100C:
	case QUATECH_HSU100D:
		device_data.porta &= ~(RR_BITS | DUPMODE_BITS);
		device_data.porta |= CLKS_X4;
		device_data.portb &= ~(LOOPMODE_BITS);
		device_data.portb |= RS232_MODE;
		break;

	case QUATECH_SSU200:
	case QUATECH_DSU200:
	case QUATECH_QSU200:
	case QUATECH_ESU200A:
	case QUATECH_ESU200B:
	case QUATECH_HSU200A:
	case QUATECH_HSU200B:
	case QUATECH_HSU200C:
	case QUATECH_HSU200D:
		device_data.porta &= ~(RR_BITS | DUPMODE_BITS);
		device_data.porta |= CLKS_X4;
		device_data.portb &= ~(LOOPMODE_BITS);
		device_data.portb |= ALL_LOOPBACK;
		break;
	default:
		device_data.porta &= ~(RR_BITS | DUPMODE_BITS);
		device_data.porta |= CLKS_X4;
		device_data.portb &= ~(LOOPMODE_BITS);
		device_data.portb |= RS232_MODE;
		break;

	}

	status = box_set_prebuffer_level(serial);	/* sets to
							 * default value
							 */
	if (status < 0) {
		dev_dbg(dev, "box_set_prebuffer_level failed\n");
		goto startup_error;
	}

	status = box_set_atc(serial, ATC_DISABLED);
	if (status < 0) {
		dev_dbg(dev, "box_set_atc failed\n");
		goto startup_error;
	}

	dev_dbg(dev, "device_data.portb = 0x%x\n", device_data.portb);

	device_data.portb |= NEXT_BOARD_POWER_BIT;
	dev_dbg(dev, "Changing device_data.portb to 0x%x\n", device_data.portb);

	status = qt_set_device(serial, &device_data);
	if (status < 0) {
		dev_dbg(dev, "qt_set_device failed\n");
		goto startup_error;
	}

	return 0;

startup_error:
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		qt_port = qt_get_port_private(port);
		kfree(qt_port);
		usb_set_serial_port_data(port, NULL);
	}

	return -EIO;
}

static void qt_release(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	struct quatech_port *qt_port;
	int i;

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!port)
			continue;

		qt_port = usb_get_serial_port_data(port);
		kfree(qt_port);
		usb_set_serial_port_data(port, NULL);
	}

}

static void qt_submit_urb_from_open(struct usb_serial *serial,
				    struct usb_serial_port *port)
{
	int result;
	struct usb_serial_port *port0 = serial->port[0];

	/* set up interrupt urb */
	usb_fill_int_urb(port0->interrupt_in_urb,
			 serial->dev,
			 usb_rcvintpipe(serial->dev,
					port0->interrupt_in_endpointAddress),
			 port0->interrupt_in_buffer,
			 port0->interrupt_in_urb->transfer_buffer_length,
			 qt_interrupt_callback, serial,
			 port0->interrupt_in_urb->interval);

	result = usb_submit_urb(port0->interrupt_in_urb,
				GFP_KERNEL);
	if (result) {
		dev_err(&port->dev,
			"%s - Error %d submitting interrupt urb\n",
			__func__, result);
	}
}

static int qt_open(struct tty_struct *tty,
		   struct usb_serial_port *port)
{
	struct usb_serial *serial;
	struct quatech_port *quatech_port;
	struct quatech_port *port0;
	struct qt_open_channel_data channel_data;

	int result;

	if (port_paranoia_check(port, __func__))
		return -ENODEV;

	serial = port->serial;

	if (serial_paranoia_check(serial, __func__))
		return -ENODEV;

	quatech_port = qt_get_port_private(port);
	port0 = qt_get_port_private(serial->port[0]);

	if (quatech_port == NULL || port0 == NULL)
		return -ENODEV;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);
	port0->open_ports++;

	result = qt_get_device(serial, &port0->device_data);

	/* Port specific setups */
	result = qt_open_channel(serial, port->port_number, &channel_data);
	if (result < 0) {
		dev_dbg(&port->dev, "qt_open_channel failed\n");
		return result;
	}
	dev_dbg(&port->dev, "qt_open_channel completed.\n");

/* FIXME: are these needed?  Does it even do anything useful? */
	quatech_port->shadow_lsr = channel_data.line_status &
	    (SERIAL_LSR_OE | SERIAL_LSR_PE | SERIAL_LSR_FE | SERIAL_LSR_BI);

	quatech_port->shadow_msr = channel_data.modem_status &
	    (SERIAL_MSR_CTS | SERIAL_MSR_DSR | SERIAL_MSR_RI | SERIAL_MSR_CD);

	/* Set Baud rate to default and turn off (default)flow control here */
	result = qt_setuart(serial, port->port_number, DEFAULT_DIVISOR,
			DEFAULT_LCR);
	if (result < 0) {
		dev_dbg(&port->dev, "qt_setuart failed\n");
		return result;
	}
	dev_dbg(&port->dev, "qt_setuart completed.\n");

	/*
	 * Put this here to make it responsive to stty and defaults set by
	 * the tty layer
	 */

	/*  Check to see if we've set up our endpoint info yet */
	if (port0->open_ports == 1) {
		if (serial->port[0]->interrupt_in_buffer == NULL)
			qt_submit_urb_from_open(serial, port);
	}

	dev_dbg(&port->dev, "minor number is %d\n", port->minor);
	dev_dbg(&port->dev,
		"Bulkin endpoint is %d\n", port->bulk_in_endpointAddress);
	dev_dbg(&port->dev,
		"BulkOut endpoint is %d\n", port->bulk_out_endpointAddress);
	dev_dbg(&port->dev, "Interrupt endpoint is %d\n",
		port->interrupt_in_endpointAddress);
	dev_dbg(&port->dev, "port's number in the device is %d\n",
		quatech_port->port_num);
	quatech_port->read_urb = port->read_urb;

	/* set up our bulk in urb */

	usb_fill_bulk_urb(quatech_port->read_urb,
			  serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->bulk_in_buffer,
			  quatech_port->read_urb->transfer_buffer_length,
			  qt_read_bulk_callback, quatech_port);

	dev_dbg(&port->dev, "qt_open: bulkin endpoint is %d\n",
		port->bulk_in_endpointAddress);
	quatech_port->read_urb_busy = true;
	result = usb_submit_urb(quatech_port->read_urb, GFP_KERNEL);
	if (result) {
		dev_err(&port->dev,
			"%s - Error %d submitting control urb\n",
			__func__, result);
		quatech_port->read_urb_busy = false;
	}

	/* initialize our wait queues */
	init_waitqueue_head(&quatech_port->wait);
	init_waitqueue_head(&quatech_port->msr_wait);

	/* initialize our icount structure */
	memset(&(quatech_port->icount), 0x00, sizeof(quatech_port->icount));

	return 0;

}

static int qt_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial;
	int chars = 0;

	serial = get_usb_serial(port, __func__);

	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS)
			chars = port->write_urb->transfer_buffer_length;
	}

	return chars;
}

static void qt_block_until_empty(struct tty_struct *tty,
				 struct quatech_port *qt_port)
{
	int timeout = HZ / 10;
	int wait = 30;

	/* returns if we get a signal, an error, or the buffer is empty */
	while (wait_event_interruptible_timeout(qt_port->wait,
					qt_chars_in_buffer(tty) <= 0,
					timeout) == 0) {
		wait--;
		if (wait == 0) {
			dev_dbg(&qt_port->port->dev, "%s - TIMEOUT", __func__);
			return;
		} else {
			wait = 30;
		}
	}
}

static void qt_close(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	struct tty_struct *tty = tty_port_tty_get(&port->port);
	unsigned int index = port->port_number;
	struct quatech_port *qt_port = qt_get_port_private(port);
	struct quatech_port *port0 = qt_get_port_private(serial->port[0]);

	/* shutdown any bulk reads that might be going on */
	if (serial->num_bulk_out)
		usb_unlink_urb(port->write_urb);
	if (serial->num_bulk_in)
		usb_unlink_urb(port->read_urb);

	/* wait up to for transmitter to empty */
	if (serial->dev)
		qt_block_until_empty(tty, qt_port);
	tty_kref_put(tty);

	/* Close uart channel */
	if (qt_close_channel(serial, index) < 0)
		dev_dbg(&port->dev, "%s - qt_close_channel failed.\n",
			__func__);

	port0->open_ports--;

	dev_dbg(&port->dev, "qt_num_open_ports in close%d\n",
		port0->open_ports);

	if (port0->open_ports == 0) {
		if (serial->port[0]->interrupt_in_urb) {
			dev_dbg(&port->dev, "Shutdown interrupt_in_urb\n");
			usb_kill_urb(serial->port[0]->interrupt_in_urb);
		}

	}

	if (qt_port->write_urb) {
		/* if this urb had a transfer buffer already (old tx) free it */
		kfree(qt_port->write_urb->transfer_buffer);
		usb_free_urb(qt_port->write_urb);
	}

}

static int qt_write(struct tty_struct *tty, struct usb_serial_port *port,
		    const unsigned char *buf, int count)
{
	int result;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	if (serial == NULL)
		return -ENODEV;

	if (count == 0) {
		dev_dbg(&port->dev,
			"%s - write request of 0 bytes\n", __func__);
		return 0;
	}

	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS) {
			dev_dbg(&port->dev, "%s - already writing\n", __func__);
			return 0;
		}

		count =
		    (count > port->bulk_out_size) ? port->bulk_out_size : count;
		memcpy(port->write_urb->transfer_buffer, buf, count);

		/* set up our urb */

		usb_fill_bulk_urb(port->write_urb, serial->dev,
				  usb_sndbulkpipe(serial->dev,
						  port->
						  bulk_out_endpointAddress),
				  port->write_urb->transfer_buffer, count,
				  qt_write_bulk_callback, port);

		/* send the data out the bulk port */
		result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
		if (result)
			dev_dbg(&port->dev,
				"%s - failed submitting write urb, error %d\n",
				__func__, result);
		else
			result = count;

		return result;
	}

	/* no bulk out, so return 0 bytes written */
	return 0;
}

static int qt_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial;
	struct quatech_port *qt_port;

	int retval = -EINVAL;

	if (port_paranoia_check(port, __func__))
		return -1;

	serial = get_usb_serial(port, __func__);

	if (!serial)
		return -ENODEV;

	qt_port = qt_get_port_private(port);

	mutex_lock(&qt_port->lock);

	if (serial->num_bulk_out) {
		if (port->write_urb->status != -EINPROGRESS)
			retval = port->bulk_out_size;
	}

	mutex_unlock(&qt_port->lock);
	return retval;

}

static int qt_ioctl(struct tty_struct *tty,
		    unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct quatech_port *qt_port = qt_get_port_private(port);
	unsigned int index;

	dev_dbg(&port->dev, "%s cmd 0x%04x\n", __func__, cmd);

	index = port->port_number;

	if (cmd == TIOCMIWAIT) {
		while (qt_port != NULL) {
#if 0
			/* this never wakes up */
			interruptible_sleep_on(&qt_port->msr_wait);
#endif
			if (signal_pending(current))
				return -ERESTARTSYS;
			else {
				char diff = qt_port->diff_status;

				if (diff == 0)
					return -EIO;	/* no change => error */

				/* Consume all events */
				qt_port->diff_status = 0;

				if (((arg & TIOCM_RNG)
				     && (diff & SERIAL_MSR_RI))
				    || ((arg & TIOCM_DSR)
					&& (diff & SERIAL_MSR_DSR))
				    || ((arg & TIOCM_CD)
					&& (diff & SERIAL_MSR_CD))
				    || ((arg & TIOCM_CTS)
					&& (diff & SERIAL_MSR_CTS))) {
					return 0;
				}
			}
		}
		return 0;
	}

	dev_dbg(&port->dev, "%s -No ioctl for that one.\n", __func__);
	return -ENOIOCTLCMD;
}

static void qt_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   struct ktermios *old_termios)
{
	struct ktermios *termios = &tty->termios;
	unsigned char new_lcr = 0;
	unsigned int cflag = termios->c_cflag;
	unsigned int index;
	int baud, divisor, remainder;
	int status;

	index = port->port_number;

	switch (cflag & CSIZE) {
	case CS5:
		new_lcr |= SERIAL_5_DATA;
		break;
	case CS6:
		new_lcr |= SERIAL_6_DATA;
		break;
	case CS7:
		new_lcr |= SERIAL_7_DATA;
		break;
	default:
		termios->c_cflag &= ~CSIZE;
		termios->c_cflag |= CS8;
	case CS8:
		new_lcr |= SERIAL_8_DATA;
		break;
	}

	/* Parity stuff */
	if (cflag & PARENB) {
		if (cflag & PARODD)
			new_lcr |= SERIAL_ODD_PARITY;
		else
			new_lcr |= SERIAL_EVEN_PARITY;
	}
	if (cflag & CSTOPB)
		new_lcr |= SERIAL_TWO_STOPB;
	else
		new_lcr |= SERIAL_ONE_STOPB;

	dev_dbg(&port->dev, "%s - 4\n", __func__);

	/* Thats the LCR stuff, go ahead and set it */
	baud = tty_get_baud_rate(tty);
	if (!baud)
		/* pick a default, any default... */
		baud = 9600;

	dev_dbg(&port->dev, "%s - got baud = %d\n", __func__, baud);

	divisor = MAX_BAUD_RATE / baud;
	remainder = MAX_BAUD_RATE % baud;
	/* Round to nearest divisor */
	if (((remainder * 2) >= baud) && (baud != 110))
		divisor++;

	/*
	 * Set Baud rate to default and turn off (default)flow control here
	 */
	status =
	    qt_setuart(port->serial, index, (unsigned short)divisor, new_lcr);
	if (status < 0) {
		dev_dbg(&port->dev, "qt_setuart failed\n");
		return;
	}

	/* Now determine flow control */
	if (cflag & CRTSCTS) {
		dev_dbg(&port->dev, "%s - Enabling HW flow control\n",
			__func__);

		/* Enable RTS/CTS flow control */
		status = box_set_hw_flow_ctrl(port->serial, index, 1);

		if (status < 0) {
			dev_dbg(&port->dev, "box_set_hw_flow_ctrl failed\n");
			return;
		}
	} else {
		/* Disable RTS/CTS flow control */
		dev_dbg(&port->dev,
			"%s - disabling HW flow control\n", __func__);

		status = box_set_hw_flow_ctrl(port->serial, index, 0);
		if (status < 0) {
			dev_dbg(&port->dev, "box_set_hw_flow_ctrl failed\n");
			return;
		}

	}

	/* if we are implementing XON/XOFF, set the start and stop character in
	 * the device */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		unsigned char start_char = START_CHAR(tty);

		status = box_set_sw_flow_ctrl(port->serial, index, stop_char,
					      start_char);
		if (status < 0)
			dev_dbg(&port->dev,
				"box_set_sw_flow_ctrl (enabled) failed\n");

	} else {
		/* disable SW flow control */
		status = box_disable_sw_flow_ctrl(port->serial, index);
		if (status < 0)
			dev_dbg(&port->dev,
				"box_set_sw_flow_ctrl (diabling) failed\n");

	}
	termios->c_cflag &= ~CMSPAR;
	/* FIXME:
	   Error cases should be returning the actual bits changed only
	*/
}

static void qt_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port;
	u16 index, onoff;
	unsigned int result;

	index = port->port_number;

	qt_port = qt_get_port_private(port);

	if (break_state == -1)
		onoff = 1;
	else
		onoff = 0;

	mutex_lock(&qt_port->lock);

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_BREAK_CONTROL, 0x40, onoff, index, NULL, 0, 300);

	mutex_unlock(&qt_port->lock);
}

static inline int qt_real_tiocmget(struct tty_struct *tty,
				   struct usb_serial_port *port,
				   struct usb_serial *serial)
{

	u8 mcr;
	u8 msr;
	unsigned int result = 0;
	int status;
	unsigned int index;

	index = port->port_number;
	status =
	    box_get_register(port->serial, index, MODEM_CONTROL_REGISTER, &mcr);
	if (status >= 0) {
		status =
		    box_get_register(port->serial, index,
				   MODEM_STATUS_REGISTER, &msr);

	}

	if (status >= 0) {
		result = ((mcr & SERIAL_MCR_DTR) ? TIOCM_DTR : 0)
		    /* DTR IS SET */
		    | ((mcr & SERIAL_MCR_RTS) ? TIOCM_RTS : 0)
		    /* RTS IS SET */
		    | ((msr & SERIAL_MSR_CTS) ? TIOCM_CTS : 0)
		    /* CTS is set */
		    | ((msr & SERIAL_MSR_CD) ? TIOCM_CAR : 0)
		    /* Carrier detect is set */
		    | ((msr & SERIAL_MSR_RI) ? TIOCM_RI : 0)
		    /* Ring indicator set */
		    | ((msr & SERIAL_MSR_DSR) ? TIOCM_DSR : 0);
		/* DSR is set */
		return result;

	} else
		return -ESPIPE;
}

static inline int qt_real_tiocmset(struct tty_struct *tty,
				   struct usb_serial_port *port,
				   struct usb_serial *serial,
				   unsigned int value)
{

	u8 mcr;
	int status;
	unsigned int index;

	index = port->port_number;
	status =
	    box_get_register(port->serial, index, MODEM_CONTROL_REGISTER, &mcr);
	if (status < 0)
		return -ESPIPE;

	/*
	 * Turn off the RTS and DTR and loopback and then only turn on what was
	 * asked for
	 */
	mcr &= ~(SERIAL_MCR_RTS | SERIAL_MCR_DTR | SERIAL_MCR_LOOP);
	if (value & TIOCM_RTS)
		mcr |= SERIAL_MCR_RTS;
	if (value & TIOCM_DTR)
		mcr |= SERIAL_MCR_DTR;
	if (value & TIOCM_LOOP)
		mcr |= SERIAL_MCR_LOOP;

	status =
	    box_set_register(port->serial, index, MODEM_CONTROL_REGISTER, mcr);
	if (status < 0)
		return -ESPIPE;
	else
		return 0;
}

static int qt_tiocmget(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port = qt_get_port_private(port);
	int retval;

	if (!serial)
		return -ENODEV;

	mutex_lock(&qt_port->lock);
	retval = qt_real_tiocmget(tty, port, serial);
	mutex_unlock(&qt_port->lock);
	return retval;
}

static int qt_tiocmset(struct tty_struct *tty,
		       unsigned int set, unsigned int clear)
{

	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port = qt_get_port_private(port);
	int retval;

	if (!serial)
		return -ENODEV;

	mutex_lock(&qt_port->lock);
	retval = qt_real_tiocmset(tty, port, serial, set);
	mutex_unlock(&qt_port->lock);
	return retval;
}

static void qt_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port;

	if (!serial)
		return;

	qt_port = qt_get_port_private(port);

	mutex_lock(&qt_port->lock);

	/* pass on to the driver specific version of this function */
	qt_port->rx_holding = 1;

	mutex_unlock(&qt_port->lock);
}

static void qt_submit_urb_from_unthrottle(struct usb_serial_port *port,
					  struct usb_serial *serial)
{
	int result;

	/* Start reading from the device */
	usb_fill_bulk_urb(port->read_urb, serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer,
			  port->read_urb->transfer_buffer_length,
			  qt_read_bulk_callback, port);

	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);

	if (result)
		dev_err(&port->dev,
			"%s - failed restarting read urb, error %d\n",
			__func__, result);
}

static void qt_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct quatech_port *qt_port;

	if (!serial)
		return;

	qt_port = qt_get_port_private(port);

	mutex_lock(&qt_port->lock);

	if (qt_port->rx_holding == 1) {
		dev_dbg(&port->dev, "%s -qt_port->rx_holding == 1\n", __func__);

		qt_port->rx_holding = 0;
		dev_dbg(&port->dev, "%s - qt_port->rx_holding = 0\n", __func__);

		/* if we have a bulk endpoint, start it up */
		if ((serial->num_bulk_in) && (qt_port->read_bulk_stopped == 1))
			qt_submit_urb_from_unthrottle(port, serial);
	}
	mutex_unlock(&qt_port->lock);
}

static int qt_calc_num_ports(struct usb_serial *serial)
{
	int num_ports;

	num_ports =
	    (serial->interface->cur_altsetting->desc.bNumEndpoints - 1) / 2;

	return num_ports;
}

static struct usb_serial_driver quatech_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "serqt",
		   },
	.description = DRIVER_DESC,
	.id_table = id_table,
	.num_ports = 8,
	.open = qt_open,
	.close = qt_close,
	.write = qt_write,
	.write_room = qt_write_room,
	.chars_in_buffer = qt_chars_in_buffer,
	.throttle = qt_throttle,
	.unthrottle = qt_unthrottle,
	.calc_num_ports = qt_calc_num_ports,
	.ioctl = qt_ioctl,
	.set_termios = qt_set_termios,
	.break_ctl = qt_break,
	.tiocmget = qt_tiocmget,
	.tiocmset = qt_tiocmset,
	.attach = qt_startup,
	.release = qt_release,
};

static struct usb_serial_driver * const serial_drivers[] = {
	&quatech_device, NULL
};

module_usb_serial_driver(serial_drivers, id_table);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
