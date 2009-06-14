/*
 * Driver for RNDIS based wireless USB devices.
 *
 * Copyright (C) 2007 by Bjorge Dijkstra <bjd@jooz.net>
 * Copyright (C) 2008 by Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Portions of this file are based on NDISwrapper project,
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *  http://ndiswrapper.sourceforge.net/
 */

// #define	DEBUG			// error path messages, extra info
// #define	VERBOSE			// more; success messages

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/usb/cdc.h>
#include <linux/wireless.h>
#include <linux/ieee80211.h>
#include <linux/if_arp.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <net/iw_handler.h>
#include <linux/usb/usbnet.h>
#include <linux/usb/rndis_host.h>


/* NOTE: All these are settings for Broadcom chipset */
static char modparam_country[4] = "EU";
module_param_string(country, modparam_country, 4, 0444);
MODULE_PARM_DESC(country, "Country code (ISO 3166-1 alpha-2), default: EU");

static int modparam_frameburst = 1;
module_param_named(frameburst, modparam_frameburst, int, 0444);
MODULE_PARM_DESC(frameburst, "enable frame bursting (default: on)");

static int modparam_afterburner = 0;
module_param_named(afterburner, modparam_afterburner, int, 0444);
MODULE_PARM_DESC(afterburner,
	"enable afterburner aka '125 High Speed Mode' (default: off)");

static int modparam_power_save = 0;
module_param_named(power_save, modparam_power_save, int, 0444);
MODULE_PARM_DESC(power_save,
	"set power save mode: 0=off, 1=on, 2=fast (default: off)");

static int modparam_power_output = 3;
module_param_named(power_output, modparam_power_output, int, 0444);
MODULE_PARM_DESC(power_output,
	"set power output: 0=25%, 1=50%, 2=75%, 3=100% (default: 100%)");

static int modparam_roamtrigger = -70;
module_param_named(roamtrigger, modparam_roamtrigger, int, 0444);
MODULE_PARM_DESC(roamtrigger,
	"set roaming dBm trigger: -80=optimize for distance, "
				"-60=bandwidth (default: -70)");

static int modparam_roamdelta = 1;
module_param_named(roamdelta, modparam_roamdelta, int, 0444);
MODULE_PARM_DESC(roamdelta,
	"set roaming tendency: 0=aggressive, 1=moderate, "
				"2=conservative (default: moderate)");

static int modparam_workaround_interval = 500;
module_param_named(workaround_interval, modparam_workaround_interval,
							int, 0444);
MODULE_PARM_DESC(workaround_interval,
	"set stall workaround interval in msecs (default: 500)");


/* various RNDIS OID defs */
#define OID_GEN_LINK_SPEED			cpu_to_le32(0x00010107)
#define OID_GEN_RNDIS_CONFIG_PARAMETER		cpu_to_le32(0x0001021b)

#define OID_GEN_XMIT_OK				cpu_to_le32(0x00020101)
#define OID_GEN_RCV_OK				cpu_to_le32(0x00020102)
#define OID_GEN_XMIT_ERROR			cpu_to_le32(0x00020103)
#define OID_GEN_RCV_ERROR			cpu_to_le32(0x00020104)
#define OID_GEN_RCV_NO_BUFFER			cpu_to_le32(0x00020105)

#define OID_802_3_PERMANENT_ADDRESS		cpu_to_le32(0x01010101)
#define OID_802_3_CURRENT_ADDRESS		cpu_to_le32(0x01010102)
#define OID_802_3_MULTICAST_LIST		cpu_to_le32(0x01010103)
#define OID_802_3_MAXIMUM_LIST_SIZE		cpu_to_le32(0x01010104)

#define OID_802_11_BSSID			cpu_to_le32(0x0d010101)
#define OID_802_11_SSID				cpu_to_le32(0x0d010102)
#define OID_802_11_INFRASTRUCTURE_MODE		cpu_to_le32(0x0d010108)
#define OID_802_11_ADD_WEP			cpu_to_le32(0x0d010113)
#define OID_802_11_REMOVE_WEP			cpu_to_le32(0x0d010114)
#define OID_802_11_DISASSOCIATE			cpu_to_le32(0x0d010115)
#define OID_802_11_AUTHENTICATION_MODE		cpu_to_le32(0x0d010118)
#define OID_802_11_PRIVACY_FILTER		cpu_to_le32(0x0d010119)
#define OID_802_11_BSSID_LIST_SCAN		cpu_to_le32(0x0d01011a)
#define OID_802_11_ENCRYPTION_STATUS		cpu_to_le32(0x0d01011b)
#define OID_802_11_ADD_KEY			cpu_to_le32(0x0d01011d)
#define OID_802_11_REMOVE_KEY			cpu_to_le32(0x0d01011e)
#define OID_802_11_ASSOCIATION_INFORMATION	cpu_to_le32(0x0d01011f)
#define OID_802_11_PMKID			cpu_to_le32(0x0d010123)
#define OID_802_11_NETWORK_TYPES_SUPPORTED	cpu_to_le32(0x0d010203)
#define OID_802_11_NETWORK_TYPE_IN_USE		cpu_to_le32(0x0d010204)
#define OID_802_11_TX_POWER_LEVEL		cpu_to_le32(0x0d010205)
#define OID_802_11_RSSI				cpu_to_le32(0x0d010206)
#define OID_802_11_RSSI_TRIGGER			cpu_to_le32(0x0d010207)
#define OID_802_11_FRAGMENTATION_THRESHOLD	cpu_to_le32(0x0d010209)
#define OID_802_11_RTS_THRESHOLD		cpu_to_le32(0x0d01020a)
#define OID_802_11_SUPPORTED_RATES		cpu_to_le32(0x0d01020e)
#define OID_802_11_CONFIGURATION		cpu_to_le32(0x0d010211)
#define OID_802_11_BSSID_LIST			cpu_to_le32(0x0d010217)


/* Typical noise/maximum signal level values taken from ndiswrapper iw_ndis.h */
#define	WL_NOISE	-96	/* typical noise level in dBm */
#define	WL_SIGMAX	-32	/* typical maximum signal level in dBm */


/* Assume that Broadcom 4320 (only chipset at time of writing known to be
 * based on wireless rndis) has default txpower of 13dBm.
 * This value is from Linksys WUSB54GSC User Guide, Appendix F: Specifications.
 *   13dBm == 19.9mW
 */
#define BCM4320_DEFAULT_TXPOWER 20


/* codes for "status" field of completion messages */
#define RNDIS_STATUS_ADAPTER_NOT_READY		cpu_to_le32(0xc0010011)
#define RNDIS_STATUS_ADAPTER_NOT_OPEN		cpu_to_le32(0xc0010012)


/* NDIS data structures. Taken from wpa_supplicant driver_ndis.c
 * slightly modified for datatype endianess, etc
 */
#define NDIS_802_11_LENGTH_SSID 32
#define NDIS_802_11_LENGTH_RATES 8
#define NDIS_802_11_LENGTH_RATES_EX 16

enum ndis_80211_net_type {
	ndis_80211_type_freq_hop,
	ndis_80211_type_direct_seq,
	ndis_80211_type_ofdm_a,
	ndis_80211_type_ofdm_g
};

enum ndis_80211_net_infra {
	ndis_80211_infra_adhoc,
	ndis_80211_infra_infra,
	ndis_80211_infra_auto_unknown
};

enum ndis_80211_auth_mode {
	ndis_80211_auth_open,
	ndis_80211_auth_shared,
	ndis_80211_auth_auto_switch,
	ndis_80211_auth_wpa,
	ndis_80211_auth_wpa_psk,
	ndis_80211_auth_wpa_none,
	ndis_80211_auth_wpa2,
	ndis_80211_auth_wpa2_psk
};

enum ndis_80211_encr_status {
	ndis_80211_encr_wep_enabled,
	ndis_80211_encr_disabled,
	ndis_80211_encr_wep_key_absent,
	ndis_80211_encr_not_supported,
	ndis_80211_encr_tkip_enabled,
	ndis_80211_encr_tkip_key_absent,
	ndis_80211_encr_ccmp_enabled,
	ndis_80211_encr_ccmp_key_absent
};

enum ndis_80211_priv_filter {
	ndis_80211_priv_accept_all,
	ndis_80211_priv_8021x_wep
};

struct ndis_80211_ssid {
	__le32 length;
	u8 essid[NDIS_802_11_LENGTH_SSID];
} __attribute__((packed));

struct ndis_80211_conf_freq_hop {
	__le32 length;
	__le32 hop_pattern;
	__le32 hop_set;
	__le32 dwell_time;
} __attribute__((packed));

struct ndis_80211_conf {
	__le32 length;
	__le32 beacon_period;
	__le32 atim_window;
	__le32 ds_config;
	struct ndis_80211_conf_freq_hop fh_config;
} __attribute__((packed));

struct ndis_80211_bssid_ex {
	__le32 length;
	u8 mac[6];
	u8 padding[2];
	struct ndis_80211_ssid ssid;
	__le32 privacy;
	__le32 rssi;
	__le32 net_type;
	struct ndis_80211_conf config;
	__le32 net_infra;
	u8 rates[NDIS_802_11_LENGTH_RATES_EX];
	__le32 ie_length;
	u8 ies[0];
} __attribute__((packed));

struct ndis_80211_bssid_list_ex {
	__le32 num_items;
	struct ndis_80211_bssid_ex bssid[0];
} __attribute__((packed));

struct ndis_80211_fixed_ies {
	u8 timestamp[8];
	__le16 beacon_interval;
	__le16 capabilities;
} __attribute__((packed));

struct ndis_80211_wep_key {
	__le32 size;
	__le32 index;
	__le32 length;
	u8 material[32];
} __attribute__((packed));

struct ndis_80211_key {
	__le32 size;
	__le32 index;
	__le32 length;
	u8 bssid[6];
	u8 padding[6];
	u8 rsc[8];
	u8 material[32];
} __attribute__((packed));

struct ndis_80211_remove_key {
	__le32 size;
	__le32 index;
	u8 bssid[6];
} __attribute__((packed));

struct ndis_config_param {
	__le32 name_offs;
	__le32 name_length;
	__le32 type;
	__le32 value_offs;
	__le32 value_length;
} __attribute__((packed));

struct ndis_80211_assoc_info {
	__le32 length;
	__le16 req_ies;
	struct req_ie {
		__le16 capa;
		__le16 listen_interval;
		u8 cur_ap_address[6];
	} req_ie;
	__le32 req_ie_length;
	__le32 offset_req_ies;
	__le16 resp_ies;
	struct resp_ie {
		__le16 capa;
		__le16 status_code;
		__le16 assoc_id;
	} resp_ie;
	__le32 resp_ie_length;
	__le32 offset_resp_ies;
} __attribute__((packed));

/* these have to match what is in wpa_supplicant */
enum wpa_alg { WPA_ALG_NONE, WPA_ALG_WEP, WPA_ALG_TKIP, WPA_ALG_CCMP };
enum wpa_cipher { CIPHER_NONE, CIPHER_WEP40, CIPHER_TKIP, CIPHER_CCMP,
		  CIPHER_WEP104 };
enum wpa_key_mgmt { KEY_MGMT_802_1X, KEY_MGMT_PSK, KEY_MGMT_NONE,
		    KEY_MGMT_802_1X_NO_WPA, KEY_MGMT_WPA_NONE };

/*
 *  private data
 */
#define NET_TYPE_11FB	0

#define CAP_MODE_80211A		1
#define CAP_MODE_80211B		2
#define CAP_MODE_80211G		4
#define CAP_MODE_MASK		7
#define CAP_SUPPORT_TXPOWER	8

