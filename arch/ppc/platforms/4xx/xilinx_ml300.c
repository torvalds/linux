/*
 * arch/ppc/platforms/4xx/xilinx_ml300.c
 *
 * Xilinx ML300 evaluation board initialization
 *
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * 2002-2004 (c) MontaVista Software, Inc.  This file is licensed under the
 * terms of the GNU General Public License version 2.  This program is licensed
 * "as is" without any warranty of any kind, whether express or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/serialP.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/ocp.h>

#include <platforms/4xx/virtex-ii_pro.h>	/* for NR_SER_PORTS */

/*
 * As an overview of how the following functions (platform_init,
 * ml300_map_io, ml300_setup_arch and ml300_init_IRQ) fit into the
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
 *    *ppc_md.setup_io_mappings == ml300_map_io	this file
 *      ppc4xx_map_io				arch/ppc/syslib/ppc4xx_setup.c
 *  start_kernel				init/main.c
 *    setup_arch				arch/ppc/kernel/setup.c
 * #if defined(CONFIG_KGDB)
 *      *ppc_md.kgdb_map_scc() == gen550_kgdb_map_scc
 * #endif
 *      *ppc_md.setup_arch == ml300_setup_arch	this file
 *        ppc4xx_setup_arch			arch/ppc/syslib/ppc4xx_setup.c
 *          ppc4xx_find_bridges			arch/ppc/syslib/ppc405_pci.c
 *    init_IRQ					arch/ppc/kernel/irq.c
 *      *ppc_md.init_IRQ == ml300_init_IRQ	this file
 *        ppc4xx_init_IRQ			arch/ppc/syslib/ppc4xx_setup.c
 *          ppc4xx_pic_init			arch/ppc/syslib/xilinx_pic.c
 */

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
ml300_map_io(void)
{
	ppc4xx_map_io();

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	powerdown_base = ioremap((unsigned long) powerdown_base,
				 XPAR_POWER_0_POWERDOWN_HIGHADDR -
				 XPAR_POWER_0_POWERDOWN_BASEADDR + 1);
#endif
}

static void __init
ml300_early_serial_map(void)
{
#ifdef CONFIG_SERIAL_8250
	struct serial_state old_ports[] = { SERIAL_PORT_DFNS };
	struct uart_port port;
	int i;

	/* Setup ioremapped serial port access */
	for (i = 0; i < ARRAY_SIZE(old_ports); i++ ) {
		memset(&port, 0, sizeof(port));
		port.membase = ioremap((phys_addr_t)(old_ports[i].iomem_base), 16);
		port.irq = old_ports[i].irq;
		port.uartclk = old_ports[i].baud_base * 16;
		port.regshift = old_ports[i].iomem_reg_shift;
		port.iotype = SERIAL_IO_MEM;
		port.flags = ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST;
		port.line = i;

		if (early_serial_setup(&port) != 0) {
			printk("Early serial init of port %d failed\n", i);
		}
	}
#endif /* CONFIG_SERIAL_8250 */
}

void __init
ml300_setup_arch(void)
{
	ppc4xx_setup_arch();	/* calls ppc4xx_find_bridges() */

	ml300_early_serial_map();

	/* Identify the system */
	printk(KERN_INFO "Xilinx Virtex-II Pro port\n");
	printk(KERN_INFO "Port by MontaVista Software, Inc. (source@mvista.com)\n");
}

/* Called after board_setup_irq from ppc4xx_init_IRQ(). */
void __init
ml300_init_irq(void)
{
	ppc4xx_init_IRQ();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = ml300_setup_arch;
	ppc_md.setup_io_mappings = ml300_map_io;
	ppc_md.init_IRQ = ml300_init_irq;

#if defined(XPAR_POWER_0_POWERDOWN_BASEADDR)
	ppc_md.power_off = xilinx_power_off;
#endif

#ifdef CONFIG_KGDB
	ppc_md.early_serial_map = ml300_early_serial_map;
#endif
}

