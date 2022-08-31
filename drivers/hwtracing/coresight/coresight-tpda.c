// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/bitmap.h>
#include <linux/of.h>
#include <linux/coresight.h>

#include "coresight-priv.h"
#include "coresight-common.h"

#define tpda_writel(drvdata, val, off)	__raw_writel((val), drvdata->base + off)
#define tpda_readl(drvdata, off)	__raw_readl(drvdata->base + off)

#define TPDA_LOCK(drvdata)						\
do {									\
	mb(); /* ensure configuration take effect before we lock it */	\
	tpda_writel(drvdata, 0x0, CORESIGHT_LAR);			\
} while (0)
#define TPDA_UNLOCK(drvdata)						\
do {									\
	tpda_writel(drvdata, CORESIGHT_UNLOCK, CORESIGHT_LAR);		\
	mb(); /* ensure unlock take effect before we configure */	\
} while (0)

#define TPDA_CR			(0x000)
#define TPDA_Pn_CR(n)		(0x004 + (n * 4))
#define TPDA_FPID_CR		(0x084)
#define TPDA_FREQREQ_VAL	(0x088)
#define TPDA_SYNCR		(0x08C)
#define TPDA_FLUSH_CR		(0x090)
#define TPDA_FLUSH_SR		(0x094)
#define TPDA_FLUSH_ERR		(0x098)

#define TPDA_MAX_INPORTS	32

DEFINE_CORESIGHT_DEVLIST(tpda_devs, "tpda");

struct tpda_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	struct mutex		lock;
	bool			enable;
	uint32_t		atid;
	uint32_t		bc_esize[TPDA_MAX_INPORTS];
	uint32_t		tc_esize[TPDA_MAX_INPORTS];
	uint32_t		dsb_esize[TPDA_MAX_INPORTS];
	uint32_t		cmb_esize[TPDA_MAX_INPORTS];
	bool			trig_async;
	bool			trig_flag_ts;
	bool			trig_freq;
	bool			freq_ts;
	uint32_t		freq_req_val;
	bool			freq_req;
	bool			cmbchan_mode;
};

static void __tpda_enable_pre_port(struct tpda_drvdata *drvdata)
{
	uint32_t val;

	val = tpda_readl(drvdata, TPDA_CR);
	/* Set the master id */
	val = val & ~(0x7F << 13);
	val = val & ~(0x7F << 6);
	val |= (drvdata->atid << 6);
	if (drvdata->trig_async)
		val = val | BIT(5);
	else
		val = val & ~BIT(5);
	if (drvdata->trig_flag_ts)
		val = val | BIT(4);
	else
		val = val & ~BIT(4);
	if (drvdata->trig_freq)
		val = val | BIT(3);
	else
		val = val & ~BIT(3);
	if (drvdata->freq_ts)
		val = val | BIT(2);
	else
		val = val & ~BIT(2);
	if (drvdata->cmbchan_mode)
		val = val | BIT(20);
	else
		val = val & ~BIT(20);
	tpda_writel(drvdata, val, TPDA_CR);

	/*
	 * If FLRIE bit is set, set the master and channel
	 * id as zero
	 */
	if (BVAL(tpda_readl(drvdata, TPDA_CR), 4))
		tpda_writel(drvdata, 0x0, TPDA_FPID_CR);
}

static void __tpda_enable_port(struct tpda_drvdata *drvdata, int port)
{
	uint32_t val;

	val = tpda_readl(drvdata, TPDA_Pn_CR(port));
	if (drvdata->bc_esize[port] == 32)
		val = val & ~BIT(4);
	else if (drvdata->bc_esize[port] == 64)
		val = val | BIT(4);

	if (drvdata->tc_esize[port] == 32)
		val = val & ~BIT(5);
	else if (drvdata->tc_esize[port] == 64)
		val = val | BIT(5);

	if (drvdata->dsb_esize[port] == 32)
		val = val & ~BIT(8);
	else if (drvdata->dsb_esize[port] == 64)
		val = val | BIT(8);

	val = val & ~(0x3 << 6);
	if (drvdata->cmb_esize[port] == 8)
		val &= ~(0x3 << 6);
	else if (drvdata->cmb_esize[port] == 32)
		val |= (0x1 << 6);
	else if (drvdata->cmb_esize[port] == 64)
		val |= (0x2 << 6);

	/* Set the hold time */
	val = val & ~(0x7 << 1);
	val |= (0x5 << 1);
	tpda_writel(drvdata, val, TPDA_Pn_CR(port));
	/* Enable the port */
	val = val | BIT(0);
	tpda_writel(drvdata, val, TPDA_Pn_CR(port));
}

