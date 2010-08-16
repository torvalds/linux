/*
 * LIRC driver for ITE IT8712/IT8705 CIR port
 *
 * Copyright (C) 2001 Hans-Gunter Lutke Uphues <hg_lu@web.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 * ITE IT8705 and IT8712(not tested) and IT8720 CIR-port support for lirc based
 * via cut and paste from lirc_sir.c (C) 2000 Milan Pikula
 *
 * Attention: Sendmode only tested with debugging logs
 *
 * 2001/02/27 Christoph Bartelmus <lirc@bartelmus.de> :
 *   reimplemented read function
 * 2005/06/05 Andrew Calkin implemented support for Asus Digimatrix,
 *   based on work of the following member of the Outertrack Digimatrix
 *   Forum: Art103 <r_tay@hotmail.com>
 * 2009/12/24 James Edwards <jimbo-lirc@edwardsclan.net> implemeted support
 *   for ITE8704/ITE8718, on my machine, the DSDT reports 8704, but the
 *   chip identifies as 18.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <asm/system.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/fcntl.h>

#include <linux/timer.h>
#include <linux/pnp.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>

#include "lirc_it87.h"

#ifdef LIRC_IT87_DIGIMATRIX
static int digimatrix = 1;
static int it87_freq = 36; /* kHz */
static int irq = 9;
#else
static int digimatrix;
static int it87_freq = 38; /* kHz */
static int irq = IT87_CIR_DEFAULT_IRQ;
#endif

static unsigned long it87_bits_in_byte_out;
static unsigned long it87_send_counter;
static unsigned char it87_RXEN_mask = IT87_CIR_RCR_RXEN;

#define RBUF_LEN 1024

#define LIRC_DRIVER_NAME "lirc_it87"

/* timeout for sequences in jiffies (=5/100s) */
/* must be longer than TIME_CONST */
#define IT87_TIMEOUT	(HZ*5/100)

/* module parameters */
static int debug;
#define dprintk(fmt, args...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG LIRC_DRIVER_NAME ": "	\
			       fmt, ## args);			\
	} while (0)

static int io = IT87_CIR_DEFAULT_IOBASE;
/* receiver demodulator default: off */
static int it87_enable_demodulator;

static int timer_enabled;
static DEFINE_SPINLOCK(timer_lock);
static struct timer_list timerlist;
/* time of last signal change detected */
static struct timeval last_tv = {0, 0};
/* time of last UART data ready interrupt */
static struct timeval last_intr_tv = {0, 0};
static int last_value;

static DECLARE_WAIT_QUEUE_HEAD(lirc_read_queue);

static DEFINE_SPINLOCK(hardware_lock);
static DEFINE_SPINLOCK(dev_lock);
static bool device_open;

static int rx_buf[RBUF_LEN];
unsigned int rx_tail, rx_head;

static struct pnp_driver it87_pnp_driver;

/* SECTION: Prototypes */

/* Communication with user-space */
static int lirc_open(struct inode *inode, struct file *file);
static int lirc_close(struct inode *inode, struct file *file);
static unsigned int lirc_poll(struct file *file, poll_table *wait);
static ssize_t lirc_read(struct file *file, char *buf,
			 size_t count, loff_t *ppos);
static ssize_t lirc_write(struct file *file, const char *buf,
			  size_t n, loff_t *pos);
static long lirc_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
static void add_read_queue(int flag, unsigned long val);
static int init_chrdev(void);
static void drop_chrdev(void);
/* Hardware */
static irqreturn_t it87_interrupt(int irq, void *dev_id);
static void send_space(unsigned long len);
static void send_pulse(unsigned long len);
static void init_send(void);
static void terminate_send(unsigned long len);
static int init_hardware(void);
static void drop_hardware(void);
/* Initialisation */
static int init_port(void);
static void drop_port(void);


/* SECTION: Communication with user-space */

