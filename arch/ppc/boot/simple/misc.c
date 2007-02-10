/*
 * Misc. bootloader code for many machines.  This assumes you have are using
 * a 6xx/7xx/74xx CPU in your machine.  This assumes the chunk of memory
 * below 8MB is free.  Finally, it assumes you have a NS16550-style uart for
 * your serial console.  If a machine meets these requirements, it can quite
 * likely use this code during boot.
 *
 * Author: Matt Porter <mporter@mvista.com>
 * Derived from arch/ppc/boot/prep/misc.c
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/types.h>
#include <linux/string.h>

#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/bootinfo.h>
#ifdef CONFIG_4xx
#include <asm/ibm4xx.h>
#endif
#include <asm/reg.h>

#include "nonstdio.h"

/* Default cmdline */
#ifdef CONFIG_CMDLINE
#define CMDLINE CONFIG_CMDLINE
#else
#define CMDLINE ""
#endif

/* Keyboard (and VGA console)? */
#ifdef CONFIG_VGA_CONSOLE
#define HAS_KEYB 1
#else
#define HAS_KEYB 0
#endif

/* Will / Can the user give input?
 */
#if (defined(CONFIG_SERIAL_8250_CONSOLE) \
	|| defined(CONFIG_VGA_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPC52xx_CONSOLE) \
	|| defined(CONFIG_SERIAL_MPSC_CONSOLE))
#define INTERACTIVE_CONSOLE	1
#endif

char *avail_ram;
char *end_avail;
char *zimage_start;
char cmd_preset[] = CMDLINE;
char cmd_buf[256];
char *cmd_line = cmd_buf;
int keyb_present = HAS_KEYB;
int zimage_size;

unsigned long com_port;
unsigned long initrd_size = 0;

/* The linker tells us various locations in the image */
extern char __image_begin, __image_end;
extern char __ramdisk_begin, __ramdisk_end;
extern char _end[];
/* Original location */
extern unsigned long start;

extern int CRT_tstc(void);
extern unsigned long serial_init(int chan, void *ignored);
extern void serial_close(unsigned long com_port);
extern void gunzip(void *, int, unsigned char *, int *);
extern void serial_fixups(void);

/* Allow get_mem_size to be hooked into.  This is the default. */
unsigned long __attribute__ ((weak))
get_mem_size(void)
{
	return 0;
}

#if defined(CONFIG_40x)
#define PPC4xx_EMAC0_MR0	EMAC0_BASE
#endif

#if defined(CONFIG_44x) && defined(PPC44x_EMAC0_MR0)
#define PPC4xx_EMAC0_MR0	PPC44x_EMAC0_MR0
#endif

