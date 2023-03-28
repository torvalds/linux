// SPDX-License-Identifier: GPL-2.0+
/*
 * Mellanox hotplug driver
 *
 * Copyright (C) 2016-2020 Mellanox Technologies
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_data/mlxreg.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/string_helpers.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>

/* Offset of event and mask registers from status register. */
#define MLXREG_HOTPLUG_EVENT_OFF	1
#define MLXREG_HOTPLUG_MASK_OFF		2
#define MLXREG_HOTPLUG_AGGR_MASK_OFF	1

/* ASIC good health mask. */
#define MLXREG_HOTPLUG_GOOD_HEALTH_MASK	0x02

#define MLXREG_HOTPLUG_ATTRS_MAX	128
#define MLXREG_HOTPLUG_NOT_ASSERT	3

/**
 * struct mlxreg_hotplug_priv_data - platform private data:
 * @irq: platform device interrupt number;
 * @dev: basic device;
 * @pdev: platform device;
 * @plat: platform data;
 * @regmap: register map handle;
 * @dwork_irq: delayed work template;
 * @lock: spin lock;
 * @hwmon: hwmon device;
 * @mlxreg_hotplug_attr: sysfs attributes array;
 * @mlxreg_hotplug_dev_attr: sysfs sensor device attribute array;
 * @group: sysfs attribute group;
 * @groups: list of sysfs attribute group for hwmon registration;
 * @cell: location of top aggregation interrupt register;
 * @mask: top aggregation interrupt common mask;
 * @aggr_cache: last value of aggregation register status;
 * @after_probe: flag indication probing completion;
 * @not_asserted: number of entries in workqueue with no signal assertion;
 */
struct mlxreg_hotplug_priv_data {
	int irq;
	struct device *dev;
	struct platform_device *pdev;
	struct mlxreg_hotplug_platform_data *plat;
	struct regmap *regmap;
	struct delayed_work dwork_irq;
	spinlock_t lock; /* sync with interrupt */
	struct device *hwmon;
	struct attribute *mlxreg_hotplug_attr[MLXREG_HOTPLUG_ATTRS_MAX + 1];
	struct sensor_device_attribute_2
			mlxreg_hotplug_dev_attr[MLXREG_HOTPLUG_ATTRS_MAX];
	struct attribute_group group;
	const struct attribute_group *groups[2];
	u32 cell;
	u32 mask;
	u32 aggr_cache;
	bool after_probe;
	u8 not_asserted;
};

/* Environment variables array for udev. */
static char *mlxreg_hotplug_udev_envp[] = { NULL, NULL };

static int
mlxreg_hotplug_udev_event_send(struct kobject *kobj,
			       struct mlxreg_core_data *data, bool action)
{
	char event_str[MLXREG_CORE_LABEL_MAX_SIZE + 2];
	char label[MLXREG_CORE_LABEL_MAX_SIZE] = { 0 };

	mlxreg_hotplug_udev_envp[0] = event_str;
	string_upper(label, data->label);
	snprintf(event_str, MLXREG_CORE_LABEL_MAX_SIZE, "%s=%d", label, !!action);

	return kobject_uevent_env(kobj, KOBJ_CHANGE, mlxreg_hotplug_udev_envp);
}

static void
mlxreg_hotplug_pdata_export(void *pdata, void *regmap)
{
	struct mlxreg_core_hotplug_platform_data *dev_pdata = pdata;

	/* Export regmap to underlying device. */
	dev_pdata->regmap = regmap;
}

static int mlxreg_hotplug_device_create(struct mlxreg_hotplug_priv_data *priv,
					struct mlxreg_core_data *data,
					enum mlxreg_hotplug_kind kind)
{
	struct i2c_board_info *brdinfo = data->hpdev.brdinfo;
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct i2c_client *client;

	/* Notify user by sending hwmon uevent. */
	mlxreg_hotplug_udev_event_send(&priv->hwmon->kobj, data, true);

	/*
	 * Return if adapter number is negative. It could be in case hotplug
	 * event is not associated with hotplug device.
	 */
	if (data->hpdev.nr < 0)
		return 0;

