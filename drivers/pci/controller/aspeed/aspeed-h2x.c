// SPDX-License-Identifier: GPL-2.0
/*
 * H2X driver for the Aspeed SoC
 *
 */
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/reset.h>
#include <linux/iopoll.h>
#include <asm/io.h>

//#include <linux/aspeed_pcie_io.h>

#include "h2x-ast2600.h"


#ifdef CONFIG_HOTPLUG_PCI
static int hotplug_event;
module_param(hotplug_event, int, 0644);
MODULE_PARM_DESC(hotplug_event, "Using sw flag mechanism for hot-plug events or not");
#endif

/* reg 0x24 */
#define PCIE_TX_IDLE			BIT(31)

#define PCIE_STATUS_OF_TX		GENMASK(25, 24)
#define	PCIE_RC_TX_COMPLETE		0
#define	PCIE_RC_L_TX_COMPLETE	BIT(24)
#define	PCIE_RC_H_TX_COMPLETE	BIT(25)

#define PCIE_TRIGGER_TX			BIT(0)

/* reg 0x80, 0xC0 */
#define PCIE_RX_TAG_MASK		GENMASK(23, 16)
#define PCIE_RX_DMA_EN			BIT(9)
#define PCIE_RX_LINEAR			BIT(8)
#define PCIE_RX_MSI_SEL			BIT(7)
#define PCIE_RX_MSI_EN			BIT(6)
#define PCIE_1M_ADDRESS_EN		BIT(5)
#define PCIE_UNLOCK_RX_BUFF		BIT(4)
#define PCIE_RX_TLP_TAG_MATCH	BIT(3)
#define PCIE_Wait_RX_TLP_CLR	BIT(2)
#define PCIE_RC_RX_ENABLE		BIT(1)
#define PCIE_RC_ENABLE			BIT(0)

/* reg 0x88, 0xC8 : RC ISR */
#define PCIE_RC_CPLCA_ISR		BIT(6)
#define PCIE_RC_CPLUR_ISR		BIT(5)
#define PCIE_RC_RX_DONE_ISR		BIT(4)

#define PCIE_RC_INTD_ISR		BIT(3)
#define PCIE_RC_INTC_ISR		BIT(2)
#define PCIE_RC_INTB_ISR		BIT(1)
#define PCIE_RC_INTA_ISR		BIT(0)

