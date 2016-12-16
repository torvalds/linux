/* Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Description: CoreSight System Trace Macrocell driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Initial implementation by Pratik Patel
 * (C) 2014-2015 Pratik Patel <pratikp@codeaurora.org>
 *
 * Serious refactoring, code cleanup and upgrading to the Coresight upstream
 * framework by Mathieu Poirier
 * (C) 2015-2016 Mathieu Poirier <mathieu.poirier@linaro.org>
 *
 * Guaranteed timing and support for various packet type coming from the
 * generic STM API by Chunyan Zhang
 * (C) 2015-2016 Chunyan Zhang <zhang.chunyan@linaro.org>
 */
#include <asm/local.h>
#include <linux/amba/bus.h>
#include <linux/bitmap.h>
#include <linux/clk.h>
#include <linux/coresight.h>
#include <linux/coresight-stm.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/of_address.h>
#include <linux/perf_event.h>
#include <linux/pm_runtime.h>
#include <linux/stm.h>

#include "coresight-priv.h"

#define STMDMASTARTR			0xc04
#define STMDMASTOPR			0xc08
#define STMDMASTATR			0xc0c
#define STMDMACTLR			0xc10
#define STMDMAIDR			0xcfc
#define STMHEER				0xd00
#define STMHETER			0xd20
#define STMHEBSR			0xd60
#define STMHEMCR			0xd64
#define STMHEMASTR			0xdf4
#define STMHEFEAT1R			0xdf8
#define STMHEIDR			0xdfc
#define STMSPER				0xe00
#define STMSPTER			0xe20
#define STMPRIVMASKR			0xe40
#define STMSPSCR			0xe60
#define STMSPMSCR			0xe64
#define STMSPOVERRIDER			0xe68
#define STMSPMOVERRIDER			0xe6c
#define STMSPTRIGCSR			0xe70
#define STMTCSR				0xe80
#define STMTSSTIMR			0xe84
#define STMTSFREQR			0xe8c
#define STMSYNCR			0xe90
#define STMAUXCR			0xe94
#define STMSPFEAT1R			0xea0
#define STMSPFEAT2R			0xea4
#define STMSPFEAT3R			0xea8
#define STMITTRIGGER			0xee8
#define STMITATBDATA0			0xeec
#define STMITATBCTR2			0xef0
#define STMITATBID			0xef4
#define STMITATBCTR0			0xef8

#define STM_32_CHANNEL			32
#define BYTES_PER_CHANNEL		256
#define STM_TRACE_BUF_SIZE		4096
#define STM_SW_MASTER_END		127

/* Register bit definition */
#define STMTCSR_BUSY_BIT		23
/* Reserve the first 10 channels for kernel usage */
#define STM_CHANNEL_OFFSET		0

enum stm_pkt_type {
	STM_PKT_TYPE_DATA	= 0x98,
	STM_PKT_TYPE_FLAG	= 0xE8,
	STM_PKT_TYPE_TRIG	= 0xF8,
};

#define stm_channel_addr(drvdata, ch)	(drvdata->chs.base +	\
					(ch * BYTES_PER_CHANNEL))
#define stm_channel_off(type, opts)	(type & ~opts)

static int boot_nr_channel;

/*
 * Not really modular but using module_param is the easiest way to
 * remain consistent with existing use cases for now.
 */
module_param_named(
	boot_nr_channel, boot_nr_channel, int, S_IRUGO
);

/**
 * struct channel_space - central management entity for extended ports
 * @base:		memory mapped base address where channels start.
 * @guaraneed:		is the channel delivery guaranteed.
 */
struct channel_space {
	void __iomem		*base;
	unsigned long		*guaranteed;
};

/**
 * struct stm_drvdata - specifics associated to an STM component
 * @base:		memory mapped base address for this component.
 * @dev:		the device entity associated to this component.
 * @atclk:		optional clock for the core parts of the STM.
 * @csdev:		component vitals needed by the framework.
 * @spinlock:		only one at a time pls.
 * @chs:		the channels accociated to this STM.
 * @stm:		structure associated to the generic STM interface.
 * @mode:		this tracer's mode, i.e sysFS, or disabled.
 * @traceid:		value of the current ID for this component.
 * @write_bytes:	Maximus bytes this STM can write at a time.
 * @stmsper:		settings for register STMSPER.
 * @stmspscr:		settings for register STMSPSCR.
 * @numsp:		the total number of stimulus port support by this STM.
 * @stmheer:		settings for register STMHEER.
 * @stmheter:		settings for register STMHETER.
 * @stmhebsr:		settings for register STMHEBSR.
 */
