/*
 *  arch/ppc/4xx_io/serial_sicc.c
 *
 *  Driver for IBM STB3xxx SICC serial port
 *
 *  Based on drivers/char/serial_amba.c, by ARM Ltd.
 *
 *  Copyright 2001 IBM Crop.
 *  Author: IBM China Research Lab
 *            Yudong Yang <yangyud@cn.ibm.com>
 *            Yi Ge       <geyi@cn.ibm.com>
 *
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
 *
 *
 * This is a driver for SICC serial port on IBM Redwood 4 evaluation board.
 * The driver support both as a console device and normal serial device and
 * is compatible with normal ttyS* devices.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/serial.h>


#include <linux/serialP.h>


/* -----------------------------------------------------------------------------
 *  From STB03xxx SICC UART Specification
 * -----------------------------------------------------------------------------
 *  UART Register Offsets.
 */

#define BL_SICC_LSR   0x0000000      /* line status register read/clear */
#define BL_SICC_LSRS  0x0000001      /* set line status register read/set */
#define BL_SICC_HSR   0x0000002      /* handshake status register r/clear */
#define BL_SICC_HSRS  0x0000003      /* set handshake status register r/set */
#define BL_SICC_BRDH  0x0000004      /* baudrate divisor high reg r/w */
#define BL_SICC_BRDL  0x0000005      /* baudrate divisor low reg r/w */
#define BL_SICC_LCR   0x0000006      /* control register r/w */
#define BL_SICC_RCR   0x0000007      /* receiver command register r/w */
#define BL_SICC_TxCR  0x0000008      /* transmitter command register r/w */
#define BL_SICC_RBR   0x0000009      /* receive buffer r */
#define BL_SICC_TBR   0x0000009      /* transmit buffer w */
#define BL_SICC_CTL2  0x000000A      /* added for Vesta */
#define BL_SICC_IrCR  0x000000B      /* added for Vesta IR */

/* masks and definitions for serial port control register */

#define _LCR_LM_MASK  0xc0            /* loop back modes */
#define _LCR_DTR_MASK 0x20            /* data terminal ready 0-inactive */
#define _LCR_RTS_MASK 0x10            /* request to send 0-inactive */
#define _LCR_DB_MASK  0x08            /* data bits mask */
#define _LCR_PE_MASK  0x04            /* parity enable */
#define _LCR_PTY_MASK 0x02            /* parity */
#define _LCR_SB_MASK  0x01            /* stop bit mask */

#define _LCR_LM_NORM  0x00            /* normal operation */
#define _LCR_LM_LOOP  0x40            /* internal loopback mode */
#define _LCR_LM_ECHO  0x80            /* automatic echo mode */
#define _LCR_LM_RES   0xc0            /* reserved */

#define _LCR_DTR_ACTIVE       _LCR_DTR_MASK /* DTR is active */
#define _LCR_RTS_ACTIVE       _LCR_RTS_MASK /* RTS is active */
#define _LCR_DB_8_BITS        _LCR_DB_MASK  /*  8 data bits */
#define _LCR_DB_7_BITS        0x00          /*  7 data bits */
#define _LCR_PE_ENABLE        _LCR_PE_MASK  /* parity enabled */
#define _LCR_PE_DISABLE       0x00          /* parity disabled */
#define _LCR_PTY_EVEN         0x00          /* even parity */
#define _LCR_PTY_ODD          _LCR_PTY_MASK /* odd parity */
#define _LCR_SB_1_BIT         0x00          /* one stop bit */
#define _LCR_SB_2_BIT         _LCR_SB_MASK  /* two stop bit */

/* serial port handshake register */

#define _HSR_DIS_MASK  0x80            /* DSR input inactive error mask */
#define _HSR_CS_MASK   0x40            /* CTS input inactive error mask */
#define _HSR_DIS_ACT   0x00            /* dsr input is active */
#define _HSR_DIS_INACT _HSR_DIS_MASK   /* dsr input is inactive */
#define _HSR_CS_ACT    0x00            /* cts input is active */
#define _HSR_CS_INACT  _HSR_CS_MASK    /* cts input is active */

/* serial port line status register */

#define _LSR_RBR_MASK  0x80            /* receive buffer ready mask */
#define _LSR_FE_MASK   0x40            /* framing error */
#define _LSR_OE_MASK   0x20            /* overrun error */
#define _LSR_PE_MASK   0x10            /* parity error */
#define _LSR_LB_MASK   0x08            /* line break */
#define _LSR_TBR_MASK  0x04            /* transmit buffer ready */
#define _LSR_TSR_MASK  0x02            /* transmit shift register ready */

#define _LSR_RBR_FULL  _LSR_RBR_MASK  /* receive buffer is full */
#define _LSR_FE_ERROR  _LSR_FE_MASK   /* framing error detected */
#define _LSR_OE_ERROR  _LSR_OE_MASK   /* overrun error detected */
#define _LSR_PE_ERROR  _LSR_PE_MASK   /* parity error detected */
#define _LSR_LB_BREAK  _LSR_LB_MASK   /* line break detected */
#define _LSR_TBR_EMPTY _LSR_TBR_MASK  /* transmit buffer is ready */
#define _LSR_TSR_EMPTY _LSR_TSR_MASK  /* transmit shift register is empty */
#define _LSR_TX_ALL    0x06           /* all physical transmit is done */

#define _LSR_RX_ERR    (_LSR_LB_BREAK | _LSR_FE_MASK | _LSR_OE_MASK | \
			 _LSR_PE_MASK )

/* serial port receiver command register */

#define _RCR_ER_MASK   0x80           /* enable receiver mask */
#define _RCR_DME_MASK  0x60           /* dma mode */
#define _RCR_EIE_MASK  0x10           /* error interrupt enable mask */
#define _RCR_PME_MASK  0x08           /* pause mode mask */

#define _RCR_ER_ENABLE _RCR_ER_MASK   /* receiver enabled */
#define _RCR_DME_DISABLE 0x00         /* dma disabled */
#define _RCR_DME_RXRDY 0x20           /* dma disabled, RxRDY interrupt enabled*/
#define _RCR_DME_ENABLE2 0x40         /* dma enabled,receiver src channel 2 */
#define _RCR_DME_ENABLE3 0x60         /* dma enabled,receiver src channel 3 */
#define _RCR_PME_HARD  _RCR_PME_MASK  /* RTS controlled by hardware */
#define _RCR_PME_SOFT  0x00           /* RTS controlled by software */

/* serial port transmit command register */

#define _TxCR_ET_MASK   0x80           /* transmiter enable mask */
#define _TxCR_DME_MASK  0x60           /* dma mode mask */
#define _TxCR_TIE_MASK  0x10           /* empty interrupt enable mask */
#define _TxCR_EIE_MASK  0x08           /* error interrupt enable mask */
#define _TxCR_SPE_MASK  0x04           /* stop/pause mask */
#define _TxCR_TB_MASK   0x02           /* transmit break mask */

#define _TxCR_ET_ENABLE _TxCR_ET_MASK  /* transmiter enabled */
#define _TxCR_DME_DISABLE 0x00         /* transmiter disabled, TBR intr disabled */
#define _TxCR_DME_TBR   0x20           /* transmiter disabled, TBR intr enabled */
#define _TxCR_DME_CHAN_2 0x40          /* dma enabled, destination chann 2 */
#define _TxCR_DME_CHAN_3 0x60          /* dma enabled, destination chann 3 */

/* serial ctl reg 2 - added for Vesta */

#define _CTL2_EXTERN  0x80            /*  */
#define _CTL2_USEFIFO 0x40            /*  */
#define _CTL2_RESETRF 0x08            /*  */
#define _CTL2_RESETTF 0x04            /*  */



#define SERIAL_SICC_NAME    "ttySICC"
#define SERIAL_SICC_MAJOR   150
#define SERIAL_SICC_MINOR   1
#define SERIAL_SICC_NR      1

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/*
 * Things needed by tty driver
 */
