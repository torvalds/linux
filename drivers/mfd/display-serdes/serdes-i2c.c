// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * serdes-i2c.c  --  I2C access for different serdes chips
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "core.h"

int serdes_i2c_set_sequence(struct serdes *serdes)
{
	struct device *dev = serdes->dev;
	int i, ret = 0;
	unsigned int def = 0;

	for (i = 0; i < serdes->serdes_init_seq->reg_seq_cnt; i++) {
		if (serdes->serdes_init_seq->reg_sequence[i].reg == 0xffff) {
			SERDES_DBG_MFD("%s: delay 0x%04x us\n", __func__,
				       serdes->serdes_init_seq->reg_sequence[i].def);
			udelay(serdes->serdes_init_seq->reg_sequence[i].def);
			continue;
		}

		ret = serdes_reg_write(serdes,
				       serdes->serdes_init_seq->reg_sequence[i].reg,
				       serdes->serdes_init_seq->reg_sequence[i].def);

		if (ret < 0) {
			dev_err(serdes->dev,
				"failed to write register %04x, ret %d, write again now\n",
				serdes->serdes_init_seq->reg_sequence[i].reg, ret);
			ret = serdes_reg_write(serdes,
					       serdes->serdes_init_seq->reg_sequence[i].reg,
					       serdes->serdes_init_seq->reg_sequence[i].def);
		}
		serdes_reg_read(serdes, serdes->serdes_init_seq->reg_sequence[i].reg, &def);
		if ((def != serdes->serdes_init_seq->reg_sequence[i].def) || (ret < 0)) {
			/* if read value != write value then write again */
			dev_err(dev, "read %04x %04x != %04x\n",
				serdes->serdes_init_seq->reg_sequence[i].reg,
				def, serdes->serdes_init_seq->reg_sequence[i].def);
			serdes_reg_write(serdes,
					 serdes->serdes_init_seq->reg_sequence[i].reg,
					 serdes->serdes_init_seq->reg_sequence[i].def);
		}
	}

	dev_info(dev, "serdes %s sequence_init\n", serdes->chip_data->name);

	return ret;
}
EXPORT_SYMBOL_GPL(serdes_i2c_set_sequence);

static void serdes_mfd_work(struct work_struct *work)
{
	struct serdes *serdes = container_of(work, struct serdes, mfd_delay_work.work);

	mutex_lock(&serdes->wq_lock);
	serdes_device_init(serdes);
	mutex_unlock(&serdes->wq_lock);
}

static const unsigned int serdes_cable[] = {
	EXTCON_JACK_VIDEO_OUT,
	EXTCON_NONE,
};

