// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2025 Intel Corporation */

#include <linux/export.h>

#include "idpf.h"
#include "idpf_virtchnl.h"

static DEFINE_IDA(idpf_idc_ida);

#define IDPF_IDC_MAX_ADEV_NAME_LEN	15

/**
 * idpf_idc_init - Called to initialize IDC
 * @adapter: driver private data structure
 *
 * Return: 0 on success or cap not enabled, error code on failure.
 */
int idpf_idc_init(struct idpf_adapter *adapter)
{
	int err;

	if (!idpf_is_rdma_cap_ena(adapter) ||
	    !adapter->dev_ops.idc_init)
		return 0;

	err = adapter->dev_ops.idc_init(adapter);
	if (err)
		dev_err(&adapter->pdev->dev, "failed to initialize idc: %d\n",
			err);

	return err;
}

/**
 * idpf_vport_adev_release - function to be mapped to aux dev's release op
 * @dev: pointer to device to free
 */
static void idpf_vport_adev_release(struct device *dev)
{
	struct iidc_rdma_vport_auxiliary_dev *iadev;

	iadev = container_of(dev, struct iidc_rdma_vport_auxiliary_dev, adev.dev);
	kfree(iadev);
	iadev = NULL;
}

/**
 * idpf_plug_vport_aux_dev - allocate and register a vport Auxiliary device
 * @cdev_info: IDC core device info pointer
 * @vdev_info: IDC vport device info pointer
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_plug_vport_aux_dev(struct iidc_rdma_core_dev_info *cdev_info,
				   struct iidc_rdma_vport_dev_info *vdev_info)
{
	struct iidc_rdma_vport_auxiliary_dev *iadev;
	char name[IDPF_IDC_MAX_ADEV_NAME_LEN];
	struct auxiliary_device *adev;
	int ret;

	iadev = kzalloc(sizeof(*iadev), GFP_KERNEL);
	if (!iadev)
		return -ENOMEM;

	adev = &iadev->adev;
	vdev_info->adev = &iadev->adev;
	iadev->vdev_info = vdev_info;

	ret = ida_alloc(&idpf_idc_ida, GFP_KERNEL);
	if (ret < 0) {
		pr_err("failed to allocate unique device ID for Auxiliary driver\n");
		goto err_ida_alloc;
	}
	adev->id = ret;
	adev->dev.release = idpf_vport_adev_release;
	adev->dev.parent = &cdev_info->pdev->dev;
	sprintf(name, "%04x.rdma.vdev", cdev_info->pdev->vendor);
	adev->name = name;

	ret = auxiliary_device_init(adev);
	if (ret)
		goto err_aux_dev_init;

	ret = auxiliary_device_add(adev);
	if (ret)
		goto err_aux_dev_add;

	return 0;

err_aux_dev_add:
	auxiliary_device_uninit(adev);
err_aux_dev_init:
	ida_free(&idpf_idc_ida, adev->id);
err_ida_alloc:
	vdev_info->adev = NULL;
	kfree(iadev);

	return ret;
}

/**
 * idpf_idc_init_aux_vport_dev - initialize vport Auxiliary Device(s)
 * @vport: virtual port data struct
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_idc_init_aux_vport_dev(struct idpf_vport *vport)
{
	struct idpf_adapter *adapter = vport->adapter;
	struct iidc_rdma_vport_dev_info *vdev_info;
	struct iidc_rdma_core_dev_info *cdev_info;
	struct virtchnl2_create_vport *vport_msg;
	int err;

	vport_msg = (struct virtchnl2_create_vport *)
			adapter->vport_params_recvd[vport->idx];

	if (!(le16_to_cpu(vport_msg->vport_flags) & VIRTCHNL2_VPORT_ENABLE_RDMA))
		return 0;

	vport->vdev_info = kzalloc(sizeof(*vdev_info), GFP_KERNEL);
	if (!vport->vdev_info)
		return -ENOMEM;

	cdev_info = vport->adapter->cdev_info;

	vdev_info = vport->vdev_info;
	vdev_info->vport_id = vport->vport_id;
	vdev_info->netdev = vport->netdev;
	vdev_info->core_adev = cdev_info->adev;

	err = idpf_plug_vport_aux_dev(cdev_info, vdev_info);
	if (err) {
		vport->vdev_info = NULL;
		kfree(vdev_info);
		return err;
	}

	return 0;
}

/**
 * idpf_idc_vdev_mtu_event - Function to handle IDC vport mtu change events
 * @vdev_info: IDC vport device info pointer
 * @event_type: type of event to pass to handler
 */
