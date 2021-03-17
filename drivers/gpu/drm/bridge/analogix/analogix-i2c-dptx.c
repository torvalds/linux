/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2016, Analogix Semiconductor.
 *
 * Based on anx7808 driver obtained from chromeos with copyright:
 * Copyright(c) 2013, Google Inc.
 */
#include <linux/regmap.h>

#include <drm/drm.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_print.h>

#include "analogix-i2c-dptx.h"

#define AUX_WAIT_TIMEOUT_MS	15
#define AUX_CH_BUFFER_SIZE	16

static int anx_i2c_dp_clear_bits(struct regmap *map, u8 reg, u8 mask)
{
	return regmap_update_bits(map, reg, mask, 0);
}

static bool anx_dp_aux_op_finished(struct regmap *map_dptx)
{
	unsigned int value;
	int err;

	err = regmap_read(map_dptx, SP_DP_AUX_CH_CTRL2_REG, &value);
	if (err < 0)
		return false;

	return (value & SP_AUX_EN) == 0;
}

static int anx_dp_aux_wait(struct regmap *map_dptx)
{
	unsigned long timeout;
	unsigned int status;
	int err;

	timeout = jiffies + msecs_to_jiffies(AUX_WAIT_TIMEOUT_MS) + 1;

	while (!anx_dp_aux_op_finished(map_dptx)) {
		if (time_after(jiffies, timeout)) {
			if (!anx_dp_aux_op_finished(map_dptx)) {
				DRM_ERROR("Timed out waiting AUX to finish\n");
				return -ETIMEDOUT;
			}

			break;
		}

		usleep_range(1000, 2000);
	}

	/* Read the AUX channel access status */
	err = regmap_read(map_dptx, SP_AUX_CH_STATUS_REG, &status);
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

static int anx_dp_aux_address(struct regmap *map_dptx, unsigned int addr)
{
	int err;

	err = regmap_write(map_dptx, SP_AUX_ADDR_7_0_REG, addr & 0xff);
	if (err)
		return err;

	err = regmap_write(map_dptx, SP_AUX_ADDR_15_8_REG,
			   (addr & 0xff00) >> 8);
	if (err)
		return err;

	/*
	 * DP AUX CH Address Register #2, only update bits[3:0]
	 * [7:4] RESERVED
	 * [3:0] AUX_ADDR[19:16], Register control AUX CH address.
	 */
	err = regmap_update_bits(map_dptx, SP_AUX_ADDR_19_16_REG,
				 SP_AUX_ADDR_19_16_MASK,
				 (addr & 0xf0000) >> 16);

	if (err)
		return err;

	return 0;
}

ssize_t anx_dp_aux_transfer(struct regmap *map_dptx,
				struct drm_dp_aux_msg *msg)
{
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

	if ((msg->size > 0) && ((msg->request & DP_AUX_I2C_READ) == 0)) {
		/* When WRITE | MOT write values to data buffer */
		err = regmap_bulk_write(map_dptx,
					SP_DP_BUF_DATA0_REG, buffer,
					msg->size);
		if (err)
			return err;
	}

	/* Write address and request */
	err = anx_dp_aux_address(map_dptx, msg->address);
	if (err)
		return err;

	err = regmap_write(map_dptx, SP_DP_AUX_CH_CTRL1_REG, ctrl1);
	if (err)
		return err;

	/* Start transaction */
	err = regmap_update_bits(map_dptx, SP_DP_AUX_CH_CTRL2_REG,
				 SP_ADDR_ONLY | SP_AUX_EN, ctrl2);
	if (err)
		return err;

	err = anx_dp_aux_wait(map_dptx);
	if (err)
		return err;

	msg->reply = DP_AUX_I2C_REPLY_ACK;

	if ((msg->size > 0) && (msg->request & DP_AUX_I2C_READ)) {
		/* Read values from data buffer */
		err = regmap_bulk_read(map_dptx,
				       SP_DP_BUF_DATA0_REG, buffer,
				       msg->size);
		if (err)
			return err;
	}

	err = anx_i2c_dp_clear_bits(map_dptx, SP_DP_AUX_CH_CTRL2_REG,
				    SP_ADDR_ONLY);
	if (err)
		return err;

	return msg->size;
}
EXPORT_SYMBOL_GPL(anx_dp_aux_transfer);
