// SPDX-License-Identifier: GPL-2.0-only
/*
 * RTL8XXXU mac80211 USB driver - 8723a specific subdriver
 *
 * Copyright (c) 2014 - 2017 Jes Sorensen <Jes.Sorensen@gmail.com>
 *
 * Portions, notably calibration code:
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This driver was written as a replacement for the vendor provided
 * rtl8723au driver. As the Realtek 8xxx chips are very similar in
 * their programming interface, I have started adding support for
 * additional 8xxx chips like the 8192cu, 8188cus, etc.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/wireless.h>
#include <linux/firmware.h>
#include <linux/moduleparam.h>
#include <net/mac80211.h>
#include "rtl8xxxu.h"
#include "rtl8xxxu_regs.h"

static struct rtl8xxxu_power_base rtl8723a_power_base = {
	.reg_0e00 = 0x0a0c0c0c,
	.reg_0e04 = 0x02040608,
	.reg_0e08 = 0x00000000,
	.reg_086c = 0x00000000,

	.reg_0e10 = 0x0a0c0d0e,
	.reg_0e14 = 0x02040608,
	.reg_0e18 = 0x0a0c0d0e,
	.reg_0e1c = 0x02040608,

	.reg_0830 = 0x0a0c0c0c,
	.reg_0834 = 0x02040608,
	.reg_0838 = 0x00000000,
	.reg_086c_2 = 0x00000000,

	.reg_083c = 0x0a0c0d0e,
	.reg_0848 = 0x02040608,
	.reg_084c = 0x0a0c0d0e,
	.reg_0868 = 0x02040608,
};

static const struct rtl8xxxu_rfregval rtl8723au_radioa_1t_init_table[] = {
	{0x00, 0x00030159}, {0x01, 0x00031284},
	{0x02, 0x00098000}, {0x03, 0x00039c63},
	{0x04, 0x000210e7}, {0x09, 0x0002044f},
	{0x0a, 0x0001a3f1}, {0x0b, 0x00014787},
	{0x0c, 0x000896fe}, {0x0d, 0x0000e02c},
	{0x0e, 0x00039ce7}, {0x0f, 0x00000451},
	{0x19, 0x00000000}, {0x1a, 0x00030355},
	{0x1b, 0x00060a00}, {0x1c, 0x000fc378},
	{0x1d, 0x000a1250}, {0x1e, 0x0000024f},
	{0x1f, 0x00000000}, {0x20, 0x0000b614},
	{0x21, 0x0006c000}, {0x22, 0x00000000},
	{0x23, 0x00001558}, {0x24, 0x00000060},
	{0x25, 0x00000483}, {0x26, 0x0004f000},
	{0x27, 0x000ec7d9}, {0x28, 0x00057730},
	{0x29, 0x00004783}, {0x2a, 0x00000001},
	{0x2b, 0x00021334}, {0x2a, 0x00000000},
	{0x2b, 0x00000054}, {0x2a, 0x00000001},
	{0x2b, 0x00000808}, {0x2b, 0x00053333},
	{0x2c, 0x0000000c}, {0x2a, 0x00000002},
	{0x2b, 0x00000808}, {0x2b, 0x0005b333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000003},
	{0x2b, 0x00000808}, {0x2b, 0x00063333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000004},
	{0x2b, 0x00000808}, {0x2b, 0x0006b333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000005},
	{0x2b, 0x00000808}, {0x2b, 0x00073333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000006},
	{0x2b, 0x00000709}, {0x2b, 0x0005b333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000007},
	{0x2b, 0x00000709}, {0x2b, 0x00063333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000008},
	{0x2b, 0x0000060a}, {0x2b, 0x0004b333},
	{0x2c, 0x0000000d}, {0x2a, 0x00000009},
	{0x2b, 0x0000060a}, {0x2b, 0x00053333},
	{0x2c, 0x0000000d}, {0x2a, 0x0000000a},
	{0x2b, 0x0000060a}, {0x2b, 0x0005b333},
	{0x2c, 0x0000000d}, {0x2a, 0x0000000b},
	{0x2b, 0x0000060a}, {0x2b, 0x00063333},
	{0x2c, 0x0000000d}, {0x2a, 0x0000000c},
	{0x2b, 0x0000060a}, {0x2b, 0x0006b333},
	{0x2c, 0x0000000d}, {0x2a, 0x0000000d},
	{0x2b, 0x0000060a}, {0x2b, 0x00073333},
	{0x2c, 0x0000000d}, {0x2a, 0x0000000e},
	{0x2b, 0x0000050b}, {0x2b, 0x00066666},
	{0x2c, 0x0000001a}, {0x2a, 0x000e0000},
	{0x10, 0x0004000f}, {0x11, 0x000e31fc},
	{0x10, 0x0006000f}, {0x11, 0x000ff9f8},
	{0x10, 0x0002000f}, {0x11, 0x000203f9},
	{0x10, 0x0003000f}, {0x11, 0x000ff500},
	{0x10, 0x00000000}, {0x11, 0x00000000},
	{0x10, 0x0008000f}, {0x11, 0x0003f100},
	{0x10, 0x0009000f}, {0x11, 0x00023100},
	{0x12, 0x00032000}, {0x12, 0x00071000},
	{0x12, 0x000b0000}, {0x12, 0x000fc000},
	{0x13, 0x000287b3}, {0x13, 0x000244b7},
	{0x13, 0x000204ab}, {0x13, 0x0001c49f},
	{0x13, 0x00018493}, {0x13, 0x0001429b},
	{0x13, 0x00010299}, {0x13, 0x0000c29c},
	{0x13, 0x000081a0}, {0x13, 0x000040ac},
	{0x13, 0x00000020}, {0x14, 0x0001944c},
	{0x14, 0x00059444}, {0x14, 0x0009944c},
	{0x14, 0x000d9444}, {0x15, 0x0000f474},
	{0x15, 0x0004f477}, {0x15, 0x0008f455},
	{0x15, 0x000cf455}, {0x16, 0x00000339},
	{0x16, 0x00040339}, {0x16, 0x00080339},
	{0x16, 0x000c0366}, {0x00, 0x00010159},
	{0x18, 0x0000f401}, {0xfe, 0x00000000},
	{0xfe, 0x00000000}, {0x1f, 0x00000003},
	{0xfe, 0x00000000}, {0xfe, 0x00000000},
	{0x1e, 0x00000247}, {0x1f, 0x00000000},
	{0x00, 0x00030159},
	{0xff, 0xffffffff}
};

static int rtl8723au_identify_chip(struct rtl8xxxu_priv *priv)
{
	struct device *dev = &priv->udev->dev;
	u32 val32, sys_cfg, vendor;
	int ret = 0;

	sys_cfg = rtl8xxxu_read32(priv, REG_SYS_CFG);
	priv->chip_cut = u32_get_bits(sys_cfg, SYS_CFG_CHIP_VERSION_MASK);
	if (sys_cfg & SYS_CFG_TRP_VAUX_EN) {
		dev_info(dev, "Unsupported test chip\n");
		ret = -ENOTSUPP;
		goto out;
	}

	strscpy(priv->chip_name, "8723AU", sizeof(priv->chip_name));
	priv->usb_interrupts = 1;
	priv->rtl_chip = RTL8723A;

	priv->rf_paths = 1;
	priv->rx_paths = 1;
	priv->tx_paths = 1;

	val32 = rtl8xxxu_read32(priv, REG_MULTI_FUNC_CTRL);
	if (val32 & MULTI_WIFI_FUNC_EN)
		priv->has_wifi = 1;
	if (val32 & MULTI_BT_FUNC_EN)
		priv->has_bluetooth = 1;
	if (val32 & MULTI_GPS_FUNC_EN)
		priv->has_gps = 1;
	priv->is_multi_func = 1;

	vendor = sys_cfg & SYS_CFG_VENDOR_ID;
	rtl8xxxu_identify_vendor_1bit(priv, vendor);

	val32 = rtl8xxxu_read32(priv, REG_GPIO_OUTSTS);
	priv->rom_rev = u32_get_bits(val32, GPIO_RF_RL_ID);

	rtl8xxxu_config_endpoints_sie(priv);

	/*
	 * Fallback for devices that do not provide REG_NORMAL_SIE_EP_TX
	 */
	if (!priv->ep_tx_count)
		ret = rtl8xxxu_config_endpoints_no_sie(priv);

