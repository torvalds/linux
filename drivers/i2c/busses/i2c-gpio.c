// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bitbanging I2C bus driver using the GPIO API
 *
 * Copyright (C) 2007 Atmel Corporation
 */
#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_data/i2c-gpio.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

static LIST_HEAD(i2c_gpio_scl_list);
static DEFINE_MUTEX(i2c_gpio_scl_list_lock);
static struct lock_class_key i2c_gpio_scl_lock_key;

struct i2c_gpio_scl_data {
	struct fwnode_reference_args args;
	struct gpio_desc *gpio;
	struct rt_mutex lock;
	struct list_head list;
	refcount_t ref;
};

struct i2c_gpio_private_data {
	struct gpio_desc *sda;
	struct i2c_gpio_scl_data *scl;
	struct i2c_adapter adap;
	struct i2c_algo_bit_data bit_data;
	struct i2c_gpio_platform_data pdata;
#ifdef CONFIG_I2C_GPIO_FAULT_INJECTOR
	/* these must be protected by bus lock */
	struct completion scl_irq_completion;
	u64 scl_irq_data;
#endif
};

static inline struct i2c_gpio_private_data *adap_to_priv(struct i2c_adapter *adap)
{
	return container_of(adap, struct i2c_gpio_private_data, adap);
}

/*
 * Toggle SDA by changing the output value of the pin. This is only
 * valid for pins configured as open drain (i.e. setting the value
 * high effectively turns off the output driver.)
 */
static void i2c_gpio_setsda_val(void *data, int state)
{
	struct i2c_gpio_private_data *priv = data;

	gpiod_set_value_cansleep(priv->sda, state);
}

/*
 * Toggle SCL by changing the output value of the pin. This is used
 * for pins that are configured as open drain and for output-only
 * pins. The latter case will break the i2c protocol, but it will
 * often work in practice.
 */
static void i2c_gpio_setscl_val(void *data, int state)
{
	struct i2c_gpio_private_data *priv = data;

	gpiod_set_value_cansleep(priv->scl->gpio, state);
}

static int i2c_gpio_getsda(void *data)
{
	struct i2c_gpio_private_data *priv = data;

	return gpiod_get_value_cansleep(priv->sda);
}

static int i2c_gpio_getscl(void *data)
{
	struct i2c_gpio_private_data *priv = data;

	return gpiod_get_value_cansleep(priv->scl->gpio);
}

static void i2c_gpio_lock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	/* Take care about adapter lock. See i2c_adapter_lock_bus() and others. */
	rt_mutex_lock_nested(&adap->bus_lock, i2c_adapter_depth(adap));
	rt_mutex_lock(&adap_to_priv(adap)->scl->lock);
}

static int i2c_gpio_trylock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	if (!rt_mutex_trylock(&adap->bus_lock))
		return 0;

	if (!rt_mutex_trylock(&adap_to_priv(adap)->scl->lock)) {
		rt_mutex_unlock(&adap->bus_lock);
		return 0;
	}

	return 1;
}

static void i2c_gpio_unlock_bus(struct i2c_adapter *adap, unsigned int flags)
{
	rt_mutex_unlock(&adap_to_priv(adap)->scl->lock);
	rt_mutex_unlock(&adap->bus_lock);
}

static const struct i2c_lock_operations i2c_gpio_lock_ops = {
	.lock_bus = i2c_gpio_lock_bus,
	.trylock_bus = i2c_gpio_trylock_bus,
	.unlock_bus = i2c_gpio_unlock_bus,
};

#ifdef CONFIG_I2C_GPIO_FAULT_INJECTOR

#define setsda(bd, val)	((bd)->setsda((bd)->data, val))
#define setscl(bd, val)	((bd)->setscl((bd)->data, val))
#define getsda(bd)	((bd)->getsda((bd)->data))
#define getscl(bd)	((bd)->getscl((bd)->data))

