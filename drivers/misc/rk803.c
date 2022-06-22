// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip RK803 driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
 *
 */

#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <uapi/linux/rk803.h>

#define RK803_CHIPID1	0x0A
#define RK803_CHIPID2	0x0B

#define IR_LED_DEFAULT_CURRENT	LED_500MA
#define PRO_LED_DEFAULT_CURRENT	LED_600MA

#define RK803_TIMEOUT		1000 /* usec */

enum SL_LED_CURRENT {
	LED_0MA = 0,
	LED_100MA,
	LED_200MA,
	LED_300MA,
	LED_400MA,
	LED_500MA,
	LED_600MA,
	LED_700MA,
	LED_800MA,
	LED_900MA,
	LED_1000MA,
	LED_1100MA,
	LED_1200MA,
	LED_1300MA,
	LED_1400MA,
	LED_1544MA = 15,
	LED_1600MA,
	LED_1700MA,
	LED_1800MA,
	LED_1900MA,
	LED_2000MA = 20,
	LED_2100MA,
	LED_2200MA,
	LED_2300MA,
	LED_2400MA,
	LED_2500MA,
	LED_2600MA,
	LED_2700MA,
	LED_2800MA,
	LED_2900MA,
	LED_3000MA = 30,
	LED_3100MA,
	LED_3200MA
};

static const char * const rk803_supply_names[] = {
	"dvdd",     /* Digital power */
};

#define RK803_NUM_SUPPLIES ARRAY_SIZE(rk803_supply_names)

struct rk803_data {
	struct i2c_client *client;
	struct regmap *regmap;
	unsigned short chip_id;

	unsigned char current1;
	unsigned char current2;
	struct gpio_desc *gpio_encc1;
	struct gpio_desc *gpio_encc2;
	struct miscdevice misc;
	struct regulator_bulk_data supplies[RK803_NUM_SUPPLIES];
};

static const struct of_device_id rk803_of_match[] = {
	{ .compatible = "rockchip,rk803" },
	{ },
};

static int rk803_power_on(struct rk803_data *rk803)
{
	int ret;
	struct device *dev = &rk803->client->dev;

	ret = regulator_bulk_enable(RK803_NUM_SUPPLIES, rk803->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		return ret;
	}

	usleep_range(1000, 2000);

	return 0;
}

static void rk803_power_off(struct rk803_data *rk803)
{
	regulator_bulk_disable(RK803_NUM_SUPPLIES, rk803->supplies);
}

static ssize_t
rk803_i2c_write_reg(struct rk803_data *rk803, uint8_t reg, uint8_t val)
{
	unsigned long timeout, write_time;
	struct i2c_client *client;
	struct regmap *regmap;
	int ret;

	regmap = rk803->regmap;
	client = rk803->client;
	timeout = jiffies + msecs_to_jiffies(25);

	do {
		/*
		 * The timestamp shall be taken before the actual operation
		 * to avoid a premature timeout in case of high CPU load.
		 */
		write_time = jiffies;

		ret = regmap_write(regmap, reg, val);
		dev_dbg(&client->dev, "write %xu@%d --> %d (%ld)\n",
			 val, reg, ret, jiffies);
		if (!ret)
			return 1;

		usleep_range(1000, 1500);
	} while (time_before(write_time, timeout));

	return -ETIMEDOUT;
}

static long rk803_dev_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;
	struct rk803_data *rk803 =
		container_of(file->private_data, struct rk803_data, misc);

	switch (cmd) {
	case RK803_SET_GPIO1: {
		int val = (int)arg;

		gpiod_set_value(rk803->gpio_encc1, val);
		break;
	}
	case RK803_SET_GPIO2: {
		int val = (int)arg;

		gpiod_set_value(rk803->gpio_encc2, val);
		break;
	}
	case RK803_SET_CURENT1: {
		int val = (int)arg;

		rk803->current1 = val;
		rk803_i2c_write_reg(rk803, 0, rk803->current1);
		break;
	}
	case RK803_SET_CURENT2: {
		int val = (int)arg;

		rk803->current2 = val;
		rk803_i2c_write_reg(rk803, 1, rk803->current2);
		break;
	}
	default:
		ret = -EFAULT;
		break;
	}
	return ret;
}

static const struct file_operations rk803_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = rk803_dev_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = rk803_dev_ioctl
#endif
};

