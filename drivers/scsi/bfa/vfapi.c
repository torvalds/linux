/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  vfapi.c Fabric module implementation.
 */

#include "fcs_fabric.h"
#include "fcs_trcmod.h"

BFA_TRC_FILE(FCS, VFAPI);

/**
 *  fcs_vf_api virtual fabrics API
 */

/**
 * 		Enable VF mode.
 *
 * @param[in]		fcs		fcs module instance
 * @param[in]		vf_id		default vf_id of port, FC_VF_ID_NULL
 * 					to use standard default vf_id of 1.
 *
 * @retval	BFA_STATUS_OK		vf mode is enabled
 * @retval	BFA_STATUS_BUSY		Port is active. Port must be disabled
 *					before VF mode can be enabled.
 */
bfa_status_t
bfa_fcs_vf_mode_enable(struct bfa_fcs_s *fcs, u16 vf_id)
{
	return BFA_STATUS_OK;
}

/**
 * 		Disable VF mode.
 *
 * @param[in]		fcs		fcs module instance
 *
 * @retval	BFA_STATUS_OK		vf mode is disabled
 * @retval	BFA_STATUS_BUSY		VFs are present and being used. All
 * 					VFs must be deleted before disabling
 *					VF mode.
 */
bfa_status_t
bfa_fcs_vf_mode_disable(struct bfa_fcs_s *fcs)
{
	return BFA_STATUS_OK;
}

/**
 * 		Create a new VF instance.
 *
 *  A new VF is created using the given VF configuration. A VF is identified
 *  by VF id. No duplicate VF creation is allowed with the same VF id. Once
 *  a VF is created, VF is automatically started after link initialization
 *  and EVFP exchange is completed.
 *
 * 	param[in] vf		- 	FCS vf data structure. Memory is
 *					allocated by caller (driver)
 * 	param[in] fcs 		- 	FCS module
 * 	param[in] vf_cfg	- 	VF configuration
 * 	param[in] vf_drv 	- 	Opaque handle back to the driver's
 *					virtual vf structure
 *
 * 	retval BFA_STATUS_OK VF creation is successful
 * 	retval BFA_STATUS_FAILED VF creation failed
 * 	retval BFA_STATUS_EEXIST A VF exists with the given vf_id
 */
bfa_status_t
bfa_fcs_vf_create(bfa_fcs_vf_t *vf, struct bfa_fcs_s *fcs, u16 vf_id,
		  struct bfa_port_cfg_s *port_cfg, struct bfad_vf_s *vf_drv)
{
	bfa_trc(fcs, vf_id);
	return BFA_STATUS_OK;
}

/**
 *  	Use this function to delete a BFA VF object. VF object should
 * 		be stopped before this function call.
 *
 * 	param[in] vf - pointer to bfa_vf_t.
 *
 * 	retval BFA_STATUS_OK	On vf deletion success
 * 	retval BFA_STATUS_BUSY VF is not in a stopped state
 * 	retval BFA_STATUS_INPROGRESS VF deletion in in progress
 */
bfa_status_t
bfa_fcs_vf_delete(bfa_fcs_vf_t *vf)
{
	bfa_trc(vf->fcs, vf->vf_id);
	return BFA_STATUS_OK;
}

/**
 *  	Start participation in VF. This triggers login to the virtual fabric.
 *
 * 	param[in] vf - pointer to bfa_vf_t.
 *
 * 	return None
 */
void
bfa_fcs_vf_start(bfa_fcs_vf_t *vf)
{
	bfa_trc(vf->fcs, vf->vf_id);
}

/**
 *  	Logout with the virtual fabric.
 *
 * 	param[in] vf - pointer to bfa_vf_t.
 *
 * 	retval BFA_STATUS_OK 	On success.
 * 	retval BFA_STATUS_INPROGRESS VF is being stopped.
 */
bfa_status_t
bfa_fcs_vf_stop(bfa_fcs_vf_t *vf)
{
	bfa_trc(vf->fcs, vf->vf_id);
	return BFA_STATUS_OK;
}

/**
 *  	Returns attributes of the given VF.
 *
 * 	param[in] 	vf			pointer to bfa_vf_t.
 * 	param[out] vf_attr 	vf attributes returned
 *
 * 	return None
 */
void
bfa_fcs_vf_get_attr(bfa_fcs_vf_t *vf, struct bfa_vf_attr_s *vf_attr)
{
	bfa_trc(vf->fcs, vf->vf_id);
}

