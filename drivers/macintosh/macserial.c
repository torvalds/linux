/*
 * macserial.c: Serial port driver for Power Macintoshes.
 *
 * Derived from drivers/sbus/char/sunserial.c by Paul Mackerras.
 *
 * Copyright (C) 1996 Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *
 * Receive DMA code by Takashi Oe <toe@unlserve.unl.edu>.
 *
 * $Id: macserial.c,v 1.24.2.4 1999/10/19 04:36:42 paulus Exp $
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#ifdef CONFIG_SERIAL_CONSOLE
#include <linux/console.h>
#endif
#include <linux/slab.h>
#include <linux/bitops.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/machdep.h>
#include <asm/pmac_feature.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#ifdef CONFIG_KGDB
#include <asm/kgdb.h>
#endif
#include <asm/dbdma.h>

#include "macserial.h"

#ifdef CONFIG_PMAC_PBOOK
static int serial_notify_sleep(struct pmu_sleep_notifier *self, int when);
static struct pmu_sleep_notifier serial_sleep_notifier = {
	serial_notify_sleep,
	SLEEP_LEVEL_MISC,
};
#endif

#define SUPPORT_SERIAL_DMA
#define MACSERIAL_VERSION	"2.0"

/*
 * It would be nice to dynamically allocate everything that
 * depends on NUM_SERIAL, so we could support any number of
 * Z8530s, but for now...
 */
#define NUM_SERIAL	2		/* Max number of ZS chips supported */
#define NUM_CHANNELS	(NUM_SERIAL * 2)	/* 2 channels per chip */

/* On PowerMacs, the hardware takes care of the SCC recovery time,
   but we need the eieio to make sure that the accesses occur
   in the order we want. */
#define RECOVERY_DELAY	eieio()

static struct tty_driver *serial_driver;

struct mac_zschannel zs_channels[NUM_CHANNELS];

struct mac_serial zs_soft[NUM_CHANNELS];
int zs_channels_found;
struct mac_serial *zs_chain;	/* list of all channels */

struct tty_struct zs_ttys[NUM_CHANNELS];

static int is_powerbook;

#ifdef CONFIG_SERIAL_CONSOLE
static struct console sercons;
#endif

#ifdef CONFIG_KGDB
struct mac_zschannel *zs_kgdbchan;
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
#endif
#define ZS_CLOCK         3686400 	/* Z8530 RTxC input clock rate */

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/*
 * Debugging.
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW
#undef SERIAL_DEBUG_POWER
#undef SERIAL_DEBUG_THROTTLE
#undef SERIAL_DEBUG_STOP
#undef SERIAL_DEBUG_BAUDS

#define RS_STROBE_TIME 10
#define RS_ISR_PASS_LIMIT 256

#define _INLINE_ inline

#ifdef SERIAL_DEBUG_OPEN
#define OPNDBG(fmt, arg...)	printk(KERN_DEBUG fmt , ## arg)
#else
#define OPNDBG(fmt, arg...)	do { } while (0)
#endif
#ifdef SERIAL_DEBUG_POWER
#define PWRDBG(fmt, arg...)	printk(KERN_DEBUG fmt , ## arg)
#else
#define PWRDBG(fmt, arg...)	do { } while (0)
#endif
#ifdef SERIAL_DEBUG_BAUDS
#define BAUDBG(fmt, arg...)	printk(fmt , ## arg)
#else
#define BAUDBG(fmt, arg...)	do { } while (0)
#endif

static void probe_sccs(void);
static void change_speed(struct mac_serial *info, struct termios *old);
static void rs_wait_until_sent(struct tty_struct *tty, int timeout);
static int set_scc_power(struct mac_serial * info, int state);
static int setup_scc(struct mac_serial * info);
static void dbdma_reset(volatile struct dbdma_regs *dma);
static void dbdma_flush(volatile struct dbdma_regs *dma);
static irqreturn_t rs_txdma_irq(int irq, void *dev_id, struct pt_regs *regs);
static irqreturn_t rs_rxdma_irq(int irq, void *dev_id, struct pt_regs *regs);
static void dma_init(struct mac_serial * info);
static void rxdma_start(struct mac_serial * info, int curr);
static void rxdma_to_tty(struct mac_serial * info);

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the copy_from_user blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char *tmp_buf;
static DECLARE_MUTEX(tmp_buf_sem);


static inline int __pmac
serial_paranoia_check(struct mac_serial *info,
		      char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char badmagic[] = KERN_WARNING
		"Warning: bad magic number for serial struct %s in %s\n";
	static const char badinfo[] = KERN_WARNING
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
 * Reading and writing Z8530 registers.
 */
static inline unsigned char __pmac read_zsreg(struct mac_zschannel *channel,
					      unsigned char reg)
{
	unsigned char retval;
	unsigned long flags;

	/*
	 * We have to make this atomic.
	 */
	spin_lock_irqsave(&channel->lock, flags);
	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	retval = *channel->control;
	RECOVERY_DELAY;
	spin_unlock_irqrestore(&channel->lock, flags);
	return retval;
}

static inline void __pmac write_zsreg(struct mac_zschannel *channel,
				      unsigned char reg, unsigned char value)
{
	unsigned long flags;

	spin_lock_irqsave(&channel->lock, flags);
	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	*channel->control = value;
	RECOVERY_DELAY;
	spin_unlock_irqrestore(&channel->lock, flags);
	return;
}

static inline unsigned char __pmac read_zsdata(struct mac_zschannel *channel)
{
	unsigned char retval;

	retval = *channel->data;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsdata(struct mac_zschannel *channel,
				unsigned char value)
{
	*channel->data = value;
	RECOVERY_DELAY;
	return;
}

static inline void load_zsregs(struct mac_zschannel *channel,
			       unsigned char *regs)
{
	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel);
	/* Load 'em up */
	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R3, regs[R3] & ~RxENABLE);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R9, regs[R9]);
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
static inline void zs_rtsdtr(struct mac_serial *ss, int set)
{
	if (set)
		ss->curregs[5] |= (RTS | DTR);
	else
		ss->curregs[5] &= ~(RTS | DTR);
	write_zsreg(ss->zs_channel, 5, ss->curregs[5]);
	return;
}

/* Utility routines for the Zilog */
static inline int get_zsbaud(struct mac_serial *ss)
{
	struct mac_zschannel *channel = ss->zs_channel;
	int brg;

	if ((ss->curregs[R11] & TCBR) == 0) {
		/* higher rates don't use the baud rate generator */
		return (ss->curregs[R4] & X32CLK)? ZS_CLOCK/32: ZS_CLOCK/16;
	}
	/* The baud rate is split up between two 8-bit registers in
	 * what is termed 'BRG time constant' format in my docs for
	 * the chip, it is a function of the clk rate the chip is
	 * receiving which happens to be constant.
	 */
	brg = (read_zsreg(channel, 13) << 8);
	brg |= read_zsreg(channel, 12);
	return BRG_TO_BPS(brg, (ZS_CLOCK/(ss->clk_divisor)));
}

/* On receive, this clears errors and the receiver interrupts */
static inline void rs_recv_clear(struct mac_zschannel *zsc)
{
	write_zsreg(zsc, 0, ERR_RES);
	write_zsreg(zsc, 0, RES_H_IUS); /* XXX this is unnecessary */
}

/*
 * Reset a Descriptor-Based DMA channel.
 */
static void dbdma_reset(volatile struct dbdma_regs *dma)
{
	int i;

	out_le32(&dma->control, (WAKE|FLUSH|PAUSE|RUN) << 16);

	/*
	 * Yes this looks peculiar, but apparently it needs to be this
	 * way on some machines.  (We need to make sure the DBDMA
	 * engine has actually got the write above and responded
	 * to it. - paulus)
	 */
	for (i = 200; i > 0; --i)
		if (ld_le32(&dma->status) & RUN)
			udelay(1);
}

/*
 * Tells a DBDMA channel to stop and write any buffered data
 * it might have to memory.
 */
static _INLINE_ void dbdma_flush(volatile struct dbdma_regs *dma)
{
	int i = 0;

	out_le32(&dma->control, (FLUSH << 16) | FLUSH);
	while (((in_le32(&dma->status) & FLUSH) != 0) && (i++ < 100))
		udelay(1);
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
static _INLINE_ void rs_sched_event(struct mac_serial *info,
				  int event)
{
	info->event |= 1 << event;
	schedule_work(&info->tqueue);
}

/* Work out the flag value for a z8530 status value. */
static _INLINE_ int stat_to_flag(int stat)
{
	int flag;

	if (stat & Rx_OVR) {
		flag = TTY_OVERRUN;
	} else if (stat & FRM_ERR) {
		flag = TTY_FRAME;
	} else if (stat & PAR_ERR) {
		flag = TTY_PARITY;
	} else
		flag = 0;
	return flag;
}

static _INLINE_ void receive_chars(struct mac_serial *info,
				   struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat, flag;

	while ((read_zsreg(info->zs_channel, 0) & Rx_CH_AV) != 0) {

		stat = read_zsreg(info->zs_channel, R1);
		ch = read_zsdata(info->zs_channel);

#ifdef CONFIG_KGDB
		if (info->kgdb_channel) {
			if (ch == 0x03 || ch == '$')
				breakpoint();
			if (stat & (Rx_OVR|FRM_ERR|PAR_ERR))
				write_zsreg(info->zs_channel, 0, ERR_RES);
			return;
		}
#endif
		if (!tty)
			continue;
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			tty_flip_buffer_push(tty);

		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			static int flip_buf_ovf;
			if (++flip_buf_ovf <= 1)
				printk(KERN_WARNING "FB. overflow: %d\n",
						    flip_buf_ovf);
			break;
		}
		tty->flip.count++;
		{
			static int flip_max_cnt;
			if (flip_max_cnt < tty->flip.count)
				flip_max_cnt = tty->flip.count;
		}
		flag = stat_to_flag(stat);
		if (flag)
			/* reset the error indication */
			write_zsreg(info->zs_channel, 0, ERR_RES);
		*tty->flip.flag_buf_ptr++ = flag;
		*tty->flip.char_buf_ptr++ = ch;
	}
	if (tty)
		tty_flip_buffer_push(tty);
}

