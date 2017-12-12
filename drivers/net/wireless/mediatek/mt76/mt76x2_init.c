/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include <linux/delay.h>
#include "mt76x2.h"
#include "mt76x2_eeprom.h"
#include "mt76x2_mcu.h"

struct mt76x2_reg_pair {
	u32 reg;
	u32 value;
};

static bool
mt76x2_wait_for_mac(struct mt76x2_dev *dev)
{
	int i;

	for (i = 0; i < 500; i++) {
		switch (mt76_rr(dev, MT_MAC_CSR0)) {
		case 0:
		case ~0:
			break;
		default:
			return true;
		}
		usleep_range(5000, 10000);
	}

	return false;
}

static bool
wait_for_wpdma(struct mt76x2_dev *dev)
{
	return mt76_poll(dev, MT_WPDMA_GLO_CFG,
			 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			 MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
			 0, 1000);
}

static void
mt76x2_mac_pbf_init(struct mt76x2_dev *dev)
{
	u32 val;

	val = MT_PBF_SYS_CTRL_MCU_RESET |
	      MT_PBF_SYS_CTRL_DMA_RESET |
	      MT_PBF_SYS_CTRL_MAC_RESET |
	      MT_PBF_SYS_CTRL_PBF_RESET |
	      MT_PBF_SYS_CTRL_ASY_RESET;

	mt76_set(dev, MT_PBF_SYS_CTRL, val);
	mt76_clear(dev, MT_PBF_SYS_CTRL, val);

	mt76_wr(dev, MT_PBF_TX_MAX_PCNT, 0xefef3f1f);
	mt76_wr(dev, MT_PBF_RX_MAX_PCNT, 0xfebf);
}

static void
mt76x2_write_reg_pairs(struct mt76x2_dev *dev,
		       const struct mt76x2_reg_pair *data, int len)
{
	while (len > 0) {
		mt76_wr(dev, data->reg, data->value);
		len--;
		data++;
	}
}

