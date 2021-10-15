// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen <csq@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/soc/rockchip/rk_vendor_storage.h>
#include <linux/slab.h>

#include "rk628.h"
#include "rk628_combrxphy.h"
#include "rk628_cru.h"
#include "rk628_hdmirx.h"

#define INIT_FIFO_STATE			64

#define is_validfs(x) (x == 32000 || \
			x == 44100 || \
			x == 48000 || \
			x == 88200 || \
			x == 96000 || \
			x == 176400 || \
			x == 192000 || \
			x == 768000)

struct rk628_audiostate {
	u32 hdmirx_aud_clkrate;
	u32 fs_audio;
	u32 ctsn_flag;
	u32 fifo_flag;
	int init_state;
	int pre_state;
	bool fifo_int;
	bool audio_enable;
};

struct rk628_audioinfo {
	struct delayed_work delayed_work_audio_rate_change;
	struct delayed_work delayed_work_audio;
	struct mutex *confctl_mutex;
	struct rk628 *rk628;
	struct rk628_audiostate audio_state;
	bool i2s_enabled_default;
	bool i2s_enabled;
	int debug;
	bool fifo_ints_en;
	bool ctsn_ints_en;
	bool audio_present;
	struct device *dev;
};

static int hdcp_load_keys_cb(struct rk628 *rk628, struct rk628_hdcp *hdcp)
{
	int size;
	u8 hdcp_vendor_data[320];

	hdcp->keys = kmalloc(HDCP_KEY_SIZE, GFP_KERNEL);
	if (!hdcp->keys)
		return -ENOMEM;

	hdcp->seeds = kmalloc(HDCP_KEY_SEED_SIZE, GFP_KERNEL);
	if (!hdcp->seeds) {
		kfree(hdcp->keys);
		hdcp->keys = NULL;
		return -ENOMEM;
	}

	size = rk_vendor_read(HDMIRX_HDCP1X_ID, hdcp_vendor_data, 314);
	if (size < (HDCP_KEY_SIZE + HDCP_KEY_SEED_SIZE)) {
		dev_dbg(rk628->dev, "HDCP: read size %d\n", size);
		kfree(hdcp->keys);
		hdcp->keys = NULL;
		kfree(hdcp->seeds);
		hdcp->seeds = NULL;
		return -EINVAL;
	}
	memcpy(hdcp->keys, hdcp_vendor_data, HDCP_KEY_SIZE);
	memcpy(hdcp->seeds, hdcp_vendor_data + HDCP_KEY_SIZE,
	       HDCP_KEY_SEED_SIZE);

	return 0;
}

static int rk628_hdmi_hdcp_load_key(struct rk628 *rk628, struct rk628_hdcp *hdcp)
{
	int i;
	int ret;
	struct hdcp_keys *hdcp_keys;
	u32 seeds = 0;

	if (!hdcp->keys) {
		ret = hdcp_load_keys_cb(rk628, hdcp);
		if (ret) {
			dev_err(rk628->dev, "HDCP: load key failed\n");
			return ret;
		}
	}
	hdcp_keys = hdcp->keys;

	rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_CTRL,
			HDCP_ENABLE_MASK |
			HDCP_ENC_EN_MASK,
			HDCP_ENABLE(0) |
			HDCP_ENC_EN(0));
	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0,
			SW_ADAPTER_I2CSLADR_MASK |
			SW_EFUSE_HDCP_EN_MASK,
			SW_ADAPTER_I2CSLADR(0) |
			SW_EFUSE_HDCP_EN(1));
	/* The useful data in ksv should be 5 byte */
	for (i = 0; i < KSV_LEN; i++)
		rk628_i2c_write(rk628, HDCP_KEY_KSV0 + i * 4,
					hdcp_keys->KSV[i]);

	for (i = 0; i < HDCP_PRIVATE_KEY_SIZE; i++)
		rk628_i2c_write(rk628, HDCP_KEY_DPK0 + i * 4,
				hdcp_keys->devicekey[i]);

	rk628_i2c_update_bits(rk628, GRF_SYSTEM_CON0,
			SW_ADAPTER_I2CSLADR_MASK |
			SW_EFUSE_HDCP_EN_MASK,
			SW_ADAPTER_I2CSLADR(0) |
			SW_EFUSE_HDCP_EN(0));
	rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_CTRL,
			HDCP_ENABLE_MASK |
			HDCP_ENC_EN_MASK,
			HDCP_ENABLE(1) |
			HDCP_ENC_EN(1));

	/* Enable decryption logic */
	if (hdcp->seeds) {
		seeds = (hdcp->seeds[0] & 0xff) << 8;
		seeds |= (hdcp->seeds[1] & 0xff);
	}
	if (seeds) {
		rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_CTRL,
				   KEY_DECRIPT_ENABLE_MASK,
				   KEY_DECRIPT_ENABLE(1));
		rk628_i2c_write(rk628, HDMI_RX_HDCP_SEED, seeds);
	} else {
		rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_CTRL,
				   KEY_DECRIPT_ENABLE_MASK,
				   KEY_DECRIPT_ENABLE(0));
	}

	return 0;
}

