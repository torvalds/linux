/*
 * Copyright (c) 2014-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "edp.h"
#include "edp.xml.h"

#define AUX_CMD_FIFO_LEN	144
#define AUX_CMD_NATIVE_MAX	16
#define AUX_CMD_I2C_MAX		128

#define EDP_INTR_AUX_I2C_ERR	\
	(EDP_INTERRUPT_REG_1_WRONG_ADDR | EDP_INTERRUPT_REG_1_TIMEOUT | \
	EDP_INTERRUPT_REG_1_NACK_DEFER | EDP_INTERRUPT_REG_1_WRONG_DATA_CNT | \
	EDP_INTERRUPT_REG_1_I2C_NACK | EDP_INTERRUPT_REG_1_I2C_DEFER)
#define EDP_INTR_TRANS_STATUS	\
	(EDP_INTERRUPT_REG_1_AUX_I2C_DONE | EDP_INTR_AUX_I2C_ERR)

struct edp_aux {
	void __iomem *base;
	bool msg_err;

	struct completion msg_comp;

	/* To prevent the message transaction routine from reentry. */
	struct mutex msg_mutex;

	struct drm_dp_aux drm_aux;
};
#define to_edp_aux(x) container_of(x, struct edp_aux, drm_aux)

static int edp_msg_fifo_tx(struct edp_aux *aux, struct drm_dp_aux_msg *msg)
{
	u32 data[4];
	u32 reg, len;
	bool native = msg->request & (DP_AUX_NATIVE_WRITE & DP_AUX_NATIVE_READ);
	bool read = msg->request & (DP_AUX_I2C_READ & DP_AUX_NATIVE_READ);
	u8 *msgdata = msg->buffer;
	int i;

	if (read)
		len = 4;
	else
		len = msg->size + 4;

	/*
	 * cmd fifo only has depth of 144 bytes
	 */
	if (len > AUX_CMD_FIFO_LEN)
		return -EINVAL;

	/* Pack cmd and write to HW */
	data[0] = (msg->address >> 16) & 0xf;	/* addr[19:16] */
	if (read)
		data[0] |=  BIT(4);		/* R/W */

	data[1] = (msg->address >> 8) & 0xff;	/* addr[15:8] */
	data[2] = msg->address & 0xff;		/* addr[7:0] */
	data[3] = (msg->size - 1) & 0xff;	/* len[7:0] */

	for (i = 0; i < len; i++) {
		reg = (i < 4) ? data[i] : msgdata[i - 4];
		reg = EDP_AUX_DATA_DATA(reg); /* index = 0, write */
		if (i == 0)
			reg |= EDP_AUX_DATA_INDEX_WRITE;
		edp_write(aux->base + REG_EDP_AUX_DATA, reg);
	}

	reg = 0; /* Transaction number is always 1 */
	if (!native) /* i2c */
		reg |= EDP_AUX_TRANS_CTRL_I2C;

	reg |= EDP_AUX_TRANS_CTRL_GO;
	edp_write(aux->base + REG_EDP_AUX_TRANS_CTRL, reg);

	return 0;
}

static int edp_msg_fifo_rx(struct edp_aux *aux, struct drm_dp_aux_msg *msg)
{
	u32 data;
	u8 *dp;
	int i;
	u32 len = msg->size;

	edp_write(aux->base + REG_EDP_AUX_DATA,
		EDP_AUX_DATA_INDEX_WRITE | EDP_AUX_DATA_READ); /* index = 0 */

	dp = msg->buffer;

	/* discard first byte */
	data = edp_read(aux->base + REG_EDP_AUX_DATA);
	for (i = 0; i < len; i++) {
		data = edp_read(aux->base + REG_EDP_AUX_DATA);
		dp[i] = (u8)((data >> 8) & 0xff);
	}

	return 0;
}

/*
 * This function does the real job to process an AUX transaction.
 * It will call msm_edp_aux_ctrl() function to reset the AUX channel,
 * if the waiting is timeout.
 * The caller who triggers the transaction should avoid the
 * msm_edp_aux_ctrl() running concurrently in other threads, i.e.
 * start transaction only when AUX channel is fully enabled.
 */
