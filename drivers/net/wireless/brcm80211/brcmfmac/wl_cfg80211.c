/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Toplevel file. Relies on dhd_linux.c to send commands to the dongle. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/netdevice.h>
#include <linux/bitops.h>
#include <linux/etherdevice.h>
#include <linux/ieee80211.h>
#include <linux/uaccess.h>
#include <net/cfg80211.h>

#include <brcmu_utils.h>
#include <defs.h>
#include <brcmu_wifi.h>
#include "dhd.h"
#include "wl_cfg80211.h"

#define BRCMF_ASSOC_PARAMS_FIXED_SIZE \
	(sizeof(struct brcmf_assoc_params_le) - sizeof(u16))

static const u8 ether_bcast[ETH_ALEN] = {255, 255, 255, 255, 255, 255};

static u32 brcmf_dbg_level = WL_DBG_ERR;

static void brcmf_set_drvdata(struct brcmf_cfg80211_dev *dev, void *data)
{
	dev->driver_data = data;
}

static void *brcmf_get_drvdata(struct brcmf_cfg80211_dev *dev)
{
	void *data = NULL;

	if (dev)
		data = dev->driver_data;
	return data;
}

static
struct brcmf_cfg80211_priv *brcmf_priv_get(struct brcmf_cfg80211_dev *cfg_dev)
{
	struct brcmf_cfg80211_iface *ci = brcmf_get_drvdata(cfg_dev);
	return ci->cfg_priv;
}

static bool check_sys_up(struct wiphy *wiphy)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	if (!test_bit(WL_STATUS_READY, &cfg_priv->status)) {
		WL_INFO("device is not ready : status (%d)\n",
			(int)cfg_priv->status);
		return false;
	}
	return true;
}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= IEEE80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _flags) {				\
	.band			= IEEE80211_BAND_5GHZ,		\
	.center_freq		= 5000 + (5 * (_channel)),	\
	.hw_value		= (_channel),			\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define RATE_TO_BASE100KBPS(rate)   (((rate) * 10) / 2)
#define RATETAB_ENT(_rateid, _flags) \
	{                                                               \
		.bitrate        = RATE_TO_BASE100KBPS(_rateid),     \
		.hw_value       = (_rateid),                            \
		.flags          = (_flags),                             \
	}

static struct ieee80211_rate __wl_rates[] = {
	RATETAB_ENT(BRCM_RATE_1M, 0),
	RATETAB_ENT(BRCM_RATE_2M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_5M5, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_11M, IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(BRCM_RATE_6M, 0),
	RATETAB_ENT(BRCM_RATE_9M, 0),
	RATETAB_ENT(BRCM_RATE_12M, 0),
	RATETAB_ENT(BRCM_RATE_18M, 0),
	RATETAB_ENT(BRCM_RATE_24M, 0),
	RATETAB_ENT(BRCM_RATE_36M, 0),
	RATETAB_ENT(BRCM_RATE_48M, 0),
	RATETAB_ENT(BRCM_RATE_54M, 0),
};

#define wl_a_rates		(__wl_rates + 4)
#define wl_a_rates_size	8
#define wl_g_rates		(__wl_rates + 0)
#define wl_g_rates_size	12

static struct ieee80211_channel __wl_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static struct ieee80211_channel __wl_5ghz_a_channels[] = {
	CHAN5G(34, 0), CHAN5G(36, 0),
	CHAN5G(38, 0), CHAN5G(40, 0),
	CHAN5G(42, 0), CHAN5G(44, 0),
	CHAN5G(46, 0), CHAN5G(48, 0),
	CHAN5G(52, 0), CHAN5G(56, 0),
	CHAN5G(60, 0), CHAN5G(64, 0),
	CHAN5G(100, 0), CHAN5G(104, 0),
	CHAN5G(108, 0), CHAN5G(112, 0),
	CHAN5G(116, 0), CHAN5G(120, 0),
	CHAN5G(124, 0), CHAN5G(128, 0),
	CHAN5G(132, 0), CHAN5G(136, 0),
	CHAN5G(140, 0), CHAN5G(149, 0),
	CHAN5G(153, 0), CHAN5G(157, 0),
	CHAN5G(161, 0), CHAN5G(165, 0),
	CHAN5G(184, 0), CHAN5G(188, 0),
	CHAN5G(192, 0), CHAN5G(196, 0),
	CHAN5G(200, 0), CHAN5G(204, 0),
	CHAN5G(208, 0), CHAN5G(212, 0),
	CHAN5G(216, 0),
};

static struct ieee80211_channel __wl_5ghz_n_channels[] = {
	CHAN5G(32, 0), CHAN5G(34, 0),
	CHAN5G(36, 0), CHAN5G(38, 0),
	CHAN5G(40, 0), CHAN5G(42, 0),
	CHAN5G(44, 0), CHAN5G(46, 0),
	CHAN5G(48, 0), CHAN5G(50, 0),
	CHAN5G(52, 0), CHAN5G(54, 0),
	CHAN5G(56, 0), CHAN5G(58, 0),
	CHAN5G(60, 0), CHAN5G(62, 0),
	CHAN5G(64, 0), CHAN5G(66, 0),
	CHAN5G(68, 0), CHAN5G(70, 0),
	CHAN5G(72, 0), CHAN5G(74, 0),
	CHAN5G(76, 0), CHAN5G(78, 0),
	CHAN5G(80, 0), CHAN5G(82, 0),
	CHAN5G(84, 0), CHAN5G(86, 0),
	CHAN5G(88, 0), CHAN5G(90, 0),
	CHAN5G(92, 0), CHAN5G(94, 0),
	CHAN5G(96, 0), CHAN5G(98, 0),
	CHAN5G(100, 0), CHAN5G(102, 0),
	CHAN5G(104, 0), CHAN5G(106, 0),
	CHAN5G(108, 0), CHAN5G(110, 0),
	CHAN5G(112, 0), CHAN5G(114, 0),
	CHAN5G(116, 0), CHAN5G(118, 0),
	CHAN5G(120, 0), CHAN5G(122, 0),
	CHAN5G(124, 0), CHAN5G(126, 0),
	CHAN5G(128, 0), CHAN5G(130, 0),
	CHAN5G(132, 0), CHAN5G(134, 0),
	CHAN5G(136, 0), CHAN5G(138, 0),
	CHAN5G(140, 0), CHAN5G(142, 0),
	CHAN5G(144, 0), CHAN5G(145, 0),
	CHAN5G(146, 0), CHAN5G(147, 0),
	CHAN5G(148, 0), CHAN5G(149, 0),
	CHAN5G(150, 0), CHAN5G(151, 0),
	CHAN5G(152, 0), CHAN5G(153, 0),
	CHAN5G(154, 0), CHAN5G(155, 0),
	CHAN5G(156, 0), CHAN5G(157, 0),
	CHAN5G(158, 0), CHAN5G(159, 0),
	CHAN5G(160, 0), CHAN5G(161, 0),
	CHAN5G(162, 0), CHAN5G(163, 0),
	CHAN5G(164, 0), CHAN5G(165, 0),
	CHAN5G(166, 0), CHAN5G(168, 0),
	CHAN5G(170, 0), CHAN5G(172, 0),
	CHAN5G(174, 0), CHAN5G(176, 0),
	CHAN5G(178, 0), CHAN5G(180, 0),
	CHAN5G(182, 0), CHAN5G(184, 0),
	CHAN5G(186, 0), CHAN5G(188, 0),
	CHAN5G(190, 0), CHAN5G(192, 0),
	CHAN5G(194, 0), CHAN5G(196, 0),
	CHAN5G(198, 0), CHAN5G(200, 0),
	CHAN5G(202, 0), CHAN5G(204, 0),
	CHAN5G(206, 0), CHAN5G(208, 0),
	CHAN5G(210, 0), CHAN5G(212, 0),
	CHAN5G(214, 0), CHAN5G(216, 0),
	CHAN5G(218, 0), CHAN5G(220, 0),
	CHAN5G(222, 0), CHAN5G(224, 0),
	CHAN5G(226, 0), CHAN5G(228, 0),
};

static struct ieee80211_supported_band __wl_band_2ghz = {
	.band = IEEE80211_BAND_2GHZ,
	.channels = __wl_2ghz_channels,
	.n_channels = ARRAY_SIZE(__wl_2ghz_channels),
	.bitrates = wl_g_rates,
	.n_bitrates = wl_g_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_a = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_a_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_a_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static struct ieee80211_supported_band __wl_band_5ghz_n = {
	.band = IEEE80211_BAND_5GHZ,
	.channels = __wl_5ghz_n_channels,
	.n_channels = ARRAY_SIZE(__wl_5ghz_n_channels),
	.bitrates = wl_a_rates,
	.n_bitrates = wl_a_rates_size,
};

static const u32 __wl_cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	WLAN_CIPHER_SUITE_AES_CMAC,
};

/* tag_ID/length/value_buffer tuple */
struct brcmf_tlv {
	u8 id;
	u8 len;
	u8 data[1];
};

/* Quarter dBm units to mW
 * Table starts at QDBM_OFFSET, so the first entry is mW for qdBm=153
 * Table is offset so the last entry is largest mW value that fits in
 * a u16.
 */

#define QDBM_OFFSET 153		/* Offset for first entry */
#define QDBM_TABLE_LEN 40	/* Table size */

/* Smallest mW value that will round up to the first table entry, QDBM_OFFSET.
 * Value is ( mW(QDBM_OFFSET - 1) + mW(QDBM_OFFSET) ) / 2
 */
#define QDBM_TABLE_LOW_BOUND 6493	/* Low bound */

/* Largest mW value that will round down to the last table entry,
 * QDBM_OFFSET + QDBM_TABLE_LEN-1.
 * Value is ( mW(QDBM_OFFSET + QDBM_TABLE_LEN - 1) +
 * mW(QDBM_OFFSET + QDBM_TABLE_LEN) ) / 2.
 */
#define QDBM_TABLE_HIGH_BOUND 64938	/* High bound */

static const u16 nqdBm_to_mW_map[QDBM_TABLE_LEN] = {
/* qdBm:	+0	+1	+2	+3	+4	+5	+6	+7 */
/* 153: */ 6683, 7079, 7499, 7943, 8414, 8913, 9441, 10000,
/* 161: */ 10593, 11220, 11885, 12589, 13335, 14125, 14962, 15849,
/* 169: */ 16788, 17783, 18836, 19953, 21135, 22387, 23714, 25119,
/* 177: */ 26607, 28184, 29854, 31623, 33497, 35481, 37584, 39811,
/* 185: */ 42170, 44668, 47315, 50119, 53088, 56234, 59566, 63096
};

static u16 brcmf_qdbm_to_mw(u8 qdbm)
{
	uint factor = 1;
	int idx = qdbm - QDBM_OFFSET;

	if (idx >= QDBM_TABLE_LEN)
		/* clamp to max u16 mW value */
		return 0xFFFF;

	/* scale the qdBm index up to the range of the table 0-40
	 * where an offset of 40 qdBm equals a factor of 10 mW.
	 */
	while (idx < 0) {
		idx += 40;
		factor *= 10;
	}

	/* return the mW value scaled down to the correct factor of 10,
	 * adding in factor/2 to get proper rounding.
	 */
	return (nqdBm_to_mW_map[idx] + factor / 2) / factor;
}

static u8 brcmf_mw_to_qdbm(u16 mw)
{
	u8 qdbm;
	int offset;
	uint mw_uint = mw;
	uint boundary;

	/* handle boundary case */
	if (mw_uint <= 1)
		return 0;

	offset = QDBM_OFFSET;

	/* move mw into the range of the table */
	while (mw_uint < QDBM_TABLE_LOW_BOUND) {
		mw_uint *= 10;
		offset -= 40;
	}

	for (qdbm = 0; qdbm < QDBM_TABLE_LEN - 1; qdbm++) {
		boundary = nqdBm_to_mW_map[qdbm] + (nqdBm_to_mW_map[qdbm + 1] -
						    nqdBm_to_mW_map[qdbm]) / 2;
		if (mw_uint < boundary)
			break;
	}

	qdbm += (u8) offset;

	return qdbm;
}

/* function for reading/writing a single u32 from/to the dongle */
static int
brcmf_exec_dcmd_u32(struct net_device *ndev, u32 cmd, u32 *par)
{
	int err;
	__le32 par_le = cpu_to_le32(*par);

	err = brcmf_exec_dcmd(ndev, cmd, &par_le, sizeof(__le32));
	*par = le32_to_cpu(par_le);

	return err;
}

static void convert_key_from_CPU(struct brcmf_wsec_key *key,
				 struct brcmf_wsec_key_le *key_le)
{
	key_le->index = cpu_to_le32(key->index);
	key_le->len = cpu_to_le32(key->len);
	key_le->algo = cpu_to_le32(key->algo);
	key_le->flags = cpu_to_le32(key->flags);
	key_le->rxiv.hi = cpu_to_le32(key->rxiv.hi);
	key_le->rxiv.lo = cpu_to_le16(key->rxiv.lo);
	key_le->iv_initialized = cpu_to_le32(key->iv_initialized);
	memcpy(key_le->data, key->data, sizeof(key->data));
	memcpy(key_le->ea, key->ea, sizeof(key->ea));
}

static int send_key_to_dongle(struct net_device *ndev,
			      struct brcmf_wsec_key *key)
{
	int err;
	struct brcmf_wsec_key_le key_le;

	convert_key_from_CPU(key, &key_le);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_KEY, &key_le, sizeof(key_le));
	if (err)
		WL_ERR("WLC_SET_KEY error (%d)\n", err);
	return err;
}

static s32
brcmf_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *ndev,
			 enum nl80211_iftype type, u32 *flags,
			 struct vif_params *params)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct wireless_dev *wdev;
	s32 infra = 0;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	switch (type) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR("type (%d) : currently we do not support this type\n",
		       type);
		return -EOPNOTSUPP;
	case NL80211_IFTYPE_ADHOC:
		cfg_priv->conf->mode = WL_MODE_IBSS;
		infra = 0;
		break;
	case NL80211_IFTYPE_STATION:
		cfg_priv->conf->mode = WL_MODE_BSS;
		infra = 1;
		break;
	default:
		err = -EINVAL;
		goto done;
	}

	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_INFRA, &infra);
	if (err) {
		WL_ERR("WLC_SET_INFRA error (%d)\n", err);
		err = -EAGAIN;
	} else {
		wdev = ndev->ieee80211_ptr;
		wdev->iftype = type;
	}

	WL_INFO("IF Type = %s\n",
		(cfg_priv->conf->mode == WL_MODE_IBSS) ? "Adhoc" : "Infra");

done:
	WL_TRACE("Exit\n");

	return err;
}

static s32 brcmf_dev_intvar_set(struct net_device *ndev, s8 *name, s32 val)
{
	s8 buf[BRCMF_DCMD_SMLEN];
	u32 len;
	s32 err = 0;
	__le32 val_le;

	val_le = cpu_to_le32(val);
	len = brcmf_c_mkiovar(name, (char *)(&val_le), sizeof(val_le), buf,
			    sizeof(buf));
	BUG_ON(!len);

	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR, buf, len);
	if (err)
		WL_ERR("error (%d)\n", err);

	return err;
}

