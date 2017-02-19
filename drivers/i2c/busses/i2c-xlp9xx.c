/*
 * Copyright (c) 2003-2015 Broadcom Corporation
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#define XLP9XX_I2C_DIV			0x0
#define XLP9XX_I2C_CTRL			0x1
#define XLP9XX_I2C_CMD			0x2
#define XLP9XX_I2C_STATUS		0x3
#define XLP9XX_I2C_MTXFIFO		0x4
#define XLP9XX_I2C_MRXFIFO		0x5
#define XLP9XX_I2C_MFIFOCTRL		0x6
#define XLP9XX_I2C_STXFIFO		0x7
#define XLP9XX_I2C_SRXFIFO		0x8
#define XLP9XX_I2C_SFIFOCTRL		0x9
#define XLP9XX_I2C_SLAVEADDR		0xA
#define XLP9XX_I2C_OWNADDR		0xB
#define XLP9XX_I2C_FIFOWCNT		0xC
#define XLP9XX_I2C_INTEN		0xD
#define XLP9XX_I2C_INTST		0xE
#define XLP9XX_I2C_WAITCNT		0xF
#define XLP9XX_I2C_TIMEOUT		0X10
#define XLP9XX_I2C_GENCALLADDR		0x11

#define XLP9XX_I2C_CMD_START		BIT(7)
#define XLP9XX_I2C_CMD_STOP		BIT(6)
#define XLP9XX_I2C_CMD_READ		BIT(5)
#define XLP9XX_I2C_CMD_WRITE		BIT(4)
#define XLP9XX_I2C_CMD_ACK		BIT(3)

#define XLP9XX_I2C_CTRL_MCTLEN_SHIFT	16
#define XLP9XX_I2C_CTRL_MCTLEN_MASK	0xffff0000
#define XLP9XX_I2C_CTRL_RST		BIT(8)
#define XLP9XX_I2C_CTRL_EN		BIT(6)
#define XLP9XX_I2C_CTRL_MASTER		BIT(4)
#define XLP9XX_I2C_CTRL_FIFORD		BIT(1)
#define XLP9XX_I2C_CTRL_ADDMODE		BIT(0)

#define XLP9XX_I2C_INTEN_NACKADDR	BIT(25)
#define XLP9XX_I2C_INTEN_SADDR		BIT(13)
#define XLP9XX_I2C_INTEN_DATADONE	BIT(12)
#define XLP9XX_I2C_INTEN_ARLOST		BIT(11)
#define XLP9XX_I2C_INTEN_MFIFOFULL	BIT(4)
#define XLP9XX_I2C_INTEN_MFIFOEMTY	BIT(3)
#define XLP9XX_I2C_INTEN_MFIFOHI	BIT(2)
#define XLP9XX_I2C_INTEN_BUSERR		BIT(0)

#define XLP9XX_I2C_MFIFOCTRL_HITH_SHIFT		8
#define XLP9XX_I2C_MFIFOCTRL_LOTH_SHIFT		0
#define XLP9XX_I2C_MFIFOCTRL_RST		BIT(16)

#define XLP9XX_I2C_SLAVEADDR_RW			BIT(0)
#define XLP9XX_I2C_SLAVEADDR_ADDR_SHIFT		1

#define XLP9XX_I2C_IP_CLK_FREQ		133000000UL
#define XLP9XX_I2C_DEFAULT_FREQ		100000
#define XLP9XX_I2C_HIGH_FREQ		400000
#define XLP9XX_I2C_FIFO_SIZE		0x80U
#define XLP9XX_I2C_TIMEOUT_MS		1000

#define XLP9XX_I2C_FIFO_WCNT_MASK	0xff
#define XLP9XX_I2C_STATUS_ERRMASK	(XLP9XX_I2C_INTEN_ARLOST | \
			XLP9XX_I2C_INTEN_NACKADDR | XLP9XX_I2C_INTEN_BUSERR)

struct xlp9xx_i2c_dev {
	struct device *dev;
	struct i2c_adapter adapter;
	struct completion msg_complete;
	int irq;
	bool msg_read;
	u32 __iomem *base;
	u32 msg_buf_remaining;
	u32 msg_len;
	u32 clk_hz;
	u32 msg_err;
	u8 *msg_buf;
};

static inline void xlp9xx_write_i2c_reg(struct xlp9xx_i2c_dev *priv,
					unsigned long reg, u32 val)
{
	writel(val, priv->base + reg);
}

static inline u32 xlp9xx_read_i2c_reg(struct xlp9xx_i2c_dev *priv,
				      unsigned long reg)
{
	return readl(priv->base + reg);
}

static void xlp9xx_i2c_mask_irq(struct xlp9xx_i2c_dev *priv, u32 mask)
{
	u32 inten;

	inten = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_INTEN) & ~mask;
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTEN, inten);
}

static void xlp9xx_i2c_unmask_irq(struct xlp9xx_i2c_dev *priv, u32 mask)
{
	u32 inten;

	inten = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_INTEN) | mask;
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTEN, inten);
}

static void xlp9xx_i2c_update_rx_fifo_thres(struct xlp9xx_i2c_dev *priv)
{
	u32 thres;

	thres = min(priv->msg_buf_remaining, XLP9XX_I2C_FIFO_SIZE);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_MFIFOCTRL,
			     thres << XLP9XX_I2C_MFIFOCTRL_HITH_SHIFT);
}

static void xlp9xx_i2c_fill_tx_fifo(struct xlp9xx_i2c_dev *priv)
{
	u32 len, i;
	u8 *buf = priv->msg_buf;

	len = min(priv->msg_buf_remaining, XLP9XX_I2C_FIFO_SIZE);
	for (i = 0; i < len; i++)
		xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_MTXFIFO, buf[i]);
	priv->msg_buf_remaining -= len;
	priv->msg_buf += len;
}

static void xlp9xx_i2c_drain_rx_fifo(struct xlp9xx_i2c_dev *priv)
{
	u32 len, i;
	u8 *buf = priv->msg_buf;

	len = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_FIFOWCNT) &
				  XLP9XX_I2C_FIFO_WCNT_MASK;
	len = min(priv->msg_buf_remaining, len);
	for (i = 0; i < len; i++, buf++)
		*buf = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_MRXFIFO);

	priv->msg_buf_remaining -= len;
	priv->msg_buf = buf;

	if (priv->msg_buf_remaining)
		xlp9xx_i2c_update_rx_fifo_thres(priv);
}

static irqreturn_t xlp9xx_i2c_isr(int irq, void *dev_id)
{
	struct xlp9xx_i2c_dev *priv = dev_id;
	u32 status;

	status = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_INTST);
	if (status == 0)
		return IRQ_NONE;

	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTST, status);
	if (status & XLP9XX_I2C_STATUS_ERRMASK) {
		priv->msg_err = status;
		goto xfer_done;
	}

	/* SADDR ACK for SMBUS_QUICK */
	if ((status & XLP9XX_I2C_INTEN_SADDR) && (priv->msg_len == 0))
		goto xfer_done;

	if (!priv->msg_read) {
		if (status & XLP9XX_I2C_INTEN_MFIFOEMTY) {
			/* TX FIFO got empty, fill it up again */
			if (priv->msg_buf_remaining)
				xlp9xx_i2c_fill_tx_fifo(priv);
			else
				xlp9xx_i2c_mask_irq(priv,
						    XLP9XX_I2C_INTEN_MFIFOEMTY);
		}
	} else {
		if (status & (XLP9XX_I2C_INTEN_DATADONE |
			      XLP9XX_I2C_INTEN_MFIFOHI)) {
			/* data is in FIFO, read it */
			if (priv->msg_buf_remaining)
				xlp9xx_i2c_drain_rx_fifo(priv);
		}
	}

	/* Transfer complete */
	if (status & XLP9XX_I2C_INTEN_DATADONE)
		goto xfer_done;

	return IRQ_HANDLED;