#define WORK_LINK_UP		(1<<0)
#define WORK_LINK_DOWN		(1<<1)
#define WORK_SET_MULTICAST_LIST	(1<<2)

#define COMMAND_BUFFER_SIZE	(CONTROL_BUFFER_SIZE + sizeof(struct rndis_set))

/* RNDIS device private data */
struct rndis_wext_private {
	char name[32];

	struct usbnet *usbdev;

	struct workqueue_struct *workqueue;
	struct delayed_work stats_work;
	struct work_struct work;
	struct mutex command_lock;
	spinlock_t stats_lock;
	unsigned long work_pending;

	struct iw_statistics iwstats;
	struct iw_statistics privstats;

	int  nick_len;
	char nick[32];

	int caps;
	int multicast_size;

	/* module parameters */
	char param_country[4];
	int  param_frameburst;
	int  param_afterburner;
	int  param_power_save;
	int  param_power_output;
	int  param_roamtrigger;
	int  param_roamdelta;
	u32  param_workaround_interval;

	/* hardware state */
	int radio_on;
	int infra_mode;
	struct ndis_80211_ssid essid;

	/* encryption stuff */
	int  encr_tx_key_index;
	char encr_keys[4][32];
	int  encr_key_len[4];
	int  wpa_version;
	int  wpa_keymgmt;
	int  wpa_authalg;
	int  wpa_ie_len;
	u8  *wpa_ie;
	int  wpa_cipher_pair;
	int  wpa_cipher_group;

	u8 command_buffer[COMMAND_BUFFER_SIZE];
};


static const int rates_80211g[8] = { 6, 9, 12, 18, 24, 36, 48, 54 };

static const int bcm4320_power_output[4] = { 25, 50, 75, 100 };

static const unsigned char zero_bssid[ETH_ALEN] = {0,};
static const unsigned char ffff_bssid[ETH_ALEN] = { 0xff, 0xff, 0xff,
							0xff, 0xff, 0xff };


static struct rndis_wext_private *get_rndis_wext_priv(struct usbnet *dev)
{
	return (struct rndis_wext_private *)dev->driver_priv;
}


static u32 get_bcm4320_power(struct rndis_wext_private *priv)
{
	return BCM4320_DEFAULT_TXPOWER *
		bcm4320_power_output[priv->param_power_output] / 100;
}


/* translate error code */
static int rndis_error_status(__le32 rndis_status)
{
	int ret = -EINVAL;
	switch (rndis_status) {
	case RNDIS_STATUS_SUCCESS:
		ret = 0;
		break;
	case RNDIS_STATUS_FAILURE:
	case RNDIS_STATUS_INVALID_DATA:
		ret = -EINVAL;
		break;
	case RNDIS_STATUS_NOT_SUPPORTED:
		ret = -EOPNOTSUPP;
		break;
	case RNDIS_STATUS_ADAPTER_NOT_READY:
	case RNDIS_STATUS_ADAPTER_NOT_OPEN:
		ret = -EBUSY;
		break;
	}
	return ret;
}


static int rndis_query_oid(struct usbnet *dev, __le32 oid, void *data, int *len)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(dev);
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_query	*get;
		struct rndis_query_c	*get_c;
	} u;
	int ret, buflen;

	buflen = *len + sizeof(*u.get);
	if (buflen < CONTROL_BUFFER_SIZE)
		buflen = CONTROL_BUFFER_SIZE;

	if (buflen > COMMAND_BUFFER_SIZE) {
		u.buf = kmalloc(buflen, GFP_KERNEL);
		if (!u.buf)
			return -ENOMEM;
	} else {
		u.buf = priv->command_buffer;
	}

	mutex_lock(&priv->command_lock);

	memset(u.get, 0, sizeof *u.get);
	u.get->msg_type = RNDIS_MSG_QUERY;
	u.get->msg_len = cpu_to_le32(sizeof *u.get);
	u.get->oid = oid;

	ret = rndis_command(dev, u.header, buflen);
	if (ret == 0) {
		ret = le32_to_cpu(u.get_c->len);
		*len = (*len > ret) ? ret : *len;
		memcpy(data, u.buf + le32_to_cpu(u.get_c->offset) + 8, *len);
		ret = rndis_error_status(u.get_c->status);
	}

	mutex_unlock(&priv->command_lock);

	if (u.buf != priv->command_buffer)
		kfree(u.buf);
	return ret;
}


static int rndis_set_oid(struct usbnet *dev, __le32 oid, void *data, int len)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(dev);
	union {
		void			*buf;
		struct rndis_msg_hdr	*header;
		struct rndis_set	*set;
		struct rndis_set_c	*set_c;
	} u;
	int ret, buflen;

	buflen = len + sizeof(*u.set);
	if (buflen < CONTROL_BUFFER_SIZE)
		buflen = CONTROL_BUFFER_SIZE;

	if (buflen > COMMAND_BUFFER_SIZE) {
		u.buf = kmalloc(buflen, GFP_KERNEL);
		if (!u.buf)
			return -ENOMEM;
	} else {
		u.buf = priv->command_buffer;
	}

	mutex_lock(&priv->command_lock);

	memset(u.set, 0, sizeof *u.set);
	u.set->msg_type = RNDIS_MSG_SET;
	u.set->msg_len = cpu_to_le32(sizeof(*u.set) + len);
	u.set->oid = oid;
	u.set->len = cpu_to_le32(len);
	u.set->offset = cpu_to_le32(sizeof(*u.set) - 8);
	u.set->handle = cpu_to_le32(0);
	memcpy(u.buf + sizeof(*u.set), data, len);

	ret = rndis_command(dev, u.header, buflen);
	if (ret == 0)
		ret = rndis_error_status(u.set_c->status);

	mutex_unlock(&priv->command_lock);

	if (u.buf != priv->command_buffer)
		kfree(u.buf);
	return ret;
}


/*
 * Specs say that we can only set config parameters only soon after device
 * initialization.
 *   value_type: 0 = u32, 2 = unicode string
 */
static int rndis_set_config_parameter(struct usbnet *dev, char *param,
						int value_type, void *value)
{
	struct ndis_config_param *infobuf;
	int value_len, info_len, param_len, ret, i;
	__le16 *unibuf;
	__le32 *dst_value;

	if (value_type == 0)
		value_len = sizeof(__le32);
	else if (value_type == 2)
		value_len = strlen(value) * sizeof(__le16);
	else
		return -EINVAL;

	param_len = strlen(param) * sizeof(__le16);
	info_len = sizeof(*infobuf) + param_len + value_len;

#ifdef DEBUG
	info_len += 12;
#endif
	infobuf = kmalloc(info_len, GFP_KERNEL);
	if (!infobuf)
		return -ENOMEM;

#ifdef DEBUG
	info_len -= 12;
	/* extra 12 bytes are for padding (debug output) */
	memset(infobuf, 0xCC, info_len + 12);
#endif

	if (value_type == 2)
		devdbg(dev, "setting config parameter: %s, value: %s",
						param, (u8 *)value);
	else
		devdbg(dev, "setting config parameter: %s, value: %d",
						param, *(u32 *)value);

	infobuf->name_offs = cpu_to_le32(sizeof(*infobuf));
	infobuf->name_length = cpu_to_le32(param_len);
	infobuf->type = cpu_to_le32(value_type);
	infobuf->value_offs = cpu_to_le32(sizeof(*infobuf) + param_len);
	infobuf->value_length = cpu_to_le32(value_len);

	/* simple string to unicode string conversion */
	unibuf = (void *)infobuf + sizeof(*infobuf);
	for (i = 0; i < param_len / sizeof(__le16); i++)
		unibuf[i] = cpu_to_le16(param[i]);

	if (value_type == 2) {
		unibuf = (void *)infobuf + sizeof(*infobuf) + param_len;
		for (i = 0; i < value_len / sizeof(__le16); i++)
			unibuf[i] = cpu_to_le16(((u8 *)value)[i]);
	} else {
		dst_value = (void *)infobuf + sizeof(*infobuf) + param_len;
		*dst_value = cpu_to_le32(*(u32 *)value);
	}

#ifdef DEBUG
	devdbg(dev, "info buffer (len: %d):", info_len);
	for (i = 0; i < info_len; i += 12) {
		u32 *tmp = (u32 *)((u8 *)infobuf + i);
		devdbg(dev, "%08X:%08X:%08X",
			cpu_to_be32(tmp[0]),
			cpu_to_be32(tmp[1]),
			cpu_to_be32(tmp[2]));
	}
#endif

	ret = rndis_set_oid(dev, OID_GEN_RNDIS_CONFIG_PARAMETER,
							infobuf, info_len);
	if (ret != 0)
		devdbg(dev, "setting rndis config parameter failed, %d.", ret);

	kfree(infobuf);
	return ret;
}

static int rndis_set_config_parameter_str(struct usbnet *dev,
						char *param, char *value)
{
	return(rndis_set_config_parameter(dev, param, 2, value));
}

/*static int rndis_set_config_parameter_u32(struct usbnet *dev,
						char *param, u32 value)
{
	return(rndis_set_config_parameter(dev, param, 0, &value));
}*/


/*
 * data conversion functions
 */
static int level_to_qual(int level)
{
	int qual = 100 * (level - WL_NOISE) / (WL_SIGMAX - WL_NOISE);
	return qual >= 0 ? (qual <= 100 ? qual : 100) : 0;
}


static void dsconfig_to_freq(unsigned int dsconfig, struct iw_freq *freq)
{
	freq->e = 0;
	freq->i = 0;
	freq->flags = 0;

	/* see comment in wireless.h above the "struct iw_freq"
	 * definition for an explanation of this if
	 * NOTE: 1000000 is due to the kHz
	 */
	if (dsconfig > 1000000) {
		freq->m = dsconfig / 10;
		freq->e = 1;
	} else
		freq->m = dsconfig;

	/* convert from kHz to Hz */
	freq->e += 3;
}


static int freq_to_dsconfig(struct iw_freq *freq, unsigned int *dsconfig)
{
	if (freq->m < 1000 && freq->e == 0) {
		if (freq->m >= 1 && freq->m <= 14)
			*dsconfig = ieee80211_dsss_chan_to_freq(freq->m) * 1000;
		else
			return -1;
	} else {
		int i;
		*dsconfig = freq->m;
		for (i = freq->e; i > 0; i--)
			*dsconfig *= 10;
		*dsconfig /= 1000;
	}

	return 0;
}


/*
 * common functions
 */
static int
add_wep_key(struct usbnet *usbdev, char *key, int key_len, int index);

static int get_essid(struct usbnet *usbdev, struct ndis_80211_ssid *ssid)
{
	int ret, len;

	len = sizeof(*ssid);
	ret = rndis_query_oid(usbdev, OID_802_11_SSID, ssid, &len);

	if (ret != 0)
		ssid->length = 0;

#ifdef DEBUG
	{
		unsigned char tmp[NDIS_802_11_LENGTH_SSID + 1];

		memcpy(tmp, ssid->essid, le32_to_cpu(ssid->length));
		tmp[le32_to_cpu(ssid->length)] = 0;
		devdbg(usbdev, "get_essid: '%s', ret: %d", tmp, ret);
	}
#endif
	return ret;
}


static int set_essid(struct usbnet *usbdev, struct ndis_80211_ssid *ssid)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	int ret;

	ret = rndis_set_oid(usbdev, OID_802_11_SSID, ssid, sizeof(*ssid));
	if (ret == 0) {
		memcpy(&priv->essid, ssid, sizeof(priv->essid));
		priv->radio_on = 1;
		devdbg(usbdev, "set_essid: radio_on = 1");
	}

	return ret;
}