static void transmit_chars(struct mac_serial *info)
{
	if ((read_zsreg(info->zs_channel, 0) & Tx_BUF_EMP) == 0)
		return;
	info->tx_active = 0;

	if (info->x_char && !info->power_wait) {
		/* Send next char */
		write_zsdata(info->zs_channel, info->x_char);
		info->x_char = 0;
		info->tx_active = 1;
		return;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped || info->tx_stopped
	    || info->power_wait) {
		write_zsreg(info->zs_channel, 0, RES_Tx_P);
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

static void powerup_done(unsigned long data)
{
	struct mac_serial *info = (struct mac_serial *) data;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	info->power_wait = 0;
	transmit_chars(info);
	spin_unlock_irqrestore(&info->lock, flags);
}

static _INLINE_ void status_handle(struct mac_serial *info)
{
	unsigned char status;

	/* Get status from Read Register 0 */
	status = read_zsreg(info->zs_channel, 0);

	/* Check for DCD transitions */
	if (((status ^ info->read_reg_zero) & DCD) != 0
	    && info->tty && !C_CLOCAL(info->tty)) {
		if (status & DCD) {
			wake_up_interruptible(&info->open_wait);
		} else {
			if (info->tty)
				tty_hangup(info->tty);
		}
	}

	/* Check for CTS transitions */
	if (info->tty && C_CRTSCTS(info->tty)) {
		/*
		 * For some reason, on the Power Macintosh,
		 * it seems that the CTS bit is 1 when CTS is
		 * *negated* and 0 when it is asserted.
		 * The DCD bit doesn't seem to be inverted
		 * like this.
		 */
		if ((status & CTS) == 0) {
			if (info->tx_stopped) {
#ifdef SERIAL_DEBUG_FLOW
				printk(KERN_DEBUG "CTS up\n");
#endif
				info->tx_stopped = 0;
				if (!info->tx_active)
					transmit_chars(info);
			}
		} else {
#ifdef SERIAL_DEBUG_FLOW
			printk(KERN_DEBUG "CTS down\n");
#endif
			info->tx_stopped = 1;
		}
	}

	/* Clear status condition... */
	write_zsreg(info->zs_channel, 0, RES_EXT_INT);
	info->read_reg_zero = status;
}

static _INLINE_ void receive_special_dma(struct mac_serial *info)
{
	unsigned char stat, flag;
	volatile struct dbdma_regs *rd = &info->rx->dma;
	int where = RX_BUF_SIZE;

	spin_lock(&info->rx_dma_lock);
	if ((ld_le32(&rd->status) & ACTIVE) != 0)
		dbdma_flush(rd);
	if (in_le32(&rd->cmdptr)
	    == virt_to_bus(info->rx_cmds[info->rx_cbuf] + 1))
		where -= in_le16(&info->rx->res_count);
	where--;

	stat = read_zsreg(info->zs_channel, R1);

	flag = stat_to_flag(stat);
	if (flag) {
		info->rx_flag_buf[info->rx_cbuf][where] = flag;
		/* reset the error indication */
		write_zsreg(info->zs_channel, 0, ERR_RES);
	}

	spin_unlock(&info->rx_dma_lock);
}

/*
 * This is the serial driver's generic interrupt routine
 */
static irqreturn_t rs_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct mac_serial *info = (struct mac_serial *) dev_id;
	unsigned char zs_intreg;
	int shift;
	unsigned long flags;
	int handled = 0;

	if (!(info->flags & ZILOG_INITIALIZED)) {
		printk(KERN_WARNING "rs_interrupt: irq %d, port not "
				    "initialized\n", irq);
		disable_irq(irq);
		return IRQ_NONE;
	}

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

	spin_lock_irqsave(&info->lock, flags);
	for (;;) {
		zs_intreg = read_zsreg(info->zs_chan_a, 3) >> shift;
#ifdef SERIAL_DEBUG_INTR
		printk(KERN_DEBUG "rs_interrupt: irq %d, zs_intreg 0x%x\n",
		       irq, (int)zs_intreg);
#endif

		if ((zs_intreg & CHAN_IRQMASK) == 0)
			break;
		handled = 1;

		if (zs_intreg & CHBRxIP) {
			/* If we are doing DMA, we only ask for interrupts
			   on characters with errors or special conditions. */
			if (info->dma_initted)
				receive_special_dma(info);
			else
				receive_chars(info, regs);
		}
		if (zs_intreg & CHBTxIP)
			transmit_chars(info);
		if (zs_intreg & CHBEXT)
			status_handle(info);
	}
	spin_unlock_irqrestore(&info->lock, flags);
	return IRQ_RETVAL(handled);
}

/* Transmit DMA interrupt - not used at present */
static irqreturn_t rs_txdma_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	return IRQ_HANDLED;
}

/*
 * Receive DMA interrupt.
 */
static irqreturn_t rs_rxdma_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mac_serial *info = (struct mac_serial *) dev_id;
	volatile struct dbdma_cmd *cd;

	if (!info->dma_initted)
		return IRQ_NONE;
	spin_lock(&info->rx_dma_lock);
	/* First, confirm that this interrupt is, indeed, coming */
	/* from Rx DMA */
	cd = info->rx_cmds[info->rx_cbuf] + 2;
	if ((in_le16(&cd->xfer_status) & (RUN | ACTIVE)) != (RUN | ACTIVE)) {
		spin_unlock(&info->rx_dma_lock);
		return IRQ_NONE;
	}
	if (info->rx_fbuf != RX_NO_FBUF) {
		info->rx_cbuf = info->rx_fbuf;
		if (++info->rx_fbuf == info->rx_nbuf)
			info->rx_fbuf = 0;
		if (info->rx_fbuf == info->rx_ubuf)
			info->rx_fbuf = RX_NO_FBUF;
	}
	spin_unlock(&info->rx_dma_lock);
	return IRQ_HANDLED;
}

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
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;

#ifdef SERIAL_DEBUG_STOP
	printk(KERN_DEBUG "rs_stop %ld....\n",
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;

#if 0
	spin_lock_irqsave(&info->lock, flags);
	if (info->curregs[5] & TxENAB) {
		info->curregs[5] &= ~TxENAB;
		info->pendregs[5] &= ~TxENAB;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
	spin_unlock_irqrestore(&info->lock, flags);
#endif
}

static void rs_start(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

#ifdef SERIAL_DEBUG_STOP
	printk(KERN_DEBUG "rs_start %ld....\n", 
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_start"))
		return;

	spin_lock_irqsave(&info->lock, flags);
#if 0
	if (info->xmit_cnt && info->xmit_buf && !(info->curregs[5] & TxENAB)) {
		info->curregs[5] |= TxENAB;
		info->pendregs[5] = info->curregs[5];
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
#else
	if (info->xmit_cnt && info->xmit_buf && !info->tx_active) {
		transmit_chars(info);
	}
#endif
	spin_unlock_irqrestore(&info->lock, flags);
}

static void do_softint(void *private_)
{
	struct mac_serial	*info = (struct mac_serial *) private_;
	struct tty_struct	*tty;

	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event))
		tty_wakeup(tty);
}

static int startup(struct mac_serial * info)
{
	int delay;

	OPNDBG("startup() (ttyS%d, irq %d)\n", info->line, info->irq);
 
	if (info->flags & ZILOG_INITIALIZED) {
		OPNDBG(" -> already inited\n");
 		return 0;
	}

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) get_zeroed_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	OPNDBG("starting up ttyS%d (irq %d)...\n", info->line, info->irq);

	delay = set_scc_power(info, 1);

	setup_scc(info);

	if (delay) {
		unsigned long flags;

		/* delay is in ms */
		spin_lock_irqsave(&info->lock, flags);
		info->power_wait = 1;
		mod_timer(&info->powerup_timer,
			  jiffies + (delay * HZ + 999) / 1000);
		spin_unlock_irqrestore(&info->lock, flags);
	}

	OPNDBG("enabling IRQ on ttyS%d (irq %d)...\n", info->line, info->irq);

	info->flags |= ZILOG_INITIALIZED;
	enable_irq(info->irq);
	if (info->dma_initted) {
		enable_irq(info->rx_dma_irq);
	}

	return 0;
}

static _INLINE_ void rxdma_start(struct mac_serial * info, int curr)
{
	volatile struct dbdma_regs *rd = &info->rx->dma;
	volatile struct dbdma_cmd *cd = info->rx_cmds[curr];

//printk(KERN_DEBUG "SCC: rxdma_start\n");

	st_le32(&rd->cmdptr, virt_to_bus(cd));
	out_le32(&rd->control, (RUN << 16) | RUN);
}

