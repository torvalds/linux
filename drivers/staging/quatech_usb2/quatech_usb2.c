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
#define QT2_FIFO_DEPTH 1024			/* size of hardware fifos */
#define QT2_TX_HEADER_LENGTH	5
/* length of the header sent to the box with each write URB */

/* directions for USB transfers */
#define USBD_TRANSFER_DIRECTION_IN    0xc0
#define USBD_TRANSFER_DIRECTION_OUT   0x40

/* special Quatech command IDs. These are pushed down the
 USB control pipe to get the box on the end to do things */
#define QT_SET_GET_DEVICE		0xc2
#define QT_OPEN_CLOSE_CHANNEL		0xca
/*#define QT_GET_SET_PREBUF_TRIG_LVL	0xcc
#define QT_SET_ATF			0xcd*/
#define QT2_GET_SET_REGISTER			0xc0
#define QT2_GET_SET_UART			0xc1
#define QT2_HW_FLOW_CONTROL_MASK		0xc5
#define QT2_SW_FLOW_CONTROL_MASK		0xc6
#define QT2_SW_FLOW_CONTROL_DISABLE		0xc7
#define QT2_BREAK_CONTROL 			0xc8
#define QT2_STOP_RECEIVE			0xe0
#define QT2_FLUSH_DEVICE			0xc4
#define QT2_GET_SET_QMCR			0xe1

/* sorts of flush we can do on */
#define QT2_FLUSH_RX			0x00
#define QT2_FLUSH_TX			0x01

/* port setting constants, used to set up serial port speeds, flow
 * control and so on */
#define QT2_SERIAL_MCR_DTR	0x01
#define QT2_SERIAL_MCR_RTS	0x02
#define QT2_SERIAL_MCR_LOOP	0x10

#define QT2_SERIAL_MSR_CTS	0x10
#define QT2_SERIAL_MSR_CD	0x80
#define QT2_SERIAL_MSR_RI	0x40
#define QT2_SERIAL_MSR_DSR	0x20
#define QT2_SERIAL_MSR_MASK	0xf0

#define QT2_SERIAL_8_DATA	0x03
#define QT2_SERIAL_7_DATA	0x02
#define QT2_SERIAL_6_DATA	0x01
#define QT2_SERIAL_5_DATA	0x00

#define QT2_SERIAL_ODD_PARITY	0x08
#define QT2_SERIAL_EVEN_PARITY	0x18
#define QT2_SERIAL_TWO_STOPB	0x04
#define QT2_SERIAL_ONE_STOPB	0x00

#define QT2_MAX_BAUD_RATE	921600
#define QT2_MAX_BAUD_REMAINDER	4608

#define QT2_SERIAL_LSR_OE	0x02
#define QT2_SERIAL_LSR_PE	0x04
#define QT2_SERIAL_LSR_FE	0x08
#define QT2_SERIAL_LSR_BI	0x10

/* value of Line Status Register when UART has completed
 * emptying data out on the line */
#define QT2_LSR_TEMT     0x40

/* register numbers on each UART, for use with  qt2_box_[get|set]_register*/
#define  QT2_XMT_HOLD_REGISTER          0x00
#define  QT2_XVR_BUFFER_REGISTER        0x00
#define  QT2_FIFO_CONTROL_REGISTER      0x02
#define  QT2_LINE_CONTROL_REGISTER      0x03
#define  QT2_MODEM_CONTROL_REGISTER     0x04
#define  QT2_LINE_STATUS_REGISTER       0x05
#define  QT2_MODEM_STATUS_REGISTER      0x06

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

/**
 * quatech2_port: Structure in which to keep all the messy stuff that this
 * driver needs alongside the usb_serial_port structure
 * @read_urb_busy: Flag indicating that port->read_urb is in use
 * @close_pending: flag indicating that this port is in the process of
 * being closed (and so no new reads / writes should be started).
 * @shadowLSR: Last received state of the line status register, holds the
 * value of the line status flags from the port
 * @shadowMSR: Last received state of the modem status register, holds
 * the value of the modem status received from the port
 * @rcv_flush: Flag indicating that a receive flush has occured on
 * the hardware.
 * @xmit_flush: Flag indicating that a transmit flush has been processed by
 * the hardware.
 * @tx_pending_bytes: Number of bytes waiting to be sent. This total
 * includes the size (excluding header) of URBs that have been submitted but
 * have not yet been sent to to the device, and bytes that have been sent out
 * of the port but not yet reported sent by the "xmit_empty" messages (which
 * indicate the number of bytes sent each time they are recieved, despite the
 * misleading name).
 * - Starts at zero when port is initialised.
 * - is incremented by the size of the data to be written (no headers)
 * each time a write urb is dispatched.
 * - is decremented each time a "transmit empty" message is received
 * by the driver in the data stream.
 * @lock: Mutex to lock access to this structure when we need to ensure that
 * races don't occur to access bits of it.
 * @open_count: The number of uses of the port currently having
 * it open, i.e. the reference count.
 */
struct quatech2_port {
	int	magic;
	bool	read_urb_busy;
	bool	close_pending;
	__u8	shadowLSR;
	__u8	shadowMSR;
	bool	rcv_flush;
	bool	xmit_flush;
	int	tx_pending_bytes;
	struct mutex modelock;
	int	open_count;

