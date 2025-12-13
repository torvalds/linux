// SPDX-License-Identifier: GPL-2.0

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/units.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_drv.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <video/mipi_display.h>

#define SSD2825_DEVICE_ID_REG			0xb0
#define SSD2825_RGB_INTERFACE_CTRL_REG_1	0xb1
#define SSD2825_RGB_INTERFACE_CTRL_REG_2	0xb2
#define SSD2825_RGB_INTERFACE_CTRL_REG_3	0xb3
#define SSD2825_RGB_INTERFACE_CTRL_REG_4	0xb4
#define SSD2825_RGB_INTERFACE_CTRL_REG_5	0xb5
#define SSD2825_RGB_INTERFACE_CTRL_REG_6	0xb6
#define   SSD2825_NON_BURST_EV			BIT(2)
#define   SSD2825_BURST				BIT(3)
#define   SSD2825_PCKL_HIGH			BIT(13)
#define   SSD2825_HSYNC_HIGH			BIT(14)
#define   SSD2825_VSYNC_HIGH			BIT(15)
#define SSD2825_CONFIGURATION_REG		0xb7
#define   SSD2825_CONF_REG_HS			BIT(0)
#define   SSD2825_CONF_REG_CKE			BIT(1)
#define   SSD2825_CONF_REG_SLP			BIT(2)
#define   SSD2825_CONF_REG_VEN			BIT(3)
#define   SSD2825_CONF_REG_HCLK			BIT(4)
#define   SSD2825_CONF_REG_CSS			BIT(5)
#define   SSD2825_CONF_REG_DCS			BIT(6)
#define   SSD2825_CONF_REG_REN			BIT(7)
#define   SSD2825_CONF_REG_ECD			BIT(8)
#define   SSD2825_CONF_REG_EOT			BIT(9)
#define   SSD2825_CONF_REG_LPE			BIT(10)
#define SSD2825_VC_CTRL_REG			0xb8
#define SSD2825_PLL_CTRL_REG			0xb9
#define SSD2825_PLL_CONFIGURATION_REG		0xba
#define SSD2825_CLOCK_CTRL_REG			0xbb
#define SSD2825_PACKET_SIZE_CTRL_REG_1		0xbc
#define SSD2825_PACKET_SIZE_CTRL_REG_2		0xbd
#define SSD2825_PACKET_SIZE_CTRL_REG_3		0xbe
#define SSD2825_PACKET_DROP_REG			0xbf
#define SSD2825_OPERATION_CTRL_REG		0xc0
#define SSD2825_MAX_RETURN_SIZE_REG		0xc1
#define SSD2825_RETURN_DATA_COUNT_REG		0xc2
#define SSD2825_ACK_RESPONSE_REG		0xc3
#define SSD2825_LINE_CTRL_REG			0xc4
#define SSD2825_INTERRUPT_CTRL_REG		0xc5
#define SSD2825_INTERRUPT_STATUS_REG		0xc6
#define SSD2825_ERROR_STATUS_REG		0xc7
#define SSD2825_DATA_FORMAT_REG			0xc8
#define SSD2825_DELAY_ADJ_REG_1			0xc9
#define SSD2825_DELAY_ADJ_REG_2			0xca
#define SSD2825_DELAY_ADJ_REG_3			0xcb
#define SSD2825_DELAY_ADJ_REG_4			0xcc
#define SSD2825_DELAY_ADJ_REG_5			0xcd
#define SSD2825_DELAY_ADJ_REG_6			0xce
#define SSD2825_HS_TX_TIMER_REG_1		0xcf
#define SSD2825_HS_TX_TIMER_REG_2		0xd0
#define SSD2825_LP_RX_TIMER_REG_1		0xd1
#define SSD2825_LP_RX_TIMER_REG_2		0xd2
#define SSD2825_TE_STATUS_REG			0xd3
#define SSD2825_SPI_READ_REG			0xd4
#define   SSD2825_SPI_READ_REG_RESET		0xfa
#define SSD2825_PLL_LOCK_REG			0xd5
#define SSD2825_TEST_REG			0xd6
#define SSD2825_TE_COUNT_REG			0xd7
#define SSD2825_ANALOG_CTRL_REG_1		0xd8
#define SSD2825_ANALOG_CTRL_REG_2		0xd9
#define SSD2825_ANALOG_CTRL_REG_3		0xda
#define SSD2825_ANALOG_CTRL_REG_4		0xdb
#define SSD2825_INTERRUPT_OUT_CTRL_REG		0xdc
#define SSD2825_RGB_INTERFACE_CTRL_REG_7	0xdd
#define SSD2825_LANE_CONFIGURATION_REG		0xde
#define SSD2825_DELAY_ADJ_REG_7			0xdf
#define SSD2825_INPUT_PIN_CTRL_REG_1		0xe0
#define SSD2825_INPUT_PIN_CTRL_REG_2		0xe1
#define SSD2825_BIDIR_PIN_CTRL_REG_1		0xe2
#define SSD2825_BIDIR_PIN_CTRL_REG_2		0xe3
#define SSD2825_BIDIR_PIN_CTRL_REG_3		0xe4
#define SSD2825_BIDIR_PIN_CTRL_REG_4		0xe5
#define SSD2825_BIDIR_PIN_CTRL_REG_5		0xe6
#define SSD2825_BIDIR_PIN_CTRL_REG_6		0xe7
#define SSD2825_BIDIR_PIN_CTRL_REG_7		0xe8
#define SSD2825_CABC_BRIGHTNESS_CTRL_REG_1	0xe9
#define SSD2825_CABC_BRIGHTNESS_CTRL_REG_2	0xea
#define SSD2825_CABC_BRIGHTNESS_STATUS_REG	0xeb
#define SSD2825_READ_REG			0xff

