/*
 * Copyright (C) 1996 Paul Mackerras.
 * Copyright (C) 2000 Dan Malek.
 * Quick hack of Paul's code to make XMON work on 8xx processors.  Lots
 * of assumptions, like the SMC1 is used, it has been initialized by the
 * loader at some point, and we can just stuff and suck bytes.
 * We rely upon the 8xx uart driver to support us, as the interface
 * changes between boot up and operational phases of the kernel.
 */
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/kernel.h>
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>
#include <asm/commproc.h>

extern void xmon_printf(const char *fmt, ...);
extern int xmon_8xx_write(char *str, int nb);
extern int xmon_8xx_read_poll(void);
extern int xmon_8xx_read_char(void);
void prom_drawhex(uint);
void prom_drawstring(const char *str);

static int use_screen = 1; /* default */

#define TB_SPEED	25000000

static inline unsigned int readtb(void)
{
	unsigned int ret;

	asm volatile("mftb %0" : "=r" (ret) :);
	return ret;
}

void buf_access(void)
{
}

void
xmon_map_scc(void)
{

	cpmp = (cpm8xx_t *)&(((immap_t *)IMAP_ADDR)->im_cpm);
	use_screen = 0;
	
	prom_drawstring("xmon uses serial port\n");
}

static int scc_initialized = 0;

void xmon_init_scc(void);

int
xmon_write(void *handle, void *ptr, int nb)
{
	char *p = ptr;
	int i, c, ct;

	if (!scc_initialized)
		xmon_init_scc();

	return(xmon_8xx_write(ptr, nb));
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
		*p++ = xmon_8xx_read_char();
	}
	return i;
}

int
xmon_read_poll(void)
{
	return(xmon_8xx_read_poll());
}

void
xmon_init_scc()
{
	scc_initialized = 1;
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
xmon_init(void)
{
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

#if 0
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
#endif

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
		return 0;
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
prom_drawhex(uint val)
{
	unsigned char buf[10];

	int i;
	for (i = 7;  i >= 0;  i--)
	{
		buf[i] = "0123456789abcdef"[val & 0x0f];
		val >>= 4;
	}
	buf[8] = '\0';
	xmon_fputs(buf, xmon_stdout);
}

void
prom_drawstring(const char *str)
{
	xmon_fputs(str, xmon_stdout);
}
