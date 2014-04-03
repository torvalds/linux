/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "htc.h"

MODULE_AUTHOR("Atheros Communications");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Atheros driver 802.11n HTC based wireless devices");

static unsigned int ath9k_debug = ATH_DBG_DEFAULT;
module_param_named(debug, ath9k_debug, uint, 0);
MODULE_PARM_DESC(debug, "Debugging mask");

int htc_modparam_nohwcrypt;
module_param_named(nohwcrypt, htc_modparam_nohwcrypt, int, 0444);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption");

static int ath9k_htc_btcoex_enable;
module_param_named(btcoex_enable, ath9k_htc_btcoex_enable, int, 0444);
MODULE_PARM_DESC(btcoex_enable, "Enable wifi-BT coexistence");

static int ath9k_ps_enable;
module_param_named(ps_enable, ath9k_ps_enable, int, 0444);
MODULE_PARM_DESC(ps_enable, "Enable WLAN PowerSave");

#ifdef CONFIG_MAC80211_LEDS
static const struct ieee80211_tpt_blink ath9k_htc_tpt_blink[] = {
	{ .throughput = 0 * 1024, .blink_time = 334 },
	{ .throughput = 1 * 1024, .blink_time = 260 },
	{ .throughput = 5 * 1024, .blink_time = 220 },
	{ .throughput = 10 * 1024, .blink_time = 190 },
	{ .throughput = 20 * 1024, .blink_time = 170 },
	{ .throughput = 50 * 1024, .blink_time = 150 },
	{ .throughput = 70 * 1024, .blink_time = 130 },
	{ .throughput = 100 * 1024, .blink_time = 110 },
	{ .throughput = 200 * 1024, .blink_time = 80 },
	{ .throughput = 300 * 1024, .blink_time = 50 },
};
#endif

static int ath9k_htc_wait_for_target(struct ath9k_htc_priv *priv)
{
	int time_left;

	if (atomic_read(&priv->htc->tgt_ready) > 0) {
		atomic_dec(&priv->htc->tgt_ready);
		return 0;
	}

	/* Firmware can take up to 50ms to get ready, to be safe use 1 second */
	time_left = wait_for_completion_timeout(&priv->htc->target_wait, HZ);
	if (!time_left) {
		dev_err(priv->dev, "ath9k_htc: Target is unresponsive\n");
		return -ETIMEDOUT;
	}

	atomic_dec(&priv->htc->tgt_ready);

	return 0;
}

static void ath9k_deinit_priv(struct ath9k_htc_priv *priv)
{
	ath9k_hw_deinit(priv->ah);
	kfree(priv->ah);
	priv->ah = NULL;
}

static void ath9k_deinit_device(struct ath9k_htc_priv *priv)
{
	struct ieee80211_hw *hw = priv->hw;

	wiphy_rfkill_stop_polling(hw->wiphy);
	ath9k_deinit_leds(priv);
	ieee80211_unregister_hw(hw);
	ath9k_rx_cleanup(priv);
	ath9k_tx_cleanup(priv);
	ath9k_deinit_priv(priv);
}

static inline int ath9k_htc_connect_svc(struct ath9k_htc_priv *priv,
					u16 service_id,
					void (*tx) (void *,
						    struct sk_buff *,
						    enum htc_endpoint_id,
						    bool txok),
					enum htc_endpoint_id *ep_id)
{
	struct htc_service_connreq req;

	memset(&req, 0, sizeof(struct htc_service_connreq));

	req.service_id = service_id;
	req.ep_callbacks.priv = priv;
	req.ep_callbacks.rx = ath9k_htc_rxep;
	req.ep_callbacks.tx = tx;

	return htc_connect_service(priv->htc, &req, ep_id);
}

