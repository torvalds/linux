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

#define DEBUG

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/console.h>

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


static int *(*__prom_putchar)(int)        = (int *(*)(int))PROM_ENTRY(PROM_PUTCHAR);

void prom_putchar(char c)
{
	__prom_putchar(c);
}

static char *(*__prom_getenv)(char *)     = (char *(*)(char *))PROM_ENTRY(PROM_GETENV);
static void (*__prom_get_memconf)(void *) = (void (*)(void *))PROM_ENTRY(PROM_GET_MEMCONF);

char *prom_getenv (char *s)
{
	return __prom_getenv(s);
}

void __init prom_free_prom_memory(void)
{
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

	pr_debug("SNI IDProm dump:\n");
	for (i = 0; i < 256; i++) {
		if (i%16 == 0)
			pr_debug("%04x ", i);

		printk("%02x ", *(unsigned char *) (SNI_IDPROM_BASE + i));

		if (i % 16 == 15)
			printk("\n");
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
	memsize = *((unsigned char *) SNI_IDPROM_MEMSIZE) * 16;

	pr_debug("IDProm memsize: %lu MByte\n", memsize);

	/* get memory bank layout from prom */
	__prom_get_memconf(&memconf);

	pr_debug("prom_get_mem_conf memory configuration:\n");
	for (i = 0;i < 8 && memconf[i].size; i++) {
		if (sni_brd_type == SNI_BRD_PCI_TOWER ||
		    sni_brd_type == SNI_BRD_PCI_TOWER_CPLUS) {
			if (memconf[i].base >= 0x20000000 &&
			    memconf[i].base <  0x30000000) {
				memconf[i].base -= 0x20000000;
			}
	}
		pr_debug("Bank%d: %08x @ %08x\n", i,
			memconf[i].size, memconf[i].base);
		add_memory_region(memconf[i].base, memconf[i].size, BOOT_MEM_RAM);
	}
}

static void __init sni_console_setup(void)
{
	char *ctype;
	char *cdev;
	char *baud;
	int port;
	static char options[8];

	cdev = prom_getenv ("console_dev");
	if (strncmp (cdev, "tty", 3) == 0) {
		ctype = prom_getenv ("console");
		switch (*ctype) {
		default:
		case 'l':
	                port = 0;
	                baud = prom_getenv("lbaud");
	                break;
		case 'r':
	                port = 1;
	                baud = prom_getenv("rbaud");
	                break;
		}
		if (baud)
			strcpy(options, baud);
		if (strncmp (cdev, "tty552", 6) == 0)
			add_preferred_console("ttyS", port, baud ? options : NULL);
		else
			add_preferred_console("ttySC", port, baud ? options : NULL);
	}
}

void __init prom_init(void)
{
	int argc = fw_arg0;
	char **argv = (void *)fw_arg1;
	int i;
	int cputype;

	sni_brd_type = *(unsigned char *)SNI_IDPROM_BRDTYPE;
	cputype = *(unsigned char *)SNI_IDPROM_CPUTYPE;
	switch (sni_brd_type) {
	case SNI_BRD_TOWER_OASIC:
	        switch (cputype) {
		case SNI_CPU_M8030:
		        systype = "RM400-330";
		        break;
		case SNI_CPU_M8031:
		        systype = "RM400-430";
		        break;
		case SNI_CPU_M8037:
		        systype = "RM400-530";
		        break;
		case SNI_CPU_M8034:
		        systype = "RM400-730";
		        break;
		default:
			systype = "RM400-xxx";
			break;
		}
	        break;
	case SNI_BRD_MINITOWER:
	        switch (cputype) {
		case SNI_CPU_M8021:
		case SNI_CPU_M8043:
		        systype = "RM400-120";
		        break;
		case SNI_CPU_M8040:
		        systype = "RM400-220";
		        break;
		case SNI_CPU_M8053:
		        systype = "RM400-225";
		        break;
		case SNI_CPU_M8050:
		        systype = "RM400-420";
		        break;
		default:
			systype = "RM400-xxx";
			break;
		}
	        break;
	case SNI_BRD_PCI_TOWER:
	        systype = "RM400-Cxx";
	        break;
	case SNI_BRD_RM200:
	        systype = "RM200-xxx";
	        break;
	case SNI_BRD_PCI_MTOWER:
	        systype = "RM300-Cxx";
	        break;
	case SNI_BRD_PCI_DESKTOP:
	        switch (read_c0_prid() & 0xff00) {
		case PRID_IMP_R4600:
		case PRID_IMP_R4700:
		        systype = "RM200-C20";
		        break;
		case PRID_IMP_R5000:
		        systype = "RM200-C40";
		        break;
		default:
		        systype = "RM200-Cxx";
		        break;
		}
	        break;
	case SNI_BRD_PCI_TOWER_CPLUS:
	        systype = "RM400-Exx";
	        break;
	case SNI_BRD_PCI_MTOWER_CPLUS:
	        systype = "RM300-Exx";
	        break;
	}
	pr_debug("Found SNI brdtype %02x name %s\n", sni_brd_type,systype);

#ifdef DEBUG
	sni_idprom_dump();
#endif
	sni_mem_init();
	sni_console_setup();

	/* copy prom cmdline parameters to kernel cmdline */
	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}
}