static void __tpda_enable_post_port(struct tpda_drvdata *drvdata)
{
	uint32_t val;

	val = tpda_readl(drvdata, TPDA_SYNCR);
	/* Clear the mode */
	val = val & ~BIT(12);
	/* Program the counter value */
	val = val | 0xFFF;
	tpda_writel(drvdata, val, TPDA_SYNCR);

	if (drvdata->freq_req_val)
		tpda_writel(drvdata, drvdata->freq_req_val, TPDA_FREQREQ_VAL);
	else
		tpda_writel(drvdata, 0x0, TPDA_FREQREQ_VAL);

	val = tpda_readl(drvdata, TPDA_CR);
	if (drvdata->freq_req)
		val = val | BIT(1);
	else
		val = val & ~BIT(1);
	tpda_writel(drvdata, val, TPDA_CR);
}

static void __tpda_enable(struct tpda_drvdata *drvdata, int port)
{
	TPDA_UNLOCK(drvdata);

	if (!drvdata->enable)
		__tpda_enable_pre_port(drvdata);

	__tpda_enable_port(drvdata, port);

	if (!drvdata->enable)
		__tpda_enable_post_port(drvdata);

	TPDA_LOCK(drvdata);
}

static int tpda_enable(struct coresight_device *csdev, int inport, int outport)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	mutex_lock(&drvdata->lock);
	__tpda_enable(drvdata, inport);
	drvdata->enable = true;
	mutex_unlock(&drvdata->lock);

	dev_info(drvdata->dev, "TPDA inport %d enabled\n", inport);
	return 0;
}

static void __tpda_disable(struct tpda_drvdata *drvdata, int port)
{
	uint32_t val;

	TPDA_UNLOCK(drvdata);

	val = tpda_readl(drvdata, TPDA_Pn_CR(port));
	val = val & ~BIT(0);
	tpda_writel(drvdata, val, TPDA_Pn_CR(port));

	TPDA_LOCK(drvdata);
}

static void tpda_disable(struct coresight_device *csdev, int inport,
			   int outport)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	mutex_lock(&drvdata->lock);
	__tpda_disable(drvdata, inport);
	drvdata->enable = false;
	mutex_unlock(&drvdata->lock);

	dev_info(drvdata->dev, "TPDA inport %d disabled\n", inport);
}

static const struct coresight_ops_link tpda_link_ops = {
	.enable		= tpda_enable,
	.disable	= tpda_disable,
};

static const struct coresight_ops tpda_cs_ops = {
	.link_ops	= &tpda_link_ops,
};

static ssize_t trig_async_enable_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->trig_async);
}

static ssize_t trig_async_enable_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf,
					    size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->trig_async = true;
	else
		drvdata->trig_async = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(trig_async_enable);

static ssize_t trig_flag_ts_enable_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->trig_flag_ts);
}

static ssize_t trig_flag_ts_enable_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf,
					      size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->trig_flag_ts = true;
	else
		drvdata->trig_flag_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(trig_flag_ts_enable);

static ssize_t trig_freq_enable_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->trig_freq);
}

static ssize_t trig_freq_enable_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->trig_freq = true;
	else
		drvdata->trig_freq = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(trig_freq_enable);

static ssize_t freq_ts_enable_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->freq_ts);
}

static ssize_t freq_ts_enable_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->freq_ts = true;
	else
		drvdata->freq_ts = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(freq_ts_enable);

static ssize_t freq_req_val_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->freq_req_val;

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t freq_req_val_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf,
				       size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	drvdata->freq_req_val = val;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(freq_req_val);

static ssize_t freq_req_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->freq_req);
}

static ssize_t freq_req_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf,
				   size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->freq_req = true;
	else
		drvdata->freq_req = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(freq_req);

static ssize_t global_flush_req_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	mutex_lock(&drvdata->lock);

	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDA_UNLOCK(drvdata);
	val = tpda_readl(drvdata, TPDA_CR);
	TPDA_LOCK(drvdata);

	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t global_flush_req_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf,
					   size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);

	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDA_UNLOCK(drvdata);
		val = tpda_readl(drvdata, TPDA_CR);
		val = val | BIT(0);
		tpda_writel(drvdata, val, TPDA_CR);
		TPDA_LOCK(drvdata);
	}

	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(global_flush_req);

