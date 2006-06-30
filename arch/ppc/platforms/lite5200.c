/*
 * Platform support file for the Freescale LITE5200 based on MPC52xx.
 * A maximum of this file should be moved to syslib/mpc52xx_?????
 * so that new platform based on MPC52xx need a minimal platform file
 * ( avoid code duplication )
 *
 * 
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Based on the 2.4 code written by Kent Borg,
 * Dale Farnsworth <dale.farnsworth@mvista.com> and
 * Wolfgang Denk <wd@denx.de>
 * 
 * Copyright 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright 2003 Motorola Inc.
 * Copyright 2003 MontaVista Software Inc.
 * Copyright 2003 DENX Software Engineering (wd@denx.de)
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/initrd.h>
#include <linux/seq_file.h>
#include <linux/kdev_t.h>
#include <linux/root_dev.h>
#include <linux/console.h>
#include <linux/module.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mpc52xx.h>
#include <asm/ppc_sys.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>


extern int powersave_nap;

/* Board data given by U-Boot */
bd_t __res;
EXPORT_SYMBOL(__res);	/* For modules */


/* ======================================================================== */
/* Platform specific code                                                   */
/* ======================================================================== */

/* Supported PSC function in "preference" order */
struct mpc52xx_psc_func mpc52xx_psc_functions[] = {
		{       .id     = 0,
			.func   = "uart",
		},
		{       .id     = -1,   /* End entry */
			.func   = NULL,
		}
	};


static int
lite5200_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "machine\t\t: Freescale LITE5200\n");
	return 0;
}

#ifdef CONFIG_PCI
#ifdef CONFIG_LITE5200B
static int
lite5200_map_irq(struct pci_dev *dev, unsigned char idsel,
		    unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *      PCI IDSEL/INTPIN->INTLINE
	 *        A             B             C             D
	 */
	{
		{MPC52xx_IRQ0, MPC52xx_IRQ1, MPC52xx_IRQ2, MPC52xx_IRQ3},
		{MPC52xx_IRQ1, MPC52xx_IRQ2, MPC52xx_IRQ3, MPC52xx_IRQ0},
	};

	const long min_idsel = 24, max_idsel = 25, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}
#else /* Original Lite */
static int
lite5200_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	return (pin == 1) && (idsel==24) ? MPC52xx_IRQ0 : -1;
}
#endif
#endif

static void __init
lite5200_setup_cpu(void)
{
	struct mpc52xx_gpio __iomem *gpio;
	struct mpc52xx_intr __iomem *intr;

	u32 port_config;
	u32 intr_ctrl;

	/* Map zones */
	gpio = ioremap(MPC52xx_PA(MPC52xx_GPIO_OFFSET), MPC52xx_GPIO_SIZE);
	intr = ioremap(MPC52xx_PA(MPC52xx_INTR_OFFSET), MPC52xx_INTR_SIZE);

	if (!gpio || !intr) {
		printk(KERN_ERR __FILE__ ": "
			"Error while mapping GPIO/INTR during "
			"lite5200_setup_cpu\n");
		goto unmap_regs;
	}

	/* Get port mux config */
	port_config = in_be32(&gpio->port_config);

	/* 48Mhz internal, pin is GPIO */
	port_config &= ~0x00800000;

	/* USB port */
	port_config &= ~0x00007000;	/* Differential mode - USB1 only */
	port_config |=  0x00001000;

	/* ATA CS is on csb_4/5 */
	port_config &= ~0x03000000;
	port_config |=  0x01000000;

	/* Commit port config */
	out_be32(&gpio->port_config, port_config);

	/* IRQ[0-3] setup */
	intr_ctrl = in_be32(&intr->ctrl);
	intr_ctrl &= ~0x00ff0000;
#ifdef CONFIG_LITE5200B
	/* IRQ[0-3] Level Active Low */
	intr_ctrl |=  0x00ff0000;
#else
	/* IRQ0 Level Active Low
	 * IRQ[1-3] Level Active High */
 	intr_ctrl |=  0x00c00000;
#endif
	out_be32(&intr->ctrl, intr_ctrl);

	/* Unmap reg zone */
unmap_regs:
	if (gpio) iounmap(gpio);
	if (intr) iounmap(intr);
}

static void __init
lite5200_setup_arch(void)
{
	/* CPU & Port mux setup */
	mpc52xx_setup_cpu();	/* Generic */
	lite5200_setup_cpu();	/* Platform specific */

#ifdef CONFIG_PCI
	/* PCI Bridge setup */
	mpc52xx_find_bridges();
#endif
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
              unsigned long r6, unsigned long r7)
{
	/* Generic MPC52xx platform initialization */
	/* TODO Create one and move a max of stuff in it.
	   Put this init in the syslib */

	struct bi_record *bootinfo = find_bootinfo();

	if (bootinfo)
		parse_bootinfo(bootinfo);
	else {
		/* Load the bd_t board info structure */
		if (r3)
			memcpy((void*)&__res,(void*)(r3+KERNELBASE),
					sizeof(bd_t));

#ifdef CONFIG_BLK_DEV_INITRD
		/* Load the initrd */
		if (r4) {
			initrd_start = r4 + KERNELBASE;
			initrd_end = r5 + KERNELBASE;
		}
#endif

		/* Load the command line */
		if (r6) {
			*(char *)(r7+KERNELBASE) = 0;
			strcpy(cmd_line, (char *)(r6+KERNELBASE));
		}
	}

	/* PPC Sys identification */
	identify_ppc_sys_by_id(mfspr(SPRN_SVR));

	/* BAT setup */
	mpc52xx_set_bat();

	/* No ISA bus by default */
#ifdef CONFIG_PCI
	isa_io_base		= 0;
	isa_mem_base		= 0;
#endif

	/* Powersave */
	/* This is provided as an example on how to do it. But you
	   need to be aware that NAP disable bus snoop and that may
	   be required for some devices to work properly, like USB ... */
	/* powersave_nap = 1; */


	/* Setup the ppc_md struct */
	ppc_md.setup_arch	= lite5200_setup_arch;
	ppc_md.show_cpuinfo	= lite5200_show_cpuinfo;
	ppc_md.show_percpuinfo	= NULL;
	ppc_md.init_IRQ		= mpc52xx_init_irq;
	ppc_md.get_irq		= mpc52xx_get_irq;

#ifdef CONFIG_PCI
	ppc_md.pci_map_irq	= lite5200_map_irq;
#endif

	ppc_md.find_end_of_memory = mpc52xx_find_end_of_memory;
	ppc_md.setup_io_mappings  = mpc52xx_map_io;

	ppc_md.restart		= mpc52xx_restart;
	ppc_md.power_off	= mpc52xx_power_off;
	ppc_md.halt		= mpc52xx_halt;

		/* No time keeper on the LITE5200 */
	ppc_md.time_init	= NULL;
	ppc_md.get_rtc_time	= NULL;
	ppc_md.set_rtc_time	= NULL;

	ppc_md.calibrate_decr	= mpc52xx_calibrate_decr;
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress		= mpc52xx_progress;
#endif
}

