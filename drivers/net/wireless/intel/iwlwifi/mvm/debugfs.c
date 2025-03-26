// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2012-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/ieee80211.h>
#include <linux/netdevice.h>
#include <linux/dmi.h>

#include "mvm.h"
#include "sta.h"
#include "iwl-io.h"
#include "debugfs.h"
#include "iwl-modparams.h"
#include "iwl-drv.h"
#include "iwl-utils.h"
#include "fw/error-dump.h"
#include "fw/api/phy-ctxt.h"

static ssize_t iwl_dbgfs_ctdp_budget_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos, budget;

	if (!iwl_mvm_is_ctdp_supported(mvm))
		return -EOPNOTSUPP;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	mutex_lock(&mvm->mutex);
	budget = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_REPORT, 0);
	mutex_unlock(&mvm->mutex);

	if (budget < 0)
		return budget;

	pos = scnprintf(buf, sizeof(buf), "%d\n", budget);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_stop_ctdp_write(struct iwl_mvm *mvm, char *buf,
					 size_t count, loff_t *ppos)
{
	int ret;
	bool force;

	if (!kstrtobool(buf, &force))
		IWL_DEBUG_INFO(mvm,
			       "force start is %d [0=disabled, 1=enabled]\n",
			       force);

	/* we allow skipping cap support check and force stop ctdp
	 * statistics collection and with guerantee that it is
	 * safe to use.
	 */
	if (!force && !iwl_mvm_is_ctdp_supported(mvm))
		return -EOPNOTSUPP;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_STOP, 0);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_start_ctdp_write(struct iwl_mvm *mvm,
					  char *buf, size_t count,
					  loff_t *ppos)
{
	int ret;
	bool force;

	if (!kstrtobool(buf, &force))
		IWL_DEBUG_INFO(mvm,
			       "force start is %d [0=disabled, 1=enabled]\n",
			       force);

	/* we allow skipping cap support check and force enable ctdp
	 * for statistics collection and with guerantee that it is
	 * safe to use.
	 */
	if (!force && !iwl_mvm_is_ctdp_supported(mvm))
		return -EOPNOTSUPP;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_START, 0);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_force_ctkill_write(struct iwl_mvm *mvm, char *buf,
					    size_t count, loff_t *ppos)
{
	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	iwl_mvm_enter_ctkill(mvm);

	return count;
}

