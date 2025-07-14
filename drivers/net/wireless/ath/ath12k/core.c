// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/remoteproc.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include "ahb.h"
#include "core.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "debug.h"
#include "debugfs.h"
#include "fw.h"
#include "hif.h"
#include "pci.h"
#include "wow.h"

static int ahb_err, pci_err;
unsigned int ath12k_debug_mask;
module_param_named(debug_mask, ath12k_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");

bool ath12k_ftm_mode;
module_param_named(ftm_mode, ath12k_ftm_mode, bool, 0444);
MODULE_PARM_DESC(ftm_mode, "Boots up in factory test mode");

/* protected with ath12k_hw_group_mutex */
static struct list_head ath12k_hw_group_list = LIST_HEAD_INIT(ath12k_hw_group_list);

static DEFINE_MUTEX(ath12k_hw_group_mutex);

static int ath12k_core_rfkill_config(struct ath12k_base *ab)
{
	struct ath12k *ar;
	int ret = 0, i;

	if (!(ab->target_caps.sys_cap_info & WMI_SYS_CAP_INFO_RFKILL))
		return 0;

	if (ath12k_acpi_get_disable_rfkill(ab))
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
					   bool bus_type_mode, bool with_default)
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
			  with_default ?
			  ATH12K_BOARD_ID_DEFAULT : ab->qmi.target.board_id,
			  variant);
		break;
	}

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot using board name '%s'\n", name);

	return 0;
}

static int ath12k_core_create_board_name(struct ath12k_base *ab, char *name,
					 size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, true, false, false);
}

static int ath12k_core_create_fallback_board_name(struct ath12k_base *ab, char *name,
						  size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, false, false, true);
}

static int ath12k_core_create_bus_type_board_name(struct ath12k_base *ab, char *name,
						  size_t name_len)
{
	return __ath12k_core_create_board_name(ab, name, name_len, false, true, true);
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

struct reserved_mem *ath12k_core_get_reserved_mem(struct ath12k_base *ab,
						  int index)
{
	struct device *dev = ab->dev;
	struct reserved_mem *rmem;
	struct device_node *node;

	node = of_parse_phandle(dev->of_node, "memory-region", index);
	if (!node) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "failed to parse memory-region for index %d\n", index);
		return NULL;
	}

	rmem = of_reserved_mem_lookup(node);
	of_node_put(node);
	if (!rmem) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "unable to get memory-region for index %d\n", index);
		return NULL;
	}

	return rmem;
}

static inline
void ath12k_core_to_group_ref_get(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;

	lockdep_assert_held(&ag->mutex);

	if (ab->hw_group_ref) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "core already attached to group %d\n",
			   ag->id);
		return;
	}

	ab->hw_group_ref = true;
	ag->num_started++;

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "core attached to group %d, num_started %d\n",
		   ag->id, ag->num_started);
}

static inline
void ath12k_core_to_group_ref_put(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;

	lockdep_assert_held(&ag->mutex);

	if (!ab->hw_group_ref) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "core already de-attached from group %d\n",
			   ag->id);
		return;
	}

	ab->hw_group_ref = false;
	ag->num_started--;

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "core de-attached from group %d, num_started %d\n",
		   ag->id, ag->num_started);
}

static void ath12k_core_stop(struct ath12k_base *ab)
{
	ath12k_core_to_group_ref_put(ab);

	if (!test_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath12k_qmi_firmware_stop(ab);

	ath12k_acpi_stop(ab);

	ath12k_dp_rx_pdev_reo_cleanup(ab);
	ath12k_hif_stop(ab);
	ath12k_wmi_detach(ab);
	ath12k_dp_free(ab);

	/* De-Init of components as needed */
}

static void ath12k_core_check_cc_code_bdfext(const struct dmi_header *hdr, void *data)
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

	spin_lock_bh(&ab->base_lock);

	switch (smbios->country_code_flag) {
	case ATH12K_SMBIOS_CC_ISO:
		ab->new_alpha2[0] = u16_get_bits(smbios->cc_code >> 8, 0xff);
		ab->new_alpha2[1] = u16_get_bits(smbios->cc_code, 0xff);
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot smbios cc_code %c%c\n",
			   ab->new_alpha2[0], ab->new_alpha2[1]);
		break;
	case ATH12K_SMBIOS_CC_WW:
		ab->new_alpha2[0] = '0';
		ab->new_alpha2[1] = '0';
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot smbios worldwide regdomain\n");
		break;
	default:
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "boot ignore smbios country code setting %d\n",
			   smbios->country_code_flag);
		break;
	}

	spin_unlock_bh(&ab->base_lock);

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
	dmi_walk(ath12k_core_check_cc_code_bdfext, ab);

	if (ab->qmi.target.bdf_ext[0] == '\0')
		return -ENODATA;

	return 0;
}

