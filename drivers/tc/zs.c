/*
 * decserial.c: Serial port driver for IOASIC DECstations.
 *
 * Derived from drivers/sbus/char/sunserial.c by Paul Mackerras.
 * Derived from drivers/macintosh/macserial.c by Harald Koerfgen.
 *
 * DECstation changes
 * Copyright (C) 1998-2000 Harald Koerfgen
 * Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005  Maciej W. Rozycki
 *
 * For the rest of the code the original Copyright applies:
 * Copyright (C) 1996 Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 *
 * Note: for IOASIC systems the wiring is as follows:
 *
 * mouse/keyboard:
 * DIN-7 MJ-4  signal        SCC
 * 2     1     TxD       <-  A.TxD
 * 3     4     RxD       ->  A.RxD
 *
 * EIA-232/EIA-423:
 * DB-25 MMJ-6 signal        SCC
 * 2     2     TxD       <-  B.TxD
 * 3     5     RxD       ->  B.RxD
 * 4           RTS       <- ~A.RTS
 * 5           CTS       -> ~B.CTS
 * 6     6     DSR       -> ~A.SYNC
 * 8           CD        -> ~B.DCD
 * 12          DSRS(DCE) -> ~A.CTS  (*)
 * 15          TxC       ->  B.TxC
 * 17          RxC       ->  B.RxC
 * 20    1     DTR       <- ~A.DTR
 * 22          RI        -> ~A.DCD
 * 23          DSRS(DTE) <- ~B.RTS
 *
 * (*) EIA-232 defines the signal at this pin to be SCD, while DSRS(DCE)
 *     is shared with DSRS(DTE) at pin 23.
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#ifdef CONFIG_SERIAL_DEC_CONSOLE
#include <linux/console.h>
#endif

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/bootinfo.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/machtype.h>
#include <asm/dec/serial.h>
#include <asm/dec/system.h>

#ifdef CONFIG_KGDB
#include <asm/kgdb.h>
#endif
#ifdef CONFIG_MAGIC_SYSRQ
#include <linux/sysrq.h>
#endif

#include "zs.h"

/*
 * It would be nice to dynamically allocate everything that
 * depends on NUM_SERIAL, so we could support any number of
 * Z8530s, but for now...
 */
#define NUM_SERIAL	2		/* Max number of ZS chips supported */
#define NUM_CHANNELS	(NUM_SERIAL * 2)	/* 2 channels per chip */
#define CHANNEL_A_NR  (zs_parms->channel_a_offset > zs_parms->channel_b_offset)
                                        /* Number of channel A in the chip */
#define ZS_CHAN_IO_SIZE 8
#define ZS_CLOCK        7372800 	/* Z8530 RTxC input clock rate */

#define RECOVERY_DELAY  udelay(2)

struct zs_parms {
	unsigned long scc0;
	unsigned long scc1;
	int channel_a_offset;
	int channel_b_offset;
	int irq0;
	int irq1;
	int clock;
};

static struct zs_parms *zs_parms;

#ifdef CONFIG_MACH_DECSTATION
static struct zs_parms ds_parms = {
	scc0 : IOASIC_SCC0,
	scc1 : IOASIC_SCC1,
	channel_a_offset : 1,
	channel_b_offset : 9,
	irq0 : -1,
	irq1 : -1,
	clock : ZS_CLOCK
};
#endif

#ifdef CONFIG_MACH_DECSTATION
#define DS_BUS_PRESENT (IOASIC)
#else
#define DS_BUS_PRESENT 0
#endif

#define BUS_PRESENT (DS_BUS_PRESENT)

DEFINE_SPINLOCK(zs_lock);

struct dec_zschannel zs_channels[NUM_CHANNELS];
struct dec_serial zs_soft[NUM_CHANNELS];
int zs_channels_found;
struct dec_serial *zs_chain;	/* list of all channels */

struct tty_struct zs_ttys[NUM_CHANNELS];

#ifdef CONFIG_SERIAL_DEC_CONSOLE
static struct console sercons;
#endif
#if defined(CONFIG_SERIAL_DEC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ) && \
   !defined(MODULE)
static unsigned long break_pressed; /* break, really ... */
#endif

static unsigned char zs_init_regs[16] __initdata = {
	0,				/* write 0 */
	0,				/* write 1 */
	0,				/* write 2 */
	0,				/* write 3 */
	(X16CLK),			/* write 4 */
	0,				/* write 5 */
	0, 0, 0,			/* write 6, 7, 8 */
	(MIE | DLC | NV),		/* write 9 */
	(NRZ),				/* write 10 */
	(TCBR | RCBR),			/* write 11 */
	0, 0,				/* BRG time constant, write 12 + 13 */
	(BRSRC | BRENABL),		/* write 14 */
	0				/* write 15 */
};

static struct tty_driver *serial_driver;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/*
 * Debugging.
 */
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_THROTTLE
#undef SERIAL_PARANOIA_CHECK

#undef ZS_DEBUG_REGS

#ifdef SERIAL_DEBUG_THROTTLE
#define _tty_name(tty,buf) tty_name(tty,buf)
#endif

#define RS_STROBE_TIME 10
#define RS_ISR_PASS_LIMIT 256

static void probe_sccs(void);
static void change_speed(struct dec_serial *info);
static void rs_wait_until_sent(struct tty_struct *tty, int timeout);

static inline int serial_paranoia_check(struct dec_serial *info,
					char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct %s in %s\n";
	static const char *badinfo =
		"Warning: null mac_serial for %s in %s\n";

	if (!info) {
		printk(badinfo, name, routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, name, routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 0 };

/*
 * Reading and writing Z8530 registers.
 */
static inline unsigned char read_zsreg(struct dec_zschannel *channel,
				       unsigned char reg)
{
	unsigned char retval;

	if (reg != 0) {
		*channel->control = reg & 0xf;
		fast_iob(); RECOVERY_DELAY;
	}
	retval = *channel->control;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsreg(struct dec_zschannel *channel,
			       unsigned char reg, unsigned char value)
{
	if (reg != 0) {
		*channel->control = reg & 0xf;
		fast_iob(); RECOVERY_DELAY;
	}
	*channel->control = value;
	fast_iob(); RECOVERY_DELAY;
	return;
}

static inline unsigned char read_zsdata(struct dec_zschannel *channel)
{
	unsigned char retval;

	retval = *channel->data;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsdata(struct dec_zschannel *channel,
				unsigned char value)
{
	*channel->data = value;
	fast_iob(); RECOVERY_DELAY;
	return;
}

static inline void load_zsregs(struct dec_zschannel *channel,
			       unsigned char *regs)
{
/*	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel); */
	/* Load 'em up */
	write_zsreg(channel, R3, regs[R3] & ~RxENABLE);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R9, regs[R9]);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R2, regs[R2]);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R11, regs[R11]);
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	write_zsreg(channel, R14, regs[R14]);
	write_zsreg(channel, R15, regs[R15]);
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);
	return;
}

