/*
 * arch/ppc/platforms/85xx/tqm85xx.c
 *
 * TQM85xx (40/41/55/60) board specific routines
 *
 * Copyright (c) 2005 DENX Software Engineering
 * Stefan Roese <sr@denx.de>
 *
 * Based on original work by
 * 	Kumar Gala <galak@kernel.crashing.org>
 *      Copyright 2004 Freescale Semiconductor Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>	/* for linux/serial_core.h */
#include <linux/serial_core.h>
#include <linux/initrd.h>
#include <linux/module.h>
#include <linux/fsl_devices.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/atomic.h>
#include <asm/time.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/open_pic.h>
#include <asm/bootinfo.h>
#include <asm/pci-bridge.h>
#include <asm/mpc85xx.h>
#include <asm/irq.h>
#include <asm/immap_85xx.h>
#include <asm/kgdb.h>
#include <asm/ppc_sys.h>
#include <asm/cpm2.h>
#include <mm/mmu_decl.h>

#include <syslib/ppc85xx_setup.h>
#include <syslib/cpm2_pic.h>
#include <syslib/ppc85xx_common.h>
#include <syslib/ppc85xx_rio.h>

#ifndef CONFIG_PCI
unsigned long isa_io_base = 0;
unsigned long isa_mem_base = 0;
#endif


extern unsigned long total_memory;	/* in mm/init */

unsigned char __res[sizeof (bd_t)];

/* Internal interrupts are all Level Sensitive, and Positive Polarity */
static u_char tqm85xx_openpic_initsenses[] __initdata = {
	MPC85XX_INTERNAL_IRQ_SENSES,
	0x0,						/* External  0: */
	0x0,						/* External  1: */
#if defined(CONFIG_PCI)
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 2: PCI INTA */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 3: PCI INTB */
#else
	0x0,				/* External  2: */
	0x0,				/* External  3: */
#endif
	0x0,				/* External  4: */
	0x0,				/* External  5: */
	0x0,				/* External  6: */
	0x0,				/* External  7: */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* External 8: PHY */
	0x0,				/* External  9: */
	0x0,				/* External 10: */
	0x0,				/* External 11: */
};

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init
tqm85xx_setup_arch(void)
{
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;
	struct gianfar_platform_data *pdata;
	struct gianfar_mdio_data *mdata;

#ifdef CONFIG_MPC8560
	cpm2_reset();
#endif

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	if (ppc_md.progress)
		ppc_md.progress("tqm85xx_setup_arch()", 0);

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = freq / HZ;

#ifdef CONFIG_PCI
	/* setup PCI host bridges */
	mpc85xx_setup_hose();
#endif

#ifndef CONFIG_MPC8560
#if defined(CONFIG_SERIAL_8250)
	mpc85xx_early_serial_map();
#endif

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/* Invalidate the entry we stole earlier the serial ports
	 * should be properly mapped */
	invalidate_tlbcam_entry(num_tlbcam_entries - 1);
#endif
#endif /* CONFIG_MPC8560 */

	/* setup the board related info for the MDIO bus */
	mdata = (struct gianfar_mdio_data *) ppc_sys_get_pdata(MPC85xx_MDIO);

	mdata->irq[0] = MPC85xx_IRQ_EXT8;
	mdata->irq[1] = MPC85xx_IRQ_EXT8;
	mdata->irq[2] = -1;
	mdata->irq[3] = MPC85xx_IRQ_EXT8;
	mdata->irq[31] = -1;

	/* setup the board related information for the enet controllers */
	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC1);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->bus_id = 0;
		pdata->phy_id = 2;
		memcpy(pdata->mac_addr, binfo->bi_enetaddr, 6);
	}

	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC2);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->bus_id = 0;
		pdata->phy_id = 1;
		memcpy(pdata->mac_addr, binfo->bi_enet1addr, 6);
	}

