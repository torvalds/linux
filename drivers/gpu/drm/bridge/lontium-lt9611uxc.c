// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 */

#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <sound/hdmi-codec.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define EDID_BLOCK_SIZE	128
#define EDID_NUM_BLOCKS	2

#define FW_FILE "lt9611uxc_fw.bin"

struct lt9611uxc {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;

	struct wait_queue_head wq;
	struct work_struct work;

	struct device_node *dsi0_node;
	struct device_node *dsi1_node;
	struct mipi_dsi_device *dsi0;
	struct mipi_dsi_device *dsi1;
	struct platform_device *audio_pdev;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	struct regulator_bulk_data supplies[2];

	struct i2c_client *client;

	bool hpd_supported;
	bool edid_read;
	/* can be accessed from different threads, so protect this with ocm_lock */
	bool hdmi_connected;
	uint8_t fw_version;
};

#define LT9611_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611uxc_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0xd0ff,
		.selector_reg = LT9611_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611uxc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611uxc_ranges,
	.num_ranges = ARRAY_SIZE(lt9611uxc_ranges),
};

struct lt9611uxc_mode {
	u16 hdisplay;
	u16 vdisplay;
	u8 vrefresh;
};

/*
 * This chip supports only a fixed set of modes.
 * Enumerate them here to check whether the mode is supported.
 */
static struct lt9611uxc_mode lt9611uxc_modes[] = {
	{ 1920, 1080, 60 },
	{ 1920, 1080, 30 },
	{ 1920, 1080, 25 },
	{ 1366, 768, 60 },
	{ 1360, 768, 60 },
	{ 1280, 1024, 60 },
	{ 1280, 800, 60 },
	{ 1280, 720, 60 },
	{ 1280, 720, 50 },
	{ 1280, 720, 30 },
	{ 1152, 864, 60 },
	{ 1024, 768, 60 },
	{ 800, 600, 60 },
	{ 720, 576, 50 },
	{ 720, 480, 60 },
	{ 640, 480, 60 },
};

static struct lt9611uxc *bridge_to_lt9611uxc(struct drm_bridge *bridge)
{
	return container_of(bridge, struct lt9611uxc, bridge);
}

static void lt9611uxc_lock(struct lt9611uxc *lt9611uxc)
{
	mutex_lock(&lt9611uxc->ocm_lock);
	regmap_write(lt9611uxc->regmap, 0x80ee, 0x01);
}

static void lt9611uxc_unlock(struct lt9611uxc *lt9611uxc)
{
	regmap_write(lt9611uxc->regmap, 0x80ee, 0x00);
	msleep(50);
	mutex_unlock(&lt9611uxc->ocm_lock);
}

static irqreturn_t lt9611uxc_irq_thread_handler(int irq, void *dev_id)
{
	struct lt9611uxc *lt9611uxc = dev_id;
	unsigned int irq_status = 0;
	unsigned int hpd_status = 0;

	lt9611uxc_lock(lt9611uxc);

	regmap_read(lt9611uxc->regmap, 0xb022, &irq_status);
	regmap_read(lt9611uxc->regmap, 0xb023, &hpd_status);
	if (irq_status)
		regmap_write(lt9611uxc->regmap, 0xb022, 0);

	if (irq_status & BIT(0)) {
		lt9611uxc->edid_read = !!(hpd_status & BIT(0));
		wake_up_all(&lt9611uxc->wq);
	}

	if (irq_status & BIT(1)) {
		lt9611uxc->hdmi_connected = hpd_status & BIT(1);
		schedule_work(&lt9611uxc->work);
	}

	lt9611uxc_unlock(lt9611uxc);

	return IRQ_HANDLED;
}

static void lt9611uxc_hpd_work(struct work_struct *work)
{
	struct lt9611uxc *lt9611uxc = container_of(work, struct lt9611uxc, work);
	bool connected;

	mutex_lock(&lt9611uxc->ocm_lock);
	connected = lt9611uxc->hdmi_connected;
	mutex_unlock(&lt9611uxc->ocm_lock);

	drm_bridge_hpd_notify(&lt9611uxc->bridge,
			      connected ?
			      connector_status_connected :
			      connector_status_disconnected);
}

static void lt9611uxc_reset(struct lt9611uxc *lt9611uxc)
{
	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 0);
	msleep(20);

	gpiod_set_value_cansleep(lt9611uxc->reset_gpio, 1);
	msleep(300);
}

