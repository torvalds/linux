// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/phy/phy.h>
#include <drm/drm_print.h>

#include "dp_reg.h"
#include "dp_aux.h"

enum msm_dp_aux_err {
	DP_AUX_ERR_NONE,
	DP_AUX_ERR_ADDR,
	DP_AUX_ERR_TOUT,
	DP_AUX_ERR_NACK,
	DP_AUX_ERR_DEFER,
	DP_AUX_ERR_NACK_DEFER,
	DP_AUX_ERR_PHY,
};

struct dp_aux_private {
	struct device *dev;
	struct dp_catalog *catalog;

	struct phy *phy;

	struct mutex mutex;
	struct completion comp;

	enum msm_dp_aux_err aux_error_num;
	u32 retry_cnt;
	bool cmd_busy;
	bool native;
	bool read;
	bool no_send_addr;
	bool no_send_stop;
	bool initted;
	bool is_edp;
	bool enable_xfers;
	u32 offset;
	u32 segment;

	struct drm_dp_aux dp_aux;
};

#define MAX_AUX_RETRIES			5

static ssize_t dp_aux_write(struct dp_aux_private *aux,
			struct drm_dp_aux_msg *msg)
{
	u8 data[4];
	u32 reg;
	ssize_t len;
	u8 *msgdata = msg->buffer;
	int const AUX_CMD_FIFO_LEN = 128;
	int i = 0;

	if (aux->read)
		len = 0;
	else
		len = msg->size;

	/*
	 * cmd fifo only has depth of 144 bytes
	 * limit buf length to 128 bytes here
	 */
	if (len > AUX_CMD_FIFO_LEN - 4) {
		DRM_ERROR("buf size greater than allowed size of 128 bytes\n");
		return -EINVAL;
	}

	/* Pack cmd and write to HW */
	data[0] = (msg->address >> 16) & 0xf;	/* addr[19:16] */
	if (aux->read)
		data[0] |=  BIT(4);		/* R/W */

	data[1] = msg->address >> 8;		/* addr[15:8] */
	data[2] = msg->address;			/* addr[7:0] */
	data[3] = msg->size - 1;		/* len[7:0] */

	for (i = 0; i < len + 4; i++) {
		reg = (i < 4) ? data[i] : msgdata[i - 4];
		reg <<= DP_AUX_DATA_OFFSET;
		reg &= DP_AUX_DATA_MASK;
		reg |= DP_AUX_DATA_WRITE;
		/* index = 0, write */
		if (i == 0)
			reg |= DP_AUX_DATA_INDEX_WRITE;
		dp_catalog_aux_write_data(aux->catalog, reg);
	}

	dp_catalog_aux_clear_trans(aux->catalog, false);
	dp_catalog_aux_clear_hw_interrupts(aux->catalog);

	reg = 0; /* Transaction number == 1 */
	if (!aux->native) { /* i2c */
		reg |= DP_AUX_TRANS_CTRL_I2C;

		if (aux->no_send_addr)
			reg |= DP_AUX_TRANS_CTRL_NO_SEND_ADDR;

		if (aux->no_send_stop)
			reg |= DP_AUX_TRANS_CTRL_NO_SEND_STOP;
	}

	reg |= DP_AUX_TRANS_CTRL_GO;
	dp_catalog_aux_write_trans(aux->catalog, reg);

	return len;
}

static ssize_t dp_aux_cmd_fifo_tx(struct dp_aux_private *aux,
			      struct drm_dp_aux_msg *msg)
{
	ssize_t ret;
	unsigned long time_left;

	reinit_completion(&aux->comp);

	ret = dp_aux_write(aux, msg);
	if (ret < 0)
		return ret;

	time_left = wait_for_completion_timeout(&aux->comp,
						msecs_to_jiffies(250));
	if (!time_left)
		return -ETIMEDOUT;

	return ret;
}

static ssize_t dp_aux_cmd_fifo_rx(struct dp_aux_private *aux,
		struct drm_dp_aux_msg *msg)
{
	u32 data;
	u8 *dp;
	u32 i, actual_i;
	u32 len = msg->size;

	dp_catalog_aux_clear_trans(aux->catalog, true);

	data = DP_AUX_DATA_INDEX_WRITE; /* INDEX_WRITE */
	data |= DP_AUX_DATA_READ;  /* read */

