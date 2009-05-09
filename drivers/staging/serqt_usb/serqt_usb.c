/*
 * This code was developed for the Quatech USB line for linux, it used
 * much of the code developed by Greg Kroah-Hartman for USB serial devices
 *
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/uaccess.h>

/* Use our own dbg macro */
/* #define DEBUG_ON */
/* #undef dbg */
#ifdef DEBUG_ON
#define  mydbg(const...)    printk(const)
#else
#define  mydbg(const...)
#endif

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

#define SERIAL_TTY_MAJOR	        0	/* Nice legal number now */
#define SERIAL_TTY_MINORS	        255	/* loads of devices :) */
#define MAX_NUM_PORTS		        8	/* The maximum number of ports one device can grab at once */
#define PREFUFF_LEVEL_CONSERVATIVE  128
#define ATC_DISABLED                0x00

#define RR_BITS             0x03	/* for clearing clock bits */
#define DUPMODE_BITS        0xc0

#define RS232_MODE          0x00
#define RTSCTS_TO_CONNECTOR 0x40
#define CLKS_X4             0x02

#define LOOPMODE_BITS       0x41	/* LOOP1 = b6, LOOP0 = b0 (PORT B) */
#define ALL_LOOPBACK        0x01
#define MODEM_CTRL          0x40

#define THISCHAR                    data[i]
#define NEXTCHAR                    data[i + 1]
#define THIRDCHAR                  data[i + 2]
#define FOURTHCHAR                  data[i + 3]

/*
 * Useful defintions for port A, Port B and Port C
 */
#define FULLPWRBIT          0x00000080
#define NEXT_BOARD_POWER_BIT        0x00000004

#define SERIAL_LSR_OE       0x02
#define SERIAL_LSR_PE       0x04
#define SERIAL_LSR_FE       0x08
#define SERIAL_LSR_BI       0x10

#define SERIAL_LSR_TEMT     0x40

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

#define  MAX_BAUD_RATE              460800
#define  MAX_BAUD_REMAINDER         4608

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

#define SERIALQT_PCI_IOC_MAGIC 'k'
#define SERIALQT_WRITE_QOPR _IOW(SERIALQT_PCI_IOC_MAGIC, 0, int)
#define SERIALQT_WRITE_QMCR _IOW(SERIALQT_PCI_IOC_MAGIC, 1, int)
#define SERIALQT_GET_NUMOF_UNITS _IOR(SERIALQT_PCI_IOC_MAGIC, 2, void *)
#define SERIALQT_GET_THIS_UNIT _IOR(SERIALQT_PCI_IOC_MAGIC, 3, void *)
#define SERIALQT_READ_QOPR _IOR(SERIALQT_PCI_IOC_MAGIC, 4, int)
#define SERIALQT_READ_QMCR _IOR(SERIALQT_PCI_IOC_MAGIC, 5, int)
#define SERIALQT_IS422_EXTENDED _IOR(SERIALQT_PCI_IOC_MAGIC, 6, int)	/* returns successful if 422 extended */

#define USBD_TRANSFER_DIRECTION_IN    0xc0
#define USBD_TRANSFER_DIRECTION_OUT   0x40

#define ATC_DISABLED                0x00
#define ATC_RTS_ENABLED                 0x02
#define ATC_DTR_ENABLED                 0x01

#define RR_BITS             0x03	/* for clearing clock bits */
#define DUPMODE_BITS        0xc0

#define FULL_DUPLEX         0x00
#define HALF_DUPLEX_RTS     0x40
#define HALF_DUPLEX_DTR     0x80

#define QMCR_FULL_DUPLEX  0x00
#define QMCR_HALF_DUPLEX_RTS 0x02
#define QMCR_HALF_DUPLEX_DTR 0x01
#define QMCR_HALF_DUPLEX_MASK 0x03
#define QMCR_CONNECTOR_MASK 0x1C

#define QMCR_RX_EN_MASK 0x20

#define QMCR_ALL_LOOPBACK    0x10
#define QMCR_MODEM_CONTROL   0X00

#define SERIALQT_IOC_MAXNR 6

struct usb_serial_port {
	struct usb_serial *serial;	/* pointer back to the owner of this port */
	struct tty_struct *tty;	/* the coresponding tty for this port */
	unsigned char number;
	char active;		/* someone has this device open */

	unsigned char *interrupt_in_buffer;
	struct urb *interrupt_in_urb;
	__u8 interrupt_in_endpointAddress;

	unsigned char *bulk_in_buffer;
	unsigned char *xfer_to_tty_buffer;
	struct urb *read_urb;
	__u8 bulk_in_endpointAddress;

	unsigned char *bulk_out_buffer;
	int bulk_out_size;
	struct urb *write_urb;
	__u8 bulk_out_endpointAddress;

	wait_queue_head_t write_wait;
	wait_queue_head_t wait;
	struct work_struct work;

	int open_count;		/* number of times this port has been opened */
	struct semaphore sem;	/* locks this structure */

	__u8 shadowLCR;		/* last LCR value received */
	__u8 shadowMCR;		/* last MCR value received */
	__u8 shadowMSR;		/* last MSR value received */
	__u8 shadowLSR;		/* last LSR value received */
	int RxHolding;
	char closePending;
	int ReadBulkStopped;

	void *private;		/* data private to the specific port */
};

struct identity {
	int index;
	int n_identity;
};

struct usb_serial {
	struct usb_device *dev;
	struct usb_interface *interface;	/* the interface for this device */
	struct tty_driver *tty_driver;	/* the tty_driver for this device */
	unsigned char minor;	/* the starting minor number for this device */
	unsigned char num_ports;	/* the number of ports this device has */
	char num_interrupt_in;	/* number of interrupt in endpoints we have */
	char num_bulk_in;	/* number of bulk in endpoints we have */
	char num_bulk_out;	/* number of bulk out endpoints we have */
	unsigned char num_OpenCount;	/* the number of ports this device has */

	__u16 vendor;		/* vendor id of this device */
	__u16 product;		/* product id of this device */
	struct usb_serial_port port[MAX_NUM_PORTS];

	void *private;		/* data private to the specific driver */
};

static inline int port_paranoia_check(struct usb_serial_port *port,
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
	if (!port->tty) {
		dbg("%s - port->tty == NULL\n", function);
		return -1;
	}

	return 0;
}

/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int serial_paranoia_check(struct usb_serial *serial,
					const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL\n", function);
		return -1;
	}

	return 0;
}

static inline struct usb_serial *get_usb_serial(struct usb_serial_port *port,
						const char *function)
{
	/* if no port was specified, or it fails a paranoia check */
	if (!port ||
	    port_paranoia_check(port, function) ||
	    serial_paranoia_check(port->serial, function)) {
		/* then say that we dont have a valid usb_serial thing, which will
		 * end up genrating -ENODEV return values */
		return NULL;
	}

	return port->serial;
}

struct qt_get_device_data {
	__u8 porta;
	__u8 portb;
	__u8 portc;
};

struct qt_open_channel_data {
	__u8 line_status;
	__u8 modem_status;
};

static void ProcessLineStatus(struct usb_serial_port *port,
			      unsigned char line_status);
static void ProcessModemStatus(struct usb_serial_port *port,
			       unsigned char modem_status);
static void ProcessRxChar(struct usb_serial_port *port, unsigned char Data);
static struct usb_serial *get_free_serial(int num_ports, int *minor);

static int serqt_probe(struct usb_interface *interface,
		       const struct usb_device_id *id);

static void serqt_usb_disconnect(struct usb_interface *interface);
static int box_set_device(struct usb_serial *serial,
			  struct qt_get_device_data *pDeviceData);
static int box_get_device(struct usb_serial *serial,
			  struct qt_get_device_data *pDeviceData);
static int serial_open(struct tty_struct *tty, struct file *filp);
static void serial_close(struct tty_struct *tty, struct file *filp);
static int serial_write_room(struct tty_struct *tty);
static int serial_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg);
static void serial_set_termios(struct tty_struct *tty, struct ktermios *old);
static int serial_write(struct tty_struct *tty, const unsigned char *buf,
			int count);

static void serial_throttle(struct tty_struct *tty);
static void serial_unthrottle(struct tty_struct *tty);
static int serial_break(struct tty_struct *tty, int break_state);
static int serial_chars_in_buffer(struct tty_struct *tty);

static int qt_open(struct tty_struct *tty, struct usb_serial_port *port,
				struct file *filp);
static int BoxSetPrebufferLevel(struct usb_serial *serial);

static int BoxSetATC(struct usb_serial *serial, __u16 n_Mode);
static int BoxSetUart(struct usb_serial *serial, unsigned short Uart_Number,
		      unsigned short default_divisor,
		      unsigned char default_LCR);

static int BoxOPenCloseChannel(struct usb_serial *serial, __u16 Uart_Number,
			       __u16 OpenClose,
			       struct qt_open_channel_data *pDeviceData);
static void qt_close(struct tty_struct *tty, struct usb_serial_port *port,
					struct file *filp);
static int BoxGetRegister(struct usb_serial *serial, unsigned short Uart_Number,
			  unsigned short Register_Num, __u8 *pValue);
static int BoxSetRegister(struct usb_serial *serial, unsigned short Uart_Number,
			  unsigned short Register_Num, unsigned short Value);
static void qt_write_bulk_callback(struct urb *urb);
static int qt_write(struct tty_struct *tty, struct usb_serial_port *port,
		    const unsigned char *buf, int count);