static int ath9k_init_htc_services(struct ath9k_htc_priv *priv, u16 devid,
				   u32 drv_info)
{
	int ret;

	/* WMI CMD*/
	ret = ath9k_wmi_connect(priv->htc, priv->wmi, &priv->wmi_cmd_ep);
	if (ret)
		goto err;

	/* Beacon */
	ret = ath9k_htc_connect_svc(priv, WMI_BEACON_SVC, ath9k_htc_beaconep,
				    &priv->beacon_ep);
	if (ret)
		goto err;

	/* CAB */
	ret = ath9k_htc_connect_svc(priv, WMI_CAB_SVC, ath9k_htc_txep,
				    &priv->cab_ep);
	if (ret)
		goto err;


	/* UAPSD */
	ret = ath9k_htc_connect_svc(priv, WMI_UAPSD_SVC, ath9k_htc_txep,
				    &priv->uapsd_ep);
	if (ret)
		goto err;

	/* MGMT */
	ret = ath9k_htc_connect_svc(priv, WMI_MGMT_SVC, ath9k_htc_txep,
				    &priv->mgmt_ep);
	if (ret)
		goto err;

	/* DATA BE */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_BE_SVC, ath9k_htc_txep,
				    &priv->data_be_ep);
	if (ret)
		goto err;

	/* DATA BK */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_BK_SVC, ath9k_htc_txep,
				    &priv->data_bk_ep);
	if (ret)
		goto err;

	/* DATA VI */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_VI_SVC, ath9k_htc_txep,
				    &priv->data_vi_ep);
	if (ret)
		goto err;

	/* DATA VO */
	ret = ath9k_htc_connect_svc(priv, WMI_DATA_VO_SVC, ath9k_htc_txep,
				    &priv->data_vo_ep);
	if (ret)
		goto err;

	/*
	 * Setup required credits before initializing HTC.
	 * This is a bit hacky, but, since queuing is done in
	 * the HIF layer, shouldn't matter much.
	 */

	if (IS_AR7010_DEVICE(drv_info))
		priv->htc->credits = 45;
	else
		priv->htc->credits = 33;

	ret = htc_init(priv->htc);
	if (ret)
		goto err;

	dev_info(priv->dev, "ath9k_htc: HTC initialized with %d credits\n",
		 priv->htc->credits);

	return 0;

err:
	dev_err(priv->dev, "ath9k_htc: Unable to initialize HTC services\n");
	return ret;
}

static void ath9k_reg_notifier(struct wiphy *wiphy,
			       struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct ath9k_htc_priv *priv = hw->priv;

	ath_reg_notifier_apply(wiphy, request,
			       ath9k_hw_regulatory(priv->ah));
}

static unsigned int ath9k_regread(void *hw_priv, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	__be32 val, reg = cpu_to_be32(reg_offset);
	int r;

	r = ath9k_wmi_cmd(priv->wmi, WMI_REG_READ_CMDID,
			  (u8 *) &reg, sizeof(reg),
			  (u8 *) &val, sizeof(val),
			  100);
	if (unlikely(r)) {
		ath_dbg(common, WMI, "REGISTER READ FAILED: (0x%04x, %d)\n",
			reg_offset, r);
		return -EIO;
	}

	return be32_to_cpu(val);
}

static void ath9k_multi_regread(void *hw_priv, u32 *addr,
				u32 *val, u16 count)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	__be32 tmpaddr[8];
	__be32 tmpval[8];
	int i, ret;

       for (i = 0; i < count; i++) {
	       tmpaddr[i] = cpu_to_be32(addr[i]);
       }

       ret = ath9k_wmi_cmd(priv->wmi, WMI_REG_READ_CMDID,
			   (u8 *)tmpaddr , sizeof(u32) * count,
			   (u8 *)tmpval, sizeof(u32) * count,
			   100);
	if (unlikely(ret)) {
		ath_dbg(common, WMI,
			"Multiple REGISTER READ FAILED (count: %d)\n", count);
	}

       for (i = 0; i < count; i++) {
	       val[i] = be32_to_cpu(tmpval[i]);
       }
}

