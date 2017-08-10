/*
 * Copyright (C) 2014 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define CFG_OFFSET                   0x00
#define CFG_RESET_SHIFT              31
#define CFG_EN_SHIFT                 30
#define CFG_M_RETRY_CNT_SHIFT        16
#define CFG_M_RETRY_CNT_MASK         0x0f

#define TIM_CFG_OFFSET               0x04
#define TIM_CFG_MODE_400_SHIFT       31

#define M_FIFO_CTRL_OFFSET           0x0c
#define M_FIFO_RX_FLUSH_SHIFT        31
#define M_FIFO_TX_FLUSH_SHIFT        30
#define M_FIFO_RX_CNT_SHIFT          16
#define M_FIFO_RX_CNT_MASK           0x7f
#define M_FIFO_RX_THLD_SHIFT         8
#define M_FIFO_RX_THLD_MASK          0x3f

#define M_CMD_OFFSET                 0x30
#define M_CMD_START_BUSY_SHIFT       31
#define M_CMD_STATUS_SHIFT           25
#define M_CMD_STATUS_MASK            0x07
#define M_CMD_STATUS_SUCCESS         0x0
#define M_CMD_STATUS_LOST_ARB        0x1
#define M_CMD_STATUS_NACK_ADDR       0x2
#define M_CMD_STATUS_NACK_DATA       0x3
#define M_CMD_STATUS_TIMEOUT         0x4
#define M_CMD_PROTOCOL_SHIFT         9
#define M_CMD_PROTOCOL_MASK          0xf
#define M_CMD_PROTOCOL_BLK_WR        0x7
#define M_CMD_PROTOCOL_BLK_RD        0x8
#define M_CMD_PEC_SHIFT              8
#define M_CMD_RD_CNT_SHIFT           0
#define M_CMD_RD_CNT_MASK            0xff

#define IE_OFFSET                    0x38
#define IE_M_RX_FIFO_FULL_SHIFT      31
#define IE_M_RX_THLD_SHIFT           30
#define IE_M_START_BUSY_SHIFT        28
#define IE_M_TX_UNDERRUN_SHIFT       27

#define IS_OFFSET                    0x3c
#define IS_M_RX_FIFO_FULL_SHIFT      31
#define IS_M_RX_THLD_SHIFT           30
#define IS_M_START_BUSY_SHIFT        28
#define IS_M_TX_UNDERRUN_SHIFT       27

#define M_TX_OFFSET                  0x40
#define M_TX_WR_STATUS_SHIFT         31
#define M_TX_DATA_SHIFT              0
#define M_TX_DATA_MASK               0xff

#define M_RX_OFFSET                  0x44
#define M_RX_STATUS_SHIFT            30
#define M_RX_STATUS_MASK             0x03
#define M_RX_PEC_ERR_SHIFT           29
#define M_RX_DATA_SHIFT              0
#define M_RX_DATA_MASK               0xff

#define I2C_TIMEOUT_MSEC             50000
#define M_TX_RX_FIFO_SIZE            64

enum bus_speed_index {
	I2C_SPD_100K = 0,
	I2C_SPD_400K,
};

struct bcm_iproc_i2c_dev {
	struct device *device;
	int irq;

	void __iomem *base;

	struct i2c_adapter adapter;
	unsigned int bus_speed;

	struct completion done;
	int xfer_is_done;

	struct i2c_msg *msg;

	/* bytes that have been transferred */
	unsigned int tx_bytes;
};

/*
 * Can be expanded in the future if more interrupt status bits are utilized
 */
#define ISR_MASK (BIT(IS_M_START_BUSY_SHIFT) | BIT(IS_M_TX_UNDERRUN_SHIFT))

