/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include <asm/mipsregs.h>
#include <asm/time.h>

#include <asm/netlogic/common.h>
#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlp-hal/iomap.h>
#include <asm/netlogic/xlp-hal/xlp.h>
#include <asm/netlogic/xlp-hal/bridge.h>
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/sys.h>

/* Main initialization */
void nlm_node_init(int node)
{
	struct nlm_soc_info *nodep;

	nodep = nlm_get_node(node);
	if (node == 0)
		nodep->coremask = 1;	/* node 0, boot cpu */
	nodep->sysbase = nlm_get_sys_regbase(node);
	nodep->picbase = nlm_get_pic_regbase(node);
	nodep->ebase = read_c0_ebase() & (~((1 << 12) - 1));
	if (cpu_is_xlp9xx())
		nodep->socbus = xlp9xx_get_socbus(node);
	else
		nodep->socbus = 0;
	spin_lock_init(&nodep->piclock);
}

static int xlp9xx_irq_to_irt(int irq)
{
	switch (irq) {
	case PIC_GPIO_IRQ:
		return 12;
	case PIC_I2C_0_IRQ:
		return 125;
	case PIC_I2C_1_IRQ:
		return 126;
	case PIC_I2C_2_IRQ:
		return 127;
	case PIC_I2C_3_IRQ:
		return 128;
	case PIC_9XX_XHCI_0_IRQ:
		return 114;
	case PIC_9XX_XHCI_1_IRQ:
		return 115;
	case PIC_9XX_XHCI_2_IRQ:
		return 116;
	case PIC_UART_0_IRQ:
		return 133;
	case PIC_UART_1_IRQ:
		return 134;
	case PIC_SATA_IRQ:
		return 143;
	case PIC_NAND_IRQ:
		return 151;
	case PIC_SPI_IRQ:
		return 152;
	case PIC_MMC_IRQ:
		return 153;
	case PIC_PCIE_LINK_LEGACY_IRQ(0):
	case PIC_PCIE_LINK_LEGACY_IRQ(1):
	case PIC_PCIE_LINK_LEGACY_IRQ(2):
	case PIC_PCIE_LINK_LEGACY_IRQ(3):
		return 191 + irq - PIC_PCIE_LINK_LEGACY_IRQ_BASE;
	}
	return -1;
}

