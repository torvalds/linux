// SPDX-License-Identifier: GPL-2.0-only
/*
 * adv7511_cec.c - Analog Devices ADV7511/33 cec driver
 *
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <media/cec.h>

#include "adv7511.h"

static const u8 ADV7511_REG_CEC_RX_FRAME_HDR[] = {
	ADV7511_REG_CEC_RX1_FRAME_HDR,
	ADV7511_REG_CEC_RX2_FRAME_HDR,
	ADV7511_REG_CEC_RX3_FRAME_HDR,
};

static const u8 ADV7511_REG_CEC_RX_FRAME_LEN[] = {
	ADV7511_REG_CEC_RX1_FRAME_LEN,
	ADV7511_REG_CEC_RX2_FRAME_LEN,
	ADV7511_REG_CEC_RX3_FRAME_LEN,
};

#define ADV7511_INT1_CEC_MASK \
	(ADV7511_INT1_CEC_TX_READY | ADV7511_INT1_CEC_TX_ARBIT_LOST | \
	 ADV7511_INT1_CEC_TX_RETRY_TIMEOUT | ADV7511_INT1_CEC_RX_READY1 | \
	 ADV7511_INT1_CEC_RX_READY2 | ADV7511_INT1_CEC_RX_READY3)

static void adv_cec_tx_raw_status(struct adv7511 *adv7511, u8 tx_raw_status)
{
	unsigned int offset = adv7511->reg_cec_offset;
	unsigned int val;

	if (regmap_read(adv7511->regmap_cec,
			ADV7511_REG_CEC_TX_ENABLE + offset, &val))
		return;

	if ((val & 0x01) == 0)
		return;

	if (tx_raw_status & ADV7511_INT1_CEC_TX_ARBIT_LOST) {
		cec_transmit_attempt_done(adv7511->cec_adap,
					  CEC_TX_STATUS_ARB_LOST);
		return;
	}
	if (tx_raw_status & ADV7511_INT1_CEC_TX_RETRY_TIMEOUT) {
		u8 status;
		u8 err_cnt = 0;
		u8 nack_cnt = 0;
		u8 low_drive_cnt = 0;
		unsigned int cnt;

		/*
		 * We set this status bit since this hardware performs
		 * retransmissions.
		 */
		status = CEC_TX_STATUS_MAX_RETRIES;
		if (regmap_read(adv7511->regmap_cec,
			    ADV7511_REG_CEC_TX_LOW_DRV_CNT + offset, &cnt)) {
			err_cnt = 1;
			status |= CEC_TX_STATUS_ERROR;
		} else {
			nack_cnt = cnt & 0xf;
			if (nack_cnt)
				status |= CEC_TX_STATUS_NACK;
			low_drive_cnt = cnt >> 4;
			if (low_drive_cnt)
				status |= CEC_TX_STATUS_LOW_DRIVE;
		}
		cec_transmit_done(adv7511->cec_adap, status,
				  0, nack_cnt, low_drive_cnt, err_cnt);
		return;
	}
	if (tx_raw_status & ADV7511_INT1_CEC_TX_READY) {
		cec_transmit_attempt_done(adv7511->cec_adap, CEC_TX_STATUS_OK);
		return;
	}
}

static void adv7511_cec_rx(struct adv7511 *adv7511, int rx_buf)
{
	unsigned int offset = adv7511->reg_cec_offset;
	struct cec_msg msg = {};
	unsigned int len;
	unsigned int val;
	u8 i;

	if (regmap_read(adv7511->regmap_cec,
			ADV7511_REG_CEC_RX_FRAME_LEN[rx_buf] + offset, &len))
		return;

	msg.len = len & 0x1f;

	if (msg.len > 16)
		msg.len = 16;

	if (!msg.len)
		return;

	for (i = 0; i < msg.len; i++) {
		regmap_read(adv7511->regmap_cec,
			    i + ADV7511_REG_CEC_RX_FRAME_HDR[rx_buf] + offset,
			    &val);
		msg.msg[i] = val;
	}

	/* Toggle RX Ready Clear bit to re-enable this RX buffer */
	regmap_update_bits(adv7511->regmap_cec,
			   ADV7511_REG_CEC_RX_BUFFERS + offset, BIT(rx_buf),
			   BIT(rx_buf));
	regmap_update_bits(adv7511->regmap_cec,
			   ADV7511_REG_CEC_RX_BUFFERS + offset, BIT(rx_buf), 0);

	cec_received_msg(adv7511->cec_adap, &msg);
}

