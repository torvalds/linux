// SPDX-License-Identifier: GPL-2.0
/*
 *   Driver for KeyStream wireless LAN cards.
 *
 *   Copyright (C) 2005-2008 KeyStream Corp.
 *   Copyright (C) 2009 Renesas Technology Corp.
 */

#include <linux/if_arp.h>
#include <net/iw_handler.h>
#include <uapi/linux/llc.h>
#include "eap_packet.h"
#include "ks_wlan.h"
#include "michael_mic.h"
#include "ks_hostif.h"

static inline void inc_smeqhead(struct ks_wlan_private *priv)
{
	priv->sme_i.qhead = (priv->sme_i.qhead + 1) % SME_EVENT_BUFF_SIZE;
}

static inline void inc_smeqtail(struct ks_wlan_private *priv)
{
	priv->sme_i.qtail = (priv->sme_i.qtail + 1) % SME_EVENT_BUFF_SIZE;
}

static inline unsigned int cnt_smeqbody(struct ks_wlan_private *priv)
{
	unsigned int sme_cnt = priv->sme_i.qtail - priv->sme_i.qhead;

	return (sme_cnt + SME_EVENT_BUFF_SIZE) % SME_EVENT_BUFF_SIZE;
}

static inline u8 get_byte(struct ks_wlan_private *priv)
{
	u8 data;

	data = *(priv->rxp)++;
	/* length check in advance ! */
	--(priv->rx_size);
	return data;
}

static inline u16 get_word(struct ks_wlan_private *priv)
{
	u16 data;

	data = (get_byte(priv) & 0xff);
	data |= ((get_byte(priv) << 8) & 0xff00);
	return data;
}

static inline u32 get_dword(struct ks_wlan_private *priv)
{
	u32 data;

	data = (get_byte(priv) & 0xff);
	data |= ((get_byte(priv) << 8) & 0x0000ff00);
	data |= ((get_byte(priv) << 16) & 0x00ff0000);
	data |= ((get_byte(priv) << 24) & 0xff000000);
	return data;
}

static void ks_wlan_hw_wakeup_task(struct work_struct *work)
{
	struct ks_wlan_private *priv;
	int ps_status;
	long time_left;

	priv = container_of(work, struct ks_wlan_private, wakeup_work);
	ps_status = atomic_read(&priv->psstatus.status);

	if (ps_status == PS_SNOOZE) {
		ks_wlan_hw_wakeup_request(priv);
		time_left = wait_for_completion_interruptible_timeout(
				&priv->psstatus.wakeup_wait,
				msecs_to_jiffies(20));
		if (time_left <= 0) {
			netdev_dbg(priv->net_dev, "wake up timeout or interrupted !!!\n");
			schedule_work(&priv->wakeup_work);
			return;
		}
	}

	/* power save */
	if (atomic_read(&priv->sme_task.count) > 0)
		tasklet_enable(&priv->sme_task);
}

static void ks_wlan_do_power_save(struct ks_wlan_private *priv)
{
	if (is_connect_status(priv->connect_status))
		hostif_sme_enqueue(priv, SME_POW_MNGMT_REQUEST);
	else
		priv->dev_state = DEVICE_STATE_READY;
}

static
int get_current_ap(struct ks_wlan_private *priv, struct link_ap_info *ap_info)
{
	struct local_ap *ap;
	union iwreq_data wrqu;
	struct net_device *netdev = priv->net_dev;
	u8 size;

	ap = &priv->current_ap;

	if (is_disconnect_status(priv->connect_status)) {
		memset(ap, 0, sizeof(struct local_ap));
		return -EPERM;
	}

	memcpy(ap->bssid, ap_info->bssid, ETH_ALEN);
	memcpy(ap->ssid.body, priv->reg.ssid.body,
	       priv->reg.ssid.size);
	ap->ssid.size = priv->reg.ssid.size;
	memcpy(ap->rate_set.body, ap_info->rate_set.body,
	       ap_info->rate_set.size);
	ap->rate_set.size = ap_info->rate_set.size;
	if (ap_info->ext_rate_set.size != 0) {
		memcpy(&ap->rate_set.body[ap->rate_set.size],
		       ap_info->ext_rate_set.body,
		       ap_info->ext_rate_set.size);
		ap->rate_set.size += ap_info->ext_rate_set.size;
	}
	ap->channel = ap_info->ds_parameter.channel;
	ap->rssi = ap_info->rssi;
	ap->sq = ap_info->sq;
	ap->noise = ap_info->noise;
	ap->capability = le16_to_cpu(ap_info->capability);
	size = (ap_info->rsn.size <= RSN_IE_BODY_MAX) ?
		ap_info->rsn.size : RSN_IE_BODY_MAX;
	if ((ap_info->rsn_mode & RSN_MODE_WPA2) &&
	    (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)) {
		ap->rsn_ie.id = 0x30;
		ap->rsn_ie.size = size;
		memcpy(ap->rsn_ie.body, ap_info->rsn.body, size);
	} else if ((ap_info->rsn_mode & RSN_MODE_WPA) &&
		   (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA)) {
		ap->wpa_ie.id = 0xdd;
		ap->wpa_ie.size = size;
		memcpy(ap->wpa_ie.body, ap_info->rsn.body, size);
	} else {
		ap->rsn_ie.id = 0;
		ap->rsn_ie.size = 0;
		ap->wpa_ie.id = 0;
		ap->wpa_ie.size = 0;
	}

	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (is_connect_status(priv->connect_status)) {
		memcpy(wrqu.ap_addr.sa_data,
		       priv->current_ap.bssid, ETH_ALEN);
		netdev_dbg(priv->net_dev,
			   "IWEVENT: connect bssid=%pM\n", wrqu.ap_addr.sa_data);
		wireless_send_event(netdev, SIOCGIWAP, &wrqu, NULL);
	}
	netdev_dbg(priv->net_dev, "Link AP\n"
		   "- bssid=%02X:%02X:%02X:%02X:%02X:%02X\n"
		   "- essid=%s\n"
		   "- rate_set=%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n"
		   "- channel=%d\n"
		   "- rssi=%d\n"
		   "- sq=%d\n"
		   "- capability=%04X\n"
		   "- rsn.mode=%d\n"
		   "- rsn.size=%d\n"
		   "- ext_rate_set_size=%d\n"
		   "- rate_set_size=%d\n",
		   ap->bssid[0], ap->bssid[1], ap->bssid[2],
		   ap->bssid[3], ap->bssid[4], ap->bssid[5],
		   &(ap->ssid.body[0]),
		   ap->rate_set.body[0], ap->rate_set.body[1],
		   ap->rate_set.body[2], ap->rate_set.body[3],
		   ap->rate_set.body[4], ap->rate_set.body[5],
		   ap->rate_set.body[6], ap->rate_set.body[7],
		   ap->channel, ap->rssi, ap->sq, ap->capability,
		   ap_info->rsn_mode, ap_info->rsn.size,
		   ap_info->ext_rate_set.size, ap_info->rate_set.size);

	return 0;
}

static u8 read_ie(unsigned char *bp, u8 max, u8 *body)
{
	u8 size = (*(bp + 1) <= max) ? *(bp + 1) : max;

	memcpy(body, bp + 2, size);
	return size;
}


static
int get_ap_information(struct ks_wlan_private *priv, struct ap_info *ap_info,
		       struct local_ap *ap)
{
	unsigned char *bp;
	int bsize, offset;

	memset(ap, 0, sizeof(struct local_ap));

	memcpy(ap->bssid, ap_info->bssid, ETH_ALEN);
	ap->rssi = ap_info->rssi;
	ap->sq = ap_info->sq;
	ap->noise = ap_info->noise;
	ap->capability = le16_to_cpu(ap_info->capability);
	ap->channel = ap_info->ch_info;

	bp = ap_info->body;
	bsize = le16_to_cpu(ap_info->body_size);
	offset = 0;

	while (bsize > offset) {
		switch (*bp) { /* Information Element ID */
		case WLAN_EID_SSID:
			ap->ssid.size = read_ie(bp, IEEE80211_MAX_SSID_LEN,
						ap->ssid.body);
			break;
		case WLAN_EID_SUPP_RATES:
		case WLAN_EID_EXT_SUPP_RATES:
			if ((*(bp + 1) + ap->rate_set.size) <=
			    RATE_SET_MAX_SIZE) {
				memcpy(&ap->rate_set.body[ap->rate_set.size],
				       bp + 2, *(bp + 1));
				ap->rate_set.size += *(bp + 1);
			} else {
				memcpy(&ap->rate_set.body[ap->rate_set.size],
				       bp + 2,
				       RATE_SET_MAX_SIZE - ap->rate_set.size);
				ap->rate_set.size +=
				    (RATE_SET_MAX_SIZE - ap->rate_set.size);
			}
			break;
		case WLAN_EID_DS_PARAMS:
			break;
		case WLAN_EID_RSN:
			ap->rsn_ie.id = *bp;
			ap->rsn_ie.size = read_ie(bp, RSN_IE_BODY_MAX,
						  ap->rsn_ie.body);
			break;
		case WLAN_EID_VENDOR_SPECIFIC: /* WPA */
			/* WPA OUI check */
			if (memcmp(bp + 2, CIPHER_ID_WPA_WEP40, 4) == 0) {
				ap->wpa_ie.id = *bp;
				ap->wpa_ie.size = read_ie(bp, RSN_IE_BODY_MAX,
							  ap->wpa_ie.body);
			}
			break;

		case WLAN_EID_FH_PARAMS:
		case WLAN_EID_CF_PARAMS:
		case WLAN_EID_TIM:
		case WLAN_EID_IBSS_PARAMS:
		case WLAN_EID_COUNTRY:
		case WLAN_EID_ERP_INFO:
			break;
		default:
			netdev_err(priv->net_dev, "unknown Element ID=%d\n", *bp);
			break;
		}

		offset += 2;	/* id & size field */
		offset += *(bp + 1);	/* +size offset */
		bp += (*(bp + 1) + 2);	/* pointer update */
	}

	return 0;
}

