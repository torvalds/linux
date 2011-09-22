/* 68328serial.c: Serial port driver for 68328 microcontroller
 *
 * Copyright (C) 1995       David S. Miller    <davem@caip.rutgers.edu>
 * Copyright (C) 1998       Kenneth Albanowski <kjahds@kjahds.com>
 * Copyright (C) 1998, 1999 D. Jeff Dionne     <jeff@uclinux.org>
 * Copyright (C) 1999       Vladimir Gurevich  <vgurevic@cisco.com>
 * Copyright (C) 2002-2003  David McCullough   <davidm@snapgear.com>
 * Copyright (C) 2002       Greg Ungerer       <gerg@snapgear.com>
 *
 * VZ Support/Fixes             Evan Stawnyczy <e@lineo.ca>
 * Multiple UART support        Daniel Potts <danielp@cse.unsw.edu.au>
 * Power management support     Daniel Potts <danielp@cse.unsw.edu.au>
 * VZ Second Serial Port enable Phil Wilshire
 * 2.4/2.5 port                 David McCullough
 */

#include <asm/dbg.h>
#include <linux/module.h>
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
#include <linux/console.h>
#include <linux/reboot.h>
#include <linux/keyboard.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

/* (es) */
/* note: perhaps we can murge these files, so that you can just
 * 	 define 1 of them, and they can sort that out for themselves
 */
#if defined(CONFIG_M68EZ328)
#include <asm/MC68EZ328.h>
#else
#if defined(CONFIG_M68VZ328)
#include <asm/MC68VZ328.h>
#else
#include <asm/MC68328.h>
#endif /* CONFIG_M68VZ328 */
#endif /* CONFIG_M68EZ328 */

#include "68328serial.h"

/* Turn off usage of real serial interrupt code, to "support" Copilot */
#ifdef CONFIG_XCOPILOT_BUGS
#undef USE_INTS
#else
#define USE_INTS
#endif

static struct m68k_serial m68k_soft[NR_PORTS];

static unsigned int uart_irqs[NR_PORTS] = UART_IRQ_DEFNS;

/* multiple ports are contiguous in memory */
m68328_uart *uart_addr = (m68328_uart *)USTCNT_ADDR;

struct tty_struct m68k_ttys;
struct m68k_serial *m68k_consinfo = 0;

#define M68K_CLOCK (16667000) /* FIXME: 16MHz is likely wrong */

struct tty_driver *serial_driver;

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Debugging... DEBUG_INTR is bad to use when one of the zs
 * lines is your console ;(
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define RS_ISR_PASS_LIMIT 256

static void change_speed(struct m68k_serial *info);

/*
 *	Setup for console. Argument comes from the boot command line.
 */

/* note: this is messy, but it works, again, perhaps defined somewhere else?*/
#ifdef CONFIG_M68VZ328
#define CONSOLE_BAUD_RATE	19200
#define DEFAULT_CBAUD		B19200
#endif


#ifndef CONSOLE_BAUD_RATE
#define	CONSOLE_BAUD_RATE	9600
#define	DEFAULT_CBAUD		B9600
#endif


static int m68328_console_initted = 0;
static int m68328_console_baud    = CONSOLE_BAUD_RATE;
static int m68328_console_cbaud   = DEFAULT_CBAUD;


static inline int serial_paranoia_check(struct m68k_serial *info,
					char *name, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct %s in %s\n";
	static const char *badinfo =
		"Warning: null m68k_serial for %s in %s\n";

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

/* Sets or clears DTR/RTS on the requested line */
static inline void m68k_rtsdtr(struct m68k_serial *ss, int set)
{
	if (set) {
		/* set the RTS/CTS line */
	} else {
		/* clear it */
	}
	return;
}

/* Utility routines */
static inline int get_baud(struct m68k_serial *ss)
{
	unsigned long result = 115200;
	unsigned short int baud = uart_addr[ss->line].ubaud;
	if (GET_FIELD(baud, UBAUD_PRESCALER) == 0x38) result = 38400;
	result >>= GET_FIELD(baud, UBAUD_DIVIDE);

	return result;
}

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_stop"))
		return;
	
	local_irq_save(flags);
	uart->ustcnt &= ~USTCNT_TXEN;
	local_irq_restore(flags);
}

static int rs_put_char(char ch)
{
        int flags, loops = 0;

        local_irq_save(flags);

	while (!(UTX & UTX_TX_AVAIL) && (loops < 1000)) {
        	loops++;
        	udelay(5);
        }

	UTX_TXDATA = ch;
        udelay(5);
        local_irq_restore(flags);
        return 1;
}

static void rs_start(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->name, "rs_start"))
		return;
	
	local_irq_save(flags);
	if (info->xmit_cnt && info->xmit_buf && !(uart->ustcnt & USTCNT_TXEN)) {
#ifdef USE_INTS
		uart->ustcnt |= USTCNT_TXEN | USTCNT_TX_INTR_MASK;
#else
		uart->ustcnt |= USTCNT_TXEN;
#endif
	}
	local_irq_restore(flags);
}

