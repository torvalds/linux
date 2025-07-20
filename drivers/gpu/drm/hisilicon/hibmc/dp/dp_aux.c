// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Hisilicon Limited.

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/minmax.h>
#include <drm/drm_device.h>
#include <drm/drm_print.h>
#include "dp_comm.h"
#include "dp_reg.h"
#include "dp_hw.h"

#define HIBMC_AUX_CMD_REQ_LEN		GENMASK(7, 4)
#define HIBMC_AUX_CMD_ADDR		GENMASK(27, 8)
#define HIBMC_AUX_CMD_I2C_ADDR_ONLY	BIT(28)
#define HIBMC_BYTES_IN_U32		4
#define HIBMC_AUX_I2C_WRITE_SUCCESS	0x1
#define HIBMC_DP_MIN_PULSE_NUM		0x9
#define BITS_IN_U8			8

static inline void hibmc_dp_aux_reset(struct hibmc_dp_dev *dp)
{
	hibmc_dp_reg_write_field(dp, HIBMC_DP_DPTX_RST_CTRL, HIBMC_DP_CFG_AUX_RST_N, 0x0);
	usleep_range(10, 15);
	hibmc_dp_reg_write_field(dp, HIBMC_DP_DPTX_RST_CTRL, HIBMC_DP_CFG_AUX_RST_N, 0x1);
}

static void hibmc_dp_aux_read_data(struct hibmc_dp_dev *dp, u8 *buf, u8 size)
{
	u32 reg_num;
	u32 value;
	u32 num;
	u8 i, j;

	reg_num = DIV_ROUND_UP(size, HIBMC_BYTES_IN_U32);
	for (i = 0; i < reg_num; i++) {
		/* number of bytes read from a single register */
		num = min(size - i * HIBMC_BYTES_IN_U32, HIBMC_BYTES_IN_U32);
		value = readl(dp->base + HIBMC_DP_AUX_RD_DATA0 + i * HIBMC_BYTES_IN_U32);
		/* convert the 32-bit value of the register to the buffer. */
		for (j = 0; j < num; j++)
			buf[i * HIBMC_BYTES_IN_U32 + j] = value >> (j * BITS_IN_U8);
	}
}

static void hibmc_dp_aux_write_data(struct hibmc_dp_dev *dp, u8 *buf, u8 size)
{
	u32 reg_num;
	u32 value;
	u32 num;
	u8 i, j;

	reg_num = DIV_ROUND_UP(size, HIBMC_BYTES_IN_U32);
	for (i = 0; i < reg_num; i++) {
		/* number of bytes written to a single register */
		num = min_t(u8, size - i * HIBMC_BYTES_IN_U32, HIBMC_BYTES_IN_U32);
		value = 0;
		/* obtain the 32-bit value written to a single register. */
		for (j = 0; j < num; j++)
			value |= buf[i * HIBMC_BYTES_IN_U32 + j] << (j * BITS_IN_U8);
		/* writing data to a single register */
		writel(value, dp->base + HIBMC_DP_AUX_WR_DATA0 + i * HIBMC_BYTES_IN_U32);
	}
}

static u32 hibmc_dp_aux_build_cmd(const struct drm_dp_aux_msg *msg)
{
	u32 aux_cmd = msg->request;

	if (msg->size)
		aux_cmd |= FIELD_PREP(HIBMC_AUX_CMD_REQ_LEN, (msg->size - 1));
	else
		aux_cmd |= FIELD_PREP(HIBMC_AUX_CMD_I2C_ADDR_ONLY, 1);

	aux_cmd |= FIELD_PREP(HIBMC_AUX_CMD_ADDR, msg->address);

	return aux_cmd;
}