static void ath9k_regwrite_multi(struct ath_common *common)
{
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	u32 rsp_status;
	int r;

	r = ath9k_wmi_cmd(priv->wmi, WMI_REG_WRITE_CMDID,
			  (u8 *) &priv->wmi->multi_write,
			  sizeof(struct register_write) * priv->wmi->multi_write_idx,
			  (u8 *) &rsp_status, sizeof(rsp_status),
			  100);
	if (unlikely(r)) {
		ath_dbg(common, WMI,
			"REGISTER WRITE FAILED, multi len: %d\n",
			priv->wmi->multi_write_idx);
	}
	priv->wmi->multi_write_idx = 0;
}

static void ath9k_regwrite_single(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;
	const __be32 buf[2] = {
		cpu_to_be32(reg_offset),
		cpu_to_be32(val),
	};
	int r;

	r = ath9k_wmi_cmd(priv->wmi, WMI_REG_WRITE_CMDID,
			  (u8 *) &buf, sizeof(buf),
			  (u8 *) &val, sizeof(val),
			  100);
	if (unlikely(r)) {
		ath_dbg(common, WMI, "REGISTER WRITE FAILED:(0x%04x, %d)\n",
			reg_offset, r);
	}
}

static void ath9k_regwrite_buffer(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	mutex_lock(&priv->wmi->multi_write_mutex);

	/* Store the register/value */
	priv->wmi->multi_write[priv->wmi->multi_write_idx].reg =
		cpu_to_be32(reg_offset);
	priv->wmi->multi_write[priv->wmi->multi_write_idx].val =
		cpu_to_be32(val);

	priv->wmi->multi_write_idx++;

	/* If the buffer is full, send it out. */
	if (priv->wmi->multi_write_idx == MAX_CMD_NUMBER)
		ath9k_regwrite_multi(common);

	mutex_unlock(&priv->wmi->multi_write_mutex);
}

static void ath9k_regwrite(void *hw_priv, u32 val, u32 reg_offset)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	if (atomic_read(&priv->wmi->mwrite_cnt))
		ath9k_regwrite_buffer(hw_priv, val, reg_offset);
	else
		ath9k_regwrite_single(hw_priv, val, reg_offset);
}

static void ath9k_enable_regwrite_buffer(void *hw_priv)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	atomic_inc(&priv->wmi->mwrite_cnt);
}

static void ath9k_regwrite_flush(void *hw_priv)
{
	struct ath_hw *ah = (struct ath_hw *) hw_priv;
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath9k_htc_priv *priv = (struct ath9k_htc_priv *) common->priv;

	atomic_dec(&priv->wmi->mwrite_cnt);

	mutex_lock(&priv->wmi->multi_write_mutex);

	if (priv->wmi->multi_write_idx)
		ath9k_regwrite_multi(common);

	mutex_unlock(&priv->wmi->multi_write_mutex);
}

static u32 ath9k_reg_rmw(void *hw_priv, u32 reg_offset, u32 set, u32 clr)
{
	u32 val;

	val = ath9k_regread(hw_priv, reg_offset);
	val &= ~clr;
	val |= set;
	ath9k_regwrite(hw_priv, val, reg_offset);
	return val;
}

static void ath_usb_read_cachesize(struct ath_common *common, int *csz)
{
	*csz = L1_CACHE_BYTES >> 2;
}

static bool ath_usb_eeprom_read(struct ath_common *common, u32 off, u16 *data)
{
	struct ath_hw *ah = (struct ath_hw *) common->ah;

	(void)REG_READ(ah, AR5416_EEPROM_OFFSET + (off << AR5416_EEPROM_S));

	if (!ath9k_hw_wait(ah,
			   AR_EEPROM_STATUS_DATA,
			   AR_EEPROM_STATUS_DATA_BUSY |
			   AR_EEPROM_STATUS_DATA_PROT_ACCESS, 0,
			   AH_WAIT_TIMEOUT))
		return false;

	*data = MS(REG_READ(ah, AR_EEPROM_STATUS_DATA),
		   AR_EEPROM_STATUS_DATA_VAL);

	return true;
}

