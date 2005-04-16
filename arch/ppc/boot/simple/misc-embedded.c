/*
 * Originally adapted by Gary Thomas.  Much additional work by
 * Cort Dougan <cort@fsmlabs.com>.  On top of that still more work by
 * Dan Malek <dmalek@jlc.net>.
 *
 * Currently maintained by: Tom Rini <trini@kernel.crashing.org>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/string.h>
#include <asm/bootinfo.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/residual.h>
#if defined(CONFIG_4xx)
#include <asm/ibm4xx.h>
#elif defined(CONFIG_8xx)
#include <asm/mpc8xx.h>
#elif defined(CONFIG_8260)
#include <asm/mpc8260.h>
#endif

#include "nonstdio.h"

/* The linker tells us where the image is. */
extern char __image_begin, __image_end;
extern char __ramdisk_begin, __ramdisk_end;
extern char _end[];

/* Because of the limited amount of memory on embedded, it presents
 * loading problems.  The biggest is that we load this boot program
 * into a relatively low memory address, and the Linux kernel Bss often
 * extends into this space when it get loaded.  When the kernel starts
 * and zeros the BSS space, it also writes over the information we
 * save here and pass to the kernel (usually board info).
 * On these boards, we grab some known memory holes to hold this information.
 */
char cmd_buf[256];
char *cmd_line = cmd_buf;
char *avail_ram;
char *end_avail;
char *zimage_start;

/* This is for 4xx treeboot.  It provides a place for the bootrom
 * give us a pointer to a rom environment command line.
 */
char *bootrom_cmdline = "";

/* This is the default cmdline that will be given to the user at boot time..
 * If none was specified at compile time, we'll give it one that should work.
 * -- Tom */
#ifdef CONFIG_CMDLINE_BOOL
char compiled_string[] = CONFIG_CMDLINE;
#endif
char ramroot_string[] = "root=/dev/ram";
char netroot_string[] = "root=/dev/nfs rw ip=on";

/* Serial port to use. */
unsigned long com_port;

/* We need to make sure that this is before the images to ensure
 * that it's in a mapped location. - Tom */
bd_t hold_resid_buf __attribute__ ((__section__ (".data.boot")));
bd_t *hold_residual = &hold_resid_buf;

extern unsigned long serial_init(int chan, bd_t *bp);
extern void serial_close(unsigned long com_port);
extern unsigned long start;
extern void flush_instruction_cache(void);
extern void gunzip(void *, int, unsigned char *, int *);
extern void embed_config(bd_t **bp);

/* Weak function for boards which don't need to build the
 * board info struct because they are using PPCBoot/U-Boot.
 */
void __attribute__ ((weak))
embed_config(bd_t **bdp)
{
}

