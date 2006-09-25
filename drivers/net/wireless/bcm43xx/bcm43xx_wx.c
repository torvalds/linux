/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

  Some parts of the code in this file are derived from the ipw2200
  driver  Copyright(c) 2003 - 2004 Intel Corporation.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/ieee80211softmac.h>
#include <net/ieee80211softmac_wx.h>
#include <linux/capability.h>
#include <linux/sched.h> /* for capable() */
#include <linux/delay.h>

#include "bcm43xx.h"
#include "bcm43xx_wx.h"
#include "bcm43xx_main.h"
#include "bcm43xx_radio.h"
#include "bcm43xx_phy.h"


/* The WIRELESS_EXT version, which is implemented by this driver. */
#define BCM43xx_WX_VERSION	18

#define MAX_WX_STRING		80

static int bcm43xx_wx_get_name(struct net_device *net_dev,
                               struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int i;
	struct bcm43xx_phyinfo *phy;
	char suffix[7] = { 0 };
	int have_a = 0, have_b = 0, have_g = 0;

	mutex_lock(&bcm->mutex);
	for (i = 0; i < bcm->nr_80211_available; i++) {
		phy = &(bcm->core_80211_ext[i].phy);
		switch (phy->type) {
		case BCM43xx_PHYTYPE_A:
			have_a = 1;
			break;
		case BCM43xx_PHYTYPE_G:
			have_g = 1;
		case BCM43xx_PHYTYPE_B:
			have_b = 1;
			break;
		default:
			assert(0);
		}
	}
	mutex_unlock(&bcm->mutex);

	i = 0;
	if (have_a) {
		suffix[i++] = 'a';
		suffix[i++] = '/';
	}
	if (have_b) {
		suffix[i++] = 'b';
		suffix[i++] = '/';
	}
	if (have_g) {
		suffix[i++] = 'g';
		suffix[i++] = '/';
	}
	if (i != 0) 
		suffix[i - 1] = '\0';

	snprintf(data->name, IFNAMSIZ, "IEEE 802.11%s", suffix);

	return 0;
}

static int bcm43xx_wx_set_channelfreq(struct net_device *net_dev,
				      struct iw_request_info *info,
				      union iwreq_data *data,
				      char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	u8 channel;
	int freq;
	int err = -EINVAL;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);

	if ((data->freq.m >= 0) && (data->freq.m <= 1000)) {
		channel = data->freq.m;
		freq = bcm43xx_channel_to_freq(bcm, channel);
	} else {
		channel = bcm43xx_freq_to_channel(bcm, data->freq.m);
		freq = data->freq.m;
	}
	if (!ieee80211_is_valid_channel(bcm->ieee, channel))
		goto out_unlock;
	if (bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED) {
		//ieee80211softmac_disassoc(softmac, $REASON);
		bcm43xx_mac_suspend(bcm);
		err = bcm43xx_radio_selectchannel(bcm, channel, 0);
		bcm43xx_mac_enable(bcm);
	} else {
		bcm43xx_current_radio(bcm)->initial_channel = channel;
		err = 0;
	}