static void rxdma_to_tty(struct mac_serial *info)
{
	struct tty_struct	*tty = info->tty;
	volatile struct dbdma_regs *rd = &info->rx->dma;
	unsigned long flags;
	int residue, available, space, do_queue;

	if (!tty)
		return;

	do_queue = 0;
	spin_lock_irqsave(&info->rx_dma_lock, flags);
more:
	space = TTY_FLIPBUF_SIZE - tty->flip.count;
	if (!space) {
		do_queue++;
		goto out;
	}
	residue = 0;
	if (info->rx_ubuf == info->rx_cbuf) {
		if ((ld_le32(&rd->status) & ACTIVE) != 0) {
			dbdma_flush(rd);
			if (in_le32(&rd->cmdptr)
			    == virt_to_bus(info->rx_cmds[info->rx_cbuf]+1))
				residue = in_le16(&info->rx->res_count);
		}
	}
	available = RX_BUF_SIZE - residue - info->rx_done_bytes;
	if (available > space)
		available = space;
	if (available) {
		memcpy(tty->flip.char_buf_ptr,
		       info->rx_char_buf[info->rx_ubuf] + info->rx_done_bytes,
		       available);
		memcpy(tty->flip.flag_buf_ptr,
		       info->rx_flag_buf[info->rx_ubuf] + info->rx_done_bytes,
		       available);
		tty->flip.char_buf_ptr += available;
		tty->flip.count += available;
		tty->flip.flag_buf_ptr += available;
		memset(info->rx_flag_buf[info->rx_ubuf] + info->rx_done_bytes,
		       0, available);
		info->rx_done_bytes += available;
		do_queue++;
	}
	if (info->rx_done_bytes == RX_BUF_SIZE) {
		volatile struct dbdma_cmd *cd = info->rx_cmds[info->rx_ubuf];

		if (info->rx_ubuf == info->rx_cbuf)
			goto out;
		/* mark rx_char_buf[rx_ubuf] free */
		st_le16(&cd->command, DBDMA_NOP);
		cd++;
		st_le32(&cd->cmd_dep, 0);
		st_le32((unsigned int *)&cd->res_count, 0);
		cd++;
		st_le16(&cd->xfer_status, 0);

		if (info->rx_fbuf == RX_NO_FBUF) {
			info->rx_fbuf = info->rx_ubuf;
			if (!(ld_le32(&rd->status) & ACTIVE)) {
				dbdma_reset(&info->rx->dma);
				rxdma_start(info, info->rx_ubuf);
				info->rx_cbuf = info->rx_ubuf;
			}
		}
		info->rx_done_bytes = 0;
		if (++info->rx_ubuf == info->rx_nbuf)
			info->rx_ubuf = 0;
		if (info->rx_fbuf == info->rx_ubuf)
			info->rx_fbuf = RX_NO_FBUF;
		goto more;
	}
out:
	spin_unlock_irqrestore(&info->rx_dma_lock, flags);
	if (do_queue)
		tty_flip_buffer_push(tty);
}

static void poll_rxdma(unsigned long private_)
{
	struct mac_serial	*info = (struct mac_serial *) private_;
	unsigned long flags;

	rxdma_to_tty(info);
	spin_lock_irqsave(&info->rx_dma_lock, flags);
	mod_timer(&info->poll_dma_timer, RX_DMA_TIMER);
	spin_unlock_irqrestore(&info->rx_dma_lock, flags);
}

static void dma_init(struct mac_serial * info)
{
	int i, size;
	volatile struct dbdma_cmd *cd;
	unsigned char *p;

	info->rx_nbuf = 8;

	/* various mem set up */
	size = sizeof(struct dbdma_cmd) * (3 * info->rx_nbuf + 2)
		+ (RX_BUF_SIZE * 2 + sizeof(*info->rx_cmds)
		   + sizeof(*info->rx_char_buf) + sizeof(*info->rx_flag_buf))
		* info->rx_nbuf;
	info->dma_priv = kmalloc(size, GFP_KERNEL | GFP_DMA);
	if (info->dma_priv == NULL)
		return;
	memset(info->dma_priv, 0, size);

	info->rx_cmds = (volatile struct dbdma_cmd **)info->dma_priv;
	info->rx_char_buf = (unsigned char **) (info->rx_cmds + info->rx_nbuf);
	info->rx_flag_buf = info->rx_char_buf + info->rx_nbuf;
	p = (unsigned char *) (info->rx_flag_buf + info->rx_nbuf);
	for (i = 0; i < info->rx_nbuf; i++, p += RX_BUF_SIZE)
		info->rx_char_buf[i] = p;
	for (i = 0; i < info->rx_nbuf; i++, p += RX_BUF_SIZE)
		info->rx_flag_buf[i] = p;

	/* a bit of DMA programming */
	cd = info->rx_cmds[0] = (volatile struct dbdma_cmd *) DBDMA_ALIGN(p);
	st_le16(&cd->command, DBDMA_NOP);
	cd++;
	st_le16(&cd->req_count, RX_BUF_SIZE);
	st_le16(&cd->command, INPUT_MORE);
	st_le32(&cd->phy_addr, virt_to_bus(info->rx_char_buf[0]));
	cd++;
	st_le16(&cd->req_count, 4);
	st_le16(&cd->command, STORE_WORD | INTR_ALWAYS);
	st_le32(&cd->phy_addr, virt_to_bus(cd-2));
	st_le32(&cd->cmd_dep, DBDMA_STOP);
	for (i = 1; i < info->rx_nbuf; i++) {
		info->rx_cmds[i] = ++cd;
		st_le16(&cd->command, DBDMA_NOP);
		cd++;
		st_le16(&cd->req_count, RX_BUF_SIZE);
		st_le16(&cd->command, INPUT_MORE);
		st_le32(&cd->phy_addr, virt_to_bus(info->rx_char_buf[i]));
		cd++;
		st_le16(&cd->req_count, 4);
		st_le16(&cd->command, STORE_WORD | INTR_ALWAYS);
		st_le32(&cd->phy_addr, virt_to_bus(cd-2));
		st_le32(&cd->cmd_dep, DBDMA_STOP);
	}
	cd++;
	st_le16(&cd->command, DBDMA_NOP | BR_ALWAYS);
	st_le32(&cd->cmd_dep, virt_to_bus(info->rx_cmds[0]));

	/* setup DMA to our liking */
	dbdma_reset(&info->rx->dma);
	st_le32(&info->rx->dma.intr_sel, 0x10001);
	st_le32(&info->rx->dma.br_sel, 0x10001);
	out_le32(&info->rx->dma.wait_sel, 0x10001);

	/* set various flags */
	info->rx_ubuf = 0;
	info->rx_cbuf = 0;
	info->rx_fbuf = info->rx_ubuf + 1;
	if (info->rx_fbuf == info->rx_nbuf)
		info->rx_fbuf = RX_NO_FBUF;
	info->rx_done_bytes = 0;

	/* setup polling */
	init_timer(&info->poll_dma_timer);
	info->poll_dma_timer.function = (void *)&poll_rxdma;
	info->poll_dma_timer.data = (unsigned long)info;

	info->dma_initted = 1;
}

/*
 * FixZeroBug....Works around a bug in the SCC receving channel.
 * Taken from Darwin code, 15 Sept. 2000  -DanM
 *
 * The following sequence prevents a problem that is seen with O'Hare ASICs
 * (most versions -- also with some Heathrow and Hydra ASICs) where a zero
 * at the input to the receiver becomes 'stuck' and locks up the receiver.
 * This problem can occur as a result of a zero bit at the receiver input
 * coincident with any of the following events:
 *
 *	The SCC is initialized (hardware or software).
 *	A framing error is detected.
 *	The clocking option changes from synchronous or X1 asynchronous
 *		clocking to X16, X32, or X64 asynchronous clocking.
 *	The decoding mode is changed among NRZ, NRZI, FM0, or FM1.
 *
 * This workaround attempts to recover from the lockup condition by placing
 * the SCC in synchronous loopback mode with a fast clock before programming
 * any of the asynchronous modes.
 */
static void fix_zero_bug_scc(struct mac_serial * info)
{
	write_zsreg(info->zs_channel, 9,
		    (info->zs_channel == info->zs_chan_a? CHRA: CHRB));
	udelay(10);
	write_zsreg(info->zs_channel, 9,
		    ((info->zs_channel == info->zs_chan_a? CHRA: CHRB) | NV));

	write_zsreg(info->zs_channel, 4, (X1CLK | EXTSYNC));

	/* I think this is wrong....but, I just copying code....
	*/
	write_zsreg(info->zs_channel, 3, (8 & ~RxENABLE));

	write_zsreg(info->zs_channel, 5, (8 & ~TxENAB));
	write_zsreg(info->zs_channel, 9, NV);	/* Didn't we already do this? */
	write_zsreg(info->zs_channel, 11, (RCBR | TCBR));
	write_zsreg(info->zs_channel, 12, 0);
	write_zsreg(info->zs_channel, 13, 0);
	write_zsreg(info->zs_channel, 14, (LOOPBAK | SSBR));
	write_zsreg(info->zs_channel, 14, (LOOPBAK | SSBR | BRENABL));
	write_zsreg(info->zs_channel, 3, (8 | RxENABLE));
	write_zsreg(info->zs_channel, 0, RES_EXT_INT);
	write_zsreg(info->zs_channel, 0, RES_EXT_INT);	/* to kill some time */

	/* The channel should be OK now, but it is probably receiving
	 * loopback garbage.
	 * Switch to asynchronous mode, disable the receiver,
	 * and discard everything in the receive buffer.
	 */
	write_zsreg(info->zs_channel, 9, NV);
	write_zsreg(info->zs_channel, 4, PAR_ENA);
	write_zsreg(info->zs_channel, 3, (8 & ~RxENABLE));

	while (read_zsreg(info->zs_channel, 0) & Rx_CH_AV) {
		(void)read_zsreg(info->zs_channel, 8);
		write_zsreg(info->zs_channel, 0, RES_EXT_INT);
		write_zsreg(info->zs_channel, 0, ERR_RES);
	}
}