static int ath12k_core_soc_create(struct ath12k_base *ab)
{
	int ret;

	if (ath12k_ftm_mode) {
		ab->fw_mode = ATH12K_FIRMWARE_MODE_FTM;
		ath12k_info(ab, "Booting in ftm mode\n");
	}

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

	ath12k_debugfs_pdev_create(ab);

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

static int ath12k_core_start(struct ath12k_base *ab)
{
	int ret;

	lockdep_assert_held(&ab->core_lock);

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

	ath12k_acpi_set_dsm_func(ab);

	/* Indicate the core start in the appropriate group */
	ath12k_core_to_group_ref_get(ab);

	return 0;

err_reo_cleanup:
	ath12k_dp_rx_pdev_reo_cleanup(ab);
err_hif_stop:
	ath12k_hif_stop(ab);
err_wmi_detach:
	ath12k_wmi_detach(ab);
	return ret;
}

static void ath12k_core_device_cleanup(struct ath12k_base *ab)
{
	mutex_lock(&ab->core_lock);

	ath12k_hif_irq_disable(ab);
	ath12k_core_pdev_destroy(ab);

	mutex_unlock(&ab->core_lock);
}

static void ath12k_core_hw_group_stop(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int i;

	lockdep_assert_held(&ag->mutex);

	clear_bit(ATH12K_GROUP_FLAG_REGISTERED, &ag->flags);

	ath12k_mac_unregister(ag);

	for (i = ag->num_devices - 1; i >= 0; i--) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		clear_bit(ATH12K_FLAG_REGISTERED, &ab->dev_flags);

		ath12k_core_device_cleanup(ab);
	}

	ath12k_mac_destroy(ag);
}

u8 ath12k_get_num_partner_link(struct ath12k *ar)
{
	struct ath12k_base *partner_ab, *ab = ar->ab;
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k_pdev *pdev;
	u8 num_link = 0;
	int i, j;

	lockdep_assert_held(&ag->mutex);

	for (i = 0; i < ag->num_devices; i++) {
		partner_ab = ag->ab[i];

		for (j = 0; j < partner_ab->num_radios; j++) {
			pdev = &partner_ab->pdevs[j];

			/* Avoid the self link */
			if (ar == pdev->ar)
				continue;

			num_link++;
		}
	}

	return num_link;
}

static int __ath12k_mac_mlo_ready(struct ath12k *ar)
{
	u8 num_link = ath12k_get_num_partner_link(ar);
	int ret;

	if (num_link == 0)
		return 0;

	ret = ath12k_wmi_mlo_ready(ar);
	if (ret) {
		ath12k_err(ar->ab, "MLO ready failed for pdev %d: %d\n",
			   ar->pdev_idx, ret);
		return ret;
	}

	ath12k_dbg(ar->ab, ATH12K_DBG_MAC, "mlo ready done for pdev %d\n",
		   ar->pdev_idx);

	return 0;
}

