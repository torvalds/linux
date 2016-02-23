/*
 * Intel(R) Trace Hub Software Trace Hub support
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stm.h>

#include "intel_th.h"
#include "sth.h"

struct sth_device {
	void __iomem	*base;
	void __iomem	*channels;
	phys_addr_t	channels_phys;
	struct device	*dev;
	struct stm_data	stm;
	unsigned int	sw_nmasters;
};

static struct intel_th_channel __iomem *
sth_channel(struct sth_device *sth, unsigned int master, unsigned int channel)
{
	struct intel_th_channel __iomem *sw_map = sth->channels;

	return &sw_map[(master - sth->stm.sw_start) * sth->stm.sw_nchannels +
		       channel];
}

static void sth_iowrite(void __iomem *dest, const unsigned char *payload,
			unsigned int size)
{
	switch (size) {
#ifdef CONFIG_64BIT
	case 8:
		writeq_relaxed(*(u64 *)payload, dest);
		break;
#endif
	case 4:
		writel_relaxed(*(u32 *)payload, dest);
		break;
	case 2:
		writew_relaxed(*(u16 *)payload, dest);
		break;
	case 1:
		writeb_relaxed(*(u8 *)payload, dest);
		break;
	default:
		break;
	}
}

static ssize_t sth_stm_packet(struct stm_data *stm_data, unsigned int master,
			      unsigned int channel, unsigned int packet,
			      unsigned int flags, unsigned int size,
			      const unsigned char *payload)
{
	struct sth_device *sth = container_of(stm_data, struct sth_device, stm);
	struct intel_th_channel __iomem *out =
		sth_channel(sth, master, channel);
	u64 __iomem *outp = &out->Dn;
	unsigned long reg = REG_STH_TRIG;

#ifndef CONFIG_64BIT
	if (size > 4)
		size = 4;
#endif

	size = rounddown_pow_of_two(size);

	switch (packet) {
	/* Global packets (GERR, XSYNC, TRIG) are sent with register writes */
	case STP_PACKET_GERR:
		reg += 4;
	case STP_PACKET_XSYNC:
		reg += 8;
	case STP_PACKET_TRIG:
		if (flags & STP_PACKET_TIMESTAMPED)
			reg += 4;
		iowrite8(*payload, sth->base + reg);
		break;

	case STP_PACKET_MERR:
		sth_iowrite(&out->MERR, payload, size);
		break;

	case STP_PACKET_FLAG:
		if (flags & STP_PACKET_TIMESTAMPED)
			outp = (u64 __iomem *)&out->FLAG_TS;
		else
			outp = (u64 __iomem *)&out->FLAG;

		size = 1;
		sth_iowrite(outp, payload, size);
		break;

	case STP_PACKET_USER:
		if (flags & STP_PACKET_TIMESTAMPED)
			outp = &out->USER_TS;
		else
			outp = &out->USER;
		sth_iowrite(outp, payload, size);
		break;

	case STP_PACKET_DATA:
		outp = &out->Dn;

		if (flags & STP_PACKET_TIMESTAMPED)
			outp += 2;
		if (flags & STP_PACKET_MARKED)
			outp++;

		sth_iowrite(outp, payload, size);
		break;
	}

	return size;
}

static phys_addr_t
sth_stm_mmio_addr(struct stm_data *stm_data, unsigned int master,
		  unsigned int channel, unsigned int nr_chans)
{
	struct sth_device *sth = container_of(stm_data, struct sth_device, stm);
	phys_addr_t addr;

	master -= sth->stm.sw_start;
	addr = sth->channels_phys + (master * sth->stm.sw_nchannels + channel) *
		sizeof(struct intel_th_channel);

	if (offset_in_page(addr) ||
	    offset_in_page(nr_chans * sizeof(struct intel_th_channel)))
		return 0;

	return addr;
}

static int sth_stm_link(struct stm_data *stm_data, unsigned int master,
			 unsigned int channel)
{
	struct sth_device *sth = container_of(stm_data, struct sth_device, stm);

	intel_th_set_output(to_intel_th_device(sth->dev), master);

	return 0;
}

static int intel_th_sw_init(struct sth_device *sth)
{
	u32 reg;

	reg = ioread32(sth->base + REG_STH_STHCAP1);
	sth->stm.sw_nchannels = reg & 0xff;

	reg = ioread32(sth->base + REG_STH_STHCAP0);
	sth->stm.sw_start = reg & 0xffff;
	sth->stm.sw_end = reg >> 16;

	sth->sw_nmasters = sth->stm.sw_end - sth->stm.sw_start;
	dev_dbg(sth->dev, "sw_start: %x sw_end: %x masters: %x nchannels: %x\n",
		sth->stm.sw_start, sth->stm.sw_end, sth->sw_nmasters,
		sth->stm.sw_nchannels);

	return 0;
}

static int intel_th_sth_probe(struct intel_th_device *thdev)
{
	struct device *dev = &thdev->dev;
	struct sth_device *sth;
	struct resource *res;
	void __iomem *base, *channels;
	int err;

	res = intel_th_device_get_resource(thdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	res = intel_th_device_get_resource(thdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	channels = devm_ioremap(dev, res->start, resource_size(res));
	if (!channels)
		return -ENOMEM;

	sth = devm_kzalloc(dev, sizeof(*sth), GFP_KERNEL);
	if (!sth)
		return -ENOMEM;

	sth->dev = dev;
	sth->base = base;
	sth->channels = channels;
	sth->channels_phys = res->start;
	sth->stm.name = dev_name(dev);
	sth->stm.packet = sth_stm_packet;
	sth->stm.mmio_addr = sth_stm_mmio_addr;
	sth->stm.sw_mmiosz = sizeof(struct intel_th_channel);
	sth->stm.link = sth_stm_link;

	err = intel_th_sw_init(sth);
	if (err)
		return err;

	err = stm_register_device(dev, &sth->stm, THIS_MODULE);
	if (err) {
		dev_err(dev, "stm_register_device failed\n");
		return err;
	}

	dev_set_drvdata(dev, sth);

	return 0;
}

static void intel_th_sth_remove(struct intel_th_device *thdev)
{
	struct sth_device *sth = dev_get_drvdata(&thdev->dev);

	stm_unregister_device(&sth->stm);
}

static struct intel_th_driver intel_th_sth_driver = {
	.probe	= intel_th_sth_probe,
	.remove	= intel_th_sth_remove,
	.driver	= {
		.name	= "sth",
		.owner	= THIS_MODULE,
	},
};

module_driver(intel_th_sth_driver,
	      intel_th_driver_register,
	      intel_th_driver_unregister);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub Software Trace Hub driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@intel.com>");