static s32
brcmf_dev_intvar_get(struct net_device *ndev, s8 *name, s32 *retval)
{
	union {
		s8 buf[BRCMF_DCMD_SMLEN];
		__le32 val;
	} var;
	u32 len;
	u32 data_null;
	s32 err = 0;

	len =
	    brcmf_c_mkiovar(name, (char *)(&data_null), 0, (char *)(&var),
			sizeof(var.buf));
	BUG_ON(!len);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_VAR, &var, len);
	if (err)
		WL_ERR("error (%d)\n", err);

	*retval = le32_to_cpu(var.val);

	return err;
}

static void brcmf_set_mpc(struct net_device *ndev, int mpc)
{
	s32 err = 0;
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);

	if (test_bit(WL_STATUS_READY, &cfg_priv->status)) {
		err = brcmf_dev_intvar_set(ndev, "mpc", mpc);
		if (err) {
			WL_ERR("fail to set mpc\n");
			return;
		}
		WL_INFO("MPC : %d\n", mpc);
	}
}

static void wl_iscan_prep(struct brcmf_scan_params_le *params_le,
			  struct brcmf_ssid *ssid)
{
	memcpy(params_le->bssid, ether_bcast, ETH_ALEN);
	params_le->bss_type = DOT11_BSSTYPE_ANY;
	params_le->scan_type = 0;
	params_le->channel_num = 0;
	params_le->nprobes = cpu_to_le32(-1);
	params_le->active_time = cpu_to_le32(-1);
	params_le->passive_time = cpu_to_le32(-1);
	params_le->home_time = cpu_to_le32(-1);
	if (ssid && ssid->SSID_len)
		memcpy(&params_le->ssid_le, ssid, sizeof(struct brcmf_ssid));
}

static s32
brcmf_dev_iovar_setbuf(struct net_device *ndev, s8 * iovar, void *param,
		    s32 paramlen, void *bufptr, s32 buflen)
{
	s32 iolen;

	iolen = brcmf_c_mkiovar(iovar, param, paramlen, bufptr, buflen);
	BUG_ON(!iolen);

	return brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR, bufptr, iolen);
}

static s32
brcmf_dev_iovar_getbuf(struct net_device *ndev, s8 * iovar, void *param,
		    s32 paramlen, void *bufptr, s32 buflen)
{
	s32 iolen;

	iolen = brcmf_c_mkiovar(iovar, param, paramlen, bufptr, buflen);
	BUG_ON(!iolen);

	return brcmf_exec_dcmd(ndev, BRCMF_C_GET_VAR, bufptr, buflen);
}

static s32
brcmf_run_iscan(struct brcmf_cfg80211_iscan_ctrl *iscan,
		struct brcmf_ssid *ssid, u16 action)
{
	s32 params_size = BRCMF_SCAN_PARAMS_FIXED_SIZE +
			  offsetof(struct brcmf_iscan_params_le, params_le);
	struct brcmf_iscan_params_le *params;
	s32 err = 0;

	if (ssid && ssid->SSID_len)
		params_size += sizeof(struct brcmf_ssid);
	params = kzalloc(params_size, GFP_KERNEL);
	if (!params)
		return -ENOMEM;
	BUG_ON(params_size >= BRCMF_DCMD_SMLEN);

	wl_iscan_prep(&params->params_le, ssid);

	params->version = cpu_to_le32(BRCMF_ISCAN_REQ_VERSION);
	params->action = cpu_to_le16(action);
	params->scan_duration = cpu_to_le16(0);

	err = brcmf_dev_iovar_setbuf(iscan->ndev, "iscan", params, params_size,
				     iscan->dcmd_buf, BRCMF_DCMD_SMLEN);
	if (err) {
		if (err == -EBUSY)
			WL_INFO("system busy : iscan canceled\n");
		else
			WL_ERR("error (%d)\n", err);
	}

	kfree(params);
	return err;
}

static s32 brcmf_do_iscan(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_to_iscan(cfg_priv);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);
	struct brcmf_ssid ssid;
	__le32 passive_scan;
	s32 err = 0;

	/* Broadcast scan by default */
	memset(&ssid, 0, sizeof(ssid));

	iscan->state = WL_ISCAN_STATE_SCANING;

	passive_scan = cfg_priv->active_scan ? 0 : cpu_to_le32(1);
	err = brcmf_exec_dcmd(cfg_to_ndev(cfg_priv), BRCMF_C_SET_PASSIVE_SCAN,
			&passive_scan, sizeof(passive_scan));
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	brcmf_set_mpc(ndev, 0);
	cfg_priv->iscan_kickstart = true;
	err = brcmf_run_iscan(iscan, &ssid, BRCMF_SCAN_ACTION_START);
	if (err) {
		brcmf_set_mpc(ndev, 1);
		cfg_priv->iscan_kickstart = false;
		return err;
	}
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;
	return err;
}

static s32
__brcmf_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
		   struct cfg80211_scan_request *request,
		   struct cfg80211_ssid *this_ssid)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct cfg80211_ssid *ssids;
	struct brcmf_cfg80211_scan_req *sr = cfg_priv->scan_req_int;
	__le32 passive_scan;
	bool iscan_req;
	bool spec_scan;
	s32 err = 0;
	u32 SSID_len;

	if (test_bit(WL_STATUS_SCANNING, &cfg_priv->status)) {
		WL_ERR("Scanning already : status (%lu)\n", cfg_priv->status);
		return -EAGAIN;
	}
	if (test_bit(WL_STATUS_SCAN_ABORTING, &cfg_priv->status)) {
		WL_ERR("Scanning being aborted : status (%lu)\n",
		       cfg_priv->status);
		return -EAGAIN;
	}
	if (test_bit(WL_STATUS_CONNECTING, &cfg_priv->status)) {
		WL_ERR("Connecting : status (%lu)\n",
		       cfg_priv->status);
		return -EAGAIN;
	}

	iscan_req = false;
	spec_scan = false;
	if (request) {
		/* scan bss */
		ssids = request->ssids;
		if (cfg_priv->iscan_on && (!ssids || !ssids->ssid_len))
			iscan_req = true;
	} else {
		/* scan in ibss */
		/* we don't do iscan in ibss */
		ssids = this_ssid;
	}

	cfg_priv->scan_request = request;
	set_bit(WL_STATUS_SCANNING, &cfg_priv->status);
	if (iscan_req) {
		err = brcmf_do_iscan(cfg_priv);
		if (!err)
			return err;
		else
			goto scan_out;
	} else {
		WL_SCAN("ssid \"%s\", ssid_len (%d)\n",
		       ssids->ssid, ssids->ssid_len);
		memset(&sr->ssid_le, 0, sizeof(sr->ssid_le));
		SSID_len = min_t(u8, sizeof(sr->ssid_le.SSID), ssids->ssid_len);
		sr->ssid_le.SSID_len = cpu_to_le32(0);
		if (SSID_len) {
			memcpy(sr->ssid_le.SSID, ssids->ssid, SSID_len);
			sr->ssid_le.SSID_len = cpu_to_le32(SSID_len);
			spec_scan = true;
		} else {
			WL_SCAN("Broadcast scan\n");
		}

		passive_scan = cfg_priv->active_scan ? 0 : cpu_to_le32(1);
		err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_PASSIVE_SCAN,
				&passive_scan, sizeof(passive_scan));
		if (err) {
			WL_ERR("WLC_SET_PASSIVE_SCAN error (%d)\n", err);
			goto scan_out;
		}
		brcmf_set_mpc(ndev, 0);
		err = brcmf_exec_dcmd(ndev, BRCMF_C_SCAN, &sr->ssid_le,
				      sizeof(sr->ssid_le));
		if (err) {
			if (err == -EBUSY)
				WL_INFO("system busy : scan for \"%s\" "
					"canceled\n", sr->ssid_le.SSID);
			else
				WL_ERR("WLC_SCAN error (%d)\n", err);

			brcmf_set_mpc(ndev, 1);
			goto scan_out;
		}
	}

	return 0;

scan_out:
	clear_bit(WL_STATUS_SCANNING, &cfg_priv->status);
	cfg_priv->scan_request = NULL;
	return err;
}

static s32
brcmf_cfg80211_scan(struct wiphy *wiphy, struct net_device *ndev,
		 struct cfg80211_scan_request *request)
{
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (!check_sys_up(wiphy))
		return -EIO;

