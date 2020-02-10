/*
 * I2C multiplexer
 *
 * Copyright (c) 2008-2009 Rodolfo Giometti <giometti@linux.it>
 * Copyright (c) 2008-2009 Eurotech S.p.A. <info@eurotech.it>
 *
 * This module supports the PCA954x and PCA954x series of I2C multiplexer/switch
 * chips made by NXP Semiconductors.
 * This includes the:
 *	 PCA9540, PCA9542, PCA9543, PCA9544, PCA9545, PCA9546, PCA9547,
 *	 PCA9548, PCA9846, PCA9847, PCA9848 and PCA9849.
 *
 * These chips are all controlled via the I2C bus itself, and all have a
 * single 8-bit register. The upstream "parent" bus fans out to two,
 * four, or eight downstream busses or channels; which of these
 * are selected is determined by the chip type and register contents. A
 * mux can select only one sub-bus at a time; a switch can select any
 * combination simultaneously.
 *
 * Based on:
 *	pca954x.c from Kumar Gala <galak@kernel.crashing.org>
 * Copyright (C) 2006
 *
 * Based on:
 *	pca954x.c from Ken Harrenstien
 * Copyright (C) 2004 Google, Inc. (Ken Harrenstien)
 *
 * Based on:
 *	i2c-virtual_cb.c from Brian Kuschak <bkuschak@yahoo.com>
 * and
 *	pca9540.c from Jean Delvare <jdelvare@suse.de>.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <dt-bindings/mux/mux.h>

#define PCA954X_MAX_NCHANS 8

#define PCA954X_IRQ_OFFSET 4

enum pca_type {
	pca_9540,
	pca_9542,
	pca_9543,
	pca_9544,
	pca_9545,
	pca_9546,
	pca_9547,
	pca_9548,
	pca_9846,
	pca_9847,
	pca_9848,
	pca_9849,
};

struct chip_desc {
	u8 nchans;
	u8 enable;	/* used for muxes only */
	u8 has_irq;
	enum muxtype {
		pca954x_ismux = 0,
		pca954x_isswi
	} muxtype;
	struct i2c_device_identity id;
};

struct pca954x {
	const struct chip_desc *chip;

	u8 last_chan;		/* last register value */
	/* MUX_IDLE_AS_IS, MUX_IDLE_DISCONNECT or >= 0 for channel */
	s32 idle_state;

	struct i2c_client *client;

	struct irq_domain *irq;
	unsigned int irq_mask;
	raw_spinlock_t lock;
};

/* Provide specs for the PCA954x types we know about */
static const struct chip_desc chips[] = {
	[pca_9540] = {
		.nchans = 2,
		.enable = 0x4,
		.muxtype = pca954x_ismux,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9542] = {
		.nchans = 2,
		.enable = 0x4,
		.has_irq = 1,
		.muxtype = pca954x_ismux,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9543] = {
		.nchans = 2,
		.has_irq = 1,
		.muxtype = pca954x_isswi,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9544] = {
		.nchans = 4,
		.enable = 0x4,
		.has_irq = 1,
		.muxtype = pca954x_ismux,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9545] = {
		.nchans = 4,
		.has_irq = 1,
		.muxtype = pca954x_isswi,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9546] = {
		.nchans = 4,
		.muxtype = pca954x_isswi,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9547] = {
		.nchans = 8,
		.enable = 0x8,
		.muxtype = pca954x_ismux,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9548] = {
		.nchans = 8,
		.muxtype = pca954x_isswi,
		.id = { .manufacturer_id = I2C_DEVICE_ID_NONE },
	},
	[pca_9846] = {
		.nchans = 4,
		.muxtype = pca954x_isswi,
		.id = {
			.manufacturer_id = I2C_DEVICE_ID_NXP_SEMICONDUCTORS,
			.part_id = 0x10b,
		},
	},
	[pca_9847] = {
		.nchans = 8,
		.enable = 0x8,
		.muxtype = pca954x_ismux,
		.id = {
			.manufacturer_id = I2C_DEVICE_ID_NXP_SEMICONDUCTORS,
			.part_id = 0x108,
		},
	},
	[pca_9848] = {
		.nchans = 8,
		.muxtype = pca954x_isswi,
		.id = {
			.manufacturer_id = I2C_DEVICE_ID_NXP_SEMICONDUCTORS,
			.part_id = 0x10a,
		},
	},
	[pca_9849] = {
		.nchans = 4,
		.enable = 0x4,
		.muxtype = pca954x_ismux,
		.id = {
			.manufacturer_id = I2C_DEVICE_ID_NXP_SEMICONDUCTORS,
			.part_id = 0x109,
		},
	},
};