static void lt9611uxc_assert_5v(struct lt9611uxc *lt9611uxc)
{
	if (!lt9611uxc->enable_gpio)
		return;

	gpiod_set_value_cansleep(lt9611uxc->enable_gpio, 1);
	msleep(20);
}

static int lt9611uxc_regulator_init(struct lt9611uxc *lt9611uxc)
{
	int ret;

	lt9611uxc->supplies[0].supply = "vdd";
	lt9611uxc->supplies[1].supply = "vcc";

	ret = devm_regulator_bulk_get(lt9611uxc->dev, 2, lt9611uxc->supplies);
	if (ret < 0)
		return ret;

	return regulator_set_load(lt9611uxc->supplies[0].consumer, 200000);
}

static int lt9611uxc_regulator_enable(struct lt9611uxc *lt9611uxc)
{
	int ret;

	ret = regulator_enable(lt9611uxc->supplies[0].consumer);
	if (ret < 0)
		return ret;

	usleep_range(1000, 10000); /* 50000 according to dtsi */

	ret = regulator_enable(lt9611uxc->supplies[1].consumer);
	if (ret < 0) {
		regulator_disable(lt9611uxc->supplies[0].consumer);
		return ret;
	}

	return 0;
}

static struct lt9611uxc_mode *lt9611uxc_find_mode(const struct drm_display_mode *mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(lt9611uxc_modes); i++) {
		if (lt9611uxc_modes[i].hdisplay == mode->hdisplay &&
		    lt9611uxc_modes[i].vdisplay == mode->vdisplay &&
		    lt9611uxc_modes[i].vrefresh == drm_mode_vrefresh(mode)) {
			return &lt9611uxc_modes[i];
		}
	}

	return NULL;
}

static struct mipi_dsi_device *lt9611uxc_attach_dsi(struct lt9611uxc *lt9611uxc,
						    struct device_node *dsi_node)
{
	const struct mipi_dsi_device_info info = { "lt9611uxc", 0, NULL };
	struct mipi_dsi_device *dsi;
	struct mipi_dsi_host *host;
	struct device *dev = lt9611uxc->dev;
	int ret;

	host = of_find_mipi_dsi_host_by_node(dsi_node);
	if (!host)
		return ERR_PTR(dev_err_probe(dev, -EPROBE_DEFER, "failed to find dsi host\n"));

	dsi = devm_mipi_dsi_device_register_full(dev, host, &info);
	if (IS_ERR(dsi)) {
		dev_err(dev, "failed to create dsi device\n");
		return dsi;
	}

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;

	ret = devm_mipi_dsi_attach(dev, dsi);
	if (ret < 0) {
		dev_err(dev, "failed to attach dsi to host\n");
		return ERR_PTR(ret);
	}

	return dsi;
}

static int lt9611uxc_bridge_attach(struct drm_bridge *bridge,
				   enum drm_bridge_attach_flags flags)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);

	return drm_bridge_attach(bridge->encoder, lt9611uxc->next_bridge,
				 bridge, flags);
}

static enum drm_mode_status
lt9611uxc_bridge_mode_valid(struct drm_bridge *bridge,
			    const struct drm_display_info *info,
			    const struct drm_display_mode *mode)
{
	struct lt9611uxc_mode *lt9611uxc_mode;

	lt9611uxc_mode = lt9611uxc_find_mode(mode);

	return lt9611uxc_mode ? MODE_OK : MODE_BAD;
}

