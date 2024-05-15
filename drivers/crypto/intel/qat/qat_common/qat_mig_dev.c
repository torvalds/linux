// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2024 Intel Corporation */
#include <linux/dev_printk.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/qat/qat_mig_dev.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"

struct qat_mig_dev *qat_vfmig_create(struct pci_dev *pdev, int vf_id)
{
	struct adf_accel_dev *accel_dev;
	struct qat_migdev_ops *ops;
	struct qat_mig_dev *mdev;

	accel_dev = adf_devmgr_pci_to_accel_dev(pdev);
	if (!accel_dev)
		return ERR_PTR(-ENODEV);

	ops = GET_VFMIG_OPS(accel_dev);
	if (!ops || !ops->init || !ops->cleanup || !ops->reset || !ops->open ||
	    !ops->close || !ops->suspend || !ops->resume || !ops->save_state ||
	    !ops->load_state || !ops->save_setup || !ops->load_setup)
		return ERR_PTR(-EINVAL);

	mdev = kmalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	mdev->vf_id = vf_id;
	mdev->parent_accel_dev = accel_dev;

	return mdev;
}
EXPORT_SYMBOL_GPL(qat_vfmig_create);

int qat_vfmig_init(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->init(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_init);

void qat_vfmig_cleanup(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->cleanup(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_cleanup);

void qat_vfmig_reset(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->reset(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_reset);

int qat_vfmig_open(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->open(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_open);

void qat_vfmig_close(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	GET_VFMIG_OPS(accel_dev)->close(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_close);

int qat_vfmig_suspend(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->suspend(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_suspend);

int qat_vfmig_resume(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->resume(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_resume);

int qat_vfmig_save_state(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->save_state(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_save_state);

int qat_vfmig_save_setup(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->save_setup(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_save_setup);

int qat_vfmig_load_state(struct qat_mig_dev *mdev)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->load_state(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_load_state);

int qat_vfmig_load_setup(struct qat_mig_dev *mdev, int size)
{
	struct adf_accel_dev *accel_dev = mdev->parent_accel_dev;

	return GET_VFMIG_OPS(accel_dev)->load_setup(mdev, size);
}
EXPORT_SYMBOL_GPL(qat_vfmig_load_setup);

void qat_vfmig_destroy(struct qat_mig_dev *mdev)
{
	kfree(mdev);
}
EXPORT_SYMBOL_GPL(qat_vfmig_destroy);
