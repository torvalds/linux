// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

static DEFINE_XARRAY_ALLOC1(ice_aux_id);

/**
 * ice_get_auxiliary_drv - retrieve iidc_rdma_core_auxiliary_drv struct
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 *
 * This function has to be called with a device_lock on the
 * cdev->adev.dev to avoid race conditions.
 *
 * Return: pointer to the matched auxiliary driver struct
 */
static struct iidc_rdma_core_auxiliary_drv *
ice_get_auxiliary_drv(struct iidc_rdma_core_dev_info *cdev)
{
	struct auxiliary_device *adev;

	adev = cdev->adev;
	if (!adev || !adev->dev.driver)
		return NULL;

	return container_of(adev->dev.driver,
			    struct iidc_rdma_core_auxiliary_drv, adrv.driver);
}

/**
 * ice_send_event_to_aux - send event to RDMA AUX driver
 * @pf: pointer to PF struct
 * @event: event struct
 */
void ice_send_event_to_aux(struct ice_pf *pf, struct iidc_rdma_event *event)
{
	struct iidc_rdma_core_auxiliary_drv *iadrv;
	struct iidc_rdma_core_dev_info *cdev;

	if (WARN_ON_ONCE(!in_task()))
		return;

	cdev = pf->cdev_info;
	if (!cdev)
		return;

	mutex_lock(&pf->adev_mutex);
	if (!cdev->adev)
		goto finish;

	device_lock(&cdev->adev->dev);
	iadrv = ice_get_auxiliary_drv(cdev);
	if (iadrv && iadrv->event_handler)
		iadrv->event_handler(cdev, event);
	device_unlock(&cdev->adev->dev);
finish:
	mutex_unlock(&pf->adev_mutex);
}

/**
 * ice_add_rdma_qset - Add Leaf Node for RDMA Qset
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @qset: Resource to be allocated
 *
 * Return: Zero on success or error code encountered
 */
int ice_add_rdma_qset(struct iidc_rdma_core_dev_info *cdev,
		      struct iidc_rdma_qset_params *qset)
{
	u16 max_rdmaqs[ICE_MAX_TRAFFIC_CLASS];
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	u32 qset_teid;
	u16 qs_handle;
	int status;
	int i;

	if (WARN_ON(!cdev || !qset))
		return -EINVAL;

	pf = pci_get_drvdata(cdev->pdev);
	dev = ice_pf_to_dev(pf);

	if (!ice_is_rdma_ena(pf))
		return -EINVAL;

	vsi = ice_get_main_vsi(pf);
	if (!vsi) {
		dev_err(dev, "RDMA QSet invalid VSI\n");
		return -EINVAL;
	}

	ice_for_each_traffic_class(i)
		max_rdmaqs[i] = 0;

	max_rdmaqs[qset->tc]++;
	qs_handle = qset->qs_handle;

	status = ice_cfg_vsi_rdma(vsi->port_info, vsi->idx, vsi->tc_cfg.ena_tc,
				  max_rdmaqs);
	if (status) {
		dev_err(dev, "Failed VSI RDMA Qset config\n");
		return status;
	}

	status = ice_ena_vsi_rdma_qset(vsi->port_info, vsi->idx, qset->tc,
				       &qs_handle, 1, &qset_teid);
	if (status) {
		dev_err(dev, "Failed VSI RDMA Qset enable\n");
		return status;
	}
	qset->teid = qset_teid;

	return 0;
}
EXPORT_SYMBOL_GPL(ice_add_rdma_qset);

/**
 * ice_del_rdma_qset - Delete leaf node for RDMA Qset
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @qset: Resource to be freed
 *
 * Return: Zero on success, error code on failure
 */
int ice_del_rdma_qset(struct iidc_rdma_core_dev_info *cdev,
		      struct iidc_rdma_qset_params *qset)
{
	struct ice_vsi *vsi;
	struct ice_pf *pf;
	u32 teid;
	u16 q_id;

	if (WARN_ON(!cdev || !qset))
		return -EINVAL;

	pf = pci_get_drvdata(cdev->pdev);
	vsi = ice_find_vsi(pf, qset->vport_id);
	if (!vsi) {
		dev_err(ice_pf_to_dev(pf), "RDMA Invalid VSI\n");
		return -EINVAL;
	}

	q_id = qset->qs_handle;
	teid = qset->teid;

