// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/module.h>
#include <asm/io.h>
#include <uapi/linux/aspeed-otp.h>

#define ASPEED_REVISION_ID0	0x04
#define ASPEED_REVISION_ID1	0x14
#define ID0_AST2600A0	0x05000303
#define ID1_AST2600A0	0x05000303
#define ID0_AST2600A1	0x05010303
#define ID1_AST2600A1	0x05010303
#define ID0_AST2600A2	0x05010303
#define ID1_AST2600A2	0x05020303
#define ID0_AST2600A3	0x05030303
#define ID1_AST2600A3	0x05030303
#define ID0_AST2620A1	0x05010203
#define ID1_AST2620A1	0x05010203
#define ID0_AST2620A2	0x05010203
#define ID1_AST2620A2	0x05020203
#define ID0_AST2620A3	0x05030203
#define ID1_AST2620A3	0x05030203
#define ID0_AST2605A2	0x05010103
#define ID1_AST2605A2	0x05020103
#define ID0_AST2605A3	0x05030103
#define ID1_AST2605A3	0x05030103
#define ID0_AST2625A3	0x05030403
#define ID1_AST2625A3	0x05030403

#define OTP_PROTECT_KEY	0x0
#define  OTP_PASSWD	0x349fe38a
#define OTP_COMMAND	0x4
#define OTP_TIMING	0x8
#define OTP_ADDR	0x10
#define OTP_STATUS	0x14
#define OTP_COMPARE_1	0x20
#define OTP_COMPARE_2	0x24
#define OTP_COMPARE_3	0x28
#define OTP_COMPARE_4	0x2c
#define SW_REV_ID0	0x68
#define SW_REV_ID1	0x6c
#define SEC_KEY_NUM	0x78
#define RETRY		20

struct aspeed_otp {
	struct miscdevice miscdev;
	void __iomem *reg_base;
	bool is_open;
	u32 otp_ver;
	u32 *data;
};

static DEFINE_SPINLOCK(otp_state_lock);

static inline u32 aspeed_otp_read(struct aspeed_otp *ctx, u32 reg)
{
	int val;

	val = readl(ctx->reg_base + reg);
	// printk("read:reg = 0x%08x, val = 0x%08x\n", reg, val);
	return val;
}

static inline void aspeed_otp_write(struct aspeed_otp *ctx, u32 val, u32 reg)
{
	// printk("write:reg = 0x%08x, val = 0x%08x\n", reg, val);
	writel(val, ctx->reg_base + reg);
}

static uint32_t chip_version(u32 revid0, u32 revid1)
{
	if (revid0 == ID0_AST2600A0 && revid1 == ID1_AST2600A0) {
		/* AST2600-A0 */
		return OTP_A0;
	} else if (revid0 == ID0_AST2600A1 && revid1 == ID1_AST2600A1) {
		/* AST2600-A1 */
		return OTP_A1;
	} else if (revid0 == ID0_AST2600A2 && revid1 == ID1_AST2600A2) {
		/* AST2600-A2 */
		return OTP_A2;
	} else if (revid0 == ID0_AST2600A3 && revid1 == ID1_AST2600A3) {
		/* AST2600-A3 */
		return OTP_A3;
	} else if (revid0 == ID0_AST2620A1 && revid1 == ID1_AST2620A1) {
		/* AST2620-A1 */
		return OTP_A1;
	} else if (revid0 == ID0_AST2620A2 && revid1 == ID1_AST2620A2) {
		/* AST2620-A2 */
		return OTP_A2;
	} else if (revid0 == ID0_AST2620A3 && revid1 == ID1_AST2620A3) {
		/* AST2620-A3 */
		return OTP_A3;
	} else if (revid0 == ID0_AST2605A2 && revid1 == ID1_AST2605A2) {
		/* AST2605-A2 */
		return OTP_A2;
	} else if (revid0 == ID0_AST2605A3 && revid1 == ID1_AST2605A3) {
		/* AST2605-A3 */
		return OTP_A3;
	} else if (revid0 == ID0_AST2625A3 && revid1 == ID1_AST2625A3) {
		/* AST2605-A3 */
		return OTP_A3;
	}
	return -1;
}

static void wait_complete(struct aspeed_otp *ctx)
{
	int reg;
	int i = 0;

	do {
		reg = aspeed_otp_read(ctx, OTP_STATUS);
		if ((reg & 0x6) == 0x6)
			i++;
	} while (i != 2);
}