static void lt9611uxc_video_setup(struct lt9611uxc *lt9611uxc,
				  const struct drm_display_mode *mode)
{
	u32 h_total, hactive, hsync_len, hfront_porch;
	u32 v_total, vactive, vsync_len, vfront_porch;

	h_total = mode->htotal;
	v_total = mode->vtotal;

	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;

	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;

	regmap_write(lt9611uxc->regmap, 0xd00d, (u8)(v_total / 256));
	regmap_write(lt9611uxc->regmap, 0xd00e, (u8)(v_total % 256));

	regmap_write(lt9611uxc->regmap, 0xd00f, (u8)(vactive / 256));
	regmap_write(lt9611uxc->regmap, 0xd010, (u8)(vactive % 256));

	regmap_write(lt9611uxc->regmap, 0xd011, (u8)(h_total / 256));
	regmap_write(lt9611uxc->regmap, 0xd012, (u8)(h_total % 256));

	regmap_write(lt9611uxc->regmap, 0xd013, (u8)(hactive / 256));
	regmap_write(lt9611uxc->regmap, 0xd014, (u8)(hactive % 256));

	regmap_write(lt9611uxc->regmap, 0xd015, (u8)(vsync_len % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd016, 0xf, (u8)(hsync_len / 256));
	regmap_write(lt9611uxc->regmap, 0xd017, (u8)(hsync_len % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd018, 0xf, (u8)(vfront_porch / 256));
	regmap_write(lt9611uxc->regmap, 0xd019, (u8)(vfront_porch % 256));

	regmap_update_bits(lt9611uxc->regmap, 0xd01a, 0xf, (u8)(hfront_porch / 256));
	regmap_write(lt9611uxc->regmap, 0xd01b, (u8)(hfront_porch % 256));
}

static void lt9611uxc_bridge_mode_set(struct drm_bridge *bridge,
				      const struct drm_display_mode *mode,
				      const struct drm_display_mode *adj_mode)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);

	lt9611uxc_lock(lt9611uxc);
	lt9611uxc_video_setup(lt9611uxc, mode);
	lt9611uxc_unlock(lt9611uxc);
}

static enum drm_connector_status lt9611uxc_bridge_detect(struct drm_bridge *bridge)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);
	unsigned int reg_val = 0;
	int ret;
	bool connected = true;

	lt9611uxc_lock(lt9611uxc);

	if (lt9611uxc->hpd_supported) {
		ret = regmap_read(lt9611uxc->regmap, 0xb023, &reg_val);

		if (ret)
			dev_err(lt9611uxc->dev, "failed to read hpd status: %d\n", ret);
		else
			connected  = reg_val & BIT(1);
	}
	lt9611uxc->hdmi_connected = connected;

	lt9611uxc_unlock(lt9611uxc);

	return connected ?  connector_status_connected :
				connector_status_disconnected;
}

static int lt9611uxc_wait_for_edid(struct lt9611uxc *lt9611uxc)
{
	return wait_event_interruptible_timeout(lt9611uxc->wq, lt9611uxc->edid_read,
			msecs_to_jiffies(500));
}

static int lt9611uxc_get_edid_block(void *data, u8 *buf, unsigned int block, size_t len)
{
	struct lt9611uxc *lt9611uxc = data;
	int ret;

	if (len > EDID_BLOCK_SIZE)
		return -EINVAL;

	if (block >= EDID_NUM_BLOCKS)
		return -EINVAL;

	lt9611uxc_lock(lt9611uxc);

	regmap_write(lt9611uxc->regmap, 0xb00b, 0x10);

	regmap_write(lt9611uxc->regmap, 0xb00a, block * EDID_BLOCK_SIZE);

	ret = regmap_noinc_read(lt9611uxc->regmap, 0xb0b0, buf, len);
	if (ret)
		dev_err(lt9611uxc->dev, "edid read failed: %d\n", ret);

	lt9611uxc_unlock(lt9611uxc);

	return 0;
};

static const struct drm_edid *lt9611uxc_bridge_edid_read(struct drm_bridge *bridge,
							 struct drm_connector *connector)
{
	struct lt9611uxc *lt9611uxc = bridge_to_lt9611uxc(bridge);
	int ret;

	ret = lt9611uxc_wait_for_edid(lt9611uxc);
	if (ret < 0) {
		dev_err(lt9611uxc->dev, "wait for EDID failed: %d\n", ret);
		return NULL;
	} else if (ret == 0) {
		dev_err(lt9611uxc->dev, "wait for EDID timeout\n");
		return NULL;
	}

	return drm_edid_read_custom(connector, lt9611uxc_get_edid_block, lt9611uxc);
}

static const struct drm_bridge_funcs lt9611uxc_bridge_funcs = {
	.attach = lt9611uxc_bridge_attach,
	.mode_valid = lt9611uxc_bridge_mode_valid,
	.mode_set = lt9611uxc_bridge_mode_set,
	.detect = lt9611uxc_bridge_detect,
	.edid_read = lt9611uxc_bridge_edid_read,
};

static int lt9611uxc_parse_dt(struct device *dev,
			      struct lt9611uxc *lt9611uxc)
{
	lt9611uxc->dsi0_node = of_graph_get_remote_node(dev->of_node, 0, -1);
	if (!lt9611uxc->dsi0_node) {
		dev_err(lt9611uxc->dev, "failed to get remote node for primary dsi\n");
		return -ENODEV;
	}