static irqreturn_t bcm_iproc_i2c_isr(int irq, void *data)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = data;
	u32 status = readl(iproc_i2c->base + IS_OFFSET);

	status &= ISR_MASK;

	if (!status)
		return IRQ_NONE;

	/* TX FIFO is empty and we have more data to send */
	if (status & BIT(IS_M_TX_UNDERRUN_SHIFT)) {
		struct i2c_msg *msg = iproc_i2c->msg;
		unsigned int tx_bytes = msg->len - iproc_i2c->tx_bytes;
		unsigned int i;
		u32 val;

		/* can only fill up to the FIFO size */
		tx_bytes = min_t(unsigned int, tx_bytes, M_TX_RX_FIFO_SIZE);
		for (i = 0; i < tx_bytes; i++) {
			/* start from where we left over */
			unsigned int idx = iproc_i2c->tx_bytes + i;

			val = msg->buf[idx];

			/* mark the last byte */
			if (idx == msg->len - 1) {
				u32 tmp;

				val |= BIT(M_TX_WR_STATUS_SHIFT);

				/*
				 * Since this is the last byte, we should
				 * now disable TX FIFO underrun interrupt
				 */
				tmp = readl(iproc_i2c->base + IE_OFFSET);
				tmp &= ~BIT(IE_M_TX_UNDERRUN_SHIFT);
				writel(tmp, iproc_i2c->base + IE_OFFSET);
			}

			/* load data into TX FIFO */
			writel(val, iproc_i2c->base + M_TX_OFFSET);
		}
		/* update number of transferred bytes */
		iproc_i2c->tx_bytes += tx_bytes;
	}

	if (status & BIT(IS_M_START_BUSY_SHIFT)) {
		iproc_i2c->xfer_is_done = 1;
		complete(&iproc_i2c->done);
	}

	writel(status, iproc_i2c->base + IS_OFFSET);

	return IRQ_HANDLED;
}

static int bcm_iproc_i2c_init(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	u32 val;

	/* put controller in reset */
	val = readl(iproc_i2c->base + CFG_OFFSET);
	val |= 1 << CFG_RESET_SHIFT;
	val &= ~(1 << CFG_EN_SHIFT);
	writel(val, iproc_i2c->base + CFG_OFFSET);

	/* wait 100 usec per spec */
	udelay(100);

	/* bring controller out of reset */
	val &= ~(1 << CFG_RESET_SHIFT);
	writel(val, iproc_i2c->base + CFG_OFFSET);

	/* flush TX/RX FIFOs and set RX FIFO threshold to zero */
	val = (1 << M_FIFO_RX_FLUSH_SHIFT) | (1 << M_FIFO_TX_FLUSH_SHIFT);
	writel(val, iproc_i2c->base + M_FIFO_CTRL_OFFSET);
	/* disable all interrupts */
	writel(0, iproc_i2c->base + IE_OFFSET);

	/* clear all pending interrupts */
	writel(0xffffffff, iproc_i2c->base + IS_OFFSET);

	return 0;
}

static void bcm_iproc_i2c_enable_disable(struct bcm_iproc_i2c_dev *iproc_i2c,
					 bool enable)
{
	u32 val;

	val = readl(iproc_i2c->base + CFG_OFFSET);
	if (enable)
		val |= BIT(CFG_EN_SHIFT);
	else
		val &= ~BIT(CFG_EN_SHIFT);
	writel(val, iproc_i2c->base + CFG_OFFSET);
}

static int bcm_iproc_i2c_check_status(struct bcm_iproc_i2c_dev *iproc_i2c,
				      struct i2c_msg *msg)
{
	u32 val;

	val = readl(iproc_i2c->base + M_CMD_OFFSET);
	val = (val >> M_CMD_STATUS_SHIFT) & M_CMD_STATUS_MASK;

	switch (val) {
	case M_CMD_STATUS_SUCCESS:
		return 0;

	case M_CMD_STATUS_LOST_ARB:
		dev_dbg(iproc_i2c->device, "lost bus arbitration\n");
		return -EAGAIN;

	case M_CMD_STATUS_NACK_ADDR:
		dev_dbg(iproc_i2c->device, "NAK addr:0x%02x\n", msg->addr);
		return -ENXIO;

	case M_CMD_STATUS_NACK_DATA:
		dev_dbg(iproc_i2c->device, "NAK data\n");
		return -ENXIO;

	case M_CMD_STATUS_TIMEOUT:
		dev_dbg(iproc_i2c->device, "bus timeout\n");
		return -ETIMEDOUT;

	default:
		dev_dbg(iproc_i2c->device, "unknown error code=%d\n", val);

		/* re-initialize i2c for recovery */
		bcm_iproc_i2c_enable_disable(iproc_i2c, false);
		bcm_iproc_i2c_init(iproc_i2c);
		bcm_iproc_i2c_enable_disable(iproc_i2c, true);

		return -EIO;
	}
}

