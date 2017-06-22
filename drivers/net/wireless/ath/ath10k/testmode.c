/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "testmode.h"

#include <net/netlink.h>
#include <linux/firmware.h>

#include "debug.h"
#include "wmi.h"
#include "hif.h"
#include "hw.h"
#include "core.h"

#include "testmode_i.h"

static const struct nla_policy ath10k_tm_policy[ATH10K_TM_ATTR_MAX + 1] = {
	[ATH10K_TM_ATTR_CMD]		= { .type = NLA_U32 },
	[ATH10K_TM_ATTR_DATA]		= { .type = NLA_BINARY,
					    .len = ATH10K_TM_DATA_MAX_LEN },
	[ATH10K_TM_ATTR_WMI_CMDID]	= { .type = NLA_U32 },
	[ATH10K_TM_ATTR_VERSION_MAJOR]	= { .type = NLA_U32 },
	[ATH10K_TM_ATTR_VERSION_MINOR]	= { .type = NLA_U32 },
};

/* Returns true if callee consumes the skb and the skb should be discarded.
 * Returns false if skb is not used. Does not sleep.
 */
bool ath10k_tm_event_wmi(struct ath10k *ar, u32 cmd_id, struct sk_buff *skb)
{
	struct sk_buff *nl_skb;
	bool consumed;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE,
		   "testmode event wmi cmd_id %d skb %pK skb->len %d\n",
		   cmd_id, skb, skb->len);

	ath10k_dbg_dump(ar, ATH10K_DBG_TESTMODE, NULL, "", skb->data, skb->len);

	spin_lock_bh(&ar->data_lock);

	if (!ar->testmode.utf_monitor) {
		consumed = false;
		goto out;
	}

	/* Only testmode.c should be handling events from utf firmware,
	 * otherwise all sort of problems will arise as mac80211 operations
	 * are not initialised.
	 */
	consumed = true;

	nl_skb = cfg80211_testmode_alloc_event_skb(ar->hw->wiphy,
						   2 * sizeof(u32) + skb->len,
						   GFP_ATOMIC);
	if (!nl_skb) {
		ath10k_warn(ar,
			    "failed to allocate skb for testmode wmi event\n");
		goto out;
	}

	ret = nla_put_u32(nl_skb, ATH10K_TM_ATTR_CMD, ATH10K_TM_CMD_WMI);
	if (ret) {
		ath10k_warn(ar,
			    "failed to to put testmode wmi event cmd attribute: %d\n",
			    ret);
		kfree_skb(nl_skb);
		goto out;
	}

	ret = nla_put_u32(nl_skb, ATH10K_TM_ATTR_WMI_CMDID, cmd_id);
	if (ret) {
		ath10k_warn(ar,
			    "failed to to put testmode wmi even cmd_id: %d\n",
			    ret);
		kfree_skb(nl_skb);
		goto out;
	}

	ret = nla_put(nl_skb, ATH10K_TM_ATTR_DATA, skb->len, skb->data);
	if (ret) {
		ath10k_warn(ar,
			    "failed to copy skb to testmode wmi event: %d\n",
			    ret);
		kfree_skb(nl_skb);
		goto out;
	}

	cfg80211_testmode_event(nl_skb, GFP_ATOMIC);

out:
	spin_unlock_bh(&ar->data_lock);

	return consumed;
}

static int ath10k_tm_cmd_get_version(struct ath10k *ar, struct nlattr *tb[])
{
	struct sk_buff *skb;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE,
		   "testmode cmd get version_major %d version_minor %d\n",
		   ATH10K_TESTMODE_VERSION_MAJOR,
		   ATH10K_TESTMODE_VERSION_MINOR);

	skb = cfg80211_testmode_alloc_reply_skb(ar->hw->wiphy,
						nla_total_size(sizeof(u32)));
	if (!skb)
		return -ENOMEM;

	ret = nla_put_u32(skb, ATH10K_TM_ATTR_VERSION_MAJOR,
			  ATH10K_TESTMODE_VERSION_MAJOR);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	ret = nla_put_u32(skb, ATH10K_TM_ATTR_VERSION_MINOR,
			  ATH10K_TESTMODE_VERSION_MINOR);
	if (ret) {
		kfree_skb(skb);
		return ret;
	}

	return cfg80211_testmode_reply(skb);
}

static int ath10k_tm_fetch_utf_firmware_api_1(struct ath10k *ar,
					      struct ath10k_fw_file *fw_file)
{
	char filename[100];
	int ret;

	snprintf(filename, sizeof(filename), "%s/%s",
		 ar->hw_params.fw.dir, ATH10K_FW_UTF_FILE);