static void
mt76_write_mac_initvals(struct mt76x2_dev *dev)
{
#define DEFAULT_PROT_CFG				\
	(FIELD_PREP(MT_PROT_CFG_RATE, 0x2004) |		\
	 FIELD_PREP(MT_PROT_CFG_NAV, 1) |			\
	 FIELD_PREP(MT_PROT_CFG_TXOP_ALLOW, 0x3f) |	\
	 MT_PROT_CFG_RTS_THRESH)

#define DEFAULT_PROT_CFG_20				\
	(FIELD_PREP(MT_PROT_CFG_RATE, 0x2004) |		\
	 FIELD_PREP(MT_PROT_CFG_CTRL, 1) |		\
	 FIELD_PREP(MT_PROT_CFG_NAV, 1) |			\
	 FIELD_PREP(MT_PROT_CFG_TXOP_ALLOW, 0x17))

#define DEFAULT_PROT_CFG_40				\
	(FIELD_PREP(MT_PROT_CFG_RATE, 0x2084) |		\
	 FIELD_PREP(MT_PROT_CFG_CTRL, 1) |		\
	 FIELD_PREP(MT_PROT_CFG_NAV, 1) |			\
	 FIELD_PREP(MT_PROT_CFG_TXOP_ALLOW, 0x3f))

	static const struct mt76x2_reg_pair vals[] = {
		/* Copied from MediaTek reference source */
		{ MT_PBF_SYS_CTRL,		0x00080c00 },
		{ MT_PBF_CFG,			0x1efebcff },
		{ MT_FCE_PSE_CTRL,		0x00000001 },
		{ MT_MAC_SYS_CTRL,		0x0000000c },
		{ MT_MAX_LEN_CFG,		0x003e3f00 },
		{ MT_AMPDU_MAX_LEN_20M1S,	0xaaa99887 },
		{ MT_AMPDU_MAX_LEN_20M2S,	0x000000aa },
		{ MT_XIFS_TIME_CFG,		0x33a40d0a },
		{ MT_BKOFF_SLOT_CFG,		0x00000209 },
		{ MT_TBTT_SYNC_CFG,		0x00422010 },
		{ MT_PWR_PIN_CFG,		0x00000000 },
		{ 0x1238,			0x001700c8 },
		{ MT_TX_SW_CFG0,		0x00101001 },
		{ MT_TX_SW_CFG1,		0x00010000 },
		{ MT_TX_SW_CFG2,		0x00000000 },
		{ MT_TXOP_CTRL_CFG,		0x0400583f },
		{ MT_TX_RTS_CFG,		0x00100020 },
		{ MT_TX_TIMEOUT_CFG,		0x000a2290 },
		{ MT_TX_RETRY_CFG,		0x47f01f0f },
		{ MT_EXP_ACK_TIME,		0x002c00dc },
		{ MT_TX_PROT_CFG6,		0xe3f42004 },
		{ MT_TX_PROT_CFG7,		0xe3f42084 },
		{ MT_TX_PROT_CFG8,		0xe3f42104 },
		{ MT_PIFS_TX_CFG,		0x00060fff },
		{ MT_RX_FILTR_CFG,		0x00015f97 },
		{ MT_LEGACY_BASIC_RATE,		0x0000017f },
		{ MT_HT_BASIC_RATE,		0x00004003 },
		{ MT_PN_PAD_MODE,		0x00000002 },
		{ MT_TXOP_HLDR_ET,		0x00000002 },
		{ 0xa44,			0x00000000 },
		{ MT_HEADER_TRANS_CTRL_REG,	0x00000000 },
		{ MT_TSO_CTRL,			0x00000000 },
		{ MT_AUX_CLK_CFG,		0x00000000 },
		{ MT_DACCLK_EN_DLY_CFG,		0x00000000 },
		{ MT_TX_ALC_CFG_4,		0x00000000 },
		{ MT_TX_ALC_VGA3,		0x00000000 },
		{ MT_TX_PWR_CFG_0,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_1,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_2,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_3,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_4,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_7,		0x3a3a3a3a },
		{ MT_TX_PWR_CFG_8,		0x0000003a },
		{ MT_TX_PWR_CFG_9,		0x0000003a },
		{ MT_EFUSE_CTRL,		0x0000d000 },
		{ MT_PAUSE_ENABLE_CONTROL1,	0x0000000a },
		{ MT_FCE_WLAN_FLOW_CONTROL1,	0x60401c18 },
		{ MT_WPDMA_DELAY_INT_CFG,	0x94ff0000 },
		{ MT_TX_SW_CFG3,		0x00000004 },
		{ MT_HT_FBK_TO_LEGACY,		0x00001818 },
		{ MT_VHT_HT_FBK_CFG1,		0xedcba980 },
		{ MT_PROT_AUTO_TX_CFG,		0x00830083 },
		{ MT_HT_CTRL_CFG,		0x000001ff },
	};
	struct mt76x2_reg_pair prot_vals[] = {
		{ MT_CCK_PROT_CFG,		DEFAULT_PROT_CFG },
		{ MT_OFDM_PROT_CFG,		DEFAULT_PROT_CFG },
		{ MT_MM20_PROT_CFG,		DEFAULT_PROT_CFG_20 },
		{ MT_MM40_PROT_CFG,		DEFAULT_PROT_CFG_40 },
		{ MT_GF20_PROT_CFG,		DEFAULT_PROT_CFG_20 },
		{ MT_GF40_PROT_CFG,		DEFAULT_PROT_CFG_40 },
	};

	mt76x2_write_reg_pairs(dev, vals, ARRAY_SIZE(vals));
	mt76x2_write_reg_pairs(dev, prot_vals, ARRAY_SIZE(prot_vals));
}

