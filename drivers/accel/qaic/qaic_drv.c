// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/mhi.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <uapi/drm/qaic_accel.h>

#include "mhi_controller.h"
#include "qaic.h"

MODULE_IMPORT_NS(DMA_BUF);

#define PCI_DEV_AIC100			0xa100
#define QAIC_NAME			"qaic"
#define QAIC_DESC			"Qualcomm Cloud AI Accelerators"
#define CNTL_MAJOR			5
#define CNTL_MINOR			0

bool datapath_polling;
module_param(datapath_polling, bool, 0400);
MODULE_PARM_DESC(datapath_polling, "Operate the datapath in polling mode");
static bool link_up;
static DEFINE_IDA(qaic_usrs);

static int qaic_create_drm_device(struct qaic_device *qdev, s32 partition_id);
static void qaic_destroy_drm_device(struct qaic_device *qdev, s32 partition_id);

static void free_usr(struct kref *kref)
{
	struct qaic_user *usr = container_of(kref, struct qaic_user, ref_count);

	cleanup_srcu_struct(&usr->qddev_lock);
	ida_free(&qaic_usrs, usr->handle);
	kfree(usr);
}

static int qaic_open(struct drm_device *dev, struct drm_file *file)
{
	struct qaic_drm_device *qddev = to_qaic_drm_device(dev);
	struct qaic_device *qdev = qddev->qdev;
	struct qaic_user *usr;
	int rcu_id;
	int ret;

	rcu_id = srcu_read_lock(&qdev->dev_lock);
	if (qdev->in_reset) {
		ret = -ENODEV;
		goto dev_unlock;
	}

	usr = kmalloc(sizeof(*usr), GFP_KERNEL);
	if (!usr) {
		ret = -ENOMEM;
		goto dev_unlock;
	}

	usr->handle = ida_alloc(&qaic_usrs, GFP_KERNEL);
	if (usr->handle < 0) {
		ret = usr->handle;
		goto free_usr;
	}
	usr->qddev = qddev;
	atomic_set(&usr->chunk_id, 0);
	init_srcu_struct(&usr->qddev_lock);
	kref_init(&usr->ref_count);

	ret = mutex_lock_interruptible(&qddev->users_mutex);
	if (ret)
		goto cleanup_usr;

	list_add(&usr->node, &qddev->users);
	mutex_unlock(&qddev->users_mutex);

	file->driver_priv = usr;

	srcu_read_unlock(&qdev->dev_lock, rcu_id);
	return 0;

cleanup_usr:
	cleanup_srcu_struct(&usr->qddev_lock);
	ida_free(&qaic_usrs, usr->handle);
free_usr:
	kfree(usr);
dev_unlock:
	srcu_read_unlock(&qdev->dev_lock, rcu_id);
	return ret;
}

static void qaic_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct qaic_user *usr = file->driver_priv;
	struct qaic_drm_device *qddev;
	struct qaic_device *qdev;
	int qdev_rcu_id;
	int usr_rcu_id;
	int i;

	qddev = usr->qddev;
	usr_rcu_id = srcu_read_lock(&usr->qddev_lock);
	if (qddev) {
		qdev = qddev->qdev;
		qdev_rcu_id = srcu_read_lock(&qdev->dev_lock);
		if (!qdev->in_reset) {
			qaic_release_usr(qdev, usr);
			for (i = 0; i < qdev->num_dbc; ++i)
				if (qdev->dbc[i].usr && qdev->dbc[i].usr->handle == usr->handle)
					release_dbc(qdev, i);
		}
		srcu_read_unlock(&qdev->dev_lock, qdev_rcu_id);

		mutex_lock(&qddev->users_mutex);
		if (!list_empty(&usr->node))
			list_del_init(&usr->node);
		mutex_unlock(&qddev->users_mutex);
	}

	srcu_read_unlock(&usr->qddev_lock, usr_rcu_id);
	kref_put(&usr->ref_count, free_usr);

	file->driver_priv = NULL;
}

DEFINE_DRM_ACCEL_FOPS(qaic_accel_fops);

