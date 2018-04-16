/** @file  moal_uap_wext.c
  *
  * @brief This file contains wireless extension standard ioctl functions
  *
  * Copyright (C) 2010-2017, Marvell International Ltd.
  *
  * This software file (the "File") is distributed by Marvell International
  * Ltd. under the terms of the GNU General Public License Version 2, June 1991
  * (the "License").  You may use, redistribute and/or modify this File in
  * accordance with the terms and conditions of the License, a copy of which
  * is available by writing to the Free Software Foundation, Inc.,
  * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
  * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
  *
  * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
  * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
  * this warranty disclaimer.
  *
  */

/************************************************************************
Change log:
    08/06/2010: initial version
************************************************************************/

#include        "moal_main.h"
#include        "moal_uap.h"
#include        "moal_wext.h"
#include        "moal_uap_priv.h"

/********************************************************
			Global Variables
********************************************************/
typedef struct _chan_to_freq_t {
    /** Channel */
	t_u16 channel;
    /** Frequency */
	t_u32 freq;
    /** Band */
	t_u8 band;
} chan_to_freq_t;

const chan_to_freq_t chan_to_freq[] = {
	{1, 2412, 0},
	{2, 2417, 0},
	{3, 2422, 0},
	{4, 2427, 0},
	{5, 2432, 0},
	{6, 2437, 0},
	{7, 2442, 0},
	{8, 2447, 0},
	{9, 2452, 0},
	{10, 2457, 0},
	{11, 2462, 0},
	{12, 2467, 0},
	{13, 2472, 0},
	{14, 2484, 0},
	{183, 4915, 1},
	{184, 4920, 1},
	{185, 4925, 1},
	{187, 4935, 1},
	{188, 4940, 1},
	{189, 4945, 1},
	{192, 4960, 1},
	{196, 4980, 1},
	{7, 5035, 1},
	{8, 5040, 1},
	{9, 5045, 1},
	{11, 5055, 1},
	{12, 5060, 1},
	{16, 5080, 1},
	{34, 5170, 1},
	{36, 5180, 1},
	{38, 5190, 1},
	{40, 5200, 1},
	{42, 5210, 1},
	{44, 5220, 1},
	{46, 5230, 1},
	{48, 5240, 1},
	{52, 5260, 1},
	{56, 5280, 1},
	{60, 5300, 1},
	{64, 5320, 1},
	{100, 5500, 1},
	{104, 5520, 1},
	{108, 5540, 1},
	{112, 5560, 1},
	{116, 5580, 1},
	{120, 5600, 1},
	{124, 5620, 1},
	{128, 5640, 1},
	{132, 5660, 1},
	{136, 5680, 1},
	{140, 5700, 1},
	{144, 5720, 1},
	{149, 5745, 1},
	{153, 5765, 1},
	{157, 5785, 1},
	{161, 5805, 1},
	{165, 5825, 1},
};

/** Convertion from frequency to channel */
#define freq_to_chan(x) ((((x) - 2412) / 5) + 1)

/********************************************************
			Local Functions
********************************************************/

/**
 *  @brief Sort Channels
 *
 *  @param freq                 A pointer to iw_freq structure
 *  @param num                  Number of Channels
 *
 *  @return                     N/A
 */
static inline void
woal_sort_channels(struct iw_freq *freq, int num)
{
	int i, j;
	struct iw_freq temp;

	for (i = 0; i < num; i++)
		for (j = i + 1; j < num; j++)
			if (freq[i].i > freq[j].i) {
				temp.i = freq[i].i;
				temp.m = freq[i].m;

				freq[i].i = freq[j].i;
				freq[i].m = freq[j].m;

				freq[j].i = temp.i;
				freq[j].m = temp.m;
			}
}

/**
 *  @brief Get frequency for channel in given band
 *
 *  @param channel      channel
 *  @param band         band
 *
 *  @return             freq
 */
static int
channel_to_frequency(t_u16 channel, t_u8 band)
{
	int i = 0;

	ENTER();
	for (i = 0; i < ARRAY_SIZE(chan_to_freq); i++) {
		if (channel == chan_to_freq[i].channel &&
		    band == chan_to_freq[i].band) {
			LEAVE();
			return chan_to_freq[i].freq;
		}
	}
	LEAVE();
	return 0;
}

/**
 *  @brief Commit handler: called after a bunch of SET operations
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param cwrq         A pointer to char buffer
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success
 */
static int
woal_config_commit(struct net_device *dev,
		   struct iw_request_info *info, char *cwrq, char *extra)
{
	ENTER();

	LEAVE();
	return 0;
}

/**
 *  @brief Get name
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param cwrq         A pointer to char buffer
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success
 */
static int
woal_get_name(struct net_device *dev, struct iw_request_info *info,
	      char *cwrq, char *extra)
{
	ENTER();
	strcpy(cwrq, "IEEE 802.11-DS");
	LEAVE();
	return 0;
}

/**
 *  @brief Get current BSSID
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param awrq         A pointer to sockaddr structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success
 */
static int
woal_get_wap(struct net_device *dev, struct iw_request_info *info,
	     struct sockaddr *awrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	int ret = 0;

	ENTER();

	if (priv->bss_started)
		memcpy(awrq->sa_data, priv->current_addr, MLAN_MAC_ADDR_LENGTH);
	else
		memset(awrq->sa_data, 0, MLAN_MAC_ADDR_LENGTH);
	awrq->sa_family = ARPHRD_ETHER;

	LEAVE();
	return ret;
}

/**
 *  @brief Change the AP BSSID
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param awrq         A pointer to iw_param structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0 --success, otherwise fail
 */
static int
woal_set_wap(struct net_device *dev, struct iw_request_info *info,
	     struct sockaddr *awrq, char *extra)
{
	int ret = 0;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	const t_u8 zero_mac[MLAN_MAC_ADDR_LENGTH] = { 0, 0, 0, 0, 0, 0 };

	ENTER();

	if (awrq->sa_family != ARPHRD_ETHER) {
		ret = -EINVAL;
		goto done;
	}

	PRINTM(MINFO, "ASSOC: WAP: uAP bss : " MACSTR "\n",
	       MAC2STR((t_u8 *)awrq->sa_data));

	/*
	 * Using this ioctl to start/stop the BSS, return if bss
	 * is already started/stopped.
	 */
	if (memcmp(zero_mac, awrq->sa_data, MLAN_MAC_ADDR_LENGTH)) {
		if (priv->bss_started == MFALSE)
			ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
						UAP_BSS_START);
		else
			PRINTM(MINFO, "BSS is already started.\n");
	} else {
		/* zero_mac means bss_stop */
		if (priv->bss_started == MTRUE)
			ret = woal_uap_bss_ctrl(priv, MOAL_IOCTL_WAIT,
						UAP_BSS_STOP);
		else
			PRINTM(MINFO, "BSS is already stopped.\n");
	}

done:
	LEAVE();
	return ret;
}