void rk628_hdmirx_set_hdcp(struct rk628 *rk628, struct rk628_hdcp *hdcp, bool en)
{
	dev_dbg(rk628->dev, "%s: %sable\n", __func__, en ? "en" : "dis");

	if (en) {
		rk628_hdmi_hdcp_load_key(rk628, hdcp);
	} else {
		rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_CTRL,
				      HDCP_ENABLE_MASK |
				      HDCP_ENC_EN_MASK,
				      HDCP_ENABLE(0) |
				      HDCP_ENC_EN(0));
	}
}
EXPORT_SYMBOL(rk628_hdmirx_set_hdcp);

void rk628_hdmirx_controller_setup(struct rk628 *rk628)
{
	rk628_i2c_write(rk628, HDMI_RX_HDMI20_CONTROL, 0x10000f10);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_MODE_RECOVER, 0x00000021);
	rk628_i2c_write(rk628, HDMI_RX_PDEC_CTRL, 0xbfff8011);
	rk628_i2c_write(rk628, HDMI_RX_PDEC_ASP_CTRL, 0x00000040);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_RESMPL_CTRL, 0x00000001);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_SYNC_CTRL, 0x00000014);
	rk628_i2c_write(rk628, HDMI_RX_PDEC_ERR_FILTER, 0x00000008);
	rk628_i2c_write(rk628, HDMI_RX_SCDC_I2CCONFIG, 0x01000000);
	rk628_i2c_write(rk628, HDMI_RX_SCDC_CONFIG, 0x00000001);
	rk628_i2c_write(rk628, HDMI_RX_SCDC_WRDATA0, 0xabcdef01);
	rk628_i2c_write(rk628, HDMI_RX_CHLOCK_CONFIG, 0x0030c15c);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_ERROR_PROTECT, 0x000d0c98);
	rk628_i2c_write(rk628, HDMI_RX_MD_HCTRL1, 0x00000010);
	rk628_i2c_write(rk628, HDMI_RX_MD_HCTRL2, 0x00001738);
	rk628_i2c_write(rk628, HDMI_RX_MD_VCTRL, 0x00000002);
	rk628_i2c_write(rk628, HDMI_RX_MD_VTH, 0x0000073a);
	rk628_i2c_write(rk628, HDMI_RX_MD_IL_POL, 0x00000004);
	rk628_i2c_write(rk628, HDMI_RX_PDEC_ACRM_CTRL, 0x00000000);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_DCM_CTRL, 0x00040414);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_CKM_EVLTM, 0x00103e70);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_CKM_F, 0x0c1c0b54);
	rk628_i2c_write(rk628, HDMI_RX_HDMI_RESMPL_CTRL, 0x00000001);

	rk628_i2c_update_bits(rk628, HDMI_RX_HDCP_SETTINGS,
			      HDMI_RESERVED_MASK |
			      FAST_I2C_MASK |
			      ONE_DOT_ONE_MASK |
			      FAST_REAUTH_MASK,
			      HDMI_RESERVED(1) |
			      FAST_I2C(0) |
			      ONE_DOT_ONE(0) |
			      FAST_REAUTH(0));
}
EXPORT_SYMBOL(rk628_hdmirx_controller_setup);

