// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub Global Trace Hub
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/pm_runtime.h>

#include "intel_th.h"
#include "gth.h"

struct gth_device;

/**
 * struct gth_output - GTH view on an output port
 * @gth:	backlink to the GTH device
 * @output:	link to output device's output descriptor
 * @index:	output port number
 * @port_type:	one of GTH_* port type values
 * @master:	bitmap of masters configured for this output
 */
struct gth_output {
	struct gth_device	*gth;
	struct intel_th_output	*output;
	unsigned int		index;
	unsigned int		port_type;
	DECLARE_BITMAP(master, TH_CONFIGURABLE_MASTERS + 1);
};

/**
 * struct gth_device - GTH device
 * @dev:	driver core's device
 * @base:	register window base address
 * @output_group:	attributes describing output ports
 * @master_group:	attributes describing master assignments
 * @output:		output ports
 * @master:		master/output port assignments
 * @gth_lock:		serializes accesses to GTH bits
 */
struct gth_device {
	struct device		*dev;
	void __iomem		*base;

	struct attribute_group	output_group;
	struct attribute_group	master_group;
	struct gth_output	output[TH_POSSIBLE_OUTPUTS];
	signed char		master[TH_CONFIGURABLE_MASTERS + 1];
	spinlock_t		gth_lock;
};

static void gth_output_set(struct gth_device *gth, int port,
			   unsigned int config)
{
	unsigned long reg = port & 4 ? REG_GTH_GTHOPT1 : REG_GTH_GTHOPT0;
	u32 val;
	int shift = (port & 3) * 8;

	val = ioread32(gth->base + reg);
	val &= ~(0xff << shift);
	val |= config << shift;
	iowrite32(val, gth->base + reg);
}

static unsigned int gth_output_get(struct gth_device *gth, int port)
{
	unsigned long reg = port & 4 ? REG_GTH_GTHOPT1 : REG_GTH_GTHOPT0;
	u32 val;
	int shift = (port & 3) * 8;

	val = ioread32(gth->base + reg);
	val &= 0xff << shift;
	val >>= shift;

	return val;
}

static void gth_smcfreq_set(struct gth_device *gth, int port,
			    unsigned int freq)
{
	unsigned long reg = REG_GTH_SMCR0 + ((port / 2) * 4);
	int shift = (port & 1) * 16;
	u32 val;

	val = ioread32(gth->base + reg);
	val &= ~(0xffff << shift);
	val |= freq << shift;
	iowrite32(val, gth->base + reg);
}

static unsigned int gth_smcfreq_get(struct gth_device *gth, int port)
{
	unsigned long reg = REG_GTH_SMCR0 + ((port / 2) * 4);
	int shift = (port & 1) * 16;
	u32 val;

	val = ioread32(gth->base + reg);
	val &= 0xffff << shift;
	val >>= shift;

	return val;
}

/*
 * "masters" attribute group
 */

struct master_attribute {
	struct device_attribute	attr;
	struct gth_device	*gth;
	unsigned int		master;
};

static void
gth_master_set(struct gth_device *gth, unsigned int master, int port)
{
	unsigned int reg = REG_GTH_SWDEST0 + ((master >> 1) & ~3u);
	unsigned int shift = (master & 0x7) * 4;
	u32 val;

	if (master >= 256) {
		reg = REG_GTH_GSWTDEST;
		shift = 0;
	}

	val = ioread32(gth->base + reg);
	val &= ~(0xf << shift);
	if (port >= 0)
		val |= (0x8 | port) << shift;
	iowrite32(val, gth->base + reg);
}

static ssize_t master_attr_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct master_attribute *ma =
		container_of(attr, struct master_attribute, attr);
	struct gth_device *gth = ma->gth;
	size_t count;
	int port;

	spin_lock(&gth->gth_lock);
	port = gth->master[ma->master];
	spin_unlock(&gth->gth_lock);

	if (port >= 0)
		count = snprintf(buf, PAGE_SIZE, "%x\n", port);
	else
		count = snprintf(buf, PAGE_SIZE, "disabled\n");

	return count;
}

