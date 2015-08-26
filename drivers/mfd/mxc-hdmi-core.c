/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.

 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <asm/mach-types.h>

#include <video/mxc_hdmi.h>
#include <linux/ipu-v3.h>
#include <video/mxc_edid.h>
#include "../mxc/ipu3/ipu_prv.h"
#include <linux/mfd/mxc-hdmi-core.h>
#include <linux/of_device.h>
#include <linux/mod_devicetable.h>
#include <linux/mfd/mxc-hdmi-core.h>

struct mxc_hdmi_data {
	struct platform_device *pdev;
	unsigned long __iomem *reg_base;
	unsigned long reg_phys_base;
	struct device *dev;
};

static void __iomem *hdmi_base;
static struct clk *isfr_clk;
static struct clk *iahb_clk;
static struct clk *mipi_core_clk;
static spinlock_t irq_spinlock;
static spinlock_t edid_spinlock;
static unsigned int sample_rate;
static unsigned long pixel_clk_rate;
static struct clk *pixel_clk;
static int hdmi_ratio;
int mxc_hdmi_ipu_id;
int mxc_hdmi_disp_id;
static struct mxc_edid_cfg hdmi_core_edid_cfg;
static int hdmi_core_init;
static unsigned int hdmi_dma_running;
static struct snd_pcm_substream *hdmi_audio_stream_playback;
static unsigned int hdmi_cable_state;
static unsigned int hdmi_blank_state;
static unsigned int hdmi_abort_state;
static spinlock_t hdmi_audio_lock, hdmi_blank_state_lock, hdmi_cable_state_lock;

unsigned int hdmi_set_cable_state(unsigned int state)
{
	unsigned long flags;
	struct snd_pcm_substream *substream = hdmi_audio_stream_playback;

	spin_lock_irqsave(&hdmi_cable_state_lock, flags);
	hdmi_cable_state = state;
	spin_unlock_irqrestore(&hdmi_cable_state_lock, flags);

#ifndef CONFIG_MFD_MXC_HDMI_ANDROID
	if (check_hdmi_state() && substream && hdmi_abort_state) {
		hdmi_abort_state = 0;
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_START);
	}
#endif
	return 0;
}
EXPORT_SYMBOL(hdmi_set_cable_state);

unsigned int hdmi_set_blank_state(unsigned int state)
{
	unsigned long flags;
	struct snd_pcm_substream *substream = hdmi_audio_stream_playback;

	spin_lock_irqsave(&hdmi_blank_state_lock, flags);
	hdmi_blank_state = state;
	spin_unlock_irqrestore(&hdmi_blank_state_lock, flags);

#ifndef CONFIG_MFD_MXC_HDMI_ANDROID
	if (check_hdmi_state() && substream && hdmi_abort_state) {
		hdmi_abort_state = 0;
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_START);
	}
#endif

	return 0;
}
EXPORT_SYMBOL(hdmi_set_blank_state);

static void hdmi_audio_abort_stream(struct snd_pcm_substream *substream)
{
	unsigned long flags;

	snd_pcm_stream_lock_irqsave(substream, flags);

#ifndef CONFIG_MFD_MXC_HDMI_ANDROID
	if (snd_pcm_running(substream)) {
		hdmi_abort_state = 1;
		substream->ops->trigger(substream, SNDRV_PCM_TRIGGER_STOP);
	}
#else
	if (snd_pcm_running(substream))
		snd_pcm_stop(substream, SNDRV_PCM_STATE_DISCONNECTED);
#endif

	snd_pcm_stream_unlock_irqrestore(substream, flags);
}

int mxc_hdmi_abort_stream(void)
{
	unsigned long flags;
	spin_lock_irqsave(&hdmi_audio_lock, flags);
	if (hdmi_audio_stream_playback)
		hdmi_audio_abort_stream(hdmi_audio_stream_playback);
	spin_unlock_irqrestore(&hdmi_audio_lock, flags);

	return 0;
}
EXPORT_SYMBOL(mxc_hdmi_abort_stream);

int check_hdmi_state(void)
{
	unsigned long flags1, flags2;
	unsigned int ret;

	spin_lock_irqsave(&hdmi_cable_state_lock, flags1);
	spin_lock_irqsave(&hdmi_blank_state_lock, flags2);

	ret = hdmi_cable_state && hdmi_blank_state;

	spin_unlock_irqrestore(&hdmi_blank_state_lock, flags2);
	spin_unlock_irqrestore(&hdmi_cable_state_lock, flags1);

	return ret;
}
EXPORT_SYMBOL(check_hdmi_state);