	pdata = dev_get_platdata(&priv->pdev->dev);
	switch (data->hpdev.action) {
	case MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION:
		data->hpdev.adapter = i2c_get_adapter(data->hpdev.nr +
						      pdata->shift_nr);
		if (!data->hpdev.adapter) {
			dev_err(priv->dev, "Failed to get adapter for bus %d\n",
				data->hpdev.nr + pdata->shift_nr);
			return -EFAULT;
		}

		/* Export platform data to underlying device. */
		if (brdinfo->platform_data)
			mlxreg_hotplug_pdata_export(brdinfo->platform_data, pdata->regmap);

		client = i2c_new_client_device(data->hpdev.adapter,
					       brdinfo);
		if (IS_ERR(client)) {
			dev_err(priv->dev, "Failed to create client %s at bus %d at addr 0x%02x\n",
				brdinfo->type, data->hpdev.nr +
				pdata->shift_nr, brdinfo->addr);

			i2c_put_adapter(data->hpdev.adapter);
			data->hpdev.adapter = NULL;
			return PTR_ERR(client);
		}

		data->hpdev.client = client;
		break;
	case MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION:
		/* Export platform data to underlying device. */
		if (data->hpdev.brdinfo && data->hpdev.brdinfo->platform_data)
			mlxreg_hotplug_pdata_export(data->hpdev.brdinfo->platform_data,
						    pdata->regmap);
		/* Pass parent hotplug device handle to underlying device. */
		data->notifier = data->hpdev.notifier;
		data->hpdev.pdev = platform_device_register_resndata(&priv->pdev->dev,
								     brdinfo->type,
								     data->hpdev.nr,
								     NULL, 0, data,
								     sizeof(*data));
		if (IS_ERR(data->hpdev.pdev))
			return PTR_ERR(data->hpdev.pdev);

		break;
	default:
		break;
	}

	if (data->hpdev.notifier && data->hpdev.notifier->user_handler)
		return data->hpdev.notifier->user_handler(data->hpdev.notifier->handle, kind, 1);

	return 0;
}

static void
mlxreg_hotplug_device_destroy(struct mlxreg_hotplug_priv_data *priv,
			      struct mlxreg_core_data *data,
			      enum mlxreg_hotplug_kind kind)
{
	/* Notify user by sending hwmon uevent. */
	mlxreg_hotplug_udev_event_send(&priv->hwmon->kobj, data, false);
	if (data->hpdev.notifier && data->hpdev.notifier->user_handler)
		data->hpdev.notifier->user_handler(data->hpdev.notifier->handle, kind, 0);

	switch (data->hpdev.action) {
	case MLXREG_HOTPLUG_DEVICE_DEFAULT_ACTION:
		if (data->hpdev.client) {
			i2c_unregister_device(data->hpdev.client);
			data->hpdev.client = NULL;
		}

		if (data->hpdev.adapter) {
			i2c_put_adapter(data->hpdev.adapter);
			data->hpdev.adapter = NULL;
		}
		break;
	case MLXREG_HOTPLUG_DEVICE_PLATFORM_ACTION:
		if (data->hpdev.pdev)
			platform_device_unregister(data->hpdev.pdev);
		break;
	default:
		break;
	}
}

static ssize_t mlxreg_hotplug_attr_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct mlxreg_hotplug_priv_data *priv = dev_get_drvdata(dev);
	struct mlxreg_core_hotplug_platform_data *pdata;
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	struct mlxreg_core_item *item;
	struct mlxreg_core_data *data;
	u32 regval;
	int ret;

	pdata = dev_get_platdata(&priv->pdev->dev);
	item = pdata->items + nr;
	data = item->data + index;

	ret = regmap_read(priv->regmap, data->reg, &regval);
	if (ret)
		return ret;

	if (item->health) {
		regval &= data->mask;
	} else {
		/* Bit = 0 : functional if item->inversed is true. */
		if (item->inversed)
			regval = !(regval & data->mask);
		else
			regval = !!(regval & data->mask);
	}

	return sprintf(buf, "%u\n", regval);
}

#define PRIV_ATTR(i) priv->mlxreg_hotplug_attr[i]
#define PRIV_DEV_ATTR(i) priv->mlxreg_hotplug_dev_attr[i]

static int mlxreg_hotplug_item_label_index_get(u32 mask, u32 bit)
{
	int i, j;

	for (i = 0, j = -1; i <= bit; i++) {
		if (mask & BIT(i))
			j++;
	}
	return j;
}

