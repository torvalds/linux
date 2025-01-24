// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox register access driver
 *
 * Copyright (C) 2018 Mellanox Technologies
 * Copyright (C) 2018 Vadim Pasternak <vadimp@mellanox.com>
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/module.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

/* Attribute parameters. */
#define MLXREG_IO_ATT_SIZE	10
#define MLXREG_IO_ATT_NUM	96

/**
 * struct mlxreg_io_priv_data - driver's private data:
 *
 * @pdev: platform device;
 * @pdata: platform data;
 * @hwmon: hwmon device;
 * @mlxreg_io_attr: sysfs attributes array;
 * @mlxreg_io_dev_attr: sysfs sensor device attribute array;
 * @group: sysfs attribute group;
 * @groups: list of sysfs attribute group for hwmon registration;
 * @regsize: size of a register value;
 * @io_lock: user access locking;
 */
struct mlxreg_io_priv_data {
	struct platform_device *pdev;
	struct mlxreg_core_platform_data *pdata;
	struct device *hwmon;
	struct attribute *mlxreg_io_attr[MLXREG_IO_ATT_NUM + 1];
	struct sensor_device_attribute mlxreg_io_dev_attr[MLXREG_IO_ATT_NUM];
	struct attribute_group group;
	const struct attribute_group *groups[2];
	int regsize;
	struct mutex io_lock; /* Protects user access. */
};

static int
mlxreg_io_get_reg(void *regmap, struct mlxreg_core_data *data, u32 in_val,
		  bool rw_flag, int regsize, u32 *regval)
{
	int i, val, ret;

	ret = regmap_read(regmap, data->reg, regval);
	if (ret)
		goto access_error;

	/*
	 * There are four kinds of attributes: single bit, full register's
	 * bits, bit sequence, bits in few registers For the first kind field
	 * mask indicates which bits are not related and field bit is set zero.
	 * For the second kind field mask is set to zero and field bit is set
	 * with all bits one. No special handling for such kind of attributes -
	 * pass value as is. For the third kind, the field mask indicates which
	 * bits are related and the field bit is set to the first bit number
	 * (from 1 to 32) is the bit sequence. For the fourth kind - the number
	 * of registers which should be read for getting an attribute are
	 * specified through 'data->regnum' field.
	 */
	if (!data->bit) {
		/* Single bit. */
		if (rw_flag) {
			/* For show: expose effective bit value as 0 or 1. */
			*regval = !!(*regval & ~data->mask);
		} else {
			/* For store: set effective bit value. */
			*regval &= data->mask;
			if (in_val)
				*regval |= ~data->mask;
		}
	} else if (data->mask) {
		/* Bit sequence. */
		if (rw_flag) {
			/* For show: mask and shift right. */
			*regval = ror32(*regval & data->mask, (data->bit - 1));
		} else {
			/* For store: shift to the position and mask. */
			in_val = rol32(in_val, data->bit - 1) & data->mask;
			/* Clear relevant bits and set them to new value. */
			*regval = (*regval & ~data->mask) | in_val;
		}
	} else {
		/*
		 * Some attributes could occupied few registers in case regmap
		 * bit size is 8 or 16. Compose such attributes from 'regnum'
		 * registers. Such attributes contain read-only data.
		 */
		for (i = 1; i < data->regnum; i++) {
			ret = regmap_read(regmap, data->reg + i, &val);
			if (ret)
				goto access_error;

			*regval |= rol32(val, regsize * i * 8);
		}
	}

access_error:
	return ret;
}

static ssize_t
mlxreg_io_attr_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct mlxreg_io_priv_data *priv = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	struct mlxreg_core_data *data = priv->pdata->data + index;
	u32 regval = 0;
	int ret;

	mutex_lock(&priv->io_lock);

	ret = mlxreg_io_get_reg(priv->pdata->regmap, data, 0, true,
				priv->regsize, &regval);
	if (ret)
		goto access_error;

	mutex_unlock(&priv->io_lock);

	return sysfs_emit(buf, "%u\n", regval);

access_error:
	mutex_unlock(&priv->io_lock);
	return ret;
}