int mxc_hdmi_register_audio(struct snd_pcm_substream *substream)
{
	unsigned long flags, flags1;
	int ret = 0;

	snd_pcm_stream_lock_irqsave(substream, flags);

	if (substream && check_hdmi_state()) {
		spin_lock_irqsave(&hdmi_audio_lock, flags1);
		if (hdmi_audio_stream_playback) {
			pr_err("%s unconsist hdmi auido stream!\n", __func__);
			ret = -EINVAL;
		}
		hdmi_audio_stream_playback = substream;
		hdmi_abort_state = 0;
		spin_unlock_irqrestore(&hdmi_audio_lock, flags1);
	} else
		ret = -EINVAL;

	snd_pcm_stream_unlock_irqrestore(substream, flags);

	return ret;
}
EXPORT_SYMBOL(mxc_hdmi_register_audio);

void mxc_hdmi_unregister_audio(struct snd_pcm_substream *substream)
{
	unsigned long flags;

	spin_lock_irqsave(&hdmi_audio_lock, flags);
	hdmi_audio_stream_playback = NULL;
	hdmi_abort_state = 0;
	spin_unlock_irqrestore(&hdmi_audio_lock, flags);
}
EXPORT_SYMBOL(mxc_hdmi_unregister_audio);

u8 hdmi_readb(unsigned int reg)
{
	u8 value;

	value = __raw_readb(hdmi_base + reg);

	return value;
}
EXPORT_SYMBOL(hdmi_readb);

#ifdef DEBUG
static bool overflow_lo;
static bool overflow_hi;

bool hdmi_check_overflow(void)
{
	u8 val, lo, hi;

	val = hdmi_readb(HDMI_IH_FC_STAT2);
	lo = (val & HDMI_IH_FC_STAT2_LOW_PRIORITY_OVERFLOW) != 0;
	hi = (val & HDMI_IH_FC_STAT2_HIGH_PRIORITY_OVERFLOW) != 0;

	if ((lo != overflow_lo) || (hi != overflow_hi)) {
		pr_debug("%s LowPriority=%d HighPriority=%d  <=======================\n",
			__func__, lo, hi);
		overflow_lo = lo;
		overflow_hi = hi;
		return true;
	}
	return false;
}
#else
bool hdmi_check_overflow(void)
{
	return false;
}
#endif
EXPORT_SYMBOL(hdmi_check_overflow);

void hdmi_writeb(u8 value, unsigned int reg)
{
	hdmi_check_overflow();
	__raw_writeb(value, hdmi_base + reg);
	hdmi_check_overflow();
}
EXPORT_SYMBOL(hdmi_writeb);

void hdmi_mask_writeb(u8 data, unsigned int reg, u8 shift, u8 mask)
{
	u8 value = hdmi_readb(reg) & ~mask;
	value |= (data << shift) & mask;
	hdmi_writeb(value, reg);
}
EXPORT_SYMBOL(hdmi_mask_writeb);

unsigned int hdmi_read4(unsigned int reg)
{
	/* read a four byte address from registers */
	return (hdmi_readb(reg + 3) << 24) |
		(hdmi_readb(reg + 2) << 16) |
		(hdmi_readb(reg + 1) << 8) |
		hdmi_readb(reg);
}
EXPORT_SYMBOL(hdmi_read4);

void hdmi_write4(unsigned int value, unsigned int reg)
{
	/* write a four byte address to hdmi regs */
	hdmi_writeb(value & 0xff, reg);
	hdmi_writeb((value >> 8) & 0xff, reg + 1);
	hdmi_writeb((value >> 16) & 0xff, reg + 2);
	hdmi_writeb((value >> 24) & 0xff, reg + 3);
}
EXPORT_SYMBOL(hdmi_write4);