	lt9611uxc->dsi1_node = of_graph_get_remote_node(dev->of_node, 1, -1);

	return drm_of_find_panel_or_bridge(dev->of_node, 2, -1, NULL, &lt9611uxc->next_bridge);
}

static int lt9611uxc_gpio_init(struct lt9611uxc *lt9611uxc)
{
	struct device *dev = lt9611uxc->dev;

	lt9611uxc->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt9611uxc->reset_gpio)) {
		dev_err(dev, "failed to acquire reset gpio\n");
		return PTR_ERR(lt9611uxc->reset_gpio);
	}

	lt9611uxc->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(lt9611uxc->enable_gpio)) {
		dev_err(dev, "failed to acquire enable gpio\n");
		return PTR_ERR(lt9611uxc->enable_gpio);
	}

	return 0;
}

static int lt9611uxc_read_device_rev(struct lt9611uxc *lt9611uxc)
{
	unsigned int rev0, rev1, rev2;
	int ret;

	lt9611uxc_lock(lt9611uxc);

	ret = regmap_read(lt9611uxc->regmap, 0x8100, &rev0);
	ret |= regmap_read(lt9611uxc->regmap, 0x8101, &rev1);
	ret |= regmap_read(lt9611uxc->regmap, 0x8102, &rev2);
	if (ret)
		dev_err(lt9611uxc->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611uxc->dev, "LT9611 revision: 0x%02x.%02x.%02x\n", rev0, rev1, rev2);

	lt9611uxc_unlock(lt9611uxc);

	return ret;
}

static int lt9611uxc_read_version(struct lt9611uxc *lt9611uxc)
{
	unsigned int rev;
	int ret;

	lt9611uxc_lock(lt9611uxc);

	ret = regmap_read(lt9611uxc->regmap, 0xb021, &rev);
	if (ret)
		dev_err(lt9611uxc->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611uxc->dev, "LT9611 version: 0x%02x\n", rev);

	lt9611uxc_unlock(lt9611uxc);

	return ret < 0 ? ret : rev;
}

static int lt9611uxc_hdmi_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *fmt,
				    struct hdmi_codec_params *hparms)
{
	/*
	 * LT9611UXC will automatically detect rate and sample size, so no need
	 * to setup anything here.
	 */
	return 0;
}

static void lt9611uxc_audio_shutdown(struct device *dev, void *data)
{
}

static int lt9611uxc_hdmi_i2s_get_dai_id(struct snd_soc_component *component,
					 struct device_node *endpoint)
{
	struct of_endpoint of_ep;
	int ret;

	ret = of_graph_parse_endpoint(endpoint, &of_ep);
	if (ret < 0)
		return ret;

	/*
	 * HDMI sound should be located as reg = <2>
	 * Then, it is sound port 0
	 */
	if (of_ep.port == 2)
		return 0;

	return -EINVAL;
}

static const struct hdmi_codec_ops lt9611uxc_codec_ops = {
	.hw_params	= lt9611uxc_hdmi_hw_params,
	.audio_shutdown = lt9611uxc_audio_shutdown,
	.get_dai_id	= lt9611uxc_hdmi_i2s_get_dai_id,
};

static int lt9611uxc_audio_init(struct device *dev, struct lt9611uxc *lt9611uxc)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &lt9611uxc_codec_ops,
		.max_i2s_channels = 2,
		.i2s = 1,
		.data = lt9611uxc,
	};

	lt9611uxc->audio_pdev =
		platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
					      PLATFORM_DEVID_AUTO,
					      &codec_data, sizeof(codec_data));

	return PTR_ERR_OR_ZERO(lt9611uxc->audio_pdev);
}

static void lt9611uxc_audio_exit(struct lt9611uxc *lt9611uxc)
{
	if (lt9611uxc->audio_pdev) {
		platform_device_unregister(lt9611uxc->audio_pdev);
		lt9611uxc->audio_pdev = NULL;
	}
}