	err = __brcmf_cfg80211_scan(wiphy, ndev, request, NULL);
	if (err)
		WL_ERR("scan error (%d)\n", err);

	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_set_rts(struct net_device *ndev, u32 rts_threshold)
{
	s32 err = 0;

	err = brcmf_dev_intvar_set(ndev, "rtsthresh", rts_threshold);
	if (err)
		WL_ERR("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_frag(struct net_device *ndev, u32 frag_threshold)
{
	s32 err = 0;

	err = brcmf_dev_intvar_set(ndev, "fragthresh", frag_threshold);
	if (err)
		WL_ERR("Error (%d)\n", err);

	return err;
}

static s32 brcmf_set_retry(struct net_device *ndev, u32 retry, bool l)
{
	s32 err = 0;
	u32 cmd = (l ? BRCM_SET_LRL : BRCM_SET_SRL);

	err = brcmf_exec_dcmd_u32(ndev, cmd, &retry);
	if (err) {
		WL_ERR("cmd (%d) , error (%d)\n", cmd, err);
		return err;
	}
	return err;
}

static s32 brcmf_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	if (changed & WIPHY_PARAM_RTS_THRESHOLD &&
	    (cfg_priv->conf->rts_threshold != wiphy->rts_threshold)) {
		cfg_priv->conf->rts_threshold = wiphy->rts_threshold;
		err = brcmf_set_rts(ndev, cfg_priv->conf->rts_threshold);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_FRAG_THRESHOLD &&
	    (cfg_priv->conf->frag_threshold != wiphy->frag_threshold)) {
		cfg_priv->conf->frag_threshold = wiphy->frag_threshold;
		err = brcmf_set_frag(ndev, cfg_priv->conf->frag_threshold);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_RETRY_LONG
	    && (cfg_priv->conf->retry_long != wiphy->retry_long)) {
		cfg_priv->conf->retry_long = wiphy->retry_long;
		err = brcmf_set_retry(ndev, cfg_priv->conf->retry_long, true);
		if (!err)
			goto done;
	}
	if (changed & WIPHY_PARAM_RETRY_SHORT
	    && (cfg_priv->conf->retry_short != wiphy->retry_short)) {
		cfg_priv->conf->retry_short = wiphy->retry_short;
		err = brcmf_set_retry(ndev, cfg_priv->conf->retry_short, false);
		if (!err)
			goto done;
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static void *brcmf_read_prof(struct brcmf_cfg80211_priv *cfg_priv, s32 item)
{
	switch (item) {
	case WL_PROF_SEC:
		return &cfg_priv->profile->sec;
	case WL_PROF_BSSID:
		return &cfg_priv->profile->bssid;
	case WL_PROF_SSID:
		return &cfg_priv->profile->ssid;
	}
	WL_ERR("invalid item (%d)\n", item);
	return NULL;
}

static s32
brcmf_update_prof(struct brcmf_cfg80211_priv *cfg_priv,
		  const struct brcmf_event_msg *e, void *data, s32 item)
{
	s32 err = 0;
	struct brcmf_ssid *ssid;

	switch (item) {
	case WL_PROF_SSID:
		ssid = (struct brcmf_ssid *) data;
		memset(cfg_priv->profile->ssid.SSID, 0,
		       sizeof(cfg_priv->profile->ssid.SSID));
		memcpy(cfg_priv->profile->ssid.SSID,
		       ssid->SSID, ssid->SSID_len);
		cfg_priv->profile->ssid.SSID_len = ssid->SSID_len;
		break;
	case WL_PROF_BSSID:
		if (data)
			memcpy(cfg_priv->profile->bssid, data, ETH_ALEN);
		else
			memset(cfg_priv->profile->bssid, 0, ETH_ALEN);
		break;
	case WL_PROF_SEC:
		memcpy(&cfg_priv->profile->sec, data,
		       sizeof(cfg_priv->profile->sec));
		break;
	case WL_PROF_BEACONINT:
		cfg_priv->profile->beacon_interval = *(u16 *)data;
		break;
	case WL_PROF_DTIMPERIOD:
		cfg_priv->profile->dtim_period = *(u8 *)data;
		break;
	default:
		WL_ERR("unsupported item (%d)\n", item);
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static void brcmf_init_prof(struct brcmf_cfg80211_profile *prof)
{
	memset(prof, 0, sizeof(*prof));
}

static void brcmf_ch_to_chanspec(int ch, struct brcmf_join_params *join_params,
	size_t *join_params_size)
{
	u16 chanspec = 0;

	if (ch != 0) {
		if (ch <= CH_MAX_2G_CHANNEL)
			chanspec |= WL_CHANSPEC_BAND_2G;
		else
			chanspec |= WL_CHANSPEC_BAND_5G;

		chanspec |= WL_CHANSPEC_BW_20;
		chanspec |= WL_CHANSPEC_CTL_SB_NONE;

		*join_params_size += BRCMF_ASSOC_PARAMS_FIXED_SIZE +
				     sizeof(u16);

		chanspec |= (ch & WL_CHANSPEC_CHAN_MASK);
		join_params->params_le.chanspec_list[0] = cpu_to_le16(chanspec);
		join_params->params_le.chanspec_num = cpu_to_le32(1);

		WL_CONN("join_params->params.chanspec_list[0]= %#X,"
			"channel %d, chanspec %#X\n",
			chanspec, ch, chanspec);
	}
}

static void brcmf_link_down(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct net_device *ndev = NULL;
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (cfg_priv->link_up) {
		ndev = cfg_to_ndev(cfg_priv);
		WL_INFO("Call WLC_DISASSOC to stop excess roaming\n ");
		err = brcmf_exec_dcmd(ndev, BRCMF_C_DISASSOC, NULL, 0);
		if (err)
			WL_ERR("WLC_DISASSOC failed (%d)\n", err);
		cfg_priv->link_up = false;
	}
	WL_TRACE("Exit\n");
}

static s32
brcmf_cfg80211_join_ibss(struct wiphy *wiphy, struct net_device *ndev,
		      struct cfg80211_ibss_params *params)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct brcmf_join_params join_params;
	size_t join_params_size = 0;
	s32 err = 0;
	s32 wsec = 0;
	s32 bcnprd;
	struct brcmf_ssid ssid;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	if (params->ssid)
		WL_CONN("SSID: %s\n", params->ssid);
	else {
		WL_CONN("SSID: NULL, Not supported\n");
		return -EOPNOTSUPP;
	}

	set_bit(WL_STATUS_CONNECTING, &cfg_priv->status);

	if (params->bssid)
		WL_CONN("BSSID: %02X %02X %02X %02X %02X %02X\n",
		params->bssid[0], params->bssid[1], params->bssid[2],
		params->bssid[3], params->bssid[4], params->bssid[5]);
	else
		WL_CONN("No BSSID specified\n");

	if (params->channel)
		WL_CONN("channel: %d\n", params->channel->center_freq);
	else
		WL_CONN("no channel specified\n");

	if (params->channel_fixed)
		WL_CONN("fixed channel required\n");
	else
		WL_CONN("no fixed channel required\n");

	if (params->ie && params->ie_len)
		WL_CONN("ie len: %d\n", params->ie_len);
	else
		WL_CONN("no ie specified\n");

	if (params->beacon_interval)
		WL_CONN("beacon interval: %d\n", params->beacon_interval);
	else
		WL_CONN("no beacon interval specified\n");

	if (params->basic_rates)
		WL_CONN("basic rates: %08X\n", params->basic_rates);
	else
		WL_CONN("no basic rates specified\n");

	if (params->privacy)
		WL_CONN("privacy required\n");
	else
		WL_CONN("no privacy required\n");

	/* Configure Privacy for starter */
	if (params->privacy)
		wsec |= WEP_ENABLED;

	err = brcmf_dev_intvar_set(ndev, "wsec", wsec);
	if (err) {
		WL_ERR("wsec failed (%d)\n", err);
		goto done;
	}

	/* Configure Beacon Interval for starter */
	if (params->beacon_interval)
		bcnprd = params->beacon_interval;
	else
		bcnprd = 100;

	err = brcmf_exec_dcmd_u32(ndev, BRCM_SET_BCNPRD, &bcnprd);
	if (err) {
		WL_ERR("WLC_SET_BCNPRD failed (%d)\n", err);
		goto done;
	}

	/* Configure required join parameter */
	memset(&join_params, 0, sizeof(struct brcmf_join_params));

	/* SSID */
	ssid.SSID_len = min_t(u32, params->ssid_len, 32);
	memcpy(ssid.SSID, params->ssid, ssid.SSID_len);
	memcpy(join_params.ssid_le.SSID, params->ssid, ssid.SSID_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(ssid.SSID_len);
	join_params_size = sizeof(join_params.ssid_le);
	brcmf_update_prof(cfg_priv, NULL, &ssid, WL_PROF_SSID);

	/* BSSID */
	if (params->bssid) {
		memcpy(join_params.params_le.bssid, params->bssid, ETH_ALEN);
		join_params_size = sizeof(join_params.ssid_le) +
				   BRCMF_ASSOC_PARAMS_FIXED_SIZE;
	} else {
		memcpy(join_params.params_le.bssid, ether_bcast, ETH_ALEN);
	}

	brcmf_update_prof(cfg_priv, NULL,
			  &join_params.params_le.bssid, WL_PROF_BSSID);

	/* Channel */
	if (params->channel) {
		u32 target_channel;

		cfg_priv->channel =
			ieee80211_frequency_to_channel(
				params->channel->center_freq);
		if (params->channel_fixed) {
			/* adding chanspec */
			brcmf_ch_to_chanspec(cfg_priv->channel,
				&join_params, &join_params_size);
		}

		/* set channel for starter */
		target_channel = cfg_priv->channel;
		err = brcmf_exec_dcmd_u32(ndev, BRCM_SET_CHANNEL,
					  &target_channel);
		if (err) {
			WL_ERR("WLC_SET_CHANNEL failed (%d)\n", err);
			goto done;
		}
	} else
		cfg_priv->channel = 0;

	cfg_priv->ibss_starter = false;


	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_SSID,
			   &join_params, join_params_size);
	if (err) {
		WL_ERR("WLC_SET_SSID failed (%d)\n", err);
		goto done;
	}

done:
	if (err)
		clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_leave_ibss(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	brcmf_link_down(cfg_priv);

	WL_TRACE("Exit\n");

	return err;
}

static s32 brcmf_set_wpa_version(struct net_device *ndev,
				 struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_1)
		val = WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED;
	else if (sme->crypto.wpa_versions & NL80211_WPA_VERSION_2)
		val = WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED;
	else
		val = WPA_AUTH_DISABLED;
	WL_CONN("setting wpa_auth to 0x%0x\n", val);
	err = brcmf_dev_intvar_set(ndev, "wpa_auth", val);
	if (err) {
		WL_ERR("set wpa_auth failed (%d)\n", err);
		return err;
	}
	sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
	sec->wpa_versions = sme->crypto.wpa_versions;
	return err;
}

static s32 brcmf_set_auth_type(struct net_device *ndev,
			       struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	switch (sme->auth_type) {
	case NL80211_AUTHTYPE_OPEN_SYSTEM:
		val = 0;
		WL_CONN("open system\n");
		break;
	case NL80211_AUTHTYPE_SHARED_KEY:
		val = 1;
		WL_CONN("shared key\n");
		break;
	case NL80211_AUTHTYPE_AUTOMATIC:
		val = 2;
		WL_CONN("automatic\n");
		break;
	case NL80211_AUTHTYPE_NETWORK_EAP:
		WL_CONN("network eap\n");
	default:
		val = 2;
		WL_ERR("invalid auth type (%d)\n", sme->auth_type);
		break;
	}

	err = brcmf_dev_intvar_set(ndev, "auth", val);
	if (err) {
		WL_ERR("set auth failed (%d)\n", err);
		return err;
	}
	sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
	sec->auth_type = sme->auth_type;
	return err;
}

static s32
brcmf_set_set_cipher(struct net_device *ndev,
		     struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 pval = 0;
	s32 gval = 0;
	s32 err = 0;

	if (sme->crypto.n_ciphers_pairwise) {
		switch (sme->crypto.ciphers_pairwise[0]) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			pval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			pval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			pval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			pval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher pairwise (%d)\n",
			       sme->crypto.ciphers_pairwise[0]);
			return -EINVAL;
		}
	}
	if (sme->crypto.cipher_group) {
		switch (sme->crypto.cipher_group) {
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			gval = WEP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			gval = TKIP_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			gval = AES_ENABLED;
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			gval = AES_ENABLED;
			break;
		default:
			WL_ERR("invalid cipher group (%d)\n",
			       sme->crypto.cipher_group);
			return -EINVAL;
		}
	}

	WL_CONN("pval (%d) gval (%d)\n", pval, gval);
	err = brcmf_dev_intvar_set(ndev, "wsec", pval | gval);
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
	sec->cipher_pairwise = sme->crypto.ciphers_pairwise[0];
	sec->cipher_group = sme->crypto.cipher_group;

	return err;
}

static s32
brcmf_set_key_mgmt(struct net_device *ndev, struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct brcmf_cfg80211_security *sec;
	s32 val = 0;
	s32 err = 0;

	if (sme->crypto.n_akm_suites) {
		err = brcmf_dev_intvar_get(ndev, "wpa_auth", &val);
		if (err) {
			WL_ERR("could not get wpa_auth (%d)\n", err);
			return err;
		}
		if (val & (WPA_AUTH_PSK | WPA_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		} else if (val & (WPA2_AUTH_PSK | WPA2_AUTH_UNSPECIFIED)) {
			switch (sme->crypto.akm_suites[0]) {
			case WLAN_AKM_SUITE_8021X:
				val = WPA2_AUTH_UNSPECIFIED;
				break;
			case WLAN_AKM_SUITE_PSK:
				val = WPA2_AUTH_PSK;
				break;
			default:
				WL_ERR("invalid cipher group (%d)\n",
				       sme->crypto.cipher_group);
				return -EINVAL;
			}
		}

		WL_CONN("setting wpa_auth to %d\n", val);
		err = brcmf_dev_intvar_set(ndev, "wpa_auth", val);
		if (err) {
			WL_ERR("could not set wpa_auth (%d)\n", err);
			return err;
		}
	}
	sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
	sec->wpa_auth = sme->crypto.akm_suites[0];

	return err;
}

static s32
brcmf_set_wep_sharedkey(struct net_device *ndev,
		     struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	struct brcmf_cfg80211_security *sec;
	struct brcmf_wsec_key key;
	s32 val;
	s32 err = 0;

	WL_CONN("key len (%d)\n", sme->key_len);

	if (sme->key_len == 0)
		return 0;

	sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
	WL_CONN("wpa_versions 0x%x cipher_pairwise 0x%x\n",
		sec->wpa_versions, sec->cipher_pairwise);

	if (sec->wpa_versions & (NL80211_WPA_VERSION_1 | NL80211_WPA_VERSION_2))
		return 0;

	if (sec->cipher_pairwise &
	    (WLAN_CIPHER_SUITE_WEP40 | WLAN_CIPHER_SUITE_WEP104)) {
		memset(&key, 0, sizeof(key));
		key.len = (u32) sme->key_len;
		key.index = (u32) sme->key_idx;
		if (key.len > sizeof(key.data)) {
			WL_ERR("Too long key length (%u)\n", key.len);
			return -EINVAL;
		}
		memcpy(key.data, sme->key, key.len);
		key.flags = BRCMF_PRIMARY_KEY;
		switch (sec->cipher_pairwise) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			break;
		default:
			WL_ERR("Invalid algorithm (%d)\n",
			       sme->crypto.ciphers_pairwise[0]);
			return -EINVAL;
		}
		/* Set the new key/index */
		WL_CONN("key length (%d) key index (%d) algo (%d)\n",
			key.len, key.index, key.algo);
		WL_CONN("key \"%s\"\n", key.data);
		err = send_key_to_dongle(ndev, &key);
		if (err)
			return err;

		if (sec->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM) {
			WL_CONN("set auth_type to shared key\n");
			val = 1;	/* shared key */
			err = brcmf_dev_intvar_set(ndev, "auth", val);
			if (err) {
				WL_ERR("set auth failed (%d)\n", err);
				return err;
			}
		}
	}
	return err;
}

static s32
brcmf_cfg80211_connect(struct wiphy *wiphy, struct net_device *ndev,
		    struct cfg80211_connect_params *sme)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct ieee80211_channel *chan = sme->channel;
	struct brcmf_join_params join_params;
	size_t join_params_size;
	struct brcmf_ssid ssid;

	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	if (!sme->ssid) {
		WL_ERR("Invalid ssid\n");
		return -EOPNOTSUPP;
	}

	set_bit(WL_STATUS_CONNECTING, &cfg_priv->status);

	if (chan) {
		cfg_priv->channel =
			ieee80211_frequency_to_channel(chan->center_freq);
		WL_CONN("channel (%d), center_req (%d)\n",
				cfg_priv->channel, chan->center_freq);
	} else
		cfg_priv->channel = 0;

	WL_INFO("ie (%p), ie_len (%zd)\n", sme->ie, sme->ie_len);

	err = brcmf_set_wpa_version(ndev, sme);
	if (err) {
		WL_ERR("wl_set_wpa_version failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_auth_type(ndev, sme);
	if (err) {
		WL_ERR("wl_set_auth_type failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_set_cipher(ndev, sme);
	if (err) {
		WL_ERR("wl_set_set_cipher failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_key_mgmt(ndev, sme);
	if (err) {
		WL_ERR("wl_set_key_mgmt failed (%d)\n", err);
		goto done;
	}

	err = brcmf_set_wep_sharedkey(ndev, sme);
	if (err) {
		WL_ERR("brcmf_set_wep_sharedkey failed (%d)\n", err);
		goto done;
	}

	memset(&join_params, 0, sizeof(join_params));
	join_params_size = sizeof(join_params.ssid_le);

	ssid.SSID_len = min_t(u32, sizeof(ssid.SSID), (u32)sme->ssid_len);
	memcpy(&join_params.ssid_le.SSID, sme->ssid, ssid.SSID_len);
	memcpy(&ssid.SSID, sme->ssid, ssid.SSID_len);
	join_params.ssid_le.SSID_len = cpu_to_le32(ssid.SSID_len);
	brcmf_update_prof(cfg_priv, NULL, &ssid, WL_PROF_SSID);

	memcpy(join_params.params_le.bssid, ether_bcast, ETH_ALEN);

	if (ssid.SSID_len < IEEE80211_MAX_SSID_LEN)
		WL_CONN("ssid \"%s\", len (%d)\n",
		       ssid.SSID, ssid.SSID_len);

	brcmf_ch_to_chanspec(cfg_priv->channel,
			     &join_params, &join_params_size);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_SSID,
			   &join_params, join_params_size);
	if (err)
		WL_ERR("WLC_SET_SSID failed (%d)\n", err);

done:
	if (err)
		clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *ndev,
		       u16 reason_code)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct brcmf_scb_val_le scbval;
	s32 err = 0;

	WL_TRACE("Enter. Reason code = %d\n", reason_code);
	if (!check_sys_up(wiphy))
		return -EIO;

	clear_bit(WL_STATUS_CONNECTED, &cfg_priv->status);

	memcpy(&scbval.ea, brcmf_read_prof(cfg_priv, WL_PROF_BSSID), ETH_ALEN);
	scbval.val = cpu_to_le32(reason_code);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_DISASSOC, &scbval,
			      sizeof(struct brcmf_scb_val_le));
	if (err)
		WL_ERR("error (%d)\n", err);

	cfg_priv->link_up = false;

	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_tx_power(struct wiphy *wiphy,
			    enum nl80211_tx_power_setting type, s32 mbm)
{

	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);
	u16 txpwrmw;
	s32 err = 0;
	s32 disable = 0;
	s32 dbm = MBM_TO_DBM(mbm);

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	switch (type) {
	case NL80211_TX_POWER_AUTOMATIC:
		break;
	case NL80211_TX_POWER_LIMITED:
	case NL80211_TX_POWER_FIXED:
		if (dbm < 0) {
			WL_ERR("TX_POWER_FIXED - dbm is negative\n");
			err = -EINVAL;
			goto done;
		}
		break;
	}
	/* Make sure radio is off or on as far as software is concerned */
	disable = WL_RADIO_SW_DISABLE << 16;
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_RADIO, &disable);
	if (err)
		WL_ERR("WLC_SET_RADIO error (%d)\n", err);

	if (dbm > 0xffff)
		txpwrmw = 0xffff;
	else
		txpwrmw = (u16) dbm;
	err = brcmf_dev_intvar_set(ndev, "qtxpower",
			(s32) (brcmf_mw_to_qdbm(txpwrmw)));
	if (err)
		WL_ERR("qtxpower error (%d)\n", err);
	cfg_priv->conf->tx_power = dbm;

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_cfg80211_get_tx_power(struct wiphy *wiphy, s32 *dbm)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);
	s32 txpwrdbm;
	u8 result;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	err = brcmf_dev_intvar_get(ndev, "qtxpower", &txpwrdbm);
	if (err) {
		WL_ERR("error (%d)\n", err);
		goto done;
	}

	result = (u8) (txpwrdbm & ~WL_TXPWR_OVERRIDE);
	*dbm = (s32) brcmf_qdbm_to_mw(result);

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_config_default_key(struct wiphy *wiphy, struct net_device *ndev,
			       u8 key_idx, bool unicast, bool multicast)
{
	u32 index;
	u32 wsec;
	s32 err = 0;

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_sys_up(wiphy))
		return -EIO;

	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_GET_WSEC, &wsec);
	if (err) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		goto done;
	}

	if (wsec & WEP_ENABLED) {
		/* Just select a new current key */
		index = key_idx;
		err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_KEY_PRIMARY,
					  &index);
		if (err)
			WL_ERR("error (%d)\n", err);
	}
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_add_keyext(struct wiphy *wiphy, struct net_device *ndev,
	      u8 key_idx, const u8 *mac_addr, struct key_params *params)
{
	struct brcmf_wsec_key key;
	struct brcmf_wsec_key_le key_le;
	s32 err = 0;

	memset(&key, 0, sizeof(key));
	key.index = (u32) key_idx;
	/* Instead of bcast for ea address for default wep keys,
		 driver needs it to be Null */
	if (!is_multicast_ether_addr(mac_addr))
		memcpy((char *)&key.ea, (void *)mac_addr, ETH_ALEN);
	key.len = (u32) params->key_len;
	/* check for key index change */
	if (key.len == 0) {
		/* key delete */
		err = send_key_to_dongle(ndev, &key);
		if (err)
			return err;
	} else {
		if (key.len > sizeof(key.data)) {
			WL_ERR("Invalid key length (%d)\n", key.len);
			return -EINVAL;
		}

		WL_CONN("Setting the key index %d\n", key.index);
		memcpy(key.data, params->key, key.len);

		if (params->cipher == WLAN_CIPHER_SUITE_TKIP) {
			u8 keybuf[8];
			memcpy(keybuf, &key.data[24], sizeof(keybuf));
			memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
			memcpy(&key.data[16], keybuf, sizeof(keybuf));
		}

		/* if IW_ENCODE_EXT_RX_SEQ_VALID set */
		if (params->seq && params->seq_len == 6) {
			/* rx iv */
			u8 *ivptr;
			ivptr = (u8 *) params->seq;
			key.rxiv.hi = (ivptr[5] << 24) | (ivptr[4] << 16) |
			    (ivptr[3] << 8) | ivptr[2];
			key.rxiv.lo = (ivptr[1] << 8) | ivptr[0];
			key.iv_initialized = true;
		}

		switch (params->cipher) {
		case WLAN_CIPHER_SUITE_WEP40:
			key.algo = CRYPTO_ALGO_WEP1;
			WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
			break;
		case WLAN_CIPHER_SUITE_WEP104:
			key.algo = CRYPTO_ALGO_WEP128;
			WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			key.algo = CRYPTO_ALGO_TKIP;
			WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
			break;
		case WLAN_CIPHER_SUITE_AES_CMAC:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			key.algo = CRYPTO_ALGO_AES_CCM;
			WL_CONN("WLAN_CIPHER_SUITE_CCMP\n");
			break;
		default:
			WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
			return -EINVAL;
		}
		convert_key_from_CPU(&key, &key_le);

		brcmf_netdev_wait_pend8021x(ndev);
		err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_KEY, &key_le,
				      sizeof(key_le));
		if (err) {
			WL_ERR("WLC_SET_KEY error (%d)\n", err);
			return err;
		}
	}
	return err;
}

