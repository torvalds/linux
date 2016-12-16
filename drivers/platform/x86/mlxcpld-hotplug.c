/*
 * drivers/platform/x86/mlxcpld-hotplug.c
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2016 Vadim Pasternak <vadimp@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_data/mlxcpld-hotplug.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

/* Offset of event and mask registers from status register */
#define MLXCPLD_HOTPLUG_EVENT_OFF	1
#define MLXCPLD_HOTPLUG_MASK_OFF	2
#define MLXCPLD_HOTPLUG_AGGR_MASK_OFF	1

#define MLXCPLD_HOTPLUG_ATTRS_NUM	8

/**
 * enum mlxcpld_hotplug_attr_type - sysfs attributes for hotplug events:
 * @MLXCPLD_HOTPLUG_ATTR_TYPE_PSU: power supply unit attribute;
 * @MLXCPLD_HOTPLUG_ATTR_TYPE_PWR: power cable attribute;
 * @MLXCPLD_HOTPLUG_ATTR_TYPE_FAN: FAN drawer attribute;
 */
enum mlxcpld_hotplug_attr_type {
	MLXCPLD_HOTPLUG_ATTR_TYPE_PSU,
	MLXCPLD_HOTPLUG_ATTR_TYPE_PWR,
	MLXCPLD_HOTPLUG_ATTR_TYPE_FAN,
};

/**
 * struct mlxcpld_hotplug_priv_data - platform private data:
 * @irq: platform interrupt number;
 * @pdev: platform device;
 * @plat: platform data;
 * @hwmon: hwmon device;
 * @mlxcpld_hotplug_attr: sysfs attributes array;
 * @mlxcpld_hotplug_dev_attr: sysfs sensor device attribute array;
 * @group: sysfs attribute group;
 * @groups: list of sysfs attribute group for hwmon registration;
 * @dwork: delayed work template;
 * @lock: spin lock;
 * @aggr_cache: last value of aggregation register status;
 * @psu_cache: last value of PSU register status;
 * @pwr_cache: last value of power register status;
 * @fan_cache: last value of FAN register status;
 */
struct mlxcpld_hotplug_priv_data {
	int irq;
	struct platform_device *pdev;
	struct mlxcpld_hotplug_platform_data *plat;
	struct device *hwmon;
	struct attribute *mlxcpld_hotplug_attr[MLXCPLD_HOTPLUG_ATTRS_NUM + 1];
	struct sensor_device_attribute_2
			mlxcpld_hotplug_dev_attr[MLXCPLD_HOTPLUG_ATTRS_NUM];
	struct attribute_group group;
	const struct attribute_group *groups[2];
	struct delayed_work dwork;
	spinlock_t lock;
	u8 aggr_cache;
	u8 psu_cache;
	u8 pwr_cache;
	u8 fan_cache;
};

static ssize_t mlxcpld_hotplug_attr_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mlxcpld_hotplug_priv_data *priv = platform_get_drvdata(pdev);
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	u8 reg_val = 0;

	switch (nr) {
	case MLXCPLD_HOTPLUG_ATTR_TYPE_PSU:
		/* Bit = 0 : PSU is present. */
		reg_val = !!!(inb(priv->plat->psu_reg_offset) & BIT(index));
		break;

	case MLXCPLD_HOTPLUG_ATTR_TYPE_PWR:
		/* Bit = 1 : power cable is attached. */
		reg_val = !!(inb(priv->plat->pwr_reg_offset) & BIT(index %
						priv->plat->pwr_count));
		break;

	case MLXCPLD_HOTPLUG_ATTR_TYPE_FAN:
		/* Bit = 0 : FAN is present. */
		reg_val = !!!(inb(priv->plat->fan_reg_offset) & BIT(index %
						priv->plat->fan_count));
		break;
	}

	return sprintf(buf, "%u\n", reg_val);
}

