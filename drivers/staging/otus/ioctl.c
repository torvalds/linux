/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
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
/*                                                                      */
/*  Module Name : ioctl.c                                               */
/*                                                                      */
/*  Abstract                                                            */
/*      This module contains Linux wireless extension related functons. */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/if_arp.h>
#include <linux/uaccess.h>

#include "usbdrv.h"

#define ZD_IOCTL_WPA			    (SIOCDEVPRIVATE + 1)
#define ZD_IOCTL_PARAM			    (SIOCDEVPRIVATE + 2)
#define ZD_IOCTL_GETWPAIE		    (SIOCDEVPRIVATE + 3)
#ifdef ZM_ENABLE_CENC
#define ZM_IOCTL_CENC               (SIOCDEVPRIVATE + 4)
#endif  /* ZM_ENABLE_CENC */
#define ZD_PARAM_ROAMING		    0x0001
#define ZD_PARAM_PRIVACY		    0x0002
#define ZD_PARAM_WPA			    0x0003
#define ZD_PARAM_COUNTERMEASURES	0x0004
#define ZD_PARAM_DROPUNENCRYPTED	0x0005
#define ZD_PARAM_AUTH_ALGS		    0x0006
#define ZD_PARAM_WPS_FILTER		    0x0007

#ifdef ZM_ENABLE_CENC
#define P80211_PACKET_CENCFLAG		0x0001
#endif  /* ZM_ENABLE_CENC */
#define P80211_PACKET_SETKEY     	0x0003

#define ZD_CMD_SET_ENCRYPT_KEY		0x0001
#define ZD_CMD_SET_MLME			    0x0002
#define ZD_CMD_SCAN_REQ			    0x0003
#define ZD_CMD_SET_GENERIC_ELEMENT	0x0004
#define ZD_CMD_GET_TSC			    0x0005

#define ZD_CRYPT_ALG_NAME_LEN		16
#define ZD_MAX_KEY_SIZE			    32
#define ZD_MAX_GENERIC_SIZE		    64

#include <net/iw_handler.h>

extern u16_t zfLnxGetVapId(zdev_t *dev);

static const u32_t channel_frequency_11A[] =
{
	/* Even element for Channel Number, Odd for Frequency */
	36, 5180,
	40, 5200,
	44, 5220,
	48, 5240,
	52, 5260,
	56, 5280,
	60, 5300,
	64, 5320,
	100, 5500,
	104, 5520,
	108, 5540,
	112, 5560,
	116, 5580,
	120, 5600,
	124, 5620,
	128, 5640,
	132, 5660,
	136, 5680,
	140, 5700,
	/**/
	184, 4920,
	188, 4940,
	192, 4960,
	196, 4980,
	8, 5040,
	12, 5060,
	16, 5080,
	34, 5170,
	38, 5190,
	42, 5210,
	46, 5230,
	/**/
	149, 5745,
	153, 5765,
	157, 5785,
	161, 5805,
	165, 5825
	/**/
};

int usbdrv_freq2chan(u32_t freq)
{
	/* 2.4G Hz */
	if (freq > 2400 && freq < 3000) {
		return ((freq-2412)/5) + 1;
	} else {
		u16_t ii;
		u16_t num_chan = sizeof(channel_frequency_11A)/sizeof(u32_t);

		for (ii = 1; ii < num_chan; ii += 2) {
			if (channel_frequency_11A[ii] == freq)
				return channel_frequency_11A[ii-1];
		}
	}

	return 0;
}

int usbdrv_chan2freq(int chan)
{
	int freq;

	/* If channel number is out of range */
	if (chan > 165 || chan <= 0)
		return -1;

	/* 2.4G band */
	if (chan >= 1 && chan <= 13) {
		freq = (2412 + (chan - 1) * 5);
			return freq;
	} else if (chan >= 36 && chan <= 165) {
		u16_t ii;
		u16_t num_chan = sizeof(channel_frequency_11A)/sizeof(u32_t);

		for (ii = 0; ii < num_chan; ii += 2) {
			if (channel_frequency_11A[ii] == chan)
				return channel_frequency_11A[ii+1];
		}

	/* Can't find desired frequency */
	if (ii == num_chan)
		return -1;
	}

	/* Can't find deisred frequency */
	return -1;
}

int usbdrv_ioctl_setessid(struct net_device *dev, struct iw_point *erq)
{
	#ifdef ZM_HOSTAPD_SUPPORT
	/* struct usbdrv_private *macp = dev->ml_priv; */
	char essidbuf[IW_ESSID_MAX_SIZE+1];
	int i;

	if (!netif_running(dev))
		return -EINVAL;

	memset(essidbuf, 0, sizeof(essidbuf));

	printk(KERN_ERR "usbdrv_ioctl_setessid\n");

	/* printk("ssidlen=%d\n", erq->length); //for any, it is 1. */
	if (erq->flags) {
		if (erq->length > (IW_ESSID_MAX_SIZE+1))
			return -E2BIG;

		if (copy_from_user(essidbuf, erq->pointer, erq->length))
			return -EFAULT;
	}

	/* zd_DisasocAll(2); */
	/* wait_ms(100); */

	printk(KERN_ERR "essidbuf: ");

	for (i = 0; i < erq->length; i++)
		printk(KERN_ERR "%02x ", essidbuf[i]);

	printk(KERN_ERR "\n");

	essidbuf[erq->length] = '\0';
	/* memcpy(macp->wd.ws.ssid, essidbuf, erq->length); */
	/* macp->wd.ws.ssidLen = strlen(essidbuf)+2; */
	/* macp->wd.ws.ssid[1] = strlen(essidbuf); Update ssid length */

	zfiWlanSetSSID(dev, essidbuf, erq->length);
	#if 0
		printk(KERN_ERR "macp->wd.ws.ssid: ");

		for (i = 0; i < macp->wd.ws.ssidLen; i++)
			printk(KERN_ERR "%02x ", macp->wd.ws.ssid[i]);

		printk(KERN_ERR "\n");
	#endif

	zfiWlanDisable(dev, 0);
	zfiWlanEnable(dev);

	#endif

	return 0;
}

int usbdrv_ioctl_getessid(struct net_device *dev, struct iw_point *erq)
{
	/* struct usbdrv_private *macp = dev->ml_priv; */
	u8_t essidbuf[IW_ESSID_MAX_SIZE+1];
	u8_t len;
	u8_t i;


	/* len = macp->wd.ws.ssidLen; */
	/* memcpy(essidbuf, macp->wd.ws.ssid, macp->wd.ws.ssidLen); */
	zfiWlanQuerySSID(dev, essidbuf, &len);

	essidbuf[len] = 0;

	printk(KERN_ERR "ESSID: ");

	for (i = 0; i < len; i++)
		printk(KERN_ERR "%c", essidbuf[i]);

	printk(KERN_ERR "\n");

	erq->flags = 1;
	erq->length = strlen(essidbuf) + 1;

	if (erq->pointer) {
		if (copy_to_user(erq->pointer, essidbuf, erq->length))
			return -EFAULT;
	}

	return 0;
}

int usbdrv_ioctl_setrts(struct net_device *dev, struct iw_param *rrq)
{
	return 0;
}

/*
 * Encode a WPA or RSN information element as a custom
 * element using the hostap format.
 */
u32 encode_ie(void *buf, u32 bufsize, const u8 *ie, u32 ielen,
		const u8 *leader, u32 leader_len)
{
	u8 *p;
	u32 i;

	if (bufsize < leader_len)
		return 0;
	p = buf;
	memcpy(p, leader, leader_len);
	bufsize -= leader_len;
	p += leader_len;
	for (i = 0; i < ielen && bufsize > 2; i++)
		p += sprintf(p, "%02x", ie[i]);
	return (i == ielen ? p - (u8 *)buf:0);
}

/*
 * Translate scan data returned from the card to a card independent
 * format that the Wireless Tools will understand
 */
char *usbdrv_translate_scan(struct net_device *dev,
	struct iw_request_info *info, char *current_ev,
	char *end_buf, struct zsBssInfo *list)
{
	struct iw_event iwe;   /* Temporary buffer */
	u16_t capabilities;
	char *current_val;     /* For rates */
	char *last_ev;
	int i;
	char    buf[64*2 + 30];

	last_ev = current_ev;

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, list->bssid, ETH_ALEN);
	current_ev = iwe_stream_add_event(info,	current_ev,
					end_buf, &iwe, IW_EV_ADDR_LEN);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	iwe.u.data.length = list->ssid[1];
	if (iwe.u.data.length > 32)
		iwe.u.data.length = 32;
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(info,	current_ev,
					end_buf, &iwe, &list->ssid[2]);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	capabilities = (list->capability[1] << 8) + list->capability[0];
	if (capabilities & (0x01 | 0x02)) {
		if (capabilities & 0x01)
			iwe.u.mode = IW_MODE_MASTER;
		else
			iwe.u.mode = IW_MODE_ADHOC;
			current_ev = iwe_stream_add_event(info,	current_ev,
						end_buf, &iwe, IW_EV_UINT_LEN);
	}

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Add frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = list->channel;
	/* Channel frequency in KHz */
	if (iwe.u.freq.m > 14) {
		if ((184 <= iwe.u.freq.m) && (iwe.u.freq.m <= 196))
			iwe.u.freq.m = 4000 + iwe.u.freq.m * 5;
		else
			iwe.u.freq.m = 5000 + iwe.u.freq.m * 5;
	} else {
		if (iwe.u.freq.m == 14)
			iwe.u.freq.m = 2484;
		else
			iwe.u.freq.m = 2412 + (iwe.u.freq.m - 1) * 5;
	}
	iwe.u.freq.e = 6;
	current_ev = iwe_stream_add_event(info, current_ev,
					end_buf, &iwe, IW_EV_FREQ_LEN);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_NOISE_UPDATED;
	iwe.u.qual.level = list->signalStrength;
	iwe.u.qual.noise = 0;
	iwe.u.qual.qual = list->signalQuality;
	current_ev = iwe_stream_add_event(info,	current_ev,
					end_buf, &iwe, IW_EV_QUAL_LEN);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Add encryption capability */

	iwe.cmd = SIOCGIWENCODE;
	if (capabilities & 0x10)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;

	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info,	current_ev,
					end_buf, &iwe, list->ssid);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	/* Rate : stuffing multiple values in a single event require a bit
	* more of magic
	*/
	current_val = current_ev + IW_EV_LCP_LEN;

	iwe.cmd = SIOCGIWRATE;
	/* Those two flags are ignored... */
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

	for (i = 0 ; i < list->supportedRates[1] ; i++) {
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = ((list->supportedRates[i+2] & 0x7f)
					* 500000);
		/* Add new value to event */
		current_val = iwe_stream_add_value(info, current_ev,
				current_val, end_buf, &iwe, IW_EV_PARAM_LEN);

		/* Ran out of buffer */
		if (last_ev == current_val)
			return end_buf;

		last_ev = current_val;
	}

	for (i = 0 ; i < list->extSupportedRates[1] ; i++) {
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = ((list->extSupportedRates[i+2] & 0x7f)
					* 500000);
		/* Add new value to event */
		current_val = iwe_stream_add_value(info, current_ev,
				current_val, end_buf, &iwe, IW_EV_PARAM_LEN);

		/* Ran out of buffer */
		if (last_ev == current_val)
			return end_buf;

		last_ev = current_ev;
	}

	/* Check if we added any event */
	if ((current_val - current_ev) > IW_EV_LCP_LEN)
		current_ev = current_val;
		#define IEEE80211_ELEMID_RSN 0x30
	memset(&iwe, 0, sizeof(iwe));
	iwe.cmd = IWEVCUSTOM;
	snprintf(buf, sizeof(buf), "bcn_int=%d", (list->beaconInterval[1] << 8)
						+ list->beaconInterval[0]);
	iwe.u.data.length = strlen(buf);
	current_ev = iwe_stream_add_point(info, current_ev,
						end_buf, &iwe, buf);

	/* Ran out of buffer */
	if (last_ev == current_ev)
		return end_buf;

	last_ev = current_ev;

	if (list->wpaIe[1] != 0) {
		static const char rsn_leader[] = "rsn_ie=";
		static const char wpa_leader[] = "wpa_ie=";

		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		if (list->wpaIe[0] == IEEE80211_ELEMID_RSN)
			iwe.u.data.length = encode_ie(buf, sizeof(buf),
					list->wpaIe, list->wpaIe[1]+2,
					rsn_leader, sizeof(rsn_leader)-1);
		else
			iwe.u.data.length = encode_ie(buf, sizeof(buf),
					list->wpaIe, list->wpaIe[1]+2,
					wpa_leader, sizeof(wpa_leader)-1);

		if (iwe.u.data.length != 0)
			current_ev = iwe_stream_add_point(info, current_ev,
							end_buf, &iwe, buf);

		/* Ran out of buffer */
		if (last_ev == current_ev)
			return end_buf;

		last_ev = current_ev;
	}

	if (list->rsnIe[1] != 0) {
		static const char rsn_leader[] = "rsn_ie=";
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;

		if (list->rsnIe[0] == IEEE80211_ELEMID_RSN) {
			iwe.u.data.length = encode_ie(buf, sizeof(buf),
			list->rsnIe, list->rsnIe[1]+2,
			rsn_leader, sizeof(rsn_leader)-1);
			if (iwe.u.data.length != 0)
				current_ev = iwe_stream_add_point(info,
					current_ev, end_buf,  &iwe, buf);

			/* Ran out of buffer */
			if (last_ev == current_ev)
				return end_buf;

			last_ev = current_ev;
		}
	}
	/* The other data in the scan result are not really
	* interesting, so for now drop it
	*/
	return current_ev;
}

