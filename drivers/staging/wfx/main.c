// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device probe and register.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 * Copyright (c) 2008, Johannes Berg <johannes@sipsolutions.net>
 * Copyright (c) 2008 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (c) 2007-2009, Christian Lamparter <chunkeey@web.de>
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 * Copyright (c) 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/mmc/sdio_func.h>
#include <linux/spi/spi.h>
#include <linux/etherdevice.h>
#include <linux/firmware.h>

#include "main.h"
#include "wfx.h"
#include "fwio.h"
#include "hwio.h"
#include "bus.h"
#include "bh.h"
#include "sta.h"
#include "key.h"
#include "debug.h"
#include "data_tx.h"
#include "secure_link.h"
#include "hif_tx_mib.h"
#include "hif_api_cmd.h"

#define WFX_PDS_MAX_SIZE 1500

MODULE_DESCRIPTION("Silicon Labs 802.11 Wireless LAN driver for WFx");
MODULE_AUTHOR("Jérôme Pouiller <jerome.pouiller@silabs.com>");
MODULE_LICENSE("GPL");

static int gpio_wakeup = -2;
module_param(gpio_wakeup, int, 0644);
MODULE_PARM_DESC(gpio_wakeup, "gpio number for wakeup. -1 for none.");

#define RATETAB_ENT(_rate, _rateid, _flags) { \
	.bitrate  = (_rate),   \
	.hw_value = (_rateid), \
	.flags    = (_flags),  \
}

