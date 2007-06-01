/*
 * linux/arch/m68k/mac/debug.c
 *
 * Shamelessly stolen (SCC code and general framework) from:
 *
 * linux/arch/m68k/atari/debug.c
 *
 * Atari debugging and serial console stuff
 *
 * Assembled of parts of former atari/config.c 97-12-18 by Roman Hodek
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/delay.h>

#define BOOTINFO_COMPAT_1_0
#include <asm/setup.h>
#include <asm/bootinfo.h>
#include <asm/machw.h>
#include <asm/macints.h>

extern unsigned long mac_videobase;
extern unsigned long mac_videodepth;
extern unsigned long mac_rowbytes;

extern void mac_serial_print(const char *);

#define DEBUG_HEADS
#undef DEBUG_SCREEN
#define DEBUG_SERIAL

/*
 * These two auxiliary debug functions should go away ASAP. Only usage:
 * before the console output is up (after head.S come some other crucial
 * setup routines :-) it permits writing 'data' to the screen as bit patterns
 * (good luck reading those). Helped to figure that the bootinfo contained
 * garbage data on the amount and size of memory chunks ...
 *
 * The 'pos' argument now simply means 'linefeed after print' ...
 */

#ifdef DEBUG_SCREEN
static int peng, line;
#endif

void mac_debugging_short(int pos, short num)
{
#ifdef DEBUG_SCREEN
	unsigned char *pengoffset;
	unsigned char *pptr;
	int i;
#endif

#ifdef DEBUG_SERIAL
	printk("debug: %d !\n", num);
#endif

#ifdef DEBUG_SCREEN
	if (!MACH_IS_MAC) {
		/* printk("debug: %d !\n", num); */
		return;
	}

	/* calculate current offset */
	pengoffset = (unsigned char *)mac_videobase +
		(150+line*2) * mac_rowbytes + 80 * peng;

	pptr = pengoffset;

	for (i = 0; i < 8 * sizeof(short); i++) { /* # of bits */
		/*        value        mask for bit i, reverse order */
		*pptr++ = (num & (1 << (8*sizeof(short)-i-1)) ? 0xFF : 0x00);
	}

	peng++;

	if (pos) {
		line++;
		peng = 0;
	}
#endif
}

void mac_debugging_long(int pos, long addr)
{
#ifdef DEBUG_SCREEN
	unsigned char *pengoffset;
	unsigned char *pptr;
	int i;
#endif

#ifdef DEBUG_SERIAL
	printk("debug: #%ld !\n", addr);
#endif

#ifdef DEBUG_SCREEN
	if (!MACH_IS_MAC) {
		/* printk("debug: #%ld !\n", addr); */
		return;
	}

	pengoffset=(unsigned char *)(mac_videobase+(150+line*2)*mac_rowbytes)
		    +80*peng;

	pptr = pengoffset;

	for (i = 0; i < 8 * sizeof(long); i++) { /* # of bits */
		*pptr++ = (addr & (1 << (8*sizeof(long)-i-1)) ? 0xFF : 0x00);
	}

	peng++;

	if (pos) {
		line++;
		peng = 0;
	}
#endif
}

#ifdef DEBUG_SERIAL
/*
 * TODO: serial debug code
 */

struct mac_SCC {
	u_char cha_b_ctrl;
	u_char char_dummy1;
	u_char cha_a_ctrl;
	u_char char_dummy2;
	u_char cha_b_data;
	u_char char_dummy3;
	u_char cha_a_data;
};

# define scc (*((volatile struct mac_SCC*)mac_bi_data.sccbase))

/* Flag that serial port is already initialized and used */
int mac_SCC_init_done;
/* Can be set somewhere, if a SCC master reset has already be done and should
 * not be repeated; used by kgdb */
int mac_SCC_reset_done;

static int scc_port = -1;

static struct console mac_console_driver = {
	.name	= "debug",
	.flags	= CON_PRINTBUFFER,
	.index	= -1,
};

