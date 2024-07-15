// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2014 Broadcom Corporation

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define N_DATA_REGS					8

/*
 * PER_I2C/BSC count register mask depends on 1 byte/4 byte data register
 * size. Cable modem and DSL SoCs with Peripheral i2c cores use 1 byte per
 * data register whereas STB SoCs use 4 byte per data register transfer,
 * account for this difference in total count per transaction and mask to
 * use.
 */
#define BSC_CNT_REG1_MASK(nb)	(nb == 1 ? GENMASK(3, 0) : GENMASK(5, 0))
#define BSC_CNT_REG1_SHIFT	0

/* BSC CTL register field definitions */
#define BSC_CTL_REG_DTF_MASK				0x00000003
#define BSC_CTL_REG_SCL_SEL_MASK			0x00000030
#define BSC_CTL_REG_SCL_SEL_SHIFT			4
#define BSC_CTL_REG_INT_EN_MASK				0x00000040
#define BSC_CTL_REG_INT_EN_SHIFT			6
#define BSC_CTL_REG_DIV_CLK_MASK			0x00000080

/* BSC_IIC_ENABLE r/w enable and interrupt field definitions */
#define BSC_IIC_EN_RESTART_MASK				0x00000040
#define BSC_IIC_EN_NOSTART_MASK				0x00000020
#define BSC_IIC_EN_NOSTOP_MASK				0x00000010
#define BSC_IIC_EN_NOACK_MASK				0x00000004
#define BSC_IIC_EN_INTRP_MASK				0x00000002
#define BSC_IIC_EN_ENABLE_MASK				0x00000001

/* BSC_CTLHI control register field definitions */
#define BSC_CTLHI_REG_INPUT_SWITCHING_LEVEL_MASK	0x00000080
#define BSC_CTLHI_REG_DATAREG_SIZE_MASK			0x00000040
#define BSC_CTLHI_REG_IGNORE_ACK_MASK			0x00000002
#define BSC_CTLHI_REG_WAIT_DIS_MASK			0x00000001

#define I2C_TIMEOUT					100 /* msecs */

/* Condition mask used for non combined transfer */
#define COND_RESTART		BSC_IIC_EN_RESTART_MASK
#define COND_NOSTART		BSC_IIC_EN_NOSTART_MASK
#define COND_NOSTOP		BSC_IIC_EN_NOSTOP_MASK
#define COND_START_STOP		(COND_RESTART | COND_NOSTART | COND_NOSTOP)

/* BSC data transfer direction */
#define DTF_WR_MASK		0x00000000
#define DTF_RD_MASK		0x00000001
/* BSC data transfer direction combined format */
#define DTF_RD_WR_MASK		0x00000002
#define DTF_WR_RD_MASK		0x00000003

#define INT_ENABLE		true
#define INT_DISABLE		false

/* BSC block register map structure to cache fields to be written */
struct bsc_regs {
	u32	chip_address;           /* slave address */
	u32	data_in[N_DATA_REGS];   /* tx data buffer*/
	u32	cnt_reg;		/* rx/tx data length */
	u32	ctl_reg;		/* control register */
	u32	iic_enable;		/* xfer enable and status */
	u32	data_out[N_DATA_REGS];  /* rx data buffer */
	u32	ctlhi_reg;		/* more control fields */
	u32	scl_param;		/* reserved */
};

struct bsc_clk_param {
	u32 hz;
	u32 scl_mask;
	u32 div_mask;
};

enum bsc_xfer_cmd {
	CMD_WR,
	CMD_RD,
	CMD_WR_NOACK,
	CMD_RD_NOACK,
};

static char const *cmd_string[] = {
	[CMD_WR] = "WR",
	[CMD_RD] = "RD",
	[CMD_WR_NOACK] = "WR NOACK",
	[CMD_RD_NOACK] = "RD NOACK",
};