static void
mt76x2_fixup_xtal(struct mt76x2_dev *dev)
{
	u16 eep_val;
	s8 offset = 0;

	eep_val = mt76x2_eeprom_get(dev, MT_EE_XTAL_TRIM_2);

	offset = eep_val & 0x7f;
	if ((eep_val & 0xff) == 0xff)
		offset = 0;
	else if (eep_val & 0x80)
		offset = 0 - offset;

	eep_val >>= 8;
	if (eep_val == 0x00 || eep_val == 0xff) {
		eep_val = mt76x2_eeprom_get(dev, MT_EE_XTAL_TRIM_1);
		eep_val &= 0xff;

		if (eep_val == 0x00 || eep_val == 0xff)
			eep_val = 0x14;
	}

	eep_val &= 0x7f;
	mt76_rmw_field(dev, MT_XO_CTRL5, MT_XO_CTRL5_C2_VAL, eep_val + offset);
	mt76_set(dev, MT_XO_CTRL6, MT_XO_CTRL6_C2_CTRL);

	eep_val = mt76x2_eeprom_get(dev, MT_EE_NIC_CONF_2);
	switch (FIELD_GET(MT_EE_NIC_CONF_2_XTAL_OPTION, eep_val)) {
	case 0:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1fee80);
		break;
	case 1:
		mt76_wr(dev, MT_XO_CTRL7, 0x5c1feed0);
		break;
	default:
		break;
	}
}

static void
mt76x2_init_beacon_offsets(struct mt76x2_dev *dev)
{
	u16 base = MT_BEACON_BASE;
	u32 regs[4] = {};
	int i;

	for (i = 0; i < 16; i++) {
		u16 addr = dev->beacon_offsets[i];

		regs[i / 4] |= ((addr - base) / 64) << (8 * (i % 4));
	}

	for (i = 0; i < 4; i++)
		mt76_wr(dev, MT_BCN_OFFSET(i), regs[i]);
}