static void otp_write(struct aspeed_otp *ctx, u32 otp_addr, u32 val)
{
	aspeed_otp_write(ctx, otp_addr, OTP_ADDR); //write address
	aspeed_otp_write(ctx, val, OTP_COMPARE_1); //write val
	aspeed_otp_write(ctx, 0x23b1e362, OTP_COMMAND); //write command
	wait_complete(ctx);
}

static void otp_soak(struct aspeed_otp *ctx, int soak)
{
	if (ctx->otp_ver == OTP_A2 || ctx->otp_ver == OTP_A3) {
		switch (soak) {
		case 0: //default
			otp_write(ctx, 0x3000, 0x0); // Write MRA
			otp_write(ctx, 0x5000, 0x0); // Write MRB
			otp_write(ctx, 0x1000, 0x0); // Write MR
			break;
		case 1: //normal program
			otp_write(ctx, 0x3000, 0x1320); // Write MRA
			otp_write(ctx, 0x5000, 0x1008); // Write MRB
			otp_write(ctx, 0x1000, 0x0024); // Write MR
			aspeed_otp_write(ctx, 0x04191388, OTP_TIMING); // 200us
			break;
		case 2: //soak program
			otp_write(ctx, 0x3000, 0x1320); // Write MRA
			otp_write(ctx, 0x5000, 0x0007); // Write MRB
			otp_write(ctx, 0x1000, 0x0100); // Write MR
			aspeed_otp_write(ctx, 0x04193a98, OTP_TIMING); // 600us
			break;
		}
	} else {
		switch (soak) {
		case 0: //default
			otp_write(ctx, 0x3000, 0x0); // Write MRA
			otp_write(ctx, 0x5000, 0x0); // Write MRB
			otp_write(ctx, 0x1000, 0x0); // Write MR
			break;
		case 1: //normal program
			otp_write(ctx, 0x3000, 0x4021); // Write MRA
			otp_write(ctx, 0x5000, 0x302f); // Write MRB
			otp_write(ctx, 0x1000, 0x4020); // Write MR
			aspeed_otp_write(ctx, 0x04190760, OTP_TIMING); // 75us
			break;
		case 2: //soak program
			otp_write(ctx, 0x3000, 0x4021); // Write MRA
			otp_write(ctx, 0x5000, 0x1027); // Write MRB
			otp_write(ctx, 0x1000, 0x4820); // Write MR
			aspeed_otp_write(ctx, 0x041930d4, OTP_TIMING); // 500us
			break;
		}
	}

	wait_complete(ctx);
}

static int verify_bit(struct aspeed_otp *ctx, u32 otp_addr, int bit_offset, int value)
{
	u32 ret[2];

	if (otp_addr % 2 == 0)
		aspeed_otp_write(ctx, otp_addr, OTP_ADDR); //Read address
	else
		aspeed_otp_write(ctx, otp_addr - 1, OTP_ADDR); //Read address

	aspeed_otp_write(ctx, 0x23b1e361, OTP_COMMAND); //trigger read
	wait_complete(ctx);
	ret[0] = aspeed_otp_read(ctx, OTP_COMPARE_1);
	ret[1] = aspeed_otp_read(ctx, OTP_COMPARE_2);

	if (otp_addr % 2 == 0) {
		if (((ret[0] >> bit_offset) & 1) == value)
			return 0;
		else
			return -1;
	} else {
		if (((ret[1] >> bit_offset) & 1) == value)
			return 0;
		else
			return -1;
	}
}

static void otp_prog(struct aspeed_otp *ctx, u32 otp_addr, u32 prog_bit)
{
	otp_write(ctx, 0x0, prog_bit);
	aspeed_otp_write(ctx, otp_addr, OTP_ADDR); //write address
	aspeed_otp_write(ctx, prog_bit, OTP_COMPARE_1); //write data
	aspeed_otp_write(ctx, 0x23b1e364, OTP_COMMAND); //write command
	wait_complete(ctx);
}

static void _otp_prog_bit(struct aspeed_otp *ctx, u32 value, u32 prog_address, u32 bit_offset)
{
	int prog_bit;

	if (prog_address % 2 == 0) {
		if (value)
			prog_bit = ~(0x1 << bit_offset);
		else
			return;
	} else {
		if (ctx->otp_ver != OTP_A3)
			prog_address |= 1 << 15;
		if (!value)
			prog_bit = 0x1 << bit_offset;
		else
			return;
	}
	otp_prog(ctx, prog_address, prog_bit);
}

