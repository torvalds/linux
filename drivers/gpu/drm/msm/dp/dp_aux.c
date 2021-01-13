// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#include <linux/delay.h>
#include <drm/drm_print.h>

#include "dp_reg.h"
#include "dp_aux.h"

#define DP_AUX_ENUM_STR(x)		#x

struct dp_aux_private {
	struct device *dev;
	struct dp_catalog *catalog;

	struct mutex mutex;
	struct completion comp;

	u32 aux_error_num;
	u32 retry_cnt;
	bool cmd_busy;
	bool native;
	bool read;
	bool no_send_addr;
	bool no_send_stop;
	u32 offset;
	u32 segment;
	u32 isr;

	struct drm_dp_aux dp_aux;
};

static const char *dp_aux_get_error(u32 aux_error)
{
	switch (aux_error) {
	case DP_AUX_ERR_NONE:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_NONE);
	case DP_AUX_ERR_ADDR:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_ADDR);
	case DP_AUX_ERR_TOUT:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_TOUT);
	case DP_AUX_ERR_NACK:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_NACK);
	case DP_AUX_ERR_DEFER:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_DEFER);
	case DP_AUX_ERR_NACK_DEFER:
		return DP_AUX_ENUM_STR(DP_AUX_ERR_NACK_DEFER);
	default:
		return "unknown";
	}
}

static u32 dp_aux_write(struct dp_aux_private *aux,
			struct drm_dp_aux_msg *msg)
{
	u32 data[4], reg, len;
	u8 *msgdata = msg->buffer;
	int const AUX_CMD_FIFO_LEN = 128;
	int i = 0;

	if (aux->read)
		len = 4;
	else
		len = msg->size + 4;

	/*
	 * cmd fifo only has depth of 144 bytes
	 * limit buf length to 128 bytes here
	 */
	if (len > AUX_CMD_FIFO_LEN) {
		DRM_ERROR("buf size greater than allowed size of 128 bytes\n");
		return 0;
	}

	/* Pack cmd and write to HW */
	data[0] = (msg->address >> 16) & 0xf; /* addr[19:16] */
	if (aux->read)
		data[0] |=  BIT(4); /* R/W */

	data[1] = (msg->address >> 8) & 0xff;	/* addr[15:8] */
	data[2] = msg->address & 0xff;		/* addr[7:0] */
	data[3] = (msg->size - 1) & 0xff;	/* len[7:0] */

	for (i = 0; i < len; i++) {
		reg = (i < 4) ? data[i] : msgdata[i - 4];
		/* index = 0, write */
		reg = (((reg) << DP_AUX_DATA_OFFSET)
		       & DP_AUX_DATA_MASK) | DP_AUX_DATA_WRITE;
		if (i == 0)
			reg |= DP_AUX_DATA_INDEX_WRITE;
		aux->catalog->aux_data = reg;
		dp_catalog_aux_write_data(aux->catalog);
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
	aux->catalog->aux_data = reg;
	dp_catalog_aux_write_trans(aux->catalog);

	return len;
}

static int dp_aux_cmd_fifo_tx(struct dp_aux_private *aux,
			      struct drm_dp_aux_msg *msg)
{
	u32 ret, len, timeout;
	int aux_timeout_ms = HZ/4;

	reinit_completion(&aux->comp);

	len = dp_aux_write(aux, msg);
	if (len == 0) {
		DRM_ERROR("DP AUX write failed\n");
		return -EINVAL;
	}

	timeout = wait_for_completion_timeout(&aux->comp, aux_timeout_ms);
	if (!timeout) {
		DRM_ERROR("aux %s timeout\n", (aux->read ? "read" : "write"));
		return -ETIMEDOUT;
	}

	if (aux->aux_error_num == DP_AUX_ERR_NONE) {
		ret = len;
	} else {
		DRM_ERROR_RATELIMITED("aux err: %s\n",
			dp_aux_get_error(aux->aux_error_num));

		ret = -EINVAL;
	}

	return ret;
}

static void dp_aux_cmd_fifo_rx(struct dp_aux_private *aux,
		struct drm_dp_aux_msg *msg)
{
	u32 data;
	u8 *dp;
	u32 i, actual_i;
	u32 len = msg->size;

	dp_catalog_aux_clear_trans(aux->catalog, true);

	data = DP_AUX_DATA_INDEX_WRITE; /* INDEX_WRITE */
	data |= DP_AUX_DATA_READ;  /* read */

	aux->catalog->aux_data = data;
	dp_catalog_aux_write_data(aux->catalog);

	dp = msg->buffer;

	/* discard first byte */
	data = dp_catalog_aux_read_data(aux->catalog);

