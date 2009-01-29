/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*************************************************************************
 *** --------------------------------------------------------------------
 ***
 *** Project Name: ATENINTL
 ***
 *** Module Name: ATEN2011
 ***
 *** File: aten2011.c
 ***
 ***
 *** File Revision: 1.2
 ***
 *** Revision Date:  2009-01-16
 ***
 ***
 *** Purpose	  : It gives an interface between USB to  4 Serial
 ***                and serves as a Serial Driver for the high
 ***		    level layers /applications.
 ***
 *** Change History:
 ***	Modified from ATEN revision 1.2 for Linux kernel 2.6.26 or later
 ***
 *** LEGEND	  :
 ***
 ***
 *** DBG - Code inserted due to as part of debugging
 *** DPRINTK - Debug Print statement
 ***
 *************************************************************************/

/* all file inclusion goes here */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
//#include <linux/spinlock.h>
#include <linux/serial.h>
//#include <linux/ioctl.h>
#include <linux/usb.h>
#include <asm/uaccess.h>

#define KERNEL_2_6		1

#include <linux/usb/serial.h>

#define MAX_RS232_PORTS		2	/* Max # of RS-232 ports per device */

#include <linux/version.h>

/*
 *  All typedef goes here
 */


/* typedefs that the insideout headers need */

#ifndef TRUE
	#define TRUE		(1)
#endif

#ifndef FALSE
	#define FALSE		(0)
#endif

#ifndef LOW8
	#define LOW8(val)	((unsigned char)(val & 0xff))
#endif

#ifndef HIGH8
	#define HIGH8(val)	((unsigned char)((val & 0xff00) >> 8))
#endif

#ifndef NUM_ENTRIES
	#define NUM_ENTRIES(x)	(sizeof(x)/sizeof((x)[0]))
#endif

#define MAX_SERIALNUMBER_LEN 	12

/* The following table is used to map the USBx port number to
 * the device serial number (or physical USB path), */

#define MAX_ATENPORTS	2
#define MAX_NAME_LEN	64

#define RAID_REG1 	0x30
#define RAID_REG2 	0x31

#define ZLP_REG1  	0x3A      //Zero_Flag_Reg1    58
#define ZLP_REG2  	0x3B      //Zero_Flag_Reg2    59
#define ZLP_REG3  	0x3C      //Zero_Flag_Reg3    60
#define ZLP_REG4  	0x3D      //Zero_Flag_Reg4    61
#define ZLP_REG5  	0x3E      //Zero_Flag_Reg5    62

#define THRESHOLD_VAL_SP1_1     0x3F
#define THRESHOLD_VAL_SP1_2     0x40
#define THRESHOLD_VAL_SP2_1     0x41
#define THRESHOLD_VAL_SP2_2     0x42

#define THRESHOLD_VAL_SP3_1     0x43
#define THRESHOLD_VAL_SP3_2     0x44
#define THRESHOLD_VAL_SP4_1     0x45
#define THRESHOLD_VAL_SP4_2     0x46


/* For higher baud Rates use TIOCEXBAUD */
#define TIOCEXBAUD	0x5462

#define BAUD_1152	0	/* 115200bps  * 1	*/
#define BAUD_2304	1	/* 230400bps  * 2	*/
#define BAUD_4032	2	/* 403200bps  * 3.5	*/
#define BAUD_4608	3	/* 460800bps  * 4	*/
#define BAUD_8064	4	/* 806400bps  * 7	*/
#define BAUD_9216	5	/* 921600bps  * 8	*/

#define CHASE_TIMEOUT		(5*HZ)		/* 5 seconds */
#define OPEN_TIMEOUT		(5*HZ)		/* 5 seconds */
#define COMMAND_TIMEOUT		(5*HZ)		/* 5 seconds */

#ifndef SERIAL_MAGIC
	#define SERIAL_MAGIC	0x6702
#endif

#define PORT_MAGIC		0x7301



/* vendor id and device id defines */

#define USB_VENDOR_ID_ATENINTL		0x0557
#define ATENINTL_DEVICE_ID_2011		0x2011
#define ATENINTL_DEVICE_ID_7820		0x7820

/* Product information read from the ATENINTL. Provided for later upgrade */

/* Interrupt Rotinue Defines	*/

#define SERIAL_IIR_RLS      0x06
#define SERIAL_IIR_RDA      0x04
#define SERIAL_IIR_CTI      0x0c
#define SERIAL_IIR_THR      0x02
#define SERIAL_IIR_MS       0x00

/*
 *  Emulation of the bit mask on the LINE STATUS REGISTER.
 */
#define SERIAL_LSR_DR       0x0001
#define SERIAL_LSR_OE       0x0002
#define SERIAL_LSR_PE       0x0004
#define SERIAL_LSR_FE       0x0008
#define SERIAL_LSR_BI       0x0010
#define SERIAL_LSR_THRE     0x0020
#define SERIAL_LSR_TEMT     0x0040
#define SERIAL_LSR_FIFOERR  0x0080

//MSR bit defines(place holders)
#define ATEN_MSR_CTS         0x01
#define ATEN_MSR_DSR         0x02
#define ATEN_MSR_RI          0x04
#define ATEN_MSR_CD          0x08
#define ATEN_MSR_DELTA_CTS   0x10
#define ATEN_MSR_DELTA_DSR   0x20
#define ATEN_MSR_DELTA_RI    0x40
#define ATEN_MSR_DELTA_CD    0x80

// Serial Port register Address
#define RECEIVE_BUFFER_REGISTER    ((__u16)(0x00))
#define TRANSMIT_HOLDING_REGISTER  ((__u16)(0x00))
#define INTERRUPT_ENABLE_REGISTER  ((__u16)(0x01))
#define INTERRUPT_IDENT_REGISTER   ((__u16)(0x02))
#define FIFO_CONTROL_REGISTER      ((__u16)(0x02))
#define LINE_CONTROL_REGISTER      ((__u16)(0x03))
#define MODEM_CONTROL_REGISTER     ((__u16)(0x04))
#define LINE_STATUS_REGISTER       ((__u16)(0x05))
#define MODEM_STATUS_REGISTER      ((__u16)(0x06))
#define SCRATCH_PAD_REGISTER       ((__u16)(0x07))
#define DIVISOR_LATCH_LSB          ((__u16)(0x00))
#define DIVISOR_LATCH_MSB          ((__u16)(0x01))

#define SP_REGISTER_BASE           ((__u16)(0x08))
#define CONTROL_REGISTER_BASE      ((__u16)(0x09))
#define DCR_REGISTER_BASE          ((__u16)(0x16))

#define SP1_REGISTER               ((__u16)(0x00))
#define CONTROL1_REGISTER          ((__u16)(0x01))
#define CLK_MULTI_REGISTER         ((__u16)(0x02))
#define CLK_START_VALUE_REGISTER   ((__u16)(0x03))
#define DCR1_REGISTER              ((__u16)(0x04))
#define GPIO_REGISTER              ((__u16)(0x07))

#define CLOCK_SELECT_REG1          ((__u16)(0x13))
#define CLOCK_SELECT_REG2          ((__u16)(0x14))

#define SERIAL_LCR_DLAB       	   ((__u16)(0x0080))

/*
 * URB POOL related defines
 */
#define NUM_URBS                        16     /* URB Count */
#define URB_TRANSFER_BUFFER_SIZE        32     /* URB Size  */

struct ATENINTL_product_info
{
	__u16	ProductId;		/* Product Identifier */
	__u8	NumPorts;		/* Number of ports on ATENINTL */
	__u8	ProdInfoVer;		/* What version of structure is this? */

	__u32	IsServer        :1;	/* Set if Server */
	__u32	IsRS232         :1;	/* Set if RS-232 ports exist */
	__u32	IsRS422         :1;	/* Set if RS-422 ports exist */
	__u32	IsRS485         :1;	/* Set if RS-485 ports exist */
	__u32	IsReserved      :28;	/* Reserved for later expansion */

	__u8	CpuRev;			/* CPU revision level (chg only if s/w visible) */
	__u8	BoardRev;		/* PCB revision level (chg only if s/w visible) */

	__u8	ManufactureDescDate[3];	/* MM/DD/YY when descriptor template was compiled */
	__u8	Unused1[1];		/* Available */
};

// different USB-serial Adapter's ID's table
static struct usb_device_id ATENINTL_port_id_table [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL,ATENINTL_DEVICE_ID_2011) },
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL,ATENINTL_DEVICE_ID_7820) },
	{ } /* terminating entry */
};

static __devinitdata struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL,ATENINTL_DEVICE_ID_2011) },
	{ USB_DEVICE(USB_VENDOR_ID_ATENINTL,ATENINTL_DEVICE_ID_7820) },
	{ } /* terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

/* This structure holds all of the local port information */
struct ATENINTL_port
{
	int		port_num;          /*Actual port number in the device(1,2,etc)*/
	__u8		bulk_out_endpoint;   	/* the bulk out endpoint handle */
	unsigned char 	*bulk_out_buffer;     	/* buffer used for the bulk out endpoint */
	struct urb    	*write_urb;	     	/* write URB for this port */
	__u8	      	bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char 	*bulk_in_buffer;	/* the buffer we use for the bulk in endpoint */
	struct urb   	*read_urb;	     	/* read URB for this port */
	__s16	     rxBytesAvail;/*the number of bytes that we need to read from this device */
	__s16		rxBytesRemaining;	/* the number of port bytes left to read */
	char		write_in_progress;	/* TRUE while a write URB is outstanding */
	__u8		shadowLCR;		/* last LCR value received */
	__u8		shadowMCR;		/* last MCR value received */
	__u8		shadowMSR;		/* last MSR value received */
	__u8		shadowLSR;		/* last LSR value received */
	__u8		shadowXonChar;		/* last value set as XON char in ATENINTL */
	__u8		shadowXoffChar;		/* last value set as XOFF char in ATENINTL */
	__u8		validDataMask;
	__u32		baudRate;
	char		open;
	char		openPending;
	char		commandPending;
	char		closePending;
	char		chaseResponsePending;
	wait_queue_head_t	wait_chase;		/* for handling sleeping while waiting for chase to finish */
	wait_queue_head_t	wait_open;		/* for handling sleeping while waiting for open to finish */
	wait_queue_head_t	wait_command;		/* for handling sleeping while waiting for command to finish */
	wait_queue_head_t	delta_msr_wait;		/* for handling sleeping while waiting for msr change to happen */
	 int                     delta_msr_cond;
	struct async_icount	icount;
	struct usb_serial_port	*port;			/* loop back to the owner of this object */
	/*Offsets*/
	__u16 		AppNum;
	__u8 		SpRegOffset;
	__u8 		ControlRegOffset;
	__u8 		DcrRegOffset;
	__u8 		ClkSelectRegOffset;
	//for processing control URBS in interrupt context
	struct urb 	*control_urb;
       // __le16 rx_creg;
        char 		*ctrl_buf;
	int  		MsrLsr;

	struct urb      *write_urb_pool[NUM_URBS];
	/* we pass a pointer to this as the arguement sent to cypress_set_termios old_termios */
        struct ktermios tmp_termios;        /* stores the old termios settings */
	spinlock_t 	lock;                   /* private lock */
};


/* This structure holds all of the individual serial device information */
struct ATENINTL_serial
{
	char		name[MAX_NAME_LEN+1];		/* string name of this device */
	struct	ATENINTL_product_info	product_info;	/* Product Info */
	__u8		interrupt_in_endpoint;		/* the interrupt endpoint handle */
	unsigned char  *interrupt_in_buffer;		/* the buffer we use for the interrupt endpoint */
	struct urb *	interrupt_read_urb;	/* our interrupt urb */
	__u8		bulk_in_endpoint;	/* the bulk in endpoint handle */
	unsigned char  *bulk_in_buffer;		/* the buffer we use for the bulk in endpoint */
	struct urb 	*read_urb;		/* our bulk read urb */
	__u8		bulk_out_endpoint;	/* the bulk out endpoint handle */
	__s16	 rxBytesAvail;	/* the number of bytes that we need to read from this device */
	__u8		rxPort;		/* the port that we are currently receiving data for */
	__u8		rxStatusCode;		/* the receive status code */
	__u8		rxStatusParam;		/* the receive status paramater */
	__s16		rxBytesRemaining;	/* the number of port bytes left to read */
	struct usb_serial	*serial;	/* loop back to the owner of this object */
	int	ATEN2011_spectrum_2or4ports; 	//this says the number of ports in the device
	// Indicates about the no.of opened ports of an individual USB-serial adapater.
	unsigned int	NoOfOpenPorts;
	// a flag for Status endpoint polling
	unsigned char	status_polling_started;
};

/* baud rate information */
struct ATEN2011_divisor_table_entry
{
	__u32  BaudRate;
	__u16  Divisor;
};

/* Define table of divisors for ATENINTL 2011 hardware	   *
 * These assume a 3.6864MHz crystal, the standard /16, and *
 * MCR.7 = 0.						   */
#ifdef NOTATEN2011
static struct ATEN2011_divisor_table_entry ATEN2011_divisor_table[] = {
	{   50,		2304},
	{   110,	1047},	/* 2094.545455 => 230450   => .0217 % over */
	{   134,	857},	/* 1713.011152 => 230398.5 => .00065% under */
	{   150,	768},
	{   300,	384},
	{   600,	192},
	{   1200,	96},
	{   1800,	64},
	{   2400,	48},
	{   4800,	24},
	{   7200,	16},
	{   9600,	12},
	{   19200,	6},
	{   38400,	3},
	{   57600,	2},
	{   115200,	1},
};
#endif

/* local function prototypes */
/* function prototypes for all URB callbacks */
static void ATEN2011_interrupt_callback(struct urb *urb);
static void ATEN2011_bulk_in_callback(struct urb *urb);
static void ATEN2011_bulk_out_data_callback(struct urb *urb);
static void ATEN2011_control_callback(struct urb *urb);
static int ATEN2011_get_reg(struct ATENINTL_port *ATEN,__u16 Wval, __u16 reg, __u16 * val);
int handle_newMsr(struct ATENINTL_port *port,__u8 newMsr);
int handle_newLsr(struct ATENINTL_port *port,__u8 newLsr);
/* function prototypes for the usbserial callbacks */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int  ATEN2011_open(struct tty_struct *tty, struct usb_serial_port *port, struct file *filp);
static void ATEN2011_close(struct tty_struct *tty, struct usb_serial_port *port, struct file *filp);
static int ATEN2011_write(struct tty_struct *tty, struct usb_serial_port *port, const unsigned char *data, int count);
static int  ATEN2011_write_room(struct tty_struct *tty);
static int  ATEN2011_chars_in_buffer(struct tty_struct *tty);
static void ATEN2011_throttle(struct tty_struct *tty);
static void ATEN2011_unthrottle(struct tty_struct *tty);
static void ATEN2011_set_termios(struct tty_struct *tty, struct usb_serial_port *port, struct ktermios *old_termios);
static int ATEN2011_tiocmset(struct tty_struct *tty, struct file *file,
        			unsigned int set, unsigned int clear);
