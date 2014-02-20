/*
 * lirc_serial.c
 *
 * lirc_serial - Device driver that records pulse- and pause-lengths
 *	       (space-lengths) between DDCD event on a serial port.
 *
 * Copyright (C) 1996,97 Ralph Metzler <rjkm@thp.uni-koeln.de>
 * Copyright (C) 1998 Trent Piepho <xyzzy@u.washington.edu>
 * Copyright (C) 1998 Ben Pfaff <blp@gnu.org>
 * Copyright (C) 1999 Christoph Bartelmus <lirc@bartelmus.de>
 * Copyright (C) 2007 Andrei Tanas <andrei@tanas.ca> (suspend/resume support)
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/*
 * Steve's changes to improve transmission fidelity:
 *   - for systems with the rdtsc instruction and the clock counter, a
 *     send_pule that times the pulses directly using the counter.
 *     This means that the LIRC_SERIAL_TRANSMITTER_LATENCY fudge is
 *     not needed. Measurement shows very stable waveform, even where
 *     PCI activity slows the access to the UART, which trips up other
 *     versions.
 *   - For other system, non-integer-microsecond pulse/space lengths,
 *     done using fixed point binary. So, much more accurate carrier
 *     frequency.
 *   - fine tuned transmitter latency, taking advantage of fractional
 *     microseconds in previous change
 *   - Fixed bug in the way transmitter latency was accounted for by
 *     tuning the pulse lengths down - the send_pulse routine ignored
 *     this overhead as it timed the overall pulse length - so the
 *     pulse frequency was right but overall pulse length was too
 *     long. Fixed by accounting for latency on each pulse/space
 *     iteration.
 *
 * Steve Davies <steve@daviesfam.org>  July 2001
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/serial_reg.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/fcntl.h>
#include <linux/spinlock.h>

#ifdef CONFIG_LIRC_SERIAL_NSLU2
#include <asm/hardware.h>
#endif
/* From Intel IXP42X Developer's Manual (#252480-005): */
/* ftp://download.intel.com/design/network/manuals/25248005.pdf */
#define UART_IE_IXP42X_UUE   0x40 /* IXP42X UART Unit enable */
#define UART_IE_IXP42X_RTOIE 0x10 /* IXP42X Receiver Data Timeout int.enable */

#include <media/lirc.h>
#include <media/lirc_dev.h>

#define LIRC_DRIVER_NAME "lirc_serial"

struct lirc_serial {
	int signal_pin;
	int signal_pin_change;
	u8 on;
	u8 off;
	long (*send_pulse)(unsigned long length);
	void (*send_space)(long length);
	int features;
	spinlock_t lock;
};

#define LIRC_HOMEBREW		0
#define LIRC_IRDEO		1
#define LIRC_IRDEO_REMOTE	2
#define LIRC_ANIMAX		3
#define LIRC_IGOR		4
#define LIRC_NSLU2		5

/*** module parameters ***/
static int type;
static int io;
static int irq;
static bool iommap;
static int ioshift;
static bool softcarrier = 1;
static bool share_irq;
static bool debug;
static int sense = -1;	/* -1 = auto, 0 = active high, 1 = active low */
static bool txsense;	/* 0 = active high, 1 = active low */

#define dprintk(fmt, args...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG LIRC_DRIVER_NAME ": "	\
			       fmt, ## args);			\
	} while (0)

/* forward declarations */
static long send_pulse_irdeo(unsigned long length);
static long send_pulse_homebrew(unsigned long length);
static void send_space_irdeo(long length);
static void send_space_homebrew(long length);

static struct lirc_serial hardware[] = {
	[LIRC_HOMEBREW] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_HOMEBREW].lock),
		.signal_pin        = UART_MSR_DCD,
		.signal_pin_change = UART_MSR_DDCD,
		.on  = (UART_MCR_RTS | UART_MCR_OUT2 | UART_MCR_DTR),
		.off = (UART_MCR_RTS | UART_MCR_OUT2),
		.send_pulse = send_pulse_homebrew,
		.send_space = send_space_homebrew,
#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
		.features    = (LIRC_CAN_SET_SEND_DUTY_CYCLE |
				LIRC_CAN_SET_SEND_CARRIER |
				LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2)
#else
		.features    = LIRC_CAN_REC_MODE2
