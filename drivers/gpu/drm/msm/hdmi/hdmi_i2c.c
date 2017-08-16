/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "hdmi.h"

struct hdmi_i2c_adapter {
	struct i2c_adapter base;
	struct hdmi *hdmi;
	bool sw_done;
	wait_queue_head_t ddc_event;
};
#define to_hdmi_i2c_adapter(x) container_of(x, struct hdmi_i2c_adapter, base)

static void init_ddc(struct hdmi_i2c_adapter *hdmi_i2c)
{
	struct hdmi *hdmi = hdmi_i2c->hdmi;

	hdmi_write(hdmi, REG_HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_SW_STATUS_RESET);
	hdmi_write(hdmi, REG_HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_SOFT_RESET);

	hdmi_write(hdmi, REG_HDMI_DDC_SPEED,
			HDMI_DDC_SPEED_THRESHOLD(2) |
			HDMI_DDC_SPEED_PRESCALE(10));

	hdmi_write(hdmi, REG_HDMI_DDC_SETUP,
			HDMI_DDC_SETUP_TIMEOUT(0xff));

	/* enable reference timer for 27us */
	hdmi_write(hdmi, REG_HDMI_DDC_REF,
			HDMI_DDC_REF_REFTIMER_ENABLE |
			HDMI_DDC_REF_REFTIMER(27));
}

static int ddc_clear_irq(struct hdmi_i2c_adapter *hdmi_i2c)
{
	struct hdmi *hdmi = hdmi_i2c->hdmi;
	struct drm_device *dev = hdmi->dev;
	uint32_t retry = 0xffff;
	uint32_t ddc_int_ctrl;

	do {
		--retry;

		hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL,
				HDMI_DDC_INT_CTRL_SW_DONE_ACK |
				HDMI_DDC_INT_CTRL_SW_DONE_MASK);

		ddc_int_ctrl = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL);

	} while ((ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_INT) && retry);

	if (!retry) {
		dev_err(dev->dev, "timeout waiting for DDC\n");
		return -ETIMEDOUT;
	}

	hdmi_i2c->sw_done = false;

	return 0;
}

#define MAX_TRANSACTIONS 4

static bool sw_done(struct hdmi_i2c_adapter *hdmi_i2c)
{
	struct hdmi *hdmi = hdmi_i2c->hdmi;

	if (!hdmi_i2c->sw_done) {
		uint32_t ddc_int_ctrl;

		ddc_int_ctrl = hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL);

		if ((ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_MASK) &&
				(ddc_int_ctrl & HDMI_DDC_INT_CTRL_SW_DONE_INT)) {
			hdmi_i2c->sw_done = true;
			hdmi_write(hdmi, REG_HDMI_DDC_INT_CTRL,
					HDMI_DDC_INT_CTRL_SW_DONE_ACK);
		}
	}

	return hdmi_i2c->sw_done;
}

