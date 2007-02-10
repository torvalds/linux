/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sysrq.h>
#include <linux/bitops.h>
#include <asm/xmon.h>
#include <asm/machdep.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/btext.h>
#include <asm/ibm4xx.h>

static volatile unsigned char *sccc, *sccd;
unsigned int TXRDY, RXRDY, DLAB;
static int xmon_expect(const char *str, unsigned int timeout);

static int via_modem;

#define TB_SPEED	25000000

static inline unsigned int readtb(void)
{
	unsigned int ret;

	asm volatile("mftb %0" : "=r" (ret) :);
	return ret;
}

void buf_access(void)
{
	if (DLAB)
		sccd[3] &= ~DLAB;	/* reset DLAB */
}


#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_handle_xmon(int key, struct pt_regs *regs,
			      struct tty_struct *tty)
{
	xmon(regs);
}

static struct sysrq_key_op sysrq_xmon_op =
{
	.handler =	sysrq_handle_xmon,
	.help_msg =	"Xmon",
	.action_msg =	"Entering xmon",
};
#endif

void
xmon_map_scc(void)
{
#if defined(CONFIG_405GP)
	sccd = (volatile unsigned char *)0xef600300;
#elif defined(CONFIG_440EP)
	sccd = (volatile unsigned char *) ioremap(PPC440EP_UART0_ADDR, 8);
#elif defined(CONFIG_440SP)
	sccd = (volatile unsigned char *) ioremap64(PPC440SP_UART0_ADDR, 8);
#elif defined(CONFIG_440SPE)
	sccd = (volatile unsigned char *) ioremap64(PPC440SPE_UART0_ADDR, 8);
#elif defined(CONFIG_44x)
	/* This is the default for 44x platforms.  Any boards that have a
	   different UART address need to be put in cases before this or the
	   port will be mapped incorrectly */
	sccd = (volatile unsigned char *) ioremap64(PPC440GP_UART0_ADDR, 8);
#endif /* platform */

#ifndef CONFIG_PPC_PREP
	sccc = sccd + 5;
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#endif

	register_sysrq_key('x', &sysrq_xmon_op);
}

static int scc_initialized;

void xmon_init_scc(void);

int
xmon_write(void *handle, void *ptr, int nb)
{
	char *p = ptr;
	int i, c, ct;

#ifdef CONFIG_SMP
	static unsigned long xmon_write_lock;
	int lock_wait = 1000000;
	int locked;

	while ((locked = test_and_set_bit(0, &xmon_write_lock)) != 0)
		if (--lock_wait == 0)
			break;
#endif

	if (!scc_initialized)
		xmon_init_scc();
	ct = 0;
	for (i = 0; i < nb; ++i) {
		while ((*sccc & TXRDY) == 0)
			;
		c = p[i];
		if (c == '\n' && !ct) {
			c = '\r';
			ct = 1;
			--i;
		} else {
			ct = 0;
		}
		buf_access();
		*sccd = c;
		eieio();
	}

#ifdef CONFIG_SMP
	if (!locked)
		clear_bit(0, &xmon_write_lock);
#endif
	return nb;
}

int xmon_wants_key;


int
xmon_read(void *handle, void *ptr, int nb)
{
    char *p = ptr;
    int i;

    if (!scc_initialized)
	xmon_init_scc();
    for (i = 0; i < nb; ++i) {
	while ((*sccc & RXRDY) == 0)
	    ;
	buf_access();
	*p++ = *sccd;
    }
    return i;
}

int
xmon_read_poll(void)
{
	if ((*sccc & RXRDY) == 0) {
		;
		return -1;
	}
	buf_access();
	return *sccd;
}

void
xmon_init_scc(void)
{
	scc_initialized = 1;
	if (via_modem) {
		for (;;) {
			xmon_write(NULL, "ATE1V1\r", 7);
			if (xmon_expect("OK", 5)) {
				xmon_write(NULL, "ATA\r", 4);
				if (xmon_expect("CONNECT", 40))
					break;
			}
			xmon_write(NULL, "+++", 3);
			xmon_expect("OK", 3);
		}
	}
}


void *xmon_stdin;
void *xmon_stdout;
void *xmon_stderr;

void
xmon_init(int arg)
{
	xmon_map_scc();
}

int
xmon_putc(int c, void *f)
{
    char ch = c;

    if (c == '\n')
	xmon_putc('\r', f);
    return xmon_write(f, &ch, 1) == 1? c: -1;
}

int
xmon_putchar(int c)
{
    return xmon_putc(c, xmon_stdout);
}

int
xmon_fputs(char *str, void *f)
{
    int n = strlen(str);

    return xmon_write(f, str, n) == n? 0: -1;
}

int
xmon_readchar(void)
{
    char ch;

    for (;;) {
	switch (xmon_read(xmon_stdin, &ch, 1)) {
	case 1:
	    return ch;
	case -1:
	    xmon_printf("read(stdin) returned -1\r\n", 0, 0);
	    return -1;
	}
    }
}

static char line[256];
static char *lineptr;
static int lineleft;

int xmon_expect(const char *str, unsigned int timeout)
{
	int c;
	unsigned int t0;

	timeout *= TB_SPEED;
	t0 = readtb();
	do {
		lineptr = line;
		for (;;) {
			c = xmon_read_poll();
			if (c == -1) {
				if (readtb() - t0 > timeout)
					return 0;
				continue;
			}
			if (c == '\n')
				break;
			if (c != '\r' && lineptr < &line[sizeof(line) - 1])
				*lineptr++ = c;
		}
		*lineptr = 0;
	} while (strstr(line, str) == NULL);
	return 1;
}

int
xmon_getchar(void)
{
    int c;

    if (lineleft == 0) {
	lineptr = line;
	for (;;) {
	    c = xmon_readchar();
	    if (c == -1 || c == 4)
		break;
	    if (c == '\r' || c == '\n') {
		*lineptr++ = '\n';
		xmon_putchar('\n');
		break;
	    }
	    switch (c) {
	    case 0177:
	    case '\b':
		if (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    case 'U' & 0x1F:
		while (lineptr > line) {
		    xmon_putchar('\b');
		    xmon_putchar(' ');
		    xmon_putchar('\b');
		    --lineptr;
		}
		break;
	    default:
		if (lineptr >= &line[sizeof(line) - 1])
		    xmon_putchar('\a');
		else {
		    xmon_putchar(c);
		    *lineptr++ = c;
		}
	    }
	}
	lineleft = lineptr - line;
	lineptr = line;
    }
    if (lineleft == 0)
	return -1;
    --lineleft;
    return *lineptr++;
}

char *
xmon_fgets(char *str, int nb, void *f)
{
    char *p;
    int c;

    for (p = str; p < str + nb - 1; ) {
	c = xmon_getchar();
	if (c == -1) {
	    if (p == str)
		return NULL;
	    break;
	}
	*p++ = c;
	if (c == '\n')
	    break;
    }
    *p = 0;
    return str;
}

void
xmon_enter(void)
{
}

void
xmon_leave(void)
{
}