/* Sets or clears DTR/RTS on the requested line */
static inline void zs_rtsdtr(struct dec_serial *info, int which, int set)
{
        unsigned long flags;

	spin_lock_irqsave(&zs_lock, flags);
	if (info->zs_channel != info->zs_chan_a) {
		if (set) {
			info->zs_chan_a->curregs[5] |= (which & (RTS | DTR));
		} else {
			info->zs_chan_a->curregs[5] &= ~(which & (RTS | DTR));
		}
		write_zsreg(info->zs_chan_a, 5, info->zs_chan_a->curregs[5]);
	}
	spin_unlock_irqrestore(&zs_lock, flags);
}

/* Utility routines for the Zilog */
static inline int get_zsbaud(struct dec_serial *ss)
{
	struct dec_zschannel *channel = ss->zs_channel;
	int brg;

	/* The baud rate is split up between two 8-bit registers in
	 * what is termed 'BRG time constant' format in my docs for
	 * the chip, it is a function of the clk rate the chip is
	 * receiving which happens to be constant.
	 */
	brg = (read_zsreg(channel, 13) << 8);
	brg |= read_zsreg(channel, 12);
	return BRG_TO_BPS(brg, (zs_parms->clock/(ss->clk_divisor)));
}

/* On receive, this clears errors and the receiver interrupts */
static inline void rs_recv_clear(struct dec_zschannel *zsc)
{
	write_zsreg(zsc, 0, ERR_RES);
	write_zsreg(zsc, 0, RES_H_IUS); /* XXX this is unnecessary */
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static void rs_sched_event(struct dec_serial *info, int event)
{
	info->event |= 1 << event;
	tasklet_schedule(&info->tlet);
}

static void receive_chars(struct dec_serial *info)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat, flag;

	while ((read_zsreg(info->zs_channel, R0) & Rx_CH_AV) != 0) {

		stat = read_zsreg(info->zs_channel, R1);
		ch = read_zsdata(info->zs_channel);

		if (!tty && (!info->hook || !info->hook->rx_char))
			continue;

		flag = TTY_NORMAL;
		if (info->tty_break) {
			info->tty_break = 0;
			flag = TTY_BREAK;
			if (info->flags & ZILOG_SAK)
				do_SAK(tty);
			/* Ignore the null char got when BREAK is removed.  */
			if (ch == 0)
				continue;
		} else {
			if (stat & Rx_OVR) {
				flag = TTY_OVERRUN;
			} else if (stat & FRM_ERR) {
				flag = TTY_FRAME;
			} else if (stat & PAR_ERR) {
				flag = TTY_PARITY;
			}
			if (flag != TTY_NORMAL)
				/* reset the error indication */
				write_zsreg(info->zs_channel, R0, ERR_RES);
		}

#if defined(CONFIG_SERIAL_DEC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ) && \
   !defined(MODULE)
		if (break_pressed && info->line == sercons.index) {
			/* Ignore the null char got when BREAK is removed.  */
			if (ch == 0)
				continue;
			if (time_before(jiffies, break_pressed + HZ * 5)) {
				handle_sysrq(ch, NULL);
				break_pressed = 0;
				continue;
			}
			break_pressed = 0;
		}
#endif

		if (info->hook && info->hook->rx_char) {
			(*info->hook->rx_char)(ch, flag);
			return;
  		}

		tty_insert_flip_char(tty, ch, flag);
	}
	if (tty)
		tty_flip_buffer_push(tty);
}