static ssize_t master_attr_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct master_attribute *ma =
		container_of(attr, struct master_attribute, attr);
	struct gth_device *gth = ma->gth;
	int old_port, port;

	if (kstrtoint(buf, 10, &port) < 0)
		return -EINVAL;

	if (port >= TH_POSSIBLE_OUTPUTS || port < -1)
		return -EINVAL;

	spin_lock(&gth->gth_lock);

	/* disconnect from the previous output port, if any */
	old_port = gth->master[ma->master];
	if (old_port >= 0) {
		gth->master[ma->master] = -1;
		clear_bit(ma->master, gth->output[old_port].master);

		/*
		 * if the port is active, program this setting,
		 * implies that runtime PM is on
		 */
		if (gth->output[old_port].output->active)
			gth_master_set(gth, ma->master, -1);
	}

	/* connect to the new output port, if any */
	if (port >= 0) {
		/* check if there's a driver for this port */
		if (!gth->output[port].output) {
			count = -ENODEV;
			goto unlock;
		}

		set_bit(ma->master, gth->output[port].master);

		/* if the port is active, program this setting, see above */
		if (gth->output[port].output->active)
			gth_master_set(gth, ma->master, port);
	}

	gth->master[ma->master] = port;

unlock:
	spin_unlock(&gth->gth_lock);

	return count;
}

struct output_attribute {
	struct device_attribute attr;
	struct gth_device	*gth;
	unsigned int		port;
	unsigned int		parm;
};

#define OUTPUT_PARM(_name, _mask, _r, _w, _what)			\
	[TH_OUTPUT_PARM(_name)] = { .name = __stringify(_name),		\
				    .get = gth_ ## _what ## _get,	\
				    .set = gth_ ## _what ## _set,	\
				    .mask = (_mask),			\
				    .readable = (_r),			\
				    .writable = (_w) }

static const struct output_parm {
	const char	*name;
	unsigned int	(*get)(struct gth_device *gth, int port);
	void		(*set)(struct gth_device *gth, int port,
			       unsigned int val);
	unsigned int	mask;
	unsigned int	readable : 1,
			writable : 1;
} output_parms[] = {
	OUTPUT_PARM(port,	0x7,	1, 0, output),
	OUTPUT_PARM(null,	BIT(3),	1, 1, output),
	OUTPUT_PARM(drop,	BIT(4),	1, 1, output),
	OUTPUT_PARM(reset,	BIT(5),	1, 0, output),
	OUTPUT_PARM(flush,	BIT(7),	0, 1, output),
	OUTPUT_PARM(smcfreq,	0xffff,	1, 1, smcfreq),
};

static void
gth_output_parm_set(struct gth_device *gth, int port, unsigned int parm,
		    unsigned int val)
{
	unsigned int config = output_parms[parm].get(gth, port);
	unsigned int mask = output_parms[parm].mask;
	unsigned int shift = __ffs(mask);

	config &= ~mask;
	config |= (val << shift) & mask;
	output_parms[parm].set(gth, port, config);
}

static unsigned int
gth_output_parm_get(struct gth_device *gth, int port, unsigned int parm)
{
	unsigned int config = output_parms[parm].get(gth, port);
	unsigned int mask = output_parms[parm].mask;
	unsigned int shift = __ffs(mask);

	config &= mask;
	config >>= shift;
	return config;
}

/*
 * Reset outputs and sources
 */
static int intel_th_gth_reset(struct gth_device *gth)
{
	u32 reg;
	int port, i;

	reg = ioread32(gth->base + REG_GTH_SCRPD0);
	if (reg & SCRPD_DEBUGGER_IN_USE)
		return -EBUSY;

	/* Always save/restore STH and TU registers in S0ix entry/exit */
	reg |= SCRPD_STH_IS_ENABLED | SCRPD_TRIGGER_IS_ENABLED;
	iowrite32(reg, gth->base + REG_GTH_SCRPD0);

	/* output ports */
	for (port = 0; port < 8; port++) {
		if (gth_output_parm_get(gth, port, TH_OUTPUT_PARM(port)) ==
		    GTH_NONE)
			continue;

		gth_output_set(gth, port, 0);
		gth_smcfreq_set(gth, port, 16);
	}
	/* disable overrides */
	iowrite32(0, gth->base + REG_GTH_DESTOVR);

	/* masters swdest_0~31 and gswdest */
	for (i = 0; i < 33; i++)
		iowrite32(0, gth->base + REG_GTH_SWDEST0 + i * 4);

	/* sources */
	iowrite32(0, gth->base + REG_GTH_SCR);
	iowrite32(0xfc, gth->base + REG_GTH_SCR2);

	return 0;
}