	return ice_dis_vsi_rdma_qset(vsi->port_info, 1, &teid, &q_id);
}
EXPORT_SYMBOL_GPL(ice_del_rdma_qset);

/**
 * ice_rdma_request_reset - accept request from RDMA to perform a reset
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @reset_type: type of reset
 *
 * Return: Zero on success, error code on failure
 */
int ice_rdma_request_reset(struct iidc_rdma_core_dev_info *cdev,
			   enum iidc_rdma_reset_type reset_type)
{
	enum ice_reset_req reset;
	struct ice_pf *pf;

	if (WARN_ON(!cdev))
		return -EINVAL;

	pf = pci_get_drvdata(cdev->pdev);

	switch (reset_type) {
	case IIDC_FUNC_RESET:
		reset = ICE_RESET_PFR;
		break;
	case IIDC_DEV_RESET:
		reset = ICE_RESET_CORER;
		break;
	default:
		return -EINVAL;
	}

	return ice_schedule_reset(pf, reset);
}
EXPORT_SYMBOL_GPL(ice_rdma_request_reset);

/**
 * ice_rdma_update_vsi_filter - update main VSI filters for RDMA
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @vsi_id: VSI HW idx to update filter on
 * @enable: bool whether to enable or disable filters
 *
 * Return: Zero on success, error code on failure
 */
int ice_rdma_update_vsi_filter(struct iidc_rdma_core_dev_info *cdev,
			       u16 vsi_id, bool enable)
{
	struct ice_vsi *vsi;
	struct ice_pf *pf;
	int status;

	if (WARN_ON(!cdev))
		return -EINVAL;

	pf = pci_get_drvdata(cdev->pdev);
	vsi = ice_find_vsi(pf, vsi_id);
	if (!vsi)
		return -EINVAL;

	status = ice_cfg_rdma_fltr(&pf->hw, vsi->idx, enable);
	if (status) {
		dev_err(ice_pf_to_dev(pf), "Failed to  %sable RDMA filtering\n",
			enable ? "en" : "dis");
	} else {
		if (enable)
			vsi->info.q_opt_flags |= ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
		else
			vsi->info.q_opt_flags &= ~ICE_AQ_VSI_Q_OPT_PE_FLTR_EN;
	}

	return status;
}
EXPORT_SYMBOL_GPL(ice_rdma_update_vsi_filter);

/**
 * ice_alloc_rdma_qvector - alloc vector resources reserved for RDMA driver
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @entry: MSI-X entry to be removed
 *
 * Return: Zero on success, error code on failure
 */
int ice_alloc_rdma_qvector(struct iidc_rdma_core_dev_info *cdev,
			   struct msix_entry *entry)
{
	struct msi_map map;
	struct ice_pf *pf;

	if (WARN_ON(!cdev))
		return -EINVAL;

	pf = pci_get_drvdata(cdev->pdev);
	map = ice_alloc_irq(pf, true);
	if (map.index < 0)
		return -ENOMEM;

	entry->entry = map.index;
	entry->vector = map.virq;

	return 0;
}
EXPORT_SYMBOL_GPL(ice_alloc_rdma_qvector);

/**
 * ice_free_rdma_qvector - free vector resources reserved for RDMA driver
 * @cdev: pointer to iidc_rdma_core_dev_info struct
 * @entry: MSI-X entry to be removed
 */
void ice_free_rdma_qvector(struct iidc_rdma_core_dev_info *cdev,
			   struct msix_entry *entry)
{
	struct msi_map map;
	struct ice_pf *pf;

	if (WARN_ON(!cdev || !entry))
		return;

	pf = pci_get_drvdata(cdev->pdev);

	map.index = entry->entry;
	map.virq = entry->vector;
	ice_free_irq(pf, map);
}
EXPORT_SYMBOL_GPL(ice_free_rdma_qvector);

/**
 * ice_adev_release - function to be mapped to AUX dev's release op
 * @dev: pointer to device to free
 */
static void ice_adev_release(struct device *dev)
{
	struct iidc_rdma_core_auxiliary_dev *iadev;

	iadev = container_of(dev, struct iidc_rdma_core_auxiliary_dev,
			     adev.dev);
	kfree(iadev);
}

/**
 * ice_plug_aux_dev - allocate and register AUX device
 * @pf: pointer to pf struct
 *
 * Return: Zero on success, error code on failure
 */