static void port_softint(struct work_struct *work);
static int qt_write_room(struct usb_serial_port *port);
static int qt_chars_in_buffer(struct usb_serial_port *port);
static int qt_ioctl(struct tty_struct *tty, struct usb_serial_port *port,
		struct file *file, unsigned int cmd, unsigned long arg);
static void qt_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   struct ktermios *old_termios);
static int BoxSetHW_FlowCtrl(struct usb_serial *serial, unsigned int index,
			     int bSet);
static int BoxDisable_SW_FlowCtrl(struct usb_serial *serial, __u16 index);
static int EmulateWriteQMCR_Reg(int index, unsigned uc_value);
static int EmulateReadQMCR_Reg(int index, unsigned *uc_value);
static struct usb_serial *find_the_box(unsigned int index);
static int ioctl_serial_usb(struct inode *innod, struct file *filp, unsigned int cmd,
		     unsigned long arg);

static int BoxSetSW_FlowCtrl(struct usb_serial *serial, __u16 Uart,
			     unsigned char stop_char, unsigned char start_char);
static void qt_read_bulk_callback(struct urb *urb);

static void port_sofrint(void *private);

static void return_serial(struct usb_serial *serial);

static int serial_tiocmset(struct tty_struct *tty, struct file *file,
			   unsigned int set, unsigned int clear);
static int serial_tiocmget(struct tty_struct *tty, struct file *file);

static int qt_tiocmset(struct tty_struct *tty, struct usb_serial_port *port,
		       struct file *file, unsigned int value);

static int qt_tiocmget(struct tty_struct *tty, struct usb_serial_port *port,
							struct file *file);

/* Version Information */
#define DRIVER_VERSION "v2.14"
#define DRIVER_AUTHOR "Tim Gobeli, Quatech, Inc"
#define DRIVER_DESC "Quatech USB to Serial Driver"

#define	USB_VENDOR_ID_QUATECH			0x061d	/* Quatech VID */
#define DEVICE_ID_QUATECH_RS232_SINGLE_PORT	0xC020	/* SSU100 */
#define DEVICE_ID_QUATECH_RS422_SINGLE_PORT	0xC030	/* SSU200 */
#define DEVICE_ID_QUATECH_RS232_DUAL_PORT	0xC040	/* DSU100 */
#define DEVICE_ID_QUATECH_RS422_DUAL_PORT	0xC050	/* DSU200 */
#define DEVICE_ID_QUATECH_RS232_FOUR_PORT	0xC060	/* QSU100 */
#define DEVICE_ID_QUATECH_RS422_FOUR_PORT	0xC070	/* QSU200 */
#define DEVICE_ID_QUATECH_RS232_EIGHT_PORT_A	0xC080	/* ESU100A */
#define DEVICE_ID_QUATECH_RS232_EIGHT_PORT_B	0xC081	/* ESU100B */
#define DEVICE_ID_QUATECH_RS422_EIGHT_PORT_A	0xC0A0	/* ESU200A */
#define DEVICE_ID_QUATECH_RS422_EIGHT_PORT_B	0xC0A1	/* ESU200B */
#define DEVICE_ID_QUATECH_RS232_16_PORT_A	0xC090	/* HSU100A */
#define DEVICE_ID_QUATECH_RS232_16_PORT_B	0xC091	/* HSU100B */
#define DEVICE_ID_QUATECH_RS232_16_PORT_C	0xC092	/* HSU100C */
#define DEVICE_ID_QUATECH_RS232_16_PORT_D	0xC093	/* HSU100D */
#define DEVICE_ID_QUATECH_RS422_16_PORT_A	0xC0B0	/* HSU200A */
#define DEVICE_ID_QUATECH_RS422_16_PORT_B	0xC0B1	/* HSU200B */
#define DEVICE_ID_QUATECH_RS422_16_PORT_C	0xC0B2	/* HSU200C */
#define DEVICE_ID_QUATECH_RS422_16_PORT_D	0xC0B3	/* HSU200D */

/* table of Quatech devices  */
static struct usb_device_id serqt_table[] = {
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_SINGLE_PORT)},
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_SINGLE_PORT)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_DUAL_PORT)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_DUAL_PORT)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_FOUR_PORT)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_FOUR_PORT)},
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_EIGHT_PORT_A)},
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_EIGHT_PORT_B)},
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_EIGHT_PORT_A)},
	{USB_DEVICE
	 (USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_EIGHT_PORT_B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_16_PORT_A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_16_PORT_B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_16_PORT_C)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS232_16_PORT_D)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_16_PORT_A)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_16_PORT_B)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_16_PORT_C)},
	{USB_DEVICE(USB_VENDOR_ID_QUATECH, DEVICE_ID_QUATECH_RS422_16_PORT_D)},
	{}			/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, serqt_table);

static int major_number;
static struct usb_serial *serial_table[SERIAL_TTY_MINORS];	/* initially all NULL */

/* table of Quatech 422devices  */
static unsigned int serqt_422_table[] = {
	DEVICE_ID_QUATECH_RS422_SINGLE_PORT,
	DEVICE_ID_QUATECH_RS422_DUAL_PORT,
	DEVICE_ID_QUATECH_RS422_FOUR_PORT,
	DEVICE_ID_QUATECH_RS422_EIGHT_PORT_A,
	DEVICE_ID_QUATECH_RS422_EIGHT_PORT_B,
	DEVICE_ID_QUATECH_RS422_16_PORT_A,
	DEVICE_ID_QUATECH_RS422_16_PORT_B,
	DEVICE_ID_QUATECH_RS422_16_PORT_C,
	DEVICE_ID_QUATECH_RS422_16_PORT_D,
	0			/* terminate with zero */
};

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver serqt_usb_driver = {
	.name = "quatech-usb-serial",
	.probe = serqt_probe,
	.disconnect = serqt_usb_disconnect,
	.id_table = serqt_table,
};

static struct ktermios *serial_termios[SERIAL_TTY_MINORS];
static struct ktermios *serial_termios_locked[SERIAL_TTY_MINORS];

static const struct tty_operations serial_ops = {
	.open = serial_open,
	.close = serial_close,
	.write = serial_write,
	.write_room = serial_write_room,
	.ioctl = serial_ioctl,
	.set_termios = serial_set_termios,
	.throttle = serial_throttle,
	.unthrottle = serial_unthrottle,
	.break_ctl = serial_break,
	.chars_in_buffer = serial_chars_in_buffer,
	.tiocmset = serial_tiocmset,
	.tiocmget = serial_tiocmget,
};

static struct tty_driver serial_tty_driver = {
	.magic = TTY_DRIVER_MAGIC,
	.driver_name = "Quatech usb-serial",
	.name = "ttyQT_USB",
	.major = SERIAL_TTY_MAJOR,
	.minor_start = 0,
	.num = SERIAL_TTY_MINORS,
	.type = TTY_DRIVER_TYPE_SERIAL,
	.subtype = SERIAL_TYPE_NORMAL,
	.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV,

	.termios = serial_termios,
	.termios_locked = serial_termios_locked,
	.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL,

	.init_termios.c_iflag = ICRNL | IXON,
	.init_termios.c_oflag = OPOST,

	.init_termios.c_lflag =
	    ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
};

/* fops for parent device */
static const struct file_operations serialqt_usb_fops = {
	.ioctl = ioctl_serial_usb,
};

 /**
 *	  serqt_probe
 *
 *	Called by the usb core when a new device is connected that it thinks
 *	this driver might be interested in.
 *
 */
