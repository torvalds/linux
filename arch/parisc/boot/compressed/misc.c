/*
 * Definitions and wrapper functions for kernel decompressor
 *
 *   (C) 2017 Helge Deller <deller@gmx.de>
 */

#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <asm/page.h>
#include "sizes.h"

/*
 * gzip declarations
 */
#define STATIC static

#undef memmove
#define memmove memmove
#define memzero(s, n) memset((s), 0, (n))

#define malloc	malloc_gzip
#define free	free_gzip

/* Symbols defined by linker scripts */
extern char input_data[];
extern int input_len;
/* output_len is inserted by the linker possibly at an unaligned address */
extern __le32 output_len __aligned(1);
extern char _text, _end;
extern char _bss, _ebss;
extern char _startcode_end;
extern void startup_continue(void *entry, unsigned long cmdline,
	unsigned long rd_start, unsigned long rd_end) __noreturn;

void error(char *m) __noreturn;

static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZ4
#include "../../../../lib/decompress_unlz4.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

#ifdef CONFIG_KERNEL_LZO
#include "../../../../lib/decompress_unlzo.c"
#endif

#ifdef CONFIG_KERNEL_XZ
#include "../../../../lib/decompress_unxz.c"
#endif

void *memmove(void *dest, const void *src, size_t n)
{
	const char *s = src;
	char *d = dest;

	if (d <= s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dest;
}

void *memset(void *s, int c, size_t count)
{
	char *xs = (char *)s;

	while (count--)
		*xs++ = c;
	return s;
}

void *memcpy(void *d, const void *s, size_t len)
{
	char *dest = (char *)d;
	const char *source = (const char *)s;

	while (len--)
		*dest++ = *source++;
	return d;
}

size_t strlen(const char *s)
{
	const char *sc;

	for (sc = s; *sc != '\0'; ++sc)
		;
	return sc - s;
}

char *strchr(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c)
			return (char *)s;
		++s;
	}
	return NULL;
}

int puts(const char *s)
{
	const char *nuline = s;

	while ((nuline = strchr(s, '\n')) != NULL) {
		if (nuline != s)
			pdc_iodc_print(s, nuline - s);
		pdc_iodc_print("\r\n", 2);
		s = nuline + 1;
	}
	if (*s != '\0')
		pdc_iodc_print(s, strlen(s));

	return 0;
}

static int putchar(int c)
{
	char buf[2];

	buf[0] = c;
	buf[1] = '\0';
	puts(buf);
	return c;
}

void __noreturn error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");
	while (1)	/* wait forever */
		;
}

static int print_hex(unsigned long num)
{
	const char hex[] = "0123456789abcdef";
	char str[40];
	int i = sizeof(str)-1;

	str[i--] = '\0';
	do {
		str[i--] = hex[num & 0x0f];
		num >>= 4;
	} while (num);

	str[i--] = 'x';
	str[i] = '0';
	puts(&str[i]);

	return 0;
}

int printf(const char *fmt, ...)
{
	va_list args;
	int i = 0;

	va_start(args, fmt);

	while (fmt[i]) {
		if (fmt[i] != '%') {
put:
			putchar(fmt[i++]);
			continue;
		}

		if (fmt[++i] == '%')
			goto put;
		++i;
		print_hex(va_arg(args, unsigned long));
	}

	va_end(args);
	return 0;
}

/* helper functions for libgcc */
void abort(void)
{
	error("aborted.");
}

#undef malloc
void *malloc(size_t size)
{
	return malloc_gzip(size);
}

#undef free
void free(void *ptr)
{
	return free_gzip(ptr);
}


static void flush_data_cache(char *start, unsigned long length)
{
	char *end = start + length;

	do {
		asm volatile("fdc 0(%0)" : : "r" (start));
		asm volatile("fic 0(%%sr0,%0)" : : "r" (start));
		start += 16;
	} while (start < end);
	asm volatile("fdc 0(%0)" : : "r" (end));

	asm ("sync");
}

unsigned long decompress_kernel(unsigned int started_wide,
		unsigned int command_line,
		const unsigned int rd_start,
		const unsigned int rd_end)
{
	char *output;
	unsigned long len, len_all;

#ifdef CONFIG_64BIT
	parisc_narrow_firmware = 0;
#endif

	set_firmware_width_unlocked();

	putchar('U');	/* if you get this p and no more, string storage */
			/* in $GLOBAL$ is wrong or %dp is wrong */
	puts("ncompressing ...\n");

	output = (char *) KERNEL_BINARY_TEXT_START;
	len_all = __pa(SZ_end) - __pa(SZparisc_kernel_start);

	if ((unsigned long) &_startcode_end > (unsigned long) output)
		error("Bootcode overlaps kernel code");

	len = get_unaligned_le32(&output_len);
	if (len > len_all)
		error("Output len too big.");
	else
		memset(&output[len], 0, len_all - len);

	/*
	 * Initialize free_mem_ptr and free_mem_end_ptr.
	 */
	free_mem_ptr = (unsigned long) &_ebss;
	free_mem_ptr += 2*1024*1024;	/* leave 2 MB for stack */

	/* Limit memory for bootoader to 1GB */
	#define ARTIFICIAL_LIMIT (1*1024*1024*1024)
	free_mem_end_ptr = PAGE0->imm_max_mem;
	if (free_mem_end_ptr > ARTIFICIAL_LIMIT)
		free_mem_end_ptr = ARTIFICIAL_LIMIT;

#ifdef CONFIG_BLK_DEV_INITRD
	/* if we have ramdisk this is at end of memory */
	if (rd_start && rd_start < free_mem_end_ptr)
		free_mem_end_ptr = rd_start;
#endif

#ifdef DEBUG
	printf("startcode_end = %x\n", &_startcode_end);
	printf("commandline   = %x\n", command_line);
	printf("rd_start      = %x\n", rd_start);
	printf("rd_end        = %x\n", rd_end);

	printf("free_ptr      = %x\n", free_mem_ptr);
	printf("free_ptr_end  = %x\n", free_mem_end_ptr);

	printf("input_data    = %x\n", input_data);
	printf("input_len     = %x\n", input_len);
	printf("output        = %x\n", output);
	printf("output_len    = %x\n", len);
	printf("output_max    = %x\n", len_all);
#endif

	__decompress(input_data, input_len, NULL, NULL,
			output, 0, NULL, error);

	flush_data_cache(output, len);

	printf("Booting kernel ...\n\n");

	return (unsigned long) output;
}
