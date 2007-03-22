/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * Updates for PPC64 by Todd Inglett, Dave Engebretsen & Peter Bergner.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "elf.h"
#include "page.h"
#include "string.h"
#include "stdio.h"
#include "ops.h"
#include "gunzip_util.h"
#include "flatdevtree.h"

extern char _start[];
extern char __bss_start[];
extern char _end[];
extern char _vmlinux_start[];
extern char _vmlinux_end[];
extern char _initrd_start[];
extern char _initrd_end[];
extern char _dtb_start[];
extern char _dtb_end[];

static struct gunzip_state gzstate;

struct addr_range {
	void *addr;
	unsigned long size;
};

struct elf_info {
	unsigned long loadsize;
	unsigned long memsize;
	unsigned long elfoffset;
};

typedef void (*kernel_entry_t)(unsigned long, unsigned long, void *);

#undef DEBUG

static int parse_elf64(void *hdr, struct elf_info *info)
{
	Elf64_Ehdr *elf64 = hdr;
	Elf64_Phdr *elf64ph;
	unsigned int i;

	if (!(elf64->e_ident[EI_MAG0]  == ELFMAG0	&&
	      elf64->e_ident[EI_MAG1]  == ELFMAG1	&&
	      elf64->e_ident[EI_MAG2]  == ELFMAG2	&&
	      elf64->e_ident[EI_MAG3]  == ELFMAG3	&&
	      elf64->e_ident[EI_CLASS] == ELFCLASS64	&&
	      elf64->e_ident[EI_DATA]  == ELFDATA2MSB	&&
	      elf64->e_type            == ET_EXEC	&&
	      elf64->e_machine         == EM_PPC64))
		return 0;

	elf64ph = (Elf64_Phdr *)((unsigned long)elf64 +
				 (unsigned long)elf64->e_phoff);
	for (i = 0; i < (unsigned int)elf64->e_phnum; i++, elf64ph++)
		if (elf64ph->p_type == PT_LOAD)
			break;
	if (i >= (unsigned int)elf64->e_phnum)
		return 0;

	info->loadsize = (unsigned long)elf64ph->p_filesz;
	info->memsize = (unsigned long)elf64ph->p_memsz;
	info->elfoffset = (unsigned long)elf64ph->p_offset;

	return 1;
}

static int parse_elf32(void *hdr, struct elf_info *info)
{
	Elf32_Ehdr *elf32 = hdr;
	Elf32_Phdr *elf32ph;
	unsigned int i;

	if (!(elf32->e_ident[EI_MAG0]  == ELFMAG0	&&
	      elf32->e_ident[EI_MAG1]  == ELFMAG1	&&
	      elf32->e_ident[EI_MAG2]  == ELFMAG2	&&
	      elf32->e_ident[EI_MAG3]  == ELFMAG3	&&
	      elf32->e_ident[EI_CLASS] == ELFCLASS32	&&
	      elf32->e_ident[EI_DATA]  == ELFDATA2MSB	&&
	      elf32->e_type            == ET_EXEC	&&
	      elf32->e_machine         == EM_PPC))
		return 0;

	elf32ph = (Elf32_Phdr *) ((unsigned long)elf32 + elf32->e_phoff);
	for (i = 0; i < elf32->e_phnum; i++, elf32ph++)
		if (elf32ph->p_type == PT_LOAD)
			break;
	if (i >= elf32->e_phnum)
		return 0;

	info->loadsize = elf32ph->p_filesz;
	info->memsize = elf32ph->p_memsz;
	info->elfoffset = elf32ph->p_offset;
	return 1;
}

static struct addr_range prep_kernel(void)
{
	char elfheader[256];
	void *vmlinuz_addr = _vmlinux_start;
	unsigned long vmlinuz_size = _vmlinux_end - _vmlinux_start;
	void *addr = 0;
	struct elf_info ei;
	int len;

	/* gunzip the ELF header of the kernel */
	gunzip_start(&gzstate, vmlinuz_addr, vmlinuz_size);
	gunzip_exactly(&gzstate, elfheader, sizeof(elfheader));

	if (!parse_elf64(elfheader, &ei) && !parse_elf32(elfheader, &ei))
		fatal("Error: not a valid PPC32 or PPC64 ELF file!\n\r");

	if (platform_ops.image_hdr)
		platform_ops.image_hdr(elfheader);

