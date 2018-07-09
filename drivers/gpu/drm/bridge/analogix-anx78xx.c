/*
 * Copyright(c) 2016, Analogix Semiconductor.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based on anx7808 driver obtained from chromeos with copyright:
 * Copyright(c) 2013, Google Inc.
 *
 */
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/types.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_edid.h>

#include "analogix-anx78xx.h"

#define I2C_NUM_ADDRESSES	5
#define I2C_IDX_TX_P0		0
#define I2C_IDX_TX_P1		1
#define I2C_IDX_TX_P2		2
#define I2C_IDX_RX_P0		3
#define I2C_IDX_RX_P1		4

#define XTAL_CLK		270 /* 27M */
#define AUX_CH_BUFFER_SIZE	16
#define AUX_WAIT_TIMEOUT_MS	15

static const u8 anx78xx_i2c_addresses[] = {
	[I2C_IDX_TX_P0] = TX_P0,
	[I2C_IDX_TX_P1] = TX_P1,
	[I2C_IDX_TX_P2] = TX_P2,
	[I2C_IDX_RX_P0] = RX_P0,
	[I2C_IDX_RX_P1] = RX_P1,
};

struct anx78xx_platform_data {
	struct regulator *dvdd10;
	struct gpio_desc *gpiod_hpd;
	struct gpio_desc *gpiod_pd;
	struct gpio_desc *gpiod_reset;

	int hpd_irq;
	int intp_irq;
};

struct anx78xx {
	struct drm_dp_aux aux;
	struct drm_bridge bridge;
	struct i2c_client *client;
	struct edid *edid;
	struct drm_connector connector;
	struct drm_dp_link link;
	struct anx78xx_platform_data pdata;
	struct mutex lock;

	/*
	 * I2C Slave addresses of ANX7814 are mapped as TX_P0, TX_P1, TX_P2,
	 * RX_P0 and RX_P1.
	 */
	struct i2c_client *i2c_dummy[I2C_NUM_ADDRESSES];
	struct regmap *map[I2C_NUM_ADDRESSES];

	u16 chipid;
	u8 dpcd[DP_RECEIVER_CAP_SIZE];

	bool powered;
};

static inline struct anx78xx *connector_to_anx78xx(struct drm_connector *c)
{
	return container_of(c, struct anx78xx, connector);
}

static inline struct anx78xx *bridge_to_anx78xx(struct drm_bridge *bridge)
{
	return container_of(bridge, struct anx78xx, bridge);
}

static int anx78xx_set_bits(struct regmap *map, u8 reg, u8 mask)
{
	return regmap_update_bits(map, reg, mask, mask);
}

static int anx78xx_clear_bits(struct regmap *map, u8 reg, u8 mask)
{
	return regmap_update_bits(map, reg, mask, 0);
}

static bool anx78xx_aux_op_finished(struct anx78xx *anx78xx)
{
	unsigned int value;
	int err;

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P0], SP_DP_AUX_CH_CTRL2_REG,
			  &value);
	if (err < 0)
		return false;

	return (value & SP_AUX_EN) == 0;
}

static int anx78xx_aux_wait(struct anx78xx *anx78xx)
{
	unsigned long timeout;
	unsigned int status;
	int err;

	timeout = jiffies + msecs_to_jiffies(AUX_WAIT_TIMEOUT_MS) + 1;

	while (!anx78xx_aux_op_finished(anx78xx)) {
		if (time_after(jiffies, timeout)) {
			if (!anx78xx_aux_op_finished(anx78xx)) {
				DRM_ERROR("Timed out waiting AUX to finish\n");
				return -ETIMEDOUT;
			}

			break;
		}

		usleep_range(1000, 2000);
	}

	/* Read the AUX channel access status */
	err = regmap_read(anx78xx->map[I2C_IDX_TX_P0], SP_AUX_CH_STATUS_REG,
			  &status);
	if (err < 0) {
		DRM_ERROR("Failed to read from AUX channel: %d\n", err);
		return err;
	}

	if (status & SP_AUX_STATUS) {
		DRM_ERROR("Failed to wait for AUX channel (status: %02x)\n",
			  status);
		return -ETIMEDOUT;
	}

	return 0;
}

static int anx78xx_aux_address(struct anx78xx *anx78xx, unsigned int addr)
{
	int err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_AUX_ADDR_7_0_REG,
			   addr & 0xff);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_AUX_ADDR_15_8_REG,
			   (addr & 0xff00) >> 8);
	if (err)
		return err;

	/*
	 * DP AUX CH Address Register #2, only update bits[3:0]
	 * [7:4] RESERVED
	 * [3:0] AUX_ADDR[19:16], Register control AUX CH address.
	 */
	err = regmap_update_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_AUX_ADDR_19_16_REG,
				 SP_AUX_ADDR_19_16_MASK,
				 (addr & 0xf0000) >> 16);

	if (err)
		return err;

	return 0;
}

static ssize_t anx78xx_aux_transfer(struct drm_dp_aux *aux,
				    struct drm_dp_aux_msg *msg)
{
	struct anx78xx *anx78xx = container_of(aux, struct anx78xx, aux);
	u8 ctrl1 = msg->request;
	u8 ctrl2 = SP_AUX_EN;
	u8 *buffer = msg->buffer;
	int err;

