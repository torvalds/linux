/*
 * linux/drivers/char/serial167.c
 *
 * Driver for MVME166/7 board serial ports, which are via a CD2401.
 * Based very much on cyclades.c.
 *
 * MVME166/7 work by Richard Hirst [richard@sleepie.demon.co.uk]
 *
 * ==============================================================
 *
 * static char rcsid[] =
 * "$Revision: 1.36.1.4 $$Date: 1995/03/29 06:14:14 $";
 *
 *  linux/kernel/cyclades.c
 *
 * Maintained by Marcio Saito (cyclades@netcom.com) and
 * Randolph Bentson (bentson@grieg.seaslug.org)
 *
 * Much of the design and some of the code came from serial.c
 * which was copyright (C) 1991, 1992  Linus Torvalds.  It was
 * extensively rewritten by Theodore Ts'o, 8/16/92 -- 9/14/92,
 * and then fixed as suggested by Michael K. Johnson 12/12/92.
 *
 * This version does not support shared irq's.
 *
 * $Log: cyclades.c,v $
 * Revision 1.36.1.4  1995/03/29  06:14:14  bentson
 * disambiguate between Cyclom-16Y and Cyclom-32Ye;
 *
 * Changes:
 *
 * 200 lines of changes record removed - RGH 11-10-95, starting work on
 * converting this to drive serial ports on mvme166 (cd2401).
 *
 * Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 2000/08/25
 * - get rid of verify_area
 * - use get_user to access memory from userspace in set_threshold,
 *   set_default_threshold and set_timeout
 * - don't use the panic function in serial167_init
 * - do resource release on failure on serial167_init
 * - include missing restore_flags in mvme167_serial_console_setup
 *
 * Kars de Jong <jongk@linux-m68k.org> - 2004/09/06
 * - replace bottom half handler with task queue handler
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/tty.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/serial167.h>
#include <linux/delay.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/tty_flip.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/mvme16xhw.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>

#include <linux/types.h>
#include <linux/kernel.h>

#include <asm/uaccess.h>
#include <linux/init.h>

#define SERIAL_PARANOIA_CHECK
#undef  SERIAL_DEBUG_OPEN
#undef  SERIAL_DEBUG_THROTTLE
#undef  SERIAL_DEBUG_OTHER
#undef  SERIAL_DEBUG_IO
#undef  SERIAL_DEBUG_COUNT
#undef  SERIAL_DEBUG_DTR
#undef  CYCLOM_16Y_HACK
#define  CYCLOM_ENABLE_MONITORING

#define WAKEUP_CHARS 256

#define STD_COM_FLAGS (0)

static struct tty_driver *cy_serial_driver;
extern int serial_console;
static struct cyclades_port *serial_console_info = NULL;
static unsigned int serial_console_cflag = 0;
u_char initial_console_speed;

/* Base address of cd2401 chip on mvme166/7 */

#define BASE_ADDR (0xfff45000)
#define pcc2chip	((volatile u_char *)0xfff42000)
#define PccSCCMICR	0x1d
#define PccSCCTICR	0x1e
#define PccSCCRICR	0x1f
#define PccTPIACKR	0x25
#define PccRPIACKR	0x27
#define PccIMLR		0x3f

/* This is the per-port data structure */
struct cyclades_port cy_port[] = {
	/* CARD#  */
	{-1},			/* ttyS0 */
	{-1},			/* ttyS1 */
	{-1},			/* ttyS2 */
	{-1},			/* ttyS3 */
};

#define NR_PORTS        ARRAY_SIZE(cy_port)

/*
 * This is used to look up the divisor speeds and the timeouts
 * We're normally limited to 15 distinct baud rates.  The extra
 * are accessed via settings in info->flags.
 *         0,     1,     2,     3,     4,     5,     6,     7,     8,     9,
 *        10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
 *                                                  HI            VHI
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200,
	1800, 2400, 4800, 9600, 19200, 38400, 57600, 76800, 115200, 150000,
	0
};

#if 0
static char baud_co[] = {	/* 25 MHz clock option table */
	/* value =>    00    01   02    03    04 */
	/* divide by    8    32   128   512  2048 */
	0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x02,
	0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static char baud_bpr[] = {	/* 25 MHz baud rate period table */
	0x00, 0xf5, 0xa3, 0x6f, 0x5c, 0x51, 0xf5, 0xa3, 0x51, 0xa3,
	0x6d, 0x51, 0xa3, 0x51, 0xa3, 0x51, 0x36, 0x29, 0x1b, 0x15
};
#endif

/* I think 166 brd clocks 2401 at 20MHz.... */

/* These values are written directly to tcor, and >> 5 for writing to rcor */
static u_char baud_co[] = {	/* 20 MHz clock option table */
	0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x60, 0x60, 0x40,
	0x40, 0x40, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* These values written directly to tbpr/rbpr */
static u_char baud_bpr[] = {	/* 20 MHz baud rate period table */
	0x00, 0xc0, 0x80, 0x58, 0x6c, 0x40, 0xc0, 0x81, 0x40, 0x81,
	0x57, 0x40, 0x81, 0x40, 0x81, 0x40, 0x2b, 0x20, 0x15, 0x10
};

static u_char baud_cor4[] = {	/* receive threshold */
	0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a,
	0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x08, 0x08, 0x08, 0x08, 0x07
};

static void shutdown(struct cyclades_port *);
static int startup(struct cyclades_port *);
static void cy_throttle(struct tty_struct *);
static void cy_unthrottle(struct tty_struct *);
static void config_setup(struct cyclades_port *);
#ifdef CYCLOM_SHOW_STATUS
static void show_status(int);
#endif

#ifdef CONFIG_REMOTE_DEBUG
static void debug_setup(void);
void queueDebugChar(int c);
int getDebugChar(void);

#define DEBUG_PORT	1
#define DEBUG_LEN	256

typedef struct {
	int in;
	int out;
	unsigned char buf[DEBUG_LEN];
} debugq;

debugq debugiq;
#endif

/*
 * I have my own version of udelay(), as it is needed when initialising
 * the chip, before the delay loop has been calibrated.  Should probably
 * reference one of the vmechip2 or pccchip2 counter for an accurate
 * delay, but this wild guess will do for now.
 */

void my_udelay(long us)
{
	u_char x;
	volatile u_char *p = &x;
	int i;

	while (us--)
		for (i = 100; i; i--)
			x |= *p;
}

static inline int serial_paranoia_check(struct cyclades_port *info, char *name,
		const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	if (!info) {
		printk("Warning: null cyclades_port for (%s) in %s\n", name,
				routine);
		return 1;
	}

	if (info < &cy_port[0] || info >= &cy_port[NR_PORTS]) {
		printk("Warning: cyclades_port out of range for (%s) in %s\n",
				name, routine);
		return 1;
	}

	if (info->magic != CYCLADES_MAGIC) {
		printk("Warning: bad magic number for serial struct (%s) in "
				"%s\n", name, routine);
		return 1;
	}
#endif
	return 0;
}				/* serial_paranoia_check */

#if 0
/* The following diagnostic routines allow the driver to spew
   information on the screen, even (especially!) during interrupts.
 */
void SP(char *data)
{
	unsigned long flags;
	local_irq_save(flags);
	printk(KERN_EMERG "%s", data);
	local_irq_restore(flags);
}

char scrn[2];
void CP(char data)
{
	unsigned long flags;
	local_irq_save(flags);
	scrn[0] = data;
	printk(KERN_EMERG "%c", scrn);
	local_irq_restore(flags);
}				/* CP */

void CP1(int data)
{
	(data < 10) ? CP(data + '0') : CP(data + 'A' - 10);
}				/* CP1 */
void CP2(int data)
{
	CP1((data >> 4) & 0x0f);
	CP1(data & 0x0f);
}				/* CP2 */
void CP4(int data)
{
	CP2((data >> 8) & 0xff);
	CP2(data & 0xff);
}				/* CP4 */
void CP8(long data)
{
	CP4((data >> 16) & 0xffff);
	CP4(data & 0xffff);
}				/* CP8 */
#endif

/* This routine waits up to 1000 micro-seconds for the previous
   command to the Cirrus chip to complete and then issues the
   new command.  An error is returned if the previous command
   didn't finish within the time limit.
 */
u_short write_cy_cmd(volatile u_char * base_addr, u_char cmd)
{
	unsigned long flags;
	volatile int i;

	local_irq_save(flags);
	/* Check to see that the previous command has completed */
	for (i = 0; i < 100; i++) {
		if (base_addr[CyCCR] == 0) {
			break;
		}
		my_udelay(10L);
	}
	/* if the CCR never cleared, the previous command
	   didn't finish within the "reasonable time" */
	if (i == 10) {
		local_irq_restore(flags);
		return (-1);
	}

	/* Issue the new command */
	base_addr[CyCCR] = cmd;
	local_irq_restore(flags);
	return (0);
}				/* write_cy_cmd */

/* cy_start and cy_stop provide software output flow control as a
   function of XON/XOFF, software CTS, and other such stuff. */

static void cy_stop(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;
	unsigned long flags;

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_stop %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_stop"))
		return;

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) (channel);	/* index channel */
	base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
	local_irq_restore(flags);
}				/* cy_stop */

static void cy_start(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;
	unsigned long flags;

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_start %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_start"))
		return;

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) (channel);
	base_addr[CyIER] |= CyTxMpty;
	local_irq_restore(flags);
}				/* cy_start */