	dp_catalog_aux_write_data(aux->catalog, data);

	dp = msg->buffer;

	/* discard first byte */
	data = dp_catalog_aux_read_data(aux->catalog);

	for (i = 0; i < len; i++) {
		data = dp_catalog_aux_read_data(aux->catalog);
		*dp++ = (u8)((data >> DP_AUX_DATA_OFFSET) & 0xff);

		actual_i = (data >> DP_AUX_DATA_INDEX_OFFSET) & 0xFF;
		if (i != actual_i)
			break;
	}

	return i;
}

static void dp_aux_update_offset_and_segment(struct dp_aux_private *aux,
					     struct drm_dp_aux_msg *input_msg)
{
	u32 edid_address = 0x50;
	u32 segment_address = 0x30;
	bool i2c_read = input_msg->request &
		(DP_AUX_I2C_READ & DP_AUX_NATIVE_READ);
	u8 *data;

	if (aux->native || i2c_read || ((input_msg->address != edid_address) &&
		(input_msg->address != segment_address)))
		return;


	data = input_msg->buffer;
	if (input_msg->address == segment_address)
		aux->segment = *data;
	else
		aux->offset = *data;
}

/**
 * dp_aux_transfer_helper() - helper function for EDID read transactions
 *
 * @aux: DP AUX private structure
 * @input_msg: input message from DRM upstream APIs
 * @send_seg: send the segment to sink
 *
 * return: void
 *
 * This helper function is used to fix EDID reads for non-compliant
 * sinks that do not handle the i2c middle-of-transaction flag correctly.
 */
static void dp_aux_transfer_helper(struct dp_aux_private *aux,
				   struct drm_dp_aux_msg *input_msg,
				   bool send_seg)
{
	struct drm_dp_aux_msg helper_msg;
	u32 message_size = 0x10;
	u32 segment_address = 0x30;
	u32 const edid_block_length = 0x80;
	bool i2c_mot = input_msg->request & DP_AUX_I2C_MOT;
	bool i2c_read = input_msg->request &
		(DP_AUX_I2C_READ & DP_AUX_NATIVE_READ);

	if (!i2c_mot || !i2c_read || (input_msg->size == 0))
		return;

	/*
	 * Sending the segment value and EDID offset will be performed
	 * from the DRM upstream EDID driver for each block. Avoid
	 * duplicate AUX transactions related to this while reading the
	 * first 16 bytes of each block.
	 */
	if (!(aux->offset % edid_block_length) || !send_seg)
		goto end;

	aux->read = false;
	aux->cmd_busy = true;
	aux->no_send_addr = true;
	aux->no_send_stop = true;

	/*
	 * Send the segment address for every i2c read in which the
	 * middle-of-tranaction flag is set. This is required to support EDID
	 * reads of more than 2 blocks as the segment address is reset to 0
	 * since we are overriding the middle-of-transaction flag for read
	 * transactions.
	 */

	if (aux->segment) {
		memset(&helper_msg, 0, sizeof(helper_msg));
		helper_msg.address = segment_address;
		helper_msg.buffer = &aux->segment;
		helper_msg.size = 1;
		dp_aux_cmd_fifo_tx(aux, &helper_msg);
	}

	/*
	 * Send the offset address for every i2c read in which the
	 * middle-of-transaction flag is set. This will ensure that the sink
	 * will update its read pointer and return the correct portion of the
	 * EDID buffer in the subsequent i2c read trasntion triggered in the
	 * native AUX transfer function.
	 */
	memset(&helper_msg, 0, sizeof(helper_msg));
	helper_msg.address = input_msg->address;
	helper_msg.buffer = &aux->offset;
	helper_msg.size = 1;
	dp_aux_cmd_fifo_tx(aux, &helper_msg);

end:
	aux->offset += message_size;
	if (aux->offset == 0x80 || aux->offset == 0x100)
		aux->segment = 0x0; /* reset segment at end of block */
}

/*
 * This function does the real job to process an AUX transaction.
 * It will call aux_reset() function to reset the AUX channel,
 * if the waiting is timeout.
 */
static ssize_t dp_aux_transfer(struct drm_dp_aux *dp_aux,
			       struct drm_dp_aux_msg *msg)
{
	ssize_t ret;
	int const aux_cmd_native_max = 16;
	int const aux_cmd_i2c_max = 128;
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	aux->native = msg->request & (DP_AUX_NATIVE_WRITE & DP_AUX_NATIVE_READ);