static ssize_t port_flush_req_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	mutex_lock(&drvdata->lock);

	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	TPDA_UNLOCK(drvdata);
	val = tpda_readl(drvdata, TPDA_FLUSH_CR);
	TPDA_LOCK(drvdata);

	mutex_unlock(&drvdata->lock);
	return scnprintf(buf, PAGE_SIZE, "%lx\n", val);
}

static ssize_t port_flush_req_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf,
					 size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (kstrtoul(buf, 16, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);

	if (!drvdata->enable) {
		mutex_unlock(&drvdata->lock);
		return -EPERM;
	}

	if (val) {
		TPDA_UNLOCK(drvdata);
		tpda_writel(drvdata, val, TPDA_FLUSH_CR);
		TPDA_LOCK(drvdata);
	}

	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(port_flush_req);

static ssize_t cmbchan_mode_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)drvdata->cmbchan_mode);
}

static ssize_t cmbchan_mode_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf,
					      size_t size)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(dev->parent);
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	mutex_lock(&drvdata->lock);
	if (val)
		drvdata->cmbchan_mode = true;
	else
		drvdata->cmbchan_mode = false;
	mutex_unlock(&drvdata->lock);
	return size;
}
static DEVICE_ATTR_RW(cmbchan_mode);

static struct attribute *tpda_attrs[] = {
	&dev_attr_trig_async_enable.attr,
	&dev_attr_trig_flag_ts_enable.attr,
	&dev_attr_trig_freq_enable.attr,
	&dev_attr_freq_ts_enable.attr,
	&dev_attr_freq_req_val.attr,
	&dev_attr_freq_req.attr,
	&dev_attr_global_flush_req.attr,
	&dev_attr_port_flush_req.attr,
	&dev_attr_cmbchan_mode.attr,
	NULL,
};

static struct attribute_group tpda_attr_grp = {
	.attrs = tpda_attrs,
};

static const struct attribute_group *tpda_attr_grps[] = {
	&tpda_attr_grp,
	NULL,
};

