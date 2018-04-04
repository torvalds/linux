/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Synopsys DesignWare PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 */

#ifndef _PCIE_DESIGNWARE_H
#define _PCIE_DESIGNWARE_H

#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include <linux/pci.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>

/* Parameters for the waiting for link up routine */
#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_USLEEP_MIN		90000
#define LINK_WAIT_USLEEP_MAX		100000

/* Parameters for the waiting for iATU enabled routine */
#define LINK_WAIT_MAX_IATU_RETRIES	5
#define LINK_WAIT_IATU_MIN		9000
#define LINK_WAIT_IATU_MAX		10000

/* Synopsys-specific PCIe configuration registers */
#define PCIE_PORT_LINK_CONTROL		0x710
#define PORT_LINK_MODE_MASK		(0x3f << 16)
#define PORT_LINK_MODE_1_LANES		(0x1 << 16)
#define PORT_LINK_MODE_2_LANES		(0x3 << 16)
#define PORT_LINK_MODE_4_LANES		(0x7 << 16)
#define PORT_LINK_MODE_8_LANES		(0xf << 16)

#define PCIE_LINK_WIDTH_SPEED_CONTROL	0x80C
#define PORT_LOGIC_SPEED_CHANGE		(0x1 << 17)
#define PORT_LOGIC_LINK_WIDTH_MASK	(0x1f << 8)
#define PORT_LOGIC_LINK_WIDTH_1_LANES	(0x1 << 8)
#define PORT_LOGIC_LINK_WIDTH_2_LANES	(0x2 << 8)
#define PORT_LOGIC_LINK_WIDTH_4_LANES	(0x4 << 8)
#define PORT_LOGIC_LINK_WIDTH_8_LANES	(0x8 << 8)

#define PCIE_MSI_ADDR_LO		0x820
#define PCIE_MSI_ADDR_HI		0x824
#define PCIE_MSI_INTR0_ENABLE		0x828
#define PCIE_MSI_INTR0_MASK		0x82C
#define PCIE_MSI_INTR0_STATUS		0x830

#define PCIE_ATU_VIEWPORT		0x900
#define PCIE_ATU_REGION_INBOUND		(0x1 << 31)
#define PCIE_ATU_REGION_OUTBOUND	(0x0 << 31)
#define PCIE_ATU_REGION_INDEX2		(0x2 << 0)
#define PCIE_ATU_REGION_INDEX1		(0x1 << 0)
#define PCIE_ATU_REGION_INDEX0		(0x0 << 0)
#define PCIE_ATU_CR1			0x904
#define PCIE_ATU_TYPE_MEM		(0x0 << 0)
#define PCIE_ATU_TYPE_IO		(0x2 << 0)
#define PCIE_ATU_TYPE_CFG0		(0x4 << 0)
#define PCIE_ATU_TYPE_CFG1		(0x5 << 0)
#define PCIE_ATU_CR2			0x908
#define PCIE_ATU_ENABLE			(0x1 << 31)
#define PCIE_ATU_BAR_MODE_ENABLE	(0x1 << 30)
#define PCIE_ATU_LOWER_BASE		0x90C
#define PCIE_ATU_UPPER_BASE		0x910
#define PCIE_ATU_LIMIT			0x914
#define PCIE_ATU_LOWER_TARGET		0x918
#define PCIE_ATU_BUS(x)			(((x) & 0xff) << 24)
#define PCIE_ATU_DEV(x)			(((x) & 0x1f) << 19)
#define PCIE_ATU_FUNC(x)		(((x) & 0x7) << 16)
#define PCIE_ATU_UPPER_TARGET		0x91C

#define PCIE_MISC_CONTROL_1_OFF		0x8BC
#define PCIE_DBI_RO_WR_EN		(0x1 << 0)

/*
 * iATU Unroll-specific register definitions
 * From 4.80 core version the address translation will be made by unroll
 */
#define PCIE_ATU_UNR_REGION_CTRL1	0x00
#define PCIE_ATU_UNR_REGION_CTRL2	0x04
#define PCIE_ATU_UNR_LOWER_BASE		0x08
#define PCIE_ATU_UNR_UPPER_BASE		0x0C
#define PCIE_ATU_UNR_LIMIT		0x10
#define PCIE_ATU_UNR_LOWER_TARGET	0x14
#define PCIE_ATU_UNR_UPPER_TARGET	0x18

/* Register address builder */
#define PCIE_GET_ATU_OUTB_UNR_REG_OFFSET(region)	\
			((0x3 << 20) | ((region) << 9))

#define PCIE_GET_ATU_INB_UNR_REG_OFFSET(region)				\
			((0x3 << 20) | ((region) << 9) | (0x1 << 8))

#define MSI_MESSAGE_CONTROL		0x52
#define MSI_CAP_MMC_SHIFT		1
#define MSI_CAP_MMC_MASK		(7 << MSI_CAP_MMC_SHIFT)
#define MSI_CAP_MME_SHIFT		4
#define MSI_CAP_MSI_EN_MASK		0x1
#define MSI_CAP_MME_MASK		(7 << MSI_CAP_MME_SHIFT)
#define MSI_MESSAGE_ADDR_L32		0x54
#define MSI_MESSAGE_ADDR_U32		0x58
#define MSI_MESSAGE_DATA_32		0x58
#define MSI_MESSAGE_DATA_64		0x5C

