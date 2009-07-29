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
#define FIFO_DEPTH 1024			/* size of hardware fifos */
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

/* handy macros for doing escape sequence parsing on data reads */
#define THISCHAR	((unsigned char *)(urb->transfer_buffer))[i]
#define NEXTCHAR	((unsigned char *)(urb->transfer_buffer))[i + 1]
#define THIRDCHAR	((unsigned char *)(urb->transfer_buffer))[i + 2]
#define FOURTHCHAR	((unsigned char *)(urb->transfer_buffer))[i + 3]
#define FIFTHCHAR	((unsigned char *)(urb->transfer_buffer))[i + 4]

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

/** structure in which to keep all the messy stuff that this driver needs
 * alongside the usb_serial_port structure
 * @param read_urb_busy Flag indicating that port->read_urb is in use
 * @param close_pending flag indicating that this port is in the process of
 * being closed.
 * @param shadowLSR Last received state of the line status register, holds the
 * value of the line status flags from the port
 * @param shadowMSR Last received state of the modem status register, holds
 * the value of the modem status received from the port
 * @param xmit_pending_bytes Number of bytes waiting to be sent out of
 * the serial port
 * @param xmit_fifo_room_bytes free space available in the transmit fifo
 * for this port on the box
 * @param rcv_flush Flag indicating that a receive flush has been requested by
 * the hardware.
 * @param xmit_flush Flag indicating that a transmit flush has been requested by
 * the hardware.
 */
struct quatech2_port {
	int	magic;
	bool	read_urb_busy;
	bool	close_pending;
	__u8	shadowLSR;
	__u8	shadowMSR;
	int	xmit_pending_bytes;
	int	xmit_fifo_room_bytes;
	char	rcv_flush;
	char	xmit_flush;

	char	active;		/* someone has this device open */
	unsigned char		*xfer_to_tty_buffer;
	wait_queue_head_t	wait;
	int	open_count;	/* number of times this	port has been opened */
	struct semaphore	sem;	/* locks this structure */
	__u8	shadowLCR;	/* last LCR value received */
	__u8	shadowMCR;	/* last MCR value received */
	char	RxHolding;
	char	fifo_empty_flag;
	struct semaphore	pend_xmit_sem;	/* locks this structure */
	spinlock_t lock;
};

/**
 * Structure to hold device-wide internal status information
 * @param ReadBulkStopped The last bulk read attempt ended in tears
 * @param open_ports The number of serial ports currently in use on the box
 * @param current_port Pointer to the serial port structure of the port which
 * the read stream is currently directed to. Escape sequences in the read
 * stream will change this around as data arrives from different ports on the
 * box
 */
struct quatech2_dev {
	bool	ReadBulkStopped;
	char	open_ports;
	struct usb_serial_port *current_port;
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
static inline struct quatech2_dev *qt2_get_dev_private(struct usb_serial
			*serial);
static inline void qt2_set_dev_private(struct usb_serial *serial,
			struct quatech2_dev *data);
static int qt2_openboxchannel(struct usb_serial *serial, __u16
			Uart_Number, struct qt2_status_data *pDeviceData);
static int qt2_closeboxchannel(struct usb_serial *serial, __u16
			Uart_Number);
static int qt2_conf_uart(struct usb_serial *serial,  unsigned short Uart_Number,
			 unsigned short divisor, unsigned char LCR);
static void qt2_read_bulk_callback(struct urb *urb);
static void qt2_process_line_status(struct usb_serial_port *port,
			      unsigned char LineStatus);
static void qt2_process_modem_status(struct usb_serial_port *port,
			       unsigned char ModemStatus);
static void qt2_process_xmit_empty(struct usb_serial_port *port,
	unsigned char fourth_char, unsigned char fifth_char);
static void qt2_process_port_change(struct usb_serial_port *port,
			      unsigned char New_Current_Port);
static void qt2_process_rcv_flush(struct usb_serial_port *port);
static void qt2_process_xmit_flush(struct usb_serial_port *port);
static void qt2_process_rx_char(struct usb_serial_port *port,
				unsigned char data);

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
	struct quatech2_port *qt2_port;	/* port-specific private data pointer */
	struct quatech2_dev  *qt2_dev;	/* dev-specific private data pointer */
	int i;
	/* stuff for storing endpoint addresses now */
	struct usb_endpoint_descriptor *endpoint;
	struct usb_host_interface *iface_desc;
	struct usb_serial_port *port0;	/* first port structure on device */

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