#define LT9611UXC_FW_PAGE_SIZE 32
static void lt9611uxc_firmware_write_page(struct lt9611uxc *lt9611uxc, u16 addr, const u8 *buf)
{
	struct reg_sequence seq_write_prepare[] = {
		REG_SEQ0(0x805a, 0x04),
		REG_SEQ0(0x805a, 0x00),

		REG_SEQ0(0x805e, 0xdf),
		REG_SEQ0(0x805a, 0x20),
		REG_SEQ0(0x805a, 0x00),
		REG_SEQ0(0x8058, 0x21),
	};

	struct reg_sequence seq_write_addr[] = {
		REG_SEQ0(0x805b, (addr >> 16) & 0xff),
		REG_SEQ0(0x805c, (addr >> 8) & 0xff),
		REG_SEQ0(0x805d, addr & 0xff),
		REG_SEQ0(0x805a, 0x10),
		REG_SEQ0(0x805a, 0x00),
	};

	regmap_write(lt9611uxc->regmap, 0x8108, 0xbf);
	msleep(20);
	regmap_write(lt9611uxc->regmap, 0x8108, 0xff);
	msleep(20);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_write_prepare, ARRAY_SIZE(seq_write_prepare));
	regmap_noinc_write(lt9611uxc->regmap, 0x8059, buf, LT9611UXC_FW_PAGE_SIZE);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_write_addr, ARRAY_SIZE(seq_write_addr));
	msleep(20);
}

static void lt9611uxc_firmware_read_page(struct lt9611uxc *lt9611uxc, u16 addr, char *buf)
{
	struct reg_sequence seq_read_page[] = {
		REG_SEQ0(0x805a, 0xa0),
		REG_SEQ0(0x805a, 0x80),
		REG_SEQ0(0x805b, (addr >> 16) & 0xff),
		REG_SEQ0(0x805c, (addr >> 8) & 0xff),
		REG_SEQ0(0x805d, addr & 0xff),
		REG_SEQ0(0x805a, 0x90),
		REG_SEQ0(0x805a, 0x80),
		REG_SEQ0(0x8058, 0x21),
	};

	regmap_multi_reg_write(lt9611uxc->regmap, seq_read_page, ARRAY_SIZE(seq_read_page));
	regmap_noinc_read(lt9611uxc->regmap, 0x805f, buf, LT9611UXC_FW_PAGE_SIZE);
}

static char *lt9611uxc_firmware_read(struct lt9611uxc *lt9611uxc, size_t size)
{
	struct reg_sequence seq_read_setup[] = {
		REG_SEQ0(0x805a, 0x84),
		REG_SEQ0(0x805a, 0x80),
	};

	char *readbuf;
	u16 offset;

	readbuf = kzalloc(ALIGN(size, 32), GFP_KERNEL);
	if (!readbuf)
		return NULL;

	regmap_multi_reg_write(lt9611uxc->regmap, seq_read_setup, ARRAY_SIZE(seq_read_setup));

	for (offset = 0;
	     offset < size;
	     offset += LT9611UXC_FW_PAGE_SIZE)
		lt9611uxc_firmware_read_page(lt9611uxc, offset, &readbuf[offset]);

	return readbuf;
}

