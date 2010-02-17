/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  Various common ioctl() support functions
 *
 *  Copyright (c) 2009 Andy Walls <awalls@radix.net>
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "cx23885.h"
#include <media/v4l2-chip-ident.h>

int cx23885_g_chip_ident(struct file *file, void *fh,
			 struct v4l2_dbg_chip_ident *chip)
{
	struct cx23885_dev *dev = ((struct cx23885_fh *)fh)->dev;
	int err = 0;
	u8 rev;

	chip->ident = V4L2_IDENT_NONE;
	chip->revision = 0;
	switch (chip->match.type) {
	case V4L2_CHIP_MATCH_HOST:
		switch (chip->match.addr) {
		case 0:
			rev = cx_read(RDR_CFG2) & 0xff;
			switch (dev->pci->device) {
			case 0x8852:
				/* rev 0x04 could be '885 or '888. Pick '888. */
				if (rev == 0x04)
					chip->ident = V4L2_IDENT_CX23888;
				else
					chip->ident = V4L2_IDENT_CX23885;
				break;
			case 0x8880:
				if (rev == 0x0e || rev == 0x0f)
					chip->ident = V4L2_IDENT_CX23887;
				else
					chip->ident = V4L2_IDENT_CX23888;
				break;
			default:
				chip->ident = V4L2_IDENT_UNKNOWN;
				break;
			}
			chip->revision = (dev->pci->device << 16) | (rev << 8) |
					 (dev->hwrevision & 0xff);
			break;
		case 1:
			if (dev->v4l_device != NULL) {
				chip->ident = V4L2_IDENT_CX23417;
				chip->revision = 0;
			}
			break;
		case 2:
			/*
			 * The integrated IR controller on the CX23888 is
			 * host chip 2.  It may not be used/initialized or sd_ir
			 * may be pointing at the cx25840 subdevice for the
			 * IR controller on the CX23885.  Thus we find it
			 * without using the dev->sd_ir pointer.
			 */
			call_hw(dev, CX23885_HW_888_IR, core, g_chip_ident,
				chip);
			break;
		default:
			err = -EINVAL; /* per V4L2 spec */
			break;
		}
		break;
	case V4L2_CHIP_MATCH_I2C_DRIVER:
		/* If needed, returns V4L2_IDENT_AMBIGUOUS without extra work */
		call_all(dev, core, g_chip_ident, chip);
		break;
	case V4L2_CHIP_MATCH_I2C_ADDR:
		/*
		 * We could return V4L2_IDENT_UNKNOWN, but we don't do the work
		 * to look if a chip is at the address with no driver.  That's a
		 * dangerous thing to do with EEPROMs anyway.
		 */
		call_all(dev, core, g_chip_ident, chip);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cx23885_g_host_register(struct cx23885_dev *dev,
				   struct v4l2_dbg_register *reg)
{
	if ((reg->reg & 0x3) != 0 || reg->reg >= pci_resource_len(dev->pci, 0))
		return -EINVAL;

	reg->size = 4;
	reg->val = cx_read(reg->reg);
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
	struct cx23885_dev *dev = ((struct cx23885_fh *)fh)->dev;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (reg->match.type == V4L2_CHIP_MATCH_HOST) {
		switch (reg->match.addr) {
		case 0:
			return cx23885_g_host_register(dev, reg);
		case 1:
			return cx23417_g_register(dev, reg);
		default:
			break;
		}
	}

	/* FIXME - any error returns should not be ignored */
	call_all(dev, core, g_register, reg);
	return 0;
}

static int cx23885_s_host_register(struct cx23885_dev *dev,
				   struct v4l2_dbg_register *reg)
{
	if ((reg->reg & 0x3) != 0 || reg->reg >= pci_resource_len(dev->pci, 0))
		return -EINVAL;

	reg->size = 4;
	cx_write(reg->reg, reg->val);
	return 0;
}

static int cx23417_s_register(struct cx23885_dev *dev,
			      struct v4l2_dbg_register *reg)
{
	if (dev->v4l_device == NULL)
		return -EINVAL;

	if ((reg->reg & 0x3) != 0 || reg->reg >= 0x10000)
		return -EINVAL;

	if (mc417_register_write(dev, (u16) reg->reg, (u32) reg->val))
		return -EINVAL; /* V4L2 spec, but -EREMOTEIO really */

	reg->size = 4;
	return 0;
}

int cx23885_s_register(struct file *file, void *fh,
		       struct v4l2_dbg_register *reg)
{
	struct cx23885_dev *dev = ((struct cx23885_fh *)fh)->dev;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (reg->match.type == V4L2_CHIP_MATCH_HOST) {
		switch (reg->match.addr) {
		case 0:
			return cx23885_s_host_register(dev, reg);
		case 1:
			return cx23417_s_register(dev, reg);
		default:
			break;
		}
	}

	/* FIXME - any error returns should not be ignored */
	call_all(dev, core, s_register, reg);
	return 0;
}
#endif