static int lirc_open(struct inode *inode, struct file *file)
{
	spin_lock(&dev_lock);
	if (device_open) {
		spin_unlock(&dev_lock);
		return -EBUSY;
	}
	device_open = true;
	spin_unlock(&dev_lock);
	return 0;
}


static int lirc_close(struct inode *inode, struct file *file)
{
	spin_lock(&dev_lock);
	device_open = false;
	spin_unlock(&dev_lock);
	return 0;
}


static unsigned int lirc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &lirc_read_queue, wait);
	if (rx_head != rx_tail)
		return POLLIN | POLLRDNORM;
	return 0;
}


static ssize_t lirc_read(struct file *file, char *buf,
			 size_t count, loff_t *ppos)
{
	int n = 0;
	int retval = 0;

	while (n < count) {
		if (file->f_flags & O_NONBLOCK && rx_head == rx_tail) {
			retval = -EAGAIN;
			break;
		}
		retval = wait_event_interruptible(lirc_read_queue,
						  rx_head != rx_tail);
		if (retval)
			break;

		if (copy_to_user((void *) buf + n, (void *) (rx_buf + rx_head),
				 sizeof(int))) {
			retval = -EFAULT;
			break;
		}
		rx_head = (rx_head + 1) & (RBUF_LEN - 1);
		n += sizeof(int);
	}
	if (n)
		return n;
	return retval;
}


static ssize_t lirc_write(struct file *file, const char *buf,
			  size_t n, loff_t *pos)
{
	int i = 0;
	int *tx_buf;

	if (n % sizeof(int))
		return -EINVAL;
	tx_buf = memdup_user(buf, n);
	if (IS_ERR(tx_buf))
		return PTR_ERR(tx_buf);
	n /= sizeof(int);
	init_send();
	while (1) {
		if (i >= n)
			break;
		if (tx_buf[i])
			send_pulse(tx_buf[i]);
		i++;
		if (i >= n)
			break;
		if (tx_buf[i])
			send_space(tx_buf[i]);
		i++;
	}
	terminate_send(tx_buf[i - 1]);
	return n;
}


static long lirc_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	unsigned long value = 0;
	unsigned int ivalue;
	unsigned long hw_flags;

	if (cmd == LIRC_GET_FEATURES)
		value = LIRC_CAN_SEND_PULSE |
			LIRC_CAN_SET_SEND_CARRIER |
			LIRC_CAN_REC_MODE2;
	else if (cmd == LIRC_GET_SEND_MODE)
		value = LIRC_MODE_PULSE;
	else if (cmd == LIRC_GET_REC_MODE)
		value = LIRC_MODE_MODE2;

	switch (cmd) {
	case LIRC_GET_FEATURES:
	case LIRC_GET_SEND_MODE:
	case LIRC_GET_REC_MODE:
		retval = put_user(value, (unsigned long *) arg);
		break;

	case LIRC_SET_SEND_MODE:
	case LIRC_SET_REC_MODE:
		retval = get_user(value, (unsigned long *) arg);
		break;

	case LIRC_SET_SEND_CARRIER:
		retval = get_user(ivalue, (unsigned int *) arg);
		if (retval)
			return retval;
		ivalue /= 1000;
		if (ivalue > IT87_CIR_FREQ_MAX ||
		    ivalue < IT87_CIR_FREQ_MIN)
			return -EINVAL;

		it87_freq = ivalue;

		spin_lock_irqsave(&hardware_lock, hw_flags);
		outb(((inb(io + IT87_CIR_TCR2) & IT87_CIR_TCR2_TXMPW) |
		      (it87_freq - IT87_CIR_FREQ_MIN) << 3),
		      io + IT87_CIR_TCR2);
		spin_unlock_irqrestore(&hardware_lock, hw_flags);
		dprintk("demodulation frequency: %d kHz\n", it87_freq);

		break;

	default:
		retval = -EINVAL;
	}

	if (retval)
		return retval;

	if (cmd == LIRC_SET_REC_MODE) {
		if (value != LIRC_MODE_MODE2)
			retval = -ENOSYS;
	} else if (cmd == LIRC_SET_SEND_MODE) {
		if (value != LIRC_MODE_PULSE)
			retval = -ENOSYS;
	}
	return retval;
}