static void transmit_chars(struct dec_serial *info)
{
	if ((read_zsreg(info->zs_channel, R0) & Tx_BUF_EMP) == 0)
		return;
	info->tx_active = 0;

	if (info->x_char) {
		/* Send next char */
		write_zsdata(info->zs_channel, info->x_char);
		info->x_char = 0;
		info->tx_active = 1;
		return;
	}

	if ((info->xmit_cnt <= 0) || (info->tty && info->tty->stopped)
	    || info->tx_stopped) {
		write_zsreg(info->zs_channel, R0, RES_Tx_P);
		return;
	}
	/* Send char */
	write_zsdata(info->zs_channel, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;
	info->tx_active = 1;

	if (info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
}

static void status_handle(struct dec_serial *info)
{
	unsigned char stat;

	/* Get status from Read Register 0 */
	stat = read_zsreg(info->zs_channel, R0);

	if ((stat & BRK_ABRT) && !(info->read_reg_zero & BRK_ABRT)) {
#if defined(CONFIG_SERIAL_DEC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ) && \
   !defined(MODULE)
		if (info->line == sercons.index) {
			if (!break_pressed)
				break_pressed = jiffies;
		} else
#endif
			info->tty_break = 1;
	}

	if (info->zs_channel != info->zs_chan_a) {

		/* Check for DCD transitions */
		if (info->tty && !C_CLOCAL(info->tty) &&
		    ((stat ^ info->read_reg_zero) & DCD) != 0 ) {
			if (stat & DCD) {
				wake_up_interruptible(&info->open_wait);
			} else {
				tty_hangup(info->tty);
			}
		}

		/* Check for CTS transitions */
		if (info->tty && C_CRTSCTS(info->tty)) {
			if ((stat & CTS) != 0) {
				if (info->tx_stopped) {
					info->tx_stopped = 0;
					if (!info->tx_active)
						transmit_chars(info);
				}
			} else {
				info->tx_stopped = 1;
			}
		}

	}

	/* Clear status condition... */
	write_zsreg(info->zs_channel, R0, RES_EXT_INT);
	info->read_reg_zero = stat;
}

/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t rs_interrupt(int irq, void *dev_id)
{
	struct dec_serial *info = (struct dec_serial *) dev_id;
	irqreturn_t status = IRQ_NONE;
	unsigned char zs_intreg;
	int shift;

	/* NOTE: The read register 3, which holds the irq status,
	 *       does so for both channels on each chip.  Although
	 *       the status value itself must be read from the A
	 *       channel and is only valid when read from channel A.
	 *       Yes... broken hardware...
	 */
#define CHAN_IRQMASK (CHBRxIP | CHBTxIP | CHBEXT)

	if (info->zs_chan_a == info->zs_channel)
		shift = 3;	/* Channel A */
	else
		shift = 0;	/* Channel B */

	for (;;) {
		zs_intreg = read_zsreg(info->zs_chan_a, R3) >> shift;
		if ((zs_intreg & CHAN_IRQMASK) == 0)
			break;

		status = IRQ_HANDLED;

		if (zs_intreg & CHBRxIP) {
			receive_chars(info);
		}
		if (zs_intreg & CHBTxIP) {
			transmit_chars(info);
		}
		if (zs_intreg & CHBEXT) {
			status_handle(info);
		}
	}

	/* Why do we need this ? */
	write_zsreg(info->zs_channel, 0, RES_H_IUS);

	return status;
}

#ifdef ZS_DEBUG_REGS
void zs_dump (void) {
	int i, j;
	for (i = 0; i < zs_channels_found; i++) {
		struct dec_zschannel *ch = &zs_channels[i];
		if ((long)ch->control == UNI_IO_BASE+UNI_SCC1A_CTRL) {
			for (j = 0; j < 15; j++) {
				printk("W%d = 0x%x\t",
				       j, (int)ch->curregs[j]);
			}
			for (j = 0; j < 15; j++) {
				printk("R%d = 0x%x\t",
				       j, (int)read_zsreg(ch,j));
			}
			printk("\n\n");
		}
	}
}
#endif

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;

#if 1
	spin_lock_irqsave(&zs_lock, flags);
	if (info->zs_channel->curregs[5] & TxENAB) {
		info->zs_channel->curregs[5] &= ~TxENAB;
		write_zsreg(info->zs_channel, 5, info->zs_channel->curregs[5]);
	}
	spin_unlock_irqrestore(&zs_lock, flags);
#endif
}

static void rs_start(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_start"))
		return;

	spin_lock_irqsave(&zs_lock, flags);
#if 1
	if (info->xmit_cnt && info->xmit_buf && !(info->zs_channel->curregs[5] & TxENAB)) {
		info->zs_channel->curregs[5] |= TxENAB;
		write_zsreg(info->zs_channel, 5, info->zs_channel->curregs[5]);
	}
#else
	if (info->xmit_cnt && info->xmit_buf && !info->tx_active) {
		transmit_chars(info);
	}
#endif
	spin_unlock_irqrestore(&zs_lock, flags);
}

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * rs_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */

static void do_softint(unsigned long private_)
{
	struct dec_serial	*info = (struct dec_serial *) private_;
	struct tty_struct	*tty;

	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event))
		tty_wakeup(tty);
}