int mt76x2_mac_reset(struct mt76x2_dev *dev, bool hard)
{
	static const u8 null_addr[ETH_ALEN] = {};
	const u8 *macaddr = dev->mt76.macaddr;
	u32 val;
	int i, k;

	if (!mt76x2_wait_for_mac(dev))
		return -ETIMEDOUT;

	val = mt76_rr(dev, MT_WPDMA_GLO_CFG);

	val &= ~(MT_WPDMA_GLO_CFG_TX_DMA_EN |
		 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
		 MT_WPDMA_GLO_CFG_RX_DMA_EN |
		 MT_WPDMA_GLO_CFG_RX_DMA_BUSY |
		 MT_WPDMA_GLO_CFG_DMA_BURST_SIZE);
	val |= FIELD_PREP(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3);

	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);

	mt76x2_mac_pbf_init(dev);
	mt76_write_mac_initvals(dev);
	mt76x2_fixup_xtal(dev);

	mt76_clear(dev, MT_MAC_SYS_CTRL,
		   MT_MAC_SYS_CTRL_RESET_CSR |
		   MT_MAC_SYS_CTRL_RESET_BBP);

	if (is_mt7612(dev))
		mt76_clear(dev, MT_COEXCFG0, MT_COEXCFG0_COEX_EN);

	mt76_set(dev, MT_EXT_CCA_CFG, 0x0000f000);
	mt76_clear(dev, MT_TX_ALC_CFG_4, BIT(31));

	mt76_wr(dev, MT_RF_BYPASS_0, 0x06000000);
	mt76_wr(dev, MT_RF_SETTING_0, 0x08800000);
	usleep_range(5000, 10000);
	mt76_wr(dev, MT_RF_BYPASS_0, 0x00000000);

	mt76_wr(dev, MT_MCU_CLOCK_CTL, 0x1401);
	mt76_clear(dev, MT_FCE_L2_STUFF, MT_FCE_L2_STUFF_WR_MPDU_LEN_EN);

	mt76_wr(dev, MT_MAC_ADDR_DW0, get_unaligned_le32(macaddr));
	mt76_wr(dev, MT_MAC_ADDR_DW1, get_unaligned_le16(macaddr + 4));

	mt76_wr(dev, MT_MAC_BSSID_DW0, get_unaligned_le32(macaddr));
	mt76_wr(dev, MT_MAC_BSSID_DW1, get_unaligned_le16(macaddr + 4) |
		FIELD_PREP(MT_MAC_BSSID_DW1_MBSS_MODE, 3) | /* 8 beacons */
		MT_MAC_BSSID_DW1_MBSS_LOCAL_BIT);

	/* Fire a pre-TBTT interrupt 8 ms before TBTT */
	mt76_rmw_field(dev, MT_INT_TIMER_CFG, MT_INT_TIMER_CFG_PRE_TBTT,
		       8 << 4);
	mt76_rmw_field(dev, MT_INT_TIMER_CFG, MT_INT_TIMER_CFG_GP_TIMER,
		       MT_DFS_GP_INTERVAL);
	mt76_wr(dev, MT_INT_TIMER_EN, 0);

	mt76_wr(dev, MT_BCN_BYPASS_MASK, 0xffff);
	if (!hard)
		return 0;

	for (i = 0; i < 256 / 32; i++)
		mt76_wr(dev, MT_WCID_DROP_BASE + i * 4, 0);

	for (i = 0; i < 256; i++)
		mt76x2_mac_wcid_setup(dev, i, 0, NULL);

	for (i = 0; i < 16; i++)
		for (k = 0; k < 4; k++)
			mt76x2_mac_shared_key_setup(dev, i, k, NULL);

	for (i = 0; i < 8; i++) {
		mt76x2_mac_set_bssid(dev, i, null_addr);
		mt76x2_mac_set_beacon(dev, i, NULL);
	}

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_STAT_FIFO);

	mt76_set(dev, MT_MAC_APC_BSSID_H(0), MT_MAC_APC_BSSID0_H_EN);

	mt76_wr(dev, MT_CH_TIME_CFG,
		MT_CH_TIME_CFG_TIMER_EN |
		MT_CH_TIME_CFG_TX_AS_BUSY |
		MT_CH_TIME_CFG_RX_AS_BUSY |
		MT_CH_TIME_CFG_NAV_AS_BUSY |
		MT_CH_TIME_CFG_EIFS_AS_BUSY |
		FIELD_PREP(MT_CH_TIME_CFG_CH_TIMER_CLR, 1));

	mt76x2_init_beacon_offsets(dev);

	mt76x2_set_tx_ackto(dev);

	return 0;
}

int mt76x2_mac_start(struct mt76x2_dev *dev)
{
	int i;

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_AGG_CNT(i));

	for (i = 0; i < 16; i++)
		mt76_rr(dev, MT_TX_STAT_FIFO);

	memset(dev->aggr_stats, 0, sizeof(dev->aggr_stats));

	mt76_wr(dev, MT_MAC_SYS_CTRL, MT_MAC_SYS_CTRL_ENABLE_TX);
	wait_for_wpdma(dev);
	usleep_range(50, 100);

	mt76_set(dev, MT_WPDMA_GLO_CFG,
		 MT_WPDMA_GLO_CFG_TX_DMA_EN |
		 MT_WPDMA_GLO_CFG_RX_DMA_EN);

	mt76_clear(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);

	mt76_wr(dev, MT_RX_FILTR_CFG, dev->rxfilter);

	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);

	mt76x2_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL |
			       MT_INT_TX_STAT);

	return 0;
}

