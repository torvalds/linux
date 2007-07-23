/*
*  Digi AccelePort USB-4 and USB-2 Serial Converters
*
*  Copyright 2000 by Digi International
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2 of the License, or
*  (at your option) any later version.
*
*  Shamelessly based on Brian Warner's keyspan_pda.c and Greg Kroah-Hartman's
*  usb-serial driver.
*
*  Peter Berger (pberger@brimson.com)
*  Al Borchers (borchers@steinerpoint.com)
* 
* (12/03/2001) gkh
*	switched to using port->open_count instead of private version.
*	Removed port->active
*
* (04/08/2001) gb
*	Identify version on module load.
*
* (11/01/2000) Adam J. Richter
*	usb_device_id table support
* 
* (11/01/2000) pberger and borchers
*    -- Turned off the USB_DISABLE_SPD flag for write bulk urbs--it caused
*       USB 4 ports to hang on startup.
*    -- Serialized access to write urbs by adding the dp_write_urb_in_use
*       flag; otherwise, the driver caused SMP system hangs.  Watching the
*       urb status is not sufficient.
*
* (10/05/2000) gkh
*    -- Fixed bug with urb->dev not being set properly, now that the usb
*	core needs it.
* 
*  (8/8/2000) pberger and borchers
*    -- Fixed close so that 
*       - it can timeout while waiting for transmit idle, if needed;
*       - it ignores interrupts when flushing the port, turning
*         of modem signalling, and so on;
*       - it waits for the flush to really complete before returning.
*    -- Read_bulk_callback and write_bulk_callback check for a closed
*       port before using the tty struct or writing to the port.
*    -- The two changes above fix the oops caused by interrupted closes.
*    -- Added interruptible args to write_oob_command and set_modem_signals
*       and added a timeout arg to transmit_idle; needed for fixes to
*       close.
*    -- Added code for rx_throttle and rx_unthrottle so that input flow
*       control works.
*    -- Added code to set overrun, parity, framing, and break errors
*       (untested).
*    -- Set USB_DISABLE_SPD flag for write bulk urbs, so no 0 length
*       bulk writes are done.  These hung the Digi USB device.  The
*       0 length bulk writes were a new feature of usb-uhci added in
*       the 2.4.0-test6 kernels.
*    -- Fixed mod inc race in open; do mod inc before sleeping to wait
*       for a close to finish.
*
*  (7/31/2000) pberger
*    -- Fixed bugs with hardware handshaking:
*       - Added code to set/clear tty->hw_stopped in digi_read_oob_callback()
*         and digi_set_termios()
*    -- Added code in digi_set_termios() to
*       - add conditional in code handling transition from B0 to only
*         set RTS if RTS/CTS flow control is either not in use or if
*         the port is not currently throttled.
*       - handle turning off CRTSCTS.
*
*  (7/30/2000) borchers
*    -- Added support for more than one Digi USB device by moving
*       globals to a private structure in the pointed to from the
*       usb_serial structure.
*    -- Moved the modem change and transmit idle wait queues into
*       the port private structure, so each port has its own queue
*       rather than sharing global queues.
*    -- Added support for break signals.
*
*  (7/25/2000) pberger
*    -- Added USB-2 support.  Note: the USB-2 supports 3 devices: two
*       serial and a parallel port.  The parallel port is implemented
*       as a serial-to-parallel converter.  That is, the driver actually
*       presents all three USB-2 interfaces as serial ports, but the third
*       one physically connects to a parallel device.  Thus, for example,
*       one could plug a parallel printer into the USB-2's third port,
*       but from the kernel's (and userland's) point of view what's
*       actually out there is a serial device.
*
*  (7/15/2000) borchers
*    -- Fixed race in open when a close is in progress.
*    -- Keep count of opens and dec the module use count for each
*       outstanding open when shutdown is called (on disconnect).
*    -- Fixed sanity checks in read_bulk_callback and write_bulk_callback
*       so pointers are checked before use.
*    -- Split read bulk callback into in band and out of band
*       callbacks, and no longer restart read chains if there is
*       a status error or a sanity error.  This fixed the seg
*       faults and other errors we used to get on disconnect.
*    -- Port->active is once again a flag as usb-serial intended it
*       to be, not a count.  Since it was only a char it would
*       have been limited to 256 simultaneous opens.  Now the open
*       count is kept in the port private structure in dp_open_count.
*    -- Added code for modularization of the digi_acceleport driver.
*
*  (6/27/2000) pberger and borchers
*    -- Zeroed out sync field in the wakeup_task before first use;
*       otherwise the uninitialized value might prevent the task from
*       being scheduled.
*    -- Initialized ret value to 0 in write_bulk_callback, otherwise
*       the uninitialized value could cause a spurious debugging message.
*
*  (6/22/2000) pberger and borchers
*    -- Made cond_wait_... inline--apparently on SPARC the flags arg
*       to spin_lock_irqsave cannot be passed to another function
*       to call spin_unlock_irqrestore.  Thanks to Pauline Middelink.
*    -- In digi_set_modem_signals the inner nested spin locks use just
*       spin_lock() rather than spin_lock_irqsave().  The old code
*       mistakenly left interrupts off.  Thanks to Pauline Middelink.
*    -- copy_from_user (which can sleep) is no longer called while a
*       spinlock is held.  We copy to a local buffer before getting
*       the spinlock--don't like the extra copy but the code is simpler.
*    -- Printk and dbg are no longer called while a spin lock is held.
*
*  (6/4/2000) pberger and borchers
*    -- Replaced separate calls to spin_unlock_irqrestore and
*       interruptible_sleep_on_timeout with a new function
*       cond_wait_interruptible_timeout_irqrestore.  This eliminates
*       the race condition where the wake up could happen after
*       the unlock and before the sleep.
*    -- Close now waits for output to drain.
*    -- Open waits until any close in progress is finished.
*    -- All out of band responses are now processed, not just the
*       first in a USB packet.
*    -- Fixed a bug that prevented the driver from working when the
*       first Digi port was not the first USB serial port--the driver
*       was mistakenly using the external USB serial port number to
*       try to index into its internal ports.
*    -- Fixed an SMP bug -- write_bulk_callback is called directly from
*       an interrupt, so spin_lock_irqsave/spin_unlock_irqrestore are
*       needed for locks outside write_bulk_callback that are also
*       acquired by write_bulk_callback to prevent deadlocks.
*    -- Fixed support for select() by making digi_chars_in_buffer()
*       return 256 when -EINPROGRESS is set, as the line discipline
*       code in n_tty.c expects.
*    -- Fixed an include file ordering problem that prevented debugging
*       messages from working.
*    -- Fixed an intermittent timeout problem that caused writes to
*       sometimes get stuck on some machines on some kernels.  It turns
*       out in these circumstances write_chan() (in n_tty.c) was
*       asleep waiting for our wakeup call.  Even though we call
*       wake_up_interruptible() in digi_write_bulk_callback(), there is
*       a race condition that could cause the wakeup to fail: if our
*       wake_up_interruptible() call occurs between the time that our
*       driver write routine finishes and write_chan() sets current->state
*       to TASK_INTERRUPTIBLE, the effect of our wakeup setting the state
*       to TASK_RUNNING will be lost and write_chan's subsequent call to
*       schedule() will never return (unless it catches a signal).
*       This race condition occurs because write_bulk_callback() (and thus
*       the wakeup) are called asynchronously from an interrupt, rather than
*       from the scheduler.  We can avoid the race by calling the wakeup
*       from the scheduler queue and that's our fix:  Now, at the end of
*       write_bulk_callback() we queue up a wakeup call on the scheduler
*       task queue.  We still also invoke the wakeup directly since that
*       squeezes a bit more performance out of the driver, and any lost
*       race conditions will get cleaned up at the next scheduler run.
*
*       NOTE:  The problem also goes away if you comment out
*       the two code lines in write_chan() where current->state
*       is set to TASK_RUNNING just before calling driver.write() and to
*       TASK_INTERRUPTIBLE immediately afterwards.  This is why the
*       problem did not show up with the 2.2 kernels -- they do not
*       include that code.
*
*  (5/16/2000) pberger and borchers
*    -- Added timeouts to sleeps, to defend against lost wake ups.
*    -- Handle transition to/from B0 baud rate in digi_set_termios.
*
*  (5/13/2000) pberger and borchers
*    -- All commands now sent on out of band port, using
*       digi_write_oob_command.
*    -- Get modem control signals whenever they change, support TIOCMGET/
*       SET/BIS/BIC ioctls.
*    -- digi_set_termios now supports parity, word size, stop bits, and
*       receive enable.
*    -- Cleaned up open and close, use digi_set_termios and
*       digi_write_oob_command to set port parameters.
*    -- Added digi_startup_device to start read chains on all ports.
*    -- Write buffer is only used when count==1, to be sure put_char can
*       write a char (unless the buffer is full).
*
*  (5/10/2000) pberger and borchers
*    -- Added MOD_INC_USE_COUNT/MOD_DEC_USE_COUNT calls on open/close.
*    -- Fixed problem where the first incoming character is lost on
*       port opens after the first close on that port.  Now we keep
*       the read_urb chain open until shutdown.
*    -- Added more port conditioning calls in digi_open and digi_close.
*    -- Convert port->active to a use count so that we can deal with multiple
*       opens and closes properly.
*    -- Fixed some problems with the locking code.
*
*  (5/3/2000) pberger and borchers
*    -- First alpha version of the driver--many known limitations and bugs.
*
*
*  Locking and SMP
*
*  - Each port, including the out-of-band port, has a lock used to
*    serialize all access to the port's private structure.
*  - The port lock is also used to serialize all writes and access to
*    the port's URB.
*  - The port lock is also used for the port write_wait condition
*    variable.  Holding the port lock will prevent a wake up on the
*    port's write_wait; this can be used with cond_wait_... to be sure
*    the wake up is not lost in a race when dropping the lock and
*    sleeping waiting for the wakeup.
*  - digi_write() does not sleep, since it is sometimes called on
*    interrupt time.
*  - digi_write_bulk_callback() and digi_read_bulk_callback() are
*    called directly from interrupts.  Hence spin_lock_irqsave()
*    and spin_unlock_irqrestore() are used in the rest of the code
*    for any locks they acquire.
*  - digi_write_bulk_callback() gets the port lock before waking up
*    processes sleeping on the port write_wait.  It also schedules
*    wake ups so they happen from the scheduler, because the tty
*    system can miss wake ups from interrupts.
*  - All sleeps use a timeout of DIGI_RETRY_TIMEOUT before looping to
*    recheck the condition they are sleeping on.  This is defensive,
*    in case a wake up is lost.
*  - Following Documentation/DocBook/kernel-locking.pdf no spin locks
*    are held when calling copy_to/from_user or printk.
*    
*  $Id: digi_acceleport.c,v 1.80.1.2 2000/11/02 05:45:08 root Exp $
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <asm/uaccess.h>
#include <linux/usb.h>
#include <linux/wait.h>
#include <linux/usb/serial.h>

/* Defines */

