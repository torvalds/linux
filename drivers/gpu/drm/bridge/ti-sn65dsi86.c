// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * datasheet: https://www.ti.com/lit/ds/symlink/sn65dsi86.pdf
 */

#include <linux/atomic.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <linux/unaligned.h>

#include <drm/display/drm_dp_aux_bus.h>
#include <drm/display/drm_dp_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_edid.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_of.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#define SN_DEVICE_ID_REGS			0x00	/* up to 0x07 */
#define SN_DEVICE_REV_REG			0x08
#define SN_DPPLL_SRC_REG			0x0A
#define  DPPLL_CLK_SRC_DSICLK			BIT(0)
#define  REFCLK_FREQ_MASK			GENMASK(3, 1)
#define  REFCLK_FREQ(x)				((x) << 1)
#define  DPPLL_SRC_DP_PLL_LOCK			BIT(7)
#define SN_PLL_ENABLE_REG			0x0D
#define SN_DSI_LANES_REG			0x10
#define  CHA_DSI_LANES_MASK			GENMASK(4, 3)
#define  CHA_DSI_LANES(x)			((x) << 3)
#define SN_DSIA_CLK_FREQ_REG			0x12
#define SN_CHA_ACTIVE_LINE_LENGTH_LOW_REG	0x20
#define SN_CHA_VERTICAL_DISPLAY_SIZE_LOW_REG	0x24
#define SN_CHA_HSYNC_PULSE_WIDTH_LOW_REG	0x2C
#define SN_CHA_HSYNC_PULSE_WIDTH_HIGH_REG	0x2D
#define  CHA_HSYNC_POLARITY			BIT(7)
#define SN_CHA_VSYNC_PULSE_WIDTH_LOW_REG	0x30
#define SN_CHA_VSYNC_PULSE_WIDTH_HIGH_REG	0x31
#define  CHA_VSYNC_POLARITY			BIT(7)
#define SN_CHA_HORIZONTAL_BACK_PORCH_REG	0x34
#define SN_CHA_VERTICAL_BACK_PORCH_REG		0x36
#define SN_CHA_HORIZONTAL_FRONT_PORCH_REG	0x38
#define SN_CHA_VERTICAL_FRONT_PORCH_REG		0x3A
#define SN_LN_ASSIGN_REG			0x59
#define  LN_ASSIGN_WIDTH			2
#define SN_ENH_FRAME_REG			0x5A
#define  VSTREAM_ENABLE				BIT(3)
#define  LN_POLRS_OFFSET			4
#define  LN_POLRS_MASK				0xf0
#define SN_DATA_FORMAT_REG			0x5B
#define  BPP_18_RGB				BIT(0)
#define SN_HPD_DISABLE_REG			0x5C
#define  HPD_DISABLE				BIT(0)
#define  HPD_DEBOUNCED_STATE			BIT(4)
#define SN_GPIO_IO_REG				0x5E
#define  SN_GPIO_INPUT_SHIFT			4
#define  SN_GPIO_OUTPUT_SHIFT			0
#define SN_GPIO_CTRL_REG			0x5F
#define  SN_GPIO_MUX_INPUT			0
#define  SN_GPIO_MUX_OUTPUT			1
#define  SN_GPIO_MUX_SPECIAL			2
#define  SN_GPIO_MUX_MASK			0x3
#define SN_AUX_WDATA_REG(x)			(0x64 + (x))
#define SN_AUX_ADDR_19_16_REG			0x74
#define SN_AUX_ADDR_15_8_REG			0x75
#define SN_AUX_ADDR_7_0_REG			0x76
#define SN_AUX_ADDR_MASK			GENMASK(19, 0)
#define SN_AUX_LENGTH_REG			0x77
#define SN_AUX_CMD_REG				0x78
#define  AUX_CMD_SEND				BIT(0)
#define  AUX_CMD_REQ(x)				((x) << 4)
#define SN_AUX_RDATA_REG(x)			(0x79 + (x))
#define SN_SSC_CONFIG_REG			0x93
#define  DP_NUM_LANES_MASK			GENMASK(5, 4)
#define  DP_NUM_LANES(x)			((x) << 4)
#define SN_DATARATE_CONFIG_REG			0x94
#define  DP_DATARATE_MASK			GENMASK(7, 5)
#define  DP_DATARATE(x)				((x) << 5)
#define SN_TRAINING_SETTING_REG			0x95
#define  SCRAMBLE_DISABLE			BIT(4)
#define SN_ML_TX_MODE_REG			0x96
#define  ML_TX_MAIN_LINK_OFF			0
#define  ML_TX_NORMAL_MODE			BIT(0)
#define SN_PWM_PRE_DIV_REG			0xA0
#define SN_BACKLIGHT_SCALE_REG			0xA1
#define  BACKLIGHT_SCALE_MAX			0xFFFF
#define SN_BACKLIGHT_REG			0xA3
#define SN_PWM_EN_INV_REG			0xA5
#define  SN_PWM_INV_MASK			BIT(0)
#define  SN_PWM_EN_MASK				BIT(1)
#define SN_AUX_CMD_STATUS_REG			0xF4
#define  AUX_IRQ_STATUS_AUX_RPLY_TOUT		BIT(3)
#define  AUX_IRQ_STATUS_AUX_SHORT		BIT(5)
#define  AUX_IRQ_STATUS_NAT_I2C_FAIL		BIT(6)

#define MIN_DSI_CLK_FREQ_MHZ	40

/* fudge factor required to account for 8b/10b encoding */
#define DP_CLK_FUDGE_NUM	10
#define DP_CLK_FUDGE_DEN	8

/* Matches DP_AUX_MAX_PAYLOAD_BYTES (for now) */
#define SN_AUX_MAX_PAYLOAD_BYTES	16

#define SN_REGULATOR_SUPPLY_NUM		4

#define SN_MAX_DP_LANES			4
#define SN_NUM_GPIOS			4
#define SN_GPIO_PHYSICAL_OFFSET		1

#define SN_LINK_TRAINING_TRIES		10

#define SN_PWM_GPIO_IDX			3 /* 4th GPIO */

/**
 * struct ti_sn65dsi86 - Platform data for ti-sn65dsi86 driver.
 * @bridge_aux:   AUX-bus sub device for MIPI-to-eDP bridge functionality.
 * @gpio_aux:     AUX-bus sub device for GPIO controller functionality.
 * @aux_aux:      AUX-bus sub device for eDP AUX channel functionality.
 * @pwm_aux:      AUX-bus sub device for PWM controller functionality.
 *
 * @dev:          Pointer to the top level (i2c) device.
 * @regmap:       Regmap for accessing i2c.
 * @aux:          Our aux channel.
 * @bridge:       Our bridge.
 * @connector:    Our connector.
 * @host_node:    Remote DSI node.
 * @dsi:          Our MIPI DSI source.
 * @refclk:       Our reference clock.
 * @next_bridge:  The bridge on the eDP side.
 * @enable_gpio:  The GPIO we toggle to enable the bridge.
 * @supplies:     Data for bulk enabling/disabling our regulators.
 * @dp_lanes:     Count of dp_lanes we're using.
 * @ln_assign:    Value to program to the LN_ASSIGN register.
 * @ln_polrs:     Value for the 4-bit LN_POLRS field of SN_ENH_FRAME_REG.
 * @comms_enabled: If true then communication over the aux channel is enabled.
 * @comms_mutex:   Protects modification of comms_enabled.
 *
 * @gchip:        If we expose our GPIOs, this is used.
 * @gchip_output: A cache of whether we've set GPIOs to output.  This
 *                serves double-duty of keeping track of the direction and
 *                also keeping track of whether we've incremented the
 *                pm_runtime reference count for this pin, which we do
 *                whenever a pin is configured as an output.  This is a
 *                bitmap so we can do atomic ops on it without an extra
 *                lock so concurrent users of our 4 GPIOs don't stomp on
 *                each other's read-modify-write.
 *
 * @pchip:        pwm_chip if the PWM is exposed.
 * @pwm_enabled:  Used to track if the PWM signal is currently enabled.
 * @pwm_pin_busy: Track if GPIO4 is currently requested for GPIO or PWM.
 * @pwm_refclk_freq: Cache for the reference clock input to the PWM.
 */
struct ti_sn65dsi86 {
	struct auxiliary_device		*bridge_aux;
	struct auxiliary_device		*gpio_aux;
	struct auxiliary_device		*aux_aux;
	struct auxiliary_device		*pwm_aux;

	struct device			*dev;
	struct regmap			*regmap;
	struct drm_dp_aux		aux;
	struct drm_bridge		bridge;
	struct drm_connector		*connector;
	struct device_node		*host_node;
	struct mipi_dsi_device		*dsi;
	struct clk			*refclk;
	struct drm_bridge		*next_bridge;
	struct gpio_desc		*enable_gpio;
	struct regulator_bulk_data	supplies[SN_REGULATOR_SUPPLY_NUM];
	int				dp_lanes;
	u8				ln_assign;
	u8				ln_polrs;
	bool				comms_enabled;
	struct mutex			comms_mutex;

#if defined(CONFIG_OF_GPIO)
	struct gpio_chip		gchip;
	DECLARE_BITMAP(gchip_output, SN_NUM_GPIOS);
#endif
#if IS_REACHABLE(CONFIG_PWM)
	struct pwm_chip			*pchip;
	bool				pwm_enabled;
	atomic_t			pwm_pin_busy;
#endif
	unsigned int			pwm_refclk_freq;
};