	char	active;		/* someone has this device open */
	unsigned char		*xfer_to_tty_buffer;
	wait_queue_head_t	wait;
	__u8	shadowLCR;	/* last LCR value received */
	__u8	shadowMCR;	/* last MCR value received */
	char	RxHolding;
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
 * @buffer_size: The max size buffer each URB can take, used to set the size of
 * the buffers allocated for writing to each port on the device (we need to
 * store this because it is known only to the endpoint, but used each time a
 * port is opened and a new buffer is allocated.
 */
struct quatech2_dev {
	bool	ReadBulkStopped;
	char	open_ports;
	struct usb_serial_port *current_port;
	int 	buffer_size;
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
static void qt2_write_bulk_callback(struct urb *urb);
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
static int qt2_box_get_register(struct usb_serial *serial,
		unsigned char uart_number, unsigned short register_num,
		__u8 *pValue);
static int qt2_box_set_register(struct usb_serial *serial,
		unsigned short Uart_Number, unsigned short Register_Num,
		unsigned short Value);
static int qt2_box_flush(struct usb_serial *serial,  unsigned char uart_number,
		unsigned short rcv_or_xmit);
static int qt2_boxsetuart(struct usb_serial *serial, unsigned short Uart_Number,
		unsigned short default_divisor, unsigned char default_LCR);
static int qt2_boxsethw_flowctl(struct usb_serial *serial,
		unsigned int UartNumber, bool bSet);
static int qt2_boxsetsw_flowctl(struct usb_serial *serial, __u16 UartNumber,
		unsigned char stop_char,  unsigned char start_char);
static int qt2_boxunsetsw_flowctl(struct usb_serial *serial, __u16 UartNumber);
static int qt2_boxstoprx(struct usb_serial *serial, unsigned short uart_number,
			 unsigned short stop);

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
		/* initialise stuff in the structure */
		qt2_port->open_count = 0;	/* port is not open */
		spin_lock_init(&qt2_port->lock);
		mutex_init(&qt2_port->modelock);
		qt2_set_port_private(port, qt2_port);
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
			dbg("found bulk in at %#.2x",
				endpoint->bEndpointAddress);
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
			((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			dbg("found bulk out at %#.2x",
				endpoint->bEndpointAddress);
			qt2_dev->buffer_size = endpoint->wMaxPacketSize;
			/* max size of URB needs recording for the device */
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
/* This function is called once per serial port on the device, when
 * that port is opened by a userspace application.
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
int qt2_open(struct tty_struct *tty, struct usb_serial_port *port)
{
	struct usb_serial *serial;	/* device structure */
	struct usb_serial_port *port0;	/* first port structure on device */
	struct quatech2_port *port_extra;       /* extra data for this port */
	struct quatech2_port *port0_extra;	/* extra data for first port */
	struct quatech2_dev *dev_extra;		/* extra data for the device */
	struct qt2_status_data ChannelData;
	unsigned short default_divisor = QU2BOXSPD9600;
	unsigned char  default_LCR = QT2_SERIAL_8_DATA;
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
	if (dev_extra == NULL) {
		dbg("device extra data pointer is null");
		return -ENODEV;
	}
	port0 = serial->port[0]; /* get the first port's device structure */
	if (port_paranoia_check(port0, __func__)) {
		dbg("port0 usb_serial_port struct failed sanity check");
		return -ENODEV;
	}

	port_extra = qt2_get_port_private(port);
	port0_extra = qt2_get_port_private(port0);
	if (port_extra == NULL || port0_extra == NULL) {
		dbg("failed to get private data for port or port0");
		return -ENODEV;
	}

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
			(QT2_SERIAL_LSR_OE | QT2_SERIAL_LSR_PE |
			QT2_SERIAL_LSR_FE | QT2_SERIAL_LSR_BI);
	port_extra->shadowMSR = ChannelData.modem_status &
			(QT2_SERIAL_MSR_CTS | QT2_SERIAL_MSR_DSR |
			QT2_SERIAL_MSR_RI | QT2_SERIAL_MSR_CD);

/*	port_extra->fifo_empty_flag = true;*/
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
	 * need the one set of endpoints. We will have one URB per port for
	 * writing, so that multiple ports can be writing at once.
	 * Finally we need a bulk in URB to use for background reads from the
	 * device, which will deal with uplink data from the box to host.
	 */
	dbg("port0 bulk in endpoint is %#.2x", port0->bulk_in_endpointAddress);
	dbg("port0 bulk out endpoint is %#.2x",
		port0->bulk_out_endpointAddress);

	/* set up write_urb for bulk out transfers on this port. The USB
	 * serial framework will have allocated a blank URB, buffer etc for
	 * port0 when it put the endpoints there, but not for any of the other
	 * ports on the device because there are no more endpoints. Thus we
	 * have to allocate our own URBs for ports 1-7
	 */
	if (port->write_urb == NULL) {
		dbg("port->write_urb == NULL, allocating one");
		port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port->write_urb) {
			err("Allocating write URB failed");
			return -ENOMEM;
		}
		/* buffer same size as port0 */
		port->bulk_out_size = dev_extra->buffer_size;
		port->bulk_out_buffer = kmalloc(port->bulk_out_size,
						GFP_KERNEL);
		if (!port->bulk_out_buffer) {
			err("Couldn't allocate bulk_out_buffer");
			return -ENOMEM;
		}
	}
	if (serial->dev == NULL)
		dbg("serial->dev == NULL");
	dbg("port->bulk_out_size is %d", port->bulk_out_size);

	usb_fill_bulk_urb(port->write_urb, serial->dev,
			usb_sndbulkpipe(serial->dev,
			port0->bulk_out_endpointAddress),
			port->bulk_out_buffer,
			port->bulk_out_size,
			qt2_write_bulk_callback,
			port);
	port_extra->tx_pending_bytes = 0;

	if (dev_extra->open_ports == 0) {
		/* this is first port to be opened, so need the read URB
		 * initialised for bulk in transfers (this is shared amongst
		 * all the ports on the device) */
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
				 "%s(): Error %d submitting bulk in urb",
				__func__, result);
			port_extra->read_urb_busy = false;
			dev_extra->ReadBulkStopped = true;
		}

		/* When the first port is opened, initialise the value of
		 * current_port in dev_extra to this port, so it is set
		 * to something. Once the box sends data it will send the
		 * relevant escape sequences to get it to the right port anyway
		 */
		dev_extra->current_port = port;
	}

	/* initialize our wait queues */
	init_waitqueue_head(&port_extra->wait);
	/* increment the count of openings of this port by one */
	port_extra->open_count++;

	/* remember to store dev_extra, port_extra and port0_extra back again at
	 * end !*/
	qt2_set_port_private(port, port_extra);
	qt2_set_port_private(serial->port[0], port0_extra);
	qt2_set_dev_private(serial, dev_extra);

	dev_extra->open_ports++; /* one more port opened */

	return 0;
}

/* called when a port is closed by userspace. It won't be called, however,
 * until calls to chars_in_buffer() reveal that the port has completed
 * sending buffered data, and there is nothing else to do. Thus we don't have
 * to rely on forcing data through in this function. */
/* Setting close_pending should keep new data from being written out,
 * once all the data in the enpoint buffers is moved out we won't get
 * any more. */
/* BoxStopReceive would keep any more data from coming from a given
 * port, but isn't called by the vendor driver, although their comments
 * mention it. Should it be used here to stop the inbound data
 * flow?
 */
static void qt2_close(struct usb_serial_port *port)
{
	/* time out value for flush loops */
	unsigned long jift;
	struct quatech2_port *port_extra;	/* extra data for this port */
	struct usb_serial *serial;	/* device structure */
	struct quatech2_dev *dev_extra; /* extra data for the device */
	__u8  lsr_value = 0;	/* value of Line Status Register */
	int status;	/* result of last USB comms function */

	dbg("%s(): port %d", __func__, port->number);
	serial = port->serial;	/* get the parent device structure */
	dev_extra = qt2_get_dev_private(serial);
	/* get the device private data */
	port_extra = qt2_get_port_private(port); /* port private data */

	/* we don't need to force flush though the hardware, so we skip using
	 * qt2_box_flush() here */

	/* we can now (and only now) stop reading data */
	port_extra->close_pending = true;
	dbg("%s(): port_extra->close_pending = true", __func__);
	/* although the USB side is now empty, the UART itself may
	 * still be pushing characters out over the line, so we have to
	 * wait testing the actual line status until the lines change
	 * indicating that the data is done transfering. */
	/* FIXME: slow this polling down so it doesn't run the USB bus flat out
	 * if it actually has to spend any time in this loop (which it normally
	 * doesn't because the buffer is nearly empty) */
	jift = jiffies + (10 * HZ);	/* 10 sec timeout */
	do {
		status = qt2_box_get_register(serial, port->number,
			QT2_LINE_STATUS_REGISTER, &lsr_value);
		if (status < 0) {
			dbg("%s(): qt2_box_get_register failed", __func__);
			break;
		}
		if ((lsr_value & QT2_LSR_TEMT)) {
			dbg("UART done sending");
			break;
		}
		schedule();
	} while (jiffies <= jift);

	status = qt2_closeboxchannel(serial, port->number);
	if (status < 0)
		dbg("%s(): port %d qt2_box_open_close_channel failed",
			__func__, port->number);
	/* to avoid leaking URBs, we should now free the write_urb for this
	 * port and set the pointer to null so that next time the port is opened
	 * a new URB is allocated. This avoids leaking URBs when the device is
	 * removed */
	usb_free_urb(port->write_urb);
	kfree(port->bulk_out_buffer);
	port->bulk_out_buffer = NULL;
	port->bulk_out_size = 0;

	/* decrement the count of openings of this port by one */
	port_extra->open_count--;
	/* one less overall open as well */
	dev_extra->open_ports--;
	dbg("%s(): Exit, dev_extra->open_ports  = %d", __func__,
		dev_extra->open_ports);
}

/**
 * qt2_write - write bytes from the tty layer out to the USB device.
 * @buf: The data to be written, size at least count.
 * @count: The number of bytes requested for transmission.
 * @return The number of bytes actually accepted for transmission to the device.
 */
static int qt2_write(struct tty_struct *tty, struct usb_serial_port *port,
		const unsigned char *buf, int count)
{
	struct usb_serial *serial;	/* parent device struct */
	__u8 header_array[5];	/* header used to direct writes to the correct
	port on the device */
	struct quatech2_port *port_extra;	/* extra data for this port */
	int result;

	serial = port->serial; /* get the parent device of the port */
	port_extra = qt2_get_port_private(port); /* port extra info */
	if (serial == NULL)
		return -ENODEV;
	dbg("%s(): port %d, requested to write %d bytes, %d already pending",
		__func__, port->number, count, port_extra->tx_pending_bytes);

	if (count <= 0)	{
		dbg("%s(): write request of <= 0 bytes", __func__);
		return 0;	/* no bytes written */
	}

	/* check if the write urb is already in use, i.e. data already being
	 * sent to this port */
	if ((port->write_urb->status == -EINPROGRESS)) {
		/* Fifo hasn't been emptied since last write to this port */
		dbg("%s(): already writing, port->write_urb->status == "
			"-EINPROGRESS", __func__);
		/* schedule_work(&port->work); commented in vendor driver */
		return 0;
	} else if (port_extra->tx_pending_bytes >= QT2_FIFO_DEPTH) {
		/* buffer is full (==). > should not occur, but would indicate
		 * that an overflow had occured */
		dbg("%s(): port transmit buffer is full!", __func__);
		/* schedule_work(&port->work); commented in vendor driver */
		return 0;
	}

	/* We must fill the first 5 bytes of anything we sent with a transmit
	 * header which directes the data to the correct port. The maximum
	 * size we can send out in one URB is port->bulk_out_size, which caps
	 * the number of bytes of real data we can send in each write. As the
	 * semantics of write allow us to write less than we were give, we cap
	 * the maximum we will ever write to the device as 5 bytes less than
	 * one URB's worth, by reducing the value of the count argument
	 * appropriately*/
	if (count > port->bulk_out_size - QT2_TX_HEADER_LENGTH) {
		count = port->bulk_out_size - QT2_TX_HEADER_LENGTH;
		dbg("%s(): write request bigger than urb, only accepting "
			"%d bytes", __func__, count);
	}
	/* we must also ensure that the FIFO at the other end can cope with the
	 * URB we send it, otherwise it will have problems. As above, we can
	 * restrict the write size by just shrinking count.*/
	if (count > (QT2_FIFO_DEPTH - port_extra->tx_pending_bytes)) {
		count = QT2_FIFO_DEPTH - port_extra->tx_pending_bytes;
		dbg("%s(): not enough room in buffer, only accepting %d bytes",
			__func__, count);
	}
	/* now build the header for transmission */
	header_array[0] = 0x1b;
	header_array[1] = 0x1b;
	header_array[2] = (__u8)port->number;
	header_array[3] = (__u8)count;
	header_array[4] = (__u8)count >> 8;
	/* copy header into URB */
	memcpy(port->write_urb->transfer_buffer, header_array,
		QT2_TX_HEADER_LENGTH);
	/* and actual data to write */
	memcpy(port->write_urb->transfer_buffer + 5, buf, count);

	dbg("%s(): first data byte to send = %#.2x", __func__, *buf);

	/* set up our urb */
	usb_fill_bulk_urb(port->write_urb, serial->dev,
			usb_sndbulkpipe(serial->dev,
			port->bulk_out_endpointAddress),
			port->write_urb->transfer_buffer, count + 5,
			(qt2_write_bulk_callback), port);
	/* send the data out the bulk port */
	result = usb_submit_urb(port->write_urb, GFP_ATOMIC);
	if (result) {
		/* error couldn't submit urb */
		result = 0;	/* return 0 as nothing got written */
		dbg("%s(): failed submitting write urb, error %d",
			__func__, result);
	} else {
		port_extra->tx_pending_bytes += count;
		result = count;	/* return number of bytes written, i.e. count */
		dbg("%s(): submitted write urb, wrote %d bytes, "
			"total pending bytes %d",
			__func__, result, port_extra->tx_pending_bytes);
	}
	return result;
}

/* This is used by the next layer up to know how much space is available
 * in the buffer on the device. It is used on a device closure to avoid
 * calling close() until the buffer is reported to be empty.
 * The returned value must never go down by more than the number of bytes
 * written for correct behaviour further up the driver stack, i.e. if I call
 * it, then write 6 bytes, then call again I should get 6 less, or possibly
 * only 5 less if one was written in the meantime, etc. I should never get 7
 * less (or any bigger number) because I only wrote 6 bytes.
 */
static int qt2_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
		/* parent usb_serial_port pointer */
	struct quatech2_port *port_extra;	/* extra data for this port */
	int room = 0;
	port_extra = qt2_get_port_private(port);

