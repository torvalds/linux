// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: scan ioctl and command handling
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cmdevt.h"
#include "11n.h"
#include "11ac.h"
#include "11ax.h"
#include "cfg80211.h"

/* The maximum number of channels the firmware can scan per command */
#define NXPWIFI_MAX_CHANNELS_PER_SPECIFIC_SCAN   14

#define NXPWIFI_DEF_CHANNELS_PER_SCAN_CMD        4

/* Memory needed to store a max sized Channel List TLV for a firmware scan */
#define CHAN_TLV_MAX_SIZE  (sizeof(struct nxpwifi_ie_types_header)        \
			    + (NXPWIFI_MAX_CHANNELS_PER_SPECIFIC_SCAN     \
			    * sizeof(struct nxpwifi_chan_scan_param_set)))

/* Memory needed to store supported rate */
#define RATE_TLV_MAX_SIZE   (sizeof(struct nxpwifi_ie_types_rates_param_set) \
			     + HOSTCMD_SUPPORTED_RATES)

/* Memory needed to store a max number/size WildCard SSID TLV for a firmware scan */
#define WILDCARD_SSID_TLV_MAX_SIZE  \
	(NXPWIFI_MAX_SSID_LIST_LENGTH *					\
		(sizeof(struct nxpwifi_ie_types_wildcard_ssid_params)	\
			+ IEEE80211_MAX_SSID_LEN))

/* Maximum memory needed for a nxpwifi_scan_cmd_config with all TLVs at max */
#define MAX_SCAN_CFG_ALLOC (sizeof(struct nxpwifi_scan_cmd_config)        \
				+ sizeof(struct nxpwifi_ie_types_num_probes)   \
				+ sizeof(struct nxpwifi_ie_types_htcap)       \
				+ sizeof(struct nxpwifi_ie_types_vhtcap)      \
				+ sizeof(struct nxpwifi_ie_types_he_cap)      \
				+ CHAN_TLV_MAX_SIZE                 \
				+ RATE_TLV_MAX_SIZE                 \
				+ WILDCARD_SSID_TLV_MAX_SIZE)

union nxpwifi_scan_cmd_config_tlv {
	/* Scan configuration (variable length) */
	struct nxpwifi_scan_cmd_config config;
	/* Max allocated block */
	u8 config_alloc_buf[MAX_SCAN_CFG_ALLOC];
};

#define NXPWIFI_WPA_CIPHER_SUITE_TKIP		SUITE(WLAN_OUI_MICROSOFT, 2)
#define NXPWIFI_WPA_CIPHER_SUITE_CCMP		SUITE(WLAN_OUI_MICROSOFT, 4)

static void
_dbg_security_flags(int log_level, const char *func, const char *desc,
		    struct nxpwifi_private *priv,
		    struct nxpwifi_bssdescriptor *bss_desc)
{
	_nxpwifi_dbg(priv->adapter, log_level,
		     "info: %s: %s:\twpa_ie=%#x wpa2_ie=%#x WEP=%s WPA=%s WPA2=%s\tEncMode=%#x privacy=%#x\n",
		     func, desc,
		     bss_desc->bcn_wpa_ie ?
		     bss_desc->bcn_wpa_ie->vend_hdr.element_id : 0,
		     bss_desc->bcn_rsn_ie ?
		     bss_desc->bcn_rsn_ie->id : 0,
		     priv->sec_info.wep_enabled ? "e" : "d",
		     priv->sec_info.wpa_enabled ? "e" : "d",
		     priv->sec_info.wpa2_enabled ? "e" : "d",
		     priv->sec_info.encryption_mode,
		     bss_desc->privacy);
}

#define dbg_security_flags(mask, desc, priv, bss_desc) \
	_dbg_security_flags(NXPWIFI_DBG_##mask, __func__, desc, priv, bss_desc)

/* Parse a WPA/RSN element and check whether its PTK list contains the OUI */
static u8
nxpwifi_search_oui_in_ie(struct ie_body *iebody, u8 *oui)
{
	u8 count;

	count = iebody->ptk_cnt[0];

	/*
	 * PTK may contain multiple OUIs; iterate through the list and compare
	 * each one
	 */
	while (count) {
		if (!memcmp(iebody->ptk_body, oui, sizeof(iebody->ptk_body)))
			return NXPWIFI_OUI_PRESENT;

		--count;
		if (count)
			iebody = (struct ie_body *)((u8 *)iebody +
						sizeof(iebody->ptk_body));
	}

	pr_debug("info: %s: OUI is not found in PTK\n", __func__);
	return NXPWIFI_OUI_NOT_PRESENT;
}

/* Check whether the RSN IE is present and if its PTK list contains the OUI */
static u8
nxpwifi_is_rsn_oui_present(struct nxpwifi_bssdescriptor *bss_desc,
			   u32 cipher)
{
	struct ie_body *iebody;
	u8 ret = NXPWIFI_OUI_NOT_PRESENT;
	__be32 oui = cpu_to_be32(cipher);

	if (bss_desc->bcn_rsn_ie) {
		iebody = (struct ie_body *)
			 (((u8 *)bss_desc->bcn_rsn_ie->data) +
			  RSN_GTK_OUI_OFFSET);
		ret = nxpwifi_search_oui_in_ie(iebody, (u8 *)&oui);
		if (ret)
			return ret;
	}
	return ret;
}

/* Check if the WPA IE exists and whether its PTK list contains the OUI */
static u8
nxpwifi_is_wpa_oui_present(struct nxpwifi_bssdescriptor *bss_desc, u32 cipher)
{
	struct ie_body *iebody;
	u8 ret = NXPWIFI_OUI_NOT_PRESENT;
	__be32 oui = cpu_to_be32(cipher);

	if (bss_desc->bcn_wpa_ie) {
		iebody = (struct ie_body *)((u8 *)bss_desc->bcn_wpa_ie->data +
					    WPA_GTK_OUI_OFFSET);
		ret = nxpwifi_search_oui_in_ie(iebody, (u8 *)&oui);
		if (ret)
			return ret;
	}
	return ret;
}

/* Check whether both driver and BSS operate with no security */
static bool
nxpwifi_is_bss_no_sec(struct nxpwifi_private *priv,
		      struct nxpwifi_bssdescriptor *bss_desc)
{
	if (!priv->sec_info.wep_enabled && !priv->sec_info.wpa_enabled &&
	    !priv->sec_info.wpa2_enabled &&
	    !bss_desc->bcn_rsn_ie &&
	    !bss_desc->bcn_wpa_ie &&
	    !priv->sec_info.encryption_mode && !bss_desc->privacy) {
		return true;
	}
	return false;
}

/* Check whether static WEP is enabled and the BSS privacy setting matches */
static bool
nxpwifi_is_bss_static_wep(struct nxpwifi_private *priv,
			  struct nxpwifi_bssdescriptor *bss_desc)
{
	if (priv->sec_info.wep_enabled && !priv->sec_info.wpa_enabled &&
	    !priv->sec_info.wpa2_enabled && bss_desc->privacy) {
		return true;
	}
	return false;
}

/* Check whether WPA is enabled and the BSS contains a WPA IE */
static bool
nxpwifi_is_bss_wpa(struct nxpwifi_private *priv,
		   struct nxpwifi_bssdescriptor *bss_desc)
{
	if (!priv->sec_info.wep_enabled && priv->sec_info.wpa_enabled &&
	    !priv->sec_info.wpa2_enabled &&
	    bss_desc->bcn_wpa_ie) {
		dbg_security_flags(INFO, "WPA", priv, bss_desc);
		return true;
	}
	return false;
}

/* Check whether WPA2 is enabled and the BSS includes an RSN IE */
static bool
nxpwifi_is_bss_wpa2(struct nxpwifi_private *priv,
		    struct nxpwifi_bssdescriptor *bss_desc)
{
	if (!priv->sec_info.wep_enabled && !priv->sec_info.wpa_enabled &&
	    priv->sec_info.wpa2_enabled &&
	    bss_desc->bcn_rsn_ie) {
		/*
		 * Some APs (e.g., WRT54G) may omit the privacy bit even when
		 * using WPA2
		 */
		dbg_security_flags(ERROR, "WPA2", priv, bss_desc);
		return true;
	}
	return false;
}

/* Check dynamic WEP: enabled in driver, privacy set, and no WPA/RSN IE present */
static bool
nxpwifi_is_bss_dynamic_wep(struct nxpwifi_private *priv,
			   struct nxpwifi_bssdescriptor *bss_desc)
{
	if (!priv->sec_info.wep_enabled && !priv->sec_info.wpa_enabled &&
	    !priv->sec_info.wpa2_enabled &&
	    !bss_desc->bcn_wpa_ie &&
	    !bss_desc->bcn_rsn_ie &&
	    priv->sec_info.encryption_mode && bss_desc->privacy) {
		dbg_security_flags(INFO, "dynamic", priv, bss_desc);
		return true;
	}
	return false;
}

/*
 * Check whether a scanned network is compatible with the driver's security
 * configuration. The decision considers WEP, WPA, WPA2, privacy settings,
 * and whether HT must be disabled when required (e.g., no AES).
 *
 * General rules:
 * - Open networks: always compatible.
 * - WPA-only: compatible; HT disabled if AES is not supported.
 * - WPA2-only: compatible; HT disabled if AES is not supported.
 * - Static WEP: compatible; HT disabled.
 * - Dynamic WEP: compatible when privacy is enabled.
 *
 * Note: Compatibility is not enforced during roaming except for security mode.
 */
static int
nxpwifi_is_network_compatible(struct nxpwifi_private *priv,
			      struct nxpwifi_bssdescriptor *bss_desc, u32 mode)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	bss_desc->disable_11n = false;

	/* Skip compatibility checks while roaming */
	if (priv->media_connected &&
	    priv->bss_mode == NL80211_IFTYPE_STATION &&
	    bss_desc->bss_mode == NL80211_IFTYPE_STATION)
		return 0;

	if (priv->wps.session_enable) {
		nxpwifi_dbg(adapter, IOCTL,
			    "info: return success directly in WPS period\n");
		return 0;
	}

	if (bss_desc->chan_sw_ie_present) {
		nxpwifi_dbg(adapter, INFO,
			    "Don't connect to AP with WLAN_EID_CHANNEL_SWITCH\n");
		return -EPERM;
	}

	if (bss_desc->bss_mode == mode) {
		if (nxpwifi_is_bss_no_sec(priv, bss_desc)) {
			return 0;
		} else if (nxpwifi_is_bss_static_wep(priv, bss_desc)) {
			nxpwifi_dbg(adapter, INFO,
				    "info: Disable 11n in WEP mode.\n");
			bss_desc->disable_11n = true;
			return 0;
		} else if (nxpwifi_is_bss_wpa(priv, bss_desc)) {
			if (((priv->config_bands & BAND_GN ||
			      priv->config_bands & BAND_AN) &&
			     bss_desc->bcn_ht_cap) &&
			    !nxpwifi_is_wpa_oui_present(bss_desc,
							 NXPWIFI_WPA_CIPHER_SUITE_CCMP)) {
				if (nxpwifi_is_wpa_oui_present
						(bss_desc, NXPWIFI_WPA_CIPHER_SUITE_TKIP)) {
					nxpwifi_dbg(adapter, INFO,
						    "info: Disable 11n if AES\t"
						    "is not supported by AP\n");
					bss_desc->disable_11n = true;
				} else {
					return -EINVAL;
				}
			}
			return 0;
		} else if (nxpwifi_is_bss_wpa2(priv, bss_desc)) {
			if (((priv->config_bands & BAND_GN ||
			      priv->config_bands & BAND_AN) &&
			     bss_desc->bcn_ht_cap) &&
			    !nxpwifi_is_rsn_oui_present(bss_desc,
							WLAN_CIPHER_SUITE_CCMP)) {
				if (nxpwifi_is_rsn_oui_present
						(bss_desc, WLAN_CIPHER_SUITE_TKIP)) {
					nxpwifi_dbg(adapter, INFO,
						    "info: Disable 11n if AES\t"
						    "is not supported by AP\n");
					bss_desc->disable_11n = true;
				} else if (nxpwifi_is_rsn_oui_present
						(bss_desc, WLAN_CIPHER_SUITE_GCMP_256) ||
						nxpwifi_is_rsn_oui_present
						(bss_desc, WLAN_CIPHER_SUITE_CCMP_256)) {
					return 0;
				} else {
					return -EINVAL;
				}
			}
			return 0;
		} else if (nxpwifi_is_bss_dynamic_wep(priv, bss_desc)) {
			return 0;
		}

		/* Security mismatch */
		dbg_security_flags(ERROR, "failed", priv, bss_desc);
		return -EINVAL;
	}

	return -EINVAL;
}

/*
 * Build the channel list for scanning based on region and band settings.
 * Used when a scan request does not specify its own channel list.
 */
