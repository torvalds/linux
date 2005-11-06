/*
 * arch/mips/vr41xx/nec-cmbvr4133/init.c
 *
 * PROM library initialisation code for NEC CMB-VR4133 board.
 *
 * Author: Yoichi Yuasa <yyuasa@mvista.com, or source@mvista.com> and
 *         Jun Sun <jsun@mvista.com, or source@mvista.com> and
 *         Alex Sapkov <asapkov@ru.mvista.com>
 *
 * 2001-2004 (c) MontaVista, Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Support for NEC-CMBVR4133 in 2.6
 * Manish Lachwani (mlachwani@mvista.com)
 */
#include <linux/config.h>

#ifdef CONFIG_ROCKHOPPER
#include <asm/io.h>
#include <linux/pci.h>

#define PCICONFDREG	0xaf000c14
#define PCICONFAREG	0xaf000c18

void disable_pcnet(void)
{
	u32 data;

	/*
	 * Workaround for the bug in PMON on VR4133. PMON leaves
	 * AMD PCNet controller (on Rockhopper) initialized and running in
	 * bus master mode. We have do disable it before doing any
	 * further initialization. Or we get problems with PCI bus 2
	 * and random lockups and crashes.
	 */

	writel((2 << 16)		|
	       (PCI_DEVFN(1,0) << 8)	|
	       (0 & 0xfc)		|
               1UL,
	       PCICONFAREG);

	data = readl(PCICONFDREG);

	writel((2 << 16)		|
	       (PCI_DEVFN(1,0) << 8)	|
	       (4 & 0xfc)		|
               1UL,
	       PCICONFAREG);

	data = readl(PCICONFDREG);

	writel((2 << 16)		|
	       (PCI_DEVFN(1,0) << 8)	|
	       (4 & 0xfc)		|
               1UL,
	       PCICONFAREG);

	data &= ~4;

	writel(data, PCICONFDREG);
}
#endif