/*
 * Version Information
 */
#define DRIVER_VERSION "v1.80.1.2"
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
	wait_queue_head_t dp_modem_change_wait;
	int dp_transmit_idle;
	wait_queue_head_t dp_transmit_idle_wait;
	int dp_throttled;
	int dp_throttle_restart;
	wait_queue_head_t dp_flush_wait;
	int dp_in_close;			/* close in progress */
	wait_queue_head_t dp_close_wait;	/* wait queue for close */
	struct work_struct dp_wakeup_work;
	struct usb_serial_port *dp_port;
};


/* Local Function Declarations */

static void digi_wakeup_write( struct usb_serial_port *port );
static void digi_wakeup_write_lock(struct work_struct *work);
static int digi_write_oob_command( struct usb_serial_port *port,
	unsigned char *buf, int count, int interruptible );
static int digi_write_inb_command( struct usb_serial_port *port,
	unsigned char *buf, int count, unsigned long timeout );
static int digi_set_modem_signals( struct usb_serial_port *port,
	unsigned int modem_signals, int interruptible );
static int digi_transmit_idle( struct usb_serial_port *port,
	unsigned long timeout );
static void digi_rx_throttle (struct usb_serial_port *port);
static void digi_rx_unthrottle (struct usb_serial_port *port);
static void digi_set_termios( struct usb_serial_port *port, 
	struct ktermios *old_termios );
static void digi_break_ctl( struct usb_serial_port *port, int break_state );
static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg );
static int digi_tiocmget( struct usb_serial_port *port, struct file *file );
static int digi_tiocmset( struct usb_serial_port *port, struct file *file,
	unsigned int set, unsigned int clear );
static int digi_write( struct usb_serial_port *port, const unsigned char *buf, int count );
static void digi_write_bulk_callback( struct urb *urb );
static int digi_write_room( struct usb_serial_port *port );
static int digi_chars_in_buffer( struct usb_serial_port *port );
static int digi_open( struct usb_serial_port *port, struct file *filp );
static void digi_close( struct usb_serial_port *port, struct file *filp );
static int digi_startup_device( struct usb_serial *serial );
static int digi_startup( struct usb_serial *serial );
static void digi_shutdown( struct usb_serial *serial );
static void digi_read_bulk_callback( struct urb *urb );
static int digi_read_inb_callback( struct urb *urb );
static int digi_read_oob_callback( struct urb *urb );


/* Statics */

static int debug;

static struct usb_device_id id_table_combined [] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_2_ID) },
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_4_ID) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_2 [] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_2_ID) },
	{ }						/* Terminating entry */
};

