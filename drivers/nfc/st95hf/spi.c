// SPDX-License-Identifier: GPL-2.0-only
/*
 * ----------------------------------------------------------------------------
 * drivers/nfc/st95hf/spi.c function definitions for SPI communication
 * ----------------------------------------------------------------------------
 * Copyright (C) 2015 STMicroelectronics Pvt. Ltd. All rights reserved.
 */

#include "spi.h"

/* Function to send user provided buffer to ST95HF through SPI */
int st95hf_spi_send(struct st95hf_spi_context *spicontext,
		    unsigned char *buffertx,
		    int datalen,
		    enum req_type reqtype)
{
	struct spi_message m;
	int result = 0;
	struct spi_device *spidev = spicontext->spidev;
	struct spi_transfer tx_transfer = {
		.tx_buf = buffertx,
		.len = datalen,
	};

	mutex_lock(&spicontext->spi_lock);

	if (reqtype == SYNC) {
		spicontext->req_issync = true;
		reinit_completion(&spicontext->done);
	} else {
		spicontext->req_issync = false;
	}

	spi_message_init(&m);
	spi_message_add_tail(&tx_transfer, &m);

	result = spi_sync(spidev, &m);
	if (result) {
		dev_err(&spidev->dev, "error: sending cmd to st95hf using SPI = %d\n",
			result);
		mutex_unlock(&spicontext->spi_lock);
		return result;
	}

	/* return for asynchronous or no-wait case */
	if (reqtype == ASYNC) {
		mutex_unlock(&spicontext->spi_lock);
		return 0;
	}

	result = wait_for_completion_timeout(&spicontext->done,
					     msecs_to_jiffies(1000));
	/* check for timeout or success */
	if (!result) {
		dev_err(&spidev->dev, "error: response not ready timeout\n");
		result = -ETIMEDOUT;
	} else {
		result = 0;
	}

	mutex_unlock(&spicontext->spi_lock);

	return result;
}
EXPORT_SYMBOL_GPL(st95hf_spi_send);

/* Function to Receive command Response */
int st95hf_spi_recv_response(struct st95hf_spi_context *spicontext,
			     unsigned char *receivebuff)
{
	int len = 0;
	struct spi_transfer tx_takedata;
	struct spi_message m;
	struct spi_device *spidev = spicontext->spidev;
	unsigned char readdata_cmd = ST95HF_COMMAND_RECEIVE;
	struct spi_transfer t[2] = {
		{.tx_buf = &readdata_cmd, .len = 1,},
		{.rx_buf = receivebuff, .len = 2, .cs_change = 1,},
	};

	int ret = 0;

	memset(&tx_takedata, 0x0, sizeof(struct spi_transfer));

	mutex_lock(&spicontext->spi_lock);

	/* First spi transfer to know the length of valid data */
	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);

	ret = spi_sync(spidev, &m);
	if (ret) {
		dev_err(&spidev->dev, "spi_recv_resp, data length error = %d\n",
			ret);
		mutex_unlock(&spicontext->spi_lock);
		return ret;
	}

	/* As 2 bytes are already read */
	len = 2;

	/* Support of long frame */
	if (receivebuff[0] & 0x60)
		len += (((receivebuff[0] & 0x60) >> 5) << 8) | receivebuff[1];
	else
		len += receivebuff[1];

	/* Now make a transfer to read only relevant bytes */
	tx_takedata.rx_buf = &receivebuff[2];
	tx_takedata.len = len - 2;

	spi_message_init(&m);
	spi_message_add_tail(&tx_takedata, &m);

	ret = spi_sync(spidev, &m);

	mutex_unlock(&spicontext->spi_lock);
	if (ret) {
		dev_err(&spidev->dev, "spi_recv_resp, data read error = %d\n",
			ret);
		return ret;
	}

	return len;
}
EXPORT_SYMBOL_GPL(st95hf_spi_recv_response);

int st95hf_spi_recv_echo_res(struct st95hf_spi_context *spicontext,
			     unsigned char *receivebuff)
{
	unsigned char readdata_cmd = ST95HF_COMMAND_RECEIVE;
	struct spi_transfer t[2] = {
		{.tx_buf = &readdata_cmd, .len = 1,},
		{.rx_buf = receivebuff, .len = 1,},
	};
	struct spi_message m;
	struct spi_device *spidev = spicontext->spidev;
	int ret = 0;

	mutex_lock(&spicontext->spi_lock);

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);
	ret = spi_sync(spidev, &m);

	mutex_unlock(&spicontext->spi_lock);

	if (ret)
		dev_err(&spidev->dev, "recv_echo_res, data read error = %d\n",
			ret);

	return ret;
}
EXPORT_SYMBOL_GPL(st95hf_spi_recv_echo_res);
