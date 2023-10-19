// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Paul Mackerras 1997.
 */
#include <stddef.h>
#include "types.h"
#include "elf.h"
#include "string.h"
#include "stdio.h"
#include "page.h"
#include "ops.h"

#include "of.h"

typedef u32 prom_arg_t;

/* The following structure is used to communicate with open firmware.
 * All arguments in and out are in big endian format. */
struct prom_args {
	__be32 service;	/* Address of service name string. */
	__be32 nargs;	/* Number of input arguments. */
	__be32 nret;	/* Number of output arguments. */
	__be32 args[10];	/* Input/output arguments. */
};

#ifdef __powerpc64__
extern int prom(void *);
#else
static int (*prom) (void *);
#endif

void of_init(void *promptr)
{
#ifndef __powerpc64__
	prom = (int (*)(void *))promptr;
#endif
}

#define ADDR(x)		(u32)(unsigned long)(x)

int of_call_prom(const char *service, int nargs, int nret, ...)
{
	int i;
	struct prom_args args;
	va_list list;

	args.service = cpu_to_be32(ADDR(service));
	args.nargs = cpu_to_be32(nargs);
	args.nret = cpu_to_be32(nret);

	va_start(list, nret);
	for (i = 0; i < nargs; i++)
		args.args[i] = cpu_to_be32(va_arg(list, prom_arg_t));
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return PROM_ERROR;

	return (nret > 0) ? be32_to_cpu(args.args[nargs]) : 0;
}

static int of_call_prom_ret(const char *service, int nargs, int nret,
			    prom_arg_t *rets, ...)
{
	int i;
	struct prom_args args;
	va_list list;

	args.service = cpu_to_be32(ADDR(service));
	args.nargs = cpu_to_be32(nargs);
	args.nret = cpu_to_be32(nret);

	va_start(list, rets);
	for (i = 0; i < nargs; i++)
		args.args[i] = cpu_to_be32(va_arg(list, prom_arg_t));
	va_end(list);

	for (i = 0; i < nret; i++)
		args.args[nargs+i] = 0;

	if (prom(&args) < 0)
		return PROM_ERROR;

	if (rets != NULL)
		for (i = 1; i < nret; ++i)
			rets[i-1] = be32_to_cpu(args.args[nargs+i]);

	return (nret > 0) ? be32_to_cpu(args.args[nargs]) : 0;
}

/* returns true if s2 is a prefix of s1 */
static int string_match(const char *s1, const char *s2)
{
	for (; *s2; ++s2)
		if (*s1++ != *s2)
			return 0;
	return 1;
}

/*
 * Older OF's require that when claiming a specific range of addresses,
 * we claim the physical space in the /memory node and the virtual
 * space in the chosen mmu node, and then do a map operation to
 * map virtual to physical.
 */
static int need_map = -1;
static ihandle chosen_mmu;
static ihandle memory;

static int check_of_version(void)
{
	phandle oprom, chosen;
	char version[64];

	oprom = of_finddevice("/openprom");
	if (oprom == (phandle) -1)
		return 0;
	if (of_getprop(oprom, "model", version, sizeof(version)) <= 0)
		return 0;
	version[sizeof(version)-1] = 0;
	printf("OF version = '%s'\r\n", version);
	if (!string_match(version, "Open Firmware, 1.")
	    && !string_match(version, "FirmWorks,3."))
		return 0;
	chosen = of_finddevice("/chosen");
	if (chosen == (phandle) -1) {
		chosen = of_finddevice("/chosen@0");
		if (chosen == (phandle) -1) {
			printf("no chosen\n");
			return 0;
		}
	}
	if (of_getprop(chosen, "mmu", &chosen_mmu, sizeof(chosen_mmu)) <= 0) {
		printf("no mmu\n");
		return 0;
	}
	memory = of_call_prom("open", 1, 1, "/memory");
	if (memory == PROM_ERROR) {
		memory = of_call_prom("open", 1, 1, "/memory@0");
		if (memory == PROM_ERROR) {
			printf("no memory node\n");
			return 0;
		}
	}
	printf("old OF detected\r\n");
	return 1;
}

unsigned int of_claim(unsigned long virt, unsigned long size,
		      unsigned long align)
{
	int ret;
	prom_arg_t result;

	if (need_map < 0)
		need_map = check_of_version();
	if (align || !need_map)
		return of_call_prom("claim", 3, 1, virt, size, align);

	ret = of_call_prom_ret("call-method", 5, 2, &result, "claim", memory,
			       align, size, virt);
	if (ret != 0 || result == -1)
		return  -1;
	ret = of_call_prom_ret("call-method", 5, 2, &result, "claim", chosen_mmu,
			       align, size, virt);
	/* 0x12 == coherent + read/write */
	ret = of_call_prom("call-method", 6, 1, "map", chosen_mmu,
			   0x12, size, virt, virt);
	return virt;
}

void *of_vmlinux_alloc(unsigned long size)
{
	unsigned long start = (unsigned long)_start, end = (unsigned long)_end;
	unsigned long addr;
	void *p;

	/* With some older POWER4 firmware we need to claim the area the kernel
	 * will reside in.  Newer firmwares don't need this so we just ignore
	 * the return value.
	 */
	addr = (unsigned long) of_claim(start, end - start, 0);
	printf("Trying to claim from 0x%lx to 0x%lx (0x%lx) got %lx\r\n",
	       start, end, end - start, addr);

	p = malloc(size);
	if (!p)
		fatal("Can't allocate memory for kernel image!\n\r");

	return p;
}

void of_exit(void)
{
	of_call_prom("exit", 0, 0);
}

/*
 * OF device tree routines
 */
void *of_finddevice(const char *name)
{
	return (void *) (unsigned long) of_call_prom("finddevice", 1, 1, name);
}

int of_getprop(const void *phandle, const char *name, void *buf,
	       const int buflen)
{
	return of_call_prom("getprop", 4, 1, phandle, name, buf, buflen);
}

int of_setprop(const void *phandle, const char *name, const void *buf,
	       const int buflen)
{
	return of_call_prom("setprop", 4, 1, phandle, name, buf, buflen);
}