static
int hostif_data_indication_wpa(struct ks_wlan_private *priv,
			       unsigned short auth_type)
{
	struct ether_hdr *eth_hdr;
	unsigned short eth_proto;
	unsigned char recv_mic[8];
	char buf[128];
	unsigned long now;
	struct mic_failure *mic_failure;
	struct michael_mic michael_mic;
	union iwreq_data wrqu;
	unsigned int key_index = auth_type - 1;
	struct wpa_key *key = &priv->wpa.key[key_index];

	eth_hdr = (struct ether_hdr *)(priv->rxp);
	eth_proto = ntohs(eth_hdr->h_proto);

	if (eth_hdr->h_dest_snap != eth_hdr->h_source_snap) {
		netdev_err(priv->net_dev, "invalid data format\n");
		priv->nstats.rx_errors++;
		return -EINVAL;
	}
	if (((auth_type == TYPE_PMK1 &&
	      priv->wpa.pairwise_suite == IW_AUTH_CIPHER_TKIP) ||
	     (auth_type == TYPE_GMK1 &&
	      priv->wpa.group_suite == IW_AUTH_CIPHER_TKIP) ||
	     (auth_type == TYPE_GMK2 &&
	      priv->wpa.group_suite == IW_AUTH_CIPHER_TKIP)) &&
	    key->key_len) {
		netdev_dbg(priv->net_dev, "TKIP: protocol=%04X: size=%u\n",
			   eth_proto, priv->rx_size);
		/* MIC save */
		memcpy(&recv_mic[0], (priv->rxp) + ((priv->rx_size) - 8), 8);
		priv->rx_size = priv->rx_size - 8;
		if (auth_type > 0 && auth_type < 4) {	/* auth_type check */
			michael_mic_function(&michael_mic, key->rx_mic_key,
					     priv->rxp, priv->rx_size,
					     0,	michael_mic.result);
		}
		if (memcmp(michael_mic.result, recv_mic, 8) != 0) {
			now = jiffies;
			mic_failure = &priv->wpa.mic_failure;
			/* MIC FAILURE */
			if (mic_failure->last_failure_time &&
			    (now - mic_failure->last_failure_time) / HZ >= 60) {
				mic_failure->failure = 0;
			}
			netdev_err(priv->net_dev, "MIC FAILURE\n");
			if (mic_failure->failure == 0) {
				mic_failure->failure = 1;
				mic_failure->counter = 0;
			} else if (mic_failure->failure == 1) {
				mic_failure->failure = 2;
				mic_failure->counter =
					(uint16_t)((now - mic_failure->last_failure_time) / HZ);
				if (!mic_failure->counter)	/*  range 1-60 */
					mic_failure->counter = 1;
			}
			priv->wpa.mic_failure.last_failure_time = now;

			/*  needed parameters: count, keyid, key type, TSC */
			sprintf(buf,
				"MLME-MICHAELMICFAILURE.indication(keyid=%d %scast addr=%pM)",
				key_index,
				eth_hdr->h_dest[0] & 0x01 ? "broad" : "uni",
				eth_hdr->h_source);
			memset(&wrqu, 0, sizeof(wrqu));
			wrqu.data.length = strlen(buf);
			wireless_send_event(priv->net_dev, IWEVCUSTOM, &wrqu,
					    buf);
			return -EINVAL;
		}
	}
	return 0;
}

static
void hostif_data_indication(struct ks_wlan_private *priv)
{
	unsigned int rx_ind_size;	/* indicate data size */
	struct sk_buff *skb;
	unsigned short auth_type;
	unsigned char temp[256];
	struct ether_hdr *eth_hdr;
	unsigned short eth_proto;
	struct ieee802_1x_hdr *aa1x_hdr;
	size_t size;
	int ret;

	/* min length check */
	if (priv->rx_size <= ETH_HLEN) {
		priv->nstats.rx_errors++;
		return;
	}

	auth_type = get_word(priv);	/* AuthType */
	get_word(priv);	/* Reserve Area */

	eth_hdr = (struct ether_hdr *)(priv->rxp);
	eth_proto = ntohs(eth_hdr->h_proto);

	/* source address check */
	if (ether_addr_equal(&priv->eth_addr[0], eth_hdr->h_source)) {
		netdev_err(priv->net_dev, "invalid : source is own mac address !!\n");
		netdev_err(priv->net_dev,
			   "eth_hdrernet->h_dest=%02X:%02X:%02X:%02X:%02X:%02X\n",
			   eth_hdr->h_source[0], eth_hdr->h_source[1],
			   eth_hdr->h_source[2], eth_hdr->h_source[3],
			   eth_hdr->h_source[4], eth_hdr->h_source[5]);
		priv->nstats.rx_errors++;
		return;
	}

	/*  for WPA */
	if (auth_type != TYPE_DATA && priv->wpa.rsn_enabled) {
		ret = hostif_data_indication_wpa(priv, auth_type);
		if (ret)
			return;
	}

	if ((priv->connect_status & FORCE_DISCONNECT) ||
	    priv->wpa.mic_failure.failure == 2) {
		return;
	}

	/* check 13th byte at rx data */
	switch (*(priv->rxp + 12)) {
	case LLC_SAP_SNAP:
		rx_ind_size = priv->rx_size - 6;
		skb = dev_alloc_skb(rx_ind_size);
		if (!skb) {
			priv->nstats.rx_dropped++;
			return;
		}
		netdev_dbg(priv->net_dev, "SNAP, rx_ind_size = %d\n",
			   rx_ind_size);

		size = ETH_ALEN * 2;
		skb_put_data(skb, priv->rxp, size);

		/* (SNAP+UI..) skip */

		size = rx_ind_size - (ETH_ALEN * 2);
		skb_put_data(skb, &eth_hdr->h_proto, size);

		aa1x_hdr = (struct ieee802_1x_hdr *)(priv->rxp + ETHER_HDR_SIZE);
		break;
	case LLC_SAP_NETBEUI:
		rx_ind_size = (priv->rx_size + 2);
		skb = dev_alloc_skb(rx_ind_size);
		if (!skb) {
			priv->nstats.rx_dropped++;
			return;
		}
		netdev_dbg(priv->net_dev, "NETBEUI/NetBIOS rx_ind_size=%d\n",
			   rx_ind_size);

		/* 8802/FDDI MAC copy */
		skb_put_data(skb, priv->rxp, 12);

		/* NETBEUI size add */
		temp[0] = (((rx_ind_size - 12) >> 8) & 0xff);
		temp[1] = ((rx_ind_size - 12) & 0xff);
		skb_put_data(skb, temp, 2);

		/* copy after Type */
		skb_put_data(skb, priv->rxp + 12, rx_ind_size - 14);

		aa1x_hdr = (struct ieee802_1x_hdr *)(priv->rxp + 14);
		break;
	default:	/* other rx data */
		netdev_err(priv->net_dev, "invalid data format\n");
		priv->nstats.rx_errors++;
		return;
	}

	if (aa1x_hdr->type == IEEE802_1X_TYPE_EAPOL_KEY &&
	    priv->wpa.rsn_enabled)
		atomic_set(&priv->psstatus.snooze_guard, 1);

	/* rx indication */
	skb->dev = priv->net_dev;
	skb->protocol = eth_type_trans(skb, skb->dev);
	priv->nstats.rx_packets++;
	priv->nstats.rx_bytes += rx_ind_size;
	netif_rx(skb);
}

static
void hostif_mib_get_confirm(struct ks_wlan_private *priv)
{
	struct net_device *dev = priv->net_dev;
	u32 mib_status;
	u32 mib_attribute;
	u16 mib_val_size;
	u16 mib_val_type;

	mib_status = get_dword(priv);	/* MIB status */
	mib_attribute = get_dword(priv);	/* MIB atttibute */
	mib_val_size = get_word(priv);	/* MIB value size */
	mib_val_type = get_word(priv);	/* MIB value type */

	if (mib_status) {
		netdev_err(priv->net_dev, "attribute=%08X, status=%08X\n",
			   mib_attribute, mib_status);
		return;
	}

	switch (mib_attribute) {
	case DOT11_MAC_ADDRESS:
		hostif_sme_enqueue(priv, SME_GET_MAC_ADDRESS);
		ether_addr_copy(priv->eth_addr, priv->rxp);
		priv->mac_address_valid = true;
		ether_addr_copy(dev->dev_addr, priv->eth_addr);
		netdev_info(dev, "MAC ADDRESS = %pM\n", priv->eth_addr);
		break;
	case DOT11_PRODUCT_VERSION:
		priv->version_size = priv->rx_size;
		memcpy(priv->firmware_version, priv->rxp, priv->rx_size);
		priv->firmware_version[priv->rx_size] = '\0';
		netdev_info(dev, "firmware ver. = %s\n",
			    priv->firmware_version);
		hostif_sme_enqueue(priv, SME_GET_PRODUCT_VERSION);
		/* wake_up_interruptible_all(&priv->confirm_wait); */
		complete(&priv->confirm_wait);
		break;
	case LOCAL_GAIN:
		memcpy(&priv->gain, priv->rxp, sizeof(priv->gain));
		netdev_dbg(priv->net_dev, "tx_mode=%d, rx_mode=%d, tx_gain=%d, rx_gain=%d\n",
			   priv->gain.tx_mode, priv->gain.rx_mode,
			   priv->gain.tx_gain, priv->gain.rx_gain);
		break;
	case LOCAL_EEPROM_SUM:
		memcpy(&priv->eeprom_sum, priv->rxp, sizeof(priv->eeprom_sum));
		if (priv->eeprom_sum.type == 0) {
			priv->eeprom_checksum = EEPROM_CHECKSUM_NONE;
		} else if (priv->eeprom_sum.type == 1) {
			if (priv->eeprom_sum.result == 0) {
				priv->eeprom_checksum = EEPROM_NG;
				netdev_info(dev, "LOCAL_EEPROM_SUM NG\n");
			} else if (priv->eeprom_sum.result == 1) {
				priv->eeprom_checksum = EEPROM_OK;
			}
		} else {
			netdev_err(dev, "LOCAL_EEPROM_SUM error!\n");
		}
		break;
	default:
		netdev_err(priv->net_dev, "mib_attribute=%08x\n",
			   (unsigned int)mib_attribute);
		break;
	}
}