static int get_bssid(struct usbnet *usbdev, u8 bssid[ETH_ALEN])
{
	int ret, len;

	len = ETH_ALEN;
	ret = rndis_query_oid(usbdev, OID_802_11_BSSID, bssid, &len);

	if (ret != 0)
		memset(bssid, 0, ETH_ALEN);

	return ret;
}

static int get_association_info(struct usbnet *usbdev,
			struct ndis_80211_assoc_info *info, int len)
{
	return rndis_query_oid(usbdev, OID_802_11_ASSOCIATION_INFORMATION,
				info, &len);
}

static int is_associated(struct usbnet *usbdev)
{
	u8 bssid[ETH_ALEN];
	int ret;

	ret = get_bssid(usbdev, bssid);

	return(ret == 0 && memcmp(bssid, zero_bssid, ETH_ALEN) != 0);
}


static int disassociate(struct usbnet *usbdev, int reset_ssid)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct ndis_80211_ssid ssid;
	int i, ret = 0;

	if (priv->radio_on) {
		ret = rndis_set_oid(usbdev, OID_802_11_DISASSOCIATE, NULL, 0);
		if (ret == 0) {
			priv->radio_on = 0;
			devdbg(usbdev, "disassociate: radio_on = 0");

			if (reset_ssid)
				msleep(100);
		}
	}

	/* disassociate causes radio to be turned off; if reset_ssid
	 * is given, set random ssid to enable radio */
	if (reset_ssid) {
		ssid.length = cpu_to_le32(sizeof(ssid.essid));
		get_random_bytes(&ssid.essid[2], sizeof(ssid.essid)-2);
		ssid.essid[0] = 0x1;
		ssid.essid[1] = 0xff;
		for (i = 2; i < sizeof(ssid.essid); i++)
			ssid.essid[i] = 0x1 + (ssid.essid[i] * 0xfe / 0xff);
		ret = set_essid(usbdev, &ssid);
	}
	return ret;
}


static int set_auth_mode(struct usbnet *usbdev, int wpa_version, int authalg)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tmp;
	int auth_mode, ret;

	devdbg(usbdev, "set_auth_mode: wpa_version=0x%x authalg=0x%x "
		"keymgmt=0x%x", wpa_version, authalg, priv->wpa_keymgmt);

	if (wpa_version & IW_AUTH_WPA_VERSION_WPA2) {
		if (priv->wpa_keymgmt & IW_AUTH_KEY_MGMT_802_1X)
			auth_mode = ndis_80211_auth_wpa2;
		else
			auth_mode = ndis_80211_auth_wpa2_psk;
	} else if (wpa_version & IW_AUTH_WPA_VERSION_WPA) {
		if (priv->wpa_keymgmt & IW_AUTH_KEY_MGMT_802_1X)
			auth_mode = ndis_80211_auth_wpa;
		else if (priv->wpa_keymgmt & IW_AUTH_KEY_MGMT_PSK)
			auth_mode = ndis_80211_auth_wpa_psk;
		else
			auth_mode = ndis_80211_auth_wpa_none;
	} else if (authalg & IW_AUTH_ALG_SHARED_KEY) {
		if (authalg & IW_AUTH_ALG_OPEN_SYSTEM)
			auth_mode = ndis_80211_auth_auto_switch;
		else
			auth_mode = ndis_80211_auth_shared;
	} else
		auth_mode = ndis_80211_auth_open;

	tmp = cpu_to_le32(auth_mode);
	ret = rndis_set_oid(usbdev, OID_802_11_AUTHENTICATION_MODE, &tmp,
								sizeof(tmp));
	if (ret != 0) {
		devwarn(usbdev, "setting auth mode failed (%08X)", ret);
		return ret;
	}

	priv->wpa_version = wpa_version;
	priv->wpa_authalg = authalg;
	return 0;
}


static int set_priv_filter(struct usbnet *usbdev)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tmp;

	devdbg(usbdev, "set_priv_filter: wpa_version=0x%x", priv->wpa_version);

	if (priv->wpa_version & IW_AUTH_WPA_VERSION_WPA2 ||
	    priv->wpa_version & IW_AUTH_WPA_VERSION_WPA)
		tmp = cpu_to_le32(ndis_80211_priv_8021x_wep);
	else
		tmp = cpu_to_le32(ndis_80211_priv_accept_all);

	return rndis_set_oid(usbdev, OID_802_11_PRIVACY_FILTER, &tmp,
								sizeof(tmp));
}


static int set_encr_mode(struct usbnet *usbdev, int pairwise, int groupwise)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tmp;
	int encr_mode, ret;

	devdbg(usbdev, "set_encr_mode: cipher_pair=0x%x cipher_group=0x%x",
		pairwise,
		groupwise);

	if (pairwise & IW_AUTH_CIPHER_CCMP)
		encr_mode = ndis_80211_encr_ccmp_enabled;
	else if (pairwise & IW_AUTH_CIPHER_TKIP)
		encr_mode = ndis_80211_encr_tkip_enabled;
	else if (pairwise &
		 (IW_AUTH_CIPHER_WEP40 | IW_AUTH_CIPHER_WEP104))
		encr_mode = ndis_80211_encr_wep_enabled;
	else if (groupwise & IW_AUTH_CIPHER_CCMP)
		encr_mode = ndis_80211_encr_ccmp_enabled;
	else if (groupwise & IW_AUTH_CIPHER_TKIP)
		encr_mode = ndis_80211_encr_tkip_enabled;
	else
		encr_mode = ndis_80211_encr_disabled;

	tmp = cpu_to_le32(encr_mode);
	ret = rndis_set_oid(usbdev, OID_802_11_ENCRYPTION_STATUS, &tmp,
								sizeof(tmp));
	if (ret != 0) {
		devwarn(usbdev, "setting encr mode failed (%08X)", ret);
		return ret;
	}

	priv->wpa_cipher_pair = pairwise;
	priv->wpa_cipher_group = groupwise;
	return 0;
}


static int set_assoc_params(struct usbnet *usbdev)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	set_auth_mode(usbdev, priv->wpa_version, priv->wpa_authalg);
	set_priv_filter(usbdev);
	set_encr_mode(usbdev, priv->wpa_cipher_pair, priv->wpa_cipher_group);

	return 0;
}


static int set_infra_mode(struct usbnet *usbdev, int mode)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tmp;
	int ret, i;

	devdbg(usbdev, "set_infra_mode: infra_mode=0x%x", priv->infra_mode);

	tmp = cpu_to_le32(mode);
	ret = rndis_set_oid(usbdev, OID_802_11_INFRASTRUCTURE_MODE, &tmp,
								sizeof(tmp));
	if (ret != 0) {
		devwarn(usbdev, "setting infra mode failed (%08X)", ret);
		return ret;
	}

	/* NDIS drivers clear keys when infrastructure mode is
	 * changed. But Linux tools assume otherwise. So set the
	 * keys */
	if (priv->wpa_keymgmt == 0 ||
		priv->wpa_keymgmt == IW_AUTH_KEY_MGMT_802_1X) {
		for (i = 0; i < 4; i++) {
			if (priv->encr_key_len[i] > 0)
				add_wep_key(usbdev, priv->encr_keys[i],
						priv->encr_key_len[i], i);
		}
	}

	priv->infra_mode = mode;
	return 0;
}


static void set_default_iw_params(struct usbnet *usbdev)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	priv->wpa_keymgmt = 0;
	priv->wpa_version = 0;

	set_infra_mode(usbdev, ndis_80211_infra_infra);
	set_auth_mode(usbdev, IW_AUTH_WPA_VERSION_DISABLED,
				IW_AUTH_ALG_OPEN_SYSTEM);
	set_priv_filter(usbdev);
	set_encr_mode(usbdev, IW_AUTH_CIPHER_NONE, IW_AUTH_CIPHER_NONE);
}


static int deauthenticate(struct usbnet *usbdev)
{
	int ret;

	ret = disassociate(usbdev, 1);
	set_default_iw_params(usbdev);
	return ret;
}


/* index must be 0 - N, as per NDIS  */
static int add_wep_key(struct usbnet *usbdev, char *key, int key_len, int index)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct ndis_80211_wep_key ndis_key;
	int ret;

	if (key_len <= 0 || key_len > 32 || index < 0 || index >= 4)
		return -EINVAL;

	memset(&ndis_key, 0, sizeof(ndis_key));

	ndis_key.size = cpu_to_le32(sizeof(ndis_key));
	ndis_key.length = cpu_to_le32(key_len);
	ndis_key.index = cpu_to_le32(index);
	memcpy(&ndis_key.material, key, key_len);

	if (index == priv->encr_tx_key_index) {
		ndis_key.index |= cpu_to_le32(1 << 31);
		ret = set_encr_mode(usbdev, IW_AUTH_CIPHER_WEP104,
						IW_AUTH_CIPHER_NONE);
		if (ret)
			devwarn(usbdev, "encryption couldn't be enabled (%08X)",
									ret);
	}

	ret = rndis_set_oid(usbdev, OID_802_11_ADD_WEP, &ndis_key,
							sizeof(ndis_key));
	if (ret != 0) {
		devwarn(usbdev, "adding encryption key %d failed (%08X)",
							index+1, ret);
		return ret;
	}

	priv->encr_key_len[index] = key_len;
	memcpy(&priv->encr_keys[index], key, key_len);

	return 0;
}


/* remove_key is for both wep and wpa */
static int remove_key(struct usbnet *usbdev, int index, u8 bssid[ETH_ALEN])
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct ndis_80211_remove_key remove_key;
	__le32 keyindex;
	int ret;

	if (priv->encr_key_len[index] == 0)
		return 0;

	priv->encr_key_len[index] = 0;
	memset(&priv->encr_keys[index], 0, sizeof(priv->encr_keys[index]));

	if (priv->wpa_cipher_pair == IW_AUTH_CIPHER_TKIP ||
	    priv->wpa_cipher_pair == IW_AUTH_CIPHER_CCMP ||
	    priv->wpa_cipher_group == IW_AUTH_CIPHER_TKIP ||
	    priv->wpa_cipher_group == IW_AUTH_CIPHER_CCMP) {
		remove_key.size = cpu_to_le32(sizeof(remove_key));
		remove_key.index = cpu_to_le32(index);
		if (bssid) {
			/* pairwise key */
			if (memcmp(bssid, ffff_bssid, ETH_ALEN) != 0)
				remove_key.index |= cpu_to_le32(1 << 30);
			memcpy(remove_key.bssid, bssid,
					sizeof(remove_key.bssid));
		} else
			memset(remove_key.bssid, 0xff,
						sizeof(remove_key.bssid));

		ret = rndis_set_oid(usbdev, OID_802_11_REMOVE_KEY, &remove_key,
							sizeof(remove_key));
		if (ret != 0)
			return ret;
	} else {
		keyindex = cpu_to_le32(index);
		ret = rndis_set_oid(usbdev, OID_802_11_REMOVE_WEP, &keyindex,
							sizeof(keyindex));
		if (ret != 0) {
			devwarn(usbdev,
				"removing encryption key %d failed (%08X)",
				index, ret);
			return ret;
		}
	}

	/* if it is transmit key, disable encryption */
	if (index == priv->encr_tx_key_index)
		set_encr_mode(usbdev, IW_AUTH_CIPHER_NONE, IW_AUTH_CIPHER_NONE);

	return 0;
}