#endif
	},

	[LIRC_IRDEO] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_IRDEO].lock),
		.signal_pin        = UART_MSR_DSR,
		.signal_pin_change = UART_MSR_DDSR,
		.on  = UART_MCR_OUT2,
		.off = (UART_MCR_RTS | UART_MCR_DTR | UART_MCR_OUT2),
		.send_pulse  = send_pulse_irdeo,
		.send_space  = send_space_irdeo,
		.features    = (LIRC_CAN_SET_SEND_DUTY_CYCLE |
				LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2)
	},

	[LIRC_IRDEO_REMOTE] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_IRDEO_REMOTE].lock),
		.signal_pin        = UART_MSR_DSR,
		.signal_pin_change = UART_MSR_DDSR,
		.on  = (UART_MCR_RTS | UART_MCR_DTR | UART_MCR_OUT2),
		.off = (UART_MCR_RTS | UART_MCR_DTR | UART_MCR_OUT2),
		.send_pulse  = send_pulse_irdeo,
		.send_space  = send_space_irdeo,
		.features    = (LIRC_CAN_SET_SEND_DUTY_CYCLE |
				LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2)
	},

	[LIRC_ANIMAX] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_ANIMAX].lock),
		.signal_pin        = UART_MSR_DCD,
		.signal_pin_change = UART_MSR_DDCD,
		.on  = 0,
		.off = (UART_MCR_RTS | UART_MCR_DTR | UART_MCR_OUT2),
		.send_pulse = NULL,
		.send_space = NULL,
		.features   = LIRC_CAN_REC_MODE2
	},

	[LIRC_IGOR] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_IGOR].lock),
		.signal_pin        = UART_MSR_DSR,
		.signal_pin_change = UART_MSR_DDSR,
		.on  = (UART_MCR_RTS | UART_MCR_OUT2 | UART_MCR_DTR),
		.off = (UART_MCR_RTS | UART_MCR_OUT2),
		.send_pulse = send_pulse_homebrew,
		.send_space = send_space_homebrew,
#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
		.features    = (LIRC_CAN_SET_SEND_DUTY_CYCLE |
				LIRC_CAN_SET_SEND_CARRIER |
				LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2)
#else
		.features    = LIRC_CAN_REC_MODE2
#endif
	},

#ifdef CONFIG_LIRC_SERIAL_NSLU2
	/*
	 * Modified Linksys Network Storage Link USB 2.0 (NSLU2):
	 * We receive on CTS of the 2nd serial port (R142,LHS), we
	 * transmit with a IR diode between GPIO[1] (green status LED),
	 * and ground (Matthias Goebl <matthias.goebl@goebl.net>).
	 * See also http://www.nslu2-linux.org for this device
	 */
	[LIRC_NSLU2] = {
		.lock = __SPIN_LOCK_UNLOCKED(hardware[LIRC_NSLU2].lock),
		.signal_pin        = UART_MSR_CTS,
		.signal_pin_change = UART_MSR_DCTS,
		.on  = (UART_MCR_RTS | UART_MCR_OUT2 | UART_MCR_DTR),
		.off = (UART_MCR_RTS | UART_MCR_OUT2),
		.send_pulse = send_pulse_homebrew,
		.send_space = send_space_homebrew,
#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
		.features    = (LIRC_CAN_SET_SEND_DUTY_CYCLE |
				LIRC_CAN_SET_SEND_CARRIER |
				LIRC_CAN_SEND_PULSE | LIRC_CAN_REC_MODE2)
#else
		.features    = LIRC_CAN_REC_MODE2
#endif
	},
#endif

};

#define RS_ISR_PASS_LIMIT 256

/*
 * A long pulse code from a remote might take up to 300 bytes.  The
 * daemon should read the bytes as soon as they are generated, so take
 * the number of keys you think you can push before the daemon runs
 * and multiply by 300.  The driver will warn you if you overrun this
 * buffer.  If you have a slow computer or non-busmastering IDE disks,
 * maybe you will need to increase this.
 */

/* This MUST be a power of two!  It has to be larger than 1 as well. */

#define RBUF_LEN 256

static struct timeval lasttv = {0, 0};

static struct lirc_buffer rbuf;

static unsigned int freq = 38000;
static unsigned int duty_cycle = 50;

/* Initialized in init_timing_params() */
static unsigned long period;
static unsigned long pulse_width;
static unsigned long space_width;

#if defined(__i386__)
/*
 * From:
 * Linux I/O port programming mini-HOWTO
 * Author: Riku Saikkonen <Riku.Saikkonen@hut.fi>
 * v, 28 December 1997
 *
 * [...]
 * Actually, a port I/O instruction on most ports in the 0-0x3ff range
 * takes almost exactly 1 microsecond, so if you're, for example, using
 * the parallel port directly, just do additional inb()s from that port
 * to delay.
 * [...]
 */
/* transmitter latency 1.5625us 0x1.90 - this figure arrived at from
 * comment above plus trimming to match actual measured frequency.
 * This will be sensitive to cpu speed, though hopefully most of the 1.5us
 * is spent in the uart access.  Still - for reference test machine was a
 * 1.13GHz Athlon system - Steve
 */

/*
 * changed from 400 to 450 as this works better on slower machines;
 * faster machines will use the rdtsc code anyway
 */
#define LIRC_SERIAL_TRANSMITTER_LATENCY 450

#else

/* does anybody have information on other platforms ? */
/* 256 = 1<<8 */
#define LIRC_SERIAL_TRANSMITTER_LATENCY 256

#endif  /* __i386__ */
/*
 * FIXME: should we be using hrtimers instead of this
 * LIRC_SERIAL_TRANSMITTER_LATENCY nonsense?
 */

/* fetch serial input packet (1 byte) from register offset */
static u8 sinp(int offset)
{
	if (iommap != 0)
		/* the register is memory-mapped */
		offset <<= ioshift;

	return inb(io + offset);
}