static int ATEN2011_tiocmget(struct tty_struct *tty, struct file *file);
static int  ATEN2011_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void ATEN2011_break(struct tty_struct *tty, int break_state);
#else
static int  ATEN2011_open(struct usb_serial_port *port, struct file *filp);
static void ATEN2011_close(struct usb_serial_port *port, struct file *filp);
static int ATEN2011_write(struct usb_serial_port *port, const unsigned char *data, int count);
static int  ATEN2011_write_room(struct usb_serial_port *port);
static int  ATEN2011_chars_in_buffer(struct usb_serial_port *port);
static void ATEN2011_throttle(struct usb_serial_port *port);
static void ATEN2011_unthrottle(struct usb_serial_port *port);
static void ATEN2011_set_termios	(struct usb_serial_port *port, struct ktermios *old_termios);
static int ATEN2011_tiocmset(struct usb_serial_port *port, struct file *file,
        			unsigned int set, unsigned int clear);
static int ATEN2011_tiocmget(struct usb_serial_port *port, struct file *file);
static int  ATEN2011_ioctl(struct usb_serial_port *port, struct file *file, unsigned int cmd, unsigned long arg);
static void ATEN2011_break(struct usb_serial_port *port, int break_state);
#endif

//static void ATEN2011_break_ctl(struct usb_serial_port *port, int break_state );

static int  ATEN2011_startup(struct usb_serial *serial);
static void ATEN2011_shutdown(struct usb_serial *serial);
//static int ATEN2011_serial_probe(struct usb_serial *serial, const struct usb_device_id *id);
static int ATEN2011_calc_num_ports(struct usb_serial *serial);

/* function prototypes for all of our local functions */
static int  ATEN2011_calc_baud_rate_divisor(int baudRate, int *divisor,__u16 *clk_sel_val);
static int  ATEN2011_send_cmd_write_baud_rate(struct ATENINTL_port *ATEN2011_port, int baudRate);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_change_port_settings(struct tty_struct *tty, struct ATENINTL_port *ATEN2011_port, struct ktermios *old_termios);
static void ATEN2011_block_until_chase_response(struct tty_struct *tty, struct ATENINTL_port *ATEN2011_port);
static void ATEN2011_block_until_tx_empty(struct tty_struct *tty, struct ATENINTL_port *ATEN2011_port);
#else
static void ATEN2011_change_port_settings(struct ATENINTL_port *ATEN2011_port, struct ktermios *old_termios);
static void ATEN2011_block_until_chase_response(struct ATENINTL_port *ATEN2011_port);
static void ATEN2011_block_until_tx_empty(struct ATENINTL_port *ATEN2011_port);
#endif

int __init ATENINTL2011_init(void);
void __exit ATENINTL2011_exit(void);


/*************************************
 * Bit definitions for each register *
 *************************************/
#define LCR_BITS_5		0x00	/* 5 bits/char */
#define LCR_BITS_6		0x01	/* 6 bits/char */
#define LCR_BITS_7		0x02	/* 7 bits/char */
#define LCR_BITS_8		0x03	/* 8 bits/char */
#define LCR_BITS_MASK		0x03	/* Mask for bits/char field */

#define LCR_STOP_1		0x00	/* 1 stop bit */
#define LCR_STOP_1_5		0x04	/* 1.5 stop bits (if 5   bits/char) */
#define LCR_STOP_2		0x04	/* 2 stop bits   (if 6-8 bits/char) */
#define LCR_STOP_MASK		0x04	/* Mask for stop bits field */

#define LCR_PAR_NONE		0x00	/* No parity */
#define LCR_PAR_ODD		0x08	/* Odd parity */
#define LCR_PAR_EVEN		0x18	/* Even parity */
#define LCR_PAR_MARK		0x28	/* Force parity bit to 1 */
#define LCR_PAR_SPACE		0x38	/* Force parity bit to 0 */
#define LCR_PAR_MASK		0x38	/* Mask for parity field */

#define LCR_SET_BREAK		0x40	/* Set Break condition */
#define LCR_DL_ENABLE		0x80	/* Enable access to divisor latch */

#define MCR_DTR			0x01	/* Assert DTR */
#define MCR_RTS			0x02	/* Assert RTS */
#define MCR_OUT1		0x04	/* Loopback only: Sets state of RI */
#define MCR_MASTER_IE		0x08	/* Enable interrupt outputs */
#define MCR_LOOPBACK		0x10	/* Set internal (digital) loopback mode */
#define MCR_XON_ANY		0x20	/* Enable any char to exit XOFF mode */

#define ATEN2011_MSR_CTS	0x10	/* Current state of CTS */
#define ATEN2011_MSR_DSR	0x20	/* Current state of DSR */
#define ATEN2011_MSR_RI		0x40	/* Current state of RI */
#define ATEN2011_MSR_CD		0x80	/* Current state of CD */


/* all defines goes here */

/*
 * Debug related defines
 */

/* 1: Enables the debugging -- 0: Disable the debugging */

//#define printk //

#define ATEN_DEBUG	0

#ifdef ATEN_DEBUG
static int debug = 0;
#define DPRINTK(fmt, args...) printk( "%s: " fmt, __FUNCTION__ , ## args)

#else
static int debug = 0;
#define DPRINTK(fmt, args...)

#endif
//#undef DPRINTK
//      #define DPRINTK(fmt, args...)

/*
 * Version Information
 */
#define DRIVER_VERSION "1.3.1"
#define DRIVER_DESC "ATENINTL 2011 USB Serial Adapter"

/*
 * Defines used for sending commands to port
 */

#define WAIT_FOR_EVER   (HZ * 0 )	/* timeout urb is wait for ever */
#define ATEN_WDR_TIMEOUT (HZ * 5 )	/* default urb timeout */

#define ATEN_PORT1       0x0200
#define ATEN_PORT2       0x0300
#define ATEN_VENREG      0x0000
#define ATEN_MAX_PORT	 0x02
#define ATEN_WRITE       0x0E
#define ATEN_READ        0x0D

/* Requests */
#define ATEN_RD_RTYPE 0xC0
#define ATEN_WR_RTYPE 0x40
#define ATEN_RDREQ    0x0D
#define ATEN_WRREQ    0x0E
#define ATEN_CTRL_TIMEOUT        500
#define VENDOR_READ_LENGTH                      (0x01)

int ATEN2011_Thr_cnt;
//int ATEN2011_spectrum_2or4ports; //this says the number of ports in the device
//int NoOfOpenPorts;

int RS485mode = 0;		//set to 1 for RS485 mode and 0 for RS232 mode

static struct usb_serial *ATEN2011_get_usb_serial(struct usb_serial_port *port, const
						  char *function);
static int ATEN2011_serial_paranoia_check(struct usb_serial *serial, const char
					  *function);
static int ATEN2011_port_paranoia_check(struct usb_serial_port *port, const char
					*function);

/* setting and get register values */
static int ATEN2011_set_reg_sync(struct usb_serial_port *port, __u16 reg,
				 __u16 val);
static int ATEN2011_get_reg_sync(struct usb_serial_port *port, __u16 reg,
				 __u16 * val);
static int ATEN2011_set_Uart_Reg(struct usb_serial_port *port, __u16 reg,
				 __u16 val);
static int ATEN2011_get_Uart_Reg(struct usb_serial_port *port, __u16 reg,
				 __u16 * val);

void ATEN2011_Dump_serial_port(struct ATENINTL_port *ATEN2011_port);

/************************************************************************/
/************************************************************************/
/*             I N T E R F A C E   F U N C T I O N S			*/
/*             I N T E R F A C E   F U N C T I O N S			*/
/************************************************************************/
/************************************************************************/

static inline void ATEN2011_set_serial_private(struct usb_serial *serial,
					       struct ATENINTL_serial *data)
{
	usb_set_serial_data(serial, (void *)data);
}

static inline struct ATENINTL_serial *ATEN2011_get_serial_private(struct
								  usb_serial
								  *serial)
{
	return (struct ATENINTL_serial *)usb_get_serial_data(serial);
}

static inline void ATEN2011_set_port_private(struct usb_serial_port *port,
					     struct ATENINTL_port *data)
{
	usb_set_serial_port_data(port, (void *)data);
}

static inline struct ATENINTL_port *ATEN2011_get_port_private(struct
							      usb_serial_port
							      *port)
{
	return (struct ATENINTL_port *)usb_get_serial_port_data(port);
}

/*
Description:- To set the Control register by calling usb_fill_control_urb function by passing usb_sndctrlpipe function as parameter.

Input Parameters:
usb_serial_port:  Data Structure usb_serialport correponding to that seril port.
Reg: Register Address
Val:  Value to set in the Register.
 */

static int ATEN2011_set_reg_sync(struct usb_serial_port *port, __u16 reg,
				 __u16 val)
{
	struct usb_device *dev = port->serial->dev;
	val = val & 0x00ff;
	DPRINTK("ATEN2011_set_reg_sync offset is %x, value %x\n", reg, val);

	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ATEN_WRREQ,
			       ATEN_WR_RTYPE, val, reg, NULL, 0,
			       ATEN_WDR_TIMEOUT);
}

/*
Description:- To set the Uart register by calling usb_fill_control_urb function by passing usb_rcvctrlpipe function as parameter.

Input Parameters:
usb_serial_port:  Data Structure usb_serialport correponding to that seril port.
Reg: Register Address
Val:  Value to receive from the Register.
 */

static int ATEN2011_get_reg_sync(struct usb_serial_port *port, __u16 reg,
				 __u16 * val)
{
	struct usb_device *dev = port->serial->dev;
	int ret = 0;

	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ATEN_RDREQ,
			      ATEN_RD_RTYPE, 0, reg, val, VENDOR_READ_LENGTH,
			      ATEN_WDR_TIMEOUT);
	DPRINTK("ATEN2011_get_reg_sync offset is %x, return val %x\n", reg,
		*val);
	*val = (*val) & 0x00ff;
	return ret;
}

/*
Description:- To set the Uart register by calling usb_fill_control_urb function by passing usb_sndctrlpipe function as parameter.

Input Parameters:
usb_serial_port:  Data Structure usb_serialport correponding to that seril port.
Reg: Register Address
Val:  Value to set in the Register.
 */

static int ATEN2011_set_Uart_Reg(struct usb_serial_port *port, __u16 reg,
				 __u16 val)
{

	struct usb_device *dev = port->serial->dev;
	struct ATENINTL_serial *ATEN2011_serial;
	int minor;
	ATEN2011_serial = ATEN2011_get_serial_private(port->serial);
	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;
	val = val & 0x00ff;
	// For the UART control registers, the application number need to be Or'ed

	if (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 4) {
		val |= (((__u16) port->number - (__u16) (minor)) + 1) << 8;
		DPRINTK("ATEN2011_set_Uart_Reg application number is %x\n",
			val);
	} else {
		if (((__u16) port->number - (__u16) (minor)) == 0) {
			//      val= 0x100;
			val |=
			    (((__u16) port->number - (__u16) (minor)) + 1) << 8;
			DPRINTK
			    ("ATEN2011_set_Uart_Reg application number is %x\n",
			     val);
		} else {
			//      val=0x300;
			val |=
			    (((__u16) port->number - (__u16) (minor)) + 2) << 8;
			DPRINTK
			    ("ATEN2011_set_Uart_Reg application number is %x\n",
			     val);
		}
	}
	return usb_control_msg(dev, usb_sndctrlpipe(dev, 0), ATEN_WRREQ,
			       ATEN_WR_RTYPE, val, reg, NULL, 0,
			       ATEN_WDR_TIMEOUT);

}

/*
Description:- To set the Control register by calling usb_fill_control_urb function by passing usb_rcvctrlpipe function as parameter.

Input Parameters:
usb_serial_port:  Data Structure usb_serialport correponding to that seril port.
Reg: Register Address
Val:  Value to receive from the Register.
 */
static int ATEN2011_get_Uart_Reg(struct usb_serial_port *port, __u16 reg,
				 __u16 * val)
{
	struct usb_device *dev = port->serial->dev;
	int ret = 0;
	__u16 Wval;
	struct ATENINTL_serial *ATEN2011_serial;
	int minor = port->serial->minor;
	ATEN2011_serial = ATEN2011_get_serial_private(port->serial);
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;

	//DPRINTK("application number is %4x \n",(((__u16)port->number - (__u16)(minor))+1)<<8);
	/*Wval  is same as application number */
	if (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 4) {
		Wval = (((__u16) port->number - (__u16) (minor)) + 1) << 8;
		DPRINTK("ATEN2011_get_Uart_Reg application number is %x\n",
			Wval);
	} else {
		if (((__u16) port->number - (__u16) (minor)) == 0) {
			//      Wval= 0x100;
			Wval =
			    (((__u16) port->number - (__u16) (minor)) + 1) << 8;
			DPRINTK
			    ("ATEN2011_get_Uart_Reg application number is %x\n",
			     Wval);
		} else {
			//      Wval=0x300;
			Wval =
			    (((__u16) port->number - (__u16) (minor)) + 2) << 8;
			DPRINTK
			    ("ATEN2011_get_Uart_Reg application number is %x\n",
			     Wval);
		}
	}
	ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), ATEN_RDREQ,
			      ATEN_RD_RTYPE, Wval, reg, val, VENDOR_READ_LENGTH,
			      ATEN_WDR_TIMEOUT);
	*val = (*val) & 0x00ff;
	return ret;
}

void ATEN2011_Dump_serial_port(struct ATENINTL_port *ATEN2011_port)
{

	DPRINTK("***************************************\n");
	DPRINTK("Application number is %4x\n", ATEN2011_port->AppNum);
	DPRINTK("SpRegOffset is %2x\n", ATEN2011_port->SpRegOffset);
	DPRINTK("ControlRegOffset is %2x \n", ATEN2011_port->ControlRegOffset);
	DPRINTK("DCRRegOffset is %2x \n", ATEN2011_port->DcrRegOffset);
	//DPRINTK("ClkSelectRegOffset is %2x \n",ATEN2011_port->ClkSelectRegOffset);
	DPRINTK("***************************************\n");

}

/* all structre defination goes here */
/****************************************************************************
 * ATENINTL2011_4port_device
 *              Structure defining ATEN2011, usb serial device
 ****************************************************************************/
static struct usb_serial_driver ATENINTL2011_4port_device = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "ATEN2011",
		   },
	.description = DRIVER_DESC,
	.id_table = ATENINTL_port_id_table,
	.open = ATEN2011_open,
	.close = ATEN2011_close,
	.write = ATEN2011_write,
	.write_room = ATEN2011_write_room,
	.chars_in_buffer = ATEN2011_chars_in_buffer,
	.throttle = ATEN2011_throttle,
	.unthrottle = ATEN2011_unthrottle,
	.calc_num_ports = ATEN2011_calc_num_ports,

#ifdef ATENSerialProbe
	.probe = ATEN2011_serial_probe,
#endif
	.ioctl = ATEN2011_ioctl,
	.set_termios = ATEN2011_set_termios,
	.break_ctl = ATEN2011_break,
//      .break_ctl              = ATEN2011_break_ctl,
	.tiocmget = ATEN2011_tiocmget,
	.tiocmset = ATEN2011_tiocmset,
	.attach = ATEN2011_startup,
	.shutdown = ATEN2011_shutdown,
	.read_bulk_callback = ATEN2011_bulk_in_callback,
	.read_int_callback = ATEN2011_interrupt_callback,
};

static struct usb_driver io_driver = {
	.name = "ATEN2011",
	.probe = usb_serial_probe,
	.disconnect = usb_serial_disconnect,
	.id_table = id_table_combined,
};

/************************************************************************/
/************************************************************************/
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/*            U S B  C A L L B A C K   F U N C T I O N S                */
/************************************************************************/
/************************************************************************/

