/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/timer.h>
#include <linux/vexpress.h>

#define SYS_ID			0x000
#define SYS_SW			0x004
#define SYS_LED			0x008
#define SYS_100HZ		0x024
#define SYS_FLAGS		0x030
#define SYS_FLAGSSET		0x030
#define SYS_FLAGSCLR		0x034
#define SYS_NVFLAGS		0x038
#define SYS_NVFLAGSSET		0x038
#define SYS_NVFLAGSCLR		0x03c
#define SYS_MCI			0x048
#define SYS_FLASH		0x04c
#define SYS_CFGSW		0x058
#define SYS_24MHZ		0x05c
#define SYS_MISC		0x060
#define SYS_DMA			0x064
#define SYS_PROCID0		0x084
#define SYS_PROCID1		0x088
#define SYS_CFGDATA		0x0a0
#define SYS_CFGCTRL		0x0a4
#define SYS_CFGSTAT		0x0a8

#define SYS_HBI_MASK		0xfff
#define SYS_ID_HBI_SHIFT	16
#define SYS_PROCIDx_HBI_SHIFT	0

#define SYS_LED_LED(n)		(1 << (n))

#define SYS_MCI_CARDIN		(1 << 0)
#define SYS_MCI_WPROT		(1 << 1)

#define SYS_FLASH_WPn		(1 << 0)

#define SYS_MISC_MASTERSITE	(1 << 14)

#define SYS_CFGCTRL_START	(1 << 31)
#define SYS_CFGCTRL_WRITE	(1 << 30)
#define SYS_CFGCTRL_DCC(n)	(((n) & 0xf) << 26)
#define SYS_CFGCTRL_FUNC(n)	(((n) & 0x3f) << 20)
#define SYS_CFGCTRL_SITE(n)	(((n) & 0x3) << 16)
#define SYS_CFGCTRL_POSITION(n)	(((n) & 0xf) << 12)
#define SYS_CFGCTRL_DEVICE(n)	(((n) & 0xfff) << 0)

#define SYS_CFGSTAT_ERR		(1 << 1)
#define SYS_CFGSTAT_COMPLETE	(1 << 0)


static void __iomem *vexpress_sysreg_base;
static struct device *vexpress_sysreg_dev;
static LIST_HEAD(vexpress_sysreg_config_funcs);
static struct device *vexpress_sysreg_config_bridge;


static int vexpress_sysreg_get_master(void)
{
	if (readl(vexpress_sysreg_base + SYS_MISC) & SYS_MISC_MASTERSITE)
		return VEXPRESS_SITE_DB2;

	return VEXPRESS_SITE_DB1;
}

void vexpress_flags_set(u32 data)
{
	writel(~0, vexpress_sysreg_base + SYS_FLAGSCLR);
	writel(data, vexpress_sysreg_base + SYS_FLAGSSET);
}

u32 vexpress_get_procid(int site)
{
	if (site == VEXPRESS_SITE_MASTER)
		site = vexpress_sysreg_get_master();

	return readl(vexpress_sysreg_base + (site == VEXPRESS_SITE_DB1 ?
			SYS_PROCID0 : SYS_PROCID1));
}

u32 vexpress_get_hbi(int site)
{
	u32 id;

	switch (site) {
	case VEXPRESS_SITE_MB:
		id = readl(vexpress_sysreg_base + SYS_ID);
		return (id >> SYS_ID_HBI_SHIFT) & SYS_HBI_MASK;
	case VEXPRESS_SITE_MASTER:
	case VEXPRESS_SITE_DB1:
	case VEXPRESS_SITE_DB2:
		id = vexpress_get_procid(site);
		return (id >> SYS_PROCIDx_HBI_SHIFT) & SYS_HBI_MASK;
	}

	return ~0;
}

void __iomem *vexpress_get_24mhz_clock_base(void)
{
	return vexpress_sysreg_base + SYS_24MHZ;
}


struct vexpress_sysreg_config_func {
	struct list_head list;
	struct regmap *regmap;
	int num_templates;
	u32 template[0]; /* Keep this last */
};