	/* The DP AUX transmit and receive buffer has 16 bytes. */
	if (WARN_ON(msg->size > AUX_CH_BUFFER_SIZE))
		return -E2BIG;

	/* Zero-sized messages specify address-only transactions. */
	if (msg->size < 1)
		ctrl2 |= SP_ADDR_ONLY;
	else	/* For non-zero-sized set the length field. */
		ctrl1 |= (msg->size - 1) << SP_AUX_LENGTH_SHIFT;

	if ((msg->request & DP_AUX_I2C_READ) == 0) {
		/* When WRITE | MOT write values to data buffer */
		err = regmap_bulk_write(anx78xx->map[I2C_IDX_TX_P0],
					SP_DP_BUF_DATA0_REG, buffer,
					msg->size);
		if (err)
			return err;
	}

	/* Write address and request */
	err = anx78xx_aux_address(anx78xx, msg->address);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_DP_AUX_CH_CTRL1_REG,
			   ctrl1);
	if (err)
		return err;

	/* Start transaction */
	err = regmap_update_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_DP_AUX_CH_CTRL2_REG, SP_ADDR_ONLY |
				 SP_AUX_EN, ctrl2);
	if (err)
		return err;

	err = anx78xx_aux_wait(anx78xx);
	if (err)
		return err;

	msg->reply = DP_AUX_I2C_REPLY_ACK;

	if ((msg->size > 0) && (msg->request & DP_AUX_I2C_READ)) {
		/* Read values from data buffer */
		err = regmap_bulk_read(anx78xx->map[I2C_IDX_TX_P0],
				       SP_DP_BUF_DATA0_REG, buffer,
				       msg->size);
		if (err)
			return err;
	}

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_DP_AUX_CH_CTRL2_REG, SP_ADDR_ONLY);
	if (err)
		return err;

	return msg->size;
}

static int anx78xx_set_hpd(struct anx78xx *anx78xx)
{
	int err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_RX_P0],
				 SP_TMDS_CTRL_BASE + 7, SP_PD_RT);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL3_REG,
			       SP_HPD_OUT);
	if (err)
		return err;

	return 0;
}

static int anx78xx_clear_hpd(struct anx78xx *anx78xx)
{
	int err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL3_REG,
				 SP_HPD_OUT);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0],
			       SP_TMDS_CTRL_BASE + 7, SP_PD_RT);
	if (err)
		return err;

	return 0;
}

static const struct reg_sequence tmds_phy_initialization[] = {
	{ SP_TMDS_CTRL_BASE +  1, 0x90 },
	{ SP_TMDS_CTRL_BASE +  2, 0xa9 },
	{ SP_TMDS_CTRL_BASE +  6, 0x92 },
	{ SP_TMDS_CTRL_BASE +  7, 0x80 },
	{ SP_TMDS_CTRL_BASE + 20, 0xf2 },
	{ SP_TMDS_CTRL_BASE + 22, 0xc4 },
	{ SP_TMDS_CTRL_BASE + 23, 0x18 },
};

static int anx78xx_rx_initialization(struct anx78xx *anx78xx)
{
	int err;

	err = regmap_write(anx78xx->map[I2C_IDX_RX_P0], SP_HDMI_MUTE_CTRL_REG,
			   SP_AUD_MUTE | SP_VID_MUTE);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0], SP_CHIP_CTRL_REG,
			       SP_MAN_HDMI5V_DET | SP_PLLLOCK_CKDT_EN |
			       SP_DIGITAL_CKDT_EN);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0],
			       SP_SOFTWARE_RESET1_REG, SP_HDCP_MAN_RST |
			       SP_SW_MAN_RST | SP_TMDS_RST | SP_VIDEO_RST);
	if (err)
		return err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_RX_P0],
				 SP_SOFTWARE_RESET1_REG, SP_HDCP_MAN_RST |
				 SP_SW_MAN_RST | SP_TMDS_RST | SP_VIDEO_RST);
	if (err)
		return err;

	/* Sync detect change, GP set mute */
	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0],
			       SP_AUD_EXCEPTION_ENABLE_BASE + 1, BIT(5) |
			       BIT(6));
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0],
			       SP_AUD_EXCEPTION_ENABLE_BASE + 3,
			       SP_AEC_EN21);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0], SP_AUDVID_CTRL_REG,
			       SP_AVC_EN | SP_AAC_OE | SP_AAC_EN);
	if (err)
		return err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_RX_P0],
				 SP_SYSTEM_POWER_DOWN1_REG, SP_PWDN_CTRL);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_RX_P0],
			       SP_VID_DATA_RANGE_CTRL_REG, SP_R2Y_INPUT_LIMIT);
	if (err)
		return err;

	/* Enable DDC stretch */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_DP_EXTRA_I2C_DEV_ADDR_REG, SP_I2C_EXTRA_ADDR);
	if (err)
		return err;

	/* TMDS phy initialization */
	err = regmap_multi_reg_write(anx78xx->map[I2C_IDX_RX_P0],
				     tmds_phy_initialization,
				     ARRAY_SIZE(tmds_phy_initialization));
	if (err)
		return err;

	err = anx78xx_clear_hpd(anx78xx);
	if (err)
		return err;

	return 0;
}

static const u8 dp_tx_output_precise_tune_bits[20] = {
	0x01, 0x03, 0x07, 0x7f, 0x71, 0x6b, 0x7f,
	0x73, 0x7f, 0x7f, 0x00, 0x00, 0x00, 0x00,
	0x0c, 0x42, 0x1e, 0x3e, 0x72, 0x7e,
};

