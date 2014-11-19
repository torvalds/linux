/*
 * I2C bus driver for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>

#define SIRFSOC_I2C_CLK_CTRL		0x00
#define SIRFSOC_I2C_STATUS		0x0C
#define SIRFSOC_I2C_CTRL		0x10
#define SIRFSOC_I2C_IO_CTRL		0x14
#define SIRFSOC_I2C_SDA_DELAY		0x18
#define SIRFSOC_I2C_CMD_START		0x1C
#define SIRFSOC_I2C_CMD_BUF		0x30
#define SIRFSOC_I2C_DATA_BUF		0x80

#define SIRFSOC_I2C_CMD_BUF_MAX		16
#define SIRFSOC_I2C_DATA_BUF_MAX	16

#define SIRFSOC_I2C_CMD(x)		(SIRFSOC_I2C_CMD_BUF + (x)*0x04)
#define SIRFSOC_I2C_DATA_MASK(x)        (0xFF<<(((x)&3)*8))
#define SIRFSOC_I2C_DATA_SHIFT(x)       (((x)&3)*8)

#define SIRFSOC_I2C_DIV_MASK		(0xFFFF)

/* I2C status flags */
#define SIRFSOC_I2C_STAT_BUSY		BIT(0)
#define SIRFSOC_I2C_STAT_TIP		BIT(1)
#define SIRFSOC_I2C_STAT_NACK		BIT(2)
#define SIRFSOC_I2C_STAT_TR_INT		BIT(4)
#define SIRFSOC_I2C_STAT_STOP		BIT(6)
#define SIRFSOC_I2C_STAT_CMD_DONE	BIT(8)
#define SIRFSOC_I2C_STAT_ERR		BIT(9)
#define SIRFSOC_I2C_CMD_INDEX		(0x1F<<16)

/* I2C control flags */
#define SIRFSOC_I2C_RESET		BIT(0)
#define SIRFSOC_I2C_CORE_EN		BIT(1)
#define SIRFSOC_I2C_MASTER_MODE		BIT(2)
#define SIRFSOC_I2C_CMD_DONE_EN		BIT(11)
#define SIRFSOC_I2C_ERR_INT_EN		BIT(12)

#define SIRFSOC_I2C_SDA_DELAY_MASK	(0xFF)
#define SIRFSOC_I2C_SCLF_FILTER		(3<<8)

#define SIRFSOC_I2C_START_CMD		BIT(0)

#define SIRFSOC_I2C_CMD_RP(x)		((x)&0x7)
#define SIRFSOC_I2C_NACK		BIT(3)
#define SIRFSOC_I2C_WRITE		BIT(4)
#define SIRFSOC_I2C_READ		BIT(5)
#define SIRFSOC_I2C_STOP		BIT(6)
#define SIRFSOC_I2C_START		BIT(7)

#define SIRFSOC_I2C_DEFAULT_SPEED 100000
#define SIRFSOC_I2C_ERR_NOACK      1
#define SIRFSOC_I2C_ERR_TIMEOUT    2

struct sirfsoc_i2c {
	void __iomem *base;
	struct clk *clk;
	u32 cmd_ptr;		/* Current position in CMD buffer */
	u8 *buf;		/* Buffer passed by user */
	u32 msg_len;		/* Message length */
	u32 finished_len;	/* number of bytes read/written */
	u32 read_cmd_len;	/* number of read cmd sent */
	int msg_read;		/* 1 indicates a read message */
	int err_status;		/* 1 indicates an error on bus */

	u32 sda_delay;		/* For suspend/resume */
	u32 clk_div;
	int last;		/* Last message in transfer, STOP cmd can be sent */

	struct completion done;	/* indicates completion of message transfer */
	struct i2c_adapter adapter;
};

static void i2c_sirfsoc_read_data(struct sirfsoc_i2c *siic)
{
	u32 data = 0;
	int i;

	for (i = 0; i < siic->read_cmd_len; i++) {
		if (!(i & 0x3))
			data = readl(siic->base + SIRFSOC_I2C_DATA_BUF + i);
		siic->buf[siic->finished_len++] =
			(u8)((data & SIRFSOC_I2C_DATA_MASK(i)) >>
				SIRFSOC_I2C_DATA_SHIFT(i));
	}
}