static const struct regmap_range ti_sn65dsi86_volatile_ranges[] = {
	{ .range_min = 0, .range_max = 0xFF },
};

static const struct regmap_access_table ti_sn_bridge_volatile_table = {
	.yes_ranges = ti_sn65dsi86_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(ti_sn65dsi86_volatile_ranges),
};

static const struct regmap_config ti_sn65dsi86_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &ti_sn_bridge_volatile_table,
	.cache_type = REGCACHE_NONE,
	.max_register = 0xFF,
};

static int __maybe_unused ti_sn65dsi86_read_u16(struct ti_sn65dsi86 *pdata,
						unsigned int reg, u16 *val)
{
	u8 buf[2];
	int ret;

	ret = regmap_bulk_read(pdata->regmap, reg, buf, ARRAY_SIZE(buf));
	if (ret)
		return ret;

	*val = buf[0] | (buf[1] << 8);

	return 0;
}

static void ti_sn65dsi86_write_u16(struct ti_sn65dsi86 *pdata,
				   unsigned int reg, u16 val)
{
	u8 buf[2] = { val & 0xff, val >> 8 };

	regmap_bulk_write(pdata->regmap, reg, buf, ARRAY_SIZE(buf));
}

static struct drm_display_mode *
get_new_adjusted_display_mode(struct drm_bridge *bridge,
			      struct drm_atomic_state *state)
{
	struct drm_connector *connector =
		drm_atomic_get_new_connector_for_encoder(state, bridge->encoder);
	struct drm_connector_state *conn_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_crtc_state *crtc_state =
		drm_atomic_get_new_crtc_state(state, conn_state->crtc);

	return &crtc_state->adjusted_mode;
}

static u32 ti_sn_bridge_get_dsi_freq(struct ti_sn65dsi86 *pdata,
				     struct drm_atomic_state *state)
{
	u32 bit_rate_khz, clk_freq_khz;
	struct drm_display_mode *mode =
		get_new_adjusted_display_mode(&pdata->bridge, state);

	bit_rate_khz = mode->clock *
			mipi_dsi_pixel_format_to_bpp(pdata->dsi->format);
	clk_freq_khz = bit_rate_khz / (pdata->dsi->lanes * 2);

	return clk_freq_khz;
}

/* clk frequencies supported by bridge in Hz in case derived from REFCLK pin */
static const u32 ti_sn_bridge_refclk_lut[] = {
	12000000,
	19200000,
	26000000,
	27000000,
	38400000,
};

/* clk frequencies supported by bridge in Hz in case derived from DACP/N pin */
static const u32 ti_sn_bridge_dsiclk_lut[] = {
	468000000,
	384000000,
	416000000,
	486000000,
	460800000,
};

static void ti_sn_bridge_set_refclk_freq(struct ti_sn65dsi86 *pdata,
					 struct drm_atomic_state *state)
{
	int i;
	u32 refclk_rate;
	const u32 *refclk_lut;
	size_t refclk_lut_size;

	if (pdata->refclk) {
		refclk_rate = clk_get_rate(pdata->refclk);
		refclk_lut = ti_sn_bridge_refclk_lut;
		refclk_lut_size = ARRAY_SIZE(ti_sn_bridge_refclk_lut);
		clk_prepare_enable(pdata->refclk);
	} else {
		refclk_rate = ti_sn_bridge_get_dsi_freq(pdata, state) * 1000;
		refclk_lut = ti_sn_bridge_dsiclk_lut;
		refclk_lut_size = ARRAY_SIZE(ti_sn_bridge_dsiclk_lut);
	}

	/* for i equals to refclk_lut_size means default frequency */
	for (i = 0; i < refclk_lut_size; i++)
		if (refclk_lut[i] == refclk_rate)
			break;

	/* avoid buffer overflow and "1" is the default rate in the datasheet. */
	if (i >= refclk_lut_size)
		i = 1;

	regmap_update_bits(pdata->regmap, SN_DPPLL_SRC_REG, REFCLK_FREQ_MASK,
			   REFCLK_FREQ(i));

	/*
	 * The PWM refclk is based on the value written to SN_DPPLL_SRC_REG,
	 * regardless of its actual sourcing.
	 */
	pdata->pwm_refclk_freq = ti_sn_bridge_refclk_lut[i];
}

static void ti_sn65dsi86_enable_comms(struct ti_sn65dsi86 *pdata,
				      struct drm_atomic_state *state)
{
	mutex_lock(&pdata->comms_mutex);

	/* configure bridge ref_clk */
	ti_sn_bridge_set_refclk_freq(pdata, state);

	/*
	 * HPD on this bridge chip is a bit useless.  This is an eDP bridge
	 * so the HPD is an internal signal that's only there to signal that
	 * the panel is done powering up.  ...but the bridge chip debounces
	 * this signal by between 100 ms and 400 ms (depending on process,
	 * voltage, and temperate--I measured it at about 200 ms).  One
	 * particular panel asserted HPD 84 ms after it was powered on meaning
	 * that we saw HPD 284 ms after power on.  ...but the same panel said
	 * that instead of looking at HPD you could just hardcode a delay of
	 * 200 ms.  We'll assume that the panel driver will have the hardcoded
	 * delay in its prepare and always disable HPD.
	 *
	 * For DisplayPort bridge type, we need HPD. So we use the bridge type
	 * to conditionally disable HPD.
	 * NOTE: The bridge type is set in ti_sn_bridge_probe() but enable_comms()
	 * can be called before. So for DisplayPort, HPD will be enabled once
	 * bridge type is set. We are using bridge type instead of "no-hpd"
	 * property because it is not used properly in devicetree description
	 * and hence is unreliable.
	 */

	if (pdata->bridge.type != DRM_MODE_CONNECTOR_DisplayPort)
		regmap_update_bits(pdata->regmap, SN_HPD_DISABLE_REG, HPD_DISABLE,
				   HPD_DISABLE);

	pdata->comms_enabled = true;

	mutex_unlock(&pdata->comms_mutex);
}

static void ti_sn65dsi86_disable_comms(struct ti_sn65dsi86 *pdata)
{
	mutex_lock(&pdata->comms_mutex);

	pdata->comms_enabled = false;
	clk_disable_unprepare(pdata->refclk);

	mutex_unlock(&pdata->comms_mutex);
}

static int __maybe_unused ti_sn65dsi86_resume(struct device *dev)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(SN_REGULATOR_SUPPLY_NUM, pdata->supplies);
	if (ret) {
		DRM_ERROR("failed to enable supplies %d\n", ret);
		return ret;
	}

	/* td2: min 100 us after regulators before enabling the GPIO */
	usleep_range(100, 110);

	gpiod_set_value_cansleep(pdata->enable_gpio, 1);

	/*
	 * After EN is deasserted and an external clock is detected, the bridge
	 * will sample GPIO3:1 to determine its frequency. The driver will
	 * overwrite this setting in ti_sn_bridge_set_refclk_freq(). But this is
	 * racy. Thus we have to wait a couple of us. According to the datasheet
	 * the GPIO lines has to be stable at least 5 us (td5) but it seems that
	 * is not enough and the refclk frequency value is still lost or
	 * overwritten by the bridge itself. Waiting for 20us seems to work.
	 */
	usleep_range(20, 30);

	/*
	 * If we have a reference clock we can enable communication w/ the
	 * panel (including the aux channel) w/out any need for an input clock
	 * so we can do it in resume which lets us read the EDID before
	 * pre_enable(). Without a reference clock we need the MIPI reference
	 * clock so reading early doesn't work.
	 */
	if (pdata->refclk)
		ti_sn65dsi86_enable_comms(pdata, NULL);

	return ret;
}

static int __maybe_unused ti_sn65dsi86_suspend(struct device *dev)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(dev);
	int ret;

	if (pdata->refclk)
		ti_sn65dsi86_disable_comms(pdata);

	gpiod_set_value_cansleep(pdata->enable_gpio, 0);

	ret = regulator_bulk_disable(SN_REGULATOR_SUPPLY_NUM, pdata->supplies);
	if (ret)
		DRM_ERROR("failed to disable supplies %d\n", ret);

	return ret;
}