	if (port_extra->close_pending == true) {
		dbg("%s(): port_extra->close_pending == true", __func__);
		return -ENODEV;
	}
	/* Q: how many bytes would a write() call actually succeed in writing
	 * if it happened now?
	 * A: one QT2_FIFO_DEPTH, less the number of bytes waiting to be sent
	 * out of the port, unless this is more than the size of the
	 * write_urb output buffer less the header, which is the maximum
	 * size write we can do.

	 * Most of the implementation of this is done when writes to the device
	 * are started or terminate. When we send a write to the device, we
	 * reduce the free space count by the size of the dispatched write.
	 * When a "transmit empty" message comes back up the USB read stream,
	 * we decrement the count by the number of bytes reported sent, thus
	 * keeping track of the difference between sent and recieved bytes.
	 */

	room = (QT2_FIFO_DEPTH - port_extra->tx_pending_bytes);
	/* space in FIFO */
	if (room > port->bulk_out_size - QT2_TX_HEADER_LENGTH)
		room = port->bulk_out_size - QT2_TX_HEADER_LENGTH;
	/* if more than the URB can hold, then cap to that limit */

	dbg("%s(): port %d: write room is %d", __func__, port->number, room);
	return room;
}

static int qt2_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	/* parent usb_serial_port pointer */
	struct quatech2_port *port_extra;	/* extra data for this port */
	port_extra = qt2_get_port_private(port);

	dbg("%s(): port %d: chars_in_buffer = %d", __func__,
		port->number, port_extra->tx_pending_bytes);
	return port_extra->tx_pending_bytes;
}