static int anx78xx_link_phy_initialization(struct anx78xx *anx78xx)
{
	int err;

	/*
	 * REVISIT : It is writing to a RESERVED bits in Analog Control 0
	 * register.
	 */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2], SP_ANALOG_CTRL0_REG,
			   0x02);
	if (err)
		return err;

	/*
	 * Write DP TX output emphasis precise tune bits.
	 */
	err = regmap_bulk_write(anx78xx->map[I2C_IDX_TX_P1],
				SP_DP_TX_LT_CTRL0_REG,
				dp_tx_output_precise_tune_bits,
				ARRAY_SIZE(dp_tx_output_precise_tune_bits));

	if (err)
		return err;

	return 0;
}

static int anx78xx_xtal_clk_sel(struct anx78xx *anx78xx)
{
	unsigned int value;
	int err;

	err = regmap_update_bits(anx78xx->map[I2C_IDX_TX_P2],
				 SP_ANALOG_DEBUG2_REG,
				 SP_XTAL_FRQ | SP_FORCE_SW_OFF_BYPASS,
				 SP_XTAL_FRQ_27M);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_DP_AUX_CH_CTRL3_REG,
			   XTAL_CLK & SP_WAIT_COUNTER_7_0_MASK);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_DP_AUX_CH_CTRL4_REG,
			   ((XTAL_CLK & 0xff00) >> 2) | (XTAL_CLK / 10));
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_I2C_GEN_10US_TIMER0_REG, XTAL_CLK & 0xff);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_I2C_GEN_10US_TIMER1_REG,
			   (XTAL_CLK & 0xff00) >> 8);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_AUX_MISC_CTRL_REG,
			   XTAL_CLK / 10 - 1);
	if (err)
		return err;

	err = regmap_read(anx78xx->map[I2C_IDX_RX_P0],
			  SP_HDMI_US_TIMER_CTRL_REG,
			  &value);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_RX_P0],
			   SP_HDMI_US_TIMER_CTRL_REG,
			   (value & SP_MS_TIMER_MARGIN_10_8_MASK) |
			   ((((XTAL_CLK / 10) >> 1) - 2) << 3));
	if (err)
		return err;

	return 0;
}

static const struct reg_sequence otp_key_protect[] = {
	{ SP_OTP_KEY_PROTECT1_REG, SP_OTP_PSW1 },
	{ SP_OTP_KEY_PROTECT2_REG, SP_OTP_PSW2 },
	{ SP_OTP_KEY_PROTECT3_REG, SP_OTP_PSW3 },
};

static int anx78xx_tx_initialization(struct anx78xx *anx78xx)
{
	int err;

	/* Set terminal resistor to 50 ohm */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_DP_AUX_CH_CTRL2_REG,
			   0x30);
	if (err)
		return err;

	/* Enable aux double diff output */
	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_AUX_CH_CTRL2_REG, 0x08);
	if (err)
		return err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_DP_HDCP_CTRL_REG, SP_AUTO_EN |
				 SP_AUTO_START);
	if (err)
		return err;

	err = regmap_multi_reg_write(anx78xx->map[I2C_IDX_TX_P0],
				     otp_key_protect,
				     ARRAY_SIZE(otp_key_protect));
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_HDCP_KEY_COMMAND_REG, SP_DISABLE_SYNC_HDCP);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL8_REG,
			   SP_VID_VRES_TH);
	if (err)
		return err;

	/*
	 * DP HDCP auto authentication wait timer (when downstream starts to
	 * auth, DP side will wait for this period then do auth automatically)
	 */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_HDCP_AUTO_TIMER_REG,
			   0x00);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_HDCP_CTRL_REG, SP_LINK_POLLING);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_LINK_DEBUG_CTRL_REG, SP_M_VID_DEBUG);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2],
			       SP_ANALOG_DEBUG2_REG, SP_POWERON_TIME_1P5MS);
	if (err)
		return err;

	err = anx78xx_xtal_clk_sel(anx78xx);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_AUX_DEFER_CTRL_REG,
			   SP_DEFER_CTRL_EN | 0x0c);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_POLLING_CTRL_REG,
			       SP_AUTO_POLLING_DISABLE);
	if (err)
		return err;

	/*
	 * Short the link integrity check timer to speed up bstatus
	 * polling for HDCP CTS item 1A-07
	 */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_HDCP_LINK_CHECK_TIMER_REG, 0x1d);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_MISC_CTRL_REG, SP_EQ_TRAINING_LOOP);
	if (err)
		return err;

	/* Power down the main link by default */
	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_ANALOG_POWER_DOWN_REG, SP_CH0_PD);
	if (err)
		return err;

	err = anx78xx_link_phy_initialization(anx78xx);
	if (err)
		return err;

	/* Gen m_clk with downspreading */
	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_DP_M_CALCULATION_CTRL_REG, SP_M_GEN_CLK_SEL);
	if (err)
		return err;

	return 0;
}

