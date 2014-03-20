/*
 *  Atheros AR71xx PCI host controller driver
 *
 *  Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 *  Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 *
 *  Parts of this file are based on Atheros' 2.6.15 BSP
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/resource.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/pci.h>
#include <linux/pci_regs.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/ath79.h>

#define AR71XX_PCI_REG_CRP_AD_CBE	0x00
#define AR71XX_PCI_REG_CRP_WRDATA	0x04
#define AR71XX_PCI_REG_CRP_RDDATA	0x08
#define AR71XX_PCI_REG_CFG_AD		0x0c
#define AR71XX_PCI_REG_CFG_CBE		0x10
#define AR71XX_PCI_REG_CFG_WRDATA	0x14
#define AR71XX_PCI_REG_CFG_RDDATA	0x18
#define AR71XX_PCI_REG_PCI_ERR		0x1c
#define AR71XX_PCI_REG_PCI_ERR_ADDR	0x20
#define AR71XX_PCI_REG_AHB_ERR		0x24
#define AR71XX_PCI_REG_AHB_ERR_ADDR	0x28

#define AR71XX_PCI_CRP_CMD_WRITE	0x00010000
#define AR71XX_PCI_CRP_CMD_READ		0x00000000
#define AR71XX_PCI_CFG_CMD_READ		0x0000000a
#define AR71XX_PCI_CFG_CMD_WRITE	0x0000000b

#define AR71XX_PCI_INT_CORE		BIT(4)
#define AR71XX_PCI_INT_DEV2		BIT(2)
#define AR71XX_PCI_INT_DEV1		BIT(1)
#define AR71XX_PCI_INT_DEV0		BIT(0)

#define AR71XX_PCI_IRQ_COUNT		5

struct ar71xx_pci_controller {
	void __iomem *cfg_base;
	spinlock_t lock;
	int irq;
	int irq_base;
	struct pci_controller pci_ctrl;
	struct resource io_res;
	struct resource mem_res;
};

/* Byte lane enable bits */
static const u8 ar71xx_pci_ble_table[4][4] = {
	{0x0, 0xf, 0xf, 0xf},
	{0xe, 0xd, 0xb, 0x7},
	{0xc, 0xf, 0x3, 0xf},
	{0xf, 0xf, 0xf, 0xf},
};

static const u32 ar71xx_pci_read_mask[8] = {
	0, 0xff, 0xffff, 0, 0xffffffff, 0, 0, 0
};

static inline u32 ar71xx_pci_get_ble(int where, int size, int local)
{
	u32 t;

	t = ar71xx_pci_ble_table[size & 3][where & 3];
	BUG_ON(t == 0xf);
	t <<= (local) ? 20 : 4;

	return t;
}

static inline u32 ar71xx_pci_bus_addr(struct pci_bus *bus, unsigned int devfn,
				      int where)
{
	u32 ret;

	if (!bus->number) {
		/* type 0 */
		ret = (1 << PCI_SLOT(devfn)) | (PCI_FUNC(devfn) << 8) |
		      (where & ~3);
	} else {
		/* type 1 */
		ret = (bus->number << 16) | (PCI_SLOT(devfn) << 11) |
		      (PCI_FUNC(devfn) << 8) | (where & ~3) | 1;
	}

	return ret;
}

static inline struct ar71xx_pci_controller *
pci_bus_to_ar71xx_controller(struct pci_bus *bus)
{
	struct pci_controller *hose;

	hose = (struct pci_controller *) bus->sysdata;
	return container_of(hose, struct ar71xx_pci_controller, pci_ctrl);
}

static int ar71xx_pci_check_error(struct ar71xx_pci_controller *apc, int quiet)
{
	void __iomem *base = apc->cfg_base;
	u32 pci_err;
	u32 ahb_err;

	pci_err = __raw_readl(base + AR71XX_PCI_REG_PCI_ERR) & 3;
	if (pci_err) {
		if (!quiet) {
			u32 addr;

			addr = __raw_readl(base + AR71XX_PCI_REG_PCI_ERR_ADDR);
			pr_crit("ar71xx: %s bus error %d at addr 0x%x\n",
				"PCI", pci_err, addr);
		}

		/* clear PCI error status */
		__raw_writel(pci_err, base + AR71XX_PCI_REG_PCI_ERR);
	}

	ahb_err = __raw_readl(base + AR71XX_PCI_REG_AHB_ERR) & 1;
	if (ahb_err) {
		if (!quiet) {
			u32 addr;

			addr = __raw_readl(base + AR71XX_PCI_REG_AHB_ERR_ADDR);
			pr_crit("ar71xx: %s bus error %d at addr 0x%x\n",
				"AHB", ahb_err, addr);
		}

		/* clear AHB error status */
		__raw_writel(ahb_err, base + AR71XX_PCI_REG_AHB_ERR);
	}

	return !!(ahb_err | pci_err);
}