enum bus_speeds {
	SPD_375K,
	SPD_390K,
	SPD_187K,
	SPD_200K,
	SPD_93K,
	SPD_97K,
	SPD_46K,
	SPD_50K
};

static const struct bsc_clk_param bsc_clk[] = {
	[SPD_375K] = {
		.hz = 375000,
		.scl_mask = SPD_375K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = 0
	},
	[SPD_390K] = {
		.hz = 390000,
		.scl_mask = SPD_390K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = 0
	},
	[SPD_187K] = {
		.hz = 187500,
		.scl_mask = SPD_187K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = 0
	},
	[SPD_200K] = {
		.hz = 200000,
		.scl_mask = SPD_200K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = 0
	},
	[SPD_93K]  = {
		.hz = 93750,
		.scl_mask = SPD_375K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = BSC_CTL_REG_DIV_CLK_MASK
	},
	[SPD_97K]  = {
		.hz = 97500,
		.scl_mask = SPD_390K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = BSC_CTL_REG_DIV_CLK_MASK
	},
	[SPD_46K]  = {
		.hz = 46875,
		.scl_mask = SPD_187K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = BSC_CTL_REG_DIV_CLK_MASK
	},
	[SPD_50K]  = {
		.hz = 50000,
		.scl_mask = SPD_200K << BSC_CTL_REG_SCL_SEL_SHIFT,
		.div_mask = BSC_CTL_REG_DIV_CLK_MASK
	}
};

struct brcmstb_i2c_dev {
	struct device *device;
	void __iomem *base;
	int irq;
	struct bsc_regs *bsc_regmap;
	struct i2c_adapter adapter;
	struct completion done;
	u32 clk_freq_hz;
	int data_regsz;
	bool atomic;
};

/* register accessors for both be and le cpu arch */
#ifdef CONFIG_CPU_BIG_ENDIAN
#define __bsc_readl(_reg) ioread32be(_reg)
#define __bsc_writel(_val, _reg) iowrite32be(_val, _reg)
#else
#define __bsc_readl(_reg) ioread32(_reg)
#define __bsc_writel(_val, _reg) iowrite32(_val, _reg)
#endif

#define bsc_readl(_dev, _reg)						\
	__bsc_readl(_dev->base + offsetof(struct bsc_regs, _reg))

#define bsc_writel(_dev, _val, _reg)					\
	__bsc_writel(_val, _dev->base + offsetof(struct bsc_regs, _reg))

static inline int brcmstb_i2c_get_xfersz(struct brcmstb_i2c_dev *dev)
{
	return (N_DATA_REGS * dev->data_regsz);
}

static inline int brcmstb_i2c_get_data_regsz(struct brcmstb_i2c_dev *dev)
{
	return dev->data_regsz;
}

static void brcmstb_i2c_enable_disable_irq(struct brcmstb_i2c_dev *dev,
					   bool int_en)
{

	if (int_en)
		/* Enable BSC  CTL interrupt line */
		dev->bsc_regmap->ctl_reg |= BSC_CTL_REG_INT_EN_MASK;
	else
		/* Disable BSC CTL interrupt line */
		dev->bsc_regmap->ctl_reg &= ~BSC_CTL_REG_INT_EN_MASK;

	barrier();
	bsc_writel(dev, dev->bsc_regmap->ctl_reg, ctl_reg);
}

static irqreturn_t brcmstb_i2c_isr(int irq, void *devid)
{
	struct brcmstb_i2c_dev *dev = devid;
	u32 status_bsc_ctl = bsc_readl(dev, ctl_reg);
	u32 status_iic_intrp = bsc_readl(dev, iic_enable);

	dev_dbg(dev->device, "isr CTL_REG %x IIC_EN %x\n",
		status_bsc_ctl, status_iic_intrp);

	if (!(status_bsc_ctl & BSC_CTL_REG_INT_EN_MASK))
		return IRQ_NONE;

	brcmstb_i2c_enable_disable_irq(dev, INT_DISABLE);
	complete(&dev->done);

	dev_dbg(dev->device, "isr handled");
	return IRQ_HANDLED;
}