static int vexpress_sysreg_config_exec(struct vexpress_sysreg_config_func *func,
		int index, bool write, u32 *data)
{
	u32 command, status;
	int tries;
	long timeout;

	if (WARN_ON(!vexpress_sysreg_base))
		return -ENOENT;

	if (WARN_ON(index > func->num_templates))
		return -EINVAL;

	command = readl(vexpress_sysreg_base + SYS_CFGCTRL);
	if (WARN_ON(command & SYS_CFGCTRL_START))
		return -EBUSY;

	command = func->template[index];
	command |= SYS_CFGCTRL_START;
	command |= write ? SYS_CFGCTRL_WRITE : 0;

	/* Use a canary for reads */
	if (!write)
		*data = 0xdeadbeef;

	dev_dbg(vexpress_sysreg_dev, "command %x, data %x\n",
			command, *data);
	writel(*data, vexpress_sysreg_base + SYS_CFGDATA);
	writel(0, vexpress_sysreg_base + SYS_CFGSTAT);
	writel(command, vexpress_sysreg_base + SYS_CFGCTRL);
	mb();

	/* The operation can take ages... Go to sleep, 100us initially */
	tries = 100;
	timeout = 100;
	do {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(usecs_to_jiffies(timeout));
		if (signal_pending(current))
			return -EINTR;

		status = readl(vexpress_sysreg_base + SYS_CFGSTAT);
		if (status & SYS_CFGSTAT_ERR)
			return -EFAULT;

		if (timeout > 20)
			timeout -= 20;
	} while (--tries && !(status & SYS_CFGSTAT_COMPLETE));
	if (WARN_ON_ONCE(!tries))
		return -ETIMEDOUT;

	if (!write) {
		*data = readl(vexpress_sysreg_base + SYS_CFGDATA);
		dev_dbg(vexpress_sysreg_dev, "func %p, read data %x\n",
				func, *data);
	}

	return 0;
}

static int vexpress_sysreg_config_read(void *context, unsigned int index,
		unsigned int *val)
{
	struct vexpress_sysreg_config_func *func = context;

	return vexpress_sysreg_config_exec(func, index, false, val);
}

static int vexpress_sysreg_config_write(void *context, unsigned int index,
		unsigned int val)
{
	struct vexpress_sysreg_config_func *func = context;

	return vexpress_sysreg_config_exec(func, index, true, &val);
}

struct regmap_config vexpress_sysreg_regmap_config = {
	.lock = vexpress_config_lock,
	.unlock = vexpress_config_unlock,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_read = vexpress_sysreg_config_read,
	.reg_write = vexpress_sysreg_config_write,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static struct regmap *vexpress_sysreg_config_regmap_init(struct device *dev,
		void *context)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vexpress_sysreg_config_func *func;
	struct property *prop;
	const __be32 *val = NULL;
	__be32 energy_quirk[4];
	int num;
	u32 site, position, dcc;
	int err;
	int i;

	if (dev->of_node) {
		err = vexpress_config_get_topo(dev->of_node, &site, &position,
				&dcc);
		if (err)
			return ERR_PTR(err);

		prop = of_find_property(dev->of_node,
				"arm,vexpress-sysreg,func", NULL);
		if (!prop)
			return ERR_PTR(-EINVAL);

		num = prop->length / sizeof(u32) / 2;
		val = prop->value;
	} else {
		if (pdev->num_resources != 1 ||
				pdev->resource[0].flags != IORESOURCE_BUS)
			return ERR_PTR(-EFAULT);

		site = pdev->resource[0].start;
		if (site == VEXPRESS_SITE_MASTER)
			site = vexpress_sysreg_get_master();
		position = 0;
		dcc = 0;
		num = 1;
	}

	/*
	 * "arm,vexpress-energy" function used to be described
	 * by its first device only, now it requires both
	 */
	if (num == 1 && of_device_is_compatible(dev->of_node,
			"arm,vexpress-energy")) {
		num = 2;
		energy_quirk[0] = *val;
		energy_quirk[2] = *val++;
		energy_quirk[1] = *val;
		energy_quirk[3] = cpu_to_be32(be32_to_cpup(val) + 1);
		val = energy_quirk;
	}

	func = kzalloc(sizeof(*func) + sizeof(*func->template) * num,
			GFP_KERNEL);
	if (!func)
		return NULL;

	func->num_templates = num;

	for (i = 0; i < num; i++) {
		u32 function, device;

		if (dev->of_node) {
			function = be32_to_cpup(val++);
			device = be32_to_cpup(val++);
		} else {
			function = pdev->resource[0].end;
			device = pdev->id;
		}

		dev_dbg(dev, "func %p: %u/%u/%u/%u/%u\n",
				func, site, position, dcc,
				function, device);

		func->template[i] = SYS_CFGCTRL_DCC(dcc);
		func->template[i] |= SYS_CFGCTRL_SITE(site);
		func->template[i] |= SYS_CFGCTRL_POSITION(position);
		func->template[i] |= SYS_CFGCTRL_FUNC(function);
		func->template[i] |= SYS_CFGCTRL_DEVICE(device);
	}