int ath12k_mac_mlo_ready(struct ath12k_hw_group *ag)
{
	struct ath12k_hw *ah;
	struct ath12k *ar;
	int ret;
	int i, j;

	for (i = 0; i < ag->num_hw; i++) {
		ah = ag->ah[i];
		if (!ah)
			continue;

		for_each_ar(ah, ar, j) {
			ar = &ah->radio[j];
			ret = __ath12k_mac_mlo_ready(ar);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int ath12k_core_mlo_setup(struct ath12k_hw_group *ag)
{
	int ret, i;

	if (!ag->mlo_capable)
		return 0;

	ret = ath12k_mac_mlo_setup(ag);
	if (ret)
		return ret;

	for (i = 0; i < ag->num_devices; i++)
		ath12k_dp_partner_cc_init(ag->ab[i]);

	ret = ath12k_mac_mlo_ready(ag);
	if (ret)
		goto err_mlo_teardown;

	return 0;

err_mlo_teardown:
	ath12k_mac_mlo_teardown(ag);

	return ret;
}

static int ath12k_core_hw_group_start(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int ret, i;

	lockdep_assert_held(&ag->mutex);

	if (test_bit(ATH12K_GROUP_FLAG_REGISTERED, &ag->flags))
		goto core_pdev_create;

	ret = ath12k_mac_allocate(ag);
	if (WARN_ON(ret))
		return ret;

	ret = ath12k_core_mlo_setup(ag);
	if (WARN_ON(ret))
		goto err_mac_destroy;

	ret = ath12k_mac_register(ag);
	if (WARN_ON(ret))
		goto err_mlo_teardown;

	set_bit(ATH12K_GROUP_FLAG_REGISTERED, &ag->flags);

core_pdev_create:
	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		mutex_lock(&ab->core_lock);

		set_bit(ATH12K_FLAG_REGISTERED, &ab->dev_flags);

		ret = ath12k_core_pdev_create(ab);
		if (ret) {
			ath12k_err(ab, "failed to create pdev core %d\n", ret);
			mutex_unlock(&ab->core_lock);
			goto err;
		}

		ath12k_hif_irq_enable(ab);

		ret = ath12k_core_rfkill_config(ab);
		if (ret && ret != -EOPNOTSUPP) {
			mutex_unlock(&ab->core_lock);
			goto err;
		}

		mutex_unlock(&ab->core_lock);
	}

	return 0;

err:
	ath12k_core_hw_group_stop(ag);
	return ret;

err_mlo_teardown:
	ath12k_mac_mlo_teardown(ag);

err_mac_destroy:
	ath12k_mac_destroy(ag);

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

static inline
bool ath12k_core_hw_group_start_ready(struct ath12k_hw_group *ag)
{
	lockdep_assert_held(&ag->mutex);

	return (ag->num_started == ag->num_devices);
}

static void ath12k_fw_stats_pdevs_free(struct list_head *head)
{
	struct ath12k_fw_stats_pdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

void ath12k_fw_stats_bcn_free(struct list_head *head)
{
	struct ath12k_fw_stats_bcn *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

static void ath12k_fw_stats_vdevs_free(struct list_head *head)
{
	struct ath12k_fw_stats_vdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

void ath12k_fw_stats_init(struct ath12k *ar)
{
	INIT_LIST_HEAD(&ar->fw_stats.vdevs);
	INIT_LIST_HEAD(&ar->fw_stats.pdevs);
	INIT_LIST_HEAD(&ar->fw_stats.bcn);
	init_completion(&ar->fw_stats_complete);
	init_completion(&ar->fw_stats_done);
}

void ath12k_fw_stats_free(struct ath12k_fw_stats *stats)
{
	ath12k_fw_stats_pdevs_free(&stats->pdevs);
	ath12k_fw_stats_vdevs_free(&stats->vdevs);
	ath12k_fw_stats_bcn_free(&stats->bcn);
}

void ath12k_fw_stats_reset(struct ath12k *ar)
{
	spin_lock_bh(&ar->data_lock);
	ath12k_fw_stats_free(&ar->fw_stats);
	ar->fw_stats.num_vdev_recvd = 0;
	ar->fw_stats.num_bcn_recvd = 0;
	spin_unlock_bh(&ar->data_lock);
}

static void ath12k_core_trigger_partner(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k_base *partner_ab;
	bool found = false;
	int i;

	for (i = 0; i < ag->num_devices; i++) {
		partner_ab = ag->ab[i];
		if (!partner_ab)
			continue;

		if (found)
			ath12k_qmi_trigger_host_cap(partner_ab);

		found = (partner_ab == ab);
	}
}

int ath12k_core_qmi_firmware_ready(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ath12k_ab_to_ag(ab);
	int ret, i;

	ret = ath12k_core_start_firmware(ab, ab->fw_mode);
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

	mutex_lock(&ag->mutex);
	mutex_lock(&ab->core_lock);

	ret = ath12k_core_start(ab);
	if (ret) {
		ath12k_err(ab, "failed to start core: %d\n", ret);
		goto err_dp_free;
	}

	mutex_unlock(&ab->core_lock);

	if (ath12k_core_hw_group_start_ready(ag)) {
		ret = ath12k_core_hw_group_start(ag);
		if (ret) {
			ath12k_warn(ab, "unable to start hw group\n");
			goto err_core_stop;
		}
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "group %d started\n", ag->id);
	} else {
		ath12k_core_trigger_partner(ab);
	}

	mutex_unlock(&ag->mutex);

	return 0;

err_core_stop:
	for (i = ag->num_devices - 1; i >= 0; i--) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		mutex_lock(&ab->core_lock);
		ath12k_core_stop(ab);
		mutex_unlock(&ab->core_lock);
	}
	mutex_unlock(&ag->mutex);
	goto exit;

err_dp_free:
	ath12k_dp_free(ab);
	mutex_unlock(&ab->core_lock);
	mutex_unlock(&ag->mutex);

err_firmware_stop:
	ath12k_qmi_firmware_stop(ab);

exit:
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
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k *ar;
	struct ath12k_hw *ah;
	struct ieee80211_hw *hw;
	bool rfkill_radio_on;
	int i, j;

	spin_lock_bh(&ab->base_lock);
	rfkill_radio_on = ab->rfkill_radio_on;
	spin_unlock_bh(&ab->base_lock);

	for (i = 0; i < ag->num_hw; i++) {
		ah = ath12k_ag_to_ah(ag, i);
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
	struct list_head *pos, *n;
	struct ath12k_base *ab = ar->ab;

	lockdep_assert_wiphy(ath12k_ar_to_hw(ar)->wiphy);

	ar->num_created_vdevs = 0;
	ar->allocated_vdev_map = 0;

	ath12k_mac_scan_finish(ar);
	ath12k_mac_peer_cleanup_all(ar);
	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->regd_update_work);
	cancel_work_sync(&ab->rfkill_work);
	cancel_work_sync(&ab->update_11d_work);

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx], NULL);
	synchronize_rcu();

	spin_lock_bh(&ar->data_lock);
	list_for_each_safe(pos, n, &ar->arvifs)
		list_del_init(pos);
	spin_unlock_bh(&ar->data_lock);

	idr_init(&ar->txmgmt_idr);
}

static void ath12k_core_pre_reconfigure_recovery(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k *ar;
	struct ath12k_hw *ah;
	int i, j;

	spin_lock_bh(&ab->base_lock);
	ab->stats.fw_crash_counter++;
	spin_unlock_bh(&ab->base_lock);

	if (ab->is_reset)
		set_bit(ATH12K_FLAG_CRASH_FLUSH, &ab->dev_flags);

	for (i = 0; i < ag->num_hw; i++) {
		ah = ath12k_ag_to_ah(ag, i);
		if (!ah || ah->state == ATH12K_HW_STATE_OFF ||
		    ah->state == ATH12K_HW_STATE_TM)
			continue;

		wiphy_lock(ah->hw->wiphy);

		/* If queue 0 is stopped, it is safe to assume that all
		 * other queues are stopped by driver via
		 * ieee80211_stop_queues() below. This means, there is
		 * no need to stop it again and hence continue
		 */
		if (ieee80211_queue_stopped(ah->hw, 0)) {
			wiphy_unlock(ah->hw->wiphy);
			continue;
		}

		ieee80211_stop_queues(ah->hw);

		for (j = 0; j < ah->num_radio; j++) {
			ar = &ah->radio[j];

			ath12k_mac_drain_tx(ar);
			ar->state_11d = ATH12K_11D_IDLE;
			complete(&ar->completed_11d_scan);
			complete(&ar->scan.started);
			complete_all(&ar->scan.completed);
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

			ar->monitor_vdev_id = -1;
			ar->monitor_vdev_created = false;
			ar->monitor_started = false;
		}

		wiphy_unlock(ah->hw->wiphy);
	}

	wake_up(&ab->wmi_ab.tx_credits_wq);
	wake_up(&ab->peer_mapping_wq);
}

static void ath12k_update_11d(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, update_11d_work);
	struct ath12k *ar;
	struct ath12k_pdev *pdev;
	struct wmi_set_current_country_arg arg = {};
	int ret, i;

	spin_lock_bh(&ab->base_lock);
	memcpy(&arg.alpha2, &ab->new_alpha2, 2);
	spin_unlock_bh(&ab->base_lock);

	ath12k_dbg(ab, ATH12K_DBG_WMI, "update 11d new cc %c%c\n",
		   arg.alpha2[0], arg.alpha2[1]);

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;

		memcpy(&ar->alpha2, &arg.alpha2, 2);
		ret = ath12k_wmi_send_set_current_country_cmd(ar, &arg);
		if (ret)
			ath12k_warn(ar->ab,
				    "pdev id %d failed set current country code: %d\n",
				    i, ret);
	}
}

