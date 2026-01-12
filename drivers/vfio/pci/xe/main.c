// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/anon_inodes.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/types.h>
#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

#include <drm/intel/xe_sriov_vfio.h>
#include <drm/intel/pciids.h>

struct xe_vfio_pci_migration_file {
	struct file *filp;
	/* serializes accesses to migration data */
	struct mutex lock;
	struct xe_vfio_pci_core_device *xe_vdev;
	u8 disabled:1;
};

struct xe_vfio_pci_core_device {
	struct vfio_pci_core_device core_device;
	struct xe_device *xe;
	/* PF internal control uses vfid index starting from 1 */
	unsigned int vfid;
	u8 deferred_reset:1;
	/* protects migration state */
	struct mutex state_mutex;
	enum vfio_device_mig_state mig_state;
	/* protects the reset_done flow */
	spinlock_t reset_lock;
	struct xe_vfio_pci_migration_file *migf;
};

#define xe_vdev_to_dev(xe_vdev) (&(xe_vdev)->core_device.pdev->dev)

static void xe_vfio_pci_disable_file(struct xe_vfio_pci_migration_file *migf)
{
	mutex_lock(&migf->lock);
	migf->disabled = true;
	mutex_unlock(&migf->lock);
}

static void xe_vfio_pci_put_file(struct xe_vfio_pci_core_device *xe_vdev)
{
	xe_vfio_pci_disable_file(xe_vdev->migf);
	fput(xe_vdev->migf->filp);
	xe_vdev->migf = NULL;
}

static void xe_vfio_pci_reset(struct xe_vfio_pci_core_device *xe_vdev)
{
	if (xe_vdev->migf)
		xe_vfio_pci_put_file(xe_vdev);

	xe_vdev->mig_state = VFIO_DEVICE_STATE_RUNNING;
}

static void xe_vfio_pci_state_mutex_lock(struct xe_vfio_pci_core_device *xe_vdev)
{
	mutex_lock(&xe_vdev->state_mutex);
}

/*
 * This function is called in all state_mutex unlock cases to
 * handle a 'deferred_reset' if exists.
 */
static void xe_vfio_pci_state_mutex_unlock(struct xe_vfio_pci_core_device *xe_vdev)
{
again:
	spin_lock(&xe_vdev->reset_lock);
	if (xe_vdev->deferred_reset) {
		xe_vdev->deferred_reset = false;
		spin_unlock(&xe_vdev->reset_lock);
		xe_vfio_pci_reset(xe_vdev);
		goto again;
	}
	mutex_unlock(&xe_vdev->state_mutex);
	spin_unlock(&xe_vdev->reset_lock);
}

static void xe_vfio_pci_reset_done(struct pci_dev *pdev)
{
	struct xe_vfio_pci_core_device *xe_vdev = pci_get_drvdata(pdev);
	int ret;

	if (!pdev->is_virtfn)
		return;

	/*
	 * VF FLR requires additional processing done by PF driver.
	 * The processing is done after FLR is already finished from PCIe
	 * perspective.
	 * In order to avoid a scenario where VF is used while PF processing
	 * is still in progress, additional synchronization point is needed.
	 */
	ret = xe_sriov_vfio_wait_flr_done(xe_vdev->xe, xe_vdev->vfid);
	if (ret)
		dev_err(&pdev->dev, "Failed to wait for FLR: %d\n", ret);

	if (!xe_vdev->vfid)
		return;

	/*
	 * As the higher VFIO layers are holding locks across reset and using
	 * those same locks with the mm_lock we need to prevent ABBA deadlock
	 * with the state_mutex and mm_lock.
	 * In case the state_mutex was taken already we defer the cleanup work
	 * to the unlock flow of the other running context.
	 */
	spin_lock(&xe_vdev->reset_lock);
	xe_vdev->deferred_reset = true;
	if (!mutex_trylock(&xe_vdev->state_mutex)) {
		spin_unlock(&xe_vdev->reset_lock);
		return;
	}
	spin_unlock(&xe_vdev->reset_lock);
	xe_vfio_pci_state_mutex_unlock(xe_vdev);

	xe_vfio_pci_reset(xe_vdev);
}

