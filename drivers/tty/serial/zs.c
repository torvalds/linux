// SPDX-License-Identifier: GPL-2.0
/*
 * zs.c: Serial port driver for IOASIC DECstations.
 *
 * Derived from drivers/sbus/char/sunserial.c by Paul Mackerras.
 * Derived from drivers/macintosh/macserial.c by Harald Koerfgen.
 *
 * DECstation changes
 * Copyright (C) 1998-2000 Harald Koerfgen
 * Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2007  Maciej W. Rozycki
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
 *
 * As you can immediately notice the wiring of the RTS, DTR and DSR signals
 * is a bit odd.  This makes the handling of port B unnecessarily
 * complicated and prevents the use of some automatic modes of operation.
 */

#include <linux/bug.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irqflags.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/major.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/spinlock.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/types.h>

#include <linux/atomic.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/system.h>

#include "zs.h"


MODULE_AUTHOR("Maciej W. Rozycki <macro@linux-mips.org>");
MODULE_DESCRIPTION("DECstation Z85C30 serial driver");
MODULE_LICENSE("GPL");


static char zs_name[] __initdata = "DECstation Z85C30 serial driver version ";
static char zs_version[] __initdata = "0.10";

/*
 * It would be nice to dynamically allocate everything that
 * depends on ZS_NUM_SCCS, so we could support any number of
 * Z85C30s, but for now...
 */
#define ZS_NUM_SCCS	2		/* Max # of ZS chips supported.  */
#define ZS_NUM_CHAN	2		/* 2 channels per chip.  */
#define ZS_CHAN_A	0		/* Index of the channel A.  */
#define ZS_CHAN_B	1		/* Index of the channel B.  */
#define ZS_CHAN_IO_SIZE 8		/* IOMEM space size.  */
#define ZS_CHAN_IO_STRIDE 4		/* Register alignment.  */
#define ZS_CHAN_IO_OFFSET 1		/* The SCC resides on the high byte
					   of the 16-bit IOBUS.  */
#define ZS_CLOCK        7372800 	/* Z85C30 PCLK input clock rate.  */

#define to_zport(uport) container_of(uport, struct zs_port, port)

struct zs_parms {
	resource_size_t scc[ZS_NUM_SCCS];
	int irq[ZS_NUM_SCCS];
};

static struct zs_scc zs_sccs[ZS_NUM_SCCS];

static u8 zs_init_regs[ZS_NUM_REGS] __initdata = {
	0,				/* write 0 */
	PAR_SPEC,			/* write 1 */
	0,				/* write 2 */
	0,				/* write 3 */
	X16CLK | SB1,			/* write 4 */
	0,				/* write 5 */
	0, 0, 0,			/* write 6, 7, 8 */
	MIE | DLC | NV,			/* write 9 */
	NRZ,				/* write 10 */
	TCBR | RCBR,			/* write 11 */
	0, 0,				/* BRG time constant, write 12 + 13 */
	BRSRC | BRENABL,		/* write 14 */
	0,				/* write 15 */
};

/*
 * Debugging.
 */
#undef ZS_DEBUG_REGS


/*
 * Reading and writing Z85C30 registers.
 */
static void recovery_delay(void)
{
	udelay(2);
}

static u8 read_zsreg(struct zs_port *zport, int reg)
{
	void __iomem *control = zport->port.membase + ZS_CHAN_IO_OFFSET;
	u8 retval;

	if (reg != 0) {
		writeb(reg & 0xf, control);
		fast_iob();
		recovery_delay();
	}
	retval = readb(control);
	recovery_delay();
	return retval;
}

static void write_zsreg(struct zs_port *zport, int reg, u8 value)
{
	void __iomem *control = zport->port.membase + ZS_CHAN_IO_OFFSET;

	if (reg != 0) {
		writeb(reg & 0xf, control);
		fast_iob(); recovery_delay();
	}
	writeb(value, control);
	fast_iob();
	recovery_delay();
	return;
}

static u8 read_zsdata(struct zs_port *zport)
{
	void __iomem *data = zport->port.membase +
			     ZS_CHAN_IO_STRIDE + ZS_CHAN_IO_OFFSET;
	u8 retval;

	retval = readb(data);
	recovery_delay();
	return retval;
}

static void write_zsdata(struct zs_port *zport, u8 value)
{
	void __iomem *data = zport->port.membase +
			     ZS_CHAN_IO_STRIDE + ZS_CHAN_IO_OFFSET;

	writeb(value, data);
	fast_iob();
	recovery_delay();
	return;
}

#ifdef ZS_DEBUG_REGS
void zs_dump(void)
{
	struct zs_port *zport;
	int i, j;

	for (i = 0; i < ZS_NUM_SCCS * ZS_NUM_CHAN; i++) {
		zport = &zs_sccs[i / ZS_NUM_CHAN].zport[i % ZS_NUM_CHAN];

		if (!zport->scc)
			continue;

		for (j = 0; j < 16; j++)
			printk("W%-2d = 0x%02x\t", j, zport->regs[j]);
		printk("\n");
		for (j = 0; j < 16; j++)
			printk("R%-2d = 0x%02x\t", j, read_zsreg(zport, j));
		printk("\n\n");
	}
}
#endif