#define PRIV_ATTR(i) priv->mlxcpld_hotplug_attr[i]
#define PRIV_DEV_ATTR(i) priv->mlxcpld_hotplug_dev_attr[i]
static int mlxcpld_hotplug_attr_init(struct mlxcpld_hotplug_priv_data *priv)
{
	int num_attrs = priv->plat->psu_count + priv->plat->pwr_count +
			priv->plat->fan_count;
	int i;

	priv->group.attrs = devm_kzalloc(&priv->pdev->dev, num_attrs *
					 sizeof(struct attribute *),
					 GFP_KERNEL);
	if (!priv->group.attrs)
		return -ENOMEM;

	for (i = 0; i < num_attrs; i++) {
		PRIV_ATTR(i) = &PRIV_DEV_ATTR(i).dev_attr.attr;

		if (i < priv->plat->psu_count) {
			PRIV_ATTR(i)->name = devm_kasprintf(&priv->pdev->dev,
						GFP_KERNEL, "psu%u", i + 1);
			PRIV_DEV_ATTR(i).nr = MLXCPLD_HOTPLUG_ATTR_TYPE_PSU;
		} else if (i < priv->plat->psu_count + priv->plat->pwr_count) {
			PRIV_ATTR(i)->name = devm_kasprintf(&priv->pdev->dev,
						GFP_KERNEL, "pwr%u", i %
						priv->plat->pwr_count + 1);
			PRIV_DEV_ATTR(i).nr = MLXCPLD_HOTPLUG_ATTR_TYPE_PWR;
		} else {
			PRIV_ATTR(i)->name = devm_kasprintf(&priv->pdev->dev,
						GFP_KERNEL, "fan%u", i %
						priv->plat->fan_count + 1);
			PRIV_DEV_ATTR(i).nr = MLXCPLD_HOTPLUG_ATTR_TYPE_FAN;
		}

		if (!PRIV_ATTR(i)->name) {
			dev_err(&priv->pdev->dev, "Memory allocation failed for sysfs attribute %d.\n",
				i + 1);
			return -ENOMEM;
		}

		PRIV_DEV_ATTR(i).dev_attr.attr.name = PRIV_ATTR(i)->name;
		PRIV_DEV_ATTR(i).dev_attr.attr.mode = S_IRUGO;
		PRIV_DEV_ATTR(i).dev_attr.show = mlxcpld_hotplug_attr_show;
		PRIV_DEV_ATTR(i).index = i;
		sysfs_attr_init(&PRIV_DEV_ATTR(i).dev_attr.attr);
	}

	priv->group.attrs = priv->mlxcpld_hotplug_attr;
	priv->groups[0] = &priv->group;
	priv->groups[1] = NULL;

	return 0;
}

static int mlxcpld_hotplug_device_create(struct device *dev,
					 struct mlxcpld_hotplug_device *item)
{
	item->adapter = i2c_get_adapter(item->bus);
	if (!item->adapter) {
		dev_err(dev, "Failed to get adapter for bus %d\n",
			item->bus);
		return -EFAULT;
	}

	item->client = i2c_new_device(item->adapter, &item->brdinfo);
	if (!item->client) {
		dev_err(dev, "Failed to create client %s at bus %d at addr 0x%02x\n",
			item->brdinfo.type, item->bus, item->brdinfo.addr);
		i2c_put_adapter(item->adapter);
		item->adapter = NULL;
		return -EFAULT;
	}

	return 0;
}

static void mlxcpld_hotplug_device_destroy(struct mlxcpld_hotplug_device *item)
{
	if (item->client) {
		i2c_unregister_device(item->client);
		item->client = NULL;
	}

	if (item->adapter) {
		i2c_put_adapter(item->adapter);
		item->adapter = NULL;
	}
}

static inline void
mlxcpld_hotplug_work_helper(struct device *dev,
			    struct mlxcpld_hotplug_device *item, u8 is_inverse,
			    u16 offset, u8 mask, u8 *cache)
{
	u8 val, asserted;
	int bit;

	/* Mask event. */
	outb(0, offset + MLXCPLD_HOTPLUG_MASK_OFF);
	/* Read status. */
	val = inb(offset) & mask;
	asserted = *cache ^ val;
	*cache = val;

	/*
	 * Validate if item related to received signal type is valid.
	 * It should never happen, excepted the situation when some
	 * piece of hardware is broken. In such situation just produce
	 * error message and return. Caller must continue to handle the
	 * signals from other devices if any.
	 */
	if (unlikely(!item)) {
		dev_err(dev, "False signal is received: register at offset 0x%02x, mask 0x%02x.\n",
			offset, mask);
		return;
	}

	for_each_set_bit(bit, (unsigned long *)&asserted, 8) {
		if (val & BIT(bit)) {
			if (is_inverse)
				mlxcpld_hotplug_device_destroy(item + bit);
			else
				mlxcpld_hotplug_device_create(dev, item + bit);
		} else {
			if (is_inverse)
				mlxcpld_hotplug_device_create(dev, item + bit);
			else
				mlxcpld_hotplug_device_destroy(item + bit);
		}
	}

	/* Acknowledge event. */
	outb(0, offset + MLXCPLD_HOTPLUG_EVENT_OFF);
	/* Unmask event. */
	outb(mask, offset + MLXCPLD_HOTPLUG_MASK_OFF);
}

