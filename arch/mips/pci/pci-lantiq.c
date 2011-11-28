/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2010 John Crispin <blogic@openwrt.org>
 */

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/platform_device.h>

#include <asm/pci.h>
#include <asm/gpio.h>
#include <asm/addrspace.h>

#include <lantiq_soc.h>
#include <lantiq_irq.h>
#include <lantiq_platform.h>

#include "pci-lantiq.h"

#define LTQ_PCI_CFG_BASE		0x17000000
#define LTQ_PCI_CFG_SIZE		0x00008000
#define LTQ_PCI_MEM_BASE		0x18000000
#define LTQ_PCI_MEM_SIZE		0x02000000
#define LTQ_PCI_IO_BASE			0x1AE00000
#define LTQ_PCI_IO_SIZE			0x00200000

#define PCI_CR_FCI_ADDR_MAP0		0x00C0
#define PCI_CR_FCI_ADDR_MAP1		0x00C4
#define PCI_CR_FCI_ADDR_MAP2		0x00C8
#define PCI_CR_FCI_ADDR_MAP3		0x00CC
#define PCI_CR_FCI_ADDR_MAP4		0x00D0
#define PCI_CR_FCI_ADDR_MAP5		0x00D4
#define PCI_CR_FCI_ADDR_MAP6		0x00D8
#define PCI_CR_FCI_ADDR_MAP7		0x00DC
#define PCI_CR_CLK_CTRL			0x0000
#define PCI_CR_PCI_MOD			0x0030
#define PCI_CR_PC_ARB			0x0080
#define PCI_CR_FCI_ADDR_MAP11hg		0x00E4
#define PCI_CR_BAR11MASK		0x0044
#define PCI_CR_BAR12MASK		0x0048
#define PCI_CR_BAR13MASK		0x004C
#define PCI_CS_BASE_ADDR1		0x0010
#define PCI_CR_PCI_ADDR_MAP11		0x0064
#define PCI_CR_FCI_BURST_LENGTH		0x00E8
#define PCI_CR_PCI_EOI			0x002C
#define PCI_CS_STS_CMD			0x0004

#define PCI_MASTER0_REQ_MASK_2BITS	8
#define PCI_MASTER1_REQ_MASK_2BITS	10
#define PCI_MASTER2_REQ_MASK_2BITS	12
#define INTERNAL_ARB_ENABLE_BIT		0

#define LTQ_CGU_IFCCR		0x0018
#define LTQ_CGU_PCICR		0x0034

#define ltq_pci_w32(x, y)	ltq_w32((x), ltq_pci_membase + (y))
#define ltq_pci_r32(x)		ltq_r32(ltq_pci_membase + (x))

#define ltq_pci_cfg_w32(x, y)	ltq_w32((x), ltq_pci_mapped_cfg + (y))
#define ltq_pci_cfg_r32(x)	ltq_r32(ltq_pci_mapped_cfg + (x))

struct ltq_pci_gpio_map {
	int pin;
	int alt0;
	int alt1;
	int dir;
	char *name;
};

/* the pci core can make use of the following gpios */
static struct ltq_pci_gpio_map ltq_pci_gpio_map[] = {
	{ 0, 1, 0, 0, "pci-exin0" },
	{ 1, 1, 0, 0, "pci-exin1" },
	{ 2, 1, 0, 0, "pci-exin2" },
	{ 39, 1, 0, 0, "pci-exin3" },
	{ 10, 1, 0, 0, "pci-exin4" },
	{ 9, 1, 0, 0, "pci-exin5" },
	{ 30, 1, 0, 1, "pci-gnt1" },
	{ 23, 1, 0, 1, "pci-gnt2" },
	{ 19, 1, 0, 1, "pci-gnt3" },
	{ 38, 1, 0, 1, "pci-gnt4" },
	{ 29, 1, 0, 0, "pci-req1" },
	{ 31, 1, 0, 0, "pci-req2" },
	{ 3, 1, 0, 0, "pci-req3" },
	{ 37, 1, 0, 0, "pci-req4" },
};

__iomem void *ltq_pci_mapped_cfg;
static __iomem void *ltq_pci_membase;

int (*ltqpci_plat_dev_init)(struct pci_dev *dev) = NULL;

/* Since the PCI REQ pins can be reused for other functionality, make it
   possible to exclude those from interpretation by the PCI controller */