static int zs_startup(struct dec_serial * info)
{
	unsigned long flags;

	if (info->flags & ZILOG_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) get_zeroed_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	spin_lock_irqsave(&zs_lock, flags);

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (irq %d)...", info->line, info->irq);
#endif

	/*
	 * Clear the receive FIFO.
	 */
	ZS_CLEARFIFO(info->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->zs_channel, R0, ERR_RES);
	write_zsreg(info->zs_channel, R0, RES_H_IUS);

	/*
	 * Set the speed of the serial port
	 */
	change_speed(info);

	/*
	 * Turn on RTS and DTR.
	 */
	zs_rtsdtr(info, RTS | DTR, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	info->zs_channel->curregs[R1] &= ~RxINT_MASK;
	info->zs_channel->curregs[R1] |= (RxINT_ALL | TxINT_ENAB |
					  EXT_INT_ENAB);
	info->zs_channel->curregs[R3] |= RxENABLE;
	info->zs_channel->curregs[R5] |= TxENAB;
	info->zs_channel->curregs[R15] |= (DCDIE | CTSIE | TxUIE | BRKIE);
	write_zsreg(info->zs_channel, R1, info->zs_channel->curregs[R1]);
	write_zsreg(info->zs_channel, R3, info->zs_channel->curregs[R3]);
	write_zsreg(info->zs_channel, R5, info->zs_channel->curregs[R5]);
	write_zsreg(info->zs_channel, R15, info->zs_channel->curregs[R15]);

	/*
	 * And clear the interrupt registers again for luck.
	 */
	write_zsreg(info->zs_channel, R0, ERR_RES);
	write_zsreg(info->zs_channel, R0, RES_H_IUS);

	/* Save the current value of RR0 */
	info->read_reg_zero = read_zsreg(info->zs_channel, R0);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	info->flags |= ZILOG_INITIALIZED;
	spin_unlock_irqrestore(&zs_lock, flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct dec_serial * info)
{
	unsigned long	flags;

	if (!(info->flags & ZILOG_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
	       info->irq);
#endif

	spin_lock_irqsave(&zs_lock, flags);

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	info->zs_channel->curregs[1] = 0;
	write_zsreg(info->zs_channel, 1, info->zs_channel->curregs[1]);	/* no interrupts */

	info->zs_channel->curregs[3] &= ~RxENABLE;
	write_zsreg(info->zs_channel, 3, info->zs_channel->curregs[3]);

	info->zs_channel->curregs[5] &= ~TxENAB;
	write_zsreg(info->zs_channel, 5, info->zs_channel->curregs[5]);
	if (!info->tty || C_HUPCL(info->tty)) {
		zs_rtsdtr(info, RTS | DTR, 0);
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ZILOG_INITIALIZED;
	spin_unlock_irqrestore(&zs_lock, flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct dec_serial *info)
{
	unsigned cflag;
	int	i;
	int	brg, bits;
	unsigned long flags;

	if (!info->hook) {
		if (!info->tty || !info->tty->termios)
			return;
		cflag = info->tty->termios->c_cflag;
		if (!info->port)
			return;
	} else {
		cflag = info->hook->cflags;
	}

	i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2) {
			if (!info->hook)
				info->tty->termios->c_cflag &= ~CBAUDEX;
			else
				info->hook->cflags &= ~CBAUDEX;
		} else
			i += 15;
	}

	spin_lock_irqsave(&zs_lock, flags);
	info->zs_baud = baud_table[i];
	if (info->zs_baud) {
		brg = BPS_TO_BRG(info->zs_baud, zs_parms->clock/info->clk_divisor);
		info->zs_channel->curregs[12] = (brg & 255);
		info->zs_channel->curregs[13] = ((brg >> 8) & 255);
		zs_rtsdtr(info, DTR, 1);
	} else {
		zs_rtsdtr(info, RTS | DTR, 0);
		return;
	}

	/* byte size and parity */
	info->zs_channel->curregs[3] &= ~RxNBITS_MASK;
	info->zs_channel->curregs[5] &= ~TxNBITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		bits = 7;
		info->zs_channel->curregs[3] |= Rx5;
		info->zs_channel->curregs[5] |= Tx5;
		break;
	case CS6:
		bits = 8;
		info->zs_channel->curregs[3] |= Rx6;
		info->zs_channel->curregs[5] |= Tx6;
		break;
	case CS7:
		bits = 9;
		info->zs_channel->curregs[3] |= Rx7;
		info->zs_channel->curregs[5] |= Tx7;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		bits = 10;
		info->zs_channel->curregs[3] |= Rx8;
		info->zs_channel->curregs[5] |= Tx8;
		break;
	}

	info->timeout = ((info->xmit_fifo_size*HZ*bits) / info->zs_baud);
        info->timeout += HZ/50;         /* Add .02 seconds of slop */

	info->zs_channel->curregs[4] &= ~(SB_MASK | PAR_ENA | PAR_EVEN);
	if (cflag & CSTOPB) {
		info->zs_channel->curregs[4] |= SB2;
	} else {
		info->zs_channel->curregs[4] |= SB1;
	}
	if (cflag & PARENB) {
		info->zs_channel->curregs[4] |= PAR_ENA;
	}
	if (!(cflag & PARODD)) {
		info->zs_channel->curregs[4] |= PAR_EVEN;
	}

	if (!(cflag & CLOCAL)) {
		if (!(info->zs_channel->curregs[15] & DCDIE))
			info->read_reg_zero = read_zsreg(info->zs_channel, 0);
		info->zs_channel->curregs[15] |= DCDIE;
	} else
		info->zs_channel->curregs[15] &= ~DCDIE;
	if (cflag & CRTSCTS) {
		info->zs_channel->curregs[15] |= CTSIE;
		if ((read_zsreg(info->zs_channel, 0) & CTS) == 0)
			info->tx_stopped = 1;
	} else {
		info->zs_channel->curregs[15] &= ~CTSIE;
		info->tx_stopped = 0;
	}

	/* Load up the new values */
	load_zsregs(info->zs_channel, info->zs_channel->curregs);

	spin_unlock_irqrestore(&zs_lock, flags);
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || info->tx_stopped ||
	    !info->xmit_buf)
		return;

	/* Enable transmitter */
	spin_lock_irqsave(&zs_lock, flags);
	transmit_chars(info);
	spin_unlock_irqrestore(&zs_lock, flags);
}

static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;

	while (1) {
		spin_lock_irqsave(&zs_lock, flags);
		c = min(count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt += c;
		spin_unlock_irqrestore(&zs_lock, flags);
		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->stopped && !info->tx_stopped
	    && !info->tx_active)
		transmit_chars(info);
	spin_unlock_irqrestore(&zs_lock, flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	int	ret;

	if (serial_paranoia_check(info, tty->name, "rs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;
	spin_lock_irq(&zs_lock);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	spin_unlock_irq(&zs_lock);
	tty_wakeup(tty);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];

	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;

	if (I_IXOFF(tty)) {
		spin_lock_irqsave(&zs_lock, flags);
		info->x_char = STOP_CHAR(tty);
		if (!info->tx_active)
			transmit_chars(info);
		spin_unlock_irqrestore(&zs_lock, flags);
	}

	if (C_CRTSCTS(tty)) {
		zs_rtsdtr(info, RTS, 0);
	}
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];

	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_unthrottle"))
		return;

	if (I_IXOFF(tty)) {
		spin_lock_irqsave(&zs_lock, flags);
		if (info->x_char)
			info->x_char = 0;
		else {
			info->x_char = START_CHAR(tty);
			if (!info->tx_active)
				transmit_chars(info);
		}
		spin_unlock_irqrestore(&zs_lock, flags);
	}

	if (C_CRTSCTS(tty)) {
		zs_rtsdtr(info, RTS, 1);
	}
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct dec_serial * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;

	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	return copy_to_user(retinfo,&tmp,sizeof(*retinfo)) ? -EFAULT : 0;
}

static int set_serial_info(struct dec_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct dec_serial old_info;
	int 			retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial,new_info,sizeof(new_serial));
	old_info = *info;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ZILOG_USR_MASK) !=
		     (info->flags & ~ZILOG_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ZILOG_USR_MASK) |
			       (new_serial.flags & ZILOG_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~ZILOG_FLAGS) |
			(new_serial.flags & ZILOG_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = zs_startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 */
static int get_lsr_info(struct dec_serial * info, unsigned int *value)
{
	unsigned char status;

	spin_lock(&zs_lock);
	status = read_zsreg(info->zs_channel, 0);
	spin_unlock_irq(&zs_lock);
	put_user(status,value);
	return 0;
}

static int rs_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct dec_serial * info = (struct dec_serial *)tty->driver_data;
	unsigned char control, status_a, status_b;
	unsigned int result;

	if (info->hook)
		return -ENODEV;

	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	if (info->zs_channel == info->zs_chan_a)
		result = 0;
	else {
		spin_lock(&zs_lock);
		control = info->zs_chan_a->curregs[5];
		status_a = read_zsreg(info->zs_chan_a, 0);
		status_b = read_zsreg(info->zs_channel, 0);
		spin_unlock_irq(&zs_lock);
		result =  ((control  & RTS) ? TIOCM_RTS: 0)
			| ((control  & DTR) ? TIOCM_DTR: 0)
			| ((status_b & DCD) ? TIOCM_CAR: 0)
			| ((status_a & DCD) ? TIOCM_RNG: 0)
			| ((status_a & SYNC_HUNT) ? TIOCM_DSR: 0)
			| ((status_b & CTS) ? TIOCM_CTS: 0);
	}
	return result;
}

static int rs_tiocmset(struct tty_struct *tty, struct file *file,
                       unsigned int set, unsigned int clear)
{
	struct dec_serial * info = (struct dec_serial *)tty->driver_data;

	if (info->hook)
		return -ENODEV;

	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	if (info->zs_channel == info->zs_chan_a)
		return 0;

	spin_lock(&zs_lock);
	if (set & TIOCM_RTS)
		info->zs_chan_a->curregs[5] |= RTS;
	if (set & TIOCM_DTR)
		info->zs_chan_a->curregs[5] |= DTR;
	if (clear & TIOCM_RTS)
		info->zs_chan_a->curregs[5] &= ~RTS;
	if (clear & TIOCM_DTR)
		info->zs_chan_a->curregs[5] &= ~DTR;
	write_zsreg(info->zs_chan_a, 5, info->zs_chan_a->curregs[5]);
	spin_unlock_irq(&zs_lock);
	return 0;
}

/*
 * rs_break - turn transmit break condition on/off
 */
static void rs_break(struct tty_struct *tty, int break_state)
{
	struct dec_serial *info = (struct dec_serial *) tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_break"))
		return;
	if (!info->port)
		return;

	spin_lock_irqsave(&zs_lock, flags);
	if (break_state == -1)
		info->zs_channel->curregs[5] |= SND_BRK;
	else
		info->zs_channel->curregs[5] &= ~SND_BRK;
	write_zsreg(info->zs_channel, 5, info->zs_channel->curregs[5]);
	spin_unlock_irqrestore(&zs_lock, flags);
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	struct dec_serial * info = (struct dec_serial *)tty->driver_data;

	if (info->hook)
		return -ENODEV;

	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
	case TIOCGSERIAL:
		if (!access_ok(VERIFY_WRITE, (void *)arg,
			       sizeof(struct serial_struct)))
			return -EFAULT;
		return get_serial_info(info, (struct serial_struct *)arg);

	case TIOCSSERIAL:
		return set_serial_info(info, (struct serial_struct *)arg);

	case TIOCSERGETLSR:			/* Get line status register */
		if (!access_ok(VERIFY_WRITE, (void *)arg,
			       sizeof(unsigned int)))
			return -EFAULT;
		return get_lsr_info(info, (unsigned int *)arg);

	case TIOCSERGSTRUCT:
		if (!access_ok(VERIFY_WRITE, (void *)arg,
			       sizeof(struct dec_serial)))
			return -EFAULT;
		copy_from_user((struct dec_serial *)arg, info,
			       sizeof(struct dec_serial));
		return 0;

	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct dec_serial *info = (struct dec_serial *)tty->driver_data;
	int was_stopped;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;
	was_stopped = info->tx_stopped;

	change_speed(info);

	if (was_stopped && !info->tx_stopped)
		rs_start(tty);
}

/*
 * ------------------------------------------------------------
 * rs_close()
 *
 * This routine is called when the serial port gets closed.
 * Wait for the last remaining data to be sent.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct dec_serial * info = (struct dec_serial *)tty->driver_data;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;

	spin_lock_irqsave(&zs_lock, flags);

	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&zs_lock, flags);
		return;
	}

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttyS%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("rs_close: bad serial port count for ttyS%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		spin_unlock_irqrestore(&zs_lock, flags);
		return;
	}
	info->flags |= ZILOG_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ZILOG_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receiver and receive interrupts.
	 */
	info->zs_channel->curregs[3] &= ~RxENABLE;
	write_zsreg(info->zs_channel, 3, info->zs_channel->curregs[3]);
	info->zs_channel->curregs[1] = 0;	/* disable any rx ints */
	write_zsreg(info->zs_channel, 1, info->zs_channel->curregs[1]);
	ZS_CLEARFIFO(info->zs_channel);
	if (info->flags & ZILOG_INITIALIZED) {
		/*
		 * Before we drop DTR, make sure the SCC transmitter
		 * has completely drained.
		 */
		rs_wait_until_sent(tty, info->timeout);
	}

	shutdown(info);
	if (tty->driver->flush_buffer)
		tty->driver->flush_buffer(tty);
	tty_ldisc_flush(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs(info->close_delay));
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ZILOG_NORMAL_ACTIVE|ZILOG_CLOSING);
	wake_up_interruptible(&info->close_wait);
	spin_unlock_irqrestore(&zs_lock, flags);
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct dec_serial *info = (struct dec_serial *) tty->driver_data;
	unsigned long orig_jiffies;
	int char_time;

	if (serial_paranoia_check(info, tty->name, "rs_wait_until_sent"))
		return;

	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 */
	char_time = (info->timeout - HZ/50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout)
		char_time = min(char_time, timeout);
	while ((read_zsreg(info->zs_channel, 1) & Tx_BUF_EMP) == 0) {
		msleep_interruptible(jiffies_to_msecs(char_time));
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, orig_jiffies + timeout))
			break;
	}
	current->state = TASK_RUNNING;
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void rs_hangup(struct tty_struct *tty)
{
	struct dec_serial * info = (struct dec_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_hangup"))
		return;

	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~ZILOG_NORMAL_ACTIVE;
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct dec_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ZILOG_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ZILOG_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		info->flags |= ZILOG_NORMAL_ACTIVE;
		return 0;
	}

	if (tty->termios->c_cflag & CLOCAL)
		do_clocal = 1;

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	spin_lock(&zs_lock);
	if (!tty_hung_up_p(filp))
		info->count--;
	spin_unlock_irq(&zs_lock);
	info->blocked_open++;
	while (1) {
		spin_lock(&zs_lock);
		if (tty->termios->c_cflag & CBAUD)
			zs_rtsdtr(info, RTS | DTR, 1);
		spin_unlock_irq(&zs_lock);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ZILOG_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ZILOG_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ZILOG_CLOSING) &&
		    (do_clocal || (read_zsreg(info->zs_channel, 0) & DCD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyS%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ZILOG_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its ZILOG structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct dec_serial	*info;
	int 			retval, line;

	line = tty->index;
	if ((line < 0) || (line >= zs_channels_found))
		return -ENODEV;
	info = zs_soft + line;

	if (info->hook)
		return -ENODEV;

	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s, count = %d\n", tty->name, info->count);
#endif

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ZILOG_CLOSING)) {
		if (info->flags & ZILOG_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		return ((info->flags & ZILOG_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS);
#else
		return -EAGAIN;
#endif
	}

	/*
	 * Start up serial port
	 */
	retval = zs_startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

#ifdef CONFIG_SERIAL_DEC_CONSOLE
	if (sercons.cflag && sercons.index == line) {
		tty->termios->c_cflag = sercons.cflag;
		sercons.cflag = 0;
		change_speed(info);
	}
#endif

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s successful...", tty->name);
#endif
/* tty->low_latency = 1; */
	return 0;
}

/* Finally, routines used to initialize the serial driver. */

static void __init show_serial_version(void)
{
	printk("DECstation Z8530 serial driver version 0.09\n");
}

/*  Initialize Z8530s zs_channels
 */

static void __init probe_sccs(void)
{
	struct dec_serial **pp;
	int i, n, n_chips = 0, n_channels, chip, channel;
	unsigned long flags;

	/*
	 * did we get here by accident?
	 */
	if(!BUS_PRESENT) {
		printk("Not on JUNKIO machine, skipping probe_sccs\n");
		return;
	}

	switch(mips_machtype) {
#ifdef CONFIG_MACH_DECSTATION
	case MACH_DS5000_2X0:
	case MACH_DS5900:
		n_chips = 2;
		zs_parms = &ds_parms;
		zs_parms->irq0 = dec_interrupt[DEC_IRQ_SCC0];
		zs_parms->irq1 = dec_interrupt[DEC_IRQ_SCC1];
		break;
	case MACH_DS5000_1XX:
		n_chips = 2;
		zs_parms = &ds_parms;
		zs_parms->irq0 = dec_interrupt[DEC_IRQ_SCC0];
		zs_parms->irq1 = dec_interrupt[DEC_IRQ_SCC1];
		break;
	case MACH_DS5000_XX:
		n_chips = 1;
		zs_parms = &ds_parms;
		zs_parms->irq0 = dec_interrupt[DEC_IRQ_SCC0];
		break;
#endif
	default:
		panic("zs: unsupported bus");
	}
	if (!zs_parms)
		panic("zs: uninitialized parms");

	pp = &zs_chain;

	n_channels = 0;

	for (chip = 0; chip < n_chips; chip++) {
		for (channel = 0; channel <= 1; channel++) {
			/*
			 * The sccs reside on the high byte of the 16 bit IOBUS
			 */
			zs_channels[n_channels].control =
				(volatile void *)CKSEG1ADDR(dec_kn_slot_base +
			  (0 == chip ? zs_parms->scc0 : zs_parms->scc1) +
			  (0 == channel ? zs_parms->channel_a_offset :
			                  zs_parms->channel_b_offset));
			zs_channels[n_channels].data =
				zs_channels[n_channels].control + 4;

#ifndef CONFIG_SERIAL_DEC_CONSOLE
			/*
			 * We're called early and memory managment isn't up, yet.
			 * Thus request_region would fail.
			 */
			if (!request_region((unsigned long)
					 zs_channels[n_channels].control,
					 ZS_CHAN_IO_SIZE, "SCC"))
				panic("SCC I/O region is not free");
#endif
			zs_soft[n_channels].zs_channel = &zs_channels[n_channels];
			/* HACK alert! */
			if (!(chip & 1))
				zs_soft[n_channels].irq = zs_parms->irq0;
			else
				zs_soft[n_channels].irq = zs_parms->irq1;

			/*
			 *  Identification of channel A. Location of channel A
                         *  inside chip depends on mapping of internal address
			 *  the chip decodes channels by.
			 *  CHANNEL_A_NR returns either 0 (in case of
			 *  DECstations) or 1 (in case of Baget).
			 */
			if (CHANNEL_A_NR == channel)
				zs_soft[n_channels].zs_chan_a =
				    &zs_channels[n_channels+1-2*CHANNEL_A_NR];
			else
				zs_soft[n_channels].zs_chan_a =
				    &zs_channels[n_channels];

			*pp = &zs_soft[n_channels];
			pp = &zs_soft[n_channels].zs_next;
			n_channels++;
		}
	}

	*pp = 0;
	zs_channels_found = n_channels;

	for (n = 0; n < zs_channels_found; n++) {
		for (i = 0; i < 16; i++) {
			zs_soft[n].zs_channel->curregs[i] = zs_init_regs[i];
		}
	}

	spin_lock_irqsave(&zs_lock, flags);
	for (n = 0; n < zs_channels_found; n++) {
		if (n % 2 == 0) {
			write_zsreg(zs_soft[n].zs_chan_a, R9, FHWRES);
			udelay(10);
			write_zsreg(zs_soft[n].zs_chan_a, R9, 0);
		}
		load_zsregs(zs_soft[n].zs_channel,
			    zs_soft[n].zs_channel->curregs);
	}
	spin_unlock_irqrestore(&zs_lock, flags);
}

static const struct tty_operations serial_ops = {
	.open = rs_open,
	.close = rs_close,
	.write = rs_write,
	.flush_chars = rs_flush_chars,
	.write_room = rs_write_room,
	.chars_in_buffer = rs_chars_in_buffer,
	.flush_buffer = rs_flush_buffer,
	.ioctl = rs_ioctl,
	.throttle = rs_throttle,
	.unthrottle = rs_unthrottle,
	.set_termios = rs_set_termios,
	.stop = rs_stop,
	.start = rs_start,
	.hangup = rs_hangup,
	.break_ctl = rs_break,
	.wait_until_sent = rs_wait_until_sent,
	.tiocmget = rs_tiocmget,
	.tiocmset = rs_tiocmset,
};

/* zs_init inits the driver */
int __init zs_init(void)
{
	int channel, i;
	struct dec_serial *info;

	if(!BUS_PRESENT)
		return -ENODEV;

	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();
	serial_driver = alloc_tty_driver(zs_channels_found);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* Not all of this is exactly right for us. */

	serial_driver->owner = THIS_MODULE;
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty_set_operations(serial_driver, &serial_ops);

	if (tty_register_driver(serial_driver))
		panic("Couldn't register serial driver");

	for (info = zs_chain, i = 0; info; info = info->zs_next, i++) {

		/* Needed before interrupts are enabled. */
		info->tty = 0;
		info->x_char = 0;

		if (info->hook && info->hook->init_info) {
			(*info->hook->init_info)(info);
			continue;
		}

		info->magic = SERIAL_MAGIC;
		info->port = (int) info->zs_channel->control;
		info->line = i;
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		tasklet_init(&info->tlet, do_softint, (unsigned long)info);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		printk("ttyS%02d at 0x%08x (irq = %d) is a Z85C30 SCC\n",
		       info->line, info->port, info->irq);
		tty_register_device(serial_driver, info->line, NULL);

	}

	for (channel = 0; channel < zs_channels_found; ++channel) {
		zs_soft[channel].clk_divisor = 16;
		zs_soft[channel].zs_baud = get_zsbaud(&zs_soft[channel]);

		if (request_irq(zs_soft[channel].irq, rs_interrupt, IRQF_SHARED,
				"scc", &zs_soft[channel]))
			printk(KERN_ERR "decserial: can't get irq %d\n",
			       zs_soft[channel].irq);

		if (zs_soft[channel].hook) {
			zs_startup(&zs_soft[channel]);
			if (zs_soft[channel].hook->init_channel)
				(*zs_soft[channel].hook->init_channel)
					(&zs_soft[channel]);
		}
	}

	return 0;
}

/*
 * polling I/O routines
 */
static int zs_poll_tx_char(void *handle, unsigned char ch)
{
	struct dec_serial *info = handle;
	struct dec_zschannel *chan = info->zs_channel;
	int    ret;

	if(chan) {
		int loops = 10000;

		while (loops && !(read_zsreg(chan, 0) & Tx_BUF_EMP))
			loops--;

		if (loops) {
			write_zsdata(chan, ch);
			ret = 0;
		} else
			ret = -EAGAIN;

		return ret;
	} else
		return -ENODEV;
}

static int zs_poll_rx_char(void *handle)
{
	struct dec_serial *info = handle;
        struct dec_zschannel *chan = info->zs_channel;
        int    ret;

	if(chan) {
                int loops = 10000;

		while (loops && !(read_zsreg(chan, 0) & Rx_CH_AV))
			loops--;

                if (loops)
                        ret = read_zsdata(chan);
                else
                        ret = -EAGAIN;

		return ret;
	} else
		return -ENODEV;
}

int register_zs_hook(unsigned int channel, struct dec_serial_hook *hook)
{
	struct dec_serial *info = &zs_soft[channel];

	if (info->hook) {
		printk("%s: line %d has already a hook registered\n",
		       __FUNCTION__, channel);

		return 0;
	} else {
		hook->poll_rx_char = zs_poll_rx_char;
		hook->poll_tx_char = zs_poll_tx_char;
		info->hook = hook;

		return 1;
	}
}

int unregister_zs_hook(unsigned int channel)
{
	struct dec_serial *info = &zs_soft[channel];

        if (info->hook) {
                info->hook = NULL;
                return 1;
        } else {
                printk("%s: trying to unregister hook on line %d,"
                       " but none is registered\n", __FUNCTION__, channel);
                return 0;
        }
}

/*
 * ------------------------------------------------------------
 * Serial console driver
 * ------------------------------------------------------------
 */
#ifdef CONFIG_SERIAL_DEC_CONSOLE


/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				 unsigned count)
{
	struct dec_serial *info;
	int i;

	info = zs_soft + co->index;

	for (i = 0; i < count; i++, s++) {
		if(*s == '\n')
			zs_poll_tx_char(info, '\r');
		zs_poll_tx_char(info, *s);
	}
}

static struct tty_driver *serial_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}

