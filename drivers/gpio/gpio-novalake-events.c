// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Nova Lake GPIO-signaled ACPI events driver
 *
 * Copyright (c) 2026, Intel Corporation.
 *
 * Author: Alan Borzeszkowski <alan.borzeszkowski@linux.intel.com>
 *
 * Intel client platforms released in 2026 and later (starting with Intel Nova
 * Lake) support two modes of handling ACPI General Purpose Events (GPE):
 * exposed GPIO interrupt mode and legacy mode.
 *
 * By default, the platform uses legacy mode, handling GPEs as usual. If this
 * driver is installed, it signals to the platform (on every boot) that exposed
 * GPIO interrupt mode is supported. The platform then switches to exposed
 * mode, which takes effect on next boot. From the user perspective, this
 * change is transparent.
 *
 * However, if driver is uninstalled while in exposed interrupt mode, GPEs will
 * _not_ be handled until platform falls back to legacy mode. This means that
 * USB keyboard, mouse might not function properly for the fallback duration.
 * Fallback requires two reboots to take effect: on first reboot, platform no
 * longer receives signal from this driver and switches to legacy mode, which
 * takes effect on second boot.
 *
 * Example ACPI event: Power Management Event coming from motherboard PCH,
 * waking system from sleep following USB mouse hotplug.
 *
 * This driver supports up to 128 GPIO pins in each GPE block, per ACPI
 * specification v6.6 section 5.6.4.
 */

#include <linux/acpi.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp_types.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uuid.h>

#include <linux/gpio/driver.h>

/*
 * GPE block has two registers, each register takes half the block size.
 * Convert size to bits to get total GPIO pin count.
 */
#define GPE_BLK_REG_SIZE(block_size)	((block_size) / 2)
#define GPE_REG_PIN_COUNT(block_size)	BYTES_TO_BITS(GPE_BLK_REG_SIZE(block_size))
#define GPE_STS_REG_OFFSET		0
#define GPE_EN_REG_OFFSET(block_size)	GPE_BLK_REG_SIZE(block_size)

/**
 * struct nvl_gpio - Intel Nova Lake GPIO driver state
 * @gc: GPIO controller interface
 * @reg_base: Base address of the GPE registers
 * @lock: Guard register access
 * @blk_size: GPE block length
 */
struct nvl_gpio {
	struct gpio_chip gc;
	void __iomem *reg_base;
	raw_spinlock_t lock;
	size_t blk_size;
};

static void __iomem *nvl_gpio_get_byte_addr(struct nvl_gpio *priv,
					    unsigned int reg_offset,
					    unsigned long gpio)
{
	return priv->reg_base + reg_offset + gpio;
}

static int nvl_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct nvl_gpio *priv = gpiochip_get_data(gc);
	unsigned int byte_idx = gpio / BITS_PER_BYTE;
	unsigned int bit_idx = gpio % BITS_PER_BYTE;
	void __iomem *addr;
	u8 reg;

	addr = nvl_gpio_get_byte_addr(priv, GPE_STS_REG_OFFSET, byte_idx);

	guard(raw_spinlock_irqsave)(&priv->lock);

	reg = ioread8(addr);

	return !!(reg & BIT(bit_idx));
}

static const struct gpio_chip nvl_gpio_chip = {
	.owner	= THIS_MODULE,
	.get	= nvl_gpio_get,
};

static int nvl_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);
	else if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static void nvl_gpio_irq_mask_unmask(struct gpio_chip *gc, unsigned long hwirq,
				     bool mask)
{
	struct nvl_gpio *priv = gpiochip_get_data(gc);
	unsigned int byte_idx = hwirq / BITS_PER_BYTE;
	unsigned int bit_idx = hwirq % BITS_PER_BYTE;
	void __iomem *addr;
	u8 reg;

	addr = nvl_gpio_get_byte_addr(priv, GPE_EN_REG_OFFSET(priv->blk_size), byte_idx);

	guard(raw_spinlock_irqsave)(&priv->lock);

	reg = ioread8(addr);
	if (mask)
		reg &= ~BIT(bit_idx);
	else
		reg |= BIT(bit_idx);
	iowrite8(reg, addr);
}

static void nvl_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	nvl_gpio_irq_mask_unmask(gc, hwirq, false);
}

static void nvl_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	nvl_gpio_irq_mask_unmask(gc, hwirq, true);
	gpiochip_disable_irq(gc, hwirq);
}

static void nvl_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct nvl_gpio *priv = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned int byte_idx = hwirq / BITS_PER_BYTE;
	unsigned int bit_idx = hwirq % BITS_PER_BYTE;
	void __iomem *addr;
	u8 reg;

	addr = nvl_gpio_get_byte_addr(priv, GPE_STS_REG_OFFSET, byte_idx);

	guard(raw_spinlock_irqsave)(&priv->lock);

	reg = ioread8(addr);
	reg |= BIT(bit_idx);
	iowrite8(reg, addr);
}

