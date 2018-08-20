/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Keystone PCI Controller's common includes
 *
 * Copyright (C) 2013-2014 Texas Instruments., Ltd.
 *		http://www.ti.com
 *
 * Author: Murali Karicheri <m-karicheri2@ti.com>
 */

#define MAX_MSI_HOST_IRQS		8

struct keystone_pcie {
	struct dw_pcie		*pci;
	struct	clk		*clk;
	/* PCI Device ID */
	u32			device_id;
	int			num_legacy_host_irqs;
	int			legacy_host_irqs[PCI_NUM_INTX];
	struct			device_node *legacy_intc_np;

	int			num_msi_host_irqs;
	int			msi_host_irqs[MAX_MSI_HOST_IRQS];
	struct			device_node *msi_intc_np;
	struct irq_domain	*legacy_irq_domain;
	struct device_node	*np;

	int error_irq;

	/* Application register space */
	void __iomem		*va_app_base;	/* DT 1st resource */
	struct resource		app;
};

/* Keystone DW specific MSI controller APIs/definitions */
void ks_dw_pcie_handle_msi_irq(struct keystone_pcie *ks_pcie, int offset);
phys_addr_t ks_dw_pcie_get_msi_addr(struct pcie_port *pp);

/* Keystone specific PCI controller APIs */
void ks_dw_pcie_enable_legacy_irqs(struct keystone_pcie *ks_pcie);
void ks_dw_pcie_handle_legacy_irq(struct keystone_pcie *ks_pcie, int offset);
void ks_dw_pcie_enable_error_irq(struct keystone_pcie *ks_pcie);
irqreturn_t ks_dw_pcie_handle_error_irq(struct keystone_pcie *ks_pcie);
int  ks_dw_pcie_host_init(struct keystone_pcie *ks_pcie,
			struct device_node *msi_intc_np);
int ks_dw_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 val);
int ks_dw_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		unsigned int devfn, int where, int size, u32 *val);
void ks_dw_pcie_setup_rc_app_regs(struct keystone_pcie *ks_pcie);
void ks_dw_pcie_initiate_link_train(struct keystone_pcie *ks_pcie);
void ks_dw_pcie_msi_irq_ack(int i, struct pcie_port *pp);
void ks_dw_pcie_msi_set_irq(struct pcie_port *pp, int irq);
void ks_dw_pcie_msi_clear_irq(struct pcie_port *pp, int irq);
void ks_dw_pcie_v3_65_scan_bus(struct pcie_port *pp);
int ks_dw_pcie_msi_host_init(struct pcie_port *pp);
int ks_dw_pcie_link_up(struct dw_pcie *pci);