	/* Ignore address only message */
	if (msg->size == 0 || !msg->buffer) {
		msg->reply = aux->native ?
			DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
		return msg->size;
	}

	/* msg sanity check */
	if ((aux->native && msg->size > aux_cmd_native_max) ||
	    msg->size > aux_cmd_i2c_max) {
		DRM_ERROR("%s: invalid msg: size(%zu), request(%x)\n",
			__func__, msg->size, msg->request);
		return -EINVAL;
	}

	ret = pm_runtime_resume_and_get(dp_aux->dev);
	if (ret)
		return  ret;

	mutex_lock(&aux->mutex);
	if (!aux->initted) {
		ret = -EIO;
		goto exit;
	}

	/*
	 * If we're using DP and an external display isn't connected then the
	 * transfer won't succeed. Return right away. If we don't do this we
	 * can end up with long timeouts if someone tries to access the DP AUX
	 * character device when no DP device is connected.
	 */
	if (!aux->is_edp && !aux->enable_xfers) {
		ret = -ENXIO;
		goto exit;
	}

	dp_aux_update_offset_and_segment(aux, msg);
	dp_aux_transfer_helper(aux, msg, true);

	aux->read = msg->request & (DP_AUX_I2C_READ & DP_AUX_NATIVE_READ);
	aux->cmd_busy = true;

	if (aux->read) {
		aux->no_send_addr = true;
		aux->no_send_stop = false;
	} else {
		aux->no_send_addr = true;
		aux->no_send_stop = true;
	}

	ret = dp_aux_cmd_fifo_tx(aux, msg);
	if (ret < 0) {
		if (aux->native) {
			aux->retry_cnt++;
			if (!(aux->retry_cnt % MAX_AUX_RETRIES))
				phy_calibrate(aux->phy);
		}
		/* reset aux if link is in connected state */
		if (dp_catalog_link_is_connected(aux->catalog))
			dp_catalog_aux_reset(aux->catalog);
	} else {
		aux->retry_cnt = 0;
		switch (aux->aux_error_num) {
		case DP_AUX_ERR_NONE:
			if (aux->read)
				ret = dp_aux_cmd_fifo_rx(aux, msg);
			msg->reply = aux->native ? DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
			break;
		case DP_AUX_ERR_DEFER:
			msg->reply = aux->native ? DP_AUX_NATIVE_REPLY_DEFER : DP_AUX_I2C_REPLY_DEFER;
			break;
		case DP_AUX_ERR_PHY:
		case DP_AUX_ERR_ADDR:
		case DP_AUX_ERR_NACK:
		case DP_AUX_ERR_NACK_DEFER:
			msg->reply = aux->native ? DP_AUX_NATIVE_REPLY_NACK : DP_AUX_I2C_REPLY_NACK;
			break;
		case DP_AUX_ERR_TOUT:
			ret = -ETIMEDOUT;
			break;
		}
	}

	aux->cmd_busy = false;

exit:
	mutex_unlock(&aux->mutex);
	pm_runtime_put_sync(dp_aux->dev);

	return ret;
}

