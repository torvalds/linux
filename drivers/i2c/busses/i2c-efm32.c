// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Uwe Kleine-Koenig for Pengutronix
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>

#define DRIVER_NAME "efm32-i2c"

#define MASK_VAL(mask, val)		((val << __ffs(mask)) & mask)

#define REG_CTRL		0x00
#define REG_CTRL_EN			0x00001
#define REG_CTRL_SLAVE			0x00002
#define REG_CTRL_AUTOACK		0x00004
#define REG_CTRL_AUTOSE			0x00008
#define REG_CTRL_AUTOSN			0x00010
#define REG_CTRL_ARBDIS			0x00020
#define REG_CTRL_GCAMEN			0x00040
#define REG_CTRL_CLHR__MASK		0x00300
#define REG_CTRL_BITO__MASK		0x03000
#define REG_CTRL_BITO_OFF		0x00000
#define REG_CTRL_BITO_40PCC		0x01000
#define REG_CTRL_BITO_80PCC		0x02000
#define REG_CTRL_BITO_160PCC		0x03000
#define REG_CTRL_GIBITO			0x08000
#define REG_CTRL_CLTO__MASK		0x70000
#define REG_CTRL_CLTO_OFF		0x00000

#define REG_CMD			0x04
#define REG_CMD_START			0x00001
#define REG_CMD_STOP			0x00002
#define REG_CMD_ACK			0x00004
#define REG_CMD_NACK			0x00008
#define REG_CMD_CONT			0x00010
#define REG_CMD_ABORT			0x00020
#define REG_CMD_CLEARTX			0x00040
#define REG_CMD_CLEARPC			0x00080

#define REG_STATE		0x08
#define REG_STATE_BUSY			0x00001
#define REG_STATE_MASTER		0x00002
#define REG_STATE_TRANSMITTER		0x00004
#define REG_STATE_NACKED		0x00008
#define REG_STATE_BUSHOLD		0x00010
#define REG_STATE_STATE__MASK		0x000e0
#define REG_STATE_STATE_IDLE		0x00000
#define REG_STATE_STATE_WAIT		0x00020
#define REG_STATE_STATE_START		0x00040
#define REG_STATE_STATE_ADDR		0x00060
#define REG_STATE_STATE_ADDRACK		0x00080
#define REG_STATE_STATE_DATA		0x000a0
#define REG_STATE_STATE_DATAACK		0x000c0

#define REG_STATUS		0x0c
#define REG_STATUS_PSTART		0x00001
#define REG_STATUS_PSTOP		0x00002
#define REG_STATUS_PACK			0x00004
#define REG_STATUS_PNACK		0x00008
#define REG_STATUS_PCONT		0x00010
#define REG_STATUS_PABORT		0x00020
#define REG_STATUS_TXC			0x00040
#define REG_STATUS_TXBL			0x00080
#define REG_STATUS_RXDATAV		0x00100

#define REG_CLKDIV		0x10
#define REG_CLKDIV_DIV__MASK		0x001ff
#define REG_CLKDIV_DIV(div)		MASK_VAL(REG_CLKDIV_DIV__MASK, (div))

#define REG_SADDR		0x14
#define REG_SADDRMASK		0x18
#define REG_RXDATA		0x1c
#define REG_RXDATAP		0x20
#define REG_TXDATA		0x24
#define REG_IF			0x28
#define REG_IF_START			0x00001
#define REG_IF_RSTART			0x00002
#define REG_IF_ADDR			0x00004
#define REG_IF_TXC			0x00008
#define REG_IF_TXBL			0x00010
#define REG_IF_RXDATAV			0x00020
#define REG_IF_ACK			0x00040
#define REG_IF_NACK			0x00080
#define REG_IF_MSTOP			0x00100
#define REG_IF_ARBLOST			0x00200
#define REG_IF_BUSERR			0x00400
#define REG_IF_BUSHOLD			0x00800
#define REG_IF_TXOF			0x01000
#define REG_IF_RXUF			0x02000
#define REG_IF_BITO			0x04000
#define REG_IF_CLTO			0x08000
#define REG_IF_SSTOP			0x10000