struct bi_record *
decompress_kernel(unsigned long load_addr, int num_words, unsigned long cksum)
{
#ifdef INTERACTIVE_CONSOLE
	int timer = 0;
	char ch;
#endif
	char *cp;
	struct bi_record *rec;
	unsigned long initrd_loc = 0, TotalMemory = 0;

#if defined(CONFIG_SERIAL_8250_CONSOLE) || defined(CONFIG_SERIAL_MPSC_CONSOLE)
	com_port = serial_init(0, NULL);
#endif

#if defined(PPC4xx_EMAC0_MR0)
	/* Reset MAL */
	mtdcr(DCRN_MALCR(DCRN_MAL_BASE), MALCR_MMSR);
	/* Wait for reset */
	while (mfdcr(DCRN_MALCR(DCRN_MAL_BASE)) & MALCR_MMSR) {};
	/* Reset EMAC */
	*(volatile unsigned long *)PPC4xx_EMAC0_MR0 = 0x20000000;
	__asm__ __volatile__("eieio");
#endif

	/*
	 * Call get_mem_size(), which is memory controller dependent,
	 * and we must have the correct file linked in here.
	 */
	TotalMemory = get_mem_size();

	/* assume the chunk below 8M is free */
	end_avail = (char *)0x00800000;

	/*
	 * Reveal where we were loaded at and where we
	 * were relocated to.
	 */
	puts("loaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words)));
	puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start )
	{
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	/*
	 * We link ourself to 0x00800000.  When we run, we relocate
	 * ourselves there.  So we just need __image_begin for the
	 * start. -- Tom
	 */
	zimage_start = (char *)(unsigned long)(&__image_begin);
	zimage_size = (unsigned long)(&__image_end) -
			(unsigned long)(&__image_begin);

	initrd_size = (unsigned long)(&__ramdisk_end) -
		(unsigned long)(&__ramdisk_begin);

	/*
	 * The zImage and initrd will be between start and _end, so they've
	 * already been moved once.  We're good to go now. -- Tom
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)_end);
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start));
	puts("\n");

	if ( initrd_size ) {
		puts("initrd at:     ");
		puthex((unsigned long)(&__ramdisk_begin));
		puts(" "); puthex((unsigned long)(&__ramdisk_end));puts("\n");
	}

#ifndef CONFIG_40x /* don't overwrite the 40x image located at 0x00400000! */
	avail_ram = (char *)0x00400000;
#endif
	end_avail = (char *)0x00800000;
	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");

	if (keyb_present)
		CRT_tstc();  /* Forces keyboard to be initialized */

	/* Display standard Linux/PPC boot prompt for kernel args */
	puts("\nLinux/PPC load: ");
	cp = cmd_line;
	memcpy (cmd_line, cmd_preset, sizeof(cmd_preset));
	while ( *cp ) putc(*cp++);

#ifdef INTERACTIVE_CONSOLE
	/*
	 * If they have a console, allow them to edit the command line.
	 * Otherwise, don't bother wasting the five seconds.
	 */
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				/* Test for backspace/delete */
				if (ch == '\b' || ch == '\177') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				/* Test for ^x/^u (and wipe the line) */
				} else if (ch == '\030' || ch == '\025') {
					while (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				} else {
					*cp++ = ch;
					putc(ch);
				}
			}
			break;  /* Exit 'timer' loop */
		}
		udelay(1000);  /* 1 msec */
	}
	*cp = 0;
#endif
	puts("\n");

	puts("Uncompressing Linux...");
	gunzip(NULL, 0x400000, zimage_start, &zimage_size);
	puts("done.\n");

	/* get the bi_rec address */
	rec = bootinfo_addr(zimage_size);

	/* We need to make sure that the initrd and bi_recs do not
	 * overlap. */
	if ( initrd_size ) {
		unsigned long rec_loc = (unsigned long) rec;
		initrd_loc = (unsigned long)(&__ramdisk_begin);
		/* If the bi_recs are in the middle of the current
		 * initrd, move the initrd to the next MB
		 * boundary. */
		if ((rec_loc > initrd_loc) &&
				((initrd_loc + initrd_size) > rec_loc)) {
			initrd_loc = _ALIGN((unsigned long)(zimage_size)
					+ (2 << 20) - 1, (2 << 20));
		 	memmove((void *)initrd_loc, &__ramdisk_begin,
				 initrd_size);
	         	puts("initrd moved:  "); puthex(initrd_loc);
		 	puts(" "); puthex(initrd_loc + initrd_size);
		 	puts("\n");
		}
	}

	bootinfo_init(rec);
	if ( TotalMemory )
		bootinfo_append(BI_MEMSIZE, sizeof(int), (void*)&TotalMemory);

	bootinfo_append(BI_CMD_LINE, strlen(cmd_line)+1, (void*)cmd_line);

	/* add a bi_rec for the initrd if it exists */
	if (initrd_size) {
		unsigned long initrd[2];

		initrd[0] = initrd_loc;
		initrd[1] = initrd_size;

		bootinfo_append(BI_INITRD, sizeof(initrd), &initrd);
	}
	puts("Now booting the kernel\n");
	serial_close(com_port);

	return rec;
}

void __attribute__ ((weak))
board_isa_init(void)
{
}

/* Allow decompress_kernel to be hooked into.  This is the default. */
void * __attribute__ ((weak))
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum,
		void *ign1, void *ign2)
{
		board_isa_init();
		return decompress_kernel(load_addr, num_words, cksum);
}