/*
 * Crude hack to get console output to the screen before the framebuffer
 * is initialized (happens a lot later in 2.1!).
 * We just use the console routines declared in head.S, this will interfere
 * with regular framebuffer console output and should be used exclusively
 * to debug kernel problems manifesting before framebuffer init (aka WSOD)
 *
 * To keep this hack from interfering with the regular console driver, either
 * deregister this driver before/on framebuffer console init, or silence this
 * function after the fbcon driver is running (will lose console messages!?).
 * To debug real early bugs, need to write a 'mac_register_console_hack()'
 * that is called from start_kernel() before setup_arch() and just registers
 * this driver if Mac.
 */

void mac_debug_console_write(struct console *co, const char *str,
			     unsigned int count)
{
	mac_serial_print(str);
}



/* Mac: loops_per_jiffy min. 19000 ^= .5 us; MFPDELAY was 0.6 us*/

#define uSEC 1

static inline void mac_sccb_out(char c)
{
	int i;

	do {
		for (i = uSEC; i > 0; --i)
			barrier();
	} while (!(scc.cha_b_ctrl & 0x04)); /* wait for tx buf empty */
	for (i = uSEC; i > 0; --i)
		barrier();
	scc.cha_b_data = c;
}

static inline void mac_scca_out(char c)
{
	int i;

	do {
		for (i = uSEC; i > 0; --i)
			barrier();
	} while (!(scc.cha_a_ctrl & 0x04)); /* wait for tx buf empty */
	for (i = uSEC; i > 0; --i)
		barrier();
	scc.cha_a_data = c;
}

void mac_sccb_console_write(struct console *co, const char *str,
			    unsigned int count)
{
	while (count--) {
		if (*str == '\n')
			mac_sccb_out('\r');
		mac_sccb_out(*str++);
	}
}

void mac_scca_console_write(struct console *co, const char *str,
			    unsigned int count)
{
	while (count--) {
		if (*str == '\n')
			mac_scca_out('\r');
		mac_scca_out(*str++);
	}
}


/* The following two functions do a quick'n'dirty initialization of the MFP or
 * SCC serial ports. They're used by the debugging interface, kgdb, and the
 * serial console code. */
#define SCCB_WRITE(reg,val)				\
	do {						\
		int i;					\
		scc.cha_b_ctrl = (reg);			\
		for (i = uSEC; i > 0; --i)		\
			barrier();			\
		scc.cha_b_ctrl = (val);			\
		for (i = uSEC; i > 0; --i)		\
			barrier();			\
	} while(0)

#define SCCA_WRITE(reg,val)				\
	do {						\
		int i;					\
		scc.cha_a_ctrl = (reg);			\
		for (i = uSEC; i > 0; --i)		\
			barrier();			\
		scc.cha_a_ctrl = (val);			\
		for (i = uSEC; i > 0; --i)		\
			barrier();			\
	} while(0)

/* loops_per_jiffy isn't initialized yet, so we can't use udelay(). This does a
 * delay of ~ 60us. */
/* Mac: loops_per_jiffy min. 19000 ^= .5 us; MFPDELAY was 0.6 us*/
#define LONG_DELAY()					\
	do {						\
		int i;					\
		for (i = 60*uSEC; i > 0; --i)		\
		    barrier();				\
	} while(0)