static void add_read_queue(int flag, unsigned long val)
{
	unsigned int new_rx_tail;
	int newval;

	dprintk("add flag %d with val %lu\n", flag, val);

	newval = val & PULSE_MASK;

	/*
	 * statistically, pulses are ~TIME_CONST/2 too long. we could
	 * maybe make this more exact, but this is good enough
	 */
	if (flag) {
		/* pulse */
		if (newval > TIME_CONST / 2)
			newval -= TIME_CONST / 2;
		else /* should not ever happen */
			newval = 1;
		newval |= PULSE_BIT;
	} else
		newval += TIME_CONST / 2;
	new_rx_tail = (rx_tail + 1) & (RBUF_LEN - 1);
	if (new_rx_tail == rx_head) {
		dprintk("Buffer overrun.\n");
		return;
	}
	rx_buf[rx_tail] = newval;
	rx_tail = new_rx_tail;
	wake_up_interruptible(&lirc_read_queue);
}


static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.read		= lirc_read,
	.write		= lirc_write,
	.poll		= lirc_poll,
	.unlocked_ioctl	= lirc_ioctl,
	.open		= lirc_open,
	.release	= lirc_close,
};

static int set_use_inc(void *data)
{
       return 0;
}

static void set_use_dec(void *data)
{
}

static struct lirc_driver driver = {
       .name		= LIRC_DRIVER_NAME,
       .minor		= -1,
       .code_length	= 1,
       .sample_rate	= 0,
       .data		= NULL,
       .add_to_buf	= NULL,
       .set_use_inc	= set_use_inc,
       .set_use_dec	= set_use_dec,
       .fops		= &lirc_fops,
       .dev		= NULL,
       .owner		= THIS_MODULE,
};


static int init_chrdev(void)
{
	driver.minor = lirc_register_driver(&driver);

	if (driver.minor < 0) {
		printk(KERN_ERR LIRC_DRIVER_NAME ": init_chrdev() failed.\n");
		return -EIO;
	}
	return 0;
}


static void drop_chrdev(void)
{
	lirc_unregister_driver(driver.minor);
}


/* SECTION: Hardware */
static long delta(struct timeval *tv1, struct timeval *tv2)
{
	unsigned long deltv;

	deltv = tv2->tv_sec - tv1->tv_sec;
	if (deltv > 15)
		deltv = 0xFFFFFF;
	else
		deltv = deltv*1000000 + tv2->tv_usec - tv1->tv_usec;
	return deltv;
}

static void it87_timeout(unsigned long data)
{
	unsigned long flags;

	/* avoid interference with interrupt */
	spin_lock_irqsave(&timer_lock, flags);

	if (digimatrix) {
		/* We have timed out. Disable the RX mechanism. */

		outb((inb(io + IT87_CIR_RCR) & ~IT87_CIR_RCR_RXEN) |
		     IT87_CIR_RCR_RXACT, io + IT87_CIR_RCR);
		if (it87_RXEN_mask)
			outb(inb(io + IT87_CIR_RCR) | IT87_CIR_RCR_RXEN,
			     io + IT87_CIR_RCR);
		dprintk(" TIMEOUT\n");
		timer_enabled = 0;

		/* fifo clear */
		outb(inb(io + IT87_CIR_TCR1) | IT87_CIR_TCR1_FIFOCLR,
		     io+IT87_CIR_TCR1);

	} else {
		/*
		 * if last received signal was a pulse, but receiving stopped
		 * within the 9 bit frame, we need to finish this pulse and
		 * simulate a signal change to from pulse to space. Otherwise
		 * upper layers will receive two sequences next time.
		 */

		if (last_value) {
			unsigned long pulse_end;

			/* determine 'virtual' pulse end: */
			pulse_end = delta(&last_tv, &last_intr_tv);
			dprintk("timeout add %d for %lu usec\n",
				last_value, pulse_end);
			add_read_queue(last_value, pulse_end);
			last_value = 0;
			last_tv = last_intr_tv;
		}
	}
	spin_unlock_irqrestore(&timer_lock, flags);
}

