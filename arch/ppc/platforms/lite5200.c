/*
 * arch/ppc/platforms/lite5200.c
 *
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

#include <linux/config.h>
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

#include <syslib/mpc52xx_pci.h>


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
static int
lite5200_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	return (pin == 1) && (idsel==24) ? MPC52xx_IRQ0 : -1;
}
#endif

static void __init
lite5200_setup_cpu(void)
{
	struct mpc52xx_cdm  __iomem *cdm;
	struct mpc52xx_gpio __iomem *gpio;
	struct mpc52xx_intr __iomem *intr;
	struct mpc52xx_xlb  __iomem *xlb;

	u32 port_config;
	u32 intr_ctrl;

	/* Map zones */
	cdm  = ioremap(MPC52xx_PA(MPC52xx_CDM_OFFSET), MPC52xx_CDM_SIZE);
	gpio = ioremap(MPC52xx_PA(MPC52xx_GPIO_OFFSET), MPC52xx_GPIO_SIZE);
	xlb  = ioremap(MPC52xx_PA(MPC52xx_XLB_OFFSET), MPC52xx_XLB_SIZE);
	intr = ioremap(MPC52xx_PA(MPC52xx_INTR_OFFSET), MPC52xx_INTR_SIZE);

	if (!cdm || !gpio || !xlb || !intr) {
		printk("lite5200.c: Error while mapping CDM/GPIO/XLB/INTR during"
				"lite5200_setup_cpu\n");
		goto unmap_regs;
	}

	/* Use internal 48 Mhz */
	out_8(&cdm->ext_48mhz_en, 0x00);
	out_8(&cdm->fd_enable, 0x01);
	if (in_be32(&cdm->rstcfg) & 0x40)	/* Assumes 33Mhz clock */
		out_be16(&cdm->fd_counters, 0x0001);
	else
		out_be16(&cdm->fd_counters, 0x5555);

	/* Get port mux config */
	port_config = in_be32(&gpio->port_config);

	/* 48Mhz internal, pin is GPIO */
	port_config &= ~0x00800000;

	/* USB port */
	port_config &= ~0x00007000;	/* Differential mode - USB1 only */
	port_config |=  0x00001000;

	/* Commit port config */
	out_be32(&gpio->port_config, port_config);

	/* Configure the XLB Arbiter */
	out_be32(&xlb->master_pri_enable, 0xff);
	out_be32(&xlb->master_priority, 0x11111111);

	/* Enable ram snooping for 1GB window */
	out_be32(&xlb->config, in_be32(&xlb->config) | MPC52xx_XLB_CFG_SNOOP);
	out_be32(&xlb->snoop_window, MPC52xx_PCI_TARGET_MEM | 0x1d);

	/* IRQ[0-3] setup : IRQ0     - Level Active Low  */
	/*                  IRQ[1-3] - Level Active High */
	intr_ctrl = in_be32(&intr->ctrl);
	intr_ctrl &= ~0x00ff0000;
	intr_ctrl |=  0x00c00000;
	out_be32(&intr->ctrl, intr_ctrl);

	/* Unmap reg zone */
unmap_regs:
	if (cdm)  iounmap(cdm);
	if (gpio) iounmap(gpio);
	if (xlb)  iounmap(xlb);
	if (intr) iounmap(intr);
}

static void __init
lite5200_setup_arch(void)
{
	/* CPU & Port mux setup */
	lite5200_setup_cpu();

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
	isa_io_base		= 0;
	isa_mem_base		= 0;

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