static int
nxpwifi_scan_create_channel_list(struct nxpwifi_private *priv,
				 const struct nxpwifi_user_scan_cfg
				 *user_scan_in,
				 struct nxpwifi_chan_scan_param_set
				 *scan_chan_list,
				 u8 filtered_scan)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct nxpwifi_adapter *adapter = priv->adapter;
	int chan_idx = 0, i;
	u16 scan_time = 0;

	if (user_scan_in)
		scan_time = (u16)user_scan_in->chan_list[0].scan_time;

	for (band = 0; (band < NUM_NL80211_BANDS) ; band++) {
		if (!priv->wdev.wiphy->bands[band])
			continue;

		sband = priv->wdev.wiphy->bands[band];

		for (i = 0; (i < sband->n_channels) ; i++) {
			ch = &sband->channels[i];
			if (ch->flags & IEEE80211_CHAN_DISABLED)
				continue;
			scan_chan_list[chan_idx].band_cfg = band;

			if (scan_time)
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(scan_time);
			else if ((ch->flags & IEEE80211_CHAN_NO_IR) ||
				 (ch->flags & IEEE80211_CHAN_RADAR))
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(adapter->passive_scan_time);
			else
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(adapter->active_scan_time);

			if (ch->flags & IEEE80211_CHAN_NO_IR)
				scan_chan_list[chan_idx].chan_scan_mode_bmap |=
					(NXPWIFI_PASSIVE_SCAN | NXPWIFI_HIDDEN_SSID_REPORT);
			else
				scan_chan_list[chan_idx].chan_scan_mode_bmap &=
					~NXPWIFI_PASSIVE_SCAN;

			scan_chan_list[chan_idx].chan_number = (u32)ch->hw_value;
			scan_chan_list[chan_idx].chan_scan_mode_bmap |=
				NXPWIFI_DISABLE_CHAN_FILT;

			if (filtered_scan &&
			    !((ch->flags & IEEE80211_CHAN_NO_IR) ||
			      (ch->flags & IEEE80211_CHAN_RADAR)))
				scan_chan_list[chan_idx].max_scan_time =
				cpu_to_le16(adapter->specific_scan_time);

			chan_idx++;
		}
	}
	return chan_idx;
}

/*
 * Build the channel-list TLV for bgscan based on region and band settings.
 */
static int
nxpwifi_bgscan_create_channel_list(struct nxpwifi_private *priv,
				   const struct nxpwifi_bg_scan_cfg
				   *bgscan_cfg_in,
				   struct nxpwifi_chan_scan_param_set
				   *scan_chan_list)
{
	enum nl80211_band band;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	struct nxpwifi_adapter *adapter = priv->adapter;
	int chan_idx = 0, i;
	u16 scan_time = 0, specific_scan_time = adapter->specific_scan_time;

	if (bgscan_cfg_in)
		scan_time = (u16)bgscan_cfg_in->chan_list[0].scan_time;

	for (band = 0; (band < NUM_NL80211_BANDS); band++) {
		if (!priv->wdev.wiphy->bands[band])
			continue;

		sband = priv->wdev.wiphy->bands[band];

		for (i = 0; (i < sband->n_channels) ; i++) {
			ch = &sband->channels[i];
			if (ch->flags & IEEE80211_CHAN_DISABLED)
				continue;
			scan_chan_list[chan_idx].band_cfg = band;

			if (scan_time)
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(scan_time);
			else if (ch->flags & IEEE80211_CHAN_NO_IR)
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(adapter->passive_scan_time);
			else
				scan_chan_list[chan_idx].max_scan_time =
					cpu_to_le16(specific_scan_time);

			if (ch->flags & IEEE80211_CHAN_NO_IR)
				scan_chan_list[chan_idx].chan_scan_mode_bmap |=
					NXPWIFI_PASSIVE_SCAN;
			else
				scan_chan_list[chan_idx].chan_scan_mode_bmap &=
					~NXPWIFI_PASSIVE_SCAN;

			scan_chan_list[chan_idx].chan_number = (u32)ch->hw_value;
			chan_idx++;
		}
	}
	return chan_idx;
}

/* Append the rate TLV to the scan configuration command */
static int
nxpwifi_append_rate_tlv(struct nxpwifi_private *priv,
			struct nxpwifi_scan_cmd_config *scan_cfg_out,
			u8 radio)
{
	struct nxpwifi_ie_types_rates_param_set *rates_tlv;
	u8 rates[NXPWIFI_SUPPORTED_RATES], *tlv_pos;
	u32 rates_size;

	memset(rates, 0, sizeof(rates));

	tlv_pos = (u8 *)scan_cfg_out->tlv_buf + scan_cfg_out->tlv_buf_len;

	if (priv->scan_request)
		rates_size = nxpwifi_get_rates_from_cfg80211(priv, rates,
							     radio);
	else
		rates_size = nxpwifi_get_supported_rates(priv, rates);

	nxpwifi_dbg(priv->adapter, CMD,
		    "info: SCAN_CMD: Rates size = %d\n",
		rates_size);
	rates_tlv = (struct nxpwifi_ie_types_rates_param_set *)tlv_pos;
	rates_tlv->header.type = cpu_to_le16(WLAN_EID_SUPP_RATES);
	rates_tlv->header.len = cpu_to_le16((u16)rates_size);
	memcpy(rates_tlv->rates, rates, rates_size);
	scan_cfg_out->tlv_buf_len += sizeof(rates_tlv->header) + rates_size;

	return rates_size;
}

/*
 * Build and send multiple scan commands by chunking channel TLVs per scan
 * limit.
 */
static int
nxpwifi_scan_channel_list(struct nxpwifi_private *priv,
			  u32 max_chan_per_scan, u8 filtered_scan,
			  struct nxpwifi_scan_cmd_config *scan_cfg_out,
			  struct nxpwifi_ie_types_chan_list_param_set *tlv_o,
			  struct nxpwifi_chan_scan_param_set *scan_chan_list)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	int ret = 0;
	struct nxpwifi_chan_scan_param_set *tmp_chan_list;
	u32 tlv_idx, rates_size, cmd_no;
	u32 total_scan_time;
	u32 done_early;
	u8 radio_type;

	if (!scan_cfg_out || !tlv_o || !scan_chan_list) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "info: Scan: Null detect: %p, %p, %p\n",
			    scan_cfg_out, tlv_o, scan_chan_list);
		return -EINVAL;
	}

	/* Check csa channel expiry before preparing scan list */
	nxpwifi_11h_get_csa_closed_channel(priv);

	tlv_o->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);

	tmp_chan_list = scan_chan_list;

	/*
	 * Iterate through the channel list and send a firmware scan command for
	 * each group of max_chan_per_scan channels, or individually for
	 * channels 1, 6, and 11 when configured.
	 */
	while (tmp_chan_list->chan_number) {
		tlv_idx = 0;
		total_scan_time = 0;
		radio_type = 0;
		tlv_o->header.len = 0;
		done_early = false;

		/*
		 * Build the channel TLV for the scan command. Continue adding
		 * channel TLVs until one of the following conditions is met:
		 * - tlv_idx reaches the maximum allowed per scan command
		 * - the next channel is 0 (end of the desired channel list)
		 * - done_early is set (used for per-channel scanning of 1, 6,
		 * and 11)
		 */
		while (tlv_idx < max_chan_per_scan &&
		       tmp_chan_list->chan_number && !done_early) {
			if (tmp_chan_list->chan_number == priv->csa_chan) {
				tmp_chan_list++;
				continue;
			}

			radio_type = tmp_chan_list->band_cfg;
			nxpwifi_dbg(priv->adapter, INFO,
				    "info: Scan: Chan(%3d), Band(%d),\t"
				    "Mode(%d, %d), Dur(%d)\n",
				    tmp_chan_list->chan_number,
				    tmp_chan_list->band_cfg,
				    tmp_chan_list->chan_scan_mode_bmap
				    & NXPWIFI_PASSIVE_SCAN,
				    (tmp_chan_list->chan_scan_mode_bmap
				    & NXPWIFI_DISABLE_CHAN_FILT) >> 1,
				    le16_to_cpu(tmp_chan_list->max_scan_time));

			/* Copy the current channel TLV into the command being prepared */
			memcpy(&tlv_o->chan_scan_param[tlv_idx], tmp_chan_list,
			       sizeof(*tlv_o->chan_scan_param));

			/*
			 * Increment the TLV header length by the size
			 * appended
			 */
			le16_unaligned_add_cpu(&tlv_o->header.len,
					       sizeof(*tlv_o->chan_scan_param));

			/*
			 * The tlv buffer length is set to the number of bytes
			 * of the between the channel tlv pointer and the start
			 * of the tlv buffer.  This compensates for any TLVs
			 * that were appended before the channel list.
			 */
			scan_cfg_out->tlv_buf_len =
				(u32)((u8 *)tlv_o - scan_cfg_out->tlv_buf);

			scan_cfg_out->tlv_buf_len +=
				(sizeof(tlv_o->header)
				 + le16_to_cpu(tlv_o->header.len));

			/* Advance the index for the channel TLV being constructed. */
			tlv_idx++;

			/* Count the total scan time per command */
			total_scan_time +=
				le16_to_cpu(tmp_chan_list->max_scan_time);

			done_early = false;

			/*
			 * Stop the loop if the current channel is one of 1, 6,
			 * or 11 and no SSID or BSSID filter is applied.
			 */
			if (!filtered_scan &&
			    (tmp_chan_list->chan_number == 1 ||
			     tmp_chan_list->chan_number == 6 ||
			     tmp_chan_list->chan_number == 11))
				done_early = true;

			/* Advance the tmp pointer to the next channel to be scanned. */
			tmp_chan_list++;

			/*
			 * Stop the loop if the next channel is one of 1, 6,
			 * or 11. This causes that channel to be scanned alone
			 * in the next iteration.
			 */
			if (!filtered_scan &&
			    (tmp_chan_list->chan_number == 1 ||
			     tmp_chan_list->chan_number == 6 ||
			     tmp_chan_list->chan_number == 11))
				done_early = true;
		}

		/* Ensure the total scan time does not exceed the scan-command timeout. */
		if (total_scan_time > NXPWIFI_MAX_TOTAL_SCAN_TIME) {
			nxpwifi_dbg(priv->adapter, ERROR,
				    "total scan time %dms\t"
				    "is over limit (%dms), scan skipped\n",
				    total_scan_time,
				    NXPWIFI_MAX_TOTAL_SCAN_TIME);
			ret = -EINVAL;
			break;
		}

		rates_size = nxpwifi_append_rate_tlv(priv, scan_cfg_out,
						     radio_type);

		if (priv->adapter->ext_scan)
			cmd_no = HOST_CMD_802_11_SCAN_EXT;
		else
			cmd_no = HOST_CMD_802_11_SCAN;

		ret = nxpwifi_send_cmd(priv, cmd_no, HOST_ACT_GEN_SET,
				       0, scan_cfg_out, false);

		/*
		 * The rate element is updated for each scan command, but the
		 * same starting pointer is reused, so the previous rate element
		 * in scan_cfg_out->buf is overwritten.
		 */
		scan_cfg_out->tlv_buf_len -=
		    sizeof(struct nxpwifi_ie_types_header) + rates_size;

		if (ret) {
			nxpwifi_cancel_pending_scan_cmd(adapter);
			break;
		}
	}

	return ret;
}

/*
 * Build final scan config from user params, disabling missing filters and using
 * defaults.
 */