static const struct dev_pm_ops ti_sn65dsi86_pm_ops = {
	SET_RUNTIME_PM_OPS(ti_sn65dsi86_suspend, ti_sn65dsi86_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static int status_show(struct seq_file *s, void *data)
{
	struct ti_sn65dsi86 *pdata = s->private;
	unsigned int reg, val;

	seq_puts(s, "STATUS REGISTERS:\n");

	pm_runtime_get_sync(pdata->dev);

	/* IRQ Status Registers, see Table 31 in datasheet */
	for (reg = 0xf0; reg <= 0xf8; reg++) {
		regmap_read(pdata->regmap, reg, &val);
		seq_printf(s, "[0x%02x] = 0x%08x\n", reg, val);
	}

	pm_runtime_put_autosuspend(pdata->dev);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(status);

/* -----------------------------------------------------------------------------
 * Auxiliary Devices (*not* AUX)
 */

static int ti_sn65dsi86_add_aux_device(struct ti_sn65dsi86 *pdata,
				       struct auxiliary_device **aux_out,
				       const char *name)
{
	struct device *dev = pdata->dev;
	const struct i2c_client *client = to_i2c_client(dev);
	struct auxiliary_device *aux;
	int id;

	id = (client->adapter->nr << 10) | client->addr;
	aux = __devm_auxiliary_device_create(dev, KBUILD_MODNAME, name,
					     NULL, id);
	if (!aux)
		return -ENODEV;

	*aux_out = aux;
	return 0;
}

/* -----------------------------------------------------------------------------
 * AUX Adapter
 */

static struct ti_sn65dsi86 *aux_to_ti_sn65dsi86(struct drm_dp_aux *aux)
{
	return container_of(aux, struct ti_sn65dsi86, aux);
}

static ssize_t ti_sn_aux_transfer(struct drm_dp_aux *aux,
				  struct drm_dp_aux_msg *msg)
{
	struct ti_sn65dsi86 *pdata = aux_to_ti_sn65dsi86(aux);
	u32 request = msg->request & ~(DP_AUX_I2C_MOT | DP_AUX_I2C_WRITE_STATUS_UPDATE);
	u32 request_val = AUX_CMD_REQ(msg->request);
	u8 *buf = msg->buffer;
	unsigned int len = msg->size;
	unsigned int short_len;
	unsigned int val;
	int ret;
	u8 addr_len[SN_AUX_LENGTH_REG + 1 - SN_AUX_ADDR_19_16_REG];

	if (len > SN_AUX_MAX_PAYLOAD_BYTES)
		return -EINVAL;

	pm_runtime_get_sync(pdata->dev);
	mutex_lock(&pdata->comms_mutex);

	/*
	 * If someone tries to do a DDC over AUX transaction before pre_enable()
	 * on a device without a dedicated reference clock then we just can't
	 * do it. Fail right away. This prevents non-refclk users from reading
	 * the EDID before enabling the panel but such is life.
	 */
	if (!pdata->comms_enabled) {
		ret = -EIO;
		goto exit;
	}

	switch (request) {
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ:
		regmap_write(pdata->regmap, SN_AUX_CMD_REG, request_val);
		/* Assume it's good */
		msg->reply = 0;
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	BUILD_BUG_ON(sizeof(addr_len) != sizeof(__be32));
	put_unaligned_be32((msg->address & SN_AUX_ADDR_MASK) << 8 | len,
			   addr_len);
	regmap_bulk_write(pdata->regmap, SN_AUX_ADDR_19_16_REG, addr_len,
			  ARRAY_SIZE(addr_len));

	if (request == DP_AUX_NATIVE_WRITE || request == DP_AUX_I2C_WRITE)
		regmap_bulk_write(pdata->regmap, SN_AUX_WDATA_REG(0), buf, len);

	/* Clear old status bits before start so we don't get confused */
	regmap_write(pdata->regmap, SN_AUX_CMD_STATUS_REG,
		     AUX_IRQ_STATUS_NAT_I2C_FAIL |
		     AUX_IRQ_STATUS_AUX_RPLY_TOUT |
		     AUX_IRQ_STATUS_AUX_SHORT);

	regmap_write(pdata->regmap, SN_AUX_CMD_REG, request_val | AUX_CMD_SEND);

	/* Zero delay loop because i2c transactions are slow already */
	ret = regmap_read_poll_timeout(pdata->regmap, SN_AUX_CMD_REG, val,
				       !(val & AUX_CMD_SEND), 0, 50 * 1000);
	if (ret)
		goto exit;

	ret = regmap_read(pdata->regmap, SN_AUX_CMD_STATUS_REG, &val);
	if (ret)
		goto exit;

	if (val & AUX_IRQ_STATUS_AUX_RPLY_TOUT) {
		/*
		 * The hardware tried the message seven times per the DP spec
		 * but it hit a timeout. We ignore defers here because they're
		 * handled in hardware.
		 */
		ret = -ETIMEDOUT;
		goto exit;
	}

	if (val & AUX_IRQ_STATUS_AUX_SHORT) {
		ret = regmap_read(pdata->regmap, SN_AUX_LENGTH_REG, &short_len);
		len = min(len, short_len);
		if (ret)
			goto exit;
	} else if (val & AUX_IRQ_STATUS_NAT_I2C_FAIL) {
		switch (request) {
		case DP_AUX_I2C_WRITE:
		case DP_AUX_I2C_READ:
			msg->reply |= DP_AUX_I2C_REPLY_NACK;
			break;
		case DP_AUX_NATIVE_READ:
		case DP_AUX_NATIVE_WRITE:
			msg->reply |= DP_AUX_NATIVE_REPLY_NACK;
			break;
		}
		len = 0;
		goto exit;
	}

	if (request != DP_AUX_NATIVE_WRITE && request != DP_AUX_I2C_WRITE && len != 0)
		ret = regmap_bulk_read(pdata->regmap, SN_AUX_RDATA_REG(0), buf, len);

exit:
	mutex_unlock(&pdata->comms_mutex);
	pm_runtime_mark_last_busy(pdata->dev);
	pm_runtime_put_autosuspend(pdata->dev);

	if (ret)
		return ret;
	return len;
}

static int ti_sn_aux_wait_hpd_asserted(struct drm_dp_aux *aux, unsigned long wait_us)
{
	/*
	 * The HPD in this chip is a bit useless (See comment in
	 * ti_sn65dsi86_enable_comms) so if our driver is expected to wait
	 * for HPD, we just assume it's asserted after the wait_us delay.
	 *
	 * In case we are asked to wait forever (wait_us=0) take conservative
	 * 500ms delay.
	 */
	if (wait_us == 0)
		wait_us = 500000;

	usleep_range(wait_us, wait_us + 1000);

	return 0;
}

static int ti_sn_aux_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);
	int ret;

	pdata->aux.name = "ti-sn65dsi86-aux";
	pdata->aux.dev = &adev->dev;
	pdata->aux.transfer = ti_sn_aux_transfer;
	pdata->aux.wait_hpd_asserted = ti_sn_aux_wait_hpd_asserted;
	drm_dp_aux_init(&pdata->aux);

	ret = devm_of_dp_aux_populate_ep_devices(&pdata->aux);
	if (ret)
		return ret;

	/*
	 * The eDP to MIPI bridge parts don't work until the AUX channel is
	 * setup so we don't add it in the main driver probe, we add it now.
	 */
	return ti_sn65dsi86_add_aux_device(pdata, &pdata->bridge_aux, "bridge");
}

static const struct auxiliary_device_id ti_sn_aux_id_table[] = {
	{ .name = "ti_sn65dsi86.aux", },
	{},
};

static struct auxiliary_driver ti_sn_aux_driver = {
	.name = "aux",
	.probe = ti_sn_aux_probe,
	.id_table = ti_sn_aux_id_table,
};

/*------------------------------------------------------------------------------
 * DRM Bridge
 */

static struct ti_sn65dsi86 *bridge_to_ti_sn65dsi86(struct drm_bridge *bridge)
{
	return container_of(bridge, struct ti_sn65dsi86, bridge);
}

static int ti_sn_attach_host(struct auxiliary_device *adev, struct ti_sn65dsi86 *pdata)
{
	int val;
	struct mipi_dsi_host *host;
	struct mipi_dsi_device *dsi;
	struct device *dev = pdata->dev;
	const struct mipi_dsi_device_info info = { .type = "ti_sn_bridge",
						   .channel = 0,
						   .node = NULL,
	};

	host = of_find_mipi_dsi_host_by_node(pdata->host_node);
	if (!host)
		return -EPROBE_DEFER;

	dsi = devm_mipi_dsi_device_register_full(&adev->dev, host, &info);
	if (IS_ERR(dsi))
		return PTR_ERR(dsi);

	/* TODO: setting to 4 MIPI lanes always for now */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO;

	/* check if continuous dsi clock is required or not */
	pm_runtime_get_sync(dev);
	regmap_read(pdata->regmap, SN_DPPLL_SRC_REG, &val);
	pm_runtime_put_autosuspend(dev);
	if (!(val & DPPLL_CLK_SRC_DSICLK))
		dsi->mode_flags |= MIPI_DSI_CLOCK_NON_CONTINUOUS;

	pdata->dsi = dsi;

	return devm_mipi_dsi_attach(&adev->dev, dsi);
}

static int ti_sn_bridge_attach(struct drm_bridge *bridge,
			       struct drm_encoder *encoder,
			       enum drm_bridge_attach_flags flags)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);
	int ret;

	pdata->aux.drm_dev = bridge->dev;
	ret = drm_dp_aux_register(&pdata->aux);
	if (ret < 0) {
		drm_err(bridge->dev, "Failed to register DP AUX channel: %d\n", ret);
		return ret;
	}

	/*
	 * Attach the next bridge.
	 * We never want the next bridge to *also* create a connector.
	 */
	ret = drm_bridge_attach(encoder, pdata->next_bridge,
				&pdata->bridge, flags | DRM_BRIDGE_ATTACH_NO_CONNECTOR);
	if (ret < 0)
		goto err_initted_aux;

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
		return 0;

	pdata->connector = drm_bridge_connector_init(pdata->bridge.dev,
						     pdata->bridge.encoder);
	if (IS_ERR(pdata->connector)) {
		ret = PTR_ERR(pdata->connector);
		goto err_initted_aux;
	}

	drm_connector_attach_encoder(pdata->connector, pdata->bridge.encoder);

	return 0;

err_initted_aux:
	drm_dp_aux_unregister(&pdata->aux);
	return ret;
}

static void ti_sn_bridge_detach(struct drm_bridge *bridge)
{
	drm_dp_aux_unregister(&bridge_to_ti_sn65dsi86(bridge)->aux);
}