/*****************************************************************************
 * ATEN2011_interrupt_callback
 *	this is the callback function for when we have received data on the
 *	interrupt endpoint.
 * Input : 1 Input
 *			pointer to the URB packet,
 *
 *****************************************************************************/
//#ifdef ATEN2011
static void ATEN2011_interrupt_callback(struct urb *urb)
{
	int result;
	int length;
	struct ATENINTL_port *ATEN2011_port;
	struct ATENINTL_serial *ATEN2011_serial;
	struct usb_serial *serial;
	__u16 Data;
	unsigned char *data;
	__u8 sp[5], st;
	int i;
	__u16 wval;
	int minor;
	//printk("in the function ATEN2011_interrupt_callback Length %d, Data %x \n",urb->actual_length,(unsigned int)urb->transfer_buffer);
	DPRINTK("%s", " : Entering\n");

	ATEN2011_serial = (struct ATENINTL_serial *)urb->context;
	if (!urb)		// || ATEN2011_serial->status_polling_started == FALSE )
	{
		DPRINTK("%s", "Invalid Pointer !!!!:\n");
		return;
	}

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__,
		    urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__,
		    urb->status);
		goto exit;
	}
	length = urb->actual_length;
	data = urb->transfer_buffer;

	//ATEN2011_serial= (struct ATENINTL_serial *)urb->context;
	//serial  = ATEN2011_get_usb_serial(port,__FUNCTION__);
	serial = ATEN2011_serial->serial;

	/* ATENINTL get 5 bytes
	 * Byte 1 IIR Port 1 (port.number is 0)
	 * Byte 2 IIR Port 2 (port.number is 1)
	 * Byte 3 IIR Port 3 (port.number is 2)
	 * Byte 4 IIR Port 4 (port.number is 3)
	 * Byte 5 FIFO status for both */

	if (length && length > 5) {
		DPRINTK("%s \n", "Wrong data !!!");
		return;
	}

	/* MATRIX */
	if (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 4) {
		sp[0] = (__u8) data[0];
		sp[1] = (__u8) data[1];
		sp[2] = (__u8) data[2];
		sp[3] = (__u8) data[3];
		st = (__u8) data[4];
	} else {
		sp[0] = (__u8) data[0];
		sp[1] = (__u8) data[2];
		//sp[2]=(__u8)data[2];
		//sp[3]=(__u8)data[3];
		st = (__u8) data[4];

	}
	//      printk("%s data is sp1:%x sp2:%x sp3:%x sp4:%x status:%x\n",__FUNCTION__,sp1,sp2,sp3,sp4,st);
	for (i = 0; i < serial->num_ports; i++) {
		ATEN2011_port = ATEN2011_get_port_private(serial->port[i]);
		minor = serial->minor;
		if (minor == SERIAL_TTY_NO_MINOR)
			minor = 0;
		if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
		    && (i != 0))
			wval =
			    (((__u16) serial->port[i]->number -
			      (__u16) (minor)) + 2) << 8;
		else
			wval =
			    (((__u16) serial->port[i]->number -
			      (__u16) (minor)) + 1) << 8;
		if (ATEN2011_port->open != FALSE) {
			//printk("%s wval is:(for 2011) %x\n",__FUNCTION__,wval);

			if (sp[i] & 0x01) {
				DPRINTK("SP%d No Interrupt !!!\n", i);
			} else {
				switch (sp[i] & 0x0f) {
				case SERIAL_IIR_RLS:
					DPRINTK
					    ("Serial Port %d: Receiver status error or ",
					     i);
					DPRINTK
					    ("address bit detected in 9-bit mode\n");
					ATEN2011_port->MsrLsr = 1;
					ATEN2011_get_reg(ATEN2011_port, wval,
							 LINE_STATUS_REGISTER,
							 &Data);
					break;
				case SERIAL_IIR_MS:
					DPRINTK
					    ("Serial Port %d: Modem status change\n",
					     i);
					ATEN2011_port->MsrLsr = 0;
					ATEN2011_get_reg(ATEN2011_port, wval,
							 MODEM_STATUS_REGISTER,
							 &Data);
					break;
				}
			}
		}

	}
      exit:
	if (ATEN2011_serial->status_polling_started == FALSE)
		return;

	result = usb_submit_urb(urb, GFP_ATOMIC);
	if (result) {
		dev_err(&urb->dev->dev,
			"%s - Error %d submitting interrupt urb\n",
			__FUNCTION__, result);
	}

	return;

}

//#endif
static void ATEN2011_control_callback(struct urb *urb)
{
	unsigned char *data;
	struct ATENINTL_port *ATEN2011_port;
	__u8 regval = 0x0;

	if (!urb) {
		DPRINTK("%s", "Invalid Pointer !!!!:\n");
		return;
	}

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		/* this urb is terminated, clean up */
		dbg("%s - urb shutting down with status: %d", __FUNCTION__,
		    urb->status);
		return;
	default:
		dbg("%s - nonzero urb status received: %d", __FUNCTION__,
		    urb->status);
		goto exit;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;

	DPRINTK("%s urb buffer size is %d\n", __FUNCTION__, urb->actual_length);
	DPRINTK("%s ATEN2011_port->MsrLsr is %d port %d\n", __FUNCTION__,
		ATEN2011_port->MsrLsr, ATEN2011_port->port_num);
	data = urb->transfer_buffer;
	regval = (__u8) data[0];
	DPRINTK("%s data is %x\n", __FUNCTION__, regval);
	if (ATEN2011_port->MsrLsr == 0)
		handle_newMsr(ATEN2011_port, regval);
	else if (ATEN2011_port->MsrLsr == 1)
		handle_newLsr(ATEN2011_port, regval);

      exit:
	return;
}
int handle_newMsr(struct ATENINTL_port *port, __u8 newMsr)
{
	struct ATENINTL_port *ATEN2011_port;
	struct async_icount *icount;
	ATEN2011_port = port;
	icount = &ATEN2011_port->icount;
	if (newMsr &
	    (ATEN_MSR_DELTA_CTS | ATEN_MSR_DELTA_DSR | ATEN_MSR_DELTA_RI |
	     ATEN_MSR_DELTA_CD)) {
		icount = &ATEN2011_port->icount;

		/* update input line counters */
		if (newMsr & ATEN_MSR_DELTA_CTS) {
			icount->cts++;
		}
		if (newMsr & ATEN_MSR_DELTA_DSR) {
			icount->dsr++;
		}
		if (newMsr & ATEN_MSR_DELTA_CD) {
			icount->dcd++;
		}
		if (newMsr & ATEN_MSR_DELTA_RI) {
			icount->rng++;
		}
	}

	return 0;
}
int handle_newLsr(struct ATENINTL_port *port, __u8 newLsr)
{
	struct async_icount *icount;

	dbg("%s - %02x", __FUNCTION__, newLsr);

	if (newLsr & SERIAL_LSR_BI) {
		//
		// Parity and Framing errors only count if they
		// occur exclusive of a break being
		// received.
		//
		newLsr &= (__u8) (SERIAL_LSR_OE | SERIAL_LSR_BI);
	}

	/* update input line counters */
	icount = &port->icount;
	if (newLsr & SERIAL_LSR_BI) {
		icount->brk++;
	}
	if (newLsr & SERIAL_LSR_OE) {
		icount->overrun++;
	}
	if (newLsr & SERIAL_LSR_PE) {
		icount->parity++;
	}
	if (newLsr & SERIAL_LSR_FE) {
		icount->frame++;
	}

	return 0;
}
static int ATEN2011_get_reg(struct ATENINTL_port *ATEN, __u16 Wval, __u16 reg,
			    __u16 * val)
{
	struct usb_device *dev = ATEN->port->serial->dev;
	struct usb_ctrlrequest *dr = NULL;
	unsigned char *buffer = NULL;
	int ret = 0;
	buffer = (__u8 *) ATEN->ctrl_buf;

//      dr=(struct usb_ctrlrequest *)(buffer);
	dr = (void *)(buffer + 2);
	dr->bRequestType = ATEN_RD_RTYPE;
	dr->bRequest = ATEN_RDREQ;
	dr->wValue = cpu_to_le16(Wval);	//0;
	dr->wIndex = cpu_to_le16(reg);
	dr->wLength = cpu_to_le16(2);

	usb_fill_control_urb(ATEN->control_urb, dev, usb_rcvctrlpipe(dev, 0),
			     (unsigned char *)dr, buffer, 2,
			     ATEN2011_control_callback, ATEN);
	ATEN->control_urb->transfer_buffer_length = 2;
	ret = usb_submit_urb(ATEN->control_urb, GFP_ATOMIC);
	return ret;
}

/*****************************************************************************
 * ATEN2011_bulk_in_callback
 *	this is the callback function for when we have received data on the
 *	bulk in endpoint.
 * Input : 1 Input
 *			pointer to the URB packet,
 *
 *****************************************************************************/
static void ATEN2011_bulk_in_callback(struct urb *urb)
{
	int status;
	unsigned char *data;
	struct usb_serial *serial;
	struct usb_serial_port *port;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct tty_struct *tty;
	if (!urb) {
		DPRINTK("%s", "Invalid Pointer !!!!:\n");
		return;
	}

	if (urb->status) {
		DPRINTK("nonzero read bulk status received: %d", urb->status);
//              if(urb->status==84)
		//ThreadState=1;
		return;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;
	if (!ATEN2011_port) {
		DPRINTK("%s", "NULL ATEN2011_port pointer \n");
		return;
	}

	port = (struct usb_serial_port *)ATEN2011_port->port;
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	serial = ATEN2011_get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s\n", "Bad serial pointer ");
		return;
	}

	DPRINTK("%s\n", "Entering... \n");

	data = urb->transfer_buffer;
	ATEN2011_serial = ATEN2011_get_serial_private(serial);

	DPRINTK("%s", "Entering ........... \n");

	if (urb->actual_length) {
//MATRIX
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
		tty = tty_port_tty_get(&ATEN2011_port->port->port);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(2,6,27)
		tty = ATEN2011_port->port->port.tty;
#else
		tty = ATEN2011_port->port->tty;
#endif
		if (tty) {
			tty_buffer_request_room(tty, urb->actual_length);
			tty_insert_flip_string(tty, data, urb->actual_length);
			DPRINTK(" %s \n", data);
			tty_flip_buffer_push(tty);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
			tty_kref_put(tty);
#endif
		}

		ATEN2011_port->icount.rx += urb->actual_length;
		DPRINTK("ATEN2011_port->icount.rx is %d:\n",
			ATEN2011_port->icount.rx);
//MATRIX
	}

	if (!ATEN2011_port->read_urb) {
		DPRINTK("%s", "URB KILLED !!!\n");
		return;
	}

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;

		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);

		if (status) {
			DPRINTK
			    (" usb_submit_urb(read bulk) failed, status = %d",
			     status);
		}
	}
}

/*****************************************************************************
 * ATEN2011_bulk_out_data_callback
 *	this is the callback function for when we have finished sending serial data
 *	on the bulk out endpoint.
 * Input : 1 Input
 *			pointer to the URB packet,
 *
 *****************************************************************************/
static void ATEN2011_bulk_out_data_callback(struct urb *urb)
{
	struct ATENINTL_port *ATEN2011_port;
	struct tty_struct *tty;
	if (!urb) {
		DPRINTK("%s", "Invalid Pointer !!!!:\n");
		return;
	}

	if (urb->status) {
		DPRINTK("nonzero write bulk status received:%d\n", urb->status);
		return;
	}

	ATEN2011_port = (struct ATENINTL_port *)urb->context;
	if (!ATEN2011_port) {
		DPRINTK("%s", "NULL ATEN2011_port pointer \n");
		return;
	}

	if (ATEN2011_port_paranoia_check(ATEN2011_port->port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	DPRINTK("%s \n", "Entering .........");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
	tty = tty_port_tty_get(&ATEN2011_port->port->port);
#elif LINUX_VERSION_CODE == KERNEL_VERSION(2,6,27)
	tty = ATEN2011_port->port->port.tty;
#else
	tty = ATEN2011_port->port->tty;
#endif

	if (tty && ATEN2011_port->open) {
		/* let the tty driver wakeup if it has a special *
		 * write_wakeup function                         */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
		    && tty->ldisc.write_wakeup) {
			(tty->ldisc.write_wakeup) (tty);
		}
#endif

		/* tell the tty driver that something has changed */
		wake_up_interruptible(&tty->write_wait);
	}

	/* Release the Write URB */
	ATEN2011_port->write_in_progress = FALSE;

//schedule_work(&ATEN2011_port->port->work);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,28)
	tty_kref_put(tty);
#endif

}

/************************************************************************/
/*       D R I V E R  T T Y  I N T E R F A C E  F U N C T I O N S       */
/************************************************************************/
#ifdef ATENSerialProbe
static int ATEN2011_serial_probe(struct usb_serial *serial,
				 const struct usb_device_id *id)
{

	/*need to implement the mode_reg reading and updating\
	   structures usb_serial_ device_type\
	   (i.e num_ports, num_bulkin,bulkout etc) */
	/* Also we can update the changes  attach */
	return 1;
}
#endif

/*****************************************************************************
 * SerialOpen
 *	this function is called by the tty driver when a port is opened
 *	If successful, we return 0
 *	Otherwise we return a negative error number.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_open(struct tty_struct *tty, struct usb_serial_port *port,
			 struct file *filp)
#else
static int ATEN2011_open(struct usb_serial_port *port, struct file *filp)
#endif
{
	int response;
	int j;
	struct usb_serial *serial;
//    struct usb_serial_port *port0;
	struct urb *urb;
	__u16 Data;
	int status;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct ktermios tmp_termios;
	int minor;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	struct tty_struct *tty = NULL;
#endif
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return -ENODEV;
	}
	//ATEN2011_serial->NoOfOpenPorts++;
	serial = port->serial;

	if (ATEN2011_serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return -ENODEV;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);

	if (ATEN2011_port == NULL)
		return -ENODEV;
/*
	if (ATEN2011_port->ctrl_buf==NULL)
        {
                ATEN2011_port->ctrl_buf = kmalloc(16,GFP_KERNEL);
                if (ATEN2011_port->ctrl_buf == NULL) {
                        printk(", Can't allocate ctrl buff\n");
                        return -ENOMEM;
                }

        }

	if(!ATEN2011_port->control_urb)
	{
	ATEN2011_port->control_urb=kmalloc(sizeof(struct urb),GFP_KERNEL);
	}
*/
//      port0 = serial->port[0];

	ATEN2011_serial = ATEN2011_get_serial_private(serial);

	if (ATEN2011_serial == NULL)	//|| port0 == NULL)
	{
		return -ENODEV;
	}
	// increment the number of opened ports counter here
	ATEN2011_serial->NoOfOpenPorts++;
	//printk("the num of ports opend is:%d\n",ATEN2011_serial->NoOfOpenPorts);

	usb_clear_halt(serial->dev, port->write_urb->pipe);
	usb_clear_halt(serial->dev, port->read_urb->pipe);

	/* Initialising the write urb pool */
	for (j = 0; j < NUM_URBS; ++j) {
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		ATEN2011_port->write_urb_pool[j] = urb;

		if (urb == NULL) {
			err("No more urbs???");
			continue;
		}

		urb->transfer_buffer = NULL;
		urb->transfer_buffer =
		    kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);
		if (!urb->transfer_buffer) {
			err("%s-out of memory for urb buffers.", __FUNCTION__);
			continue;
		}
	}