static int bcm_iproc_i2c_xfer_single_msg(struct bcm_iproc_i2c_dev *iproc_i2c,
					 struct i2c_msg *msg)
{
	int ret, i;
	u8 addr;
	u32 val;
	unsigned int tx_bytes;
	unsigned long time_left = msecs_to_jiffies(I2C_TIMEOUT_MSEC);

	/* check if bus is busy */
	if (!!(readl(iproc_i2c->base + M_CMD_OFFSET) &
	       BIT(M_CMD_START_BUSY_SHIFT))) {
		dev_warn(iproc_i2c->device, "bus is busy\n");
		return -EBUSY;
	}

	iproc_i2c->msg = msg;

	/* format and load slave address into the TX FIFO */
	addr = i2c_8bit_addr_from_msg(msg);
	writel(addr, iproc_i2c->base + M_TX_OFFSET);

	/*
	 * For a write transaction, load data into the TX FIFO. Only allow
	 * loading up to TX FIFO size - 1 bytes of data since the first byte
	 * has been used up by the slave address
	 */
	tx_bytes = min_t(unsigned int, msg->len, M_TX_RX_FIFO_SIZE - 1);
	if (!(msg->flags & I2C_M_RD)) {
		for (i = 0; i < tx_bytes; i++) {
			val = msg->buf[i];

			/* mark the last byte */
			if (i == msg->len - 1)
				val |= 1 << M_TX_WR_STATUS_SHIFT;

			writel(val, iproc_i2c->base + M_TX_OFFSET);
		}
		iproc_i2c->tx_bytes = tx_bytes;
	}

	/* mark as incomplete before starting the transaction */
	reinit_completion(&iproc_i2c->done);
	iproc_i2c->xfer_is_done = 0;

	/*
	 * Enable the "start busy" interrupt, which will be triggered after the
	 * transaction is done, i.e., the internal start_busy bit, transitions
	 * from 1 to 0.
	 */
	val = BIT(IE_M_START_BUSY_SHIFT);

	/*
	 * If TX data size is larger than the TX FIFO, need to enable TX
	 * underrun interrupt, which will be triggerred when the TX FIFO is
	 * empty. When that happens we can then pump more data into the FIFO
	 */
	if (!(msg->flags & I2C_M_RD) &&
	    msg->len > iproc_i2c->tx_bytes)
		val |= BIT(IE_M_TX_UNDERRUN_SHIFT);

	writel(val, iproc_i2c->base + IE_OFFSET);

	/*
	 * Now we can activate the transfer. For a read operation, specify the
	 * number of bytes to read
	 */
	val = BIT(M_CMD_START_BUSY_SHIFT);
	if (msg->flags & I2C_M_RD) {
		val |= (M_CMD_PROTOCOL_BLK_RD << M_CMD_PROTOCOL_SHIFT) |
		       (msg->len << M_CMD_RD_CNT_SHIFT);
	} else {
		val |= (M_CMD_PROTOCOL_BLK_WR << M_CMD_PROTOCOL_SHIFT);
	}
	writel(val, iproc_i2c->base + M_CMD_OFFSET);

	time_left = wait_for_completion_timeout(&iproc_i2c->done, time_left);

	/* disable all interrupts */
	writel(0, iproc_i2c->base + IE_OFFSET);
	/* read it back to flush the write */
	readl(iproc_i2c->base + IE_OFFSET);

	/* make sure the interrupt handler isn't running */
	synchronize_irq(iproc_i2c->irq);