void mt76x2_mac_stop(struct mt76x2_dev *dev, bool force)
{
	bool stopped = false;
	u32 rts_cfg;
	int i;

	mt76_wr(dev, MT_MAC_SYS_CTRL, 0);

	rts_cfg = mt76_rr(dev, MT_TX_RTS_CFG);
	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg & ~MT_TX_RTS_CFG_RETRY_LIMIT);

	/* Wait for MAC to become idle */
	for (i = 0; i < 300; i++) {
		if (mt76_rr(dev, MT_MAC_STATUS) &
		    (MT_MAC_STATUS_RX | MT_MAC_STATUS_TX))
			continue;

		if (mt76_rr(dev, MT_BBP(IBI, 12)))
			continue;

		stopped = true;
		break;
	}

	if (force && !stopped) {
		mt76_set(dev, MT_BBP(CORE, 4), BIT(1));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(1));

		mt76_set(dev, MT_BBP(CORE, 4), BIT(0));
		mt76_clear(dev, MT_BBP(CORE, 4), BIT(0));
	}

	mt76_wr(dev, MT_TX_RTS_CFG, rts_cfg);
}

void mt76x2_mac_resume(struct mt76x2_dev *dev)
{
	mt76_wr(dev, MT_MAC_SYS_CTRL,
		MT_MAC_SYS_CTRL_ENABLE_TX |
		MT_MAC_SYS_CTRL_ENABLE_RX);
}

static void
mt76x2_power_on_rf_patch(struct mt76x2_dev *dev)
{
	mt76_set(dev, 0x10130, BIT(0) | BIT(16));
	udelay(1);

	mt76_clear(dev, 0x1001c, 0xff);
	mt76_set(dev, 0x1001c, 0x30);

	mt76_wr(dev, 0x10014, 0x484f);
	udelay(1);

	mt76_set(dev, 0x10130, BIT(17));
	udelay(125);

	mt76_clear(dev, 0x10130, BIT(16));
	udelay(50);

	mt76_set(dev, 0x1014c, BIT(19) | BIT(20));
}

static void
mt76x2_power_on_rf(struct mt76x2_dev *dev, int unit)
{
	int shift = unit ? 8 : 0;

	/* Enable RF BG */
	mt76_set(dev, 0x10130, BIT(0) << shift);
	udelay(10);

	/* Enable RFDIG LDO/AFE/ABB/ADDA */
	mt76_set(dev, 0x10130, (BIT(1) | BIT(3) | BIT(4) | BIT(5)) << shift);
	udelay(10);

	/* Switch RFDIG power to internal LDO */
	mt76_clear(dev, 0x10130, BIT(2) << shift);
	udelay(10);

	mt76x2_power_on_rf_patch(dev);

	mt76_set(dev, 0x530, 0xf);
}

static void
mt76x2_power_on(struct mt76x2_dev *dev)
{
	u32 val;

	/* Turn on WL MTCMOS */
	mt76_set(dev, MT_WLAN_MTC_CTRL, MT_WLAN_MTC_CTRL_MTCMOS_PWR_UP);

	val = MT_WLAN_MTC_CTRL_STATE_UP |
	      MT_WLAN_MTC_CTRL_PWR_ACK |
	      MT_WLAN_MTC_CTRL_PWR_ACK_S;

	mt76_poll(dev, MT_WLAN_MTC_CTRL, val, val, 1000);

	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0x7f << 16);
	udelay(10);

	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0xf << 24);
	udelay(10);

	mt76_set(dev, MT_WLAN_MTC_CTRL, 0xf << 24);
	mt76_clear(dev, MT_WLAN_MTC_CTRL, 0xfff);

	/* Turn on AD/DA power down */
	mt76_clear(dev, 0x11204, BIT(3));

	/* WLAN function enable */
	mt76_set(dev, 0x10080, BIT(0));

	/* Release BBP software reset */
	mt76_clear(dev, 0x10064, BIT(18));

	mt76x2_power_on_rf(dev, 0);
	mt76x2_power_on_rf(dev, 1);
}

void mt76x2_set_tx_ackto(struct mt76x2_dev *dev)
{
	u8 ackto, sifs, slottime = dev->slottime;

	slottime += 3 * dev->coverage_class;

	sifs = mt76_get_field(dev, MT_XIFS_TIME_CFG,
			      MT_XIFS_TIME_CFG_OFDM_SIFS);

	ackto = slottime + sifs;
	mt76_rmw_field(dev, MT_TX_TIMEOUT_CFG,
		       MT_TX_TIMEOUT_CFG_ACKTO, ackto);
}