/*****************************************************************************
 * Initialize ATEN2011 -- Write Init values to corresponding Registers
 *
 * Register Index
 * 1 : IER
 * 2 : FCR
 * 3 : LCR
 * 4 : MCR
 *
 * 0x08 : SP1/2 Control Reg
 *****************************************************************************/

//NEED to check the fallowing Block

	status = 0;
	Data = 0x0;
	status = ATEN2011_get_reg_sync(port, ATEN2011_port->SpRegOffset, &Data);
	if (status < 0) {
		DPRINTK("Reading Spreg failed\n");
		return -1;
	}
	Data |= 0x80;
	status = ATEN2011_set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	if (status < 0) {
		DPRINTK("writing Spreg failed\n");
		return -1;
	}

	Data &= ~0x80;
	status = ATEN2011_set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	if (status < 0) {
		DPRINTK("writing Spreg failed\n");
		return -1;
	}

//End of block to be checked
//**************************CHECK***************************//

	if (RS485mode == 0)
		Data = 0xC0;
	else
		Data = 0x00;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, SCRATCH_PAD_REGISTER, Data);
	if (status < 0) {
		DPRINTK("Writing SCRATCH_PAD_REGISTER failed status-0x%x\n",
			status);
		return -1;
	} else
		DPRINTK("SCRATCH_PAD_REGISTER Writing success status%d\n",
			status);

//**************************CHECK***************************//

	status = 0;
	Data = 0x0;
	status =
	    ATEN2011_get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	if (status < 0) {
		DPRINTK("Reading Controlreg failed\n");
		return -1;
	}
	Data |= 0x08;		//Driver done bit
	/*
	   status = ATEN2011_set_reg_sync(port,ATEN2011_port->ControlRegOffset,Data);
	   if(status<0){
	   DPRINTK("writing Controlreg failed\n");
	   return -1;
	   }
	 */
	Data |= 0x20;		//rx_disable
	status = 0;
	status =
	    ATEN2011_set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);
	if (status < 0) {
		DPRINTK("writing Controlreg failed\n");
		return -1;
	}
	//do register settings here
	// Set all regs to the device default values.
	////////////////////////////////////
	// First Disable all interrupts.
	////////////////////////////////////

	Data = 0x00;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, INTERRUPT_ENABLE_REGISTER, Data);
	if (status < 0) {
		DPRINTK("disableing interrupts failed\n");
		return -1;
	}
	// Set FIFO_CONTROL_REGISTER to the default value
	Data = 0x00;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		DPRINTK("Writing FIFO_CONTROL_REGISTER  failed\n");
		return -1;
	}

	Data = 0xcf;		//chk
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, FIFO_CONTROL_REGISTER, Data);
	if (status < 0) {
		DPRINTK("Writing FIFO_CONTROL_REGISTER  failed\n");
		return -1;
	}

	Data = 0x03;		//LCR_BITS_8
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowLCR = Data;

	Data = 0x0b;		// MCR_DTR|MCR_RTS|MCR_MASTER_IE
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowMCR = Data;

#ifdef Check
	Data = 0x00;
	status = 0;
	status = ATEN2011_get_Uart_Reg(port, LINE_CONTROL_REGISTER, &Data);
	ATEN2011_port->shadowLCR = Data;

	Data |= SERIAL_LCR_DLAB;	//data latch enable in LCR 0x80
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x0c;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, DIVISOR_LATCH_LSB, Data);

	Data = 0x0;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, DIVISOR_LATCH_MSB, Data);

	Data = 0x00;
	status = 0;
	status = ATEN2011_get_Uart_Reg(port, LINE_CONTROL_REGISTER, &Data);

//      Data = ATEN2011_port->shadowLCR; //data latch disable
	Data = Data & ~SERIAL_LCR_DLAB;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);
	ATEN2011_port->shadowLCR = Data;
#endif
	//clearing Bulkin and Bulkout Fifo
	Data = 0x0;
	status = 0;
	status = ATEN2011_get_reg_sync(port, ATEN2011_port->SpRegOffset, &Data);

	Data = Data | 0x0c;
	status = 0;
	status = ATEN2011_set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);

	Data = Data & ~0x0c;
	status = 0;
	status = ATEN2011_set_reg_sync(port, ATEN2011_port->SpRegOffset, Data);
	//Finally enable all interrupts
	Data = 0x0;
	Data = 0x0c;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	//clearing rx_disable
	Data = 0x0;
	status = 0;
	status =
	    ATEN2011_get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	Data = Data & ~0x20;
	status = 0;
	status =
	    ATEN2011_set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);

	// rx_negate
	Data = 0x0;
	status = 0;
	status =
	    ATEN2011_get_reg_sync(port, ATEN2011_port->ControlRegOffset, &Data);
	Data = Data | 0x10;
	status = 0;
	status =
	    ATEN2011_set_reg_sync(port, ATEN2011_port->ControlRegOffset, Data);

	/* force low_latency on so that our tty_push actually forces *
	 * the data through,otherwise it is scheduled, and with      *
	 * high data rates (like with OHCI) data can get lost.       */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = port->tty;
#endif
	if (tty)
		tty->low_latency = 1;
/*
	printk("port number is %d \n",port->number);
	printk("serial number is %d \n",port->serial->minor);
	printk("Bulkin endpoint is %d \n",port->bulk_in_endpointAddress);
	printk("BulkOut endpoint is %d \n",port->bulk_out_endpointAddress);
	printk("Interrupt endpoint is %d \n",port->interrupt_in_endpointAddress);
	printk("port's number in the device is %d\n",ATEN2011_port->port_num);
*/
////////////////////////
//#ifdef CheckStatusPipe
/* Check to see if we've set up our endpoint info yet    *
     * (can't set it up in ATEN2011_startup as the structures *
     * were not set up at that time.)                        */
	if (ATEN2011_serial->NoOfOpenPorts == 1) {
		// start the status polling here
		ATEN2011_serial->status_polling_started = TRUE;
		//if (ATEN2011_serial->interrupt_in_buffer == NULL)
		// {
		/* If not yet set, Set here */
		ATEN2011_serial->interrupt_in_buffer =
		    serial->port[0]->interrupt_in_buffer;
		ATEN2011_serial->interrupt_in_endpoint =
		    serial->port[0]->interrupt_in_endpointAddress;
		//printk(" interrupt endpoint:%d \n",ATEN2011_serial->interrupt_in_endpoint);
		ATEN2011_serial->interrupt_read_urb =
		    serial->port[0]->interrupt_in_urb;

		/* set up interrupt urb */
		usb_fill_int_urb(ATEN2011_serial->interrupt_read_urb,
				 serial->dev,
				 usb_rcvintpipe(serial->dev,
						ATEN2011_serial->
						interrupt_in_endpoint),
				 ATEN2011_serial->interrupt_in_buffer,
				 ATEN2011_serial->interrupt_read_urb->
				 transfer_buffer_length,
				 ATEN2011_interrupt_callback, ATEN2011_serial,
				 ATEN2011_serial->interrupt_read_urb->interval);

		/* start interrupt read for ATEN2011               *
		 * will continue as long as ATEN2011 is connected  */

		response =
		    usb_submit_urb(ATEN2011_serial->interrupt_read_urb,
				   GFP_KERNEL);
		if (response) {
			DPRINTK("%s - Error %d submitting interrupt urb",
				__FUNCTION__, response);
		}
		//      else
		// printk(" interrupt URB submitted\n");

		//}

	}
//#endif

///////////////////////
	/* see if we've set up our endpoint info yet   *
	 * (can't set it up in ATEN2011_startup as the  *
	 * structures were not set up at that time.)   */

	DPRINTK("port number is %d \n", port->number);
	DPRINTK("serial number is %d \n", port->serial->minor);
	DPRINTK("Bulkin endpoint is %d \n", port->bulk_in_endpointAddress);
	DPRINTK("BulkOut endpoint is %d \n", port->bulk_out_endpointAddress);
	DPRINTK("Interrupt endpoint is %d \n",
		port->interrupt_in_endpointAddress);
	DPRINTK("port's number in the device is %d\n", ATEN2011_port->port_num);
	ATEN2011_port->bulk_in_buffer = port->bulk_in_buffer;
	ATEN2011_port->bulk_in_endpoint = port->bulk_in_endpointAddress;
	ATEN2011_port->read_urb = port->read_urb;
	ATEN2011_port->bulk_out_endpoint = port->bulk_out_endpointAddress;

	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;

	/* set up our bulk in urb */
	if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
	    && (((__u16) port->number - (__u16) (minor)) != 0)) {
		usb_fill_bulk_urb(ATEN2011_port->read_urb, serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  (port->
						   bulk_in_endpointAddress +
						   2)), port->bulk_in_buffer,
				  ATEN2011_port->read_urb->
				  transfer_buffer_length,
				  ATEN2011_bulk_in_callback, ATEN2011_port);
	} else
		usb_fill_bulk_urb(ATEN2011_port->read_urb,
				  serial->dev,
				  usb_rcvbulkpipe(serial->dev,
						  port->
						  bulk_in_endpointAddress),
				  port->bulk_in_buffer,
				  ATEN2011_port->read_urb->
				  transfer_buffer_length,
				  ATEN2011_bulk_in_callback, ATEN2011_port);

	DPRINTK("ATEN2011_open: bulkin endpoint is %d\n",
		port->bulk_in_endpointAddress);
	response = usb_submit_urb(ATEN2011_port->read_urb, GFP_KERNEL);
	if (response) {
		err("%s - Error %d submitting control urb", __FUNCTION__,
		    response);
	}

	/* initialize our wait queues */
	init_waitqueue_head(&ATEN2011_port->wait_open);
	init_waitqueue_head(&ATEN2011_port->wait_chase);
	init_waitqueue_head(&ATEN2011_port->delta_msr_wait);
	init_waitqueue_head(&ATEN2011_port->wait_command);

	/* initialize our icount structure */
	memset(&(ATEN2011_port->icount), 0x00, sizeof(ATEN2011_port->icount));

	/* initialize our port settings */
	ATEN2011_port->shadowMCR = MCR_MASTER_IE;	/* Must set to enable ints! */
	ATEN2011_port->chaseResponsePending = FALSE;
	/* send a open port command */
	ATEN2011_port->openPending = FALSE;
	ATEN2011_port->open = TRUE;
	//ATEN2011_change_port_settings(ATEN2011_port,old_termios);
	/* Setup termios */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	ATEN2011_set_termios(tty, port, &tmp_termios);
#else
	ATEN2011_set_termios(port, &tmp_termios);
#endif
	ATEN2011_port->rxBytesAvail = 0x0;
	ATEN2011_port->icount.tx = 0;
	ATEN2011_port->icount.rx = 0;

	DPRINTK
	    ("\n\nusb_serial serial:%x       ATEN2011_port:%x\nATEN2011_serial:%x      usb_serial_port port:%x\n\n",
	     (unsigned int)serial, (unsigned int)ATEN2011_port,
	     (unsigned int)ATEN2011_serial, (unsigned int)port);

	return 0;

}

/*****************************************************************************
 * ATEN2011_close
 *	this function is called by the tty driver when a port is closed
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_close(struct tty_struct *tty, struct usb_serial_port *port,
			   struct file *filp)
#else
static void ATEN2011_close(struct usb_serial_port *port, struct file *filp)
#endif
{
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	int no_urbs;
	__u16 Data;
	//__u16   Data1= 20;

	DPRINTK("%s\n", "ATEN2011_close:entering...");
	/* MATRIX  */
	//ThreadState = 1;
	/* MATRIX  */
	//printk("Entering... :ATEN2011_close\n");
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}
	serial = ATEN2011_get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return;
	}
	// take the Adpater and port's private data
	ATEN2011_serial = ATEN2011_get_serial_private(serial);
	ATEN2011_port = ATEN2011_get_port_private(port);
	if ((ATEN2011_serial == NULL) || (ATEN2011_port == NULL)) {
		return;
	}
	if (serial->dev) {
		/* flush and block(wait) until tx is empty */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		ATEN2011_block_until_tx_empty(tty, ATEN2011_port);
#else
		ATEN2011_block_until_tx_empty(ATEN2011_port);
#endif
	}
	// kill the ports URB's
	for (no_urbs = 0; no_urbs < NUM_URBS; no_urbs++)
		usb_kill_urb(ATEN2011_port->write_urb_pool[no_urbs]);
	/* Freeing Write URBs */
	for (no_urbs = 0; no_urbs < NUM_URBS; ++no_urbs) {
		if (ATEN2011_port->write_urb_pool[no_urbs]) {
			if (ATEN2011_port->write_urb_pool[no_urbs]->
			    transfer_buffer)
				kfree(ATEN2011_port->write_urb_pool[no_urbs]->
				      transfer_buffer);
			usb_free_urb(ATEN2011_port->write_urb_pool[no_urbs]);
		}
	}
	/* While closing port, shutdown all bulk read, write  *
	 * and interrupt read if they exists                  */
	if (serial->dev) {
		if (ATEN2011_port->write_urb) {
			DPRINTK("%s", "Shutdown bulk write\n");
			usb_kill_urb(ATEN2011_port->write_urb);
		}
		if (ATEN2011_port->read_urb) {
			DPRINTK("%s", "Shutdown bulk read\n");
			usb_kill_urb(ATEN2011_port->read_urb);
		}
		if ((&ATEN2011_port->control_urb)) {
			DPRINTK("%s", "Shutdown control read\n");
			//      usb_kill_urb (ATEN2011_port->control_urb);

		}
	}
	//if(ATEN2011_port->ctrl_buf != NULL)
	//kfree(ATEN2011_port->ctrl_buf);
	// decrement the no.of open ports counter of an individual USB-serial adapter.
	ATEN2011_serial->NoOfOpenPorts--;
	DPRINTK("NoOfOpenPorts in close%d:in port%d\n",
		ATEN2011_serial->NoOfOpenPorts, port->number);
	//printk("the num of ports opend is:%d\n",ATEN2011_serial->NoOfOpenPorts);
	if (ATEN2011_serial->NoOfOpenPorts == 0) {
		//stop the stus polling here
		//printk("disabling the status polling flag to FALSE :\n");
		ATEN2011_serial->status_polling_started = FALSE;
		if (ATEN2011_serial->interrupt_read_urb) {
			DPRINTK("%s", "Shutdown interrupt_read_urb\n");
			//ATEN2011_serial->interrupt_in_buffer=NULL;
			//usb_kill_urb (ATEN2011_serial->interrupt_read_urb);
		}
	}
	if (ATEN2011_port->write_urb) {
		/* if this urb had a transfer buffer already (old tx) free it */
		if (ATEN2011_port->write_urb->transfer_buffer != NULL) {
			kfree(ATEN2011_port->write_urb->transfer_buffer);
		}
		usb_free_urb(ATEN2011_port->write_urb);
	}
	// clear the MCR & IER
	Data = 0x00;
	ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
	Data = 0x00;
	ATEN2011_set_Uart_Reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	//ATEN2011_get_Uart_Reg(port,MODEM_CONTROL_REGISTER,&Data1);
	//printk("value of MCR after closing the port is : 0x%x\n",Data1);

	ATEN2011_port->open = FALSE;
	ATEN2011_port->closePending = FALSE;
	ATEN2011_port->openPending = FALSE;
	DPRINTK("%s \n", "Leaving ............");

}