static const struct pci_error_handlers xe_vfio_pci_err_handlers = {
	.reset_done = xe_vfio_pci_reset_done,
	.error_detected = vfio_pci_core_aer_err_detected,
};

static int xe_vfio_pci_open_device(struct vfio_device *core_vdev)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);
	struct vfio_pci_core_device *vdev = &xe_vdev->core_device;
	int ret;

	ret = vfio_pci_core_enable(vdev);
	if (ret)
		return ret;

	xe_vdev->mig_state = VFIO_DEVICE_STATE_RUNNING;

	vfio_pci_core_finish_enable(vdev);

	return 0;
}

static void xe_vfio_pci_close_device(struct vfio_device *core_vdev)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);

	xe_vfio_pci_state_mutex_lock(xe_vdev);
	xe_vfio_pci_reset(xe_vdev);
	xe_vfio_pci_state_mutex_unlock(xe_vdev);
	vfio_pci_core_close_device(core_vdev);
}

static int xe_vfio_pci_release_file(struct inode *inode, struct file *filp)
{
	struct xe_vfio_pci_migration_file *migf = filp->private_data;

	mutex_destroy(&migf->lock);
	kfree(migf);

	return 0;
}

static ssize_t xe_vfio_pci_save_read(struct file *filp, char __user *buf, size_t len, loff_t *pos)
{
	struct xe_vfio_pci_migration_file *migf = filp->private_data;
	ssize_t ret;

	if (pos)
		return -ESPIPE;

	mutex_lock(&migf->lock);
	if (migf->disabled) {
		mutex_unlock(&migf->lock);
		return -ENODEV;
	}

	ret = xe_sriov_vfio_data_read(migf->xe_vdev->xe, migf->xe_vdev->vfid, buf, len);
	mutex_unlock(&migf->lock);

	return ret;
}

static const struct file_operations xe_vfio_pci_save_fops = {
	.owner = THIS_MODULE,
	.read = xe_vfio_pci_save_read,
	.release = xe_vfio_pci_release_file,
	.llseek = noop_llseek,
};

static ssize_t xe_vfio_pci_resume_write(struct file *filp, const char __user *buf,
					size_t len, loff_t *pos)
{
	struct xe_vfio_pci_migration_file *migf = filp->private_data;
	ssize_t ret;

	if (pos)
		return -ESPIPE;

	mutex_lock(&migf->lock);
	if (migf->disabled) {
		mutex_unlock(&migf->lock);
		return -ENODEV;
	}

	ret = xe_sriov_vfio_data_write(migf->xe_vdev->xe, migf->xe_vdev->vfid, buf, len);
	mutex_unlock(&migf->lock);

	return ret;
}

static const struct file_operations xe_vfio_pci_resume_fops = {
	.owner = THIS_MODULE,
	.write = xe_vfio_pci_resume_write,
	.release = xe_vfio_pci_release_file,
	.llseek = noop_llseek,
};

static const char *vfio_dev_state_str(u32 state)
{
	switch (state) {
	case VFIO_DEVICE_STATE_RUNNING: return "running";
	case VFIO_DEVICE_STATE_RUNNING_P2P: return "running_p2p";
	case VFIO_DEVICE_STATE_STOP_COPY: return "stopcopy";
	case VFIO_DEVICE_STATE_STOP: return "stop";
	case VFIO_DEVICE_STATE_RESUMING: return "resuming";
	case VFIO_DEVICE_STATE_ERROR: return "error";
	default: return "";
	}
}

enum xe_vfio_pci_file_type {
	XE_VFIO_FILE_SAVE = 0,
	XE_VFIO_FILE_RESUME,
};

static struct xe_vfio_pci_migration_file *
xe_vfio_pci_alloc_file(struct xe_vfio_pci_core_device *xe_vdev,
		       enum xe_vfio_pci_file_type type)
{
	struct xe_vfio_pci_migration_file *migf;
	const struct file_operations *fops;
	int flags;
	int ret;