/* called when userspace does an ioctl() on the device. Note that
 * TIOCMGET and TIOCMSET are filtered off to their own methods before they get
 * here, so we don't have to handle them.
 */
static int qt2_ioctl(struct tty_struct *tty, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	__u8 mcr_value;	/* Modem Control Register value */
	__u8 msr_value; /* Modem Status Register value */
	unsigned short prev_msr_value; /* Previous value of Modem Status
	 * Register used to implement waiting for a line status change to
	 * occur */
	struct quatech2_port *port_extra;	/* extra data for this port */
	DECLARE_WAITQUEUE(wait, current);
	/* Declare a wait queue named "wait" */

	unsigned int value;
	unsigned int UartNumber;

	if (serial == NULL)
		return -ENODEV;
	UartNumber = tty->index - serial->minor;
	port_extra = qt2_get_port_private(port);

	dbg("%s(): port %d, UartNumber %d, tty =0x%p", __func__,
	    port->number, UartNumber, tty);

	if (cmd == TIOCMBIS || cmd == TIOCMBIC) {
		if (qt2_box_get_register(port->serial, UartNumber,
			QT2_MODEM_CONTROL_REGISTER, &mcr_value) < 0)
			return -ESPIPE;
		if (copy_from_user(&value, (unsigned int *)arg,
			sizeof(value)))
			return -EFAULT;

		switch (cmd) {
		case TIOCMBIS:
			if (value & TIOCM_RTS)
				mcr_value |= QT2_SERIAL_MCR_RTS;
			if (value & TIOCM_DTR)
				mcr_value |= QT2_SERIAL_MCR_DTR;
			if (value & TIOCM_LOOP)
				mcr_value |= QT2_SERIAL_MCR_LOOP;
		break;
		case TIOCMBIC:
			if (value & TIOCM_RTS)
				mcr_value &= ~QT2_SERIAL_MCR_RTS;
			if (value & TIOCM_DTR)
				mcr_value &= ~QT2_SERIAL_MCR_DTR;
			if (value & TIOCM_LOOP)
				mcr_value &= ~QT2_SERIAL_MCR_LOOP;
		break;
		default:
		break;
		}	/* end of local switch on cmd */
		if (qt2_box_set_register(port->serial,  UartNumber,
		    QT2_MODEM_CONTROL_REGISTER, mcr_value) < 0) {
			return -ESPIPE;
		} else {
			port_extra->shadowMCR = mcr_value;
			return 0;
		}
	} else if (cmd == TIOCMIWAIT) {
		dbg("%s() port %d, cmd == TIOCMIWAIT enter",
			__func__, port->number);
		prev_msr_value = port_extra->shadowMSR  & QT2_SERIAL_MSR_MASK;
		while (1) {
			add_wait_queue(&port_extra->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			dbg("%s(): port %d, cmd == TIOCMIWAIT here\n",
				__func__, port->number);
			remove_wait_queue(&port_extra->wait, &wait);
			/* see if a signal woke us up */
			if (signal_pending(current))
				return -ERESTARTSYS;
			msr_value = port_extra->shadowMSR & QT2_SERIAL_MSR_MASK;
			if (msr_value == prev_msr_value)
				return -EIO;  /* no change - error */
			if ((arg & TIOCM_RNG &&
				((prev_msr_value & QT2_SERIAL_MSR_RI) ==
					(msr_value & QT2_SERIAL_MSR_RI))) ||
				(arg & TIOCM_DSR &&
				((prev_msr_value & QT2_SERIAL_MSR_DSR) ==
					(msr_value & QT2_SERIAL_MSR_DSR))) ||
				(arg & TIOCM_CD &&
				((prev_msr_value & QT2_SERIAL_MSR_CD) ==
					(msr_value & QT2_SERIAL_MSR_CD))) ||
				(arg & TIOCM_CTS &&
				((prev_msr_value & QT2_SERIAL_MSR_CTS) ==
					(msr_value & QT2_SERIAL_MSR_CTS)))) {
				return 0;
			}
		} /* end inifinite while */
		/* FIXME: This while loop needs a way to break out if the device
		 * is disconnected while a process is waiting for the MSR to
		 * change, because once it's disconnected, it isn't going to
		 * change state ... */
	} else {
		/* any other ioctls we don't know about come here */
		dbg("%s(): No ioctl for that one. port = %d", __func__,
			port->number);
		return -ENOIOCTLCMD;
	}
}

/* Called when the user wishes to change the port settings using the termios
 * userspace interface */
static void qt2_set_termios(struct tty_struct *tty,
	struct usb_serial_port *port, struct ktermios *old_termios)
{
	struct usb_serial *serial; /* parent serial device */
	int baud, divisor, remainder;
	unsigned char LCR_change_to = 0;
	int status;
	__u16 UartNumber;

	dbg("%s(): port %d", __func__, port->number);

	serial = port->serial;

	UartNumber = port->number;

	if (old_termios && !tty_termios_hw_change(old_termios, tty->termios))
		return;

	switch (tty->termios->c_cflag) {
	case CS5:
		LCR_change_to |= QT2_SERIAL_5_DATA;
		break;
	case CS6:
		LCR_change_to |= QT2_SERIAL_6_DATA;
		break;
	case CS7:
		LCR_change_to |= QT2_SERIAL_7_DATA;
		break;
	default:
	case CS8:
		LCR_change_to |= QT2_SERIAL_8_DATA;
		break;
	}

	/* Parity stuff */
	if (tty->termios->c_cflag & PARENB) {
		if (tty->termios->c_cflag & PARODD)
			LCR_change_to |= QT2_SERIAL_ODD_PARITY;
		else
			LCR_change_to |= QT2_SERIAL_EVEN_PARITY;
	}
	/* Because LCR_change_to is initialised to zero, we don't have to worry
	 * about the case where PARENB is not set or clearing bits, because by
	 * default all of them are cleared, turning parity off.
	 * as we don't support mark/space parity, we should clear the
	 * mark/space parity bit in c_cflag, so the caller can tell we have
	 * ignored the request */
	tty->termios->c_cflag &= ~CMSPAR;

	if (tty->termios->c_cflag & CSTOPB)
		LCR_change_to |= QT2_SERIAL_TWO_STOPB;
	else
		LCR_change_to |= QT2_SERIAL_ONE_STOPB;

	/* Thats the LCR stuff, next we need to work out the divisor as the
	 * LCR and the divisor are set together */
	baud = tty_get_baud_rate(tty);
	if (!baud) {
		/* pick a default, any default... */
		baud = 9600;
	}
	dbg("%s(): got baud = %d", __func__, baud);

	divisor = QT2_MAX_BAUD_RATE / baud;
	remainder = QT2_MAX_BAUD_RATE % baud;
	/* Round to nearest divisor */
	if (((remainder * 2) >= baud) && (baud != 110))
		divisor++;
	dbg("%s(): setting divisor = %d, QT2_MAX_BAUD_RATE = %d , LCR = %#.2x",
	      __func__, divisor, QT2_MAX_BAUD_RATE, LCR_change_to);

	status = qt2_boxsetuart(serial, UartNumber, (unsigned short) divisor,
			    LCR_change_to);
	if (status < 0)	{
		dbg("qt2_boxsetuart() failed");
		return;
	} else {
		/* now encode the baud rate we actually set, which may be
		 * different to the request */
		baud = QT2_MAX_BAUD_RATE / divisor;
		tty_encode_baud_rate(tty, baud, baud);
	}

	/* Now determine flow control */
	if (tty->termios->c_cflag & CRTSCTS) {
		dbg("%s(): Enabling HW flow control port %d", __func__,
		      port->number);
		/* Enable  RTS/CTS flow control */
		status = qt2_boxsethw_flowctl(serial, UartNumber, true);
		if (status < 0) {
			dbg("qt2_boxsethw_flowctl() failed");
			return;
		}
	} else {
		/* Disable RTS/CTS flow control */
		dbg("%s(): disabling HW flow control port %d", __func__,
			port->number);
		status = qt2_boxsethw_flowctl(serial, UartNumber, false);
		if (status < 0)	{
			dbg("qt2_boxsethw_flowctl failed");
			return;
		}
	}
	/* if we are implementing XON/XOFF, set the start and stop character
	 * in the device */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		unsigned char stop_char  = STOP_CHAR(tty);
		unsigned char start_char = START_CHAR(tty);
		status = qt2_boxsetsw_flowctl(serial, UartNumber, stop_char,
				start_char);
		if (status < 0)
			dbg("qt2_boxsetsw_flowctl (enabled) failed");
	} else {
		/* disable SW flow control */
		status = qt2_boxunsetsw_flowctl(serial, UartNumber);
		if (status < 0)
			dbg("qt2_boxunsetsw_flowctl (disabling) failed");
	}
}