static void ath12k_core_post_reconfigure_recovery(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k_hw *ah;
	struct ath12k *ar;
	int i, j;

	for (i = 0; i < ag->num_hw; i++) {
		ah = ath12k_ag_to_ah(ag, i);
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
		case ATH12K_HW_STATE_TM:
			ath12k_warn(ab, "fw mode reset done radio %d\n", i);
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
	struct ath12k_hw_group *ag = ab->ag;
	struct ath12k_hw *ah;
	int ret, i;

	ret = ath12k_core_reconfigure_on_crash(ab);
	if (ret) {
		ath12k_err(ab, "failed to reconfigure driver on crash recovery\n");
		return;
	}

	if (ab->is_reset) {
		if (!test_bit(ATH12K_FLAG_REGISTERED, &ab->dev_flags)) {
			atomic_dec(&ab->reset_count);
			complete(&ab->reset_complete);
			ab->is_reset = false;
			atomic_set(&ab->fail_cont_count, 0);
			ath12k_dbg(ab, ATH12K_DBG_BOOT, "reset success\n");
		}

		mutex_lock(&ag->mutex);

		if (!ath12k_core_hw_group_start_ready(ag)) {
			mutex_unlock(&ag->mutex);
			goto exit_restart;
		}

		for (i = 0; i < ag->num_hw; i++) {
			ah = ath12k_ag_to_ah(ag, i);
			ieee80211_restart_hw(ah->hw);
		}

		mutex_unlock(&ag->mutex);
	}

exit_restart:
	complete(&ab->restart_completed);
}

static void ath12k_core_reset(struct work_struct *work)
{
	struct ath12k_base *ab = container_of(work, struct ath12k_base, reset_work);
	struct ath12k_hw_group *ag = ab->ag;
	int reset_count, fail_cont_count, i;
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

	/* prepare for power up */
	ab->qmi.num_radios = U8_MAX;

	mutex_lock(&ag->mutex);
	ath12k_core_to_group_ref_put(ab);

	if (ag->num_started > 0) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT,
			   "waiting for %d partner device(s) to reset\n",
			   ag->num_started);
		mutex_unlock(&ag->mutex);
		return;
	}

	/* Prepare MLO global memory region for power up */
	ath12k_qmi_reset_mlo_mem(ag);

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		ath12k_hif_power_up(ab);
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "reset started\n");
	}

	mutex_unlock(&ag->mutex);
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

