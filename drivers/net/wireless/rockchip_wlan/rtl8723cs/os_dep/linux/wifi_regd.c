/******************************************************************************
 *
 * Copyright(c) 2009-2010 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <drv_types.h>

#ifdef CONFIG_IOCTL_CFG80211
static void rtw_regd_overide_flags(struct wiphy *wiphy, struct rf_ctl_t *rfctl)
{
	RT_CHANNEL_INFO *channel_set = rfctl->channel_set;
	u8 max_chan_nums = rfctl->max_chan_nums;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch;
	unsigned int i, j;
	u16 channel;
	u32 freq;

	/* all channels disable */
	for (i = 0; i < NUM_NL80211_BANDS; i++) {
		sband = wiphy->bands[i];
		if (!sband)
			continue;
		for (j = 0; j < sband->n_channels; j++) {
			ch = &sband->channels[j];
			if (!ch)
				continue;
			ch->flags = IEEE80211_CHAN_DISABLED;
		}
	}

	/* channels apply by channel plans. */
	for (i = 0; i < max_chan_nums; i++) {
		channel = channel_set[i].ChannelNum;
		freq = rtw_ch2freq(channel);
		ch = ieee80211_get_channel(wiphy, freq);
		if (!ch) {
			rtw_warn_on(1);
			continue;
		}

		/* enable */
		ch->flags = 0;

		if (channel_set[i].flags & RTW_CHF_DFS) {
			/*
			* before integrating with nl80211 flow
			* bypass IEEE80211_CHAN_RADAR when configured with radar detection
			* to prevent from hostapd blocking DFS channels
			*/
			if (rtw_rfctl_dfs_domain_unknown(rfctl))
				ch->flags |= IEEE80211_CHAN_RADAR;
		}

		if (channel_set[i].flags & RTW_CHF_NO_IR) {
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			ch->flags |= IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN;
			#else
			ch->flags |= IEEE80211_CHAN_NO_IR;
			#endif
		}
	}
}

#ifdef CONFIG_REGD_SRC_FROM_OS
static void rtw_regd_apply_dfs_flags(struct rf_ctl_t *rfctl)
{
	RT_CHANNEL_INFO *channel_set = rfctl->channel_set;
	u8 max_chan_nums = rfctl->max_chan_nums;
	unsigned int i;
	struct ieee80211_channel *chan;

	/* channels apply by channel plans. */
	for (i = 0; i < max_chan_nums; i++) {
		chan = channel_set[i].os_chan;
		if (channel_set[i].flags & RTW_CHF_DFS) {
			/*
			* before integrating with nl80211 flow
			* clear IEEE80211_CHAN_RADAR when configured with radar detection
			* to prevent from hostapd blocking DFS channels
			*/
			if (!rtw_rfctl_dfs_domain_unknown(rfctl))
				chan->flags &= ~IEEE80211_CHAN_RADAR;
		}
	}
}
#endif

void rtw_regd_apply_flags(struct wiphy *wiphy)
{
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);

	if (rfctl->regd_src == REGD_SRC_RTK_PRIV)
		rtw_regd_overide_flags(wiphy, rfctl);
#ifdef CONFIG_REGD_SRC_FROM_OS
	else if (rfctl->regd_src == REGD_SRC_OS)
		rtw_regd_apply_dfs_flags(rfctl);
#endif
	else
		rtw_warn_on(1);
}

#ifdef CONFIG_REGD_SRC_FROM_OS
/* init_channel_set_from_wiphy */
u8 rtw_os_init_channel_set(_adapter *padapter, RT_CHANNEL_INFO *channel_set)
{
	struct wiphy *wiphy = adapter_to_wiphy(padapter);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(padapter);
	struct registry_priv *regsty = adapter_to_regsty(padapter);
	struct ieee80211_channel *chan;
	u8 chanset_size = 0;
	int i, j;

	_rtw_memset(channel_set, 0, sizeof(RT_CHANNEL_INFO) * MAX_CHANNEL_NUM);

	for (i = NL80211_BAND_2GHZ; i <= NL80211_BAND_5GHZ; i++) {
		if (!wiphy->bands[i])
			continue;
		for (j = 0; j < wiphy->bands[i]->n_channels; j++) {
			chan = &wiphy->bands[i]->channels[j];
			if (chan->flags & IEEE80211_CHAN_DISABLED)
				continue;
			if (rtw_regsty_is_excl_chs(regsty, chan->hw_value))
				continue;

			if (chanset_size >= MAX_CHANNEL_NUM) {
				RTW_WARN("chset size can't exceed MAX_CHANNEL_NUM(%u)\n", MAX_CHANNEL_NUM);
				i = NL80211_BAND_5GHZ + 1;
				break;
			}

			channel_set[chanset_size].ChannelNum = chan->hw_value;
			#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
			if (chan->flags & (IEEE80211_CHAN_NO_IBSS | IEEE80211_CHAN_PASSIVE_SCAN))
			#else
			if (chan->flags & IEEE80211_CHAN_NO_IR)
			#endif
				channel_set[chanset_size].flags |= RTW_CHF_NO_IR;
			if (chan->flags & IEEE80211_CHAN_RADAR)
				channel_set[chanset_size].flags |= RTW_CHF_DFS;
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27))
			if (chan->flags & IEEE80211_CHAN_NO_HT40PLUS)
				channel_set[chanset_size].flags |= RTW_CHF_NO_HT40U;
			if (chan->flags & IEEE80211_CHAN_NO_HT40MINUS)
				channel_set[chanset_size].flags |= RTW_CHF_NO_HT40L;
			#endif
			#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0))
			if (chan->flags & IEEE80211_CHAN_NO_80MHZ)
				channel_set[chanset_size].flags |= RTW_CHF_NO_80MHZ;
			if (chan->flags & IEEE80211_CHAN_NO_160MHZ)
				channel_set[chanset_size].flags |= RTW_CHF_NO_160MHZ;
			#endif
			channel_set[chanset_size].os_chan = chan;
			chanset_size++;
		}
	}

