// SPDX-License-Identifier: GPL-2.0-only

/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved. */
/* Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved. */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/kobject.h>
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
#include "qaic_debugfs.h"
#include "qaic_timesync.h"
#include "sahara.h"

MODULE_IMPORT_NS("DMA_BUF");

#define PCI_DEVICE_ID_QCOM_AIC080	0xa080
#define PCI_DEVICE_ID_QCOM_AIC100	0xa100
#define PCI_DEVICE_ID_QCOM_AIC200	0xa110
#define QAIC_NAME			"qaic"
#define QAIC_DESC			"Qualcomm Cloud AI Accelerators"
#define CNTL_MAJOR			5
#define CNTL_MINOR			0

struct qaic_device_config {
	/* Indicates the AIC family the device belongs to */
	int family;
	/* A bitmask representing the available BARs */
	int bar_mask;
	/* An index value used to identify the MHI controller BAR */
	unsigned int mhi_bar_idx;
	/* An index value used to identify the DBCs BAR */
	unsigned int dbc_bar_idx;
};

static const struct qaic_device_config aic080_config = {
	.family = FAMILY_AIC100,
	.bar_mask = BIT(0) | BIT(2) | BIT(4),
	.mhi_bar_idx = 0,
	.dbc_bar_idx = 2,
};

static const struct qaic_device_config aic100_config = {
	.family = FAMILY_AIC100,
	.bar_mask = BIT(0) | BIT(2) | BIT(4),
	.mhi_bar_idx = 0,
	.dbc_bar_idx = 2,
};

static const struct qaic_device_config aic200_config = {
	.family = FAMILY_AIC200,
	.bar_mask = BIT(0) | BIT(1) | BIT(2) | BIT(4),
	.mhi_bar_idx = 1,
	.dbc_bar_idx = 2,
};

bool datapath_polling;
module_param(datapath_polling, bool, 0400);
MODULE_PARM_DESC(datapath_polling, "Operate the datapath in polling mode");
static bool link_up;
static DEFINE_IDA(qaic_usrs);

static void qaicm_wq_release(struct drm_device *dev, void *res)
{
	struct workqueue_struct *wq = res;

	destroy_workqueue(wq);
}

static struct workqueue_struct *qaicm_wq_init(struct drm_device *dev, const char *name)
{
	struct workqueue_struct *wq;
	int ret;

	wq = alloc_workqueue("%s", WQ_UNBOUND, 0, name);
	if (!wq)
		return ERR_PTR(-ENOMEM);
	ret = drmm_add_action_or_reset(dev, qaicm_wq_release, wq);
	if (ret)
		return ERR_PTR(ret);

	return wq;
}

static void qaicm_srcu_release(struct drm_device *dev, void *res)
{
	struct srcu_struct *lock = res;

	cleanup_srcu_struct(lock);
}

static int qaicm_srcu_init(struct drm_device *dev, struct srcu_struct *lock)
{
	int ret;

	ret = init_srcu_struct(lock);
	if (ret)
		return ret;

	return drmm_add_action_or_reset(dev, qaicm_srcu_release, lock);
}