static enum drm_mode_status
ti_sn_bridge_mode_valid(struct drm_bridge *bridge,
			const struct drm_display_info *info,
			const struct drm_display_mode *mode)
{
	/* maximum supported resolution is 4K at 60 fps */
	if (mode->clock > 594000)
		return MODE_CLOCK_HIGH;

	/*
	 * The front and back porch registers are 8 bits, and pulse width
	 * registers are 15 bits, so reject any modes with larger periods.
	 */

	if ((mode->hsync_start - mode->hdisplay) > 0xff)
		return MODE_HBLANK_WIDE;

	if ((mode->vsync_start - mode->vdisplay) > 0xff)
		return MODE_VBLANK_WIDE;

	if ((mode->hsync_end - mode->hsync_start) > 0x7fff)
		return MODE_HSYNC_WIDE;

	if ((mode->vsync_end - mode->vsync_start) > 0x7fff)
		return MODE_VSYNC_WIDE;

	if ((mode->htotal - mode->hsync_end) > 0xff)
		return MODE_HBLANK_WIDE;

	if ((mode->vtotal - mode->vsync_end) > 0xff)
		return MODE_VBLANK_WIDE;

	return MODE_OK;
}

static void ti_sn_bridge_atomic_disable(struct drm_bridge *bridge,
					struct drm_atomic_state *state)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	/* disable video stream */
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, VSTREAM_ENABLE, 0);
}

static void ti_sn_bridge_set_dsi_rate(struct ti_sn65dsi86 *pdata,
				      struct drm_atomic_state *state)
{
	unsigned int bit_rate_mhz, clk_freq_mhz;
	unsigned int val;
	struct drm_display_mode *mode =
		get_new_adjusted_display_mode(&pdata->bridge, state);

	/* set DSIA clk frequency */
	bit_rate_mhz = (mode->clock / 1000) *
			mipi_dsi_pixel_format_to_bpp(pdata->dsi->format);
	clk_freq_mhz = bit_rate_mhz / (pdata->dsi->lanes * 2);

	/* for each increment in val, frequency increases by 5MHz */
	val = (MIN_DSI_CLK_FREQ_MHZ / 5) +
		(((clk_freq_mhz - MIN_DSI_CLK_FREQ_MHZ) / 5) & 0xFF);
	regmap_write(pdata->regmap, SN_DSIA_CLK_FREQ_REG, val);
}

static unsigned int ti_sn_bridge_get_bpp(struct drm_connector *connector)
{
	if (connector->display_info.bpc <= 6)
		return 18;
	else
		return 24;
}

/*
 * LUT index corresponds to register value and
 * LUT values corresponds to dp data rate supported
 * by the bridge in Mbps unit.
 */
static const unsigned int ti_sn_bridge_dp_rate_lut[] = {
	0, 1620, 2160, 2430, 2700, 3240, 4320, 5400
};

static int ti_sn_bridge_calc_min_dp_rate_idx(struct ti_sn65dsi86 *pdata,
					     struct drm_atomic_state *state,
					     unsigned int bpp)
{
	unsigned int bit_rate_khz, dp_rate_mhz;
	unsigned int i;
	struct drm_display_mode *mode =
		get_new_adjusted_display_mode(&pdata->bridge, state);

	/* Calculate minimum bit rate based on our pixel clock. */
	bit_rate_khz = mode->clock * bpp;

	/* Calculate minimum DP data rate, taking 80% as per DP spec */
	dp_rate_mhz = DIV_ROUND_UP(bit_rate_khz * DP_CLK_FUDGE_NUM,
				   1000 * pdata->dp_lanes * DP_CLK_FUDGE_DEN);

	for (i = 1; i < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut) - 1; i++)
		if (ti_sn_bridge_dp_rate_lut[i] >= dp_rate_mhz)
			break;

	return i;
}

static unsigned int ti_sn_bridge_read_valid_rates(struct ti_sn65dsi86 *pdata)
{
	unsigned int valid_rates = 0;
	unsigned int rate_per_200khz;
	unsigned int rate_mhz;
	u8 dpcd_val;
	int ret;
	int i, j;

	ret = drm_dp_dpcd_readb(&pdata->aux, DP_EDP_DPCD_REV, &dpcd_val);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read eDP rev (%d), assuming 1.1\n", ret);
		dpcd_val = DP_EDP_11;
	}

	if (dpcd_val >= DP_EDP_14) {
		/* eDP 1.4 devices must provide a custom table */
		__le16 sink_rates[DP_MAX_SUPPORTED_RATES];

		ret = drm_dp_dpcd_read(&pdata->aux, DP_SUPPORTED_LINK_RATES,
				       sink_rates, sizeof(sink_rates));

		if (ret != sizeof(sink_rates)) {
			DRM_DEV_ERROR(pdata->dev,
				"Can't read supported rate table (%d)\n", ret);

			/* By zeroing we'll fall back to DP_MAX_LINK_RATE. */
			memset(sink_rates, 0, sizeof(sink_rates));
		}

		for (i = 0; i < ARRAY_SIZE(sink_rates); i++) {
			rate_per_200khz = le16_to_cpu(sink_rates[i]);

			if (!rate_per_200khz)
				break;

			rate_mhz = rate_per_200khz * 200 / 1000;
			for (j = 0;
			     j < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut);
			     j++) {
				if (ti_sn_bridge_dp_rate_lut[j] == rate_mhz)
					valid_rates |= BIT(j);
			}
		}

		for (i = 0; i < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut); i++) {
			if (valid_rates & BIT(i))
				return valid_rates;
		}
		DRM_DEV_ERROR(pdata->dev,
			      "No matching eDP rates in table; falling back\n");
	}

	/* On older versions best we can do is use DP_MAX_LINK_RATE */
	ret = drm_dp_dpcd_readb(&pdata->aux, DP_MAX_LINK_RATE, &dpcd_val);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read max rate (%d); assuming 5.4 GHz\n",
			      ret);
		dpcd_val = DP_LINK_BW_5_4;
	}

	switch (dpcd_val) {
	default:
		DRM_DEV_ERROR(pdata->dev,
			      "Unexpected max rate (%#x); assuming 5.4 GHz\n",
			      (int)dpcd_val);
		fallthrough;
	case DP_LINK_BW_5_4:
		valid_rates |= BIT(7);
		fallthrough;
	case DP_LINK_BW_2_7:
		valid_rates |= BIT(4);
		fallthrough;
	case DP_LINK_BW_1_62:
		valid_rates |= BIT(1);
		break;
	}

	return valid_rates;
}

static void ti_sn_bridge_set_video_timings(struct ti_sn65dsi86 *pdata,
					   struct drm_atomic_state *state)
{
	struct drm_display_mode *mode =
		get_new_adjusted_display_mode(&pdata->bridge, state);
	u8 hsync_polarity = 0, vsync_polarity = 0;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		hsync_polarity = CHA_HSYNC_POLARITY;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		vsync_polarity = CHA_VSYNC_POLARITY;

	ti_sn65dsi86_write_u16(pdata, SN_CHA_ACTIVE_LINE_LENGTH_LOW_REG,
			       mode->hdisplay);
	ti_sn65dsi86_write_u16(pdata, SN_CHA_VERTICAL_DISPLAY_SIZE_LOW_REG,
			       mode->vdisplay);
	regmap_write(pdata->regmap, SN_CHA_HSYNC_PULSE_WIDTH_LOW_REG,
		     (mode->hsync_end - mode->hsync_start) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_HSYNC_PULSE_WIDTH_HIGH_REG,
		     (((mode->hsync_end - mode->hsync_start) >> 8) & 0x7F) |
		     hsync_polarity);
	regmap_write(pdata->regmap, SN_CHA_VSYNC_PULSE_WIDTH_LOW_REG,
		     (mode->vsync_end - mode->vsync_start) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VSYNC_PULSE_WIDTH_HIGH_REG,
		     (((mode->vsync_end - mode->vsync_start) >> 8) & 0x7F) |
		     vsync_polarity);

	regmap_write(pdata->regmap, SN_CHA_HORIZONTAL_BACK_PORCH_REG,
		     (mode->htotal - mode->hsync_end) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VERTICAL_BACK_PORCH_REG,
		     (mode->vtotal - mode->vsync_end) & 0xFF);

	regmap_write(pdata->regmap, SN_CHA_HORIZONTAL_FRONT_PORCH_REG,
		     (mode->hsync_start - mode->hdisplay) & 0xFF);
	regmap_write(pdata->regmap, SN_CHA_VERTICAL_FRONT_PORCH_REG,
		     (mode->vsync_start - mode->vdisplay) & 0xFF);

	usleep_range(10000, 10500); /* 10ms delay recommended by spec */
}

static unsigned int ti_sn_get_max_lanes(struct ti_sn65dsi86 *pdata)
{
	u8 data;
	int ret;

	ret = drm_dp_dpcd_readb(&pdata->aux, DP_MAX_LANE_COUNT, &data);
	if (ret != 1) {
		DRM_DEV_ERROR(pdata->dev,
			      "Can't read lane count (%d); assuming 4\n", ret);
		return 4;
	}

	return data & DP_LANE_COUNT_MASK;
}

