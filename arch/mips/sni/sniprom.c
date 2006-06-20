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
#include <linux/string.h>

#include <asm/addrspace.h>
#include <asm/sni.h>
#include <asm/mipsprom.h>
#include <asm/bootinfo.h>

/* special SNI prom calls */
/*
 * This does not exist in all proms - SINIX compares
 * the prom env variable "version" against "2.0008"
 * or greater. If lesser it tries to probe interesting
 * registers
 */
#define PROM_GET_MEMCONF	58

#define PROM_VEC		(u64 *)CKSEG1ADDR(0x1fc00000)
#define PROM_ENTRY(x)		(PROM_VEC + (x))


#undef DEBUG
#ifdef DEBUG
#define DBG_PRINTF(x...)     prom_printf(x)
#else
#define DBG_PRINTF(x...)
#endif

static int *(*__prom_putchar)(int)        = (int *(*)(int))PROM_ENTRY(PROM_PUTCHAR);
static char *(*__prom_getenv)(char *)     = (char *(*)(char *))PROM_ENTRY(PROM_GETENV);
static void (*__prom_get_memconf)(void *) = (void (*)(void *))PROM_ENTRY(PROM_GET_MEMCONF);

char *prom_getenv (char *s)
{
	return __prom_getenv(s);
}

void prom_printf(char *fmt, ...)
{
	va_list args;
	char ppbuf[1024];
	char *bptr;

	va_start(args, fmt);
	vsprintf(ppbuf, fmt, args);

	bptr = ppbuf;

	while (*bptr != 0) {
		if (*bptr == '\n')
			__prom_putchar('\r');

		__prom_putchar(*bptr++);
	}
	va_end(args);
}

unsigned long prom_free_prom_memory(void)
{
	return 0;
}

/*
 * /proc/cpuinfo system type
 *
 */
static const char *systype = "Unknown";
const char *get_system_type(void)
{
	return systype;
}

#define SNI_IDPROM_BASE                0xbff00000
#define SNI_IDPROM_MEMSIZE             (SNI_IDPROM_BASE+0x28)  /* Memsize in 16MB quantities */
#define SNI_IDPROM_BRDTYPE             (SNI_IDPROM_BASE+0x29)  /* Board Type */
#define SNI_IDPROM_CPUTYPE             (SNI_IDPROM_BASE+0x30)  /* CPU Type */

#define SNI_IDPROM_SIZE	0x1000

#ifdef DEBUG
static void sni_idprom_dump(void)
{
	int	i;

	prom_printf("SNI IDProm dump (first 128byte):\n");
	for(i=0;i<128;i++) {
		if (i%16 == 0)
			prom_printf("%04x ", i);

		prom_printf("%02x ", *(unsigned char *) (SNI_IDPROM_BASE+i));

		if (i%16 == 15)
			prom_printf("\n");
	}
}
#endif

static void sni_mem_init(void )
{
	int i, memsize;
	struct membank {
	        u32		size;
	        u32		base;
	        u32		size2;
	        u32		pad1;
	        u32		pad2;
	} memconf[8];

	/* MemSIZE from prom in 16MByte chunks */
	memsize=*((unsigned char *) SNI_IDPROM_MEMSIZE) * 16;

	DBG_PRINTF("IDProm memsize: %lu MByte\n", memsize);

	/* get memory bank layout from prom */
	__prom_get_memconf(&memconf);

	DBG_PRINTF("prom_get_mem_conf memory configuration:\n");
	for(i=0;i<8 && memconf[i].size;i++) {
		prom_printf("Bank%d: %08x @ %08x\n", i,
			memconf[i].size, memconf[i].base);
		add_memory_region(memconf[i].base, memconf[i].size, BOOT_MEM_RAM);
	}
}

void __init prom_init(void)
{
	int argc = fw_arg0;
	char **argv = (void *)fw_arg1;
	unsigned int sni_brd_type = *(unsigned char *) SNI_IDPROM_BRDTYPE;
	int i;

	DBG_PRINTF("Found SNI brdtype %02x\n", sni_brd_type);

#ifdef DEBUG
	sni_idprom_dump();
#endif
	sni_mem_init();

	/* copy prom cmdline parameters to kernel cmdline */
	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}
}

