// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include "fw.h"
#include "debugfs.h"
#include "wow.h"

unsigned int ath12k_debug_mask;
module_param_named(debug_mask, ath12k_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");

static int ath12k_core_rfkill_config(struct ath12k_base *ab)
{
	struct ath12k *ar;
	int ret = 0, i;

	if (!(ab->target_caps.sys_cap_info & WMI_SYS_CAP_INFO_RFKILL))
		return 0;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;

		ret = ath12k_mac_rfkill_config(ar);
		if (ret && ret != -EOPNOTSUPP) {
			ath12k_warn(ab, "failed to configure rfkill: %d", ret);
			return ret;
		}
	}

	return ret;
}

/* Check if we need to continue with suspend/resume operation.
 * Return:
 *	a negative value: error happens and don't continue.
 *	0:  no error but don't continue.
 *	positive value: no error and do continue.
 */
static int ath12k_core_continue_suspend_resume(struct ath12k_base *ab)
{
	struct ath12k *ar;

	if (!ab->hw_params->supports_suspend)
		return -EOPNOTSUPP;

	/* so far single_pdev_only chips have supports_suspend as true
	 * so pass 0 as a dummy pdev_id here.
	 */
	ar = ab->pdevs[0].ar;
	if (!ar || !ar->ah || ar->ah->state != ATH12K_HW_STATE_OFF)
		return 0;

	return 1;
}

int ath12k_core_suspend(struct ath12k_base *ab)
{
	struct ath12k *ar;
	int ret, i;

	ret = ath12k_core_continue_suspend_resume(ab);
	if (ret <= 0)
		return ret;

	for (i = 0; i < ab->num_radios; i++) {
		ar = ab->pdevs[i].ar;
		if (!ar)
			continue;

		wiphy_lock(ath12k_ar_to_hw(ar)->wiphy);

		ret = ath12k_mac_wait_tx_complete(ar);
		if (ret) {
			wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);
			ath12k_warn(ab, "failed to wait tx complete: %d\n", ret);
			return ret;
		}

		wiphy_unlock(ath12k_ar_to_hw(ar)->wiphy);
	}

	/* PM framework skips suspend_late/resume_early callbacks
	 * if other devices report errors in their suspend callbacks.
	 * However ath12k_core_resume() would still be called because
	 * here we return success thus kernel put us on dpm_suspended_list.
	 * Since we won't go through a power down/up cycle, there is
	 * no chance to call complete(&ab->restart_completed) in
	 * ath12k_core_restart(), making ath12k_core_resume() timeout.
	 * So call it here to avoid this issue. This also works in case
	 * no error happens thus suspend_late/resume_early get called,
	 * because it will be reinitialized in ath12k_core_resume_early().
	 */
	complete(&ab->restart_completed);

	return 0;
}
EXPORT_SYMBOL(ath12k_core_suspend);

int ath12k_core_suspend_late(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_core_continue_suspend_resume(ab);
	if (ret <= 0)
		return ret;

	ath12k_acpi_stop(ab);

	ath12k_hif_irq_disable(ab);
	ath12k_hif_ce_irq_disable(ab);

	ath12k_hif_power_down(ab, true);

	return 0;
}
EXPORT_SYMBOL(ath12k_core_suspend_late);

int ath12k_core_resume_early(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_core_continue_suspend_resume(ab);
	if (ret <= 0)
		return ret;

	reinit_completion(&ab->restart_completed);
	ret = ath12k_hif_power_up(ab);
	if (ret)
		ath12k_warn(ab, "failed to power up hif during resume: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL(ath12k_core_resume_early);

int ath12k_core_resume(struct ath12k_base *ab)
{
	long time_left;
	int ret;

	ret = ath12k_core_continue_suspend_resume(ab);
	if (ret <= 0)
		return ret;

	time_left = wait_for_completion_timeout(&ab->restart_completed,
						ATH12K_RESET_TIMEOUT_HZ);
	if (time_left == 0) {
		ath12k_warn(ab, "timeout while waiting for restart complete");
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL(ath12k_core_resume);

static int __ath12k_core_create_board_name(struct ath12k_base *ab, char *name,
					   size_t name_len, bool with_variant,
					   bool bus_type_mode)
{
	/* strlen(',variant=') + strlen(ab->qmi.target.bdf_ext) */
	char variant[9 + ATH12K_QMI_BDF_EXT_STR_LENGTH] = { 0 };

	if (with_variant && ab->qmi.target.bdf_ext[0] != '\0')
		scnprintf(variant, sizeof(variant), ",variant=%s",
			  ab->qmi.target.bdf_ext);

	switch (ab->id.bdf_search) {
	case ATH12K_BDF_SEARCH_BUS_AND_BOARD:
		if (bus_type_mode)
			scnprintf(name, name_len,
				  "bus=%s",
				  ath12k_bus_str(ab->hif.bus));
		else
			scnprintf(name, name_len,
				  "bus=%s,vendor=%04x,device=%04x,subsystem-vendor=%04x,subsystem-device=%04x,qmi-chip-id=%d,qmi-board-id=%d%s",
				  ath12k_bus_str(ab->hif.bus),
				  ab->id.vendor, ab->id.device,
				  ab->id.subsystem_vendor,
				  ab->id.subsystem_device,
				  ab->qmi.target.chip_id,
				  ab->qmi.target.board_id,
				  variant);
		break;
	default:
		scnprintf(name, name_len,
			  "bus=%s,qmi-chip-id=%d,qmi-board-id=%d%s",
			  ath12k_bus_str(ab->hif.bus),
			  ab->qmi.target.chip_id,
			  ab->qmi.target.board_id, variant);
		break;
	}

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot using board name '%s'\n", name);

	return 0;
}

static int ath12k_core_create_board_name(struct ath12k_base *ab, char *name,
					 size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, true, false);
}

static int ath12k_core_create_fallback_board_name(struct ath12k_base *ab, char *name,
						  size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, false, false);
}

static int ath12k_core_create_bus_type_board_name(struct ath12k_base *ab, char *name,
						  size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, false, true);
}

const struct firmware *ath12k_core_firmware_request(struct ath12k_base *ab,
						    const char *file)
{
	const struct firmware *fw;
	char path[100];
	int ret;

	if (!file)
		return ERR_PTR(-ENOENT);

	ath12k_core_create_firmware_path(ab, file, path, sizeof(path));

	ret = firmware_request_nowarn(&fw, path, ab->dev);
	if (ret)
		return ERR_PTR(ret);

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot firmware request %s size %zu\n",
		   path, fw->size);

	return fw;
}