xfer_done:
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTEN, 0);
	complete(&priv->msg_complete);
	return IRQ_HANDLED;
}

static int xlp9xx_i2c_init(struct xlp9xx_i2c_dev *priv)
{
	u32 prescale;

	/*
	 * The controller uses 5 * SCL clock internally.
	 * So prescale value should be divided by 5.
	 */
	prescale = DIV_ROUND_UP(XLP9XX_I2C_IP_CLK_FREQ, priv->clk_hz);
	prescale = ((prescale - 8) / 5) - 1;
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_CTRL, XLP9XX_I2C_CTRL_RST);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_CTRL, XLP9XX_I2C_CTRL_EN |
			     XLP9XX_I2C_CTRL_MASTER);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_DIV, prescale);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTEN, 0);

	return 0;
}

static int xlp9xx_i2c_xfer_msg(struct xlp9xx_i2c_dev *priv, struct i2c_msg *msg,
			       int last_msg)
{
	unsigned long timeleft;
	u32 intr_mask, cmd, val;

	priv->msg_buf = msg->buf;
	priv->msg_buf_remaining = priv->msg_len = msg->len;
	priv->msg_err = 0;
	priv->msg_read = (msg->flags & I2C_M_RD);
	reinit_completion(&priv->msg_complete);

	/* Reset FIFO */
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_MFIFOCTRL,
			     XLP9XX_I2C_MFIFOCTRL_RST);

	/* set FIFO threshold if reading */
	if (priv->msg_read)
		xlp9xx_i2c_update_rx_fifo_thres(priv);

	/* set slave addr */
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_SLAVEADDR,
			     (msg->addr << XLP9XX_I2C_SLAVEADDR_ADDR_SHIFT) |
			     (priv->msg_read ? XLP9XX_I2C_SLAVEADDR_RW : 0));

	/* Build control word for transfer */
	val = xlp9xx_read_i2c_reg(priv, XLP9XX_I2C_CTRL);
	if (!priv->msg_read)
		val &= ~XLP9XX_I2C_CTRL_FIFORD;
	else
		val |= XLP9XX_I2C_CTRL_FIFORD;	/* read */

	if (msg->flags & I2C_M_TEN)
		val |= XLP9XX_I2C_CTRL_ADDMODE;	/* 10-bit address mode*/
	else
		val &= ~XLP9XX_I2C_CTRL_ADDMODE;

	/* set data length to be transferred */
	val = (val & ~XLP9XX_I2C_CTRL_MCTLEN_MASK) |
	      (msg->len << XLP9XX_I2C_CTRL_MCTLEN_SHIFT);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_CTRL, val);

	/* fill fifo during tx */
	if (!priv->msg_read)
		xlp9xx_i2c_fill_tx_fifo(priv);

	/* set interrupt mask */
	intr_mask = (XLP9XX_I2C_INTEN_ARLOST | XLP9XX_I2C_INTEN_BUSERR |
		     XLP9XX_I2C_INTEN_NACKADDR | XLP9XX_I2C_INTEN_DATADONE);

	if (priv->msg_read) {
		intr_mask |= XLP9XX_I2C_INTEN_MFIFOHI;
		if (msg->len == 0)
			intr_mask |= XLP9XX_I2C_INTEN_SADDR;
	} else {
		if (msg->len == 0)
			intr_mask |= XLP9XX_I2C_INTEN_SADDR;
		else
			intr_mask |= XLP9XX_I2C_INTEN_MFIFOEMTY;
	}
	xlp9xx_i2c_unmask_irq(priv, intr_mask);

	/* set cmd reg */
	cmd = XLP9XX_I2C_CMD_START;
	cmd |= (priv->msg_read ? XLP9XX_I2C_CMD_READ : XLP9XX_I2C_CMD_WRITE);
	if (last_msg)
		cmd |= XLP9XX_I2C_CMD_STOP;

	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_CMD, cmd);

	timeleft = msecs_to_jiffies(XLP9XX_I2C_TIMEOUT_MS);
	timeleft = wait_for_completion_timeout(&priv->msg_complete, timeleft);

	if (priv->msg_err) {
		dev_dbg(priv->dev, "transfer error %x!\n", priv->msg_err);
		if (priv->msg_err & XLP9XX_I2C_INTEN_BUSERR)
			xlp9xx_i2c_init(priv);
		return -EIO;
	}

	if (timeleft == 0) {
		dev_dbg(priv->dev, "i2c transfer timed out!\n");
		xlp9xx_i2c_init(priv);
		return -ETIMEDOUT;
	}

	return 0;
}