static int serqt_probe(struct usb_interface *interface,
		       const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct usb_serial *serial = NULL;
	struct usb_serial_port *port;
	struct usb_endpoint_descriptor *endpoint;
	struct usb_endpoint_descriptor *interrupt_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_in_endpoint[MAX_NUM_PORTS];
	struct usb_endpoint_descriptor *bulk_out_endpoint[MAX_NUM_PORTS];
	int minor;
	int buffer_size;
	int i;
	struct usb_host_interface *iface_desc;
	int num_interrupt_in = 0;
	int num_bulk_in = 0;
	int num_bulk_out = 0;
	int num_ports;
	struct qt_get_device_data DeviceData;
	int status;

	mydbg("In %s\n", __func__);

	/* let's find the endpoints needed */
	/* check out the endpoints */
	iface_desc = interface->cur_altsetting;;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk in endpoint */
			mydbg("found bulk in");
			bulk_in_endpoint[num_bulk_in] = endpoint;
			++num_bulk_in;
		}

		if (((endpoint->bEndpointAddress & 0x80) == 0x00) &&
		    ((endpoint->bmAttributes & 3) == 0x02)) {
			/* we found a bulk out endpoint */
			mydbg("found bulk out\n");
			bulk_out_endpoint[num_bulk_out] = endpoint;
			++num_bulk_out;
		}

		if ((endpoint->bEndpointAddress & 0x80) &&
		    ((endpoint->bmAttributes & 3) == 0x03)) {
			/* we found a interrupt in endpoint */
			mydbg("found interrupt in\n");
			interrupt_in_endpoint[num_interrupt_in] = endpoint;
			++num_interrupt_in;
		}
	}

	/* found all that we need */
	dev_info(&interface->dev, "Quatech converter detected\n");
	num_ports = num_bulk_out;
	if (num_ports == 0) {
		err("Quatech device with no bulk out, not allowed.");
		return -ENODEV;

	}

	serial = get_free_serial(num_ports, &minor);
	if (serial == NULL) {
		err("No more free serial devices");
		return -ENODEV;
	}

	serial->dev = dev;
	serial->interface = interface;
	serial->minor = minor;
	serial->num_ports = num_ports;
	serial->num_bulk_in = num_bulk_in;
	serial->num_bulk_out = num_bulk_out;
	serial->num_interrupt_in = num_interrupt_in;
	serial->vendor = dev->descriptor.idVendor;
	serial->product = dev->descriptor.idProduct;

	/* set up the endpoint information */
	for (i = 0; i < num_bulk_in; ++i) {
		endpoint = bulk_in_endpoint[i];
		port = &serial->port[i];
		port->read_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port->read_urb) {
			err("No free urbs available");
			goto probe_error;
		}
		buffer_size = endpoint->wMaxPacketSize;
		port->bulk_in_endpointAddress = endpoint->bEndpointAddress;
		port->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
		port->xfer_to_tty_buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!port->bulk_in_buffer) {
			err("Couldn't allocate bulk_in_buffer");
			goto probe_error;
		}
		usb_fill_bulk_urb(port->read_urb, dev,
				  usb_rcvbulkpipe(dev,
						  endpoint->bEndpointAddress),
				  port->bulk_in_buffer, buffer_size,
				  qt_read_bulk_callback, port);
	}

	for (i = 0; i < num_bulk_out; ++i) {
		endpoint = bulk_out_endpoint[i];
		port = &serial->port[i];
		port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!port->write_urb) {
			err("No free urbs available");
			goto probe_error;
		}
		buffer_size = endpoint->wMaxPacketSize;
		port->bulk_out_size = buffer_size;
		port->bulk_out_endpointAddress = endpoint->bEndpointAddress;
		port->bulk_out_buffer = kmalloc(buffer_size, GFP_KERNEL);
		if (!port->bulk_out_buffer) {
			err("Couldn't allocate bulk_out_buffer");
			goto probe_error;
		}
		usb_fill_bulk_urb(port->write_urb, dev,
				  usb_sndbulkpipe(dev,
						  endpoint->bEndpointAddress),
				  port->bulk_out_buffer, buffer_size,
				  qt_write_bulk_callback, port);

	}

	/* For us numb of bulkin  or out = number of ports */
	mydbg("%s - setting up %d port structures for this device\n",
	      __func__, num_bulk_in);
	for (i = 0; i < num_bulk_in; ++i) {
		port = &serial->port[i];
		port->number = i + serial->minor;
		port->serial = serial;

		INIT_WORK(&port->work, port_softint);

		init_MUTEX(&port->sem);

	}
	status = box_get_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_get_device failed");
		goto probe_error;
	}

	mydbg(__FILE__ "DeviceData.portb = 0x%x", DeviceData.portb);

	DeviceData.portb &= ~FULLPWRBIT;
	mydbg(__FILE__ "Changing DeviceData.portb to 0x%x", DeviceData.portb);

	status = box_set_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_set_device failed\n");
		goto probe_error;
	}

	/* initialize the devfs nodes for this device and let the user know what ports we are bound to */
	for (i = 0; i < serial->num_ports; ++i) {
		dev_info(&interface->dev,
			 "Converter now attached to ttyUSB%d (or usb/tts/%d for devfs)",
			 serial->port[i].number, serial->port[i].number);
	}

	/* usb_serial_console_init (debug, minor); */

	/***********TAG add start next board here ****/
	status = box_get_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_get_device failed");
		goto probe_error;
	}
	/*
	 * and before we power up lets initialiaze parnent device stuff here before
	 * we set thmem via any other method such as the property pages
	 */
	switch (serial->product) {
	case DEVICE_ID_QUATECH_RS232_SINGLE_PORT:
	case DEVICE_ID_QUATECH_RS232_DUAL_PORT:
	case DEVICE_ID_QUATECH_RS232_FOUR_PORT:
	case DEVICE_ID_QUATECH_RS232_EIGHT_PORT_A:
	case DEVICE_ID_QUATECH_RS232_EIGHT_PORT_B:
	case DEVICE_ID_QUATECH_RS232_16_PORT_A:
	case DEVICE_ID_QUATECH_RS232_16_PORT_B:
	case DEVICE_ID_QUATECH_RS232_16_PORT_C:
	case DEVICE_ID_QUATECH_RS232_16_PORT_D:
		DeviceData.porta &= ~(RR_BITS | DUPMODE_BITS);
		DeviceData.porta |= CLKS_X4;
		DeviceData.portb &= ~(LOOPMODE_BITS);
		DeviceData.portb |= RS232_MODE;
		break;

	case DEVICE_ID_QUATECH_RS422_SINGLE_PORT:
	case DEVICE_ID_QUATECH_RS422_DUAL_PORT:
	case DEVICE_ID_QUATECH_RS422_FOUR_PORT:
	case DEVICE_ID_QUATECH_RS422_EIGHT_PORT_A:
	case DEVICE_ID_QUATECH_RS422_EIGHT_PORT_B:
	case DEVICE_ID_QUATECH_RS422_16_PORT_A:
	case DEVICE_ID_QUATECH_RS422_16_PORT_B:
	case DEVICE_ID_QUATECH_RS422_16_PORT_C:
	case DEVICE_ID_QUATECH_RS422_16_PORT_D:
		DeviceData.porta &= ~(RR_BITS | DUPMODE_BITS);
		DeviceData.porta |= CLKS_X4;
		DeviceData.portb &= ~(LOOPMODE_BITS);
		DeviceData.portb |= ALL_LOOPBACK;
		break;
	default:
		DeviceData.porta &= ~(RR_BITS | DUPMODE_BITS);
		DeviceData.porta |= CLKS_X4;
		DeviceData.portb &= ~(LOOPMODE_BITS);
		DeviceData.portb |= RS232_MODE;
		break;

	}
	status = BoxSetPrebufferLevel(serial);	/* sets to default vaue */
	if (status < 0) {
		mydbg(__FILE__ "BoxSetPrebufferLevel failed\n");
		goto probe_error;
	}

	status = BoxSetATC(serial, ATC_DISABLED);
	if (status < 0) {
		mydbg(__FILE__ "BoxSetATC failed\n");
		goto probe_error;
	}
	/**********************************************************/
	mydbg(__FILE__ "DeviceData.portb = 0x%x", DeviceData.portb);

	DeviceData.portb |= NEXT_BOARD_POWER_BIT;
	mydbg(__FILE__ "Changing DeviceData.portb to 0x%x", DeviceData.portb);

	status = box_set_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_set_device failed\n");
		goto probe_error;
	}

	mydbg("Exit Success %s\n", __func__);

	usb_set_intfdata(interface, serial);
	return 0;

probe_error:

	for (i = 0; i < num_bulk_in; ++i) {
		port = &serial->port[i];
		usb_free_urb(port->read_urb);
		kfree(port->bulk_in_buffer);
	}
	for (i = 0; i < num_bulk_out; ++i) {
		port = &serial->port[i];
		usb_free_urb(port->write_urb);
		kfree(port->bulk_out_buffer);
		kfree(port->xfer_to_tty_buffer);
	}
	for (i = 0; i < num_interrupt_in; ++i) {
		port = &serial->port[i];
		usb_free_urb(port->interrupt_in_urb);
		kfree(port->interrupt_in_buffer);
	}

	/* return the minor range that this device had */
	return_serial(serial);
	mydbg("Exit fail %s\n", __func__);

	/* free up any memory that we allocated */
	kfree(serial);
	return -EIO;
}

/*
 * returns the serial_table array pointers that are taken
 * up in consecutive positions for each port to a common usb_serial structure
 * back to NULL
 */
static void return_serial(struct usb_serial *serial)
{
	int i;

	mydbg("%s\n", __func__);

	if (serial == NULL)
		return;

	for (i = 0; i < serial->num_ports; ++i)
		serial_table[serial->minor + i] = NULL;

	return;
}

/*
 * Finds the first locatio int the serial_table array where it can fit
 * num_ports number of consecutive points to a common usb_serial
 * structure,allocates a stucture points to it in all the structures, and
 * returns the index to the first location in the array in the "minor"
 * variable.
 */
static struct usb_serial *get_free_serial(int num_ports, int *minor)
{
	struct usb_serial *serial = NULL;
	int i, j;
	int good_spot;

	mydbg("%s %d\n", __func__, num_ports);

	*minor = 0;
	for (i = 0; i < SERIAL_TTY_MINORS; ++i) {
		if (serial_table[i])
			continue;

		good_spot = 1;
		/*
		 * find a spot in the array where you can fit consecutive
		 * positions to put the pointers to the usb_serail allocated
		 * structure for all the minor numbers (ie. ports)
		 */
		for (j = 1; j <= num_ports - 1; ++j)
			if (serial_table[i + j])
				good_spot = 0;
		if (good_spot == 0)
			continue;

		serial = kmalloc(sizeof(struct usb_serial), GFP_KERNEL);
		if (!serial) {
			err("%s - Out of memory", __func__);
			return NULL;
		}
		memset(serial, 0, sizeof(struct usb_serial));
		serial_table[i] = serial;
		*minor = i;
		mydbg("%s - minor base = %d\n", __func__, *minor);

		/*
		 * copy in the pointer into the array starting a the *minor
		 * position minor is the index into the array.
		 */
		for (i = *minor + 1;
		     (i < (*minor + num_ports)) && (i < SERIAL_TTY_MINORS); ++i)
			serial_table[i] = serial;
		return serial;
	}
	return NULL;
}

static int flip_that(struct tty_struct *tty, __u16 index,
		     struct usb_serial *serial)
{
	tty_flip_buffer_push(tty);
	tty_schedule_flip(tty);
	return 0;
}