static int xlp_irq_to_irt(int irq)
{
	uint64_t pcibase;
	int devoff, irt;

	devoff = 0;
	switch (irq) {
	case PIC_UART_0_IRQ:
		devoff = XLP_IO_UART0_OFFSET(0);
		break;
	case PIC_UART_1_IRQ:
		devoff = XLP_IO_UART1_OFFSET(0);
		break;
	case PIC_MMC_IRQ:
		devoff = XLP_IO_MMC_OFFSET(0);
		break;
	case PIC_I2C_0_IRQ:	/* I2C will be fixed up */
	case PIC_I2C_1_IRQ:
	case PIC_I2C_2_IRQ:
	case PIC_I2C_3_IRQ:
		if (cpu_is_xlpii())
			devoff = XLP2XX_IO_I2C_OFFSET(0);
		else
			devoff = XLP_IO_I2C0_OFFSET(0);
		break;
	case PIC_SATA_IRQ:
		devoff = XLP_IO_SATA_OFFSET(0);
		break;
	case PIC_GPIO_IRQ:
		devoff = XLP_IO_GPIO_OFFSET(0);
		break;
	case PIC_NAND_IRQ:
		devoff = XLP_IO_NAND_OFFSET(0);
		break;
	case PIC_SPI_IRQ:
		devoff = XLP_IO_SPI_OFFSET(0);
		break;
	default:
		if (cpu_is_xlpii()) {
			switch (irq) {
				/* XLP2XX has three XHCI USB controller */
			case PIC_2XX_XHCI_0_IRQ:
				devoff = XLP2XX_IO_USB_XHCI0_OFFSET(0);
				break;
			case PIC_2XX_XHCI_1_IRQ:
				devoff = XLP2XX_IO_USB_XHCI1_OFFSET(0);
				break;
			case PIC_2XX_XHCI_2_IRQ:
				devoff = XLP2XX_IO_USB_XHCI2_OFFSET(0);
				break;
			}
		} else {
			switch (irq) {
			case PIC_EHCI_0_IRQ:
				devoff = XLP_IO_USB_EHCI0_OFFSET(0);
				break;
			case PIC_EHCI_1_IRQ:
				devoff = XLP_IO_USB_EHCI1_OFFSET(0);
				break;
			case PIC_OHCI_0_IRQ:
				devoff = XLP_IO_USB_OHCI0_OFFSET(0);
				break;
			case PIC_OHCI_1_IRQ:
				devoff = XLP_IO_USB_OHCI1_OFFSET(0);
				break;
			case PIC_OHCI_2_IRQ:
				devoff = XLP_IO_USB_OHCI2_OFFSET(0);
				break;
			case PIC_OHCI_3_IRQ:
				devoff = XLP_IO_USB_OHCI3_OFFSET(0);
				break;
			}
		}
	}

	if (devoff != 0) {
		uint32_t val;

		pcibase = nlm_pcicfg_base(devoff);
		val = nlm_read_reg(pcibase, XLP_PCI_IRTINFO_REG);
		if (val == 0xffffffff) {
			irt = -1;
		} else {
			irt = val & 0xffff;
			/* HW weirdness, I2C IRT entry has to be fixed up */
			switch (irq) {
			case PIC_I2C_1_IRQ:
				irt = irt + 1; break;
			case PIC_I2C_2_IRQ:
				irt = irt + 2; break;
			case PIC_I2C_3_IRQ:
				irt = irt + 3; break;
			}
		}
	} else if (irq >= PIC_PCIE_LINK_LEGACY_IRQ(0) &&
			irq <= PIC_PCIE_LINK_LEGACY_IRQ(3)) {
		/* HW bug, PCI IRT entries are bad on early silicon, fix */
		irt = PIC_IRT_PCIE_LINK_INDEX(irq -
					PIC_PCIE_LINK_LEGACY_IRQ_BASE);
	} else {
		irt = -1;
	}
	return irt;
}

int nlm_irq_to_irt(int irq)
{
	/* return -2 for irqs without 1-1 mapping */
	if (irq >= PIC_PCIE_LINK_MSI_IRQ(0) && irq <= PIC_PCIE_LINK_MSI_IRQ(3))
		return -2;
	if (irq >= PIC_PCIE_MSIX_IRQ(0) && irq <= PIC_PCIE_MSIX_IRQ(3))
		return -2;

	if (cpu_is_xlp9xx())
		return xlp9xx_irq_to_irt(irq);
	else
		return xlp_irq_to_irt(irq);
}

static unsigned int nlm_xlp2_get_core_frequency(int node, int core)
{
	unsigned int pll_post_div, ctrl_val0, ctrl_val1, denom;
	uint64_t num, sysbase, clockbase;

	if (cpu_is_xlp9xx()) {
		clockbase = nlm_get_clock_regbase(node);
		ctrl_val0 = nlm_read_sys_reg(clockbase,
					SYS_9XX_CPU_PLL_CTRL0(core));
		ctrl_val1 = nlm_read_sys_reg(clockbase,
					SYS_9XX_CPU_PLL_CTRL1(core));
	} else {
		sysbase = nlm_get_node(node)->sysbase;
		ctrl_val0 = nlm_read_sys_reg(sysbase,
						SYS_CPU_PLL_CTRL0(core));
		ctrl_val1 = nlm_read_sys_reg(sysbase,
						SYS_CPU_PLL_CTRL1(core));
	}

	/* Find PLL post divider value */
	switch ((ctrl_val0 >> 24) & 0x7) {
	case 1:
		pll_post_div = 2;
		break;
	case 3:
		pll_post_div = 4;
		break;
	case 7:
		pll_post_div = 8;
		break;
	case 6:
		pll_post_div = 16;
		break;
	case 0:
	default:
		pll_post_div = 1;
		break;
	}

	num = 1000000ULL * (400 * 3 + 100 * (ctrl_val1 & 0x3f));
	denom = 3 * pll_post_div;
	do_div(num, denom);

	return (unsigned int)num;
}