struct stm_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct clk		*atclk;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	struct channel_space	chs;
	struct stm_data		stm;
	local_t			mode;
	u8			traceid;
	u32			write_bytes;
	u32			stmsper;
	u32			stmspscr;
	u32			numsp;
	u32			stmheer;
	u32			stmheter;
	u32			stmhebsr;
};

static void stm_hwevent_enable_hw(struct stm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	writel_relaxed(drvdata->stmhebsr, drvdata->base + STMHEBSR);
	writel_relaxed(drvdata->stmheter, drvdata->base + STMHETER);
	writel_relaxed(drvdata->stmheer, drvdata->base + STMHEER);
	writel_relaxed(0x01 |	/* Enable HW event tracing */
		       0x04,	/* Error detection on event tracing */
		       drvdata->base + STMHEMCR);

	CS_LOCK(drvdata->base);
}

static void stm_port_enable_hw(struct stm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);
	/* ATB trigger enable on direct writes to TRIG locations */
	writel_relaxed(0x10,
		       drvdata->base + STMSPTRIGCSR);
	writel_relaxed(drvdata->stmspscr, drvdata->base + STMSPSCR);
	writel_relaxed(drvdata->stmsper, drvdata->base + STMSPER);

	CS_LOCK(drvdata->base);
}

static void stm_enable_hw(struct stm_drvdata *drvdata)
{
	if (drvdata->stmheer)
		stm_hwevent_enable_hw(drvdata);

	stm_port_enable_hw(drvdata);

	CS_UNLOCK(drvdata->base);

	/* 4096 byte between synchronisation packets */
	writel_relaxed(0xFFF, drvdata->base + STMSYNCR);
	writel_relaxed((drvdata->traceid << 16 | /* trace id */
			0x02 |			 /* timestamp enable */
			0x01),			 /* global STM enable */
			drvdata->base + STMTCSR);

	CS_LOCK(drvdata->base);
}

static int stm_enable(struct coresight_device *csdev,
		      struct perf_event_attr *attr, u32 mode)
{
	u32 val;
	struct stm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	if (mode != CS_MODE_SYSFS)
		return -EINVAL;

	val = local_cmpxchg(&drvdata->mode, CS_MODE_DISABLED, mode);

	/* Someone is already using the tracer */
	if (val)
		return -EBUSY;

	pm_runtime_get_sync(drvdata->dev);

	spin_lock(&drvdata->spinlock);
	stm_enable_hw(drvdata);
	spin_unlock(&drvdata->spinlock);

	dev_info(drvdata->dev, "STM tracing enabled\n");
	return 0;
}

static void stm_hwevent_disable_hw(struct stm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	writel_relaxed(0x0, drvdata->base + STMHEMCR);
	writel_relaxed(0x0, drvdata->base + STMHEER);
	writel_relaxed(0x0, drvdata->base + STMHETER);

	CS_LOCK(drvdata->base);
}

static void stm_port_disable_hw(struct stm_drvdata *drvdata)
{
	CS_UNLOCK(drvdata->base);

	writel_relaxed(0x0, drvdata->base + STMSPER);
	writel_relaxed(0x0, drvdata->base + STMSPTRIGCSR);

	CS_LOCK(drvdata->base);
}

static void stm_disable_hw(struct stm_drvdata *drvdata)
{
	u32 val;

	CS_UNLOCK(drvdata->base);

	val = readl_relaxed(drvdata->base + STMTCSR);
	val &= ~0x1; /* clear global STM enable [0] */
	writel_relaxed(val, drvdata->base + STMTCSR);

	CS_LOCK(drvdata->base);

	stm_port_disable_hw(drvdata);
	if (drvdata->stmheer)
		stm_hwevent_disable_hw(drvdata);
}