	/* Set up per-device private data, storing extra data alongside
	 * struct usb_serial */
	qt2_dev = kzalloc(sizeof(*qt2_dev), GFP_KERNEL);
	if (!qt2_dev) {
		dbg("%s: kmalloc for quatech2_dev failed!",
		    __func__);
		return -ENOMEM;
	}
	qt2_dev->open_ports = 0;	/* no ports open */
	qt2_set_dev_private(serial, qt2_dev);	/* store private data */

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
		usb_set_serial_port_data(port, qt2_port);
	}

	/* gain access to port[0]'s structure because we want to store
	 * device-level stuff in it */
	if (serial_paranoia_check(serial, __func__))
		return -ENODEV;
	port0 = serial->port[0]; /* get the first port's device structure */

	/* print endpoint addresses so we can check them later
	 * by hand */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;
		if ((endpoint->bEndpointAddress & 0x80) &&
			((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			dbg("found bulk in at 0x%x",
				endpoint->bEndpointAddress);
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
			((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out at 0x%x",
				endpoint->bEndpointAddress);
		}
	}	/* end printing endpoint addresses */

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
		qt2_set_port_private(port, NULL);
	}
	qt2_dev = qt2_get_dev_private(serial);
	kfree(qt2_dev);
	qt2_set_dev_private(serial, NULL);

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
	struct quatech2_dev *dev_extra;		/* extra data for the device */
	struct qt2_status_data ChannelData;
	unsigned short default_divisor = QU2BOXSPD9600;
	unsigned char  default_LCR = SERIAL_8_DATA;
	int status;
	int result;

	if (port_paranoia_check(port, __func__))
		return -ENODEV;

	dbg("%s(): port %d", __func__, port->number);

	serial = port->serial;	/* get the parent device structure */
	if (serial_paranoia_check(serial, __func__)) {
		dbg("usb_serial struct failed sanity check");
		return -ENODEV;
	}
	dev_extra = qt2_get_dev_private(serial);
	/* get the device private data */
	port0 = serial->port[0]; /* get the first port's device structure */
	if (port_paranoia_check(port, __func__)) {
		dbg("port0 usb_serial_port struct failed sanity check");
		return -ENODEV;
	}
	port_extra = qt2_get_port_private(port);
	port0_extra = qt2_get_port_private(port0);

	if (port_extra == NULL || port0_extra == NULL) {
		dbg("failed to get private data for port and port0");
		return -ENODEV;
	}

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

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

	/*
	 * At this point we will need some end points to make further progress.
	 * Handlily, the correct endpoint addresses have been filled out into
	 * the usb_serial_port structure for us by the driver core, so we
	 * already have access to them.
	 * As there is only one bulk in and one bulk out end-point, these are in
	 * port[0]'s structure, and the rest are uninitialised. Handily,
	 * when we do a write to a port, we will use the same endpoint
	 * regardless of the port, with a 5-byte header added on to
	 * tell the box which port it should eventually come out of, so we only
	 * need the one set of endpoints.
	 * Finally we need a bulk in URB to use for background reads from the
	 * device, which will deal with uplink data from the box to host.
	 */
	dbg("port number is %d", port->number);
	dbg("serial number is %d", port->serial->minor);
	dbg("port0 bulk in endpoint is %#.2x", port0->bulk_in_endpointAddress);
	dbg("port0 bulk out endpoint is %#.2x",
		port0->bulk_out_endpointAddress);

	if (dev_extra->open_ports == 0) {
		/* this is first port to be opened, so need some URBs */
		/* initialise read_urb for bulk in transfers */
		usb_fill_bulk_urb(port0->read_urb, serial->dev,
			usb_rcvbulkpipe(serial->dev,
			port0->bulk_in_endpointAddress),
			port0->bulk_in_buffer,
			port0->bulk_in_size,
			qt2_read_bulk_callback, serial);
		dbg("port0 bulk in URB intialised");

		/* submit URB, i.e. start reading from device (async) */
		dev_extra->ReadBulkStopped = false;
		port_extra->read_urb_busy = true;
		result = usb_submit_urb(port->read_urb, GFP_KERNEL);
		if (result) {
			dev_err(&port->dev,
				 "%s - Error %d submitting bulk in urb\n",
				__func__, result);
			port_extra->read_urb_busy = false;
		}
	}

	/* initialize our wait queues */
	init_waitqueue_head(&port_extra->wait);

	/* remember to store port_extra and port0 back again at end !*/
	qt2_set_port_private(port, port_extra);
	qt2_set_port_private(serial->port[0], port0_extra);
	qt2_set_dev_private(serial, dev_extra);

	dev_extra->open_ports++;	/* one more port opened */

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

static inline struct quatech2_dev *qt2_get_dev_private(struct usb_serial
		*serial)
{
	return (struct quatech2_dev *)usb_get_serial_data(serial);
}
static inline void qt2_set_dev_private(struct usb_serial *serial,
		struct quatech2_dev *data)
{
	usb_set_serial_data(serial, (void *)data);
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

/** @brief Callback for asynchronous submission of URBs on bulk in
 * endpoints
 *
 * Registered in qt2_open_port(), used to deal with incomming data
 * from the box.
 */
static void qt2_read_bulk_callback(struct urb *urb)
{
	/* Get the device pointer (struct usb_serial) back out of the URB */
	struct usb_serial *serial = urb->context;
	/* get the extra struct for the device */
	struct quatech2_dev *dev_extra = qt2_get_dev_private(serial);
	/* Get first port structure from the device */
	struct usb_serial_port *port0 = serial->port[0];
	/* Get the currently active port structure from serial struct */
	struct usb_serial_port *active = dev_extra->current_port;
	/* get the extra struct for port 0 */
	struct quatech2_port *port0_extra = qt2_get_port_private(port0);
	/* and for the currently active port */
	struct quatech2_port *active_extra = qt2_get_port_private(active);
	/* When we finally get to doing some tty stuff, we will need this */
	struct tty_struct *tty_st;
	unsigned int RxCount;	/* the length of the data to process */
	unsigned int i;	/* loop counter over the data to process */
	int result;	/* return value cache variable */
	bool escapeflag;	/* flag set to true if this loop iteration is
				 * parsing an escape sequence, rather than
				 * ordinary data */


	dbg("%s(): callback running", __func__);

	if (urb->status) {
		/* read didn't go well */
		dev_extra->ReadBulkStopped = true;
		dbg("%s(): nonzero write bulk status received: %d",
			__func__, urb->status);
		return;
	}

	/* inline port_sofrint() here */
	if (port_paranoia_check(port0, __func__) != 0) {
		dbg("%s - port_paranoia_check on port0 failed, exiting\n",
__func__);
		return;
	}
	if (port_paranoia_check(active, __func__) != 0) {
		dbg("%s - port_paranoia_check on current_port "
			"failed, exiting", __func__);
		return;
	}

/* This single callback function has to do for all the ports on
 * the device. Data being read up the USB can contain certain
 * escape sequences which are used to communicate out-of-band
 * information from the serial port in-band over the USB.
 * These escapes include sending modem and flow control line
 * status, and switching the port. The concept of a "Current Port"
 * is used, which is where data is going until a port change
 * escape seqence is received. This Current Port is kept between
 * callbacks so that when this function enters we know which the
 * currently active port is and can get to work right away without
 * the box having to send repeat escape sequences (anyway, how
 * would it know to do so?).
 */

	if (active_extra->close_pending == true) {
		/* We are closing , stop reading */
		dbg("%s - (active->close_pending == true", __func__);
		if (dev_extra->open_ports <= 0) {
			/* If this is the only port left open - stop the
			 * bulk read */
			dev_extra->ReadBulkStopped = true;
			dbg("%s - (ReadBulkStopped == true;", __func__);
			return;
		}
	}

	/*
	 * RxHolding is asserted by throttle, if we assert it, we're not
	 * receiving any more characters and let the box handle the flow
	 * control
	 */
	if ((port0_extra->RxHolding == true) &&
		    (serial->dev->descriptor.idProduct == QUATECH_SSU2_100)) {
		/* single port device, input is already stopped, so we don't
		 * need any more input data */
		dev_extra->ReadBulkStopped = true;
			return;
	}
	/* finally, we are in a situation where we might consider the data
	 * that is contained within the URB, and what to do about it.
	 * This is likely to involved communicating up to the TTY layer, so
	 * we will need to get hold of the tty for the port we are currently
	 * dealing with */

	/* active is a usb_serial_port. It has a member port which is a
	 * tty_port. From this we get a tty_struct pointer which is what we
	 * actually wanted, and keep it on tty_st */
	tty_st = tty_port_tty_get(&active->port);
	if (!tty_st) {
		dbg("%s - bad tty pointer - exiting", __func__);
		return;
	}
	dbg("%s(): active port %d, tty_st =0x%p\n", __func__, active->number,
		tty_st);
	RxCount = urb->actual_length;	/* grab length of data handy */

	if (RxCount) {
		/* skip all this if no data to process */
		for (i = 0; i < RxCount ; ++i) {
			/* Look ahead code here -works on several bytes at onc*/
			if ((i <= (RxCount - 3)) && (THISCHAR == 0x1b)
				&& (NEXTCHAR == 0x1b)) {
				/* we are in an escape sequence, type
				 * determined by the 3rd char */
				escapeflag = false;
				switch (THIRDCHAR) {
				case 0x00:
					/* Line status change 4th byte must
					 * follow */
					if (i > (RxCount - 4)) {
						dbg("Illegal escape sequences "
						"in received data");
						break;
					}
					qt2_process_line_status(active,
						FOURTHCHAR);
					i += 3;
					escapeflag = true;
					break;
				case 0x01:
					/* Modem status status change 4th byte
					 * must follow */
					if (i > (RxCount - 4)) {
						dbg("Illegal escape sequences "
						"in received data");
						break;
					}
					qt2_process_modem_status(active,
						FOURTHCHAR);
					i += 3;
					escapeflag = true;
					break;
				case 0x02:
					/* xmit hold empty 4th byte
					 * must follow */
					if (i > (RxCount - 4)) {
						dbg("Illegal escape sequences "
						"in received data");
						break;
					}
					qt2_process_xmit_empty(active,
						FOURTHCHAR,
							FIFTHCHAR);
					i += 4;
					escapeflag = true;
					break;
				case 0x03:
					/* Port number change 4th byte
					 * must follow */
					if (i > (RxCount - 4)) {
						dbg("Illegal escape sequences "
						"in received data");
						break;
					}
					/* Port change. If port open push
					 * current data up to tty layer */
					if (dev_extra->open_ports > 0)
						tty_flip_buffer_push(tty_st);

					dbg("Port Change: new port = %d",
						FOURTHCHAR);
					qt2_process_port_change(active,
						FOURTHCHAR);
					i += 3;
					escapeflag = true;
					/* having changed port, the pointers for
					 * the currently active port are all out
					 * of date and need updating */
					active = dev_extra->current_port;
					active_extra =
						qt2_get_port_private(active);
					tty_st = tty_port_tty_get(
						&active->port);
					break;
				case 0x04:
					/* Recv flush 3rd byte must
					 * follow */
					if (i > (RxCount - 3)) {
						dbg("Illegal escape sequences "
							"in received data");
						break;
					}
					qt2_process_rcv_flush(active);
					i += 2;
					escapeflag = true;
					break;
				case 0x05:
					/* xmit flush 3rd byte must follow */
					if (i > (RxCount - 3)) {
						dbg("Illegal escape sequences "
						"in received data");
						break;
					}
					qt2_process_xmit_flush(active);
					i += 2;
					escapeflag = true;
					break;
				case 0xff:
					dbg("No status sequence");
					qt2_process_rx_char(active, THISCHAR);
					qt2_process_rx_char(active, NEXTCHAR);
					i += 2;
					break;
				default:
					qt2_process_rx_char(active, THISCHAR);
					i += 1;
					break;
				} /*end switch*/
				if (escapeflag == true)
					continue;
				/* if we did an escape char, we don't need
				 * to mess around pushing data through the
				 * tty layer, and can go round again */
			} /*endif*/
			if (tty_st && urb->actual_length) {
				tty_buffer_request_room(tty_st, 1);
				tty_insert_flip_string(tty_st,
					&((unsigned char *)(urb->transfer_buffer)
						)[i],
					1);
			}
		} /*endfor*/
		tty_flip_buffer_push(tty_st);
	} /*endif*/

	/* at this point we have complete dealing with the data for this
	 * callback. All we have to do now is to start the async read process
	 * back off again. */

	usb_fill_bulk_urb(port0->read_urb, serial->dev,
		usb_rcvbulkpipe(serial->dev, port0->bulk_in_endpointAddress),
		port0->bulk_in_buffer, port0->bulk_in_size,
		qt2_read_bulk_callback, serial);
	result = usb_submit_urb(port0->read_urb, GFP_ATOMIC);
	if (result) {
		dbg("%s(): failed resubmitting read urb, error %d",
			__func__, result);
	} else {
		if (tty_st && RxCount) {
			/* if some inbound data was processed, then
			 * we need to push that through the tty layer
			 */
			tty_flip_buffer_push(tty_st);
			tty_schedule_flip(tty_st);
		}
	}

	/* cribbed from serqt_usb2 driver, but not sure which work needs
	 * scheduling - port0 or currently active port? */
	/* schedule_work(&port->work); */

	return;
}
static void qt2_process_line_status(struct usb_serial_port *port,
	unsigned char LineStatus)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->shadowLSR = LineStatus & (SERIAL_LSR_OE | SERIAL_LSR_PE |
		SERIAL_LSR_FE | SERIAL_LSR_BI);
}
static void qt2_process_modem_status(struct usb_serial_port *port,
	unsigned char ModemStatus)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->shadowMSR = ModemStatus;
	/* ?? */
	wake_up_interruptible(&port_extra->wait);
}

