/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"

typedef void *ihandle;
typedef void *phandle;

extern char _end[];

/* Value picked to match that used by yaboot */
#define PROG_START	0x01400000	/* only used on 64-bit systems */
#define RAM_END		(512<<20)	/* Fixme: use OF */
#define	ONE_MB		0x100000

int (*prom) (void *);


static unsigned long claim_base;

static int call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return -1;

	return (nret > 0)? args.args[nargs]: 0;
}

static int call_prom_ret(const char *service, int nargs, int nret,
		  unsigned int *rets, ...)
{
	int i;
	struct prom_args {
		const char *service;
		int nargs;
		int nret;
		unsigned int args[12];
	} args;
	va_list list;

	args.service = service;
	args.nargs = nargs;
	args.nret = nret;

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = va_arg(list, unsigned int);
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return -1;

	if (rets != (void *) 0)
		for (i = 1; i < nret; ++i)
			rets[i-1] = args.args[nargs+i];

	return (nret > 0)? args.args[nargs]: 0;
}

/*
 * Older OF's require that when claiming a specific range of addresses,
 * we claim the physical space in the /memory node and the virtual
 * space in the chosen mmu node, and then do a map operation to
 * map virtual to physical.
 */
static int need_map = -1;
static ihandle chosen_mmu;
static phandle memory;

/* returns true if s2 is a prefix of s1 */
static int string_match(const char *s1, const char *s2)
{
	for (; *s2; ++s2)
		if (*s1++ != *s2)
			return 0;
	return 1;
}

static int check_of_version(void)
{
	phandle oprom, chosen;
	char version[64];

	oprom = finddevice("/openprom");
	if (oprom == (phandle) -1)
		return 0;
	if (getprop(oprom, "model", version, sizeof(version)) <= 0)
		return 0;
	version[sizeof(version)-1] = 0;
	printf("OF version = '%s'\r\n", version);
	if (!string_match(version, "Open Firmware, 1.")
	    && !string_match(version, "FirmWorks,3."))
		return 0;
	chosen = finddevice("/chosen");
	if (chosen == (phandle) -1) {
		chosen = finddevice("/chosen@0");
		if (chosen == (phandle) -1) {
			printf("no chosen\n");
			return 0;
		}
	}
	if (getprop(chosen, "mmu", &chosen_mmu, sizeof(chosen_mmu)) <= 0) {
		printf("no mmu\n");
		return 0;
	}
	memory = (ihandle) call_prom("open", 1, 1, "/memory");
	if (memory == (ihandle) -1) {
		memory = (ihandle) call_prom("open", 1, 1, "/memory@0");
		if (memory == (ihandle) -1) {
			printf("no memory node\n");
			return 0;
		}
	}
	printf("old OF detected\r\n");
	return 1;
}

static void *claim(unsigned long virt, unsigned long size, unsigned long align)
{
	int ret;
	unsigned int result;

	if (need_map < 0)
		need_map = check_of_version();
	if (align || !need_map)
		return (void *) call_prom("claim", 3, 1, virt, size, align);

	ret = call_prom_ret("call-method", 5, 2, &result, "claim", memory,
			    align, size, virt);
	if (ret != 0 || result == -1)
		return (void *) -1;
	ret = call_prom_ret("call-method", 5, 2, &result, "claim", chosen_mmu,
			    align, size, virt);
	/* 0x12 == coherent + read/write */
	ret = call_prom("call-method", 6, 1, "map", chosen_mmu,
			0x12, size, virt, virt);
	return (void *) virt;
}

static void *of_try_claim(unsigned long size)
{
	unsigned long addr = 0;

	if (claim_base == 0)
		claim_base = _ALIGN_UP((unsigned long)_end, ONE_MB);

	for(; claim_base < RAM_END; claim_base += ONE_MB) {
#ifdef DEBUG
		printf("    trying: 0x%08lx\n\r", claim_base);
#endif
		addr = (unsigned long)claim(claim_base, size, 0);
		if ((void *)addr != (void *)-1)
			break;
	}
	if (addr == 0)
		return NULL;
	claim_base = PAGE_ALIGN(claim_base + size);
	return (void *)addr;
}

static void of_image_hdr(const void *hdr)
{
	const Elf64_Ehdr *elf64 = hdr;

	if (elf64->e_ident[EI_CLASS] == ELFCLASS64) {
		/*
		 * Maintain a "magic" minimum address. This keeps some older
		 * firmware platforms running.
		 */
		if (claim_base < PROG_START)
			claim_base = PROG_START;
	}
}

static void *of_vmlinux_alloc(unsigned long size)
{
	void *p = malloc(size);

	if (!p)
		fatal("Can't allocate memory for kernel image!\n\r");

	return p;
}

static void of_exit(void)
{
	call_prom("exit", 0, 0);
}

/*
 * OF device tree routines
 */
static void *of_finddevice(const char *name)
{
	return (phandle) call_prom("finddevice", 1, 1, name);
}

static int of_getprop(const void *phandle, const char *name, void *buf,
		const int buflen)
{
	return call_prom("getprop", 4, 1, phandle, name, buf, buflen);
}

static int of_setprop(const void *phandle, const char *name, const void *buf,
		const int buflen)
{
	return call_prom("setprop", 4, 1, phandle, name, buf, buflen);
}

/*
 * OF console routines
 */
static void *of_stdout_handle;

static int of_console_open(void)
{
	void *devp;

	if (((devp = finddevice("/chosen")) != NULL)
			&& (getprop(devp, "stdout", &of_stdout_handle,
				sizeof(of_stdout_handle))
				== sizeof(of_stdout_handle)))
		return 0;

	return -1;
}

static void of_console_write(char *buf, int len)
{
	call_prom("write", 3, 1, of_stdout_handle, buf, len);
}

void platform_init(unsigned long a1, unsigned long a2, void *promptr)
{
	platform_ops.image_hdr = of_image_hdr;
	platform_ops.malloc = of_try_claim;
	platform_ops.exit = of_exit;
	platform_ops.vmlinux_alloc = of_vmlinux_alloc;

	dt_ops.finddevice = of_finddevice;
	dt_ops.getprop = of_getprop;
	dt_ops.setprop = of_setprop;

	console_ops.open = of_console_open;
	console_ops.write = of_console_write;

	prom = (int (*)(void *))promptr;
	loader_info.promptr = promptr;
	if (a1 && a2 && a2 != 0xdeadbeef) {
		loader_info.initrd_addr = a1;
		loader_info.initrd_size = a2;
	}
}