int usbdrvwext_giwname(struct net_device *dev,
		struct iw_request_info *info,
		union iwreq_data *wrq, char *extra)
{
	/* struct usbdrv_private *macp = dev->ml_priv; */

	strcpy(wrq->name, "IEEE 802.11-MIMO");

	return 0;
}

int usbdrvwext_siwfreq(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_freq *freq, char *extra)
{
	u32_t FreqKHz;
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (freq->e > 1)
		return -EINVAL;

	if (freq->e == 1) {
		FreqKHz = (freq->m / 100000);

		if (FreqKHz > 4000000) {
			if (FreqKHz > 5825000)
				FreqKHz = 5825000;
			else if (FreqKHz < 4920000)
				FreqKHz = 4920000;
			else if (FreqKHz < 5000000)
				FreqKHz = (((FreqKHz - 4000000) / 5000) * 5000)
						+ 4000000;
			else
				FreqKHz = (((FreqKHz - 5000000) / 5000) * 5000)
						+ 5000000;
		} else {
			if (FreqKHz > 2484000)
				FreqKHz = 2484000;
			else if (FreqKHz < 2412000)
				FreqKHz = 2412000;
			else
				FreqKHz = (((FreqKHz - 2412000) / 5000) * 5000)
						+ 2412000;
		}
	} else {
		FreqKHz = usbdrv_chan2freq(freq->m);

		if (FreqKHz != -1)
			FreqKHz *= 1000;
		else
			FreqKHz = 2412000;
	}

	/* printk("freq->m: %d, freq->e: %d\n", freq->m, freq->e); */
	/* printk("FreqKHz: %d\n", FreqKHz); */

	if (macp->DeviceOpened == 1) {
		zfiWlanSetFrequency(dev, FreqKHz, 0); /* Immediate */
		/* u8_t wpaieLen,wpaie[50]; */
		/* zfiWlanQueryWpaIe(dev, wpaie, &wpaieLen); */
		zfiWlanDisable(dev, 0);
		zfiWlanEnable(dev);
		/* if (wpaieLen > 2) */
		/* zfiWlanSetWpaIe(dev, wpaie, wpaieLen); */
	}

	return 0;
}

int usbdrvwext_giwfreq(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_freq *freq, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (macp->DeviceOpened != 1)
		return 0;

	freq->m = zfiWlanQueryFrequency(dev);
	freq->e = 3;

	return 0;
}

int usbdrvwext_siwmode(struct net_device *dev,
		struct iw_request_info *info,
		union iwreq_data *wrq, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t WlanMode;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	switch (wrq->mode) {
	case IW_MODE_MASTER:
		WlanMode = ZM_MODE_AP;
		break;
	case IW_MODE_INFRA:
		WlanMode = ZM_MODE_INFRASTRUCTURE;
		break;
	case IW_MODE_ADHOC:
		WlanMode = ZM_MODE_IBSS;
		break;
	default:
		WlanMode = ZM_MODE_IBSS;
		break;
	}

	zfiWlanSetWlanMode(dev, WlanMode);
	zfiWlanDisable(dev, 1);
	zfiWlanEnable(dev);

	return 0;
}

int usbdrvwext_giwmode(struct net_device *dev,
	struct iw_request_info *info,
	__u32 *mode, char *extra)
{
	unsigned long irqFlag;
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	spin_lock_irqsave(&macp->cs_lock, irqFlag);

	switch (zfiWlanQueryWlanMode(dev)) {
	case ZM_MODE_AP:
		*mode = IW_MODE_MASTER;
		break;
	case ZM_MODE_INFRASTRUCTURE:
		*mode = IW_MODE_INFRA;
		break;
	case ZM_MODE_IBSS:
		*mode = IW_MODE_ADHOC;
		break;
	default:
		*mode = IW_MODE_ADHOC;
		break;
	}

	spin_unlock_irqrestore(&macp->cs_lock, irqFlag);

	return 0;
}

int usbdrvwext_siwsens(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *sens, char *extra)
{
	return 0;
}

int usbdrvwext_giwsens(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *sens, char *extra)
{
	sens->value = 0;
	sens->fixed = 1;

	return 0;
}

int usbdrvwext_giwrange(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *data, char *extra)
{
	struct iw_range *range = (struct iw_range *) extra;
	int i, val;
	/* int num_band_a; */
	u16_t channels[60];
	u16_t channel_num;

	if (!netif_running(dev))
		return -EINVAL;

	range->txpower_capa = IW_TXPOW_DBM;
	/* XXX what about min/max_pmp, min/max_pmt, etc. */

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 13;

	range->retry_capa = IW_RETRY_LIMIT;
	range->retry_flags = IW_RETRY_LIMIT;
	range->min_retry = 0;
	range->max_retry = 255;

	channel_num = zfiWlanQueryAllowChannels(dev, channels);

	/* Gurantee reported channel numbers is less
	* or equal to IW_MAX_FREQUENCIES
	*/
	if (channel_num > IW_MAX_FREQUENCIES)
		channel_num = IW_MAX_FREQUENCIES;

	val = 0;

	for (i = 0; i < channel_num; i++) {
		range->freq[val].i = usbdrv_freq2chan(channels[i]);
		range->freq[val].m = channels[i];
		range->freq[val].e = 6;
		val++;
	}

	range->num_channels = channel_num;
	range->num_frequency = channel_num;

	#if 0
	range->num_channels = 14; /* Only 2.4G */

	/* XXX need to filter against the regulatory domain &| active set */
	val = 0;
	/* B,G Bands */
	for (i = 1; i <= 14; i++) {
		range->freq[val].i = i;
		if (i == 14)
			range->freq[val].m = 2484000;
		else
			range->freq[val].m = (2412+(i-1)*5)*1000;
		range->freq[val].e = 3;
		val++;
	}

	num_band_a = (IW_MAX_FREQUENCIES - val);
	/* A Bands */
	for (i = 0; i < num_band_a; i++) {
		range->freq[val].i = channel_frequency_11A[2 * i];
		range->freq[val].m = channel_frequency_11A[2 * i + 1] * 1000;
		range->freq[val].e = 3;
		val++;
	}
	/* MIMO Rate Not Defined Now
	* For 802.11a, there are too more frequency.
	* We can't return them all.
	*/
	range->num_frequency = val;
	#endif

	/* Max of /proc/net/wireless */
	range->max_qual.qual = 100;  /* ??  92; */
	range->max_qual.level = 154; /* ?? */
	range->max_qual.noise = 154; /* ?? */
	range->sensitivity = 3;      /* ?? */

	/* XXX these need to be nsd-specific! */
	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;
	range->max_encoding_tokens = 4 /* NUM_WEPKEYS ?? */;
	range->num_encoding_sizes = 2; /* ?? */

	range->encoding_size[0] = 5; /* ?? WEP Key Encoding Size */
	range->encoding_size[1] = 13; /* ?? */

	/* XXX what about num_bitrates/throughput? */
	range->num_bitrates = 0; /* ?? */

	/* estimated max throughput
	* XXX need to cap it if we're running at ~2Mbps..
	*/

	range->throughput = 300000000;

	return 0;
}

int usbdrvwext_siwap(struct net_device *dev, struct iw_request_info *info,
		struct sockaddr *MacAddr, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (zfiWlanQueryWlanMode(dev) == ZM_MODE_AP) {
		/* AP Mode */
		zfiWlanSetMacAddress(dev, (u16_t *)&MacAddr->sa_data[0]);
	} else {
		/* STA Mode */
		zfiWlanSetBssid(dev, &MacAddr->sa_data[0]);
	}

	if (macp->DeviceOpened == 1) {
		/* u8_t wpaieLen,wpaie[80]; */
		/* zfiWlanQueryWpaIe(dev, wpaie, &wpaieLen); */
		zfiWlanDisable(dev, 0);
		zfiWlanEnable(dev);
		/* if (wpaieLen > 2) */
		/* zfiWlanSetWpaIe(dev, wpaie, wpaieLen); */
	}

	return 0;
}

int usbdrvwext_giwap(struct net_device *dev,
		struct iw_request_info *info,
		struct sockaddr *MacAddr, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (macp->DeviceOpened != 1)
		return 0;

	if (zfiWlanQueryWlanMode(dev) == ZM_MODE_AP) {
		/* AP Mode */
		zfiWlanQueryMacAddress(dev, &MacAddr->sa_data[0]);
	} else {
		/* STA Mode */
		if (macp->adapterState == ZM_STATUS_MEDIA_CONNECT) {
			zfiWlanQueryBssid(dev, &MacAddr->sa_data[0]);
		} else {
			u8_t zero_addr[6] = { 0x00, 0x00, 0x00, 0x00,
								0x00, 0x00 };
			memcpy(&MacAddr->sa_data[0], zero_addr,
							sizeof(zero_addr));
		}
	}

	return 0;
}

int usbdrvwext_iwaplist(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *data, char *extra)
{
	/* Don't know how to do yet--CWYang(+) */
	return 0;

}