/**
 * 		Return statistics associated with the given vf.
 *
 * 	param[in] 	vf			pointer to bfa_vf_t.
 * 	param[out] vf_stats 	vf statistics returned
 *
 *  @return None
 */
void
bfa_fcs_vf_get_stats(bfa_fcs_vf_t *vf, struct bfa_vf_stats_s *vf_stats)
{
	bfa_os_memcpy(vf_stats, &vf->stats, sizeof(struct bfa_vf_stats_s));
	return;
}

void
/**
 * 		clear statistics associated with the given vf.
 *
 * 	param[in] 	vf			pointer to bfa_vf_t.
 *
 *  @return None
 */
bfa_fcs_vf_clear_stats(bfa_fcs_vf_t *vf)
{
	bfa_os_memset(&vf->stats, 0, sizeof(struct bfa_vf_stats_s));
	return;
}

/**
 *  	Returns FCS vf structure for a given vf_id.
 *
 * 	param[in] 	vf_id		- VF_ID
 *
 * 	return
 * 		If lookup succeeds, retuns fcs vf object, otherwise returns NULL
 */
bfa_fcs_vf_t   *
bfa_fcs_vf_lookup(struct bfa_fcs_s *fcs, u16 vf_id)
{
	bfa_trc(fcs, vf_id);
	if (vf_id == FC_VF_ID_NULL)
		return &fcs->fabric;

	/**
	 * @todo vf support
	 */

	return NULL;
}

/**
 *  	Returns driver VF structure for a given FCS vf.
 *
 * 	param[in] 	vf		- pointer to bfa_vf_t
 *
 * 	return Driver VF structure
 */
struct bfad_vf_s      *
bfa_fcs_vf_get_drv_vf(bfa_fcs_vf_t *vf)
{
	bfa_assert(vf);
	bfa_trc(vf->fcs, vf->vf_id);
	return vf->vf_drv;
}

/**
 *  	Return the list of VFs configured.
 *
 * 	param[in]	fcs	fcs module instance
 * 	param[out] 	vf_ids	returned list of vf_ids
 * 	param[in,out] 	nvfs	in:size of vf_ids array,
 * 				out:total elements present,
 * 				actual elements returned is limited by the size
 *
 * 	return Driver VF structure
 */
void
bfa_fcs_vf_list(struct bfa_fcs_s *fcs, u16 *vf_ids, int *nvfs)
{
	bfa_trc(fcs, *nvfs);
}

/**
 *  	Return the list of all VFs visible from fabric.
 *
 * 	param[in]	fcs	fcs module instance
 * 	param[out] 	vf_ids	returned list of vf_ids
 * 	param[in,out] 	nvfs	in:size of vf_ids array,
 *				out:total elements present,
 * 				actual elements returned is limited by the size
 *
 * 	return Driver VF structure
 */
void
bfa_fcs_vf_list_all(struct bfa_fcs_s *fcs, u16 *vf_ids, int *nvfs)
{
	bfa_trc(fcs, *nvfs);
}

/**
 * 		Return the list of local logical ports present in the given VF.
 *
 * 	param[in]	vf	vf for which logical ports are returned
 * 	param[out] 	lpwwn	returned logical port wwn list
 * 	param[in,out] 	nlports	in:size of lpwwn list;
 *				out:total elements present,
 * 				actual elements returned is limited by the size
 *
 */
void
bfa_fcs_vf_get_ports(bfa_fcs_vf_t *vf, wwn_t lpwwn[], int *nlports)
{
	struct list_head        *qe;
	struct bfa_fcs_vport_s *vport;
	int             i;
	struct bfa_fcs_s      *fcs;

	if (vf == NULL || lpwwn == NULL || *nlports == 0)
		return;

	fcs = vf->fcs;

	bfa_trc(fcs, vf->vf_id);
	bfa_trc(fcs, (u32) *nlports);

	i = 0;
	lpwwn[i++] = vf->bport.port_cfg.pwwn;

	list_for_each(qe, &vf->vport_q) {
		if (i >= *nlports)
			break;

		vport = (struct bfa_fcs_vport_s *) qe;
		lpwwn[i++] = vport->lport.port_cfg.pwwn;
	}

	bfa_trc(fcs, i);
	*nlports = i;
	return;
}


