/*
 * Definitions and wrapper functions for kernel decompressor
 *
 *   (C) 2017 Helge Deller <deller@gmx.de>
 */

#include <linux/uaccess.h>
#include <linux/elf.h>
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
extern char output_len;
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

static int puts(const char *s)
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
	if (x) puts(x);
	puts("\n -- System halted\n");
	while (1)	/* wait forever */
		;
}

static int print_num(unsigned long num, int base)
{
	const char hex[] = "0123456789abcdef";
	char str[40];
	int i = sizeof(str)-1;

	str[i--] = '\0';
	do {
		str[i--] = hex[num % base];
		num = num / base;
	} while (num);

	if (base == 16) {
		str[i--] = 'x';
		str[i] = '0';
	} else i++;
	puts(&str[i]);

	return 0;
}

static int printf(const char *fmt, ...)
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
		print_num(va_arg(args, unsigned long),
			fmt[i] == 'x' ? 16:10);
		++i;
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
static void *malloc(size_t size)
{
	return malloc_gzip(size);
}

#undef free
static void free(void *ptr)
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

static void parse_elf(void *output)
{
#ifdef CONFIG_64BIT
	Elf64_Ehdr ehdr;
	Elf64_Phdr *phdrs, *phdr;
#else
	Elf32_Ehdr ehdr;
	Elf32_Phdr *phdrs, *phdr;
#endif
	void *dest;
	int i;

	memcpy(&ehdr, output, sizeof(ehdr));
	if (ehdr.e_ident[EI_MAG0] != ELFMAG0 ||
	   ehdr.e_ident[EI_MAG1] != ELFMAG1 ||
	   ehdr.e_ident[EI_MAG2] != ELFMAG2 ||
	   ehdr.e_ident[EI_MAG3] != ELFMAG3) {
		error("Kernel is not a valid ELF file");
		return;
	}

#ifdef DEBUG
	printf("Parsing ELF... ");
#endif

	phdrs = malloc(sizeof(*phdrs) * ehdr.e_phnum);
	if (!phdrs)
		error("Failed to allocate space for phdrs");

	memcpy(phdrs, output + ehdr.e_phoff, sizeof(*phdrs) * ehdr.e_phnum);

	for (i = 0; i < ehdr.e_phnum; i++) {
		phdr = &phdrs[i];

		switch (phdr->p_type) {
		case PT_LOAD:
			dest = (void *)((unsigned long) phdr->p_paddr &
					(__PAGE_OFFSET_DEFAULT-1));
			memmove(dest, output + phdr->p_offset, phdr->p_filesz);
			break;
		default:
			break;
		}
	}

	free(phdrs);
}

asmlinkage unsigned long __visible decompress_kernel(unsigned int started_wide,
		unsigned int command_line,
		const unsigned int rd_start,
		const unsigned int rd_end)
{
	char *output;
	unsigned long vmlinux_addr, vmlinux_len;
	unsigned long kernel_addr, kernel_len;

#ifdef CONFIG_64BIT
	parisc_narrow_firmware = 0;
#endif

	set_firmware_width_unlocked();

	putchar('D');	/* if you get this D and no more, string storage */
			/* in $GLOBAL$ is wrong or %dp is wrong */
	puts("ecompressing Linux... ");

	/* where the final bits are stored */
	kernel_addr = KERNEL_BINARY_TEXT_START;
	kernel_len = __pa(SZ_end) - __pa(SZparisc_kernel_start);
	if ((unsigned long) &_startcode_end > kernel_addr)
		error("Bootcode overlaps kernel code");

	/*
	 * Calculate addr to where the vmlinux ELF file shall be decompressed.
	 * Assembly code in head.S positioned the stack directly behind bss, so
	 * leave 2 MB for the stack.
	 */
	vmlinux_addr = (unsigned long) &_ebss + 2*1024*1024;
	vmlinux_len = get_unaligned_le32(&output_len);
	output = (char *) vmlinux_addr;

	/*
	 * Initialize free_mem_ptr and free_mem_end_ptr.
	 */
	free_mem_ptr = vmlinux_addr + vmlinux_len;

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

	if (free_mem_ptr >= free_mem_end_ptr) {
		int free_ram;
		free_ram = (free_mem_ptr >> 20) + 1;
		if (free_ram < 32)
			free_ram = 32;
		printf("\nKernel requires at least %d MB RAM.\n",
			free_ram);
		error(NULL);
	}

#ifdef DEBUG
	printf("\n");
	printf("startcode_end = %x\n", &_startcode_end);
	printf("commandline   = %x\n", command_line);
	printf("rd_start      = %x\n", rd_start);
	printf("rd_end        = %x\n", rd_end);

	printf("free_ptr      = %x\n", free_mem_ptr);
	printf("free_ptr_end  = %x\n", free_mem_end_ptr);

	printf("input_data    = %x\n", input_data);
	printf("input_len     = %x\n", input_len);
	printf("output        = %x\n", output);
	printf("output_len    = %x\n", vmlinux_len);
	printf("kernel_addr   = %x\n", kernel_addr);
	printf("kernel_len    = %x\n", kernel_len);
#endif

	__decompress(input_data, input_len, NULL, NULL,
			output, 0, NULL, error);
	parse_elf(output);

	output = (char *) kernel_addr;
	flush_data_cache(output, kernel_len);

	printf("done.\nBooting the kernel.\n");

	return (unsigned long) output;
}