static int rk803_configure_regulators(struct rk803_data *rk803)
{
	unsigned int i;

	for (i = 0; i < RK803_NUM_SUPPLIES; i++)
		rk803->supplies[i].supply = rk803_supply_names[i];

	return devm_regulator_bulk_get(&rk803->client->dev,
				       RK803_NUM_SUPPLIES,
				       rk803->supplies);
}

static int
rk803_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	//struct device_node *np = client->dev.of_node;
	struct device *dev = &client->dev;
	int msb, lsb;
	unsigned short chipid;
	struct rk803_data *rk803;
	struct regmap *regmap;
	struct regmap_config regmap_config = { };
	int ret;

	rk803 = devm_kzalloc(dev, sizeof(*rk803), GFP_KERNEL);
	if (!rk803)
		return -ENOMEM;

	rk803->client = client;

	ret = rk803_configure_regulators(rk803);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	rk803_power_on(rk803);

	/* check chip id */
	msb = i2c_smbus_read_byte_data(client, RK803_CHIPID1);
	if (msb < 0) {
		dev_err(dev, "failed to read the chip1 id at 0x%x\n",
			RK803_CHIPID1);
		ret = -EPROBE_DEFER;
		goto error;
	}
	lsb = i2c_smbus_read_byte_data(client, RK803_CHIPID2);
	if (lsb < 0) {
		dev_err(dev, "failed to read the chip2 id at 0x%x\n",
			RK803_CHIPID2);
		ret = lsb;
		goto error;
	}

	chipid = ((msb << 8) | lsb);
	dev_info(dev, "chip id: 0x%x\n", (unsigned int)chipid);

	regmap_config.val_bits = 8;
	regmap_config.reg_bits = 8;
	regmap_config.disable_locking = true;

	regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		goto error;
	}

	rk803->chip_id = chipid;
	rk803->regmap = regmap;
	rk803->current1 = IR_LED_DEFAULT_CURRENT;
	rk803->current2 = PRO_LED_DEFAULT_CURRENT;

	rk803->gpio_encc1 = devm_gpiod_get(dev, "gpio-encc1", GPIOD_OUT_LOW);
	if (IS_ERR(rk803->gpio_encc1)) {
		dev_err(dev, "can not find gpio_encc1\n");
		ret = PTR_ERR(rk803->gpio_encc1);
		goto error;
	}
	rk803->gpio_encc2 = devm_gpiod_get(dev, "gpio-encc2", GPIOD_OUT_LOW);
	if (IS_ERR(rk803->gpio_encc2)) {
		dev_err(dev, "can not find gpio_encc2\n");
		ret = PTR_ERR(rk803->gpio_encc2);
		goto error;
	}

	/* OVP */
	rk803_i2c_write_reg(rk803, 4, 1);

	/* Control time */
	rk803_i2c_write_reg(rk803, 2, 0xe3);

	/* Control CV */
	rk803_i2c_write_reg(rk803, 3, 0xa7);

	/* PRO */
	rk803_i2c_write_reg(rk803, 0, PRO_LED_DEFAULT_CURRENT);

	/* IR */
	rk803_i2c_write_reg(rk803, 1, IR_LED_DEFAULT_CURRENT);

	i2c_set_clientdata(client, rk803);

	rk803->misc.minor = MISC_DYNAMIC_MINOR;
	rk803->misc.name = "rk803";
	rk803->misc.fops = &rk803_fops;

	ret = misc_register(&rk803->misc);
	if (ret < 0) {
		dev_err(&client->dev, "Error: misc_register returned %d\n",
			ret);
		goto error;
	}

	dev_info(dev, "rk803 probe ok!\n");
	return 0;

error:
	rk803_power_off(rk803);
	return ret;
}

static int rk803_remove(struct i2c_client *client)
{
	struct rk803_data *rk803;

	rk803 = i2c_get_clientdata(client);
	misc_deregister(&rk803->misc);

	rk803_power_off(rk803);

	return 0;
}

static struct i2c_driver rk803_driver = {
	.driver = {
		.name = "rk803",
		.of_match_table = rk803_of_match,
	},
	.probe = rk803_probe,
	.remove = rk803_remove,
};

static int __init rk803_init(void)
{
	return i2c_add_driver(&rk803_driver);
}

subsys_initcall(rk803_init);

static void __exit rk803_exit(void)
{
	i2c_del_driver(&rk803_driver);
}

module_exit(rk803_exit);

MODULE_DESCRIPTION("Driver for RK803");
MODULE_AUTHOR("Rockchip");
MODULE_LICENSE("GPL");