static const struct i2c_device_id pca954x_id[] = {
	{ "pca9540", pca_9540 },
	{ "pca9542", pca_9542 },
	{ "pca9543", pca_9543 },
	{ "pca9544", pca_9544 },
	{ "pca9545", pca_9545 },
	{ "pca9546", pca_9546 },
	{ "pca9547", pca_9547 },
	{ "pca9548", pca_9548 },
	{ "pca9846", pca_9846 },
	{ "pca9847", pca_9847 },
	{ "pca9848", pca_9848 },
	{ "pca9849", pca_9849 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pca954x_id);

#ifdef CONFIG_OF
static const struct of_device_id pca954x_of_match[] = {
	{ .compatible = "nxp,pca9540", .data = &chips[pca_9540] },
	{ .compatible = "nxp,pca9542", .data = &chips[pca_9542] },
	{ .compatible = "nxp,pca9543", .data = &chips[pca_9543] },
	{ .compatible = "nxp,pca9544", .data = &chips[pca_9544] },
	{ .compatible = "nxp,pca9545", .data = &chips[pca_9545] },
	{ .compatible = "nxp,pca9546", .data = &chips[pca_9546] },
	{ .compatible = "nxp,pca9547", .data = &chips[pca_9547] },
	{ .compatible = "nxp,pca9548", .data = &chips[pca_9548] },
	{ .compatible = "nxp,pca9846", .data = &chips[pca_9846] },
	{ .compatible = "nxp,pca9847", .data = &chips[pca_9847] },
	{ .compatible = "nxp,pca9848", .data = &chips[pca_9848] },
	{ .compatible = "nxp,pca9849", .data = &chips[pca_9849] },
	{}
};
MODULE_DEVICE_TABLE(of, pca954x_of_match);
#endif

/* Write to mux register. Don't use i2c_transfer()/i2c_smbus_xfer()
   for this as they will try to lock adapter a second time */
static int pca954x_reg_write(struct i2c_adapter *adap,
			     struct i2c_client *client, u8 val)
{
	union i2c_smbus_data dummy;

	return __i2c_smbus_xfer(adap, client->addr, client->flags,
				I2C_SMBUS_WRITE, val,
				I2C_SMBUS_BYTE, &dummy);
}

static u8 pca954x_regval(struct pca954x *data, u8 chan)
{
	/* We make switches look like muxes, not sure how to be smarter. */
	if (data->chip->muxtype == pca954x_ismux)
		return chan | data->chip->enable;
	else
		return 1 << chan;
}

static int pca954x_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	struct pca954x *data = i2c_mux_priv(muxc);
	struct i2c_client *client = data->client;
	u8 regval;
	int ret = 0;

	regval = pca954x_regval(data, chan);
	/* Only select the channel if its different from the last channel */
	if (data->last_chan != regval) {
		ret = pca954x_reg_write(muxc->parent, client, regval);
		data->last_chan = ret < 0 ? 0 : regval;
	}

	return ret;
}

static int pca954x_deselect_mux(struct i2c_mux_core *muxc, u32 chan)
{
	struct pca954x *data = i2c_mux_priv(muxc);
	struct i2c_client *client = data->client;
	s32 idle_state;

	idle_state = READ_ONCE(data->idle_state);
	if (idle_state >= 0)
		/* Set the mux back to a predetermined channel */
		return pca954x_select_chan(muxc, idle_state);

	if (idle_state == MUX_IDLE_DISCONNECT) {
		/* Deselect active channel */
		data->last_chan = 0;
		return pca954x_reg_write(muxc->parent, client,
					 data->last_chan);
	}

	/* otherwise leave as-is */

	return 0;
}

