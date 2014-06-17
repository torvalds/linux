/*
 *  Atheros AR71XX/AR724X specific PCI setup code
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/resource.h>
#include <linux/platform_device.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/irq.h>
#include "pci.h"

static int (*ath79_pci_plat_dev_init)(struct pci_dev *dev);
static const struct ath79_pci_irq *ath79_pci_irq_map __initdata;
static unsigned ath79_pci_nr_irqs __initdata;

static const struct ath79_pci_irq ar71xx_pci_irq_map[] __initconst = {
	{
		.slot	= 17,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(0),
	}, {
		.slot	= 18,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(1),
	}, {
		.slot	= 19,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(2),
	}
};

static const struct ath79_pci_irq ar724x_pci_irq_map[] __initconst = {
	{
		.slot	= 0,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(0),
	}
};

static const struct ath79_pci_irq qca955x_pci_irq_map[] __initconst = {
	{
		.bus	= 0,
		.slot	= 0,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(0),
	},
	{
		.bus	= 1,
		.slot	= 0,
		.pin	= 1,
		.irq	= ATH79_PCI_IRQ(1),
	},
};

int __init pcibios_map_irq(const struct pci_dev *dev, uint8_t slot, uint8_t pin)
{
	int irq = -1;
	int i;

	if (ath79_pci_nr_irqs == 0 ||
	    ath79_pci_irq_map == NULL) {
		if (soc_is_ar71xx()) {
			ath79_pci_irq_map = ar71xx_pci_irq_map;
			ath79_pci_nr_irqs = ARRAY_SIZE(ar71xx_pci_irq_map);
		} else if (soc_is_ar724x() ||
			   soc_is_ar9342() ||
			   soc_is_ar9344()) {
			ath79_pci_irq_map = ar724x_pci_irq_map;
			ath79_pci_nr_irqs = ARRAY_SIZE(ar724x_pci_irq_map);
		} else if (soc_is_qca955x()) {
			ath79_pci_irq_map = qca955x_pci_irq_map;
			ath79_pci_nr_irqs = ARRAY_SIZE(qca955x_pci_irq_map);
		} else {
			pr_crit("pci %s: invalid irq map\n",
				pci_name((struct pci_dev *) dev));
			return irq;
		}
	}

	for (i = 0; i < ath79_pci_nr_irqs; i++) {
		const struct ath79_pci_irq *entry;

		entry = &ath79_pci_irq_map[i];
		if (entry->bus == dev->bus->number &&
		    entry->slot == slot &&
		    entry->pin == pin) {
			irq = entry->irq;
			break;
		}
	}

	if (irq < 0)
		pr_crit("pci %s: no irq found for pin %u\n",
			pci_name((struct pci_dev *) dev), pin);
	else
		pr_info("pci %s: using irq %d for pin %u\n",
			pci_name((struct pci_dev *) dev), irq, pin);

	return irq;
}

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	if (ath79_pci_plat_dev_init)
		return ath79_pci_plat_dev_init(dev);

	return 0;
}

void __init ath79_pci_set_irq_map(unsigned nr_irqs,
				  const struct ath79_pci_irq *map)
{
	ath79_pci_nr_irqs = nr_irqs;
	ath79_pci_irq_map = map;
}

void __init ath79_pci_set_plat_dev_init(int (*func)(struct pci_dev *dev))
{
	ath79_pci_plat_dev_init = func;
}

static struct platform_device *
ath79_register_pci_ar71xx(void)
{
	struct platform_device *pdev;
	struct resource res[4];

	memset(res, 0, sizeof(res));

	res[0].name = "cfg_base";
	res[0].flags = IORESOURCE_MEM;
	res[0].start = AR71XX_PCI_CFG_BASE;
	res[0].end = AR71XX_PCI_CFG_BASE + AR71XX_PCI_CFG_SIZE - 1;

	res[1].flags = IORESOURCE_IRQ;
	res[1].start = ATH79_CPU_IRQ(2);
	res[1].end = ATH79_CPU_IRQ(2);

	res[2].name = "io_base";
	res[2].flags = IORESOURCE_IO;
	res[2].start = 0;
	res[2].end = 0;

	res[3].name = "mem_base";
	res[3].flags = IORESOURCE_MEM;
	res[3].start = AR71XX_PCI_MEM_BASE;
	res[3].end = AR71XX_PCI_MEM_BASE + AR71XX_PCI_MEM_SIZE - 1;

	pdev = platform_device_register_simple("ar71xx-pci", -1,
					       res, ARRAY_SIZE(res));
	return pdev;
}

static struct platform_device *
ath79_register_pci_ar724x(int id,
			  unsigned long cfg_base,
			  unsigned long ctrl_base,
			  unsigned long crp_base,
			  unsigned long mem_base,
			  unsigned long mem_size,
			  unsigned long io_base,
			  int irq)
{
	struct platform_device *pdev;
	struct resource res[6];

	memset(res, 0, sizeof(res));

	res[0].name = "cfg_base";
	res[0].flags = IORESOURCE_MEM;
	res[0].start = cfg_base;
	res[0].end = cfg_base + AR724X_PCI_CFG_SIZE - 1;

	res[1].name = "ctrl_base";
	res[1].flags = IORESOURCE_MEM;
	res[1].start = ctrl_base;
	res[1].end = ctrl_base + AR724X_PCI_CTRL_SIZE - 1;

	res[2].flags = IORESOURCE_IRQ;
	res[2].start = irq;
	res[2].end = irq;

	res[3].name = "mem_base";
	res[3].flags = IORESOURCE_MEM;
	res[3].start = mem_base;
	res[3].end = mem_base + mem_size - 1;

	res[4].name = "io_base";
	res[4].flags = IORESOURCE_IO;
	res[4].start = io_base;
	res[4].end = io_base;

	res[5].name = "crp_base";
	res[5].flags = IORESOURCE_MEM;
	res[5].start = crp_base;
	res[5].end = crp_base + AR724X_PCI_CRP_SIZE - 1;

	pdev = platform_device_register_simple("ar724x-pci", id,
					       res, ARRAY_SIZE(res));
	return pdev;
}

int __init ath79_register_pci(void)
{
	struct platform_device *pdev = NULL;

	if (soc_is_ar71xx()) {
		pdev = ath79_register_pci_ar71xx();
	} else if (soc_is_ar724x()) {
		pdev = ath79_register_pci_ar724x(-1,
						 AR724X_PCI_CFG_BASE,
						 AR724X_PCI_CTRL_BASE,
						 AR724X_PCI_CRP_BASE,
						 AR724X_PCI_MEM_BASE,
						 AR724X_PCI_MEM_SIZE,
						 0,
						 ATH79_CPU_IRQ(2));
	} else if (soc_is_ar9342() ||
		   soc_is_ar9344()) {
		u32 bootstrap;

		bootstrap = ath79_reset_rr(AR934X_RESET_REG_BOOTSTRAP);
		if ((bootstrap & AR934X_BOOTSTRAP_PCIE_RC) == 0)
			return -ENODEV;

		pdev = ath79_register_pci_ar724x(-1,
						 AR724X_PCI_CFG_BASE,
						 AR724X_PCI_CTRL_BASE,
						 AR724X_PCI_CRP_BASE,
						 AR724X_PCI_MEM_BASE,
						 AR724X_PCI_MEM_SIZE,
						 0,
						 ATH79_IP2_IRQ(0));
	} else if (soc_is_qca9558()) {
		pdev = ath79_register_pci_ar724x(0,
						 QCA955X_PCI_CFG_BASE0,
						 QCA955X_PCI_CTRL_BASE0,
						 QCA955X_PCI_CRP_BASE0,
						 QCA955X_PCI_MEM_BASE0,
						 QCA955X_PCI_MEM_SIZE,
						 0,
						 ATH79_IP2_IRQ(0));

		pdev = ath79_register_pci_ar724x(1,
						 QCA955X_PCI_CFG_BASE1,
						 QCA955X_PCI_CTRL_BASE1,
						 QCA955X_PCI_CRP_BASE1,
						 QCA955X_PCI_MEM_BASE1,
						 QCA955X_PCI_MEM_SIZE,
						 1,
						 ATH79_IP3_IRQ(2));
	} else {
		/* No PCI support */
		return -ENODEV;
	}

	if (!pdev)
		pr_err("unable to register PCI controller device\n");

	return pdev ? 0 : -ENODEV;
}