#ifndef CONFIG_SERIAL_CONSOLE
static void __init mac_init_scc_port(int cflag, int port)
#else
void mac_init_scc_port(int cflag, int port)
#endif
{
	extern int mac_SCC_reset_done;

	/*
	 * baud rates: 1200, 1800, 2400, 4800, 9600, 19.2k, 38.4k, 57.6k, 115.2k
	 */

	static int clksrc_table[9] =
		/* reg 11: 0x50 = BRG, 0x00 = RTxC, 0x28 = TRxC */
		{ 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x00, 0x00 };
	static int clkmode_table[9] =
		/* reg 4: 0x40 = x16, 0x80 = x32, 0xc0 = x64 */
		{ 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0xc0, 0x80 };
	static int div_table[9] =
		/* reg12 (BRG low) */
		{ 94, 62, 46, 22, 10, 4, 1, 0, 0 };

	int baud = cflag & CBAUD;
	int clksrc, clkmode, div, reg3, reg5;

	if (cflag & CBAUDEX)
		baud += B38400;
	if (baud < B1200 || baud > B38400+2)
		baud = B9600; /* use default 9600bps for non-implemented rates */
	baud -= B1200; /* tables starts at 1200bps */

	clksrc  = clksrc_table[baud];
	clkmode = clkmode_table[baud];
	div     = div_table[baud];

	reg3 = (((cflag & CSIZE) == CS8) ? 0xc0 : 0x40);
	reg5 = (((cflag & CSIZE) == CS8) ? 0x60 : 0x20) | 0x82 /* assert DTR/RTS */;

	if (port == 1) {
		(void)scc.cha_b_ctrl;	/* reset reg pointer */
		SCCB_WRITE(9, 0xc0);	/* reset */
		LONG_DELAY();		/* extra delay after WR9 access */
		SCCB_WRITE(4, (cflag & PARENB) ? ((cflag & PARODD) ? 0x01 : 0x03) : 0 |
			   0x04 /* 1 stopbit */ |
			   clkmode);
		SCCB_WRITE(3, reg3);
		SCCB_WRITE(5, reg5);
		SCCB_WRITE(9, 0);	/* no interrupts */
		LONG_DELAY();		/* extra delay after WR9 access */
		SCCB_WRITE(10, 0);	/* NRZ mode */
		SCCB_WRITE(11, clksrc);	/* main clock source */
		SCCB_WRITE(12, div);	/* BRG value */
		SCCB_WRITE(13, 0);	/* BRG high byte */
		SCCB_WRITE(14, 1);
		SCCB_WRITE(3, reg3 | 1);
		SCCB_WRITE(5, reg5 | 8);
	} else if (port == 0) {
		(void)scc.cha_a_ctrl;	/* reset reg pointer */
		SCCA_WRITE(9, 0xc0);	/* reset */
		LONG_DELAY();		/* extra delay after WR9 access */
		SCCA_WRITE(4, (cflag & PARENB) ? ((cflag & PARODD) ? 0x01 : 0x03) : 0 |
			  0x04 /* 1 stopbit */ |
			  clkmode);
		SCCA_WRITE(3, reg3);
		SCCA_WRITE(5, reg5);
		SCCA_WRITE(9, 0);	/* no interrupts */
		LONG_DELAY();		/* extra delay after WR9 access */
		SCCA_WRITE(10, 0);	/* NRZ mode */
		SCCA_WRITE(11, clksrc);	/* main clock source */
		SCCA_WRITE(12, div);	/* BRG value */
		SCCA_WRITE(13, 0);	/* BRG high byte */
		SCCA_WRITE(14, 1);
		SCCA_WRITE(3, reg3 | 1);
		SCCA_WRITE(5, reg5 | 8);
	}

	mac_SCC_reset_done = 1;
	mac_SCC_init_done = 1;
}
#endif /* DEBUG_SERIAL */

void mac_init_scca_port(int cflag)
{
	mac_init_scc_port(cflag, 0);
}

void mac_init_sccb_port(int cflag)
{
	mac_init_scc_port(cflag, 1);
}

static int __init mac_debug_setup(char *arg)
{
	if (!MACH_IS_MAC)
		return 0;

#ifdef DEBUG_SERIAL
	if (!strcmp(arg, "ser") || !strcmp(arg, "ser1")) {
		/* Mac modem port */
		mac_init_scc_port(B9600|CS8, 0);
		mac_console_driver.write = mac_scca_console_write;
		scc_port = 0;
	} else if (!strcmp(arg, "ser2")) {
		/* Mac printer port */
		mac_init_scc_port(B9600|CS8, 1);
		mac_console_driver.write = mac_sccb_console_write;
		scc_port = 1;
	}
#endif
#ifdef DEBUG_HEADS
	if (!strcmp(arg, "scn") || !strcmp(arg, "con")) {
		/* display, using head.S console routines */
		mac_console_driver.write = mac_debug_console_write;
	}
#endif
	if (mac_console_driver.write)
		register_console(&mac_console_driver);
	return 0;
}

early_param("debug", mac_debug_setup);
