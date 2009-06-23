/*
 * Driver for Quatech Inc USB2.0 to serial adaptors. Largely unrelated to the
 * serqt_usb driver, based on a re-write of the vendor supplied serqt_usb2 code,
 * which is unrelated to the serqt_usb2 in the staging kernel
 */

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
#include <linux/uaccess.h>

static int debug;

/* Version Information */
#define DRIVER_VERSION "v2.00"
#define DRIVER_AUTHOR "Tim Gobeli, Quatech, Inc"
#define DRIVER_DESC "Quatech USB 2.0 to Serial Driver"

/* vendor and device IDs */
#define	USB_VENDOR_ID_QUATECH 0x061d	/* Quatech VID */
#define QUATECH_SSU2_100 0xC120		/* RS232 single port */
#define QUATECH_DSU2_100 0xC140		/* RS232 dual port */
#define QUATECH_DSU2_400 0xC150		/* RS232/422/485 dual port */
#define QUATECH_QSU2_100 0xC160		/* RS232 four port */
#define QUATECH_QSU2_400 0xC170		/* RS232/422/485 four port */
#define QUATECH_ESU2_100 0xC1A0		/* RS232 eight port */
#define QUATECH_ESU2_400 0xC180		/* RS232/422/485 eight port */

/* magic numbers go here, when we find out which ones are needed */

#define QU2BOXPWRON 0x8000		/* magic number to turn FPGA power on */
#define QU2BOX232 0x40			/* RS232 mode on MEI devices */
#define QU2BOXSPD9600 0x60		/* set speed to 9600 baud */
/* directions for USB transfers */
#define USBD_TRANSFER_DIRECTION_IN    0xc0
#define USBD_TRANSFER_DIRECTION_OUT   0x40
/* special Quatech command IDs */
#define QT_SET_GET_DEVICE		0xc2
#define QT_OPEN_CLOSE_CHANNEL		0xca
/*#define QT_GET_SET_PREBUF_TRIG_LVL	0xcc
#define QT_SET_ATF			0xcd
#define QT_GET_SET_REGISTER		0xc0*/
#define QT_GET_SET_UART			0xc1
/*#define QT_HW_FLOW_CONTROL_MASK		0xc5
#define QT_SW_FLOW_CONTROL_MASK		0xc6
#define QT_SW_FLOW_CONTROL_DISABLE	0xc7
#define QT_BREAK_CONTROL 		0xc8
#define QT_STOP_RECEIVE			0xe0
#define QT_FLUSH_DEVICE			0xc4*/
#define QT_GET_SET_QMCR			0xe1
/* port setting constants */
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

#define  MAX_BAUD_RATE              921600
#define  MAX_BAUD_REMAINDER         4608

#define SERIAL_LSR_OE       0x02
#define SERIAL_LSR_PE       0x04
#define SERIAL_LSR_FE       0x08
#define SERIAL_LSR_BI       0x10


static struct usb_device_id quausb2_id_table[] = {
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_SSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_DSU2_400)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_QSU2_400)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU2_100)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, QUATECH_ESU2_400)},
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, quausb2_id_table);

/* custom structures we need go here */


static struct usb_driver quausb2_usb_driver = {
	.name = "quatech-usb2-serial",
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table = quausb2_id_table,
	.no_dynamic_id = 1,
};

/* structure in which to keep all the messy stuff that this driver needs
 * alongside the usb_serial_port structure */
struct quatech2_port {
	int	magic;
	char	active;		/* someone has this device open */
	unsigned char		*xfer_to_tty_buffer;
	wait_queue_head_t	wait;
	int	open_count;	/* number of times this	port has been opened */
	struct semaphore	sem;	/* locks this structure */
	__u8	shadowLCR;	/* last LCR value received */
	__u8	shadowMCR;	/* last MCR value received */
	__u8	shadowMSR;	/* last MSR value received */
	__u8	shadowLSR;	/* last LSR value received */
	char	open_ports;	/* ports open on whole device */
	char	RxHolding;
	char	Rcv_Flush;
	char	Xmit_Flush;
	char	closePending;
	char	fifo_empty_flag;
	int	xmit_pending_bytes;
	int	xmit_fifo_room_bytes;
	struct semaphore	pend_xmit_sem;	/* locks this structure */
	spinlock_t lock;
};

/* structure which holds line and modem status flags */
struct qt2_status_data {
	__u8 line_status;
	__u8 modem_status;
};

/* Function prototypes */
static int qt2_boxpoweron(struct usb_serial *serial);
static int qt2_boxsetQMCR(struct usb_serial *serial, __u16 Uart_Number,
			__u8 QMCR_Value);