static int qt2_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;

	__u8 mcr_value;	/* Modem Control Register value */
	__u8 msr_value;	/* Modem Status Register value */
	unsigned int result = 0;
	int status;
	unsigned int UartNumber;

	if (serial == NULL)
		return -ENODEV;

	dbg("%s(): port %d, tty =0x%p", __func__, port->number, tty);
	UartNumber = tty->index - serial->minor;
	dbg("UartNumber is %d", UartNumber);

	status = qt2_box_get_register(port->serial, UartNumber,
			QT2_MODEM_CONTROL_REGISTER,	&mcr_value);
	if (status >= 0) {
		status = qt2_box_get_register(port->serial,  UartNumber,
				QT2_MODEM_STATUS_REGISTER, &msr_value);
	}
	if (status >= 0) {
		result = ((mcr_value & QT2_SERIAL_MCR_DTR) ? TIOCM_DTR : 0)
				/*DTR set */
			| ((mcr_value & QT2_SERIAL_MCR_RTS)  ? TIOCM_RTS : 0)
				/*RTS set */
			| ((msr_value & QT2_SERIAL_MSR_CTS)  ? TIOCM_CTS : 0)
				/* CTS set */
			| ((msr_value & QT2_SERIAL_MSR_CD)  ? TIOCM_CAR : 0)
				/*Carrier detect set */
			| ((msr_value & QT2_SERIAL_MSR_RI)  ? TIOCM_RI : 0)
				/* Ring indicator set */
			| ((msr_value & QT2_SERIAL_MSR_DSR)  ? TIOCM_DSR : 0);
				/* DSR set */
		return result;
	} else {
		return -ESPIPE;
	}
}