static int ti_sn_link_training(struct ti_sn65dsi86 *pdata, int dp_rate_idx,
			       const char **last_err_str)
{
	unsigned int val;
	int ret;
	int i;

	/* set dp clk frequency value */
	regmap_update_bits(pdata->regmap, SN_DATARATE_CONFIG_REG,
			   DP_DATARATE_MASK, DP_DATARATE(dp_rate_idx));

	/* enable DP PLL */
	regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 1);

	ret = regmap_read_poll_timeout(pdata->regmap, SN_DPPLL_SRC_REG, val,
				       val & DPPLL_SRC_DP_PLL_LOCK, 1000,
				       50 * 1000);
	if (ret) {
		*last_err_str = "DP_PLL_LOCK polling failed";
		goto exit;
	}

	/*
	 * We'll try to link train several times.  As part of link training
	 * the bridge chip will write DP_SET_POWER_D0 to DP_SET_POWER.  If
	 * the panel isn't ready quite it might respond NAK here which means
	 * we need to try again.
	 */
	for (i = 0; i < SN_LINK_TRAINING_TRIES; i++) {
		/* Semi auto link training mode */
		regmap_write(pdata->regmap, SN_ML_TX_MODE_REG, 0x0A);
		ret = regmap_read_poll_timeout(pdata->regmap, SN_ML_TX_MODE_REG, val,
					       val == ML_TX_MAIN_LINK_OFF ||
					       val == ML_TX_NORMAL_MODE, 1000,
					       500 * 1000);
		if (ret) {
			*last_err_str = "Training complete polling failed";
		} else if (val == ML_TX_MAIN_LINK_OFF) {
			*last_err_str = "Link training failed, link is off";
			ret = -EIO;
			continue;
		}

		break;
	}

	/* If we saw quite a few retries, add a note about it */
	if (!ret && i > SN_LINK_TRAINING_TRIES / 2)
		DRM_DEV_INFO(pdata->dev, "Link training needed %d retries\n", i);

exit:
	/* Disable the PLL if we failed */
	if (ret)
		regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 0);

	return ret;
}

static void ti_sn_bridge_atomic_enable(struct drm_bridge *bridge,
				       struct drm_atomic_state *state)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);
	struct drm_connector *connector;
	const char *last_err_str = "No supported DP rate";
	unsigned int valid_rates;
	int dp_rate_idx;
	unsigned int val;
	int ret = -EINVAL;
	int max_dp_lanes;
	unsigned int bpp;

	connector = drm_atomic_get_new_connector_for_encoder(state,
							     bridge->encoder);
	if (!connector) {
		dev_err_ratelimited(pdata->dev, "Could not get the connector\n");
		return;
	}

	max_dp_lanes = ti_sn_get_max_lanes(pdata);
	pdata->dp_lanes = min(pdata->dp_lanes, max_dp_lanes);

	/* DSI_A lane config */
	val = CHA_DSI_LANES(SN_MAX_DP_LANES - pdata->dsi->lanes);
	regmap_update_bits(pdata->regmap, SN_DSI_LANES_REG,
			   CHA_DSI_LANES_MASK, val);

	regmap_write(pdata->regmap, SN_LN_ASSIGN_REG, pdata->ln_assign);
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, LN_POLRS_MASK,
			   pdata->ln_polrs << LN_POLRS_OFFSET);

	/* set dsi clk frequency value */
	ti_sn_bridge_set_dsi_rate(pdata, state);

	/*
	 * The SN65DSI86 only supports ASSR Display Authentication method and
	 * this method is enabled for eDP panels. An eDP panel must support this
	 * authentication method. We need to enable this method in the eDP panel
	 * at DisplayPort address 0x0010A prior to link training.
	 *
	 * As only ASSR is supported by SN65DSI86, for full DisplayPort displays
	 * we need to disable the scrambler.
	 */
	if (pdata->bridge.type == DRM_MODE_CONNECTOR_eDP) {
		drm_dp_dpcd_writeb(&pdata->aux, DP_EDP_CONFIGURATION_SET,
				   DP_ALTERNATE_SCRAMBLER_RESET_ENABLE);

		regmap_update_bits(pdata->regmap, SN_TRAINING_SETTING_REG,
				   SCRAMBLE_DISABLE, 0);
	} else {
		regmap_update_bits(pdata->regmap, SN_TRAINING_SETTING_REG,
				   SCRAMBLE_DISABLE, SCRAMBLE_DISABLE);
	}

	bpp = ti_sn_bridge_get_bpp(connector);
	/* Set the DP output format (18 bpp or 24 bpp) */
	val = bpp == 18 ? BPP_18_RGB : 0;
	regmap_update_bits(pdata->regmap, SN_DATA_FORMAT_REG, BPP_18_RGB, val);

	/* DP lane config */
	val = DP_NUM_LANES(min(pdata->dp_lanes, 3));
	regmap_update_bits(pdata->regmap, SN_SSC_CONFIG_REG, DP_NUM_LANES_MASK,
			   val);

	valid_rates = ti_sn_bridge_read_valid_rates(pdata);

	/* Train until we run out of rates */
	for (dp_rate_idx = ti_sn_bridge_calc_min_dp_rate_idx(pdata, state, bpp);
	     dp_rate_idx < ARRAY_SIZE(ti_sn_bridge_dp_rate_lut);
	     dp_rate_idx++) {
		if (!(valid_rates & BIT(dp_rate_idx)))
			continue;

		ret = ti_sn_link_training(pdata, dp_rate_idx, &last_err_str);
		if (!ret)
			break;
	}
	if (ret) {
		DRM_DEV_ERROR(pdata->dev, "%s (%d)\n", last_err_str, ret);
		return;
	}

	/* config video parameters */
	ti_sn_bridge_set_video_timings(pdata, state);

	/* enable video stream */
	regmap_update_bits(pdata->regmap, SN_ENH_FRAME_REG, VSTREAM_ENABLE,
			   VSTREAM_ENABLE);
}

static void ti_sn_bridge_atomic_pre_enable(struct drm_bridge *bridge,
					   struct drm_atomic_state *state)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	pm_runtime_get_sync(pdata->dev);

	if (!pdata->refclk)
		ti_sn65dsi86_enable_comms(pdata, state);

	/* td7: min 100 us after enable before DSI data */
	usleep_range(100, 110);
}

static void ti_sn_bridge_atomic_post_disable(struct drm_bridge *bridge,
					     struct drm_atomic_state *state)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	/* semi auto link training mode OFF */
	regmap_write(pdata->regmap, SN_ML_TX_MODE_REG, 0);
	/* Num lanes to 0 as per power sequencing in data sheet */
	regmap_update_bits(pdata->regmap, SN_SSC_CONFIG_REG, DP_NUM_LANES_MASK, 0);
	/* disable DP PLL */
	regmap_write(pdata->regmap, SN_PLL_ENABLE_REG, 0);

	if (!pdata->refclk)
		ti_sn65dsi86_disable_comms(pdata);

	pm_runtime_put_sync(pdata->dev);
}

static enum drm_connector_status
ti_sn_bridge_detect(struct drm_bridge *bridge, struct drm_connector *connector)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);
	int val = 0;

	/*
	 * Runtime reference is grabbed in ti_sn_bridge_hpd_enable()
	 * as the chip won't report HPD just after being powered on.
	 * HPD_DEBOUNCED_STATE reflects correct state only after the
	 * debounce time (~100-400 ms).
	 */

	regmap_read(pdata->regmap, SN_HPD_DISABLE_REG, &val);

	return val & HPD_DEBOUNCED_STATE ? connector_status_connected
					 : connector_status_disconnected;
}

static const struct drm_edid *ti_sn_bridge_edid_read(struct drm_bridge *bridge,
						     struct drm_connector *connector)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	return drm_edid_read_ddc(connector, &pdata->aux.ddc);
}

static void ti_sn65dsi86_debugfs_init(struct drm_bridge *bridge, struct dentry *root)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);
	struct dentry *debugfs;

	debugfs = debugfs_create_dir(dev_name(pdata->dev), root);
	debugfs_create_file("status", 0600, debugfs, pdata, &status_fops);
}

static void ti_sn_bridge_hpd_enable(struct drm_bridge *bridge)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	/*
	 * Device needs to be powered on before reading the HPD state
	 * for reliable hpd detection in ti_sn_bridge_detect() due to
	 * the high debounce time.
	 */

	pm_runtime_get_sync(pdata->dev);
}

static void ti_sn_bridge_hpd_disable(struct drm_bridge *bridge)
{
	struct ti_sn65dsi86 *pdata = bridge_to_ti_sn65dsi86(bridge);

	pm_runtime_put_autosuspend(pdata->dev);
}

static const struct drm_bridge_funcs ti_sn_bridge_funcs = {
	.attach = ti_sn_bridge_attach,
	.detach = ti_sn_bridge_detach,
	.mode_valid = ti_sn_bridge_mode_valid,
	.edid_read = ti_sn_bridge_edid_read,
	.detect = ti_sn_bridge_detect,
	.atomic_pre_enable = ti_sn_bridge_atomic_pre_enable,
	.atomic_enable = ti_sn_bridge_atomic_enable,
	.atomic_disable = ti_sn_bridge_atomic_disable,
	.atomic_post_disable = ti_sn_bridge_atomic_post_disable,
	.atomic_reset = drm_atomic_helper_bridge_reset,
	.atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
	.debugfs_init = ti_sn65dsi86_debugfs_init,
	.hpd_enable = ti_sn_bridge_hpd_enable,
	.hpd_disable = ti_sn_bridge_hpd_disable,
};