#define REG_IFS			0x2c
#define REG_IFC			0x30
#define REG_IFC__MASK			0x1ffcf

#define REG_IEN			0x34

#define REG_ROUTE		0x38
#define REG_ROUTE_SDAPEN		0x00001
#define REG_ROUTE_SCLPEN		0x00002
#define REG_ROUTE_LOCATION__MASK	0x00700
#define REG_ROUTE_LOCATION(n)		MASK_VAL(REG_ROUTE_LOCATION__MASK, (n))

struct efm32_i2c_ddata {
	struct i2c_adapter adapter;

	struct clk *clk;
	void __iomem *base;
	unsigned int irq;
	u8 location;
	unsigned long frequency;

	/* transfer data */
	struct completion done;
	struct i2c_msg *msgs;
	size_t num_msgs;
	size_t current_word, current_msg;
	int retval;
};

static u32 efm32_i2c_read32(struct efm32_i2c_ddata *ddata, unsigned offset)
{
	return readl(ddata->base + offset);
}

static void efm32_i2c_write32(struct efm32_i2c_ddata *ddata,
		unsigned offset, u32 value)
{
	writel(value, ddata->base + offset);
}

static void efm32_i2c_send_next_msg(struct efm32_i2c_ddata *ddata)
{
	struct i2c_msg *cur_msg = &ddata->msgs[ddata->current_msg];

	efm32_i2c_write32(ddata, REG_CMD, REG_CMD_START);
	efm32_i2c_write32(ddata, REG_TXDATA, i2c_8bit_addr_from_msg(cur_msg));
}

static void efm32_i2c_send_next_byte(struct efm32_i2c_ddata *ddata)
{
	struct i2c_msg *cur_msg = &ddata->msgs[ddata->current_msg];

	if (ddata->current_word >= cur_msg->len) {
		/* cur_msg completely transferred */
		ddata->current_word = 0;
		ddata->current_msg += 1;

		if (ddata->current_msg >= ddata->num_msgs) {
			efm32_i2c_write32(ddata, REG_CMD, REG_CMD_STOP);
			complete(&ddata->done);
		} else {
			efm32_i2c_send_next_msg(ddata);
		}
	} else {
		efm32_i2c_write32(ddata, REG_TXDATA,
				cur_msg->buf[ddata->current_word++]);
	}
}

static void efm32_i2c_recv_next_byte(struct efm32_i2c_ddata *ddata)
{
	struct i2c_msg *cur_msg = &ddata->msgs[ddata->current_msg];

	cur_msg->buf[ddata->current_word] = efm32_i2c_read32(ddata, REG_RXDATA);
	ddata->current_word += 1;
	if (ddata->current_word >= cur_msg->len) {
		/* cur_msg completely transferred */
		ddata->current_word = 0;
		ddata->current_msg += 1;

		efm32_i2c_write32(ddata, REG_CMD, REG_CMD_NACK);

		if (ddata->current_msg >= ddata->num_msgs) {
			efm32_i2c_write32(ddata, REG_CMD, REG_CMD_STOP);
			complete(&ddata->done);
		} else {
			efm32_i2c_send_next_msg(ddata);
		}
	} else {
		efm32_i2c_write32(ddata, REG_CMD, REG_CMD_ACK);
	}
}