static void zs_spin_lock_cond_irq(spinlock_t *lock, int irq)
{
	if (irq)
		spin_lock_irq(lock);
	else
		spin_lock(lock);
}

static void zs_spin_unlock_cond_irq(spinlock_t *lock, int irq)
{
	if (irq)
		spin_unlock_irq(lock);
	else
		spin_unlock(lock);
}

static int zs_receive_drain(struct zs_port *zport)
{
	int loops = 10000;

	while ((read_zsreg(zport, R0) & Rx_CH_AV) && --loops)
		read_zsdata(zport);
	return loops;
}

static int zs_transmit_drain(struct zs_port *zport, int irq)
{
	struct zs_scc *scc = zport->scc;
	int loops = 10000;

	while (!(read_zsreg(zport, R0) & Tx_BUF_EMP) && --loops) {
		zs_spin_unlock_cond_irq(&scc->zlock, irq);
		udelay(2);
		zs_spin_lock_cond_irq(&scc->zlock, irq);
	}
	return loops;
}

static int zs_line_drain(struct zs_port *zport, int irq)
{
	struct zs_scc *scc = zport->scc;
	int loops = 10000;

	while (!(read_zsreg(zport, R1) & ALL_SNT) && --loops) {
		zs_spin_unlock_cond_irq(&scc->zlock, irq);
		udelay(2);
		zs_spin_lock_cond_irq(&scc->zlock, irq);
	}
	return loops;
}


static void load_zsregs(struct zs_port *zport, u8 *regs, int irq)
{
	/* Let the current transmission finish.  */
	zs_line_drain(zport, irq);
	/* Load 'em up.  */
	write_zsreg(zport, R3, regs[3] & ~RxENABLE);
	write_zsreg(zport, R5, regs[5] & ~TxENAB);
	write_zsreg(zport, R4, regs[4]);
	write_zsreg(zport, R9, regs[9]);
	write_zsreg(zport, R1, regs[1]);
	write_zsreg(zport, R2, regs[2]);
	write_zsreg(zport, R10, regs[10]);
	write_zsreg(zport, R14, regs[14] & ~BRENABL);
	write_zsreg(zport, R11, regs[11]);
	write_zsreg(zport, R12, regs[12]);
	write_zsreg(zport, R13, regs[13]);
	write_zsreg(zport, R14, regs[14]);
	write_zsreg(zport, R15, regs[15]);
	if (regs[3] & RxENABLE)
		write_zsreg(zport, R3, regs[3]);
	if (regs[5] & TxENAB)
		write_zsreg(zport, R5, regs[5]);
	return;
}


/*
 * Status handling routines.
 */

/*
 * zs_tx_empty() -- get the transmitter empty status
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting.  This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space.
 */
static unsigned int zs_tx_empty(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	unsigned long flags;
	u8 status;

	spin_lock_irqsave(&scc->zlock, flags);
	status = read_zsreg(zport, R1);
	spin_unlock_irqrestore(&scc->zlock, flags);

	return status & ALL_SNT ? TIOCSER_TEMT : 0;
}

static unsigned int zs_raw_get_ab_mctrl(struct zs_port *zport_a,
					struct zs_port *zport_b)
{
	u8 status_a, status_b;
	unsigned int mctrl;

	status_a = read_zsreg(zport_a, R0);
	status_b = read_zsreg(zport_b, R0);

	mctrl = ((status_b & CTS) ? TIOCM_CTS : 0) |
		((status_b & DCD) ? TIOCM_CAR : 0) |
		((status_a & DCD) ? TIOCM_RNG : 0) |
		((status_a & SYNC_HUNT) ? TIOCM_DSR : 0);

	return mctrl;
}

static unsigned int zs_raw_get_mctrl(struct zs_port *zport)
{
	struct zs_port *zport_a = &zport->scc->zport[ZS_CHAN_A];

	return zport != zport_a ? zs_raw_get_ab_mctrl(zport_a, zport) : 0;
}

static unsigned int zs_raw_xor_mctrl(struct zs_port *zport)
{
	struct zs_port *zport_a = &zport->scc->zport[ZS_CHAN_A];
	unsigned int mmask, mctrl, delta;
	u8 mask_a, mask_b;

	if (zport == zport_a)
		return 0;

	mask_a = zport_a->regs[15];
	mask_b = zport->regs[15];

	mmask = ((mask_b & CTSIE) ? TIOCM_CTS : 0) |
		((mask_b & DCDIE) ? TIOCM_CAR : 0) |
		((mask_a & DCDIE) ? TIOCM_RNG : 0) |
		((mask_a & SYNCIE) ? TIOCM_DSR : 0);

	mctrl = zport->mctrl;
	if (mmask) {
		mctrl &= ~mmask;
		mctrl |= zs_raw_get_ab_mctrl(zport_a, zport) & mmask;
	}

	delta = mctrl ^ zport->mctrl;
	if (delta)
		zport->mctrl = mctrl;

	return delta;
}