void ath12k_core_free_bdf(struct ath12k_base *ab, struct ath12k_board_data *bd)
{
	if (!IS_ERR(bd->fw))
		release_firmware(bd->fw);

	memset(bd, 0, sizeof(*bd));
}

static int ath12k_core_parse_bd_ie_board(struct ath12k_base *ab,
					 struct ath12k_board_data *bd,
					 const void *buf, size_t buf_len,
					 const char *boardname,
					 int ie_id,
					 int name_id,
					 int data_id)
{
	const struct ath12k_fw_ie *hdr;
	bool name_match_found;
	int ret, board_ie_id;
	size_t board_ie_len;
	const void *board_ie_data;

	name_match_found = false;

	/* go through ATH12K_BD_IE_BOARD_/ATH12K_BD_IE_REGDB_ elements */
	while (buf_len > sizeof(struct ath12k_fw_ie)) {
		hdr = buf;
		board_ie_id = le32_to_cpu(hdr->id);
		board_ie_len = le32_to_cpu(hdr->len);
		board_ie_data = hdr->data;

		buf_len -= sizeof(*hdr);
		buf += sizeof(*hdr);

		if (buf_len < ALIGN(board_ie_len, 4)) {
			ath12k_err(ab, "invalid %s length: %zu < %zu\n",
				   ath12k_bd_ie_type_str(ie_id),
				   buf_len, ALIGN(board_ie_len, 4));
			ret = -EINVAL;
			goto out;
		}

		if (board_ie_id == name_id) {
			ath12k_dbg_dump(ab, ATH12K_DBG_BOOT, "board name", "",
					board_ie_data, board_ie_len);

			if (board_ie_len != strlen(boardname))
				goto next;

			ret = memcmp(board_ie_data, boardname, strlen(boardname));
			if (ret)
				goto next;

			name_match_found = true;
			ath12k_dbg(ab, ATH12K_DBG_BOOT,
				   "boot found match %s for name '%s'",
				   ath12k_bd_ie_type_str(ie_id),
				   boardname);
		} else if (board_ie_id == data_id) {
			if (!name_match_found)
				/* no match found */
				goto next;

			ath12k_dbg(ab, ATH12K_DBG_BOOT,
				   "boot found %s for '%s'",
				   ath12k_bd_ie_type_str(ie_id),
				   boardname);

			bd->data = board_ie_data;
			bd->len = board_ie_len;

			ret = 0;
			goto out;
		} else {
			ath12k_warn(ab, "unknown %s id found: %d\n",
				    ath12k_bd_ie_type_str(ie_id),
				    board_ie_id);
		}
next:
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

static int ath12k_core_fetch_board_data_api_n(struct ath12k_base *ab,
					      struct ath12k_board_data *bd,
					      const char *boardname,
					      int ie_id_match,
					      int name_id,
					      int data_id)
{
	size_t len, magic_len;
	const u8 *data;
	char *filename, filepath[100];
	size_t ie_len;
	struct ath12k_fw_ie *hdr;
	int ret, ie_id;

	filename = ATH12K_BOARD_API2_FILE;

	if (!bd->fw)
		bd->fw = ath12k_core_firmware_request(ab, filename);

	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	data = bd->fw->data;
	len = bd->fw->size;

	ath12k_core_create_firmware_path(ab, filename,
					 filepath, sizeof(filepath));

	/* magic has extra null byte padded */
	magic_len = strlen(ATH12K_BOARD_MAGIC) + 1;
	if (len < magic_len) {
		ath12k_err(ab, "failed to find magic value in %s, file too short: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	if (memcmp(data, ATH12K_BOARD_MAGIC, magic_len)) {
		ath12k_err(ab, "found invalid board magic\n");
		ret = -EINVAL;
		goto err;
	}

	/* magic is padded to 4 bytes */
	magic_len = ALIGN(magic_len, 4);
	if (len < magic_len) {
		ath12k_err(ab, "failed: %s too small to contain board data, len: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	data += magic_len;
	len -= magic_len;

	while (len > sizeof(struct ath12k_fw_ie)) {
		hdr = (struct ath12k_fw_ie *)data;
		ie_id = le32_to_cpu(hdr->id);
		ie_len = le32_to_cpu(hdr->len);

		len -= sizeof(*hdr);
		data = hdr->data;

		if (len < ALIGN(ie_len, 4)) {
			ath12k_err(ab, "invalid length for board ie_id %d ie_len %zu len %zu\n",
				   ie_id, ie_len, len);
			ret = -EINVAL;
			goto err;
		}

		if (ie_id == ie_id_match) {
			ret = ath12k_core_parse_bd_ie_board(ab, bd, data,
							    ie_len,
							    boardname,
							    ie_id_match,
							    name_id,
							    data_id);
			if (ret == -ENOENT)
				/* no match found, continue */
				goto next;
			else if (ret)
				/* there was an error, bail out */
				goto err;
			/* either found or error, so stop searching */
			goto out;
		}
next:
		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

out:
	if (!bd->data || !bd->len) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "failed to fetch %s for %s from %s\n",
			   ath12k_bd_ie_type_str(ie_id_match),
			   boardname, filepath);
		ret = -ENODATA;
		goto err;
	}

	return 0;

err:
	ath12k_core_free_bdf(ab, bd);
	return ret;
}

int ath12k_core_fetch_board_data_api_1(struct ath12k_base *ab,
				       struct ath12k_board_data *bd,
				       char *filename)
{
	bd->fw = ath12k_core_firmware_request(ab, filename);
	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	bd->data = bd->fw->data;
	bd->len = bd->fw->size;

	return 0;
}

#define BOARD_NAME_SIZE 200
int ath12k_core_fetch_bdf(struct ath12k_base *ab, struct ath12k_board_data *bd)
{
	char boardname[BOARD_NAME_SIZE], fallback_boardname[BOARD_NAME_SIZE];
	char *filename, filepath[100];
	int bd_api;
	int ret;

	filename = ATH12K_BOARD_API2_FILE;

	ret = ath12k_core_create_board_name(ab, boardname, sizeof(boardname));
	if (ret) {
		ath12k_err(ab, "failed to create board name: %d", ret);
		return ret;
	}

	bd_api = 2;
	ret = ath12k_core_fetch_board_data_api_n(ab, bd, boardname,
						 ATH12K_BD_IE_BOARD,
						 ATH12K_BD_IE_BOARD_NAME,
						 ATH12K_BD_IE_BOARD_DATA);
	if (!ret)
		goto success;

	ret = ath12k_core_create_fallback_board_name(ab, fallback_boardname,
						     sizeof(fallback_boardname));
	if (ret) {
		ath12k_err(ab, "failed to create fallback board name: %d", ret);
		return ret;
	}

	ret = ath12k_core_fetch_board_data_api_n(ab, bd, fallback_boardname,
						 ATH12K_BD_IE_BOARD,
						 ATH12K_BD_IE_BOARD_NAME,
						 ATH12K_BD_IE_BOARD_DATA);
	if (!ret)
		goto success;

	bd_api = 1;
	ret = ath12k_core_fetch_board_data_api_1(ab, bd, ATH12K_DEFAULT_BOARD_FILE);
	if (ret) {
		ath12k_core_create_firmware_path(ab, filename,
						 filepath, sizeof(filepath));
		ath12k_err(ab, "failed to fetch board data for %s from %s\n",
			   boardname, filepath);
		if (memcmp(boardname, fallback_boardname, strlen(boardname)))
			ath12k_err(ab, "failed to fetch board data for %s from %s\n",
				   fallback_boardname, filepath);

		ath12k_err(ab, "failed to fetch board.bin from %s\n",
			   ab->hw_params->fw.dir);
		return ret;
	}

success:
	ath12k_dbg(ab, ATH12K_DBG_BOOT, "using board api %d\n", bd_api);
	return 0;
}

int ath12k_core_fetch_regdb(struct ath12k_base *ab, struct ath12k_board_data *bd)
{
	char boardname[BOARD_NAME_SIZE], default_boardname[BOARD_NAME_SIZE];
	int ret;

	ret = ath12k_core_create_board_name(ab, boardname, BOARD_NAME_SIZE);
	if (ret) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "failed to create board name for regdb: %d", ret);
		goto exit;
	}

	ret = ath12k_core_fetch_board_data_api_n(ab, bd, boardname,
						 ATH12K_BD_IE_REGDB,
						 ATH12K_BD_IE_REGDB_NAME,
						 ATH12K_BD_IE_REGDB_DATA);
	if (!ret)
		goto exit;

	ret = ath12k_core_create_bus_type_board_name(ab, default_boardname,
						     BOARD_NAME_SIZE);
	if (ret) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "failed to create default board name for regdb: %d", ret);
		goto exit;
	}

	ret = ath12k_core_fetch_board_data_api_n(ab, bd, default_boardname,
						 ATH12K_BD_IE_REGDB,
						 ATH12K_BD_IE_REGDB_NAME,
						 ATH12K_BD_IE_REGDB_DATA);
	if (!ret)
		goto exit;

	ret = ath12k_core_fetch_board_data_api_1(ab, bd, ATH12K_REGDB_FILE_NAME);
	if (ret)
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "failed to fetch %s from %s\n",
			   ATH12K_REGDB_FILE_NAME, ab->hw_params->fw.dir);