#define SSD2825_COM_BYTE			0x00
#define SSD2825_DAT_BYTE			0x01

#define SSD2828_LP_CLOCK_DIVIDER(n)		(((n) - 1) & 0x3f)
#define SSD2825_LP_MIN_CLK			5000 /* KHz */
#define SSD2825_REF_MIN_CLK			2000 /* KHz */

static const struct regulator_bulk_data ssd2825_supplies[] = {
	{ .supply = "dvdd" },
	{ .supply = "avdd" },
	{ .supply = "vddio" },
};

struct ssd2825_dsi_output {
	struct mipi_dsi_device *dev;
	struct drm_panel *panel;
	struct drm_bridge *bridge;
};

struct ssd2825_priv {
	struct spi_device *spi;
	struct device *dev;

	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data *supplies;

	struct clk *tx_clk;

	struct mipi_dsi_host dsi_host;
	struct drm_bridge bridge;
	struct ssd2825_dsi_output output;

	struct mutex mlock;	/* for host transfer operations */

	u32 pd_lines;		/* number of Parallel Port Input Data Lines */
	u32 dsi_lanes;		/* number of DSI Lanes */

	/* Parameters for PLL programming */
	u32 pll_freq_kbps;	/* PLL in kbps */
	u32 nibble_freq_khz;	/* PLL div by 4 */

	u32 hzd;		/* HS Zero Delay in ns*/
	u32 hpd;		/* HS Prepare Delay is ns */
};

static inline struct ssd2825_priv *dsi_host_to_ssd2825(struct mipi_dsi_host *host)
{
	return container_of(host, struct ssd2825_priv, dsi_host);
}

static inline struct ssd2825_priv *bridge_to_ssd2825(struct drm_bridge *bridge)
{
	return container_of(bridge, struct ssd2825_priv, bridge);
}

static int ssd2825_write_raw(struct ssd2825_priv *priv, u8 high_byte, u8 low_byte)
{
	struct spi_device *spi = priv->spi;
	u8 tx_buf[2];

	/*
	 * Low byte is the value, high byte defines type of
	 * write cycle, 0 for command and 1 for data.
	 */
	tx_buf[0] = low_byte;
	tx_buf[1] = high_byte;

	return spi_write(spi, tx_buf, 2);
}