static const struct irq_chip nvl_gpio_irq_chip = {
	.name		= "gpio-novalake",
	.irq_ack	= nvl_gpio_irq_ack,
	.irq_mask	= nvl_gpio_irq_mask,
	.irq_unmask	= nvl_gpio_irq_unmask,
	.irq_set_type	= nvl_gpio_irq_set_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static irqreturn_t nvl_gpio_irq(int irq, void *data)
{
	struct nvl_gpio *priv = data;
	const size_t block_size = priv->blk_size;
	unsigned int handled = 0;

	for (unsigned int i = 0; i < block_size; i++) {
		const void __iomem *reg = priv->reg_base + i;
		unsigned long pending;
		unsigned long enabled;
		unsigned int bit_idx;

		scoped_guard(raw_spinlock, &priv->lock) {
			pending = ioread8(reg + GPE_STS_REG_OFFSET);
			enabled = ioread8(reg + GPE_EN_REG_OFFSET(block_size));
		}
		pending &= enabled;

		for_each_set_bit(bit_idx, &pending, BITS_PER_BYTE) {
			unsigned int hwirq = i * BITS_PER_BYTE + bit_idx;

			generic_handle_domain_irq(priv->gc.irq.domain, hwirq);
		}

		handled += pending ? 1 : 0;
	}

	return IRQ_RETVAL(handled);
}

/* UUID for GPE device _DSM: 079406e6-bdea-49cf-8563-03e2811901cb */
static const guid_t nvl_gpe_dsm_guid =
	GUID_INIT(0x079406e6, 0xbdea, 0x49cf,
		  0x85, 0x63, 0x03, 0xe2, 0x81, 0x19, 0x01, 0xcb);

#define DSM_GPE_MODE_REV	1
#define DSM_GPE_MODE_FN_INDEX	1
#define DSM_ENABLE_GPE_MODE	1

static int nvl_acpi_enable_gpe_mode(struct device *dev)
{
	union acpi_object argv4[2];
	union acpi_object *obj;

	argv4[0].type = ACPI_TYPE_PACKAGE;
	argv4[0].package.count = 1;
	argv4[0].package.elements = &argv4[1];
	argv4[1].integer.type = ACPI_TYPE_INTEGER;
	argv4[1].integer.value = DSM_ENABLE_GPE_MODE;

	obj = acpi_evaluate_dsm_typed(ACPI_HANDLE(dev), &nvl_gpe_dsm_guid,
				      DSM_GPE_MODE_REV, DSM_GPE_MODE_FN_INDEX,
				      argv4, ACPI_TYPE_BUFFER);
	if (!obj)
		return -EIO;
	ACPI_FREE(obj);

	return 0;
}

static int nvl_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	resource_size_t ioresource_size;
	struct gpio_irq_chip *girq;
	struct nvl_gpio *priv;
	struct resource *res;
	void __iomem *regs;
	int ret, irq;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENXIO;

	/*
	 * GPE block length should be non-negative multiple of two and allow up
	 * to 128 pins. ACPI v6.6 section 5.2.9 and 5.6.4.
	 */
	ioresource_size = resource_size(res);
	if (!ioresource_size || ioresource_size % 2 || ioresource_size > 0x20)
		return dev_err_probe(dev, -EINVAL,
				     "invalid GPE block length, resource: %pR\n",
				     res);

	regs = devm_ioport_map(dev, res->start, ioresource_size);
	if (!regs)
		return -ENOMEM;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	raw_spin_lock_init(&priv->lock);

	priv->reg_base = regs;
	priv->blk_size = ioresource_size;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, nvl_gpio_irq, IRQF_SHARED, dev_name(dev), priv);
	if (ret)
		return ret;

	priv->gc	= nvl_gpio_chip;
	priv->gc.label	= dev_name(dev);
	priv->gc.parent	= dev;
	priv->gc.ngpio	= GPE_REG_PIN_COUNT(priv->blk_size);
	priv->gc.base	= -1;

	girq = &priv->gc.irq;
	gpio_irq_chip_set_chip(girq, &nvl_gpio_irq_chip);
	girq->parent_handler	= NULL;
	girq->num_parents	= 0;
	girq->parents		= NULL;
	girq->default_type	= IRQ_TYPE_NONE;
	girq->handler		= handle_bad_irq;

	ret = devm_gpiochip_add_data(dev, &priv->gc, priv);
	if (ret)
		return ret;

	return nvl_acpi_enable_gpe_mode(dev);
}

static const struct acpi_device_id nvl_gpio_acpi_match[] = {
	{ "INTC1114" },
	{}
};
MODULE_DEVICE_TABLE(acpi, nvl_gpio_acpi_match);

static struct platform_driver nvl_gpio_driver = {
	.driver = {
		.name		  = "gpio-novalake-events",
		.acpi_match_table = nvl_gpio_acpi_match,
	},
	.probe = nvl_gpio_probe,
};
module_platform_driver(nvl_gpio_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alan Borzeszkowski <alan.borzeszkowski@linux.intel.com>");
MODULE_DESCRIPTION("Intel Nova Lake ACPI GPIO events driver");