static void
mt76x2_set_wlan_state(struct mt76x2_dev *dev, bool enable)
{
	u32 val = mt76_rr(dev, MT_WLAN_FUN_CTRL);

	if (enable)
		val |= (MT_WLAN_FUN_CTRL_WLAN_EN |
			MT_WLAN_FUN_CTRL_WLAN_CLK_EN);
	else
		val &= ~(MT_WLAN_FUN_CTRL_WLAN_EN |
			 MT_WLAN_FUN_CTRL_WLAN_CLK_EN);

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);
}

static void
mt76x2_reset_wlan(struct mt76x2_dev *dev, bool enable)
{
	u32 val;

	val = mt76_rr(dev, MT_WLAN_FUN_CTRL);

	val &= ~MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL;

	if (val & MT_WLAN_FUN_CTRL_WLAN_EN) {
		val |= MT_WLAN_FUN_CTRL_WLAN_RESET_RF;
		mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
		udelay(20);

		val &= ~MT_WLAN_FUN_CTRL_WLAN_RESET_RF;
	}

	mt76_wr(dev, MT_WLAN_FUN_CTRL, val);
	udelay(20);

	mt76x2_set_wlan_state(dev, enable);
}

int mt76x2_init_hardware(struct mt76x2_dev *dev)
{
	static const u16 beacon_offsets[16] = {
		/* 1024 byte per beacon */
		0xc000,
		0xc400,
		0xc800,
		0xcc00,
		0xd000,
		0xd400,
		0xd800,
		0xdc00,

		/* BSS idx 8-15 not used for beacons */
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
		0xc000,
	};
	u32 val;
	int ret;

	dev->beacon_offsets = beacon_offsets;
	tasklet_init(&dev->pre_tbtt_tasklet, mt76x2_pre_tbtt_tasklet,
		     (unsigned long) dev);

	dev->chainmask = 0x202;
	dev->global_wcid.idx = 255;
	dev->global_wcid.hw_key_idx = -1;
	dev->slottime = 9;

	val = mt76_rr(dev, MT_WPDMA_GLO_CFG);
	val &= MT_WPDMA_GLO_CFG_DMA_BURST_SIZE |
	       MT_WPDMA_GLO_CFG_BIG_ENDIAN |
	       MT_WPDMA_GLO_CFG_HDR_SEG_LEN;
	val |= MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE;
	mt76_wr(dev, MT_WPDMA_GLO_CFG, val);

	mt76x2_reset_wlan(dev, true);
	mt76x2_power_on(dev);

	ret = mt76x2_eeprom_init(dev);
	if (ret)
		return ret;

	ret = mt76x2_mac_reset(dev, true);
	if (ret)
		return ret;

	ret = mt76x2_dma_init(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_INITIALIZED, &dev->mt76.state);
	ret = mt76x2_mac_start(dev);
	if (ret)
		return ret;

	ret = mt76x2_mcu_init(dev);
	if (ret)
		return ret;

	mt76x2_mac_stop(dev, false);
	dev->rxfilter = mt76_rr(dev, MT_RX_FILTR_CFG);

	return 0;
}

void mt76x2_stop_hardware(struct mt76x2_dev *dev)
{
	cancel_delayed_work_sync(&dev->cal_work);
	cancel_delayed_work_sync(&dev->mac_work);
	mt76x2_mcu_set_radio_state(dev, false);
	mt76x2_mac_stop(dev, false);
}

void mt76x2_cleanup(struct mt76x2_dev *dev)
{
	mt76x2_stop_hardware(dev);
	mt76x2_dma_cleanup(dev);
	mt76x2_mcu_cleanup(dev);
}