/* Wait for device to be ready */
static int brcmstb_i2c_wait_if_busy(struct brcmstb_i2c_dev *dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(I2C_TIMEOUT);

	while ((bsc_readl(dev, iic_enable) & BSC_IIC_EN_INTRP_MASK)) {
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		cpu_relax();
	}
	return 0;
}

/* i2c xfer completion function, handles both irq and polling mode */
static int brcmstb_i2c_wait_for_completion(struct brcmstb_i2c_dev *dev)
{
	int ret = 0;
	unsigned long timeout = msecs_to_jiffies(I2C_TIMEOUT);

	if (dev->irq >= 0 && !dev->atomic) {
		if (!wait_for_completion_timeout(&dev->done, timeout))
			ret = -ETIMEDOUT;
	} else {
		/* we are in polling mode */
		u32 bsc_intrp;
		unsigned long time_left = jiffies + timeout;

		do {
			bsc_intrp = bsc_readl(dev, iic_enable) &
				BSC_IIC_EN_INTRP_MASK;
			if (time_after(jiffies, time_left)) {
				ret = -ETIMEDOUT;
				break;
			}
			cpu_relax();
		} while (!bsc_intrp);
	}

	if (dev->irq < 0 || ret == -ETIMEDOUT)
		brcmstb_i2c_enable_disable_irq(dev, INT_DISABLE);

	return ret;
}

/* Set xfer START/STOP conditions for subsequent transfer */
static void brcmstb_set_i2c_start_stop(struct brcmstb_i2c_dev *dev,
				       u32 cond_flag)
{
	u32 regval = dev->bsc_regmap->iic_enable;

	dev->bsc_regmap->iic_enable = (regval & ~COND_START_STOP) | cond_flag;
}

/* Send I2C request check completion */
static int brcmstb_send_i2c_cmd(struct brcmstb_i2c_dev *dev,
				enum bsc_xfer_cmd cmd)
{
	int rc = 0;
	struct bsc_regs *pi2creg = dev->bsc_regmap;

	/* Make sure the hardware is ready */
	rc = brcmstb_i2c_wait_if_busy(dev);
	if (rc < 0)
		return rc;

	/* only if we are in interrupt mode */
	if (dev->irq >= 0 && !dev->atomic)
		reinit_completion(&dev->done);

	/* enable BSC CTL interrupt line */
	brcmstb_i2c_enable_disable_irq(dev, INT_ENABLE);

	/* initiate transfer by setting iic_enable */
	pi2creg->iic_enable |= BSC_IIC_EN_ENABLE_MASK;
	bsc_writel(dev, pi2creg->iic_enable, iic_enable);

	/* Wait for transaction to finish or timeout */
	rc = brcmstb_i2c_wait_for_completion(dev);
	if (rc) {
		dev_dbg(dev->device, "intr timeout for cmd %s\n",
			cmd_string[cmd]);
		goto cmd_out;
	}

	if ((cmd == CMD_RD || cmd == CMD_WR) &&
	    bsc_readl(dev, iic_enable) & BSC_IIC_EN_NOACK_MASK) {
		rc = -EREMOTEIO;
		dev_dbg(dev->device, "controller received NOACK intr for %s\n",
			cmd_string[cmd]);
	}

cmd_out:
	bsc_writel(dev, 0, cnt_reg);
	bsc_writel(dev, 0, iic_enable);

	return rc;
}

/* Actual data transfer through the BSC master */
static int brcmstb_i2c_xfer_bsc_data(struct brcmstb_i2c_dev *dev,
				     u8 *buf, unsigned int len,
				     struct i2c_msg *pmsg)
{
	int cnt, byte, i, rc;
	enum bsc_xfer_cmd cmd;
	u32 ctl_reg;
	struct bsc_regs *pi2creg = dev->bsc_regmap;
	int no_ack = pmsg->flags & I2C_M_IGNORE_NAK;
	int data_regsz = brcmstb_i2c_get_data_regsz(dev);