/* Drop into either the boot monitor or kadb upon receiving a break
 * from keyboard/console input.
 */
static void batten_down_hatches(void)
{
	/* Drop into the debugger */
}

static void status_handle(struct m68k_serial *info, unsigned short status)
{
	/* If this is console input and this is a
	 * 'break asserted' status change interrupt
	 * see if we can drop into the debugger
	 */
	if((status & URX_BREAK) && info->break_abort)
		batten_down_hatches();
	return;
}

static void receive_chars(struct m68k_serial *info, unsigned short rx)
{
	struct tty_struct *tty = info->tty;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned char ch, flag;

	/*
	 * This do { } while() loop will get ALL chars out of Rx FIFO 
         */
#ifndef CONFIG_XCOPILOT_BUGS
	do {
#endif	
		ch = GET_FIELD(rx, URX_RXDATA);
	
		if(info->is_cons) {
			if(URX_BREAK & rx) { /* whee, break received */
				status_handle(info, rx);
				return;
#ifdef CONFIG_MAGIC_SYSRQ
			} else if (ch == 0x10) { /* ^P */
				show_state();
				show_free_areas(0);
				show_buffers();
/*				show_net_buffers(); */
				return;
			} else if (ch == 0x12) { /* ^R */
				emergency_restart();
				return;
#endif /* CONFIG_MAGIC_SYSRQ */
			}
		}

		if(!tty)
			goto clear_and_exit;
		
		flag = TTY_NORMAL;

		if(rx & URX_PARITY_ERROR) {
			flag = TTY_PARITY;
			status_handle(info, rx);
		} else if(rx & URX_OVRUN) {
			flag = TTY_OVERRUN;
			status_handle(info, rx);
		} else if(rx & URX_FRAME_ERROR) {
			flag = TTY_FRAME;
			status_handle(info, rx);
		}
		tty_insert_flip_char(tty, ch, flag);
#ifndef CONFIG_XCOPILOT_BUGS
	} while((rx = uart->urx.w) & URX_DATA_READY);
#endif

	tty_schedule_flip(tty);

clear_and_exit:
	return;
}

static void transmit_chars(struct m68k_serial *info)
{
	m68328_uart *uart = &uart_addr[info->line];

	if (info->x_char) {
		/* Send next char */
		uart->utx.b.txdata = info->x_char;
		info->x_char = 0;
		goto clear_and_return;
	}

	if((info->xmit_cnt <= 0) || info->tty->stopped) {
		/* That's peculiar... TX ints off */
		uart->ustcnt &= ~USTCNT_TX_INTR_MASK;
		goto clear_and_return;
	}

	/* Send char */
	uart->utx.b.txdata = info->xmit_buf[info->xmit_tail++];
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;

	if(info->xmit_cnt <= 0) {
		/* All done for now... TX ints off */
		uart->ustcnt &= ~USTCNT_TX_INTR_MASK;
		goto clear_and_return;
	}

clear_and_return:
	/* Clear interrupt (should be auto)*/
	return;
}

/*
 * This is the serial driver's generic interrupt routine
 */
irqreturn_t rs_interrupt(int irq, void *dev_id)
{
	struct m68k_serial *info = dev_id;
	m68328_uart *uart;
	unsigned short rx;
	unsigned short tx;

	uart = &uart_addr[info->line];
	rx = uart->urx.w;

#ifdef USE_INTS
	tx = uart->utx.w;

	if (rx & URX_DATA_READY) receive_chars(info, rx);
	if (tx & UTX_TX_AVAIL)   transmit_chars(info);
#else
	receive_chars(info, rx);		
#endif
	return IRQ_HANDLED;
}