/* Handles processing and moving data to the tty layer */
static void port_sofrint(void *private)
{
	struct usb_serial_port *port = private;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct tty_struct *tty = port->tty;
	unsigned char *data = port->read_urb->transfer_buffer;
	unsigned int index;
	struct urb *urb = port->read_urb;
	unsigned int RxCount = urb->actual_length;
	int i, result;
	int flag, flag_data;

	/* index = MINOR(port->tty->device) - serial->minor; */
	index = tty->index - serial->minor;

	mydbg("%s - port %d\n", __func__, port->number);
	mydbg("%s - port->RxHolding = %d\n", __func__, port->RxHolding);

	if (port_paranoia_check(port, __func__) != 0) {
		mydbg("%s - port_paranoia_check, exiting\n", __func__);
		port->ReadBulkStopped = 1;
		return;
	}

	if (!serial) {
		mydbg("%s - bad serial pointer, exiting\n", __func__);
		return;
	}
	if (port->closePending == 1) {
		/* Were closing , stop reading */
		mydbg("%s - (port->closepending == 1\n", __func__);
		port->ReadBulkStopped = 1;
		return;
	}

	/*
	 * RxHolding is asserted by throttle, if we assert it, we're not
	 * receiving any more characters and let the box handle the flow
	 * control
	 */
	if (port->RxHolding == 1) {
		port->ReadBulkStopped = 1;
		return;
	}

	if (urb->status) {
		port->ReadBulkStopped = 1;

		mydbg("%s - nonzero read bulk status received: %d\n",
		      __func__, urb->status);
		return;
	}

	tty = port->tty;
	mydbg("%s - port %d, tty =0x%p\n", __func__, port->number, tty);

	if (tty && RxCount) {
		flag_data = 0;
		for (i = 0; i < RxCount; ++i) {
			/* Look ahead code here */
			if ((i <= (RxCount - 3)) && (THISCHAR == 0x1b)
			    && (NEXTCHAR == 0x1b)) {
				flag = 0;
				switch (THIRDCHAR) {
				case 0x00:
					/* Line status change 4th byte must follow */
					if (i > (RxCount - 4)) {
						mydbg("Illegal escape sequences in received data\n");
						break;
					}
					ProcessLineStatus(port, FOURTHCHAR);
					i += 3;
					flag = 1;
					break;

				case 0x01:
					/* Modem status status change 4th byte must follow */
					mydbg("Modem status status. \n");
					if (i > (RxCount - 4)) {
						mydbg
						    ("Illegal escape sequences in received data\n");
						break;
					}
					ProcessModemStatus(port, FOURTHCHAR);
					i += 3;
					flag = 1;
					break;
				case 0xff:
					mydbg("No status sequence. \n");

					ProcessRxChar(port, THISCHAR);
					ProcessRxChar(port, NEXTCHAR);
					i += 2;
					break;
				}
				if (flag == 1)
					continue;
			}

			if (tty && urb->actual_length) {
				tty_buffer_request_room(tty, 1);
				tty_insert_flip_string(tty, (data + i), 1);
			}

		}
		tty_flip_buffer_push(tty);
	}

	/* Continue trying to always read  */
	usb_fill_bulk_urb(port->read_urb, serial->dev,
			  usb_rcvbulkpipe(serial->dev,
					  port->bulk_in_endpointAddress),
			  port->read_urb->transfer_buffer,
			  port->read_urb->transfer_buffer_length,
			  qt_read_bulk_callback, port);
	result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
	if (result)
		mydbg("%s - failed resubmitting read urb, error %d",
		      __func__, result);
	else {
		if (tty && RxCount)
			flip_that(tty, index, serial);
	}

	return;

}

static void qt_read_bulk_callback(struct urb *urb)
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;

	if (urb->status) {
		port->ReadBulkStopped = 1;
		mydbg("%s - nonzero write bulk status received: %d\n",
		      __func__, urb->status);
		return;
	}

	port_sofrint((void *)port);
	schedule_work(&port->work);
}

static void ProcessRxChar(struct usb_serial_port *port, unsigned char Data)
{
	struct tty_struct *tty;
	struct urb *urb = port->read_urb;
	tty = port->tty;
	/* if we insert more than TTY_FLIPBUF_SIZE characters, we drop them. */

	if (tty && urb->actual_length) {
		tty_buffer_request_room(tty, 1);
		tty_insert_flip_string(tty, &Data, 1);
		/* tty_flip_buffer_push(tty); */
	}

	return;
}

static void ProcessLineStatus(struct usb_serial_port *port,
			      unsigned char line_status)
{

	port->shadowLSR =
	    line_status & (SERIAL_LSR_OE | SERIAL_LSR_PE | SERIAL_LSR_FE |
			   SERIAL_LSR_BI);
	return;
}

static void ProcessModemStatus(struct usb_serial_port *port,
			       unsigned char modem_status)
{

	port->shadowMSR = modem_status;
	wake_up_interruptible(&port->wait);
	return;
}

static void serqt_usb_disconnect(struct usb_interface *interface)
{
	struct usb_serial *serial = usb_get_intfdata(interface);
	/* struct device *dev = &interface->dev; */
	struct usb_serial_port *port;
	int i;

	mydbg("%s\n", __func__);
	if (serial) {

		serial->dev = NULL;

		for (i = 0; i < serial->num_ports; ++i)
			serial->port[i].open_count = 0;

		for (i = 0; i < serial->num_bulk_in; ++i) {
			port = &serial->port[i];
			usb_unlink_urb(port->read_urb);
			usb_free_urb(port->read_urb);
			kfree(port->bulk_in_buffer);
		}
		for (i = 0; i < serial->num_bulk_out; ++i) {
			port = &serial->port[i];
			usb_unlink_urb(port->write_urb);
			usb_free_urb(port->write_urb);
			kfree(port->bulk_out_buffer);
		}
		for (i = 0; i < serial->num_interrupt_in; ++i) {
			port = &serial->port[i];
			usb_unlink_urb(port->interrupt_in_urb);
			usb_free_urb(port->interrupt_in_urb);
			kfree(port->interrupt_in_buffer);
	}

		/* return the minor range that this device had */
		return_serial(serial);

		/* free up any memory that we allocated */
		kfree(serial);

	} else {
		dev_info(&interface->dev, "device disconnected");
	}

}

static struct usb_serial *get_serial_by_minor(unsigned int minor)
{
	return serial_table[minor];
}

/*****************************************************************************
 * Driver tty interface functions
 *****************************************************************************/
static int serial_open(struct tty_struct *tty, struct file *filp)
{
	struct usb_serial *serial;
	struct usb_serial_port *port;
	unsigned int portNumber;
	int retval = 0;

	mydbg("%s\n", __func__);

	/* initialize the pointer incase something fails */
	tty->driver_data = NULL;

	/* get the serial object associated with this tty pointer */
	/* serial = get_serial_by_minor (MINOR(tty->device)); */

	/* get the serial object associated with this tty pointer */
	serial = get_serial_by_minor(tty->index);

	if (serial_paranoia_check(serial, __func__))
		return -ENODEV;

	/* set up our port structure making the tty driver remember our port object, and us it */
	portNumber = tty->index - serial->minor;
	port = &serial->port[portNumber];
	tty->driver_data = port;

	down(&port->sem);
	port->tty = tty;

	++port->open_count;
	if (port->open_count == 1) {
		port->closePending = 0;
		mydbg("%s port->closepending = 0\n", __func__);

		port->RxHolding = 0;
		mydbg("%s port->RxHolding = 0\n", __func__);

		retval = qt_open(tty, port, filp);
	}

	if (retval)
		port->open_count = 0;
	mydbg("%s returning port->closePending  = %d\n", __func__,
	      port->closePending);

	up(&port->sem);
	return retval;
}

/*****************************************************************************
 *device's specific driver functions
 *****************************************************************************/
static int qt_open(struct tty_struct *tty, struct usb_serial_port *port,
					struct file *filp)
{
	struct usb_serial *serial = port->serial;
	int result = 0;
	unsigned int index;
	struct qt_get_device_data DeviceData;
	struct qt_open_channel_data ChannelData;
	unsigned short default_divisor = 0x30;		/* gives 9600 baud rate */
	unsigned char default_LCR = SERIAL_8_DATA;	/* 8, none , 1 */
	int status = 0;

	if (port_paranoia_check(port, __func__))
		return -ENODEV;

	mydbg("%s - port %d\n", __func__, port->number);

	index = tty->index - serial->minor;

	status = box_get_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_get_device failed\n");
		return status;
	}
	serial->num_OpenCount++;
	mydbg("%s serial->num_OpenCount  = %d\n", __func__,
	      serial->num_OpenCount);
	/* Open uart channel */

	/* Port specific setups */
	status = BoxOPenCloseChannel(serial, index, 1, &ChannelData);
	if (status < 0) {
		mydbg(__FILE__ "BoxOPenCloseChannel failed\n");
		return status;
	}
	mydbg(__FILE__ "BoxOPenCloseChannel completed.\n");

	port->shadowLSR = ChannelData.line_status &
	    (SERIAL_LSR_OE | SERIAL_LSR_PE | SERIAL_LSR_FE | SERIAL_LSR_BI);

	port->shadowMSR = ChannelData.modem_status &
	    (SERIAL_MSR_CTS | SERIAL_MSR_DSR | SERIAL_MSR_RI | SERIAL_MSR_CD);

	/* Set Baud rate to default and turn off (default)flow control here */
	status = BoxSetUart(serial, index, default_divisor, default_LCR);
	if (status < 0) {
		mydbg(__FILE__ "BoxSetUart failed\n");
		return status;
	}
	mydbg(__FILE__ "BoxSetUart completed.\n");

	/* Put this here to make it responsive to stty and defauls set by the tty layer */
	qt_set_termios(tty, port, NULL);

	/* Initialize the wait que head */
	init_waitqueue_head(&(port->wait));

	/* if we have a bulk endpoint, start reading from it */
	if (serial->num_bulk_in) {
		/* Start reading from the device */
		usb_fill_bulk_urb(port->read_urb, serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  port->
						  bulk_in_endpointAddress),
				  port->read_urb->transfer_buffer,
				  port->read_urb->transfer_buffer_length,
				  qt_read_bulk_callback, port);

		port->ReadBulkStopped = 0;

		result = usb_submit_urb(port->read_urb, GFP_ATOMIC);

		if (result) {
			err("%s - failed resubmitting read urb, error %d\n",
			    __func__, result);
			port->ReadBulkStopped = 1;
		}

	}

	return result;
}