static s32
brcmf_cfg80211_add_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr,
		    struct key_params *params)
{
	struct brcmf_wsec_key key;
	s32 val;
	s32 wsec;
	s32 err = 0;
	u8 keybuf[8];

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_sys_up(wiphy))
		return -EIO;

	if (mac_addr) {
		WL_TRACE("Exit");
		return brcmf_add_keyext(wiphy, ndev, key_idx, mac_addr, params);
	}
	memset(&key, 0, sizeof(key));

	key.len = (u32) params->key_len;
	key.index = (u32) key_idx;

	if (key.len > sizeof(key.data)) {
		WL_ERR("Too long key length (%u)\n", key.len);
		err = -EINVAL;
		goto done;
	}
	memcpy(key.data, params->key, key.len);

	key.flags = BRCMF_PRIMARY_KEY;
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		key.algo = CRYPTO_ALGO_WEP1;
		WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		key.algo = CRYPTO_ALGO_WEP128;
		WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		memcpy(keybuf, &key.data[24], sizeof(keybuf));
		memcpy(&key.data[24], &key.data[16], sizeof(keybuf));
		memcpy(&key.data[16], keybuf, sizeof(keybuf));
		key.algo = CRYPTO_ALGO_TKIP;
		WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		key.algo = CRYPTO_ALGO_AES_CCM;
		WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		key.algo = CRYPTO_ALGO_AES_CCM;
		WL_CONN("WLAN_CIPHER_SUITE_CCMP\n");
		break;
	default:
		WL_ERR("Invalid cipher (0x%x)\n", params->cipher);
		err = -EINVAL;
		goto done;
	}

	err = send_key_to_dongle(ndev, &key); /* Set the new key/index */
	if (err)
		goto done;

	val = WEP_ENABLED;
	err = brcmf_dev_intvar_get(ndev, "wsec", &wsec);
	if (err) {
		WL_ERR("get wsec error (%d)\n", err);
		goto done;
	}
	wsec &= ~(WEP_ENABLED);
	wsec |= val;
	err = brcmf_dev_intvar_set(ndev, "wsec", wsec);
	if (err) {
		WL_ERR("set wsec error (%d)\n", err);
		goto done;
	}

	val = 1;		/* assume shared key. otherwise 0 */
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_AUTH, &val);
	if (err)
		WL_ERR("WLC_SET_AUTH error (%d)\n", err);
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr)
{
	struct brcmf_wsec_key key;
	s32 err = 0;
	s32 val;
	s32 wsec;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	memset(&key, 0, sizeof(key));

	key.index = (u32) key_idx;
	key.flags = BRCMF_PRIMARY_KEY;
	key.algo = CRYPTO_ALGO_OFF;

	WL_CONN("key index (%d)\n", key_idx);

	/* Set the new key/index */
	err = send_key_to_dongle(ndev, &key);
	if (err) {
		if (err == -EINVAL) {
			if (key.index >= DOT11_MAX_DEFAULT_KEYS)
				/* we ignore this key index in this case */
				WL_ERR("invalid key index (%d)\n", key_idx);
		}
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}

	val = 0;
	err = brcmf_dev_intvar_get(ndev, "wsec", &wsec);
	if (err) {
		WL_ERR("get wsec error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}
	wsec &= ~(WEP_ENABLED);
	wsec |= val;
	err = brcmf_dev_intvar_set(ndev, "wsec", wsec);
	if (err) {
		WL_ERR("set wsec error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}

	val = 0;		/* assume open key. otherwise 1 */
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_AUTH, &val);
	if (err) {
		WL_ERR("WLC_SET_AUTH error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
	}
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_get_key(struct wiphy *wiphy, struct net_device *ndev,
		    u8 key_idx, bool pairwise, const u8 *mac_addr, void *cookie,
		    void (*callback) (void *cookie, struct key_params * params))
{
	struct key_params params;
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct brcmf_cfg80211_security *sec;
	s32 wsec;
	s32 err = 0;

	WL_TRACE("Enter\n");
	WL_CONN("key index (%d)\n", key_idx);
	if (!check_sys_up(wiphy))
		return -EIO;

	memset(&params, 0, sizeof(params));

	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_GET_WSEC, &wsec);
	if (err) {
		WL_ERR("WLC_GET_WSEC error (%d)\n", err);
		/* Ignore this error, may happen during DISASSOC */
		err = -EAGAIN;
		goto done;
	}
	switch (wsec) {
	case WEP_ENABLED:
		sec = brcmf_read_prof(cfg_priv, WL_PROF_SEC);
		if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP40) {
			params.cipher = WLAN_CIPHER_SUITE_WEP40;
			WL_CONN("WLAN_CIPHER_SUITE_WEP40\n");
		} else if (sec->cipher_pairwise & WLAN_CIPHER_SUITE_WEP104) {
			params.cipher = WLAN_CIPHER_SUITE_WEP104;
			WL_CONN("WLAN_CIPHER_SUITE_WEP104\n");
		}
		break;
	case TKIP_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_TKIP;
		WL_CONN("WLAN_CIPHER_SUITE_TKIP\n");
		break;
	case AES_ENABLED:
		params.cipher = WLAN_CIPHER_SUITE_AES_CMAC;
		WL_CONN("WLAN_CIPHER_SUITE_AES_CMAC\n");
		break;
	default:
		WL_ERR("Invalid algo (0x%x)\n", wsec);
		err = -EINVAL;
		goto done;
	}
	callback(cookie, &params);

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_config_default_mgmt_key(struct wiphy *wiphy,
				    struct net_device *ndev, u8 key_idx)
{
	WL_INFO("Not supported\n");

	return -EOPNOTSUPP;
}

static s32
brcmf_cfg80211_get_station(struct wiphy *wiphy, struct net_device *ndev,
			u8 *mac, struct station_info *sinfo)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct brcmf_scb_val_le scb_val;
	int rssi;
	s32 rate;
	s32 err = 0;
	u8 *bssid = brcmf_read_prof(cfg_priv, WL_PROF_BSSID);

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	if (memcmp(mac, bssid, ETH_ALEN)) {
		WL_ERR("Wrong Mac address cfg_mac-%X:%X:%X:%X:%X:%X"
			"wl_bssid-%X:%X:%X:%X:%X:%X\n",
			mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			bssid[0], bssid[1], bssid[2], bssid[3],
			bssid[4], bssid[5]);
		err = -ENOENT;
		goto done;
	}

	/* Report the current tx rate */
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_GET_RATE, &rate);
	if (err) {
		WL_ERR("Could not get rate (%d)\n", err);
	} else {
		sinfo->filled |= STATION_INFO_TX_BITRATE;
		sinfo->txrate.legacy = rate * 5;
		WL_CONN("Rate %d Mbps\n", rate / 2);
	}

	if (test_bit(WL_STATUS_CONNECTED, &cfg_priv->status)) {
		scb_val.val = cpu_to_le32(0);
		err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_RSSI, &scb_val,
				      sizeof(struct brcmf_scb_val_le));
		if (err)
			WL_ERR("Could not get rssi (%d)\n", err);

		rssi = le32_to_cpu(scb_val.val);
		sinfo->filled |= STATION_INFO_SIGNAL;
		sinfo->signal = rssi;
		WL_CONN("RSSI %d dBm\n", rssi);
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *ndev,
			   bool enabled, s32 timeout)
{
	s32 pm;
	s32 err = 0;
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);

	WL_TRACE("Enter\n");

	/*
	 * Powersave enable/disable request is coming from the
	 * cfg80211 even before the interface is up. In that
	 * scenario, driver will be storing the power save
	 * preference in cfg_priv struct to apply this to
	 * FW later while initializing the dongle
	 */
	cfg_priv->pwr_save = enabled;
	if (!test_bit(WL_STATUS_READY, &cfg_priv->status)) {

		WL_INFO("Device is not ready,"
			"storing the value in cfg_priv struct\n");
		goto done;
	}

	pm = enabled ? PM_FAST : PM_OFF;
	WL_INFO("power save %s\n", (pm ? "enabled" : "disabled"));

	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_PM, &pm);
	if (err) {
		if (err == -ENODEV)
			WL_ERR("net_device is not ready yet\n");
		else
			WL_ERR("error (%d)\n", err);
	}
done:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_set_bitrate_mask(struct wiphy *wiphy, struct net_device *ndev,
			     const u8 *addr,
			     const struct cfg80211_bitrate_mask *mask)
{
	struct brcm_rateset_le rateset_le;
	s32 rate;
	s32 val;
	s32 err_bg;
	s32 err_a;
	u32 legacy;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	/* addr param is always NULL. ignore it */
	/* Get current rateset */
	err = brcmf_exec_dcmd(ndev, BRCM_GET_CURR_RATESET, &rateset_le,
			      sizeof(rateset_le));
	if (err) {
		WL_ERR("could not get current rateset (%d)\n", err);
		goto done;
	}

	legacy = ffs(mask->control[IEEE80211_BAND_2GHZ].legacy & 0xFFFF);
	if (!legacy)
		legacy = ffs(mask->control[IEEE80211_BAND_5GHZ].legacy &
			     0xFFFF);

	val = wl_g_rates[legacy - 1].bitrate * 100000;

	if (val < le32_to_cpu(rateset_le.count))
		/* Select rate by rateset index */
		rate = rateset_le.rates[val] & 0x7f;
	else
		/* Specified rate in bps */
		rate = val / 500000;

	WL_CONN("rate %d mbps\n", rate / 2);

	/*
	 *
	 *      Set rate override,
	 *      Since the is a/b/g-blind, both a/bg_rate are enforced.
	 */
	err_bg = brcmf_dev_intvar_set(ndev, "bg_rate", rate);
	err_a = brcmf_dev_intvar_set(ndev, "a_rate", rate);
	if (err_bg && err_a) {
		WL_ERR("could not set fixed rate (%d) (%d)\n", err_bg, err_a);
		err = err_bg | err_a;
	}

done:
	WL_TRACE("Exit\n");
	return err;
}

static s32 brcmf_inform_single_bss(struct brcmf_cfg80211_priv *cfg_priv,
				   struct brcmf_bss_info_le *bi)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg_priv);
	struct ieee80211_channel *notify_channel;
	struct cfg80211_bss *bss;
	struct ieee80211_supported_band *band;
	s32 err = 0;
	u16 channel;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	if (le32_to_cpu(bi->length) > WL_BSS_INFO_MAX) {
		WL_ERR("Bss info is larger than buffer. Discarding\n");
		return 0;
	}

	channel = bi->ctl_ch ? bi->ctl_ch :
				CHSPEC_CHANNEL(le16_to_cpu(bi->chanspec));

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_capability = le16_to_cpu(bi->capability);
	notify_interval = le16_to_cpu(bi->beacon_period);
	notify_ie = (u8 *)bi + le16_to_cpu(bi->ie_offset);
	notify_ielen = le32_to_cpu(bi->ie_length);
	notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	WL_CONN("bssid: %2.2X:%2.2X:%2.2X:%2.2X:%2.2X:%2.2X\n",
			bi->BSSID[0], bi->BSSID[1], bi->BSSID[2],
			bi->BSSID[3], bi->BSSID[4], bi->BSSID[5]);
	WL_CONN("Channel: %d(%d)\n", channel, freq);
	WL_CONN("Capability: %X\n", notify_capability);
	WL_CONN("Beacon interval: %d\n", notify_interval);
	WL_CONN("Signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel, (const u8 *)bi->BSSID,
		0, notify_capability, notify_interval, notify_ie,
		notify_ielen, notify_signal, GFP_KERNEL);

	if (!bss)
		return -ENOMEM;

	cfg80211_put_bss(bss);

	return err;
}

static struct brcmf_bss_info_le *
next_bss_le(struct brcmf_scan_results *list, struct brcmf_bss_info_le *bss)
{
	if (bss == NULL)
		return list->bss_info_le;
	return (struct brcmf_bss_info_le *)((unsigned long)bss +
					    le32_to_cpu(bss->length));
}

static s32 brcmf_inform_bss(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_scan_results *bss_list;
	struct brcmf_bss_info_le *bi = NULL;	/* must be initialized */
	s32 err = 0;
	int i;

	bss_list = cfg_priv->bss_list;
	if (bss_list->version != BRCMF_BSS_INFO_VERSION) {
		WL_ERR("Version %d != WL_BSS_INFO_VERSION\n",
		       bss_list->version);
		return -EOPNOTSUPP;
	}
	WL_SCAN("scanned AP count (%d)\n", bss_list->count);
	for (i = 0; i < bss_list->count && i < WL_AP_MAX; i++) {
		bi = next_bss_le(bss_list, bi);
		err = brcmf_inform_single_bss(cfg_priv, bi);
		if (err)
			break;
	}
	return err;
}

static s32 wl_inform_ibss(struct brcmf_cfg80211_priv *cfg_priv,
			  struct net_device *ndev, const u8 *bssid)
{
	struct wiphy *wiphy = cfg_to_wiphy(cfg_priv);
	struct ieee80211_channel *notify_channel;
	struct brcmf_bss_info_le *bi = NULL;
	struct ieee80211_supported_band *band;
	struct cfg80211_bss *bss;
	u8 *buf = NULL;
	s32 err = 0;
	u16 channel;
	u32 freq;
	u16 notify_capability;
	u16 notify_interval;
	u8 *notify_ie;
	size_t notify_ielen;
	s32 notify_signal;

