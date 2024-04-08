// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/of_irq.h>
#include "i2c-viai2c-common.h"

int wmt_i2c_wait_bus_not_busy(struct wmt_i2c_dev *i2c_dev)
{
	unsigned long timeout;

	timeout = jiffies + WMT_I2C_TIMEOUT;
	while (!(readw(i2c_dev->base + REG_CSR) & CSR_READY_MASK)) {
		if (time_after(jiffies, timeout)) {
			dev_warn(i2c_dev->dev, "timeout waiting for bus ready\n");
			return -EBUSY;
		}
		msleep(20);
	}

	return 0;
}

int wmt_check_status(struct wmt_i2c_dev *i2c_dev)
{
	int ret = 0;
	unsigned long wait_result;

	wait_result = wait_for_completion_timeout(&i2c_dev->complete,
						  msecs_to_jiffies(500));
	if (!wait_result)
		return -ETIMEDOUT;

	if (i2c_dev->cmd_status & ISR_NACK_ADDR)
		ret = -EIO;

	if (i2c_dev->cmd_status & ISR_SCL_TIMEOUT)
		ret = -ETIMEDOUT;

	return ret;
}

static int wmt_i2c_write(struct wmt_i2c_dev *i2c_dev, struct i2c_msg *pmsg, int last)
{
	u16 val, tcr_val = i2c_dev->tcr;
	int ret;
	int xfer_len = 0;

	if (pmsg->len == 0) {
		/*
		 * We still need to run through the while (..) once, so
		 * start at -1 and break out early from the loop
		 */
		xfer_len = -1;
		writew(0, i2c_dev->base + REG_CDR);
	} else {
		writew(pmsg->buf[0] & 0xFF, i2c_dev->base + REG_CDR);
	}

	if (!(pmsg->flags & I2C_M_NOSTART)) {
		val = readw(i2c_dev->base + REG_CR);
		val &= ~CR_TX_END;
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	reinit_completion(&i2c_dev->complete);

	tcr_val |= (TCR_MASTER_WRITE | (pmsg->addr & TCR_SLAVE_ADDR_MASK));

	writew(tcr_val, i2c_dev->base + REG_TCR);

	if (pmsg->flags & I2C_M_NOSTART) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	while (xfer_len < pmsg->len) {
		ret = wmt_check_status(i2c_dev);
		if (ret)
			return ret;

		xfer_len++;

		val = readw(i2c_dev->base + REG_CSR);
		if ((val & CSR_RCV_ACK_MASK) == CSR_RCV_NOT_ACK) {
			dev_dbg(i2c_dev->dev, "write RCV NACK error\n");
			return -EIO;
		}

		if (pmsg->len == 0) {
			val = CR_TX_END | CR_CPU_RDY | CR_ENABLE;
			writew(val, i2c_dev->base + REG_CR);
			break;
		}

		if (xfer_len == pmsg->len) {
			if (last != 1)
				writew(CR_ENABLE, i2c_dev->base + REG_CR);
		} else {
			writew(pmsg->buf[xfer_len] & 0xFF, i2c_dev->base +
								REG_CDR);
			writew(CR_CPU_RDY | CR_ENABLE, i2c_dev->base + REG_CR);
		}
	}

	return 0;
}

static int wmt_i2c_read(struct wmt_i2c_dev *i2c_dev, struct i2c_msg *pmsg)
{
	u16 val, tcr_val = i2c_dev->tcr;
	int ret;
	u32 xfer_len = 0;

	val = readw(i2c_dev->base + REG_CR);
	val &= ~(CR_TX_END | CR_TX_NEXT_NO_ACK);

	if (!(pmsg->flags & I2C_M_NOSTART))
		val |= CR_CPU_RDY;

	if (pmsg->len == 1)
		val |= CR_TX_NEXT_NO_ACK;

	writew(val, i2c_dev->base + REG_CR);

	reinit_completion(&i2c_dev->complete);

	tcr_val |= TCR_MASTER_READ | (pmsg->addr & TCR_SLAVE_ADDR_MASK);

	writew(tcr_val, i2c_dev->base + REG_TCR);

	if (pmsg->flags & I2C_M_NOSTART) {
		val = readw(i2c_dev->base + REG_CR);
		val |= CR_CPU_RDY;
		writew(val, i2c_dev->base + REG_CR);
	}

	while (xfer_len < pmsg->len) {
		ret = wmt_check_status(i2c_dev);
		if (ret)
			return ret;

		pmsg->buf[xfer_len] = readw(i2c_dev->base + REG_CDR) >> 8;
		xfer_len++;

		val = readw(i2c_dev->base + REG_CR) | CR_CPU_RDY;
		if (xfer_len == pmsg->len - 1)
			val |= CR_TX_NEXT_NO_ACK;
		writew(val, i2c_dev->base + REG_CR);
	}

	return 0;
}

int wmt_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct i2c_msg *pmsg;
	int i;
	int ret = 0;
	struct wmt_i2c_dev *i2c_dev = i2c_get_adapdata(adap);

	for (i = 0; ret >= 0 && i < num; i++) {
		pmsg = &msgs[i];
		if (!(pmsg->flags & I2C_M_NOSTART)) {
			ret = wmt_i2c_wait_bus_not_busy(i2c_dev);
			if (ret < 0)
				return ret;
		}

		if (pmsg->flags & I2C_M_RD)
			ret = wmt_i2c_read(i2c_dev, pmsg);
		else
			ret = wmt_i2c_write(i2c_dev, pmsg, (i + 1) == num);
	}

	return (ret < 0) ? ret : i;
}

static irqreturn_t wmt_i2c_isr(int irq, void *data)
{
	struct wmt_i2c_dev *i2c_dev = data;

	/* save the status and write-clear it */
	i2c_dev->cmd_status = readw(i2c_dev->base + REG_ISR);
	writew(i2c_dev->cmd_status, i2c_dev->base + REG_ISR);

	complete(&i2c_dev->complete);

	return IRQ_HANDLED;
}

int wmt_i2c_init(struct platform_device *pdev, struct wmt_i2c_dev **pi2c_dev)
{
	int err;
	struct wmt_i2c_dev *i2c_dev;
	struct device_node *np = pdev->dev.of_node;

	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->base = devm_platform_get_and_ioremap_resource(pdev, 0, NULL);
	if (IS_ERR(i2c_dev->base))
		return PTR_ERR(i2c_dev->base);

	i2c_dev->irq = irq_of_parse_and_map(np, 0);
	if (!i2c_dev->irq)
		return -EINVAL;

	err = devm_request_irq(&pdev->dev, i2c_dev->irq, wmt_i2c_isr,
			       0, pdev->name, i2c_dev);
	if (err)
		return dev_err_probe(&pdev->dev, err,
				"failed to request irq %i\n", i2c_dev->irq);

	i2c_dev->dev = &pdev->dev;
	init_completion(&i2c_dev->complete);
	platform_set_drvdata(pdev, i2c_dev);

	*pi2c_dev = i2c_dev;
	return 0;
}