static const struct ath_bus_ops ath9k_usb_bus_ops = {
	.ath_bus_type = ATH_USB,
	.read_cachesize = ath_usb_read_cachesize,
	.eeprom_read = ath_usb_eeprom_read,
};

static int ath9k_init_queues(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->hwq_map); i++)
		priv->hwq_map[i] = -1;

	priv->beacon.beaconq = ath9k_hw_beaconq_setup(priv->ah);
	if (priv->beacon.beaconq == -1) {
		ath_err(common, "Unable to setup BEACON xmit queue\n");
		goto err;
	}

	priv->cabq = ath9k_htc_cabq_setup(priv);
	if (priv->cabq == -1) {
		ath_err(common, "Unable to setup CAB xmit queue\n");
		goto err;
	}

	if (!ath9k_htc_txq_setup(priv, IEEE80211_AC_BE)) {
		ath_err(common, "Unable to setup xmit queue for BE traffic\n");
		goto err;
	}

	if (!ath9k_htc_txq_setup(priv, IEEE80211_AC_BK)) {
		ath_err(common, "Unable to setup xmit queue for BK traffic\n");
		goto err;
	}
	if (!ath9k_htc_txq_setup(priv, IEEE80211_AC_VI)) {
		ath_err(common, "Unable to setup xmit queue for VI traffic\n");
		goto err;
	}
	if (!ath9k_htc_txq_setup(priv, IEEE80211_AC_VO)) {
		ath_err(common, "Unable to setup xmit queue for VO traffic\n");
		goto err;
	}

	return 0;

err:
	return -EINVAL;
}

static void ath9k_init_misc(struct ath9k_htc_priv *priv)
{
	struct ath_common *common = ath9k_hw_common(priv->ah);

	memcpy(common->bssidmask, ath_bcast_mac, ETH_ALEN);

	common->last_rssi = ATH_RSSI_DUMMY_MARKER;
	priv->ah->opmode = NL80211_IFTYPE_STATION;
}

static int ath9k_init_priv(struct ath9k_htc_priv *priv,
			   u16 devid, char *product,
			   u32 drv_info)
{
	struct ath_hw *ah = NULL;
	struct ath_common *common;
	int i, ret = 0, csz = 0;

	ah = kzalloc(sizeof(struct ath_hw), GFP_KERNEL);
	if (!ah)
		return -ENOMEM;

	ah->dev = priv->dev;
	ah->hw_version.devid = devid;
	ah->hw_version.usbdev = drv_info;
	ah->ah_flags |= AH_USE_EEPROM;
	ah->reg_ops.read = ath9k_regread;
	ah->reg_ops.multi_read = ath9k_multi_regread;
	ah->reg_ops.write = ath9k_regwrite;
	ah->reg_ops.enable_write_buffer = ath9k_enable_regwrite_buffer;
	ah->reg_ops.write_flush = ath9k_regwrite_flush;
	ah->reg_ops.rmw = ath9k_reg_rmw;
	priv->ah = ah;

	common = ath9k_hw_common(ah);
	common->ops = &ah->reg_ops;
	common->bus_ops = &ath9k_usb_bus_ops;
	common->ah = ah;
	common->hw = priv->hw;
	common->priv = priv;
	common->debug_mask = ath9k_debug;
	common->btcoex_enabled = ath9k_htc_btcoex_enable == 1;
	set_bit(ATH_OP_INVALID, &common->op_flags);

	spin_lock_init(&priv->beacon_lock);
	spin_lock_init(&priv->tx.tx_lock);
	mutex_init(&priv->mutex);
	mutex_init(&priv->htc_pm_lock);
	tasklet_init(&priv->rx_tasklet, ath9k_rx_tasklet,
		     (unsigned long)priv);
	tasklet_init(&priv->tx_failed_tasklet, ath9k_tx_failed_tasklet,
		     (unsigned long)priv);
	INIT_DELAYED_WORK(&priv->ani_work, ath9k_htc_ani_work);
	INIT_WORK(&priv->ps_work, ath9k_ps_work);
	INIT_WORK(&priv->fatal_work, ath9k_fatal_work);
	setup_timer(&priv->tx.cleanup_timer, ath9k_htc_tx_cleanup_timer,
		    (unsigned long)priv);

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	ath_read_cachesize(common, &csz);
	common->cachelsz = csz << 2; /* convert to bytes */

	ret = ath9k_hw_init(ah);
	if (ret) {
		ath_err(common,
			"Unable to initialize hardware; initialization status: %d\n",
			ret);
		goto err_hw;
	}

	ret = ath9k_init_queues(priv);
	if (ret)
		goto err_queues;

	for (i = 0; i < ATH9K_HTC_MAX_BCN_VIF; i++)
		priv->beacon.bslot[i] = NULL;
	priv->beacon.slottime = ATH9K_SLOT_TIME_9;

	ath9k_cmn_init_channels_rates(common);
	ath9k_cmn_init_crypto(ah);
	ath9k_init_misc(priv);
	ath9k_htc_init_btcoex(priv, product);

	return 0;

err_queues:
	ath9k_hw_deinit(ah);
err_hw:

	kfree(ah);
	priv->ah = NULL;

	return ret;
}