static void
nxpwifi_config_scan(struct nxpwifi_private *priv,
		    const struct nxpwifi_user_scan_cfg *user_scan_in,
		    struct nxpwifi_scan_cmd_config *scan_cfg_out,
		    struct nxpwifi_ie_types_chan_list_param_set **chan_list_out,
		    struct nxpwifi_chan_scan_param_set *scan_chan_list,
		    u8 *max_chan_per_scan, u8 *filtered_scan,
		    u8 *scan_current_only)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_ie_types_num_probes *num_probes_tlv;
	struct nxpwifi_ie_types_scan_chan_gap *chan_gap_tlv;
	struct nxpwifi_ie_types_random_mac *random_mac_tlv;
	struct nxpwifi_ie_types_wildcard_ssid_params *wildcard_ssid_tlv;
	struct nxpwifi_ie_types_bssid_list *bssid_tlv;
	struct nxpwifi_ie_types_extcap *ext_cap;
	u8 *ext_capab = NULL;
	u8 *tlv_pos;
	u32 num_probes;
	u32 ssid_len;
	u32 chan_idx;
	u32 scan_time;
	u32 scan_type;
	u16 scan_dur;
	u8 channel;
	u8 radio_type;
	int i, vsid;
	u8 ssid_filter;
	struct nxpwifi_ie_types_htcap *ht_cap;
	struct nxpwifi_ie_types_bss_mode *bss_mode;
	struct nxpwifi_ie_types_vhtcap *vht_cap;
	struct nxpwifi_ie_types_he_cap *he_cap;

	/*
	 * tlv_buf_len is recalculated for each scan command. TLVs added in this
	 * routine are preserved because the send routine appends channel TLVs
	 * at chan_list_out. The difference between chan_list_out and the start
	 * of the TLV buffer determines the size of the TLVs added here.
	 */
	scan_cfg_out->tlv_buf_len = 0;

	/*
	 * Running TLV pointer. It is assigned to chan_list_out at the end of
	 * the function so later routines know where channel TLVs can be
	 * appended in the command buffer.
	 */
	tlv_pos = scan_cfg_out->tlv_buf;

	/*
	 * Initialize the scan as un-filtered; the flag is later set to TRUE
	 * below if a SSID or BSSID filter is sent in the command
	 */
	*filtered_scan = false;

	/*
	 * Initialize the scan as not being only on the current channel.  If
	 * the channel list is customized, only contains one channel, and is
	 * the active channel, this is set true and data flow is not halted.
	 */
	*scan_current_only = false;

	if (user_scan_in) {
		u8 tmpaddr[ETH_ALEN];

		/*
		 * Default the ssid_filter flag to TRUE, set false under
		 * certain wildcard conditions and qualified by the existence
		 * of an SSID list before marking the scan as filtered
		 */
		ssid_filter = true;

		/*
		 * Set the BSS type scan filter, use Adapter setting if
		 * unset
		 */
		scan_cfg_out->bss_mode =
			(u8)(user_scan_in->bss_mode ?: adapter->scan_mode);

		/*
		 * Set the number of probes to send, use Adapter setting
		 * if unset
		 */
		num_probes = user_scan_in->num_probes ?: adapter->scan_probes;

		/*
		 * Set the BSSID filter to the incoming configuration,
		 * if non-zero.  If not set, it will remain disabled
		 * (all zeros).
		 */
		memcpy(scan_cfg_out->specific_bssid,
		       user_scan_in->specific_bssid,
		       sizeof(scan_cfg_out->specific_bssid));

		memcpy(tmpaddr, scan_cfg_out->specific_bssid, ETH_ALEN);

		if (adapter->ext_scan &&
		    !is_zero_ether_addr(tmpaddr)) {
			bssid_tlv =
				(struct nxpwifi_ie_types_bssid_list *)tlv_pos;
			bssid_tlv->header.type = cpu_to_le16(TLV_TYPE_BSSID);
			bssid_tlv->header.len = cpu_to_le16(ETH_ALEN);
			memcpy(bssid_tlv->bssid, user_scan_in->specific_bssid,
			       ETH_ALEN);
			tlv_pos += sizeof(struct nxpwifi_ie_types_bssid_list);
		}

		for (i = 0; i < user_scan_in->num_ssids; i++) {
			ssid_len = user_scan_in->ssid_list[i].ssid_len;

			wildcard_ssid_tlv =
				(struct nxpwifi_ie_types_wildcard_ssid_params *)
				tlv_pos;
			wildcard_ssid_tlv->header.type =
				cpu_to_le16(TLV_TYPE_WILDCARDSSID);
			wildcard_ssid_tlv->header.len =
				cpu_to_le16((u16)(ssid_len + sizeof(u8)));

			/*
			 * max_ssid_length = 0 tells firmware to perform
			 * specific scan for the SSID filled, whereas
			 * max_ssid_length = IEEE80211_MAX_SSID_LEN is for
			 * wildcard scan.
			 */
			if (ssid_len)
				wildcard_ssid_tlv->max_ssid_length = 0;
			else
				wildcard_ssid_tlv->max_ssid_length =
							IEEE80211_MAX_SSID_LEN;

			if (!memcmp(user_scan_in->ssid_list[i].ssid,
				    "DIRECT-", 7))
				wildcard_ssid_tlv->max_ssid_length = 0xfe;

			memcpy(wildcard_ssid_tlv->ssid,
			       user_scan_in->ssid_list[i].ssid, ssid_len);

			tlv_pos += (sizeof(wildcard_ssid_tlv->header)
				+ le16_to_cpu(wildcard_ssid_tlv->header.len));

			nxpwifi_dbg(adapter, INFO,
				    "info: scan: ssid[%d]: %s, %d\n",
				    i, wildcard_ssid_tlv->ssid,
				    wildcard_ssid_tlv->max_ssid_length);

			/*
			 * Empty wildcard ssid with a maxlen will match many or
			 * potentially all SSIDs (maxlen == 32), therefore do
			 * not treat the scan as
			 * filtered.
			 */
			if (!ssid_len && wildcard_ssid_tlv->max_ssid_length)
				ssid_filter = false;
		}

		/*
		 * The default number of channels sent in the command is low to
		 *  ensure the response buffer from the firmware does not
		 *  truncate scan results.  That is not an issue with an SSID
		 *  or BSSID filter applied to the scan results in the firmware.
		 */
		memcpy(tmpaddr, scan_cfg_out->specific_bssid, ETH_ALEN);
		if ((i && ssid_filter) ||
		    !is_zero_ether_addr(tmpaddr))
			*filtered_scan = true;

		if (user_scan_in->scan_chan_gap) {
			nxpwifi_dbg(adapter, INFO,
				    "info: scan: channel gap = %d\n",
				    user_scan_in->scan_chan_gap);
			*max_chan_per_scan =
					NXPWIFI_MAX_CHANNELS_PER_SPECIFIC_SCAN;

			chan_gap_tlv = (void *)tlv_pos;
			chan_gap_tlv->header.type =
					 cpu_to_le16(TLV_TYPE_SCAN_CHANNEL_GAP);
			chan_gap_tlv->header.len =
				    cpu_to_le16(sizeof(chan_gap_tlv->chan_gap));
			chan_gap_tlv->chan_gap =
				     cpu_to_le16((user_scan_in->scan_chan_gap));
			tlv_pos +=
				  sizeof(struct nxpwifi_ie_types_scan_chan_gap);
		}

		if (!is_zero_ether_addr(user_scan_in->random_mac)) {
			random_mac_tlv = (void *)tlv_pos;
			random_mac_tlv->header.type =
					 cpu_to_le16(TLV_TYPE_RANDOM_MAC);
			random_mac_tlv->header.len =
				    cpu_to_le16(sizeof(random_mac_tlv->mac));
			ether_addr_copy(random_mac_tlv->mac,
					user_scan_in->random_mac);
			tlv_pos +=
				  sizeof(struct nxpwifi_ie_types_random_mac);
		}
	} else {
		scan_cfg_out->bss_mode = (u8)adapter->scan_mode;
		num_probes = adapter->scan_probes;
	}

	/*
	 * If a specific BSSID or SSID is used, the number of channels in the
	 *  scan command will be increased to the absolute maximum.
	 */
	if (*filtered_scan) {
		*max_chan_per_scan = NXPWIFI_MAX_CHANNELS_PER_SPECIFIC_SCAN;
	} else {
		if (!priv->media_connected)
			*max_chan_per_scan = NXPWIFI_DEF_CHANNELS_PER_SCAN_CMD;
		else
			*max_chan_per_scan =
					NXPWIFI_DEF_CHANNELS_PER_SCAN_CMD / 2;
	}

	if (adapter->ext_scan) {
		bss_mode = (struct nxpwifi_ie_types_bss_mode *)tlv_pos;
		bss_mode->header.type = cpu_to_le16(TLV_TYPE_BSS_MODE);
		bss_mode->header.len = cpu_to_le16(sizeof(bss_mode->bss_mode));
		bss_mode->bss_mode = scan_cfg_out->bss_mode;
		tlv_pos += sizeof(bss_mode->header) +
			   le16_to_cpu(bss_mode->header.len);
	}

	/*
	 * If the input config or adapter has the number of Probes set,
	 * add tlv
	 */
	if (num_probes) {
		nxpwifi_dbg(adapter, INFO,
			    "info: scan: num_probes = %d\n",
			    num_probes);

		num_probes_tlv = (struct nxpwifi_ie_types_num_probes *)tlv_pos;
		num_probes_tlv->header.type = cpu_to_le16(TLV_TYPE_NUMPROBES);
		num_probes_tlv->header.len =
			cpu_to_le16(sizeof(num_probes_tlv->num_probes));
		num_probes_tlv->num_probes = cpu_to_le16((u16)num_probes);

		tlv_pos += sizeof(num_probes_tlv->header) +
			le16_to_cpu(num_probes_tlv->header.len);
	}

	if (ISSUPP_11NENABLED(priv->adapter->fw_cap_info) &&
	    (priv->config_bands & BAND_GN ||
	     priv->config_bands & BAND_AN)) {
		ht_cap = (struct nxpwifi_ie_types_htcap *)tlv_pos;
		memset(ht_cap, 0, sizeof(struct nxpwifi_ie_types_htcap));
		ht_cap->header.type = cpu_to_le16(WLAN_EID_HT_CAPABILITY);
		ht_cap->header.len =
			cpu_to_le16(sizeof(struct ieee80211_ht_cap));
		radio_type =
			nxpwifi_band_to_radio_type(priv->config_bands);
		nxpwifi_fill_cap_info(priv, radio_type, &ht_cap->ht_cap);
		tlv_pos += sizeof(struct nxpwifi_ie_types_htcap);
	}

	if (ISSUPP_11ACENABLED(adapter->fw_cap_info) &&
	    (priv->config_bands & BAND_AAC)) {
		vht_cap = (struct nxpwifi_ie_types_vhtcap *)tlv_pos;
		memset(vht_cap, 0, sizeof(struct nxpwifi_ie_types_vhtcap));
		vht_cap->header.type = cpu_to_le16(WLAN_EID_VHT_CAPABILITY);
		vht_cap->header.len = cpu_to_le16(sizeof(struct ieee80211_vht_cap));
		nxpwifi_fill_vht_cap_tlv(priv, &vht_cap->vht_cap, priv->config_bands);
		tlv_pos += sizeof(*vht_cap);
	}

	if (ISSUPP_11AXENABLED(adapter->fw_cap_ext) &&
	    (priv->config_bands & BAND_GAX ||
	     priv->config_bands & BAND_AAX)) {
		he_cap = (struct nxpwifi_ie_types_he_cap *)tlv_pos;
		memset(he_cap, 0, sizeof(struct nxpwifi_ie_types_he_cap));
		tlv_pos += nxpwifi_fill_he_cap_tlv(priv, he_cap, priv->config_bands);
	}

	if (nxpwifi_is_sta_11ax_twt_req_supported(priv)) {
		for (vsid = 0; vsid < NXPWIFI_MAX_VSIE_NUM; vsid++) {
			if (priv->vs_ie[vsid].mask & NXPWIFI_VSIE_MASK_SCAN) {
				ext_capab = (u8 *)cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY,
								   priv->vs_ie[vsid].ie,
								   sizeof(priv->vs_ie[vsid].ie));
				break;
			}
		}

		if (ext_capab) {
			ext_capab += 2;
		} else {
			ext_cap = (struct nxpwifi_ie_types_extcap *)tlv_pos;
			memset(ext_cap, 0, sizeof(struct nxpwifi_ie_types_extcap) +
			       NXPWIFI_EXT_CAPAB_IE_LEN);
			ext_cap->header.type = cpu_to_le16(WLAN_EID_EXT_CAPABILITY);
			ext_cap->header.len = cpu_to_le16(NXPWIFI_EXT_CAPAB_IE_LEN);
			ext_capab = ext_cap->ext_capab;
			tlv_pos += sizeof(struct nxpwifi_ie_types_extcap) +
				le16_to_cpu(ext_cap->header.len);
		}

		ext_capab[9] |= WLAN_EXT_CAPA10_TWT_REQUESTER_SUPPORT;
	}

	/* Append vendor specific element TLV */
	nxpwifi_cmd_append_vsie_tlv(priv, NXPWIFI_VSIE_MASK_SCAN, &tlv_pos);

	/*
	 * Set the channel TLV output pointer to the end of the newly added TLVs
	 * (SSID, num_probes). Channel TLVs for each scan will be appended after
	 * these, preserving previously added TLVs.
	 */
	*chan_list_out =
		(struct nxpwifi_ie_types_chan_list_param_set *)tlv_pos;

	if (user_scan_in && user_scan_in->chan_list[0].chan_number) {
		nxpwifi_dbg(adapter, INFO,
			    "info: Scan: Using supplied channel list\n");

		for (chan_idx = 0;
		     chan_idx < NXPWIFI_USER_SCAN_CHAN_MAX &&
		     user_scan_in->chan_list[chan_idx].chan_number;
		     chan_idx++) {
			channel = user_scan_in->chan_list[chan_idx].chan_number;
			scan_chan_list[chan_idx].chan_number = channel;

			radio_type =
				user_scan_in->chan_list[chan_idx].radio_type;
			scan_chan_list[chan_idx].band_cfg = radio_type;

			scan_type = user_scan_in->chan_list[chan_idx].scan_type;

			if (scan_type == NXPWIFI_SCAN_TYPE_PASSIVE)
				scan_chan_list[chan_idx].chan_scan_mode_bmap |=
					(NXPWIFI_PASSIVE_SCAN |
					 NXPWIFI_HIDDEN_SSID_REPORT);
			else
				scan_chan_list[chan_idx].chan_scan_mode_bmap &=
					~NXPWIFI_PASSIVE_SCAN;

			scan_chan_list[chan_idx].chan_scan_mode_bmap |=
				NXPWIFI_DISABLE_CHAN_FILT;

			scan_time = user_scan_in->chan_list[chan_idx].scan_time;

			if (scan_time) {
				scan_dur = (u16)scan_time;
			} else {
				if (scan_type == NXPWIFI_SCAN_TYPE_PASSIVE)
					scan_dur = adapter->passive_scan_time;
				else if (*filtered_scan)
					scan_dur = adapter->specific_scan_time;
				else
					scan_dur = adapter->active_scan_time;
			}

			scan_chan_list[chan_idx].min_scan_time =
				cpu_to_le16(scan_dur);
			scan_chan_list[chan_idx].max_scan_time =
				cpu_to_le16(scan_dur);
		}

		/* Check if we are only scanning the current channel */
		if (chan_idx == 1 &&
		    user_scan_in->chan_list[0].chan_number ==
		    priv->curr_bss_params.bss_descriptor.channel) {
			*scan_current_only = true;
			nxpwifi_dbg(adapter, INFO,
				    "info: Scan: Scanning current channel only\n");
		}
	} else {
		nxpwifi_dbg(adapter, INFO,
			    "info: Scan: Creating full region channel list\n");
		nxpwifi_scan_create_channel_list(priv, user_scan_in,
						 scan_chan_list,
						 *filtered_scan);
	}
}