static int tpda_parse_tc(struct tpda_drvdata *drvdata)
{
	int len, port, i;
	const __be32 *prop;
	struct device_node *node = drvdata->dev->of_node;

	prop = of_get_property(node, "qcom,tc-elem-size", &len);
	if (prop) {
		len /= sizeof(__be32);
		if (len < 2 || len > 63 || len % 2 != 0) {
			dev_err(drvdata->dev,
				"Dataset TC width entries are wrong\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++) {
			port = be32_to_cpu(prop[i++]);
			if (port >= TPDA_MAX_INPORTS) {
				dev_err(drvdata->dev,
					"Wrong port specified for TC\n");
				return -EINVAL;
			}
			drvdata->tc_esize[port] = be32_to_cpu(prop[i]);
		}
	}

	return 0;
}

static int tpda_parse_bc(struct tpda_drvdata *drvdata)
{
	int len, port, i;
	const __be32 *prop;
	struct device_node *node = drvdata->dev->of_node;

	prop = of_get_property(node, "qcom,bc-elem-size", &len);
	if (prop) {
		len /= sizeof(__be32);
		if (len < 2 || len > 63 || len % 2 != 0) {
			dev_err(drvdata->dev,
				"Dataset BC width entries are wrong\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++) {
			port = be32_to_cpu(prop[i++]);
			if (port >= TPDA_MAX_INPORTS) {
				dev_err(drvdata->dev,
					"Wrong port specified for BC\n");
				return -EINVAL;
			}
			drvdata->bc_esize[port] = be32_to_cpu(prop[i]);
		}
	}

	return 0;
}

static int tpda_parse_dsb(struct tpda_drvdata *drvdata)
{
	int len, port, i;
	const __be32 *prop;
	struct device_node *node = drvdata->dev->of_node;

	prop = of_get_property(node, "qcom,dsb-elem-size", &len);
	if (prop) {
		len /= sizeof(__be32);
		if (len < 2 || len > 63 || len % 2 != 0) {
			dev_err(drvdata->dev,
				"Dataset DSB width entries are wrong\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++) {
			port = be32_to_cpu(prop[i++]);
			if (port >= TPDA_MAX_INPORTS) {
				dev_err(drvdata->dev,
					"Wrong port specified for DSB\n");
				return -EINVAL;
			}
			drvdata->dsb_esize[port] = be32_to_cpu(prop[i]);
		}
	}

	return 0;
}

static int tpda_parse_cmb(struct tpda_drvdata *drvdata)
{
	int len, port, i;
	const __be32 *prop;
	struct device_node *node = drvdata->dev->of_node;

	prop = of_get_property(node, "qcom,cmb-elem-size", &len);
	if (prop) {
		len /= sizeof(__be32);
		if (len < 2 || len > 63 || len % 2 != 0) {
			dev_err(drvdata->dev,
				"Dataset CMB width entries are wrong\n");
			return -EINVAL;
		}

		for (i = 0; i < len; i++) {
			port = be32_to_cpu(prop[i++]);
			if (port >= TPDA_MAX_INPORTS) {
				dev_err(drvdata->dev,
					"Wrong port specified for CMB\n");
				return -EINVAL;
			}
			drvdata->cmb_esize[port] = be32_to_cpu(prop[i]);
		}
	}

	return 0;
}

static int tpda_parse_of_data(struct tpda_drvdata *drvdata)
{
	int ret;
	struct device_node *node = drvdata->dev->of_node;

	ret = of_property_read_u32(node, "qcom,tpda-atid", &drvdata->atid);
	if (ret) {
		dev_err(drvdata->dev, "TPDA ATID is not specified\n");
		return -EINVAL;
	}

	ret = tpda_parse_tc(drvdata);
	if (ret) {
		dev_err(drvdata->dev, "Dataset TC width entries are wrong\n");
		return -EINVAL;
	}

	ret = tpda_parse_bc(drvdata);
	if (ret) {
		dev_err(drvdata->dev, "Dataset BC width entries are wrong\n");
		return -EINVAL;
	}

	ret = tpda_parse_dsb(drvdata);
	if (ret) {
		dev_err(drvdata->dev, "Dataset DSB width entries are wrong\n");
		return -EINVAL;
	}

	ret = tpda_parse_cmb(drvdata);
	if (ret) {
		dev_err(drvdata->dev, "Dataset CMB width entries are wrong\n");
		return -EINVAL;
	}

	return 0;
}

static void tpda_init_default_data(struct tpda_drvdata *drvdata)
{
	drvdata->freq_ts = true;
}

static bool coresight_authstatus_enabled(void __iomem *addr)
{
	int ret;
	unsigned int auth_val;

	if (!addr)
		return false;

	auth_val = readl_relaxed(addr + CORESIGHT_AUTHSTATUS);

	if ((BMVAL(auth_val, 0, 1) == 0x2) ||
		(BMVAL(auth_val, 2, 3) == 0x2) ||
		(BMVAL(auth_val, 4, 5) == 0x2) ||
		(BMVAL(auth_val, 6, 7) == 0x2))
		ret = false;
	else
		ret = true;

	return ret;
}

static int tpda_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata;
	struct tpda_drvdata *drvdata;
	struct coresight_desc desc = { 0 };

	desc.name = coresight_alloc_device_name(&tpda_devs, dev);
	if (!desc.name)
		return -ENOMEM;
	pdata = coresight_get_platform_data(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);
	adev->dev.platform_data = pdata;

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	drvdata->base = devm_ioremap_resource(dev, &adev->res);
	if (!drvdata->base)
		return -ENOMEM;

	mutex_init(&drvdata->lock);

	ret = tpda_parse_of_data(drvdata);
	if (ret)
		return ret;

	if (!coresight_authstatus_enabled(drvdata->base))
		goto err;

	tpda_init_default_data(drvdata);

	desc.type = CORESIGHT_DEV_TYPE_LINK;
	desc.subtype.link_subtype = CORESIGHT_DEV_SUBTYPE_LINK_MERG;
	desc.ops = &tpda_cs_ops;
	desc.pdata = adev->dev.platform_data;
	desc.dev = &adev->dev;
	desc.groups = tpda_attr_grps;
	drvdata->csdev = coresight_register(&desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	pm_runtime_put(&adev->dev);

	dev_dbg(drvdata->dev, "TPDA initialized\n");
	return 0;
err:
	return -EPERM;
}

static void __exit tpda_remove(struct amba_device *adev)
{
	struct tpda_drvdata *drvdata = dev_get_drvdata(&adev->dev);

	coresight_unregister(drvdata->csdev);
}

static struct amba_id tpda_ids[] = {
	{
		.id     = 0x0003b969,
		.mask   = 0x0003ffff,
		.data	= "TPDA",
	},
	{ 0, 0},
};
MODULE_DEVICE_TABLE(amba, tpda_ids);

static struct amba_driver tpda_driver = {
	.drv = {
		.name   = "coresight-tpda",
		.owner	= THIS_MODULE,
		.suppress_bind_attrs = true,
	},
	.probe          = tpda_probe,
	.remove		= tpda_remove,
	.id_table	= tpda_ids,
};

module_amba_driver(tpda_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Trace, Profiling & Diagnostic Aggregator driver");
