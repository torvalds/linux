// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/remoteproc.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include "core.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "debug.h"
#include "hif.h"
#include "wow.h"

unsigned int ath11k_debug_mask;
EXPORT_SYMBOL(ath11k_debug_mask);
module_param_named(debug_mask, ath11k_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");

static unsigned int ath11k_crypto_mode;
module_param_named(crypto_mode, ath11k_crypto_mode, uint, 0644);
MODULE_PARM_DESC(crypto_mode, "crypto mode: 0-hardware, 1-software");

/* frame mode values are mapped as per enum ath11k_hw_txrx_mode */
unsigned int ath11k_frame_mode = ATH11K_HW_TXRX_NATIVE_WIFI;
module_param_named(frame_mode, ath11k_frame_mode, uint, 0644);
MODULE_PARM_DESC(frame_mode,
		 "Datapath frame mode (0: raw, 1: native wifi (default), 2: ethernet)");

static const struct ath11k_hw_params ath11k_hw_params[] = {
	{
		.hw_rev = ATH11K_HW_IPQ8074,
		.name = "ipq8074 hw2.0",
		.fw = {
			.dir = "IPQ8074/hw2.0",
			.board_size = 256 * 1024,
			.cal_size = 256 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &ipq8074_ops,
		.ring_mask = &ath11k_hw_ring_mask_ipq8074,
		.internal_sleep_clock = false,
		.regs = &ipq8074_regs,
		.host_ce_config = ath11k_host_ce_config_ipq8074,
		.ce_count = 12,
		.target_ce_config = ath11k_target_ce_config_wlan_ipq8074,
		.target_ce_count = 11,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_ipq8074,
		.svc_to_ce_map_len = 21,
		.single_pdev_only = false,
		.rxdma1_enable = true,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,
		.htt_peer_map_v2 = true,
		.tcl_0_only = false,
		.spectral_fft_sz = 2,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,
		.supports_shadow_regs = false,
		.idle_ps = false,
		.cold_boot_calib = true,
		.supports_suspend = false,
	},
	{
		.hw_rev = ATH11K_HW_IPQ6018_HW10,
		.name = "ipq6018 hw1.0",
		.fw = {
			.dir = "IPQ6018/hw1.0",
			.board_size = 256 * 1024,
			.cal_size = 256 * 1024,
		},
		.max_radios = 2,
		.bdf_addr = 0x4ABC0000,
		.hw_ops = &ipq6018_ops,
		.ring_mask = &ath11k_hw_ring_mask_ipq8074,
		.internal_sleep_clock = false,
		.regs = &ipq8074_regs,
		.host_ce_config = ath11k_host_ce_config_ipq8074,
		.ce_count = 12,
		.target_ce_config = ath11k_target_ce_config_wlan_ipq8074,
		.target_ce_count = 11,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_ipq6018,
		.svc_to_ce_map_len = 19,
		.single_pdev_only = false,
		.rxdma1_enable = true,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,
		.htt_peer_map_v2 = true,
		.tcl_0_only = false,
		.spectral_fft_sz = 4,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,
		.supports_shadow_regs = false,
		.idle_ps = false,
		.cold_boot_calib = true,
		.supports_suspend = false,
	},
	{
		.name = "qca6390 hw2.0",
		.hw_rev = ATH11K_HW_QCA6390_HW20,
		.fw = {
			.dir = "QCA6390/hw2.0",
			.board_size = 256 * 1024,
			.cal_size = 256 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &qca6390_ops,
		.ring_mask = &ath11k_hw_ring_mask_qca6390,
		.internal_sleep_clock = true,
		.regs = &qca6390_regs,
		.host_ce_config = ath11k_host_ce_config_qca6390,
		.ce_count = 9,
		.target_ce_config = ath11k_target_ce_config_wlan_qca6390,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qca6390,
		.svc_to_ce_map_len = 14,
		.single_pdev_only = true,
		.rxdma1_enable = false,
		.num_rxmda_per_pdev = 2,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,
		.htt_peer_map_v2 = false,
		.tcl_0_only = true,
		.spectral_fft_sz = 0,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP),
		.supports_monitor = false,
		.supports_shadow_regs = true,
		.idle_ps = true,
		.cold_boot_calib = false,
		.supports_suspend = true,
	},
};

int ath11k_core_suspend(struct ath11k_base *ab)
{
	int ret;

	if (!ab->hw_params.supports_suspend)
		return -EOPNOTSUPP;

	/* TODO: there can frames in queues so for now add delay as a hack.
	 * Need to implement to handle and remove this delay.
	 */
	msleep(500);

	ret = ath11k_dp_rx_pktlog_stop(ab, true);
	if (ret) {
		ath11k_warn(ab, "failed to stop dp rx (and timer) pktlog during suspend: %d\n",
			    ret);
		return ret;
	}

	ret = ath11k_wow_enable(ab);
	if (ret) {
		ath11k_warn(ab, "failed to enable wow during suspend: %d\n", ret);
		return ret;
	}

	ret = ath11k_dp_rx_pktlog_stop(ab, false);
	if (ret) {
		ath11k_warn(ab, "failed to stop dp rx pktlog during suspend: %d\n",
			    ret);
		return ret;
	}

	ath11k_ce_stop_shadow_timers(ab);
	ath11k_dp_stop_shadow_timers(ab);

	ath11k_hif_irq_disable(ab);
	ath11k_hif_ce_irq_disable(ab);

	ret = ath11k_hif_suspend(ab);
	if (ret) {
		ath11k_warn(ab, "failed to suspend hif: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_suspend);

int ath11k_core_resume(struct ath11k_base *ab)
{
	int ret;

	if (!ab->hw_params.supports_suspend)
		return -EOPNOTSUPP;

	ret = ath11k_hif_resume(ab);
	if (ret) {
		ath11k_warn(ab, "failed to resume hif during resume: %d\n", ret);
		return ret;
	}

	ath11k_hif_ce_irq_enable(ab);
	ath11k_hif_irq_enable(ab);

	ret = ath11k_dp_rx_pktlog_start(ab);
	if (ret) {
		ath11k_warn(ab, "failed to start rx pktlog during resume: %d\n",
			    ret);
		return ret;
	}

	ret = ath11k_wow_wakeup(ab);
	if (ret) {
		ath11k_warn(ab, "failed to wakeup wow during resume: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_resume);

int ath11k_core_check_dt(struct ath11k_base *ab)
{
	size_t max_len = sizeof(ab->qmi.target.bdf_ext);
	const char *variant = NULL;
	struct device_node *node;

	node = ab->dev->of_node;
	if (!node)
		return -ENOENT;

	of_property_read_string(node, "qcom,ath11k-calibration-variant",
				&variant);
	if (!variant)
		return -ENODATA;

	if (strscpy(ab->qmi.target.bdf_ext, variant, max_len) < 0)
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "bdf variant string is longer than the buffer can accommodate (variant: %s)\n",
			    variant);

	return 0;
}

static int ath11k_core_create_board_name(struct ath11k_base *ab, char *name,
					 size_t name_len)
{
	/* strlen(',variant=') + strlen(ab->qmi.target.bdf_ext) */
	char variant[9 + ATH11K_QMI_BDF_EXT_STR_LENGTH] = { 0 };

	if (ab->qmi.target.bdf_ext[0] != '\0')
		scnprintf(variant, sizeof(variant), ",variant=%s",
			  ab->qmi.target.bdf_ext);

	scnprintf(name, name_len,
		  "bus=%s,qmi-chip-id=%d,qmi-board-id=%d%s",
		  ath11k_bus_str(ab->hif.bus),
		  ab->qmi.target.chip_id,
		  ab->qmi.target.board_id, variant);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot using board name '%s'\n", name);

	return 0;
}

const struct firmware *ath11k_core_firmware_request(struct ath11k_base *ab,
						    const char *file)
{
	const struct firmware *fw;
	char path[100];
	int ret;

	if (file == NULL)
		return ERR_PTR(-ENOENT);

	ath11k_core_create_firmware_path(ab, file, path, sizeof(path));

	ret = firmware_request_nowarn(&fw, path, ab->dev);
	if (ret)
		return ERR_PTR(ret);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot firmware request %s size %zu\n",
		   path, fw->size);

	return fw;
}

void ath11k_core_free_bdf(struct ath11k_base *ab, struct ath11k_board_data *bd)
{
	if (!IS_ERR(bd->fw))
		release_firmware(bd->fw);

	memset(bd, 0, sizeof(*bd));
}

static int ath11k_core_parse_bd_ie_board(struct ath11k_base *ab,
					 struct ath11k_board_data *bd,
					 const void *buf, size_t buf_len,
					 const char *boardname,
					 int bd_ie_type)
{
	const struct ath11k_fw_ie *hdr;
	bool name_match_found;
	int ret, board_ie_id;
	size_t board_ie_len;
	const void *board_ie_data;

	name_match_found = false;

	/* go through ATH11K_BD_IE_BOARD_ elements */
	while (buf_len > sizeof(struct ath11k_fw_ie)) {
		hdr = buf;
		board_ie_id = le32_to_cpu(hdr->id);
		board_ie_len = le32_to_cpu(hdr->len);
		board_ie_data = hdr->data;

		buf_len -= sizeof(*hdr);
		buf += sizeof(*hdr);

		if (buf_len < ALIGN(board_ie_len, 4)) {
			ath11k_err(ab, "invalid ATH11K_BD_IE_BOARD length: %zu < %zu\n",
				   buf_len, ALIGN(board_ie_len, 4));
			ret = -EINVAL;
			goto out;
		}

		switch (board_ie_id) {
		case ATH11K_BD_IE_BOARD_NAME:
			ath11k_dbg_dump(ab, ATH11K_DBG_BOOT, "board name", "",
					board_ie_data, board_ie_len);

			if (board_ie_len != strlen(boardname))
				break;

			ret = memcmp(board_ie_data, boardname, strlen(boardname));
			if (ret)
				break;

			name_match_found = true;
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "boot found match for name '%s'",
				   boardname);
			break;
		case ATH11K_BD_IE_BOARD_DATA:
			if (!name_match_found)
				/* no match found */
				break;

			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "boot found board data for '%s'", boardname);

			bd->data = board_ie_data;
			bd->len = board_ie_len;

			ret = 0;
			goto out;
		default:
			ath11k_warn(ab, "unknown ATH11K_BD_IE_BOARD found: %d\n",
				    board_ie_id);
			break;
		}

		/* jump over the padding */
		board_ie_len = ALIGN(board_ie_len, 4);

		buf_len -= board_ie_len;
		buf += board_ie_len;
	}

	/* no match found */
	ret = -ENOENT;

out:
	return ret;
}

static int ath11k_core_fetch_board_data_api_n(struct ath11k_base *ab,
					      struct ath11k_board_data *bd,
					      const char *boardname)
{
	size_t len, magic_len;
	const u8 *data;
	char *filename, filepath[100];
	size_t ie_len;
	struct ath11k_fw_ie *hdr;
	int ret, ie_id;

	filename = ATH11K_BOARD_API2_FILE;

	if (!bd->fw)
		bd->fw = ath11k_core_firmware_request(ab, filename);

	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	data = bd->fw->data;
	len = bd->fw->size;

	ath11k_core_create_firmware_path(ab, filename,
					 filepath, sizeof(filepath));

	/* magic has extra null byte padded */
	magic_len = strlen(ATH11K_BOARD_MAGIC) + 1;
	if (len < magic_len) {
		ath11k_err(ab, "failed to find magic value in %s, file too short: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	if (memcmp(data, ATH11K_BOARD_MAGIC, magic_len)) {
		ath11k_err(ab, "found invalid board magic\n");
		ret = -EINVAL;
		goto err;
	}

	/* magic is padded to 4 bytes */
	magic_len = ALIGN(magic_len, 4);
	if (len < magic_len) {
		ath11k_err(ab, "failed: %s too small to contain board data, len: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	data += magic_len;
	len -= magic_len;

	while (len > sizeof(struct ath11k_fw_ie)) {
		hdr = (struct ath11k_fw_ie *)data;
		ie_id = le32_to_cpu(hdr->id);
		ie_len = le32_to_cpu(hdr->len);

		len -= sizeof(*hdr);
		data = hdr->data;

		if (len < ALIGN(ie_len, 4)) {
			ath11k_err(ab, "invalid length for board ie_id %d ie_len %zu len %zu\n",
				   ie_id, ie_len, len);
			return -EINVAL;
		}

		switch (ie_id) {
		case ATH11K_BD_IE_BOARD:
			ret = ath11k_core_parse_bd_ie_board(ab, bd, data,
							    ie_len,
							    boardname,
							    ATH11K_BD_IE_BOARD);
			if (ret == -ENOENT)
				/* no match found, continue */
				break;
			else if (ret)
				/* there was an error, bail out */
				goto err;
			/* either found or error, so stop searching */
			goto out;
		}

		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

out:
	if (!bd->data || !bd->len) {
		ath11k_err(ab,
			   "failed to fetch board data for %s from %s\n",
			   boardname, filepath);
		ret = -ENODATA;
		goto err;
	}

	return 0;

err:
	ath11k_core_free_bdf(ab, bd);
	return ret;
}

static int ath11k_core_fetch_board_data_api_1(struct ath11k_base *ab,
					      struct ath11k_board_data *bd)
{
	bd->fw = ath11k_core_firmware_request(ab, ATH11K_DEFAULT_BOARD_FILE);
	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	bd->data = bd->fw->data;
	bd->len = bd->fw->size;

	return 0;
}

#define BOARD_NAME_SIZE 100
int ath11k_core_fetch_bdf(struct ath11k_base *ab, struct ath11k_board_data *bd)
{
	char boardname[BOARD_NAME_SIZE];
	int ret;

	ret = ath11k_core_create_board_name(ab, boardname, BOARD_NAME_SIZE);
	if (ret) {
		ath11k_err(ab, "failed to create board name: %d", ret);
		return ret;
	}

	ab->bd_api = 2;
	ret = ath11k_core_fetch_board_data_api_n(ab, bd, boardname);
	if (!ret)
		goto success;

	ab->bd_api = 1;
	ret = ath11k_core_fetch_board_data_api_1(ab, bd);
	if (ret) {
		ath11k_err(ab, "failed to fetch board-2.bin or board.bin from %s\n",
			   ab->hw_params.fw.dir);
		return ret;
	}

success:
	ath11k_dbg(ab, ATH11K_DBG_BOOT, "using board api %d\n", ab->bd_api);
	return 0;
}

static void ath11k_core_stop(struct ath11k_base *ab)
{
	if (!test_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath11k_qmi_firmware_stop(ab);

	ath11k_hif_stop(ab);
	ath11k_wmi_detach(ab);
	ath11k_dp_pdev_reo_cleanup(ab);

	/* De-Init of components as needed */
}

static int ath11k_core_soc_create(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_qmi_init_service(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize qmi :%d\n", ret);
		return ret;
	}

	ret = ath11k_debugfs_soc_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create ath11k debugfs\n");
		goto err_qmi_deinit;
	}

	ret = ath11k_hif_power_up(ab);
	if (ret) {
		ath11k_err(ab, "failed to power up :%d\n", ret);
		goto err_debugfs_reg;
	}

	return 0;

err_debugfs_reg:
	ath11k_debugfs_soc_destroy(ab);
err_qmi_deinit:
	ath11k_qmi_deinit_service(ab);
	return ret;
}

static void ath11k_core_soc_destroy(struct ath11k_base *ab)
{
	ath11k_debugfs_soc_destroy(ab);
	ath11k_dp_free(ab);
	ath11k_reg_free(ab);
	ath11k_qmi_deinit_service(ab);
}

static int ath11k_core_pdev_create(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_debugfs_pdev_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create core pdev debugfs: %d\n", ret);
		return ret;
	}

	ret = ath11k_mac_register(ab);
	if (ret) {
		ath11k_err(ab, "failed register the radio with mac80211: %d\n", ret);
		goto err_pdev_debug;
	}

	ret = ath11k_dp_pdev_alloc(ab);
	if (ret) {
		ath11k_err(ab, "failed to attach DP pdev: %d\n", ret);
		goto err_mac_unregister;
	}

	ret = ath11k_thermal_register(ab);
	if (ret) {
		ath11k_err(ab, "could not register thermal device: %d\n",
			   ret);
		goto err_dp_pdev_free;
	}

	ret = ath11k_spectral_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init spectral %d\n", ret);
		goto err_thermal_unregister;
	}

	return 0;

err_thermal_unregister:
	ath11k_thermal_unregister(ab);
err_dp_pdev_free:
	ath11k_dp_pdev_free(ab);
err_mac_unregister:
	ath11k_mac_unregister(ab);
err_pdev_debug:
	ath11k_debugfs_pdev_destroy(ab);

	return ret;
}

static void ath11k_core_pdev_destroy(struct ath11k_base *ab)
{
	ath11k_spectral_deinit(ab);
	ath11k_thermal_unregister(ab);
	ath11k_mac_unregister(ab);
	ath11k_hif_irq_disable(ab);
	ath11k_dp_pdev_free(ab);
	ath11k_debugfs_pdev_destroy(ab);
}

static int ath11k_core_start(struct ath11k_base *ab,
			     enum ath11k_firmware_mode mode)
{
	int ret;

	ret = ath11k_qmi_firmware_start(ab, mode);
	if (ret) {
		ath11k_err(ab, "failed to attach wmi: %d\n", ret);
		return ret;
	}

	ret = ath11k_wmi_attach(ab);
	if (ret) {
		ath11k_err(ab, "failed to attach wmi: %d\n", ret);
		goto err_firmware_stop;
	}

	ret = ath11k_htc_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init htc: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath11k_hif_start(ab);
	if (ret) {
		ath11k_err(ab, "failed to start HIF: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath11k_htc_wait_target(&ab->htc);
	if (ret) {
		ath11k_err(ab, "failed to connect to HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_dp_htt_connect(&ab->dp);
	if (ret) {
		ath11k_err(ab, "failed to connect to HTT: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_wmi_connect(ab);
	if (ret) {
		ath11k_err(ab, "failed to connect wmi: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_htc_start(&ab->htc);
	if (ret) {
		ath11k_err(ab, "failed to start HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_wmi_wait_for_service_ready(ab);
	if (ret) {
		ath11k_err(ab, "failed to receive wmi service ready event: %d\n",
			   ret);
		goto err_hif_stop;
	}

	ret = ath11k_mac_allocate(ab);
	if (ret) {
		ath11k_err(ab, "failed to create new hw device with mac80211 :%d\n",
			   ret);
		goto err_hif_stop;
	}

	ath11k_dp_pdev_pre_alloc(ab);

	ret = ath11k_dp_pdev_reo_setup(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize reo destination rings: %d\n", ret);
		goto err_mac_destroy;
	}

	ret = ath11k_wmi_cmd_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to send wmi init cmd: %d\n", ret);
		goto err_reo_cleanup;
	}

	ret = ath11k_wmi_wait_for_unified_ready(ab);
	if (ret) {
		ath11k_err(ab, "failed to receive wmi unified ready event: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	/* put hardware to DBS mode */
	if (ab->hw_params.single_pdev_only) {
		ret = ath11k_wmi_set_hw_mode(ab, WMI_HOST_HW_MODE_DBS);
		if (ret) {
			ath11k_err(ab, "failed to send dbs mode: %d\n", ret);
			goto err_hif_stop;
		}
	}

	ret = ath11k_dp_tx_htt_h2t_ver_req_msg(ab);
	if (ret) {
		ath11k_err(ab, "failed to send htt version request message: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	return 0;

err_reo_cleanup:
	ath11k_dp_pdev_reo_cleanup(ab);
err_mac_destroy:
	ath11k_mac_destroy(ab);
err_hif_stop:
	ath11k_hif_stop(ab);
err_wmi_detach:
	ath11k_wmi_detach(ab);
err_firmware_stop:
	ath11k_qmi_firmware_stop(ab);

	return ret;
}

int ath11k_core_qmi_firmware_ready(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_ce_init_pipes(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize CE: %d\n", ret);
		return ret;
	}

	ret = ath11k_dp_alloc(ab);
	if (ret) {
		ath11k_err(ab, "failed to init DP: %d\n", ret);
		return ret;
	}

	switch (ath11k_crypto_mode) {
	case ATH11K_CRYPT_MODE_SW:
		set_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags);
		set_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);
		break;
	case ATH11K_CRYPT_MODE_HW:
		clear_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags);
		clear_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);
		break;
	default:
		ath11k_info(ab, "invalid crypto_mode: %d\n", ath11k_crypto_mode);
		return -EINVAL;
	}

	if (ath11k_frame_mode == ATH11K_HW_TXRX_RAW)
		set_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);

	mutex_lock(&ab->core_lock);
	ret = ath11k_core_start(ab, ATH11K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath11k_err(ab, "failed to start core: %d\n", ret);
		goto err_dp_free;
	}

	ret = ath11k_core_pdev_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create pdev core: %d\n", ret);
		goto err_core_stop;
	}
	ath11k_hif_irq_enable(ab);
	mutex_unlock(&ab->core_lock);

	return 0;

err_core_stop:
	ath11k_core_stop(ab);
	ath11k_mac_destroy(ab);
err_dp_free:
	ath11k_dp_free(ab);
	mutex_unlock(&ab->core_lock);
	return ret;
}

static int ath11k_core_reconfigure_on_crash(struct ath11k_base *ab)
{
	int ret;

	mutex_lock(&ab->core_lock);
	ath11k_thermal_unregister(ab);
	ath11k_hif_irq_disable(ab);
	ath11k_dp_pdev_free(ab);
	ath11k_spectral_deinit(ab);
	ath11k_hif_stop(ab);
	ath11k_wmi_detach(ab);
	ath11k_dp_pdev_reo_cleanup(ab);
	mutex_unlock(&ab->core_lock);

	ath11k_dp_free(ab);
	ath11k_hal_srng_deinit(ab);

	ab->free_vdev_map = (1LL << (ab->num_radios * TARGET_NUM_VDEVS)) - 1;

	ret = ath11k_hal_srng_init(ab);
	if (ret)
		return ret;

	clear_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags);

	ret = ath11k_core_qmi_firmware_ready(ab);
	if (ret)
		goto err_hal_srng_deinit;

	clear_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags);

	return 0;

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);
	return ret;
}

void ath11k_core_halt(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	ar->num_created_vdevs = 0;
	ar->allocated_vdev_map = 0;

	ath11k_mac_scan_finish(ar);
	ath11k_mac_peer_cleanup_all(ar);
	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->regd_update_work);

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx], NULL);
	synchronize_rcu();
	INIT_LIST_HEAD(&ar->arvifs);
	idr_init(&ar->txmgmt_idr);
}

static void ath11k_core_restart(struct work_struct *work)
{
	struct ath11k_base *ab = container_of(work, struct ath11k_base, restart_work);
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i, ret = 0;

	spin_lock_bh(&ab->base_lock);
	ab->stats.fw_crash_counter++;
	spin_unlock_bh(&ab->base_lock);

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar || ar->state == ATH11K_STATE_OFF)
			continue;

		ieee80211_stop_queues(ar->hw);
		ath11k_mac_drain_tx(ar);
		complete(&ar->scan.started);
		complete(&ar->scan.completed);
		complete(&ar->peer_assoc_done);
		complete(&ar->peer_delete_done);
		complete(&ar->install_key_done);
		complete(&ar->vdev_setup_done);
		complete(&ar->vdev_delete_done);
		complete(&ar->bss_survey_done);
		complete(&ar->thermal.wmi_sync);

		wake_up(&ar->dp.tx_empty_waitq);
		idr_for_each(&ar->txmgmt_idr,
			     ath11k_mac_tx_mgmt_pending_free, ar);
		idr_destroy(&ar->txmgmt_idr);
	}

	wake_up(&ab->wmi_ab.tx_credits_wq);
	wake_up(&ab->peer_mapping_wq);

	ret = ath11k_core_reconfigure_on_crash(ab);
	if (ret) {
		ath11k_err(ab, "failed to reconfigure driver on crash recovery\n");
		return;
	}

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar || ar->state == ATH11K_STATE_OFF)
			continue;

		mutex_lock(&ar->conf_mutex);

		switch (ar->state) {
		case ATH11K_STATE_ON:
			ar->state = ATH11K_STATE_RESTARTING;
			ath11k_core_halt(ar);
			ieee80211_restart_hw(ar->hw);
			break;
		case ATH11K_STATE_OFF:
			ath11k_warn(ab,
				    "cannot restart radio %d that hasn't been started\n",
				    i);
			break;
		case ATH11K_STATE_RESTARTING:
			break;
		case ATH11K_STATE_RESTARTED:
			ar->state = ATH11K_STATE_WEDGED;
			fallthrough;
		case ATH11K_STATE_WEDGED:
			ath11k_warn(ab,
				    "device is wedged, will not restart radio %d\n", i);
			break;
		}
		mutex_unlock(&ar->conf_mutex);
	}
	complete(&ab->driver_recovery);
}

static int ath11k_init_hw_params(struct ath11k_base *ab)
{
	const struct ath11k_hw_params *hw_params = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath11k_hw_params); i++) {
		hw_params = &ath11k_hw_params[i];

		if (hw_params->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath11k_hw_params)) {
		ath11k_err(ab, "Unsupported hardware version: 0x%x\n", ab->hw_rev);
		return -EINVAL;
	}

	ab->hw_params = *hw_params;

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "Hardware name %s\n", ab->hw_params.name);

	return 0;
}