	if (!time_left && !iproc_i2c->xfer_is_done) {
		dev_err(iproc_i2c->device, "transaction timed out\n");

		/* flush FIFOs */
		val = (1 << M_FIFO_RX_FLUSH_SHIFT) |
		      (1 << M_FIFO_TX_FLUSH_SHIFT);
		writel(val, iproc_i2c->base + M_FIFO_CTRL_OFFSET);
		return -ETIMEDOUT;
	}

	ret = bcm_iproc_i2c_check_status(iproc_i2c, msg);
	if (ret) {
		/* flush both TX/RX FIFOs */
		val = (1 << M_FIFO_RX_FLUSH_SHIFT) |
		      (1 << M_FIFO_TX_FLUSH_SHIFT);
		writel(val, iproc_i2c->base + M_FIFO_CTRL_OFFSET);
		return ret;
	}

	/*
	 * For a read operation, we now need to load the data from FIFO
	 * into the memory buffer
	 */
	if (msg->flags & I2C_M_RD) {
		for (i = 0; i < msg->len; i++) {
			msg->buf[i] = (readl(iproc_i2c->base + M_RX_OFFSET) >>
				      M_RX_DATA_SHIFT) & M_RX_DATA_MASK;
		}
	}

	return 0;
}

static int bcm_iproc_i2c_xfer(struct i2c_adapter *adapter,
			      struct i2c_msg msgs[], int num)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = i2c_get_adapdata(adapter);
	int ret, i;

	/* go through all messages */
	for (i = 0; i < num; i++) {
		ret = bcm_iproc_i2c_xfer_single_msg(iproc_i2c, &msgs[i]);
		if (ret) {
			dev_dbg(iproc_i2c->device, "xfer failed\n");
			return ret;
		}
	}

	return num;
}

static uint32_t bcm_iproc_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm bcm_iproc_algo = {
	.master_xfer = bcm_iproc_i2c_xfer,
	.functionality = bcm_iproc_i2c_functionality,
};

static const struct i2c_adapter_quirks bcm_iproc_i2c_quirks = {
	/* need to reserve one byte in the FIFO for the slave address */
	.max_read_len = M_TX_RX_FIFO_SIZE - 1,
};

static int bcm_iproc_i2c_cfg_speed(struct bcm_iproc_i2c_dev *iproc_i2c)
{
	unsigned int bus_speed;
	u32 val;
	int ret = of_property_read_u32(iproc_i2c->device->of_node,
				       "clock-frequency", &bus_speed);
	if (ret < 0) {
		dev_info(iproc_i2c->device,
			"unable to interpret clock-frequency DT property\n");
		bus_speed = 100000;
	}

	if (bus_speed < 100000) {
		dev_err(iproc_i2c->device, "%d Hz bus speed not supported\n",
			bus_speed);
		dev_err(iproc_i2c->device,
			"valid speeds are 100khz and 400khz\n");
		return -EINVAL;
	} else if (bus_speed < 400000) {
		bus_speed = 100000;
	} else {
		bus_speed = 400000;
	}

	iproc_i2c->bus_speed = bus_speed;
	val = readl(iproc_i2c->base + TIM_CFG_OFFSET);
	val &= ~(1 << TIM_CFG_MODE_400_SHIFT);
	val |= (bus_speed == 400000) << TIM_CFG_MODE_400_SHIFT;
	writel(val, iproc_i2c->base + TIM_CFG_OFFSET);

	dev_info(iproc_i2c->device, "bus set to %u Hz\n", bus_speed);

	return 0;
}