static const struct drm_ioctl_desc qaic_drm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(QAIC_MANAGE, qaic_manage_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_CREATE_BO, qaic_create_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_MMAP_BO, qaic_mmap_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_ATTACH_SLICE_BO, qaic_attach_slice_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_EXECUTE_BO, qaic_execute_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_PARTIAL_EXECUTE_BO, qaic_partial_execute_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_WAIT_BO, qaic_wait_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_PERF_STATS_BO, qaic_perf_stats_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(QAIC_DETACH_SLICE_BO, qaic_detach_slice_bo_ioctl, 0),
};

static const struct drm_driver qaic_accel_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_COMPUTE_ACCEL,

	.name			= QAIC_NAME,
	.desc			= QAIC_DESC,
	.date			= "20190618",

	.fops			= &qaic_accel_fops,
	.open			= qaic_open,
	.postclose		= qaic_postclose,

	.ioctls			= qaic_drm_ioctls,
	.num_ioctls		= ARRAY_SIZE(qaic_drm_ioctls),
	.gem_prime_import	= qaic_gem_prime_import,
};

static int qaic_create_drm_device(struct qaic_device *qdev, s32 partition_id)
{
	struct qaic_drm_device *qddev = qdev->qddev;
	struct drm_device *drm = to_drm(qddev);
	int ret;

	/* Hold off implementing partitions until the uapi is determined */
	if (partition_id != QAIC_NO_PARTITION)
		return -EINVAL;

	qddev->partition_id = partition_id;

	/*
	 * drm_dev_unregister() sets the driver data to NULL and
	 * drm_dev_register() does not update the driver data. During a SOC
	 * reset drm dev is unregistered and registered again leaving the
	 * driver data to NULL.
	 */
	dev_set_drvdata(to_accel_kdev(qddev), drm->accel);
	ret = drm_dev_register(drm, 0);
	if (ret)
		pci_dbg(qdev->pdev, "drm_dev_register failed %d\n", ret);

	return ret;
}

static void qaic_destroy_drm_device(struct qaic_device *qdev, s32 partition_id)
{
	struct qaic_drm_device *qddev = qdev->qddev;
	struct drm_device *drm = to_drm(qddev);
	struct qaic_user *usr;

	drm_dev_get(drm);
	drm_dev_unregister(drm);
	qddev->partition_id = 0;
	/*
	 * Existing users get unresolvable errors till they close FDs.
	 * Need to sync carefully with users calling close(). The
	 * list of users can be modified elsewhere when the lock isn't
	 * held here, but the sync'ing the srcu with the mutex held
	 * could deadlock. Grab the mutex so that the list will be
	 * unmodified. The user we get will exist as long as the
	 * lock is held. Signal that the qcdev is going away, and
	 * grab a reference to the user so they don't go away for
	 * synchronize_srcu(). Then release the mutex to avoid
	 * deadlock and make sure the user has observed the signal.
	 * With the lock released, we cannot maintain any state of the
	 * user list.
	 */
	mutex_lock(&qddev->users_mutex);
	while (!list_empty(&qddev->users)) {
		usr = list_first_entry(&qddev->users, struct qaic_user, node);
		list_del_init(&usr->node);
		kref_get(&usr->ref_count);
		usr->qddev = NULL;
		mutex_unlock(&qddev->users_mutex);
		synchronize_srcu(&usr->qddev_lock);
		kref_put(&usr->ref_count, free_usr);
		mutex_lock(&qddev->users_mutex);
	}
	mutex_unlock(&qddev->users_mutex);
	drm_dev_put(drm);
}