/* The real interrupt service routines are called
   whenever the card wants its hand held--chars
   received, out buffer empty, modem change, etc.
 */
static irqreturn_t cd2401_rxerr_interrupt(int irq, void *dev_id)
{
	struct tty_struct *tty;
	struct cyclades_port *info;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	unsigned char err, rfoc;
	int channel;
	char data;

	/* determine the channel and change to that context */
	channel = (u_short) (base_addr[CyLICR] >> 2);
	info = &cy_port[channel];
	info->last_active = jiffies;

	if ((err = base_addr[CyRISR]) & CyTIMEOUT) {
		/* This is a receive timeout interrupt, ignore it */
		base_addr[CyREOIR] = CyNOTRANS;
		return IRQ_HANDLED;
	}

	/* Read a byte of data if there is any - assume the error
	 * is associated with this character */

	if ((rfoc = base_addr[CyRFOC]) != 0)
		data = base_addr[CyRDR];
	else
		data = 0;

	/* if there is nowhere to put the data, discard it */
	if (info->tty == 0) {
		base_addr[CyREOIR] = rfoc ? 0 : CyNOTRANS;
		return IRQ_HANDLED;
	} else {		/* there is an open port for this data */
		tty = info->tty;
		if (err & info->ignore_status_mask) {
			base_addr[CyREOIR] = rfoc ? 0 : CyNOTRANS;
			return IRQ_HANDLED;
		}
		if (tty_buffer_request_room(tty, 1) != 0) {
			if (err & info->read_status_mask) {
				if (err & CyBREAK) {
					tty_insert_flip_char(tty, data,
							     TTY_BREAK);
					if (info->flags & ASYNC_SAK) {
						do_SAK(tty);
					}
				} else if (err & CyFRAME) {
					tty_insert_flip_char(tty, data,
							     TTY_FRAME);
				} else if (err & CyPARITY) {
					tty_insert_flip_char(tty, data,
							     TTY_PARITY);
				} else if (err & CyOVERRUN) {
					tty_insert_flip_char(tty, 0,
							     TTY_OVERRUN);
					/*
					   If the flip buffer itself is
					   overflowing, we still lose
					   the next incoming character.
					 */
					if (tty_buffer_request_room(tty, 1) !=
					    0) {
						tty_insert_flip_char(tty, data,
								     TTY_FRAME);
					}
					/* These two conditions may imply */
					/* a normal read should be done. */
					/* else if(data & CyTIMEOUT) */
					/* else if(data & CySPECHAR) */
				} else {
					tty_insert_flip_char(tty, 0,
							     TTY_NORMAL);
				}
			} else {
				tty_insert_flip_char(tty, data, TTY_NORMAL);
			}
		} else {
			/* there was a software buffer overrun
			   and nothing could be done about it!!! */
		}
	}
	tty_schedule_flip(tty);
	/* end of service */
	base_addr[CyREOIR] = rfoc ? 0 : CyNOTRANS;
	return IRQ_HANDLED;
}				/* cy_rxerr_interrupt */

static irqreturn_t cd2401_modem_interrupt(int irq, void *dev_id)
{
	struct cyclades_port *info;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;
	int mdm_change;
	int mdm_status;

	/* determine the channel and change to that context */
	channel = (u_short) (base_addr[CyLICR] >> 2);
	info = &cy_port[channel];
	info->last_active = jiffies;

	mdm_change = base_addr[CyMISR];
	mdm_status = base_addr[CyMSVR1];

	if (info->tty == 0) {	/* nowhere to put the data, ignore it */
		;
	} else {
		if ((mdm_change & CyDCD)
		    && (info->flags & ASYNC_CHECK_CD)) {
			if (mdm_status & CyDCD) {
/* CP('!'); */
				wake_up_interruptible(&info->open_wait);
			} else {
/* CP('@'); */
				tty_hangup(info->tty);
				wake_up_interruptible(&info->open_wait);
				info->flags &= ~ASYNC_NORMAL_ACTIVE;
			}
		}
		if ((mdm_change & CyCTS)
		    && (info->flags & ASYNC_CTS_FLOW)) {
			if (info->tty->stopped) {
				if (mdm_status & CyCTS) {
					/* !!! cy_start isn't used because... */
					info->tty->stopped = 0;
					base_addr[CyIER] |= CyTxMpty;
					tty_wakeup(info->tty);
				}
			} else {
				if (!(mdm_status & CyCTS)) {
					/* !!! cy_stop isn't used because... */
					info->tty->stopped = 1;
					base_addr[CyIER] &=
					    ~(CyTxMpty | CyTxRdy);
				}
			}
		}
		if (mdm_status & CyDSR) {
		}
	}
	base_addr[CyMEOIR] = 0;
	return IRQ_HANDLED;
}				/* cy_modem_interrupt */

static irqreturn_t cd2401_tx_interrupt(int irq, void *dev_id)
{
	struct cyclades_port *info;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;
	int char_count, saved_cnt;
	int outch;

	/* determine the channel and change to that context */
	channel = (u_short) (base_addr[CyLICR] >> 2);

#ifdef CONFIG_REMOTE_DEBUG
	if (channel == DEBUG_PORT) {
		panic("TxInt on debug port!!!");
	}
#endif
	/* validate the port number (as configured and open) */
	if ((channel < 0) || (NR_PORTS <= channel)) {
		base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
		base_addr[CyTEOIR] = CyNOTRANS;
		return IRQ_HANDLED;
	}
	info = &cy_port[channel];
	info->last_active = jiffies;
	if (info->tty == 0) {
		base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
		base_addr[CyTEOIR] = CyNOTRANS;
		return IRQ_HANDLED;
	}

	/* load the on-chip space available for outbound data */
	saved_cnt = char_count = base_addr[CyTFTC];

	if (info->x_char) {	/* send special char */
		outch = info->x_char;
		base_addr[CyTDR] = outch;
		char_count--;
		info->x_char = 0;
	}

	if (info->x_break) {
		/*  The Cirrus chip requires the "Embedded Transmit
		   Commands" of start break, delay, and end break
		   sequences to be sent.  The duration of the
		   break is given in TICs, which runs at HZ
		   (typically 100) and the PPR runs at 200 Hz,
		   so the delay is duration * 200/HZ, and thus a
		   break can run from 1/100 sec to about 5/4 sec.
		   Need to check these values - RGH 141095.
		 */
		base_addr[CyTDR] = 0;	/* start break */
		base_addr[CyTDR] = 0x81;
		base_addr[CyTDR] = 0;	/* delay a bit */
		base_addr[CyTDR] = 0x82;
		base_addr[CyTDR] = info->x_break * 200 / HZ;
		base_addr[CyTDR] = 0;	/* terminate break */
		base_addr[CyTDR] = 0x83;
		char_count -= 7;
		info->x_break = 0;
	}

	while (char_count > 0) {
		if (!info->xmit_cnt) {
			base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
			break;
		}
		if (info->xmit_buf == 0) {
			base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
			break;
		}
		if (info->tty->stopped || info->tty->hw_stopped) {
			base_addr[CyIER] &= ~(CyTxMpty | CyTxRdy);
			break;
		}
		/* Because the Embedded Transmit Commands have been
		   enabled, we must check to see if the escape
		   character, NULL, is being sent.  If it is, we
		   must ensure that there is room for it to be
		   doubled in the output stream.  Therefore we
		   no longer advance the pointer when the character
		   is fetched, but rather wait until after the check
		   for a NULL output character. (This is necessary
		   because there may not be room for the two chars
		   needed to send a NULL.
		 */
		outch = info->xmit_buf[info->xmit_tail];
		if (outch) {
			info->xmit_cnt--;
			info->xmit_tail = (info->xmit_tail + 1)
			    & (PAGE_SIZE - 1);
			base_addr[CyTDR] = outch;
			char_count--;
		} else {
			if (char_count > 1) {
				info->xmit_cnt--;
				info->xmit_tail = (info->xmit_tail + 1)
				    & (PAGE_SIZE - 1);
				base_addr[CyTDR] = outch;
				base_addr[CyTDR] = 0;
				char_count--;
				char_count--;
			} else {
				break;
			}
		}
	}

	if (info->xmit_cnt < WAKEUP_CHARS)
		tty_wakeup(info->tty);

	base_addr[CyTEOIR] = (char_count != saved_cnt) ? 0 : CyNOTRANS;
	return IRQ_HANDLED;
}				/* cy_tx_interrupt */

static irqreturn_t cd2401_rx_interrupt(int irq, void *dev_id)
{
	struct tty_struct *tty;
	struct cyclades_port *info;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;
	char data;
	int char_count;
	int save_cnt;
	int len;

	/* determine the channel and change to that context */
	channel = (u_short) (base_addr[CyLICR] >> 2);
	info = &cy_port[channel];
	info->last_active = jiffies;
	save_cnt = char_count = base_addr[CyRFOC];

#ifdef CONFIG_REMOTE_DEBUG
	if (channel == DEBUG_PORT) {
		while (char_count--) {
			data = base_addr[CyRDR];
			queueDebugChar(data);
		}
	} else
#endif
		/* if there is nowhere to put the data, discard it */
	if (info->tty == 0) {
		while (char_count--) {
			data = base_addr[CyRDR];
		}
	} else {		/* there is an open port for this data */
		tty = info->tty;
		/* load # characters available from the chip */

#ifdef CYCLOM_ENABLE_MONITORING
		++info->mon.int_count;
		info->mon.char_count += char_count;
		if (char_count > info->mon.char_max)
			info->mon.char_max = char_count;
		info->mon.char_last = char_count;
#endif
		while (char_count--) {
			data = base_addr[CyRDR];
			tty_insert_flip_char(tty, data, TTY_NORMAL);
#ifdef CYCLOM_16Y_HACK
			udelay(10L);
#endif
		}
		tty_schedule_flip(tty);
	}
	/* end of service */
	base_addr[CyREOIR] = save_cnt ? 0 : CyNOTRANS;
	return IRQ_HANDLED;
}				/* cy_rx_interrupt */