static void serial_close(struct tty_struct *tty, struct file *filp)
{
	struct usb_serial_port *port =
	    tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	if (!serial)
		return;

	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);

	/* if disconnect beat us to the punch here, there's nothing to do */
	if (tty->driver_data) {
		if (!port->open_count) {
			mydbg("%s - port not opened\n", __func__);
			goto exit;
		}

		--port->open_count;
		if (port->open_count <= 0) {
			port->closePending = 1;
			mydbg("%s - port->closePending = 1\n", __func__);

			if (serial->dev) {
				qt_close(tty, port, filp);
				port->open_count = 0;
			}
		}

	}

exit:
	up(&port->sem);

	mydbg("%s - %d return\n", __func__, port->number);

}

static void qt_close(struct tty_struct *tty, struct usb_serial_port *port,
							struct file *filp)
{
	unsigned long jift = jiffies + 10 * HZ;
	u8 lsr, mcr;
	struct usb_serial *serial = port->serial;
	int status;
	unsigned int index;

	struct qt_open_channel_data ChannelData;
	status = 0;
	lsr = 0;

	mydbg("%s - port %d\n", __func__, port->number);
	index = tty->index - serial->minor;

	/* shutdown any bulk reads that might be going on */
	if (serial->num_bulk_out)
		usb_unlink_urb(port->write_urb);
	if (serial->num_bulk_in)
		usb_unlink_urb(port->read_urb);

	/* wait up to 30 seconds for transmitter to empty */
	do {
		status = BoxGetRegister(serial, index, LINE_STATUS_REGISTER, &lsr);
		if (status < 0) {
			mydbg(__FILE__ "box_get_device failed\n");
			break;
		}

		if ((lsr & SERIAL_LSR_TEMT)
		    && (port->ReadBulkStopped == 1))
			break;
		schedule();

	}
	while (jiffies <= jift);

	if (jiffies > jift)
		mydbg("%s - port %d timout of checking transmitter empty\n",
		      __func__, port->number);
	else
		mydbg("%s - port %d checking transmitter empty succeded\n",
		      __func__, port->number);

	status =
	    BoxGetRegister(serial, index, MODEM_CONTROL_REGISTER,
			   &mcr);
	mydbg(__FILE__ "BoxGetRegister MCR = 0x%x.\n", mcr);

	if (status >= 0) {
		mcr &= ~(SERIAL_MCR_DTR | SERIAL_MCR_RTS);
		/* status = BoxSetRegister(serial, index, MODEM_CONTROL_REGISTER, mcr); */
	}

	/* Close uart channel */
	status = BoxOPenCloseChannel(serial, index, 0, &ChannelData);
	if (status < 0)
		mydbg("%s - port %d BoxOPenCloseChannel failed.\n",
		      __func__, port->number);

	serial->num_OpenCount--;

}

static int serial_write(struct tty_struct *tty, const unsigned char *buf,
			int count)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial;
	int retval = -EINVAL;
	unsigned int index;

	serial = get_usb_serial(port, __func__);
	if (serial == NULL)
		return -ENODEV;
	/* This can happen if we get disconnected a */
	if (port->open_count == 0)
		return -ENODEV;
	index = tty->index - serial->minor;

	mydbg("%s - port %d, %d byte(s)\n", __func__, port->number, count);
	mydbg("%s - port->RxHolding =  %d\n", __func__, port->RxHolding);

	if (!port->open_count) {
		mydbg("%s - port not opened\n", __func__);
		goto exit;
	}

	retval = qt_write(tty, port, buf, count);

exit:
	return retval;
}

static int qt_write(struct tty_struct *tty, struct usb_serial_port *port,
				const unsigned char *buf, int count)
{
	int result;
	unsigned int index;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	if (serial == NULL)
		return -ENODEV;

	mydbg("%s - port %d\n", __func__, port->number);

	if (count == 0) {
		mydbg("%s - write request of 0 bytes\n", __func__);
		return 0;
	}

	index = tty->index - serial->minor;
	/* only do something if we have a bulk out endpoint */
	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS) {
			mydbg("%s - already writing\n", __func__);
			return 0;
		}

		count =
		    (count > port->bulk_out_size) ? port->bulk_out_size : count;
		memcpy(port->write_urb->transfer_buffer, buf, count);

		/* usb_serial_debug_data(__FILE__, __func__, count, port->write_urb->transfer_buffer); */

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
			mydbg("%s - failed submitting write urb, error %d\n",
			      __func__, result);
		else
			result = count;

		return result;
	}

	/* no bulk out, so return 0 bytes written */
	return 0;
}

static void qt_write_bulk_callback(struct urb *urb)
{
	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	mydbg("%s - port %d\n", __func__, port->number);

	if (!serial) {
		mydbg("%s - bad serial pointer, exiting\n", __func__);
		return;
	}

	if (urb->status) {
		mydbg("%s - nonzero write bulk status received: %d\n",
		      __func__, urb->status);
		return;
	}
	port_softint(&port->work);
	schedule_work(&port->work);

	return;
}

static void port_softint(struct work_struct *work)
{
	struct usb_serial_port *port =
	    container_of(work, struct usb_serial_port, work);
	struct usb_serial *serial = get_usb_serial(port, __func__);
	struct tty_struct *tty;

	mydbg("%s - port %d\n", __func__, port->number);

	if (!serial)
		return;

	tty = port->tty;
	if (!tty)
		return;
#if 0
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
	    && tty->ldisc.write_wakeup) {
		mydbg("%s - write wakeup call.\n", __func__);
		(tty->ldisc.write_wakeup) (tty);
	}
#endif

	wake_up_interruptible(&tty->write_wait);
}
static int serial_write_room(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	int retval = -EINVAL;

	if (!serial)
		return -ENODEV;

	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	retval = qt_write_room(port);

exit:
	up(&port->sem);
	return retval;
}
static int qt_write_room(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	int room = 0;
	if (port->closePending == 1) {
		mydbg("%s - port->closePending == 1\n", __func__);
		return -ENODEV;
	}

	mydbg("%s - port %d\n", __func__, port->number);

	if (serial->num_bulk_out) {
		if (port->write_urb->status != -EINPROGRESS)
			room = port->bulk_out_size;
	}

	mydbg("%s - returns %d\n", __func__, room);
	return room;
}
static int serial_chars_in_buffer(struct tty_struct *tty)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	int retval = -EINVAL;

	if (!serial)
		return -ENODEV;

	down(&port->sem);

	mydbg("%s = port %d\n", __func__, port->number);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	retval = qt_chars_in_buffer(port);

exit:
	up(&port->sem);
	return retval;
}

static int qt_chars_in_buffer(struct usb_serial_port *port)
{
	struct usb_serial *serial = port->serial;
	int chars = 0;

	mydbg("%s - port %d\n", __func__, port->number);

	if (serial->num_bulk_out) {
		if (port->write_urb->status == -EINPROGRESS)
			chars = port->write_urb->transfer_buffer_length;
	}

	mydbg("%s - returns %d\n", __func__, chars);
	return chars;
}

static int serial_tiocmset(struct tty_struct *tty, struct file *file,
			   unsigned int set, unsigned int clear)
{

	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	int retval = -ENODEV;
	unsigned int index;
	mydbg("In %s \n", __func__);

	if (!serial)
		return -ENODEV;

	index = tty->index - serial->minor;

	down(&port->sem);

	mydbg("%s - port %d \n", __func__, port->number);
	mydbg("%s - port->RxHolding = %d\n", __func__, port->RxHolding);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	retval = qt_tiocmset(tty, port, file, set);

exit:
	up(&port->sem);
	return retval;
}

static int qt_tiocmset(struct tty_struct *tty, struct usb_serial_port *port,
		       struct file *file, unsigned int value)
{

	u8 mcr;
	int status;
	unsigned int index;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	if (serial == NULL)
		return -ENODEV;

	mydbg("%s - port %d\n", __func__, port->number);

    /**************************************************************************************/
    /**  TIOCMGET
     */
	index = tty->index - serial->minor;
	status =
	    BoxGetRegister(port->serial, index, MODEM_CONTROL_REGISTER,
			   &mcr);
	if (status < 0)
		return -ESPIPE;