static int setup_scc(struct mac_serial * info)
{
	unsigned long flags;

	OPNDBG("setting up ttyS%d SCC...\n", info->line);

	spin_lock_irqsave(&info->lock, flags);

	/* Nice buggy HW ... */
	fix_zero_bug_scc(info);

	/*
	 * Reset the chip.
	 */
	write_zsreg(info->zs_channel, 9,
		    (info->zs_channel == info->zs_chan_a? CHRA: CHRB));
	udelay(10);
	write_zsreg(info->zs_channel, 9, 0);

	/*
	 * Clear the receive FIFO.
	 */
	ZS_CLEARFIFO(info->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Reset DMAs
	 */
	if (info->has_dma)
		dma_init(info);

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->zs_channel, 0, ERR_RES);
	write_zsreg(info->zs_channel, 0, RES_H_IUS);

	/*
	 * Turn on RTS and DTR.
	 */
	if (!info->is_irda)
		zs_rtsdtr(info, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	if (!info->dma_initted) {
		/* interrupt on ext/status changes, all received chars,
		   transmit ready */
		info->curregs[1] = (info->curregs[1] & ~0x18)
				| (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB);
	} else {
		/* interrupt on ext/status changes, W/Req pin is
		   receive DMA request */
		info->curregs[1] = (info->curregs[1] & ~(0x18 | TxINT_ENAB))
				| (EXT_INT_ENAB | WT_RDY_RT | WT_FN_RDYFN);
		write_zsreg(info->zs_channel, 1, info->curregs[1]);
		/* enable W/Req pin */
		info->curregs[1] |= WT_RDY_ENAB;
		write_zsreg(info->zs_channel, 1, info->curregs[1]);
		/* enable interrupts on transmit ready and receive errors */
		info->curregs[1] |= INT_ERR_Rx | TxINT_ENAB;
	}
	info->pendregs[1] = info->curregs[1];
	info->curregs[3] |= (RxENABLE | Rx8);
	info->pendregs[3] = info->curregs[3];
	info->curregs[5] |= (TxENAB | Tx8);
	info->pendregs[5] = info->curregs[5];
	info->curregs[9] |= (NV | MIE);
	info->pendregs[9] = info->curregs[9];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	write_zsreg(info->zs_channel, 9, info->curregs[9]);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	spin_unlock_irqrestore(&info->lock, flags);

	/*
	 * Set the speed of the serial port
	 */
	change_speed(info, 0);

	/* Save the current value of RR0 */
	info->read_reg_zero = read_zsreg(info->zs_channel, 0);

	if (info->dma_initted) {
		spin_lock_irqsave(&info->rx_dma_lock, flags);
		rxdma_start(info, 0);
		info->poll_dma_timer.expires = RX_DMA_TIMER;
		add_timer(&info->poll_dma_timer);
		spin_unlock_irqrestore(&info->rx_dma_lock, flags);
	}

	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct mac_serial * info)
{
	OPNDBG("Shutting down serial port %d (irq %d)....\n", info->line,
	       info->irq);

	if (!(info->flags & ZILOG_INITIALIZED)) {
		OPNDBG("(already shutdown)\n");
		return;
	}

	if (info->has_dma) {
		del_timer(&info->poll_dma_timer);
		dbdma_reset(info->tx_dma);
		dbdma_reset(&info->rx->dma);
		disable_irq(info->tx_dma_irq);
		disable_irq(info->rx_dma_irq);
	}
	disable_irq(info->irq);

	info->pendregs[1] = info->curregs[1] = 0;
	write_zsreg(info->zs_channel, 1, 0);	/* no interrupts */

	info->curregs[3] &= ~RxENABLE;
	info->pendregs[3] = info->curregs[3];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);

	info->curregs[5] &= ~TxENAB;
	if (!info->tty || C_HUPCL(info->tty))
		info->curregs[5] &= ~DTR;
	info->pendregs[5] = info->curregs[5];
	write_zsreg(info->zs_channel, 5, info->curregs[5]);

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	set_scc_power(info, 0);

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->has_dma && info->dma_priv) {
		kfree(info->dma_priv);
		info->dma_priv = NULL;
		info->dma_initted = 0;
	}

	memset(info->curregs, 0, sizeof(info->curregs));
	memset(info->pendregs, 0, sizeof(info->pendregs));

	info->flags &= ~ZILOG_INITIALIZED;
}

/*
 * Turn power on or off to the SCC and associated stuff
 * (port drivers, modem, IR port, etc.)
 * Returns the number of milliseconds we should wait before
 * trying to use the port.
 */
static int set_scc_power(struct mac_serial * info, int state)
{
	int delay = 0;

	if (state) {
		PWRDBG("ttyS%d: powering up hardware\n", info->line);
		pmac_call_feature(
			PMAC_FTR_SCC_ENABLE,
			info->dev_node, info->port_type, 1);
		if (info->is_internal_modem) {
			pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE,
				info->dev_node, 0, 1);
			delay = 2500;	/* wait for 2.5s before using */
		} else if (info->is_irda)
			mdelay(50);	/* Do better here once the problems
			                 * with blocking have been ironed out
			                 */
	} else {
		/* TODO: Make that depend on a timer, don't power down
		 * immediately
		 */
		PWRDBG("ttyS%d: shutting down hardware\n", info->line);
		if (info->is_internal_modem) {
			PWRDBG("ttyS%d: shutting down modem\n", info->line);
			pmac_call_feature(
				PMAC_FTR_MODEM_ENABLE,
				info->dev_node, 0, 0);
		}
		pmac_call_feature(
			PMAC_FTR_SCC_ENABLE,
			info->dev_node, info->port_type, 0);
	}
	return delay;
}

static void irda_rts_pulses(struct mac_serial *info, int w)
{
	udelay(w);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB);
	udelay(2);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB | RTS);
	udelay(8);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB);
	udelay(4);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB | RTS);
}

/*
 * Set the irda codec on the imac to the specified baud rate.
 */
