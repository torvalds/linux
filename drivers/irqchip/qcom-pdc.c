// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/soc/qcom/irq.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/qcom_scm.h>
#include <linux/ipc_logging.h>

#define PDC_IPC_LOG_SZ		2
#define PDC_MAX_GPIO_IRQS	256

#define IRQ_ENABLE_BANK		0x10
#define IRQ_i_CFG		0x110
#define IRQ_i_CFG_IRQ_ENABLE	3
#define IRQ_i_CFG_TYPE_MASK	0x7

#define VERSION			0x1000
#define MAJOR_VER_MASK		0xFF
#define MAJOR_VER_SHIFT		16
#define MINOR_VER_MASK		0xFF
#define MINOR_VER_SHIFT		8

struct pdc_pin_region {
	u32 pin_base;
	u32 parent_base;
	u32 cnt;
};

struct spi_cfg_regs {
	union {
		u64 start;
		void __iomem *base;
	};
	resource_size_t size;
	bool scm_io;
};

#define pin_to_hwirq(r, p)	((r)->parent_base + (p) - (r)->pin_base)

static DEFINE_RAW_SPINLOCK(pdc_lock);
static void __iomem *pdc_base;
static struct pdc_pin_region *pdc_region;
static int pdc_region_cnt;
static struct spi_cfg_regs *spi_cfg;
static void *pdc_ipc_log;
static bool enable_in_cfg;

static u32 __spi_pin_read(unsigned int pin)
{
	void __iomem *cfg_reg = spi_cfg->base + pin * 4;
	u64 scm_cfg_reg = spi_cfg->start + pin * 4;

	if (spi_cfg->scm_io) {
		unsigned int val;

		qcom_scm_io_readl(scm_cfg_reg, &val);
		return val;
	} else {
		return readl(cfg_reg);
	}
}

static void __spi_pin_write(unsigned int pin, unsigned int val)
{
	void __iomem *cfg_reg = spi_cfg->base + pin * 4;
	u64 scm_cfg_reg = spi_cfg->start + pin * 4;

	if (spi_cfg->scm_io)
		qcom_scm_io_writel(scm_cfg_reg, val);
	else
		writel(val, cfg_reg);
}

static int spi_configure_type(irq_hw_number_t hwirq, unsigned int type)
{
	int spi = hwirq - 32;
	u32 pin = spi / 32;
	u32 mask = BIT(spi % 32);
	u32 val;
	unsigned long flags;

	if (!spi_cfg)
		return 0;

	if (pin * 4 > spi_cfg->size)
		return -EFAULT;

	raw_spin_lock_irqsave(&pdc_lock, flags);
	val = __spi_pin_read(pin);
	val &= ~mask;
	if (type & IRQ_TYPE_LEVEL_MASK)
		val |= mask;
	__spi_pin_write(pin, val);
	ipc_log_string(pdc_ipc_log,
		       "SPI config: GIC-SPI=%d (reg=%d,bit=%d) val=%d",
		       spi, pin, spi % 32, type & IRQ_TYPE_LEVEL_MASK);
	raw_spin_unlock_irqrestore(&pdc_lock, flags);

	return 0;
}

static void pdc_reg_write(int reg, u32 i, u32 val)
{
	writel_relaxed(val, pdc_base + reg + i * sizeof(u32));
}

static u32 pdc_reg_read(int reg, u32 i)
{
	return readl_relaxed(pdc_base + reg + i * sizeof(u32));
}

static void __pdc_enable_intr(int pin_out, bool on)
{
	unsigned long enable;

	if (!enable_in_cfg) {
		u32 index, mask;
		index = pin_out / 32;
		mask = pin_out % 32;
		enable = pdc_reg_read(IRQ_ENABLE_BANK, index);
		__assign_bit(mask, &enable, on);
		pdc_reg_write(IRQ_ENABLE_BANK, index, enable);
	} else {
		enable = pdc_reg_read(IRQ_i_CFG, pin_out);
		__assign_bit(IRQ_i_CFG_IRQ_ENABLE, &enable, on);
		pdc_reg_write(IRQ_i_CFG, pin_out, enable);
	}
}

static void pdc_enable_intr(struct irq_data *d, bool on)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&pdc_lock, flags);
	__pdc_enable_intr(d->hwirq, on);
	raw_spin_unlock_irqrestore(&pdc_lock, flags);
	ipc_log_string(pdc_ipc_log, "PIN=%lu enable=%d", d->hwirq, on);
}

static void qcom_pdc_gic_disable(struct irq_data *d)
{
	pdc_enable_intr(d, false);
	irq_chip_disable_parent(d);
}

static void qcom_pdc_gic_enable(struct irq_data *d)
{
	pdc_enable_intr(d, true);
	irq_chip_enable_parent(d);
}