static int anx78xx_enable_interrupts(struct anx78xx *anx78xx)
{
	int err;

	/*
	 * BIT0: INT pin assertion polarity: 1 = assert high
	 * BIT1: INT pin output type: 0 = push/pull
	 */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2], SP_INT_CTRL_REG, 0x01);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2],
			   SP_COMMON_INT_MASK4_REG, SP_HPD_LOST | SP_HPD_PLUG);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2], SP_DP_INT_MASK1_REG,
			   SP_TRAINING_FINISH);
	if (err)
		return err;

	err = regmap_write(anx78xx->map[I2C_IDX_RX_P0], SP_INT_MASK1_REG,
			   SP_CKDT_CHG | SP_SCDT_CHG);
	if (err)
		return err;

	return 0;
}

static void anx78xx_poweron(struct anx78xx *anx78xx)
{
	struct anx78xx_platform_data *pdata = &anx78xx->pdata;
	int err;

	if (WARN_ON(anx78xx->powered))
		return;

	if (pdata->dvdd10) {
		err = regulator_enable(pdata->dvdd10);
		if (err) {
			DRM_ERROR("Failed to enable DVDD10 regulator: %d\n",
				  err);
			return;
		}

		usleep_range(1000, 2000);
	}

	gpiod_set_value_cansleep(pdata->gpiod_reset, 1);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(pdata->gpiod_pd, 0);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(pdata->gpiod_reset, 0);

	/* Power on registers module */
	anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2], SP_POWERDOWN_CTRL_REG,
			 SP_HDCP_PD | SP_AUDIO_PD | SP_VIDEO_PD | SP_LINK_PD);
	anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2], SP_POWERDOWN_CTRL_REG,
			   SP_REGISTER_PD | SP_TOTAL_PD);

	anx78xx->powered = true;
}

static void anx78xx_poweroff(struct anx78xx *anx78xx)
{
	struct anx78xx_platform_data *pdata = &anx78xx->pdata;
	int err;

	if (WARN_ON(!anx78xx->powered))
		return;

	gpiod_set_value_cansleep(pdata->gpiod_reset, 1);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(pdata->gpiod_pd, 1);
	usleep_range(1000, 2000);

	if (pdata->dvdd10) {
		err = regulator_disable(pdata->dvdd10);
		if (err) {
			DRM_ERROR("Failed to disable DVDD10 regulator: %d\n",
				  err);
			return;
		}

		usleep_range(1000, 2000);
	}

	anx78xx->powered = false;
}

static int anx78xx_start(struct anx78xx *anx78xx)
{
	int err;

	/* Power on all modules */
	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2],
				 SP_POWERDOWN_CTRL_REG,
				 SP_HDCP_PD | SP_AUDIO_PD | SP_VIDEO_PD |
				 SP_LINK_PD);

	err = anx78xx_enable_interrupts(anx78xx);
	if (err) {
		DRM_ERROR("Failed to enable interrupts: %d\n", err);
		goto err_poweroff;
	}

	err = anx78xx_rx_initialization(anx78xx);
	if (err) {
		DRM_ERROR("Failed receiver initialization: %d\n", err);
		goto err_poweroff;
	}

	err = anx78xx_tx_initialization(anx78xx);
	if (err) {
		DRM_ERROR("Failed transmitter initialization: %d\n", err);
		goto err_poweroff;
	}

	/*
	 * This delay seems to help keep the hardware in a good state. Without
	 * it, there are times where it fails silently.
	 */
	usleep_range(10000, 15000);

	return 0;

err_poweroff:
	DRM_ERROR("Failed SlimPort transmitter initialization: %d\n", err);
	anx78xx_poweroff(anx78xx);

	return err;
}

static int anx78xx_init_pdata(struct anx78xx *anx78xx)
{
	struct anx78xx_platform_data *pdata = &anx78xx->pdata;
	struct device *dev = &anx78xx->client->dev;

	/* 1.0V digital core power regulator  */
	pdata->dvdd10 = devm_regulator_get(dev, "dvdd10");
	if (IS_ERR(pdata->dvdd10)) {
		DRM_ERROR("DVDD10 regulator not found\n");
		return PTR_ERR(pdata->dvdd10);
	}

	/* GPIO for HPD */
	pdata->gpiod_hpd = devm_gpiod_get(dev, "hpd", GPIOD_IN);
	if (IS_ERR(pdata->gpiod_hpd))
		return PTR_ERR(pdata->gpiod_hpd);

	/* GPIO for chip power down */
	pdata->gpiod_pd = devm_gpiod_get(dev, "pd", GPIOD_OUT_HIGH);
	if (IS_ERR(pdata->gpiod_pd))
		return PTR_ERR(pdata->gpiod_pd);

	/* GPIO for chip reset */
	pdata->gpiod_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);

	return PTR_ERR_OR_ZERO(pdata->gpiod_reset);
}