static unsigned int zs_get_mctrl(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	unsigned int mctrl;

	spin_lock(&scc->zlock);
	mctrl = zs_raw_get_mctrl(zport);
	spin_unlock(&scc->zlock);

	return mctrl;
}

static void zs_set_mctrl(struct uart_port *uport, unsigned int mctrl)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	struct zs_port *zport_a = &scc->zport[ZS_CHAN_A];
	u8 oldloop, newloop;

	spin_lock(&scc->zlock);
	if (zport != zport_a) {
		if (mctrl & TIOCM_DTR)
			zport_a->regs[5] |= DTR;
		else
			zport_a->regs[5] &= ~DTR;
		if (mctrl & TIOCM_RTS)
			zport_a->regs[5] |= RTS;
		else
			zport_a->regs[5] &= ~RTS;
		write_zsreg(zport_a, R5, zport_a->regs[5]);
	}

	/* Rarely modified, so don't poke at hardware unless necessary. */
	oldloop = zport->regs[14];
	newloop = oldloop;
	if (mctrl & TIOCM_LOOP)
		newloop |= LOOPBAK;
	else
		newloop &= ~LOOPBAK;
	if (newloop != oldloop) {
		zport->regs[14] = newloop;
		write_zsreg(zport, R14, zport->regs[14]);
	}
	spin_unlock(&scc->zlock);
}

static void zs_raw_stop_tx(struct zs_port *zport)
{
	write_zsreg(zport, R0, RES_Tx_P);
	zport->tx_stopped = 1;
}

static void zs_stop_tx(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;

	spin_lock(&scc->zlock);
	zs_raw_stop_tx(zport);
	spin_unlock(&scc->zlock);
}

static void zs_raw_transmit_chars(struct zs_port *);

static void zs_start_tx(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;

	spin_lock(&scc->zlock);
	if (zport->tx_stopped) {
		zs_transmit_drain(zport, 0);
		zport->tx_stopped = 0;
		zs_raw_transmit_chars(zport);
	}
	spin_unlock(&scc->zlock);
}

static void zs_stop_rx(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	struct zs_port *zport_a = &scc->zport[ZS_CHAN_A];

	spin_lock(&scc->zlock);
	zport->regs[15] &= ~BRKIE;
	zport->regs[1] &= ~(RxINT_MASK | TxINT_ENAB);
	zport->regs[1] |= RxINT_DISAB;

	if (zport != zport_a) {
		/* A-side DCD tracks RI and SYNC tracks DSR.  */
		zport_a->regs[15] &= ~(DCDIE | SYNCIE);
		write_zsreg(zport_a, R15, zport_a->regs[15]);
		if (!(zport_a->regs[15] & BRKIE)) {
			zport_a->regs[1] &= ~EXT_INT_ENAB;
			write_zsreg(zport_a, R1, zport_a->regs[1]);
		}

		/* This-side DCD tracks DCD and CTS tracks CTS.  */
		zport->regs[15] &= ~(DCDIE | CTSIE);
		zport->regs[1] &= ~EXT_INT_ENAB;
	} else {
		/* DCD tracks RI and SYNC tracks DSR for the B side.  */
		if (!(zport->regs[15] & (DCDIE | SYNCIE)))
			zport->regs[1] &= ~EXT_INT_ENAB;
	}

	write_zsreg(zport, R15, zport->regs[15]);
	write_zsreg(zport, R1, zport->regs[1]);
	spin_unlock(&scc->zlock);
}

static void zs_enable_ms(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	struct zs_port *zport_a = &scc->zport[ZS_CHAN_A];

	if (zport == zport_a)
		return;

	spin_lock(&scc->zlock);

	/* Clear Ext interrupts if not being handled already.  */
	if (!(zport_a->regs[1] & EXT_INT_ENAB))
		write_zsreg(zport_a, R0, RES_EXT_INT);

	/* A-side DCD tracks RI and SYNC tracks DSR.  */
	zport_a->regs[1] |= EXT_INT_ENAB;
	zport_a->regs[15] |= DCDIE | SYNCIE;

	/* This-side DCD tracks DCD and CTS tracks CTS.  */
	zport->regs[15] |= DCDIE | CTSIE;

	zs_raw_xor_mctrl(zport);

	write_zsreg(zport_a, R1, zport_a->regs[1]);
	write_zsreg(zport_a, R15, zport_a->regs[15]);
	write_zsreg(zport, R15, zport->regs[15]);
	spin_unlock(&scc->zlock);
}