	/* see if the transaction needs to check NACK conditions */
	if (no_ack) {
		cmd = (pmsg->flags & I2C_M_RD) ? CMD_RD_NOACK
			: CMD_WR_NOACK;
		pi2creg->ctlhi_reg |= BSC_CTLHI_REG_IGNORE_ACK_MASK;
	} else {
		cmd = (pmsg->flags & I2C_M_RD) ? CMD_RD : CMD_WR;
		pi2creg->ctlhi_reg &= ~BSC_CTLHI_REG_IGNORE_ACK_MASK;
	}
	bsc_writel(dev, pi2creg->ctlhi_reg, ctlhi_reg);

	/* set data transfer direction */
	ctl_reg = pi2creg->ctl_reg & ~BSC_CTL_REG_DTF_MASK;
	if (cmd == CMD_WR || cmd == CMD_WR_NOACK)
		pi2creg->ctl_reg = ctl_reg | DTF_WR_MASK;
	else
		pi2creg->ctl_reg = ctl_reg | DTF_RD_MASK;

	/* set the read/write length */
	bsc_writel(dev, BSC_CNT_REG1_MASK(data_regsz) &
		   (len << BSC_CNT_REG1_SHIFT), cnt_reg);

	/* Write data into data_in register */

	if (cmd == CMD_WR || cmd == CMD_WR_NOACK) {
		for (cnt = 0, i = 0; cnt < len; cnt += data_regsz, i++) {
			u32 word = 0;

			for (byte = 0; byte < data_regsz; byte++) {
				word >>= BITS_PER_BYTE;
				if ((cnt + byte) < len)
					word |= buf[cnt + byte] <<
					(BITS_PER_BYTE * (data_regsz - 1));
			}
			bsc_writel(dev, word, data_in[i]);
		}
	}

	/* Initiate xfer, the function will return on completion */
	rc = brcmstb_send_i2c_cmd(dev, cmd);

	if (rc != 0) {
		dev_dbg(dev->device, "%s failure", cmd_string[cmd]);
		return rc;
	}

	/* Read data from data_out register */
	if (cmd == CMD_RD || cmd == CMD_RD_NOACK) {
		for (cnt = 0, i = 0; cnt < len; cnt += data_regsz, i++) {
			u32 data = bsc_readl(dev, data_out[i]);

			for (byte = 0; byte < data_regsz &&
				     (byte + cnt) < len; byte++) {
				buf[cnt + byte] = data & 0xff;
				data >>= BITS_PER_BYTE;
			}
		}
	}

	return 0;
}

/* Write a single byte of data to the i2c bus */
static int brcmstb_i2c_write_data_byte(struct brcmstb_i2c_dev *dev,
				       u8 *buf, unsigned int nak_expected)
{
	enum bsc_xfer_cmd cmd = nak_expected ? CMD_WR : CMD_WR_NOACK;

	bsc_writel(dev, 1, cnt_reg);
	bsc_writel(dev, *buf, data_in);

	return brcmstb_send_i2c_cmd(dev, cmd);
}

/* Send i2c address */
static int brcmstb_i2c_do_addr(struct brcmstb_i2c_dev *dev,
			       struct i2c_msg *msg)
{
	unsigned char addr;