exit:
	if (!ret)
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "fetched regdb\n");

	return ret;
}

u32 ath12k_core_get_max_station_per_radio(struct ath12k_base *ab)
{
	if (ab->num_radios == 2)
		return TARGET_NUM_STATIONS_DBS;
	else if (ab->num_radios == 3)
		return TARGET_NUM_PEERS_PDEV_DBS_SBS;
	return TARGET_NUM_STATIONS_SINGLE;
}

u32 ath12k_core_get_max_peers_per_radio(struct ath12k_base *ab)
{
	if (ab->num_radios == 2)
		return TARGET_NUM_PEERS_PDEV_DBS;
	else if (ab->num_radios == 3)
		return TARGET_NUM_PEERS_PDEV_DBS_SBS;
	return TARGET_NUM_PEERS_PDEV_SINGLE;
}

u32 ath12k_core_get_max_num_tids(struct ath12k_base *ab)
{
	if (ab->num_radios == 2)
		return TARGET_NUM_TIDS(DBS);
	else if (ab->num_radios == 3)
		return TARGET_NUM_TIDS(DBS_SBS);
	return TARGET_NUM_TIDS(SINGLE);
}

static void ath12k_core_stop(struct ath12k_base *ab)
{
	if (!test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath12k_qmi_firmware_stop(ab);

	ath12k_acpi_stop(ab);

	ath12k_dp_rx_pdev_reo_cleanup(ab);
	ath12k_hif_stop(ab);
	ath12k_wmi_detach(ab);
	ath12k_dp_free(ab);

	/* De-Init of components as needed */
}

static void ath12k_core_check_bdfext(const struct dmi_header *hdr, void *data)
{
	struct ath12k_base *ab = data;
	const char *magic = ATH12K_SMBIOS_BDF_EXT_MAGIC;
	struct ath12k_smbios_bdf *smbios = (struct ath12k_smbios_bdf *)hdr;
	ssize_t copied;
	size_t len;
	int i;

	if (ab->qmi.target.bdf_ext[0] != '\0')
		return;

	if (hdr->type != ATH12K_SMBIOS_BDF_EXT_TYPE)
		return;

	if (hdr->length != ATH12K_SMBIOS_BDF_EXT_LENGTH) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "wrong smbios bdf ext type length (%d).\n",
			   hdr->length);
		return;
	}

	if (!smbios->bdf_enabled) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "bdf variant name not found.\n");
		return;
	}

	/* Only one string exists (per spec) */
	if (memcmp(smbios->bdf_ext, magic, strlen(magic)) != 0) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "bdf variant magic does not match.\n");
		return;
	}

	len = min_t(size_t,
		    strlen(smbios->bdf_ext), sizeof(ab->qmi.target.bdf_ext));
	for (i = 0; i < len; i++) {
		if (!isascii(smbios->bdf_ext[i]) || !isprint(smbios->bdf_ext[i])) {
			ath12k_dbg(ab, ATH12K_DBG_BOOT,
				   "bdf variant name contains non ascii chars.\n");
			return;
		}
	}

	/* Copy extension name without magic prefix */
	copied = strscpy(ab->qmi.target.bdf_ext, smbios->bdf_ext + strlen(magic),
			 sizeof(ab->qmi.target.bdf_ext));
	if (copied < 0) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "bdf variant string is longer than the buffer can accommodate\n");
		return;
	}

	ath12k_dbg(ab, ATH12K_DBG_BOOT,
		   "found and validated bdf variant smbios_type 0x%x bdf %s\n",
		   ATH12K_SMBIOS_BDF_EXT_TYPE, ab->qmi.target.bdf_ext);
}