void idpf_idc_vdev_mtu_event(struct iidc_rdma_vport_dev_info *vdev_info,
			     enum iidc_rdma_event_type event_type)
{
	struct iidc_rdma_vport_auxiliary_drv *iadrv;
	struct iidc_rdma_event event = { };
	struct auxiliary_device *adev;

	if (!vdev_info)
		/* RDMA is not enabled */
		return;

	set_bit(event_type, event.type);

	device_lock(&vdev_info->adev->dev);
	adev = vdev_info->adev;
	if (!adev || !adev->dev.driver)
		goto unlock;
	iadrv = container_of(adev->dev.driver,
			     struct iidc_rdma_vport_auxiliary_drv,
			     adrv.driver);
	if (iadrv->event_handler)
		iadrv->event_handler(vdev_info, &event);
unlock:
	device_unlock(&vdev_info->adev->dev);
}

/**
 * idpf_core_adev_release - function to be mapped to aux dev's release op
 * @dev: pointer to device to free
 */
static void idpf_core_adev_release(struct device *dev)
{
	struct iidc_rdma_core_auxiliary_dev *iadev;

	iadev = container_of(dev, struct iidc_rdma_core_auxiliary_dev, adev.dev);
	kfree(iadev);
	iadev = NULL;
}

/**
 * idpf_plug_core_aux_dev - allocate and register an Auxiliary device
 * @cdev_info: IDC core device info pointer
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_plug_core_aux_dev(struct iidc_rdma_core_dev_info *cdev_info)
{
	struct iidc_rdma_core_auxiliary_dev *iadev;
	char name[IDPF_IDC_MAX_ADEV_NAME_LEN];
	struct auxiliary_device *adev;
	int ret;

	iadev = kzalloc(sizeof(*iadev), GFP_KERNEL);
	if (!iadev)
		return -ENOMEM;

	adev = &iadev->adev;
	cdev_info->adev = adev;
	iadev->cdev_info = cdev_info;

	ret = ida_alloc(&idpf_idc_ida, GFP_KERNEL);
	if (ret < 0) {
		pr_err("failed to allocate unique device ID for Auxiliary driver\n");
		goto err_ida_alloc;
	}
	adev->id = ret;
	adev->dev.release = idpf_core_adev_release;
	adev->dev.parent = &cdev_info->pdev->dev;
	sprintf(name, "%04x.rdma.core", cdev_info->pdev->vendor);
	adev->name = name;

	ret = auxiliary_device_init(adev);
	if (ret)
		goto err_aux_dev_init;

	ret = auxiliary_device_add(adev);
	if (ret)
		goto err_aux_dev_add;

	return 0;

err_aux_dev_add:
	auxiliary_device_uninit(adev);
err_aux_dev_init:
	ida_free(&idpf_idc_ida, adev->id);
err_ida_alloc:
	cdev_info->adev = NULL;
	kfree(iadev);

	return ret;
}

/**
 * idpf_unplug_aux_dev - unregister and free an Auxiliary device
 * @adev: auxiliary device struct
 */
