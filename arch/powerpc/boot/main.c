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
#include "zlib.h"
#include "ops.h"
#include "flatdevtree.h"

extern void flush_cache(void *, unsigned long);

extern char _start[];
extern char __bss_start[];
extern char _end[];
extern char _vmlinux_start[];
extern char _vmlinux_end[];
extern char _initrd_start[];
extern char _initrd_end[];
extern char _dtb_start[];
extern char _dtb_end[];

struct addr_range {
	unsigned long addr;
	unsigned long size;
	unsigned long memsize;
};
static struct addr_range vmlinux;
static struct addr_range vmlinuz;
static struct addr_range initrd;

static unsigned long elfoffset;
static int is_64bit;

/* scratch space for gunzip; 46912 is from zlib_inflate_workspacesize() */
static char scratch[46912];
static char elfheader[256];

typedef void (*kernel_entry_t)(unsigned long, unsigned long, void *);

#undef DEBUG

#define HEAD_CRC	2
#define EXTRA_FIELD	4
#define ORIG_NAME	8
#define COMMENT		0x10
#define RESERVED	0xe0

static void gunzip(void *dst, int dstlen, unsigned char *src, int *lenp)
{
	z_stream s;
	int r, i, flags;

	/* skip header */
	i = 10;
	flags = src[3];
	if (src[2] != Z_DEFLATED || (flags & RESERVED) != 0) {
		printf("bad gzipped data\n\r");
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
		printf("gunzip: ran out of data in header\n\r");
		exit();
	}

	if (zlib_inflate_workspacesize() > sizeof(scratch)) {
		printf("gunzip needs more mem\n");
		exit();
	}
	memset(&s, 0, sizeof(s));
	s.workspace = scratch;
	r = zlib_inflateInit2(&s, -MAX_WBITS);
	if (r != Z_OK) {
		printf("inflateInit2 returned %d\n\r", r);
		exit();
	}
	s.next_in = src + i;
	s.avail_in = *lenp - i;
	s.next_out = dst;
	s.avail_out = dstlen;
	r = zlib_inflate(&s, Z_FULL_FLUSH);
	if (r != Z_OK && r != Z_STREAM_END) {
		printf("inflate returned %d msg: %s\n\r", r, s.msg);
		exit();
	}
	*lenp = s.next_out - (unsigned char *) dst;
	zlib_inflateEnd(&s);
}

static int is_elf64(void *hdr)
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

	elfoffset = (unsigned long)elf64ph->p_offset;
	vmlinux.size = (unsigned long)elf64ph->p_filesz + elfoffset;
	vmlinux.memsize = (unsigned long)elf64ph->p_memsz + elfoffset;

	is_64bit = 1;
	return 1;
}

static int is_elf32(void *hdr)
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

	elf32 = (Elf32_Ehdr *)elfheader;
	elf32ph = (Elf32_Phdr *) ((unsigned long)elf32 + elf32->e_phoff);
	for (i = 0; i < elf32->e_phnum; i++, elf32ph++)
		if (elf32ph->p_type == PT_LOAD)
			break;
	if (i >= elf32->e_phnum)
		return 0;

	elfoffset = elf32ph->p_offset;
	vmlinux.size = elf32ph->p_filesz + elf32ph->p_offset;
	vmlinux.memsize = elf32ph->p_memsz + elf32ph->p_offset;
	return 1;
}