/*****************************************************************************
 * SerialBreak
 *	this function sends a break to the port
 *****************************************************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_break(struct tty_struct *tty, int break_state)
#else
static void ATEN2011_break(struct usb_serial_port *port, int break_state)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#endif
	unsigned char data;
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;

	DPRINTK("%s \n", "Entering ...........");
	DPRINTK("ATEN2011_break: Start\n");

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return;
	}

	serial = ATEN2011_get_usb_serial(port, __FUNCTION__);
	if (!serial) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return;
	}

	ATEN2011_serial = ATEN2011_get_serial_private(serial);
	ATEN2011_port = ATEN2011_get_port_private(port);

	if ((ATEN2011_serial == NULL) || (ATEN2011_port == NULL)) {
		return;
	}

	/* flush and chase */
	ATEN2011_port->chaseResponsePending = TRUE;

	if (serial->dev) {

		/* flush and block until tx is empty */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		ATEN2011_block_until_chase_response(tty, ATEN2011_port);
#else
		ATEN2011_block_until_chase_response(ATEN2011_port);
#endif
	}

	if (break_state == -1) {
		data = ATEN2011_port->shadowLCR | LCR_SET_BREAK;
	} else {
		data = ATEN2011_port->shadowLCR & ~LCR_SET_BREAK;
	}

	ATEN2011_port->shadowLCR = data;
	DPRINTK("ATEN2011_break ATEN2011_port->shadowLCR is %x\n",
		ATEN2011_port->shadowLCR);
	ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER,
			      ATEN2011_port->shadowLCR);

	return;
}

/************************************************************************
 *
 * ATEN2011_block_until_chase_response
 *
 *	This function will block the close until one of the following:
 *		1. Response to our Chase comes from ATEN2011
 *		2. A timout of 10 seconds without activity has expired
 *		   (1K of ATEN2011 data @ 2400 baud ==> 4 sec to empty)
 *
 ************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_block_until_chase_response(struct tty_struct *tty,
						struct ATENINTL_port
						*ATEN2011_port)
#else
static void ATEN2011_block_until_chase_response(struct ATENINTL_port
						*ATEN2011_port)
#endif
{
	int timeout = 1 * HZ;
	int wait = 10;
	int count;

	while (1) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		count = ATEN2011_chars_in_buffer(tty);
#else
		count = ATEN2011_chars_in_buffer(ATEN2011_port->port);
#endif

		/* Check for Buffer status */
		if (count <= 0) {
			ATEN2011_port->chaseResponsePending = FALSE;
			return;
		}

		/* Block the thread for a while */
		interruptible_sleep_on_timeout(&ATEN2011_port->wait_chase,
					       timeout);
		/* No activity.. count down section */
		wait--;
		if (wait == 0) {
			dbg("%s - TIMEOUT", __FUNCTION__);
			return;
		} else {
			/* Reset timout value back to seconds */
			wait = 10;
		}
	}

}

/************************************************************************
 *
 * ATEN2011_block_until_tx_empty
 *
 *	This function will block the close until one of the following:
 *		1. TX count are 0
 *		2. The ATEN2011 has stopped
 *		3. A timout of 3 seconds without activity has expired
 *
 ************************************************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_block_until_tx_empty(struct tty_struct *tty,
					  struct ATENINTL_port *ATEN2011_port)
#else
static void ATEN2011_block_until_tx_empty(struct ATENINTL_port *ATEN2011_port)
#endif
{
	int timeout = HZ / 10;
	int wait = 30;
	int count;

	while (1) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		count = ATEN2011_chars_in_buffer(tty);
#else
		count = ATEN2011_chars_in_buffer(ATEN2011_port->port);
#endif

		/* Check for Buffer status */
		if (count <= 0) {
			return;
		}

		/* Block the thread for a while */
		interruptible_sleep_on_timeout(&ATEN2011_port->wait_chase,
					       timeout);

		/* No activity.. count down section */
		wait--;
		if (wait == 0) {
			dbg("%s - TIMEOUT", __FUNCTION__);
			return;
		} else {
			/* Reset timout value back to seconds */
			wait = 30;
		}
	}
}

/*****************************************************************************
 * ATEN2011_write_room
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we can accept for a specific port.
 *	If successful, we return the amount of room that we have for this port
 *	Otherwise we return a negative error number.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_write_room(struct tty_struct *tty)
#else
static int ATEN2011_write_room(struct usb_serial_port *port)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#endif
	int i;
	int room = 0;
	struct ATENINTL_port *ATEN2011_port;

//      DPRINTK("%s \n"," ATEN2011_write_room:entering ...........");

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		DPRINTK("%s \n", " ATEN2011_write_room:leaving ...........");
		return -1;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);
	if (ATEN2011_port == NULL) {
		DPRINTK("%s \n", "ATEN2011_break:leaving ...........");
		return -1;
	}

	for (i = 0; i < NUM_URBS; ++i) {
		if (ATEN2011_port->write_urb_pool[i]->status != -EINPROGRESS) {
			room += URB_TRANSFER_BUFFER_SIZE;
		}
	}

	dbg("%s - returns %d", __FUNCTION__, room);
	return (room);

}

/*****************************************************************************
 * ATEN2011_chars_in_buffer
 *	this function is called by the tty driver when it wants to know how many
 *	bytes of data we currently have outstanding in the port (data that has
 *	been written, but hasn't made it out the port yet)
 *	If successful, we return the number of bytes left to be written in the
 *	system,
 *	Otherwise we return a negative error number.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_chars_in_buffer(struct tty_struct *tty)
#else
static int ATEN2011_chars_in_buffer(struct usb_serial_port *port)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#endif
	int i;
	int chars = 0;
	struct ATENINTL_port *ATEN2011_port;

	//DPRINTK("%s \n"," ATEN2011_chars_in_buffer:entering ...........");

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);
	if (ATEN2011_port == NULL) {
		DPRINTK("%s \n", "ATEN2011_break:leaving ...........");
		return -1;
	}

	for (i = 0; i < NUM_URBS; ++i) {
		if (ATEN2011_port->write_urb_pool[i]->status == -EINPROGRESS) {
			chars += URB_TRANSFER_BUFFER_SIZE;
		}
	}
	dbg("%s - returns %d", __FUNCTION__, chars);
	return (chars);

}

/*****************************************************************************
 * SerialWrite
 *	this function is called by the tty driver when data should be written to
 *	the port.
 *	If successful, we return the number of bytes written, otherwise we
 *      return a negative error number.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_write(struct tty_struct *tty, struct usb_serial_port *port,
			  const unsigned char *data, int count)
#else
static int ATEN2011_write(struct usb_serial_port *port,
			  const unsigned char *data, int count)
#endif
{
	int status;
	int i;
	int bytes_sent = 0;
	int transfer_size;
	int from_user = 0;
	int minor;

	struct ATENINTL_port *ATEN2011_port;
	struct usb_serial *serial;
	struct ATENINTL_serial *ATEN2011_serial;
	struct urb *urb;
	//__u16 Data;
	const unsigned char *current_position = data;
	unsigned char *data1;
	DPRINTK("%s \n", "entering ...........");
	//DPRINTK("ATEN2011_write: ATEN2011_port->shadowLCR is %x\n",ATEN2011_port->shadowLCR);

#ifdef NOTATEN2011
	Data = 0x00;
	status = 0;
	status = ATEN2011_get_Uart_Reg(port, LINE_CONTROL_REGISTER, &Data);
	ATEN2011_port->shadowLCR = Data;
	DPRINTK("ATEN2011_write: LINE_CONTROL_REGISTER is %x\n", Data);
	DPRINTK("ATEN2011_write: ATEN2011_port->shadowLCR is %x\n",
		ATEN2011_port->shadowLCR);

	//Data = 0x03;
	//status = ATEN2011_set_Uart_Reg(port,LINE_CONTROL_REGISTER,Data);
	//ATEN2011_port->shadowLCR=Data;//Need to add later

	Data |= SERIAL_LCR_DLAB;	//data latch enable in LCR 0x80
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);

	//Data = 0x0c;
	//status = ATEN2011_set_Uart_Reg(port,DIVISOR_LATCH_LSB,Data);
	Data = 0x00;
	status = 0;
	status = ATEN2011_get_Uart_Reg(port, DIVISOR_LATCH_LSB, &Data);
	DPRINTK("ATEN2011_write:DLL value is %x\n", Data);

	Data = 0x0;
	status = 0;
	status = ATEN2011_get_Uart_Reg(port, DIVISOR_LATCH_MSB, &Data);
	DPRINTK("ATEN2011_write:DLM value is %x\n", Data);

	Data = Data & ~SERIAL_LCR_DLAB;
	DPRINTK("ATEN2011_write: ATEN2011_port->shadowLCR is %x\n",
		ATEN2011_port->shadowLCR);
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);
#endif

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Port Paranoia failed \n");
		return -1;
	}

	serial = port->serial;
	if (ATEN2011_serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Serial Paranoia failed \n");
		return -1;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);
	if (ATEN2011_port == NULL) {
		DPRINTK("%s", "ATEN2011_port is NULL\n");
		return -1;
	}

	ATEN2011_serial = ATEN2011_get_serial_private(serial);
	if (ATEN2011_serial == NULL) {
		DPRINTK("%s", "ATEN2011_serial is NULL \n");
		return -1;
	}

	/* try to find a free urb in the list */
	urb = NULL;

	for (i = 0; i < NUM_URBS; ++i) {
		if (ATEN2011_port->write_urb_pool[i]->status != -EINPROGRESS) {
			urb = ATEN2011_port->write_urb_pool[i];
			DPRINTK("\nURB:%d", i);
			break;
		}
	}

	if (urb == NULL) {
		dbg("%s - no more free urbs", __FUNCTION__);
		goto exit;
	}

	if (urb->transfer_buffer == NULL) {
		urb->transfer_buffer =
		    kmalloc(URB_TRANSFER_BUFFER_SIZE, GFP_KERNEL);

		if (urb->transfer_buffer == NULL) {
			err("%s no more kernel memory...", __FUNCTION__);
			goto exit;
		}
	}
	transfer_size = min(count, URB_TRANSFER_BUFFER_SIZE);

	if (from_user) {
		if (copy_from_user
		    (urb->transfer_buffer, current_position, transfer_size)) {
			bytes_sent = -EFAULT;
			goto exit;
		}
	} else {
		memcpy(urb->transfer_buffer, current_position, transfer_size);
	}
	//usb_serial_debug_data (__FILE__, __FUNCTION__, transfer_size, urb->transfer_buffer);

	/* fill urb with data and submit  */
	minor = port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR) ;
	minor = 0;
	if ((ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)
	    && (((__u16) port->number - (__u16) (minor)) != 0)) {
		usb_fill_bulk_urb(urb, ATEN2011_serial->serial->dev,
				  usb_sndbulkpipe(ATEN2011_serial->serial->dev,
						  (port->
						   bulk_out_endpointAddress) +
						  2), urb->transfer_buffer,
				  transfer_size,
				  ATEN2011_bulk_out_data_callback,
				  ATEN2011_port);
	} else

		usb_fill_bulk_urb(urb,
				  ATEN2011_serial->serial->dev,
				  usb_sndbulkpipe(ATEN2011_serial->serial->dev,
						  port->
						  bulk_out_endpointAddress),
				  urb->transfer_buffer, transfer_size,
				  ATEN2011_bulk_out_data_callback,
				  ATEN2011_port);

	data1 = urb->transfer_buffer;
	DPRINTK("\nbulkout endpoint is %d", port->bulk_out_endpointAddress);
	//for(i=0;i < urb->actual_length;i++)
	//      DPRINTK("Data is %c\n ",data1[i]);

	/* send it down the pipe */
	status = usb_submit_urb(urb, GFP_ATOMIC);

	if (status) {
		err("%s - usb_submit_urb(write bulk) failed with status = %d",
		    __FUNCTION__, status);
		bytes_sent = status;
		goto exit;
	}
	bytes_sent = transfer_size;
	ATEN2011_port->icount.tx += transfer_size;
	DPRINTK("ATEN2011_port->icount.tx is %d:\n", ATEN2011_port->icount.tx);
      exit:

	return bytes_sent;

}

/*****************************************************************************
 * SerialThrottle
 *	this function is called by the tty driver when it wants to stop the data
 *	being read from the port.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_throttle(struct tty_struct *tty)
#else
static void ATEN2011_throttle(struct usb_serial_port *port)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#else
	struct tty_struct *tty;
#endif
	struct ATENINTL_port *ATEN2011_port;
	int status;

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	DPRINTK("- port %d\n", port->number);

	ATEN2011_port = ATEN2011_get_port_private(port);

	if (ATEN2011_port == NULL)
		return;

	if (!ATEN2011_port->open) {
		DPRINTK("%s\n", "port not opened");
		return;
	}

	DPRINTK("%s", "Entering .......... \n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = port->tty;
#endif
	if (!tty) {
		dbg("%s - no tty available", __FUNCTION__);
		return;
	}

	/* if we are implementing XON/XOFF, send the stop character */
	if (I_IXOFF(tty)) {
		unsigned char stop_char = STOP_CHAR(tty);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		status = ATEN2011_write(tty, port, &stop_char, 1);	//FC4
#else
		status = ATEN2011_write(port, &stop_char, 1);	//FC4
#endif
		if (status <= 0) {
			return;
		}
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		ATEN2011_port->shadowMCR &= ~MCR_RTS;
		status = 0;
		status =
		    ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER,
					  ATEN2011_port->shadowMCR);

		if (status < 0) {
			return;
		}
	}

	return;
}

/*****************************************************************************
 * ATEN2011_unthrottle
 *	this function is called by the tty driver when it wants to resume the data
 *	being read from the port (called after SerialThrottle is called)
 *****************************************************************************/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_unthrottle(struct tty_struct *tty)
#else
static void ATEN2011_unthrottle(struct usb_serial_port *port)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#else
	struct tty_struct *tty;
#endif
	int status;
	struct ATENINTL_port *ATEN2011_port = ATEN2011_get_port_private(port);

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	if (ATEN2011_port == NULL)
		return;

	if (!ATEN2011_port->open) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}

	DPRINTK("%s", "Entering .......... \n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = port->tty;
#endif
	if (!tty) {
		dbg("%s - no tty available", __FUNCTION__);
		return;
	}

	/* if we are implementing XON/XOFF, send the start character */
	if (I_IXOFF(tty)) {
		unsigned char start_char = START_CHAR(tty);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		status = ATEN2011_write(tty, port, &start_char, 1);	//FC4
#else
		status = ATEN2011_write(port, &start_char, 1);	//FC4
#endif
		if (status <= 0) {
			return;
		}
	}

	/* if we are implementing RTS/CTS, toggle that line */
	if (tty->termios->c_cflag & CRTSCTS) {
		ATEN2011_port->shadowMCR |= MCR_RTS;
		status = 0;
		status =
		    ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER,
					  ATEN2011_port->shadowMCR);
		if (status < 0) {
			return;
		}
	}

	return;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_tiocmget(struct tty_struct *tty, struct file *file)
