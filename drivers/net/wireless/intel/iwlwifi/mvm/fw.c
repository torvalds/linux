// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <net/mac80211.h>
#include <linux/netdevice.h>
#include <linux/dmi.h>

#include "iwl-trans.h"
#include "iwl-op-mode.h"
#include "fw/img.h"
#include "iwl-debug.h"
#include "iwl-prph.h"
#include "fw/acpi.h"
#include "fw/pnvm.h"

#include "mvm.h"
#include "fw/dbg.h"
#include "iwl-phy-db.h"
#include "iwl-modparams.h"
#include "iwl-nvm-parse.h"
#include "time-sync.h"

#define MVM_UCODE_ALIVE_TIMEOUT	(HZ)
#define MVM_UCODE_CALIB_TIMEOUT	(2 * HZ)

#define IWL_TAS_US_MCC 0x5553
#define IWL_TAS_CANADA_MCC 0x4341

struct iwl_mvm_alive_data {
	bool valid;
	u32 scd_base_addr;
};

static int iwl_send_tx_ant_cfg(struct iwl_mvm *mvm, u8 valid_tx_ant)
{
	struct iwl_tx_ant_cfg_cmd tx_ant_cmd = {
		.valid = cpu_to_le32(valid_tx_ant),
	};

	IWL_DEBUG_FW(mvm, "select valid tx ant: %u\n", valid_tx_ant);
	return iwl_mvm_send_cmd_pdu(mvm, TX_ANT_CONFIGURATION_CMD, 0,
				    sizeof(tx_ant_cmd), &tx_ant_cmd);
}

static int iwl_send_rss_cfg_cmd(struct iwl_mvm *mvm)
{
	int i;
	struct iwl_rss_config_cmd cmd = {
		.flags = cpu_to_le32(IWL_RSS_ENABLE),
		.hash_mask = BIT(IWL_RSS_HASH_TYPE_IPV4_TCP) |
			     BIT(IWL_RSS_HASH_TYPE_IPV4_UDP) |
			     BIT(IWL_RSS_HASH_TYPE_IPV4_PAYLOAD) |
			     BIT(IWL_RSS_HASH_TYPE_IPV6_TCP) |
			     BIT(IWL_RSS_HASH_TYPE_IPV6_UDP) |
			     BIT(IWL_RSS_HASH_TYPE_IPV6_PAYLOAD),
	};

	if (mvm->trans->num_rx_queues == 1)
		return 0;

	/* Do not direct RSS traffic to Q 0 which is our fallback queue */
	for (i = 0; i < ARRAY_SIZE(cmd.indirection_table); i++)
		cmd.indirection_table[i] =
			1 + (i % (mvm->trans->num_rx_queues - 1));
	netdev_rss_key_fill(cmd.secret_key, sizeof(cmd.secret_key));

	return iwl_mvm_send_cmd_pdu(mvm, RSS_CONFIG_CMD, 0, sizeof(cmd), &cmd);
}

static int iwl_mvm_send_dqa_cmd(struct iwl_mvm *mvm)
{
	struct iwl_dqa_enable_cmd dqa_cmd = {
		.cmd_queue = cpu_to_le32(IWL_MVM_DQA_CMD_QUEUE),
	};
	u32 cmd_id = WIDE_ID(DATA_PATH_GROUP, DQA_ENABLE_CMD);
	int ret;

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, sizeof(dqa_cmd), &dqa_cmd);
	if (ret)
		IWL_ERR(mvm, "Failed to send DQA enabling command: %d\n", ret);
	else
		IWL_DEBUG_FW(mvm, "Working in DQA mode\n");

	return ret;
}

void iwl_mvm_mfu_assert_dump_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mfu_assert_dump_notif *mfu_dump_notif = (void *)pkt->data;
	__le32 *dump_data = mfu_dump_notif->data;
	int n_words = le32_to_cpu(mfu_dump_notif->data_size) / sizeof(__le32);
	int i;

	if (mfu_dump_notif->index_num == 0)
		IWL_INFO(mvm, "MFUART assert id 0x%x occurred\n",
			 le32_to_cpu(mfu_dump_notif->assert_id));

	for (i = 0; i < n_words; i++)
		IWL_DEBUG_INFO(mvm,
			       "MFUART assert dump, dword %u: 0x%08x\n",
			       le16_to_cpu(mfu_dump_notif->index_num) *
			       n_words + i,
			       le32_to_cpu(dump_data[i]));
}

static bool iwl_alive_fn(struct iwl_notif_wait_data *notif_wait,
			 struct iwl_rx_packet *pkt, void *data)
{
	unsigned int pkt_len = iwl_rx_packet_payload_len(pkt);
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_mvm_alive_data *alive_data = data;
	struct iwl_umac_alive *umac;
	struct iwl_lmac_alive *lmac1;
	struct iwl_lmac_alive *lmac2 = NULL;
	u16 status;
	u32 lmac_error_event_table, umac_error_table;
	u32 version = iwl_fw_lookup_notif_ver(mvm->fw, LEGACY_GROUP,
					      UCODE_ALIVE_NTFY, 0);
	u32 i;


	if (version == 6) {
		struct iwl_alive_ntf_v6 *palive;

		if (pkt_len < sizeof(*palive))
			return false;

		palive = (void *)pkt->data;
		mvm->trans->dbg.imr_data.imr_enable =
			le32_to_cpu(palive->imr.enabled);
		mvm->trans->dbg.imr_data.imr_size =
			le32_to_cpu(palive->imr.size);
		mvm->trans->dbg.imr_data.imr2sram_remainbyte =
			mvm->trans->dbg.imr_data.imr_size;
		mvm->trans->dbg.imr_data.imr_base_addr =
			palive->imr.base_addr;
		mvm->trans->dbg.imr_data.imr_curr_addr =
			le64_to_cpu(mvm->trans->dbg.imr_data.imr_base_addr);
		IWL_DEBUG_FW(mvm, "IMR Enabled: 0x0%x  size 0x0%x Address 0x%016llx\n",
			     mvm->trans->dbg.imr_data.imr_enable,
			     mvm->trans->dbg.imr_data.imr_size,
			     le64_to_cpu(mvm->trans->dbg.imr_data.imr_base_addr));

		if (!mvm->trans->dbg.imr_data.imr_enable) {
			for (i = 0; i < ARRAY_SIZE(mvm->trans->dbg.active_regions); i++) {
				struct iwl_ucode_tlv *reg_tlv;
				struct iwl_fw_ini_region_tlv *reg;

				reg_tlv = mvm->trans->dbg.active_regions[i];
				if (!reg_tlv)
					continue;

				reg = (void *)reg_tlv->data;
				/*
				 * We have only one DRAM IMR region, so we
				 * can break as soon as we find the first
				 * one.
				 */
				if (reg->type == IWL_FW_INI_REGION_DRAM_IMR) {
					mvm->trans->dbg.unsupported_region_msk |= BIT(i);
					break;
				}
			}
		}
	}

	if (version >= 5) {
		struct iwl_alive_ntf_v5 *palive;

		if (pkt_len < sizeof(*palive))
			return false;

		palive = (void *)pkt->data;
		umac = &palive->umac_data;
		lmac1 = &palive->lmac_data[0];
		lmac2 = &palive->lmac_data[1];
		status = le16_to_cpu(palive->status);

		mvm->trans->sku_id[0] = le32_to_cpu(palive->sku_id.data[0]);
		mvm->trans->sku_id[1] = le32_to_cpu(palive->sku_id.data[1]);
		mvm->trans->sku_id[2] = le32_to_cpu(palive->sku_id.data[2]);

		IWL_DEBUG_FW(mvm, "Got sku_id: 0x0%x 0x0%x 0x0%x\n",
			     mvm->trans->sku_id[0],
			     mvm->trans->sku_id[1],
			     mvm->trans->sku_id[2]);
	} else if (iwl_rx_packet_payload_len(pkt) == sizeof(struct iwl_alive_ntf_v4)) {
		struct iwl_alive_ntf_v4 *palive;

		if (pkt_len < sizeof(*palive))
			return false;

		palive = (void *)pkt->data;
		umac = &palive->umac_data;
		lmac1 = &palive->lmac_data[0];
		lmac2 = &palive->lmac_data[1];
		status = le16_to_cpu(palive->status);
	} else if (iwl_rx_packet_payload_len(pkt) ==
		   sizeof(struct iwl_alive_ntf_v3)) {
		struct iwl_alive_ntf_v3 *palive3;

		if (pkt_len < sizeof(*palive3))
			return false;

		palive3 = (void *)pkt->data;
		umac = &palive3->umac_data;
		lmac1 = &palive3->lmac_data;
		status = le16_to_cpu(palive3->status);
	} else {
		WARN(1, "unsupported alive notification (size %d)\n",
		     iwl_rx_packet_payload_len(pkt));
		/* get timeout later */
		return false;
	}