/*
 *	Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *	Return non-zero if we didn't find a serial port.
 */
static int __init serial_console_setup(struct console *co, char *options)
{
	struct dec_serial *info;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int cflag = CREAD | HUPCL | CLOCAL;
	int clk_divisor = 16;
	int brg;
	char *s;
	unsigned long flags;

	if(!BUS_PRESENT)
		return -ENODEV;

	info = zs_soft + co->index;

	if (zs_chain == 0)
		probe_sccs();

	info->is_cons = 1;

	if (options) {
		baud = simple_strtoul(options, NULL, 10);
		s = options;
		while(*s >= '0' && *s <= '9')
			s++;
		if (*s)
			parity = *s++;
		if (*s)
			bits   = *s - '0';
	}

	/*
	 *	Now construct a cflag setting.
	 */
	switch(baud) {
	case 1200:
		cflag |= B1200;
		break;
	case 2400:
		cflag |= B2400;
		break;
	case 4800:
		cflag |= B4800;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 38400:
		cflag |= B38400;
		break;
	case 57600:
		cflag |= B57600;
		break;
	case 115200:
		cflag |= B115200;
		break;
	case 9600:
	default:
		cflag |= B9600;
		/*
		 * Set this to a sane value to prevent a divide error.
		 */
		baud  = 9600;
		break;
	}
	switch(bits) {
	case 7:
		cflag |= CS7;
		break;
	default:
	case 8:
		cflag |= CS8;
		break;
	}
	switch(parity) {
	case 'o': case 'O':
		cflag |= PARODD;
		break;
	case 'e': case 'E':
		cflag |= PARENB;
		break;
	}
	co->cflag = cflag;

	spin_lock_irqsave(&zs_lock, flags);

	/*
	 * Set up the baud rate generator.
	 */
	brg = BPS_TO_BRG(baud, zs_parms->clock / clk_divisor);
	info->zs_channel->curregs[R12] = (brg & 255);
	info->zs_channel->curregs[R13] = ((brg >> 8) & 255);

	/*
	 * Set byte size and parity.
	 */
	if (bits == 7) {
		info->zs_channel->curregs[R3] |= Rx7;
		info->zs_channel->curregs[R5] |= Tx7;
	} else {
		info->zs_channel->curregs[R3] |= Rx8;
		info->zs_channel->curregs[R5] |= Tx8;
	}
	if (cflag & PARENB) {
		info->zs_channel->curregs[R4] |= PAR_ENA;
	}
	if (!(cflag & PARODD)) {
		info->zs_channel->curregs[R4] |= PAR_EVEN;
	}
	info->zs_channel->curregs[R4] |= SB1;

	/*
	 * Turn on RTS and DTR.
	 */
	zs_rtsdtr(info, RTS | DTR, 1);

	/*
	 * Finally, enable sequencing.
	 */
	info->zs_channel->curregs[R3] |= RxENABLE;
	info->zs_channel->curregs[R5] |= TxENAB;

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->zs_channel, R0, ERR_RES);
	write_zsreg(info->zs_channel, R0, RES_H_IUS);

	/*
	 * Load up the new values.
	 */
	load_zsregs(info->zs_channel, info->zs_channel->curregs);

	/* Save the current value of RR0 */
	info->read_reg_zero = read_zsreg(info->zs_channel, R0);

	zs_soft[co->index].clk_divisor = clk_divisor;
	zs_soft[co->index].zs_baud = get_zsbaud(&zs_soft[co->index]);

	spin_unlock_irqrestore(&zs_lock, flags);

	return 0;
}

