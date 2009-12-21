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
/*  Module Name : wrap_ev.c                                            */
/*                                                                      */
/*  Abstract                                                            */
/*     This module contains wrapper functions for events                */
/*                                                                      */
/*  NOTES                                                               */
/*     Platform dependent.                                              */
/*                                                                      */
/************************************************************************/

#include "oal_dt.h"
#include "usbdrv.h"

#include <linux/netlink.h>
#include <net/iw_handler.h>


/***** Management *****/
u16_t zfLnxAuthNotify(zdev_t *dev, u16_t *macAddr)
{
	return 0;
}

u16_t zfLnxAsocNotify(zdev_t *dev, u16_t *macAddr, u8_t *body, u16_t bodySize,
			 u16_t port)
{
/* #ifdef ZM_HOSTAPD_SUPPORT	*/
	struct usbdrv_private *macp = dev->ml_priv;
	union iwreq_data wreq;
	u8_t *addr = (u8_t *) macAddr;
	u16_t i, j;

	memset(&wreq, 0, sizeof(wreq));
	memcpy(wreq.addr.sa_data, macAddr, ETH_ALEN);
	wreq.addr.sa_family = ARPHRD_ETHER;
	printk(KERN_DEBUG "join_event of MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	for (i = 0; i < ZM_OAL_MAX_STA_SUPPORT; i++) {
		for (j = 0; j < IEEE80211_ADDR_LEN; j++) {
			if ((macp->stawpaie[i].wpa_macaddr[j] != 0) &&
				(macp->stawpaie[i].wpa_macaddr[j] != addr[j]))
				break;
		}
		if (j == 6)
			break;
	}
	if (i < ZM_OAL_MAX_STA_SUPPORT) {
		/*
		 * printk("zfwAsocNotify - store wpa ie in macp,
		 * 	index = %d\n", i);
		 */
		memcpy(macp->stawpaie[i].wpa_macaddr, macAddr,
			IEEE80211_ADDR_LEN);
		memcpy(macp->stawpaie[i].wpa_ie, body, bodySize);
	}
	/*
	 * if(macp->cardSetting.BssType == INFRASTRUCTURE_BSS) {
	 * wireless_send_event(macp->device, SIOCGIWSCAN, &wreq, NULL);
	 *	wireless_send_event(macp->device, SIOCGIWAP, &wreq, NULL);
	 * }
	 * else if(macp->cardSetting.BssType == AP_BSS) {
	 *	if (port == 0)
	 *	{
	 */
			wireless_send_event(dev, IWEVREGISTERED, &wreq, NULL);
	/*
	 *	}
	 *	else
	 *	{
	 *		 Check whether the VAP device is valid
	 *		if (vap[port].dev != NULL)
	 *		{
	 *			wireless_send_event(vap[port].dev,
	 *					IWEVREGISTERED, &wreq, NULL);
	 *		}
	 *		else
	 *		{
	 *			printk(KERN_ERR "Can' find a valid VAP device,
	 *				 port: %d\n", port);
	 *		}
	 *	}
	 * }
	 */
/* #endif	*/

	return 0;
}