	if (msg->flags & I2C_M_TEN) {
		/* First byte is 11110XX0 where XX is upper 2 bits */
		addr = 0xF0 | ((msg->addr & 0x300) >> 7);
		bsc_writel(dev, addr, chip_address);

		/* Second byte is the remaining 8 bits */
		addr = msg->addr & 0xFF;
		if (brcmstb_i2c_write_data_byte(dev, &addr, 0) < 0)
			return -EREMOTEIO;

		if (msg->flags & I2C_M_RD) {
			/* For read, send restart without stop condition */
			brcmstb_set_i2c_start_stop(dev, COND_RESTART
						   | COND_NOSTOP);
			/* Then re-send the first byte with the read bit set */
			addr = 0xF0 | ((msg->addr & 0x300) >> 7) | 0x01;
			if (brcmstb_i2c_write_data_byte(dev, &addr, 0) < 0)
				return -EREMOTEIO;

		}
	} else {
		addr = i2c_8bit_addr_from_msg(msg);

		bsc_writel(dev, addr, chip_address);
	}

	return 0;
}

/* Master transfer function */
static int brcmstb_i2c_xfer(struct i2c_adapter *adapter,
			    struct i2c_msg msgs[], int num)
{
	struct brcmstb_i2c_dev *dev = i2c_get_adapdata(adapter);
	struct i2c_msg *pmsg;
	int rc = 0;
	int i;
	int bytes_to_xfer;
	u8 *tmp_buf;
	int len = 0;
	int xfersz = brcmstb_i2c_get_xfersz(dev);
	u32 cond, cond_per_msg;

	/* Loop through all messages */
	for (i = 0; i < num; i++) {
		pmsg = &msgs[i];
		len = pmsg->len;
		tmp_buf = pmsg->buf;

		dev_dbg(dev->device,
			"msg# %d/%d flg %x buf %x len %d\n", i,
			num - 1, pmsg->flags,
			pmsg->buf ? pmsg->buf[0] : '0', pmsg->len);

		if (i < (num - 1) && (msgs[i + 1].flags & I2C_M_NOSTART))
			cond = ~COND_START_STOP;
		else
			cond = COND_RESTART | COND_NOSTOP;

		brcmstb_set_i2c_start_stop(dev, cond);

		/* Send slave address */
		if (!(pmsg->flags & I2C_M_NOSTART)) {
			rc = brcmstb_i2c_do_addr(dev, pmsg);
			if (rc < 0) {
				dev_dbg(dev->device,
					"NACK for addr %2.2x msg#%d rc = %d\n",
					pmsg->addr, i, rc);
				goto out;
			}
		}

		cond_per_msg = cond;

		/* Perform data transfer */
		while (len) {
			bytes_to_xfer = min(len, xfersz);

			if (len <= xfersz) {
				if (i == (num - 1))
					cond_per_msg = cond_per_msg &
						~(COND_RESTART | COND_NOSTOP);
				else
					cond_per_msg = cond;
			} else {
				cond_per_msg = (cond_per_msg & ~COND_RESTART) |
					COND_NOSTOP;
			}

			brcmstb_set_i2c_start_stop(dev, cond_per_msg);

			rc = brcmstb_i2c_xfer_bsc_data(dev, tmp_buf,
						       bytes_to_xfer, pmsg);
			if (rc < 0)
				goto out;

			len -=  bytes_to_xfer;
			tmp_buf += bytes_to_xfer;

			cond_per_msg = COND_NOSTART | COND_NOSTOP;
		}
	}

	rc = num;
out:
	return rc;

}

static int brcmstb_i2c_xfer_atomic(struct i2c_adapter *adapter,
				   struct i2c_msg msgs[], int num)
{
	struct brcmstb_i2c_dev *dev = i2c_get_adapdata(adapter);
	int ret;

	if (dev->irq >= 0)
		disable_irq(dev->irq);
	dev->atomic = true;
	ret = brcmstb_i2c_xfer(adapter, msgs, num);
	dev->atomic = false;
	if (dev->irq >= 0)
		enable_irq(dev->irq);

	return ret;
}

static u32 brcmstb_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_10BIT_ADDR
		| I2C_FUNC_NOSTART | I2C_FUNC_PROTOCOL_MANGLING;
}