void adv7511_cec_irq_process(struct adv7511 *adv7511, unsigned int irq1)
{
	unsigned int offset = adv7511->reg_cec_offset;
	const u32 irq_tx_mask = ADV7511_INT1_CEC_TX_READY |
				ADV7511_INT1_CEC_TX_ARBIT_LOST |
				ADV7511_INT1_CEC_TX_RETRY_TIMEOUT;
	const u32 irq_rx_mask = ADV7511_INT1_CEC_RX_READY1 |
				ADV7511_INT1_CEC_RX_READY2 |
				ADV7511_INT1_CEC_RX_READY3;
	unsigned int rx_status;
	int rx_order[3] = { -1, -1, -1 };
	int i;

	if (irq1 & irq_tx_mask)
		adv_cec_tx_raw_status(adv7511, irq1);

	if (!(irq1 & irq_rx_mask))
		return;

	if (regmap_read(adv7511->regmap_cec,
			ADV7511_REG_CEC_RX_STATUS + offset, &rx_status))
		return;

	/*
	 * ADV7511_REG_CEC_RX_STATUS[5:0] contains the reception order of RX
	 * buffers 0, 1, and 2 in bits [1:0], [3:2], and [5:4] respectively.
	 * The values are to be interpreted as follows:
	 *
	 *   0 = buffer unused
	 *   1 = buffer contains oldest received frame (if applicable)
	 *   2 = buffer contains second oldest received frame (if applicable)
	 *   3 = buffer contains third oldest received frame (if applicable)
	 *
	 * Fill rx_order with the sequence of RX buffer indices to
	 * read from in order, where -1 indicates that there are no
	 * more buffers to process.
	 */
	for (i = 0; i < 3; i++) {
		unsigned int timestamp = (rx_status >> (2 * i)) & 0x3;

		if (timestamp)
			rx_order[timestamp - 1] = i;
	}

	/* Read CEC RX buffers in the appropriate order as prescribed above */
	for (i = 0; i < 3; i++) {
		int rx_buf = rx_order[i];

		if (rx_buf < 0)
			break;

		adv7511_cec_rx(adv7511, rx_buf);
	}
}

static int adv7511_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct adv7511 *adv7511 = cec_get_drvdata(adap);
	unsigned int offset = adv7511->reg_cec_offset;

	if (adv7511->i2c_cec == NULL)
		return -EIO;

	if (!adv7511->cec_enabled_adap && enable) {
		/* power up cec section */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_CLK_DIV + offset,
				   0x03, 0x01);
		/* non-legacy mode and clear all rx buffers */
		regmap_write(adv7511->regmap_cec,
			     ADV7511_REG_CEC_RX_BUFFERS + offset, 0x0f);
		regmap_write(adv7511->regmap_cec,
			     ADV7511_REG_CEC_RX_BUFFERS + offset, 0x08);
		/* initially disable tx */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_TX_ENABLE + offset, 1, 0);
		/* enabled irqs: */
		/* tx: ready */
		/* tx: arbitration lost */
		/* tx: retry timeout */
		/* rx: ready 1-3 */
		regmap_update_bits(adv7511->regmap,
				   ADV7511_REG_INT_ENABLE(1), 0x3f,
				   ADV7511_INT1_CEC_MASK);
	} else if (adv7511->cec_enabled_adap && !enable) {
		regmap_update_bits(adv7511->regmap,
				   ADV7511_REG_INT_ENABLE(1), 0x3f, 0);
		/* disable address mask 1-3 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x70, 0x00);
		/* power down cec section */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_CLK_DIV + offset,
				   0x03, 0x00);
		adv7511->cec_valid_addrs = 0;
	}
	adv7511->cec_enabled_adap = enable;
	return 0;
}

