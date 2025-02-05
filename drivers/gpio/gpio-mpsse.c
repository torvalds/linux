// SPDX-License-Identifier: GPL-2.0-only
/*
 * FTDI MPSSE GPIO support
 *
 * Based on code by Anatolij Gustschin
 *
 * Copyright (C) 2024 Mary Strodl <mstrodl@csh.rit.edu>
 */

#include <linux/cleanup.h>
#include <linux/gpio/driver.h>
#include <linux/mutex.h>
#include <linux/usb.h>

struct mpsse_priv {
	struct gpio_chip gpio;
	struct usb_device *udev;     /* USB device encompassing all MPSSEs */
	struct usb_interface *intf;  /* USB interface for this MPSSE */
	u8 intf_id;                  /* USB interface number for this MPSSE */
	struct work_struct irq_work; /* polling work thread */
	struct mutex irq_mutex;	     /* lock over irq_data */
	atomic_t irq_type[16];	     /* pin -> edge detection type */
	atomic_t irq_enabled;
	int id;

	u8 gpio_outputs[2];	     /* Output states for GPIOs [L, H] */
	u8 gpio_dir[2];		     /* Directions for GPIOs [L, H] */

	u8 *bulk_in_buf;	     /* Extra recv buffer to grab status bytes */

	struct usb_endpoint_descriptor *bulk_in;
	struct usb_endpoint_descriptor *bulk_out;

	struct mutex io_mutex;	    /* sync I/O with disconnect */
};

struct bulk_desc {
	bool tx;	            /* direction of bulk transfer */
	u8 *data;                   /* input (tx) or output (rx) */
	int len;                    /* Length of `data` if tx, or length of */
				    /* Data to read if rx */
	int len_actual;		    /* Length successfully transferred */
	int timeout;
};

static const struct usb_device_id gpio_mpsse_table[] = {
	{ USB_DEVICE(0x0c52, 0xa064) },   /* SeaLevel Systems, Inc. */
	{ }                               /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, gpio_mpsse_table);

static DEFINE_IDA(gpio_mpsse_ida);

/* MPSSE commands */
#define SET_BITS_CMD 0x80
#define GET_BITS_CMD 0x81

#define SET_BITMODE_REQUEST 0x0B
#define MODE_MPSSE (2 << 8)
#define MODE_RESET 0

/* Arbitrarily decided. This could probably be much less */
#define MPSSE_WRITE_TIMEOUT 5000
#define MPSSE_READ_TIMEOUT 5000

/* 1 millisecond, also pretty arbitrary */
#define MPSSE_POLL_INTERVAL 1000

static int mpsse_bulk_xfer(struct usb_interface *intf, struct bulk_desc *desc)
{
	struct mpsse_priv *priv = usb_get_intfdata(intf);
	struct usb_device *udev = priv->udev;
	unsigned int pipe;
	int ret;

	if (desc->tx)
		pipe = usb_sndbulkpipe(udev, priv->bulk_out->bEndpointAddress);
	else
		pipe = usb_rcvbulkpipe(udev, priv->bulk_in->bEndpointAddress);

	ret = usb_bulk_msg(udev, pipe, desc->data, desc->len,
			   &desc->len_actual, desc->timeout);
	if (ret)
		dev_dbg(&udev->dev, "mpsse: bulk transfer failed: %d\n", ret);

	return ret;
}

static int mpsse_write(struct usb_interface *intf,
		       u8 *buf, size_t len)
{
	int ret;
	struct bulk_desc desc;

	desc.len_actual = 0;
	desc.tx = true;
	desc.data = buf;
	desc.len = len;
	desc.timeout = MPSSE_WRITE_TIMEOUT;

	ret = mpsse_bulk_xfer(intf, &desc);

	return ret;
}

static int mpsse_read(struct usb_interface *intf, u8 *buf, size_t len)
{
	int ret;
	struct bulk_desc desc;
	struct mpsse_priv *priv = usb_get_intfdata(intf);

	desc.len_actual = 0;
	desc.tx = false;
	desc.data = priv->bulk_in_buf;
	/* Device sends 2 additional status bytes, read len + 2 */
	desc.len = min_t(size_t, len + 2, usb_endpoint_maxp(priv->bulk_in));
	desc.timeout = MPSSE_READ_TIMEOUT;

	ret = mpsse_bulk_xfer(intf, &desc);
	if (ret)
		return ret;

	/* Did we get enough data? */
	if (desc.len_actual < desc.len)
		return -EIO;

	memcpy(buf, desc.data + 2, desc.len_actual - 2);

	return ret;
}