static void i2c_sirfsoc_queue_cmd(struct sirfsoc_i2c *siic)
{
	u32 regval;
	int i = 0;

	if (siic->msg_read) {
		while (((siic->finished_len + i) < siic->msg_len)
				&& (siic->cmd_ptr < SIRFSOC_I2C_CMD_BUF_MAX)) {
			regval = SIRFSOC_I2C_READ | SIRFSOC_I2C_CMD_RP(0);
			if (((siic->finished_len + i) ==
					(siic->msg_len - 1)) && siic->last)
				regval |= SIRFSOC_I2C_STOP | SIRFSOC_I2C_NACK;
			writel(regval,
				siic->base + SIRFSOC_I2C_CMD(siic->cmd_ptr++));
			i++;
		}

		siic->read_cmd_len = i;
	} else {
		while ((siic->cmd_ptr < SIRFSOC_I2C_CMD_BUF_MAX - 1)
				&& (siic->finished_len < siic->msg_len)) {
			regval = SIRFSOC_I2C_WRITE | SIRFSOC_I2C_CMD_RP(0);
			if ((siic->finished_len == (siic->msg_len - 1))
				&& siic->last)
				regval |= SIRFSOC_I2C_STOP;
			writel(regval,
				siic->base + SIRFSOC_I2C_CMD(siic->cmd_ptr++));
			writel(siic->buf[siic->finished_len++],
				siic->base + SIRFSOC_I2C_CMD(siic->cmd_ptr++));
		}
	}
	siic->cmd_ptr = 0;

	/* Trigger the transfer */
	writel(SIRFSOC_I2C_START_CMD, siic->base + SIRFSOC_I2C_CMD_START);
}

static irqreturn_t i2c_sirfsoc_irq(int irq, void *dev_id)
{
	struct sirfsoc_i2c *siic = (struct sirfsoc_i2c *)dev_id;
	u32 i2c_stat = readl(siic->base + SIRFSOC_I2C_STATUS);

	if (i2c_stat & SIRFSOC_I2C_STAT_ERR) {
		/* Error conditions */
		siic->err_status = SIRFSOC_I2C_ERR_NOACK;
		writel(SIRFSOC_I2C_STAT_ERR, siic->base + SIRFSOC_I2C_STATUS);

		if (i2c_stat & SIRFSOC_I2C_STAT_NACK)
			dev_dbg(&siic->adapter.dev, "ACK not received\n");
		else
			dev_err(&siic->adapter.dev, "I2C error\n");

		/*
		 * Due to hardware ANOMALY, we need to reset I2C earlier after
		 * we get NOACK while accessing non-existing clients, otherwise
		 * we will get errors even we access existing clients later
		 */
		writel(readl(siic->base + SIRFSOC_I2C_CTRL) | SIRFSOC_I2C_RESET,
				siic->base + SIRFSOC_I2C_CTRL);
		while (readl(siic->base + SIRFSOC_I2C_CTRL) & SIRFSOC_I2C_RESET)
			cpu_relax();

		complete(&siic->done);
	} else if (i2c_stat & SIRFSOC_I2C_STAT_CMD_DONE) {
		/* CMD buffer execution complete */
		if (siic->msg_read)
			i2c_sirfsoc_read_data(siic);
		if (siic->finished_len == siic->msg_len)
			complete(&siic->done);
		else /* Fill a new CMD buffer for left data */
			i2c_sirfsoc_queue_cmd(siic);

		writel(SIRFSOC_I2C_STAT_CMD_DONE, siic->base + SIRFSOC_I2C_STATUS);
	}

	return IRQ_HANDLED;
}

static void i2c_sirfsoc_set_address(struct sirfsoc_i2c *siic,
	struct i2c_msg *msg)
{
	unsigned char addr;
	u32 regval = SIRFSOC_I2C_START | SIRFSOC_I2C_CMD_RP(0) | SIRFSOC_I2C_WRITE;

	/* no data and last message -> add STOP */
	if (siic->last && (msg->len == 0))
		regval |= SIRFSOC_I2C_STOP;

	writel(regval, siic->base + SIRFSOC_I2C_CMD(siic->cmd_ptr++));

	addr = msg->addr << 1;	/* Generate address */
	if (msg->flags & I2C_M_RD)
		addr |= 1;

	/* Reverse direction bit */
	if (msg->flags & I2C_M_REV_DIR_ADDR)
		addr ^= 1;

	writel(addr, siic->base + SIRFSOC_I2C_CMD(siic->cmd_ptr++));
}

