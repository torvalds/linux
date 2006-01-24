/*
 * Copyright (C) 1996 Paul Mackerras.
 */
#include <linux/config.h>
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/sysrq.h>
#include <linux/bitops.h>
#include <asm/xmon.h>
#include <asm/prom.h>
#include <asm/bootx.h>
#include <asm/machdep.h>
#include <asm/errno.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/btext.h>

static volatile unsigned char *sccc, *sccd;
unsigned int TXRDY, RXRDY, DLAB;
static int xmon_expect(const char *str, unsigned int timeout);

static int use_screen;
static int via_modem;
static int xmon_use_sccb;

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

extern int adb_init(void);

#ifdef CONFIG_PPC_CHRP
/*
 * This looks in the "ranges" property for the primary PCI host bridge
 * to find the physical address of the start of PCI/ISA I/O space.
 * It is basically a cut-down version of pci_process_bridge_OF_ranges.
 */
static unsigned long chrp_find_phys_io_base(void)
{
	struct device_node *node;
	unsigned int *ranges;
	unsigned long base = CHRP_ISA_IO_BASE;
	int rlen = 0;
	int np;

	node = find_devices("isa");
	if (node != NULL) {
		node = node->parent;
		if (node == NULL || node->type == NULL
		    || strcmp(node->type, "pci") != 0)
			node = NULL;
	}
	if (node == NULL)
		node = find_devices("pci");
	if (node == NULL)
		return base;

	ranges = (unsigned int *) get_property(node, "ranges", &rlen);
	np = prom_n_addr_cells(node) + 5;
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		if ((ranges[0] >> 24) == 1 && ranges[2] == 0) {
			/* I/O space starting at 0, grab the phys base */
			base = ranges[np - 3];
			break;
		}
		ranges += np;
	}
	return base;
}
#endif /* CONFIG_PPC_CHRP */

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
#ifdef CONFIG_PPC_MULTIPLATFORM
	volatile unsigned char *base;

#ifdef CONFIG_PPC_CHRP
	base = (volatile unsigned char *) isa_io_base;
	if (_machine == _MACH_chrp)
		base = (volatile unsigned char *)
			ioremap(chrp_find_phys_io_base(), 0x1000);

	sccc = base + 0x3fd;
	sccd = base + 0x3f8;
	if (xmon_use_sccb) {
		sccc -= 0x100;
		sccd -= 0x100;
	}
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#endif /* CONFIG_PPC_CHRP */
#elif defined(CONFIG_GEMINI)
	/* should already be mapped by the kernel boot */
	sccc = (volatile unsigned char *) 0xffeffb0d;
	sccd = (volatile unsigned char *) 0xffeffb08;
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#elif defined(CONFIG_405GP)
	sccc = (volatile unsigned char *)0xef600305;
	sccd = (volatile unsigned char *)0xef600300;
	TXRDY = 0x20;
	RXRDY = 1;
	DLAB = 0x80;
#endif /* platform */

	register_sysrq_key('x', &sysrq_xmon_op);
}

static int scc_initialized = 0;

void xmon_init_scc(void);
extern void cuda_poll(void);

static inline void do_poll_adb(void)
{
#ifdef CONFIG_ADB_PMU
	if (sys_ctrler == SYS_CTRLER_PMU)
		pmu_poll_adb();
#endif /* CONFIG_ADB_PMU */
#ifdef CONFIG_ADB_CUDA
	if (sys_ctrler == SYS_CTRLER_CUDA)
		cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}

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

#ifdef CONFIG_BOOTX_TEXT
	if (use_screen) {
		/* write it on the screen */
		for (i = 0; i < nb; ++i)
			btext_drawchar(*p++);
		goto out;
	}
#endif
	if (!scc_initialized)
		xmon_init_scc();
	ct = 0;
	for (i = 0; i < nb; ++i) {
		while ((*sccc & TXRDY) == 0)
			do_poll_adb();
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

 out:
#ifdef CONFIG_SMP
	if (!locked)
		clear_bit(0, &xmon_write_lock);
#endif
	return nb;
}

int xmon_wants_key;
int xmon_adb_keycode;

#ifdef CONFIG_BOOTX_TEXT
static int xmon_adb_shiftstate;

static unsigned char xmon_keytab[128] =
	"asdfhgzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80]o"				/* 0x10 - 0x1f */
	"u[ip\rlj'k;\\,/nm."				/* 0x20 - 0x2f */
	"\t `\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static unsigned char xmon_shift_keytab[128] =
	"ASDFHGZXCV\000BQWER"				/* 0x00 - 0x0f */
	"YT!@#$^%+(&_*)}O"				/* 0x10 - 0x1f */
	"U{IP\rLJ\"K:|<?NM>"				/* 0x20 - 0x2f */
	"\t ~\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static int
xmon_get_adb_key(void)
{
	int k, t, on;

	xmon_wants_key = 1;
	for (;;) {
		xmon_adb_keycode = -1;
		t = 0;
		on = 0;
		do {
			if (--t < 0) {
				on = 1 - on;
				btext_drawchar(on? 0xdb: 0x20);
				btext_drawchar('\b');
				t = 200000;
			}
			do_poll_adb();
		} while (xmon_adb_keycode == -1);
		k = xmon_adb_keycode;
		if (on)
			btext_drawstring(" \b");

		/* test for shift keys */
		if ((k & 0x7f) == 0x38 || (k & 0x7f) == 0x7b) {
			xmon_adb_shiftstate = (k & 0x80) == 0;
			continue;
		}
		if (k >= 0x80)
			continue;	/* ignore up transitions */
		k = (xmon_adb_shiftstate? xmon_shift_keytab: xmon_keytab)[k];
		if (k != 0)
			break;
	}
	xmon_wants_key = 0;
	return k;
}
#endif /* CONFIG_BOOTX_TEXT */

int
xmon_read(void *handle, void *ptr, int nb)
{
    char *p = ptr;
    int i;

#ifdef CONFIG_BOOTX_TEXT
    if (use_screen) {
	for (i = 0; i < nb; ++i)
	    *p++ = xmon_get_adb_key();
	return i;
    }
#endif
    if (!scc_initialized)
	xmon_init_scc();
    for (i = 0; i < nb; ++i) {
	while ((*sccc & RXRDY) == 0)
	    do_poll_adb();
	buf_access();
	*p++ = *sccd;
    }
    return i;
}

int
xmon_read_poll(void)
{
	if ((*sccc & RXRDY) == 0) {
		do_poll_adb();
		return -1;
	}
	buf_access();
	return *sccd;
}

void
xmon_init_scc(void)
{
	if ( _machine == _MACH_chrp )
	{
		sccd[3] = 0x83; eieio();	/* LCR = 8N1 + DLAB */
		sccd[0] = 12; eieio();		/* DLL = 9600 baud */
		sccd[1] = 0; eieio();
		sccd[2] = 0; eieio();		/* FCR = 0 */
		sccd[3] = 3; eieio();		/* LCR = 8N1 */
		sccd[1] = 0; eieio();		/* IER = 0 */
	}
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

#if 0
extern int (*prom_entry)(void *);

int
xmon_exit(void)
{
    struct prom_args {
	char *service;
    } args;

    for (;;) {
	args.service = "exit";
	(*prom_entry)(&args);
    }
}
#endif

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