out_unlock:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_get_channelfreq(struct net_device *net_dev,
				      struct iw_request_info *info,
				      union iwreq_data *data,
				      char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct bcm43xx_radioinfo *radio;
	int err = -ENODEV;
	u16 channel;

	mutex_lock(&bcm->mutex);
	radio = bcm43xx_current_radio(bcm);
	channel = radio->channel;
	if (channel == 0xFF) {
		channel = radio->initial_channel;
		if (channel == 0xFF)
			goto out_unlock;
	}
	assert(channel > 0 && channel <= 1000);
	data->freq.e = 1;
	data->freq.m = bcm43xx_channel_to_freq(bcm, channel) * 100000;
	data->freq.flags = 1;

	err = 0;
out_unlock:
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_set_mode(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int mode;

	mode = data->mode;
	if (mode == IW_MODE_AUTO)
		mode = BCM43xx_INITIAL_IWMODE;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED) {
		if (bcm->ieee->iw_mode != mode)
			bcm43xx_set_iwmode(bcm, mode);
	} else
		bcm->ieee->iw_mode = mode;
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_get_mode(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	mutex_lock(&bcm->mutex);
	data->mode = bcm->ieee->iw_mode;
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_get_rangeparams(struct net_device *net_dev,
				      struct iw_request_info *info,
				      union iwreq_data *data,
				      char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct iw_range *range = (struct iw_range *)extra;
	const struct ieee80211_geo *geo;
	int i, j;
	struct bcm43xx_phyinfo *phy;

	data->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	//TODO: What about 802.11b?
	/* 54Mb/s == ~27Mb/s payload throughput (802.11g) */
	range->throughput = 27 * 1000 * 1000;

	range->max_qual.qual = 100;
	range->max_qual.level = 146; /* set floor at -110 dBm (146 - 256) */
	range->max_qual.noise = 146;
	range->max_qual.updated = IW_QUAL_ALL_UPDATED;

	range->avg_qual.qual = 50;
	range->avg_qual.level = 0;
	range->avg_qual.noise = 0;
	range->avg_qual.updated = IW_QUAL_ALL_UPDATED;

	range->min_rts = BCM43xx_MIN_RTS_THRESHOLD;
	range->max_rts = BCM43xx_MAX_RTS_THRESHOLD;
	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = WEP_KEYS;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = BCM43xx_WX_VERSION;

	range->enc_capa = IW_ENC_CAPA_WPA |
			  IW_ENC_CAPA_WPA2 |
			  IW_ENC_CAPA_CIPHER_TKIP |
			  IW_ENC_CAPA_CIPHER_CCMP;

	mutex_lock(&bcm->mutex);
	phy = bcm43xx_current_phy(bcm);

	range->num_bitrates = 0;
	i = 0;
	if (phy->type == BCM43xx_PHYTYPE_A ||
	    phy->type == BCM43xx_PHYTYPE_G) {
		range->num_bitrates = 8;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_6MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_9MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_12MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_18MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_24MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_36MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_48MB;
		range->bitrate[i++] = IEEE80211_OFDM_RATE_54MB;
	}
	if (phy->type == BCM43xx_PHYTYPE_B ||
	    phy->type == BCM43xx_PHYTYPE_G) {
		range->num_bitrates += 4;
		range->bitrate[i++] = IEEE80211_CCK_RATE_1MB;
		range->bitrate[i++] = IEEE80211_CCK_RATE_2MB;
		range->bitrate[i++] = IEEE80211_CCK_RATE_5MB;
		range->bitrate[i++] = IEEE80211_CCK_RATE_11MB;
	}

	geo = ieee80211_get_geo(bcm->ieee);
	range->num_channels = geo->a_channels + geo->bg_channels;
	j = 0;
	for (i = 0; i < geo->a_channels; i++) {
		if (j == IW_MAX_FREQUENCIES)
			break;
		range->freq[j].i = j + 1;
		range->freq[j].m = geo->a[i].freq;//FIXME?
		range->freq[j].e = 1;
		j++;
	}
	for (i = 0; i < geo->bg_channels; i++) {
		if (j == IW_MAX_FREQUENCIES)
			break;
		range->freq[j].i = j + 1;
		range->freq[j].m = geo->bg[i].freq;//FIXME?
		range->freq[j].e = 1;
		j++;
	}
	range->num_frequency = j;

	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_set_nick(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	size_t len;

	mutex_lock(&bcm->mutex);
	len =  min((size_t)data->data.length, (size_t)IW_ESSID_MAX_SIZE);
	memcpy(bcm->nick, extra, len);
	bcm->nick[len] = '\0';
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_get_nick(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	size_t len;

	mutex_lock(&bcm->mutex);
	len = strlen(bcm->nick);
	memcpy(extra, bcm->nick, len);
	data->data.length = (__u16)len;
	data->data.flags = 1;
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_set_rts(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int err = -EINVAL;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (data->rts.disabled) {
		bcm->rts_threshold = BCM43xx_MAX_RTS_THRESHOLD;
		err = 0;
	} else {
		if (data->rts.value >= BCM43xx_MIN_RTS_THRESHOLD &&
		    data->rts.value <= BCM43xx_MAX_RTS_THRESHOLD) {
			bcm->rts_threshold = data->rts.value;
			err = 0;
		}
	}
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_get_rts(struct net_device *net_dev,
			      struct iw_request_info *info,
			      union iwreq_data *data,
			      char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	mutex_lock(&bcm->mutex);
	data->rts.value = bcm->rts_threshold;
	data->rts.fixed = 0;
	data->rts.disabled = (bcm->rts_threshold == BCM43xx_MAX_RTS_THRESHOLD);
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_set_frag(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int err = -EINVAL;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (data->frag.disabled) {
		bcm->ieee->fts = MAX_FRAG_THRESHOLD;
		err = 0;
	} else {
		if (data->frag.value >= MIN_FRAG_THRESHOLD &&
		    data->frag.value <= MAX_FRAG_THRESHOLD) {
			bcm->ieee->fts = data->frag.value & ~0x1;
			err = 0;
		}
	}
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_get_frag(struct net_device *net_dev,
			       struct iw_request_info *info,
			       union iwreq_data *data,
			       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);

	mutex_lock(&bcm->mutex);
	data->frag.value = bcm->ieee->fts;
	data->frag.fixed = 0;
	data->frag.disabled = (bcm->ieee->fts == MAX_FRAG_THRESHOLD);
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_set_xmitpower(struct net_device *net_dev,
				    struct iw_request_info *info,
				    union iwreq_data *data,
				    char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct bcm43xx_radioinfo *radio;
	struct bcm43xx_phyinfo *phy;
	unsigned long flags;
	int err = -ENODEV;
	u16 maxpower;

	if ((data->txpower.flags & IW_TXPOW_TYPE) != IW_TXPOW_DBM) {
		printk(PFX KERN_ERR "TX power not in dBm.\n");
		return -EOPNOTSUPP;
	}

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED)
		goto out_unlock;
	radio = bcm43xx_current_radio(bcm);
	phy = bcm43xx_current_phy(bcm);
	if (data->txpower.disabled != (!(radio->enabled))) {
		if (data->txpower.disabled)
			bcm43xx_radio_turn_off(bcm);
		else
			bcm43xx_radio_turn_on(bcm);
	}
	if (data->txpower.value > 0) {
		/* desired and maxpower dBm values are in Q5.2 */
		if (phy->type == BCM43xx_PHYTYPE_A)
			maxpower = bcm->sprom.maxpower_aphy;
		else
			maxpower = bcm->sprom.maxpower_bgphy;
		radio->txpower_desired = limit_value(data->txpower.value << 2,
						     0, maxpower);
		bcm43xx_phy_xmitpower(bcm);
	}
	err = 0;

out_unlock:
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_get_xmitpower(struct net_device *net_dev,
				    struct iw_request_info *info,
				    union iwreq_data *data,
				    char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct bcm43xx_radioinfo *radio;
	int err = -ENODEV;

	mutex_lock(&bcm->mutex);
	if (bcm43xx_status(bcm) != BCM43xx_STAT_INITIALIZED)
		goto out_unlock;
	radio = bcm43xx_current_radio(bcm);
	/* desired dBm value is in Q5.2 */
	data->txpower.value = radio->txpower_desired >> 2;
	data->txpower.fixed = 1;
	data->txpower.flags = IW_TXPOW_DBM;
	data->txpower.disabled = !(radio->enabled);

	err = 0;
out_unlock:
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_set_encoding(struct net_device *net_dev,
				   struct iw_request_info *info,
				   union iwreq_data *data,
				   char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err;

	err = ieee80211_wx_set_encode(bcm->ieee, info, data, extra);

	return err;
}

static int bcm43xx_wx_set_encodingext(struct net_device *net_dev,
                                   struct iw_request_info *info,
                                   union iwreq_data *data,
                                   char *extra)
{
        struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
        int err;

        err = ieee80211_wx_set_encodeext(bcm->ieee, info, data, extra);

        return err;
}

static int bcm43xx_wx_get_encoding(struct net_device *net_dev,
				   struct iw_request_info *info,
				   union iwreq_data *data,
				   char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err;

	err = ieee80211_wx_get_encode(bcm->ieee, info, data, extra);

	return err;
}

static int bcm43xx_wx_get_encodingext(struct net_device *net_dev,
                                   struct iw_request_info *info,
                                   union iwreq_data *data,
                                   char *extra)
{
        struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
        int err;

        err = ieee80211_wx_get_encodeext(bcm->ieee, info, data, extra);

        return err;
}

static int bcm43xx_wx_set_interfmode(struct net_device *net_dev,
				     struct iw_request_info *info,
				     union iwreq_data *data,
				     char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int mode, err = 0;

	mode = *((int *)extra);
	switch (mode) {
	case 0:
		mode = BCM43xx_RADIO_INTERFMODE_NONE;
		break;
	case 1:
		mode = BCM43xx_RADIO_INTERFMODE_NONWLAN;
		break;
	case 2:
		mode = BCM43xx_RADIO_INTERFMODE_MANUALWLAN;
		break;
	case 3:
		mode = BCM43xx_RADIO_INTERFMODE_AUTOWLAN;
		break;
	default:
		printk(KERN_ERR PFX "set_interfmode allowed parameters are: "
				    "0 => None,  1 => Non-WLAN,  2 => WLAN,  "
				    "3 => Auto-WLAN\n");
		return -EINVAL;
	}

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	if (bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED) {
		err = bcm43xx_radio_set_interference_mitigation(bcm, mode);
		if (err) {
			printk(KERN_ERR PFX "Interference Mitigation not "
					    "supported by device\n");
		}
	} else {
		if (mode == BCM43xx_RADIO_INTERFMODE_AUTOWLAN) {
			printk(KERN_ERR PFX "Interference Mitigation mode Auto-WLAN "
					    "not supported while the interface is down.\n");
			err = -ENODEV;
		} else
			bcm43xx_current_radio(bcm)->interfmode = mode;
	}
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return err;
}

static int bcm43xx_wx_get_interfmode(struct net_device *net_dev,
				     struct iw_request_info *info,
				     union iwreq_data *data,
				     char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int mode;

	mutex_lock(&bcm->mutex);
	mode = bcm43xx_current_radio(bcm)->interfmode;
	mutex_unlock(&bcm->mutex);

	switch (mode) {
	case BCM43xx_RADIO_INTERFMODE_NONE:
		strncpy(extra, "0 (No Interference Mitigation)", MAX_WX_STRING);
		break;
	case BCM43xx_RADIO_INTERFMODE_NONWLAN:
		strncpy(extra, "1 (Non-WLAN Interference Mitigation)", MAX_WX_STRING);
		break;
	case BCM43xx_RADIO_INTERFMODE_MANUALWLAN:
		strncpy(extra, "2 (WLAN Interference Mitigation)", MAX_WX_STRING);
		break;
	default:
		assert(0);
	}
	data->data.length = strlen(extra) + 1;

	return 0;
}

static int bcm43xx_wx_set_shortpreamble(struct net_device *net_dev,
					struct iw_request_info *info,
					union iwreq_data *data,
					char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int on;

	on = *((int *)extra);
	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	bcm->short_preamble = !!on;
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_get_shortpreamble(struct net_device *net_dev,
					struct iw_request_info *info,
					union iwreq_data *data,
					char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int on;

	mutex_lock(&bcm->mutex);
	on = bcm->short_preamble;
	mutex_unlock(&bcm->mutex);

	if (on)
		strncpy(extra, "1 (Short Preamble enabled)", MAX_WX_STRING);
	else
		strncpy(extra, "0 (Short Preamble disabled)", MAX_WX_STRING);
	data->data.length = strlen(extra) + 1;

	return 0;
}

static int bcm43xx_wx_set_swencryption(struct net_device *net_dev,
				       struct iw_request_info *info,
				       union iwreq_data *data,
				       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	unsigned long flags;
	int on;
	
	on = *((int *)extra);

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	bcm->ieee->host_encrypt = !!on;
	bcm->ieee->host_decrypt = !!on;
	bcm->ieee->host_build_iv = !on;
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);

	return 0;
}

static int bcm43xx_wx_get_swencryption(struct net_device *net_dev,
				       struct iw_request_info *info,
				       union iwreq_data *data,
				       char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int on;

	mutex_lock(&bcm->mutex);
	on = bcm->ieee->host_encrypt;
	mutex_unlock(&bcm->mutex);

	if (on)
		strncpy(extra, "1 (SW encryption enabled) ", MAX_WX_STRING);
	else
		strncpy(extra, "0 (SW encryption disabled) ", MAX_WX_STRING);
	data->data.length = strlen(extra + 1);

	return 0;
}

/* Enough buffer to hold a hexdump of the sprom data. */
#define SPROM_BUFFERSIZE	512

static int sprom2hex(const u16 *sprom, char *dump)
{
	int i, pos = 0;

	for (i = 0; i < BCM43xx_SPROM_SIZE; i++) {
		pos += snprintf(dump + pos, SPROM_BUFFERSIZE - pos - 1,
				"%04X", swab16(sprom[i]) & 0xFFFF);
	}

	return pos + 1;
}

static int hex2sprom(u16 *sprom, const char *dump, unsigned int len)
{
	char tmp[5] = { 0 };
	int cnt = 0;
	unsigned long parsed;

	if (len < BCM43xx_SPROM_SIZE * sizeof(u16) * 2)
		return -EINVAL;
	while (cnt < BCM43xx_SPROM_SIZE) {
		memcpy(tmp, dump, 4);
		dump += 4;
		parsed = simple_strtoul(tmp, NULL, 16);
		sprom[cnt++] = swab16((u16)parsed);
	}

	return 0;
}

static int bcm43xx_wx_sprom_read(struct net_device *net_dev,
				 struct iw_request_info *info,
				 union iwreq_data *data,
				 char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err = -EPERM;
	u16 *sprom;
	unsigned long flags;

	if (!capable(CAP_SYS_RAWIO))
		goto out;

	err = -ENOMEM;
	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		goto out;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	err = -ENODEV;
	if (bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED)
		err = bcm43xx_sprom_read(bcm, sprom);
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
	if (!err)
		data->data.length = sprom2hex(sprom, extra);
	kfree(sprom);
out:
	return err;
}

static int bcm43xx_wx_sprom_write(struct net_device *net_dev,
				  struct iw_request_info *info,
				  union iwreq_data *data,
				  char *extra)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	int err = -EPERM;
	u16 *sprom;
	unsigned long flags;
	char *input;
	unsigned int len;

	if (!capable(CAP_SYS_RAWIO))
		goto out;

	err = -ENOMEM;
	sprom = kmalloc(BCM43xx_SPROM_SIZE * sizeof(*sprom),
			GFP_KERNEL);
	if (!sprom)
		goto out;

	len = data->data.length;
	extra[len - 1] = '\0';
	input = strchr(extra, ':');
	if (input) {
		input++;
		len -= input - extra;
	} else
		input = extra;
	err = hex2sprom(sprom, input, len);
	if (err)
		goto out_kfree;

	mutex_lock(&bcm->mutex);
	spin_lock_irqsave(&bcm->irq_lock, flags);
	spin_lock(&bcm->leds_lock);
	err = -ENODEV;
	if (bcm43xx_status(bcm) == BCM43xx_STAT_INITIALIZED)
		err = bcm43xx_sprom_write(bcm, sprom);
	spin_unlock(&bcm->leds_lock);
	spin_unlock_irqrestore(&bcm->irq_lock, flags);
	mutex_unlock(&bcm->mutex);
out_kfree:
	kfree(sprom);
out:
	return err;
}

/* Get wireless statistics.  Called by /proc/net/wireless and by SIOCGIWSTATS */

static struct iw_statistics *bcm43xx_get_wireless_stats(struct net_device *net_dev)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(net_dev);
	struct ieee80211softmac_device *mac = ieee80211_priv(net_dev);
	struct iw_statistics *wstats;
	struct ieee80211_network *network = NULL;
	static int tmp_level = 0;
	static int tmp_qual = 0;
	unsigned long flags;

	wstats = &bcm->stats.wstats;
	if (!mac->associnfo.associated) {
		wstats->miss.beacon = 0;
//		bcm->ieee->ieee_stats.tx_retry_limit_exceeded = 0; // FIXME: should this be cleared here?
		wstats->discard.retries = 0;
//		bcm->ieee->ieee_stats.tx_discards_wrong_sa = 0; // FIXME: same question
		wstats->discard.nwid = 0;
//		bcm->ieee->ieee_stats.rx_discards_undecryptable = 0; // FIXME: ditto
		wstats->discard.code = 0;
//		bcm->ieee->ieee_stats.rx_fragments = 0;  // FIXME: same here
		wstats->discard.fragment = 0;
		wstats->discard.misc = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = 7;
		wstats->qual.updated |= IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
		return wstats;
	}
	/* fill in the real statistics when iface associated */
	spin_lock_irqsave(&mac->ieee->lock, flags);
	list_for_each_entry(network, &mac->ieee->network_list, list) {
		if (!memcmp(mac->associnfo.bssid, network->bssid, ETH_ALEN)) {
			if (!tmp_level)	{	/* get initial values */
				tmp_level = network->stats.signal;
				tmp_qual = network->stats.rssi;
			} else {		/* smooth results */
				tmp_level = (15 * tmp_level + network->stats.signal)/16;
				tmp_qual = (15 * tmp_qual + network->stats.rssi)/16;
			}
			break;
		}
	}
	spin_unlock_irqrestore(&mac->ieee->lock, flags);
	wstats->qual.level = tmp_level;
	wstats->qual.qual = 100 * tmp_qual / RX_RSSI_MAX;
	wstats->qual.noise = bcm->stats.noise;
	wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;
	wstats->discard.code = bcm->ieee->ieee_stats.rx_discards_undecryptable;
	wstats->discard.retries = bcm->ieee->ieee_stats.tx_retry_limit_exceeded;
	wstats->discard.nwid = bcm->ieee->ieee_stats.tx_discards_wrong_sa;
	wstats->discard.fragment = bcm->ieee->ieee_stats.rx_fragments;
	wstats->discard.misc = 0;	// FIXME
	wstats->miss.beacon = 0;	// FIXME
	return wstats;
}


#ifdef WX
# undef WX
#endif
#define WX(ioctl)  [(ioctl) - SIOCSIWCOMMIT]
static const iw_handler bcm43xx_wx_handlers[] = {
	/* Wireless Identification */
	WX(SIOCGIWNAME)		= bcm43xx_wx_get_name,
	/* Basic operations */
	WX(SIOCSIWFREQ)		= bcm43xx_wx_set_channelfreq,
	WX(SIOCGIWFREQ)		= bcm43xx_wx_get_channelfreq,
	WX(SIOCSIWMODE)		= bcm43xx_wx_set_mode,
	WX(SIOCGIWMODE)		= bcm43xx_wx_get_mode,
	/* Informative stuff */
	WX(SIOCGIWRANGE)	= bcm43xx_wx_get_rangeparams,
	/* Access Point manipulation */
	WX(SIOCSIWAP)           = ieee80211softmac_wx_set_wap,
	WX(SIOCGIWAP)           = ieee80211softmac_wx_get_wap,
	WX(SIOCSIWSCAN)		= ieee80211softmac_wx_trigger_scan,
	WX(SIOCGIWSCAN)		= ieee80211softmac_wx_get_scan_results,
	/* 802.11 specific support */
	WX(SIOCSIWESSID)	= ieee80211softmac_wx_set_essid,
	WX(SIOCGIWESSID)	= ieee80211softmac_wx_get_essid,
	WX(SIOCSIWNICKN)	= bcm43xx_wx_set_nick,
	WX(SIOCGIWNICKN)	= bcm43xx_wx_get_nick,
	/* Other parameters */
	WX(SIOCSIWRATE)		= ieee80211softmac_wx_set_rate,
	WX(SIOCGIWRATE)		= ieee80211softmac_wx_get_rate,
	WX(SIOCSIWRTS)		= bcm43xx_wx_set_rts,
	WX(SIOCGIWRTS)		= bcm43xx_wx_get_rts,
	WX(SIOCSIWFRAG)		= bcm43xx_wx_set_frag,
	WX(SIOCGIWFRAG)		= bcm43xx_wx_get_frag,
	WX(SIOCSIWTXPOW)	= bcm43xx_wx_set_xmitpower,
	WX(SIOCGIWTXPOW)	= bcm43xx_wx_get_xmitpower,
//TODO	WX(SIOCSIWRETRY)	= bcm43xx_wx_set_retry,
//TODO	WX(SIOCGIWRETRY)	= bcm43xx_wx_get_retry,
	/* Encoding */
	WX(SIOCSIWENCODE)	= bcm43xx_wx_set_encoding,
	WX(SIOCGIWENCODE)	= bcm43xx_wx_get_encoding,
	WX(SIOCSIWENCODEEXT)	= bcm43xx_wx_set_encodingext,
	WX(SIOCGIWENCODEEXT)	= bcm43xx_wx_get_encodingext,
	/* Power saving */
//TODO	WX(SIOCSIWPOWER)	= bcm43xx_wx_set_power,
//TODO	WX(SIOCGIWPOWER)	= bcm43xx_wx_get_power,
	WX(SIOCSIWGENIE)	= ieee80211softmac_wx_set_genie,
	WX(SIOCGIWGENIE)	= ieee80211softmac_wx_get_genie,
	WX(SIOCSIWAUTH)		= ieee80211_wx_set_auth,
	WX(SIOCGIWAUTH)		= ieee80211_wx_get_auth,
};
#undef WX

static const iw_handler bcm43xx_priv_wx_handlers[] = {
	/* Set Interference Mitigation Mode. */
	bcm43xx_wx_set_interfmode,
	/* Get Interference Mitigation Mode. */
	bcm43xx_wx_get_interfmode,
	/* Enable/Disable Short Preamble mode. */
	bcm43xx_wx_set_shortpreamble,
	/* Get Short Preamble mode. */
	bcm43xx_wx_get_shortpreamble,
	/* Enable/Disable Software Encryption mode */
	bcm43xx_wx_set_swencryption,
	/* Get Software Encryption mode */
	bcm43xx_wx_get_swencryption,
	/* Write SRPROM data. */
	bcm43xx_wx_sprom_write,
	/* Read SPROM data. */
	bcm43xx_wx_sprom_read,
};

#define PRIV_WX_SET_INTERFMODE		(SIOCIWFIRSTPRIV + 0)
#define PRIV_WX_GET_INTERFMODE		(SIOCIWFIRSTPRIV + 1)
#define PRIV_WX_SET_SHORTPREAMBLE	(SIOCIWFIRSTPRIV + 2)
#define PRIV_WX_GET_SHORTPREAMBLE	(SIOCIWFIRSTPRIV + 3)
#define PRIV_WX_SET_SWENCRYPTION	(SIOCIWFIRSTPRIV + 4)
#define PRIV_WX_GET_SWENCRYPTION	(SIOCIWFIRSTPRIV + 5)
#define PRIV_WX_SPROM_WRITE		(SIOCIWFIRSTPRIV + 6)
#define PRIV_WX_SPROM_READ		(SIOCIWFIRSTPRIV + 7)

#define PRIV_WX_DUMMY(ioctl)	\
	{					\
		.cmd		= (ioctl),	\
		.name		= "__unused"	\
	}

static const struct iw_priv_args bcm43xx_priv_wx_args[] = {
	{
		.cmd		= PRIV_WX_SET_INTERFMODE,
		.set_args	= IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		.name		= "set_interfmode",
	},
	{
		.cmd		= PRIV_WX_GET_INTERFMODE,
		.get_args	= IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		.name		= "get_interfmode",
	},
	{
		.cmd		= PRIV_WX_SET_SHORTPREAMBLE,
		.set_args	= IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		.name		= "set_shortpreamb",
	},
	{
		.cmd		= PRIV_WX_GET_SHORTPREAMBLE,
		.get_args	= IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		.name		= "get_shortpreamb",
	},
	{
		.cmd		= PRIV_WX_SET_SWENCRYPTION,
		.set_args	= IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
		.name		= "set_swencrypt",
	},
	{
		.cmd		= PRIV_WX_GET_SWENCRYPTION,
		.get_args	= IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
		.name		= "get_swencrypt",
	},
	{
		.cmd		= PRIV_WX_SPROM_WRITE,
		.set_args	= IW_PRIV_TYPE_CHAR | SPROM_BUFFERSIZE,
		.name		= "write_sprom",
	},
	{
		.cmd		= PRIV_WX_SPROM_READ,
		.get_args	= IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | SPROM_BUFFERSIZE,
		.name		= "read_sprom",
	},
};

const struct iw_handler_def bcm43xx_wx_handlers_def = {
	.standard		= bcm43xx_wx_handlers,
	.num_standard		= ARRAY_SIZE(bcm43xx_wx_handlers),
	.num_private		= ARRAY_SIZE(bcm43xx_priv_wx_handlers),
	.num_private_args	= ARRAY_SIZE(bcm43xx_priv_wx_args),
	.private		= bcm43xx_priv_wx_handlers,
	.private_args		= bcm43xx_priv_wx_args,
	.get_wireless_stats	= bcm43xx_get_wireless_stats,
};