static inline void ar71xx_pci_local_write(struct ar71xx_pci_controller *apc,
					  int where, int size, u32 value)
{
	void __iomem *base = apc->cfg_base;
	u32 ad_cbe;

	value = value << (8 * (where & 3));

	ad_cbe = AR71XX_PCI_CRP_CMD_WRITE | (where & ~3);
	ad_cbe |= ar71xx_pci_get_ble(where, size, 1);

	__raw_writel(ad_cbe, base + AR71XX_PCI_REG_CRP_AD_CBE);
	__raw_writel(value, base + AR71XX_PCI_REG_CRP_WRDATA);
}

static inline int ar71xx_pci_set_cfgaddr(struct pci_bus *bus,
					 unsigned int devfn,
					 int where, int size, u32 cmd)
{
	struct ar71xx_pci_controller *apc = pci_bus_to_ar71xx_controller(bus);
	void __iomem *base = apc->cfg_base;
	u32 addr;

	addr = ar71xx_pci_bus_addr(bus, devfn, where);

	__raw_writel(addr, base + AR71XX_PCI_REG_CFG_AD);
	__raw_writel(cmd | ar71xx_pci_get_ble(where, size, 0),
		     base + AR71XX_PCI_REG_CFG_CBE);

	return ar71xx_pci_check_error(apc, 1);
}

static int ar71xx_pci_read_config(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *value)
{
	struct ar71xx_pci_controller *apc = pci_bus_to_ar71xx_controller(bus);
	void __iomem *base = apc->cfg_base;
	unsigned long flags;
	u32 data;
	int err;
	int ret;

	ret = PCIBIOS_SUCCESSFUL;
	data = ~0;

	spin_lock_irqsave(&apc->lock, flags);

	err = ar71xx_pci_set_cfgaddr(bus, devfn, where, size,
				     AR71XX_PCI_CFG_CMD_READ);
	if (err)
		ret = PCIBIOS_DEVICE_NOT_FOUND;
	else
		data = __raw_readl(base + AR71XX_PCI_REG_CFG_RDDATA);

	spin_unlock_irqrestore(&apc->lock, flags);

	*value = (data >> (8 * (where & 3))) & ar71xx_pci_read_mask[size & 7];

	return ret;
}

static int ar71xx_pci_write_config(struct pci_bus *bus, unsigned int devfn,
				   int where, int size, u32 value)
{
	struct ar71xx_pci_controller *apc = pci_bus_to_ar71xx_controller(bus);
	void __iomem *base = apc->cfg_base;
	unsigned long flags;
	int err;
	int ret;

	value = value << (8 * (where & 3));
	ret = PCIBIOS_SUCCESSFUL;

	spin_lock_irqsave(&apc->lock, flags);

	err = ar71xx_pci_set_cfgaddr(bus, devfn, where, size,
				     AR71XX_PCI_CFG_CMD_WRITE);
	if (err)
		ret = PCIBIOS_DEVICE_NOT_FOUND;
	else
		__raw_writel(value, base + AR71XX_PCI_REG_CFG_WRDATA);

	spin_unlock_irqrestore(&apc->lock, flags);

	return ret;
}

static struct pci_ops ar71xx_pci_ops = {
	.read	= ar71xx_pci_read_config,
	.write	= ar71xx_pci_write_config,
};

static void ar71xx_pci_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct ar71xx_pci_controller *apc;
	void __iomem *base = ath79_reset_base;
	u32 pending;

	apc = irq_get_handler_data(irq);

	pending = __raw_readl(base + AR71XX_RESET_REG_PCI_INT_STATUS) &
		  __raw_readl(base + AR71XX_RESET_REG_PCI_INT_ENABLE);

	if (pending & AR71XX_PCI_INT_DEV0)
		generic_handle_irq(apc->irq_base + 0);

	else if (pending & AR71XX_PCI_INT_DEV1)
		generic_handle_irq(apc->irq_base + 1);

	else if (pending & AR71XX_PCI_INT_DEV2)
		generic_handle_irq(apc->irq_base + 2);

	else if (pending & AR71XX_PCI_INT_CORE)
		generic_handle_irq(apc->irq_base + 4);

	else
		spurious_interrupt();
}

static void ar71xx_pci_irq_unmask(struct irq_data *d)
{
	struct ar71xx_pci_controller *apc;
	unsigned int irq;
	void __iomem *base = ath79_reset_base;
	u32 t;

	apc = irq_data_get_irq_chip_data(d);
	irq = d->irq - apc->irq_base;

	t = __raw_readl(base + AR71XX_RESET_REG_PCI_INT_ENABLE);
	__raw_writel(t | (1 << irq), base + AR71XX_RESET_REG_PCI_INT_ENABLE);

	/* flush write */
	__raw_readl(base + AR71XX_RESET_REG_PCI_INT_ENABLE);
}