static int anx78xx_dp_link_training(struct anx78xx *anx78xx)
{
	u8 dp_bw, value;
	int err;

	err = regmap_write(anx78xx->map[I2C_IDX_RX_P0], SP_HDMI_MUTE_CTRL_REG,
			   0x0);
	if (err)
		return err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2],
				 SP_POWERDOWN_CTRL_REG,
				 SP_TOTAL_PD);
	if (err)
		return err;

	err = drm_dp_dpcd_readb(&anx78xx->aux, DP_MAX_LINK_RATE, &dp_bw);
	if (err < 0)
		return err;

	switch (dp_bw) {
	case DP_LINK_BW_1_62:
	case DP_LINK_BW_2_7:
	case DP_LINK_BW_5_4:
		break;

	default:
		DRM_DEBUG_KMS("DP bandwidth (%#02x) not supported\n", dp_bw);
		return -EINVAL;
	}

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL1_REG,
			       SP_VIDEO_MUTE);
	if (err)
		return err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2],
				 SP_VID_CTRL1_REG, SP_VIDEO_EN);
	if (err)
		return err;

	/* Get DPCD info */
	err = drm_dp_dpcd_read(&anx78xx->aux, DP_DPCD_REV,
			       &anx78xx->dpcd, DP_RECEIVER_CAP_SIZE);
	if (err < 0) {
		DRM_ERROR("Failed to read DPCD: %d\n", err);
		return err;
	}

	/* Clear channel x SERDES power down */
	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_DP_ANALOG_POWER_DOWN_REG, SP_CH0_PD);
	if (err)
		return err;

	/* Check link capabilities */
	err = drm_dp_link_probe(&anx78xx->aux, &anx78xx->link);
	if (err < 0) {
		DRM_ERROR("Failed to probe link capabilities: %d\n", err);
		return err;
	}

	/* Power up the sink */
	err = drm_dp_link_power_up(&anx78xx->aux, &anx78xx->link);
	if (err < 0) {
		DRM_ERROR("Failed to power up DisplayPort link: %d\n", err);
		return err;
	}

	/* Possibly enable downspread on the sink */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_DP_DOWNSPREAD_CTRL1_REG, 0);
	if (err)
		return err;

	if (anx78xx->dpcd[DP_MAX_DOWNSPREAD] & DP_MAX_DOWNSPREAD_0_5) {
		DRM_DEBUG("Enable downspread on the sink\n");
		/* 4000PPM */
		err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
				   SP_DP_DOWNSPREAD_CTRL1_REG, 8);
		if (err)
			return err;

		err = drm_dp_dpcd_writeb(&anx78xx->aux, DP_DOWNSPREAD_CTRL,
					 DP_SPREAD_AMP_0_5);
		if (err < 0)
			return err;
	} else {
		err = drm_dp_dpcd_writeb(&anx78xx->aux, DP_DOWNSPREAD_CTRL, 0);
		if (err < 0)
			return err;
	}

	/* Set the lane count and the link rate on the sink */
	if (drm_dp_enhanced_frame_cap(anx78xx->dpcd))
		err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
				       SP_DP_SYSTEM_CTRL_BASE + 4,
				       SP_ENHANCED_MODE);
	else
		err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P0],
					 SP_DP_SYSTEM_CTRL_BASE + 4,
					 SP_ENHANCED_MODE);
	if (err)
		return err;

	value = drm_dp_link_rate_to_bw_code(anx78xx->link.rate);
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0],
			   SP_DP_MAIN_LINK_BW_SET_REG, value);
	if (err)
		return err;

	err = drm_dp_link_configure(&anx78xx->aux, &anx78xx->link);
	if (err < 0) {
		DRM_ERROR("Failed to configure DisplayPort link: %d\n", err);
		return err;
	}

	/* Start training on the source */
	err = regmap_write(anx78xx->map[I2C_IDX_TX_P0], SP_DP_LT_CTRL_REG,
			   SP_LT_EN);
	if (err)
		return err;

	return 0;
}

static int anx78xx_config_dp_output(struct anx78xx *anx78xx)
{
	int err;

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL1_REG,
				 SP_VIDEO_MUTE);
	if (err)
		return err;

	/* Enable DP output */
	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2], SP_VID_CTRL1_REG,
			       SP_VIDEO_EN);
	if (err)
		return err;

	return 0;
}

static int anx78xx_send_video_infoframe(struct anx78xx *anx78xx,
					struct hdmi_avi_infoframe *frame)
{
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_AVI_INFOFRAME_SIZE];
	int err;

	err = hdmi_avi_infoframe_pack(frame, buffer, sizeof(buffer));
	if (err < 0) {
		DRM_ERROR("Failed to pack AVI infoframe: %d\n", err);
		return err;
	}

	err = anx78xx_clear_bits(anx78xx->map[I2C_IDX_TX_P0],
				 SP_PACKET_SEND_CTRL_REG, SP_AVI_IF_EN);
	if (err)
		return err;

	err = regmap_bulk_write(anx78xx->map[I2C_IDX_TX_P2],
				SP_INFOFRAME_AVI_DB1_REG, buffer,
				frame->length);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_PACKET_SEND_CTRL_REG, SP_AVI_IF_UD);
	if (err)
		return err;

	err = anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P0],
			       SP_PACKET_SEND_CTRL_REG, SP_AVI_IF_EN);
	if (err)
		return err;

	return 0;
}

static int anx78xx_get_downstream_info(struct anx78xx *anx78xx)
{
	u8 value;
	int err;

	err = drm_dp_dpcd_readb(&anx78xx->aux, DP_SINK_COUNT, &value);
	if (err < 0) {
		DRM_ERROR("Get sink count failed %d\n", err);
		return err;
	}

	if (!DP_GET_SINK_COUNT(value)) {
		DRM_ERROR("Downstream disconnected\n");
		return -EIO;
	}

	return 0;
}