#define WIRE_ATTRIBUTE(wire) \
static int fops_##wire##_get(void *data, u64 *val)		\
{								\
	struct i2c_gpio_private_data *priv = data;		\
								\
	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);	\
	*val = get##wire(&priv->bit_data);			\
	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);	\
	return 0;						\
}								\
static int fops_##wire##_set(void *data, u64 val)		\
{								\
	struct i2c_gpio_private_data *priv = data;		\
								\
	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);	\
	set##wire(&priv->bit_data, val);			\
	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);	\
	return 0;						\
}								\
DEFINE_DEBUGFS_ATTRIBUTE(fops_##wire, fops_##wire##_get, fops_##wire##_set, "%llu\n")

WIRE_ATTRIBUTE(scl);
WIRE_ATTRIBUTE(sda);

static void i2c_gpio_incomplete_transfer(struct i2c_gpio_private_data *priv,
					u32 pattern, u8 pattern_size)
{
	struct i2c_algo_bit_data *bit_data = &priv->bit_data;
	int i;

	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);

	/* START condition */
	setsda(bit_data, 0);
	udelay(bit_data->udelay);

	/* Send pattern, request ACK, don't send STOP */
	for (i = pattern_size - 1; i >= 0; i--) {
		setscl(bit_data, 0);
		udelay(bit_data->udelay / 2);
		setsda(bit_data, (pattern >> i) & 1);
		udelay((bit_data->udelay + 1) / 2);
		setscl(bit_data, 1);
		udelay(bit_data->udelay);
	}

	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);
}

static int fops_incomplete_addr_phase_set(void *data, u64 addr)
{
	struct i2c_gpio_private_data *priv = data;
	u32 pattern;

	if (addr > 0x7f)
		return -EINVAL;

	/* ADDR (7 bit) + RD (1 bit) + Client ACK, keep SDA hi (1 bit) */
	pattern = (addr << 2) | 3;

	i2c_gpio_incomplete_transfer(priv, pattern, 9);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_incomplete_addr_phase, NULL, fops_incomplete_addr_phase_set, "%llu\n");

static int fops_incomplete_write_byte_set(void *data, u64 addr)
{
	struct i2c_gpio_private_data *priv = data;
	u32 pattern;

	if (addr > 0x7f)
		return -EINVAL;

	/* ADDR (7 bit) + WR (1 bit) + Client ACK (1 bit) */
	pattern = (addr << 2) | 1;
	/* 0x00 (8 bit) + Client ACK, keep SDA hi (1 bit) */
	pattern = (pattern << 9) | 1;

	i2c_gpio_incomplete_transfer(priv, pattern, 18);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_incomplete_write_byte, NULL, fops_incomplete_write_byte_set, "%llu\n");

static int i2c_gpio_fi_act_on_scl_irq(struct i2c_gpio_private_data *priv,
				       irqreturn_t handler(int, void*))
{
	int ret, irq = gpiod_to_irq(priv->scl->gpio);

	if (irq < 0)
		return irq;

	i2c_lock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);

	ret = gpiod_direction_input(priv->scl->gpio);
	if (ret)
		goto unlock;

	reinit_completion(&priv->scl_irq_completion);

	ret = request_irq(irq, handler, IRQF_TRIGGER_FALLING,
			  "i2c_gpio_fault_injector_scl_irq", priv);
	if (ret)
		goto output;

	wait_for_completion_interruptible(&priv->scl_irq_completion);

	free_irq(irq, priv);
 output:
	ret = gpiod_direction_output(priv->scl->gpio, 1) ?: ret;
 unlock:
	i2c_unlock_bus(&priv->adap, I2C_LOCK_ROOT_ADAPTER);

	return ret;
}

static irqreturn_t lose_arbitration_irq(int irq, void *dev_id)
{
	struct i2c_gpio_private_data *priv = dev_id;

	setsda(&priv->bit_data, 0);
	udelay(priv->scl_irq_data);
	setsda(&priv->bit_data, 1);

	complete(&priv->scl_irq_completion);

	return IRQ_HANDLED;
}

