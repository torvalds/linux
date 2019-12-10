// SPDX-License-Identifier: GPL-2.0
/*
 *   Driver for KeyStream 11b/g wireless LAN
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/if_arp.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

static int wep_on_off;
#define	WEP_OFF		0
#define	WEP_ON_64BIT	1
#define	WEP_ON_128BIT	2

#include "ks_wlan.h"
#include "ks_hostif.h"
#include "ks_wlan_ioctl.h"

/* Include Wireless Extension definition and check version */
#include <linux/wireless.h>
#define WIRELESS_SPY	/* enable iwspy support */
#include <net/iw_handler.h>	/* New driver API */

/* Frequency list (map channels to frequencies) */
static const long frequency_list[] = {
	2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

/* A few details needed for WEP (Wireless Equivalent Privacy) */
#define MAX_KEY_SIZE 13	/* 128 (?) bits */
#define MIN_KEY_SIZE  5	/* 40 bits RC4 - WEP */
struct wep_key {
	u16 len;
	u8 key[16];	/* 40-bit and 104-bit keys */
};

/*
 *	function prototypes
 */
static int ks_wlan_open(struct net_device *dev);
static void ks_wlan_tx_timeout(struct net_device *dev, unsigned int txqueue);
static int ks_wlan_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int ks_wlan_close(struct net_device *dev);
static void ks_wlan_set_rx_mode(struct net_device *dev);
static struct net_device_stats *ks_wlan_get_stats(struct net_device *dev);
static int ks_wlan_set_mac_address(struct net_device *dev, void *addr);
static int ks_wlan_netdev_ioctl(struct net_device *dev, struct ifreq *rq,
				int cmd);

static atomic_t update_phyinfo;
static struct timer_list update_phyinfo_timer;
static
int ks_wlan_update_phy_information(struct ks_wlan_private *priv)
{
	struct iw_statistics *wstats = &priv->wstats;

	netdev_dbg(priv->net_dev, "in_interrupt = %ld\n", in_interrupt());

	if (priv->dev_state < DEVICE_STATE_READY)
		return -EBUSY;	/* not finished initialize */

	if (atomic_read(&update_phyinfo))
		return -EPERM;

	/* The status */
	wstats->status = priv->reg.operation_mode;	/* Operation mode */

	/* Signal quality and co. But where is the noise level ??? */
	hostif_sme_enqueue(priv, SME_PHY_INFO_REQUEST);

	/* interruptible_sleep_on_timeout(&priv->confirm_wait, HZ/2); */
	if (!wait_for_completion_interruptible_timeout
	    (&priv->confirm_wait, HZ / 2)) {
		netdev_dbg(priv->net_dev, "wait time out!!\n");
	}

	atomic_inc(&update_phyinfo);
	update_phyinfo_timer.expires = jiffies + HZ;	/* 1sec */
	add_timer(&update_phyinfo_timer);

	return 0;
}

static
void ks_wlan_update_phyinfo_timeout(struct timer_list *unused)
{
	pr_debug("in_interrupt = %ld\n", in_interrupt());
	atomic_set(&update_phyinfo, 0);
}

int ks_wlan_setup_parameter(struct ks_wlan_private *priv,
			    unsigned int commit_flag)
{
	hostif_sme_enqueue(priv, SME_STOP_REQUEST);

	if (commit_flag & SME_RTS)
		hostif_sme_enqueue(priv, SME_RTS_THRESHOLD_REQUEST);
	if (commit_flag & SME_FRAG)
		hostif_sme_enqueue(priv, SME_FRAGMENTATION_THRESHOLD_REQUEST);

	if (commit_flag & SME_WEP_INDEX)
		hostif_sme_enqueue(priv, SME_WEP_INDEX_REQUEST);
	if (commit_flag & SME_WEP_VAL1)
		hostif_sme_enqueue(priv, SME_WEP_KEY1_REQUEST);
	if (commit_flag & SME_WEP_VAL2)
		hostif_sme_enqueue(priv, SME_WEP_KEY2_REQUEST);
	if (commit_flag & SME_WEP_VAL3)
		hostif_sme_enqueue(priv, SME_WEP_KEY3_REQUEST);
	if (commit_flag & SME_WEP_VAL4)
		hostif_sme_enqueue(priv, SME_WEP_KEY4_REQUEST);
	if (commit_flag & SME_WEP_FLAG)
		hostif_sme_enqueue(priv, SME_WEP_FLAG_REQUEST);

	if (commit_flag & SME_RSN) {
		hostif_sme_enqueue(priv, SME_RSN_ENABLED_REQUEST);
		hostif_sme_enqueue(priv, SME_RSN_MODE_REQUEST);
	}
	if (commit_flag & SME_RSN_MULTICAST)
		hostif_sme_enqueue(priv, SME_RSN_MCAST_REQUEST);
	if (commit_flag & SME_RSN_UNICAST)
		hostif_sme_enqueue(priv, SME_RSN_UCAST_REQUEST);
	if (commit_flag & SME_RSN_AUTH)
		hostif_sme_enqueue(priv, SME_RSN_AUTH_REQUEST);

	hostif_sme_enqueue(priv, SME_MODE_SET_REQUEST);

	hostif_sme_enqueue(priv, SME_START_REQUEST);

	return 0;
}

/*
 * Initial Wireless Extension code for Ks_Wlannet driver by :
 *	Jean Tourrilhes <jt@hpl.hp.com> - HPL - 17 November 00
 * Conversion to new driver API by :
 *	Jean Tourrilhes <jt@hpl.hp.com> - HPL - 26 March 02
 * Javier also did a good amount of work here, adding some new extensions
 * and fixing my code. Let's just say that without him this code just
 * would not work at all... - Jean II
 */

static int ks_wlan_get_name(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *cwrq,
			    char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (priv->dev_state < DEVICE_STATE_READY)
		strcpy(cwrq->name, "NOT READY!");
	else if (priv->reg.phy_type == D_11B_ONLY_MODE)
		strcpy(cwrq->name, "IEEE 802.11b");
	else if (priv->reg.phy_type == D_11G_ONLY_MODE)
		strcpy(cwrq->name, "IEEE 802.11g");
	else
		strcpy(cwrq->name, "IEEE 802.11b/g");

	return 0;
}

static int ks_wlan_set_freq(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *fwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int channel;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* If setting by frequency, convert to a channel */
	if ((fwrq->freq.e == 1) &&
	    (fwrq->freq.m >= 241200000) && (fwrq->freq.m <= 248700000)) {
		int f = fwrq->freq.m / 100000;
		int c = 0;

		while ((c < 14) && (f != frequency_list[c]))
			c++;
		/* Hack to fall through... */
		fwrq->freq.e = 0;
		fwrq->freq.m = c + 1;
	}
	/* Setting by channel number */
	if ((fwrq->freq.m > 1000) || (fwrq->freq.e > 0))
		return -EOPNOTSUPP;

	channel = fwrq->freq.m;
	/* We should do a better check than that,
	 * based on the card capability !!!
	 */
	if ((channel < 1) || (channel > 14)) {
		netdev_dbg(dev, "%s: New channel value of %d is invalid!\n",
			   dev->name, fwrq->freq.m);
		return -EINVAL;
	}

	/* Yes ! We can set it !!! */
	priv->reg.channel = (u8)(channel);
	priv->need_commit |= SME_MODE_SET;

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_freq(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *fwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int f;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (is_connect_status(priv->connect_status))
		f = (int)priv->current_ap.channel;
	else
		f = (int)priv->reg.channel;

	fwrq->freq.m = frequency_list[f - 1] * 100000;
	fwrq->freq.e = 1;

	return 0;
}

static int ks_wlan_set_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	size_t len;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* Check if we asked for `any' */
	if (!dwrq->essid.flags) {
		/* Just send an empty SSID list */
		memset(priv->reg.ssid.body, 0, sizeof(priv->reg.ssid.body));
		priv->reg.ssid.size = 0;
	} else {
		len = dwrq->essid.length;
		/* iwconfig uses nul termination in SSID.. */
		if (len > 0 && extra[len - 1] == '\0')
			len--;

		/* Check the size of the string */
		if (len > IW_ESSID_MAX_SIZE)
			return -EINVAL;

		/* Set the SSID */
		memset(priv->reg.ssid.body, 0, sizeof(priv->reg.ssid.body));
		memcpy(priv->reg.ssid.body, extra, len);
		priv->reg.ssid.size = len;
	}
	/* Write it to the card */
	priv->need_commit |= SME_MODE_SET;

	ks_wlan_setup_parameter(priv, priv->need_commit);
	priv->need_commit = 0;
	return 0;
}

static int ks_wlan_get_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* Note : if dwrq->flags != 0, we should
	 * get the relevant SSID from the SSID list...
	 */
	if (priv->reg.ssid.size != 0) {
		/* Get the current SSID */
		memcpy(extra, priv->reg.ssid.body, priv->reg.ssid.size);

		/* If none, we may want to get the one that was set */

		/* Push it out ! */
		dwrq->essid.length = priv->reg.ssid.size;
		dwrq->essid.flags = 1;	/* active */
	} else {
		dwrq->essid.length = 0;
		dwrq->essid.flags = 0;	/* ANY */
	}

	return 0;
}

static int ks_wlan_set_wap(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *awrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (priv->reg.operation_mode != MODE_ADHOC &&
	    priv->reg.operation_mode != MODE_INFRASTRUCTURE) {
		eth_zero_addr(priv->reg.bssid);
		return -EOPNOTSUPP;
	}

	ether_addr_copy(priv->reg.bssid, awrq->ap_addr.sa_data);
	if (is_valid_ether_addr((u8 *)priv->reg.bssid))
		priv->need_commit |= SME_MODE_SET;

	netdev_dbg(dev, "bssid = %pM\n", priv->reg.bssid);

	/* Write it to the card */
	if (priv->need_commit) {
		priv->need_commit |= SME_MODE_SET;
		return -EINPROGRESS;	/* Call commit handler */
	}
	return 0;
}

static int ks_wlan_get_wap(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *awrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (is_connect_status(priv->connect_status))
		ether_addr_copy(awrq->ap_addr.sa_data, priv->current_ap.bssid);
	else
		eth_zero_addr(awrq->ap_addr.sa_data);

	awrq->ap_addr.sa_family = ARPHRD_ETHER;

	return 0;
}

static int ks_wlan_set_nick(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* Check the size of the string */
	if (dwrq->data.length > 16 + 1)
		return -E2BIG;

	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, extra, dwrq->data.length);

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_nick(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	strncpy(extra, priv->nick, 16);
	extra[16] = '\0';
	dwrq->data.length = strlen(extra) + 1;

	return 0;
}

static int ks_wlan_set_rate(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int i = 0;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (priv->reg.phy_type == D_11B_ONLY_MODE) {
		if (vwrq->bitrate.fixed == 1) {
			switch (vwrq->bitrate.value) {
			case 11000000:
			case 5500000:
				priv->reg.rate_set.body[0] =
				    (u8)(vwrq->bitrate.value / 500000);
				break;
			case 2000000:
			case 1000000:
				priv->reg.rate_set.body[0] =
				    ((u8)(vwrq->bitrate.value / 500000)) |
				    BASIC_RATE;
				break;
			default:
				return -EINVAL;
			}
			priv->reg.tx_rate = TX_RATE_FIXED;
			priv->reg.rate_set.size = 1;
		} else {	/* vwrq->fixed == 0 */
			if (vwrq->bitrate.value > 0) {
				switch (vwrq->bitrate.value) {
				case 11000000:
					priv->reg.rate_set.body[3] =
					    TX_RATE_11M;
					i++;
					/* fall through */
				case 5500000:
					priv->reg.rate_set.body[2] = TX_RATE_5M;
					i++;
					/* fall through */
				case 2000000:
					priv->reg.rate_set.body[1] =
					    TX_RATE_2M | BASIC_RATE;
					i++;
					/* fall through */
				case 1000000:
					priv->reg.rate_set.body[0] =
					    TX_RATE_1M | BASIC_RATE;
					i++;
					break;
				default:
					return -EINVAL;
				}
				priv->reg.tx_rate = TX_RATE_MANUAL_AUTO;
				priv->reg.rate_set.size = i;
			} else {
				priv->reg.rate_set.body[3] = TX_RATE_11M;
				priv->reg.rate_set.body[2] = TX_RATE_5M;
				priv->reg.rate_set.body[1] =
				    TX_RATE_2M | BASIC_RATE;
				priv->reg.rate_set.body[0] =
				    TX_RATE_1M | BASIC_RATE;
				priv->reg.tx_rate = TX_RATE_FULL_AUTO;
				priv->reg.rate_set.size = 4;
			}
		}
	} else {	/* D_11B_ONLY_MODE or  D_11BG_COMPATIBLE_MODE */
		if (vwrq->bitrate.fixed == 1) {
			switch (vwrq->bitrate.value) {
			case 54000000:
			case 48000000:
			case 36000000:
			case 18000000:
			case 9000000:
				priv->reg.rate_set.body[0] =
				    (u8)(vwrq->bitrate.value / 500000);
				break;
			case 24000000:
			case 12000000:
			case 11000000:
			case 6000000:
			case 5500000:
			case 2000000:
			case 1000000:
				priv->reg.rate_set.body[0] =
				    ((u8)(vwrq->bitrate.value / 500000)) |
				    BASIC_RATE;
				break;
			default:
				return -EINVAL;
			}
			priv->reg.tx_rate = TX_RATE_FIXED;
			priv->reg.rate_set.size = 1;
		} else {	/* vwrq->fixed == 0 */
			if (vwrq->bitrate.value > 0) {
				switch (vwrq->bitrate.value) {
				case 54000000:
					priv->reg.rate_set.body[11] =
					    TX_RATE_54M;
					i++;
					/* fall through */
				case 48000000:
					priv->reg.rate_set.body[10] =
					    TX_RATE_48M;
					i++;
					/* fall through */
				case 36000000:
					priv->reg.rate_set.body[9] =
					    TX_RATE_36M;
					i++;
					/* fall through */
				case 24000000:
				case 18000000:
				case 12000000:
				case 11000000:
				case 9000000:
				case 6000000:
					if (vwrq->bitrate.value == 24000000) {
						priv->reg.rate_set.body[8] =
						    TX_RATE_18M;
						i++;
						priv->reg.rate_set.body[7] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[6] =
						    TX_RATE_24M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[5] =
						    TX_RATE_12M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[4] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_11M | BASIC_RATE;
						i++;
					} else if (vwrq->bitrate.value == 18000000) {
						priv->reg.rate_set.body[7] =
						    TX_RATE_18M;
						i++;
						priv->reg.rate_set.body[6] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[5] =
						    TX_RATE_12M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[4] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_11M | BASIC_RATE;
						i++;
					} else if (vwrq->bitrate.value == 12000000) {
						priv->reg.rate_set.body[6] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[5] =
						    TX_RATE_12M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[4] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_11M | BASIC_RATE;
						i++;
					} else if (vwrq->bitrate.value == 11000000) {
						priv->reg.rate_set.body[5] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[4] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_11M | BASIC_RATE;
						i++;
					} else if (vwrq->bitrate.value == 9000000) {
						priv->reg.rate_set.body[4] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
					} else {	/* vwrq->value == 6000000 */
						priv->reg.rate_set.body[3] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
					}
					/* fall through */
				case 5500000:
					priv->reg.rate_set.body[2] =
					    TX_RATE_5M | BASIC_RATE;
					i++;
					/* fall through */
				case 2000000:
					priv->reg.rate_set.body[1] =
					    TX_RATE_2M | BASIC_RATE;
					i++;
					/* fall through */
				case 1000000:
					priv->reg.rate_set.body[0] =
					    TX_RATE_1M | BASIC_RATE;
					i++;
					break;
				default:
					return -EINVAL;
				}
				priv->reg.tx_rate = TX_RATE_MANUAL_AUTO;
				priv->reg.rate_set.size = i;
			} else {
				priv->reg.rate_set.body[11] = TX_RATE_54M;
				priv->reg.rate_set.body[10] = TX_RATE_48M;
				priv->reg.rate_set.body[9] = TX_RATE_36M;
				priv->reg.rate_set.body[8] = TX_RATE_18M;
				priv->reg.rate_set.body[7] = TX_RATE_9M;
				priv->reg.rate_set.body[6] =
				    TX_RATE_24M | BASIC_RATE;
				priv->reg.rate_set.body[5] =
				    TX_RATE_12M | BASIC_RATE;
				priv->reg.rate_set.body[4] =
				    TX_RATE_6M | BASIC_RATE;
				priv->reg.rate_set.body[3] =
				    TX_RATE_11M | BASIC_RATE;
				priv->reg.rate_set.body[2] =
				    TX_RATE_5M | BASIC_RATE;
				priv->reg.rate_set.body[1] =
				    TX_RATE_2M | BASIC_RATE;
				priv->reg.rate_set.body[0] =
				    TX_RATE_1M | BASIC_RATE;
				priv->reg.tx_rate = TX_RATE_FULL_AUTO;
				priv->reg.rate_set.size = 12;
			}
		}
	}

	priv->need_commit |= SME_MODE_SET;

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_rate(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	netdev_dbg(dev, "in_interrupt = %ld update_phyinfo = %d\n",
		   in_interrupt(), atomic_read(&update_phyinfo));

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (!atomic_read(&update_phyinfo))
		ks_wlan_update_phy_information(priv);

	vwrq->bitrate.value = ((priv->current_rate) & RATE_MASK) * 500000;
	vwrq->bitrate.fixed = (priv->reg.tx_rate == TX_RATE_FIXED) ? 1 : 0;

	return 0;
}

static int ks_wlan_set_rts(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int rthr = vwrq->rts.value;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (vwrq->rts.disabled)
		rthr = 2347;
	if ((rthr < 0) || (rthr > 2347))
		return -EINVAL;

	priv->reg.rts = rthr;
	priv->need_commit |= SME_RTS;

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_rts(struct net_device *dev, struct iw_request_info *info,
			   union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	vwrq->rts.value = priv->reg.rts;
	vwrq->rts.disabled = (vwrq->rts.value >= 2347);
	vwrq->rts.fixed = 1;

	return 0;
}

static int ks_wlan_set_frag(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int fthr = vwrq->frag.value;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (vwrq->frag.disabled)
		fthr = 2346;
	if ((fthr < 256) || (fthr > 2346))
		return -EINVAL;

	fthr &= ~0x1;	/* Get an even value - is it really needed ??? */
	priv->reg.fragment = fthr;
	priv->need_commit |= SME_FRAG;

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_frag(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	vwrq->frag.value = priv->reg.fragment;
	vwrq->frag.disabled = (vwrq->frag.value >= 2346);
	vwrq->frag.fixed = 1;

	return 0;
}

static int ks_wlan_set_mode(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *uwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (uwrq->mode != IW_MODE_ADHOC &&
	    uwrq->mode != IW_MODE_INFRA)
		return -EINVAL;

	priv->reg.operation_mode = (uwrq->mode == IW_MODE_ADHOC) ?
				    MODE_ADHOC : MODE_INFRASTRUCTURE;
	priv->need_commit |= SME_MODE_SET;

	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_mode(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *uwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* If not managed, assume it's ad-hoc */
	uwrq->mode = (priv->reg.operation_mode == MODE_INFRASTRUCTURE) ?
		      IW_MODE_INFRA : IW_MODE_ADHOC;

	return 0;
}

static int ks_wlan_set_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_point *enc = &dwrq->encoding;
	struct wep_key key;
	int index = (enc->flags & IW_ENCODE_INDEX);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (enc->length > MAX_KEY_SIZE)
		return -EINVAL;

	/* for SLEEP MODE */
	if ((index < 0) || (index > 4))
		return -EINVAL;

	index = (index == 0) ? priv->reg.wep_index : (index - 1);

	/* Is WEP supported ? */
	/* Basic checking: do we have a key to set ? */
	if (enc->length > 0) {
		key.len = (enc->length > MIN_KEY_SIZE) ?
			   MAX_KEY_SIZE : MIN_KEY_SIZE;
		priv->reg.privacy_invoked = 0x01;
		priv->need_commit |= SME_WEP_FLAG;
		wep_on_off = (enc->length > MIN_KEY_SIZE) ?
			      WEP_ON_128BIT : WEP_ON_64BIT;
		/* Check if the key is not marked as invalid */
		if (enc->flags & IW_ENCODE_NOKEY)
			return 0;

		/* Cleanup */
		memset(key.key, 0, MAX_KEY_SIZE);
		/* Copy the key in the driver */
		if (copy_from_user(key.key, enc->pointer, enc->length)) {
			key.len = 0;
			return -EFAULT;
		}
		/* Send the key to the card */
		priv->reg.wep_key[index].size = key.len;
		memcpy(&priv->reg.wep_key[index].val[0], &key.key[0],
		       priv->reg.wep_key[index].size);
		priv->need_commit |= (SME_WEP_VAL1 << index);
		priv->reg.wep_index = index;
		priv->need_commit |= SME_WEP_INDEX;
	} else {
		if (enc->flags & IW_ENCODE_DISABLED) {
			priv->reg.wep_key[0].size = 0;
			priv->reg.wep_key[1].size = 0;
			priv->reg.wep_key[2].size = 0;
			priv->reg.wep_key[3].size = 0;
			priv->reg.privacy_invoked = 0x00;
			if (priv->reg.authenticate_type == AUTH_TYPE_SHARED_KEY)
				priv->need_commit |= SME_MODE_SET;

			priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;
			wep_on_off = WEP_OFF;
			priv->need_commit |= SME_WEP_FLAG;
		} else {
			/* set_wep_key(priv, index, 0, 0, 1);   xxx */
			if (priv->reg.wep_key[index].size == 0)
				return -EINVAL;
			priv->reg.wep_index = index;
			priv->need_commit |= SME_WEP_INDEX;
		}
	}

	/* Commit the changes if needed */
	if (enc->flags & IW_ENCODE_MODE)
		priv->need_commit |= SME_WEP_FLAG;

	if (enc->flags & IW_ENCODE_OPEN) {
		if (priv->reg.authenticate_type == AUTH_TYPE_SHARED_KEY)
			priv->need_commit |= SME_MODE_SET;

		priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;
	} else if (enc->flags & IW_ENCODE_RESTRICTED) {
		if (priv->reg.authenticate_type == AUTH_TYPE_OPEN_SYSTEM)
			priv->need_commit |= SME_MODE_SET;

		priv->reg.authenticate_type = AUTH_TYPE_SHARED_KEY;
	}
	if (priv->need_commit) {
		ks_wlan_setup_parameter(priv, priv->need_commit);
		priv->need_commit = 0;
	}
	return 0;
}

static int ks_wlan_get_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_point *enc = &dwrq->encoding;
	int index = (enc->flags & IW_ENCODE_INDEX) - 1;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	enc->flags = IW_ENCODE_DISABLED;

	/* Check encryption mode */
	switch (priv->reg.authenticate_type) {
	case AUTH_TYPE_OPEN_SYSTEM:
		enc->flags = IW_ENCODE_OPEN;
		break;
	case AUTH_TYPE_SHARED_KEY:
		enc->flags = IW_ENCODE_RESTRICTED;
		break;
	}

	/* Which key do we want ? -1 -> tx index */
	if ((index < 0) || (index >= 4))
		index = priv->reg.wep_index;
	if (priv->reg.privacy_invoked) {
		enc->flags &= ~IW_ENCODE_DISABLED;
		/* dwrq->flags |= IW_ENCODE_NOKEY; */
	}
	enc->flags |= index + 1;
	/* Copy the key to the user buffer */
	if (index >= 0 && index < 4) {
		enc->length = (priv->reg.wep_key[index].size <= 16) ?
				priv->reg.wep_key[index].size : 0;
		memcpy(extra, priv->reg.wep_key[index].val, enc->length);
	}

	return 0;
}

static int ks_wlan_get_range(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_range *range = (struct iw_range *)extra;
	int i, k;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	dwrq->data.length = sizeof(struct iw_range);
	memset(range, 0, sizeof(*range));
	range->min_nwid = 0x0000;
	range->max_nwid = 0x0000;
	range->num_channels = 14;
	/* Should be based on cap_rid.country to give only
	 * what the current card support
	 */
	k = 0;
	for (i = 0; i < 13; i++) {	/* channel 1 -- 13 */
		range->freq[k].i = i + 1;	/* List index */
		range->freq[k].m = frequency_list[i] * 100000;
		range->freq[k++].e = 1;	/* Values in table in MHz -> * 10^5 * 10 */
	}
	range->num_frequency = k;
	if (priv->reg.phy_type == D_11B_ONLY_MODE ||
	    priv->reg.phy_type == D_11BG_COMPATIBLE_MODE) {	/* channel 14 */
		range->freq[13].i = 14;	/* List index */
		range->freq[13].m = frequency_list[13] * 100000;
		range->freq[13].e = 1;	/* Values in table in MHz -> * 10^5 * 10 */
		range->num_frequency = 14;
	}

	/* Hum... Should put the right values there */
	range->max_qual.qual = 100;
	range->max_qual.level = 256 - 128;	/* 0 dBm? */
	range->max_qual.noise = 256 - 128;
	range->sensitivity = 1;

	if (priv->reg.phy_type == D_11B_ONLY_MODE) {
		range->bitrate[0] = 1e6;
		range->bitrate[1] = 2e6;
		range->bitrate[2] = 5.5e6;
		range->bitrate[3] = 11e6;
		range->num_bitrates = 4;
	} else {	/* D_11G_ONLY_MODE or D_11BG_COMPATIBLE_MODE */
		range->bitrate[0] = 1e6;
		range->bitrate[1] = 2e6;
		range->bitrate[2] = 5.5e6;
		range->bitrate[3] = 11e6;

		range->bitrate[4] = 6e6;
		range->bitrate[5] = 9e6;
		range->bitrate[6] = 12e6;
		if (IW_MAX_BITRATES < 9) {
			range->bitrate[7] = 54e6;
			range->num_bitrates = 8;
		} else {
			range->bitrate[7] = 18e6;
			range->bitrate[8] = 24e6;
			range->bitrate[9] = 36e6;
			range->bitrate[10] = 48e6;
			range->bitrate[11] = 54e6;

			range->num_bitrates = 12;
		}
	}

	/* Set an indication of the max TCP throughput
	 * in bit/s that we can expect using this interface.
	 * May be use for QoS stuff... Jean II
	 */
	if (i > 2)
		range->throughput = 5000 * 1000;
	else
		range->throughput = 1500 * 1000;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->encoding_size[0] = 5;	/* WEP: RC4 40 bits */
	range->encoding_size[1] = 13;	/* WEP: RC4 ~128 bits */
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = 4;

	/* power management not support */
	range->pmp_flags = IW_POWER_ON;
	range->pmt_flags = IW_POWER_ON;
	range->pm_capa = 0;

	/* Transmit Power - values are in dBm( or mW) */
	range->txpower[0] = -256;
	range->num_txpower = 1;
	range->txpower_capa = IW_TXPOW_DBM;
	/* range->txpower_capa = IW_TXPOW_MWATT; */

	range->we_version_source = 21;
	range->we_version_compiled = WIRELESS_EXT;

	range->retry_capa = IW_RETRY_ON;
	range->retry_flags = IW_RETRY_ON;
	range->r_time_flags = IW_RETRY_ON;

	/* Experimental measurements - boundary 11/5.5 Mb/s
	 *
	 * Note : with or without the (local->rssi), results
	 * are somewhat different. - Jean II
	 */
	range->avg_qual.qual = 50;
	range->avg_qual.level = 186;	/* -70 dBm */
	range->avg_qual.noise = 0;

	/* Event capability (kernel + driver) */
	range->event_capa[0] = (IW_EVENT_CAPA_K_0 |
				IW_EVENT_CAPA_MASK(SIOCGIWAP) |
				IW_EVENT_CAPA_MASK(SIOCGIWSCAN));
	range->event_capa[1] = IW_EVENT_CAPA_K_1;
	range->event_capa[4] = (IW_EVENT_CAPA_MASK(IWEVCUSTOM) |
				IW_EVENT_CAPA_MASK(IWEVMICHAELMICFAILURE));

	/* encode extension (WPA) capability */
	range->enc_capa = (IW_ENC_CAPA_WPA |
			   IW_ENC_CAPA_WPA2 |
			   IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP);
	return 0;
}

static int ks_wlan_set_power(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (vwrq->power.disabled) {
		priv->reg.power_mgmt = POWER_MGMT_ACTIVE;
	} else {
		if (priv->reg.operation_mode != MODE_INFRASTRUCTURE)
			return -EINVAL;
		priv->reg.power_mgmt = POWER_MGMT_SAVE1;
	}

	hostif_sme_enqueue(priv, SME_POW_MNGMT_REQUEST);

	return 0;
}

static int ks_wlan_get_power(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	vwrq->power.disabled = (priv->reg.power_mgmt <= 0);

	return 0;
}

static int ks_wlan_get_iwstats(struct net_device *dev,
			       struct iw_request_info *info,
			       union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	vwrq->qual.qual = 0;	/* not supported */
	vwrq->qual.level = priv->wstats.qual.level;
	vwrq->qual.noise = 0;	/* not supported */
	vwrq->qual.updated = 0;

	return 0;
}

/* Note : this is deprecated in favor of IWSCAN */
static int ks_wlan_get_aplist(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct sockaddr *address = (struct sockaddr *)extra;
	struct iw_quality qual[LOCAL_APLIST_MAX];
	int i;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	for (i = 0; i < priv->aplist.size; i++) {
		ether_addr_copy(address[i].sa_data, priv->aplist.ap[i].bssid);
		address[i].sa_family = ARPHRD_ETHER;
		qual[i].level = 256 - priv->aplist.ap[i].rssi;
		qual[i].qual = priv->aplist.ap[i].sq;
		qual[i].noise = 0;	/* invalid noise value */
		qual[i].updated = 7;
	}
	if (i) {
		dwrq->data.flags = 1;	/* Should be define'd */
		memcpy(extra + sizeof(struct sockaddr) * i,
		       &qual, sizeof(struct iw_quality) * i);
	}
	dwrq->data.length = i;

	return 0;
}

static int ks_wlan_set_scan(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_scan_req *req = NULL;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* specified SSID SCAN */
	if (wrqu->data.length == sizeof(struct iw_scan_req) &&
	    wrqu->data.flags & IW_SCAN_THIS_ESSID) {
		req = (struct iw_scan_req *)extra;
		priv->scan_ssid_len = req->essid_len;
		memcpy(priv->scan_ssid, req->essid, priv->scan_ssid_len);
	} else {
		priv->scan_ssid_len = 0;
	}

	priv->sme_i.sme_flag |= SME_AP_SCAN;
	hostif_sme_enqueue(priv, SME_BSS_SCAN_REQUEST);

	/* At this point, just return to the user. */

	return 0;
}

static char *ks_wlan_add_leader_event(const char *rsn_leader, char *end_buf,
				      char *current_ev, struct rsn_ie *rsn,
				      struct iw_event *iwe,
				      struct iw_request_info *info)
{
	char buffer[RSN_IE_BODY_MAX * 2 + 30];
	char *pbuf;
	int i;

	pbuf = &buffer[0];
	memset(iwe, 0, sizeof(*iwe));
	iwe->cmd = IWEVCUSTOM;
	memcpy(buffer, rsn_leader, sizeof(rsn_leader) - 1);
	iwe->u.data.length += sizeof(rsn_leader) - 1;
	pbuf += sizeof(rsn_leader) - 1;
	pbuf += sprintf(pbuf, "%02x", rsn->id);
	pbuf += sprintf(pbuf, "%02x", rsn->size);
	iwe->u.data.length += 4;

	for (i = 0; i < rsn->size; i++)
		pbuf += sprintf(pbuf, "%02x", rsn->body[i]);

	iwe->u.data.length += rsn->size * 2;

	return iwe_stream_add_point(info, current_ev, end_buf, iwe, &buffer[0]);
}

/*
 * Translate scan data returned from the card to a card independent
 * format that the Wireless Tools will understand - Jean II
 */
static inline char *ks_wlan_translate_scan(struct net_device *dev,
					   struct iw_request_info *info,
					   char *current_ev, char *end_buf,
					   struct local_ap *ap)
{
	/* struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv; */
	static const char rsn_leader[] = "rsn_ie=";
	static const char wpa_leader[] = "wpa_ie=";
	struct iw_event iwe;	/* Temporary buffer */
	u16 capabilities;
	char *current_val;	/* For rates */
	int i;

	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	ether_addr_copy(iwe.u.ap_addr.sa_data, ap->bssid);
	current_ev = iwe_stream_add_event(info, current_ev,
					  end_buf, &iwe, IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	iwe.u.data.length = ap->ssid.size;
	if (iwe.u.data.length > 32)
		iwe.u.data.length = 32;
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	current_ev = iwe_stream_add_point(info, current_ev,
					  end_buf, &iwe, ap->ssid.body);

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	capabilities = ap->capability;
	if (capabilities & (WLAN_CAPABILITY_ESS | WLAN_CAPABILITY_IBSS)) {
		iwe.u.mode = (capabilities & WLAN_CAPABILITY_ESS) ?
			      IW_MODE_INFRA : IW_MODE_ADHOC;
		current_ev = iwe_stream_add_event(info, current_ev,
						  end_buf, &iwe, IW_EV_UINT_LEN);
	}

	/* Add frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = ap->channel;
	iwe.u.freq.m = frequency_list[iwe.u.freq.m - 1] * 100000;
	iwe.u.freq.e = 1;
	current_ev = iwe_stream_add_event(info, current_ev,
					  end_buf, &iwe, IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.level = 256 - ap->rssi;
	iwe.u.qual.qual = ap->sq;
	iwe.u.qual.noise = 0;	/* invalid noise value */
	current_ev = iwe_stream_add_event(info, current_ev, end_buf,
					  &iwe, IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	iwe.u.data.flags = (capabilities & WLAN_CAPABILITY_PRIVACY) ?
			    (IW_ENCODE_ENABLED | IW_ENCODE_NOKEY) :
			     IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev = iwe_stream_add_point(info, current_ev, end_buf,
					  &iwe, ap->ssid.body);

	/*
	 * Rate : stuffing multiple values in a single event
	 * require a bit more of magic - Jean II
	 */
	current_val = current_ev + IW_EV_LCP_LEN;

	iwe.cmd = SIOCGIWRATE;

	/* These two flags are ignored... */
	iwe.u.bitrate.fixed = 0;
	iwe.u.bitrate.disabled = 0;

	/* Max 16 values */
	for (i = 0; i < 16; i++) {
		/* NULL terminated */
		if (i >= ap->rate_set.size)
			break;
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = ((ap->rate_set.body[i] & 0x7f) * 500000);
		/* Add new value to event */
		current_val = iwe_stream_add_value(info, current_ev,
						   current_val, end_buf, &iwe,
						   IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if ((current_val - current_ev) > IW_EV_LCP_LEN)
		current_ev = current_val;

	if (ap->rsn_ie.id == RSN_INFO_ELEM_ID && ap->rsn_ie.size != 0)
		current_ev = ks_wlan_add_leader_event(rsn_leader, end_buf,
						      current_ev, &ap->rsn_ie,
						      &iwe, info);

	if (ap->wpa_ie.id == WPA_INFO_ELEM_ID && ap->wpa_ie.size != 0)
		current_ev = ks_wlan_add_leader_event(wpa_leader, end_buf,
						      current_ev, &ap->wpa_ie,
						      &iwe, info);

	/*
	 * The other data in the scan result are not really
	 * interesting, so for now drop it - Jean II
	 */
	return current_ev;
}

static int ks_wlan_get_scan(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int i;
	char *current_ev = extra;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (priv->sme_i.sme_flag & SME_AP_SCAN)
		return -EAGAIN;

	if (priv->aplist.size == 0) {
		/* Client error, no scan results...
		 * The caller need to restart the scan.
		 */
		return -ENODATA;
	}

	/* Read and parse all entries */
	for (i = 0; i < priv->aplist.size; i++) {
		if ((extra + dwrq->data.length) - current_ev <= IW_EV_ADDR_LEN) {
			dwrq->data.length = 0;
			return -E2BIG;
		}
		/* Translate to WE format this entry */
		current_ev = ks_wlan_translate_scan(dev, info, current_ev,
						    extra + dwrq->data.length,
						    &priv->aplist.ap[i]);
	}
	/* Length of data */
	dwrq->data.length = (current_ev - extra);
	dwrq->data.flags = 0;

	return 0;
}

/* called after a bunch of SET operations */
static int ks_wlan_config_commit(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *zwrq,
				 char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (!priv->need_commit)
		return 0;

	ks_wlan_setup_parameter(priv, priv->need_commit);
	priv->need_commit = 0;
	return 0;
}

/* set association ie params */
static int ks_wlan_set_genie(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	return 0;
//      return -EOPNOTSUPP;
}

static int ks_wlan_set_auth_mode(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_param *param = &vwrq->param;
	int index = (param->flags & IW_AUTH_INDEX);
	int value = param->value;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	switch (index) {
	case IW_AUTH_WPA_VERSION:	/* 0 */
		switch (value) {
		case IW_AUTH_WPA_VERSION_DISABLED:
			priv->wpa.version = value;
			if (priv->wpa.rsn_enabled)
				priv->wpa.rsn_enabled = false;
			priv->need_commit |= SME_RSN;
			break;
		case IW_AUTH_WPA_VERSION_WPA:
		case IW_AUTH_WPA_VERSION_WPA2:
			priv->wpa.version = value;
			if (!(priv->wpa.rsn_enabled))
				priv->wpa.rsn_enabled = true;
			priv->need_commit |= SME_RSN;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IW_AUTH_CIPHER_PAIRWISE:	/* 1 */
		switch (value) {
		case IW_AUTH_CIPHER_NONE:
			if (priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x00;
				priv->need_commit |= SME_WEP_FLAG;
			}
			break;
		case IW_AUTH_CIPHER_WEP40:
		case IW_AUTH_CIPHER_TKIP:
		case IW_AUTH_CIPHER_CCMP:
		case IW_AUTH_CIPHER_WEP104:
			if (!priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x01;
				priv->need_commit |= SME_WEP_FLAG;
			}
			priv->wpa.pairwise_suite = value;
			priv->need_commit |= SME_RSN_UNICAST;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IW_AUTH_CIPHER_GROUP:	/* 2 */
		switch (value) {
		case IW_AUTH_CIPHER_NONE:
			if (priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x00;
				priv->need_commit |= SME_WEP_FLAG;
			}
			break;
		case IW_AUTH_CIPHER_WEP40:
		case IW_AUTH_CIPHER_TKIP:
		case IW_AUTH_CIPHER_CCMP:
		case IW_AUTH_CIPHER_WEP104:
			if (!priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x01;
				priv->need_commit |= SME_WEP_FLAG;
			}
			priv->wpa.group_suite = value;
			priv->need_commit |= SME_RSN_MULTICAST;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IW_AUTH_KEY_MGMT:	/* 3 */
		switch (value) {
		case IW_AUTH_KEY_MGMT_802_1X:
		case IW_AUTH_KEY_MGMT_PSK:
		case 0:	/* NONE or 802_1X_NO_WPA */
		case 4:	/* WPA_NONE */
			priv->wpa.key_mgmt_suite = value;
			priv->need_commit |= SME_RSN_AUTH;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case IW_AUTH_80211_AUTH_ALG:	/* 6 */
		switch (value) {
		case IW_AUTH_ALG_OPEN_SYSTEM:
			priv->wpa.auth_alg = value;
			priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;
			break;
		case IW_AUTH_ALG_SHARED_KEY:
			priv->wpa.auth_alg = value;
			priv->reg.authenticate_type = AUTH_TYPE_SHARED_KEY;
			break;
		case IW_AUTH_ALG_LEAP:
		default:
			return -EOPNOTSUPP;
		}
		priv->need_commit |= SME_MODE_SET;
		break;
	case IW_AUTH_WPA_ENABLED:	/* 7 */
		priv->wpa.wpa_enabled = value;
		break;
	case IW_AUTH_PRIVACY_INVOKED:	/* 10 */
		if ((value && !priv->reg.privacy_invoked) ||
		    (!value && priv->reg.privacy_invoked)) {
			priv->reg.privacy_invoked = value ? 0x01 : 0x00;
			priv->need_commit |= SME_WEP_FLAG;
		}
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:	/* 4 */
	case IW_AUTH_TKIP_COUNTERMEASURES:	/* 5 */
	case IW_AUTH_DROP_UNENCRYPTED:	/* 8 */
	case IW_AUTH_ROAMING_CONTROL:	/* 9 */
	default:
		break;
	}

	/* return -EINPROGRESS; */
	if (priv->need_commit) {
		ks_wlan_setup_parameter(priv, priv->need_commit);
		priv->need_commit = 0;
	}
	return 0;
}

static int ks_wlan_get_auth_mode(struct net_device *dev,
				 struct iw_request_info *info,
				 union iwreq_data *vwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_param *param = &vwrq->param;
	int index = (param->flags & IW_AUTH_INDEX);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/*  WPA (not used ?? wpa_supplicant) */
	switch (index) {
	case IW_AUTH_WPA_VERSION:
		param->value = priv->wpa.version;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		param->value = priv->wpa.pairwise_suite;
		break;
	case IW_AUTH_CIPHER_GROUP:
		param->value = priv->wpa.group_suite;
		break;
	case IW_AUTH_KEY_MGMT:
		param->value = priv->wpa.key_mgmt_suite;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		param->value = priv->wpa.auth_alg;
		break;
	case IW_AUTH_WPA_ENABLED:
		param->value = priv->wpa.rsn_enabled;
		break;
	case IW_AUTH_RX_UNENCRYPTED_EAPOL:	/* OK??? */
	case IW_AUTH_TKIP_COUNTERMEASURES:
	case IW_AUTH_DROP_UNENCRYPTED:
	default:
		/* return -EOPNOTSUPP; */
		break;
	}
	return 0;
}

/* set encoding token & mode (WPA)*/
static int ks_wlan_set_encode_ext(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_encode_ext *enc;
	int index = dwrq->encoding.flags & IW_ENCODE_INDEX;
	unsigned int commit = 0;
	struct wpa_key *key;

	enc = (struct iw_encode_ext *)extra;
	if (!enc)
		return -EINVAL;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (index < 1 || index > 4)
		return -EINVAL;
	index--;
	key = &priv->wpa.key[index];

	if (dwrq->encoding.flags & IW_ENCODE_DISABLED)
		key->key_len = 0;

	key->ext_flags = enc->ext_flags;
	if (enc->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
		priv->wpa.txkey = index;
		commit |= SME_WEP_INDEX;
	} else if (enc->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
		memcpy(&key->rx_seq[0], &enc->rx_seq[0], IW_ENCODE_SEQ_MAX_SIZE);
	}

	ether_addr_copy(&key->addr.sa_data[0], &enc->addr.sa_data[0]);

	switch (enc->alg) {
	case IW_ENCODE_ALG_NONE:
		if (priv->reg.privacy_invoked) {
			priv->reg.privacy_invoked = 0x00;
			commit |= SME_WEP_FLAG;
		}
		key->key_len = 0;

		break;
	case IW_ENCODE_ALG_WEP:
	case IW_ENCODE_ALG_CCMP:
		if (!priv->reg.privacy_invoked) {
			priv->reg.privacy_invoked = 0x01;
			commit |= SME_WEP_FLAG;
		}
		if (enc->key_len) {
			memcpy(&key->key_val[0], &enc->key[0], enc->key_len);
			key->key_len = enc->key_len;
			commit |= (SME_WEP_VAL1 << index);
		}
		break;
	case IW_ENCODE_ALG_TKIP:
		if (!priv->reg.privacy_invoked) {
			priv->reg.privacy_invoked = 0x01;
			commit |= SME_WEP_FLAG;
		}
		if (enc->key_len == 32) {
			memcpy(&key->key_val[0], &enc->key[0], enc->key_len - 16);
			key->key_len = enc->key_len - 16;
			if (priv->wpa.key_mgmt_suite == 4) {	/* WPA_NONE */
				memcpy(&key->tx_mic_key[0], &enc->key[16], 8);
				memcpy(&key->rx_mic_key[0], &enc->key[16], 8);
			} else {
				memcpy(&key->tx_mic_key[0], &enc->key[16], 8);
				memcpy(&key->rx_mic_key[0], &enc->key[24], 8);
			}
			commit |= (SME_WEP_VAL1 << index);
		}
		break;
	default:
		return -EINVAL;
	}
	key->alg = enc->alg;

	if (commit) {
		if (commit & SME_WEP_INDEX)
			hostif_sme_enqueue(priv, SME_SET_TXKEY);
		if (commit & SME_WEP_VAL_MASK)
			hostif_sme_enqueue(priv, SME_SET_KEY1 + index);
		if (commit & SME_WEP_FLAG)
			hostif_sme_enqueue(priv, SME_WEP_FLAG_REQUEST);
	}

	return 0;
}

/* get encoding token & mode (WPA)*/
static int ks_wlan_get_encode_ext(struct net_device *dev,
				  struct iw_request_info *info,
				  union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	/* WPA (not used ?? wpa_supplicant)
	 * struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	 * struct iw_encode_ext *enc;
	 * enc = (struct iw_encode_ext *)extra;
	 * int index = dwrq->flags & IW_ENCODE_INDEX;
	 * WPA (not used ?? wpa_supplicant)
	 */
	return 0;
}

static int ks_wlan_set_pmksa(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_pmksa *pmksa;
	int i;
	struct pmk *pmk;
	struct list_head *ptr;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (!extra)
		return -EINVAL;

	pmksa = (struct iw_pmksa *)extra;

	switch (pmksa->cmd) {
	case IW_PMKSA_ADD:
		if (list_empty(&priv->pmklist.head)) {
			for (i = 0; i < PMK_LIST_MAX; i++) {
				pmk = &priv->pmklist.pmk[i];
				if (is_zero_ether_addr(pmk->bssid))
					break;
			}
			ether_addr_copy(pmk->bssid, pmksa->bssid.sa_data);
			memcpy(pmk->pmkid, pmksa->pmkid, IW_PMKID_LEN);
			list_add(&pmk->list, &priv->pmklist.head);
			priv->pmklist.size++;
			break;
		}
		/* search cache data */
		list_for_each(ptr, &priv->pmklist.head) {
			pmk = list_entry(ptr, struct pmk, list);
			if (ether_addr_equal(pmksa->bssid.sa_data, pmk->bssid)) {
				memcpy(pmk->pmkid, pmksa->pmkid, IW_PMKID_LEN);
				list_move(&pmk->list, &priv->pmklist.head);
				break;
			}
		}
		/* not find address. */
		if (ptr != &priv->pmklist.head)
			break;
		/* new cache data */
		if (priv->pmklist.size < PMK_LIST_MAX) {
			for (i = 0; i < PMK_LIST_MAX; i++) {
				pmk = &priv->pmklist.pmk[i];
				if (is_zero_ether_addr(pmk->bssid))
					break;
			}
			ether_addr_copy(pmk->bssid, pmksa->bssid.sa_data);
			memcpy(pmk->pmkid, pmksa->pmkid, IW_PMKID_LEN);
			list_add(&pmk->list, &priv->pmklist.head);
			priv->pmklist.size++;
		} else { /* overwrite old cache data */
			pmk = list_entry(priv->pmklist.head.prev, struct pmk,
					 list);
			ether_addr_copy(pmk->bssid, pmksa->bssid.sa_data);
			memcpy(pmk->pmkid, pmksa->pmkid, IW_PMKID_LEN);
			list_move(&pmk->list, &priv->pmklist.head);
		}
		break;
	case IW_PMKSA_REMOVE:
		if (list_empty(&priv->pmklist.head))
			return -EINVAL;
		/* search cache data */
		list_for_each(ptr, &priv->pmklist.head) {
			pmk = list_entry(ptr, struct pmk, list);
			if (ether_addr_equal(pmksa->bssid.sa_data, pmk->bssid)) {
				eth_zero_addr(pmk->bssid);
				memset(pmk->pmkid, 0, IW_PMKID_LEN);
				list_del_init(&pmk->list);
				break;
			}
		}
		/* not find address. */
		if (ptr == &priv->pmklist.head)
			return 0;
		break;
	case IW_PMKSA_FLUSH:
		memset(&priv->pmklist, 0, sizeof(priv->pmklist));
		INIT_LIST_HEAD(&priv->pmklist.head);
		for (i = 0; i < PMK_LIST_MAX; i++)
			INIT_LIST_HEAD(&priv->pmklist.pmk[i].list);
		break;
	default:
		return -EINVAL;
	}

	hostif_sme_enqueue(priv, SME_SET_PMKSA);
	return 0;
}

static struct iw_statistics *ks_get_wireless_stats(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_statistics *wstats = &priv->wstats;

	if (!atomic_read(&update_phyinfo))
		return (priv->dev_state < DEVICE_STATE_READY) ? NULL : wstats;

	/*
	 * Packets discarded in the wireless adapter due to wireless
	 * specific problems
	 */
	wstats->discard.nwid = 0;	/* Rx invalid nwid      */
	wstats->discard.code = 0;	/* Rx invalid crypt     */
	wstats->discard.fragment = 0;	/* Rx invalid frag      */
	wstats->discard.retries = 0;	/* Tx excessive retries */
	wstats->discard.misc = 0;	/* Invalid misc         */
	wstats->miss.beacon = 0;	/* Missed beacon        */

	return wstats;
}

static int ks_wlan_set_stop_request(struct net_device *dev,
				    struct iw_request_info *info, __u32 *uwrq,
				    char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (!(*uwrq))
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_STOP_REQUEST);
	return 0;
}

#include <linux/ieee80211.h>
static int ks_wlan_set_mlme(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	__u32 mode = 1;

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (mlme->cmd != IW_MLME_DEAUTH &&
	    mlme->cmd != IW_MLME_DISASSOC)
		return -EOPNOTSUPP;

	if (mlme->cmd == IW_MLME_DEAUTH &&
	    mlme->reason_code == WLAN_REASON_MIC_FAILURE)
		return 0;

	return ks_wlan_set_stop_request(dev, NULL, &mode, NULL);
}

static int ks_wlan_get_firmware_version(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	strcpy(extra, priv->firmware_version);
	dwrq->length = priv->version_size + 1;
	return 0;
}

static int ks_wlan_set_preamble(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	if (*uwrq != LONG_PREAMBLE && *uwrq != SHORT_PREAMBLE)
		return -EINVAL;

	priv->reg.preamble = *uwrq;
	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_preamble(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	*uwrq = priv->reg.preamble;
	return 0;
}

static int ks_wlan_set_power_mgmt(struct net_device *dev,
				  struct iw_request_info *info, __u32 *uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (*uwrq != POWER_MGMT_ACTIVE &&
	    *uwrq != POWER_MGMT_SAVE1 &&
	    *uwrq != POWER_MGMT_SAVE2)
		return -EINVAL;

	if ((*uwrq == POWER_MGMT_SAVE1 || *uwrq == POWER_MGMT_SAVE2) &&
	    (priv->reg.operation_mode != MODE_INFRASTRUCTURE))
		return -EINVAL;

	priv->reg.power_mgmt = *uwrq;
	hostif_sme_enqueue(priv, SME_POW_MNGMT_REQUEST);

	return 0;
}

static int ks_wlan_get_power_mgmt(struct net_device *dev,
				  struct iw_request_info *info, __u32 *uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* for SLEEP MODE */
	*uwrq = priv->reg.power_mgmt;
	return 0;
}

static int ks_wlan_set_scan_type(struct net_device *dev,
				 struct iw_request_info *info, __u32 *uwrq,
				 char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */

	if (*uwrq != ACTIVE_SCAN && *uwrq != PASSIVE_SCAN)
		return -EINVAL;

	priv->reg.scan_type = *uwrq;
	return 0;
}

static int ks_wlan_get_scan_type(struct net_device *dev,
				 struct iw_request_info *info, __u32 *uwrq,
				 char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->reg.scan_type;
	return 0;
}

static int ks_wlan_set_beacon_lost(struct net_device *dev,
				   struct iw_request_info *info, __u32 *uwrq,
				   char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (*uwrq > BEACON_LOST_COUNT_MAX)
		return -EINVAL;

	priv->reg.beacon_lost_count = *uwrq;

	if (priv->reg.operation_mode == MODE_INFRASTRUCTURE) {
		priv->need_commit |= SME_MODE_SET;
		return -EINPROGRESS;	/* Call commit handler */
	}

	return 0;
}

static int ks_wlan_get_beacon_lost(struct net_device *dev,
				   struct iw_request_info *info, __u32 *uwrq,
				   char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->reg.beacon_lost_count;
	return 0;
}

static int ks_wlan_set_phy_type(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	if (*uwrq != D_11B_ONLY_MODE &&
	    *uwrq != D_11G_ONLY_MODE &&
	    *uwrq != D_11BG_COMPATIBLE_MODE)
		return -EINVAL;

	/* for SLEEP MODE */
	priv->reg.phy_type = *uwrq;
	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_phy_type(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->reg.phy_type;
	return 0;
}

static int ks_wlan_set_cts_mode(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (*uwrq != CTS_MODE_FALSE && *uwrq != CTS_MODE_TRUE)
		return -EINVAL;

	priv->reg.cts_mode = (*uwrq == CTS_MODE_FALSE) ? *uwrq :
			      (priv->reg.phy_type == D_11G_ONLY_MODE ||
			       priv->reg.phy_type == D_11BG_COMPATIBLE_MODE) ?
			       *uwrq : !*uwrq;

	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */
}

static int ks_wlan_get_cts_mode(struct net_device *dev,
				struct iw_request_info *info, __u32 *uwrq,
				char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->reg.cts_mode;
	return 0;
}

static int ks_wlan_set_sleep_mode(struct net_device *dev,
				  struct iw_request_info *info,
				  __u32 *uwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (*uwrq != SLP_SLEEP &&
	    *uwrq != SLP_ACTIVE) {
		netdev_err(dev, "SET_SLEEP_MODE %d error\n", *uwrq);
		return -EINVAL;
	}

	priv->sleep_mode = *uwrq;
	netdev_info(dev, "SET_SLEEP_MODE %d\n", priv->sleep_mode);

	if (*uwrq == SLP_SLEEP)
		hostif_sme_enqueue(priv, SME_STOP_REQUEST);

	hostif_sme_enqueue(priv, SME_SLEEP_REQUEST);

	return 0;
}

static int ks_wlan_get_sleep_mode(struct net_device *dev,
				  struct iw_request_info *info,
				  __u32 *uwrq, char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	*uwrq = priv->sleep_mode;

	return 0;
}

static int ks_wlan_set_wps_enable(struct net_device *dev,
				  struct iw_request_info *info, __u32 *uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (*uwrq != 0 && *uwrq != 1)
		return -EINVAL;

	priv->wps.wps_enabled = *uwrq;
	hostif_sme_enqueue(priv, SME_WPS_ENABLE_REQUEST);

	return 0;
}

static int ks_wlan_get_wps_enable(struct net_device *dev,
				  struct iw_request_info *info, __u32 *uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->wps.wps_enabled;
	netdev_info(dev, "return=%d\n", *uwrq);

	return 0;
}

static int ks_wlan_set_wps_probe_req(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *dwrq, char *extra)
{
	u8 *p = extra;
	unsigned char len;
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;

	/* length check */
	if (p[1] + 2 != dwrq->length || dwrq->length > 256)
		return -EINVAL;

	priv->wps.ielen = p[1] + 2 + 1;	/* IE header + IE + sizeof(len) */
	len = p[1] + 2;	/* IE header + IE */

	memcpy(priv->wps.ie, &len, sizeof(len));
	p = memcpy(priv->wps.ie + 1, p, len);

	netdev_dbg(dev, "%d(%#x): %02X %02X %02X %02X ... %02X %02X %02X\n",
		   priv->wps.ielen, priv->wps.ielen, p[0], p[1], p[2], p[3],
		   p[priv->wps.ielen - 3], p[priv->wps.ielen - 2],
		   p[priv->wps.ielen - 1]);

	hostif_sme_enqueue(priv, SME_WPS_PROBE_REQUEST);

	return 0;
}

static int ks_wlan_set_tx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 *uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (*uwrq > 0xFF)
		return -EINVAL;

	priv->gain.tx_gain = (u8)*uwrq;
	priv->gain.tx_mode = (priv->gain.tx_gain < 0xFF) ? 1 : 0;
	hostif_sme_enqueue(priv, SME_SET_GAIN);
	return 0;
}

static int ks_wlan_get_tx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 *uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->gain.tx_gain;
	hostif_sme_enqueue(priv, SME_GET_GAIN);
	return 0;
}

static int ks_wlan_set_rx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 *uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	if (*uwrq > 0xFF)
		return -EINVAL;

	priv->gain.rx_gain = (u8)*uwrq;
	priv->gain.rx_mode = (priv->gain.rx_gain < 0xFF) ? 1 : 0;
	hostif_sme_enqueue(priv, SME_SET_GAIN);
	return 0;
}

static int ks_wlan_get_rx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 *uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP)
		return -EPERM;
	/* for SLEEP MODE */
	*uwrq = priv->gain.rx_gain;
	hostif_sme_enqueue(priv, SME_GET_GAIN);
	return 0;
}

static int ks_wlan_get_eeprom_cksum(struct net_device *dev,
				    struct iw_request_info *info, __u32 *uwrq,
				    char *extra)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	*uwrq = priv->eeprom_checksum;
	return 0;
}

static void print_hif_event(struct net_device *dev, int event)
{
	switch (event) {
	case HIF_DATA_REQ:
		netdev_info(dev, "HIF_DATA_REQ\n");
		break;
	case HIF_DATA_IND:
		netdev_info(dev, "HIF_DATA_IND\n");
		break;
	case HIF_MIB_GET_REQ:
		netdev_info(dev, "HIF_MIB_GET_REQ\n");
		break;
	case HIF_MIB_GET_CONF:
		netdev_info(dev, "HIF_MIB_GET_CONF\n");
		break;
	case HIF_MIB_SET_REQ:
		netdev_info(dev, "HIF_MIB_SET_REQ\n");
		break;
	case HIF_MIB_SET_CONF:
		netdev_info(dev, "HIF_MIB_SET_CONF\n");
		break;
	case HIF_POWER_MGMT_REQ:
		netdev_info(dev, "HIF_POWER_MGMT_REQ\n");
		break;
	case HIF_POWER_MGMT_CONF:
		netdev_info(dev, "HIF_POWER_MGMT_CONF\n");
		break;
	case HIF_START_REQ:
		netdev_info(dev, "HIF_START_REQ\n");
		break;
	case HIF_START_CONF:
		netdev_info(dev, "HIF_START_CONF\n");
		break;
	case HIF_CONNECT_IND:
		netdev_info(dev, "HIF_CONNECT_IND\n");
		break;
	case HIF_STOP_REQ:
		netdev_info(dev, "HIF_STOP_REQ\n");
		break;
	case HIF_STOP_CONF:
		netdev_info(dev, "HIF_STOP_CONF\n");
		break;
	case HIF_PS_ADH_SET_REQ:
		netdev_info(dev, "HIF_PS_ADH_SET_REQ\n");
		break;
	case HIF_PS_ADH_SET_CONF:
		netdev_info(dev, "HIF_PS_ADH_SET_CONF\n");
		break;
	case HIF_INFRA_SET_REQ:
		netdev_info(dev, "HIF_INFRA_SET_REQ\n");
		break;
	case HIF_INFRA_SET_CONF:
		netdev_info(dev, "HIF_INFRA_SET_CONF\n");
		break;
	case HIF_ADH_SET_REQ:
		netdev_info(dev, "HIF_ADH_SET_REQ\n");
		break;
	case HIF_ADH_SET_CONF:
		netdev_info(dev, "HIF_ADH_SET_CONF\n");
		break;
	case HIF_AP_SET_REQ:
		netdev_info(dev, "HIF_AP_SET_REQ\n");
		break;
	case HIF_AP_SET_CONF:
		netdev_info(dev, "HIF_AP_SET_CONF\n");
		break;
	case HIF_ASSOC_INFO_IND:
		netdev_info(dev, "HIF_ASSOC_INFO_IND\n");
		break;
	case HIF_MIC_FAILURE_REQ:
		netdev_info(dev, "HIF_MIC_FAILURE_REQ\n");
		break;
	case HIF_MIC_FAILURE_CONF:
		netdev_info(dev, "HIF_MIC_FAILURE_CONF\n");
		break;
	case HIF_SCAN_REQ:
		netdev_info(dev, "HIF_SCAN_REQ\n");
		break;
	case HIF_SCAN_CONF:
		netdev_info(dev, "HIF_SCAN_CONF\n");
		break;
	case HIF_PHY_INFO_REQ:
		netdev_info(dev, "HIF_PHY_INFO_REQ\n");
		break;
	case HIF_PHY_INFO_CONF:
		netdev_info(dev, "HIF_PHY_INFO_CONF\n");
		break;
	case HIF_SLEEP_REQ:
		netdev_info(dev, "HIF_SLEEP_REQ\n");
		break;
	case HIF_SLEEP_CONF:
		netdev_info(dev, "HIF_SLEEP_CONF\n");
		break;
	case HIF_PHY_INFO_IND:
		netdev_info(dev, "HIF_PHY_INFO_IND\n");
		break;
	case HIF_SCAN_IND:
		netdev_info(dev, "HIF_SCAN_IND\n");
		break;
	case HIF_INFRA_SET2_REQ:
		netdev_info(dev, "HIF_INFRA_SET2_REQ\n");
		break;
	case HIF_INFRA_SET2_CONF:
		netdev_info(dev, "HIF_INFRA_SET2_CONF\n");
		break;
	case HIF_ADH_SET2_REQ:
		netdev_info(dev, "HIF_ADH_SET2_REQ\n");
		break;
	case HIF_ADH_SET2_CONF:
		netdev_info(dev, "HIF_ADH_SET2_CONF\n");
	}
}

/* get host command history */
static int ks_wlan_hostt(struct net_device *dev, struct iw_request_info *info,
			 __u32 *uwrq, char *extra)
{
	int i, event;
	struct ks_wlan_private *priv = netdev_priv(dev);

	for (i = 63; i >= 0; i--) {
		event =
		    priv->hostt.buff[(priv->hostt.qtail - 1 - i) %
				     SME_EVENT_BUFF_SIZE];
		print_hif_event(dev, event);
	}
	return 0;
}

/* Structures to export the Wireless Handlers */

static const struct iw_priv_args ks_wlan_private_args[] = {
/*{ cmd, set_args, get_args, name[16] } */
	{KS_WLAN_GET_FIRM_VERSION, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_CHAR | (128 + 1), "GetFirmwareVer"},
	{KS_WLAN_SET_WPS_ENABLE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetWPSEnable"},
	{KS_WLAN_GET_WPS_ENABLE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetW"},
	{KS_WLAN_SET_WPS_PROBE_REQ, IW_PRIV_TYPE_BYTE | 2047, IW_PRIV_TYPE_NONE,
	 "SetWPSProbeReq"},
	{KS_WLAN_SET_PREAMBLE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetPreamble"},
	{KS_WLAN_GET_PREAMBLE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetPreamble"},
	{KS_WLAN_SET_POWER_SAVE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetPowerSave"},
	{KS_WLAN_GET_POWER_SAVE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetPowerSave"},
	{KS_WLAN_SET_SCAN_TYPE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetScanType"},
	{KS_WLAN_GET_SCAN_TYPE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetScanType"},
	{KS_WLAN_SET_RX_GAIN, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetRxGain"},
	{KS_WLAN_GET_RX_GAIN, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetRxGain"},
	{KS_WLAN_HOSTT, IW_PRIV_TYPE_NONE, IW_PRIV_TYPE_CHAR | (128 + 1),
	 "hostt"},
	{KS_WLAN_SET_BEACON_LOST, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetBeaconLost"},
	{KS_WLAN_GET_BEACON_LOST, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetBeaconLost"},
	{KS_WLAN_SET_SLEEP_MODE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetSleepMode"},
	{KS_WLAN_GET_SLEEP_MODE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetSleepMode"},
	{KS_WLAN_SET_TX_GAIN, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetTxGain"},
	{KS_WLAN_GET_TX_GAIN, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetTxGain"},
	{KS_WLAN_SET_PHY_TYPE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetPhyType"},
	{KS_WLAN_GET_PHY_TYPE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetPhyType"},
	{KS_WLAN_SET_CTS_MODE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetCtsMode"},
	{KS_WLAN_GET_CTS_MODE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetCtsMode"},
	{KS_WLAN_GET_EEPROM_CKSUM, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetChecksum"},
};

static const iw_handler ks_wlan_handler[] = {
	IW_HANDLER(SIOCSIWCOMMIT, ks_wlan_config_commit),
	IW_HANDLER(SIOCGIWNAME, ks_wlan_get_name),
	IW_HANDLER(SIOCSIWFREQ, ks_wlan_set_freq),
	IW_HANDLER(SIOCGIWFREQ, ks_wlan_get_freq),
	IW_HANDLER(SIOCSIWMODE, ks_wlan_set_mode),
	IW_HANDLER(SIOCGIWMODE, ks_wlan_get_mode),
	IW_HANDLER(SIOCGIWRANGE, ks_wlan_get_range),
	IW_HANDLER(SIOCGIWSTATS, ks_wlan_get_iwstats),
	IW_HANDLER(SIOCSIWAP, ks_wlan_set_wap),
	IW_HANDLER(SIOCGIWAP, ks_wlan_get_wap),
	IW_HANDLER(SIOCSIWMLME, ks_wlan_set_mlme),
	IW_HANDLER(SIOCGIWAPLIST, ks_wlan_get_aplist),
	IW_HANDLER(SIOCSIWSCAN, ks_wlan_set_scan),
	IW_HANDLER(SIOCGIWSCAN, ks_wlan_get_scan),
	IW_HANDLER(SIOCSIWESSID, ks_wlan_set_essid),
	IW_HANDLER(SIOCGIWESSID, ks_wlan_get_essid),
	IW_HANDLER(SIOCSIWNICKN, ks_wlan_set_nick),
	IW_HANDLER(SIOCGIWNICKN, ks_wlan_get_nick),
	IW_HANDLER(SIOCSIWRATE, ks_wlan_set_rate),
	IW_HANDLER(SIOCGIWRATE, ks_wlan_get_rate),
	IW_HANDLER(SIOCSIWRTS, ks_wlan_set_rts),
	IW_HANDLER(SIOCGIWRTS, ks_wlan_get_rts),
	IW_HANDLER(SIOCSIWFRAG, ks_wlan_set_frag),
	IW_HANDLER(SIOCGIWFRAG, ks_wlan_get_frag),
	IW_HANDLER(SIOCSIWENCODE, ks_wlan_set_encode),
	IW_HANDLER(SIOCGIWENCODE, ks_wlan_get_encode),
	IW_HANDLER(SIOCSIWPOWER, ks_wlan_set_power),
	IW_HANDLER(SIOCGIWPOWER, ks_wlan_get_power),
	IW_HANDLER(SIOCSIWGENIE, ks_wlan_set_genie),
	IW_HANDLER(SIOCSIWAUTH, ks_wlan_set_auth_mode),
	IW_HANDLER(SIOCGIWAUTH, ks_wlan_get_auth_mode),
	IW_HANDLER(SIOCSIWENCODEEXT, ks_wlan_set_encode_ext),
	IW_HANDLER(SIOCGIWENCODEEXT, ks_wlan_get_encode_ext),
	IW_HANDLER(SIOCSIWPMKSA, ks_wlan_set_pmksa),
};

/* private_handler */
static const iw_handler ks_wlan_private_handler[] = {
	(iw_handler)NULL,			/* 0 */
	(iw_handler)NULL,			/* 1, KS_WLAN_GET_DRIVER_VERSION */
	(iw_handler)NULL,			/* 2 */
	(iw_handler)ks_wlan_get_firmware_version,/* 3 KS_WLAN_GET_FIRM_VERSION */
	(iw_handler)ks_wlan_set_wps_enable,	/* 4 KS_WLAN_SET_WPS_ENABLE */
	(iw_handler)ks_wlan_get_wps_enable,	/* 5 KS_WLAN_GET_WPS_ENABLE */
	(iw_handler)ks_wlan_set_wps_probe_req,	/* 6 KS_WLAN_SET_WPS_PROBE_REQ */
	(iw_handler)ks_wlan_get_eeprom_cksum,	/* 7 KS_WLAN_GET_CONNECT */
	(iw_handler)ks_wlan_set_preamble,	/* 8 KS_WLAN_SET_PREAMBLE */
	(iw_handler)ks_wlan_get_preamble,	/* 9 KS_WLAN_GET_PREAMBLE */
	(iw_handler)ks_wlan_set_power_mgmt,	/* 10 KS_WLAN_SET_POWER_SAVE */
	(iw_handler)ks_wlan_get_power_mgmt,	/* 11 KS_WLAN_GET_POWER_SAVE */
	(iw_handler)ks_wlan_set_scan_type,	/* 12 KS_WLAN_SET_SCAN_TYPE */
	(iw_handler)ks_wlan_get_scan_type,	/* 13 KS_WLAN_GET_SCAN_TYPE */
	(iw_handler)ks_wlan_set_rx_gain,	/* 14 KS_WLAN_SET_RX_GAIN */
	(iw_handler)ks_wlan_get_rx_gain,	/* 15 KS_WLAN_GET_RX_GAIN */
	(iw_handler)ks_wlan_hostt,		/* 16 KS_WLAN_HOSTT */
	(iw_handler)NULL,			/* 17 */
	(iw_handler)ks_wlan_set_beacon_lost,	/* 18 KS_WLAN_SET_BECAN_LOST */
	(iw_handler)ks_wlan_get_beacon_lost,	/* 19 KS_WLAN_GET_BECAN_LOST */
	(iw_handler)ks_wlan_set_tx_gain,	/* 20 KS_WLAN_SET_TX_GAIN */
	(iw_handler)ks_wlan_get_tx_gain,	/* 21 KS_WLAN_GET_TX_GAIN */
	(iw_handler)ks_wlan_set_phy_type,	/* 22 KS_WLAN_SET_PHY_TYPE */
	(iw_handler)ks_wlan_get_phy_type,	/* 23 KS_WLAN_GET_PHY_TYPE */
	(iw_handler)ks_wlan_set_cts_mode,	/* 24 KS_WLAN_SET_CTS_MODE */
	(iw_handler)ks_wlan_get_cts_mode,	/* 25 KS_WLAN_GET_CTS_MODE */
	(iw_handler)NULL,			/* 26 */
	(iw_handler)NULL,			/* 27 */
	(iw_handler)ks_wlan_set_sleep_mode,	/* 28 KS_WLAN_SET_SLEEP_MODE */
	(iw_handler)ks_wlan_get_sleep_mode,	/* 29 KS_WLAN_GET_SLEEP_MODE */
	(iw_handler)NULL,			/* 30 */
	(iw_handler)NULL,			/* 31 */
};

static const struct iw_handler_def ks_wlan_handler_def = {
	.num_standard = ARRAY_SIZE(ks_wlan_handler),
	.num_private = ARRAY_SIZE(ks_wlan_private_handler),
	.num_private_args = ARRAY_SIZE(ks_wlan_private_args),
	.standard = ks_wlan_handler,
	.private = ks_wlan_private_handler,
	.private_args = ks_wlan_private_args,
	.get_wireless_stats = ks_get_wireless_stats,
};

static int ks_wlan_netdev_ioctl(struct net_device *dev, struct ifreq *rq,
				int cmd)
{
	int ret;
	struct iwreq *wrq = (struct iwreq *)rq;

	switch (cmd) {
	case SIOCIWFIRSTPRIV + 20:	/* KS_WLAN_SET_STOP_REQ */
		ret = ks_wlan_set_stop_request(dev, NULL, &wrq->u.mode, NULL);
		break;
		// All other calls are currently unsupported
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

static
struct net_device_stats *ks_wlan_get_stats(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->dev_state < DEVICE_STATE_READY)
		return NULL;	/* not finished initialize */

	return &priv->nstats;
}

static
int ks_wlan_set_mac_address(struct net_device *dev, void *addr)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	struct sockaddr *mac_addr = (struct sockaddr *)addr;

	if (netif_running(dev))
		return -EBUSY;
	memcpy(dev->dev_addr, mac_addr->sa_data, dev->addr_len);
	ether_addr_copy(priv->eth_addr, mac_addr->sa_data);

	priv->mac_address_valid = false;
	hostif_sme_enqueue(priv, SME_MACADDRESS_SET_REQUEST);
	netdev_info(dev, "ks_wlan:  MAC ADDRESS = %pM\n", priv->eth_addr);
	return 0;
}

static
void ks_wlan_tx_timeout(struct net_device *dev, unsigned int txqueue)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	netdev_dbg(dev, "head(%d) tail(%d)!!\n", priv->tx_dev.qhead,
		   priv->tx_dev.qtail);
	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);
	priv->nstats.tx_errors++;
	netif_wake_queue(dev);
}

static
int ks_wlan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int ret;

	netdev_dbg(dev, "in_interrupt()=%ld\n", in_interrupt());

	if (!skb) {
		netdev_err(dev, "ks_wlan:  skb == NULL!!!\n");
		return 0;
	}
	if (priv->dev_state < DEVICE_STATE_READY) {
		dev_kfree_skb(skb);
		return 0;	/* not finished initialize */
	}

	if (netif_running(dev))
		netif_stop_queue(dev);

	ret = hostif_data_request(priv, skb);
	netif_trans_update(dev);

	if (ret)
		netdev_err(dev, "hostif_data_request error: =%d\n", ret);

	return 0;
}

void send_packet_complete(struct ks_wlan_private *priv, struct sk_buff *skb)
{
	priv->nstats.tx_packets++;

	if (netif_queue_stopped(priv->net_dev))
		netif_wake_queue(priv->net_dev);

	if (skb) {
		priv->nstats.tx_bytes += skb->len;
		dev_kfree_skb(skb);
	}
}

/*
 * Set or clear the multicast filter for this adaptor.
 * This routine is not state sensitive and need not be SMP locked.
 */
static
void ks_wlan_set_rx_mode(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->dev_state < DEVICE_STATE_READY)
		return;	/* not finished initialize */
	hostif_sme_enqueue(priv, SME_MULTICAST_REQUEST);
}

static
int ks_wlan_open(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	priv->cur_rx = 0;

	if (!priv->mac_address_valid) {
		netdev_err(dev, "ks_wlan : %s Not READY !!\n", dev->name);
		return -EBUSY;
	}
	netif_start_queue(dev);

	return 0;
}

static
int ks_wlan_close(struct net_device *dev)
{
	netif_stop_queue(dev);

	return 0;
}

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (3 * HZ)
static const unsigned char dummy_addr[] = {
	0x00, 0x0b, 0xe3, 0x00, 0x00, 0x00
};

static const struct net_device_ops ks_wlan_netdev_ops = {
	.ndo_start_xmit = ks_wlan_start_xmit,
	.ndo_open = ks_wlan_open,
	.ndo_stop = ks_wlan_close,
	.ndo_do_ioctl = ks_wlan_netdev_ioctl,
	.ndo_set_mac_address = ks_wlan_set_mac_address,
	.ndo_get_stats = ks_wlan_get_stats,
	.ndo_tx_timeout = ks_wlan_tx_timeout,
	.ndo_set_rx_mode = ks_wlan_set_rx_mode,
};

int ks_wlan_net_start(struct net_device *dev)
{
	struct ks_wlan_private *priv;
	/* int rc; */

	priv = netdev_priv(dev);
	priv->mac_address_valid = false;
	priv->is_device_open = true;
	priv->need_commit = 0;
	/* phy information update timer */
	atomic_set(&update_phyinfo, 0);
	timer_setup(&update_phyinfo_timer, ks_wlan_update_phyinfo_timeout, 0);

	/* dummy address set */
	ether_addr_copy(priv->eth_addr, dummy_addr);
	ether_addr_copy(dev->dev_addr, priv->eth_addr);

	/* The ks_wlan-specific entries in the device structure. */
	dev->netdev_ops = &ks_wlan_netdev_ops;
	dev->wireless_handlers = &ks_wlan_handler_def;
	dev->watchdog_timeo = TX_TIMEOUT;

	netif_carrier_off(dev);

	return 0;
}

int ks_wlan_net_stop(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	priv->is_device_open = false;
	del_timer_sync(&update_phyinfo_timer);

	if (netif_running(dev))
		netif_stop_queue(dev);

	return 0;
}

/**
 * is_connect_status() - return true if status is 'connected'
 * @status: high bit is used as FORCE_DISCONNECT, low bits used for
 *	connect status.
 */
bool is_connect_status(u32 status)
{
	return (status & CONNECT_STATUS_MASK) == CONNECT_STATUS;
}

/**
 * is_disconnect_status() - return true if status is 'disconnected'
 * @status: high bit is used as FORCE_DISCONNECT, low bits used for
 *	disconnect status.
 */
bool is_disconnect_status(u32 status)
{
	return (status & CONNECT_STATUS_MASK) == DISCONNECT_STATUS;
}