extern int aspeed_h2x_rd_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct aspeed_pcie *pcie = bus->sysdata;
	u32 timeout = 0;
	u32 bdf_offset;
	int rx_done_fail = 0;
	u32 type = 0;

	//H2X80[4] (unlock) is write-only.
	//Driver may set H2X80[4]=1 before triggering next TX config.
	writel(BIT(4) | readl(pcie->h2x_rc_base), pcie->h2x_rc_base);

	if (bus->number)
		type = 1;
	else
		type = 0;

	bdf_offset = (bus->number << 24) |
					(PCI_SLOT(devfn) << 19) |
					(PCI_FUNC(devfn) << 16) |
					(where & ~3);

	pcie->txTag %= 0x7;

	writel(0x04000001 | (type << 24), pcie->h2xreg_base + 0x10);
	writel(0x0000200f | (pcie->txTag << 8), pcie->h2xreg_base + 0x14);
	writel(bdf_offset, pcie->h2xreg_base + 0x18);
	writel(0x00000000, pcie->h2xreg_base + 0x1c);

	//trigger tx
	writel((readl(pcie->h2xreg_base + 0x24) & 0xf) | PCIE_TRIGGER_TX, pcie->h2xreg_base + 0x24);

	//wait tx idle
	while (!(readl(pcie->h2xreg_base + 0x24) & PCIE_TX_IDLE)) {
		timeout++;
		if (timeout > 10000) {
			*val = 0xffffffff;
			goto out;
		}
	};

	//write clr tx idle
	writel(1, pcie->h2xreg_base + 0x08);

	timeout = 0;
	//check tx status
	switch (readl(pcie->h2xreg_base + 0x24) & PCIE_STATUS_OF_TX) {
	case PCIE_RC_L_TX_COMPLETE:
		while (!(readl(pcie->h2xreg_base + 0x88) & PCIE_RC_RX_DONE_ISR)) {
			timeout++;
			if (timeout > 10) {
				rx_done_fail = 1;
				*val = 0xffffffff;
				break;
			}
			mdelay(1);
		}
		if (!rx_done_fail) {
			if (readl(pcie->h2xreg_base + 0x94) & BIT(13))
				*val = 0xffffffff;
			else
				*val = readl(pcie->h2xreg_base + 0x8C);
		}
		writel(BIT(4) | readl(pcie->h2xreg_base + 0x80), pcie->h2xreg_base + 0x80);
		writel(readl(pcie->h2xreg_base + 0x88), pcie->h2xreg_base + 0x88);
		break;
	case PCIE_RC_H_TX_COMPLETE:
		while (!(readl(pcie->h2xreg_base + 0xC8) & PCIE_RC_RX_DONE_ISR)) {
			timeout++;
			if (timeout > 10) {
				rx_done_fail = 1;
				*val = 0xffffffff;
				break;
			}
			mdelay(1);
		}
		if (!rx_done_fail) {
			if (readl(pcie->h2xreg_base + 0xD4) & BIT(13))
				*val = 0xffffffff;
			else
				*val = readl(pcie->h2xreg_base + 0xCC);
		}
		writel(BIT(4) | readl(pcie->h2xreg_base + 0xC0), pcie->h2xreg_base + 0xC0);
		writel(readl(pcie->h2xreg_base + 0xC8), pcie->h2xreg_base + 0xC8);
		break;
	default:	//read rc data
		*val = readl(pcie->h2xreg_base + 0x0C);
		break;
	}

	switch (size) {
	case 1:
		*val = (*val >> ((where & 3) * 8)) & 0xff;
		break;
	case 2:
		*val = (*val >> ((where & 2) * 8)) & 0xffff;
		break;
	}

#ifdef CONFIG_HOTPLUG_PCI
	if ((where == 0x9a) && (bus->number == 0x0) &&
		(PCI_SLOT(devfn) == 0x8) && (PCI_FUNC(devfn) == 0x0) &&
		hotplug_event)
		*val |= PCI_EXP_SLTSTA_ABP;
#endif
out:
	pcie->txTag++;
	return PCIBIOS_SUCCESSFUL;

}