static void qaicm_pci_release(struct drm_device *dev, void *res)
{
	struct qaic_device *qdev = to_qaic_device(dev);

	pci_set_drvdata(qdev->pdev, NULL);
}

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
	if (qdev->dev_state != QAIC_ONLINE) {
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
		if (qdev->dev_state == QAIC_ONLINE) {
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

	ret = drm_dev_register(drm, 0);
	if (ret) {
		pci_dbg(qdev->pdev, "drm_dev_register failed %d\n", ret);
		return ret;
	}

	qaic_debugfs_init(qddev);

	return ret;
}

static void qaic_destroy_drm_device(struct qaic_device *qdev, s32 partition_id)
{
	struct qaic_drm_device *qddev = qdev->qddev;
	struct drm_device *drm = to_drm(qddev);
	struct qaic_user *usr;

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

	dev_set_drvdata(&mhi_dev->dev, qdev);
	qdev->cntl_ch = mhi_dev;

	ret = qaic_control_open(qdev);
	if (ret) {
		pci_dbg(qdev->pdev, "%s: control_open failed %d\n", __func__, ret);
		return ret;
	}

	qdev->dev_state = QAIC_BOOT;
	ret = get_cntl_version(qdev, NULL, &major, &minor);
	if (ret || major != CNTL_MAJOR || minor > CNTL_MINOR) {
		pci_err(qdev->pdev, "%s: Control protocol version (%d.%d) not supported. Supported version is (%d.%d). Ret: %d\n",
			__func__, major, minor, CNTL_MAJOR, CNTL_MINOR, ret);
		ret = -EINVAL;
		goto close_control;
	}
	qdev->dev_state = QAIC_ONLINE;
	kobject_uevent(&(to_accel_kdev(qdev->qddev))->kobj, KOBJ_ONLINE);

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

	kobject_uevent(&(to_accel_kdev(qdev->qddev))->kobj, KOBJ_OFFLINE);
	qdev->dev_state = QAIC_OFFLINE;
	/* wake up any waiters to avoid waiting for timeouts at sync */
	wake_all_cntl(qdev);
	for (i = 0; i < qdev->num_dbc; ++i)
		wakeup_dbc(qdev, i);
	synchronize_srcu(&qdev->dev_lock);
}

void qaic_dev_reset_clean_local_state(struct qaic_device *qdev)
{
	int i;

	qaic_notify_reset(qdev);

	/* start tearing things down */
	for (i = 0; i < qdev->num_dbc; ++i)
		release_dbc(qdev, i);
}

static struct qaic_device *create_qdev(struct pci_dev *pdev,
				       const struct qaic_device_config *config)
{
	struct device *dev = &pdev->dev;
	struct qaic_drm_device *qddev;
	struct qaic_device *qdev;
	struct drm_device *drm;
	int i, ret;

	qdev = devm_kzalloc(dev, sizeof(*qdev), GFP_KERNEL);
	if (!qdev)
		return NULL;

	qdev->dev_state = QAIC_OFFLINE;
	qdev->num_dbc = 16;
	qdev->dbc = devm_kcalloc(dev, qdev->num_dbc, sizeof(*qdev->dbc), GFP_KERNEL);
	if (!qdev->dbc)
		return NULL;

	qddev = devm_drm_dev_alloc(&pdev->dev, &qaic_accel_driver, struct qaic_drm_device, drm);
	if (IS_ERR(qddev))
		return NULL;

	drm = to_drm(qddev);
	pci_set_drvdata(pdev, qdev);

	ret = drmm_mutex_init(drm, &qddev->users_mutex);
	if (ret)
		return NULL;
	ret = drmm_add_action_or_reset(drm, qaicm_pci_release, NULL);
	if (ret)
		return NULL;
	ret = drmm_mutex_init(drm, &qdev->cntl_mutex);
	if (ret)
		return NULL;
	ret = drmm_mutex_init(drm, &qdev->bootlog_mutex);
	if (ret)
		return NULL;

	qdev->cntl_wq = qaicm_wq_init(drm, "qaic_cntl");
	if (IS_ERR(qdev->cntl_wq))
		return NULL;
	qdev->qts_wq = qaicm_wq_init(drm, "qaic_ts");
	if (IS_ERR(qdev->qts_wq))
		return NULL;

	ret = qaicm_srcu_init(drm, &qdev->dev_lock);
	if (ret)
		return NULL;

	qdev->qddev = qddev;
	qdev->pdev = pdev;
	qddev->qdev = qdev;

	INIT_LIST_HEAD(&qdev->cntl_xfer_list);
	INIT_LIST_HEAD(&qdev->bootlog);
	INIT_LIST_HEAD(&qddev->users);

	for (i = 0; i < qdev->num_dbc; ++i) {
		spin_lock_init(&qdev->dbc[i].xfer_lock);
		qdev->dbc[i].qdev = qdev;
		qdev->dbc[i].id = i;
		INIT_LIST_HEAD(&qdev->dbc[i].xfer_list);
		ret = qaicm_srcu_init(drm, &qdev->dbc[i].ch_lock);
		if (ret)
			return NULL;
		init_waitqueue_head(&qdev->dbc[i].dbc_release);
		INIT_LIST_HEAD(&qdev->dbc[i].bo_lists);
	}

	return qdev;
}

static int init_pci(struct qaic_device *qdev, struct pci_dev *pdev,
		    const struct qaic_device_config *config)
{
	int bars;
	int ret;

	bars = pci_select_bars(pdev, IORESOURCE_MEM) & 0x3f;

	/* make sure the device has the expected BARs */
	if (bars != config->bar_mask) {
		pci_dbg(pdev, "%s: expected BARs %#x not found in device. Found %#x\n",
			__func__, config->bar_mask, bars);
		return -EINVAL;
	}

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;
	dma_set_max_seg_size(&pdev->dev, UINT_MAX);

	qdev->bar_mhi = devm_ioremap_resource(&pdev->dev, &pdev->resource[config->mhi_bar_idx]);
	if (IS_ERR(qdev->bar_mhi))
		return PTR_ERR(qdev->bar_mhi);

	qdev->bar_dbc = devm_ioremap_resource(&pdev->dev, &pdev->resource[config->dbc_bar_idx]);
	if (IS_ERR(qdev->bar_dbc))
		return PTR_ERR(qdev->bar_dbc);

	/* Managed release since we use pcim_enable_device above */
	pci_set_master(pdev);

	return 0;
}

static int init_msi(struct qaic_device *qdev, struct pci_dev *pdev)
{
	int irq_count = qdev->num_dbc + 1;
	int mhi_irq;
	int ret;
	int i;

	/* Managed release since we use pcim_enable_device */
	ret = pci_alloc_irq_vectors(pdev, irq_count, irq_count, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret == -ENOSPC) {
		ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI | PCI_IRQ_MSIX);
		if (ret < 0)
			return ret;

		/*
		 * Operate in one MSI mode. All interrupts will be directed to
		 * MSI0; every interrupt will wake up all the interrupt handlers
		 * (MHI and DBC[0-15]). Since the interrupt is now shared, it is
		 * not disabled during DBC threaded handler, but only one thread
		 * will be allowed to run per DBC, so while it can be
		 * interrupted, it shouldn't race with itself.
		 */
		qdev->single_msi = true;
		pci_info(pdev, "Allocating %d MSIs failed, operating in 1 MSI mode. Performance may be impacted.\n",
			 irq_count);
	} else if (ret < 0) {
		return ret;
	}

	mhi_irq = pci_irq_vector(pdev, 0);
	if (mhi_irq < 0)
		return mhi_irq;

	for (i = 0; i < qdev->num_dbc; ++i) {
		ret = devm_request_threaded_irq(&pdev->dev,
						pci_irq_vector(pdev, qdev->single_msi ? 0 : i + 1),
						dbc_irq_handler, dbc_irq_threaded_fn, IRQF_SHARED,
						"qaic_dbc", &qdev->dbc[i]);
		if (ret)
			return ret;

		if (datapath_polling) {
			qdev->dbc[i].irq = pci_irq_vector(pdev, qdev->single_msi ? 0 : i + 1);
			if (!qdev->single_msi)
				disable_irq_nosync(qdev->dbc[i].irq);
			INIT_WORK(&qdev->dbc[i].poll_work, irq_polling_work);
		}
	}

	return mhi_irq;
}