static unsigned int nlm_xlp_get_core_frequency(int node, int core)
{
	unsigned int pll_divf, pll_divr, dfs_div, ext_div;
	unsigned int rstval, dfsval, denom;
	uint64_t num, sysbase;

	sysbase = nlm_get_node(node)->sysbase;
	rstval = nlm_read_sys_reg(sysbase, SYS_POWER_ON_RESET_CFG);
	dfsval = nlm_read_sys_reg(sysbase, SYS_CORE_DFS_DIV_VALUE);
	pll_divf = ((rstval >> 10) & 0x7f) + 1;
	pll_divr = ((rstval >> 8)  & 0x3) + 1;
	ext_div  = ((rstval >> 30) & 0x3) + 1;
	dfs_div  = ((dfsval >> (core * 4)) & 0xf) + 1;

	num = 800000000ULL * pll_divf;
	denom = 3 * pll_divr * ext_div * dfs_div;
	do_div(num, denom);

	return (unsigned int)num;
}

unsigned int nlm_get_core_frequency(int node, int core)
{
	if (cpu_is_xlpii())
		return nlm_xlp2_get_core_frequency(node, core);
	else
		return nlm_xlp_get_core_frequency(node, core);
}

/*
 * Calculate PIC frequency from PLL registers.
 * freq_out = (ref_freq/2 * (6 + ctrl2[7:0]) + ctrl2[20:8]/2^13) /
 * 		((2^ctrl0[7:5]) * Table(ctrl0[26:24]))
 */
static unsigned int nlm_xlp2_get_pic_frequency(int node)
{
	u32 ctrl_val0, ctrl_val2, vco_post_div, pll_post_div, cpu_xlp9xx;
	u32 mdiv, fdiv, pll_out_freq_den, reg_select, ref_div, pic_div;
	u64 sysbase, pll_out_freq_num, ref_clk_select, clockbase, ref_clk;

	sysbase = nlm_get_node(node)->sysbase;
	clockbase = nlm_get_clock_regbase(node);
	cpu_xlp9xx = cpu_is_xlp9xx();

	/* Find ref_clk_base */
	if (cpu_xlp9xx)
		ref_clk_select = (nlm_read_sys_reg(sysbase,
				SYS_9XX_POWER_ON_RESET_CFG) >> 18) & 0x3;
	else
		ref_clk_select = (nlm_read_sys_reg(sysbase,
					SYS_POWER_ON_RESET_CFG) >> 18) & 0x3;
	switch (ref_clk_select) {
	case 0:
		ref_clk = 200000000ULL;
		ref_div = 3;
		break;
	case 1:
		ref_clk = 100000000ULL;
		ref_div = 1;
		break;
	case 2:
		ref_clk = 125000000ULL;
		ref_div = 1;
		break;
	case 3:
		ref_clk = 400000000ULL;
		ref_div = 3;
		break;
	}

	/* Find the clock source PLL device for PIC */
	if (cpu_xlp9xx) {
		reg_select = nlm_read_sys_reg(clockbase,
				SYS_9XX_CLK_DEV_SEL_REG) & 0x3;
		switch (reg_select) {
		case 0:
			ctrl_val0 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL0);
			ctrl_val2 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL2);
			break;
		case 1:
			ctrl_val0 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL0_DEVX(0));
			ctrl_val2 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL2_DEVX(0));
			break;
		case 2:
			ctrl_val0 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL0_DEVX(1));
			ctrl_val2 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL2_DEVX(1));
			break;
		case 3:
			ctrl_val0 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL0_DEVX(2));
			ctrl_val2 = nlm_read_sys_reg(clockbase,
					SYS_9XX_PLL_CTRL2_DEVX(2));
			break;
		}
	} else {
		reg_select = (nlm_read_sys_reg(sysbase,
					SYS_CLK_DEV_SEL_REG) >> 22) & 0x3;
		switch (reg_select) {
		case 0:
			ctrl_val0 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL0);
			ctrl_val2 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL2);
			break;
		case 1:
			ctrl_val0 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL0_DEVX(0));
			ctrl_val2 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL2_DEVX(0));
			break;
		case 2:
			ctrl_val0 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL0_DEVX(1));
			ctrl_val2 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL2_DEVX(1));
			break;
		case 3:
			ctrl_val0 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL0_DEVX(2));
			ctrl_val2 = nlm_read_sys_reg(sysbase,
					SYS_PLL_CTRL2_DEVX(2));
			break;
		}
	}

	vco_post_div = (ctrl_val0 >> 5) & 0x7;
	pll_post_div = (ctrl_val0 >> 24) & 0x7;
	mdiv = ctrl_val2 & 0xff;
	fdiv = (ctrl_val2 >> 8) & 0x1fff;

	/* Find PLL post divider value */
	switch (pll_post_div) {
	case 1:
		pll_post_div = 2;
		break;
	case 3:
		pll_post_div = 4;
		break;
	case 7:
		pll_post_div = 8;
		break;
	case 6:
		pll_post_div = 16;
		break;
	case 0:
	default:
		pll_post_div = 1;
		break;
	}

	fdiv = fdiv/(1 << 13);
	pll_out_freq_num = ((ref_clk >> 1) * (6 + mdiv)) + fdiv;
	pll_out_freq_den = (1 << vco_post_div) * pll_post_div * ref_div;

	if (pll_out_freq_den > 0)
		do_div(pll_out_freq_num, pll_out_freq_den);

	/* PIC post divider, which happens after PLL */
	if (cpu_xlp9xx)
		pic_div = nlm_read_sys_reg(clockbase,
				SYS_9XX_CLK_DEV_DIV_REG) & 0x3;
	else
		pic_div = (nlm_read_sys_reg(sysbase,
					SYS_CLK_DEV_DIV_REG) >> 22) & 0x3;
	do_div(pll_out_freq_num, 1 << pic_div);

	return pll_out_freq_num;
}

