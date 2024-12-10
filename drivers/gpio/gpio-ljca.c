// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel La Jolla Cove Adapter USB-GPIO driver
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/dev_printk.h>
#include <linux/gpio/driver.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kref.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb/ljca.h>

/* GPIO commands */
#define LJCA_GPIO_CONFIG		1
#define LJCA_GPIO_READ			2
#define LJCA_GPIO_WRITE			3
#define LJCA_GPIO_INT_EVENT		4
#define LJCA_GPIO_INT_MASK		5
#define LJCA_GPIO_INT_UNMASK		6

#define LJCA_GPIO_CONF_DISABLE		BIT(0)
#define LJCA_GPIO_CONF_INPUT		BIT(1)
#define LJCA_GPIO_CONF_OUTPUT		BIT(2)
#define LJCA_GPIO_CONF_PULLUP		BIT(3)
#define LJCA_GPIO_CONF_PULLDOWN		BIT(4)
#define LJCA_GPIO_CONF_DEFAULT		BIT(5)
#define LJCA_GPIO_CONF_INTERRUPT	BIT(6)
#define LJCA_GPIO_INT_TYPE		BIT(7)

#define LJCA_GPIO_CONF_EDGE		FIELD_PREP(LJCA_GPIO_INT_TYPE, 1)
#define LJCA_GPIO_CONF_LEVEL		FIELD_PREP(LJCA_GPIO_INT_TYPE, 0)

/* Intentional overlap with PULLUP / PULLDOWN */
#define LJCA_GPIO_CONF_SET		BIT(3)
#define LJCA_GPIO_CONF_CLR		BIT(4)

#define LJCA_GPIO_BUF_SIZE		60u

struct ljca_gpio_op {
	u8 index;
	u8 value;
} __packed;

struct ljca_gpio_packet {
	u8 num;
	struct ljca_gpio_op item[] __counted_by(num);
} __packed;

struct ljca_gpio_dev {
	struct ljca_client *ljca;
	struct gpio_chip gc;
	struct ljca_gpio_info *gpio_info;
	DECLARE_BITMAP(unmasked_irqs, LJCA_MAX_GPIO_NUM);
	DECLARE_BITMAP(enabled_irqs, LJCA_MAX_GPIO_NUM);
	DECLARE_BITMAP(reenable_irqs, LJCA_MAX_GPIO_NUM);
	DECLARE_BITMAP(output_enabled, LJCA_MAX_GPIO_NUM);
	u8 *connect_mode;
	/* protect irq bus */
	struct mutex irq_lock;
	struct work_struct work;
	/* protect package transfer to hardware */
	struct mutex trans_lock;

	u8 obuf[LJCA_GPIO_BUF_SIZE];
	u8 ibuf[LJCA_GPIO_BUF_SIZE];
};

static int ljca_gpio_config(struct ljca_gpio_dev *ljca_gpio, u8 gpio_id,
			    u8 config)
{
	struct ljca_gpio_packet *packet =
				(struct ljca_gpio_packet *)ljca_gpio->obuf;
	int ret;

	mutex_lock(&ljca_gpio->trans_lock);
	packet->num = 1;
	packet->item[0].index = gpio_id;
	packet->item[0].value = config | ljca_gpio->connect_mode[gpio_id];

	ret = ljca_transfer(ljca_gpio->ljca, LJCA_GPIO_CONFIG, (u8 *)packet,
			    struct_size(packet, item, packet->num), NULL, 0);
	mutex_unlock(&ljca_gpio->trans_lock);

	return ret < 0 ? ret : 0;
}

static int ljca_gpio_read(struct ljca_gpio_dev *ljca_gpio, u8 gpio_id)
{
	struct ljca_gpio_packet *ack_packet =
				(struct ljca_gpio_packet *)ljca_gpio->ibuf;
	struct ljca_gpio_packet *packet =
				(struct ljca_gpio_packet *)ljca_gpio->obuf;
	int ret;

	mutex_lock(&ljca_gpio->trans_lock);
	packet->num = 1;
	packet->item[0].index = gpio_id;
	ret = ljca_transfer(ljca_gpio->ljca, LJCA_GPIO_READ, (u8 *)packet,
			    struct_size(packet, item, packet->num),
			    ljca_gpio->ibuf, LJCA_GPIO_BUF_SIZE);

	if (ret <= 0 || ack_packet->num != packet->num) {
		dev_err(&ljca_gpio->ljca->auxdev.dev,
			"read package error, gpio_id: %u num: %u ret: %d\n",
			gpio_id, ack_packet->num, ret);
		ret = ret < 0 ? ret : -EIO;
	}
	mutex_unlock(&ljca_gpio->trans_lock);

	return ret < 0 ? ret : ack_packet->item[0].value > 0;
}