static ssize_t iwl_dbgfs_tx_flush_write(struct iwl_mvm *mvm, char *buf,
					size_t count, loff_t *ppos)
{
	int ret;
	u32 flush_arg;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	if (kstrtou32(buf, 0, &flush_arg))
		return -EINVAL;

	if (iwl_mvm_has_new_tx_api(mvm)) {
		IWL_DEBUG_TX_QUEUES(mvm,
				    "FLUSHING all tids queues on sta_id = %d\n",
				    flush_arg);
		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_flush_sta_tids(mvm, flush_arg, 0xFFFF)
			? : count;
		mutex_unlock(&mvm->mutex);
		return ret;
	}

	IWL_DEBUG_TX_QUEUES(mvm, "FLUSHING queues mask to flush = 0x%x\n",
			    flush_arg);

	mutex_lock(&mvm->mutex);
	ret =  iwl_mvm_flush_tx_path(mvm, flush_arg) ? : count;
	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sram_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	const struct fw_img *img;
	unsigned int ofs, len;
	size_t ret;
	u8 *ptr;

	if (!iwl_mvm_firmware_running(mvm))
		return -EINVAL;

	/* default is to dump the entire data segment */
	img = &mvm->fw->img[mvm->fwrt.cur_fw_img];
	ofs = img->sec[IWL_UCODE_SECTION_DATA].offset;
	len = img->sec[IWL_UCODE_SECTION_DATA].len;

	if (mvm->dbgfs_sram_len) {
		ofs = mvm->dbgfs_sram_offset;
		len = mvm->dbgfs_sram_len;
	}

	ptr = kzalloc(len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	iwl_trans_read_mem_bytes(mvm->trans, ofs, ptr, len);

	ret = simple_read_from_buffer(user_buf, count, ppos, ptr, len);

	kfree(ptr);

	return ret;
}

static ssize_t iwl_dbgfs_sram_write(struct iwl_mvm *mvm, char *buf,
				    size_t count, loff_t *ppos)
{
	const struct fw_img *img;
	u32 offset, len;
	u32 img_offset, img_len;

	if (!iwl_mvm_firmware_running(mvm))
		return -EINVAL;

	img = &mvm->fw->img[mvm->fwrt.cur_fw_img];
	img_offset = img->sec[IWL_UCODE_SECTION_DATA].offset;
	img_len = img->sec[IWL_UCODE_SECTION_DATA].len;

	if (sscanf(buf, "%x,%x", &offset, &len) == 2) {
		if ((offset & 0x3) || (len & 0x3))
			return -EINVAL;

		if (offset + len > img_offset + img_len)
			return -EINVAL;

		mvm->dbgfs_sram_offset = offset;
		mvm->dbgfs_sram_len = len;
	} else {
		mvm->dbgfs_sram_offset = 0;
		mvm->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t iwl_dbgfs_set_nic_temperature_read(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos;

	if (!mvm->temperature_test)
		pos = scnprintf(buf, sizeof(buf), "disabled\n");
	else
		pos = scnprintf(buf, sizeof(buf), "%d\n", mvm->temperature);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

/*
 * Set NIC Temperature
 * Cause the driver to ignore the actual NIC temperature reported by the FW
 * Enable: any value between IWL_MVM_DEBUG_SET_TEMPERATURE_MIN -
 * IWL_MVM_DEBUG_SET_TEMPERATURE_MAX
 * Disable: IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE
 */
static ssize_t iwl_dbgfs_set_nic_temperature_write(struct iwl_mvm *mvm,
						   char *buf, size_t count,
						   loff_t *ppos)
{
	int temperature;

	if (!iwl_mvm_firmware_running(mvm) && !mvm->temperature_test)
		return -EIO;

	if (kstrtoint(buf, 10, &temperature))
		return -EINVAL;
	/* not a legal temperature */
	if ((temperature > IWL_MVM_DEBUG_SET_TEMPERATURE_MAX &&
	     temperature != IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE) ||
	    temperature < IWL_MVM_DEBUG_SET_TEMPERATURE_MIN)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	if (temperature == IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE) {
		if (!mvm->temperature_test)
			goto out;

		mvm->temperature_test = false;
		/* Since we can't read the temp while awake, just set
		 * it to zero until we get the next RX stats from the
		 * firmware.
		 */
		mvm->temperature = 0;
	} else {
		mvm->temperature_test = true;
		mvm->temperature = temperature;
	}
	IWL_DEBUG_TEMP(mvm, "%sabling debug set temperature (temp = %d)\n",
		       mvm->temperature_test ? "En" : "Dis",
		       mvm->temperature);
	/* handle the temperature change */
	iwl_mvm_tt_handler(mvm);

out:
	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_nic_temp_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos, ret;
	s32 temp;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_get_temp(mvm, &temp);
	mutex_unlock(&mvm->mutex);

	if (ret)
		return -EIO;

	pos = scnprintf(buf, sizeof(buf), "%d\n", temp);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#ifdef CONFIG_ACPI
static ssize_t iwl_dbgfs_sar_geo_profile_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[256];
	int pos = 0;
	int bufsz = sizeof(buf);
	int tbl_idx;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	tbl_idx = iwl_mvm_get_sar_geo_profile(mvm);
	if (tbl_idx < 0) {
		mutex_unlock(&mvm->mutex);
		return tbl_idx;
	}

	if (!tbl_idx) {
		pos = scnprintf(buf, bufsz,
				"SAR geographic profile disabled\n");
	} else {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "Use geographic profile %d\n", tbl_idx);
		pos += scnprintf(buf + pos, bufsz - pos,
				 "2.4GHz:\n\tChain A offset: %u dBm\n\tChain B offset: %u dBm\n\tmax tx power: %u dBm\n",
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[0].chains[0],
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[0].chains[1],
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[0].max);
		pos += scnprintf(buf + pos, bufsz - pos,
				 "5.2GHz:\n\tChain A offset: %u dBm\n\tChain B offset: %u dBm\n\tmax tx power: %u dBm\n",
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[1].chains[0],
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[1].chains[1],
				 mvm->fwrt.geo_profiles[tbl_idx - 1].bands[1].max);
	}
	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_wifi_6e_enable_read(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int err, pos;
	char buf[12];
	u32 value;

	err = iwl_bios_get_dsm(&mvm->fwrt, DSM_FUNC_ENABLE_6E, &value);
	if (err)
		return err;

	pos = sprintf(buf, "0x%08x\n", value);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}
#endif

static ssize_t iwl_dbgfs_stations_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct ieee80211_sta *sta;
	char buf[400];
	int i, pos = 0, bufsz = sizeof(buf);

	mutex_lock(&mvm->mutex);

	for (i = 0; i < mvm->fw->ucode_capa.num_stations; i++) {
		pos += scnprintf(buf + pos, bufsz - pos, "%.2d: ", i);
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (!sta)
			pos += scnprintf(buf + pos, bufsz - pos, "N/A\n");
		else if (IS_ERR(sta))
			pos += scnprintf(buf + pos, bufsz - pos, "%ld\n",
					 PTR_ERR(sta));
		else
			pos += scnprintf(buf + pos, bufsz - pos, "%pM\n",
					 sta->addr);
	}

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_rs_data_read(struct ieee80211_link_sta *link_sta,
				      struct iwl_mvm_sta *mvmsta,
				      struct iwl_mvm *mvm,
				      struct iwl_mvm_link_sta *mvm_link_sta,
				      char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct iwl_lq_sta_rs_fw *lq_sta = &mvm_link_sta->lq_sta.rs_fw;
	static const size_t bufsz = 2048;
	char *buff;
	int desc = 0;
	ssize_t ret;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	desc += scnprintf(buff + desc, bufsz - desc, "sta_id %d\n",
			  lq_sta->pers.sta_id);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "fixed rate 0x%X\n",
			  lq_sta->pers.dbg_fixed_rate);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "A-MPDU size limit %d\n",
			  lq_sta->pers.dbg_agg_frame_count_lim);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "valid_tx_ant %s%s\n",
		(iwl_mvm_get_valid_tx_ant(mvm) & ANT_A) ? "ANT_A," : "",
		(iwl_mvm_get_valid_tx_ant(mvm) & ANT_B) ? "ANT_B," : "");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "last tx rate=0x%X ",
			  lq_sta->last_rate_n_flags);

	desc += rs_pretty_print_rate(buff + desc, bufsz - desc,
				     lq_sta->last_rate_n_flags);
	if (desc < bufsz - 1)
		buff[desc++] = '\n';

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static ssize_t iwl_dbgfs_amsdu_len_write(struct ieee80211_link_sta *link_sta,
					 struct iwl_mvm_sta *mvmsta,
					 struct iwl_mvm *mvm,
					 struct iwl_mvm_link_sta *mvm_link_sta,
					 char *buf, size_t count,
					 loff_t *ppos)
{
	int i;
	u16 amsdu_len;

	if (kstrtou16(buf, 0, &amsdu_len))
		return -EINVAL;

	/* only change from debug set <-> debug unset */
	if (amsdu_len && mvm_link_sta->orig_amsdu_len)
		return -EBUSY;

	if (amsdu_len) {
		mvm_link_sta->orig_amsdu_len = link_sta->agg.max_amsdu_len;
		link_sta->agg.max_amsdu_len = amsdu_len;
		for (i = 0; i < ARRAY_SIZE(link_sta->agg.max_tid_amsdu_len); i++)
			link_sta->agg.max_tid_amsdu_len[i] = amsdu_len;
	} else {
		link_sta->agg.max_amsdu_len = mvm_link_sta->orig_amsdu_len;
		mvm_link_sta->orig_amsdu_len = 0;
	}

	ieee80211_sta_recalc_aggregates(link_sta->sta);

	return count;
}

static ssize_t iwl_dbgfs_amsdu_len_read(struct ieee80211_link_sta *link_sta,
					struct iwl_mvm_sta *mvmsta,
					struct iwl_mvm *mvm,
					struct iwl_mvm_link_sta *mvm_link_sta,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	char buf[32];
	int pos;

	pos = scnprintf(buf, sizeof(buf), "current %d ",
			link_sta->agg.max_amsdu_len);
	pos += scnprintf(buf + pos, sizeof(buf) - pos, "stored %d\n",
			 mvm_link_sta->orig_amsdu_len);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_disable_power_off_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[64];
	int bufsz = sizeof(buf);
	int pos = 0;

	pos += scnprintf(buf+pos, bufsz-pos, "disable_power_off_d0=%d\n",
			 mvm->disable_power_off);
	pos += scnprintf(buf+pos, bufsz-pos, "disable_power_off_d3=%d\n",
			 mvm->disable_power_off_d3);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_disable_power_off_write(struct iwl_mvm *mvm, char *buf,
						 size_t count, loff_t *ppos)
{
	int ret, val;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (!strncmp("disable_power_off_d0=", buf, 21)) {
		if (sscanf(buf + 21, "%d", &val) != 1)
			return -EINVAL;
		mvm->disable_power_off = val;
	} else if (!strncmp("disable_power_off_d3=", buf, 21)) {
		if (sscanf(buf + 21, "%d", &val) != 1)
			return -EINVAL;
		mvm->disable_power_off_d3 = val;
	} else {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_power_update_device(mvm);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_tas_get_status_read(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_mvm_tas_status_resp *rsp = NULL;
	static const size_t bufsz = 1024;
	char *buff, *pos, *endpos;
	const char * const tas_dis_reason[TAS_DISABLED_REASON_MAX] = {
		[TAS_DISABLED_DUE_TO_BIOS] =
			"Due To BIOS",
		[TAS_DISABLED_DUE_TO_SAR_6DBM] =
			"Due To SAR Limit Less Than 6 dBm",
		[TAS_DISABLED_REASON_INVALID] =
			"N/A",
	};
	const char * const tas_current_status[TAS_DYNA_STATUS_MAX] = {
		[TAS_DYNA_INACTIVE] = "INACTIVE",
		[TAS_DYNA_INACTIVE_MVM_MODE] =
			"inactive due to mvm mode",
		[TAS_DYNA_INACTIVE_TRIGGER_MODE] =
			"inactive due to trigger mode",
		[TAS_DYNA_INACTIVE_BLOCK_LISTED] =
			"inactive due to block listed",
		[TAS_DYNA_INACTIVE_UHB_NON_US] =
			"inactive due to uhb non US",
		[TAS_DYNA_ACTIVE] = "ACTIVE",
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, GET_TAS_STATUS),
		.flags = CMD_WANT_SKB,
		.len = { 0, },
		.data = { NULL, },
	};
	int ret, i, tmp;
	bool tas_enabled = false;
	unsigned long dyn_status;

	if (!iwl_mvm_firmware_running(mvm))
		return -ENODEV;

	if (iwl_fw_lookup_notif_ver(mvm->fw, DEBUG_GROUP, GET_TAS_STATUS,
				    0) != 3)
		return -EOPNOTSUPP;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	mutex_unlock(&mvm->mutex);
	if (ret < 0)
		return ret;

	buff = kzalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;
	pos = buff;
	endpos = pos + bufsz;

	rsp = (void *)hcmd.resp_pkt->data;

	pos += scnprintf(pos, endpos - pos, "TAS Conclusion:\n");
	for (i = 0; i < rsp->in_dual_radio + 1; i++) {
		if (rsp->tas_status_mac[i].band != TAS_LMAC_BAND_INVALID &&
		    rsp->tas_status_mac[i].dynamic_status & BIT(TAS_DYNA_ACTIVE)) {
			pos += scnprintf(pos, endpos - pos, "\tON for ");
			switch (rsp->tas_status_mac[i].band) {
			case TAS_LMAC_BAND_HB:
				pos += scnprintf(pos, endpos - pos, "HB\n");
				break;
			case TAS_LMAC_BAND_LB:
				pos += scnprintf(pos, endpos - pos, "LB\n");
				break;
			case TAS_LMAC_BAND_UHB:
				pos += scnprintf(pos, endpos - pos, "UHB\n");
				break;
			case TAS_LMAC_BAND_INVALID:
				pos += scnprintf(pos, endpos - pos,
						 "INVALID BAND\n");
				break;
			default:
				pos += scnprintf(pos, endpos - pos,
						 "Unsupported band (%d)\n",
						 rsp->tas_status_mac[i].band);
				goto out;
			}
			tas_enabled = true;
		}
	}
	if (!tas_enabled)
		pos += scnprintf(pos, endpos - pos, "\tOFF\n");

	pos += scnprintf(pos, endpos - pos, "TAS Report\n");
	pos += scnprintf(pos, endpos - pos, "TAS FW version: %d\n",
			 rsp->tas_fw_version);
	pos += scnprintf(pos, endpos - pos, "Is UHB enabled for USA?: %s\n",
			 rsp->is_uhb_for_usa_enable ? "True" : "False");

	if (fw_has_capa(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_UHB_CANADA_TAS_SUPPORT))
		pos += scnprintf(pos, endpos - pos,
				 "Is UHB enabled for CANADA?: %s\n",
				 rsp->uhb_allowed_flags &
				 TAS_UHB_ALLOWED_CANADA ? "True" : "False");

	pos += scnprintf(pos, endpos - pos, "Current MCC: 0x%x\n",
			 le16_to_cpu(rsp->curr_mcc));

	pos += scnprintf(pos, endpos - pos, "Block list entries:");
	for (i = 0; i < IWL_WTAS_BLACK_LIST_MAX; i++)
		pos += scnprintf(pos, endpos - pos, " 0x%x",
				 le16_to_cpu(rsp->block_list[i]));

	pos += scnprintf(pos, endpos - pos, "\nOEM name: %s\n",
			 dmi_get_system_info(DMI_SYS_VENDOR) ?: "<unknown>");
	pos += scnprintf(pos, endpos - pos, "\tVendor In Approved List: %s\n",
			 iwl_is_tas_approved() ? "YES" : "NO");
	pos += scnprintf(pos, endpos - pos,
			 "\tDo TAS Support Dual Radio?: %s\n",
			 rsp->in_dual_radio ? "TRUE" : "FALSE");

	for (i = 0; i < rsp->in_dual_radio + 1; i++) {
		if (rsp->tas_status_mac[i].static_status == 0) {
			pos += scnprintf(pos, endpos - pos,
					 "Static status: disabled\n");
			pos += scnprintf(pos, endpos - pos,
					 "Static disabled reason: %s (0)\n",
					 tas_dis_reason[0]);
			goto out;
		}

		pos += scnprintf(pos, endpos - pos, "TAS status for ");
		switch (rsp->tas_status_mac[i].band) {
		case TAS_LMAC_BAND_HB:
			pos += scnprintf(pos, endpos - pos, "High band\n");
			break;
		case TAS_LMAC_BAND_LB:
			pos += scnprintf(pos, endpos - pos, "Low band\n");
			break;
		case TAS_LMAC_BAND_UHB:
			pos += scnprintf(pos, endpos - pos,
					 "Ultra high band\n");
			break;
		case TAS_LMAC_BAND_INVALID:
			pos += scnprintf(pos, endpos - pos,
					 "INVALID band\n");
			break;
		default:
			pos += scnprintf(pos, endpos - pos,
					 "Unsupported band (%d)\n",
					 rsp->tas_status_mac[i].band);
			goto out;
		}
		pos += scnprintf(pos, endpos - pos, "Static status: %sabled\n",
				 rsp->tas_status_mac[i].static_status ?
				 "En" : "Dis");
		pos += scnprintf(pos, endpos - pos,
				 "\tStatic Disabled Reason: ");
		if (rsp->tas_status_mac[i].static_dis_reason < TAS_DISABLED_REASON_MAX)
			pos += scnprintf(pos, endpos - pos, "%s (%d)\n",
					 tas_dis_reason[rsp->tas_status_mac[i].static_dis_reason],
					 rsp->tas_status_mac[i].static_dis_reason);
		else
			pos += scnprintf(pos, endpos - pos,
					 "unsupported value (%d)\n",
					 rsp->tas_status_mac[i].static_dis_reason);

		pos += scnprintf(pos, endpos - pos, "Dynamic status:\n");
		dyn_status = (rsp->tas_status_mac[i].dynamic_status);
		for_each_set_bit(tmp, &dyn_status, sizeof(dyn_status)) {
			if (tmp >= 0 && tmp < TAS_DYNA_STATUS_MAX)
				pos += scnprintf(pos, endpos - pos,
						 "\t%s (%d)\n",
						 tas_current_status[tmp], tmp);
		}

		pos += scnprintf(pos, endpos - pos,
				 "Is near disconnection?: %s\n",
				 rsp->tas_status_mac[i].near_disconnection ?
				 "True" : "False");
		tmp = le16_to_cpu(rsp->tas_status_mac[i].max_reg_pwr_limit);
		pos += scnprintf(pos, endpos - pos,
				 "Max. regulatory pwr limit (dBm): %d.%03d\n",
				 tmp / 8, 125 * (tmp % 8));
		tmp = le16_to_cpu(rsp->tas_status_mac[i].sar_limit);
		pos += scnprintf(pos, endpos - pos,
				 "SAR limit (dBm): %d.%03d\n",
				 tmp / 8, 125 * (tmp % 8));
	}

out:
	ret = simple_read_from_buffer(user_buf, count, ppos, buff, pos - buff);
	kfree(buff);
	iwl_free_resp(&hcmd);
	return ret;
}

static ssize_t iwl_dbgfs_phy_integration_ver_read(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char *buf;
	size_t bufsz;
	int pos;
	ssize_t ret;

	bufsz = mvm->fw->phy_integration_ver_len + 2;
	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos = scnprintf(buf, bufsz, "%.*s\n", mvm->fw->phy_integration_ver_len,
			mvm->fw->phy_integration_ver);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);

	kfree(buf);
	return ret;
}

#define PRINT_STATS_LE32(_struct, _memb)				\
			 pos += scnprintf(buf + pos, bufsz - pos,	\
					  fmt_table, #_memb,		\
					  le32_to_cpu(_struct->_memb))

static ssize_t iwl_dbgfs_fw_rx_stats_read(struct file *file,
					  char __user *user_buf, size_t count,
					  loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	static const char *fmt_table = "\t%-30s %10u\n";
	static const char *fmt_header = "%-32s\n";
	int pos = 0;
	char *buf;
	int ret;
	size_t bufsz;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					   WIDE_ID(SYSTEM_GROUP,
						   SYSTEM_STATISTICS_CMD),
					   IWL_FW_CMD_VER_UNKNOWN);

	if (cmd_ver != IWL_FW_CMD_VER_UNKNOWN)
		return -EOPNOTSUPP;

	if (iwl_mvm_has_new_rx_stats_api(mvm))
		bufsz = ((sizeof(struct mvm_statistics_rx) /
			  sizeof(__le32)) * 43) + (4 * 33) + 1;
	else
		/* 43 = size of each data line; 33 = size of each header */
		bufsz = ((sizeof(struct mvm_statistics_rx_v3) /
			  sizeof(__le32)) * 43) + (4 * 33) + 1;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	if (iwl_mvm_firmware_running(mvm))
		iwl_mvm_request_statistics(mvm, false);

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - OFDM");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_phy_v2 *ofdm = &mvm->rx_stats_v3.ofdm;

		PRINT_STATS_LE32(ofdm, ina_cnt);
		PRINT_STATS_LE32(ofdm, fina_cnt);
		PRINT_STATS_LE32(ofdm, plcp_err);
		PRINT_STATS_LE32(ofdm, crc32_err);
		PRINT_STATS_LE32(ofdm, overrun_err);
		PRINT_STATS_LE32(ofdm, early_overrun_err);
		PRINT_STATS_LE32(ofdm, crc32_good);
		PRINT_STATS_LE32(ofdm, false_alarm_cnt);
		PRINT_STATS_LE32(ofdm, fina_sync_err_cnt);
		PRINT_STATS_LE32(ofdm, sfd_timeout);
		PRINT_STATS_LE32(ofdm, fina_timeout);
		PRINT_STATS_LE32(ofdm, unresponded_rts);
		PRINT_STATS_LE32(ofdm, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(ofdm, sent_ack_cnt);
		PRINT_STATS_LE32(ofdm, sent_cts_cnt);
		PRINT_STATS_LE32(ofdm, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(ofdm, dsp_self_kill);
		PRINT_STATS_LE32(ofdm, mh_format_err);
		PRINT_STATS_LE32(ofdm, re_acq_main_rssi_sum);
		PRINT_STATS_LE32(ofdm, reserved);
	} else {
		struct mvm_statistics_rx_phy *ofdm = &mvm->rx_stats.ofdm;

		PRINT_STATS_LE32(ofdm, unresponded_rts);
		PRINT_STATS_LE32(ofdm, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(ofdm, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(ofdm, dsp_self_kill);
		PRINT_STATS_LE32(ofdm, reserved);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - CCK");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_phy_v2 *cck = &mvm->rx_stats_v3.cck;

		PRINT_STATS_LE32(cck, ina_cnt);
		PRINT_STATS_LE32(cck, fina_cnt);
		PRINT_STATS_LE32(cck, plcp_err);
		PRINT_STATS_LE32(cck, crc32_err);
		PRINT_STATS_LE32(cck, overrun_err);
		PRINT_STATS_LE32(cck, early_overrun_err);
		PRINT_STATS_LE32(cck, crc32_good);
		PRINT_STATS_LE32(cck, false_alarm_cnt);
		PRINT_STATS_LE32(cck, fina_sync_err_cnt);
		PRINT_STATS_LE32(cck, sfd_timeout);
		PRINT_STATS_LE32(cck, fina_timeout);
		PRINT_STATS_LE32(cck, unresponded_rts);
		PRINT_STATS_LE32(cck, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(cck, sent_ack_cnt);
		PRINT_STATS_LE32(cck, sent_cts_cnt);
		PRINT_STATS_LE32(cck, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(cck, dsp_self_kill);
		PRINT_STATS_LE32(cck, mh_format_err);
		PRINT_STATS_LE32(cck, re_acq_main_rssi_sum);
		PRINT_STATS_LE32(cck, reserved);
	} else {
		struct mvm_statistics_rx_phy *cck = &mvm->rx_stats.cck;

		PRINT_STATS_LE32(cck, unresponded_rts);
		PRINT_STATS_LE32(cck, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(cck, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(cck, dsp_self_kill);
		PRINT_STATS_LE32(cck, reserved);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - GENERAL");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_non_phy_v3 *general =
			&mvm->rx_stats_v3.general;

		PRINT_STATS_LE32(general, bogus_cts);
		PRINT_STATS_LE32(general, bogus_ack);
		PRINT_STATS_LE32(general, non_bssid_frames);
		PRINT_STATS_LE32(general, filtered_frames);
		PRINT_STATS_LE32(general, non_channel_beacons);
		PRINT_STATS_LE32(general, channel_beacons);
		PRINT_STATS_LE32(general, num_missed_bcon);
		PRINT_STATS_LE32(general, adc_rx_saturation_time);
		PRINT_STATS_LE32(general, ina_detection_search_time);
		PRINT_STATS_LE32(general, beacon_silence_rssi_a);
		PRINT_STATS_LE32(general, beacon_silence_rssi_b);
		PRINT_STATS_LE32(general, beacon_silence_rssi_c);
		PRINT_STATS_LE32(general, interference_data_flag);
		PRINT_STATS_LE32(general, channel_load);
		PRINT_STATS_LE32(general, dsp_false_alarms);
		PRINT_STATS_LE32(general, beacon_rssi_a);
		PRINT_STATS_LE32(general, beacon_rssi_b);
		PRINT_STATS_LE32(general, beacon_rssi_c);
		PRINT_STATS_LE32(general, beacon_energy_a);
		PRINT_STATS_LE32(general, beacon_energy_b);
		PRINT_STATS_LE32(general, beacon_energy_c);
		PRINT_STATS_LE32(general, num_bt_kills);
		PRINT_STATS_LE32(general, mac_id);
		PRINT_STATS_LE32(general, directed_data_mpdu);
	} else {
		struct mvm_statistics_rx_non_phy *general =
			&mvm->rx_stats.general;

		PRINT_STATS_LE32(general, bogus_cts);
		PRINT_STATS_LE32(general, bogus_ack);
		PRINT_STATS_LE32(general, non_channel_beacons);
		PRINT_STATS_LE32(general, channel_beacons);
		PRINT_STATS_LE32(general, num_missed_bcon);
		PRINT_STATS_LE32(general, adc_rx_saturation_time);
		PRINT_STATS_LE32(general, ina_detection_search_time);
		PRINT_STATS_LE32(general, beacon_silence_rssi_a);
		PRINT_STATS_LE32(general, beacon_silence_rssi_b);
		PRINT_STATS_LE32(general, beacon_silence_rssi_c);
		PRINT_STATS_LE32(general, interference_data_flag);
		PRINT_STATS_LE32(general, channel_load);
		PRINT_STATS_LE32(general, beacon_rssi_a);
		PRINT_STATS_LE32(general, beacon_rssi_b);
		PRINT_STATS_LE32(general, beacon_rssi_c);
		PRINT_STATS_LE32(general, beacon_energy_a);
		PRINT_STATS_LE32(general, beacon_energy_b);
		PRINT_STATS_LE32(general, beacon_energy_c);
		PRINT_STATS_LE32(general, num_bt_kills);
		PRINT_STATS_LE32(general, mac_id);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - HT");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_ht_phy_v1 *ht =
			&mvm->rx_stats_v3.ofdm_ht;

		PRINT_STATS_LE32(ht, plcp_err);
		PRINT_STATS_LE32(ht, overrun_err);
		PRINT_STATS_LE32(ht, early_overrun_err);
		PRINT_STATS_LE32(ht, crc32_good);
		PRINT_STATS_LE32(ht, crc32_err);
		PRINT_STATS_LE32(ht, mh_format_err);
		PRINT_STATS_LE32(ht, agg_crc32_good);
		PRINT_STATS_LE32(ht, agg_mpdu_cnt);
		PRINT_STATS_LE32(ht, agg_cnt);
		PRINT_STATS_LE32(ht, unsupport_mcs);
	} else {
		struct mvm_statistics_rx_ht_phy *ht =
			&mvm->rx_stats.ofdm_ht;

		PRINT_STATS_LE32(ht, mh_format_err);
		PRINT_STATS_LE32(ht, agg_mpdu_cnt);
		PRINT_STATS_LE32(ht, agg_cnt);
		PRINT_STATS_LE32(ht, unsupport_mcs);
	}

	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);

	return ret;
}
#undef PRINT_STAT_LE32

static ssize_t iwl_dbgfs_fw_system_stats_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	char *buff, *pos, *endpos;
	int ret;
	size_t bufsz;
	int i;
	struct iwl_mvm_vif *mvmvif;
	struct ieee80211_vif *vif;
	struct iwl_mvm *mvm = file->private_data;
	u8 cmd_ver = iwl_fw_lookup_cmd_ver(mvm->fw,
					   WIDE_ID(SYSTEM_GROUP,
						   SYSTEM_STATISTICS_CMD),
					   IWL_FW_CMD_VER_UNKNOWN);

	/* in case of a wrong cmd version, allocate buffer only for error msg */
	bufsz = (cmd_ver == 1) ? 4096 : 64;

	buff = kzalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	pos = buff;
	endpos = pos + bufsz;

	if (cmd_ver != 1) {
		pos += scnprintf(pos, endpos - pos,
				 "System stats not supported:%d\n", cmd_ver);
		goto send_out;
	}

	mutex_lock(&mvm->mutex);
	if (iwl_mvm_firmware_running(mvm))
		iwl_mvm_request_statistics(mvm, false);

	for (i = 0; i < NUM_MAC_INDEX_DRIVER; i++) {
		vif = iwl_mvm_rcu_dereference_vif_id(mvm, i, false);
		if (!vif)
			continue;

		if (vif->type == NL80211_IFTYPE_STATION)
			break;
	}

	if (i == NUM_MAC_INDEX_DRIVER || !vif) {
		pos += scnprintf(pos, endpos - pos, "vif is NULL\n");
		goto release_send_out;
	}

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	if (!mvmvif) {
		pos += scnprintf(pos, endpos - pos, "mvmvif is NULL\n");
		goto release_send_out;
	}

	for_each_mvm_vif_valid_link(mvmvif, i) {
		struct iwl_mvm_vif_link_info *link_info = mvmvif->link[i];

		pos += scnprintf(pos, endpos - pos,
				 "link_id %d", i);
		pos += scnprintf(pos, endpos - pos,
				 " num_beacons %d",
				 link_info->beacon_stats.num_beacons);
		pos += scnprintf(pos, endpos - pos,
				 " accu_num_beacons %d",
				 link_info->beacon_stats.accu_num_beacons);
		pos += scnprintf(pos, endpos - pos,
				 " avg_signal %d\n",
				 link_info->beacon_stats.avg_signal);
	}

	pos += scnprintf(pos, endpos - pos,
			 "radio_stats.rx_time %lld\n",
			 mvm->radio_stats.rx_time);
	pos += scnprintf(pos, endpos - pos,
			 "radio_stats.tx_time %lld\n",
			 mvm->radio_stats.tx_time);
	pos += scnprintf(pos, endpos - pos,
			 "accu_radio_stats.rx_time %lld\n",
			 mvm->accu_radio_stats.rx_time);
	pos += scnprintf(pos, endpos - pos,
			 "accu_radio_stats.tx_time %lld\n",
			 mvm->accu_radio_stats.tx_time);

release_send_out:
	mutex_unlock(&mvm->mutex);

send_out:
	ret = simple_read_from_buffer(user_buf, count, ppos, buff, pos - buff);
	kfree(buff);

	return ret;
}

static ssize_t iwl_dbgfs_frame_stats_read(struct iwl_mvm *mvm,
					  char __user *user_buf, size_t count,
					  loff_t *ppos,
					  struct iwl_mvm_frame_stats *stats)
{
	char *buff, *pos, *endpos;
	int idx, i;
	int ret;
	static const size_t bufsz = 1024;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	spin_lock_bh(&mvm->drv_stats_lock);

	pos = buff;
	endpos = pos + bufsz;

	pos += scnprintf(pos, endpos - pos,
			 "Legacy/HT/VHT\t:\t%d/%d/%d\n",
			 stats->legacy_frames,
			 stats->ht_frames,
			 stats->vht_frames);
	pos += scnprintf(pos, endpos - pos, "20/40/80\t:\t%d/%d/%d\n",
			 stats->bw_20_frames,
			 stats->bw_40_frames,
			 stats->bw_80_frames);
	pos += scnprintf(pos, endpos - pos, "NGI/SGI\t\t:\t%d/%d\n",
			 stats->ngi_frames,
			 stats->sgi_frames);
	pos += scnprintf(pos, endpos - pos, "SISO/MIMO2\t:\t%d/%d\n",
			 stats->siso_frames,
			 stats->mimo2_frames);
	pos += scnprintf(pos, endpos - pos, "FAIL/SCSS\t:\t%d/%d\n",
			 stats->fail_frames,
			 stats->success_frames);
	pos += scnprintf(pos, endpos - pos, "MPDUs agg\t:\t%d\n",
			 stats->agg_frames);
	pos += scnprintf(pos, endpos - pos, "A-MPDUs\t\t:\t%d\n",
			 stats->ampdu_count);
	pos += scnprintf(pos, endpos - pos, "Avg MPDUs/A-MPDU:\t%d\n",
			 stats->ampdu_count > 0 ?
			 (stats->agg_frames / stats->ampdu_count) : 0);

	pos += scnprintf(pos, endpos - pos, "Last Rates\n");

	idx = stats->last_frame_idx - 1;
	for (i = 0; i < ARRAY_SIZE(stats->last_rates); i++) {
		idx = (idx + 1) % ARRAY_SIZE(stats->last_rates);
		if (stats->last_rates[idx] == 0)
			continue;
		pos += scnprintf(pos, endpos - pos, "Rate[%d]: ",
				 (int)(ARRAY_SIZE(stats->last_rates) - i));
		pos += rs_pretty_print_rate_v1(pos, endpos - pos,
					       stats->last_rates[idx]);
		if (pos < endpos - 1)
			*pos++ = '\n';
	}
	spin_unlock_bh(&mvm->drv_stats_lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, pos - buff);
	kfree(buff);

	return ret;
}

static ssize_t iwl_dbgfs_drv_rx_stats_read(struct file *file,
					   char __user *user_buf, size_t count,
					   loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;

	return iwl_dbgfs_frame_stats_read(mvm, user_buf, count, ppos,
					  &mvm->drv_rx_stats);
}

static ssize_t iwl_dbgfs_fw_restart_write(struct iwl_mvm *mvm, char *buf,
					  size_t count, loff_t *ppos)
{
	int __maybe_unused ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);

	if (count == 6 && !strcmp(buf, "nolog\n")) {
		set_bit(IWL_MVM_STATUS_SUPPRESS_ERROR_LOG_ONCE, &mvm->status);
		set_bit(STATUS_SUPPRESS_CMD_ERROR_ONCE, &mvm->trans->status);
	}

	/* take the return value to make compiler happy - it will fail anyway */
	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(LONG_GROUP, REPLY_ERROR),
				   0, 0, NULL);

	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_fw_nmi_write(struct iwl_mvm *mvm, char *buf,
				      size_t count, loff_t *ppos)
{
	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	IWL_ERR(mvm, "Triggering an NMI from debugfs\n");

	if (count == 6 && !strcmp(buf, "nolog\n"))
		set_bit(IWL_MVM_STATUS_SUPPRESS_ERROR_LOG_ONCE, &mvm->status);

	iwl_force_nmi(mvm->trans);

	return count;
}

static ssize_t
iwl_dbgfs_scan_ant_rxchain_read(struct file *file,
				char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int pos = 0;
	char buf[32];
	const size_t bufsz = sizeof(buf);

	/* print which antennas were set for the scan command by the user */
	pos += scnprintf(buf + pos, bufsz - pos, "Antennas for scan: ");
	if (mvm->scan_rx_ant & ANT_A)
		pos += scnprintf(buf + pos, bufsz - pos, "A");
	if (mvm->scan_rx_ant & ANT_B)
		pos += scnprintf(buf + pos, bufsz - pos, "B");
	pos += scnprintf(buf + pos, bufsz - pos, " (%x)\n", mvm->scan_rx_ant);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_scan_ant_rxchain_write(struct iwl_mvm *mvm, char *buf,
				 size_t count, loff_t *ppos)
{
	u8 scan_rx_ant;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (sscanf(buf, "%hhx", &scan_rx_ant) != 1)
		return -EINVAL;
	if (scan_rx_ant > ANT_ABC)
		return -EINVAL;
	if (scan_rx_ant & ~(iwl_mvm_get_valid_rx_ant(mvm)))
		return -EINVAL;

	if (mvm->scan_rx_ant != scan_rx_ant) {
		mvm->scan_rx_ant = scan_rx_ant;
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_UMAC_SCAN))
			iwl_mvm_config_scan(mvm);
	}

	return count;
}

static ssize_t iwl_dbgfs_indirection_tbl_write(struct iwl_mvm *mvm,
					       char *buf, size_t count,
					       loff_t *ppos)
{
	struct iwl_rss_config_cmd cmd = {
		.flags = cpu_to_le32(IWL_RSS_ENABLE),
		.hash_mask = IWL_RSS_HASH_TYPE_IPV4_TCP |
			     IWL_RSS_HASH_TYPE_IPV4_UDP |
			     IWL_RSS_HASH_TYPE_IPV4_PAYLOAD |
			     IWL_RSS_HASH_TYPE_IPV6_TCP |
			     IWL_RSS_HASH_TYPE_IPV6_UDP |
			     IWL_RSS_HASH_TYPE_IPV6_PAYLOAD,
	};
	int ret, i, num_repeats, nbytes = count / 2;

	ret = hex2bin(cmd.indirection_table, buf, nbytes);
	if (ret)
		return ret;

	/*
	 * The input is the redirection table, partial or full.
	 * Repeat the pattern if needed.
	 * For example, input of 01020F will be repeated 42 times,
	 * indirecting RSS hash results to queues 1, 2, 15 (skipping
	 * queues 3 - 14).
	 */
	num_repeats = ARRAY_SIZE(cmd.indirection_table) / nbytes;
	for (i = 1; i < num_repeats; i++)
		memcpy(&cmd.indirection_table[i * nbytes],
		       cmd.indirection_table, nbytes);
	/* handle cut in the middle pattern for the last places */
	memcpy(&cmd.indirection_table[i * nbytes], cmd.indirection_table,
	       ARRAY_SIZE(cmd.indirection_table) % nbytes);

	netdev_rss_key_fill(cmd.secret_key, sizeof(cmd.secret_key));

	mutex_lock(&mvm->mutex);
	if (iwl_mvm_firmware_running(mvm))
		ret = iwl_mvm_send_cmd_pdu(mvm, RSS_CONFIG_CMD, 0,
					   sizeof(cmd), &cmd);
	else
		ret = 0;
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_inject_packet_write(struct iwl_mvm *mvm,
					     char *buf, size_t count,
					     loff_t *ppos)
{
	struct iwl_op_mode *opmode = container_of((void *)mvm,
						  struct iwl_op_mode,
						  op_mode_specific);
	struct iwl_rx_cmd_buffer rxb = {
		._rx_page_order = 0,
		.truesize = 0, /* not used */
		._offset = 0,
	};
	struct iwl_rx_packet *pkt;
	int bin_len = count / 2;
	int ret = -EINVAL;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	/* supporting only MQ RX */
	if (!mvm->trans->trans_cfg->mq_rx_supported)
		return -EOPNOTSUPP;

	rxb._page = alloc_pages(GFP_ATOMIC, 0);
	if (!rxb._page)
		return -ENOMEM;
	pkt = rxb_addr(&rxb);

	ret = hex2bin(page_address(rxb._page), buf, bin_len);
	if (ret)
		goto out;

	/* avoid invalid memory access and malformed packet */
	if (bin_len < sizeof(*pkt) ||
	    bin_len != sizeof(*pkt) + iwl_rx_packet_payload_len(pkt))
		goto out;

	local_bh_disable();
	iwl_mvm_rx_mq(opmode, NULL, &rxb);
	local_bh_enable();
	ret = 0;

out:
	iwl_free_rxb(&rxb);

	return ret ?: count;
}

static int _iwl_dbgfs_inject_beacon_ie(struct iwl_mvm *mvm, char *bin, int len)
{
	struct ieee80211_vif *vif;
	struct iwl_mvm_vif *mvmvif;
	struct sk_buff *beacon;
	struct ieee80211_tx_info *info;
	struct iwl_mac_beacon_cmd beacon_cmd = {};
	unsigned int link_id;
	u8 rate;
	int i;

	len /= 2;

	/* Element len should be represented by u8 */
	if (len >= U8_MAX)
		return -EINVAL;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (!iwl_mvm_has_new_tx_api(mvm) &&
	    !fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_NEW_BEACON_TEMPLATE))
		return -EINVAL;

	mutex_lock(&mvm->mutex);

	for (i = 0; i < NUM_MAC_INDEX_DRIVER; i++) {
		vif = iwl_mvm_rcu_dereference_vif_id(mvm, i, false);
		if (!vif)
			continue;

		if (vif->type == NL80211_IFTYPE_AP)
			break;
	}

	if (i == NUM_MAC_INDEX_DRIVER || !vif)
		goto out_err;

	mvm->hw->extra_beacon_tailroom = len;

	beacon = ieee80211_beacon_get_template(mvm->hw, vif, NULL, 0);
	if (!beacon)
		goto out_err;

	if (len && hex2bin(skb_put_zero(beacon, len), bin, len)) {
		dev_kfree_skb(beacon);
		goto out_err;
	}

	mvm->beacon_inject_active = true;

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	info = IEEE80211_SKB_CB(beacon);
	rate = iwl_mvm_mac_ctxt_get_beacon_rate(mvm, info, vif);

	for_each_mvm_vif_valid_link(mvmvif, link_id) {
		beacon_cmd.flags =
			cpu_to_le16(iwl_mvm_mac_ctxt_get_beacon_flags(mvm->fw,
								      rate));
		beacon_cmd.byte_cnt = cpu_to_le16((u16)beacon->len);
		if (iwl_fw_lookup_cmd_ver(mvm->fw, BEACON_TEMPLATE_CMD, 0) > 12)
			beacon_cmd.link_id =
				cpu_to_le32(mvmvif->link[link_id]->fw_link_id);
		else
			beacon_cmd.link_id = cpu_to_le32((u32)mvmvif->id);

		iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd.tim_idx,
					 &beacon_cmd.tim_size,
					 beacon->data, beacon->len);

		if (iwl_fw_lookup_cmd_ver(mvm->fw,
					  BEACON_TEMPLATE_CMD, 0) >= 14) {
			u32 offset = iwl_find_ie_offset(beacon->data,
							WLAN_EID_S1G_TWT,
							beacon->len);

			beacon_cmd.btwt_offset = cpu_to_le32(offset);
		}

		iwl_mvm_mac_ctxt_send_beacon_cmd(mvm, beacon, &beacon_cmd,
						 sizeof(beacon_cmd));
	}
	mutex_unlock(&mvm->mutex);

	dev_kfree_skb(beacon);

	return 0;

out_err:
	mutex_unlock(&mvm->mutex);
	return -EINVAL;
}

static ssize_t iwl_dbgfs_inject_beacon_ie_write(struct iwl_mvm *mvm,
						char *buf, size_t count,
						loff_t *ppos)
{
	int ret = _iwl_dbgfs_inject_beacon_ie(mvm, buf, count);

	mvm->hw->extra_beacon_tailroom = 0;
	return ret ?: count;
}

static ssize_t iwl_dbgfs_inject_beacon_ie_restore_write(struct iwl_mvm *mvm,
							char *buf,
							size_t count,
							loff_t *ppos)
{
	int ret = _iwl_dbgfs_inject_beacon_ie(mvm, NULL, 0);

	mvm->hw->extra_beacon_tailroom = 0;
	mvm->beacon_inject_active = false;
	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_conf_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int conf;
	char buf[8];
	const size_t bufsz = sizeof(buf);
	int pos = 0;

	mutex_lock(&mvm->mutex);
	conf = mvm->fwrt.dump.conf;
	mutex_unlock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "%d\n", conf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_fw_dbg_conf_write(struct iwl_mvm *mvm,
					   char *buf, size_t count,
					   loff_t *ppos)
{
	unsigned int conf_id;
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	ret = kstrtouint(buf, 0, &conf_id);
	if (ret)
		return ret;

	if (WARN_ON(conf_id >= FW_DBG_CONF_MAX))
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	ret = iwl_fw_start_dbg_conf(&mvm->fwrt, conf_id);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_clear_write(struct iwl_mvm *mvm,
					    char *buf, size_t count,
					    loff_t *ppos)
{
	if (mvm->trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_9000)
		return -EOPNOTSUPP;

	/*
	 * If the firmware is not running, silently succeed since there is
	 * no data to clear.
	 */
	if (!iwl_mvm_firmware_running(mvm))
		return count;

	mutex_lock(&mvm->mutex);
	iwl_fw_dbg_clear_monitor_buf(&mvm->fwrt);
	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_dbg_time_point_write(struct iwl_mvm *mvm,
					      char *buf, size_t count,
					      loff_t *ppos)
{
	u32 timepoint;

	if (kstrtou32(buf, 0, &timepoint))
		return -EINVAL;

	if (timepoint == IWL_FW_INI_TIME_POINT_INVALID ||
	    timepoint >= IWL_FW_INI_TIME_POINT_NUM)
		return -EINVAL;

	iwl_dbg_tlv_time_point(&mvm->fwrt, timepoint, NULL);

	return count;
}

#define MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_ADD_FILE_ALIAS(alias, name, parent, mode) do {	\
		debugfs_create_file(alias, mode, parent, mvm,		\
				    &iwl_dbgfs_##name##_ops);		\
	} while (0)
#define MVM_DEBUGFS_ADD_FILE(name, parent, mode) \
	MVM_DEBUGFS_ADD_FILE_ALIAS(#name, name, parent, mode)

static ssize_t
_iwl_dbgfs_link_sta_wrap_write(ssize_t (*real)(struct ieee80211_link_sta *,
					       struct iwl_mvm_sta *,
					       struct iwl_mvm *,
					       struct iwl_mvm_link_sta *,
					       char *,
					       size_t, loff_t *),
			   struct file *file,
			   char *buf, size_t buf_size, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(link_sta->sta);
	struct iwl_mvm *mvm = iwl_mvm_vif_from_mac80211(mvmsta->vif)->mvm;
	struct iwl_mvm_link_sta *mvm_link_sta;
	ssize_t ret;

	mutex_lock(&mvm->mutex);

	mvm_link_sta = rcu_dereference_protected(mvmsta->link[link_sta->link_id],
						 lockdep_is_held(&mvm->mutex));
	if (WARN_ON(!mvm_link_sta)) {
		mutex_unlock(&mvm->mutex);
		return -ENODEV;
	}

	ret = real(link_sta, mvmsta, mvm, mvm_link_sta, buf, buf_size, ppos);

	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t
_iwl_dbgfs_link_sta_wrap_read(ssize_t (*real)(struct ieee80211_link_sta *,
					      struct iwl_mvm_sta *,
					      struct iwl_mvm *,
					      struct iwl_mvm_link_sta *,
					      char __user *,
					      size_t, loff_t *),
			   struct file *file,
			   char __user *user_buf, size_t count, loff_t *ppos)
{
	struct ieee80211_link_sta *link_sta = file->private_data;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(link_sta->sta);
	struct iwl_mvm *mvm = iwl_mvm_vif_from_mac80211(mvmsta->vif)->mvm;
	struct iwl_mvm_link_sta *mvm_link_sta;
	ssize_t ret;

	mutex_lock(&mvm->mutex);

	mvm_link_sta = rcu_dereference_protected(mvmsta->link[link_sta->link_id],
						 lockdep_is_held(&mvm->mutex));
	if (WARN_ON(!mvm_link_sta)) {
		mutex_unlock(&mvm->mutex);
		return -ENODEV;
	}

	ret = real(link_sta, mvmsta, mvm, mvm_link_sta, user_buf, count, ppos);

	mutex_unlock(&mvm->mutex);

	return ret;
}

#define MVM_DEBUGFS_LINK_STA_WRITE_WRAPPER(name, buflen)		\
static ssize_t _iwl_dbgfs_link_sta_##name##_write(struct file *file,	\
					 const char __user *user_buf,	\
					 size_t count, loff_t *ppos)	\
{									\
	char buf[buflen] = {};						\
	size_t buf_size = min(count, sizeof(buf) -  1);			\
									\
	if (copy_from_user(buf, user_buf, buf_size))			\
		return -EFAULT;						\
									\
	return _iwl_dbgfs_link_sta_wrap_write(iwl_dbgfs_##name##_write,	\
					      file,			\
					      buf, buf_size, ppos);	\
}									\

#define MVM_DEBUGFS_LINK_STA_READ_WRAPPER(name)		\
static ssize_t _iwl_dbgfs_link_sta_##name##_read(struct file *file,	\
					 char __user *user_buf,		\
					 size_t count, loff_t *ppos)	\
{									\
	return _iwl_dbgfs_link_sta_wrap_read(iwl_dbgfs_##name##_read,	\
					     file,			\
					     user_buf, count, ppos);	\
}									\

#define MVM_DEBUGFS_WRITE_LINK_STA_FILE_OPS(name, bufsz)		\
MVM_DEBUGFS_LINK_STA_WRITE_WRAPPER(name, bufsz)				\
static const struct file_operations iwl_dbgfs_link_sta_##name##_ops = {	\
	.write = _iwl_dbgfs_link_sta_##name##_write,			\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define MVM_DEBUGFS_READ_LINK_STA_FILE_OPS(name)			\
MVM_DEBUGFS_LINK_STA_READ_WRAPPER(name)					\
static const struct file_operations iwl_dbgfs_link_sta_##name##_ops = {	\
	.read = _iwl_dbgfs_link_sta_##name##_read,			\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define MVM_DEBUGFS_READ_WRITE_LINK_STA_FILE_OPS(name, bufsz)		\
MVM_DEBUGFS_LINK_STA_READ_WRAPPER(name)					\
MVM_DEBUGFS_LINK_STA_WRITE_WRAPPER(name, bufsz)				\
static const struct file_operations iwl_dbgfs_link_sta_##name##_ops = {	\
	.read = _iwl_dbgfs_link_sta_##name##_read,			\
	.write = _iwl_dbgfs_link_sta_##name##_write,			\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define MVM_DEBUGFS_ADD_LINK_STA_FILE_ALIAS(alias, name, parent, mode)	\
		debugfs_create_file(alias, mode, parent, link_sta,	\
				    &iwl_dbgfs_link_sta_##name##_ops)
#define MVM_DEBUGFS_ADD_LINK_STA_FILE(name, parent, mode) \
	MVM_DEBUGFS_ADD_LINK_STA_FILE_ALIAS(#name, name, parent, mode)

static ssize_t
iwl_dbgfs_prph_reg_read(struct file *file,
			char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int pos = 0;
	char buf[32];
	const size_t bufsz = sizeof(buf);

	if (!mvm->dbgfs_prph_reg_addr)
		return -EINVAL;

	pos += scnprintf(buf + pos, bufsz - pos, "Reg 0x%x: (0x%x)\n",
		mvm->dbgfs_prph_reg_addr,
		iwl_read_prph(mvm->trans, mvm->dbgfs_prph_reg_addr));

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_prph_reg_write(struct iwl_mvm *mvm, char *buf,
			 size_t count, loff_t *ppos)
{
	u8 args;
	u32 value;

	args = sscanf(buf, "%i %i", &mvm->dbgfs_prph_reg_addr, &value);
	/* if we only want to set the reg address - nothing more to do */
	if (args == 1)
		goto out;

	/* otherwise, make sure we have both address and value */
	if (args != 2)
		return -EINVAL;

	iwl_write_prph(mvm->trans, mvm->dbgfs_prph_reg_addr, value);

out:
	return count;
}

static ssize_t
iwl_dbgfs_send_echo_cmd_write(struct iwl_mvm *mvm, char *buf,
			      size_t count, loff_t *ppos)
{
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, ECHO_CMD, 0, 0, NULL);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

struct iwl_mvm_sniffer_apply {
	struct iwl_mvm *mvm;
	u8 *bssid;
	u16 aid;
};

static bool iwl_mvm_sniffer_apply(struct iwl_notif_wait_data *notif_data,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm_sniffer_apply *apply = data;

	apply->mvm->cur_aid = cpu_to_le16(apply->aid);
	memcpy(apply->mvm->cur_bssid, apply->bssid,
	       sizeof(apply->mvm->cur_bssid));

	return true;
}

static ssize_t
iwl_dbgfs_he_sniffer_params_write(struct iwl_mvm *mvm, char *buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_notification_wait wait;
	struct iwl_he_monitor_cmd he_mon_cmd = {};
	struct iwl_mvm_sniffer_apply apply = {
		.mvm = mvm,
	};
	u16 wait_cmds[] = {
		WIDE_ID(DATA_PATH_GROUP, HE_AIR_SNIFFER_CONFIG_CMD),
	};
	u32 aid;
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	ret = sscanf(buf, "%x %2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &aid,
		     &he_mon_cmd.bssid[0], &he_mon_cmd.bssid[1],
		     &he_mon_cmd.bssid[2], &he_mon_cmd.bssid[3],
		     &he_mon_cmd.bssid[4], &he_mon_cmd.bssid[5]);
	if (ret != 7)
		return -EINVAL;

	he_mon_cmd.aid = cpu_to_le16(aid);

	apply.aid = aid;
	apply.bssid = (void *)he_mon_cmd.bssid;

	mutex_lock(&mvm->mutex);

	/*
	 * Use the notification waiter to get our function triggered
	 * in sequence with other RX. This ensures that frames we get
	 * on the RX queue _before_ the new configuration is applied
	 * still have mvm->cur_aid pointing to the old AID, and that
	 * frames on the RX queue _after_ the firmware processed the
	 * new configuration (and sent the response, synchronously)
	 * get mvm->cur_aid correctly set to the new AID.
	 */
	iwl_init_notification_wait(&mvm->notif_wait, &wait,
				   wait_cmds, ARRAY_SIZE(wait_cmds),
				   iwl_mvm_sniffer_apply, &apply);

	ret = iwl_mvm_send_cmd_pdu(mvm,
				   WIDE_ID(DATA_PATH_GROUP, HE_AIR_SNIFFER_CONFIG_CMD),
				   0,
				   sizeof(he_mon_cmd), &he_mon_cmd);

	/* no need to really wait, we already did anyway */
	iwl_remove_notification(&mvm->notif_wait, &wait);

	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t
iwl_dbgfs_he_sniffer_params_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u8 buf[32];
	int len;

	len = scnprintf(buf, sizeof(buf),
			"%d %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
			le16_to_cpu(mvm->cur_aid), mvm->cur_bssid[0],
			mvm->cur_bssid[1], mvm->cur_bssid[2], mvm->cur_bssid[3],
			mvm->cur_bssid[4], mvm->cur_bssid[5]);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t
iwl_dbgfs_uapsd_noagg_bssids_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u8 buf[IWL_MVM_UAPSD_NOAGG_BSSIDS_NUM * ETH_ALEN * 3 + 1];
	unsigned int pos = 0;
	size_t bufsz = sizeof(buf);
	int i;

	mutex_lock(&mvm->mutex);

	for (i = 0; i < IWL_MVM_UAPSD_NOAGG_LIST_LEN; i++)
		pos += scnprintf(buf + pos, bufsz - pos, "%pM\n",
				 mvm->uapsd_noagg_bssids[i].addr);

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_ltr_config_write(struct iwl_mvm *mvm,
			   char *buf, size_t count, loff_t *ppos)
{
	int ret;
	struct iwl_ltr_config_cmd ltr_config = {0};

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (sscanf(buf, "%x,%x,%x,%x,%x,%x,%x",
		   &ltr_config.flags,
		   &ltr_config.static_long,
		   &ltr_config.static_short,
		   &ltr_config.ltr_cfg_values[0],
		   &ltr_config.ltr_cfg_values[1],
		   &ltr_config.ltr_cfg_values[2],
		   &ltr_config.ltr_cfg_values[3]) != 7) {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0, sizeof(ltr_config),
				   &ltr_config);
	mutex_unlock(&mvm->mutex);

	if (ret)
		IWL_ERR(mvm, "failed to send ltr configuration cmd\n");

	return ret ?: count;
}

static ssize_t iwl_dbgfs_rfi_freq_table_write(struct iwl_mvm *mvm, char *buf,
					      size_t count, loff_t *ppos)
{
	int ret = 0;
	u16 op_id;

	if (kstrtou16(buf, 10, &op_id))
		return -EINVAL;

	/* value zero triggers re-sending the default table to the device */
	if (!op_id) {
		mutex_lock(&mvm->mutex);
		ret = iwl_rfi_send_config_cmd(mvm, NULL);
		mutex_unlock(&mvm->mutex);
	} else {
		ret = -EOPNOTSUPP; /* in the future a new table will be added */
	}

	return ret ?: count;
}

/* The size computation is as follows:
 * each number needs at most 3 characters, number of rows is the size of
 * the table; So, need 5 chars for the "freq: " part and each tuple afterwards
 * needs 6 characters for numbers and 5 for the punctuation around.
 */
#define IWL_RFI_BUF_SIZE (IWL_RFI_LUT_INSTALLED_SIZE *\
				(5 + IWL_RFI_LUT_ENTRY_CHANNELS_NUM * (6 + 5)))

static ssize_t iwl_dbgfs_rfi_freq_table_read(struct file *file,
					     char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_rfi_freq_table_resp_cmd *resp;
	u32 status;
	char buf[IWL_RFI_BUF_SIZE];
	int i, j, pos = 0;

	resp = iwl_rfi_get_freq_table(mvm);
	if (IS_ERR(resp))
		return PTR_ERR(resp);

	status = le32_to_cpu(resp->status);
	if (status != RFI_FREQ_TABLE_OK) {
		scnprintf(buf, IWL_RFI_BUF_SIZE, "status = %d\n", status);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(resp->table); i++) {
		pos += scnprintf(buf + pos, IWL_RFI_BUF_SIZE - pos, "%d: ",
				 resp->table[i].freq);

		for (j = 0; j < ARRAY_SIZE(resp->table[i].channels); j++)
			pos += scnprintf(buf + pos, IWL_RFI_BUF_SIZE - pos,
					 "(%d, %d) ",
					 resp->table[i].channels[j],
					 resp->table[i].bands[j]);
		pos += scnprintf(buf + pos, IWL_RFI_BUF_SIZE - pos, "\n");
	}

out:
	kfree(resp);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

MVM_DEBUGFS_READ_WRITE_FILE_OPS(prph_reg, 64);

/* Device wide debugfs entries */
MVM_DEBUGFS_READ_FILE_OPS(ctdp_budget);
MVM_DEBUGFS_WRITE_FILE_OPS(stop_ctdp, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(start_ctdp, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(force_ctkill, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(tx_flush, 16);
MVM_DEBUGFS_WRITE_FILE_OPS(send_echo_cmd, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(sram, 64);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(set_nic_temperature, 64);
MVM_DEBUGFS_READ_FILE_OPS(nic_temp);
MVM_DEBUGFS_READ_FILE_OPS(stations);
MVM_DEBUGFS_READ_LINK_STA_FILE_OPS(rs_data);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(disable_power_off, 64);
MVM_DEBUGFS_READ_FILE_OPS(fw_rx_stats);
MVM_DEBUGFS_READ_FILE_OPS(drv_rx_stats);
MVM_DEBUGFS_READ_FILE_OPS(fw_system_stats);
MVM_DEBUGFS_READ_FILE_OPS(phy_integration_ver);
MVM_DEBUGFS_READ_FILE_OPS(tas_get_status);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_restart, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_nmi, 10);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(scan_ant_rxchain, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg_conf, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_dbg_clear, 64);
MVM_DEBUGFS_WRITE_FILE_OPS(dbg_time_point, 64);
MVM_DEBUGFS_WRITE_FILE_OPS(indirection_tbl,
			   (IWL_RSS_INDIRECTION_TABLE_SIZE * 2));
MVM_DEBUGFS_WRITE_FILE_OPS(inject_packet, 512);
MVM_DEBUGFS_WRITE_FILE_OPS(inject_beacon_ie, 512);
MVM_DEBUGFS_WRITE_FILE_OPS(inject_beacon_ie_restore, 512);

MVM_DEBUGFS_READ_FILE_OPS(uapsd_noagg_bssids);

#ifdef CONFIG_ACPI
MVM_DEBUGFS_READ_FILE_OPS(sar_geo_profile);
MVM_DEBUGFS_READ_FILE_OPS(wifi_6e_enable);
#endif

MVM_DEBUGFS_READ_WRITE_LINK_STA_FILE_OPS(amsdu_len, 16);

MVM_DEBUGFS_READ_WRITE_FILE_OPS(he_sniffer_params, 32);

MVM_DEBUGFS_WRITE_FILE_OPS(ltr_config, 512);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(rfi_freq_table, 16);

static ssize_t iwl_dbgfs_mem_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_dbg_mem_access_cmd cmd = {};
	struct iwl_dbg_mem_access_rsp *rsp;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
	};
	size_t delta;
	ssize_t ret, len;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	hcmd.id = WIDE_ID(DEBUG_GROUP, *ppos >> 24 ? UMAC_RD_WR : LMAC_RD_WR);
	cmd.op = cpu_to_le32(DEBUG_MEM_OP_READ);

	/* Take care of alignment of both the position and the length */
	delta = *ppos & 0x3;
	cmd.addr = cpu_to_le32(*ppos - delta);
	cmd.len = cpu_to_le32(min(ALIGN(count + delta, 4) / 4,
				  (size_t)DEBUG_MEM_MAX_SIZE_DWORDS));

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	mutex_unlock(&mvm->mutex);

	if (ret < 0)
		return ret;

	if (iwl_rx_packet_payload_len(hcmd.resp_pkt) < sizeof(*rsp)) {
		ret = -EIO;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	if (le32_to_cpu(rsp->status) != DEBUG_MEM_STATUS_SUCCESS) {
		ret = -ENXIO;
		goto out;
	}

	len = min((size_t)le32_to_cpu(rsp->len) << 2,
		  iwl_rx_packet_payload_len(hcmd.resp_pkt) - sizeof(*rsp));
	len = min(len - delta, count);
	if (len < 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = len - copy_to_user(user_buf, (u8 *)rsp->data + delta, len);
	*ppos += ret;

out:
	iwl_free_resp(&hcmd);
	return ret;
}

static ssize_t iwl_dbgfs_mem_write(struct file *file,
				   const char __user *user_buf, size_t count,
				   loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_dbg_mem_access_cmd *cmd;
	struct iwl_dbg_mem_access_rsp *rsp;
	struct iwl_host_cmd hcmd = {};
	size_t cmd_size;
	size_t data_size;
	u32 op, len;
	ssize_t ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	hcmd.id = WIDE_ID(DEBUG_GROUP, *ppos >> 24 ? UMAC_RD_WR : LMAC_RD_WR);

	if (*ppos & 0x3 || count < 4) {
		op = DEBUG_MEM_OP_WRITE_BYTES;
		len = min(count, (size_t)(4 - (*ppos & 0x3)));
		data_size = len;
	} else {
		op = DEBUG_MEM_OP_WRITE;
		len = min(count >> 2, (size_t)DEBUG_MEM_MAX_SIZE_DWORDS);
		data_size = len << 2;
	}

	cmd_size = sizeof(*cmd) + ALIGN(data_size, 4);
	cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->op = cpu_to_le32(op);
	cmd->len = cpu_to_le32(len);
	cmd->addr = cpu_to_le32(*ppos);
	if (copy_from_user((void *)cmd->data, user_buf, data_size)) {
		kfree(cmd);
		return -EFAULT;
	}

	hcmd.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
	hcmd.data[0] = (void *)cmd;
	hcmd.len[0] = cmd_size;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	mutex_unlock(&mvm->mutex);

	kfree(cmd);

	if (ret < 0)
		return ret;

	if (iwl_rx_packet_payload_len(hcmd.resp_pkt) < sizeof(*rsp)) {
		ret = -EIO;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	if (rsp->status != DEBUG_MEM_STATUS_SUCCESS) {
		ret = -ENXIO;
		goto out;
	}

	ret = data_size;
	*ppos += ret;

out:
	iwl_free_resp(&hcmd);
	return ret;
}

static const struct file_operations iwl_dbgfs_mem_ops = {
	.read = iwl_dbgfs_mem_read,
	.write = iwl_dbgfs_mem_write,
	.open = simple_open,
	.llseek = default_llseek,
};

void iwl_mvm_link_sta_add_debugfs(struct ieee80211_hw *hw,
				  struct ieee80211_vif *vif,
				  struct ieee80211_link_sta *link_sta,
				  struct dentry *dir)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (iwl_mvm_has_tlc_offload(mvm)) {
		MVM_DEBUGFS_ADD_LINK_STA_FILE(rs_data, dir, 0400);
	}

	MVM_DEBUGFS_ADD_LINK_STA_FILE(amsdu_len, dir, 0600);
}

void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm)
{
	struct dentry *bcast_dir __maybe_unused;

	spin_lock_init(&mvm->drv_stats_lock);

	MVM_DEBUGFS_ADD_FILE(tx_flush, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(sram, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(set_nic_temperature, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(nic_temp, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(ctdp_budget, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(stop_ctdp, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(start_ctdp, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(force_ctkill, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(stations, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(disable_power_off, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_rx_stats, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(drv_rx_stats, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(fw_system_stats, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(fw_restart, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(fw_nmi, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(scan_ant_rxchain, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(prph_reg, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_conf, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_clear, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(dbg_time_point, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(send_echo_cmd, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(indirection_tbl, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_packet, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_beacon_ie, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_beacon_ie_restore, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(rfi_freq_table, mvm->debugfs_dir, 0600);

	if (mvm->fw->phy_integration_ver)
		MVM_DEBUGFS_ADD_FILE(phy_integration_ver, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(tas_get_status, mvm->debugfs_dir, 0400);
#ifdef CONFIG_ACPI
	MVM_DEBUGFS_ADD_FILE(sar_geo_profile, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(wifi_6e_enable, mvm->debugfs_dir, 0400);
#endif
	MVM_DEBUGFS_ADD_FILE(he_sniffer_params, mvm->debugfs_dir, 0600);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SET_LTR_GEN2))
		MVM_DEBUGFS_ADD_FILE(ltr_config, mvm->debugfs_dir, 0200);

	debugfs_create_bool("enable_scan_iteration_notif", 0600,
			    mvm->debugfs_dir, &mvm->scan_iter_notif_enabled);
	debugfs_create_bool("drop_bcn_ap_mode", 0600, mvm->debugfs_dir,
			    &mvm->drop_bcn_ap_mode);

	MVM_DEBUGFS_ADD_FILE(uapsd_noagg_bssids, mvm->debugfs_dir, S_IRUSR);

#ifdef CONFIG_PM_SLEEP
	MVM_DEBUGFS_ADD_FILE(d3_test, mvm->debugfs_dir, 0400);
	debugfs_create_bool("d3_wake_sysassert", 0600, mvm->debugfs_dir,
			    &mvm->d3_wake_sysassert);
	debugfs_create_u32("last_netdetect_scans", 0400, mvm->debugfs_dir,
			   &mvm->last_netdetect_scans);
#endif

	debugfs_create_u8("ps_disabled", 0400, mvm->debugfs_dir,
			  &mvm->ps_disabled);
	debugfs_create_blob("nvm_hw", 0400, mvm->debugfs_dir,
			    &mvm->nvm_hw_blob);
	debugfs_create_blob("nvm_sw", 0400, mvm->debugfs_dir,
			    &mvm->nvm_sw_blob);
	debugfs_create_blob("nvm_calib", 0400, mvm->debugfs_dir,
			    &mvm->nvm_calib_blob);
	debugfs_create_blob("nvm_prod", 0400, mvm->debugfs_dir,
			    &mvm->nvm_prod_blob);
	debugfs_create_blob("nvm_phy_sku", 0400, mvm->debugfs_dir,
			    &mvm->nvm_phy_sku_blob);
	debugfs_create_blob("nvm_reg", S_IRUSR,
			    mvm->debugfs_dir, &mvm->nvm_reg_blob);

	debugfs_create_file("mem", 0600, mvm->debugfs_dir, mvm,
			    &iwl_dbgfs_mem_ops);

	debugfs_create_bool("rx_ts_ptp", 0600, mvm->debugfs_dir,
			    &mvm->rx_ts_ptp);

	/*
	 * Create a symlink with mac80211. It will be removed when mac80211
	 * exists (before the opmode exists which removes the target.)
	 */
	if (!IS_ERR(mvm->debugfs_dir)) {
		char buf[100];

		snprintf(buf, 100, "../../%pd2", mvm->debugfs_dir->d_parent);
		debugfs_create_symlink("iwlwifi", mvm->hw->wiphy->debugfsdir,
				       buf);
	}
}