static
void hostif_mib_set_confirm(struct ks_wlan_private *priv)
{
	u32 mib_status;	/* +04 MIB Status */
	u32 mib_attribute;	/* +08 MIB attribute */

	mib_status = get_dword(priv);	/* MIB Status */
	mib_attribute = get_dword(priv);	/* MIB attribute */

	if (mib_status) {
		/* in case of error */
		netdev_err(priv->net_dev, "error :: attribute=%08X, status=%08X\n",
			   mib_attribute, mib_status);
	}

	switch (mib_attribute) {
	case DOT11_RTS_THRESHOLD:
		hostif_sme_enqueue(priv, SME_RTS_THRESHOLD_CONFIRM);
		break;
	case DOT11_FRAGMENTATION_THRESHOLD:
		hostif_sme_enqueue(priv, SME_FRAGMENTATION_THRESHOLD_CONFIRM);
		break;
	case DOT11_WEP_DEFAULT_KEY_ID:
		if (!priv->wpa.wpa_enabled)
			hostif_sme_enqueue(priv, SME_WEP_INDEX_CONFIRM);
		break;
	case DOT11_WEP_DEFAULT_KEY_VALUE1:
		if (priv->wpa.rsn_enabled)
			hostif_sme_enqueue(priv, SME_SET_PMK_TSC);
		else
			hostif_sme_enqueue(priv, SME_WEP_KEY1_CONFIRM);
		break;
	case DOT11_WEP_DEFAULT_KEY_VALUE2:
		if (priv->wpa.rsn_enabled)
			hostif_sme_enqueue(priv, SME_SET_GMK1_TSC);
		else
			hostif_sme_enqueue(priv, SME_WEP_KEY2_CONFIRM);
		break;
	case DOT11_WEP_DEFAULT_KEY_VALUE3:
		if (priv->wpa.rsn_enabled)
			hostif_sme_enqueue(priv, SME_SET_GMK2_TSC);
		else
			hostif_sme_enqueue(priv, SME_WEP_KEY3_CONFIRM);
		break;
	case DOT11_WEP_DEFAULT_KEY_VALUE4:
		if (!priv->wpa.rsn_enabled)
			hostif_sme_enqueue(priv, SME_WEP_KEY4_CONFIRM);
		break;
	case DOT11_PRIVACY_INVOKED:
		if (!priv->wpa.rsn_enabled)
			hostif_sme_enqueue(priv, SME_WEP_FLAG_CONFIRM);
		break;
	case DOT11_RSN_ENABLED:
		hostif_sme_enqueue(priv, SME_RSN_ENABLED_CONFIRM);
		break;
	case LOCAL_RSN_MODE:
		hostif_sme_enqueue(priv, SME_RSN_MODE_CONFIRM);
		break;
	case LOCAL_MULTICAST_ADDRESS:
		hostif_sme_enqueue(priv, SME_MULTICAST_REQUEST);
		break;
	case LOCAL_MULTICAST_FILTER:
		hostif_sme_enqueue(priv, SME_MULTICAST_CONFIRM);
		break;
	case LOCAL_CURRENTADDRESS:
		priv->mac_address_valid = true;
		break;
	case DOT11_RSN_CONFIG_MULTICAST_CIPHER:
		hostif_sme_enqueue(priv, SME_RSN_MCAST_CONFIRM);
		break;
	case DOT11_RSN_CONFIG_UNICAST_CIPHER:
		hostif_sme_enqueue(priv, SME_RSN_UCAST_CONFIRM);
		break;
	case DOT11_RSN_CONFIG_AUTH_SUITE:
		hostif_sme_enqueue(priv, SME_RSN_AUTH_CONFIRM);
		break;
	case DOT11_GMK1_TSC:
		if (atomic_read(&priv->psstatus.snooze_guard))
			atomic_set(&priv->psstatus.snooze_guard, 0);
		break;
	case DOT11_GMK2_TSC:
		if (atomic_read(&priv->psstatus.snooze_guard))
			atomic_set(&priv->psstatus.snooze_guard, 0);
		break;
	case DOT11_PMK_TSC:
	case LOCAL_PMK:
	case LOCAL_GAIN:
	case LOCAL_WPS_ENABLE:
	case LOCAL_WPS_PROBE_REQ:
	case LOCAL_REGION:
	default:
		break;
	}
}

static
void hostif_power_mgmt_confirm(struct ks_wlan_private *priv)
{
	if (priv->reg.power_mgmt > POWER_MGMT_ACTIVE &&
	    priv->reg.operation_mode == MODE_INFRASTRUCTURE) {
		atomic_set(&priv->psstatus.confirm_wait, 0);
		priv->dev_state = DEVICE_STATE_SLEEP;
		ks_wlan_hw_power_save(priv);
	} else {
		priv->dev_state = DEVICE_STATE_READY;
	}
}

static
void hostif_sleep_confirm(struct ks_wlan_private *priv)
{
	atomic_set(&priv->sleepstatus.doze_request, 1);
	queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
}

static
void hostif_start_confirm(struct ks_wlan_private *priv)
{
	union iwreq_data wrqu;

	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (is_connect_status(priv->connect_status)) {
		eth_zero_addr(wrqu.ap_addr.sa_data);
		wireless_send_event(priv->net_dev, SIOCGIWAP, &wrqu, NULL);
	}
	netdev_dbg(priv->net_dev, " scan_ind_count=%d\n", priv->scan_ind_count);
	hostif_sme_enqueue(priv, SME_START_CONFIRM);
}

static
void hostif_connect_indication(struct ks_wlan_private *priv)
{
	unsigned short connect_code;
	unsigned int tmp = 0;
	unsigned int old_status = priv->connect_status;
	struct net_device *netdev = priv->net_dev;
	union iwreq_data wrqu0;

	connect_code = get_word(priv);

	switch (connect_code) {
	case RESULT_CONNECT:
		if (!(priv->connect_status & FORCE_DISCONNECT))
			netif_carrier_on(netdev);
		tmp = FORCE_DISCONNECT & priv->connect_status;
		priv->connect_status = tmp + CONNECT_STATUS;
		break;
	case RESULT_DISCONNECT:
		netif_carrier_off(netdev);
		tmp = FORCE_DISCONNECT & priv->connect_status;
		priv->connect_status = tmp + DISCONNECT_STATUS;
		break;
	default:
		netdev_dbg(priv->net_dev, "unknown connect_code=%d :: scan_ind_count=%d\n",
			   connect_code, priv->scan_ind_count);
		netif_carrier_off(netdev);
		tmp = FORCE_DISCONNECT & priv->connect_status;
		priv->connect_status = tmp + DISCONNECT_STATUS;
		break;
	}

	get_current_ap(priv, (struct link_ap_info *)priv->rxp);
	if (is_connect_status(priv->connect_status) &&
	    is_disconnect_status(old_status)) {
		/* for power save */
		atomic_set(&priv->psstatus.snooze_guard, 0);
		atomic_set(&priv->psstatus.confirm_wait, 0);
	}
	ks_wlan_do_power_save(priv);

	wrqu0.data.length = 0;
	wrqu0.data.flags = 0;
	wrqu0.ap_addr.sa_family = ARPHRD_ETHER;
	if (is_disconnect_status(priv->connect_status) &&
	    is_connect_status(old_status)) {
		eth_zero_addr(wrqu0.ap_addr.sa_data);
		netdev_dbg(priv->net_dev, "disconnect :: scan_ind_count=%d\n",
			   priv->scan_ind_count);
		wireless_send_event(netdev, SIOCGIWAP, &wrqu0, NULL);
	}
	priv->scan_ind_count = 0;
}

static
void hostif_scan_indication(struct ks_wlan_private *priv)
{
	int i;
	struct ap_info *ap_info;

	netdev_dbg(priv->net_dev, "scan_ind_count = %d\n", priv->scan_ind_count);
	ap_info = (struct ap_info *)(priv->rxp);

	if (priv->scan_ind_count) {
		/* bssid check */
		for (i = 0; i < priv->aplist.size; i++) {
			u8 *bssid = priv->aplist.ap[i].bssid;

			if (ether_addr_equal(ap_info->bssid, bssid))
				continue;

			if (ap_info->frame_type == IEEE80211_STYPE_PROBE_RESP)
				get_ap_information(priv, ap_info,
						   &priv->aplist.ap[i]);
			return;
		}
	}
	priv->scan_ind_count++;
	if (priv->scan_ind_count < LOCAL_APLIST_MAX + 1) {
		netdev_dbg(priv->net_dev, " scan_ind_count=%d :: aplist.size=%d\n",
			priv->scan_ind_count, priv->aplist.size);
		get_ap_information(priv, (struct ap_info *)(priv->rxp),
				   &(priv->aplist.ap[priv->scan_ind_count - 1]));
		priv->aplist.size = priv->scan_ind_count;
	} else {
		netdev_dbg(priv->net_dev, " count over :: scan_ind_count=%d\n",
			   priv->scan_ind_count);
	}
}

static
void hostif_stop_confirm(struct ks_wlan_private *priv)
{
	unsigned int tmp = 0;
	unsigned int old_status = priv->connect_status;
	struct net_device *netdev = priv->net_dev;
	union iwreq_data wrqu0;

	if (priv->dev_state == DEVICE_STATE_SLEEP)
		priv->dev_state = DEVICE_STATE_READY;

	/* disconnect indication */
	if (is_connect_status(priv->connect_status)) {
		netif_carrier_off(netdev);
		tmp = FORCE_DISCONNECT & priv->connect_status;
		priv->connect_status = tmp | DISCONNECT_STATUS;
		netdev_info(netdev, "IWEVENT: disconnect\n");

		wrqu0.data.length = 0;
		wrqu0.data.flags = 0;
		wrqu0.ap_addr.sa_family = ARPHRD_ETHER;
		if (is_disconnect_status(priv->connect_status) &&
		    is_connect_status(old_status)) {
			eth_zero_addr(wrqu0.ap_addr.sa_data);
			netdev_info(netdev, "IWEVENT: disconnect\n");
			wireless_send_event(netdev, SIOCGIWAP, &wrqu0, NULL);
		}
		priv->scan_ind_count = 0;
	}

	hostif_sme_enqueue(priv, SME_STOP_CONFIRM);
}

static
void hostif_ps_adhoc_set_confirm(struct ks_wlan_private *priv)
{
	priv->infra_status = 0;	/* infrastructure mode cancel */
	hostif_sme_enqueue(priv, SME_MODE_SET_CONFIRM);
}

static
void hostif_infrastructure_set_confirm(struct ks_wlan_private *priv)
{
	u16 result_code;

	result_code = get_word(priv);
	priv->infra_status = 1;	/* infrastructure mode set */
	hostif_sme_enqueue(priv, SME_MODE_SET_CONFIRM);
}

static
void hostif_adhoc_set_confirm(struct ks_wlan_private *priv)
{
	priv->infra_status = 1;	/* infrastructure mode set */
	hostif_sme_enqueue(priv, SME_MODE_SET_CONFIRM);
}