static int fops_lose_arbitration_set(void *data, u64 duration)
{
	struct i2c_gpio_private_data *priv = data;

	if (duration > 100 * 1000)
		return -EINVAL;

	priv->scl_irq_data = duration;
	/*
	 * Interrupt on falling SCL. This ensures that the controller under test
	 * has really started the transfer. Interrupt on falling SDA did only
	 * exercise 'bus busy' detection on some HW but not 'arbitration lost'.
	 * Note that the interrupt latency may cause the first bits to be
	 * transmitted correctly.
	 */
	return i2c_gpio_fi_act_on_scl_irq(priv, lose_arbitration_irq);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_lose_arbitration, NULL, fops_lose_arbitration_set, "%llu\n");

static irqreturn_t inject_panic_irq(int irq, void *dev_id)
{
	struct i2c_gpio_private_data *priv = dev_id;

	udelay(priv->scl_irq_data);
	panic("I2C fault injector induced panic");

	return IRQ_HANDLED;
}

static int fops_inject_panic_set(void *data, u64 duration)
{
	struct i2c_gpio_private_data *priv = data;

	if (duration > 100 * 1000)
		return -EINVAL;

	priv->scl_irq_data = duration;
	/*
	 * Interrupt on falling SCL. This ensures that the controller under test
	 * has really started the transfer.
	 */
	return i2c_gpio_fi_act_on_scl_irq(priv, inject_panic_irq);
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_inject_panic, NULL, fops_inject_panic_set, "%llu\n");

static void i2c_gpio_fault_injector_init(struct platform_device *pdev)
{
	struct i2c_gpio_private_data *priv = platform_get_drvdata(pdev);

	init_completion(&priv->scl_irq_completion);

	debugfs_create_file_unsafe("incomplete_address_phase", 0200, priv->adap.debugfs,
				   priv, &fops_incomplete_addr_phase);
	debugfs_create_file_unsafe("incomplete_write_byte", 0200, priv->adap.debugfs,
				   priv, &fops_incomplete_write_byte);
	if (priv->bit_data.getscl) {
		debugfs_create_file_unsafe("inject_panic", 0200, priv->adap.debugfs,
					   priv, &fops_inject_panic);
		debugfs_create_file_unsafe("lose_arbitration", 0200, priv->adap.debugfs,
					   priv, &fops_lose_arbitration);
	}
	debugfs_create_file_unsafe("scl", 0600, priv->adap.debugfs, priv, &fops_scl);
	debugfs_create_file_unsafe("sda", 0600, priv->adap.debugfs, priv, &fops_sda);
}
#else
static inline void i2c_gpio_fault_injector_init(struct platform_device *pdev) {}
#endif /* CONFIG_I2C_GPIO_FAULT_INJECTOR*/

/* Get i2c-gpio properties from DT or ACPI table */
static void i2c_gpio_get_properties(struct device *dev,
				    struct i2c_gpio_platform_data *pdata)
{
	u32 reg;

	device_property_read_u32(dev, "i2c-gpio,delay-us", &pdata->udelay);

	if (!device_property_read_u32(dev, "i2c-gpio,timeout-ms", &reg))
		pdata->timeout = msecs_to_jiffies(reg);

	pdata->sda_is_open_drain =
		device_property_read_bool(dev, "i2c-gpio,sda-open-drain");
	pdata->scl_is_open_drain =
		device_property_read_bool(dev, "i2c-gpio,scl-open-drain");
	pdata->scl_is_output_only =
		device_property_read_bool(dev, "i2c-gpio,scl-output-only");
	pdata->sda_is_output_only =
		device_property_read_bool(dev, "i2c-gpio,sda-output-only");
	pdata->sda_has_no_pullup =
		device_property_read_bool(dev, "i2c-gpio,sda-has-no-pullup");
	pdata->scl_has_no_pullup =
		device_property_read_bool(dev, "i2c-gpio,scl-has-no-pullup");
}

static struct gpio_desc *i2c_gpio_get_desc(struct device *dev,
					   const char *con_id,
					   unsigned int index,
					   enum gpiod_flags gflags)
{
	struct gpio_desc *retdesc;
	int ret;

