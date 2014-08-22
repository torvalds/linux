/*
 *  drivers/mfd/rt5025-debug.c
 *  Driver foo Richtek RT5025 PMIC Debug
 *
 *  Copyright (C) 2013 Richtek Technologh Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include <linux/mfd/rt5025.h>

struct rt5025_debug_info {
	struct i2c_client *i2c;
	struct device *dev;
	unsigned char reg_addr;
	unsigned char reg_data;
};

struct rt_debug_st {
	void *info;
	int id;
};

enum {
	RT5025_DBG_REG,
	RT5025_DBG_DATA,
	RT5025_DBG_REGS,
	RT5025_DBG_MAX
};

static struct dentry *debugfs_rt_dent;
static struct dentry *debugfs_file[RT5025_DBG_MAX];
static struct rt_debug_st rtdbg_data[RT5025_DBG_MAX];

static int reg_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static int get_parameters(char *buf, long int *param1, int num_of_par)
{
	char *token;
	int base, cnt;

	token = strsep(&buf, " ");

	for (cnt = 0; cnt < num_of_par; cnt++) {
		if (token != NULL) {
			if ((token[1] == 'x') || (token[1] == 'X'))
				base = 16;
			else
				base = 10;

			if (strict_strtoul(token, base, &param1[cnt]) != 0)
				return -EINVAL;

			token = strsep(&buf, " ");
		} else {
			return -EINVAL;
		}
	}
	return 0;
}

static ssize_t reg_debug_read(struct file *filp, char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct rt_debug_st *st = filp->private_data;
	struct rt5025_debug_info *di = st->info;
	char lbuf[1000];
	int i = 0, j = 0;

	lbuf[0] = '\0';
	switch (st->id) {
	case RT5025_DBG_REG:
		snprintf(lbuf, sizeof(lbuf), "0x%x\n", di->reg_addr);
		break;
	case RT5025_DBG_DATA:
		di->reg_data = (unsigned char)rt5025_reg_read(di->i2c,
			di->reg_addr);
		snprintf(lbuf, sizeof(lbuf), "0x%x\n", di->reg_data);
		break;
	case RT5025_DBG_REGS:
		for (i = RT5025_REG_RANGE1START;
			i <= RT5025_REG_RANGE1END; i++)
			j += sprintf(lbuf+j, "0x%02x:%02x\n", i, \
				rt5025_reg_read(di->i2c, i));
		for (i = RT5025_REG_RANGE2START;
			i <= RT5025_REG_RANGE2END; i++)
			j += sprintf(lbuf+j, "0x%02x:%02x\n", i, \
				rt5025_reg_read(di->i2c, i));
		for (i = RT5025_REG_RANGE3START;
			i <= RT5025_REG_RANGE3END; i++)
			j += sprintf(lbuf+j, "0x%02x:%02x\n", i, \
				rt5025_reg_read(di->i2c, i));
		for (i = RT5025_REG_RANGE4START;
			i <= RT5025_REG_RANGE4END; i++)
			j += sprintf(lbuf+j, "0x%02x:%02x\n", i, \
				rt5025_reg_read(di->i2c, i));
		break;
	default:
		return -EINVAL;
	}
	return simple_read_from_buffer(ubuf, count, ppos, lbuf, strlen(lbuf));
}

static ssize_t reg_debug_write(struct file *filp,
	const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	struct rt_debug_st *st = filp->private_data;
	struct rt5025_debug_info *di = st->info;
	char lbuf[32];
	int rc;
	long int param[5];

	if (cnt > sizeof(lbuf) - 1)
		return -EINVAL;

	rc = copy_from_user(lbuf, ubuf, cnt);
	if (rc)
		return -EFAULT;

	lbuf[cnt] = '\0';

	switch (st->id) {
	case RT5025_DBG_REG:
		rc = get_parameters(lbuf, param, 1);
		if ((param[0] < RT5025_REG_MAX) && (rc == 0)) {
			if ((param[0] >= RT5025_REG_RANGE1START && param[0]
				<= RT5025_REG_RANGE1END) || \
				(param[0] >= RT5025_REG_RANGE2START && param[0]
				<= RT5025_REG_RANGE2END) || \
				(param[0] >= RT5025_REG_RANGE3START && param[0]
				<= RT5025_REG_RANGE3END) || \
				(param[0] >= RT5025_REG_RANGE4START && param[0]
				<= RT5025_REG_RANGE4END))
				di->reg_addr = (unsigned char)param[0];
			else
				rc = -EINVAL;
		} else {
				rc = -EINVAL;
		}
		break;
	case RT5025_DBG_DATA:
		rc = get_parameters(lbuf, param, 1);
		if ((param[0] <= 0xff) && (rc == 0))
			rt5025_reg_write(di->i2c, di->reg_addr, param[0]);
		else
			rc = -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	if (rc == 0)
		rc = cnt;
	return rc;
}

static const struct file_operations reg_debug_ops = {
	.open = reg_debug_open,
	.write = reg_debug_write,
	.read = reg_debug_read
};

static int rt5025_debug_probe(struct platform_device *pdev)
{
	struct rt5025_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct rt5025_debug_info *di;
	int ret = 0;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->i2c = chip->i2c;
	di->dev = &pdev->dev;

	RTINFO("add debugfs for RT5025");
	debugfs_rt_dent = debugfs_create_dir("rt5025_dbg", 0);
	if (!IS_ERR(debugfs_rt_dent)) {
		rtdbg_data[0].info = di;
		rtdbg_data[0].id = RT5025_DBG_REG;
		debugfs_file[0] = debugfs_create_file("reg",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) &rtdbg_data[0], &reg_debug_ops);

		rtdbg_data[1].info = di;
		rtdbg_data[1].id = RT5025_DBG_DATA;
		debugfs_file[1] = debugfs_create_file("data",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) &rtdbg_data[1], &reg_debug_ops);

		rtdbg_data[2].info = di;
		rtdbg_data[2].id = RT5025_DBG_REGS;
		debugfs_file[2] = debugfs_create_file("regs",
		S_IFREG | S_IRUGO, debugfs_rt_dent,
		(void *) &rtdbg_data[2], &reg_debug_ops);
	} else {
		dev_err(&pdev->dev, "create debugfs failed\n");
		ret = -EINVAL;
		goto err_init;
	}
	platform_set_drvdata(pdev, di);
	return 0;
err_init:
	return ret;
}

static int rt5025_debug_remove(struct platform_device *pdev)
{
	if (!IS_ERR(debugfs_rt_dent))
		debugfs_remove_recursive(debugfs_rt_dent);
	return 0;
}

static const struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025-debug",},
	{},
};

static struct platform_driver rt5025_debug_driver = {
	.driver = {
		.name = RT5025_DEV_NAME "-debug",
		.owner = THIS_MODULE,
		.of_match_table = rt_match_table,
	},
	.probe = rt5025_debug_probe,
	.remove = rt5025_debug_remove,
};

static int rt5025_debug_init(void)
{
	return platform_driver_register(&rt5025_debug_driver);
}
module_init(rt5025_debug_init);

static void rt5025_debug_exit(void)
{
	platform_driver_unregister(&rt5025_debug_driver);
}
module_exit(rt5025_debug_exit);
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com");
MODULE_DESCRIPTION("Debug driver for RT5025");
MODULE_ALIAS("platform:" RT5025_DEV_NAME "-debug");
MODULE_VERSION(RT5025_DRV_VER);