static void zs_break_ctl(struct uart_port *uport, int break_state)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	unsigned long flags;

	spin_lock_irqsave(&scc->zlock, flags);
	if (break_state == -1)
		zport->regs[5] |= SND_BRK;
	else
		zport->regs[5] &= ~SND_BRK;
	write_zsreg(zport, R5, zport->regs[5]);
	spin_unlock_irqrestore(&scc->zlock, flags);
}


/*
 * Interrupt handling routines.
 */
#define Rx_BRK 0x0100			/* BREAK event software flag.  */
#define Rx_SYS 0x0200			/* SysRq event software flag.  */

static void zs_receive_chars(struct zs_port *zport)
{
	struct uart_port *uport = &zport->port;
	struct zs_scc *scc = zport->scc;
	struct uart_icount *icount;
	unsigned int avail, status;
	int count;
	u8 ch, flag;

	for (count = 16; count; count--) {
		spin_lock(&scc->zlock);
		avail = read_zsreg(zport, R0) & Rx_CH_AV;
		spin_unlock(&scc->zlock);
		if (!avail)
			break;

		spin_lock(&scc->zlock);
		status = read_zsreg(zport, R1) & (Rx_OVR | FRM_ERR | PAR_ERR);
		ch = read_zsdata(zport);
		spin_unlock(&scc->zlock);

		flag = TTY_NORMAL;

		icount = &uport->icount;
		icount->rx++;

		/* Handle the null char got when BREAK is removed.  */
		if (!ch)
			status |= zport->tty_break;
		if (unlikely(status &
			     (Rx_OVR | FRM_ERR | PAR_ERR | Rx_SYS | Rx_BRK))) {
			zport->tty_break = 0;

			/* Reset the error indication.  */
			if (status & (Rx_OVR | FRM_ERR | PAR_ERR)) {
				spin_lock(&scc->zlock);
				write_zsreg(zport, R0, ERR_RES);
				spin_unlock(&scc->zlock);
			}

			if (status & (Rx_SYS | Rx_BRK)) {
				icount->brk++;
				/* SysRq discards the null char.  */
				if (status & Rx_SYS)
					continue;
			} else if (status & FRM_ERR)
				icount->frame++;
			else if (status & PAR_ERR)
				icount->parity++;
			if (status & Rx_OVR)
				icount->overrun++;

			status &= uport->read_status_mask;
			if (status & Rx_BRK)
				flag = TTY_BREAK;
			else if (status & FRM_ERR)
				flag = TTY_FRAME;
			else if (status & PAR_ERR)
				flag = TTY_PARITY;
		}

		if (uart_handle_sysrq_char(uport, ch))
			continue;

		uart_insert_char(uport, status, Rx_OVR, ch, flag);
	}

	tty_flip_buffer_push(&uport->state->port);
}

static void zs_raw_transmit_chars(struct zs_port *zport)
{
	struct circ_buf *xmit = &zport->port.state->xmit;

	/* XON/XOFF chars.  */
	if (zport->port.x_char) {
		write_zsdata(zport, zport->port.x_char);
		zport->port.icount.tx++;
		zport->port.x_char = 0;
		return;
	}

	/* If nothing to do or stopped or hardware stopped.  */
	if (uart_circ_empty(xmit) || uart_tx_stopped(&zport->port)) {
		zs_raw_stop_tx(zport);
		return;
	}

	/* Send char.  */
	write_zsdata(zport, xmit->buf[xmit->tail]);
	uart_xmit_advance(&zport->port, 1);

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(&zport->port);

	/* Are we are done?  */
	if (uart_circ_empty(xmit))
		zs_raw_stop_tx(zport);
}

static void zs_transmit_chars(struct zs_port *zport)
{
	struct zs_scc *scc = zport->scc;

	spin_lock(&scc->zlock);
	zs_raw_transmit_chars(zport);
	spin_unlock(&scc->zlock);
}

static void zs_status_handle(struct zs_port *zport, struct zs_port *zport_a)
{
	struct uart_port *uport = &zport->port;
	struct zs_scc *scc = zport->scc;
	unsigned int delta;
	u8 status, brk;

	spin_lock(&scc->zlock);

	/* Get status from Read Register 0.  */
	status = read_zsreg(zport, R0);

	if (zport->regs[15] & BRKIE) {
		brk = status & BRK_ABRT;
		if (brk && !zport->brk) {
			spin_unlock(&scc->zlock);
			if (uart_handle_break(uport))
				zport->tty_break = Rx_SYS;
			else
				zport->tty_break = Rx_BRK;
			spin_lock(&scc->zlock);
		}
		zport->brk = brk;
	}

	if (zport != zport_a) {
		delta = zs_raw_xor_mctrl(zport);
		spin_unlock(&scc->zlock);

		if (delta & TIOCM_CTS)
			uart_handle_cts_change(uport,
					       zport->mctrl & TIOCM_CTS);
		if (delta & TIOCM_CAR)
			uart_handle_dcd_change(uport,
					       zport->mctrl & TIOCM_CAR);
		if (delta & TIOCM_RNG)
			uport->icount.dsr++;
		if (delta & TIOCM_DSR)
			uport->icount.rng++;

		if (delta)
			wake_up_interruptible(&uport->state->port.delta_msr_wait);

		spin_lock(&scc->zlock);
	}

	/* Clear the status condition...  */
	write_zsreg(zport, R0, RES_EXT_INT);

	spin_unlock(&scc->zlock);
}