struct mt76x2_dev *mt76x2_alloc_device(struct device *pdev)
{
	static const struct mt76_driver_ops drv_ops = {
		.txwi_size = sizeof(struct mt76x2_txwi),
		.update_survey = mt76x2_update_channel,
		.tx_prepare_skb = mt76x2_tx_prepare_skb,
		.tx_complete_skb = mt76x2_tx_complete_skb,
		.rx_skb = mt76x2_queue_rx_skb,
		.rx_poll_complete = mt76x2_rx_poll_complete,
	};
	struct ieee80211_hw *hw;
	struct mt76x2_dev *dev;

	hw = ieee80211_alloc_hw(sizeof(*dev), &mt76x2_ops);
	if (!hw)
		return NULL;

	dev = hw->priv;
	dev->mt76.dev = pdev;
	dev->mt76.hw = hw;
	dev->mt76.drv = &drv_ops;
	mutex_init(&dev->mutex);
	spin_lock_init(&dev->irq_lock);

	return dev;
}

static void mt76x2_regd_notifier(struct wiphy *wiphy,
				 struct regulatory_request *request)
{
	struct ieee80211_hw *hw = wiphy_to_ieee80211_hw(wiphy);
	struct mt76x2_dev *dev = hw->priv;

	dev->dfs_pd.region = request->dfs_region;
}

#define CCK_RATE(_idx, _rate) {					\
	.bitrate = _rate,					\
	.flags = IEEE80211_RATE_SHORT_PREAMBLE,			\
	.hw_value = (MT_PHY_TYPE_CCK << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_CCK << 8) | (8 + _idx),	\
}

#define OFDM_RATE(_idx, _rate) {				\
	.bitrate = _rate,					\
	.hw_value = (MT_PHY_TYPE_OFDM << 8) | _idx,		\
	.hw_value_short = (MT_PHY_TYPE_OFDM << 8) | _idx,	\
}

static struct ieee80211_rate mt76x2_rates[] = {
	CCK_RATE(0, 10),
	CCK_RATE(1, 20),
	CCK_RATE(2, 55),
	CCK_RATE(3, 110),
	OFDM_RATE(0, 60),
	OFDM_RATE(1, 90),
	OFDM_RATE(2, 120),
	OFDM_RATE(3, 180),
	OFDM_RATE(4, 240),
	OFDM_RATE(5, 360),
	OFDM_RATE(6, 480),
	OFDM_RATE(7, 540),
};

static const struct ieee80211_iface_limit if_limits[] = {
	{
		.max = 1,
		.types = BIT(NL80211_IFTYPE_ADHOC)
	}, {
		.max = 8,
		.types = BIT(NL80211_IFTYPE_STATION) |
#ifdef CONFIG_MAC80211_MESH
			 BIT(NL80211_IFTYPE_MESH_POINT) |
#endif
			 BIT(NL80211_IFTYPE_AP)
	 },
};

static const struct ieee80211_iface_combination if_comb[] = {
	{
		.limits = if_limits,
		.n_limits = ARRAY_SIZE(if_limits),
		.max_interfaces = 8,
		.num_different_channels = 1,
		.beacon_int_infra_match = true,
		.radar_detect_widths = BIT(NL80211_CHAN_WIDTH_20_NOHT) |
				       BIT(NL80211_CHAN_WIDTH_20) |
				       BIT(NL80211_CHAN_WIDTH_40) |
				       BIT(NL80211_CHAN_WIDTH_80),
	}
};

static void mt76x2_led_set_config(struct mt76_dev *mt76, u8 delay_on,
				  u8 delay_off)
{
	struct mt76x2_dev *dev = container_of(mt76, struct mt76x2_dev,
					      mt76);
	u32 val;

	val = MT_LED_STATUS_DURATION(0xff) |
	      MT_LED_STATUS_OFF(delay_off) |
	      MT_LED_STATUS_ON(delay_on);

	mt76_wr(dev, MT_LED_S0(mt76->led_pin), val);
	mt76_wr(dev, MT_LED_S1(mt76->led_pin), val);

	val = MT_LED_CTRL_REPLAY(mt76->led_pin) |
	      MT_LED_CTRL_KICK(mt76->led_pin);
	if (mt76->led_al)
		val |= MT_LED_CTRL_POLARITY(mt76->led_pin);
	mt76_wr(dev, MT_LED_CTRL, val);
}