int ath11k_core_pre_init(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_init_hw_params(ab);
	if (ret) {
		ath11k_err(ab, "failed to get hw params: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_pre_init);

int ath11k_core_init(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_core_soc_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create soc core: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_init);

void ath11k_core_deinit(struct ath11k_base *ab)
{
	mutex_lock(&ab->core_lock);

	ath11k_core_pdev_destroy(ab);
	ath11k_core_stop(ab);

	mutex_unlock(&ab->core_lock);

	ath11k_hif_power_down(ab);
	ath11k_mac_destroy(ab);
	ath11k_core_soc_destroy(ab);
}
EXPORT_SYMBOL(ath11k_core_deinit);

void ath11k_core_free(struct ath11k_base *ab)
{
	kfree(ab);
}
EXPORT_SYMBOL(ath11k_core_free);

struct ath11k_base *ath11k_core_alloc(struct device *dev, size_t priv_size,
				      enum ath11k_bus bus,
				      const struct ath11k_bus_params *bus_params)
{
	struct ath11k_base *ab;

	ab = kzalloc(sizeof(*ab) + priv_size, GFP_KERNEL);
	if (!ab)
		return NULL;

	init_completion(&ab->driver_recovery);

	ab->workqueue = create_singlethread_workqueue("ath11k_wq");
	if (!ab->workqueue)
		goto err_sc_free;

	mutex_init(&ab->core_lock);
	spin_lock_init(&ab->base_lock);

	INIT_LIST_HEAD(&ab->peers);
	init_waitqueue_head(&ab->peer_mapping_wq);
	init_waitqueue_head(&ab->wmi_ab.tx_credits_wq);
	init_waitqueue_head(&ab->qmi.cold_boot_waitq);
	INIT_WORK(&ab->restart_work, ath11k_core_restart);
	timer_setup(&ab->rx_replenish_retry, ath11k_ce_rx_replenish_retry, 0);
	init_completion(&ab->htc_suspend);
	init_completion(&ab->wow.wakeup_completed);

	ab->dev = dev;
	ab->bus_params = *bus_params;
	ab->hif.bus = bus;

	return ab;

err_sc_free:
	kfree(ab);
	return NULL;
}
EXPORT_SYMBOL(ath11k_core_alloc);

MODULE_DESCRIPTION("Core module for Qualcomm Atheros 802.11ax wireless LAN cards.");
MODULE_LICENSE("Dual BSD/GPL");