static struct tty_driver *siccnormal_driver;

#if defined(CONFIG_SERIAL_SICC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

/*
 * Things needed internally to this driver
 */

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the copy_from_user blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static u_char *tmp_buf;
static DECLARE_MUTEX(tmp_buf_sem);

#define HIGH_BITS_OFFSET    ((sizeof(long)-sizeof(int))*8)

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS        256
#define SICC_ISR_PASS_LIMIT 256

#define EVT_WRITE_WAKEUP    0

struct SICC_icount {
    __u32   cts;
    __u32   dsr;
    __u32   rng;
    __u32   dcd;
    __u32   rx;
    __u32   tx;
    __u32   frame;
    __u32   overrun;
    __u32   parity;
    __u32   brk;
    __u32   buf_overrun;
};

/*
 * Static information about the port
 */
struct SICC_port {
    unsigned int        uart_base;
    unsigned int        uart_base_phys;
    unsigned int        irqrx;
    unsigned int        irqtx;
    unsigned int        uartclk;
    unsigned int        fifosize;
    unsigned int        tiocm_support;
    void (*set_mctrl)(struct SICC_port *, u_int mctrl);
};

/*
 * This is the state information which is persistent across opens
 */
struct SICC_state {
    struct SICC_icount  icount;
    unsigned int        line;
    unsigned int        close_delay;
    unsigned int        closing_wait;
    unsigned int        custom_divisor;
    unsigned int        flags;
    int         count;
    struct SICC_info    *info;
    spinlock_t		sicc_lock;
};

#define SICC_XMIT_SIZE 1024
/*
 * This is the state information which is only valid when the port is open.
 */
struct SICC_info {
    struct SICC_port    *port;
    struct SICC_state   *state;
    struct tty_struct   *tty;
    unsigned char       x_char;
    unsigned char       old_status;
    unsigned char       read_status_mask;
    unsigned char       ignore_status_mask;
    struct circ_buf     xmit;
    unsigned int        flags;
#ifdef SUPPORT_SYSRQ
    unsigned long       sysrq;
#endif

    unsigned int        event;
    unsigned int        timeout;
    unsigned int        lcr_h;
    unsigned int        mctrl;
    int         blocked_open;

    struct tasklet_struct   tlet;

    wait_queue_head_t   open_wait;
    wait_queue_head_t   close_wait;
    wait_queue_head_t   delta_msr_wait;
};

#ifdef CONFIG_SERIAL_SICC_CONSOLE
static struct console siccuart_cons;
#endif
static void siccuart_change_speed(struct SICC_info *info, struct termios *old_termios);
static void siccuart_wait_until_sent(struct tty_struct *tty, int timeout);



static void powerpcMtcic_cr(unsigned long value)
{
    mtdcr(DCRN_CICCR, value);
}

static unsigned long powerpcMfcic_cr(void)
{
    return mfdcr(DCRN_CICCR);
}

static unsigned long powerpcMfclkgpcr(void)
{
    return mfdcr(DCRN_SCCR);
}

static void sicc_set_mctrl_null(struct SICC_port *port, u_int mctrl)
{
}

static struct SICC_port sicc_ports[SERIAL_SICC_NR] = {
    {
        .uart_base = 0,
        .uart_base_phys = SICC0_IO_BASE,
        .irqrx =    SICC0_INTRX,
        .irqtx =    SICC0_INTTX,
//      .uartclk =    0,
        .fifosize = 1,
        .set_mctrl = sicc_set_mctrl_null,
    }
};

static struct SICC_state sicc_state[SERIAL_SICC_NR];

static void siccuart_enable_rx_interrupt(struct SICC_info *info)
{
    unsigned char cr;

    cr = readb(info->port->uart_base+BL_SICC_RCR);
    cr &= ~_RCR_DME_MASK;
    cr |= _RCR_DME_RXRDY;
    writeb(cr, info->port->uart_base+BL_SICC_RCR);
}

static void siccuart_disable_rx_interrupt(struct SICC_info *info)
{
    unsigned char cr;

    cr = readb(info->port->uart_base+BL_SICC_RCR);
    cr &= ~_RCR_DME_MASK;
    cr |=  _RCR_DME_DISABLE;
    writeb(cr, info->port->uart_base+BL_SICC_RCR);
}


static void siccuart_enable_tx_interrupt(struct SICC_info *info)
{
    unsigned char cr;

    cr = readb(info->port->uart_base+BL_SICC_TxCR);
    cr &= ~_TxCR_DME_MASK;
    cr |= _TxCR_DME_TBR;
    writeb(cr, info->port->uart_base+BL_SICC_TxCR);
}

static void siccuart_disable_tx_interrupt(struct SICC_info *info)
{
    unsigned char cr;

    cr = readb(info->port->uart_base+BL_SICC_TxCR);
    cr &= ~_TxCR_DME_MASK;
    cr |=  _TxCR_DME_DISABLE;
    writeb(cr, info->port->uart_base+BL_SICC_TxCR);
}


static void siccuart_stop(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    /* disable interrupts while stopping serial port interrupts */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    siccuart_disable_tx_interrupt(info);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}

static void siccuart_start(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    /* disable interrupts while starting serial port interrupts */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    if (info->xmit.head != info->xmit.tail
        && info->xmit.buf)
        siccuart_enable_tx_interrupt(info);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}


/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static void siccuart_event(struct SICC_info *info, int event)
{
    info->event |= 1 << event;
    tasklet_schedule(&info->tlet);
}

static void
siccuart_rx_chars(struct SICC_info *info, struct pt_regs *regs)
{
    struct tty_struct *tty = info->tty;
    unsigned int status, ch, rsr, flg, ignored = 0;
    struct SICC_icount *icount = &info->state->icount;
    struct SICC_port *port = info->port;

    status = readb(port->uart_base+BL_SICC_LSR );
    while (status & _LSR_RBR_FULL) {
        ch = readb(port->uart_base+BL_SICC_RBR);

        if (tty->flip.count >= TTY_FLIPBUF_SIZE)
            goto ignore_char;
        icount->rx++;

        flg = TTY_NORMAL;

        /*
         * Note that the error handling code is
         * out of the main execution path
         */
        rsr = readb(port->uart_base+BL_SICC_LSR);
        if (rsr & _LSR_RX_ERR)
            goto handle_error;
#ifdef SUPPORT_SYSRQ
        if (info->sysrq) {
            if (ch && time_before(jiffies, info->sysrq)) {
                handle_sysrq(ch, regs, NULL);
                info->sysrq = 0;
                goto ignore_char;
            }
            info->sysrq = 0;
        }
#endif
    error_return:
        *tty->flip.flag_buf_ptr++ = flg;
        *tty->flip.char_buf_ptr++ = ch;
        tty->flip.count++;
    ignore_char:
        status = readb(port->uart_base+BL_SICC_LSR );
    }
out:
    tty_flip_buffer_push(tty);
    return;

handle_error:
    if (rsr & _LSR_LB_BREAK) {
        rsr &= ~(_LSR_FE_MASK | _LSR_PE_MASK);
        icount->brk++;

#ifdef SUPPORT_SYSRQ
        if (info->state->line == siccuart_cons.index) {
            if (!info->sysrq) {
                info->sysrq = jiffies + HZ*5;
                goto ignore_char;
            }
        }
#endif
    } else if (rsr & _LSR_PE_MASK)
        icount->parity++;
    else if (rsr & _LSR_FE_MASK)
        icount->frame++;
    if (rsr & _LSR_OE_MASK)
        icount->overrun++;

    if (rsr & info->ignore_status_mask) {
        if (++ignored > 100)
            goto out;
        goto ignore_char;
    }
    rsr &= info->read_status_mask;

    if (rsr & _LSR_LB_BREAK)
        flg = TTY_BREAK;
    else if (rsr &  _LSR_PE_MASK)
        flg = TTY_PARITY;
    else if (rsr &  _LSR_FE_MASK)
        flg = TTY_FRAME;

    if (rsr &  _LSR_OE_MASK) {
        /*
         * CHECK: does overrun affect the current character?
         * ASSUMPTION: it does not.
         */
        *tty->flip.flag_buf_ptr++ = flg;
        *tty->flip.char_buf_ptr++ = ch;
        tty->flip.count++;
        if (tty->flip.count >= TTY_FLIPBUF_SIZE)
            goto ignore_char;
        ch = 0;
        flg = TTY_OVERRUN;
    }
#ifdef SUPPORT_SYSRQ
    info->sysrq = 0;
#endif
    goto error_return;
}

