// SPDX-License-Identifier: GPL-2.0
/*
 * Bitbanged MDIO support.
 *
 * Author: Scott Wood <scottwood@freescale.com>
 * Copyright (c) 2007 Freescale Semiconductor
 *
 * Based on CPM2 MDIO code which is:
 *
 * Copyright (c) 2003 Intracom S.A.
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * 2005 (c) MontaVista Software, Inc.
 * Vitaly Bordug <vbordug@ru.mvista.com>
 */

#include <linux/delay.h>
#include <linux/mdio-bitbang.h>
#include <linux/module.h>
#include <linux/types.h>

#define MDIO_READ 2
#define MDIO_WRITE 1

#define MDIO_C45 (1<<15)
#define MDIO_C45_ADDR (MDIO_C45 | 0)
#define MDIO_C45_READ (MDIO_C45 | 3)
#define MDIO_C45_WRITE (MDIO_C45 | 1)

#define MDIO_SETUP_TIME 10
#define MDIO_HOLD_TIME 10

/* Minimum MDC period is 400 ns, plus some margin for error.  MDIO_DELAY
 * is done twice per period.
 */
#define MDIO_DELAY 250

/* The PHY may take up to 300 ns to produce data, plus some margin
 * for error.
 */
#define MDIO_READ_DELAY 350

/* MDIO must already be configured as output. */
static void mdiobb_send_bit(struct mdiobb_ctrl *ctrl, int val)
{
	const struct mdiobb_ops *ops = ctrl->ops;

	ops->set_mdio_data(ctrl, val);
	ndelay(MDIO_DELAY);
	ops->set_mdc(ctrl, 1);
	ndelay(MDIO_DELAY);
	ops->set_mdc(ctrl, 0);
}

/* MDIO must already be configured as input. */
static int mdiobb_get_bit(struct mdiobb_ctrl *ctrl)
{
	const struct mdiobb_ops *ops = ctrl->ops;

	ndelay(MDIO_DELAY);
	ops->set_mdc(ctrl, 1);
	ndelay(MDIO_READ_DELAY);
	ops->set_mdc(ctrl, 0);

	return ops->get_mdio_data(ctrl);
}

/* MDIO must already be configured as output. */
static void mdiobb_send_num(struct mdiobb_ctrl *ctrl, u16 val, int bits)
{
	int i;

	for (i = bits - 1; i >= 0; i--)
		mdiobb_send_bit(ctrl, (val >> i) & 1);
}

/* MDIO must already be configured as input. */
static u16 mdiobb_get_num(struct mdiobb_ctrl *ctrl, int bits)
{
	int i;
	u16 ret = 0;

	for (i = bits - 1; i >= 0; i--) {
		ret <<= 1;
		ret |= mdiobb_get_bit(ctrl);
	}

	return ret;
}

/* Utility to send the preamble, address, and
 * register (common to read and write).
 */
static void mdiobb_cmd(struct mdiobb_ctrl *ctrl, int op, u8 phy, u8 reg)
{
	const struct mdiobb_ops *ops = ctrl->ops;
	int i;

	ops->set_mdio_dir(ctrl, 1);

	/*
	 * Send a 32 bit preamble ('1's) with an extra '1' bit for good
	 * measure.  The IEEE spec says this is a PHY optional
	 * requirement.  The AMD 79C874 requires one after power up and
	 * one after a MII communications error.  This means that we are
	 * doing more preambles than we need, but it is safer and will be
	 * much more robust.
	 */

	for (i = 0; i < 32; i++)
		mdiobb_send_bit(ctrl, 1);

	/* send the start bit (01) and the read opcode (10) or write (01).
	   Clause 45 operation uses 00 for the start and 11, 10 for
	   read/write */
	mdiobb_send_bit(ctrl, 0);
	if (op & MDIO_C45)
		mdiobb_send_bit(ctrl, 0);
	else
		mdiobb_send_bit(ctrl, 1);
	mdiobb_send_bit(ctrl, (op >> 1) & 1);
	mdiobb_send_bit(ctrl, (op >> 0) & 1);

	mdiobb_send_num(ctrl, phy, 5);
	mdiobb_send_num(ctrl, reg, 5);
}