static int qt2_tiocmset(struct tty_struct *tty, struct file *file,
		       unsigned int set, unsigned int clear)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	__u8 mcr_value;	/* Modem Control Register value */
	int status;
	unsigned int UartNumber;

	if (serial == NULL)
		return -ENODEV;

	UartNumber = tty->index - serial->minor;
	dbg("%s(): port %d, UartNumber %d", __func__, port->number, UartNumber);

	status = qt2_box_get_register(port->serial, UartNumber,
			QT2_MODEM_CONTROL_REGISTER, &mcr_value);
	if (status < 0)
		return -ESPIPE;

	/* Turn off RTS, DTR and loopback, then only turn on what was asked
	 * for */
	mcr_value &= ~(QT2_SERIAL_MCR_RTS | QT2_SERIAL_MCR_DTR |
			QT2_SERIAL_MCR_LOOP);
	if (set & TIOCM_RTS)
		mcr_value |= QT2_SERIAL_MCR_RTS;
	if (set & TIOCM_DTR)
		mcr_value |= QT2_SERIAL_MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr_value |= QT2_SERIAL_MCR_LOOP;

	status = qt2_box_set_register(port->serial, UartNumber,
			QT2_MODEM_CONTROL_REGISTER, mcr_value);
	if (status < 0)
		return -ESPIPE;
	else
		return 0;
}

/** qt2_break - Turn BREAK on and off on the UARTs
 */
static void qt2_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data; /* parent port */
	struct usb_serial *serial = port->serial;	/* parent device */
	struct quatech2_port *port_extra;	/* extra data for this port */
	__u16 break_value;
	unsigned int result;

	port_extra = qt2_get_port_private(port);
	if (!serial) {
		dbg("%s(): port %d: no serial object", __func__, port->number);
		return;
	}

	if (break_state == -1)
		break_value = 1;
	else
		break_value = 0;
	dbg("%s(): port %d, break_value %d", __func__, port->number,
		break_value);

	mutex_lock(&port_extra->modelock);
	if (!port_extra->open_count) {
		dbg("%s(): port not open", __func__);
		goto exit;
	}

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				QT2_BREAK_CONTROL, 0x40, break_value,
				port->number, NULL, 0, 300);
exit:
	mutex_unlock(&port_extra->modelock);
	dbg("%s(): exit port %d", __func__, port->number);

}
/**
 * qt2_throttle: - stop reading new data from the port
 */