unsigned long
load_kernel(unsigned long load_addr, int num_words, unsigned long cksum, bd_t *bp)
{
	char *cp, ch;
	int timer = 0, zimage_size;
	unsigned long initrd_size;

	/* First, capture the embedded board information.  Then
	 * initialize the serial console port.
	 */
	embed_config(&bp);
#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE)
	com_port = serial_init(0, bp);
#endif

	/* Grab some space for the command line and board info.  Since
	 * we no longer use the ELF header, but it was loaded, grab
	 * that space.
	 */
#ifdef CONFIG_MBX
	/* Because of the way the MBX loads the ELF image, we can't
	 * tell where we started.  We read a magic variable from the NVRAM
	 * that gives us the intermediate buffer load address.
	 */
	load_addr = *(uint *)0xfa000020;
	load_addr += 0x10000;		/* Skip ELF header */
#endif
	/* copy board data */
	if (bp)
		memcpy(hold_residual,bp,sizeof(bd_t));

	/* Set end of memory available to us.  It is always the highest
	 * memory address provided by the board information.
	 */
	end_avail = (char *)(bp->bi_memsize);

	puts("\nloaded at:     "); puthex(load_addr);
	puts(" "); puthex((unsigned long)(load_addr + (4*num_words))); puts("\n");
	if ( (unsigned long)load_addr != (unsigned long)&start ) {
		puts("relocated to:  "); puthex((unsigned long)&start);
		puts(" ");
		puthex((unsigned long)((unsigned long)&start + (4*num_words)));
		puts("\n");
	}

	if ( bp ) {
		puts("board data at: "); puthex((unsigned long)bp);
		puts(" ");
		puthex((unsigned long)((unsigned long)bp + sizeof(bd_t)));
		puts("\nrelocated to:  ");
		puthex((unsigned long)hold_residual);
		puts(" ");
		puthex((unsigned long)((unsigned long)hold_residual + sizeof(bd_t)));
		puts("\n");
	}

	/*
	 * We link ourself to an arbitrary low address.  When we run, we
	 * relocate outself to that address.  __image_being points to
	 * the part of the image where the zImage is. -- Tom
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
	puts("zimage at:     "); puthex((unsigned long)zimage_start);
	puts(" "); puthex((unsigned long)(zimage_size+zimage_start));
	puts("\n");

	if ( initrd_size ) {
		puts("initrd at:     ");
		puthex((unsigned long)(&__ramdisk_begin));
		puts(" "); puthex((unsigned long)(&__ramdisk_end));puts("\n");
	}

	/*
	 * setup avail_ram - this is the first part of ram usable
	 * by the uncompress code.  Anything after this program in RAM
	 * is now fair game. -- Tom
	 */
	avail_ram = (char *)PAGE_ALIGN((unsigned long)_end);

	puts("avail ram:     "); puthex((unsigned long)avail_ram); puts(" ");
	puthex((unsigned long)end_avail); puts("\n");
	puts("\nLinux/PPC load: ");
	cp = cmd_line;
	/* This is where we try and pick the right command line for booting.
	 * If we were given one at compile time, use it.  It Is Right.
	 * If we weren't, see if we have a ramdisk.  If so, thats root.
	 * When in doubt, give them the netroot (root=/dev/nfs rw) -- Tom
	 */
#ifdef CONFIG_CMDLINE_BOOL
	memcpy (cmd_line, compiled_string, sizeof(compiled_string));
#else
	if ( initrd_size )
		memcpy (cmd_line, ramroot_string, sizeof(ramroot_string));
	else
		memcpy (cmd_line, netroot_string, sizeof(netroot_string));
#endif
	while ( *cp )
		putc(*cp++);
	while (timer++ < 5*1000) {
		if (tstc()) {
			while ((ch = getc()) != '\n' && ch != '\r') {
				if (ch == '\b' || ch == '\177') {
					if (cp != cmd_line) {
						cp--;
						puts("\b \b");
					}
				} else if (ch == '\030'		/* ^x */
					   || ch == '\025') {	/* ^u */
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
	puts("\nUncompressing Linux...");

	gunzip(0, 0x400000, zimage_start, &zimage_size);
	flush_instruction_cache();
	puts("done.\n");
	{
		struct bi_record *rec;
		unsigned long initrd_loc = 0;
		unsigned long rec_loc = _ALIGN((unsigned long)(zimage_size) +
				(1 << 20) - 1, (1 << 20));
		rec = (struct bi_record *)rec_loc;

		/* We need to make sure that the initrd and bi_recs do not
		 * overlap. */
		if ( initrd_size ) {
			initrd_loc = (unsigned long)(&__ramdisk_begin);
			/* If the bi_recs are in the middle of the current
			 * initrd, move the initrd to the next MB
			 * boundary. */
			if ((rec_loc > initrd_loc) &&
					((initrd_loc + initrd_size)
					 > rec_loc)) {
				initrd_loc = _ALIGN((unsigned long)(zimage_size)
						+ (2 << 20) - 1, (2 << 20));
			 	memmove((void *)initrd_loc, &__ramdisk_begin,
					 initrd_size);
		         	puts("initrd moved:  "); puthex(initrd_loc);
			 	puts(" "); puthex(initrd_loc + initrd_size);
			 	puts("\n");
			}
		}

		rec->tag = BI_FIRST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		rec->tag = BI_CMD_LINE;
		memcpy( (char *)rec->data, cmd_line, strlen(cmd_line)+1);
		rec->size = sizeof(struct bi_record) + strlen(cmd_line) + 1;
		rec = (struct bi_record *)((unsigned long)rec + rec->size);

		if ( initrd_size ) {
			rec->tag = BI_INITRD;
			rec->data[0] = initrd_loc;
			rec->data[1] = initrd_size;
			rec->size = sizeof(struct bi_record) + 2 *
				sizeof(unsigned long);
			rec = (struct bi_record *)((unsigned long)rec +
					rec->size);
		}

		rec->tag = BI_LAST;
		rec->size = sizeof(struct bi_record);
		rec = (struct bi_record *)((unsigned long)rec + rec->size);
	}
	puts("Now booting the kernel\n");
#if defined(CONFIG_SERIAL_CPM_CONSOLE) || defined(CONFIG_SERIAL_8250_CONSOLE)
	serial_close(com_port);
#endif

	return (unsigned long)hold_residual;
}