	/*
	 * Don't use resource-managed functions. SCL may be shared across adapters and has
	 * its own lifetime management. SDA uses the same path for consistency.
	 */
	retdesc = gpiod_get(dev, con_id, gflags);
	if (!IS_ERR(retdesc)) {
		dev_dbg(dev, "got GPIO from name %s\n", con_id);
		return retdesc;
	}

	retdesc = gpiod_get_index(dev, NULL, index, gflags);
	if (!IS_ERR(retdesc)) {
		dev_dbg(dev, "got GPIO from index %u\n", index);
		return retdesc;
	}

	ret = PTR_ERR(retdesc);

	/* FIXME: hack in the old code, is this really necessary? */
	if (ret == -EINVAL)
		retdesc = ERR_PTR(-EPROBE_DEFER);

	/* This happens if the GPIO driver is not yet probed, let's defer */
	if (ret == -ENOENT)
		retdesc = ERR_PTR(-EPROBE_DEFER);

	if (PTR_ERR(retdesc) != -EPROBE_DEFER)
		dev_err(dev, "error trying to get descriptor: %d\n", ret);

	return retdesc;
}

static struct i2c_gpio_scl_data *i2c_gpio_create_scl(struct device *dev)
{
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct fwnode_reference_args args;
	struct i2c_gpio_scl_data *scl;
	bool sharable = false;
	int ret;

	/*
	 * SCL gpios can be shared if they are defined in the devicetree and have at least one
	 * cell in addition to the phandle. This is usually the pin. To support complex designs
	 * with arbitrary of_xlate() mappings handle the node cells opaque.
	 */
	if (fwnode) {
		ret = fwnode_property_get_reference_args(fwnode, "scl-gpios",
							 "#gpio-cells", 0, 0, &args);
		if (ret)
			/* try the ancient way */
			ret = fwnode_property_get_reference_args(fwnode, "gpios",
								 "#gpio-cells", 0, 1, &args);

		if (!ret) {
			if (args.nargs >= 1)
				sharable = true;
			else
				fwnode_handle_put(args.fwnode);
		}
	}

	scl = kzalloc(sizeof(*scl), GFP_KERNEL);
	if (!scl) {
		if (sharable)
			fwnode_handle_put(args.fwnode);
		return ERR_PTR(-ENOMEM);
	}

	if (sharable) {
		scl->args.fwnode = args.fwnode;
		scl->args.nargs = args.nargs;
		memcpy(scl->args.args, args.args, sizeof(args.args[0]) * args.nargs);
	}

	rt_mutex_init(&scl->lock);
	lockdep_set_class(&scl->lock, &i2c_gpio_scl_lock_key);
	refcount_set(&scl->ref, 1);

	return scl;
}

static void i2c_gpio_free_scl(struct i2c_gpio_scl_data *scl)
{
	if (scl->args.fwnode)
		fwnode_handle_put(scl->args.fwnode);
	kfree(scl);
}

static bool i2c_gpio_scl_matches(struct i2c_gpio_scl_data *a, struct i2c_gpio_scl_data *b)
{
	if (!a->args.fwnode || !b->args.fwnode ||
	    a->args.fwnode != b->args.fwnode || a->args.nargs != b->args.nargs)
		return false;

	return memcmp(a->args.args, b->args.args, sizeof(a->args.args[0]) * a->args.nargs) == 0;
}

/*
 * Look up an existing or create a new shared SCL structure. Optimistic setup sequence always
 * creates and tries to add a new entry to the list. This uses minimum locking and afterwards
 * requests the GPIO without a lock held. Concurrent probes for the same SCL pin see the entry
 * and do not race into a second gpiod_get(). Until everything is setup they terminate with
 * -EPROBE_DEFER.
 */
static struct i2c_gpio_scl_data *i2c_gpio_lookup_scl(struct device *dev, enum gpiod_flags gflags)
{
	struct i2c_gpio_scl_data *scl, *new_scl;
	struct gpio_desc *gpio;