	WL_TRACE("Enter\n");

	buf = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (buf == NULL) {
		err = -ENOMEM;
		goto CleanUp;
	}

	*(__le32 *)buf = cpu_to_le32(WL_BSS_INFO_MAX);

	err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_BSS_INFO, buf, WL_BSS_INFO_MAX);
	if (err) {
		WL_ERR("WLC_GET_BSS_INFO failed: %d\n", err);
		goto CleanUp;
	}

	bi = (struct brcmf_bss_info_le *)(buf + 4);

	channel = bi->ctl_ch ? bi->ctl_ch :
				CHSPEC_CHANNEL(le16_to_cpu(bi->chanspec));

	if (channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	notify_capability = le16_to_cpu(bi->capability);
	notify_interval = le16_to_cpu(bi->beacon_period);
	notify_ie = (u8 *)bi + le16_to_cpu(bi->ie_offset);
	notify_ielen = le32_to_cpu(bi->ie_length);
	notify_signal = (s16)le16_to_cpu(bi->RSSI) * 100;

	WL_CONN("channel: %d(%d)\n", channel, freq);
	WL_CONN("capability: %X\n", notify_capability);
	WL_CONN("beacon interval: %d\n", notify_interval);
	WL_CONN("signal: %d\n", notify_signal);

	bss = cfg80211_inform_bss(wiphy, notify_channel, bssid,
		0, notify_capability, notify_interval,
		notify_ie, notify_ielen, notify_signal, GFP_KERNEL);

	if (!bss) {
		err = -ENOMEM;
		goto CleanUp;
	}

	cfg80211_put_bss(bss);

CleanUp:

	kfree(buf);

	WL_TRACE("Exit\n");

	return err;
}

static bool brcmf_is_ibssmode(struct brcmf_cfg80211_priv *cfg_priv)
{
	return cfg_priv->conf->mode == WL_MODE_IBSS;
}

/*
 * Traverse a string of 1-byte tag/1-byte length/variable-length value
 * triples, returning a pointer to the substring whose first element
 * matches tag
 */
static struct brcmf_tlv *brcmf_parse_tlvs(void *buf, int buflen, uint key)
{
	struct brcmf_tlv *elt;
	int totlen;

	elt = (struct brcmf_tlv *) buf;
	totlen = buflen;

	/* find tagged parameter */
	while (totlen >= 2) {
		int len = elt->len;

		/* validate remaining totlen */
		if ((elt->id == key) && (totlen >= (len + 2)))
			return elt;

		elt = (struct brcmf_tlv *) ((u8 *) elt + (len + 2));
		totlen -= (len + 2);
	}

	return NULL;
}

static s32 brcmf_update_bss_info(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_bss_info_le *bi;
	struct brcmf_ssid *ssid;
	struct brcmf_tlv *tim;
	u16 beacon_interval;
	u8 dtim_period;
	size_t ie_len;
	u8 *ie;
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (brcmf_is_ibssmode(cfg_priv))
		return err;

	ssid = (struct brcmf_ssid *)brcmf_read_prof(cfg_priv, WL_PROF_SSID);

	*(__le32 *)cfg_priv->extra_buf = cpu_to_le32(WL_EXTRA_BUF_MAX);
	err = brcmf_exec_dcmd(cfg_to_ndev(cfg_priv), BRCMF_C_GET_BSS_INFO,
			cfg_priv->extra_buf, WL_EXTRA_BUF_MAX);
	if (err) {
		WL_ERR("Could not get bss info %d\n", err);
		goto update_bss_info_out;
	}

	bi = (struct brcmf_bss_info_le *)(cfg_priv->extra_buf + 4);
	err = brcmf_inform_single_bss(cfg_priv, bi);
	if (err)
		goto update_bss_info_out;

	ie = ((u8 *)bi) + le16_to_cpu(bi->ie_offset);
	ie_len = le32_to_cpu(bi->ie_length);
	beacon_interval = le16_to_cpu(bi->beacon_period);

	tim = brcmf_parse_tlvs(ie, ie_len, WLAN_EID_TIM);
	if (tim)
		dtim_period = tim->data[1];
	else {
		/*
		* active scan was done so we could not get dtim
		* information out of probe response.
		* so we speficially query dtim information to dongle.
		*/
		u32 var;
		err = brcmf_dev_intvar_get(cfg_to_ndev(cfg_priv),
					   "dtim_assoc", &var);
		if (err) {
			WL_ERR("wl dtim_assoc failed (%d)\n", err);
			goto update_bss_info_out;
		}
		dtim_period = (u8)var;
	}

	brcmf_update_prof(cfg_priv, NULL, &beacon_interval, WL_PROF_BEACONINT);
	brcmf_update_prof(cfg_priv, NULL, &dtim_period, WL_PROF_DTIMPERIOD);

update_bss_info_out:
	WL_TRACE("Exit");
	return err;
}

static void brcmf_term_iscan(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_to_iscan(cfg_priv);
	struct brcmf_ssid ssid;

	if (cfg_priv->iscan_on) {
		iscan->state = WL_ISCAN_STATE_IDLE;

		if (iscan->timer_on) {
			del_timer_sync(&iscan->timer);
			iscan->timer_on = 0;
		}

		cancel_work_sync(&iscan->work);

		/* Abort iscan running in FW */
		memset(&ssid, 0, sizeof(ssid));
		brcmf_run_iscan(iscan, &ssid, WL_SCAN_ACTION_ABORT);
	}
}

static void brcmf_notify_iscan_complete(struct brcmf_cfg80211_iscan_ctrl *iscan,
					bool aborted)
{
	struct brcmf_cfg80211_priv *cfg_priv = iscan_to_cfg(iscan);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);

	if (!test_and_clear_bit(WL_STATUS_SCANNING, &cfg_priv->status)) {
		WL_ERR("Scan complete while device not scanning\n");
		return;
	}
	if (cfg_priv->scan_request) {
		WL_SCAN("ISCAN Completed scan: %s\n",
				aborted ? "Aborted" : "Done");
		cfg80211_scan_done(cfg_priv->scan_request, aborted);
		brcmf_set_mpc(ndev, 1);
		cfg_priv->scan_request = NULL;
	}
	cfg_priv->iscan_kickstart = false;
}

static s32 brcmf_wakeup_iscan(struct brcmf_cfg80211_iscan_ctrl *iscan)
{
	if (iscan->state != WL_ISCAN_STATE_IDLE) {
		WL_SCAN("wake up iscan\n");
		schedule_work(&iscan->work);
		return 0;
	}

	return -EIO;
}

static s32
brcmf_get_iscan_results(struct brcmf_cfg80211_iscan_ctrl *iscan, u32 *status,
		     struct brcmf_scan_results **bss_list)
{
	struct brcmf_iscan_results list;
	struct brcmf_scan_results *results;
	struct brcmf_scan_results_le *results_le;
	struct brcmf_iscan_results *list_buf;
	s32 err = 0;

	memset(iscan->scan_buf, 0, WL_ISCAN_BUF_MAX);
	list_buf = (struct brcmf_iscan_results *)iscan->scan_buf;
	results = &list_buf->results;
	results_le = &list_buf->results_le;
	results->buflen = BRCMF_ISCAN_RESULTS_FIXED_SIZE;
	results->version = 0;
	results->count = 0;

	memset(&list, 0, sizeof(list));
	list.results_le.buflen = cpu_to_le32(WL_ISCAN_BUF_MAX);
	err = brcmf_dev_iovar_getbuf(iscan->ndev, "iscanresults", &list,
				     BRCMF_ISCAN_RESULTS_FIXED_SIZE,
				     iscan->scan_buf, WL_ISCAN_BUF_MAX);
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	results->buflen = le32_to_cpu(results_le->buflen);
	results->version = le32_to_cpu(results_le->version);
	results->count = le32_to_cpu(results_le->count);
	WL_SCAN("results->count = %d\n", results_le->count);
	WL_SCAN("results->buflen = %d\n", results_le->buflen);
	*status = le32_to_cpu(list_buf->status_le);
	WL_SCAN("status = %d\n", *status);
	*bss_list = results;

	return err;
}

static s32 brcmf_iscan_done(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_priv->iscan;
	s32 err = 0;

	iscan->state = WL_ISCAN_STATE_IDLE;
	brcmf_inform_bss(cfg_priv);
	brcmf_notify_iscan_complete(iscan, false);

	return err;
}

static s32 brcmf_iscan_pending(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_priv->iscan;
	s32 err = 0;

	/* Reschedule the timer */
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;

	return err;
}

static s32 brcmf_iscan_inprogress(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_priv->iscan;
	s32 err = 0;

	brcmf_inform_bss(cfg_priv);
	brcmf_run_iscan(iscan, NULL, BRCMF_SCAN_ACTION_CONTINUE);
	/* Reschedule the timer */
	mod_timer(&iscan->timer, jiffies + iscan->timer_ms * HZ / 1000);
	iscan->timer_on = 1;

	return err;
}

static s32 brcmf_iscan_aborted(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_priv->iscan;
	s32 err = 0;

	iscan->state = WL_ISCAN_STATE_IDLE;
	brcmf_notify_iscan_complete(iscan, true);

	return err;
}

static void brcmf_cfg80211_iscan_handler(struct work_struct *work)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan =
			container_of(work, struct brcmf_cfg80211_iscan_ctrl,
				     work);
	struct brcmf_cfg80211_priv *cfg_priv = iscan_to_cfg(iscan);
	struct brcmf_cfg80211_iscan_eloop *el = &iscan->el;
	u32 status = BRCMF_SCAN_RESULTS_PARTIAL;

	if (iscan->timer_on) {
		del_timer_sync(&iscan->timer);
		iscan->timer_on = 0;
	}

	if (brcmf_get_iscan_results(iscan, &status, &cfg_priv->bss_list)) {
		status = BRCMF_SCAN_RESULTS_ABORTED;
		WL_ERR("Abort iscan\n");
	}

	el->handler[status](cfg_priv);
}

static void brcmf_iscan_timer(unsigned long data)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan =
			(struct brcmf_cfg80211_iscan_ctrl *)data;

	if (iscan) {
		iscan->timer_on = 0;
		WL_SCAN("timer expired\n");
		brcmf_wakeup_iscan(iscan);
	}
}

static s32 brcmf_invoke_iscan(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_to_iscan(cfg_priv);

	if (cfg_priv->iscan_on) {
		iscan->state = WL_ISCAN_STATE_IDLE;
		INIT_WORK(&iscan->work, brcmf_cfg80211_iscan_handler);
	}

	return 0;
}

static void brcmf_init_iscan_eloop(struct brcmf_cfg80211_iscan_eloop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[BRCMF_SCAN_RESULTS_SUCCESS] = brcmf_iscan_done;
	el->handler[BRCMF_SCAN_RESULTS_PARTIAL] = brcmf_iscan_inprogress;
	el->handler[BRCMF_SCAN_RESULTS_PENDING] = brcmf_iscan_pending;
	el->handler[BRCMF_SCAN_RESULTS_ABORTED] = brcmf_iscan_aborted;
	el->handler[BRCMF_SCAN_RESULTS_NO_MEM] = brcmf_iscan_aborted;
}

static s32 brcmf_init_iscan(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_iscan_ctrl *iscan = cfg_to_iscan(cfg_priv);
	int err = 0;

	if (cfg_priv->iscan_on) {
		iscan->ndev = cfg_to_ndev(cfg_priv);
		brcmf_init_iscan_eloop(&iscan->el);
		iscan->timer_ms = WL_ISCAN_TIMER_INTERVAL_MS;
		init_timer(&iscan->timer);
		iscan->timer.data = (unsigned long) iscan;
		iscan->timer.function = brcmf_iscan_timer;
		err = brcmf_invoke_iscan(cfg_priv);
		if (!err)
			iscan->data = cfg_priv;
	}

	return err;
}

static __always_inline void brcmf_delay(u32 ms)
{
	if (ms < 1000 / HZ) {
		cond_resched();
		mdelay(ms);
	} else {
		msleep(ms);
	}
}

static s32 brcmf_cfg80211_resume(struct wiphy *wiphy)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);

	/*
	 * Check for WL_STATUS_READY before any function call which
	 * could result is bus access. Don't block the resume for
	 * any driver error conditions
	 */
	WL_TRACE("Enter\n");

	if (test_bit(WL_STATUS_READY, &cfg_priv->status))
		brcmf_invoke_iscan(wiphy_to_cfg(wiphy));

	WL_TRACE("Exit\n");
	return 0;
}

static s32 brcmf_cfg80211_suspend(struct wiphy *wiphy,
				  struct cfg80211_wowlan *wow)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct net_device *ndev = cfg_to_ndev(cfg_priv);

	WL_TRACE("Enter\n");

	/*
	 * Check for WL_STATUS_READY before any function call which
	 * could result is bus access. Don't block the suspend for
	 * any driver error conditions
	 */

	/*
	 * While going to suspend if associated with AP disassociate
	 * from AP to save power while system is in suspended state
	 */
	if ((test_bit(WL_STATUS_CONNECTED, &cfg_priv->status) ||
	     test_bit(WL_STATUS_CONNECTING, &cfg_priv->status)) &&
	     test_bit(WL_STATUS_READY, &cfg_priv->status)) {
		WL_INFO("Disassociating from AP"
			" while entering suspend state\n");
		brcmf_link_down(cfg_priv);

		/*
		 * Make sure WPA_Supplicant receives all the event
		 * generated due to DISASSOC call to the fw to keep
		 * the state fw and WPA_Supplicant state consistent
		 */
		brcmf_delay(500);
	}

	set_bit(WL_STATUS_SCAN_ABORTING, &cfg_priv->status);
	if (test_bit(WL_STATUS_READY, &cfg_priv->status))
		brcmf_term_iscan(cfg_priv);

	if (cfg_priv->scan_request) {
		/* Indidate scan abort to cfg80211 layer */
		WL_INFO("Terminating scan in progress\n");
		cfg80211_scan_done(cfg_priv->scan_request, true);
		cfg_priv->scan_request = NULL;
	}
	clear_bit(WL_STATUS_SCANNING, &cfg_priv->status);
	clear_bit(WL_STATUS_SCAN_ABORTING, &cfg_priv->status);

	/* Turn off watchdog timer */
	if (test_bit(WL_STATUS_READY, &cfg_priv->status)) {
		WL_INFO("Enable MPC\n");
		brcmf_set_mpc(ndev, 1);
	}

	WL_TRACE("Exit\n");

	return 0;
}

static __used s32
brcmf_dev_bufvar_set(struct net_device *ndev, s8 *name, s8 *buf, s32 len)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	u32 buflen;

	buflen = brcmf_c_mkiovar(name, buf, len, cfg_priv->dcmd_buf,
			       WL_DCMD_LEN_MAX);
	BUG_ON(!buflen);

	return brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR, cfg_priv->dcmd_buf,
			       buflen);
}

static s32
brcmf_dev_bufvar_get(struct net_device *ndev, s8 *name, s8 *buf,
		  s32 buf_len)
{
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);
	u32 len;
	s32 err = 0;

	len = brcmf_c_mkiovar(name, NULL, 0, cfg_priv->dcmd_buf,
			    WL_DCMD_LEN_MAX);
	BUG_ON(!len);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_VAR, cfg_priv->dcmd_buf,
			      WL_DCMD_LEN_MAX);
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}
	memcpy(buf, cfg_priv->dcmd_buf, buf_len);

	return err;
}