static
void hostif_associate_indication(struct ks_wlan_private *priv)
{
	struct association_request *assoc_req;
	struct association_response *assoc_resp;
	unsigned char *pb;
	union iwreq_data wrqu;
	char buf[IW_CUSTOM_MAX];
	char *pbuf = &buf[0];
	int i;

	static const char associnfo_leader0[] = "ASSOCINFO(ReqIEs=";
	static const char associnfo_leader1[] = " RespIEs=";

	assoc_req = (struct association_request *)(priv->rxp);
	assoc_resp = (struct association_response *)(assoc_req + 1);
	pb = (unsigned char *)(assoc_resp + 1);

	memset(&wrqu, 0, sizeof(wrqu));
	memcpy(pbuf, associnfo_leader0, sizeof(associnfo_leader0) - 1);
	wrqu.data.length += sizeof(associnfo_leader0) - 1;
	pbuf += sizeof(associnfo_leader0) - 1;

	for (i = 0; i < le16_to_cpu(assoc_req->req_ies_size); i++)
		pbuf += sprintf(pbuf, "%02x", *(pb + i));
	wrqu.data.length += (le16_to_cpu(assoc_req->req_ies_size)) * 2;

	memcpy(pbuf, associnfo_leader1, sizeof(associnfo_leader1) - 1);
	wrqu.data.length += sizeof(associnfo_leader1) - 1;
	pbuf += sizeof(associnfo_leader1) - 1;

	pb += le16_to_cpu(assoc_req->req_ies_size);
	for (i = 0; i < le16_to_cpu(assoc_resp->resp_ies_size); i++)
		pbuf += sprintf(pbuf, "%02x", *(pb + i));
	wrqu.data.length += (le16_to_cpu(assoc_resp->resp_ies_size)) * 2;

	pbuf += sprintf(pbuf, ")");
	wrqu.data.length += 1;

	wireless_send_event(priv->net_dev, IWEVCUSTOM, &wrqu, buf);
}

static
void hostif_bss_scan_confirm(struct ks_wlan_private *priv)
{
	unsigned int result_code;
	struct net_device *dev = priv->net_dev;
	union iwreq_data wrqu;

	result_code = get_dword(priv);
	netdev_dbg(priv->net_dev, "result=%d :: scan_ind_count=%d\n", result_code,
		   priv->scan_ind_count);

	priv->sme_i.sme_flag &= ~SME_AP_SCAN;
	hostif_sme_enqueue(priv, SME_BSS_SCAN_CONFIRM);

	wrqu.data.length = 0;
	wrqu.data.flags = 0;
	wireless_send_event(dev, SIOCGIWSCAN, &wrqu, NULL);
	priv->scan_ind_count = 0;
}

static
void hostif_phy_information_confirm(struct ks_wlan_private *priv)
{
	struct iw_statistics *wstats = &priv->wstats;
	unsigned char rssi, signal, noise;
	unsigned char link_speed;
	unsigned int transmitted_frame_count, received_fragment_count;
	unsigned int failed_count, fcs_error_count;

	rssi = get_byte(priv);
	signal = get_byte(priv);
	noise = get_byte(priv);
	link_speed = get_byte(priv);
	transmitted_frame_count = get_dword(priv);
	received_fragment_count = get_dword(priv);
	failed_count = get_dword(priv);
	fcs_error_count = get_dword(priv);

	netdev_dbg(priv->net_dev, "phyinfo confirm rssi=%d signal=%d\n",
		   rssi, signal);
	priv->current_rate = (link_speed & RATE_MASK);
	wstats->qual.qual = signal;
	wstats->qual.level = 256 - rssi;
	wstats->qual.noise = 0;	/* invalid noise value */
	wstats->qual.updated = IW_QUAL_ALL_UPDATED | IW_QUAL_DBM;

	netdev_dbg(priv->net_dev, "\n    rssi=%u\n"
		   "    signal=%u\n"
		   "    link_speed=%ux500Kbps\n"
		   "    transmitted_frame_count=%u\n"
		   "    received_fragment_count=%u\n"
		   "    failed_count=%u\n"
		   "    fcs_error_count=%u\n",
		   rssi, signal, link_speed, transmitted_frame_count,
		   received_fragment_count, failed_count, fcs_error_count);
	/* wake_up_interruptible_all(&priv->confirm_wait); */
	complete(&priv->confirm_wait);
}

static
void hostif_mic_failure_confirm(struct ks_wlan_private *priv)
{
	netdev_dbg(priv->net_dev, "mic_failure=%u\n", priv->wpa.mic_failure.failure);
	hostif_sme_enqueue(priv, SME_MIC_FAILURE_CONFIRM);
}

static
void hostif_event_check(struct ks_wlan_private *priv)
{
	unsigned short event;

	event = get_word(priv);
	switch (event) {
	case HIF_DATA_IND:
		hostif_data_indication(priv);
		break;
	case HIF_MIB_GET_CONF:
		hostif_mib_get_confirm(priv);
		break;
	case HIF_MIB_SET_CONF:
		hostif_mib_set_confirm(priv);
		break;
	case HIF_POWER_MGMT_CONF:
		hostif_power_mgmt_confirm(priv);
		break;
	case HIF_SLEEP_CONF:
		hostif_sleep_confirm(priv);
		break;
	case HIF_START_CONF:
		hostif_start_confirm(priv);
		break;
	case HIF_CONNECT_IND:
		hostif_connect_indication(priv);
		break;
	case HIF_STOP_CONF:
		hostif_stop_confirm(priv);
		break;
	case HIF_PS_ADH_SET_CONF:
		hostif_ps_adhoc_set_confirm(priv);
		break;
	case HIF_INFRA_SET_CONF:
	case HIF_INFRA_SET2_CONF:
		hostif_infrastructure_set_confirm(priv);
		break;
	case HIF_ADH_SET_CONF:
	case HIF_ADH_SET2_CONF:
		hostif_adhoc_set_confirm(priv);
		break;
	case HIF_ASSOC_INFO_IND:
		hostif_associate_indication(priv);
		break;
	case HIF_MIC_FAILURE_CONF:
		hostif_mic_failure_confirm(priv);
		break;
	case HIF_SCAN_CONF:
		hostif_bss_scan_confirm(priv);
		break;
	case HIF_PHY_INFO_CONF:
	case HIF_PHY_INFO_IND:
		hostif_phy_information_confirm(priv);
		break;
	case HIF_SCAN_IND:
		hostif_scan_indication(priv);
		break;
	case HIF_AP_SET_CONF:
	default:
		netdev_err(priv->net_dev, "undefined event[%04X]\n", event);
		/* wake_up_all(&priv->confirm_wait); */
		complete(&priv->confirm_wait);
		break;
	}

	/* add event to hostt buffer */
	priv->hostt.buff[priv->hostt.qtail] = event;
	priv->hostt.qtail = (priv->hostt.qtail + 1) % SME_EVENT_BUFF_SIZE;
}

/* allocate size bytes, set header size and event */
static void *hostif_generic_request(size_t size, int event)
{
	struct hostif_hdr *p;

	p = kzalloc(hif_align_size(size), GFP_ATOMIC);
	if (!p)
		return NULL;

	p->size = cpu_to_le16((u16)(size - sizeof(p->size)));
	p->event = cpu_to_le16(event);

	return p;
}

int hostif_data_request(struct ks_wlan_private *priv, struct sk_buff *skb)
{
	unsigned int skb_len = 0;
	unsigned char *buffer = NULL;
	unsigned int length = 0;
	struct hostif_data_request *pp;
	unsigned char *p;
	int result = 0;
	unsigned short eth_proto;
	struct ether_hdr *eth_hdr;
	struct michael_mic michael_mic;
	unsigned short keyinfo = 0;
	struct ieee802_1x_hdr *aa1x_hdr;
	struct wpa_eapol_key *eap_key;
	struct ethhdr *eth;
	size_t size;
	int ret;

	skb_len = skb->len;
	if (skb_len > ETH_FRAME_LEN) {
		netdev_err(priv->net_dev, "bad length skb_len=%d\n", skb_len);
		ret = -EOVERFLOW;
		goto err_kfree_skb;
	}

	if (is_disconnect_status(priv->connect_status) ||
	    (priv->connect_status & FORCE_DISCONNECT) ||
	    priv->wpa.mic_failure.stop) {
		if (netif_queue_stopped(priv->net_dev))
			netif_wake_queue(priv->net_dev);
		if (skb)
			dev_kfree_skb(skb);

		return 0;
	}

	/* power save wakeup */
	if (atomic_read(&priv->psstatus.status) == PS_SNOOZE) {
		if (!netif_queue_stopped(priv->net_dev))
			netif_stop_queue(priv->net_dev);
	}

	size = sizeof(*pp) + 6 + skb_len + 8;
	pp = kmalloc(hif_align_size(size), GFP_ATOMIC);
	if (!pp) {
		ret = -ENOMEM;
		goto err_kfree_skb;
	}

	p = (unsigned char *)pp->data;

	buffer = skb->data;
	length = skb->len;

	/* skb check */
	eth = (struct ethhdr *)skb->data;
	if (!ether_addr_equal(&priv->eth_addr[0], eth->h_source)) {
		netdev_err(priv->net_dev, "invalid mac address !!\n");
		netdev_err(priv->net_dev, "ethernet->h_source=%pM\n", eth->h_source);
		ret = -ENXIO;
		goto err_kfree;
	}

	/* dest and src MAC address copy */
	size = ETH_ALEN * 2;
	memcpy(p, buffer, size);
	p += size;
	buffer += size;
	length -= size;

	/* EtherType/Length check */
	if (*(buffer + 1) + (*buffer << 8) > 1500) {
		/* ProtocolEAP = *(buffer+1) + (*buffer << 8); */
		/* netdev_dbg(priv->net_dev, "Send [SNAP]Type %x\n",ProtocolEAP); */
		/* SAP/CTL/OUI(6 byte) add */
		*p++ = 0xAA;	/* DSAP */
		*p++ = 0xAA;	/* SSAP */
		*p++ = 0x03;	/* CTL */
		*p++ = 0x00;	/* OUI ("000000") */
		*p++ = 0x00;	/* OUI ("000000") */
		*p++ = 0x00;	/* OUI ("000000") */
		skb_len += 6;
	} else {
		/* Length(2 byte) delete */
		buffer += 2;
		length -= 2;
		skb_len -= 2;
	}

	/* pp->data copy */
	memcpy(p, buffer, length);

	p += length;

	/* for WPA */
	eth_hdr = (struct ether_hdr *)&pp->data[0];
	eth_proto = ntohs(eth_hdr->h_proto);

	/* for MIC FAILURE REPORT check */
	if (eth_proto == ETH_P_PAE &&
	    priv->wpa.mic_failure.failure > 0) {
		aa1x_hdr = (struct ieee802_1x_hdr *)(eth_hdr + 1);
		if (aa1x_hdr->type == IEEE802_1X_TYPE_EAPOL_KEY) {
			eap_key = (struct wpa_eapol_key *)(aa1x_hdr + 1);
			keyinfo = ntohs(eap_key->key_info);
		}
	}

	if (priv->wpa.rsn_enabled && priv->wpa.key[0].key_len) {
		/* no encryption */
		if (eth_proto == ETH_P_PAE &&
		    priv->wpa.key[1].key_len == 0 &&
		    priv->wpa.key[2].key_len == 0 &&
		    priv->wpa.key[3].key_len == 0) {
			pp->auth_type = cpu_to_le16((uint16_t)TYPE_AUTH);
		} else {
			if (priv->wpa.pairwise_suite == IW_AUTH_CIPHER_TKIP) {
				michael_mic_function(&michael_mic,
						     priv->wpa.key[0].tx_mic_key,
						     &pp->data[0], skb_len,
						     0,	michael_mic.result);
				memcpy(p, michael_mic.result, 8);
				length += 8;
				skb_len += 8;
				p += 8;
				pp->auth_type =
				    cpu_to_le16((uint16_t)TYPE_DATA);

			} else if (priv->wpa.pairwise_suite ==
				   IW_AUTH_CIPHER_CCMP) {
				pp->auth_type =
				    cpu_to_le16((uint16_t)TYPE_DATA);
			}
		}
	} else {
		if (eth_proto == ETH_P_PAE)
			pp->auth_type = cpu_to_le16((uint16_t)TYPE_AUTH);
		else
			pp->auth_type = cpu_to_le16((uint16_t)TYPE_DATA);
	}

	/* header value set */
	pp->header.size =
	    cpu_to_le16((uint16_t)
			(sizeof(*pp) - sizeof(pp->header.size) + skb_len));
	pp->header.event = cpu_to_le16((uint16_t)HIF_DATA_REQ);

	/* tx request */
	result = ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp) + skb_len),
			       send_packet_complete, skb);

	/* MIC FAILURE REPORT check */
	if (eth_proto == ETH_P_PAE &&
	    priv->wpa.mic_failure.failure > 0) {
		if (keyinfo & WPA_KEY_INFO_ERROR &&
		    keyinfo & WPA_KEY_INFO_REQUEST) {
			netdev_err(priv->net_dev, " MIC ERROR Report SET : %04X\n", keyinfo);
			hostif_sme_enqueue(priv, SME_MIC_FAILURE_REQUEST);
		}
		if (priv->wpa.mic_failure.failure == 2)
			priv->wpa.mic_failure.stop = 1;
	}

	return result;