static ssize_t
mlxreg_io_attr_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t len)
{
	struct mlxreg_io_priv_data *priv = dev_get_drvdata(dev);
	int index = to_sensor_dev_attr(attr)->index;
	struct mlxreg_core_data *data = priv->pdata->data + index;
	u32 input_val, regval;
	int ret;

	if (len > MLXREG_IO_ATT_SIZE)
		return -EINVAL;

	/* Convert buffer to input value. */
	ret = kstrtou32(buf, 0, &input_val);
	if (ret)
		return ret;

	mutex_lock(&priv->io_lock);

	ret = mlxreg_io_get_reg(priv->pdata->regmap, data, input_val, false,
				priv->regsize, &regval);
	if (ret)
		goto access_error;

	ret = regmap_write(priv->pdata->regmap, data->reg, regval);
	if (ret)
		goto access_error;

	mutex_unlock(&priv->io_lock);

	return len;

access_error:
	mutex_unlock(&priv->io_lock);
	dev_err(&priv->pdev->dev, "Bus access error\n");
	return ret;
}

static struct device_attribute mlxreg_io_devattr_rw = {
	.show	= mlxreg_io_attr_show,
	.store	= mlxreg_io_attr_store,
};

static int mlxreg_io_attr_init(struct mlxreg_io_priv_data *priv)
{
	int i;

	priv->group.attrs = devm_kcalloc(&priv->pdev->dev,
					 priv->pdata->counter,
					 sizeof(struct attribute *),
					 GFP_KERNEL);
	if (!priv->group.attrs)
		return -ENOMEM;

	for (i = 0; i < priv->pdata->counter; i++) {
		priv->mlxreg_io_attr[i] =
				&priv->mlxreg_io_dev_attr[i].dev_attr.attr;
		memcpy(&priv->mlxreg_io_dev_attr[i].dev_attr,
		       &mlxreg_io_devattr_rw, sizeof(struct device_attribute));

		/* Set attribute name as a label. */
		priv->mlxreg_io_attr[i]->name =
				devm_kasprintf(&priv->pdev->dev, GFP_KERNEL,
					       priv->pdata->data[i].label);

		if (!priv->mlxreg_io_attr[i]->name) {
			dev_err(&priv->pdev->dev, "Memory allocation failed for sysfs attribute %d.\n",
				i + 1);
			return -ENOMEM;
		}

		priv->mlxreg_io_dev_attr[i].dev_attr.attr.mode =
						priv->pdata->data[i].mode;
		priv->mlxreg_io_dev_attr[i].dev_attr.attr.name =
					priv->mlxreg_io_attr[i]->name;
		priv->mlxreg_io_dev_attr[i].index = i;
		sysfs_attr_init(&priv->mlxreg_io_dev_attr[i].dev_attr.attr);
	}

	priv->group.attrs = priv->mlxreg_io_attr;
	priv->groups[0] = &priv->group;
	priv->groups[1] = NULL;

	return 0;
}

static int mlxreg_io_probe(struct platform_device *pdev)
{
	struct mlxreg_io_priv_data *priv;
	int err;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdata = dev_get_platdata(&pdev->dev);
	if (!priv->pdata) {
		dev_err(&pdev->dev, "Failed to get platform data.\n");
		return -EINVAL;
	}

	priv->pdev = pdev;
	priv->regsize = regmap_get_val_bytes(priv->pdata->regmap);
	if (priv->regsize < 0)
		return priv->regsize;

	err = mlxreg_io_attr_init(priv);
	if (err) {
		dev_err(&priv->pdev->dev, "Failed to allocate attributes: %d\n",
			err);
		return err;
	}

	priv->hwmon = devm_hwmon_device_register_with_groups(&pdev->dev,
							     "mlxreg_io",
							      priv,
							      priv->groups);
	if (IS_ERR(priv->hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device %ld\n",
			PTR_ERR(priv->hwmon));
		return PTR_ERR(priv->hwmon);
	}

	mutex_init(&priv->io_lock);
	dev_set_drvdata(&pdev->dev, priv);

	return 0;
}

static void mlxreg_io_remove(struct platform_device *pdev)
{
	struct mlxreg_io_priv_data *priv = dev_get_drvdata(&pdev->dev);

	mutex_destroy(&priv->io_lock);
}

static struct platform_driver mlxreg_io_driver = {
	.driver = {
	    .name = "mlxreg-io",
	},
	.probe = mlxreg_io_probe,
	.remove = mlxreg_io_remove,
};

module_platform_driver(mlxreg_io_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox regmap I/O access driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mlxreg-io");