/* write serial output packet (1 byte) of value to register offset */
static void soutp(int offset, u8 value)
{
	if (iommap != 0)
		/* the register is memory-mapped */
		offset <<= ioshift;

	outb(value, io + offset);
}

static void on(void)
{
#ifdef CONFIG_LIRC_SERIAL_NSLU2
	/*
	 * On NSLU2, we put the transmit diode between the output of the green
	 * status LED and ground
	 */
	if (type == LIRC_NSLU2) {
		gpio_set_value(NSLU2_LED_GRN, 0);
		return;
	}
#endif
	if (txsense)
		soutp(UART_MCR, hardware[type].off);
	else
		soutp(UART_MCR, hardware[type].on);
}

static void off(void)
{
#ifdef CONFIG_LIRC_SERIAL_NSLU2
	if (type == LIRC_NSLU2) {
		gpio_set_value(NSLU2_LED_GRN, 1);
		return;
	}
#endif
	if (txsense)
		soutp(UART_MCR, hardware[type].on);
	else
		soutp(UART_MCR, hardware[type].off);
}

#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_US 5000
#else
#define MAX_UDELAY_US (MAX_UDELAY_MS*1000)
#endif

static void safe_udelay(unsigned long usecs)
{
	while (usecs > MAX_UDELAY_US) {
		udelay(MAX_UDELAY_US);
		usecs -= MAX_UDELAY_US;
	}
	udelay(usecs);
}

#ifdef USE_RDTSC
/*
 * This is an overflow/precision juggle, complicated in that we can't
 * do long long divide in the kernel
 */

/*
 * When we use the rdtsc instruction to measure clocks, we keep the
 * pulse and space widths as clock cycles.  As this is CPU speed
 * dependent, the widths must be calculated in init_port and ioctl
 * time
 */

/* So send_pulse can quickly convert microseconds to clocks */
static unsigned long conv_us_to_clocks;

static int init_timing_params(unsigned int new_duty_cycle,
		unsigned int new_freq)
{
	__u64 loops_per_sec, work;

	duty_cycle = new_duty_cycle;
	freq = new_freq;

	loops_per_sec = __this_cpu_read(cpu.info.loops_per_jiffy);
	loops_per_sec *= HZ;

	/* How many clocks in a microsecond?, avoiding long long divide */
	work = loops_per_sec;
	work *= 4295;  /* 4295 = 2^32 / 1e6 */
	conv_us_to_clocks = (work >> 32);

	/*
	 * Carrier period in clocks, approach good up to 32GHz clock,
	 * gets carrier frequency within 8Hz
	 */
	period = loops_per_sec >> 3;
	period /= (freq >> 3);

	/* Derive pulse and space from the period */
	pulse_width = period * duty_cycle / 100;
	space_width = period - pulse_width;
	dprintk("in init_timing_params, freq=%d, duty_cycle=%d, "
		"clk/jiffy=%ld, pulse=%ld, space=%ld, "
		"conv_us_to_clocks=%ld\n",
		freq, duty_cycle, __this_cpu_read(cpu_info.loops_per_jiffy),
		pulse_width, space_width, conv_us_to_clocks);
	return 0;
}
#else /* ! USE_RDTSC */
static int init_timing_params(unsigned int new_duty_cycle,
		unsigned int new_freq)
{
/*
 * period, pulse/space width are kept with 8 binary places -
 * IE multiplied by 256.
 */
	if (256 * 1000000L / new_freq * new_duty_cycle / 100 <=
	    LIRC_SERIAL_TRANSMITTER_LATENCY)
		return -EINVAL;
	if (256 * 1000000L / new_freq * (100 - new_duty_cycle) / 100 <=
	    LIRC_SERIAL_TRANSMITTER_LATENCY)
		return -EINVAL;
	duty_cycle = new_duty_cycle;
	freq = new_freq;
	period = 256 * 1000000L / freq;
	pulse_width = period * duty_cycle / 100;
	space_width = period - pulse_width;
	dprintk("in init_timing_params, freq=%d pulse=%ld, space=%ld\n",
		freq, pulse_width, space_width);
	return 0;
}
#endif /* USE_RDTSC */


/* return value: space length delta */

static long send_pulse_irdeo(unsigned long length)
{
	long rawbits, ret;
	int i;
	unsigned char output;
	unsigned char chunk, shifted;

	/* how many bits have to be sent ? */
	rawbits = length * 1152 / 10000;
	if (duty_cycle > 50)
		chunk = 3;
	else
		chunk = 1;
	for (i = 0, output = 0x7f; rawbits > 0; rawbits -= 3) {
		shifted = chunk << (i * 3);
		shifted >>= 1;
		output &= (~shifted);
		i++;
		if (i == 3) {
			soutp(UART_TX, output);
			while (!(sinp(UART_LSR) & UART_LSR_THRE))
				;
			output = 0x7f;
			i = 0;
		}
	}
	if (i != 0) {
		soutp(UART_TX, output);
		while (!(sinp(UART_LSR) & UART_LSR_TEMT))
			;
	}

	if (i == 0)
		ret = (-rawbits) * 10000 / 1152;
	else
		ret = (3 - i) * 3 * 10000 / 1152 + (-rawbits) * 10000 / 1152;

	return ret;
}

