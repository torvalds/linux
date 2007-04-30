/*
 * Xilinx ML403 evaluation board initialization
 *
 * Author: Grant Likely <grant.likely@secretlab.ca>
 *
 * 2005-2007 (c) Secret Lab Technologies Ltd.
 * 2002-2004 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/serialP.h>
#include <asm/io.h>
#include <asm/machdep.h>

#include <syslib/gen550.h>
#include <syslib/virtex_devices.h>
#include <platforms/4xx/xparameters/xparameters.h>

/*
 * As an overview of how the following functions (platform_init,
 * ml403_map_io, ml403_setup_arch and ml403_init_IRQ) fit into the
 * kernel startup procedure, here's a call tree:
 *
 * start_here					arch/ppc/kernel/head_4xx.S
 *  early_init					arch/ppc/kernel/setup.c
 *  machine_init				arch/ppc/kernel/setup.c
 *    platform_init				this file
 *      ppc4xx_init				arch/ppc/syslib/ppc4xx_setup.c
 *        parse_bootinfo
 *          find_bootinfo
 *        "setup some default ppc_md pointers"
 *  MMU_init					arch/ppc/mm/init.c
 *    *ppc_md.setup_io_mappings == ml403_map_io	this file
 *      ppc4xx_map_io				arch/ppc/syslib/ppc4xx_setup.c
 *  start_kernel				init/main.c
 *    setup_arch				arch/ppc/kernel/setup.c
 * #if defined(CONFIG_KGDB)
 *      *ppc_md.kgdb_map_scc() == gen550_kgdb_map_scc
 * #endif
 *      *ppc_md.setup_arch == ml403_setup_arch	this file
 *        ppc4xx_setup_arch			arch/ppc/syslib/ppc4xx_setup.c
 *          ppc4xx_find_bridges			arch/ppc/syslib/ppc405_pci.c
 *    init_IRQ					arch/ppc/kernel/irq.c
 *      *ppc_md.init_IRQ == ml403_init_IRQ	this file
 *        ppc4xx_init_IRQ			arch/ppc/syslib/ppc4xx_setup.c
 *          ppc4xx_pic_init			arch/ppc/syslib/xilinx_pic.c
 */

const char* virtex_machine_name = "ML403 Reference Design";

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
static volatile unsigned *powerdown_base =
    (volatile unsigned *) XPAR_POWER_0_POWERDOWN_BASEADDR;

static void
xilinx_power_off(void)
{
	local_irq_disable();
	out_be32(powerdown_base, XPAR_POWER_0_POWERDOWN_VALUE);
	while (1) ;
}
#endif

void __init
ml403_map_io(void)
{
	ppc4xx_map_io();

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	powerdown_base = ioremap((unsigned long) powerdown_base,
				 XPAR_POWER_0_POWERDOWN_HIGHADDR -
				 XPAR_POWER_0_POWERDOWN_BASEADDR + 1);
#endif
}

void __init
ml403_setup_arch(void)
{
	virtex_early_serial_map();
	ppc4xx_setup_arch();	/* calls ppc4xx_find_bridges() */

	/* Identify the system */
	printk(KERN_INFO "Xilinx ML403 Reference System (Virtex-4 FX)\n");
}

/* Called after board_setup_irq from ppc4xx_init_IRQ(). */
void __init
ml403_init_irq(void)
{
	ppc4xx_init_IRQ();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = ml403_setup_arch;
	ppc_md.setup_io_mappings = ml403_map_io;
	ppc_md.init_IRQ = ml403_init_irq;

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	ppc_md.power_off = xilinx_power_off;
#endif

#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = virtex_early_serial_map;
#endif
}