ssize_t edp_aux_transfer(struct drm_dp_aux *drm_aux, struct drm_dp_aux_msg *msg)
{
	struct edp_aux *aux = to_edp_aux(drm_aux);
	ssize_t ret;
	bool native = msg->request & (DP_AUX_NATIVE_WRITE & DP_AUX_NATIVE_READ);
	bool read = msg->request & (DP_AUX_I2C_READ & DP_AUX_NATIVE_READ);

	/* Ignore address only message */
	if ((msg->size == 0) || (msg->buffer == NULL)) {
		msg->reply = native ?
			DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
		return msg->size;
	}

	/* msg sanity check */
	if ((native && (msg->size > AUX_CMD_NATIVE_MAX)) ||
		(msg->size > AUX_CMD_I2C_MAX)) {
		pr_err("%s: invalid msg: size(%zu), request(%x)\n",
			__func__, msg->size, msg->request);
		return -EINVAL;
	}

	mutex_lock(&aux->msg_mutex);

	aux->msg_err = false;
	reinit_completion(&aux->msg_comp);

	ret = edp_msg_fifo_tx(aux, msg);
	if (ret < 0)
		goto unlock_exit;

	DBG("wait_for_completion");
	ret = wait_for_completion_timeout(&aux->msg_comp, 300);
	if (ret <= 0) {
		/*
		 * Clear GO and reset AUX channel
		 * to cancel the current transaction.
		 */
		edp_write(aux->base + REG_EDP_AUX_TRANS_CTRL, 0);
		msm_edp_aux_ctrl(aux, 1);
		pr_err("%s: aux timeout, %zd\n", __func__, ret);
		goto unlock_exit;
	}
	DBG("completion");

	if (!aux->msg_err) {
		if (read) {
			ret = edp_msg_fifo_rx(aux, msg);
			if (ret < 0)
				goto unlock_exit;
		}

		msg->reply = native ?
			DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
	} else {
		/* Reply defer to retry */
		msg->reply = native ?
			DP_AUX_NATIVE_REPLY_DEFER : DP_AUX_I2C_REPLY_DEFER;
		/*
		 * The sleep time in caller is not long enough to make sure
		 * our H/W completes transactions. Add more defer time here.
		 */
		msleep(100);
	}

	/* Return requested size for success or retry */
	ret = msg->size;

unlock_exit:
	mutex_unlock(&aux->msg_mutex);
	return ret;
}

void *msm_edp_aux_init(struct device *dev, void __iomem *regbase,
	struct drm_dp_aux **drm_aux)
{
	struct edp_aux *aux = NULL;
	int ret;

	DBG("");
	aux = devm_kzalloc(dev, sizeof(*aux), GFP_KERNEL);
	if (!aux)
		return NULL;

	aux->base = regbase;
	mutex_init(&aux->msg_mutex);
	init_completion(&aux->msg_comp);

	aux->drm_aux.name = "msm_edp_aux";
	aux->drm_aux.dev = dev;
	aux->drm_aux.transfer = edp_aux_transfer;
	ret = drm_dp_aux_register(&aux->drm_aux);
	if (ret) {
		pr_err("%s: failed to register drm aux: %d\n", __func__, ret);
		mutex_destroy(&aux->msg_mutex);
	}

	if (drm_aux && aux)
		*drm_aux = &aux->drm_aux;

	return aux;
}

void msm_edp_aux_destroy(struct device *dev, struct edp_aux *aux)
{
	if (aux) {
		drm_dp_aux_unregister(&aux->drm_aux);
		mutex_destroy(&aux->msg_mutex);
	}
}

irqreturn_t msm_edp_aux_irq(struct edp_aux *aux, u32 isr)
{
	if (isr & EDP_INTR_TRANS_STATUS) {
		DBG("isr=%x", isr);
		edp_write(aux->base + REG_EDP_AUX_TRANS_CTRL, 0);

		if (isr & EDP_INTR_AUX_I2C_ERR)
			aux->msg_err = true;
		else
			aux->msg_err = false;

		complete(&aux->msg_comp);
	}

	return IRQ_HANDLED;
}

void msm_edp_aux_ctrl(struct edp_aux *aux, int enable)
{
	u32 data;

	DBG("enable=%d", enable);
	data = edp_read(aux->base + REG_EDP_AUX_CTRL);

	if (enable) {
		data |= EDP_AUX_CTRL_RESET;
		edp_write(aux->base + REG_EDP_AUX_CTRL, data);
		/* Make sure full reset */
		wmb();
		usleep_range(500, 1000);

		data &= ~EDP_AUX_CTRL_RESET;
		data |= EDP_AUX_CTRL_ENABLE;
		edp_write(aux->base + REG_EDP_AUX_CTRL, data);
	} else {
		data &= ~EDP_AUX_CTRL_ENABLE;
		edp_write(aux->base + REG_EDP_AUX_CTRL, data);
	}
}

