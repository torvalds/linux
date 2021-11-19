/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __H2X_ASPEED_H_INCLUDED
#define __H2X_ASPEED_H_INCLUDED

#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/msi.h>
#include "../../pci.h"
#include <linux/reset.h>
#include <linux/irqdomain.h>
//#include <linux/aspeed_pcie_io.h>

#define MAX_MSI_HOST_IRQS		64

struct aspeed_pcie {
	struct device *dev;
	void __iomem *pciereg_base;

	struct regmap *ahbc;
	int irq_pcie;
	u32 rc_offset;
	u32 msi_address;
	int			irq;
	struct reset_control *reset;
	/* INTx */
	struct irq_domain *leg_domain;
	// msi
	struct irq_domain *dev_domain;
	struct irq_domain *msi_domain;
	//h2x info
	void __iomem *h2xreg_base;
	void __iomem *h2x_rc_base;
	int irq_h2x;
	u8 txTag;
	struct reset_control *h2x_reset;
};

extern void aspeed_h2x_intx_ack_irq(struct irq_data *d);
extern void aspeed_h2x_intx_mask_irq(struct irq_data *d);
extern void aspeed_h2x_intx_unmask_irq(struct irq_data *d);
extern void aspeed_h2x_msi_mask_irq(struct irq_data *d);
extern void aspeed_h2x_msi_unmask_irq(struct irq_data *d);

extern void aspeed_h2x_msi_enable(struct aspeed_pcie *pcie);
extern void aspeed_h2x_msi_disable(struct aspeed_pcie *pcie);

extern void aspeed_h2x_rc_intr_handler(struct aspeed_pcie *pcie);

extern int aspeed_h2x_rd_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val);

extern int aspeed_h2x_wr_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val);

extern void aspeed_h2x_set_slot_power_limit(struct aspeed_pcie *pcie);

extern void aspeed_h2x_rc_init(struct aspeed_pcie *pcie);

#endif