/* ret >= 0, ret is size; ret < 0, ret is err code */
static int hibmc_dp_aux_parse_xfer(struct hibmc_dp_dev *dp, struct drm_dp_aux_msg *msg)
{
	u32 buf_data_cnt;
	u32 aux_status;

	aux_status = readl(dp->base + HIBMC_DP_AUX_STATUS);
	msg->reply = FIELD_GET(HIBMC_DP_CFG_AUX_STATUS, aux_status);

	if (aux_status & HIBMC_DP_CFG_AUX_TIMEOUT)
		return -ETIMEDOUT;

	/* only address */
	if (!msg->size)
		return 0;

	if (msg->reply != DP_AUX_NATIVE_REPLY_ACK)
		return -EIO;

	buf_data_cnt = FIELD_GET(HIBMC_DP_CFG_AUX_READY_DATA_BYTE, aux_status);

	switch (msg->request) {
	case DP_AUX_NATIVE_WRITE:
		return msg->size;
	case DP_AUX_I2C_WRITE | DP_AUX_I2C_MOT:
		if (buf_data_cnt == HIBMC_AUX_I2C_WRITE_SUCCESS)
			return msg->size;
		else
			return FIELD_GET(HIBMC_DP_CFG_AUX, aux_status);
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ | DP_AUX_I2C_MOT:
		buf_data_cnt--;
		if (buf_data_cnt != msg->size) {
			/* only the successful part of data is read */
			return -EBUSY;
		}

		/* all data is successfully read */
		hibmc_dp_aux_read_data(dp, msg->buffer, msg->size);
		return msg->size;
	default:
		return -EINVAL;
	}
}

/* ret >= 0 ,ret is size; ret < 0, ret is err code */
static ssize_t hibmc_dp_aux_xfer(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	struct hibmc_dp *dp_priv = container_of(aux, struct hibmc_dp, aux);
	struct hibmc_dp_dev *dp = dp_priv->dp_dev;
	u32 aux_cmd;
	int ret;
	u32 val; /* val will be assigned at the beginning of readl_poll_timeout function */

	writel(0, dp->base + HIBMC_DP_AUX_WR_DATA0);
	writel(0, dp->base + HIBMC_DP_AUX_WR_DATA1);
	writel(0, dp->base + HIBMC_DP_AUX_WR_DATA2);
	writel(0, dp->base + HIBMC_DP_AUX_WR_DATA3);

	hibmc_dp_aux_write_data(dp, msg->buffer, msg->size);

	aux_cmd = hibmc_dp_aux_build_cmd(msg);
	writel(aux_cmd, dp->base + HIBMC_DP_AUX_CMD_ADDR);

	/* enable aux transfer */
	hibmc_dp_reg_write_field(dp, HIBMC_DP_AUX_REQ, HIBMC_DP_CFG_AUX_REQ, 0x1);
	ret = readl_poll_timeout(dp->base + HIBMC_DP_AUX_REQ, val,
				 !(val & HIBMC_DP_CFG_AUX_REQ), 50, 5000);
	if (ret) {
		hibmc_dp_aux_reset(dp);
		return ret;
	}

	return hibmc_dp_aux_parse_xfer(dp, msg);
}

void hibmc_dp_aux_init(struct hibmc_dp *dp)
{
	hibmc_dp_reg_write_field(dp->dp_dev, HIBMC_DP_AUX_REQ, HIBMC_DP_CFG_AUX_SYNC_LEN_SEL, 0x0);
	hibmc_dp_reg_write_field(dp->dp_dev, HIBMC_DP_AUX_REQ, HIBMC_DP_CFG_AUX_TIMER_TIMEOUT, 0x1);
	hibmc_dp_reg_write_field(dp->dp_dev, HIBMC_DP_AUX_REQ, HIBMC_DP_CFG_AUX_MIN_PULSE_NUM,
				 HIBMC_DP_MIN_PULSE_NUM);

	dp->aux.transfer = hibmc_dp_aux_xfer;
	dp->aux.name = "HIBMC DRM dp aux";
	dp->aux.drm_dev = dp->drm_dev;
	drm_dp_aux_init(&dp->aux);
	dp->dp_dev->aux = &dp->aux;
}