static void ti_sn_bridge_parse_lanes(struct ti_sn65dsi86 *pdata,
				     struct device_node *np)
{
	u32 lane_assignments[SN_MAX_DP_LANES] = { 0, 1, 2, 3 };
	u32 lane_polarities[SN_MAX_DP_LANES] = { };
	struct device_node *endpoint;
	u8 ln_assign = 0;
	u8 ln_polrs = 0;
	int dp_lanes;
	int i;

	/*
	 * Read config from the device tree about lane remapping and lane
	 * polarities.  These are optional and we assume identity map and
	 * normal polarity if nothing is specified.  It's OK to specify just
	 * data-lanes but not lane-polarities but not vice versa.
	 *
	 * Error checking is light (we just make sure we don't crash or
	 * buffer overrun) and we assume dts is well formed and specifying
	 * mappings that the hardware supports.
	 */
	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	dp_lanes = drm_of_get_data_lanes_count(endpoint, 1, SN_MAX_DP_LANES);
	if (dp_lanes > 0) {
		of_property_read_u32_array(endpoint, "data-lanes",
					   lane_assignments, dp_lanes);
		of_property_read_u32_array(endpoint, "lane-polarities",
					   lane_polarities, dp_lanes);
	} else {
		dp_lanes = SN_MAX_DP_LANES;
	}
	of_node_put(endpoint);

	/*
	 * Convert into register format.  Loop over all lanes even if
	 * data-lanes had fewer elements so that we nicely initialize
	 * the LN_ASSIGN register.
	 */
	for (i = SN_MAX_DP_LANES - 1; i >= 0; i--) {
		ln_assign = ln_assign << LN_ASSIGN_WIDTH | lane_assignments[i];
		ln_polrs = ln_polrs << 1 | lane_polarities[i];
	}

	/* Stash in our struct for when we power on */
	pdata->dp_lanes = dp_lanes;
	pdata->ln_assign = ln_assign;
	pdata->ln_polrs = ln_polrs;
}

static int ti_sn_bridge_parse_dsi_host(struct ti_sn65dsi86 *pdata)
{
	struct device_node *np = pdata->dev->of_node;

	pdata->host_node = of_graph_get_remote_node(np, 0, 0);

	if (!pdata->host_node) {
		DRM_ERROR("remote dsi host node not found\n");
		return -ENODEV;
	}

	return 0;
}

static int ti_sn_bridge_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);
	struct device_node *np = pdata->dev->of_node;
	int ret;

	pdata->next_bridge = devm_drm_of_get_bridge(&adev->dev, np, 1, 0);
	if (IS_ERR(pdata->next_bridge))
		return dev_err_probe(&adev->dev, PTR_ERR(pdata->next_bridge),
				     "failed to create panel bridge\n");

	ti_sn_bridge_parse_lanes(pdata, np);

	ret = ti_sn_bridge_parse_dsi_host(pdata);
	if (ret)
		return ret;

	pdata->bridge.of_node = np;
	pdata->bridge.type = pdata->next_bridge->type == DRM_MODE_CONNECTOR_DisplayPort
			   ? DRM_MODE_CONNECTOR_DisplayPort : DRM_MODE_CONNECTOR_eDP;

	if (pdata->bridge.type == DRM_MODE_CONNECTOR_DisplayPort) {
		pdata->bridge.ops = DRM_BRIDGE_OP_EDID | DRM_BRIDGE_OP_DETECT |
				    DRM_BRIDGE_OP_HPD;
		/*
		 * If comms were already enabled they would have been enabled
		 * with the wrong value of HPD_DISABLE. Update it now. Comms
		 * could be enabled if anyone is holding a pm_runtime reference
		 * (like if a GPIO is in use). Note that in most cases nobody
		 * is doing AUX channel xfers before the bridge is added so
		 * HPD doesn't _really_ matter then. The only exception is in
		 * the eDP case where the panel wants to read the EDID before
		 * the bridge is added. We always consistently have HPD disabled
		 * for eDP.
		 */
		mutex_lock(&pdata->comms_mutex);
		if (pdata->comms_enabled)
			regmap_update_bits(pdata->regmap, SN_HPD_DISABLE_REG,
					   HPD_DISABLE, 0);
		mutex_unlock(&pdata->comms_mutex);
	}

	drm_bridge_add(&pdata->bridge);

	ret = ti_sn_attach_host(adev, pdata);
	if (ret) {
		dev_err_probe(&adev->dev, ret, "failed to attach dsi host\n");
		goto err_remove_bridge;
	}

	return 0;

err_remove_bridge:
	drm_bridge_remove(&pdata->bridge);
	return ret;
}

static void ti_sn_bridge_remove(struct auxiliary_device *adev)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);

	if (!pdata)
		return;

	drm_bridge_remove(&pdata->bridge);

	of_node_put(pdata->host_node);
}

static const struct auxiliary_device_id ti_sn_bridge_id_table[] = {
	{ .name = "ti_sn65dsi86.bridge", },
	{},
};

static struct auxiliary_driver ti_sn_bridge_driver = {
	.name = "bridge",
	.probe = ti_sn_bridge_probe,
	.remove = ti_sn_bridge_remove,
	.id_table = ti_sn_bridge_id_table,
};

/* -----------------------------------------------------------------------------
 * PWM Controller
 */
#if IS_REACHABLE(CONFIG_PWM)
static int ti_sn_pwm_pin_request(struct ti_sn65dsi86 *pdata)
{
	return atomic_xchg(&pdata->pwm_pin_busy, 1) ? -EBUSY : 0;
}

static void ti_sn_pwm_pin_release(struct ti_sn65dsi86 *pdata)
{
	atomic_set(&pdata->pwm_pin_busy, 0);
}

static struct ti_sn65dsi86 *pwm_chip_to_ti_sn_bridge(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int ti_sn_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ti_sn65dsi86 *pdata = pwm_chip_to_ti_sn_bridge(chip);

	return ti_sn_pwm_pin_request(pdata);
}

static void ti_sn_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ti_sn65dsi86 *pdata = pwm_chip_to_ti_sn_bridge(chip);

	ti_sn_pwm_pin_release(pdata);
}

/*
 * Limitations:
 * - The PWM signal is not driven when the chip is powered down, or in its
 *   reset state and the driver does not implement the "suspend state"
 *   described in the documentation. In order to save power, state->enabled is
 *   interpreted as denoting if the signal is expected to be valid, and is used
 *   to determine if the chip needs to be kept powered.
 * - Changing both period and duty_cycle is not done atomically, neither is the
 *   multi-byte register updates, so the output might briefly be undefined
 *   during update.
 */
static int ti_sn_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct ti_sn65dsi86 *pdata = pwm_chip_to_ti_sn_bridge(chip);
	unsigned int pwm_en_inv;
	unsigned int backlight;
	unsigned int pre_div;
	unsigned int scale;
	u64 period_max;
	u64 period;
	int ret;

	if (!pdata->pwm_enabled) {
		ret = pm_runtime_resume_and_get(pwmchip_parent(chip));
		if (ret < 0)
			return ret;
	}

	if (state->enabled) {
		if (!pdata->pwm_enabled) {
			/*
			 * The chip might have been powered down while we
			 * didn't hold a PM runtime reference, so mux in the
			 * PWM function on the GPIO pin again.
			 */
			ret = regmap_update_bits(pdata->regmap, SN_GPIO_CTRL_REG,
						 SN_GPIO_MUX_MASK << (2 * SN_PWM_GPIO_IDX),
						 SN_GPIO_MUX_SPECIAL << (2 * SN_PWM_GPIO_IDX));
			if (ret) {
				dev_err(pwmchip_parent(chip), "failed to mux in PWM function\n");
				goto out;
			}
		}

		/*
		 * Per the datasheet the PWM frequency is given by:
		 *
		 *                          REFCLK_FREQ
		 *   PWM_FREQ = -----------------------------------
		 *               PWM_PRE_DIV * BACKLIGHT_SCALE + 1
		 *
		 * However, after careful review the author is convinced that
		 * the documentation has lost some parenthesis around
		 * "BACKLIGHT_SCALE + 1".
		 *
		 * With the period T_pwm = 1/PWM_FREQ this can be written:
		 *
		 *   T_pwm * REFCLK_FREQ = PWM_PRE_DIV * (BACKLIGHT_SCALE + 1)
		 *
		 * In order to keep BACKLIGHT_SCALE within its 16 bits,
		 * PWM_PRE_DIV must be:
		 *
		 *                     T_pwm * REFCLK_FREQ
		 *   PWM_PRE_DIV >= -------------------------
		 *                   BACKLIGHT_SCALE_MAX + 1
		 *
		 * To simplify the search and to favour higher resolution of
		 * the duty cycle over accuracy of the period, the lowest
		 * possible PWM_PRE_DIV is used. Finally the scale is
		 * calculated as:
		 *
		 *                      T_pwm * REFCLK_FREQ
		 *   BACKLIGHT_SCALE = ---------------------- - 1
		 *                          PWM_PRE_DIV
		 *
		 * Here T_pwm is represented in seconds, so appropriate scaling
		 * to nanoseconds is necessary.
		 */

		/* Minimum T_pwm is 1 / REFCLK_FREQ */
		if (state->period <= NSEC_PER_SEC / pdata->pwm_refclk_freq) {
			ret = -EINVAL;
			goto out;
		}

		/*
		 * Maximum T_pwm is 255 * (65535 + 1) / REFCLK_FREQ
		 * Limit period to this to avoid overflows
		 */
		period_max = div_u64((u64)NSEC_PER_SEC * 255 * (65535 + 1),
				     pdata->pwm_refclk_freq);
		period = min(state->period, period_max);

		pre_div = DIV64_U64_ROUND_UP(period * pdata->pwm_refclk_freq,
					     (u64)NSEC_PER_SEC * (BACKLIGHT_SCALE_MAX + 1));
		scale = div64_u64(period * pdata->pwm_refclk_freq, (u64)NSEC_PER_SEC * pre_div) - 1;

		/*
		 * The documentation has the duty ratio given as:
		 *
		 *     duty          BACKLIGHT
		 *   ------- = ---------------------
		 *    period    BACKLIGHT_SCALE + 1
		 *
		 * Solve for BACKLIGHT, substituting BACKLIGHT_SCALE according
		 * to definition above and adjusting for nanosecond
		 * representation of duty cycle gives us:
		 */
		backlight = div64_u64(state->duty_cycle * pdata->pwm_refclk_freq,
				      (u64)NSEC_PER_SEC * pre_div);
		if (backlight > scale)
			backlight = scale;

		ret = regmap_write(pdata->regmap, SN_PWM_PRE_DIV_REG, pre_div);
		if (ret) {
			dev_err(pwmchip_parent(chip), "failed to update PWM_PRE_DIV\n");
			goto out;
		}

		ti_sn65dsi86_write_u16(pdata, SN_BACKLIGHT_SCALE_REG, scale);
		ti_sn65dsi86_write_u16(pdata, SN_BACKLIGHT_REG, backlight);
	}

	pwm_en_inv = FIELD_PREP(SN_PWM_EN_MASK, state->enabled) |
		     FIELD_PREP(SN_PWM_INV_MASK, state->polarity == PWM_POLARITY_INVERSED);
	ret = regmap_write(pdata->regmap, SN_PWM_EN_INV_REG, pwm_en_inv);
	if (ret) {
		dev_err(pwmchip_parent(chip), "failed to update PWM_EN/PWM_INV\n");
		goto out;
	}

	pdata->pwm_enabled = state->enabled;