static irqreturn_t it87_interrupt(int irq, void *dev_id)
{
	unsigned char data;
	struct timeval curr_tv;
	static unsigned long deltv;
	unsigned long deltintrtv;
	unsigned long flags, hw_flags;
	int iir, lsr;
	int fifo = 0;
	static char lastbit;
	char bit;

	/* Bit duration in microseconds */
	const unsigned long bit_duration = 1000000ul /
		(115200 / IT87_CIR_BAUDRATE_DIVISOR);


	iir = inb(io + IT87_CIR_IIR);

	switch (iir & IT87_CIR_IIR_IID) {
	case 0x4:
	case 0x6:
		lsr = inb(io + IT87_CIR_RSR) & (IT87_CIR_RSR_RXFTO |
						IT87_CIR_RSR_RXFBC);
		fifo = lsr & IT87_CIR_RSR_RXFBC;
		dprintk("iir: 0x%x fifo: 0x%x\n", iir, lsr);

		/* avoid interference with timer */
		spin_lock_irqsave(&timer_lock, flags);
		spin_lock_irqsave(&hardware_lock, hw_flags);
		if (digimatrix) {
			static unsigned long acc_pulse;
			static unsigned long acc_space;

			do {
				data = inb(io + IT87_CIR_DR);
				data = ~data;
				fifo--;
				if (data != 0x00) {
					if (timer_enabled)
						del_timer(&timerlist);
					/*
					 * start timer for end of
					 * sequence detection
					 */
					timerlist.expires = jiffies +
							    IT87_TIMEOUT;
					add_timer(&timerlist);
					timer_enabled = 1;
				}
				/* Loop through */
				for (bit = 0; bit < 8; ++bit) {
					if ((data >> bit) & 1) {
						++acc_pulse;
						if (lastbit == 0) {
							add_read_queue(0,
								acc_space *
								 bit_duration);
							acc_space = 0;
						}
					} else {
						++acc_space;
						if (lastbit == 1) {
							add_read_queue(1,
								acc_pulse *
								 bit_duration);
							acc_pulse = 0;
						}
					}
					lastbit = (data >> bit) & 1;
				}

			} while (fifo != 0);
		} else { /* Normal Operation */
			do {
				del_timer(&timerlist);
				data = inb(io + IT87_CIR_DR);

				dprintk("data=%02x\n", data);
				do_gettimeofday(&curr_tv);
				deltv = delta(&last_tv, &curr_tv);
				deltintrtv = delta(&last_intr_tv, &curr_tv);

				dprintk("t %lu , d %d\n",
					deltintrtv, (int)data);

				/*
				 * if nothing came in last 2 cycles,
				 * it was gap
				 */
				if (deltintrtv > TIME_CONST * 2) {
					if (last_value) {
						dprintk("GAP\n");

						/* simulate signal change */
						add_read_queue(last_value,
							       deltv -
							       deltintrtv);
						last_value = 0;
						last_tv.tv_sec =
							last_intr_tv.tv_sec;
						last_tv.tv_usec =
							last_intr_tv.tv_usec;
						deltv = deltintrtv;
					}
				}
				data = 1;
				if (data ^ last_value) {
					/*
					 * deltintrtv > 2*TIME_CONST,
					 * remember ? the other case is
					 * timeout
					 */
					add_read_queue(last_value,
						       deltv-TIME_CONST);
					last_value = data;
					last_tv = curr_tv;
					if (last_tv.tv_usec >= TIME_CONST)
						last_tv.tv_usec -= TIME_CONST;
					else {
						last_tv.tv_sec--;
						last_tv.tv_usec += 1000000 -
							TIME_CONST;
					}
				}
				last_intr_tv = curr_tv;
				if (data) {
					/*
					 * start timer for end of
					 * sequence detection
					 */
					timerlist.expires =
						jiffies + IT87_TIMEOUT;
					add_timer(&timerlist);
				}
				outb((inb(io + IT87_CIR_RCR) &
				     ~IT87_CIR_RCR_RXEN) |
				     IT87_CIR_RCR_RXACT,
				     io + IT87_CIR_RCR);
				if (it87_RXEN_mask)
					outb(inb(io + IT87_CIR_RCR) |
					     IT87_CIR_RCR_RXEN,
					     io + IT87_CIR_RCR);
				fifo--;
			} while (fifo != 0);
		}
		spin_unlock_irqrestore(&hardware_lock, hw_flags);
		spin_unlock_irqrestore(&timer_lock, flags);

		return IRQ_RETVAL(IRQ_HANDLED);

	default:
		/* not our irq */
		dprintk("unknown IRQ (shouldn't happen) !!\n");
		return IRQ_RETVAL(IRQ_NONE);
	}
}