static int anx78xx_get_modes(struct drm_connector *connector)
{
	struct anx78xx *anx78xx = connector_to_anx78xx(connector);
	int err, num_modes = 0;

	if (WARN_ON(!anx78xx->powered))
		return 0;

	if (anx78xx->edid)
		return drm_add_edid_modes(connector, anx78xx->edid);

	mutex_lock(&anx78xx->lock);

	err = anx78xx_get_downstream_info(anx78xx);
	if (err) {
		DRM_ERROR("Failed to get downstream info: %d\n", err);
		goto unlock;
	}

	anx78xx->edid = drm_get_edid(connector, &anx78xx->aux.ddc);
	if (!anx78xx->edid) {
		DRM_ERROR("Failed to read EDID\n");
		goto unlock;
	}

	err = drm_connector_update_edid_property(connector,
						 anx78xx->edid);
	if (err) {
		DRM_ERROR("Failed to update EDID property: %d\n", err);
		goto unlock;
	}

	num_modes = drm_add_edid_modes(connector, anx78xx->edid);

unlock:
	mutex_unlock(&anx78xx->lock);

	return num_modes;
}

static const struct drm_connector_helper_funcs anx78xx_connector_helper_funcs = {
	.get_modes = anx78xx_get_modes,
};

static enum drm_connector_status anx78xx_detect(struct drm_connector *connector,
						bool force)
{
	struct anx78xx *anx78xx = connector_to_anx78xx(connector);

	if (!gpiod_get_value(anx78xx->pdata.gpiod_hpd))
		return connector_status_disconnected;

	return connector_status_connected;
}

static const struct drm_connector_funcs anx78xx_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = anx78xx_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int anx78xx_bridge_attach(struct drm_bridge *bridge)
{
	struct anx78xx *anx78xx = bridge_to_anx78xx(bridge);
	int err;

	if (!bridge->encoder) {
		DRM_ERROR("Parent encoder object not found");
		return -ENODEV;
	}

	/* Register aux channel */
	anx78xx->aux.name = "DP-AUX";
	anx78xx->aux.dev = &anx78xx->client->dev;
	anx78xx->aux.transfer = anx78xx_aux_transfer;

	err = drm_dp_aux_register(&anx78xx->aux);
	if (err < 0) {
		DRM_ERROR("Failed to register aux channel: %d\n", err);
		return err;
	}

	err = drm_connector_init(bridge->dev, &anx78xx->connector,
				 &anx78xx_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (err) {
		DRM_ERROR("Failed to initialize connector: %d\n", err);
		return err;
	}

	drm_connector_helper_add(&anx78xx->connector,
				 &anx78xx_connector_helper_funcs);

	err = drm_connector_register(&anx78xx->connector);
	if (err) {
		DRM_ERROR("Failed to register connector: %d\n", err);
		return err;
	}

	anx78xx->connector.polled = DRM_CONNECTOR_POLL_HPD;

	err = drm_connector_attach_encoder(&anx78xx->connector,
					   bridge->encoder);
	if (err) {
		DRM_ERROR("Failed to link up connector to encoder: %d\n", err);
		return err;
	}

	return 0;
}

static enum drm_mode_status
anx78xx_bridge_mode_valid(struct drm_bridge *bridge,
			  const struct drm_display_mode *mode)
{
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	/* Max 1200p at 5.4 Ghz, one lane */
	if (mode->clock > 154000)
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static void anx78xx_bridge_disable(struct drm_bridge *bridge)
{
	struct anx78xx *anx78xx = bridge_to_anx78xx(bridge);

	/* Power off all modules except configuration registers access */
	anx78xx_set_bits(anx78xx->map[I2C_IDX_TX_P2], SP_POWERDOWN_CTRL_REG,
			 SP_HDCP_PD | SP_AUDIO_PD | SP_VIDEO_PD | SP_LINK_PD);
}

static void anx78xx_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{
	struct anx78xx *anx78xx = bridge_to_anx78xx(bridge);
	struct hdmi_avi_infoframe frame;
	int err;

	if (WARN_ON(!anx78xx->powered))
		return;

	mutex_lock(&anx78xx->lock);

	err = drm_hdmi_avi_infoframe_from_display_mode(&frame, adjusted_mode,
						       false);
	if (err) {
		DRM_ERROR("Failed to setup AVI infoframe: %d\n", err);
		goto unlock;
	}

	err = anx78xx_send_video_infoframe(anx78xx, &frame);
	if (err)
		DRM_ERROR("Failed to send AVI infoframe: %d\n", err);

unlock:
	mutex_unlock(&anx78xx->lock);
}

static void anx78xx_bridge_enable(struct drm_bridge *bridge)
{
	struct anx78xx *anx78xx = bridge_to_anx78xx(bridge);
	int err;

	err = anx78xx_start(anx78xx);
	if (err) {
		DRM_ERROR("Failed to initialize: %d\n", err);
		return;
	}

	err = anx78xx_set_hpd(anx78xx);
	if (err)
		DRM_ERROR("Failed to set HPD: %d\n", err);
}

static const struct drm_bridge_funcs anx78xx_bridge_funcs = {
	.attach = anx78xx_bridge_attach,
	.mode_valid = anx78xx_bridge_mode_valid,
	.disable = anx78xx_bridge_disable,
	.mode_set = anx78xx_bridge_mode_set,
	.enable = anx78xx_bridge_enable,
};

static irqreturn_t anx78xx_hpd_threaded_handler(int irq, void *data)
{
	struct anx78xx *anx78xx = data;
	int err;

	if (anx78xx->powered)
		return IRQ_HANDLED;

	mutex_lock(&anx78xx->lock);

	/* Cable is pulled, power on the chip */
	anx78xx_poweron(anx78xx);

	err = anx78xx_enable_interrupts(anx78xx);
	if (err)
		DRM_ERROR("Failed to enable interrupts: %d\n", err);

	mutex_unlock(&anx78xx->lock);

	return IRQ_HANDLED;
}

static int anx78xx_handle_dp_int_1(struct anx78xx *anx78xx, u8 irq)
{
	int err;

	DRM_DEBUG_KMS("Handle DP interrupt 1: %02x\n", irq);

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2], SP_DP_INT_STATUS1_REG,
			   irq);
	if (err)
		return err;

	if (irq & SP_TRAINING_FINISH) {
		DRM_DEBUG_KMS("IRQ: hardware link training finished\n");
		err = anx78xx_config_dp_output(anx78xx);
	}

	return err;
}

