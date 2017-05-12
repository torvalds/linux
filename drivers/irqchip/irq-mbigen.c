/*
 * Copyright (C) 2015 Hisilicon Limited, All Rights Reserved.
 * Author: Jun Ma <majun258@huawei.com>
 * Author: Yun Wu <wuyun.wu@huawei.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Interrupt numbers per mbigen node supported */
#define IRQS_PER_MBIGEN_NODE		128

/* 64 irqs (Pin0-pin63) are reserved for each mbigen chip */
#define RESERVED_IRQ_PER_MBIGEN_CHIP	64

/* The maximum IRQ pin number of mbigen chip(start from 0) */
#define MAXIMUM_IRQ_PIN_NUM		1407

/**
 * In mbigen vector register
 * bit[21:12]:	event id value
 * bit[11:0]:	device id
 */
#define IRQ_EVENT_ID_SHIFT		12
#define IRQ_EVENT_ID_MASK		0x3ff

/* register range of each mbigen node */
#define MBIGEN_NODE_OFFSET		0x1000

/* offset of vector register in mbigen node */
#define REG_MBIGEN_VEC_OFFSET		0x200

/**
 * offset of clear register in mbigen node
 * This register is used to clear the status
 * of interrupt
 */
#define REG_MBIGEN_CLEAR_OFFSET		0xa000

/**
 * offset of interrupt type register
 * This register is used to configure interrupt
 * trigger type
 */
#define REG_MBIGEN_TYPE_OFFSET		0x0

/**
 * struct mbigen_device - holds the information of mbigen device.
 *
 * @pdev:		pointer to the platform device structure of mbigen chip.
 * @base:		mapped address of this mbigen chip.
 */
struct mbigen_device {
	struct platform_device	*pdev;
	void __iomem		*base;
};

static inline unsigned int get_mbigen_vec_reg(irq_hw_number_t hwirq)
{
	unsigned int nid, pin;

	hwirq -= RESERVED_IRQ_PER_MBIGEN_CHIP;
	nid = hwirq / IRQS_PER_MBIGEN_NODE + 1;
	pin = hwirq % IRQS_PER_MBIGEN_NODE;

	return pin * 4 + nid * MBIGEN_NODE_OFFSET
			+ REG_MBIGEN_VEC_OFFSET;
}

static inline void get_mbigen_type_reg(irq_hw_number_t hwirq,
					u32 *mask, u32 *addr)
{
	unsigned int nid, irq_ofst, ofst;

	hwirq -= RESERVED_IRQ_PER_MBIGEN_CHIP;
	nid = hwirq / IRQS_PER_MBIGEN_NODE + 1;
	irq_ofst = hwirq % IRQS_PER_MBIGEN_NODE;

	*mask = 1 << (irq_ofst % 32);
	ofst = irq_ofst / 32 * 4;

	*addr = ofst + nid * MBIGEN_NODE_OFFSET
		+ REG_MBIGEN_TYPE_OFFSET;
}

static inline void get_mbigen_clear_reg(irq_hw_number_t hwirq,
					u32 *mask, u32 *addr)
{
	unsigned int ofst;

	hwirq -= RESERVED_IRQ_PER_MBIGEN_CHIP;
	ofst = hwirq / 32 * 4;

	*mask = 1 << (hwirq % 32);
	*addr = ofst + REG_MBIGEN_CLEAR_OFFSET;
}

static void mbigen_eoi_irq(struct irq_data *data)
{
	void __iomem *base = data->chip_data;
	u32 mask, addr;

	get_mbigen_clear_reg(data->hwirq, &mask, &addr);

	writel_relaxed(mask, base + addr);

	irq_chip_eoi_parent(data);
}