out:

	if (!pdata->pwm_enabled)
		pm_runtime_put_sync(pwmchip_parent(chip));

	return ret;
}

static int ti_sn_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct ti_sn65dsi86 *pdata = pwm_chip_to_ti_sn_bridge(chip);
	unsigned int pwm_en_inv;
	unsigned int pre_div;
	u16 backlight;
	u16 scale;
	int ret;

	ret = regmap_read(pdata->regmap, SN_PWM_EN_INV_REG, &pwm_en_inv);
	if (ret)
		return ret;

	ret = ti_sn65dsi86_read_u16(pdata, SN_BACKLIGHT_SCALE_REG, &scale);
	if (ret)
		return ret;

	ret = ti_sn65dsi86_read_u16(pdata, SN_BACKLIGHT_REG, &backlight);
	if (ret)
		return ret;

	ret = regmap_read(pdata->regmap, SN_PWM_PRE_DIV_REG, &pre_div);
	if (ret)
		return ret;

	state->enabled = FIELD_GET(SN_PWM_EN_MASK, pwm_en_inv);
	if (FIELD_GET(SN_PWM_INV_MASK, pwm_en_inv))
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	state->period = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * pre_div * (scale + 1),
					 pdata->pwm_refclk_freq);
	state->duty_cycle = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * pre_div * backlight,
					     pdata->pwm_refclk_freq);

	if (state->duty_cycle > state->period)
		state->duty_cycle = state->period;

	return 0;
}

static const struct pwm_ops ti_sn_pwm_ops = {
	.request = ti_sn_pwm_request,
	.free = ti_sn_pwm_free,
	.apply = ti_sn_pwm_apply,
	.get_state = ti_sn_pwm_get_state,
};

static int ti_sn_pwm_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct pwm_chip *chip;
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);

	pdata->pchip = chip = devm_pwmchip_alloc(&adev->dev, 1, 0);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	pwmchip_set_drvdata(chip, pdata);

	chip->ops = &ti_sn_pwm_ops;
	chip->of_xlate = of_pwm_single_xlate;

	devm_pm_runtime_enable(&adev->dev);

	return pwmchip_add(chip);
}

static void ti_sn_pwm_remove(struct auxiliary_device *adev)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);

	pwmchip_remove(pdata->pchip);

	if (pdata->pwm_enabled)
		pm_runtime_put_sync(&adev->dev);
}

static const struct auxiliary_device_id ti_sn_pwm_id_table[] = {
	{ .name = "ti_sn65dsi86.pwm", },
	{},
};

static struct auxiliary_driver ti_sn_pwm_driver = {
	.name = "pwm",
	.probe = ti_sn_pwm_probe,
	.remove = ti_sn_pwm_remove,
	.id_table = ti_sn_pwm_id_table,
};

static int __init ti_sn_pwm_register(void)
{
	return auxiliary_driver_register(&ti_sn_pwm_driver);
}

static void ti_sn_pwm_unregister(void)
{
	auxiliary_driver_unregister(&ti_sn_pwm_driver);
}

#else
static inline int __maybe_unused ti_sn_pwm_pin_request(struct ti_sn65dsi86 *pdata) { return 0; }
static inline void __maybe_unused ti_sn_pwm_pin_release(struct ti_sn65dsi86 *pdata) {}

static inline int ti_sn_pwm_register(void) { return 0; }
static inline void ti_sn_pwm_unregister(void) {}
#endif

/* -----------------------------------------------------------------------------
 * GPIO Controller
 */
#if defined(CONFIG_OF_GPIO)

static int tn_sn_bridge_of_xlate(struct gpio_chip *chip,
				 const struct of_phandle_args *gpiospec,
				 u32 *flags)
{
	if (WARN_ON(gpiospec->args_count < chip->of_gpio_n_cells))
		return -EINVAL;

	if (gpiospec->args[0] > chip->ngpio || gpiospec->args[0] < 1)
		return -EINVAL;

	if (flags)
		*flags = gpiospec->args[1];

	return gpiospec->args[0] - SN_GPIO_PHYSICAL_OFFSET;
}

static int ti_sn_bridge_gpio_get_direction(struct gpio_chip *chip,
					   unsigned int offset)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);

	/*
	 * We already have to keep track of the direction because we use
	 * that to figure out whether we've powered the device.  We can
	 * just return that rather than (maybe) powering up the device
	 * to ask its direction.
	 */
	return test_bit(offset, pdata->gchip_output) ?
		GPIO_LINE_DIRECTION_OUT : GPIO_LINE_DIRECTION_IN;
}

static int ti_sn_bridge_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);
	unsigned int val;
	int ret;

	/*
	 * When the pin is an input we don't forcibly keep the bridge
	 * powered--we just power it on to read the pin.  NOTE: part of
	 * the reason this works is that the bridge defaults (when
	 * powered back on) to all 4 GPIOs being configured as GPIO input.
	 * Also note that if something else is keeping the chip powered the
	 * pm_runtime functions are lightweight increments of a refcount.
	 */
	pm_runtime_get_sync(pdata->dev);
	ret = regmap_read(pdata->regmap, SN_GPIO_IO_REG, &val);
	pm_runtime_put_autosuspend(pdata->dev);

	if (ret)
		return ret;

	return !!(val & BIT(SN_GPIO_INPUT_SHIFT + offset));
}

static int ti_sn_bridge_gpio_set(struct gpio_chip *chip, unsigned int offset,
				 int val)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);

	val &= 1;
	return regmap_update_bits(pdata->regmap, SN_GPIO_IO_REG,
				  BIT(SN_GPIO_OUTPUT_SHIFT + offset),
				  val << (SN_GPIO_OUTPUT_SHIFT + offset));
}

static int ti_sn_bridge_gpio_direction_input(struct gpio_chip *chip,
					     unsigned int offset)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);
	int shift = offset * 2;
	int ret;

	if (!test_and_clear_bit(offset, pdata->gchip_output))
		return 0;

	ret = regmap_update_bits(pdata->regmap, SN_GPIO_CTRL_REG,
				 SN_GPIO_MUX_MASK << shift,
				 SN_GPIO_MUX_INPUT << shift);
	if (ret) {
		set_bit(offset, pdata->gchip_output);
		return ret;
	}

	/*
	 * NOTE: if nobody else is powering the device this may fully power
	 * it off and when it comes back it will have lost all state, but
	 * that's OK because the default is input and we're now an input.
	 */
	pm_runtime_put_autosuspend(pdata->dev);

	return 0;
}

static int ti_sn_bridge_gpio_direction_output(struct gpio_chip *chip,
					      unsigned int offset, int val)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);
	int shift = offset * 2;
	int ret;

	if (test_and_set_bit(offset, pdata->gchip_output))
		return 0;

	pm_runtime_get_sync(pdata->dev);

	/* Set value first to avoid glitching */
	ti_sn_bridge_gpio_set(chip, offset, val);

	/* Set direction */
	ret = regmap_update_bits(pdata->regmap, SN_GPIO_CTRL_REG,
				 SN_GPIO_MUX_MASK << shift,
				 SN_GPIO_MUX_OUTPUT << shift);
	if (ret) {
		clear_bit(offset, pdata->gchip_output);
		pm_runtime_put_autosuspend(pdata->dev);
	}

	return ret;
}

static int ti_sn_bridge_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);

	if (offset == SN_PWM_GPIO_IDX)
		return ti_sn_pwm_pin_request(pdata);

	return 0;
}

