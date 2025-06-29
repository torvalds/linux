// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2021, 2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include "iwl-drv.h"
#include "runtime.h"
#include "fw/api/commands.h"

static void iwl_parse_shared_mem_22000(struct iwl_fw_runtime *fwrt,
				       struct iwl_rx_packet *pkt)
{
	struct iwl_shared_mem_cfg *mem_cfg = (void *)pkt->data;
	int i, lmac;
	int lmac_num = le32_to_cpu(mem_cfg->lmac_num);
	u8 api_ver = iwl_fw_lookup_notif_ver(fwrt->fw, SYSTEM_GROUP,
					     SHARED_MEM_CFG_CMD, 0);

	if (WARN_ON(lmac_num > ARRAY_SIZE(mem_cfg->lmac_smem)))
		return;

	fwrt->smem_cfg.num_lmacs = lmac_num;
	fwrt->smem_cfg.num_txfifo_entries =
		ARRAY_SIZE(mem_cfg->lmac_smem[0].txfifo_size);
	fwrt->smem_cfg.rxfifo2_size = le32_to_cpu(mem_cfg->rxfifo2_size);

	if (api_ver >= 4 &&
	    !WARN_ON_ONCE(iwl_rx_packet_payload_len(pkt) < sizeof(*mem_cfg))) {
		fwrt->smem_cfg.rxfifo2_control_size =
			le32_to_cpu(mem_cfg->rxfifo2_control_size);
	}

	for (lmac = 0; lmac < lmac_num; lmac++) {
		struct iwl_shared_mem_lmac_cfg *lmac_cfg =
			&mem_cfg->lmac_smem[lmac];

		for (i = 0; i < ARRAY_SIZE(lmac_cfg->txfifo_size); i++)
			fwrt->smem_cfg.lmac[lmac].txfifo_size[i] =
				le32_to_cpu(lmac_cfg->txfifo_size[i]);
		fwrt->smem_cfg.lmac[lmac].rxfifo1_size =
			le32_to_cpu(lmac_cfg->rxfifo1_size);
	}
}

static void iwl_parse_shared_mem(struct iwl_fw_runtime *fwrt,
				 struct iwl_rx_packet *pkt)
{
	struct iwl_shared_mem_cfg_v2 *mem_cfg = (void *)pkt->data;
	int i;

	fwrt->smem_cfg.num_lmacs = 1;

	fwrt->smem_cfg.num_txfifo_entries = ARRAY_SIZE(mem_cfg->txfifo_size);
	for (i = 0; i < ARRAY_SIZE(mem_cfg->txfifo_size); i++)
		fwrt->smem_cfg.lmac[0].txfifo_size[i] =
			le32_to_cpu(mem_cfg->txfifo_size[i]);

	fwrt->smem_cfg.lmac[0].rxfifo1_size =
		le32_to_cpu(mem_cfg->rxfifo_size[0]);
	fwrt->smem_cfg.rxfifo2_size = le32_to_cpu(mem_cfg->rxfifo_size[1]);

	/* new API has more data, from rxfifo_addr field and on */
	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG)) {
		BUILD_BUG_ON(sizeof(fwrt->smem_cfg.internal_txfifo_size) !=
			     sizeof(mem_cfg->internal_txfifo_size));

		fwrt->smem_cfg.internal_txfifo_addr =
			le32_to_cpu(mem_cfg->internal_txfifo_addr);

		for (i = 0;
		     i < ARRAY_SIZE(fwrt->smem_cfg.internal_txfifo_size);
		     i++)
			fwrt->smem_cfg.internal_txfifo_size[i] =
				le32_to_cpu(mem_cfg->internal_txfifo_size[i]);
	}
}

void iwl_get_shared_mem_conf(struct iwl_fw_runtime *fwrt)
{
	struct iwl_host_cmd cmd = {
		.flags = CMD_WANT_SKB,
		.data = { NULL, },
		.len = { 0, },
	};
	struct iwl_rx_packet *pkt;
	int ret;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG))
		cmd.id = WIDE_ID(SYSTEM_GROUP, SHARED_MEM_CFG_CMD);
	else
		cmd.id = SHARED_MEM_CFG;

	ret = iwl_trans_send_cmd(fwrt->trans, &cmd);

	if (ret) {
		WARN(ret != -ERFKILL,
		     "Could not send the SMEM command: %d\n", ret);
		return;
	}

	pkt = cmd.resp_pkt;
	if (fwrt->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_22000)
		iwl_parse_shared_mem_22000(fwrt, pkt);
	else
		iwl_parse_shared_mem(fwrt, pkt);

	IWL_DEBUG_INFO(fwrt, "SHARED MEM CFG: got memory offsets/sizes\n");

	iwl_free_resp(&cmd);
}
IWL_EXPORT_SYMBOL(iwl_get_shared_mem_conf);