static inline
bool ath12k_core_hw_group_create_ready(struct ath12k_hw_group *ag)
{
	lockdep_assert_held(&ag->mutex);

	return (ag->num_probed == ag->num_devices);
}

static struct ath12k_hw_group *ath12k_core_hw_group_alloc(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag;
	int count = 0;

	lockdep_assert_held(&ath12k_hw_group_mutex);

	list_for_each_entry(ag, &ath12k_hw_group_list, list)
		count++;

	ag = kzalloc(sizeof(*ag), GFP_KERNEL);
	if (!ag)
		return NULL;

	ag->id = count;
	list_add(&ag->list, &ath12k_hw_group_list);
	mutex_init(&ag->mutex);
	ag->mlo_capable = false;

	return ag;
}

static void ath12k_core_hw_group_free(struct ath12k_hw_group *ag)
{
	mutex_lock(&ath12k_hw_group_mutex);

	list_del(&ag->list);
	kfree(ag);

	mutex_unlock(&ath12k_hw_group_mutex);
}

static struct ath12k_hw_group *ath12k_core_hw_group_find_by_dt(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag;
	int i;

	if (!ab->dev->of_node)
		return NULL;

	list_for_each_entry(ag, &ath12k_hw_group_list, list)
		for (i = 0; i < ag->num_devices; i++)
			if (ag->wsi_node[i] == ab->dev->of_node)
				return ag;

	return NULL;
}