static void rk628_hdmirx_audio_fifo_init(struct rk628_audioinfo *aif)
{

	dev_dbg(aif->dev, "%s initial fifo\n", __func__);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_ICLR, 0x1f);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10001);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10000);
	aif->audio_state.pre_state = aif->audio_state.init_state = INIT_FIFO_STATE*4;
}

static void rk628_hdmirx_audio_fifo_initd(struct rk628_audioinfo *aif)
{

	dev_dbg(aif->dev, "%s double initial fifo\n", __func__);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_ICLR, 0x1f);
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_FIFO_TH,
			   AFIF_TH_START_MASK,
			   AFIF_TH_START(192));
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10001);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10000);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10001);
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_CTRL, 0x10000);
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_FIFO_TH,
			   AFIF_TH_START_MASK,
			   AFIF_TH_START(INIT_FIFO_STATE));
	aif->audio_state.pre_state = aif->audio_state.init_state = INIT_FIFO_STATE*4;
}

static u32 _rk628_hdmirx_audio_fs(struct rk628_audioinfo *aif)
{
	u64 tmdsclk = 0;
	u32 clkrate = 0, cts_decoded = 0, n_decoded = 0, fs_audio = 0;

	/* fout=128*fs=ftmds*N/CTS */
	rk628_i2c_read(aif->rk628, HDMI_RX_HDMI_CKM_RESULT, &clkrate);
	clkrate = clkrate & 0xffff;
	/* tmdsclk = (clkrate/1000) * 49500000 */
	tmdsclk = clkrate * (49500000 / 1000);
	rk628_i2c_read(aif->rk628, HDMI_RX_PDEC_ACR_CTS, &cts_decoded);
	rk628_i2c_read(aif->rk628, HDMI_RX_PDEC_ACR_N, &n_decoded);
	if (cts_decoded != 0) {
		fs_audio = div_u64((tmdsclk * n_decoded), cts_decoded);
		fs_audio /= 128;
		fs_audio = div_u64(fs_audio + 50, 100);
		fs_audio *= 100;
	}
	dev_dbg(aif->dev,
		"%s: clkrate:%u tmdsclk:%llu, n_decoded:%u, cts_decoded:%u, fs_audio:%u\n",
		__func__, clkrate, tmdsclk, n_decoded, cts_decoded, fs_audio);
	if (!is_validfs(fs_audio))
		fs_audio = 0;
	return fs_audio;
}

static void rk628_hdmirx_audio_clk_set_rate(struct rk628_audioinfo *aif, u32 rate)
{

	dev_dbg(aif->dev, "%s: %u to %u\n",
		 __func__, aif->audio_state.hdmirx_aud_clkrate, rate);
	rk628_clk_set_rate(aif->rk628, CGU_CLK_HDMIRX_AUD, rate);
	aif->audio_state.hdmirx_aud_clkrate = rate;
}

static void rk628_hdmirx_audio_clk_inc_rate(struct rk628_audioinfo *aif, int dis)
{
	u32 hdmirx_aud_clkrate = aif->audio_state.hdmirx_aud_clkrate + dis;

	dev_dbg(aif->dev, "%s: %u to %u\n",
		 __func__, aif->audio_state.hdmirx_aud_clkrate, hdmirx_aud_clkrate);
	rk628_clk_set_rate(aif->rk628, CGU_CLK_HDMIRX_AUD, hdmirx_aud_clkrate);
	aif->audio_state.hdmirx_aud_clkrate = hdmirx_aud_clkrate;
}

static void rk628_hdmirx_audio_set_fs(struct rk628_audioinfo *aif, u32 fs_audio)
{
	u32 hdmirx_aud_clkrate_t = fs_audio*128;

	dev_dbg(aif->dev, "%s: %u to %u with fs %u\n", __func__,
		 aif->audio_state.hdmirx_aud_clkrate, hdmirx_aud_clkrate_t,
		 fs_audio);
	rk628_clk_set_rate(aif->rk628, CGU_CLK_HDMIRX_AUD, hdmirx_aud_clkrate_t);
	aif->audio_state.hdmirx_aud_clkrate = hdmirx_aud_clkrate_t;
	aif->audio_state.fs_audio = fs_audio;
}

