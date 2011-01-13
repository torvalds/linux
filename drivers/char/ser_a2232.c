/* drivers/char/ser_a2232.c */

/* $Id: ser_a2232.c,v 0.4 2000/01/25 12:00:00 ehaase Exp $ */

/* Linux serial driver for the Amiga A2232 board */

/* This driver is MAINTAINED. Before applying any changes, please contact
 * the author.
 */

/* Copyright (c) 2000-2001 Enver Haase    <ehaase@inf.fu-berlin.de>
 *                   alias The A2232 driver project <A2232@gmx.net>
 * All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/***************************** Documentation ************************/
/*
 * This driver is in EXPERIMENTAL state. That means I could not find
 * someone with five A2232 boards with 35 ports running at 19200 bps
 * at the same time and test the machine's behaviour.
 * However, I know that you can performance-tweak this driver (see
 * the source code).
 * One thing to consider is the time this driver consumes during the
 * Amiga's vertical blank interrupt. Everything that is to be done
 * _IS DONE_ when entering the vertical blank interrupt handler of
 * this driver.
 * However, it would be more sane to only do the job for only ONE card
 * instead of ALL cards at a time; or, more generally, to handle only
 * SOME ports instead of ALL ports at a time.
 * However, as long as no-one runs into problems I guess I shouldn't
 * change the driver as it runs fine for me :) .
 *
 * Version history of this file:
 * 0.4	Resolved licensing issues.
 * 0.3	Inclusion in the Linux/m68k tree, small fixes.
 * 0.2	Added documentation, minor typo fixes.
 * 0.1	Initial release.
 *
 * TO DO:
 * -	Handle incoming BREAK events. I guess "Stevens: Advanced
 *	Programming in the UNIX(R) Environment" is a good reference
 *	on what is to be done.
 * -	When installing as a module, don't simply 'printk' text, but
 *	send it to the TTY used by the user.
 *
 * THANKS TO:
 * -	Jukka Marin (65EC02 code).
 * -	The other NetBSD developers on whose A2232 driver I had a
 *	pretty close look. However, I didn't copy any code so it
 *	is okay to put my code under the GPL and include it into
 *	Linux.
 */
/***************************** End of Documentation *****************/

/***************************** Defines ******************************/
/*
 * Enables experimental 115200 (normal) 230400 (turbo) baud rate.
 * The A2232 specification states it can only operate at speeds up to
 * 19200 bits per second, and I was not able to send a file via
 * "sz"/"rz" and a null-modem cable from one A2232 port to another
 * at 115200 bits per second.
 * However, this might work for you.
 */
#undef A2232_SPEEDHACK
/*
 * Default is not to use RTS/CTS so you could be talked to death.
 */
#define A2232_SUPPRESS_RTSCTS_WARNING
/************************* End of Defines ***************************/

/***************************** Includes *****************************/
#include <linux/module.h>

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/setup.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <linux/zorro.h>
#include <asm/irq.h>
#include <linux/mutex.h>

#include <linux/delay.h>

#include <linux/serial.h>
#include <linux/generic_serial.h>
#include <linux/tty_flip.h>

#include "ser_a2232.h"
#include "ser_a2232fw.h"
/************************* End of Includes **************************/

/***************************** Prototypes ***************************/
/* The interrupt service routine */
static irqreturn_t a2232_vbl_inter(int irq, void *data);
/* Initialize the port structures */
static void a2232_init_portstructs(void);
/* Initialize and register TTY drivers. */
/* returns 0 IFF successful */
static int a2232_init_drivers(void); 

/* BEGIN GENERIC_SERIAL PROTOTYPES */
static void a2232_disable_tx_interrupts(void *ptr);
static void a2232_enable_tx_interrupts(void *ptr);
static void a2232_disable_rx_interrupts(void *ptr);
static void a2232_enable_rx_interrupts(void *ptr);
static int  a2232_carrier_raised(struct tty_port *port);
static void a2232_shutdown_port(void *ptr);
static int  a2232_set_real_termios(void *ptr);
static int  a2232_chars_in_buffer(void *ptr);
static void a2232_close(void *ptr);
static void a2232_hungup(void *ptr);
/* static void a2232_getserial (void *ptr, struct serial_struct *sp); */
/* END GENERIC_SERIAL PROTOTYPES */

