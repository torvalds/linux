// SPDX-License-Identifier: GPL-2.0
/*
 * arch/ia64/hp/sim/boot/bootloader.c
 *
 * Loads an ELF kernel.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * 01/07/99 S.Eranian modified to pass command line arguments to kernel
 */
struct task_struct;	/* forward declaration for elf.h */

#include <linux/elf.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/elf.h>
#include <asm/intrinsics.h>
#include <asm/pal.h>
#include <asm/pgtable.h>
#include <asm/sal.h>

#include "ssc.h"

struct disk_req {
	unsigned long addr;
	unsigned len;
};

struct disk_stat {
	int fd;
	unsigned count;
};

extern void jmp_to_kernel (unsigned long bp, unsigned long e_entry);
extern struct ia64_boot_param *sys_fw_init (const char *args, int arglen);
extern void debug_break (void);

static void
cons_write (const char *buf)
{
	unsigned long ch;

	while ((ch = *buf++) != '\0') {
		ssc(ch, 0, 0, 0, SSC_PUTCHAR);
		if (ch == '\n')
		  ssc('\r', 0, 0, 0, SSC_PUTCHAR);
	}
}

#define MAX_ARGS 32

void
start_bootloader (void)
{
	static char mem[4096];
	static char buffer[1024];
	unsigned long off;
	int fd, i;
	struct disk_req req;
	struct disk_stat stat;
	struct elfhdr *elf;
	struct elf_phdr *elf_phdr;	/* program header */
	unsigned long e_entry, e_phoff, e_phnum;
	register struct ia64_boot_param *bp;
	char *kpath, *args;
	long arglen = 0;

	ssc(0, 0, 0, 0, SSC_CONSOLE_INIT);

	/*
	 * S.Eranian: extract the commandline argument from the simulator
	 *
	 * The expected format is as follows:
         *
	 *	kernelname args...
	 *
	 * Both are optional but you can't have the second one without the first.
	 */
	arglen = ssc((long) buffer, 0, 0, 0, SSC_GET_ARGS);

	kpath = "vmlinux";
	args = buffer;
	if (arglen > 0) {
		kpath = buffer;
		while (*args != ' ' && *args != '\0')
			++args, --arglen;
		if (*args == ' ')
			*args++ = '\0', --arglen;
	}

	if (arglen <= 0) {
		args = "";
		arglen = 1;
	}

	fd = ssc((long) kpath, 1, 0, 0, SSC_OPEN);

	if (fd < 0) {
		cons_write(kpath);
		cons_write(": file not found, reboot now\n");
		for(;;);
	}
	stat.fd = fd;
	off = 0;

	req.len = sizeof(mem);
	req.addr = (long) mem;
	ssc(fd, 1, (long) &req, off, SSC_READ);
	ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);

	elf = (struct elfhdr *) mem;
	if (elf->e_ident[0] == 0x7f && strncmp(elf->e_ident + 1, "ELF", 3) != 0) {
		cons_write("not an ELF file\n");
		return;
	}
	if (elf->e_type != ET_EXEC) {
		cons_write("not an ELF executable\n");
		return;
	}
	if (!elf_check_arch(elf)) {
		cons_write("kernel not for this processor\n");
		return;
	}

	e_entry = elf->e_entry;
	e_phnum = elf->e_phnum;
	e_phoff = elf->e_phoff;

	cons_write("loading ");
	cons_write(kpath);
	cons_write("...\n");

	for (i = 0; i < e_phnum; ++i) {
		req.len = sizeof(*elf_phdr);
		req.addr = (long) mem;
		ssc(fd, 1, (long) &req, e_phoff, SSC_READ);
		ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);
		if (stat.count != sizeof(*elf_phdr)) {
			cons_write("failed to read phdr\n");
			return;
		}
		e_phoff += sizeof(*elf_phdr);

		elf_phdr = (struct elf_phdr *) mem;

		if (elf_phdr->p_type != PT_LOAD)
			continue;

		req.len = elf_phdr->p_filesz;
		req.addr = __pa(elf_phdr->p_paddr);
		ssc(fd, 1, (long) &req, elf_phdr->p_offset, SSC_READ);
		ssc((long) &stat, 0, 0, 0, SSC_WAIT_COMPLETION);
		memset((char *)__pa(elf_phdr->p_paddr) + elf_phdr->p_filesz, 0,
		       elf_phdr->p_memsz - elf_phdr->p_filesz);
	}
	ssc(fd, 0, 0, 0, SSC_CLOSE);

	cons_write("starting kernel...\n");

	/* fake an I/O base address: */
	ia64_setreg(_IA64_REG_AR_KR0, 0xffffc000000UL);

	bp = sys_fw_init(args, arglen);

	ssc(0, (long) kpath, 0, 0, SSC_LOAD_SYMBOLS);

	debug_break();
	jmp_to_kernel((unsigned long) bp, e_entry);

	cons_write("kernel returned!\n");
	ssc(-1, 0, 0, 0, SSC_EXIT);
}