static int mt76x2_led_set_blink(struct led_classdev *led_cdev,
				unsigned long *delay_on,
				unsigned long *delay_off)
{
	struct mt76_dev *mt76 = container_of(led_cdev, struct mt76_dev,
					     led_cdev);
	u8 delta_on, delta_off;

	delta_off = max_t(u8, *delay_off / 10, 1);
	delta_on = max_t(u8, *delay_on / 10, 1);

	mt76x2_led_set_config(mt76, delta_on, delta_off);
	return 0;
}

static void mt76x2_led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	struct mt76_dev *mt76 = container_of(led_cdev, struct mt76_dev,
					     led_cdev);

	if (!brightness)
		mt76x2_led_set_config(mt76, 0, 0xff);
	else
		mt76x2_led_set_config(mt76, 0xff, 0);
}

int mt76x2_register_device(struct mt76x2_dev *dev)
{
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct wiphy *wiphy = hw->wiphy;
	void *status_fifo;
	int fifo_size;
	int i, ret;

	fifo_size = roundup_pow_of_two(32 * sizeof(struct mt76x2_tx_status));
	status_fifo = devm_kzalloc(dev->mt76.dev, fifo_size, GFP_KERNEL);
	if (!status_fifo)
		return -ENOMEM;

	kfifo_init(&dev->txstatus_fifo, status_fifo, fifo_size);

	ret = mt76x2_init_hardware(dev);
	if (ret)
		return ret;

	hw->queues = 4;
	hw->max_rates = 1;
	hw->max_report_rates = 7;
	hw->max_rate_tries = 1;
	hw->extra_tx_headroom = 2;

	hw->sta_data_size = sizeof(struct mt76x2_sta);
	hw->vif_data_size = sizeof(struct mt76x2_vif);

	for (i = 0; i < ARRAY_SIZE(dev->macaddr_list); i++) {
		u8 *addr = dev->macaddr_list[i].addr;

		memcpy(addr, dev->mt76.macaddr, ETH_ALEN);

		if (!i)
			continue;

		addr[0] |= BIT(1);
		addr[0] ^= ((i - 1) << 2);
	}
	wiphy->addresses = dev->macaddr_list;
	wiphy->n_addresses = ARRAY_SIZE(dev->macaddr_list);

	wiphy->iface_combinations = if_comb;
	wiphy->n_iface_combinations = ARRAY_SIZE(if_comb);

	wiphy->reg_notifier = mt76x2_regd_notifier;

	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_VHT_IBSS);

	ieee80211_hw_set(hw, SUPPORTS_HT_CCK_RATES);
	INIT_DELAYED_WORK(&dev->cal_work, mt76x2_phy_calibrate);
	INIT_DELAYED_WORK(&dev->mac_work, mt76x2_mac_work);

	dev->mt76.sband_2g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;
	dev->mt76.sband_5g.sband.ht_cap.cap |= IEEE80211_HT_CAP_LDPC_CODING;

	mt76x2_dfs_init_detector(dev);

	/* init led callbacks */
	dev->mt76.led_cdev.brightness_set = mt76x2_led_set_brightness;
	dev->mt76.led_cdev.blink_set = mt76x2_led_set_blink;

	ret = mt76_register_device(&dev->mt76, true, mt76x2_rates,
				   ARRAY_SIZE(mt76x2_rates));
	if (ret)
		goto fail;

	mt76x2_init_debugfs(dev);

	return 0;

fail:
	mt76x2_stop_hardware(dev);
	return ret;
}


