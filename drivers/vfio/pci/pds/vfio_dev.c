// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/vfio.h>
#include <linux/vfio_pci_core.h>

#include "lm.h"
#include "dirty.h"
#include "vfio_dev.h"

struct pci_dev *pds_vfio_to_pci_dev(struct pds_vfio_pci_device *pds_vfio)
{
	return pds_vfio->vfio_coredev.pdev;
}

struct device *pds_vfio_to_dev(struct pds_vfio_pci_device *pds_vfio)
{
	return &pds_vfio_to_pci_dev(pds_vfio)->dev;
}

struct pds_vfio_pci_device *pds_vfio_pci_drvdata(struct pci_dev *pdev)
{
	struct vfio_pci_core_device *core_device = dev_get_drvdata(&pdev->dev);

	return container_of(core_device, struct pds_vfio_pci_device,
			    vfio_coredev);
}

void pds_vfio_reset(struct pds_vfio_pci_device *pds_vfio,
		    enum vfio_device_mig_state state)
{
	pds_vfio_put_restore_file(pds_vfio);
	pds_vfio_put_save_file(pds_vfio);
	if (state == VFIO_DEVICE_STATE_ERROR)
		pds_vfio_dirty_disable(pds_vfio, false);
	pds_vfio->state = state;
}

static struct file *
pds_vfio_set_device_state(struct vfio_device *vdev,
			  enum vfio_device_mig_state new_state)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	struct file *res = NULL;

	mutex_lock(&pds_vfio->state_mutex);
	/*
	 * only way to transition out of VFIO_DEVICE_STATE_ERROR is via
	 * VFIO_DEVICE_RESET, so prevent the state machine from running since
	 * vfio_mig_get_next_state() will throw a WARN_ON() when transitioning
	 * from VFIO_DEVICE_STATE_ERROR to any other state
	 */
	while (pds_vfio->state != VFIO_DEVICE_STATE_ERROR &&
	       new_state != pds_vfio->state) {
		enum vfio_device_mig_state next_state;

		int err = vfio_mig_get_next_state(vdev, pds_vfio->state,
						  new_state, &next_state);
		if (err) {
			res = ERR_PTR(err);
			break;
		}

		res = pds_vfio_step_device_state_locked(pds_vfio, next_state);
		if (IS_ERR(res))
			break;

		pds_vfio->state = next_state;

		if (WARN_ON(res && new_state != pds_vfio->state)) {
			res = ERR_PTR(-EINVAL);
			break;
		}
	}
	mutex_unlock(&pds_vfio->state_mutex);
	if (pds_vfio->state == VFIO_DEVICE_STATE_ERROR)
		res = ERR_PTR(-EIO);

	return res;
}

static int pds_vfio_get_device_state(struct vfio_device *vdev,
				     enum vfio_device_mig_state *current_state)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);

	mutex_lock(&pds_vfio->state_mutex);
	*current_state = pds_vfio->state;
	mutex_unlock(&pds_vfio->state_mutex);
	return 0;
}

static int pds_vfio_get_device_state_size(struct vfio_device *vdev,
					  unsigned long *stop_copy_length)
{
	*stop_copy_length = PDS_LM_DEVICE_STATE_LENGTH;
	return 0;
}

static const struct vfio_migration_ops pds_vfio_lm_ops = {
	.migration_set_state = pds_vfio_set_device_state,
	.migration_get_state = pds_vfio_get_device_state,
	.migration_get_data_size = pds_vfio_get_device_state_size
};

static const struct vfio_log_ops pds_vfio_log_ops = {
	.log_start = pds_vfio_dma_logging_start,
	.log_stop = pds_vfio_dma_logging_stop,
	.log_read_and_clear = pds_vfio_dma_logging_report,
};

static int pds_vfio_init_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	struct pci_dev *pdev = to_pci_dev(vdev->dev);
	int err, vf_id, pci_id;

	vf_id = pci_iov_vf_id(pdev);
	if (vf_id < 0)
		return vf_id;

	err = vfio_pci_core_init_dev(vdev);
	if (err)
		return err;

	pds_vfio->vf_id = vf_id;

	mutex_init(&pds_vfio->state_mutex);

	vdev->migration_flags = VFIO_MIGRATION_STOP_COPY | VFIO_MIGRATION_P2P;
	vdev->mig_ops = &pds_vfio_lm_ops;
	vdev->log_ops = &pds_vfio_log_ops;

	pci_id = PCI_DEVID(pdev->bus->number, pdev->devfn);
	dev_dbg(&pdev->dev,
		"%s: PF %#04x VF %#04x vf_id %d domain %d pds_vfio %p\n",
		__func__, pci_dev_id(pci_physfn(pdev)), pci_id, vf_id,
		pci_domain_nr(pdev->bus), pds_vfio);

	return 0;
}

static void pds_vfio_release_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);

	mutex_destroy(&pds_vfio->state_mutex);
	vfio_pci_core_release_dev(vdev);
}

static int pds_vfio_open_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);
	int err;

	err = vfio_pci_core_enable(&pds_vfio->vfio_coredev);
	if (err)
		return err;

	pds_vfio->state = VFIO_DEVICE_STATE_RUNNING;

	vfio_pci_core_finish_enable(&pds_vfio->vfio_coredev);

	return 0;
}

static void pds_vfio_close_device(struct vfio_device *vdev)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(vdev, struct pds_vfio_pci_device,
			     vfio_coredev.vdev);

	mutex_lock(&pds_vfio->state_mutex);
	pds_vfio_put_restore_file(pds_vfio);
	pds_vfio_put_save_file(pds_vfio);
	pds_vfio_dirty_disable(pds_vfio, true);
	mutex_unlock(&pds_vfio->state_mutex);
	vfio_pci_core_close_device(vdev);
}

static const struct vfio_device_ops pds_vfio_ops = {
	.name = "pds-vfio",
	.init = pds_vfio_init_device,
	.release = pds_vfio_release_device,
	.open_device = pds_vfio_open_device,
	.close_device = pds_vfio_close_device,
	.ioctl = vfio_pci_core_ioctl,
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

const struct vfio_device_ops *pds_vfio_ops_info(void)
{
	return &pds_vfio_ops;
}