static int ljca_gpio_write(struct ljca_gpio_dev *ljca_gpio, u8 gpio_id, int value)
{
	struct ljca_gpio_packet *packet =
			(struct ljca_gpio_packet *)ljca_gpio->obuf;
	int ret;

	mutex_lock(&ljca_gpio->trans_lock);
	packet->num = 1;
	packet->item[0].index = gpio_id;
	packet->item[0].value = value & 1;

	ret = ljca_transfer(ljca_gpio->ljca, LJCA_GPIO_WRITE, (u8 *)packet,
			    struct_size(packet, item, packet->num), NULL, 0);
	mutex_unlock(&ljca_gpio->trans_lock);

	return ret < 0 ? ret : 0;
}

static int ljca_gpio_get_value(struct gpio_chip *chip, unsigned int offset)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);

	return ljca_gpio_read(ljca_gpio, offset);
}

static void ljca_gpio_set_value(struct gpio_chip *chip, unsigned int offset,
				int val)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);
	int ret;

	ret = ljca_gpio_write(ljca_gpio, offset, val);
	if (ret)
		dev_err(chip->parent,
			"set value failed offset: %u val: %d ret: %d\n",
			offset, val, ret);
}

static int ljca_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);
	u8 config = LJCA_GPIO_CONF_INPUT | LJCA_GPIO_CONF_CLR;
	int ret;

	ret = ljca_gpio_config(ljca_gpio, offset, config);
	if (ret)
		return ret;

	clear_bit(offset, ljca_gpio->output_enabled);

	return 0;
}

static int ljca_gpio_direction_output(struct gpio_chip *chip,
				      unsigned int offset, int val)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);
	u8 config = LJCA_GPIO_CONF_OUTPUT | LJCA_GPIO_CONF_CLR;
	int ret;

	ret = ljca_gpio_config(ljca_gpio, offset, config);
	if (ret)
		return ret;

	ljca_gpio_set_value(chip, offset, val);
	set_bit(offset, ljca_gpio->output_enabled);

	return 0;
}

static int ljca_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);

	if (test_bit(offset, ljca_gpio->output_enabled))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int ljca_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
				unsigned long config)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);

	ljca_gpio->connect_mode[offset] = 0;
	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
		ljca_gpio->connect_mode[offset] |= LJCA_GPIO_CONF_PULLUP;
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		ljca_gpio->connect_mode[offset] |= LJCA_GPIO_CONF_PULLDOWN;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
	case PIN_CONFIG_PERSIST_STATE:
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static int ljca_gpio_init_valid_mask(struct gpio_chip *chip,
				     unsigned long *valid_mask,
				     unsigned int ngpios)
{
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(chip);

	WARN_ON_ONCE(ngpios != ljca_gpio->gpio_info->num);
	bitmap_copy(valid_mask, ljca_gpio->gpio_info->valid_pin_map, ngpios);

	return 0;
}

static void ljca_gpio_irq_init_valid_mask(struct gpio_chip *chip,
					  unsigned long *valid_mask,
					  unsigned int ngpios)
{
	ljca_gpio_init_valid_mask(chip, valid_mask, ngpios);
}

static int ljca_enable_irq(struct ljca_gpio_dev *ljca_gpio, int gpio_id,
			   bool enable)
{
	struct ljca_gpio_packet *packet =
			(struct ljca_gpio_packet *)ljca_gpio->obuf;
	int ret;

	mutex_lock(&ljca_gpio->trans_lock);
	packet->num = 1;
	packet->item[0].index = gpio_id;
	packet->item[0].value = 0;

	ret = ljca_transfer(ljca_gpio->ljca,
			    enable ? LJCA_GPIO_INT_UNMASK : LJCA_GPIO_INT_MASK,
			    (u8 *)packet, struct_size(packet, item, packet->num),
			    NULL, 0);
	mutex_unlock(&ljca_gpio->trans_lock);

	return ret < 0 ? ret : 0;
}