static irqreturn_t efm32_i2c_irq(int irq, void *dev_id)
{
	struct efm32_i2c_ddata *ddata = dev_id;
	struct i2c_msg *cur_msg = &ddata->msgs[ddata->current_msg];
	u32 irqflag = efm32_i2c_read32(ddata, REG_IF);
	u32 state = efm32_i2c_read32(ddata, REG_STATE);

	efm32_i2c_write32(ddata, REG_IFC, irqflag & REG_IFC__MASK);

	switch (state & REG_STATE_STATE__MASK) {
	case REG_STATE_STATE_IDLE:
		/* arbitration lost? */
		ddata->retval = -EAGAIN;
		complete(&ddata->done);
		break;
	case REG_STATE_STATE_WAIT:
		/*
		 * huh, this shouldn't happen.
		 * Reset hardware state and get out
		 */
		ddata->retval = -EIO;
		efm32_i2c_write32(ddata, REG_CMD,
				REG_CMD_STOP | REG_CMD_ABORT |
				REG_CMD_CLEARTX | REG_CMD_CLEARPC);
		complete(&ddata->done);
		break;
	case REG_STATE_STATE_START:
		/* "caller" is expected to send an address */
		break;
	case REG_STATE_STATE_ADDR:
		/* wait for Ack or NAck of slave */
		break;
	case REG_STATE_STATE_ADDRACK:
		if (state & REG_STATE_NACKED) {
			efm32_i2c_write32(ddata, REG_CMD, REG_CMD_STOP);
			ddata->retval = -ENXIO;
			complete(&ddata->done);
		} else if (cur_msg->flags & I2C_M_RD) {
			/* wait for slave to send first data byte */
		} else {
			efm32_i2c_send_next_byte(ddata);
		}
		break;
	case REG_STATE_STATE_DATA:
		if (cur_msg->flags & I2C_M_RD) {
			efm32_i2c_recv_next_byte(ddata);
		} else {
			/* wait for Ack or Nack of slave */
		}
		break;
	case REG_STATE_STATE_DATAACK:
		if (state & REG_STATE_NACKED) {
			efm32_i2c_write32(ddata, REG_CMD, REG_CMD_STOP);
			complete(&ddata->done);
		} else {
			efm32_i2c_send_next_byte(ddata);
		}
	}

	return IRQ_HANDLED;
}

static int efm32_i2c_master_xfer(struct i2c_adapter *adap,
		struct i2c_msg *msgs, int num)
{
	struct efm32_i2c_ddata *ddata = i2c_get_adapdata(adap);
	int ret;

	if (ddata->msgs)
		return -EBUSY;

	ddata->msgs = msgs;
	ddata->num_msgs = num;
	ddata->current_word = 0;
	ddata->current_msg = 0;
	ddata->retval = -EIO;

	reinit_completion(&ddata->done);

	dev_dbg(&ddata->adapter.dev, "state: %08x, status: %08x\n",
			efm32_i2c_read32(ddata, REG_STATE),
			efm32_i2c_read32(ddata, REG_STATUS));

	efm32_i2c_send_next_msg(ddata);

	wait_for_completion(&ddata->done);

	if (ddata->current_msg >= ddata->num_msgs)
		ret = ddata->num_msgs;
	else
		ret = ddata->retval;

	return ret;
}

static u32 efm32_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm efm32_i2c_algo = {
	.master_xfer = efm32_i2c_master_xfer,
	.functionality = efm32_i2c_functionality,
};

static u32 efm32_i2c_get_configured_location(struct efm32_i2c_ddata *ddata)
{
	u32 reg = efm32_i2c_read32(ddata, REG_ROUTE);

	return (reg & REG_ROUTE_LOCATION__MASK) >>
		__ffs(REG_ROUTE_LOCATION__MASK);
}