/* Functions that the TTY driver struct expects */
static int  a2232_ioctl(struct tty_struct *tty, struct file *file,
										unsigned int cmd, unsigned long arg);
static void a2232_throttle(struct tty_struct *tty);
static void a2232_unthrottle(struct tty_struct *tty);
static int  a2232_open(struct tty_struct * tty, struct file * filp);
/************************* End of Prototypes ************************/

/***************************** Global variables *********************/
/*---------------------------------------------------------------------------
 * Interface from generic_serial.c back here
 *--------------------------------------------------------------------------*/
static struct real_driver a2232_real_driver = {
        a2232_disable_tx_interrupts,
        a2232_enable_tx_interrupts,
        a2232_disable_rx_interrupts,
        a2232_enable_rx_interrupts,
        a2232_shutdown_port,
        a2232_set_real_termios,
        a2232_chars_in_buffer,
        a2232_close,
        a2232_hungup,
	NULL	/* a2232_getserial */
};

static void *a2232_driver_ID = &a2232_driver_ID; // Some memory address WE own.

/* Ports structs */
static struct a2232_port a2232_ports[MAX_A2232_BOARDS*NUMLINES];

/* TTY driver structs */
static struct tty_driver *a2232_driver;

/* nr of cards completely (all ports) and correctly configured */
static int nr_a2232; 

/* zorro_dev structs for the A2232's */
static struct zorro_dev *zd_a2232[MAX_A2232_BOARDS]; 
/***************************** End of Global variables **************/

/* Helper functions */

static inline volatile struct a2232memory *a2232mem(unsigned int board)
{
	return (volatile struct a2232memory *)ZTWO_VADDR(zd_a2232[board]->resource.start);
}

static inline volatile struct a2232status *a2232stat(unsigned int board,
						     unsigned int portonboard)
{
	volatile struct a2232memory *mem = a2232mem(board);
	return &(mem->Status[portonboard]);
}

static inline void a2232_receive_char(struct a2232_port *port, int ch, int err)
{
/* 	Mostly stolen from other drivers.
	Maybe one could implement a more efficient version by not only
	transferring one character at a time.
*/
	struct tty_struct *tty = port->gs.port.tty;

#if 0
	switch(err) {
	case TTY_BREAK:
		break;
	case TTY_PARITY:
		break;
	case TTY_OVERRUN:
		break;
	case TTY_FRAME:
		break;
	}
#endif

	tty_insert_flip_char(tty, ch, err);
	tty_flip_buffer_push(tty);
}

/***************************** Functions ****************************/
/*** BEGIN OF REAL_DRIVER FUNCTIONS ***/

static void a2232_disable_tx_interrupts(void *ptr)
{
	struct a2232_port *port;
	volatile struct a2232status *stat;
	unsigned long flags;
  
	port = ptr;
	stat = a2232stat(port->which_a2232, port->which_port_on_a2232);
	stat->OutDisable = -1;

	/* Does this here really have to be? */
	local_irq_save(flags);
	port->gs.port.flags &= ~GS_TX_INTEN;
	local_irq_restore(flags);
}

static void a2232_enable_tx_interrupts(void *ptr)
{
	struct a2232_port *port;
	volatile struct a2232status *stat;
	unsigned long flags;

	port = ptr;
	stat = a2232stat(port->which_a2232, port->which_port_on_a2232);
	stat->OutDisable = 0;

	/* Does this here really have to be? */
	local_irq_save(flags);
	port->gs.port.flags |= GS_TX_INTEN;
	local_irq_restore(flags);
}

static void a2232_disable_rx_interrupts(void *ptr)
{
	struct a2232_port *port;
	port = ptr;
	port->disable_rx = -1;
}

static void a2232_enable_rx_interrupts(void *ptr)
{
	struct a2232_port *port;
	port = ptr;
	port->disable_rx = 0;
}

static int  a2232_carrier_raised(struct tty_port *port)
{
	struct a2232_port *ap = container_of(port, struct a2232_port, gs.port);
	return ap->cd_status;
}