static void rk628_hdmirx_audio_enable(struct rk628_audioinfo *aif)
{
	u32 fifo_ints;

	rk628_i2c_read(aif->rk628, HDMI_RX_AUD_FIFO_ISTS, &fifo_ints);
	dev_dbg(aif->dev, "%s fifo ints %#x\n", __func__, fifo_ints);
	if ((fifo_ints & 0x18) == 0x18)
		rk628_hdmirx_audio_fifo_initd(aif);
	else if (fifo_ints & 0x18)
		rk628_hdmirx_audio_fifo_init(aif);
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_DMI_DISABLE_IF,
			      AUD_ENABLE_MASK, AUD_ENABLE(1));
	aif->audio_state.audio_enable = true;
	aif->fifo_ints_en = true;
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_IEN_SET,
			AFIF_OVERFL_ISTS | AFIF_UNDERFL_ISTS);
}

static void rk628_csi_delayed_work_audio(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk628_audioinfo *aif = container_of(dwork, struct rk628_audioinfo,
						   delayed_work_audio);
	struct rk628_audiostate *audio_state = &aif->audio_state;
	u32 fs_audio;
	int cur_state, init_state, pre_state;

	init_state = audio_state->init_state;
	pre_state = audio_state->pre_state;
	fs_audio = _rk628_hdmirx_audio_fs(aif);
	if (!is_validfs(fs_audio)) {
		dev_dbg(aif->dev, "%s: no supported fs(%u)\n", __func__, fs_audio);
		goto exit;
	}
	if (!audio_state->audio_enable) {
		rk628_hdmirx_audio_set_fs(aif, fs_audio);
		rk628_hdmirx_audio_enable(aif);
		goto exit;
	}
	if (abs(fs_audio - audio_state->fs_audio) > 1000)
		rk628_hdmirx_audio_set_fs(aif, fs_audio);
	rk628_i2c_read(aif->rk628, HDMI_RX_AUD_FIFO_FILLSTS1, &cur_state);
	dev_dbg(aif->dev, "%s: HDMI_RX_AUD_FIFO_FILLSTS1:%#x, single offset:%d, total offset:%d\n",
		 __func__, cur_state, cur_state - pre_state, cur_state - init_state);
	if (cur_state != 0)
		aif->audio_present = true;
	else
		aif->audio_present = false;

	if ((cur_state - init_state) > 16 && (cur_state - pre_state) > 0)
		rk628_hdmirx_audio_clk_inc_rate(aif, 10);
	else if ((cur_state != 0) && (cur_state - init_state) < -16 && (cur_state - pre_state) < 0)
		rk628_hdmirx_audio_clk_inc_rate(aif, -10);
	audio_state->pre_state = cur_state;
exit:
	schedule_delayed_work(&aif->delayed_work_audio, msecs_to_jiffies(1000));

}