static int ltq_pci_req_mask = 0xf;

static int *ltq_pci_irq_map;

struct pci_ops ltq_pci_ops = {
	.read	= ltq_pci_read_config_dword,
	.write	= ltq_pci_write_config_dword
};

static struct resource pci_io_resource = {
	.name	= "pci io space",
	.start	= LTQ_PCI_IO_BASE,
	.end	= LTQ_PCI_IO_BASE + LTQ_PCI_IO_SIZE - 1,
	.flags	= IORESOURCE_IO
};

static struct resource pci_mem_resource = {
	.name	= "pci memory space",
	.start	= LTQ_PCI_MEM_BASE,
	.end	= LTQ_PCI_MEM_BASE + LTQ_PCI_MEM_SIZE - 1,
	.flags	= IORESOURCE_MEM
};

static struct pci_controller ltq_pci_controller = {
	.pci_ops	= &ltq_pci_ops,
	.mem_resource	= &pci_mem_resource,
	.mem_offset	= 0x00000000UL,
	.io_resource	= &pci_io_resource,
	.io_offset	= 0x00000000UL,
};

int pcibios_plat_dev_init(struct pci_dev *dev)
{
	if (ltqpci_plat_dev_init)
		return ltqpci_plat_dev_init(dev);

	return 0;
}

static u32 ltq_calc_bar11mask(void)
{
	u32 mem, bar11mask;

	/* BAR11MASK value depends on available memory on system. */
	mem = num_physpages * PAGE_SIZE;
	bar11mask = (0x0ffffff0 & ~((1 << (fls(mem) - 1)) - 1)) | 8;

	return bar11mask;
}

static void ltq_pci_setup_gpio(int gpio)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ltq_pci_gpio_map); i++) {
		if (gpio & (1 << i)) {
			ltq_gpio_request(ltq_pci_gpio_map[i].pin,
				ltq_pci_gpio_map[i].alt0,
				ltq_pci_gpio_map[i].alt1,
				ltq_pci_gpio_map[i].dir,
				ltq_pci_gpio_map[i].name);
		}
	}
	ltq_gpio_request(21, 0, 0, 1, "pci-reset");
	ltq_pci_req_mask = (gpio >> PCI_REQ_SHIFT) & PCI_REQ_MASK;
}