/*
 * Maximum number of MSI IRQs can be 256 per controller. But keep
 * it 32 as of now. Probably we will never need more than 32. If needed,
 * then increment it in multiple of 32.
 */
#define MAX_MSI_IRQS			32
#define MAX_MSI_CTRLS			(MAX_MSI_IRQS / 32)

/* Maximum number of inbound/outbound iATUs */
#define MAX_IATU_IN			256
#define MAX_IATU_OUT			256

struct pcie_port;
struct dw_pcie;
struct dw_pcie_ep;

enum dw_pcie_region_type {
	DW_PCIE_REGION_UNKNOWN,
	DW_PCIE_REGION_INBOUND,
	DW_PCIE_REGION_OUTBOUND,
};

enum dw_pcie_device_mode {
	DW_PCIE_UNKNOWN_TYPE,
	DW_PCIE_EP_TYPE,
	DW_PCIE_LEG_EP_TYPE,
	DW_PCIE_RC_TYPE,
};

struct dw_pcie_host_ops {
	int (*rd_own_conf)(struct pcie_port *pp, int where, int size, u32 *val);
	int (*wr_own_conf)(struct pcie_port *pp, int where, int size, u32 val);
	int (*rd_other_conf)(struct pcie_port *pp, struct pci_bus *bus,
			     unsigned int devfn, int where, int size, u32 *val);
	int (*wr_other_conf)(struct pcie_port *pp, struct pci_bus *bus,
			     unsigned int devfn, int where, int size, u32 val);
	int (*host_init)(struct pcie_port *pp);
	void (*msi_set_irq)(struct pcie_port *pp, int irq);
	void (*msi_clear_irq)(struct pcie_port *pp, int irq);
	phys_addr_t (*get_msi_addr)(struct pcie_port *pp);
	u32 (*get_msi_data)(struct pcie_port *pp, int pos);
	void (*scan_bus)(struct pcie_port *pp);
	int (*msi_host_init)(struct pcie_port *pp, struct msi_controller *chip);
};

struct pcie_port {
	u8			root_bus_nr;
	u64			cfg0_base;
	void __iomem		*va_cfg0_base;
	u32			cfg0_size;
	u64			cfg1_base;
	void __iomem		*va_cfg1_base;
	u32			cfg1_size;
	resource_size_t		io_base;
	phys_addr_t		io_bus_addr;
	u32			io_size;
	u64			mem_base;
	phys_addr_t		mem_bus_addr;
	u32			mem_size;
	struct resource		*cfg;
	struct resource		*io;
	struct resource		*mem;
	struct resource		*busn;
	int			irq;
	const struct dw_pcie_host_ops *ops;
	int			msi_irq;
	struct irq_domain	*irq_domain;
	dma_addr_t		msi_data;
	DECLARE_BITMAP(msi_irq_in_use, MAX_MSI_IRQS);
};

enum dw_pcie_as_type {
	DW_PCIE_AS_UNKNOWN,
	DW_PCIE_AS_MEM,
	DW_PCIE_AS_IO,
};

struct dw_pcie_ep_ops {
	void	(*ep_init)(struct dw_pcie_ep *ep);
	int	(*raise_irq)(struct dw_pcie_ep *ep, u8 func_no,
			     enum pci_epc_irq_type type, u8 interrupt_num);
};

struct dw_pcie_ep {
	struct pci_epc		*epc;
	struct dw_pcie_ep_ops	*ops;
	phys_addr_t		phys_base;
	size_t			addr_size;
	size_t			page_size;
	u8			bar_to_atu[6];
	phys_addr_t		*outbound_addr;
	unsigned long		*ib_window_map;
	unsigned long		*ob_window_map;
	u32			num_ib_windows;
	u32			num_ob_windows;
	void __iomem		*msi_mem;
	phys_addr_t		msi_mem_phys;
};

struct dw_pcie_ops {
	u64	(*cpu_addr_fixup)(struct dw_pcie *pcie, u64 cpu_addr);
	u32	(*read_dbi)(struct dw_pcie *pcie, void __iomem *base, u32 reg,
			    size_t size);
	void	(*write_dbi)(struct dw_pcie *pcie, void __iomem *base, u32 reg,
			     size_t size, u32 val);
	int	(*link_up)(struct dw_pcie *pcie);
	int	(*start_link)(struct dw_pcie *pcie);
	void	(*stop_link)(struct dw_pcie *pcie);
};

struct dw_pcie {
	struct device		*dev;
	void __iomem		*dbi_base;
	void __iomem		*dbi_base2;
	u32			num_viewport;
	u8			iatu_unroll_enabled;
	struct pcie_port	pp;
	struct dw_pcie_ep	ep;
	const struct dw_pcie_ops *ops;
};

#define to_dw_pcie_from_pp(port) container_of((port), struct dw_pcie, pp)

#define to_dw_pcie_from_ep(endpoint)   \
		container_of((endpoint), struct dw_pcie, ep)

