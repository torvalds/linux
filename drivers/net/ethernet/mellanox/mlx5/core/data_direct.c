// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <linux/list.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/data_direct.h>
#include <linux/mlx5/mlx5_ifc.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "mlx5_core.h"

static LIST_HEAD(mlx5_data_direct_dev_list);
static LIST_HEAD(mlx5_data_direct_reg_list);

/*
 * This mutex should be held when accessing either of the above lists
 */
static DEFINE_MUTEX(mlx5_data_direct_mutex);

struct mlx5_data_direct_registration {
	char vuid[MLX5_ST_SZ_BYTES(array1024_auto) + 1];
	struct list_head list;
	struct blocking_notifier_head users;
	struct mlx5_data_direct dd;
};

static int mlx5_data_direct_query_vuid(struct mlx5_core_dev *dev,
				       char *out_vuid)
{
	u8 out[MLX5_ST_SZ_BYTES(query_vuid_out) +
	       MLX5_ST_SZ_BYTES(array1024_auto)] = {};
	u8 in[MLX5_ST_SZ_BYTES(query_vuid_in)] = {};
	char *vuid;
	int err;

	MLX5_SET(query_vuid_in, in, opcode, MLX5_CMD_OPCODE_QUERY_VUID);
	MLX5_SET(query_vuid_in, in, vhca_id, MLX5_CAP_GEN(dev, vhca_id));
	MLX5_SET(query_vuid_in, in, data_direct, 1);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	vuid = MLX5_ADDR_OF(query_vuid_out, out, vuid);
	memcpy(out_vuid, vuid, MLX5_ST_SZ_BYTES(array1024_auto));
	return 0;
}

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

static int mlx5_data_direct_create_resources(struct mlx5_core_dev *mdev)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	bool ro_supp = false;
	void *mkc;
	u32 mkey;
	u32 pdn;
	u32 *in;
	int err;

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		return err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err;
	}

	MLX5_SET(create_mkey_in, in, data_direct, 1);
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, rr, 1);
	MLX5_SET(mkc, mkc, a, 1);
	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	err = mlx5_core_create_mkey(mdev, &mkey, in, inlen);
	if (err)
		goto err_mkey;

	mdev->data_direct->mkey = mkey;
	mdev->data_direct->pdn = pdn;

	/* create another mkey with RO support */
	if (MLX5_CAP_GEN(mdev, relaxed_ordering_write)) {
		MLX5_SET(mkc, mkc, relaxed_ordering_write, 1);
		ro_supp = true;
	}

	if (MLX5_CAP_GEN(mdev, relaxed_ordering_read)) {
		MLX5_SET(mkc, mkc, relaxed_ordering_read, 1);
		ro_supp = true;
	}

	if (ro_supp) {
		err = mlx5_core_create_mkey(mdev, &mkey, in, inlen);
		/* RO is defined as best effort */
		if (!err) {
			mdev->data_direct->mkey_ro = mkey;
			mdev->data_direct->mkey_ro_valid = true;
		}
	}

	kvfree(in);
	return 0;

err_mkey:
	kvfree(in);
err:
	mlx5_core_dealloc_pd(mdev, pdn);
	return err;
}

static void mlx5_data_direct_free_resources(struct mlx5_core_dev *mdev)
{
	struct mlx5_data_direct *data_direct = mdev->data_direct;

	if (data_direct->mkey_ro_valid)
		mlx5_core_destroy_mkey(mdev, data_direct->mkey_ro);

	mlx5_core_destroy_mkey(mdev, data_direct->mkey);
	mlx5_core_dealloc_pd(mdev, data_direct->pdn);
}

static void mlx5_data_direct_bind(struct mlx5_data_direct_registration *reg,
				  struct mlx5_data_direct_dev *dev)
{
	WRITE_ONCE(reg->dd.dev, dev);
}

static void
mlx5_data_direct_do_unbind(struct mlx5_data_direct_registration *reg)
{
	WRITE_ONCE(reg->dd.dev, NULL);
	blocking_notifier_call_chain(&reg->users, MLX5_DATA_DIRECT_UNBIND,
				     NULL);
}