static int startup(struct m68k_serial * info)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;
	
	if (info->flags & S_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	local_irq_save(flags);

	/*
	 * Clear the FIFO buffers and disable them
	 * (they will be reenabled in change_speed())
	 */

	uart->ustcnt = USTCNT_UEN;
	info->xmit_fifo_size = 1;
	uart->ustcnt = USTCNT_UEN | USTCNT_RXEN | USTCNT_TXEN;
	(void)uart->urx.w;

	/*
	 * Finally, enable sequencing and interrupts
	 */
#ifdef USE_INTS
	uart->ustcnt = USTCNT_UEN | USTCNT_RXEN | 
                 USTCNT_RX_INTR_MASK | USTCNT_TX_INTR_MASK;
#else
	uart->ustcnt = USTCNT_UEN | USTCNT_RXEN | USTCNT_RX_INTR_MASK;
#endif

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */

	change_speed(info);

	info->flags |= S_INITIALIZED;
	local_irq_restore(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct m68k_serial * info)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long	flags;

	uart->ustcnt = 0; /* All off! */
	if (!(info->flags & S_INITIALIZED))
		return;

	local_irq_save(flags);
	
	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~S_INITIALIZED;
	local_irq_restore(flags);
}

struct {
	int divisor, prescale;
}
#ifndef CONFIG_M68VZ328
 hw_baud_table[18] = {
	{0,0}, /* 0 */
	{0,0}, /* 50 */
	{0,0}, /* 75 */
	{0,0}, /* 110 */
	{0,0}, /* 134 */
	{0,0}, /* 150 */
	{0,0}, /* 200 */
	{7,0x26}, /* 300 */
	{6,0x26}, /* 600 */
	{5,0x26}, /* 1200 */
	{0,0}, /* 1800 */
	{4,0x26}, /* 2400 */
	{3,0x26}, /* 4800 */
	{2,0x26}, /* 9600 */
	{1,0x26}, /* 19200 */
	{0,0x26}, /* 38400 */
	{1,0x38}, /* 57600 */
	{0,0x38}, /* 115200 */
};
#else
 hw_baud_table[18] = {
                 {0,0}, /* 0 */
                 {0,0}, /* 50 */
                 {0,0}, /* 75 */
                 {0,0}, /* 110 */
                 {0,0}, /* 134 */
                 {0,0}, /* 150 */
                 {0,0}, /* 200 */
                 {0,0}, /* 300 */
                 {7,0x26}, /* 600 */
                 {6,0x26}, /* 1200 */
                 {0,0}, /* 1800 */
                 {5,0x26}, /* 2400 */
                 {4,0x26}, /* 4800 */
                 {3,0x26}, /* 9600 */
                 {2,0x26}, /* 19200 */
                 {1,0x26}, /* 38400 */
                 {0,0x26}, /* 57600 */
                 {1,0x38}, /* 115200 */
}; 
#endif
/* rate = 1036800 / ((65 - prescale) * (1<<divider)) */

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct m68k_serial *info)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned short port;
	unsigned short ustcnt;
	unsigned cflag;
	int	i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;

	ustcnt = uart->ustcnt;
	uart->ustcnt = ustcnt & ~USTCNT_TXEN;

	i = cflag & CBAUD;
        if (i & CBAUDEX) {
                i = (i & ~CBAUDEX) + B38400;
        }

	info->baud = baud_table[i];
	uart->ubaud = PUT_FIELD(UBAUD_DIVIDE,    hw_baud_table[i].divisor) | 
		PUT_FIELD(UBAUD_PRESCALER, hw_baud_table[i].prescale);

	ustcnt &= ~(USTCNT_PARITYEN | USTCNT_ODD_EVEN | USTCNT_STOP | USTCNT_8_7);
	
	if ((cflag & CSIZE) == CS8)
		ustcnt |= USTCNT_8_7;
		
	if (cflag & CSTOPB)
		ustcnt |= USTCNT_STOP;

	if (cflag & PARENB)
		ustcnt |= USTCNT_PARITYEN;
	if (cflag & PARODD)
		ustcnt |= USTCNT_ODD_EVEN;
	
#ifdef CONFIG_SERIAL_68328_RTS_CTS
	if (cflag & CRTSCTS) {
		uart->utx.w &= ~ UTX_NOCTS;
	} else {
		uart->utx.w |= UTX_NOCTS;
	}