static int __devinit ltq_pci_startup(struct ltq_pci_data *conf)
{
	u32 temp_buffer;

	/* set clock to 33Mhz */
	if (ltq_is_ar9()) {
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) & ~0x1f00000, LTQ_CGU_IFCCR);
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) | 0xe00000, LTQ_CGU_IFCCR);
	} else {
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) & ~0xf00000, LTQ_CGU_IFCCR);
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) | 0x800000, LTQ_CGU_IFCCR);
	}

	/* external or internal clock ? */
	if (conf->clock) {
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) & ~(1 << 16),
			LTQ_CGU_IFCCR);
		ltq_cgu_w32((1 << 30), LTQ_CGU_PCICR);
	} else {
		ltq_cgu_w32(ltq_cgu_r32(LTQ_CGU_IFCCR) | (1 << 16),
			LTQ_CGU_IFCCR);
		ltq_cgu_w32((1 << 31) | (1 << 30), LTQ_CGU_PCICR);
	}

	/* setup pci clock and gpis used by pci */
	ltq_pci_setup_gpio(conf->gpio);

	/* enable auto-switching between PCI and EBU */
	ltq_pci_w32(0xa, PCI_CR_CLK_CTRL);

	/* busy, i.e. configuration is not done, PCI access has to be retried */
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_MOD) & ~(1 << 24), PCI_CR_PCI_MOD);
	wmb();
	/* BUS Master/IO/MEM access */
	ltq_pci_cfg_w32(ltq_pci_cfg_r32(PCI_CS_STS_CMD) | 7, PCI_CS_STS_CMD);

	/* enable external 2 PCI masters */
	temp_buffer = ltq_pci_r32(PCI_CR_PC_ARB);
	temp_buffer &= (~(ltq_pci_req_mask << 16));
	/* enable internal arbiter */
	temp_buffer |= (1 << INTERNAL_ARB_ENABLE_BIT);
	/* enable internal PCI master reqest */
	temp_buffer &= (~(3 << PCI_MASTER0_REQ_MASK_2BITS));

	/* enable EBU request */
	temp_buffer &= (~(3 << PCI_MASTER1_REQ_MASK_2BITS));

	/* enable all external masters request */
	temp_buffer &= (~(3 << PCI_MASTER2_REQ_MASK_2BITS));
	ltq_pci_w32(temp_buffer, PCI_CR_PC_ARB);
	wmb();

	/* setup BAR memory regions */
	ltq_pci_w32(0x18000000, PCI_CR_FCI_ADDR_MAP0);
	ltq_pci_w32(0x18400000, PCI_CR_FCI_ADDR_MAP1);
	ltq_pci_w32(0x18800000, PCI_CR_FCI_ADDR_MAP2);
	ltq_pci_w32(0x18c00000, PCI_CR_FCI_ADDR_MAP3);
	ltq_pci_w32(0x19000000, PCI_CR_FCI_ADDR_MAP4);
	ltq_pci_w32(0x19400000, PCI_CR_FCI_ADDR_MAP5);
	ltq_pci_w32(0x19800000, PCI_CR_FCI_ADDR_MAP6);
	ltq_pci_w32(0x19c00000, PCI_CR_FCI_ADDR_MAP7);
	ltq_pci_w32(0x1ae00000, PCI_CR_FCI_ADDR_MAP11hg);
	ltq_pci_w32(ltq_calc_bar11mask(), PCI_CR_BAR11MASK);
	ltq_pci_w32(0, PCI_CR_PCI_ADDR_MAP11);
	ltq_pci_w32(0, PCI_CS_BASE_ADDR1);
	/* both TX and RX endian swap are enabled */
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_EOI) | 3, PCI_CR_PCI_EOI);
	wmb();
	ltq_pci_w32(ltq_pci_r32(PCI_CR_BAR12MASK) | 0x80000000,
		PCI_CR_BAR12MASK);
	ltq_pci_w32(ltq_pci_r32(PCI_CR_BAR13MASK) | 0x80000000,
		PCI_CR_BAR13MASK);
	/*use 8 dw burst length */
	ltq_pci_w32(0x303, PCI_CR_FCI_BURST_LENGTH);
	ltq_pci_w32(ltq_pci_r32(PCI_CR_PCI_MOD) | (1 << 24), PCI_CR_PCI_MOD);
	wmb();

	/* setup irq line */
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_PCC_CON) | 0xc, LTQ_EBU_PCC_CON);
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_PCC_IEN) | 0x10, LTQ_EBU_PCC_IEN);

	/* toggle reset pin */
	__gpio_set_value(21, 0);
	wmb();
	mdelay(1);
	__gpio_set_value(21, 1);
	return 0;
}

int __init pcibios_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	if (ltq_pci_irq_map[slot])
		return ltq_pci_irq_map[slot];
	printk(KERN_ERR "lq_pci: trying to map irq for unknown slot %d\n",
		slot);

	return 0;
}

static int __devinit ltq_pci_probe(struct platform_device *pdev)
{
	struct ltq_pci_data *ltq_pci_data =
		(struct ltq_pci_data *) pdev->dev.platform_data;
	pci_probe_only = 0;
	ltq_pci_irq_map = ltq_pci_data->irq;
	ltq_pci_membase = ioremap_nocache(PCI_CR_BASE_ADDR, PCI_CR_SIZE);
	ltq_pci_mapped_cfg =
		ioremap_nocache(LTQ_PCI_CFG_BASE, LTQ_PCI_CFG_BASE);
	ltq_pci_controller.io_map_base =
		(unsigned long)ioremap(LTQ_PCI_IO_BASE, LTQ_PCI_IO_SIZE - 1);
	ltq_pci_startup(ltq_pci_data);
	register_pci_controller(&ltq_pci_controller);

	return 0;
}

static struct platform_driver
ltq_pci_driver = {
	.probe = ltq_pci_probe,
	.driver = {
		.name = "ltq_pci",
		.owner = THIS_MODULE,
	},
};

int __init pcibios_init(void)
{
	int ret = platform_driver_register(&ltq_pci_driver);
	if (ret)
		printk(KERN_INFO "ltq_pci: Error registering platfom driver!");
	return ret;
}

arch_initcall(pcibios_init);