/* This is called whenever a port becomes active;
   interrupts are enabled and DTR & RTS are turned on.
 */
static int startup(struct cyclades_port *info)
{
	unsigned long flags;
	volatile unsigned char *base_addr = (unsigned char *)BASE_ADDR;
	int channel;

	if (info->flags & ASYNC_INITIALIZED) {
		return 0;
	}

	if (!info->type) {
		if (info->tty) {
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		}
		return 0;
	}
	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *)get_zeroed_page(GFP_KERNEL);
		if (!info->xmit_buf) {
			return -ENOMEM;
		}
	}

	config_setup(info);

	channel = info->line;

#ifdef SERIAL_DEBUG_OPEN
	printk("startup channel %d\n", channel);
#endif

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) channel;
	write_cy_cmd(base_addr, CyENB_RCVR | CyENB_XMTR);

	base_addr[CyCAR] = (u_char) channel;	/* !!! Is this needed? */
	base_addr[CyMSVR1] = CyRTS;
/* CP('S');CP('1'); */
	base_addr[CyMSVR2] = CyDTR;

#ifdef SERIAL_DEBUG_DTR
	printk("cyc: %d: raising DTR\n", __LINE__);
	printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
	       base_addr[CyMSVR2]);
#endif

	base_addr[CyIER] |= CyRxData;
	info->flags |= ASYNC_INITIALIZED;

	if (info->tty) {
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	local_irq_restore(flags);

#ifdef SERIAL_DEBUG_OPEN
	printk(" done\n");
#endif
	return 0;
}				/* startup */

void start_xmit(struct cyclades_port *info)
{
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;

	channel = info->line;
	local_irq_save(flags);
	base_addr[CyCAR] = channel;
	base_addr[CyIER] |= CyTxMpty;
	local_irq_restore(flags);
}				/* start_xmit */

/*
 * This routine shuts down a serial port; interrupts are disabled,
 * and DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct cyclades_port *info)
{
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;

	if (!(info->flags & ASYNC_INITIALIZED)) {
/* CP('$'); */
		return;
	}

	channel = info->line;

#ifdef SERIAL_DEBUG_OPEN
	printk("shutdown channel %d\n", channel);
#endif

	/* !!! REALLY MUST WAIT FOR LAST CHARACTER TO BE
	   SENT BEFORE DROPPING THE LINE !!!  (Perhaps
	   set some flag that is read when XMTY happens.)
	   Other choices are to delay some fixed interval
	   or schedule some later processing.
	 */
	local_irq_save(flags);
	if (info->xmit_buf) {
		free_page((unsigned long)info->xmit_buf);
		info->xmit_buf = NULL;
	}

	base_addr[CyCAR] = (u_char) channel;
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		base_addr[CyMSVR1] = 0;
/* CP('C');CP('1'); */
		base_addr[CyMSVR2] = 0;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: dropping DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
	}
	write_cy_cmd(base_addr, CyDIS_RCVR);
	/* it may be appropriate to clear _XMIT at
	   some later date (after testing)!!! */

	if (info->tty) {
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	}
	info->flags &= ~ASYNC_INITIALIZED;
	local_irq_restore(flags);

#ifdef SERIAL_DEBUG_OPEN
	printk(" done\n");
#endif
}				/* shutdown */

/*
 * This routine finds or computes the various line characteristics.
 */
static void config_setup(struct cyclades_port *info)
{
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;
	unsigned cflag;
	int i;
	unsigned char ti, need_init_chan = 0;

	if (!info->tty || !info->tty->termios) {
		return;
	}
	if (info->line == -1) {
		return;
	}
	cflag = info->tty->termios->c_cflag;

	/* baud rate */
	i = cflag & CBAUD;
#ifdef CBAUDEX
/* Starting with kernel 1.1.65, there is direct support for
   higher baud rates.  The following code supports those
   changes.  The conditional aspect allows this driver to be
   used for earlier as well as later kernel versions.  (The
   mapping is slightly different from serial.c because there
   is still the possibility of supporting 75 kbit/sec with
   the Cyclades board.)
 */
	if (i & CBAUDEX) {
		if (i == B57600)
			i = 16;
		else if (i == B115200)
			i = 18;
#ifdef B78600
		else if (i == B78600)
			i = 17;
#endif
		else
			info->tty->termios->c_cflag &= ~CBAUDEX;
	}
#endif
	if (i == 15) {
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			i += 1;
		if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			i += 3;
	}
	/* Don't ever change the speed of the console port.  It will
	 * run at the speed specified in bootinfo, or at 19.2K */
	/* Actually, it should run at whatever speed 166Bug was using */
	/* Note info->timeout isn't used at present */
	if (info != serial_console_info) {
		info->tbpr = baud_bpr[i];	/* Tx BPR */
		info->tco = baud_co[i];	/* Tx CO */
		info->rbpr = baud_bpr[i];	/* Rx BPR */
		info->rco = baud_co[i] >> 5;	/* Rx CO */
		if (baud_table[i] == 134) {
			info->timeout =
			    (info->xmit_fifo_size * HZ * 30 / 269) + 2;
			/* get it right for 134.5 baud */
		} else if (baud_table[i]) {
			info->timeout =
			    (info->xmit_fifo_size * HZ * 15 / baud_table[i]) +
			    2;
			/* this needs to be propagated into the card info */
		} else {
			info->timeout = 0;
		}
	}
	/* By tradition (is it a standard?) a baud rate of zero
	   implies the line should be/has been closed.  A bit
	   later in this routine such a test is performed. */

	/* byte size and parity */
	info->cor7 = 0;
	info->cor6 = 0;
	info->cor5 = 0;
	info->cor4 = (info->default_threshold ? info->default_threshold : baud_cor4[i]);	/* receive threshold */
	/* Following two lines added 101295, RGH. */
	/* It is obviously wrong to access CyCORx, and not info->corx here,
	 * try and remember to fix it later! */
	channel = info->line;
	base_addr[CyCAR] = (u_char) channel;
	if (C_CLOCAL(info->tty)) {
		if (base_addr[CyIER] & CyMdmCh)
			base_addr[CyIER] &= ~CyMdmCh;	/* without modem intr */
		/* ignore 1->0 modem transitions */
		if (base_addr[CyCOR4] & (CyDSR | CyCTS | CyDCD))
			base_addr[CyCOR4] &= ~(CyDSR | CyCTS | CyDCD);
		/* ignore 0->1 modem transitions */
		if (base_addr[CyCOR5] & (CyDSR | CyCTS | CyDCD))
			base_addr[CyCOR5] &= ~(CyDSR | CyCTS | CyDCD);
	} else {
		if ((base_addr[CyIER] & CyMdmCh) != CyMdmCh)
			base_addr[CyIER] |= CyMdmCh;	/* with modem intr */
		/* act on 1->0 modem transitions */
		if ((base_addr[CyCOR4] & (CyDSR | CyCTS | CyDCD)) !=
		    (CyDSR | CyCTS | CyDCD))
			base_addr[CyCOR4] |= CyDSR | CyCTS | CyDCD;
		/* act on 0->1 modem transitions */
		if ((base_addr[CyCOR5] & (CyDSR | CyCTS | CyDCD)) !=
		    (CyDSR | CyCTS | CyDCD))
			base_addr[CyCOR5] |= CyDSR | CyCTS | CyDCD;
	}
	info->cor3 = (cflag & CSTOPB) ? Cy_2_STOP : Cy_1_STOP;
	info->cor2 = CyETC;
	switch (cflag & CSIZE) {
	case CS5:
		info->cor1 = Cy_5_BITS;
		break;
	case CS6:
		info->cor1 = Cy_6_BITS;
		break;
	case CS7:
		info->cor1 = Cy_7_BITS;
		break;
	case CS8:
		info->cor1 = Cy_8_BITS;
		break;
	}
	if (cflag & PARENB) {
		if (cflag & PARODD) {
			info->cor1 |= CyPARITY_O;
		} else {
			info->cor1 |= CyPARITY_E;
		}
	} else {
		info->cor1 |= CyPARITY_NONE;
	}

	/* CTS flow control flag */
#if 0
	/* Don't complcate matters for now! RGH 141095 */
	if (cflag & CRTSCTS) {
		info->flags |= ASYNC_CTS_FLOW;
		info->cor2 |= CyCtsAE;
	} else {
		info->flags &= ~ASYNC_CTS_FLOW;
		info->cor2 &= ~CyCtsAE;
	}
#endif
	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else
		info->flags |= ASYNC_CHECK_CD;

     /***********************************************
	The hardware option, CyRtsAO, presents RTS when
	the chip has characters to send.  Since most modems
	use RTS as reverse (inbound) flow control, this
	option is not used.  If inbound flow control is
	necessary, DTR can be programmed to provide the
	appropriate signals for use with a non-standard
	cable.  Contact Marcio Saito for details.
     ***********************************************/

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) channel;

	/* CyCMR set once only in mvme167_init_serial() */
	if (base_addr[CyLICR] != channel << 2)
		base_addr[CyLICR] = channel << 2;
	if (base_addr[CyLIVR] != 0x5c)
		base_addr[CyLIVR] = 0x5c;

	/* tx and rx baud rate */

	if (base_addr[CyCOR1] != info->cor1)
		need_init_chan = 1;
	if (base_addr[CyTCOR] != info->tco)
		base_addr[CyTCOR] = info->tco;
	if (base_addr[CyTBPR] != info->tbpr)
		base_addr[CyTBPR] = info->tbpr;
	if (base_addr[CyRCOR] != info->rco)
		base_addr[CyRCOR] = info->rco;
	if (base_addr[CyRBPR] != info->rbpr)
		base_addr[CyRBPR] = info->rbpr;

	/* set line characteristics  according configuration */

	if (base_addr[CySCHR1] != START_CHAR(info->tty))
		base_addr[CySCHR1] = START_CHAR(info->tty);
	if (base_addr[CySCHR2] != STOP_CHAR(info->tty))
		base_addr[CySCHR2] = STOP_CHAR(info->tty);
	if (base_addr[CySCRL] != START_CHAR(info->tty))
		base_addr[CySCRL] = START_CHAR(info->tty);
	if (base_addr[CySCRH] != START_CHAR(info->tty))
		base_addr[CySCRH] = START_CHAR(info->tty);
	if (base_addr[CyCOR1] != info->cor1)
		base_addr[CyCOR1] = info->cor1;
	if (base_addr[CyCOR2] != info->cor2)
		base_addr[CyCOR2] = info->cor2;
	if (base_addr[CyCOR3] != info->cor3)
		base_addr[CyCOR3] = info->cor3;
	if (base_addr[CyCOR4] != info->cor4)
		base_addr[CyCOR4] = info->cor4;
	if (base_addr[CyCOR5] != info->cor5)
		base_addr[CyCOR5] = info->cor5;
	if (base_addr[CyCOR6] != info->cor6)
		base_addr[CyCOR6] = info->cor6;
	if (base_addr[CyCOR7] != info->cor7)
		base_addr[CyCOR7] = info->cor7;

	if (need_init_chan)
		write_cy_cmd(base_addr, CyINIT_CHAN);

	base_addr[CyCAR] = (u_char) channel;	/* !!! Is this needed? */

	/* 2ms default rx timeout */
	ti = info->default_timeout ? info->default_timeout : 0x02;
	if (base_addr[CyRTPRL] != ti)
		base_addr[CyRTPRL] = ti;
	if (base_addr[CyRTPRH] != 0)
		base_addr[CyRTPRH] = 0;

	/* Set up RTS here also ????? RGH 141095 */
	if (i == 0) {		/* baud rate is zero, turn off line */
		if ((base_addr[CyMSVR2] & CyDTR) == CyDTR)
			base_addr[CyMSVR2] = 0;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: dropping DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
	} else {
		if ((base_addr[CyMSVR2] & CyDTR) != CyDTR)
			base_addr[CyMSVR2] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: raising DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
	}

	if (info->tty) {
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	}

	local_irq_restore(flags);

}				/* config_setup */

