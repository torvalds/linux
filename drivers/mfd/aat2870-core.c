// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/mfd/aat2870-core.c
 *
 * Copyright (c) 2011, NVIDIA Corporation.
 * Author: Jin Park <jinyoungp@nvidia.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/mfd/aat2870.h>
#include <linux/regulator/machine.h>

static struct aat2870_register aat2870_regs[AAT2870_REG_NUM] = {
	/* readable, writeable, value */
	{ 0, 1, 0x00 },	/* 0x00 AAT2870_BL_CH_EN */
	{ 0, 1, 0x16 },	/* 0x01 AAT2870_BLM */
	{ 0, 1, 0x16 },	/* 0x02 AAT2870_BLS */
	{ 0, 1, 0x56 },	/* 0x03 AAT2870_BL1 */
	{ 0, 1, 0x56 },	/* 0x04 AAT2870_BL2 */
	{ 0, 1, 0x56 },	/* 0x05 AAT2870_BL3 */
	{ 0, 1, 0x56 },	/* 0x06 AAT2870_BL4 */
	{ 0, 1, 0x56 },	/* 0x07 AAT2870_BL5 */
	{ 0, 1, 0x56 },	/* 0x08 AAT2870_BL6 */
	{ 0, 1, 0x56 },	/* 0x09 AAT2870_BL7 */
	{ 0, 1, 0x56 },	/* 0x0A AAT2870_BL8 */
	{ 0, 1, 0x00 },	/* 0x0B AAT2870_FLR */
	{ 0, 1, 0x03 },	/* 0x0C AAT2870_FM */
	{ 0, 1, 0x03 },	/* 0x0D AAT2870_FS */
	{ 0, 1, 0x10 },	/* 0x0E AAT2870_ALS_CFG0 */
	{ 0, 1, 0x06 },	/* 0x0F AAT2870_ALS_CFG1 */
	{ 0, 1, 0x00 },	/* 0x10 AAT2870_ALS_CFG2 */
	{ 1, 0, 0x00 },	/* 0x11 AAT2870_AMB */
	{ 0, 1, 0x00 },	/* 0x12 AAT2870_ALS0 */
	{ 0, 1, 0x00 },	/* 0x13 AAT2870_ALS1 */
	{ 0, 1, 0x00 },	/* 0x14 AAT2870_ALS2 */
	{ 0, 1, 0x00 },	/* 0x15 AAT2870_ALS3 */
	{ 0, 1, 0x00 },	/* 0x16 AAT2870_ALS4 */
	{ 0, 1, 0x00 },	/* 0x17 AAT2870_ALS5 */
	{ 0, 1, 0x00 },	/* 0x18 AAT2870_ALS6 */
	{ 0, 1, 0x00 },	/* 0x19 AAT2870_ALS7 */
	{ 0, 1, 0x00 },	/* 0x1A AAT2870_ALS8 */
	{ 0, 1, 0x00 },	/* 0x1B AAT2870_ALS9 */
	{ 0, 1, 0x00 },	/* 0x1C AAT2870_ALSA */
	{ 0, 1, 0x00 },	/* 0x1D AAT2870_ALSB */
	{ 0, 1, 0x00 },	/* 0x1E AAT2870_ALSC */
	{ 0, 1, 0x00 },	/* 0x1F AAT2870_ALSD */
	{ 0, 1, 0x00 },	/* 0x20 AAT2870_ALSE */
	{ 0, 1, 0x00 },	/* 0x21 AAT2870_ALSF */
	{ 0, 1, 0x00 },	/* 0x22 AAT2870_SUB_SET */
	{ 0, 1, 0x00 },	/* 0x23 AAT2870_SUB_CTRL */
	{ 0, 1, 0x00 },	/* 0x24 AAT2870_LDO_AB */
	{ 0, 1, 0x00 },	/* 0x25 AAT2870_LDO_CD */
	{ 0, 1, 0x00 },	/* 0x26 AAT2870_LDO_EN */
};

static struct mfd_cell aat2870_devs[] = {
	{
		.name = "aat2870-backlight",
		.id = AAT2870_ID_BL,
		.pdata_size = sizeof(struct aat2870_bl_platform_data),
	},
	{
		.name = "aat2870-regulator",
		.id = AAT2870_ID_LDOA,
		.pdata_size = sizeof(struct regulator_init_data),
	},
	{
		.name = "aat2870-regulator",
		.id = AAT2870_ID_LDOB,
		.pdata_size = sizeof(struct regulator_init_data),
	},
	{
		.name = "aat2870-regulator",
		.id = AAT2870_ID_LDOC,
		.pdata_size = sizeof(struct regulator_init_data),
	},
	{
		.name = "aat2870-regulator",
		.id = AAT2870_ID_LDOD,
		.pdata_size = sizeof(struct regulator_init_data),
	},
};