int usbdrvwext_siwscan(struct net_device *dev, struct iw_request_info *info,
	struct iw_point *data, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (macp->DeviceOpened != 1)
		return 0;

	printk(KERN_WARNING "CWY - usbdrvwext_siwscan\n");

	zfiWlanScan(dev);

	return 0;
}

int usbdrvwext_giwscan(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *data, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	/* struct zsWlanDev* wd = (struct zsWlanDev*) zmw_wlan_dev(dev); */
	char *current_ev = extra;
	char *end_buf;
	int i;
	/* BssList = wd->sta.pBssList; */
	/* zmw_get_wlan_dev(dev); */

	if (macp->DeviceOpened != 1)
		return 0;

	/* struct zsBssList BssList; */
	struct zsBssListV1 *pBssList = kmalloc(sizeof(struct zsBssListV1),
								GFP_KERNEL);
	if (data->length == 0)
		end_buf = extra + IW_SCAN_MAX_DATA;
	else
		end_buf = extra + data->length;

	printk(KERN_WARNING "giwscan - Report Scan Results\n");
	/* printk("giwscan - BssList Sreucture Len : %d\n", sizeof(BssList));
	* printk("giwscan - BssList Count : %d\n",
	* wd->sta.pBssList->bssCount);
	* printk("giwscan - UpdateBssList Count : %d\n",
	* wd->sta.pUpdateBssList->bssCount);
	*/
	zfiWlanQueryBssListV1(dev, pBssList);
	/* zfiWlanQueryBssList(dev, &BssList); */

	/* Read and parse all entries */
	printk(KERN_WARNING "giwscan - pBssList->bssCount : %d\n",
						pBssList->bssCount);
	/* printk("giwscan - BssList.bssCount : %d\n", BssList.bssCount); */

	for (i = 0; i < pBssList->bssCount; i++) {
		/* Translate to WE format this entry
		* current_ev = usbdrv_translate_scan(dev, info, current_ev,
		* extra + IW_SCAN_MAX_DATA, &pBssList->bssInfo[i]);
		*/
		current_ev = usbdrv_translate_scan(dev, info, current_ev,
					end_buf, &pBssList->bssInfo[i]);

		if (current_ev == end_buf) {
			kfree(pBssList);
			data->length = current_ev - extra;
			return -E2BIG;
		}
	}

	/* Length of data */
	data->length = (current_ev - extra);
	data->flags = 0;   /* todo */

	kfree(pBssList);

	return 0;
}

int usbdrvwext_siwessid(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *essid, char *extra)
{
	char EssidBuf[IW_ESSID_MAX_SIZE + 1];
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (essid->flags == 1) {
		if (essid->length > IW_ESSID_MAX_SIZE)
			return -E2BIG;

		if (copy_from_user(&EssidBuf, essid->pointer, essid->length))
			return -EFAULT;

		EssidBuf[essid->length] = '\0';
		/* printk("siwessid - Set Essid : %s\n",EssidBuf); */
		/* printk("siwessid - Essid Len : %d\n",essid->length); */
		/* printk("siwessid - Essid Flag : %x\n",essid->flags); */
		if (macp->DeviceOpened == 1) {
			zfiWlanSetSSID(dev, EssidBuf, strlen(EssidBuf));
			zfiWlanSetFrequency(dev, zfiWlanQueryFrequency(dev),
						FALSE);
			zfiWlanSetEncryMode(dev, zfiWlanQueryEncryMode(dev));
			/* u8_t wpaieLen,wpaie[50]; */
			/* zfiWlanQueryWpaIe(dev, wpaie, &wpaieLen); */
			zfiWlanDisable(dev, 0);
			zfiWlanEnable(dev);
			/* if (wpaieLen > 2) */
			/* zfiWlanSetWpaIe(dev, wpaie, wpaieLen); */
		}
	}

	return 0;
}

int usbdrvwext_giwessid(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *essid, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t EssidLen;
	char EssidBuf[IW_ESSID_MAX_SIZE + 1];
	int ssid_len;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	zfiWlanQuerySSID(dev, &EssidBuf[0], &EssidLen);

	/* Convert type from unsigned char to char */
	ssid_len = (int)EssidLen;

	/* Make sure the essid length is not greater than IW_ESSID_MAX_SIZE */
	if (ssid_len > IW_ESSID_MAX_SIZE)
		ssid_len = IW_ESSID_MAX_SIZE;

	EssidBuf[ssid_len] = '\0';

	essid->flags = 1;
	essid->length = strlen(EssidBuf);

	memcpy(extra, EssidBuf, essid->length);
	/* wireless.c in Kernel would handle copy_to_user -- line 679 */
	/* if (essid->pointer) {
	* if (copy_to_user(essid->pointer, EssidBuf, essid->length)) {
	* printk("giwessid - copy_to_user Fail\n");
	* return -EFAULT;
	* }
	* }
	*/

	return 0;
}

int usbdrvwext_siwnickn(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *data, char *nickname)
{
	/* Exist but junk--CWYang(+) */
	return 0;
}

int usbdrvwext_giwnickn(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_point *data, char *nickname)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t EssidLen;
	char EssidBuf[IW_ESSID_MAX_SIZE + 1];

	if (macp->DeviceOpened != 1)
		return 0;

	zfiWlanQuerySSID(dev, &EssidBuf[0], &EssidLen);
	EssidBuf[EssidLen] = 0;

	data->flags = 1;
	data->length = strlen(EssidBuf);

	memcpy(nickname, EssidBuf, data->length);

	return 0;
}

int usbdrvwext_siwrate(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frq, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	/* Array to Define Rate Number that Send to Driver */
	u16_t zcIndextoRateBG[16] = {1000, 2000, 5500, 11000, 0, 0, 0, 0,
			48000, 24000, 12000, 6000, 54000, 36000, 18000, 9000};
	u16_t zcRateToMCS[] = {0xff, 0, 1, 2, 3, 0xb, 0xf, 0xa, 0xe, 0x9, 0xd,
				0x8, 0xc};
	u8_t i, RateIndex = 4;
	u16_t RateKbps;

	/* printk("frq->disabled : 0x%x\n",frq->disabled); */
	/* printk("frq->value : 0x%x\n",frq->value); */

	RateKbps = frq->value / 1000;
	/* printk("RateKbps : %d\n", RateKbps); */
	for (i = 0; i < 16; i++) {
		if (RateKbps == zcIndextoRateBG[i])
			RateIndex = i;
	}

	if (zcIndextoRateBG[RateIndex] == 0)
		RateIndex = 0xff;
	/* printk("RateIndex : %x\n", RateIndex); */
	for (i = 0; i < 13; i++)
		if (RateIndex == zcRateToMCS[i])
			break;
	/* printk("Index : %x\n", i); */
	if (RateKbps == 65000) {
		RateIndex = 20;
		printk(KERN_WARNING "RateIndex : %d\n", RateIndex);
	}

	if (macp->DeviceOpened == 1) {
		zfiWlanSetTxRate(dev, i);
		/* zfiWlanDisable(dev); */
		/* zfiWlanEnable(dev); */
	}

	return 0;
}

int usbdrvwext_giwrate(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frq, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	frq->fixed = 0;
	frq->disabled = 0;
	frq->value = zfiWlanQueryRxRate(dev) * 1000;

	return 0;
}

int usbdrvwext_siwrts(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *rts, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	int val = rts->value;

	if (macp->DeviceOpened != 1)
		return 0;

	if (rts->disabled)
		val = 2347;

	if ((val < 0) || (val > 2347))
		return -EINVAL;

	zfiWlanSetRtsThreshold(dev, val);

	return 0;
}

int usbdrvwext_giwrts(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *rts, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	rts->value = zfiWlanQueryRtsThreshold(dev);
	rts->disabled = (rts->value >= 2347);
	rts->fixed = 1;

	return 0;
}

int usbdrvwext_siwfrag(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frag, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u16_t fragThreshold;

	if (macp->DeviceOpened != 1)
		return 0;

	if (frag->disabled)
		fragThreshold = 0;
	else
		fragThreshold = frag->value;

	zfiWlanSetFragThreshold(dev, fragThreshold);

	return 0;
}

int usbdrvwext_giwfrag(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frag, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u16 val;
	unsigned long irqFlag;

	if (!netif_running(dev))
		return -EINVAL;

	if (macp->DeviceOpened != 1)
		return 0;

	spin_lock_irqsave(&macp->cs_lock, irqFlag);

	val = zfiWlanQueryFragThreshold(dev);

	frag->value = val;

	frag->disabled = (val >= 2346);
	frag->fixed = 1;

	spin_unlock_irqrestore(&macp->cs_lock, irqFlag);

	return 0;
}

int usbdrvwext_siwtxpow(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *rrq, char *extra)
{
	/* Not support yet--CWYng(+) */
	return 0;
}

int usbdrvwext_giwtxpow(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *rrq, char *extra)
{
	/* Not support yet--CWYng(+) */
	return 0;
}

int usbdrvwext_siwretry(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *rrq, char *extra)
{
	/* Do nothing--CWYang(+) */
	return 0;
}

int usbdrvwext_giwretry(struct net_device *dev,
			struct iw_request_info *info,
			struct iw_param *rrq, char *extra)
{
	/* Do nothing--CWYang(+) */
	return 0;
}

int usbdrvwext_siwencode(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *erq, char *key)
{
	struct zsKeyInfo keyInfo;
	int i;
	int WepState = ZM_ENCRYPTION_WEP_DISABLED;
	struct usbdrv_private *macp = dev->ml_priv;

	if (!netif_running(dev))
		return -EINVAL;

	if ((erq->flags & IW_ENCODE_DISABLED) == 0) {
		keyInfo.key = key;
		keyInfo.keyLength = erq->length;
		keyInfo.keyIndex = (erq->flags & IW_ENCODE_INDEX) - 1;
		if (keyInfo.keyIndex >= 4)
			keyInfo.keyIndex = 0;
		keyInfo.flag = ZM_KEY_FLAG_DEFAULT_KEY;

		zfiWlanSetKey(dev, keyInfo);
		WepState = ZM_ENCRYPTION_WEP_ENABLED;
	} else {
		for (i = 1; i < 4; i++)
			zfiWlanRemoveKey(dev, 0, i);
		WepState = ZM_ENCRYPTION_WEP_DISABLED;
		/* zfiWlanSetEncryMode(dev, ZM_NO_WEP); */
	}

	if (macp->DeviceOpened == 1) {
		zfiWlanSetWepStatus(dev, WepState);
		zfiWlanSetFrequency(dev, zfiWlanQueryFrequency(dev), FALSE);
		/* zfiWlanSetEncryMode(dev, zfiWlanQueryEncryMode(dev)); */
		/* u8_t wpaieLen,wpaie[50]; */
		/* zfiWlanQueryWpaIe(dev, wpaie, &wpaieLen); */
		zfiWlanDisable(dev, 0);
		zfiWlanEnable(dev);
		/* if (wpaieLen > 2) */
		/* zfiWlanSetWpaIe(dev, wpaie, wpaieLen); */
	}

	return 0;
}