static void initialize_hdmi_ih_mutes(void)
{
	u8 ih_mute;

	/*
	 * Boot up defaults are:
	 * HDMI_IH_MUTE   = 0x03 (disabled)
	 * HDMI_IH_MUTE_* = 0x00 (enabled)
	 */

	/* Disable top level interrupt bits in HDMI block */
	ih_mute = hdmi_readb(HDMI_IH_MUTE) |
		  HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		  HDMI_IH_MUTE_MUTE_ALL_INTERRUPT;

	hdmi_writeb(ih_mute, HDMI_IH_MUTE);

	/* by default mask all interrupts */
	hdmi_writeb(0xff, HDMI_VP_MASK);
	hdmi_writeb(0xff, HDMI_FC_MASK0);
	hdmi_writeb(0xff, HDMI_FC_MASK1);
	hdmi_writeb(0xff, HDMI_FC_MASK2);
	hdmi_writeb(0xff, HDMI_PHY_MASK0);
	hdmi_writeb(0xff, HDMI_PHY_I2CM_INT_ADDR);
	hdmi_writeb(0xff, HDMI_PHY_I2CM_CTLINT_ADDR);
	hdmi_writeb(0xff, HDMI_AUD_INT);
	hdmi_writeb(0xff, HDMI_AUD_SPDIFINT);
	hdmi_writeb(0xff, HDMI_AUD_HBR_MASK);
	hdmi_writeb(0xff, HDMI_GP_MASK);
	hdmi_writeb(0xff, HDMI_A_APIINTMSK);
	hdmi_writeb(0xff, HDMI_CEC_MASK);
	hdmi_writeb(0xff, HDMI_I2CM_INT);
	hdmi_writeb(0xff, HDMI_I2CM_CTLINT);

	/* Disable interrupts in the IH_MUTE_* registers */
	hdmi_writeb(0xff, HDMI_IH_MUTE_FC_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_FC_STAT1);
	hdmi_writeb(0xff, HDMI_IH_MUTE_FC_STAT2);
	hdmi_writeb(0xff, HDMI_IH_MUTE_AS_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_PHY_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_I2CM_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_CEC_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_VP_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_I2CMPHY_STAT0);
	hdmi_writeb(0xff, HDMI_IH_MUTE_AHBDMAAUD_STAT0);

	/* Enable top level interrupt bits in HDMI block */
	ih_mute &= ~(HDMI_IH_MUTE_MUTE_WAKEUP_INTERRUPT |
		    HDMI_IH_MUTE_MUTE_ALL_INTERRUPT);
	hdmi_writeb(ih_mute, HDMI_IH_MUTE);
}

static void hdmi_set_clock_regenerator_n(unsigned int value)
{
	u8 val;

	if (!hdmi_dma_running) {
		hdmi_writeb(value & 0xff, HDMI_AUD_N1);
		hdmi_writeb(0, HDMI_AUD_N2);
		hdmi_writeb(0, HDMI_AUD_N3);
	}

	hdmi_writeb(value & 0xff, HDMI_AUD_N1);
	hdmi_writeb((value >> 8) & 0xff, HDMI_AUD_N2);
	hdmi_writeb((value >> 16) & 0x0f, HDMI_AUD_N3);

	/* nshift factor = 0 */
	val = hdmi_readb(HDMI_AUD_CTS3);
	val &= ~HDMI_AUD_CTS3_N_SHIFT_MASK;
	hdmi_writeb(val, HDMI_AUD_CTS3);
}

static void hdmi_set_clock_regenerator_cts(unsigned int cts)
{
	u8 val;

	if (!hdmi_dma_running) {
		hdmi_writeb(cts & 0xff, HDMI_AUD_CTS1);
		hdmi_writeb(0, HDMI_AUD_CTS2);
		hdmi_writeb(0, HDMI_AUD_CTS3);
	}

	/* Must be set/cleared first */
	val = hdmi_readb(HDMI_AUD_CTS3);
	val &= ~HDMI_AUD_CTS3_CTS_MANUAL;
	hdmi_writeb(val, HDMI_AUD_CTS3);

	hdmi_writeb(cts & 0xff, HDMI_AUD_CTS1);
	hdmi_writeb((cts >> 8) & 0xff, HDMI_AUD_CTS2);
	hdmi_writeb(((cts >> 16) & HDMI_AUD_CTS3_AUDCTS19_16_MASK) |
		    HDMI_AUD_CTS3_CTS_MANUAL, HDMI_AUD_CTS3);
}