static __used s32
brcmf_update_pmklist(struct net_device *ndev,
		     struct brcmf_cfg80211_pmk_list *pmk_list, s32 err)
{
	int i, j;
	int pmkid_len;

	pmkid_len = le32_to_cpu(pmk_list->pmkids.npmkid);

	WL_CONN("No of elements %d\n", pmkid_len);
	for (i = 0; i < pmkid_len; i++) {
		WL_CONN("PMKID[%d]: %pM =\n", i,
			&pmk_list->pmkids.pmkid[i].BSSID);
		for (j = 0; j < WLAN_PMKID_LEN; j++)
			WL_CONN("%02x\n", pmk_list->pmkids.pmkid[i].PMKID[j]);
	}

	if (!err)
		brcmf_dev_bufvar_set(ndev, "pmkid_info", (char *)pmk_list,
					sizeof(*pmk_list));

	return err;
}

static s32
brcmf_cfg80211_set_pmksa(struct wiphy *wiphy, struct net_device *ndev,
			 struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct pmkid_list *pmkids = &cfg_priv->pmk_list->pmkids;
	s32 err = 0;
	int i;
	int pmkid_len;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	pmkid_len = le32_to_cpu(pmkids->npmkid);
	for (i = 0; i < pmkid_len; i++)
		if (!memcmp(pmksa->bssid, pmkids->pmkid[i].BSSID, ETH_ALEN))
			break;
	if (i < WL_NUM_PMKIDS_MAX) {
		memcpy(pmkids->pmkid[i].BSSID, pmksa->bssid, ETH_ALEN);
		memcpy(pmkids->pmkid[i].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);
		if (i == pmkid_len) {
			pmkid_len++;
			pmkids->npmkid = cpu_to_le32(pmkid_len);
		}
	} else
		err = -EINVAL;

	WL_CONN("set_pmksa,IW_PMKSA_ADD - PMKID: %pM =\n",
		pmkids->pmkid[pmkid_len].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++)
		WL_CONN("%02x\n", pmkids->pmkid[pmkid_len].PMKID[i]);

	err = brcmf_update_pmklist(ndev, cfg_priv->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_cfg80211_del_pmksa(struct wiphy *wiphy, struct net_device *ndev,
		      struct cfg80211_pmksa *pmksa)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	struct pmkid_list pmkid;
	s32 err = 0;
	int i, pmkid_len;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	memcpy(&pmkid.pmkid[0].BSSID, pmksa->bssid, ETH_ALEN);
	memcpy(&pmkid.pmkid[0].PMKID, pmksa->pmkid, WLAN_PMKID_LEN);

	WL_CONN("del_pmksa,IW_PMKSA_REMOVE - PMKID: %pM =\n",
	       &pmkid.pmkid[0].BSSID);
	for (i = 0; i < WLAN_PMKID_LEN; i++)
		WL_CONN("%02x\n", pmkid.pmkid[0].PMKID[i]);

	pmkid_len = le32_to_cpu(cfg_priv->pmk_list->pmkids.npmkid);
	for (i = 0; i < pmkid_len; i++)
		if (!memcmp
		    (pmksa->bssid, &cfg_priv->pmk_list->pmkids.pmkid[i].BSSID,
		     ETH_ALEN))
			break;

	if ((pmkid_len > 0)
	    && (i < pmkid_len)) {
		memset(&cfg_priv->pmk_list->pmkids.pmkid[i], 0,
		       sizeof(struct pmkid));
		for (; i < (pmkid_len - 1); i++) {
			memcpy(&cfg_priv->pmk_list->pmkids.pmkid[i].BSSID,
			       &cfg_priv->pmk_list->pmkids.pmkid[i + 1].BSSID,
			       ETH_ALEN);
			memcpy(&cfg_priv->pmk_list->pmkids.pmkid[i].PMKID,
			       &cfg_priv->pmk_list->pmkids.pmkid[i + 1].PMKID,
			       WLAN_PMKID_LEN);
		}
		cfg_priv->pmk_list->pmkids.npmkid = cpu_to_le32(pmkid_len - 1);
	} else
		err = -EINVAL;

	err = brcmf_update_pmklist(ndev, cfg_priv->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;

}

static s32
brcmf_cfg80211_flush_pmksa(struct wiphy *wiphy, struct net_device *ndev)
{
	struct brcmf_cfg80211_priv *cfg_priv = wiphy_to_cfg(wiphy);
	s32 err = 0;

	WL_TRACE("Enter\n");
	if (!check_sys_up(wiphy))
		return -EIO;

	memset(cfg_priv->pmk_list, 0, sizeof(*cfg_priv->pmk_list));
	err = brcmf_update_pmklist(ndev, cfg_priv->pmk_list, err);

	WL_TRACE("Exit\n");
	return err;

}

static struct cfg80211_ops wl_cfg80211_ops = {
	.change_virtual_intf = brcmf_cfg80211_change_iface,
	.scan = brcmf_cfg80211_scan,
	.set_wiphy_params = brcmf_cfg80211_set_wiphy_params,
	.join_ibss = brcmf_cfg80211_join_ibss,
	.leave_ibss = brcmf_cfg80211_leave_ibss,
	.get_station = brcmf_cfg80211_get_station,
	.set_tx_power = brcmf_cfg80211_set_tx_power,
	.get_tx_power = brcmf_cfg80211_get_tx_power,
	.add_key = brcmf_cfg80211_add_key,
	.del_key = brcmf_cfg80211_del_key,
	.get_key = brcmf_cfg80211_get_key,
	.set_default_key = brcmf_cfg80211_config_default_key,
	.set_default_mgmt_key = brcmf_cfg80211_config_default_mgmt_key,
	.set_power_mgmt = brcmf_cfg80211_set_power_mgmt,
	.set_bitrate_mask = brcmf_cfg80211_set_bitrate_mask,
	.connect = brcmf_cfg80211_connect,
	.disconnect = brcmf_cfg80211_disconnect,
	.suspend = brcmf_cfg80211_suspend,
	.resume = brcmf_cfg80211_resume,
	.set_pmksa = brcmf_cfg80211_set_pmksa,
	.del_pmksa = brcmf_cfg80211_del_pmksa,
	.flush_pmksa = brcmf_cfg80211_flush_pmksa
};

static s32 brcmf_mode_to_nl80211_iftype(s32 mode)
{
	s32 err = 0;

	switch (mode) {
	case WL_MODE_BSS:
		return NL80211_IFTYPE_STATION;
	case WL_MODE_IBSS:
		return NL80211_IFTYPE_ADHOC;
	default:
		return NL80211_IFTYPE_UNSPECIFIED;
	}

	return err;
}

static struct wireless_dev *brcmf_alloc_wdev(s32 sizeof_iface,
					  struct device *ndev)
{
	struct wireless_dev *wdev;
	s32 err = 0;

	wdev = kzalloc(sizeof(*wdev), GFP_KERNEL);
	if (!wdev)
		return ERR_PTR(-ENOMEM);

	wdev->wiphy =
	    wiphy_new(&wl_cfg80211_ops,
		      sizeof(struct brcmf_cfg80211_priv) + sizeof_iface);
	if (!wdev->wiphy) {
		WL_ERR("Could not allocate wiphy device\n");
		err = -ENOMEM;
		goto wiphy_new_out;
	}
	set_wiphy_dev(wdev->wiphy, ndev);
	wdev->wiphy->max_scan_ssids = WL_NUM_SCAN_MAX;
	wdev->wiphy->max_num_pmkids = WL_NUM_PMKIDS_MAX;
	wdev->wiphy->interface_modes =
	    BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_ADHOC);
	wdev->wiphy->bands[IEEE80211_BAND_2GHZ] = &__wl_band_2ghz;
	wdev->wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_a;	/* Set
						* it as 11a by default.
						* This will be updated with
						* 11n phy tables in
						* "ifconfig up"
						* if phy has 11n capability
						*/
	wdev->wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wdev->wiphy->cipher_suites = __wl_cipher_suites;
	wdev->wiphy->n_cipher_suites = ARRAY_SIZE(__wl_cipher_suites);
	wdev->wiphy->flags |= WIPHY_FLAG_PS_ON_BY_DEFAULT;	/* enable power
								 * save mode
								 * by default
								 */
	err = wiphy_register(wdev->wiphy);
	if (err < 0) {
		WL_ERR("Could not register wiphy device (%d)\n", err);
		goto wiphy_register_out;
	}
	return wdev;

wiphy_register_out:
	wiphy_free(wdev->wiphy);

wiphy_new_out:
	kfree(wdev);

	return ERR_PTR(err);
}

static void brcmf_free_wdev(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct wireless_dev *wdev = cfg_priv->wdev;

	if (!wdev) {
		WL_ERR("wdev is invalid\n");
		return;
	}
	wiphy_unregister(wdev->wiphy);
	wiphy_free(wdev->wiphy);
	kfree(wdev);
	cfg_priv->wdev = NULL;
}

static bool brcmf_is_linkup(struct brcmf_cfg80211_priv *cfg_priv,
			    const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_SET_SSID && status == BRCMF_E_STATUS_SUCCESS) {
		WL_CONN("Processing set ssid\n");
		cfg_priv->link_up = true;
		return true;
	}

	return false;
}

static bool brcmf_is_linkdown(struct brcmf_cfg80211_priv *cfg_priv,
			      const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u16 flags = be16_to_cpu(e->flags);

	if (event == BRCMF_E_LINK && (!(flags & BRCMF_EVENT_MSG_LINK))) {
		WL_CONN("Processing link down\n");
		return true;
	}
	return false;
}

static bool brcmf_is_nonetwork(struct brcmf_cfg80211_priv *cfg_priv,
			       const struct brcmf_event_msg *e)
{
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_LINK && status == BRCMF_E_STATUS_NO_NETWORKS) {
		WL_CONN("Processing Link %s & no network found\n",
				be16_to_cpu(e->flags) & BRCMF_EVENT_MSG_LINK ?
				"up" : "down");
		return true;
	}

	if (event == BRCMF_E_SET_SSID && status != BRCMF_E_STATUS_SUCCESS) {
		WL_CONN("Processing connecting & no network found\n");
		return true;
	}

	return false;
}

static void brcmf_clear_assoc_ies(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg_priv);

	kfree(conn_info->req_ie);
	conn_info->req_ie = NULL;
	conn_info->req_ie_len = 0;
	kfree(conn_info->resp_ie);
	conn_info->resp_ie = NULL;
	conn_info->resp_ie_len = 0;
}

static s32 brcmf_get_assoc_ies(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct net_device *ndev = cfg_to_ndev(cfg_priv);
	struct brcmf_cfg80211_assoc_ielen_le *assoc_info;
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg_priv);
	u32 req_len;
	u32 resp_len;
	s32 err = 0;

	brcmf_clear_assoc_ies(cfg_priv);

	err = brcmf_dev_bufvar_get(ndev, "assoc_info", cfg_priv->extra_buf,
				WL_ASSOC_INFO_MAX);
	if (err) {
		WL_ERR("could not get assoc info (%d)\n", err);
		return err;
	}
	assoc_info =
		(struct brcmf_cfg80211_assoc_ielen_le *)cfg_priv->extra_buf;
	req_len = le32_to_cpu(assoc_info->req_len);
	resp_len = le32_to_cpu(assoc_info->resp_len);
	if (req_len) {
		err = brcmf_dev_bufvar_get(ndev, "assoc_req_ies",
					   cfg_priv->extra_buf,
					   WL_ASSOC_INFO_MAX);
		if (err) {
			WL_ERR("could not get assoc req (%d)\n", err);
			return err;
		}
		conn_info->req_ie_len = req_len;
		conn_info->req_ie =
		    kmemdup(cfg_priv->extra_buf, conn_info->req_ie_len,
			    GFP_KERNEL);
	} else {
		conn_info->req_ie_len = 0;
		conn_info->req_ie = NULL;
	}
	if (resp_len) {
		err = brcmf_dev_bufvar_get(ndev, "assoc_resp_ies",
					   cfg_priv->extra_buf,
					   WL_ASSOC_INFO_MAX);
		if (err) {
			WL_ERR("could not get assoc resp (%d)\n", err);
			return err;
		}
		conn_info->resp_ie_len = resp_len;
		conn_info->resp_ie =
		    kmemdup(cfg_priv->extra_buf, conn_info->resp_ie_len,
			    GFP_KERNEL);
	} else {
		conn_info->resp_ie_len = 0;
		conn_info->resp_ie = NULL;
	}
	WL_CONN("req len (%d) resp len (%d)\n",
	       conn_info->req_ie_len, conn_info->resp_ie_len);

	return err;
}

static s32
brcmf_bss_roaming_done(struct brcmf_cfg80211_priv *cfg_priv,
		       struct net_device *ndev,
		       const struct brcmf_event_msg *e)
{
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg_priv);
	struct wiphy *wiphy = cfg_to_wiphy(cfg_priv);
	struct brcmf_channel_info_le channel_le;
	struct ieee80211_channel *notify_channel;
	struct ieee80211_supported_band *band;
	u32 freq;
	s32 err = 0;
	u32 target_channel;

	WL_TRACE("Enter\n");

	brcmf_get_assoc_ies(cfg_priv);
	brcmf_update_prof(cfg_priv, NULL, &e->addr, WL_PROF_BSSID);
	brcmf_update_bss_info(cfg_priv);

	brcmf_exec_dcmd(ndev, BRCMF_C_GET_CHANNEL, &channel_le,
			sizeof(channel_le));

	target_channel = le32_to_cpu(channel_le.target_channel);
	WL_CONN("Roamed to channel %d\n", target_channel);

	if (target_channel <= CH_MAX_2G_CHANNEL)
		band = wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		band = wiphy->bands[IEEE80211_BAND_5GHZ];

	freq = ieee80211_channel_to_frequency(target_channel, band->band);
	notify_channel = ieee80211_get_channel(wiphy, freq);

	cfg80211_roamed(ndev, notify_channel,
			(u8 *)brcmf_read_prof(cfg_priv, WL_PROF_BSSID),
			conn_info->req_ie, conn_info->req_ie_len,
			conn_info->resp_ie, conn_info->resp_ie_len, GFP_KERNEL);
	WL_CONN("Report roaming result\n");

	set_bit(WL_STATUS_CONNECTED, &cfg_priv->status);
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_bss_connect_done(struct brcmf_cfg80211_priv *cfg_priv,
		       struct net_device *ndev, const struct brcmf_event_msg *e,
		       bool completed)
{
	struct brcmf_cfg80211_connect_info *conn_info = cfg_to_conn(cfg_priv);
	s32 err = 0;

	WL_TRACE("Enter\n");

	if (test_and_clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status)) {
		if (completed) {
			brcmf_get_assoc_ies(cfg_priv);
			brcmf_update_prof(cfg_priv, NULL, &e->addr,
					  WL_PROF_BSSID);
			brcmf_update_bss_info(cfg_priv);
		}
		cfg80211_connect_result(ndev,
					(u8 *)brcmf_read_prof(cfg_priv,
							      WL_PROF_BSSID),
					conn_info->req_ie,
					conn_info->req_ie_len,
					conn_info->resp_ie,
					conn_info->resp_ie_len,
					completed ? WLAN_STATUS_SUCCESS :
						    WLAN_STATUS_AUTH_TIMEOUT,
					GFP_KERNEL);
		if (completed)
			set_bit(WL_STATUS_CONNECTED, &cfg_priv->status);
		WL_CONN("Report connect result - connection %s\n",
				completed ? "succeeded" : "failed");
	}
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_notify_connect_status(struct brcmf_cfg80211_priv *cfg_priv,
			    struct net_device *ndev,
			    const struct brcmf_event_msg *e, void *data)
{
	s32 err = 0;

	if (brcmf_is_linkup(cfg_priv, e)) {
		WL_CONN("Linkup\n");
		if (brcmf_is_ibssmode(cfg_priv)) {
			brcmf_update_prof(cfg_priv, NULL, (void *)e->addr,
				WL_PROF_BSSID);
			wl_inform_ibss(cfg_priv, ndev, e->addr);
			cfg80211_ibss_joined(ndev, e->addr, GFP_KERNEL);
			clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status);
			set_bit(WL_STATUS_CONNECTED, &cfg_priv->status);
		} else
			brcmf_bss_connect_done(cfg_priv, ndev, e, true);
	} else if (brcmf_is_linkdown(cfg_priv, e)) {
		WL_CONN("Linkdown\n");
		if (brcmf_is_ibssmode(cfg_priv)) {
			clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status);
			if (test_and_clear_bit(WL_STATUS_CONNECTED,
				&cfg_priv->status))
				brcmf_link_down(cfg_priv);
		} else {
			brcmf_bss_connect_done(cfg_priv, ndev, e, false);
			if (test_and_clear_bit(WL_STATUS_CONNECTED,
				&cfg_priv->status)) {
				cfg80211_disconnected(ndev, 0, NULL, 0,
					GFP_KERNEL);
				brcmf_link_down(cfg_priv);
			}
		}
		brcmf_init_prof(cfg_priv->profile);
	} else if (brcmf_is_nonetwork(cfg_priv, e)) {
		if (brcmf_is_ibssmode(cfg_priv))
			clear_bit(WL_STATUS_CONNECTING, &cfg_priv->status);
		else
			brcmf_bss_connect_done(cfg_priv, ndev, e, false);
	}

	return err;
}