static void qt2_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	struct quatech2_port *port_extra;	/* extra data for this port */
	dbg("%s(): port %d", __func__, port->number);

	port_extra = qt2_get_port_private(port);
	if (!serial) {
		dbg("%s(): enter port %d no serial object", __func__,
		      port->number);
		return;
	}

	mutex_lock(&port_extra->modelock);	/* lock structure */
	if (!port_extra->open_count) {
		dbg("%s(): port not open", __func__);
		goto exit;
	}
	/* Send command to box to stop receiving stuff. This will stop this
	 * particular UART from filling the endpoint - in the multiport case the
	 * FPGA UART will handle any flow control implmented, but for the single
	 * port it's handed differently and we just quit submitting urbs
	 */
	if (serial->dev->descriptor.idProduct != QUATECH_SSU2_100)
		qt2_boxstoprx(serial, port->number, 1);

	port->throttled = 1;
exit:
	mutex_unlock(&port_extra->modelock);
	dbg("%s(): port %d: setting port->throttled", __func__, port->number);
	return;
}

/**
 * qt2_unthrottle: - start receiving data through the port again after being
 * throttled
 */
static void qt2_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = port->serial;
	struct quatech2_port *port_extra;	/* extra data for this port */
	struct usb_serial_port *port0;	/* first port structure on device */
	struct quatech2_dev *dev_extra;		/* extra data for the device */

	if (!serial) {
		dbg("%s() enter port %d no serial object!", __func__,
			port->number);
		return;
	}
	dbg("%s(): enter port %d", __func__, port->number);
	dev_extra = qt2_get_dev_private(serial);
	port_extra = qt2_get_port_private(port);
	port0 = serial->port[0]; /* get the first port's device structure */

	mutex_lock(&port_extra->modelock);
	if (!port_extra->open_count) {
		dbg("%s(): port %d not open", __func__, port->number);
		goto exit;
	}

	if (port->throttled != 0) {
		dbg("%s(): port %d: unsetting port->throttled", __func__,
		    port->number);
		port->throttled = 0;
		/* Send command to box to start receiving stuff */
		if (serial->dev->descriptor.idProduct != QUATECH_SSU2_100) {
			qt2_boxstoprx(serial,  port->number, 0);
		} else if (dev_extra->ReadBulkStopped == true) {
			usb_fill_bulk_urb(port0->read_urb, serial->dev,
				usb_rcvbulkpipe(serial->dev,
				port0->bulk_in_endpointAddress),
				port0->bulk_in_buffer,
				port0->bulk_in_size,
				qt2_read_bulk_callback,
				serial);
		}
	}
exit:
	mutex_unlock(&port_extra->modelock);
	dbg("%s(): exit port %d", __func__, port->number);
	return;
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
 * qt2_boxsetQMCR Issue a QT2_GET_SET_QMCR vendor-spcific request on the
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
				QT2_GET_SET_QMCR, 0x40, PortSettings,
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
				QT2_GET_SET_UART, 0x40, divisor, UartNumandLCR,
				NULL, 0, 300);
	return result;
}

/** @brief Callback for asynchronous submission of read URBs on bulk in
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
	dbg("%s(): callback running, active port is %d", __func__,
		active->number);

	if (urb->status) {
		/* read didn't go well */
		dev_extra->ReadBulkStopped = true;
		dbg("%s(): nonzero bulk read status received: %d",
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
						FOURTHCHAR, FIFTHCHAR);
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
					if (active_extra->open_count > 0)
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
		dbg("%s() successfully resubmitted read urb", __func__);
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
	dbg("%s() completed", __func__);
	return;
}

/** @brief Callback for asynchronous submission of write URBs on bulk in
 * endpoints
 *
 * Registered in qt2_write(), used to deal with outgoing data
 * to the box.
 */
static void qt2_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	dbg("%s(): port %d", __func__, port->number);
	if (!serial) {
		dbg("%s(): bad serial pointer, exiting", __func__);
		return;
	}
	if (urb->status) {
		dbg("%s(): nonzero write bulk status received: %d",
			__func__, urb->status);
		return;
	}
	/* FIXME What is supposed to be going on here?
	 * does this actually do anything useful, and should it?
	 */
	/*port_softint((void *) serial); commented in vendor driver */
	schedule_work(&port->work);
	dbg("%s(): port %d exit", __func__, port->number);
	return;
}

static void qt2_process_line_status(struct usb_serial_port *port,
	unsigned char LineStatus)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->shadowLSR = LineStatus & (QT2_SERIAL_LSR_OE |
		QT2_SERIAL_LSR_PE | QT2_SERIAL_LSR_FE | QT2_SERIAL_LSR_BI);
}
static void qt2_process_modem_status(struct usb_serial_port *port,
	unsigned char ModemStatus)
{
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);
	port_extra->shadowMSR = ModemStatus;
	wake_up_interruptible(&port_extra->wait);
	/* this wakes up the otherwise indefinitely waiting code for
	 * the TIOCMIWAIT ioctl, so that it can notice that
	 * port_extra->shadowMSR has changed and the ioctl needs to return.
	 */
}

static void qt2_process_xmit_empty(struct usb_serial_port *port,
	unsigned char fourth_char, unsigned char fifth_char)
{
	int byte_count;
	/* obtain the private structure for the port */
	struct quatech2_port *port_extra = qt2_get_port_private(port);

	byte_count = (int)(fifth_char * 16);
	byte_count +=  (int)fourth_char;
	/* byte_count indicates how many bytes the device has written out. This
	 * message appears to occur regularly, and is used in the vendor driver
	 * to keep track of the fill state of the port transmit buffer */
	port_extra->tx_pending_bytes -= byte_count;
	/* reduce the stored data queue length by the known number of bytes
	 * sent */
	dbg("port %d: %d bytes reported sent, %d still pending", port->number,
			byte_count, port_extra->tx_pending_bytes);

	/*port_extra->xmit_fifo_room_bytes = FIFO_DEPTH; ???*/
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

/** @brief Retreive the value of a register from the device
 *
 * Issues a GET_REGISTER vendor-spcific request over the USB control
 * pipe to obtain a value back from a specific register on a specific
 * UART
 * @param serial Serial device handle to access the device through
 * @param uart_number Which UART the value is wanted from
 * @param register_num Which register to read the value from
 * @param pValue Pointer to somewhere to put the retrieved value
 */
static int qt2_box_get_register(struct usb_serial *serial,
		unsigned char uart_number, unsigned short register_num,
		__u8 *pValue)
{
	int result;
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			QT2_GET_SET_REGISTER, 0xC0, register_num,
			uart_number, (void *)pValue, sizeof(*pValue), 300);
	return result;
}