static void ar71xx_pci_irq_mask(struct irq_data *d)
{
	struct ar71xx_pci_controller *apc;
	unsigned int irq;
	void __iomem *base = ath79_reset_base;
	u32 t;

	apc = irq_data_get_irq_chip_data(d);
	irq = d->irq - apc->irq_base;

	t = __raw_readl(base + AR71XX_RESET_REG_PCI_INT_ENABLE);
	__raw_writel(t & ~(1 << irq), base + AR71XX_RESET_REG_PCI_INT_ENABLE);

	/* flush write */
	__raw_readl(base + AR71XX_RESET_REG_PCI_INT_ENABLE);
}

static struct irq_chip ar71xx_pci_irq_chip = {
	.name		= "AR71XX PCI",
	.irq_mask	= ar71xx_pci_irq_mask,
	.irq_unmask	= ar71xx_pci_irq_unmask,
	.irq_mask_ack	= ar71xx_pci_irq_mask,
};

static void ar71xx_pci_irq_init(struct ar71xx_pci_controller *apc)
{
	void __iomem *base = ath79_reset_base;
	int i;

	__raw_writel(0, base + AR71XX_RESET_REG_PCI_INT_ENABLE);
	__raw_writel(0, base + AR71XX_RESET_REG_PCI_INT_STATUS);

	BUILD_BUG_ON(ATH79_PCI_IRQ_COUNT < AR71XX_PCI_IRQ_COUNT);

	apc->irq_base = ATH79_PCI_IRQ_BASE;
	for (i = apc->irq_base;
	     i < apc->irq_base + AR71XX_PCI_IRQ_COUNT; i++) {
		irq_set_chip_and_handler(i, &ar71xx_pci_irq_chip,
					 handle_level_irq);
		irq_set_chip_data(i, apc);
	}

	irq_set_handler_data(apc->irq, apc);
	irq_set_chained_handler(apc->irq, ar71xx_pci_irq_handler);
}

static void ar71xx_pci_reset(void)
{
	void __iomem *ddr_base = ath79_ddr_base;

	ath79_device_reset_set(AR71XX_RESET_PCI_BUS | AR71XX_RESET_PCI_CORE);
	mdelay(100);

	ath79_device_reset_clear(AR71XX_RESET_PCI_BUS | AR71XX_RESET_PCI_CORE);
	mdelay(100);

	__raw_writel(AR71XX_PCI_WIN0_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN0);
	__raw_writel(AR71XX_PCI_WIN1_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN1);
	__raw_writel(AR71XX_PCI_WIN2_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN2);
	__raw_writel(AR71XX_PCI_WIN3_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN3);
	__raw_writel(AR71XX_PCI_WIN4_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN4);
	__raw_writel(AR71XX_PCI_WIN5_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN5);
	__raw_writel(AR71XX_PCI_WIN6_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN6);
	__raw_writel(AR71XX_PCI_WIN7_OFFS, ddr_base + AR71XX_DDR_REG_PCI_WIN7);

	mdelay(100);
}

static int ar71xx_pci_probe(struct platform_device *pdev)
{
	struct ar71xx_pci_controller *apc;
	struct resource *res;
	u32 t;

	apc = devm_kzalloc(&pdev->dev, sizeof(struct ar71xx_pci_controller),
			   GFP_KERNEL);
	if (!apc)
		return -ENOMEM;

	spin_lock_init(&apc->lock);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg_base");
	apc->cfg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(apc->cfg_base))
		return PTR_ERR(apc->cfg_base);

	apc->irq = platform_get_irq(pdev, 0);
	if (apc->irq < 0)
		return -EINVAL;

	res = platform_get_resource_byname(pdev, IORESOURCE_IO, "io_base");
	if (!res)
		return -EINVAL;

	apc->io_res.parent = res;
	apc->io_res.name = "PCI IO space";
	apc->io_res.start = res->start;
	apc->io_res.end = res->end;
	apc->io_res.flags = IORESOURCE_IO;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "mem_base");
	if (!res)
		return -EINVAL;

	apc->mem_res.parent = res;
	apc->mem_res.name = "PCI memory space";
	apc->mem_res.start = res->start;
	apc->mem_res.end = res->end;
	apc->mem_res.flags = IORESOURCE_MEM;

	ar71xx_pci_reset();

	/* setup COMMAND register */
	t = PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE
	  | PCI_COMMAND_PARITY | PCI_COMMAND_SERR | PCI_COMMAND_FAST_BACK;
	ar71xx_pci_local_write(apc, PCI_COMMAND, 4, t);

	/* clear bus errors */
	ar71xx_pci_check_error(apc, 1);

	ar71xx_pci_irq_init(apc);

	apc->pci_ctrl.pci_ops = &ar71xx_pci_ops;
	apc->pci_ctrl.mem_resource = &apc->mem_res;
	apc->pci_ctrl.io_resource = &apc->io_res;

	register_pci_controller(&apc->pci_ctrl);

	return 0;
}

static struct platform_driver ar71xx_pci_driver = {
	.probe = ar71xx_pci_probe,
	.driver = {
		.name = "ar71xx-pci",
		.owner = THIS_MODULE,
	},
};

static int __init ar71xx_pci_init(void)
{
	return platform_driver_register(&ar71xx_pci_driver);
}

postcore_initcall(ar71xx_pci_init);