static int gpio_mpsse_set_bank(struct mpsse_priv *priv, u8 bank)
{
	int ret;
	u8 tx_buf[3] = {
		SET_BITS_CMD | (bank << 1),
		priv->gpio_outputs[bank],
		priv->gpio_dir[bank],
	};

	ret = mpsse_write(priv->intf, tx_buf, 3);

	return ret;
}

static int gpio_mpsse_get_bank(struct mpsse_priv *priv, u8 bank)
{
	int ret;
	u8 buf = GET_BITS_CMD | (bank << 1);

	ret = mpsse_write(priv->intf, &buf, 1);
	if (ret)
		return ret;

	ret = mpsse_read(priv->intf, &buf, 1);
	if (ret)
		return ret;

	return buf;
}

static void gpio_mpsse_set_multiple(struct gpio_chip *chip, unsigned long *mask,
				    unsigned long *bits)
{
	unsigned long i, bank, bank_mask, bank_bits;
	int ret;
	struct mpsse_priv *priv = gpiochip_get_data(chip);

	guard(mutex)(&priv->io_mutex);
	for_each_set_clump8(i, bank_mask, mask, chip->ngpio) {
		bank = i / 8;

		if (bank_mask) {
			bank_bits = bitmap_get_value8(bits, i);
			/* Zero out pins we want to change */
			priv->gpio_outputs[bank] &= ~bank_mask;
			/* Set pins we care about */
			priv->gpio_outputs[bank] |= bank_bits & bank_mask;

			ret = gpio_mpsse_set_bank(priv, bank);
			if (ret)
				dev_err(&priv->intf->dev,
					"Couldn't set values for bank %ld!",
					bank);
		}
	}
}

static int gpio_mpsse_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				   unsigned long *bits)
{
	unsigned long i, bank, bank_mask;
	int ret;
	struct mpsse_priv *priv = gpiochip_get_data(chip);

	guard(mutex)(&priv->io_mutex);
	for_each_set_clump8(i, bank_mask, mask, chip->ngpio) {
		bank = i / 8;

		if (bank_mask) {
			ret = gpio_mpsse_get_bank(priv, bank);
			if (ret < 0)
				return ret;

			bitmap_set_value8(bits, ret & bank_mask, i);
		}
	}

	return 0;
}

static int gpio_mpsse_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	int err;
	unsigned long mask = 0, bits = 0;

	__set_bit(offset, &mask);
	err = gpio_mpsse_get_multiple(chip, &mask, &bits);
	if (err)
		return err;

	/* == is not guaranteed to give 1 if true */
	if (bits)
		return 1;
	else
		return 0;
}

static void gpio_mpsse_gpio_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	unsigned long mask = 0, bits = 0;

	__set_bit(offset, &mask);
	if (value)
		__set_bit(offset, &bits);

	gpio_mpsse_set_multiple(chip, &mask, &bits);
}

static int gpio_mpsse_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct mpsse_priv *priv = gpiochip_get_data(chip);
	int bank = (offset & 8) >> 3;
	int bank_offset = offset & 7;

	scoped_guard(mutex, &priv->io_mutex)
		priv->gpio_dir[bank] |= BIT(bank_offset);

	gpio_mpsse_gpio_set(chip, offset, value);

	return 0;
}

static int gpio_mpsse_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct mpsse_priv *priv = gpiochip_get_data(chip);
	int bank = (offset & 8) >> 3;
	int bank_offset = offset & 7;

	guard(mutex)(&priv->io_mutex);
	priv->gpio_dir[bank] &= ~BIT(bank_offset);
	gpio_mpsse_set_bank(priv, bank);

	return 0;
}

static int gpio_mpsse_get_direction(struct gpio_chip *chip,
				    unsigned int offset)
{
	int ret;
	int bank = (offset & 8) >> 3;
	int bank_offset = offset & 7;
	struct mpsse_priv *priv = gpiochip_get_data(chip);

	guard(mutex)(&priv->io_mutex);
	/* MPSSE directions are inverted */
	if (priv->gpio_dir[bank] & BIT(bank_offset))
		ret = GPIO_LINE_DIRECTION_OUT;
	else
		ret = GPIO_LINE_DIRECTION_IN;

	return ret;
}