int usbdrvwext_giwencode(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_point *erq, char *key)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t EncryptionMode;
	u8_t keyLen = 0;

	if (macp->DeviceOpened != 1)
		return 0;

	EncryptionMode = zfiWlanQueryEncryMode(dev);

	if (EncryptionMode)
		erq->flags = IW_ENCODE_ENABLED;
	else
		erq->flags = IW_ENCODE_DISABLED;

	/* We can't return the key, so set the proper flag and return zero */
	erq->flags |= IW_ENCODE_NOKEY;
	memset(key, 0, 16);

	/* Copy the key to the user buffer */
	switch (EncryptionMode) {
	case ZM_WEP64:
		keyLen = 5;
		break;
	case ZM_WEP128:
		keyLen = 13;
		break;
	case ZM_WEP256:
		keyLen = 29;
		break;
	case ZM_AES:
		keyLen = 16;
		break;
	case ZM_TKIP:
		keyLen = 32;
		break;
	#ifdef ZM_ENABLE_CENC
	case ZM_CENC:
		/* ZM_ENABLE_CENC */
		keyLen = 32;
		break;
	#endif
	case ZM_NO_WEP:
		keyLen = 0;
		break;
	default:
		keyLen = 0;
		printk(KERN_ERR "Unknown EncryMode\n");
		break;
	}
	erq->length = keyLen;

	return 0;
}

int usbdrvwext_siwpower(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frq, char *extra)
{
	struct usbdrv_private *macp = dev->ml_priv;
	u8_t PSMode;

	if (macp->DeviceOpened != 1)
		return 0;

	if (frq->disabled)
		PSMode = ZM_STA_PS_NONE;
	else
		PSMode = ZM_STA_PS_MAX;

	zfiWlanSetPowerSaveMode(dev, PSMode);

	return 0;
}

int usbdrvwext_giwpower(struct net_device *dev,
		struct iw_request_info *info,
		struct iw_param *frq, char *extra)
{
	unsigned long irqFlag;
	struct usbdrv_private *macp = dev->ml_priv;

	if (macp->DeviceOpened != 1)
		return 0;

	spin_lock_irqsave(&macp->cs_lock, irqFlag);

	if (zfiWlanQueryPowerSaveMode(dev) == ZM_STA_PS_NONE)
		frq->disabled = 1;
	else
		frq->disabled = 0;

	spin_unlock_irqrestore(&macp->cs_lock, irqFlag);

	return 0;
}