static bool anx78xx_handle_common_int_4(struct anx78xx *anx78xx, u8 irq)
{
	bool event = false;
	int err;

	DRM_DEBUG_KMS("Handle common interrupt 4: %02x\n", irq);

	err = regmap_write(anx78xx->map[I2C_IDX_TX_P2],
			   SP_COMMON_INT_STATUS4_REG, irq);
	if (err) {
		DRM_ERROR("Failed to write SP_COMMON_INT_STATUS4 %d\n", err);
		return event;
	}

	if (irq & SP_HPD_LOST) {
		DRM_DEBUG_KMS("IRQ: Hot plug detect - cable is pulled out\n");
		event = true;
		anx78xx_poweroff(anx78xx);
		/* Free cached EDID */
		kfree(anx78xx->edid);
		anx78xx->edid = NULL;
	} else if (irq & SP_HPD_PLUG) {
		DRM_DEBUG_KMS("IRQ: Hot plug detect - cable plug\n");
		event = true;
	}

	return event;
}

static void anx78xx_handle_hdmi_int_1(struct anx78xx *anx78xx, u8 irq)
{
	unsigned int value;
	int err;

	DRM_DEBUG_KMS("Handle HDMI interrupt 1: %02x\n", irq);

	err = regmap_write(anx78xx->map[I2C_IDX_RX_P0], SP_INT_STATUS1_REG,
			   irq);
	if (err) {
		DRM_ERROR("Write HDMI int 1 failed: %d\n", err);
		return;
	}

	if ((irq & SP_CKDT_CHG) || (irq & SP_SCDT_CHG)) {
		DRM_DEBUG_KMS("IRQ: HDMI input detected\n");

		err = regmap_read(anx78xx->map[I2C_IDX_RX_P0],
				  SP_SYSTEM_STATUS_REG, &value);
		if (err) {
			DRM_ERROR("Read system status reg failed: %d\n", err);
			return;
		}

		if (!(value & SP_TMDS_CLOCK_DET)) {
			DRM_DEBUG_KMS("IRQ: *** Waiting for HDMI clock ***\n");
			return;
		}

		if (!(value & SP_TMDS_DE_DET)) {
			DRM_DEBUG_KMS("IRQ: *** Waiting for HDMI signal ***\n");
			return;
		}

		err = anx78xx_dp_link_training(anx78xx);
		if (err)
			DRM_ERROR("Failed to start link training: %d\n", err);
	}
}

static irqreturn_t anx78xx_intp_threaded_handler(int unused, void *data)
{
	struct anx78xx *anx78xx = data;
	bool event = false;
	unsigned int irq;
	int err;

	mutex_lock(&anx78xx->lock);

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P2], SP_DP_INT_STATUS1_REG,
			  &irq);
	if (err) {
		DRM_ERROR("Failed to read DP interrupt 1 status: %d\n", err);
		goto unlock;
	}

	if (irq)
		anx78xx_handle_dp_int_1(anx78xx, irq);

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P2],
			  SP_COMMON_INT_STATUS4_REG, &irq);
	if (err) {
		DRM_ERROR("Failed to read common interrupt 4 status: %d\n",
			  err);
		goto unlock;
	}

	if (irq)
		event = anx78xx_handle_common_int_4(anx78xx, irq);

	/* Make sure we are still powered after handle HPD events */
	if (!anx78xx->powered)
		goto unlock;

	err = regmap_read(anx78xx->map[I2C_IDX_RX_P0], SP_INT_STATUS1_REG,
			  &irq);
	if (err) {
		DRM_ERROR("Failed to read HDMI int 1 status: %d\n", err);
		goto unlock;
	}

	if (irq)
		anx78xx_handle_hdmi_int_1(anx78xx, irq);

unlock:
	mutex_unlock(&anx78xx->lock);

	if (event)
		drm_helper_hpd_irq_event(anx78xx->connector.dev);

	return IRQ_HANDLED;
}

static void unregister_i2c_dummy_clients(struct anx78xx *anx78xx)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(anx78xx->i2c_dummy); i++)
		i2c_unregister_device(anx78xx->i2c_dummy[i]);
}

static const struct regmap_config anx78xx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static const u16 anx78xx_chipid_list[] = {
	0x7812,
	0x7814,
	0x7818,
};

static int anx78xx_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct anx78xx *anx78xx;
	struct anx78xx_platform_data *pdata;
	unsigned int i, idl, idh, version;
	bool found = false;
	int err;

	anx78xx = devm_kzalloc(&client->dev, sizeof(*anx78xx), GFP_KERNEL);
	if (!anx78xx)
		return -ENOMEM;

	pdata = &anx78xx->pdata;

	mutex_init(&anx78xx->lock);