	new_scl = i2c_gpio_create_scl(dev);
	if (IS_ERR(new_scl))
		return new_scl;

	scoped_guard(mutex, &i2c_gpio_scl_list_lock) {
		list_for_each_entry(scl, &i2c_gpio_scl_list, list) {
			if (!i2c_gpio_scl_matches(scl, new_scl))
				continue;

			i2c_gpio_free_scl(new_scl);
			if (!scl->gpio)
				return ERR_PTR(-EPROBE_DEFER);

			refcount_inc(&scl->ref);
			if (scl->args.fwnode)
				dev_dbg(dev, "reusing shared SCL (%pfwP)\n", scl->args.fwnode);

			return scl;
		}
		list_add(&new_scl->list, &i2c_gpio_scl_list);
	}

	gpio = i2c_gpio_get_desc(dev, "scl", 1, gflags);
	if (IS_ERR(gpio)) {
		scoped_guard(mutex, &i2c_gpio_scl_list_lock)
			list_del(&new_scl->list);
		i2c_gpio_free_scl(new_scl);

		return ERR_CAST(gpio);
	}

	scoped_guard(mutex, &i2c_gpio_scl_list_lock)
		new_scl->gpio = gpio;

	if (new_scl->args.fwnode)
		dev_dbg(dev, "registered shared SCL (%pfwP)\n", new_scl->args.fwnode);

	return new_scl;
}

static void i2c_gpio_cleanup_scl(struct i2c_gpio_scl_data *scl)
{
	if (!refcount_dec_and_mutex_lock(&scl->ref, &i2c_gpio_scl_list_lock))
		return;

	list_del(&scl->list);
	mutex_unlock(&i2c_gpio_scl_list_lock);
	gpiod_put(scl->gpio);
	i2c_gpio_free_scl(scl);
}