	/* load utf firmware image */
	ret = request_firmware_direct(&fw_file->firmware, filename, ar->dev);
	ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode fw request '%s': %d\n",
		   filename, ret);

	if (ret) {
		ath10k_warn(ar, "failed to retrieve utf firmware '%s': %d\n",
			    filename, ret);
		return ret;
	}

	/* We didn't find FW UTF API 1 ("utf.bin") does not advertise
	 * firmware features. Do an ugly hack where we force the firmware
	 * features to match with 10.1 branch so that wmi.c will use the
	 * correct WMI interface.
	 */

	fw_file->wmi_op_version = ATH10K_FW_WMI_OP_VERSION_10_1;
	fw_file->htt_op_version = ATH10K_FW_HTT_OP_VERSION_10_1;
	fw_file->firmware_data = fw_file->firmware->data;
	fw_file->firmware_len = fw_file->firmware->size;

	return 0;
}

static int ath10k_tm_fetch_firmware(struct ath10k *ar)
{
	struct ath10k_fw_components *utf_mode_fw;
	int ret;

	ret = ath10k_core_fetch_firmware_api_n(ar, ATH10K_FW_UTF_API2_FILE,
					       &ar->testmode.utf_mode_fw.fw_file);
	if (ret == 0) {
		ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode using fw utf api 2");
		goto out;
	}

	ret = ath10k_tm_fetch_utf_firmware_api_1(ar, &ar->testmode.utf_mode_fw.fw_file);
	if (ret) {
		ath10k_err(ar, "failed to fetch utf firmware binary: %d", ret);
		return ret;
	}

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode using utf api 1");

out:
	utf_mode_fw = &ar->testmode.utf_mode_fw;

	/* Use the same board data file as the normal firmware uses (but
	 * it's still "owned" by normal_mode_fw so we shouldn't free it.
	 */
	utf_mode_fw->board_data = ar->normal_mode_fw.board_data;
	utf_mode_fw->board_len = ar->normal_mode_fw.board_len;

	if (!utf_mode_fw->fw_file.otp_data) {
		ath10k_info(ar, "utf.bin didn't contain otp binary, taking it from the normal mode firmware");
		utf_mode_fw->fw_file.otp_data = ar->normal_mode_fw.fw_file.otp_data;
		utf_mode_fw->fw_file.otp_len = ar->normal_mode_fw.fw_file.otp_len;
	}

	return 0;
}

static int ath10k_tm_cmd_utf_start(struct ath10k *ar, struct nlattr *tb[])
{
	const char *ver;
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode cmd utf start\n");

	mutex_lock(&ar->conf_mutex);

	if (ar->state == ATH10K_STATE_UTF) {
		ret = -EALREADY;
		goto err;
	}

	/* start utf only when the driver is not in use  */
	if (ar->state != ATH10K_STATE_OFF) {
		ret = -EBUSY;
		goto err;
	}

	if (WARN_ON(ar->testmode.utf_mode_fw.fw_file.firmware != NULL)) {
		/* utf image is already downloaded, it shouldn't be */
		ret = -EEXIST;
		goto err;
	}

	ret = ath10k_tm_fetch_firmware(ar);
	if (ret) {
		ath10k_err(ar, "failed to fetch UTF firmware: %d", ret);
		goto err;
	}

	if (ar->testmode.utf_mode_fw.fw_file.codeswap_data &&
	    ar->testmode.utf_mode_fw.fw_file.codeswap_len) {
		ret = ath10k_swap_code_seg_init(ar,
						&ar->testmode.utf_mode_fw.fw_file);
		if (ret) {
			ath10k_warn(ar,
				    "failed to init utf code swap segment: %d\n",
				    ret);
			goto err_release_utf_mode_fw;
		}
	}

	spin_lock_bh(&ar->data_lock);
	ar->testmode.utf_monitor = true;
	spin_unlock_bh(&ar->data_lock);

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode wmi version %d\n",
		   ar->testmode.utf_mode_fw.fw_file.wmi_op_version);

	ret = ath10k_hif_power_up(ar);
	if (ret) {
		ath10k_err(ar, "failed to power up hif (testmode): %d\n", ret);
		ar->state = ATH10K_STATE_OFF;
		goto err_release_utf_mode_fw;
	}

	ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_UTF,
				&ar->testmode.utf_mode_fw);
	if (ret) {
		ath10k_err(ar, "failed to start core (testmode): %d\n", ret);
		ar->state = ATH10K_STATE_OFF;
		goto err_power_down;
	}

	ar->state = ATH10K_STATE_UTF;

	if (strlen(ar->testmode.utf_mode_fw.fw_file.fw_version) > 0)
		ver = ar->testmode.utf_mode_fw.fw_file.fw_version;
	else
		ver = "API 1";

	ath10k_info(ar, "UTF firmware %s started\n", ver);

	mutex_unlock(&ar->conf_mutex);

	return 0;

err_power_down:
	ath10k_hif_power_down(ar);