static unsigned int hdmi_compute_n(unsigned int freq, unsigned long pixel_clk,
				   unsigned int ratio)
{
	unsigned int n = (128 * freq) / 1000;

	switch (freq) {
	case 32000:
		if (pixel_clk == 25174000)
			n = (ratio == 150) ? 9152 : 4576;
		else if (pixel_clk == 27020000)
			n = (ratio == 150) ? 8192 : 4096;
		else if (pixel_clk == 74170000 || pixel_clk == 148350000)
			n = 11648;
		else if (pixel_clk == 297000000)
			n = (ratio == 150) ? 6144 : 3072;
		else
			n = 4096;
		break;

	case 44100:
		if (pixel_clk == 25174000)
			n = 7007;
		else if (pixel_clk == 74170000)
			n = 17836;
		else if (pixel_clk == 148350000)
			n = (ratio == 150) ? 17836 : 8918;
		else if (pixel_clk == 297000000)
			n = (ratio == 150) ? 9408 : 4704;
		else
			n = 6272;
		break;

	case 48000:
		if (pixel_clk == 25174000)
			n = (ratio == 150) ? 9152 : 6864;
		else if (pixel_clk == 27020000)
			n = (ratio == 150) ? 8192 : 6144;
		else if (pixel_clk == 74170000)
			n = 11648;
		else if (pixel_clk == 148350000)
			n = (ratio == 150) ? 11648 : 5824;
		else if (pixel_clk == 297000000)
			n = (ratio == 150) ? 10240 : 5120;
		else
			n = 6144;
		break;

	case 88200:
		n = hdmi_compute_n(44100, pixel_clk, ratio) * 2;
		break;

	case 96000:
		n = hdmi_compute_n(48000, pixel_clk, ratio) * 2;
		break;

	case 176400:
		n = hdmi_compute_n(44100, pixel_clk, ratio) * 4;
		break;

	case 192000:
		n = hdmi_compute_n(48000, pixel_clk, ratio) * 4;
		break;

	default:
		break;
	}

	return n;
}

