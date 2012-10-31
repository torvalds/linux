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
#include <asm/netlogic/xlp-hal/pic.h>
#include <asm/netlogic/xlp-hal/sys.h>

/* Main initialization */
void nlm_node_init(int node)
{
	struct nlm_soc_info *nodep;

	nodep = nlm_get_node(node);
	nodep->sysbase = nlm_get_sys_regbase(node);
	nodep->picbase = nlm_get_pic_regbase(node);
	nodep->ebase = read_c0_ebase() & (~((1 << 12) - 1));
	spin_lock_init(&nodep->piclock);
}

int nlm_irq_to_irt(int irq)
{
	if (!PIC_IRQ_IS_IRT(irq))
		return -1;

	switch (irq) {
	case PIC_UART_0_IRQ:
		return PIC_IRT_UART_0_INDEX;
	case PIC_UART_1_IRQ:
		return PIC_IRT_UART_1_INDEX;
	case PIC_PCIE_LINK_0_IRQ:
	       return PIC_IRT_PCIE_LINK_0_INDEX;
	case PIC_PCIE_LINK_1_IRQ:
	       return PIC_IRT_PCIE_LINK_1_INDEX;
	case PIC_PCIE_LINK_2_IRQ:
	       return PIC_IRT_PCIE_LINK_2_INDEX;
	case PIC_PCIE_LINK_3_IRQ:
	       return PIC_IRT_PCIE_LINK_3_INDEX;
	case PIC_EHCI_0_IRQ:
	       return PIC_IRT_EHCI_0_INDEX;
	case PIC_EHCI_1_IRQ:
	       return PIC_IRT_EHCI_1_INDEX;
	case PIC_OHCI_0_IRQ:
	       return PIC_IRT_OHCI_0_INDEX;
	case PIC_OHCI_1_IRQ:
	       return PIC_IRT_OHCI_1_INDEX;
	case PIC_OHCI_2_IRQ:
	       return PIC_IRT_OHCI_2_INDEX;
	case PIC_OHCI_3_IRQ:
	       return PIC_IRT_OHCI_3_INDEX;
	case PIC_MMC_IRQ:
	       return PIC_IRT_MMC_INDEX;
	case PIC_I2C_0_IRQ:
		return PIC_IRT_I2C_0_INDEX;
	case PIC_I2C_1_IRQ:
		return PIC_IRT_I2C_1_INDEX;
	default:
		return -1;
	}
}

unsigned int nlm_get_core_frequency(int node, int core)
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

unsigned int nlm_get_cpu_frequency(void)
{
	return nlm_get_core_frequency(0, 0);
}