static const struct ieee80211_iface_limit if_limits[] = {
	{ .max = 2,	.types = BIT(NL80211_IFTYPE_STATION) |
				 BIT(NL80211_IFTYPE_P2P_CLIENT) },
	{ .max = 2,	.types = BIT(NL80211_IFTYPE_AP) |
#ifdef CONFIG_MAC80211_MESH
				 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
				 BIT(NL80211_IFTYPE_P2P_GO) },
};

static const struct ieee80211_iface_combination if_comb = {
	.limits = if_limits,
	.n_limits = ARRAY_SIZE(if_limits),
	.max_interfaces = 2,
	.num_different_channels = 1,
};

static void ath9k_set_hw_capab(struct ath9k_htc_priv *priv,
			       struct ieee80211_hw *hw)
{
	struct ath_hw *ah = priv->ah;
	struct ath_common *common = ath9k_hw_common(priv->ah);
	struct base_eep_header *pBase;

	hw->flags = IEEE80211_HW_SIGNAL_DBM |
		IEEE80211_HW_AMPDU_AGGREGATION |
		IEEE80211_HW_SPECTRUM_MGMT |
		IEEE80211_HW_HAS_RATE_CONTROL |
		IEEE80211_HW_RX_INCLUDES_FCS |
		IEEE80211_HW_PS_NULLFUNC_STACK |
		IEEE80211_HW_REPORTS_TX_ACK_STATUS |
		IEEE80211_HW_MFP_CAPABLE |
		IEEE80211_HW_HOST_BROADCAST_PS_BUFFERING;

	if (ath9k_ps_enable)
		hw->flags |= IEEE80211_HW_SUPPORTS_PS;

	hw->wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION) |
		BIT(NL80211_IFTYPE_ADHOC) |
		BIT(NL80211_IFTYPE_AP) |
		BIT(NL80211_IFTYPE_P2P_GO) |
		BIT(NL80211_IFTYPE_P2P_CLIENT) |
		BIT(NL80211_IFTYPE_MESH_POINT);

	hw->wiphy->iface_combinations = &if_comb;
	hw->wiphy->n_iface_combinations = 1;

	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;

	hw->wiphy->flags |= WIPHY_FLAG_IBSS_RSN |
			    WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL;

	hw->queues = 4;
	hw->max_listen_interval = 1;

	hw->vif_data_size = sizeof(struct ath9k_htc_vif);
	hw->sta_data_size = sizeof(struct ath9k_htc_sta);

	/* tx_frame_hdr is larger than tx_mgmt_hdr anyway */
	hw->extra_tx_headroom = sizeof(struct tx_frame_hdr) +
		sizeof(struct htc_frame_hdr) + 4;

	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_2GHZ)
		hw->wiphy->bands[IEEE80211_BAND_2GHZ] =
			&common->sbands[IEEE80211_BAND_2GHZ];
	if (priv->ah->caps.hw_caps & ATH9K_HW_CAP_5GHZ)
		hw->wiphy->bands[IEEE80211_BAND_5GHZ] =
			&common->sbands[IEEE80211_BAND_5GHZ];

	ath9k_cmn_reload_chainmask(ah);

	pBase = ath9k_htc_get_eeprom_base(priv);
	if (pBase) {
		hw->wiphy->available_antennas_rx = pBase->rxMask;
		hw->wiphy->available_antennas_tx = pBase->txMask;
	}

	SET_IEEE80211_PERM_ADDR(hw, common->macaddr);
}