static void ljca_gpio_async(struct work_struct *work)
{
	struct ljca_gpio_dev *ljca_gpio =
			container_of(work, struct ljca_gpio_dev, work);
	int gpio_id, unmasked;

	for_each_set_bit(gpio_id, ljca_gpio->reenable_irqs, ljca_gpio->gc.ngpio) {
		clear_bit(gpio_id, ljca_gpio->reenable_irqs);
		unmasked = test_bit(gpio_id, ljca_gpio->unmasked_irqs);
		if (unmasked)
			ljca_enable_irq(ljca_gpio, gpio_id, true);
	}
}

static void ljca_gpio_event_cb(void *context, u8 cmd, const void *evt_data,
			       int len)
{
	const struct ljca_gpio_packet *packet = evt_data;
	struct ljca_gpio_dev *ljca_gpio = context;
	int i, irq;

	if (cmd != LJCA_GPIO_INT_EVENT)
		return;

	for (i = 0; i < packet->num; i++) {
		irq = irq_find_mapping(ljca_gpio->gc.irq.domain,
				       packet->item[i].index);
		if (!irq) {
			dev_err(ljca_gpio->gc.parent,
				"gpio_id %u does not mapped to IRQ yet\n",
				packet->item[i].index);
			return;
		}

		generic_handle_domain_irq(ljca_gpio->gc.irq.domain, irq);
		set_bit(packet->item[i].index, ljca_gpio->reenable_irqs);
	}

	schedule_work(&ljca_gpio->work);
}

static void ljca_irq_unmask(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(gc);
	int gpio_id = irqd_to_hwirq(irqd);

	gpiochip_enable_irq(gc, gpio_id);
	set_bit(gpio_id, ljca_gpio->unmasked_irqs);
}

static void ljca_irq_mask(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(gc);
	int gpio_id = irqd_to_hwirq(irqd);

	clear_bit(gpio_id, ljca_gpio->unmasked_irqs);
	gpiochip_disable_irq(gc, gpio_id);
}