static const struct i2c_algorithm brcmstb_i2c_algo = {
	.master_xfer = brcmstb_i2c_xfer,
	.master_xfer_atomic = brcmstb_i2c_xfer_atomic,
	.functionality = brcmstb_i2c_functionality,
};

static void brcmstb_i2c_set_bus_speed(struct brcmstb_i2c_dev *dev)
{
	int i = 0, num_speeds = ARRAY_SIZE(bsc_clk);
	u32 clk_freq_hz = dev->clk_freq_hz;

	for (i = 0; i < num_speeds; i++) {
		if (bsc_clk[i].hz == clk_freq_hz) {
			dev->bsc_regmap->ctl_reg &= ~(BSC_CTL_REG_SCL_SEL_MASK
						| BSC_CTL_REG_DIV_CLK_MASK);
			dev->bsc_regmap->ctl_reg |= (bsc_clk[i].scl_mask |
						     bsc_clk[i].div_mask);
			bsc_writel(dev, dev->bsc_regmap->ctl_reg, ctl_reg);
			break;
		}
	}

	/* in case we did not get find a valid speed */
	if (i == num_speeds) {
		i = (bsc_readl(dev, ctl_reg) & BSC_CTL_REG_SCL_SEL_MASK) >>
			BSC_CTL_REG_SCL_SEL_SHIFT;
		dev_warn(dev->device, "leaving current clock-frequency @ %dHz\n",
			bsc_clk[i].hz);
	}
}

static void brcmstb_i2c_set_bsc_reg_defaults(struct brcmstb_i2c_dev *dev)
{
	if (brcmstb_i2c_get_data_regsz(dev) == sizeof(u32))
		/* set 4 byte data in/out xfers  */
		dev->bsc_regmap->ctlhi_reg = BSC_CTLHI_REG_DATAREG_SIZE_MASK;
	else
		dev->bsc_regmap->ctlhi_reg &= ~BSC_CTLHI_REG_DATAREG_SIZE_MASK;

	bsc_writel(dev, dev->bsc_regmap->ctlhi_reg, ctlhi_reg);
	/* set bus speed */
	brcmstb_i2c_set_bus_speed(dev);
}

#define AUTOI2C_CTRL0		0x26c
#define AUTOI2C_CTRL0_RELEASE_BSC	BIT(1)

static int bcm2711_release_bsc(struct brcmstb_i2c_dev *dev)
{
	struct platform_device *pdev = to_platform_device(dev->device);
	void __iomem *autoi2c;

	/* Map hardware registers */
	autoi2c = devm_platform_ioremap_resource_byname(pdev, "auto-i2c");
	if (IS_ERR(autoi2c))
		return PTR_ERR(autoi2c);

	writel(AUTOI2C_CTRL0_RELEASE_BSC, autoi2c + AUTOI2C_CTRL0);
	devm_iounmap(&pdev->dev, autoi2c);

	/* We need to reset the controller after the release */
	dev->bsc_regmap->iic_enable = 0;
	bsc_writel(dev, dev->bsc_regmap->iic_enable, iic_enable);

	return 0;
}