int mlx5_data_direct_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_data_direct_registration *reg;
	struct mlx5_data_direct_dev *dev;
	int err;

	if (!mlx5_data_direct_supported(mdev))
		return 0;

	reg = kzalloc_obj(*reg);
	if (!reg)
		return -ENOMEM;

	BLOCKING_INIT_NOTIFIER_HEAD(&reg->users);

	err = mlx5_data_direct_query_vuid(mdev, reg->vuid);
	if (err) {
		mlx5_core_warn(mdev, "Failed to query VUID, err=%d\n", err);
		kfree(reg);
		return -EINVAL;
	}

	mdev->data_direct = &reg->dd;

	err = mlx5_data_direct_create_resources(mdev);
	if (err)
		goto err_resources;

	mutex_lock(&mlx5_data_direct_mutex);
	list_for_each_entry(dev, &mlx5_data_direct_dev_list, list) {
		if (strcmp(dev->vuid, reg->vuid) == 0) {
			mlx5_data_direct_bind(reg, dev);
			break;
		}
	}

	/* Add the registration to its global list, to be used upon bind/unbind
	 * of its affiliated data direct device.
	 */
	list_add_tail(&reg->list, &mlx5_data_direct_reg_list);
	mutex_unlock(&mlx5_data_direct_mutex);
	return 0;

err_resources:
	mdev->data_direct = NULL;
	kfree(reg);
	return err;
}

void mlx5_data_direct_cleanup(struct mlx5_core_dev *mdev)
{
	struct mlx5_data_direct_registration *reg;

	if (!mlx5_data_direct_supported(mdev))
		return;

	reg = container_of(mdev->data_direct,
			   struct mlx5_data_direct_registration, dd);
	mutex_lock(&mlx5_data_direct_mutex);
	list_del(&reg->list);
	mlx5_data_direct_do_unbind(reg);
	mutex_unlock(&mlx5_data_direct_mutex);

	mlx5_data_direct_free_resources(mdev);
	mdev->data_direct = NULL;
	kfree(reg);
}

int mlx5_data_direct_register(struct mlx5_core_dev *mdev,
			      struct notifier_block *nb)
{
	struct mlx5_data_direct_registration *reg;

	if (!mlx5_data_direct_supported(mdev))
		return 0;

	reg = container_of(mdev->data_direct,
			   struct mlx5_data_direct_registration, dd);
	blocking_notifier_chain_register(&reg->users, nb);

	return 0;
}
EXPORT_SYMBOL_GPL(mlx5_data_direct_register);

void mlx5_data_direct_unregister(struct mlx5_core_dev *mdev,
				 struct notifier_block *nb)
{
	struct mlx5_data_direct_registration *reg;

	if (!mlx5_data_direct_supported(mdev))
		return;

	reg = container_of(mdev->data_direct,
			   struct mlx5_data_direct_registration, dd);
	blocking_notifier_chain_unregister(&reg->users, nb);
}
EXPORT_SYMBOL_GPL(mlx5_data_direct_unregister);

static void mlx5_data_direct_dev_reg(struct mlx5_data_direct_dev *dev)
{
	struct mlx5_data_direct_registration *reg;

	mutex_lock(&mlx5_data_direct_mutex);
	list_for_each_entry(reg, &mlx5_data_direct_reg_list, list) {
		if (strcmp(dev->vuid, reg->vuid) == 0)
			mlx5_data_direct_bind(reg, dev);
	}

	/* Add the data direct device to the global list, further mlx5 devices
	 * may use it later as well.
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
			mlx5_data_direct_do_unbind(reg);
	}
	mutex_unlock(&mlx5_data_direct_mutex);
}

static int mlx5_data_direct_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct mlx5_data_direct_dev *dev;
	int err;

	dev = kzalloc_obj(*dev);
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
	.name = "mlx5_dd",
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
