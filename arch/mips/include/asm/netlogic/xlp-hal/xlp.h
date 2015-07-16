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

#ifndef _NLM_HAL_XLP_H
#define _NLM_HAL_XLP_H

#define PIC_UART_0_IRQ			17
#define PIC_UART_1_IRQ			18

#define PIC_PCIE_LINK_LEGACY_IRQ_BASE	19
#define PIC_PCIE_LINK_LEGACY_IRQ(i)	(19 + (i))

#define PIC_EHCI_0_IRQ			23
#define PIC_EHCI_1_IRQ			24
#define PIC_OHCI_0_IRQ			25
#define PIC_OHCI_1_IRQ			26
#define PIC_OHCI_2_IRQ			27
#define PIC_OHCI_3_IRQ			28
#define PIC_2XX_XHCI_0_IRQ		23
#define PIC_2XX_XHCI_1_IRQ		24
#define PIC_2XX_XHCI_2_IRQ		25
#define PIC_9XX_XHCI_0_IRQ		23
#define PIC_9XX_XHCI_1_IRQ		24
#define PIC_9XX_XHCI_2_IRQ		25

#define PIC_MMC_IRQ			29
#define PIC_I2C_0_IRQ			30
#define PIC_I2C_1_IRQ			31
#define PIC_I2C_2_IRQ			32
#define PIC_I2C_3_IRQ			33
#define PIC_SPI_IRQ			34
#define PIC_NAND_IRQ			37
#define PIC_SATA_IRQ			38
#define PIC_GPIO_IRQ			39

#define PIC_PCIE_LINK_MSI_IRQ_BASE	44	/* 44 - 47 MSI IRQ */
#define PIC_PCIE_LINK_MSI_IRQ(i)	(44 + (i))

/* MSI-X with second link-level dispatch */
#define PIC_PCIE_MSIX_IRQ_BASE		48	/* 48 - 51 MSI-X IRQ */
#define PIC_PCIE_MSIX_IRQ(i)		(48 + (i))

/* XLP9xx and XLP8xx has 128 and 32 MSIX vectors respectively */
#define NLM_MSIX_VEC_BASE		96	/* 96 - 223 - MSIX mapped */
#define NLM_MSI_VEC_BASE		224	/* 224 -351 - MSI mapped */

#define NLM_PIC_INDIRECT_VEC_BASE	512
#define NLM_GPIO_VEC_BASE		768

#define PIC_IRQ_BASE			8
#define PIC_IRT_FIRST_IRQ		PIC_IRQ_BASE
#define PIC_IRT_LAST_IRQ		63

#ifndef __ASSEMBLY__

/* SMP support functions */
void xlp_boot_core0_siblings(void);
void xlp_wakeup_secondary_cpus(void);

void xlp_mmu_init(void);
void nlm_hal_init(void);
int nlm_get_dram_map(int node, uint64_t *dram_map, int nentries);

struct pci_dev;
int xlp_socdev_to_node(const struct pci_dev *dev);

/* Device tree related */
void xlp_early_init_devtree(void);
void *xlp_dt_init(void *fdtp);

static inline int cpu_is_xlpii(void)
{
	int chip = read_c0_prid() & PRID_IMP_MASK;

	return chip == PRID_IMP_NETLOGIC_XLP2XX ||
		chip == PRID_IMP_NETLOGIC_XLP9XX ||
		chip == PRID_IMP_NETLOGIC_XLP5XX;
}

static inline int cpu_is_xlp9xx(void)
{
	int chip = read_c0_prid() & PRID_IMP_MASK;

	return chip == PRID_IMP_NETLOGIC_XLP9XX ||
		chip == PRID_IMP_NETLOGIC_XLP5XX;
}
#endif /* !__ASSEMBLY__ */
#endif /* _ASM_NLM_XLP_H */