/*int usbdrvwext_setparam(struct net_device *dev, struct iw_request_info *info,
*		   	 void *w, char *extra)
*{
*	struct ieee80211vap *vap = dev->ml_priv;
*	struct ieee80211com *ic = vap->iv_ic;
*	struct ieee80211_rsnparms *rsn = &vap->iv_bss->ni_rsn;
*	int *i = (int *) extra;
*	int param = i[0];		// parameter id is 1st
*	int value = i[1];		// NB: most values are TYPE_INT
*	int retv = 0;
*	int j, caps;
*	const struct ieee80211_authenticator *auth;
*	const struct ieee80211_aclator *acl;
*
*	switch (param) {
*	case IEEE80211_PARAM_AUTHMODE:
*		switch (value) {
*		case IEEE80211_AUTH_WPA:	// WPA
*		case IEEE80211_AUTH_8021X:	// 802.1x
*		case IEEE80211_AUTH_OPEN:	// open
*		case IEEE80211_AUTH_SHARED:	// shared-key
*		case IEEE80211_AUTH_AUTO:	// auto
*			auth = ieee80211_authenticator_get(value);
*			if (auth == NULL)
*				return -EINVAL;
*			break;
*		default:
*			return -EINVAL;
*		}
*		switch (value) {
*		case IEEE80211_AUTH_WPA:	// WPA w/ 802.1x
*			vap->iv_flags |= IEEE80211_F_PRIVACY;
*			value = IEEE80211_AUTH_8021X;
*			break;
*		case IEEE80211_AUTH_OPEN:	// open
*		vap->iv_flags &= ~(IEEE80211_F_WPA | IEEE80211_F_PRIVACY);
*			break;
*		case IEEE80211_AUTH_SHARED:	// shared-key
*		case IEEE80211_AUTH_AUTO:	// auto
*		case IEEE80211_AUTH_8021X:	// 802.1x
*			vap->iv_flags &= ~IEEE80211_F_WPA;
*			// both require a key so mark the PRIVACY capability
*			vap->iv_flags |= IEEE80211_F_PRIVACY;
*			break;
*		}
*		// NB: authenticator attach/detach happens on state change
*		vap->iv_bss->ni_authmode = value;
*		// XXX mixed/mode/usage?
*		vap->iv_auth = auth;
*		retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_PROTMODE:
*		if (value > IEEE80211_PROT_RTSCTS)
*			return -EINVAL;
*		ic->ic_protmode = value;
*		// NB: if not operating in 11g this can wait
*		if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&
*		    IEEE80211_IS_CHAN_ANYG(ic->ic_bsschan))
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_MCASTCIPHER:
*		if ((vap->iv_caps & cipher2cap(value)) == 0 &&
*		    !ieee80211_crypto_available(value))
*			return -EINVAL;
*		rsn->rsn_mcastcipher = value;
*		if (vap->iv_flags & IEEE80211_F_WPA)
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_MCASTKEYLEN:
*		if (!(0 < value && value < IEEE80211_KEYBUF_SIZE))
*			return -EINVAL;
*		// XXX no way to verify driver capability
*		rsn->rsn_mcastkeylen = value;
*		if (vap->iv_flags & IEEE80211_F_WPA)
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_UCASTCIPHERS:
*
*		 // Convert cipher set to equivalent capabilities.
*		 // NB: this logic intentionally ignores unknown and
*		 // unsupported ciphers so folks can specify 0xff or
*		 // similar and get all available ciphers.
*
*		caps = 0;
*		for (j = 1; j < 32; j++)	// NB: skip WEP
*			if ((value & (1<<j)) &&
*			    ((vap->iv_caps & cipher2cap(j)) ||
*			     ieee80211_crypto_available(j)))
*				caps |= 1<<j;
*		if (caps == 0)			// nothing available
*			return -EINVAL;
*		// XXX verify ciphers ok for unicast use?
*		// XXX disallow if running as it'll have no effect
*		rsn->rsn_ucastcipherset = caps;
*		if (vap->iv_flags & IEEE80211_F_WPA)
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_UCASTCIPHER:
*		if ((rsn->rsn_ucastcipherset & cipher2cap(value)) == 0)
*			return -EINVAL;
*		rsn->rsn_ucastcipher = value;
*		break;
*	case IEEE80211_PARAM_UCASTKEYLEN:
*		if (!(0 < value && value < IEEE80211_KEYBUF_SIZE))
*			return -EINVAL;
*		// XXX no way to verify driver capability
*		rsn->rsn_ucastkeylen = value;
*		break;
*	case IEEE80211_PARAM_KEYMGTALGS:
*		// XXX check
*		rsn->rsn_keymgmtset = value;
*		if (vap->iv_flags & IEEE80211_F_WPA)
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_RSNCAPS:
*		// XXX check
*		rsn->rsn_caps = value;
*		if (vap->iv_flags & IEEE80211_F_WPA)
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_WPA:
*		if (value > 3)
*			return -EINVAL;
*		// XXX verify ciphers available
*		vap->iv_flags &= ~IEEE80211_F_WPA;
*		switch (value) {
*		case 1:
*			vap->iv_flags |= IEEE80211_F_WPA1;
*			break;
*		case 2:
*			vap->iv_flags |= IEEE80211_F_WPA2;
*			break;
*		case 3:
*			vap->iv_flags |= IEEE80211_F_WPA1 | IEEE80211_F_WPA2;
*			break;
*		}
*		retv = ENETRESET;		// XXX?
*		break;
*	case IEEE80211_PARAM_ROAMING:
*		if (!(IEEE80211_ROAMING_DEVICE <= value &&
*		    value <= IEEE80211_ROAMING_MANUAL))
*			return -EINVAL;
*		ic->ic_roaming = value;
*		break;
*	case IEEE80211_PARAM_PRIVACY:
*		if (value) {
*			// XXX check for key state?
*			vap->iv_flags |= IEEE80211_F_PRIVACY;
*		} else
*			vap->iv_flags &= ~IEEE80211_F_PRIVACY;
*		break;
*	case IEEE80211_PARAM_DROPUNENCRYPTED:
*		if (value)
*			vap->iv_flags |= IEEE80211_F_DROPUNENC;
*		else
*			vap->iv_flags &= ~IEEE80211_F_DROPUNENC;
*		break;
*	case IEEE80211_PARAM_COUNTERMEASURES:
*		if (value) {
*			if ((vap->iv_flags & IEEE80211_F_WPA) == 0)
*				return -EINVAL;
*			vap->iv_flags |= IEEE80211_F_COUNTERM;
*		} else
*			vap->iv_flags &= ~IEEE80211_F_COUNTERM;
*		break;
*	case IEEE80211_PARAM_DRIVER_CAPS:
*		vap->iv_caps = value;		// NB: for testing
*		break;
*	case IEEE80211_PARAM_MACCMD:
*		acl = vap->iv_acl;
*		switch (value) {
*		case IEEE80211_MACCMD_POLICY_OPEN:
*		case IEEE80211_MACCMD_POLICY_ALLOW:
*		case IEEE80211_MACCMD_POLICY_DENY:
*			if (acl == NULL) {
*				acl = ieee80211_aclator_get("mac");
*				if (acl == NULL || !acl->iac_attach(vap))
*					return -EINVAL;
*				vap->iv_acl = acl;
*			}
*			acl->iac_setpolicy(vap, value);
*			break;
*		case IEEE80211_MACCMD_FLUSH:
*			if (acl != NULL)
*				acl->iac_flush(vap);
*			// NB: silently ignore when not in use
*			break;
*		case IEEE80211_MACCMD_DETACH:
*			if (acl != NULL) {
*				vap->iv_acl = NULL;
*				acl->iac_detach(vap);
*			}
*			break;
*		}
*		break;
*	case IEEE80211_PARAM_WMM:
*		if (ic->ic_caps & IEEE80211_C_WME){
*			if (value) {
*				vap->iv_flags |= IEEE80211_F_WME;
*				 *//* XXX needed by ic_reset *//*
*				vap->iv_ic->ic_flags |= IEEE80211_F_WME;
*			}
*			else {
*				*//* XXX needed by ic_reset *//*
*				vap->iv_flags &= ~IEEE80211_F_WME;
*				vap->iv_ic->ic_flags &= ~IEEE80211_F_WME;
*			}
*			retv = ENETRESET;	// Renegotiate for capabilities
*		}
*		break;
*	case IEEE80211_PARAM_HIDESSID:
*		if (value)
*			vap->iv_flags |= IEEE80211_F_HIDESSID;
*		else
*			vap->iv_flags &= ~IEEE80211_F_HIDESSID;
*		retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_APBRIDGE:
*		if (value == 0)
*			vap->iv_flags |= IEEE80211_F_NOBRIDGE;
*		else
*			vap->iv_flags &= ~IEEE80211_F_NOBRIDGE;
*		break;
*	case IEEE80211_PARAM_INACT:
*		vap->iv_inact_run = value / IEEE80211_INACT_WAIT;
*		break;
*	case IEEE80211_PARAM_INACT_AUTH:
*		vap->iv_inact_auth = value / IEEE80211_INACT_WAIT;
*		break;
*	case IEEE80211_PARAM_INACT_INIT:
*		vap->iv_inact_init = value / IEEE80211_INACT_WAIT;
*		break;
*	case IEEE80211_PARAM_ABOLT:
*		caps = 0;
*
*		 // Map abolt settings to capability bits;
*		 // this also strips unknown/unwanted bits.
*
*		if (value & IEEE80211_ABOLT_TURBO_PRIME)
*			caps |= IEEE80211_ATHC_TURBOP;
*		if (value & IEEE80211_ABOLT_COMPRESSION)
*			caps |= IEEE80211_ATHC_COMP;
*		if (value & IEEE80211_ABOLT_FAST_FRAME)
*			caps |= IEEE80211_ATHC_FF;
*		if (value & IEEE80211_ABOLT_XR)
*			caps |= IEEE80211_ATHC_XR;
*		if (value & IEEE80211_ABOLT_AR)
*			caps |= IEEE80211_ATHC_AR;
*		if (value & IEEE80211_ABOLT_BURST)
*			caps |= IEEE80211_ATHC_BURST;
*        if (value & IEEE80211_ABOLT_WME_ELE)
*            caps |= IEEE80211_ATHC_WME;
*		// verify requested capabilities are supported
*		if ((caps & ic->ic_ath_cap) != caps)
*			return -EINVAL;
*		if (vap->iv_ath_cap != caps) {
*			if ((vap->iv_ath_cap ^ caps) & IEEE80211_ATHC_TURBOP) {
*				if (ieee80211_set_turbo(dev,
*						caps & IEEE80211_ATHC_TURBOP))
*					return -EINVAL;
*				ieee80211_scan_flush(ic);
*			}
*			vap->iv_ath_cap = caps;
*			ic->ic_athcapsetup(vap->iv_ic, vap->iv_ath_cap);
*			retv = ENETRESET;
*		}
*		break;
*	case IEEE80211_PARAM_DTIM_PERIOD:
*		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
*		    vap->iv_opmode != IEEE80211_M_IBSS)
*			return -EINVAL;
*		if (IEEE80211_DTIM_MIN <= value &&
*		    value <= IEEE80211_DTIM_MAX) {
*			vap->iv_dtim_period = value;
*			retv = ENETRESET;		// requires restart
*		} else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_BEACON_INTERVAL:
*		if (vap->iv_opmode != IEEE80211_M_HOSTAP &&
*		    vap->iv_opmode != IEEE80211_M_IBSS)
*			return -EINVAL;
*		if (IEEE80211_BINTVAL_MIN <= value &&
*		    value <= IEEE80211_BINTVAL_MAX) {
*			ic->ic_lintval = value;		// XXX multi-bss
*			retv = ENETRESET;		// requires restart
*		} else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_DOTH:
*		if (value) {
*			ic->ic_flags |= IEEE80211_F_DOTH;
*		}
*		else
*			ic->ic_flags &= ~IEEE80211_F_DOTH;
*		retv = ENETRESET;	// XXX: need something this drastic?
*		break;
*	case IEEE80211_PARAM_PWRTARGET:
*		ic->ic_curchanmaxpwr = value;
*		break;
*	case IEEE80211_PARAM_GENREASSOC:
*		IEEE80211_SEND_MGMT(vap->iv_bss,
*					IEEE80211_FC0_SUBTYPE_REASSOC_REQ, 0);
*		break;
*	case IEEE80211_PARAM_COMPRESSION:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_COMP, value);
*		break;
*    case IEEE80211_PARAM_WMM_AGGRMODE:
*        retv = ieee80211_setathcap(vap, IEEE80211_ATHC_WME, value);
*        break;
*	case IEEE80211_PARAM_FF:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_FF, value);
*		break;
*	case IEEE80211_PARAM_TURBO:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_TURBOP, value);
*		if (retv == ENETRESET) {
*			if(ieee80211_set_turbo(dev,value))
*					return -EINVAL;
*			ieee80211_scan_flush(ic);
*		}
*		break;
*	case IEEE80211_PARAM_XR:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_XR, value);
*		break;
*	case IEEE80211_PARAM_BURST:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_BURST, value);
*		break;
*	case IEEE80211_PARAM_AR:
*		retv = ieee80211_setathcap(vap, IEEE80211_ATHC_AR, value);
*		break;
*	case IEEE80211_PARAM_PUREG:
*		if (value)
*			vap->iv_flags |= IEEE80211_F_PUREG;
*		else
*			vap->iv_flags &= ~IEEE80211_F_PUREG;
*		// NB: reset only if we're operating on an 11g channel
*		if (ic->ic_bsschan != IEEE80211_CHAN_ANYC &&
*		    IEEE80211_IS_CHAN_ANYG(ic->ic_bsschan))
*			retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_WDS:
*		if (value)
*			vap->iv_flags_ext |= IEEE80211_FEXT_WDS;
*		else
*			vap->iv_flags_ext &= ~IEEE80211_FEXT_WDS;
*		break;
*	case IEEE80211_PARAM_BGSCAN:
*		if (value) {
*			if ((vap->iv_caps & IEEE80211_C_BGSCAN) == 0)
*				return -EINVAL;
*			vap->iv_flags |= IEEE80211_F_BGSCAN;
*		} else {
*			// XXX racey?
*			vap->iv_flags &= ~IEEE80211_F_BGSCAN;
*			ieee80211_cancel_scan(vap);	// anything current
*		}
*		break;
*	case IEEE80211_PARAM_BGSCAN_IDLE:
*		if (value >= IEEE80211_BGSCAN_IDLE_MIN)
*			vap->iv_bgscanidle = value*HZ/1000;
*		else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_BGSCAN_INTERVAL:
*		if (value >= IEEE80211_BGSCAN_INTVAL_MIN)
*			vap->iv_bgscanintvl = value*HZ;
*		else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_MCAST_RATE:
*		// units are in KILObits per second
*		if (value >= 256 && value <= 54000)
*			vap->iv_mcast_rate = value;
*		else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_COVERAGE_CLASS:
*		if (value >= 0 && value <= IEEE80211_COVERAGE_CLASS_MAX) {
*			ic->ic_coverageclass = value;
*			if (IS_UP_AUTO(vap))
*				ieee80211_new_state(vap, IEEE80211_S_SCAN, 0);
*			retv = 0;
*		}
*		else
*			retv = EINVAL;
*		break;
*	case IEEE80211_PARAM_COUNTRY_IE:
*		if (value)
*			ic->ic_flags_ext |= IEEE80211_FEXT_COUNTRYIE;
*		else
*			ic->ic_flags_ext &= ~IEEE80211_FEXT_COUNTRYIE;
*		retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_REGCLASS:
*		if (value)
*			ic->ic_flags_ext |= IEEE80211_FEXT_REGCLASS;
*		else
*			ic->ic_flags_ext &= ~IEEE80211_FEXT_REGCLASS;
*		retv = ENETRESET;
*		break;
*	case IEEE80211_PARAM_SCANVALID:
*		vap->iv_scanvalid = value*HZ;
*		break;
*	case IEEE80211_PARAM_ROAM_RSSI_11A:
*		vap->iv_roam.rssi11a = value;
*		break;
*	case IEEE80211_PARAM_ROAM_RSSI_11B:
*		vap->iv_roam.rssi11bOnly = value;
*		break;
*	case IEEE80211_PARAM_ROAM_RSSI_11G:
*		vap->iv_roam.rssi11b = value;
*		break;
*	case IEEE80211_PARAM_ROAM_RATE_11A:
*		vap->iv_roam.rate11a = value;
*		break;
*	case IEEE80211_PARAM_ROAM_RATE_11B:
*		vap->iv_roam.rate11bOnly = value;
*		break;
*	case IEEE80211_PARAM_ROAM_RATE_11G:
*		vap->iv_roam.rate11b = value;
*		break;
*	case IEEE80211_PARAM_UAPSDINFO:
*		if (vap->iv_opmode == IEEE80211_M_HOSTAP) {
*			if (ic->ic_caps & IEEE80211_C_UAPSD) {
*				if (value)
*					IEEE80211_VAP_UAPSD_ENABLE(vap);
*				else
*					IEEE80211_VAP_UAPSD_DISABLE(vap);
*				retv = ENETRESET;
*			}
*		}
*		else if (vap->iv_opmode == IEEE80211_M_STA) {
*			vap->iv_uapsdinfo = value;
*			IEEE80211_VAP_UAPSD_ENABLE(vap);
*			retv = ENETRESET;
*		}
*		break;
*	case IEEE80211_PARAM_SLEEP:
*		// XXX: Forced sleep for testing. Does not actually place the
*		//      HW in sleep mode yet. this only makes sense for STAs.
*
*		if (value) {
*			// goto sleep
*			IEEE80211_VAP_GOTOSLEEP(vap);
*		}
*		else {
*			// wakeup
*			IEEE80211_VAP_WAKEUP(vap);
*		}
*		ieee80211_send_nulldata(ieee80211_ref_node(vap->iv_bss));
*		break;
*	case IEEE80211_PARAM_QOSNULL:
*		// Force a QoS Null for testing.
*		ieee80211_send_qosnulldata(vap->iv_bss, value);
*		break;
*	case IEEE80211_PARAM_PSPOLL:
*		// Force a PS-POLL for testing.
*		ieee80211_send_pspoll(vap->iv_bss);
*		break;
*	case IEEE80211_PARAM_EOSPDROP:
*		if (vap->iv_opmode == IEEE80211_M_HOSTAP) {
*			if (value) IEEE80211_VAP_EOSPDROP_ENABLE(vap);
*			else IEEE80211_VAP_EOSPDROP_DISABLE(vap);
*		}
*		break;
*	case IEEE80211_PARAM_MARKDFS:
*		if (value)
*			ic->ic_flags_ext |= IEEE80211_FEXT_MARKDFS;
*		else
*			ic->ic_flags_ext &= ~IEEE80211_FEXT_MARKDFS;
*		break;
*	case IEEE80211_PARAM_CHANBW:
*		switch (value) {
*			case 0:
*				ic->ic_chanbwflag = 0;
*				break;
*			case 1:
*				ic->ic_chanbwflag = IEEE80211_CHAN_HALF;
*				break;
*			case 2:
*				ic->ic_chanbwflag = IEEE80211_CHAN_QUARTER;
*				break;
*			default:
*				retv = EINVAL;
*				break;
*		}
*		break;
*	case IEEE80211_PARAM_SHORTPREAMBLE:
*		if (value) {
*			ic->ic_caps |= IEEE80211_C_SHPREAMBLE;
*		} else {
*			ic->ic_caps &= ~IEEE80211_C_SHPREAMBLE;
*		}
*		retv = ENETRESET;
*		break;
*	default:
*		retv = EOPNOTSUPP;
*		break;
*	}
*	// XXX should any of these cause a rescan?
*	if (retv == ENETRESET)
*		retv = IS_UP_AUTO(vap) ? ieee80211_open(vap->iv_dev) : 0;
*	return -retv;
*}
*/

int usbdrvwext_setmode(struct net_device *dev, struct iw_request_info *info,
			void *w, char *extra)
{
	return 0;
}

int usbdrvwext_getmode(struct net_device *dev, struct iw_request_info *info,
			void *w, char *extra)
{
	/* struct usbdrv_private *macp = dev->ml_priv; */
	struct iw_point *wri = (struct iw_point *)extra;
	char mode[8];

	strcpy(mode, "11g");
	return copy_to_user(wri->pointer, mode, 6) ? -EFAULT : 0;
}

