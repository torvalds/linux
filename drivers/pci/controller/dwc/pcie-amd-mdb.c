// SPDX-License-Identifier: GPL-2.0
/*
 * PCIe host controller driver for AMD MDB PCIe Bridge
 *
 * Copyright (C) 2024-2025, Advanced Micro Devices, Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/resource.h>
#include <linux/types.h>

#include "../../pci.h"
#include "pcie-designware.h"

#define AMD_MDB_TLP_IR_STATUS_MISC		0x4C0
#define AMD_MDB_TLP_IR_MASK_MISC		0x4C4
#define AMD_MDB_TLP_IR_ENABLE_MISC		0x4C8
#define AMD_MDB_TLP_IR_DISABLE_MISC		0x4CC

#define AMD_MDB_TLP_PCIE_INTX_MASK	GENMASK(23, 16)

#define AMD_MDB_PCIE_INTR_INTX_ASSERT(x)	BIT((x) * 2)

/* Interrupt registers definitions. */
#define AMD_MDB_PCIE_INTR_CMPL_TIMEOUT		15
#define AMD_MDB_PCIE_INTR_INTX			16
#define AMD_MDB_PCIE_INTR_PM_PME_RCVD		24
#define AMD_MDB_PCIE_INTR_PME_TO_ACK_RCVD	25
#define AMD_MDB_PCIE_INTR_MISC_CORRECTABLE	26
#define AMD_MDB_PCIE_INTR_NONFATAL		27
#define AMD_MDB_PCIE_INTR_FATAL			28

#define IMR(x) BIT(AMD_MDB_PCIE_INTR_ ##x)
#define AMD_MDB_PCIE_IMR_ALL_MASK			\
	(						\
		IMR(CMPL_TIMEOUT)	|		\
		IMR(PM_PME_RCVD)	|		\
		IMR(PME_TO_ACK_RCVD)	|		\
		IMR(MISC_CORRECTABLE)	|		\
		IMR(NONFATAL)		|		\
		IMR(FATAL)		|		\
		AMD_MDB_TLP_PCIE_INTX_MASK		\
	)

/**
 * struct amd_mdb_pcie - PCIe port information
 * @pci: DesignWare PCIe controller structure
 * @slcr: MDB System Level Control and Status Register (SLCR) base
 * @intx_domain: INTx IRQ domain pointer
 * @mdb_domain: MDB IRQ domain pointer
 * @perst_gpio: GPIO descriptor for PERST# signal handling
 * @intx_irq: INTx IRQ interrupt number
 */
struct amd_mdb_pcie {
	struct dw_pcie			pci;
	void __iomem			*slcr;
	struct irq_domain		*intx_domain;
	struct irq_domain		*mdb_domain;
	struct gpio_desc		*perst_gpio;
	int				intx_irq;
};

static const struct dw_pcie_host_ops amd_mdb_pcie_host_ops = {
};

static void amd_mdb_intx_irq_mask(struct irq_data *data)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = FIELD_PREP(AMD_MDB_TLP_PCIE_INTX_MASK,
			 AMD_MDB_PCIE_INTR_INTX_ASSERT(data->hwirq));

	/*
	 * Writing '1' to a bit in AMD_MDB_TLP_IR_DISABLE_MISC disables that
	 * interrupt, writing '0' has no effect.
	 */
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_DISABLE_MISC);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void amd_mdb_intx_irq_unmask(struct irq_data *data)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(data);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = FIELD_PREP(AMD_MDB_TLP_PCIE_INTX_MASK,
			 AMD_MDB_PCIE_INTR_INTX_ASSERT(data->hwirq));

	/*
	 * Writing '1' to a bit in AMD_MDB_TLP_IR_ENABLE_MISC enables that
	 * interrupt, writing '0' has no effect.
	 */
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_ENABLE_MISC);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip amd_mdb_intx_irq_chip = {
	.name		= "AMD MDB INTx",
	.irq_mask	= amd_mdb_intx_irq_mask,
	.irq_unmask	= amd_mdb_intx_irq_unmask,
};

/**
 * amd_mdb_pcie_intx_map - Set the handler for the INTx and mark IRQ as valid
 * @domain: IRQ domain
 * @irq: Virtual IRQ number
 * @hwirq: Hardware interrupt number
 *
 * Return: Always returns '0'.
 */