	lmac_error_event_table =
		le32_to_cpu(lmac1->dbg_ptrs.error_event_table_ptr);
	iwl_fw_lmac1_set_alive_err_table(mvm->trans, lmac_error_event_table);

	if (lmac2)
		mvm->trans->dbg.lmac_error_event_table[1] =
			le32_to_cpu(lmac2->dbg_ptrs.error_event_table_ptr);

	umac_error_table = le32_to_cpu(umac->dbg_ptrs.error_info_addr) &
							~FW_ADDR_CACHE_CONTROL;

	if (umac_error_table) {
		if (umac_error_table >=
		    mvm->trans->cfg->min_umac_error_event_table) {
			iwl_fw_umac_set_alive_err_table(mvm->trans,
							umac_error_table);
		} else {
			IWL_ERR(mvm,
				"Not valid error log pointer 0x%08X for %s uCode\n",
				umac_error_table,
				(mvm->fwrt.cur_fw_img == IWL_UCODE_INIT) ?
				"Init" : "RT");
		}
	}

	alive_data->scd_base_addr = le32_to_cpu(lmac1->dbg_ptrs.scd_base_ptr);
	alive_data->valid = status == IWL_ALIVE_STATUS_OK;

	IWL_DEBUG_FW(mvm,
		     "Alive ucode status 0x%04x revision 0x%01X 0x%01X\n",
		     status, lmac1->ver_type, lmac1->ver_subtype);

	if (lmac2)
		IWL_DEBUG_FW(mvm, "Alive ucode CDB\n");

	IWL_DEBUG_FW(mvm,
		     "UMAC version: Major - 0x%x, Minor - 0x%x\n",
		     le32_to_cpu(umac->umac_major),
		     le32_to_cpu(umac->umac_minor));

	iwl_fwrt_update_fw_versions(&mvm->fwrt, lmac1, umac);

	return true;
}

static bool iwl_wait_init_complete(struct iwl_notif_wait_data *notif_wait,
				   struct iwl_rx_packet *pkt, void *data)
{
	WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);

	return true;
}

static bool iwl_wait_phy_db_entry(struct iwl_notif_wait_data *notif_wait,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_phy_db *phy_db = data;

	if (pkt->hdr.cmd != CALIB_RES_NOTIF_PHY_DB) {
		WARN_ON(pkt->hdr.cmd != INIT_COMPLETE_NOTIF);
		return true;
	}

	WARN_ON(iwl_phy_db_set_section(phy_db, pkt));

	return false;
}

static void iwl_mvm_print_pd_notification(struct iwl_mvm *mvm)
{
#define IWL_FW_PRINT_REG_INFO(reg_name) \
	IWL_ERR(mvm, #reg_name ": 0x%x\n", iwl_read_umac_prph(trans, reg_name))

	struct iwl_trans *trans = mvm->trans;
	enum iwl_device_family device_family = trans->trans_cfg->device_family;

	if (device_family < IWL_DEVICE_FAMILY_8000)
		return;

	if (device_family <= IWL_DEVICE_FAMILY_9000)
		IWL_FW_PRINT_REG_INFO(WFPM_ARC1_PD_NOTIFICATION);
	else
		IWL_FW_PRINT_REG_INFO(WFPM_LMAC1_PD_NOTIFICATION);

	IWL_FW_PRINT_REG_INFO(HPM_SECONDARY_DEVICE_STATE);

	/* print OPT info */
	IWL_FW_PRINT_REG_INFO(WFPM_MAC_OTP_CFG7_ADDR);
	IWL_FW_PRINT_REG_INFO(WFPM_MAC_OTP_CFG7_DATA);
}

static int iwl_mvm_load_ucode_wait_alive(struct iwl_mvm *mvm,
					 enum iwl_ucode_type ucode_type)
{
	struct iwl_notification_wait alive_wait;
	struct iwl_mvm_alive_data alive_data = {};
	const struct fw_img *fw;
	int ret;
	enum iwl_ucode_type old_type = mvm->fwrt.cur_fw_img;
	static const u16 alive_cmd[] = { UCODE_ALIVE_NTFY };
	bool run_in_rfkill =
		ucode_type == IWL_UCODE_INIT || iwl_mvm_has_unified_ucode(mvm);
	u8 count;
	struct iwl_pc_data *pc_data;

	if (ucode_type == IWL_UCODE_REGULAR &&
	    iwl_fw_dbg_conf_usniffer(mvm->fw, FW_DBG_START_FROM_ALIVE) &&
	    !(fw_has_capa(&mvm->fw->ucode_capa,
			  IWL_UCODE_TLV_CAPA_USNIFFER_UNIFIED)))
		fw = iwl_get_ucode_image(mvm->fw, IWL_UCODE_REGULAR_USNIFFER);
	else
		fw = iwl_get_ucode_image(mvm->fw, ucode_type);
	if (WARN_ON(!fw))
		return -EINVAL;
	iwl_fw_set_current_image(&mvm->fwrt, ucode_type);
	clear_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);

	iwl_init_notification_wait(&mvm->notif_wait, &alive_wait,
				   alive_cmd, ARRAY_SIZE(alive_cmd),
				   iwl_alive_fn, &alive_data);

	/*
	 * We want to load the INIT firmware even in RFKILL
	 * For the unified firmware case, the ucode_type is not
	 * INIT, but we still need to run it.
	 */
	ret = iwl_trans_start_fw(mvm->trans, fw, run_in_rfkill);
	if (ret) {
		iwl_fw_set_current_image(&mvm->fwrt, old_type);
		iwl_remove_notification(&mvm->notif_wait, &alive_wait);
		return ret;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the ALIVE notification here.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &alive_wait,
				    MVM_UCODE_ALIVE_TIMEOUT);

	if (mvm->trans->trans_cfg->device_family ==
	    IWL_DEVICE_FAMILY_AX210) {
		/* print these registers regardless of alive fail/success */
		IWL_INFO(mvm, "WFPM_UMAC_PD_NOTIFICATION: 0x%x\n",
			 iwl_read_umac_prph(mvm->trans, WFPM_ARC1_PD_NOTIFICATION));
		IWL_INFO(mvm, "WFPM_LMAC2_PD_NOTIFICATION: 0x%x\n",
			 iwl_read_umac_prph(mvm->trans, WFPM_LMAC2_PD_NOTIFICATION));
		IWL_INFO(mvm, "WFPM_AUTH_KEY_0: 0x%x\n",
			 iwl_read_umac_prph(mvm->trans, SB_MODIFY_CFG_FLAG));
		IWL_INFO(mvm, "CNVI_SCU_SEQ_DATA_DW9: 0x%x\n",
			 iwl_read_prph(mvm->trans, CNVI_SCU_SEQ_DATA_DW9));
	}

	if (ret) {
		struct iwl_trans *trans = mvm->trans;

		/* SecBoot info */
		if (trans->trans_cfg->device_family >=
					IWL_DEVICE_FAMILY_22000) {
			IWL_ERR(mvm,
				"SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
				iwl_read_umac_prph(trans, UMAG_SB_CPU_1_STATUS),
				iwl_read_umac_prph(trans,
						   UMAG_SB_CPU_2_STATUS));
		} else if (trans->trans_cfg->device_family >=
			   IWL_DEVICE_FAMILY_8000) {
			IWL_ERR(mvm,
				"SecBoot CPU1 Status: 0x%x, CPU2 Status: 0x%x\n",
				iwl_read_prph(trans, SB_CPU_1_STATUS),
				iwl_read_prph(trans, SB_CPU_2_STATUS));
		}

		iwl_mvm_print_pd_notification(mvm);

		/* LMAC/UMAC PC info */
		if (trans->trans_cfg->device_family >=
					IWL_DEVICE_FAMILY_22000) {
			pc_data = trans->dbg.pc_data;
			for (count = 0; count < trans->dbg.num_pc;
			     count++, pc_data++)
				IWL_ERR(mvm, "%s: 0x%x\n",
					pc_data->pc_name,
					pc_data->pc_address);
		} else if (trans->trans_cfg->device_family >=
					IWL_DEVICE_FAMILY_9000) {
			IWL_ERR(mvm, "UMAC PC: 0x%x\n",
				iwl_read_umac_prph(trans,
						   UREG_UMAC_CURRENT_PC));
			IWL_ERR(mvm, "LMAC PC: 0x%x\n",
				iwl_read_umac_prph(trans,
						   UREG_LMAC1_CURRENT_PC));
			if (iwl_mvm_is_cdb_supported(mvm))
				IWL_ERR(mvm, "LMAC2 PC: 0x%x\n",
					iwl_read_umac_prph(trans,
						UREG_LMAC2_CURRENT_PC));
		}

		if (ret == -ETIMEDOUT && !mvm->pldr_sync)
			iwl_fw_dbg_error_collect(&mvm->fwrt,
						 FW_DBG_TRIGGER_ALIVE_TIMEOUT);

		iwl_fw_set_current_image(&mvm->fwrt, old_type);
		return ret;
	}

	if (!alive_data.valid) {
		IWL_ERR(mvm, "Loaded ucode is not valid!\n");
		iwl_fw_set_current_image(&mvm->fwrt, old_type);
		return -EIO;
	}

	/* if reached this point, Alive notification was received */
	iwl_mei_alive_notif(true);

	ret = iwl_pnvm_load(mvm->trans, &mvm->notif_wait,
			    &mvm->fw->ucode_capa);
	if (ret) {
		IWL_ERR(mvm, "Timeout waiting for PNVM load!\n");
		iwl_fw_set_current_image(&mvm->fwrt, old_type);
		return ret;
	}

	iwl_trans_fw_alive(mvm->trans, alive_data.scd_base_addr);

	/*
	 * Note: all the queues are enabled as part of the interface
	 * initialization, but in firmware restart scenarios they
	 * could be stopped, so wake them up. In firmware restart,
	 * mac80211 will have the queues stopped as well until the
	 * reconfiguration completes. During normal startup, they
	 * will be empty.
	 */

	memset(&mvm->queue_info, 0, sizeof(mvm->queue_info));
	/*
	 * Set a 'fake' TID for the command queue, since we use the
	 * hweight() of the tid_bitmap as a refcount now. Not that
	 * we ever even consider the command queue as one we might
	 * want to reuse, but be safe nevertheless.
	 */
	mvm->queue_info[IWL_MVM_DQA_CMD_QUEUE].tid_bitmap =
		BIT(IWL_MAX_TID_COUNT + 2);

	set_bit(IWL_MVM_STATUS_FIRMWARE_RUNNING, &mvm->status);
#ifdef CONFIG_IWLWIFI_DEBUGFS
	iwl_fw_set_dbg_rec_on(&mvm->fwrt);
#endif

	/*
	 * All the BSSes in the BSS table include the GP2 in the system
	 * at the beacon Rx time, this is of course no longer relevant
	 * since we are resetting the firmware.
	 * Purge all the BSS table.
	 */
	cfg80211_bss_flush(mvm->hw->wiphy);

	return 0;
}