/*
 * This is the Z85C30 driver's generic interrupt routine.
 */
static irqreturn_t zs_interrupt(int irq, void *dev_id)
{
	struct zs_scc *scc = dev_id;
	struct zs_port *zport_a = &scc->zport[ZS_CHAN_A];
	struct zs_port *zport_b = &scc->zport[ZS_CHAN_B];
	irqreturn_t status = IRQ_NONE;
	u8 zs_intreg;
	int count;

	/*
	 * NOTE: The read register 3, which holds the irq status,
	 *       does so for both channels on each chip.  Although
	 *       the status value itself must be read from the A
	 *       channel and is only valid when read from channel A.
	 *       Yes... broken hardware...
	 */
	for (count = 16; count; count--) {
		spin_lock(&scc->zlock);
		zs_intreg = read_zsreg(zport_a, R3);
		spin_unlock(&scc->zlock);
		if (!zs_intreg)
			break;

		/*
		 * We do not like losing characters, so we prioritise
		 * interrupt sources a little bit differently than
		 * the SCC would, was it allowed to.
		 */
		if (zs_intreg & CHBRxIP)
			zs_receive_chars(zport_b);
		if (zs_intreg & CHARxIP)
			zs_receive_chars(zport_a);
		if (zs_intreg & CHBEXT)
			zs_status_handle(zport_b, zport_a);
		if (zs_intreg & CHAEXT)
			zs_status_handle(zport_a, zport_a);
		if (zs_intreg & CHBTxIP)
			zs_transmit_chars(zport_b);
		if (zs_intreg & CHATxIP)
			zs_transmit_chars(zport_a);

		status = IRQ_HANDLED;
	}

	return status;
}


/*
 * Finally, routines used to initialize the serial port.
 */
static int zs_startup(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	unsigned long flags;
	int irq_guard;
	int ret;

	irq_guard = atomic_add_return(1, &scc->irq_guard);
	if (irq_guard == 1) {
		ret = request_irq(zport->port.irq, zs_interrupt,
				  IRQF_SHARED, "scc", scc);
		if (ret) {
			atomic_add(-1, &scc->irq_guard);
			printk(KERN_ERR "zs: can't get irq %d\n",
			       zport->port.irq);
			return ret;
		}
	}

	spin_lock_irqsave(&scc->zlock, flags);

	/* Clear the receive FIFO.  */
	zs_receive_drain(zport);

	/* Clear the interrupt registers.  */
	write_zsreg(zport, R0, ERR_RES);
	write_zsreg(zport, R0, RES_Tx_P);
	/* But Ext only if not being handled already.  */
	if (!(zport->regs[1] & EXT_INT_ENAB))
		write_zsreg(zport, R0, RES_EXT_INT);

	/* Finally, enable sequencing and interrupts.  */
	zport->regs[1] &= ~RxINT_MASK;
	zport->regs[1] |= RxINT_ALL | TxINT_ENAB | EXT_INT_ENAB;
	zport->regs[3] |= RxENABLE;
	zport->regs[15] |= BRKIE;
	write_zsreg(zport, R1, zport->regs[1]);
	write_zsreg(zport, R3, zport->regs[3]);
	write_zsreg(zport, R5, zport->regs[5]);
	write_zsreg(zport, R15, zport->regs[15]);

	/* Record the current state of RR0.  */
	zport->mctrl = zs_raw_get_mctrl(zport);
	zport->brk = read_zsreg(zport, R0) & BRK_ABRT;

	zport->tx_stopped = 1;

	spin_unlock_irqrestore(&scc->zlock, flags);

	return 0;
}

static void zs_shutdown(struct uart_port *uport)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	unsigned long flags;
	int irq_guard;

	spin_lock_irqsave(&scc->zlock, flags);

	zport->regs[3] &= ~RxENABLE;
	write_zsreg(zport, R5, zport->regs[5]);
	write_zsreg(zport, R3, zport->regs[3]);

	spin_unlock_irqrestore(&scc->zlock, flags);

	irq_guard = atomic_add_return(-1, &scc->irq_guard);
	if (!irq_guard)
		free_irq(zport->port.irq, scc);
}


static void zs_reset(struct zs_port *zport)
{
	struct zs_scc *scc = zport->scc;
	int irq;
	unsigned long flags;

	spin_lock_irqsave(&scc->zlock, flags);
	irq = !irqs_disabled_flags(flags);
	if (!scc->initialised) {
		/* Reset the pointer first, just in case...  */
		read_zsreg(zport, R0);
		/* And let the current transmission finish.  */
		zs_line_drain(zport, irq);
		write_zsreg(zport, R9, FHWRES);
		udelay(10);
		write_zsreg(zport, R9, 0);
		scc->initialised = 1;
	}
	load_zsregs(zport, zport->regs, irq);
	spin_unlock_irqrestore(&scc->zlock, flags);
}