static int port_paranoia_check(struct usb_serial_port *port,
			const char *function);
static int serial_paranoia_check(struct usb_serial *serial,
			 const char *function);
static inline struct quatech2_port *qt2_get_port_private(struct usb_serial_port
			*port);
static inline void qt2_set_port_private(struct usb_serial_port *port,
			struct quatech2_port *data);
static int qt2_openboxchannel(struct usb_serial *serial, __u16
			Uart_Number, struct qt2_status_data *pDeviceData);
static int qt2_closeboxchannel(struct usb_serial *serial, __u16
			Uart_Number);
static int qt2_conf_uart(struct usb_serial *serial,  unsigned short Uart_Number,
			 unsigned short divisor, unsigned char LCR);
/* implementation functions, roughly in order of use, are here */
static int qt2_calc_num_ports(struct usb_serial *serial)
{
	int num_ports;
	int flag_as_400;
	switch (serial->dev->descriptor.idProduct) {
	case QUATECH_SSU2_100:
		num_ports = 1;
		break;

	case QUATECH_DSU2_400:
		flag_as_400 = true;
	case QUATECH_DSU2_100:
		num_ports = 2;
	break;

	case QUATECH_QSU2_400:
		flag_as_400 = true;
	case QUATECH_QSU2_100:
		num_ports = 4;
	break;

	case QUATECH_ESU2_400:
		flag_as_400 = true;
	case QUATECH_ESU2_100:
		num_ports = 8;
	break;
	default:
	num_ports = 1;
	break;
	}
	return num_ports;
}

static int qt2_attach(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	struct quatech2_port *qt2_port;
	int i;
	/* stuff for printing endpoint addresses, not needed for
	 * production */
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface_desc;

	/* check how many endpoints there are on the device, for
	 * sanity's sake */
	dbg("%s(): Endpoints: %d bulk in, %d bulk out, %d interrupt in",
			__func__, serial->num_bulk_in,
			serial->num_bulk_out, serial->num_interrupt_in);
	if ((serial->num_bulk_in != 1) || (serial->num_bulk_out != 1)) {
		dbg("Device has wrong number of bulk endpoints!");
		return -ENODEV;
	}
	iface_desc = serial->interface->cur_altsetting;
	/* print endpoint addresses so we can check them later
	 * by hand */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i)
	{
		endpoint = &iface_desc->endpoint[i].desc;

		if ((endpoint->bEndpointAddress & 0x80) &&
			((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in at 0x%x",
				endpoint->bEndpointAddress);
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
				    ((endpoint->bmAttributes & 3) == 0x02))
		{
			/* we found a bulk out endpoint */
			dbg("found bulk out at 0x%x",
				endpoint->bEndpointAddress);
		}
	}	/* end printing endpoint addresses */

	/* Now setup per port private data, which replaces all the things
	* that quatech added to standard kernel structures in their driver */
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		qt2_port = kzalloc(sizeof(*qt2_port), GFP_KERNEL);
		if (!qt2_port) {
			dbg("%s: kmalloc for quatech2_port (%d) failed!.",
			    __func__, i);
			return -ENOMEM;
		}
		spin_lock_init(&qt2_port->lock);
		if (i == 0)
			qt2_port->open_ports = 0; /* no ports */
		else
			qt2_port->open_ports = -1; /* unused */

		usb_set_serial_port_data(port, qt2_port);

	}
	/* switch on power to the hardware */
	if (qt2_boxpoweron(serial) < 0) {
		dbg("qt2_boxpoweron() failed");
		goto startup_error;
	}
	/* set all ports to RS232 mode */
	for (i = 0; i < serial->num_ports; ++i) {
		if (qt2_boxsetQMCR(serial, i, QU2BOX232) < 0) {
			dbg("qt2_boxsetQMCR() on port %d failed",
				i);
			goto startup_error;
		}
	}

	return 0;

startup_error:
	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		qt2_port = qt2_get_port_private(port);
		kfree(qt2_port);
		usb_set_serial_port_data(port, NULL);
	}

	dbg("Exit fail %s\n", __func__);
	return -EIO;
}

static void qt2_release(struct usb_serial *serial)
{
	struct usb_serial_port *port;
	struct quatech2_port *qt_port;
	int i;

	dbg("enterting %s", __func__);

	for (i = 0; i < serial->num_ports; i++) {
		port = serial->port[i];
		if (!port)
			continue;

		qt_port = usb_get_serial_port_data(port);
		kfree(qt_port);
		usb_set_serial_port_data(port, NULL);
	}
}
/* This function is called once per serial port on the device.
 * The tty_struct and the usb_serial_port belong to this port,
 * i.e. there are multiple ones for a multi-port device.
 * However the usb_serial_port structure has a back-pointer
 * to the parent usb_serial structure which belongs to the device,
 * so we can access either the device-wide information or
 * any other port's information (because there are also forward
 * pointers) via that pointer.
 * This is most helpful if the device shares resources (e.g. end
 * points) between different ports
 */