static void qt2_process_xmit_empty(struct usb_serial_port *port,
	unsigned char fourth_char, unsigned char fifth_char)
{
	int byte_count;
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);

	byte_count = (int)(fifth_char * 16);
	byte_count +=  (int)fourth_char;
	port_extra->xmit_pending_bytes -= (int)byte_count;
	port_extra->xmit_fifo_room_bytes = FIFO_DEPTH;
}

static void qt2_process_port_change(struct usb_serial_port *port,
	unsigned char New_Current_Port)
{
	/* obtain the parent usb serial device structure */
	struct usb_serial *serial = port->serial;
	/* obtain the private structure for the device */
	struct quatech2_dev *dev_extra = qt2_get_dev_private(serial);
	dev_extra->current_port = serial->port[New_Current_Port];
	/* what should I do with this? commented out in upstream
	 * driver */
	/*schedule_work(&port->work);*/
}

static void qt2_process_rcv_flush(struct usb_serial_port *port)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->rcv_flush = true;
}
static void qt2_process_xmit_flush(struct usb_serial_port *port)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->xmit_flush = true;
}

static void qt2_process_rx_char(struct usb_serial_port *port,
	unsigned char data)
{
	/* get the tty_struct for this port */
	struct tty_struct *tty = tty_port_tty_get(&(port->port));
	/* get the URB with the data in to push */
	struct urb *urb = port->serial->port[0]->read_urb;

	if (tty && urb->actual_length) {
		tty_buffer_request_room(tty, 1);
		tty_insert_flip_string(tty, &data, 1);
		/* should this be commented out here? */
		/*tty_flip_buffer_push(tty);*/
	}
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
