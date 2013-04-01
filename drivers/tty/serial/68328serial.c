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
#include <linux/serial.h>
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

/* Turn off usage of real serial interrupt code, to "support" Copilot */
#ifdef CONFIG_XCOPILOT_BUGS
#undef USE_INTS
#else
#define USE_INTS
#endif

/*
 * I believe this is the optimal setting that reduces the number of interrupts.
 * At high speeds the output might become a little "bursted" (use USTCNT_TXHE
 * if that bothers you), but in most cases it will not, since we try to
 * transmit characters every time rs_interrupt is called. Thus, quite often
 * you'll see that a receive interrupt occures before the transmit one.
 *                                  -- Vladimir Gurevich
 */
#define USTCNT_TX_INTR_MASK (USTCNT_TXEE)

/*
 * 68328 and 68EZ328 UARTS are a little bit different. EZ328 has special
 * "Old data interrupt" which occures whenever the data stay in the FIFO
 * longer than 30 bits time. This allows us to use FIFO without compromising
 * latency. '328 does not have this feature and without the real  328-based
 * board I would assume that RXRE is the safest setting.
 *
 * For EZ328 I use RXHE (Half empty) interrupt to reduce the number of
 * interrupts. RXFE (receive queue full) causes the system to lose data
 * at least at 115200 baud
 *
 * If your board is busy doing other stuff, you might consider to use
 * RXRE (data ready intrrupt) instead.
 *
 * The other option is to make these INTR masks run-time configurable, so
 * that people can dynamically adapt them according to the current usage.
 *                                  -- Vladimir Gurevich
 */

/* (es) */
#if defined(CONFIG_M68EZ328) || defined(CONFIG_M68VZ328)
#define USTCNT_RX_INTR_MASK (USTCNT_RXHE | USTCNT_ODEN)
#elif defined(CONFIG_M68328)
#define USTCNT_RX_INTR_MASK (USTCNT_RXRE)
#else
#error Please, define the Rx interrupt events for your CPU
#endif
/* (/es) */

/*
 * This is our internal structure for each serial port's state.
 */
struct m68k_serial {
	struct tty_port		tport;
	char			is_cons;	/* Is this our console. */
	int			magic;
	int			baud_base;
	int			port;
	int			irq;
	int			type;		/* UART type */
	int			custom_divisor;
	int			x_char;		/* xon/xoff character */
	int			line;
	unsigned char		*xmit_buf;
	int			xmit_head;
	int			xmit_tail;
	int			xmit_cnt;
};

#define SERIAL_MAGIC 0x5301

/*
 * Define the number of ports supported and their irqs.
 */
#define NR_PORTS 1

static struct m68k_serial m68k_soft[NR_PORTS];

static unsigned int uart_irqs[NR_PORTS] = { UART_IRQ_NUM };

/* multiple ports are contiguous in memory */
m68328_uart *uart_addr = (m68328_uart *)USTCNT_ADDR;

struct tty_driver *serial_driver;

static void change_speed(struct m68k_serial *info, struct tty_struct *tty);

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
	unsigned long flags;
	int loops = 0;

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

static void receive_chars(struct m68k_serial *info, unsigned short rx)
{
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

		flag = TTY_NORMAL;

		if (rx & URX_PARITY_ERROR)
			flag = TTY_PARITY;
		else if (rx & URX_OVRUN)
			flag = TTY_OVERRUN;
		else if (rx & URX_FRAME_ERROR)
			flag = TTY_FRAME;

		tty_insert_flip_char(&info->tport, ch, flag);
#ifndef CONFIG_XCOPILOT_BUGS
	} while((rx = uart->urx.w) & URX_DATA_READY);
#endif

	tty_schedule_flip(&info->tport);
}