static int xlp9xx_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			   int num)
{
	int i, ret;
	struct xlp9xx_i2c_dev *priv = i2c_get_adapdata(adap);

	for (i = 0; i < num; i++) {
		ret = xlp9xx_i2c_xfer_msg(priv, &msgs[i], i == num - 1);
		if (ret != 0)
			return ret;
	}

	return num;
}

static u32 xlp9xx_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C |
		I2C_FUNC_10BIT_ADDR;
}

static struct i2c_algorithm xlp9xx_i2c_algo = {
	.master_xfer = xlp9xx_i2c_xfer,
	.functionality = xlp9xx_i2c_functionality,
};

static int xlp9xx_i2c_get_frequency(struct platform_device *pdev,
				    struct xlp9xx_i2c_dev *priv)
{
	u32 freq;
	int err;

	err = device_property_read_u32(&pdev->dev, "clock-frequency", &freq);
	if (err) {
		freq = XLP9XX_I2C_DEFAULT_FREQ;
		dev_dbg(&pdev->dev, "using default frequency %u\n", freq);
	} else if (freq == 0 || freq > XLP9XX_I2C_HIGH_FREQ) {
		dev_warn(&pdev->dev, "invalid frequency %u, using default\n",
			 freq);
		freq = XLP9XX_I2C_DEFAULT_FREQ;
	}
	priv->clk_hz = freq;