static struct usb_device_id id_table_4 [] = {
	{ USB_DEVICE(DIGI_VENDOR_ID, DIGI_4_ID) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, id_table_combined);

static struct usb_driver digi_driver = {
	.name =		"digi_acceleport",
	.probe =	usb_serial_probe,
	.disconnect =	usb_serial_disconnect,
	.id_table =	id_table_combined,
	.no_dynamic_id = 	1,
};


/* device info needed for the Digi serial converter */

static struct usb_serial_driver digi_acceleport_2_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"digi_2",
	},
	.description =			"Digi 2 port USB adapter",
	.usb_driver = 			&digi_driver,
	.id_table =			id_table_2,
	.num_interrupt_in =		0,
	.num_bulk_in =			4,
	.num_bulk_out =			4,
	.num_ports =			3,
	.open =				digi_open,
	.close =			digi_close,
	.write =			digi_write,
	.write_room =			digi_write_room,
	.write_bulk_callback = 		digi_write_bulk_callback,
	.read_bulk_callback =		digi_read_bulk_callback,
	.chars_in_buffer =		digi_chars_in_buffer,
	.throttle =			digi_rx_throttle,
	.unthrottle =			digi_rx_unthrottle,
	.ioctl =			digi_ioctl,
	.set_termios =			digi_set_termios,
	.break_ctl =			digi_break_ctl,
	.tiocmget =			digi_tiocmget,
	.tiocmset =			digi_tiocmset,
	.attach =			digi_startup,
	.shutdown =			digi_shutdown,
};

static struct usb_serial_driver digi_acceleport_4_device = {
	.driver = {
		.owner =		THIS_MODULE,
		.name =			"digi_4",
	},
	.description =			"Digi 4 port USB adapter",
	.usb_driver = 			&digi_driver,
	.id_table =			id_table_4,
	.num_interrupt_in =		0,
	.num_bulk_in =			5,
	.num_bulk_out =			5,
	.num_ports =			4,
	.open =				digi_open,
	.close =			digi_close,
	.write =			digi_write,
	.write_room =			digi_write_room,
	.write_bulk_callback = 		digi_write_bulk_callback,
	.read_bulk_callback =		digi_read_bulk_callback,
	.chars_in_buffer =		digi_chars_in_buffer,
	.throttle =			digi_rx_throttle,
	.unthrottle =			digi_rx_unthrottle,
	.ioctl =			digi_ioctl,
	.set_termios =			digi_set_termios,
	.break_ctl =			digi_break_ctl,
	.tiocmget =			digi_tiocmget,
	.tiocmset =			digi_tiocmset,
	.attach =			digi_startup,
	.shutdown =			digi_shutdown,
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

static inline long cond_wait_interruptible_timeout_irqrestore(
	wait_queue_head_t *q, long timeout,
	spinlock_t *lock, unsigned long flags )
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


	spin_lock_irqsave( &priv->dp_port_lock, flags );
	digi_wakeup_write( port );
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
}