static int ssd2825_write_reg(struct ssd2825_priv *priv, u8 reg, u16 command)
{
	u8 datal = (command & 0x00FF);
	u8 datah = (command & 0xFF00) >> 8;
	int ret;

	/* Command write cycle */
	ret = ssd2825_write_raw(priv, SSD2825_COM_BYTE, reg);
	if (ret)
		return ret;

	/* Data write cycle bits 7-0 */
	ret = ssd2825_write_raw(priv, SSD2825_DAT_BYTE, datal);
	if (ret)
		return ret;

	/* Data write cycle bits 15-8 */
	ret = ssd2825_write_raw(priv, SSD2825_DAT_BYTE, datah);
	if (ret)
		return ret;

	return 0;
}

static int ssd2825_write_dsi(struct ssd2825_priv *priv, const u8 *command, int len)
{
	int ret, i;

	ret = ssd2825_write_reg(priv, SSD2825_PACKET_SIZE_CTRL_REG_1, len);
	if (ret)
		return ret;

	ret = ssd2825_write_raw(priv, SSD2825_COM_BYTE, SSD2825_PACKET_DROP_REG);
	if (ret)
		return ret;

	for (i = 0; i < len; i++) {
		ret = ssd2825_write_raw(priv, SSD2825_DAT_BYTE, command[i]);
		if (ret)
			return ret;
	}

	return 0;
}

static int ssd2825_read_raw(struct ssd2825_priv *priv, u8 cmd, u16 *data)
{
	struct spi_device *spi = priv->spi;
	struct spi_message msg;
	struct spi_transfer xfer[2];
	u8 tx_buf[2];
	u8 rx_buf[2];
	int ret;

	memset(&xfer, 0, sizeof(xfer));

	tx_buf[1] = (cmd & 0xFF00) >> 8;
	tx_buf[0] = (cmd & 0x00FF);

	xfer[0].tx_buf = tx_buf;
	xfer[0].bits_per_word = 9;
	xfer[0].len = 2;

	xfer[1].rx_buf = rx_buf;
	xfer[1].bits_per_word = 16;
	xfer[1].len = 2;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer[0], &msg);
	spi_message_add_tail(&xfer[1], &msg);

	ret = spi_sync(spi, &msg);
	if (ret) {
		dev_err(&spi->dev, "ssd2825 read raw failed %d\n", ret);
		return ret;
	}

	*data = rx_buf[1] | (rx_buf[0] << 8);

	return 0;
}

static int ssd2825_read_reg(struct ssd2825_priv *priv, u8 reg, u16 *data)
{
	int ret;

	/* Reset the read register */
	ret = ssd2825_write_reg(priv, SSD2825_SPI_READ_REG, SSD2825_SPI_READ_REG_RESET);
	if (ret)
		return ret;

	/* Push the address to read */
	ret = ssd2825_write_raw(priv, SSD2825_COM_BYTE, reg);
	if (ret)
		return ret;

	/* Perform a reading cycle */
	ret = ssd2825_read_raw(priv, SSD2825_SPI_READ_REG_RESET, data);
	if (ret)
		return ret;

	return 0;
}

