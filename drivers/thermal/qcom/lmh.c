// SPDX-License-Identifier: GPL-2.0-only

/*
 * Copyright (C) 2021, Linaro Limited. All rights reserved.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/firmware/qcom/qcom_scm.h>

#define LMH_NODE_DCVS			0x44435653
#define LMH_CLUSTER0_NODE_ID		0x6370302D
#define LMH_CLUSTER1_NODE_ID		0x6370312D

#define LMH_SUB_FN_THERMAL		0x54484D4C
#define LMH_SUB_FN_CRNT			0x43524E54
#define LMH_SUB_FN_REL			0x52454C00
#define LMH_SUB_FN_BCL			0x42434C00

#define LMH_ALGO_MODE_ENABLE		0x454E424C
#define LMH_TH_HI_THRESHOLD		0x48494748
#define LMH_TH_LOW_THRESHOLD		0x4C4F5700
#define LMH_TH_ARM_THRESHOLD		0x41524D00

#define LMH_REG_DCVS_INTR_CLR		0x8

#define LMH_ENABLE_ALGOS		1

struct lmh_hw_data {
	void __iomem *base;
	struct irq_domain *domain;
	int irq;
};

static irqreturn_t lmh_handle_irq(int hw_irq, void *data)
{
	struct lmh_hw_data *lmh_data = data;
	int irq = irq_find_mapping(lmh_data->domain, 0);

	/* Call the cpufreq driver to handle the interrupt */
	if (irq)
		generic_handle_irq(irq);

	return IRQ_HANDLED;
}

static void lmh_enable_interrupt(struct irq_data *d)
{
	struct lmh_hw_data *lmh_data = irq_data_get_irq_chip_data(d);

	/* Clear the existing interrupt */
	writel(0xff, lmh_data->base + LMH_REG_DCVS_INTR_CLR);
	enable_irq(lmh_data->irq);
}

static void lmh_disable_interrupt(struct irq_data *d)
{
	struct lmh_hw_data *lmh_data = irq_data_get_irq_chip_data(d);

	disable_irq_nosync(lmh_data->irq);
}

static struct irq_chip lmh_irq_chip = {
	.name           = "lmh",
	.irq_enable	= lmh_enable_interrupt,
	.irq_disable	= lmh_disable_interrupt
};

static int lmh_irq_map(struct irq_domain *d, unsigned int irq, irq_hw_number_t hw)
{
	struct lmh_hw_data *lmh_data = d->host_data;

	irq_set_chip_and_handler(irq, &lmh_irq_chip, handle_simple_irq);
	irq_set_chip_data(irq, lmh_data);

	return 0;
}