/**
 *  @brief Set frequency/channel
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param fwrq                 A pointer to iw_freq structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_freq(struct net_device *dev, struct iw_request_info *info,
	      struct iw_freq *fwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *sys_cfg = NULL, *ap_cfg = NULL;
	int ret = 0, chan = 0, i = 0;

	ENTER();

	ap_cfg = kmalloc(sizeof(mlan_uap_bss_param), GFP_KERNEL);
	if (ap_cfg == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	sys_cfg = kmalloc(sizeof(mlan_uap_bss_param), GFP_KERNEL);
	if (sys_cfg == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}
	i = ap_cfg->num_of_chan;

	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);

	/* If setting by frequency, convert to a channel */
	if (fwrq->e == 1)
		chan = freq_to_chan(fwrq->m / 100000);
	else
		chan = fwrq->m;
	if (chan > 0 && chan < MLAN_MAX_CHANNEL)
		sys_cfg->channel = chan;
	else {
		ret = -EINVAL;
		goto done;
	}
	for (i = 0; i < ap_cfg->num_of_chan; i++)
		if (ap_cfg->chan_list[i].chan_number == chan)
			break;
	if (i == ap_cfg->num_of_chan) {
		PRINTM(MERROR, "Channel %d is not supported\n", chan);
		ret = -EINVAL;
		goto done;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_cfg)) {
		PRINTM(MERROR, "Error setting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(sys_cfg);
	kfree(ap_cfg);
	LEAVE();
	return ret;
}

/**
 *  @brief Get frequency and channel
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param fwrq                 A pointer to iw_freq structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_get_freq(struct net_device *dev, struct iw_request_info *info,
	      struct iw_freq *fwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *ap_cfg = NULL;
	t_u8 band = 0;
	int ret = 0;

	ENTER();

	ap_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!ap_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		return -EFAULT;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		kfree(ap_cfg);
		LEAVE();
		return -EFAULT;
	}

	band = (ap_cfg->bandcfg.chanBand == BAND_5GHZ);
	fwrq->m = (long)channel_to_frequency(ap_cfg->channel, band);
	fwrq->i = (long)ap_cfg->channel;
	fwrq->e = 6;

	kfree(ap_cfg);
	LEAVE();
	return ret;
}

/**
 *  @brief Set wlan mode
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param uwrq                 A pointer to t_u32 string
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_bss_mode(struct net_device *dev, struct iw_request_info *info,
		  t_u32 *uwrq, char *extra)
{
	int ret = 0;
	ENTER();

	switch (*uwrq) {
	case IW_MODE_AUTO:
	case IW_MODE_MASTER:
		PRINTM(MINFO, "This is correct mode in AP mode\n");
		break;
	default:
		PRINTM(MERROR, "Invalid mode for AP\n");
		ret = -EINVAL;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief Get wlan mode
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param uwrq                 A pointer to t_u32 string
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success
 */
static int
woal_get_bss_mode(struct net_device *dev, struct iw_request_info *info,
		  t_u32 *uwrq, char *extra)
{
	ENTER();

	*uwrq = IW_MODE_MASTER;

	LEAVE();
	return 0;
}

/**
 *  @brief Set encryption key
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param dwrq                 A pointer to iw_point structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_encode(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	int ret = 0;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *sys_cfg = NULL, *ap_cfg = NULL;
	wep_key *pkey = NULL;
	int key_index = 0;

	ENTER();

	/* Check index */
	key_index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
	if (key_index > 3) {
		PRINTM(MERROR, "Key index #%d out of range\n", key_index);
		ret = -EINVAL;
		goto done;
	}

	ap_cfg = kmalloc(sizeof(mlan_uap_bss_param), GFP_KERNEL);
	if (ap_cfg == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	sys_cfg = kmalloc(sizeof(mlan_uap_bss_param), GFP_KERNEL);
	if (sys_cfg == NULL) {
		ret = -ENOMEM;
		goto done;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);
	sys_cfg->wep_cfg.key0.key_index = 0;
	sys_cfg->wep_cfg.key1.key_index = 1;
	sys_cfg->wep_cfg.key2.key_index = 2;
	sys_cfg->wep_cfg.key3.key_index = 3;

	if (key_index >= 0 && key_index <= 3) {
		if (key_index == 0)
			pkey = &sys_cfg->wep_cfg.key0;
		else if (key_index == 1)
			pkey = &sys_cfg->wep_cfg.key1;
		else if (key_index == 2)
			pkey = &sys_cfg->wep_cfg.key2;
		else if (key_index == 3)
			pkey = &sys_cfg->wep_cfg.key3;
	}

	if (!(dwrq->flags & IW_ENCODE_NOKEY) && dwrq->length) {
		if (dwrq->length > MAX_WEP_KEY_SIZE) {
			PRINTM(MERROR, "Key length (%d) out of range\n",
			       dwrq->length);
			ret = -E2BIG;
			goto done;
		}
		if (key_index < 0) {
			/* Get current default key index */
			if (ap_cfg->wep_cfg.key0.is_default)
				pkey = &sys_cfg->wep_cfg.key0;
			if (ap_cfg->wep_cfg.key1.is_default)
				pkey = &sys_cfg->wep_cfg.key1;
			if (ap_cfg->wep_cfg.key2.is_default)
				pkey = &sys_cfg->wep_cfg.key2;
			if (ap_cfg->wep_cfg.key3.is_default)
				pkey = &sys_cfg->wep_cfg.key3;
			else {	/* Something wrong, select first key as default */
				PRINTM(MERROR,
				       "No default key set! Selecting first key.\n");
				pkey = &sys_cfg->wep_cfg.key0;
			}
		}

		sys_cfg->protocol = PROTOCOL_STATIC_WEP;
		if (extra)
			memcpy(pkey->key, extra, dwrq->length);
		/* Set the length */
		if (dwrq->length > MIN_WEP_KEY_SIZE)
			pkey->length = MAX_WEP_KEY_SIZE;
		else
			pkey->length = MIN_WEP_KEY_SIZE;
		/* Set current key index as default */
		pkey->is_default = MTRUE;
	} else {
		/*
		 * No key provided so it is either enable key,
		 * on or off
		 */
		if (dwrq->flags & IW_ENCODE_DISABLED) {
			PRINTM(MINFO, "*** iwconfig mlanX key off ***\n");
			sys_cfg->protocol = PROTOCOL_NO_SECURITY;
		} else {
			/*
			 * iwconfig mlanX key [n]
			 * iwconfig mlanX key on
			 * Do we want to just set the transmit key index ?
			 */
			if (key_index < 0) {
				PRINTM(MINFO,
				       "*** iwconfig mlanX key on ***\n");
			} else {
				/* Get current key configuration at key_index */
				if (key_index == 0)
					memcpy(pkey, &ap_cfg->wep_cfg.key0,
					       sizeof(wep_key));
				if (key_index == 1)
					memcpy(pkey, &ap_cfg->wep_cfg.key1,
					       sizeof(wep_key));
				if (key_index == 2)
					memcpy(pkey, &ap_cfg->wep_cfg.key2,
					       sizeof(wep_key));
				if (key_index == 3)
					memcpy(pkey, &ap_cfg->wep_cfg.key3,
					       sizeof(wep_key));
				/* Set current key index as default */
				pkey->is_default = MTRUE;
			}
		}
	}
	if (dwrq->flags & (IW_ENCODE_RESTRICTED | IW_ENCODE_OPEN)) {
		switch (dwrq->flags & 0xf000) {
		case IW_ENCODE_RESTRICTED:
			/* iwconfig mlanX restricted key [1] */
			sys_cfg->auth_mode = MLAN_AUTH_MODE_SHARED;
			PRINTM(MINFO, "Auth mode restricted!\n");
			break;
		case IW_ENCODE_OPEN:
			/* iwconfig mlanX key [2] open */
			sys_cfg->auth_mode = MLAN_AUTH_MODE_OPEN;
			PRINTM(MINFO, "Auth mode open!\n");
			break;
		case IW_ENCODE_RESTRICTED | IW_ENCODE_OPEN:
		default:
			/* iwconfig mlanX key [2] open restricted */
			PRINTM(MINFO, "Auth mode auto!\n");
			break;
		}
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_cfg)) {
		PRINTM(MERROR, "Error setting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(sys_cfg);
	kfree(ap_cfg);
	LEAVE();
	return ret;
}

/**
 *  @brief Get encryption key
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param dwrq                 A pointer to iw_point structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_get_encode(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	int index = (dwrq->flags & IW_ENCODE_INDEX);
	wep_key *pkey = NULL;
	mlan_uap_bss_param *ap_cfg = NULL;
	int ret = 0;

	ENTER();
	if (index < 0 || index > 4) {
		PRINTM(MERROR, "Key index #%d out of range\n", index);
		ret = -EINVAL;
		goto done;
	}
	ap_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!ap_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		ret = -EFAULT;
		goto done;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

	dwrq->flags = 0;
	/*
	 * Check encryption mode
	 */
	switch (ap_cfg->auth_mode) {
	case MLAN_AUTH_MODE_OPEN:
		dwrq->flags = IW_ENCODE_OPEN;
		break;
	case MLAN_AUTH_MODE_SHARED:
	case MLAN_AUTH_MODE_NETWORKEAP:
		dwrq->flags = IW_ENCODE_RESTRICTED;
		break;
	default:
		dwrq->flags = IW_ENCODE_DISABLED | IW_ENCODE_OPEN;
		break;
	}

	switch (ap_cfg->protocol) {
	case PROTOCOL_NO_SECURITY:
		dwrq->flags |= IW_ENCODE_DISABLED;
		break;
	case PROTOCOL_STATIC_WEP:
		if (ap_cfg->wep_cfg.key0.is_default)
			pkey = &ap_cfg->wep_cfg.key0;
		else if (ap_cfg->wep_cfg.key1.is_default)
			pkey = &ap_cfg->wep_cfg.key1;
		else if (ap_cfg->wep_cfg.key2.is_default)
			pkey = &ap_cfg->wep_cfg.key2;
		else if (ap_cfg->wep_cfg.key3.is_default)
			pkey = &ap_cfg->wep_cfg.key3;
		if (pkey) {
			dwrq->flags |= (pkey->key_index + 1);
			dwrq->length = pkey->length;
			memcpy(extra, pkey->key, pkey->length);
			dwrq->flags &= ~IW_ENCODE_DISABLED;
		} else {
			ret = -EFAULT;
		}
		break;
	case PROTOCOL_WPA:
	case PROTOCOL_WPA2:
	case PROTOCOL_WPA2_MIXED:
		memcpy(extra, ap_cfg->wpa_cfg.passphrase,
		       ap_cfg->wpa_cfg.length);
		dwrq->length = ap_cfg->wpa_cfg.length;
		dwrq->flags |= 1;
		dwrq->flags &= ~IW_ENCODE_DISABLED;
		break;
	default:
		dwrq->flags &= ~IW_ENCODE_DISABLED;
		break;
	}
	dwrq->flags |= IW_ENCODE_NOKEY;

done:
	kfree(ap_cfg);
	LEAVE();
	return ret;
}

#if (WIRELESS_EXT >= 18)
/**
 *  @brief Get IE
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param dwrq                 A pointer to iw_point structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     -EOPNOTSUPP
 */
static int
woal_get_gen_ie(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	ENTER();
	LEAVE();
	return -EOPNOTSUPP;
}

/**
 *  @brief Set IE
 *
 *  Pass an opaque block of data, expected to be IEEE IEs, to the driver
 *    for eventual passthrough to the firmware in an associate/join
 *    (and potentially start) command.
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param dwrq                 A pointer to iw_point structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_gen_ie(struct net_device *dev, struct iw_request_info *info,
		struct iw_point *dwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *sys_cfg = NULL;
	IEEEtypes_Header_t *tlv = NULL;
	int tlv_hdr_len = sizeof(IEEEtypes_Header_t), tlv_buf_left = 0;
	int ret = 0;

	ENTER();

	sys_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}
	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);

	tlv_buf_left = dwrq->length;
	tlv = (IEEEtypes_Header_t *)extra;
	while (tlv_buf_left >= tlv_hdr_len) {
		if (tlv->element_id == WPA_IE) {
			sys_cfg->protocol |= PROTOCOL_WPA;
			if (priv->pairwise_cipher == CIPHER_TKIP) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa =
					CIPHER_TKIP;
				PRINTM(MINFO, "Set IE Cipher TKIP\n");
			}
			if (priv->pairwise_cipher == CIPHER_AES_CCMP) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa =
					CIPHER_AES_CCMP;
				PRINTM(MINFO, "Set IE Cipher CCMP\n");
			}
			if (priv->pairwise_cipher ==
			    (CIPHER_TKIP | CIPHER_AES_CCMP)) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa =
					CIPHER_TKIP | CIPHER_AES_CCMP;
				PRINTM(MINFO, "Set IE Cipher TKIP + CCMP\n");
			}
			memcpy(priv->bcn_ie_buf + priv->bcn_ie_len,
			       ((t_u8 *)tlv),
			       sizeof(IEEEtypes_Header_t) + tlv->len);
			priv->bcn_ie_len +=
				sizeof(IEEEtypes_Header_t) + tlv->len;
		}
		if (tlv->element_id == RSN_IE) {
			sys_cfg->protocol |= PROTOCOL_WPA2;
			if (priv->pairwise_cipher == CIPHER_TKIP) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa2 =
					CIPHER_TKIP;
			}
			if (priv->pairwise_cipher == CIPHER_AES_CCMP) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa2 =
					CIPHER_AES_CCMP;
			}
			if (priv->pairwise_cipher ==
			    (CIPHER_TKIP | CIPHER_AES_CCMP)) {
				sys_cfg->wpa_cfg.pairwise_cipher_wpa2 =
					(CIPHER_TKIP | CIPHER_AES_CCMP);
			}
			memcpy(priv->bcn_ie_buf + priv->bcn_ie_len,
			       ((t_u8 *)tlv),
			       sizeof(IEEEtypes_Header_t) + tlv->len);
			priv->bcn_ie_len +=
				sizeof(IEEEtypes_Header_t) + tlv->len;
		}
		if (priv->group_cipher == CIPHER_TKIP)
			sys_cfg->wpa_cfg.group_cipher = CIPHER_TKIP;
		if (priv->group_cipher == CIPHER_AES_CCMP)
			sys_cfg->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
		tlv_buf_left -= (tlv_hdr_len + tlv->len);
		tlv = (IEEEtypes_Header_t *)((t_u8 *)tlv + tlv_hdr_len +
					     tlv->len);
	}
	sys_cfg->key_mgmt = priv->uap_key_mgmt;
	if (sys_cfg->key_mgmt & KEY_MGMT_PSK)
		sys_cfg->key_mgmt_operation |= 0x01;
	if (sys_cfg->key_mgmt & KEY_MGMT_EAP)
		sys_cfg->key_mgmt_operation |= 0x03;

	if (sys_cfg->protocol) {
		if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
								   MLAN_ACT_SET,
								   MOAL_IOCTL_WAIT,
								   sys_cfg)) {
			PRINTM(MERROR, "Error setting AP configuration\n");
			ret = -EFAULT;
			goto done;
		}
		priv->pairwise_cipher = 0;
		priv->group_cipher = 0;

		/* custom IE command to set priv->bcn_ie_buf */
		if (MLAN_STATUS_SUCCESS !=
#define UAP_RSN_MASK  (BIT(8) | BIT(5) | BIT(1) | BIT(3))
		    woal_set_get_custom_ie(priv, UAP_RSN_MASK, priv->bcn_ie_buf,
					   priv->bcn_ie_len)) {
			PRINTM(MERROR, "Error setting wpa-rsn IE\n");
			ret = -EFAULT;
		}
	} else if (dwrq->length == 0) {
		/* custom IE command to re-set priv->bcn_ie_buf */
		if (MLAN_STATUS_SUCCESS !=
		    woal_set_get_custom_ie(priv, 0, priv->bcn_ie_buf,
					   priv->bcn_ie_len)) {
			PRINTM(MERROR, "Error resetting wpa-rsn IE\n");
			ret = -EFAULT;
		}
		priv->bcn_ie_len = 0;
	}

