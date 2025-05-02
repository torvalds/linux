// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

/* Inter-Driver Communication */
#include "ice.h"
#include "ice_lib.h"
#include "ice_dcb_lib.h"

static DEFINE_XARRAY_ALLOC1(ice_aux_id);

/**
 * ice_get_auxiliary_drv - retrieve iidc_auxiliary_drv struct
 * @pf: pointer to PF struct
 *
 * This function has to be called with a device_lock on the
 * pf->adev.dev to avoid race conditions.
 */
static struct iidc_auxiliary_drv *ice_get_auxiliary_drv(struct ice_pf *pf)
{
	struct auxiliary_device *adev;

	adev = pf->adev;
	if (!adev || !adev->dev.driver)
		return NULL;

	return container_of(adev->dev.driver, struct iidc_auxiliary_drv,
			    adrv.driver);
}

/**
 * ice_send_event_to_aux - send event to RDMA AUX driver
 * @pf: pointer to PF struct
 * @event: event struct
 */
void ice_send_event_to_aux(struct ice_pf *pf, struct iidc_event *event)
{
	struct iidc_auxiliary_drv *iadrv;

	if (WARN_ON_ONCE(!in_task()))
		return;

	mutex_lock(&pf->adev_mutex);
	if (!pf->adev)
		goto finish;

	device_lock(&pf->adev->dev);
	iadrv = ice_get_auxiliary_drv(pf);
	if (iadrv && iadrv->event_handler)
		iadrv->event_handler(pf, event);
	device_unlock(&pf->adev->dev);
finish:
	mutex_unlock(&pf->adev_mutex);
}

/**
 * ice_add_rdma_qset - Add Leaf Node for RDMA Qset
 * @pf: PF struct
 * @qset: Resource to be allocated
 */
int ice_add_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset)
{
	u16 max_rdmaqs[ICE_MAX_TRAFFIC_CLASS];
	struct ice_vsi *vsi;
	struct device *dev;
	u32 qset_teid;
	u16 qs_handle;
	int status;
	int i;

	if (WARN_ON(!pf || !qset))
		return -EINVAL;

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
	vsi->qset_handle[qset->tc] = qset->qs_handle;
	qset->teid = qset_teid;

	return 0;
}
EXPORT_SYMBOL_GPL(ice_add_rdma_qset);

/**
 * ice_del_rdma_qset - Delete leaf node for RDMA Qset
 * @pf: PF struct
 * @qset: Resource to be freed
 */
int ice_del_rdma_qset(struct ice_pf *pf, struct iidc_rdma_qset_params *qset)
{
	struct ice_vsi *vsi;
	u32 teid;
	u16 q_id;

	if (WARN_ON(!pf || !qset))
		return -EINVAL;

	vsi = ice_find_vsi(pf, qset->vport_id);
	if (!vsi) {
		dev_err(ice_pf_to_dev(pf), "RDMA Invalid VSI\n");
		return -EINVAL;
	}

	q_id = qset->qs_handle;
	teid = qset->teid;

	vsi->qset_handle[qset->tc] = 0;

	return ice_dis_vsi_rdma_qset(vsi->port_info, 1, &teid, &q_id);
}
EXPORT_SYMBOL_GPL(ice_del_rdma_qset);

/**
 * ice_rdma_request_reset - accept request from RDMA to perform a reset
 * @pf: struct for PF
 * @reset_type: type of reset
 */
int ice_rdma_request_reset(struct ice_pf *pf, enum iidc_reset_type reset_type)
{
	enum ice_reset_req reset;

	if (WARN_ON(!pf))
		return -EINVAL;

	switch (reset_type) {
	case IIDC_PFR:
		reset = ICE_RESET_PFR;
		break;
	case IIDC_CORER:
		reset = ICE_RESET_CORER;
		break;
	case IIDC_GLOBR:
		reset = ICE_RESET_GLOBR;
		break;
	default:
		dev_err(ice_pf_to_dev(pf), "incorrect reset request\n");
		return -EINVAL;
	}

	return ice_schedule_reset(pf, reset);
}
EXPORT_SYMBOL_GPL(ice_rdma_request_reset);

/**
 * ice_rdma_update_vsi_filter - update main VSI filters for RDMA
 * @pf: pointer to struct for PF
 * @vsi_id: VSI HW idx to update filter on
 * @enable: bool whether to enable or disable filters
 */
