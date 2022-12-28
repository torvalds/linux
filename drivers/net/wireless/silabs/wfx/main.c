// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device probe and register.
 *
 * Copyright (c) 2017-2020, Silicon Laboratories, Inc.
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
#include "scan.h"
#include "debug.h"
#include "data_tx.h"
#include "hif_tx_mib.h"
#include "hif_api_cmd.h"

#define WFX_PDS_TLV_TYPE 0x4450 // "PD" (Platform Data) in ascii little-endian
#define WFX_PDS_MAX_CHUNK_SIZE 1500

MODULE_DESCRIPTION("Silicon Labs 802.11 Wireless LAN driver for WF200");
MODULE_AUTHOR("Jérôme Pouiller <jerome.pouiller@silabs.com>");
MODULE_LICENSE("GPL");

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
		/* Receive caps */
		.cap = IEEE80211_HT_CAP_GRN_FLD | IEEE80211_HT_CAP_SGI_20 |
		       IEEE80211_HT_CAP_MAX_AMSDU | (1 << IEEE80211_HT_CAP_RX_STBC_SHIFT),
		.ht_supported = 1,
		.ampdu_factor = IEEE80211_HT_MAX_AMPDU_16K,
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_NONE,
		.mcs = {
			.rx_mask = { 0xFF }, /* MCS0 to MCS7 */
			.rx_highest = cpu_to_le16(72),
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
	.start                   = wfx_start,
	.stop                    = wfx_stop,
	.add_interface           = wfx_add_interface,
	.remove_interface        = wfx_remove_interface,
	.config                  = wfx_config,
	.tx                      = wfx_tx,
	.wake_tx_queue           = ieee80211_handle_wake_tx_queue,
	.join_ibss               = wfx_join_ibss,
	.leave_ibss              = wfx_leave_ibss,
	.conf_tx                 = wfx_conf_tx,
	.hw_scan                 = wfx_hw_scan,
	.cancel_hw_scan          = wfx_cancel_hw_scan,
	.start_ap                = wfx_start_ap,
	.stop_ap                 = wfx_stop_ap,
	.sta_add                 = wfx_sta_add,
	.sta_remove              = wfx_sta_remove,
	.set_tim                 = wfx_set_tim,
	.set_key                 = wfx_set_key,
	.set_rts_threshold       = wfx_set_rts_threshold,
	.set_default_unicast_key = wfx_set_default_unicast_key,
	.bss_info_changed        = wfx_bss_info_changed,
	.configure_filter        = wfx_configure_filter,
	.ampdu_action            = wfx_ampdu_action,
	.flush                   = wfx_flush,
	.add_chanctx             = wfx_add_chanctx,
	.remove_chanctx          = wfx_remove_chanctx,
	.change_chanctx          = wfx_change_chanctx,
	.assign_vif_chanctx      = wfx_assign_vif_chanctx,
	.unassign_vif_chanctx    = wfx_unassign_vif_chanctx,
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

/* The device needs data about the antenna configuration. This information in provided by PDS
 * (Platform Data Set, this is the wording used in WF200 documentation) files. For hardware
 * integrators, the full process to create PDS files is described here:
 *   https://github.com/SiliconLabs/wfx-firmware/blob/master/PDS/README.md
 *
 * The PDS file is an array of Time-Length-Value structs.
 */
int wfx_send_pds(struct wfx_dev *wdev, u8 *buf, size_t len)
{
	int ret, chunk_type, chunk_len, chunk_num = 0;

	if (*buf == '{') {
		dev_err(wdev->dev, "PDS: malformed file (legacy format?)\n");
		return -EINVAL;
	}
	while (len > 0) {
		chunk_type = get_unaligned_le16(buf + 0);
		chunk_len = get_unaligned_le16(buf + 2);
		if (chunk_len < 4 || chunk_len > len) {
			dev_err(wdev->dev, "PDS:%d: corrupted file\n", chunk_num);
			return -EINVAL;
		}
		if (chunk_type != WFX_PDS_TLV_TYPE) {
			dev_info(wdev->dev, "PDS:%d: skip unknown data\n", chunk_num);
			goto next;
		}
		if (chunk_len > WFX_PDS_MAX_CHUNK_SIZE)
			dev_warn(wdev->dev, "PDS:%d: unexpectedly large chunk\n", chunk_num);
		if (buf[4] != '{' || buf[chunk_len - 1] != '}')
			dev_warn(wdev->dev, "PDS:%d: unexpected content\n", chunk_num);

		ret = wfx_hif_configuration(wdev, buf + 4, chunk_len - 4);
		if (ret > 0) {
			dev_err(wdev->dev, "PDS:%d: invalid data (unsupported options?)\n", chunk_num);
			return -EINVAL;
		}
		if (ret == -ETIMEDOUT) {
			dev_err(wdev->dev, "PDS:%d: chip didn't reply (corrupted file?)\n", chunk_num);
			return ret;
		}
		if (ret) {
			dev_err(wdev->dev, "PDS:%d: chip returned an unknown error\n", chunk_num);
			return -EIO;
		}
next:
		chunk_num++;
		len -= chunk_len;
		buf += chunk_len;
	}
	return 0;
}

static int wfx_send_pdata_pds(struct wfx_dev *wdev)
{
	int ret = 0;
	const struct firmware *pds;
	u8 *tmp_buf;

	ret = request_firmware(&pds, wdev->pdata.file_pds, wdev->dev);
	if (ret) {
		dev_err(wdev->dev, "can't load antenna parameters (PDS file %s). The device may be unstable.\n",
			wdev->pdata.file_pds);
		return ret;
	}
	tmp_buf = kmemdup(pds->data, pds->size, GFP_KERNEL);
	if (!tmp_buf) {
		ret = -ENOMEM;
		goto release_fw;
	}
	ret = wfx_send_pds(wdev, tmp_buf, pds->size);
	kfree(tmp_buf);
release_fw:
	release_firmware(pds);
	return ret;
}

static void wfx_free_common(void *data)
{
	struct wfx_dev *wdev = data;

	mutex_destroy(&wdev->tx_power_loop_info_lock);
	mutex_destroy(&wdev->rx_stats_lock);
	mutex_destroy(&wdev->conf_mutex);
	ieee80211_free_hw(wdev->hw);
}

struct wfx_dev *wfx_init_common(struct device *dev, const struct wfx_platform_data *pdata,
				const struct wfx_hwbus_ops *hwbus_ops, void *hwbus_priv)
{
	struct ieee80211_hw *hw;
	struct wfx_dev *wdev;

	hw = ieee80211_alloc_hw(sizeof(struct wfx_dev), &wfx_ops);
	if (!hw)
		return NULL;

	SET_IEEE80211_DEV(hw, dev);

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
	hw->extra_tx_headroom = sizeof(struct wfx_hif_msg) + sizeof(struct wfx_hif_req_tx) +
				4 /* alignment */ + 8 /* TKIP IV */;
	hw->wiphy->interface_modes = BIT(NL80211_IFTYPE_STATION) |
				     BIT(NL80211_IFTYPE_ADHOC) |
				     BIT(NL80211_IFTYPE_AP);
	hw->wiphy->probe_resp_offload = NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_WPS2 |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_P2P |
					NL80211_PROBE_RESP_OFFLOAD_SUPPORT_80211U;
	hw->wiphy->features |= NL80211_FEATURE_AP_SCAN;
	hw->wiphy->flags |= WIPHY_FLAG_AP_PROBE_RESP_OFFLOAD;
	hw->wiphy->flags |= WIPHY_FLAG_AP_UAPSD;
	hw->wiphy->max_ap_assoc_sta = HIF_LINK_ID_MAX;
	hw->wiphy->max_scan_ssids = 2;
	hw->wiphy->max_scan_ie_len = IEEE80211_MAX_DATA_LEN;
	hw->wiphy->n_iface_combinations = ARRAY_SIZE(wfx_iface_combinations);
	hw->wiphy->iface_combinations = wfx_iface_combinations;
	hw->wiphy->bands[NL80211_BAND_2GHZ] = devm_kmalloc(dev, sizeof(wfx_band_2ghz), GFP_KERNEL);
	if (!hw->wiphy->bands[NL80211_BAND_2GHZ])
		goto err;

	/* FIXME: also copy wfx_rates and wfx_2ghz_chantable */
	memcpy(hw->wiphy->bands[NL80211_BAND_2GHZ], &wfx_band_2ghz, sizeof(wfx_band_2ghz));

	wdev = hw->priv;
	wdev->hw = hw;
	wdev->dev = dev;
	wdev->hwbus_ops = hwbus_ops;
	wdev->hwbus_priv = hwbus_priv;
	memcpy(&wdev->pdata, pdata, sizeof(*pdata));
	of_property_read_string(dev->of_node, "silabs,antenna-config-file", &wdev->pdata.file_pds);
	wdev->pdata.gpio_wakeup = devm_gpiod_get_optional(dev, "wakeup", GPIOD_OUT_LOW);
	if (IS_ERR(wdev->pdata.gpio_wakeup))
		goto err;

	if (wdev->pdata.gpio_wakeup)
		gpiod_set_consumer_name(wdev->pdata.gpio_wakeup, "wfx wakeup");

	mutex_init(&wdev->conf_mutex);
	mutex_init(&wdev->rx_stats_lock);
	mutex_init(&wdev->tx_power_loop_info_lock);
	init_completion(&wdev->firmware_ready);
	INIT_DELAYED_WORK(&wdev->cooling_timeout_work, wfx_cooling_timeout_work);
	skb_queue_head_init(&wdev->tx_pending);
	init_waitqueue_head(&wdev->tx_dequeue);
	wfx_init_hif_cmd(&wdev->hif_cmd);

	if (devm_add_action_or_reset(dev, wfx_free_common, wdev))
		return NULL;

	return wdev;

err:
	ieee80211_free_hw(hw);
	return NULL;
}

int wfx_probe(struct wfx_dev *wdev)
{
	int i;
	int err;
	struct gpio_desc *gpio_saved;

	/* During first part of boot, gpio_wakeup cannot yet been used. So prevent bh() to touch
	 * it.
	 */
	gpio_saved = wdev->pdata.gpio_wakeup;
	wdev->pdata.gpio_wakeup = NULL;
	wdev->poll_irq = true;

	wdev->bh_wq = alloc_workqueue("wfx_bh_wq", WQ_HIGHPRI, 0);
	if (!wdev->bh_wq)
		return -ENOMEM;

	wfx_bh_register(wdev);

	err = wfx_init_device(wdev);
	if (err)
		goto bh_unregister;

	wfx_bh_poll_irq(wdev);
	err = wait_for_completion_timeout(&wdev->firmware_ready, 1 * HZ);
	if (err <= 0) {
		if (err == 0) {
			dev_err(wdev->dev, "timeout while waiting for startup indication\n");
			err = -ETIMEDOUT;
		} else if (err == -ERESTARTSYS) {
			dev_info(wdev->dev, "probe interrupted by user\n");
		}
		goto bh_unregister;
	}

	/* FIXME: fill wiphy::hw_version */
	dev_info(wdev->dev, "started firmware %d.%d.%d \"%s\" (API: %d.%d, keyset: %02X, caps: 0x%.8X)\n",
		 wdev->hw_caps.firmware_major, wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build, wdev->hw_caps.firmware_label,
		 wdev->hw_caps.api_version_major, wdev->hw_caps.api_version_minor,
		 wdev->keyset, wdev->hw_caps.link_mode);
	snprintf(wdev->hw->wiphy->fw_version,
		 sizeof(wdev->hw->wiphy->fw_version),
		 "%d.%d.%d",
		 wdev->hw_caps.firmware_major,
		 wdev->hw_caps.firmware_minor,
		 wdev->hw_caps.firmware_build);

	if (wfx_api_older_than(wdev, 1, 0)) {
		dev_err(wdev->dev, "unsupported firmware API version (expect 1 while firmware returns %d)\n",
			wdev->hw_caps.api_version_major);
		err = -EOPNOTSUPP;
		goto bh_unregister;
	}

	if (wdev->hw_caps.link_mode == SEC_LINK_ENFORCED) {
		dev_err(wdev->dev, "chip require secure_link, but can't negotiate it\n");
		goto bh_unregister;
	}

	if (wdev->hw_caps.region_sel_mode) {
		wdev->hw->wiphy->regulatory_flags |= REGULATORY_DISABLE_BEACON_HINTS;
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[11].flags |=
			IEEE80211_CHAN_NO_IR;
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[12].flags |=
			IEEE80211_CHAN_NO_IR;
		wdev->hw->wiphy->bands[NL80211_BAND_2GHZ]->channels[13].flags |=
			IEEE80211_CHAN_DISABLED;
	}

	dev_dbg(wdev->dev, "sending configuration file %s\n", wdev->pdata.file_pds);
	err = wfx_send_pdata_pds(wdev);
	if (err < 0 && err != -ENOENT)
		goto bh_unregister;

	wdev->poll_irq = false;
	err = wdev->hwbus_ops->irq_subscribe(wdev->hwbus_priv);
	if (err)
		goto bh_unregister;

	err = wfx_hif_use_multi_tx_conf(wdev, true);
	if (err)
		dev_err(wdev->dev, "misconfigured IRQ?\n");

	wdev->pdata.gpio_wakeup = gpio_saved;
	if (wdev->pdata.gpio_wakeup) {
		dev_dbg(wdev->dev, "enable 'quiescent' power mode with wakeup GPIO and PDS file %s\n",
			wdev->pdata.file_pds);
		gpiod_set_value_cansleep(wdev->pdata.gpio_wakeup, 1);
		wfx_control_reg_write(wdev, 0);
		wfx_hif_set_operational_mode(wdev, HIF_OP_POWER_MODE_QUIESCENT);
	} else {
		wfx_hif_set_operational_mode(wdev, HIF_OP_POWER_MODE_DOZE);
	}

	for (i = 0; i < ARRAY_SIZE(wdev->addresses); i++) {
		eth_zero_addr(wdev->addresses[i].addr);
		err = of_get_mac_address(wdev->dev->of_node, wdev->addresses[i].addr);
		if (!err)
			wdev->addresses[i].addr[ETH_ALEN - 1] += i;
		else
			ether_addr_copy(wdev->addresses[i].addr, wdev->hw_caps.mac_addr[i]);
		if (!is_valid_ether_addr(wdev->addresses[i].addr)) {
			dev_warn(wdev->dev, "using random MAC address\n");
			eth_random_addr(wdev->addresses[i].addr);
		}
		dev_info(wdev->dev, "MAC address %d: %pM\n", i, wdev->addresses[i].addr);
	}
	wdev->hw->wiphy->n_addresses = ARRAY_SIZE(wdev->addresses);
	wdev->hw->wiphy->addresses = wdev->addresses;

	if (!wfx_api_older_than(wdev, 3, 8))
		wdev->hw->wiphy->flags |= WIPHY_FLAG_SUPPORTS_TDLS;

	err = ieee80211_register_hw(wdev->hw);
	if (err)
		goto irq_unsubscribe;

	err = wfx_debug_init(wdev);
	if (err)
		goto ieee80211_unregister;

	return 0;

ieee80211_unregister:
	ieee80211_unregister_hw(wdev->hw);
irq_unsubscribe:
	wdev->hwbus_ops->irq_unsubscribe(wdev->hwbus_priv);
bh_unregister:
	wfx_bh_unregister(wdev);
	destroy_workqueue(wdev->bh_wq);
	return err;
}

void wfx_release(struct wfx_dev *wdev)
{
	ieee80211_unregister_hw(wdev->hw);
	wfx_hif_shutdown(wdev);
	wdev->hwbus_ops->irq_unsubscribe(wdev->hwbus_priv);
	wfx_bh_unregister(wdev);
	destroy_workqueue(wdev->bh_wq);
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
