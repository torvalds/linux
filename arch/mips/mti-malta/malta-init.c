/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM library initialisation code.
 *
 * Copyright (C) 1999,2000,2004,2005,2012  MIPS Technologies, Inc.
 * All rights reserved.
 * Authors: Carsten Langgaard <carstenl@mips.com>
 *         Maciej W. Rozycki <macro@mips.com>
 *          Steven J. Hill <sjhill@mips.com>
 */
#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/pci_regs.h>
#include <linux/serial_core.h>

#include <asm/cacheflush.h>
#include <asm/smp-ops.h>
#include <asm/traps.h>
#include <asm/fw/fw.h>
#include <asm/mips-cps.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/malta.h>

static int mips_revision_corid;
int mips_revision_sconid;

/* Bonito64 system controller register base. */
unsigned long _pcictrl_bonito;
unsigned long _pcictrl_bonito_pcicfg;

/* GT64120 system controller register base */
unsigned long _pcictrl_gt64120;

/* MIPS System controller register base */
unsigned long _pcictrl_msc;

#ifdef CONFIG_SERIAL_8250_CONSOLE
static void __init console_config(void)
{
	char console_string[40];
	int baud = 0;
	char parity = '\0', bits = '\0', flow = '\0';
	char *s;

	s = fw_getenv("modetty0");
	if (s) {
		while (*s >= '0' && *s <= '9')
			baud = baud*10 + *s++ - '0';
		if (*s == ',')
			s++;
		if (*s)
			parity = *s++;
		if (*s == ',')
			s++;
		if (*s)
			bits = *s++;
		if (*s == ',')
			s++;
		if (*s == 'h')
			flow = 'r';
	}
	if (baud == 0)
		baud = 38400;
	if (parity != 'n' && parity != 'o' && parity != 'e')
		parity = 'n';
	if (bits != '7' && bits != '8')
		bits = '8';
	if (flow == '\0')
		flow = 'r';

	if ((strstr(fw_getcmdline(), "earlycon=")) == NULL) {
		sprintf(console_string, "uart8250,io,0x3f8,%d%c%c", baud,
			parity, bits);
		setup_earlycon(console_string);
	}

	if ((strstr(fw_getcmdline(), "console=")) == NULL) {
		sprintf(console_string, " console=ttyS0,%d%c%c%c", baud,
			parity, bits, flow);
		strcat(fw_getcmdline(), console_string);
		pr_info("Config serial console:%s\n", console_string);
	}
}
#endif