static void zs_set_termios(struct uart_port *uport, struct ktermios *termios,
			   const struct ktermios *old_termios)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	struct zs_port *zport_a = &scc->zport[ZS_CHAN_A];
	int irq;
	unsigned int baud, brg;
	unsigned long flags;

	spin_lock_irqsave(&scc->zlock, flags);
	irq = !irqs_disabled_flags(flags);

	/* Byte size.  */
	zport->regs[3] &= ~RxNBITS_MASK;
	zport->regs[5] &= ~TxNBITS_MASK;
	switch (termios->c_cflag & CSIZE) {
	case CS5:
		zport->regs[3] |= Rx5;
		zport->regs[5] |= Tx5;
		break;
	case CS6:
		zport->regs[3] |= Rx6;
		zport->regs[5] |= Tx6;
		break;
	case CS7:
		zport->regs[3] |= Rx7;
		zport->regs[5] |= Tx7;
		break;
	case CS8:
	default:
		zport->regs[3] |= Rx8;
		zport->regs[5] |= Tx8;
		break;
	}

	/* Parity and stop bits.  */
	zport->regs[4] &= ~(XCLK_MASK | SB_MASK | PAR_ENA | PAR_EVEN);
	if (termios->c_cflag & CSTOPB)
		zport->regs[4] |= SB2;
	else
		zport->regs[4] |= SB1;
	if (termios->c_cflag & PARENB)
		zport->regs[4] |= PAR_ENA;
	if (!(termios->c_cflag & PARODD))
		zport->regs[4] |= PAR_EVEN;
	switch (zport->clk_mode) {
	case 64:
		zport->regs[4] |= X64CLK;
		break;
	case 32:
		zport->regs[4] |= X32CLK;
		break;
	case 16:
		zport->regs[4] |= X16CLK;
		break;
	case 1:
		zport->regs[4] |= X1CLK;
		break;
	default:
		BUG();
	}

	baud = uart_get_baud_rate(uport, termios, old_termios, 0,
				  uport->uartclk / zport->clk_mode / 4);

	brg = ZS_BPS_TO_BRG(baud, uport->uartclk / zport->clk_mode);
	zport->regs[12] = brg & 0xff;
	zport->regs[13] = (brg >> 8) & 0xff;

	uart_update_timeout(uport, termios->c_cflag, baud);

	uport->read_status_mask = Rx_OVR;
	if (termios->c_iflag & INPCK)
		uport->read_status_mask |= FRM_ERR | PAR_ERR;
	if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
		uport->read_status_mask |= Rx_BRK;

	uport->ignore_status_mask = 0;
	if (termios->c_iflag & IGNPAR)
		uport->ignore_status_mask |= FRM_ERR | PAR_ERR;
	if (termios->c_iflag & IGNBRK) {
		uport->ignore_status_mask |= Rx_BRK;
		if (termios->c_iflag & IGNPAR)
			uport->ignore_status_mask |= Rx_OVR;
	}

	if (termios->c_cflag & CREAD)
		zport->regs[3] |= RxENABLE;
	else
		zport->regs[3] &= ~RxENABLE;

	if (zport != zport_a) {
		if (!(termios->c_cflag & CLOCAL)) {
			zport->regs[15] |= DCDIE;
		} else
			zport->regs[15] &= ~DCDIE;
		if (termios->c_cflag & CRTSCTS) {
			zport->regs[15] |= CTSIE;
		} else
			zport->regs[15] &= ~CTSIE;
		zs_raw_xor_mctrl(zport);
	}

	/* Load up the new values.  */
	load_zsregs(zport, zport->regs, irq);

	spin_unlock_irqrestore(&scc->zlock, flags);
}

/*
 * Hack alert!
 * Required solely so that the initial PROM-based console
 * works undisturbed in parallel with this one.
 */
static void zs_pm(struct uart_port *uport, unsigned int state,
		  unsigned int oldstate)
{
	struct zs_port *zport = to_zport(uport);

	if (state < 3)
		zport->regs[5] |= TxENAB;
	else
		zport->regs[5] &= ~TxENAB;
	write_zsreg(zport, R5, zport->regs[5]);
}


static const char *zs_type(struct uart_port *uport)
{
	return "Z85C30 SCC";
}

static void zs_release_port(struct uart_port *uport)
{
	iounmap(uport->membase);
	uport->membase = NULL;
	release_mem_region(uport->mapbase, ZS_CHAN_IO_SIZE);
}

static int zs_map_port(struct uart_port *uport)
{
	if (!uport->membase)
		uport->membase = ioremap(uport->mapbase,
						 ZS_CHAN_IO_SIZE);
	if (!uport->membase) {
		printk(KERN_ERR "zs: Cannot map MMIO\n");
		return -ENOMEM;
	}
	return 0;
}