static int mlxreg_hotplug_attr_init(struct mlxreg_hotplug_priv_data *priv)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_core_item *item;
	struct mlxreg_core_data *data;
	unsigned long mask;
	u32 regval;
	int num_attrs = 0, id = 0, i, j, k, count, ret;

	pdata = dev_get_platdata(&priv->pdev->dev);
	item = pdata->items;

	/* Go over all kinds of items - psu, pwr, fan. */
	for (i = 0; i < pdata->counter; i++, item++) {
		if (item->capability) {
			/*
			 * Read group capability register to get actual number
			 * of interrupt capable components and set group mask
			 * accordingly.
			 */
			ret = regmap_read(priv->regmap, item->capability,
					  &regval);
			if (ret)
				return ret;

			item->mask = GENMASK((regval & item->mask) - 1, 0);
		}

		data = item->data;

		/* Go over all unmasked units within item. */
		mask = item->mask;
		k = 0;
		count = item->ind ? item->ind : item->count;
		for_each_set_bit(j, &mask, count) {
			if (data->capability) {
				/*
				 * Read capability register and skip non
				 * relevant attributes.
				 */
				ret = regmap_read(priv->regmap,
						  data->capability, &regval);
				if (ret)
					return ret;

				if (!(regval & data->bit)) {
					data++;
					continue;
				}
			}

			PRIV_ATTR(id) = &PRIV_DEV_ATTR(id).dev_attr.attr;
			PRIV_ATTR(id)->name = devm_kasprintf(&priv->pdev->dev,
							     GFP_KERNEL,
							     data->label);
			if (!PRIV_ATTR(id)->name) {
				dev_err(priv->dev, "Memory allocation failed for attr %d.\n",
					id);
				return -ENOMEM;
			}

			PRIV_DEV_ATTR(id).dev_attr.attr.name =
							PRIV_ATTR(id)->name;
			PRIV_DEV_ATTR(id).dev_attr.attr.mode = 0444;
			PRIV_DEV_ATTR(id).dev_attr.show =
						mlxreg_hotplug_attr_show;
			PRIV_DEV_ATTR(id).nr = i;
			PRIV_DEV_ATTR(id).index = k;
			sysfs_attr_init(&PRIV_DEV_ATTR(id).dev_attr.attr);
			data++;
			id++;
			k++;
		}
		num_attrs += k;
	}

	priv->group.attrs = devm_kcalloc(&priv->pdev->dev,
					 num_attrs,
					 sizeof(struct attribute *),
					 GFP_KERNEL);
	if (!priv->group.attrs)
		return -ENOMEM;

	priv->group.attrs = priv->mlxreg_hotplug_attr;
	priv->groups[0] = &priv->group;
	priv->groups[1] = NULL;

	return 0;
}

static void
mlxreg_hotplug_work_helper(struct mlxreg_hotplug_priv_data *priv,
			   struct mlxreg_core_item *item)
{
	struct mlxreg_core_data *data;
	unsigned long asserted;
	u32 regval, bit;
	int ret;

	/*
	 * Validate if item related to received signal type is valid.
	 * It should never happen, excepted the situation when some
	 * piece of hardware is broken. In such situation just produce
	 * error message and return. Caller must continue to handle the
	 * signals from other devices if any.
	 */
	if (unlikely(!item)) {
		dev_err(priv->dev, "False signal: at offset:mask 0x%02x:0x%02x.\n",
			item->reg, item->mask);

		return;
	}

	/* Mask event. */
	ret = regmap_write(priv->regmap, item->reg + MLXREG_HOTPLUG_MASK_OFF,
			   0);
	if (ret)
		goto out;

	/* Read status. */
	ret = regmap_read(priv->regmap, item->reg, &regval);
	if (ret)
		goto out;

	/* Set asserted bits and save last status. */
	regval &= item->mask;
	asserted = item->cache ^ regval;
	item->cache = regval;
	for_each_set_bit(bit, &asserted, 8) {
		int pos;

		pos = mlxreg_hotplug_item_label_index_get(item->mask, bit);
		if (pos < 0)
			goto out;

		data = item->data + pos;
		if (regval & BIT(bit)) {
			if (item->inversed)
				mlxreg_hotplug_device_destroy(priv, data, item->kind);
			else
				mlxreg_hotplug_device_create(priv, data, item->kind);
		} else {
			if (item->inversed)
				mlxreg_hotplug_device_create(priv, data, item->kind);
			else
				mlxreg_hotplug_device_destroy(priv, data, item->kind);
		}
	}