/* Parse the beacon buffer and update the BSS descriptor fields. */
int nxpwifi_update_bss_desc_with_ie(struct nxpwifi_adapter *adapter,
				    struct nxpwifi_bssdescriptor *bss_entry)
{
	u8 element_id;
	u16 elem_size = sizeof(struct element);
	struct ieee_types_fh_param_set *fh_param_set;
	struct ieee_types_ds_param_set *ds_param_set;
	struct ieee_types_cf_param_set *cf_param_set;
	u8 *current_ptr;
	u8 *rate;
	u8 element_len;
	u16 total_ie_len;
	u8 bytes_to_copy;
	u8 rate_size;
	u8 found_data_rate_ie;
	u32 bytes_left;
	struct ieee_types_vendor_specific *vendor_ie;
	const u8 wpa_oui[4] = { 0x00, 0x50, 0xf2, 0x01 };
	const u8 wmm_oui[4] = { 0x00, 0x50, 0xf2, 0x02 };
	struct element *elem;

	found_data_rate_ie = false;
	rate_size = 0;
	current_ptr = bss_entry->beacon_buf;
	bytes_left = bss_entry->beacon_buf_size;

	/* Process variable element */
	while (bytes_left >= 2) {
		element_id = *current_ptr;
		element_len = *(current_ptr + 1);
		total_ie_len = element_len + elem_size;

		if (bytes_left < total_ie_len) {
			nxpwifi_dbg(adapter, ERROR,
				    "err: InterpretIE: in processing\t"
				    "element, bytes left < element length\n");
			return -EINVAL;
		}
		switch (element_id) {
		case WLAN_EID_SSID:
			if (element_len > IEEE80211_MAX_SSID_LEN)
				return -EINVAL;
			bss_entry->ssid.ssid_len = element_len;
			memcpy(bss_entry->ssid.ssid, (current_ptr + 2),
			       element_len);
			nxpwifi_dbg(adapter, INFO,
				    "info: InterpretIE: ssid: %-32s\n",
				    bss_entry->ssid.ssid);
			break;

		case WLAN_EID_SUPP_RATES:
			if (element_len > NXPWIFI_SUPPORTED_RATES)
				return -EINVAL;
			memcpy(bss_entry->data_rates, current_ptr + 2,
			       element_len);
			memcpy(bss_entry->supported_rates, current_ptr + 2,
			       element_len);
			rate_size = element_len;
			found_data_rate_ie = true;
			break;

		case WLAN_EID_FH_PARAMS:
			if (total_ie_len < sizeof(*fh_param_set))
				return -EINVAL;
			fh_param_set =
				(struct ieee_types_fh_param_set *)current_ptr;
			memcpy(&bss_entry->phy_param_set.fh_param_set,
			       fh_param_set,
			       sizeof(struct ieee_types_fh_param_set));
			break;

		case WLAN_EID_DS_PARAMS:
			if (total_ie_len < sizeof(*ds_param_set))
				return -EINVAL;
			ds_param_set =
				(struct ieee_types_ds_param_set *)current_ptr;

			bss_entry->channel = ds_param_set->current_chan;

			memcpy(&bss_entry->phy_param_set.ds_param_set,
			       ds_param_set,
			       sizeof(struct ieee_types_ds_param_set));
			break;

		case WLAN_EID_CF_PARAMS:
			if (total_ie_len < sizeof(*cf_param_set))
				return -EINVAL;
			cf_param_set =
				(struct ieee_types_cf_param_set *)current_ptr;
			memcpy(&bss_entry->cf_param_set,
			       cf_param_set,
			       sizeof(struct ieee_types_cf_param_set));
			break;

		case WLAN_EID_ERP_INFO:
			if (!element_len)
				return -EINVAL;
			bss_entry->erp_flags = *(current_ptr + 2);
			break;

		case WLAN_EID_PWR_CONSTRAINT:
			if (!element_len)
				return -EINVAL;
			bss_entry->local_constraint = *(current_ptr + 2);
			bss_entry->sensed_11h = true;
			break;

		case WLAN_EID_CHANNEL_SWITCH:
			bss_entry->chan_sw_ie_present = true;
			fallthrough;
		case WLAN_EID_PWR_CAPABILITY:
		case WLAN_EID_TPC_REPORT:
		case WLAN_EID_QUIET:
			bss_entry->sensed_11h = true;
			break;

		case WLAN_EID_EXT_SUPP_RATES:
			/*
			 * Only process extended supported rate
			 * if data rate is already found.
			 * Data rate element should come before
			 * extended supported rate element
			 */
			if (found_data_rate_ie) {
				if ((element_len + rate_size) >
				    NXPWIFI_SUPPORTED_RATES)
					bytes_to_copy =
						(NXPWIFI_SUPPORTED_RATES -
						 rate_size);
				else
					bytes_to_copy = element_len;

				rate = (u8 *)bss_entry->data_rates;
				rate += rate_size;
				memcpy(rate, current_ptr + 2, bytes_to_copy);

				rate = (u8 *)bss_entry->supported_rates;
				rate += rate_size;
				memcpy(rate, current_ptr + 2, bytes_to_copy);
			}
			break;

		case WLAN_EID_VENDOR_SPECIFIC:
			vendor_ie = (struct ieee_types_vendor_specific *)
				current_ptr;

			/* 802.11 requires at least 3-byte OUI. */
			if (element_len < sizeof(vendor_ie->vend_hdr.oui))
				return -EINVAL;

			/* Not long enough for a match? Skip it. */
			if (element_len < sizeof(wpa_oui))
				break;

			if (!memcmp(&vendor_ie->vend_hdr.oui, wpa_oui,
				    sizeof(wpa_oui))) {
				bss_entry->bcn_wpa_ie =
					(struct ieee_types_vendor_specific *)
					current_ptr;
				bss_entry->wpa_offset =
					(u16)(current_ptr -
					      bss_entry->beacon_buf);
			} else if (!memcmp(&vendor_ie->vend_hdr.oui, wmm_oui,
					   sizeof(wmm_oui))) {
				if (total_ie_len ==
				    sizeof(struct ieee80211_wmm_param_ie) ||
				    total_ie_len ==
				    sizeof(struct ieee_types_wmm_info))
					/*
					 * Only accept and copy the WMM element if
					 * it matches the size expected for the
					 * WMM Info element or the WMM Parameter element.
					 */
					memcpy((u8 *)&bss_entry->wmm_ie,
					       current_ptr, total_ie_len);
			}
			break;
		case WLAN_EID_RSN:
			bss_entry->bcn_rsn_ie =
				(struct element *)current_ptr;
			bss_entry->rsn_offset =
				(u16)(current_ptr - bss_entry->beacon_buf);
			break;
		case WLAN_EID_RSNX:
			bss_entry->bcn_rsnx_ie =
				(struct element *)current_ptr;
			bss_entry->rsnx_offset =
				(u16)(current_ptr - bss_entry->beacon_buf);
			break;
		case WLAN_EID_HT_CAPABILITY:
			bss_entry->bcn_ht_cap =
				(struct ieee80211_ht_cap *)(current_ptr +
							    elem_size);
			bss_entry->ht_cap_offset =
				(u16)(current_ptr + elem_size -
				      bss_entry->beacon_buf);
			break;
		case WLAN_EID_HT_OPERATION:
			bss_entry->bcn_ht_oper =
				(struct ieee80211_ht_operation *)(current_ptr +
								  elem_size);
			bss_entry->ht_info_offset =
				(u16)(current_ptr + elem_size -
				      bss_entry->beacon_buf);
			break;
		case WLAN_EID_VHT_CAPABILITY:
			bss_entry->disable_11ac = false;
			bss_entry->bcn_vht_cap = (void *)(current_ptr +
							  elem_size);
			bss_entry->vht_cap_offset =
				(u16)((u8 *)bss_entry->bcn_vht_cap -
				      bss_entry->beacon_buf);
			break;
		case WLAN_EID_VHT_OPERATION:
			bss_entry->bcn_vht_oper =
				(void *)(current_ptr + elem_size);
			bss_entry->vht_info_offset =
				(u16)((u8 *)bss_entry->bcn_vht_oper -
				      bss_entry->beacon_buf);
			break;
		case WLAN_EID_BSS_COEX_2040:
			bss_entry->bcn_bss_co_2040 = current_ptr;
			bss_entry->bss_co_2040_offset =
				(u16)(current_ptr - bss_entry->beacon_buf);
			break;
		case WLAN_EID_EXT_CAPABILITY:
			bss_entry->bcn_ext_cap = current_ptr;
			bss_entry->ext_cap_offset =
				(u16)(current_ptr - bss_entry->beacon_buf);
			break;
		case WLAN_EID_OPMODE_NOTIF:
			bss_entry->oper_mode = (void *)current_ptr;
			bss_entry->oper_mode_offset =
				(u16)(current_ptr - bss_entry->beacon_buf);
			break;
		case WLAN_EID_EXTENSION:
			if (!element_len)
				return -EINVAL;

			elem = (struct element *)current_ptr;

			switch (elem->data[0]) {
			case WLAN_EID_EXT_HE_CAPABILITY:
				bss_entry->disable_11ax = false;
				bss_entry->bcn_he_cap =
					(void *)(current_ptr + elem_size + 1);
				bss_entry->he_cap_offset =
					(u16)((u8 *)bss_entry->bcn_he_cap -
					      bss_entry->beacon_buf);
				break;
			case WLAN_EID_EXT_HE_OPERATION:
				bss_entry->bcn_he_oper =
					(void *)(current_ptr + elem_size + 1);
				bss_entry->he_info_offset =
					(u16)((u8 *)bss_entry->bcn_he_oper -
					      bss_entry->beacon_buf);
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}

		current_ptr += total_ie_len;
		bytes_left -= total_ie_len;

	}	/* while (bytes_left > 2) */
	return 0;
}

/* Convert the radio-type scan parameter to the join command's band config. */
static u8
nxpwifi_radio_type_to_band(u8 radio_type)
{
	switch (radio_type) {
	case HOST_SCAN_RADIO_TYPE_A:
		return BAND_A;
	case HOST_SCAN_RADIO_TYPE_BG:
	default:
		return BAND_G;
	}
}

/* Internal helper to start a scan using the given configuration. */
int nxpwifi_scan_networks(struct nxpwifi_private *priv,
			  const struct nxpwifi_user_scan_cfg *user_scan_in)
{
	int ret;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmd_node;
	union nxpwifi_scan_cmd_config_tlv *scan_cfg_out;
	struct nxpwifi_ie_types_chan_list_param_set *chan_list_out;
	struct nxpwifi_chan_scan_param_set *scan_chan_list;
	u8 filtered_scan;
	u8 scan_current_chan_only;
	u8 max_chan_per_scan;

	if (adapter->scan_processing) {
		nxpwifi_dbg(adapter, WARN,
			    "cmd: Scan already in process...\n");
		return -EBUSY;
	}

	if (priv->scan_block) {
		nxpwifi_dbg(adapter, WARN,
			    "cmd: Scan is blocked during association...\n");
		return -EBUSY;
	}

	if (test_bit(NXPWIFI_SURPRISE_REMOVED, &adapter->work_flags) ||
	    test_bit(NXPWIFI_IS_CMD_TIMEDOUT, &adapter->work_flags)) {
		nxpwifi_dbg(adapter, ERROR,
			    "Ignore scan. Card removed or firmware in bad state\n");
		return -EPERM;
	}

	spin_lock_bh(&adapter->nxpwifi_cmd_lock);
	adapter->scan_processing = true;
	spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

	scan_cfg_out = kzalloc_obj(union nxpwifi_scan_cmd_config_tlv);
	if (!scan_cfg_out) {
		ret = -ENOMEM;
		goto done;
	}

	scan_chan_list = kzalloc_objs(struct nxpwifi_chan_scan_param_set,
				      NXPWIFI_USER_SCAN_CHAN_MAX);
	if (!scan_chan_list) {
		kfree(scan_cfg_out);
		ret = -ENOMEM;
		goto done;
	}

	nxpwifi_config_scan(priv, user_scan_in, &scan_cfg_out->config,
			    &chan_list_out, scan_chan_list, &max_chan_per_scan,
			    &filtered_scan, &scan_current_chan_only);

	ret = nxpwifi_scan_channel_list(priv, max_chan_per_scan, filtered_scan,
					&scan_cfg_out->config, chan_list_out,
					scan_chan_list);

	/* Get scan command from scan_pending_q and put to cmd_pending_q */
	if (!ret) {
		spin_lock_bh(&adapter->scan_pending_q_lock);
		if (!list_empty(&adapter->scan_pending_q)) {
			cmd_node = list_first_entry(&adapter->scan_pending_q,
						    struct cmd_ctrl_node, list);
			list_del(&cmd_node->list);
			spin_unlock_bh(&adapter->scan_pending_q_lock);
			nxpwifi_insert_cmd_to_pending_q(adapter, cmd_node);
			nxpwifi_queue_work(adapter, &adapter->main_work);

			/* Perform internal scan synchronously */
			if (!priv->scan_request) {
				nxpwifi_dbg(adapter, INFO,
					    "wait internal scan\n");
				nxpwifi_wait_queue_complete(adapter, cmd_node);
			}
		} else {
			spin_unlock_bh(&adapter->scan_pending_q_lock);
		}
	}

	kfree(scan_cfg_out);
	kfree(scan_chan_list);
done:
	if (ret) {
		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->scan_processing = false;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
	}
	return ret;
}

/*
 * Build the firmware scan command from the given configuration, including
 * fixed fields and TLVs, and set the command ID, size, and endianness.
 */
int nxpwifi_cmd_802_11_scan(struct host_cmd_ds_command *cmd,
			    struct nxpwifi_scan_cmd_config *scan_cfg)
{
	struct host_cmd_ds_802_11_scan *scan_cmd = &cmd->params.scan;

