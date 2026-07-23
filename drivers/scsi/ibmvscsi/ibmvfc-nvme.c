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
	struct ibmvfc_host *vhost = lport->private;

	vhost->nvme_local_port = NULL;
	complete(&vhost->nvme_delete_done);
}

static void ibmvfc_nvme_remoteport_delete(struct nvme_fc_remote_port *rport)
{
	struct ibmvfc_target *tgt = rport->private;

	tgt->nvme_remote_port = NULL;
	complete(&tgt->nvme_delete_done);
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
	struct ibmvfc_host *vhost = tgt->vhost;
	struct nvme_fc_port_info pinfo;
	int rc;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return 0;

	if (!vhost->nvme_local_port) {
		dev_err(vhost->dev, "Attempt to register NVMe fc remoteport without valid localport\n");
		return -EINVAL;
	}

	memset(&pinfo, 0, sizeof(struct nvme_fc_port_info));
	pinfo.node_name = tgt->ids.node_name;
	pinfo.port_name = tgt->ids.port_name;
	pinfo.port_id = tgt->ids.port_id;
	pinfo.port_role = FC_PORT_ROLE_NVME_TARGET;

	rc = nvme_fc_register_remoteport(vhost->nvme_local_port, &pinfo,
					 &tgt->nvme_remote_port);

	if (!rc) {
		ibmvfc_log(vhost, 2, "register_remoteport: traddr=nn-0x%llx:pn-0x%llx PortID:%x\n",
			   pinfo.node_name, pinfo.port_name, pinfo.port_id);
		tgt->nvme_remote_port->private = tgt;
	}

	return rc;
}

void ibmvfc_nvme_unregister_remoteport(struct ibmvfc_target *tgt)
{
	struct ibmvfc_host *vhost = tgt->vhost;
	struct nvme_fc_remote_port *rport = tgt->nvme_remote_port;
	int rc;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	if (!tgt->nvme_remote_port)
		return;

	ibmvfc_log(vhost, 2, "unregister_remoteport: traddr=nn-0x%llx:pn-0x%llx PortID:%x\n",
		   rport->node_name, rport->port_name, rport->port_id);
	init_completion(&tgt->nvme_delete_done);
	rc = nvme_fc_unregister_remoteport(tgt->nvme_remote_port);

	if (!rc) {
		wait_for_completion(&tgt->nvme_delete_done);
	}
}

int ibmvfc_nvme_register(struct ibmvfc_host *vhost)
{
	struct nvme_fc_port_info pinfo;
	int rc;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return 0;

	pinfo.node_name = fc_host_node_name(vhost->host);
	pinfo.port_name = fc_host_port_name(vhost->host);
	pinfo.port_id = fc_host_port_id(vhost->host);
	pinfo.port_role = FC_PORT_ROLE_NVME_INITIATOR;
	pinfo.dev_loss_tmo = 0;

	rc = nvme_fc_register_localport(&pinfo, &ibmvfc_nvme_fc_transport,
					vhost->dev, &vhost->nvme_local_port);

	if (!rc) {
		ibmvfc_log(vhost, 2, "register_localport: host-traddr=nn-0x%llx:pn-0x%llx on portID:%x\n",
			   pinfo.node_name, pinfo.port_name, pinfo.port_id);
		vhost->nvme_local_port->private = vhost;
	} else
		dev_err(vhost->dev, "Failed to register NVMe fc localport (%d)\n", rc);

	return rc;
}

void ibmvfc_nvme_unregister(struct ibmvfc_host *vhost)
{
	int rc;

	if (!IS_ENABLED(CONFIG_NVME_FC))
		return;

	if (vhost->nvme_local_port) {
		init_completion(&vhost->nvme_delete_done);
		rc = nvme_fc_unregister_localport(vhost->nvme_local_port);
		if (!rc)
			wait_for_completion(&vhost->nvme_delete_done);
	}
}