static int efm32_i2c_probe(struct platform_device *pdev)
{
	struct efm32_i2c_ddata *ddata;
	struct resource *res;
	unsigned long rate;
	struct device_node *np = pdev->dev.of_node;
	u32 location, frequency;
	int ret;
	u32 clkdiv;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;
	platform_set_drvdata(pdev, ddata);

	init_completion(&ddata->done);
	strlcpy(ddata->adapter.name, pdev->name, sizeof(ddata->adapter.name));
	ddata->adapter.owner = THIS_MODULE;
	ddata->adapter.algo = &efm32_i2c_algo;
	ddata->adapter.dev.parent = &pdev->dev;
	ddata->adapter.dev.of_node = pdev->dev.of_node;
	i2c_set_adapdata(&ddata->adapter, ddata);

	ddata->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ddata->clk)) {
		ret = PTR_ERR(ddata->clk);
		dev_err(&pdev->dev, "failed to get clock: %d\n", ret);
		return ret;
	}

	ddata->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);

	if (resource_size(res) < 0x42) {
		dev_err(&pdev->dev, "memory resource too small\n");
		return -EINVAL;
	}

	ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		if (!ret)
			ret = -EINVAL;
		return ret;
	}

	ddata->irq = ret;

	ret = clk_prepare_enable(ddata->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to enable clock (%d)\n", ret);
		return ret;
	}


	ret = of_property_read_u32(np, "energymicro,location", &location);

	if (ret)
		/* fall back to wrongly namespaced property */
		ret = of_property_read_u32(np, "efm32,location", &location);

	if (!ret) {
		dev_dbg(&pdev->dev, "using location %u\n", location);
	} else {
		/* default to location configured in hardware */
		location = efm32_i2c_get_configured_location(ddata);

		dev_info(&pdev->dev, "fall back to location %u\n", location);
	}

	ddata->location = location;

	ret = of_property_read_u32(np, "clock-frequency", &frequency);
	if (!ret) {
		dev_dbg(&pdev->dev, "using frequency %u\n", frequency);
	} else {
		frequency = I2C_MAX_STANDARD_MODE_FREQ;
		dev_info(&pdev->dev, "defaulting to 100 kHz\n");
	}
	ddata->frequency = frequency;

	rate = clk_get_rate(ddata->clk);
	if (!rate) {
		dev_err(&pdev->dev, "there is no input clock available\n");
		ret = -EINVAL;
		goto err_disable_clk;
	}
	clkdiv = DIV_ROUND_UP(rate, 8 * ddata->frequency) - 1;
	if (clkdiv >= 0x200) {
		dev_err(&pdev->dev,
				"input clock too fast (%lu) to divide down to bus freq (%lu)",
				rate, ddata->frequency);
		ret = -EINVAL;
		goto err_disable_clk;
	}

	dev_dbg(&pdev->dev, "input clock = %lu, bus freq = %lu, clkdiv = %lu\n",
			rate, ddata->frequency, (unsigned long)clkdiv);
	efm32_i2c_write32(ddata, REG_CLKDIV, REG_CLKDIV_DIV(clkdiv));

	efm32_i2c_write32(ddata, REG_ROUTE, REG_ROUTE_SDAPEN |
			REG_ROUTE_SCLPEN |
			REG_ROUTE_LOCATION(ddata->location));

	efm32_i2c_write32(ddata, REG_CTRL, REG_CTRL_EN |
			REG_CTRL_BITO_160PCC | 0 * REG_CTRL_GIBITO);

	efm32_i2c_write32(ddata, REG_IFC, REG_IFC__MASK);
	efm32_i2c_write32(ddata, REG_IEN, REG_IF_TXC | REG_IF_ACK | REG_IF_NACK
			| REG_IF_ARBLOST | REG_IF_BUSERR | REG_IF_RXDATAV);

	/* to make bus idle */
	efm32_i2c_write32(ddata, REG_CMD, REG_CMD_ABORT);

	ret = request_irq(ddata->irq, efm32_i2c_irq, 0, DRIVER_NAME, ddata);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request irq (%d)\n", ret);
		goto err_disable_clk;
	}

	ret = i2c_add_adapter(&ddata->adapter);
	if (ret) {
		free_irq(ddata->irq, ddata);

err_disable_clk:
		clk_disable_unprepare(ddata->clk);
	}
	return ret;
}

static int efm32_i2c_remove(struct platform_device *pdev)
{
	struct efm32_i2c_ddata *ddata = platform_get_drvdata(pdev);

	i2c_del_adapter(&ddata->adapter);
	free_irq(ddata->irq, ddata);
	clk_disable_unprepare(ddata->clk);

	return 0;
}

static const struct of_device_id efm32_i2c_dt_ids[] = {
	{
		.compatible = "energymicro,efm32-i2c",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, efm32_i2c_dt_ids);

static struct platform_driver efm32_i2c_driver = {
	.probe = efm32_i2c_probe,
	.remove = efm32_i2c_remove,

	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = efm32_i2c_dt_ids,
	},
};
module_platform_driver(efm32_i2c_driver);

MODULE_AUTHOR("Uwe Kleine-Koenig <u.kleine-koenig@pengutronix.de>");
MODULE_DESCRIPTION("EFM32 i2c driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