static void iwl_mvm_phy_filter_init(struct iwl_mvm *mvm,
				    struct iwl_phy_specific_cfg *phy_filters)
{
#ifdef CONFIG_ACPI
	*phy_filters = mvm->phy_filters;
#endif /* CONFIG_ACPI */
}

#if defined(CONFIG_ACPI) && defined(CONFIG_EFI)
static int iwl_mvm_sgom_init(struct iwl_mvm *mvm)
{
	u8 cmd_ver;
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP,
			      SAR_OFFSET_MAPPING_TABLE_CMD),
		.flags = 0,
		.data[0] = &mvm->fwrt.sgom_table,
		.len[0] =  sizeof(mvm->fwrt.sgom_table),
		.dataflags[0] = IWL_HCMD_DFL_NOCOPY,
	};

	if (!mvm->fwrt.sgom_enabled) {
		IWL_DEBUG_RADIO(mvm, "SGOM table is disabled\n");
		return 0;
	}

	cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd.id,
					IWL_FW_CMD_VER_UNKNOWN);

	if (cmd_ver != 2) {
		IWL_DEBUG_RADIO(mvm, "command version is unsupported. version = %d\n",
				cmd_ver);
		return 0;
	}

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret < 0)
		IWL_ERR(mvm, "failed to send SAR_OFFSET_MAPPING_CMD (%d)\n", ret);

	return ret;
}
#else

static int iwl_mvm_sgom_init(struct iwl_mvm *mvm)
{
	return 0;
}
#endif

static int iwl_send_phy_cfg_cmd(struct iwl_mvm *mvm)
{
	u32 cmd_id = PHY_CONFIGURATION_CMD;
	struct iwl_phy_cfg_cmd_v3 phy_cfg_cmd;
	enum iwl_ucode_type ucode_type = mvm->fwrt.cur_fw_img;
	u8 cmd_ver;
	size_t cmd_size;

	if (iwl_mvm_has_unified_ucode(mvm) &&
	    !mvm->trans->cfg->tx_with_siso_diversity)
		return 0;

	if (mvm->trans->cfg->tx_with_siso_diversity) {
		/*
		 * TODO: currently we don't set the antenna but letting the NIC
		 * to decide which antenna to use. This should come from BIOS.
		 */
		phy_cfg_cmd.phy_cfg =
			cpu_to_le32(FW_PHY_CFG_CHAIN_SAD_ENABLED);
	}

	/* Set parameters */
	phy_cfg_cmd.phy_cfg = cpu_to_le32(iwl_mvm_get_phy_config(mvm));

	/* set flags extra PHY configuration flags from the device's cfg */
	phy_cfg_cmd.phy_cfg |=
		cpu_to_le32(mvm->trans->trans_cfg->extra_phy_cfg_flags);

	phy_cfg_cmd.calib_control.event_trigger =
		mvm->fw->default_calib[ucode_type].event_trigger;
	phy_cfg_cmd.calib_control.flow_trigger =
		mvm->fw->default_calib[ucode_type].flow_trigger;

	cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					IWL_FW_CMD_VER_UNKNOWN);
	if (cmd_ver >= 3)
		iwl_mvm_phy_filter_init(mvm, &phy_cfg_cmd.phy_specific_cfg);

	IWL_DEBUG_INFO(mvm, "Sending Phy CFG command: 0x%x\n",
		       phy_cfg_cmd.phy_cfg);
	cmd_size = (cmd_ver == 3) ? sizeof(struct iwl_phy_cfg_cmd_v3) :
				    sizeof(struct iwl_phy_cfg_cmd_v1);
	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, cmd_size, &phy_cfg_cmd);
}