extern int
aspeed_h2x_wr_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 val)
{
	u32 timeout = 0;
	u32 type = 0;
	u32 shift = 8 * (where & 3);
	u32 bdf_offset;
	u8 byte_en = 0;
	struct aspeed_pcie *pcie = bus->sysdata;

#ifdef CONFIG_HOTPLUG_PCI
	if ((where == 0x9a) && (bus->number == 0x0) &&
		(PCI_SLOT(devfn) == 0x8) && (PCI_FUNC(devfn) == 0x0) &&
		hotplug_event && (val & PCI_EXP_SLTSTA_ABP)) {
		hotplug_event = 0;
		return PCIBIOS_SUCCESSFUL;
	}
#endif
	writel(BIT(4) | readl(pcie->h2x_rc_base), pcie->h2x_rc_base);

	switch (size) {
	case 1:
		switch (where % 4) {
		case 0:
			byte_en = 0x1;
			break;
		case 1:
			byte_en = 0x2;
			break;
		case 2:
			byte_en = 0x4;
			break;
		case 3:
			byte_en = 0x8;
			break;
		}
		val = (val & 0xff) << shift;
		break;
	case 2:
		switch ((where >> 1) % 2) {
		case 0:
			byte_en = 0x3;
			break;
		case 1:
			byte_en = 0xc;
			break;
		}
		val = (val & 0xffff) << shift;
		break;
	default:
		byte_en = 0xf;
		break;
	}

	if (bus->number)
		type = 1;
	else
		type = 0;

	bdf_offset = (bus->number << 24) | (PCI_SLOT(devfn) << 19) |
					(PCI_FUNC(devfn) << 16) | (where & ~3);

	pcie->txTag %= 0x7;

	writel(0x44000001 | (type << 24), pcie->h2xreg_base + 0x10);
	writel(0x00002000 | (pcie->txTag << 8) | byte_en, pcie->h2xreg_base + 0x14);
	writel(bdf_offset, pcie->h2xreg_base + 0x18);
	writel(0x00000000, pcie->h2xreg_base + 0x1C);

	writel(val, pcie->h2xreg_base + 0x20);

	//trigger tx
	writel((readl(pcie->h2xreg_base + 0x24) & 0xf) | PCIE_TRIGGER_TX, pcie->h2xreg_base + 0x24);

//wait tx idle
	while (!(readl(pcie->h2xreg_base + 0x24) & BIT(31))) {
		timeout++;
		if (timeout > 10000)
			goto out;

	};

	//write clr tx idle
	writel(1, pcie->h2xreg_base + 0x08);

	timeout = 0;
	//check tx status and clr rx done int
	switch (readl(pcie->h2xreg_base + 0x24) & PCIE_STATUS_OF_TX) {
	case PCIE_RC_L_TX_COMPLETE:
		while (!(readl(pcie->h2xreg_base + 0x88) & PCIE_RC_RX_DONE_ISR)) {
			timeout++;
			if (timeout > 10)
				break;

			mdelay(1);
		}
		writel(PCIE_RC_RX_DONE_ISR, pcie->h2xreg_base + 0x88);

		break;
	case PCIE_RC_H_TX_COMPLETE:
		while (!(readl(pcie->h2xreg_base + 0xC8) & PCIE_RC_RX_DONE_ISR)) {
			timeout++;
			if (timeout > 10)
				break;
			mdelay(1);
		}
		writel(PCIE_RC_RX_DONE_ISR, pcie->h2xreg_base + 0xC8);
		break;
	}

out:
	pcie->txTag++;
	return PCIBIOS_SUCCESSFUL;

}

/* INTx Functions */
extern void aspeed_h2x_intx_ack_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	struct aspeed_pcie *pcie = irq_desc_get_chip_data(desc);

	writel(readl(pcie->h2x_rc_base + 0x04) | BIT(d->hwirq), pcie->h2x_rc_base + 0x04);
}

extern void aspeed_h2x_intx_mask_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	struct aspeed_pcie *pcie = irq_desc_get_chip_data(desc);

	writel(readl(pcie->h2x_rc_base + 0x04) & ~BIT(d->hwirq), pcie->h2x_rc_base + 0x04);
}

extern void aspeed_h2x_intx_unmask_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	struct aspeed_pcie *pcie = irq_desc_get_chip_data(desc);

	//Enable IRQ ..
	writel(readl(pcie->h2x_rc_base + 0x04) | BIT(d->hwirq), pcie->h2x_rc_base + 0x04);
}
#ifdef CONFIG_PCI_MSI
/* msi Functions */
extern void aspeed_h2x_msi_mask_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	struct aspeed_pcie *pcie = irq_desc_get_chip_data(desc);

	if (d->hwirq > 31)
		writel(readl(pcie->h2x_rc_base + 0x24) & ~BIT((d->hwirq - 32)), pcie->h2x_rc_base + 0x24);
	else
		writel(readl(pcie->h2x_rc_base + 0x20) & ~BIT(d->hwirq), pcie->h2x_rc_base + 0x20);

	pci_msi_mask_irq(d);
}

extern void aspeed_h2x_msi_unmask_irq(struct irq_data *d)
{
	struct irq_desc *desc = irq_to_desc(d->irq);
	struct aspeed_pcie *pcie = irq_desc_get_chip_data(desc);

	if (d->hwirq > 31)
		writel(readl(pcie->h2x_rc_base + 0x24) | BIT((d->hwirq - 32)), pcie->h2x_rc_base + 0x24);
	else
		writel(readl(pcie->h2x_rc_base + 0x20) | BIT(d->hwirq), pcie->h2x_rc_base + 0x20);

	pci_msi_unmask_irq(d);
}