static void a2232_shutdown_port(void *ptr)
{
	struct a2232_port *port;
	volatile struct a2232status *stat;
	unsigned long flags;

	port = ptr;
	stat = a2232stat(port->which_a2232, port->which_port_on_a2232);

	local_irq_save(flags);

	port->gs.port.flags &= ~GS_ACTIVE;
	
	if (port->gs.port.tty && port->gs.port.tty->termios->c_cflag & HUPCL) {
		/* Set DTR and RTS to Low, flush output.
		   The NetBSD driver "msc.c" does it this way. */
		stat->Command = (	(stat->Command & ~A2232CMD_CMask) | 
					A2232CMD_Close );
		stat->OutFlush = -1;
		stat->Setup = -1;
	}

	local_irq_restore(flags);
	
	/* After analyzing control flow, I think a2232_shutdown_port
		is actually the last call from the system when at application
		level someone issues a "echo Hello >>/dev/ttyY0".
		Therefore I think the MOD_DEC_USE_COUNT should be here and
		not in "a2232_close()". See the comment in "sx.c", too.
		If you run into problems, compile this driver into the
		kernel instead of compiling it as a module. */
}

static int  a2232_set_real_termios(void *ptr)
{
	unsigned int cflag, baud, chsize, stopb, parity, softflow;
	int rate;
	int a2232_param, a2232_cmd;
	unsigned long flags;
	unsigned int i;
	struct a2232_port *port = ptr;
	volatile struct a2232status *status;
	volatile struct a2232memory *mem;

	if (!port->gs.port.tty || !port->gs.port.tty->termios) return 0;

	status = a2232stat(port->which_a2232, port->which_port_on_a2232);
	mem = a2232mem(port->which_a2232);
	
	a2232_param = a2232_cmd = 0;

	// get baud rate
	baud = port->gs.baud;
	if (baud == 0) {
		/* speed == 0 -> drop DTR, do nothing else */
		local_irq_save(flags);
		// Clear DTR (and RTS... mhhh).
		status->Command = (	(status->Command & ~A2232CMD_CMask) |
					A2232CMD_Close );
		status->OutFlush = -1;
		status->Setup = -1;
		
		local_irq_restore(flags);
		return 0;
	}
	
	rate = A2232_BAUD_TABLE_NOAVAIL;
	for (i=0; i < A2232_BAUD_TABLE_NUM_RATES * 3; i += 3){
		if (a2232_baud_table[i] == baud){
			if (mem->Common.Crystal == A2232_TURBO) rate = a2232_baud_table[i+2];
			else                                    rate = a2232_baud_table[i+1];
		}
	}
	if (rate == A2232_BAUD_TABLE_NOAVAIL){
		printk("a2232: Board %d Port %d unsupported baud rate: %d baud. Using another.\n",port->which_a2232,port->which_port_on_a2232,baud);
		// This is useful for both (turbo or normal) Crystal versions.
		rate = A2232PARAM_B9600;
	}
	a2232_param |= rate;

	cflag  = port->gs.port.tty->termios->c_cflag;

	// get character size
	chsize = cflag & CSIZE;
	switch (chsize){
		case CS8: 	a2232_param |= A2232PARAM_8Bit; break;
		case CS7: 	a2232_param |= A2232PARAM_7Bit; break;
		case CS6: 	a2232_param |= A2232PARAM_6Bit; break;
		case CS5: 	a2232_param |= A2232PARAM_5Bit; break;
		default:	printk("a2232: Board %d Port %d unsupported character size: %d. Using 8 data bits.\n",
					port->which_a2232,port->which_port_on_a2232,chsize);
				a2232_param |= A2232PARAM_8Bit; break;
	}

	// get number of stop bits
	stopb  = cflag & CSTOPB;
	if (stopb){ // two stop bits instead of one
		printk("a2232: Board %d Port %d 2 stop bits unsupported. Using 1 stop bit.\n",
			port->which_a2232,port->which_port_on_a2232);
	}

	// Warn if RTS/CTS not wanted
	if (!(cflag & CRTSCTS)){
#ifndef A2232_SUPPRESS_RTSCTS_WARNING
		printk("a2232: Board %d Port %d cannot switch off firmware-implemented RTS/CTS hardware flow control.\n",
			port->which_a2232,port->which_port_on_a2232);
#endif
	}

	/*	I think this is correct.
		However, IXOFF means _input_ flow control and I wonder
		if one should care about IXON _output_ flow control,
		too. If this makes problems, one should turn the A2232
		firmware XON/XOFF "SoftFlow" flow control off and use
		the conventional way of inserting START/STOP characters
		by hand in throttle()/unthrottle().
	*/
	softflow = !!( port->gs.port.tty->termios->c_iflag & IXOFF );

	// get Parity (Enabled/Disabled? If Enabled, Odd or Even?)
	parity = cflag & (PARENB | PARODD);
	if (parity & PARENB){
		if (parity & PARODD){
			a2232_cmd |= A2232CMD_OddParity;
		}
		else{
			a2232_cmd |= A2232CMD_EvenParity;
		}
	}
	else a2232_cmd |= A2232CMD_NoParity;


	/*	Hmm. Maybe an own a2232_port structure
		member would be cleaner?	*/
	if (cflag & CLOCAL)
		port->gs.port.flags &= ~ASYNC_CHECK_CD;
	else
		port->gs.port.flags |= ASYNC_CHECK_CD;


	/* Now we have all parameters and can go to set them: */
	local_irq_save(flags);

	status->Param = a2232_param | A2232PARAM_RcvBaud;
	status->Command = a2232_cmd | A2232CMD_Open |  A2232CMD_Enable;
	status->SoftFlow = softflow;
	status->OutDisable = 0;
	status->Setup = -1;

	local_irq_restore(flags);
	return 0;
}