static int ath12k_core_get_wsi_info(struct ath12k_hw_group *ag,
				    struct ath12k_base *ab)
{
	struct device_node *wsi_dev = ab->dev->of_node, *next_wsi_dev;
	struct device_node *tx_endpoint, *next_rx_endpoint;
	int device_count = 0;

	next_wsi_dev = wsi_dev;

	if (!next_wsi_dev)
		return -ENODEV;

	do {
		ag->wsi_node[device_count] = next_wsi_dev;

		tx_endpoint = of_graph_get_endpoint_by_regs(next_wsi_dev, 0, -1);
		if (!tx_endpoint) {
			of_node_put(next_wsi_dev);
			return -ENODEV;
		}

		next_rx_endpoint = of_graph_get_remote_endpoint(tx_endpoint);
		if (!next_rx_endpoint) {
			of_node_put(next_wsi_dev);
			of_node_put(tx_endpoint);
			return -ENODEV;
		}

		of_node_put(tx_endpoint);
		of_node_put(next_wsi_dev);

		next_wsi_dev = of_graph_get_port_parent(next_rx_endpoint);
		if (!next_wsi_dev) {
			of_node_put(next_rx_endpoint);
			return -ENODEV;
		}

		of_node_put(next_rx_endpoint);

		device_count++;
		if (device_count > ATH12K_MAX_DEVICES) {
			ath12k_warn(ab, "device count in DT %d is more than limit %d\n",
				    device_count, ATH12K_MAX_DEVICES);
			of_node_put(next_wsi_dev);
			return -EINVAL;
		}
	} while (wsi_dev != next_wsi_dev);

	of_node_put(next_wsi_dev);
	ag->num_devices = device_count;

	return 0;
}

static int ath12k_core_get_wsi_index(struct ath12k_hw_group *ag,
				     struct ath12k_base *ab)
{
	int i, wsi_controller_index = -1, node_index = -1;
	bool control;

	for (i = 0; i < ag->num_devices; i++) {
		control = of_property_read_bool(ag->wsi_node[i], "qcom,wsi-controller");
		if (control)
			wsi_controller_index = i;

		if (ag->wsi_node[i] == ab->dev->of_node)
			node_index = i;
	}

	if (wsi_controller_index == -1) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "wsi controller is not defined in dt");
		return -EINVAL;
	}

	if (node_index == -1) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "unable to get WSI node index");
		return -EINVAL;
	}

	ab->wsi_info.index = (ag->num_devices + node_index - wsi_controller_index) %
		ag->num_devices;

	return 0;
}

static struct ath12k_hw_group *ath12k_core_hw_group_assign(struct ath12k_base *ab)
{
	struct ath12k_wsi_info *wsi = &ab->wsi_info;
	struct ath12k_hw_group *ag;

	lockdep_assert_held(&ath12k_hw_group_mutex);

	if (ath12k_ftm_mode)
		goto invalid_group;

	/* The grouping of multiple devices will be done based on device tree file.
	 * The platforms that do not have any valid group information would have
	 * each device to be part of its own invalid group.
	 *
	 * We use group id ATH12K_INVALID_GROUP_ID for single device group
	 * which didn't have dt entry or wrong dt entry, there could be many
	 * groups with same group id, i.e ATH12K_INVALID_GROUP_ID. So
	 * default group id of ATH12K_INVALID_GROUP_ID combined with
	 * num devices in ath12k_hw_group determines if the group is
	 * multi device or single device group
	 */

