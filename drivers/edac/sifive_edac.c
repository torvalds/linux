// SPDX-License-Identifier: GPL-2.0
/*
 * SiFive Platform EDAC Driver
 *
 * Copyright (C) 2018-2019 SiFive, Inc.
 *
 * This driver is partially based on octeon_edac-pc.c
 *
 */
#include <linux/edac.h>
#include <linux/platform_device.h>
#include "edac_module.h"
#include <asm/sifive_l2_cache.h>

#define DRVNAME "sifive_edac"

struct sifive_edac_priv {
	struct notifier_block notifier;
	struct edac_device_ctl_info *dci;
};

/**
 * EDAC error callback
 *
 * @event: non-zero if unrecoverable.
 */
static
int ecc_err_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	const char *msg = (char *)ptr;
	struct sifive_edac_priv *p;

	p = container_of(this, struct sifive_edac_priv, notifier);

	if (event == SIFIVE_L2_ERR_TYPE_UE)
		edac_device_handle_ue(p->dci, 0, 0, msg);
	else if (event == SIFIVE_L2_ERR_TYPE_CE)
		edac_device_handle_ce(p->dci, 0, 0, msg);

	return NOTIFY_OK;
}

static int ecc_register(struct platform_device *pdev)
{
	struct sifive_edac_priv *p;

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->notifier.notifier_call = ecc_err_event;
	platform_set_drvdata(pdev, p);

	p->dci = edac_device_alloc_ctl_info(0, "sifive_ecc", 1, "sifive_ecc",
					    1, 1, NULL, 0,
					    edac_device_alloc_index());
	if (!p->dci)
		return -ENOMEM;

	p->dci->dev = &pdev->dev;
	p->dci->mod_name = "Sifive ECC Manager";
	p->dci->ctl_name = dev_name(&pdev->dev);
	p->dci->dev_name = dev_name(&pdev->dev);

	if (edac_device_add_device(p->dci)) {
		dev_err(p->dci->dev, "failed to register with EDAC core\n");
		goto err;
	}

	register_sifive_l2_error_notifier(&p->notifier);

	return 0;

err:
	edac_device_free_ctl_info(p->dci);

	return -ENXIO;
}

static int ecc_unregister(struct platform_device *pdev)
{
	struct sifive_edac_priv *p = platform_get_drvdata(pdev);

	unregister_sifive_l2_error_notifier(&p->notifier);
	edac_device_del_device(&pdev->dev);
	edac_device_free_ctl_info(p->dci);

	return 0;
}

static struct platform_device *sifive_pdev;

static int __init sifive_edac_init(void)
{
	int ret;

	sifive_pdev = platform_device_register_simple(DRVNAME, 0, NULL, 0);
	if (IS_ERR(sifive_pdev))
		return PTR_ERR(sifive_pdev);

	ret = ecc_register(sifive_pdev);
	if (ret)
		platform_device_unregister(sifive_pdev);

	return ret;
}

static void __exit sifive_edac_exit(void)
{
	ecc_unregister(sifive_pdev);
	platform_device_unregister(sifive_pdev);
}

module_init(sifive_edac_init);
module_exit(sifive_edac_exit);

MODULE_AUTHOR("SiFive Inc.");
MODULE_DESCRIPTION("SiFive platform EDAC driver");
MODULE_LICENSE("GPL v2");
