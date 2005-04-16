/*
 * arch/ppc/boot/common/misc-common.c
 *
 * Misc. bootloader code (almost) all platforms can use
 *
 * Author: Johnnie Peters <jpeters@mvista.com>
 * Editor: Tom Rini <trini@mvista.com>
 *
 * Derived from arch/ppc/boot/prep/misc.c
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <stdarg.h>	/* for va_ bits */
#include <linux/config.h>
#include <linux/string.h>
#include <linux/zlib.h>
#include "nonstdio.h"

/* If we're on a PReP, assume we have a keyboard controller
 * Also note, if we're not PReP, we assume you are a serial
 * console - Tom */
#if defined(CONFIG_PPC_PREP) && defined(CONFIG_VGA_CONSOLE)
extern void cursor(int x, int y);
extern void scroll(void);
extern char *vidmem;
extern int lines, cols;
extern int orig_x, orig_y;
extern int keyb_present;
extern int CRT_tstc(void);
extern int CRT_getc(void);
#else
int cursor(int x, int y) {return 0;}
void scroll(void) {}
char vidmem[1];
#define lines 0
#define cols 0
int orig_x = 0;
int orig_y = 0;
#define keyb_present 0
int CRT_tstc(void) {return 0;}
int CRT_getc(void) {return 0;}
#endif

extern char *avail_ram;
extern char *end_avail;
extern char _end[];

void puts(const char *);
void putc(const char c);
void puthex(unsigned long val);
void gunzip(void *, int, unsigned char *, int *);
static int _cvt(unsigned long val, char *buf, long radix, char *digits);

void _vprintk(void(*putc)(const char), const char *fmt0, va_list ap);
unsigned char *ISA_io = NULL;

#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE)
extern unsigned long com_port;

extern int serial_tstc(unsigned long com_port);
extern unsigned char serial_getc(unsigned long com_port);
extern void serial_putc(unsigned long com_port, unsigned char c);
#endif

void pause(void)
{
	puts("pause\n");
}

void exit(void)
{
	puts("exit\n");
	while(1);
}

int tstc(void)
{
#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE)
	if(keyb_present)
		return (CRT_tstc() || serial_tstc(com_port));
	else
		return (serial_tstc(com_port));
#else
	return CRT_tstc();
#endif
}

int getc(void)
{
	while (1) {
#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE)
		if (serial_tstc(com_port))
			return (serial_getc(com_port));
#endif /* serial console */
		if (keyb_present)
			if(CRT_tstc())
				return (CRT_getc());
	}
}

void
putc(const char c)
{
	int x,y;

#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE)
	serial_putc(com_port, c);
	if ( c == '\n' )
		serial_putc(com_port, '\r');
#endif /* serial console */

	x = orig_x;
	y = orig_y;

	if ( c == '\n' ) {
		x = 0;
		if ( ++y >= lines ) {
			scroll();
			y--;
		}
	} else if (c == '\r') {
		x = 0;
	} else if (c == '\b') {
		if (x > 0) {
			x--;
		}
	} else {
		vidmem [ ( x + cols * y ) * 2 ] = c;
		if ( ++x >= cols ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		}
	}

	cursor(x, y);

	orig_x = x;
	orig_y = y;
}

void puts(const char *s)
{
	int x,y;
	char c;

	x = orig_x;
	y = orig_y;

	while ( ( c = *s++ ) != '\0' ) {
#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE)
	        serial_putc(com_port, c);
	        if ( c == '\n' ) serial_putc(com_port, '\r');
#endif /* serial console */

		if ( c == '\n' ) {
			x = 0;
			if ( ++y >= lines ) {
				scroll();
				y--;
			}
		} else if (c == '\b') {
		  if (x > 0) {
		    x--;
		  }
		} else {
			vidmem [ ( x + cols * y ) * 2 ] = c;
			if ( ++x >= cols ) {
				x = 0;
				if ( ++y >= lines ) {
					scroll();
					y--;
				}
			}
		}
	}

	cursor(x, y);

	orig_x = x;
	orig_y = y;
}

void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

static void *zalloc(unsigned size)
{
	void *p = avail_ram;

	size = (size + 7) & -8;
	avail_ram += size;
	if (avail_ram > end_avail) {
		puts("oops... out of memory\n");
		pause();
	}
	return p;
}

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;

	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != Z_DEFLATED || (flags & RESERVED) != 0) {
		puts("bad gzipped data\n");
		exit();
	}
	if ((flags & EXTRA_FIELD) != 0)
		i = 12 + src[10] + (src[11] << 8);
	if ((flags & ORIG_NAME) != 0)
		while (src[i++] != 0)
			;
	if ((flags & COMMENT) != 0)
		while (src[i++] != 0)
			;
	if ((flags & HEAD_CRC) != 0)
		i += 2;
	if (i >= *lenp) {
		puts("gunzip: ran out of data in header\n");
		exit();
	}

	/* Initialize ourself. */
	s.workspace = zalloc(zlib_inflate_workspacesize());
	r = zlib_inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		puts("zlib_inflateInit2 returned "); puthex(r); puts("\n");
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = zlib_inflate(&s, Z_FINISH);
	if (r != Z_OK && r != Z_STREAM_END) {
		puts("inflate returned "); puthex(r); puts("\n");
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	zlib_inflateEnd(&s);
}

