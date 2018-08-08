// SPDX-License-Identifier: GPL-2.0
/* Implement 802.11d. */

#include "dot11d.h"

void dot11d_init(struct ieee80211_device *ieee)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(ieee);

	pDot11dInfo->enabled = false;

	pDot11dInfo->state = DOT11D_STATE_NONE;
	pDot11dInfo->country_ie_len = 0;
	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER + 1);
	memset(pDot11dInfo->max_tx_pwr_dbm_list, 0xFF, MAX_CHANNEL_NUMBER+1);
	RESET_CIE_WATCHDOG(ieee);

	netdev_info(ieee->dev, "dot11d_init()\n");
}
EXPORT_SYMBOL(dot11d_init);

/* Reset to the state as we are just entering a regulatory domain. */
void dot11d_reset(struct ieee80211_device *ieee)
{
	u32 i;
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(ieee);
	/* Clear old channel map */
	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER+1);
	memset(pDot11dInfo->max_tx_pwr_dbm_list, 0xFF, MAX_CHANNEL_NUMBER+1);
	/* Set new channel map */
	for (i = 1; i <= 11; i++)
		(pDot11dInfo->channel_map)[i] = 1;

	for (i = 12; i <= 14; i++)
		(pDot11dInfo->channel_map)[i] = 2;

	pDot11dInfo->state = DOT11D_STATE_NONE;
	pDot11dInfo->country_ie_len = 0;
	RESET_CIE_WATCHDOG(ieee);
}
EXPORT_SYMBOL(dot11d_reset);

/*
 * Update country IE from Beacon or Probe Resopnse and configure PHY for
 * operation in the regulatory domain.
 *
 * TODO: Configure Tx power.
 * Assumption:
 * 1. IS_DOT11D_ENABLE() is TRUE.
 * 2. Input IE is an valid one.
 */
void dot11d_update_country_ie(struct ieee80211_device *dev, u8 *pTaddr,
			    u16 CoutryIeLen, u8 *pCoutryIe)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 i, j, NumTriples, MaxChnlNum;
	struct chnl_txpower_triple *pTriple;

	memset(pDot11dInfo->channel_map, 0, MAX_CHANNEL_NUMBER+1);
	memset(pDot11dInfo->max_tx_pwr_dbm_list, 0xFF, MAX_CHANNEL_NUMBER+1);
	MaxChnlNum = 0;
	NumTriples = (CoutryIeLen - 3) / 3; /* skip 3-byte country string. */
	pTriple = (struct chnl_txpower_triple *)(pCoutryIe + 3);
	for (i = 0; i < NumTriples; i++) {
		if (MaxChnlNum >= pTriple->first_channel) {
			/* It is not in a monotonically increasing order, so
			 * stop processing.
			 */
			netdev_err(dev->dev, "dot11d_update_country_ie(): Invalid country IE, skip it........1\n");
			return;
		}
		if (MAX_CHANNEL_NUMBER < (pTriple->first_channel + pTriple->num_channels)) {
			/* It is not a valid set of channel id, so stop
			 * processing.
			 */
			netdev_err(dev->dev, "dot11d_update_country_ie(): Invalid country IE, skip it........2\n");
			return;
		}

		for (j = 0; j < pTriple->num_channels; j++) {
			pDot11dInfo->channel_map[pTriple->first_channel + j] = 1;
			pDot11dInfo->max_tx_pwr_dbm_list[pTriple->first_channel + j] = pTriple->max_tx_pwr_dbm;
			MaxChnlNum = pTriple->first_channel + j;
		}

		pTriple = (struct chnl_txpower_triple *)((u8 *)pTriple + 3);
	}
	netdev_info(dev->dev, "Channel List:");
	for (i = 1; i <= MAX_CHANNEL_NUMBER; i++)
		if (pDot11dInfo->channel_map[i] > 0)
			netdev_info(dev->dev, " %d", i);
	netdev_info(dev->dev, "\n");

	UPDATE_CIE_SRC(dev, pTaddr);

	pDot11dInfo->country_ie_len = CoutryIeLen;
	memcpy(pDot11dInfo->country_ie_buf, pCoutryIe, CoutryIeLen);
	pDot11dInfo->state = DOT11D_STATE_LEARNED;
}
EXPORT_SYMBOL(dot11d_update_country_ie);

u8 dot11d_get_max_tx_pwr_in_dbm(struct ieee80211_device *dev, u8 Channel)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 MaxTxPwrInDbm = 255;

	if (Channel > MAX_CHANNEL_NUMBER) {
		netdev_err(dev->dev, "dot11d_get_max_tx_pwr_in_dbm(): Invalid Channel\n");
		return MaxTxPwrInDbm;
	}
	if (pDot11dInfo->channel_map[Channel])
		MaxTxPwrInDbm = pDot11dInfo->max_tx_pwr_dbm_list[Channel];

	return MaxTxPwrInDbm;
}
EXPORT_SYMBOL(dot11d_get_max_tx_pwr_in_dbm);

void dot11d_scan_complete(struct ieee80211_device *dev)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);

	switch (pDot11dInfo->state) {
	case DOT11D_STATE_LEARNED:
		pDot11dInfo->state = DOT11D_STATE_DONE;
		break;

	case DOT11D_STATE_DONE:
		if (GET_CIE_WATCHDOG(dev) == 0) {
			/* Reset country IE if previous one is gone. */
			dot11d_reset(dev);
		}
		break;
	case DOT11D_STATE_NONE:
		break;
	}
}
EXPORT_SYMBOL(dot11d_scan_complete);

int is_legal_channel(struct ieee80211_device *dev, u8 channel)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);

	if (channel > MAX_CHANNEL_NUMBER) {
		netdev_err(dev->dev, "is_legal_channel(): Invalid Channel\n");
		return 0;
	}
	if (pDot11dInfo->channel_map[channel] > 0)
		return 1;
	return 0;
}
EXPORT_SYMBOL(is_legal_channel);

int to_legal_channel(struct ieee80211_device *dev, u8 channel)
{
	struct rt_dot11d_info *pDot11dInfo = GET_DOT11D_INFO(dev);
	u8 default_chn = 0;
	u32 i = 0;

	for (i = 1; i <= MAX_CHANNEL_NUMBER; i++) {
		if (pDot11dInfo->channel_map[i] > 0) {
			default_chn = i;
			break;
		}
	}

	if (channel > MAX_CHANNEL_NUMBER) {
		netdev_err(dev->dev, "is_legal_channel(): Invalid Channel\n");
		return default_chn;
	}

	if (pDot11dInfo->channel_map[channel] > 0)
		return channel;

	return default_chn;
}
EXPORT_SYMBOL(to_legal_channel);