#ifdef USE_RDTSC
/* Version that uses Pentium rdtsc instruction to measure clocks */

/*
 * This version does sub-microsecond timing using rdtsc instruction,
 * and does away with the fudged LIRC_SERIAL_TRANSMITTER_LATENCY
 * Implicitly i586 architecture...  - Steve
 */

static long send_pulse_homebrew_softcarrier(unsigned long length)
{
	int flag;
	unsigned long target, start, now;

	/* Get going quick as we can */
	rdtscl(start);
	on();
	/* Convert length from microseconds to clocks */
	length *= conv_us_to_clocks;
	/* And loop till time is up - flipping at right intervals */
	now = start;
	target = pulse_width;
	flag = 1;
	/*
	 * FIXME: This looks like a hard busy wait, without even an occasional,
	 * polite, cpu_relax() call.  There's got to be a better way?
	 *
	 * The i2c code has the result of a lot of bit-banging work, I wonder if
	 * there's something there which could be helpful here.
	 */
	while ((now - start) < length) {
		/* Delay till flip time */
		do {
			rdtscl(now);
		} while ((now - start) < target);

		/* flip */
		if (flag) {
			rdtscl(now);
			off();
			target += space_width;
		} else {
			rdtscl(now); on();
			target += pulse_width;
		}
		flag = !flag;
	}
	rdtscl(now);
	return ((now - start) - length) / conv_us_to_clocks;
}
#else /* ! USE_RDTSC */
/* Version using udelay() */

/*
 * here we use fixed point arithmetic, with 8
 * fractional bits.  that gets us within 0.1% or so of the right average
 * frequency, albeit with some jitter in pulse length - Steve
 */

/* To match 8 fractional bits used for pulse/space length */

static long send_pulse_homebrew_softcarrier(unsigned long length)
{
	int flag;
	unsigned long actual, target, d;
	length <<= 8;

	actual = 0; target = 0; flag = 0;
	while (actual < length) {
		if (flag) {
			off();
			target += space_width;
		} else {
			on();
			target += pulse_width;
		}
		d = (target - actual -
		     LIRC_SERIAL_TRANSMITTER_LATENCY + 128) >> 8;
		/*
		 * Note - we've checked in ioctl that the pulse/space
		 * widths are big enough so that d is > 0
		 */
		udelay(d);
		actual += (d << 8) + LIRC_SERIAL_TRANSMITTER_LATENCY;
		flag = !flag;
	}
	return (actual-length) >> 8;
}
#endif /* USE_RDTSC */

static long send_pulse_homebrew(unsigned long length)
{
	if (length <= 0)
		return 0;

	if (softcarrier)
		return send_pulse_homebrew_softcarrier(length);
	else {
		on();
		safe_udelay(length);
		return 0;
	}
}

static void send_space_irdeo(long length)
{
	if (length <= 0)
		return;

	safe_udelay(length);
}

static void send_space_homebrew(long length)
{
	off();
	if (length <= 0)
		return;
	safe_udelay(length);
}

static void rbwrite(int l)
{
	if (lirc_buffer_full(&rbuf)) {
		/* no new signals will be accepted */
		dprintk("Buffer overrun\n");
		return;
	}
	lirc_buffer_write(&rbuf, (void *)&l);
}

static void frbwrite(int l)
{
	/* simple noise filter */
	static int pulse, space;
	static unsigned int ptr;

	if (ptr > 0 && (l & PULSE_BIT)) {
		pulse += l & PULSE_MASK;
		if (pulse > 250) {
			rbwrite(space);
			rbwrite(pulse | PULSE_BIT);
			ptr = 0;
			pulse = 0;
		}
		return;
	}
	if (!(l & PULSE_BIT)) {
		if (ptr == 0) {
			if (l > 20000) {
				space = l;
				ptr++;
				return;
			}
		} else {
			if (l > 20000) {
				space += pulse;
				if (space > PULSE_MASK)
					space = PULSE_MASK;
				space += l;
				if (space > PULSE_MASK)
					space = PULSE_MASK;
				pulse = 0;
				return;
			}
			rbwrite(space);
			rbwrite(pulse | PULSE_BIT);
			ptr = 0;
			pulse = 0;
		}
	}
	rbwrite(l);
}

