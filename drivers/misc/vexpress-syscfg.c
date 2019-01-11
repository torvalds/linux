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
 * Copyright (C) 2014 ARM Limited
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/vexpress.h>


#define SYS_CFGDATA		0x0

#define SYS_CFGCTRL		0x4
#define SYS_CFGCTRL_START	(1 << 31)
#define SYS_CFGCTRL_WRITE	(1 << 30)
#define SYS_CFGCTRL_DCC(n)	(((n) & 0xf) << 26)
#define SYS_CFGCTRL_FUNC(n)	(((n) & 0x3f) << 20)
#define SYS_CFGCTRL_SITE(n)	(((n) & 0x3) << 16)
#define SYS_CFGCTRL_POSITION(n)	(((n) & 0xf) << 12)
#define SYS_CFGCTRL_DEVICE(n)	(((n) & 0xfff) << 0)

#define SYS_CFGSTAT		0x8
#define SYS_CFGSTAT_ERR		(1 << 1)
#define SYS_CFGSTAT_COMPLETE	(1 << 0)


struct vexpress_syscfg {
	struct device *dev;
	void __iomem *base;
	struct list_head funcs;
};

struct vexpress_syscfg_func {
	struct list_head list;
	struct vexpress_syscfg *syscfg;
	struct regmap *regmap;
	int num_templates;
	u32 template[0]; /* Keep it last! */
};


static int vexpress_syscfg_exec(struct vexpress_syscfg_func *func,
		int index, bool write, u32 *data)
{
	struct vexpress_syscfg *syscfg = func->syscfg;
	u32 command, status;
	int tries;
	long timeout;

	if (WARN_ON(index > func->num_templates))
		return -EINVAL;

	command = readl(syscfg->base + SYS_CFGCTRL);
	if (WARN_ON(command & SYS_CFGCTRL_START))
		return -EBUSY;

	command = func->template[index];
	command |= SYS_CFGCTRL_START;
	command |= write ? SYS_CFGCTRL_WRITE : 0;

	/* Use a canary for reads */
	if (!write)
		*data = 0xdeadbeef;

	dev_dbg(syscfg->dev, "func %p, command %x, data %x\n",
			func, command, *data);
	writel(*data, syscfg->base + SYS_CFGDATA);
	writel(0, syscfg->base + SYS_CFGSTAT);
	writel(command, syscfg->base + SYS_CFGCTRL);
	mb();

	/* The operation can take ages... Go to sleep, 100us initially */
	tries = 100;
	timeout = 100;
	do {
		if (!irqs_disabled()) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(usecs_to_jiffies(timeout));
			if (signal_pending(current))
				return -EINTR;
		} else {
			udelay(timeout);
		}

		status = readl(syscfg->base + SYS_CFGSTAT);
		if (status & SYS_CFGSTAT_ERR)
			return -EFAULT;

		if (timeout > 20)
			timeout -= 20;
	} while (--tries && !(status & SYS_CFGSTAT_COMPLETE));
	if (WARN_ON_ONCE(!tries))
		return -ETIMEDOUT;

	if (!write) {
		*data = readl(syscfg->base + SYS_CFGDATA);
		dev_dbg(syscfg->dev, "func %p, read data %x\n", func, *data);
	}

	return 0;
}

static int vexpress_syscfg_read(void *context, unsigned int index,
		unsigned int *val)
{
	struct vexpress_syscfg_func *func = context;

	return vexpress_syscfg_exec(func, index, false, val);
}

static int vexpress_syscfg_write(void *context, unsigned int index,
		unsigned int val)
{
	struct vexpress_syscfg_func *func = context;

	return vexpress_syscfg_exec(func, index, true, &val);
}

static struct regmap_config vexpress_syscfg_regmap_config = {
	.lock = vexpress_config_lock,
	.unlock = vexpress_config_unlock,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_read = vexpress_syscfg_read,
	.reg_write = vexpress_syscfg_write,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};


