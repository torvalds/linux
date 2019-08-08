// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * Updates for PPC64 by Todd Inglett, Dave Engebretsen & Peter Bergner.
 */
#include <stdarg.h>
#include <stddef.h>
#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"
#include "reg.h"

struct addr_range {
	void *addr;
	unsigned long size;
};

#undef DEBUG

static struct addr_range prep_kernel(void)
{
	char elfheader[256];
	unsigned char *vmlinuz_addr = (unsigned char *)_vmlinux_start;
	unsigned long vmlinuz_size = _vmlinux_end - _vmlinux_start;
	void *addr = 0;
	struct elf_info ei;
	long len;
	int uncompressed_image = 0;

	len = partial_decompress(vmlinuz_addr, vmlinuz_size,
		elfheader, sizeof(elfheader), 0);
	/* assume uncompressed data if -1 is returned */
	if (len == -1) {
		uncompressed_image = 1;
		memcpy(elfheader, vmlinuz_addr, sizeof(elfheader));
		printf("No valid compressed data found, assume uncompressed data\n\r");
	}

	if (!parse_elf64(elfheader, &ei) && !parse_elf32(elfheader, &ei))
		fatal("Error: not a valid PPC32 or PPC64 ELF file!\n\r");

	if (platform_ops.image_hdr)
		platform_ops.image_hdr(elfheader);

	/* We need to alloc the memsize: gzip will expand the kernel
	 * text/data, then possible rubbish we don't care about. But
	 * the kernel bss must be claimed (it will be zero'd by the
	 * kernel itself)
	 */
	printf("Allocating 0x%lx bytes for kernel...\n\r", ei.memsize);

	if (platform_ops.vmlinux_alloc) {
		addr = platform_ops.vmlinux_alloc(ei.memsize);
	} else {
		/*
		 * Check if the kernel image (without bss) would overwrite the
		 * bootwrapper. The device tree has been moved in fdt_init()
		 * to an area allocated with malloc() (somewhere past _end).
		 */
		if ((unsigned long)_start < ei.loadsize)
			fatal("Insufficient memory for kernel at address 0!"
			       " (_start=%p, uncompressed size=%08lx)\n\r",
			       _start, ei.loadsize);

		if ((unsigned long)_end < ei.memsize)
			fatal("The final kernel image would overwrite the "
					"device tree\n\r");
	}

	if (uncompressed_image) {
		memcpy(addr, vmlinuz_addr + ei.elfoffset, ei.loadsize);
		printf("0x%lx bytes of uncompressed data copied\n\r",
		       ei.loadsize);
		goto out;
	}

	/* Finally, decompress the kernel */
	printf("Decompressing (0x%p <- 0x%p:0x%p)...\n\r", addr,
	       vmlinuz_addr, vmlinuz_addr+vmlinuz_size);

	len = partial_decompress(vmlinuz_addr, vmlinuz_size,
		addr, ei.loadsize, ei.elfoffset);

	if (len < 0)
		fatal("Decompression failed with error code %ld\n\r", len);

	if (len != ei.loadsize)
		 fatal("Decompression error: got 0x%lx bytes, expected 0x%lx.\n\r",
			 len, ei.loadsize);

	printf("Done! Decompressed 0x%lx bytes\n\r", len);
out:
	flush_cache(addr, ei.loadsize);

	return (struct addr_range){addr, ei.memsize};
}