static irqreturn_t lirc_irq_handler(int i, void *blah)
{
	struct timeval tv;
	int counter, dcd;
	u8 status;
	long deltv;
	int data;
	static int last_dcd = -1;

	if ((sinp(UART_IIR) & UART_IIR_NO_INT)) {
		/* not our interrupt */
		return IRQ_NONE;
	}

	counter = 0;
	do {
		counter++;
		status = sinp(UART_MSR);
		if (counter > RS_ISR_PASS_LIMIT) {
			pr_warn("AIEEEE: We're caught!\n");
			break;
		}
		if ((status & hardware[type].signal_pin_change)
		    && sense != -1) {
			/* get current time */
			do_gettimeofday(&tv);

			/* New mode, written by Trent Piepho
			   <xyzzy@u.washington.edu>. */

			/*
			 * The old format was not very portable.
			 * We now use an int to pass pulses
			 * and spaces to user space.
			 *
			 * If PULSE_BIT is set a pulse has been
			 * received, otherwise a space has been
			 * received.  The driver needs to know if your
			 * receiver is active high or active low, or
			 * the space/pulse sense could be
			 * inverted. The bits denoted by PULSE_MASK are
			 * the length in microseconds. Lengths greater
			 * than or equal to 16 seconds are clamped to
			 * PULSE_MASK.  All other bits are unused.
			 * This is a much simpler interface for user
			 * programs, as well as eliminating "out of
			 * phase" errors with space/pulse
			 * autodetection.
			 */

			/* calc time since last interrupt in microseconds */
			dcd = (status & hardware[type].signal_pin) ? 1 : 0;

			if (dcd == last_dcd) {
				pr_warn("ignoring spike: %d %d %lx %lx %lx %lx\n",
					dcd, sense,
					tv.tv_sec, lasttv.tv_sec,
					(unsigned long)tv.tv_usec,
					(unsigned long)lasttv.tv_usec);
				continue;
			}

			deltv = tv.tv_sec-lasttv.tv_sec;
			if (tv.tv_sec < lasttv.tv_sec ||
			    (tv.tv_sec == lasttv.tv_sec &&
			     tv.tv_usec < lasttv.tv_usec)) {
				pr_warn("AIEEEE: your clock just jumped backwards\n");
				pr_warn("%d %d %lx %lx %lx %lx\n",
					dcd, sense,
					tv.tv_sec, lasttv.tv_sec,
					(unsigned long)tv.tv_usec,
					(unsigned long)lasttv.tv_usec);
				data = PULSE_MASK;
			} else if (deltv > 15) {
				data = PULSE_MASK; /* really long time */
				if (!(dcd^sense)) {
					/* sanity check */
					pr_warn("AIEEEE: %d %d %lx %lx %lx %lx\n",
						dcd, sense,
						tv.tv_sec, lasttv.tv_sec,
						(unsigned long)tv.tv_usec,
						(unsigned long)lasttv.tv_usec);
					/*
					 * detecting pulse while this
					 * MUST be a space!
					 */
					sense = sense ? 0 : 1;
				}
			} else
				data = (int) (deltv*1000000 +
					       tv.tv_usec -
					       lasttv.tv_usec);
			frbwrite(dcd^sense ? data : (data|PULSE_BIT));
			lasttv = tv;
			last_dcd = dcd;
			wake_up_interruptible(&rbuf.wait_poll);
		}
	} while (!(sinp(UART_IIR) & UART_IIR_NO_INT)); /* still pending ? */
	return IRQ_HANDLED;
}


static int hardware_init_port(void)
{
	u8 scratch, scratch2, scratch3;

	/*
	 * This is a simple port existence test, borrowed from the autoconfig
	 * function in drivers/serial/8250.c
	 */
	scratch = sinp(UART_IER);
	soutp(UART_IER, 0);
#ifdef __i386__
	outb(0xff, 0x080);
#endif
	scratch2 = sinp(UART_IER) & 0x0f;
	soutp(UART_IER, 0x0f);
#ifdef __i386__
	outb(0x00, 0x080);
#endif
	scratch3 = sinp(UART_IER) & 0x0f;
	soutp(UART_IER, scratch);
	if (scratch2 != 0 || scratch3 != 0x0f) {
		/* we fail, there's nothing here */
		pr_err("port existence test failed, cannot continue\n");
		return -ENODEV;
	}



	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	/* First of all, disable all interrupts */
	soutp(UART_IER, sinp(UART_IER) &
	      (~(UART_IER_MSI|UART_IER_RLSI|UART_IER_THRI|UART_IER_RDI)));

	/* Clear registers. */
	sinp(UART_LSR);
	sinp(UART_RX);
	sinp(UART_IIR);
	sinp(UART_MSR);

#ifdef CONFIG_LIRC_SERIAL_NSLU2
	if (type == LIRC_NSLU2) {
		/* Setup NSLU2 UART */

		/* Enable UART */
		soutp(UART_IER, sinp(UART_IER) | UART_IE_IXP42X_UUE);
		/* Disable Receiver data Time out interrupt */
		soutp(UART_IER, sinp(UART_IER) & ~UART_IE_IXP42X_RTOIE);
		/* set out2 = interrupt unmask; off() doesn't set MCR
		   on NSLU2 */
		soutp(UART_MCR, UART_MCR_RTS|UART_MCR_OUT2);
	}
#endif

	/* Set line for power source */
	off();

	/* Clear registers again to be sure. */
	sinp(UART_LSR);
	sinp(UART_RX);
	sinp(UART_IIR);
	sinp(UART_MSR);

	switch (type) {
	case LIRC_IRDEO:
	case LIRC_IRDEO_REMOTE:
		/* setup port to 7N1 @ 115200 Baud */
		/* 7N1+start = 9 bits at 115200 ~ 3 bits at 38kHz */

		/* Set DLAB 1. */
		soutp(UART_LCR, sinp(UART_LCR) | UART_LCR_DLAB);
		/* Set divisor to 1 => 115200 Baud */
		soutp(UART_DLM, 0);
		soutp(UART_DLL, 1);
		/* Set DLAB 0 +  7N1 */
		soutp(UART_LCR, UART_LCR_WLEN7);
		/* THR interrupt already disabled at this point */
		break;
	default:
		break;
	}

	return 0;
}