static struct ieee80211_rate wfx_rates[] = {
	RATETAB_ENT(10,  0,  0),
	RATETAB_ENT(20,  1,  IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(55,  2,  IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(110, 3,  IEEE80211_RATE_SHORT_PREAMBLE),
	RATETAB_ENT(60,  6,  0),
	RATETAB_ENT(90,  7,  0),
	RATETAB_ENT(120, 8,  0),
	RATETAB_ENT(180, 9,  0),
	RATETAB_ENT(240, 10, 0),
	RATETAB_ENT(360, 11, 0),
	RATETAB_ENT(480, 12, 0),
	RATETAB_ENT(540, 13, 0),
};

#define CHAN2G(_channel, _freq, _flags) { \
	.band = NL80211_BAND_2GHZ, \
	.center_freq = (_freq),    \
	.hw_value = (_channel),    \
	.flags = (_flags),         \
	.max_antenna_gain = 0,     \
	.max_power = 30,           \
}

static struct ieee80211_channel wfx_2ghz_chantable[] = {
	CHAN2G(1,  2412, 0),
	CHAN2G(2,  2417, 0),
	CHAN2G(3,  2422, 0),
	CHAN2G(4,  2427, 0),
	CHAN2G(5,  2432, 0),
	CHAN2G(6,  2437, 0),
	CHAN2G(7,  2442, 0),
	CHAN2G(8,  2447, 0),
	CHAN2G(9,  2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
};

static const struct ieee80211_supported_band wfx_band_2ghz = {
	.channels = wfx_2ghz_chantable,
	.n_channels = ARRAY_SIZE(wfx_2ghz_chantable),
	.bitrates = wfx_rates,
	.n_bitrates = ARRAY_SIZE(wfx_rates),
	.ht_cap = {
		// Receive caps
		.cap = IEEE80211_HT_CAP_GRN_FLD | IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_MAX_AMSDU |
		       (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT),
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask = { 0xFF }, // MCS0 to MCS7
			.rx_highest = 65,
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,
		},
	},
};

static const struct ieee80211_iface_limit wdev_iface_limits[] = {
	{ .max = 1, .types = BIT(NL80211_IFTYPE_STATION) },
	{ .max = 1, .types = BIT(NL80211_IFTYPE_AP) },
};

static const struct ieee80211_iface_combination wfx_iface_combinations[] = {
	{
		.num_different_channels = 2,
		.max_interfaces = 2,
		.limits = wdev_iface_limits,
		.n_limits = ARRAY_SIZE(wdev_iface_limits),
	}
};

static const struct ieee80211_ops wfx_ops = {
	.start			= wfx_start,
	.stop			= wfx_stop,
	.add_interface		= wfx_add_interface,
	.remove_interface	= wfx_remove_interface,
	.config                 = wfx_config,
	.tx			= wfx_tx,
	.join_ibss		= wfx_join_ibss,
	.leave_ibss		= wfx_leave_ibss,
	.conf_tx		= wfx_conf_tx,
	.hw_scan		= wfx_hw_scan,
	.cancel_hw_scan		= wfx_cancel_hw_scan,
	.start_ap		= wfx_start_ap,
	.stop_ap		= wfx_stop_ap,
	.sta_add		= wfx_sta_add,
	.sta_remove		= wfx_sta_remove,
	.set_tim		= wfx_set_tim,
	.set_key		= wfx_set_key,
	.set_rts_threshold	= wfx_set_rts_threshold,
	.bss_info_changed	= wfx_bss_info_changed,
	.prepare_multicast	= wfx_prepare_multicast,
	.configure_filter	= wfx_configure_filter,
	.ampdu_action		= wfx_ampdu_action,
	.flush			= wfx_flush,
	.add_chanctx		= wfx_add_chanctx,
	.remove_chanctx		= wfx_remove_chanctx,
	.change_chanctx		= wfx_change_chanctx,
	.assign_vif_chanctx	= wfx_assign_vif_chanctx,
	.unassign_vif_chanctx	= wfx_unassign_vif_chanctx,
};

bool wfx_api_older_than(struct wfx_dev *wdev, int major, int minor)
{
	if (wdev->hw_caps.api_version_major < major)
		return true;
	if (wdev->hw_caps.api_version_major > major)
		return false;
	if (wdev->hw_caps.api_version_minor < minor)
		return true;
	return false;
}

struct gpio_desc *wfx_get_gpio(struct device *dev, int override,
			       const char *label)
{
	struct gpio_desc *ret;
	char label_buf[256];

	if (override >= 0) {
		snprintf(label_buf, sizeof(label_buf), "wfx_%s", label);
		ret = ERR_PTR(devm_gpio_request_one(dev, override,
						    GPIOF_OUT_INIT_LOW,
						    label_buf));
		if (!ret)
			ret = gpio_to_desc(override);
	} else if (override == -1) {
		ret = NULL;
	} else {
		ret = devm_gpiod_get(dev, label, GPIOD_OUT_LOW);
	}
	if (IS_ERR_OR_NULL(ret)) {
		if (!ret || PTR_ERR(ret) == -ENOENT)
			dev_warn(dev, "gpio %s is not defined\n", label);
		else
			dev_warn(dev,
				 "error while requesting gpio %s\n", label);
		ret = NULL;
	} else {
		dev_dbg(dev,
			"using gpio %d for %s\n", desc_to_gpio(ret), label);
	}
	return ret;
}

/* NOTE: wfx_send_pds() destroy buf */
int wfx_send_pds(struct wfx_dev *wdev, unsigned char *buf, size_t len)
{
	int ret;
	int start, brace_level, i;

	start = 0;
	brace_level = 0;
	if (buf[0] != '{') {
		dev_err(wdev->dev, "valid PDS start with '{'. Did you forget to compress it?\n");
		return -EINVAL;
	}
	for (i = 1; i < len - 1; i++) {
		if (buf[i] == '{')
			brace_level++;
		if (buf[i] == '}')
			brace_level--;
		if (buf[i] == '}' && !brace_level) {
			i++;
			if (i - start + 1 > WFX_PDS_MAX_SIZE)
				return -EFBIG;
			buf[start] = '{';
			buf[i] = 0;
			dev_dbg(wdev->dev, "send PDS '%s}'\n", buf + start);
			buf[i] = '}';
			ret = hif_configuration(wdev, buf + start,
						i - start + 1);
			if (ret == HIF_STATUS_FAILURE) {
				dev_err(wdev->dev, "PDS bytes %d to %d: invalid data (unsupported options?)\n", start, i);
				return -EINVAL;
			}
			if (ret == -ETIMEDOUT) {
				dev_err(wdev->dev, "PDS bytes %d to %d: chip didn't reply (corrupted file?)\n", start, i);
				return ret;
			}
			if (ret) {
				dev_err(wdev->dev, "PDS bytes %d to %d: chip returned an unknown error\n", start, i);
				return -EIO;
			}
			buf[i] = ',';
			start = i;
		}
	}
	return 0;
}

static int wfx_send_pdata_pds(struct wfx_dev *wdev)
{
	int ret = 0;
	const struct firmware *pds;
	unsigned char *tmp_buf;

	ret = request_firmware(&pds, wdev->pdata.file_pds, wdev->dev);
	if (ret) {
		dev_err(wdev->dev, "can't load PDS file %s\n",
			wdev->pdata.file_pds);
		return ret;
	}
	tmp_buf = kmemdup(pds->data, pds->size, GFP_KERNEL);
	ret = wfx_send_pds(wdev, tmp_buf, pds->size);
	kfree(tmp_buf);
	release_firmware(pds);
	return ret;
}

static void wfx_free_common(void *data)
{
	struct wfx_dev *wdev = data;

	mutex_destroy(&wdev->rx_stats_lock);
	mutex_destroy(&wdev->conf_mutex);
	ieee80211_free_hw(wdev->hw);
}

struct wfx_dev *wfx_init_common(struct device *dev,
				const struct wfx_platform_data *pdata,
				const struct hwbus_ops *hwbus_ops,
				void *hwbus_priv)
{
	struct ieee80211_hw *hw;
	struct wfx_dev *wdev;

	hw = ieee80211_alloc_hw(sizeof(struct wfx_dev), &wfx_ops);
	if (!hw)
		return NULL;

	SET_IEEE80211_DEV(hw, dev);

	ieee80211_hw_set(hw, NEED_DTIM_BEFORE_ASSOC);
	ieee80211_hw_set(hw, TX_AMPDU_SETUP_IN_HW);
	ieee80211_hw_set(hw, AMPDU_AGGREGATION);
	ieee80211_hw_set(hw, CONNECTION_MONITOR);
	ieee80211_hw_set(hw, REPORTS_TX_ACK_STATUS);
	ieee80211_hw_set(hw, SUPPORTS_DYNAMIC_PS);
	ieee80211_hw_set(hw, SIGNAL_DBM);
	ieee80211_hw_set(hw, SUPPORTS_PS);
	ieee80211_hw_set(hw, MFP_CAPABLE);

	hw->vif_data_size = sizeof(struct wfx_vif);
	hw->sta_data_size = sizeof(struct wfx_sta_priv);
	hw->queues = 4;
	hw->max_rates = 8;
	hw->max_rate_tries = 8;
	hw->extra_tx_headroom = sizeof(struct hif_sl_msg_hdr) +
				sizeof(struct hif_msg)
				+ sizeof(struct hif_req_tx)
				+ 4 /* alignment */ + 8 /* TKIP IV */;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_AP);
	hw->wiphy->probe_resp_offload = NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_80211U;
	hw->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->flags &= ~WIPHY_FLAG_PS_ON_BY_DEFAULT;
	hw->wiphy->max_ap_assoc_sta = HIF_LINK_ID_MAX;
	hw->wiphy->max_scan_ssids = 2;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	hw->wiphy->n_iface_combinations = ARRAY_SIZE(wfx_iface_combinations);
	hw->wiphy->iface_combinations = wfx_iface_combinations;
	hw->wiphy->bands[NL80211_BAND_2GHZ] = devm_kmalloc(dev, sizeof(wfx_band_2ghz), GFP_KERNEL);
	// FIXME: also copy wfx_rates and wfx_2ghz_chantable
	memcpy(hw->wiphy->bands[NL80211_BAND_2GHZ], &wfx_band_2ghz,
	       sizeof(wfx_band_2ghz));