static int ath9k_init_firmware_version(struct ath9k_htc_priv *priv)
{
	struct ieee80211_hw *hw = priv->hw;
	struct wmi_fw_version cmd_rsp;
	int ret;

	memset(&cmd_rsp, 0, sizeof(cmd_rsp));

	WMI_CMD(WMI_GET_FW_VERSION);
	if (ret)
		return -EINVAL;

	priv->fw_version_major = be16_to_cpu(cmd_rsp.major);
	priv->fw_version_minor = be16_to_cpu(cmd_rsp.minor);

	snprintf(hw->wiphy->fw_version, sizeof(hw->wiphy->fw_version), "%d.%d",
		 priv->fw_version_major,
		 priv->fw_version_minor);

	dev_info(priv->dev, "ath9k_htc: FW Version: %d.%d\n",
		 priv->fw_version_major,
		 priv->fw_version_minor);

	/*
	 * Check if the available FW matches the driver's
	 * required version.
	 */
	if (priv->fw_version_major != MAJOR_VERSION_REQ ||
	    priv->fw_version_minor < MINOR_VERSION_REQ) {
		dev_err(priv->dev, "ath9k_htc: Please upgrade to FW version %d.%d\n",
			MAJOR_VERSION_REQ, MINOR_VERSION_REQ);
		return -EINVAL;
	}

	return 0;
}

static int ath9k_init_device(struct ath9k_htc_priv *priv,
			     u16 devid, char *product, u32 drv_info)
{
	struct ieee80211_hw *hw = priv->hw;
	struct ath_common *common;
	struct ath_hw *ah;
	int error = 0;
	struct ath_regulatory *reg;
	char hw_name[64];

	/* Bring up device */
	error = ath9k_init_priv(priv, devid, product, drv_info);
	if (error != 0)
		goto err_init;

	ah = priv->ah;
	common = ath9k_hw_common(ah);
	ath9k_set_hw_capab(priv, hw);

	error = ath9k_init_firmware_version(priv);
	if (error != 0)
		goto err_fw;

	/* Initialize regulatory */
	error = ath_regd_init(&common->regulatory, priv->hw->wiphy,
			      ath9k_reg_notifier);
	if (error)
		goto err_regd;

	reg = &common->regulatory;

	/* Setup TX */
	error = ath9k_tx_init(priv);
	if (error != 0)
		goto err_tx;

	/* Setup RX */
	error = ath9k_rx_init(priv);
	if (error != 0)
		goto err_rx;

	ath9k_hw_disable(priv->ah);
#ifdef CONFIG_MAC80211_LEDS
	/* must be initialized before ieee80211_register_hw */
	priv->led_cdev.default_trigger = ieee80211_create_tpt_led_trigger(priv->hw,
		IEEE80211_TPT_LEDTRIG_FL_RADIO, ath9k_htc_tpt_blink,
		ARRAY_SIZE(ath9k_htc_tpt_blink));
#endif

	/* Register with mac80211 */
	error = ieee80211_register_hw(hw);
	if (error)
		goto err_register;

	/* Handle world regulatory */
	if (!ath_is_world_regd(reg)) {
		error = regulatory_hint(hw->wiphy, reg->alpha2);
		if (error)
			goto err_world;
	}

	error = ath9k_htc_init_debug(priv->ah);
	if (error) {
		ath_err(common, "Unable to create debugfs files\n");
		goto err_world;
	}

	ath_dbg(common, CONFIG,
		"WMI:%d, BCN:%d, CAB:%d, UAPSD:%d, MGMT:%d, BE:%d, BK:%d, VI:%d, VO:%d\n",
		priv->wmi_cmd_ep,
		priv->beacon_ep,
		priv->cab_ep,
		priv->uapsd_ep,
		priv->mgmt_ep,
		priv->data_be_ep,
		priv->data_bk_ep,
		priv->data_vi_ep,
		priv->data_vo_ep);

	ath9k_hw_name(priv->ah, hw_name, sizeof(hw_name));
	wiphy_info(hw->wiphy, "%s\n", hw_name);

	ath9k_init_leds(priv);
	ath9k_start_rfkill_poll(priv);

	return 0;

err_world:
	ieee80211_unregister_hw(hw);
err_register:
	ath9k_rx_cleanup(priv);
err_rx:
	ath9k_tx_cleanup(priv);
err_tx:
	/* Nothing */
err_regd:
	/* Nothing */
err_fw:
	ath9k_deinit_priv(priv);
err_init:
	return error;
}