	migf = kzalloc(sizeof(*migf), GFP_KERNEL_ACCOUNT);
	if (!migf)
		return ERR_PTR(-ENOMEM);

	fops = type == XE_VFIO_FILE_SAVE ? &xe_vfio_pci_save_fops : &xe_vfio_pci_resume_fops;
	flags = type == XE_VFIO_FILE_SAVE ? O_RDONLY : O_WRONLY;
	migf->filp = anon_inode_getfile("xe_vfio_mig", fops, migf, flags);
	if (IS_ERR(migf->filp)) {
		ret = PTR_ERR(migf->filp);
		kfree(migf);
		return ERR_PTR(ret);
	}

	mutex_init(&migf->lock);
	migf->xe_vdev = xe_vdev;
	xe_vdev->migf = migf;

	stream_open(migf->filp->f_inode, migf->filp);

	return migf;
}

static struct file *
xe_vfio_set_state(struct xe_vfio_pci_core_device *xe_vdev, u32 new)
{
	u32 cur = xe_vdev->mig_state;
	int ret;

	dev_dbg(xe_vdev_to_dev(xe_vdev),
		"state: %s->%s\n", vfio_dev_state_str(cur), vfio_dev_state_str(new));

	/*
	 * "STOP" handling is reused for "RUNNING_P2P", as the device doesn't
	 * have the capability to selectively block outgoing p2p DMA transfers.
	 * While the device is allowing BAR accesses when the VF is stopped, it
	 * is not processing any new workload requests, effectively stopping
	 * any outgoing DMA transfers (not just p2p).
	 * Any VRAM / MMIO accesses occurring during "RUNNING_P2P" are kept and
	 * will be migrated to target VF during stop-copy.
	 */
	if (cur == VFIO_DEVICE_STATE_RUNNING && new == VFIO_DEVICE_STATE_RUNNING_P2P) {
		ret = xe_sriov_vfio_suspend_device(xe_vdev->xe, xe_vdev->vfid);
		if (ret)
			goto err;

		return NULL;
	}

	if ((cur == VFIO_DEVICE_STATE_RUNNING_P2P && new == VFIO_DEVICE_STATE_STOP) ||
	    (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_RUNNING_P2P))
		return NULL;

	if (cur == VFIO_DEVICE_STATE_RUNNING_P2P && new == VFIO_DEVICE_STATE_RUNNING) {
		ret = xe_sriov_vfio_resume_device(xe_vdev->xe, xe_vdev->vfid);
		if (ret)
			goto err;

		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_STOP_COPY) {
		struct xe_vfio_pci_migration_file *migf;

		migf = xe_vfio_pci_alloc_file(xe_vdev, XE_VFIO_FILE_SAVE);
		if (IS_ERR(migf)) {
			ret = PTR_ERR(migf);
			goto err;
		}
		get_file(migf->filp);

		ret = xe_sriov_vfio_stop_copy_enter(xe_vdev->xe, xe_vdev->vfid);
		if (ret) {
			fput(migf->filp);
			goto err;
		}

		return migf->filp;
	}

	if (cur == VFIO_DEVICE_STATE_STOP_COPY && new == VFIO_DEVICE_STATE_STOP) {
		if (xe_vdev->migf)
			xe_vfio_pci_put_file(xe_vdev);

		ret = xe_sriov_vfio_stop_copy_exit(xe_vdev->xe, xe_vdev->vfid);
		if (ret)
			goto err;

		return NULL;
	}

	if (cur == VFIO_DEVICE_STATE_STOP && new == VFIO_DEVICE_STATE_RESUMING) {
		struct xe_vfio_pci_migration_file *migf;

		migf = xe_vfio_pci_alloc_file(xe_vdev, XE_VFIO_FILE_RESUME);
		if (IS_ERR(migf)) {
			ret = PTR_ERR(migf);
			goto err;
		}
		get_file(migf->filp);

		ret = xe_sriov_vfio_resume_data_enter(xe_vdev->xe, xe_vdev->vfid);
		if (ret) {
			fput(migf->filp);
			goto err;
		}

		return migf->filp;
	}

	if (cur == VFIO_DEVICE_STATE_RESUMING && new == VFIO_DEVICE_STATE_STOP) {
		if (xe_vdev->migf)
			xe_vfio_pci_put_file(xe_vdev);

		ret = xe_sriov_vfio_resume_data_exit(xe_vdev->xe, xe_vdev->vfid);
		if (ret)
			goto err;

		return NULL;
	}

	WARN(true, "Unknown state transition %d->%d", cur, new);
	return ERR_PTR(-EINVAL);

err:
	dev_dbg(xe_vdev_to_dev(xe_vdev),
		"Failed to transition state: %s->%s err=%d\n",
		vfio_dev_state_str(cur), vfio_dev_state_str(new), ret);
	return ERR_PTR(ret);
}

