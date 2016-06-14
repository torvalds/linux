/*
 *   Driver for KeyStream 11b/g wireless LAN
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License version 2 as
 *   published by the Free Software Foundation.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/delay.h>
#include <linux/completion.h>
#include <linux/mii.h>
#include <linux/pci.h>
#include <linux/ctype.h>
#include <linux/timer.h>
#include <asm/atomic.h>
#include <linux/io.h>
#include <asm/uaccess.h>

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
static const long frequency_list[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
	2447, 2452, 2457, 2462, 2467, 2472, 2484
};

/* A few details needed for WEP (Wireless Equivalent Privacy) */
#define MAX_KEY_SIZE 13	/* 128 (?) bits */
#define MIN_KEY_SIZE  5	/* 40 bits RC4 - WEP */
typedef struct wep_key_t {
	u16 len;
	u8 key[16];	/* 40-bit and 104-bit keys */
} wep_key_t;

/* Backward compatibility */
#ifndef IW_ENCODE_NOKEY
#define IW_ENCODE_NOKEY 0x0800	/* Key is write only, so not present */
#define IW_ENCODE_MODE  (IW_ENCODE_DISABLED | IW_ENCODE_RESTRICTED | IW_ENCODE_OPEN)
#endif /* IW_ENCODE_NOKEY */

/* List of Wireless Handlers (new API) */
static const struct iw_handler_def ks_wlan_handler_def;

#define KSC_OPNOTSUPP	/* Operation Not Support */

/*
 *	function prototypes
 */
extern int ks_wlan_hw_tx(struct ks_wlan_private *priv, void *p,
			 unsigned long size,
			 void (*complete_handler) (void *arg1, void *arg2),
			 void *arg1, void *arg2);
static int ks_wlan_open(struct net_device *dev);
static void ks_wlan_tx_timeout(struct net_device *dev);
static int ks_wlan_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int ks_wlan_close(struct net_device *dev);
static void ks_wlan_set_multicast_list(struct net_device *dev);
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

	DPRINTK(4, "in_interrupt = %ld\n", in_interrupt());

	if (priv->dev_state < DEVICE_STATE_READY) {
		return -1;	/* not finished initialize */
	}
	if (atomic_read(&update_phyinfo))
		return 1;

	/* The status */
	wstats->status = priv->reg.operation_mode;	/* Operation mode */

	/* Signal quality and co. But where is the noise level ??? */
	hostif_sme_enqueue(priv, SME_PHY_INFO_REQUEST);

	/* interruptible_sleep_on_timeout(&priv->confirm_wait, HZ/2); */
	if (!wait_for_completion_interruptible_timeout
	    (&priv->confirm_wait, HZ / 2)) {
		DPRINTK(1, "wait time out!!\n");
	}

	atomic_inc(&update_phyinfo);
	update_phyinfo_timer.expires = jiffies + HZ;	/* 1sec */
	add_timer(&update_phyinfo_timer);

	return 0;
}

static
void ks_wlan_update_phyinfo_timeout(unsigned long ptr)
{
	DPRINTK(4, "in_interrupt = %ld\n", in_interrupt());
	atomic_set(&update_phyinfo, 0);
}