err_kfree:
	kfree(pp);
err_kfree_skb:
	dev_kfree_skb(skb);

	return ret;
}

static inline void ps_confirm_wait_inc(struct ks_wlan_private *priv)
{
	if (atomic_read(&priv->psstatus.status) > PS_ACTIVE_SET)
		atomic_inc(&priv->psstatus.confirm_wait);
}

static
void hostif_mib_get_request(struct ks_wlan_private *priv,
			    unsigned long mib_attribute)
{
	struct hostif_mib_get_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_MIB_GET_REQ);
	if (!pp)
		return;

	pp->mib_attribute = cpu_to_le32((uint32_t)mib_attribute);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_mib_set_request(struct ks_wlan_private *priv,
			    unsigned long mib_attribute, unsigned short size,
			    unsigned short type, void *vp)
{
	struct hostif_mib_set_request_t *pp;

	if (priv->dev_state < DEVICE_STATE_BOOT)
		return;

	pp = hostif_generic_request(sizeof(*pp), HIF_MIB_SET_REQ);
	if (!pp)
		return;

	pp->mib_attribute = cpu_to_le32((uint32_t)mib_attribute);
	pp->mib_value.size = cpu_to_le16((uint16_t)size);
	pp->mib_value.type = cpu_to_le16((uint16_t)type);
	memcpy(&pp->mib_value.body, vp, size);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp) + size), NULL, NULL);
}

static
void hostif_start_request(struct ks_wlan_private *priv, unsigned char mode)
{
	struct hostif_start_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_START_REQ);
	if (!pp)
		return;

	pp->mode = cpu_to_le16((uint16_t)mode);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);

	priv->aplist.size = 0;
	priv->scan_ind_count = 0;
}

static __le16 ks_wlan_cap(struct ks_wlan_private *priv)
{
	u16 capability = 0x0000;

	if (priv->reg.preamble == SHORT_PREAMBLE)
		capability |= WLAN_CAPABILITY_SHORT_PREAMBLE;

	capability &= ~(WLAN_CAPABILITY_PBCC);	/* pbcc not support */

	if (priv->reg.phy_type != D_11B_ONLY_MODE) {
		capability |= WLAN_CAPABILITY_SHORT_SLOT_TIME;
		capability &= ~(WLAN_CAPABILITY_DSSS_OFDM);
	}

	return cpu_to_le16((uint16_t)capability);
}

static void init_request(struct ks_wlan_private *priv,
			 struct hostif_request *req)
{
	req->phy_type = cpu_to_le16((uint16_t)(priv->reg.phy_type));
	req->cts_mode = cpu_to_le16((uint16_t)(priv->reg.cts_mode));
	req->scan_type = cpu_to_le16((uint16_t)(priv->reg.scan_type));
	req->rate_set.size = priv->reg.rate_set.size;
	req->capability = ks_wlan_cap(priv);
	memcpy(&req->rate_set.body[0], &priv->reg.rate_set.body[0],
	       priv->reg.rate_set.size);
}

static
void hostif_ps_adhoc_set_request(struct ks_wlan_private *priv)
{
	struct hostif_ps_adhoc_set_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_PS_ADH_SET_REQ);
	if (!pp)
		return;

	init_request(priv, &pp->request);
	pp->channel = cpu_to_le16((uint16_t)(priv->reg.channel));

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_infrastructure_set_request(struct ks_wlan_private *priv, int event)
{
	struct hostif_infrastructure_set_request *pp;

	pp = hostif_generic_request(sizeof(*pp), event);
	if (!pp)
		return;

	init_request(priv, &pp->request);
	pp->ssid.size = priv->reg.ssid.size;
	memcpy(&pp->ssid.body[0], &priv->reg.ssid.body[0], priv->reg.ssid.size);
	pp->beacon_lost_count =
	    cpu_to_le16((uint16_t)(priv->reg.beacon_lost_count));
	pp->auth_type = cpu_to_le16((uint16_t)(priv->reg.authenticate_type));

	pp->channel_list.body[0] = 1;
	pp->channel_list.body[1] = 8;
	pp->channel_list.body[2] = 2;
	pp->channel_list.body[3] = 9;
	pp->channel_list.body[4] = 3;
	pp->channel_list.body[5] = 10;
	pp->channel_list.body[6] = 4;
	pp->channel_list.body[7] = 11;
	pp->channel_list.body[8] = 5;
	pp->channel_list.body[9] = 12;
	pp->channel_list.body[10] = 6;
	pp->channel_list.body[11] = 13;
	pp->channel_list.body[12] = 7;
	if (priv->reg.phy_type == D_11G_ONLY_MODE) {
		pp->channel_list.size = 13;
	} else {
		pp->channel_list.body[13] = 14;
		pp->channel_list.size = 14;
	}

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_adhoc_set_request(struct ks_wlan_private *priv)
{
	struct hostif_adhoc_set_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_ADH_SET_REQ);
	if (!pp)
		return;

	init_request(priv, &pp->request);
	pp->channel = cpu_to_le16((uint16_t)(priv->reg.channel));
	pp->ssid.size = priv->reg.ssid.size;
	memcpy(&pp->ssid.body[0], &priv->reg.ssid.body[0], priv->reg.ssid.size);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_adhoc_set2_request(struct ks_wlan_private *priv)
{
	struct hostif_adhoc_set2_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_ADH_SET_REQ);
	if (!pp)
		return;

	init_request(priv, &pp->request);
	pp->ssid.size = priv->reg.ssid.size;
	memcpy(&pp->ssid.body[0], &priv->reg.ssid.body[0], priv->reg.ssid.size);

	pp->channel_list.body[0] = priv->reg.channel;
	pp->channel_list.size = 1;
	memcpy(pp->bssid, priv->reg.bssid, ETH_ALEN);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_stop_request(struct ks_wlan_private *priv)
{
	struct hostif_stop_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_STOP_REQ);
	if (!pp)
		return;

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_phy_information_request(struct ks_wlan_private *priv)
{
	struct hostif_phy_information_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_PHY_INFO_REQ);
	if (!pp)
		return;

	if (priv->reg.phy_info_timer) {
		pp->type = cpu_to_le16((uint16_t)TIME_TYPE);
		pp->time = cpu_to_le16((uint16_t)(priv->reg.phy_info_timer));
	} else {
		pp->type = cpu_to_le16((uint16_t)NORMAL_TYPE);
		pp->time = cpu_to_le16((uint16_t)0);
	}

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_power_mgmt_request(struct ks_wlan_private *priv,
			       unsigned long mode, unsigned long wake_up,
			       unsigned long receive_dtims)
{
	struct hostif_power_mgmt_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_POWER_MGMT_REQ);
	if (!pp)
		return;

	pp->mode = cpu_to_le32((uint32_t)mode);
	pp->wake_up = cpu_to_le32((uint32_t)wake_up);
	pp->receive_dtims = cpu_to_le32((uint32_t)receive_dtims);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

static
void hostif_sleep_request(struct ks_wlan_private *priv,
			  enum sleep_mode_type mode)
{
	struct hostif_sleep_request *pp;

	if (mode == SLP_SLEEP) {
		pp = hostif_generic_request(sizeof(*pp), HIF_SLEEP_REQ);
		if (!pp)
			return;

		/* send to device request */
		ps_confirm_wait_inc(priv);
		ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL,
			      NULL);
	} else if (mode == SLP_ACTIVE) {
		atomic_set(&priv->sleepstatus.wakeup_request, 1);
		queue_delayed_work(priv->wq, &priv->rw_dwork, 1);
	} else {
		netdev_err(priv->net_dev, "invalid mode %ld\n", (long)mode);
		return;
	}
}