static void rk628_csi_delayed_work_audio_rate_change(struct work_struct *work)
{
	u32 fifo_fillsts;
	u32 fs_audio;
	struct delayed_work *dwork = to_delayed_work(work);
	struct rk628_audioinfo *aif = container_of(dwork, struct rk628_audioinfo,
						   delayed_work_audio_rate_change);

	mutex_lock(aif->confctl_mutex);
	fs_audio = _rk628_hdmirx_audio_fs(aif);
	dev_dbg(aif->dev, "%s get audio fs %u\n", __func__, fs_audio);
	if (aif->audio_state.ctsn_flag == (ACR_N_CHG_ICLR | ACR_CTS_CHG_ICLR)) {
		aif->audio_state.ctsn_flag = 0;
		if (is_validfs(fs_audio)) {
			rk628_hdmirx_audio_set_fs(aif, fs_audio);
			/* We start audio work after recieveing cts n interrupt */
			rk628_hdmirx_audio_enable(aif);
		} else {
			dev_dbg(aif->dev, "%s invalid fs when ctsn updating\n", __func__);
		}
		schedule_delayed_work(&aif->delayed_work_audio, msecs_to_jiffies(1000));
	}
	if (aif->audio_state.fifo_int) {
		aif->audio_state.fifo_int = false;
		if (is_validfs(fs_audio))
			rk628_hdmirx_audio_set_fs(aif, fs_audio);
		rk628_i2c_read(aif->rk628, HDMI_RX_AUD_FIFO_FILLSTS1, &fifo_fillsts);
		if (!fifo_fillsts) {
			dev_dbg(aif->dev, "%s underflow after overflow\n", __func__);
			rk628_hdmirx_audio_fifo_initd(aif);
		} else {
			dev_dbg(aif->dev, "%s overflow after underflow\n", __func__);
			rk628_hdmirx_audio_fifo_initd(aif);
		}
	}
	mutex_unlock(aif->confctl_mutex);
}

HAUDINFO rk628_hdmirx_audioinfo_alloc(struct device *dev,
				      struct mutex *confctl_mutex,
				      struct rk628 *rk628,
				      bool en)
{
	struct rk628_audioinfo *aif;

	aif = devm_kzalloc(dev, sizeof(*aif), GFP_KERNEL);
	if (!aif)
		return NULL;
	INIT_DELAYED_WORK(&aif->delayed_work_audio_rate_change,
			  rk628_csi_delayed_work_audio_rate_change);
	INIT_DELAYED_WORK(&aif->delayed_work_audio,
			  rk628_csi_delayed_work_audio);
	aif->confctl_mutex = confctl_mutex;
	aif->rk628 = rk628;
	aif->i2s_enabled_default = en;
	aif->dev = dev;
	return aif;
}
EXPORT_SYMBOL(rk628_hdmirx_audioinfo_alloc);

void rk628_hdmirx_audio_cancel_work_audio(HAUDINFO info, bool sync)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (sync)
		cancel_delayed_work_sync(&aif->delayed_work_audio);
	else
		cancel_delayed_work(&aif->delayed_work_audio);
}
EXPORT_SYMBOL(rk628_hdmirx_audio_cancel_work_audio);

void rk628_hdmirx_audio_cancel_work_rate_change(HAUDINFO info, bool sync)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (sync)
		cancel_delayed_work_sync(&aif->delayed_work_audio_rate_change);
	else
		cancel_delayed_work(&aif->delayed_work_audio_rate_change);
}
EXPORT_SYMBOL(rk628_hdmirx_audio_cancel_work_rate_change);

void rk628_hdmirx_audio_destroy(HAUDINFO info)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (!aif)
		return;
	rk628_hdmirx_audio_cancel_work_audio(aif, true);
	rk628_hdmirx_audio_cancel_work_rate_change(aif, true);
	aif->confctl_mutex = NULL;
	aif->rk628 = NULL;
}
EXPORT_SYMBOL(rk628_hdmirx_audio_destroy);

bool rk628_hdmirx_audio_present(HAUDINFO info)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (!aif)
		return false;
	return aif->audio_present;
}
EXPORT_SYMBOL(rk628_hdmirx_audio_present);

int rk628_hdmirx_audio_fs(HAUDINFO info)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (!aif)
		return 0;
	return aif->audio_state.fs_audio;
}
EXPORT_SYMBOL(rk628_hdmirx_audio_fs);

void rk628_hdmirx_audio_i2s_ctrl(HAUDINFO info, bool enable)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;

	if (enable == aif->i2s_enabled)
		return;
	if (enable) {
		rk628_i2c_write(aif->rk628, HDMI_RX_AUD_SAO_CTRL,
				I2S_LPCM_BPCUV(0) |
				I2S_32_16(1));
	} else {
		rk628_i2c_write(aif->rk628, HDMI_RX_AUD_SAO_CTRL,
				I2S_LPCM_BPCUV(0) |
				I2S_32_16(1) |
				I2S_ENABLE_BITS(0x3f));
	}
	aif->i2s_enabled = enable;
}
EXPORT_SYMBOL(rk628_hdmirx_audio_i2s_ctrl);