	wdev = hw->priv;
	wdev->hw = hw;
	wdev->dev = dev;
	wdev->hwbus_ops = hwbus_ops;
	wdev->hwbus_priv = hwbus_priv;
	memcpy(&wdev->pdata, pdata, sizeof(*pdata));
	of_property_read_string(dev->of_node, "config-file",
				&wdev->pdata.file_pds);
	wdev->pdata.gpio_wakeup = wfx_get_gpio(dev, gpio_wakeup, "wakeup");
	wfx_sl_fill_pdata(dev, &wdev->pdata);

	mutex_init(&wdev->conf_mutex);
	mutex_init(&wdev->rx_stats_lock);
	init_completion(&wdev->firmware_ready);
	wfx_init_hif_cmd(&wdev->hif_cmd);
	wfx_tx_queues_init(wdev);

	if (devm_add_action_or_reset(dev, wfx_free_common, wdev))
		return NULL;

	return wdev;
}

int wfx_probe(struct wfx_dev *wdev)
{
	int i;
	int err;
	const void *macaddr;
	struct gpio_desc *gpio_saved;

	// During first part of boot, gpio_wakeup cannot yet been used. So
	// prevent bh() to touch it.
	gpio_saved = wdev->pdata.gpio_wakeup;
	wdev->pdata.gpio_wakeup = NULL;

	wfx_bh_register(wdev);

	err = wfx_init_device(wdev);
	if (err)
		goto err1;

	err = wait_for_completion_interruptible_timeout(&wdev->firmware_ready,
							10 * HZ);
	if (err <= 0) {
		if (err == 0) {
			dev_err(wdev->dev, "timeout while waiting for startup indication. IRQ configuration error?\n");
			err = -ETIMEDOUT;
		} else if (err == -ERESTARTSYS) {
			dev_info(wdev->dev, "probe interrupted by user\n");
		}
		goto err1;
	}

	// FIXME: fill wiphy::hw_version
	dev_info(wdev->dev, "started firmware %d.%d.%d \"%s\" (API: %d.%d, keyset: %02X, caps: 0x%.8X)\n",
		 wdev->hw_caps.firmware_major, wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build, wdev->hw_caps.firmware_label,
		 wdev->hw_caps.api_version_major,
		 wdev->hw_caps.api_version_minor,
		 wdev->keyset, *((u32 *) &wdev->hw_caps.capabilities));
	snprintf(wdev->hw->wiphy->fw_version,
		 sizeof(wdev->hw->wiphy->fw_version),
		 "%d.%d.%d",
		 wdev->hw_caps.firmware_major,
		 wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build);

	if (wfx_api_older_than(wdev, 1, 0)) {
		dev_err(wdev->dev,
			"unsupported firmware API version (expect 1 while firmware returns %d)\n",
			wdev->hw_caps.api_version_major);
		err = -ENOTSUPP;
		goto err1;
	}

	err = wfx_sl_init(wdev);
	if (err && wdev->hw_caps.capabilities.link_mode == SEC_LINK_ENFORCED) {
		dev_err(wdev->dev,
			"chip require secure_link, but can't negociate it\n");
		goto err1;
	}

	if (wdev->hw_caps.regul_sel_mode_info.region_sel_mode) {
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[11].flags |= IEEE80211_CHAN_NO_IR;
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[12].flags |= IEEE80211_CHAN_NO_IR;
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[13].flags |= IEEE80211_CHAN_DISABLED;
	}

	dev_dbg(wdev->dev, "sending configuration file %s\n",
		wdev->pdata.file_pds);
	err = wfx_send_pdata_pds(wdev);
	if (err < 0)
		goto err1;

	wdev->pdata.gpio_wakeup = gpio_saved;
	if (wdev->pdata.gpio_wakeup) {
		dev_dbg(wdev->dev,
			"enable 'quiescent' power mode with gpio %d and PDS file %s\n",
			desc_to_gpio(wdev->pdata.gpio_wakeup),
			wdev->pdata.file_pds);
		gpiod_set_value_cansleep(wdev->pdata.gpio_wakeup, 1);
		control_reg_write(wdev, 0);
		hif_set_operational_mode(wdev, HIF_OP_POWER_MODE_QUIESCENT);
	} else {
		hif_set_operational_mode(wdev, HIF_OP_POWER_MODE_DOZE);
	}

	hif_use_multi_tx_conf(wdev, true);

	for (i = 0; i < ARRAY_SIZE(wdev->addresses); i++) {
		eth_zero_addr(wdev->addresses[i].addr);
		macaddr = of_get_mac_address(wdev->dev->of_node);
		if (!IS_ERR_OR_NULL(macaddr)) {
			ether_addr_copy(wdev->addresses[i].addr, macaddr);
			wdev->addresses[i].addr[ETH_ALEN - 1] += i;
		} else {
			ether_addr_copy(wdev->addresses[i].addr,
					wdev->hw_caps.mac_addr[i]);
		}
		if (!is_valid_ether_addr(wdev->addresses[i].addr)) {
			dev_warn(wdev->dev, "using random MAC address\n");
			eth_random_addr(wdev->addresses[i].addr);
		}
		dev_info(wdev->dev, "MAC address %d: %pM\n", i,
			 wdev->addresses[i].addr);
	}
	wdev->hw->wiphy->n_addresses = ARRAY_SIZE(wdev->addresses);
	wdev->hw->wiphy->addresses = wdev->addresses;

	err = ieee80211_register_hw(wdev->hw);
	if (err)
		goto err1;

	err = wfx_debug_init(wdev);
	if (err)
		goto err2;

	return 0;

err2:
	ieee80211_unregister_hw(wdev->hw);
	ieee80211_free_hw(wdev->hw);
err1:
	wfx_bh_unregister(wdev);
	return err;
}

void wfx_release(struct wfx_dev *wdev)
{
	ieee80211_unregister_hw(wdev->hw);
	hif_shutdown(wdev);
	wfx_bh_unregister(wdev);
	wfx_sl_deinit(wdev);
}

static int __init wfx_core_init(void)
{
	int ret = 0;

	if (IS_ENABLED(CONFIG_SPI))
		ret = spi_register_driver(&wfx_spi_driver);
	if (IS_ENABLED(CONFIG_MMC) && !ret)
		ret = sdio_register_driver(&wfx_sdio_driver);
	return ret;
}
module_init(wfx_core_init);

static void __exit wfx_core_exit(void)
{
	if (IS_ENABLED(CONFIG_MMC))
		sdio_unregister_driver(&wfx_sdio_driver);
	if (IS_ENABLED(CONFIG_SPI))
		spi_unregister_driver(&wfx_spi_driver);
}
module_exit(wfx_core_exit);