static int cy_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;

#ifdef SERIAL_DEBUG_IO
	printk("cy_put_char %s(0x%02x)\n", tty->name, ch);
#endif

	if (serial_paranoia_check(info, tty->name, "cy_put_char"))
		return 0;

	if (!info->xmit_buf)
		return 0;

	local_irq_save(flags);
	if (info->xmit_cnt >= PAGE_SIZE - 1) {
		local_irq_restore(flags);
		return 0;
	}

	info->xmit_buf[info->xmit_head++] = ch;
	info->xmit_head &= PAGE_SIZE - 1;
	info->xmit_cnt++;
	local_irq_restore(flags);
	return 1;
}				/* cy_put_char */

static void cy_flush_chars(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;

#ifdef SERIAL_DEBUG_IO
	printk("cy_flush_chars %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped
	    || tty->hw_stopped || !info->xmit_buf)
		return;

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = channel;
	base_addr[CyIER] |= CyTxMpty;
	local_irq_restore(flags);
}				/* cy_flush_chars */

/* This routine gets called when tty_write has put something into
    the write_queue.  If the port is not already transmitting stuff,
    start it off by enabling interrupts.  The interrupt service
    routine will then ensure that the characters are sent.  If the
    port is already active, there is no need to kick it.
 */
static int cy_write(struct tty_struct *tty, const unsigned char *buf, int count)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;
	int c, total = 0;

#ifdef SERIAL_DEBUG_IO
	printk("cy_write %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_write")) {
		return 0;
	}

	if (!info->xmit_buf) {
		return 0;
	}

	while (1) {
		local_irq_save(flags);
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					  SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0) {
			local_irq_restore(flags);
			break;
		}

		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head =
		    (info->xmit_head + c) & (SERIAL_XMIT_SIZE - 1);
		info->xmit_cnt += c;
		local_irq_restore(flags);

		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		start_xmit(info);
	}
	return total;
}				/* cy_write */

static int cy_write_room(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	int ret;

#ifdef SERIAL_DEBUG_IO
	printk("cy_write_room %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_write_room"))
		return 0;
	ret = PAGE_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}				/* cy_write_room */

static int cy_chars_in_buffer(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;

#ifdef SERIAL_DEBUG_IO
	printk("cy_chars_in_buffer %s %d\n", tty->name, info->xmit_cnt);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_chars_in_buffer"))
		return 0;

	return info->xmit_cnt;
}				/* cy_chars_in_buffer */

static void cy_flush_buffer(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;

#ifdef SERIAL_DEBUG_IO
	printk("cy_flush_buffer %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_flush_buffer"))
		return;
	local_irq_save(flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	local_irq_restore(flags);
	tty_wakeup(tty);
}				/* cy_flush_buffer */

/* This routine is called by the upper-layer tty layer to signal
   that incoming characters should be throttled or that the
   throttle should be released.
 */
static void cy_throttle(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;

#ifdef SERIAL_DEBUG_THROTTLE
	char buf[64];

	printk("throttle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
	printk("cy_throttle %s\n", tty->name);
#endif

	if (serial_paranoia_check(info, tty->name, "cy_nthrottle")) {
		return;
	}

	if (I_IXOFF(tty)) {
		info->x_char = STOP_CHAR(tty);
		/* Should use the "Send Special Character" feature!!! */
	}

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) channel;
	base_addr[CyMSVR1] = 0;
	local_irq_restore(flags);
}				/* cy_throttle */

static void cy_unthrottle(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;

#ifdef SERIAL_DEBUG_THROTTLE
	char buf[64];

	printk("throttle %s: %d....\n", tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
	printk("cy_unthrottle %s\n", tty->name);
#endif

	if (serial_paranoia_check(info, tty->name, "cy_nthrottle")) {
		return;
	}

	if (I_IXOFF(tty)) {
		info->x_char = START_CHAR(tty);
		/* Should use the "Send Special Character" feature!!! */
	}

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) channel;
	base_addr[CyMSVR1] = CyRTS;
	local_irq_restore(flags);
}				/* cy_unthrottle */

static int
get_serial_info(struct cyclades_port *info,
		struct serial_struct __user * retinfo)
{
	struct serial_struct tmp;

/* CP('g'); */
	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->line;
	tmp.irq = 0;
	tmp.flags = info->flags;
	tmp.baud_base = 0;	/*!!! */
	tmp.close_delay = info->close_delay;
	tmp.custom_divisor = 0;	/*!!! */
	tmp.hub6 = 0;		/*!!! */
	return copy_to_user(retinfo, &tmp, sizeof(*retinfo)) ? -EFAULT : 0;
}				/* get_serial_info */

static int
set_serial_info(struct cyclades_port *info,
		struct serial_struct __user * new_info)
{
	struct serial_struct new_serial;
	struct cyclades_port old_info;

/* CP('s'); */
	if (!new_info)
		return -EFAULT;
	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;
	old_info = *info;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ASYNC_FLAGS & ~ASYNC_USR_MASK) !=
		     (info->flags & ASYNC_FLAGS & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		goto check_and_exit;
	}

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->flags = ((info->flags & ~ASYNC_FLAGS) |
		       (new_serial.flags & ASYNC_FLAGS));
	info->close_delay = new_serial.close_delay;

check_and_exit:
	if (info->flags & ASYNC_INITIALIZED) {
		config_setup(info);
		return 0;
	}
	return startup(info);
}				/* set_serial_info */

static int cy_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct cyclades_port *info = tty->driver_data;
	int channel;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long flags;
	unsigned char status;

	channel = info->line;

	local_irq_save(flags);
	base_addr[CyCAR] = (u_char) channel;
	status = base_addr[CyMSVR1] | base_addr[CyMSVR2];
	local_irq_restore(flags);

	return ((status & CyRTS) ? TIOCM_RTS : 0)
	    | ((status & CyDTR) ? TIOCM_DTR : 0)
	    | ((status & CyDCD) ? TIOCM_CAR : 0)
	    | ((status & CyDSR) ? TIOCM_DSR : 0)
	    | ((status & CyCTS) ? TIOCM_CTS : 0);
}				/* cy_tiocmget */