static int lt9611uxc_firmware_update(struct lt9611uxc *lt9611uxc)
{
	int ret;
	u16 offset;
	size_t remain;
	char *readbuf;
	const struct firmware *fw;

	struct reg_sequence seq_setup[] = {
		REG_SEQ0(0x805e, 0xdf),
		REG_SEQ0(0x8058, 0x00),
		REG_SEQ0(0x8059, 0x50),
		REG_SEQ0(0x805a, 0x10),
		REG_SEQ0(0x805a, 0x00),
	};


	struct reg_sequence seq_block_erase[] = {
		REG_SEQ0(0x805a, 0x04),
		REG_SEQ0(0x805a, 0x00),
		REG_SEQ0(0x805b, 0x00),
		REG_SEQ0(0x805c, 0x00),
		REG_SEQ0(0x805d, 0x00),
		REG_SEQ0(0x805a, 0x01),
		REG_SEQ0(0x805a, 0x00),
	};

	ret = request_firmware(&fw, FW_FILE, lt9611uxc->dev);
	if (ret < 0)
		return ret;

	dev_info(lt9611uxc->dev, "Updating firmware\n");
	lt9611uxc_lock(lt9611uxc);

	regmap_multi_reg_write(lt9611uxc->regmap, seq_setup, ARRAY_SIZE(seq_setup));

	/*
	 * Need erase block 2 timess here. Sometimes, block erase can fail.
	 * This is a workaroud.
	 */
	regmap_multi_reg_write(lt9611uxc->regmap, seq_block_erase, ARRAY_SIZE(seq_block_erase));
	msleep(3000);
	regmap_multi_reg_write(lt9611uxc->regmap, seq_block_erase, ARRAY_SIZE(seq_block_erase));
	msleep(3000);

	for (offset = 0, remain = fw->size;
	     remain >= LT9611UXC_FW_PAGE_SIZE;
	     offset += LT9611UXC_FW_PAGE_SIZE, remain -= LT9611UXC_FW_PAGE_SIZE)
		lt9611uxc_firmware_write_page(lt9611uxc, offset, fw->data + offset);

	if (remain > 0) {
		char buf[LT9611UXC_FW_PAGE_SIZE];

		memset(buf, 0xff, LT9611UXC_FW_PAGE_SIZE);
		memcpy(buf, fw->data + offset, remain);
		lt9611uxc_firmware_write_page(lt9611uxc, offset, buf);
	}
	msleep(20);

	readbuf = lt9611uxc_firmware_read(lt9611uxc, fw->size);
	if (!readbuf) {
		ret = -ENOMEM;
		goto out;
	}

	if (!memcmp(readbuf, fw->data, fw->size)) {
		dev_err(lt9611uxc->dev, "Firmware update failed\n");
		print_hex_dump(KERN_ERR, "fw: ", DUMP_PREFIX_OFFSET, 16, 1, readbuf, fw->size, false);
		ret = -EINVAL;
	} else {
		dev_info(lt9611uxc->dev, "Firmware updates successfully\n");
		ret = 0;
	}
	kfree(readbuf);

out:
	lt9611uxc_unlock(lt9611uxc);
	lt9611uxc_reset(lt9611uxc);
	release_firmware(fw);

	return ret;
}

static ssize_t lt9611uxc_firmware_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);
	int ret;

	ret = lt9611uxc_firmware_update(lt9611uxc);
	if (ret < 0)
		return ret;
	return len;
}

static ssize_t lt9611uxc_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lt9611uxc *lt9611uxc = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%02x\n", lt9611uxc->fw_version);
}

static DEVICE_ATTR_RW(lt9611uxc_firmware);

static struct attribute *lt9611uxc_attrs[] = {
	&dev_attr_lt9611uxc_firmware.attr,
	NULL,
};

static const struct attribute_group lt9611uxc_attr_group = {
	.attrs = lt9611uxc_attrs,
};

static const struct attribute_group *lt9611uxc_attr_groups[] = {
	&lt9611uxc_attr_group,
	NULL,
};

static int lt9611uxc_probe(struct i2c_client *client)
{
	struct lt9611uxc *lt9611uxc;
	struct device *dev = &client->dev;
	int ret;
	bool fw_updated = false;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "device doesn't support I2C\n");
		return -ENODEV;
	}

	lt9611uxc = devm_kzalloc(dev, sizeof(*lt9611uxc), GFP_KERNEL);
	if (!lt9611uxc)
		return -ENOMEM;

	lt9611uxc->dev = dev;
	lt9611uxc->client = client;
	mutex_init(&lt9611uxc->ocm_lock);

	lt9611uxc->regmap = devm_regmap_init_i2c(client, &lt9611uxc_regmap_config);
	if (IS_ERR(lt9611uxc->regmap)) {
		dev_err(lt9611uxc->dev, "regmap i2c init failed\n");
		return PTR_ERR(lt9611uxc->regmap);
	}

	ret = lt9611uxc_parse_dt(dev, lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to parse device tree\n");
		return ret;
	}

	ret = lt9611uxc_gpio_init(lt9611uxc);
	if (ret < 0)
		goto err_of_put;

	ret = lt9611uxc_regulator_init(lt9611uxc);
	if (ret < 0)
		goto err_of_put;

	lt9611uxc_assert_5v(lt9611uxc);

	ret = lt9611uxc_regulator_enable(lt9611uxc);
	if (ret)
		goto err_of_put;

	lt9611uxc_reset(lt9611uxc);

	ret = lt9611uxc_read_device_rev(lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to read chip rev\n");
		goto err_disable_regulators;
	}