static struct file *
xe_vfio_pci_set_device_state(struct vfio_device *core_vdev,
			     enum vfio_device_mig_state new_state)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);
	enum vfio_device_mig_state next_state;
	struct file *f = NULL;
	int ret;

	xe_vfio_pci_state_mutex_lock(xe_vdev);
	while (new_state != xe_vdev->mig_state) {
		ret = vfio_mig_get_next_state(core_vdev, xe_vdev->mig_state,
					      new_state, &next_state);
		if (ret) {
			xe_sriov_vfio_error(xe_vdev->xe, xe_vdev->vfid);
			f = ERR_PTR(ret);
			break;
		}
		f = xe_vfio_set_state(xe_vdev, next_state);
		if (IS_ERR(f))
			break;

		xe_vdev->mig_state = next_state;

		/* Multiple state transitions with non-NULL file in the middle */
		if (f && new_state != xe_vdev->mig_state) {
			fput(f);
			f = ERR_PTR(-EINVAL);
			break;
		}
	}
	xe_vfio_pci_state_mutex_unlock(xe_vdev);

	return f;
}

static int xe_vfio_pci_get_device_state(struct vfio_device *core_vdev,
					enum vfio_device_mig_state *curr_state)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);

	xe_vfio_pci_state_mutex_lock(xe_vdev);
	*curr_state = xe_vdev->mig_state;
	xe_vfio_pci_state_mutex_unlock(xe_vdev);

	return 0;
}

static int xe_vfio_pci_get_data_size(struct vfio_device *vdev,
				     unsigned long *stop_copy_length)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(vdev, struct xe_vfio_pci_core_device, core_device.vdev);

	xe_vfio_pci_state_mutex_lock(xe_vdev);
	*stop_copy_length = xe_sriov_vfio_stop_copy_size(xe_vdev->xe, xe_vdev->vfid);
	xe_vfio_pci_state_mutex_unlock(xe_vdev);

	return 0;
}

static const struct vfio_migration_ops xe_vfio_pci_migration_ops = {
	.migration_set_state = xe_vfio_pci_set_device_state,
	.migration_get_state = xe_vfio_pci_get_device_state,
	.migration_get_data_size = xe_vfio_pci_get_data_size,
};

static void xe_vfio_pci_migration_init(struct xe_vfio_pci_core_device *xe_vdev)
{
	struct vfio_device *core_vdev = &xe_vdev->core_device.vdev;
	struct pci_dev *pdev = to_pci_dev(core_vdev->dev);
	struct xe_device *xe = xe_sriov_vfio_get_pf(pdev);

	if (!xe)
		return;
	if (!xe_sriov_vfio_migration_supported(xe))
		return;

	mutex_init(&xe_vdev->state_mutex);
	spin_lock_init(&xe_vdev->reset_lock);

	/* PF internal control uses vfid index starting from 1 */
	xe_vdev->vfid = pci_iov_vf_id(pdev) + 1;
	xe_vdev->xe = xe;

	core_vdev->migration_flags = VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P;
	core_vdev->mig_ops = &xe_vfio_pci_migration_ops;
}