out:
	return ret;
}

static int rtl8723au_parse_efuse(struct rtl8xxxu_priv *priv)
{
	struct rtl8723au_efuse *efuse = &priv->efuse_wifi.efuse8723;

	if (efuse->rtl_id != cpu_to_le16(0x8129))
		return -EINVAL;

	ether_addr_copy(priv->mac_addr, efuse->mac_addr);

	memcpy(priv->cck_tx_power_index_A,
	       efuse->cck_tx_power_index_A,
	       sizeof(efuse->cck_tx_power_index_A));
	memcpy(priv->cck_tx_power_index_B,
	       efuse->cck_tx_power_index_B,
	       sizeof(efuse->cck_tx_power_index_B));

	memcpy(priv->ht40_1s_tx_power_index_A,
	       efuse->ht40_1s_tx_power_index_A,
	       sizeof(efuse->ht40_1s_tx_power_index_A));
	memcpy(priv->ht40_1s_tx_power_index_B,
	       efuse->ht40_1s_tx_power_index_B,
	       sizeof(efuse->ht40_1s_tx_power_index_B));

	memcpy(priv->ht20_tx_power_index_diff,
	       efuse->ht20_tx_power_index_diff,
	       sizeof(efuse->ht20_tx_power_index_diff));
	memcpy(priv->ofdm_tx_power_index_diff,
	       efuse->ofdm_tx_power_index_diff,
	       sizeof(efuse->ofdm_tx_power_index_diff));

	memcpy(priv->ht40_max_power_offset,
	       efuse->ht40_max_power_offset,
	       sizeof(efuse->ht40_max_power_offset));
	memcpy(priv->ht20_max_power_offset,
	       efuse->ht20_max_power_offset,
	       sizeof(efuse->ht20_max_power_offset));

	if (priv->efuse_wifi.efuse8723.version >= 0x01)
		priv->default_crystal_cap = priv->efuse_wifi.efuse8723.xtal_k & 0x3f;
	else
		priv->fops->set_crystal_cap = NULL;

	priv->power_base = &rtl8723a_power_base;

	return 0;
}