#ifdef CONFIG_MPC8540
	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_FEC);
	if (pdata) {
		pdata->board_flags = 0;
		pdata->bus_id = 0;
		pdata->phy_id = 3;
		memcpy(pdata->mac_addr, binfo->bi_enet2addr, 6);
	}
#endif

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef  CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
	ROOT_DEV = Root_HDA1;
#endif
}

#ifdef CONFIG_MPC8560
static irqreturn_t cpm2_cascade(int irq, void *dev_id, struct pt_regs *regs)
{
	while ((irq = cpm2_get_irq(regs)) >= 0)
		__do_IRQ(irq, regs);
	return IRQ_HANDLED;
}

static struct irqaction cpm2_irqaction = {
	.handler = cpm2_cascade,
	.flags = SA_INTERRUPT,
	.mask = CPU_MASK_NONE,
	.name = "cpm2_cascade",
};
#endif /* CONFIG_MPC8560 */

void __init
tqm85xx_init_IRQ(void)
{
	bd_t *binfo = (bd_t *) __res;

	/* Determine the Physical Address of the OpenPIC regs */
	phys_addr_t OpenPIC_PAddr =
		binfo->bi_immr_base + MPC85xx_OPENPIC_OFFSET;
	OpenPIC_Addr = ioremap(OpenPIC_PAddr, MPC85xx_OPENPIC_SIZE);
	OpenPIC_InitSenses = tqm85xx_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof (tqm85xx_openpic_initsenses);

	/* Skip reserved space and internal sources */
	openpic_set_sources(0, 32, OpenPIC_Addr + 0x10200);

	/* Map PIC IRQs 0-11 */
	openpic_set_sources(48, 12, OpenPIC_Addr + 0x10000);

	/* we let openpic interrupts starting from an offset, to
	 * leave space for cascading interrupts underneath.
	 */
	openpic_init(MPC85xx_OPENPIC_IRQ_OFFSET);

#ifdef CONFIG_MPC8560
	/* Setup CPM2 PIC */
        cpm2_init_IRQ();

	setup_irq(MPC85xx_IRQ_CPM, &cpm2_irqaction);
#endif /* CONFIG_MPC8560 */

	return;
}

int tqm85xx_show_cpuinfo(struct seq_file *m)
{
	uint pvid, svid, phid1;
	uint memsize = total_memory;
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	pvid = mfspr(SPRN_PVR);
	svid = mfspr(SPRN_SVR);

	seq_printf(m, "Vendor\t\t: TQ Components\n");
	seq_printf(m, "Machine\t\t: TQM%s\n", cur_ppc_sys_spec->ppc_sys_name);
	seq_printf(m, "clock\t\t: %dMHz\n", freq / 1000000);
	seq_printf(m, "PVR\t\t: 0x%x\n", pvid);
	seq_printf(m, "SVR\t\t: 0x%x\n", svid);

	/* Display cpu Pll setting */
	phid1 = mfspr(SPRN_HID1);
	seq_printf(m, "PLL setting\t: 0x%x\n", ((phid1 >> 24) & 0x3f));

	/* Display the amount of memory */
	seq_printf(m, "Memory\t\t: %d MB\n", memsize / (1024 * 1024));

	return 0;
}

#if defined(CONFIG_I2C) && defined(CONFIG_SENSORS_DS1337)
extern ulong ds1337_get_rtc_time(void);
extern int ds1337_set_rtc_time(unsigned long nowtime);

static int __init
tqm85xx_rtc_hookup(void)
{
	struct timespec	tv;

        ppc_md.set_rtc_time = ds1337_set_rtc_time;
        ppc_md.get_rtc_time = ds1337_get_rtc_time;

	tv.tv_nsec = 0;
	tv.tv_sec = (ppc_md.get_rtc_time)();
	do_settimeofday(&tv);

	return 0;
}
late_initcall(tqm85xx_rtc_hookup);
#endif

