/*
 * arch/sh/boot/compressed/misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SH by Stuart Menefy, Aug 1999
 *
 * Modified to use standard LinuxSH BIOS by Greg Banks 7Jul2000
 */

#include <asm/uaccess.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/sh_bios.h>

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
static unsigned char *output;

static void error(char *m);

int puts(const char *);

extern int _text;		/* Defined in vmlinux.lds.S */
extern int _end;
static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#define HEAP_SIZE             0x10000

#ifdef CONFIG_KERNEL_GZIP
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_SH_STANDARD_BIOS
size_t strlen(const char *s)
{
	int i = 0;

	while (*s++)
		i++;
	return i;
}

int puts(const char *s)
{
	int len = strlen(s);
	sh_bios_console_write(s, len);
	return len;
}
#else
int puts(const char *s)
{
	/* This should be updated to use the sh-sci routines */
	return 0;
}
#endif

void* memset(void* s, int c, size_t n)
{
	int i;
	char *ss = (char*)s;

	for (i=0;i<n;i++) ss[i] = c;
	return s;
}

void* memcpy(void* __dest, __const void* __src,
			    size_t __n)
{
	int i;
	char *d = (char *)__dest, *s = (char *)__src;

	for (i=0;i<__n;i++) d[i] = s[i];
	return __dest;
}

static void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

#define STACK_SIZE (4096)
long user_stack [STACK_SIZE];
long* stack_start = &user_stack[STACK_SIZE];

void decompress_kernel(void)
{
	unsigned long output_addr;

	output_addr = PHYSADDR((unsigned long)&_text+PAGE_SIZE);
#ifdef CONFIG_29BIT
	output_addr |= P2SEG;
#endif

	output = (unsigned char *)output_addr;
	free_mem_ptr = (unsigned long)&_end;
	free_mem_end_ptr = free_mem_ptr + HEAP_SIZE;

	puts("Uncompressing Linux... ");
	decompress(input_data, input_len, NULL, NULL, output, NULL, error);
	puts("Ok, booting the kernel.\n");
}