#else
static int ATEN2011_tiocmget(struct usb_serial_port *port, struct file *file)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#endif
	//struct ti_port *tport = usb_get_serial_port_data(port);
	struct ATENINTL_port *ATEN2011_port;
	unsigned int result;
	__u16 msr;
	__u16 mcr;
	//unsigned int mcr;
	int status = 0;
	ATEN2011_port = ATEN2011_get_port_private(port);

	DPRINTK("%s - port %d", __FUNCTION__, port->number);

	if (ATEN2011_port == NULL)
		return -ENODEV;

	status = ATEN2011_get_Uart_Reg(port, MODEM_STATUS_REGISTER, &msr);
	status = ATEN2011_get_Uart_Reg(port, MODEM_CONTROL_REGISTER, &mcr);
//        mcr = ATEN2011_port->shadowMCR;
// COMMENT2: the Fallowing three line are commented for updating only MSR values
	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)
	    | ((mcr & MCR_RTS) ? TIOCM_RTS : 0)
	    | ((mcr & MCR_LOOPBACK) ? TIOCM_LOOP : 0)
	    | ((msr & ATEN2011_MSR_CTS) ? TIOCM_CTS : 0)
	    | ((msr & ATEN2011_MSR_CD) ? TIOCM_CAR : 0)
	    | ((msr & ATEN2011_MSR_RI) ? TIOCM_RI : 0)
	    | ((msr & ATEN2011_MSR_DSR) ? TIOCM_DSR : 0);

	DPRINTK("%s - 0x%04X", __FUNCTION__, result);

	return result;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_tiocmset(struct tty_struct *tty, struct file *file,
			     unsigned int set, unsigned int clear)
#else
static int ATEN2011_tiocmset(struct usb_serial_port *port, struct file *file,
			     unsigned int set, unsigned int clear)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#endif
	struct ATENINTL_port *ATEN2011_port;
	//struct ti_port *tport = usb_get_serial_port_data(port);
	unsigned int mcr;
	unsigned int status;

	DPRINTK("%s - port %d", __FUNCTION__, port->number);

	ATEN2011_port = ATEN2011_get_port_private(port);

	if (ATEN2011_port == NULL)
		return -ENODEV;

	mcr = ATEN2011_port->shadowMCR;
	if (clear & TIOCM_RTS)
		mcr &= ~MCR_RTS;
	if (clear & TIOCM_DTR)
		mcr &= ~MCR_DTR;
	if (clear & TIOCM_LOOP)
		mcr &= ~MCR_LOOPBACK;

	if (set & TIOCM_RTS)
		mcr |= MCR_RTS;
	if (set & TIOCM_DTR)
		mcr |= MCR_DTR;
	if (set & TIOCM_LOOP)
		mcr |= MCR_LOOPBACK;

	ATEN2011_port->shadowMCR = mcr;

	status = 0;
	status = ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, mcr);
	if (status < 0) {
		DPRINTK("setting MODEM_CONTROL_REGISTER Failed\n");
		return -1;
	}

	return 0;
}

/*****************************************************************************
 * SerialSetTermios
 *	this function is called by the tty driver when it wants to change the termios structure
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_set_termios(struct tty_struct *tty,
				 struct usb_serial_port *port,
				 struct ktermios *old_termios)
#else
static void ATEN2011_set_termios(struct usb_serial_port *port,
				 struct ktermios *old_termios)
#endif
{
	int status;
	unsigned int cflag;
	struct usb_serial *serial;
	struct ATENINTL_port *ATEN2011_port;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	struct tty_struct *tty;
#endif
	DPRINTK("ATEN2011_set_termios: START\n");
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	serial = port->serial;

	if (ATEN2011_serial_paranoia_check(serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);

	if (ATEN2011_port == NULL)
		return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = port->tty;

	if (!port->tty || !port->tty->termios) {
		dbg("%s - no tty or termios", __FUNCTION__);
		return;
	}
#endif

	if (!ATEN2011_port->open) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}

	DPRINTK("%s\n", "setting termios - ");

	cflag = tty->termios->c_cflag;

	if (!cflag) {
		DPRINTK("%s %s\n", __FUNCTION__, "cflag is NULL");
		return;
	}

	/* check that they really want us to change something */
	if (old_termios) {
		if ((cflag == old_termios->c_cflag) &&
		    (RELEVANT_IFLAG(tty->termios->c_iflag) ==
		     RELEVANT_IFLAG(old_termios->c_iflag))) {
			DPRINTK("%s\n", "Nothing to change");
			return;
		}
	}

	dbg("%s - clfag %08x iflag %08x", __FUNCTION__,
	    tty->termios->c_cflag, RELEVANT_IFLAG(tty->termios->c_iflag));

	if (old_termios) {
		dbg("%s - old clfag %08x old iflag %08x", __FUNCTION__,
		    old_termios->c_cflag, RELEVANT_IFLAG(old_termios->c_iflag));
	}

	dbg("%s - port %d", __FUNCTION__, port->number);

	/* change the port settings to the new ones specified */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	ATEN2011_change_port_settings(tty, ATEN2011_port, old_termios);
#else
	ATEN2011_change_port_settings(ATEN2011_port, old_termios);
#endif

	if (!ATEN2011_port->read_urb) {
		DPRINTK("%s", "URB KILLED !!!!!\n");
		return;
	}

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;
		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);
		if (status) {
			DPRINTK
			    (" usb_submit_urb(read bulk) failed, status = %d",
			     status);
		}
	}
	return;
}

/*
static void ATEN2011_break_ctl( struct usb_serial_port *port, int break_state )
{
        //struct usb_serial *serial = port->serial;

//        if (BSA_USB_CMD(BELKIN_SA_SET_BREAK_REQUEST, break_state ? 1 : 0) < 0)
  //              err("Set break_ctl %d", break_state);
}
*/

/*****************************************************************************
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int get_lsr_info(struct tty_struct *tty,
			struct ATENINTL_port *ATEN2011_port,
			unsigned int *value)
#else
static int get_lsr_info(struct ATENINTL_port *ATEN2011_port,
			unsigned int *value)
#endif
{
	int count;
	unsigned int result = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	count = ATEN2011_chars_in_buffer(tty);
#else
	count = ATEN2011_chars_in_buffer(ATEN2011_port->port);
#endif
	if (count == 0) {
		dbg("%s -- Empty", __FUNCTION__);
		result = TIOCSER_TEMT;
	}

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

/*****************************************************************************
 * get_number_bytes_avail - get number of bytes available
 *
 * Purpose: Let user call ioctl to get the count of number of bytes available.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int get_number_bytes_avail(struct tty_struct *tty,
				  struct ATENINTL_port *ATEN2011_port,
				  unsigned int *value)
#else
static int get_number_bytes_avail(struct ATENINTL_port *ATEN2011_port,
				  unsigned int *value)
#endif
{
	unsigned int result = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	struct tty_struct *tty = ATEN2011_port->port->tty;
#endif

	if (!tty)
		return -ENOIOCTLCMD;

	result = tty->read_cnt;

	dbg("%s(%d) = %d", __FUNCTION__, ATEN2011_port->port->number, result);
	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;

	return -ENOIOCTLCMD;
}

/*****************************************************************************
 * set_modem_info
 *      function to set modem info
 *****************************************************************************/

static int set_modem_info(struct ATENINTL_port *ATEN2011_port, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int mcr;
	unsigned int arg;
	__u16 Data;
	int status;
	struct usb_serial_port *port;

	if (ATEN2011_port == NULL)
		return -1;

	port = (struct usb_serial_port *)ATEN2011_port->port;
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	mcr = ATEN2011_port->shadowMCR;

	if (copy_from_user(&arg, value, sizeof(int)))
		return -EFAULT;

	switch (cmd) {
	case TIOCMBIS:
		if (arg & TIOCM_RTS)
			mcr |= MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr |= MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr |= MCR_LOOPBACK;
		break;

	case TIOCMBIC:
		if (arg & TIOCM_RTS)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_DTR)
			mcr &= ~MCR_RTS;
		if (arg & TIOCM_LOOP)
			mcr &= ~MCR_LOOPBACK;
		break;

	case TIOCMSET:
		/* turn off the RTS and DTR and LOOPBACK
		 * and then only turn on what was asked to */
		mcr &= ~(MCR_RTS | MCR_DTR | MCR_LOOPBACK);
		mcr |= ((arg & TIOCM_RTS) ? MCR_RTS : 0);
		mcr |= ((arg & TIOCM_DTR) ? MCR_DTR : 0);
		mcr |= ((arg & TIOCM_LOOP) ? MCR_LOOPBACK : 0);
		break;
	}

	ATEN2011_port->shadowMCR = mcr;

	Data = ATEN2011_port->shadowMCR;
	status = 0;
	status = ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
	if (status < 0) {
		DPRINTK("setting MODEM_CONTROL_REGISTER Failed\n");
		return -1;
	}

	return 0;
}

/*****************************************************************************
 * get_modem_info
 *      function to get modem info
 *****************************************************************************/

static int get_modem_info(struct ATENINTL_port *ATEN2011_port,
			  unsigned int *value)
{
	unsigned int result = 0;
	__u16 msr;
	unsigned int mcr = ATEN2011_port->shadowMCR;
	int status = 0;
	status =
	    ATEN2011_get_Uart_Reg(ATEN2011_port->port, MODEM_STATUS_REGISTER,
				  &msr);
	result = ((mcr & MCR_DTR) ? TIOCM_DTR : 0)	/* 0x002 */
	    |((mcr & MCR_RTS) ? TIOCM_RTS : 0)	/* 0x004 */
	    |((msr & ATEN2011_MSR_CTS) ? TIOCM_CTS : 0)	/* 0x020 */
	    |((msr & ATEN2011_MSR_CD) ? TIOCM_CAR : 0)	/* 0x040 */
	    |((msr & ATEN2011_MSR_RI) ? TIOCM_RI : 0)	/* 0x080 */
	    |((msr & ATEN2011_MSR_DSR) ? TIOCM_DSR : 0);	/* 0x100 */

	dbg("%s -- %x", __FUNCTION__, result);

	if (copy_to_user(value, &result, sizeof(int)))
		return -EFAULT;
	return 0;
}

/*****************************************************************************
 * get_serial_info
 *      function to get information about serial port
 *****************************************************************************/

static int get_serial_info(struct ATENINTL_port *ATEN2011_port,
			   struct serial_struct *retinfo)
{
	struct serial_struct tmp;

	if (ATEN2011_port == NULL)
		return -1;

	if (!retinfo)
		return -EFAULT;

	memset(&tmp, 0, sizeof(tmp));

	tmp.type = PORT_16550A;
	tmp.line = ATEN2011_port->port->serial->minor;
	if (tmp.line == SERIAL_TTY_NO_MINOR)
		tmp.line = 0;
	tmp.port = ATEN2011_port->port->number;
	tmp.irq = 0;
	tmp.flags = ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ;
	tmp.xmit_fifo_size = NUM_URBS * URB_TRANSFER_BUFFER_SIZE;
	tmp.baud_base = 9600;
	tmp.close_delay = 5 * HZ;
	tmp.closing_wait = 30 * HZ;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

/*****************************************************************************
 * SerialIoctl
 *	this function handles any ioctl calls to the driver
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static int ATEN2011_ioctl(struct tty_struct *tty, struct file *file,
			  unsigned int cmd, unsigned long arg)
#else
static int ATEN2011_ioctl(struct usb_serial_port *port, struct file *file,
			  unsigned int cmd, unsigned long arg)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
	struct usb_serial_port *port = tty->driver_data;
#else
	struct tty_struct *tty;
#endif
	struct ATENINTL_port *ATEN2011_port;
	struct async_icount cnow;
	struct async_icount cprev;
	struct serial_icounter_struct icount;
	int ATENret = 0;
	//int retval;
	//struct tty_ldisc *ld;

	//printk("%s - port %d, cmd = 0x%x\n", __FUNCTION__, port->number, cmd);
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	ATEN2011_port = ATEN2011_get_port_private(port);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = ATEN2011_port->port->tty;
#endif

	if (ATEN2011_port == NULL)
		return -1;

	dbg("%s - port %d, cmd = 0x%x", __FUNCTION__, port->number, cmd);

	switch (cmd) {
		/* return number of bytes available */

	case TIOCINQ:
		dbg("%s (%d) TIOCINQ", __FUNCTION__, port->number);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		return get_number_bytes_avail(tty, ATEN2011_port,
					      (unsigned int *)arg);
#else
		return get_number_bytes_avail(ATEN2011_port,
					      (unsigned int *)arg);
#endif
		break;

	case TIOCOUTQ:
		dbg("%s (%d) TIOCOUTQ", __FUNCTION__, port->number);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		return put_user(ATEN2011_chars_in_buffer(tty),
				(int __user *)arg);
#else
		return put_user(tty->driver->ops->chars_in_buffer ?
				tty->driver->ops->chars_in_buffer(tty) : 0,
				(int __user *)arg);
#endif
		break;

		/*  //2.6.17 block
		   case TCFLSH:
		   retval = tty_check_change(tty);
		   if (retval)
		   return retval;

		   ld = tty_ldisc_ref(tty);
		   switch (arg) {
		   case TCIFLUSH:
		   if (ld && ld->flush_buffer)
		   ld->flush_buffer(tty);
		   break;
		   case TCIOFLUSH:
		   if (ld && ld->flush_buffer)
		   ld->flush_buffer(tty);
		   // fall through
		   case TCOFLUSH:
		   if (tty->driver->flush_buffer)
		   tty->driver->flush_buffer(tty);
		   break;
		   default:
		   tty_ldisc_deref(ld);
		   return -EINVAL;
		   }
		   tty_ldisc_deref(ld);
		   return 0;
		 */
	case TIOCSERGETLSR:
		dbg("%s (%d) TIOCSERGETLSR", __FUNCTION__, port->number);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
		return get_lsr_info(tty, ATEN2011_port, (unsigned int *)arg);
#else
		return get_lsr_info(ATEN2011_port, (unsigned int *)arg);
#endif
		return 0;

	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMSET:
		dbg("%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET", __FUNCTION__,
		    port->number);
		//      printk("%s (%d) TIOCMSET/TIOCMBIC/TIOCMSET", __FUNCTION__,  port->number);
		ATENret =
		    set_modem_info(ATEN2011_port, cmd, (unsigned int *)arg);
		//              printk(" %s: ret:%d\n",__FUNCTION__,ATENret);
		return ATENret;

	case TIOCMGET:
		dbg("%s (%d) TIOCMGET", __FUNCTION__, port->number);
		return get_modem_info(ATEN2011_port, (unsigned int *)arg);

	case TIOCGSERIAL:
		dbg("%s (%d) TIOCGSERIAL", __FUNCTION__, port->number);
		return get_serial_info(ATEN2011_port,
				       (struct serial_struct *)arg);

	case TIOCSSERIAL:
		dbg("%s (%d) TIOCSSERIAL", __FUNCTION__, port->number);
		break;