	/* Set fixed field variables in scan command */
	scan_cmd->bss_mode = scan_cfg->bss_mode;
	memcpy(scan_cmd->bssid, scan_cfg->specific_bssid,
	       sizeof(scan_cmd->bssid));
	memcpy(scan_cmd->tlv_buffer, scan_cfg->tlv_buf, scan_cfg->tlv_buf_len);

	cmd->command = cpu_to_le16(HOST_CMD_802_11_SCAN);

	/* Size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16((u16)(sizeof(scan_cmd->bss_mode)
					  + sizeof(scan_cmd->bssid)
					  + scan_cfg->tlv_buf_len + S_DS_GEN));

	return 0;
}

/* Check compatibility of the requested network with current driver settings. */
int nxpwifi_check_network_compatibility(struct nxpwifi_private *priv,
					struct nxpwifi_bssdescriptor *bss_desc)
{
	int ret = 0;

	if (!bss_desc)
		return -EINVAL;

	if ((nxpwifi_get_cfp(priv, (u8)bss_desc->bss_band,
			     (u16)bss_desc->channel, 0))) {
		switch (priv->bss_mode) {
		case NL80211_IFTYPE_STATION:
			ret = nxpwifi_is_network_compatible(priv, bss_desc,
							    priv->bss_mode);
			if (ret)
				nxpwifi_dbg(priv->adapter, ERROR,
					    "Incompatible network settings\n");
			break;
		default:
			ret = 0;
		}
	}

	return ret;
}

/* Check if the SSID length is zero or all bytes are zero. */
static bool nxpwifi_is_hidden_ssid(struct cfg80211_ssid *ssid)
{
	int idx;

	for (idx = 0; idx < ssid->ssid_len; idx++) {
		if (ssid->ssid[idx])
			return false;
	}

	return true;
}

/* Find hidden SSIDs on passive channels and save those channels for active scan. */
static int nxpwifi_save_hidden_ssid_channels(struct nxpwifi_private *priv,
					     struct cfg80211_bss *bss)
{
	struct nxpwifi_bssdescriptor *bss_desc;
	int ret;
	int chid;

	/* Allocate and fill new bss descriptor */
	bss_desc = kzalloc_obj(*bss_desc);
	if (!bss_desc)
		return -ENOMEM;

	ret = nxpwifi_fill_new_bss_desc(priv, bss, bss_desc);
	if (ret)
		goto done;

	if (nxpwifi_is_hidden_ssid(&bss_desc->ssid)) {
		nxpwifi_dbg(priv->adapter, INFO, "found hidden SSID\n");
		for (chid = 0 ; chid < NXPWIFI_USER_SCAN_CHAN_MAX; chid++) {
			if (priv->hidden_chan[chid].chan_number ==
			    bss->channel->hw_value)
				break;

			if (!priv->hidden_chan[chid].chan_number) {
				priv->hidden_chan[chid].chan_number =
					bss->channel->hw_value;
				priv->hidden_chan[chid].radio_type =
					bss->channel->band;
				priv->hidden_chan[chid].scan_type =
					NXPWIFI_SCAN_TYPE_ACTIVE;
				break;
			}
		}
	}

done:
	/* Free beacon_ie allocated by nxpwifi_fill_new_bss_desc(). */
	kfree(bss_desc->beacon_buf);
	kfree(bss_desc);
	return ret;
}

static int nxpwifi_update_curr_bss_params(struct nxpwifi_private *priv,
					  struct cfg80211_bss *bss)
{
	struct nxpwifi_bssdescriptor *bss_desc;
	int ret;

	/* Allocate and fill new bss descriptor */
	bss_desc = kzalloc_obj(*bss_desc);
	if (!bss_desc)
		return -ENOMEM;

	ret = nxpwifi_fill_new_bss_desc(priv, bss, bss_desc);
	if (ret)
		goto done;

	ret = nxpwifi_check_network_compatibility(priv, bss_desc);
	if (ret)
		goto done;

	spin_lock_bh(&priv->curr_bcn_buf_lock);
	/* Make a copy of current BSSID descriptor */
	memcpy(&priv->curr_bss_params.bss_descriptor, bss_desc,
	       sizeof(priv->curr_bss_params.bss_descriptor));

	/* beacon_ie will be copied to its own buffer in nxpwifi_save_curr_bcn(). */
	nxpwifi_save_curr_bcn(priv);
	spin_unlock_bh(&priv->curr_bcn_buf_lock);

done:
	/* Free beacon_ie allocated by nxpwifi_fill_new_bss_desc(). */
	kfree(bss_desc->beacon_buf);
	kfree(bss_desc);
	return ret;
}

static int
nxpwifi_parse_single_response_buf(struct nxpwifi_private *priv, u8 **bss_info,
				  u32 *bytes_left, u64 fw_tsf, const u8 *radio_type,
				  bool ext_scan, s32 rssi_val)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct nxpwifi_chan_freq_power *cfp;
	struct cfg80211_bss *bss;
	u8 bssid[ETH_ALEN];
	s32 rssi;
	const u8 *ie_buf;
	size_t ie_len;
	u16 channel = 0;
	u16 beacon_size = 0;
	u32 curr_bcn_bytes;
	u32 freq;
	u16 beacon_period;
	u16 cap_info_bitmap;
	u8 *current_ptr;
	u64 timestamp;
	struct nxpwifi_fixed_bcn_param *bcn_param;
	struct nxpwifi_bss_priv *bss_priv;

	if (*bytes_left >= sizeof(beacon_size)) {
		/* Extract & convert beacon size from command buffer */
		beacon_size = get_unaligned_le16((*bss_info));
		*bytes_left -= sizeof(beacon_size);
		*bss_info += sizeof(beacon_size);
	}

	if (!beacon_size || beacon_size > *bytes_left) {
		*bss_info += *bytes_left;
		*bytes_left = 0;
		return -EINVAL;
	}

	/*
	 * Initialize the current working beacon pointer for this BSS
	 * iteration
	 */
	current_ptr = *bss_info;

	/* Advance the return beacon pointer past the current beacon */
	*bss_info += beacon_size;
	*bytes_left -= beacon_size;

	curr_bcn_bytes = beacon_size;

	/*
	 * First 5 fields are bssid, RSSI(for legacy scan only),
	 * time stamp, beacon interval, and capability information
	 */
	if (curr_bcn_bytes < ETH_ALEN + sizeof(u8) +
	    sizeof(struct nxpwifi_fixed_bcn_param)) {
		nxpwifi_dbg(adapter, ERROR,
			    "InterpretIE: not enough bytes left\n");
		return -EINVAL;
	}

	memcpy(bssid, current_ptr, ETH_ALEN);
	current_ptr += ETH_ALEN;
	curr_bcn_bytes -= ETH_ALEN;

	if (!ext_scan) {
		rssi = (s32)*current_ptr;
		rssi = (-rssi) * 100;		/* Convert dBm to mBm */
		current_ptr += sizeof(u8);
		curr_bcn_bytes -= sizeof(u8);
		nxpwifi_dbg(adapter, INFO,
			    "info: InterpretIE: RSSI=%d\n", rssi);
	} else {
		rssi = rssi_val;
	}

	bcn_param = (struct nxpwifi_fixed_bcn_param *)current_ptr;
	current_ptr += sizeof(*bcn_param);
	curr_bcn_bytes -= sizeof(*bcn_param);

	timestamp = le64_to_cpu(bcn_param->timestamp);
	beacon_period = le16_to_cpu(bcn_param->beacon_period);

	cap_info_bitmap = le16_to_cpu(bcn_param->cap_info_bitmap);
	nxpwifi_dbg(adapter, INFO,
		    "info: InterpretIE: capabilities=0x%X\n",
		    cap_info_bitmap);

	/* Rest of the current buffer are element's */
	ie_buf = current_ptr;
	ie_len = curr_bcn_bytes;
	nxpwifi_dbg(adapter, INFO,
		    "info: InterpretIE: IELength for this AP = %d\n",
		    curr_bcn_bytes);

	while (curr_bcn_bytes >= sizeof(struct element)) {
		u8 element_id, element_len;

		element_id = *current_ptr;
		element_len = *(current_ptr + 1);
		if (curr_bcn_bytes < element_len +
				sizeof(struct element)) {
			nxpwifi_dbg(adapter, ERROR,
				    "%s: bytes left < element length\n", __func__);
			return -EFAULT;
		}
		if (element_id == WLAN_EID_DS_PARAMS) {
			channel = *(current_ptr +
				    sizeof(struct element));
			break;
		}

		current_ptr += element_len + sizeof(struct element);
		curr_bcn_bytes -= element_len +
					sizeof(struct element);
	}

	if (channel) {
		struct ieee80211_channel *chan;
		struct nxpwifi_bssdescriptor *bss_desc;
		u8 band;

		/* Skip entry if on csa closed channel */
		if (channel == priv->csa_chan) {
			nxpwifi_dbg(adapter, WARN,
				    "Dropping entry on csa closed channel\n");
			return 0;
		}

		band = BAND_G;
		if (radio_type)
			band = nxpwifi_radio_type_to_band(*radio_type &
							  (BIT(0) | BIT(1)));

		cfp = nxpwifi_get_cfp(priv, band, channel, 0);

		freq = cfp ? cfp->freq : 0;

		chan = ieee80211_get_channel(priv->wdev.wiphy, freq);

		if (chan && !(chan->flags & IEEE80211_CHAN_DISABLED)) {
			bss = cfg80211_inform_bss(priv->wdev.wiphy, chan,
						  CFG80211_BSS_FTYPE_UNKNOWN,
						  bssid, timestamp,
						  cap_info_bitmap,
						  beacon_period,
						  ie_buf, ie_len, rssi,
						  GFP_ATOMIC);
			if (bss) {
				bss_priv = (struct nxpwifi_bss_priv *)bss->priv;
				bss_priv->band = band;
				bss_priv->fw_tsf = fw_tsf;
				bss_desc =
					&priv->curr_bss_params.bss_descriptor;
				if (priv->media_connected &&
				    !memcmp(bssid, bss_desc->mac_address,
					    ETH_ALEN))
					nxpwifi_update_curr_bss_params(priv,
								       bss);

				if ((chan->flags & IEEE80211_CHAN_RADAR) ||
				    (chan->flags & IEEE80211_CHAN_NO_IR)) {
					nxpwifi_dbg(adapter, INFO,
						    "radar or passive channel %d\n",
						    channel);
					nxpwifi_save_hidden_ssid_channels(priv,
									  bss);
				}

				cfg80211_put_bss(priv->wdev.wiphy, bss);
			}
		}
	} else {
		nxpwifi_dbg(adapter, WARN, "missing BSS channel element\n");
	}