static void send_it87(unsigned long len, unsigned long stime,
		      unsigned char send_byte, unsigned int count_bits)
{
	long count = len / stime;
	long time_left = 0;
	static unsigned char byte_out;
	unsigned long hw_flags;

	dprintk("%s: len=%ld, sb=%d\n", __func__, len, send_byte);

	time_left = (long)len - (long)count * (long)stime;
	count += ((2 * time_left) / stime);
	while (count) {
		long i = 0;
		for (i = 0; i < count_bits; i++) {
			byte_out = (byte_out << 1) | (send_byte & 1);
			it87_bits_in_byte_out++;
		}
		if (it87_bits_in_byte_out == 8) {
			dprintk("out=0x%x, tsr_txfbc: 0x%x\n",
				byte_out,
				inb(io + IT87_CIR_TSR) &
				IT87_CIR_TSR_TXFBC);

			while ((inb(io + IT87_CIR_TSR) &
				IT87_CIR_TSR_TXFBC) >= IT87_CIR_FIFO_SIZE)
				;

			spin_lock_irqsave(&hardware_lock, hw_flags);
			outb(byte_out, io + IT87_CIR_DR);
			spin_unlock_irqrestore(&hardware_lock, hw_flags);

			it87_bits_in_byte_out = 0;
			it87_send_counter++;
			byte_out = 0;
		}
		count--;
	}
}


/*TODO: maybe exchange space and pulse because it8705 only modulates 0-bits */

static void send_space(unsigned long len)
{
	send_it87(len, TIME_CONST, IT87_CIR_SPACE, IT87_CIR_BAUDRATE_DIVISOR);
}

static void send_pulse(unsigned long len)
{
	send_it87(len, TIME_CONST, IT87_CIR_PULSE, IT87_CIR_BAUDRATE_DIVISOR);
}


static void init_send()
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);
	/* RXEN=0: receiver disable */
	it87_RXEN_mask = 0;
	outb(inb(io + IT87_CIR_RCR) & ~IT87_CIR_RCR_RXEN,
	     io + IT87_CIR_RCR);
	spin_unlock_irqrestore(&hardware_lock, flags);
	it87_bits_in_byte_out = 0;
	it87_send_counter = 0;
}


static void terminate_send(unsigned long len)
{
	unsigned long flags;
	unsigned long last = 0;

	last = it87_send_counter;
	/* make sure all necessary data has been sent */
	while (last == it87_send_counter)
		send_space(len);
	/* wait until all data sent */
	while ((inb(io + IT87_CIR_TSR) & IT87_CIR_TSR_TXFBC) != 0)
		;
	/* then re-enable receiver */
	spin_lock_irqsave(&hardware_lock, flags);
	it87_RXEN_mask = IT87_CIR_RCR_RXEN;
	outb(inb(io + IT87_CIR_RCR) | IT87_CIR_RCR_RXEN,
	     io + IT87_CIR_RCR);
	spin_unlock_irqrestore(&hardware_lock, flags);
}