int qt2_open(struct tty_struct *tty,
	    struct usb_serial_port *port, struct file *filp)
{
	struct usb_serial *serial;	/* device structure */
	struct usb_serial_port *port0;	/* first port structure on device */
	struct quatech2_port *port_extra;	/* extra data for this port */
	struct quatech2_port *port0_extra;	/* extra data for first port */
	struct qt2_status_data ChannelData;
	unsigned short default_divisor = QU2BOXSPD9600;
	unsigned char  default_LCR = SERIAL_8_DATA;
	int status;

	if (port_paranoia_check(port, __func__))
		return -ENODEV;

	dbg("%s - port %d\n", __func__, port->number);

	serial = port->serial;	/* get the parent device structure */
	if (serial_paranoia_check(serial, __func__))
		return -ENODEV;
	port0 = serial->port[0]; /* get the first port's device structure */

	port_extra = qt2_get_port_private(port);
	port0_extra = qt2_get_port_private(port0);

	if (port_extra == NULL || port0_extra == NULL)
		return -ENODEV;

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);
	port0_extra->open_ports++;

	/* FIXME: are these needed?  Does it even do anything useful? */
	/* get the modem and line status values from the UART */
	status = qt2_openboxchannel(serial, port->number,
			&ChannelData);
	if (status < 0) {
		dbg("qt2_openboxchannel on channel %d failed",
		    port->number);
		return status;
	}
	port_extra->shadowLSR = ChannelData.line_status &
			(SERIAL_LSR_OE | SERIAL_LSR_PE | SERIAL_LSR_FE |
			SERIAL_LSR_BI);

	port_extra->shadowMSR = ChannelData.modem_status &
			(SERIAL_MSR_CTS | SERIAL_MSR_DSR | SERIAL_MSR_RI |
			SERIAL_MSR_CD);

	port_extra->fifo_empty_flag = true;
	dbg("qt2_openboxchannel on channel %d completed.",
	    port->number);

	/* Set Baud rate to default and turn off flow control here */
	status = qt2_conf_uart(serial, port->number, default_divisor,
				default_LCR);
	if (status < 0) {
		dbg("qt2_conf_uart() failed on channel %d",
		    port->number);
		return status;
	}
	dbg("qt2_conf_uart() completed on channel %d",
		port->number);

	dbg("port number is %d", port->number);
	dbg("serial number is %d", port->serial->minor);

	/* We need to set up endpoints here. We only
	 * have one pair of endpoints per device, so in fact
	 * we only need to set up endpoints on the first time
	 * round, not subsequent ones.
	 * When we do a write to a port, we will use the same endpoint
	 * regardless of the port, with a 5-byte header added on to
	 * tell the box which port it should eventually come out of,
	 * so the same endpoint information needs to be visible to
	 * write calls regardless of which port is being written.
	 * To this end we actually keep the relevant endpoints
	 * in port 0's structure, because that's always there
	 * and avoids providing our own duplicate members in some
	 * user data structure for the same purpose.
	 * URBs will be allocated and freed dynamically as the are
	 * used, so are not touched here.
	 */
	if (port0_extra->open_ports == 1) {
		/* this is first port to be opened */
	}

	dbg("Bulkin endpoint is %d", port->bulk_in_endpointAddress);
	dbg("BulkOut endpoint is %d", port->bulk_out_endpointAddress);
	dbg("Interrupt endpoint is %d", port->interrupt_in_endpointAddress);

	/* initialize our wait queues */
	init_waitqueue_head(&port_extra->wait);

	/* remember to store port_extra and port0 back again at end !*/
	qt2_set_port_private(port, port_extra);
	qt2_set_port_private(serial->port[0], port0_extra);

	return 0;
}

/* internal, private helper functions for the driver */

/* Power up the FPGA in the box to get it working */
static int qt2_boxpoweron(struct usb_serial *serial)
{
	int result;
	__u8  Direcion;
	unsigned int pipe;
	Direcion = USBD_TRANSFER_DIRECTION_OUT;
	pipe = usb_rcvctrlpipe(serial->dev, 0);
	result = usb_control_msg(serial->dev, pipe, QT_SET_GET_DEVICE,
				Direcion, QU2BOXPWRON, 0x00, NULL, 0x00,
				5000);
	return result;
}