#if CONFIG_IEEE80211_BAND_5GHZ
	#ifdef CONFIG_DFS_MASTER
	for (i = 0; i < chanset_size; i++)
		channel_set[i].non_ocp_end_time = rtw_get_current_time();
	#endif
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

	if (chanset_size)
		RTW_INFO(FUNC_ADPT_FMT" ch num:%d\n"
			, FUNC_ADPT_ARG(padapter), chanset_size);
	else
		RTW_WARN(FUNC_ADPT_FMT" final chset has no channel\n"
			, FUNC_ADPT_ARG(padapter));

	return chanset_size;
}

s16 rtw_os_get_total_txpwr_regd_lmt_mbm(_adapter *adapter, u8 cch, enum channel_width bw)
{
	struct wiphy *wiphy = adapter_to_wiphy(adapter);
	s16 mbm = UNSPECIFIED_MBM;
	u8 *op_chs;
	u8 op_ch_num;
	u8 i;
	u32 freq;
	struct ieee80211_channel *ch;

	if (!rtw_get_op_chs_by_cch_bw(cch, bw, &op_chs, &op_ch_num))
		goto exit;

	for (i = 0; i < op_ch_num; i++) {
		freq = rtw_ch2freq(op_chs[i]);
		ch = ieee80211_get_channel(wiphy, freq);
		if (!ch) {
			rtw_warn_on(1);
			continue;
		}
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 4, 0))
		mbm = rtw_min(mbm, ch->max_reg_power * MBM_PDBM);
		#else
		/* require max_power == 0 (therefore orig_mpwr set to 0) when wiphy registration */
		mbm = rtw_min(mbm, ch->max_power * MBM_PDBM);
		#endif
	}

exit:
	return mbm;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
static enum rtw_dfs_regd nl80211_dfs_regions_to_rtw_dfs_region(enum nl80211_dfs_regions region)
{
	switch (region) {
	case NL80211_DFS_FCC:
		return RTW_DFS_REGD_FCC;
	case NL80211_DFS_ETSI:
		return RTW_DFS_REGD_ETSI;
	case NL80211_DFS_JP:
		return RTW_DFS_REGD_MKK;
	case NL80211_DFS_UNSET:
	default:
		return RTW_DFS_REGD_NONE;
	}
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) */
#endif /* CONFIG_REGD_SRC_FROM_OS */

#ifdef CONFIG_RTW_DEBUG
static const char *nl80211_reg_initiator_str(enum nl80211_reg_initiator initiator)
{
	switch (initiator) {
	case NL80211_REGDOM_SET_BY_DRIVER:
		return "DRIVER";
	case NL80211_REGDOM_SET_BY_CORE:
		return "CORE";
	case NL80211_REGDOM_SET_BY_USER:
		return "USER";
	case NL80211_REGDOM_SET_BY_COUNTRY_IE:
		return "COUNTRY_IE";
	}
	rtw_warn_on(1);
	return "UNKNOWN";
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
static const char *nl80211_user_reg_hint_type_str(enum nl80211_user_reg_hint_type type)
{
	switch (type) {
	case NL80211_USER_REG_HINT_USER:
		return "USER";
	case NL80211_USER_REG_HINT_CELL_BASE:
		return "CELL_BASE";
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0))
	case NL80211_USER_REG_HINT_INDOOR:
		return "INDOOR";
	#endif
	}
	rtw_warn_on(1);
	return "UNKNOWN";
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
static const char *nl80211_dfs_regions_str(enum nl80211_dfs_regions region)
{
	switch (region) {
	case NL80211_DFS_UNSET:
		return "UNSET";
	case NL80211_DFS_FCC:
		return "FCC";
	case NL80211_DFS_ETSI:
		return "ETSI";
	case NL80211_DFS_JP:
		return "JP";
	}
	rtw_warn_on(1);
	return "UNKNOWN";
};
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)) */