	/*
	 * Turn off the RTS and DTR and loopbcck and then only turn on what was
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
	    BoxSetRegister(port->serial, index, MODEM_CONTROL_REGISTER,
			   mcr);
	if (status < 0)
		return -ESPIPE;
	else
		return 0;
}

static int serial_tiocmget(struct tty_struct *tty, struct file *file)
{

	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	int retval = -ENODEV;
	unsigned int index;
	mydbg("In %s \n", __func__);

	if (!serial)
		return -ENODEV;

	index = tty->index - serial->minor;

	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);
	mydbg("%s - port->RxHolding = %d\n", __func__, port->RxHolding);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	retval = qt_tiocmget(tty, port, file);

exit:
	up(&port->sem);
	return retval;
}

static int qt_tiocmget(struct tty_struct *tty,
			struct usb_serial_port *port, struct file *file)
{

	u8 mcr;
	u8 msr;
	unsigned int result = 0;
	int status;
	unsigned int index;

	struct usb_serial *serial = get_usb_serial(port, __func__);
	if (serial == NULL)
		return -ENODEV;

	mydbg("%s - port %d, tty =0x%p\n", __func__, port->number, tty);

    /**************************************************************************************/
    /**  TIOCMGET
     */
	index = tty->index - serial->minor;
	status =
	    BoxGetRegister(port->serial, index, MODEM_CONTROL_REGISTER,
			   &mcr);
	if (status >= 0) {
		status =
		    BoxGetRegister(port->serial, index,
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

static int serial_ioctl(struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{

	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	int retval = -ENODEV;
	unsigned int index;
	mydbg("In %s \n", __func__);

	if (!serial)
		return -ENODEV;

	index = tty->index - serial->minor;

	down(&port->sem);

	mydbg("%s - port %d, cmd 0x%.4x\n", __func__, port->number, cmd);
	mydbg("%s - port->RxHolding = %d\n", __func__, port->RxHolding);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	retval = qt_ioctl(tty, port, file, cmd, arg);

exit:
	up(&port->sem);
	return retval;
}
static int qt_ioctl(struct tty_struct *tty, struct usb_serial_port *port,
			struct file *file, unsigned int cmd, unsigned long arg)
{
	__u8 mcr;
	__u8 msr;
	unsigned short prev_msr;
	unsigned int value, result = 0;
	int status;
	unsigned int index;

	struct usb_serial *serial = get_usb_serial(port, __func__);
	if (serial == NULL)
		return -ENODEV;

	mydbg("%s - port %d, tty =0x%p\n", __func__, port->number, tty);

	/* TIOCMGET */
	index = tty->index - serial->minor;

	if (cmd == TIOCMIWAIT) {
		DECLARE_WAITQUEUE(wait, current);
		prev_msr = port->shadowMSR & SERIAL_MSR_MASK;
		while (1) {
			add_wait_queue(&port->wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
			remove_wait_queue(&port->wait, &wait);
			/* see if a signal woke us up */
			if (signal_pending(current))
				return -ERESTARTSYS;
			msr = port->shadowMSR & SERIAL_MSR_MASK;
			if (msr == prev_msr)
				return -EIO;	/* no change error */

			if ((arg & TIOCM_RNG
			     && ((prev_msr & SERIAL_MSR_RI) ==
				 (msr & SERIAL_MSR_RI)))
			    || (arg & TIOCM_DSR
				&& ((prev_msr & SERIAL_MSR_DSR) ==
				    (msr & SERIAL_MSR_DSR)))
			    || (arg & TIOCM_CD
				&& ((prev_msr & SERIAL_MSR_CD) ==
				    (msr & SERIAL_MSR_CD)))
			    || (arg & TIOCM_CTS
				&& ((prev_msr & SERIAL_MSR_CTS) ==
				    (msr & SERIAL_MSR_CTS)))) {
				return 0;
			}

		}

	}
	mydbg("%s -No ioctl for that one.  port = %d\n", __func__,
	      port->number);

	return -ENOIOCTLCMD;
}

static void serial_set_termios(struct tty_struct *tty, struct ktermios *old)
{
	struct usb_serial_port *port =
	    tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);

	if (!serial)
		return;

	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	/* pass on to the driver specific version of this function if it is available */
	qt_set_termios(tty, port, old);

exit:
	up(&port->sem);
}

static void qt_set_termios(struct tty_struct *tty,
			   struct usb_serial_port *port,
			   struct ktermios *old_termios)
{
	unsigned int cflag;
	int baud, divisor, remainder;
	unsigned char new_LCR = 0;
	int status;
	struct usb_serial *serial;
	__u16 index;
	__u16 tmp, tmp2;

	mydbg("%s - port %d\n", __func__, port->number);

	tmp = port->tty->index;
	mydbg("%s - MINOR(port->tty->index) =  %d\n", __func__, tmp);

	serial = port->serial;
	tmp2 = serial->minor;
	mydbg("%s - serial->minor =  %d\n", __func__, tmp2);

	index = port->tty->index - serial->minor;

	cflag = tty->termios->c_cflag;

	mydbg("%s - 3\n", __func__);

	switch (cflag) {
	case CS5:
		new_LCR |= SERIAL_5_DATA;
		break;
	case CS6:
		new_LCR |= SERIAL_6_DATA;
		break;
	case CS7:
		new_LCR |= SERIAL_7_DATA;
		break;
	default:
	case CS8:
		new_LCR |= SERIAL_8_DATA;
		break;
	}

	/* Parity stuff */
	if (cflag & PARENB) {
		if (cflag & PARODD)
			new_LCR |= SERIAL_ODD_PARITY;
		else
			new_LCR |= SERIAL_EVEN_PARITY;
	}
	if (cflag & CSTOPB)
		new_LCR |= SERIAL_TWO_STOPB;
	else
		new_LCR |= SERIAL_TWO_STOPB;

	mydbg("%s - 4\n", __func__);
	/* Thats the LCR stuff, go ahead and set it */
	baud = tty_get_baud_rate(tty);
	if (!baud)
		/* pick a default, any default... */
		baud = 9600;

	mydbg("%s - got baud = %d\n", __func__, baud);

	divisor = MAX_BAUD_RATE / baud;
	remainder = MAX_BAUD_RATE % baud;
	/* Round to nearest divisor */
	if (((remainder * 2) >= baud) && (baud != 110))
		divisor++;

	/*
	 * Set Baud rate to default and turn off (default)flow control here
	 */
	status = BoxSetUart(serial, index, (unsigned short)divisor, new_LCR);
	if (status < 0) {
		mydbg(__FILE__ "BoxSetUart failed\n");
		return;
	}

	/* Now determine flow control */
	if (cflag & CRTSCTS) {
		mydbg("%s - Enabling HW flow control port %d\n", __func__,
		      port->number);

		/* Enable RTS/CTS flow control */
		status = BoxSetHW_FlowCtrl(serial, index, 1);

		if (status < 0) {
			mydbg(__FILE__ "BoxSetHW_FlowCtrl failed\n");
			return;
		}
	} else {
		/* Disable RTS/CTS flow control */
		mydbg("%s - disabling HW flow control port %d\n", __func__,
		      port->number);

		status = BoxSetHW_FlowCtrl(serial, index, 0);
		if (status < 0) {
			mydbg(__FILE__ "BoxSetHW_FlowCtrl failed\n");
			return;
		}

	}

	/* if we are implementing XON/XOFF, set the start and stop character in
	 * the device */
	if (I_IXOFF(tty) || I_IXON(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
		unsigned char start_char = START_CHAR(tty);
		status =
		    BoxSetSW_FlowCtrl(serial, index, stop_char,
				      start_char);
		if (status < 0)
			mydbg(__FILE__ "BoxSetSW_FlowCtrl (enabled) failed\n");

	} else {
		/* disable SW flow control */
		status = BoxDisable_SW_FlowCtrl(serial, index);
		if (status < 0)
			mydbg(__FILE__ "BoxSetSW_FlowCtrl (diabling) failed\n");

	}
	tty->termios->c_cflag &= ~CMSPAR;
	/* FIXME: Error cases should be returning the actual bits changed only */
}

/****************************************************************************
* BoxGetRegister
*	issuse a GET_REGISTER vendor-spcific request on the default control pipe
*	If successful, fills in the  pValue with the register value asked for
****************************************************************************/
static int BoxGetRegister(struct usb_serial *serial, unsigned short Uart_Number,
			  unsigned short Register_Num, __u8 *pValue)
{
	int result;
	__u16 current_length;

	current_length = sizeof(struct qt_get_device_data);

	result =
	    usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			    QT_GET_SET_REGISTER, 0xC0, Register_Num,
			    Uart_Number, (void *)pValue, sizeof(*pValue), 300);

	return result;
}

/****************************************************************************
* BoxSetRegister
*	issuse a GET_REGISTER vendor-spcific request on the default control pipe
*	If successful, fills in the  pValue with the register value asked for
****************************************************************************/
static int BoxSetRegister(struct usb_serial *serial, unsigned short Uart_Number,
			  unsigned short Register_Num, unsigned short Value)
{
	int result;
	unsigned short RegAndByte;

	RegAndByte = Value;
	RegAndByte = RegAndByte << 8;
	RegAndByte = RegAndByte + Register_Num;

/*
	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 QT_GET_SET_REGISTER, 0xC0, Register_Num,
				 Uart_Number, NULL, 0, 300);
*/

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_GET_SET_REGISTER, 0x40, RegAndByte, Uart_Number,
			    NULL, 0, 300);

	return result;
}

/**
 * box_get_device
 *   Issue a GET_DEVICE vendor-specific request on the default control pipe If
 *   successful, fills in the qt_get_device_data structure pointed to by
 *   device_data, otherwise return a negative error number of the problem.
 */
static int box_get_device(struct usb_serial *serial,
			  struct qt_get_device_data *device_data)
{
	int result;
	__u16 current_length;
	unsigned char *transfer_buffer;

	current_length = sizeof(struct qt_get_device_data);
	transfer_buffer = kmalloc(current_length, GFP_KERNEL);
	if (!transfer_buffer)
		return -ENOMEM;

	result = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
				 QT_SET_GET_DEVICE, 0xc0, 0, 0,
				 transfer_buffer, current_length, 300);
	if (result > 0)
		memcpy(device_data, transfer_buffer, current_length);
	kfree(transfer_buffer);

	return result;
}

/**
 * box_set_device
 *   Issue a SET_DEVICE vendor-specific request on the default control pipe If
 *   successful returns the number of bytes written, otherwise it returns a
 *   negative error number of the problem.
 */
static int box_set_device(struct usb_serial *serial,
			  struct qt_get_device_data *device_data)
{
	int result;
	__u16 length;
	__u16 PortSettings;

	PortSettings = ((__u16) (device_data->portb));
	PortSettings = (PortSettings << 8);
	PortSettings += ((__u16) (device_data->porta));

	length = sizeof(struct qt_get_device_data);
	mydbg("%s - PortSettings = 0x%x\n", __func__, PortSettings);

	result = usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
				 QT_SET_GET_DEVICE, 0x40, PortSettings,
				 0, NULL, 0, 300);
	return result;
}

/****************************************************************************
 * BoxOPenCloseChannel
 * This funciotn notifies the device that the device driver wishes to open a particular UART channel. its
 * purpose is to allow the device driver and the device to synchronize state information.
 * OpenClose = 1 for open , 0 for close
  ****************************************************************************/
static int BoxOPenCloseChannel(struct usb_serial *serial, __u16 Uart_Number,
			       __u16 OpenClose,
			       struct qt_open_channel_data *pDeviceData)
{
	int result;
	__u16 length;
	__u8 Direcion;
	unsigned int pipe;
	length = sizeof(struct qt_open_channel_data);

