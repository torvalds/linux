/*
 * adv7511_cec.c - Analog Devices ADV7511/33 cec driver
 *
 * Copyright 2017 Cisco Systems, Inc. and/or its affiliates. All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include <media/cec.h>

#include "adv7511.h"

#define ADV7511_INT1_CEC_MASK \
	(ADV7511_INT1_CEC_TX_READY | ADV7511_INT1_CEC_TX_ARBIT_LOST | \
	 ADV7511_INT1_CEC_TX_RETRY_TIMEOUT | ADV7511_INT1_CEC_RX_READY1)

static void adv_cec_tx_raw_status(struct adv7511 *adv7511, u8 tx_raw_status)
{
	unsigned int offset = adv7511->type == ADV7533 ?
					ADV7533_REG_CEC_OFFSET : 0;
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

void adv7511_cec_irq_process(struct adv7511 *adv7511, unsigned int irq1)
{
	unsigned int offset = adv7511->type == ADV7533 ?
					ADV7533_REG_CEC_OFFSET : 0;
	const u32 irq_tx_mask = ADV7511_INT1_CEC_TX_READY |
				ADV7511_INT1_CEC_TX_ARBIT_LOST |
				ADV7511_INT1_CEC_TX_RETRY_TIMEOUT;
	struct cec_msg msg = {};
	unsigned int len;
	unsigned int val;
	u8 i;

	if (irq1 & irq_tx_mask)
		adv_cec_tx_raw_status(adv7511, irq1);

	if (!(irq1 & ADV7511_INT1_CEC_RX_READY1))
		return;

	if (regmap_read(adv7511->regmap_cec,
			ADV7511_REG_CEC_RX_FRAME_LEN + offset, &len))
		return;

	msg.len = len & 0x1f;

	if (msg.len > 16)
		msg.len = 16;

	if (!msg.len)
		return;

	for (i = 0; i < msg.len; i++) {
		regmap_read(adv7511->regmap_cec,
			    i + ADV7511_REG_CEC_RX_FRAME_HDR + offset, &val);
		msg.msg[i] = val;
	}

	/* toggle to re-enable rx 1 */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_RX_BUFFERS + offset, 1);
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_RX_BUFFERS + offset, 0);
	cec_received_msg(adv7511->cec_adap, &msg);
}

static int adv7511_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct adv7511 *adv7511 = cec_get_drvdata(adap);
	unsigned int offset = adv7511->type == ADV7533 ?
					ADV7533_REG_CEC_OFFSET : 0;

	if (adv7511->i2c_cec == NULL)
		return -EIO;

	if (!adv7511->cec_enabled_adap && enable) {
		/* power up cec section */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_CLK_DIV + offset,
				   0x03, 0x01);
		/* legacy mode and clear all rx buffers */
		regmap_write(adv7511->regmap_cec,
			     ADV7511_REG_CEC_RX_BUFFERS + offset, 0x07);
		regmap_write(adv7511->regmap_cec,
			     ADV7511_REG_CEC_RX_BUFFERS + offset, 0);
		/* initially disable tx */
		regmap_update_bits(adv7511->regmap_cec,
				   ADV7511_REG_CEC_TX_ENABLE + offset, 1, 0);
		/* enabled irqs: */
		/* tx: ready */
		/* tx: arbitration lost */
		/* tx: retry timeout */
		/* rx: ready 1 */
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
	unsigned int offset = adv7511->type == ADV7533 ?
					ADV7533_REG_CEC_OFFSET : 0;
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
	unsigned int offset = adv7511->type == ADV7533 ?
					ADV7533_REG_CEC_OFFSET : 0;
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

int adv7511_cec_init(struct device *dev, struct adv7511 *adv7511,
		     unsigned int offset)
{
	int ret = adv7511_cec_parse_dt(dev, adv7511);

	if (ret)
		return ret;

	adv7511->cec_adap = cec_allocate_adapter(&adv7511_cec_adap_ops,
		adv7511, dev_name(dev), CEC_CAP_DEFAULTS, ADV7511_MAX_ADDRS);
	if (IS_ERR(adv7511->cec_adap))
		return PTR_ERR(adv7511->cec_adap);

	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL + offset, 0);
	/* cec soft reset */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_SOFT_RESET + offset, 0x01);
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_SOFT_RESET + offset, 0x00);

	/* legacy mode */
	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_RX_BUFFERS + offset, 0x00);

	regmap_write(adv7511->regmap_cec,
		     ADV7511_REG_CEC_CLK_DIV + offset,
		     ((adv7511->cec_clk_freq / 750000) - 1) << 2);

	ret = cec_register_adapter(adv7511->cec_adap, dev);
	if (ret) {
		cec_delete_adapter(adv7511->cec_adap);
		adv7511->cec_adap = NULL;
	}
	return ret;
}