static const char *environment_cap_str(enum environment_cap cap)
{
	switch (cap) {
	case ENVIRON_ANY:
		return "ANY";
	case ENVIRON_INDOOR:
		return "INDOOR";
	case ENVIRON_OUTDOOR:
		return "OUTDOOR";
	}
	rtw_warn_on(1);
	return "UNKNOWN";
}

static void dump_requlatory_request(void *sel, struct regulatory_request *request)
{
	u8 alpha2_len;

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 16, 0))
	alpha2_len = 3;
	#else
	alpha2_len = 2;
	#endif

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
	RTW_PRINT_SEL(sel, "initiator:%s, wiphy_idx:%d, type:%s\n"
		, nl80211_reg_initiator_str(request->initiator)
		, request->wiphy_idx
		, nl80211_user_reg_hint_type_str(request->user_reg_hint_type));
	#else
	RTW_PRINT_SEL(sel, "initiator:%s, wiphy_idx:%d\n"
		, nl80211_reg_initiator_str(request->initiator)
		, request->wiphy_idx);
	#endif

	RTW_PRINT_SEL(sel, "alpha2:%.*s\n", alpha2_len, request->alpha2);
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
	RTW_PRINT_SEL(sel, "dfs_region:%s\n", nl80211_dfs_regions_str(request->dfs_region));
	#endif

	RTW_PRINT_SEL(sel, "intersect:%d\n", request->intersect);
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38))
	RTW_PRINT_SEL(sel, "processed:%d\n", request->processed);
	#endif
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36))
	RTW_PRINT_SEL(sel, "country_ie_checksum:0x%08x\n", request->country_ie_checksum);
	#endif

	RTW_PRINT_SEL(sel, "country_ie_env:%s\n", environment_cap_str(request->country_ie_env));
}
#endif /* CONFIG_RTW_DEBUG */

static void rtw_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct dvobj_priv *dvobj = wiphy_to_dvobj(wiphy);
	struct rf_ctl_t *rfctl = dvobj_to_rfctl(dvobj);
	struct registry_priv *regsty = dvobj_to_regsty(dvobj);

#ifdef CONFIG_RTW_DEBUG
	if (rtw_drv_log_level >= _DRV_INFO_) {
		RTW_INFO(FUNC_WIPHY_FMT"\n", FUNC_WIPHY_ARG(wiphy));
		dump_requlatory_request(RTW_DBGDUMP, request);
	}
#endif

#ifdef CONFIG_REGD_SRC_FROM_OS
	if (REGSTY_REGD_SRC_FROM_OS(regsty)) {
		enum rtw_dfs_regd dfs_region =  RTW_DFS_REGD_NONE;

		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
		dfs_region = nl80211_dfs_regions_to_rtw_dfs_region(request->dfs_region);
		#endif

		/* trigger command to sync regulatory form OS */
		rtw_sync_os_regd_cmd(wiphy_to_adapter(wiphy), RTW_CMDF_WAIT_ACK, request->alpha2, dfs_region);
	} else
#endif
	{
		/* use alpha2 as input to select the corresponding channel plan settings defined by Realtek */
		switch (request->initiator) {
		case NL80211_REGDOM_SET_BY_DRIVER:
			break;
		case NL80211_REGDOM_SET_BY_CORE:
			break;
		case NL80211_REGDOM_SET_BY_USER:
			rtw_set_country(wiphy_to_adapter(wiphy), request->alpha2);
			break;
		case NL80211_REGDOM_SET_BY_COUNTRY_IE:
			break;
		}

		rtw_regd_apply_flags(wiphy);
	}
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
static int rtw_reg_notifier_return(struct wiphy *wiphy, struct regulatory_request *request)
{
	rtw_reg_notifier(wiphy, request);
	return 0;
}
#endif

int rtw_regd_init(struct wiphy *wiphy)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
	wiphy->reg_notifier = rtw_reg_notifier_return;
#else
	wiphy->reg_notifier = rtw_reg_notifier;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 14, 0))
	wiphy->flags &= ~WIPHY_FLAG_STRICT_REGULATORY;
	wiphy->flags &= ~WIPHY_FLAG_DISABLE_BEACON_HINTS;
#else
	wiphy->regulatory_flags &= ~REGULATORY_STRICT_REG;
	wiphy->regulatory_flags &= ~REGULATORY_DISABLE_BEACON_HINTS;
#endif

	return 0;
}
#endif /* CONFIG_IOCTL_CFG80211 */
