// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, Linaro Limited
 * Copyright (c) 2010-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/soc/qcom/irq.h>
#include <linux/spinlock.h>

/*
 * This is the driver for Qualcomm MPM (MSM Power Manager) interrupt controller,
 * which is commonly found on Qualcomm SoCs built on the RPM architecture.
 * Sitting in always-on domain, MPM monitors the wakeup interrupts when SoC is
 * asleep, and wakes up the AP when one of those interrupts occurs.  This driver
 * doesn't directly access physical MPM registers though.  Instead, the access
 * is bridged via a piece of internal memory (SRAM) that is accessible to both
 * AP and RPM.  This piece of memory is called 'vMPM' in the driver.
 *
 * When SoC is awake, the vMPM is owned by AP and the register setup by this
 * driver all happens on vMPM.  When AP is about to get power collapsed, the
 * driver sends a mailbox notification to RPM, which will take over the vMPM
 * ownership and dump vMPM into physical MPM registers.  On wakeup, AP is woken
 * up by a MPM pin/interrupt, and RPM will copy STATUS registers into vMPM.
 * Then AP start owning vMPM again.
 *
 * vMPM register map:
 *
 *    31                              0
 *    +--------------------------------+
 *    |            TIMER0              | 0x00
 *    +--------------------------------+
 *    |            TIMER1              | 0x04
 *    +--------------------------------+
 *    |            ENABLE0             | 0x08
 *    +--------------------------------+
 *    |              ...               | ...
 *    +--------------------------------+
 *    |            ENABLEn             |
 *    +--------------------------------+
 *    |          FALLING_EDGE0         |
 *    +--------------------------------+
 *    |              ...               |
 *    +--------------------------------+
 *    |            STATUSn             |
 *    +--------------------------------+
 *
 *    n = DIV_ROUND_UP(pin_cnt, 32)
 *
 */

#define MPM_REG_ENABLE		0
#define MPM_REG_FALLING_EDGE	1
#define MPM_REG_RISING_EDGE	2
#define MPM_REG_POLARITY	3
#define MPM_REG_STATUS		4

/* MPM pin map to GIC hwirq */
struct mpm_gic_map {
	int pin;
	irq_hw_number_t hwirq;
};

struct qcom_mpm_priv {
	void __iomem *base;
	raw_spinlock_t lock;
	struct mbox_client mbox_client;
	struct mbox_chan *mbox_chan;
	struct mpm_gic_map *maps;
	unsigned int map_cnt;
	unsigned int reg_stride;
	struct irq_domain *domain;
	struct generic_pm_domain genpd;
};

static u32 qcom_mpm_read(struct qcom_mpm_priv *priv, unsigned int reg,
			 unsigned int index)
{
	unsigned int offset = (reg * priv->reg_stride + index + 2) * 4;

	return readl_relaxed(priv->base + offset);
}

static void qcom_mpm_write(struct qcom_mpm_priv *priv, unsigned int reg,
			   unsigned int index, u32 val)
{
	unsigned int offset = (reg * priv->reg_stride + index + 2) * 4;

	writel_relaxed(val, priv->base + offset);

	/* Ensure the write is completed */
	wmb();
}

static void qcom_mpm_enable_irq(struct irq_data *d, bool en)
{
	struct qcom_mpm_priv *priv = d->chip_data;
	int pin = d->hwirq;
	unsigned int index = pin / 32;
	unsigned int shift = pin % 32;
	unsigned long flags, val;

	raw_spin_lock_irqsave(&priv->lock, flags);

	val = qcom_mpm_read(priv, MPM_REG_ENABLE, index);
	__assign_bit(shift, &val, en);
	qcom_mpm_write(priv, MPM_REG_ENABLE, index, val);

	raw_spin_unlock_irqrestore(&priv->lock, flags);
}

static void qcom_mpm_mask(struct irq_data *d)
{
	qcom_mpm_enable_irq(d, false);

	if (d->parent_data)
		irq_chip_mask_parent(d);
}

static void qcom_mpm_unmask(struct irq_data *d)
{
	qcom_mpm_enable_irq(d, true);

	if (d->parent_data)
		irq_chip_unmask_parent(d);
}

static void mpm_set_type(struct qcom_mpm_priv *priv, bool set, unsigned int reg,
			 unsigned int index, unsigned int shift)
{
	unsigned long flags, val;

	raw_spin_lock_irqsave(&priv->lock, flags);

	val = qcom_mpm_read(priv, reg, index);
	__assign_bit(shift, &val, set);
	qcom_mpm_write(priv, reg, index, val);

	raw_spin_unlock_irqrestore(&priv->lock, flags);
}