static int amd_mdb_pcie_intx_map(struct irq_domain *domain,
				 unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &amd_mdb_intx_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

/* INTx IRQ domain operations. */
static const struct irq_domain_ops amd_intx_domain_ops = {
	.map = amd_mdb_pcie_intx_map,
};

static irqreturn_t dw_pcie_rp_intx(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	unsigned long val;
	int i, int_status;

	val = readl_relaxed(pcie->slcr + AMD_MDB_TLP_IR_STATUS_MISC);
	int_status = FIELD_GET(AMD_MDB_TLP_PCIE_INTX_MASK, val);

	for (i = 0; i < PCI_NUM_INTX; i++) {
		if (int_status & AMD_MDB_PCIE_INTR_INTX_ASSERT(i))
			generic_handle_domain_irq(pcie->intx_domain, i);
	}

	return IRQ_HANDLED;
}

#define _IC(x, s)[AMD_MDB_PCIE_INTR_ ## x] = { __stringify(x), s }

static const struct {
	const char	*sym;
	const char	*str;
} intr_cause[32] = {
	_IC(CMPL_TIMEOUT,	"Completion timeout"),
	_IC(PM_PME_RCVD,	"PM_PME message received"),
	_IC(PME_TO_ACK_RCVD,	"PME_TO_ACK message received"),
	_IC(MISC_CORRECTABLE,	"Correctable error message"),
	_IC(NONFATAL,		"Non fatal error message"),
	_IC(FATAL,		"Fatal error message"),
};

static void amd_mdb_event_irq_mask(struct irq_data *d)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = BIT(d->hwirq);
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_DISABLE_MISC);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static void amd_mdb_event_irq_unmask(struct irq_data *d)
{
	struct amd_mdb_pcie *pcie = irq_data_get_irq_chip_data(d);
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *port = &pci->pp;
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&port->lock, flags);
	val = BIT(d->hwirq);
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_ENABLE_MISC);
	raw_spin_unlock_irqrestore(&port->lock, flags);
}

static struct irq_chip amd_mdb_event_irq_chip = {
	.name		= "AMD MDB RC-Event",
	.irq_mask	= amd_mdb_event_irq_mask,
	.irq_unmask	= amd_mdb_event_irq_unmask,
};

static int amd_mdb_pcie_event_map(struct irq_domain *domain,
				  unsigned int irq, irq_hw_number_t hwirq)
{
	irq_set_chip_and_handler(irq, &amd_mdb_event_irq_chip,
				 handle_level_irq);
	irq_set_chip_data(irq, domain->host_data);
	irq_set_status_flags(irq, IRQ_LEVEL);

	return 0;
}

static const struct irq_domain_ops event_domain_ops = {
	.map = amd_mdb_pcie_event_map,
};

static irqreturn_t amd_mdb_pcie_event(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	unsigned long val;
	int i;

	val = readl_relaxed(pcie->slcr + AMD_MDB_TLP_IR_STATUS_MISC);
	val &= ~readl_relaxed(pcie->slcr + AMD_MDB_TLP_IR_MASK_MISC);
	for_each_set_bit(i, &val, 32)
		generic_handle_domain_irq(pcie->mdb_domain, i);
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_STATUS_MISC);

	return IRQ_HANDLED;
}

static void amd_mdb_pcie_free_irq_domains(struct amd_mdb_pcie *pcie)
{
	if (pcie->intx_domain) {
		irq_domain_remove(pcie->intx_domain);
		pcie->intx_domain = NULL;
	}

	if (pcie->mdb_domain) {
		irq_domain_remove(pcie->mdb_domain);
		pcie->mdb_domain = NULL;
	}
}

static int amd_mdb_pcie_init_port(struct amd_mdb_pcie *pcie)
{
	unsigned long val;

	/* Disable all TLP interrupts. */
	writel_relaxed(AMD_MDB_PCIE_IMR_ALL_MASK,
		       pcie->slcr + AMD_MDB_TLP_IR_DISABLE_MISC);

	/* Clear pending TLP interrupts. */
	val = readl_relaxed(pcie->slcr + AMD_MDB_TLP_IR_STATUS_MISC);
	val &= AMD_MDB_PCIE_IMR_ALL_MASK;
	writel_relaxed(val, pcie->slcr + AMD_MDB_TLP_IR_STATUS_MISC);

	/* Enable all TLP interrupts. */
	writel_relaxed(AMD_MDB_PCIE_IMR_ALL_MASK,
		       pcie->slcr + AMD_MDB_TLP_IR_ENABLE_MISC);

	return 0;
}