int ath9k_htc_probe_device(struct htc_target *htc_handle, struct device *dev,
			   u16 devid, char *product, u32 drv_info)
{
	struct ieee80211_hw *hw;
	struct ath9k_htc_priv *priv;
	int ret;

	hw = ieee80211_alloc_hw(sizeof(struct ath9k_htc_priv), &ath9k_htc_ops);
	if (!hw)
		return -ENOMEM;

	priv = hw->priv;
	priv->hw = hw;
	priv->htc = htc_handle;
	priv->dev = dev;
	htc_handle->drv_priv = priv;
	SET_IEEE80211_DEV(hw, priv->dev);

	ret = ath9k_htc_wait_for_target(priv);
	if (ret)
		goto err_free;

	priv->wmi = ath9k_init_wmi(priv);
	if (!priv->wmi) {
		ret = -EINVAL;
		goto err_free;
	}

	ret = ath9k_init_htc_services(priv, devid, drv_info);
	if (ret)
		goto err_init;

	ret = ath9k_init_device(priv, devid, product, drv_info);
	if (ret)
		goto err_init;

	return 0;

err_init:
	ath9k_deinit_wmi(priv);
err_free:
	ieee80211_free_hw(hw);
	return ret;
}

void ath9k_htc_disconnect_device(struct htc_target *htc_handle, bool hotunplug)
{
	if (htc_handle->drv_priv) {

		/* Check if the device has been yanked out. */
		if (hotunplug)
			htc_handle->drv_priv->ah->ah_flags |= AH_UNPLUGGED;

		ath9k_deinit_device(htc_handle->drv_priv);
		ath9k_deinit_wmi(htc_handle->drv_priv);
		ieee80211_free_hw(htc_handle->drv_priv->hw);
	}
}

#ifdef CONFIG_PM

void ath9k_htc_suspend(struct htc_target *htc_handle)
{
	ath9k_htc_setpower(htc_handle->drv_priv, ATH9K_PM_FULL_SLEEP);
}

int ath9k_htc_resume(struct htc_target *htc_handle)
{
	struct ath9k_htc_priv *priv = htc_handle->drv_priv;
	int ret;

	ret = ath9k_htc_wait_for_target(priv);
	if (ret)
		return ret;

	ret = ath9k_init_htc_services(priv, priv->ah->hw_version.devid,
				      priv->ah->hw_version.usbdev);
	ath9k_configure_leds(priv);

	return ret;
}
#endif

static int __init ath9k_htc_init(void)
{
	if (ath9k_hif_usb_init() < 0) {
		pr_err("No USB devices found, driver not installed\n");
		return -ENODEV;
	}

	return 0;
}
module_init(ath9k_htc_init);

static void __exit ath9k_htc_exit(void)
{
	ath9k_hif_usb_exit();
	pr_info("Driver unloaded\n");
}
module_exit(ath9k_htc_exit);