static void irda_setup(struct mac_serial *info)
{
	int code, speed, t;

	speed = info->tty->termios->c_cflag & CBAUD;
	if (speed < B2400 || speed > B115200)
		return;
	code = 0x4d + B115200 - speed;

	/* disable serial interrupts and receive DMA */
	write_zsreg(info->zs_channel, 1, info->curregs[1] & ~0x9f);

	/* wait for transmitter to drain */
	t = 10000;
	while ((read_zsreg(info->zs_channel, 0) & Tx_BUF_EMP) == 0
	       || (read_zsreg(info->zs_channel, 1) & ALL_SNT) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "transmitter didn't drain\n");
			return;
		}
		udelay(10);
	}
	udelay(100);

	/* set to 8 bits, no parity, 19200 baud, RTS on, DTR off */
	write_zsreg(info->zs_channel, 4, X16CLK | SB1);
	write_zsreg(info->zs_channel, 11, TCBR | RCBR);
	t = BPS_TO_BRG(19200, ZS_CLOCK/16);
	write_zsreg(info->zs_channel, 12, t);
	write_zsreg(info->zs_channel, 13, t >> 8);
	write_zsreg(info->zs_channel, 14, BRENABL);
	write_zsreg(info->zs_channel, 3, Rx8 | RxENABLE);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB | RTS);

	/* set TxD low for ~104us and pulse RTS */
	udelay(1000);
	write_zsdata(info->zs_channel, 0xfe);
	irda_rts_pulses(info, 150);
	irda_rts_pulses(info, 180);
	irda_rts_pulses(info, 50);
	udelay(100);

	/* assert DTR, wait 30ms, talk to the chip */
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB | RTS | DTR);
	mdelay(30);
	while (read_zsreg(info->zs_channel, 0) & Rx_CH_AV)
		read_zsdata(info->zs_channel);

	write_zsdata(info->zs_channel, 1);
	t = 1000;
	while ((read_zsreg(info->zs_channel, 0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "irda_setup timed out on 1st byte\n");
			goto out;
		}
		udelay(10);
	}
	t = read_zsdata(info->zs_channel);
	if (t != 4)
		printk(KERN_ERR "irda_setup 1st byte = %x\n", t);

	write_zsdata(info->zs_channel, code);
	t = 1000;
	while ((read_zsreg(info->zs_channel, 0) & Rx_CH_AV) == 0) {
		if (--t <= 0) {
			printk(KERN_ERR "irda_setup timed out on 2nd byte\n");
			goto out;
		}
		udelay(10);
	}
	t = read_zsdata(info->zs_channel);
	if (t != code)
		printk(KERN_ERR "irda_setup 2nd byte = %x (%x)\n", t, code);

	/* Drop DTR again and do some more RTS pulses */
 out:
	udelay(100);
	write_zsreg(info->zs_channel, 5, Tx8 | TxENAB | RTS);
	irda_rts_pulses(info, 80);

	/* We should be right to go now.  We assume that load_zsregs
	   will get called soon to load up the correct baud rate etc. */
	info->curregs[5] = (info->curregs[5] | RTS) & ~DTR;
	info->pendregs[5] = info->curregs[5];
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct mac_serial *info, struct termios *old_termios)
{
	unsigned cflag;
	int	bits;
	int	brg, baud;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return;

	cflag = info->tty->termios->c_cflag;
	baud = tty_get_baud_rate(info->tty);
	if (baud == 0) {
		if (old_termios) {
			info->tty->termios->c_cflag &= ~CBAUD;
			info->tty->termios->c_cflag |= (old_termios->c_cflag & CBAUD);
			cflag = info->tty->termios->c_cflag;
			baud = tty_get_baud_rate(info->tty);
		}
		else
			baud = info->zs_baud;
	}
	if (baud > 230400)
		baud = 230400;
	else if (baud == 0)
		baud = 38400;

	spin_lock_irqsave(&info->lock, flags);
	info->zs_baud = baud;
	info->clk_divisor = 16;

	BAUDBG(KERN_DEBUG "set speed to %d bds, ", baud);

	switch (baud) {
	case ZS_CLOCK/16:	/* 230400 */
		info->curregs[4] = X16CLK;
		info->curregs[11] = 0;
		break;
	case ZS_CLOCK/32:	/* 115200 */
		info->curregs[4] = X32CLK;
		info->curregs[11] = 0;
		break;
	default:
		info->curregs[4] = X16CLK;
		info->curregs[11] = TCBR | RCBR;
		brg = BPS_TO_BRG(baud, ZS_CLOCK/info->clk_divisor);
		info->curregs[12] = (brg & 255);
		info->curregs[13] = ((brg >> 8) & 255);
		info->curregs[14] = BRENABL;
	}

	/* byte size and parity */
	info->curregs[3] &= ~RxNBITS_MASK;
	info->curregs[5] &= ~TxNBITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		info->curregs[3] |= Rx5;
		info->curregs[5] |= Tx5;
		BAUDBG("5 bits, ");
		bits = 7;
		break;
	case CS6:
		info->curregs[3] |= Rx6;
		info->curregs[5] |= Tx6;
		BAUDBG("6 bits, ");
		bits = 8;
		break;
	case CS7:
		info->curregs[3] |= Rx7;
		info->curregs[5] |= Tx7;
		BAUDBG("7 bits, ");
		bits = 9;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		info->curregs[3] |= Rx8;
		info->curregs[5] |= Tx8;
		BAUDBG("8 bits, ");
		bits = 10;
		break;
	}
	info->pendregs[3] = info->curregs[3];
	info->pendregs[5] = info->curregs[5];

	info->curregs[4] &= ~(SB_MASK | PAR_ENA | PAR_EVEN);
	if (cflag & CSTOPB) {
		info->curregs[4] |= SB2;
		bits++;
		BAUDBG("2 stop, ");
	} else {
		info->curregs[4] |= SB1;
		BAUDBG("1 stop, ");
	}
	if (cflag & PARENB) {
		bits++;
 		info->curregs[4] |= PAR_ENA;
		BAUDBG("parity, ");
	}
	if (!(cflag & PARODD)) {
		info->curregs[4] |= PAR_EVEN;
	}
	info->pendregs[4] = info->curregs[4];

	if (!(cflag & CLOCAL)) {
		if (!(info->curregs[15] & DCDIE))
			info->read_reg_zero = read_zsreg(info->zs_channel, 0);
		info->curregs[15] |= DCDIE;
	} else
		info->curregs[15] &= ~DCDIE;
	if (cflag & CRTSCTS) {
		info->curregs[15] |= CTSIE;
		if ((read_zsreg(info->zs_channel, 0) & CTS) != 0)
			info->tx_stopped = 1;
	} else {
		info->curregs[15] &= ~CTSIE;
		info->tx_stopped = 0;
	}
	info->pendregs[15] = info->curregs[15];

	/* Calc timeout value. This is pretty broken with high baud rates with HZ=100.
	   This code would love a larger HZ and a >1 fifo size, but this is not
	   a priority. The resulting value must be >HZ/2
	 */
	info->timeout = ((info->xmit_fifo_size*HZ*bits) / baud);
	info->timeout += HZ/50+1;	/* Add .02 seconds of slop */

	BAUDBG("timeout=%d/%ds, base:%d\n", (int)info->timeout, (int)HZ,
	       (int)info->baud_base);

	/* set the irda codec to the right rate */
	if (info->is_irda)
		irda_setup(info);

	/* Load up the new values */
	load_zsregs(info->zs_channel, info->curregs);

	spin_unlock_irqrestore(&info->lock, flags);
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_flush_chars"))
		return;

	spin_lock_irqsave(&info->lock, flags);
	if (!(info->xmit_cnt <= 0 || tty->stopped || info->tx_stopped ||
	      !info->xmit_buf))
		/* Enable transmitter */
		transmit_chars(info);
	spin_unlock_irqrestore(&info->lock, flags);
}