	ag = ath12k_core_hw_group_find_by_dt(ab);
	if (!ag) {
		ag = ath12k_core_hw_group_alloc(ab);
		if (!ag) {
			ath12k_warn(ab, "unable to create new hw group\n");
			return NULL;
		}

		if (ath12k_core_get_wsi_info(ag, ab) ||
		    ath12k_core_get_wsi_index(ag, ab)) {
			ath12k_dbg(ab, ATH12K_DBG_BOOT,
				   "unable to get wsi info from dt, grouping single device");
			ag->id = ATH12K_INVALID_GROUP_ID;
			ag->num_devices = 1;
			memset(ag->wsi_node, 0, sizeof(ag->wsi_node));
			wsi->index = 0;
		}

		goto exit;
	} else if (test_bit(ATH12K_GROUP_FLAG_UNREGISTER, &ag->flags)) {
		ath12k_dbg(ab, ATH12K_DBG_BOOT, "group id %d in unregister state\n",
			   ag->id);
		goto invalid_group;
	} else {
		if (ath12k_core_get_wsi_index(ag, ab))
			goto invalid_group;
		goto exit;
	}

invalid_group:
	ag = ath12k_core_hw_group_alloc(ab);
	if (!ag) {
		ath12k_warn(ab, "unable to create new hw group\n");
		return NULL;
	}

	ag->id = ATH12K_INVALID_GROUP_ID;
	ag->num_devices = 1;
	wsi->index = 0;

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "single device added to hardware group\n");

exit:
	if (ag->num_probed >= ag->num_devices) {
		ath12k_warn(ab, "unable to add new device to group, max limit reached\n");
		goto invalid_group;
	}

	ab->device_id = ag->num_probed++;
	ag->ab[ab->device_id] = ab;
	ab->ag = ag;

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "wsi group-id %d num-devices %d index %d",
		   ag->id, ag->num_devices, wsi->index);

	return ag;
}

void ath12k_core_hw_group_unassign(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag = ath12k_ab_to_ag(ab);
	u8 device_id = ab->device_id;
	int num_probed;

	if (!ag)
		return;

	mutex_lock(&ag->mutex);

	if (WARN_ON(device_id >= ag->num_devices)) {
		mutex_unlock(&ag->mutex);
		return;
	}

	if (WARN_ON(ag->ab[device_id] != ab)) {
		mutex_unlock(&ag->mutex);
		return;
	}

	ag->ab[device_id] = NULL;
	ab->ag = NULL;
	ab->device_id = ATH12K_INVALID_DEVICE_ID;

	if (ag->num_probed)
		ag->num_probed--;

	num_probed = ag->num_probed;

	mutex_unlock(&ag->mutex);

	if (!num_probed)
		ath12k_core_hw_group_free(ag);
}

static void ath12k_core_hw_group_destroy(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int i;

	if (WARN_ON(!ag))
		return;

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		ath12k_core_soc_destroy(ab);
	}
}

void ath12k_core_hw_group_cleanup(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int i;

	if (!ag)
		return;

	mutex_lock(&ag->mutex);

	if (test_bit(ATH12K_GROUP_FLAG_UNREGISTER, &ag->flags)) {
		mutex_unlock(&ag->mutex);
		return;
	}

	set_bit(ATH12K_GROUP_FLAG_UNREGISTER, &ag->flags);

	ath12k_core_hw_group_stop(ag);

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		mutex_lock(&ab->core_lock);
		ath12k_core_stop(ab);
		mutex_unlock(&ab->core_lock);
	}

	mutex_unlock(&ag->mutex);
}

static int ath12k_core_hw_group_create(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int i, ret;

	lockdep_assert_held(&ag->mutex);

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		mutex_lock(&ab->core_lock);

		ret = ath12k_core_soc_create(ab);
		if (ret) {
			mutex_unlock(&ab->core_lock);
			ath12k_err(ab, "failed to create soc core: %d\n", ret);
			return ret;
		}

		mutex_unlock(&ab->core_lock);
	}

	return 0;
}

