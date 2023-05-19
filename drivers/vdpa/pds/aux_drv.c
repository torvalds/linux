// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/auxiliary_bus.h>
#include <linux/pci.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "aux_drv.h"
#include "debugfs.h"

static const struct auxiliary_device_id pds_vdpa_id_table[] = {
	{ .name = PDS_VDPA_DEV_NAME, },
	{},
};

static int pds_vdpa_probe(struct auxiliary_device *aux_dev,
			  const struct auxiliary_device_id *id)

{
	struct pds_auxiliary_dev *padev =
		container_of(aux_dev, struct pds_auxiliary_dev, aux_dev);
	struct pds_vdpa_aux *vdpa_aux;

	vdpa_aux = kzalloc(sizeof(*vdpa_aux), GFP_KERNEL);
	if (!vdpa_aux)
		return -ENOMEM;

	vdpa_aux->padev = padev;
	auxiliary_set_drvdata(aux_dev, vdpa_aux);

	return 0;
}

static void pds_vdpa_remove(struct auxiliary_device *aux_dev)
{
	struct pds_vdpa_aux *vdpa_aux = auxiliary_get_drvdata(aux_dev);
	struct device *dev = &aux_dev->dev;

	kfree(vdpa_aux);
	auxiliary_set_drvdata(aux_dev, NULL);

	dev_info(dev, "Removed\n");
}

static struct auxiliary_driver pds_vdpa_driver = {
	.name = PDS_DEV_TYPE_VDPA_STR,
	.probe = pds_vdpa_probe,
	.remove = pds_vdpa_remove,
	.id_table = pds_vdpa_id_table,
};

static void __exit pds_vdpa_cleanup(void)
{
	auxiliary_driver_unregister(&pds_vdpa_driver);

	pds_vdpa_debugfs_destroy();
}
module_exit(pds_vdpa_cleanup);

static int __init pds_vdpa_init(void)
{
	int err;

	pds_vdpa_debugfs_create();

	err = auxiliary_driver_register(&pds_vdpa_driver);
	if (err) {
		pr_err("%s: aux driver register failed: %pe\n",
		       PDS_VDPA_DRV_NAME, ERR_PTR(err));
		pds_vdpa_debugfs_destroy();
	}

	return err;
}
module_init(pds_vdpa_init);

MODULE_DESCRIPTION(PDS_VDPA_DRV_DESCRIPTION);
MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_LICENSE("GPL");