static int init_hardware(void)
{
	unsigned long flags;
	unsigned char it87_rcr = 0;

	spin_lock_irqsave(&hardware_lock, flags);
	/* init cir-port */
	/* enable r/w-access to Baudrate-Register */
	outb(IT87_CIR_IER_BR, io + IT87_CIR_IER);
	outb(IT87_CIR_BAUDRATE_DIVISOR % 0x100, io+IT87_CIR_BDLR);
	outb(IT87_CIR_BAUDRATE_DIVISOR / 0x100, io+IT87_CIR_BDHR);
	/* Baudrate Register off, define IRQs: Input only */
	if (digimatrix) {
		outb(IT87_CIR_IER_IEC | IT87_CIR_IER_RFOIE, io + IT87_CIR_IER);
		/* RX: HCFS=0, RXDCR = 001b (33,75..38,25 kHz), RXEN=1 */
	} else {
		outb(IT87_CIR_IER_IEC | IT87_CIR_IER_RDAIE, io + IT87_CIR_IER);
		/* RX: HCFS=0, RXDCR = 001b (35,6..40,3 kHz), RXEN=1 */
	}
	it87_rcr = (IT87_CIR_RCR_RXEN & it87_RXEN_mask) | 0x1;
	if (it87_enable_demodulator)
		it87_rcr |= IT87_CIR_RCR_RXEND;
	outb(it87_rcr, io + IT87_CIR_RCR);
	if (digimatrix) {
		/* Set FIFO depth to 1 byte, and disable TX */
		outb(inb(io + IT87_CIR_TCR1) |  0x00,
		     io + IT87_CIR_TCR1);

		/*
		 * TX: it87_freq (36kHz), 'reserved' sensitivity
		 * setting (0x00)
		 */
		outb(((it87_freq - IT87_CIR_FREQ_MIN) << 3) | 0x00,
		     io + IT87_CIR_TCR2);
	} else {
		/* TX: 38kHz, 13,3us (pulse-width) */
		outb(((it87_freq - IT87_CIR_FREQ_MIN) << 3) | 0x06,
		     io + IT87_CIR_TCR2);
	}
	spin_unlock_irqrestore(&hardware_lock, flags);
	return 0;
}


static void drop_hardware(void)
{
	unsigned long flags;

	spin_lock_irqsave(&hardware_lock, flags);
	disable_irq(irq);
	/* receiver disable */
	it87_RXEN_mask = 0;
	outb(0x1, io + IT87_CIR_RCR);
	/* turn off irqs */
	outb(0, io + IT87_CIR_IER);
	/* fifo clear */
	outb(IT87_CIR_TCR1_FIFOCLR, io+IT87_CIR_TCR1);
	/* reset */
	outb(IT87_CIR_IER_RESET, io+IT87_CIR_IER);
	enable_irq(irq);
	spin_unlock_irqrestore(&hardware_lock, flags);
}


static unsigned char it87_read(unsigned char port)
{
	outb(port, IT87_ADRPORT);
	return inb(IT87_DATAPORT);
}


static void it87_write(unsigned char port, unsigned char data)
{
	outb(port, IT87_ADRPORT);
	outb(data, IT87_DATAPORT);
}


/* SECTION: Initialisation */