static ssize_t idle_state_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);
	struct pca954x *data = i2c_mux_priv(muxc);

	return sprintf(buf, "%d\n", READ_ONCE(data->idle_state));
}

static ssize_t idle_state_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);
	struct pca954x *data = i2c_mux_priv(muxc);
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (val != MUX_IDLE_AS_IS && val != MUX_IDLE_DISCONNECT &&
	    (val < 0 || val >= data->chip->nchans))
		return -EINVAL;

	i2c_lock_bus(muxc->parent, I2C_LOCK_SEGMENT);

	WRITE_ONCE(data->idle_state, val);
	/*
	 * Set the mux into a state consistent with the new
	 * idle_state.
	 */
	if (data->last_chan || val != MUX_IDLE_DISCONNECT)
		ret = pca954x_deselect_mux(muxc, 0);

	i2c_unlock_bus(muxc->parent, I2C_LOCK_SEGMENT);

	return ret < 0 ? ret : count;
}

static DEVICE_ATTR_RW(idle_state);

static irqreturn_t pca954x_irq_handler(int irq, void *dev_id)
{
	struct pca954x *data = dev_id;
	unsigned int child_irq;
	int ret, i, handled = 0;

	ret = i2c_smbus_read_byte(data->client);
	if (ret < 0)
		return IRQ_NONE;

	for (i = 0; i < data->chip->nchans; i++) {
		if (ret & BIT(PCA954X_IRQ_OFFSET + i)) {
			child_irq = irq_linear_revmap(data->irq, i);
			handle_nested_irq(child_irq);
			handled++;
		}
	}
	return handled ? IRQ_HANDLED : IRQ_NONE;
}

static int pca954x_irq_set_type(struct irq_data *idata, unsigned int type)
{
	if ((type & IRQ_TYPE_SENSE_MASK) != IRQ_TYPE_LEVEL_LOW)
		return -EINVAL;
	return 0;
}

static struct irq_chip pca954x_irq_chip = {
	.name = "i2c-mux-pca954x",
	.irq_set_type = pca954x_irq_set_type,
};

static int pca954x_irq_setup(struct i2c_mux_core *muxc)
{
	struct pca954x *data = i2c_mux_priv(muxc);
	struct i2c_client *client = data->client;
	int c, irq;

	if (!data->chip->has_irq || client->irq <= 0)
		return 0;

	raw_spin_lock_init(&data->lock);

	data->irq = irq_domain_add_linear(client->dev.of_node,
					  data->chip->nchans,
					  &irq_domain_simple_ops, data);
	if (!data->irq)
		return -ENODEV;

	for (c = 0; c < data->chip->nchans; c++) {
		irq = irq_create_mapping(data->irq, c);
		if (!irq) {
			dev_err(&client->dev, "failed irq create map\n");
			return -EINVAL;
		}
		irq_set_chip_data(irq, data);
		irq_set_chip_and_handler(irq, &pca954x_irq_chip,
			handle_simple_irq);
	}

	return 0;
}

static void pca954x_cleanup(struct i2c_mux_core *muxc)
{
	struct pca954x *data = i2c_mux_priv(muxc);
	struct i2c_client *client = data->client;
	int c, irq;

	device_remove_file(&client->dev, &dev_attr_idle_state);

	if (data->irq) {
		for (c = 0; c < data->chip->nchans; c++) {
			irq = irq_find_mapping(data->irq, c);
			irq_dispose_mapping(irq);
		}
		irq_domain_remove(data->irq);
	}
	i2c_mux_del_adapters(muxc);
}

static int pca954x_init(struct i2c_client *client, struct pca954x *data)
{
	int ret;

	if (data->idle_state >= 0)
		data->last_chan = pca954x_regval(data, data->idle_state);
	else
		data->last_chan = 0; /* Disconnect multiplexer */

	ret = i2c_smbus_write_byte(client, data->last_chan);
	if (ret < 0)
		data->last_chan = 0;

	return ret;
}

/*
 * I2C init/probing/exit functions
 */