static void set_multicast_list(struct usbnet *usbdev)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct dev_mc_list *mclist;
	__le32 filter;
	int ret, i, size;
	char *buf;

	filter = RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_BROADCAST;

	if (usbdev->net->flags & IFF_PROMISC) {
		filter |= RNDIS_PACKET_TYPE_PROMISCUOUS |
			RNDIS_PACKET_TYPE_ALL_LOCAL;
	} else if (usbdev->net->flags & IFF_ALLMULTI ||
		   usbdev->net->mc_count > priv->multicast_size) {
		filter |= RNDIS_PACKET_TYPE_ALL_MULTICAST;
	} else if (usbdev->net->mc_count > 0) {
		size = min(priv->multicast_size, usbdev->net->mc_count);
		buf = kmalloc(size * ETH_ALEN, GFP_KERNEL);
		if (!buf) {
			devwarn(usbdev,
				"couldn't alloc %d bytes of memory",
				size * ETH_ALEN);
			return;
		}

		mclist = usbdev->net->mc_list;
		for (i = 0; i < size && mclist; mclist = mclist->next) {
			if (mclist->dmi_addrlen != ETH_ALEN)
				continue;

			memcpy(buf + i * ETH_ALEN, mclist->dmi_addr, ETH_ALEN);
			i++;
		}

		ret = rndis_set_oid(usbdev, OID_802_3_MULTICAST_LIST, buf,
								i * ETH_ALEN);
		if (ret == 0 && i > 0)
			filter |= RNDIS_PACKET_TYPE_MULTICAST;
		else
			filter |= RNDIS_PACKET_TYPE_ALL_MULTICAST;

		devdbg(usbdev, "OID_802_3_MULTICAST_LIST(%d, max: %d) -> %d",
						i, priv->multicast_size, ret);

		kfree(buf);
	}

	ret = rndis_set_oid(usbdev, OID_GEN_CURRENT_PACKET_FILTER, &filter,
							sizeof(filter));
	if (ret < 0) {
		devwarn(usbdev, "couldn't set packet filter: %08x",
							le32_to_cpu(filter));
	}

	devdbg(usbdev, "OID_GEN_CURRENT_PACKET_FILTER(%08x) -> %d",
						le32_to_cpu(filter), ret);
}


/*
 * wireless extension handlers
 */

static int rndis_iw_commit(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	/* dummy op */
	return 0;
}


static int rndis_iw_get_range(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_range *range = (struct iw_range *)extra;
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	int len, ret, i, j, num, has_80211g_rates;
	u8 rates[8];
	__le32 tx_power;

	devdbg(usbdev, "SIOCGIWRANGE");

	/* clear iw_range struct */
	memset(range, 0, sizeof(*range));
	wrqu->data.length = sizeof(*range);

	range->txpower_capa = IW_TXPOW_MWATT;
	range->num_txpower = 1;
	if (priv->caps & CAP_SUPPORT_TXPOWER) {
		len = sizeof(tx_power);
		ret = rndis_query_oid(usbdev, OID_802_11_TX_POWER_LEVEL,
						&tx_power, &len);
		if (ret == 0 && le32_to_cpu(tx_power) != 0xFF)
			range->txpower[0] = le32_to_cpu(tx_power);
		else
			range->txpower[0] = get_bcm4320_power(priv);
	} else
		range->txpower[0] = get_bcm4320_power(priv);

	len = sizeof(rates);
	ret = rndis_query_oid(usbdev, OID_802_11_SUPPORTED_RATES, &rates,
								&len);
	has_80211g_rates = 0;
	if (ret == 0) {
		j = 0;
		for (i = 0; i < len; i++) {
			if (rates[i] == 0)
				break;
			range->bitrate[j] = (rates[i] & 0x7f) * 500000;
			/* check for non 802.11b rates */
			if (range->bitrate[j] == 6000000 ||
				range->bitrate[j] == 9000000 ||
				(range->bitrate[j] >= 12000000 &&
				range->bitrate[j] != 22000000))
				has_80211g_rates = 1;
			j++;
		}
		range->num_bitrates = j;
	} else
		range->num_bitrates = 0;

	/* fill in 802.11g rates */
	if (has_80211g_rates) {
		num = range->num_bitrates;
		for (i = 0; i < ARRAY_SIZE(rates_80211g); i++) {
			for (j = 0; j < num; j++) {
				if (range->bitrate[j] ==
					rates_80211g[i] * 1000000)
					break;
			}
			if (j == num)
				range->bitrate[range->num_bitrates++] =
					rates_80211g[i] * 1000000;
			if (range->num_bitrates == IW_MAX_BITRATES)
				break;
		}

		/* estimated max real througput in bps */
		range->throughput = 54 * 1000 * 1000 / 2;

		/* ~35%	more with afterburner */
		if (priv->param_afterburner)
			range->throughput = range->throughput / 100 * 135;
	} else {
		/* estimated max real througput in bps */
		range->throughput = 11 * 1000 * 1000 / 2;
	}

	range->num_channels = 14;

	for (i = 0; (i < 14) && (i < IW_MAX_FREQUENCIES); i++) {
		range->freq[i].i = i + 1;
		range->freq[i].m = ieee80211_dsss_chan_to_freq(i + 1) * 100000;
		range->freq[i].e = 1;
	}
	range->num_frequency = i;

	range->min_rts = 0;
	range->max_rts = 2347;
	range->min_frag = 256;
	range->max_frag = 2346;

	range->max_qual.qual = 100;
	range->max_qual.level = 154;
	range->max_qual.updated = IW_QUAL_QUAL_UPDATED
				| IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_NOISE_INVALID;

	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = WIRELESS_EXT;

	range->enc_capa = IW_ENC_CAPA_WPA | IW_ENC_CAPA_WPA2 |
			IW_ENC_CAPA_CIPHER_TKIP | IW_ENC_CAPA_CIPHER_CCMP;
	return 0;
}


static int rndis_iw_get_name(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	strcpy(wrqu->name, priv->name);
	return 0;
}


static int rndis_iw_set_essid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *essid)
{
	struct ndis_80211_ssid ssid;
	int length = wrqu->essid.length;
	struct usbnet *usbdev = netdev_priv(dev);

	devdbg(usbdev, "SIOCSIWESSID: [flags:%d,len:%d] '%.32s'",
		wrqu->essid.flags, wrqu->essid.length, essid);

	if (length > NDIS_802_11_LENGTH_SSID)
		length = NDIS_802_11_LENGTH_SSID;

	ssid.length = cpu_to_le32(length);
	if (length > 0)
		memcpy(ssid.essid, essid, length);
	else
		memset(ssid.essid, 0, NDIS_802_11_LENGTH_SSID);

	set_assoc_params(usbdev);

	if (!wrqu->essid.flags || length == 0)
		return disassociate(usbdev, 1);
	else
		return set_essid(usbdev, &ssid);
}


static int rndis_iw_get_essid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *essid)
{
	struct ndis_80211_ssid ssid;
	struct usbnet *usbdev = netdev_priv(dev);
	int ret;

	ret = get_essid(usbdev, &ssid);

	if (ret == 0 && le32_to_cpu(ssid.length) > 0) {
		wrqu->essid.flags = 1;
		wrqu->essid.length = le32_to_cpu(ssid.length);
		memcpy(essid, ssid.essid, wrqu->essid.length);
		essid[wrqu->essid.length] = 0;
	} else {
		memset(essid, 0, sizeof(NDIS_802_11_LENGTH_SSID));
		wrqu->essid.flags = 0;
		wrqu->essid.length = 0;
	}
	devdbg(usbdev, "SIOCGIWESSID: %s", essid);
	return ret;
}


static int rndis_iw_get_bssid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	unsigned char bssid[ETH_ALEN];
	int ret;

	ret = get_bssid(usbdev, bssid);

	if (ret == 0)
		devdbg(usbdev, "SIOCGIWAP: %pM", bssid);
	else
		devdbg(usbdev, "SIOCGIWAP: <not associated>");

	wrqu->ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(wrqu->ap_addr.sa_data, bssid, ETH_ALEN);

	return ret;
}


static int rndis_iw_set_bssid(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	u8 *bssid = (u8 *)wrqu->ap_addr.sa_data;
	int ret;

	devdbg(usbdev, "SIOCSIWAP: %pM", bssid);

	ret = rndis_set_oid(usbdev, OID_802_11_BSSID, bssid, ETH_ALEN);

	/* user apps may set ap's mac address, which is not required;
	 * they may fail to work if this function fails, so return
	 * success */
	if (ret)
		devwarn(usbdev, "setting AP mac address failed (%08X)", ret);

	return 0;
}


static int rndis_iw_set_auth(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_param *p = &wrqu->param;
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	int ret = -ENOTSUPP;

	switch (p->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		devdbg(usbdev, "SIOCSIWAUTH: WPA_VERSION, %08x", p->value);
		priv->wpa_version = p->value;
		ret = 0;
		break;

	case IW_AUTH_CIPHER_PAIRWISE:
		devdbg(usbdev, "SIOCSIWAUTH: CIPHER_PAIRWISE, %08x", p->value);
		priv->wpa_cipher_pair = p->value;
		ret = 0;
		break;

	case IW_AUTH_CIPHER_GROUP:
		devdbg(usbdev, "SIOCSIWAUTH: CIPHER_GROUP, %08x", p->value);
		priv->wpa_cipher_group = p->value;
		ret = 0;
		break;

	case IW_AUTH_KEY_MGMT:
		devdbg(usbdev, "SIOCSIWAUTH: KEY_MGMT, %08x", p->value);
		priv->wpa_keymgmt = p->value;
		ret = 0;
		break;

	case IW_AUTH_TKIP_COUNTERMEASURES:
		devdbg(usbdev, "SIOCSIWAUTH: TKIP_COUNTERMEASURES, %08x",
								p->value);
		ret = 0;
		break;

	case IW_AUTH_DROP_UNENCRYPTED:
		devdbg(usbdev, "SIOCSIWAUTH: DROP_UNENCRYPTED, %08x", p->value);
		ret = 0;
		break;

	case IW_AUTH_80211_AUTH_ALG:
		devdbg(usbdev, "SIOCSIWAUTH: 80211_AUTH_ALG, %08x", p->value);
		priv->wpa_authalg = p->value;
		ret = 0;
		break;

	case IW_AUTH_WPA_ENABLED:
		devdbg(usbdev, "SIOCSIWAUTH: WPA_ENABLED, %08x", p->value);
		if (wrqu->param.value)
			deauthenticate(usbdev);
		ret = 0;
		break;

	case IW_AUTH_RX_UNENCRYPTED_EAPOL:
		devdbg(usbdev, "SIOCSIWAUTH: RX_UNENCRYPTED_EAPOL, %08x",
								p->value);
		ret = 0;
		break;

	case IW_AUTH_ROAMING_CONTROL:
		devdbg(usbdev, "SIOCSIWAUTH: ROAMING_CONTROL, %08x", p->value);
		ret = 0;
		break;

	case IW_AUTH_PRIVACY_INVOKED:
		devdbg(usbdev, "SIOCSIWAUTH: invalid cmd %d",
				wrqu->param.flags & IW_AUTH_INDEX);
		return -EOPNOTSUPP;

	default:
		devdbg(usbdev, "SIOCSIWAUTH: UNKNOWN  %08x, %08x",
			p->flags & IW_AUTH_INDEX, p->value);
	}
	return ret;
}


static int rndis_iw_get_auth(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_param *p = &wrqu->param;
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	switch (p->flags & IW_AUTH_INDEX) {
	case IW_AUTH_WPA_VERSION:
		p->value = priv->wpa_version;
		break;
	case IW_AUTH_CIPHER_PAIRWISE:
		p->value = priv->wpa_cipher_pair;
		break;
	case IW_AUTH_CIPHER_GROUP:
		p->value = priv->wpa_cipher_group;
		break;
	case IW_AUTH_KEY_MGMT:
		p->value = priv->wpa_keymgmt;
		break;
	case IW_AUTH_80211_AUTH_ALG:
		p->value = priv->wpa_authalg;
		break;
	default:
		devdbg(usbdev, "SIOCGIWAUTH: invalid cmd %d",
				wrqu->param.flags & IW_AUTH_INDEX);
		return -EOPNOTSUPP;
	}
	return 0;
}