static int init_port(void)
{
	unsigned long hw_flags;
	int retval = 0;

	unsigned char init_bytes[4] = IT87_INIT;
	unsigned char it87_chipid = 0;
	unsigned char ldn = 0;
	unsigned int  it87_io = 0;
	unsigned int  it87_irq = 0;

	/* Enter MB PnP Mode */
	outb(init_bytes[0], IT87_ADRPORT);
	outb(init_bytes[1], IT87_ADRPORT);
	outb(init_bytes[2], IT87_ADRPORT);
	outb(init_bytes[3], IT87_ADRPORT);

	/* 8712 or 8705 ? */
	it87_chipid = it87_read(IT87_CHIP_ID1);
	if (it87_chipid != 0x87) {
		retval = -ENXIO;
		return retval;
	}
	it87_chipid = it87_read(IT87_CHIP_ID2);
	if ((it87_chipid != 0x05) &&
		(it87_chipid != 0x12) &&
		(it87_chipid != 0x18) &&
		(it87_chipid != 0x20)) {
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": no IT8704/05/12/18/20 found (claimed IT87%02x), "
		       "exiting..\n", it87_chipid);
		retval = -ENXIO;
		return retval;
	}
	printk(KERN_INFO LIRC_DRIVER_NAME
	       ": found IT87%02x.\n",
	       it87_chipid);

	/* get I/O-Port and IRQ */
	if (it87_chipid == 0x12 || it87_chipid == 0x18)
		ldn = IT8712_CIR_LDN;
	else
		ldn = IT8705_CIR_LDN;
	it87_write(IT87_LDN, ldn);

	it87_io = it87_read(IT87_CIR_BASE_MSB) * 256 +
		  it87_read(IT87_CIR_BASE_LSB);
	if (it87_io == 0) {
		if (io == 0)
			io = IT87_CIR_DEFAULT_IOBASE;
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": set default io 0x%x\n",
		       io);
		it87_write(IT87_CIR_BASE_MSB, io / 0x100);
		it87_write(IT87_CIR_BASE_LSB, io % 0x100);
	} else
		io = it87_io;

	it87_irq = it87_read(IT87_CIR_IRQ);
	if (digimatrix || it87_irq == 0) {
		if (irq == 0)
			irq = IT87_CIR_DEFAULT_IRQ;
		printk(KERN_INFO LIRC_DRIVER_NAME
		       ": set default irq 0x%x\n",
		       irq);
		it87_write(IT87_CIR_IRQ, irq);
	} else
		irq = it87_irq;

	spin_lock_irqsave(&hardware_lock, hw_flags);
	/* reset */
	outb(IT87_CIR_IER_RESET, io+IT87_CIR_IER);
	/* fifo clear */
	outb(IT87_CIR_TCR1_FIFOCLR |
	     /*	     IT87_CIR_TCR1_ILE | */
	     IT87_CIR_TCR1_TXRLE |
	     IT87_CIR_TCR1_TXENDF, io+IT87_CIR_TCR1);
	spin_unlock_irqrestore(&hardware_lock, hw_flags);

	/* get I/O port access and IRQ line */
	if (request_region(io, 8, LIRC_DRIVER_NAME) == NULL) {
		printk(KERN_ERR LIRC_DRIVER_NAME
		       ": i/o port 0x%.4x already in use.\n", io);
		/* Leaving MB PnP Mode */
		it87_write(IT87_CFGCTRL, 0x2);
		return -EBUSY;
	}

	/* activate CIR-Device */
	it87_write(IT87_CIR_ACT, 0x1);

	/* Leaving MB PnP Mode */
	it87_write(IT87_CFGCTRL, 0x2);

	retval = request_irq(irq, it87_interrupt, 0 /*IRQF_DISABLED*/,
			     LIRC_DRIVER_NAME, NULL);
	if (retval < 0) {
		printk(KERN_ERR LIRC_DRIVER_NAME
		       ": IRQ %d already in use.\n",
		       irq);
		release_region(io, 8);
		return retval;
	}

	printk(KERN_INFO LIRC_DRIVER_NAME
	       ": I/O port 0x%.4x, IRQ %d.\n", io, irq);

	init_timer(&timerlist);
	timerlist.function = it87_timeout;
	timerlist.data = 0xabadcafe;

	return 0;
}