static void __init mips_nmi_setup(void)
{
	void *base;
	extern char except_vec_nmi[];

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa80) :
		(void *)(CAC_BASE + 0x380);
	memcpy(base, except_vec_nmi, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

static void __init mips_ejtag_setup(void)
{
	void *base;
	extern char except_vec_ejtag_debug[];

	base = cpu_has_veic ?
		(void *)(CAC_BASE + 0xa00) :
		(void *)(CAC_BASE + 0x300);
	memcpy(base, except_vec_ejtag_debug, 0x80);
	flush_icache_range((unsigned long)base, (unsigned long)base + 0x80);
}

phys_addr_t mips_cpc_default_phys_base(void)
{
	return CPC_BASE_ADDR;
}

void __init prom_init(void)
{
	/*
	 * early setup of _pcictrl_bonito so that we can determine
	 * the system controller on a CORE_EMUL board
	 */
	_pcictrl_bonito = (unsigned long)ioremap(BONITO_REG_BASE, BONITO_REG_SIZE);

	mips_revision_corid = MIPS_REVISION_CORID;

	if (mips_revision_corid == MIPS_REVISION_CORID_CORE_EMUL) {
		if (BONITO_PCIDID == 0x0001df53 ||
		    BONITO_PCIDID == 0x0003df53)
			mips_revision_corid = MIPS_REVISION_CORID_CORE_EMUL_BON;
		else
			mips_revision_corid = MIPS_REVISION_CORID_CORE_EMUL_MSC;
	}

	mips_revision_sconid = MIPS_REVISION_SCONID;
	if (mips_revision_sconid == MIPS_REVISION_SCON_OTHER) {
		switch (mips_revision_corid) {
		case MIPS_REVISION_CORID_QED_RM5261:
		case MIPS_REVISION_CORID_CORE_LV:
		case MIPS_REVISION_CORID_CORE_FPGA:
		case MIPS_REVISION_CORID_CORE_FPGAR2:
			mips_revision_sconid = MIPS_REVISION_SCON_GT64120;
			break;
		case MIPS_REVISION_CORID_CORE_EMUL_BON:
		case MIPS_REVISION_CORID_BONITO64:
		case MIPS_REVISION_CORID_CORE_20K:
			mips_revision_sconid = MIPS_REVISION_SCON_BONITO;
			break;
		case MIPS_REVISION_CORID_CORE_MSC:
		case MIPS_REVISION_CORID_CORE_FPGA2:
		case MIPS_REVISION_CORID_CORE_24K:
			/*
			 * SOCit/ROCit support is essentially identical
			 * but make an attempt to distinguish them
			 */
			mips_revision_sconid = MIPS_REVISION_SCON_SOCIT;
			break;
		case MIPS_REVISION_CORID_CORE_FPGA3:
		case MIPS_REVISION_CORID_CORE_FPGA4:
		case MIPS_REVISION_CORID_CORE_FPGA5:
		case MIPS_REVISION_CORID_CORE_EMUL_MSC:
		default:
			/* See above */
			mips_revision_sconid = MIPS_REVISION_SCON_ROCIT;
			break;
		}
	}

	switch (mips_revision_sconid) {
		u32 start, map, mask, data;

	case MIPS_REVISION_SCON_GT64120:
		/*
		 * Setup the North bridge to do Master byte-lane swapping
		 * when running in bigendian.
		 */
		_pcictrl_gt64120 = (unsigned long)ioremap(MIPS_GT_BASE, 0x2000);

#ifdef CONFIG_CPU_LITTLE_ENDIAN
		GT_WRITE(GT_PCI0_CMD_OFS, GT_PCI0_CMD_MBYTESWAP_BIT |
			 GT_PCI0_CMD_SBYTESWAP_BIT);
#else
		GT_WRITE(GT_PCI0_CMD_OFS, 0);
#endif
		/* Fix up PCI I/O mapping if necessary (for Atlas).  */
		start = GT_READ(GT_PCI0IOLD_OFS);
		map = GT_READ(GT_PCI0IOREMAP_OFS);
		if ((start & map) != 0) {
			map &= ~start;
			GT_WRITE(GT_PCI0IOREMAP_OFS, map);
		}

		set_io_port_base(MALTA_GT_PORT_BASE);
		break;

	case MIPS_REVISION_SCON_BONITO:
		_pcictrl_bonito_pcicfg = (unsigned long)ioremap(BONITO_PCICFG_BASE, BONITO_PCICFG_SIZE);

		/*
		 * Disable Bonito IOBC.
		 */
		BONITO_PCIMEMBASECFG = BONITO_PCIMEMBASECFG &
			~(BONITO_PCIMEMBASECFG_MEMBASE0_CACHED |
			  BONITO_PCIMEMBASECFG_MEMBASE1_CACHED);

		/*
		 * Setup the North bridge to do Master byte-lane swapping
		 * when running in bigendian.
		 */
#ifdef CONFIG_CPU_LITTLE_ENDIAN
		BONITO_BONGENCFG = BONITO_BONGENCFG &
			~(BONITO_BONGENCFG_MSTRBYTESWAP |
			  BONITO_BONGENCFG_BYTESWAP);
#else
		BONITO_BONGENCFG = BONITO_BONGENCFG |
			BONITO_BONGENCFG_MSTRBYTESWAP |
			BONITO_BONGENCFG_BYTESWAP;
#endif

		set_io_port_base(MALTA_BONITO_PORT_BASE);
		break;

	case MIPS_REVISION_SCON_SOCIT:
	case MIPS_REVISION_SCON_ROCIT:
		_pcictrl_msc = (unsigned long)ioremap(MIPS_MSC01_PCI_REG_BASE, 0x2000);
mips_pci_controller:
		mb();
		MSC_READ(MSC01_PCI_CFG, data);
		MSC_WRITE(MSC01_PCI_CFG, data & ~MSC01_PCI_CFG_EN_BIT);
		wmb();

		/* Fix up lane swapping.  */
#ifdef CONFIG_CPU_LITTLE_ENDIAN
		MSC_WRITE(MSC01_PCI_SWAP, MSC01_PCI_SWAP_NOSWAP);
#else
		MSC_WRITE(MSC01_PCI_SWAP,
			  MSC01_PCI_SWAP_BYTESWAP << MSC01_PCI_SWAP_IO_SHF |
			  MSC01_PCI_SWAP_BYTESWAP << MSC01_PCI_SWAP_MEM_SHF |
			  MSC01_PCI_SWAP_BYTESWAP << MSC01_PCI_SWAP_BAR0_SHF);
#endif

		/*
		 * Setup the Malta max (2GB) memory for PCI DMA in host bridge
		 * in transparent addressing mode.
		 */
		mask = PHYS_OFFSET | PCI_BASE_ADDRESS_MEM_PREFETCH;
		MSC_WRITE(MSC01_PCI_BAR0, mask);
		MSC_WRITE(MSC01_PCI_HEAD4, mask);

		mask &= MSC01_PCI_BAR0_SIZE_MSK;
		MSC_WRITE(MSC01_PCI_P2SCMSKL, mask);
		MSC_WRITE(MSC01_PCI_P2SCMAPL, mask);

		/* Don't handle target retries indefinitely.  */
		if ((data & MSC01_PCI_CFG_MAXRTRY_MSK) ==
		    MSC01_PCI_CFG_MAXRTRY_MSK)
			data = (data & ~(MSC01_PCI_CFG_MAXRTRY_MSK <<
					 MSC01_PCI_CFG_MAXRTRY_SHF)) |
			       ((MSC01_PCI_CFG_MAXRTRY_MSK - 1) <<
				MSC01_PCI_CFG_MAXRTRY_SHF);

		wmb();
		MSC_WRITE(MSC01_PCI_CFG, data);
		mb();

		set_io_port_base(MALTA_MSC_PORT_BASE);
		break;

	case MIPS_REVISION_SCON_SOCITSC:
	case MIPS_REVISION_SCON_SOCITSCP:
		_pcictrl_msc = (unsigned long)ioremap(MIPS_SOCITSC_PCI_REG_BASE, 0x2000);
		goto mips_pci_controller;

	default:
		/* Unknown system controller */
		while (1);	/* We die here... */
	}
	board_nmi_handler_setup = mips_nmi_setup;
	board_ejtag_handler_setup = mips_ejtag_setup;

	fw_init_cmdline();
	fw_meminit();
#ifdef CONFIG_SERIAL_8250_CONSOLE
	console_config();
#endif
	/* Early detection of CMP support */
	mips_cpc_probe();

	if (!register_cps_smp_ops())
		return;
	if (!register_cmp_smp_ops())
		return;
	if (!register_vsmp_smp_ops())
		return;
	register_up_smp_ops();
}