static int rndis_iw_get_mode(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	switch (priv->infra_mode) {
	case ndis_80211_infra_adhoc:
		wrqu->mode = IW_MODE_ADHOC;
		break;
	case ndis_80211_infra_infra:
		wrqu->mode = IW_MODE_INFRA;
		break;
	/*case ndis_80211_infra_auto_unknown:*/
	default:
		wrqu->mode = IW_MODE_AUTO;
		break;
	}
	devdbg(usbdev, "SIOCGIWMODE: %08x", wrqu->mode);
	return 0;
}


static int rndis_iw_set_mode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	int mode;

	devdbg(usbdev, "SIOCSIWMODE: %08x", wrqu->mode);

	switch (wrqu->mode) {
	case IW_MODE_ADHOC:
		mode = ndis_80211_infra_adhoc;
		break;
	case IW_MODE_INFRA:
		mode = ndis_80211_infra_infra;
		break;
	/*case IW_MODE_AUTO:*/
	default:
		mode = ndis_80211_infra_auto_unknown;
		break;
	}

	return set_infra_mode(usbdev, mode);
}


static int rndis_iw_set_encode(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	int ret, index, key_len;
	u8 *key;

	index = (wrqu->encoding.flags & IW_ENCODE_INDEX);

	/* iwconfig gives index as 1 - N */
	if (index > 0)
		index--;
	else
		index = priv->encr_tx_key_index;

	if (index < 0 || index >= 4) {
		devwarn(usbdev, "encryption index out of range (%u)", index);
		return -EINVAL;
	}

	/* remove key if disabled */
	if (wrqu->data.flags & IW_ENCODE_DISABLED) {
		if (remove_key(usbdev, index, NULL))
			return -EINVAL;
		else
			return 0;
	}

	/* global encryption state (for all keys) */
	if (wrqu->data.flags & IW_ENCODE_OPEN)
		ret = set_auth_mode(usbdev, IW_AUTH_WPA_VERSION_DISABLED,
						IW_AUTH_ALG_OPEN_SYSTEM);
	else /*if (wrqu->data.flags & IW_ENCODE_RESTRICTED)*/
		ret = set_auth_mode(usbdev, IW_AUTH_WPA_VERSION_DISABLED,
						IW_AUTH_ALG_SHARED_KEY);
	if (ret != 0)
		return ret;

	if (wrqu->data.length > 0) {
		key_len = wrqu->data.length;
		key = extra;
	} else {
		/* must be set as tx key */
		if (priv->encr_key_len[index] == 0)
			return -EINVAL;
		key_len = priv->encr_key_len[index];
		key = priv->encr_keys[index];
		priv->encr_tx_key_index = index;
	}

	if (add_wep_key(usbdev, key, key_len, index) != 0)
		return -EINVAL;

	if (index == priv->encr_tx_key_index)
		/* ndis drivers want essid to be set after setting encr */
		set_essid(usbdev, &priv->essid);

	return 0;
}


static int rndis_iw_set_encode_ext(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct iw_encode_ext *ext = (struct iw_encode_ext *)extra;
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct ndis_80211_key ndis_key;
	int keyidx, ret;
	u8 *addr;

	keyidx = wrqu->encoding.flags & IW_ENCODE_INDEX;

	/* iwconfig gives index as 1 - N */
	if (keyidx)
		keyidx--;
	else
		keyidx = priv->encr_tx_key_index;

	if (keyidx < 0 || keyidx >= 4)
		return -EINVAL;

	if (ext->alg == WPA_ALG_WEP) {
		if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
			priv->encr_tx_key_index = keyidx;
		return add_wep_key(usbdev, ext->key, ext->key_len, keyidx);
	}

	if ((wrqu->encoding.flags & IW_ENCODE_DISABLED) ||
	    ext->alg == IW_ENCODE_ALG_NONE || ext->key_len == 0)
		return remove_key(usbdev, keyidx, NULL);

	if (ext->key_len > sizeof(ndis_key.material))
		return -1;

	memset(&ndis_key, 0, sizeof(ndis_key));

	ndis_key.size = cpu_to_le32(sizeof(ndis_key) -
				sizeof(ndis_key.material) + ext->key_len);
	ndis_key.length = cpu_to_le32(ext->key_len);
	ndis_key.index = cpu_to_le32(keyidx);

	if (ext->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
		memcpy(ndis_key.rsc, ext->rx_seq, 6);
		ndis_key.index |= cpu_to_le32(1 << 29);
	}

	addr = ext->addr.sa_data;
	if (ext->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
		/* group key */
		if (priv->infra_mode == ndis_80211_infra_adhoc)
			memset(ndis_key.bssid, 0xff, ETH_ALEN);
		else
			get_bssid(usbdev, ndis_key.bssid);
	} else {
		/* pairwise key */
		ndis_key.index |= cpu_to_le32(1 << 30);
		memcpy(ndis_key.bssid, addr, ETH_ALEN);
	}

	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		ndis_key.index |= cpu_to_le32(1 << 31);

	if (ext->alg == IW_ENCODE_ALG_TKIP && ext->key_len == 32) {
		/* wpa_supplicant gives us the Michael MIC RX/TX keys in
		 * different order than NDIS spec, so swap the order here. */
		memcpy(ndis_key.material, ext->key, 16);
		memcpy(ndis_key.material + 16, ext->key + 24, 8);
		memcpy(ndis_key.material + 24, ext->key + 16, 8);
	} else
		memcpy(ndis_key.material, ext->key, ext->key_len);

	ret = rndis_set_oid(usbdev, OID_802_11_ADD_KEY, &ndis_key,
					le32_to_cpu(ndis_key.size));
	devdbg(usbdev, "SIOCSIWENCODEEXT: OID_802_11_ADD_KEY -> %08X", ret);
	if (ret != 0)
		return ret;

	priv->encr_key_len[keyidx] = ext->key_len;
	memcpy(&priv->encr_keys[keyidx], ndis_key.material, ext->key_len);
	if (ext->ext_flags & IW_ENCODE_EXT_SET_TX_KEY)
		priv->encr_tx_key_index = keyidx;

	return 0;
}


static int rndis_iw_set_scan(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	union iwreq_data evt;
	int ret = -EINVAL;
	__le32 tmp;

	devdbg(usbdev, "SIOCSIWSCAN");

	if (wrqu->data.flags == 0) {
		tmp = cpu_to_le32(1);
		ret = rndis_set_oid(usbdev, OID_802_11_BSSID_LIST_SCAN, &tmp,
								sizeof(tmp));
		evt.data.flags = 0;
		evt.data.length = 0;
		wireless_send_event(dev, SIOCGIWSCAN, &evt, NULL);
	}
	return ret;
}


static char *rndis_translate_scan(struct net_device *dev,
				  struct iw_request_info *info, char *cev,
				  char *end_buf,
				  struct ndis_80211_bssid_ex *bssid)
{
	struct usbnet *usbdev = netdev_priv(dev);
	u8 *ie;
	char *current_val;
	int bssid_len, ie_len, i;
	u32 beacon, atim;
	struct iw_event iwe;
	unsigned char sbuf[32];

	bssid_len = le32_to_cpu(bssid->length);

	devdbg(usbdev, "BSSID %pM", bssid->mac);
	iwe.cmd = SIOCGIWAP;
	iwe.u.ap_addr.sa_family = ARPHRD_ETHER;
	memcpy(iwe.u.ap_addr.sa_data, bssid->mac, ETH_ALEN);
	cev = iwe_stream_add_event(info, cev, end_buf, &iwe, IW_EV_ADDR_LEN);

	devdbg(usbdev, "SSID(%d) %s", le32_to_cpu(bssid->ssid.length),
						bssid->ssid.essid);
	iwe.cmd = SIOCGIWESSID;
	iwe.u.essid.length = le32_to_cpu(bssid->ssid.length);
	iwe.u.essid.flags = 1;
	cev = iwe_stream_add_point(info, cev, end_buf, &iwe, bssid->ssid.essid);

	devdbg(usbdev, "MODE %d", le32_to_cpu(bssid->net_infra));
	iwe.cmd = SIOCGIWMODE;
	switch (le32_to_cpu(bssid->net_infra)) {
	case ndis_80211_infra_adhoc:
		iwe.u.mode = IW_MODE_ADHOC;
		break;
	case ndis_80211_infra_infra:
		iwe.u.mode = IW_MODE_INFRA;
		break;
	/*case ndis_80211_infra_auto_unknown:*/
	default:
		iwe.u.mode = IW_MODE_AUTO;
		break;
	}
	cev = iwe_stream_add_event(info, cev, end_buf, &iwe, IW_EV_UINT_LEN);

	devdbg(usbdev, "FREQ %d kHz", le32_to_cpu(bssid->config.ds_config));
	iwe.cmd = SIOCGIWFREQ;
	dsconfig_to_freq(le32_to_cpu(bssid->config.ds_config), &iwe.u.freq);
	cev = iwe_stream_add_event(info, cev, end_buf, &iwe, IW_EV_FREQ_LEN);

	devdbg(usbdev, "QUAL %d", le32_to_cpu(bssid->rssi));
	iwe.cmd = IWEVQUAL;
	iwe.u.qual.qual  = level_to_qual(le32_to_cpu(bssid->rssi));
	iwe.u.qual.level = le32_to_cpu(bssid->rssi);
	iwe.u.qual.updated = IW_QUAL_QUAL_UPDATED
			| IW_QUAL_LEVEL_UPDATED
			| IW_QUAL_NOISE_INVALID;
	cev = iwe_stream_add_event(info, cev, end_buf, &iwe, IW_EV_QUAL_LEN);

	devdbg(usbdev, "ENCODE %d", le32_to_cpu(bssid->privacy));
	iwe.cmd = SIOCGIWENCODE;
	iwe.u.data.length = 0;
	if (le32_to_cpu(bssid->privacy) == ndis_80211_priv_accept_all)
		iwe.u.data.flags = IW_ENCODE_DISABLED;
	else
		iwe.u.data.flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;

	cev = iwe_stream_add_point(info, cev, end_buf, &iwe, NULL);

	devdbg(usbdev, "RATES:");
	current_val = cev + iwe_stream_lcp_len(info);
	iwe.cmd = SIOCGIWRATE;
	for (i = 0; i < sizeof(bssid->rates); i++) {
		if (bssid->rates[i] & 0x7f) {
			iwe.u.bitrate.value =
				((bssid->rates[i] & 0x7f) *
				500000);
			devdbg(usbdev, " %d", iwe.u.bitrate.value);
			current_val = iwe_stream_add_value(info, cev,
				current_val, end_buf, &iwe,
				IW_EV_PARAM_LEN);
		}
	}

	if ((current_val - cev) > iwe_stream_lcp_len(info))
		cev = current_val;

	beacon = le32_to_cpu(bssid->config.beacon_period);
	devdbg(usbdev, "BCN_INT %d", beacon);
	iwe.cmd = IWEVCUSTOM;
	snprintf(sbuf, sizeof(sbuf), "bcn_int=%d", beacon);
	iwe.u.data.length = strlen(sbuf);
	cev = iwe_stream_add_point(info, cev, end_buf, &iwe, sbuf);