int ks_wlan_setup_parameter(struct ks_wlan_private *priv,
			    unsigned int commit_flag)
{
	DPRINTK(2, "\n");

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

/*------------------------------------------------------------------*/
/* Wireless Handler : get protocol name */
static int ks_wlan_get_name(struct net_device *dev,
			    struct iw_request_info *info, char *cwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (priv->dev_state < DEVICE_STATE_READY) {
		strcpy(cwrq, "NOT READY!");
	} else if (priv->reg.phy_type == D_11B_ONLY_MODE) {
		strcpy(cwrq, "IEEE 802.11b");
	} else if (priv->reg.phy_type == D_11G_ONLY_MODE) {
		strcpy(cwrq, "IEEE 802.11g");
	} else {
		strcpy(cwrq, "IEEE 802.11b/g");
	}

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set frequency */
static int ks_wlan_set_freq(struct net_device *dev,
			    struct iw_request_info *info, struct iw_freq *fwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int rc = -EINPROGRESS;	/* Call commit handler */

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* If setting by frequency, convert to a channel */
	if ((fwrq->e == 1) &&
	    (fwrq->m >= (int)2.412e8) && (fwrq->m <= (int)2.487e8)) {
		int f = fwrq->m / 100000;
		int c = 0;
		while ((c < 14) && (f != frequency_list[c]))
			c++;
		/* Hack to fall through... */
		fwrq->e = 0;
		fwrq->m = c + 1;
	}
	/* Setting by channel number */
	if ((fwrq->m > 1000) || (fwrq->e > 0))
		rc = -EOPNOTSUPP;
	else {
		int channel = fwrq->m;
		/* We should do a better check than that,
		 * based on the card capability !!! */
		if ((channel < 1) || (channel > 14)) {
			printk(KERN_DEBUG
			       "%s: New channel value of %d is invalid!\n",
			       dev->name, fwrq->m);
			rc = -EINVAL;
		} else {
			/* Yes ! We can set it !!! */
			priv->reg.channel = (u8) (channel);
			priv->need_commit |= SME_MODE_SET;
		}
	}

	return rc;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get frequency */
static int ks_wlan_get_freq(struct net_device *dev,
			    struct iw_request_info *info, struct iw_freq *fwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int f;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if ((priv->connect_status & CONNECT_STATUS_MASK) == CONNECT_STATUS) {
		f = (int)priv->current_ap.channel;
	} else
		f = (int)priv->reg.channel;
	fwrq->m = frequency_list[f - 1] * 100000;
	fwrq->e = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set ESSID */
static int ks_wlan_set_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	size_t len;

	DPRINTK(2, " %d\n", dwrq->flags);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* Check if we asked for `any' */
	if (dwrq->flags == 0) {
		/* Just send an empty SSID list */
		memset(priv->reg.ssid.body, 0, sizeof(priv->reg.ssid.body));
		priv->reg.ssid.size = 0;
	} else {
#if 1
		len = dwrq->length;
		/* iwconfig uses nul termination in SSID.. */
		if (len > 0 && extra[len - 1] == '\0')
			len--;

		/* Check the size of the string */
		if (len > IW_ESSID_MAX_SIZE) {
			return -EINVAL;
		}
#else
		/* Check the size of the string */
		if (dwrq->length > IW_ESSID_MAX_SIZE + 1) {
			return -E2BIG;
		}
#endif

		/* Set the SSID */
		memset(priv->reg.ssid.body, 0, sizeof(priv->reg.ssid.body));

#if 1
		memcpy(priv->reg.ssid.body, extra, len);
		priv->reg.ssid.size = len;
#else
		memcpy(priv->reg.ssid.body, extra, dwrq->length);
		priv->reg.ssid.size = dwrq->length;
#endif
	}
	/* Write it to the card */
	priv->need_commit |= SME_MODE_SET;

//      return  -EINPROGRESS;   /* Call commit handler */
	ks_wlan_setup_parameter(priv, priv->need_commit);
	priv->need_commit = 0;
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get ESSID */
static int ks_wlan_get_essid(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* Note : if dwrq->flags != 0, we should
	 * get the relevant SSID from the SSID list... */
	if (priv->reg.ssid.size) {
		/* Get the current SSID */
		memcpy(extra, priv->reg.ssid.body, priv->reg.ssid.size);
#if 0
		extra[priv->reg.ssid.size] = '\0';
#endif
		/* If none, we may want to get the one that was set */

		/* Push it out ! */
#if 1
		dwrq->length = priv->reg.ssid.size;
#else
		dwrq->length = priv->reg.ssid.size + 1;
#endif
		dwrq->flags = 1;	/* active */
	} else {
#if 1
		dwrq->length = 0;
#else
		extra[0] = '\0';
		dwrq->length = 1;
#endif
		dwrq->flags = 0;	/* ANY */
	}

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set AP address */
static int ks_wlan_set_wap(struct net_device *dev, struct iw_request_info *info,
			   struct sockaddr *ap_addr, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (priv->reg.operation_mode == MODE_ADHOC ||
	    priv->reg.operation_mode == MODE_INFRASTRUCTURE) {
		memcpy(priv->reg.bssid, (u8 *) & ap_addr->sa_data, ETH_ALEN);

		if (is_valid_ether_addr((u8 *) priv->reg.bssid)) {
			priv->need_commit |= SME_MODE_SET;
		}
	} else {
		memset(priv->reg.bssid, 0x0, ETH_ALEN);
		return -EOPNOTSUPP;
	}

	DPRINTK(2, "bssid = %02x:%02x:%02x:%02x:%02x:%02x\n",
		priv->reg.bssid[0], priv->reg.bssid[1], priv->reg.bssid[2],
		priv->reg.bssid[3], priv->reg.bssid[4], priv->reg.bssid[5]);

	/* Write it to the card */
	if (priv->need_commit) {
		priv->need_commit |= SME_MODE_SET;
		return -EINPROGRESS;	/* Call commit handler */
	}
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get AP address */
static int ks_wlan_get_wap(struct net_device *dev, struct iw_request_info *info,
			   struct sockaddr *awrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if ((priv->connect_status & CONNECT_STATUS_MASK) == CONNECT_STATUS) {
		memcpy(awrq->sa_data, &(priv->current_ap.bssid[0]), ETH_ALEN);
	} else {
		memset(awrq->sa_data, 0, ETH_ALEN);
	}

	awrq->sa_family = ARPHRD_ETHER;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Nickname */
static int ks_wlan_set_nick(struct net_device *dev,
			    struct iw_request_info *info, struct iw_point *dwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* Check the size of the string */
	if (dwrq->length > 16 + 1) {
		return -E2BIG;
	}
	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, extra, dwrq->length);

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Nickname */
static int ks_wlan_get_nick(struct net_device *dev,
			    struct iw_request_info *info, struct iw_point *dwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	strncpy(extra, priv->nick, 16);
	extra[16] = '\0';
	dwrq->length = strlen(extra) + 1;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Bit-Rate */
static int ks_wlan_set_rate(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int i = 0;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (priv->reg.phy_type == D_11B_ONLY_MODE) {
		if (vwrq->fixed == 1) {
			switch (vwrq->value) {
			case 11000000:
			case 5500000:
				priv->reg.rate_set.body[0] =
				    (uint8_t) (vwrq->value / 500000);
				break;
			case 2000000:
			case 1000000:
				priv->reg.rate_set.body[0] =
				    ((uint8_t) (vwrq->value / 500000)) |
				    BASIC_RATE;
				break;
			default:
				return -EINVAL;
			}
			priv->reg.tx_rate = TX_RATE_FIXED;
			priv->reg.rate_set.size = 1;
		} else {	/* vwrq->fixed == 0 */
			if (vwrq->value > 0) {
				switch (vwrq->value) {
				case 11000000:
					priv->reg.rate_set.body[3] =
					    TX_RATE_11M;
					i++;
				case 5500000:
					priv->reg.rate_set.body[2] = TX_RATE_5M;
					i++;
				case 2000000:
					priv->reg.rate_set.body[1] =
					    TX_RATE_2M | BASIC_RATE;
					i++;
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
		if (vwrq->fixed == 1) {
			switch (vwrq->value) {
			case 54000000:
			case 48000000:
			case 36000000:
			case 18000000:
			case 9000000:
				priv->reg.rate_set.body[0] =
				    (uint8_t) (vwrq->value / 500000);
				break;
			case 24000000:
			case 12000000:
			case 11000000:
			case 6000000:
			case 5500000:
			case 2000000:
			case 1000000:
				priv->reg.rate_set.body[0] =
				    ((uint8_t) (vwrq->value / 500000)) |
				    BASIC_RATE;
				break;
			default:
				return -EINVAL;
			}
			priv->reg.tx_rate = TX_RATE_FIXED;
			priv->reg.rate_set.size = 1;
		} else {	/* vwrq->fixed == 0 */
			if (vwrq->value > 0) {
				switch (vwrq->value) {
				case 54000000:
					priv->reg.rate_set.body[11] =
					    TX_RATE_54M;
					i++;
				case 48000000:
					priv->reg.rate_set.body[10] =
					    TX_RATE_48M;
					i++;
				case 36000000:
					priv->reg.rate_set.body[9] =
					    TX_RATE_36M;
					i++;
				case 24000000:
				case 18000000:
				case 12000000:
				case 11000000:
				case 9000000:
				case 6000000:
					if (vwrq->value == 24000000) {
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
					} else if (vwrq->value == 18000000) {
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
					} else if (vwrq->value == 12000000) {
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
					} else if (vwrq->value == 11000000) {
						priv->reg.rate_set.body[5] =
						    TX_RATE_9M;
						i++;
						priv->reg.rate_set.body[4] =
						    TX_RATE_6M | BASIC_RATE;
						i++;
						priv->reg.rate_set.body[3] =
						    TX_RATE_11M | BASIC_RATE;
						i++;
					} else if (vwrq->value == 9000000) {
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
				case 5500000:
					priv->reg.rate_set.body[2] =
					    TX_RATE_5M | BASIC_RATE;
					i++;
				case 2000000:
					priv->reg.rate_set.body[1] =
					    TX_RATE_2M | BASIC_RATE;
					i++;
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

/*------------------------------------------------------------------*/
/* Wireless Handler : get Bit-Rate */
static int ks_wlan_get_rate(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "in_interrupt = %ld update_phyinfo = %d\n",
		in_interrupt(), atomic_read(&update_phyinfo));

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (!atomic_read(&update_phyinfo)) {
		ks_wlan_update_phy_information(priv);
	}
	vwrq->value = ((priv->current_rate) & RATE_MASK) * 500000;
	if (priv->reg.tx_rate == TX_RATE_FIXED)
		vwrq->fixed = 1;
	else
		vwrq->fixed = 0;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set RTS threshold */
static int ks_wlan_set_rts(struct net_device *dev, struct iw_request_info *info,
			   struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int rthr = vwrq->value;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (vwrq->disabled)
		rthr = 2347;
	if ((rthr < 0) || (rthr > 2347)) {
		return -EINVAL;
	}
	priv->reg.rts = rthr;
	priv->need_commit |= SME_RTS;

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get RTS threshold */
static int ks_wlan_get_rts(struct net_device *dev, struct iw_request_info *info,
			   struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	vwrq->value = priv->reg.rts;
	vwrq->disabled = (vwrq->value >= 2347);
	vwrq->fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Fragmentation threshold */
static int ks_wlan_set_frag(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int fthr = vwrq->value;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (vwrq->disabled)
		fthr = 2346;
	if ((fthr < 256) || (fthr > 2346)) {
		return -EINVAL;
	}
	fthr &= ~0x1;	/* Get an even value - is it really needed ??? */
	priv->reg.fragment = fthr;
	priv->need_commit |= SME_FRAG;

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Fragmentation threshold */
static int ks_wlan_get_frag(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	vwrq->value = priv->reg.fragment;
	vwrq->disabled = (vwrq->value >= 2346);
	vwrq->fixed = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Mode of Operation */
static int ks_wlan_set_mode(struct net_device *dev,
			    struct iw_request_info *info, __u32 * uwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "mode=%d\n", *uwrq);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	switch (*uwrq) {
	case IW_MODE_ADHOC:
		priv->reg.operation_mode = MODE_ADHOC;
		priv->need_commit |= SME_MODE_SET;
		break;
	case IW_MODE_INFRA:
		priv->reg.operation_mode = MODE_INFRASTRUCTURE;
		priv->need_commit |= SME_MODE_SET;
		break;
	case IW_MODE_AUTO:
	case IW_MODE_MASTER:
	case IW_MODE_REPEAT:
	case IW_MODE_SECOND:
	case IW_MODE_MONITOR:
	default:
		return -EINVAL;
	}

	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Mode of Operation */
static int ks_wlan_get_mode(struct net_device *dev,
			    struct iw_request_info *info, __u32 * uwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* If not managed, assume it's ad-hoc */
	switch (priv->reg.operation_mode) {
	case MODE_INFRASTRUCTURE:
		*uwrq = IW_MODE_INFRA;
		break;
	case MODE_ADHOC:
		*uwrq = IW_MODE_ADHOC;
		break;
	default:
		*uwrq = IW_MODE_ADHOC;
	}

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Encryption Key */
static int ks_wlan_set_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	wep_key_t key;
	int index = (dwrq->flags & IW_ENCODE_INDEX);
	int current_index = priv->reg.wep_index;
	int i;

	DPRINTK(2, "flags=%04X\n", dwrq->flags);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* index check */
	if ((index < 0) || (index > 4))
		return -EINVAL;
	else if (index == 0)
		index = current_index;
	else
		index--;

	/* Is WEP supported ? */
	/* Basic checking: do we have a key to set ? */
	if (dwrq->length > 0) {
		if (dwrq->length > MAX_KEY_SIZE) {	/* Check the size of the key */
			return -EINVAL;
		}
		if (dwrq->length > MIN_KEY_SIZE) {	/* Set the length */
			key.len = MAX_KEY_SIZE;
			priv->reg.privacy_invoked = 0x01;
			priv->need_commit |= SME_WEP_FLAG;
			wep_on_off = WEP_ON_128BIT;
		} else {
			if (dwrq->length > 0) {
				key.len = MIN_KEY_SIZE;
				priv->reg.privacy_invoked = 0x01;
				priv->need_commit |= SME_WEP_FLAG;
				wep_on_off = WEP_ON_64BIT;
			} else {	/* Disable the key */
				key.len = 0;
			}
		}
		/* Check if the key is not marked as invalid */
		if (!(dwrq->flags & IW_ENCODE_NOKEY)) {
			/* Cleanup */
			memset(key.key, 0, MAX_KEY_SIZE);
			/* Copy the key in the driver */
			if (copy_from_user
			    (key.key, dwrq->pointer, dwrq->length)) {
				key.len = 0;
				return -EFAULT;
			}
			/* Send the key to the card */
			priv->reg.wep_key[index].size = key.len;
			for (i = 0; i < (priv->reg.wep_key[index].size); i++) {
				priv->reg.wep_key[index].val[i] = key.key[i];
			}
			priv->need_commit |= (SME_WEP_VAL1 << index);
			priv->reg.wep_index = index;
			priv->need_commit |= SME_WEP_INDEX;
		}
	} else {
		if (dwrq->flags & IW_ENCODE_DISABLED) {
			priv->reg.wep_key[0].size = 0;
			priv->reg.wep_key[1].size = 0;
			priv->reg.wep_key[2].size = 0;
			priv->reg.wep_key[3].size = 0;
			priv->reg.privacy_invoked = 0x00;
			if (priv->reg.authenticate_type == AUTH_TYPE_SHARED_KEY) {
				priv->need_commit |= SME_MODE_SET;
			}
			priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;
			wep_on_off = WEP_OFF;
			priv->need_commit |= SME_WEP_FLAG;
		} else {
			/* Do we want to just set the transmit key index ? */
			if ((index >= 0) && (index < 4)) {
				/* set_wep_key(priv, index, 0, 0, 1);   xxx */
				if (priv->reg.wep_key[index].size) {
					priv->reg.wep_index = index;
					priv->need_commit |= SME_WEP_INDEX;
				} else
					return -EINVAL;
			}
		}
	}

	/* Commit the changes if needed */
	if (dwrq->flags & IW_ENCODE_MODE)
		priv->need_commit |= SME_WEP_FLAG;

	if (dwrq->flags & IW_ENCODE_OPEN) {
		if (priv->reg.authenticate_type == AUTH_TYPE_SHARED_KEY) {
			priv->need_commit |= SME_MODE_SET;
		}
		priv->reg.authenticate_type = AUTH_TYPE_OPEN_SYSTEM;
	} else if (dwrq->flags & IW_ENCODE_RESTRICTED) {
		if (priv->reg.authenticate_type == AUTH_TYPE_OPEN_SYSTEM) {
			priv->need_commit |= SME_MODE_SET;
		}
		priv->reg.authenticate_type = AUTH_TYPE_SHARED_KEY;
	}
//      return -EINPROGRESS;            /* Call commit handler */
	if (priv->need_commit) {
		ks_wlan_setup_parameter(priv, priv->need_commit);
		priv->need_commit = 0;
	}
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Encryption Key */
static int ks_wlan_get_encode(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	char zeros[16];
	int index = (dwrq->flags & IW_ENCODE_INDEX) - 1;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	dwrq->flags = IW_ENCODE_DISABLED;

	/* Check encryption mode */
	switch (priv->reg.authenticate_type) {
	case AUTH_TYPE_OPEN_SYSTEM:
		dwrq->flags = IW_ENCODE_OPEN;
		break;
	case AUTH_TYPE_SHARED_KEY:
		dwrq->flags = IW_ENCODE_RESTRICTED;
		break;
	}

	memset(zeros, 0, sizeof(zeros));

	/* Which key do we want ? -1 -> tx index */
	if ((index < 0) || (index >= 4))
		index = priv->reg.wep_index;
	if (priv->reg.privacy_invoked) {
		dwrq->flags &= ~IW_ENCODE_DISABLED;
		/* dwrq->flags |= IW_ENCODE_NOKEY; */
	}
	dwrq->flags |= index + 1;
	DPRINTK(2, "encoding flag = 0x%04X\n", dwrq->flags);
	/* Copy the key to the user buffer */
	if ((index >= 0) && (index < 4))
		dwrq->length = priv->reg.wep_key[index].size;
	if (dwrq->length > 16) {
		dwrq->length = 0;
	}
#if 1	/* IW_ENCODE_NOKEY; */
	if (dwrq->length) {
		if ((index >= 0) && (index < 4))
			memcpy(extra, priv->reg.wep_key[index].val,
			       dwrq->length);
	} else
		memcpy(extra, zeros, dwrq->length);
#endif
	return 0;
}

#ifndef KSC_OPNOTSUPP
/*------------------------------------------------------------------*/
/* Wireless Handler : set Tx-Power */
static int ks_wlan_set_txpow(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	return -EOPNOTSUPP;	/* Not Support */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Tx-Power */
static int ks_wlan_get_txpow(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* Not Support */
	vwrq->value = 0;
	vwrq->disabled = (vwrq->value == 0);
	vwrq->fixed = 1;
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set Retry limits */
static int ks_wlan_set_retry(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	return -EOPNOTSUPP;	/* Not Support */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Retry limits */
static int ks_wlan_get_retry(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* Not Support */
	vwrq->value = 0;
	vwrq->disabled = (vwrq->value == 0);
	vwrq->fixed = 1;
	return 0;
}
#endif /* KSC_OPNOTSUPP */

/*------------------------------------------------------------------*/
/* Wireless Handler : get range info */
static int ks_wlan_get_range(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_range *range = (struct iw_range *)extra;
	int i, k;

	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	dwrq->length = sizeof(struct iw_range);
	memset(range, 0, sizeof(*range));
	range->min_nwid = 0x0000;
	range->max_nwid = 0x0000;
	range->num_channels = 14;
	/* Should be based on cap_rid.country to give only
	 * what the current card support */
	k = 0;
	for (i = 0; i < 13; i++) {	/* channel 1 -- 13 */
		range->freq[k].i = i + 1;	/* List index */
		range->freq[k].m = frequency_list[i] * 100000;
		range->freq[k++].e = 1;	/* Values in table in MHz -> * 10^5 * 10 */
	}
	range->num_frequency = k;
	if (priv->reg.phy_type == D_11B_ONLY_MODE || priv->reg.phy_type == D_11BG_COMPATIBLE_MODE) {	/* channel 14 */
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
	 * May be use for QoS stuff... Jean II */
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

	/* Experimental measurements - boundary 11/5.5 Mb/s */
	/* Note : with or without the (local->rssi), results
	 * are somewhat different. - Jean II */
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

/*------------------------------------------------------------------*/
/* Wireless Handler : set Power Management */
static int ks_wlan_set_power(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	short enabled;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	enabled = vwrq->disabled ? 0 : 1;
	if (enabled == 0) {	/* 0 */
		priv->reg.powermgt = POWMGT_ACTIVE_MODE;
	} else if (enabled) {	/* 1 */
		if (priv->reg.operation_mode == MODE_INFRASTRUCTURE)
			priv->reg.powermgt = POWMGT_SAVE1_MODE;
		else
			return -EINVAL;
	} else if (enabled) {	/* 2 */
		if (priv->reg.operation_mode == MODE_INFRASTRUCTURE)
			priv->reg.powermgt = POWMGT_SAVE2_MODE;
		else
			return -EINVAL;
	} else
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_POW_MNGMT_REQUEST);

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Power Management */
static int ks_wlan_get_power(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (priv->reg.powermgt > 0)
		vwrq->disabled = 0;
	else
		vwrq->disabled = 1;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get wirless statistics */
static int ks_wlan_get_iwstats(struct net_device *dev,
			       struct iw_request_info *info,
			       struct iw_quality *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	vwrq->qual = 0;	/* not supported */
	vwrq->level = priv->wstats.qual.level;
	vwrq->noise = 0;	/* not supported */
	vwrq->updated = 0;

	return 0;
}

#ifndef KSC_OPNOTSUPP
/*------------------------------------------------------------------*/
/* Wireless Handler : set Sensitivity */
static int ks_wlan_set_sens(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	return -EOPNOTSUPP;	/* Not Support */
}

/*------------------------------------------------------------------*/
/* Wireless Handler : get Sensitivity */
static int ks_wlan_get_sens(struct net_device *dev,
			    struct iw_request_info *info, struct iw_param *vwrq,
			    char *extra)
{
	/* Not Support */
	vwrq->value = 0;
	vwrq->disabled = (vwrq->value == 0);
	vwrq->fixed = 1;
	return 0;
}
#endif /* KSC_OPNOTSUPP */

/*------------------------------------------------------------------*/
/* Wireless Handler : get AP List */
/* Note : this is deprecated in favor of IWSCAN */
static int ks_wlan_get_aplist(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct sockaddr *address = (struct sockaddr *)extra;
	struct iw_quality qual[LOCAL_APLIST_MAX];

	int i;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	for (i = 0; i < priv->aplist.size; i++) {
		memcpy(address[i].sa_data, &(priv->aplist.ap[i].bssid[0]),
		       ETH_ALEN);
		address[i].sa_family = ARPHRD_ETHER;
		qual[i].level = 256 - priv->aplist.ap[i].rssi;
		qual[i].qual = priv->aplist.ap[i].sq;
		qual[i].noise = 0;	/* invalid noise value */
		qual[i].updated = 7;
	}
	if (i) {
		dwrq->flags = 1;	/* Should be define'd */
		memcpy(extra + sizeof(struct sockaddr) * i,
		       &qual, sizeof(struct iw_quality) * i);
	}
	dwrq->length = i;

	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : Initiate Scan */
static int ks_wlan_set_scan(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_scan_req *req = NULL;
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/* specified SSID SCAN */
	if (wrqu->data.length == sizeof(struct iw_scan_req)
	    && wrqu->data.flags & IW_SCAN_THIS_ESSID) {
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

/*------------------------------------------------------------------*/
/*
 * Translate scan data returned from the card to a card independent
 * format that the Wireless Tools will understand - Jean II
 */
static inline char *ks_wlan_translate_scan(struct net_device *dev,
					   struct iw_request_info *info,
					   char *current_ev, char *end_buf,
					   struct local_ap_t *ap)
{
	/* struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv; */
	struct iw_event iwe;	/* Temporary buffer */
	u16 capabilities;
	char *current_val;	/* For rates */
	int i;
	static const char rsn_leader[] = "rsn_ie=";
	static const char wpa_leader[] = "wpa_ie=";
	char buf0[RSN_IE_BODY_MAX * 2 + 30];
	char buf1[RSN_IE_BODY_MAX * 2 + 30];
	char *pbuf;
	/* First entry *MUST* be the AP MAC address */
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, ap->bssid, ETH_ALEN);
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_ADDR_LEN);

	/* Other entries will be displayed in the order we give them */

	/* Add the ESSID */
	iwe.u.data.length = ap->ssid.size;
	if (iwe.u.data.length > 32)
		iwe.u.data.length = 32;
	iwe.cmd = SIOCGIWESSID;
	iwe.u.data.flags = 1;
	current_ev =
	    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
				 &(ap->ssid.body[0]));

	/* Add mode */
	iwe.cmd = SIOCGIWMODE;
	capabilities = le16_to_cpu(ap->capability);
	if (capabilities & (BSS_CAP_ESS | BSS_CAP_IBSS)) {
		if (capabilities & BSS_CAP_ESS)
			iwe.u.mode = IW_MODE_INFRA;
		else
			iwe.u.mode = IW_MODE_ADHOC;
		current_ev =
		    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
					 IW_EV_UINT_LEN);
	}

	/* Add frequency */
	iwe.cmd = SIOCGIWFREQ;
	iwe.u.freq.m = ap->channel;
	iwe.u.freq.m = frequency_list[iwe.u.freq.m - 1] * 100000;
	iwe.u.freq.e = 1;
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_FREQ_LEN);

	/* Add quality statistics */
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.level = 256 - ap->rssi;
	iwe.u.qual.qual = ap->sq;
	iwe.u.qual.noise = 0;	/* invalid noise value */
	current_ev =
	    iwe_stream_add_event(info, current_ev, end_buf, &iwe,
				 IW_EV_QUAL_LEN);

	/* Add encryption capability */
	iwe.cmd = SIOCGIWENCODE;
	if (capabilities & BSS_CAP_PRIVACY)
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
	else
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	iwe.u.data.length = 0;
	current_ev =
	    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
				 &(ap->ssid.body[0]));

	/* Rate : stuffing multiple values in a single event require a bit
	 * more of magic - Jean II */
	current_val = current_ev + IW_EV_LCP_LEN;

	iwe.cmd = SIOCGIWRATE;
	/* Those two flags are ignored... */
	iwe.u.bitrate.fixed = iwe.u.bitrate.disabled = 0;

	/* Max 16 values */
	for (i = 0; i < 16; i++) {
		/* NULL terminated */
		if (i >= ap->rate_set.size)
			break;
		/* Bit rate given in 500 kb/s units (+ 0x80) */
		iwe.u.bitrate.value = ((ap->rate_set.body[i] & 0x7f) * 500000);
		/* Add new value to event */
		current_val =
		    iwe_stream_add_value(info, current_ev, current_val, end_buf,
					 &iwe, IW_EV_PARAM_LEN);
	}
	/* Check if we added any event */
	if ((current_val - current_ev) > IW_EV_LCP_LEN)
		current_ev = current_val;

#define GENERIC_INFO_ELEM_ID 0xdd
#define RSN_INFO_ELEM_ID 0x30
	if (ap->rsn_ie.id == RSN_INFO_ELEM_ID && ap->rsn_ie.size != 0) {
		pbuf = &buf0[0];
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		memcpy(buf0, rsn_leader, sizeof(rsn_leader) - 1);
		iwe.u.data.length += sizeof(rsn_leader) - 1;
		pbuf += sizeof(rsn_leader) - 1;

		pbuf += sprintf(pbuf, "%02x", ap->rsn_ie.id);
		pbuf += sprintf(pbuf, "%02x", ap->rsn_ie.size);
		iwe.u.data.length += 4;

		for (i = 0; i < ap->rsn_ie.size; i++)
			pbuf += sprintf(pbuf, "%02x", ap->rsn_ie.body[i]);
		iwe.u.data.length += (ap->rsn_ie.size) * 2;

		DPRINTK(4, "ap->rsn.size=%d\n", ap->rsn_ie.size);

		current_ev =
		    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
					 &buf0[0]);
	}
	if (ap->wpa_ie.id == GENERIC_INFO_ELEM_ID && ap->wpa_ie.size != 0) {
		pbuf = &buf1[0];
		memset(&iwe, 0, sizeof(iwe));
		iwe.cmd = IWEVCUSTOM;
		memcpy(buf1, wpa_leader, sizeof(wpa_leader) - 1);
		iwe.u.data.length += sizeof(wpa_leader) - 1;
		pbuf += sizeof(wpa_leader) - 1;

		pbuf += sprintf(pbuf, "%02x", ap->wpa_ie.id);
		pbuf += sprintf(pbuf, "%02x", ap->wpa_ie.size);
		iwe.u.data.length += 4;

		for (i = 0; i < ap->wpa_ie.size; i++)
			pbuf += sprintf(pbuf, "%02x", ap->wpa_ie.body[i]);
		iwe.u.data.length += (ap->wpa_ie.size) * 2;

		DPRINTK(4, "ap->rsn.size=%d\n", ap->wpa_ie.size);
		DPRINTK(4, "iwe.u.data.length=%d\n", iwe.u.data.length);

		current_ev =
		    iwe_stream_add_point(info, current_ev, end_buf, &iwe,
					 &buf1[0]);
	}

	/* The other data in the scan result are not really
	 * interesting, so for now drop it - Jean II */
	return current_ev;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : Read Scan Results */
static int ks_wlan_get_scan(struct net_device *dev,
			    struct iw_request_info *info, struct iw_point *dwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int i;
	char *current_ev = extra;
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (priv->sme_i.sme_flag & SME_AP_SCAN) {
		DPRINTK(2, "flag AP_SCAN\n");
		return -EAGAIN;
	}

	if (priv->aplist.size == 0) {
		/* Client error, no scan results...
		 * The caller need to restart the scan. */
		DPRINTK(2, "aplist 0\n");
		return -ENODATA;
	}
#if 0
	/* current connect ap */
	if ((priv->connect_status & CONNECT_STATUS_MASK) == CONNECT_STATUS) {
		if ((extra + dwrq->length) - current_ev <= IW_EV_ADDR_LEN) {
			dwrq->length = 0;
			return -E2BIG;
		}
		current_ev = ks_wlan_translate_scan(dev, current_ev,
//                                                  extra + IW_SCAN_MAX_DATA,
						    extra + dwrq->length,
						    &(priv->current_ap));
	}
#endif
	/* Read and parse all entries */
	for (i = 0; i < priv->aplist.size; i++) {
		if ((extra + dwrq->length) - current_ev <= IW_EV_ADDR_LEN) {
			dwrq->length = 0;
			return -E2BIG;
		}
		/* Translate to WE format this entry */
		current_ev = ks_wlan_translate_scan(dev, info, current_ev,
//                                                  extra + IW_SCAN_MAX_DATA,
						    extra + dwrq->length,
						    &(priv->aplist.ap[i]));
	}
	/* Length of data */
	dwrq->length = (current_ev - extra);
	dwrq->flags = 0;

	return 0;
}

/*------------------------------------------------------------------*/
/* Commit handler : called after a bunch of SET operations */
static int ks_wlan_config_commit(struct net_device *dev,
				 struct iw_request_info *info, void *zwrq,
				 char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (!priv->need_commit)
		return 0;

	ks_wlan_setup_parameter(priv, priv->need_commit);
	priv->need_commit = 0;
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless handler : set association ie params */
static int ks_wlan_set_genie(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	return 0;
//      return -EOPNOTSUPP;
}

/*------------------------------------------------------------------*/
/* Wireless handler : set authentication mode params */
static int ks_wlan_set_auth_mode(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int index = (vwrq->flags & IW_AUTH_INDEX);
	int value = vwrq->value;

	DPRINTK(2, "index=%d:value=%08X\n", index, value);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	switch (index) {
	case IW_AUTH_WPA_VERSION:	/* 0 */
		switch (value) {
		case IW_AUTH_WPA_VERSION_DISABLED:
			priv->wpa.version = value;
			if (priv->wpa.rsn_enabled) {
				priv->wpa.rsn_enabled = 0;
			}
			priv->need_commit |= SME_RSN;
			break;
		case IW_AUTH_WPA_VERSION_WPA:
		case IW_AUTH_WPA_VERSION_WPA2:
			priv->wpa.version = value;
			if (!(priv->wpa.rsn_enabled)) {
				priv->wpa.rsn_enabled = 1;
			}
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

/*------------------------------------------------------------------*/
/* Wireless handler : get authentication mode params */
static int ks_wlan_get_auth_mode(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_param *vwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	int index = (vwrq->flags & IW_AUTH_INDEX);
	DPRINTK(2, "index=%d\n", index);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/*  WPA (not used ?? wpa_supplicant) */
	switch (index) {
	case IW_AUTH_WPA_VERSION:
		vwrq->value = priv->wpa.version;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		vwrq->value = priv->wpa.pairwise_suite;
		break;
	case IW_AUTH_CIPHER_GROUP:
		vwrq->value = priv->wpa.group_suite;
		break;
	case IW_AUTH_KEY_MGMT:
		vwrq->value = priv->wpa.key_mgmt_suite;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		vwrq->value = priv->wpa.auth_alg;
		break;
	case IW_AUTH_WPA_ENABLED:
		vwrq->value = priv->wpa.rsn_enabled;
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

/*------------------------------------------------------------------*/
/* Wireless Handler : set encoding token & mode (WPA)*/
static int ks_wlan_set_encode_ext(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_encode_ext *enc;
	int index = dwrq->flags & IW_ENCODE_INDEX;
	unsigned int commit = 0;

	enc = (struct iw_encode_ext *)extra;

	DPRINTK(2, "flags=%04X:: ext_flags=%08X\n", dwrq->flags,
		enc->ext_flags);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (index < 1 || index > 4)
		return -EINVAL;
	else
		index--;

	if (dwrq->flags & IW_ENCODE_DISABLED) {
		priv->wpa.key[index].key_len = 0;
	}

	if (enc) {
		priv->wpa.key[index].ext_flags = enc->ext_flags;
		if (enc->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
			priv->wpa.txkey = index;
			commit |= SME_WEP_INDEX;
		} else if (enc->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
			memcpy(&priv->wpa.key[index].rx_seq[0],
			       enc->rx_seq, IW_ENCODE_SEQ_MAX_SIZE);
		}

		memcpy(&priv->wpa.key[index].addr.sa_data[0],
		       &enc->addr.sa_data[0], ETH_ALEN);

		switch (enc->alg) {
		case IW_ENCODE_ALG_NONE:
			if (priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x00;
				commit |= SME_WEP_FLAG;
			}
			priv->wpa.key[index].key_len = 0;

			break;
		case IW_ENCODE_ALG_WEP:
		case IW_ENCODE_ALG_CCMP:
			if (!priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x01;
				commit |= SME_WEP_FLAG;
			}
			if (enc->key_len) {
				memcpy(&priv->wpa.key[index].key_val[0],
				       &enc->key[0], enc->key_len);
				priv->wpa.key[index].key_len = enc->key_len;
				commit |= (SME_WEP_VAL1 << index);
			}
			break;
		case IW_ENCODE_ALG_TKIP:
			if (!priv->reg.privacy_invoked) {
				priv->reg.privacy_invoked = 0x01;
				commit |= SME_WEP_FLAG;
			}
			if (enc->key_len == 32) {
				memcpy(&priv->wpa.key[index].key_val[0],
				       &enc->key[0], enc->key_len - 16);
				priv->wpa.key[index].key_len =
				    enc->key_len - 16;
				if (priv->wpa.key_mgmt_suite == 4) {	/* WPA_NONE */
					memcpy(&priv->wpa.key[index].
					       tx_mic_key[0], &enc->key[16], 8);
					memcpy(&priv->wpa.key[index].
					       rx_mic_key[0], &enc->key[16], 8);
				} else {
					memcpy(&priv->wpa.key[index].
					       tx_mic_key[0], &enc->key[16], 8);
					memcpy(&priv->wpa.key[index].
					       rx_mic_key[0], &enc->key[24], 8);
				}
				commit |= (SME_WEP_VAL1 << index);
			}
			break;
		default:
			return -EINVAL;
		}
		priv->wpa.key[index].alg = enc->alg;
	} else
		return -EINVAL;

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

/*------------------------------------------------------------------*/
/* Wireless Handler : get encoding token & mode (WPA)*/
static int ks_wlan_get_encode_ext(struct net_device *dev,
				  struct iw_request_info *info,
				  struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}

	/* for SLEEP MODE */
	/*  WPA (not used ?? wpa_supplicant)
	   struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	   struct iw_encode_ext *enc;
	   enc = (struct iw_encode_ext *)extra;
	   int index = dwrq->flags & IW_ENCODE_INDEX;
	   WPA (not used ?? wpa_supplicant) */
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : PMKSA cache operation (WPA2) */
static int ks_wlan_set_pmksa(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_pmksa *pmksa;
	int i;
	struct pmk_t *pmk;
	struct list_head *ptr;

	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (!extra) {
		return -EINVAL;
	}
	pmksa = (struct iw_pmksa *)extra;
	DPRINTK(2, "cmd=%d\n", pmksa->cmd);

	switch (pmksa->cmd) {
	case IW_PMKSA_ADD:
		if (list_empty(&priv->pmklist.head)) {	/* new list */
			for (i = 0; i < PMK_LIST_MAX; i++) {
				pmk = &priv->pmklist.pmk[i];
				if (!memcmp
				    ("\x00\x00\x00\x00\x00\x00", pmk->bssid,
				     ETH_ALEN))
					break;
			}
			memcpy(pmk->bssid, pmksa->bssid.sa_data, ETH_ALEN);
			memcpy(pmk->pmkid, pmksa->pmkid, IW_PMKID_LEN);
			list_add(&pmk->list, &priv->pmklist.head);
			priv->pmklist.size++;
		} else {	/* search cache data */
			list_for_each(ptr, &priv->pmklist.head) {
				pmk = list_entry(ptr, struct pmk_t, list);
				if (!memcmp(pmksa->bssid.sa_data, pmk->bssid, ETH_ALEN)) {	/* match address! list move to head. */
					memcpy(pmk->pmkid, pmksa->pmkid,
					       IW_PMKID_LEN);
					list_move(&pmk->list,
						  &priv->pmklist.head);
					break;
				}
			}
			if (ptr == &priv->pmklist.head) {	/* not find address. */
				if (PMK_LIST_MAX > priv->pmklist.size) {	/* new cache data */
					for (i = 0; i < PMK_LIST_MAX; i++) {
						pmk = &priv->pmklist.pmk[i];
						if (!memcmp
						    ("\x00\x00\x00\x00\x00\x00",
						     pmk->bssid, ETH_ALEN))
							break;
					}
					memcpy(pmk->bssid, pmksa->bssid.sa_data,
					       ETH_ALEN);
					memcpy(pmk->pmkid, pmksa->pmkid,
					       IW_PMKID_LEN);
					list_add(&pmk->list,
						 &priv->pmklist.head);
					priv->pmklist.size++;
				} else {	/* overwrite old cache data */
					pmk =
					    list_entry(priv->pmklist.head.prev,
						       struct pmk_t, list);
					memcpy(pmk->bssid, pmksa->bssid.sa_data,
					       ETH_ALEN);
					memcpy(pmk->pmkid, pmksa->pmkid,
					       IW_PMKID_LEN);
					list_move(&pmk->list,
						  &priv->pmklist.head);
				}
			}
		}
		break;
	case IW_PMKSA_REMOVE:
		if (list_empty(&priv->pmklist.head)) {	/* list empty */
			return -EINVAL;
		} else {	/* search cache data */
			list_for_each(ptr, &priv->pmklist.head) {
				pmk = list_entry(ptr, struct pmk_t, list);
				if (!memcmp(pmksa->bssid.sa_data, pmk->bssid, ETH_ALEN)) {	/* match address! list del. */
					memset(pmk->bssid, 0, ETH_ALEN);
					memset(pmk->pmkid, 0, IW_PMKID_LEN);
					list_del_init(&pmk->list);
					break;
				}
			}
			if (ptr == &priv->pmklist.head) {	/* not find address. */
				return 0;
			}
		}
		break;
	case IW_PMKSA_FLUSH:
		memset(&(priv->pmklist), 0, sizeof(priv->pmklist));
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

	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_statistics *wstats = &priv->wstats;

	if (!atomic_read(&update_phyinfo)) {
		if (priv->dev_state < DEVICE_STATE_READY)
			return NULL;	/* not finished initialize */
		else
			return wstats;
	}

	/* Packets discarded in the wireless adapter due to wireless
	 * specific problems */
	wstats->discard.nwid = 0;	/* Rx invalid nwid      */
	wstats->discard.code = 0;	/* Rx invalid crypt     */
	wstats->discard.fragment = 0;	/* Rx invalid frag      */
	wstats->discard.retries = 0;	/* Tx excessive retries */
	wstats->discard.misc = 0;	/* Invalid misc         */
	wstats->miss.beacon = 0;	/* Missed beacon        */

	return wstats;
}

/*------------------------------------------------------------------*/
/* Private handler : set stop request */
static int ks_wlan_set_stop_request(struct net_device *dev,
				    struct iw_request_info *info, __u32 * uwrq,
				    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (!(*uwrq))
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_STOP_REQUEST);
	return 0;
}

/*------------------------------------------------------------------*/
/* Wireless Handler : set MLME */
#include <linux/ieee80211.h>
static int ks_wlan_set_mlme(struct net_device *dev,
			    struct iw_request_info *info, struct iw_point *dwrq,
			    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	__u32 mode;

	DPRINTK(2, ":%d :%d\n", mlme->cmd, mlme->reason_code);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		if (mlme->reason_code == WLAN_REASON_MIC_FAILURE) {
			return 0;
		}
	case IW_MLME_DISASSOC:
		mode = 1;
		return ks_wlan_set_stop_request(dev, NULL, &mode, NULL);
	default:
		return -EOPNOTSUPP;	/* Not Support */
	}
}

/*------------------------------------------------------------------*/
/* Private handler : get firemware version */
static int ks_wlan_get_firmware_version(struct net_device *dev,
					struct iw_request_info *info,
					struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	strcpy(extra, &(priv->firmware_version[0]));
	dwrq->length = priv->version_size + 1;
	return 0;
}

#if 0
/*------------------------------------------------------------------*/
/* Private handler : set force disconnect status */
static int ks_wlan_set_detach(struct net_device *dev,
			      struct iw_request_info *info, __u32 * uwrq,
			      char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == CONNECT_STATUS) {	/* 0 */
		priv->connect_status &= ~FORCE_DISCONNECT;
		if ((priv->connect_status & CONNECT_STATUS_MASK) ==
		    CONNECT_STATUS)
			netif_carrier_on(dev);
	} else if (*uwrq == DISCONNECT_STATUS) {	/* 1 */
		priv->connect_status |= FORCE_DISCONNECT;
		netif_carrier_off(dev);
	} else
		return -EINVAL;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get force disconnect status */
static int ks_wlan_get_detach(struct net_device *dev,
			      struct iw_request_info *info, __u32 * uwrq,
			      char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = ((priv->connect_status & FORCE_DISCONNECT) ? 1 : 0);
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get connect status */
static int ks_wlan_get_connect(struct net_device *dev,
			       struct iw_request_info *info, __u32 * uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = (priv->connect_status & CONNECT_STATUS_MASK);
	return 0;
}
#endif

/*------------------------------------------------------------------*/
/* Private handler : set preamble */
static int ks_wlan_set_preamble(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == LONG_PREAMBLE) {	/* 0 */
		priv->reg.preamble = LONG_PREAMBLE;
	} else if (*uwrq == SHORT_PREAMBLE) {	/* 1 */
		priv->reg.preamble = SHORT_PREAMBLE;
	} else
		return -EINVAL;

	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */

}

/*------------------------------------------------------------------*/
/* Private handler : get preamble */
static int ks_wlan_get_preamble(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.preamble;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set power save mode */
static int ks_wlan_set_powermgt(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == POWMGT_ACTIVE_MODE) {	/* 0 */
		priv->reg.powermgt = POWMGT_ACTIVE_MODE;
	} else if (*uwrq == POWMGT_SAVE1_MODE) {	/* 1 */
		if (priv->reg.operation_mode == MODE_INFRASTRUCTURE)
			priv->reg.powermgt = POWMGT_SAVE1_MODE;
		else
			return -EINVAL;
	} else if (*uwrq == POWMGT_SAVE2_MODE) {	/* 2 */
		if (priv->reg.operation_mode == MODE_INFRASTRUCTURE)
			priv->reg.powermgt = POWMGT_SAVE2_MODE;
		else
			return -EINVAL;
	} else
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_POW_MNGMT_REQUEST);

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get power save made */
static int ks_wlan_get_powermgt(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.powermgt;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set scan type */
static int ks_wlan_set_scan_type(struct net_device *dev,
				 struct iw_request_info *info, __u32 * uwrq,
				 char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == ACTIVE_SCAN) {	/* 0 */
		priv->reg.scan_type = ACTIVE_SCAN;
	} else if (*uwrq == PASSIVE_SCAN) {	/* 1 */
		priv->reg.scan_type = PASSIVE_SCAN;
	} else
		return -EINVAL;

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get scan type */
static int ks_wlan_get_scan_type(struct net_device *dev,
				 struct iw_request_info *info, __u32 * uwrq,
				 char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.scan_type;
	return 0;
}

#if 0
/*------------------------------------------------------------------*/
/* Private handler : write raw data to device */
static int ks_wlan_data_write(struct net_device *dev,
			      struct iw_request_info *info,
			      struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	unsigned char *wbuff = NULL;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	wbuff = (unsigned char *)kmalloc(dwrq->length, GFP_ATOMIC);
	if (!wbuff)
		return -EFAULT;
	memcpy(wbuff, extra, dwrq->length);

	/* write to device */
	ks_wlan_hw_tx(priv, wbuff, dwrq->length, NULL, NULL, NULL);

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : read raw data form device */
static int ks_wlan_data_read(struct net_device *dev,
			     struct iw_request_info *info,
			     struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	unsigned short read_length;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (!atomic_read(&priv->event_count)) {
		if (priv->dev_state < DEVICE_STATE_BOOT) {	/* Remove device */
			read_length = 4;
			memset(extra, 0xff, read_length);
			dwrq->length = read_length;
			return 0;
		}
		read_length = 0;
		memset(extra, 0, 1);
		dwrq->length = 0;
		return 0;
	}

	if (atomic_read(&priv->event_count) > 0)
		atomic_dec(&priv->event_count);

	spin_lock(&priv->dev_read_lock);	/* request spin lock */

	/* Copy length max size 0x07ff */
	if (priv->dev_size[priv->dev_count] > 2047)
		read_length = 2047;
	else
		read_length = priv->dev_size[priv->dev_count];

	/* Copy data */
	memcpy(extra, &(priv->dev_data[priv->dev_count][0]), read_length);

	spin_unlock(&priv->dev_read_lock);	/* release spin lock */

	/* Initialize */
	priv->dev_data[priv->dev_count] = 0;
	priv->dev_size[priv->dev_count] = 0;

	priv->dev_count++;
	if (priv->dev_count == DEVICE_STOCK_COUNT)
		priv->dev_count = 0;

	/* Set read size */
	dwrq->length = read_length;

	return 0;
}
#endif

#if 0
/*------------------------------------------------------------------*/
/* Private handler : get wep string */
#define WEP_ASCII_BUFF_SIZE (17+64*4+1)
static int ks_wlan_get_wep_ascii(struct net_device *dev,
				 struct iw_request_info *info,
				 struct iw_point *dwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	int i, j, len = 0;
	char tmp[WEP_ASCII_BUFF_SIZE];

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	strcpy(tmp, " WEP keys ASCII \n");
	len += strlen(" WEP keys ASCII \n");

	for (i = 0; i < 4; i++) {
		strcpy(tmp + len, "\t[");
		len += strlen("\t[");
		tmp[len] = '1' + i;
		len++;
		strcpy(tmp + len, "] ");
		len += strlen("] ");
		if (priv->reg.wep_key[i].size) {
			strcpy(tmp + len,
			       (priv->reg.wep_key[i].size <
				6 ? "(40bits) [" : "(104bits) ["));
			len +=
			    strlen((priv->reg.wep_key[i].size <
				    6 ? "(40bits) [" : "(104bits) ["));
			for (j = 0; j < priv->reg.wep_key[i].size; j++, len++)
				tmp[len] =
				    (isprint(priv->reg.wep_key[i].val[j]) ?
				     priv->reg.wep_key[i].val[j] : ' ');

			strcpy(tmp + len, "]\n");
			len += strlen("]\n");
		} else {
			strcpy(tmp + len, "off\n");
			len += strlen("off\n");
		}
	}

	memcpy(extra, tmp, len);
	dwrq->length = len + 1;
	return 0;
}
#endif

/*------------------------------------------------------------------*/
/* Private handler : set beacon lost count */
static int ks_wlan_set_beacon_lost(struct net_device *dev,
				   struct iw_request_info *info, __u32 * uwrq,
				   char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq >= BEACON_LOST_COUNT_MIN && *uwrq <= BEACON_LOST_COUNT_MAX) {
		priv->reg.beacon_lost_count = *uwrq;
	} else
		return -EINVAL;

	if (priv->reg.operation_mode == MODE_INFRASTRUCTURE) {
		priv->need_commit |= SME_MODE_SET;
		return -EINPROGRESS;	/* Call commit handler */
	} else
		return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get beacon lost count */
static int ks_wlan_get_beacon_lost(struct net_device *dev,
				   struct iw_request_info *info, __u32 * uwrq,
				   char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.beacon_lost_count;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set phy type */
static int ks_wlan_set_phy_type(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == D_11B_ONLY_MODE) {	/* 0 */
		priv->reg.phy_type = D_11B_ONLY_MODE;
	} else if (*uwrq == D_11G_ONLY_MODE) {	/* 1 */
		priv->reg.phy_type = D_11G_ONLY_MODE;
	} else if (*uwrq == D_11BG_COMPATIBLE_MODE) {	/* 2 */
		priv->reg.phy_type = D_11BG_COMPATIBLE_MODE;
	} else
		return -EINVAL;

	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Private handler : get phy type */
static int ks_wlan_get_phy_type(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.phy_type;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set cts mode */
static int ks_wlan_set_cts_mode(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == CTS_MODE_FALSE) {	/* 0 */
		priv->reg.cts_mode = CTS_MODE_FALSE;
	} else if (*uwrq == CTS_MODE_TRUE) {	/* 1 */
		if (priv->reg.phy_type == D_11G_ONLY_MODE ||
		    priv->reg.phy_type == D_11BG_COMPATIBLE_MODE)
			priv->reg.cts_mode = CTS_MODE_TRUE;
		else
			priv->reg.cts_mode = CTS_MODE_FALSE;
	} else
		return -EINVAL;

	priv->need_commit |= SME_MODE_SET;
	return -EINPROGRESS;	/* Call commit handler */
}

/*------------------------------------------------------------------*/
/* Private handler : get cts mode */
static int ks_wlan_get_cts_mode(struct net_device *dev,
				struct iw_request_info *info, __u32 * uwrq,
				char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.cts_mode;
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set sleep mode */
static int ks_wlan_set_sleep_mode(struct net_device *dev,
				  struct iw_request_info *info,
				  __u32 * uwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "\n");

	if (*uwrq == SLP_SLEEP) {
		priv->sleep_mode = *uwrq;
		printk("SET_SLEEP_MODE %d\n", priv->sleep_mode);

		hostif_sme_enqueue(priv, SME_STOP_REQUEST);
		hostif_sme_enqueue(priv, SME_SLEEP_REQUEST);

	} else if (*uwrq == SLP_ACTIVE) {
		priv->sleep_mode = *uwrq;
		printk("SET_SLEEP_MODE %d\n", priv->sleep_mode);
		hostif_sme_enqueue(priv, SME_SLEEP_REQUEST);
	} else {
		printk("SET_SLEEP_MODE %d errror\n", *uwrq);
		return -EINVAL;
	}

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get sleep mode */
static int ks_wlan_get_sleep_mode(struct net_device *dev,
				  struct iw_request_info *info,
				  __u32 * uwrq, char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "GET_SLEEP_MODE %d\n", priv->sleep_mode);
	*uwrq = priv->sleep_mode;

	return 0;
}

#if 0
/*------------------------------------------------------------------*/
/* Private handler : set phy information timer */
static int ks_wlan_set_phy_information_timer(struct net_device *dev,
					     struct iw_request_info *info,
					     __u32 * uwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq >= 0 && *uwrq <= 0xFFFF)	/* 0-65535 */
		priv->reg.phy_info_timer = (uint16_t) * uwrq;
	else
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_PHY_INFO_REQUEST);

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get phy information timer */
static int ks_wlan_get_phy_information_timer(struct net_device *dev,
					     struct iw_request_info *info,
					     __u32 * uwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->reg.phy_info_timer;
	return 0;
}
#endif

#ifdef WPS
/*------------------------------------------------------------------*/
/* Private handler : set WPS enable */
static int ks_wlan_set_wps_enable(struct net_device *dev,
				  struct iw_request_info *info, __u32 * uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq == 0 || *uwrq == 1)
		priv->wps.wps_enabled = *uwrq;
	else
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_WPS_ENABLE_REQUEST);

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get WPS enable */
static int ks_wlan_get_wps_enable(struct net_device *dev,
				  struct iw_request_info *info, __u32 * uwrq,
				  char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->wps.wps_enabled;
	printk("return=%d\n", *uwrq);

	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set WPS probe req */
static int ks_wlan_set_wps_probe_req(struct net_device *dev,
				     struct iw_request_info *info,
				     struct iw_point *dwrq, char *extra)
{
	uint8_t *p = extra;
	unsigned char len;
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	DPRINTK(2, "dwrq->length=%d\n", dwrq->length);

	/* length check */
	if (p[1] + 2 != dwrq->length || dwrq->length > 256) {
		return -EINVAL;
	}

	priv->wps.ielen = p[1] + 2 + 1;	/* IE header + IE + sizeof(len) */
	len = p[1] + 2;	/* IE header + IE */

	memcpy(priv->wps.ie, &len, sizeof(len));
	p = memcpy(priv->wps.ie + 1, p, len);

	DPRINTK(2, "%d(%#x): %02X %02X %02X %02X ... %02X %02X %02X\n",
		priv->wps.ielen, priv->wps.ielen, p[0], p[1], p[2], p[3],
		p[priv->wps.ielen - 3], p[priv->wps.ielen - 2],
		p[priv->wps.ielen - 1]);

	hostif_sme_enqueue(priv, SME_WPS_PROBE_REQUEST);

	return 0;
}

#if 0
/*------------------------------------------------------------------*/
/* Private handler : get WPS probe req */
static int ks_wlan_get_wps_probe_req(struct net_device *dev,
				     struct iw_request_info *info,
				     __u32 * uwrq, char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;
	DPRINTK(2, "\n");

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	return 0;
}
#endif
#endif /* WPS */

/*------------------------------------------------------------------*/
/* Private handler : set tx gain control value */
static int ks_wlan_set_tx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 * uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq >= 0 && *uwrq <= 0xFF)	/* 0-255 */
		priv->gain.TxGain = (uint8_t) * uwrq;
	else
		return -EINVAL;

	if (priv->gain.TxGain < 0xFF)
		priv->gain.TxMode = 1;
	else
		priv->gain.TxMode = 0;

	hostif_sme_enqueue(priv, SME_SET_GAIN);
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get tx gain control value */
static int ks_wlan_get_tx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 * uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->gain.TxGain;
	hostif_sme_enqueue(priv, SME_GET_GAIN);
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : set rx gain control value */
static int ks_wlan_set_rx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 * uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq >= 0 && *uwrq <= 0xFF)	/* 0-255 */
		priv->gain.RxGain = (uint8_t) * uwrq;
	else
		return -EINVAL;

	if (priv->gain.RxGain < 0xFF)
		priv->gain.RxMode = 1;
	else
		priv->gain.RxMode = 0;

	hostif_sme_enqueue(priv, SME_SET_GAIN);
	return 0;
}

/*------------------------------------------------------------------*/
/* Private handler : get rx gain control value */
static int ks_wlan_get_rx_gain(struct net_device *dev,
			       struct iw_request_info *info, __u32 * uwrq,
			       char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	*uwrq = priv->gain.RxGain;
	hostif_sme_enqueue(priv, SME_GET_GAIN);
	return 0;
}

#if 0
/*------------------------------------------------------------------*/
/* Private handler : set region value */
static int ks_wlan_set_region(struct net_device *dev,
			      struct iw_request_info *info, __u32 * uwrq,
			      char *extra)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev->priv;

	if (priv->sleep_mode == SLP_SLEEP) {
		return -EPERM;
	}
	/* for SLEEP MODE */
	if (*uwrq >= 0x9 && *uwrq <= 0xF)	/* 0x9-0xf */
		priv->region = (uint8_t) * uwrq;
	else
		return -EINVAL;

	hostif_sme_enqueue(priv, SME_SET_REGION);
	return 0;
}
#endif

/*------------------------------------------------------------------*/
/* Private handler : get eeprom checksum result */
static int ks_wlan_get_eeprom_cksum(struct net_device *dev,
				    struct iw_request_info *info, __u32 * uwrq,
				    char *extra)
{
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	*uwrq = priv->eeprom_checksum;
	return 0;
}

static void print_hif_event(int event)
{

	switch (event) {
	case HIF_DATA_REQ:
		printk("HIF_DATA_REQ\n");
		break;
	case HIF_DATA_IND:
		printk("HIF_DATA_IND\n");
		break;
	case HIF_MIB_GET_REQ:
		printk("HIF_MIB_GET_REQ\n");
		break;
	case HIF_MIB_GET_CONF:
		printk("HIF_MIB_GET_CONF\n");
		break;
	case HIF_MIB_SET_REQ:
		printk("HIF_MIB_SET_REQ\n");
		break;
	case HIF_MIB_SET_CONF:
		printk("HIF_MIB_SET_CONF\n");
		break;
	case HIF_POWERMGT_REQ:
		printk("HIF_POWERMGT_REQ\n");
		break;
	case HIF_POWERMGT_CONF:
		printk("HIF_POWERMGT_CONF\n");
		break;
	case HIF_START_REQ:
		printk("HIF_START_REQ\n");
		break;
	case HIF_START_CONF:
		printk("HIF_START_CONF\n");
		break;
	case HIF_CONNECT_IND:
		printk("HIF_CONNECT_IND\n");
		break;
	case HIF_STOP_REQ:
		printk("HIF_STOP_REQ\n");
		break;
	case HIF_STOP_CONF:
		printk("HIF_STOP_CONF\n");
		break;
	case HIF_PS_ADH_SET_REQ:
		printk("HIF_PS_ADH_SET_REQ\n");
		break;
	case HIF_PS_ADH_SET_CONF:
		printk("HIF_PS_ADH_SET_CONF\n");
		break;
	case HIF_INFRA_SET_REQ:
		printk("HIF_INFRA_SET_REQ\n");
		break;
	case HIF_INFRA_SET_CONF:
		printk("HIF_INFRA_SET_CONF\n");
		break;
	case HIF_ADH_SET_REQ:
		printk("HIF_ADH_SET_REQ\n");
		break;
	case HIF_ADH_SET_CONF:
		printk("HIF_ADH_SET_CONF\n");
		break;
	case HIF_AP_SET_REQ:
		printk("HIF_AP_SET_REQ\n");
		break;
	case HIF_AP_SET_CONF:
		printk("HIF_AP_SET_CONF\n");
		break;
	case HIF_ASSOC_INFO_IND:
		printk("HIF_ASSOC_INFO_IND\n");
		break;
	case HIF_MIC_FAILURE_REQ:
		printk("HIF_MIC_FAILURE_REQ\n");
		break;
	case HIF_MIC_FAILURE_CONF:
		printk("HIF_MIC_FAILURE_CONF\n");
		break;
	case HIF_SCAN_REQ:
		printk("HIF_SCAN_REQ\n");
		break;
	case HIF_SCAN_CONF:
		printk("HIF_SCAN_CONF\n");
		break;
	case HIF_PHY_INFO_REQ:
		printk("HIF_PHY_INFO_REQ\n");
		break;
	case HIF_PHY_INFO_CONF:
		printk("HIF_PHY_INFO_CONF\n");
		break;
	case HIF_SLEEP_REQ:
		printk("HIF_SLEEP_REQ\n");
		break;
	case HIF_SLEEP_CONF:
		printk("HIF_SLEEP_CONF\n");
		break;
	case HIF_PHY_INFO_IND:
		printk("HIF_PHY_INFO_IND\n");
		break;
	case HIF_SCAN_IND:
		printk("HIF_SCAN_IND\n");
		break;
	case HIF_INFRA_SET2_REQ:
		printk("HIF_INFRA_SET2_REQ\n");
		break;
	case HIF_INFRA_SET2_CONF:
		printk("HIF_INFRA_SET2_CONF\n");
		break;
	case HIF_ADH_SET2_REQ:
		printk("HIF_ADH_SET2_REQ\n");
		break;
	case HIF_ADH_SET2_CONF:
		printk("HIF_ADH_SET2_CONF\n");
	}
}

/*------------------------------------------------------------------*/
/* Private handler : get host command history */
static int ks_wlan_hostt(struct net_device *dev, struct iw_request_info *info,
			 __u32 * uwrq, char *extra)
{
	int i, event;
	struct ks_wlan_private *priv =
	    (struct ks_wlan_private *)netdev_priv(dev);

	for (i = 63; i >= 0; i--) {
		event =
		    priv->hostt.buff[(priv->hostt.qtail - 1 - i) %
				     SME_EVENT_BUFF_SIZE];
		print_hif_event(event);
	}
	return 0;
}

/* Structures to export the Wireless Handlers */

static const struct iw_priv_args ks_wlan_private_args[] = {
/*{ cmd, set_args, get_args, name[16] } */
	{KS_WLAN_GET_FIRM_VERSION, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_CHAR | (128 + 1), "GetFirmwareVer"},
#ifdef WPS
	{KS_WLAN_SET_WPS_ENABLE, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 IW_PRIV_TYPE_NONE, "SetWPSEnable"},
	{KS_WLAN_GET_WPS_ENABLE, IW_PRIV_TYPE_NONE,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "GetW"},
	{KS_WLAN_SET_WPS_PROBE_REQ, IW_PRIV_TYPE_BYTE | 2047, IW_PRIV_TYPE_NONE,
	 "SetWPSProbeReq"},
#endif /* WPS */
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
	(iw_handler) ks_wlan_config_commit,	/* SIOCSIWCOMMIT */
	(iw_handler) ks_wlan_get_name,	/* SIOCGIWNAME */
	(iw_handler) NULL,	/* SIOCSIWNWID */
	(iw_handler) NULL,	/* SIOCGIWNWID */
	(iw_handler) ks_wlan_set_freq,	/* SIOCSIWFREQ */
	(iw_handler) ks_wlan_get_freq,	/* SIOCGIWFREQ */
	(iw_handler) ks_wlan_set_mode,	/* SIOCSIWMODE */
	(iw_handler) ks_wlan_get_mode,	/* SIOCGIWMODE */
#ifndef KSC_OPNOTSUPP
	(iw_handler) ks_wlan_set_sens,	/* SIOCSIWSENS */
	(iw_handler) ks_wlan_get_sens,	/* SIOCGIWSENS */
#else /* KSC_OPNOTSUPP */
	(iw_handler) NULL,	/* SIOCSIWSENS */
	(iw_handler) NULL,	/* SIOCGIWSENS */
#endif /* KSC_OPNOTSUPP */
	(iw_handler) NULL,	/* SIOCSIWRANGE */
	(iw_handler) ks_wlan_get_range,	/* SIOCGIWRANGE */
	(iw_handler) NULL,	/* SIOCSIWPRIV */
	(iw_handler) NULL,	/* SIOCGIWPRIV */
	(iw_handler) NULL,	/* SIOCSIWSTATS */
	(iw_handler) ks_wlan_get_iwstats,	/* SIOCGIWSTATS */
	(iw_handler) NULL,	/* SIOCSIWSPY */
	(iw_handler) NULL,	/* SIOCGIWSPY */
	(iw_handler) NULL,	/* SIOCSIWTHRSPY */
	(iw_handler) NULL,	/* SIOCGIWTHRSPY */
	(iw_handler) ks_wlan_set_wap,	/* SIOCSIWAP */
	(iw_handler) ks_wlan_get_wap,	/* SIOCGIWAP */
//      (iw_handler) NULL,                      /* SIOCSIWMLME */
	(iw_handler) ks_wlan_set_mlme,	/* SIOCSIWMLME */
	(iw_handler) ks_wlan_get_aplist,	/* SIOCGIWAPLIST */
	(iw_handler) ks_wlan_set_scan,	/* SIOCSIWSCAN */
	(iw_handler) ks_wlan_get_scan,	/* SIOCGIWSCAN */
	(iw_handler) ks_wlan_set_essid,	/* SIOCSIWESSID */
	(iw_handler) ks_wlan_get_essid,	/* SIOCGIWESSID */
	(iw_handler) ks_wlan_set_nick,	/* SIOCSIWNICKN */
	(iw_handler) ks_wlan_get_nick,	/* SIOCGIWNICKN */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) ks_wlan_set_rate,	/* SIOCSIWRATE */
	(iw_handler) ks_wlan_get_rate,	/* SIOCGIWRATE */
	(iw_handler) ks_wlan_set_rts,	/* SIOCSIWRTS */
	(iw_handler) ks_wlan_get_rts,	/* SIOCGIWRTS */
	(iw_handler) ks_wlan_set_frag,	/* SIOCSIWFRAG */
	(iw_handler) ks_wlan_get_frag,	/* SIOCGIWFRAG */
#ifndef KSC_OPNOTSUPP
	(iw_handler) ks_wlan_set_txpow,	/* SIOCSIWTXPOW */
	(iw_handler) ks_wlan_get_txpow,	/* SIOCGIWTXPOW */
	(iw_handler) ks_wlan_set_retry,	/* SIOCSIWRETRY */
	(iw_handler) ks_wlan_get_retry,	/* SIOCGIWRETRY */
#else /* KSC_OPNOTSUPP */
	(iw_handler) NULL,	/* SIOCSIWTXPOW */
	(iw_handler) NULL,	/* SIOCGIWTXPOW */
	(iw_handler) NULL,	/* SIOCSIWRETRY */
	(iw_handler) NULL,	/* SIOCGIWRETRY */
#endif /* KSC_OPNOTSUPP */
	(iw_handler) ks_wlan_set_encode,	/* SIOCSIWENCODE */
	(iw_handler) ks_wlan_get_encode,	/* SIOCGIWENCODE */
	(iw_handler) ks_wlan_set_power,	/* SIOCSIWPOWER */
	(iw_handler) ks_wlan_get_power,	/* SIOCGIWPOWER */
	(iw_handler) NULL,	/* -- hole -- */
	(iw_handler) NULL,	/* -- hole -- */
//      (iw_handler) NULL,                      /* SIOCSIWGENIE */
	(iw_handler) ks_wlan_set_genie,	/* SIOCSIWGENIE */
	(iw_handler) NULL,	/* SIOCGIWGENIE */
	(iw_handler) ks_wlan_set_auth_mode,	/* SIOCSIWAUTH */
	(iw_handler) ks_wlan_get_auth_mode,	/* SIOCGIWAUTH */
	(iw_handler) ks_wlan_set_encode_ext,	/* SIOCSIWENCODEEXT */
	(iw_handler) ks_wlan_get_encode_ext,	/* SIOCGIWENCODEEXT */
	(iw_handler) ks_wlan_set_pmksa,	/* SIOCSIWPMKSA */
	(iw_handler) NULL,	/* -- hole -- */
};

/* private_handler */
static const iw_handler ks_wlan_private_handler[] = {
	(iw_handler) NULL,	/*  0 */
	(iw_handler) NULL,	/*  1, used to be: KS_WLAN_GET_DRIVER_VERSION */
	(iw_handler) NULL,	/*  2 */
	(iw_handler) ks_wlan_get_firmware_version,	/*  3 KS_WLAN_GET_FIRM_VERSION */
#ifdef WPS
	(iw_handler) ks_wlan_set_wps_enable,	/*  4 KS_WLAN_SET_WPS_ENABLE  */
	(iw_handler) ks_wlan_get_wps_enable,	/*  5 KS_WLAN_GET_WPS_ENABLE  */
	(iw_handler) ks_wlan_set_wps_probe_req,	/*  6 KS_WLAN_SET_WPS_PROBE_REQ */
#else
	(iw_handler) NULL,	/*  4 */
	(iw_handler) NULL,	/*  5 */
	(iw_handler) NULL,	/*  6 */
#endif /* WPS */

	(iw_handler) ks_wlan_get_eeprom_cksum,	/*  7 KS_WLAN_GET_CONNECT */
	(iw_handler) ks_wlan_set_preamble,	/*  8 KS_WLAN_SET_PREAMBLE */
	(iw_handler) ks_wlan_get_preamble,	/*  9 KS_WLAN_GET_PREAMBLE */
	(iw_handler) ks_wlan_set_powermgt,	/* 10 KS_WLAN_SET_POWER_SAVE */
	(iw_handler) ks_wlan_get_powermgt,	/* 11 KS_WLAN_GET_POWER_SAVE */
	(iw_handler) ks_wlan_set_scan_type,	/* 12 KS_WLAN_SET_SCAN_TYPE */
	(iw_handler) ks_wlan_get_scan_type,	/* 13 KS_WLAN_GET_SCAN_TYPE */
	(iw_handler) ks_wlan_set_rx_gain,	/* 14 KS_WLAN_SET_RX_GAIN */
	(iw_handler) ks_wlan_get_rx_gain,	/* 15 KS_WLAN_GET_RX_GAIN */
	(iw_handler) ks_wlan_hostt,	/* 16 KS_WLAN_HOSTT */
	(iw_handler) NULL,	/* 17 */
	(iw_handler) ks_wlan_set_beacon_lost,	/* 18 KS_WLAN_SET_BECAN_LOST */
	(iw_handler) ks_wlan_get_beacon_lost,	/* 19 KS_WLAN_GET_BECAN_LOST */
	(iw_handler) ks_wlan_set_tx_gain,	/* 20 KS_WLAN_SET_TX_GAIN */
	(iw_handler) ks_wlan_get_tx_gain,	/* 21 KS_WLAN_GET_TX_GAIN */
	(iw_handler) ks_wlan_set_phy_type,	/* 22 KS_WLAN_SET_PHY_TYPE */
	(iw_handler) ks_wlan_get_phy_type,	/* 23 KS_WLAN_GET_PHY_TYPE */
	(iw_handler) ks_wlan_set_cts_mode,	/* 24 KS_WLAN_SET_CTS_MODE */
	(iw_handler) ks_wlan_get_cts_mode,	/* 25 KS_WLAN_GET_CTS_MODE */
	(iw_handler) NULL,	/* 26 */
	(iw_handler) NULL,	/* 27 */
	(iw_handler) ks_wlan_set_sleep_mode,	/* 28 KS_WLAN_SET_SLEEP_MODE */
	(iw_handler) ks_wlan_get_sleep_mode,	/* 29 KS_WLAN_GET_SLEEP_MODE */
	(iw_handler) NULL,	/* 30 */
	(iw_handler) NULL,	/* 31 */
};

static const struct iw_handler_def ks_wlan_handler_def = {
	.num_standard = sizeof(ks_wlan_handler) / sizeof(iw_handler),
	.num_private = sizeof(ks_wlan_private_handler) / sizeof(iw_handler),
	.num_private_args =
	    sizeof(ks_wlan_private_args) / sizeof(struct iw_priv_args),
	.standard = (iw_handler *) ks_wlan_handler,
	.private = (iw_handler *) ks_wlan_private_handler,
	.private_args = (struct iw_priv_args *)ks_wlan_private_args,
	.get_wireless_stats = ks_get_wireless_stats,
};

static int ks_wlan_netdev_ioctl(struct net_device *dev, struct ifreq *rq,
				int cmd)
{
	int rc = 0;
	struct iwreq *wrq = (struct iwreq *)rq;
	switch (cmd) {
	case SIOCIWFIRSTPRIV + 20:	/* KS_WLAN_SET_STOP_REQ */
		rc = ks_wlan_set_stop_request(dev, NULL, &(wrq->u.mode), NULL);
		break;
		// All other calls are currently unsupported
	default:
		rc = -EOPNOTSUPP;
	}

	DPRINTK(5, "return=%d\n", rc);
	return rc;
}

static
struct net_device_stats *ks_wlan_get_stats(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	if (priv->dev_state < DEVICE_STATE_READY) {
		return NULL;	/* not finished initialize */
	}

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
	memcpy(priv->eth_addr, mac_addr->sa_data, ETH_ALEN);

	priv->mac_address_valid = 0;
	hostif_sme_enqueue(priv, SME_MACADDRESS_SET_REQUEST);
	printk(KERN_INFO
	       "ks_wlan: MAC ADDRESS = %02x:%02x:%02x:%02x:%02x:%02x\n",
	       priv->eth_addr[0], priv->eth_addr[1], priv->eth_addr[2],
	       priv->eth_addr[3], priv->eth_addr[4], priv->eth_addr[5]);
	return 0;
}

static
void ks_wlan_tx_timeout(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	DPRINTK(1, "head(%d) tail(%d)!!\n", priv->tx_dev.qhead,
		priv->tx_dev.qtail);
	if (!netif_queue_stopped(dev)) {
		netif_stop_queue(dev);
	}
	priv->nstats.tx_errors++;
	netif_wake_queue(dev);

	return;
}

static
int ks_wlan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);
	int rc = 0;

	DPRINTK(3, "in_interrupt()=%ld\n", in_interrupt());

	if (skb == NULL) {
		printk(KERN_ERR "ks_wlan:  skb == NULL!!!\n");
		return 0;
	}
	if (priv->dev_state < DEVICE_STATE_READY) {
		dev_kfree_skb(skb);
		return 0;	/* not finished initialize */
	}

	if (netif_running(dev))
		netif_stop_queue(dev);

	rc = hostif_data_request(priv, skb);
	netif_trans_update(dev);

	DPRINTK(4, "rc=%d\n", rc);
	if (rc) {
		rc = 0;
	}

	return rc;
}

void send_packet_complete(void *arg1, void *arg2)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)arg1;
	struct sk_buff *packet = (struct sk_buff *)arg2;

	DPRINTK(3, "\n");

	priv->nstats.tx_bytes += packet->len;
	priv->nstats.tx_packets++;

	if (netif_queue_stopped(priv->net_dev))
		netif_wake_queue(priv->net_dev);

	if (packet) {
		dev_kfree_skb(packet);
		packet = NULL;
	}

}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */
static
void ks_wlan_set_multicast_list(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	DPRINTK(4, "\n");
	if (priv->dev_state < DEVICE_STATE_READY) {
		return;	/* not finished initialize */
	}
	hostif_sme_enqueue(priv, SME_MULTICAST_REQUEST);

	return;
}

static
int ks_wlan_open(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	priv->cur_rx = 0;

	if (!priv->mac_address_valid) {
		printk(KERN_ERR "ks_wlan : %s Not READY !!\n", dev->name);
		return -EBUSY;
	} else
		netif_start_queue(dev);

	return 0;
}

static
int ks_wlan_close(struct net_device *dev)
{

	netif_stop_queue(dev);

	DPRINTK(4, "%s: Shutting down ethercard, status was 0x%4.4x.\n",
		dev->name, 0x00);

	return 0;
}

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (3*HZ)
static const unsigned char dummy_addr[] =
    { 0x00, 0x0b, 0xe3, 0x00, 0x00, 0x00 };

static const struct net_device_ops ks_wlan_netdev_ops = {
	.ndo_start_xmit = ks_wlan_start_xmit,
	.ndo_open = ks_wlan_open,
	.ndo_stop = ks_wlan_close,
	.ndo_do_ioctl = ks_wlan_netdev_ioctl,
	.ndo_set_mac_address = ks_wlan_set_mac_address,
	.ndo_get_stats = ks_wlan_get_stats,
	.ndo_tx_timeout = ks_wlan_tx_timeout,
	.ndo_set_rx_mode = ks_wlan_set_multicast_list,
};

int ks_wlan_net_start(struct net_device *dev)
{
	struct ks_wlan_private *priv;
	/* int rc; */

	priv = netdev_priv(dev);
	priv->mac_address_valid = 0;
	priv->need_commit = 0;

	priv->device_open_status = 1;

	/* phy information update timer */
	atomic_set(&update_phyinfo, 0);
	init_timer(&update_phyinfo_timer);
	update_phyinfo_timer.function = ks_wlan_update_phyinfo_timeout;
	update_phyinfo_timer.data = (unsigned long)priv;

	/* dummy address set */
	memcpy(priv->eth_addr, dummy_addr, ETH_ALEN);
	dev->dev_addr[0] = priv->eth_addr[0];
	dev->dev_addr[1] = priv->eth_addr[1];
	dev->dev_addr[2] = priv->eth_addr[2];
	dev->dev_addr[3] = priv->eth_addr[3];
	dev->dev_addr[4] = priv->eth_addr[4];
	dev->dev_addr[5] = priv->eth_addr[5];
	dev->dev_addr[6] = 0x00;
	dev->dev_addr[7] = 0x00;

	/* The ks_wlan-specific entries in the device structure. */
	dev->netdev_ops = &ks_wlan_netdev_ops;
	dev->wireless_handlers = (struct iw_handler_def *)&ks_wlan_handler_def;
	dev->watchdog_timeo = TX_TIMEOUT;

	netif_carrier_off(dev);

	return 0;
}

int ks_wlan_net_stop(struct net_device *dev)
{
	struct ks_wlan_private *priv = netdev_priv(dev);

	int ret = 0;
	priv->device_open_status = 0;
	del_timer_sync(&update_phyinfo_timer);

	if (netif_running(dev))
		netif_stop_queue(dev);

	return ret;
}

int ks_wlan_reset(struct net_device *dev)
{
	return 0;
}