static unsigned int hdmi_compute_cts(unsigned int freq, unsigned long pixel_clk,
				     unsigned int ratio)
{
	unsigned int cts = 0;
	switch (freq) {
	case 32000:
		if (pixel_clk == 297000000) {
			cts = 222750;
			break;
		} else if (pixel_clk == 25174000) {
			cts = 28125;
			break;
		}
	case 48000:
	case 96000:
	case 192000:
		switch (pixel_clk) {
		case 25200000:
		case 27000000:
		case 54000000:
		case 74250000:
		case 148500000:
			cts = pixel_clk / 1000;
			break;
		case 297000000:
			cts = 247500;
			break;
		case 25174000:
			cts = 28125l;
			break;
		/*
		 * All other TMDS clocks are not supported by
		 * DWC_hdmi_tx. The TMDS clocks divided or
		 * multiplied by 1,001 coefficients are not
		 * supported.
		 */
		default:
			break;
		}
		break;
	case 44100:
	case 88200:
	case 176400:
		switch (pixel_clk) {
		case 25200000:
			cts = 28000;
			break;
		case 25174000:
			cts = 31250;
			break;
		case 27000000:
			cts = 30000;
			break;
		case 54000000:
			cts = 60000;
			break;
		case 74250000:
			cts = 82500;
			break;
		case 148500000:
			cts = 165000;
			break;
		case 297000000:
			cts = 247500;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
	if (ratio == 100)
		return cts;
	else
		return (cts * ratio) / 100;
}

static void hdmi_set_clk_regenerator(void)
{
	unsigned int clk_n, clk_cts;

	clk_n = hdmi_compute_n(sample_rate, pixel_clk_rate, hdmi_ratio);
	clk_cts = hdmi_compute_cts(sample_rate, pixel_clk_rate, hdmi_ratio);

	if (clk_cts == 0) {
		pr_debug("%s: pixel clock not supported: %d\n",
			__func__, (int)pixel_clk_rate);
		return;
	}

	pr_debug("%s: samplerate=%d  ratio=%d  pixelclk=%d  N=%d  cts=%d\n",
		__func__, sample_rate, hdmi_ratio, (int)pixel_clk_rate,
		clk_n, clk_cts);

	hdmi_set_clock_regenerator_cts(clk_cts);
	hdmi_set_clock_regenerator_n(clk_n);
}

static int hdmi_core_get_of_property(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err;
	int ipu_id, disp_id;

	err = of_property_read_u32(np, "ipu_id", &ipu_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property ipu_id fail\n");
		return err;
	}
	err = of_property_read_u32(np, "disp_id", &disp_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property disp_id fail\n");
		return err;
	}

	mxc_hdmi_ipu_id = ipu_id;
	mxc_hdmi_disp_id = disp_id;

	return err;
}

/* Need to run this before phy is enabled the first time to prevent
 * overflow condition in HDMI_IH_FC_STAT2 */
void hdmi_init_clk_regenerator(void)
{
	if (pixel_clk_rate == 0) {
		pixel_clk_rate = 74250000;
		hdmi_set_clk_regenerator();
	}
}
EXPORT_SYMBOL(hdmi_init_clk_regenerator);

void hdmi_clk_regenerator_update_pixel_clock(u32 pixclock)
{

	if (!pixclock)
		return;
	/* Translate pixel clock in ps (pico seconds) to Hz  */
	pixel_clk_rate = PICOS2KHZ(pixclock) * 1000UL;
	hdmi_set_clk_regenerator();
}
EXPORT_SYMBOL(hdmi_clk_regenerator_update_pixel_clock);

void hdmi_set_dma_mode(unsigned int dma_running)
{
	hdmi_dma_running = dma_running;
	hdmi_set_clk_regenerator();
}
EXPORT_SYMBOL(hdmi_set_dma_mode);

void hdmi_set_sample_rate(unsigned int rate)
{
	sample_rate = rate;
}
EXPORT_SYMBOL(hdmi_set_sample_rate);

void hdmi_set_edid_cfg(struct mxc_edid_cfg *cfg)
{
	unsigned long flags;

	spin_lock_irqsave(&edid_spinlock, flags);
	memcpy(&hdmi_core_edid_cfg, cfg, sizeof(struct mxc_edid_cfg));
	spin_unlock_irqrestore(&edid_spinlock, flags);
}
EXPORT_SYMBOL(hdmi_set_edid_cfg);

void hdmi_get_edid_cfg(struct mxc_edid_cfg *cfg)
{
	unsigned long flags;

	spin_lock_irqsave(&edid_spinlock, flags);
	memcpy(cfg, &hdmi_core_edid_cfg, sizeof(struct mxc_edid_cfg));
	spin_unlock_irqrestore(&edid_spinlock, flags);
}
EXPORT_SYMBOL(hdmi_get_edid_cfg);

void hdmi_set_registered(int registered)
{
	hdmi_core_init = registered;
}
EXPORT_SYMBOL(hdmi_set_registered);

int hdmi_get_registered(void)
{
	return hdmi_core_init;
}
EXPORT_SYMBOL(hdmi_get_registered);

static int mxc_hdmi_core_probe(struct platform_device *pdev)
{
	struct mxc_hdmi_data *hdmi_data;
	struct resource *res;
	unsigned long flags;
	int ret = 0;

#ifdef DEBUG
	overflow_lo = false;
	overflow_hi = false;
#endif

	hdmi_core_init = 0;
	hdmi_dma_running = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	ret = hdmi_core_get_of_property(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "get hdmi of property fail\n");
		return -ENOENT;
	}

	hdmi_data = devm_kzalloc(&pdev->dev, sizeof(struct mxc_hdmi_data), GFP_KERNEL);
	if (!hdmi_data) {
		dev_err(&pdev->dev, "Couldn't allocate mxc hdmi mfd device\n");
		return -ENOMEM;
	}
	hdmi_data->pdev = pdev;

	pixel_clk = NULL;
	sample_rate = 48000;
	pixel_clk_rate = 0;
	hdmi_ratio = 100;

	spin_lock_init(&irq_spinlock);
	spin_lock_init(&edid_spinlock);


	spin_lock_init(&hdmi_cable_state_lock);
	spin_lock_init(&hdmi_blank_state_lock);
	spin_lock_init(&hdmi_audio_lock);

	spin_lock_irqsave(&hdmi_cable_state_lock, flags);
	hdmi_cable_state = 0;
	spin_unlock_irqrestore(&hdmi_cable_state_lock, flags);

	spin_lock_irqsave(&hdmi_blank_state_lock, flags);
	hdmi_blank_state = 0;
	spin_unlock_irqrestore(&hdmi_blank_state_lock, flags);

	spin_lock_irqsave(&hdmi_audio_lock, flags);
	hdmi_audio_stream_playback = NULL;
	hdmi_abort_state = 0;
	spin_unlock_irqrestore(&hdmi_audio_lock, flags);

	mipi_core_clk = clk_get(&hdmi_data->pdev->dev, "mipi_core");
	if (IS_ERR(mipi_core_clk)) {
		ret = PTR_ERR(mipi_core_clk);
		dev_err(&hdmi_data->pdev->dev,
			"Unable to get mipi core clk: %d\n", ret);
		goto eclkg;
	}

	ret = clk_prepare_enable(mipi_core_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot enable mipi core clock: %d\n", ret);
		goto eclke;
	}

	isfr_clk = clk_get(&hdmi_data->pdev->dev, "hdmi_isfr");
	if (IS_ERR(isfr_clk)) {
		ret = PTR_ERR(isfr_clk);
		dev_err(&hdmi_data->pdev->dev,
			"Unable to get HDMI isfr clk: %d\n", ret);
		goto eclkg1;
	}

	ret = clk_prepare_enable(isfr_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot enable HDMI clock: %d\n", ret);
		goto eclke1;
	}

	pr_debug("%s isfr_clk:%d\n", __func__,
		(int)clk_get_rate(isfr_clk));

	iahb_clk = clk_get(&hdmi_data->pdev->dev, "hdmi_iahb");
	if (IS_ERR(iahb_clk)) {
		ret = PTR_ERR(iahb_clk);
		dev_err(&hdmi_data->pdev->dev,
			"Unable to get HDMI iahb clk: %d\n", ret);
		goto eclkg2;
	}

	ret = clk_prepare_enable(iahb_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "Cannot enable HDMI clock: %d\n", ret);
		goto eclke2;
	}

	hdmi_data->reg_phys_base = res->start;
	if (!request_mem_region(res->start, resource_size(res),
				dev_name(&pdev->dev))) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto emem;
	}

	hdmi_data->reg_base = ioremap(res->start, resource_size(res));
	if (!hdmi_data->reg_base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto eirq;
	}
	hdmi_base = hdmi_data->reg_base;

	pr_debug("\n%s hdmi hw base = 0x%08x\n\n", __func__, (int)res->start);

	initialize_hdmi_ih_mutes();

	/* Disable HDMI clocks until video/audio sub-drivers are initialized */
	clk_disable_unprepare(isfr_clk);
	clk_disable_unprepare(iahb_clk);
	clk_disable_unprepare(mipi_core_clk);

	/* Replace platform data coming in with a local struct */
	platform_set_drvdata(pdev, hdmi_data);

	return ret;