static void digi_wakeup_write( struct usb_serial_port *port )
{
	tty_wakeup(port->tty);
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

static int digi_write_oob_command( struct usb_serial_port *port,
	unsigned char *buf, int count, int interruptible )
{

	int ret = 0;
	int len;
	struct usb_serial_port *oob_port = (struct usb_serial_port *)((struct digi_serial *)(usb_get_serial_data(port->serial)))->ds_oob_port;
	struct digi_port *oob_priv = usb_get_serial_port_data(oob_port);
	unsigned long flags = 0;


dbg( "digi_write_oob_command: TOP: port=%d, count=%d", oob_priv->dp_port_num, count );

	spin_lock_irqsave( &oob_priv->dp_port_lock, flags );

	while( count > 0 ) {

		while( oob_port->write_urb->status == -EINPROGRESS
		|| oob_priv->dp_write_urb_in_use ) {
			cond_wait_interruptible_timeout_irqrestore(
				&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
				&oob_priv->dp_port_lock, flags );
			if( interruptible && signal_pending(current) ) {
				return( -EINTR );
			}
			spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
		}

		/* len must be a multiple of 4, so commands are not split */
		len = min(count, oob_port->bulk_out_size );
		if( len > 4 )
			len &= ~3;

		memcpy( oob_port->write_urb->transfer_buffer, buf, len );
		oob_port->write_urb->transfer_buffer_length = len;
		oob_port->write_urb->dev = port->serial->dev;

		if( (ret=usb_submit_urb(oob_port->write_urb, GFP_ATOMIC)) == 0 ) {
			oob_priv->dp_write_urb_in_use = 1;
			count -= len;
			buf += len;
		}

	}

	spin_unlock_irqrestore( &oob_priv->dp_port_lock, flags );

	if( ret ) {
		err("%s: usb_submit_urb failed, ret=%d", __FUNCTION__,
			ret );
	}

	return( ret );

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

static int digi_write_inb_command( struct usb_serial_port *port,
	unsigned char *buf, int count, unsigned long timeout )
{

	int ret = 0;
	int len;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned long flags = 0;


dbg( "digi_write_inb_command: TOP: port=%d, count=%d", priv->dp_port_num,
count );

	if( timeout )
		timeout += jiffies;
	else
		timeout = ULONG_MAX;

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	while( count > 0 && ret == 0 ) {

		while( (port->write_urb->status == -EINPROGRESS
		|| priv->dp_write_urb_in_use) && time_before(jiffies, timeout)) {
			cond_wait_interruptible_timeout_irqrestore(
				&port->write_wait, DIGI_RETRY_TIMEOUT,
				&priv->dp_port_lock, flags );
			if( signal_pending(current) ) {
				return( -EINTR );
			}
			spin_lock_irqsave( &priv->dp_port_lock, flags );
		}

		/* len must be a multiple of 4 and small enough to */
		/* guarantee the write will send buffered data first, */
		/* so commands are in order with data and not split */
		len = min(count, port->bulk_out_size-2-priv->dp_out_buf_len );
		if( len > 4 )
			len &= ~3;

		/* write any buffered data first */
		if( priv->dp_out_buf_len > 0 ) {
			data[0] = DIGI_CMD_SEND_DATA;
			data[1] = priv->dp_out_buf_len;
			memcpy( data+2, priv->dp_out_buf,
				priv->dp_out_buf_len );
			memcpy( data+2+priv->dp_out_buf_len, buf, len );
			port->write_urb->transfer_buffer_length
				= priv->dp_out_buf_len+2+len;
		} else {
			memcpy( data, buf, len );
			port->write_urb->transfer_buffer_length = len;
		}
		port->write_urb->dev = port->serial->dev;

		if( (ret=usb_submit_urb(port->write_urb, GFP_ATOMIC)) == 0 ) {
			priv->dp_write_urb_in_use = 1;
			priv->dp_out_buf_len = 0;
			count -= len;
			buf += len;
		}

	}

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	if( ret ) {
		err("%s: usb_submit_urb failed, ret=%d, port=%d", __FUNCTION__,
		ret, priv->dp_port_num );
	}

	return( ret );

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

static int digi_set_modem_signals( struct usb_serial_port *port,
	unsigned int modem_signals, int interruptible )
{

	int ret;
	struct digi_port *port_priv = usb_get_serial_port_data(port);
	struct usb_serial_port *oob_port = (struct usb_serial_port *)((struct digi_serial *)(usb_get_serial_data(port->serial)))->ds_oob_port;
	struct digi_port *oob_priv = usb_get_serial_port_data(oob_port);
	unsigned char *data = oob_port->write_urb->transfer_buffer;
	unsigned long flags = 0;


dbg( "digi_set_modem_signals: TOP: port=%d, modem_signals=0x%x",
port_priv->dp_port_num, modem_signals );

	spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
	spin_lock( &port_priv->dp_port_lock );

	while( oob_port->write_urb->status == -EINPROGRESS
	|| oob_priv->dp_write_urb_in_use ) {
		spin_unlock( &port_priv->dp_port_lock );
		cond_wait_interruptible_timeout_irqrestore(
			&oob_port->write_wait, DIGI_RETRY_TIMEOUT,
			&oob_priv->dp_port_lock, flags );
		if( interruptible && signal_pending(current) ) {
			return( -EINTR );
		}
		spin_lock_irqsave( &oob_priv->dp_port_lock, flags );
		spin_lock( &port_priv->dp_port_lock );
	}

	data[0] = DIGI_CMD_SET_DTR_SIGNAL;
	data[1] = port_priv->dp_port_num;
	data[2] = (modem_signals&TIOCM_DTR) ?
		DIGI_DTR_ACTIVE : DIGI_DTR_INACTIVE;
	data[3] = 0;

	data[4] = DIGI_CMD_SET_RTS_SIGNAL;
	data[5] = port_priv->dp_port_num;
	data[6] = (modem_signals&TIOCM_RTS) ?
		DIGI_RTS_ACTIVE : DIGI_RTS_INACTIVE;
	data[7] = 0;

	oob_port->write_urb->transfer_buffer_length = 8;
	oob_port->write_urb->dev = port->serial->dev;

	if( (ret=usb_submit_urb(oob_port->write_urb, GFP_ATOMIC)) == 0 ) {
		oob_priv->dp_write_urb_in_use = 1;
		port_priv->dp_modem_signals =
			(port_priv->dp_modem_signals&~(TIOCM_DTR|TIOCM_RTS))
			| (modem_signals&(TIOCM_DTR|TIOCM_RTS));
	}

	spin_unlock( &port_priv->dp_port_lock );
	spin_unlock_irqrestore( &oob_priv->dp_port_lock, flags );

	if( ret ) {
		err("%s: usb_submit_urb failed, ret=%d", __FUNCTION__,
		ret );
	}

	return( ret );

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

static int digi_transmit_idle( struct usb_serial_port *port,
	unsigned long timeout )
{

	int ret;
	unsigned char buf[2];
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;


	spin_lock_irqsave( &priv->dp_port_lock, flags );
	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	buf[0] = DIGI_CMD_TRANSMIT_IDLE;
	buf[1] = 0;

	timeout += jiffies;

	if( (ret=digi_write_inb_command( port, buf, 2, timeout-jiffies )) != 0 )
		return( ret );

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	while( time_before(jiffies, timeout) && !priv->dp_transmit_idle ) {
		cond_wait_interruptible_timeout_irqrestore(
			&priv->dp_transmit_idle_wait, DIGI_RETRY_TIMEOUT,
			&priv->dp_port_lock, flags );
		if( signal_pending(current) ) {
			return( -EINTR );
		}
		spin_lock_irqsave( &priv->dp_port_lock, flags );
	}

	priv->dp_transmit_idle = 0;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	return( 0 );

}


static void digi_rx_throttle( struct usb_serial_port *port )
{

	unsigned long flags;
	struct digi_port *priv = usb_get_serial_port_data(port);


dbg( "digi_rx_throttle: TOP: port=%d", priv->dp_port_num );

	/* stop receiving characters by not resubmitting the read urb */
	spin_lock_irqsave( &priv->dp_port_lock, flags );
	priv->dp_throttled = 1;
	priv->dp_throttle_restart = 0;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

}


static void digi_rx_unthrottle( struct usb_serial_port *port )
{

	int ret = 0;
	unsigned long flags;
	struct digi_port *priv = usb_get_serial_port_data(port);

dbg( "digi_rx_unthrottle: TOP: port=%d", priv->dp_port_num );

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	/* turn throttle off */
	priv->dp_throttled = 0;
	priv->dp_throttle_restart = 0;

	/* restart read chain */
	if( priv->dp_throttle_restart ) {
		port->read_urb->dev = port->serial->dev;
		ret = usb_submit_urb( port->read_urb, GFP_ATOMIC );
	}

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	if( ret ) {
		err("%s: usb_submit_urb failed, ret=%d, port=%d", __FUNCTION__,
			ret, priv->dp_port_num );
	}

}


static void digi_set_termios( struct usb_serial_port *port, 
	struct ktermios *old_termios )
{

	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned int iflag = port->tty->termios->c_iflag;
	unsigned int cflag = port->tty->termios->c_cflag;
	unsigned int old_iflag = old_termios->c_iflag;
	unsigned int old_cflag = old_termios->c_cflag;
	unsigned char buf[32];
	unsigned int modem_signals;
	int arg,ret;
	int i = 0;


dbg( "digi_set_termios: TOP: port=%d, iflag=0x%x, old_iflag=0x%x, cflag=0x%x, old_cflag=0x%x", priv->dp_port_num, iflag, old_iflag, cflag, old_cflag );

	/* set baud rate */
	if( (cflag&CBAUD) != (old_cflag&CBAUD) ) {

		arg = -1;

		/* reassert DTR and (maybe) RTS on transition from B0 */
		if( (old_cflag&CBAUD) == B0 ) {
			/* don't set RTS if using hardware flow control */
			/* and throttling input */
			modem_signals = TIOCM_DTR;
			if( !(port->tty->termios->c_cflag & CRTSCTS) ||
			!test_bit(TTY_THROTTLED, &port->tty->flags) ) {
				modem_signals |= TIOCM_RTS;
			}
			digi_set_modem_signals( port, modem_signals, 1 );
		}

		switch( (cflag&CBAUD) ) {
			/* drop DTR and RTS on transition to B0 */
		case B0: digi_set_modem_signals( port, 0, 1 ); break;
		case B50: arg = DIGI_BAUD_50; break;
		case B75: arg = DIGI_BAUD_75; break;
		case B110: arg = DIGI_BAUD_110; break;
		case B150: arg = DIGI_BAUD_150; break;
		case B200: arg = DIGI_BAUD_200; break;
		case B300: arg = DIGI_BAUD_300; break;
		case B600: arg = DIGI_BAUD_600; break;
		case B1200: arg = DIGI_BAUD_1200; break;
		case B1800: arg = DIGI_BAUD_1800; break;
		case B2400: arg = DIGI_BAUD_2400; break;
		case B4800: arg = DIGI_BAUD_4800; break;
		case B9600: arg = DIGI_BAUD_9600; break;
		case B19200: arg = DIGI_BAUD_19200; break;
		case B38400: arg = DIGI_BAUD_38400; break;
		case B57600: arg = DIGI_BAUD_57600; break;
		case B115200: arg = DIGI_BAUD_115200; break;
		case B230400: arg = DIGI_BAUD_230400; break;
		case B460800: arg = DIGI_BAUD_460800; break;
		default:
			dbg( "digi_set_termios: can't handle baud rate 0x%x",
				(cflag&CBAUD) );
			break;
		}

		if( arg != -1 ) {
			buf[i++] = DIGI_CMD_SET_BAUD_RATE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}

	}

	/* set parity */
	if( (cflag&(PARENB|PARODD)) != (old_cflag&(PARENB|PARODD)) ) {

		if( (cflag&PARENB) ) {
			if( (cflag&PARODD) )
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
	if( (cflag&CSIZE) != (old_cflag&CSIZE) ) {

		arg = -1;

		switch( (cflag&CSIZE) ) {
		case CS5: arg = DIGI_WORD_SIZE_5; break;
		case CS6: arg = DIGI_WORD_SIZE_6; break;
		case CS7: arg = DIGI_WORD_SIZE_7; break;
		case CS8: arg = DIGI_WORD_SIZE_8; break;
		default:
			dbg( "digi_set_termios: can't handle word size %d",
				(cflag&CSIZE) );
			break;
		}

		if( arg != -1 ) {
			buf[i++] = DIGI_CMD_SET_WORD_SIZE;
			buf[i++] = priv->dp_port_num;
			buf[i++] = arg;
			buf[i++] = 0;
		}

	}

	/* set stop bits */
	if( (cflag&CSTOPB) != (old_cflag&CSTOPB) ) {

		if( (cflag&CSTOPB) )
			arg = DIGI_STOP_BITS_2;
		else
			arg = DIGI_STOP_BITS_1;

		buf[i++] = DIGI_CMD_SET_STOP_BITS;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set input flow control */
	if( (iflag&IXOFF) != (old_iflag&IXOFF)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) {

		arg = 0;

		if( (iflag&IXOFF) )
			arg |= DIGI_INPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_INPUT_FLOW_CONTROL_XON_XOFF;

		if( (cflag&CRTSCTS) ) {

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
	if( (iflag&IXON) != (old_iflag&IXON)
	|| (cflag&CRTSCTS) != (old_cflag&CRTSCTS) ) {

		arg = 0;

		if( (iflag&IXON) )
			arg |= DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;
		else
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_XON_XOFF;

		if( (cflag&CRTSCTS) ) {
			arg |= DIGI_OUTPUT_FLOW_CONTROL_CTS;
		} else {
			arg &= ~DIGI_OUTPUT_FLOW_CONTROL_CTS;
			port->tty->hw_stopped = 0;
		}

		buf[i++] = DIGI_CMD_SET_OUTPUT_FLOW_CONTROL;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	/* set receive enable/disable */
	if( (cflag&CREAD) != (old_cflag&CREAD) ) {

		if( (cflag&CREAD) )
			arg = DIGI_ENABLE;
		else
			arg = DIGI_DISABLE;

		buf[i++] = DIGI_CMD_RECEIVE_ENABLE;
		buf[i++] = priv->dp_port_num;
		buf[i++] = arg;
		buf[i++] = 0;

	}

	if( (ret=digi_write_oob_command( port, buf, i, 1 )) != 0 )
		dbg( "digi_set_termios: write oob failed, ret=%d", ret );

}


static void digi_break_ctl( struct usb_serial_port *port, int break_state )
{

	unsigned char buf[4];


	buf[0] = DIGI_CMD_BREAK_CONTROL;
	buf[1] = 2;				/* length */
	buf[2] = break_state ? 1 : 0;
	buf[3] = 0;				/* pad */

	digi_write_inb_command( port, buf, 4, 0 );

}


static int digi_tiocmget( struct usb_serial_port *port, struct file *file )
{
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned int val;
	unsigned long flags;

	dbg("%s: TOP: port=%d", __FUNCTION__, priv->dp_port_num);

	spin_lock_irqsave( &priv->dp_port_lock, flags );
	val = priv->dp_modem_signals;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
	return val;
}


static int digi_tiocmset( struct usb_serial_port *port, struct file *file,
	unsigned int set, unsigned int clear )
{
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned int val;
	unsigned long flags;

	dbg("%s: TOP: port=%d", __FUNCTION__, priv->dp_port_num);

	spin_lock_irqsave( &priv->dp_port_lock, flags );
	val = (priv->dp_modem_signals & ~clear) | set;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
	return digi_set_modem_signals( port, val, 1 );
}


static int digi_ioctl( struct usb_serial_port *port, struct file *file,
	unsigned int cmd, unsigned long arg )
{

	struct digi_port *priv = usb_get_serial_port_data(port);

dbg( "digi_ioctl: TOP: port=%d, cmd=0x%x", priv->dp_port_num, cmd );

	switch (cmd) {

	case TIOCMIWAIT:
		/* wait for any of the 4 modem inputs (DCD,RI,DSR,CTS)*/
		/* TODO */
		return( 0 );

	case TIOCGICOUNT:
		/* return count of modemline transitions */
		/* TODO */
		return 0;

	}

	return( -ENOIOCTLCMD );

}


static int digi_write( struct usb_serial_port *port, const unsigned char *buf, int count )
{

	int ret,data_len,new_len;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned char *data = port->write_urb->transfer_buffer;
	unsigned long flags = 0;


dbg( "digi_write: TOP: port=%d, count=%d, in_interrupt=%ld",
priv->dp_port_num, count, in_interrupt() );

	/* copy user data (which can sleep) before getting spin lock */
	count = min( count, port->bulk_out_size-2 );
	count = min( 64, count);

	/* be sure only one write proceeds at a time */
	/* there are races on the port private buffer */
	/* and races to check write_urb->status */
	spin_lock_irqsave( &priv->dp_port_lock, flags );

	/* wait for urb status clear to submit another urb */
	if( port->write_urb->status == -EINPROGRESS
	|| priv->dp_write_urb_in_use ) {

		/* buffer data if count is 1 (probably put_char) if possible */
		if( count == 1 && priv->dp_out_buf_len < DIGI_OUT_BUF_SIZE ) {
			priv->dp_out_buf[priv->dp_out_buf_len++] = *buf;
			new_len = 1;
		} else {
			new_len = 0;
		}

		spin_unlock_irqrestore( &priv->dp_port_lock, flags );

		return( new_len );

	}

	/* allow space for any buffered data and for new data, up to */
	/* transfer buffer size - 2 (for command and length bytes) */
	new_len = min(count, port->bulk_out_size-2-priv->dp_out_buf_len);
	data_len = new_len + priv->dp_out_buf_len;

	if( data_len == 0 ) {
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return( 0 );
	}

	port->write_urb->transfer_buffer_length = data_len+2;
	port->write_urb->dev = port->serial->dev;

	*data++ = DIGI_CMD_SEND_DATA;
	*data++ = data_len;

	/* copy in buffered data first */
	memcpy( data, priv->dp_out_buf, priv->dp_out_buf_len );
	data += priv->dp_out_buf_len;

	/* copy in new data */
	memcpy( data, buf, new_len );

	if( (ret=usb_submit_urb(port->write_urb, GFP_ATOMIC)) == 0 ) {
		priv->dp_write_urb_in_use = 1;
		ret = new_len;
		priv->dp_out_buf_len = 0;
	}

	/* return length of new data written, or error */
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
	if( ret < 0 ) {
		err("%s: usb_submit_urb failed, ret=%d, port=%d", __FUNCTION__,
			ret, priv->dp_port_num );
	}

dbg( "digi_write: returning %d", ret );
	return( ret );

} 


static void digi_write_bulk_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial;
	struct digi_port *priv;
	struct digi_serial *serial_priv;
	int ret = 0;
	int status = urb->status;


	dbg("digi_write_bulk_callback: TOP, urb status=%d", status);

	/* port and serial sanity check */
	if( port == NULL || (priv=usb_get_serial_port_data(port)) == NULL ) {
		err("%s: port or port->private is NULL, status=%d",
		    __FUNCTION__, status);
		return;
	}
	serial = port->serial;
	if( serial == NULL || (serial_priv=usb_get_serial_data(serial)) == NULL ) {
		err("%s: serial or serial->private is NULL, status=%d",
		    __FUNCTION__, status);
		return;
	}

	/* handle oob callback */
	if( priv->dp_port_num == serial_priv->ds_oob_port_num ) {
		dbg( "digi_write_bulk_callback: oob callback" );
		spin_lock( &priv->dp_port_lock );
		priv->dp_write_urb_in_use = 0;
		wake_up_interruptible( &port->write_wait );
		spin_unlock( &priv->dp_port_lock );
		return;
	}

	/* try to send any buffered data on this port, if it is open */
	spin_lock( &priv->dp_port_lock );
	priv->dp_write_urb_in_use = 0;
	if( port->open_count && port->write_urb->status != -EINPROGRESS
	&& priv->dp_out_buf_len > 0 ) {

		*((unsigned char *)(port->write_urb->transfer_buffer))
			= (unsigned char)DIGI_CMD_SEND_DATA;
		*((unsigned char *)(port->write_urb->transfer_buffer)+1)
			= (unsigned char)priv->dp_out_buf_len;

		port->write_urb->transfer_buffer_length
			= priv->dp_out_buf_len+2;
		port->write_urb->dev = serial->dev;

		memcpy( port->write_urb->transfer_buffer+2, priv->dp_out_buf,
			priv->dp_out_buf_len );

		if( (ret=usb_submit_urb(port->write_urb, GFP_ATOMIC)) == 0 ) {
			priv->dp_write_urb_in_use = 1;
			priv->dp_out_buf_len = 0;
		}

	}

	/* wake up processes sleeping on writes immediately */
	digi_wakeup_write( port );

	/* also queue up a wakeup at scheduler time, in case we */
	/* lost the race in write_chan(). */
	schedule_work(&priv->dp_wakeup_work);

	spin_unlock( &priv->dp_port_lock );

	if( ret ) {
		err("%s: usb_submit_urb failed, ret=%d, port=%d", __FUNCTION__,
			ret, priv->dp_port_num );
	}

}


static int digi_write_room( struct usb_serial_port *port )
{

	int room;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;


	spin_lock_irqsave( &priv->dp_port_lock, flags );

	if( port->write_urb->status == -EINPROGRESS
	|| priv->dp_write_urb_in_use )
		room = 0;
	else
		room = port->bulk_out_size - 2 - priv->dp_out_buf_len;

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

dbg( "digi_write_room: port=%d, room=%d", priv->dp_port_num, room );
	return( room );

}


static int digi_chars_in_buffer( struct usb_serial_port *port )
{

	struct digi_port *priv = usb_get_serial_port_data(port);


	if( port->write_urb->status == -EINPROGRESS
	|| priv->dp_write_urb_in_use ) {
dbg( "digi_chars_in_buffer: port=%d, chars=%d", priv->dp_port_num, port->bulk_out_size - 2 );
		/* return( port->bulk_out_size - 2 ); */
		return( 256 );
	} else {
dbg( "digi_chars_in_buffer: port=%d, chars=%d", priv->dp_port_num, priv->dp_out_buf_len );
		return( priv->dp_out_buf_len );
	}

}


static int digi_open( struct usb_serial_port *port, struct file *filp )
{

	int ret;
	unsigned char buf[32];
	struct digi_port *priv = usb_get_serial_port_data(port);
	struct ktermios not_termios;
	unsigned long flags = 0;


dbg( "digi_open: TOP: port=%d, open_count=%d", priv->dp_port_num, port->open_count );

	/* be sure the device is started up */
	if( digi_startup_device( port->serial ) != 0 )
		return( -ENXIO );

	spin_lock_irqsave( &priv->dp_port_lock, flags );

	/* don't wait on a close in progress for non-blocking opens */
	if( priv->dp_in_close && (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
		spin_unlock_irqrestore( &priv->dp_port_lock, flags );
		return( -EAGAIN );
	}

	/* wait for a close in progress to finish */
	while( priv->dp_in_close ) {
		cond_wait_interruptible_timeout_irqrestore(
			&priv->dp_close_wait, DIGI_RETRY_TIMEOUT,
			&priv->dp_port_lock, flags );
		if( signal_pending(current) ) {
			return( -EINTR );
		}
		spin_lock_irqsave( &priv->dp_port_lock, flags );
	}

	spin_unlock_irqrestore( &priv->dp_port_lock, flags );
 
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

	if( (ret=digi_write_oob_command( port, buf, 8, 1 )) != 0 )
		dbg( "digi_open: write oob failed, ret=%d", ret );

	/* set termios settings */
	not_termios.c_cflag = ~port->tty->termios->c_cflag;
	not_termios.c_iflag = ~port->tty->termios->c_iflag;
	digi_set_termios( port, &not_termios );

	/* set DTR and RTS */
	digi_set_modem_signals( port, TIOCM_DTR|TIOCM_RTS, 1 );

	return( 0 );

}


static void digi_close( struct usb_serial_port *port, struct file *filp )
{
	DEFINE_WAIT(wait);
	int ret;
	unsigned char buf[32];
	struct tty_struct *tty = port->tty;
	struct digi_port *priv = usb_get_serial_port_data(port);
	unsigned long flags = 0;


dbg( "digi_close: TOP: port=%d, open_count=%d", priv->dp_port_num, port->open_count );


	/* if disconnected, just clear flags */
	if (!usb_get_intfdata(port->serial->interface))
		goto exit;

	/* do cleanup only after final close on this port */
	spin_lock_irqsave( &priv->dp_port_lock, flags );
	priv->dp_in_close = 1;
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

	/* tell line discipline to process only XON/XOFF */
	tty->closing = 1;

	/* wait for output to drain */
	if( (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
		tty_wait_until_sent( tty, DIGI_CLOSE_TIMEOUT );
	}

	/* flush driver and line discipline buffers */
	if( tty->driver->flush_buffer )
		tty->driver->flush_buffer( tty );
	tty_ldisc_flush(tty);

	if (port->serial->dev) {
		/* wait for transmit idle */
		if( (filp->f_flags&(O_NDELAY|O_NONBLOCK)) == 0 ) {
			digi_transmit_idle( port, DIGI_CLOSE_TIMEOUT );
		}

		/* drop DTR and RTS */
		digi_set_modem_signals( port, 0, 0 );

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

		if( (ret=digi_write_oob_command( port, buf, 20, 0 )) != 0 )
			dbg( "digi_close: write oob failed, ret=%d", ret );

		/* wait for final commands on oob port to complete */
		prepare_to_wait(&priv->dp_flush_wait, &wait, TASK_INTERRUPTIBLE);
		schedule_timeout(DIGI_CLOSE_TIMEOUT);
		finish_wait(&priv->dp_flush_wait, &wait);

		/* shutdown any outstanding bulk writes */
		usb_kill_urb(port->write_urb);
	}

	tty->closing = 0;

exit:
	spin_lock_irqsave( &priv->dp_port_lock, flags );
	priv->dp_write_urb_in_use = 0;
	priv->dp_in_close = 0;
	wake_up_interruptible( &priv->dp_close_wait );
	spin_unlock_irqrestore( &priv->dp_port_lock, flags );

dbg( "digi_close: done" );
}


/*
*  Digi Startup Device
*
*  Starts reads on all ports.  Must be called AFTER startup, with
*  urbs initialized.  Returns 0 if successful, non-zero error otherwise.
*/

static int digi_startup_device( struct usb_serial *serial )
{

	int i,ret = 0;
	struct digi_serial *serial_priv = usb_get_serial_data(serial);
	struct usb_serial_port *port;


	/* be sure this happens exactly once */
	spin_lock( &serial_priv->ds_serial_lock );
	if( serial_priv->ds_device_started ) {
		spin_unlock( &serial_priv->ds_serial_lock );
		return( 0 );
	}
	serial_priv->ds_device_started = 1;
	spin_unlock( &serial_priv->ds_serial_lock );

	/* start reading from each bulk in endpoint for the device */
	/* set USB_DISABLE_SPD flag for write bulk urbs */
	for( i=0; i<serial->type->num_ports+1; i++ ) {

		port = serial->port[i];

		port->write_urb->dev = port->serial->dev;

		if( (ret=usb_submit_urb(port->read_urb, GFP_KERNEL)) != 0 ) {
			err("%s: usb_submit_urb failed, ret=%d, port=%d", __FUNCTION__,
			ret, i );
			break;
		}

	}

	return( ret );

}


static int digi_startup( struct usb_serial *serial )
{

	int i;
	struct digi_port *priv;
	struct digi_serial *serial_priv;


dbg( "digi_startup: TOP" );

	/* allocate the private data structures for all ports */
	/* number of regular ports + 1 for the out-of-band port */
	for( i=0; i<serial->type->num_ports+1; i++ ) {

		/* allocate port private structure */
		priv = kmalloc( sizeof(struct digi_port),
			GFP_KERNEL );
		if( priv == (struct digi_port *)0 ) {
			while( --i >= 0 )
				kfree( usb_get_serial_port_data(serial->port[i]) );
			return( 1 );			/* error */
		}

		/* initialize port private structure */
		spin_lock_init( &priv->dp_port_lock );
		priv->dp_port_num = i;
		priv->dp_out_buf_len = 0;
		priv->dp_write_urb_in_use = 0;
		priv->dp_modem_signals = 0;
		init_waitqueue_head( &priv->dp_modem_change_wait );
		priv->dp_transmit_idle = 0;
		init_waitqueue_head( &priv->dp_transmit_idle_wait );
		priv->dp_throttled = 0;
		priv->dp_throttle_restart = 0;
		init_waitqueue_head( &priv->dp_flush_wait );
		priv->dp_in_close = 0;
		init_waitqueue_head( &priv->dp_close_wait );
		INIT_WORK(&priv->dp_wakeup_work, digi_wakeup_write_lock);
		priv->dp_port = serial->port[i];

		/* initialize write wait queue for this port */
		init_waitqueue_head( &serial->port[i]->write_wait );

		usb_set_serial_port_data(serial->port[i], priv);
	}

	/* allocate serial private structure */
	serial_priv = kmalloc( sizeof(struct digi_serial),
		GFP_KERNEL );
	if( serial_priv == (struct digi_serial *)0 ) {
		for( i=0; i<serial->type->num_ports+1; i++ )
			kfree( usb_get_serial_port_data(serial->port[i]) );
		return( 1 );			/* error */
	}

	/* initialize serial private structure */
	spin_lock_init( &serial_priv->ds_serial_lock );
	serial_priv->ds_oob_port_num = serial->type->num_ports;
	serial_priv->ds_oob_port = serial->port[serial_priv->ds_oob_port_num];
	serial_priv->ds_device_started = 0;
	usb_set_serial_data(serial, serial_priv);

	return( 0 );

}


static void digi_shutdown( struct usb_serial *serial )
{

	int i;


dbg( "digi_shutdown: TOP, in_interrupt()=%ld", in_interrupt() );

	/* stop reads and writes on all ports */
	for( i=0; i<serial->type->num_ports+1; i++ ) {
		usb_kill_urb(serial->port[i]->read_urb);
		usb_kill_urb(serial->port[i]->write_urb);
	}

	/* free the private data structures for all ports */
	/* number of regular ports + 1 for the out-of-band port */
	for( i=0; i<serial->type->num_ports+1; i++ )
		kfree( usb_get_serial_port_data(serial->port[i]) );
	kfree( usb_get_serial_data(serial) );
}


static void digi_read_bulk_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct digi_port *priv;
	struct digi_serial *serial_priv;
	int ret;
	int status = urb->status;


dbg( "digi_read_bulk_callback: TOP" );

	/* port sanity check, do not resubmit if port is not valid */
	if( port == NULL || (priv=usb_get_serial_port_data(port)) == NULL ) {
		err("%s: port or port->private is NULL, status=%d",
		    __FUNCTION__, status);
		return;
	}
	if( port->serial == NULL
	|| (serial_priv=usb_get_serial_data(port->serial)) == NULL ) {
		err("%s: serial is bad or serial->private is NULL, status=%d",
		    __FUNCTION__, status);
		return;
	}

	/* do not resubmit urb if it has any status error */
	if (status) {
		err("%s: nonzero read bulk status: status=%d, port=%d",
		    __FUNCTION__, status, priv->dp_port_num);
		return;
	}

	/* handle oob or inb callback, do not resubmit if error */
	if( priv->dp_port_num == serial_priv->ds_oob_port_num ) {
		if( digi_read_oob_callback( urb ) != 0 )
			return;
	} else {
		if( digi_read_inb_callback( urb ) != 0 )
			return;
	}

	/* continue read */
	urb->dev = port->serial->dev;
	if( (ret=usb_submit_urb(urb, GFP_ATOMIC)) != 0 ) {
		err("%s: failed resubmitting urb, ret=%d, port=%d", __FUNCTION__,
			ret, priv->dp_port_num );
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

static int digi_read_inb_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct tty_struct *tty = port->tty;
	struct digi_port *priv = usb_get_serial_port_data(port);
	int opcode = ((unsigned char *)urb->transfer_buffer)[0];
	int len = ((unsigned char *)urb->transfer_buffer)[1];
	int port_status = ((unsigned char *)urb->transfer_buffer)[2];
	unsigned char *data = ((unsigned char *)urb->transfer_buffer)+3;
	int flag,throttled;
	int i;
	int status = urb->status;

	/* do not process callbacks on closed ports */
	/* but do continue the read chain */
	if( port->open_count == 0 )
		return( 0 );

	/* short/multiple packet check */
	if( urb->actual_length != len + 2 ) {
		err("%s: INCOMPLETE OR MULTIPLE PACKET, urb status=%d, "
		    "port=%d, opcode=%d, len=%d, actual_length=%d, "
		    "port_status=%d", __FUNCTION__, status, priv->dp_port_num,
		    opcode, len, urb->actual_length, port_status);
		return( -1 );
	}

	spin_lock( &priv->dp_port_lock );

	/* check for throttle; if set, do not resubmit read urb */
	/* indicate the read chain needs to be restarted on unthrottle */
	throttled = priv->dp_throttled;
	if( throttled )
		priv->dp_throttle_restart = 1;

	/* receive data */
	if( opcode == DIGI_CMD_RECEIVE_DATA ) {

		/* get flag from port_status */
		flag = 0;

		/* overrun is special, not associated with a char */
		if (port_status & DIGI_OVERRUN_ERROR) {
			tty_insert_flip_char( tty, 0, TTY_OVERRUN );
		}

		/* break takes precedence over parity, */
		/* which takes precedence over framing errors */
		if (port_status & DIGI_BREAK_ERROR) {
			flag = TTY_BREAK;
		} else if (port_status & DIGI_PARITY_ERROR) {
			flag = TTY_PARITY;
		} else if (port_status & DIGI_FRAMING_ERROR) {
			flag = TTY_FRAME;
		}

		/* data length is len-1 (one byte of len is port_status) */
		--len;

		len = tty_buffer_request_room(tty, len);
		if( len > 0 ) {
			/* Hot path */
			if(flag == TTY_NORMAL)
				tty_insert_flip_string(tty, data, len);
			else {
				for(i = 0; i < len; i++)
					tty_insert_flip_char(tty, data[i], flag);
			}
			tty_flip_buffer_push( tty );
		}
	}

	spin_unlock( &priv->dp_port_lock );

	if( opcode == DIGI_CMD_RECEIVE_DISABLE ) {
		dbg("%s: got RECEIVE_DISABLE", __FUNCTION__ );
	} else if( opcode != DIGI_CMD_RECEIVE_DATA ) {
		dbg("%s: unknown opcode: %d", __FUNCTION__, opcode );
	}

	return( throttled ? 1 : 0 );

}


/* 
*  Digi Read OOB Callback
*
*  Digi Read OOB Callback handles reads on the out of band port.
*  When called we know port and port->private are not NULL and
*  the port->serial is valid.  It returns 0 if successful, and
*  -1 if the sanity checks failed.
*/

static int digi_read_oob_callback( struct urb *urb )
{

	struct usb_serial_port *port = (struct usb_serial_port *)urb->context;
	struct usb_serial *serial = port->serial;
	struct digi_port *priv = usb_get_serial_port_data(port);
	int opcode, line, status, val;
	int i;


dbg( "digi_read_oob_callback: port=%d, len=%d", priv->dp_port_num,
urb->actual_length );

	/* handle each oob command */
	for( i=0; i<urb->actual_length-3; ) {

		opcode = ((unsigned char *)urb->transfer_buffer)[i++];
		line = ((unsigned char *)urb->transfer_buffer)[i++];
		status = ((unsigned char *)urb->transfer_buffer)[i++];
		val = ((unsigned char *)urb->transfer_buffer)[i++];

dbg( "digi_read_oob_callback: opcode=%d, line=%d, status=%d, val=%d",
opcode, line, status, val );

		if( status != 0 || line >= serial->type->num_ports )
			continue;

		port = serial->port[line];

		if ((priv=usb_get_serial_port_data(port)) == NULL )
			return -1;

		if( opcode == DIGI_CMD_READ_INPUT_SIGNALS ) {

			spin_lock( &priv->dp_port_lock );

			/* convert from digi flags to termiox flags */
			if( val & DIGI_READ_INPUT_SIGNALS_CTS ) {
				priv->dp_modem_signals |= TIOCM_CTS;
				/* port must be open to use tty struct */
				if( port->open_count
				&& port->tty->termios->c_cflag & CRTSCTS ) {
					port->tty->hw_stopped = 0;
					digi_wakeup_write( port );
				}
			} else {
				priv->dp_modem_signals &= ~TIOCM_CTS;
				/* port must be open to use tty struct */
				if( port->open_count
				&& port->tty->termios->c_cflag & CRTSCTS ) {
					port->tty->hw_stopped = 1;
				}
			}
			if( val & DIGI_READ_INPUT_SIGNALS_DSR )
				priv->dp_modem_signals |= TIOCM_DSR;
			else
				priv->dp_modem_signals &= ~TIOCM_DSR;
			if( val & DIGI_READ_INPUT_SIGNALS_RI )
				priv->dp_modem_signals |= TIOCM_RI;
			else
				priv->dp_modem_signals &= ~TIOCM_RI;
			if( val & DIGI_READ_INPUT_SIGNALS_DCD )
				priv->dp_modem_signals |= TIOCM_CD;
			else
				priv->dp_modem_signals &= ~TIOCM_CD;

			wake_up_interruptible( &priv->dp_modem_change_wait );
			spin_unlock( &priv->dp_port_lock );

		} else if( opcode == DIGI_CMD_TRANSMIT_IDLE ) {

			spin_lock( &priv->dp_port_lock );
			priv->dp_transmit_idle = 1;
			wake_up_interruptible( &priv->dp_transmit_idle_wait );
			spin_unlock( &priv->dp_port_lock );

		} else if( opcode == DIGI_CMD_IFLUSH_FIFO ) {

			wake_up_interruptible( &priv->dp_flush_wait );

		}

	}

	return( 0 );

}


static int __init digi_init (void)
{
	int retval;
	retval = usb_serial_register(&digi_acceleport_2_device);
	if (retval)
		goto failed_acceleport_2_device;
	retval = usb_serial_register(&digi_acceleport_4_device);
	if (retval) 
		goto failed_acceleport_4_device;
	retval = usb_register(&digi_driver);
	if (retval)
		goto failed_usb_register;
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
failed_usb_register:
	usb_serial_deregister(&digi_acceleport_4_device);
failed_acceleport_4_device:
	usb_serial_deregister(&digi_acceleport_2_device);
failed_acceleport_2_device:
	return retval;
}


static void __exit digi_exit (void)
{
	usb_deregister (&digi_driver);
	usb_serial_deregister (&digi_acceleport_2_device);
	usb_serial_deregister (&digi_acceleport_4_device);
}


module_init(digi_init);
module_exit(digi_exit);


MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debug enabled or not");