	atim = le32_to_cpu(bssid->config.atim_window);
	devdbg(usbdev, "ATIM %d", atim);
	iwe.cmd = IWEVCUSTOM;
	snprintf(sbuf, sizeof(sbuf), "atim=%u", atim);
	iwe.u.data.length = strlen(sbuf);
	cev = iwe_stream_add_point(info, cev, end_buf, &iwe, sbuf);

	ie = (void *)(bssid->ies + sizeof(struct ndis_80211_fixed_ies));
	ie_len = min(bssid_len - (int)sizeof(*bssid),
					(int)le32_to_cpu(bssid->ie_length));
	ie_len -= sizeof(struct ndis_80211_fixed_ies);
	while (ie_len >= 2 && 2 + ie[1] <= ie_len) {
		if ((ie[0] == WLAN_EID_GENERIC && ie[1] >= 4 &&
		     memcmp(ie + 2, "\x00\x50\xf2\x01", 4) == 0) ||
		    ie[0] == WLAN_EID_RSN) {
			devdbg(usbdev, "IE: WPA%d",
					(ie[0] == WLAN_EID_RSN) ? 2 : 1);
			iwe.cmd = IWEVGENIE;
			/* arbitrary cut-off at 64 */
			iwe.u.data.length = min(ie[1] + 2, 64);
			cev = iwe_stream_add_point(info, cev, end_buf, &iwe, ie);
		}

		ie_len -= 2 + ie[1];
		ie += 2 + ie[1];
	}

	return cev;
}


static int rndis_iw_get_scan(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	void *buf = NULL;
	char *cev = extra;
	struct ndis_80211_bssid_list_ex *bssid_list;
	struct ndis_80211_bssid_ex *bssid;
	int ret = -EINVAL, len, count, bssid_len;

	devdbg(usbdev, "SIOCGIWSCAN");

	len = CONTROL_BUFFER_SIZE;
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}

	ret = rndis_query_oid(usbdev, OID_802_11_BSSID_LIST, buf, &len);

	if (ret != 0)
		goto out;

	bssid_list = buf;
	bssid = bssid_list->bssid;
	bssid_len = le32_to_cpu(bssid->length);
	count = le32_to_cpu(bssid_list->num_items);
	devdbg(usbdev, "SIOCGIWSCAN: %d BSSIDs found", count);

	while (count && ((void *)bssid + bssid_len) <= (buf + len)) {
		cev = rndis_translate_scan(dev, info, cev,
					   extra + IW_SCAN_MAX_DATA, bssid);
		bssid = (void *)bssid + bssid_len;
		bssid_len = le32_to_cpu(bssid->length);
		count--;
	}

out:
	wrqu->data.length = cev - extra;
	wrqu->data.flags = 0;
	kfree(buf);
	return ret;
}


static int rndis_iw_set_genie(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	int ret = 0;

#ifdef DEBUG
	int j;
	u8 *gie = extra;
	for (j = 0; j < wrqu->data.length; j += 8)
		devdbg(usbdev,
			"SIOCSIWGENIE %04x - "
			"%02x %02x %02x %02x %02x %02x %02x %02x", j,
			gie[j + 0], gie[j + 1], gie[j + 2], gie[j + 3],
			gie[j + 4], gie[j + 5], gie[j + 6], gie[j + 7]);
#endif
	/* clear existing IEs */
	if (priv->wpa_ie_len) {
		kfree(priv->wpa_ie);
		priv->wpa_ie_len = 0;
	}

	/* set new IEs */
	priv->wpa_ie = kmalloc(wrqu->data.length, GFP_KERNEL);
	if (priv->wpa_ie) {
		priv->wpa_ie_len = wrqu->data.length;
		memcpy(priv->wpa_ie, extra, priv->wpa_ie_len);
	} else
		ret = -ENOMEM;
	return ret;
}


static int rndis_iw_get_genie(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	devdbg(usbdev, "SIOCGIWGENIE");

	if (priv->wpa_ie_len == 0 || priv->wpa_ie == NULL) {
		wrqu->data.length = 0;
		return 0;
	}

	if (wrqu->data.length < priv->wpa_ie_len)
		return -E2BIG;

	wrqu->data.length = priv->wpa_ie_len;
	memcpy(extra, priv->wpa_ie, priv->wpa_ie_len);

	return 0;
}


static int rndis_iw_set_rts(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	__le32 tmp;
	devdbg(usbdev, "SIOCSIWRTS");

	tmp = cpu_to_le32(wrqu->rts.value);
	return rndis_set_oid(usbdev, OID_802_11_RTS_THRESHOLD, &tmp,
								sizeof(tmp));
}


static int rndis_iw_get_rts(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	__le32 tmp;
	int len, ret;

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_802_11_RTS_THRESHOLD, &tmp, &len);
	if (ret == 0) {
		wrqu->rts.value = le32_to_cpu(tmp);
		wrqu->rts.flags = 1;
		wrqu->rts.disabled = 0;
	}

	devdbg(usbdev, "SIOCGIWRTS: %d", wrqu->rts.value);

	return ret;
}


static int rndis_iw_set_frag(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	__le32 tmp;

	devdbg(usbdev, "SIOCSIWFRAG");

	tmp = cpu_to_le32(wrqu->frag.value);
	return rndis_set_oid(usbdev, OID_802_11_FRAGMENTATION_THRESHOLD, &tmp,
								sizeof(tmp));
}


static int rndis_iw_get_frag(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	__le32 tmp;
	int len, ret;

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_802_11_FRAGMENTATION_THRESHOLD, &tmp,
									&len);
	if (ret == 0) {
		wrqu->frag.value = le32_to_cpu(tmp);
		wrqu->frag.flags = 1;
		wrqu->frag.disabled = 0;
	}
	devdbg(usbdev, "SIOCGIWFRAG: %d", wrqu->frag.value);
	return ret;
}


static int rndis_iw_set_nick(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	devdbg(usbdev, "SIOCSIWNICK");

	priv->nick_len = wrqu->data.length;
	if (priv->nick_len > 32)
		priv->nick_len = 32;

	memcpy(priv->nick, extra, priv->nick_len);
	return 0;
}


static int rndis_iw_get_nick(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	wrqu->data.flags = 1;
	wrqu->data.length = priv->nick_len;
	memcpy(extra, priv->nick, priv->nick_len);

	devdbg(usbdev, "SIOCGIWNICK: '%s'", priv->nick);

	return 0;
}


static int rndis_iw_set_freq(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct ndis_80211_conf config;
	unsigned int dsconfig;
	int len, ret;

	/* this OID is valid only when not associated */
	if (is_associated(usbdev))
		return 0;

	dsconfig = 0;
	if (freq_to_dsconfig(&wrqu->freq, &dsconfig))
		return -EINVAL;

	len = sizeof(config);
	ret = rndis_query_oid(usbdev, OID_802_11_CONFIGURATION, &config, &len);
	if (ret != 0) {
		devdbg(usbdev, "SIOCSIWFREQ: querying configuration failed");
		return 0;
	}

	config.ds_config = cpu_to_le32(dsconfig);

	devdbg(usbdev, "SIOCSIWFREQ: %d * 10^%d", wrqu->freq.m, wrqu->freq.e);
	return rndis_set_oid(usbdev, OID_802_11_CONFIGURATION, &config,
								sizeof(config));
}


static int rndis_iw_get_freq(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct ndis_80211_conf config;
	int len, ret;

	len = sizeof(config);
	ret = rndis_query_oid(usbdev, OID_802_11_CONFIGURATION, &config, &len);
	if (ret == 0)
		dsconfig_to_freq(le32_to_cpu(config.ds_config), &wrqu->freq);

	devdbg(usbdev, "SIOCGIWFREQ: %d", wrqu->freq.m);
	return ret;
}


static int rndis_iw_get_txpower(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tx_power;
	int ret = 0, len;

	if (priv->radio_on) {
		if (priv->caps & CAP_SUPPORT_TXPOWER) {
			len = sizeof(tx_power);
			ret = rndis_query_oid(usbdev, OID_802_11_TX_POWER_LEVEL,
							&tx_power, &len);
			if (ret != 0)
				return ret;
		} else
			/* fake incase not supported */
			tx_power = cpu_to_le32(get_bcm4320_power(priv));

		wrqu->txpower.flags = IW_TXPOW_MWATT;
		wrqu->txpower.value = le32_to_cpu(tx_power);
		wrqu->txpower.disabled = 0;
	} else {
		wrqu->txpower.flags = IW_TXPOW_MWATT;
		wrqu->txpower.value = 0;
		wrqu->txpower.disabled = 1;
	}

	devdbg(usbdev, "SIOCGIWTXPOW: %d", wrqu->txpower.value);

	return ret;
}


static int rndis_iw_set_txpower(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	__le32 tx_power = 0;
	int ret = 0;

	if (!wrqu->txpower.disabled) {
		if (wrqu->txpower.flags == IW_TXPOW_MWATT)
			tx_power = cpu_to_le32(wrqu->txpower.value);
		else { /* wrqu->txpower.flags == IW_TXPOW_DBM */
			if (wrqu->txpower.value > 20)
				tx_power = cpu_to_le32(128);
			else if (wrqu->txpower.value < -43)
				tx_power = cpu_to_le32(127);
			else {
				signed char tmp;
				tmp = wrqu->txpower.value;
				tmp = -12 - tmp;
				tmp <<= 2;
				tx_power = cpu_to_le32((unsigned char)tmp);
			}
		}
	}

	devdbg(usbdev, "SIOCSIWTXPOW: %d", le32_to_cpu(tx_power));

	if (le32_to_cpu(tx_power) != 0) {
		if (priv->caps & CAP_SUPPORT_TXPOWER) {
			/* turn radio on first */
			if (!priv->radio_on)
				disassociate(usbdev, 1);

			ret = rndis_set_oid(usbdev, OID_802_11_TX_POWER_LEVEL,
						&tx_power, sizeof(tx_power));
			if (ret != 0)
				ret = -EOPNOTSUPP;
			return ret;
		} else {
			/* txpower unsupported, just turn radio on */
			if (!priv->radio_on)
				return disassociate(usbdev, 1);
			return 0; /* all ready on */
		}
	}

	/* tx_power == 0, turn off radio */
	return disassociate(usbdev, 0);
}


static int rndis_iw_get_rate(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	__le32 tmp;
	int ret, len;

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_GEN_LINK_SPEED, &tmp, &len);
	if (ret == 0) {
		wrqu->bitrate.value = le32_to_cpu(tmp) * 100;
		wrqu->bitrate.disabled = 0;
		wrqu->bitrate.flags = 1;
	}
	return ret;
}


static int rndis_iw_set_mlme(struct net_device *dev,
    struct iw_request_info *info, union iwreq_data *wrqu, char *extra)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	struct iw_mlme *mlme = (struct iw_mlme *)extra;
	unsigned char bssid[ETH_ALEN];

	get_bssid(usbdev, bssid);

	if (memcmp(bssid, mlme->addr.sa_data, ETH_ALEN))
		return -EINVAL;

	switch (mlme->cmd) {
	case IW_MLME_DEAUTH:
		return deauthenticate(usbdev);
	case IW_MLME_DISASSOC:
		return disassociate(usbdev, priv->radio_on);
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}


static struct iw_statistics *rndis_get_wireless_stats(struct net_device *dev)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	unsigned long flags;

	spin_lock_irqsave(&priv->stats_lock, flags);
	memcpy(&priv->iwstats, &priv->privstats, sizeof(priv->iwstats));
	spin_unlock_irqrestore(&priv->stats_lock, flags);

	return &priv->iwstats;
}