static int pca954x_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = client->adapter;
	struct device *dev = &client->dev;
	struct device_node *np = dev->of_node;
	struct gpio_desc *gpio;
	struct i2c_mux_core *muxc;
	struct pca954x *data;
	int num;
	int ret;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE))
		return -ENODEV;

	muxc = i2c_mux_alloc(adap, dev, PCA954X_MAX_NCHANS, sizeof(*data), 0,
			     pca954x_select_chan, pca954x_deselect_mux);
	if (!muxc)
		return -ENOMEM;
	data = i2c_mux_priv(muxc);

	i2c_set_clientdata(client, muxc);
	data->client = client;

	/* Reset the mux if a reset GPIO is specified. */
	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);
	if (gpio) {
		udelay(1);
		gpiod_set_value_cansleep(gpio, 0);
		/* Give the chip some time to recover. */
		udelay(1);
	}

	data->chip = of_device_get_match_data(dev);
	if (!data->chip)
		data->chip = &chips[id->driver_data];

	if (data->chip->id.manufacturer_id != I2C_DEVICE_ID_NONE) {
		struct i2c_device_identity id;

		ret = i2c_get_device_id(client, &id);
		if (ret && ret != -EOPNOTSUPP)
			return ret;

		if (!ret &&
		    (id.manufacturer_id != data->chip->id.manufacturer_id ||
		     id.part_id != data->chip->id.part_id)) {
			dev_warn(dev, "unexpected device id %03x-%03x-%x\n",
				 id.manufacturer_id, id.part_id,
				 id.die_revision);
			return -ENODEV;
		}
	}

	data->idle_state = MUX_IDLE_AS_IS;
	if (of_property_read_u32(np, "idle-state", &data->idle_state)) {
		if (np && of_property_read_bool(np, "i2c-mux-idle-disconnect"))
			data->idle_state = MUX_IDLE_DISCONNECT;
	}

	/*
	 * Write the mux register at addr to verify
	 * that the mux is in fact present. This also
	 * initializes the mux to a channel
	 * or disconnected state.
	 */
	ret = pca954x_init(client, data);
	if (ret < 0) {
		dev_warn(dev, "probe failed\n");
		return -ENODEV;
	}

	ret = pca954x_irq_setup(muxc);
	if (ret)
		goto fail_cleanup;

	/* Now create an adapter for each channel */
	for (num = 0; num < data->chip->nchans; num++) {
		ret = i2c_mux_add_adapter(muxc, 0, num, 0);
		if (ret)
			goto fail_cleanup;
	}

	if (data->irq) {
		ret = devm_request_threaded_irq(dev, data->client->irq,
						NULL, pca954x_irq_handler,
						IRQF_ONESHOT | IRQF_SHARED,
						"pca954x", data);
		if (ret)
			goto fail_cleanup;
	}

	/*
	 * The attr probably isn't going to be needed in most cases,
	 * so don't fail completely on error.
	 */
	device_create_file(dev, &dev_attr_idle_state);

	dev_info(dev, "registered %d multiplexed busses for I2C %s %s\n",
		 num, data->chip->muxtype == pca954x_ismux
				? "mux" : "switch", client->name);

	return 0;

fail_cleanup:
	pca954x_cleanup(muxc);
	return ret;
}

static int pca954x_remove(struct i2c_client *client)
{
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);

	pca954x_cleanup(muxc);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pca954x_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_mux_core *muxc = i2c_get_clientdata(client);
	struct pca954x *data = i2c_mux_priv(muxc);
	int ret;

	ret = pca954x_init(client, data);
	if (ret < 0)
		dev_err(&client->dev, "failed to verify mux presence\n");

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(pca954x_pm, NULL, pca954x_resume);

static struct i2c_driver pca954x_driver = {
	.driver		= {
		.name	= "pca954x",
		.pm	= &pca954x_pm,
		.of_match_table = of_match_ptr(pca954x_of_match),
	},
	.probe		= pca954x_probe,
	.remove		= pca954x_remove,
	.id_table	= pca954x_id,
};

module_i2c_driver(pca954x_driver);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("PCA954x I2C mux/switch driver");
MODULE_LICENSE("GPL v2");