static int serdes_parse_init_seq(struct device *dev, const u16 *data,
				 int length, struct serdes_init_seq *seq)
{
	struct reg_sequence *reg_sequence;
	u16 *buf, *d;
	unsigned int i, cnt;

	if (!seq)
		return -EINVAL;

	buf = devm_kmemdup(dev, data, length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	d = buf;
	cnt = length / 4;
	seq->reg_seq_cnt = cnt;

	seq->reg_sequence = devm_kcalloc(dev, cnt, sizeof(struct reg_sequence), GFP_KERNEL);
	if (!seq->reg_sequence)
		return -ENOMEM;

	for (i = 0; i < cnt; i++) {
		reg_sequence = &seq->reg_sequence[i];
		reg_sequence->reg = get_unaligned_be16(&d[0]);
		reg_sequence->def = get_unaligned_be16(&d[1]);
		d += 2;
	}

	return 0;
}

static int serdes_get_init_seq(struct serdes *serdes)
{
	struct device *dev = serdes->dev;
	struct device_node *np = dev->of_node;
	const void *data;
	int err, len, ret = 0;

	data = of_get_property(np, "serdes-init-sequence", &len);
	if (!data) {
		dev_err(dev, "failed to get serdes-init-sequence\n");
		return -EINVAL;
	}

	serdes->serdes_init_seq = devm_kzalloc(dev, sizeof(*serdes->serdes_init_seq),
					       GFP_KERNEL);
	if (!serdes->serdes_init_seq)
		return -ENOMEM;

	err = serdes_parse_init_seq(dev, data, len, serdes->serdes_init_seq);
	if (err) {
		dev_err(dev, "failed to parse serdes-init-sequence\n");
		return err;
	}

	/* init ser register(not des register) more early if uboot logo disabled */
	serdes->route_enable = of_property_read_bool(dev->of_node, "route-enable");
	if ((!serdes->route_enable) && (serdes->chip_data->serdes_type == TYPE_SER))
		ret = serdes_i2c_set_sequence(serdes);

	return ret;
}

static int serdes_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct serdes *serdes;
	int ret;

	serdes = devm_kzalloc(&client->dev, sizeof(struct serdes), GFP_KERNEL);
	if (serdes == NULL)
		return -ENOMEM;

	serdes->dev = dev;
	serdes->chip_data = (struct serdes_chip_data *)of_device_get_match_data(dev);
	i2c_set_clientdata(client, serdes);

	dev_info(dev, "serdes %s probe start\n", serdes->chip_data->name);

	serdes->type = serdes->chip_data->serdes_type;
	serdes->regmap = devm_regmap_init_i2c(client, serdes->chip_data->regmap_config);
	if (IS_ERR(serdes->regmap)) {
		ret = PTR_ERR(serdes->regmap);
		dev_err(serdes->dev, "Failed to allocate serdes register map: %d\n",
			ret);
		return ret;
	}

	serdes->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_ASIS);
	if (IS_ERR(serdes->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(serdes->reset_gpio),
				     "failed to acquire serdes reset gpio\n");

	serdes->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(serdes->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(serdes->enable_gpio),
				     "failed to acquire serdes enable gpio\n");

	serdes->vpower = devm_regulator_get_optional(dev, "vpower");
	if (IS_ERR(serdes->vpower)) {
		if (PTR_ERR(serdes->vpower) != -ENODEV)
			return PTR_ERR(serdes->vpower);
	}

	if (!IS_ERR(serdes->vpower)) {
		ret = regulator_enable(serdes->vpower);
		if (ret) {
			dev_err(dev, "fail to enable vpower regulator\n");
			return ret;
		}
	}

	serdes->extcon = devm_extcon_dev_allocate(dev, serdes_cable);
	if (IS_ERR(serdes->extcon))
		return dev_err_probe(dev, PTR_ERR(serdes->extcon),
				     "failed to allocate serdes extcon device\n");

	ret = devm_extcon_dev_register(dev, serdes->extcon);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to register serdes extcon device\n");

	ret = serdes_get_init_seq(serdes);
	if (ret)
		dev_err(dev, "failed to write serdes register with i2c\n");

	mutex_init(&serdes->io_lock);
	dev_set_drvdata(serdes->dev, serdes);
	ret = serdes_irq_init(serdes);
	if (ret != 0) {
		serdes_irq_exit(serdes);
		return ret;
	}

	serdes->use_delay_work = of_property_read_bool(dev->of_node, "use-delay-work");
	if (serdes->use_delay_work) {
		serdes->mfd_wq = alloc_ordered_workqueue("%s",
							 WQ_MEM_RECLAIM | WQ_FREEZABLE,
							 "serdes-mfd-wq");
		mutex_init(&serdes->wq_lock);
		INIT_DELAYED_WORK(&serdes->mfd_delay_work, serdes_mfd_work);
		queue_delayed_work(serdes->mfd_wq, &serdes->mfd_delay_work, msecs_to_jiffies(300));
		SERDES_DBG_MFD("%s: use_delay_work=%d\n", __func__, serdes->use_delay_work);
	} else {
		ret = serdes_device_init(serdes);
		SERDES_DBG_MFD("%s: use_delay_work=%d\n", __func__, serdes->use_delay_work);
	}

	dev_info(dev, "serdes %s serdes_i2c_probe successful version %s\n",
		 serdes->chip_data->name, MFD_SERDES_DISPLAY_VERSION);

	return 0;
}