static s32
brcmf_notify_roaming_status(struct brcmf_cfg80211_priv *cfg_priv,
			    struct net_device *ndev,
			    const struct brcmf_event_msg *e, void *data)
{
	s32 err = 0;
	u32 event = be32_to_cpu(e->event_type);
	u32 status = be32_to_cpu(e->status);

	if (event == BRCMF_E_ROAM && status == BRCMF_E_STATUS_SUCCESS) {
		if (test_bit(WL_STATUS_CONNECTED, &cfg_priv->status))
			brcmf_bss_roaming_done(cfg_priv, ndev, e);
		else
			brcmf_bss_connect_done(cfg_priv, ndev, e, true);
	}

	return err;
}

static s32
brcmf_notify_mic_status(struct brcmf_cfg80211_priv *cfg_priv,
			struct net_device *ndev,
			const struct brcmf_event_msg *e, void *data)
{
	u16 flags = be16_to_cpu(e->flags);
	enum nl80211_key_type key_type;

	if (flags & BRCMF_EVENT_MSG_GROUP)
		key_type = NL80211_KEYTYPE_GROUP;
	else
		key_type = NL80211_KEYTYPE_PAIRWISE;

	cfg80211_michael_mic_failure(ndev, (u8 *)&e->addr, key_type, -1,
				     NULL, GFP_KERNEL);

	return 0;
}

static s32
brcmf_notify_scan_status(struct brcmf_cfg80211_priv *cfg_priv,
			 struct net_device *ndev,
			 const struct brcmf_event_msg *e, void *data)
{
	struct brcmf_channel_info_le channel_inform_le;
	struct brcmf_scan_results_le *bss_list_le;
	u32 len = WL_SCAN_BUF_MAX;
	s32 err = 0;
	bool scan_abort = false;
	u32 scan_channel;

	WL_TRACE("Enter\n");

	if (cfg_priv->iscan_on && cfg_priv->iscan_kickstart) {
		WL_TRACE("Exit\n");
		return brcmf_wakeup_iscan(cfg_to_iscan(cfg_priv));
	}

	if (!test_and_clear_bit(WL_STATUS_SCANNING, &cfg_priv->status)) {
		WL_ERR("Scan complete while device not scanning\n");
		scan_abort = true;
		err = -EINVAL;
		goto scan_done_out;
	}

	err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_CHANNEL, &channel_inform_le,
			      sizeof(channel_inform_le));
	if (err) {
		WL_ERR("scan busy (%d)\n", err);
		scan_abort = true;
		goto scan_done_out;
	}
	scan_channel = le32_to_cpu(channel_inform_le.scan_channel);
	if (scan_channel)
		WL_CONN("channel_inform.scan_channel (%d)\n", scan_channel);
	cfg_priv->bss_list = cfg_priv->scan_results;
	bss_list_le = (struct brcmf_scan_results_le *) cfg_priv->bss_list;

	memset(cfg_priv->scan_results, 0, len);
	bss_list_le->buflen = cpu_to_le32(len);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SCAN_RESULTS,
			      cfg_priv->scan_results, len);
	if (err) {
		WL_ERR("%s Scan_results error (%d)\n", ndev->name, err);
		err = -EINVAL;
		scan_abort = true;
		goto scan_done_out;
	}
	cfg_priv->scan_results->buflen = le32_to_cpu(bss_list_le->buflen);
	cfg_priv->scan_results->version = le32_to_cpu(bss_list_le->version);
	cfg_priv->scan_results->count = le32_to_cpu(bss_list_le->count);

	err = brcmf_inform_bss(cfg_priv);
	if (err) {
		scan_abort = true;
		goto scan_done_out;
	}

scan_done_out:
	if (cfg_priv->scan_request) {
		WL_SCAN("calling cfg80211_scan_done\n");
		cfg80211_scan_done(cfg_priv->scan_request, scan_abort);
		brcmf_set_mpc(ndev, 1);
		cfg_priv->scan_request = NULL;
	}

	WL_TRACE("Exit\n");

	return err;
}

static void brcmf_init_conf(struct brcmf_cfg80211_conf *conf)
{
	conf->mode = (u32)-1;
	conf->frag_threshold = (u32)-1;
	conf->rts_threshold = (u32)-1;
	conf->retry_short = (u32)-1;
	conf->retry_long = (u32)-1;
	conf->tx_power = -1;
}

static void brcmf_init_eloop_handler(struct brcmf_cfg80211_event_loop *el)
{
	memset(el, 0, sizeof(*el));
	el->handler[BRCMF_E_SCAN_COMPLETE] = brcmf_notify_scan_status;
	el->handler[BRCMF_E_LINK] = brcmf_notify_connect_status;
	el->handler[BRCMF_E_ROAM] = brcmf_notify_roaming_status;
	el->handler[BRCMF_E_MIC_ERROR] = brcmf_notify_mic_status;
	el->handler[BRCMF_E_SET_SSID] = brcmf_notify_connect_status;
}

static void brcmf_deinit_priv_mem(struct brcmf_cfg80211_priv *cfg_priv)
{
	kfree(cfg_priv->scan_results);
	cfg_priv->scan_results = NULL;
	kfree(cfg_priv->bss_info);
	cfg_priv->bss_info = NULL;
	kfree(cfg_priv->conf);
	cfg_priv->conf = NULL;
	kfree(cfg_priv->profile);
	cfg_priv->profile = NULL;
	kfree(cfg_priv->scan_req_int);
	cfg_priv->scan_req_int = NULL;
	kfree(cfg_priv->dcmd_buf);
	cfg_priv->dcmd_buf = NULL;
	kfree(cfg_priv->extra_buf);
	cfg_priv->extra_buf = NULL;
	kfree(cfg_priv->iscan);
	cfg_priv->iscan = NULL;
	kfree(cfg_priv->pmk_list);
	cfg_priv->pmk_list = NULL;
}

static s32 brcmf_init_priv_mem(struct brcmf_cfg80211_priv *cfg_priv)
{
	cfg_priv->scan_results = kzalloc(WL_SCAN_BUF_MAX, GFP_KERNEL);
	if (!cfg_priv->scan_results)
		goto init_priv_mem_out;
	cfg_priv->conf = kzalloc(sizeof(*cfg_priv->conf), GFP_KERNEL);
	if (!cfg_priv->conf)
		goto init_priv_mem_out;
	cfg_priv->profile = kzalloc(sizeof(*cfg_priv->profile), GFP_KERNEL);
	if (!cfg_priv->profile)
		goto init_priv_mem_out;
	cfg_priv->bss_info = kzalloc(WL_BSS_INFO_MAX, GFP_KERNEL);
	if (!cfg_priv->bss_info)
		goto init_priv_mem_out;
	cfg_priv->scan_req_int = kzalloc(sizeof(*cfg_priv->scan_req_int),
					 GFP_KERNEL);
	if (!cfg_priv->scan_req_int)
		goto init_priv_mem_out;
	cfg_priv->dcmd_buf = kzalloc(WL_DCMD_LEN_MAX, GFP_KERNEL);
	if (!cfg_priv->dcmd_buf)
		goto init_priv_mem_out;
	cfg_priv->extra_buf = kzalloc(WL_EXTRA_BUF_MAX, GFP_KERNEL);
	if (!cfg_priv->extra_buf)
		goto init_priv_mem_out;
	cfg_priv->iscan = kzalloc(sizeof(*cfg_priv->iscan), GFP_KERNEL);
	if (!cfg_priv->iscan)
		goto init_priv_mem_out;
	cfg_priv->pmk_list = kzalloc(sizeof(*cfg_priv->pmk_list), GFP_KERNEL);
	if (!cfg_priv->pmk_list)
		goto init_priv_mem_out;

	return 0;

init_priv_mem_out:
	brcmf_deinit_priv_mem(cfg_priv);

	return -ENOMEM;
}

/*
* retrieve first queued event from head
*/

static struct brcmf_cfg80211_event_q *brcmf_deq_event(
	struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_event_q *e = NULL;

	spin_lock_irq(&cfg_priv->evt_q_lock);
	if (!list_empty(&cfg_priv->evt_q_list)) {
		e = list_first_entry(&cfg_priv->evt_q_list,
				     struct brcmf_cfg80211_event_q, evt_q_list);
		list_del(&e->evt_q_list);
	}
	spin_unlock_irq(&cfg_priv->evt_q_lock);

	return e;
}

/*
*	push event to tail of the queue
*
*	remark: this function may not sleep as it is called in atomic context.
*/

static s32
brcmf_enq_event(struct brcmf_cfg80211_priv *cfg_priv, u32 event,
		const struct brcmf_event_msg *msg)
{
	struct brcmf_cfg80211_event_q *e;
	s32 err = 0;
	ulong flags;

	e = kzalloc(sizeof(struct brcmf_cfg80211_event_q), GFP_ATOMIC);
	if (!e)
		return -ENOMEM;

	e->etype = event;
	memcpy(&e->emsg, msg, sizeof(struct brcmf_event_msg));

	spin_lock_irqsave(&cfg_priv->evt_q_lock, flags);
	list_add_tail(&e->evt_q_list, &cfg_priv->evt_q_list);
	spin_unlock_irqrestore(&cfg_priv->evt_q_lock, flags);

	return err;
}

static void brcmf_put_event(struct brcmf_cfg80211_event_q *e)
{
	kfree(e);
}

static void brcmf_cfg80211_event_handler(struct work_struct *work)
{
	struct brcmf_cfg80211_priv *cfg_priv =
			container_of(work, struct brcmf_cfg80211_priv,
				     event_work);
	struct brcmf_cfg80211_event_q *e;

	e = brcmf_deq_event(cfg_priv);
	if (unlikely(!e)) {
		WL_ERR("event queue empty...\n");
		return;
	}

	do {
		WL_INFO("event type (%d)\n", e->etype);
		if (cfg_priv->el.handler[e->etype])
			cfg_priv->el.handler[e->etype](cfg_priv,
						       cfg_to_ndev(cfg_priv),
						       &e->emsg, e->edata);
		else
			WL_INFO("Unknown Event (%d): ignoring\n", e->etype);
		brcmf_put_event(e);
	} while ((e = brcmf_deq_event(cfg_priv)));

}

static void brcmf_init_eq(struct brcmf_cfg80211_priv *cfg_priv)
{
	spin_lock_init(&cfg_priv->evt_q_lock);
	INIT_LIST_HEAD(&cfg_priv->evt_q_list);
}

static void brcmf_flush_eq(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct brcmf_cfg80211_event_q *e;

	spin_lock_irq(&cfg_priv->evt_q_lock);
	while (!list_empty(&cfg_priv->evt_q_list)) {
		e = list_first_entry(&cfg_priv->evt_q_list,
				     struct brcmf_cfg80211_event_q, evt_q_list);
		list_del(&e->evt_q_list);
		kfree(e);
	}
	spin_unlock_irq(&cfg_priv->evt_q_lock);
}

static s32 wl_init_priv(struct brcmf_cfg80211_priv *cfg_priv)
{
	s32 err = 0;

	cfg_priv->scan_request = NULL;
	cfg_priv->pwr_save = true;
	cfg_priv->iscan_on = true;	/* iscan on & off switch.
				 we enable iscan per default */
	cfg_priv->roam_on = true;	/* roam on & off switch.
				 we enable roam per default */

	cfg_priv->iscan_kickstart = false;
	cfg_priv->active_scan = true;	/* we do active scan for
				 specific scan per default */
	cfg_priv->dongle_up = false;	/* dongle is not up yet */
	brcmf_init_eq(cfg_priv);
	err = brcmf_init_priv_mem(cfg_priv);
	if (err)
		return err;
	INIT_WORK(&cfg_priv->event_work, brcmf_cfg80211_event_handler);
	brcmf_init_eloop_handler(&cfg_priv->el);
	mutex_init(&cfg_priv->usr_sync);
	err = brcmf_init_iscan(cfg_priv);
	if (err)
		return err;
	brcmf_init_conf(cfg_priv->conf);
	brcmf_init_prof(cfg_priv->profile);
	brcmf_link_down(cfg_priv);

	return err;
}

static void wl_deinit_priv(struct brcmf_cfg80211_priv *cfg_priv)
{
	cancel_work_sync(&cfg_priv->event_work);
	cfg_priv->dongle_up = false;	/* dongle down */
	brcmf_flush_eq(cfg_priv);
	brcmf_link_down(cfg_priv);
	brcmf_term_iscan(cfg_priv);
	brcmf_deinit_priv_mem(cfg_priv);
}

struct brcmf_cfg80211_dev *brcmf_cfg80211_attach(struct net_device *ndev,
						 struct device *busdev,
						 void *data)
{
	struct wireless_dev *wdev;
	struct brcmf_cfg80211_priv *cfg_priv;
	struct brcmf_cfg80211_iface *ci;
	struct brcmf_cfg80211_dev *cfg_dev;
	s32 err = 0;

	if (!ndev) {
		WL_ERR("ndev is invalid\n");
		return NULL;
	}
	cfg_dev = kzalloc(sizeof(struct brcmf_cfg80211_dev), GFP_KERNEL);
	if (!cfg_dev)
		return NULL;

	wdev = brcmf_alloc_wdev(sizeof(struct brcmf_cfg80211_iface), busdev);
	if (IS_ERR(wdev)) {
		kfree(cfg_dev);
		return NULL;
	}

	wdev->iftype = brcmf_mode_to_nl80211_iftype(WL_MODE_BSS);
	cfg_priv = wdev_to_cfg(wdev);
	cfg_priv->wdev = wdev;
	cfg_priv->pub = data;
	ci = (struct brcmf_cfg80211_iface *)&cfg_priv->ci;
	ci->cfg_priv = cfg_priv;
	ndev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(ndev, wiphy_dev(wdev->wiphy));
	wdev->netdev = ndev;
	err = wl_init_priv(cfg_priv);
	if (err) {
		WL_ERR("Failed to init iwm_priv (%d)\n", err);
		goto cfg80211_attach_out;
	}
	brcmf_set_drvdata(cfg_dev, ci);

	return cfg_dev;

cfg80211_attach_out:
	brcmf_free_wdev(cfg_priv);
	kfree(cfg_dev);
	return NULL;
}