static int qaic_mhi_probe(struct mhi_device *mhi_dev, const struct mhi_device_id *id)
{
	u16 major = -1, minor = -1;
	struct qaic_device *qdev;
	int ret;

	/*
	 * Invoking this function indicates that the control channel to the
	 * device is available. We use that as a signal to indicate that
	 * the device side firmware has booted. The device side firmware
	 * manages the device resources, so we need to communicate with it
	 * via the control channel in order to utilize the device. Therefore
	 * we wait until this signal to create the drm dev that userspace will
	 * use to control the device, because without the device side firmware,
	 * userspace can't do anything useful.
	 */

	qdev = pci_get_drvdata(to_pci_dev(mhi_dev->mhi_cntrl->cntrl_dev));

	qdev->in_reset = false;

	dev_set_drvdata(&mhi_dev->dev, qdev);
	qdev->cntl_ch = mhi_dev;

	ret = qaic_control_open(qdev);
	if (ret) {
		pci_dbg(qdev->pdev, "%s: control_open failed %d\n", __func__, ret);
		return ret;
	}

	ret = get_cntl_version(qdev, NULL, &major, &minor);
	if (ret || major != CNTL_MAJOR || minor > CNTL_MINOR) {
		pci_err(qdev->pdev, "%s: Control protocol version (%d.%d) not supported. Supported version is (%d.%d). Ret: %d\n",
			__func__, major, minor, CNTL_MAJOR, CNTL_MINOR, ret);
		ret = -EINVAL;
		goto close_control;
	}

	ret = qaic_create_drm_device(qdev, QAIC_NO_PARTITION);

	return ret;

close_control:
	qaic_control_close(qdev);
	return ret;
}

static void qaic_mhi_remove(struct mhi_device *mhi_dev)
{
/* This is redundant since we have already observed the device crash */
}

static void qaic_notify_reset(struct qaic_device *qdev)
{
	int i;

	qdev->in_reset = true;
	/* wake up any waiters to avoid waiting for timeouts at sync */
	wake_all_cntl(qdev);
	for (i = 0; i < qdev->num_dbc; ++i)
		wakeup_dbc(qdev, i);
	synchronize_srcu(&qdev->dev_lock);
}

void qaic_dev_reset_clean_local_state(struct qaic_device *qdev, bool exit_reset)
{
	int i;

	qaic_notify_reset(qdev);

	/* remove drmdevs to prevent new users from coming in */
	qaic_destroy_drm_device(qdev, QAIC_NO_PARTITION);

	/* start tearing things down */
	for (i = 0; i < qdev->num_dbc; ++i)
		release_dbc(qdev, i);

	if (exit_reset)
		qdev->in_reset = false;
}

static void cleanup_qdev(struct qaic_device *qdev)
{
	int i;

	for (i = 0; i < qdev->num_dbc; ++i)
		cleanup_srcu_struct(&qdev->dbc[i].ch_lock);
	cleanup_srcu_struct(&qdev->dev_lock);
	pci_set_drvdata(qdev->pdev, NULL);
	destroy_workqueue(qdev->cntl_wq);
}