static int lirc_serial_probe(struct platform_device *dev)
{
	int i, nlow, nhigh, result;

#ifdef CONFIG_LIRC_SERIAL_NSLU2
	/* This GPIO is used for a LED on the NSLU2 */
	result = devm_gpio_request(dev, NSLU2_LED_GRN, "lirc-serial");
	if (result)
		return result;
	result = gpio_direction_output(NSLU2_LED_GRN, 0);
	if (result)
		return result;
#endif

	result = request_irq(irq, lirc_irq_handler,
			     (share_irq ? IRQF_SHARED : 0),
			     LIRC_DRIVER_NAME, (void *)&hardware);
	if (result < 0) {
		if (result == -EBUSY)
			dev_err(&dev->dev, "IRQ %d busy\n", irq);
		else if (result == -EINVAL)
			dev_err(&dev->dev, "Bad irq number or handler\n");
		return result;
	}

	/* Reserve io region. */
	/*
	 * Future MMAP-Developers: Attention!
	 * For memory mapped I/O you *might* need to use ioremap() first,
	 * for the NSLU2 it's done in boot code.
	 */
	if (((iommap != 0)
	     && (request_mem_region(iommap, 8 << ioshift,
				    LIRC_DRIVER_NAME) == NULL))
	   || ((iommap == 0)
	       && (request_region(io, 8, LIRC_DRIVER_NAME) == NULL))) {
		dev_err(&dev->dev, "port %04x already in use\n", io);
		dev_warn(&dev->dev, "use 'setserial /dev/ttySX uart none'\n");
		dev_warn(&dev->dev,
			 "or compile the serial port driver as module and\n");
		dev_warn(&dev->dev, "make sure this module is loaded first\n");
		result = -EBUSY;
		goto exit_free_irq;
	}

	result = hardware_init_port();
	if (result < 0)
		goto exit_release_region;

	/* Initialize pulse/space widths */
	init_timing_params(duty_cycle, freq);

	/* If pin is high, then this must be an active low receiver. */
	if (sense == -1) {
		/* wait 1/2 sec for the power supply */
		msleep(500);

		/*
		 * probe 9 times every 0.04s, collect "votes" for
		 * active high/low
		 */
		nlow = 0;
		nhigh = 0;
		for (i = 0; i < 9; i++) {
			if (sinp(UART_MSR) & hardware[type].signal_pin)
				nlow++;
			else
				nhigh++;
			msleep(40);
		}
		sense = (nlow >= nhigh ? 1 : 0);
		dev_info(&dev->dev, "auto-detected active %s receiver\n",
			 sense ? "low" : "high");
	} else
		dev_info(&dev->dev, "Manually using active %s receiver\n",
			 sense ? "low" : "high");

	dprintk("Interrupt %d, port %04x obtained\n", irq, io);
	return 0;

exit_release_region:
	if (iommap != 0)
		release_mem_region(iommap, 8 << ioshift);
	else
		release_region(io, 8);
exit_free_irq:
	free_irq(irq, (void *)&hardware);

	return result;
}

static int lirc_serial_remove(struct platform_device *dev)
{
	free_irq(irq, (void *)&hardware);

	if (iommap != 0)
		release_mem_region(iommap, 8 << ioshift);
	else
		release_region(io, 8);

	return 0;
}

static int set_use_inc(void *data)
{
	unsigned long flags;

	/* initialize timestamp */
	do_gettimeofday(&lasttv);

	spin_lock_irqsave(&hardware[type].lock, flags);

	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	soutp(UART_IER, sinp(UART_IER)|UART_IER_MSI);

	spin_unlock_irqrestore(&hardware[type].lock, flags);

	return 0;
}

static void set_use_dec(void *data)
{	unsigned long flags;

	spin_lock_irqsave(&hardware[type].lock, flags);

	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	/* First of all, disable all interrupts */
	soutp(UART_IER, sinp(UART_IER) &
	      (~(UART_IER_MSI|UART_IER_RLSI|UART_IER_THRI|UART_IER_RDI)));
	spin_unlock_irqrestore(&hardware[type].lock, flags);
}

static ssize_t lirc_write(struct file *file, const char __user *buf,
			 size_t n, loff_t *ppos)
{
	int i, count;
	unsigned long flags;
	long delta = 0;
	int *wbuf;

	if (!(hardware[type].features & LIRC_CAN_SEND_PULSE))
		return -EPERM;

	count = n / sizeof(int);
	if (n % sizeof(int) || count % 2 == 0)
		return -EINVAL;
	wbuf = memdup_user(buf, n);
	if (IS_ERR(wbuf))
		return PTR_ERR(wbuf);
	spin_lock_irqsave(&hardware[type].lock, flags);
	if (type == LIRC_IRDEO) {
		/* DTR, RTS down */
		on();
	}
	for (i = 0; i < count; i++) {
		if (i%2)
			hardware[type].send_space(wbuf[i] - delta);
		else
			delta = hardware[type].send_pulse(wbuf[i]);
	}
	off();
	spin_unlock_irqrestore(&hardware[type].lock, flags);
	kfree(wbuf);
	return n;
}