int ath12k_core_check_smbios(struct ath12k_base *ab)
{
	ab->qmi.target.bdf_ext[0] = '\0';
	dmi_walk(ath12k_core_check_bdfext, ab);

	if (ab->qmi.target.bdf_ext[0] == '\0')
		return -ENODATA;

	return 0;
}

static int ath12k_core_soc_create(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_qmi_init_service(ab);
	if (ret) {
		ath12k_err(ab, "failed to initialize qmi :%d\n", ret);
		return ret;
	}

	ath12k_debugfs_soc_create(ab);

	ret = ath12k_hif_power_up(ab);
	if (ret) {
		ath12k_err(ab, "failed to power up :%d\n", ret);
		goto err_qmi_deinit;
	}

	return 0;

err_qmi_deinit:
	ath12k_debugfs_soc_destroy(ab);
	ath12k_qmi_deinit_service(ab);
	return ret;
}

static void ath12k_core_soc_destroy(struct ath12k_base *ab)
{
	ath12k_hif_power_down(ab, false);
	ath12k_reg_free(ab);
	ath12k_debugfs_soc_destroy(ab);
	ath12k_qmi_deinit_service(ab);
}

static int ath12k_core_pdev_create(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_dp_pdev_alloc(ab);
	if (ret) {
		ath12k_err(ab, "failed to attach DP pdev: %d\n", ret);
		return ret;
	}

	return 0;
}

