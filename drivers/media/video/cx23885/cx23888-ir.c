/*
 *  Driver for the Conexant CX23885/7/8 PCIe bridge
 *
 *  CX23888 Integrated Consumer Infrared Controller
 *
 *  Copyright (C) 2009  Andy Walls <awalls@radix.net>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>

#include "cx23885.h"

#define CX23888_IR_REG_BASE 	0x170000
/*
 * These CX23888 register offsets have a straightforward one to one mapping
 * to the CX23885 register offsets of 0x200 through 0x218
 */
#define CX23888_IR_CNTRL_REG	0x170000
#define CX23888_IR_TXCLK_REG	0x170004
#define CX23888_IR_RXCLK_REG	0x170008
#define CX23888_IR_CDUTY_REG	0x17000C
#define CX23888_IR_STATS_REG	0x170010
#define CX23888_IR_IRQEN_REG	0x170014
#define CX23888_IR_FILTR_REG	0x170018
/* This register doesn't follow the pattern; it's 0x23C on a CX23885 */
#define CX23888_IR_FIFO_REG	0x170040

/* CX23888 unique registers */
#define CX23888_IR_SEEDP_REG	0x17001C
#define CX23888_IR_TIMOL_REG	0x170020
#define CX23888_IR_WAKE0_REG	0x170024
#define CX23888_IR_WAKE1_REG	0x170028
#define CX23888_IR_WAKE2_REG	0x17002C
#define CX23888_IR_MASK0_REG	0x170030
#define CX23888_IR_MASK1_REG	0x170034
#define CX23888_IR_MAKS2_REG	0x170038
#define CX23888_IR_DPIPG_REG	0x17003C
#define CX23888_IR_LEARN_REG	0x170044


struct cx23888_ir_state {
	struct v4l2_subdev sd;
	struct cx23885_dev *dev;
	u32 id;
	u32 rev;
};

static inline struct cx23888_ir_state *to_state(struct v4l2_subdev *sd)
{
	return v4l2_get_subdevdata(sd);
}

static int cx23888_ir_write(struct cx23885_dev *dev, u32 addr, u8 value)
{
	u32 reg = (addr & ~3);
	int shift = (addr & 3) * 8;
	u32 x = cx_read(reg);

	x = (x & ~(0xff << shift)) | ((u32)value << shift);
	cx_write(reg, x);
	return 0;
}

static
inline int cx23888_ir_write4(struct cx23885_dev *dev, u32 addr, u32 value)
{
	cx_write(addr, value);
	return 0;
}

static u8 cx23888_ir_read(struct cx23885_dev *dev, u32 addr)
{
	u32 x = cx_read((addr & ~3));
	int shift = (addr & 3) * 8;

	return (x >> shift) & 0xff;
}

static inline u32 cx23888_ir_read4(struct cx23885_dev *dev, u32 addr)
{
	return cx_read(addr);
}

static int cx23888_ir_and_or(struct cx23885_dev *dev, u32 addr,
			     unsigned and_mask, u8 or_value)
{
	return cx23888_ir_write(dev, addr,
				(cx23888_ir_read(dev, addr) & and_mask) |
				or_value);
}

static inline int cx23888_ir_and_or4(struct cx23885_dev *dev, u32 addr,
				     u32 and_mask, u32 or_value)
{
	cx_andor(addr, and_mask, or_value);
	return 0;
}

static int cx23888_ir_log_status(struct v4l2_subdev *sd)
{
	struct cx23888_ir_state *state = to_state(sd);
	struct cx23885_dev *dev = state->dev;
	u8 cntrl = cx23888_ir_read(dev, CX23888_IR_CNTRL_REG+1);
	v4l2_info(sd, "receiver    %sabled\n", cntrl & 0x1 ? "en" : "dis");
	v4l2_info(sd, "transmitter %sabled\n", cntrl & 0x2 ? "en" : "dis");
	return 0;
}

static inline int cx23888_ir_dbg_match(const struct v4l2_dbg_match *match)
{
	return match->type == V4L2_CHIP_MATCH_HOST && match->addr == 2;
}

static int cx23888_ir_g_chip_ident(struct v4l2_subdev *sd,
				   struct v4l2_dbg_chip_ident *chip)
{
	struct cx23888_ir_state *state = to_state(sd);

	if (cx23888_ir_dbg_match(&chip->match)) {
		chip->ident = state->id;
		chip->revision = state->rev;
	}
	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int cx23888_ir_g_register(struct v4l2_subdev *sd,
				 struct v4l2_dbg_register *reg)
{
	struct cx23888_ir_state *state = to_state(sd);
	u32 addr = CX23888_IR_REG_BASE + (u32) reg->reg;

	if (!cx23888_ir_dbg_match(&reg->match))
		return -EINVAL;
	if ((addr & 0x3) != 0)
		return -EINVAL;
	if (addr < CX23888_IR_CNTRL_REG || addr > CX23888_IR_LEARN_REG)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	reg->size = 4;
	reg->val = cx23888_ir_read4(state->dev, addr);
	return 0;
}

static int cx23888_ir_s_register(struct v4l2_subdev *sd,
				 struct v4l2_dbg_register *reg)
{
	struct cx23888_ir_state *state = to_state(sd);
	u32 addr = CX23888_IR_REG_BASE + (u32) reg->reg;

	if (!cx23888_ir_dbg_match(&reg->match))
		return -EINVAL;
	if ((addr & 0x3) != 0)
		return -EINVAL;
	if (addr < CX23888_IR_CNTRL_REG || addr > CX23888_IR_LEARN_REG)
		return -EINVAL;
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	cx23888_ir_write4(state->dev, addr, reg->val);
	return 0;
}
#endif

static const struct v4l2_subdev_core_ops cx23888_ir_core_ops = {
	.g_chip_ident = cx23888_ir_g_chip_ident,
	.log_status = cx23888_ir_log_status,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = cx23888_ir_g_register,
	.s_register = cx23888_ir_s_register,
#endif
};

static const struct v4l2_subdev_ops cx23888_ir_controller_ops = {
	.core = &cx23888_ir_core_ops,
};

int cx23888_ir_probe(struct cx23885_dev *dev)
{
	struct cx23888_ir_state *state;
	struct v4l2_subdev *sd;

	state = kzalloc(sizeof(struct cx23888_ir_state), GFP_KERNEL);
	if (state == NULL)
		return -ENOMEM;

	state->dev = dev;
	state->id = V4L2_IDENT_CX23888_IR;
	state->rev = 0;
	sd = &state->sd;

	v4l2_subdev_init(sd, &cx23888_ir_controller_ops);
	v4l2_set_subdevdata(sd, state);
	/* FIXME - fix the formatting of dev->v4l2_dev.name and use it */
	snprintf(sd->name, sizeof(sd->name), "%s/888-ir", dev->name);
	sd->grp_id = CX23885_HW_888_IR;
	return v4l2_device_register_subdev(&dev->v4l2_dev, sd);
}

int cx23888_ir_remove(struct cx23885_dev *dev)
{
	struct v4l2_subdev *sd;
	struct cx23888_ir_state *state;

	sd = cx23885_find_hw(dev, CX23885_HW_888_IR);
	if (sd == NULL)
		return -ENODEV;

	/* Disable receiver and transmitter */
	cx23888_ir_and_or(dev, CX23888_IR_CNTRL_REG+1, 0xfc, 0);

	state = to_state(sd);
	v4l2_device_unregister_subdev(sd);
	kfree(state);
	/* Nothing more to free() as state held the actual v4l2_subdev object */
	return 0;
}