static int  a2232_chars_in_buffer(void *ptr)
{
	struct a2232_port *port;
	volatile struct a2232status *status; 
	unsigned char ret; /* we need modulo-256 arithmetics */
	port = ptr;
	status = a2232stat(port->which_a2232, port->which_port_on_a2232);
#if A2232_IOBUFLEN != 256
#error "Re-Implement a2232_chars_in_buffer()!"
#endif
	ret = (status->OutHead - status->OutTail);
	return ret;
}

static void a2232_close(void *ptr)
{
	a2232_disable_tx_interrupts(ptr);
	a2232_disable_rx_interrupts(ptr);
	/* see the comment in a2232_shutdown_port above. */
}

static void a2232_hungup(void *ptr)
{
	a2232_close(ptr);
}
/*** END   OF REAL_DRIVER FUNCTIONS ***/

/*** BEGIN  FUNCTIONS EXPECTED BY TTY DRIVER STRUCTS ***/
static int a2232_ioctl(	struct tty_struct *tty, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static void a2232_throttle(struct tty_struct *tty)
{
/* Throttle: System cannot take another chars: Drop RTS or
             send the STOP char or whatever.
   The A2232 firmware does RTS/CTS anyway, and XON/XOFF
   if switched on. So the only thing we can do at this
   layer here is not taking any characters out of the
   A2232 buffer any more. */
	struct a2232_port *port = tty->driver_data;
	port->throttle_input = -1;
}

static void a2232_unthrottle(struct tty_struct *tty)
{
/* Unthrottle: dual to "throttle()" above. */
	struct a2232_port *port = tty->driver_data;
	port->throttle_input = 0;
}

static int  a2232_open(struct tty_struct * tty, struct file * filp)
{
/* More or less stolen from other drivers. */
	int line;
	int retval;
	struct a2232_port *port;

	line = tty->index;
	port = &a2232_ports[line];
	
	tty->driver_data = port;
	port->gs.port.tty = tty;
	port->gs.port.count++;
	retval = gs_init_port(&port->gs);
	if (retval) {
		port->gs.port.count--;
		return retval;
	}
	port->gs.port.flags |= GS_ACTIVE;
	retval = gs_block_til_ready(port, filp);

	if (retval) {
		port->gs.port.count--;
		return retval;
	}

	a2232_enable_rx_interrupts(port);
	
	return 0;
}
/*** END OF FUNCTIONS EXPECTED BY TTY DRIVER STRUCTS ***/

static irqreturn_t a2232_vbl_inter(int irq, void *data)
{
#if A2232_IOBUFLEN != 256
#error "Re-Implement a2232_vbl_inter()!"
#endif

struct a2232_port *port;
volatile struct a2232memory *mem;
volatile struct a2232status *status;
unsigned char newhead;
unsigned char bufpos; /* Must be unsigned char. We need the modulo-256 arithmetics */
unsigned char ncd, ocd, ccd; /* names consistent with the NetBSD driver */
volatile u_char *ibuf, *cbuf, *obuf;
int ch, err, n, p;
	for (n = 0; n < nr_a2232; n++){		/* for every completely initialized A2232 board */
		mem = a2232mem(n);
		for (p = 0; p < NUMLINES; p++){	/* for every port on this board */
			err = 0;
			port = &a2232_ports[n*NUMLINES+p];
			if ( port->gs.port.flags & GS_ACTIVE ){ /* if the port is used */

				status = a2232stat(n,p);

				if (!port->disable_rx && !port->throttle_input){ /* If input is not disabled */
					newhead = status->InHead;               /* 65EC02 write pointer */
					bufpos = status->InTail;

					/* check for input for this port */
					if (newhead != bufpos) {
						/* buffer for input chars/events */
						ibuf = mem->InBuf[p];
 
						/* data types of bytes in ibuf */
						cbuf = mem->InCtl[p];
 
						/* do for all chars */
						while (bufpos != newhead) {
							/* which type of input data? */
							switch (cbuf[bufpos]) {
								/* switch on input event (CD, BREAK, etc.) */
							case A2232INCTL_EVENT:
								switch (ibuf[bufpos++]) {
								case A2232EVENT_Break:
									/* TODO: Handle BREAK signal */
									break;
									/*	A2232EVENT_CarrierOn and A2232EVENT_CarrierOff are
										handled in a separate queue and should not occur here. */
								case A2232EVENT_Sync:
									printk("A2232: 65EC02 software sent SYNC event, don't know what to do. Ignoring.");
									break;
								default:
									printk("A2232: 65EC02 software broken, unknown event type %d occurred.\n",ibuf[bufpos-1]);
								} /* event type switch */
								break;
 							case A2232INCTL_CHAR:
								/* Receive incoming char */
								a2232_receive_char(port, ibuf[bufpos], err);
								bufpos++;
								break;
 							default:
								printk("A2232: 65EC02 software broken, unknown data type %d occurred.\n",cbuf[bufpos]);
								bufpos++;
							} /* switch on input data type */
						} /* while there's something in the buffer */

						status->InTail = bufpos;            /* tell 65EC02 what we've read */
						
					} /* if there was something in the buffer */                          
				} /* If input is not disabled */

				/* Now check if there's something to output */
				obuf = mem->OutBuf[p];
				bufpos = status->OutHead;
				while ( (port->gs.xmit_cnt > 0)		&&
					(!port->gs.port.tty->stopped)	&&
					(!port->gs.port.tty->hw_stopped) ){	/* While there are chars to transmit */
					if (((bufpos+1) & A2232_IOBUFLENMASK) != status->OutTail) { /* If the A2232 buffer is not full */
						ch = port->gs.xmit_buf[port->gs.xmit_tail];					/* get the next char to transmit */
						port->gs.xmit_tail = (port->gs.xmit_tail+1) & (SERIAL_XMIT_SIZE-1); /* modulo-addition for the gs.xmit_buf ring-buffer */
						obuf[bufpos++] = ch;																/* put it into the A2232 buffer */
						port->gs.xmit_cnt--;
					}
					else{																									/* If A2232 the buffer is full */
						break;																							/* simply stop filling it. */
					}													
				}					
				status->OutHead = bufpos;
					
				/* WakeUp if output buffer runs low */
				if ((port->gs.xmit_cnt <= port->gs.wakeup_chars) && port->gs.port.tty) {
					tty_wakeup(port->gs.port.tty);
				}
			} // if the port is used
		} // for every port on the board
			
		/* Now check the CD message queue */
		newhead = mem->Common.CDHead;
		bufpos = mem->Common.CDTail;
		if (newhead != bufpos){				/* There are CD events in queue */
			ocd = mem->Common.CDStatus; 		/* get old status bits */
			while (newhead != bufpos){		/* read all events */
				ncd = mem->CDBuf[bufpos++]; 	/* get one event */
				ccd = ncd ^ ocd; 		/* mask of changed lines */
				ocd = ncd; 			/* save new status bits */
				for(p=0; p < NUMLINES; p++){	/* for all ports */
					if (ccd & 1){		/* this one changed */

						struct a2232_port *port = &a2232_ports[n*7+p];
						port->cd_status = !(ncd & 1); /* ncd&1 <=> CD is now off */

						if (!(port->gs.port.flags & ASYNC_CHECK_CD))
							;	/* Don't report DCD changes */
						else if (port->cd_status) { // if DCD on: DCD went UP!
							
							/* Are we blocking in open?*/
							wake_up_interruptible(&port->gs.port.open_wait);
						}
						else { // if DCD off: DCD went DOWN!
							if (port->gs.port.tty)
								tty_hangup (port->gs.port.tty);
						}
						
					} // if CD changed for this port
					ccd >>= 1;
					ncd >>= 1;									/* Shift bits for next line */
				} // for every port
			} // while CD events in queue
			mem->Common.CDStatus = ocd; /* save new status */
			mem->Common.CDTail = bufpos; /* remove events */
		} // if events in CD queue
		
	} // for every completely initialized A2232 board
	return IRQ_HANDLED;
}

static const struct tty_port_operations a2232_port_ops = {
	.carrier_raised = a2232_carrier_raised,
};

static void a2232_init_portstructs(void)
{
	struct a2232_port *port;
	int i;

	for (i = 0; i < MAX_A2232_BOARDS*NUMLINES; i++) {
		port = a2232_ports + i;
		tty_port_init(&port->gs.port);
		port->gs.port.ops = &a2232_port_ops;
		port->which_a2232 = i/NUMLINES;
		port->which_port_on_a2232 = i%NUMLINES;
		port->disable_rx = port->throttle_input = port->cd_status = 0;
		port->gs.magic = A2232_MAGIC;
		port->gs.close_delay = HZ/2;
		port->gs.closing_wait = 30 * HZ;
		port->gs.rd = &a2232_real_driver;
	}
}

static const struct tty_operations a2232_ops = {
	.open = a2232_open,
	.close = gs_close,
	.write = gs_write,
	.put_char = gs_put_char,
	.flush_chars = gs_flush_chars,
	.write_room = gs_write_room,
	.chars_in_buffer = gs_chars_in_buffer,
	.flush_buffer = gs_flush_buffer,
	.ioctl = a2232_ioctl,
	.throttle = a2232_throttle,
	.unthrottle = a2232_unthrottle,
	.set_termios = gs_set_termios,
	.stop = gs_stop,
	.start = gs_start,
	.hangup = gs_hangup,
};

static int a2232_init_drivers(void)
{
	int error;

	a2232_driver = alloc_tty_driver(NUMLINES * nr_a2232);
	if (!a2232_driver)
		return -ENOMEM;
	a2232_driver->owner = THIS_MODULE;
	a2232_driver->driver_name = "commodore_a2232";
	a2232_driver->name = "ttyY";
	a2232_driver->major = A2232_NORMAL_MAJOR;
	a2232_driver->type = TTY_DRIVER_TYPE_SERIAL;
	a2232_driver->subtype = SERIAL_TYPE_NORMAL;
	a2232_driver->init_termios = tty_std_termios;
	a2232_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	a2232_driver->init_termios.c_ispeed = 9600;
	a2232_driver->init_termios.c_ospeed = 9600;
	a2232_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(a2232_driver, &a2232_ops);
	if ((error = tty_register_driver(a2232_driver))) {
		printk(KERN_ERR "A2232: Couldn't register A2232 driver, error = %d\n",
		       error);
		put_tty_driver(a2232_driver);
		return 1;
	}
	return 0;
}

static int __init a2232board_init(void)
{
	struct zorro_dev *z;

	unsigned int boardaddr;
	int bcount;
	short start;
	u_char *from;
	volatile u_char *to;
	volatile struct a2232memory *mem;
	int error, i;

#ifdef CONFIG_SMP
	return -ENODEV;	/* This driver is not SMP aware. Is there an SMP ZorroII-bus-machine? */
#endif

	if (!MACH_IS_AMIGA){
		return -ENODEV;
	}

	printk("Commodore A2232 driver initializing.\n"); /* Say that we're alive. */

	z = NULL;
	nr_a2232 = 0;
	while ( (z = zorro_find_device(ZORRO_WILDCARD, z)) ){
		if (	(z->id != ZORRO_PROD_CBM_A2232_PROTOTYPE) && 
			(z->id != ZORRO_PROD_CBM_A2232)	){
			continue;	// The board found was no A2232
		}
		if (!zorro_request_device(z,"A2232 driver"))
			continue;

		printk("Commodore A2232 found (#%d).\n",nr_a2232);

		zd_a2232[nr_a2232] = z;

		boardaddr = ZTWO_VADDR( z->resource.start );
		printk("Board is located at address 0x%x, size is 0x%x.\n", boardaddr, (unsigned int) ((z->resource.end+1) - (z->resource.start)));

		mem = (volatile struct a2232memory *) boardaddr;

		(void) mem->Enable6502Reset;   /* copy the code across to the board */
		to = (u_char *)mem;  from = a2232_65EC02code; bcount = sizeof(a2232_65EC02code) - 2;
		start = *(short *)from;
		from += sizeof(start);
		to += start;
		while(bcount--) *to++ = *from++;
		printk("65EC02 software uploaded to the A2232 memory.\n");
  
		mem->Common.Crystal = A2232_UNKNOWN;  /* use automatic speed check */
  
		/* start 6502 running */
		(void) mem->ResetBoard;
		printk("A2232's 65EC02 CPU up and running.\n");
  
		/* wait until speed detector has finished */
		for (bcount = 0; bcount < 2000; bcount++) {
			udelay(1000);
			if (mem->Common.Crystal)
				break;
		}
		printk((mem->Common.Crystal?"A2232 oscillator crystal detected by 65EC02 software: ":"65EC02 software could not determine A2232 oscillator crystal: "));
		switch (mem->Common.Crystal){
		case A2232_UNKNOWN:
			printk("Unknown crystal.\n");
			break;
 		case A2232_NORMAL:
			printk ("Normal crystal.\n");
			break;
		case A2232_TURBO:
			printk ("Turbo crystal.\n");
			break;
		default:
			printk ("0x%x. Huh?\n",mem->Common.Crystal);
		}

		nr_a2232++;

	}	

	printk("Total: %d A2232 boards initialized.\n", nr_a2232); /* Some status report if no card was found */

	a2232_init_portstructs();

	/*
		a2232_init_drivers also registers the drivers. Must be here because all boards
		have to be detected first.
	*/
	if (a2232_init_drivers()) return -ENODEV; // maybe we should use a different -Exxx?

	error = request_irq(IRQ_AMIGA_VERTB, a2232_vbl_inter, 0,
			    "A2232 serial VBL", a2232_driver_ID);
	if (error) {
		for (i = 0; i < nr_a2232; i++)
			zorro_release_device(zd_a2232[i]);
		tty_unregister_driver(a2232_driver);
		put_tty_driver(a2232_driver);
	}
	return error;
}

static void __exit a2232board_exit(void)
{
	int i;

	for (i = 0; i < nr_a2232; i++) {
		zorro_release_device(zd_a2232[i]);
	}

	tty_unregister_driver(a2232_driver);
	put_tty_driver(a2232_driver);
	free_irq(IRQ_AMIGA_VERTB, a2232_driver_ID);
}

module_init(a2232board_init);
module_exit(a2232board_exit);

MODULE_AUTHOR("Enver Haase");
MODULE_DESCRIPTION("Amiga A2232 multi-serial board driver");
MODULE_LICENSE("GPL");