/*
 * GIC does not handle falling edge or active low. To allow falling edge and
 * active low interrupts to be handled at GIC, PDC has an inverter that inverts
 * falling edge into a rising edge and active low into an active high.
 * For the inverter to work, the polarity bit in the IRQ_CONFIG register has to
 * set as per the table below.
 * Level sensitive active low    LOW
 * Rising edge sensitive         NOT USED
 * Falling edge sensitive        LOW
 * Dual Edge sensitive           NOT USED
 * Level sensitive active High   HIGH
 * Falling Edge sensitive        NOT USED
 * Rising edge sensitive         HIGH
 * Dual Edge sensitive           HIGH
 */
enum pdc_irq_config_bits {
	PDC_LEVEL_LOW		= 0b000,
	PDC_EDGE_FALLING	= 0b010,
	PDC_LEVEL_HIGH		= 0b100,
	PDC_EDGE_RISING		= 0b110,
	PDC_EDGE_DUAL		= 0b111,
};

/**
 * qcom_pdc_gic_set_type: Configure PDC for the interrupt
 *
 * @d: the interrupt data
 * @type: the interrupt type
 *
 * If @type is edge triggered, forward that as Rising edge as PDC
 * takes care of converting falling edge to rising edge signal
 * If @type is level, then forward that as level high as PDC
 * takes care of converting falling edge to rising edge signal
 */
static int qcom_pdc_gic_set_type(struct irq_data *d, unsigned int type)
{
	int parent_hwirq = d->parent_data->hwirq;
	enum pdc_irq_config_bits pdc_type;
	enum pdc_irq_config_bits old_pdc_type;
	int ret;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		pdc_type = PDC_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		pdc_type = PDC_EDGE_FALLING;
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		pdc_type = PDC_EDGE_DUAL;
		type = IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		pdc_type = PDC_LEVEL_HIGH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		pdc_type = PDC_LEVEL_LOW;
		type = IRQ_TYPE_LEVEL_HIGH;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	old_pdc_type = pdc_reg_read(IRQ_i_CFG, d->hwirq);
	pdc_type |= (old_pdc_type & ~IRQ_i_CFG_TYPE_MASK);
	pdc_reg_write(IRQ_i_CFG, d->hwirq, pdc_type);
	ipc_log_string(pdc_ipc_log, "Set type: PIN=%lu pdc_type=%d gic_type=%d",
		       d->hwirq, pdc_type, type);

	/* Additionally, configure (only) the GPIO in the f/w */
	ret = spi_configure_type(parent_hwirq, type);
	if (ret)
		return ret;

	ret = irq_chip_set_type_parent(d, type);
	if (ret)
		return ret;

	/*
	 * When we change types the PDC can give a phantom interrupt.
	 * Clear it.  Specifically the phantom shows up when reconfiguring
	 * polarity of interrupt without changing the state of the signal
	 * but let's be consistent and clear it always.
	 *
	 * Doing this works because we have IRQCHIP_SET_TYPE_MASKED so the
	 * interrupt will be cleared before the rest of the system sees it.
	 */
	if (old_pdc_type != pdc_type)
		irq_chip_set_parent_state(d, IRQCHIP_STATE_PENDING, false);

	return 0;
}

static struct irq_chip qcom_pdc_gic_chip = {
	.name			= "PDC",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= irq_chip_mask_parent,
	.irq_unmask		= irq_chip_unmask_parent,
	.irq_disable		= qcom_pdc_gic_disable,
	.irq_enable		= qcom_pdc_gic_enable,
	.irq_get_irqchip_state	= irq_chip_get_parent_state,
	.irq_set_irqchip_state	= irq_chip_set_parent_state,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= qcom_pdc_gic_set_type,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SET_TYPE_MASKED |
				  IRQCHIP_SKIP_SET_WAKE |
				  IRQCHIP_ENABLE_WAKEUP_ON_SUSPEND,
	.irq_set_vcpu_affinity	= irq_chip_set_vcpu_affinity_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
};

static struct pdc_pin_region *get_pin_region(int pin)
{
	int i;

	for (i = 0; i < pdc_region_cnt; i++) {
		if (pin >= pdc_region[i].pin_base &&
		    pin < pdc_region[i].pin_base + pdc_region[i].cnt)
			return &pdc_region[i];
	}

	return NULL;
}