	case TIOCMIWAIT:
		dbg("%s (%d) TIOCMIWAIT", __FUNCTION__, port->number);
		cprev = ATEN2011_port->icount;
		while (1) {
			//interruptible_sleep_on(&ATEN2011_port->delta_msr_wait);
			// ATEN2011_port->delta_msr_cond=0;
			//wait_event_interruptible(ATEN2011_port->delta_msr_wait,(ATEN2011_port->delta_msr_cond==1));

			/* see if a signal did it */
			if (signal_pending(current))
				return -ERESTARTSYS;
			cnow = ATEN2011_port->icount;
			if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
			    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
				return -EIO;	/* no change => error */
			if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
			    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
			    ((arg & TIOCM_CD) && (cnow.dcd != cprev.dcd)) ||
			    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				return 0;
			}
			cprev = cnow;
		}
		/* NOTREACHED */
		break;

	case TIOCGICOUNT:
		cnow = ATEN2011_port->icount;
		icount.cts = cnow.cts;
		icount.dsr = cnow.dsr;
		icount.rng = cnow.rng;
		icount.dcd = cnow.dcd;
		icount.rx = cnow.rx;
		icount.tx = cnow.tx;
		icount.frame = cnow.frame;
		icount.overrun = cnow.overrun;
		icount.parity = cnow.parity;
		icount.brk = cnow.brk;
		icount.buf_overrun = cnow.buf_overrun;

		dbg("%s (%d) TIOCGICOUNT RX=%d, TX=%d", __FUNCTION__,
		    port->number, icount.rx, icount.tx);
		if (copy_to_user((void *)arg, &icount, sizeof(icount)))
			return -EFAULT;
		return 0;

	case TIOCEXBAUD:
		return 0;
	default:
		break;
	}

	return -ENOIOCTLCMD;
}

/*****************************************************************************
 * ATEN2011_send_cmd_write_baud_rate
 *	this function sends the proper command to change the baud rate of the
 *	specified port.
 *****************************************************************************/

static int ATEN2011_send_cmd_write_baud_rate(struct ATENINTL_port
					     *ATEN2011_port, int baudRate)
{
	int divisor = 0;
	int status;
	__u16 Data;
	unsigned char number;
	__u16 clk_sel_val;
	struct usb_serial_port *port;
	int minor;

	if (ATEN2011_port == NULL)
		return -1;

	port = (struct usb_serial_port *)ATEN2011_port->port;
	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return -1;
	}

	if (ATEN2011_serial_paranoia_check(port->serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return -1;
	}

	DPRINTK("%s", "Entering .......... \n");

	minor = ATEN2011_port->port->serial->minor;
	if (minor == SERIAL_TTY_NO_MINOR)
		minor = 0;
	number = ATEN2011_port->port->number - minor;

	dbg("%s - port = %d, baud = %d", __FUNCTION__,
	    ATEN2011_port->port->number, baudRate);
	//reset clk_uart_sel in spregOffset
	if (baudRate > 115200) {
#ifdef HW_flow_control
		//NOTE: need to see the pther register to modify
		//setting h/w flow control bit to 1;
		status = 0;
		//Data = ATEN2011_port->shadowMCR ;
		Data = 0x2b;
		ATEN2011_port->shadowMCR = Data;
		status =
		    ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
		if (status < 0) {
			DPRINTK("Writing spreg failed in set_serial_baud\n");
			return -1;
		}
#endif

	} else {
#ifdef HW_flow_control
		//setting h/w flow control bit to 0;
		status = 0;
		//Data = ATEN2011_port->shadowMCR ;
		Data = 0xb;
		ATEN2011_port->shadowMCR = Data;
		status =
		    ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
		if (status < 0) {
			DPRINTK("Writing spreg failed in set_serial_baud\n");
			return -1;
		}
#endif

	}

	if (1)			//baudRate <= 115200)
	{
		clk_sel_val = 0x0;
		Data = 0x0;
		status = 0;
		status =
		    ATEN2011_calc_baud_rate_divisor(baudRate, &divisor,
						    &clk_sel_val);
		status =
		    ATEN2011_get_reg_sync(port, ATEN2011_port->SpRegOffset,
					  &Data);
		if (status < 0) {
			DPRINTK("reading spreg failed in set_serial_baud\n");
			return -1;
		}
		Data = (Data & 0x8f) | clk_sel_val;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(port, ATEN2011_port->SpRegOffset,
					  Data);
		if (status < 0) {
			DPRINTK("Writing spreg failed in set_serial_baud\n");
			return -1;
		}
		/* Calculate the Divisor */

		if (status) {
			err("%s - bad baud rate", __FUNCTION__);
			DPRINTK("%s\n", "bad baud rate");
			return status;
		}
		/* Enable access to divisor latch */
		Data = ATEN2011_port->shadowLCR | SERIAL_LCR_DLAB;
		ATEN2011_port->shadowLCR = Data;
		ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);

		/* Write the divisor */
		Data = LOW8(divisor);	//:  commented to test
		DPRINTK("set_serial_baud Value to write DLL is %x\n", Data);
		ATEN2011_set_Uart_Reg(port, DIVISOR_LATCH_LSB, Data);

		Data = HIGH8(divisor);	//:  commented to test
		DPRINTK("set_serial_baud Value to write DLM is %x\n", Data);
		ATEN2011_set_Uart_Reg(port, DIVISOR_LATCH_MSB, Data);

		/* Disable access to divisor latch */
		Data = ATEN2011_port->shadowLCR & ~SERIAL_LCR_DLAB;
		ATEN2011_port->shadowLCR = Data;
		ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);

	}

	return status;
}

/*****************************************************************************
 * ATEN2011_calc_baud_rate_divisor
 *	this function calculates the proper baud rate divisor for the specified
 *	baud rate.
 *****************************************************************************/
static int ATEN2011_calc_baud_rate_divisor(int baudRate, int *divisor,
					   __u16 * clk_sel_val)
{
	//int i;
	//__u16 custom,round1, round;

	dbg("%s - %d", __FUNCTION__, baudRate);

	if (baudRate <= 115200) {
		*divisor = 115200 / baudRate;
		*clk_sel_val = 0x0;
	}
	if ((baudRate > 115200) && (baudRate <= 230400)) {
		*divisor = 230400 / baudRate;
		*clk_sel_val = 0x10;
	} else if ((baudRate > 230400) && (baudRate <= 403200)) {
		*divisor = 403200 / baudRate;
		*clk_sel_val = 0x20;
	} else if ((baudRate > 403200) && (baudRate <= 460800)) {
		*divisor = 460800 / baudRate;
		*clk_sel_val = 0x30;
	} else if ((baudRate > 460800) && (baudRate <= 806400)) {
		*divisor = 806400 / baudRate;
		*clk_sel_val = 0x40;
	} else if ((baudRate > 806400) && (baudRate <= 921600)) {
		*divisor = 921600 / baudRate;
		*clk_sel_val = 0x50;
	} else if ((baudRate > 921600) && (baudRate <= 1572864)) {
		*divisor = 1572864 / baudRate;
		*clk_sel_val = 0x60;
	} else if ((baudRate > 1572864) && (baudRate <= 3145728)) {
		*divisor = 3145728 / baudRate;
		*clk_sel_val = 0x70;
	}
	return 0;

#ifdef NOTATEN2011

	for (i = 0; i < NUM_ENTRIES(ATEN2011_divisor_table); i++) {
		if (ATEN2011_divisor_table[i].BaudRate == baudrate) {
			*divisor = ATEN2011_divisor_table[i].Divisor;
			return 0;
		}
	}

	/* After trying for all the standard baud rates    *
	 * Try calculating the divisor for this baud rate  */

	if (baudrate > 75 && baudrate < 230400) {
		/* get the divisor */
		custom = (__u16) (230400L / baudrate);

		/* Check for round off */
		round1 = (__u16) (2304000L / baudrate);
		round = (__u16) (round1 - (custom * 10));
		if (round > 4) {
			custom++;
		}
		*divisor = custom;

		DPRINTK(" Baud %d = %d\n", baudrate, custom);
		return 0;
	}

	DPRINTK("%s\n", " Baud calculation Failed...");
	return -1;
#endif
}

/*****************************************************************************
 * ATEN2011_change_port_settings
 *	This routine is called to set the UART on the device to match
 *      the specified new settings.
 *****************************************************************************/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
static void ATEN2011_change_port_settings(struct tty_struct *tty,
					  struct ATENINTL_port *ATEN2011_port,
					  struct ktermios *old_termios)
#else
static void ATEN2011_change_port_settings(struct ATENINTL_port *ATEN2011_port,
					  struct ktermios *old_termios)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	struct tty_struct *tty;
#endif
	int baud;
	unsigned cflag;
	unsigned iflag;
	__u8 mask = 0xff;
	__u8 lData;
	__u8 lParity;
	__u8 lStop;
	int status;
	__u16 Data;
	struct usb_serial_port *port;
	struct usb_serial *serial;

	if (ATEN2011_port == NULL)
		return;

	port = (struct usb_serial_port *)ATEN2011_port->port;

	if (ATEN2011_port_paranoia_check(port, __FUNCTION__)) {
		DPRINTK("%s", "Invalid port \n");
		return;
	}

	if (ATEN2011_serial_paranoia_check(port->serial, __FUNCTION__)) {
		DPRINTK("%s", "Invalid Serial \n");
		return;
	}

	serial = port->serial;

	dbg("%s - port %d", __FUNCTION__, ATEN2011_port->port->number);

	if ((!ATEN2011_port->open) && (!ATEN2011_port->openPending)) {
		dbg("%s - port not opened", __FUNCTION__);
		return;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
	tty = ATEN2011_port->port->tty;
#endif

	if ((!tty) || (!tty->termios)) {
		dbg("%s - no tty structures", __FUNCTION__);
		return;
	}

	DPRINTK("%s", "Entering .......... \n");

	lData = LCR_BITS_8;
	lStop = LCR_STOP_1;
	lParity = LCR_PAR_NONE;

	cflag = tty->termios->c_cflag;
	iflag = tty->termios->c_iflag;

	/* Change the number of bits */

//COMMENT1: the below Line"if(cflag & CSIZE)" is added for the errors we get for serial loop data test i.e serial_loopback.pl -v
	//if(cflag & CSIZE)
	{
		switch (cflag & CSIZE) {
		case CS5:
			lData = LCR_BITS_5;
			mask = 0x1f;
			break;

		case CS6:
			lData = LCR_BITS_6;
			mask = 0x3f;
			break;

		case CS7:
			lData = LCR_BITS_7;
			mask = 0x7f;
			break;
		default:
		case CS8:
			lData = LCR_BITS_8;
			break;
		}
	}
	/* Change the Parity bit */
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			lParity = LCR_PAR_ODD;
			dbg("%s - parity = odd", __FUNCTION__);
		} else {
			lParity = LCR_PAR_EVEN;
			dbg("%s - parity = even", __FUNCTION__);
		}

	} else {
		dbg("%s - parity = none", __FUNCTION__);
	}

	if (cflag & CMSPAR) {
		lParity = lParity | 0x20;
	}

	/* Change the Stop bit */
	if (cflag & CSTOPB) {
		lStop = LCR_STOP_2;
		dbg("%s - stop bits = 2", __FUNCTION__);
	} else {
		lStop = LCR_STOP_1;
		dbg("%s - stop bits = 1", __FUNCTION__);
	}

	/* Update the LCR with the correct value */
	ATEN2011_port->shadowLCR &=
	    ~(LCR_BITS_MASK | LCR_STOP_MASK | LCR_PAR_MASK);
	ATEN2011_port->shadowLCR |= (lData | lParity | lStop);

	ATEN2011_port->validDataMask = mask;
	DPRINTK
	    ("ATEN2011_change_port_settings ATEN2011_port->shadowLCR is %x\n",
	     ATEN2011_port->shadowLCR);
	/* Disable Interrupts */
	Data = 0x00;
	ATEN2011_set_Uart_Reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	Data = 0x00;
	ATEN2011_set_Uart_Reg(port, FIFO_CONTROL_REGISTER, Data);

	Data = 0xcf;
	ATEN2011_set_Uart_Reg(port, FIFO_CONTROL_REGISTER, Data);

	/* Send the updated LCR value to the ATEN2011 */
	Data = ATEN2011_port->shadowLCR;

	ATEN2011_set_Uart_Reg(port, LINE_CONTROL_REGISTER, Data);

	Data = 0x00b;
	ATEN2011_port->shadowMCR = Data;
	ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);
	Data = 0x00b;
	ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);

	/* set up the MCR register and send it to the ATEN2011 */

	ATEN2011_port->shadowMCR = MCR_MASTER_IE;
	if (cflag & CBAUD) {
		ATEN2011_port->shadowMCR |= (MCR_DTR | MCR_RTS);
	}

	if (cflag & CRTSCTS) {
		ATEN2011_port->shadowMCR |= (MCR_XON_ANY);

	} else {
		ATEN2011_port->shadowMCR &= ~(MCR_XON_ANY);
	}

	Data = ATEN2011_port->shadowMCR;
	ATEN2011_set_Uart_Reg(port, MODEM_CONTROL_REGISTER, Data);

	/* Determine divisor based on baud rate */
	baud = tty_get_baud_rate(tty);

	if (!baud) {
		/* pick a default, any default... */
		DPRINTK("%s\n", "Picked default baud...");
		baud = 9600;
	}

	dbg("%s - baud rate = %d", __FUNCTION__, baud);
	status = ATEN2011_send_cmd_write_baud_rate(ATEN2011_port, baud);

	/* Enable Interrupts */
	Data = 0x0c;
	ATEN2011_set_Uart_Reg(port, INTERRUPT_ENABLE_REGISTER, Data);

	if (ATEN2011_port->read_urb->status != -EINPROGRESS) {
		ATEN2011_port->read_urb->dev = serial->dev;

		status = usb_submit_urb(ATEN2011_port->read_urb, GFP_ATOMIC);

		if (status) {
			DPRINTK
			    (" usb_submit_urb(read bulk) failed, status = %d",
			     status);
		}
	}
	//wake_up(&ATEN2011_port->delta_msr_wait);
	//ATEN2011_port->delta_msr_cond=1;
	DPRINTK
	    ("ATEN2011_change_port_settings ATEN2011_port->shadowLCR is End %x\n",
	     ATEN2011_port->shadowLCR);

	return;
}

static int ATEN2011_calc_num_ports(struct usb_serial *serial)
{

	__u16 Data = 0x00;
	int ret = 0;
	int ATEN2011_2or4ports;
	ret = usb_control_msg(serial->dev, usb_rcvctrlpipe(serial->dev, 0),
			      ATEN_RDREQ, ATEN_RD_RTYPE, 0, GPIO_REGISTER,
			      &Data, VENDOR_READ_LENGTH, ATEN_WDR_TIMEOUT);

	//printk("ATEN2011_calc_num_ports GPIO is %x\n",Data);

/* ghostgum: here is where the problem appears to bet */
/* Which of the following are needed? */
/* Greg used the serial->type->num_ports=2 */
/* But the code in the ATEN2011_open relies on serial->num_ports=2 */
	if ((Data & 0x01) == 0) {
		ATEN2011_2or4ports = 2;
		serial->type->num_ports = 2;
		serial->num_ports = 2;
	}
	//else if(serial->interface->cur_altsetting->desc.bNumEndpoints == 9)
	else {
		ATEN2011_2or4ports = 4;
		serial->type->num_ports = 4;
		serial->num_ports = 4;

	}

	return ATEN2011_2or4ports;
}

/****************************************************************************
 * ATEN2011_startup
 ****************************************************************************/