static int zs_request_port(struct uart_port *uport)
{
	int ret;

	if (!request_mem_region(uport->mapbase, ZS_CHAN_IO_SIZE, "scc")) {
		printk(KERN_ERR "zs: Unable to reserve MMIO resource\n");
		return -EBUSY;
	}
	ret = zs_map_port(uport);
	if (ret) {
		release_mem_region(uport->mapbase, ZS_CHAN_IO_SIZE);
		return ret;
	}
	return 0;
}

static void zs_config_port(struct uart_port *uport, int flags)
{
	struct zs_port *zport = to_zport(uport);

	if (flags & UART_CONFIG_TYPE) {
		if (zs_request_port(uport))
			return;

		uport->type = PORT_ZS;

		zs_reset(zport);
	}
}

static int zs_verify_port(struct uart_port *uport, struct serial_struct *ser)
{
	struct zs_port *zport = to_zport(uport);
	int ret = 0;

	if (ser->type != PORT_UNKNOWN && ser->type != PORT_ZS)
		ret = -EINVAL;
	if (ser->irq != uport->irq)
		ret = -EINVAL;
	if (ser->baud_base != uport->uartclk / zport->clk_mode / 4)
		ret = -EINVAL;
	return ret;
}


static const struct uart_ops zs_ops = {
	.tx_empty	= zs_tx_empty,
	.set_mctrl	= zs_set_mctrl,
	.get_mctrl	= zs_get_mctrl,
	.stop_tx	= zs_stop_tx,
	.start_tx	= zs_start_tx,
	.stop_rx	= zs_stop_rx,
	.enable_ms	= zs_enable_ms,
	.break_ctl	= zs_break_ctl,
	.startup	= zs_startup,
	.shutdown	= zs_shutdown,
	.set_termios	= zs_set_termios,
	.pm		= zs_pm,
	.type		= zs_type,
	.release_port	= zs_release_port,
	.request_port	= zs_request_port,
	.config_port	= zs_config_port,
	.verify_port	= zs_verify_port,
};

/*
 * Initialize Z85C30 port structures.
 */
static int __init zs_probe_sccs(void)
{
	static int probed;
	struct zs_parms zs_parms;
	int chip, side, irq;
	int n_chips = 0;
	int i;

	if (probed)
		return 0;

	irq = dec_interrupt[DEC_IRQ_SCC0];
	if (irq >= 0) {
		zs_parms.scc[n_chips] = IOASIC_SCC0;
		zs_parms.irq[n_chips] = dec_interrupt[DEC_IRQ_SCC0];
		n_chips++;
	}
	irq = dec_interrupt[DEC_IRQ_SCC1];
	if (irq >= 0) {
		zs_parms.scc[n_chips] = IOASIC_SCC1;
		zs_parms.irq[n_chips] = dec_interrupt[DEC_IRQ_SCC1];
		n_chips++;
	}
	if (!n_chips)
		return -ENXIO;

	probed = 1;

	for (chip = 0; chip < n_chips; chip++) {
		spin_lock_init(&zs_sccs[chip].zlock);
		for (side = 0; side < ZS_NUM_CHAN; side++) {
			struct zs_port *zport = &zs_sccs[chip].zport[side];
			struct uart_port *uport = &zport->port;

			zport->scc	= &zs_sccs[chip];
			zport->clk_mode	= 16;

			uport->has_sysrq = IS_ENABLED(CONFIG_SERIAL_ZS_CONSOLE);
			uport->irq	= zs_parms.irq[chip];
			uport->uartclk	= ZS_CLOCK;
			uport->fifosize	= 1;
			uport->iotype	= UPIO_MEM;
			uport->flags	= UPF_BOOT_AUTOCONF;
			uport->ops	= &zs_ops;
			uport->line	= chip * ZS_NUM_CHAN + side;
			uport->mapbase	= dec_kn_slot_base +
					  zs_parms.scc[chip] +
					  (side ^ ZS_CHAN_B) * ZS_CHAN_IO_SIZE;

			for (i = 0; i < ZS_NUM_REGS; i++)
				zport->regs[i] = zs_init_regs[i];
		}
	}

	return 0;
}


#ifdef CONFIG_SERIAL_ZS_CONSOLE
static void zs_console_putchar(struct uart_port *uport, unsigned char ch)
{
	struct zs_port *zport = to_zport(uport);
	struct zs_scc *scc = zport->scc;
	int irq;
	unsigned long flags;

	spin_lock_irqsave(&scc->zlock, flags);
	irq = !irqs_disabled_flags(flags);
	if (zs_transmit_drain(zport, irq))
		write_zsdata(zport, ch);
	spin_unlock_irqrestore(&scc->zlock, flags);
}

/*
 * Print a string to the serial port trying not to disturb
 * any possible real use of the port...
 */