static void xe_vfio_pci_migration_fini(struct xe_vfio_pci_core_device *xe_vdev)
{
	if (!xe_vdev->vfid)
		return;

	mutex_destroy(&xe_vdev->state_mutex);
}

static int xe_vfio_pci_init_dev(struct vfio_device *core_vdev)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);

	xe_vfio_pci_migration_init(xe_vdev);

	return vfio_pci_core_init_dev(core_vdev);
}

static void xe_vfio_pci_release_dev(struct vfio_device *core_vdev)
{
	struct xe_vfio_pci_core_device *xe_vdev =
		container_of(core_vdev, struct xe_vfio_pci_core_device, core_device.vdev);

	xe_vfio_pci_migration_fini(xe_vdev);
}

static const struct vfio_device_ops xe_vfio_pci_ops = {
	.name = "xe-vfio-pci",
	.init = xe_vfio_pci_init_dev,
	.release = xe_vfio_pci_release_dev,
	.open_device = xe_vfio_pci_open_device,
	.close_device = xe_vfio_pci_close_device,
	.ioctl = vfio_pci_core_ioctl,
	.get_region_info_caps = vfio_pci_ioctl_get_region_info,
	.device_feature = vfio_pci_core_ioctl_feature,
	.read = vfio_pci_core_read,
	.write = vfio_pci_core_write,
	.mmap = vfio_pci_core_mmap,
	.request = vfio_pci_core_request,
	.match = vfio_pci_core_match,
	.match_token_uuid = vfio_pci_core_match_token_uuid,
	.bind_iommufd = vfio_iommufd_physical_bind,
	.unbind_iommufd = vfio_iommufd_physical_unbind,
	.attach_ioas = vfio_iommufd_physical_attach_ioas,
	.detach_ioas = vfio_iommufd_physical_detach_ioas,
};

static int xe_vfio_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct xe_vfio_pci_core_device *xe_vdev;
	int ret;

	xe_vdev = vfio_alloc_device(xe_vfio_pci_core_device, core_device.vdev, &pdev->dev,
				    &xe_vfio_pci_ops);
	if (IS_ERR(xe_vdev))
		return PTR_ERR(xe_vdev);

	dev_set_drvdata(&pdev->dev, &xe_vdev->core_device);

	ret = vfio_pci_core_register_device(&xe_vdev->core_device);
	if (ret) {
		vfio_put_device(&xe_vdev->core_device.vdev);
		return ret;
	}

	return 0;
}

static void xe_vfio_pci_remove(struct pci_dev *pdev)
{
	struct xe_vfio_pci_core_device *xe_vdev = pci_get_drvdata(pdev);

	vfio_pci_core_unregister_device(&xe_vdev->core_device);
	vfio_put_device(&xe_vdev->core_device.vdev);
}

#define INTEL_PCI_VFIO_DEVICE(_id) { \
	PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_INTEL, (_id)) \
}

static const struct pci_device_id xe_vfio_pci_table[] = {
	INTEL_PTL_IDS(INTEL_PCI_VFIO_DEVICE),
	INTEL_WCL_IDS(INTEL_PCI_VFIO_DEVICE),
	INTEL_BMG_IDS(INTEL_PCI_VFIO_DEVICE),
	{}
};
MODULE_DEVICE_TABLE(pci, xe_vfio_pci_table);

static struct pci_driver xe_vfio_pci_driver = {
	.name = "xe-vfio-pci",
	.id_table = xe_vfio_pci_table,
	.probe = xe_vfio_pci_probe,
	.remove = xe_vfio_pci_remove,
	.err_handler = &xe_vfio_pci_err_handlers,
	.driver_managed_dma = true,
};
module_pci_driver(xe_vfio_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michał Winiarski <michal.winiarski@intel.com>");
MODULE_DESCRIPTION("VFIO PCI driver with migration support for Intel Graphics");
