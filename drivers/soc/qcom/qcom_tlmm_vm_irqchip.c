// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/io.h>

#define TLMM_VM_NUM_IRQ 10

struct qcom_tlmm_vm_irqchip_data {
	struct irq_domain       *domain;
};

static struct qcom_tlmm_vm_irqchip_data qcom_tlmm_vm_irqchip_data __read_mostly;
/*
 * Set simple handler and mark IRQ as valid. Nothing interesting to do here
 * since we are using a dummy interrupt chip.
 */
static int qcom_tlmm_vm_irqchip_domain(struct irq_domain *domain,
					unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &dummy_irq_chip, handle_simple_irq);

	return 0;
}

static const struct irq_domain_ops qcom_tlmm_vm_irqchip_domain_ops = {
	.map = qcom_tlmm_vm_irqchip_domain,
};

static int qcom_tlmm_vm_irqchip_probe(struct platform_device *pdev)
{

	qcom_tlmm_vm_irqchip_data.domain = irq_domain_add_linear(pdev->dev.of_node, TLMM_VM_NUM_IRQ,
						&qcom_tlmm_vm_irqchip_domain_ops,
						NULL);
	if (!qcom_tlmm_vm_irqchip_data.domain)
		return -ENOMEM;

	qcom_tlmm_vm_irqchip_data.domain->name = "qcom-tlmm-vm-irq-domain";

	pr_info("qcom tlmm vm irq controller registered\n");

	return 0;
}

static int qcom_tlmm_vm_irqchip_remove(struct platform_device *pdev)
{
	irq_domain_remove(qcom_tlmm_vm_irqchip_data.domain);

	return 0;
}

static const struct of_device_id qcom_tlmm_vm_irqchip_of_match[] = {
	{ .compatible = "qcom,tlmm-vm-irq"},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_tlmm_vm_irqchip_of_match);

static struct platform_driver qcom_tlmm_vm_irqchip_driver = {
	.probe = qcom_tlmm_vm_irqchip_probe,
	.remove = qcom_tlmm_vm_irqchip_remove,
	.driver = {
		.name = "qcom_tlmm_vm_irqchip",
	.of_match_table = qcom_tlmm_vm_irqchip_of_match,
	},
};

static int __init qcom_tlmm_vm_irqchip_init(void)
{
	int ret;

	ret = platform_driver_register(&qcom_tlmm_vm_irqchip_driver);
	if (ret)
		pr_err("%s: qcom_tlmm_vm_irqchip register failed %d\n", __func__, ret);
	return ret;
}
arch_initcall(qcom_tlmm_vm_irqchip_init);

static __exit void qcom_tlmm_vm_irqchip_exit(void)
{
	platform_driver_unregister(&qcom_tlmm_vm_irqchip_driver);
}
module_exit(qcom_tlmm_vm_irqchip_exit);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. TLMM VM Irqchip Driver");
MODULE_LICENSE("GPL");