static void zs_console_write(struct console *co, const char *s,
			     unsigned int count)
{
	int chip = co->index / ZS_NUM_CHAN, side = co->index % ZS_NUM_CHAN;
	struct zs_port *zport = &zs_sccs[chip].zport[side];
	struct zs_scc *scc = zport->scc;
	unsigned long flags;
	u8 txint, txenb;
	int irq;

	/* Disable transmit interrupts and enable the transmitter. */
	spin_lock_irqsave(&scc->zlock, flags);
	txint = zport->regs[1];
	txenb = zport->regs[5];
	if (txint & TxINT_ENAB) {
		zport->regs[1] = txint & ~TxINT_ENAB;
		write_zsreg(zport, R1, zport->regs[1]);
	}
	if (!(txenb & TxENAB)) {
		zport->regs[5] = txenb | TxENAB;
		write_zsreg(zport, R5, zport->regs[5]);
	}
	spin_unlock_irqrestore(&scc->zlock, flags);

	uart_console_write(&zport->port, s, count, zs_console_putchar);

	/* Restore transmit interrupts and the transmitter enable. */
	spin_lock_irqsave(&scc->zlock, flags);
	irq = !irqs_disabled_flags(flags);
	zs_line_drain(zport, irq);
	if (!(txenb & TxENAB)) {
		zport->regs[5] &= ~TxENAB;
		write_zsreg(zport, R5, zport->regs[5]);
	}
	if (txint & TxINT_ENAB) {
		zport->regs[1] |= TxINT_ENAB;
		write_zsreg(zport, R1, zport->regs[1]);

		/* Resume any transmission as the TxIP bit won't be set.  */
		if (!zport->tx_stopped)
			zs_raw_transmit_chars(zport);
	}
	spin_unlock_irqrestore(&scc->zlock, flags);
}

/*
 * Setup serial console baud/bits/parity.  We do two things here:
 * - construct a cflag setting for the first uart_open()
 * - initialise the serial port
 * Return non-zero if we didn't find a serial port.
 */
static int __init zs_console_setup(struct console *co, char *options)
{
	int chip = co->index / ZS_NUM_CHAN, side = co->index % ZS_NUM_CHAN;
	struct zs_port *zport = &zs_sccs[chip].zport[side];
	struct uart_port *uport = &zport->port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';
	int ret;

	ret = zs_map_port(uport);
	if (ret)
		return ret;

	zs_reset(zport);
	zs_pm(uport, 0, -1);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	return uart_set_options(uport, co, baud, parity, bits, flow);
}

static struct uart_driver zs_reg;
static struct console zs_console = {
	.name	= "ttyS",
	.write	= zs_console_write,
	.device	= uart_console_device,
	.setup	= zs_console_setup,
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
	.data	= &zs_reg,
};

/*
 *	Register console.
 */
static int __init zs_serial_console_init(void)
{
	int ret;

	ret = zs_probe_sccs();
	if (ret)
		return ret;
	register_console(&zs_console);

	return 0;
}

console_initcall(zs_serial_console_init);

#define SERIAL_ZS_CONSOLE	&zs_console
#else
#define SERIAL_ZS_CONSOLE	NULL
#endif /* CONFIG_SERIAL_ZS_CONSOLE */

static struct uart_driver zs_reg = {
	.owner			= THIS_MODULE,
	.driver_name		= "serial",
	.dev_name		= "ttyS",
	.major			= TTY_MAJOR,
	.minor			= 64,
	.nr			= ZS_NUM_SCCS * ZS_NUM_CHAN,
	.cons			= SERIAL_ZS_CONSOLE,
};

/* zs_init inits the driver. */
static int __init zs_init(void)
{
	int i, ret;

	pr_info("%s%s\n", zs_name, zs_version);

	/* Find out how many Z85C30 SCCs we have.  */
	ret = zs_probe_sccs();
	if (ret)
		return ret;

	ret = uart_register_driver(&zs_reg);
	if (ret)
		return ret;

	for (i = 0; i < ZS_NUM_SCCS * ZS_NUM_CHAN; i++) {
		struct zs_scc *scc = &zs_sccs[i / ZS_NUM_CHAN];
		struct zs_port *zport = &scc->zport[i % ZS_NUM_CHAN];
		struct uart_port *uport = &zport->port;

		if (zport->scc)
			uart_add_one_port(&zs_reg, uport);
	}

	return 0;
}

static void __exit zs_exit(void)
{
	int i;

	for (i = ZS_NUM_SCCS * ZS_NUM_CHAN - 1; i >= 0; i--) {
		struct zs_scc *scc = &zs_sccs[i / ZS_NUM_CHAN];
		struct zs_port *zport = &scc->zport[i % ZS_NUM_CHAN];
		struct uart_port *uport = &zport->port;

		if (zport->scc)
			uart_remove_one_port(&zs_reg, uport);
	}

	uart_unregister_driver(&zs_reg);
}

module_init(zs_init);
module_exit(zs_exit);