int zfLnxPrivateIoctl(struct net_device *dev, struct zdap_ioctl* zdreq)
{
	/* void* regp = macp->regp; */
	u16_t cmd;
	/* u32_t temp; */
	u32_t *p;
	u32_t i;

	cmd = zdreq->cmd;
	switch (cmd) {
	case ZM_IOCTL_REG_READ:
		zfiDbgReadReg(dev, zdreq->addr);
		break;
	case ZM_IOCTL_REG_WRITE:
		zfiDbgWriteReg(dev, zdreq->addr, zdreq->value);
		break;
	case ZM_IOCTL_MEM_READ:
		p = (u32_t *) bus_to_virt(zdreq->addr);
		printk(KERN_WARNING
				"usbdrv: read memory addr: 0x%08x value:"
				" 0x%08x\n", zdreq->addr, *p);
		break;
	case ZM_IOCTL_MEM_WRITE:
		p = (u32_t *) bus_to_virt(zdreq->addr);
		*p = zdreq->value;
		printk(KERN_WARNING
			"usbdrv : write value : 0x%08x to memory addr :"
			" 0x%08x\n", zdreq->value, zdreq->addr);
		break;
	case ZM_IOCTL_TALLY:
		zfiWlanShowTally(dev);
		if (zdreq->addr)
			zfiWlanResetTally(dev);
		break;
	case ZM_IOCTL_TEST:
		printk(KERN_WARNING
				"ZM_IOCTL_TEST:len=%d\n", zdreq->addr);
		/* zfiWlanReadReg(dev, 0x10f400); */
		/* zfiWlanReadReg(dev, 0x10f404); */
		printk(KERN_WARNING "IOCTL TEST\n");
		#if 1
		/* print packet */
		for (i = 0; i < zdreq->addr; i++) {
			if ((i&0x7) == 0)
				printk(KERN_WARNING "\n");
			printk(KERN_WARNING "%02X ",
					(unsigned char)zdreq->data[i]);
		}
		printk(KERN_WARNING "\n");
		#endif

		/* For Test?? 1 to 0 by CWYang(-) */
		#if 0
			struct sk_buff *s;

			/* Allocate a skb */
			s = alloc_skb(2000, GFP_ATOMIC);

			/* Copy data to skb */
			for (i = 0; i < zdreq->addr; i++)
				s->data[i] = zdreq->data[i];
			s->len = zdreq->addr;

			/* Call zfIdlRecv() */
			zfiRecv80211(dev, s, NULL);
		#endif
		break;
	/************************* ZDCONFIG ***************************/
	case ZM_IOCTL_FRAG:
		zfiWlanSetFragThreshold(dev, zdreq->addr);
		break;
	case ZM_IOCTL_RTS:
		zfiWlanSetRtsThreshold(dev, zdreq->addr);
		break;
	case ZM_IOCTL_SCAN:
		zfiWlanScan(dev);
		break;
	case ZM_IOCTL_KEY: {
		u8_t key[29];
		struct zsKeyInfo keyInfo;
		u32_t i;

		for (i = 0; i < 29; i++)
			key[i] = 0;

		for (i = 0; i < zdreq->addr; i++)
			key[i] = zdreq->data[i];

		printk(KERN_WARNING
			"key len=%d, key=%02x%02x%02x%02x%02x...\n",
			zdreq->addr, key[0], key[1], key[2], key[3], key[4]);

		keyInfo.keyLength = zdreq->addr;
		keyInfo.keyIndex = 0;
		keyInfo.flag = 0;
		keyInfo.key = key;
		zfiWlanSetKey(dev, keyInfo);
	}
		break;
	case ZM_IOCTL_RATE:
		zfiWlanSetTxRate(dev, zdreq->addr);
		break;
	case ZM_IOCTL_ENCRYPTION_MODE:
		zfiWlanSetEncryMode(dev, zdreq->addr);

		zfiWlanDisable(dev, 0);
		zfiWlanEnable(dev);
		break;
		/* CWYang(+) */
	case ZM_IOCTL_SIGNAL_STRENGTH: {
		u8_t buffer[2];
		zfiWlanQuerySignalInfo(dev, &buffer[0]);
		printk(KERN_WARNING
			"Current Signal Strength : %02d\n", buffer[0]);
	}
		break;
		/* CWYang(+) */
	case ZM_IOCTL_SIGNAL_QUALITY: {
		u8_t buffer[2];
		zfiWlanQuerySignalInfo(dev, &buffer[0]);
		printk(KERN_WARNING
			"Current Signal Quality : %02d\n", buffer[1]);
	}
		break;
	case ZM_IOCTL_SET_PIBSS_MODE:
		if (zdreq->addr == 1)
			zfiWlanSetWlanMode(dev, ZM_MODE_PSEUDO);
		else
			zfiWlanSetWlanMode(dev, ZM_MODE_INFRASTRUCTURE);

		zfiWlanDisable(dev, 0);
		zfiWlanEnable(dev);
		break;
	/********************* ZDCONFIG ***********************/
	default:
		printk(KERN_ERR "usbdrv: error command = %x\n", cmd);
		break;
	}

	return 0;
}