static int qaic_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct qaic_device_config *config = (struct qaic_device_config *)id->driver_data;
	struct qaic_device *qdev;
	int mhi_irq;
	int ret;
	int i;

	qdev = create_qdev(pdev, config);
	if (!qdev)
		return -ENOMEM;

	ret = init_pci(qdev, pdev, config);
	if (ret)
		return ret;

	for (i = 0; i < qdev->num_dbc; ++i)
		qdev->dbc[i].dbc_base = qdev->bar_dbc + QAIC_DBC_OFF(i);

	mhi_irq = init_msi(qdev, pdev);
	if (mhi_irq < 0)
		return mhi_irq;

	ret = qaic_create_drm_device(qdev, QAIC_NO_PARTITION);
	if (ret)
		return ret;

	qdev->mhi_cntrl = qaic_mhi_register_controller(pdev, qdev->bar_mhi, mhi_irq,
						       qdev->single_msi, config->family);
	if (IS_ERR(qdev->mhi_cntrl)) {
		ret = PTR_ERR(qdev->mhi_cntrl);
		qaic_destroy_drm_device(qdev, QAIC_NO_PARTITION);
		return ret;
	}

	return 0;
}

static void qaic_pci_remove(struct pci_dev *pdev)
{
	struct qaic_device *qdev = pci_get_drvdata(pdev);

	if (!qdev)
		return;

	qaic_dev_reset_clean_local_state(qdev);
	qaic_mhi_free_controller(qdev->mhi_cntrl, link_up);
	qaic_destroy_drm_device(qdev, QAIC_NO_PARTITION);
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
	qaic_dev_reset_clean_local_state(qdev);
}

static void qaic_pci_reset_done(struct pci_dev *pdev)
{
	struct qaic_device *qdev = pci_get_drvdata(pdev);

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
	{ PCI_DEVICE_DATA(QCOM, AIC080, (kernel_ulong_t)&aic080_config), },
	{ PCI_DEVICE_DATA(QCOM, AIC100, (kernel_ulong_t)&aic100_config), },
	{ PCI_DEVICE_DATA(QCOM, AIC200, (kernel_ulong_t)&aic200_config), },
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

	ret = sahara_register();
	if (ret) {
		pr_debug("qaic: sahara_register failed %d\n", ret);
		goto free_mhi;
	}

	ret = qaic_timesync_init();
	if (ret)
		pr_debug("qaic: qaic_timesync_init failed %d\n", ret);

	ret = qaic_bootlog_register();
	if (ret)
		pr_debug("qaic: qaic_bootlog_register failed %d\n", ret);

	return 0;

free_mhi:
	mhi_driver_unregister(&qaic_mhi_driver);
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
	qaic_bootlog_unregister();
	qaic_timesync_deinit();
	sahara_unregister();
	mhi_driver_unregister(&qaic_mhi_driver);
	pci_unregister_driver(&qaic_pci_driver);
}

module_init(qaic_init);
module_exit(qaic_exit);

MODULE_AUTHOR(QAIC_DESC " Kernel Driver Team");
MODULE_DESCRIPTION(QAIC_DESC " Accel Driver");
MODULE_LICENSE("GPL");