static int ssd2825_dsi_host_attach(struct mipi_dsi_host *host, struct mipi_dsi_device *dev)
{
	struct ssd2825_priv *priv = dsi_host_to_ssd2825(host);
	struct drm_bridge *bridge;
	struct drm_panel *panel;
	struct device_node *ep;
	int ret;

	if (dev->lanes > 4) {
		dev_err(priv->dev, "unsupported number of data lanes(%u)\n", dev->lanes);
		return -EINVAL;
	}

	/*
	 * ssd2825 supports both Video and Pulse mode, but the driver only
	 * implements Video (event) mode currently
	 */
	if (!(dev->mode_flags & MIPI_DSI_MODE_VIDEO)) {
		dev_err(priv->dev, "Only MIPI_DSI_MODE_VIDEO is supported\n");
		return -EOPNOTSUPP;
	}

	ret = drm_of_find_panel_or_bridge(host->dev->of_node, 1, 0, &panel, &bridge);
	if (ret)
		return ret;

	if (panel) {
		bridge = drm_panel_bridge_add_typed(panel, DRM_MODE_CONNECTOR_DSI);
		if (IS_ERR(bridge))
			return PTR_ERR(bridge);
	}

	priv->output.dev = dev;
	priv->output.bridge = bridge;
	priv->output.panel = panel;

	priv->dsi_lanes = dev->lanes;

	/* get input ep (port0/endpoint0) */
	ret = -EINVAL;
	ep = of_graph_get_endpoint_by_regs(host->dev->of_node, 0, 0);
	if (ep) {
		ret = of_property_read_u32(ep, "bus-width", &priv->pd_lines);
		of_node_put(ep);
	}

	if (ret)
		priv->pd_lines = mipi_dsi_pixel_format_to_bpp(dev->format);

	drm_bridge_add(&priv->bridge);

	return 0;
}

static int ssd2825_dsi_host_detach(struct mipi_dsi_host *host, struct mipi_dsi_device *dev)
{
	struct ssd2825_priv *priv = dsi_host_to_ssd2825(host);

	drm_bridge_remove(&priv->bridge);
	if (priv->output.panel)
		drm_panel_bridge_remove(priv->output.bridge);

	return 0;
}

static ssize_t ssd2825_dsi_host_transfer(struct mipi_dsi_host *host,
					 const struct mipi_dsi_msg *msg)
{
	struct ssd2825_priv *priv = dsi_host_to_ssd2825(host);
	u16 config;
	int ret;

	if (msg->rx_len) {
		dev_warn(priv->dev, "MIPI rx is not supported\n");
		return -EOPNOTSUPP;
	}

	guard(mutex)(&priv->mlock);

	ret = ssd2825_read_reg(priv, SSD2825_CONFIGURATION_REG, &config);
	if (ret)
		return ret;

	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_DCS_LONG_WRITE:
		config |= SSD2825_CONF_REG_DCS;
		break;
	case MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_GENERIC_LONG_WRITE:
		config &= ~SSD2825_CONF_REG_DCS;
		break;
	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
	default:
		return 0;
	}

	ret = ssd2825_write_reg(priv, SSD2825_CONFIGURATION_REG, config);
	if (ret)
		return ret;

	ret = ssd2825_write_reg(priv, SSD2825_VC_CTRL_REG, 0x0000);
	if (ret)
		return ret;

	ret = ssd2825_write_dsi(priv, msg->tx_buf, msg->tx_len);
	if (ret)
		return ret;

	return 0;
}

static const struct mipi_dsi_host_ops ssd2825_dsi_host_ops = {
	.attach = ssd2825_dsi_host_attach,
	.detach = ssd2825_dsi_host_detach,
	.transfer = ssd2825_dsi_host_transfer,
};

static void ssd2825_hw_reset(struct ssd2825_priv *priv)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(5000, 6000);
}

/*
 * PLL configuration register settings.
 *
 * See the "PLL Configuration Register Description" in the SSD2825 datasheet.
 */
static u16 construct_pll_config(struct ssd2825_priv *priv,
				u32 desired_pll_freq_kbps, u32 reference_freq_khz)
{
	u32 div_factor = 1, mul_factor, fr = 0;

	while (reference_freq_khz / (div_factor + 1) >= SSD2825_REF_MIN_CLK)
		div_factor++;
	if (div_factor > 31)
		div_factor = 31;

	mul_factor = DIV_ROUND_UP(desired_pll_freq_kbps * div_factor,
				  reference_freq_khz);

	priv->pll_freq_kbps = reference_freq_khz * mul_factor / div_factor;
	priv->nibble_freq_khz = priv->pll_freq_kbps / 4;

	if (priv->pll_freq_kbps >= 501000)
		fr = 3;
	else if (priv->pll_freq_kbps >= 251000)
		fr = 2;
	else if (priv->pll_freq_kbps >= 126000)
		fr = 1;

	return (fr << 14) | (div_factor << 8) | mul_factor;
}

