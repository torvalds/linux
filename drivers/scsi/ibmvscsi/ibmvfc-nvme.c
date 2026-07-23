/* SPDX-License-Identifier: GPL-2.0-or-later */

/*
 * ibmvfc-nvme.c -- IBM Power Virtual Fibre Channel NVMeoF HBA driver
 *
 * Written By: Tyrel Datwyler <tyreld@linux.ibm.com>, IBM Corporation
 *
 * Copyright (C) IBM Corporation, 2026
 */

#include <scsi/scsi_transport_fc.h>

#include "ibmvfc-nvme.h"

static void ibmvfc_nvme_localport_delete(struct nvme_fc_local_port *lport)
{
}

static void ibmvfc_nvme_remoteport_delete(struct nvme_fc_remote_port *rport)
{
}

static int ibmvfc_nvme_ls_req(struct nvme_fc_local_port *lport,
			      struct nvme_fc_remote_port *rport,
			      struct nvmefc_ls_req *ls_req)
{
	return 0;
}

static void ibmvfc_nvme_ls_abort(struct nvme_fc_local_port *lport,
				struct nvme_fc_remote_port *rport,
				struct nvmefc_ls_req *ls_abort)
{
}

static int ibmvfc_nvme_fcp_io(struct nvme_fc_local_port *lport,
			      struct nvme_fc_remote_port *rport,
			      void *hw_queue_handle,
			      struct nvmefc_fcp_req *fcp_req)
{
	return 0;
}

static void ibmvfc_nvme_fcp_abort(struct nvme_fc_local_port *lport,
				  struct nvme_fc_remote_port *rport,
				  void *hw_queue_handle,
				  struct nvmefc_fcp_req *abort_req)
{
}

static struct nvme_fc_port_template ibmvfc_nvme_fc_transport = {
	.localport_delete	= ibmvfc_nvme_localport_delete,
	.remoteport_delete	= ibmvfc_nvme_remoteport_delete,
	.create_queue		= NULL,
	.delete_queue		= NULL,
	.ls_req			= ibmvfc_nvme_ls_req,
	.ls_abort		= ibmvfc_nvme_ls_abort,
	.fcp_io			= ibmvfc_nvme_fcp_io,
	.fcp_abort		= ibmvfc_nvme_fcp_abort,
	.map_queues		= NULL,
	.max_hw_queues		= IBMVFC_NVME_HW_QUEUES,
	.max_sgl_segments	= 1024,
	.max_dif_sgl_segments	= 64,
	.dma_boundary		= 0xFFFFFFFF,
	.local_priv_sz		= sizeof(struct ibmvfc_host *),
	.remote_priv_sz		= sizeof(struct ibmvfc_target *),
	.lsrqst_priv_sz		= sizeof(struct ibmvfc_event *),
	.fcprqst_priv_sz	= sizeof(struct ibmvfc_event *),
};

int ibmvfc_nvme_register_remoteport(struct ibmvfc_target *tgt)
{
	if (!IS_ENABLED(CONFIG_NVME_FC))
		return 0;

	return 0;
}

void ibmvfc_nvme_unregister_remoteport(struct ibmvfc_target *tgt)
{
	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;
}

int ibmvfc_nvme_register(struct ibmvfc_host *vhost)
{
	if (!IS_ENABLED(CONFIG_NVME_FC))
		return 0;

	return 0;
}

void ibmvfc_nvme_unregister(struct ibmvfc_host *vhost)
{
	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;
}