void
puthex(unsigned long val)
{

	unsigned char buf[10];
	int i;
	for (i = 7;  i >= 0;  i--)
	{
		buf[i] = "0123456789ABCDEF"[val & 0x0F];
		val >>= 4;
	}
	buf[8] = '\0';
	puts(buf);
}

#define FALSE 0
#define TRUE  1

void
_printk(char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_vprintk(putc, fmt, ap);
	va_end(ap);
	return;
}

#define is_digit(c) ((c >= '0') && (c <= '9'))

void
_vprintk(void(*putc)(const char), const char *fmt0, va_list ap)
{
	char c, sign, *cp = 0;
	int left_prec, right_prec, zero_fill, length = 0, pad, pad_on_right;
	char buf[32];
	long val;
	while ((c = *fmt0++))
	{
		if (c == '%')
		{
			c = *fmt0++;
			left_prec = right_prec = pad_on_right = 0;
			if (c == '-')
			{
				c = *fmt0++;
				pad_on_right++;
			}
			if (c == '0')
			{
				zero_fill = TRUE;
				c = *fmt0++;
			} else
			{
				zero_fill = FALSE;
			}
			while (is_digit(c))
			{
				left_prec = (left_prec * 10) + (c - '0');
				c = *fmt0++;
			}
			if (c == '.')
			{
				c = *fmt0++;
				zero_fill++;
				while (is_digit(c))
				{
					right_prec = (right_prec * 10) + (c - '0');
					c = *fmt0++;
				}
			} else
			{
				right_prec = left_prec;
			}
			sign = '\0';
			switch (c)
			{
			case 'd':
			case 'x':
			case 'X':
				val = va_arg(ap, long);
				switch (c)
				{
				case 'd':
					if (val < 0)
					{
						sign = '-';
						val = -val;
					}
					length = _cvt(val, buf, 10, "0123456789");
					break;
				case 'x':
					length = _cvt(val, buf, 16, "0123456789abcdef");
					break;
				case 'X':
					length = _cvt(val, buf, 16, "0123456789ABCDEF");
					break;
				}
				cp = buf;
				break;
			case 's':
				cp = va_arg(ap, char *);
				length = strlen(cp);
				break;
			case 'c':
				c = va_arg(ap, long /*char*/);
				(*putc)(c);
				continue;
			default:
				(*putc)('?');
			}
			pad = left_prec - length;
			if (sign != '\0')
			{
				pad--;
			}
			if (zero_fill)
			{
				c = '0';
				if (sign != '\0')
				{
					(*putc)(sign);
					sign = '\0';
				}
			} else
			{
				c = ' ';
			}
			if (!pad_on_right)
			{
				while (pad-- > 0)
				{
					(*putc)(c);
				}
			}
			if (sign != '\0')
			{
				(*putc)(sign);
			}
			while (length-- > 0)
			{
				(*putc)(c = *cp++);
				if (c == '\n')
				{
					(*putc)('\r');
				}
			}
			if (pad_on_right)
			{
				while (pad-- > 0)
				{
					(*putc)(c);
				}
			}
		} else
		{
			(*putc)(c);
			if (c == '\n')
			{
				(*putc)('\r');
			}
		}
	}
}

int
_cvt(unsigned long val, char *buf, long radix, char *digits)
{
	char temp[80];
	char *cp = temp;
	int length = 0;
	if (val == 0)
	{ /* Special case */
		*cp++ = '0';
	} else
		while (val)
		{
			*cp++ = digits[val % radix];
			val /= radix;
		}
	while (cp != temp)
	{
		*buf++ = *--cp;
		length++;
	}
	*buf = '\0';
	return (length);
}

void
_dump_buf_with_offset(unsigned char *p, int s, unsigned char *base)
{
	int i, c;
	if ((unsigned int)s > (unsigned int)p)
	{
		s = (unsigned int)s - (unsigned int)p;
	}
	while (s > 0)
	{
		if (base)
		{
			_printk("%06X: ", (int)p - (int)base);
		} else
		{
			_printk("%06X: ", p);
		}
		for (i = 0;  i < 16;  i++)
		{
			if (i < s)
			{
				_printk("%02X", p[i] & 0xFF);
			} else
			{
				_printk("  ");
			}
			if ((i % 2) == 1) _printk(" ");
			if ((i % 8) == 7) _printk(" ");
		}
		_printk(" |");
		for (i = 0;  i < 16;  i++)
		{
			if (i < s)
			{
				c = p[i] & 0xFF;
				if ((c < 0x20) || (c >= 0x7F)) c = '.';
			} else
			{
				c = ' ';
			}
			_printk("%c", c);
		}
		_printk("|\n");
		s -= 16;
		p += 16;
	}
}

void
_dump_buf(unsigned char *p, int s)
{
	_printk("\n");
	_dump_buf_with_offset(p, s, 0);
}

/* Very simple inb/outb routines.  We declare ISA_io to be 0 above, and
 * then modify it on platforms which need to.  We do it like this
 * because on some platforms we give inb/outb an exact location, and
 * on others it's an offset from a given location. -- Tom
 */

void ISA_init(unsigned long base)
{
	ISA_io = (unsigned char *)base;
}

void
outb(int port, unsigned char val)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	ISA_io[port] = val;
}

unsigned char
inb(int port)
{
	/* Ensure I/O operations complete */
	__asm__ volatile("eieio");
	return (ISA_io[port]);
}

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