static int adv7511_cec_adap_log_addr(struct cec_adapter *adap, u8 addr)
{
	struct adv7511 *adv7511 = cec_get_drvdata(adap);
	unsigned int offset = adv7511->reg_cec_offset;
	unsigned int i, free_idx = ADV7511_MAX_ADDRS;

	if (!adv7511->cec_enabled_adap)
		return addr == CEC_LOG_ADDR_INVALID ? 0 : -EIO;

	if (addr == CEC_LOG_ADDR_INVALID) {
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x70, 0);
		adv7511->cec_valid_addrs = 0;
		return 0;
	}

	for (i = 0; i < ADV7511_MAX_ADDRS; i++) {
		bool is_valid = adv7511->cec_valid_addrs & (1 << i);

		if (free_idx == ADV7511_MAX_ADDRS && !is_valid)
			free_idx = i;
		if (is_valid && adv7511->cec_addr[i] == addr)
			return 0;
	}
	if (i == ADV7511_MAX_ADDRS) {
		i = free_idx;
		if (i == ADV7511_MAX_ADDRS)
			return -ENXIO;
	}
	adv7511->cec_addr[i] = addr;
	adv7511->cec_valid_addrs |= 1 << i;

	switch (i) {
	case 0:
		/* enable address mask 0 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x10, 0x10);
		/* set address for mask 0 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_0_1 + offset,
				   0x0f, addr);
		break;
	case 1:
		/* enable address mask 1 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x20, 0x20);
		/* set address for mask 1 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_0_1 + offset,
				   0xf0, addr << 4);
		break;
	case 2:
		/* enable address mask 2 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_MASK + offset,
				   0x40, 0x40);
		/* set address for mask 1 */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_LOG_ADDR_2 + offset,
				   0x0f, addr);
		break;
	}
	return 0;
}

static int adv7511_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				     u32 signal_free_time, struct cec_msg *msg)
{
	struct adv7511 *adv7511 = cec_get_drvdata(adap);
	unsigned int offset = adv7511->reg_cec_offset;
	u8 len = msg->len;
	unsigned int i;

	/*
	 * The number of retries is the number of attempts - 1, but retry
	 * at least once. It's not clear if a value of 0 is allowed, so
	 * let's do at least one retry.
	 */
	regmap_update_bits(adv7511->regmap_cec,
			   ADV7511_REG_CEC_TX_RETRY + offset,
			   0x70, max(1, attempts - 1) << 4);

	/* blocking, clear cec tx irq status */
	regmap_update_bits(adv7511->regmap, ADV7511_REG_INT(1), 0x38, 0x38);

	/* write data */
	for (i = 0; i < len; i++)
		regmap_write(adv7511->regmap_cec,
			     i + ADV7511_REG_CEC_TX_FRAME_HDR + offset,
			     msg->msg[i]);

	/* set length (data + header) */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_TX_FRAME_LEN + offset, len);
	/* start transmit, enable tx */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_TX_ENABLE + offset, 0x01);
	return 0;
}

static const struct cec_adap_ops adv7511_cec_adap_ops = {
	.adap_enable = adv7511_cec_adap_enable,
	.adap_log_addr = adv7511_cec_adap_log_addr,
	.adap_transmit = adv7511_cec_adap_transmit,
};

static int adv7511_cec_parse_dt(struct device *dev, struct adv7511 *adv7511)
{
	adv7511->cec_clk = devm_clk_get(dev, "cec");
	if (IS_ERR(adv7511->cec_clk)) {
		int ret = PTR_ERR(adv7511->cec_clk);

		adv7511->cec_clk = NULL;
		return ret;
	}
	clk_prepare_enable(adv7511->cec_clk);
	adv7511->cec_clk_freq = clk_get_rate(adv7511->cec_clk);
	return 0;
}

int adv7511_cec_init(struct device *dev, struct adv7511 *adv7511)
{
	unsigned int offset = adv7511->reg_cec_offset;
	int ret = adv7511_cec_parse_dt(dev, adv7511);

	if (ret)
		goto err_cec_parse_dt;

	adv7511->cec_adap = cec_allocate_adapter(&adv7511_cec_adap_ops,
		adv7511, dev_name(dev), CEC_CAP_DEFAULTS, ADV7511_MAX_ADDRS);
	if (IS_ERR(adv7511->cec_adap)) {
		ret = PTR_ERR(adv7511->cec_adap);
		goto err_cec_alloc;
	}

	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL + offset, 0);
	/* cec soft reset */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_SOFT_RESET + offset, 0x01);
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_SOFT_RESET + offset, 0x00);

	/* non-legacy mode - use all three RX buffers */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_RX_BUFFERS + offset, 0x08);

	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_CLK_DIV + offset,
		     ((adv7511->cec_clk_freq / 750000) - 1) << 2);

	ret = cec_register_adapter(adv7511->cec_adap, dev);
	if (ret)
		goto err_cec_register;
	return 0;

err_cec_register:
	cec_delete_adapter(adv7511->cec_adap);
	adv7511->cec_adap = NULL;
err_cec_alloc:
	dev_info(dev, "Initializing CEC failed with error %d, disabling CEC\n",
		 ret);
err_cec_parse_dt:
	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL + offset,
		     ADV7511_CEC_CTRL_POWER_DOWN);
	return ret == -EPROBE_DEFER ? ret : 0;
}