static int i2c_sirfsoc_xfer_msg(struct sirfsoc_i2c *siic, struct i2c_msg *msg)
{
	u32 regval = readl(siic->base + SIRFSOC_I2C_CTRL);
	/* timeout waiting for the xfer to finish or fail */
	int timeout = msecs_to_jiffies((msg->len + 1) * 50);

	i2c_sirfsoc_set_address(siic, msg);

	writel(regval | SIRFSOC_I2C_CMD_DONE_EN | SIRFSOC_I2C_ERR_INT_EN,
		siic->base + SIRFSOC_I2C_CTRL);
	i2c_sirfsoc_queue_cmd(siic);

	if (wait_for_completion_timeout(&siic->done, timeout) == 0) {
		siic->err_status = SIRFSOC_I2C_ERR_TIMEOUT;
		dev_err(&siic->adapter.dev, "Transfer timeout\n");
	}

	writel(regval & ~(SIRFSOC_I2C_CMD_DONE_EN | SIRFSOC_I2C_ERR_INT_EN),
		siic->base + SIRFSOC_I2C_CTRL);
	writel(0, siic->base + SIRFSOC_I2C_CMD_START);

	/* i2c control doesn't response, reset it */
	if (siic->err_status == SIRFSOC_I2C_ERR_TIMEOUT) {
		writel(readl(siic->base + SIRFSOC_I2C_CTRL) | SIRFSOC_I2C_RESET,
			siic->base + SIRFSOC_I2C_CTRL);
		while (readl(siic->base + SIRFSOC_I2C_CTRL) & SIRFSOC_I2C_RESET)
			cpu_relax();
	}
	return siic->err_status ? -EAGAIN : 0;
}

static u32 i2c_sirfsoc_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static int i2c_sirfsoc_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
	int num)
{
	struct sirfsoc_i2c *siic = adap->algo_data;
	int i, ret;

	clk_enable(siic->clk);

	for (i = 0; i < num; i++) {
		siic->buf = msgs[i].buf;
		siic->msg_len = msgs[i].len;
		siic->msg_read = !!(msgs[i].flags & I2C_M_RD);
		siic->err_status = 0;
		siic->cmd_ptr = 0;
		siic->finished_len = 0;
		siic->last = (i == (num - 1));

		ret = i2c_sirfsoc_xfer_msg(siic, &msgs[i]);
		if (ret) {
			clk_disable(siic->clk);
			return ret;
		}
	}

	clk_disable(siic->clk);
	return num;
}

/* I2C algorithms associated with this master controller driver */
static const struct i2c_algorithm i2c_sirfsoc_algo = {
	.master_xfer = i2c_sirfsoc_xfer,
	.functionality = i2c_sirfsoc_func,
};

static int i2c_sirfsoc_probe(struct platform_device *pdev)
{
	struct sirfsoc_i2c *siic;
	struct i2c_adapter *adap;
	struct resource *mem_res;
	struct clk *clk;
	int bitrate;
	int ctrl_speed;
	int irq;

	int err;
	u32 regval;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		dev_err(&pdev->dev, "Clock get failed\n");
		goto err_get_clk;
	}

	err = clk_prepare(clk);
	if (err) {
		dev_err(&pdev->dev, "Clock prepare failed\n");
		goto err_clk_prep;
	}

	err = clk_enable(clk);
	if (err) {
		dev_err(&pdev->dev, "Clock enable failed\n");
		goto err_clk_en;
	}

	ctrl_speed = clk_get_rate(clk);

	siic = devm_kzalloc(&pdev->dev, sizeof(*siic), GFP_KERNEL);
	if (!siic) {
		err = -ENOMEM;
		goto out;
	}
	adap = &siic->adapter;
	adap->class = I2C_CLASS_DEPRECATED;

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	siic->base = devm_ioremap_resource(&pdev->dev, mem_res);
	if (IS_ERR(siic->base)) {
		err = PTR_ERR(siic->base);
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = irq;
		goto out;
	}
	err = devm_request_irq(&pdev->dev, irq, i2c_sirfsoc_irq, 0,
		dev_name(&pdev->dev), siic);
	if (err)
		goto out;

	adap->algo = &i2c_sirfsoc_algo;
	adap->algo_data = siic;
	adap->retries = 3;

	adap->dev.of_node = pdev->dev.of_node;
	adap->dev.parent = &pdev->dev;
	adap->nr = pdev->id;

	strlcpy(adap->name, "sirfsoc-i2c", sizeof(adap->name));

	platform_set_drvdata(pdev, adap);
	init_completion(&siic->done);

	/* Controller Initalisation */

	writel(SIRFSOC_I2C_RESET, siic->base + SIRFSOC_I2C_CTRL);
	while (readl(siic->base + SIRFSOC_I2C_CTRL) & SIRFSOC_I2C_RESET)
		cpu_relax();
	writel(SIRFSOC_I2C_CORE_EN | SIRFSOC_I2C_MASTER_MODE,
		siic->base + SIRFSOC_I2C_CTRL);

	siic->clk = clk;

	err = of_property_read_u32(pdev->dev.of_node,
		"clock-frequency", &bitrate);
	if (err < 0)
		bitrate = SIRFSOC_I2C_DEFAULT_SPEED;

	if (bitrate < 100000)
		regval =
			(2 * ctrl_speed) / (bitrate * 11);
	else
		regval = ctrl_speed / (bitrate * 5);

	writel(regval, siic->base + SIRFSOC_I2C_CLK_CTRL);
	if (regval > 0xFF)
		writel(0xFF, siic->base + SIRFSOC_I2C_SDA_DELAY);
	else
		writel(regval, siic->base + SIRFSOC_I2C_SDA_DELAY);

	err = i2c_add_numbered_adapter(adap);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't add new i2c adapter\n");
		goto out;
	}

	clk_disable(clk);

	dev_info(&pdev->dev, " I2C adapter ready to operate\n");

	return 0;