extern void aspeed_h2x_msi_enable(struct aspeed_pcie *pcie)
{
	writel(0xffffffff, pcie->h2x_rc_base + 0x20);
	writel(0xffffffff, pcie->h2x_rc_base + 0x24);
}

extern void aspeed_h2x_msi_disable(struct aspeed_pcie *pcie)
{
	writel(0x0, pcie->h2x_rc_base + 0x20);
	writel(0x0, pcie->h2x_rc_base + 0x24);
}
#endif
extern void aspeed_h2x_rc_intr_handler(struct aspeed_pcie *pcie)
{
	u32 bit;
	u32 virq;
	unsigned long status;
	int i;
	unsigned long intx = readl(pcie->h2x_rc_base + 0x08) & 0xf;

	//intx isr
	if (intx) {
		for_each_set_bit(bit, &intx, PCI_NUM_INTX) {
			virq = irq_find_mapping(pcie->leg_domain, bit);
			if (virq)
				generic_handle_irq(virq);
			else
				dev_err(pcie->dev, "unexpected Int - X\n");
		}
	}
	//msi isr
	for (i = 0; i < 2; i++) {
		status = readl(pcie->h2x_rc_base + 0x28 + (i * 4));
		writel(status, pcie->h2x_rc_base + 0x28 + (i * 4));
		if (!status)
			continue;

		for_each_set_bit(bit, &status, 32) {
			if (i)
				bit += 32;
			generic_handle_domain_irq(pcie->dev_domain, bit);
		}
	}
}

extern void aspeed_h2x_set_slot_power_limit(struct aspeed_pcie *pcie)
{
	u32 timeout = 0;

	writel(BIT(4) | readl(pcie->h2x_rc_base), pcie->h2x_rc_base);

	writel(0x74000001, pcie->h2xreg_base + 0x10);
	writel(0x00400050, pcie->h2xreg_base + 0x14);
	writel(0x00000000, pcie->h2xreg_base + 0x18);
	writel(0x00000000, pcie->h2xreg_base + 0x1c);

	writel(0x1a, pcie->h2xreg_base + 0x20);

	//trigger tx
	writel(PCIE_TRIGGER_TX, pcie->h2xreg_base + 0x24);

	//wait tx idle
	while (!(readl(pcie->h2xreg_base + 0x24) & BIT(31))) {
		timeout++;
		if (timeout > 1000)
			return;
	};

	//write clr tx idle
	writel(1, pcie->h2xreg_base + 0x08);
	timeout = 0;

	//check tx status and clr rx done int
	while (!(readl(pcie->h2x_rc_base + 0x08) & PCIE_RC_RX_DONE_ISR)) {
		timeout++;
		if (timeout > 10)
			break;
		mdelay(1);
	}
	writel(PCIE_RC_RX_DONE_ISR, pcie->h2x_rc_base + 0x08);

}

extern void aspeed_h2x_rc_init(struct aspeed_pcie *pcie)
{
	//clr intx isr
	writel(0x0, pcie->h2x_rc_base + 0x04);

	//clr msi isr
	writel(0xFFFFFFFF, pcie->h2x_rc_base + 0x28);
	writel(0xFFFFFFFF, pcie->h2x_rc_base + 0x2c);

	//rc_l
	writel(PCIE_RX_DMA_EN | PCIE_RX_LINEAR | PCIE_RX_MSI_SEL | PCIE_RX_MSI_EN |
			PCIE_Wait_RX_TLP_CLR | PCIE_RC_RX_ENABLE | PCIE_RC_ENABLE,
	pcie->h2x_rc_base);
	//assign debug tx tag
	writel(0x28, pcie->h2x_rc_base + 0x3C);
}