	/* Acknowledge event. */
	ret = regmap_write(priv->regmap, item->reg + MLXREG_HOTPLUG_EVENT_OFF,
			   0);
	if (ret)
		goto out;

	/* Unmask event. */
	ret = regmap_write(priv->regmap, item->reg + MLXREG_HOTPLUG_MASK_OFF,
			   item->mask);

 out:
	if (ret)
		dev_err(priv->dev, "Failed to complete workqueue.\n");
}

static void
mlxreg_hotplug_health_work_helper(struct mlxreg_hotplug_priv_data *priv,
				  struct mlxreg_core_item *item)
{
	struct mlxreg_core_data *data = item->data;
	u32 regval;
	int i, ret = 0;

	for (i = 0; i < item->count; i++, data++) {
		/* Mask event. */
		ret = regmap_write(priv->regmap, data->reg +
				   MLXREG_HOTPLUG_MASK_OFF, 0);
		if (ret)
			goto out;

		/* Read status. */
		ret = regmap_read(priv->regmap, data->reg, &regval);
		if (ret)
			goto out;

		regval &= data->mask;

		if (item->cache == regval)
			goto ack_event;

		/*
		 * ASIC health indication is provided through two bits. Bits
		 * value 0x2 indicates that ASIC reached the good health, value
		 * 0x0 indicates ASIC the bad health or dormant state and value
		 * 0x3 indicates the booting state. During ASIC reset it should
		 * pass the following states: dormant -> booting -> good.
		 */
		if (regval == MLXREG_HOTPLUG_GOOD_HEALTH_MASK) {
			if (!data->attached) {
				/*
				 * ASIC is in steady state. Connect associated
				 * device, if configured.
				 */
				mlxreg_hotplug_device_create(priv, data, item->kind);
				data->attached = true;
			}
		} else {
			if (data->attached) {
				/*
				 * ASIC health is failed after ASIC has been
				 * in steady state. Disconnect associated
				 * device, if it has been connected.
				 */
				mlxreg_hotplug_device_destroy(priv, data, item->kind);
				data->attached = false;
				data->health_cntr = 0;
			}
		}
		item->cache = regval;
ack_event:
		/* Acknowledge event. */
		ret = regmap_write(priv->regmap, data->reg +
				   MLXREG_HOTPLUG_EVENT_OFF, 0);
		if (ret)
			goto out;

		/* Unmask event. */
		ret = regmap_write(priv->regmap, data->reg +
				   MLXREG_HOTPLUG_MASK_OFF, data->mask);
		if (ret)
			goto out;
	}

 out:
	if (ret)
		dev_err(priv->dev, "Failed to complete workqueue.\n");
}

/*
 * mlxreg_hotplug_work_handler - performs traversing of device interrupt
 * registers according to the below hierarchy schema:
 *
 *				Aggregation registers (status/mask)
 * PSU registers:		*---*
 * *-----------------*		|   |
 * |status/event/mask|----->    | * |
 * *-----------------*		|   |
 * Power registers:		|   |
 * *-----------------*		|   |
 * |status/event/mask|----->    | * |
 * *-----------------*		|   |
 * FAN registers:		|   |--> CPU
 * *-----------------*		|   |
 * |status/event/mask|----->    | * |
 * *-----------------*		|   |
 * ASIC registers:		|   |
 * *-----------------*		|   |
 * |status/event/mask|----->    | * |
 * *-----------------*		|   |
 *				*---*
 *
 * In case some system changed are detected: FAN in/out, PSU in/out, power
 * cable attached/detached, ASIC health good/bad, relevant device is created
 * or destroyed.
 */