static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	int	c, ret = 0;
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf || !tmp_buf)
		return 0;

	while (1) {
		spin_lock_irqsave(&info->lock, flags);
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
					  SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0) {
			spin_unlock_irqrestore(&info->lock, flags);
			break;
		}
		memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head = ((info->xmit_head + c) &
				   (SERIAL_XMIT_SIZE-1));
		info->xmit_cnt += c;
		spin_unlock_irqrestore(&info->lock, flags);
		buf += c;
		count -= c;
		ret += c;
	}
	spin_lock_irqsave(&info->lock, flags);
	if (info->xmit_cnt && !tty->stopped && !info->tx_stopped
	    && !info->tx_active)
		transmit_chars(info);
	spin_unlock_irqrestore(&info->lock, flags);
	return ret;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
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
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;
	spin_lock_irqsave(&info->lock, flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	spin_unlock_irqrestore(&info->lock, flags);
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
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	printk(KERN_DEBUG "throttle %ld....\n",tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;

	if (I_IXOFF(tty)) {
		spin_lock_irqsave(&info->lock, flags);
		info->x_char = STOP_CHAR(tty);
		if (!info->tx_active)
			transmit_chars(info);
		spin_unlock_irqrestore(&info->lock, flags);
	}

	if (C_CRTSCTS(tty)) {
		/*
		 * Here we want to turn off the RTS line.  On Macintoshes,
		 * the external serial ports using a DIN-8 or DIN-9
		 * connector only have the DTR line (which is usually
		 * wired to both RTS and DTR on an external modem in
		 * the cable).  RTS doesn't go out to the serial port
		 * socket, it acts as an output enable for the transmit
		 * data line.  So in this case we don't drop RTS.
		 *
		 * Macs with internal modems generally do have both RTS
		 * and DTR wired to the modem, so in that case we do
		 * drop RTS.
		 */
		if (info->is_internal_modem) {
			spin_lock_irqsave(&info->lock, flags);
			info->curregs[5] &= ~RTS;
			info->pendregs[5] &= ~RTS;
			write_zsreg(info->zs_channel, 5, info->curregs[5]);
			spin_unlock_irqrestore(&info->lock, flags);
		}
	}
	
#ifdef CDTRCTS
	if (tty->termios->c_cflag & CDTRCTS) {
		spin_lock_irqsave(&info->lock, flags);
		info->curregs[5] &= ~DTR;
		info->pendregs[5] &= ~DTR;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
		spin_unlock_irqrestore(&info->lock, flags);
	}
#endif /* CDTRCTS */
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	printk(KERN_DEBUG "unthrottle %s: %d....\n",
			tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->name, "rs_unthrottle"))
		return;

	if (I_IXOFF(tty)) {
		spin_lock_irqsave(&info->lock, flags);
		if (info->x_char)
			info->x_char = 0;
		else {
			info->x_char = START_CHAR(tty);
			if (!info->tx_active)
				transmit_chars(info);
		}
		spin_unlock_irqrestore(&info->lock, flags);
	}

	if (C_CRTSCTS(tty) && info->is_internal_modem) {
		/* Assert RTS line */
		spin_lock_irqsave(&info->lock, flags);
		info->curregs[5] |= RTS;
		info->pendregs[5] |= RTS;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
		spin_unlock_irqrestore(&info->lock, flags);
	}

#ifdef CDTRCTS
	if (tty->termios->c_cflag & CDTRCTS) {
		/* Assert DTR line */
		spin_lock_irqsave(&info->lock, flags);
		info->curregs[5] |= DTR;
		info->pendregs[5] |= DTR;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
		spin_unlock_irqrestore(&info->lock, flags);
	}
#endif
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct mac_serial * info,
			   struct serial_struct __user * retinfo)
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
	if (copy_to_user(retinfo,&tmp,sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int set_serial_info(struct mac_serial * info,
			   struct serial_struct __user * new_info)
{
	struct serial_struct new_serial;
	struct mac_serial old_info;
	int 			retval = 0;

	if (copy_from_user(&new_serial,new_info,sizeof(new_serial)))
		return -EFAULT;
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
	if (info->flags & ZILOG_INITIALIZED)
		retval = setup_scc(info);
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
static int get_lsr_info(struct mac_serial * info, unsigned int *value)
{
	unsigned char status;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	status = read_zsreg(info->zs_channel, 0);
	spin_unlock_irqrestore(&info->lock, flags);
	status = (status & Tx_BUF_EMP)? TIOCSER_TEMT: 0;
	return put_user(status,value);
}

static int rs_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;
	unsigned char control, status;
	unsigned long flags;

#ifdef CONFIG_KGDB
	if (info->kgdb_channel)
		return -ENODEV;
#endif
	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	spin_lock_irqsave(&info->lock, flags);
	control = info->curregs[5];
	status = read_zsreg(info->zs_channel, 0);
	spin_unlock_irqrestore(&info->lock, flags);
	return    ((control & RTS) ? TIOCM_RTS: 0)
		| ((control & DTR) ? TIOCM_DTR: 0)
		| ((status  & DCD) ? TIOCM_CAR: 0)
		| ((status  & CTS) ? 0: TIOCM_CTS);
}

static int rs_tiocmset(struct tty_struct *tty, struct file *file,
		       unsigned int set, unsigned int clear)
{
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;
	unsigned int arg, bits;
	unsigned long flags;

#ifdef CONFIG_KGDB
	if (info->kgdb_channel)
		return -ENODEV;
#endif
	if (serial_paranoia_check(info, tty->name, __FUNCTION__))
		return -ENODEV;

	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;

	spin_lock_irqsave(&info->lock, flags);
	if (set & TIOCM_RTS)
		info->curregs[5] |= RTS;
	if (set & TIOCM_DTR)
		info->curregs[5] |= DTR;
	if (clear & TIOCM_RTS)
		info->curregs[5] &= ~RTS;
	if (clear & TIOCM_DTR)
		info->curregs[5] &= ~DTR;

	info->pendregs[5] = info->curregs[5];
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	spin_unlock_irqrestore(&info->lock, flags);
	return 0;
}

/*
 * rs_break - turn transmit break condition on/off
 */
static void rs_break(struct tty_struct *tty, int break_state)
{
	struct mac_serial *info = (struct mac_serial *) tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_break"))
		return;

	spin_lock_irqsave(&info->lock, flags);
	if (break_state == -1)
		info->curregs[5] |= SND_BRK;
	else
		info->curregs[5] &= ~SND_BRK;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	spin_unlock_irqrestore(&info->lock, flags);
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;

#ifdef CONFIG_KGDB
	if (info->kgdb_channel)
		return -ENODEV;
#endif
	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}

	switch (cmd) {
		case TIOCGSERIAL:
			return get_serial_info(info,
					(struct serial_struct __user *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					(struct serial_struct __user *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			if (copy_to_user((struct mac_serial __user *) arg,
					 info, sizeof(struct mac_serial)))
				return -EFAULT;
			return 0;

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	int was_stopped;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;
	was_stopped = info->tx_stopped;

	change_speed(info, old_termios);

	if (was_stopped && !info->tx_stopped) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
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
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;

	spin_lock_irqsave(&info->lock, flags);

	if (tty_hung_up_p(filp)) {
		spin_unlock_irqrestore(&info->lock, flags);
		return;
	}

	OPNDBG("rs_close ttyS%d, count = %d\n", info->line, info->count);
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "rs_close: bad serial port count; tty->count "
				"is 1, info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "rs_close: bad serial port count for "
				"ttyS%d: %d\n", info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		spin_unlock_irqrestore(&info->lock, flags);
		return;
	}
	info->flags |= ZILOG_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	OPNDBG("waiting end of Tx... (timeout:%d)\n", info->closing_wait);
	tty->closing = 1;
	if (info->closing_wait != ZILOG_CLOSING_WAIT_NONE) {
		spin_unlock_irqrestore(&info->lock, flags);
		tty_wait_until_sent(tty, info->closing_wait);
		spin_lock_irqsave(&info->lock, flags);
	}

	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receiver and receive interrupts.
	 */
	info->curregs[3] &= ~RxENABLE;
	info->pendregs[3] = info->curregs[3];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	info->curregs[1] &= ~(0x18);	/* disable any rx ints */
	info->pendregs[1] = info->curregs[1];
	write_zsreg(info->zs_channel, 1, info->curregs[1]);
	ZS_CLEARFIFO(info->zs_channel);
	if (info->flags & ZILOG_INITIALIZED) {
		/*
		 * Before we drop DTR, make sure the SCC transmitter
		 * has completely drained.
		 */
		OPNDBG("waiting end of Rx...\n");
		spin_unlock_irqrestore(&info->lock, flags);
		rs_wait_until_sent(tty, info->timeout);
		spin_lock_irqsave(&info->lock, flags);
	}

	shutdown(info);
	/* restore flags now since shutdown() will have disabled this port's
	   specific irqs */
	spin_unlock_irqrestore(&info->lock, flags);

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
}

/*
 * rs_wait_until_sent() --- wait until the transmitter is empty
 */
static void rs_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct mac_serial *info = (struct mac_serial *) tty->driver_data;
	unsigned long orig_jiffies, char_time;

	if (serial_paranoia_check(info, tty->name, "rs_wait_until_sent"))
		return;

/*	printk("rs_wait_until_sent, timeout:%d, tty_stopped:%d, tx_stopped:%d\n",
			timeout, tty->stopped, info->tx_stopped);
*/
	orig_jiffies = jiffies;
	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 */
	if (info->timeout <= HZ/50) {
		printk(KERN_INFO "macserial: invalid info->timeout=%d\n",
				    info->timeout);
		info->timeout = HZ/50+1;
	}

	char_time = (info->timeout - HZ/50) / info->xmit_fifo_size;
	char_time = char_time / 5;
	if (char_time > HZ) {
		printk(KERN_WARNING "macserial: char_time %ld >HZ !!!\n",
				    char_time);
		char_time = 1;
	} else if (char_time == 0)
		char_time = 1;
	if (timeout)
		char_time = min_t(unsigned long, char_time, timeout);
	while ((read_zsreg(info->zs_channel, 1) & ALL_SNT) == 0) {
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
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;

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
			   struct mac_serial *info)
{
	DECLARE_WAITQUEUE(wait,current);
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ZILOG_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
		return -EAGAIN;
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
	OPNDBG("block_til_ready before block: ttyS%d, count = %d\n",
	       info->line, info->count);
	spin_lock_irq(&info->lock);
	if (!tty_hung_up_p(filp)) 
		info->count--;
	spin_unlock_irq(&info->lock);
	info->blocked_open++;
	while (1) {
		spin_lock_irq(&info->lock);
		if ((tty->termios->c_cflag & CBAUD) &&
		    !info->is_irda)
			zs_rtsdtr(info, 1);
		spin_unlock_irq(&info->lock);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ZILOG_INITIALIZED)) {
			retval = -EAGAIN;
			break;
		}
		if (!(info->flags & ZILOG_CLOSING) &&
		    (do_clocal || (read_zsreg(info->zs_channel, 0) & DCD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		OPNDBG("block_til_ready blocking: ttyS%d, count = %d\n",
		       info->line, info->count);
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
	OPNDBG("block_til_ready after blocking: ttyS%d, count = %d\n",
	       info->line, info->count);
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
	struct mac_serial	*info;
	int 			retval, line;
	unsigned long		page;

	line = tty->index;
	if ((line < 0) || (line >= zs_channels_found)) {
		return -ENODEV;
	}
	info = zs_soft + line;

#ifdef CONFIG_KGDB
	if (info->kgdb_channel) {
		return -ENODEV;
	}
#endif
	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;
	OPNDBG("rs_open %s, count = %d, tty=%p\n", tty->name,
	       info->count, tty);

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	if (!tmp_buf) {
		page = get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		if (tmp_buf)
			free_page(page);
		else
			tmp_buf = (unsigned char *) page;
	}

	/*
	 * If the port is the middle of closing, bail out now
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ZILOG_CLOSING)) {
		if (info->flags & ZILOG_CLOSING)
			interruptible_sleep_on(&info->close_wait);
		return -EAGAIN;
	}

	/*
	 * Start up serial port
	 */

	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
		OPNDBG("rs_open returning after block_til_ready with %d\n",
			retval);
		return retval;
	}

#ifdef CONFIG_SERIAL_CONSOLE
	if (sercons.cflag && sercons.index == line) {
		tty->termios->c_cflag = sercons.cflag;
		sercons.cflag = 0;
		change_speed(info, 0);
	}
#endif

	OPNDBG("rs_open %s successful...\n", tty->name);
	return 0;
}

/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	printk(KERN_INFO "PowerMac Z8530 serial driver version " MACSERIAL_VERSION "\n");
}

/*
 * Initialize one channel, both the mac_serial and mac_zschannel
 * structs.  We use the dev_node field of the mac_serial struct.
 */
static int
chan_init(struct mac_serial *zss, struct mac_zschannel *zs_chan,
	  struct mac_zschannel *zs_chan_a)
{
	struct device_node *ch = zss->dev_node;
	char *conn;
	int len;
	struct slot_names_prop {
		int	count;
		char	name[1];
	} *slots;

	zss->irq = ch->intrs[0].line;
	zss->has_dma = 0;
#if !defined(CONFIG_KGDB) && defined(SUPPORT_SERIAL_DMA)
	if (ch->n_addrs >= 3 && ch->n_intrs == 3)
		zss->has_dma = 1;
#endif
	zss->dma_initted = 0;

	zs_chan->control = (volatile unsigned char *)
		ioremap(ch->addrs[0].address, 0x1000);
	zs_chan->data = zs_chan->control + 0x10;
	spin_lock_init(&zs_chan->lock);
	zs_chan->parent = zss;
	zss->zs_channel = zs_chan;
	zss->zs_chan_a = zs_chan_a;

	/* setup misc varariables */
	zss->kgdb_channel = 0;

	/* For now, we assume you either have a slot-names property
	 * with "Modem" in it, or your channel is compatible with
	 * "cobalt". Might need additional fixups
	 */
	zss->is_internal_modem = device_is_compatible(ch, "cobalt");
	conn = get_property(ch, "AAPL,connector", &len);
	zss->is_irda = conn && (strcmp(conn, "infrared") == 0);
	zss->port_type = PMAC_SCC_ASYNC;
	/* 1999 Powerbook G3 has slot-names property instead */
	slots = (struct slot_names_prop *)get_property(ch, "slot-names", &len);
	if (slots && slots->count > 0) {
		if (strcmp(slots->name, "IrDA") == 0)
			zss->is_irda = 1;
		else if (strcmp(slots->name, "Modem") == 0)
			zss->is_internal_modem = 1;
	}
	if (zss->is_irda)
		zss->port_type = PMAC_SCC_IRDA;
	if (zss->is_internal_modem) {
		struct device_node* i2c_modem = find_devices("i2c-modem");
		if (i2c_modem) {
			char* mid = get_property(i2c_modem, "modem-id", NULL);
			if (mid) switch(*mid) {
			case 0x04 :
			case 0x05 :
			case 0x07 :
			case 0x08 :
			case 0x0b :
			case 0x0c :
				zss->port_type = PMAC_SCC_I2S1;
			}
			printk(KERN_INFO "macserial: i2c-modem detected, id: %d\n",
				mid ? (*mid) : 0);
		} else {
			printk(KERN_INFO "macserial: serial modem detected\n");
		}
	}

	while (zss->has_dma) {
		zss->dma_priv = NULL;
		/* it seems that the last two addresses are the
		   DMA controllers */
		zss->tx_dma = (volatile struct dbdma_regs *)
			ioremap(ch->addrs[ch->n_addrs - 2].address, 0x100);
		zss->rx = (volatile struct mac_dma *)
			ioremap(ch->addrs[ch->n_addrs - 1].address, 0x100);
		zss->tx_dma_irq = ch->intrs[1].line;
		zss->rx_dma_irq = ch->intrs[2].line;
		spin_lock_init(&zss->rx_dma_lock);
		break;
	}

	init_timer(&zss->powerup_timer);
	zss->powerup_timer.function = powerup_done;
	zss->powerup_timer.data = (unsigned long) zss;
	return 0;
}

/*
 * /proc fs routines. TODO: Add status lines & error stats
 */
static inline int
line_info(char *buf, struct mac_serial *info)
{
	int		ret=0;
	unsigned char* connector;
	int lenp;

	ret += sprintf(buf, "%d: port:0x%X irq:%d", info->line, info->port, info->irq);

	connector = get_property(info->dev_node, "AAPL,connector", &lenp);
	if (connector)
		ret+=sprintf(buf+ret," con:%s ", connector);
	if (info->is_internal_modem) {
		if (!connector)
			ret+=sprintf(buf+ret," con:");
		ret+=sprintf(buf+ret,"%s", " (internal modem)");
	}
	if (info->is_irda) {
		if (!connector)
			ret+=sprintf(buf+ret," con:");
		ret+=sprintf(buf+ret,"%s", " (IrDA)");
	}
	ret+=sprintf(buf+ret,"\n");

	return ret;
}

int macserial_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	int l, len = 0;
	off_t	begin = 0;
	struct mac_serial *info;

	len += sprintf(page, "serinfo:1.0 driver:" MACSERIAL_VERSION "\n");
	for (info = zs_chain; info && len < 4000; info = info->zs_next) {
		l = line_info(page + len, info);
		len += l;
		if (len+begin > off+count)
			goto done;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len+begin)
		return 0;
	*start = page + (off-begin);
	return ((count < begin+len-off) ? count : begin+len-off);
}

/* Ask the PROM how many Z8530s we have and initialize their zs_channels */
static void
probe_sccs(void)
{
	struct device_node *dev, *ch;
	struct mac_serial **pp;
	int n, chip, nchan;
	struct mac_zschannel *zs_chan;
	int chan_a_index;

	n = 0;
	pp = &zs_chain;
	zs_chan = zs_channels;
	for (dev = find_devices("escc"); dev != 0; dev = dev->next) {
		nchan = 0;
		chip = n;
		if (n >= NUM_CHANNELS) {
			printk(KERN_WARNING "Sorry, can't use %s: no more "
					    "channels\n", dev->full_name);
			continue;
		}
		chan_a_index = 0;
		for (ch = dev->child; ch != 0; ch = ch->sibling) {
			if (nchan >= 2) {
				printk(KERN_WARNING "SCC: Only 2 channels per "
					"chip are supported\n");
				break;
			}
			if (ch->n_addrs < 1 || (ch ->n_intrs < 1)) {
				printk("Can't use %s: %d addrs %d intrs\n",
				      ch->full_name, ch->n_addrs, ch->n_intrs);
				continue;
			}

			/* The channel with the higher address
			   will be the A side. */
			if (nchan > 0 &&
			    ch->addrs[0].address
			    > zs_soft[n-1].dev_node->addrs[0].address)
				chan_a_index = 1;

			/* minimal initialization for now */
			zs_soft[n].dev_node = ch;
			*pp = &zs_soft[n];
			pp = &zs_soft[n].zs_next;
			++nchan;
			++n;
		}
		if (nchan == 0)
			continue;

		/* set up A side */
		if (chan_init(&zs_soft[chip + chan_a_index], zs_chan, zs_chan))
			continue;
		++zs_chan;

		/* set up B side, if it exists */
		if (nchan > 1)
			if (chan_init(&zs_soft[chip + 1 - chan_a_index],
				  zs_chan, zs_chan - 1))
				continue;
		++zs_chan;
	}
	*pp = 0;

	zs_channels_found = n;
#ifdef CONFIG_PMAC_PBOOK
	if (n)
		pmu_register_sleep_notifier(&serial_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
}

static struct tty_operations serial_ops = {
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
	.read_proc = macserial_read_proc,
	.tiocmget = rs_tiocmget,
	.tiocmset = rs_tiocmset,
};

static int macserial_init(void)
{
	int channel, i;
	struct mac_serial *info;

	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();

	serial_driver = alloc_tty_driver(zs_channels_found);
	if (!serial_driver)
		return -ENOMEM;

	/* XXX assume it's a powerbook if we have a via-pmu
	 * 
	 * This is OK for core99 machines as well.
	 */
	is_powerbook = find_devices("via-pmu") != 0;

	/* Register the interrupt handler for each one
	 * We also request the OF resources here as probe_sccs()
	 * might be called too early for that
	 */
	for (i = 0; i < zs_channels_found; ++i) {
		struct device_node* ch = zs_soft[i].dev_node;
		if (!request_OF_resource(ch, 0, NULL)) {
			printk(KERN_ERR "macserial: can't request IO resource !\n");
			put_tty_driver(serial_driver);
			return -ENODEV;
		}
		if (zs_soft[i].has_dma) {
			if (!request_OF_resource(ch, ch->n_addrs - 2, " (tx dma)")) {
				printk(KERN_ERR "macserial: can't request TX DMA resource !\n");
				zs_soft[i].has_dma = 0;
				goto no_dma;
			}
			if (!request_OF_resource(ch, ch->n_addrs - 1, " (rx dma)")) {
				release_OF_resource(ch, ch->n_addrs - 2);
				printk(KERN_ERR "macserial: can't request RX DMA resource !\n");
				zs_soft[i].has_dma = 0;
				goto no_dma;
			}
			if (request_irq(zs_soft[i].tx_dma_irq, rs_txdma_irq, 0,
					"SCC-txdma", &zs_soft[i]))
				printk(KERN_ERR "macserial: can't get irq %d\n",
				       zs_soft[i].tx_dma_irq);
			disable_irq(zs_soft[i].tx_dma_irq);
			if (request_irq(zs_soft[i].rx_dma_irq, rs_rxdma_irq, 0,
					"SCC-rxdma", &zs_soft[i]))
				printk(KERN_ERR "macserial: can't get irq %d\n",
				       zs_soft[i].rx_dma_irq);
			disable_irq(zs_soft[i].rx_dma_irq);
		}
no_dma:		
		if (request_irq(zs_soft[i].irq, rs_interrupt, 0,
				"SCC", &zs_soft[i]))
			printk(KERN_ERR "macserial: can't get irq %d\n",
			       zs_soft[i].irq);
		disable_irq(zs_soft[i].irq);
	}

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* Not all of this is exactly right for us. */

	serial_driver->owner = THIS_MODULE;
	serial_driver->driver_name = "macserial";
	serial_driver->devfs_name = "tts/";
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag =
		B38400 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &serial_ops);

	if (tty_register_driver(serial_driver))
		printk(KERN_ERR "Error: couldn't register serial driver\n");

	for (channel = 0; channel < zs_channels_found; ++channel) {
#ifdef CONFIG_KGDB
		if (zs_soft[channel].kgdb_channel) {
			kgdb_interruptible(1);
			continue;
		}
#endif
		zs_soft[channel].clk_divisor = 16;
/* -- we are not sure the SCC is powered ON at this point
 		zs_soft[channel].zs_baud = get_zsbaud(&zs_soft[channel]);
*/
		zs_soft[channel].zs_baud = 38400;

		/* If console serial line, then enable interrupts. */
		if (zs_soft[channel].is_cons) {
			printk(KERN_INFO "macserial: console line, enabling "
					"interrupt %d\n", zs_soft[channel].irq);
			panic("macserial: console not supported yet !");
			write_zsreg(zs_soft[channel].zs_channel, R1,
				    (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB));
			write_zsreg(zs_soft[channel].zs_channel, R9,
				    (NV | MIE));
		}
	}

	for (info = zs_chain, i = 0; info; info = info->zs_next, i++)
	{
		unsigned char* connector;
		int lenp;

#ifdef CONFIG_KGDB
		if (info->kgdb_channel) {
			continue;
		}
#endif
		info->magic = SERIAL_MAGIC;
		info->port = (int) info->zs_channel->control;
		info->line = i;
		info->tty = 0;
		info->custom_divisor = 16;
		info->timeout = 0;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		INIT_WORK(&info->tqueue, do_softint, info);
		spin_lock_init(&info->lock);
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->timeout = HZ;
		printk(KERN_INFO "tty%02d at 0x%08x (irq = %d)", info->line, 
			info->port, info->irq);
		printk(" is a Z8530 ESCC");
		connector = get_property(info->dev_node, "AAPL,connector", &lenp);
		if (connector)
			printk(", port = %s", connector);
		if (info->is_internal_modem)
			printk(" (internal modem)");
		if (info->is_irda)
			printk(" (IrDA)");
		printk("\n");
 	}
	tmp_buf = 0;

	return 0;
}

void macserial_cleanup(void)
{
	int i;
	unsigned long flags;
	struct mac_serial *info;

	for (info = zs_chain, i = 0; info; info = info->zs_next, i++)
		set_scc_power(info, 0);
	spin_lock_irqsave(&info->lock, flags);
	for (i = 0; i < zs_channels_found; ++i) {
		free_irq(zs_soft[i].irq, &zs_soft[i]);
		if (zs_soft[i].has_dma) {
			free_irq(zs_soft[i].tx_dma_irq, &zs_soft[i]);
			free_irq(zs_soft[i].rx_dma_irq, &zs_soft[i]);
		}
		release_OF_resource(zs_soft[i].dev_node, 0);
		if (zs_soft[i].has_dma) {
			struct device_node* ch = zs_soft[i].dev_node;
			release_OF_resource(ch, ch->n_addrs - 2);
			release_OF_resource(ch, ch->n_addrs - 1);
		}
	}
	spin_unlock_irqrestore(&info->lock, flags);
	tty_unregister_driver(serial_driver);
	put_tty_driver(serial_driver);

	if (tmp_buf) {
		free_page((unsigned long) tmp_buf);
		tmp_buf = 0;
	}

#ifdef CONFIG_PMAC_PBOOK
	if (zs_channels_found)
		pmu_unregister_sleep_notifier(&serial_sleep_notifier);
#endif /* CONFIG_PMAC_PBOOK */
}

module_init(macserial_init);
module_exit(macserial_cleanup);
MODULE_LICENSE("GPL");

#if 0
/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
/* PowerMac: Unused at this time, just here to make things link. */
int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
	return;
}
#endif

/*
 * ------------------------------------------------------------
 * Serial console driver
 * ------------------------------------------------------------
 */
#ifdef CONFIG_SERIAL_CONSOLE

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 */
static void serial_console_write(struct console *co, const char *s,
				 unsigned count)
{
	struct mac_serial *info = zs_soft + co->index;
	int i;

	/* Turn of interrupts and enable the transmitter. */
	write_zsreg(info->zs_channel, R1, info->curregs[1] & ~TxINT_ENAB);
	write_zsreg(info->zs_channel, R5, info->curregs[5] | TxENAB | RTS | DTR);

	for (i=0; i<count; i++) {
		/* Wait for the transmit buffer to empty. */
		while ((read_zsreg(info->zs_channel, 0) & Tx_BUF_EMP) == 0) {
			eieio();
		}

		write_zsdata(info->zs_channel, s[i]);
		if (s[i] == 10) {
			while ((read_zsreg(info->zs_channel, 0) & Tx_BUF_EMP)
                                == 0)
				eieio();

			write_zsdata(info->zs_channel, 13);
		}
	}

	/* Restore the values in the registers. */
	write_zsreg(info->zs_channel, R1, info->curregs[1]);
	/* Don't disable the transmitter. */
}

static struct tty_driver *serial_driver;

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
	struct mac_serial *info;
	int	baud = 38400;
	int	bits = 8;
	int	parity = 'n';
	int	cflag = CREAD | HUPCL | CLOCAL;
	int	brg;
	char	*s;
	long	flags;

	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();

	if (zs_chain == 0)
		return -1;

	/* Do we have the device asked for? */
	if (co->index >= zs_channels_found)
		return -1;
	info = zs_soft + co->index;

	set_scc_power(info, 1);

	/* Reset the channel */
	write_zsreg(info->zs_channel, R9, CHRA);

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
	case 9600:
		cflag |= B9600;
		break;
	case 19200:
		cflag |= B19200;
		break;
	case 57600:
		cflag |= B57600;
		break;
	case 115200:
		cflag |= B115200;
		break;
	case 38400:
	default:
		cflag |= B38400;
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
		cflag |= PARENB | PARODD;
		break;
	case 'e': case 'E':
		cflag |= PARENB;
		break;
	}
	co->cflag = cflag;

	spin_lock_irqsave(&info->lock, flags);
        memset(info->curregs, 0, sizeof(info->curregs));

	info->zs_baud = baud;
	info->clk_divisor = 16;
	switch (info->zs_baud) {
	case ZS_CLOCK/16:	/* 230400 */
		info->curregs[4] = X16CLK;
		info->curregs[11] = 0;
		break;
	case ZS_CLOCK/32:	/* 115200 */
		info->curregs[4] = X32CLK;
		info->curregs[11] = 0;
		break;
	default:
		info->curregs[4] = X16CLK;
		info->curregs[11] = TCBR | RCBR;
		brg = BPS_TO_BRG(info->zs_baud, ZS_CLOCK/info->clk_divisor);
		info->curregs[12] = (brg & 255);
		info->curregs[13] = ((brg >> 8) & 255);
		info->curregs[14] = BRENABL;
	}

	/* byte size and parity */
	info->curregs[3] &= ~RxNBITS_MASK;
	info->curregs[5] &= ~TxNBITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		info->curregs[3] |= Rx5;
		info->curregs[5] |= Tx5;
		break;
	case CS6:
		info->curregs[3] |= Rx6;
		info->curregs[5] |= Tx6;
		break;
	case CS7:
		info->curregs[3] |= Rx7;
		info->curregs[5] |= Tx7;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		info->curregs[3] |= Rx8;
		info->curregs[5] |= Tx8;
		break;
	}
        info->curregs[5] |= TxENAB | RTS | DTR;
	info->pendregs[3] = info->curregs[3];
	info->pendregs[5] = info->curregs[5];

	info->curregs[4] &= ~(SB_MASK | PAR_ENA | PAR_EVEN);
	if (cflag & CSTOPB) {
		info->curregs[4] |= SB2;
	} else {
		info->curregs[4] |= SB1;
	}
	if (cflag & PARENB) {
		info->curregs[4] |= PAR_ENA;
		if (!(cflag & PARODD)) {
			info->curregs[4] |= PAR_EVEN;
		}
	}
	info->pendregs[4] = info->curregs[4];

	if (!(cflag & CLOCAL)) {
		if (!(info->curregs[15] & DCDIE))
			info->read_reg_zero = read_zsreg(info->zs_channel, 0);
		info->curregs[15] |= DCDIE;
	} else
		info->curregs[15] &= ~DCDIE;
	if (cflag & CRTSCTS) {
		info->curregs[15] |= CTSIE;
		if ((read_zsreg(info->zs_channel, 0) & CTS) != 0)
			info->tx_stopped = 1;
	} else {
		info->curregs[15] &= ~CTSIE;
		info->tx_stopped = 0;
	}
	info->pendregs[15] = info->curregs[15];

	/* Load up the new values */
	load_zsregs(info->zs_channel, info->curregs);

	spin_unlock_irqrestore(&info->lock, flags);

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
static void __init mac_scc_console_init(void)
{
	register_console(&sercons);
}
console_initcall(mac_scc_console_init);