static
void hostif_bss_scan_request(struct ks_wlan_private *priv,
			     unsigned long scan_type, uint8_t *scan_ssid,
			     uint8_t scan_ssid_len)
{
	struct hostif_bss_scan_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_SCAN_REQ);
	if (!pp)
		return;

	pp->scan_type = scan_type;

	pp->ch_time_min = cpu_to_le32((uint32_t)110);	/* default value */
	pp->ch_time_max = cpu_to_le32((uint32_t)130);	/* default value */
	pp->channel_list.body[0] = 1;
	pp->channel_list.body[1] = 8;
	pp->channel_list.body[2] = 2;
	pp->channel_list.body[3] = 9;
	pp->channel_list.body[4] = 3;
	pp->channel_list.body[5] = 10;
	pp->channel_list.body[6] = 4;
	pp->channel_list.body[7] = 11;
	pp->channel_list.body[8] = 5;
	pp->channel_list.body[9] = 12;
	pp->channel_list.body[10] = 6;
	pp->channel_list.body[11] = 13;
	pp->channel_list.body[12] = 7;
	if (priv->reg.phy_type == D_11G_ONLY_MODE) {
		pp->channel_list.size = 13;
	} else {
		pp->channel_list.body[13] = 14;
		pp->channel_list.size = 14;
	}
	pp->ssid.size = 0;

	/* specified SSID SCAN */
	if (scan_ssid_len > 0 && scan_ssid_len <= 32) {
		pp->ssid.size = scan_ssid_len;
		memcpy(&pp->ssid.body[0], scan_ssid, scan_ssid_len);
	}

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);

	priv->aplist.size = 0;
	priv->scan_ind_count = 0;
}

static
void hostif_mic_failure_request(struct ks_wlan_private *priv,
				unsigned short failure_count,
				unsigned short timer)
{
	struct hostif_mic_failure_request *pp;

	pp = hostif_generic_request(sizeof(*pp), HIF_MIC_FAILURE_REQ);
	if (!pp)
		return;

	pp->failure_count = cpu_to_le16((uint16_t)failure_count);
	pp->timer = cpu_to_le16((uint16_t)timer);

	/* send to device request */
	ps_confirm_wait_inc(priv);
	ks_wlan_hw_tx(priv, pp, hif_align_size(sizeof(*pp)), NULL, NULL);
}

/* Device I/O Receive indicate */
static void devio_rec_ind(struct ks_wlan_private *priv, unsigned char *p,
			  unsigned int size)
{
	if (!priv->is_device_open)
		return;

	spin_lock(&priv->dev_read_lock);
	priv->dev_data[atomic_read(&priv->rec_count)] = p;
	priv->dev_size[atomic_read(&priv->rec_count)] = size;

	if (atomic_read(&priv->event_count) != DEVICE_STOCK_COUNT) {
		/* rx event count inc */
		atomic_inc(&priv->event_count);
	}
	atomic_inc(&priv->rec_count);
	if (atomic_read(&priv->rec_count) == DEVICE_STOCK_COUNT)
		atomic_set(&priv->rec_count, 0);

	wake_up_interruptible_all(&priv->devread_wait);

	spin_unlock(&priv->dev_read_lock);
}

void hostif_receive(struct ks_wlan_private *priv, unsigned char *p,
		    unsigned int size)
{
	devio_rec_ind(priv, p, size);

	priv->rxp = p;
	priv->rx_size = size;

	if (get_word(priv) == priv->rx_size)
		hostif_event_check(priv);
}

static
void hostif_sme_set_wep(struct ks_wlan_private *priv, int type)
{
	__le32 val;

	switch (type) {
	case SME_WEP_INDEX_REQUEST:
		val = cpu_to_le32((uint32_t)(priv->reg.wep_index));
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_ID,
				       sizeof(val), MIB_VALUE_TYPE_INT, &val);
		break;
	case SME_WEP_KEY1_REQUEST:
		if (!priv->wpa.wpa_enabled)
			hostif_mib_set_request(priv,
					       DOT11_WEP_DEFAULT_KEY_VALUE1,
					       priv->reg.wep_key[0].size,
					       MIB_VALUE_TYPE_OSTRING,
					       &priv->reg.wep_key[0].val[0]);
		break;
	case SME_WEP_KEY2_REQUEST:
		if (!priv->wpa.wpa_enabled)
			hostif_mib_set_request(priv,
					       DOT11_WEP_DEFAULT_KEY_VALUE2,
					       priv->reg.wep_key[1].size,
					       MIB_VALUE_TYPE_OSTRING,
					       &priv->reg.wep_key[1].val[0]);
		break;
	case SME_WEP_KEY3_REQUEST:
		if (!priv->wpa.wpa_enabled)
			hostif_mib_set_request(priv,
					       DOT11_WEP_DEFAULT_KEY_VALUE3,
					       priv->reg.wep_key[2].size,
					       MIB_VALUE_TYPE_OSTRING,
					       &priv->reg.wep_key[2].val[0]);
		break;
	case SME_WEP_KEY4_REQUEST:
		if (!priv->wpa.wpa_enabled)
			hostif_mib_set_request(priv,
					       DOT11_WEP_DEFAULT_KEY_VALUE4,
					       priv->reg.wep_key[3].size,
					       MIB_VALUE_TYPE_OSTRING,
					       &priv->reg.wep_key[3].val[0]);
		break;
	case SME_WEP_FLAG_REQUEST:
		val = cpu_to_le32((uint32_t)(priv->reg.privacy_invoked));
		hostif_mib_set_request(priv, DOT11_PRIVACY_INVOKED,
				       sizeof(val), MIB_VALUE_TYPE_BOOL, &val);
		break;
	}
}

struct wpa_suite {
	__le16 size;
	unsigned char suite[4][CIPHER_ID_LEN];
} __packed;

struct rsn_mode {
	__le32 rsn_mode;
	__le16 rsn_capability;
} __packed;

static
void hostif_sme_set_rsn(struct ks_wlan_private *priv, int type)
{
	struct wpa_suite wpa_suite;
	struct rsn_mode rsn_mode;
	__le32 val;

	memset(&wpa_suite, 0, sizeof(wpa_suite));

	switch (type) {
	case SME_RSN_UCAST_REQUEST:
		wpa_suite.size = cpu_to_le16((uint16_t)1);
		switch (priv->wpa.pairwise_suite) {
		case IW_AUTH_CIPHER_NONE:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_NONE, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_NONE, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_WEP40:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_WEP40, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_WEP40, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_TKIP:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_TKIP, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_TKIP, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_CCMP:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_CCMP, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_CCMP, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_WEP104:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_WEP104, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_WEP104, CIPHER_ID_LEN);
			break;
		}

		hostif_mib_set_request(priv, DOT11_RSN_CONFIG_UNICAST_CIPHER,
				       sizeof(wpa_suite.size) +
				       CIPHER_ID_LEN *
				       le16_to_cpu(wpa_suite.size),
				       MIB_VALUE_TYPE_OSTRING, &wpa_suite);
		break;
	case SME_RSN_MCAST_REQUEST:
		switch (priv->wpa.group_suite) {
		case IW_AUTH_CIPHER_NONE:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_NONE, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_NONE, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_WEP40:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_WEP40, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_WEP40, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_TKIP:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_TKIP, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_TKIP, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_CCMP:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_CCMP, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_CCMP, CIPHER_ID_LEN);
			break;
		case IW_AUTH_CIPHER_WEP104:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA2_WEP104, CIPHER_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       CIPHER_ID_WPA_WEP104, CIPHER_ID_LEN);
			break;
		}

		hostif_mib_set_request(priv, DOT11_RSN_CONFIG_MULTICAST_CIPHER,
				       CIPHER_ID_LEN, MIB_VALUE_TYPE_OSTRING,
				       &wpa_suite.suite[0][0]);
		break;
	case SME_RSN_AUTH_REQUEST:
		wpa_suite.size = cpu_to_le16((uint16_t)1);
		switch (priv->wpa.key_mgmt_suite) {
		case IW_AUTH_KEY_MGMT_802_1X:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA2_1X, KEY_MGMT_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA_1X, KEY_MGMT_ID_LEN);
			break;
		case IW_AUTH_KEY_MGMT_PSK:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA2_PSK, KEY_MGMT_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA_PSK, KEY_MGMT_ID_LEN);
			break;
		case 0:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA2_NONE, KEY_MGMT_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA_NONE, KEY_MGMT_ID_LEN);
			break;
		case 4:
			if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2)
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA2_WPANONE,
				       KEY_MGMT_ID_LEN);
			else
				memcpy(&wpa_suite.suite[0][0],
				       KEY_MGMT_ID_WPA_WPANONE,
				       KEY_MGMT_ID_LEN);
			break;
		}

		hostif_mib_set_request(priv, DOT11_RSN_CONFIG_AUTH_SUITE,
				       sizeof(wpa_suite.size) +
				       KEY_MGMT_ID_LEN *
				       le16_to_cpu(wpa_suite.size),
				       MIB_VALUE_TYPE_OSTRING, &wpa_suite);
		break;
	case SME_RSN_ENABLED_REQUEST:
		val = cpu_to_le32((uint32_t)(priv->wpa.rsn_enabled));
		hostif_mib_set_request(priv, DOT11_RSN_ENABLED,
				       sizeof(val), MIB_VALUE_TYPE_BOOL, &val);
		break;
	case SME_RSN_MODE_REQUEST:
		if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA2) {
			rsn_mode.rsn_mode =
			    cpu_to_le32((uint32_t)RSN_MODE_WPA2);
			rsn_mode.rsn_capability = cpu_to_le16((uint16_t)0);
		} else if (priv->wpa.version == IW_AUTH_WPA_VERSION_WPA) {
			rsn_mode.rsn_mode =
			    cpu_to_le32((uint32_t)RSN_MODE_WPA);
			rsn_mode.rsn_capability = cpu_to_le16((uint16_t)0);
		} else {
			rsn_mode.rsn_mode =
			    cpu_to_le32((uint32_t)RSN_MODE_NONE);
			rsn_mode.rsn_capability = cpu_to_le16((uint16_t)0);
		}
		hostif_mib_set_request(priv, LOCAL_RSN_MODE, sizeof(rsn_mode),
				       MIB_VALUE_TYPE_OSTRING, &rsn_mode);
		break;
	}
}