#endif

	ustcnt |= USTCNT_TXEN;
	
	uart->ustcnt = ustcnt;
	return;
}

/*
 * Fair output driver allows a process to speak.
 */
static void rs_fair_output(void)
{
	int left;		/* Output no more than that */
	unsigned long flags;
	struct m68k_serial *info = &m68k_soft[0];
	char c;

	if (info == 0) return;
	if (info->xmit_buf == 0) return;

	local_irq_save(flags);
	left = info->xmit_cnt;
	while (left != 0) {
		c = info->xmit_buf[info->xmit_tail];
		info->xmit_tail = (info->xmit_tail+1) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
		local_irq_restore(flags);

		rs_put_char(c);

		local_irq_save(flags);
		left = min(info->xmit_cnt, left-1);
	}

	/* Last character is being transmitted now (hopefully). */
	udelay(5);

	local_irq_restore(flags);
	return;
}

/*
 * m68k_console_print is registered for printk.
 */
void console_print_68328(const char *p)
{
	char c;
	
	while((c=*(p++)) != 0) {
		if(c == '\n')
			rs_put_char('\r');
		rs_put_char(c);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
	rs_fair_output();

	return;
}

static void rs_set_ldisc(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_set_ldisc"))
		return;

	info->is_cons = (tty->termios->c_line == N_TTY);
	
	printk("ttyS%d console mode %s\n", info->line, info->is_cons ? "on" : "off");
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_flush_chars"))
		return;
#ifndef USE_INTS
	for(;;) {
#endif

	/* Enable transmitter */
	local_irq_save(flags);

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
			!info->xmit_buf) {
		local_irq_restore(flags);
		return;
	}

#ifdef USE_INTS
	uart->ustcnt |= USTCNT_TXEN | USTCNT_TX_INTR_MASK;
#else
	uart->ustcnt |= USTCNT_TXEN;
#endif

#ifdef USE_INTS
	if (uart->utx.w & UTX_TX_AVAIL) {
#else
	if (1) {
#endif
		/* Send char */
		uart->utx.b.txdata = info->xmit_buf[info->xmit_tail++];
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
	}

#ifndef USE_INTS
	while (!(uart->utx.w & UTX_TX_AVAIL)) udelay(5);
	}
#endif
	local_irq_restore(flags);
}

extern void console_printn(const char * b, int count);

static int rs_write(struct tty_struct * tty,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;

	local_save_flags(flags);
	while (1) {
		local_irq_disable();		
		c = min_t(int, count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));
		local_irq_restore(flags);

		if (c <= 0)
			break;

		memcpy(info->xmit_buf + info->xmit_head, buf, c);

		local_irq_disable();
		info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt += c;
		local_irq_restore(flags);
		buf += c;
		count -= c;
		total += c;
	}

	if (info->xmit_cnt && !tty->stopped && !tty->hw_stopped) {
		/* Enable transmitter */
		local_irq_disable();		
#ifndef USE_INTS
		while(info->xmit_cnt) {
#endif

		uart->ustcnt |= USTCNT_TXEN;
#ifdef USE_INTS
		uart->ustcnt |= USTCNT_TX_INTR_MASK;
#else
		while (!(uart->utx.w & UTX_TX_AVAIL)) udelay(5);
#endif
		if (uart->utx.w & UTX_TX_AVAIL) {
			uart->utx.b.txdata = info->xmit_buf[info->xmit_tail++];
			info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
			info->xmit_cnt--;
		}

#ifndef USE_INTS
		}
#endif
		local_irq_restore(flags);
	}

	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
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
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->name, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;
	unsigned long flags;
				
	if (serial_paranoia_check(info, tty->name, "rs_flush_buffer"))
		return;
	local_irq_save(flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	local_irq_restore(flags);
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
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_throttle"))
		return;
	
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line (do this atomic) */
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->name, "rs_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}

	/* Assert RTS line (do this atomic) */
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct m68k_serial * info,
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
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

static int set_serial_info(struct m68k_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct m68k_serial old_info;
	int 			retval = 0;

	if (!new_info)
		return -EFAULT;
	if (copy_from_user(&new_serial, new_info, sizeof(new_serial)))
		return -EFAULT;
	old_info = *info;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~S_USR_MASK) !=
		     (info->flags & ~S_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~S_USR_MASK) |
			       (new_serial.flags & S_USR_MASK));
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
	info->flags = ((info->flags & ~S_FLAGS) |
			(new_serial.flags & S_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = startup(info);
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
static int get_lsr_info(struct m68k_serial * info, unsigned int *value)
{
#ifdef CONFIG_SERIAL_68328_RTS_CTS
	m68328_uart *uart = &uart_addr[info->line];
#endif
	unsigned char status;
	unsigned long flags;

	local_irq_save(flags);
#ifdef CONFIG_SERIAL_68328_RTS_CTS
	status = (uart->utx.w & UTX_CTS_STAT) ? 1 : 0;
#else
	status = 0;
#endif
	local_irq_restore(flags);
	return put_user(status, value);
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(struct m68k_serial * info, unsigned int duration)
{
	m68328_uart *uart = &uart_addr[info->line];
        unsigned long flags;
        if (!info->port)
                return;
        local_irq_save(flags);
#ifdef USE_INTS	
	uart->utx.w |= UTX_SEND_BREAK;
	msleep_interruptible(duration);
	uart->utx.w &= ~UTX_SEND_BREAK;
#endif		
        local_irq_restore(flags);
}

static int rs_ioctl(struct tty_struct *tty,
		    unsigned int cmd, unsigned long arg)
{
	struct m68k_serial * info = (struct m68k_serial *)tty->driver_data;
	int retval;

	if (serial_paranoia_check(info, tty->name, "rs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}
	
	switch (cmd) {
		case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!arg)
				send_break(info, 250);	/* 1/4 second */
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			send_break(info, arg ? arg*(100) : 250);
			return 0;
		case TIOCGSERIAL:
			return get_serial_info(info,
				       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			return get_lsr_info(info, (unsigned int *) arg);
		case TIOCSERGSTRUCT:
			if (copy_to_user((struct m68k_serial *) arg,
				    info, sizeof(struct m68k_serial)))
				return -EFAULT;
			return 0;
		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct ktermios *old_termios)
{
	struct m68k_serial *info = (struct m68k_serial *)tty->driver_data;

	change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		rs_start(tty);
	}
	
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * S structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct m68k_serial * info = (struct m68k_serial *)tty->driver_data;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;

	if (!info || serial_paranoia_check(info, tty->name, "rs_close"))
		return;
	
	local_irq_save(flags);
	
	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}
	
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
		local_irq_restore(flags);
		return;
	}
	info->flags |= S_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != S_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */

	uart->ustcnt &= ~USTCNT_RXEN;
	uart->ustcnt &= ~(USTCNT_RXEN | USTCNT_RX_INTR_MASK);

	shutdown(info);
	rs_flush_buffer(tty);
		
	tty_ldisc_flush(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = NULL;
#warning "This is not and has never been valid so fix it"	
#if 0
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open)(tty);
	}
#endif	
	if (info->blocked_open) {
		if (info->close_delay) {
			msleep_interruptible(jiffies_to_msecs(info->close_delay));
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(S_NORMAL_ACTIVE|S_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void rs_hangup(struct tty_struct *tty)
{
	struct m68k_serial * info = (struct m68k_serial *)tty->driver_data;
	
	if (serial_paranoia_check(info, tty->name, "rs_hangup"))
		return;
	
	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~S_NORMAL_ACTIVE;
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct m68k_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & S_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & S_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
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
		info->flags |= S_NORMAL_ACTIVE;
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

	info->count--;
	info->blocked_open++;
	while (1) {
		local_irq_disable();
		m68k_rtsdtr(info, 1);
		local_irq_enable();
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & S_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & S_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & S_CLOSING) && do_clocal)
			break;
                if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		tty_unlock();
		schedule();
		tty_lock();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;

	if (retval)
		return retval;
	info->flags |= S_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its S structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct m68k_serial	*info;
	int 			retval, line;

	line = tty->index;
	
	if (line >= NR_PORTS || line < 0) /* we have exactly one */
		return -ENODEV;

	info = &m68k_soft[line];

	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;

	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	return block_til_ready(tty, filp, info);
}

/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	printk("MC68328 serial driver version 1.00\n");
}

static const struct tty_operations rs_ops = {
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
	.set_ldisc = rs_set_ldisc,
};

/* rs_init inits the driver */
static int __init
rs68328_init(void)
{
	int flags, i;
	struct m68k_serial *info;

	serial_driver = alloc_tty_driver(NR_PORTS);
	if (!serial_driver)
		return -ENOMEM;

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* SPARC: Not all of this is exactly right for us. */
	
	serial_driver->name = "ttyS";
	serial_driver->major = TTY_MAJOR;
	serial_driver->minor_start = 64;
	serial_driver->type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver->subtype = SERIAL_TYPE_NORMAL;
	serial_driver->init_termios = tty_std_termios;
	serial_driver->init_termios.c_cflag = 
			m68328_console_cbaud | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(serial_driver, &rs_ops);

	if (tty_register_driver(serial_driver)) {
		put_tty_driver(serial_driver);
		printk(KERN_ERR "Couldn't register serial driver\n");
		return -ENOMEM;
	}

	local_irq_save(flags);

	for(i=0;i<NR_PORTS;i++) {

	    info = &m68k_soft[i];
	    info->magic = SERIAL_MAGIC;
	    info->port = (int) &uart_addr[i];
	    info->tty = NULL;
	    info->irq = uart_irqs[i];
	    info->custom_divisor = 16;
	    info->close_delay = 50;
	    info->closing_wait = 3000;
	    info->x_char = 0;
	    info->event = 0;
	    info->count = 0;
	    info->blocked_open = 0;
	    init_waitqueue_head(&info->open_wait);
	    init_waitqueue_head(&info->close_wait);
	    info->line = i;
	    info->is_cons = 1; /* Means shortcuts work */
	    
	    printk("%s%d at 0x%08x (irq = %d)", serial_driver->name, info->line, 
		   info->port, info->irq);
	    printk(" is a builtin MC68328 UART\n");
	    
#ifdef CONFIG_M68VZ328
		if (i > 0 )
			PJSEL &= 0xCF;  /* PSW enable second port output */
#endif

	    if (request_irq(uart_irqs[i],
			    rs_interrupt,
			    0,
			    "M68328_UART", info))
                panic("Unable to attach 68328 serial interrupt\n");
	}
	local_irq_restore(flags);
	return 0;
}

module_init(rs68328_init);



static void m68328_set_baud(void)
{
	unsigned short ustcnt;
	int	i;

	ustcnt = USTCNT;
	USTCNT = ustcnt & ~USTCNT_TXEN;

again:
	for (i = 0; i < ARRAY_SIZE(baud_table); i++)
		if (baud_table[i] == m68328_console_baud)
			break;
	if (i >= ARRAY_SIZE(baud_table)) {
		m68328_console_baud = 9600;
		goto again;
	}

	UBAUD = PUT_FIELD(UBAUD_DIVIDE,    hw_baud_table[i].divisor) | 
		PUT_FIELD(UBAUD_PRESCALER, hw_baud_table[i].prescale);
	ustcnt &= ~(USTCNT_PARITYEN | USTCNT_ODD_EVEN | USTCNT_STOP | USTCNT_8_7);
	ustcnt |= USTCNT_8_7;
	ustcnt |= USTCNT_TXEN;
	USTCNT = ustcnt;
	m68328_console_initted = 1;
	return;
}


int m68328_console_setup(struct console *cp, char *arg)
{
	int		i, n = CONSOLE_BAUD_RATE;

	if (!cp)
		return(-1);

	if (arg)
		n = simple_strtoul(arg,NULL,0);

	for (i = 0; i < ARRAY_SIZE(baud_table); i++)
		if (baud_table[i] == n)
			break;
	if (i < ARRAY_SIZE(baud_table)) {
		m68328_console_baud = n;
		m68328_console_cbaud = 0;
		if (i > 15) {
			m68328_console_cbaud |= CBAUDEX;
			i -= 15;
		}
		m68328_console_cbaud |= i;
	}

	m68328_set_baud(); /* make sure baud rate changes */
	return(0);
}


static struct tty_driver *m68328_console_device(struct console *c, int *index)
{
	*index = c->index;
	return serial_driver;
}


void m68328_console_write (struct console *co, const char *str,
			   unsigned int count)
{
	if (!m68328_console_initted)
		m68328_set_baud();
    while (count--) {
        if (*str == '\n')
           rs_put_char('\r');
        rs_put_char( *str++ );
    }
}


static struct console m68328_driver = {
	.name		= "ttyS",
	.write		= m68328_console_write,
	.device		= m68328_console_device,
	.setup		= m68328_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};


static int __init m68328_console_init(void)
{
	register_console(&m68328_driver);
	return 0;
}

console_initcall(m68328_console_init);