/* Notification that a STA is disassociated from AP */
/* AP mode only */
u16_t zfLnxDisAsocNotify(zdev_t *dev, u8_t *macAddr, u16_t port)
{
	union iwreq_data wreq;
	u8_t *addr = (u8_t *) macAddr;

	memset(&wreq, 0, sizeof(wreq));
	memcpy(wreq.addr.sa_data, macAddr, ETH_ALEN);
	wreq.addr.sa_family = ARPHRD_ETHER;
	printk(KERN_DEBUG "zfwDisAsocNotify(), MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);


	return 0;
}

/* Notification that a STA is connect to AP */
/* AP mode only */
u16_t zfLnxApConnectNotify(zdev_t *dev, u8_t *macAddr, u16_t port)
{
	union iwreq_data wreq;
	u8_t *addr = (u8_t *) macAddr;

	memset(&wreq, 0, sizeof(wreq));
	memcpy(wreq.addr.sa_data, macAddr, ETH_ALEN);
	wreq.addr.sa_family = ARPHRD_ETHER;
	printk(KERN_DEBUG "zfwApConnectNotify(), MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);


	return 0;
}



void zfLnxConnectNotify(zdev_t *dev, u16_t status, u16_t *bssid)
{
	union iwreq_data wreq;
	u8_t *addr = (u8_t *) bssid;
	struct usbdrv_private *macp = dev->ml_priv;

	if (bssid != NULL) {
		memset(&wreq, 0, sizeof(wreq));
		if (status == ZM_STATUS_MEDIA_CONNECT)
			memcpy(wreq.addr.sa_data, bssid, ETH_ALEN);
		wreq.addr.sa_family = ARPHRD_ETHER;

		if (status == ZM_STATUS_MEDIA_CONNECT) {
#ifdef ZM_CONFIG_BIG_ENDIAN
			printk(KERN_DEBUG "Connected to AP, MAC:"
				"%02x:%02x:%02x:%02x:%02x:%02x\n",
				addr[1], addr[0], addr[3], addr[2],
				addr[5], addr[4]);
#else
			printk(KERN_DEBUG "Connected to AP, MAC:"
				 "%02x:%02x:%02x:%02x:%02x:%02x\n",
				addr[0], addr[1], addr[2], addr[3],
				addr[4], addr[5]);
#endif

			netif_start_queue(dev);
		} else if ((status == ZM_STATUS_MEDIA_DISCONNECT) ||
			    (status == ZM_STATUS_MEDIA_DISABLED) ||
			    (status == ZM_STATUS_MEDIA_CONNECTION_DISABLED) ||
			    (status == ZM_STATUS_MEDIA_CONNECTION_RESET) ||
			    (status == ZM_STATUS_MEDIA_RESET) ||
			    (status == ZM_STATUS_MEDIA_DISCONNECT_DEAUTH) ||
			    (status == ZM_STATUS_MEDIA_DISCONNECT_DISASOC) ||
			   (status == ZM_STATUS_MEDIA_DISCONNECT_BEACON_MISS) ||
			    (status == ZM_STATUS_MEDIA_DISCONNECT_NOT_FOUND) ||
			    (status == ZM_STATUS_MEDIA_DISCONNECT_TIMEOUT)) {
			printk(KERN_DEBUG "Disconnection Notify\n");

			netif_stop_queue(dev);
		}

	/* Save the connected status */
	macp->adapterState = status;

		if (zfiWlanQueryWlanMode(dev) == ZM_MODE_INFRASTRUCTURE) {
			/*wireless_send_event(dev, SIOCGIWSCAN, &wreq, NULL);*/
			wireless_send_event(dev, SIOCGIWAP, &wreq, NULL);
		} else if (zfiWlanQueryWlanMode(dev) == ZM_MODE_AP) {
			/*
			 * if (port == 0)
			 * {
			 *	wireless_send_event(dev, IWEVREGISTERED,
			 *				 &wreq, NULL);
			 * }
			 * else
			 * {
			 *	Check whether the VAP device is valid
			 *	if (vap[port].dev != NULL)
			 *	{
			 *		wireless_send_event(vap[port].dev,
		 *				 IWEVREGISTERED, &wreq, NULL);
			 *	}
			 *	else
			 *	{
			 *		printk(KERN_ERR "Can' find a valid VAP"
			 *			" device, port: %d\n", port);
			 *	}
			 * }
			*/
		}
	}
	/* return 0;	*/
}

void zfLnxScanNotify(zdev_t *dev, struct zsScanResult *result)
{
	return;
}

void zfLnxStatisticsNotify(zdev_t *dev, struct zsStastics *result)
{
	return;
}

/* void zfwMicFailureNotify(zdev_t *dev, u8_t *message, u16_t event) */
void zfLnxMicFailureNotify(zdev_t *dev, u16_t *addr, u16_t status)
{
	static const char *tag = "MLME-MICHAELMICFAILURE.indication";
	union iwreq_data wrqu;
	char buf[128];

	/* TODO: needed parameters: count, type, src address */
	/*
	 * snprintf(buf, sizeof(buf), "%s(%scast addr=%s)", tag,
	 *	(status == ZM_MIC_GROUP_ERROR) ?  "broad" : "uni",
	 *	ether_sprintf((u8_t *)addr));
	 */

	if (zfiWlanQueryWlanMode(dev) == ZM_MODE_INFRASTRUCTURE)
		strcpy(buf, tag);

	memset(&wrqu, 0, sizeof(wrqu));
	wrqu.data.length = strlen(buf);
	wireless_send_event(dev, IWEVCUSTOM, &wrqu, buf);
}


void zfLnxApMicFailureNotify(zdev_t *dev, u8_t *addr, zbuf_t *buf)
{
	union iwreq_data wreq;

	memset(&wreq, 0, sizeof(wreq));
	memcpy(wreq.addr.sa_data, addr, ETH_ALEN);
	wreq.addr.sa_family = ARPHRD_ETHER;
	printk(KERN_DEBUG "zfwApMicFailureNotify(), "
			"MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
			addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	return;
}
/*
 * status = 0  => partner lost
 *		= 1  => partner alive
 * void zfwIbssPartnerNotify(zdev_t* dev, u8_t status)
 */
void zfLnxIbssPartnerNotify(zdev_t *dev, u16_t status,
				struct zsPartnerNotifyEvent *event)
{
}

void zfLnxMacAddressNotify(zdev_t *dev, u8_t *addr)
{
	dev->dev_addr[0] = addr[0];
	dev->dev_addr[1] = addr[1];
	dev->dev_addr[2] = addr[2];
	dev->dev_addr[3] = addr[3];
	dev->dev_addr[4] = addr[4];
	dev->dev_addr[5] = addr[5];
}

void zfLnxSendCompleteIndication(zdev_t *dev, zbuf_t *buf)
{
}


void zfLnxRestoreBufData(zdev_t *dev, zbuf_t *buf)
{

}
/* Leave an empty line below to remove warning message on some compiler */
