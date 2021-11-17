/*
 * Big Endian PROM code for SNI RM machines
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2006 Florian Lohoff (flo@rfc822.org)
 * Copyright (C) 2005-2006 Thomas Bogendoerfer (tsbogend@alpha.franken.de)
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/string.h>
#include <linux/console.h>

#include <asm/addrspace.h>
#include <asm/sni.h>
#include <asm/mipsprom.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/setup.h>

/* special SNI prom calls */
/*
 * This does not exist in all proms - SINIX compares
 * the prom env variable "version" against "2.0008"
 * or greater. If lesser it tries to probe interesting
 * registers
 */
#define PROM_GET_MEMCONF	58
#define PROM_GET_HWCONF		61

#define PROM_VEC		(u64 *)CKSEG1ADDR(0x1fc00000)
#define PROM_ENTRY(x)		(PROM_VEC + (x))

#define ___prom_putchar		((int *(*)(int))PROM_ENTRY(PROM_PUTCHAR))
#define ___prom_getenv		((char *(*)(char *))PROM_ENTRY(PROM_GETENV))
#define ___prom_get_memconf	((void (*)(void *))PROM_ENTRY(PROM_GET_MEMCONF))
#define ___prom_get_hwconf	((u32 (*)(void))PROM_ENTRY(PROM_GET_HWCONF))

#ifdef CONFIG_64BIT

/* O32 stack has to be 8-byte aligned. */
static u64 o32_stk[4096];
#define O32_STK	  (&o32_stk[ARRAY_SIZE(o32_stk)])

#define __PROM_O32(fun, arg) fun arg __asm__(#fun); \
				     __asm__(#fun " = call_o32")

int   __PROM_O32(__prom_putchar, (int *(*)(int), void *, int));
char *__PROM_O32(__prom_getenv, (char *(*)(char *), void *, char *));
void  __PROM_O32(__prom_get_memconf, (void (*)(void *), void *, void *));
u32   __PROM_O32(__prom_get_hwconf, (u32 (*)(void), void *));

#define _prom_putchar(x)     __prom_putchar(___prom_putchar, O32_STK, x)
#define _prom_getenv(x)	     __prom_getenv(___prom_getenv, O32_STK, x)
#define _prom_get_memconf(x) __prom_get_memconf(___prom_get_memconf, O32_STK, x)
#define _prom_get_hwconf()   __prom_get_hwconf(___prom_get_hwconf, O32_STK)

#else
#define _prom_putchar(x)     ___prom_putchar(x)
#define _prom_getenv(x)	     ___prom_getenv(x)
#define _prom_get_memconf(x) ___prom_get_memconf(x)
#define _prom_get_hwconf(x)  ___prom_get_hwconf(x)
#endif

void prom_putchar(char c)
{
	_prom_putchar(c);
}


char *prom_getenv(char *s)
{
	return _prom_getenv(s);
}

void *prom_get_hwconf(void)
{
	u32 hwconf = _prom_get_hwconf();

	if (hwconf == 0xffffffff)
		return NULL;

	return (void *)CKSEG1ADDR(hwconf);
}

/*
 * /proc/cpuinfo system type
 *
 */
char *system_type = "Unknown";
const char *get_system_type(void)
{
	return system_type;
}

static void __init sni_mem_init(void)
{
	int i, memsize;
	struct membank {
		u32		size;
		u32		base;
		u32		size2;
		u32		pad1;
		u32		pad2;
	} memconf[8];
	int brd_type = *(unsigned char *)SNI_IDPROM_BRDTYPE;


	/* MemSIZE from prom in 16MByte chunks */
	memsize = *((unsigned char *) SNI_IDPROM_MEMSIZE) * 16;

	pr_debug("IDProm memsize: %u MByte\n", memsize);

	/* get memory bank layout from prom */
	_prom_get_memconf(&memconf);

	pr_debug("prom_get_mem_conf memory configuration:\n");
	for (i = 0; i < 8 && memconf[i].size; i++) {
		if (brd_type == SNI_BRD_PCI_TOWER ||
		    brd_type == SNI_BRD_PCI_TOWER_CPLUS) {
			if (memconf[i].base >= 0x20000000 &&
			    memconf[i].base <  0x30000000)
				memconf[i].base -= 0x20000000;
		}
		pr_debug("Bank%d: %08x @ %08x\n", i,
			memconf[i].size, memconf[i].base);
		memblock_add(memconf[i].base, memconf[i].size);
	}
}

void __init prom_init(void)
{
	int argc = fw_arg0;
	u32 *argv = (u32 *)CKSEG0ADDR(fw_arg1);
	int i;

	sni_mem_init();

	/* copy prom cmdline parameters to kernel cmdline */
	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, (char *)CKSEG0ADDR(argv[i]));
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}
}