done:
	kfree(sys_cfg);
	LEAVE();
	return ret;
}

/**
 *  @brief  Extended version of encoding configuration
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return              0 --success, otherwise fail
 */
static int
woal_set_encode_ext(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	int key_index;
	t_u8 *pkey_material = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_ds_sec_cfg *sec = NULL;
	mlan_uap_bss_param *sys_cfg = NULL;
	wep_key *pwep_key = NULL;
	int ret = 0;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	key_index = (dwrq->flags & IW_ENCODE_INDEX) - 1;
	if (key_index < 0 || key_index > 5) {
		ret = -EINVAL;
		goto done;
	}
	if (ext->key_len > (dwrq->length - sizeof(struct iw_encode_ext))) {
		ret = -EINVAL;
		goto done;
	}
	sys_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		ret = -EFAULT;
		goto done;
	}

	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);

	pkey_material = (t_u8 *)(ext + 1);
	/* Disable Key */
	if ((dwrq->flags & IW_ENCODE_DISABLED) && !ext->key_len) {
		sys_cfg->protocol = PROTOCOL_NO_SECURITY;
	} else if (ext->alg == IW_ENCODE_ALG_WEP) {
		sys_cfg->protocol = PROTOCOL_STATIC_WEP;
		/* Set WEP key */
		switch (key_index) {
		case 0:
			pwep_key = &sys_cfg->wep_cfg.key0;
			break;
		case 1:
			pwep_key = &sys_cfg->wep_cfg.key1;
			break;
		case 2:
			pwep_key = &sys_cfg->wep_cfg.key2;
			break;
		case 3:
			pwep_key = &sys_cfg->wep_cfg.key3;
			break;
		}
		if (pwep_key) {
			pwep_key->key_index = key_index;
			pwep_key->is_default = MTRUE;
			pwep_key->length = ext->key_len;
			memcpy(pwep_key->key, pkey_material, ext->key_len);
		}
	} else {
		/* Set GTK/PTK key */
		req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_sec_cfg));
		if (req == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		sec = (mlan_ds_sec_cfg *)req->pbuf;
		sec->sub_command = MLAN_OID_SEC_CFG_ENCRYPT_KEY;
		req->req_id = MLAN_IOCTL_SEC_CFG;
		req->action = MLAN_ACT_SET;
		sec->param.encrypt_key.key_len = ext->key_len;
		sec->param.encrypt_key.key_index = key_index;
		memcpy(sec->param.encrypt_key.key_material, pkey_material,
		       ext->key_len);
		memcpy(sec->param.encrypt_key.mac_addr, ext->addr.sa_data,
		       ETH_ALEN);
		sec->param.encrypt_key.key_flags = ext->ext_flags;
		if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
			memcpy(sec->param.encrypt_key.pn, (t_u8 *)ext->rx_seq,
			       SEQ_MAX_SIZE);
			DBG_HEXDUMP(MCMD_D, "Uap Rx PN",
				    sec->param.encrypt_key.pn, SEQ_MAX_SIZE);
		}
		if (ext->ext_flags & IW_ENCODE_EXT_TX_SEQ_VALID) {
			memcpy(sec->param.encrypt_key.pn, (t_u8 *)ext->tx_seq,
			       SEQ_MAX_SIZE);
			DBG_HEXDUMP(MCMD_D, "Uap Tx PN",
				    sec->param.encrypt_key.pn, SEQ_MAX_SIZE);
		}
		PRINTM(MIOCTL,
		       "set uap wpa key key_index=%d, key_len=%d key_flags=0x%x "
		       MACSTR "\n", key_index, ext->key_len,
		       sec->param.encrypt_key.key_flags,
		       MAC2STR(sec->param.encrypt_key.mac_addr));
		DBG_HEXDUMP(MCMD_D, "uap wpa key", pkey_material, ext->key_len);