static int
cy_tiocmset(struct tty_struct *tty, struct file *file,
	    unsigned int set, unsigned int clear)
{
	struct cyclades_port *info = tty->driver_data;
	int channel;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long flags;

	channel = info->line;

	if (set & TIOCM_RTS) {
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
		base_addr[CyMSVR1] = CyRTS;
		local_irq_restore(flags);
	}
	if (set & TIOCM_DTR) {
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
/* CP('S');CP('2'); */
		base_addr[CyMSVR2] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: raising DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
		local_irq_restore(flags);
	}

	if (clear & TIOCM_RTS) {
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
		base_addr[CyMSVR1] = 0;
		local_irq_restore(flags);
	}
	if (clear & TIOCM_DTR) {
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
/* CP('C');CP('2'); */
		base_addr[CyMSVR2] = 0;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: dropping DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
		local_irq_restore(flags);
	}

	return 0;
}				/* set_modem_info */

static void send_break(struct cyclades_port *info, int duration)
{				/* Let the transmit ISR take care of this (since it
				   requires stuffing characters into the output stream).
				 */
	info->x_break = duration;
	if (!info->xmit_cnt) {
		start_xmit(info);
	}
}				/* send_break */

static int
get_mon_info(struct cyclades_port *info, struct cyclades_monitor __user * mon)
{

	if (copy_to_user(mon, &info->mon, sizeof(struct cyclades_monitor)))
		return -EFAULT;
	info->mon.int_count = 0;
	info->mon.char_count = 0;
	info->mon.char_max = 0;
	info->mon.char_last = 0;
	return 0;
}

static int set_threshold(struct cyclades_port *info, unsigned long __user * arg)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long value;
	int channel;

	if (get_user(value, arg))
		return -EFAULT;

	channel = info->line;
	info->cor4 &= ~CyREC_FIFO;
	info->cor4 |= value & CyREC_FIFO;
	base_addr[CyCOR4] = info->cor4;
	return 0;
}

static int
get_threshold(struct cyclades_port *info, unsigned long __user * value)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;
	unsigned long tmp;

	channel = info->line;

	tmp = base_addr[CyCOR4] & CyREC_FIFO;
	return put_user(tmp, value);
}

static int
set_default_threshold(struct cyclades_port *info, unsigned long __user * arg)
{
	unsigned long value;

	if (get_user(value, arg))
		return -EFAULT;

	info->default_threshold = value & 0x0f;
	return 0;
}

static int
get_default_threshold(struct cyclades_port *info, unsigned long __user * value)
{
	return put_user(info->default_threshold, value);
}

static int set_timeout(struct cyclades_port *info, unsigned long __user * arg)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;
	unsigned long value;

	if (get_user(value, arg))
		return -EFAULT;

	channel = info->line;

	base_addr[CyRTPRL] = value & 0xff;
	base_addr[CyRTPRH] = (value >> 8) & 0xff;
	return 0;
}

static int get_timeout(struct cyclades_port *info, unsigned long __user * value)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;
	unsigned long tmp;

	channel = info->line;

	tmp = base_addr[CyRTPRL];
	return put_user(tmp, value);
}

static int set_default_timeout(struct cyclades_port *info, unsigned long value)
{
	info->default_timeout = value & 0xff;
	return 0;
}

static int
get_default_timeout(struct cyclades_port *info, unsigned long __user * value)
{
	return put_user(info->default_timeout, value);
}

static int
cy_ioctl(struct tty_struct *tty, struct file *file,
	 unsigned int cmd, unsigned long arg)
{
	unsigned long val;
	struct cyclades_port *info = tty->driver_data;
	int ret_val = 0;
	void __user *argp = (void __user *)arg;

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_ioctl %s, cmd = %x arg = %lx\n", tty->name, cmd, arg);	/* */
#endif

	lock_kernel();

	switch (cmd) {
	case CYGETMON:
		ret_val = get_mon_info(info, argp);
		break;
	case CYGETTHRESH:
		ret_val = get_threshold(info, argp);
		break;
	case CYSETTHRESH:
		ret_val = set_threshold(info, argp);
		break;
	case CYGETDEFTHRESH:
		ret_val = get_default_threshold(info, argp);
		break;
	case CYSETDEFTHRESH:
		ret_val = set_default_threshold(info, argp);
		break;
	case CYGETTIMEOUT:
		ret_val = get_timeout(info, argp);
		break;
	case CYSETTIMEOUT:
		ret_val = set_timeout(info, argp);
		break;
	case CYGETDEFTIMEOUT:
		ret_val = get_default_timeout(info, argp);
		break;
	case CYSETDEFTIMEOUT:
		ret_val = set_default_timeout(info, (unsigned long)arg);
		break;
	case TCSBRK:		/* SVID version: non-zero arg --> no break */
		ret_val = tty_check_change(tty);
		if (ret_val)
			break;
		tty_wait_until_sent(tty, 0);
		if (!arg)
			send_break(info, HZ / 4);	/* 1/4 second */
		break;
	case TCSBRKP:		/* support for POSIX tcsendbreak() */
		ret_val = tty_check_change(tty);
		if (ret_val)
			break;
		tty_wait_until_sent(tty, 0);
		send_break(info, arg ? arg * (HZ / 10) : HZ / 4);
		break;

/* The following commands are incompletely implemented!!! */
	case TIOCGSERIAL:
		ret_val = get_serial_info(info, argp);
		break;
	case TIOCSSERIAL:
		ret_val = set_serial_info(info, argp);
		break;
	default:
		ret_val = -ENOIOCTLCMD;
	}
	unlock_kernel();

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_ioctl done\n");
#endif

	return ret_val;
}				/* cy_ioctl */

static void cy_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct cyclades_port *info = tty->driver_data;

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_set_termios %s\n", tty->name);
#endif

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;
	config_setup(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->stopped = 0;
		cy_start(tty);
	}
#ifdef tytso_patch_94Nov25_1726
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&info->open_wait);
#endif
}				/* cy_set_termios */

static void cy_close(struct tty_struct *tty, struct file *filp)
{
	struct cyclades_port *info = tty->driver_data;

/* CP('C'); */
#ifdef SERIAL_DEBUG_OTHER
	printk("cy_close %s\n", tty->name);
#endif

	if (!info || serial_paranoia_check(info, tty->name, "cy_close")) {
		return;
	}
#ifdef SERIAL_DEBUG_OPEN
	printk("cy_close %s, count = %d\n", tty->name, info->count);
#endif

	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("cy_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
#ifdef SERIAL_DEBUG_COUNT
	printk("cyc: %d: decrementing count to %d\n", __LINE__,
	       info->count - 1);
#endif
	if (--info->count < 0) {
		printk("cy_close: bad serial port count for ttys%d: %d\n",
		       info->line, info->count);
#ifdef SERIAL_DEBUG_COUNT
		printk("cyc: %d: setting count to 0\n", __LINE__);
#endif
		info->count = 0;
	}
	if (info->count)
		return;
	info->flags |= ASYNC_CLOSING;
	if (info->flags & ASYNC_INITIALIZED)
		tty_wait_until_sent(tty, 3000);	/* 30 seconds timeout */
	shutdown(info);
	cy_flush_buffer(tty);
	tty_ldisc_flush(tty);
	info->tty = NULL;
	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs
					     (info->close_delay));
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_close done\n");
#endif
}				/* cy_close */

/*
 * cy_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void cy_hangup(struct tty_struct *tty)
{
	struct cyclades_port *info = tty->driver_data;

#ifdef SERIAL_DEBUG_OTHER
	printk("cy_hangup %s\n", tty->name);	/* */
#endif

	if (serial_paranoia_check(info, tty->name, "cy_hangup"))
		return;

	shutdown(info);
#if 0
	info->event = 0;
	info->count = 0;
#ifdef SERIAL_DEBUG_COUNT
	printk("cyc: %d: setting count to 0\n", __LINE__);
#endif
	info->tty = 0;
#endif
	info->flags &= ~ASYNC_NORMAL_ACTIVE;
	wake_up_interruptible(&info->open_wait);
}				/* cy_hangup */

/*
 * ------------------------------------------------------------
 * cy_open() and friends
 * ------------------------------------------------------------
 */

static int
block_til_ready(struct tty_struct *tty, struct file *filp,
		struct cyclades_port *info)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long flags;
	int channel;
	int retval;
	volatile u_char *base_addr = (u_char *) BASE_ADDR;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
		if (info->flags & ASYNC_HUP_NOTIFY) {
			return -EAGAIN;
		} else {
			return -ERESTARTSYS;
		}
	}

	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if (filp->f_flags & O_NONBLOCK) {
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * cy_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: %s, count = %d\n",
	       tty->name, info->count);
	/**/
#endif
	    info->count--;
#ifdef SERIAL_DEBUG_COUNT
	printk("cyc: %d: decrementing count to %d\n", __LINE__, info->count);
#endif
	info->blocked_open++;

	channel = info->line;

	while (1) {
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
		base_addr[CyMSVR1] = CyRTS;
/* CP('S');CP('4'); */
		base_addr[CyMSVR2] = CyDTR;
#ifdef SERIAL_DEBUG_DTR
		printk("cyc: %d: raising DTR\n", __LINE__);
		printk("     status: 0x%x, 0x%x\n", base_addr[CyMSVR1],
		       base_addr[CyMSVR2]);
#endif
		local_irq_restore(flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp)
		    || !(info->flags & ASYNC_INITIALIZED)) {
			if (info->flags & ASYNC_HUP_NOTIFY) {
				retval = -EAGAIN;
			} else {
				retval = -ERESTARTSYS;
			}
			break;
		}
		local_irq_save(flags);
		base_addr[CyCAR] = (u_char) channel;