/*
 * mlxcpld_hotplug_work_handler - performs traversing of CPLD interrupt
 * registers according to the below hierarchy schema:
 *
 *                   Aggregation registers (status/mask)
 * PSU registers:           *---*
 * *-----------------*      |   |
 * |status/event/mask|----->| * |
 * *-----------------*      |   |
 * Power registers:         |   |
 * *-----------------*      |   |
 * |status/event/mask|----->| * |---> CPU
 * *-----------------*      |   |
 * FAN registers:
 * *-----------------*      |   |
 * |status/event/mask|----->| * |
 * *-----------------*      |   |
 *                          *---*
 * In case some system changed are detected: FAN in/out, PSU in/out, power
 * cable attached/detached, relevant device is created or destroyed.
 */
static void mlxcpld_hotplug_work_handler(struct work_struct *work)
{
	struct mlxcpld_hotplug_priv_data *priv = container_of(work,
				struct mlxcpld_hotplug_priv_data, dwork.work);
	u8 val, aggr_asserted;
	unsigned long flags;

	/* Mask aggregation event. */
	outb(0, priv->plat->top_aggr_offset + MLXCPLD_HOTPLUG_AGGR_MASK_OFF);
	/* Read aggregation status. */
	val = inb(priv->plat->top_aggr_offset) & priv->plat->top_aggr_mask;
	aggr_asserted = priv->aggr_cache ^ val;
	priv->aggr_cache = val;

	/* Handle PSU configuration changes. */
	if (aggr_asserted & priv->plat->top_aggr_psu_mask)
		mlxcpld_hotplug_work_helper(&priv->pdev->dev, priv->plat->psu,
					    1, priv->plat->psu_reg_offset,
					    priv->plat->psu_mask,
					    &priv->psu_cache);

	/* Handle power cable configuration changes. */
	if (aggr_asserted & priv->plat->top_aggr_pwr_mask)
		mlxcpld_hotplug_work_helper(&priv->pdev->dev, priv->plat->pwr,
					    0, priv->plat->pwr_reg_offset,
					    priv->plat->pwr_mask,
					    &priv->pwr_cache);

	/* Handle FAN configuration changes. */
	if (aggr_asserted & priv->plat->top_aggr_fan_mask)
		mlxcpld_hotplug_work_helper(&priv->pdev->dev, priv->plat->fan,
					    1, priv->plat->fan_reg_offset,
					    priv->plat->fan_mask,
					    &priv->fan_cache);

	if (aggr_asserted) {
		spin_lock_irqsave(&priv->lock, flags);

		/*
		 * It is possible, that some signals have been inserted, while
		 * interrupt has been masked by mlxcpld_hotplug_work_handler.
		 * In this case such signals will be missed. In order to handle
		 * these signals delayed work is canceled and work task
		 * re-scheduled for immediate execution. It allows to handle
		 * missed signals, if any. In other case work handler just
		 * validates that no new signals have been received during
		 * masking.
		 */
		cancel_delayed_work(&priv->dwork);
		schedule_delayed_work(&priv->dwork, 0);

		spin_unlock_irqrestore(&priv->lock, flags);

		return;
	}

	/* Unmask aggregation event (no need acknowledge). */
	outb(priv->plat->top_aggr_mask, priv->plat->top_aggr_offset +
						MLXCPLD_HOTPLUG_AGGR_MASK_OFF);
}

static void mlxcpld_hotplug_set_irq(struct mlxcpld_hotplug_priv_data *priv)
{
	/* Clear psu presense event. */
	outb(0, priv->plat->psu_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);
	/* Set psu initial status as mask and unmask psu event. */
	priv->psu_cache = priv->plat->psu_mask;
	outb(priv->plat->psu_mask, priv->plat->psu_reg_offset +
						MLXCPLD_HOTPLUG_MASK_OFF);

	/* Clear power cable event. */
	outb(0, priv->plat->pwr_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);
	/* Keep power initial status as zero and unmask power event. */
	outb(priv->plat->pwr_mask, priv->plat->pwr_reg_offset +
						MLXCPLD_HOTPLUG_MASK_OFF);

	/* Clear fan presense event. */
	outb(0, priv->plat->fan_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);
	/* Set fan initial status as mask and unmask fan event. */
	priv->fan_cache = priv->plat->fan_mask;
	outb(priv->plat->fan_mask, priv->plat->fan_reg_offset +
						MLXCPLD_HOTPLUG_MASK_OFF);

	/* Keep aggregation initial status as zero and unmask events. */
	outb(priv->plat->top_aggr_mask, priv->plat->top_aggr_offset +
						MLXCPLD_HOTPLUG_AGGR_MASK_OFF);

	/* Invoke work handler for initializing hot plug devices setting. */
	mlxcpld_hotplug_work_handler(&priv->dwork.work);

	enable_irq(priv->irq);
}