static long lirc_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int result;
	__u32 value;

	switch (cmd) {
	case LIRC_GET_SEND_MODE:
		if (!(hardware[type].features&LIRC_CAN_SEND_MASK))
			return -ENOIOCTLCMD;

		result = put_user(LIRC_SEND2MODE
				  (hardware[type].features&LIRC_CAN_SEND_MASK),
				  (__u32 *) arg);
		if (result)
			return result;
		break;

	case LIRC_SET_SEND_MODE:
		if (!(hardware[type].features&LIRC_CAN_SEND_MASK))
			return -ENOIOCTLCMD;

		result = get_user(value, (__u32 *) arg);
		if (result)
			return result;
		/* only LIRC_MODE_PULSE supported */
		if (value != LIRC_MODE_PULSE)
			return -EINVAL;
		break;

	case LIRC_GET_LENGTH:
		return -ENOIOCTLCMD;
		break;

	case LIRC_SET_SEND_DUTY_CYCLE:
		dprintk("SET_SEND_DUTY_CYCLE\n");
		if (!(hardware[type].features&LIRC_CAN_SET_SEND_DUTY_CYCLE))
			return -ENOIOCTLCMD;

		result = get_user(value, (__u32 *) arg);
		if (result)
			return result;
		if (value <= 0 || value > 100)
			return -EINVAL;
		return init_timing_params(value, freq);
		break;

	case LIRC_SET_SEND_CARRIER:
		dprintk("SET_SEND_CARRIER\n");
		if (!(hardware[type].features&LIRC_CAN_SET_SEND_CARRIER))
			return -ENOIOCTLCMD;

		result = get_user(value, (__u32 *) arg);
		if (result)
			return result;
		if (value > 500000 || value < 20000)
			return -EINVAL;
		return init_timing_params(duty_cycle, value);
		break;

	default:
		return lirc_dev_fop_ioctl(filep, cmd, arg);
	}
	return 0;
}

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.write		= lirc_write,
	.unlocked_ioctl	= lirc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= lirc_ioctl,
#endif
	.read		= lirc_dev_fop_read,
	.poll		= lirc_dev_fop_poll,
	.open		= lirc_dev_fop_open,
	.release	= lirc_dev_fop_close,
	.llseek		= no_llseek,
};

static struct lirc_driver driver = {
	.name		= LIRC_DRIVER_NAME,
	.minor		= -1,
	.code_length	= 1,
	.sample_rate	= 0,
	.data		= NULL,
	.add_to_buf	= NULL,
	.rbuf		= &rbuf,
	.set_use_inc	= set_use_inc,
	.set_use_dec	= set_use_dec,
	.fops		= &lirc_fops,
	.dev		= NULL,
	.owner		= THIS_MODULE,
};

static struct platform_device *lirc_serial_dev;

static int lirc_serial_suspend(struct platform_device *dev,
			       pm_message_t state)
{
	/* Set DLAB 0. */
	soutp(UART_LCR, sinp(UART_LCR) & (~UART_LCR_DLAB));

	/* Disable all interrupts */
	soutp(UART_IER, sinp(UART_IER) &
	      (~(UART_IER_MSI|UART_IER_RLSI|UART_IER_THRI|UART_IER_RDI)));

	/* Clear registers. */
	sinp(UART_LSR);
	sinp(UART_RX);
	sinp(UART_IIR);
	sinp(UART_MSR);

	return 0;
}

/* twisty maze... need a forward-declaration here... */
static void lirc_serial_exit(void);

static int lirc_serial_resume(struct platform_device *dev)
{
	unsigned long flags;
	int result;

	result = hardware_init_port();
	if (result < 0)
		return result;

	spin_lock_irqsave(&hardware[type].lock, flags);
	/* Enable Interrupt */
	do_gettimeofday(&lasttv);
	soutp(UART_IER, sinp(UART_IER)|UART_IER_MSI);
	off();

	lirc_buffer_clear(&rbuf);

	spin_unlock_irqrestore(&hardware[type].lock, flags);

	return 0;
}

static struct platform_driver lirc_serial_driver = {
	.probe		= lirc_serial_probe,
	.remove		= lirc_serial_remove,
	.suspend	= lirc_serial_suspend,
	.resume		= lirc_serial_resume,
	.driver		= {
		.name	= "lirc_serial",
		.owner	= THIS_MODULE,
	},
};