static const struct irq_domain_ops lmh_irq_ops = {
	.map = lmh_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int lmh_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *cpu_node;
	struct lmh_hw_data *lmh_data;
	int temp_low, temp_high, temp_arm, cpu_id, ret;
	unsigned int enable_alg;
	u32 node_id;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	lmh_data = devm_kzalloc(dev, sizeof(*lmh_data), GFP_KERNEL);
	if (!lmh_data)
		return -ENOMEM;

	lmh_data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lmh_data->base))
		return PTR_ERR(lmh_data->base);

	cpu_node = of_parse_phandle(np, "cpus", 0);
	if (!cpu_node)
		return -EINVAL;
	cpu_id = of_cpu_node_to_id(cpu_node);
	of_node_put(cpu_node);

	ret = of_property_read_u32(np, "qcom,lmh-temp-high-millicelsius", &temp_high);
	if (ret) {
		dev_err(dev, "missing qcom,lmh-temp-high-millicelsius property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "qcom,lmh-temp-low-millicelsius", &temp_low);
	if (ret) {
		dev_err(dev, "missing qcom,lmh-temp-low-millicelsius property\n");
		return ret;
	}

	ret = of_property_read_u32(np, "qcom,lmh-temp-arm-millicelsius", &temp_arm);
	if (ret) {
		dev_err(dev, "missing qcom,lmh-temp-arm-millicelsius property\n");
		return ret;
	}

	/*
	 * Only sdm845 has lmh hardware currently enabled from hlos. If this is needed
	 * for other platforms, revisit this to check if the <cpu-id, node-id> should be part
	 * of a dt match table.
	 */
	if (cpu_id == 0) {
		node_id = LMH_CLUSTER0_NODE_ID;
	} else if (cpu_id == 4) {
		node_id = LMH_CLUSTER1_NODE_ID;
	} else {
		dev_err(dev, "Wrong CPU id associated with LMh node\n");
		return -EINVAL;
	}

	if (!qcom_scm_lmh_dcvsh_available())
		return -EINVAL;

	enable_alg = (uintptr_t)of_device_get_match_data(dev);

	if (enable_alg) {
		ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_CRNT, LMH_ALGO_MODE_ENABLE, 1,
					 LMH_NODE_DCVS, node_id, 0);
		if (ret)
			dev_err(dev, "Error %d enabling current subfunction\n", ret);

		ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_REL, LMH_ALGO_MODE_ENABLE, 1,
					 LMH_NODE_DCVS, node_id, 0);
		if (ret)
			dev_err(dev, "Error %d enabling reliability subfunction\n", ret);

		ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_BCL, LMH_ALGO_MODE_ENABLE, 1,
					 LMH_NODE_DCVS, node_id, 0);
		if (ret)
			dev_err(dev, "Error %d enabling BCL subfunction\n", ret);

		ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_THERMAL, LMH_ALGO_MODE_ENABLE, 1,
					 LMH_NODE_DCVS, node_id, 0);
		if (ret) {
			dev_err(dev, "Error %d enabling thermal subfunction\n", ret);
			return ret;
		}

		ret = qcom_scm_lmh_profile_change(0x1);
		if (ret) {
			dev_err(dev, "Error %d changing profile\n", ret);
			return ret;
		}
	}

	/* Set default thermal trips */
	ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_THERMAL, LMH_TH_ARM_THRESHOLD, temp_arm,
				 LMH_NODE_DCVS, node_id, 0);
	if (ret) {
		dev_err(dev, "Error setting thermal ARM threshold%d\n", ret);
		return ret;
	}

	ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_THERMAL, LMH_TH_HI_THRESHOLD, temp_high,
				 LMH_NODE_DCVS, node_id, 0);
	if (ret) {
		dev_err(dev, "Error setting thermal HI threshold%d\n", ret);
		return ret;
	}

	ret = qcom_scm_lmh_dcvsh(LMH_SUB_FN_THERMAL, LMH_TH_LOW_THRESHOLD, temp_low,
				 LMH_NODE_DCVS, node_id, 0);
	if (ret) {
		dev_err(dev, "Error setting thermal ARM threshold%d\n", ret);
		return ret;
	}

	lmh_data->irq = platform_get_irq(pdev, 0);
	lmh_data->domain = irq_domain_add_linear(np, 1, &lmh_irq_ops, lmh_data);
	if (!lmh_data->domain) {
		dev_err(dev, "Error adding irq_domain\n");
		return -EINVAL;
	}

	/* Disable the irq and let cpufreq enable it when ready to handle the interrupt */
	irq_set_status_flags(lmh_data->irq, IRQ_NOAUTOEN);
	ret = devm_request_irq(dev, lmh_data->irq, lmh_handle_irq,
			       IRQF_ONESHOT | IRQF_NO_SUSPEND,
			       "lmh-irq", lmh_data);
	if (ret) {
		dev_err(dev, "Error %d registering irq %x\n", ret, lmh_data->irq);
		irq_domain_remove(lmh_data->domain);
		return ret;
	}

	return 0;
}

static const struct of_device_id lmh_table[] = {
	{ .compatible = "qcom,sc8180x-lmh", },
	{ .compatible = "qcom,sdm845-lmh", .data = (void *)LMH_ENABLE_ALGOS},
	{ .compatible = "qcom,sm8150-lmh", },
	{}
};
MODULE_DEVICE_TABLE(of, lmh_table);

static struct platform_driver lmh_driver = {
	.probe = lmh_probe,
	.driver = {
		.name = "qcom-lmh",
		.of_match_table = lmh_table,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(lmh_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("QCOM LMh driver");