static void prep_kernel(unsigned long a1, unsigned long a2)
{
	int len;

	vmlinuz.addr = (unsigned long)_vmlinux_start;
	vmlinuz.size = (unsigned long)(_vmlinux_end - _vmlinux_start);

	/* gunzip the ELF header of the kernel */
	if (*(unsigned short *)vmlinuz.addr == 0x1f8b) {
		len = vmlinuz.size;
		gunzip(elfheader, sizeof(elfheader),
				(unsigned char *)vmlinuz.addr, &len);
	} else
		memcpy(elfheader, (const void *)vmlinuz.addr,
		       sizeof(elfheader));

	if (!is_elf64(elfheader) && !is_elf32(elfheader)) {
		printf("Error: not a valid PPC32 or PPC64 ELF file!\n\r");
		exit();
	}
	if (platform_ops.image_hdr)
		platform_ops.image_hdr(elfheader);

	/* We need to alloc the memsize plus the file offset since gzip
	 * will expand the header (file offset), then the kernel, then
	 * possible rubbish we don't care about. But the kernel bss must
	 * be claimed (it will be zero'd by the kernel itself)
	 */
	printf("Allocating 0x%lx bytes for kernel ...\n\r", vmlinux.memsize);
	vmlinux.addr = (unsigned long)malloc(vmlinux.memsize);
	if (vmlinux.addr == 0) {
		printf("Can't allocate memory for kernel image !\n\r");
		exit();
	}

	/*
	 * Now find the initrd
	 *
	 * First see if we have an image attached to us.  If so
	 * allocate memory for it and copy it there.
	 */
	initrd.size = (unsigned long)(_initrd_end - _initrd_start);
	initrd.memsize = initrd.size;
	if (initrd.size > 0) {
		printf("Allocating 0x%lx bytes for initrd ...\n\r",
		       initrd.size);
		initrd.addr = (unsigned long)malloc((u32)initrd.size);
		if (initrd.addr == 0) {
			printf("Can't allocate memory for initial "
					"ramdisk !\n\r");
			exit();
		}
		printf("initial ramdisk moving 0x%lx <- 0x%lx "
			"(0x%lx bytes)\n\r", initrd.addr,
			(unsigned long)_initrd_start, initrd.size);
		memmove((void *)initrd.addr, (void *)_initrd_start,
			initrd.size);
		printf("initrd head: 0x%lx\n\r",
				*((unsigned long *)initrd.addr));
	} else if (a2 != 0) {
		/* Otherwise, see if yaboot or another loader gave us an initrd */
		initrd.addr = a1;
		initrd.memsize = initrd.size = a2;
		printf("Using loader supplied initrd at 0x%lx (0x%lx bytes)\n\r",
		       initrd.addr, initrd.size);
	}

	/* Eventually gunzip the kernel */
	if (*(unsigned short *)vmlinuz.addr == 0x1f8b) {
		printf("gunzipping (0x%lx <- 0x%lx:0x%0lx)...",
		       vmlinux.addr, vmlinuz.addr, vmlinuz.addr+vmlinuz.size);
		len = vmlinuz.size;
		gunzip((void *)vmlinux.addr, vmlinux.memsize,
			(unsigned char *)vmlinuz.addr, &len);
		printf("done 0x%lx bytes\n\r", len);
	} else {
		memmove((void *)vmlinux.addr,(void *)vmlinuz.addr,
			vmlinuz.size);
	}

	/* Skip over the ELF header */
#ifdef DEBUG
	printf("... skipping 0x%lx bytes of ELF header\n\r",
			elfoffset);
#endif
	vmlinux.addr += elfoffset;

	flush_cache((void *)vmlinux.addr, vmlinux.size);
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

void start(unsigned long a1, unsigned long a2, void *promptr, void *sp)
{
	kernel_entry_t kentry;
	char cmdline[COMMAND_LINE_SIZE];

	memset(__bss_start, 0, _end - __bss_start);
	memset(&platform_ops, 0, sizeof(platform_ops));
	memset(&dt_ops, 0, sizeof(dt_ops));
	memset(&console_ops, 0, sizeof(console_ops));

	if (platform_init(promptr, _dtb_start, _dtb_end))
		exit();
	if (console_ops.open && (console_ops.open() < 0))
		exit();
	if (platform_ops.fixups)
		platform_ops.fixups();

	printf("\n\rzImage starting: loaded at 0x%p (sp: 0x%p)\n\r",
	       _start, sp);

	prep_kernel(a1, a2);

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

	if (console_ops.close)
		console_ops.close();

	kentry = (kernel_entry_t) vmlinux.addr;
	if (_dtb_end > _dtb_start) {
		dt_ops.ft_pack();
		kentry(dt_ops.ft_addr(), 0, NULL);
	}
	else
		/* XXX initrd addr/size should be passed in properties */
		kentry(initrd.addr, initrd.size, promptr);

	/* console closed so printf below may not work */
	printf("Error: Linux kernel returned to zImage boot wrapper!\n\r");
	exit();
}
