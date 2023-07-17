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
	int sdei_event;
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

static int agdi_probe(struct platform_device *pdev)
{
	struct agdi_data *adata = dev_get_platdata(&pdev->dev);

	if (!adata)
		return -EINVAL;

	return agdi_sdei_probe(pdev, adata);
}

static int agdi_remove(struct platform_device *pdev)
{
	struct agdi_data *adata = dev_get_platdata(&pdev->dev);
	int err, i;

	err = sdei_event_disable(adata->sdei_event);
	if (err) {
		dev_err(&pdev->dev, "Failed to disable sdei-event #%d (%pe)\n",
			adata->sdei_event, ERR_PTR(err));
		return 0;
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

	return 0;
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
	struct agdi_data pdata;
	struct platform_device *pdev;
	acpi_status status;

	status = acpi_get_table(ACPI_SIG_AGDI, 0,
				(struct acpi_table_header **) &agdi_table);
	if (ACPI_FAILURE(status))
		return;

	if (agdi_table->flags & ACPI_AGDI_SIGNALING_MODE) {
		pr_warn("Interrupt signaling is not supported");
		goto err_put_table;
	}

	pdata.sdei_event = agdi_table->sdei_event;

	pdev = platform_device_register_data(NULL, "agdi", 0, &pdata, sizeof(pdata));
	if (IS_ERR(pdev))
		goto err_put_table;

	if (platform_driver_register(&agdi_driver))
		platform_device_unregister(pdev);

err_put_table:
	acpi_put_table((struct acpi_table_header *)agdi_table);
}