/**
 * amd_mdb_pcie_init_irq_domains - Initialize IRQ domain
 * @pcie: PCIe port information
 * @pdev: Platform device
 *
 * Return: Returns '0' on success and error value on failure.
 */
static int amd_mdb_pcie_init_irq_domains(struct amd_mdb_pcie *pcie,
					 struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;
	int err;

	pcie_intc_node = of_get_child_by_name(node, "interrupt-controller");
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	pcie->mdb_domain = irq_domain_create_linear(of_fwnode_handle(pcie_intc_node), 32,
						    &event_domain_ops, pcie);
	if (!pcie->mdb_domain) {
		err = -ENOMEM;
		dev_err(dev, "Failed to add MDB domain\n");
		goto out;
	}

	irq_domain_update_bus_token(pcie->mdb_domain, DOMAIN_BUS_NEXUS);

	pcie->intx_domain = irq_domain_create_linear(of_fwnode_handle(pcie_intc_node),
						     PCI_NUM_INTX, &amd_intx_domain_ops, pcie);
	if (!pcie->intx_domain) {
		err = -ENOMEM;
		dev_err(dev, "Failed to add INTx domain\n");
		goto mdb_out;
	}

	of_node_put(pcie_intc_node);
	irq_domain_update_bus_token(pcie->intx_domain, DOMAIN_BUS_WIRED);

	raw_spin_lock_init(&pp->lock);

	return 0;
mdb_out:
	amd_mdb_pcie_free_irq_domains(pcie);
out:
	of_node_put(pcie_intc_node);
	return err;
}

static irqreturn_t amd_mdb_pcie_intr_handler(int irq, void *args)
{
	struct amd_mdb_pcie *pcie = args;
	struct device *dev;
	struct irq_data *d;

	dev = pcie->pci.dev;

	/*
	 * In the future, error reporting will be hooked to the AER subsystem.
	 * Currently, the driver prints a warning message to the user.
	 */
	d = irq_domain_get_irq_data(pcie->mdb_domain, irq);
	if (intr_cause[d->hwirq].str)
		dev_warn(dev, "%s\n", intr_cause[d->hwirq].str);
	else
		dev_warn_once(dev, "Unknown IRQ %ld\n", d->hwirq);

	return IRQ_HANDLED;
}

static int amd_mdb_setup_irq(struct amd_mdb_pcie *pcie,
			     struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int i, irq, err;

	amd_mdb_pcie_init_port(pcie);

	pp->irq = platform_get_irq(pdev, 0);
	if (pp->irq < 0)
		return pp->irq;

	for (i = 0; i < ARRAY_SIZE(intr_cause); i++) {
		if (!intr_cause[i].str)
			continue;

		irq = irq_create_mapping(pcie->mdb_domain, i);
		if (!irq) {
			dev_err(dev, "Failed to map MDB domain interrupt\n");
			return -ENOMEM;
		}

		err = devm_request_irq(dev, irq, amd_mdb_pcie_intr_handler,
				       IRQF_NO_THREAD, intr_cause[i].sym, pcie);
		if (err) {
			dev_err(dev, "Failed to request IRQ %d, err=%d\n",
				irq, err);
			return err;
		}
	}

	pcie->intx_irq = irq_create_mapping(pcie->mdb_domain,
					    AMD_MDB_PCIE_INTR_INTX);
	if (!pcie->intx_irq) {
		dev_err(dev, "Failed to map INTx interrupt\n");
		return -ENXIO;
	}

	err = devm_request_irq(dev, pcie->intx_irq, dw_pcie_rp_intx,
			       IRQF_NO_THREAD, NULL, pcie);
	if (err) {
		dev_err(dev, "Failed to request INTx IRQ %d, err=%d\n",
			irq, err);
		return err;
	}

	/* Plug the main event handler. */
	err = devm_request_irq(dev, pp->irq, amd_mdb_pcie_event, IRQF_NO_THREAD,
			       "amd_mdb pcie_irq", pcie);
	if (err) {
		dev_err(dev, "Failed to request event IRQ %d, err=%d\n",
			pp->irq, err);
		return err;
	}

	return 0;
}