static struct console sercons = {
	.name		= "ttyS",
	.write		= serial_console_write,
	.device		= serial_console_device,
	.setup		= serial_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

/*
 *	Register console.
 */
void __init zs_serial_console_init(void)
{
	register_console(&sercons);
}
#endif /* ifdef CONFIG_SERIAL_DEC_CONSOLE */

#ifdef CONFIG_KGDB
struct dec_zschannel *zs_kgdbchan;
static unsigned char scc_inittab[] = {
	9,  0x80,	/* reset A side (CHRA) */
	13, 0,		/* set baud rate divisor */
	12, 1,
	14, 1,		/* baud rate gen enable, src=rtxc (BRENABL) */
	11, 0x50,	/* clocks = br gen (RCBR | TCBR) */
	5,  0x6a,	/* tx 8 bits, assert RTS (Tx8 | TxENAB | RTS) */
	4,  0x44,	/* x16 clock, 1 stop (SB1 | X16CLK)*/
	3,  0xc1,	/* rx enable, 8 bits (RxENABLE | Rx8)*/
};

/* These are for receiving and sending characters under the kgdb
 * source level kernel debugger.
 */
void putDebugChar(char kgdb_char)
{
	struct dec_zschannel *chan = zs_kgdbchan;
	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0)
		RECOVERY_DELAY;
	write_zsdata(chan, kgdb_char);
}
char getDebugChar(void)
{
	struct dec_zschannel *chan = zs_kgdbchan;
	while((read_zsreg(chan, 0) & Rx_CH_AV) == 0)
		eieio(); /*barrier();*/
	return read_zsdata(chan);
}
void kgdb_interruptible(int yes)
{
	struct dec_zschannel *chan = zs_kgdbchan;
	int one, nine;
	nine = read_zsreg(chan, 9);
	if (yes == 1) {
		one = EXT_INT_ENAB|RxINT_ALL;
		nine |= MIE;
		printk("turning serial ints on\n");
	} else {
		one = RxINT_DISAB;
		nine &= ~MIE;
		printk("turning serial ints off\n");
	}
	write_zsreg(chan, 1, one);
	write_zsreg(chan, 9, nine);
}

