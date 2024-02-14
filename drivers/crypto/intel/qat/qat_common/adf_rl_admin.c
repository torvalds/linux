// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/dma-mapping.h>
#include <linux/pci.h>

#include "adf_admin.h"
#include "adf_accel_devices.h"
#include "adf_rl_admin.h"

static void
prep_admin_req_msg(struct rl_sla *sla, dma_addr_t dma_addr,
		   struct icp_qat_fw_init_admin_sla_config_params *fw_params,
		   struct icp_qat_fw_init_admin_req *req, bool is_update)
{
	req->cmd_id = is_update ? ICP_QAT_FW_RL_UPDATE : ICP_QAT_FW_RL_ADD;
	req->init_cfg_ptr = dma_addr;
	req->init_cfg_sz = sizeof(*fw_params);
	req->node_id = sla->node_id;
	req->node_type = sla->type;
	req->rp_count = sla->ring_pairs_cnt;
	req->svc_type = sla->srv;
}

static void
prep_admin_req_params(struct adf_accel_dev *accel_dev, struct rl_sla *sla,
		      struct icp_qat_fw_init_admin_sla_config_params *fw_params)
{
	fw_params->pcie_in_cir =
		adf_rl_calculate_pci_bw(accel_dev, sla->cir, sla->srv, false);
	fw_params->pcie_in_pir =
		adf_rl_calculate_pci_bw(accel_dev, sla->pir, sla->srv, false);
	fw_params->pcie_out_cir =
		adf_rl_calculate_pci_bw(accel_dev, sla->cir, sla->srv, true);
	fw_params->pcie_out_pir =
		adf_rl_calculate_pci_bw(accel_dev, sla->pir, sla->srv, true);

	fw_params->slice_util_cir =
		adf_rl_calculate_slice_tokens(accel_dev, sla->cir, sla->srv);
	fw_params->slice_util_pir =
		adf_rl_calculate_slice_tokens(accel_dev, sla->pir, sla->srv);

	fw_params->ae_util_cir =
		adf_rl_calculate_ae_cycles(accel_dev, sla->cir, sla->srv);
	fw_params->ae_util_pir =
		adf_rl_calculate_ae_cycles(accel_dev, sla->pir, sla->srv);

	memcpy(fw_params->rp_ids, sla->ring_pairs_ids,
	       sizeof(sla->ring_pairs_ids));
}

int adf_rl_send_admin_init_msg(struct adf_accel_dev *accel_dev,
			       struct rl_slice_cnt *slices_int)
{
	struct icp_qat_fw_init_admin_slice_cnt slices_resp = { };
	int ret;

	ret = adf_send_admin_rl_init(accel_dev, &slices_resp);
	if (ret)
		return ret;

	slices_int->dcpr_cnt = slices_resp.dcpr_cnt;
	slices_int->pke_cnt = slices_resp.pke_cnt;
	/* For symmetric crypto, slice tokens are relative to the UCS slice */
	slices_int->cph_cnt = slices_resp.ucs_cnt;

	return 0;
}

int adf_rl_send_admin_add_update_msg(struct adf_accel_dev *accel_dev,
				     struct rl_sla *sla, bool is_update)
{
	struct icp_qat_fw_init_admin_sla_config_params *fw_params;
	struct icp_qat_fw_init_admin_req req = { };
	dma_addr_t dma_addr;
	int ret;

	fw_params = dma_alloc_coherent(&GET_DEV(accel_dev), sizeof(*fw_params),
				       &dma_addr, GFP_KERNEL);
	if (!fw_params)
		return -ENOMEM;

	prep_admin_req_params(accel_dev, sla, fw_params);
	prep_admin_req_msg(sla, dma_addr, fw_params, &req, is_update);
	ret = adf_send_admin_rl_add_update(accel_dev, &req);

	dma_free_coherent(&GET_DEV(accel_dev), sizeof(*fw_params), fw_params,
			  dma_addr);

	return ret;
}

int adf_rl_send_admin_delete_msg(struct adf_accel_dev *accel_dev, u16 node_id,
				 u8 node_type)
{
	return adf_send_admin_rl_delete(accel_dev, node_id, node_type);
}
