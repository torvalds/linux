/*
 * arch/ppc/platforms/85xx/sbc8560.c
 * 
 * Wind River SBC8560 board specific routines
 * 
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2004 Freescale Semiconductor Inc.
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
#include <mm/mmu_decl.h>

#include <syslib/ppc85xx_common.h>
#include <syslib/ppc85xx_setup.h>

#ifdef CONFIG_SERIAL_8250
static void __init
sbc8560_early_serial_map(void)
{
        struct uart_port uart_req;
 
        /* Setup serial port access */
        memset(&uart_req, 0, sizeof (uart_req));
	uart_req.irq = MPC85xx_IRQ_EXT9;
	uart_req.flags = STD_COM_FLAGS;
	uart_req.uartclk = BASE_BAUD * 16;
        uart_req.iotype = SERIAL_IO_MEM;
        uart_req.mapbase = UARTA_ADDR;
        uart_req.membase = ioremap(uart_req.mapbase, MPC85xx_UART0_SIZE);
	uart_req.type = PORT_16650;

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
        gen550_init(0, &uart_req);
#endif
 
        if (early_serial_setup(&uart_req) != 0)
                printk("Early serial init of port 0 failed\n");
 
        /* Assume early_serial_setup() doesn't modify uart_req */
	uart_req.line = 1;
        uart_req.mapbase = UARTB_ADDR;
        uart_req.membase = ioremap(uart_req.mapbase, MPC85xx_UART1_SIZE);
	uart_req.irq = MPC85xx_IRQ_EXT10;
 
#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
        gen550_init(1, &uart_req);
#endif
 
        if (early_serial_setup(&uart_req) != 0)
                printk("Early serial init of port 1 failed\n");
}
#endif

/* ************************************************************************
 *
 * Setup the architecture
 *
 */
static void __init
sbc8560_setup_arch(void)
{
	bd_t *binfo = (bd_t *) __res;
	unsigned int freq;
	struct gianfar_platform_data *pdata;

	/* get the core frequency */
	freq = binfo->bi_intfreq;

	if (ppc_md.progress)
		ppc_md.progress("sbc8560_setup_arch()", 0);

	/* Set loops_per_jiffy to a half-way reasonable value,
	   for use until calibrate_delay gets called. */
	loops_per_jiffy = freq / HZ;

#ifdef CONFIG_PCI
	/* setup PCI host bridges */
	mpc85xx_setup_hose();
#endif
#ifdef CONFIG_SERIAL_8250
	sbc8560_early_serial_map();
#endif
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/* Invalidate the entry we stole earlier the serial ports
	 * should be properly mapped */ 
	invalidate_tlbcam_entry(num_tlbcam_entries - 1);
#endif

	/* setup the board related information for the enet controllers */
	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC1);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->interruptPHY = MPC85xx_IRQ_EXT6;
		pdata->phyid = 25;
		/* fixup phy address */
		pdata->phy_reg_addr += binfo->bi_immr_base;
		memcpy(pdata->mac_addr, binfo->bi_enetaddr, 6);
	}

	pdata = (struct gianfar_platform_data *) ppc_sys_get_pdata(MPC85xx_TSEC2);
	if (pdata) {
		pdata->board_flags = FSL_GIANFAR_BRD_HAS_PHY_INTR;
		pdata->interruptPHY = MPC85xx_IRQ_EXT7;
		pdata->phyid = 26;
		/* fixup phy address */
		pdata->phy_reg_addr += binfo->bi_immr_base;
		memcpy(pdata->mac_addr, binfo->bi_enet1addr, 6);
	}

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

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	/* Use the last TLB entry to map CCSRBAR to allow access to DUART regs */
	settlbcam(num_tlbcam_entries - 1, UARTA_ADDR,
		  UARTA_ADDR, 0x1000, _PAGE_IO, 0);
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
	ppc_md.setup_arch = sbc8560_setup_arch;
	ppc_md.show_cpuinfo = sbc8560_show_cpuinfo;

	ppc_md.init_IRQ = sbc8560_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = mpc85xx_restart;
	ppc_md.power_off = mpc85xx_power_off;
	ppc_md.halt = mpc85xx_halt;

	ppc_md.find_end_of_memory = mpc85xx_find_end_of_memory;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = mpc85xx_calibrate_decr;

#if defined(CONFIG_SERIAL_8250) && defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.progress = gen550_progress;
#endif	/* CONFIG_SERIAL_8250 && CONFIG_SERIAL_TEXT_DEBUG */
#if defined(CONFIG_SERIAL_8250) && defined(CONFIG_KGDB)
	ppc_md.early_serial_map = sbc8560_early_serial_map;
#endif	/* CONFIG_SERIAL_8250 && CONFIG_KGDB */

	if (ppc_md.progress)
		ppc_md.progress("sbc8560_init(): exit", 0);
}
