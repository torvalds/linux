/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PCIe host controller driver for Mobiveil PCIe Host controller
 *
 * Copyright (c) 2018 Mobiveil Inc.
 * Copyright 2019 NXP
 *
 * Author: Subrahmanya Lingappa <l.subrahmanya@mobiveil.co.in>
 *	   Hou Zhiqiang <Zhiqiang.Hou@nxp.com>
 */

#ifndef _PCIE_MOBIVEIL_H
#define _PCIE_MOBIVEIL_H

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include "../../pci.h"

/* register offsets and bit positions */

/*
 * translation tables are grouped into windows, each window registers are
 * grouped into blocks of 4 or 16 registers each
 */
#define PAB_REG_BLOCK_SIZE		16
#define PAB_EXT_REG_BLOCK_SIZE		4

#define PAB_REG_ADDR(offset, win)	\
	(offset + (win * PAB_REG_BLOCK_SIZE))
#define PAB_EXT_REG_ADDR(offset, win)	\
	(offset + (win * PAB_EXT_REG_BLOCK_SIZE))

#define LTSSM_STATUS			0x0404
#define  LTSSM_STATUS_L0_MASK		0x3f
#define  LTSSM_STATUS_L0		0x2d

#define PAB_CTRL			0x0808
#define  AMBA_PIO_ENABLE_SHIFT		0
#define  PEX_PIO_ENABLE_SHIFT		1
#define  PAGE_SEL_SHIFT			13
#define  PAGE_SEL_MASK			0x3f
#define  PAGE_LO_MASK			0x3ff
#define  PAGE_SEL_OFFSET_SHIFT		10

#define PAB_ACTIVITY_STAT		0x81c

#define PAB_AXI_PIO_CTRL		0x0840
#define  APIO_EN_MASK			0xf

#define PAB_PEX_PIO_CTRL		0x08c0
#define  PIO_ENABLE_SHIFT		0

#define PAB_INTP_AMBA_MISC_ENB		0x0b0c
#define PAB_INTP_AMBA_MISC_STAT		0x0b1c
#define  PAB_INTP_RESET			BIT(1)
#define  PAB_INTP_MSI			BIT(3)
#define  PAB_INTP_INTA			BIT(5)
#define  PAB_INTP_INTB			BIT(6)
#define  PAB_INTP_INTC			BIT(7)
#define  PAB_INTP_INTD			BIT(8)
#define  PAB_INTP_PCIE_UE		BIT(9)
#define  PAB_INTP_IE_PMREDI		BIT(29)
#define  PAB_INTP_IE_EC			BIT(30)
#define  PAB_INTP_MSI_MASK		PAB_INTP_MSI
#define  PAB_INTP_INTX_MASK		(PAB_INTP_INTA | PAB_INTP_INTB |\
					PAB_INTP_INTC | PAB_INTP_INTD)

#define PAB_AXI_AMAP_CTRL(win)		PAB_REG_ADDR(0x0ba0, win)
#define  WIN_ENABLE_SHIFT		0
#define  WIN_TYPE_SHIFT			1
#define  WIN_TYPE_MASK			0x3
#define  WIN_SIZE_MASK			0xfffffc00

#define PAB_EXT_AXI_AMAP_SIZE(win)	PAB_EXT_REG_ADDR(0xbaf0, win)

#define PAB_EXT_AXI_AMAP_AXI_WIN(win)	PAB_EXT_REG_ADDR(0x80a0, win)
#define PAB_AXI_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x0ba4, win)
#define  AXI_WINDOW_ALIGN_MASK		3

#define PAB_AXI_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x0ba8, win)
#define  PAB_BUS_SHIFT			24
#define  PAB_DEVICE_SHIFT		19
#define  PAB_FUNCTION_SHIFT		16

#define PAB_AXI_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x0bac, win)
#define PAB_INTP_AXI_PIO_CLASS		0x474

#define PAB_PEX_AMAP_CTRL(win)		PAB_REG_ADDR(0x4ba0, win)
#define  AMAP_CTRL_EN_SHIFT		0
#define  AMAP_CTRL_TYPE_SHIFT		1
#define  AMAP_CTRL_TYPE_MASK		3

#define PAB_EXT_PEX_AMAP_SIZEN(win)	PAB_EXT_REG_ADDR(0xbef0, win)
#define PAB_EXT_PEX_AMAP_AXI_WIN(win)	PAB_EXT_REG_ADDR(0xb4a0, win)
#define PAB_PEX_AMAP_AXI_WIN(win)	PAB_REG_ADDR(0x4ba4, win)
#define PAB_PEX_AMAP_PEX_WIN_L(win)	PAB_REG_ADDR(0x4ba8, win)
#define PAB_PEX_AMAP_PEX_WIN_H(win)	PAB_REG_ADDR(0x4bac, win)

/* starting offset of INTX bits in status register */
#define PAB_INTX_START			5