static void stm_disable(struct coresight_device *csdev)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	/*
	 * For as long as the tracer isn't disabled another entity can't
	 * change its status.  As such we can read the status here without
	 * fearing it will change under us.
	 */
	if (local_read(&drvdata->mode) == CS_MODE_SYSFS) {
		spin_lock(&drvdata->spinlock);
		stm_disable_hw(drvdata);
		spin_unlock(&drvdata->spinlock);

		/* Wait until the engine has completely stopped */
		coresight_timeout(drvdata, STMTCSR, STMTCSR_BUSY_BIT, 0);

		pm_runtime_put(drvdata->dev);

		local_set(&drvdata->mode, CS_MODE_DISABLED);
		dev_info(drvdata->dev, "STM tracing disabled\n");
	}
}

static int stm_trace_id(struct coresight_device *csdev)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);

	return drvdata->traceid;
}

static const struct coresight_ops_source stm_source_ops = {
	.trace_id	= stm_trace_id,
	.enable		= stm_enable,
	.disable	= stm_disable,
};

static const struct coresight_ops stm_cs_ops = {
	.source_ops	= &stm_source_ops,
};

static inline bool stm_addr_unaligned(const void *addr, u8 write_bytes)
{
	return ((unsigned long)addr & (write_bytes - 1));
}

static void stm_send(void *addr, const void *data, u32 size, u8 write_bytes)
{
	u8 paload[8];

	if (stm_addr_unaligned(data, write_bytes)) {
		memcpy(paload, data, size);
		data = paload;
	}

	/* now we are 64bit/32bit aligned */
	switch (size) {
#ifdef CONFIG_64BIT
	case 8:
		writeq_relaxed(*(u64 *)data, addr);
		break;
#endif
	case 4:
		writel_relaxed(*(u32 *)data, addr);
		break;
	case 2:
		writew_relaxed(*(u16 *)data, addr);
		break;
	case 1:
		writeb_relaxed(*(u8 *)data, addr);
		break;
	default:
		break;
	}
}

static int stm_generic_link(struct stm_data *stm_data,
			    unsigned int master,  unsigned int channel)
{
	struct stm_drvdata *drvdata = container_of(stm_data,
						   struct stm_drvdata, stm);
	if (!drvdata || !drvdata->csdev)
		return -EINVAL;

	return coresight_enable(drvdata->csdev);
}

static void stm_generic_unlink(struct stm_data *stm_data,
			       unsigned int master,  unsigned int channel)
{
	struct stm_drvdata *drvdata = container_of(stm_data,
						   struct stm_drvdata, stm);
	if (!drvdata || !drvdata->csdev)
		return;

	stm_disable(drvdata->csdev);
}

static long stm_generic_set_options(struct stm_data *stm_data,
				    unsigned int master,
				    unsigned int channel,
				    unsigned int nr_chans,
				    unsigned long options)
{
	struct stm_drvdata *drvdata = container_of(stm_data,
						   struct stm_drvdata, stm);
	if (!(drvdata && local_read(&drvdata->mode)))
		return -EINVAL;

	if (channel >= drvdata->numsp)
		return -EINVAL;

	switch (options) {
	case STM_OPTION_GUARANTEED:
		set_bit(channel, drvdata->chs.guaranteed);
		break;

	case STM_OPTION_INVARIANT:
		clear_bit(channel, drvdata->chs.guaranteed);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static ssize_t stm_generic_packet(struct stm_data *stm_data,
				  unsigned int master,
				  unsigned int channel,
				  unsigned int packet,
				  unsigned int flags,
				  unsigned int size,
				  const unsigned char *payload)
{
	unsigned long ch_addr;
	struct stm_drvdata *drvdata = container_of(stm_data,
						   struct stm_drvdata, stm);

	if (!(drvdata && local_read(&drvdata->mode)))
		return 0;

	if (channel >= drvdata->numsp)
		return 0;

	ch_addr = (unsigned long)stm_channel_addr(drvdata, channel);

	flags = (flags == STP_PACKET_TIMESTAMPED) ? STM_FLAG_TIMESTAMPED : 0;
	flags |= test_bit(channel, drvdata->chs.guaranteed) ?
			   STM_FLAG_GUARANTEED : 0;

	if (size > drvdata->write_bytes)
		size = drvdata->write_bytes;
	else
		size = rounddown_pow_of_two(size);

	switch (packet) {
	case STP_PACKET_FLAG:
		ch_addr |= stm_channel_off(STM_PKT_TYPE_FLAG, flags);

		/*
		 * The generic STM core sets a size of '0' on flag packets.
		 * As such send a flag packet of size '1' and tell the
		 * core we did so.
		 */
		stm_send((void *)ch_addr, payload, 1, drvdata->write_bytes);
		size = 1;
		break;

	case STP_PACKET_DATA:
		ch_addr |= stm_channel_off(STM_PKT_TYPE_DATA, flags);
		stm_send((void *)ch_addr, payload, size,
				drvdata->write_bytes);
		break;

	default:
		return -ENOTSUPP;
	}

	return size;
}

static ssize_t hwevent_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->stmheer;

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t hwevent_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	int ret = 0;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return -EINVAL;

	drvdata->stmheer = val;
	/* HW event enable and trigger go hand in hand */
	drvdata->stmheter = val;

	return size;
}
static DEVICE_ATTR_RW(hwevent_enable);

static ssize_t hwevent_select_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->stmhebsr;

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t hwevent_select_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t size)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	int ret = 0;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return -EINVAL;

	drvdata->stmhebsr = val;

	return size;
}
static DEVICE_ATTR_RW(hwevent_select);