static int rtl8723au_load_firmware(struct rtl8xxxu_priv *priv)
{
	const char *fw_name;
	int ret;

	switch (priv->chip_cut) {
	case 0:
		fw_name = "rtlwifi/rtl8723aufw_A.bin";
		break;
	case 1:
		if (priv->enable_bluetooth)
			fw_name = "rtlwifi/rtl8723aufw_B.bin";
		else
			fw_name = "rtlwifi/rtl8723aufw_B_NoBT.bin";

		break;
	default:
		return -EINVAL;
	}

	ret = rtl8xxxu_load_firmware(priv, fw_name);
	return ret;
}

static int rtl8723au_init_phy_rf(struct rtl8xxxu_priv *priv)
{
	int ret;

	ret = rtl8xxxu_init_phy_rf(priv, rtl8723au_radioa_1t_init_table, RF_A);

	/* Reduce 80M spur */
	rtl8xxxu_write32(priv, REG_AFE_XTAL_CTRL, 0x0381808d);
	rtl8xxxu_write32(priv, REG_AFE_PLL_CTRL, 0xf0ffff83);
	rtl8xxxu_write32(priv, REG_AFE_PLL_CTRL, 0xf0ffff82);
	rtl8xxxu_write32(priv, REG_AFE_PLL_CTRL, 0xf0ffff83);

	return ret;
}

static int rtl8723a_emu_to_active(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u32 val32;
	int count, ret = 0;

	/* 0x20[0] = 1 enable LDOA12 MACRO block for all interface*/
	val8 = rtl8xxxu_read8(priv, REG_LDOA15_CTRL);
	val8 |= LDOA15_ENABLE;
	rtl8xxxu_write8(priv, REG_LDOA15_CTRL, val8);

	/* 0x67[0] = 0 to disable BT_GPS_SEL pins*/
	val8 = rtl8xxxu_read8(priv, 0x0067);
	val8 &= ~BIT(4);
	rtl8xxxu_write8(priv, 0x0067, val8);

	mdelay(1);

	/* 0x00[5] = 0 release analog Ips to digital, 1:isolation */
	val8 = rtl8xxxu_read8(priv, REG_SYS_ISO_CTRL);
	val8 &= ~SYS_ISO_ANALOG_IPS;
	rtl8xxxu_write8(priv, REG_SYS_ISO_CTRL, val8);

	/* disable SW LPS 0x04[10]= 0 */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~BIT(2);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* wait till 0x04[17] = 1 power ready*/
	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if (val32 & BIT(17))
			break;

		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* We should be able to optimize the following three entries into one */

	/* release WLON reset 0x04[16]= 1*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 2);
	val8 |= BIT(0);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 2, val8);

	/* disable HWPDN 0x04[15]= 0*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~BIT(7);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* disable WL suspend*/
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 1);
	val8 &= ~(BIT(3) | BIT(4));
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 1, val8);

	/* set, then poll until 0 */
	val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
	val32 |= APS_FSMCO_MAC_ENABLE;
	rtl8xxxu_write32(priv, REG_APS_FSMCO, val32);

	for (count = RTL8XXXU_MAX_REG_POLL; count; count--) {
		val32 = rtl8xxxu_read32(priv, REG_APS_FSMCO);
		if ((val32 & APS_FSMCO_MAC_ENABLE) == 0) {
			ret = 0;
			break;
		}
		udelay(10);
	}

	if (!count) {
		ret = -EBUSY;
		goto exit;
	}

	/* 0x4C[23] = 0x4E[7] = 1, switch DPDT_SEL_P output from WL BB */
	/*
	 * Note: Vendor driver actually clears this bit, despite the
	 * documentation claims it's being set!
	 */
	val8 = rtl8xxxu_read8(priv, REG_LEDCFG2);
	val8 |= LEDCFG2_DPDT_SELECT;
	val8 &= ~LEDCFG2_DPDT_SELECT;
	rtl8xxxu_write8(priv, REG_LEDCFG2, val8);