void brcmf_cfg80211_detach(struct brcmf_cfg80211_dev *cfg_dev)
{
	struct brcmf_cfg80211_priv *cfg_priv;

	cfg_priv = brcmf_priv_get(cfg_dev);

	wl_deinit_priv(cfg_priv);
	brcmf_free_wdev(cfg_priv);
	brcmf_set_drvdata(cfg_dev, NULL);
	kfree(cfg_dev);
}

void
brcmf_cfg80211_event(struct net_device *ndev,
		  const struct brcmf_event_msg *e, void *data)
{
	u32 event_type = be32_to_cpu(e->event_type);
	struct brcmf_cfg80211_priv *cfg_priv = ndev_to_cfg(ndev);

	if (!brcmf_enq_event(cfg_priv, event_type, e))
		schedule_work(&cfg_priv->event_work);
}

static s32 brcmf_dongle_mode(struct net_device *ndev, s32 iftype)
{
	s32 infra = 0;
	s32 err = 0;

	switch (iftype) {
	case NL80211_IFTYPE_MONITOR:
	case NL80211_IFTYPE_WDS:
		WL_ERR("type (%d) : currently we do not support this mode\n",
		       iftype);
		err = -EINVAL;
		return err;
	case NL80211_IFTYPE_ADHOC:
		infra = 0;
		break;
	case NL80211_IFTYPE_STATION:
		infra = 1;
		break;
	default:
		err = -EINVAL;
		WL_ERR("invalid type (%d)\n", iftype);
		return err;
	}
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_INFRA, &infra);
	if (err) {
		WL_ERR("WLC_SET_INFRA error (%d)\n", err);
		return err;
	}

	return 0;
}

static s32 brcmf_dongle_eventmsg(struct net_device *ndev)
{
	/* Room for "event_msgs" + '\0' + bitvec */
	s8 iovbuf[BRCMF_EVENTING_MASK_LEN + 12];
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	s32 err = 0;

	WL_TRACE("Enter\n");

	/* Setup event_msgs */
	brcmf_c_mkiovar("event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN,
			iovbuf, sizeof(iovbuf));
	err = brcmf_exec_dcmd(ndev, BRCMF_C_GET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		WL_ERR("Get event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}
	memcpy(eventmask, iovbuf, BRCMF_EVENTING_MASK_LEN);

	setbit(eventmask, BRCMF_E_SET_SSID);
	setbit(eventmask, BRCMF_E_ROAM);
	setbit(eventmask, BRCMF_E_PRUNE);
	setbit(eventmask, BRCMF_E_AUTH);
	setbit(eventmask, BRCMF_E_REASSOC);
	setbit(eventmask, BRCMF_E_REASSOC_IND);
	setbit(eventmask, BRCMF_E_DEAUTH_IND);
	setbit(eventmask, BRCMF_E_DISASSOC_IND);
	setbit(eventmask, BRCMF_E_DISASSOC);
	setbit(eventmask, BRCMF_E_JOIN);
	setbit(eventmask, BRCMF_E_ASSOC_IND);
	setbit(eventmask, BRCMF_E_PSK_SUP);
	setbit(eventmask, BRCMF_E_LINK);
	setbit(eventmask, BRCMF_E_NDIS_LINK);
	setbit(eventmask, BRCMF_E_MIC_ERROR);
	setbit(eventmask, BRCMF_E_PMKID_CACHE);
	setbit(eventmask, BRCMF_E_TXFAIL);
	setbit(eventmask, BRCMF_E_JOIN_START);
	setbit(eventmask, BRCMF_E_SCAN_COMPLETE);

	brcmf_c_mkiovar("event_msgs", eventmask, BRCMF_EVENTING_MASK_LEN,
			iovbuf, sizeof(iovbuf));
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		WL_ERR("Set event_msgs error (%d)\n", err);
		goto dongle_eventmsg_out;
	}

dongle_eventmsg_out:
	WL_TRACE("Exit\n");
	return err;
}

static s32
brcmf_dongle_roam(struct net_device *ndev, u32 roamvar, u32 bcn_timeout)
{
	s8 iovbuf[32];
	s32 err = 0;
	__le32 roamtrigger[2];
	__le32 roam_delta[2];
	__le32 bcn_to_le;
	__le32 roamvar_le;

	/*
	 * Setup timeout if Beacons are lost and roam is
	 * off to report link down
	 */
	if (roamvar) {
		bcn_to_le = cpu_to_le32(bcn_timeout);
		brcmf_c_mkiovar("bcn_timeout", (char *)&bcn_to_le,
			sizeof(bcn_to_le), iovbuf, sizeof(iovbuf));
		err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR,
				   iovbuf, sizeof(iovbuf));
		if (err) {
			WL_ERR("bcn_timeout error (%d)\n", err);
			goto dongle_rom_out;
		}
	}

	/*
	 * Enable/Disable built-in roaming to allow supplicant
	 * to take care of roaming
	 */
	WL_INFO("Internal Roaming = %s\n", roamvar ? "Off" : "On");
	roamvar_le = cpu_to_le32(roamvar);
	brcmf_c_mkiovar("roam_off", (char *)&roamvar_le,
				sizeof(roamvar_le), iovbuf, sizeof(iovbuf));
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_VAR, iovbuf, sizeof(iovbuf));
	if (err) {
		WL_ERR("roam_off error (%d)\n", err);
		goto dongle_rom_out;
	}

	roamtrigger[0] = cpu_to_le32(WL_ROAM_TRIGGER_LEVEL);
	roamtrigger[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_ROAM_TRIGGER,
			(void *)roamtrigger, sizeof(roamtrigger));
	if (err) {
		WL_ERR("WLC_SET_ROAM_TRIGGER error (%d)\n", err);
		goto dongle_rom_out;
	}

	roam_delta[0] = cpu_to_le32(WL_ROAM_DELTA);
	roam_delta[1] = cpu_to_le32(BRCM_BAND_ALL);
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_ROAM_DELTA,
				(void *)roam_delta, sizeof(roam_delta));
	if (err) {
		WL_ERR("WLC_SET_ROAM_DELTA error (%d)\n", err);
		goto dongle_rom_out;
	}

dongle_rom_out:
	return err;
}

static s32
brcmf_dongle_scantime(struct net_device *ndev, s32 scan_assoc_time,
		      s32 scan_unassoc_time, s32 scan_passive_time)
{
	s32 err = 0;
	__le32 scan_assoc_tm_le = cpu_to_le32(scan_assoc_time);
	__le32 scan_unassoc_tm_le = cpu_to_le32(scan_unassoc_time);
	__le32 scan_passive_tm_le = cpu_to_le32(scan_passive_time);

	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_SCAN_CHANNEL_TIME,
			   &scan_assoc_tm_le, sizeof(scan_assoc_tm_le));
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan assoc time is not supported\n");
		else
			WL_ERR("Scan assoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}
	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_SCAN_UNASSOC_TIME,
			   &scan_unassoc_tm_le, sizeof(scan_unassoc_tm_le));
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan unassoc time is not supported\n");
		else
			WL_ERR("Scan unassoc time error (%d)\n", err);
		goto dongle_scantime_out;
	}

	err = brcmf_exec_dcmd(ndev, BRCMF_C_SET_SCAN_PASSIVE_TIME,
			   &scan_passive_tm_le, sizeof(scan_passive_tm_le));
	if (err) {
		if (err == -EOPNOTSUPP)
			WL_INFO("Scan passive time is not supported\n");
		else
			WL_ERR("Scan passive time error (%d)\n", err);
		goto dongle_scantime_out;
	}

dongle_scantime_out:
	return err;
}

static s32 wl_update_wiphybands(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct wiphy *wiphy;
	s32 phy_list;
	s8 phy;
	s32 err = 0;

	err = brcmf_exec_dcmd(cfg_to_ndev(cfg_priv), BRCM_GET_PHYLIST,
			      &phy_list, sizeof(phy_list));
	if (err) {
		WL_ERR("error (%d)\n", err);
		return err;
	}

	phy = ((char *)&phy_list)[1];
	WL_INFO("%c phy\n", phy);
	if (phy == 'n' || phy == 'a') {
		wiphy = cfg_to_wiphy(cfg_priv);
		wiphy->bands[IEEE80211_BAND_5GHZ] = &__wl_band_5ghz_n;
	}

	return err;
}

static s32 brcmf_dongle_probecap(struct brcmf_cfg80211_priv *cfg_priv)
{
	return wl_update_wiphybands(cfg_priv);
}

static s32 brcmf_config_dongle(struct brcmf_cfg80211_priv *cfg_priv)
{
	struct net_device *ndev;
	struct wireless_dev *wdev;
	s32 power_mode;
	s32 err = 0;

	if (cfg_priv->dongle_up)
		return err;

	ndev = cfg_to_ndev(cfg_priv);
	wdev = ndev->ieee80211_ptr;

	brcmf_dongle_scantime(ndev, WL_SCAN_CHANNEL_TIME,
			WL_SCAN_UNASSOC_TIME, WL_SCAN_PASSIVE_TIME);

	err = brcmf_dongle_eventmsg(ndev);
	if (err)
		goto default_conf_out;

	power_mode = cfg_priv->pwr_save ? PM_FAST : PM_OFF;
	err = brcmf_exec_dcmd_u32(ndev, BRCMF_C_SET_PM, &power_mode);
	if (err)
		goto default_conf_out;
	WL_INFO("power save set to %s\n",
		(power_mode ? "enabled" : "disabled"));

	err = brcmf_dongle_roam(ndev, (cfg_priv->roam_on ? 0 : 1),
				WL_BEACON_TIMEOUT);
	if (err)
		goto default_conf_out;
	err = brcmf_dongle_mode(ndev, wdev->iftype);
	if (err && err != -EINPROGRESS)
		goto default_conf_out;
	err = brcmf_dongle_probecap(cfg_priv);
	if (err)
		goto default_conf_out;

	/* -EINPROGRESS: Call commit handler */

default_conf_out:

	cfg_priv->dongle_up = true;

	return err;

}

static int brcmf_debugfs_add_netdev_params(struct brcmf_cfg80211_priv *cfg_priv)
{
	char buf[10+IFNAMSIZ];
	struct dentry *fd;
	s32 err = 0;

	sprintf(buf, "netdev:%s", cfg_to_ndev(cfg_priv)->name);
	cfg_priv->debugfsdir = debugfs_create_dir(buf,
					cfg_to_wiphy(cfg_priv)->debugfsdir);

	fd = debugfs_create_u16("beacon_int", S_IRUGO, cfg_priv->debugfsdir,
		(u16 *)&cfg_priv->profile->beacon_interval);
	if (!fd) {
		err = -ENOMEM;
		goto err_out;
	}

	fd = debugfs_create_u8("dtim_period", S_IRUGO, cfg_priv->debugfsdir,
		(u8 *)&cfg_priv->profile->dtim_period);
	if (!fd) {
		err = -ENOMEM;
		goto err_out;
	}

err_out:
	return err;
}

static void brcmf_debugfs_remove_netdev(struct brcmf_cfg80211_priv *cfg_priv)
{
	debugfs_remove_recursive(cfg_priv->debugfsdir);
	cfg_priv->debugfsdir = NULL;
}

static s32 __brcmf_cfg80211_up(struct brcmf_cfg80211_priv *cfg_priv)
{
	s32 err = 0;

	set_bit(WL_STATUS_READY, &cfg_priv->status);

	brcmf_debugfs_add_netdev_params(cfg_priv);

	err = brcmf_config_dongle(cfg_priv);
	if (err)
		return err;

	brcmf_invoke_iscan(cfg_priv);

	return err;
}

static s32 __brcmf_cfg80211_down(struct brcmf_cfg80211_priv *cfg_priv)
{
	/*
	 * While going down, if associated with AP disassociate
	 * from AP to save power
	 */
	if ((test_bit(WL_STATUS_CONNECTED, &cfg_priv->status) ||
	     test_bit(WL_STATUS_CONNECTING, &cfg_priv->status)) &&
	     test_bit(WL_STATUS_READY, &cfg_priv->status)) {
		WL_INFO("Disassociating from AP");
		brcmf_link_down(cfg_priv);

		/* Make sure WPA_Supplicant receives all the event
		   generated due to DISASSOC call to the fw to keep
		   the state fw and WPA_Supplicant state consistent
		 */
		brcmf_delay(500);
	}

	set_bit(WL_STATUS_SCAN_ABORTING, &cfg_priv->status);
	brcmf_term_iscan(cfg_priv);
	if (cfg_priv->scan_request) {
		cfg80211_scan_done(cfg_priv->scan_request, true);
		/* May need to perform this to cover rmmod */
		/* wl_set_mpc(cfg_to_ndev(wl), 1); */
		cfg_priv->scan_request = NULL;
	}
	clear_bit(WL_STATUS_READY, &cfg_priv->status);
	clear_bit(WL_STATUS_SCANNING, &cfg_priv->status);
	clear_bit(WL_STATUS_SCAN_ABORTING, &cfg_priv->status);

	brcmf_debugfs_remove_netdev(cfg_priv);

	return 0;
}

s32 brcmf_cfg80211_up(struct brcmf_cfg80211_dev *cfg_dev)
{
	struct brcmf_cfg80211_priv *cfg_priv;
	s32 err = 0;

	cfg_priv = brcmf_priv_get(cfg_dev);
	mutex_lock(&cfg_priv->usr_sync);
	err = __brcmf_cfg80211_up(cfg_priv);
	mutex_unlock(&cfg_priv->usr_sync);

	return err;
}

s32 brcmf_cfg80211_down(struct brcmf_cfg80211_dev *cfg_dev)
{
	struct brcmf_cfg80211_priv *cfg_priv;
	s32 err = 0;

	cfg_priv = brcmf_priv_get(cfg_dev);
	mutex_lock(&cfg_priv->usr_sync);
	err = __brcmf_cfg80211_down(cfg_priv);
	mutex_unlock(&cfg_priv->usr_sync);

	return err;
}

static __used s32 brcmf_add_ie(struct brcmf_cfg80211_priv *cfg_priv,
			       u8 t, u8 l, u8 *v)
{
	struct brcmf_cfg80211_ie *ie = &cfg_priv->ie;
	s32 err = 0;

	if (ie->offset + l + 2 > WL_TLV_INFO_MAX) {
		WL_ERR("ei crosses buffer boundary\n");
		return -ENOSPC;
	}
	ie->buf[ie->offset] = t;
	ie->buf[ie->offset + 1] = l;
	memcpy(&ie->buf[ie->offset + 2], v, l);
	ie->offset += l + 2;

	return err;
}