#define IW_IOCTL(x) [(x) - SIOCSIWCOMMIT]
static const iw_handler rndis_iw_handler[] =
{
	IW_IOCTL(SIOCSIWCOMMIT)    = rndis_iw_commit,
	IW_IOCTL(SIOCGIWNAME)      = rndis_iw_get_name,
	IW_IOCTL(SIOCSIWFREQ)      = rndis_iw_set_freq,
	IW_IOCTL(SIOCGIWFREQ)      = rndis_iw_get_freq,
	IW_IOCTL(SIOCSIWMODE)      = rndis_iw_set_mode,
	IW_IOCTL(SIOCGIWMODE)      = rndis_iw_get_mode,
	IW_IOCTL(SIOCGIWRANGE)     = rndis_iw_get_range,
	IW_IOCTL(SIOCSIWAP)        = rndis_iw_set_bssid,
	IW_IOCTL(SIOCGIWAP)        = rndis_iw_get_bssid,
	IW_IOCTL(SIOCSIWSCAN)      = rndis_iw_set_scan,
	IW_IOCTL(SIOCGIWSCAN)      = rndis_iw_get_scan,
	IW_IOCTL(SIOCSIWESSID)     = rndis_iw_set_essid,
	IW_IOCTL(SIOCGIWESSID)     = rndis_iw_get_essid,
	IW_IOCTL(SIOCSIWNICKN)     = rndis_iw_set_nick,
	IW_IOCTL(SIOCGIWNICKN)     = rndis_iw_get_nick,
	IW_IOCTL(SIOCGIWRATE)      = rndis_iw_get_rate,
	IW_IOCTL(SIOCSIWRTS)       = rndis_iw_set_rts,
	IW_IOCTL(SIOCGIWRTS)       = rndis_iw_get_rts,
	IW_IOCTL(SIOCSIWFRAG)      = rndis_iw_set_frag,
	IW_IOCTL(SIOCGIWFRAG)      = rndis_iw_get_frag,
	IW_IOCTL(SIOCSIWTXPOW)     = rndis_iw_set_txpower,
	IW_IOCTL(SIOCGIWTXPOW)     = rndis_iw_get_txpower,
	IW_IOCTL(SIOCSIWENCODE)    = rndis_iw_set_encode,
	IW_IOCTL(SIOCSIWENCODEEXT) = rndis_iw_set_encode_ext,
	IW_IOCTL(SIOCSIWAUTH)      = rndis_iw_set_auth,
	IW_IOCTL(SIOCGIWAUTH)      = rndis_iw_get_auth,
	IW_IOCTL(SIOCSIWGENIE)     = rndis_iw_set_genie,
	IW_IOCTL(SIOCGIWGENIE)     = rndis_iw_get_genie,
	IW_IOCTL(SIOCSIWMLME)      = rndis_iw_set_mlme,
};

static const iw_handler rndis_wext_private_handler[] = {
};

static const struct iw_priv_args rndis_wext_private_args[] = {
};


static const struct iw_handler_def rndis_iw_handlers = {
	.num_standard = ARRAY_SIZE(rndis_iw_handler),
	.num_private  = ARRAY_SIZE(rndis_wext_private_handler),
	.num_private_args = ARRAY_SIZE(rndis_wext_private_args),
	.standard = (iw_handler *)rndis_iw_handler,
	.private  = (iw_handler *)rndis_wext_private_handler,
	.private_args = (struct iw_priv_args *)rndis_wext_private_args,
	.get_wireless_stats = rndis_get_wireless_stats,
};


static void rndis_wext_worker(struct work_struct *work)
{
	struct rndis_wext_private *priv =
		container_of(work, struct rndis_wext_private, work);
	struct usbnet *usbdev = priv->usbdev;
	union iwreq_data evt;
	unsigned char bssid[ETH_ALEN];
	struct ndis_80211_assoc_info *info;
	int assoc_size = sizeof(*info) + IW_CUSTOM_MAX + 32;
	int ret, offset;

	if (test_and_clear_bit(WORK_LINK_UP, &priv->work_pending)) {
		netif_carrier_on(usbdev->net);

		info = kzalloc(assoc_size, GFP_KERNEL);
		if (!info)
			goto get_bssid;

		/* Get association info IEs from device and send them back to
		 * userspace. */
		ret = get_association_info(usbdev, info, assoc_size);
		if (!ret) {
			evt.data.length = le32_to_cpu(info->req_ie_length);
			if (evt.data.length > 0) {
				offset = le32_to_cpu(info->offset_req_ies);
				wireless_send_event(usbdev->net,
					IWEVASSOCREQIE, &evt,
					(char *)info + offset);
			}

			evt.data.length = le32_to_cpu(info->resp_ie_length);
			if (evt.data.length > 0) {
				offset = le32_to_cpu(info->offset_resp_ies);
				wireless_send_event(usbdev->net,
					IWEVASSOCRESPIE, &evt,
					(char *)info + offset);
			}
		}

		kfree(info);

get_bssid:
		ret = get_bssid(usbdev, bssid);
		if (!ret) {
			evt.data.flags = 0;
			evt.data.length = 0;
			memcpy(evt.ap_addr.sa_data, bssid, ETH_ALEN);
			wireless_send_event(usbdev->net, SIOCGIWAP, &evt, NULL);
		}
	}

	if (test_and_clear_bit(WORK_LINK_DOWN, &priv->work_pending)) {
		netif_carrier_off(usbdev->net);

		evt.data.flags = 0;
		evt.data.length = 0;
		memset(evt.ap_addr.sa_data, 0, ETH_ALEN);
		wireless_send_event(usbdev->net, SIOCGIWAP, &evt, NULL);
	}

	if (test_and_clear_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending))
		set_multicast_list(usbdev);
}

static void rndis_wext_set_multicast_list(struct net_device *dev)
{
	struct usbnet *usbdev = netdev_priv(dev);
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	if (test_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending))
		return;

	set_bit(WORK_SET_MULTICAST_LIST, &priv->work_pending);
	queue_work(priv->workqueue, &priv->work);
}

static void rndis_wext_link_change(struct usbnet *usbdev, int state)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	/* queue work to avoid recursive calls into rndis_command */
	set_bit(state ? WORK_LINK_UP : WORK_LINK_DOWN, &priv->work_pending);
	queue_work(priv->workqueue, &priv->work);
}


static int rndis_wext_get_caps(struct usbnet *usbdev)
{
	struct {
		__le32	num_items;
		__le32	items[8];
	} networks_supported;
	int len, retval, i, n;
	__le32 tx_power;
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	/* determine if supports setting txpower */
	len = sizeof(tx_power);
	retval = rndis_query_oid(usbdev, OID_802_11_TX_POWER_LEVEL, &tx_power,
									&len);
	if (retval == 0 && le32_to_cpu(tx_power) != 0xFF)
		priv->caps |= CAP_SUPPORT_TXPOWER;

	/* determine supported modes */
	len = sizeof(networks_supported);
	retval = rndis_query_oid(usbdev, OID_802_11_NETWORK_TYPES_SUPPORTED,
						&networks_supported, &len);
	if (retval >= 0) {
		n = le32_to_cpu(networks_supported.num_items);
		if (n > 8)
			n = 8;
		for (i = 0; i < n; i++) {
			switch (le32_to_cpu(networks_supported.items[i])) {
			case ndis_80211_type_freq_hop:
			case ndis_80211_type_direct_seq:
				priv->caps |= CAP_MODE_80211B;
				break;
			case ndis_80211_type_ofdm_a:
				priv->caps |= CAP_MODE_80211A;
				break;
			case ndis_80211_type_ofdm_g:
				priv->caps |= CAP_MODE_80211G;
				break;
			}
		}
		if (priv->caps & CAP_MODE_80211A)
			strcat(priv->name, "a");
		if (priv->caps & CAP_MODE_80211B)
			strcat(priv->name, "b");
		if (priv->caps & CAP_MODE_80211G)
			strcat(priv->name, "g");
	}

	return retval;
}


#define STATS_UPDATE_JIFFIES (HZ)
static void rndis_update_wireless_stats(struct work_struct *work)
{
	struct rndis_wext_private *priv =
		container_of(work, struct rndis_wext_private, stats_work.work);
	struct usbnet *usbdev = priv->usbdev;
	struct iw_statistics iwstats;
	__le32 rssi, tmp;
	int len, ret, j;
	unsigned long flags;
	int update_jiffies = STATS_UPDATE_JIFFIES;
	void *buf;

	spin_lock_irqsave(&priv->stats_lock, flags);
	memcpy(&iwstats, &priv->privstats, sizeof(iwstats));
	spin_unlock_irqrestore(&priv->stats_lock, flags);

	/* only update stats when connected */
	if (!is_associated(usbdev)) {
		iwstats.qual.qual = 0;
		iwstats.qual.level = 0;
		iwstats.qual.updated = IW_QUAL_QUAL_UPDATED
				| IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_NOISE_INVALID
				| IW_QUAL_QUAL_INVALID
				| IW_QUAL_LEVEL_INVALID;
		goto end;
	}

	len = sizeof(rssi);
	ret = rndis_query_oid(usbdev, OID_802_11_RSSI, &rssi, &len);

	devdbg(usbdev, "stats: OID_802_11_RSSI -> %d, rssi:%d", ret,
							le32_to_cpu(rssi));
	if (ret == 0) {
		memset(&iwstats.qual, 0, sizeof(iwstats.qual));
		iwstats.qual.qual  = level_to_qual(le32_to_cpu(rssi));
		iwstats.qual.level = le32_to_cpu(rssi);
		iwstats.qual.updated = IW_QUAL_QUAL_UPDATED
				| IW_QUAL_LEVEL_UPDATED
				| IW_QUAL_NOISE_INVALID;
	}

	memset(&iwstats.discard, 0, sizeof(iwstats.discard));

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_GEN_XMIT_ERROR, &tmp, &len);
	if (ret == 0)
		iwstats.discard.misc += le32_to_cpu(tmp);

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_GEN_RCV_ERROR, &tmp, &len);
	if (ret == 0)
		iwstats.discard.misc += le32_to_cpu(tmp);

	len = sizeof(tmp);
	ret = rndis_query_oid(usbdev, OID_GEN_RCV_NO_BUFFER, &tmp, &len);
	if (ret == 0)
		iwstats.discard.misc += le32_to_cpu(tmp);

	/* Workaround transfer stalls on poor quality links.
	 * TODO: find right way to fix these stalls (as stalls do not happen
	 * with ndiswrapper/windows driver). */
	if (iwstats.qual.qual <= 25) {
		/* Decrease stats worker interval to catch stalls.
		 * faster. Faster than 400-500ms causes packet loss,
		 * Slower doesn't catch stalls fast enough.
		 */
		j = msecs_to_jiffies(priv->param_workaround_interval);
		if (j > STATS_UPDATE_JIFFIES)
			j = STATS_UPDATE_JIFFIES;
		else if (j <= 0)
			j = 1;
		update_jiffies = j;

		/* Send scan OID. Use of both OIDs is required to get device
		 * working.
		 */
		tmp = cpu_to_le32(1);
		rndis_set_oid(usbdev, OID_802_11_BSSID_LIST_SCAN, &tmp,
								sizeof(tmp));

		len = CONTROL_BUFFER_SIZE;
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			goto end;

		rndis_query_oid(usbdev, OID_802_11_BSSID_LIST, buf, &len);
		kfree(buf);
	}
end:
	spin_lock_irqsave(&priv->stats_lock, flags);
	memcpy(&priv->privstats, &iwstats, sizeof(iwstats));
	spin_unlock_irqrestore(&priv->stats_lock, flags);

	if (update_jiffies >= HZ)
		update_jiffies = round_jiffies_relative(update_jiffies);
	else {
		j = round_jiffies_relative(update_jiffies);
		if (abs(j - update_jiffies) <= 10)
			update_jiffies = j;
	}

	queue_delayed_work(priv->workqueue, &priv->stats_work, update_jiffies);
}