	return 0;
}

static void nxpwifi_complete_scan(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	adapter->survey_idx = 0;
	if (adapter->curr_cmd->wait_q_enabled) {
		adapter->cmd_wait_q.status = 0;
		if (!priv->scan_request) {
			nxpwifi_dbg(adapter, INFO,
				    "complete internal scan\n");
			nxpwifi_complete_cmd(adapter, adapter->curr_cmd);
		}
	}
}

/* Find hidden SSIDs on passive channels and run active scans on them. */
static int
nxpwifi_active_scan_req_for_passive_chan(struct nxpwifi_private *priv)
{
	int ret;
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 id = 0;
	struct nxpwifi_user_scan_cfg  *user_scan_cfg;

	if (adapter->active_scan_triggered || !priv->scan_request ||
	    priv->scan_aborting) {
		adapter->active_scan_triggered = false;
		return 0;
	}

	if (!priv->hidden_chan[0].chan_number) {
		nxpwifi_dbg(adapter, INFO, "No BSS with hidden SSID found on DFS channels\n");
		return 0;
	}
	user_scan_cfg = kzalloc_obj(*user_scan_cfg);

	if (!user_scan_cfg)
		return -ENOMEM;

	for (id = 0; id < NXPWIFI_USER_SCAN_CHAN_MAX; id++) {
		if (!priv->hidden_chan[id].chan_number)
			break;
		memcpy(&user_scan_cfg->chan_list[id],
		       &priv->hidden_chan[id],
		       sizeof(struct nxpwifi_user_scan_chan));
	}

	adapter->active_scan_triggered = true;
	if (priv->scan_request->flags & NL80211_SCAN_FLAG_RANDOM_ADDR)
		ether_addr_copy(user_scan_cfg->random_mac,
				priv->scan_request->mac_addr);
	user_scan_cfg->num_ssids = priv->scan_request->n_ssids;
	user_scan_cfg->ssid_list = priv->scan_request->ssids;

	ret = nxpwifi_scan_networks(priv, user_scan_cfg);
	kfree(user_scan_cfg);

	memset(&priv->hidden_chan, 0, sizeof(priv->hidden_chan));

	if (ret)
		nxpwifi_dbg(adapter, ERROR, "scan failed: %d\n", ret);

	return ret;
}

static void nxpwifi_check_next_scan_command(struct nxpwifi_private *priv)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct cmd_ctrl_node *cmd_node;

	spin_lock_bh(&adapter->scan_pending_q_lock);
	if (list_empty(&adapter->scan_pending_q)) {
		spin_unlock_bh(&adapter->scan_pending_q_lock);

		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->scan_processing = false;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

		nxpwifi_active_scan_req_for_passive_chan(priv);

		if (!adapter->ext_scan)
			nxpwifi_complete_scan(priv);

		if (priv->scan_request) {
			struct cfg80211_scan_info info = {
				.aborted = false,
			};

			nxpwifi_dbg(adapter, INFO,
				    "info: notifying scan done\n");
			cfg80211_scan_done(priv->scan_request, &info);
			priv->scan_request = NULL;
			priv->scan_aborting = false;
		} else {
			priv->scan_aborting = false;
			nxpwifi_dbg(adapter, INFO,
				    "info: scan already aborted\n");
		}
	} else if ((priv->scan_aborting && !priv->scan_request) ||
		   priv->scan_block) {
		spin_unlock_bh(&adapter->scan_pending_q_lock);

		nxpwifi_cancel_pending_scan_cmd(adapter);

		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->scan_processing = false;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);

		if (!adapter->active_scan_triggered) {
			if (priv->scan_request) {
				struct cfg80211_scan_info info = {
					.aborted = true,
				};

				nxpwifi_dbg(adapter, INFO,
					    "info: aborting scan\n");
				cfg80211_scan_done(priv->scan_request, &info);
				priv->scan_request = NULL;
				priv->scan_aborting = false;
			} else {
				priv->scan_aborting = false;
				nxpwifi_dbg(adapter, INFO,
					    "info: scan already aborted\n");
			}
		}
	} else {
		/* Move a scan command from scan_pending_q to cmd_pending_q. */
		cmd_node = list_first_entry(&adapter->scan_pending_q,
					    struct cmd_ctrl_node, list);
		list_del(&cmd_node->list);
		spin_unlock_bh(&adapter->scan_pending_q_lock);
		nxpwifi_insert_cmd_to_pending_q(adapter, cmd_node);
	}
}

void nxpwifi_cancel_scan(struct nxpwifi_adapter *adapter)
{
	struct nxpwifi_private *priv;
	int i;

	nxpwifi_cancel_pending_scan_cmd(adapter);

	if (adapter->scan_processing) {
		spin_lock_bh(&adapter->nxpwifi_cmd_lock);
		adapter->scan_processing = false;
		spin_unlock_bh(&adapter->nxpwifi_cmd_lock);
		for (i = 0; i < adapter->priv_num; i++) {
			priv = adapter->priv[i];
			if (priv->scan_request) {
				struct cfg80211_scan_info info = {
					.aborted = true,
				};

				nxpwifi_dbg(adapter, INFO,
					    "info: aborting scan\n");
				cfg80211_scan_done(priv->scan_request, &info);
				priv->scan_request = NULL;
				priv->scan_aborting = false;
			}
		}
	}
}

/*
 * Handle the scan command response.
 *
 * The scan response buffer has the following layout:
 *
 *   -------------------------------------------------------------
 *   | Header (4 * t_u16): standard command response header       |
 *   -------------------------------------------------------------
 *   | BufSize (t_u16): size of the BSS description data         |
 *   -------------------------------------------------------------
 *   | NumOfSet (t_u8): number of returned BSS descriptions      |
 *   -------------------------------------------------------------
 *   | BSS description data (variable, size = BufSize)           |
 *   -------------------------------------------------------------
 *   | TLV data (variable, size = cmd_size - fixed fields)       |
 *   -------------------------------------------------------------
 */
int nxpwifi_ret_802_11_scan(struct nxpwifi_private *priv,
			    struct host_cmd_ds_command *resp)
{
	int ret = 0;
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_scan_rsp *scan_rsp;
	u8 *tlv_data;
	const struct nxpwifi_ie_types_tsf_timestamp *tsf_tlv;
	u8 *bss_info;
	u32 scan_resp_size;
	u32 bytes_left;
	u32 idx;
	u32 tlv_buf_size;
	const struct nxpwifi_ie_types_chan_band_list_param_set *chan_band_tlv;
	const struct chan_band_param_set *chan_band;
	u8 is_bgscan_resp;
	__le64 fw_tsf = 0;
	const u8 *radio_type;
	struct cfg80211_wowlan_nd_match *pmatch;
	struct cfg80211_sched_scan_request *nd_config = NULL;

	is_bgscan_resp = (le16_to_cpu(resp->command)
			  == HOST_CMD_802_11_BG_SCAN_QUERY);
	if (is_bgscan_resp)
		scan_rsp = &resp->params.bg_scan_query_resp.scan_resp;
	else
		scan_rsp = &resp->params.scan_resp;

	if (scan_rsp->number_of_sets > NXPWIFI_MAX_AP) {
		nxpwifi_dbg(adapter, ERROR,
			    "SCAN_RESP: too many AP returned (%d)\n",
			    scan_rsp->number_of_sets);
		ret = -EINVAL;
		goto check_next_scan;
	}

	/* Check csa channel expiry before parsing scan response */
	nxpwifi_11h_get_csa_closed_channel(priv);

	bytes_left = le16_to_cpu(scan_rsp->bss_descript_size);
	nxpwifi_dbg(adapter, INFO,
		    "info: SCAN_RESP: bss_descript_size %d\n",
		    bytes_left);

	scan_resp_size = le16_to_cpu(resp->size);

	nxpwifi_dbg(adapter, INFO,
		    "info: SCAN_RESP: returned %d APs before parsing\n",
		    scan_rsp->number_of_sets);

	bss_info = scan_rsp->bss_desc_and_tlv_buffer;

	/*
	 * TLV buffer size = scan_resp_size minus the fixed fields, BSS
	 * description data, and the command response header (S_DS_GEN).
	 */
	tlv_buf_size = scan_resp_size - (bytes_left
					 + sizeof(scan_rsp->bss_descript_size)
					 + sizeof(scan_rsp->number_of_sets)
					 + S_DS_GEN);

	tlv_data = (scan_rsp->bss_desc_and_tlv_buffer +
					  bytes_left);

	/* Find timestamp TLV */
	{
		const struct nxpwifi_tlv *t;

		t = nxpwifi_find_tlv(TLV_TYPE_TSFTIMESTAMP, tlv_data, tlv_buf_size);
		tsf_tlv = (const struct nxpwifi_ie_types_tsf_timestamp *)t;
	}

	/* Find channel-band list TLV */
	{
		const struct nxpwifi_tlv *t;

		t = nxpwifi_find_tlv(TLV_TYPE_CHANNELBANDLIST, tlv_data,
				     tlv_buf_size);
		chan_band_tlv =
		    (const struct nxpwifi_ie_types_chan_band_list_param_set *)t;
	}

#ifdef CONFIG_PM
	if (priv->wdev.wiphy->wowlan_config)
		nd_config = priv->wdev.wiphy->wowlan_config->nd_config;
#endif

	if (nd_config) {
		adapter->nd_info =
			kzalloc_flex(*adapter->nd_info, matches,
				     scan_rsp->number_of_sets, GFP_ATOMIC);

		if (adapter->nd_info)
			adapter->nd_info->n_matches = scan_rsp->number_of_sets;
	}

	for (idx = 0; idx < scan_rsp->number_of_sets && bytes_left; idx++) {
		/*
		 * If a TSF TLV is present, save its TSF value in fw_tsf. This
		 * is the firmware TSF at the time the beacon or probe response
		 * was received.
		 */
		if (tsf_tlv)
			memcpy(&fw_tsf, &tsf_tlv->tsf_data[idx * TSF_DATA_SIZE],
			       sizeof(fw_tsf));

		if (chan_band_tlv) {
			chan_band = &chan_band_tlv->chan_band_param[idx];
			radio_type = &chan_band->radio_type;
		} else {
			radio_type = NULL;
		}

		if (chan_band_tlv && adapter->nd_info) {
			adapter->nd_info->matches[idx] =
				kzalloc(sizeof(*pmatch) + sizeof(u32),
					GFP_ATOMIC);

			pmatch = adapter->nd_info->matches[idx];

			if (pmatch) {
				pmatch->n_channels = 1;
				pmatch->channels[0] = chan_band->chan_number;
			}
		}

		ret = nxpwifi_parse_single_response_buf(priv, &bss_info,
							&bytes_left,
							le64_to_cpu(fw_tsf),
							radio_type, false, 0);
		if (ret)
			goto check_next_scan;
	}

check_next_scan:
	nxpwifi_check_next_scan_command(priv);
	return ret;
}

/*
 * Prepare the extended scan command using the provided scan configuration
 * and build the structure to be sent to firmware.
 */
int nxpwifi_cmd_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *cmd,
				void *data_buf)
{
	struct host_cmd_ds_802_11_scan_ext *ext_scan = &cmd->params.ext_scan;
	struct nxpwifi_scan_cmd_config *scan_cfg = data_buf;

	memcpy(ext_scan->tlv_buffer, scan_cfg->tlv_buf, scan_cfg->tlv_buf_len);

	cmd->command = cpu_to_le16(HOST_CMD_802_11_SCAN_EXT);

	/* Size is equal to the sizeof(fixed portions) + the TLV len + header */
	cmd->size = cpu_to_le16((u16)(sizeof(ext_scan->reserved)
				      + scan_cfg->tlv_buf_len + S_DS_GEN));

	return 0;
}