irqreturn_t dp_aux_isr(struct drm_dp_aux *dp_aux)
{
	u32 isr;
	struct dp_aux_private *aux;

	if (!dp_aux) {
		DRM_ERROR("invalid input\n");
		return IRQ_NONE;
	}

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	isr = dp_catalog_aux_get_irq(aux->catalog);

	/* no interrupts pending, return immediately */
	if (!isr)
		return IRQ_NONE;

	if (!aux->cmd_busy) {
		DRM_ERROR("Unexpected DP AUX IRQ %#010x when not busy\n", isr);
		return IRQ_NONE;
	}

	/*
	 * The logic below assumes only one error bit is set (other than "done"
	 * which can apparently be set at the same time as some of the other
	 * bits). Warn if more than one get set so we know we need to improve
	 * the logic.
	 */
	if (hweight32(isr & ~DP_INTR_AUX_XFER_DONE) > 1)
		DRM_WARN("Some DP AUX interrupts unhandled: %#010x\n", isr);

	if (isr & DP_INTR_AUX_ERROR) {
		aux->aux_error_num = DP_AUX_ERR_PHY;
		dp_catalog_aux_clear_hw_interrupts(aux->catalog);
	} else if (isr & DP_INTR_NACK_DEFER) {
		aux->aux_error_num = DP_AUX_ERR_NACK_DEFER;
	} else if (isr & DP_INTR_WRONG_ADDR) {
		aux->aux_error_num = DP_AUX_ERR_ADDR;
	} else if (isr & DP_INTR_TIMEOUT) {
		aux->aux_error_num = DP_AUX_ERR_TOUT;
	} else if (!aux->native && (isr & DP_INTR_I2C_NACK)) {
		aux->aux_error_num = DP_AUX_ERR_NACK;
	} else if (!aux->native && (isr & DP_INTR_I2C_DEFER)) {
		if (isr & DP_INTR_AUX_XFER_DONE)
			aux->aux_error_num = DP_AUX_ERR_NACK;
		else
			aux->aux_error_num = DP_AUX_ERR_DEFER;
	} else if (isr & DP_INTR_AUX_XFER_DONE) {
		aux->aux_error_num = DP_AUX_ERR_NONE;
	} else {
		DRM_WARN("Unexpected interrupt: %#010x\n", isr);
		return IRQ_NONE;
	}

	complete(&aux->comp);

	return IRQ_HANDLED;
}

void dp_aux_enable_xfers(struct drm_dp_aux *dp_aux, bool enabled)
{
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);
	aux->enable_xfers = enabled;
}

void dp_aux_reconfig(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	phy_calibrate(aux->phy);
	dp_catalog_aux_reset(aux->catalog);
}

void dp_aux_init(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	if (!dp_aux) {
		DRM_ERROR("invalid input\n");
		return;
	}

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	mutex_lock(&aux->mutex);

	dp_catalog_aux_enable(aux->catalog, true);
	aux->retry_cnt = 0;
	aux->initted = true;

	mutex_unlock(&aux->mutex);
}

void dp_aux_deinit(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	mutex_lock(&aux->mutex);

	aux->initted = false;
	dp_catalog_aux_enable(aux->catalog, false);

	mutex_unlock(&aux->mutex);
}

int dp_aux_register(struct drm_dp_aux *dp_aux)
{
	int ret;

	if (!dp_aux) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	ret = drm_dp_aux_register(dp_aux);
	if (ret) {
		DRM_ERROR("%s: failed to register drm aux: %d\n", __func__,
				ret);
		return ret;
	}

	return 0;
}

void dp_aux_unregister(struct drm_dp_aux *dp_aux)
{
	drm_dp_aux_unregister(dp_aux);
}

static int dp_wait_hpd_asserted(struct drm_dp_aux *dp_aux,
				 unsigned long wait_us)
{
	int ret;
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	pm_runtime_get_sync(aux->dev);
	ret = dp_catalog_aux_wait_for_hpd_connect_state(aux->catalog, wait_us);
	pm_runtime_put_sync(aux->dev);

	return ret;
}

struct drm_dp_aux *dp_aux_get(struct device *dev, struct dp_catalog *catalog,
			      struct phy *phy,
			      bool is_edp)
{
	struct dp_aux_private *aux;

	if (!catalog) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-ENODEV);
	}

	aux = devm_kzalloc(dev, sizeof(*aux), GFP_KERNEL);
	if (!aux)
		return ERR_PTR(-ENOMEM);

	init_completion(&aux->comp);
	aux->cmd_busy = false;
	aux->is_edp = is_edp;
	mutex_init(&aux->mutex);

	aux->dev = dev;
	aux->catalog = catalog;
	aux->phy = phy;
	aux->retry_cnt = 0;

	/*
	 * Use the drm_dp_aux_init() to use the aux adapter
	 * before registering AUX with the DRM device so that
	 * msm eDP panel can be detected by generic_dep_panel_probe().
	 */
	aux->dp_aux.name = "dpu_dp_aux";
	aux->dp_aux.dev = dev;
	aux->dp_aux.transfer = dp_aux_transfer;
	aux->dp_aux.wait_hpd_asserted = dp_wait_hpd_asserted;
	drm_dp_aux_init(&aux->dp_aux);

	return &aux->dp_aux;
}

void dp_aux_put(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	if (!dp_aux)
		return;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	mutex_destroy(&aux->mutex);

	devm_kfree(aux->dev, aux);
}