static int iwl_run_unified_mvm_ucode(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait init_wait;
	struct iwl_nvm_access_complete_cmd nvm_complete = {};
	struct iwl_init_extended_cfg_cmd init_cfg = {
		.init_flags = cpu_to_le32(BIT(IWL_INIT_NVM)),
	};
	static const u16 init_complete[] = {
		INIT_COMPLETE_NOTIF,
	};
	int ret;

	if (mvm->trans->cfg->tx_with_siso_diversity)
		init_cfg.init_flags |= cpu_to_le32(BIT(IWL_INIT_PHY));

	lockdep_assert_held(&mvm->mutex);

	mvm->rfkill_safe_init_done = false;

	iwl_init_notification_wait(&mvm->notif_wait,
				   &init_wait,
				   init_complete,
				   ARRAY_SIZE(init_complete),
				   iwl_wait_init_complete,
				   NULL);

	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_EARLY, NULL);

	/* Will also start the device */
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
	if (ret) {
		IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
		goto error;
	}
	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_AFTER_ALIVE,
			       NULL);

	/* Send init config command to mark that we are sending NVM access
	 * commands
	 */
	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(SYSTEM_GROUP,
						INIT_EXTENDED_CFG_CMD),
				   CMD_SEND_IN_RFKILL,
				   sizeof(init_cfg), &init_cfg);
	if (ret) {
		IWL_ERR(mvm, "Failed to run init config command: %d\n",
			ret);
		goto error;
	}

	/* Load NVM to NIC if needed */
	if (mvm->nvm_file_name) {
		ret = iwl_read_external_nvm(mvm->trans, mvm->nvm_file_name,
					    mvm->nvm_sections);
		if (ret)
			goto error;
		ret = iwl_mvm_load_nvm_to_nic(mvm);
		if (ret)
			goto error;
	}

	if (IWL_MVM_PARSE_NVM && !mvm->nvm_data) {
		ret = iwl_nvm_init(mvm);
		if (ret) {
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			goto error;
		}
	}

	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(REGULATORY_AND_NVM_GROUP,
						NVM_ACCESS_COMPLETE),
				   CMD_SEND_IN_RFKILL,
				   sizeof(nvm_complete), &nvm_complete);
	if (ret) {
		IWL_ERR(mvm, "Failed to run complete NVM access: %d\n",
			ret);
		goto error;
	}

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to run PHY configuration: %d\n",
			ret);
		goto error;
	}

	/* We wait for the INIT complete notification */
	ret = iwl_wait_notification(&mvm->notif_wait, &init_wait,
				    MVM_UCODE_ALIVE_TIMEOUT);
	if (ret)
		return ret;

	/* Read the NVM only at driver load time, no need to do this twice */
	if (!IWL_MVM_PARSE_NVM && !mvm->nvm_data) {
		mvm->nvm_data = iwl_get_nvm(mvm->trans, mvm->fw);
		if (IS_ERR(mvm->nvm_data)) {
			ret = PTR_ERR(mvm->nvm_data);
			mvm->nvm_data = NULL;
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			return ret;
		}
	}

	mvm->rfkill_safe_init_done = true;

	return 0;

error:
	iwl_remove_notification(&mvm->notif_wait, &init_wait);
	return ret;
}

int iwl_run_init_mvm_ucode(struct iwl_mvm *mvm)
{
	struct iwl_notification_wait calib_wait;
	static const u16 init_complete[] = {
		INIT_COMPLETE_NOTIF,
		CALIB_RES_NOTIF_PHY_DB
	};
	int ret;

	if (iwl_mvm_has_unified_ucode(mvm))
		return iwl_run_unified_mvm_ucode(mvm);

	lockdep_assert_held(&mvm->mutex);

	mvm->rfkill_safe_init_done = false;

	iwl_init_notification_wait(&mvm->notif_wait,
				   &calib_wait,
				   init_complete,
				   ARRAY_SIZE(init_complete),
				   iwl_wait_phy_db_entry,
				   mvm->phy_db);

	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_EARLY, NULL);

	/* Will also start the device */
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_INIT);
	if (ret) {
		IWL_ERR(mvm, "Failed to start INIT ucode: %d\n", ret);
		goto remove_notif;
	}

	if (mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_8000) {
		ret = iwl_mvm_send_bt_init_conf(mvm);
		if (ret)
			goto remove_notif;
	}

	/* Read the NVM only at driver load time, no need to do this twice */
	if (!mvm->nvm_data) {
		ret = iwl_nvm_init(mvm);
		if (ret) {
			IWL_ERR(mvm, "Failed to read NVM: %d\n", ret);
			goto remove_notif;
		}
	}

	/* In case we read the NVM from external file, load it to the NIC */
	if (mvm->nvm_file_name) {
		ret = iwl_mvm_load_nvm_to_nic(mvm);
		if (ret)
			goto remove_notif;
	}

	WARN_ONCE(mvm->nvm_data->nvm_version < mvm->trans->cfg->nvm_ver,
		  "Too old NVM version (0x%0x, required = 0x%0x)",
		  mvm->nvm_data->nvm_version, mvm->trans->cfg->nvm_ver);

	/*
	 * abort after reading the nvm in case RF Kill is on, we will complete
	 * the init seq later when RF kill will switch to off
	 */
	if (iwl_mvm_is_radio_hw_killed(mvm)) {
		IWL_DEBUG_RF_KILL(mvm,
				  "jump over all phy activities due to RF kill\n");
		goto remove_notif;
	}

	mvm->rfkill_safe_init_done = true;

	/* Send TX valid antennas before triggering calibrations */
	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto remove_notif;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n",
			ret);
		goto remove_notif;
	}

	/*
	 * Some things may run in the background now, but we
	 * just wait for the calibration complete notification.
	 */
	ret = iwl_wait_notification(&mvm->notif_wait, &calib_wait,
				    MVM_UCODE_CALIB_TIMEOUT);
	if (!ret)
		goto out;

	if (iwl_mvm_is_radio_hw_killed(mvm)) {
		IWL_DEBUG_RF_KILL(mvm, "RFKILL while calibrating.\n");
		ret = 0;
	} else {
		IWL_ERR(mvm, "Failed to run INIT calibrations: %d\n",
			ret);
	}

	goto out;

remove_notif:
	iwl_remove_notification(&mvm->notif_wait, &calib_wait);
out:
	mvm->rfkill_safe_init_done = false;
	if (iwlmvm_mod_params.init_dbg && !mvm->nvm_data) {
		/* we want to debug INIT and we have no NVM - fake */
		mvm->nvm_data = kzalloc(sizeof(struct iwl_nvm_data) +
					sizeof(struct ieee80211_channel) +
					sizeof(struct ieee80211_rate),
					GFP_KERNEL);
		if (!mvm->nvm_data)
			return -ENOMEM;
		mvm->nvm_data->bands[0].channels = mvm->nvm_data->channels;
		mvm->nvm_data->bands[0].n_channels = 1;
		mvm->nvm_data->bands[0].n_bitrates = 1;
		mvm->nvm_data->bands[0].bitrates =
			(void *)(mvm->nvm_data->channels + 1);
		mvm->nvm_data->bands[0].bitrates->hw_value = 10;
	}

	return ret;
}

static int iwl_mvm_config_ltr(struct iwl_mvm *mvm)
{
	struct iwl_ltr_config_cmd cmd = {
		.flags = cpu_to_le32(LTR_CFG_FLAG_FEATURE_ENABLE),
	};

	if (!mvm->trans->ltr_enabled)
		return 0;

	return iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0,
				    sizeof(cmd), &cmd);
}

#ifdef CONFIG_ACPI
int iwl_mvm_sar_select_profile(struct iwl_mvm *mvm, int prof_a, int prof_b)
{
	u32 cmd_id = REDUCE_TX_POWER_CMD;
	struct iwl_dev_tx_power_cmd cmd = {
		.common.set_mode = cpu_to_le32(IWL_TX_POWER_MODE_SET_CHAINS),
	};
	__le16 *per_chain;
	int ret;
	u16 len = 0;
	u32 n_subbands;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					   IWL_FW_CMD_VER_UNKNOWN);
	if (cmd_ver == 7) {
		len = sizeof(cmd.v7);
		n_subbands = IWL_NUM_SUB_BANDS_V2;
		per_chain = cmd.v7.per_chain[0][0];
		cmd.v7.flags = cpu_to_le32(mvm->fwrt.reduced_power_flags);
	} else if (cmd_ver == 6) {
		len = sizeof(cmd.v6);
		n_subbands = IWL_NUM_SUB_BANDS_V2;
		per_chain = cmd.v6.per_chain[0][0];
	} else if (fw_has_api(&mvm->fw->ucode_capa,
			      IWL_UCODE_TLV_API_REDUCE_TX_POWER)) {
		len = sizeof(cmd.v5);
		n_subbands = IWL_NUM_SUB_BANDS_V1;
		per_chain = cmd.v5.per_chain[0][0];
	} else if (fw_has_capa(&mvm->fw->ucode_capa,
			       IWL_UCODE_TLV_CAPA_TX_POWER_ACK)) {
		len = sizeof(cmd.v4);
		n_subbands = IWL_NUM_SUB_BANDS_V1;
		per_chain = cmd.v4.per_chain[0][0];
	} else {
		len = sizeof(cmd.v3);
		n_subbands = IWL_NUM_SUB_BANDS_V1;
		per_chain = cmd.v3.per_chain[0][0];
	}

	/* all structs have the same common part, add it */
	len += sizeof(cmd.common);

	ret = iwl_sar_select_profile(&mvm->fwrt, per_chain,
				     IWL_NUM_CHAIN_TABLES,
				     n_subbands, prof_a, prof_b);

	/* return on error or if the profile is disabled (positive number) */
	if (ret)
		return ret;

	iwl_mei_set_power_limit(per_chain);

	IWL_DEBUG_RADIO(mvm, "Sending REDUCE_TX_POWER_CMD per chain\n");
	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, len, &cmd);
}