void rk628_hdmirx_audio_setup(HAUDINFO info)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;
	u32 audio_pll_n, audio_pll_cts;

	dev_dbg(aif->dev, "%s: setup audio\n", __func__);
	audio_pll_n = 5644;
	audio_pll_cts = 148500;
	aif->audio_state.ctsn_flag = 0;
	aif->audio_state.fs_audio = 0;
	aif->audio_state.pre_state = 0;
	aif->audio_state.init_state = INIT_FIFO_STATE*4;
	aif->audio_state.fifo_int = false;
	aif->audio_state.audio_enable = false;
	aif->fifo_ints_en = false;
	aif->ctsn_ints_en = false;
	aif->i2s_enabled = false;

	rk628_hdmirx_audio_clk_set_rate(aif, 5644800);
	/* manual aud CTS */
	rk628_i2c_write(aif->rk628, HDMI_RX_AUDPLL_GEN_CTS, audio_pll_cts);
	/* manual aud N */
	rk628_i2c_write(aif->rk628, HDMI_RX_AUDPLL_GEN_N, audio_pll_n);

	/* aud CTS N en manual */
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_CLK_CTRL,
			CTS_N_REF_MASK, CTS_N_REF(1));
	/* aud pll ctrl */
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_PLL_CTRL,
			PLL_LOCK_TOGGLE_DIV_MASK, PLL_LOCK_TOGGLE_DIV(0));
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_FIFO_TH,
		AFIF_TH_START_MASK |
		AFIF_TH_MAX_MASK |
		AFIF_TH_MIN_MASK,
		AFIF_TH_START(64) |
		AFIF_TH_MAX(8) |
		AFIF_TH_MIN(8));

	/* AUTO_VMUTE */
	rk628_i2c_update_bits(aif->rk628, HDMI_RX_AUD_FIFO_CTRL,
			AFIF_SUBPACKET_DESEL_MASK |
			AFIF_SUBPACKETS_MASK,
			AFIF_SUBPACKET_DESEL(0) |
			AFIF_SUBPACKETS(1));
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_SAO_CTRL,
			I2S_LPCM_BPCUV(0) |
			I2S_32_16(1)|
			(aif->i2s_enabled_default ? 0 : I2S_ENABLE_BITS(0x3f)));
	aif->i2s_enabled = aif->i2s_enabled_default;
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_MUTE_CTRL,
			APPLY_INT_MUTE(0)	|
			APORT_SHDW_CTRL(3)	|
			AUTO_ACLK_MUTE(2)	|
			AUD_MUTE_SPEED(1)	|
			AUD_AVMUTE_EN(1)	|
			AUD_MUTE_SEL(0)		|
			AUD_MUTE_MODE(1));

	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_PAO_CTRL,
			PAO_RATE(0));
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_CHEXTR_CTRL,
			AUD_LAYOUT_CTRL(1));
	aif->ctsn_ints_en = true;
	rk628_i2c_write(aif->rk628, HDMI_RX_PDEC_IEN_SET, ACR_N_CHG_ICLR | ACR_CTS_CHG_ICLR);
	/* audio detect */
	rk628_i2c_write(aif->rk628, HDMI_RX_PDEC_AUDIODET_CTRL,
			AUDIODET_THRESHOLD(0));
}
EXPORT_SYMBOL(rk628_hdmirx_audio_setup);

bool rk628_audio_fifoints_enabled(HAUDINFO info)
{
	return ((struct rk628_audioinfo *)info)->fifo_ints_en;
}
EXPORT_SYMBOL(rk628_audio_fifoints_enabled);

bool rk628_audio_ctsnints_enabled(HAUDINFO info)
{
	return ((struct rk628_audioinfo *)info)->ctsn_ints_en;
}
EXPORT_SYMBOL(rk628_audio_ctsnints_enabled);

