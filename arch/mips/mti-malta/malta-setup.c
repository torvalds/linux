// SPDX-License-Identifier: GPL-2.0-only
/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2008 Dmitri Vorobiev
 */
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/of_fdt.h>
#include <linux/pci.h>
#include <linux/screen_info.h>
#include <linux/time.h>
#include <linux/dma-map-ops.h> /* for dma_default_coherent */

#include <asm/fw/fw.h>
#include <asm/mips-cps.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/malta.h>
#include <asm/mips-boards/maltaint.h>
#include <asm/dma.h>
#include <asm/prom.h>
#include <asm/traps.h>
#ifdef CONFIG_VT
#include <linux/console.h>
#endif

#define ROCIT_CONFIG_GEN0		0x1f403000
#define  ROCIT_CONFIG_GEN0_PCI_IOCU	BIT(7)

static struct resource standard_io_resources[] = {
	{
		.name = "dma1",
		.start = 0x00,
		.end = 0x1f,
		.flags = IORESOURCE_IO | IORESOURCE_BUSY
	},
	{
		.name = "timer",
		.start = 0x40,
		.end = 0x5f,
		.flags = IORESOURCE_IO | IORESOURCE_BUSY
	},
	{
		.name = "keyboard",
		.start = 0x60,
		.end = 0x6f,
		.flags = IORESOURCE_IO
	},
	{
		.name = "dma page reg",
		.start = 0x80,
		.end = 0x8f,
		.flags = IORESOURCE_IO | IORESOURCE_BUSY
	},
	{
		.name = "dma2",
		.start = 0xc0,
		.end = 0xdf,
		.flags = IORESOURCE_IO | IORESOURCE_BUSY
	},
};

const char *get_system_type(void)
{
	return "MIPS Malta";
}

#ifdef CONFIG_BLK_DEV_FD
static void __init fd_activate(void)
{
	/*
	 * Activate Floppy Controller in the SMSC FDC37M817 Super I/O
	 * Controller.
	 * Done by YAMON 2.00 onwards
	 */
	/* Entering config state. */
	SMSC_WRITE(SMSC_CONFIG_ENTER, SMSC_CONFIG_REG);

	/* Activate floppy controller. */
	SMSC_WRITE(SMSC_CONFIG_DEVNUM, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_DEVNUM_FLOPPY, SMSC_DATA_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE, SMSC_CONFIG_REG);
	SMSC_WRITE(SMSC_CONFIG_ACTIVATE_ENABLE, SMSC_DATA_REG);

	/* Exit config state. */
	SMSC_WRITE(SMSC_CONFIG_EXIT, SMSC_CONFIG_REG);
}
#endif

static void __init plat_setup_iocoherency(void)
{
	u32 cfg;

	if (mips_revision_sconid == MIPS_REVISION_SCON_BONITO) {
		if (BONITO_PCICACHECTRL & BONITO_PCICACHECTRL_CPUCOH_PRES) {
			BONITO_PCICACHECTRL |= BONITO_PCICACHECTRL_CPUCOH_EN;
			pr_info("Enabled Bonito CPU coherency\n");
			dma_default_coherent = true;
		}
		if (strstr(fw_getcmdline(), "iobcuncached")) {
			BONITO_PCICACHECTRL &= ~BONITO_PCICACHECTRL_IOBCCOH_EN;
			BONITO_PCIMEMBASECFG = BONITO_PCIMEMBASECFG &
				~(BONITO_PCIMEMBASECFG_MEMBASE0_CACHED |
				  BONITO_PCIMEMBASECFG_MEMBASE1_CACHED);
			pr_info("Disabled Bonito IOBC coherency\n");
		} else {
			BONITO_PCICACHECTRL |= BONITO_PCICACHECTRL_IOBCCOH_EN;
			BONITO_PCIMEMBASECFG |=
				(BONITO_PCIMEMBASECFG_MEMBASE0_CACHED |
				 BONITO_PCIMEMBASECFG_MEMBASE1_CACHED);
			pr_info("Enabled Bonito IOBC coherency\n");
		}
	} else if (mips_cps_numiocu(0) != 0) {
		/* Nothing special needs to be done to enable coherency */
		pr_info("CMP IOCU detected\n");
		cfg = __raw_readl((u32 *)CKSEG1ADDR(ROCIT_CONFIG_GEN0));
		if (cfg & ROCIT_CONFIG_GEN0_PCI_IOCU)
			dma_default_coherent = true;
		else
			pr_crit("IOCU OPERATION DISABLED BY SWITCH - DEFAULTING TO SW IO COHERENCY\n");
	}

	if (dma_default_coherent)
		pr_info("Hardware DMA cache coherency enabled\n");
	else
		pr_info("Software DMA cache coherency enabled\n");
}