static
void hostif_sme_mode_setup(struct ks_wlan_private *priv)
{
	unsigned char rate_size;
	unsigned char rate_octet[RATE_SET_MAX_SIZE];
	int i = 0;

	/* rate setting if rate segging is auto for changing phy_type (#94) */
	if (priv->reg.tx_rate == TX_RATE_FULL_AUTO) {
		if (priv->reg.phy_type == D_11B_ONLY_MODE) {
			priv->reg.rate_set.body[3] = TX_RATE_11M;
			priv->reg.rate_set.body[2] = TX_RATE_5M;
			priv->reg.rate_set.body[1] = TX_RATE_2M | BASIC_RATE;
			priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
			priv->reg.rate_set.size = 4;
		} else {	/* D_11G_ONLY_MODE or D_11BG_COMPATIBLE_MODE */
			priv->reg.rate_set.body[11] = TX_RATE_54M;
			priv->reg.rate_set.body[10] = TX_RATE_48M;
			priv->reg.rate_set.body[9] = TX_RATE_36M;
			priv->reg.rate_set.body[8] = TX_RATE_18M;
			priv->reg.rate_set.body[7] = TX_RATE_9M;
			priv->reg.rate_set.body[6] = TX_RATE_24M | BASIC_RATE;
			priv->reg.rate_set.body[5] = TX_RATE_12M | BASIC_RATE;
			priv->reg.rate_set.body[4] = TX_RATE_6M | BASIC_RATE;
			priv->reg.rate_set.body[3] = TX_RATE_11M | BASIC_RATE;
			priv->reg.rate_set.body[2] = TX_RATE_5M | BASIC_RATE;
			priv->reg.rate_set.body[1] = TX_RATE_2M | BASIC_RATE;
			priv->reg.rate_set.body[0] = TX_RATE_1M | BASIC_RATE;
			priv->reg.rate_set.size = 12;
		}
	}

	/* rate mask by phy setting */
	if (priv->reg.phy_type == D_11B_ONLY_MODE) {
		for (i = 0; i < priv->reg.rate_set.size; i++) {
			if (!is_11b_rate(priv->reg.rate_set.body[i]))
				break;

			if ((priv->reg.rate_set.body[i] & RATE_MASK) >= TX_RATE_5M) {
				rate_octet[i] = priv->reg.rate_set.body[i] &
						RATE_MASK;
			} else {
				rate_octet[i] = priv->reg.rate_set.body[i];
			}
		}

	} else {	/* D_11G_ONLY_MODE or D_11BG_COMPATIBLE_MODE */
		for (i = 0; i < priv->reg.rate_set.size; i++) {
			if (!is_11bg_rate(priv->reg.rate_set.body[i]))
				break;

			if (is_ofdm_ext_rate(priv->reg.rate_set.body[i])) {
				rate_octet[i] = priv->reg.rate_set.body[i] &
						RATE_MASK;
			} else {
				rate_octet[i] = priv->reg.rate_set.body[i];
			}
		}
	}
	rate_size = i;
	if (rate_size == 0) {
		if (priv->reg.phy_type == D_11G_ONLY_MODE)
			rate_octet[0] = TX_RATE_6M | BASIC_RATE;
		else
			rate_octet[0] = TX_RATE_2M | BASIC_RATE;
		rate_size = 1;
	}

	/* rate set update */
	priv->reg.rate_set.size = rate_size;
	memcpy(&priv->reg.rate_set.body[0], &rate_octet[0], rate_size);

	switch (priv->reg.operation_mode) {
	case MODE_PSEUDO_ADHOC:
		hostif_ps_adhoc_set_request(priv);
		break;
	case MODE_INFRASTRUCTURE:
		if (!is_valid_ether_addr((u8 *)priv->reg.bssid)) {
			hostif_infrastructure_set_request(priv, HIF_INFRA_SET_REQ);
		} else {
			hostif_infrastructure_set_request(priv, HIF_INFRA_SET2_REQ);
			netdev_dbg(priv->net_dev,
				   "Infra bssid = %pM\n", priv->reg.bssid);
		}
		break;
	case MODE_ADHOC:
		if (!is_valid_ether_addr((u8 *)priv->reg.bssid)) {
			hostif_adhoc_set_request(priv);
		} else {
			hostif_adhoc_set2_request(priv);
			netdev_dbg(priv->net_dev,
				   "Adhoc bssid = %pM\n", priv->reg.bssid);
		}
		break;
	default:
		break;
	}
}

static
void hostif_sme_multicast_set(struct ks_wlan_private *priv)
{
	struct net_device *dev = priv->net_dev;
	int mc_count;
	struct netdev_hw_addr *ha;
	char set_address[NIC_MAX_MCAST_LIST * ETH_ALEN];
	__le32 filter_type;
	int i = 0;

	spin_lock(&priv->multicast_spin);

	memset(set_address, 0, NIC_MAX_MCAST_LIST * ETH_ALEN);

	if (dev->flags & IFF_PROMISC) {
		filter_type = cpu_to_le32((uint32_t)MCAST_FILTER_PROMISC);
		hostif_mib_set_request(priv, LOCAL_MULTICAST_FILTER,
				       sizeof(filter_type), MIB_VALUE_TYPE_BOOL,
				       &filter_type);
		goto spin_unlock;
	}

	if ((netdev_mc_count(dev) > NIC_MAX_MCAST_LIST) ||
	    (dev->flags & IFF_ALLMULTI)) {
		filter_type = cpu_to_le32((uint32_t)MCAST_FILTER_MCASTALL);
		hostif_mib_set_request(priv, LOCAL_MULTICAST_FILTER,
				       sizeof(filter_type), MIB_VALUE_TYPE_BOOL,
				       &filter_type);
		goto spin_unlock;
	}

	if (priv->sme_i.sme_flag & SME_MULTICAST) {
		mc_count = netdev_mc_count(dev);
		netdev_for_each_mc_addr(ha, dev) {
			ether_addr_copy(&set_address[i * ETH_ALEN], ha->addr);
			i++;
		}
		priv->sme_i.sme_flag &= ~SME_MULTICAST;
		hostif_mib_set_request(priv, LOCAL_MULTICAST_ADDRESS,
				       ETH_ALEN * mc_count,
				       MIB_VALUE_TYPE_OSTRING,
				       &set_address[0]);
	} else {
		filter_type = cpu_to_le32((uint32_t)MCAST_FILTER_MCAST);
		priv->sme_i.sme_flag |= SME_MULTICAST;
		hostif_mib_set_request(priv, LOCAL_MULTICAST_FILTER,
				       sizeof(filter_type), MIB_VALUE_TYPE_BOOL,
				       &filter_type);
	}

spin_unlock:
	spin_unlock(&priv->multicast_spin);
}

static
void hostif_sme_power_mgmt_set(struct ks_wlan_private *priv)
{
	unsigned long mode, wake_up, receive_dtims;

	switch (priv->reg.power_mgmt) {
	case POWER_MGMT_SAVE1:
		mode = (priv->reg.operation_mode == MODE_INFRASTRUCTURE) ?
			POWER_SAVE : POWER_ACTIVE;
		wake_up = 0;
		receive_dtims = 0;
		break;
	case POWER_MGMT_SAVE2:
		if (priv->reg.operation_mode == MODE_INFRASTRUCTURE) {
			mode = POWER_SAVE;
			wake_up = 0;
			receive_dtims = 1;
		} else {
			mode = POWER_ACTIVE;
			wake_up = 0;
			receive_dtims = 0;
		}
		break;
	case POWER_MGMT_ACTIVE:
	default:
		mode = POWER_ACTIVE;
		wake_up = 0;
		receive_dtims = 0;
		break;
	}
	hostif_power_mgmt_request(priv, mode, wake_up, receive_dtims);
}

static void hostif_sme_sleep_set(struct ks_wlan_private *priv)
{
	if (priv->sleep_mode != SLP_SLEEP &&
	    priv->sleep_mode != SLP_ACTIVE)
		return;

	hostif_sleep_request(priv, priv->sleep_mode);
}

static
void hostif_sme_set_key(struct ks_wlan_private *priv, int type)
{
	__le32 val;

	switch (type) {
	case SME_SET_FLAG:
		val = cpu_to_le32((uint32_t)(priv->reg.privacy_invoked));
		hostif_mib_set_request(priv, DOT11_PRIVACY_INVOKED,
				       sizeof(val), MIB_VALUE_TYPE_BOOL, &val);
		break;
	case SME_SET_TXKEY:
		val = cpu_to_le32((uint32_t)(priv->wpa.txkey));
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_ID,
				       sizeof(val), MIB_VALUE_TYPE_INT, &val);
		break;
	case SME_SET_KEY1:
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_VALUE1,
				       priv->wpa.key[0].key_len,
				       MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[0].key_val[0]);
		break;
	case SME_SET_KEY2:
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_VALUE2,
				       priv->wpa.key[1].key_len,
				       MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[1].key_val[0]);
		break;
	case SME_SET_KEY3:
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_VALUE3,
				       priv->wpa.key[2].key_len,
				       MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[2].key_val[0]);
		break;
	case SME_SET_KEY4:
		hostif_mib_set_request(priv, DOT11_WEP_DEFAULT_KEY_VALUE4,
				       priv->wpa.key[3].key_len,
				       MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[3].key_val[0]);
		break;
	case SME_SET_PMK_TSC:
		hostif_mib_set_request(priv, DOT11_PMK_TSC,
				       WPA_RX_SEQ_LEN, MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[0].rx_seq[0]);
		break;
	case SME_SET_GMK1_TSC:
		hostif_mib_set_request(priv, DOT11_GMK1_TSC,
				       WPA_RX_SEQ_LEN, MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[1].rx_seq[0]);
		break;
	case SME_SET_GMK2_TSC:
		hostif_mib_set_request(priv, DOT11_GMK2_TSC,
				       WPA_RX_SEQ_LEN, MIB_VALUE_TYPE_OSTRING,
				       &priv->wpa.key[2].rx_seq[0]);
		break;
	}
}

static
void hostif_sme_set_pmksa(struct ks_wlan_private *priv)
{
	struct pmk_cache {
		__le16 size;
		struct {
			u8 bssid[ETH_ALEN];
			u8 pmkid[IW_PMKID_LEN];
		} __packed list[PMK_LIST_MAX];
	} __packed pmkcache;
	struct pmk *pmk;
	int i = 0;

	list_for_each_entry(pmk, &priv->pmklist.head, list) {
		if (i >= PMK_LIST_MAX)
			break;
		ether_addr_copy(pmkcache.list[i].bssid, pmk->bssid);
		memcpy(pmkcache.list[i].pmkid, pmk->pmkid, IW_PMKID_LEN);
		i++;
	}
	pmkcache.size = cpu_to_le16((uint16_t)(priv->pmklist.size));
	hostif_mib_set_request(priv, LOCAL_PMK,
			       sizeof(priv->pmklist.size) + (ETH_ALEN +
							     IW_PMKID_LEN) *
			       (priv->pmklist.size), MIB_VALUE_TYPE_OSTRING,
			       &pmkcache);
}