static void ath12k_core_pdev_destroy(struct ath12k_base *ab)
{
	ath12k_dp_pdev_free(ab);
}

static int ath12k_core_start(struct ath12k_base *ab,
			     enum ath12k_firmware_mode mode)
{
	int ret;

	ret = ath12k_wmi_attach(ab);
	if (ret) {
		ath12k_err(ab, "failed to attach wmi: %d\n", ret);
		return ret;
	}

	ret = ath12k_htc_init(ab);
	if (ret) {
		ath12k_err(ab, "failed to init htc: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath12k_hif_start(ab);
	if (ret) {
		ath12k_err(ab, "failed to start HIF: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath12k_htc_wait_target(&ab->htc);
	if (ret) {
		ath12k_err(ab, "failed to connect to HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath12k_dp_htt_connect(&ab->dp);
	if (ret) {
		ath12k_err(ab, "failed to connect to HTT: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath12k_wmi_connect(ab);
	if (ret) {
		ath12k_err(ab, "failed to connect wmi: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath12k_htc_start(&ab->htc);
	if (ret) {
		ath12k_err(ab, "failed to start HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath12k_wmi_wait_for_service_ready(ab);
	if (ret) {
		ath12k_err(ab, "failed to receive wmi service ready event: %d\n",
			   ret);
		goto err_hif_stop;
	}

	ath12k_dp_cc_config(ab);

	ret = ath12k_dp_rx_pdev_reo_setup(ab);
	if (ret) {
		ath12k_err(ab, "failed to initialize reo destination rings: %d\n", ret);
		goto err_hif_stop;
	}

	ath12k_dp_hal_rx_desc_init(ab);

	ret = ath12k_wmi_cmd_init(ab);
	if (ret) {
		ath12k_err(ab, "failed to send wmi init cmd: %d\n", ret);
		goto err_reo_cleanup;
	}

	ret = ath12k_wmi_wait_for_unified_ready(ab);
	if (ret) {
		ath12k_err(ab, "failed to receive wmi unified ready event: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	/* put hardware to DBS mode */
	if (ab->hw_params->single_pdev_only) {
		ret = ath12k_wmi_set_hw_mode(ab, WMI_HOST_HW_MODE_DBS);
		if (ret) {
			ath12k_err(ab, "failed to send dbs mode: %d\n", ret);
			goto err_reo_cleanup;
		}
	}

	ret = ath12k_dp_tx_htt_h2t_ver_req_msg(ab);
	if (ret) {
		ath12k_err(ab, "failed to send htt version request message: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	ret = ath12k_acpi_start(ab);
	if (ret)
		/* ACPI is optional so continue in case of an error */
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "acpi failed: %d\n", ret);

	return 0;

err_reo_cleanup:
	ath12k_dp_rx_pdev_reo_cleanup(ab);
err_hif_stop:
	ath12k_hif_stop(ab);
err_wmi_detach:
	ath12k_wmi_detach(ab);
	return ret;
}

static int ath12k_core_start_firmware(struct ath12k_base *ab,
				      enum ath12k_firmware_mode mode)
{
	int ret;

	ath12k_ce_get_shadow_config(ab, &ab->qmi.ce_cfg.shadow_reg_v3,
				    &ab->qmi.ce_cfg.shadow_reg_v3_len);

	ret = ath12k_qmi_firmware_start(ab, mode);
	if (ret) {
		ath12k_err(ab, "failed to send firmware start: %d\n", ret);
		return ret;
	}

	return ret;
}

int ath12k_core_qmi_firmware_ready(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_core_start_firmware(ab, ATH12K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath12k_err(ab, "failed to start firmware: %d\n", ret);
		return ret;
	}

	ret = ath12k_ce_init_pipes(ab);
	if (ret) {
		ath12k_err(ab, "failed to initialize CE: %d\n", ret);
		goto err_firmware_stop;
	}

	ret = ath12k_dp_alloc(ab);
	if (ret) {
		ath12k_err(ab, "failed to init DP: %d\n", ret);
		goto err_firmware_stop;
	}

	mutex_lock(&ab->core_lock);
	ret = ath12k_core_start(ab, ATH12K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath12k_err(ab, "failed to start core: %d\n", ret);
		goto err_dp_free;
	}

	ret = ath12k_mac_allocate(ab);
	if (ret) {
		ath12k_err(ab, "failed to create new hw device with mac80211 :%d\n",
			   ret);
		goto err_core_stop;
	}

	ret = ath12k_mac_register(ab);
	if (ret) {
		ath12k_err(ab, "failed register the radio with mac80211: %d\n", ret);
		goto err_mac_destroy;
	}

	ret = ath12k_core_pdev_create(ab);
	if (ret) {
		ath12k_err(ab, "failed to create pdev core: %d\n", ret);
		goto err_mac_unregister;
	}

	ath12k_hif_irq_enable(ab);

	ret = ath12k_core_rfkill_config(ab);
	if (ret && ret != -EOPNOTSUPP) {
		ath12k_err(ab, "failed to config rfkill: %d\n", ret);
		goto err_hif_irq_disable;
	}

	mutex_unlock(&ab->core_lock);

	return 0;

err_hif_irq_disable:
	ath12k_hif_irq_disable(ab);
	ath12k_core_pdev_destroy(ab);
err_mac_unregister:
	ath12k_mac_unregister(ab);
err_mac_destroy:
	ath12k_mac_destroy(ab);
err_core_stop:
	ath12k_core_stop(ab);
err_dp_free:
	ath12k_dp_free(ab);
	mutex_unlock(&ab->core_lock);
err_firmware_stop:
	ath12k_qmi_firmware_stop(ab);

	return ret;
}

static int ath12k_core_reconfigure_on_crash(struct ath12k_base *ab)
{
	int ret;

	mutex_lock(&ab->core_lock);
	ath12k_dp_pdev_free(ab);
	ath12k_ce_cleanup_pipes(ab);
	ath12k_wmi_detach(ab);
	ath12k_dp_rx_pdev_reo_cleanup(ab);
	mutex_unlock(&ab->core_lock);

	ath12k_dp_free(ab);
	ath12k_hal_srng_deinit(ab);

	ab->free_vdev_map = (1LL << (ab->num_radios * TARGET_NUM_VDEVS)) - 1;

	ret = ath12k_hal_srng_init(ab);
	if (ret)
		return ret;

	clear_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags);

	ret = ath12k_core_qmi_firmware_ready(ab);
	if (ret)
		goto err_hal_srng_deinit;

	clear_bit(ATH12K_FLAG_RECOVERY, &ab->dev_flags);

	return 0;

err_hal_srng_deinit:
	ath12k_hal_srng_deinit(ab);
	return ret;
}

static void ath12k_rfkill_work(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, rfkill_work);
	struct ath12k *ar;
	struct ath12k_hw *ah;
	struct ieee80211_hw *hw;
	bool rfkill_radio_on;
	int i, j;

	spin_lock_bh(&ab->base_lock);
	rfkill_radio_on = ab->rfkill_radio_on;
	spin_unlock_bh(&ab->base_lock);

	for (i = 0; i < ath12k_get_num_hw(ab); i++) {
		ah = ath12k_ab_to_ah(ab, i);
		if (!ah)
			continue;

		for (j = 0; j < ah->num_radio; j++) {
			ar = &ah->radio[j];
			if (!ar)
				continue;

			ath12k_mac_rfkill_enable_radio(ar, rfkill_radio_on);
		}

		hw = ah->hw;
		wiphy_rfkill_set_hw_state(hw->wiphy, !rfkill_radio_on);
	}
}

void ath12k_core_halt(struct ath12k *ar)
{
	struct ath12k_base *ab = ar->ab;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ar->num_created_vdevs = 0;
	ar->allocated_vdev_map = 0;

	ath12k_mac_scan_finish(ar);
	ath12k_mac_peer_cleanup_all(ar);
	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->regd_update_work);
	cancel_work_sync(&ab->rfkill_work);

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx], NULL);
	synchronize_rcu();
	INIT_LIST_HEAD(&ar->arvifs);
	idr_init(&ar->txmgmt_idr);
}

static void ath12k_core_pre_reconfigure_recovery(struct ath12k_base *ab)
{
	struct ath12k *ar;
	struct ath12k_hw *ah;
	int i, j;

	spin_lock_bh(&ab->base_lock);
	ab->stats.fw_crash_counter++;
	spin_unlock_bh(&ab->base_lock);

	if (ab->is_reset)
		set_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags);

	for (i = 0; i < ath12k_get_num_hw(ab); i++) {
		ah = ath12k_ab_to_ah(ab, i);
		if (!ah || ah->state == ATH12K_HW_STATE_OFF)
			continue;

		ieee80211_stop_queues(ah->hw);

		for (j = 0; j < ah->num_radio; j++) {
			ar = &ah->radio[j];

			ath12k_mac_drain_tx(ar);
			complete(&ar->scan.started);
			complete(&ar->scan.completed);
			complete(&ar->scan.on_channel);
			complete(&ar->peer_assoc_done);
			complete(&ar->peer_delete_done);
			complete(&ar->install_key_done);
			complete(&ar->vdev_setup_done);
			complete(&ar->vdev_delete_done);
			complete(&ar->bss_survey_done);

			wake_up(&ar->dp.tx_empty_waitq);
			idr_for_each(&ar->txmgmt_idr,
				     ath12k_mac_tx_mgmt_pending_free, ar);
			idr_destroy(&ar->txmgmt_idr);
			wake_up(&ar->txmgmt_empty_waitq);
		}
	}

	wake_up(&ab->wmi_ab.tx_credits_wq);
	wake_up(&ab->peer_mapping_wq);
}

static void ath12k_core_post_reconfigure_recovery(struct ath12k_base *ab)
{
	struct ath12k_hw *ah;
	struct ath12k *ar;
	int i, j;

	for (i = 0; i < ath12k_get_num_hw(ab); i++) {
		ah = ath12k_ab_to_ah(ab, i);
		if (!ah || ah->state == ATH12K_HW_STATE_OFF)
			continue;

		wiphy_lock(ah->hw->wiphy);
		mutex_lock(&ah->hw_mutex);

		switch (ah->state) {
		case ATH12K_HW_STATE_ON:
			ah->state = ATH12K_HW_STATE_RESTARTING;

			for (j = 0; j < ah->num_radio; j++) {
				ar = &ah->radio[j];
				ath12k_core_halt(ar);
			}

			break;
		case ATH12K_HW_STATE_OFF:
			ath12k_warn(ab,
				    "cannot restart hw %d that hasn't been started\n",
				    i);
			break;
		case ATH12K_HW_STATE_RESTARTING:
			break;
		case ATH12K_HW_STATE_RESTARTED:
			ah->state = ATH12K_HW_STATE_WEDGED;
			fallthrough;
		case ATH12K_HW_STATE_WEDGED:
			ath12k_warn(ab,
				    "device is wedged, will not restart hw %d\n", i);
			break;
		}

		mutex_unlock(&ah->hw_mutex);
		wiphy_unlock(ah->hw->wiphy);
	}

	complete(&ab->driver_recovery);
}

static void ath12k_core_restart(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, restart_work);
	struct ath12k_hw *ah;
	int ret, i;

	ret = ath12k_core_reconfigure_on_crash(ab);
	if (ret) {
		ath12k_err(ab, "failed to reconfigure driver on crash recovery\n");
		return;
	}

	if (ab->is_reset) {
		for (i = 0; i < ath12k_get_num_hw(ab); i++) {
			ah = ath12k_ab_to_ah(ab, i);
			ieee80211_restart_hw(ah->hw);
		}
	}

	complete(&ab->restart_completed);
}

static void ath12k_core_reset(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, reset_work);
	int reset_count, fail_cont_count;
	long time_left;

	if (!(test_bit(ATH12K_FLAG_QMI_FW_READY_COMPLETE, &ab->dev_flags))) {
		ath12k_warn(ab, "ignore reset dev flags 0x%lx\n", ab->dev_flags);
		return;
	}

	/* Sometimes the recovery will fail and then the next all recovery fail,
	 * this is to avoid infinite recovery since it can not recovery success
	 */
	fail_cont_count = atomic_read(&ab->fail_cont_count);

	if (fail_cont_count >= ATH12K_RESET_MAX_FAIL_COUNT_FINAL)
		return;

	if (fail_cont_count >= ATH12K_RESET_MAX_FAIL_COUNT_FIRST &&
	    time_before(jiffies, ab->reset_fail_timeout))
		return;

	reset_count = atomic_inc_return(&ab->reset_count);

	if (reset_count > 1) {
		/* Sometimes it happened another reset worker before the previous one
		 * completed, then the second reset worker will destroy the previous one,
		 * thus below is to avoid that.
		 */
		ath12k_warn(ab, "already resetting count %d\n", reset_count);

		reinit_completion(&ab->reset_complete);
		time_left = wait_for_completion_timeout(&ab->reset_complete,
							ATH12K_RESET_TIMEOUT_HZ);
		if (time_left) {
			ath12k_dbg(ab, ATH12K_DBG_BOOT, "to skip reset\n");
			atomic_dec(&ab->reset_count);
			return;
		}

		ab->reset_fail_timeout = jiffies + ATH12K_RESET_FAIL_TIMEOUT_HZ;
		/* Record the continuous recovery fail count when recovery failed*/
		fail_cont_count = atomic_inc_return(&ab->fail_cont_count);
	}

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "reset starting\n");

	ab->is_reset = true;
	atomic_set(&ab->recovery_count, 0);

	ath12k_coredump_collect(ab);
	ath12k_core_pre_reconfigure_recovery(ab);

	ath12k_core_post_reconfigure_recovery(ab);

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "waiting recovery start...\n");

	ath12k_hif_irq_disable(ab);
	ath12k_hif_ce_irq_disable(ab);

	ath12k_hif_power_down(ab, false);
	ath12k_hif_power_up(ab);

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "reset started\n");
}

int ath12k_core_pre_init(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_hw_init(ab);
	if (ret) {
		ath12k_err(ab, "failed to init hw params: %d\n", ret);
		return ret;
	}

	ath12k_fw_map(ab);

	return 0;
}

static int ath12k_core_panic_handler(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct ath12k_base *ab = container_of(nb, struct ath12k_base,
					      panic_nb);

	return ath12k_hif_panic_handler(ab);
}

static int ath12k_core_panic_notifier_register(struct ath12k_base *ab)
{
	ab->panic_nb.notifier_call = ath12k_core_panic_handler;

	return atomic_notifier_chain_register(&panic_notifier_list,
					      &ab->panic_nb);
}

static void ath12k_core_panic_notifier_unregister(struct ath12k_base *ab)
{
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &ab->panic_nb);
}

int ath12k_core_init(struct ath12k_base *ab)
{
	int ret;

	ret = ath12k_core_soc_create(ab);
	if (ret) {
		ath12k_err(ab, "failed to create soc core: %d\n", ret);
		return ret;
	}

	ret = ath12k_core_panic_notifier_register(ab);
	if (ret)
		ath12k_warn(ab, "failed to register panic handler: %d\n", ret);

	return 0;
}

void ath12k_core_deinit(struct ath12k_base *ab)
{
	ath12k_core_panic_notifier_unregister(ab);

	mutex_lock(&ab->core_lock);

	ath12k_hif_irq_disable(ab);
	ath12k_core_pdev_destroy(ab);
	ath12k_mac_unregister(ab);
	ath12k_mac_destroy(ab);
	ath12k_core_stop(ab);

	mutex_unlock(&ab->core_lock);

	ath12k_core_soc_destroy(ab);
}

void ath12k_core_free(struct ath12k_base *ab)
{
	timer_delete_sync(&ab->rx_replenish_retry);
	destroy_workqueue(ab->workqueue_aux);
	destroy_workqueue(ab->workqueue);
	kfree(ab);
}

struct ath12k_base *ath12k_core_alloc(struct device *dev, size_t priv_size,
				      enum ath12k_bus bus)
{
	struct ath12k_base *ab;

	ab = kzalloc(sizeof(*ab) + priv_size, GFP_KERNEL);
	if (!ab)
		return NULL;

	init_completion(&ab->driver_recovery);

	ab->workqueue = create_singlethread_workqueue("ath12k_wq");
	if (!ab->workqueue)
		goto err_sc_free;

	ab->workqueue_aux = create_singlethread_workqueue("ath12k_aux_wq");
	if (!ab->workqueue_aux)
		goto err_free_wq;

	mutex_init(&ab->core_lock);
	spin_lock_init(&ab->base_lock);
	init_completion(&ab->reset_complete);

	INIT_LIST_HEAD(&ab->peers);
	init_waitqueue_head(&ab->peer_mapping_wq);
	init_waitqueue_head(&ab->wmi_ab.tx_credits_wq);
	INIT_WORK(&ab->restart_work, ath12k_core_restart);
	INIT_WORK(&ab->reset_work, ath12k_core_reset);
	INIT_WORK(&ab->rfkill_work, ath12k_rfkill_work);
	INIT_WORK(&ab->dump_work, ath12k_coredump_upload);

	timer_setup(&ab->rx_replenish_retry, ath12k_ce_rx_replenish_retry, 0);
	init_completion(&ab->htc_suspend);
	init_completion(&ab->restart_completed);
	init_completion(&ab->wow.wakeup_completed);

	ab->dev = dev;
	ab->hif.bus = bus;
	ab->qmi.num_radios = U8_MAX;
	ab->mlo_capable_flags = ATH12K_INTRA_DEVICE_MLO_SUPPORT;

	/* Device index used to identify the devices in a group.
	 *
	 * In Intra-device MLO, only one device present in a group,
	 * so it is always zero.
	 *
	 * In Inter-device MLO, Multiple device present in a group,
	 * expect non-zero value.
	 */
	ab->device_id = 0;

	return ab;

err_free_wq:
	destroy_workqueue(ab->workqueue);
err_sc_free:
	kfree(ab);
	return NULL;
}

MODULE_DESCRIPTION("Core module for Qualcomm Atheros 802.11be wireless LAN cards.");
MODULE_LICENSE("Dual BSD/GPL");