static void __init pci_clock_check(void)
{
	unsigned int __iomem *jmpr_p =
		(unsigned int *) ioremap(MALTA_JMPRS_REG, sizeof(unsigned int));
	int jmpr = (__raw_readl(jmpr_p) >> 2) & 0x07;
	static const int pciclocks[] __initconst = {
		33, 20, 25, 30, 12, 16, 37, 10
	};
	int pciclock = pciclocks[jmpr];
	char *optptr, *argptr = fw_getcmdline();

	/*
	 * If user passed a pci_clock= option, don't tack on another one
	 */
	optptr = strstr(argptr, "pci_clock=");
	if (optptr && (optptr == argptr || optptr[-1] == ' '))
		return;

	if (pciclock != 33) {
		pr_warn("WARNING: PCI clock is %dMHz, setting pci_clock\n",
			pciclock);
		argptr += strlen(argptr);
		sprintf(argptr, " pci_clock=%d", pciclock);
		if (pciclock < 20 || pciclock > 66)
			pr_warn("WARNING: IDE timing calculations will be "
			        "incorrect\n");
	}
}

#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE)
static void __init screen_info_setup(void)
{
	static struct screen_info si = {
		.orig_x = 0,
		.orig_y = 25,
		.ext_mem_k = 0,
		.orig_video_page = 0,
		.orig_video_mode = 0,
		.orig_video_cols = 80,
		.unused2 = 0,
		.orig_video_ega_bx = 0,
		.unused3 = 0,
		.orig_video_lines = 25,
		.orig_video_isVGA = VIDEO_TYPE_VGAC,
		.orig_video_points = 16
	};

	vgacon_register_screen(&si);
}
#endif

static void __init bonito_quirks_setup(void)
{
	char *argptr;

	argptr = fw_getcmdline();
	if (strstr(argptr, "debug")) {
		BONITO_BONGENCFG |= BONITO_BONGENCFG_DEBUGMODE;
		pr_info("Enabled Bonito debug mode\n");
	} else
		BONITO_BONGENCFG &= ~BONITO_BONGENCFG_DEBUGMODE;
}

void __init *plat_get_fdt(void)
{
	return (void *)__dtb_start;
}

void __init plat_mem_setup(void)
{
	unsigned int i;
	void *fdt = plat_get_fdt();

	fdt = malta_dt_shim(fdt);
	__dt_setup_arch(fdt);

	if (IS_ENABLED(CONFIG_EVA))
		/* EVA has already been configured in mach-malta/kernel-init.h */
		pr_info("Enhanced Virtual Addressing (EVA) activated\n");

	mips_pcibios_init();

	/* Request I/O space for devices used on the Malta board. */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		insert_resource(&ioport_resource, standard_io_resources + i);

	/*
	 * Enable DMA channel 4 (cascade channel) in the PIIX4 south bridge.
	 */
	enable_dma(4);

	if (mips_revision_sconid == MIPS_REVISION_SCON_BONITO)
		bonito_quirks_setup();

	plat_setup_iocoherency();

	pci_clock_check();

#ifdef CONFIG_BLK_DEV_FD
	fd_activate();
#endif

#if defined(CONFIG_VT) && defined(CONFIG_VGA_CONSOLE)
	screen_info_setup();
#endif
}