/*
 * "outputs" attribute group
 */

static ssize_t output_attr_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct output_attribute *oa =
		container_of(attr, struct output_attribute, attr);
	struct gth_device *gth = oa->gth;
	size_t count;

	pm_runtime_get_sync(dev);

	spin_lock(&gth->gth_lock);
	count = snprintf(buf, PAGE_SIZE, "%x\n",
			 gth_output_parm_get(gth, oa->port, oa->parm));
	spin_unlock(&gth->gth_lock);

	pm_runtime_put(dev);

	return count;
}

static ssize_t output_attr_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct output_attribute *oa =
		container_of(attr, struct output_attribute, attr);
	struct gth_device *gth = oa->gth;
	unsigned int config;

	if (kstrtouint(buf, 16, &config) < 0)
		return -EINVAL;

	pm_runtime_get_sync(dev);

	spin_lock(&gth->gth_lock);
	gth_output_parm_set(gth, oa->port, oa->parm, config);
	spin_unlock(&gth->gth_lock);

	pm_runtime_put(dev);

	return count;
}

static int intel_th_master_attributes(struct gth_device *gth)
{
	struct master_attribute *master_attrs;
	struct attribute **attrs;
	int i, nattrs = TH_CONFIGURABLE_MASTERS + 2;

	attrs = devm_kcalloc(gth->dev, nattrs, sizeof(void *), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	master_attrs = devm_kcalloc(gth->dev, nattrs,
				    sizeof(struct master_attribute),
				    GFP_KERNEL);
	if (!master_attrs)
		return -ENOMEM;

	for (i = 0; i < TH_CONFIGURABLE_MASTERS + 1; i++) {
		char *name;

		name = devm_kasprintf(gth->dev, GFP_KERNEL, "%d%s", i,
				      i == TH_CONFIGURABLE_MASTERS ? "+" : "");
		if (!name)
			return -ENOMEM;

		master_attrs[i].attr.attr.name = name;
		master_attrs[i].attr.attr.mode = S_IRUGO | S_IWUSR;
		master_attrs[i].attr.show = master_attr_show;
		master_attrs[i].attr.store = master_attr_store;

		sysfs_attr_init(&master_attrs[i].attr.attr);
		attrs[i] = &master_attrs[i].attr.attr;

		master_attrs[i].gth = gth;
		master_attrs[i].master = i;
	}

	gth->master_group.name	= "masters";
	gth->master_group.attrs = attrs;

	return sysfs_create_group(&gth->dev->kobj, &gth->master_group);
}

static int intel_th_output_attributes(struct gth_device *gth)
{
	struct output_attribute *out_attrs;
	struct attribute **attrs;
	int i, j, nouts = TH_POSSIBLE_OUTPUTS;
	int nparms = ARRAY_SIZE(output_parms);
	int nattrs = nouts * nparms + 1;

	attrs = devm_kcalloc(gth->dev, nattrs, sizeof(void *), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	out_attrs = devm_kcalloc(gth->dev, nattrs,
				 sizeof(struct output_attribute),
				 GFP_KERNEL);
	if (!out_attrs)
		return -ENOMEM;

	for (i = 0; i < nouts; i++) {
		for (j = 0; j < nparms; j++) {
			unsigned int idx = i * nparms + j;
			char *name;

			name = devm_kasprintf(gth->dev, GFP_KERNEL, "%d_%s", i,
					      output_parms[j].name);
			if (!name)
				return -ENOMEM;

			out_attrs[idx].attr.attr.name = name;

			if (output_parms[j].readable) {
				out_attrs[idx].attr.attr.mode |= S_IRUGO;
				out_attrs[idx].attr.show = output_attr_show;
			}

			if (output_parms[j].writable) {
				out_attrs[idx].attr.attr.mode |= S_IWUSR;
				out_attrs[idx].attr.store = output_attr_store;
			}

			sysfs_attr_init(&out_attrs[idx].attr.attr);
			attrs[idx] = &out_attrs[idx].attr.attr;

			out_attrs[idx].gth = gth;
			out_attrs[idx].port = i;
			out_attrs[idx].parm = j;
		}
	}

	gth->output_group.name	= "outputs";
	gth->output_group.attrs = attrs;

	return sysfs_create_group(&gth->dev->kobj, &gth->output_group);
}

/**
 * intel_th_gth_disable() - disable tracing to an output device
 * @thdev:	GTH device
 * @output:	output device's descriptor
 *
 * This will deconfigure all masters set to output to this device,
 * disable tracing using force storeEn off signal and wait for the
 * "pipeline empty" bit for corresponding output port.
 */
static void intel_th_gth_disable(struct intel_th_device *thdev,
				 struct intel_th_output *output)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);
	unsigned long count;
	int master;
	u32 reg;

	spin_lock(&gth->gth_lock);
	output->active = false;

	for_each_set_bit(master, gth->output[output->port].master,
			 TH_CONFIGURABLE_MASTERS) {
		gth_master_set(gth, master, -1);
	}
	spin_unlock(&gth->gth_lock);

	iowrite32(0, gth->base + REG_GTH_SCR);
	iowrite32(0xfd, gth->base + REG_GTH_SCR2);

	/* wait on pipeline empty for the given port */
	for (reg = 0, count = GTH_PLE_WAITLOOP_DEPTH;
	     count && !(reg & BIT(output->port)); count--) {
		reg = ioread32(gth->base + REG_GTH_STAT);
		cpu_relax();
	}

	/* clear force capture done for next captures */
	iowrite32(0xfc, gth->base + REG_GTH_SCR2);

	if (!count)
		dev_dbg(&thdev->dev, "timeout waiting for GTH[%d] PLE\n",
			output->port);

	reg = ioread32(gth->base + REG_GTH_SCRPD0);
	reg &= ~output->scratchpad;
	iowrite32(reg, gth->base + REG_GTH_SCRPD0);
}