static struct qaic_device *create_qdev(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qaic_drm_device *qddev;
	struct qaic_device *qdev;
	int i;

	qdev = devm_kzalloc(&pdev->dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return NULL;

	if (id->device == PCI_DEV_AIC100) {
		qdev->num_dbc = 16;
		qdev->dbc = devm_kcalloc(&pdev->dev, qdev->num_dbc, sizeof(*qdev->dbc), GFP_KERNEL);
		if (!qdev->dbc)
			return NULL;
	}

	qdev->cntl_wq = alloc_workqueue("qaic_cntl", WQ_UNBOUND, 0);
	if (!qdev->cntl_wq)
		return NULL;

	pci_set_drvdata(pdev, qdev);
	qdev->pdev = pdev;

	mutex_init(&qdev->cntl_mutex);
	INIT_LIST_HEAD(&qdev->cntl_xfer_list);
	init_srcu_struct(&qdev->dev_lock);

	for (i = 0; i < qdev->num_dbc; ++i) {
		spin_lock_init(&qdev->dbc[i].xfer_lock);
		qdev->dbc[i].qdev = qdev;
		qdev->dbc[i].id = i;
		INIT_LIST_HEAD(&qdev->dbc[i].xfer_list);
		init_srcu_struct(&qdev->dbc[i].ch_lock);
		init_waitqueue_head(&qdev->dbc[i].dbc_release);
		INIT_LIST_HEAD(&qdev->dbc[i].bo_lists);
	}

	qddev = devm_drm_dev_alloc(&pdev->dev, &qaic_accel_driver, struct qaic_drm_device, drm);
	if (IS_ERR(qddev)) {
		cleanup_qdev(qdev);
		return NULL;
	}

	drmm_mutex_init(to_drm(qddev), &qddev->users_mutex);
	INIT_LIST_HEAD(&qddev->users);
	qddev->qdev = qdev;
	qdev->qddev = qddev;

	return qdev;
}

static int init_pci(struct qaic_device *qdev, struct pci_dev *pdev)
{
	int bars;
	int ret;

	bars = pci_select_bars(pdev, IORESOURCE_MEM);

	/* make sure the device has the expected BARs */
	if (bars != (BIT(0) | BIT(2) | BIT(4))) {
		pci_dbg(pdev, "%s: expected BARs 0, 2, and 4 not found in device. Found 0x%x\n",
			__func__, bars);
		return -EINVAL;
	}

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;
	ret = dma_set_max_seg_size(&pdev->dev, UINT_MAX);
	if (ret)
		return ret;

	qdev->bar_0 = devm_ioremap_resource(&pdev->dev, &pdev->resource[0]);
	if (IS_ERR(qdev->bar_0))
		return PTR_ERR(qdev->bar_0);

	qdev->bar_2 = devm_ioremap_resource(&pdev->dev, &pdev->resource[2]);
	if (IS_ERR(qdev->bar_2))
		return PTR_ERR(qdev->bar_2);

	/* Managed release since we use pcim_enable_device above */
	pci_set_master(pdev);

	return 0;
}

static int init_msi(struct qaic_device *qdev, struct pci_dev *pdev)
{
	int mhi_irq;
	int ret;
	int i;

	/* Managed release since we use pcim_enable_device */
	ret = pci_alloc_irq_vectors(pdev, 1, 32, PCI_IRQ_MSI);
	if (ret < 0)
		return ret;

	if (ret < 32) {
		pci_err(pdev, "%s: Requested 32 MSIs. Obtained %d MSIs which is less than the 32 required.\n",
			__func__, ret);
		return -ENODEV;
	}

	mhi_irq = pci_irq_vector(pdev, 0);
	if (mhi_irq < 0)
		return mhi_irq;

	for (i = 0; i < qdev->num_dbc; ++i) {
		ret = devm_request_threaded_irq(&pdev->dev, pci_irq_vector(pdev, i + 1),
						dbc_irq_handler, dbc_irq_threaded_fn, IRQF_SHARED,
						"qaic_dbc", &qdev->dbc[i]);
		if (ret)
			return ret;

		if (datapath_polling) {
			qdev->dbc[i].irq = pci_irq_vector(pdev, i + 1);
			disable_irq_nosync(qdev->dbc[i].irq);
			INIT_WORK(&qdev->dbc[i].poll_work, irq_polling_work);
		}
	}

	return mhi_irq;
}

static int qaic_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qaic_device *qdev;
	int mhi_irq;
	int ret;
	int i;

	qdev = create_qdev(pdev, id);
	if (!qdev)
		return -ENOMEM;

	ret = init_pci(qdev, pdev);
	if (ret)
		goto cleanup_qdev;

	for (i = 0; i < qdev->num_dbc; ++i)
		qdev->dbc[i].dbc_base = qdev->bar_2 + QAIC_DBC_OFF(i);

	mhi_irq = init_msi(qdev, pdev);
	if (mhi_irq < 0) {
		ret = mhi_irq;
		goto cleanup_qdev;
	}

	qdev->mhi_cntrl = qaic_mhi_register_controller(pdev, qdev->bar_0, mhi_irq);
	if (IS_ERR(qdev->mhi_cntrl)) {
		ret = PTR_ERR(qdev->mhi_cntrl);
		goto cleanup_qdev;
	}

	return 0;

cleanup_qdev:
	cleanup_qdev(qdev);
	return ret;
}