	for (i = 0; i < len; i++) {
		data = dp_catalog_aux_read_data(aux->catalog);
		*dp++ = (u8)((data >> DP_AUX_DATA_OFFSET) & 0xff);

		actual_i = (data >> DP_AUX_DATA_INDEX_OFFSET) & 0xFF;
		if (i != actual_i)
			DRM_ERROR("Index mismatch: expected %d, found %d\n",
				i, actual_i);
	}
}

static void dp_aux_native_handler(struct dp_aux_private *aux)
{
	u32 isr = aux->isr;

	if (isr & DP_INTR_AUX_I2C_DONE)
		aux->aux_error_num = DP_AUX_ERR_NONE;
	else if (isr & DP_INTR_WRONG_ADDR)
		aux->aux_error_num = DP_AUX_ERR_ADDR;
	else if (isr & DP_INTR_TIMEOUT)
		aux->aux_error_num = DP_AUX_ERR_TOUT;
	if (isr & DP_INTR_NACK_DEFER)
		aux->aux_error_num = DP_AUX_ERR_NACK;
	if (isr & DP_INTR_AUX_ERROR) {
		aux->aux_error_num = DP_AUX_ERR_PHY;
		dp_catalog_aux_clear_hw_interrupts(aux->catalog);
	}

	complete(&aux->comp);
}

static void dp_aux_i2c_handler(struct dp_aux_private *aux)
{
	u32 isr = aux->isr;

	if (isr & DP_INTR_AUX_I2C_DONE) {
		if (isr & (DP_INTR_I2C_NACK | DP_INTR_I2C_DEFER))
			aux->aux_error_num = DP_AUX_ERR_NACK;
		else
			aux->aux_error_num = DP_AUX_ERR_NONE;
	} else {
		if (isr & DP_INTR_WRONG_ADDR)
			aux->aux_error_num = DP_AUX_ERR_ADDR;
		else if (isr & DP_INTR_TIMEOUT)
			aux->aux_error_num = DP_AUX_ERR_TOUT;
		if (isr & DP_INTR_NACK_DEFER)
			aux->aux_error_num = DP_AUX_ERR_NACK_DEFER;
		if (isr & DP_INTR_I2C_NACK)
			aux->aux_error_num = DP_AUX_ERR_NACK;
		if (isr & DP_INTR_I2C_DEFER)
			aux->aux_error_num = DP_AUX_ERR_DEFER;
		if (isr & DP_INTR_AUX_ERROR) {
			aux->aux_error_num = DP_AUX_ERR_PHY;
			dp_catalog_aux_clear_hw_interrupts(aux->catalog);
		}
	}

	complete(&aux->comp);
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
	struct dp_aux_private *aux = container_of(dp_aux,
		struct dp_aux_private, dp_aux);

	mutex_lock(&aux->mutex);

	aux->native = msg->request & (DP_AUX_NATIVE_WRITE & DP_AUX_NATIVE_READ);

	/* Ignore address only message */
	if ((msg->size == 0) || (msg->buffer == NULL)) {
		msg->reply = aux->native ?
			DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
		ret = msg->size;
		goto unlock_exit;
	}

	/* msg sanity check */
	if ((aux->native && (msg->size > aux_cmd_native_max)) ||
		(msg->size > aux_cmd_i2c_max)) {
		DRM_ERROR("%s: invalid msg: size(%zu), request(%x)\n",
			__func__, msg->size, msg->request);
		ret = -EINVAL;
		goto unlock_exit;
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
		usleep_range(400, 500); /* at least 400us to next try */
		goto unlock_exit;
	}

	if (aux->aux_error_num == DP_AUX_ERR_NONE) {
		if (aux->read)
			dp_aux_cmd_fifo_rx(aux, msg);

		msg->reply = aux->native ?
			DP_AUX_NATIVE_REPLY_ACK : DP_AUX_I2C_REPLY_ACK;
	} else {
		/* Reply defer to retry */
		msg->reply = aux->native ?
			DP_AUX_NATIVE_REPLY_DEFER : DP_AUX_I2C_REPLY_DEFER;
	}

	/* Return requested size for success or retry */
	ret = msg->size;
	aux->retry_cnt = 0;

unlock_exit:
	aux->cmd_busy = false;
	mutex_unlock(&aux->mutex);
	return ret;
}

void dp_aux_isr(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	if (!dp_aux) {
		DRM_ERROR("invalid input\n");
		return;
	}

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	aux->isr = dp_catalog_aux_get_irq(aux->catalog);

	if (!aux->cmd_busy)
		return;

	if (aux->native)
		dp_aux_native_handler(aux);
	else
		dp_aux_i2c_handler(aux);
}

void dp_aux_reconfig(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	dp_catalog_aux_update_cfg(aux->catalog);
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

	dp_catalog_aux_enable(aux->catalog, true);
	aux->retry_cnt = 0;
}

void dp_aux_deinit(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	dp_catalog_aux_enable(aux->catalog, false);
}

int dp_aux_register(struct drm_dp_aux *dp_aux)
{
	struct dp_aux_private *aux;
	int ret;

	if (!dp_aux) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	aux = container_of(dp_aux, struct dp_aux_private, dp_aux);

	aux->dp_aux.name = "dpu_dp_aux";
	aux->dp_aux.dev = aux->dev;
	aux->dp_aux.transfer = dp_aux_transfer;
	ret = drm_dp_aux_register(&aux->dp_aux);
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

struct drm_dp_aux *dp_aux_get(struct device *dev, struct dp_catalog *catalog)
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
	mutex_init(&aux->mutex);

	aux->dev = dev;
	aux->catalog = catalog;
	aux->retry_cnt = 0;

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