	return 0;
}

static int xlp9xx_i2c_probe(struct platform_device *pdev)
{
	struct xlp9xx_i2c_dev *priv;
	struct resource *res;
	int err = 0;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq <= 0) {
		dev_err(&pdev->dev, "invalid irq!\n");
		return priv->irq;
	}

	xlp9xx_i2c_get_frequency(pdev, priv);
	xlp9xx_i2c_init(priv);

	err = devm_request_irq(&pdev->dev, priv->irq, xlp9xx_i2c_isr, 0,
			       pdev->name, priv);
	if (err) {
		dev_err(&pdev->dev, "IRQ request failed!\n");
		return err;
	}

	init_completion(&priv->msg_complete);
	priv->adapter.dev.parent = &pdev->dev;
	priv->adapter.algo = &xlp9xx_i2c_algo;
	ACPI_COMPANION_SET(&priv->adapter.dev, ACPI_COMPANION(&pdev->dev));
	priv->adapter.dev.of_node = pdev->dev.of_node;
	priv->dev = &pdev->dev;

	snprintf(priv->adapter.name, sizeof(priv->adapter.name), "xlp9xx-i2c");
	i2c_set_adapdata(&priv->adapter, priv);

	err = i2c_add_adapter(&priv->adapter);
	if (err)
		return err;

	platform_set_drvdata(pdev, priv);
	dev_dbg(&pdev->dev, "I2C bus:%d added\n", priv->adapter.nr);

	return 0;
}

static int xlp9xx_i2c_remove(struct platform_device *pdev)
{
	struct xlp9xx_i2c_dev *priv;

	priv = platform_get_drvdata(pdev);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_INTEN, 0);
	synchronize_irq(priv->irq);
	i2c_del_adapter(&priv->adapter);
	xlp9xx_write_i2c_reg(priv, XLP9XX_I2C_CTRL, 0);

	return 0;
}

static const struct of_device_id xlp9xx_i2c_of_match[] = {
	{ .compatible = "netlogic,xlp980-i2c", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, xlp9xx_i2c_of_match);

#ifdef CONFIG_ACPI
static const struct acpi_device_id xlp9xx_i2c_acpi_ids[] = {
	{"BRCM9007", 0},
	{}
};
MODULE_DEVICE_TABLE(acpi, xlp9xx_i2c_acpi_ids);
#endif

static struct platform_driver xlp9xx_i2c_driver = {
	.probe = xlp9xx_i2c_probe,
	.remove = xlp9xx_i2c_remove,
	.driver = {
		.name = "xlp9xx-i2c",
		.of_match_table = xlp9xx_i2c_of_match,
		.acpi_match_table = ACPI_PTR(xlp9xx_i2c_acpi_ids),
	},
};

module_platform_driver(xlp9xx_i2c_driver);

MODULE_AUTHOR("Subhendu Sekhar Behera <sbehera@broadcom.com>");
MODULE_DESCRIPTION("XLP9XX/5XX I2C Bus Controller Driver");
MODULE_LICENSE("GPL v2");