int iwl_mvm_get_sar_geo_profile(struct iwl_mvm *mvm)
{
	union iwl_geo_tx_power_profiles_cmd geo_tx_cmd;
	struct iwl_geo_tx_power_profiles_resp *resp;
	u16 len;
	int ret;
	struct iwl_host_cmd cmd = {
		.id = WIDE_ID(PHY_OPS_GROUP, PER_CHAIN_LIMIT_OFFSET_CMD),
		.flags = CMD_WANT_SKB,
		.data = { &geo_tx_cmd },
	};
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd.id,
					   IWL_FW_CMD_VER_UNKNOWN);

	/* the ops field is at the same spot for all versions, so set in v1 */
	geo_tx_cmd.v1.ops =
		cpu_to_le32(IWL_PER_CHAIN_OFFSET_GET_CURRENT_TABLE);

	if (cmd_ver == 5)
		len = sizeof(geo_tx_cmd.v5);
	else if (cmd_ver == 4)
		len = sizeof(geo_tx_cmd.v4);
	else if (cmd_ver == 3)
		len = sizeof(geo_tx_cmd.v3);
	else if (fw_has_api(&mvm->fwrt.fw->ucode_capa,
			    IWL_UCODE_TLV_API_SAR_TABLE_VER))
		len = sizeof(geo_tx_cmd.v2);
	else
		len = sizeof(geo_tx_cmd.v1);

	if (!iwl_sar_geo_support(&mvm->fwrt))
		return -EOPNOTSUPP;

	cmd.len[0] = len;

	ret = iwl_mvm_send_cmd(mvm, &cmd);
	if (ret) {
		IWL_ERR(mvm, "Failed to get geographic profile info %d\n", ret);
		return ret;
	}

	resp = (void *)cmd.resp_pkt->data;
	ret = le32_to_cpu(resp->profile_idx);

	if (WARN_ON(ret > ACPI_NUM_GEO_PROFILES_REV3))
		ret = -EIO;

	iwl_free_resp(&cmd);
	return ret;
}

static int iwl_mvm_sar_geo_init(struct iwl_mvm *mvm)
{
	u32 cmd_id = WIDE_ID(PHY_OPS_GROUP, PER_CHAIN_LIMIT_OFFSET_CMD);
	union iwl_geo_tx_power_profiles_cmd cmd;
	u16 len;
	u32 n_bands;
	u32 n_profiles;
	u32 sk = 0;
	int ret;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
					   IWL_FW_CMD_VER_UNKNOWN);

	BUILD_BUG_ON(offsetof(struct iwl_geo_tx_power_profiles_cmd_v1, ops) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v2, ops) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v2, ops) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v3, ops) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v3, ops) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v4, ops) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v4, ops) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v5, ops));

	/* the ops field is at the same spot for all versions, so set in v1 */
	cmd.v1.ops = cpu_to_le32(IWL_PER_CHAIN_OFFSET_SET_TABLES);

	if (cmd_ver == 5) {
		len = sizeof(cmd.v5);
		n_bands = ARRAY_SIZE(cmd.v5.table[0]);
		n_profiles = ACPI_NUM_GEO_PROFILES_REV3;
	} else if (cmd_ver == 4) {
		len = sizeof(cmd.v4);
		n_bands = ARRAY_SIZE(cmd.v4.table[0]);
		n_profiles = ACPI_NUM_GEO_PROFILES_REV3;
	} else if (cmd_ver == 3) {
		len = sizeof(cmd.v3);
		n_bands = ARRAY_SIZE(cmd.v3.table[0]);
		n_profiles = ACPI_NUM_GEO_PROFILES;
	} else if (fw_has_api(&mvm->fwrt.fw->ucode_capa,
			      IWL_UCODE_TLV_API_SAR_TABLE_VER)) {
		len = sizeof(cmd.v2);
		n_bands = ARRAY_SIZE(cmd.v2.table[0]);
		n_profiles = ACPI_NUM_GEO_PROFILES;
	} else {
		len = sizeof(cmd.v1);
		n_bands = ARRAY_SIZE(cmd.v1.table[0]);
		n_profiles = ACPI_NUM_GEO_PROFILES;
	}

	BUILD_BUG_ON(offsetof(struct iwl_geo_tx_power_profiles_cmd_v1, table) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v2, table) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v2, table) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v3, table) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v3, table) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v4, table) ||
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v4, table) !=
		     offsetof(struct iwl_geo_tx_power_profiles_cmd_v5, table));
	/* the table is at the same position for all versions, so set use v1 */
	ret = iwl_sar_geo_init(&mvm->fwrt, &cmd.v1.table[0][0],
			       n_bands, n_profiles);

	/*
	 * It is a valid scenario to not support SAR, or miss wgds table,
	 * but in that case there is no need to send the command.
	 */
	if (ret)
		return 0;

	/* Only set to South Korea if the table revision is 1 */
	if (mvm->fwrt.geo_rev == 1)
		sk = 1;

	/*
	 * Set the table_revision to South Korea (1) or not (0).  The
	 * element name is misleading, as it doesn't contain the table
	 * revision number, but whether the South Korea variation
	 * should be used.
	 * This must be done after calling iwl_sar_geo_init().
	 */
	if (cmd_ver == 5)
		cmd.v5.table_revision = cpu_to_le32(sk);
	else if (cmd_ver == 4)
		cmd.v4.table_revision = cpu_to_le32(sk);
	else if (cmd_ver == 3)
		cmd.v3.table_revision = cpu_to_le32(sk);
	else if (fw_has_api(&mvm->fwrt.fw->ucode_capa,
			    IWL_UCODE_TLV_API_SAR_TABLE_VER))
		cmd.v2.table_revision = cpu_to_le32(sk);

	return iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, len, &cmd);
}

int iwl_mvm_ppag_send_cmd(struct iwl_mvm *mvm)
{
	union iwl_ppag_table_cmd cmd;
	int ret, cmd_size;

	ret = iwl_read_ppag_table(&mvm->fwrt, &cmd, &cmd_size);
	/* Not supporting PPAG table is a valid scenario */
	if (ret < 0)
		return 0;

	IWL_DEBUG_RADIO(mvm, "Sending PER_PLATFORM_ANT_GAIN_CMD\n");
	ret = iwl_mvm_send_cmd_pdu(mvm, WIDE_ID(PHY_OPS_GROUP,
						PER_PLATFORM_ANT_GAIN_CMD),
				   0, cmd_size, &cmd);
	if (ret < 0)
		IWL_ERR(mvm, "failed to send PER_PLATFORM_ANT_GAIN_CMD (%d)\n",
			ret);

	return ret;
}

static int iwl_mvm_ppag_init(struct iwl_mvm *mvm)
{
	/* no need to read the table, done in INIT stage */
	if (!(iwl_acpi_is_ppag_approved(&mvm->fwrt)))
		return 0;

	return iwl_mvm_ppag_send_cmd(mvm);
}

static const struct dmi_system_id dmi_tas_approved_list[] = {
	{ .ident = "HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		},
	},
	{ .ident = "SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
		{ .ident = "LENOVO",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{ .ident = "MSFT",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
		},
	},
	{ .ident = "Acer",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		},
	},
	{ .ident = "ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		},
	},
	{ .ident = "MSI",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Micro-Star International Co., Ltd."),
		},
	},
	{ .ident = "Honor",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HONOR"),
		},
	},
	/* keep last */
	{}
};

bool iwl_mvm_is_vendor_in_approved_list(void)
{
	return dmi_check_system(dmi_tas_approved_list);
}