static void idpf_unplug_aux_dev(struct auxiliary_device *adev)
{
	if (!adev)
		return;

	ida_free(&idpf_idc_ida, adev->id);

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

/**
 * idpf_idc_issue_reset_event - Function to handle reset IDC event
 * @cdev_info: IDC core device info pointer
 */
void idpf_idc_issue_reset_event(struct iidc_rdma_core_dev_info *cdev_info)
{
	enum iidc_rdma_event_type event_type = IIDC_RDMA_EVENT_WARN_RESET;
	struct iidc_rdma_core_auxiliary_drv *iadrv;
	struct iidc_rdma_event event = { };
	struct auxiliary_device *adev;

	if (!cdev_info)
		/* RDMA is not enabled */
		return;

	set_bit(event_type, event.type);

	device_lock(&cdev_info->adev->dev);

	adev = cdev_info->adev;
	if (!adev || !adev->dev.driver)
		goto unlock;

	iadrv = container_of(adev->dev.driver,
			     struct iidc_rdma_core_auxiliary_drv,
			     adrv.driver);
	if (iadrv->event_handler)
		iadrv->event_handler(cdev_info, &event);
unlock:
	device_unlock(&cdev_info->adev->dev);
}

/**
 * idpf_idc_vport_dev_up - called when CORE is ready for vport aux devs
 * @adapter: private data struct
 *
 * Return: 0 on success or error code on failure.
 */
static int idpf_idc_vport_dev_up(struct idpf_adapter *adapter)
{
	int i, err = 0;

	for (i = 0; i < adapter->num_alloc_vports; i++) {
		struct idpf_vport *vport = adapter->vports[i];

		if (!vport)
			continue;

		if (!vport->vdev_info)
			err = idpf_idc_init_aux_vport_dev(vport);
		else
			err = idpf_plug_vport_aux_dev(vport->adapter->cdev_info,
						      vport->vdev_info);
	}

	return err;
}

/**
 * idpf_idc_vport_dev_down - called CORE is leaving vport aux dev support state
 * @adapter: private data struct
 */
static void idpf_idc_vport_dev_down(struct idpf_adapter *adapter)
{
	int i;

	for (i = 0; i < adapter->num_alloc_vports; i++) {
		struct idpf_vport *vport = adapter->vports[i];

		if (!vport)
			continue;

		idpf_unplug_aux_dev(vport->vdev_info->adev);
		vport->vdev_info->adev = NULL;
	}
}

/**
 * idpf_idc_vport_dev_ctrl - Called by an Auxiliary Driver
 * @cdev_info: IDC core device info pointer
 * @up: RDMA core driver status
 *
 * This callback function is accessed by an Auxiliary Driver to indicate
 * whether core driver is ready to support vport driver load or if vport
 * drivers need to be taken down.
 *
 * Return: 0 on success or error code on failure.
 */
int idpf_idc_vport_dev_ctrl(struct iidc_rdma_core_dev_info *cdev_info, bool up)
{
	struct idpf_adapter *adapter = pci_get_drvdata(cdev_info->pdev);

	if (up)
		return idpf_idc_vport_dev_up(adapter);

	idpf_idc_vport_dev_down(adapter);

	return 0;
}
EXPORT_SYMBOL_GPL(idpf_idc_vport_dev_ctrl);

/**
 * idpf_idc_request_reset - Called by an Auxiliary Driver
 * @cdev_info: IDC core device info pointer
 * @reset_type: function, core or other
 *
 * This callback function is accessed by an Auxiliary Driver to request a reset
 * on the Auxiliary Device.
 *
 * Return: 0 on success or error code on failure.
 */
int idpf_idc_request_reset(struct iidc_rdma_core_dev_info *cdev_info,
			   enum iidc_rdma_reset_type __always_unused reset_type)
{
	struct idpf_adapter *adapter = pci_get_drvdata(cdev_info->pdev);

	if (!idpf_is_reset_in_prog(adapter)) {
		set_bit(IDPF_HR_FUNC_RESET, adapter->flags);
		queue_delayed_work(adapter->vc_event_wq,
				   &adapter->vc_event_task,
				   msecs_to_jiffies(10));
	}

	return 0;
}
EXPORT_SYMBOL_GPL(idpf_idc_request_reset);

/**
 * idpf_idc_init_msix_data - initialize MSIX data for the cdev_info structure
 * @adapter: driver private data structure
 */
static void
idpf_idc_init_msix_data(struct idpf_adapter *adapter)
{
	struct iidc_rdma_core_dev_info *cdev_info;
	struct iidc_rdma_priv_dev_info *privd;

	if (!adapter->rdma_msix_entries)
		return;

	cdev_info = adapter->cdev_info;
	privd = cdev_info->iidc_priv;

	privd->msix_entries = adapter->rdma_msix_entries;
	privd->msix_count = adapter->num_rdma_msix_entries;
}

/**
 * idpf_idc_init_aux_core_dev - initialize Auxiliary Device(s)
 * @adapter: driver private data structure
 * @ftype: PF or VF
 *
 * Return: 0 on success or error code on failure.
 */
int idpf_idc_init_aux_core_dev(struct idpf_adapter *adapter,
			       enum iidc_function_type ftype)
{
	struct iidc_rdma_core_dev_info *cdev_info;
	struct iidc_rdma_priv_dev_info *privd;
	int err, i;

	adapter->cdev_info = kzalloc(sizeof(*cdev_info), GFP_KERNEL);
	if (!adapter->cdev_info)
		return -ENOMEM;
	cdev_info = adapter->cdev_info;

	privd = kzalloc(sizeof(*privd), GFP_KERNEL);
	if (!privd) {
		err = -ENOMEM;
		goto err_privd_alloc;
	}

	cdev_info->iidc_priv = privd;
	cdev_info->pdev = adapter->pdev;
	cdev_info->rdma_protocol = IIDC_RDMA_PROTOCOL_ROCEV2;
	privd->ftype = ftype;

	privd->mapped_mem_regions =
		kcalloc(adapter->hw.num_lan_regs,
			sizeof(struct iidc_rdma_lan_mapped_mem_region),
			GFP_KERNEL);
	if (!privd->mapped_mem_regions) {
		err = -ENOMEM;
		goto err_plug_aux_dev;
	}

	privd->num_memory_regions = cpu_to_le16(adapter->hw.num_lan_regs);
	for (i = 0; i < adapter->hw.num_lan_regs; i++) {
		privd->mapped_mem_regions[i].region_addr =
			adapter->hw.lan_regs[i].vaddr;
		privd->mapped_mem_regions[i].size =
			cpu_to_le64(adapter->hw.lan_regs[i].addr_len);
		privd->mapped_mem_regions[i].start_offset =
			cpu_to_le64(adapter->hw.lan_regs[i].addr_start);
	}

	idpf_idc_init_msix_data(adapter);

	err = idpf_plug_core_aux_dev(cdev_info);
	if (err)
		goto err_free_mem_regions;

	return 0;

err_free_mem_regions:
	kfree(privd->mapped_mem_regions);
	privd->mapped_mem_regions = NULL;
err_plug_aux_dev:
	kfree(privd);
err_privd_alloc:
	kfree(cdev_info);
	adapter->cdev_info = NULL;

	return err;
}

/**
 * idpf_idc_deinit_core_aux_device - de-initialize Auxiliary Device(s)
 * @cdev_info: IDC core device info pointer
 */
void idpf_idc_deinit_core_aux_device(struct iidc_rdma_core_dev_info *cdev_info)
{
	struct iidc_rdma_priv_dev_info *privd;

	if (!cdev_info)
		return;

	idpf_unplug_aux_dev(cdev_info->adev);

	privd = cdev_info->iidc_priv;
	kfree(privd->mapped_mem_regions);
	kfree(privd);
	kfree(cdev_info);
}

/**
 * idpf_idc_deinit_vport_aux_device - de-initialize Auxiliary Device(s)
 * @vdev_info: IDC vport device info pointer
 */
void idpf_idc_deinit_vport_aux_device(struct iidc_rdma_vport_dev_info *vdev_info)
{
	if (!vdev_info)
		return;

	idpf_unplug_aux_dev(vdev_info->adev);

	kfree(vdev_info);
}