static void siccuart_tx_chars(struct SICC_info *info)
{
    struct SICC_port *port = info->port;
    int count;
        unsigned char status;


    if (info->x_char) {
        writeb(info->x_char, port->uart_base+ BL_SICC_TBR);
        info->state->icount.tx++;
        info->x_char = 0;
        return;
    }
    if (info->xmit.head == info->xmit.tail
        || info->tty->stopped
        || info->tty->hw_stopped) {
        siccuart_disable_tx_interrupt(info);
                writeb(status&(~_LSR_RBR_MASK),port->uart_base+BL_SICC_LSR);
        return;
    }

    count = port->fifosize;
    do {
        writeb(info->xmit.buf[info->xmit.tail], port->uart_base+ BL_SICC_TBR);
        info->xmit.tail = (info->xmit.tail + 1) & (SICC_XMIT_SIZE - 1);
        info->state->icount.tx++;
        if (info->xmit.head == info->xmit.tail)
            break;
    } while (--count > 0);

    if (CIRC_CNT(info->xmit.head,
             info->xmit.tail,
             SICC_XMIT_SIZE) < WAKEUP_CHARS)
        siccuart_event(info, EVT_WRITE_WAKEUP);

    if (info->xmit.head == info->xmit.tail) {
        siccuart_disable_tx_interrupt(info);
    }
}


static irqreturn_t siccuart_int_rx(int irq, void *dev_id, struct pt_regs *regs)
{
    struct SICC_info *info = dev_id;
    siccuart_rx_chars(info, regs);
    return IRQ_HANDLED;
}


static irqreturn_t siccuart_int_tx(int irq, void *dev_id, struct pt_regs *regs)
{
    struct SICC_info *info = dev_id;
    siccuart_tx_chars(info);
    return IRQ_HANDLED;
}

static void siccuart_tasklet_action(unsigned long data)
{
    struct SICC_info *info = (struct SICC_info *)data;
    struct tty_struct *tty;

    tty = info->tty;
    if (!tty || !test_and_clear_bit(EVT_WRITE_WAKEUP, &info->event))
        return;

    if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
        tty->ldisc.write_wakeup)
        (tty->ldisc.write_wakeup)(tty);
    wake_up_interruptible(&tty->write_wait);
}