static int kgdbhook_init_channel(void *handle)
{
	return 0;
}

static void kgdbhook_init_info(void *handle)
{
}

static void kgdbhook_rx_char(void *handle, unsigned char ch, unsigned char fl)
{
	struct dec_serial *info = handle;

	if (fl != TTY_NORMAL)
		return;
	if (ch == 0x03 || ch == '$')
		breakpoint();
}

/* This sets up the serial port we're using, and turns on
 * interrupts for that channel, so kgdb is usable once we're done.
 */
static inline void kgdb_chaninit(struct dec_zschannel *ms, int intson, int bps)
{
	int brg;
	int i, x;
	volatile char *sccc = ms->control;
	brg = BPS_TO_BRG(bps, zs_parms->clock/16);
	printk("setting bps on kgdb line to %d [brg=%x]\n", bps, brg);
	for (i = 20000; i != 0; --i) {
		x = *sccc; eieio();
	}
	for (i = 0; i < sizeof(scc_inittab); ++i) {
		write_zsreg(ms, scc_inittab[i], scc_inittab[i+1]);
		i++;
	}
}
/* This is called at boot time to prime the kgdb serial debugging
 * serial line.  The 'tty_num' argument is 0 for /dev/ttya and 1
 * for /dev/ttyb which is determined in setup_arch() from the
 * boot command line flags.
 */
struct dec_serial_hook zs_kgdbhook = {
	.init_channel	= kgdbhook_init_channel,
	.init_info	= kgdbhook_init_info,
	.rx_char	= kgdbhook_rx_char,
	.cflags		= B38400 | CS8 | CLOCAL,
}

void __init zs_kgdb_hook(int tty_num)
{
	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();
	zs_soft[tty_num].zs_channel = &zs_channels[tty_num];
	zs_kgdbchan = zs_soft[tty_num].zs_channel;
	zs_soft[tty_num].change_needed = 0;
	zs_soft[tty_num].clk_divisor = 16;
	zs_soft[tty_num].zs_baud = 38400;
 	zs_soft[tty_num].hook = &zs_kgdbhook; /* This runs kgdb */
	/* Turn on transmitter/receiver at 8-bits/char */
        kgdb_chaninit(zs_soft[tty_num].zs_channel, 1, 38400);
	printk("KGDB: on channel %d initialized\n", tty_num);
	set_debug_traps(); /* init stub */
}
#endif /* ifdef CONFIG_KGDB */