	/* if opening... */
	if (OpenClose == 1) {
		Direcion = USBD_TRANSFER_DIRECTION_IN;
		pipe = usb_rcvctrlpipe(serial->dev, 0);
		result =
		    usb_control_msg(serial->dev, pipe, QT_OPEN_CLOSE_CHANNEL,
				    Direcion, OpenClose, Uart_Number,
				    pDeviceData, length, 300);

	} else {
		Direcion = USBD_TRANSFER_DIRECTION_OUT;
		pipe = usb_sndctrlpipe(serial->dev, 0);
		result =
		    usb_control_msg(serial->dev, pipe, QT_OPEN_CLOSE_CHANNEL,
				    Direcion, OpenClose, Uart_Number, NULL, 0,
				    300);

	}

	return result;
}

/****************************************************************************
 *  BoxSetPrebufferLevel
   TELLS BOX WHEN TO ASSERT FLOW CONTROL
 ****************************************************************************/
static int BoxSetPrebufferLevel(struct usb_serial *serial)
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
 *  BoxSetATC
   TELLS BOX WHEN TO ASSERT automatic transmitter control
   ****************************************************************************/
static int BoxSetATC(struct usb_serial *serial, __u16 n_Mode)
{
	int result;
	__u16 buffer_length;

	buffer_length = PREFUFF_LEVEL_CONSERVATIVE;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SET_ATF, 0x40, n_Mode, 0, NULL, 0, 300);

	return result;
}

/****************************************************************************
* BoxSetUart
*	issuse a SET_UART vendor-spcific request on the default control pipe
*	If successful sets baud rate divisor and LCR value
****************************************************************************/
static int BoxSetUart(struct usb_serial *serial, unsigned short Uart_Number,
		      unsigned short default_divisor, unsigned char default_LCR)
{
	int result;
	unsigned short UartNumandLCR;

	UartNumandLCR = (default_LCR << 8) + Uart_Number;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_GET_SET_UART, 0x40, default_divisor,
			    UartNumandLCR, NULL, 0, 300);

	return result;
}

static int BoxSetHW_FlowCtrl(struct usb_serial *serial, unsigned int index,
			     int bSet)
{
	__u8 mcr = 0;
	__u8 msr = 0, MOUT_Value = 0;
	struct usb_serial_port *port;
	unsigned int status;

	port = serial->port;

	if (bSet == 1) {
		/* flow control, box will clear RTS line to prevent remote */
		mcr = SERIAL_MCR_RTS;
	}			/* device from xmitting more chars */
	else {
		/* no flow control to remote device */
		mcr = 0;

	}
	MOUT_Value = mcr << 8;

	if (bSet == 1) {
		/* flow control, box will inhibit xmit data if CTS line is
		 * asserted */
		msr = SERIAL_MSR_CTS;
	} else {
		/* Box will not inhimbe xmit data due to CTS line */
		msr = 0;
	}
	MOUT_Value |= msr;

	status =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_HW_FLOW_CONTROL_MASK, 0x40, MOUT_Value,
			    index, NULL, 0, 300);
	return status;

}

static int BoxSetSW_FlowCtrl(struct usb_serial *serial, __u16 index,
			     unsigned char stop_char, unsigned char start_char)
{
	__u16 nSWflowout;
	int result;

	nSWflowout = start_char << 8;
	nSWflowout = (unsigned short)stop_char;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SW_FLOW_CONTROL_MASK, 0x40, nSWflowout,
			    index, NULL, 0, 300);
	return result;

}
static int BoxDisable_SW_FlowCtrl(struct usb_serial *serial, __u16 index)
{
	int result;

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_SW_FLOW_CONTROL_DISABLE, 0x40, 0, index,
			    NULL, 0, 300);
	return result;

}

static void serial_throttle(struct tty_struct *tty)
{
	struct usb_serial_port *port =
	    tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	mydbg("%s - port %d\n", __func__, port->number);

	if (!serial)
		return;

	down(&port->sem);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}
	/* shut down any bulk reads that may be going on */
/*	usb_unlink_urb (port->read_urb); */
	/* pass on to the driver specific version of this function */
	port->RxHolding = 1;
	mydbg("%s - port->RxHolding = 1\n", __func__);

exit:
	up(&port->sem);
	return;
}

static void serial_unthrottle(struct tty_struct *tty)
{
	struct usb_serial_port *port =
	    tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	unsigned int result;

	if (!serial)
		return;
	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	if (port->RxHolding == 1) {
		mydbg("%s -port->RxHolding == 1\n", __func__);

		port->RxHolding = 0;
		mydbg("%s - port->RxHolding = 0\n", __func__);

		/* if we have a bulk endpoint, start it up */
		if ((serial->num_bulk_in) && (port->ReadBulkStopped == 1)) {
			/* Start reading from the device */
			usb_fill_bulk_urb(port->read_urb, serial->dev,
					  usb_rcvbulkpipe(serial->dev,
							  port->
							  bulk_in_endpointAddress),
					  port->read_urb->transfer_buffer,
					  port->read_urb->
					  transfer_buffer_length,
					  qt_read_bulk_callback, port);
			result = usb_submit_urb(port->read_urb, GFP_ATOMIC);
			if (result)
				err("%s - failed restarting read urb, error %d",
				    __func__, result);
		}
	}
exit:
	up(&port->sem);
	return;

}

static int serial_break(struct tty_struct *tty, int break_state)
{
	struct usb_serial_port *port = tty->driver_data;
	struct usb_serial *serial = get_usb_serial(port, __func__);
	u16 index, onoff;
	unsigned int result;

	index = tty->index - serial->minor;
	if (!serial)
		return -ENODEV;

	if (break_state == -1)
		onoff = 1;
	else
		onoff = 0;

	down(&port->sem);

	mydbg("%s - port %d\n", __func__, port->number);

	if (!port->open_count) {
		mydbg("%s - port not open\n", __func__);
		goto exit;
	}

	result =
	    usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			    QT_BREAK_CONTROL, 0x40, onoff, index,
			    NULL, 0, 300);

exit:
	up(&port->sem);
	return 0;
}