static int __init lirc_serial_init(void)
{
	int result;

	/* Init read buffer. */
	result = lirc_buffer_init(&rbuf, sizeof(int), RBUF_LEN);
	if (result < 0)
		return result;

	result = platform_driver_register(&lirc_serial_driver);
	if (result) {
		printk("lirc register returned %d\n", result);
		goto exit_buffer_free;
	}

	lirc_serial_dev = platform_device_alloc("lirc_serial", 0);
	if (!lirc_serial_dev) {
		result = -ENOMEM;
		goto exit_driver_unregister;
	}

	result = platform_device_add(lirc_serial_dev);
	if (result)
		goto exit_device_put;

	return 0;

exit_device_put:
	platform_device_put(lirc_serial_dev);
exit_driver_unregister:
	platform_driver_unregister(&lirc_serial_driver);
exit_buffer_free:
	lirc_buffer_free(&rbuf);
	return result;
}

static void lirc_serial_exit(void)
{
	platform_device_unregister(lirc_serial_dev);
	platform_driver_unregister(&lirc_serial_driver);
	lirc_buffer_free(&rbuf);
}

static int __init lirc_serial_init_module(void)
{
	int result;

	switch (type) {
	case LIRC_HOMEBREW:
	case LIRC_IRDEO:
	case LIRC_IRDEO_REMOTE:
	case LIRC_ANIMAX:
	case LIRC_IGOR:
		/* if nothing specified, use ttyS0/com1 and irq 4 */
		io = io ? io : 0x3f8;
		irq = irq ? irq : 4;
		break;
#ifdef CONFIG_LIRC_SERIAL_NSLU2
	case LIRC_NSLU2:
		io = io ? io : IRQ_IXP4XX_UART2;
		irq = irq ? irq : (IXP4XX_UART2_BASE_VIRT + REG_OFFSET);
		iommap = iommap ? iommap : IXP4XX_UART2_BASE_PHYS;
		ioshift = ioshift ? ioshift : 2;
		break;
#endif
	default:
		return -EINVAL;
	}
	if (!softcarrier) {
		switch (type) {
		case LIRC_HOMEBREW:
		case LIRC_IGOR:
#ifdef CONFIG_LIRC_SERIAL_NSLU2
		case LIRC_NSLU2:
#endif
			hardware[type].features &=
				~(LIRC_CAN_SET_SEND_DUTY_CYCLE|
				  LIRC_CAN_SET_SEND_CARRIER);
			break;
		}
	}

	/* make sure sense is either -1, 0, or 1 */
	if (sense != -1)
		sense = !!sense;

	result = lirc_serial_init();
	if (result)
		return result;

	driver.features = hardware[type].features;
	driver.dev = &lirc_serial_dev->dev;
	driver.minor = lirc_register_driver(&driver);
	if (driver.minor < 0) {
		pr_err("register_chrdev failed!\n");
		lirc_serial_exit();
		return driver.minor;
	}
	return 0;
}

static void __exit lirc_serial_exit_module(void)
{
	lirc_unregister_driver(driver.minor);
	lirc_serial_exit();
	dprintk("cleaned up module\n");
}


module_init(lirc_serial_init_module);
module_exit(lirc_serial_exit_module);

MODULE_DESCRIPTION("Infra-red receiver driver for serial ports.");
MODULE_AUTHOR("Ralph Metzler, Trent Piepho, Ben Pfaff, "
	      "Christoph Bartelmus, Andrei Tanas");
MODULE_LICENSE("GPL");

module_param(type, int, S_IRUGO);
MODULE_PARM_DESC(type, "Hardware type (0 = home-brew, 1 = IRdeo,"
		 " 2 = IRdeo Remote, 3 = AnimaX, 4 = IgorPlug,"
		 " 5 = NSLU2 RX:CTS2/TX:GreenLED)");

module_param(io, int, S_IRUGO);
MODULE_PARM_DESC(io, "I/O address base (0x3f8 or 0x2f8)");

/* some architectures (e.g. intel xscale) have memory mapped registers */
module_param(iommap, bool, S_IRUGO);
MODULE_PARM_DESC(iommap, "physical base for memory mapped I/O"
		" (0 = no memory mapped io)");

/*
 * some architectures (e.g. intel xscale) align the 8bit serial registers
 * on 32bit word boundaries.
 * See linux-kernel/drivers/tty/serial/8250/8250.c serial_in()/out()
 */
module_param(ioshift, int, S_IRUGO);
MODULE_PARM_DESC(ioshift, "shift I/O register offset (0 = no shift)");

module_param(irq, int, S_IRUGO);
MODULE_PARM_DESC(irq, "Interrupt (4 or 3)");

module_param(share_irq, bool, S_IRUGO);
MODULE_PARM_DESC(share_irq, "Share interrupts (0 = off, 1 = on)");

module_param(sense, int, S_IRUGO);
MODULE_PARM_DESC(sense, "Override autodetection of IR receiver circuit"
		 " (0 = active high, 1 = active low )");

#ifdef CONFIG_LIRC_SERIAL_TRANSMITTER
module_param(txsense, bool, S_IRUGO);
MODULE_PARM_DESC(txsense, "Sense of transmitter circuit"
		 " (0 = active high, 1 = active low )");
#endif

module_param(softcarrier, bool, S_IRUGO);
MODULE_PARM_DESC(softcarrier, "Software carrier (0 = off, 1 = on, default on)");

module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable debugging messages");
