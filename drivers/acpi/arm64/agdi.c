// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file implements handling of
 * Arm Generic Diagnostic Dump and Reset Interface table (AGDI)
 *
 * Copyright (c) 2022, Ampere Computing LLC
 */

#define pr_fmt(fmt) "ACPI: AGDI: " fmt

#include <linux/acpi.h>
#include <linux/arm_sdei.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include "init.h"

struct agdi_data {
	unsigned char flags; /* AGDI Signaling Mode */
	int sdei_event;
	unsigned int gsiv;
	bool use_nmi;
	int irq;
};

static int agdi_sdei_handler(u32 sdei_event, struct pt_regs *regs, void *arg)
{
	nmi_panic(regs, "Arm Generic Diagnostic Dump and Reset SDEI event issued");
	return 0;
}

static int agdi_sdei_probe(struct platform_device *pdev,
			   struct agdi_data *adata)
{
	int err;

	err = sdei_event_register(adata->sdei_event, agdi_sdei_handler, pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register for SDEI event %d",
			adata->sdei_event);
		return err;
	}

	err = sdei_event_enable(adata->sdei_event);
	if (err)  {
		sdei_event_unregister(adata->sdei_event);
		dev_err(&pdev->dev, "Failed to enable event %d\n",
			adata->sdei_event);
		return err;
	}

	return 0;
}

static irqreturn_t agdi_interrupt_handler_nmi(int irq, void *dev_id)
{
	nmi_panic(NULL, "Arm Generic Diagnostic Dump and Reset NMI Interrupt event issued\n");
	return IRQ_HANDLED;
}

static irqreturn_t agdi_interrupt_handler_irq(int irq, void *dev_id)
{
	panic("Arm Generic Diagnostic Dump and Reset Interrupt event issued\n");
	return IRQ_HANDLED;
}

static int agdi_interrupt_probe(struct platform_device *pdev,
				struct agdi_data *adata)
{
	unsigned long irq_flags;
	int ret;
	int irq;

	irq = acpi_register_gsi(NULL, adata->gsiv, ACPI_EDGE_SENSITIVE, ACPI_ACTIVE_HIGH);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot register GSI#%d (%d)\n", adata->gsiv, irq);
		return irq;
	}

	irq_flags = IRQF_PERCPU | IRQF_NOBALANCING | IRQF_NO_AUTOEN |
		    IRQF_NO_THREAD;
	/* try NMI first */
	ret = request_nmi(irq, &agdi_interrupt_handler_nmi, irq_flags,
			  "agdi_interrupt_nmi", NULL);
	if (!ret) {
		enable_nmi(irq);
		adata->irq = irq;
		adata->use_nmi = true;
		return 0;
	}

	/* Then try normal interrupt */
	ret = request_irq(irq, &agdi_interrupt_handler_irq,
			  irq_flags, "agdi_interrupt_irq", NULL);
	if (ret) {
		dev_err(&pdev->dev, "cannot register IRQ %d\n", ret);
		acpi_unregister_gsi(adata->gsiv);
		return ret;
	}
	enable_irq(irq);
	adata->irq = irq;

	return 0;
}

static int agdi_probe(struct platform_device *pdev)
{
	struct agdi_data *adata = dev_get_platdata(&pdev->dev);

	if (!adata)
		return -EINVAL;

	if (adata->flags & ACPI_AGDI_SIGNALING_MODE)
		return agdi_interrupt_probe(pdev, adata);
	else
		return agdi_sdei_probe(pdev, adata);
}

static void agdi_sdei_remove(struct platform_device *pdev,
			     struct agdi_data *adata)
{
	int err, i;

	err = sdei_event_disable(adata->sdei_event);
	if (err) {
		dev_err(&pdev->dev, "Failed to disable sdei-event #%d (%pe)\n",
			adata->sdei_event, ERR_PTR(err));
		return;
	}

	for (i = 0; i < 3; i++) {
		err = sdei_event_unregister(adata->sdei_event);
		if (err != -EINPROGRESS)
			break;

		schedule();
	}

	if (err)
		dev_err(&pdev->dev, "Failed to unregister sdei-event #%d (%pe)\n",
			adata->sdei_event, ERR_PTR(err));
}

static void agdi_interrupt_remove(struct platform_device *pdev,
				  struct agdi_data *adata)
{
	if (adata->irq == -1)
		return;

	if (adata->use_nmi)
		free_nmi(adata->irq, NULL);
	else
		free_irq(adata->irq, NULL);

	acpi_unregister_gsi(adata->gsiv);
}

static void agdi_remove(struct platform_device *pdev)
{
	struct agdi_data *adata = dev_get_platdata(&pdev->dev);

	if (adata->flags & ACPI_AGDI_SIGNALING_MODE)
		agdi_interrupt_remove(pdev, adata);
	else
		agdi_sdei_remove(pdev, adata);
}

static struct platform_driver agdi_driver = {
	.driver = {
		.name = "agdi",
	},
	.probe = agdi_probe,
	.remove = agdi_remove,
};

void __init acpi_agdi_init(void)
{
	struct acpi_table_agdi *agdi_table;
	struct agdi_data pdata = { 0 };
	struct platform_device *pdev;
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_AGDI, 0,
				(struct acpi_table_header **) &agdi_table);
	if (ACPI_FAILURE(status))
		return;

	if (agdi_table->flags & ACPI_AGDI_SIGNALING_MODE)
		pdata.gsiv = agdi_table->gsiv;
	else
		pdata.sdei_event = agdi_table->sdei_event;

	pdata.irq = -1;
	pdata.flags = agdi_table->flags;

	pdev = platform_device_register_data(NULL, "agdi", 0, &pdata, sizeof(pdata));
	if (IS_ERR(pdev))
		goto err_put_table;

	if (platform_driver_register(&agdi_driver))
		platform_device_unregister(pdev);

err_put_table:
	acpi_put_table((struct acpi_table_header *)agdi_table);
}