static int ssd2825_setup_pll(struct ssd2825_priv *priv,
			     const struct drm_display_mode *mode)
{
	u16 pll_config, lp_div;
	u32 nibble_delay, pclk_mult, tx_freq_khz;
	u8 hzd, hpd;

	tx_freq_khz = clk_get_rate(priv->tx_clk) / KILO;
	if (!tx_freq_khz)
		tx_freq_khz = SSD2825_REF_MIN_CLK;

	pclk_mult = priv->pd_lines / priv->dsi_lanes + 1;
	pll_config = construct_pll_config(priv, pclk_mult * mode->clock,
					  tx_freq_khz);

	lp_div = priv->pll_freq_kbps / (SSD2825_LP_MIN_CLK * 8);

	/* nibble_delay in nanoseconds */
	nibble_delay = MICRO / priv->nibble_freq_khz;

	hzd = priv->hzd / nibble_delay;
	hpd = (priv->hpd - 4 * nibble_delay) / nibble_delay;

	/* Disable PLL */
	ssd2825_write_reg(priv, SSD2825_PLL_CTRL_REG, 0x0000);
	ssd2825_write_reg(priv, SSD2825_LINE_CTRL_REG, 0x0001);

	/* Set delays */
	ssd2825_write_reg(priv, SSD2825_DELAY_ADJ_REG_1, (hzd << 8) | hpd);

	/* Set PLL coefficients */
	ssd2825_write_reg(priv, SSD2825_PLL_CONFIGURATION_REG, pll_config);

	/* Clock Control Register */
	ssd2825_write_reg(priv, SSD2825_CLOCK_CTRL_REG,
			  SSD2828_LP_CLOCK_DIVIDER(lp_div));

	/* Enable PLL */
	ssd2825_write_reg(priv, SSD2825_PLL_CTRL_REG, 0x0001);
	ssd2825_write_reg(priv, SSD2825_VC_CTRL_REG, 0);

	return 0;
}

static void ssd2825_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct ssd2825_priv *priv = bridge_to_ssd2825(bridge);
	struct mipi_dsi_device *dsi_dev = priv->output.dev;
	const struct drm_crtc_state *crtc_state;
	const struct drm_display_mode *mode;
	struct drm_connector *connector;
	struct drm_crtc *crtc;
	u32 input_bus_flags = bridge->timings->input_bus_flags;
	u16 flags = 0, config;
	u8 pixel_format;
	int ret;

	/* Power Sequence */
	ret = clk_prepare_enable(priv->tx_clk);
	if (ret)
		dev_err(priv->dev, "error enabling tx_clk (%d)\n", ret);

	ret = regulator_bulk_enable(ARRAY_SIZE(ssd2825_supplies), priv->supplies);
	if (ret)
		dev_err(priv->dev, "error enabling regulators (%d)\n", ret);

	usleep_range(1000, 2000);

	ssd2825_hw_reset(priv);

	/* Perform SW reset */
	ssd2825_write_reg(priv, SSD2825_OPERATION_CTRL_REG, 0x0100);

	/* Set pixel format */
	switch (dsi_dev->format) {
	case MIPI_DSI_FMT_RGB565:
		pixel_format = 0x00;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		pixel_format = 0x01;
		break;
	case MIPI_DSI_FMT_RGB666:
		pixel_format = 0x02;
		break;
	case MIPI_DSI_FMT_RGB888:
	default:
		pixel_format = 0x03;
		break;
	}

	connector = drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	crtc = drm_atomic_get_new_connector_state(state, connector)->crtc;
	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	mode = &crtc_state->adjusted_mode;

	/* Set panel timings */
	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_1,
			  ((mode->vtotal - mode->vsync_end) << 8) |
			  (mode->htotal - mode->hsync_end));
	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_2,
			  ((mode->vtotal - mode->vsync_start) << 8) |
			  (mode->htotal - mode->hsync_start));
	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_3,
			  ((mode->vsync_start - mode->vdisplay) << 8) |
			  (mode->hsync_start - mode->hdisplay));
	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_4, mode->hdisplay);
	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_5, mode->vdisplay);

	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		flags |= SSD2825_HSYNC_HIGH;

	if (mode->flags & DRM_MODE_FLAG_PVSYNC)
		flags |= SSD2825_VSYNC_HIGH;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_VIDEO)
		flags |= SSD2825_NON_BURST_EV;

	if (input_bus_flags & DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE)
		flags |= SSD2825_PCKL_HIGH;

	ssd2825_write_reg(priv, SSD2825_RGB_INTERFACE_CTRL_REG_6, flags | pixel_format);
	ssd2825_write_reg(priv, SSD2825_LANE_CONFIGURATION_REG, dsi_dev->lanes - 1);
	ssd2825_write_reg(priv, SSD2825_TEST_REG, 0x0004);

	/* Call PLL configuration */
	ssd2825_setup_pll(priv, mode);

	usleep_range(10000, 11000);

	config = SSD2825_CONF_REG_HS | SSD2825_CONF_REG_CKE | SSD2825_CONF_REG_DCS |
		 SSD2825_CONF_REG_ECD | SSD2825_CONF_REG_EOT;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_LPM)
		config &= ~SSD2825_CONF_REG_HS;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET)
		config &= ~SSD2825_CONF_REG_EOT;

	/* Initial DSI configuration register set */
	ssd2825_write_reg(priv, SSD2825_CONFIGURATION_REG, config);
	ssd2825_write_reg(priv, SSD2825_VC_CTRL_REG, 0);

	if (priv->output.panel)
		drm_panel_enable(priv->output.panel);
}