/* In clause 45 mode all commands are prefixed by MDIO_ADDR to specify the
   lower 16 bits of the 21 bit address. This transfer is done identically to a
   MDIO_WRITE except for a different code. Theoretically clause 45 and normal
   devices can exist on the same bus. Normal devices should ignore the MDIO_ADDR
   phase. */
static void mdiobb_cmd_addr(struct mdiobb_ctrl *ctrl, int phy, int dev_addr,
			    int reg)
{
	mdiobb_cmd(ctrl, MDIO_C45_ADDR, phy, dev_addr);

	/* send the turnaround (10) */
	mdiobb_send_bit(ctrl, 1);
	mdiobb_send_bit(ctrl, 0);

	mdiobb_send_num(ctrl, reg, 16);

	ctrl->ops->set_mdio_dir(ctrl, 0);
	mdiobb_get_bit(ctrl);
}

static int mdiobb_read_common(struct mii_bus *bus, int phy)
{
	struct mdiobb_ctrl *ctrl = bus->priv;
	int ret, i;

	ctrl->ops->set_mdio_dir(ctrl, 0);

	/* check the turnaround bit: the PHY should be driving it to zero, if this
	 * PHY is listed in phy_ignore_ta_mask as having broken TA, skip that
	 */
	if (mdiobb_get_bit(ctrl) != 0 &&
	    !(bus->phy_ignore_ta_mask & (1 << phy))) {
		/* PHY didn't drive TA low -- flush any bits it
		 * may be trying to send.
		 */
		for (i = 0; i < 32; i++)
			mdiobb_get_bit(ctrl);

		return 0xffff;
	}

	ret = mdiobb_get_num(ctrl, 16);
	mdiobb_get_bit(ctrl);
	return ret;
}

int mdiobb_read_c22(struct mii_bus *bus, int phy, int reg)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	mdiobb_cmd(ctrl, ctrl->op_c22_read, phy, reg);

	return mdiobb_read_common(bus, phy);
}
EXPORT_SYMBOL(mdiobb_read_c22);

int mdiobb_read_c45(struct mii_bus *bus, int phy, int devad, int reg)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	mdiobb_cmd_addr(ctrl, phy, devad, reg);
	mdiobb_cmd(ctrl, MDIO_C45_READ, phy, devad);

	return mdiobb_read_common(bus, phy);
}
EXPORT_SYMBOL(mdiobb_read_c45);

static int mdiobb_write_common(struct mii_bus *bus, u16 val)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	/* send the turnaround (10) */
	mdiobb_send_bit(ctrl, 1);
	mdiobb_send_bit(ctrl, 0);

	mdiobb_send_num(ctrl, val, 16);

	ctrl->ops->set_mdio_dir(ctrl, 0);
	mdiobb_get_bit(ctrl);
	return 0;
}

int mdiobb_write_c22(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	mdiobb_cmd(ctrl, ctrl->op_c22_write, phy, reg);

	return mdiobb_write_common(bus, val);
}
EXPORT_SYMBOL(mdiobb_write_c22);

int mdiobb_write_c45(struct mii_bus *bus, int phy, int devad, int reg, u16 val)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	mdiobb_cmd_addr(ctrl, phy, devad, reg);
	mdiobb_cmd(ctrl, MDIO_C45_WRITE, phy, devad);

	return mdiobb_write_common(bus, val);
}
EXPORT_SYMBOL(mdiobb_write_c45);

struct mii_bus *alloc_mdio_bitbang(struct mdiobb_ctrl *ctrl)
{
	struct mii_bus *bus;

	bus = mdiobus_alloc();
	if (!bus)
		return NULL;

	__module_get(ctrl->ops->owner);

	bus->read = mdiobb_read_c22;
	bus->write = mdiobb_write_c22;
	bus->read_c45 = mdiobb_read_c45;
	bus->write_c45 = mdiobb_write_c45;

	bus->priv = ctrl;
	if (!ctrl->override_op_c22) {
		ctrl->op_c22_read = MDIO_READ;
		ctrl->op_c22_write = MDIO_WRITE;
	}

	return bus;
}
EXPORT_SYMBOL(alloc_mdio_bitbang);

void free_mdio_bitbang(struct mii_bus *bus)
{
	struct mdiobb_ctrl *ctrl = bus->priv;

	module_put(ctrl->ops->owner);
	mdiobus_free(bus);
}
EXPORT_SYMBOL(free_mdio_bitbang);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Bitbanged MDIO buses");