/* CP('L');CP1(1 && C_CLOCAL(tty)); CP1(1 && (base_addr[CyMSVR1] & CyDCD) ); */
		if (!(info->flags & ASYNC_CLOSING)
		    && (C_CLOCAL(tty)
			|| (base_addr[CyMSVR1] & CyDCD))) {
			local_irq_restore(flags);
			break;
		}
		local_irq_restore(flags);
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: %s, count = %d\n",
		       tty->name, info->count);
		/**/
#endif
		    schedule();
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp)) {
		info->count++;
#ifdef SERIAL_DEBUG_COUNT
		printk("cyc: %d: incrementing count to %d\n", __LINE__,
		       info->count);
#endif
	}
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: %s, count = %d\n",
	       tty->name, info->count);
	/**/
#endif
	    if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}				/* block_til_ready */

/*
 * This routine is called whenever a serial port is opened.  It
 * performs the serial-specific initialization for the tty structure.
 */
int cy_open(struct tty_struct *tty, struct file *filp)
{
	struct cyclades_port *info;
	int retval, line;

/* CP('O'); */
	line = tty->index;
	if ((line < 0) || (NR_PORTS <= line)) {
		return -ENODEV;
	}
	info = &cy_port[line];
	if (info->line < 0) {
		return -ENODEV;
	}
#ifdef SERIAL_DEBUG_OTHER
	printk("cy_open %s\n", tty->name);	/* */
#endif
	if (serial_paranoia_check(info, tty->name, "cy_open")) {
		return -ENODEV;
	}
#ifdef SERIAL_DEBUG_OPEN
	printk("cy_open %s, count = %d\n", tty->name, info->count);
	/**/
#endif
	    info->count++;
#ifdef SERIAL_DEBUG_COUNT
	printk("cyc: %d: incrementing count to %d\n", __LINE__, info->count);
#endif
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval) {
		return retval;
	}

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("cy_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}
#ifdef SERIAL_DEBUG_OPEN
	printk("cy_open done\n");
	/**/
#endif
	    return 0;
}				/* cy_open */

/*
 * ---------------------------------------------------------------------
 * serial167_init() and friends
 *
 * serial167_init() is called at boot-time to initialize the serial driver.
 * ---------------------------------------------------------------------
 */

/*
 * This routine prints out the appropriate serial driver version
 * number, and identifies which options were configured into this
 * driver.
 */
static void show_version(void)
{
	printk("MVME166/167 cd2401 driver\n");
}				/* show_version */

/* initialize chips on card -- return number of valid
   chips (which is number of ports/4) */

/*
 * This initialises the hardware to a reasonable state.  It should
 * probe the chip first so as to copy 166-Bug setup as a default for
 * port 0.  It initialises CMR to CyASYNC; that is never done again, so
 * as to limit the number of CyINIT_CHAN commands in normal running.
 *
 * ... I wonder what I should do if this fails ...
 */

void mvme167_serial_console_setup(int cflag)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int ch;
	u_char spd;
	u_char rcor, rbpr, badspeed = 0;
	unsigned long flags;

	local_irq_save(flags);

	/*
	 * First probe channel zero of the chip, to see what speed has
	 * been selected.
	 */

	base_addr[CyCAR] = 0;

	rcor = base_addr[CyRCOR] << 5;
	rbpr = base_addr[CyRBPR];

	for (spd = 0; spd < sizeof(baud_bpr); spd++)
		if (rbpr == baud_bpr[spd] && rcor == baud_co[spd])
			break;
	if (spd >= sizeof(baud_bpr)) {
		spd = 14;	/* 19200 */
		badspeed = 1;	/* Failed to identify speed */
	}
	initial_console_speed = spd;

	/* OK, we have chosen a speed, now reset and reinitialise */

	my_udelay(20000L);	/* Allow time for any active o/p to complete */
	if (base_addr[CyCCR] != 0x00) {
		local_irq_restore(flags);
		/* printk(" chip is never idle (CCR != 0)\n"); */
		return;
	}

	base_addr[CyCCR] = CyCHIP_RESET;	/* Reset the chip */
	my_udelay(1000L);

	if (base_addr[CyGFRCR] == 0x00) {
		local_irq_restore(flags);
		/* printk(" chip is not responding (GFRCR stayed 0)\n"); */
		return;
	}

	/*
	 * System clock is 20Mhz, divided by 2048, so divide by 10 for a 1.0ms
	 * tick
	 */

	base_addr[CyTPR] = 10;

	base_addr[CyPILR1] = 0x01;	/* Interrupt level for modem change */
	base_addr[CyPILR2] = 0x02;	/* Interrupt level for tx ints */
	base_addr[CyPILR3] = 0x03;	/* Interrupt level for rx ints */

	/*
	 * Attempt to set up all channels to something reasonable, and
	 * bang out a INIT_CHAN command.  We should then be able to limit
	 * the amount of fiddling we have to do in normal running.
	 */

	for (ch = 3; ch >= 0; ch--) {
		base_addr[CyCAR] = (u_char) ch;
		base_addr[CyIER] = 0;
		base_addr[CyCMR] = CyASYNC;
		base_addr[CyLICR] = (u_char) ch << 2;
		base_addr[CyLIVR] = 0x5c;
		base_addr[CyTCOR] = baud_co[spd];
		base_addr[CyTBPR] = baud_bpr[spd];
		base_addr[CyRCOR] = baud_co[spd] >> 5;
		base_addr[CyRBPR] = baud_bpr[spd];
		base_addr[CySCHR1] = 'Q' & 0x1f;
		base_addr[CySCHR2] = 'X' & 0x1f;
		base_addr[CySCRL] = 0;
		base_addr[CySCRH] = 0;
		base_addr[CyCOR1] = Cy_8_BITS | CyPARITY_NONE;
		base_addr[CyCOR2] = 0;
		base_addr[CyCOR3] = Cy_1_STOP;
		base_addr[CyCOR4] = baud_cor4[spd];
		base_addr[CyCOR5] = 0;
		base_addr[CyCOR6] = 0;
		base_addr[CyCOR7] = 0;
		base_addr[CyRTPRL] = 2;
		base_addr[CyRTPRH] = 0;
		base_addr[CyMSVR1] = 0;
		base_addr[CyMSVR2] = 0;
		write_cy_cmd(base_addr, CyINIT_CHAN | CyDIS_RCVR | CyDIS_XMTR);
	}

	/*
	 * Now do specials for channel zero....
	 */

	base_addr[CyMSVR1] = CyRTS;
	base_addr[CyMSVR2] = CyDTR;
	base_addr[CyIER] = CyRxData;
	write_cy_cmd(base_addr, CyENB_RCVR | CyENB_XMTR);

	local_irq_restore(flags);

	my_udelay(20000L);	/* Let it all settle down */

	printk("CD2401 initialised,  chip is rev 0x%02x\n", base_addr[CyGFRCR]);
	if (badspeed)
		printk
		    ("  WARNING:  Failed to identify line speed, rcor=%02x,rbpr=%02x\n",
		     rcor >> 5, rbpr);
}				/* serial_console_init */

static const struct tty_operations cy_ops = {
	.open = cy_open,
	.close = cy_close,
	.write = cy_write,
	.put_char = cy_put_char,
	.flush_chars = cy_flush_chars,
	.write_room = cy_write_room,
	.chars_in_buffer = cy_chars_in_buffer,
	.flush_buffer = cy_flush_buffer,
	.ioctl = cy_ioctl,
	.throttle = cy_throttle,
	.unthrottle = cy_unthrottle,
	.set_termios = cy_set_termios,
	.stop = cy_stop,
	.start = cy_start,
	.hangup = cy_hangup,
	.tiocmget = cy_tiocmget,
	.tiocmset = cy_tiocmset,
};

/* The serial driver boot-time initialization code!
    Hardware I/O ports are mapped to character special devices on a
    first found, first allocated manner.  That is, this code searches
    for Cyclom cards in the system.  As each is found, it is probed
    to discover how many chips (and thus how many ports) are present.
    These ports are mapped to the tty ports 64 and upward in monotonic
    fashion.  If an 8-port card is replaced with a 16-port card, the
    port mapping on a following card will shift.

    This approach is different from what is used in the other serial
    device driver because the Cyclom is more properly a multiplexer,
    not just an aggregation of serial ports on one card.

    If there are more cards with more ports than have been statically
    allocated above, a warning is printed and the extra ports are ignored.
 */