/* execute sme */
static void hostif_sme_execute(struct ks_wlan_private *priv, int event)
{
	__le32 val;
	u16 failure;

	switch (event) {
	case SME_START:
		if (priv->dev_state == DEVICE_STATE_BOOT)
			hostif_mib_get_request(priv, DOT11_MAC_ADDRESS);
		break;
	case SME_MULTICAST_REQUEST:
		hostif_sme_multicast_set(priv);
		break;
	case SME_MACADDRESS_SET_REQUEST:
		hostif_mib_set_request(priv, LOCAL_CURRENTADDRESS, ETH_ALEN,
				       MIB_VALUE_TYPE_OSTRING,
				       &priv->eth_addr[0]);
		break;
	case SME_BSS_SCAN_REQUEST:
		hostif_bss_scan_request(priv, priv->reg.scan_type,
					priv->scan_ssid, priv->scan_ssid_len);
		break;
	case SME_POW_MNGMT_REQUEST:
		hostif_sme_power_mgmt_set(priv);
		break;
	case SME_PHY_INFO_REQUEST:
		hostif_phy_information_request(priv);
		break;
	case SME_MIC_FAILURE_REQUEST:
		failure = priv->wpa.mic_failure.failure;
		if (failure != 1 && failure != 2) {
			netdev_err(priv->net_dev,
				   "SME_MIC_FAILURE_REQUEST: failure count=%u error?\n",
				   failure);
			return;
		}
		hostif_mic_failure_request(priv, failure - 1, (failure == 1) ?
					    0 : priv->wpa.mic_failure.counter);
		break;
	case SME_MIC_FAILURE_CONFIRM:
		if (priv->wpa.mic_failure.failure == 2) {
			if (priv->wpa.mic_failure.stop)
				priv->wpa.mic_failure.stop = 0;
			priv->wpa.mic_failure.failure = 0;
			hostif_start_request(priv, priv->reg.operation_mode);
		}
		break;
	case SME_GET_MAC_ADDRESS:
		if (priv->dev_state == DEVICE_STATE_BOOT)
			hostif_mib_get_request(priv, DOT11_PRODUCT_VERSION);
		break;
	case SME_GET_PRODUCT_VERSION:
		if (priv->dev_state == DEVICE_STATE_BOOT)
			priv->dev_state = DEVICE_STATE_PREINIT;
		break;
	case SME_STOP_REQUEST:
		hostif_stop_request(priv);
		break;
	case SME_RTS_THRESHOLD_REQUEST:
		val = cpu_to_le32((uint32_t)(priv->reg.rts));
		hostif_mib_set_request(priv, DOT11_RTS_THRESHOLD,
				       sizeof(val), MIB_VALUE_TYPE_INT, &val);
		break;
	case SME_FRAGMENTATION_THRESHOLD_REQUEST:
		val = cpu_to_le32((uint32_t)(priv->reg.fragment));
		hostif_mib_set_request(priv, DOT11_FRAGMENTATION_THRESHOLD,
				       sizeof(val), MIB_VALUE_TYPE_INT, &val);
		break;
	case SME_WEP_INDEX_REQUEST:
	case SME_WEP_KEY1_REQUEST:
	case SME_WEP_KEY2_REQUEST:
	case SME_WEP_KEY3_REQUEST:
	case SME_WEP_KEY4_REQUEST:
	case SME_WEP_FLAG_REQUEST:
		hostif_sme_set_wep(priv, event);
		break;
	case SME_RSN_UCAST_REQUEST:
	case SME_RSN_MCAST_REQUEST:
	case SME_RSN_AUTH_REQUEST:
	case SME_RSN_ENABLED_REQUEST:
	case SME_RSN_MODE_REQUEST:
		hostif_sme_set_rsn(priv, event);
		break;
	case SME_SET_FLAG:
	case SME_SET_TXKEY:
	case SME_SET_KEY1:
	case SME_SET_KEY2:
	case SME_SET_KEY3:
	case SME_SET_KEY4:
	case SME_SET_PMK_TSC:
	case SME_SET_GMK1_TSC:
	case SME_SET_GMK2_TSC:
		hostif_sme_set_key(priv, event);
		break;
	case SME_SET_PMKSA:
		hostif_sme_set_pmksa(priv);
		break;
	case SME_WPS_ENABLE_REQUEST:
		hostif_mib_set_request(priv, LOCAL_WPS_ENABLE,
				       sizeof(priv->wps.wps_enabled),
				       MIB_VALUE_TYPE_INT,
				       &priv->wps.wps_enabled);
		break;
	case SME_WPS_PROBE_REQUEST:
		hostif_mib_set_request(priv, LOCAL_WPS_PROBE_REQ,
				       priv->wps.ielen,
				       MIB_VALUE_TYPE_OSTRING, priv->wps.ie);
		break;
	case SME_MODE_SET_REQUEST:
		hostif_sme_mode_setup(priv);
		break;
	case SME_SET_GAIN:
		hostif_mib_set_request(priv, LOCAL_GAIN,
				       sizeof(priv->gain),
				       MIB_VALUE_TYPE_OSTRING, &priv->gain);
		break;
	case SME_GET_GAIN:
		hostif_mib_get_request(priv, LOCAL_GAIN);
		break;
	case SME_GET_EEPROM_CKSUM:
		priv->eeprom_checksum = EEPROM_FW_NOT_SUPPORT;	/* initialize */
		hostif_mib_get_request(priv, LOCAL_EEPROM_SUM);
		break;
	case SME_START_REQUEST:
		hostif_start_request(priv, priv->reg.operation_mode);
		break;
	case SME_START_CONFIRM:
		/* for power save */
		atomic_set(&priv->psstatus.snooze_guard, 0);
		atomic_set(&priv->psstatus.confirm_wait, 0);
		if (priv->dev_state == DEVICE_STATE_PREINIT)
			priv->dev_state = DEVICE_STATE_INIT;
		/* wake_up_interruptible_all(&priv->confirm_wait); */
		complete(&priv->confirm_wait);
		break;
	case SME_SLEEP_REQUEST:
		hostif_sme_sleep_set(priv);
		break;
	case SME_SET_REGION:
		val = cpu_to_le32((uint32_t)(priv->region));
		hostif_mib_set_request(priv, LOCAL_REGION,
				       sizeof(val), MIB_VALUE_TYPE_INT, &val);
		break;
	case SME_MULTICAST_CONFIRM:
	case SME_BSS_SCAN_CONFIRM:
	case SME_POW_MNGMT_CONFIRM:
	case SME_PHY_INFO_CONFIRM:
	case SME_STOP_CONFIRM:
	case SME_RTS_THRESHOLD_CONFIRM:
	case SME_FRAGMENTATION_THRESHOLD_CONFIRM:
	case SME_WEP_INDEX_CONFIRM:
	case SME_WEP_KEY1_CONFIRM:
	case SME_WEP_KEY2_CONFIRM:
	case SME_WEP_KEY3_CONFIRM:
	case SME_WEP_KEY4_CONFIRM:
	case SME_WEP_FLAG_CONFIRM:
	case SME_RSN_UCAST_CONFIRM:
	case SME_RSN_MCAST_CONFIRM:
	case SME_RSN_AUTH_CONFIRM:
	case SME_RSN_ENABLED_CONFIRM:
	case SME_RSN_MODE_CONFIRM:
	case SME_MODE_SET_CONFIRM:
	case SME_TERMINATE:
	default:
		break;
	}
}

static
void hostif_sme_task(unsigned long dev)
{
	struct ks_wlan_private *priv = (struct ks_wlan_private *)dev;

	if (priv->dev_state < DEVICE_STATE_BOOT)
		return;

	if (cnt_smeqbody(priv) <= 0)
		return;

	hostif_sme_execute(priv, priv->sme_i.event_buff[priv->sme_i.qhead]);
	inc_smeqhead(priv);
	if (cnt_smeqbody(priv) > 0)
		tasklet_schedule(&priv->sme_task);
}

/* send to Station Management Entity module */
void hostif_sme_enqueue(struct ks_wlan_private *priv, u16 event)
{
	/* enqueue sme event */
	if (cnt_smeqbody(priv) < (SME_EVENT_BUFF_SIZE - 1)) {
		priv->sme_i.event_buff[priv->sme_i.qtail] = event;
		inc_smeqtail(priv);
	} else {
		/* in case of buffer overflow */
		netdev_err(priv->net_dev, "sme queue buffer overflow\n");
	}

	tasklet_schedule(&priv->sme_task);
}

static inline void hostif_aplist_init(struct ks_wlan_private *priv)
{
	size_t size = LOCAL_APLIST_MAX * sizeof(struct local_ap);
	priv->aplist.size = 0;
	memset(&priv->aplist.ap[0], 0, size);
}

static inline void hostif_status_init(struct ks_wlan_private *priv)
{
	priv->infra_status = 0;
	priv->current_rate = 4;
	priv->connect_status = DISCONNECT_STATUS;
}

static inline void hostif_sme_init(struct ks_wlan_private *priv)
{
	priv->sme_i.sme_status = SME_IDLE;
	priv->sme_i.qhead = 0;
	priv->sme_i.qtail = 0;
	spin_lock_init(&priv->sme_i.sme_spin);
	priv->sme_i.sme_flag = 0;
	tasklet_init(&priv->sme_task, hostif_sme_task, (unsigned long)priv);
}

static inline void hostif_wpa_init(struct ks_wlan_private *priv)
{
	memset(&priv->wpa, 0, sizeof(priv->wpa));
	priv->wpa.rsn_enabled = 0;
	priv->wpa.mic_failure.failure = 0;
	priv->wpa.mic_failure.last_failure_time = 0;
	priv->wpa.mic_failure.stop = 0;
}

static inline void hostif_power_save_init(struct ks_wlan_private *priv)
{
	atomic_set(&priv->psstatus.status, PS_NONE);
	atomic_set(&priv->psstatus.confirm_wait, 0);
	atomic_set(&priv->psstatus.snooze_guard, 0);
	init_completion(&priv->psstatus.wakeup_wait);
	INIT_WORK(&priv->wakeup_work, ks_wlan_hw_wakeup_task);
}

static inline void hostif_pmklist_init(struct ks_wlan_private *priv)
{
	int i;

	memset(&priv->pmklist, 0, sizeof(priv->pmklist));
	INIT_LIST_HEAD(&priv->pmklist.head);
	for (i = 0; i < PMK_LIST_MAX; i++)
		INIT_LIST_HEAD(&priv->pmklist.pmk[i].list);
}

static inline void hostif_counters_init(struct ks_wlan_private *priv)
{
	priv->dev_count = 0;
	atomic_set(&priv->event_count, 0);
	atomic_set(&priv->rec_count, 0);
}

int hostif_init(struct ks_wlan_private *priv)
{
	hostif_aplist_init(priv);
	hostif_status_init(priv);

	spin_lock_init(&priv->multicast_spin);
	spin_lock_init(&priv->dev_read_lock);
	init_waitqueue_head(&priv->devread_wait);

	hostif_counters_init(priv);
	hostif_power_save_init(priv);
	hostif_wpa_init(priv);
	hostif_pmklist_init(priv);
	hostif_sme_init(priv);

	return 0;
}

void hostif_exit(struct ks_wlan_private *priv)
{
	tasklet_kill(&priv->sme_task);
}