static void gth_tscu_resync(struct gth_device *gth)
{
	u32 reg;

	reg = ioread32(gth->base + REG_TSCU_TSUCTRL);
	reg &= ~TSUCTRL_CTCRESYNC;
	iowrite32(reg, gth->base + REG_TSCU_TSUCTRL);
}

/**
 * intel_th_gth_enable() - enable tracing to an output device
 * @thdev:	GTH device
 * @output:	output device's descriptor
 *
 * This will configure all masters set to output to this device and
 * enable tracing using force storeEn signal.
 */
static void intel_th_gth_enable(struct intel_th_device *thdev,
				struct intel_th_output *output)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);
	struct intel_th *th = to_intel_th(thdev);
	u32 scr = 0xfc0000, scrpd;
	int master;

	spin_lock(&gth->gth_lock);
	for_each_set_bit(master, gth->output[output->port].master,
			 TH_CONFIGURABLE_MASTERS + 1) {
		gth_master_set(gth, master, output->port);
	}

	if (output->multiblock)
		scr |= 0xff;

	output->active = true;
	spin_unlock(&gth->gth_lock);

	if (INTEL_TH_CAP(th, tscu_enable))
		gth_tscu_resync(gth);

	scrpd = ioread32(gth->base + REG_GTH_SCRPD0);
	scrpd |= output->scratchpad;
	iowrite32(scrpd, gth->base + REG_GTH_SCRPD0);

	iowrite32(scr, gth->base + REG_GTH_SCR);
	iowrite32(0, gth->base + REG_GTH_SCR2);
}

/**
 * intel_th_gth_assign() - assign output device to a GTH output port
 * @thdev:	GTH device
 * @othdev:	output device
 *
 * This will match a given output device parameters against present
 * output ports on the GTH and fill out relevant bits in output device's
 * descriptor.
 *
 * Return:	0 on success, -errno on error.
 */
static int intel_th_gth_assign(struct intel_th_device *thdev,
			       struct intel_th_device *othdev)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);
	int i, id;

	if (thdev->host_mode)
		return -EBUSY;

	if (othdev->type != INTEL_TH_OUTPUT)
		return -EINVAL;

	for (i = 0, id = 0; i < TH_POSSIBLE_OUTPUTS; i++) {
		if (gth->output[i].port_type != othdev->output.type)
			continue;

		if (othdev->id == -1 || othdev->id == id)
			goto found;

		id++;
	}

	return -ENOENT;

found:
	spin_lock(&gth->gth_lock);
	othdev->output.port = i;
	othdev->output.active = false;
	gth->output[i].output = &othdev->output;
	spin_unlock(&gth->gth_lock);

	return 0;
}

/**
 * intel_th_gth_unassign() - deassociate an output device from its output port
 * @thdev:	GTH device
 * @othdev:	output device
 */