unsigned int nlm_get_pic_frequency(int node)
{
	if (cpu_is_xlpii())
		return nlm_xlp2_get_pic_frequency(node);
	else
		return 133333333;
}

unsigned int nlm_get_cpu_frequency(void)
{
	return nlm_get_core_frequency(0, 0);
}

/*
 * Fills upto 8 pairs of entries containing the DRAM map of a node
 * if node < 0, get dram map for all nodes
 */
int nlm_get_dram_map(int node, uint64_t *dram_map, int nentries)
{
	uint64_t bridgebase, base, lim;
	uint32_t val;
	unsigned int barreg, limreg, xlatreg;
	int i, n, rv;

	/* Look only at mapping on Node 0, we don't handle crazy configs */
	bridgebase = nlm_get_bridge_regbase(0);
	rv = 0;
	for (i = 0; i < 8; i++) {
		if (rv + 1 >= nentries)
			break;
		if (cpu_is_xlp9xx()) {
			barreg = BRIDGE_9XX_DRAM_BAR(i);
			limreg = BRIDGE_9XX_DRAM_LIMIT(i);
			xlatreg = BRIDGE_9XX_DRAM_NODE_TRANSLN(i);
		} else {
			barreg = BRIDGE_DRAM_BAR(i);
			limreg = BRIDGE_DRAM_LIMIT(i);
			xlatreg = BRIDGE_DRAM_NODE_TRANSLN(i);
		}
		if (node >= 0) {
			/* node specified, get node mapping of BAR */
			val = nlm_read_bridge_reg(bridgebase, xlatreg);
			n = (val >> 1) & 0x3;
			if (n != node)
				continue;
		}
		val = nlm_read_bridge_reg(bridgebase, barreg);
		val = (val >>  12) & 0xfffff;
		base = (uint64_t) val << 20;
		val = nlm_read_bridge_reg(bridgebase, limreg);
		val = (val >>  12) & 0xfffff;
		if (val == 0)   /* BAR not used */
			continue;
		lim = ((uint64_t)val + 1) << 20;
		dram_map[rv] = base;
		dram_map[rv + 1] = lim;
		rv += 2;
	}
	return rv;
}
