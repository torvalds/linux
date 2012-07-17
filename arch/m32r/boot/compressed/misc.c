/*
 * arch/m32r/boot/compressed/misc.c
 *
 * This is a collection of several routines from gzip-1.0.3
 * adapted for Linux.
 *
 * malloc by Hannu Savolainen 1993 and Matthias Urlichs 1994
 *
 * Adapted for SH by Stuart Menefy, Aug 1999
 *
 * 2003-02-12:	Support M32R by Takeo Takahashi
 */

/*
 * gzip declarations
 */
#define STATIC static

#undef memset
#undef memcpy
#define memzero(s, n)     memset ((s), 0, (n))

static void error(char *m);

#include "m32r_sio.c"

static unsigned long free_mem_ptr;
static unsigned long free_mem_end_ptr;

#ifdef CONFIG_KERNEL_BZIP2
static void *memset(void *s, int c, size_t n)
{
	char *ss = s;

	while (n--)
	       	*ss++ = c;
	return s;
}
#endif

#ifdef CONFIG_KERNEL_GZIP
void *memcpy(void *dest, const void *src, size_t n)
{
	char *d = dest;
	const char *s = src;
	while (n--)
		*d++ = *s++;

	return dest;
}

#define BOOT_HEAP_SIZE             0x10000
#include "../../../../lib/decompress_inflate.c"
#endif

#ifdef CONFIG_KERNEL_BZIP2
#define BOOT_HEAP_SIZE             0x400000
#include "../../../../lib/decompress_bunzip2.c"
#endif

#ifdef CONFIG_KERNEL_LZMA
#define BOOT_HEAP_SIZE             0x10000
#include "../../../../lib/decompress_unlzma.c"
#endif

static void error(char *x)
{
	puts("\n\n");
	puts(x);
	puts("\n\n -- System halted");

	while(1);	/* Halt */
}

void
decompress_kernel(int mmu_on, unsigned char *zimage_data,
		  unsigned int zimage_len, unsigned long heap)
{
	unsigned char *input_data = zimage_data;
	int input_len = zimage_len;
	unsigned char *output_data;

	output_data = (unsigned char *)CONFIG_MEMORY_START + 0x2000
		+ (mmu_on ? 0x80000000 : 0);
	free_mem_ptr = heap;
	free_mem_end_ptr = free_mem_ptr + BOOT_HEAP_SIZE;

	puts("\nDecompressing Linux... ");
	decompress(input_data, input_len, NULL, NULL, output_data, NULL, error);
	puts("done.\nBooting the kernel.\n");
}