static int siccuart_startup(struct SICC_info *info)
{
    unsigned long flags;
    unsigned long page;
    int retval = 0;

    if (info->flags & ASYNC_INITIALIZED) {
        return 0;
    }

    page = get_zeroed_page(GFP_KERNEL);
    if (!page)
        return -ENOMEM;

    if (info->port->uart_base == 0)
	info->port->uart_base = (int)ioremap(info->port->uart_base_phys, PAGE_SIZE);
    if (info->port->uart_base == 0) {
	free_page(page);
	return -ENOMEM;
    }

    /* lock access to info while doing setup */
    spin_lock_irqsave(&info->state->sicc_lock,flags);

    if (info->xmit.buf)
        free_page(page);
    else
        info->xmit.buf = (unsigned char *) page;


    info->mctrl = 0;
    if (info->tty->termios->c_cflag & CBAUD)
        info->mctrl = TIOCM_RTS | TIOCM_DTR;
    info->port->set_mctrl(info->port, info->mctrl);

    /*
     * initialise the old status of the modem signals
     */
    info->old_status = 0; // UART_GET_FR(info->port) & AMBA_UARTFR_MODEM_ANY;


    if (info->tty)
        clear_bit(TTY_IO_ERROR, &info->tty->flags);
    info->xmit.head = info->xmit.tail = 0;

    /*
     * Set up the tty->alt_speed kludge
     */
    if (info->tty) {
        if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
            info->tty->alt_speed = 57600;
        if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
            info->tty->alt_speed = 115200;
        if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
            info->tty->alt_speed = 230400;
        if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
            info->tty->alt_speed = 460800;
    }


    writeb( 0x00, info->port->uart_base + BL_SICC_IrCR );  // disable IrDA


    /*
     * and set the speed of the serial port
     */
    siccuart_change_speed(info, 0);

    // enable rx/tx ports
    writeb(_RCR_ER_ENABLE /*| _RCR_PME_HARD*/, info->port->uart_base + BL_SICC_RCR);
    writeb(_TxCR_ET_ENABLE               , info->port->uart_base + BL_SICC_TxCR);

    readb(info->port->uart_base + BL_SICC_RBR); // clear rx port

    writeb(0xf8, info->port->uart_base + BL_SICC_LSR);   /* reset bits 0-4 of LSR */

    /*
     * Finally, enable interrupts
     */

     /*
     * Allocate the IRQ
     */
        retval = request_irq(info->port->irqrx, siccuart_int_rx, 0, "SICC rx", info);
        if (retval) {
             if (capable(CAP_SYS_ADMIN)) {
                   if (info->tty)
                          set_bit(TTY_IO_ERROR, &info->tty->flags);
                   retval = 0;
             }
              goto errout;
         }
    retval = request_irq(info->port->irqtx, siccuart_int_tx, 0, "SICC tx", info);
    if (retval) {
        if (capable(CAP_SYS_ADMIN)) {
            if (info->tty)
                set_bit(TTY_IO_ERROR, &info->tty->flags);
            retval = 0;
        }
        free_irq(info->port->irqrx, info);
        goto errout;
    }

    siccuart_enable_rx_interrupt(info);

    info->flags |= ASYNC_INITIALIZED;
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    return 0;


errout:
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void siccuart_shutdown(struct SICC_info *info)
{
    unsigned long flags;

    if (!(info->flags & ASYNC_INITIALIZED))
        return;

    /* lock while shutting down port */
    spin_lock_irqsave(&info->state->sicc_lock,flags); /* Disable interrupts */

    /*
     * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
     * here so the queue might never be woken up
     */
    wake_up_interruptible(&info->delta_msr_wait);

    /*
     * disable all interrupts, disable the port
     */
    siccuart_disable_rx_interrupt(info);
    siccuart_disable_tx_interrupt(info);

    /*
     * Free the IRQ
     */
    free_irq(info->port->irqtx, info);
    free_irq(info->port->irqrx, info);

    if (info->xmit.buf) {
        unsigned long pg = (unsigned long) info->xmit.buf;
        info->xmit.buf = NULL;
        free_page(pg);
    }


    if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
        info->mctrl &= ~(TIOCM_DTR|TIOCM_RTS);
    info->port->set_mctrl(info->port, info->mctrl);

    /* kill off our tasklet */
    tasklet_kill(&info->tlet);
    if (info->tty)
        set_bit(TTY_IO_ERROR, &info->tty->flags);

    info->flags &= ~ASYNC_INITIALIZED;

    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}


static void siccuart_change_speed(struct SICC_info *info, struct termios *old_termios)
{
    unsigned int lcr_h, baud, quot, cflag, old_rcr, old_tcr, bits;
    unsigned long flags;

    if (!info->tty || !info->tty->termios)
        return;

    cflag = info->tty->termios->c_cflag;

    pr_debug("siccuart_set_cflag(0x%x) called\n", cflag);
    /* byte size and parity */
    switch (cflag & CSIZE) {
    case CS7: lcr_h =   _LCR_PE_DISABLE | _LCR_DB_7_BITS | _LCR_SB_1_BIT; bits = 9;  break;
    default:  lcr_h =   _LCR_PE_DISABLE | _LCR_DB_8_BITS | _LCR_SB_1_BIT; bits = 10; break; // CS8
    }
    if (cflag & CSTOPB) {
        lcr_h |= _LCR_SB_2_BIT;
        bits ++;
    }
    if (cflag & PARENB) {
        lcr_h |=  _LCR_PE_ENABLE;
        bits++;
        if (!(cflag & PARODD))
            lcr_h |=  _LCR_PTY_ODD;
        else
            lcr_h |=  _LCR_PTY_EVEN;
    }

    do {
        /* Determine divisor based on baud rate */
        baud = tty_get_baud_rate(info->tty);
        if (!baud)
            baud = 9600;


        {
           // here is ppc403SetBaud(com_port, baud);
           unsigned long divisor, clockSource, temp;

           /* Ensure CICCR[7] is 0 to select Internal Baud Clock */
           powerpcMtcic_cr((unsigned long)(powerpcMfcic_cr() & 0xFEFFFFFF));

           /* Determine Internal Baud Clock Frequency */
           /* powerpcMfclkgpcr() reads DCR 0x120 - the*/
           /* SCCR (Serial Clock Control Register) on Vesta */
           temp = powerpcMfclkgpcr();

           if(temp & 0x00000080) {
               clockSource = 324000000;
           }
           else {
               clockSource = 216000000;
           }
           clockSource = clockSource/(unsigned long)((temp&0x00FC0000)>>18);
           divisor = clockSource/(16*baud) - 1;
           /* divisor has only 12 bits of resolution */
           if(divisor>0x00000FFF){
               divisor=0x00000FFF;
           }

           quot = divisor;
        }

        if (baud == 38400 &&
            ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
            quot = info->state->custom_divisor;

        if (!quot && old_termios) {
            info->tty->termios->c_cflag &= ~CBAUD;
            info->tty->termios->c_cflag |= (old_termios->c_cflag & CBAUD);
            old_termios = NULL;
        }
    } while (quot == 0 && old_termios);

    /* As a last resort, if the quotient is zero, default to 9600 bps */
    if (!quot)
        quot = (info->port->uartclk / (16 * 9600)) - 1;

    info->timeout = info->port->fifosize * HZ * bits / baud;
    info->timeout += HZ/50;     /* Add .02 seconds of slop */

    if (cflag & CRTSCTS)
        info->flags |= ASYNC_CTS_FLOW;
    else
        info->flags &= ~ASYNC_CTS_FLOW;
    if (cflag & CLOCAL)
        info->flags &= ~ASYNC_CHECK_CD;
    else
        info->flags |= ASYNC_CHECK_CD;

    /*
     * Set up parity check flag
     */
#define RELEVENT_IFLAG(iflag)   ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

    info->read_status_mask = _LSR_OE_MASK;
    if (I_INPCK(info->tty))
        info->read_status_mask |= _LSR_FE_MASK | _LSR_PE_MASK;
    if (I_BRKINT(info->tty) || I_PARMRK(info->tty))
        info->read_status_mask |= _LSR_LB_MASK;

    /*
     * Characters to ignore
     */
    info->ignore_status_mask = 0;
    if (I_IGNPAR(info->tty))
        info->ignore_status_mask |= _LSR_FE_MASK | _LSR_PE_MASK;
    if (I_IGNBRK(info->tty)) {
        info->ignore_status_mask |=  _LSR_LB_MASK;
        /*
         * If we're ignoring parity and break indicators,
         * ignore overruns to (for real raw support).
         */
        if (I_IGNPAR(info->tty))
            info->ignore_status_mask |=  _LSR_OE_MASK;
    }

    /* disable interrupts while reading and clearing registers */
    spin_lock_irqsave(&info->state->sicc_lock,flags);

    old_rcr = readb(info->port->uart_base + BL_SICC_RCR);
    old_tcr = readb(info->port->uart_base + BL_SICC_TxCR);


    writeb(0, info->port->uart_base + BL_SICC_RCR);
    writeb(0, info->port->uart_base + BL_SICC_TxCR);

    /*RLBtrace (&ppc403Chan0, 0x2000000c, 0, 0);*/


    spin_unlock_irqrestore(&info->state->sicc_lock,flags);


    /* Set baud rate */
    writeb((quot & 0x00000F00)>>8, info->port->uart_base + BL_SICC_BRDH );
    writeb( quot & 0x00000FF,      info->port->uart_base + BL_SICC_BRDL );

    /* Set CTL2 reg to use external clock (ExtClk) and enable FIFOs. */
    /* For now, do NOT use FIFOs since 403 UART did not have this    */
    /* capability and this driver was inherited from 403UART.        */
    writeb(_CTL2_EXTERN, info->port->uart_base + BL_SICC_CTL2);

    writeb(lcr_h, info->port->uart_base + BL_SICC_LCR);

    writeb(old_rcr, info->port->uart_base + BL_SICC_RCR);  // restore rcr
    writeb(old_tcr, info->port->uart_base + BL_SICC_TxCR); // restore txcr

}


static void siccuart_put_char(struct tty_struct *tty, u_char ch)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    if (!tty || !info->xmit.buf)
        return;

    /* lock info->xmit while adding character to tx buffer */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    if (CIRC_SPACE(info->xmit.head, info->xmit.tail, SICC_XMIT_SIZE) != 0) {
        info->xmit.buf[info->xmit.head] = ch;
        info->xmit.head = (info->xmit.head + 1) & (SICC_XMIT_SIZE - 1);
    }
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}

static void siccuart_flush_chars(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    if (info->xmit.head == info->xmit.tail
        || tty->stopped
        || tty->hw_stopped
        || !info->xmit.buf)
        return;

    /* disable interrupts while transmitting characters */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    siccuart_enable_tx_interrupt(info);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}

static int siccuart_write(struct tty_struct *tty,
              const u_char * buf, int count)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;
    int c, ret = 0;

    if (!tty || !info->xmit.buf || !tmp_buf)
        return 0;

    /* lock info->xmit while removing characters from buffer */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    while (1) {
        c = CIRC_SPACE_TO_END(info->xmit.head,
                      info->xmit.tail,
                      SICC_XMIT_SIZE);
        if (count < c)
            c = count;
        if (c <= 0)
            break;
        memcpy(info->xmit.buf + info->xmit.head, buf, c);
        info->xmit.head = (info->xmit.head + c) &
                  (SICC_XMIT_SIZE - 1);
        buf += c;
        count -= c;
        ret += c;
    }
    if (info->xmit.head != info->xmit.tail
        && !tty->stopped
        && !tty->hw_stopped)
        siccuart_enable_tx_interrupt(info);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    return ret;
}

static int siccuart_write_room(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;

    return CIRC_SPACE(info->xmit.head, info->xmit.tail, SICC_XMIT_SIZE);
}

static int siccuart_chars_in_buffer(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;

    return CIRC_CNT(info->xmit.head, info->xmit.tail, SICC_XMIT_SIZE);
}