out:
	clk_disable(clk);
err_clk_en:
	clk_unprepare(clk);
err_clk_prep:
	clk_put(clk);
err_get_clk:
	return err;
}

static int i2c_sirfsoc_remove(struct platform_device *pdev)
{
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);
	struct sirfsoc_i2c *siic = adapter->algo_data;

	writel(SIRFSOC_I2C_RESET, siic->base + SIRFSOC_I2C_CTRL);
	i2c_del_adapter(adapter);
	clk_unprepare(siic->clk);
	clk_put(siic->clk);
	return 0;
}

#ifdef CONFIG_PM
static int i2c_sirfsoc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);
	struct sirfsoc_i2c *siic = adapter->algo_data;

	clk_enable(siic->clk);
	siic->sda_delay = readl(siic->base + SIRFSOC_I2C_SDA_DELAY);
	siic->clk_div = readl(siic->base + SIRFSOC_I2C_CLK_CTRL);
	clk_disable(siic->clk);
	return 0;
}

static int i2c_sirfsoc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct i2c_adapter *adapter = platform_get_drvdata(pdev);
	struct sirfsoc_i2c *siic = adapter->algo_data;

	clk_enable(siic->clk);
	writel(SIRFSOC_I2C_RESET, siic->base + SIRFSOC_I2C_CTRL);
	while (readl(siic->base + SIRFSOC_I2C_CTRL) & SIRFSOC_I2C_RESET)
		cpu_relax();
	writel(SIRFSOC_I2C_CORE_EN | SIRFSOC_I2C_MASTER_MODE,
		siic->base + SIRFSOC_I2C_CTRL);
	writel(siic->clk_div, siic->base + SIRFSOC_I2C_CLK_CTRL);
	writel(siic->sda_delay, siic->base + SIRFSOC_I2C_SDA_DELAY);
	clk_disable(siic->clk);
	return 0;
}

static const struct dev_pm_ops i2c_sirfsoc_pm_ops = {
	.suspend = i2c_sirfsoc_suspend,
	.resume = i2c_sirfsoc_resume,
};
#endif

static const struct of_device_id sirfsoc_i2c_of_match[] = {
	{ .compatible = "sirf,prima2-i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, sirfsoc_i2c_of_match);

static struct platform_driver i2c_sirfsoc_driver = {
	.driver = {
		.name = "sirfsoc_i2c",
		.owner = THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &i2c_sirfsoc_pm_ops,
#endif
		.of_match_table = sirfsoc_i2c_of_match,
	},
	.probe = i2c_sirfsoc_probe,
	.remove = i2c_sirfsoc_remove,
};
module_platform_driver(i2c_sirfsoc_driver);

MODULE_DESCRIPTION("SiRF SoC I2C master controller driver");
MODULE_AUTHOR("Zhiwu Song <Zhiwu.Song@csr.com>, "
	"Xiangzhen Ye <Xiangzhen.Ye@csr.com>");
MODULE_LICENSE("GPL v2");