static bool iwl_mvm_add_to_tas_block_list(__le32 *list, __le32 *le_size, unsigned int mcc)
{
	int i;
	u32 size = le32_to_cpu(*le_size);

	/* Verify that there is room for another country */
	if (size >= IWL_TAS_BLOCK_LIST_MAX)
		return false;

	for (i = 0; i < size; i++) {
		if (list[i] == cpu_to_le32(mcc))
			return true;
	}

	list[size++] = cpu_to_le32(mcc);
	*le_size = cpu_to_le32(size);
	return true;
}

static void iwl_mvm_tas_init(struct iwl_mvm *mvm)
{
	u32 cmd_id = WIDE_ID(REGULATORY_AND_NVM_GROUP, TAS_CONFIG);
	int ret;
	union iwl_tas_config_cmd cmd = {};
	int cmd_size, fw_ver;

	BUILD_BUG_ON(ARRAY_SIZE(cmd.v3.block_list_array) <
		     APCI_WTAS_BLACK_LIST_MAX);

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TAS_CFG)) {
		IWL_DEBUG_RADIO(mvm, "TAS not enabled in FW\n");
		return;
	}

	fw_ver = iwl_fw_lookup_cmd_ver(mvm->fw, cmd_id,
				       IWL_FW_CMD_VER_UNKNOWN);

	ret = iwl_acpi_get_tas(&mvm->fwrt, &cmd, fw_ver);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mvm,
				"TAS table invalid or unavailable. (%d)\n",
				ret);
		return;
	}

	if (ret == 0)
		return;

	if (!iwl_mvm_is_vendor_in_approved_list()) {
		IWL_DEBUG_RADIO(mvm,
				"System vendor '%s' is not in the approved list, disabling TAS in US and Canada.\n",
				dmi_get_system_info(DMI_SYS_VENDOR));
		if ((!iwl_mvm_add_to_tas_block_list(cmd.v4.block_list_array,
						    &cmd.v4.block_list_size,
							IWL_TAS_US_MCC)) ||
		    (!iwl_mvm_add_to_tas_block_list(cmd.v4.block_list_array,
						    &cmd.v4.block_list_size,
							IWL_TAS_CANADA_MCC))) {
			IWL_DEBUG_RADIO(mvm,
					"Unable to add US/Canada to TAS block list, disabling TAS\n");
			return;
		}
	} else {
		IWL_DEBUG_RADIO(mvm,
				"System vendor '%s' is in the approved list.\n",
				dmi_get_system_info(DMI_SYS_VENDOR));
	}

	/* v4 is the same size as v3, so no need to differentiate here */
	cmd_size = fw_ver < 3 ?
		sizeof(struct iwl_tas_config_cmd_v2) :
		sizeof(struct iwl_tas_config_cmd_v3);

	ret = iwl_mvm_send_cmd_pdu(mvm, cmd_id, 0, cmd_size, &cmd);
	if (ret < 0)
		IWL_DEBUG_RADIO(mvm, "failed to send TAS_CONFIG (%d)\n", ret);
}

static u8 iwl_mvm_eval_dsm_rfi(struct iwl_mvm *mvm)
{
	u8 value;
	int ret = iwl_acpi_get_dsm_u8(mvm->fwrt.dev, 0, DSM_RFI_FUNC_ENABLE,
				      &iwl_rfi_guid, &value);

	if (ret < 0) {
		IWL_DEBUG_RADIO(mvm, "Failed to get DSM RFI, ret=%d\n", ret);

	} else if (value >= DSM_VALUE_RFI_MAX) {
		IWL_DEBUG_RADIO(mvm, "DSM RFI got invalid value, ret=%d\n",
				value);

	} else if (value == DSM_VALUE_RFI_ENABLE) {
		IWL_DEBUG_RADIO(mvm, "DSM RFI is evaluated to enable\n");
		return DSM_VALUE_RFI_ENABLE;
	}

	IWL_DEBUG_RADIO(mvm, "DSM RFI is disabled\n");

	/* default behaviour is disabled */
	return DSM_VALUE_RFI_DISABLE;
}

static void iwl_mvm_lari_cfg(struct iwl_mvm *mvm)
{
	int ret;
	u32 value;
	struct iwl_lari_config_change_cmd_v6 cmd = {};

	cmd.config_bitmap = iwl_acpi_get_lari_config_bitmap(&mvm->fwrt);

	ret = iwl_acpi_get_dsm_u32(mvm->fwrt.dev, 0, DSM_FUNC_11AX_ENABLEMENT,
				   &iwl_guid, &value);
	if (!ret)
		cmd.oem_11ax_allow_bitmap = cpu_to_le32(value);

	ret = iwl_acpi_get_dsm_u32(mvm->fwrt.dev, 0,
				   DSM_FUNC_ENABLE_UNII4_CHAN,
				   &iwl_guid, &value);
	if (!ret)
		cmd.oem_unii4_allow_bitmap = cpu_to_le32(value);

	ret = iwl_acpi_get_dsm_u32(mvm->fwrt.dev, 0,
				   DSM_FUNC_ACTIVATE_CHANNEL,
				   &iwl_guid, &value);
	if (!ret)
		cmd.chan_state_active_bitmap = cpu_to_le32(value);

	ret = iwl_acpi_get_dsm_u32(mvm->fwrt.dev, 0,
				   DSM_FUNC_ENABLE_6E,
				   &iwl_guid, &value);
	if (!ret)
		cmd.oem_uhb_allow_bitmap = cpu_to_le32(value);

	ret = iwl_acpi_get_dsm_u32(mvm->fwrt.dev, 0,
				   DSM_FUNC_FORCE_DISABLE_CHANNELS,
				   &iwl_guid, &value);
	if (!ret)
		cmd.force_disable_channels_bitmap = cpu_to_le32(value);

	if (cmd.config_bitmap ||
	    cmd.oem_uhb_allow_bitmap ||
	    cmd.oem_11ax_allow_bitmap ||
	    cmd.oem_unii4_allow_bitmap ||
	    cmd.chan_state_active_bitmap ||
	    cmd.force_disable_channels_bitmap) {
		size_t cmd_size;
		u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
						   WIDE_ID(REGULATORY_AND_NVM_GROUP,
							   LARI_CONFIG_CHANGE),
						   1);
		switch (cmd_ver) {
		case 6:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v6);
			break;
		case 5:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v5);
			break;
		case 4:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v4);
			break;
		case 3:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v3);
			break;
		case 2:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v2);
			break;
		default:
			cmd_size = sizeof(struct iwl_lari_config_change_cmd_v1);
			break;
		}

		IWL_DEBUG_RADIO(mvm,
				"sending LARI_CONFIG_CHANGE, config_bitmap=0x%x, oem_11ax_allow_bitmap=0x%x\n",
				le32_to_cpu(cmd.config_bitmap),
				le32_to_cpu(cmd.oem_11ax_allow_bitmap));
		IWL_DEBUG_RADIO(mvm,
				"sending LARI_CONFIG_CHANGE, oem_unii4_allow_bitmap=0x%x, chan_state_active_bitmap=0x%x, cmd_ver=%d\n",
				le32_to_cpu(cmd.oem_unii4_allow_bitmap),
				le32_to_cpu(cmd.chan_state_active_bitmap),
				cmd_ver);
		IWL_DEBUG_RADIO(mvm,
				"sending LARI_CONFIG_CHANGE, oem_uhb_allow_bitmap=0x%x, force_disable_channels_bitmap=0x%x\n",
				le32_to_cpu(cmd.oem_uhb_allow_bitmap),
				le32_to_cpu(cmd.force_disable_channels_bitmap));
		ret = iwl_mvm_send_cmd_pdu(mvm,
					   WIDE_ID(REGULATORY_AND_NVM_GROUP,
						   LARI_CONFIG_CHANGE),
					   0, cmd_size, &cmd);
		if (ret < 0)
			IWL_DEBUG_RADIO(mvm,
					"Failed to send LARI_CONFIG_CHANGE (%d)\n",
					ret);
	}
}