#define IW_ENCODE_ALG_AES_CMAC	5

		if (ext->alg == IW_ENCODE_ALG_AES_CMAC)
			sec->param.encrypt_key.key_flags |=
				KEY_FLAG_AES_MCAST_IGTK;
		status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
		if (status != MLAN_STATUS_SUCCESS)
			ret = -EFAULT;
		/* Cipher set will be done in set generic IE */
		priv->pairwise_cipher = ext->alg;
		priv->group_cipher = ext->alg;
		goto done;	/* No AP configuration */
	}
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_cfg)) {
		PRINTM(MERROR, "Error setting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(sys_cfg);
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	LEAVE();
	return ret;
}

/**
 *  @brief  Extended version of encoding configuration
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             -EOPNOTSUPP
 */
static int
woal_get_encode_ext(struct net_device *dev,
		    struct iw_request_info *info,
		    struct iw_point *dwrq, char *extra)
{
	ENTER();
	LEAVE();
	return -EOPNOTSUPP;
}

/**
 *  @brief  Request MLME operation
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0--success, otherwise fail
 */
static int
woal_set_mlme(struct net_device *dev,
	      struct iw_request_info *info, struct iw_point *dwrq, char *extra)
{
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_ds_bss *bss = NULL;
	mlan_ds_get_info *pinfo = NULL;
	mlan_ioctl_req *req = NULL;
	mlan_ds_sta_list *sta_list = NULL;
	const t_u8 bc_addr[] = { 0XFF, 0XFF, 0XFF, 0XFF, 0XFF, 0XFF };
	t_u8 sta_addr[ETH_ALEN];
	int ret = 0, i;
	mlan_status status = MLAN_STATUS_SUCCESS;

	ENTER();

	memset(sta_addr, 0, ETH_ALEN);
	if ((mlme->cmd == IW_MLME_DEAUTH) || (mlme->cmd == IW_MLME_DISASSOC)) {
		memcpy(sta_addr, (t_u8 *)mlme->addr.sa_data, ETH_ALEN);
		PRINTM(MIOCTL,
		       "Deauth station: " MACSTR ", reason=%d\n",
		       MAC2STR(sta_addr), mlme->reason_code);

		/* FIXME: For flushing all stations we need to use zero MAC,
		 * but right now the FW does not support this. So, manually
		 * delete each one individually.
		 */
		/* If deauth all station, get the connected STA list first */
		if (!memcmp(bc_addr, sta_addr, ETH_ALEN)) {
			PRINTM(MIOCTL, "Deauth all stations\n");
			req = woal_alloc_mlan_ioctl_req(sizeof
							(mlan_ds_get_info));
			if (req == NULL) {
				LEAVE();
				return -ENOMEM;
			}
			pinfo = (mlan_ds_get_info *)req->pbuf;
			pinfo->sub_command = MLAN_OID_UAP_STA_LIST;
			req->req_id = MLAN_IOCTL_GET_INFO;
			req->action = MLAN_ACT_GET;
			status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
			if (status != MLAN_STATUS_SUCCESS) {
				ret = -EFAULT;
				goto done;
			}
			sta_list =
				kmalloc(sizeof(mlan_ds_sta_list), GFP_KERNEL);
			if (sta_list == NULL) {
				PRINTM(MERROR, "Memory allocation failed!\n");
				ret = -ENOMEM;
				goto done;
			}
			memcpy(sta_list, &pinfo->param.sta_list,
			       sizeof(mlan_ds_sta_list));
			kfree(req);
			req = NULL;
		}
		req = woal_alloc_mlan_ioctl_req(sizeof(mlan_ds_bss));
		if (req == NULL) {
			ret = -ENOMEM;
			goto done;
		}
		bss = (mlan_ds_bss *)req->pbuf;
		bss->sub_command = MLAN_OID_UAP_DEAUTH_STA;
		req->req_id = MLAN_IOCTL_BSS;
		req->action = MLAN_ACT_SET;

		if (!memcmp(bc_addr, sta_addr, ETH_ALEN)) {
			for (i = 0; i < sta_list->sta_count; i++) {
				memcpy(bss->param.deauth_param.mac_addr,
				       sta_list->info[i].mac_address, ETH_ALEN);
				bss->param.deauth_param.reason_code =
					mlme->reason_code;

				status = woal_request_ioctl(priv, req,
							    MOAL_IOCTL_WAIT);
				if (status != MLAN_STATUS_SUCCESS) {
					ret = -EFAULT;
					goto done;
				}
			}
		} else {
			memcpy(bss->param.deauth_param.mac_addr, sta_addr,
			       ETH_ALEN);
			bss->param.deauth_param.reason_code = mlme->reason_code;

			status = woal_request_ioctl(priv, req, MOAL_IOCTL_WAIT);
			if (status != MLAN_STATUS_SUCCESS) {
				ret = -EFAULT;
				goto done;
			}
		}
	}

done:
	if (status != MLAN_STATUS_PENDING)
		kfree(req);
	kfree(sta_list);
	LEAVE();
	return ret;
}

/** @brief Set authentication mode parameters
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param vwrq                 A pointer to iw_param structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_auth(struct net_device *dev, struct iw_request_info *info,
	      struct iw_param *vwrq, char *extra)
{
	int ret = 0;
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *sys_cfg = NULL;

	ENTER();

	sys_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}
	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);

	switch (vwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_CIPHER_PAIRWISE:
		/* Rest are not supported now */
		if (vwrq->value & IW_AUTH_CIPHER_NONE)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value & IW_AUTH_CIPHER_WEP40)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value & IW_AUTH_CIPHER_WEP104)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value == IW_AUTH_CIPHER_TKIP) {
			sys_cfg->wpa_cfg.pairwise_cipher_wpa = CIPHER_TKIP;
			sys_cfg->wpa_cfg.pairwise_cipher_wpa2 = CIPHER_TKIP;
			priv->pairwise_cipher = CIPHER_TKIP;
			if (!priv->uap_key_mgmt)
				priv->uap_key_mgmt = KEY_MGMT_PSK;
			PRINTM(MINFO, "Set Auth Cipher TKIP\n");
		} else if (vwrq->value == IW_AUTH_CIPHER_CCMP) {
			sys_cfg->wpa_cfg.pairwise_cipher_wpa = CIPHER_AES_CCMP;
			sys_cfg->wpa_cfg.pairwise_cipher_wpa2 = CIPHER_AES_CCMP;
			priv->pairwise_cipher = CIPHER_AES_CCMP;
			if (!priv->uap_key_mgmt)
				priv->uap_key_mgmt = KEY_MGMT_PSK;
			PRINTM(MINFO, "Set Auth Cipher CCMP\n");
		} else if (vwrq->value ==
			   (IW_AUTH_CIPHER_TKIP | IW_AUTH_CIPHER_CCMP)) {
			sys_cfg->wpa_cfg.pairwise_cipher_wpa =
				(CIPHER_TKIP | CIPHER_AES_CCMP);
			sys_cfg->wpa_cfg.pairwise_cipher_wpa2 =
				(CIPHER_TKIP | CIPHER_AES_CCMP);
			priv->pairwise_cipher = (CIPHER_TKIP | CIPHER_AES_CCMP);
			if (!priv->uap_key_mgmt)
				priv->uap_key_mgmt = KEY_MGMT_PSK;
			PRINTM(MINFO, "Set Auth Cipher TKIP + CCMP\n");
		}
		break;
	case IW_AUTH_CIPHER_GROUP:
		/* Rest are not supported now */
		if (vwrq->value & IW_AUTH_CIPHER_NONE)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value & IW_AUTH_CIPHER_WEP40)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value & IW_AUTH_CIPHER_WEP104)
			/* XXX Do not delete no-operation line */
			;
		else if (vwrq->value & IW_AUTH_CIPHER_TKIP) {
			sys_cfg->wpa_cfg.group_cipher = CIPHER_TKIP;
			priv->group_cipher = CIPHER_TKIP;
			if (!priv->uap_key_mgmt)
				priv->uap_key_mgmt = KEY_MGMT_PSK;
			PRINTM(MINFO, "Set Auth Cipher TKIP\n");
		} else if (vwrq->value & IW_AUTH_CIPHER_CCMP) {
			sys_cfg->wpa_cfg.group_cipher = CIPHER_AES_CCMP;
			priv->group_cipher = CIPHER_AES_CCMP;
			if (!priv->uap_key_mgmt)
				priv->uap_key_mgmt = KEY_MGMT_PSK;
			PRINTM(MINFO, "Set Auth Cipher CCMP\n");
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:
		switch (vwrq->value) {
		case IW_AUTH_ALG_SHARED_KEY:
			PRINTM(MINFO, "Auth mode shared key!\n");
			sys_cfg->auth_mode = MLAN_AUTH_MODE_SHARED;
			break;
		case IW_AUTH_ALG_LEAP:
			break;
		case IW_AUTH_ALG_OPEN_SYSTEM:
			PRINTM(MINFO, "Auth mode open!\n");
			sys_cfg->auth_mode = MLAN_AUTH_MODE_OPEN;
			break;
		default:
			PRINTM(MINFO, "Auth mode auto!\n");
			break;
		}
		break;
	case IW_AUTH_WPA_VERSION:
		switch (vwrq->value) {
		case IW_AUTH_WPA_VERSION_DISABLED:
			sys_cfg->protocol = PROTOCOL_NO_SECURITY;
			break;
		case IW_AUTH_WPA_VERSION_WPA:
			sys_cfg->protocol = PROTOCOL_WPA;
			break;
		case IW_AUTH_WPA_VERSION_WPA2:
			sys_cfg->protocol = PROTOCOL_WPA2;
			break;
		case IW_AUTH_WPA_VERSION_WPA | IW_AUTH_WPA_VERSION_WPA2:
			sys_cfg->protocol = PROTOCOL_WPA2_MIXED;
			break;
		default:
			break;
		}
		priv->uap_protocol = sys_cfg->protocol;
		break;
	case IW_AUTH_KEY_MGMT:
		switch (vwrq->value) {
		case IW_AUTH_KEY_MGMT_802_1X:
			sys_cfg->key_mgmt |= KEY_MGMT_EAP;
			priv->uap_key_mgmt |= KEY_MGMT_EAP;
			break;
		case IW_AUTH_KEY_MGMT_PSK:
			sys_cfg->key_mgmt |= KEY_MGMT_PSK;
			priv->uap_key_mgmt |= KEY_MGMT_PSK;
			break;
		default:
			break;
		}
		break;
	case IW_AUTH_WPA_ENABLED:
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_DROP_UNENCRYPTED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
	default:
		kfree(sys_cfg);
		LEAVE();
		return -EOPNOTSUPP;	/* No AP configuration */
	}
	if (!sys_cfg->key_mgmt)
		sys_cfg->key_mgmt = priv->uap_key_mgmt;
	if (sys_cfg->key_mgmt & KEY_MGMT_PSK)
		sys_cfg->key_mgmt_operation |= 0x01;
	if (sys_cfg->key_mgmt & KEY_MGMT_EAP)
		sys_cfg->key_mgmt_operation |= 0x03;
	if (!sys_cfg->protocol)
		sys_cfg->protocol = priv->uap_protocol;

	/* Set AP configuration */
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_cfg)) {
		PRINTM(MERROR, "Error setting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(sys_cfg);
	LEAVE();
	return 0;
}

/**
 *  @brief Get authentication mode parameters
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param vwrq                 A pointer to iw_param structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_get_auth(struct net_device *dev, struct iw_request_info *info,
	      struct iw_param *vwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *ap_cfg = NULL;

	ENTER();

	ap_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!ap_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		kfree(ap_cfg);
		LEAVE();
		return -EFAULT;
	}
	switch (vwrq->flags & IW_AUTH_INDEX) {
	case IW_AUTH_CIPHER_PAIRWISE:
		if (ap_cfg->wpa_cfg.pairwise_cipher_wpa == CIPHER_TKIP ||
		    ap_cfg->wpa_cfg.pairwise_cipher_wpa2 == CIPHER_TKIP)
			vwrq->value = IW_AUTH_CIPHER_TKIP;
		else if (ap_cfg->wpa_cfg.pairwise_cipher_wpa == CIPHER_AES_CCMP
			 || ap_cfg->wpa_cfg.pairwise_cipher_wpa2 ==
			 CIPHER_AES_CCMP)
			vwrq->value = IW_AUTH_CIPHER_CCMP;
		else
			vwrq->value = IW_AUTH_CIPHER_NONE;
		break;
	case IW_AUTH_CIPHER_GROUP:
		if (ap_cfg->wpa_cfg.group_cipher == CIPHER_TKIP)
			vwrq->value = IW_AUTH_CIPHER_TKIP;
		else if (ap_cfg->wpa_cfg.group_cipher == CIPHER_AES_CCMP)
			vwrq->value = IW_AUTH_CIPHER_CCMP;
		else
			vwrq->value = IW_AUTH_CIPHER_NONE;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		if (ap_cfg->auth_mode == MLAN_AUTH_MODE_SHARED)
			vwrq->value = IW_AUTH_ALG_SHARED_KEY;
		else if (ap_cfg->auth_mode == MLAN_AUTH_MODE_NETWORKEAP)
			vwrq->value = IW_AUTH_ALG_LEAP;
		else
			vwrq->value = IW_AUTH_ALG_OPEN_SYSTEM;
		break;
	case IW_AUTH_WPA_ENABLED:
		if (ap_cfg->protocol == PROTOCOL_WPA ||
		    ap_cfg->protocol == PROTOCOL_WPA2 ||
		    ap_cfg->protocol == PROTOCOL_WPA2_MIXED)
			vwrq->value = 1;
		else
			vwrq->value = 0;
		break;
	case IW_AUTH_KEY_MGMT:
		if (ap_cfg->key_mgmt & KEY_MGMT_EAP)
			vwrq->value |= IW_AUTH_KEY_MGMT_802_1X;
		if (ap_cfg->key_mgmt & KEY_MGMT_PSK)
			vwrq->value |= IW_AUTH_KEY_MGMT_PSK;
		break;
	case IW_AUTH_WPA_VERSION:
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_DROP_UNENCRYPTED:
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
	case IW_AUTH_ROAMING_CONTROL:
	case IW_AUTH_PRIVACY_INVOKED:
	default:
		kfree(ap_cfg);
		LEAVE();
		return -EOPNOTSUPP;
	}
	kfree(ap_cfg);
	LEAVE();
	return 0;
}
#endif /* WE >= 18 */

/* Data rate listing
 *      MULTI_BANDS:
 *              abg             a       b       b/g
 *  Infra       G(12)           A(8)    B(4)    G(12)
 *  Adhoc       A+B(12)         A(8)    B(4)    B(4)
 *      non-MULTI_BANDS:
										b       b/g
 *  Infra                               B(4)    G(12)
 *  Adhoc                               B(4)    B(4)
 */
/**
 *  @brief Get Range Info
 *
 *  @param dev                  A pointer to net_device structure
 *  @param info                 A pointer to iw_request_info structure
 *  @param dwrq                 A pointer to iw_point structure
 *  @param extra                A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_get_range(struct net_device *dev, struct iw_request_info *info,
	       struct iw_point *dwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *ap_cfg = NULL;
	struct iw_range *range = (struct iw_range *)extra;
	t_u8 band = 0;
	int i;

	ENTER();

	ap_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!ap_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		LEAVE();
		return -EFAULT;
	}
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		kfree(ap_cfg);
		LEAVE();
		return -EFAULT;
	}
	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(struct iw_range));

	range->min_nwid = 0;
	range->max_nwid = 0;

	range->num_bitrates = MAX_DATA_RATES;
	for (i = 0; i < MIN(MAX_DATA_RATES, IW_MAX_BITRATES) &&
	     ap_cfg->rates[i]; i++) {
		range->bitrate[i] = (ap_cfg->rates[i] & 0x7f) * 500000;
	}
	range->num_bitrates = i;
	PRINTM(MINFO, "IW_MAX_BITRATES=%d num_bitrates=%d\n",
	       IW_MAX_BITRATES, range->num_bitrates);

	range->num_frequency = MIN(ap_cfg->num_of_chan, IW_MAX_FREQUENCIES);

	for (i = 0; i < range->num_frequency; i++) {
		range->freq[i].i = (long)ap_cfg->chan_list[i].chan_number;
		band = (ap_cfg->chan_list[i].bandcfg.chanBand == BAND_5GHZ);
		range->freq[i].m =
			(long)channel_to_frequency(ap_cfg->chan_list[i].
						   chan_number, band) * 100000;
		range->freq[i].e = 1;
	}

	PRINTM(MINFO, "IW_MAX_FREQUENCIES=%d num_frequency=%d\n",
	       IW_MAX_FREQUENCIES, range->num_frequency);

	range->num_channels = range->num_frequency;

	woal_sort_channels(&range->freq[0], range->num_frequency);

	/*
	 * Set an indication of the max TCP throughput in bit/s that we can
	 * expect using this interface
	 */
	if (i > 2)
		range->throughput = 5000 * 1000;
	else
		range->throughput = 1500 * 1000;

	range->min_rts = MLAN_RTS_MIN_VALUE;
	range->max_rts = MLAN_RTS_MAX_VALUE;
	range->min_frag = MLAN_FRAG_MIN_VALUE;
	range->max_frag = MLAN_FRAG_MAX_VALUE;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = 4;

/** Minimum power period */
#define IW_POWER_PERIOD_MIN 1000000	/* 1 sec */
/** Maximum power period */
#define IW_POWER_PERIOD_MAX 120000000	/* 2 min */
/** Minimum power timeout value */
#define IW_POWER_TIMEOUT_MIN 1000	/* 1 ms  */
/** Maximim power timeout value */
#define IW_POWER_TIMEOUT_MAX 1000000	/* 1 sec */

	/* Power Management duration & timeout */
	range->min_pmp = IW_POWER_PERIOD_MIN;
	range->max_pmp = IW_POWER_PERIOD_MAX;
	range->min_pmt = IW_POWER_TIMEOUT_MIN;
	range->max_pmt = IW_POWER_TIMEOUT_MAX;
	range->pmp_flags = IW_POWER_PERIOD;
	range->pmt_flags = IW_POWER_TIMEOUT;
	range->pm_capa = IW_POWER_PERIOD | IW_POWER_TIMEOUT | IW_POWER_ALL_R;

	/*
	 * Minimum version we recommend
	 */
	range->we_version_source = 15;

	/*
	 * Version we are compiled with
	 */
	range->we_version_compiled = WIRELESS_EXT;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT | IW_RETRY_MAX;

	range->min_retry = MLAN_TX_RETRY_MIN;
	range->max_retry = MLAN_TX_RETRY_MAX;

#if (WIRELESS_EXT >= 18)
	if (ap_cfg->protocol & PROTOCOL_WPA)
		range->enc_capa |= IW_ENC_CAPA_WPA;
	if (ap_cfg->protocol & PROTOCOL_WPA2)
		range->enc_capa |= IW_ENC_CAPA_WPA2;
	if (ap_cfg->wpa_cfg.pairwise_cipher_wpa == CIPHER_TKIP ||
	    ap_cfg->wpa_cfg.pairwise_cipher_wpa2 == CIPHER_TKIP ||
	    ap_cfg->wpa_cfg.group_cipher == CIPHER_TKIP)
		range->enc_capa |= IW_ENC_CAPA_CIPHER_TKIP;
	if (ap_cfg->wpa_cfg.pairwise_cipher_wpa == CIPHER_AES_CCMP ||
	    ap_cfg->wpa_cfg.pairwise_cipher_wpa2 == CIPHER_AES_CCMP ||
	    ap_cfg->wpa_cfg.group_cipher == CIPHER_AES_CCMP)
		range->enc_capa |= IW_ENC_CAPA_CIPHER_CCMP;
#endif
	kfree(ap_cfg);
	LEAVE();
	return 0;
}