static int otp_prog_bit(struct aspeed_otp *ctx, u32 value, u32 prog_address, u32 bit_offset)
{
	int pass;
	int i;

	otp_soak(ctx, 1);
	_otp_prog_bit(ctx, value, prog_address, bit_offset);
	pass = 0;

	for (i = 0; i < RETRY; i++) {
		if (verify_bit(ctx, prog_address, bit_offset, value) != 0) {
			otp_soak(ctx, 2);
			_otp_prog_bit(ctx, value, prog_address, bit_offset);
			if (verify_bit(ctx, prog_address, bit_offset, value) != 0) {
				otp_soak(ctx, 1);
			} else {
				pass = 1;
				break;
			}
		} else {
			pass = 1;
			break;
		}
	}
	otp_soak(ctx, 0);
	return pass;
}

static void otp_read_conf_dw(struct aspeed_otp *ctx, u32 offset, u32 *buf)
{
	u32 config_offset;

	config_offset = 0x800;
	config_offset |= (offset / 8) * 0x200;
	config_offset |= (offset % 8) * 0x2;

	aspeed_otp_write(ctx, config_offset, OTP_ADDR); //Read address
	aspeed_otp_write(ctx, 0x23b1e361, OTP_COMMAND); //trigger read
	wait_complete(ctx);
	buf[0] = aspeed_otp_read(ctx, OTP_COMPARE_1);
}

static void otp_read_conf(struct aspeed_otp *ctx, u32 offset, u32 len)
{
	int i, j;

	otp_soak(ctx, 0);
	for (i = offset, j = 0; j < len; i++, j++)
		otp_read_conf_dw(ctx, i, &ctx->data[j]);
}

static void otp_read_data_2dw(struct aspeed_otp *ctx, u32 offset, u32 *buf)
{
	aspeed_otp_write(ctx, offset, OTP_ADDR); //Read address
	aspeed_otp_write(ctx, 0x23b1e361, OTP_COMMAND); //trigger read
	wait_complete(ctx);
	buf[0] = aspeed_otp_read(ctx, OTP_COMPARE_1);
	buf[1] = aspeed_otp_read(ctx, OTP_COMPARE_2);
}

static void otp_read_data(struct aspeed_otp *ctx, u32 offset, u32 len)
{
	int i, j;
	u32 ret[2];

	otp_soak(ctx, 0);

	i = offset;
	j = 0;
	if (offset % 2) {
		otp_read_data_2dw(ctx, i - 1, ret);
		ctx->data[0] = ret[1];
		i++;
		j++;
	}
	for (; j < len; i += 2, j += 2)
		otp_read_data_2dw(ctx, i, &ctx->data[j]);
}

static int otp_prog_data(struct aspeed_otp *ctx, u32 value, u32 dw_offset, u32 bit_offset)
{
	u32 read[2];
	int otp_bit;

	if (dw_offset % 2 == 0) {
		otp_read_data_2dw(ctx, dw_offset, read);
		otp_bit = (read[0] >> bit_offset) & 0x1;

		if (otp_bit == 1 && value == 0) {
			pr_err("OTPDATA%X[%X] = 1\n", dw_offset, bit_offset);
			pr_err("OTP is programed, which can't be cleaned\n");
			return -EINVAL;
		}
	} else {
		otp_read_data_2dw(ctx, dw_offset - 1, read);
		otp_bit = (read[1] >> bit_offset) & 0x1;

		if (otp_bit == 0 && value == 1) {
			pr_err("OTPDATA%X[%X] = 1\n", dw_offset, bit_offset);
			pr_err("OTP is programed, which can't be writen\n");
			return -EINVAL;
		}
	}
	if (otp_bit == value) {
		pr_err("OTPDATA%X[%X] = %d\n", dw_offset, bit_offset, value);
		pr_err("No need to program\n");
		return 0;
	}

	return otp_prog_bit(ctx, value, dw_offset, bit_offset);
}