/*
 * qt2_boxsetQMCR Issue a QT_GET_SET_QMCR vendor-spcific request on the
 * default control pipe. If successful return the number of bytes written,
 * otherwise return a negative error number of the problem.
 */
static int qt2_boxsetQMCR(struct usb_serial *serial, __u16 Uart_Number,
			  __u8 QMCR_Value)
{
	int result;
	__u16 PortSettings;

	PortSettings = (__u16)(QMCR_Value);

	dbg("%s(): Port = %d, PortSettings = 0x%x", __func__,
			Uart_Number, PortSettings);

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				QT_GET_SET_QMCR, 0x40, PortSettings,
				(__u16)Uart_Number, NULL, 0, 5000);
	return result;
}

static int port_paranoia_check(struct usb_serial_port *port,
			       const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
	if (!port->serial) {
		dbg("%s - port->serial == NULL\n", function);
		return -1;
	}
	return 0;
}

static int serial_paranoia_check(struct usb_serial *serial,
				 const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL\n", function);
		return -1;
	}

	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}

static inline struct quatech2_port *qt2_get_port_private(struct usb_serial_port
		*port)
{
	return (struct quatech2_port *)usb_get_serial_port_data(port);
}

static inline void qt2_set_port_private(struct usb_serial_port *port,
				       struct quatech2_port *data)
{
	usb_set_serial_port_data(port, (void *)data);
}

static int qt2_openboxchannel(struct usb_serial *serial, __u16
		Uart_Number, struct qt2_status_data *status)
{
	int result;
	__u16 length;
	__u8  Direcion;
	unsigned int pipe;
	length = sizeof(struct qt2_status_data);
	Direcion = USBD_TRANSFER_DIRECTION_IN;
	pipe = usb_rcvctrlpipe(serial->dev, 0);
	result = usb_control_msg(serial->dev, pipe, QT_OPEN_CLOSE_CHANNEL,
			Direcion, 0x00, Uart_Number, status, length, 5000);
	return result;
}
static int qt2_closeboxchannel(struct usb_serial *serial, __u16 Uart_Number)
{
	int result;
	__u8  direcion;
	unsigned int pipe;
	direcion = USBD_TRANSFER_DIRECTION_OUT;
	pipe = usb_sndctrlpipe(serial->dev, 0);
	result = usb_control_msg(serial->dev, pipe, QT_OPEN_CLOSE_CHANNEL,
		  direcion, 0, Uart_Number, NULL, 0, 5000);
	return result;
}

/* qt2_conf_uart Issue a SET_UART vendor-spcific request on the default
 * control pipe. If successful sets baud rate divisor and LCR value
 */
static int qt2_conf_uart(struct usb_serial *serial,  unsigned short Uart_Number,
		      unsigned short divisor, unsigned char LCR)
{
	int result;
	unsigned short UartNumandLCR;

	UartNumandLCR = (LCR << 8) + Uart_Number;

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				QT_GET_SET_UART, 0x40, divisor, UartNumandLCR,
				NULL, 0, 300);
	return result;
}

/*
 * last things in file: stuff to register this driver into the generic
 * USB serial framework.
 */

static struct usb_serial_driver quatech2_device = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "quatech_usb2",
	},
	.description = DRIVER_DESC,
	.usb_driver = &quausb2_usb_driver,
	.id_table = quausb2_id_table,
	.num_ports = 8,
	.open = qt2_open,
	/*.close = qt_close,
	.write = qt_write,
	.write_room = qt_write_room,
	.chars_in_buffer = qt_chars_in_buffer,
	.throttle = qt_throttle,
	.unthrottle = qt_unthrottle,*/
	.calc_num_ports = qt2_calc_num_ports,
	/*.ioctl = qt_ioctl,
	.set_termios = qt_set_termios,
	.break_ctl = qt_break,
	.tiocmget = qt_tiocmget,
	.tiocmset = qt_tiocmset,*/
	.attach = qt2_attach,
	.release = qt2_release,
};

static int __init quausb2_usb_init(void)
{
	int retval;

	dbg("%s\n", __func__);

	/* register with usb-serial */
	retval = usb_serial_register(&quatech2_device);

	if (retval)
		goto failed_usb_serial_register;

	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION ":"
			DRIVER_DESC "\n");

	/* register with usb */

	retval = usb_register(&quausb2_usb_driver);
	if (retval == 0)
		return 0;

	/* if we're here, usb_register() failed */
	usb_serial_deregister(&quatech2_device);
failed_usb_serial_register:
		return retval;
}



static void __exit quausb2_usb_exit(void)
{
	usb_deregister(&quausb2_usb_driver);
	usb_serial_deregister(&quatech2_device);
}

module_init(quausb2_usb_init);
module_exit(quausb2_usb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