/**
 *  @brief Set priv command
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return                     0 --success, otherwise fail
 */
static int
woal_set_priv(struct net_device *dev, struct iw_request_info *info,
	      struct iw_point *dwrq, char *extra)
{
	ENTER();
	LEAVE();
	return -EOPNOTSUPP;
}

/**
 *  @brief Set essid
 *
 *  @param dev          A pointer to net_device structure
 *  @param info         A pointer to iw_request_info structure
 *  @param dwrq         A pointer to iw_point structure
 *  @param extra        A pointer to extra data buf
 *
 *  @return             0--success, otherwise fail
 */
static int
woal_set_essid(struct net_device *dev, struct iw_request_info *info,
	       struct iw_point *dwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *sys_cfg = NULL;
	int ret = 0;

	ENTER();

	/* Check the size of the string */
	if (dwrq->length > IW_ESSID_MAX_SIZE + 1) {
		ret = -E2BIG;
		goto done;
	}
	sys_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!sys_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		ret = -EFAULT;
		goto done;
	}

	/* Initialize the invalid values so that the correct values
	 * below are downloaded to firmware */
	woal_set_sys_config_invalid_data(sys_cfg);

	/* Set the SSID */
#if WIRELESS_EXT > 20
	sys_cfg->ssid.ssid_len = dwrq->length;