static int bcm4320_early_init(struct usbnet *usbdev)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);
	char buf[8];

	/* Early initialization settings, setting these won't have effect
	 * if called after generic_rndis_bind().
	 */

	priv->param_country[0] = modparam_country[0];
	priv->param_country[1] = modparam_country[1];
	priv->param_country[2] = 0;
	priv->param_frameburst   = modparam_frameburst;
	priv->param_afterburner  = modparam_afterburner;
	priv->param_power_save   = modparam_power_save;
	priv->param_power_output = modparam_power_output;
	priv->param_roamtrigger  = modparam_roamtrigger;
	priv->param_roamdelta    = modparam_roamdelta;

	priv->param_country[0] = toupper(priv->param_country[0]);
	priv->param_country[1] = toupper(priv->param_country[1]);
	/* doesn't support EU as country code, use FI instead */
	if (!strcmp(priv->param_country, "EU"))
		strcpy(priv->param_country, "FI");

	if (priv->param_power_save < 0)
		priv->param_power_save = 0;
	else if (priv->param_power_save > 2)
		priv->param_power_save = 2;

	if (priv->param_power_output < 0)
		priv->param_power_output = 0;
	else if (priv->param_power_output > 3)
		priv->param_power_output = 3;

	if (priv->param_roamtrigger < -80)
		priv->param_roamtrigger = -80;
	else if (priv->param_roamtrigger > -60)
		priv->param_roamtrigger = -60;

	if (priv->param_roamdelta < 0)
		priv->param_roamdelta = 0;
	else if (priv->param_roamdelta > 2)
		priv->param_roamdelta = 2;

	if (modparam_workaround_interval < 0)
		priv->param_workaround_interval = 500;
	else
		priv->param_workaround_interval = modparam_workaround_interval;

	rndis_set_config_parameter_str(usbdev, "Country", priv->param_country);
	rndis_set_config_parameter_str(usbdev, "FrameBursting",
					priv->param_frameburst ? "1" : "0");
	rndis_set_config_parameter_str(usbdev, "Afterburner",
					priv->param_afterburner ? "1" : "0");
	sprintf(buf, "%d", priv->param_power_save);
	rndis_set_config_parameter_str(usbdev, "PowerSaveMode", buf);
	sprintf(buf, "%d", priv->param_power_output);
	rndis_set_config_parameter_str(usbdev, "PwrOut", buf);
	sprintf(buf, "%d", priv->param_roamtrigger);
	rndis_set_config_parameter_str(usbdev, "RoamTrigger", buf);
	sprintf(buf, "%d", priv->param_roamdelta);
	rndis_set_config_parameter_str(usbdev, "RoamDelta", buf);

	return 0;
}

/* same as rndis_netdev_ops but with local multicast handler */
static const struct net_device_ops rndis_wext_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_multicast_list	= rndis_wext_set_multicast_list,
};


static int rndis_wext_bind(struct usbnet *usbdev, struct usb_interface *intf)
{
	struct rndis_wext_private *priv;
	int retval, len;
	__le32 tmp;

	/* allocate rndis private data */
	priv = kzalloc(sizeof(struct rndis_wext_private), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	/* These have to be initialized before calling generic_rndis_bind().
	 * Otherwise we'll be in big trouble in rndis_wext_early_init().
	 */
	usbdev->driver_priv = priv;
	strcpy(priv->name, "IEEE802.11");
	usbdev->net->wireless_handlers = &rndis_iw_handlers;
	priv->usbdev = usbdev;

	mutex_init(&priv->command_lock);
	spin_lock_init(&priv->stats_lock);

	/* because rndis_command() sleeps we need to use workqueue */
	priv->workqueue = create_singlethread_workqueue("rndis_wlan");
	INIT_WORK(&priv->work, rndis_wext_worker);
	INIT_DELAYED_WORK(&priv->stats_work, rndis_update_wireless_stats);

	/* try bind rndis_host */
	retval = generic_rndis_bind(usbdev, intf, FLAG_RNDIS_PHYM_WIRELESS);
	if (retval < 0)
		goto fail;

	/* generic_rndis_bind set packet filter to multicast_all+
	 * promisc mode which doesn't work well for our devices (device
	 * picks up rssi to closest station instead of to access point).
	 *
	 * rndis_host wants to avoid all OID as much as possible
	 * so do promisc/multicast handling in rndis_wext.
	 */
	usbdev->net->netdev_ops = &rndis_wext_netdev_ops;

	tmp = RNDIS_PACKET_TYPE_DIRECTED | RNDIS_PACKET_TYPE_BROADCAST;
	retval = rndis_set_oid(usbdev, OID_GEN_CURRENT_PACKET_FILTER, &tmp,
								sizeof(tmp));

	len = sizeof(tmp);
	retval = rndis_query_oid(usbdev, OID_802_3_MAXIMUM_LIST_SIZE, &tmp,
								&len);
	priv->multicast_size = le32_to_cpu(tmp);
	if (retval < 0 || priv->multicast_size < 0)
		priv->multicast_size = 0;
	if (priv->multicast_size > 0)
		usbdev->net->flags |= IFF_MULTICAST;
	else
		usbdev->net->flags &= ~IFF_MULTICAST;

	priv->iwstats.qual.qual = 0;
	priv->iwstats.qual.level = 0;
	priv->iwstats.qual.updated = IW_QUAL_QUAL_UPDATED
					| IW_QUAL_LEVEL_UPDATED
					| IW_QUAL_NOISE_INVALID
					| IW_QUAL_QUAL_INVALID
					| IW_QUAL_LEVEL_INVALID;

	rndis_wext_get_caps(usbdev);
	set_default_iw_params(usbdev);

	/* turn radio on */
	priv->radio_on = 1;
	disassociate(usbdev, 1);
	netif_carrier_off(usbdev->net);

	queue_delayed_work(priv->workqueue, &priv->stats_work,
		round_jiffies_relative(STATS_UPDATE_JIFFIES));

	return 0;

fail:
	cancel_delayed_work_sync(&priv->stats_work);
	cancel_work_sync(&priv->work);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);

	kfree(priv);
	return retval;
}


static void rndis_wext_unbind(struct usbnet *usbdev, struct usb_interface *intf)
{
	struct rndis_wext_private *priv = get_rndis_wext_priv(usbdev);

	/* turn radio off */
	disassociate(usbdev, 0);

	cancel_delayed_work_sync(&priv->stats_work);
	cancel_work_sync(&priv->work);
	flush_workqueue(priv->workqueue);
	destroy_workqueue(priv->workqueue);

	if (priv && priv->wpa_ie_len)
		kfree(priv->wpa_ie);
	kfree(priv);

	rndis_unbind(usbdev, intf);
}


static int rndis_wext_reset(struct usbnet *usbdev)
{
	return deauthenticate(usbdev);
}


static const struct driver_info	bcm4320b_info = {
	.description =	"Wireless RNDIS device, BCM4320b based",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT,
	.bind =		rndis_wext_bind,
	.unbind =	rndis_wext_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wext_reset,
	.early_init =	bcm4320_early_init,
	.link_change =	rndis_wext_link_change,
};

static const struct driver_info	bcm4320a_info = {
	.description =	"Wireless RNDIS device, BCM4320a based",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT,
	.bind =		rndis_wext_bind,
	.unbind =	rndis_wext_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wext_reset,
	.early_init =	bcm4320_early_init,
	.link_change =	rndis_wext_link_change,
};

static const struct driver_info rndis_wext_info = {
	.description =	"Wireless RNDIS device",
	.flags =	FLAG_WLAN | FLAG_FRAMING_RN | FLAG_NO_SETINT,
	.bind =		rndis_wext_bind,
	.unbind =	rndis_wext_unbind,
	.status =	rndis_status,
	.rx_fixup =	rndis_rx_fixup,
	.tx_fixup =	rndis_tx_fixup,
	.reset =	rndis_wext_reset,
	.early_init =	bcm4320_early_init,
	.link_change =	rndis_wext_link_change,
};

/*-------------------------------------------------------------------------*/

static const struct usb_device_id products [] = {
#define	RNDIS_MASTER_INTERFACE \
	.bInterfaceClass	= USB_CLASS_COMM, \
	.bInterfaceSubClass	= 2 /* ACM */, \
	.bInterfaceProtocol	= 0x0ff

/* INF driver for these devices have DriverVer >= 4.xx.xx.xx and many custom
 * parameters available. Chipset marked as 'BCM4320SKFBG' in NDISwrapper-wiki.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0411,
	.idProduct		= 0x00bc,	/* Buffalo WLI-U2-KG125S */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0baf,
	.idProduct		= 0x011b,	/* U.S. Robotics USR5421 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x050d,
	.idProduct		= 0x011b,	/* Belkin F5D7051 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x1799,	/* Belkin has two vendor ids */
	.idProduct		= 0x011b,	/* Belkin F5D7051 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x0014,	/* Linksys WUSB54GSv2 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x0026,	/* Linksys WUSB54GSC */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0b05,
	.idProduct		= 0x1717,	/* Asus WL169gE */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0a5c,
	.idProduct		= 0xd11b,	/* Eminent EM4045 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x1690,
	.idProduct		= 0x0715,	/* BT Voyager 1055 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320b_info,
},
/* These devices have DriverVer < 4.xx.xx.xx and do not have any custom
 * parameters available, hardware probably contain older firmware version with
 * no way of updating. Chipset marked as 'BCM4320????' in NDISwrapper-wiki.
 */
{
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x13b1,
	.idProduct		= 0x000e,	/* Linksys WUSB54GSv1 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0baf,
	.idProduct		= 0x0111,	/* U.S. Robotics USR5420 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
}, {
	.match_flags	=   USB_DEVICE_ID_MATCH_INT_INFO
			  | USB_DEVICE_ID_MATCH_DEVICE,
	.idVendor		= 0x0411,
	.idProduct		= 0x004b,	/* BUFFALO WLI-USB-G54 */
	RNDIS_MASTER_INTERFACE,
	.driver_info		= (unsigned long) &bcm4320a_info,
},
/* Generic Wireless RNDIS devices that we don't have exact
 * idVendor/idProduct/chip yet.
 */
{
	/* RNDIS is MSFT's un-official variant of CDC ACM */
	USB_INTERFACE_INFO(USB_CLASS_COMM, 2 /* ACM */, 0x0ff),
	.driver_info = (unsigned long) &rndis_wext_info,
}, {
	/* "ActiveSync" is an undocumented variant of RNDIS, used in WM5 */
	USB_INTERFACE_INFO(USB_CLASS_MISC, 1, 1),
	.driver_info = (unsigned long) &rndis_wext_info,
},
	{ },		// END
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver rndis_wlan_driver = {
	.name =		"rndis_wlan",
	.id_table =	products,
	.probe =	usbnet_probe,
	.disconnect =	usbnet_disconnect,
	.suspend =	usbnet_suspend,
	.resume =	usbnet_resume,
};

static int __init rndis_wlan_init(void)
{
	return usb_register(&rndis_wlan_driver);
}
module_init(rndis_wlan_init);

static void __exit rndis_wlan_exit(void)
{
	usb_deregister(&rndis_wlan_driver);
}
module_exit(rndis_wlan_exit);

MODULE_AUTHOR("Bjorge Dijkstra");
MODULE_AUTHOR("Jussi Kivilinna");
MODULE_DESCRIPTION("Driver for RNDIS based USB Wireless adapters");
MODULE_LICENSE("GPL");