static void gpio_mpsse_poll(struct work_struct *work)
{
	unsigned long pin_mask, pin_states, flags;
	int irq_enabled, offset, err, value, fire_irq,
		irq, old_value[16], irq_type[16];
	struct mpsse_priv *priv = container_of(work, struct mpsse_priv,
					       irq_work);

	for (offset = 0; offset < priv->gpio.ngpio; ++offset)
		old_value[offset] = -1;

	while ((irq_enabled = atomic_read(&priv->irq_enabled))) {
		usleep_range(MPSSE_POLL_INTERVAL, MPSSE_POLL_INTERVAL + 1000);
		/* Cleanup will trigger at the end of the loop */
		guard(mutex)(&priv->irq_mutex);

		pin_mask = 0;
		pin_states = 0;
		for (offset = 0; offset < priv->gpio.ngpio; ++offset) {
			irq_type[offset] = atomic_read(&priv->irq_type[offset]);
			if (irq_type[offset] != IRQ_TYPE_NONE &&
			    irq_enabled & BIT(offset))
				pin_mask |= BIT(offset);
			else
				old_value[offset] = -1;
		}

		err = gpio_mpsse_get_multiple(&priv->gpio, &pin_mask,
					      &pin_states);
		if (err) {
			dev_err_ratelimited(&priv->intf->dev,
					    "Error polling!\n");
			continue;
		}

		/* Check each value */
		for (offset = 0; offset < priv->gpio.ngpio; ++offset) {
			if (old_value[offset] == -1)
				continue;

			fire_irq = 0;
			value = pin_states & BIT(offset);

			switch (irq_type[offset]) {
			case IRQ_TYPE_EDGE_RISING:
				fire_irq = value > old_value[offset];
				break;
			case IRQ_TYPE_EDGE_FALLING:
				fire_irq = value < old_value[offset];
				break;
			case IRQ_TYPE_EDGE_BOTH:
				fire_irq = value != old_value[offset];
				break;
			}
			if (!fire_irq)
				continue;

			irq = irq_find_mapping(priv->gpio.irq.domain,
					       offset);
			local_irq_save(flags);
			generic_handle_irq(irq);
			local_irq_disable();
			local_irq_restore(flags);
		}

		/* Sync back values so we can refer to them next tick */
		for (offset = 0; offset < priv->gpio.ngpio; ++offset)
			if (irq_type[offset] != IRQ_TYPE_NONE &&
			    irq_enabled & BIT(offset))
				old_value[offset] = pin_states & BIT(offset);
	}
}

static int gpio_mpsse_set_irq_type(struct irq_data *irqd, unsigned int type)
{
	int offset;
	struct mpsse_priv *priv = irq_data_get_irq_chip_data(irqd);

	offset = irqd->hwirq;
	atomic_set(&priv->irq_type[offset], type & IRQ_TYPE_EDGE_BOTH);

	return 0;
}

static void gpio_mpsse_irq_disable(struct irq_data *irqd)
{
	struct mpsse_priv *priv = irq_data_get_irq_chip_data(irqd);

	atomic_and(~BIT(irqd->hwirq), &priv->irq_enabled);
	gpiochip_disable_irq(&priv->gpio, irqd->hwirq);
}

static void gpio_mpsse_irq_enable(struct irq_data *irqd)
{
	struct mpsse_priv *priv = irq_data_get_irq_chip_data(irqd);

	gpiochip_enable_irq(&priv->gpio, irqd->hwirq);
	/* If no-one else was using the IRQ, enable it */
	if (!atomic_fetch_or(BIT(irqd->hwirq), &priv->irq_enabled)) {
		INIT_WORK(&priv->irq_work, gpio_mpsse_poll);
		schedule_work(&priv->irq_work);
	}
}