static int __init serial167_init(void)
{
	struct cyclades_port *info;
	int ret = 0;
	int good_ports = 0;
	int port_num = 0;
	int index;
	int DefSpeed;
#ifdef notyet
	struct sigaction sa;
#endif

	if (!(mvme16x_config & MVME16x_CONFIG_GOT_CD2401))
		return 0;

	cy_serial_driver = alloc_tty_driver(NR_PORTS);
	if (!cy_serial_driver)
		return -ENOMEM;

#if 0
	scrn[1] = '\0';
#endif

	show_version();

	/* Has "console=0,9600n8" been used in bootinfo to change speed? */
	if (serial_console_cflag)
		DefSpeed = serial_console_cflag & 0017;
	else {
		DefSpeed = initial_console_speed;
		serial_console_info = &cy_port[0];
		serial_console_cflag = DefSpeed | CS8;
#if 0
		serial_console = 64;	/*callout_driver.minor_start */
#endif
	}

	/* Initialize the tty_driver structure */

	cy_serial_driver->owner = THIS_MODULE;
	cy_serial_driver->name = "ttyS";
	cy_serial_driver->major = TTY_MAJOR;
	cy_serial_driver->minor_start = 64;
	cy_serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	cy_serial_driver->subtype = SERIAL_TYPE_NORMAL;
	cy_serial_driver->init_termios = tty_std_termios;
	cy_serial_driver->init_termios.c_cflag =
	    B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	cy_serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(cy_serial_driver, &cy_ops);

	ret = tty_register_driver(cy_serial_driver);
	if (ret) {
		printk(KERN_ERR "Couldn't register MVME166/7 serial driver\n");
		put_tty_driver(cy_serial_driver);
		return ret;
	}

	port_num = 0;
	info = cy_port;
	for (index = 0; index < 1; index++) {

		good_ports = 4;

		if (port_num < NR_PORTS) {
			while (good_ports-- && port_num < NR_PORTS) {
		/*** initialize port ***/
				info->magic = CYCLADES_MAGIC;
				info->type = PORT_CIRRUS;
				info->card = index;
				info->line = port_num;
				info->flags = STD_COM_FLAGS;
				info->tty = NULL;
				info->xmit_fifo_size = 12;
				info->cor1 = CyPARITY_NONE | Cy_8_BITS;
				info->cor2 = CyETC;
				info->cor3 = Cy_1_STOP;
				info->cor4 = 0x08;	/* _very_ small receive threshold */
				info->cor5 = 0;
				info->cor6 = 0;
				info->cor7 = 0;
				info->tbpr = baud_bpr[DefSpeed];	/* Tx BPR */
				info->tco = baud_co[DefSpeed];	/* Tx CO */
				info->rbpr = baud_bpr[DefSpeed];	/* Rx BPR */
				info->rco = baud_co[DefSpeed] >> 5;	/* Rx CO */
				info->close_delay = 0;
				info->x_char = 0;
				info->count = 0;
#ifdef SERIAL_DEBUG_COUNT
				printk("cyc: %d: setting count to 0\n",
				       __LINE__);
#endif
				info->blocked_open = 0;
				info->default_threshold = 0;
				info->default_timeout = 0;
				init_waitqueue_head(&info->open_wait);
				init_waitqueue_head(&info->close_wait);
				/* info->session */
				/* info->pgrp */
/*** !!!!!!!! this may expose new bugs !!!!!!!!! *********/
				info->read_status_mask =
				    CyTIMEOUT | CySPECHAR | CyBREAK | CyPARITY |
				    CyFRAME | CyOVERRUN;
				/* info->timeout */

				printk("ttyS%d ", info->line);
				port_num++;
				info++;
				if (!(port_num & 7)) {
					printk("\n               ");
				}
			}
		}
		printk("\n");
	}
	while (port_num < NR_PORTS) {
		info->line = -1;
		port_num++;
		info++;
	}
#ifdef CONFIG_REMOTE_DEBUG
	debug_setup();
#endif
	ret = request_irq(MVME167_IRQ_SER_ERR, cd2401_rxerr_interrupt, 0,
			  "cd2401_errors", cd2401_rxerr_interrupt);
	if (ret) {
		printk(KERN_ERR "Could't get cd2401_errors IRQ");
		goto cleanup_serial_driver;
	}

	ret = request_irq(MVME167_IRQ_SER_MODEM, cd2401_modem_interrupt, 0,
			  "cd2401_modem", cd2401_modem_interrupt);
	if (ret) {
		printk(KERN_ERR "Could't get cd2401_modem IRQ");
		goto cleanup_irq_cd2401_errors;
	}

	ret = request_irq(MVME167_IRQ_SER_TX, cd2401_tx_interrupt, 0,
			  "cd2401_txints", cd2401_tx_interrupt);
	if (ret) {
		printk(KERN_ERR "Could't get cd2401_txints IRQ");
		goto cleanup_irq_cd2401_modem;
	}

	ret = request_irq(MVME167_IRQ_SER_RX, cd2401_rx_interrupt, 0,
			  "cd2401_rxints", cd2401_rx_interrupt);
	if (ret) {
		printk(KERN_ERR "Could't get cd2401_rxints IRQ");
		goto cleanup_irq_cd2401_txints;
	}

	/* Now we have registered the interrupt handlers, allow the interrupts */

	pcc2chip[PccSCCMICR] = 0x15;	/* Serial ints are level 5 */
	pcc2chip[PccSCCTICR] = 0x15;
	pcc2chip[PccSCCRICR] = 0x15;

	pcc2chip[PccIMLR] = 3;	/* Allow PCC2 ints above 3!? */

	return 0;
cleanup_irq_cd2401_txints:
	free_irq(MVME167_IRQ_SER_TX, cd2401_tx_interrupt);
cleanup_irq_cd2401_modem:
	free_irq(MVME167_IRQ_SER_MODEM, cd2401_modem_interrupt);
cleanup_irq_cd2401_errors:
	free_irq(MVME167_IRQ_SER_ERR, cd2401_rxerr_interrupt);
cleanup_serial_driver:
	if (tty_unregister_driver(cy_serial_driver))
		printk(KERN_ERR
		       "Couldn't unregister MVME166/7 serial driver\n");
	put_tty_driver(cy_serial_driver);
	return ret;
}				/* serial167_init */

module_init(serial167_init);

#ifdef CYCLOM_SHOW_STATUS
static void show_status(int line_num)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int channel;
	struct cyclades_port *info;
	unsigned long flags;

	info = &cy_port[line_num];
	channel = info->line;
	printk("  channel %d\n", channel);
	/**/ printk(" cy_port\n");
	printk("  card line flags = %d %d %x\n",
	       info->card, info->line, info->flags);
	printk
	    ("  *tty read_status_mask timeout xmit_fifo_size = %lx %x %x %x\n",
	     (long)info->tty, info->read_status_mask, info->timeout,
	     info->xmit_fifo_size);
	printk("  cor1,cor2,cor3,cor4,cor5,cor6,cor7 = %x %x %x %x %x %x %x\n",
	       info->cor1, info->cor2, info->cor3, info->cor4, info->cor5,
	       info->cor6, info->cor7);
	printk("  tbpr,tco,rbpr,rco = %d %d %d %d\n", info->tbpr, info->tco,
	       info->rbpr, info->rco);
	printk("  close_delay event count = %d %d %d\n", info->close_delay,
	       info->event, info->count);
	printk("  x_char blocked_open = %x %x\n", info->x_char,
	       info->blocked_open);
	printk("  open_wait = %lx %lx %lx\n", (long)info->open_wait);

	local_irq_save(flags);

/* Global Registers */

	printk(" CyGFRCR %x\n", base_addr[CyGFRCR]);
	printk(" CyCAR %x\n", base_addr[CyCAR]);
	printk(" CyRISR %x\n", base_addr[CyRISR]);
	printk(" CyTISR %x\n", base_addr[CyTISR]);
	printk(" CyMISR %x\n", base_addr[CyMISR]);
	printk(" CyRIR %x\n", base_addr[CyRIR]);
	printk(" CyTIR %x\n", base_addr[CyTIR]);
	printk(" CyMIR %x\n", base_addr[CyMIR]);
	printk(" CyTPR %x\n", base_addr[CyTPR]);

	base_addr[CyCAR] = (u_char) channel;

/* Virtual Registers */

#if 0
	printk(" CyRIVR %x\n", base_addr[CyRIVR]);
	printk(" CyTIVR %x\n", base_addr[CyTIVR]);
	printk(" CyMIVR %x\n", base_addr[CyMIVR]);
	printk(" CyMISR %x\n", base_addr[CyMISR]);
#endif

/* Channel Registers */

	printk(" CyCCR %x\n", base_addr[CyCCR]);
	printk(" CyIER %x\n", base_addr[CyIER]);
	printk(" CyCOR1 %x\n", base_addr[CyCOR1]);
	printk(" CyCOR2 %x\n", base_addr[CyCOR2]);
	printk(" CyCOR3 %x\n", base_addr[CyCOR3]);
	printk(" CyCOR4 %x\n", base_addr[CyCOR4]);
	printk(" CyCOR5 %x\n", base_addr[CyCOR5]);
#if 0
	printk(" CyCCSR %x\n", base_addr[CyCCSR]);
	printk(" CyRDCR %x\n", base_addr[CyRDCR]);
#endif
	printk(" CySCHR1 %x\n", base_addr[CySCHR1]);
	printk(" CySCHR2 %x\n", base_addr[CySCHR2]);
#if 0
	printk(" CySCHR3 %x\n", base_addr[CySCHR3]);
	printk(" CySCHR4 %x\n", base_addr[CySCHR4]);
	printk(" CySCRL %x\n", base_addr[CySCRL]);
	printk(" CySCRH %x\n", base_addr[CySCRH]);
	printk(" CyLNC %x\n", base_addr[CyLNC]);
	printk(" CyMCOR1 %x\n", base_addr[CyMCOR1]);
	printk(" CyMCOR2 %x\n", base_addr[CyMCOR2]);
#endif
	printk(" CyRTPRL %x\n", base_addr[CyRTPRL]);
	printk(" CyRTPRH %x\n", base_addr[CyRTPRH]);
	printk(" CyMSVR1 %x\n", base_addr[CyMSVR1]);
	printk(" CyMSVR2 %x\n", base_addr[CyMSVR2]);
	printk(" CyRBPR %x\n", base_addr[CyRBPR]);
	printk(" CyRCOR %x\n", base_addr[CyRCOR]);
	printk(" CyTBPR %x\n", base_addr[CyTBPR]);
	printk(" CyTCOR %x\n", base_addr[CyTCOR]);

	local_irq_restore(flags);
}				/* show_status */
#endif