static int amd_mdb_parse_pcie_port(struct amd_mdb_pcie *pcie)
{
	struct device *dev = pcie->pci.dev;
	struct device_node *pcie_port_node __maybe_unused;

	/*
	 * This platform currently supports only one Root Port, so the loop
	 * will execute only once.
	 * TODO: Enhance the driver to handle multiple Root Ports in the future.
	 */
	for_each_child_of_node_with_prefix(dev->of_node, pcie_port_node, "pcie") {
		pcie->perst_gpio = devm_fwnode_gpiod_get(dev, of_fwnode_handle(pcie_port_node),
							 "reset", GPIOD_OUT_HIGH, NULL);
		if (IS_ERR(pcie->perst_gpio))
			return dev_err_probe(dev, PTR_ERR(pcie->perst_gpio),
					     "Failed to request reset GPIO\n");
		return 0;
	}

	return -ENODEV;
}

static int amd_mdb_add_pcie_port(struct amd_mdb_pcie *pcie,
				 struct platform_device *pdev)
{
	struct dw_pcie *pci = &pcie->pci;
	struct dw_pcie_rp *pp = &pci->pp;
	struct device *dev = &pdev->dev;
	int err;

	pcie->slcr = devm_platform_ioremap_resource_byname(pdev, "slcr");
	if (IS_ERR(pcie->slcr))
		return PTR_ERR(pcie->slcr);

	err = amd_mdb_pcie_init_irq_domains(pcie, pdev);
	if (err)
		return err;

	err = amd_mdb_setup_irq(pcie, pdev);
	if (err) {
		dev_err(dev, "Failed to set up interrupts, err=%d\n", err);
		goto out;
	}

	pp->ops = &amd_mdb_pcie_host_ops;

	if (pcie->perst_gpio) {
		mdelay(PCIE_T_PVPERL_MS);
		gpiod_set_value_cansleep(pcie->perst_gpio, 0);
		mdelay(PCIE_RESET_CONFIG_WAIT_MS);
	}

	err = dw_pcie_host_init(pp);
	if (err) {
		dev_err(dev, "Failed to initialize host, err=%d\n", err);
		goto out;
	}

	return 0;

out:
	amd_mdb_pcie_free_irq_domains(pcie);
	return err;
}

static int amd_mdb_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct amd_mdb_pcie *pcie;
	struct dw_pcie *pci;
	int ret;

	pcie = devm_kzalloc(dev, sizeof(*pcie), GFP_KERNEL);
	if (!pcie)
		return -ENOMEM;

	pci = &pcie->pci;
	pci->dev = dev;

	platform_set_drvdata(pdev, pcie);

	ret = amd_mdb_parse_pcie_port(pcie);
	/*
	 * If amd_mdb_parse_pcie_port returns -ENODEV, it indicates that the
	 * PCIe Bridge node was not found in the device tree. This is not
	 * considered a fatal error and will trigger a fallback where the
	 * reset GPIO is acquired directly from the PCIe Host Bridge node.
	 */
	if (ret) {
		if (ret != -ENODEV)
			return ret;

		pcie->perst_gpio = devm_gpiod_get_optional(dev, "reset",
							   GPIOD_OUT_HIGH);
		if (IS_ERR(pcie->perst_gpio))
			return dev_err_probe(dev, PTR_ERR(pcie->perst_gpio),
					     "Failed to request reset GPIO\n");
	}

	return amd_mdb_add_pcie_port(pcie, pdev);
}

static const struct of_device_id amd_mdb_pcie_of_match[] = {
	{
		.compatible = "amd,versal2-mdb-host",
	},
	{},
};

static struct platform_driver amd_mdb_pcie_driver = {
	.driver = {
		.name	= "amd-mdb-pcie",
		.of_match_table = amd_mdb_pcie_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = amd_mdb_pcie_probe,
};

builtin_platform_driver(amd_mdb_pcie_driver);