static int msm_hdmi_i2c_xfer(struct i2c_adapter *i2c,
		struct i2c_msg *msgs, int num)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(i2c);
	struct hdmi *hdmi = hdmi_i2c->hdmi;
	struct drm_device *dev = hdmi->dev;
	static const uint32_t nack[] = {
			HDMI_DDC_SW_STATUS_NACK0, HDMI_DDC_SW_STATUS_NACK1,
			HDMI_DDC_SW_STATUS_NACK2, HDMI_DDC_SW_STATUS_NACK3,
	};
	int indices[MAX_TRANSACTIONS];
	int ret, i, j, index = 0;
	uint32_t ddc_status, ddc_data, i2c_trans;

	num = min(num, MAX_TRANSACTIONS);

	WARN_ON(!(hdmi_read(hdmi, REG_HDMI_CTRL) & HDMI_CTRL_ENABLE));

	if (num == 0)
		return num;

	init_ddc(hdmi_i2c);

	ret = ddc_clear_irq(hdmi_i2c);
	if (ret)
		return ret;

	for (i = 0; i < num; i++) {
		struct i2c_msg *p = &msgs[i];
		uint32_t raw_addr = p->addr << 1;

		if (p->flags & I2C_M_RD)
			raw_addr |= 1;

		ddc_data = HDMI_DDC_DATA_DATA(raw_addr) |
				HDMI_DDC_DATA_DATA_RW(DDC_WRITE);

		if (i == 0) {
			ddc_data |= HDMI_DDC_DATA_INDEX(0) |
					HDMI_DDC_DATA_INDEX_WRITE;
		}

		hdmi_write(hdmi, REG_HDMI_DDC_DATA, ddc_data);
		index++;

		indices[i] = index;

		if (p->flags & I2C_M_RD) {
			index += p->len;
		} else {
			for (j = 0; j < p->len; j++) {
				ddc_data = HDMI_DDC_DATA_DATA(p->buf[j]) |
						HDMI_DDC_DATA_DATA_RW(DDC_WRITE);
				hdmi_write(hdmi, REG_HDMI_DDC_DATA, ddc_data);
				index++;
			}
		}

		i2c_trans = HDMI_I2C_TRANSACTION_REG_CNT(p->len) |
				HDMI_I2C_TRANSACTION_REG_RW(
						(p->flags & I2C_M_RD) ? DDC_READ : DDC_WRITE) |
				HDMI_I2C_TRANSACTION_REG_START;

		if (i == (num - 1))
			i2c_trans |= HDMI_I2C_TRANSACTION_REG_STOP;

		hdmi_write(hdmi, REG_HDMI_I2C_TRANSACTION(i), i2c_trans);
	}

	/* trigger the transfer: */
	hdmi_write(hdmi, REG_HDMI_DDC_CTRL,
			HDMI_DDC_CTRL_TRANSACTION_CNT(num - 1) |
			HDMI_DDC_CTRL_GO);

	ret = wait_event_timeout(hdmi_i2c->ddc_event, sw_done(hdmi_i2c), HZ/4);
	if (ret <= 0) {
		if (ret == 0)
			ret = -ETIMEDOUT;
		dev_warn(dev->dev, "DDC timeout: %d\n", ret);
		DBG("sw_status=%08x, hw_status=%08x, int_ctrl=%08x",
				hdmi_read(hdmi, REG_HDMI_DDC_SW_STATUS),
				hdmi_read(hdmi, REG_HDMI_DDC_HW_STATUS),
				hdmi_read(hdmi, REG_HDMI_DDC_INT_CTRL));
		return ret;
	}

	ddc_status = hdmi_read(hdmi, REG_HDMI_DDC_SW_STATUS);

	/* read back results of any read transactions: */
	for (i = 0; i < num; i++) {
		struct i2c_msg *p = &msgs[i];

		if (!(p->flags & I2C_M_RD))
			continue;

		/* check for NACK: */
		if (ddc_status & nack[i]) {
			DBG("ddc_status=%08x", ddc_status);
			break;
		}

		ddc_data = HDMI_DDC_DATA_DATA_RW(DDC_READ) |
				HDMI_DDC_DATA_INDEX(indices[i]) |
				HDMI_DDC_DATA_INDEX_WRITE;

		hdmi_write(hdmi, REG_HDMI_DDC_DATA, ddc_data);

		/* discard first byte: */
		hdmi_read(hdmi, REG_HDMI_DDC_DATA);

		for (j = 0; j < p->len; j++) {
			ddc_data = hdmi_read(hdmi, REG_HDMI_DDC_DATA);
			p->buf[j] = FIELD(ddc_data, HDMI_DDC_DATA_DATA);
		}
	}

	return i;
}

static u32 msm_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm msm_hdmi_i2c_algorithm = {
	.master_xfer	= msm_hdmi_i2c_xfer,
	.functionality	= msm_hdmi_i2c_func,
};

void msm_hdmi_i2c_irq(struct i2c_adapter *i2c)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(i2c);

	if (sw_done(hdmi_i2c))
		wake_up_all(&hdmi_i2c->ddc_event);
}

void msm_hdmi_i2c_destroy(struct i2c_adapter *i2c)
{
	struct hdmi_i2c_adapter *hdmi_i2c = to_hdmi_i2c_adapter(i2c);
	i2c_del_adapter(i2c);
	kfree(hdmi_i2c);
}

struct i2c_adapter *msm_hdmi_i2c_init(struct hdmi *hdmi)
{
	struct hdmi_i2c_adapter *hdmi_i2c;
	struct i2c_adapter *i2c = NULL;
	int ret;

	hdmi_i2c = kzalloc(sizeof(*hdmi_i2c), GFP_KERNEL);
	if (!hdmi_i2c) {
		ret = -ENOMEM;
		goto fail;
	}

	i2c = &hdmi_i2c->base;

	hdmi_i2c->hdmi = hdmi;
	init_waitqueue_head(&hdmi_i2c->ddc_event);


	i2c->owner = THIS_MODULE;
	i2c->class = I2C_CLASS_DDC;
	snprintf(i2c->name, sizeof(i2c->name), "msm hdmi i2c");
	i2c->dev.parent = &hdmi->pdev->dev;
	i2c->algo = &msm_hdmi_i2c_algorithm;

	ret = i2c_add_adapter(i2c);
	if (ret)
		goto fail;

	return i2c;

fail:
	if (i2c)
		msm_hdmi_i2c_destroy(i2c);
	return ERR_PTR(ret);
}