static void serdes_i2c_shutdown(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct serdes *serdes = dev_get_drvdata(dev);

	serdes_device_shutdown(serdes);
}

static int serdes_i2c_prepare(struct device *dev)
{
	return 0;
}

static void serdes_i2c_complete(struct device *dev)
{
	struct serdes *serdes = dev_get_drvdata(dev);

	if (serdes->chip_data->serdes_type == TYPE_SER)
		serdes_i2c_set_sequence(serdes);

	SERDES_DBG_MFD("%s: name=%s\n", __func__, dev_name(serdes->dev));
}

static int serdes_i2c_suspend(struct device *dev)
{
	struct serdes *serdes = dev_get_drvdata(dev);

	serdes_device_suspend(serdes);

	SERDES_DBG_MFD("%s: name=%s\n", __func__, dev_name(serdes->dev));
	return 0;
}

static int serdes_i2c_resume(struct device *dev)
{
	struct serdes *serdes = dev_get_drvdata(dev);

	if (serdes->chip_data->serdes_type == TYPE_OTHER)
		serdes_i2c_set_sequence(serdes);

	serdes_device_resume(serdes);
	SERDES_DBG_MFD("%s: name=%s\n", __func__, dev_name(serdes->dev));
	return 0;
}

static int serdes_i2c_poweroff(struct device *dev)
{
	struct serdes *serdes = dev_get_drvdata(dev);

	serdes_device_poweroff(serdes);

	return 0;
}

static const struct of_device_id serdes_of_match[] = {
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_ROHM_BU18TL82)
	{ .compatible = "rohm,bu18tl82", .data = &serdes_bu18tl82_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_ROHM_BU18RL82)
	{ .compatible = "rohm,bu18rl82", .data = &serdes_bu18rl82_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_MAXIM_MAX96745)
	{ .compatible = "maxim,max96745", .data = &serdes_max96745_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_MAXIM_MAX96752)
	{ .compatible = "maxim,max96752", .data = &serdes_max96752_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_MAXIM_MAX96755)
	{ .compatible = "maxim,max96755", .data = &serdes_max96755_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_MAXIM_MAX96772)
	{ .compatible = "maxim,max96772", .data = &serdes_max96772_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_ROCKCHIP_RKX111)
	{ .compatible = "rockchip,rkx111", .data = &serdes_rkx111_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_ROCKCHIP_RKX121)
	{ .compatible = "rockchip,rkx121", .data = &serdes_rkx121_data },
#endif
#if IS_ENABLED(CONFIG_SERDES_DISPLAY_CHIP_NOVO_NCA9539)
	{ .compatible = "novo,nca9539", .data = &serdes_nca9539_data },
#endif
	{ }
};

static const struct dev_pm_ops serdes_pm_ops = {
	.prepare = serdes_i2c_prepare,
	.complete = serdes_i2c_complete,
	.suspend = serdes_i2c_suspend,
	.resume = serdes_i2c_resume,
	.poweroff = serdes_i2c_poweroff,
};

static struct i2c_driver serdes_i2c_driver = {
	.driver = {
		.name = "serdes",
		.pm = &serdes_pm_ops,
		.of_match_table = of_match_ptr(serdes_of_match),
	},
	.probe = serdes_i2c_probe,
	.shutdown = serdes_i2c_shutdown,
};

static int __init serdes_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&serdes_i2c_driver);
	if (ret != 0)
		pr_err("Failed to register serdes I2C driver: %d\n", ret);

	return ret;
}
subsys_initcall(serdes_i2c_init);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_DESCRIPTION("display i2c interface for different serdes");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:serdes-i2c");
