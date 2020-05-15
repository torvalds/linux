// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2014 ARM Limited
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/vexpress.h>

#define SYS_MISC		0x0
#define SYS_MISC_MASTERSITE	(1 << 14)

#define SYS_PROCID0		0x24
#define SYS_PROCID1		0x28
#define SYS_HBI_MASK		0xfff
#define SYS_PROCIDx_HBI_SHIFT	0

#define SYS_CFGDATA		0x40

#define SYS_CFGCTRL		0x44
#define SYS_CFGCTRL_START	(1 << 31)
#define SYS_CFGCTRL_WRITE	(1 << 30)
#define SYS_CFGCTRL_DCC(n)	(((n) & 0xf) << 26)
#define SYS_CFGCTRL_FUNC(n)	(((n) & 0x3f) << 20)
#define SYS_CFGCTRL_SITE(n)	(((n) & 0x3) << 16)
#define SYS_CFGCTRL_POSITION(n)	(((n) & 0xf) << 12)
#define SYS_CFGCTRL_DEVICE(n)	(((n) & 0xfff) << 0)

#define SYS_CFGSTAT		0x48
#define SYS_CFGSTAT_ERR		(1 << 1)
#define SYS_CFGSTAT_COMPLETE	(1 << 0)

#define VEXPRESS_SITE_MB		0
#define VEXPRESS_SITE_DB1		1
#define VEXPRESS_SITE_DB2		2
#define VEXPRESS_SITE_MASTER		0xf

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
	u32 template[]; /* Keep it last! */
};

struct vexpress_config_bridge_ops {
	struct regmap * (*regmap_init)(struct device *dev, void *context);
	void (*regmap_exit)(struct regmap *regmap, void *context);
};

struct vexpress_config_bridge {
	struct vexpress_config_bridge_ops *ops;
	void *context;
};


static DEFINE_MUTEX(vexpress_config_mutex);
static u32 vexpress_config_site_master = VEXPRESS_SITE_MASTER;


static void vexpress_config_set_master(u32 site)
{
	vexpress_config_site_master = site;
}

static void vexpress_config_lock(void *arg)
{
	mutex_lock(&vexpress_config_mutex);
}

static void vexpress_config_unlock(void *arg)
{
	mutex_unlock(&vexpress_config_mutex);
}


static void vexpress_config_find_prop(struct device_node *node,
		const char *name, u32 *val)
{
	/* Default value */
	*val = 0;

	of_node_get(node);
	while (node) {
		if (of_property_read_u32(node, name, val) == 0) {
			of_node_put(node);
			return;
		}
		node = of_get_next_parent(node);
	}
}

static int vexpress_config_get_topo(struct device_node *node, u32 *site,
		u32 *position, u32 *dcc)
{
	vexpress_config_find_prop(node, "arm,vexpress,site", site);
	if (*site == VEXPRESS_SITE_MASTER)
		*site = vexpress_config_site_master;
	if (WARN_ON(vexpress_config_site_master == VEXPRESS_SITE_MASTER))
		return -EINVAL;
	vexpress_config_find_prop(node, "arm,vexpress,position", position);
	vexpress_config_find_prop(node, "arm,vexpress,dcc", dcc);

	return 0;
}


static void vexpress_config_devres_release(struct device *dev, void *res)
{
	struct vexpress_config_bridge *bridge = dev_get_drvdata(dev->parent);
	struct regmap *regmap = res;

	bridge->ops->regmap_exit(regmap, bridge->context);
}

struct regmap *devm_regmap_init_vexpress_config(struct device *dev)
{
	struct vexpress_config_bridge *bridge;
	struct regmap *regmap;
	struct regmap **res;

	bridge = dev_get_drvdata(dev->parent);
	if (WARN_ON(!bridge))
		return ERR_PTR(-EINVAL);

	res = devres_alloc(vexpress_config_devres_release, sizeof(*res),
			GFP_KERNEL);
	if (!res)
		return ERR_PTR(-ENOMEM);

	regmap = (bridge->ops->regmap_init)(dev, bridge->context);
	if (IS_ERR(regmap)) {
		devres_free(res);
		return regmap;
	}

	*res = regmap;
	devres_add(dev, res);

	return regmap;
}
EXPORT_SYMBOL_GPL(devm_regmap_init_vexpress_config);

static int vexpress_syscfg_exec(struct vexpress_syscfg_func *func,
		int index, bool write, u32 *data)
{
	struct vexpress_syscfg *syscfg = func->syscfg;
	u32 command, status;
	int tries;
	long timeout;

	if (WARN_ON(index >= func->num_templates))
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
	struct vexpress_config_bridge *bridge;
	struct device_node *node;
	int master;
	u32 dt_hbi;

	syscfg = devm_kzalloc(&pdev->dev, sizeof(*syscfg), GFP_KERNEL);
	if (!syscfg)
		return -ENOMEM;
	syscfg->dev = &pdev->dev;
	INIT_LIST_HEAD(&syscfg->funcs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	syscfg->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(syscfg->base))
		return PTR_ERR(syscfg->base);

	bridge = devm_kmalloc(&pdev->dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -ENOMEM;

	bridge->ops = &vexpress_syscfg_bridge_ops;
	bridge->context = syscfg;

	dev_set_drvdata(&pdev->dev, bridge);

	master = readl(syscfg->base + SYS_MISC) & SYS_MISC_MASTERSITE ?
			VEXPRESS_SITE_DB2 : VEXPRESS_SITE_DB1;
	vexpress_config_set_master(master);

	/* Confirm board type against DT property, if available */
	if (of_property_read_u32(of_root, "arm,hbi", &dt_hbi) == 0) {
		u32 id = readl(syscfg->base + (master == VEXPRESS_SITE_DB1 ?
				 SYS_PROCID0 : SYS_PROCID1));
		u32 hbi = (id >> SYS_PROCIDx_HBI_SHIFT) & SYS_HBI_MASK;

		if (WARN_ON(dt_hbi != hbi))
			dev_warn(&pdev->dev, "DT HBI (%x) is not matching hardware (%x)!\n",
					dt_hbi, hbi);
	}

	for_each_compatible_node(node, NULL, "arm,vexpress,config-bus") {
		struct device_node *bridge_np;

		bridge_np = of_parse_phandle(node, "arm,vexpress,config-bridge", 0);
		if (bridge_np != pdev->dev.parent->of_node)
			continue;

		of_platform_populate(node, NULL, NULL, &pdev->dev);
	}

	return 0;
}

static const struct platform_device_id vexpress_syscfg_id_table[] = {
	{ "vexpress-syscfg", },
	{},
};
MODULE_DEVICE_TABLE(platform, vexpress_syscfg_id_table);

static struct platform_driver vexpress_syscfg_driver = {
	.driver.name = "vexpress-syscfg",
	.id_table = vexpress_syscfg_id_table,
	.probe = vexpress_syscfg_probe,
};
module_platform_driver(vexpress_syscfg_driver);
MODULE_LICENSE("GPL v2");