exit:
	return ret;
}

static int rtl8723au_power_on(struct rtl8xxxu_priv *priv)
{
	u8 val8;
	u16 val16;
	u32 val32;
	int ret;

	/*
	 * RSV_CTRL 0x001C[7:0] = 0x00, unlock ISO/CLK/Power control register
	 */
	rtl8xxxu_write8(priv, REG_RSV_CTRL, 0x0);

	rtl8xxxu_disabled_to_emu(priv);

	ret = rtl8723a_emu_to_active(priv);
	if (ret)
		goto exit;

	/*
	 * 0x0004[19] = 1, reset 8051
	 */
	val8 = rtl8xxxu_read8(priv, REG_APS_FSMCO + 2);
	val8 |= BIT(3);
	rtl8xxxu_write8(priv, REG_APS_FSMCO + 2, val8);

	/*
	 * Enable MAC DMA/WMAC/SCHEDULE/SEC block
	 * Set CR bit10 to enable 32k calibration.
	 */
	val16 = rtl8xxxu_read16(priv, REG_CR);
	val16 |= (CR_HCI_TXDMA_ENABLE | CR_HCI_RXDMA_ENABLE |
		  CR_TXDMA_ENABLE | CR_RXDMA_ENABLE |
		  CR_PROTOCOL_ENABLE | CR_SCHEDULE_ENABLE |
		  CR_MAC_TX_ENABLE | CR_MAC_RX_ENABLE |
		  CR_SECURITY_ENABLE | CR_CALTIMER_ENABLE);
	rtl8xxxu_write16(priv, REG_CR, val16);

	/* For EFuse PG */
	val32 = rtl8xxxu_read32(priv, REG_EFUSE_CTRL);
	val32 &= ~(BIT(28) | BIT(29) | BIT(30));
	val32 |= (0x06 << 28);
	rtl8xxxu_write32(priv, REG_EFUSE_CTRL, val32);
exit:
	return ret;
}

#define XTAL1	GENMASK(23, 18)
#define XTAL0	GENMASK(17, 12)

void rtl8723a_set_crystal_cap(struct rtl8xxxu_priv *priv, u8 crystal_cap)
{
	struct rtl8xxxu_cfo_tracking *cfo = &priv->cfo_tracking;
	u32 val32;

	if (crystal_cap == cfo->crystal_cap)
		return;

	val32 = rtl8xxxu_read32(priv, REG_MAC_PHY_CTRL);

	dev_dbg(&priv->udev->dev,
	        "%s: Adjusting crystal cap from 0x%x (actually 0x%lx 0x%lx) to 0x%x\n",
	        __func__,
	        cfo->crystal_cap,
	        FIELD_GET(XTAL1, val32),
	        FIELD_GET(XTAL0, val32),
	        crystal_cap);

	val32 &= ~(XTAL1 | XTAL0);
	val32 |= FIELD_PREP(XTAL1, crystal_cap) |
		 FIELD_PREP(XTAL0, crystal_cap);
	rtl8xxxu_write32(priv, REG_MAC_PHY_CTRL, val32);

	cfo->crystal_cap = crystal_cap;
}

s8 rtl8723a_cck_rssi(struct rtl8xxxu_priv *priv, struct rtl8723au_phy_stats *phy_stats)
{
	u8 cck_agc_rpt = phy_stats->cck_agc_rpt_ofdm_cfosho_a;
	s8 rx_pwr_all = 0x00;

	switch (cck_agc_rpt & 0xc0) {
	case 0xc0:
		rx_pwr_all = -46 - (cck_agc_rpt & 0x3e);
		break;
	case 0x80:
		rx_pwr_all = -26 - (cck_agc_rpt & 0x3e);
		break;
	case 0x40:
		rx_pwr_all = -12 - (cck_agc_rpt & 0x3e);
		break;
	case 0x00:
		rx_pwr_all = 16 - (cck_agc_rpt & 0x3e);
		break;
	}

	return rx_pwr_all;
}