static struct addr_range prep_initrd(struct addr_range vmlinux, void *chosen,
				     unsigned long initrd_addr,
				     unsigned long initrd_size)
{
	/* If we have an image attached to us, it overrides anything
	 * supplied by the loader. */
	if (_initrd_end > _initrd_start) {
		printf("Attached initrd image at 0x%p-0x%p\n\r",
		       _initrd_start, _initrd_end);
		initrd_addr = (unsigned long)_initrd_start;
		initrd_size = _initrd_end - _initrd_start;
	} else if (initrd_size > 0) {
		printf("Using loader supplied ramdisk at 0x%lx-0x%lx\n\r",
		       initrd_addr, initrd_addr + initrd_size);
	}

	/* If there's no initrd at all, we're done */
	if (! initrd_size)
		return (struct addr_range){0, 0};

	/*
	 * If the initrd is too low it will be clobbered when the
	 * kernel relocates to its final location.  In this case,
	 * allocate a safer place and move it.
	 */
	if (initrd_addr < vmlinux.size) {
		void *old_addr = (void *)initrd_addr;

		printf("Allocating 0x%lx bytes for initrd ...\n\r",
		       initrd_size);
		initrd_addr = (unsigned long)malloc(initrd_size);
		if (! initrd_addr)
			fatal("Can't allocate memory for initial "
			       "ramdisk !\n\r");
		printf("Relocating initrd 0x%lx <- 0x%p (0x%lx bytes)\n\r",
		       initrd_addr, old_addr, initrd_size);
		memmove((void *)initrd_addr, old_addr, initrd_size);
	}

	printf("initrd head: 0x%lx\n\r", *((unsigned long *)initrd_addr));

	/* Tell the kernel initrd address via device tree */
	setprop_val(chosen, "linux,initrd-start", (u32)(initrd_addr));
	setprop_val(chosen, "linux,initrd-end", (u32)(initrd_addr+initrd_size));

	return (struct addr_range){(void *)initrd_addr, initrd_size};
}

/* A buffer that may be edited by tools operating on a zImage binary so as to
 * edit the command line passed to vmlinux (by setting /chosen/bootargs).
 * The buffer is put in it's own section so that tools may locate it easier.
 */
static char cmdline[BOOT_COMMAND_LINE_SIZE]
	__attribute__((__section__("__builtin_cmdline")));

static void prep_cmdline(void *chosen)
{
	unsigned int getline_timeout = 5000;
	int v;
	int n;

	/* Wait-for-input time */
	n = getprop(chosen, "linux,cmdline-timeout", &v, sizeof(v));
	if (n == sizeof(v))
		getline_timeout = v;

	if (cmdline[0] == '\0')
		getprop(chosen, "bootargs", cmdline, BOOT_COMMAND_LINE_SIZE-1);

	printf("\n\rLinux/PowerPC load: %s", cmdline);

	/* If possible, edit the command line */
	if (console_ops.edit_cmdline && getline_timeout)
		console_ops.edit_cmdline(cmdline, BOOT_COMMAND_LINE_SIZE, getline_timeout);

	printf("\n\r");

	/* Put the command line back into the devtree for the kernel */
	setprop_str(chosen, "bootargs", cmdline);
}

struct platform_ops platform_ops;
struct dt_ops dt_ops;
struct console_ops console_ops;
struct loader_info loader_info;

void start(void)
{
	struct addr_range vmlinux, initrd;
	kernel_entry_t kentry;
	unsigned long ft_addr = 0;
	void *chosen;

	/* Do this first, because malloc() could clobber the loader's
	 * command line.  Only use the loader command line if a
	 * built-in command line wasn't set by an external tool */
	if ((loader_info.cmdline_len > 0) && (cmdline[0] == '\0'))
		memmove(cmdline, loader_info.cmdline,
			min(loader_info.cmdline_len, BOOT_COMMAND_LINE_SIZE-1));

	if (console_ops.open && (console_ops.open() < 0))
		exit();
	if (platform_ops.fixups)
		platform_ops.fixups();

	printf("\n\rzImage starting: loaded at 0x%p (sp: 0x%p)\n\r",
	       _start, get_sp());

	/* Ensure that the device tree has a /chosen node */
	chosen = finddevice("/chosen");
	if (!chosen)
		chosen = create_node(NULL, "chosen");

	vmlinux = prep_kernel();
	initrd = prep_initrd(vmlinux, chosen,
			     loader_info.initrd_addr, loader_info.initrd_size);
	prep_cmdline(chosen);

	printf("Finalizing device tree...");
	if (dt_ops.finalize)
		ft_addr = dt_ops.finalize();
	if (ft_addr)
		printf(" flat tree at 0x%lx\n\r", ft_addr);
	else
		printf(" using OF tree (promptr=%p)\n\r", loader_info.promptr);

	if (console_ops.close)
		console_ops.close();

	kentry = (kernel_entry_t) vmlinux.addr;
	if (ft_addr) {
		if(platform_ops.kentry)
			platform_ops.kentry(ft_addr, vmlinux.addr);
		else
			kentry(ft_addr, 0, NULL);
	}
	else
		kentry((unsigned long)initrd.addr, initrd.size,
		       loader_info.promptr);

	/* console closed so printf in fatal below may not work */
	fatal("Error: Linux kernel returned to zImage boot wrapper!\n\r");
}