static struct regmap *vexpress_syscfg_regmap_init(struct device *dev,
		void *context)
{
	int err;
	struct vexpress_syscfg *syscfg = context;
	struct vexpress_syscfg_func *func;
	struct property *prop;
	const __be32 *val = NULL;
	__be32 energy_quirk[4];
	int num;
	u32 site, position, dcc;
	int i;

	err = vexpress_config_get_topo(dev->of_node, &site,
				&position, &dcc);
	if (err)
		return ERR_PTR(err);

	prop = of_find_property(dev->of_node,
			"arm,vexpress-sysreg,func", NULL);
	if (!prop)
		return ERR_PTR(-EINVAL);

	num = prop->length / sizeof(u32) / 2;
	val = prop->value;

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

	func = kzalloc(struct_size(func, template, num), GFP_KERNEL);
	if (!func)
		return ERR_PTR(-ENOMEM);

	func->syscfg = syscfg;
	func->num_templates = num;

	for (i = 0; i < num; i++) {
		u32 function, device;

		function = be32_to_cpup(val++);
		device = be32_to_cpup(val++);

		dev_dbg(dev, "func %p: %u/%u/%u/%u/%u\n",
				func, site, position, dcc,
				function, device);

		func->template[i] = SYS_CFGCTRL_DCC(dcc);
		func->template[i] |= SYS_CFGCTRL_SITE(site);
		func->template[i] |= SYS_CFGCTRL_POSITION(position);
		func->template[i] |= SYS_CFGCTRL_FUNC(function);
		func->template[i] |= SYS_CFGCTRL_DEVICE(device);
	}

	vexpress_syscfg_regmap_config.max_register = num - 1;

	func->regmap = regmap_init(dev, NULL, func,
			&vexpress_syscfg_regmap_config);

	if (IS_ERR(func->regmap)) {
		void *err = func->regmap;

		kfree(func);
		return err;
	}

	list_add(&func->list, &syscfg->funcs);

	return func->regmap;
}

static void vexpress_syscfg_regmap_exit(struct regmap *regmap, void *context)
{
	struct vexpress_syscfg *syscfg = context;
	struct vexpress_syscfg_func *func, *tmp;

	regmap_exit(regmap);

	list_for_each_entry_safe(func, tmp, &syscfg->funcs, list) {
		if (func->regmap == regmap) {
			list_del(&syscfg->funcs);
			kfree(func);
			break;
		}
	}
}

static struct vexpress_config_bridge_ops vexpress_syscfg_bridge_ops = {
	.regmap_init = vexpress_syscfg_regmap_init,
	.regmap_exit = vexpress_syscfg_regmap_exit,
};


static int vexpress_syscfg_probe(struct platform_device *pdev)
{
	struct vexpress_syscfg *syscfg;
	struct resource *res;
	struct device *bridge;

	syscfg = devm_kzalloc(&pdev->dev, sizeof(*syscfg), GFP_KERNEL);
	if (!syscfg)
		return -ENOMEM;
	syscfg->dev = &pdev->dev;
	INIT_LIST_HEAD(&syscfg->funcs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), pdev->name))
		return -EBUSY;

	syscfg->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!syscfg->base)
		return -EFAULT;

	/* Must use dev.parent (MFD), as that's where DT phandle points at... */
	bridge = vexpress_config_bridge_register(pdev->dev.parent,
			&vexpress_syscfg_bridge_ops, syscfg);

	return PTR_ERR_OR_ZERO(bridge);
}

static const struct platform_device_id vexpress_syscfg_id_table[] = {
	{ "vexpress-syscfg", },
	{},
};

static struct platform_driver vexpress_syscfg_driver = {
	.driver.name = "vexpress-syscfg",
	.id_table = vexpress_syscfg_id_table,
	.probe = vexpress_syscfg_probe,
};

static int __init vexpress_syscfg_init(void)
{
	return platform_driver_register(&vexpress_syscfg_driver);
}
core_initcall(vexpress_syscfg_init);