static void ssd2825_bridge_atomic_enable(struct drm_bridge *bridge,
					 struct drm_atomic_state *state)
{
	struct ssd2825_priv *priv = bridge_to_ssd2825(bridge);
	struct mipi_dsi_device *dsi_dev = priv->output.dev;
	u16 config;

	config = SSD2825_CONF_REG_HS | SSD2825_CONF_REG_DCS |
		 SSD2825_CONF_REG_ECD | SSD2825_CONF_REG_EOT;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_VIDEO)
		config |= SSD2825_CONF_REG_VEN;

	if (dsi_dev->mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET)
		config &= ~SSD2825_CONF_REG_EOT;

	/* Complete configuration after DSI commands were sent */
	ssd2825_write_reg(priv, SSD2825_CONFIGURATION_REG, config);
	ssd2825_write_reg(priv, SSD2825_PLL_CTRL_REG, 0x0001);
	ssd2825_write_reg(priv, SSD2825_VC_CTRL_REG, 0x0000);
}

static void ssd2825_bridge_atomic_disable(struct drm_bridge *bridge,
					  struct drm_atomic_state *state)
{
	struct ssd2825_priv *priv = bridge_to_ssd2825(bridge);
	int ret;

	msleep(100);

	/* Exit DSI configuration register set */
	ssd2825_write_reg(priv, SSD2825_CONFIGURATION_REG,
			  SSD2825_CONF_REG_ECD | SSD2825_CONF_REG_EOT);
	ssd2825_write_reg(priv, SSD2825_VC_CTRL_REG, 0);

	/* HW disable */
	gpiod_set_value_cansleep(priv->reset_gpio, 1);
	usleep_range(5000, 6000);

	ret = regulator_bulk_disable(ARRAY_SIZE(ssd2825_supplies),
				     priv->supplies);
	if (ret < 0)
		dev_err(priv->dev, "error disabling regulators (%d)\n", ret);

	clk_disable_unprepare(priv->tx_clk);
}

static int ssd2825_bridge_attach(struct drm_bridge *bridge, struct drm_encoder *encoder,
				 enum drm_bridge_attach_flags flags)
{
	struct ssd2825_priv *priv = bridge_to_ssd2825(bridge);

	return drm_bridge_attach(bridge->encoder, priv->output.bridge, bridge,
				 flags);
}