static int rtl8723au_led_brightness_set(struct led_classdev *led_cdev,
					enum led_brightness brightness)
{
	struct rtl8xxxu_priv *priv = container_of(led_cdev,
						  struct rtl8xxxu_priv,
						  led_cdev);
	u8 ledcfg = rtl8xxxu_read8(priv, REG_LEDCFG2);

	if (brightness == LED_OFF) {
		ledcfg &= ~LEDCFG2_HW_LED_CONTROL;
		ledcfg |= LEDCFG2_SW_LED_CONTROL | LEDCFG2_SW_LED_DISABLE;
	} else if (brightness == LED_ON) {
		ledcfg &= ~(LEDCFG2_HW_LED_CONTROL | LEDCFG2_SW_LED_DISABLE);
		ledcfg |= LEDCFG2_SW_LED_CONTROL;
	} else if (brightness == RTL8XXXU_HW_LED_CONTROL) {
		ledcfg &= ~LEDCFG2_SW_LED_DISABLE;
		ledcfg |= LEDCFG2_HW_LED_CONTROL | LEDCFG2_HW_LED_ENABLE;
	}

	rtl8xxxu_write8(priv, REG_LEDCFG2, ledcfg);

	return 0;
}

struct rtl8xxxu_fileops rtl8723au_fops = {
	.identify_chip = rtl8723au_identify_chip,
	.parse_efuse = rtl8723au_parse_efuse,
	.load_firmware = rtl8723au_load_firmware,
	.power_on = rtl8723au_power_on,
	.power_off = rtl8xxxu_power_off,
	.read_efuse = rtl8xxxu_read_efuse,
	.reset_8051 = rtl8xxxu_reset_8051,
	.llt_init = rtl8xxxu_init_llt_table,
	.init_phy_bb = rtl8xxxu_gen1_init_phy_bb,
	.init_phy_rf = rtl8723au_init_phy_rf,
	.phy_lc_calibrate = rtl8723a_phy_lc_calibrate,
	.phy_iq_calibrate = rtl8xxxu_gen1_phy_iq_calibrate,
	.config_channel = rtl8xxxu_gen1_config_channel,
	.parse_rx_desc = rtl8xxxu_parse_rxdesc16,
	.parse_phystats = rtl8723au_rx_parse_phystats,
	.init_aggregation = rtl8xxxu_gen1_init_aggregation,
	.enable_rf = rtl8xxxu_gen1_enable_rf,
	.disable_rf = rtl8xxxu_gen1_disable_rf,
	.usb_quirks = rtl8xxxu_gen1_usb_quirks,
	.set_tx_power = rtl8xxxu_gen1_set_tx_power,
	.update_rate_mask = rtl8xxxu_update_rate_mask,
	.report_connect = rtl8xxxu_gen1_report_connect,
	.report_rssi = rtl8xxxu_gen1_report_rssi,
	.fill_txdesc = rtl8xxxu_fill_txdesc_v1,
	.set_crystal_cap = rtl8723a_set_crystal_cap,
	.cck_rssi = rtl8723a_cck_rssi,
	.led_classdev_brightness_set = rtl8723au_led_brightness_set,
	.writeN_block_size = 1024,
	.rx_agg_buf_size = 16000,
	.tx_desc_size = sizeof(struct rtl8xxxu_txdesc32),
	.rx_desc_size = sizeof(struct rtl8xxxu_rxdesc16),
	.adda_1t_init = 0x0b1b25a0,
	.adda_1t_path_on = 0x0bdb25a0,
	.adda_2t_path_on_a = 0x04db25a4,
	.adda_2t_path_on_b = 0x0b1b25a4,
	.trxff_boundary = 0x27ff,
	.pbp_rx = PBP_PAGE_SIZE_128,
	.pbp_tx = PBP_PAGE_SIZE_128,
	.mactable = rtl8xxxu_gen1_mac_init_table,
	.total_page_num = TX_TOTAL_PAGE_NUM,
	.page_num_hi = TX_PAGE_NUM_HI_PQ,
	.page_num_lo = TX_PAGE_NUM_LO_PQ,
	.page_num_norm = TX_PAGE_NUM_NORM_PQ,
};