err_release_utf_mode_fw:
	if (ar->testmode.utf_mode_fw.fw_file.codeswap_data &&
	    ar->testmode.utf_mode_fw.fw_file.codeswap_len)
		ath10k_swap_code_seg_release(ar,
					     &ar->testmode.utf_mode_fw.fw_file);

	release_firmware(ar->testmode.utf_mode_fw.fw_file.firmware);
	ar->testmode.utf_mode_fw.fw_file.firmware = NULL;

err:
	mutex_unlock(&ar->conf_mutex);

	return ret;
}

static void __ath10k_tm_cmd_utf_stop(struct ath10k *ar)
{
	lockdep_assert_held(&ar->conf_mutex);

	ath10k_core_stop(ar);
	ath10k_hif_power_down(ar);

	spin_lock_bh(&ar->data_lock);

	ar->testmode.utf_monitor = false;

	spin_unlock_bh(&ar->data_lock);

	if (ar->testmode.utf_mode_fw.fw_file.codeswap_data &&
	    ar->testmode.utf_mode_fw.fw_file.codeswap_len)
		ath10k_swap_code_seg_release(ar,
					     &ar->testmode.utf_mode_fw.fw_file);

	release_firmware(ar->testmode.utf_mode_fw.fw_file.firmware);
	ar->testmode.utf_mode_fw.fw_file.firmware = NULL;

	ar->state = ATH10K_STATE_OFF;
}

static int ath10k_tm_cmd_utf_stop(struct ath10k *ar, struct nlattr *tb[])
{
	int ret;

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE, "testmode cmd utf stop\n");

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto out;
	}

	__ath10k_tm_cmd_utf_stop(ar);

	ret = 0;

	ath10k_info(ar, "UTF firmware stopped\n");

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

static int ath10k_tm_cmd_wmi(struct ath10k *ar, struct nlattr *tb[])
{
	struct sk_buff *skb;
	int ret, buf_len;
	u32 cmd_id;
	void *buf;

	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_UTF) {
		ret = -ENETDOWN;
		goto out;
	}

	if (!tb[ATH10K_TM_ATTR_DATA]) {
		ret = -EINVAL;
		goto out;
	}

	if (!tb[ATH10K_TM_ATTR_WMI_CMDID]) {
		ret = -EINVAL;
		goto out;
	}

	buf = nla_data(tb[ATH10K_TM_ATTR_DATA]);
	buf_len = nla_len(tb[ATH10K_TM_ATTR_DATA]);
	cmd_id = nla_get_u32(tb[ATH10K_TM_ATTR_WMI_CMDID]);

	ath10k_dbg(ar, ATH10K_DBG_TESTMODE,
		   "testmode cmd wmi cmd_id %d buf %pK buf_len %d\n",
		   cmd_id, buf, buf_len);

	ath10k_dbg_dump(ar, ATH10K_DBG_TESTMODE, NULL, "", buf, buf_len);

	skb = ath10k_wmi_alloc_skb(ar, buf_len);
	if (!skb) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(skb->data, buf, buf_len);

	ret = ath10k_wmi_cmd_send(ar, skb, cmd_id);
	if (ret) {
		ath10k_warn(ar, "failed to transmit wmi command (testmode): %d\n",
			    ret);
		goto out;
	}

	ret = 0;

out:
	mutex_unlock(&ar->conf_mutex);
	return ret;
}

int ath10k_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		  void *data, int len)
{
	struct ath10k *ar = hw->priv;
	struct nlattr *tb[ATH10K_TM_ATTR_MAX + 1];
	int ret;

	ret = nla_parse(tb, ATH10K_TM_ATTR_MAX, data, len,
			ath10k_tm_policy);
	if (ret)
		return ret;

	if (!tb[ATH10K_TM_ATTR_CMD])
		return -EINVAL;

	switch (nla_get_u32(tb[ATH10K_TM_ATTR_CMD])) {
	case ATH10K_TM_CMD_GET_VERSION:
		return ath10k_tm_cmd_get_version(ar, tb);
	case ATH10K_TM_CMD_UTF_START:
		return ath10k_tm_cmd_utf_start(ar, tb);
	case ATH10K_TM_CMD_UTF_STOP:
		return ath10k_tm_cmd_utf_stop(ar, tb);
	case ATH10K_TM_CMD_WMI:
		return ath10k_tm_cmd_wmi(ar, tb);
	default:
		return -EOPNOTSUPP;
	}
}

void ath10k_testmode_destroy(struct ath10k *ar)
{
	mutex_lock(&ar->conf_mutex);

	if (ar->state != ATH10K_STATE_UTF) {
		/* utf firmware is not running, nothing to do */
		goto out;
	}

	__ath10k_tm_cmd_utf_stop(ar);

out:
	mutex_unlock(&ar->conf_mutex);
}