static void transmit_chars(struct m68k_serial *info, struct tty_struct *tty)
{
	m68328_uart *uart = &uart_addr[info->line];

	if (info->x_char) {
		/* Send next char */
		uart->utx.b.txdata = info->x_char;
		info->x_char = 0;
		goto clear_and_return;
	}

	if ((info->xmit_cnt <= 0) || !tty || tty->stopped) {
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
	struct tty_struct *tty = tty_port_tty_get(&info->tport);
	m68328_uart *uart;
	unsigned short rx;
	unsigned short tx;

	uart = &uart_addr[info->line];
	rx = uart->urx.w;

#ifdef USE_INTS
	tx = uart->utx.w;

	if (rx & URX_DATA_READY)
		receive_chars(info, rx);
	if (tx & UTX_TX_AVAIL)
		transmit_chars(info, tty);
#else
	receive_chars(info, rx);
#endif
	tty_kref_put(tty);

	return IRQ_HANDLED;
}

static int startup(struct m68k_serial *info, struct tty_struct *tty)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;
	
	if (info->tport.flags & ASYNC_INITIALIZED)
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

	if (tty)
		clear_bit(TTY_IO_ERROR, &tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */

	change_speed(info, tty);

	info->tport.flags |= ASYNC_INITIALIZED;
	local_irq_restore(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct m68k_serial *info, struct tty_struct *tty)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long	flags;

	uart->ustcnt = 0; /* All off! */
	if (!(info->tport.flags & ASYNC_INITIALIZED))
		return;

	local_irq_save(flags);
	
	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (tty)
		set_bit(TTY_IO_ERROR, &tty->flags);
	
	info->tport.flags &= ~ASYNC_INITIALIZED;
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
static void change_speed(struct m68k_serial *info, struct tty_struct *tty)
{
	m68328_uart *uart = &uart_addr[info->line];
	unsigned short port;
	unsigned short ustcnt;
	unsigned cflag;
	int	i;

	cflag = tty->termios.c_cflag;
	if (!(port = info->port))
		return;

	ustcnt = uart->ustcnt;
	uart->ustcnt = ustcnt & ~USTCNT_TXEN;

	i = cflag & CBAUD;
        if (i & CBAUDEX) {
                i = (i & ~CBAUDEX) + B38400;
        }

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

	info->is_cons = (tty->termios.c_line == N_TTY);
	
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
	tmp.flags = info->tport.flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->tport.close_delay;
	tmp.closing_wait = info->tport.closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;

	return 0;
}

static int set_serial_info(struct m68k_serial *info, struct tty_struct *tty,
			   struct serial_struct * new_info)
{
	struct tty_port *port = &info->tport;
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
		    (new_serial.close_delay != port->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (port->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	port->flags = ((port->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->type = new_serial.type;
	port->close_delay = new_serial.close_delay;
	port->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = startup(info, tty);
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
			return set_serial_info(info, tty,
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

	change_speed(info, tty);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios.c_cflag & CRTSCTS)) {
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
	struct tty_port *port = &info->tport;
	m68328_uart *uart = &uart_addr[info->line];
	unsigned long flags;

	if (serial_paranoia_check(info, tty->name, "rs_close"))
		return;
	
	local_irq_save(flags);
	
	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}
	
	if ((tty->count == 1) && (port->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "port->count is %d\n", port->count);
		port->count = 1;
	}
	if (--port->count < 0) {
		printk("rs_close: bad serial port count for ttyS%d: %d\n",
		       info->line, port->count);
		port->count = 0;
	}
	if (port->count) {
		local_irq_restore(flags);
		return;
	}
	port->flags |= ASYNC_CLOSING;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (port->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, port->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */

	uart->ustcnt &= ~USTCNT_RXEN;
	uart->ustcnt &= ~(USTCNT_RXEN | USTCNT_RX_INTR_MASK);

	shutdown(info, tty);
	rs_flush_buffer(tty);
		
	tty_ldisc_flush(tty);
	tty->closing = 0;
	tty_port_tty_set(&info->tport, NULL);
#warning "This is not and has never been valid so fix it"	
#if 0
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios.c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open)(tty);
	}
#endif	
	if (port->blocked_open) {
		if (port->close_delay)
			msleep_interruptible(jiffies_to_msecs(port->close_delay));
		wake_up_interruptible(&port->open_wait);
	}
	port->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CLOSING);
	wake_up_interruptible(&port->close_wait);
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
	shutdown(info, tty);
	info->tport.count = 0;
	info->tport.flags &= ~ASYNC_NORMAL_ACTIVE;
	tty_port_tty_set(&info->tport, NULL);
	wake_up_interruptible(&info->tport.open_wait);
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
	int retval;

	info = &m68k_soft[tty->index];

	if (serial_paranoia_check(info, tty->name, "rs_open"))
		return -ENODEV;

	info->tport.count++;
	tty->driver_data = info;
	tty_port_tty_set(&info->tport, tty);

	/*
	 * Start up serial port
	 */
	retval = startup(info, tty);
	if (retval)
		return retval;

	return tty_port_block_til_ready(&info->tport, tty, filp);
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

static const struct tty_port_operations rs_port_ops = {
};

/* rs_init inits the driver */
static int __init
rs68328_init(void)
{
	unsigned long flags;
	int i;
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

	local_irq_save(flags);

	for(i=0;i<NR_PORTS;i++) {

	    info = &m68k_soft[i];
	    tty_port_init(&info->tport);
	    info->tport.ops = &rs_port_ops;
	    info->magic = SERIAL_MAGIC;
	    info->port = (int) &uart_addr[i];
	    info->irq = uart_irqs[i];
	    info->custom_divisor = 16;
	    info->x_char = 0;
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

	    tty_port_link_device(&info->tport, serial_driver, i);
	}
	local_irq_restore(flags);

	if (tty_register_driver(serial_driver)) {
		put_tty_driver(serial_driver);
		for (i = 0; i < NR_PORTS; i++)
			tty_port_destroy(&m68k_soft[i].tport);
		printk(KERN_ERR "Couldn't register serial driver\n");
		return -ENOMEM;
	}

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