	/* We need to alloc the memsize: gzip will expand the kernel
	 * text/data, then possible rubbish we don't care about. But
	 * the kernel bss must be claimed (it will be zero'd by the
	 * kernel itself)
	 */
	printf("Allocating 0x%lx bytes for kernel ...\n\r", ei.memsize);

	if (platform_ops.vmlinux_alloc) {
		addr = platform_ops.vmlinux_alloc(ei.memsize);
	} else {
		if ((unsigned long)_start < ei.memsize)
			fatal("Insufficient memory for kernel at address 0!"
			       " (_start=%p)\n\r", _start);
	}

	/* Finally, gunzip the kernel */
	printf("gunzipping (0x%p <- 0x%p:0x%p)...", addr,
	       vmlinuz_addr, vmlinuz_addr+vmlinuz_size);
	/* discard up to the actual load data */
	gunzip_discard(&gzstate, ei.elfoffset - sizeof(elfheader));
	len = gunzip_finish(&gzstate, addr, ei.memsize);
	printf("done 0x%x bytes\n\r", len);

	flush_cache(addr, ei.loadsize);

	return (struct addr_range){addr, ei.memsize};
}

static struct addr_range prep_initrd(struct addr_range vmlinux,
				     unsigned long initrd_addr,
				     unsigned long initrd_size)
{
	void *devp;
	u32 initrd_start, initrd_end;

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
	devp = finddevice("/chosen");
	if (! devp)
		fatal("Device tree has no chosen node!\n\r");

	initrd_start = (u32)initrd_addr;
	initrd_end = (u32)initrd_addr + initrd_size;

	setprop(devp, "linux,initrd-start", &initrd_start,
		sizeof(initrd_start));
	setprop(devp, "linux,initrd-end", &initrd_end, sizeof(initrd_end));

	return (struct addr_range){(void *)initrd_addr, initrd_size};
}

/* A buffer that may be edited by tools operating on a zImage binary so as to
 * edit the command line passed to vmlinux (by setting /chosen/bootargs).
 * The buffer is put in it's own section so that tools may locate it easier.
 */
static char builtin_cmdline[COMMAND_LINE_SIZE]
	__attribute__((__section__("__builtin_cmdline")));

static void get_cmdline(char *buf, int size)
{
	void *devp;
	int len = strlen(builtin_cmdline);

	buf[0] = '\0';

	if (len > 0) { /* builtin_cmdline overrides dt's /chosen/bootargs */
		len = min(len, size-1);
		strncpy(buf, builtin_cmdline, len);
		buf[len] = '\0';
	}
	else if ((devp = finddevice("/chosen")))
		getprop(devp, "bootargs", buf, size);
}

static void set_cmdline(char *buf)
{
	void *devp;

	if ((devp = finddevice("/chosen")))
		setprop(devp, "bootargs", buf, strlen(buf) + 1);
}

struct platform_ops platform_ops;
struct dt_ops dt_ops;
struct console_ops console_ops;
struct loader_info loader_info;

void start(void *sp)
{
	struct addr_range vmlinux, initrd;
	kernel_entry_t kentry;
	char cmdline[COMMAND_LINE_SIZE];
	unsigned long ft_addr = 0;

	if (console_ops.open && (console_ops.open() < 0))
		exit();
	if (platform_ops.fixups)
		platform_ops.fixups();

	printf("\n\rzImage starting: loaded at 0x%p (sp: 0x%p)\n\r",
	       _start, sp);

	vmlinux = prep_kernel();
	initrd = prep_initrd(vmlinux, loader_info.initrd_addr,
			     loader_info.initrd_size);

	/* If cmdline came from zimage wrapper or if we can edit the one
	 * in the dt, print it out and edit it, if possible.
	 */
	if ((strlen(builtin_cmdline) > 0) || console_ops.edit_cmdline) {
		get_cmdline(cmdline, COMMAND_LINE_SIZE);
		printf("\n\rLinux/PowerPC load: %s", cmdline);
		if (console_ops.edit_cmdline)
			console_ops.edit_cmdline(cmdline, COMMAND_LINE_SIZE);
		printf("\n\r");
		set_cmdline(cmdline);
	}

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
	if (ft_addr)
		kentry(ft_addr, 0, NULL);
	else
		kentry((unsigned long)initrd.addr, initrd.size,
		       loader_info.promptr);

	/* console closed so printf in fatal below may not work */
	fatal("Error: Linux kernel returned to zImage boot wrapper!\n\r");
}