#if IS_ENABLED(CONFIG_OF)
	anx78xx->bridge.of_node = client->dev.of_node;
#endif

	anx78xx->client = client;
	i2c_set_clientdata(client, anx78xx);

	err = anx78xx_init_pdata(anx78xx);
	if (err) {
		DRM_ERROR("Failed to initialize pdata: %d\n", err);
		return err;
	}

	pdata->hpd_irq = gpiod_to_irq(pdata->gpiod_hpd);
	if (pdata->hpd_irq < 0) {
		DRM_ERROR("Failed to get HPD IRQ: %d\n", pdata->hpd_irq);
		return -ENODEV;
	}

	pdata->intp_irq = client->irq;
	if (!pdata->intp_irq) {
		DRM_ERROR("Failed to get CABLE_DET and INTP IRQ\n");
		return -ENODEV;
	}

	/* Map slave addresses of ANX7814 */
	for (i = 0; i < I2C_NUM_ADDRESSES; i++) {
		anx78xx->i2c_dummy[i] = i2c_new_dummy(client->adapter,
						anx78xx_i2c_addresses[i] >> 1);
		if (!anx78xx->i2c_dummy[i]) {
			err = -ENOMEM;
			DRM_ERROR("Failed to reserve I2C bus %02x\n",
				  anx78xx_i2c_addresses[i]);
			goto err_unregister_i2c;
		}

		anx78xx->map[i] = devm_regmap_init_i2c(anx78xx->i2c_dummy[i],
						       &anx78xx_regmap_config);
		if (IS_ERR(anx78xx->map[i])) {
			err = PTR_ERR(anx78xx->map[i]);
			DRM_ERROR("Failed regmap initialization %02x\n",
				  anx78xx_i2c_addresses[i]);
			goto err_unregister_i2c;
		}
	}

	/* Look for supported chip ID */
	anx78xx_poweron(anx78xx);

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P2], SP_DEVICE_IDL_REG,
			  &idl);
	if (err)
		goto err_poweroff;

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P2], SP_DEVICE_IDH_REG,
			  &idh);
	if (err)
		goto err_poweroff;

	anx78xx->chipid = (u8)idl | ((u8)idh << 8);

	err = regmap_read(anx78xx->map[I2C_IDX_TX_P2], SP_DEVICE_VERSION_REG,
			  &version);
	if (err)
		goto err_poweroff;

	for (i = 0; i < ARRAY_SIZE(anx78xx_chipid_list); i++) {
		if (anx78xx->chipid == anx78xx_chipid_list[i]) {
			DRM_INFO("Found ANX%x (ver. %d) SlimPort Transmitter\n",
				 anx78xx->chipid, version);
			found = true;
			break;
		}
	}

	if (!found) {
		DRM_ERROR("ANX%x (ver. %d) not supported by this driver\n",
			  anx78xx->chipid, version);
		err = -ENODEV;
		goto err_poweroff;
	}

	err = devm_request_threaded_irq(&client->dev, pdata->hpd_irq, NULL,
					anx78xx_hpd_threaded_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"anx78xx-hpd", anx78xx);
	if (err) {
		DRM_ERROR("Failed to request CABLE_DET threaded IRQ: %d\n",
			  err);
		goto err_poweroff;
	}

	err = devm_request_threaded_irq(&client->dev, pdata->intp_irq, NULL,
					anx78xx_intp_threaded_handler,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					"anx78xx-intp", anx78xx);
	if (err) {
		DRM_ERROR("Failed to request INTP threaded IRQ: %d\n", err);
		goto err_poweroff;
	}

	anx78xx->bridge.funcs = &anx78xx_bridge_funcs;

	drm_bridge_add(&anx78xx->bridge);

	/* If cable is pulled out, just poweroff and wait for HPD event */
	if (!gpiod_get_value(anx78xx->pdata.gpiod_hpd))
		anx78xx_poweroff(anx78xx);

	return 0;

err_poweroff:
	anx78xx_poweroff(anx78xx);

err_unregister_i2c:
	unregister_i2c_dummy_clients(anx78xx);
	return err;
}

static int anx78xx_i2c_remove(struct i2c_client *client)
{
	struct anx78xx *anx78xx = i2c_get_clientdata(client);

	drm_bridge_remove(&anx78xx->bridge);

	unregister_i2c_dummy_clients(anx78xx);

	kfree(anx78xx->edid);

	return 0;
}

static const struct i2c_device_id anx78xx_id[] = {
	{ "anx7814", 0 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, anx78xx_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id anx78xx_match_table[] = {
	{ .compatible = "analogix,anx7814", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, anx78xx_match_table);
#endif

static struct i2c_driver anx78xx_driver = {
	.driver = {
		   .name = "anx7814",
		   .of_match_table = of_match_ptr(anx78xx_match_table),
		  },
	.probe = anx78xx_i2c_probe,
	.remove = anx78xx_i2c_remove,
	.id_table = anx78xx_id,
};
module_i2c_driver(anx78xx_driver);

MODULE_DESCRIPTION("ANX78xx SlimPort Transmitter driver");
MODULE_AUTHOR("Enric Balletbo i Serra <enric.balletbo@collabora.com>");
MODULE_LICENSE("GPL v2");