#else
	sys_cfg->ssid.ssid_len = dwrq->length - 1;
#endif

	memcpy(sys_cfg->ssid.ssid, extra,
	       MIN(sys_cfg->ssid.ssid_len, MLAN_MAX_SSID_LENGTH));
	if (!sys_cfg->ssid.ssid_len || sys_cfg->ssid.ssid[0] < 0x20) {
		PRINTM(MERROR, "Invalid SSID - aborting set_essid\n");
		ret = -EINVAL;
		goto done;
	}
	PRINTM(MINFO, "Requested new SSID = %s\n",
	       (sys_cfg->ssid.ssid_len > 0) ?
	       (char *)sys_cfg->ssid.ssid : "NULL");

	/* Set AP configuration */
	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_SET,
							   MOAL_IOCTL_WAIT,
							   sys_cfg)) {
		PRINTM(MERROR, "Error setting AP confiruration\n");
		ret = -EFAULT;
		goto done;
	}

done:
	kfree(sys_cfg);
	LEAVE();
	return ret;
}

/**
 *  @brief Get current essid
 *
 *  @param dev      A pointer to net_device structure
 *  @param info     A pointer to iw_request_info structure
 *  @param dwrq     A pointer to iw_point structure
 *  @param extra    A pointer to extra data buf
 *
 *  @return         0--success, otherwise fail
 */