eirq:
	release_mem_region(res->start, resource_size(res));
emem:
	clk_disable_unprepare(iahb_clk);
eclke2:
	clk_put(iahb_clk);
eclkg2:
	clk_disable_unprepare(isfr_clk);
eclke1:
	clk_put(isfr_clk);
eclkg1:
	clk_disable_unprepare(mipi_core_clk);
eclke:
	clk_put(mipi_core_clk);
eclkg:
	return ret;
}


static int __exit mxc_hdmi_core_remove(struct platform_device *pdev)
{
	struct mxc_hdmi_data *hdmi_data = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	iounmap(hdmi_data->reg_base);
	release_mem_region(res->start, resource_size(res));

	return 0;
}

static const struct of_device_id imx_hdmi_dt_ids[] = {
	{ .compatible = "fsl,imx6q-hdmi-core", },
	{ .compatible = "fsl,imx6dl-hdmi-core", },
	{ /* sentinel */ }
};

static struct platform_driver mxc_hdmi_core_driver = {
	.driver = {
		.name = "mxc_hdmi_core",
		.of_match_table	= imx_hdmi_dt_ids,
		.owner = THIS_MODULE,
	},
	.remove = __exit_p(mxc_hdmi_core_remove),
};

static int __init mxc_hdmi_core_init(void)
{
	return platform_driver_probe(&mxc_hdmi_core_driver,
				     mxc_hdmi_core_probe);
}

static void __exit mxc_hdmi_core_exit(void)
{
	platform_driver_unregister(&mxc_hdmi_core_driver);
}

subsys_initcall(mxc_hdmi_core_init);
module_exit(mxc_hdmi_core_exit);

MODULE_DESCRIPTION("Core driver for Freescale i.Mx on-chip HDMI");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