/** qt2_box_set_register
 * Issue a SET_REGISTER vendor-specific request on the default control pipe
 */
static int qt2_box_set_register(struct usb_serial *serial,
		unsigned short Uart_Number, unsigned short Register_Num,
		unsigned short Value)
{
	int result;
	unsigned short reg_and_byte;

	reg_and_byte = Value;
	reg_and_byte = reg_and_byte << 8;
	reg_and_byte = reg_and_byte + Register_Num;

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			QT2_GET_SET_REGISTER, 0x40, reg_and_byte,
			Uart_Number, NULL, 0, 300);
	return result;
}


/** @brief Request the Tx or Rx buffers on the USB side be flushed
 *
 * Tx flush: When all the currently buffered data has been sent, send an escape
 * sequence back up the data stream to us
 * Rx flush: add a flag in the data stream now so we know when it's made it's
 * way up to us.
 */
static int qt2_box_flush(struct usb_serial *serial,  unsigned char uart_number,
		    unsigned short rcv_or_xmit)
{
	int result;
	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
		QT2_FLUSH_DEVICE, 0x40, rcv_or_xmit, uart_number, NULL, 0,
		300);
	return result;
}

/** qt2_boxsetuart - Issue a SET_UART vendor-spcific request on the default
 * control pipe. If successful sets baud rate divisor and LCR value.
 */
static int qt2_boxsetuart(struct usb_serial *serial, unsigned short Uart_Number,
		unsigned short default_divisor, unsigned char default_LCR)
{
	unsigned short UartNumandLCR;

	UartNumandLCR = (default_LCR << 8) + Uart_Number;

	return usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			QT2_GET_SET_UART, 0x40, default_divisor, UartNumandLCR,
			NULL, 0, 300);
}
/** qt2_boxsethw_flowctl - Turn hardware (RTS/CTS) flow control on and off for
 * a hardware UART.
 */
static int qt2_boxsethw_flowctl(struct usb_serial *serial,
		unsigned int UartNumber, bool bSet)
{
	__u8 MCR_Value = 0;
	__u8 MSR_Value = 0;
	__u16 MOUT_Value = 0;

	if (bSet == true) {
		MCR_Value =  QT2_SERIAL_MCR_RTS;
		/* flow control, box will clear RTS line to prevent remote
		 * device from transmitting more chars */
	} else {
		/* no flow control to remote device */
		MCR_Value =  0;
	}
	MOUT_Value = MCR_Value << 8;

	if (bSet == true) {
		MSR_Value = QT2_SERIAL_MSR_CTS;
		/* flow control on, box will inhibit tx data if CTS line is
		 * asserted */
	} else {
		/* Box will not inhibit tx data due to CTS line */
		MSR_Value = 0;
	}
	MOUT_Value |= MSR_Value;
	return usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			QT2_HW_FLOW_CONTROL_MASK, 0x40, MOUT_Value, UartNumber,
			NULL, 0, 300);
}

/** qt2_boxsetsw_flowctl - Turn software (XON/XOFF) flow control on for
 * a hardware UART, and set the XON and XOFF characters.
 */
static int qt2_boxsetsw_flowctl(struct usb_serial *serial, __u16 UartNumber,
			unsigned char stop_char,  unsigned char start_char)
{
	__u16 nSWflowout;

	nSWflowout = start_char << 8;
	nSWflowout = (unsigned short)stop_char;
	return usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			QT2_SW_FLOW_CONTROL_MASK, 0x40, nSWflowout, UartNumber,
			NULL, 0, 300);
}

/** qt2_boxunsetsw_flowctl - Turn software (XON/XOFF) flow control off for
 * a hardware UART.
 */
static int qt2_boxunsetsw_flowctl(struct usb_serial *serial, __u16 UartNumber)
{
	return usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			QT2_SW_FLOW_CONTROL_DISABLE, 0x40, 0, UartNumber, NULL,
			0, 300);
}

/**
 * qt2_boxstoprx - Start and stop reception of data by the FPGA UART in
 * response to requests from the tty layer
 * @serial: pointer to the usb_serial structure for the parent device
 * @uart_number: which UART on the device we are addressing
 * @stop: Whether to start or stop data reception. Set to 1 to stop data being
 * received, and to 0 to start it being received.
 */
static int qt2_boxstoprx(struct usb_serial *serial, unsigned short uart_number,
		unsigned short stop)
{
	return usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
		QT2_STOP_RECEIVE, 0x40, stop, uart_number, NULL, 0, 300);
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
	.close = qt2_close,
	.write = qt2_write,
	.write_room = qt2_write_room,
	.chars_in_buffer = qt2_chars_in_buffer,
	.throttle = qt2_throttle,
	.unthrottle = qt2_unthrottle,
	.calc_num_ports = qt2_calc_num_ports,
	.ioctl = qt2_ioctl,
	.set_termios = qt2_set_termios,
	.break_ctl = qt2_break,
	.tiocmget = qt2_tiocmget,
	.tiocmset = qt2_tiocmset,
	.attach = qt2_attach,
	.release = qt2_release,
	.read_bulk_callback = qt2_read_bulk_callback,
	.write_bulk_callback = qt2_write_bulk_callback,
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