#if 0
/* Dummy routine in mvme16x/config.c for now */

/* Serial console setup. Called from linux/init/main.c */

void console_setup(char *str, int *ints)
{
	char *s;
	int baud, bits, parity;
	int cflag = 0;

	/* Sanity check. */
	if (ints[0] > 3 || ints[1] > 3)
		return;

	/* Get baud, bits and parity */
	baud = 2400;
	bits = 8;
	parity = 'n';
	if (ints[2])
		baud = ints[2];
	if ((s = strchr(str, ','))) {
		do {
			s++;
		} while (*s >= '0' && *s <= '9');
		if (*s)
			parity = *s++;
		if (*s)
			bits = *s - '0';
	}

	/* Now construct a cflag setting. */
	switch (baud) {
	case 1200:
		cflag |= B1200;
		break;
	case 9600:
		cflag |= B9600;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 2400:
	default:
		cflag |= B2400;
		break;
	}
	switch (bits) {
	case 7:
		cflag |= CS7;
		break;
	default:
	case 8:
		cflag |= CS8;
		break;
	}
	switch (parity) {
	case 'o':
	case 'O':
		cflag |= PARODD;
		break;
	case 'e':
	case 'E':
		cflag |= PARENB;
		break;
	}

	serial_console_info = &cy_port[ints[1]];
	serial_console_cflag = cflag;
	serial_console = ints[1] + 64;	/*callout_driver.minor_start */
}
#endif

/*
 * The following is probably out of date for 2.1.x serial console stuff.
 *
 * The console is registered early on from arch/m68k/kernel/setup.c, and
 * it therefore relies on the chip being setup correctly by 166-Bug.  This
 * seems reasonable, as the serial port has been used to invoke the system
 * boot.  It also means that this function must not rely on any data
 * initialisation performed by serial167_init() etc.
 *
 * Of course, once the console has been registered, we had better ensure
 * that serial167_init() doesn't leave the chip non-functional.
 *
 * The console must be locked when we get here.
 */

void serial167_console_write(struct console *co, const char *str,
			     unsigned count)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long flags;
	volatile u_char sink;
	u_char ier;
	int port;
	u_char do_lf = 0;
	int i = 0;

	local_irq_save(flags);

	/* Ensure transmitter is enabled! */

	port = 0;
	base_addr[CyCAR] = (u_char) port;
	while (base_addr[CyCCR])
		;
	base_addr[CyCCR] = CyENB_XMTR;

	ier = base_addr[CyIER];
	base_addr[CyIER] = CyTxMpty;

	while (1) {
		if (pcc2chip[PccSCCTICR] & 0x20) {
			/* We have a Tx int. Acknowledge it */
			sink = pcc2chip[PccTPIACKR];
			if ((base_addr[CyLICR] >> 2) == port) {
				if (i == count) {
					/* Last char of string is now output */
					base_addr[CyTEOIR] = CyNOTRANS;
					break;
				}
				if (do_lf) {
					base_addr[CyTDR] = '\n';
					str++;
					i++;
					do_lf = 0;
				} else if (*str == '\n') {
					base_addr[CyTDR] = '\r';
					do_lf = 1;
				} else {
					base_addr[CyTDR] = *str++;
					i++;
				}
				base_addr[CyTEOIR] = 0;
			} else
				base_addr[CyTEOIR] = CyNOTRANS;
		}
	}

	base_addr[CyIER] = ier;

	local_irq_restore(flags);
}

static struct tty_driver *serial167_console_device(struct console *c,
						   int *index)
{
	*index = c->index;
	return cy_serial_driver;
}

static struct console sercons = {
	.name = "ttyS",
	.write = serial167_console_write,
	.device = serial167_console_device,
	.flags = CON_PRINTBUFFER,
	.index = -1,
};

static int __init serial167_console_init(void)
{
	if (vme_brdtype == VME_TYPE_MVME166 ||
	    vme_brdtype == VME_TYPE_MVME167 ||
	    vme_brdtype == VME_TYPE_MVME177) {
		mvme167_serial_console_setup(0);
		register_console(&sercons);
	}
	return 0;
}

console_initcall(serial167_console_init);

#ifdef CONFIG_REMOTE_DEBUG
void putDebugChar(int c)
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long flags;
	volatile u_char sink;
	u_char ier;
	int port;

	local_irq_save(flags);

	/* Ensure transmitter is enabled! */

	port = DEBUG_PORT;
	base_addr[CyCAR] = (u_char) port;
	while (base_addr[CyCCR])
		;
	base_addr[CyCCR] = CyENB_XMTR;

	ier = base_addr[CyIER];
	base_addr[CyIER] = CyTxMpty;

	while (1) {
		if (pcc2chip[PccSCCTICR] & 0x20) {
			/* We have a Tx int. Acknowledge it */
			sink = pcc2chip[PccTPIACKR];
			if ((base_addr[CyLICR] >> 2) == port) {
				base_addr[CyTDR] = c;
				base_addr[CyTEOIR] = 0;
				break;
			} else
				base_addr[CyTEOIR] = CyNOTRANS;
		}
	}

	base_addr[CyIER] = ier;

	local_irq_restore(flags);
}

int getDebugChar()
{
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	unsigned long flags;
	volatile u_char sink;
	u_char ier;
	int port;
	int i, c;

	i = debugiq.out;
	if (i != debugiq.in) {
		c = debugiq.buf[i];
		if (++i == DEBUG_LEN)
			i = 0;
		debugiq.out = i;
		return c;
	}
	/* OK, nothing in queue, wait in poll loop */

	local_irq_save(flags);

	/* Ensure receiver is enabled! */

	port = DEBUG_PORT;
	base_addr[CyCAR] = (u_char) port;
#if 0
	while (base_addr[CyCCR])
		;
	base_addr[CyCCR] = CyENB_RCVR;
#endif
	ier = base_addr[CyIER];
	base_addr[CyIER] = CyRxData;

	while (1) {
		if (pcc2chip[PccSCCRICR] & 0x20) {
			/* We have a Rx int. Acknowledge it */
			sink = pcc2chip[PccRPIACKR];
			if ((base_addr[CyLICR] >> 2) == port) {
				int cnt = base_addr[CyRFOC];
				while (cnt-- > 0) {
					c = base_addr[CyRDR];
					if (c == 0)
						printk
						    ("!! debug char is null (cnt=%d) !!",
						     cnt);
					else
						queueDebugChar(c);
				}
				base_addr[CyREOIR] = 0;
				i = debugiq.out;
				if (i == debugiq.in)
					panic("Debug input queue empty!");
				c = debugiq.buf[i];
				if (++i == DEBUG_LEN)
					i = 0;
				debugiq.out = i;
				break;
			} else
				base_addr[CyREOIR] = CyNOTRANS;
		}
	}

	base_addr[CyIER] = ier;

	local_irq_restore(flags);

	return (c);
}

void queueDebugChar(int c)
{
	int i;

	i = debugiq.in;
	debugiq.buf[i] = c;
	if (++i == DEBUG_LEN)
		i = 0;
	if (i != debugiq.out)
		debugiq.in = i;
}

static void debug_setup()
{
	unsigned long flags;
	volatile unsigned char *base_addr = (u_char *) BASE_ADDR;
	int i, cflag;

	cflag = B19200;

	local_irq_save(flags);

	for (i = 0; i < 4; i++) {
		base_addr[CyCAR] = i;
		base_addr[CyLICR] = i << 2;
	}

	debugiq.in = debugiq.out = 0;

	base_addr[CyCAR] = DEBUG_PORT;

	/* baud rate */
	i = cflag & CBAUD;

	base_addr[CyIER] = 0;

	base_addr[CyCMR] = CyASYNC;
	base_addr[CyLICR] = DEBUG_PORT << 2;
	base_addr[CyLIVR] = 0x5c;

	/* tx and rx baud rate */

	base_addr[CyTCOR] = baud_co[i];
	base_addr[CyTBPR] = baud_bpr[i];
	base_addr[CyRCOR] = baud_co[i] >> 5;
	base_addr[CyRBPR] = baud_bpr[i];

	/* set line characteristics  according configuration */

	base_addr[CySCHR1] = 0;
	base_addr[CySCHR2] = 0;
	base_addr[CySCRL] = 0;
	base_addr[CySCRH] = 0;
	base_addr[CyCOR1] = Cy_8_BITS | CyPARITY_NONE;
	base_addr[CyCOR2] = 0;
	base_addr[CyCOR3] = Cy_1_STOP;
	base_addr[CyCOR4] = baud_cor4[i];
	base_addr[CyCOR5] = 0;
	base_addr[CyCOR6] = 0;
	base_addr[CyCOR7] = 0;

	write_cy_cmd(base_addr, CyINIT_CHAN);
	write_cy_cmd(base_addr, CyENB_RCVR);

	base_addr[CyCAR] = DEBUG_PORT;	/* !!! Is this needed? */

	base_addr[CyRTPRL] = 2;
	base_addr[CyRTPRH] = 0;

	base_addr[CyMSVR1] = CyRTS;
	base_addr[CyMSVR2] = CyDTR;

	base_addr[CyIER] = CyRxData;

	local_irq_restore(flags);

}				/* debug_setup */

#endif

MODULE_LICENSE("GPL");