static int
woal_get_essid(struct net_device *dev, struct iw_request_info *info,
	       struct iw_point *dwrq, char *extra)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	mlan_uap_bss_param *ap_cfg = NULL;

	ENTER();

	ap_cfg = kzalloc(sizeof(mlan_uap_bss_param), GFP_ATOMIC);
	if (!ap_cfg) {
		PRINTM(MERROR, "Fail to alloc memory for mlan_uap_bss_param\n");
		return -EFAULT;
	}

	if (MLAN_STATUS_SUCCESS != woal_set_get_sys_config(priv,
							   MLAN_ACT_GET,
							   MOAL_IOCTL_WAIT,
							   ap_cfg)) {
		PRINTM(MERROR, "Error getting AP confiruration\n");
		kfree(ap_cfg);
		LEAVE();
		return -EFAULT;
	}

	if (priv->bss_started) {
		dwrq->length = MIN(dwrq->length, ap_cfg->ssid.ssid_len);
		memcpy(extra, ap_cfg->ssid.ssid, dwrq->length);
	} else
		dwrq->length = 0;

	dwrq->flags = 1;
	kfree(ap_cfg);
	LEAVE();
	return 0;
}

/**
 * iwconfig settable callbacks
 */
static const iw_handler woal_handler[] = {
	(iw_handler) woal_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) woal_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) woal_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) woal_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) woal_set_bss_mode,	/* SIOCSIWMODE */
	(iw_handler) woal_get_bss_mode,	/* SIOCGIWMODE */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) woal_get_range,	/* SIOCGIWRANGE */
	(iw_handler) woal_set_priv,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) NULL,	/* SIOCGIWSTATS */
