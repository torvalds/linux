/*
 * MIPS boards bootprom interface for the Linux kernel.
 *
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 * Author: Carsten Langgaard, carstenl@mips.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

#ifndef _ASM_MSP_PROM_H
#define _ASM_MSP_PROM_H

#include <linux/types.h>

#define DEVICEID			"deviceid"
#define FEATURES			"features"
#define PROM_ENV			"prom_env"
#define PROM_ENV_FILE			"/proc/"PROM_ENV
#define PROM_ENV_SIZE			256

#define CPU_DEVID_FAMILY		0x0000ff00
#define CPU_DEVID_REVISION		0x000000ff

#define FPGA_IS_POLO(revision) \
		(((revision >= 0xb0) && (revision < 0xd0)))
#define FPGA_IS_5000(revision) \
		((revision >= 0x80) && (revision <= 0x90))
#define	FPGA_IS_ZEUS(revision)		((revision < 0x7f))
#define FPGA_IS_DUET(revision) \
		(((revision >= 0xa0) && (revision < 0xb0)))
#define FPGA_IS_MSP4200(revision)	((revision >= 0xd0))
#define FPGA_IS_MSP7100(revision)	((revision >= 0xd0))

#define MACHINE_TYPE_POLO		"POLO"
#define MACHINE_TYPE_DUET		"DUET"
#define	MACHINE_TYPE_ZEUS		"ZEUS"
#define MACHINE_TYPE_MSP2000REVB	"MSP2000REVB"
#define MACHINE_TYPE_MSP5000		"MSP5000"
#define MACHINE_TYPE_MSP4200		"MSP4200"
#define MACHINE_TYPE_MSP7120		"MSP7120"
#define MACHINE_TYPE_MSP7130		"MSP7130"
#define MACHINE_TYPE_OTHER		"OTHER"

#define MACHINE_TYPE_POLO_FPGA		"POLO-FPGA"
#define MACHINE_TYPE_DUET_FPGA		"DUET-FPGA"
#define	MACHINE_TYPE_ZEUS_FPGA		"ZEUS_FPGA"
#define MACHINE_TYPE_MSP2000REVB_FPGA	"MSP2000REVB-FPGA"
#define MACHINE_TYPE_MSP5000_FPGA	"MSP5000-FPGA"
#define MACHINE_TYPE_MSP4200_FPGA	"MSP4200-FPGA"
#define MACHINE_TYPE_MSP7100_FPGA	"MSP7100-FPGA"
#define MACHINE_TYPE_OTHER_FPGA		"OTHER-FPGA"

/* Device Family definitions */
#define FAMILY_FPGA			0x0000
#define FAMILY_ZEUS			0x1000
#define FAMILY_POLO			0x2000
#define FAMILY_DUET			0x4000
#define FAMILY_TRIAD			0x5000
#define FAMILY_MSP4200			0x4200
#define FAMILY_MSP4200_FPGA		0x4f00
#define FAMILY_MSP7100			0x7100
#define FAMILY_MSP7100_FPGA		0x7f00

/* Device Type definitions */
#define TYPE_MSP7120			0x7120
#define TYPE_MSP7130			0x7130

#define ENET_KEY		'E'
#define ENETTXD_KEY		'e'
#define PCI_KEY			'P'
#define PCIMUX_KEY		'p'
#define SEC_KEY			'S'
#define SPAD_KEY		'D'
#define TDM_KEY			'T'
#define ZSP_KEY			'Z'

#define FEATURE_NOEXIST		'-'
#define FEATURE_EXIST		'+'

#define ENET_MII		'M'
#define ENET_RMII		'R'

#define	ENETTXD_FALLING		'F'
#define ENETTXD_RISING		'R'

#define PCI_HOST		'H'
#define PCI_PERIPHERAL		'P'

#define PCIMUX_FULL		'F'
#define PCIMUX_SINGLE		'S'

#define SEC_DUET		'D'
#define SEC_POLO		'P'
#define SEC_SLOW		'S'
#define SEC_TRIAD		'T'

#define SPAD_POLO		'P'

#define TDM_DUET		'D'	/* DUET TDMs might exist */
#define TDM_POLO		'P'	/* POLO TDMs might exist */
#define TDM_TRIAD		'T'	/* TRIAD TDMs might exist */

#define ZSP_DUET		'D'	/* one DUET zsp engine */
#define ZSP_TRIAD		'T'	/* two TRIAD zsp engines */

extern char *prom_getenv(char *name);
extern void prom_init_cmdline(void);
extern void prom_meminit(void);
extern void prom_fixup_mem_map(unsigned long start_mem,
			       unsigned long end_mem);

#ifdef CONFIG_MTD_PMC_MSP_RAMROOT
extern bool get_ramroot(void **start, unsigned long *size);
#endif

extern int get_ethernet_addr(char *ethaddr_name, char *ethernet_addr);
extern unsigned long get_deviceid(void);
extern char identify_enet(unsigned long interface_num);
extern char identify_enetTxD(unsigned long interface_num);
extern char identify_pci(void);
extern char identify_sec(void);
extern char identify_spad(void);
extern char identify_sec(void);
extern char identify_tdm(void);
extern char identify_zsp(void);
extern unsigned long identify_family(void);
extern unsigned long identify_revision(void);

/*
 * The following macro calls prom_printf and puts the format string
 * into an init section so it can be reclaimed.
 */
#define ppfinit(f, x...) \
	do { \
		static char _f[] __initdata = KERN_INFO f; \
		printk(_f, ## x); \
	} while (0)

/* Memory descriptor management. */
#define PROM_MAX_PMEMBLOCKS    7	/* 6 used */

enum yamon_memtypes {
	yamon_dontuse,
	yamon_prom,
	yamon_free,
};

struct prom_pmemblock {
	unsigned long base; /* Within KSEG0. */
	unsigned int size;  /* In bytes. */
	unsigned int type;  /* free or prom memory */
};

extern int prom_argc;
extern char **prom_argv;
extern char **prom_envp;
extern int *prom_vec;
extern struct prom_pmemblock *prom_getmdesc(void);

#endif /* !_ASM_MSP_PROM_H */