	vexpress_sysreg_regmap_config.max_register = num - 1;

	func->regmap = regmap_init(dev, NULL, func,
			&vexpress_sysreg_regmap_config);

	if (IS_ERR(func->regmap))
		kfree(func);
	else
		list_add(&func->list, &vexpress_sysreg_config_funcs);

	return func->regmap;
}

static void vexpress_sysreg_config_regmap_exit(struct regmap *regmap,
		void *context)
{
	struct vexpress_sysreg_config_func *func, *tmp;

	regmap_exit(regmap);

	list_for_each_entry_safe(func, tmp, &vexpress_sysreg_config_funcs,
			list) {
		if (func->regmap == regmap) {
			list_del(&vexpress_sysreg_config_funcs);
			kfree(func);
			break;
		}
	}
}

static struct vexpress_config_bridge_ops vexpress_sysreg_config_bridge_ops = {
	.regmap_init = vexpress_sysreg_config_regmap_init,
	.regmap_exit = vexpress_sysreg_config_regmap_exit,
};

int vexpress_sysreg_config_device_register(struct platform_device *pdev)
{
	pdev->dev.parent = vexpress_sysreg_config_bridge;

	return platform_device_register(pdev);
}


void __init vexpress_sysreg_early_init(void __iomem *base)
{
	vexpress_sysreg_base = base;
	vexpress_config_set_master(vexpress_sysreg_get_master());
}

void __init vexpress_sysreg_of_early_init(void)
{
	struct device_node *node;

	if (vexpress_sysreg_base)
		return;

	node = of_find_compatible_node(NULL, NULL, "arm,vexpress-sysreg");
	if (WARN_ON(!node))
		return;

	vexpress_sysreg_base = of_iomap(node, 0);
	if (WARN_ON(!vexpress_sysreg_base))
		return;

	vexpress_config_set_master(vexpress_sysreg_get_master());
}


#ifdef CONFIG_GPIOLIB

#define VEXPRESS_SYSREG_GPIO(_name, _reg, _value) \
	[VEXPRESS_GPIO_##_name] = { \
		.reg = _reg, \
		.value = _reg##_##_value, \
	}

static struct vexpress_sysreg_gpio {
	unsigned long reg;
	u32 value;
} vexpress_sysreg_gpios[] = {
	VEXPRESS_SYSREG_GPIO(MMC_CARDIN,	SYS_MCI,	CARDIN),
	VEXPRESS_SYSREG_GPIO(MMC_WPROT,		SYS_MCI,	WPROT),
	VEXPRESS_SYSREG_GPIO(FLASH_WPn,		SYS_FLASH,	WPn),
	VEXPRESS_SYSREG_GPIO(LED0,		SYS_LED,	LED(0)),
	VEXPRESS_SYSREG_GPIO(LED1,		SYS_LED,	LED(1)),
	VEXPRESS_SYSREG_GPIO(LED2,		SYS_LED,	LED(2)),
	VEXPRESS_SYSREG_GPIO(LED3,		SYS_LED,	LED(3)),
	VEXPRESS_SYSREG_GPIO(LED4,		SYS_LED,	LED(4)),
	VEXPRESS_SYSREG_GPIO(LED5,		SYS_LED,	LED(5)),
	VEXPRESS_SYSREG_GPIO(LED6,		SYS_LED,	LED(6)),
	VEXPRESS_SYSREG_GPIO(LED7,		SYS_LED,	LED(7)),
};

static int vexpress_sysreg_gpio_direction_input(struct gpio_chip *chip,
				       unsigned offset)
{
	return 0;
}

static int vexpress_sysreg_gpio_get(struct gpio_chip *chip,
				       unsigned offset)
{
	struct vexpress_sysreg_gpio *gpio = &vexpress_sysreg_gpios[offset];
	u32 reg_value = readl(vexpress_sysreg_base + gpio->reg);

	return !!(reg_value & gpio->value);
}

static void vexpress_sysreg_gpio_set(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	struct vexpress_sysreg_gpio *gpio = &vexpress_sysreg_gpios[offset];
	u32 reg_value = readl(vexpress_sysreg_base + gpio->reg);

	if (value)
		reg_value |= gpio->value;
	else
		reg_value &= ~gpio->value;

	writel(reg_value, vexpress_sysreg_base + gpio->reg);
}

static int vexpress_sysreg_gpio_direction_output(struct gpio_chip *chip,
						unsigned offset, int value)
{
	vexpress_sysreg_gpio_set(chip, offset, value);

	return 0;
}