/* Prepare the background scan config command to send to firmware. */
int nxpwifi_cmd_802_11_bg_scan_config(struct nxpwifi_private *priv,
				      struct host_cmd_ds_command *cmd,
				      void *data_buf)
{
	struct host_cmd_ds_802_11_bg_scan_config *bgscan_config =
					&cmd->params.bg_scan_config;
	struct nxpwifi_bg_scan_cfg *bgscan_cfg_in = data_buf;
	u8 *tlv_pos = bgscan_config->tlv;
	u8 num_probes;
	u32 ssid_len, chan_idx, scan_time, scan_type, scan_dur, chan_num;
	int i;
	struct nxpwifi_ie_types_num_probes *num_probes_tlv;
	struct nxpwifi_ie_types_repeat_count *repeat_count_tlv;
	struct nxpwifi_ie_types_min_rssi_threshold *rssi_threshold_tlv;
	struct nxpwifi_ie_types_bgscan_start_later *start_later_tlv;
	struct nxpwifi_ie_types_wildcard_ssid_params *wildcard_ssid_tlv;
	struct nxpwifi_ie_types_chan_list_param_set *tlv_l;
	struct nxpwifi_chan_scan_param_set *temp_chan;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_BG_SCAN_CONFIG);
	cmd->size = cpu_to_le16(sizeof(*bgscan_config) + S_DS_GEN);

	bgscan_config->action = cpu_to_le16(bgscan_cfg_in->action);
	bgscan_config->enable = bgscan_cfg_in->enable;
	bgscan_config->bss_type = bgscan_cfg_in->bss_type;
	bgscan_config->scan_interval =
		cpu_to_le32(bgscan_cfg_in->scan_interval);
	bgscan_config->report_condition =
		cpu_to_le32(bgscan_cfg_in->report_condition);

	/*  stop sched scan  */
	if (!bgscan_config->enable)
		return 0;

	bgscan_config->chan_per_scan = bgscan_cfg_in->chan_per_scan;

	num_probes = (bgscan_cfg_in->num_probes ?
		bgscan_cfg_in->num_probes : priv->adapter->scan_probes);

	if (num_probes) {
		num_probes_tlv = (struct nxpwifi_ie_types_num_probes *)tlv_pos;
		num_probes_tlv->header.type = cpu_to_le16(TLV_TYPE_NUMPROBES);
		num_probes_tlv->header.len =
			cpu_to_le16(sizeof(num_probes_tlv->num_probes));
		num_probes_tlv->num_probes = cpu_to_le16((u16)num_probes);

		tlv_pos += sizeof(num_probes_tlv->header) +
			le16_to_cpu(num_probes_tlv->header.len);
	}

	if (bgscan_cfg_in->repeat_count) {
		repeat_count_tlv =
			(struct nxpwifi_ie_types_repeat_count *)tlv_pos;
		repeat_count_tlv->header.type =
			cpu_to_le16(TLV_TYPE_REPEAT_COUNT);
		repeat_count_tlv->header.len =
			cpu_to_le16(sizeof(repeat_count_tlv->repeat_count));
		repeat_count_tlv->repeat_count =
			cpu_to_le16(bgscan_cfg_in->repeat_count);

		tlv_pos += sizeof(repeat_count_tlv->header) +
			le16_to_cpu(repeat_count_tlv->header.len);
	}

	if (bgscan_cfg_in->rssi_threshold) {
		rssi_threshold_tlv =
			(struct nxpwifi_ie_types_min_rssi_threshold *)tlv_pos;
		rssi_threshold_tlv->header.type =
			cpu_to_le16(TLV_TYPE_RSSI_LOW);
		rssi_threshold_tlv->header.len =
			cpu_to_le16(sizeof(rssi_threshold_tlv->rssi_threshold));
		rssi_threshold_tlv->rssi_threshold =
			cpu_to_le16(bgscan_cfg_in->rssi_threshold);

		tlv_pos += sizeof(rssi_threshold_tlv->header) +
			le16_to_cpu(rssi_threshold_tlv->header.len);
	}

	for (i = 0; i < bgscan_cfg_in->num_ssids; i++) {
		ssid_len = bgscan_cfg_in->ssid_list[i].ssid.ssid_len;

		wildcard_ssid_tlv =
			(struct nxpwifi_ie_types_wildcard_ssid_params *)tlv_pos;
		wildcard_ssid_tlv->header.type =
				cpu_to_le16(TLV_TYPE_WILDCARDSSID);
		wildcard_ssid_tlv->header.len =
			cpu_to_le16((u16)(ssid_len + sizeof(u8)));

		/*
		 * max_ssid_length = 0 tells firmware to scan only for the given
		 * SSID. max_ssid_length = IEEE80211_MAX_SSID_LEN triggers a
		 * wildcard scan.
		 */
		if (ssid_len)
			wildcard_ssid_tlv->max_ssid_length = 0;
		else
			wildcard_ssid_tlv->max_ssid_length =
						IEEE80211_MAX_SSID_LEN;

		memcpy(wildcard_ssid_tlv->ssid,
		       bgscan_cfg_in->ssid_list[i].ssid.ssid, ssid_len);

		tlv_pos += (sizeof(wildcard_ssid_tlv->header) +
			le16_to_cpu(wildcard_ssid_tlv->header.len));
	}

	tlv_l = (struct nxpwifi_ie_types_chan_list_param_set *)tlv_pos;

	if (bgscan_cfg_in->chan_list[0].chan_number) {
		nxpwifi_dbg(priv->adapter, INFO, "info: bgscan: Using supplied channel list\n");

		tlv_l->header.type = cpu_to_le16(TLV_TYPE_CHANLIST);

		for (chan_idx = 0;
		     chan_idx < NXPWIFI_BG_SCAN_CHAN_MAX &&
		     bgscan_cfg_in->chan_list[chan_idx].chan_number;
		     chan_idx++) {
			temp_chan = &tlv_l->chan_scan_param[chan_idx];

			/* Increment the TLV header length by size appended */
			le16_unaligned_add_cpu(&tlv_l->header.len,
					       sizeof(*tlv_l->chan_scan_param));

			temp_chan->chan_number =
				bgscan_cfg_in->chan_list[chan_idx].chan_number;
			temp_chan->band_cfg =
				bgscan_cfg_in->chan_list[chan_idx].radio_type;

			scan_type =
				bgscan_cfg_in->chan_list[chan_idx].scan_type;

			if (scan_type == NXPWIFI_SCAN_TYPE_PASSIVE)
				temp_chan->chan_scan_mode_bmap |=
					NXPWIFI_PASSIVE_SCAN;
			else
				temp_chan->chan_scan_mode_bmap &=
					~NXPWIFI_PASSIVE_SCAN;

			scan_time = bgscan_cfg_in->chan_list[chan_idx].scan_time;

			if (scan_time) {
				scan_dur = (u16)scan_time;
			} else {
				scan_dur = (scan_type ==
					    NXPWIFI_SCAN_TYPE_PASSIVE) ?
					    priv->adapter->passive_scan_time :
					    priv->adapter->specific_scan_time;
			}

			temp_chan->min_scan_time = cpu_to_le16(scan_dur);
			temp_chan->max_scan_time = cpu_to_le16(scan_dur);
		}
	} else {
		nxpwifi_dbg(priv->adapter, INFO,
			    "info: bgscan: Creating full region channel list\n");
		chan_num =
			nxpwifi_bgscan_create_channel_list
			(priv, bgscan_cfg_in,
			 tlv_l->chan_scan_param);
		le16_unaligned_add_cpu(&tlv_l->header.len,
				       chan_num *
				       sizeof(*tlv_l->chan_scan_param));
	}

	tlv_pos += (sizeof(tlv_l->header)
			+ le16_to_cpu(tlv_l->header.len));

	if (bgscan_cfg_in->start_later) {
		start_later_tlv =
			(struct nxpwifi_ie_types_bgscan_start_later *)tlv_pos;
		start_later_tlv->header.type =
			cpu_to_le16(TLV_TYPE_BGSCAN_START_LATER);
		start_later_tlv->header.len =
			cpu_to_le16(sizeof(start_later_tlv->start_later));
		start_later_tlv->start_later =
			cpu_to_le16(bgscan_cfg_in->start_later);

		tlv_pos += sizeof(start_later_tlv->header) +
			le16_to_cpu(start_later_tlv->header.len);
	}

	/* Append vendor specific element TLV */
	nxpwifi_cmd_append_vsie_tlv(priv, NXPWIFI_VSIE_MASK_BGSCAN, &tlv_pos);

	le16_unaligned_add_cpu(&cmd->size, tlv_pos - bgscan_config->tlv);

	return 0;
}

int nxpwifi_stop_bg_scan(struct nxpwifi_private *priv)
{
	struct nxpwifi_bg_scan_cfg *bgscan_cfg;
	int ret;

	if (!priv->sched_scanning) {
		nxpwifi_dbg(priv->adapter, MSG, "bgscan already stopped!\n");
		return 0;
	}

	bgscan_cfg = kzalloc_obj(*bgscan_cfg);
	if (!bgscan_cfg)
		return -ENOMEM;

	bgscan_cfg->bss_type = NXPWIFI_BSS_MODE_INFRA;
	bgscan_cfg->action = NXPWIFI_BGSCAN_ACT_SET;
	bgscan_cfg->enable = false;

	ret = nxpwifi_send_cmd(priv, HOST_CMD_802_11_BG_SCAN_CONFIG,
			       HOST_ACT_GEN_SET, 0, bgscan_cfg, true);
	if (!ret)
		priv->sched_scanning = false;

	kfree(bgscan_cfg);
	return ret;
}

static void
nxpwifi_update_chan_statistics(struct nxpwifi_private *priv,
			       struct nxpwifi_ietypes_chanstats *tlv_stat)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 i, num_chan;
	struct nxpwifi_fw_chan_stats *fw_chan_stats;
	struct nxpwifi_chan_stats chan_stats;

	fw_chan_stats = (void *)((u8 *)tlv_stat +
			      sizeof(struct nxpwifi_ie_types_header));
	num_chan = le16_to_cpu(tlv_stat->header.len) /
					      sizeof(struct nxpwifi_chan_stats);

	for (i = 0 ; i < num_chan; i++) {
		if (adapter->survey_idx >= adapter->num_in_chan_stats) {
			nxpwifi_dbg(adapter, WARN,
				    "FW reported too many channel results (max %d)\n",
				    adapter->num_in_chan_stats);
			return;
		}
		chan_stats.chan_num = fw_chan_stats->chan_num;
		chan_stats.bandcfg = fw_chan_stats->bandcfg;
		chan_stats.flags = fw_chan_stats->flags;
		chan_stats.noise = fw_chan_stats->noise;
		chan_stats.total_bss = le16_to_cpu(fw_chan_stats->total_bss);
		chan_stats.cca_scan_dur =
				       le16_to_cpu(fw_chan_stats->cca_scan_dur);
		chan_stats.cca_busy_dur =
				       le16_to_cpu(fw_chan_stats->cca_busy_dur);
		nxpwifi_dbg(adapter, INFO,
			    "chan=%d, noise=%d, total_network=%d scan_duration=%d, busy_duration=%d\n",
			    chan_stats.chan_num,
			    chan_stats.noise,
			    chan_stats.total_bss,
			    chan_stats.cca_scan_dur,
			    chan_stats.cca_busy_dur);
		memcpy(&adapter->chan_stats[adapter->survey_idx++], &chan_stats,
		       sizeof(struct nxpwifi_chan_stats));
		fw_chan_stats++;
	}
}

/* Handle the extended scan command response. */
int nxpwifi_ret_802_11_scan_ext(struct nxpwifi_private *priv,
				struct host_cmd_ds_command *resp)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	struct host_cmd_ds_802_11_scan_ext *ext_scan_resp;
	struct nxpwifi_ie_types_header *tlv;
	struct nxpwifi_ietypes_chanstats *tlv_stat;
	u16 buf_left, type, len;

	struct host_cmd_ds_command *cmd_ptr;
	struct cmd_ctrl_node *cmd_node;
	bool complete_scan = false;

	nxpwifi_dbg(adapter, INFO, "info: EXT scan returns successfully\n");

	ext_scan_resp = &resp->params.ext_scan;

	tlv = (void *)ext_scan_resp->tlv_buffer;
	buf_left = le16_to_cpu(resp->size) - (sizeof(*ext_scan_resp) + S_DS_GEN);

	while (buf_left >= sizeof(struct nxpwifi_ie_types_header)) {
		type = le16_to_cpu(tlv->type);
		len = le16_to_cpu(tlv->len);

		if (buf_left < (sizeof(struct nxpwifi_ie_types_header) + len)) {
			nxpwifi_dbg(adapter, ERROR,
				    "error processing scan response TLVs");
			break;
		}

		switch (type) {
		case TLV_TYPE_CHANNEL_STATS:
			tlv_stat = (void *)tlv;
			nxpwifi_update_chan_statistics(priv, tlv_stat);
			break;
		default:
			break;
		}

		buf_left -= len + sizeof(struct nxpwifi_ie_types_header);
		tlv = (void *)((u8 *)tlv + len +
			       sizeof(struct nxpwifi_ie_types_header));
	}

	spin_lock_bh(&adapter->cmd_pending_q_lock);
	spin_lock_bh(&adapter->scan_pending_q_lock);
	if (list_empty(&adapter->scan_pending_q)) {
		complete_scan = true;
		list_for_each_entry(cmd_node, &adapter->cmd_pending_q, list) {
			cmd_ptr = (void *)cmd_node->cmd_skb->data;
			if (le16_to_cpu(cmd_ptr->command) ==
			    HOST_CMD_802_11_SCAN_EXT) {
				nxpwifi_dbg(adapter, INFO,
					    "Scan pending in command pending list");
				complete_scan = false;
				break;
			}
		}
	}
	spin_unlock_bh(&adapter->scan_pending_q_lock);
	spin_unlock_bh(&adapter->cmd_pending_q_lock);

	if (complete_scan)
		nxpwifi_complete_scan(priv);

	return 0;
}