int ice_plug_aux_dev(struct ice_pf *pf)
{
	struct iidc_rdma_core_auxiliary_dev *iadev;
	struct iidc_rdma_core_dev_info *cdev;
	struct auxiliary_device *adev;
	int ret;

	/* if this PF doesn't support a technology that requires auxiliary
	 * devices, then gracefully exit
	 */
	if (!ice_is_rdma_ena(pf))
		return 0;

	cdev = pf->cdev_info;
	if (!cdev)
		return -ENODEV;

	iadev = kzalloc(sizeof(*iadev), GFP_KERNEL);
	if (!iadev)
		return -ENOMEM;

	adev = &iadev->adev;
	iadev->cdev_info = cdev;

	adev->id = pf->aux_idx;
	adev->dev.release = ice_adev_release;
	adev->dev.parent = &pf->pdev->dev;
	adev->name = cdev->rdma_protocol & IIDC_RDMA_PROTOCOL_ROCEV2 ?
		"roce" : "iwarp";

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(iadev);
		return ret;
	}

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	mutex_lock(&pf->adev_mutex);
	cdev->adev = adev;
	mutex_unlock(&pf->adev_mutex);
	set_bit(ICE_FLAG_AUX_DEV_CREATED, pf->flags);

	return 0;
}

/* ice_unplug_aux_dev - unregister and free AUX device
 * @pf: pointer to pf struct
 */
void ice_unplug_aux_dev(struct ice_pf *pf)
{
	struct auxiliary_device *adev;

	if (!test_and_clear_bit(ICE_FLAG_AUX_DEV_CREATED, pf->flags))
		return;

	mutex_lock(&pf->adev_mutex);
	adev = pf->cdev_info->adev;
	pf->cdev_info->adev = NULL;
	mutex_unlock(&pf->adev_mutex);

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

/**
 * ice_init_rdma - initializes PF for RDMA use
 * @pf: ptr to ice_pf
 */
int ice_init_rdma(struct ice_pf *pf)
{
	struct iidc_rdma_priv_dev_info *privd;
	struct device *dev = &pf->pdev->dev;
	struct iidc_rdma_core_dev_info *cdev;
	int ret;

	if (!ice_is_rdma_ena(pf)) {
		dev_warn(dev, "RDMA is not supported on this device\n");
		return 0;
	}

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	pf->cdev_info = cdev;

	privd = kzalloc(sizeof(*privd), GFP_KERNEL);
	if (!privd) {
		ret = -ENOMEM;
		goto err_privd_alloc;
	}

	privd->pf_id = pf->hw.pf_id;
	ret = xa_alloc(&ice_aux_id, &pf->aux_idx, NULL, XA_LIMIT(1, INT_MAX),
		       GFP_KERNEL);
	if (ret) {
		dev_err(dev, "Failed to allocate device ID for AUX driver\n");
		ret = -ENOMEM;
		goto err_alloc_xa;
	}

	cdev->iidc_priv = privd;
	privd->netdev = pf->vsi[0]->netdev;

	privd->hw_addr = (u8 __iomem *)pf->hw.hw_addr;
	cdev->pdev = pf->pdev;
	privd->vport_id = pf->vsi[0]->vsi_num;

	pf->cdev_info->rdma_protocol |= IIDC_RDMA_PROTOCOL_ROCEV2;
	ice_setup_dcb_qos_info(pf, &privd->qos_info);
	ret = ice_plug_aux_dev(pf);
	if (ret)
		goto err_plug_aux_dev;
	return 0;

err_plug_aux_dev:
	pf->cdev_info->adev = NULL;
	xa_erase(&ice_aux_id, pf->aux_idx);
err_alloc_xa:
	kfree(privd);
err_privd_alloc:
	kfree(cdev);
	pf->cdev_info = NULL;

	return ret;
}

/**
 * ice_deinit_rdma - deinitialize RDMA on PF
 * @pf: ptr to ice_pf
 */
void ice_deinit_rdma(struct ice_pf *pf)
{
	if (!ice_is_rdma_ena(pf))
		return;

	ice_unplug_aux_dev(pf);
	xa_erase(&ice_aux_id, pf->aux_idx);
	kfree(pf->cdev_info->iidc_priv);
	kfree(pf->cdev_info);
	pf->cdev_info = NULL;
}
