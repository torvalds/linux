/*
 * arch/sh/boot/compressed/misc_64.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SHmedia from sh by Stuart Menefy, May 2002
 */

#include <asm/uaccess.h>

/* cache.c */
#define CACHE_ENABLE      0
#define CACHE_DISABLE     1
int cache_control(unsigned int command);

/*
 * gzip declarations
 */

#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)     memset ((s), 0, (n))

static void error(char *m);

extern char input_data[];
extern int input_len;

static unsigned char *output_data;

static void puts(const char *);

extern int _text;		/* Defined in vmlinux.lds.S */
extern int _end;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#ifdef CONFIG_HAVE_KERNEL_BZIP2
#define HEAP_SIZE	0x400000
#else
#define HEAP_SIZE	0x10000
#endif

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#include "../../../../lib/decompress_unlzma.c"
#endif

void puts(const char *s)
{
}

void *memset(void *s, int c, size_t n)
{
	int i;
	char *ss = (char *) s;

	for (i = 0; i < n; i++)
		ss[i] = c;
	return s;
}

void *memcpy(void *__dest, __const void *__src, size_t __n)
{
	int i;
	char *d = (char *) __dest, *s = (char *) __src;

	for (i = 0; i < __n; i++)
		d[i] = s[i];
	return __dest;
}

static void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while (1) ;		/* Halt */
}

#define STACK_SIZE (4096)
long __attribute__ ((aligned(8))) user_stack[STACK_SIZE];
long *stack_start = &user_stack[STACK_SIZE];

void decompress_kernel(void)
{
	output_data = (unsigned char *) (CONFIG_MEMORY_START + 0x2000);
	free_mem_ptr = (unsigned long) &_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	puts("Uncompressing Linux... ");
	cache_control(CACHE_ENABLE);
	decompress(input_data, input_len, NULL, NULL, output_data, NULL, error);
	cache_control(CACHE_DISABLE);
	puts("Ok, booting the kernel.\n");
}