retry:
	ret = lt9611uxc_read_version(lt9611uxc);
	if (ret < 0) {
		dev_err(dev, "failed to read FW version\n");
		goto err_disable_regulators;
	} else if (ret == 0) {
		if (!fw_updated) {
			fw_updated = true;
			dev_err(dev, "FW version 0, enforcing firmware update\n");
			ret = lt9611uxc_firmware_update(lt9611uxc);
			if (ret < 0)
				goto err_disable_regulators;
			else
				goto retry;
		} else {
			dev_err(dev, "FW version 0, update failed\n");
			ret = -EOPNOTSUPP;
			goto err_disable_regulators;
		}
	} else if (ret < 0x40) {
		dev_info(dev, "FW version 0x%x, HPD not supported\n", ret);
	} else {
		lt9611uxc->hpd_supported = true;
	}
	lt9611uxc->fw_version = ret;

	init_waitqueue_head(&lt9611uxc->wq);
	INIT_WORK(&lt9611uxc->work, lt9611uxc_hpd_work);

	ret = request_threaded_irq(client->irq, NULL,
				   lt9611uxc_irq_thread_handler,
				   IRQF_ONESHOT, "lt9611uxc", lt9611uxc);
	if (ret) {
		dev_err(dev, "failed to request irq\n");
		goto err_disable_regulators;
	}

	i2c_set_clientdata(client, lt9611uxc);

	lt9611uxc->bridge.funcs = &lt9611uxc_bridge_funcs;
	lt9611uxc->bridge.of_node = client->dev.of_node;
	lt9611uxc->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID;
	if (lt9611uxc->hpd_supported)
		lt9611uxc->bridge.ops |= DRM_BRIDGE_OP_HPD;
	lt9611uxc->bridge.type = DRM_MODE_CONNECTOR_HDMIA;

	drm_bridge_add(&lt9611uxc->bridge);

	/* Attach primary DSI */
	lt9611uxc->dsi0 = lt9611uxc_attach_dsi(lt9611uxc, lt9611uxc->dsi0_node);
	if (IS_ERR(lt9611uxc->dsi0)) {
		ret = PTR_ERR(lt9611uxc->dsi0);
		goto err_remove_bridge;
	}

	/* Attach secondary DSI, if specified */
	if (lt9611uxc->dsi1_node) {
		lt9611uxc->dsi1 = lt9611uxc_attach_dsi(lt9611uxc, lt9611uxc->dsi1_node);
		if (IS_ERR(lt9611uxc->dsi1)) {
			ret = PTR_ERR(lt9611uxc->dsi1);
			goto err_remove_bridge;
		}
	}

	return lt9611uxc_audio_init(dev, lt9611uxc);

err_remove_bridge:
	free_irq(client->irq, lt9611uxc);
	cancel_work_sync(&lt9611uxc->work);
	drm_bridge_remove(&lt9611uxc->bridge);

err_disable_regulators:
	regulator_bulk_disable(ARRAY_SIZE(lt9611uxc->supplies), lt9611uxc->supplies);

err_of_put:
	of_node_put(lt9611uxc->dsi1_node);
	of_node_put(lt9611uxc->dsi0_node);

	return ret;
}

static void lt9611uxc_remove(struct i2c_client *client)
{
	struct lt9611uxc *lt9611uxc = i2c_get_clientdata(client);

	free_irq(client->irq, lt9611uxc);
	cancel_work_sync(&lt9611uxc->work);
	lt9611uxc_audio_exit(lt9611uxc);
	drm_bridge_remove(&lt9611uxc->bridge);

	mutex_destroy(&lt9611uxc->ocm_lock);

	regulator_bulk_disable(ARRAY_SIZE(lt9611uxc->supplies), lt9611uxc->supplies);

	of_node_put(lt9611uxc->dsi1_node);
	of_node_put(lt9611uxc->dsi0_node);
}

static const struct i2c_device_id lt9611uxc_id[] = {
	{ "lontium,lt9611uxc", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id lt9611uxc_match_table[] = {
	{ .compatible = "lontium,lt9611uxc" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt9611uxc_match_table);

static struct i2c_driver lt9611uxc_driver = {
	.driver = {
		.name = "lt9611uxc",
		.of_match_table = lt9611uxc_match_table,
		.dev_groups = lt9611uxc_attr_groups,
	},
	.probe = lt9611uxc_probe,
	.remove = lt9611uxc_remove,
	.id_table = lt9611uxc_id,
};
module_i2c_driver(lt9611uxc_driver);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_DESCRIPTION("Lontium LT9611UXC DSI/HDMI bridge driver");
MODULE_LICENSE("GPL v2");

MODULE_FIRMWARE(FW_FILE);