static void mlxcpld_hotplug_unset_irq(struct mlxcpld_hotplug_priv_data *priv)
{
	int i;

	disable_irq(priv->irq);
	cancel_delayed_work_sync(&priv->dwork);

	/* Mask aggregation event. */
	outb(0, priv->plat->top_aggr_offset + MLXCPLD_HOTPLUG_AGGR_MASK_OFF);

	/* Mask psu presense event. */
	outb(0, priv->plat->psu_reg_offset + MLXCPLD_HOTPLUG_MASK_OFF);
	/* Clear psu presense event. */
	outb(0, priv->plat->psu_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);

	/* Mask power cable event. */
	outb(0, priv->plat->pwr_reg_offset + MLXCPLD_HOTPLUG_MASK_OFF);
	/* Clear power cable event. */
	outb(0, priv->plat->pwr_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);

	/* Mask fan presense event. */
	outb(0, priv->plat->fan_reg_offset + MLXCPLD_HOTPLUG_MASK_OFF);
	/* Clear fan presense event. */
	outb(0, priv->plat->fan_reg_offset + MLXCPLD_HOTPLUG_EVENT_OFF);

	/* Remove all the attached devices. */
	for (i = 0; i < priv->plat->psu_count; i++)
		mlxcpld_hotplug_device_destroy(priv->plat->psu + i);

	for (i = 0; i < priv->plat->pwr_count; i++)
		mlxcpld_hotplug_device_destroy(priv->plat->pwr + i);

	for (i = 0; i < priv->plat->fan_count; i++)
		mlxcpld_hotplug_device_destroy(priv->plat->fan + i);
}

static irqreturn_t mlxcpld_hotplug_irq_handler(int irq, void *dev)
{
	struct mlxcpld_hotplug_priv_data *priv =
				(struct mlxcpld_hotplug_priv_data *)dev;

	/* Schedule work task for immediate execution.*/
	schedule_delayed_work(&priv->dwork, 0);

	return IRQ_HANDLED;
}

static int mlxcpld_hotplug_probe(struct platform_device *pdev)
{
	struct mlxcpld_hotplug_platform_data *pdata;
	struct mlxcpld_hotplug_priv_data *priv;
	int err;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "Failed to get platform data.\n");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->pdev = pdev;
	priv->plat = pdata;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n",
			priv->irq);
		return priv->irq;
	}

	err = devm_request_irq(&pdev->dev, priv->irq,
				mlxcpld_hotplug_irq_handler, 0, pdev->name,
				priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", err);
		return err;
	}
	disable_irq(priv->irq);

	INIT_DELAYED_WORK(&priv->dwork, mlxcpld_hotplug_work_handler);
	spin_lock_init(&priv->lock);

	err = mlxcpld_hotplug_attr_init(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate attributes: %d\n", err);
		return err;
	}

	priv->hwmon = devm_hwmon_device_register_with_groups(&pdev->dev,
					"mlxcpld_hotplug", priv, priv->groups);
	if (IS_ERR(priv->hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device %ld\n",
			PTR_ERR(priv->hwmon));
		return PTR_ERR(priv->hwmon);
	}

	platform_set_drvdata(pdev, priv);

	/* Perform initial interrupts setup. */
	mlxcpld_hotplug_set_irq(priv);

	return 0;
}

static int mlxcpld_hotplug_remove(struct platform_device *pdev)
{
	struct mlxcpld_hotplug_priv_data *priv = platform_get_drvdata(pdev);

	/* Clean interrupts setup. */
	mlxcpld_hotplug_unset_irq(priv);

	return 0;
}

static struct platform_driver mlxcpld_hotplug_driver = {
	.driver = {
		.name = "mlxcpld-hotplug",
	},
	.probe = mlxcpld_hotplug_probe,
	.remove = mlxcpld_hotplug_remove,
};

module_platform_driver(mlxcpld_hotplug_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox CPLD hotplug platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:mlxcpld-hotplug");