static ssize_t port_select_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!local_read(&drvdata->mode)) {
		val = drvdata->stmspscr;
	} else {
		spin_lock(&drvdata->spinlock);
		val = readl_relaxed(drvdata->base + STMSPSCR);
		spin_unlock(&drvdata->spinlock);
	}

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t port_select_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val, stmsper;
	int ret = 0;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->stmspscr = val;

	if (local_read(&drvdata->mode)) {
		CS_UNLOCK(drvdata->base);
		/* Process as per ARM's TRM recommendation */
		stmsper = readl_relaxed(drvdata->base + STMSPER);
		writel_relaxed(0x0, drvdata->base + STMSPER);
		writel_relaxed(drvdata->stmspscr, drvdata->base + STMSPSCR);
		writel_relaxed(stmsper, drvdata->base + STMSPER);
		CS_LOCK(drvdata->base);
	}
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(port_select);

static ssize_t port_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;

	if (!local_read(&drvdata->mode)) {
		val = drvdata->stmsper;
	} else {
		spin_lock(&drvdata->spinlock);
		val = readl_relaxed(drvdata->base + STMSPER);
		spin_unlock(&drvdata->spinlock);
	}

	return scnprintf(buf, PAGE_SIZE, "%#lx\n", val);
}

static ssize_t port_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t size)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val;
	int ret = 0;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	spin_lock(&drvdata->spinlock);
	drvdata->stmsper = val;

	if (local_read(&drvdata->mode)) {
		CS_UNLOCK(drvdata->base);
		writel_relaxed(drvdata->stmsper, drvdata->base + STMSPER);
		CS_LOCK(drvdata->base);
	}
	spin_unlock(&drvdata->spinlock);

	return size;
}
static DEVICE_ATTR_RW(port_enable);

static ssize_t traceid_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	unsigned long val;
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	val = drvdata->traceid;
	return sprintf(buf, "%#lx\n", val);
}

static ssize_t traceid_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct stm_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	/* traceid field is 7bit wide on STM32 */
	drvdata->traceid = val & 0x7f;
	return size;
}
static DEVICE_ATTR_RW(traceid);

#define coresight_stm_simple_func(name, offset)	\
	coresight_simple_func(struct stm_drvdata, name, offset)

coresight_stm_simple_func(tcsr, STMTCSR);
coresight_stm_simple_func(tsfreqr, STMTSFREQR);
coresight_stm_simple_func(syncr, STMSYNCR);
coresight_stm_simple_func(sper, STMSPER);
coresight_stm_simple_func(spter, STMSPTER);
coresight_stm_simple_func(privmaskr, STMPRIVMASKR);
coresight_stm_simple_func(spscr, STMSPSCR);
coresight_stm_simple_func(spmscr, STMSPMSCR);
coresight_stm_simple_func(spfeat1r, STMSPFEAT1R);
coresight_stm_simple_func(spfeat2r, STMSPFEAT2R);
coresight_stm_simple_func(spfeat3r, STMSPFEAT3R);
coresight_stm_simple_func(devid, CORESIGHT_DEVID);