static int __aat2870_read(struct aat2870_data *aat2870, u8 addr, u8 *val)
{
	int ret;

	if (addr >= AAT2870_REG_NUM) {
		dev_err(aat2870->dev, "Invalid address, 0x%02x\n", addr);
		return -EINVAL;
	}

	if (!aat2870->reg_cache[addr].readable) {
		*val = aat2870->reg_cache[addr].value;
		goto out;
	}

	ret = i2c_master_send(aat2870->client, &addr, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	ret = i2c_master_recv(aat2870->client, val, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

out:
	dev_dbg(aat2870->dev, "read: addr=0x%02x, val=0x%02x\n", addr, *val);
	return 0;
}

static int __aat2870_write(struct aat2870_data *aat2870, u8 addr, u8 val)
{
	u8 msg[2];
	int ret;

	if (addr >= AAT2870_REG_NUM) {
		dev_err(aat2870->dev, "Invalid address, 0x%02x\n", addr);
		return -EINVAL;
	}

	if (!aat2870->reg_cache[addr].writeable) {
		dev_err(aat2870->dev, "Address 0x%02x is not writeable\n",
			addr);
		return -EINVAL;
	}

	msg[0] = addr;
	msg[1] = val;
	ret = i2c_master_send(aat2870->client, msg, 2);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	aat2870->reg_cache[addr].value = val;

	dev_dbg(aat2870->dev, "write: addr=0x%02x, val=0x%02x\n", addr, val);
	return 0;
}

static int aat2870_read(struct aat2870_data *aat2870, u8 addr, u8 *val)
{
	int ret;

	mutex_lock(&aat2870->io_lock);
	ret = __aat2870_read(aat2870, addr, val);
	mutex_unlock(&aat2870->io_lock);

	return ret;
}

static int aat2870_write(struct aat2870_data *aat2870, u8 addr, u8 val)
{
	int ret;

	mutex_lock(&aat2870->io_lock);
	ret = __aat2870_write(aat2870, addr, val);
	mutex_unlock(&aat2870->io_lock);

	return ret;
}

static int aat2870_update(struct aat2870_data *aat2870, u8 addr, u8 mask,
			  u8 val)
{
	int change;
	u8 old_val, new_val;
	int ret;

	mutex_lock(&aat2870->io_lock);

	ret = __aat2870_read(aat2870, addr, &old_val);
	if (ret)
		goto out_unlock;

	new_val = (old_val & ~mask) | (val & mask);
	change = old_val != new_val;
	if (change)
		ret = __aat2870_write(aat2870, addr, new_val);

out_unlock:
	mutex_unlock(&aat2870->io_lock);

	return ret;
}

static inline void aat2870_enable(struct aat2870_data *aat2870)
{
	if (aat2870->en_pin >= 0)
		gpio_set_value(aat2870->en_pin, 1);

	aat2870->is_enable = 1;
}

static inline void aat2870_disable(struct aat2870_data *aat2870)
{
	if (aat2870->en_pin >= 0)
		gpio_set_value(aat2870->en_pin, 0);

	aat2870->is_enable = 0;
}

#ifdef CONFIG_DEBUG_FS
static ssize_t aat2870_dump_reg(struct aat2870_data *aat2870, char *buf)
{
	u8 addr, val;
	ssize_t count = 0;
	int ret;

	count += sprintf(buf, "aat2870 registers\n");
	for (addr = 0; addr < AAT2870_REG_NUM; addr++) {
		count += snprintf(buf + count, PAGE_SIZE - count, "0x%02x: ", addr);
		if (count >= PAGE_SIZE - 1)
			break;

		ret = aat2870->read(aat2870, addr, &val);
		if (ret == 0)
			count += snprintf(buf + count, PAGE_SIZE - count,
					  "0x%02x", val);
		else
			count += snprintf(buf + count, PAGE_SIZE - count,
					  "<read fail: %d>", ret);

		if (count >= PAGE_SIZE - 1)
			break;

		count += snprintf(buf + count, PAGE_SIZE - count, "\n");
		if (count >= PAGE_SIZE - 1)
			break;
	}

	/* Truncate count; min() would cause a warning */
	if (count >= PAGE_SIZE)
		count = PAGE_SIZE - 1;

	return count;
}

static ssize_t aat2870_reg_read_file(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct aat2870_data *aat2870 = file->private_data;
	char *buf;
	ssize_t ret;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = aat2870_dump_reg(aat2870, buf);
	if (ret >= 0)
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return ret;
}

static ssize_t aat2870_reg_write_file(struct file *file,
				      const char __user *user_buf, size_t count,
				      loff_t *ppos)
{
	struct aat2870_data *aat2870 = file->private_data;
	char buf[32];
	ssize_t buf_size;
	char *start = buf;
	unsigned long addr, val;
	int ret;

	buf_size = min(count, (size_t)(sizeof(buf)-1));
	if (copy_from_user(buf, user_buf, buf_size)) {
		dev_err(aat2870->dev, "Failed to copy from user\n");
		return -EFAULT;
	}
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;

	ret = kstrtoul(start, 16, &addr);
	if (ret)
		return ret;

	if (addr >= AAT2870_REG_NUM) {
		dev_err(aat2870->dev, "Invalid address, 0x%lx\n", addr);
		return -EINVAL;
	}

	while (*start == ' ')
		start++;

	ret = kstrtoul(start, 16, &val);
	if (ret)
		return ret;

	ret = aat2870->write(aat2870, (u8)addr, (u8)val);
	if (ret)
		return ret;

	return buf_size;
}

static const struct file_operations aat2870_reg_fops = {
	.open = simple_open,
	.read = aat2870_reg_read_file,
	.write = aat2870_reg_write_file,
};

static void aat2870_init_debugfs(struct aat2870_data *aat2870)
{
	aat2870->dentry_root = debugfs_create_dir("aat2870", NULL);

	debugfs_create_file("regs", 0644, aat2870->dentry_root, aat2870,
			    &aat2870_reg_fops);
}

#else
static inline void aat2870_init_debugfs(struct aat2870_data *aat2870)
{
}
#endif /* CONFIG_DEBUG_FS */

static int aat2870_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct aat2870_platform_data *pdata = dev_get_platdata(&client->dev);
	struct aat2870_data *aat2870;
	int i, j;
	int ret = 0;

	aat2870 = devm_kzalloc(&client->dev, sizeof(struct aat2870_data),
				GFP_KERNEL);
	if (!aat2870)
		return -ENOMEM;

	aat2870->dev = &client->dev;
	dev_set_drvdata(aat2870->dev, aat2870);

	aat2870->client = client;
	i2c_set_clientdata(client, aat2870);

	aat2870->reg_cache = aat2870_regs;

	if (pdata->en_pin < 0)
		aat2870->en_pin = -1;
	else
		aat2870->en_pin = pdata->en_pin;

	aat2870->init = pdata->init;
	aat2870->uninit = pdata->uninit;
	aat2870->read = aat2870_read;
	aat2870->write = aat2870_write;
	aat2870->update = aat2870_update;

	mutex_init(&aat2870->io_lock);

	if (aat2870->init)
		aat2870->init(aat2870);

	if (aat2870->en_pin >= 0) {
		ret = devm_gpio_request_one(&client->dev, aat2870->en_pin,
					GPIOF_OUT_INIT_HIGH, "aat2870-en");
		if (ret < 0) {
			dev_err(&client->dev,
				"Failed to request GPIO %d\n", aat2870->en_pin);
			return ret;
		}
	}

	aat2870_enable(aat2870);

	for (i = 0; i < pdata->num_subdevs; i++) {
		for (j = 0; j < ARRAY_SIZE(aat2870_devs); j++) {
			if ((pdata->subdevs[i].id == aat2870_devs[j].id) &&
					!strcmp(pdata->subdevs[i].name,
						aat2870_devs[j].name)) {
				aat2870_devs[j].platform_data =
					pdata->subdevs[i].platform_data;
				break;
			}
		}
	}

	ret = mfd_add_devices(aat2870->dev, 0, aat2870_devs,
			      ARRAY_SIZE(aat2870_devs), NULL, 0, NULL);
	if (ret != 0) {
		dev_err(aat2870->dev, "Failed to add subdev: %d\n", ret);
		goto out_disable;
	}

	aat2870_init_debugfs(aat2870);

	return 0;

out_disable:
	aat2870_disable(aat2870);
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int aat2870_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aat2870_data *aat2870 = i2c_get_clientdata(client);

	aat2870_disable(aat2870);

	return 0;
}

static int aat2870_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct aat2870_data *aat2870 = i2c_get_clientdata(client);
	struct aat2870_register *reg = NULL;
	int i;

	aat2870_enable(aat2870);

	/* restore registers */
	for (i = 0; i < AAT2870_REG_NUM; i++) {
		reg = &aat2870->reg_cache[i];
		if (reg->writeable)
			aat2870->write(aat2870, i, reg->value);
	}

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(aat2870_pm_ops, aat2870_i2c_suspend,
			 aat2870_i2c_resume);

static const struct i2c_device_id aat2870_i2c_id_table[] = {
	{ "aat2870", 0 },
	{ }
};

static struct i2c_driver aat2870_i2c_driver = {
	.driver = {
		.name			= "aat2870",
		.pm			= &aat2870_pm_ops,
		.suppress_bind_attrs	= true,
	},
	.probe		= aat2870_i2c_probe,
	.id_table	= aat2870_i2c_id_table,
};

static int __init aat2870_init(void)
{
	return i2c_add_driver(&aat2870_i2c_driver);
}
subsys_initcall(aat2870_init);