int dw_pcie_read(void __iomem *addr, int size, u32 *val);
int dw_pcie_write(void __iomem *addr, int size, u32 val);

u32 __dw_pcie_read_dbi(struct dw_pcie *pci, void __iomem *base, u32 reg,
		       size_t size);
void __dw_pcie_write_dbi(struct dw_pcie *pci, void __iomem *base, u32 reg,
			 size_t size, u32 val);
int dw_pcie_link_up(struct dw_pcie *pci);
int dw_pcie_wait_for_link(struct dw_pcie *pci);
void dw_pcie_prog_outbound_atu(struct dw_pcie *pci, int index,
			       int type, u64 cpu_addr, u64 pci_addr,
			       u32 size);
int dw_pcie_prog_inbound_atu(struct dw_pcie *pci, int index, int bar,
			     u64 cpu_addr, enum dw_pcie_as_type as_type);
void dw_pcie_disable_atu(struct dw_pcie *pci, int index,
			 enum dw_pcie_region_type type);
void dw_pcie_setup(struct dw_pcie *pci);

static inline void dw_pcie_writel_dbi(struct dw_pcie *pci, u32 reg, u32 val)
{
	__dw_pcie_write_dbi(pci, pci->dbi_base, reg, 0x4, val);
}

static inline u32 dw_pcie_readl_dbi(struct dw_pcie *pci, u32 reg)
{
	return __dw_pcie_read_dbi(pci, pci->dbi_base, reg, 0x4);
}

static inline void dw_pcie_writew_dbi(struct dw_pcie *pci, u32 reg, u16 val)
{
	__dw_pcie_write_dbi(pci, pci->dbi_base, reg, 0x2, val);
}

static inline u16 dw_pcie_readw_dbi(struct dw_pcie *pci, u32 reg)
{
	return __dw_pcie_read_dbi(pci, pci->dbi_base, reg, 0x2);
}

static inline void dw_pcie_writeb_dbi(struct dw_pcie *pci, u32 reg, u8 val)
{
	__dw_pcie_write_dbi(pci, pci->dbi_base, reg, 0x1, val);
}

static inline u8 dw_pcie_readb_dbi(struct dw_pcie *pci, u32 reg)
{
	return __dw_pcie_read_dbi(pci, pci->dbi_base, reg, 0x1);
}

static inline void dw_pcie_writel_dbi2(struct dw_pcie *pci, u32 reg, u32 val)
{
	__dw_pcie_write_dbi(pci, pci->dbi_base2, reg, 0x4, val);
}

static inline u32 dw_pcie_readl_dbi2(struct dw_pcie *pci, u32 reg)
{
	return __dw_pcie_read_dbi(pci, pci->dbi_base2, reg, 0x4);
}

static inline void dw_pcie_dbi_ro_wr_en(struct dw_pcie *pci)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = dw_pcie_readl_dbi(pci, reg);
	val |= PCIE_DBI_RO_WR_EN;
	dw_pcie_writel_dbi(pci, reg, val);
}

static inline void dw_pcie_dbi_ro_wr_dis(struct dw_pcie *pci)
{
	u32 reg;
	u32 val;

	reg = PCIE_MISC_CONTROL_1_OFF;
	val = dw_pcie_readl_dbi(pci, reg);
	val &= ~PCIE_DBI_RO_WR_EN;
	dw_pcie_writel_dbi(pci, reg, val);
}

#ifdef CONFIG_PCIE_DW_HOST
irqreturn_t dw_handle_msi_irq(struct pcie_port *pp);
void dw_pcie_msi_init(struct pcie_port *pp);
void dw_pcie_setup_rc(struct pcie_port *pp);
int dw_pcie_host_init(struct pcie_port *pp);
#else
static inline irqreturn_t dw_handle_msi_irq(struct pcie_port *pp)
{
	return IRQ_NONE;
}

static inline void dw_pcie_msi_init(struct pcie_port *pp)
{
}

static inline void dw_pcie_setup_rc(struct pcie_port *pp)
{
}

static inline int dw_pcie_host_init(struct pcie_port *pp)
{
	return 0;
}
#endif

#ifdef CONFIG_PCIE_DW_EP
void dw_pcie_ep_linkup(struct dw_pcie_ep *ep);
int dw_pcie_ep_init(struct dw_pcie_ep *ep);
void dw_pcie_ep_exit(struct dw_pcie_ep *ep);
int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
			     u8 interrupt_num);
void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar);
#else
static inline void dw_pcie_ep_linkup(struct dw_pcie_ep *ep)
{
}

static inline int dw_pcie_ep_init(struct dw_pcie_ep *ep)
{
	return 0;
}

static inline void dw_pcie_ep_exit(struct dw_pcie_ep *ep)
{
}

static inline int dw_pcie_ep_raise_msi_irq(struct dw_pcie_ep *ep, u8 func_no,
					   u8 interrupt_num)
{
	return 0;
}

static inline void dw_pcie_ep_reset_bar(struct dw_pcie *pci, enum pci_barno bar)
{
}
#endif
#endif /* _PCIE_DESIGNWARE_H */