static int otp_prog_conf(struct aspeed_otp *ctx, u32 value, u32 dw_offset, u32 bit_offset)
{
	u32 read;
	u32 prog_address = 0;
	int otp_bit;

	otp_read_conf_dw(ctx, dw_offset, &read);

	prog_address = 0x800;
	prog_address |= (dw_offset / 8) * 0x200;
	prog_address |= (dw_offset % 8) * 0x2;
	otp_bit = (read >> bit_offset) & 0x1;
	if (otp_bit == value) {
		pr_err("OTPCFG%X[%X] = %d\n", dw_offset, bit_offset, value);
		pr_err("No need to program\n");
		return 0;
	}
	if (otp_bit == 1 && value == 0) {
		pr_err("OTPCFG%X[%X] = 1\n", dw_offset, bit_offset);
		pr_err("OTP is programed, which can't be clean\n");
		return -EINVAL;
	}

	return otp_prog_bit(ctx, value, prog_address, bit_offset);
}

struct aspeed_otp *glob_ctx;

void otp_read_data_buf(u32 offset, u32 *buf, u32 len)
{
	int i, j;
	u32 ret[2];

	aspeed_otp_write(glob_ctx, OTP_PASSWD, OTP_PROTECT_KEY);

	otp_soak(glob_ctx, 0);

	i = offset;
	j = 0;
	if (offset % 2) {
		otp_read_data_2dw(glob_ctx, i - 1, ret);
		buf[0] = ret[1];
		i++;
		j++;
	}
	for (; j < len; i += 2, j += 2)
		otp_read_data_2dw(glob_ctx, i, &buf[j]);
	aspeed_otp_write(glob_ctx, 0, OTP_PROTECT_KEY);
}
EXPORT_SYMBOL(otp_read_data_buf);

void otp_read_conf_buf(u32 offset, u32 *buf, u32 len)
{
	int i, j;

	aspeed_otp_write(glob_ctx, OTP_PASSWD, OTP_PROTECT_KEY);
	otp_soak(glob_ctx, 0);
	for (i = offset, j = 0; j < len; i++, j++)
		otp_read_conf_dw(glob_ctx, i, &buf[j]);
	aspeed_otp_write(glob_ctx, 0, OTP_PROTECT_KEY);
}
EXPORT_SYMBOL(otp_read_conf_buf);

static long otp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct miscdevice *c = file->private_data;
	struct aspeed_otp *ctx = container_of(c, struct aspeed_otp, miscdev);
	void __user *argp = (void __user *)arg;
	struct otp_read xfer;
	struct otp_prog prog;
	u32 reg_read[2];
	int ret = 0;

	switch (cmd) {
	case ASPEED_OTP_READ_DATA:
		if (copy_from_user(&xfer, argp, sizeof(struct otp_read)))
			return -EFAULT;
		if ((xfer.offset + xfer.len) > 0x800) {
			pr_err("out of range");
			return -EINVAL;
		}

		aspeed_otp_write(ctx, OTP_PASSWD, OTP_PROTECT_KEY);
		otp_read_data(ctx, xfer.offset, xfer.len);
		aspeed_otp_write(ctx, 0, OTP_PROTECT_KEY);

		if (copy_to_user(xfer.data, ctx->data, xfer.len * 4))
			return -EFAULT;
		if (copy_to_user(argp, &xfer, sizeof(struct otp_read)))
			return -EFAULT;
		break;
	case ASPEED_OTP_READ_CONF:
		if (copy_from_user(&xfer, argp, sizeof(struct otp_read)))
			return -EFAULT;
		if ((xfer.offset + xfer.len) > 0x800) {
			pr_err("out of range");
			return -EINVAL;
		}

		aspeed_otp_write(ctx, OTP_PASSWD, OTP_PROTECT_KEY);
		otp_read_conf(ctx, xfer.offset, xfer.len);
		aspeed_otp_write(ctx, 0, OTP_PROTECT_KEY);

		if (copy_to_user(xfer.data, ctx->data, xfer.len * 4))
			return -EFAULT;
		if (copy_to_user(argp, &xfer, sizeof(struct otp_read)))
			return -EFAULT;
		break;
	case ASPEED_OTP_PROG_DATA:
		if (copy_from_user(&prog, argp, sizeof(struct otp_prog)))
			return -EFAULT;
		if (prog.bit_offset >= 32 || (prog.value != 0 && prog.value != 1)) {
			pr_err("out of range");
			return -EINVAL;
		}
		if (prog.dw_offset >= 0x800) {
			pr_err("out of range");
			return -EINVAL;
		}
		aspeed_otp_write(ctx, OTP_PASSWD, OTP_PROTECT_KEY);
		ret = otp_prog_data(ctx, prog.value, prog.dw_offset, prog.bit_offset);
		break;
	case ASPEED_OTP_PROG_CONF:
		if (copy_from_user(&prog, argp, sizeof(struct otp_prog)))
			return -EFAULT;
		if (prog.bit_offset >= 32 || (prog.value != 0 && prog.value != 1)) {
			pr_err("out of range");
			return -EINVAL;
		}
		if (prog.dw_offset >= 0x20) {
			pr_err("out of range");
			return -EINVAL;
		}
		aspeed_otp_write(ctx, OTP_PASSWD, OTP_PROTECT_KEY);
		ret = otp_prog_conf(ctx, prog.value, prog.dw_offset, prog.bit_offset);
		break;
	case ASPEED_OTP_VER:
		if (copy_to_user(argp, &ctx->otp_ver, sizeof(u32)))
			return -EFAULT;
		break;
	case ASPEED_OTP_SW_RID:
		reg_read[0] = aspeed_otp_read(ctx, SW_REV_ID0);
		reg_read[1] = aspeed_otp_read(ctx, SW_REV_ID1);
		if (copy_to_user(argp, reg_read, sizeof(u32) * 2))
			return -EFAULT;
		break;
	case ASPEED_SEC_KEY_NUM:
		reg_read[0] = aspeed_otp_read(ctx, SEC_KEY_NUM) & 3;
		if (copy_to_user(argp, reg_read, sizeof(u32)))
			return -EFAULT;
		break;
	}
	return ret;
}