static void mlxreg_hotplug_work_handler(struct work_struct *work)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_hotplug_priv_data *priv;
	struct mlxreg_core_item *item;
	u32 regval, aggr_asserted;
	unsigned long flags;
	int i, ret;

	priv = container_of(work, struct mlxreg_hotplug_priv_data,
			    dwork_irq.work);
	pdata = dev_get_platdata(&priv->pdev->dev);
	item = pdata->items;

	/* Mask aggregation event. */
	ret = regmap_write(priv->regmap, pdata->cell +
			   MLXREG_HOTPLUG_AGGR_MASK_OFF, 0);
	if (ret < 0)
		goto out;

	/* Read aggregation status. */
	ret = regmap_read(priv->regmap, pdata->cell, &regval);
	if (ret)
		goto out;

	regval &= pdata->mask;
	aggr_asserted = priv->aggr_cache ^ regval;
	priv->aggr_cache = regval;

	/*
	 * Handler is invoked, but no assertion is detected at top aggregation
	 * status level. Set aggr_asserted to mask value to allow handler extra
	 * run over all relevant signals to recover any missed signal.
	 */
	if (priv->not_asserted == MLXREG_HOTPLUG_NOT_ASSERT) {
		priv->not_asserted = 0;
		aggr_asserted = pdata->mask;
	}
	if (!aggr_asserted)
		goto unmask_event;

	/* Handle topology and health configuration changes. */
	for (i = 0; i < pdata->counter; i++, item++) {
		if (aggr_asserted & item->aggr_mask) {
			if (item->health)
				mlxreg_hotplug_health_work_helper(priv, item);
			else
				mlxreg_hotplug_work_helper(priv, item);
		}
	}

	spin_lock_irqsave(&priv->lock, flags);

	/*
	 * It is possible, that some signals have been inserted, while
	 * interrupt has been masked by mlxreg_hotplug_work_handler. In this
	 * case such signals will be missed. In order to handle these signals
	 * delayed work is canceled and work task re-scheduled for immediate
	 * execution. It allows to handle missed signals, if any. In other case
	 * work handler just validates that no new signals have been received
	 * during masking.
	 */
	cancel_delayed_work(&priv->dwork_irq);
	schedule_delayed_work(&priv->dwork_irq, 0);

	spin_unlock_irqrestore(&priv->lock, flags);

	return;

unmask_event:
	priv->not_asserted++;
	/* Unmask aggregation event (no need acknowledge). */
	ret = regmap_write(priv->regmap, pdata->cell +
			   MLXREG_HOTPLUG_AGGR_MASK_OFF, pdata->mask);

 out:
	if (ret)
		dev_err(priv->dev, "Failed to complete workqueue.\n");
}

static int mlxreg_hotplug_set_irq(struct mlxreg_hotplug_priv_data *priv)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_core_item *item;
	struct mlxreg_core_data *data;
	u32 regval;
	int i, j, ret;

	pdata = dev_get_platdata(&priv->pdev->dev);
	item = pdata->items;

	for (i = 0; i < pdata->counter; i++, item++) {
		/* Clear group presense event. */
		ret = regmap_write(priv->regmap, item->reg +
				   MLXREG_HOTPLUG_EVENT_OFF, 0);
		if (ret)
			goto out;

		/*
		 * Verify if hardware configuration requires to disable
		 * interrupt capability for some of components.
		 */
		data = item->data;
		for (j = 0; j < item->count; j++, data++) {
			/* Verify if the attribute has capability register. */
			if (data->capability) {
				/* Read capability register. */
				ret = regmap_read(priv->regmap,
						  data->capability, &regval);
				if (ret)
					goto out;

				if (!(regval & data->bit))
					item->mask &= ~BIT(j);
			}
		}

		/* Set group initial status as mask and unmask group event. */
		if (item->inversed) {
			item->cache = item->mask;
			ret = regmap_write(priv->regmap, item->reg +
					   MLXREG_HOTPLUG_MASK_OFF,
					   item->mask);
			if (ret)
				goto out;
		}
	}

	/* Keep aggregation initial status as zero and unmask events. */
	ret = regmap_write(priv->regmap, pdata->cell +
			   MLXREG_HOTPLUG_AGGR_MASK_OFF, pdata->mask);
	if (ret)
		goto out;

	/* Keep low aggregation initial status as zero and unmask events. */
	if (pdata->cell_low) {
		ret = regmap_write(priv->regmap, pdata->cell_low +
				   MLXREG_HOTPLUG_AGGR_MASK_OFF,
				   pdata->mask_low);
		if (ret)
			goto out;
	}

	/* Invoke work handler for initializing hot plug devices setting. */
	mlxreg_hotplug_work_handler(&priv->dwork_irq.work);

 out:
	if (ret)
		dev_err(priv->dev, "Failed to set interrupts.\n");
	enable_irq(priv->irq);
	return ret;
}