void rk628_csi_isr_ctsn(HAUDINFO info, u32 pdec_ints)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;
	u32 ctsn_mask = ACR_N_CHG_ICLR | ACR_CTS_CHG_ICLR;

	dev_dbg(aif->dev, "%s: pdec_ints:%#x\n", __func__, pdec_ints);
	/* cts & n both need update but maybe come diff int */
	if (pdec_ints & ACR_N_CHG_ICLR)
		aif->audio_state.ctsn_flag |= ACR_N_CHG_ICLR;
	if (pdec_ints & ACR_CTS_CHG_ICLR)
		aif->audio_state.ctsn_flag |= ACR_CTS_CHG_ICLR;
	if (aif->audio_state.ctsn_flag == ctsn_mask) {
		dev_dbg(aif->dev, "%s: ctsn updated, disable ctsn int\n", __func__);
		rk628_i2c_write(aif->rk628, HDMI_RX_PDEC_IEN_CLR, ctsn_mask);
		aif->ctsn_ints_en = false;
		schedule_delayed_work(&aif->delayed_work_audio_rate_change, 0);
	}
	rk628_i2c_write(aif->rk628, HDMI_RX_PDEC_ICLR, pdec_ints & ctsn_mask);
}
EXPORT_SYMBOL(rk628_csi_isr_ctsn);

void rk628_csi_isr_fifoints(HAUDINFO info, u32 fifo_ints)
{
	struct rk628_audioinfo *aif = (struct rk628_audioinfo *)info;
	u32 fifo_mask = AFIF_OVERFL_ISTS | AFIF_UNDERFL_ISTS;

	dev_dbg(aif->dev, "%s: fifo_ints:%#x\n", __func__, fifo_ints);
	/* cts & n both need update but maybe come diff int */
	if (fifo_ints & AFIF_OVERFL_ISTS) {
		dev_dbg(aif->dev, "%s: Audio FIFO overflow\n", __func__);
		aif->audio_state.fifo_flag |= AFIF_OVERFL_ISTS;
	}
	if (fifo_ints & AFIF_UNDERFL_ISTS) {
		dev_dbg(aif->dev, "%s: Audio FIFO underflow\n", __func__);
		aif->audio_state.fifo_flag |= AFIF_UNDERFL_ISTS;
	}
	if (aif->audio_state.fifo_flag == fifo_mask) {
		aif->audio_state.fifo_int = true;
		aif->audio_state.fifo_flag = 0;
		schedule_delayed_work(&aif->delayed_work_audio_rate_change, 0);
	}
	rk628_i2c_write(aif->rk628, HDMI_RX_AUD_FIFO_ICLR, fifo_ints & fifo_mask);
}
EXPORT_SYMBOL(rk628_csi_isr_fifoints);

int rk628_is_avi_ready(struct rk628 *rk628, bool avi_rcv_rdy)
{
	u8 i;
	u32 val, avi_pb = 0;
	u8 cnt = 0, max_cnt = 2;
	u32 hdcp_ctrl_val = 0;

	rk628_i2c_read(rk628, HDMI_RX_HDCP_CTRL, &val);
	if ((val & HDCP_ENABLE_MASK))
		max_cnt = 5;

	for (i = 0; i < 100; i++) {
		rk628_i2c_read(rk628, HDMI_RX_PDEC_AVI_PB, &val);
		dev_info(rk628->dev, "%s PDEC_AVI_PB:%#x, avi_rcv_rdy:%d\n",
			 __func__, val, avi_rcv_rdy);
		if (i > 30 && !(hdcp_ctrl_val & 0x400)) {
			rk628_i2c_read(rk628, HDMI_RX_HDCP_CTRL, &hdcp_ctrl_val);
			/* force hdcp avmute */
			hdcp_ctrl_val |= 0x400;
			rk628_i2c_write(rk628, HDMI_RX_HDCP_CTRL, hdcp_ctrl_val);
		}

		if (val && val == avi_pb && avi_rcv_rdy) {
			if (++cnt >= max_cnt)
				break;
		} else {
			cnt = 0;
			avi_pb = val;
		}
		msleep(30);
	}
	if (cnt < max_cnt)
		return 0;

	return 1;
}
EXPORT_SYMBOL(rk628_is_avi_ready);
