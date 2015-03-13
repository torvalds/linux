/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  Various common ioctl() support functions
 *
 *  Copyright (c) 2009 Andy Walls <awalls@md.metrocast.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#include "cx23885.h"
#include "cx23885-ioctl.h"

#ifdef CONFIG_VIDEO_ADV_DEBUG
int cx23885_g_chip_info(struct file *file, void *fh,
			 struct v4l2_dbg_chip_info *chip)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if (chip->match.addr > 1)
		return -EINVAL;
	if (chip->match.addr == 1) {
		if (dev->v4l_device == NULL)
			return -EINVAL;
		strlcpy(chip->name, "cx23417", sizeof(chip->name));
	} else {
		strlcpy(chip->name, dev->v4l2_dev.name, sizeof(chip->name));
	}
	return 0;
}

static int cx23417_g_register(struct cx23885_dev *dev,
			      struct v4l2_dbg_register *reg)
{
	u32 value;

	if (dev->v4l_device == NULL)
		return -EINVAL;

	if ((reg->reg & 0x3) != 0 || reg->reg >= 0x10000)
		return -EINVAL;

	if (mc417_register_read(dev, (u16) reg->reg, &value))
		return -EINVAL; /* V4L2 spec, but -EREMOTEIO really */

	reg->size = 4;
	reg->val = value;
	return 0;
}

int cx23885_g_register(struct file *file, void *fh,
		       struct v4l2_dbg_register *reg)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if (reg->match.addr > 1)
		return -EINVAL;
	if (reg->match.addr)
		return cx23417_g_register(dev, reg);

	if ((reg->reg & 0x3) != 0 || reg->reg >= pci_resource_len(dev->pci, 0))
		return -EINVAL;

	reg->size = 4;
	reg->val = cx_read(reg->reg);
	return 0;
}

static int cx23417_s_register(struct cx23885_dev *dev,
			      const struct v4l2_dbg_register *reg)
{
	if (dev->v4l_device == NULL)
		return -EINVAL;

	if ((reg->reg & 0x3) != 0 || reg->reg >= 0x10000)
		return -EINVAL;

	if (mc417_register_write(dev, (u16) reg->reg, (u32) reg->val))
		return -EINVAL; /* V4L2 spec, but -EREMOTEIO really */
	return 0;
}

int cx23885_s_register(struct file *file, void *fh,
		       const struct v4l2_dbg_register *reg)
{
	struct cx23885_dev *dev = video_drvdata(file);

	if (reg->match.addr > 1)
		return -EINVAL;
	if (reg->match.addr)
		return cx23417_s_register(dev, reg);

	if ((reg->reg & 0x3) != 0 || reg->reg >= pci_resource_len(dev->pci, 0))
		return -EINVAL;

	cx_write(reg->reg, reg->val);
	return 0;
}
#endif