static int otp_open(struct inode *inode, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct aspeed_otp *ctx = container_of(c, struct aspeed_otp, miscdev);

	spin_lock(&otp_state_lock);

	if (ctx->is_open) {
		spin_unlock(&otp_state_lock);
		return -EBUSY;
	}

	ctx->is_open = true;

	spin_unlock(&otp_state_lock);

	return 0;
}

static int otp_release(struct inode *inode, struct file *file)
{
	struct miscdevice *c = file->private_data;
	struct aspeed_otp *ctx = container_of(c, struct aspeed_otp, miscdev);

	spin_lock(&otp_state_lock);

	ctx->is_open = false;

	spin_unlock(&otp_state_lock);

	return 0;
}

static const struct file_operations otp_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	otp_ioctl,
	.open =			otp_open,
	.release =		otp_release,
};

static const struct of_device_id aspeed_otp_of_matches[] = {
	{ .compatible = "aspeed,ast2600-otp" },
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_otp_of_matches);

static int aspeed_otp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *scu;
	struct aspeed_otp *priv;
	struct resource *res;
	u32 revid0, revid1;
	int rc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	glob_ctx = priv;
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot get IORESOURCE_MEM\n");
		return -ENOENT;
	}

	priv->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (!priv->reg_base)
		return -EIO;

	scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_err(dev, "failed to find 2600 SCU regmap\n");
		return PTR_ERR(scu);
	}

	regmap_read(scu, ASPEED_REVISION_ID0, &revid0);
	regmap_read(scu, ASPEED_REVISION_ID1, &revid1);

	priv->otp_ver = chip_version(revid0, revid1);

	if (priv->otp_ver == -1) {
		dev_err(dev, "invalid SCU\n");
		return -EINVAL;
	}

	priv->data = kmalloc(8192, GFP_KERNEL);
	if (!priv->data)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	/* Set up the miscdevice */
	priv->miscdev.minor = MISC_DYNAMIC_MINOR;
	priv->miscdev.name = "aspeed-otp";
	priv->miscdev.fops = &otp_fops;

	/* Register the device */
	rc = misc_register(&priv->miscdev);
	if (rc) {
		dev_err(dev, "Unable to register device\n");
		return rc;
	}

	return 0;
}

static int aspeed_otp_remove(struct platform_device *pdev)
{
	struct aspeed_otp *ctx = dev_get_drvdata(&pdev->dev);

	kfree(ctx->data);
	misc_deregister(&ctx->miscdev);

	return 0;
}

static struct platform_driver aspeed_otp_driver = {
	.probe = aspeed_otp_probe,
	.remove = aspeed_otp_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = aspeed_otp_of_matches,
	},
};

module_platform_driver(aspeed_otp_driver);

MODULE_AUTHOR("Johnny Huang <johnny_huang@aspeedtech.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASPEED OTP Driver");