static void ti_sn_bridge_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct ti_sn65dsi86 *pdata = gpiochip_get_data(chip);

	/* We won't keep pm_runtime if we're input, so switch there on free */
	ti_sn_bridge_gpio_direction_input(chip, offset);

	if (offset == SN_PWM_GPIO_IDX)
		ti_sn_pwm_pin_release(pdata);
}

static const char * const ti_sn_bridge_gpio_names[SN_NUM_GPIOS] = {
	"GPIO1", "GPIO2", "GPIO3", "GPIO4"
};

static int ti_sn_gpio_probe(struct auxiliary_device *adev,
			    const struct auxiliary_device_id *id)
{
	struct ti_sn65dsi86 *pdata = dev_get_drvdata(adev->dev.parent);
	int ret;

	/* Only init if someone is going to use us as a GPIO controller */
	if (!of_property_read_bool(pdata->dev->of_node, "gpio-controller"))
		return 0;

	pdata->gchip.label = dev_name(pdata->dev);
	pdata->gchip.parent = pdata->dev;
	pdata->gchip.owner = THIS_MODULE;
	pdata->gchip.of_xlate = tn_sn_bridge_of_xlate;
	pdata->gchip.of_gpio_n_cells = 2;
	pdata->gchip.request = ti_sn_bridge_gpio_request;
	pdata->gchip.free = ti_sn_bridge_gpio_free;
	pdata->gchip.get_direction = ti_sn_bridge_gpio_get_direction;
	pdata->gchip.direction_input = ti_sn_bridge_gpio_direction_input;
	pdata->gchip.direction_output = ti_sn_bridge_gpio_direction_output;
	pdata->gchip.get = ti_sn_bridge_gpio_get;
	pdata->gchip.set = ti_sn_bridge_gpio_set;
	pdata->gchip.can_sleep = true;
	pdata->gchip.names = ti_sn_bridge_gpio_names;
	pdata->gchip.ngpio = SN_NUM_GPIOS;
	pdata->gchip.base = -1;
	ret = devm_gpiochip_add_data(&adev->dev, &pdata->gchip, pdata);
	if (ret)
		dev_err(pdata->dev, "can't add gpio chip\n");

	return ret;
}

static const struct auxiliary_device_id ti_sn_gpio_id_table[] = {
	{ .name = "ti_sn65dsi86.gpio", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, ti_sn_gpio_id_table);

static struct auxiliary_driver ti_sn_gpio_driver = {
	.name = "gpio",
	.probe = ti_sn_gpio_probe,
	.id_table = ti_sn_gpio_id_table,
};

static int __init ti_sn_gpio_register(void)
{
	return auxiliary_driver_register(&ti_sn_gpio_driver);
}

static void ti_sn_gpio_unregister(void)
{
	auxiliary_driver_unregister(&ti_sn_gpio_driver);
}

#else

static inline int ti_sn_gpio_register(void) { return 0; }
static inline void ti_sn_gpio_unregister(void) {}

#endif

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static void ti_sn65dsi86_runtime_disable(void *data)
{
	pm_runtime_dont_use_autosuspend(data);
	pm_runtime_disable(data);
}

static int ti_sn65dsi86_parse_regulators(struct ti_sn65dsi86 *pdata)
{
	unsigned int i;
	const char * const ti_sn_bridge_supply_names[] = {
		"vcca", "vcc", "vccio", "vpll",
	};

	for (i = 0; i < SN_REGULATOR_SUPPLY_NUM; i++)
		pdata->supplies[i].supply = ti_sn_bridge_supply_names[i];

	return devm_regulator_bulk_get(pdata->dev, SN_REGULATOR_SUPPLY_NUM,
				       pdata->supplies);
}

static int ti_sn65dsi86_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ti_sn65dsi86 *pdata;
	u8 id_buf[8];
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		DRM_ERROR("device doesn't support I2C\n");
		return -ENODEV;
	}

	pdata = devm_drm_bridge_alloc(dev, struct ti_sn65dsi86, bridge, &ti_sn_bridge_funcs);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	dev_set_drvdata(dev, pdata);
	pdata->dev = dev;

	mutex_init(&pdata->comms_mutex);

	pdata->regmap = devm_regmap_init_i2c(client,
					     &ti_sn65dsi86_regmap_config);
	if (IS_ERR(pdata->regmap))
		return dev_err_probe(dev, PTR_ERR(pdata->regmap),
				     "regmap i2c init failed\n");

	pdata->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						     GPIOD_OUT_LOW);
	if (IS_ERR(pdata->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(pdata->enable_gpio),
				     "failed to get enable gpio from DT\n");

	ret = ti_sn65dsi86_parse_regulators(pdata);
	if (ret)
		return dev_err_probe(dev, ret, "failed to parse regulators\n");

	pdata->refclk = devm_clk_get_optional(dev, "refclk");
	if (IS_ERR(pdata->refclk))
		return dev_err_probe(dev, PTR_ERR(pdata->refclk),
				     "failed to get reference clock\n");

	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(pdata->dev, 500);
	pm_runtime_use_autosuspend(pdata->dev);
	ret = devm_add_action_or_reset(dev, ti_sn65dsi86_runtime_disable, dev);
	if (ret)
		return ret;

	pm_runtime_get_sync(dev);
	ret = regmap_bulk_read(pdata->regmap, SN_DEVICE_ID_REGS, id_buf, ARRAY_SIZE(id_buf));
	pm_runtime_put_autosuspend(dev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read device id\n");

	/* The ID string is stored backwards */
	if (strncmp(id_buf, "68ISD   ", ARRAY_SIZE(id_buf)))
		return dev_err_probe(dev, -EOPNOTSUPP, "unsupported device id\n");

	/*
	 * Break ourselves up into a collection of aux devices. The only real
	 * motiviation here is to solve the chicken-and-egg problem of probe
	 * ordering. The bridge wants the panel to be there when it probes.
	 * The panel wants its HPD GPIO (provided by sn65dsi86 on some boards)
	 * when it probes. The panel and maybe backlight might want the DDC
	 * bus or the pwm_chip. Having sub-devices allows the some sub devices
	 * to finish probing even if others return -EPROBE_DEFER and gets us
	 * around the problems.
	 */

	if (IS_ENABLED(CONFIG_OF_GPIO)) {
		ret = ti_sn65dsi86_add_aux_device(pdata, &pdata->gpio_aux, "gpio");
		if (ret)
			return ret;
	}

	if (IS_REACHABLE(CONFIG_PWM)) {
		ret = ti_sn65dsi86_add_aux_device(pdata, &pdata->pwm_aux, "pwm");
		if (ret)
			return ret;
	}

	/*
	 * NOTE: At the end of the AUX channel probe we'll add the aux device
	 * for the bridge. This is because the bridge can't be used until the
	 * AUX channel is there and this is a very simple solution to the
	 * dependency problem.
	 */
	return ti_sn65dsi86_add_aux_device(pdata, &pdata->aux_aux, "aux");
}

static const struct i2c_device_id ti_sn65dsi86_id[] = {
	{ "ti,sn65dsi86" },
	{}
};
MODULE_DEVICE_TABLE(i2c, ti_sn65dsi86_id);

static const struct of_device_id ti_sn65dsi86_match_table[] = {
	{.compatible = "ti,sn65dsi86"},
	{},
};
MODULE_DEVICE_TABLE(of, ti_sn65dsi86_match_table);

static struct i2c_driver ti_sn65dsi86_driver = {
	.driver = {
		.name = "ti_sn65dsi86",
		.of_match_table = ti_sn65dsi86_match_table,
		.pm = &ti_sn65dsi86_pm_ops,
	},
	.probe = ti_sn65dsi86_probe,
	.id_table = ti_sn65dsi86_id,
};

static int __init ti_sn65dsi86_init(void)
{
	int ret;

	ret = i2c_add_driver(&ti_sn65dsi86_driver);
	if (ret)
		return ret;

	ret = ti_sn_gpio_register();
	if (ret)
		goto err_main_was_registered;

	ret = ti_sn_pwm_register();
	if (ret)
		goto err_gpio_was_registered;

	ret = auxiliary_driver_register(&ti_sn_aux_driver);
	if (ret)
		goto err_pwm_was_registered;

	ret = auxiliary_driver_register(&ti_sn_bridge_driver);
	if (ret)
		goto err_aux_was_registered;

	return 0;

err_aux_was_registered:
	auxiliary_driver_unregister(&ti_sn_aux_driver);
err_pwm_was_registered:
	ti_sn_pwm_unregister();
err_gpio_was_registered:
	ti_sn_gpio_unregister();
err_main_was_registered:
	i2c_del_driver(&ti_sn65dsi86_driver);

	return ret;
}
module_init(ti_sn65dsi86_init);

static void __exit ti_sn65dsi86_exit(void)
{
	auxiliary_driver_unregister(&ti_sn_bridge_driver);
	auxiliary_driver_unregister(&ti_sn_aux_driver);
	ti_sn_pwm_unregister();
	ti_sn_gpio_unregister();
	i2c_del_driver(&ti_sn65dsi86_driver);
}
module_exit(ti_sn65dsi86_exit);

MODULE_AUTHOR("Sandeep Panda <spanda@codeaurora.org>");
MODULE_DESCRIPTION("sn65dsi86 DSI to eDP bridge driver");
MODULE_LICENSE("GPL v2");