static void qaic_pci_remove(struct pci_dev *pdev)
{
	struct qaic_device *qdev = pci_get_drvdata(pdev);

	if (!qdev)
		return;

	qaic_dev_reset_clean_local_state(qdev, false);
	qaic_mhi_free_controller(qdev->mhi_cntrl, link_up);
	cleanup_qdev(qdev);
}

static void qaic_pci_shutdown(struct pci_dev *pdev)
{
	/* see qaic_exit for what link_up is doing */
	link_up = true;
	qaic_pci_remove(pdev);
}

static pci_ers_result_t qaic_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t error)
{
	return PCI_ERS_RESULT_NEED_RESET;
}

static void qaic_pci_reset_prepare(struct pci_dev *pdev)
{
	struct qaic_device *qdev = pci_get_drvdata(pdev);

	qaic_notify_reset(qdev);
	qaic_mhi_start_reset(qdev->mhi_cntrl);
	qaic_dev_reset_clean_local_state(qdev, false);
}

static void qaic_pci_reset_done(struct pci_dev *pdev)
{
	struct qaic_device *qdev = pci_get_drvdata(pdev);

	qdev->in_reset = false;
	qaic_mhi_reset_done(qdev->mhi_cntrl);
}

static const struct mhi_device_id qaic_mhi_match_table[] = {
	{ .chan = "QAIC_CONTROL", },
	{},
};

static struct mhi_driver qaic_mhi_driver = {
	.id_table = qaic_mhi_match_table,
	.remove = qaic_mhi_remove,
	.probe = qaic_mhi_probe,
	.ul_xfer_cb = qaic_mhi_ul_xfer_cb,
	.dl_xfer_cb = qaic_mhi_dl_xfer_cb,
	.driver = {
		.name = "qaic_mhi",
	},
};

static const struct pci_device_id qaic_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QCOM, PCI_DEV_AIC100), },
	{ }
};
MODULE_DEVICE_TABLE(pci, qaic_ids);

static const struct pci_error_handlers qaic_pci_err_handler = {
	.error_detected = qaic_pci_error_detected,
	.reset_prepare = qaic_pci_reset_prepare,
	.reset_done = qaic_pci_reset_done,
};

static struct pci_driver qaic_pci_driver = {
	.name = QAIC_NAME,
	.id_table = qaic_ids,
	.probe = qaic_pci_probe,
	.remove = qaic_pci_remove,
	.shutdown = qaic_pci_shutdown,
	.err_handler = &qaic_pci_err_handler,
};

static int __init qaic_init(void)
{
	int ret;

	ret = pci_register_driver(&qaic_pci_driver);
	if (ret) {
		pr_debug("qaic: pci_register_driver failed %d\n", ret);
		return ret;
	}

	ret = mhi_driver_register(&qaic_mhi_driver);
	if (ret) {
		pr_debug("qaic: mhi_driver_register failed %d\n", ret);
		goto free_pci;
	}

	return 0;

free_pci:
	pci_unregister_driver(&qaic_pci_driver);
	return ret;
}

static void __exit qaic_exit(void)
{
	/*
	 * We assume that qaic_pci_remove() is called due to a hotplug event
	 * which would mean that the link is down, and thus
	 * qaic_mhi_free_controller() should not try to access the device during
	 * cleanup.
	 * We call pci_unregister_driver() below, which also triggers
	 * qaic_pci_remove(), but since this is module exit, we expect the link
	 * to the device to be up, in which case qaic_mhi_free_controller()
	 * should try to access the device during cleanup to put the device in
	 * a sane state.
	 * For that reason, we set link_up here to let qaic_mhi_free_controller
	 * know the expected link state. Since the module is going to be
	 * removed at the end of this, we don't need to worry about
	 * reinitializing the link_up state after the cleanup is done.
	 */
	link_up = true;
	mhi_driver_unregister(&qaic_mhi_driver);
	pci_unregister_driver(&qaic_pci_driver);
}

module_init(qaic_init);
module_exit(qaic_exit);

MODULE_AUTHOR(QAIC_DESC " Kernel Driver Team");
MODULE_DESCRIPTION(QAIC_DESC " Accel Driver");
MODULE_LICENSE("GPL");