#endif /* ifdef CONFIG_SERIAL_CONSOLE */

#ifdef CONFIG_KGDB
/* These are for receiving and sending characters under the kgdb
 * source level kernel debugger.
 */
void putDebugChar(char kgdb_char)
{
	struct mac_zschannel *chan = zs_kgdbchan;
	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0)
		udelay(5);
	write_zsdata(chan, kgdb_char);
}

char getDebugChar(void)
{
	struct mac_zschannel *chan = zs_kgdbchan;
	while((read_zsreg(chan, 0) & Rx_CH_AV) == 0)
		eieio(); /*barrier();*/
	return read_zsdata(chan);
}

void kgdb_interruptible(int yes)
{
	struct mac_zschannel *chan = zs_kgdbchan;
	int one, nine;
	nine = read_zsreg(chan, 9);
	if (yes == 1) {
		one = EXT_INT_ENAB|INT_ALL_Rx;
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

/* This sets up the serial port we're using, and turns on
 * interrupts for that channel, so kgdb is usable once we're done.
 */
static inline void kgdb_chaninit(struct mac_zschannel *ms, int intson, int bps)
{
	int brg;
	int i, x;
	volatile char *sccc = ms->control;
	brg = BPS_TO_BRG(bps, ZS_CLOCK/16);
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
 * XXX at the moment probably only channel A will work
 */
void __init zs_kgdb_hook(int tty_num)
{
	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();

	set_scc_power(&zs_soft[tty_num], 1);

	zs_kgdbchan = zs_soft[tty_num].zs_channel;
	zs_soft[tty_num].change_needed = 0;
	zs_soft[tty_num].clk_divisor = 16;
	zs_soft[tty_num].zs_baud = 38400;
	zs_soft[tty_num].kgdb_channel = 1;     /* This runs kgdb */

	/* Turn on transmitter/receiver at 8-bits/char */
        kgdb_chaninit(zs_soft[tty_num].zs_channel, 1, 38400);
	printk("KGDB: on channel %d initialized\n", tty_num);
	set_debug_traps(); /* init stub */
}
#endif /* ifdef CONFIG_KGDB */

#ifdef CONFIG_PMAC_PBOOK
/*
 * notify clients before sleep and reset bus afterwards
 */
int
serial_notify_sleep(struct pmu_sleep_notifier *self, int when)
{
	int i;

	switch (when) {
	case PBOOK_SLEEP_REQUEST:
	case PBOOK_SLEEP_REJECT:
		break;

	case PBOOK_SLEEP_NOW:
		for (i=0; i<zs_channels_found; i++) {
			struct mac_serial *info = &zs_soft[i];
			if (info->flags & ZILOG_INITIALIZED) {
				shutdown(info);
				info->flags |= ZILOG_SLEEPING;
			}
		}
		break;
	case PBOOK_WAKE:
		for (i=0; i<zs_channels_found; i++) {
			struct mac_serial *info = &zs_soft[i];
			if (info->flags & ZILOG_SLEEPING) {
				info->flags &= ~ZILOG_SLEEPING;
				startup(info);
			}
		}
		break;
	}
	return PBOOK_SLEEP_OK;
}
#endif /* CONFIG_PMAC_PBOOK */