static int mbigen_set_type(struct irq_data *data, unsigned int type)
{
	void __iomem *base = data->chip_data;
	u32 mask, addr, val;

	if (type != IRQ_TYPE_LEVEL_HIGH && type != IRQ_TYPE_EDGE_RISING)
		return -EINVAL;

	get_mbigen_type_reg(data->hwirq, &mask, &addr);

	val = readl_relaxed(base + addr);

	if (type == IRQ_TYPE_LEVEL_HIGH)
		val |= mask;
	else
		val &= ~mask;

	writel_relaxed(val, base + addr);

	return 0;
}

static struct irq_chip mbigen_irq_chip = {
	.name =			"mbigen-v2",
	.irq_mask =		irq_chip_mask_parent,
	.irq_unmask =		irq_chip_unmask_parent,
	.irq_eoi =		mbigen_eoi_irq,
	.irq_set_type =		mbigen_set_type,
	.irq_set_affinity =	irq_chip_set_affinity_parent,
};

static void mbigen_write_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	struct irq_data *d = irq_get_irq_data(desc->irq);
	void __iomem *base = d->chip_data;
	u32 val;

	base += get_mbigen_vec_reg(d->hwirq);
	val = readl_relaxed(base);

	val &= ~(IRQ_EVENT_ID_MASK << IRQ_EVENT_ID_SHIFT);
	val |= (msg->data << IRQ_EVENT_ID_SHIFT);

	/* The address of doorbell is encoded in mbigen register by default
	 * So,we don't need to program the doorbell address at here
	 */
	writel_relaxed(val, base);
}

static int mbigen_domain_translate(struct irq_domain *d,
				    struct irq_fwspec *fwspec,
				    unsigned long *hwirq,
				    unsigned int *type)
{
	if (is_of_node(fwspec->fwnode) || is_acpi_device_node(fwspec->fwnode)) {
		if (fwspec->param_count != 2)
			return -EINVAL;

		if ((fwspec->param[0] > MAXIMUM_IRQ_PIN_NUM) ||
			(fwspec->param[0] < RESERVED_IRQ_PER_MBIGEN_CHIP))
			return -EINVAL;
		else
			*hwirq = fwspec->param[0];

		/* If there is no valid irq type, just use the default type */
		if ((fwspec->param[1] == IRQ_TYPE_EDGE_RISING) ||
			(fwspec->param[1] == IRQ_TYPE_LEVEL_HIGH))
			*type = fwspec->param[1];
		else
			return -EINVAL;

		return 0;
	}
	return -EINVAL;
}

static int mbigen_irq_domain_alloc(struct irq_domain *domain,
					unsigned int virq,
					unsigned int nr_irqs,
					void *args)
{
	struct irq_fwspec *fwspec = args;
	irq_hw_number_t hwirq;
	unsigned int type;
	struct mbigen_device *mgn_chip;
	int i, err;

	err = mbigen_domain_translate(domain, fwspec, &hwirq, &type);
	if (err)
		return err;

	err = platform_msi_domain_alloc(domain, virq, nr_irqs);
	if (err)
		return err;

	mgn_chip = platform_msi_get_host_data(domain);

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_hwirq_and_chip(domain, virq + i, hwirq + i,
				      &mbigen_irq_chip, mgn_chip->base);

	return 0;
}

static struct irq_domain_ops mbigen_domain_ops = {
	.translate	= mbigen_domain_translate,
	.alloc		= mbigen_irq_domain_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int mbigen_of_create_domain(struct platform_device *pdev,
				   struct mbigen_device *mgn_chip)
{
	struct device *parent;
	struct platform_device *child;
	struct irq_domain *domain;
	struct device_node *np;
	u32 num_pins;