void ath12k_core_hw_group_set_mlo_capable(struct ath12k_hw_group *ag)
{
	struct ath12k_base *ab;
	int i;

	if (ath12k_ftm_mode)
		return;

	lockdep_assert_held(&ag->mutex);

	if (ag->num_devices == 1) {
		ab = ag->ab[0];
		/* QCN9274 firmware uses firmware IE for MLO advertisement */
		if (ab->fw.fw_features_valid) {
			ag->mlo_capable =
				ath12k_fw_feature_supported(ab, ATH12K_FW_FEATURE_MLO);
			return;
		}

		/* while WCN7850 firmware uses QMI single_chip_mlo_support bit */
		ag->mlo_capable = ab->single_chip_mlo_support;
		return;
	}

	ag->mlo_capable = true;

	for (i = 0; i < ag->num_devices; i++) {
		ab = ag->ab[i];
		if (!ab)
			continue;

		/* even if 1 device's firmware feature indicates MLO
		 * unsupported, make MLO unsupported for the whole group
		 */
		if (!ath12k_fw_feature_supported(ab, ATH12K_FW_FEATURE_MLO)) {
			ag->mlo_capable = false;
			return;
		}
	}
}

int ath12k_core_init(struct ath12k_base *ab)
{
	struct ath12k_hw_group *ag;
	int ret;

	ret = ath12k_core_panic_notifier_register(ab);
	if (ret)
		ath12k_warn(ab, "failed to register panic handler: %d\n", ret);

	mutex_lock(&ath12k_hw_group_mutex);

	ag = ath12k_core_hw_group_assign(ab);
	if (!ag) {
		mutex_unlock(&ath12k_hw_group_mutex);
		ath12k_warn(ab, "unable to get hw group\n");
		ret = -ENODEV;
		goto err_unregister_notifier;
	}

	mutex_unlock(&ath12k_hw_group_mutex);

	mutex_lock(&ag->mutex);

	ath12k_dbg(ab, ATH12K_DBG_BOOT, "num devices %d num probed %d\n",
		   ag->num_devices, ag->num_probed);

	if (ath12k_core_hw_group_create_ready(ag)) {
		ret = ath12k_core_hw_group_create(ag);
		if (ret) {
			mutex_unlock(&ag->mutex);
			ath12k_warn(ab, "unable to create hw group\n");
			goto err_destroy_hw_group;
		}
	}

	mutex_unlock(&ag->mutex);

	return 0;

err_destroy_hw_group:
	ath12k_core_hw_group_destroy(ab->ag);
	ath12k_core_hw_group_unassign(ab);
err_unregister_notifier:
	ath12k_core_panic_notifier_unregister(ab);

	return ret;
}

void ath12k_core_deinit(struct ath12k_base *ab)
{
	ath12k_core_hw_group_destroy(ab->ag);
	ath12k_core_hw_group_unassign(ab);
	ath12k_core_panic_notifier_unregister(ab);
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
	INIT_WORK(&ab->update_11d_work, ath12k_update_11d);

	timer_setup(&ab->rx_replenish_retry, ath12k_ce_rx_replenish_retry, 0);
	init_completion(&ab->htc_suspend);
	init_completion(&ab->restart_completed);
	init_completion(&ab->wow.wakeup_completed);

	ab->dev = dev;
	ab->hif.bus = bus;
	ab->qmi.num_radios = U8_MAX;
	ab->single_chip_mlo_support = false;

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

static int ath12k_init(void)
{
	ahb_err = ath12k_ahb_init();
	if (ahb_err)
		pr_warn("Failed to initialize ath12k AHB device: %d\n", ahb_err);

	pci_err = ath12k_pci_init();
	if (pci_err)
		pr_warn("Failed to initialize ath12k PCI device: %d\n", pci_err);

	/* If both failed, return one of the failures (arbitrary) */
	return ahb_err && pci_err ? ahb_err : 0;
}

static void ath12k_exit(void)
{
	if (!pci_err)
		ath12k_pci_exit();

	if (!ahb_err)
		ath12k_ahb_exit();
}

module_init(ath12k_init);
module_exit(ath12k_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11be WLAN devices");
MODULE_LICENSE("Dual BSD/GPL");