void iwl_mvm_get_acpi_tables(struct iwl_mvm *mvm)
{
	int ret;

	/* read PPAG table */
	ret = iwl_acpi_get_ppag_table(&mvm->fwrt);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mvm,
				"PPAG BIOS table invalid or unavailable. (%d)\n",
				ret);
	}

	/* read SAR tables */
	ret = iwl_sar_get_wrds_table(&mvm->fwrt);
	if (ret < 0) {
		IWL_DEBUG_RADIO(mvm,
				"WRDS SAR BIOS table invalid or unavailable. (%d)\n",
				ret);
		/*
		 * If not available, don't fail and don't bother with EWRD and
		 * WGDS */

		if (!iwl_sar_get_wgds_table(&mvm->fwrt)) {
			/*
			 * If basic SAR is not available, we check for WGDS,
			 * which should *not* be available either.  If it is
			 * available, issue an error, because we can't use SAR
			 * Geo without basic SAR.
			 */
			IWL_ERR(mvm, "BIOS contains WGDS but no WRDS\n");
		}

	} else {
		ret = iwl_sar_get_ewrd_table(&mvm->fwrt);
		/* if EWRD is not available, we can still use
		* WRDS, so don't fail */
		if (ret < 0)
			IWL_DEBUG_RADIO(mvm,
					"EWRD SAR BIOS table invalid or unavailable. (%d)\n",
					ret);

		/* read geo SAR table */
		if (iwl_sar_geo_support(&mvm->fwrt)) {
			ret = iwl_sar_get_wgds_table(&mvm->fwrt);
			if (ret < 0)
				IWL_DEBUG_RADIO(mvm,
						"Geo SAR BIOS table invalid or unavailable. (%d)\n",
						ret);
				/* we don't fail if the table is not available */
		}
	}

	iwl_acpi_get_phy_filters(&mvm->fwrt, &mvm->phy_filters);
}
#else /* CONFIG_ACPI */

inline int iwl_mvm_sar_select_profile(struct iwl_mvm *mvm,
				      int prof_a, int prof_b)
{
	return 1;
}

inline int iwl_mvm_get_sar_geo_profile(struct iwl_mvm *mvm)
{
	return -ENOENT;
}

static int iwl_mvm_sar_geo_init(struct iwl_mvm *mvm)
{
	return 0;
}

int iwl_mvm_ppag_send_cmd(struct iwl_mvm *mvm)
{
	return -ENOENT;
}

static int iwl_mvm_ppag_init(struct iwl_mvm *mvm)
{
	return 0;
}

static void iwl_mvm_tas_init(struct iwl_mvm *mvm)
{
}

static void iwl_mvm_lari_cfg(struct iwl_mvm *mvm)
{
}

bool iwl_mvm_is_vendor_in_approved_list(void)
{
	return false;
}

static u8 iwl_mvm_eval_dsm_rfi(struct iwl_mvm *mvm)
{
	return DSM_VALUE_RFI_DISABLE;
}

void iwl_mvm_get_acpi_tables(struct iwl_mvm *mvm)
{
}

#endif /* CONFIG_ACPI */

void iwl_mvm_send_recovery_cmd(struct iwl_mvm *mvm, u32 flags)
{
	u32 error_log_size = mvm->fw->ucode_capa.error_log_size;
	int ret;
	u32 resp;

	struct iwl_fw_error_recovery_cmd recovery_cmd = {
		.flags = cpu_to_le32(flags),
		.buf_size = 0,
	};
	struct iwl_host_cmd host_cmd = {
		.id = WIDE_ID(SYSTEM_GROUP, FW_ERROR_RECOVERY_CMD),
		.flags = CMD_WANT_SKB,
		.data = {&recovery_cmd, },
		.len = {sizeof(recovery_cmd), },
	};

	/* no error log was defined in TLV */
	if (!error_log_size)
		return;

	if (flags & ERROR_RECOVERY_UPDATE_DB) {
		/* no buf was allocated while HW reset */
		if (!mvm->error_recovery_buf)
			return;

		host_cmd.data[1] = mvm->error_recovery_buf;
		host_cmd.len[1] =  error_log_size;
		host_cmd.dataflags[1] = IWL_HCMD_DFL_NOCOPY;
		recovery_cmd.buf_size = cpu_to_le32(error_log_size);
	}

	ret = iwl_mvm_send_cmd(mvm, &host_cmd);
	kfree(mvm->error_recovery_buf);
	mvm->error_recovery_buf = NULL;

	if (ret) {
		IWL_ERR(mvm, "Failed to send recovery cmd %d\n", ret);
		return;
	}

	/* skb respond is only relevant in ERROR_RECOVERY_UPDATE_DB */
	if (flags & ERROR_RECOVERY_UPDATE_DB) {
		resp = le32_to_cpu(*(__le32 *)host_cmd.resp_pkt->data);
		if (resp)
			IWL_ERR(mvm,
				"Failed to send recovery cmd blob was invalid %d\n",
				resp);
	}
}

static int iwl_mvm_sar_init(struct iwl_mvm *mvm)
{
	return iwl_mvm_sar_select_profile(mvm, 1, 1);
}

static int iwl_mvm_load_rt_fw(struct iwl_mvm *mvm)
{
	int ret;

	if (iwl_mvm_has_unified_ucode(mvm))
		return iwl_run_unified_mvm_ucode(mvm);

	ret = iwl_run_init_mvm_ucode(mvm);

	if (ret) {
		IWL_ERR(mvm, "Failed to run INIT ucode: %d\n", ret);

		if (iwlmvm_mod_params.init_dbg)
			return 0;
		return ret;
	}

	iwl_fw_dbg_stop_sync(&mvm->fwrt);
	iwl_trans_stop_device(mvm->trans);
	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	mvm->rfkill_safe_init_done = false;
	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_REGULAR);
	if (ret)
		return ret;

	mvm->rfkill_safe_init_done = true;

	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_AFTER_ALIVE,
			       NULL);

	return iwl_init_paging(&mvm->fwrt, mvm->fwrt.cur_fw_img);
}