static const struct irq_chip gpio_mpsse_irq_chip = {
	.name = "gpio-mpsse-irq",
	.irq_enable = gpio_mpsse_irq_enable,
	.irq_disable = gpio_mpsse_irq_disable,
	.irq_set_type = gpio_mpsse_set_irq_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void gpio_mpsse_ida_remove(void *data)
{
	struct mpsse_priv *priv = data;

	ida_free(&gpio_mpsse_ida, priv->id);
}

static int gpio_mpsse_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct mpsse_priv *priv;
	struct device *dev;
	int err;

	dev = &interface->dev;
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->udev = usb_get_dev(interface_to_usbdev(interface));
	priv->intf = interface;
	priv->intf_id = interface->cur_altsetting->desc.bInterfaceNumber;

	priv->id = ida_alloc(&gpio_mpsse_ida, GFP_KERNEL);
	if (priv->id < 0)
		return priv->id;

	err = devm_add_action_or_reset(dev, gpio_mpsse_ida_remove, priv);
	if (err)
		return err;

	err = devm_mutex_init(dev, &priv->io_mutex);
	if (err)
		return err;

	err = devm_mutex_init(dev, &priv->irq_mutex);
	if (err)
		return err;

	priv->gpio.label = devm_kasprintf(dev, GFP_KERNEL,
					  "gpio-mpsse.%d.%d",
					  priv->id, priv->intf_id);
	if (!priv->gpio.label)
		return -ENOMEM;

	priv->gpio.owner = THIS_MODULE;
	priv->gpio.parent = interface->usb_dev;
	priv->gpio.get_direction = gpio_mpsse_get_direction;
	priv->gpio.direction_input = gpio_mpsse_direction_input;
	priv->gpio.direction_output = gpio_mpsse_direction_output;
	priv->gpio.get = gpio_mpsse_gpio_get;
	priv->gpio.set = gpio_mpsse_gpio_set;
	priv->gpio.get_multiple = gpio_mpsse_get_multiple;
	priv->gpio.set_multiple = gpio_mpsse_set_multiple;
	priv->gpio.base = -1;
	priv->gpio.ngpio = 16;
	priv->gpio.offset = priv->intf_id * priv->gpio.ngpio;
	priv->gpio.can_sleep = 1;

	err = usb_find_common_endpoints(interface->cur_altsetting,
					&priv->bulk_in, &priv->bulk_out,
					NULL, NULL);
	if (err)
		return err;

	priv->bulk_in_buf = devm_kmalloc(dev, usb_endpoint_maxp(priv->bulk_in),
					 GFP_KERNEL);
	if (!priv->bulk_in_buf)
		return -ENOMEM;

	usb_set_intfdata(interface, priv);

	/* Reset mode, needed to correctly enter MPSSE mode */
	err = usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			      SET_BITMODE_REQUEST,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      MODE_RESET, priv->intf_id + 1, NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (err)
		return err;

	/* Enter MPSSE mode */
	err = usb_control_msg(priv->udev, usb_sndctrlpipe(priv->udev, 0),
			      SET_BITMODE_REQUEST,
			      USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			      MODE_MPSSE, priv->intf_id + 1, NULL, 0,
			      USB_CTRL_SET_TIMEOUT);
	if (err)
		return err;

	gpio_irq_chip_set_chip(&priv->gpio.irq, &gpio_mpsse_irq_chip);

	priv->gpio.irq.parent_handler = NULL;
	priv->gpio.irq.num_parents = 0;
	priv->gpio.irq.parents = NULL;
	priv->gpio.irq.default_type = IRQ_TYPE_NONE;
	priv->gpio.irq.handler = handle_simple_irq;

	err = devm_gpiochip_add_data(dev, &priv->gpio, priv);
	if (err)
		return err;

	return 0;
}

static void gpio_mpsse_disconnect(struct usb_interface *intf)
{
	struct mpsse_priv *priv = usb_get_intfdata(intf);

	priv->intf = NULL;
	usb_set_intfdata(intf, NULL);
	usb_put_dev(priv->udev);
}

static struct usb_driver gpio_mpsse_driver = {
	.name           = "gpio-mpsse",
	.probe          = gpio_mpsse_probe,
	.disconnect     = gpio_mpsse_disconnect,
	.id_table       = gpio_mpsse_table,
};

module_usb_driver(gpio_mpsse_driver);

MODULE_AUTHOR("Mary Strodl <mstrodl@csh.rit.edu>");
MODULE_DESCRIPTION("MPSSE GPIO driver");
MODULE_LICENSE("GPL");