static int qcom_mpm_set_type(struct irq_data *d, unsigned int type)
{
	struct qcom_mpm_priv *priv = d->chip_data;
	int pin = d->hwirq;
	unsigned int index = pin / 32;
	unsigned int shift = pin % 32;

	if (type & IRQ_TYPE_EDGE_RISING)
		mpm_set_type(priv, true, MPM_REG_RISING_EDGE, index, shift);
	else
		mpm_set_type(priv, false, MPM_REG_RISING_EDGE, index, shift);

	if (type & IRQ_TYPE_EDGE_FALLING)
		mpm_set_type(priv, true, MPM_REG_FALLING_EDGE, index, shift);
	else
		mpm_set_type(priv, false, MPM_REG_FALLING_EDGE, index, shift);

	if (type & IRQ_TYPE_LEVEL_HIGH)
		mpm_set_type(priv, true, MPM_REG_POLARITY, index, shift);
	else
		mpm_set_type(priv, false, MPM_REG_POLARITY, index, shift);

	if (!d->parent_data)
		return 0;

	if (type & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	if (type & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;

	return irq_chip_set_type_parent(d, type);
}

static struct irq_chip qcom_mpm_chip = {
	.name			= "mpm",
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_mask		= qcom_mpm_mask,
	.irq_unmask		= qcom_mpm_unmask,
	.irq_retrigger		= irq_chip_retrigger_hierarchy,
	.irq_set_type		= qcom_mpm_set_type,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.flags			= IRQCHIP_MASK_ON_SUSPEND |
				  IRQCHIP_SKIP_SET_WAKE,
};

static struct mpm_gic_map *get_mpm_gic_map(struct qcom_mpm_priv *priv, int pin)
{
	struct mpm_gic_map *maps = priv->maps;
	int i;

	for (i = 0; i < priv->map_cnt; i++) {
		if (maps[i].pin == pin)
			return &maps[i];
	}

	return NULL;
}

static int qcom_mpm_alloc(struct irq_domain *domain, unsigned int virq,
			  unsigned int nr_irqs, void *data)
{
	struct qcom_mpm_priv *priv = domain->host_data;
	struct irq_fwspec *fwspec = data;
	struct irq_fwspec parent_fwspec;
	struct mpm_gic_map *map;
	irq_hw_number_t pin;
	unsigned int type;
	int  ret;

	ret = irq_domain_translate_twocell(domain, fwspec, &pin, &type);
	if (ret)
		return ret;

	ret = irq_domain_set_hwirq_and_chip(domain, virq, pin,
					    &qcom_mpm_chip, priv);
	if (ret)
		return ret;

	map = get_mpm_gic_map(priv, pin);
	if (map == NULL)
		return irq_domain_disconnect_hierarchy(domain->parent, virq);

	if (type & IRQ_TYPE_EDGE_BOTH)
		type = IRQ_TYPE_EDGE_RISING;

	if (type & IRQ_TYPE_LEVEL_MASK)
		type = IRQ_TYPE_LEVEL_HIGH;

	parent_fwspec.fwnode = domain->parent->fwnode;
	parent_fwspec.param_count = 3;
	parent_fwspec.param[0] = 0;
	parent_fwspec.param[1] = map->hwirq;
	parent_fwspec.param[2] = type;

	return irq_domain_alloc_irqs_parent(domain, virq, nr_irqs,
					    &parent_fwspec);
}

static const struct irq_domain_ops qcom_mpm_ops = {
	.alloc		= qcom_mpm_alloc,
	.free		= irq_domain_free_irqs_common,
	.translate	= irq_domain_translate_twocell,
};

/* Triggered by RPM when system resumes from deep sleep */
static irqreturn_t qcom_mpm_handler(int irq, void *dev_id)
{
	struct qcom_mpm_priv *priv = dev_id;
	unsigned long enable, pending;
	irqreturn_t ret = IRQ_NONE;
	unsigned long flags;
	int i, j;

	for (i = 0; i < priv->reg_stride; i++) {
		raw_spin_lock_irqsave(&priv->lock, flags);
		enable = qcom_mpm_read(priv, MPM_REG_ENABLE, i);
		pending = qcom_mpm_read(priv, MPM_REG_STATUS, i);
		pending &= enable;
		raw_spin_unlock_irqrestore(&priv->lock, flags);

		for_each_set_bit(j, &pending, 32) {
			unsigned int pin = 32 * i + j;
			struct irq_desc *desc = irq_resolve_mapping(priv->domain, pin);
			struct irq_data *d = &desc->irq_data;

			if (!irqd_is_level_type(d))
				irq_set_irqchip_state(d->irq,
						IRQCHIP_STATE_PENDING, true);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

static int mpm_pd_power_off(struct generic_pm_domain *genpd)
{
	struct qcom_mpm_priv *priv = container_of(genpd, struct qcom_mpm_priv,
						  genpd);
	int i, ret;

	for (i = 0; i < priv->reg_stride; i++)
		qcom_mpm_write(priv, MPM_REG_STATUS, i, 0);

	/* Notify RPM to write vMPM into HW */
	ret = mbox_send_message(priv->mbox_chan, NULL);
	if (ret < 0)
		return ret;

	return 0;
}

static bool gic_hwirq_is_mapped(struct mpm_gic_map *maps, int cnt, u32 hwirq)
{
	int i;

	for (i = 0; i < cnt; i++)
		if (maps[i].hwirq == hwirq)
			return true;

	return false;
}

static int qcom_mpm_init(struct device_node *np, struct device_node *parent)
{
	struct platform_device *pdev = of_find_device_by_node(np);
	struct device *dev = &pdev->dev;
	struct irq_domain *parent_domain;
	struct generic_pm_domain *genpd;
	struct qcom_mpm_priv *priv;
	unsigned int pin_cnt;
	int i, irq;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = of_property_read_u32(np, "qcom,mpm-pin-count", &pin_cnt);
	if (ret) {
		dev_err(dev, "failed to read qcom,mpm-pin-count: %d\n", ret);
		return ret;
	}

	priv->reg_stride = DIV_ROUND_UP(pin_cnt, 32);

	ret = of_property_count_u32_elems(np, "qcom,mpm-pin-map");
	if (ret < 0) {
		dev_err(dev, "failed to read qcom,mpm-pin-map: %d\n", ret);
		return ret;
	}

	if (ret % 2) {
		dev_err(dev, "invalid qcom,mpm-pin-map\n");
		return -EINVAL;
	}

	priv->map_cnt = ret / 2;
	priv->maps = devm_kcalloc(dev, priv->map_cnt, sizeof(*priv->maps),
				  GFP_KERNEL);
	if (!priv->maps)
		return -ENOMEM;

	for (i = 0; i < priv->map_cnt; i++) {
		u32 pin, hwirq;

		of_property_read_u32_index(np, "qcom,mpm-pin-map", i * 2, &pin);
		of_property_read_u32_index(np, "qcom,mpm-pin-map", i * 2 + 1, &hwirq);

		if (gic_hwirq_is_mapped(priv->maps, i, hwirq)) {
			dev_warn(dev, "failed to map pin %d as GIC hwirq %d is already mapped\n",
				 pin, hwirq);
			continue;
		}

		priv->maps[i].pin = pin;
		priv->maps[i].hwirq = hwirq;
	}

	raw_spin_lock_init(&priv->lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	for (i = 0; i < priv->reg_stride; i++) {
		qcom_mpm_write(priv, MPM_REG_ENABLE, i, 0);
		qcom_mpm_write(priv, MPM_REG_FALLING_EDGE, i, 0);
		qcom_mpm_write(priv, MPM_REG_RISING_EDGE, i, 0);
		qcom_mpm_write(priv, MPM_REG_POLARITY, i, 0);
		qcom_mpm_write(priv, MPM_REG_STATUS, i, 0);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	genpd = &priv->genpd;
	genpd->flags = GENPD_FLAG_IRQ_SAFE;
	genpd->power_off = mpm_pd_power_off;

	genpd->name = devm_kasprintf(dev, GFP_KERNEL, "%s", dev_name(dev));
	if (!genpd->name)
		return -ENOMEM;

	ret = pm_genpd_init(genpd, NULL, false);
	if (ret) {
		dev_err(dev, "failed to init genpd: %d\n", ret);
		return ret;
	}

	ret = of_genpd_add_provider_simple(np, genpd);
	if (ret) {
		dev_err(dev, "failed to add genpd provider: %d\n", ret);
		goto remove_genpd;
	}

	priv->mbox_client.dev = dev;
	priv->mbox_chan = mbox_request_channel(&priv->mbox_client, 0);
	if (IS_ERR(priv->mbox_chan)) {
		ret = PTR_ERR(priv->mbox_chan);
		dev_err(dev, "failed to acquire IPC channel: %d\n", ret);
		return ret;
	}

	parent_domain = irq_find_host(parent);
	if (!parent_domain) {
		dev_err(dev, "failed to find MPM parent domain\n");
		ret = -ENXIO;
		goto free_mbox;
	}

	priv->domain = irq_domain_create_hierarchy(parent_domain,
				IRQ_DOMAIN_FLAG_QCOM_MPM_WAKEUP, pin_cnt,
				of_node_to_fwnode(np), &qcom_mpm_ops, priv);
	if (!priv->domain) {
		dev_err(dev, "failed to create MPM domain\n");
		ret = -ENOMEM;
		goto free_mbox;
	}

	irq_domain_update_bus_token(priv->domain, DOMAIN_BUS_WAKEUP);

	ret = devm_request_irq(dev, irq, qcom_mpm_handler, IRQF_NO_SUSPEND,
			       "qcom_mpm", priv);
	if (ret) {
		dev_err(dev, "failed to request irq: %d\n", ret);
		goto remove_domain;
	}

	return 0;

remove_domain:
	irq_domain_remove(priv->domain);
free_mbox:
	mbox_free_channel(priv->mbox_chan);
remove_genpd:
	pm_genpd_remove(genpd);
	return ret;
}

IRQCHIP_PLATFORM_DRIVER_BEGIN(qcom_mpm)
IRQCHIP_MATCH("qcom,mpm", qcom_mpm_init)
IRQCHIP_PLATFORM_DRIVER_END(qcom_mpm)
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. MSM Power Manager");
MODULE_LICENSE("GPL v2");