#ifdef CONFIG_PCI
/*
 * interrupt routing
 */
int mpc85xx_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
		/*
		 *      PCI IDSEL/INTPIN->INTLINE
		 *       A      B      C      D
		 */
		{
			{PIRQA, PIRQB, 0, 0},
		};

	const long min_idsel = 0x1c, max_idsel = 0x1c, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

int mpc85xx_exclude_device(u_char bus, u_char devfn)
{
	if (bus == 0 && PCI_SLOT(devfn) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

#endif /* CONFIG_PCI */

#ifdef CONFIG_RAPIDIO
void platform_rio_init(void)
{
	/* 512MB RIO LAW at 0xc0000000 */
	mpc85xx_rio_setup(0xc0000000, 0x20000000);
}
#endif /* CONFIG_RAPIDIO */

/* ************************************************************************ */
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	/* parse_bootinfo must always be called first */
	parse_bootinfo(find_bootinfo());

	/*
	 * If we were passed in a board information, copy it into the
	 * residual data area.
	 */
	if (r3) {
		memcpy((void *) __res, (void *) (r3 + KERNELBASE),
		       sizeof (bd_t));
	}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && !defined(CONFIG_MPC8560)
	{
		bd_t *binfo = (bd_t *) __res;
		struct uart_port p;

		/* Use the last TLB entry to map CCSRBAR to allow access to DUART regs */
		settlbcam(num_tlbcam_entries - 1, binfo->bi_immr_base,
			  binfo->bi_immr_base, MPC85xx_CCSRBAR_SIZE, _PAGE_IO, 0);

		memset(&p, 0, sizeof (p));
		p.iotype = SERIAL_IO_MEM;
		p.membase = (void *) binfo->bi_immr_base + MPC85xx_UART0_OFFSET;
		p.uartclk = binfo->bi_busfreq;

		gen550_init(0, &p);

		memset(&p, 0, sizeof (p));
		p.iotype = SERIAL_IO_MEM;
		p.membase = (void *) binfo->bi_immr_base + MPC85xx_UART1_OFFSET;
		p.uartclk = binfo->bi_busfreq;

		gen550_init(1, &p);
	}
#endif

#if defined(CONFIG_BLK_DEV_INITRD)
	/*
	 * If the init RAM disk has been configured in, and there's a valid
	 * starting address for it, set it up.
	 */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif				/* CONFIG_BLK_DEV_INITRD */

	/* Copy the kernel command line arguments to a safe place. */

	if (r6) {
		*(char *) (r7 + KERNELBASE) = 0;
		strcpy(cmd_line, (char *) (r6 + KERNELBASE));
	}

	identify_ppc_sys_by_id(mfspr(SPRN_SVR));

	/* setup the PowerPC module struct */
	ppc_md.setup_arch = tqm85xx_setup_arch;
	ppc_md.show_cpuinfo = tqm85xx_show_cpuinfo;

	ppc_md.init_IRQ = tqm85xx_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = mpc85xx_restart;
	ppc_md.power_off = mpc85xx_power_off;
	ppc_md.halt = mpc85xx_halt;

	ppc_md.find_end_of_memory = mpc85xx_find_end_of_memory;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = mpc85xx_calibrate_decr;

#ifndef CONFIG_MPC8560
#if defined(CONFIG_SERIAL_8250) && defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.progress = gen550_progress;
#endif	/* CONFIG_SERIAL_8250 && CONFIG_SERIAL_TEXT_DEBUG */
#if defined(CONFIG_SERIAL_8250) && defined(CONFIG_KGDB)
	ppc_md.early_serial_map = mpc85xx_early_serial_map;
#endif	/* CONFIG_SERIAL_8250 && CONFIG_KGDB */
#endif /* CONFIG_MPC8560 */

	if (ppc_md.progress)
		ppc_md.progress("tqm85xx_init(): exit", 0);

	return;
}