static void mlxreg_hotplug_unset_irq(struct mlxreg_hotplug_priv_data *priv)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_core_item *item;
	struct mlxreg_core_data *data;
	int count, i, j;

	pdata = dev_get_platdata(&priv->pdev->dev);
	item = pdata->items;
	disable_irq(priv->irq);
	cancel_delayed_work_sync(&priv->dwork_irq);

	/* Mask low aggregation event, if defined. */
	if (pdata->cell_low)
		regmap_write(priv->regmap, pdata->cell_low +
			     MLXREG_HOTPLUG_AGGR_MASK_OFF, 0);

	/* Mask aggregation event. */
	regmap_write(priv->regmap, pdata->cell + MLXREG_HOTPLUG_AGGR_MASK_OFF,
		     0);

	/* Clear topology configurations. */
	for (i = 0; i < pdata->counter; i++, item++) {
		data = item->data;
		/* Mask group presense event. */
		regmap_write(priv->regmap, data->reg + MLXREG_HOTPLUG_MASK_OFF,
			     0);
		/* Clear group presense event. */
		regmap_write(priv->regmap, data->reg +
			     MLXREG_HOTPLUG_EVENT_OFF, 0);

		/* Remove all the attached devices in group. */
		count = item->count;
		for (j = 0; j < count; j++, data++)
			mlxreg_hotplug_device_destroy(priv, data, item->kind);
	}
}

static irqreturn_t mlxreg_hotplug_irq_handler(int irq, void *dev)
{
	struct mlxreg_hotplug_priv_data *priv;

	priv = (struct mlxreg_hotplug_priv_data *)dev;

	/* Schedule work task for immediate execution.*/
	schedule_delayed_work(&priv->dwork_irq, 0);

	return IRQ_HANDLED;
}

static int mlxreg_hotplug_probe(struct platform_device *pdev)
{
	struct mlxreg_core_hotplug_platform_data *pdata;
	struct mlxreg_hotplug_priv_data *priv;
	struct i2c_adapter *deferred_adap;
	int err;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "Failed to get platform data.\n");
		return -EINVAL;
	}

	/* Defer probing if the necessary adapter is not configured yet. */
	deferred_adap = i2c_get_adapter(pdata->deferred_nr);
	if (!deferred_adap)
		return -EPROBE_DEFER;
	i2c_put_adapter(deferred_adap);

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (pdata->irq) {
		priv->irq = pdata->irq;
	} else {
		priv->irq = platform_get_irq(pdev, 0);
		if (priv->irq < 0)
			return priv->irq;
	}

	priv->regmap = pdata->regmap;
	priv->dev = pdev->dev.parent;
	priv->pdev = pdev;

	err = devm_request_irq(&pdev->dev, priv->irq,
			       mlxreg_hotplug_irq_handler, IRQF_TRIGGER_FALLING
			       | IRQF_SHARED, "mlxreg-hotplug", priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", err);
		return err;
	}

	disable_irq(priv->irq);
	spin_lock_init(&priv->lock);
	INIT_DELAYED_WORK(&priv->dwork_irq, mlxreg_hotplug_work_handler);
	dev_set_drvdata(&pdev->dev, priv);

	err = mlxreg_hotplug_attr_init(priv);
	if (err) {
		dev_err(&pdev->dev, "Failed to allocate attributes: %d\n",
			err);
		return err;
	}

	priv->hwmon = devm_hwmon_device_register_with_groups(&pdev->dev,
					"mlxreg_hotplug", priv, priv->groups);
	if (IS_ERR(priv->hwmon)) {
		dev_err(&pdev->dev, "Failed to register hwmon device %ld\n",
			PTR_ERR(priv->hwmon));
		return PTR_ERR(priv->hwmon);
	}

	/* Perform initial interrupts setup. */
	mlxreg_hotplug_set_irq(priv);
	priv->after_probe = true;

	return 0;
}

static int mlxreg_hotplug_remove(struct platform_device *pdev)
{
	struct mlxreg_hotplug_priv_data *priv = dev_get_drvdata(&pdev->dev);

	/* Clean interrupts setup. */
	mlxreg_hotplug_unset_irq(priv);
	devm_free_irq(&pdev->dev, priv->irq, priv);

	return 0;
}

static struct platform_driver mlxreg_hotplug_driver = {
	.driver = {
		.name = "mlxreg-hotplug",
	},
	.probe = mlxreg_hotplug_probe,
	.remove = mlxreg_hotplug_remove,
};

module_platform_driver(mlxreg_hotplug_driver);

MODULE_AUTHOR("Vadim Pasternak <vadimp@mellanox.com>");
MODULE_DESCRIPTION("Mellanox regmap hotplug platform driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:mlxreg-hotplug");