static int brcmstb_i2c_probe(struct platform_device *pdev)
{
	struct brcmstb_i2c_dev *dev;
	struct i2c_adapter *adap;
	const char *int_name;
	int rc;

	/* Allocate memory for private data structure */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->bsc_regmap = devm_kzalloc(&pdev->dev, sizeof(*dev->bsc_regmap), GFP_KERNEL);
	if (!dev->bsc_regmap)
		return -ENOMEM;

	platform_set_drvdata(pdev, dev);
	dev->device = &pdev->dev;
	init_completion(&dev->done);

	/* Map hardware registers */
	dev->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->base))
		return PTR_ERR(dev->base);

	if (of_device_is_compatible(dev->device->of_node,
				    "brcm,bcm2711-hdmi-i2c")) {
		rc = bcm2711_release_bsc(dev);
		if (rc)
			return rc;
	}

	rc = of_property_read_string(dev->device->of_node, "interrupt-names",
				     &int_name);
	if (rc < 0)
		int_name = NULL;

	/* Get the interrupt number */
	dev->irq = platform_get_irq_optional(pdev, 0);

	/* disable the bsc interrupt line */
	brcmstb_i2c_enable_disable_irq(dev, INT_DISABLE);

	/* register the ISR handler */
	if (dev->irq >= 0) {
		rc = devm_request_irq(&pdev->dev, dev->irq, brcmstb_i2c_isr,
				      IRQF_SHARED,
				      int_name ? int_name : pdev->name,
				      dev);

		if (rc) {
			dev_dbg(dev->device, "falling back to polling mode");
			dev->irq = -1;
		}
	}

	if (of_property_read_u32(dev->device->of_node,
				 "clock-frequency", &dev->clk_freq_hz)) {
		dev_warn(dev->device, "setting clock-frequency@%dHz\n",
			 bsc_clk[0].hz);
		dev->clk_freq_hz = bsc_clk[0].hz;
	}

	/* set the data in/out register size for compatible SoCs */
	if (of_device_is_compatible(dev->device->of_node,
				    "brcm,brcmper-i2c"))
		dev->data_regsz = sizeof(u8);
	else
		dev->data_regsz = sizeof(u32);

	brcmstb_i2c_set_bsc_reg_defaults(dev);

	/* Add the i2c adapter */
	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	strscpy(adap->name, dev_name(&pdev->dev), sizeof(adap->name));
	adap->algo = &brcmstb_i2c_algo;
	adap->dev.parent = &pdev->dev;
	adap->dev.of_node = pdev->dev.of_node;
	rc = i2c_add_adapter(adap);
	if (rc)
		return rc;

	dev_info(dev->device, "%s@%dhz registered in %s mode\n",
		 int_name ? int_name : " ", dev->clk_freq_hz,
		 (dev->irq >= 0) ? "interrupt" : "polling");

	return 0;
}

static void brcmstb_i2c_remove(struct platform_device *pdev)
{
	struct brcmstb_i2c_dev *dev = platform_get_drvdata(pdev);

	i2c_del_adapter(&dev->adapter);
}

static int brcmstb_i2c_suspend(struct device *dev)
{
	struct brcmstb_i2c_dev *i2c_dev = dev_get_drvdata(dev);

	i2c_mark_adapter_suspended(&i2c_dev->adapter);
	return 0;
}

static int brcmstb_i2c_resume(struct device *dev)
{
	struct brcmstb_i2c_dev *i2c_dev = dev_get_drvdata(dev);

	brcmstb_i2c_set_bsc_reg_defaults(i2c_dev);
	i2c_mark_adapter_resumed(&i2c_dev->adapter);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(brcmstb_i2c_pm, brcmstb_i2c_suspend,
				brcmstb_i2c_resume);

static const struct of_device_id brcmstb_i2c_of_match[] = {
	{.compatible = "brcm,brcmstb-i2c"},
	{.compatible = "brcm,brcmper-i2c"},
	{.compatible = "brcm,bcm2711-hdmi-i2c"},
	{},
};
MODULE_DEVICE_TABLE(of, brcmstb_i2c_of_match);

static struct platform_driver brcmstb_i2c_driver = {
	.driver = {
		   .name = "brcmstb-i2c",
		   .of_match_table = brcmstb_i2c_of_match,
		   .pm = pm_sleep_ptr(&brcmstb_i2c_pm),
		   },
	.probe = brcmstb_i2c_probe,
	.remove_new = brcmstb_i2c_remove,
};
module_platform_driver(brcmstb_i2c_driver);

MODULE_AUTHOR("Kamal Dasu <kdasu@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Settop I2C Driver");
MODULE_LICENSE("GPL v2");