static int ljca_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(gc);
	int gpio_id = irqd_to_hwirq(irqd);

	ljca_gpio->connect_mode[gpio_id] = LJCA_GPIO_CONF_INTERRUPT;
	switch (type) {
	case IRQ_TYPE_LEVEL_HIGH:
		ljca_gpio->connect_mode[gpio_id] |=
			(LJCA_GPIO_CONF_LEVEL | LJCA_GPIO_CONF_PULLUP);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		ljca_gpio->connect_mode[gpio_id] |=
			(LJCA_GPIO_CONF_LEVEL | LJCA_GPIO_CONF_PULLDOWN);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		break;
	case IRQ_TYPE_EDGE_RISING:
		ljca_gpio->connect_mode[gpio_id] |=
			(LJCA_GPIO_CONF_EDGE | LJCA_GPIO_CONF_PULLUP);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		ljca_gpio->connect_mode[gpio_id] |=
			(LJCA_GPIO_CONF_EDGE | LJCA_GPIO_CONF_PULLDOWN);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void ljca_irq_bus_lock(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(gc);

	mutex_lock(&ljca_gpio->irq_lock);
}

static void ljca_irq_bus_unlock(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct ljca_gpio_dev *ljca_gpio = gpiochip_get_data(gc);
	int gpio_id = irqd_to_hwirq(irqd);
	int enabled, unmasked;

	enabled = test_bit(gpio_id, ljca_gpio->enabled_irqs);
	unmasked = test_bit(gpio_id, ljca_gpio->unmasked_irqs);

	if (enabled != unmasked) {
		if (unmasked) {
			ljca_gpio_config(ljca_gpio, gpio_id, 0);
			ljca_enable_irq(ljca_gpio, gpio_id, true);
			set_bit(gpio_id, ljca_gpio->enabled_irqs);
		} else {
			ljca_enable_irq(ljca_gpio, gpio_id, false);
			clear_bit(gpio_id, ljca_gpio->enabled_irqs);
		}
	}

	mutex_unlock(&ljca_gpio->irq_lock);
}

static const struct irq_chip ljca_gpio_irqchip = {
	.name = "ljca-irq",
	.irq_mask = ljca_irq_mask,
	.irq_unmask = ljca_irq_unmask,
	.irq_set_type = ljca_irq_set_type,
	.irq_bus_lock = ljca_irq_bus_lock,
	.irq_bus_sync_unlock = ljca_irq_bus_unlock,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int ljca_gpio_probe(struct auxiliary_device *auxdev,
			   const struct auxiliary_device_id *aux_dev_id)
{
	struct ljca_client *ljca = auxiliary_dev_to_ljca_client(auxdev);
	struct ljca_gpio_dev *ljca_gpio;
	struct gpio_irq_chip *girq;
	int ret;

	ljca_gpio = devm_kzalloc(&auxdev->dev, sizeof(*ljca_gpio), GFP_KERNEL);
	if (!ljca_gpio)
		return -ENOMEM;

	ljca_gpio->ljca = ljca;
	ljca_gpio->gpio_info = dev_get_platdata(&auxdev->dev);
	ljca_gpio->connect_mode = devm_kcalloc(&auxdev->dev,
					       ljca_gpio->gpio_info->num,
					       sizeof(*ljca_gpio->connect_mode),
					       GFP_KERNEL);
	if (!ljca_gpio->connect_mode)
		return -ENOMEM;

	ret = devm_mutex_init(&auxdev->dev, &ljca_gpio->irq_lock);
	if (ret)
		return ret;

	ret = devm_mutex_init(&auxdev->dev, &ljca_gpio->trans_lock);
	if (ret)
		return ret;

	ljca_gpio->gc.direction_input = ljca_gpio_direction_input;
	ljca_gpio->gc.direction_output = ljca_gpio_direction_output;
	ljca_gpio->gc.get_direction = ljca_gpio_get_direction;
	ljca_gpio->gc.get = ljca_gpio_get_value;
	ljca_gpio->gc.set = ljca_gpio_set_value;
	ljca_gpio->gc.set_config = ljca_gpio_set_config;
	ljca_gpio->gc.init_valid_mask = ljca_gpio_init_valid_mask;
	ljca_gpio->gc.can_sleep = true;
	ljca_gpio->gc.parent = &auxdev->dev;

	ljca_gpio->gc.base = -1;
	ljca_gpio->gc.ngpio = ljca_gpio->gpio_info->num;
	ljca_gpio->gc.label = ACPI_COMPANION(&auxdev->dev) ?
			      acpi_dev_name(ACPI_COMPANION(&auxdev->dev)) :
			      dev_name(&auxdev->dev);
	ljca_gpio->gc.owner = THIS_MODULE;

	auxiliary_set_drvdata(auxdev, ljca_gpio);
	ljca_register_event_cb(ljca, ljca_gpio_event_cb, ljca_gpio);

	girq = &ljca_gpio->gc.irq;
	gpio_irq_chip_set_chip(girq, &ljca_gpio_irqchip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_simple_irq;
	girq->init_valid_mask = ljca_gpio_irq_init_valid_mask;

	INIT_WORK(&ljca_gpio->work, ljca_gpio_async);
	ret = gpiochip_add_data(&ljca_gpio->gc, ljca_gpio);
	if (ret)
		ljca_unregister_event_cb(ljca);

	return ret;
}

static void ljca_gpio_remove(struct auxiliary_device *auxdev)
{
	struct ljca_gpio_dev *ljca_gpio = auxiliary_get_drvdata(auxdev);

	gpiochip_remove(&ljca_gpio->gc);
	ljca_unregister_event_cb(ljca_gpio->ljca);
	cancel_work_sync(&ljca_gpio->work);
}

static const struct auxiliary_device_id ljca_gpio_id_table[] = {
	{ "usb_ljca.ljca-gpio", 0 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(auxiliary, ljca_gpio_id_table);

static struct auxiliary_driver ljca_gpio_driver = {
	.probe = ljca_gpio_probe,
	.remove = ljca_gpio_remove,
	.id_table = ljca_gpio_id_table,
};
module_auxiliary_driver(ljca_gpio_driver);

MODULE_AUTHOR("Wentong Wu <wentong.wu@intel.com>");
MODULE_AUTHOR("Zhifeng Wang <zhifeng.wang@intel.com>");
MODULE_AUTHOR("Lixu Zhang <lixu.zhang@intel.com>");
MODULE_DESCRIPTION("Intel La Jolla Cove Adapter USB-GPIO driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("LJCA");