static int bcm_iproc_i2c_probe(struct platform_device *pdev)
{
	int irq, ret = 0;
	struct bcm_iproc_i2c_dev *iproc_i2c;
	struct i2c_adapter *adap;
	struct resource *res;

	iproc_i2c = devm_kzalloc(&pdev->dev, sizeof(*iproc_i2c),
				 GFP_KERNEL);
	if (!iproc_i2c)
		return -ENOMEM;

	platform_set_drvdata(pdev, iproc_i2c);
	iproc_i2c->device = &pdev->dev;
	init_completion(&iproc_i2c->done);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	iproc_i2c->base = devm_ioremap_resource(iproc_i2c->device, res);
	if (IS_ERR(iproc_i2c->base))
		return PTR_ERR(iproc_i2c->base);

	ret = bcm_iproc_i2c_init(iproc_i2c);
	if (ret)
		return ret;

	ret = bcm_iproc_i2c_cfg_speed(iproc_i2c);
	if (ret)
		return ret;

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(iproc_i2c->device, "no irq resource\n");
		return irq;
	}
	iproc_i2c->irq = irq;

	ret = devm_request_irq(iproc_i2c->device, irq, bcm_iproc_i2c_isr, 0,
			       pdev->name, iproc_i2c);
	if (ret < 0) {
		dev_err(iproc_i2c->device, "unable to request irq %i\n", irq);
		return ret;
	}

	bcm_iproc_i2c_enable_disable(iproc_i2c, true);

	adap = &iproc_i2c->adapter;
	i2c_set_adapdata(adap, iproc_i2c);
	strlcpy(adap->name, "Broadcom iProc I2C adapter", sizeof(adap->name));
	adap->algo = &bcm_iproc_algo;
	adap->quirks = &bcm_iproc_i2c_quirks;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;

	return i2c_add_adapter(adap);
}

static int bcm_iproc_i2c_remove(struct platform_device *pdev)
{
	struct bcm_iproc_i2c_dev *iproc_i2c = platform_get_drvdata(pdev);

	/* make sure there's no pending interrupt when we remove the adapter */
	writel(0, iproc_i2c->base + IE_OFFSET);
	readl(iproc_i2c->base + IE_OFFSET);
	synchronize_irq(iproc_i2c->irq);

	i2c_del_adapter(&iproc_i2c->adapter);
	bcm_iproc_i2c_enable_disable(iproc_i2c, false);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int bcm_iproc_i2c_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bcm_iproc_i2c_dev *iproc_i2c = platform_get_drvdata(pdev);

	/* make sure there's no pending interrupt when we go into suspend */
	writel(0, iproc_i2c->base + IE_OFFSET);
	readl(iproc_i2c->base + IE_OFFSET);
	synchronize_irq(iproc_i2c->irq);

	/* now disable the controller */
	bcm_iproc_i2c_enable_disable(iproc_i2c, false);

	return 0;
}

static int bcm_iproc_i2c_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct bcm_iproc_i2c_dev *iproc_i2c = platform_get_drvdata(pdev);
	int ret;
	u32 val;

	/*
	 * Power domain could have been shut off completely in system deep
	 * sleep, so re-initialize the block here
	 */
	ret = bcm_iproc_i2c_init(iproc_i2c);
	if (ret)
		return ret;

	/* configure to the desired bus speed */
	val = readl(iproc_i2c->base + TIM_CFG_OFFSET);
	val &= ~(1 << TIM_CFG_MODE_400_SHIFT);
	val |= (iproc_i2c->bus_speed == 400000) << TIM_CFG_MODE_400_SHIFT;
	writel(val, iproc_i2c->base + TIM_CFG_OFFSET);

	bcm_iproc_i2c_enable_disable(iproc_i2c, true);

	return 0;
}

static const struct dev_pm_ops bcm_iproc_i2c_pm_ops = {
	.suspend_late = &bcm_iproc_i2c_suspend,
	.resume_early = &bcm_iproc_i2c_resume
};

#define BCM_IPROC_I2C_PM_OPS (&bcm_iproc_i2c_pm_ops)
#else
#define BCM_IPROC_I2C_PM_OPS NULL
#endif /* CONFIG_PM_SLEEP */

static const struct of_device_id bcm_iproc_i2c_of_match[] = {
	{ .compatible = "brcm,iproc-i2c" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, bcm_iproc_i2c_of_match);

static struct platform_driver bcm_iproc_i2c_driver = {
	.driver = {
		.name = "bcm-iproc-i2c",
		.of_match_table = bcm_iproc_i2c_of_match,
		.pm = BCM_IPROC_I2C_PM_OPS,
	},
	.probe = bcm_iproc_i2c_probe,
	.remove = bcm_iproc_i2c_remove,
};
module_platform_driver(bcm_iproc_i2c_driver);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom iProc I2C Driver");
MODULE_LICENSE("GPL v2");