static int qcom_pdc_alloc(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs, void *data)
{
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct pdc_pin_region *region;
	irq_hw_number_t hwirq;
	unsigned int type;
	int ret;

	ret = irq_domain_translate_twocell(domain, fwspec, &hwirq, &type);
	if (ret)
		return ret;

	if (hwirq == GPIO_NO_WAKE_IRQ)
		return irq_domain_disconnect_hierarchy(domain, virq);

	ret = irq_domain_set_hwirq_and_chip(domain, virq, hwirq,
					    &qcom_pdc_gic_chip, NULL);
	if (ret)
		return ret;

	region = get_pin_region(hwirq);
	if (!region)
		return irq_domain_disconnect_hierarchy(domain->parent, virq);

	if (type & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	if (type & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;

	parent_fwspec.fwnode      = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0]    = 0;
	parent_fwspec.param[1]    = pin_to_hwirq(region, hwirq);
	parent_fwspec.param[2]    = type;

	ipc_log_string(pdc_ipc_log, "Alloc: PIN=%lu", hwirq);
	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops qcom_pdc_ops = {
	.translate	= irq_domain_translate_twocell,
	.alloc		= qcom_pdc_alloc,
	.free		= irq_domain_free_irqs_common,
};

static int pdc_setup_pin_mapping(struct device_node *np)
{
	int ret, n, i;

	n = of_property_count_elems_of_size(np, "qcom,pdc-ranges", sizeof(u32));
	if (n <= 0 || n % 3)
		return -EINVAL;

	pdc_region_cnt = n / 3;
	pdc_region = kcalloc(pdc_region_cnt, sizeof(*pdc_region), GFP_KERNEL);
	if (!pdc_region) {
		pdc_region_cnt = 0;
		return -ENOMEM;
	}

	for (n = 0; n < pdc_region_cnt; n++) {
		ret = of_property_read_u32_index(np, "qcom,pdc-ranges",
						 n * 3 + 0,
						 &pdc_region[n].pin_base);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,pdc-ranges",
						 n * 3 + 1,
						 &pdc_region[n].parent_base);
		if (ret)
			return ret;
		ret = of_property_read_u32_index(np, "qcom,pdc-ranges",
						 n * 3 + 2,
						 &pdc_region[n].cnt);
		if (ret)
			return ret;

		for (i = 0; i < pdc_region[n].cnt; i++)
			__pdc_enable_intr(i + pdc_region[n].pin_base, 0);
	}

	return 0;
}

static int qcom_pdc_init(struct device_node *node, struct device_node *parent)
{
	struct irq_domain *parent_domain, *pdc_domain;
	struct resource res;
	int ret;
	u32 version, major_ver, minor_ver;

	pdc_base = of_iomap(node, 0);
	if (!pdc_base) {
		pr_err("%pOF: unable to map PDC registers\n", node);
		return -ENXIO;
	}

	version = pdc_reg_read(VERSION, 0);
	major_ver = version & (MAJOR_VER_MASK << MAJOR_VER_SHIFT);
	major_ver >>= MAJOR_VER_SHIFT;
	minor_ver = version & (MINOR_VER_MASK << MINOR_VER_SHIFT);
	minor_ver >>= MINOR_VER_SHIFT;
	if (major_ver >= 3 && minor_ver > 1)
		enable_in_cfg = true;
	else
		enable_in_cfg = false;

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		pr_err("%pOF: unable to find PDC's parent domain\n", node);
		ret = -ENXIO;
		goto fail;
	}

	ret = pdc_setup_pin_mapping(node);
	if (ret) {
		pr_err("%pOF: failed to init PDC pin-hwirq mapping\n", node);
		goto fail;
	}

	ret = of_address_to_resource(node, 1, &res);
	if (!ret) {
		spi_cfg = kcalloc(1, sizeof(*spi_cfg), GFP_KERNEL);
		if (!spi_cfg) {
			ret = -ENOMEM;
			goto fail;
		}
		spi_cfg->scm_io = of_find_property(node,
						   "qcom,scm-spi-cfg", NULL);
		spi_cfg->size = resource_size(&res);
		if (spi_cfg->scm_io) {
			spi_cfg->start = res.start;
		} else {
			spi_cfg->base = ioremap(res.start, spi_cfg->size);
			if (!spi_cfg->base) {
				ret = -ENOMEM;
				goto fail;
			}
		}
	}

	pdc_domain = irq_domain_create_hierarchy(parent_domain,
					IRQ_DOMAIN_FLAG_QCOM_PDC_WAKEUP,
					PDC_MAX_GPIO_IRQS,
					of_fwnode_handle(node),
					&qcom_pdc_ops, NULL);
	if (!pdc_domain) {
		pr_err("%pOF: PDC domain add failed\n", node);
		ret = -ENOMEM;
		if (spi_cfg && spi_cfg->base)
			iounmap(spi_cfg->base);
		goto fail;
	}

	irq_domain_update_bus_token(pdc_domain, DOMAIN_BUS_WAKEUP);

	pdc_ipc_log = ipc_log_context_create(PDC_IPC_LOG_SZ, "pdc", 0);
	return 0;

fail:
	if (spi_cfg)
		kfree(spi_cfg);
	if (pdc_region)
		kfree(pdc_region);
	iounmap(pdc_base);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(qcom_pdc)
IRQCHIP_MATCH("qcom,pdc", qcom_pdc_init)
IRQCHIP_PLATFORM_DRIVER_END(qcom_pdc)
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Power Domain Controller");
MODULE_LICENSE("GPL v2");