static void siccuart_flush_buffer(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    pr_debug("siccuart_flush_buffer(%d) called\n", tty->index);
    /* lock info->xmit while zeroing buffer counts */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    info->xmit.head = info->xmit.tail = 0;
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    wake_up_interruptible(&tty->write_wait);
    if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
        tty->ldisc.write_wakeup)
        (tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void siccuart_send_xchar(struct tty_struct *tty, char ch)
{
    struct SICC_info *info = tty->driver_data;

    info->x_char = ch;
    if (ch)
       siccuart_enable_tx_interrupt(info);
}

static void siccuart_throttle(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;

    if (I_IXOFF(tty))
        siccuart_send_xchar(tty, STOP_CHAR(tty));

    if (tty->termios->c_cflag & CRTSCTS) {
        /* disable interrupts while setting modem control lines */
        spin_lock_irqsave(&info->state->sicc_lock,flags);
        info->mctrl &= ~TIOCM_RTS;
        info->port->set_mctrl(info->port, info->mctrl);
        spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    }
}

static void siccuart_unthrottle(struct tty_struct *tty)
{
    struct SICC_info *info = (struct SICC_info *) tty->driver_data;
    unsigned long flags;

    if (I_IXOFF(tty)) {
        if (info->x_char)
            info->x_char = 0;
        else
            siccuart_send_xchar(tty, START_CHAR(tty));
    }

    if (tty->termios->c_cflag & CRTSCTS) {
        /* disable interrupts while setting modem control lines */
        spin_lock_irqsave(&info->state->sicc_lock,flags);
        info->mctrl |= TIOCM_RTS;
        info->port->set_mctrl(info->port, info->mctrl);
        spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    }
}

static int get_serial_info(struct SICC_info *info, struct serial_struct *retinfo)
{
    struct SICC_state *state = info->state;
    struct SICC_port *port = info->port;
    struct serial_struct tmp;

    memset(&tmp, 0, sizeof(tmp));
    tmp.type       = 0;
    tmp.line       = state->line;
    tmp.port       = port->uart_base;
    if (HIGH_BITS_OFFSET)
        tmp.port_high = port->uart_base >> HIGH_BITS_OFFSET;
    tmp.irq        = port->irqrx;
    tmp.flags      = 0;
    tmp.xmit_fifo_size = port->fifosize;
    tmp.baud_base      = port->uartclk / 16;
    tmp.close_delay    = state->close_delay;
    tmp.closing_wait   = state->closing_wait;
    tmp.custom_divisor = state->custom_divisor;

    if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
        return -EFAULT;
    return 0;
}

static int set_serial_info(struct SICC_info *info,
               struct serial_struct *newinfo)
{
    struct serial_struct new_serial;
    struct SICC_state *state, old_state;
    struct SICC_port *port;
    unsigned long new_port;
    unsigned int i, change_irq, change_port;
    int retval = 0;

    if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
        return -EFAULT;

    state = info->state;
    old_state = *state;
    port = info->port;

    new_port = new_serial.port;
    if (HIGH_BITS_OFFSET)
        new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

    change_irq  = new_serial.irq != port->irqrx;
    change_port = new_port != port->uart_base;

    if (!capable(CAP_SYS_ADMIN)) {
        if (change_irq || change_port ||
            (new_serial.baud_base != port->uartclk / 16) ||
            (new_serial.close_delay != state->close_delay) ||
            (new_serial.xmit_fifo_size != port->fifosize) ||
            ((new_serial.flags & ~ASYNC_USR_MASK) !=
             (state->flags & ~ASYNC_USR_MASK)))
            return -EPERM;
        state->flags = ((state->flags & ~ASYNC_USR_MASK) |
                (new_serial.flags & ASYNC_USR_MASK));
        info->flags = ((info->flags & ~ASYNC_USR_MASK) |
                   (new_serial.flags & ASYNC_USR_MASK));
        state->custom_divisor = new_serial.custom_divisor;
        goto check_and_exit;
    }

    if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
        (new_serial.baud_base < 9600))
        return -EINVAL;

    if (new_serial.type && change_port) {
        for (i = 0; i < SERIAL_SICC_NR; i++)
            if ((port != sicc_ports + i) &&
                sicc_ports[i].uart_base != new_port)
                return -EADDRINUSE;
    }

    if ((change_port || change_irq) && (state->count > 1))
        return -EBUSY;

    /*
     * OK, past this point, all the error checking has been done.
     * At this point, we start making changes.....
     */
    port->uartclk = new_serial.baud_base * 16;
    state->flags = ((state->flags & ~ASYNC_FLAGS) |
            (new_serial.flags & ASYNC_FLAGS));
    info->flags = ((state->flags & ~ASYNC_INTERNAL_FLAGS) |
               (info->flags & ASYNC_INTERNAL_FLAGS));
    state->custom_divisor = new_serial.custom_divisor;
    state->close_delay = msecs_to_jiffies(10 * new_serial.close_delay);
    state->closing_wait = msecs_to_jiffies(10 * new_serial.closing_wait);
    info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
    port->fifosize = new_serial.xmit_fifo_size;

    if (change_port || change_irq) {
        /*
         * We need to shutdown the serial port at the old
         * port/irq combination.
         */
        siccuart_shutdown(info);
        port->irqrx = new_serial.irq;
        port->uart_base = new_port;
    }

check_and_exit:
    if (!port->uart_base)
        return 0;
    if (info->flags & ASYNC_INITIALIZED) {
        if ((old_state.flags & ASYNC_SPD_MASK) !=
            (state->flags & ASYNC_SPD_MASK) ||
            (old_state.custom_divisor != state->custom_divisor)) {
            if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
                info->tty->alt_speed = 57600;
            if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
                info->tty->alt_speed = 115200;
            if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
                info->tty->alt_speed = 230400;
            if ((state->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
                info->tty->alt_speed = 460800;
            siccuart_change_speed(info, NULL);
        }
    } else
        retval = siccuart_startup(info);
    return retval;
}


/*
 * get_lsr_info - get line status register info
 */
static int get_lsr_info(struct SICC_info *info, unsigned int *value)
{
    unsigned int result, status;
    unsigned long flags;

    /* disable interrupts while reading status from port */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    status = readb(info->port->uart_base +  BL_SICC_LSR);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    result = status & _LSR_TSR_EMPTY ? TIOCSER_TEMT : 0;

    /*
     * If we're about to load something into the transmit
     * register, we'll pretend the transmitter isn't empty to
     * avoid a race condition (depending on when the transmit
     * interrupt happens).
     */
    if (info->x_char ||
        ((CIRC_CNT(info->xmit.head, info->xmit.tail,
               SICC_XMIT_SIZE) > 0) &&
         !info->tty->stopped && !info->tty->hw_stopped))
        result &= TIOCSER_TEMT;

    return put_user(result, value);
}

static int get_modem_info(struct SICC_info *info, unsigned int *value)
{
    unsigned int result = info->mctrl;

    return put_user(result, value);
}

static int set_modem_info(struct SICC_info *info, unsigned int cmd,
              unsigned int *value)
{
    unsigned int arg, old;
    unsigned long flags;

    if (get_user(arg, value))
        return -EFAULT;

    old = info->mctrl;
    switch (cmd) {
    case TIOCMBIS:
        info->mctrl |= arg;
        break;

    case TIOCMBIC:
        info->mctrl &= ~arg;
        break;

    case TIOCMSET:
        info->mctrl = arg;
        break;

    default:
        return -EINVAL;
    }
    /* disable interrupts while setting modem control lines */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    if (old != info->mctrl)
        info->port->set_mctrl(info->port, info->mctrl);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    return 0;
}

static void siccuart_break_ctl(struct tty_struct *tty, int break_state)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;
    unsigned int lcr_h;


    /* disable interrupts while setting break state */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    lcr_h = readb(info->port + BL_SICC_LSR);
    if (break_state == -1)
        lcr_h |=  _LSR_LB_MASK;
    else
        lcr_h &= ~_LSR_LB_MASK;
    writeb(lcr_h, info->port + BL_SICC_LSRS);
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
}

static int siccuart_ioctl(struct tty_struct *tty, struct file *file,
               unsigned int cmd, unsigned long arg)
{
    struct SICC_info *info = tty->driver_data;
    struct SICC_icount cnow;
    struct serial_icounter_struct icount;
    unsigned long flags;

    if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
        (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
        (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
        if (tty->flags & (1 << TTY_IO_ERROR))
            return -EIO;
    }

    switch (cmd) {
        case TIOCMGET:
            return get_modem_info(info, (unsigned int *)arg);
        case TIOCMBIS:
        case TIOCMBIC:
        case TIOCMSET:
            return set_modem_info(info, cmd, (unsigned int *)arg);
        case TIOCGSERIAL:
            return get_serial_info(info,
                           (struct serial_struct *)arg);
        case TIOCSSERIAL:
            return set_serial_info(info,
                           (struct serial_struct *)arg);
        case TIOCSERGETLSR: /* Get line status register */
            return get_lsr_info(info, (unsigned int *)arg);
        /*
         * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
         * - mask passed in arg for lines of interest
         *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
         * Caller should use TIOCGICOUNT to see which one it was
         */
        case TIOCMIWAIT:
            return 0;
        /*
         * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
         * Return: write counters to the user passed counter struct
         * NB: both 1->0 and 0->1 transitions are counted except for
         *     RI where only 0->1 is counted.
         */
        case TIOCGICOUNT:
            /* disable interrupts while getting interrupt count */
            spin_lock_irqsave(&info->state->sicc_lock,flags);
            cnow = info->state->icount;
            spin_unlock_irqrestore(&info->state->sicc_lock,flags);
            icount.cts = cnow.cts;
            icount.dsr = cnow.dsr;
            icount.rng = cnow.rng;
            icount.dcd = cnow.dcd;
            icount.rx  = cnow.rx;
            icount.tx  = cnow.tx;
            icount.frame = cnow.frame;
            icount.overrun = cnow.overrun;
            icount.parity = cnow.parity;
            icount.brk = cnow.brk;
            icount.buf_overrun = cnow.buf_overrun;

            return copy_to_user((void *)arg, &icount, sizeof(icount))
                    ? -EFAULT : 0;

        default:
            return -ENOIOCTLCMD;
    }
    return 0;
}

static void siccuart_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
    struct SICC_info *info = tty->driver_data;
    unsigned long flags;
    unsigned int cflag = tty->termios->c_cflag;

    if ((cflag ^ old_termios->c_cflag) == 0 &&
        RELEVENT_IFLAG(tty->termios->c_iflag ^ old_termios->c_iflag) == 0)
        return;

    siccuart_change_speed(info, old_termios);

    /* Handle transition to B0 status */
    if ((old_termios->c_cflag & CBAUD) &&
        !(cflag & CBAUD)) {
        /* disable interrupts while setting break state */
        spin_lock_irqsave(&info->state->sicc_lock,flags);
        info->mctrl &= ~(TIOCM_RTS | TIOCM_DTR);
        info->port->set_mctrl(info->port, info->mctrl);
        spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    }

    /* Handle transition away from B0 status */
    if (!(old_termios->c_cflag & CBAUD) &&
        (cflag & CBAUD)) {
        /* disable interrupts while setting break state */
        spin_lock_irqsave(&info->state->sicc_lock,flags);
        info->mctrl |= TIOCM_DTR;
        if (!(cflag & CRTSCTS) ||
            !test_bit(TTY_THROTTLED, &tty->flags))
            info->mctrl |= TIOCM_RTS;
        info->port->set_mctrl(info->port, info->mctrl);
        spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    }

    /* Handle turning off CRTSCTS */
    if ((old_termios->c_cflag & CRTSCTS) &&
        !(cflag & CRTSCTS)) {
        tty->hw_stopped = 0;
        siccuart_start(tty);
    }

#if 0
    /*
     * No need to wake up processes in open wait, since they
     * sample the CLOCAL flag once, and don't recheck it.
     * XXX  It's not clear whether the current behavior is correct
     * or not.  Hence, this may change.....
     */
    if (!(old_termios->c_cflag & CLOCAL) &&
        (tty->termios->c_cflag & CLOCAL))
        wake_up_interruptible(&info->open_wait);
#endif
}

static void siccuart_close(struct tty_struct *tty, struct file *filp)
{
    struct SICC_info *info = tty->driver_data;
    struct SICC_state *state;
    unsigned long flags;

    if (!info)
        return;

    state = info->state;

    //pr_debug("siccuart_close() called\n");

    /* lock tty->driver_data while closing port */
    spin_lock_irqsave(&info->state->sicc_lock,flags);

    if (tty_hung_up_p(filp)) {
        goto quick_close;
    }

    if ((tty->count == 1) && (state->count != 1)) {
        /*
         * Uh, oh.  tty->count is 1, which means that the tty
         * structure will be freed.  state->count should always
         * be one in these conditions.  If it's greater than
         * one, we've got real problems, since it means the
         * serial port won't be shutdown.
         */
        printk("siccuart_close: bad serial port count; tty->count is 1, state->count is %d\n", state->count);
        state->count = 1;
    }
    if (--state->count < 0) {
        printk("rs_close: bad serial port count for %s: %d\n", tty->name, state->count);
        state->count = 0;
    }
    if (state->count) {
        goto quick_close;
    }
    info->flags |= ASYNC_CLOSING;
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    /*
     * Now we wait for the transmit buffer to clear; and we notify
     * the line discipline to only process XON/XOFF characters.
     */
    tty->closing = 1;
    if (info->state->closing_wait != ASYNC_CLOSING_WAIT_NONE)
        tty_wait_until_sent(tty, info->state->closing_wait);
    /*
     * At this point, we stop accepting input.  To do this, we
     * disable the receive line status interrupts.
     */
    if (info->flags & ASYNC_INITIALIZED) {
        siccuart_disable_rx_interrupt(info);
        /*
         * Before we drop DTR, make sure the UART transmitter
         * has completely drained; this is especially
         * important if there is a transmit FIFO!
         */
        siccuart_wait_until_sent(tty, info->timeout);
    }
    siccuart_shutdown(info);
    if (tty->driver->flush_buffer)
        tty->driver->flush_buffer(tty);
    if (tty->ldisc.flush_buffer)
        tty->ldisc.flush_buffer(tty);
    tty->closing = 0;
    info->event = 0;
    info->tty = NULL;
    if (info->blocked_open) {
        if (info->state->close_delay)
            schedule_timeout_interruptible(info->state->close_delay);
        wake_up_interruptible(&info->open_wait);
    }
    info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
    wake_up_interruptible(&info->close_wait);
    return;

quick_close:
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    return;
}

static void siccuart_wait_until_sent(struct tty_struct *tty, int timeout)
{
    struct SICC_info *info = (struct SICC_info *) tty->driver_data;
    unsigned long char_time, expire;

    if (info->port->fifosize == 0)
        return;

    /*
     * Set the check interval to be 1/5 of the estimated time to
     * send a single character, and make it at least 1.  The check
     * interval should also be less than the timeout.
     *
     * Note: we have to use pretty tight timings here to satisfy
     * the NIST-PCTS.
     */
    char_time = (info->timeout - msecs_to_jiffies(20)) / info->port->fifosize;
    char_time = char_time / 5;
    if (char_time == 0)
        char_time = 1;

    // Crazy!!   sometimes the input arg 'timeout' can be negtive numbers  :-(
    if (timeout >= 0 && timeout < char_time)
        char_time = timeout;
    /*
     * If the transmitter hasn't cleared in twice the approximate
     * amount of time to send the entire FIFO, it probably won't
     * ever clear.  This assumes the UART isn't doing flow
     * control, which is currently the case.  Hence, if it ever
     * takes longer than info->timeout, this is probably due to a
     * UART bug of some kind.  So, we clamp the timeout parameter at
     * 2*info->timeout.
     */
    if (!timeout || timeout > 2 * info->timeout)
        timeout = 2 * info->timeout;

    expire = jiffies + timeout;
    pr_debug("siccuart_wait_until_sent(%d), jiff=%lu, expire=%lu  char_time=%lu...\n",
           tty->index, jiffies,
           expire, char_time);
    while ((readb(info->port->uart_base + BL_SICC_LSR) & _LSR_TX_ALL) != _LSR_TX_ALL) {
        schedule_timeout_interruptible(char_time);
        if (signal_pending(current))
            break;
        if (timeout && time_after(jiffies, expire))
            break;
    }
    set_current_state(TASK_RUNNING);
}

static void siccuart_hangup(struct tty_struct *tty)
{
    struct SICC_info *info = tty->driver_data;
    struct SICC_state *state = info->state;

    siccuart_flush_buffer(tty);
    if (info->flags & ASYNC_CLOSING)
        return;
    siccuart_shutdown(info);
    info->event = 0;
    state->count = 0;
    info->flags &= ~ASYNC_NORMAL_ACTIVE;
    info->tty = NULL;
    wake_up_interruptible(&info->open_wait);
}

static int block_til_ready(struct tty_struct *tty, struct file *filp,
               struct SICC_info *info)
{
    DECLARE_WAITQUEUE(wait, current);
    struct SICC_state *state = info->state;
    unsigned long flags;
    int do_clocal = 0, extra_count = 0, retval;

    /*
     * If the device is in the middle of being closed, then block
     * until it's done, and then try again.
     */
    if (tty_hung_up_p(filp) ||
        (info->flags & ASYNC_CLOSING)) {
        if (info->flags & ASYNC_CLOSING)
            interruptible_sleep_on(&info->close_wait);
        return (info->flags & ASYNC_HUP_NOTIFY) ?
            -EAGAIN : -ERESTARTSYS;
    }

    /*
     * If non-blocking mode is set, or the port is not enabled,
     * then make the check up front and then exit.
     */
    if ((filp->f_flags & O_NONBLOCK) ||
        (tty->flags & (1 << TTY_IO_ERROR))) {
        info->flags |= ASYNC_NORMAL_ACTIVE;
        return 0;
    }

    if (tty->termios->c_cflag & CLOCAL)
	do_clocal = 1;

    /*
     * Block waiting for the carrier detect and the line to become
     * free (i.e., not in use by the callout).  While we are in
     * this loop, state->count is dropped by one, so that
     * rs_close() knows when to free things.  We restore it upon
     * exit, either normal or abnormal.
     */
    retval = 0;
    add_wait_queue(&info->open_wait, &wait);
    /* lock while decrementing state->count */
    spin_lock_irqsave(&info->state->sicc_lock,flags);
    if (!tty_hung_up_p(filp)) {
        extra_count = 1;
        state->count--;
    }
    spin_unlock_irqrestore(&info->state->sicc_lock,flags);
    info->blocked_open++;
    while (1) {
        /* disable interrupts while setting modem control lines */
        spin_lock_irqsave(&info->state->sicc_lock,flags);
        if (tty->termios->c_cflag & CBAUD) {
            info->mctrl = TIOCM_DTR | TIOCM_RTS;
            info->port->set_mctrl(info->port, info->mctrl);
        }
        spin_unlock_irqrestore(&info->state->sicc_lock,flags);
        set_current_state(TASK_INTERRUPTIBLE);
        if (tty_hung_up_p(filp) ||
            !(info->flags & ASYNC_INITIALIZED)) {
            if (info->flags & ASYNC_HUP_NOTIFY)
                retval = -EAGAIN;
            else
                retval = -ERESTARTSYS;
            break;
        }
        if (!(info->flags & ASYNC_CLOSING) &&
            (do_clocal /*|| (UART_GET_FR(info->port) & SICC_UARTFR_DCD)*/))
            break;
        if (signal_pending(current)) {
            retval = -ERESTARTSYS;
            break;
        }
        schedule();
    }
    set_current_state(TASK_RUNNING);
    remove_wait_queue(&info->open_wait, &wait);
    if (extra_count)
        state->count++;
    info->blocked_open--;
    if (retval)
        return retval;
    info->flags |= ASYNC_NORMAL_ACTIVE;
    return 0;
}

static struct SICC_info *siccuart_get(int line)
{
    struct SICC_info *info;
    struct SICC_state *state = sicc_state + line;

    state->count++;
    if (state->info)
        return state->info;
    info = kmalloc(sizeof(struct SICC_info), GFP_KERNEL);
    if (info) {
        memset(info, 0, sizeof(struct SICC_info));
        init_waitqueue_head(&info->open_wait);
        init_waitqueue_head(&info->close_wait);
        init_waitqueue_head(&info->delta_msr_wait);
        info->flags = state->flags;
        info->state = state;
        info->port  = sicc_ports + line;
        tasklet_init(&info->tlet, siccuart_tasklet_action,
                 (unsigned long)info);
    }
    if (state->info) {
        kfree(info);
        return state->info;
    }
    state->info = info;
    return info;
}

static int siccuart_open(struct tty_struct *tty, struct file *filp)
{
    struct SICC_info *info;
    int retval, line = tty->index;


    // is this a line that we've got?
    if (line >= SERIAL_SICC_NR) {
        return -ENODEV;
    }

    info = siccuart_get(line);
    if (!info)
        return -ENOMEM;

    tty->driver_data = info;
    info->tty = tty;
    info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

    /*
     * Make sure we have the temporary buffer allocated
     */
    if (!tmp_buf) {
        unsigned long page = get_zeroed_page(GFP_KERNEL);
        if (tmp_buf)
            free_page(page);
        else if (!page) {
            return -ENOMEM;
        }
        tmp_buf = (u_char *)page;
    }

    /*
     * If the port is in the middle of closing, bail out now.
     */
    if (tty_hung_up_p(filp) ||
        (info->flags & ASYNC_CLOSING)) {
        if (info->flags & ASYNC_CLOSING)
            interruptible_sleep_on(&info->close_wait);
        return -EAGAIN;
    }

    /*
     * Start up the serial port
     */
    retval = siccuart_startup(info);
    if (retval) {
        return retval;
    }

    retval = block_til_ready(tty, filp, info);
    if (retval) {
        return retval;
    }

#ifdef CONFIG_SERIAL_SICC_CONSOLE
    if (siccuart_cons.cflag && siccuart_cons.index == line) {
        tty->termios->c_cflag = siccuart_cons.cflag;
        siccuart_cons.cflag = 0;
        siccuart_change_speed(info, NULL);
    }
#endif
    return 0;
}

static struct tty_operations sicc_ops = {
	.open = siccuart_open,
	.close = siccuart_close,
	.write = siccuart_write,
	.put_char = siccuart_put_char,
	.flush_chars = siccuart_flush_chars,
	.write_room = siccuart_write_room,
	.chars_in_buffer = siccuart_chars_in_buffer,
	.flush_buffer  = siccuart_flush_buffer,
	.ioctl = siccuart_ioctl,
	.throttle = siccuart_throttle,
	.unthrottle = siccuart_unthrottle,
	.send_xchar = siccuart_send_xchar,
	.set_termios = siccuart_set_termios,
	.stop = siccuart_stop,
	.start = siccuart_start,
	.hangup = siccuart_hangup,
	.break_ctl = siccuart_break_ctl,
	.wait_until_sent = siccuart_wait_until_sent,
};

int __init siccuart_init(void)
{
    int i;
    siccnormal_driver = alloc_tty_driver(SERIAL_SICC_NR);
    if (!siccnormal_driver)
	return -ENOMEM;
    printk("IBM Vesta SICC serial port driver V 0.1 by Yudong Yang and Yi Ge / IBM CRL .\n");
    siccnormal_driver->driver_name = "serial_sicc";
    siccnormal_driver->owner = THIS_MODULE;
    siccnormal_driver->name = SERIAL_SICC_NAME;
    siccnormal_driver->major = SERIAL_SICC_MAJOR;
    siccnormal_driver->minor_start = SERIAL_SICC_MINOR;
    siccnormal_driver->type = TTY_DRIVER_TYPE_SERIAL;
    siccnormal_driver->subtype = SERIAL_TYPE_NORMAL;
    siccnormal_driver->init_termios = tty_std_termios;
    siccnormal_driver->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
    siccnormal_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
    tty_set_operations(siccnormal_driver, &sicc_ops);

    if (tty_register_driver(siccnormal_driver))
        panic("Couldn't register SICC serial driver\n");

    for (i = 0; i < SERIAL_SICC_NR; i++) {
        struct SICC_state *state = sicc_state + i;
        state->line     = i;
        state->close_delay  = msecs_to_jiffies(500);
        state->closing_wait = 30 * HZ;
	spin_lock_init(&state->sicc_lock);
    }


    return 0;
}

__initcall(siccuart_init);

#ifdef CONFIG_SERIAL_SICC_CONSOLE
/************** console driver *****************/

/*
 * This code is currently never used; console->read is never called.
 * Therefore, although we have an implementation, we don't use it.
 * FIXME: the "const char *s" should be fixed to "char *s" some day.
 * (when the definition in include/linux/console.h is also fixed)
 */
#ifdef used_and_not_const_char_pointer
static int siccuart_console_read(struct console *co, const char *s, u_int count)
{
    struct SICC_port *port = &sicc_ports[co->index];
    unsigned int status;
    char *w;
    int c;

    pr_debug("siccuart_console_read() called\n");

    c = 0;
    w = s;
    while (c < count) {
        if(readb(port->uart_base +  BL_SICC_LSR) & _LSR_RBR_FULL) {
            *w++ = readb(port->uart_base +  BL_SICC_RBR);
            c++;
        } else {
            // nothing more to get, return
            return c;
        }
    }
    // return the count
    return c;
}
#endif

/*
 *  Print a string to the serial port trying not to disturb
 *  any possible real use of the port...
 *
 *  The console_lock must be held when we get here.
 */
static void siccuart_console_write(struct console *co, const char *s, u_int count)
{
    struct SICC_port *port = &sicc_ports[co->index];
    unsigned int old_cr;
    int i;

    /*
     *  First save the CR then disable the interrupts
     */
    old_cr = readb(port->uart_base +  BL_SICC_TxCR);
    writeb(old_cr & ~_TxCR_DME_MASK, port->uart_base +  BL_SICC_TxCR);

    /*
     *  Now, do each character
     */
    for (i = 0; i < count; i++) {
        while ((readb(port->uart_base +  BL_SICC_LSR)&_LSR_TX_ALL) != _LSR_TX_ALL);
        writeb(s[i], port->uart_base +  BL_SICC_TBR);
        if (s[i] == '\n') {
            while ((readb(port->uart_base +  BL_SICC_LSR)&_LSR_TX_ALL) != _LSR_TX_ALL);
            writeb('\r', port->uart_base +  BL_SICC_TBR);
        }
    }

    /*
     *  Finally, wait for transmitter to become empty
     *  and restore the TCR
     */
    while ((readb(port->uart_base +  BL_SICC_LSR)&_LSR_TX_ALL) != _LSR_TX_ALL);
    writeb(old_cr, port->uart_base +  BL_SICC_TxCR);
}

/*
 *  Receive character from the serial port
 */
static int siccuart_console_wait_key(struct console *co)
{
    struct SICC_port *port = &sicc_ports[co->index];
    int c;

    while(!(readb(port->uart_base +  BL_SICC_LSR) & _LSR_RBR_FULL));
    c = readb(port->uart_base +  BL_SICC_RBR);
    return c;
}

static struct tty_driver *siccuart_console_device(struct console *c, int *index)
{
	*index = c->index;
	return siccnormal_driver;
}

static int __init siccuart_console_setup(struct console *co, char *options)
{
    struct SICC_port *port;
    int baud = 9600;
    int bits = 8;
    int parity = 'n';
    u_int cflag = CREAD | HUPCL | CLOCAL;
    u_int lcr_h, quot;


    if (co->index >= SERIAL_SICC_NR)
        co->index = 0;

    port = &sicc_ports[co->index];

    if (port->uart_base == 0)
	port->uart_base = (int)ioremap(port->uart_base_phys, PAGE_SIZE);

    if (options) {
        char *s = options;
        baud = simple_strtoul(s, NULL, 10);
        while (*s >= '0' && *s <= '9')
            s++;
        if (*s) parity = *s++;
        if (*s) bits = *s - '0';
    }

    /*
     *    Now construct a cflag setting.
     */
    switch (baud) {
    case 1200:  cflag |= B1200;         break;
    case 2400:  cflag |= B2400;         break;
    case 4800:  cflag |= B4800;         break;
    default:    cflag |= B9600;   baud = 9600;  break;
    case 19200: cflag |= B19200;        break;
    case 38400: cflag |= B38400;        break;
    case 57600: cflag |= B57600;        break;
    case 115200:    cflag |= B115200;       break;
    }
    switch (bits) {
    case 7:   cflag |= CS7; lcr_h = _LCR_PE_DISABLE | _LCR_DB_7_BITS | _LCR_SB_1_BIT;   break;
    default:  cflag |= CS8; lcr_h = _LCR_PE_DISABLE | _LCR_DB_8_BITS | _LCR_SB_1_BIT;   break;
    }
    switch (parity) {
    case 'o':
    case 'O': cflag |= PARODD; lcr_h |= _LCR_PTY_ODD;   break;
    case 'e':
    case 'E': cflag |= PARENB; lcr_h |= _LCR_PE_ENABLE |  _LCR_PTY_ODD; break;
    }

    co->cflag = cflag;


       {
           // a copy of is inserted here ppc403SetBaud(com_port, (int)9600);
           unsigned long divisor, clockSource, temp;
           unsigned int rate = baud;

          /* Ensure CICCR[7] is 0 to select Internal Baud Clock */
          powerpcMtcic_cr((unsigned long)(powerpcMfcic_cr() & 0xFEFFFFFF));

          /* Determine Internal Baud Clock Frequency */
          /* powerpcMfclkgpcr() reads DCR 0x120 - the*/
          /* SCCR (Serial Clock Control Register) on Vesta */
          temp = powerpcMfclkgpcr();

          if(temp & 0x00000080) {
              clockSource = 324000000;
          }
          else {
              clockSource = 216000000;
          }
          clockSource = clockSource/(unsigned long)((temp&0x00FC0000)>>18);
          divisor = clockSource/(16*rate) - 1;
          /* divisor has only 12 bits of resolution */
          if(divisor>0x00000FFF){
               divisor=0x00000FFF;
          }

          quot = divisor;
       }

    writeb((quot & 0x00000F00)>>8, port->uart_base + BL_SICC_BRDH );
    writeb( quot & 0x00000FF,      port->uart_base   + BL_SICC_BRDL );

    /* Set CTL2 reg to use external clock (ExtClk) and enable FIFOs. */
    /* For now, do NOT use FIFOs since 403 UART did not have this    */
    /* capability and this driver was inherited from 403UART.        */
    writeb(_CTL2_EXTERN, port->uart_base  + BL_SICC_CTL2);

    writeb(lcr_h, port->uart_base + BL_SICC_LCR);
    writeb(_RCR_ER_ENABLE | _RCR_PME_HARD, port->uart_base + BL_SICC_RCR);
    writeb( _TxCR_ET_ENABLE , port->uart_base + BL_SICC_TxCR);

    // writeb(, info->port->uart_base + BL_SICC_RCR );
    /*
     * Transmitter Command Register: Transmitter enabled & DMA + TBR interrupt
     * + Transmitter Empty interrupt + Transmitter error interrupt disabled &
     * Stop mode when CTS active enabled & Transmit Break + Pattern Generation
     * mode disabled.
     */

    writeb( 0x00, port->uart_base + BL_SICC_IrCR );  // disable IrDA

    readb(port->uart_base + BL_SICC_RBR);

    writeb(0xf8, port->uart_base + BL_SICC_LSR);   /* reset bits 0-4 of LSR */

    /* we will enable the port as we need it */

    return 0;
}

static struct console siccuart_cons =
{
    .name =     SERIAL_SICC_NAME,
    .write =    siccuart_console_write,
#ifdef used_and_not_const_char_pointer
    .read =     siccuart_console_read,
#endif
    .device =   siccuart_console_device,
    .wait_key = siccuart_console_wait_key,
    .setup =    siccuart_console_setup,
    .flags =    CON_PRINTBUFFER,
    .index =    -1,
};

void __init sicc_console_init(void)
{
    register_console(&siccuart_cons);
}

#endif /* CONFIG_SERIAL_SICC_CONSOLE */