int iwl_mvm_up(struct iwl_mvm *mvm)
{
	int ret, i;
	struct ieee80211_channel *chan;
	struct cfg80211_chan_def chandef;
	struct ieee80211_supported_band *sband = NULL;
	u32 sb_cfg;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	sb_cfg = iwl_read_umac_prph(mvm->trans, SB_MODIFY_CFG_FLAG);
	mvm->pldr_sync = !(sb_cfg & SB_CFG_RESIDES_IN_OTP_MASK);
	if (mvm->pldr_sync && iwl_mei_pldr_req())
		return -EBUSY;

	ret = iwl_mvm_load_rt_fw(mvm);
	if (ret) {
		IWL_ERR(mvm, "Failed to start RT ucode: %d\n", ret);
		if (ret != -ERFKILL && !mvm->pldr_sync)
			iwl_fw_dbg_error_collect(&mvm->fwrt,
						 FW_DBG_TRIGGER_DRIVER);
		goto error;
	}

	/* FW loaded successfully */
	mvm->pldr_sync = false;

	iwl_get_shared_mem_conf(&mvm->fwrt);

	ret = iwl_mvm_sf_update(mvm, NULL, false);
	if (ret)
		IWL_ERR(mvm, "Failed to initialize Smart Fifo\n");

	if (!iwl_trans_dbg_ini_valid(mvm->trans)) {
		mvm->fwrt.dump.conf = FW_DBG_INVALID;
		/* if we have a destination, assume EARLY START */
		if (mvm->fw->dbg.dest_tlv)
			mvm->fwrt.dump.conf = FW_DBG_START_FROM_ALIVE;
		iwl_fw_start_dbg_conf(&mvm->fwrt, FW_DBG_START_FROM_ALIVE);
	}

	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto error;

	if (!iwl_mvm_has_unified_ucode(mvm)) {
		/* Send phy db control command and then phy db calibration */
		ret = iwl_send_phy_db_data(mvm->phy_db);
		if (ret)
			goto error;
		ret = iwl_send_phy_cfg_cmd(mvm);
		if (ret)
			goto error;
	}

	ret = iwl_mvm_send_bt_init_conf(mvm);
	if (ret)
		goto error;

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_SOC_LATENCY_SUPPORT)) {
		ret = iwl_set_soc_latency(&mvm->fwrt);
		if (ret)
			goto error;
	}

	iwl_mvm_lari_cfg(mvm);

	/* Init RSS configuration */
	ret = iwl_configure_rxq(&mvm->fwrt);
	if (ret)
		goto error;

	if (iwl_mvm_has_new_rx_api(mvm)) {
		ret = iwl_send_rss_cfg_cmd(mvm);
		if (ret) {
			IWL_ERR(mvm, "Failed to configure RSS queues: %d\n",
				ret);
			goto error;
		}
	}

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);
		RCU_INIT_POINTER(mvm->fw_id_to_link_sta[i], NULL);
	}

	for (i = 0; i < IWL_MVM_FW_MAX_LINK_ID + 1; i++)
		RCU_INIT_POINTER(mvm->link_id_to_link_conf[i], NULL);

	memset(&mvm->fw_link_ids_map, 0, sizeof(mvm->fw_link_ids_map));

	mvm->tdls_cs.peer.sta_id = IWL_MVM_INVALID_STA;

	/* reset quota debouncing buffer - 0xff will yield invalid data */
	memset(&mvm->last_quota_cmd, 0xff, sizeof(mvm->last_quota_cmd));

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_DQA_SUPPORT)) {
		ret = iwl_mvm_send_dqa_cmd(mvm);
		if (ret)
			goto error;
	}

	/*
	 * Add auxiliary station for scanning.
	 * Newer versions of this command implies that the fw uses
	 * internal aux station for all aux activities that don't
	 * requires a dedicated data queue.
	 */
	if (!iwl_mvm_has_new_station_api(mvm->fw)) {
		 /*
		  * In old version the aux station uses mac id like other
		  * station and not lmac id
		  */
		ret = iwl_mvm_add_aux_sta(mvm, MAC_INDEX_AUX);
		if (ret)
			goto error;
	}

	/* Add all the PHY contexts */
	i = 0;
	while (!sband && i < NUM_NL80211_BANDS)
		sband = mvm->hw->wiphy->bands[i++];

	if (WARN_ON_ONCE(!sband)) {
		ret = -ENODEV;
		goto error;
	}

	chan = &sband->channels[0];

	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
	for (i = 0; i < NUM_PHY_CTX; i++) {
		/*
		 * The channel used here isn't relevant as it's
		 * going to be overwritten in the other flows.
		 * For now use the first channel we have.
		 */
		ret = iwl_mvm_phy_ctxt_add(mvm, &mvm->phy_ctxts[i],
					   &chandef, 1, 1);
		if (ret)
			goto error;
	}

	if (iwl_mvm_is_tt_in_fw(mvm)) {
		/* in order to give the responsibility of ct-kill and
		 * TX backoff to FW we need to send empty temperature reporting
		 * cmd during init time
		 */
		iwl_mvm_send_temp_report_ths_cmd(mvm);
	} else {
		/* Initialize tx backoffs to the minimal possible */
		iwl_mvm_tt_tx_backoff(mvm, 0);
	}

#ifdef CONFIG_THERMAL
	/* TODO: read the budget from BIOS / Platform NVM */

	/*
	 * In case there is no budget from BIOS / Platform NVM the default
	 * budget should be 2000mW (cooling state 0).
	 */
	if (iwl_mvm_is_ctdp_supported(mvm)) {
		ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_START,
					   mvm->cooling_dev.cur_state);
		if (ret)
			goto error;
	}
#endif

	if (!fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SET_LTR_GEN2))
		WARN_ON(iwl_mvm_config_ltr(mvm));

	ret = iwl_mvm_power_update_device(mvm);
	if (ret)
		goto error;

	/*
	 * RTNL is not taken during Ct-kill, but we don't need to scan/Tx
	 * anyway, so don't init MCC.
	 */
	if (!test_bit(IWL_MVM_STATUS_HW_CTKILL, &mvm->status)) {
		ret = iwl_mvm_init_mcc(mvm);
		if (ret)
			goto error;
	}

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_UMAC_SCAN)) {
		mvm->scan_type = IWL_SCAN_TYPE_NOT_SET;
		mvm->hb_scan_type = IWL_SCAN_TYPE_NOT_SET;
		ret = iwl_mvm_config_scan(mvm);
		if (ret)
			goto error;
	}

	if (test_bit(IWL_MVM_STATUS_IN_HW_RESTART, &mvm->status)) {
		iwl_mvm_send_recovery_cmd(mvm, ERROR_RECOVERY_UPDATE_DB);

		if (mvm->time_sync.active)
			iwl_mvm_time_sync_config(mvm, mvm->time_sync.peer_addr,
						 IWL_TIME_SYNC_PROTOCOL_TM |
						 IWL_TIME_SYNC_PROTOCOL_FTM);
	}

	if (!mvm->ptp_data.ptp_clock)
		iwl_mvm_ptp_init(mvm);

	if (iwl_acpi_get_eckv(mvm->dev, &mvm->ext_clock_valid))
		IWL_DEBUG_INFO(mvm, "ECKV table doesn't exist in BIOS\n");

	ret = iwl_mvm_ppag_init(mvm);
	if (ret)
		goto error;

	ret = iwl_mvm_sar_init(mvm);
	if (ret == 0)
		ret = iwl_mvm_sar_geo_init(mvm);
	if (ret < 0)
		goto error;

	ret = iwl_mvm_sgom_init(mvm);
	if (ret)
		goto error;

	iwl_mvm_tas_init(mvm);
	iwl_mvm_leds_sync(mvm);

	if (iwl_rfi_supported(mvm)) {
		if (iwl_mvm_eval_dsm_rfi(mvm) == DSM_VALUE_RFI_ENABLE)
			iwl_rfi_send_config_cmd(mvm, NULL);
	}

	iwl_mvm_mei_device_state(mvm, true);

	IWL_DEBUG_INFO(mvm, "RT uCode started.\n");
	return 0;
 error:
	if (!iwlmvm_mod_params.init_dbg || !ret)
		iwl_mvm_stop_device(mvm);
	return ret;
}

int iwl_mvm_load_d3_fw(struct iwl_mvm *mvm)
{
	int ret, i;

	lockdep_assert_held(&mvm->mutex);

	ret = iwl_trans_start_hw(mvm->trans);
	if (ret)
		return ret;

	ret = iwl_mvm_load_ucode_wait_alive(mvm, IWL_UCODE_WOWLAN);
	if (ret) {
		IWL_ERR(mvm, "Failed to start WoWLAN firmware: %d\n", ret);
		goto error;
	}

	ret = iwl_send_tx_ant_cfg(mvm, iwl_mvm_get_valid_tx_ant(mvm));
	if (ret)
		goto error;

	/* Send phy db control command and then phy db calibration*/
	ret = iwl_send_phy_db_data(mvm->phy_db);
	if (ret)
		goto error;

	ret = iwl_send_phy_cfg_cmd(mvm);
	if (ret)
		goto error;

	/* init the fw <-> mac80211 STA mapping */
	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		RCU_INIT_POINTER(mvm->fw_id_to_mac_id[i], NULL);
		RCU_INIT_POINTER(mvm->fw_id_to_link_sta[i], NULL);
	}

	if (!iwl_mvm_has_new_station_api(mvm->fw)) {
		/*
		 * Add auxiliary station for scanning.
		 * Newer versions of this command implies that the fw uses
		 * internal aux station for all aux activities that don't
		 * requires a dedicated data queue.
		 * In old version the aux station uses mac id like other
		 * station and not lmac id
		 */
		ret = iwl_mvm_add_aux_sta(mvm, MAC_INDEX_AUX);
		if (ret)
			goto error;
	}

	return 0;
 error:
	iwl_mvm_stop_device(mvm);
	return ret;
}

void iwl_mvm_rx_mfuart_notif(struct iwl_mvm *mvm,
			     struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mfuart_load_notif *mfuart_notif = (void *)pkt->data;

	IWL_DEBUG_INFO(mvm,
		       "MFUART: installed ver: 0x%08x, external ver: 0x%08x, status: 0x%08x, duration: 0x%08x\n",
		       le32_to_cpu(mfuart_notif->installed_ver),
		       le32_to_cpu(mfuart_notif->external_ver),
		       le32_to_cpu(mfuart_notif->status),
		       le32_to_cpu(mfuart_notif->duration));

	if (iwl_rx_packet_payload_len(pkt) == sizeof(*mfuart_notif))
		IWL_DEBUG_INFO(mvm,
			       "MFUART: image size: 0x%08x\n",
			       le32_to_cpu(mfuart_notif->image_size));
}