#if WIRELESS_EXT > 15
#ifdef CONFIG_WEXT_SPY
	iw_handler_set_spy,	/* SIOCSIWSPY */
	iw_handler_get_spy,	/* SIOCGIWSPY */
	iw_handler_set_thrspy,	/* SIOCSIWTHRSPY */
	iw_handler_get_thrspy,	/* SIOCGIWTHRSPY */
#else
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
#endif
#else /* WIRELESS_EXT > 15 */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
#endif /* WIRELESS_EXT > 15 */
	(iw_handler) woal_set_wap,	/* SIOCSIWAP */
	(iw_handler) woal_get_wap,	/* SIOCGIWAP */
#if WIRELESS_EXT >= 18
	(iw_handler) woal_set_mlme,	/* SIOCSIWMLME  */
#else
	(iw_handler) NULL,	/* -- hole -- */
#endif
	/* (iw_handler) wlan_get_aplist, *//* SIOCGIWAPLIST */
	NULL,			/* SIOCGIWAPLIST */
#if WIRELESS_EXT > 13
	(iw_handler) NULL,	/* SIOCSIWSCAN */
	(iw_handler) NULL,	/* SIOCGIWSCAN */
#else /* WIRELESS_EXT > 13 */
	(iw_handler) NULL,	/* SIOCSIWSCAN */
	(iw_handler) NULL,	/* SIOCGIWSCAN */
#endif /* WIRELESS_EXT > 13 */
	(iw_handler) woal_set_essid,	/* SIOCSIWESSID */
	(iw_handler) woal_get_essid,	/* SIOCGIWESSID */
	(iw_handler) NULL,	/* SIOCSIWNICKN */
	(iw_handler) NULL,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* SIOCSIWRATE */
	(iw_handler) NULL,	/* SIOCGIWRATE */
	(iw_handler) NULL,	/* SIOCSIWRTS */
	(iw_handler) NULL,	/* SIOCGIWRTS */
	(iw_handler) NULL,	/* SIOCSIWFRAG */
	(iw_handler) NULL,	/* SIOCGIWFRAG */
	(iw_handler) NULL,	/* SIOCSIWTXPOW */
	(iw_handler) NULL,	/* SIOCGIWTXPOW */
	(iw_handler) NULL,	/* SIOCSIWRETRY */
	(iw_handler) NULL,	/* SIOCGIWRETRY */
	(iw_handler) woal_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) woal_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) NULL,	/* SIOCSIWPOWER */
	(iw_handler) NULL,	/* SIOCGIWPOWER */
#if (WIRELESS_EXT >= 18)
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) woal_set_gen_ie,	/* SIOCSIWGENIE */
	(iw_handler) woal_get_gen_ie,	/* SIOCGIWGENIE */
	(iw_handler) woal_set_auth,	/* SIOCSIWAUTH  */
	(iw_handler) woal_get_auth,	/* SIOCGIWAUTH  */
	(iw_handler) woal_set_encode_ext,	/* SIOCSIWENCODEEXT */
	(iw_handler) woal_get_encode_ext,	/* SIOCGIWENCODEEXT */
#endif /* WIRELESSS_EXT >= 18 */
};

/**
 * iwpriv settable callbacks
 */
static const iw_handler woal_private_handler[] = {
	NULL,			/* SIOCIWFIRSTPRIV */
};

/********************************************************
			Global Functions
********************************************************/

/** wlan_handler_def */
struct iw_handler_def woal_uap_handler_def = {
num_standard:ARRAY_SIZE(woal_handler),
num_private:ARRAY_SIZE(woal_private_handler),
num_private_args:ARRAY_SIZE(woal_uap_priv_args),
standard:(iw_handler *) woal_handler,
private:(iw_handler *) woal_private_handler,
private_args:(struct iw_priv_args *)woal_uap_priv_args,
#if WIRELESS_EXT > 20
get_wireless_stats:woal_get_uap_wireless_stats,
#endif
};

/**
 *  @brief Get wireless statistics
 *
 *  @param dev          A pointer to net_device structure
 *
 *  @return             A pointer to iw_statistics buf
 */
struct iw_statistics *
woal_get_uap_wireless_stats(struct net_device *dev)
{
	moal_private *priv = (moal_private *)netdev_priv(dev);
	t_u16 wait_option = MOAL_IOCTL_WAIT;

	ENTER();

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
	/*
	 * Since schedule() is not allowed from an atomic context
	 * such as when dev_base_lock for netdevices is acquired
	 * for reading/writing in kernel before this call, HostCmd
	 * is issued in non-blocking way in such contexts and
	 * blocking in other cases.
	 */
	if (in_atomic() || !write_can_lock(&dev_base_lock))
		wait_option = MOAL_NO_WAIT;
#endif

	priv->w_stats.qual.qual = 0;
	priv->w_stats.qual.level = 0;
	priv->w_stats.discard.code = 0;
	priv->w_stats.status = IW_MODE_MASTER;
	woal_uap_get_stats(priv, wait_option, NULL);

	LEAVE();
	return &priv->w_stats;
}