/*
 * Handle the extended scan report event: parse the results and notify
 * cfg80211.
 */
int nxpwifi_handle_event_ext_scan_report(struct nxpwifi_private *priv,
					 void *buf)
{
	int ret = 0;
	struct nxpwifi_adapter *adapter = priv->adapter;
	u8 *bss_info;
	u32 bytes_left, bytes_left_for_tlv, idx;
	u16 type, len;
	struct nxpwifi_ie_types_data *tlv;
	struct nxpwifi_ie_types_scan_rsp *scan_rsp_tlv;
	struct nxpwifi_ie_types_scan_inf *scan_info_tlv;
	u8 *radio_type;
	u64 fw_tsf = 0;
	s32 rssi = 0;
	struct nxpwifi_event_scan_result *event_scan = buf;
	u8 num_of_set = event_scan->num_of_set;
	u8 *scan_resp = buf + sizeof(struct nxpwifi_event_scan_result);
	u16 scan_resp_size = le16_to_cpu(event_scan->buf_size);

	if (num_of_set > NXPWIFI_MAX_AP) {
		nxpwifi_dbg(adapter, ERROR,
			    "EXT_SCAN: Invalid number of AP returned (%d)!!\n",
			    num_of_set);
		ret = -EINVAL;
		goto check_next_scan;
	}

	bytes_left = scan_resp_size;
	nxpwifi_dbg(adapter, INFO,
		    "EXT_SCAN: size %d, returned %d APs...",
		    scan_resp_size, num_of_set);
	nxpwifi_dbg_dump(adapter, CMD_D, "EXT_SCAN buffer:", buf,
			 scan_resp_size +
			 sizeof(struct nxpwifi_event_scan_result));

	tlv = (struct nxpwifi_ie_types_data *)scan_resp;

	for (idx = 0; idx < num_of_set && bytes_left; idx++) {
		type = le16_to_cpu(tlv->header.type);
		len = le16_to_cpu(tlv->header.len);
		if (bytes_left < sizeof(struct nxpwifi_ie_types_header) + len) {
			nxpwifi_dbg(adapter, ERROR,
				    "EXT_SCAN: Error bytes left < TLV length\n");
			break;
		}
		scan_rsp_tlv = NULL;
		scan_info_tlv = NULL;
		bytes_left_for_tlv = bytes_left;

		/*
		 * BSS response TLV with beacon or probe response buffer
		 * at the initial position of each descriptor
		 */
		if (type != TLV_TYPE_BSS_SCAN_RSP)
			break;

		bss_info = (u8 *)tlv;
		scan_rsp_tlv = (struct nxpwifi_ie_types_scan_rsp *)tlv;
		tlv = (struct nxpwifi_ie_types_data *)(tlv->data + len);
		bytes_left_for_tlv -=
			(len + sizeof(struct nxpwifi_ie_types_header));

		while (bytes_left_for_tlv >=
		       sizeof(struct nxpwifi_ie_types_header) &&
		       le16_to_cpu(tlv->header.type) != TLV_TYPE_BSS_SCAN_RSP) {
			type = le16_to_cpu(tlv->header.type);
			len = le16_to_cpu(tlv->header.len);
			if (bytes_left_for_tlv <
			    sizeof(struct nxpwifi_ie_types_header) + len) {
				nxpwifi_dbg(adapter, ERROR,
					    "EXT_SCAN: Error in processing TLV,\t"
					    "bytes left < TLV length\n");
				scan_rsp_tlv = NULL;
				bytes_left_for_tlv = 0;
				continue;
			}
			switch (type) {
			case TLV_TYPE_BSS_SCAN_INFO:
				scan_info_tlv =
					(struct nxpwifi_ie_types_scan_inf *)tlv;
				if (len !=
				 sizeof(struct nxpwifi_ie_types_scan_inf) -
				 sizeof(struct nxpwifi_ie_types_header)) {
					bytes_left_for_tlv = 0;
					continue;
				}
				break;
			default:
				break;
			}
			tlv = (struct nxpwifi_ie_types_data *)(tlv->data + len);
			bytes_left -=
				(len + sizeof(struct nxpwifi_ie_types_header));
			bytes_left_for_tlv -=
				(len + sizeof(struct nxpwifi_ie_types_header));
		}

		if (!scan_rsp_tlv)
			break;

		/*
		 * Advance pointer to the beacon buffer length and
		 * update the bytes count so that the function
		 * wlan_interpret_bss_desc_with_ie() can handle the
		 * scan buffer withut any change
		 */
		bss_info += sizeof(u16);
		bytes_left -= sizeof(u16);

		if (scan_info_tlv) {
			rssi = (s32)(s16)(le16_to_cpu(scan_info_tlv->rssi));
			rssi *= 100;           /* Convert dBm to mBm */
			nxpwifi_dbg(adapter, INFO,
				    "info: InterpretIE: RSSI=%d\n", rssi);
			fw_tsf = le64_to_cpu(scan_info_tlv->tsf);
			radio_type = &scan_info_tlv->radio_type;
		} else {
			radio_type = NULL;
		}
		ret = nxpwifi_parse_single_response_buf(priv, &bss_info,
							&bytes_left, fw_tsf,
							radio_type, true, rssi);
		if (ret)
			goto check_next_scan;
	}

check_next_scan:
	if (!event_scan->more_event)
		nxpwifi_check_next_scan_command(priv);

	return ret;
}

/*
 * Prepare the background scan query command. Sets the command ID, size,
 * flush parameter, and fixes endianness.
 */
int nxpwifi_cmd_802_11_bg_scan_query(struct host_cmd_ds_command *cmd)
{
	struct host_cmd_ds_802_11_bg_scan_query *bg_query =
		&cmd->params.bg_scan_query;

	cmd->command = cpu_to_le16(HOST_CMD_802_11_BG_SCAN_QUERY);
	cmd->size = cpu_to_le16(sizeof(struct host_cmd_ds_802_11_bg_scan_query)
				+ S_DS_GEN);

	bg_query->flush = 1;

	return 0;
}

/* Insert a scan command node into the scan_pending_q. */
void
nxpwifi_queue_scan_cmd(struct nxpwifi_private *priv,
		       struct cmd_ctrl_node *cmd_node)
{
	struct nxpwifi_adapter *adapter = priv->adapter;

	cmd_node->wait_q_enabled = true;
	cmd_node->condition = &adapter->scan_wait_q_woken;
	spin_lock_bh(&adapter->scan_pending_q_lock);
	list_add_tail(&cmd_node->list, &adapter->scan_pending_q);
	spin_unlock_bh(&adapter->scan_pending_q_lock);
}

/* Append a vendor-specific element TLV to the buffer. */
int
nxpwifi_cmd_append_vsie_tlv(struct nxpwifi_private *priv,
			    u16 vsie_mask, u8 **buffer)
{
	int id, ret_len = 0;
	struct nxpwifi_ie_types_vendor_param_set *vs_param_set;

	if (!buffer)
		return 0;
	if (!(*buffer))
		return 0;

	/*
	 * Traverse through the saved vendor specific element array and append
	 * the selected(scan/assoc) element as TLV to the command
	 */
	for (id = 0; id < NXPWIFI_MAX_VSIE_NUM; id++) {
		if (priv->vs_ie[id].mask & vsie_mask) {
			vs_param_set =
				(struct nxpwifi_ie_types_vendor_param_set *)
				*buffer;
			vs_param_set->header.type =
				cpu_to_le16(TLV_TYPE_PASSTHROUGH);
			vs_param_set->header.len =
				cpu_to_le16((((u16)priv->vs_ie[id].ie[1])
				& 0x00FF) + 2);
			if (le16_to_cpu(vs_param_set->header.len) >
				NXPWIFI_MAX_VSIE_LEN) {
				nxpwifi_dbg(priv->adapter, ERROR,
					    "Invalid param length!\n");
				break;
			}

			memcpy(vs_param_set->ie, priv->vs_ie[id].ie,
			       le16_to_cpu(vs_param_set->header.len));
			*buffer += le16_to_cpu(vs_param_set->header.len) +
				   sizeof(struct nxpwifi_ie_types_header);
			ret_len += le16_to_cpu(vs_param_set->header.len) +
				   sizeof(struct nxpwifi_ie_types_header);
		}
	}
	return ret_len;
}

/*
 * Save the beacon buffer of the current BSS descriptor.
 *
 * The buffer is preserved so it can be restored when the current SSID's
 * beacon is missing, such as when:
 *  - the SSID was not found in the latest scan, or
 *  - the SSID was the last entry in the scan table and was overwritten.
 */
void
nxpwifi_save_curr_bcn(struct nxpwifi_private *priv)
{
	struct nxpwifi_bssdescriptor *curr_bss =
		&priv->curr_bss_params.bss_descriptor;

	if (!curr_bss->beacon_buf_size)
		return;

	/* allocate beacon buffer at 1st time; or if it's size has changed */
	if (!priv->curr_bcn_buf ||
	    priv->curr_bcn_size != curr_bss->beacon_buf_size) {
		priv->curr_bcn_size = curr_bss->beacon_buf_size;

		kfree(priv->curr_bcn_buf);
		priv->curr_bcn_buf = kmalloc(curr_bss->beacon_buf_size,
					     GFP_ATOMIC);
		if (!priv->curr_bcn_buf)
			return;
	}

	memcpy(priv->curr_bcn_buf, curr_bss->beacon_buf,
	       curr_bss->beacon_buf_size);
	nxpwifi_dbg(priv->adapter, INFO,
		    "info: current beacon saved %d\n",
		    priv->curr_bcn_size);

	curr_bss->beacon_buf = priv->curr_bcn_buf;

	/* adjust the pointers in the current BSS descriptor */
	if (curr_bss->bcn_wpa_ie)
		curr_bss->bcn_wpa_ie =
			(struct ieee_types_vendor_specific *)
			(curr_bss->beacon_buf +
			 curr_bss->wpa_offset);

	if (curr_bss->bcn_rsn_ie)
		curr_bss->bcn_rsn_ie =
			(struct element *)(curr_bss->beacon_buf +
					   curr_bss->rsn_offset);

	if (curr_bss->bcn_ht_cap)
		curr_bss->bcn_ht_cap = (struct ieee80211_ht_cap *)
			(curr_bss->beacon_buf +
			 curr_bss->ht_cap_offset);

	if (curr_bss->bcn_ht_oper)
		curr_bss->bcn_ht_oper = (struct ieee80211_ht_operation *)
			(curr_bss->beacon_buf +
			 curr_bss->ht_info_offset);

	if (curr_bss->bcn_vht_cap)
		curr_bss->bcn_vht_cap = (void *)(curr_bss->beacon_buf +
						 curr_bss->vht_cap_offset);

	if (curr_bss->bcn_vht_oper)
		curr_bss->bcn_vht_oper = (void *)(curr_bss->beacon_buf +
						  curr_bss->vht_info_offset);

	if (curr_bss->bcn_he_cap)
		curr_bss->bcn_he_cap = (void *)(curr_bss->beacon_buf +
						curr_bss->he_cap_offset);

	if (curr_bss->bcn_he_oper)
		curr_bss->bcn_he_oper = (void *)(curr_bss->beacon_buf +
						 curr_bss->he_info_offset);

	if (curr_bss->bcn_bss_co_2040)
		curr_bss->bcn_bss_co_2040 =
			(curr_bss->beacon_buf + curr_bss->bss_co_2040_offset);

	if (curr_bss->bcn_ext_cap)
		curr_bss->bcn_ext_cap = curr_bss->beacon_buf +
			curr_bss->ext_cap_offset;

	if (curr_bss->oper_mode)
		curr_bss->oper_mode = (void *)(curr_bss->beacon_buf +
					       curr_bss->oper_mode_offset);
}

/* Free the beacon buffer in the current BSS descriptor. */
void
nxpwifi_free_curr_bcn(struct nxpwifi_private *priv)
{
	kfree(priv->curr_bcn_buf);
	priv->curr_bcn_buf = NULL;
}