static struct gpio_chip vexpress_sysreg_gpio_chip = {
	.label = "vexpress-sysreg",
	.direction_input = vexpress_sysreg_gpio_direction_input,
	.direction_output = vexpress_sysreg_gpio_direction_output,
	.get = vexpress_sysreg_gpio_get,
	.set = vexpress_sysreg_gpio_set,
	.ngpio = ARRAY_SIZE(vexpress_sysreg_gpios),
	.base = 0,
};


#define VEXPRESS_SYSREG_GREEN_LED(_name, _default_trigger, _gpio) \
	{ \
		.name = "v2m:green:"_name, \
		.default_trigger = _default_trigger, \
		.gpio = VEXPRESS_GPIO_##_gpio, \
	}

struct gpio_led vexpress_sysreg_leds[] = {
	VEXPRESS_SYSREG_GREEN_LED("user1",	"heartbeat",	LED0),
	VEXPRESS_SYSREG_GREEN_LED("user2",	"mmc0",		LED1),
	VEXPRESS_SYSREG_GREEN_LED("user3",	"cpu0",		LED2),
	VEXPRESS_SYSREG_GREEN_LED("user4",	"cpu1",		LED3),
	VEXPRESS_SYSREG_GREEN_LED("user5",	"cpu2",		LED4),
	VEXPRESS_SYSREG_GREEN_LED("user6",	"cpu3",		LED5),
	VEXPRESS_SYSREG_GREEN_LED("user7",	"cpu4",		LED6),
	VEXPRESS_SYSREG_GREEN_LED("user8",	"cpu5",		LED7),
};

struct gpio_led_platform_data vexpress_sysreg_leds_pdata = {
	.num_leds = ARRAY_SIZE(vexpress_sysreg_leds),
	.leds = vexpress_sysreg_leds,
};

#endif


static ssize_t vexpress_sysreg_sys_id_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%08x\n", readl(vexpress_sysreg_base + SYS_ID));
}

DEVICE_ATTR(sys_id, S_IRUGO, vexpress_sysreg_sys_id_show, NULL);

static int vexpress_sysreg_probe(struct platform_device *pdev)
{
	int err;
	struct resource *res = platform_get_resource(pdev,
			IORESOURCE_MEM, 0);

	if (!devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), pdev->name)) {
		dev_err(&pdev->dev, "Failed to request memory region!\n");
		return -EBUSY;
	}

	if (!vexpress_sysreg_base)
		vexpress_sysreg_base = devm_ioremap(&pdev->dev, res->start,
				resource_size(res));

	if (!vexpress_sysreg_base) {
		dev_err(&pdev->dev, "Failed to obtain base address!\n");
		return -EFAULT;
	}

	vexpress_config_set_master(vexpress_sysreg_get_master());
	vexpress_sysreg_dev = &pdev->dev;

#ifdef CONFIG_GPIOLIB
	vexpress_sysreg_gpio_chip.dev = &pdev->dev;
	err = gpiochip_add(&vexpress_sysreg_gpio_chip);
	if (err) {
		dev_err(&pdev->dev, "Failed to register GPIO chip! (%d)\n",
				err);
		return err;
	}

	platform_device_register_data(vexpress_sysreg_dev, "leds-gpio",
			PLATFORM_DEVID_AUTO, &vexpress_sysreg_leds_pdata,
			sizeof(vexpress_sysreg_leds_pdata));
#endif

	vexpress_sysreg_config_bridge = vexpress_config_bridge_register(
			&pdev->dev, &vexpress_sysreg_config_bridge_ops, NULL);
	WARN_ON(!vexpress_sysreg_config_bridge);

	device_create_file(vexpress_sysreg_dev, &dev_attr_sys_id);

	return 0;
}

static const struct of_device_id vexpress_sysreg_match[] = {
	{ .compatible = "arm,vexpress-sysreg", },
	{},
};

static struct platform_driver vexpress_sysreg_driver = {
	.driver = {
		.name = "vexpress-sysreg",
		.of_match_table = vexpress_sysreg_match,
	},
	.probe = vexpress_sysreg_probe,
};

static int __init vexpress_sysreg_init(void)
{
	struct device_node *node;

	/* Need the sysreg early, before any other device... */
	for_each_matching_node(node, vexpress_sysreg_match)
		of_platform_device_create(node, NULL, NULL);

	return platform_driver_register(&vexpress_sysreg_driver);
}
core_initcall(vexpress_sysreg_init);