/* supported number of MSI interrupts */
#define PCI_NUM_MSI			16

/* MSI registers */
#define MSI_BASE_LO_OFFSET		0x04
#define MSI_BASE_HI_OFFSET		0x08
#define MSI_SIZE_OFFSET			0x0c
#define MSI_ENABLE_OFFSET		0x14
#define MSI_STATUS_OFFSET		0x18
#define MSI_DATA_OFFSET			0x20
#define MSI_ADDR_L_OFFSET		0x24
#define MSI_ADDR_H_OFFSET		0x28

/* outbound and inbound window definitions */
#define WIN_NUM_0			0
#define WIN_NUM_1			1
#define CFG_WINDOW_TYPE			0
#define IO_WINDOW_TYPE			1
#define MEM_WINDOW_TYPE			2
#define IB_WIN_SIZE			((u64)256 * 1024 * 1024 * 1024)
#define MAX_PIO_WINDOWS			8

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_MIN			90000
#define LINK_WAIT_MAX			100000

#define PAGED_ADDR_BNDRY		0xc00
#define OFFSET_TO_PAGE_ADDR(off)	\
	((off & PAGE_LO_MASK) | PAGED_ADDR_BNDRY)
#define OFFSET_TO_PAGE_IDX(off)		\
	((off >> PAGE_SEL_OFFSET_SHIFT) & PAGE_SEL_MASK)

struct mobiveil_msi {			/* MSI information */
	struct mutex lock;		/* protect bitmap variable */
	struct irq_domain *msi_domain;
	struct irq_domain *dev_domain;
	phys_addr_t msi_pages_phys;
	int num_of_vectors;
	DECLARE_BITMAP(msi_irq_in_use, PCI_NUM_MSI);
};

struct mobiveil_pcie;

struct mobiveil_rp_ops {
	int (*interrupt_init)(struct mobiveil_pcie *pcie);
};

struct mobiveil_root_port {
	void __iomem *config_axi_slave_base;	/* endpoint config base */
	struct resource *ob_io_res;
	struct mobiveil_rp_ops *ops;
	int irq;
	raw_spinlock_t intx_mask_lock;
	struct irq_domain *intx_domain;
	struct mobiveil_msi msi;
	struct pci_host_bridge *bridge;
};

struct mobiveil_pab_ops {
	int (*link_up)(struct mobiveil_pcie *pcie);
};

struct mobiveil_pcie {
	struct platform_device *pdev;
	void __iomem *csr_axi_slave_base;	/* root port config base */
	void __iomem *apb_csr_base;	/* MSI register base */
	phys_addr_t pcie_reg_base;	/* Physical PCIe Controller Base */
	int apio_wins;
	int ppio_wins;
	int ob_wins_configured;		/* configured outbound windows */
	int ib_wins_configured;		/* configured inbound windows */
	const struct mobiveil_pab_ops *ops;
	struct mobiveil_root_port rp;
};

int mobiveil_pcie_host_probe(struct mobiveil_pcie *pcie);
int mobiveil_host_init(struct mobiveil_pcie *pcie, bool reinit);
bool mobiveil_pcie_link_up(struct mobiveil_pcie *pcie);
int mobiveil_bringup_link(struct mobiveil_pcie *pcie);
void program_ob_windows(struct mobiveil_pcie *pcie, int win_num, u64 cpu_addr,
			u64 pci_addr, u32 type, u64 size);
void program_ib_windows(struct mobiveil_pcie *pcie, int win_num, u64 cpu_addr,
			u64 pci_addr, u32 type, u64 size);
u32 mobiveil_csr_read(struct mobiveil_pcie *pcie, u32 off, size_t size);
void mobiveil_csr_write(struct mobiveil_pcie *pcie, u32 val, u32 off,
			size_t size);

static inline u32 mobiveil_csr_readl(struct mobiveil_pcie *pcie, u32 off)
{
	return mobiveil_csr_read(pcie, off, 0x4);
}

static inline u16 mobiveil_csr_readw(struct mobiveil_pcie *pcie, u32 off)
{
	return mobiveil_csr_read(pcie, off, 0x2);
}

static inline u8 mobiveil_csr_readb(struct mobiveil_pcie *pcie, u32 off)
{
	return mobiveil_csr_read(pcie, off, 0x1);
}


static inline void mobiveil_csr_writel(struct mobiveil_pcie *pcie, u32 val,
				       u32 off)
{
	mobiveil_csr_write(pcie, val, off, 0x4);
}

static inline void mobiveil_csr_writew(struct mobiveil_pcie *pcie, u16 val,
				       u32 off)
{
	mobiveil_csr_write(pcie, val, off, 0x2);
}

static inline void mobiveil_csr_writeb(struct mobiveil_pcie *pcie, u8 val,
				       u32 off)
{
	mobiveil_csr_write(pcie, val, off, 0x1);
}

#endif /* _PCIE_MOBIVEIL_H */