static void intel_th_gth_unassign(struct intel_th_device *thdev,
				  struct intel_th_device *othdev)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);
	int port = othdev->output.port;
	int master;

	if (thdev->host_mode)
		return;

	spin_lock(&gth->gth_lock);
	othdev->output.port = -1;
	othdev->output.active = false;
	gth->output[port].output = NULL;
	for (master = 0; master <= TH_CONFIGURABLE_MASTERS; master++)
		if (gth->master[master] == port)
			gth->master[master] = -1;
	spin_unlock(&gth->gth_lock);
}

static int
intel_th_gth_set_output(struct intel_th_device *thdev, unsigned int master)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);
	int port = 0; /* FIXME: make default output configurable */

	/*
	 * everything above TH_CONFIGURABLE_MASTERS is controlled by the
	 * same register
	 */
	if (master > TH_CONFIGURABLE_MASTERS)
		master = TH_CONFIGURABLE_MASTERS;

	spin_lock(&gth->gth_lock);
	if (gth->master[master] == -1) {
		set_bit(master, gth->output[port].master);
		gth->master[master] = port;
	}
	spin_unlock(&gth->gth_lock);

	return 0;
}

static int intel_th_gth_probe(struct intel_th_device *thdev)
{
	struct device *dev = &thdev->dev;
	struct intel_th *th = dev_get_drvdata(dev->parent);
	struct gth_device *gth;
	struct resource *res;
	void __iomem *base;
	int i, ret;

	res = intel_th_device_get_resource(thdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	gth = devm_kzalloc(dev, sizeof(*gth), GFP_KERNEL);
	if (!gth)
		return -ENOMEM;

	gth->dev = dev;
	gth->base = base;
	spin_lock_init(&gth->gth_lock);

	dev_set_drvdata(dev, gth);

	/*
	 * Host mode can be signalled via SW means or via SCRPD_DEBUGGER_IN_USE
	 * bit. Either way, don't reset HW in this case, and don't export any
	 * capture configuration attributes. Also, refuse to assign output
	 * drivers to ports, see intel_th_gth_assign().
	 */
	if (thdev->host_mode)
		return 0;

	ret = intel_th_gth_reset(gth);
	if (ret) {
		if (ret != -EBUSY)
			return ret;

		thdev->host_mode = true;

		return 0;
	}

	for (i = 0; i < TH_CONFIGURABLE_MASTERS + 1; i++)
		gth->master[i] = -1;

	for (i = 0; i < TH_POSSIBLE_OUTPUTS; i++) {
		gth->output[i].gth = gth;
		gth->output[i].index = i;
		gth->output[i].port_type =
			gth_output_parm_get(gth, i, TH_OUTPUT_PARM(port));
		if (gth->output[i].port_type == GTH_NONE)
			continue;

		ret = intel_th_output_enable(th, gth->output[i].port_type);
		/* -ENODEV is ok, we just won't have that device enumerated */
		if (ret && ret != -ENODEV)
			return ret;
	}

	if (intel_th_output_attributes(gth) ||
	    intel_th_master_attributes(gth)) {
		pr_warn("Can't initialize sysfs attributes\n");

		if (gth->output_group.attrs)
			sysfs_remove_group(&gth->dev->kobj, &gth->output_group);
		return -ENOMEM;
	}

	return 0;
}

static void intel_th_gth_remove(struct intel_th_device *thdev)
{
	struct gth_device *gth = dev_get_drvdata(&thdev->dev);

	sysfs_remove_group(&gth->dev->kobj, &gth->output_group);
	sysfs_remove_group(&gth->dev->kobj, &gth->master_group);
}

static struct intel_th_driver intel_th_gth_driver = {
	.probe		= intel_th_gth_probe,
	.remove		= intel_th_gth_remove,
	.assign		= intel_th_gth_assign,
	.unassign	= intel_th_gth_unassign,
	.set_output	= intel_th_gth_set_output,
	.enable		= intel_th_gth_enable,
	.disable	= intel_th_gth_disable,
	.driver	= {
		.name	= "gth",
		.owner	= THIS_MODULE,
	},
};

module_driver(intel_th_gth_driver,
	      intel_th_driver_register,
	      intel_th_driver_unregister);

MODULE_ALIAS("intel_th_switch");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub Global Trace Hub driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