static int ATEN2011_startup(struct usb_serial *serial)
{
	struct ATENINTL_serial *ATEN2011_serial;
	struct ATENINTL_port *ATEN2011_port;
	struct usb_device *dev;
	int i, status;
	int minor;

	__u16 Data;
	DPRINTK("%s \n", " ATEN2011_startup :entering..........");

	if (!serial) {
		DPRINTK("%s\n", "Invalid Handler");
		return -1;
	}

	dev = serial->dev;

	DPRINTK("%s\n", "Entering...");

	/* create our private serial structure */
	ATEN2011_serial = kzalloc(sizeof(struct ATENINTL_serial), GFP_KERNEL);
	if (ATEN2011_serial == NULL) {
		err("%s - Out of memory", __FUNCTION__);
		return -ENOMEM;
	}

	/* resetting the private structure field values to zero */
	memset(ATEN2011_serial, 0, sizeof(struct ATENINTL_serial));

	ATEN2011_serial->serial = serial;
	//initilize status polling flag to FALSE
	ATEN2011_serial->status_polling_started = FALSE;

	ATEN2011_set_serial_private(serial, ATEN2011_serial);
	ATEN2011_serial->ATEN2011_spectrum_2or4ports =
	    ATEN2011_calc_num_ports(serial);
	/* we set up the pointers to the endpoints in the ATEN2011_open *
	 * function, as the structures aren't created yet.             */

	/* set up port private structures */
	for (i = 0; i < serial->num_ports; ++i) {
		ATEN2011_port =
		    kmalloc(sizeof(struct ATENINTL_port), GFP_KERNEL);
		if (ATEN2011_port == NULL) {
			err("%s - Out of memory", __FUNCTION__);
			ATEN2011_set_serial_private(serial, NULL);
			kfree(ATEN2011_serial);
			return -ENOMEM;
		}
		memset(ATEN2011_port, 0, sizeof(struct ATENINTL_port));

		/* Initialize all port interrupt end point to port 0 int endpoint *
		 * Our device has only one interrupt end point comman to all port */

		//      serial->port[i]->interrupt_in_endpointAddress = serial->port[0]->interrupt_in_endpointAddress;

		ATEN2011_port->port = serial->port[i];
//
		ATEN2011_set_port_private(serial->port[i], ATEN2011_port);

		minor = serial->port[i]->serial->minor;
		if (minor == SERIAL_TTY_NO_MINOR)
			minor = 0;
		ATEN2011_port->port_num =
		    ((serial->port[i]->number - minor) + 1);

		ATEN2011_port->AppNum = (((__u16) serial->port[i]->number -
					  (__u16) (minor)) + 1) << 8;

		if (ATEN2011_port->port_num == 1) {
			ATEN2011_port->SpRegOffset = 0x0;
			ATEN2011_port->ControlRegOffset = 0x1;
			ATEN2011_port->DcrRegOffset = 0x4;
			//ATEN2011_port->ClkSelectRegOffset =  ;
		} else if ((ATEN2011_port->port_num == 2)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0x8;
			ATEN2011_port->ControlRegOffset = 0x9;
			ATEN2011_port->DcrRegOffset = 0x16;
			//ATEN2011_port->ClkSelectRegOffset =  ;
		} else if ((ATEN2011_port->port_num == 2)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       2)) {
			ATEN2011_port->SpRegOffset = 0xa;
			ATEN2011_port->ControlRegOffset = 0xb;
			ATEN2011_port->DcrRegOffset = 0x19;
			//ATEN2011_port->ClkSelectRegOffset =  ;
		} else if ((ATEN2011_port->port_num == 3)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0xa;
			ATEN2011_port->ControlRegOffset = 0xb;
			ATEN2011_port->DcrRegOffset = 0x19;
			//ATEN2011_port->ClkSelectRegOffset =  ;
		} else if ((ATEN2011_port->port_num == 4)
			   && (ATEN2011_serial->ATEN2011_spectrum_2or4ports ==
			       4)) {
			ATEN2011_port->SpRegOffset = 0xc;
			ATEN2011_port->ControlRegOffset = 0xd;
			ATEN2011_port->DcrRegOffset = 0x1c;
			//ATEN2011_port->ClkSelectRegOffset =  ;
		}
		ATEN2011_Dump_serial_port(ATEN2011_port);

		ATEN2011_set_port_private(serial->port[i], ATEN2011_port);

		//enable rx_disable bit in control register

		status =
		    ATEN2011_get_reg_sync(serial->port[i],
					  ATEN2011_port->ControlRegOffset,
					  &Data);
		if (status < 0) {
			DPRINTK("Reading ControlReg failed status-0x%x\n",
				status);
			break;
		} else
			DPRINTK
			    ("ControlReg Reading success val is %x, status%d\n",
			     Data, status);
		Data |= 0x08;	//setting driver done bit
		Data |= 0x04;	//sp1_bit to have cts change reflect in modem status reg

		//Data |= 0x20; //rx_disable bit
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i],
					  ATEN2011_port->ControlRegOffset,
					  Data);
		if (status < 0) {
			DPRINTK
			    ("Writing ControlReg failed(rx_disable) status-0x%x\n",
			     status);
			break;
		} else
			DPRINTK
			    ("ControlReg Writing success(rx_disable) status%d\n",
			     status);

		//Write default values in DCR (i.e 0x01 in DCR0, 0x05 in DCR2 and 0x24 in DCR3
		Data = 0x01;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i],
					  (__u16) (ATEN2011_port->DcrRegOffset +
						   0), Data);
		if (status < 0) {
			DPRINTK("Writing DCR0 failed status-0x%x\n", status);
			break;
		} else
			DPRINTK("DCR0 Writing success status%d\n", status);

		Data = 0x05;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i],
					  (__u16) (ATEN2011_port->DcrRegOffset +
						   1), Data);
		if (status < 0) {
			DPRINTK("Writing DCR1 failed status-0x%x\n", status);
			break;
		} else
			DPRINTK("DCR1 Writing success status%d\n", status);

		Data = 0x24;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i],
					  (__u16) (ATEN2011_port->DcrRegOffset +
						   2), Data);
		if (status < 0) {
			DPRINTK("Writing DCR2 failed status-0x%x\n", status);
			break;
		} else
			DPRINTK("DCR2 Writing success status%d\n", status);

		// write values in clkstart0x0 and clkmulti 0x20
		Data = 0x0;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i],
					  CLK_START_VALUE_REGISTER, Data);
		if (status < 0) {
			DPRINTK
			    ("Writing CLK_START_VALUE_REGISTER failed status-0x%x\n",
			     status);
			break;
		} else
			DPRINTK
			    ("CLK_START_VALUE_REGISTER Writing success status%d\n",
			     status);

		Data = 0x20;
		status = 0;
		status =
		    ATEN2011_set_reg_sync(serial->port[i], CLK_MULTI_REGISTER,
					  Data);
		if (status < 0) {
			DPRINTK
			    ("Writing CLK_MULTI_REGISTER failed status-0x%x\n",
			     status);
			break;
		} else
			DPRINTK("CLK_MULTI_REGISTER Writing success status%d\n",
				status);

		//write value 0x0 to scratchpad register
		/*
		   if(RS485mode==0)
		   Data = 0xC0;
		   else
		   Data = 0x00;
		   status=0;
		   status=ATEN2011_set_Uart_Reg(serial->port[i],SCRATCH_PAD_REGISTER,Data);
		   if(status<0) {
		   DPRINTK("Writing SCRATCH_PAD_REGISTER failed status-0x%x\n", status);
		   break;
		   }
		   else DPRINTK("SCRATCH_PAD_REGISTER Writing success status%d\n",status);
		 */

		/*
		   //Threshold Registers
		   if(ATEN2011_serial->ATEN2011_spectrum_2or4ports==4)
		   {
		   Data = 0x00;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   (__u16)(THRESHOLD_VAL_SP1_1+(__u16)ATEN2011_Thr_cnt),Data);
		   DPRINTK("THRESHOLD_VAL offset is%x\n", (__u16)(THRESHOLD_VAL_SP1_1+(__u16)ATEN2011_Thr_cnt));
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;
		   }
		   else DPRINTK("THRESHOLD_VAL Writing success status%d\n",status);
		   ATEN2011_Thr_cnt++;

		   Data = 0x01;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   (__u16)(THRESHOLD_VAL_SP1_1+(__u16)ATEN2011_Thr_cnt),Data);
		   DPRINTK("THRESHOLD_VAL offsetis%x\n",(__u16)(THRESHOLD_VAL_SP1_1+(__u16)ATEN2011_Thr_cnt));
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;
		   }
		   else DPRINTK("THRESHOLD_VAL Writing success status%d\n",status);
		   ATEN2011_Thr_cnt++;
		   }

		   else
		   {

		   if(ATEN2011_port->port_num==1)
		   {
		   Data = 0x00;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   0x3f,Data);
		   DPRINTK("THRESHOLD_VAL offset is 0x3f\n");
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;
		   }
		   Data = 0x01;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   0x40,Data);
		   DPRINTK("THRESHOLD_VAL offset is 0x40\n");
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;

		   }
		   }
		   else
		   {
		   Data = 0x00;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   0x43,Data);
		   DPRINTK("THRESHOLD_VAL offset is 0x43\n");
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;
		   }
		   Data = 0x01;
		   status=0;
		   status=ATEN2011_set_reg_sync(serial->port[i],\
		   0x44,Data);
		   DPRINTK("THRESHOLD_VAL offset is 0x44\n");
		   if(status<0) {
		   DPRINTK("Writing THRESHOLD_VAL failed status-0x%x\n",status);
		   break;

		   }

		   }

		   }
		 */
		//Zero Length flag register
		if ((ATEN2011_port->port_num != 1)
		    && (ATEN2011_serial->ATEN2011_spectrum_2or4ports == 2)) {

			Data = 0xff;
			status = 0;
			status = ATEN2011_set_reg_sync(serial->port[i],
						       (__u16) (ZLP_REG1 +
								((__u16)
								 ATEN2011_port->
								 port_num)),
						       Data);
			DPRINTK("ZLIP offset%x\n",
				(__u16) (ZLP_REG1 +
					 ((__u16) ATEN2011_port->port_num)));
			if (status < 0) {
				DPRINTK
				    ("Writing ZLP_REG%d failed status-0x%x\n",
				     i + 2, status);
				break;
			} else
				DPRINTK("ZLP_REG%d Writing success status%d\n",
					i + 2, status);
		} else {
			Data = 0xff;
			status = 0;
			status = ATEN2011_set_reg_sync(serial->port[i],
						       (__u16) (ZLP_REG1 +
								((__u16)
								 ATEN2011_port->
								 port_num) -
								0x1), Data);
			DPRINTK("ZLIP offset%x\n",
				(__u16) (ZLP_REG1 +
					 ((__u16) ATEN2011_port->port_num) -
					 0x1));
			if (status < 0) {
				DPRINTK
				    ("Writing ZLP_REG%d failed status-0x%x\n",
				     i + 1, status);
				break;
			} else
				DPRINTK("ZLP_REG%d Writing success status%d\n",
					i + 1, status);

		}
		ATEN2011_port->control_urb = usb_alloc_urb(0, GFP_ATOMIC);
		ATEN2011_port->ctrl_buf = kmalloc(16, GFP_KERNEL);

	}

	ATEN2011_Thr_cnt = 0;
	//Zero Length flag enable
	Data = 0x0f;
	status = 0;
	status = ATEN2011_set_reg_sync(serial->port[0], ZLP_REG5, Data);
	if (status < 0) {
		DPRINTK("Writing ZLP_REG5 failed status-0x%x\n", status);
		return -1;
	} else
		DPRINTK("ZLP_REG5 Writing success status%d\n", status);

	/* setting configuration feature to one */
	usb_control_msg(serial->dev, usb_sndctrlpipe(serial->dev, 0),
			(__u8) 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 5 * HZ);
	ATEN2011_Thr_cnt = 0;
	return 0;
}

/****************************************************************************
 * ATEN2011_shutdown
 *	This function is called whenever the device is removed from the usb bus.
 ****************************************************************************/

static void ATEN2011_shutdown(struct usb_serial *serial)
{
	int i;
	struct ATENINTL_port *ATEN2011_port;
	DPRINTK("%s \n", " shutdown :entering..........");

/* MATRIX  */
	//ThreadState = 1;
/* MATRIX  */

	if (!serial) {
		DPRINTK("%s", "Invalid Handler \n");
		return;
	}

	/*      check for the ports to be closed,close the ports and disconnect         */

	/* free private structure allocated for serial port  *
	 * stop reads and writes on all ports                */

	for (i = 0; i < serial->num_ports; ++i) {
		ATEN2011_port = ATEN2011_get_port_private(serial->port[i]);
		kfree(ATEN2011_port->ctrl_buf);
		usb_kill_urb(ATEN2011_port->control_urb);
		kfree(ATEN2011_port);
		ATEN2011_set_port_private(serial->port[i], NULL);
	}

	/* free private structure allocated for serial device */

	kfree(ATEN2011_get_serial_private(serial));
	ATEN2011_set_serial_private(serial, NULL);

	DPRINTK("%s\n", "Thank u :: ");

}

/* Inline functions to check the sanity of a pointer that is passed to us */
static int ATEN2011_serial_paranoia_check(struct usb_serial *serial,
					  const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL", function);
		return -1;
	}
//      if (serial->magic != USB_SERIAL_MAGIC) {
//              dbg("%s - bad magic number for serial", function);
//              return -1;
//      }
	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}
static int ATEN2011_port_paranoia_check(struct usb_serial_port *port,
					const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
//      if (port->magic != USB_SERIAL_PORT_MAGIC) {
//              dbg("%s - bad magic number for port", function);
//              return -1;
//      }
	if (!port->serial) {
		dbg("%s - port->serial == NULL", function);
		return -1;
	}

	return 0;
}
static struct usb_serial *ATEN2011_get_usb_serial(struct usb_serial_port *port,
						  const char *function)
{
	/* if no port was specified, or it fails a paranoia check */
	if (!port ||
	    ATEN2011_port_paranoia_check(port, function) ||
	    ATEN2011_serial_paranoia_check(port->serial, function)) {
		/* then say that we don't have a valid usb_serial thing, which will                  * end up genrating -ENODEV return values */
		return NULL;
	}

	return port->serial;
}

/****************************************************************************
 * ATENINTL2011_init
 *	This is called by the module subsystem, or on startup to initialize us
 ****************************************************************************/
int __init ATENINTL2011_init(void)
{
	int retval;

	DPRINTK("%s \n", " ATEN2011_init :entering..........");

	/* Register with the usb serial */
	retval = usb_serial_register(&ATENINTL2011_4port_device);

	if (retval)
		goto failed_port_device_register;

/*	info(DRIVER_DESC " " DRIVER_VERSION); */
	printk(KERN_INFO KBUILD_MODNAME ":"
	       DRIVER_DESC " " DRIVER_VERSION "\n");

	/* Register with the usb */
	retval = usb_register(&io_driver);

	if (retval)
		goto failed_usb_register;

	if (retval == 0) {
		DPRINTK("%s\n", "Leaving...");
		return 0;
	}

      failed_usb_register:
	usb_serial_deregister(&ATENINTL2011_4port_device);

      failed_port_device_register:

	return retval;
}

/****************************************************************************
 * ATENINTL2011_exit
 *	Called when the driver is about to be unloaded.
 ****************************************************************************/
void __exit ATENINTL2011_exit(void)
{

	DPRINTK("%s \n", " ATEN2011_exit :entering..........");

	usb_deregister(&io_driver);

	usb_serial_deregister(&ATENINTL2011_4port_device);

	DPRINTK("%s\n", "End...");
}

module_init(ATENINTL2011_init);
module_exit(ATENINTL2011_exit);

/* Module information */
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(debug, "Debug enabled or not");