static struct attribute *coresight_stm_attrs[] = {
	&dev_attr_hwevent_enable.attr,
	&dev_attr_hwevent_select.attr,
	&dev_attr_port_enable.attr,
	&dev_attr_port_select.attr,
	&dev_attr_traceid.attr,
	NULL,
};

static struct attribute *coresight_stm_mgmt_attrs[] = {
	&dev_attr_tcsr.attr,
	&dev_attr_tsfreqr.attr,
	&dev_attr_syncr.attr,
	&dev_attr_sper.attr,
	&dev_attr_spter.attr,
	&dev_attr_privmaskr.attr,
	&dev_attr_spscr.attr,
	&dev_attr_spmscr.attr,
	&dev_attr_spfeat1r.attr,
	&dev_attr_spfeat2r.attr,
	&dev_attr_spfeat3r.attr,
	&dev_attr_devid.attr,
	NULL,
};

static const struct attribute_group coresight_stm_group = {
	.attrs = coresight_stm_attrs,
};

static const struct attribute_group coresight_stm_mgmt_group = {
	.attrs = coresight_stm_mgmt_attrs,
	.name = "mgmt",
};

static const struct attribute_group *coresight_stm_groups[] = {
	&coresight_stm_group,
	&coresight_stm_mgmt_group,
	NULL,
};

static int stm_get_resource_byname(struct device_node *np,
				   char *ch_base, struct resource *res)
{
	const char *name = NULL;
	int index = 0, found = 0;

	while (!of_property_read_string_index(np, "reg-names", index, &name)) {
		if (strcmp(ch_base, name)) {
			index++;
			continue;
		}

		/* We have a match and @index is where it's at */
		found = 1;
		break;
	}

	if (!found)
		return -EINVAL;

	return of_address_to_resource(np, index, res);
}

static u32 stm_fundamental_data_size(struct stm_drvdata *drvdata)
{
	u32 stmspfeat2r;

	if (!IS_ENABLED(CONFIG_64BIT))
		return 4;

	stmspfeat2r = readl_relaxed(drvdata->base + STMSPFEAT2R);

	/*
	 * bit[15:12] represents the fundamental data size
	 * 0 - 32-bit data
	 * 1 - 64-bit data
	 */
	return BMVAL(stmspfeat2r, 12, 15) ? 8 : 4;
}

static u32 stm_num_stimulus_port(struct stm_drvdata *drvdata)
{
	u32 numsp;

	numsp = readl_relaxed(drvdata->base + CORESIGHT_DEVID);
	/*
	 * NUMPS in STMDEVID is 17 bit long and if equal to 0x0,
	 * 32 stimulus ports are supported.
	 */
	numsp &= 0x1ffff;
	if (!numsp)
		numsp = STM_32_CHANNEL;
	return numsp;
}

static void stm_init_default_data(struct stm_drvdata *drvdata)
{
	/* Don't use port selection */
	drvdata->stmspscr = 0x0;
	/*
	 * Enable all channel regardless of their number.  When port
	 * selection isn't used (see above) STMSPER applies to all
	 * 32 channel group available, hence setting all 32 bits to 1
	 */
	drvdata->stmsper = ~0x0;

	/*
	 * The trace ID value for *ETM* tracers start at CPU_ID * 2 + 0x10 and
	 * anything equal to or higher than 0x70 is reserved.  Since 0x00 is
	 * also reserved the STM trace ID needs to be higher than 0x00 and
	 * lowner than 0x10.
	 */
	drvdata->traceid = 0x1;

	/* Set invariant transaction timing on all channels */
	bitmap_clear(drvdata->chs.guaranteed, 0, drvdata->numsp);
}