static enum drm_mode_status
ssd2825_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_info *info,
			  const struct drm_display_mode *mode)
{
	if (mode->hdisplay > 1366)
		return MODE_H_ILLEGAL;

	if (mode->vdisplay > 1366)
		return MODE_V_ILLEGAL;

	return MODE_OK;
}

static bool ssd2825_mode_fixup(struct drm_bridge *bridge,
			       const struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode)
{
	/* Default to positive sync */

	if (!(adjusted_mode->flags &
	      (DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_NHSYNC)))
		adjusted_mode->flags |= DRM_MODE_FLAG_PHSYNC;

	if (!(adjusted_mode->flags &
	      (DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_NVSYNC)))
		adjusted_mode->flags |= DRM_MODE_FLAG_PVSYNC;

	return true;
}

static const struct drm_bridge_funcs ssd2825_bridge_funcs = {
	.attach = ssd2825_bridge_attach,
	.mode_valid = ssd2825_bridge_mode_valid,
	.mode_fixup = ssd2825_mode_fixup,

	.atomic_pre_enable = ssd2825_bridge_atomic_pre_enable,
	.atomic_enable = ssd2825_bridge_atomic_enable,
	.atomic_disable = ssd2825_bridge_atomic_disable,

	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
};

static const struct drm_bridge_timings default_ssd2825_timings = {
	.input_bus_flags = DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE
		 | DRM_BUS_FLAG_SYNC_SAMPLE_NEGEDGE
		 | DRM_BUS_FLAG_DE_HIGH,
};

static int ssd2825_probe(struct spi_device *spi)
{
	struct ssd2825_priv *priv;
	struct device *dev = &spi->dev;
	struct device_node *np = dev->of_node;
	int ret;

	/* Driver supports only 8 bit 3 Wire mode */
	spi->bits_per_word = 9;

	ret = spi_setup(spi);
	if (ret)
		return ret;

	priv = devm_drm_bridge_alloc(dev, struct ssd2825_priv, bridge, &ssd2825_bridge_funcs);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	spi_set_drvdata(spi, priv);

	priv->spi = spi;
	priv->dev = dev;

	mutex_init(&priv->mlock);

	priv->tx_clk = devm_clk_get_optional(dev, NULL);
	if (IS_ERR(priv->tx_clk))
		return dev_err_probe(dev, PTR_ERR(priv->tx_clk),
				     "can't retrieve bridge tx_clk\n");

	priv->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(priv->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(priv->reset_gpio),
				     "failed to get reset GPIO\n");

	ret = devm_regulator_bulk_get_const(dev, ARRAY_SIZE(ssd2825_supplies),
					    ssd2825_supplies, &priv->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get regulators\n");

	priv->hzd = 133; /* ns */
	device_property_read_u32(dev, "solomon,hs-zero-delay-ns", &priv->hzd);

	priv->hpd = 40; /* ns */
	device_property_read_u32(dev, "solomon,hs-prep-delay-ns", &priv->hpd);

	priv->dsi_host.dev = dev;
	priv->dsi_host.ops = &ssd2825_dsi_host_ops;

	priv->bridge.timings = &default_ssd2825_timings;
	priv->bridge.of_node = np;

	return mipi_dsi_host_register(&priv->dsi_host);
}

static void ssd2825_remove(struct spi_device *spi)
{
	struct ssd2825_priv *priv = spi_get_drvdata(spi);

	mipi_dsi_host_unregister(&priv->dsi_host);
}

static const struct of_device_id ssd2825_of_match[] = {
	{ .compatible = "solomon,ssd2825" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ssd2825_of_match);

static struct spi_driver ssd2825_driver = {
	.driver = {
		.name = "ssd2825",
		.of_match_table = ssd2825_of_match,
	},
	.probe = ssd2825_probe,
	.remove = ssd2825_remove,
};
module_spi_driver(ssd2825_driver);

MODULE_AUTHOR("Svyatoslav Ryhel <clamor95@gmail.com>");
MODULE_DESCRIPTION("Solomon SSD2825 RGB to MIPI-DSI bridge driver SPI");
MODULE_LICENSE("GPL");