int ice_rdma_update_vsi_filter(struct ice_pf *pf, u16 vsi_id, bool enable)
{
	struct ice_vsi *vsi;
	int status;

	if (WARN_ON(!pf))
		return -EINVAL;

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
 * ice_get_qos_params - parse QoS params for RDMA consumption
 * @pf: pointer to PF struct
 * @qos: set of QoS values
 */
void ice_get_qos_params(struct ice_pf *pf, struct iidc_qos_params *qos)
{
	struct ice_dcbx_cfg *dcbx_cfg;
	unsigned int i;
	u32 up2tc;

	dcbx_cfg = &pf->hw.port_info->qos_cfg.local_dcbx_cfg;
	up2tc = rd32(&pf->hw, PRTDCB_TUP2TC);

	qos->num_tc = ice_dcb_get_num_tc(dcbx_cfg);
	for (i = 0; i < IIDC_MAX_USER_PRIORITY; i++)
		qos->up2tc[i] = (up2tc >> (i * 3)) & 0x7;

	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		qos->tc_info[i].rel_bw = dcbx_cfg->etscfg.tcbwtable[i];

	qos->pfc_mode = dcbx_cfg->pfc_mode;
	if (qos->pfc_mode == IIDC_DSCP_PFC_MODE)
		for (i = 0; i < IIDC_MAX_DSCP_MAPPING; i++)
			qos->dscp_map[i] = dcbx_cfg->dscp_map[i];
}
EXPORT_SYMBOL_GPL(ice_get_qos_params);

int ice_alloc_rdma_qvector(struct ice_pf *pf, struct msix_entry *entry)
{
	struct msi_map map = ice_alloc_irq(pf, true);

	if (map.index < 0)
		return -ENOMEM;

	entry->entry = map.index;
	entry->vector = map.virq;

	return 0;
}
EXPORT_SYMBOL_GPL(ice_alloc_rdma_qvector);

/**
 * ice_free_rdma_qvector - free vector resources reserved for RDMA driver
 * @pf: board private structure to initialize
 * @entry: MSI-X entry to be removed
 */
void ice_free_rdma_qvector(struct ice_pf *pf, struct msix_entry *entry)
{
	struct msi_map map;

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
	struct iidc_auxiliary_dev *iadev;

	iadev = container_of(dev, struct iidc_auxiliary_dev, adev.dev);
	kfree(iadev);
}

/**
 * ice_plug_aux_dev - allocate and register AUX device
 * @pf: pointer to pf struct
 */
int ice_plug_aux_dev(struct ice_pf *pf)
{
	struct iidc_auxiliary_dev *iadev;
	struct auxiliary_device *adev;
	int ret;

	/* if this PF doesn't support a technology that requires auxiliary
	 * devices, then gracefully exit
	 */
	if (!ice_is_rdma_ena(pf))
		return 0;

	iadev = kzalloc(sizeof(*iadev), GFP_KERNEL);
	if (!iadev)
		return -ENOMEM;

	adev = &iadev->adev;
	iadev->pf = pf;

	adev->id = pf->aux_idx;
	adev->dev.release = ice_adev_release;
	adev->dev.parent = &pf->pdev->dev;
	adev->name = pf->rdma_mode & IIDC_RDMA_PROTOCOL_ROCEV2 ? "roce" : "iwarp";

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
	pf->adev = adev;
	mutex_unlock(&pf->adev_mutex);

	return 0;
}

/* ice_unplug_aux_dev - unregister and free AUX device
 * @pf: pointer to pf struct
 */
void ice_unplug_aux_dev(struct ice_pf *pf)
{
	struct auxiliary_device *adev;

	mutex_lock(&pf->adev_mutex);
	adev = pf->adev;
	pf->adev = NULL;
	mutex_unlock(&pf->adev_mutex);

	if (adev) {
		auxiliary_device_delete(adev);
		auxiliary_device_uninit(adev);
	}
}

/**
 * ice_init_rdma - initializes PF for RDMA use
 * @pf: ptr to ice_pf
 */
int ice_init_rdma(struct ice_pf *pf)
{
	struct device *dev = &pf->pdev->dev;
	int ret;

	if (!ice_is_rdma_ena(pf)) {
		dev_warn(dev, "RDMA is not supported on this device\n");
		return 0;
	}

	ret = xa_alloc(&ice_aux_id, &pf->aux_idx, NULL, XA_LIMIT(1, INT_MAX),
		       GFP_KERNEL);
	if (ret) {
		dev_err(dev, "Failed to allocate device ID for AUX driver\n");
		return -ENOMEM;
	}

	pf->rdma_mode |= IIDC_RDMA_PROTOCOL_ROCEV2;
	ret = ice_plug_aux_dev(pf);
	if (ret)
		goto err_plug_aux_dev;
	return 0;

err_plug_aux_dev:
	pf->adev = NULL;
	xa_erase(&ice_aux_id, pf->aux_idx);
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
}
