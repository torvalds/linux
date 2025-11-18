// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include "mlx5_ib.h"
#include "data_direct.h"

static LIST_HEAD(mlx5_data_direct_dev_list);
static LIST_HEAD(mlx5_data_direct_reg_list);

/*
 * This mutex should be held when accessing either of the above lists
 */
static DEFINE_MUTEX(mlx5_data_direct_mutex);

struct mlx5_data_direct_registration {
	struct mlx5_ib_dev *ibdev;
	char vuid[MLX5_ST_SZ_BYTES(array1024_auto) + 1];
	struct list_head list;
};

static const struct pci_device_id mlx5_data_direct_pci_table[] = {
	{ PCI_VDEVICE(MELLANOX, 0x2100) }, /* ConnectX-8 Data Direct */
	{ 0, }
};

static int mlx5_data_direct_vpd_get_vuid(struct mlx5_data_direct_dev *dev)
{
	struct pci_dev *pdev = dev->pdev;
	unsigned int vpd_size, kw_len;
	u8 *vpd_data;
	int start;
	int ret;

	vpd_data = pci_vpd_alloc(pdev, &vpd_size);
	if (IS_ERR(vpd_data)) {
		pci_err(pdev, "Unable to read VPD, err=%pe\n", vpd_data);
		return PTR_ERR(vpd_data);
	}

	start = pci_vpd_find_ro_info_keyword(vpd_data, vpd_size, "VU", &kw_len);
	if (start < 0) {
		ret = start;
		pci_err(pdev, "VU keyword not found, err=%d\n", ret);
		goto end;
	}

	dev->vuid = kmemdup_nul(vpd_data + start, kw_len, GFP_KERNEL);
	ret = dev->vuid ? 0 : -ENOMEM;

end:
	kfree(vpd_data);
	return ret;
}

static void mlx5_data_direct_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

static int mlx5_data_direct_set_dma_caps(struct pci_dev *pdev)
{
	int err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev,
			 "Warning: couldn't set 64-bit PCI DMA mask, err=%d\n", err);
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Can't set PCI DMA mask, err=%d\n", err);
			return err;
		}
	}

	dma_set_max_seg_size(&pdev->dev, SZ_2G);
	return 0;
}

int mlx5_data_direct_ib_reg(struct mlx5_ib_dev *ibdev, char *vuid)
{
	struct mlx5_data_direct_registration *reg;
	struct mlx5_data_direct_dev *dev;

	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg)
		return -ENOMEM;

	reg->ibdev = ibdev;
	strcpy(reg->vuid, vuid);

	mutex_lock(&mlx5_data_direct_mutex);
	list_for_each_entry(dev, &mlx5_data_direct_dev_list, list) {
		if (strcmp(dev->vuid, vuid) == 0) {
			mlx5_ib_data_direct_bind(ibdev, dev);
			break;
		}
	}

	/* Add the registration to its global list, to be used upon bind/unbind
	 * of its affiliated data direct device
	 */
	list_add_tail(&reg->list, &mlx5_data_direct_reg_list);
	mutex_unlock(&mlx5_data_direct_mutex);
	return 0;
}

void mlx5_data_direct_ib_unreg(struct mlx5_ib_dev *ibdev)
{
	struct mlx5_data_direct_registration *reg;

	mutex_lock(&mlx5_data_direct_mutex);
	list_for_each_entry(reg, &mlx5_data_direct_reg_list, list) {
		if (reg->ibdev == ibdev) {
			list_del(&reg->list);
			kfree(reg);
			goto end;
		}
	}

	WARN_ON(true);
end:
	mutex_unlock(&mlx5_data_direct_mutex);
}

static void mlx5_data_direct_dev_reg(struct mlx5_data_direct_dev *dev)
{
	struct mlx5_data_direct_registration *reg;

	mutex_lock(&mlx5_data_direct_mutex);
	list_for_each_entry(reg, &mlx5_data_direct_reg_list, list) {
		if (strcmp(dev->vuid, reg->vuid) == 0)
			mlx5_ib_data_direct_bind(reg->ibdev, dev);
	}

	/* Add the data direct device to the global list, further IB devices may
	 * use it later as well
	 */
	list_add_tail(&dev->list, &mlx5_data_direct_dev_list);
	mutex_unlock(&mlx5_data_direct_mutex);
}

static void mlx5_data_direct_dev_unreg(struct mlx5_data_direct_dev *dev)
{
	struct mlx5_data_direct_registration *reg;

	mutex_lock(&mlx5_data_direct_mutex);
	/* Prevent any further affiliations */
	list_del(&dev->list);
	list_for_each_entry(reg, &mlx5_data_direct_reg_list, list) {
		if (strcmp(dev->vuid, reg->vuid) == 0)
			mlx5_ib_data_direct_unbind(reg->ibdev);
	}
	mutex_unlock(&mlx5_data_direct_mutex);
}

static int mlx5_data_direct_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mlx5_data_direct_dev *dev;
	int err;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->device = &pdev->dev;
	dev->pdev = pdev;

	pci_set_drvdata(dev->pdev, dev);
	err = pci_enable_device(pdev);
	if (err) {
		dev_err(dev->device, "Cannot enable PCI device, err=%d\n", err);
		goto err;
	}

	pci_set_master(pdev);
	err = mlx5_data_direct_set_dma_caps(pdev);
	if (err)
		goto err_disable;

	if (pci_enable_atomic_ops_to_root(pdev, PCI_EXP_DEVCAP2_ATOMIC_COMP32) &&
	    pci_enable_atomic_ops_to_root(pdev, PCI_EXP_DEVCAP2_ATOMIC_COMP64) &&
	    pci_enable_atomic_ops_to_root(pdev, PCI_EXP_DEVCAP2_ATOMIC_COMP128))
		dev_dbg(dev->device, "Enabling pci atomics failed\n");

	err = mlx5_data_direct_vpd_get_vuid(dev);
	if (err)
		goto err_disable;

	mlx5_data_direct_dev_reg(dev);
	return 0;

err_disable:
	pci_disable_device(pdev);
err:
	kfree(dev);
	return err;
}

static void mlx5_data_direct_remove(struct pci_dev *pdev)
{
	struct mlx5_data_direct_dev *dev = pci_get_drvdata(pdev);

	mlx5_data_direct_dev_unreg(dev);
	pci_disable_device(pdev);
	kfree(dev->vuid);
	kfree(dev);
}

static struct pci_driver mlx5_data_direct_driver = {
	.name = KBUILD_MODNAME,
	.id_table = mlx5_data_direct_pci_table,
	.probe = mlx5_data_direct_probe,
	.remove = mlx5_data_direct_remove,
	.shutdown = mlx5_data_direct_shutdown,
};

int mlx5_data_direct_driver_register(void)
{
	return pci_register_driver(&mlx5_data_direct_driver);
}

void mlx5_data_direct_driver_unregister(void)
{
	pci_unregister_driver(&mlx5_data_direct_driver);
}