static int i2c_gpio_probe(struct platform_device *pdev)
{
	struct i2c_gpio_private_data *priv;
	struct i2c_gpio_platform_data *pdata;
	struct i2c_algo_bit_data *bit_data;
	struct i2c_adapter *adap;
	struct device *dev = &pdev->dev;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	enum gpiod_flags gflags;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	adap = &priv->adap;
	bit_data = &priv->bit_data;
	pdata = &priv->pdata;

	if (fwnode) {
		i2c_gpio_get_properties(dev, pdata);
	} else {
		/*
		 * If all platform data settings are zero it is OK
		 * to not provide any platform data from the board.
		 */
		if (dev_get_platdata(dev))
			memcpy(pdata, dev_get_platdata(dev), sizeof(*pdata));
	}

	/*
	 * First get the GPIO pins; if it fails, we'll defer the probe.
	 * If the SCL/SDA lines are marked "open drain" by platform data or
	 * device tree then this means that something outside of our control is
	 * marking these lines to be handled as open drain, and we should just
	 * handle them as we handle any other output. Else we enforce open
	 * drain as this is required for an I2C bus.
	 */
	if (pdata->sda_is_open_drain || pdata->sda_has_no_pullup)
		gflags = GPIOD_OUT_HIGH;
	else
		gflags = GPIOD_OUT_HIGH_OPEN_DRAIN;
	priv->sda = i2c_gpio_get_desc(dev, "sda", 0, gflags);
	if (IS_ERR(priv->sda))
		return PTR_ERR(priv->sda);

	if (pdata->scl_is_open_drain || pdata->scl_has_no_pullup)
		gflags = GPIOD_OUT_HIGH;
	else
		gflags = GPIOD_OUT_HIGH_OPEN_DRAIN;
	priv->scl = i2c_gpio_lookup_scl(dev, gflags);
	if (IS_ERR(priv->scl)) {
		ret = PTR_ERR(priv->scl);
		goto err_cleanup_sda;
	}

	if (gpiod_cansleep(priv->sda) || gpiod_cansleep(priv->scl->gpio))
		dev_warn(dev, "Slow GPIO pins might wreak havoc into I2C/SMBus bus timing");
	else
		bit_data->can_do_atomic = true;

	bit_data->skip_bit_test = !!priv->scl->args.fwnode;
	bit_data->setsda = i2c_gpio_setsda_val;
	bit_data->setscl = i2c_gpio_setscl_val;

	if (!pdata->scl_is_output_only)
		bit_data->getscl = i2c_gpio_getscl;
	if (!pdata->sda_is_output_only)
		bit_data->getsda = i2c_gpio_getsda;

	if (pdata->udelay)
		bit_data->udelay = pdata->udelay;
	else if (pdata->scl_is_output_only)
		bit_data->udelay = 50;			/* 10 kHz */
	else
		bit_data->udelay = 5;			/* 100 kHz */

	if (pdata->timeout)
		bit_data->timeout = pdata->timeout;
	else
		bit_data->timeout = HZ / 10;		/* 100 ms */

	bit_data->data = priv;

	adap->owner = THIS_MODULE;
	if (fwnode)
		strscpy(adap->name, dev_name(dev), sizeof(adap->name));
	else
		snprintf(adap->name, sizeof(adap->name), "i2c-gpio%d", pdev->id);

	/* Always use shared SCL aware locking */
	adap->lock_ops = &i2c_gpio_lock_ops;
	adap->algo_data = bit_data;
	adap->class = I2C_CLASS_HWMON;
	adap->dev.parent = dev;
	device_set_node(&adap->dev, fwnode);

	adap->nr = pdev->id;
	ret = i2c_bit_add_numbered_bus(adap);
	if (ret)
		goto err_cleanup_scl;

	platform_set_drvdata(pdev, priv);

	/*
	 * FIXME: using global GPIO numbers is not helpful. If/when we
	 * get accessors to get the actual name of the GPIO line,
	 * from the descriptor, then provide that instead.
	 */
	dev_info(dev, "using lines %u (SDA) and %u (SCL%s)\n",
		 desc_to_gpio(priv->sda), desc_to_gpio(priv->scl->gpio),
		 pdata->scl_is_output_only
		 ? ", no clock stretching" : "");

	i2c_gpio_fault_injector_init(pdev);

	return 0;

err_cleanup_scl:
	i2c_gpio_cleanup_scl(priv->scl);
err_cleanup_sda:
	gpiod_put(priv->sda);

	return ret;
}

static void i2c_gpio_remove(struct platform_device *pdev)
{
	struct i2c_gpio_private_data *priv;
	struct i2c_adapter *adap;

	priv = platform_get_drvdata(pdev);
	adap = &priv->adap;

	i2c_del_adapter(adap);
	i2c_gpio_cleanup_scl(priv->scl);
	gpiod_put(priv->sda);
}

static const struct of_device_id i2c_gpio_dt_ids[] = {
	{ .compatible = "i2c-gpio", },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, i2c_gpio_dt_ids);

static const struct acpi_device_id i2c_gpio_acpi_match[] = {
	{ "LOON0005" }, /* LoongArch */
	{ }
};
MODULE_DEVICE_TABLE(acpi, i2c_gpio_acpi_match);

static struct platform_driver i2c_gpio_driver = {
	.driver		= {
		.name	= "i2c-gpio",
		.of_match_table	= i2c_gpio_dt_ids,
		.acpi_match_table = i2c_gpio_acpi_match,
	},
	.probe		= i2c_gpio_probe,
	.remove		= i2c_gpio_remove,
};

static int __init i2c_gpio_init(void)
{
	int ret;

	ret = platform_driver_register(&i2c_gpio_driver);
	if (ret)
		printk(KERN_ERR "i2c-gpio: probe failed: %d\n", ret);

	return ret;
}
subsys_initcall(i2c_gpio_init);

static void __exit i2c_gpio_exit(void)
{
	platform_driver_unregister(&i2c_gpio_driver);
}
module_exit(i2c_gpio_exit);

MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_DESCRIPTION("Platform-independent bitbanging I2C driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:i2c-gpio");