static int ioctl_serial_usb(struct inode *innod, struct file *filp, unsigned int cmd,
		     unsigned long arg)
{

	unsigned err;
	unsigned ucOPR_NewValue, uc_Value;
	int *p_Num_of_adapters, counts, index, *p_QMCR_Value;
	struct identity *p_Identity_of;
	struct identity Identity_of;
	struct usb_serial *lastserial, *serial;

	mydbg(KERN_DEBUG "ioctl_serial_usb  cmd =\n");
	if (_IOC_TYPE(cmd) != SERIALQT_PCI_IOC_MAGIC)
		return -ENOTTY;
	if (_IOC_NR(cmd) > SERIALQT_IOC_MAXNR)
		return -ENOTTY;
	mydbg(KERN_DEBUG "ioctl_serial_usb  cmd = 0x%x\n", cmd);
	err = 0;
	switch (cmd) {

	case SERIALQT_WRITE_QMCR:
		err = -ENOTTY;
		index = arg >> 16;
		counts = 0;

		ucOPR_NewValue = arg;

		err = EmulateWriteQMCR_Reg(index, ucOPR_NewValue);
		break;

	case SERIALQT_READ_QMCR:
		err = -ENOTTY;
		p_QMCR_Value = (int *)arg;
		index = arg >> 16;
		counts = 0;

		err = EmulateReadQMCR_Reg(index, &uc_Value);
		if (err == 0)
			err = put_user(uc_Value, p_QMCR_Value);
		break;

	case SERIALQT_GET_NUMOF_UNITS:
		p_Num_of_adapters = (int *)arg;
		counts = 0;	/* Initialize counts to zero */
		/* struct usb_serial *lastserial = serial_table[0], *serial; */
		lastserial = serial_table[0];

		mydbg(KERN_DEBUG "SERIALQT_GET_NUMOF_UNITS \n");
		/* if first pointer is nonull, we at least have one box */
		if (lastserial)
			counts = 1;	/* we at least have one box */

		for (index = 1; index < SERIAL_TTY_MINORS; index++) {
			serial = serial_table[index];
			if (serial) {
				if (serial != lastserial) {
					/* we had a change in the array, hence
					 * another box is there */
					lastserial = serial;
					counts++;
				}
			} else
				break;
		}

		mydbg(KERN_DEBUG "ioctl_serial_usb writting counts = %d",
		      counts);

		err = put_user(counts, p_Num_of_adapters);

		break;
	case SERIALQT_GET_THIS_UNIT:
		counts = 0;
		p_Identity_of = (struct identity *)arg;
		/* copy user structure to local variable */
		get_user(Identity_of.index, &p_Identity_of->index);
		mydbg(KERN_DEBUG "SERIALQT_GET_THIS_UNIT Identity_of.index\n");
		mydbg(KERN_DEBUG
		      "SERIALQT_GET_THIS_UNIT Identity_of.index= 0x%x\n",
		      Identity_of.index);

		err = -ENOTTY;
		serial = find_the_box(Identity_of.index);
		if (serial) {
			err =
			    put_user(serial->product,
				     &p_Identity_of->n_identity);

		}
		break;

	case SERIALQT_IS422_EXTENDED:
		err = -ENOTTY;
		mydbg(KERN_DEBUG "SERIALQT_IS422_EXTENDED \n");
		index = arg >> 16;

		counts = 0;

		mydbg(KERN_DEBUG
		      "SERIALQT_IS422_EXTENDED, looking Identity_of.indext = 0x%x\n",
		      index);
		serial = find_the_box(index);
		if (serial) {
			mydbg("%s index = 0x%x, serial = 0x%p\n", __func__,
			      index, serial);
			for (counts = 0; serqt_422_table[counts] != 0; counts++) {

				mydbg
				    ("%s serial->product = = 0x%x, serqt_422_table[counts] = 0x%x\n",
				     __func__, serial->product,
				     serqt_422_table[counts]);
				if (serial->product == serqt_422_table[counts]) {
					err = 0;

					mydbg
					    ("%s found match for 422extended\n",
					     __func__);
					break;
				}
			}
		}
		break;

	default:
		err = -ENOTTY;
	}

	mydbg("%s returning err = 0x%x\n", __func__, err);
	return err;
}

static struct usb_serial *find_the_box(unsigned int index)
{
	struct usb_serial *lastserial, *foundserial, *serial;
	int counts = 0, index2;
	lastserial = serial_table[0];
	foundserial = NULL;
	for (index2 = 0; index2 < SERIAL_TTY_MINORS; index2++) {
		serial = serial_table[index2];

		mydbg("%s index = 0x%x, index2 = 0x%x, serial = 0x%p\n",
		      __func__, index, index2, serial);

		if (serial) {
			/* first see if this is the unit we'er looking for */
			mydbg
			    ("%s inside if(serial) counts = 0x%x , index = 0x%x\n",
			     __func__, counts, index);
			if (counts == index) {
				/* we found the one we're looking for, copythe
				 * product Id to user */
				mydbg("%s we found the one we're looking for serial = 0x%p\n",
				     __func__, serial);
				foundserial = serial;
				break;
			}

			if (serial != lastserial) {
				/* when we have a change in the pointer */
				lastserial = serial;
				counts++;
			}
		} else
			break;	/* no matches */
	}

	mydbg("%s returning foundserial = 0x%p\n", __func__, foundserial);
	return foundserial;
}

static int EmulateWriteQMCR_Reg(int index, unsigned uc_value)
{

	__u16 ATC_Mode = 0;
	struct usb_serial *serial;
	int status;
	struct qt_get_device_data DeviceData;
	unsigned uc_temp = 0;
	mydbg("Inside %s, uc_value = 0x%x\n", __func__, uc_value);

	DeviceData.porta = 0;
	DeviceData.portb = 0;
	serial = find_the_box(index);
	/* Determine Duplex mode */
	if (!(serial))
		return -ENOTTY;
	status = box_get_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_set_device failed\n");
		return status;
	}

	uc_temp = uc_value & QMCR_HALF_DUPLEX_MASK;
	switch (uc_temp) {
	case QMCR_FULL_DUPLEX:
		DeviceData.porta &= ~DUPMODE_BITS;
		DeviceData.porta |= FULL_DUPLEX;
		ATC_Mode = ATC_DISABLED;
		break;
	case QMCR_HALF_DUPLEX_RTS:
		DeviceData.porta &= ~DUPMODE_BITS;
		DeviceData.porta |= HALF_DUPLEX_RTS;
		ATC_Mode = ATC_RTS_ENABLED;
		break;
	case QMCR_HALF_DUPLEX_DTR:
		DeviceData.porta &= ~DUPMODE_BITS;
		DeviceData.porta |= HALF_DUPLEX_DTR;
		ATC_Mode = ATC_DTR_ENABLED;
		break;
	default:
		break;
	}

	uc_temp = uc_value & QMCR_CONNECTOR_MASK;
	switch (uc_temp) {
	case QMCR_MODEM_CONTROL:
		DeviceData.portb &= ~LOOPMODE_BITS;	/* reset connection bits */
		DeviceData.portb |= MODEM_CTRL;
		break;
	case QMCR_ALL_LOOPBACK:
		DeviceData.portb &= ~LOOPMODE_BITS;	/* reset connection bits */
		DeviceData.portb |= ALL_LOOPBACK;
		break;
	}

	mydbg(__FILE__ "Calling box_set_device with failed\n");
	status = box_set_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_set_device failed\n");
		return status;
	}

	/* This bit (otherwise unused) i'll used  to detect whether ATC is
	 * selected */
	if (uc_value & QMCR_RX_EN_MASK) {

		mydbg(__FILE__
		      "calling BoxsetATC with DeviceData.porta = 0x%x and DeviceData.portb = 0x%x\n",
		      DeviceData.porta, DeviceData.portb);
		status = BoxSetATC(serial, ATC_Mode);
		if (status < 0) {
			mydbg(__FILE__ "BoxSetATC failed\n");
			return status;
		}
	} else {

		mydbg(__FILE__
		      "calling BoxsetATC with DeviceData.porta = 0x%x and DeviceData.portb = 0x%x\n",
		      DeviceData.porta, DeviceData.portb);
		status = BoxSetATC(serial, ATC_DISABLED);
		if (status < 0) {
			mydbg(__FILE__ "BoxSetATC failed\n");
			return status;
		}
	}

	return 0;

}

static int EmulateReadQMCR_Reg(int index, unsigned *uc_value)
{
	struct usb_serial *serial;
	int status;
	struct qt_get_device_data DeviceData;
	__u8 uc_temp;

	*uc_value = 0;

	serial = find_the_box(index);
	if (!(serial))
		return -ENOTTY;

	status = box_get_device(serial, &DeviceData);
	if (status < 0) {
		mydbg(__FILE__ "box_get_device failed\n");
		return status;
	}
	uc_temp = DeviceData.porta & DUPMODE_BITS;
	switch (uc_temp) {
	case FULL_DUPLEX:
		*uc_value |= QMCR_FULL_DUPLEX;
		break;
	case HALF_DUPLEX_RTS:
		*uc_value |= QMCR_HALF_DUPLEX_RTS;
		break;
	case HALF_DUPLEX_DTR:
		*uc_value |= QMCR_HALF_DUPLEX_DTR;
		break;
	default:
		break;
	}

	/* I use this for ATC control se */
	uc_temp = DeviceData.portb & LOOPMODE_BITS;

	switch (uc_temp) {
	case ALL_LOOPBACK:
		*uc_value |= QMCR_ALL_LOOPBACK;
		break;
	case MODEM_CTRL:
		*uc_value |= QMCR_MODEM_CONTROL;
		break;
	default:
		break;

	}
	return 0;

}

static int __init serqt_usb_init(void)
{
	int i, result;
	int status = 0;

	mydbg("%s\n", __func__);
	tty_set_operations(&serial_tty_driver, &serial_ops);
	result = tty_register_driver(&serial_tty_driver);
	if (result) {
		mydbg("tty_register_driver failed error = 0x%x", result);
		return result;
	}

	/* Initalize our global data */
	for (i = 0; i < SERIAL_TTY_MINORS; ++i)
		serial_table[i] = NULL;

	/* register this driver with the USB subsystem */
	result = usb_register(&serqt_usb_driver);
	if (result < 0) {
		err("usb_register failed for the " __FILE__
		    " driver. Error number %d", result);
		return result;
	}
	status = 0;		/* Dynamic assignment of major number */
	major_number =
	    register_chrdev(status, "SerialQT_USB", &serialqt_usb_fops);
	if (major_number < 0) {
		mydbg(KERN_DEBUG "No devices found \n\n");
		return -EBUSY;
	} else
		mydbg(KERN_DEBUG "SerQT_USB major number assignment = %d \n\n",
		      major_number);

	printk(KERN_INFO DRIVER_DESC " " DRIVER_VERSION);
	return 0;
}

static void __exit serqt_usb_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&serqt_usb_driver);
	tty_unregister_driver(&serial_tty_driver);
	unregister_chrdev(major_number, "SerialQT_USB");
}

module_init(serqt_usb_init);
module_exit(serqt_usb_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
