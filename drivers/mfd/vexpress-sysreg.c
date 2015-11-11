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
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
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
static int vexpress_master_site;


void vexpress_flags_set(u32 data)
{
	writel(~0, vexpress_sysreg_base + SYS_FLAGSCLR);
	writel(data, vexpress_sysreg_base + SYS_FLAGSSET);
}

u32 vexpress_get_procid(int site)
{
	if (site == VEXPRESS_SITE_MASTER)
		site = vexpress_master_site;

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


static void vexpress_sysreg_find_prop(struct device_node *node,
		const char *name, u32 *val)
{
	of_node_get(node);
	while (node) {
		if (of_property_read_u32(node, name, val) == 0) {
			of_node_put(node);
			return;
		}
		node = of_get_next_parent(node);
	}
}

unsigned __vexpress_get_site(struct device *dev, struct device_node *node)
{
	u32 site = 0;

	WARN_ON(dev && node && dev->of_node != node);
	if (dev && !node)
		node = dev->of_node;

	if (node) {
		vexpress_sysreg_find_prop(node, "arm,vexpress,site", &site);
	} else if (dev && dev->bus == &platform_bus_type) {
		struct platform_device *pdev = to_platform_device(dev);

		if (pdev->num_resources == 1 &&
				pdev->resource[0].flags == IORESOURCE_BUS)
			site = pdev->resource[0].start;
	} else if (dev && strncmp(dev_name(dev), "ct:", 3) == 0) {
		site = VEXPRESS_SITE_MASTER;
	}

	if (site == VEXPRESS_SITE_MASTER)
		site = vexpress_master_site;

	return site;
}


struct vexpress_sysreg_config_func {
	u32 template;
	u32 device;
};

static struct vexpress_config_bridge *vexpress_sysreg_config_bridge;
static struct timer_list vexpress_sysreg_config_timer;
static u32 *vexpress_sysreg_config_data;
static int vexpress_sysreg_config_tries;

static void *vexpress_sysreg_config_func_get(struct device *dev,
		struct device_node *node)
{
	struct vexpress_sysreg_config_func *config_func;
	u32 site;
	u32 position = 0;
	u32 dcc = 0;
	u32 func_device[2];
	int err = -EFAULT;

	if (node) {
		of_node_get(node);
		vexpress_sysreg_find_prop(node, "arm,vexpress,site", &site);
		vexpress_sysreg_find_prop(node, "arm,vexpress,position",
				&position);
		vexpress_sysreg_find_prop(node, "arm,vexpress,dcc", &dcc);
		err = of_property_read_u32_array(node,
				"arm,vexpress-sysreg,func", func_device,
				ARRAY_SIZE(func_device));
		of_node_put(node);
	} else if (dev && dev->bus == &platform_bus_type) {
		struct platform_device *pdev = to_platform_device(dev);

		if (pdev->num_resources == 1 &&
				pdev->resource[0].flags == IORESOURCE_BUS) {
			site = pdev->resource[0].start;
			func_device[0] = pdev->resource[0].end;
			func_device[1] = pdev->id;
			err = 0;
		}
	}
	if (err)
		return NULL;

	config_func = kzalloc(sizeof(*config_func), GFP_KERNEL);
	if (!config_func)
		return NULL;

	config_func->template = SYS_CFGCTRL_DCC(dcc);
	config_func->template |= SYS_CFGCTRL_FUNC(func_device[0]);
	config_func->template |= SYS_CFGCTRL_SITE(site == VEXPRESS_SITE_MASTER ?
			vexpress_master_site : site);
	config_func->template |= SYS_CFGCTRL_POSITION(position);
	config_func->device |= func_device[1];

	dev_dbg(vexpress_sysreg_dev, "func 0x%p = 0x%x, %d\n", config_func,
			config_func->template, config_func->device);

	return config_func;
}

static void vexpress_sysreg_config_func_put(void *func)
{
	kfree(func);
}

static int vexpress_sysreg_config_func_exec(void *func, int offset,
		bool write, u32 *data)
{
	int status;
	struct vexpress_sysreg_config_func *config_func = func;
	u32 command;

	if (WARN_ON(!vexpress_sysreg_base))
		return -ENOENT;

	command = readl(vexpress_sysreg_base + SYS_CFGCTRL);
	if (WARN_ON(command & SYS_CFGCTRL_START))
		return -EBUSY;

	command = SYS_CFGCTRL_START;
	command |= write ? SYS_CFGCTRL_WRITE : 0;
	command |= config_func->template;
	command |= SYS_CFGCTRL_DEVICE(config_func->device + offset);

	/* Use a canary for reads */
	if (!write)
		*data = 0xdeadbeef;

	dev_dbg(vexpress_sysreg_dev, "command %x, data %x\n",
			command, *data);
	writel(*data, vexpress_sysreg_base + SYS_CFGDATA);
	writel(0, vexpress_sysreg_base + SYS_CFGSTAT);
	writel(command, vexpress_sysreg_base + SYS_CFGCTRL);
	mb();

	if (vexpress_sysreg_dev) {
		/* Schedule completion check */
		if (!write)
			vexpress_sysreg_config_data = data;
		vexpress_sysreg_config_tries = 100;
		mod_timer(&vexpress_sysreg_config_timer,
				jiffies + usecs_to_jiffies(100));
		status = VEXPRESS_CONFIG_STATUS_WAIT;
	} else {
		/* Early execution, no timer available, have to spin */
		u32 cfgstat;

		do {
			cpu_relax();
			cfgstat = readl(vexpress_sysreg_base + SYS_CFGSTAT);
		} while (!cfgstat);

		if (!write && (cfgstat & SYS_CFGSTAT_COMPLETE))
			*data = readl(vexpress_sysreg_base + SYS_CFGDATA);
		status = VEXPRESS_CONFIG_STATUS_DONE;

		if (cfgstat & SYS_CFGSTAT_ERR)
			status = -EINVAL;
	}

	return status;
}

struct vexpress_config_bridge_info vexpress_sysreg_config_bridge_info = {
	.name = "vexpress-sysreg",
	.func_get = vexpress_sysreg_config_func_get,
	.func_put = vexpress_sysreg_config_func_put,
	.func_exec = vexpress_sysreg_config_func_exec,
};

static void vexpress_sysreg_config_complete(unsigned long data)
{
	int status = VEXPRESS_CONFIG_STATUS_DONE;
	u32 cfgstat = readl(vexpress_sysreg_base + SYS_CFGSTAT);

	if (cfgstat & SYS_CFGSTAT_ERR)
		status = -EINVAL;
	if (!vexpress_sysreg_config_tries--)
		status = -ETIMEDOUT;

	if (status < 0) {
		dev_err(vexpress_sysreg_dev, "error %d\n", status);
	} else if (!(cfgstat & SYS_CFGSTAT_COMPLETE)) {
		mod_timer(&vexpress_sysreg_config_timer,
				jiffies + usecs_to_jiffies(50));
		return;
	}

	if (vexpress_sysreg_config_data) {
		*vexpress_sysreg_config_data = readl(vexpress_sysreg_base +
				SYS_CFGDATA);
		dev_dbg(vexpress_sysreg_dev, "read data %x\n",
				*vexpress_sysreg_config_data);
		vexpress_sysreg_config_data = NULL;
	}

	vexpress_config_complete(vexpress_sysreg_config_bridge, status);
}


void vexpress_sysreg_setup(struct device_node *node)
{
	if (WARN_ON(!vexpress_sysreg_base))
		return;

	if (readl(vexpress_sysreg_base + SYS_MISC) & SYS_MISC_MASTERSITE)
		vexpress_master_site = VEXPRESS_SITE_DB2;
	else
		vexpress_master_site = VEXPRESS_SITE_DB1;

	vexpress_sysreg_config_bridge = vexpress_config_bridge_register(
			node, &vexpress_sysreg_config_bridge_info);
	WARN_ON(!vexpress_sysreg_config_bridge);
}

void __init vexpress_sysreg_early_init(void __iomem *base)
{
	vexpress_sysreg_base = base;
	vexpress_sysreg_setup(NULL);
}

void __init vexpress_sysreg_of_early_init(void)
{
	struct device_node *node;

	if (vexpress_sysreg_base)
		return;

	node = of_find_compatible_node(NULL, NULL, "arm,vexpress-sysreg");
	if (node) {
		vexpress_sysreg_base = of_iomap(node, 0);
		vexpress_sysreg_setup(node);
	}
}


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

	if (!vexpress_sysreg_base) {
		vexpress_sysreg_base = devm_ioremap(&pdev->dev, res->start,
				resource_size(res));
		vexpress_sysreg_setup(pdev->dev.of_node);
	}

	if (!vexpress_sysreg_base) {
		dev_err(&pdev->dev, "Failed to obtain base address!\n");
		return -EFAULT;
	}

	setup_timer(&vexpress_sysreg_config_timer,
			vexpress_sysreg_config_complete, 0);

	vexpress_sysreg_gpio_chip.dev = &pdev->dev;
	err = gpiochip_add(&vexpress_sysreg_gpio_chip);
	if (err) {
		vexpress_config_bridge_unregister(
				vexpress_sysreg_config_bridge);
		dev_err(&pdev->dev, "Failed to register GPIO chip! (%d)\n",
				err);
		return err;
	}

	vexpress_sysreg_dev = &pdev->dev;

	platform_device_register_data(vexpress_sysreg_dev, "leds-gpio",
			PLATFORM_DEVID_AUTO, &vexpress_sysreg_leds_pdata,
			sizeof(vexpress_sysreg_leds_pdata));

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
	vexpress_sysreg_of_early_init();
	return platform_driver_register(&vexpress_sysreg_driver);
}
core_initcall(vexpress_sysreg_init);