int usbdrv_wpa_ioctl(struct net_device *dev, struct athr_wlan_param *zdparm)
{
	int ret = 0;
	u8_t bc_addr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	u8_t mac_addr[80];
	struct zsKeyInfo keyInfo;
	struct usbdrv_private *macp = dev->ml_priv;
	u16_t vapId = 0;
	int ii;

	/* zmw_get_wlan_dev(dev); */

	switch (zdparm->cmd) {
	case ZD_CMD_SET_ENCRYPT_KEY:
		/* Set up key information */
		keyInfo.keyLength = zdparm->u.crypt.key_len;
		keyInfo.keyIndex = zdparm->u.crypt.idx;
		if (zfiWlanQueryWlanMode(dev) == ZM_MODE_AP) {
			/* AP Mode */
			keyInfo.flag = ZM_KEY_FLAG_AUTHENTICATOR;
		} else
			keyInfo.flag = 0;
		keyInfo.key = zdparm->u.crypt.key;
		keyInfo.initIv = zdparm->u.crypt.seq;
		keyInfo.macAddr = (u16_t *)zdparm->sta_addr;

		/* Identify the MAC address information */
		if (memcmp(zdparm->sta_addr, bc_addr, sizeof(bc_addr)) == 0)
			keyInfo.flag |= ZM_KEY_FLAG_GK;
		else
			keyInfo.flag |= ZM_KEY_FLAG_PK;

		if (!strcmp(zdparm->u.crypt.alg, "NONE")) {
			/* u8_t zero_mac[]={0,0,0,0,0,0}; */

			/* Set key length to zero */
			keyInfo.keyLength = 0;

			/* del group key */
			if (zdparm->sta_addr[0] & 1) {
				/* if (macp->cardSetting.WPAIeLen==0)
				* { 802.1x dynamic WEP
				*    mDynKeyMode = 0;
				*    mKeyFormat[0] = 0;
				*    mPrivacyInvoked[0]=FALSE;
				*    mCap[0] &= ~CAP_PRIVACY;
				*    macp->cardSetting.EncryOnOff[0]=0;
				* }
				* mWpaBcKeyLen = mGkInstalled = 0;
				*/
			} else {
				/* if (memcmp(zero_mac,zdparm->sta_addr, 6)==0)
				* {
				*     mDynKeyMode=0;
				*    mKeyFormat[0]=0;
				*    pSetting->DynKeyMode=0;
				*    pSetting->EncryMode[0]=0;
				*    mDynKeyMode=0;
				* }
				*/
			}

			printk(KERN_ERR "Set Encryption Type NONE\n");
			return ret;
		} else if (!strcmp(zdparm->u.crypt.alg, "TKIP")) {
			zfiWlanSetEncryMode(dev, ZM_TKIP);
			/* //Linux Supplicant will inverse Tx/Rx key
			* //So we inverse it back, CWYang(+)
			* zfMemoryCopy(&temp[0], &keyInfo.key[16], 8);
			* zfMemoryCopy(&keyInfo.key[16], keyInfo.key[24], 8);
			* zfMemoryCopy(&keyInfo.key[24], &temp[0], 8);
			* u8_t temp;
			* int k;
			* for (k = 0; k < 8; k++)
			* {
			*     temp = keyInfo.key[16 + k];
			*     keyInfo.key[16 + k] = keyInfo.key[24 + k];
			*     keyInfo.key[24 + k] = temp;
			* }
			* CamEncryType = ZM_TKIP;
			* if (idx == 0)
			* {   // Pairwise key
			*     mKeyFormat[0] = CamEncryType;
			*     mDynKeyMode = pSetting->DynKeyMode = DYN_KEY_TKIP;
			* }
			*/
		} else if (!strcmp(zdparm->u.crypt.alg, "CCMP")) {
			zfiWlanSetEncryMode(dev, ZM_AES);
			/* CamEncryType = ZM_AES;
			* if (idx == 0)
			* {  // Pairwise key
			*    mKeyFormat[0] = CamEncryType;
			*    mDynKeyMode = pSetting->DynKeyMode = DYN_KEY_AES;
			* }
			*/
		} else if (!strcmp(zdparm->u.crypt.alg, "WEP")) {
			if (keyInfo.keyLength == 5) {
				/* WEP 64 */
				zfiWlanSetEncryMode(dev, ZM_WEP64);
				/* CamEncryType = ZM_WEP64; */
				/* tmpDynKeyMode=DYN_KEY_WEP64; */
			} else if (keyInfo.keyLength == 13) {
				/* keylen=13, WEP 128 */
				zfiWlanSetEncryMode(dev, ZM_WEP128);
				/* CamEncryType = ZM_WEP128; */
				/* tmpDynKeyMode=DYN_KEY_WEP128; */
			} else {
				zfiWlanSetEncryMode(dev, ZM_WEP256);
			}

	/* For Dynamic WEP key (Non-WPA Radius), the key ID range: 0-3
	* In WPA/RSN mode, the key ID range: 1-3, usually, a broadcast key.
	* For WEP key setting: we set mDynKeyMode and mKeyFormat in following
	* case:
	*   1. For 802.1x dynamically generated WEP key method.
	*   2. For WPA/RSN mode, but key id == 0.
	*	(But this is an impossible case)
	* So, only check case 1.
	* if (macp->cardSetting.WPAIeLen==0)
	* {
	*    mKeyFormat[0] = CamEncryType;
	*    mDynKeyMode = pSetting->DynKeyMode = tmpDynKeyMode;
	*    mPrivacyInvoked[0]=TRUE;
	*    mCap[0] |= CAP_PRIVACY;
	*    macp->cardSetting.EncryOnOff[0]=1;
	* }
	*/
		}

		/* DUMP key context */
		/* #ifdef WPA_DEBUG */
		if (keyInfo.keyLength > 0) {
			printk(KERN_WARNING
						"Otus: Key Context:\n");
			for (ii = 0; ii < keyInfo.keyLength; ) {
				printk(KERN_WARNING
						"0x%02x ", keyInfo.key[ii]);
				if ((++ii % 16) == 0)
					printk(KERN_WARNING "\n");
			}
			printk(KERN_WARNING "\n");
		}
		/* #endif */

		/* Set encrypt mode */
		/* zfiWlanSetEncryMode(dev, CamEncryType); */
		vapId = zfLnxGetVapId(dev);
		if (vapId == 0xffff)
			keyInfo.vapId = 0;
		else
			keyInfo.vapId = vapId + 1;
		keyInfo.vapAddr[0] = keyInfo.macAddr[0];
		keyInfo.vapAddr[1] = keyInfo.macAddr[1];
		keyInfo.vapAddr[2] = keyInfo.macAddr[2];

		zfiWlanSetKey(dev, keyInfo);

		/* zfiWlanDisable(dev); */
		/* zfiWlanEnable(dev); */
		break;
	case ZD_CMD_SET_MLME:
		printk(KERN_ERR "usbdrv_wpa_ioctl: ZD_CMD_SET_MLME\n");

		/* Translate STA's address */
		sprintf(mac_addr, "%02x:%02x:%02x:%02x:%02x:%02x",
			zdparm->sta_addr[0], zdparm->sta_addr[1],
			zdparm->sta_addr[2], zdparm->sta_addr[3],
			zdparm->sta_addr[4], zdparm->sta_addr[5]);

		switch (zdparm->u.mlme.cmd) {
		case MLME_STA_DEAUTH:
			printk(KERN_WARNING
				" -------Call zfiWlanDeauth, reason:%d\n",
				zdparm->u.mlme.reason_code);
			if (zfiWlanDeauth(dev, (u16_t *) zdparm->sta_addr,
				zdparm->u.mlme.reason_code) != 0)
				printk(KERN_ERR "Can't deauthencate STA: %s\n",
					mac_addr);
			else
				printk(KERN_ERR "Deauthenticate STA: %s"
					"with reason code: %d\n",
					mac_addr, zdparm->u.mlme.reason_code);
			break;
		case MLME_STA_DISASSOC:
			printk(KERN_WARNING
				" -------Call zfiWlanDeauth, reason:%d\n",
				zdparm->u.mlme.reason_code);
			if (zfiWlanDeauth(dev, (u16_t *) zdparm->sta_addr,
				zdparm->u.mlme.reason_code) != 0)
				printk(KERN_ERR "Can't disassociate STA: %s\n",
					mac_addr);
			else
				printk(KERN_ERR "Disassociate STA: %s"
					"with reason code: %d\n",
					mac_addr, zdparm->u.mlme.reason_code);
			break;
		default:
			printk(KERN_ERR "MLME command: 0x%04x not support\n",
				zdparm->u.mlme.cmd);
			break;
		}

		break;
	case ZD_CMD_SCAN_REQ:
		printk(KERN_ERR "usbdrv_wpa_ioctl: ZD_CMD_SCAN_REQ\n");
		break;
	case ZD_CMD_SET_GENERIC_ELEMENT: {
		u8_t len, *wpaie;
		printk(KERN_ERR "usbdrv_wpa_ioctl:"
					" ZD_CMD_SET_GENERIC_ELEMENT\n");

		/* Copy the WPA IE
		* zm_msg1_mm(ZM_LV_0, "CWY - wpaie Length : ",
		* zdparm->u.generic_elem.len);
		*/
		printk(KERN_ERR "wpaie Length : % d\n",
						zdparm->u.generic_elem.len);
		if (zfiWlanQueryWlanMode(dev) == ZM_MODE_AP) {
			/* AP Mode */
			zfiWlanSetWpaIe(dev, zdparm->u.generic_elem.data,
					zdparm->u.generic_elem.len);
		} else {
			macp->supLen = zdparm->u.generic_elem.len;
			memcpy(macp->supIe, zdparm->u.generic_elem.data,
				zdparm->u.generic_elem.len);
		}
		zfiWlanSetWpaSupport(dev, 1);
		/* zfiWlanSetWpaIe(dev, zdparm->u.generic_elem.data,
		* zdparm->u.generic_elem.len);
		*/
		len = zdparm->u.generic_elem.len;
		wpaie = zdparm->u.generic_elem.data;

		printk(KERN_ERR "wd->ap.wpaLen : % d\n", len);

		/* DUMP WPA IE */
		for(ii = 0; ii < len;) {
			printk(KERN_ERR "0x%02x ", wpaie[ii]);

			if((++ii % 16) == 0)
				printk(KERN_ERR "\n");
		}
		printk(KERN_ERR "\n");

		/* #ifdef ZM_HOSTAPD_SUPPORT
		* if (wd->wlanMode == ZM_MODE_AP)
		* {// Update Beacon FIFO in the next TBTT.
		*     memcpy(&mWPAIe, pSetting->WPAIe, pSetting->WPAIeLen);
		*     printk(KERN_ERR "Copy WPA IE into mWPAIe\n");
		* }
		* #endif
		*/
		break;
	}

	/* #ifdef ZM_HOSTAPD_SUPPORT */
	case ZD_CMD_GET_TSC:
		printk(KERN_ERR "usbdrv_wpa_ioctl : ZD_CMD_GET_TSC\n");
		break;
	/* #endif */

	default:
		printk(KERN_ERR "usbdrv_wpa_ioctl default : 0x%04x\n",
			zdparm->cmd);
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef ZM_ENABLE_CENC
int usbdrv_cenc_ioctl(struct net_device *dev, struct zydas_cenc_param *zdparm)
{
	/* struct usbdrv_private *macp = dev->ml_priv; */
	struct zsKeyInfo keyInfo;
	u16_t apId;
	u8_t bc_addr[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int ret = 0;
	int ii;

	/* Get the AP Id */
	apId = zfLnxGetVapId(dev);

	if (apId == 0xffff) {
		apId = 0;
	} else {
		apId = apId + 1;
	}

	switch (zdparm->cmd) {
	case ZM_CMD_CENC_SETCENC:
		printk(KERN_ERR "ZM_CMD_CENC_SETCENC\n");
		printk(KERN_ERR "length : % d\n", zdparm->len);
		printk(KERN_ERR "policy : % d\n", zdparm->u.info.cenc_policy);
		break;
	case ZM_CMD_CENC_SETKEY:
		/* ret = wai_ioctl_setkey(vap, ioctl_msg); */
		printk(KERN_ERR "ZM_CMD_CENC_SETKEY\n");

		printk(KERN_ERR "MAC address = ");
		for (ii = 0; ii < 6; ii++) {
			printk(KERN_ERR "0x%02x ",
				zdparm->u.crypt.sta_addr[ii]);
		}
		printk(KERN_ERR "\n");

		printk(KERN_ERR "Key Index : % d\n", zdparm->u.crypt.keyid);
		printk(KERN_ERR "Encryption key = ");
		for (ii = 0; ii < 16; ii++) {
			printk(KERN_ERR "0x%02x ", zdparm->u.crypt.key[ii]);
		}
		printk(KERN_ERR "\n");

		printk(KERN_ERR "MIC key = ");
		for(ii = 16; ii < ZM_CENC_KEY_SIZE; ii++) {
			printk(KERN_ERR "0x%02x ", zdparm->u.crypt.key[ii]);
		}
		printk(KERN_ERR "\n");

		/* Set up key information */
		keyInfo.keyLength = ZM_CENC_KEY_SIZE;
		keyInfo.keyIndex = zdparm->u.crypt.keyid;
		keyInfo.flag = ZM_KEY_FLAG_AUTHENTICATOR | ZM_KEY_FLAG_CENC;
		keyInfo.key = zdparm->u.crypt.key;
		keyInfo.macAddr = (u16_t *)zdparm->u.crypt.sta_addr;

		/* Identify the MAC address information */
		if (memcmp(zdparm->u.crypt.sta_addr, bc_addr,
				sizeof(bc_addr)) == 0) {
			keyInfo.flag |= ZM_KEY_FLAG_GK;
			keyInfo.vapId = apId;
			memcpy(keyInfo.vapAddr, dev->dev_addr, ETH_ALEN);
		} else {
			keyInfo.flag |= ZM_KEY_FLAG_PK;
		}

		zfiWlanSetKey(dev, keyInfo);

		break;
	case ZM_CMD_CENC_REKEY:
		/* ret = wai_ioctl_rekey(vap, ioctl_msg); */
		printk(KERN_ERR "ZM_CMD_CENC_REKEY\n");
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	/* if (retv == ENETRESET) */
	/* retv = IS_UP_AUTO(vap) ? ieee80211_open(vap->iv_dev) : 0; */

	return ret;
}
#endif /* ZM_ENABLE_CENC */

int usbdrv_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	/* struct usbdrv_private *macp; */
	/* void *regp; */
	struct zdap_ioctl zdreq;
	struct iwreq *wrq = (struct iwreq *)ifr;
	struct athr_wlan_param zdparm;
	struct usbdrv_private *macp = dev->ml_priv;

	int err = 0, val = 0;
	int changed = 0;

	/* regp = macp->regp; */

	if (!netif_running(dev))
		return -EINVAL;

	switch (cmd) {
	case SIOCGIWNAME:
		strcpy(wrq->u.name, "IEEE 802.11-DS");
		break;
	case SIOCGIWAP:
		err = usbdrvwext_giwap(dev, NULL, &wrq->u.ap_addr, NULL);
		break;
	case SIOCSIWAP:
		err = usbdrvwext_siwap(dev, NULL, &wrq->u.ap_addr, NULL);
		break;
	case SIOCGIWMODE:
		err = usbdrvwext_giwmode(dev, NULL, &wrq->u.mode, NULL);
		break;
	case SIOCSIWESSID:
		printk(KERN_ERR "CWY - usbdrvwext_siwessid\n");
		/* err = usbdrv_ioctl_setessid(dev, &wrq->u.essid); */
		err = usbdrvwext_siwessid(dev, NULL, &wrq->u.essid, NULL);

		if (!err)
			changed = 1;
		break;
	case SIOCGIWESSID:
		err = usbdrvwext_giwessid(dev, NULL, &wrq->u.essid, NULL);
		break;
	case SIOCSIWRTS:
		err = usbdrv_ioctl_setrts(dev, &wrq->u.rts);
		if (! err)
			changed = 1;
		break;
	/* set_auth */
	case SIOCIWFIRSTPRIV + 0x2: {
		/* printk("CWY - SIOCIWFIRSTPRIV + 0x2(set_auth)\n"); */
		if (!capable(CAP_NET_ADMIN)) {
			err = -EPERM;
			break;
		}
		val = *((int *) wrq->u.name);
		if ((val < 0) || (val > 2)) {
			err = -EINVAL;
			break;
		} else {
			zfiWlanSetAuthenticationMode(dev, val);

			if (macp->DeviceOpened == 1) {
				zfiWlanDisable(dev, 0);
				zfiWlanEnable(dev);
			}

			err = 0;
			changed = 1;
		}
	}
		break;
	/* get_auth */
	case SIOCIWFIRSTPRIV + 0x3: {
		int AuthMode = ZM_AUTH_MODE_OPEN;

		/* printk("CWY - SIOCIWFIRSTPRIV + 0x3(get_auth)\n"); */

		if (wrq->u.data.pointer) {
			wrq->u.data.flags = 1;

			AuthMode = zfiWlanQueryAuthenticationMode(dev, 0);
			if (AuthMode == ZM_AUTH_MODE_OPEN) {
				wrq->u.data.length = 12;

				if (copy_to_user(wrq->u.data.pointer,
					"open system", 12)) {
						return -EFAULT;
				}
			} else if (AuthMode == ZM_AUTH_MODE_SHARED_KEY)	{
				wrq->u.data.length = 11;

				if (copy_to_user(wrq->u.data.pointer,
					"shared key", 11)) {
							return -EFAULT;
				}
			} else if (AuthMode == ZM_AUTH_MODE_AUTO) {
				wrq->u.data.length = 10;

				if (copy_to_user(wrq->u.data.pointer,
					"auto mode", 10)) {
							return -EFAULT;
				}
			} else {
				return -EFAULT;
			}
		}
	}
		break;
	/* debug command */
	case ZDAPIOCTL:
		if (copy_from_user(&zdreq, ifr->ifr_data, sizeof(zdreq))) {
			printk(KERN_ERR "usbdrv : copy_from_user error\n");
			return -EFAULT;
		}

		/* printk(KERN_WARNING
		* "usbdrv : cmd = % 2x, reg = 0x%04lx,
		*value = 0x%08lx\n",
		* zdreq.cmd, zdreq.addr, zdreq.value);
		*/
		zfLnxPrivateIoctl(dev, &zdreq);

		err = 0;
		break;
	case ZD_IOCTL_WPA:
		if (copy_from_user(&zdparm, ifr->ifr_data,
			sizeof(struct athr_wlan_param))) {
			printk(KERN_ERR "usbdrv : copy_from_user error\n");
			return -EFAULT;
		}

		usbdrv_wpa_ioctl(dev, &zdparm);
		err = 0;
		break;
	case ZD_IOCTL_PARAM: {
		int *p;
		int op;
		int arg;

		/* Point to the name field and retrieve the
		* op and arg elements.
		*/
		p = (int *)wrq->u.name;
		op = *p++;
		arg = *p;

		if (op == ZD_PARAM_ROAMING) {
			printk(KERN_ERR
			"*************ZD_PARAM_ROAMING : % d\n", arg);
			/* macp->cardSetting.ap_scan=(U8)arg; */
		}
		if (op == ZD_PARAM_PRIVACY) {
			printk(KERN_ERR "ZD_IOCTL_PRIVACY : ");

			/* Turn on the privacy invoke flag */
			if (arg) {
				/* mCap[0] |= CAP_PRIVACY; */
				/* macp->cardSetting.EncryOnOff[0] = 1; */
				printk(KERN_ERR "enable\n");

			} else {
				/* mCap[0] &= ~CAP_PRIVACY; */
				/* macp->cardSetting.EncryOnOff[0] = 0; */
				printk(KERN_ERR "disable\n");
			}
			/* changed=1; */
		}
		if (op == ZD_PARAM_WPA) {

		printk(KERN_ERR "ZD_PARAM_WPA : ");

		if (arg) {
			printk(KERN_ERR "enable\n");

			if (zfiWlanQueryWlanMode(dev) != ZM_MODE_AP) {
				printk(KERN_ERR "Station Mode\n");
				/* zfiWlanQueryWpaIe(dev, (u8_t *)
					&wpaIe, &wpalen); */
				/* printk("wpaIe : % 2x, % 2x, % 2x\n",
					wpaIe[21], wpaIe[22], wpaIe[23]); */
				/* printk("rsnIe : % 2x, % 2x, % 2x\n",
					wpaIe[17], wpaIe[18], wpaIe[19]); */
				if ((macp->supIe[21] == 0x50) &&
					(macp->supIe[22] == 0xf2) &&
					(macp->supIe[23] == 0x2)) {
					printk(KERN_ERR
				"wd->sta.authMode = ZM_AUTH_MODE_WPAPSK\n");
				/* wd->sta.authMode = ZM_AUTH_MODE_WPAPSK; */
				/* wd->ws.authMode = ZM_AUTH_MODE_WPAPSK; */
				zfiWlanSetAuthenticationMode(dev,
							ZM_AUTH_MODE_WPAPSK);
				} else if ((macp->supIe[21] == 0x50) &&
					(macp->supIe[22] == 0xf2) &&
					(macp->supIe[23] == 0x1)) {
					printk(KERN_ERR
				"wd->sta.authMode = ZM_AUTH_MODE_WPA\n");
				/* wd->sta.authMode = ZM_AUTH_MODE_WPA; */
				/* wd->ws.authMode = ZM_AUTH_MODE_WPA; */
				zfiWlanSetAuthenticationMode(dev,
							ZM_AUTH_MODE_WPA);
					} else if ((macp->supIe[17] == 0xf) &&
						(macp->supIe[18] == 0xac) &&
						(macp->supIe[19] == 0x2))
					{
						printk(KERN_ERR
				"wd->sta.authMode = ZM_AUTH_MODE_WPA2PSK\n");
				/* wd->sta.authMode = ZM_AUTH_MODE_WPA2PSK; */
				/* wd->ws.authMode = ZM_AUTH_MODE_WPA2PSK; */
				zfiWlanSetAuthenticationMode(dev,
				ZM_AUTH_MODE_WPA2PSK);
			} else if ((macp->supIe[17] == 0xf) &&
				(macp->supIe[18] == 0xac) &&
				(macp->supIe[19] == 0x1))
				{
					printk(KERN_ERR
				"wd->sta.authMode = ZM_AUTH_MODE_WPA2\n");
				/* wd->sta.authMode = ZM_AUTH_MODE_WPA2; */
				/* wd->ws.authMode = ZM_AUTH_MODE_WPA2; */
				zfiWlanSetAuthenticationMode(dev,
							ZM_AUTH_MODE_WPA2);
			}
			/* WPA or WPAPSK */
			if ((macp->supIe[21] == 0x50) ||
				(macp->supIe[22] == 0xf2)) {
				if (macp->supIe[11] == 0x2) {
					printk(KERN_ERR
				"wd->sta.wepStatus = ZM_ENCRYPTION_TKIP\n");
				/* wd->sta.wepStatus = ZM_ENCRYPTION_TKIP; */
				/* wd->ws.wepStatus = ZM_ENCRYPTION_TKIP; */
				zfiWlanSetWepStatus(dev, ZM_ENCRYPTION_TKIP);
			} else {
				printk(KERN_ERR
				"wd->sta.wepStatus = ZM_ENCRYPTION_AES\n");
				/* wd->sta.wepStatus = ZM_ENCRYPTION_AES; */
				/* wd->ws.wepStatus = ZM_ENCRYPTION_AES; */
				zfiWlanSetWepStatus(dev, ZM_ENCRYPTION_AES);
				}
			}
			//WPA2 or WPA2PSK
			if ((macp->supIe[17] == 0xf) ||
				(macp->supIe[18] == 0xac)) {
				if (macp->supIe[13] == 0x2) {
					printk(KERN_ERR
				"wd->sta.wepStatus = ZM_ENCRYPTION_TKIP\n");
				/* wd->sta.wepStatus = ZM_ENCRYPTION_TKIP; */
				/* wd->ws.wepStatus = ZM_ENCRYPTION_TKIP; */
				zfiWlanSetWepStatus(dev, ZM_ENCRYPTION_TKIP);
				} else {
					printk(KERN_ERR
				"wd->sta.wepStatus = ZM_ENCRYPTION_AES\n");
				/* wd->sta.wepStatus = ZM_ENCRYPTION_AES; */
				/* wd->ws.wepStatus = ZM_ENCRYPTION_AES; */
				zfiWlanSetWepStatus(dev, ZM_ENCRYPTION_AES);
					}
				}
			}
			zfiWlanSetWpaSupport(dev, 1);
		} else {
			/* Reset the WPA related variables */
			printk(KERN_ERR "disable\n");

			zfiWlanSetWpaSupport(dev, 0);
			zfiWlanSetAuthenticationMode(dev, ZM_AUTH_MODE_OPEN);
			zfiWlanSetWepStatus(dev, ZM_ENCRYPTION_WEP_DISABLED);

			/* Now we only set the length in the WPA IE
			* field to zero.
			*macp->cardSetting.WPAIe[1] = 0;
			*/
			}
		}

		if (op == ZD_PARAM_COUNTERMEASURES) {
			printk(KERN_ERR
				"****************ZD_PARAM_COUNTERMEASURES : ");

			if(arg) {
				/*    mCounterMeasureState=1; */
				printk(KERN_ERR "enable\n");
			} else {
				/*    mCounterMeasureState=0; */
				printk(KERN_ERR "disable\n");
			}
		}
		if (op == ZD_PARAM_DROPUNENCRYPTED) {
			printk(KERN_ERR "ZD_PARAM_DROPUNENCRYPTED : ");

			if(arg) {
				printk(KERN_ERR "enable\n");
			} else {
				printk(KERN_ERR "disable\n");
			}
		}
		if (op == ZD_PARAM_AUTH_ALGS) {
			printk(KERN_ERR "ZD_PARAM_AUTH_ALGS : ");

			if (arg == 0) {
				printk(KERN_ERR "OPEN_SYSTEM\n");
			} else {
				printk(KERN_ERR "SHARED_KEY\n");
			}
		}
		if (op == ZD_PARAM_WPS_FILTER) {
			printk(KERN_ERR "ZD_PARAM_WPS_FILTER : ");

			if (arg) {
				/*    mCounterMeasureState=1; */
				macp->forwardMgmt = 1;
				printk(KERN_ERR "enable\n");
			} else {
				/*    mCounterMeasureState=0; */
				macp->forwardMgmt = 0;
				printk(KERN_ERR "disable\n");
			}
		}
	}
		err = 0;
		break;
	case ZD_IOCTL_GETWPAIE: {
		struct ieee80211req_wpaie req_wpaie;
		u16_t apId, i, j;

		/* Get the AP Id */
		apId = zfLnxGetVapId(dev);

		if (apId == 0xffff) {
			apId = 0;
		} else {
			apId = apId + 1;
		}

		if (copy_from_user(&req_wpaie, ifr->ifr_data,
					sizeof(struct ieee80211req_wpaie))) {
			printk(KERN_ERR "usbdrv : copy_from_user error\n");
			return -EFAULT;
		}

		for (i = 0; i < ZM_OAL_MAX_STA_SUPPORT; i++) {
			for (j = 0; j < IEEE80211_ADDR_LEN; j++) {
				if (macp->stawpaie[i].wpa_macaddr[j] !=
						req_wpaie.wpa_macaddr[j])
				break;
			}
			if (j == 6)
			break;
		}

		if (i < ZM_OAL_MAX_STA_SUPPORT) {
		/* printk("ZD_IOCTL_GETWPAIE - sta index = % d\n", i); */
		memcpy(req_wpaie.wpa_ie, macp->stawpaie[i].wpa_ie,
							IEEE80211_MAX_IE_SIZE);
		}

		if (copy_to_user(wrq->u.data.pointer, &req_wpaie,
				sizeof(struct ieee80211req_wpaie))) {
			return -EFAULT;
		}
	}

		err = 0;
		break;
	#ifdef ZM_ENABLE_CENC
	case ZM_IOCTL_CENC:
		if (copy_from_user(&macp->zd_wpa_req, ifr->ifr_data,
			sizeof(struct athr_wlan_param))) {
			printk(KERN_ERR "usbdrv : copy_from_user error\n");
			return -EFAULT;
		}

		usbdrv_cenc_ioctl(dev,
				(struct zydas_cenc_param *)&macp->zd_wpa_req);
		err = 0;
		break;
	#endif /* ZM_ENABLE_CENC */
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}