static void stm_init_generic_data(struct stm_drvdata *drvdata)
{
	drvdata->stm.name = dev_name(drvdata->dev);

	/*
	 * MasterIDs are assigned at HW design phase. As such the core is
	 * using a single master for interaction with this device.
	 */
	drvdata->stm.sw_start = 1;
	drvdata->stm.sw_end = 1;
	drvdata->stm.hw_override = true;
	drvdata->stm.sw_nchannels = drvdata->numsp;
	drvdata->stm.packet = stm_generic_packet;
	drvdata->stm.link = stm_generic_link;
	drvdata->stm.unlink = stm_generic_unlink;
	drvdata->stm.set_options = stm_generic_set_options;
}

static int stm_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	unsigned long *guaranteed;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct stm_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct resource ch_res;
	size_t res_size, bitmap_size;
	struct coresight_desc *desc;
	struct device_node *np = adev->dev.of_node;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		adev->dev.platform_data = pdata;
	}
	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	drvdata->atclk = devm_clk_get(&adev->dev, "atclk"); /* optional */
	if (!IS_ERR(drvdata->atclk)) {
		ret = clk_prepare_enable(drvdata->atclk);
		if (ret)
			return ret;
	}
	dev_set_drvdata(dev, drvdata);

	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	drvdata->base = base;

	ret = stm_get_resource_byname(np, "stm-stimulus-base", &ch_res);
	if (ret)
		return ret;

	base = devm_ioremap_resource(dev, &ch_res);
	if (IS_ERR(base))
		return PTR_ERR(base);
	drvdata->chs.base = base;

	drvdata->write_bytes = stm_fundamental_data_size(drvdata);

	if (boot_nr_channel) {
		drvdata->numsp = boot_nr_channel;
		res_size = min((resource_size_t)(boot_nr_channel *
				  BYTES_PER_CHANNEL), resource_size(res));
	} else {
		drvdata->numsp = stm_num_stimulus_port(drvdata);
		res_size = min((resource_size_t)(drvdata->numsp *
				 BYTES_PER_CHANNEL), resource_size(res));
	}
	bitmap_size = BITS_TO_LONGS(drvdata->numsp) * sizeof(long);

	guaranteed = devm_kzalloc(dev, bitmap_size, GFP_KERNEL);
	if (!guaranteed)
		return -ENOMEM;
	drvdata->chs.guaranteed = guaranteed;

	spin_lock_init(&drvdata->spinlock);

	stm_init_default_data(drvdata);
	stm_init_generic_data(drvdata);

	if (stm_register_device(dev, &drvdata->stm, THIS_MODULE)) {
		dev_info(dev,
			 "stm_register_device failed, probing deffered\n");
		return -EPROBE_DEFER;
	}

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto stm_unregister;
	}

	desc->type = CORESIGHT_DEV_TYPE_SOURCE;
	desc->subtype.source_subtype = CORESIGHT_DEV_SUBTYPE_SOURCE_SOFTWARE;
	desc->ops = &stm_cs_ops;
	desc->pdata = pdata;
	desc->dev = dev;
	desc->groups = coresight_stm_groups;
	drvdata->csdev = coresight_register(desc);
	if (IS_ERR(drvdata->csdev)) {
		ret = PTR_ERR(drvdata->csdev);
		goto stm_unregister;
	}

	pm_runtime_put(&adev->dev);

	dev_info(dev, "%s initialized\n", (char *)id->data);
	return 0;

stm_unregister:
	stm_unregister_device(&drvdata->stm);
	return ret;
}

#ifdef CONFIG_PM
static int stm_runtime_suspend(struct device *dev)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_disable_unprepare(drvdata->atclk);

	return 0;
}

static int stm_runtime_resume(struct device *dev)
{
	struct stm_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata && !IS_ERR(drvdata->atclk))
		clk_prepare_enable(drvdata->atclk);

	return 0;
}
#endif

static const struct dev_pm_ops stm_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(stm_runtime_suspend, stm_runtime_resume, NULL)
};

static struct amba_id stm_ids[] = {
	{
		.id     = 0x0003b962,
		.mask   = 0x0003ffff,
		.data	= "STM32",
	},
	{ 0, 0},
};

static struct amba_driver stm_driver = {
	.drv = {
		.name   = "coresight-stm",
		.owner	= THIS_MODULE,
		.pm	= &stm_dev_pm_ops,
		.suppress_bind_attrs = true,
	},
	.probe          = stm_probe,
	.id_table	= stm_ids,
};

builtin_amba_driver(stm_driver);