	for_each_child_of_node(pdev->dev.of_node, np) {
		if (!of_property_read_bool(np, "interrupt-controller"))
			continue;

		parent = platform_bus_type.dev_root;
		child = of_platform_device_create(np, NULL, parent);
		if (!child)
			return -ENOMEM;

		if (of_property_read_u32(child->dev.of_node, "num-pins",
					 &num_pins) < 0) {
			dev_err(&pdev->dev, "No num-pins property\n");
			return -EINVAL;
		}

		domain = platform_msi_create_device_domain(&child->dev, num_pins,
							   mbigen_write_msg,
							   &mbigen_domain_ops,
							   mgn_chip);
		if (!domain)
			return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_ACPI
static int mbigen_acpi_create_domain(struct platform_device *pdev,
				     struct mbigen_device *mgn_chip)
{
	struct irq_domain *domain;
	u32 num_pins = 0;
	int ret;

	/*
	 * "num-pins" is the total number of interrupt pins implemented in
	 * this mbigen instance, and mbigen is an interrupt controller
	 * connected to ITS  converting wired interrupts into MSI, so we
	 * use "num-pins" to alloc MSI vectors which are needed by client
	 * devices connected to it.
	 *
	 * Here is the DSDT device node used for mbigen in firmware:
	 *	Device(MBI0) {
	 *		Name(_HID, "HISI0152")
	 *		Name(_UID, Zero)
	 *		Name(_CRS, ResourceTemplate() {
	 *			Memory32Fixed(ReadWrite, 0xa0080000, 0x10000)
	 *		})
	 *
	 *		Name(_DSD, Package () {
	 *			ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
	 *			Package () {
	 *				Package () {"num-pins", 378}
	 *			}
	 *		})
	 *	}
	 */
	ret = device_property_read_u32(&pdev->dev, "num-pins", &num_pins);
	if (ret || num_pins == 0)
		return -EINVAL;

	domain = platform_msi_create_device_domain(&pdev->dev, num_pins,
						   mbigen_write_msg,
						   &mbigen_domain_ops,
						   mgn_chip);
	if (!domain)
		return -ENOMEM;

	return 0;
}
#else
static inline int mbigen_acpi_create_domain(struct platform_device *pdev,
					    struct mbigen_device *mgn_chip)
{
	return -ENODEV;
}
#endif

static int mbigen_device_probe(struct platform_device *pdev)
{
	struct mbigen_device *mgn_chip;
	struct resource *res;
	int err;

	mgn_chip = devm_kzalloc(&pdev->dev, sizeof(*mgn_chip), GFP_KERNEL);
	if (!mgn_chip)
		return -ENOMEM;

	mgn_chip->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	mgn_chip->base = devm_ioremap(&pdev->dev, res->start,
				      resource_size(res));
	if (!mgn_chip->base) {
		dev_err(&pdev->dev, "failed to ioremap %pR\n", res);
		return -ENOMEM;
	}

	if (IS_ENABLED(CONFIG_OF) && pdev->dev.of_node)
		err = mbigen_of_create_domain(pdev, mgn_chip);
	else if (ACPI_COMPANION(&pdev->dev))
		err = mbigen_acpi_create_domain(pdev, mgn_chip);
	else
		err = -EINVAL;

	if (err) {
		dev_err(&pdev->dev, "Failed to create mbi-gen@%p irqdomain",
			mgn_chip->base);
		return err;
	}

	platform_set_drvdata(pdev, mgn_chip);
	return 0;
}

static const struct of_device_id mbigen_of_match[] = {
	{ .compatible = "hisilicon,mbigen-v2" },
	{ /* END */ }
};
MODULE_DEVICE_TABLE(of, mbigen_of_match);

static const struct acpi_device_id mbigen_acpi_match[] = {
	{ "HISI0152", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, mbigen_acpi_match);

static struct platform_driver mbigen_platform_driver = {
	.driver = {
		.name		= "Hisilicon MBIGEN-V2",
		.of_match_table	= mbigen_of_match,
		.acpi_match_table = ACPI_PTR(mbigen_acpi_match),
	},
	.probe			= mbigen_device_probe,
};

module_platform_driver(mbigen_platform_driver);

MODULE_AUTHOR("Jun Ma <majun258@huawei.com>");
MODULE_AUTHOR("Yun Wu <wuyun.wu@huawei.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hisilicon MBI Generator driver");