static void drop_port(void)
{
#if 0
	unsigned char init_bytes[4] = IT87_INIT;

	/* Enter MB PnP Mode */
	outb(init_bytes[0], IT87_ADRPORT);
	outb(init_bytes[1], IT87_ADRPORT);
	outb(init_bytes[2], IT87_ADRPORT);
	outb(init_bytes[3], IT87_ADRPORT);

	/* deactivate CIR-Device */
	it87_write(IT87_CIR_ACT, 0x0);

	/* Leaving MB PnP Mode */
	it87_write(IT87_CFGCTRL, 0x2);
#endif

	del_timer_sync(&timerlist);
	free_irq(irq, NULL);
	release_region(io, 8);
}


static int init_lirc_it87(void)
{
	int retval;

	init_waitqueue_head(&lirc_read_queue);
	retval = init_port();
	if (retval < 0)
		return retval;
	init_hardware();
	printk(KERN_INFO LIRC_DRIVER_NAME ": Installed.\n");
	return 0;
}

static int it87_probe(struct pnp_dev *pnp_dev,
		      const struct pnp_device_id *dev_id)
{
	int retval;

	driver.dev = &pnp_dev->dev;

	retval = init_chrdev();
	if (retval < 0)
		return retval;

	retval = init_lirc_it87();
	if (retval)
		goto init_lirc_it87_failed;

	return 0;

init_lirc_it87_failed:
	drop_chrdev();

	return retval;
}

static int __init lirc_it87_init(void)
{
	return pnp_register_driver(&it87_pnp_driver);
}


static void __exit lirc_it87_exit(void)
{
	drop_hardware();
	drop_chrdev();
	drop_port();
	pnp_unregister_driver(&it87_pnp_driver);
	printk(KERN_INFO LIRC_DRIVER_NAME ": Uninstalled.\n");
}

/* SECTION: PNP for ITE8704/18 */

static const struct pnp_device_id pnp_dev_table[] = {
	{"ITE8704", 0},
	{}
};

MODULE_DEVICE_TABLE(pnp, pnp_dev_table);

static struct pnp_driver it87_pnp_driver = {
	.name           = LIRC_DRIVER_NAME,
	.id_table       = pnp_dev_table,
	.probe		= it87_probe,
};

module_init(lirc_it87_init);
module_exit(lirc_it87_exit);

MODULE_DESCRIPTION("LIRC driver for ITE IT8704/05/12/18/20 CIR port");
MODULE_AUTHOR("Hans-Gunter Lutke Uphues");
MODULE_LICENSE("GPL");

module_param(io, int, S_IRUGO);
MODULE_PARM_DESC(io, "I/O base address (default: 0x310)");

module_param(irq, int, S_IRUGO);
#ifdef LIRC_IT87_DIGIMATRIX
MODULE_PARM_DESC(irq, "Interrupt (1,3-12) (default: 9)");
#else
MODULE_PARM_DESC(irq, "Interrupt (1,3-12) (default: 7)");
#endif

module_param(it87_enable_demodulator, bool, S_IRUGO);
MODULE_PARM_DESC(it87_enable_demodulator,
		 "Receiver demodulator enable/disable (1/0), default: 0");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");

module_param(digimatrix, bool, S_IRUGO | S_IWUSR);
#ifdef LIRC_IT87_DIGIMATRIX
MODULE_PARM_DESC(digimatrix,
	"Asus Digimatrix it87 compat. enable/disable (1/0), default: 1");
#else
MODULE_PARM_DESC(digimatrix,
	"Asus Digimatrix it87 compat. enable/disable (1/0), default: 0");
#endif


module_param(it87_freq, int, S_IRUGO);
#ifdef LIRC_IT87_DIGIMATRIX
MODULE_PARM_DESC(it87_freq,
    "Carrier demodulator frequency (kHz), (default: 36)");
#else
MODULE_PARM_DESC(it87_freq,
    "Carrier demodulator frequency (kHz), (default: 38)");
#endif
