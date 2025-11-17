// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/auxiliary_bus.h>

#include <rdma/ib_verbs.h>

#include "bng_re.h"
#include "bnge.h"
#include "bnge_auxr.h"

MODULE_AUTHOR("Siva Reddy Kallam <siva.kallam@broadcom.com>");
MODULE_DESCRIPTION(BNG_RE_DESC);
MODULE_LICENSE("Dual BSD/GPL");

static struct bng_re_dev *bng_re_dev_add(struct auxiliary_device *adev,
					 struct bnge_auxr_dev *aux_dev)
{
	struct bng_re_dev *rdev;

	/* Allocate bng_re_dev instance */
	rdev = ib_alloc_device(bng_re_dev, ibdev);
	if (!rdev) {
		pr_err("%s: bng_re_dev allocation failure!", KBUILD_MODNAME);
		return NULL;
	}

	/* Assign auxiliary device specific data */
	rdev->netdev = aux_dev->net;
	rdev->aux_dev = aux_dev;
	rdev->adev = adev;
	rdev->fn_id = rdev->aux_dev->pdev->devfn;

	return rdev;
}

static int bng_re_add_device(struct auxiliary_device *adev)
{
	struct bnge_auxr_priv *auxr_priv =
		container_of(adev, struct bnge_auxr_priv, aux_dev);
	struct bng_re_en_dev_info *dev_info;
	struct bng_re_dev *rdev;
	int rc;

	dev_info = auxiliary_get_drvdata(adev);

	rdev = bng_re_dev_add(adev, auxr_priv->auxr_dev);
	if (!rdev) {
		rc = -ENOMEM;
		goto exit;
	}

	dev_info->rdev = rdev;

	return 0;
exit:
	return rc;
}


static void bng_re_remove_device(struct bng_re_dev *rdev,
				 struct auxiliary_device *aux_dev)
{
	ib_dealloc_device(&rdev->ibdev);
}


static int bng_re_probe(struct auxiliary_device *adev,
			const struct auxiliary_device_id *id)
{
	struct bnge_auxr_priv *aux_priv =
		container_of(adev, struct bnge_auxr_priv, aux_dev);
	struct bng_re_en_dev_info *en_info;
	int rc;

	en_info = kzalloc(sizeof(*en_info), GFP_KERNEL);
	if (!en_info)
		return -ENOMEM;

	en_info->auxr_dev = aux_priv->auxr_dev;

	auxiliary_set_drvdata(adev, en_info);

	rc = bng_re_add_device(adev);
	if (rc)
		kfree(en_info);
	return rc;
}

static void bng_re_remove(struct auxiliary_device *adev)
{
	struct bng_re_en_dev_info *dev_info = auxiliary_get_drvdata(adev);
	struct bng_re_dev *rdev;

	rdev = dev_info->rdev;

	if (rdev)
		bng_re_remove_device(rdev, adev);
	kfree(dev_info);
}

static const struct auxiliary_device_id bng_re_id_table[] = {
	{ .name = BNG_RE_ADEV_NAME ".rdma", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, bng_re_id_table);

static struct auxiliary_driver bng_re_driver = {
	.name = "rdma",
	.probe = bng_re_probe,
	.remove = bng_re_remove,
	.id_table = bng_re_id_table,
};

static int __init bng_re_mod_init(void)
{
	int rc;


	rc = auxiliary_driver_register(&bng_re_driver);
	if (rc) {
		pr_err("%s: Failed to register auxiliary driver\n",
		       KBUILD_MODNAME);
	}
	return rc;
}

static void __exit bng_re_mod_exit(void)
{
	auxiliary_driver_unregister(&bng_re_driver);
}

module_init(bng_re_mod_init);
module_exit(bng_re_mod_exit);
