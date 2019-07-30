// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RapidIO Tsi568 switch support
 *
 * Copyright 2009-2010 Integrated Device Technology, Inc.
 * Alexandre Bounine <alexandre.bounine@idt.com>
 *  - Added EM support
 *  - Modified switch operations initialization.
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 */

#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/delay.h>
#include <linux/module.h>
#include "../rio.h"

/* Global (broadcast) route registers */
#define SPBC_ROUTE_CFG_DESTID	0x10070
#define SPBC_ROUTE_CFG_PORT	0x10074

/* Per port route registers */
#define SPP_ROUTE_CFG_DESTID(n)	(0x11070 + 0x100*n)
#define SPP_ROUTE_CFG_PORT(n)	(0x11074 + 0x100*n)

#define TSI568_SP_MODE(n)	(0x11004 + 0x100*n)
#define  TSI568_SP_MODE_PW_DIS	0x08000000

static int
tsi568_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 route_port)
{
	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPBC_ROUTE_CFG_DESTID, route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
					SPBC_ROUTE_CFG_PORT, route_port);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPP_ROUTE_CFG_DESTID(table),
					route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
					SPP_ROUTE_CFG_PORT(table), route_port);
	}

	udelay(10);

	return 0;
}

static int
tsi568_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 *route_port)
{
	int ret = 0;
	u32 result;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPBC_ROUTE_CFG_DESTID, route_destid);
		rio_mport_read_config_32(mport, destid, hopcount,
					SPBC_ROUTE_CFG_PORT, &result);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPP_ROUTE_CFG_DESTID(table),
					route_destid);
		rio_mport_read_config_32(mport, destid, hopcount,
					SPP_ROUTE_CFG_PORT(table), &result);
	}

	*route_port = result;
	if (*route_port > 15)
		ret = -1;

	return ret;
}

static int
tsi568_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table)
{
	u32 route_idx;
	u32 lut_size;

	lut_size = (mport->sys_size) ? 0x1ff : 0xff;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPBC_ROUTE_CFG_DESTID, 0x80000000);
		for (route_idx = 0; route_idx <= lut_size; route_idx++)
			rio_mport_write_config_32(mport, destid, hopcount,
						SPBC_ROUTE_CFG_PORT,
						RIO_INVALID_ROUTE);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
					SPP_ROUTE_CFG_DESTID(table),
					0x80000000);
		for (route_idx = 0; route_idx <= lut_size; route_idx++)
			rio_mport_write_config_32(mport, destid, hopcount,
						SPP_ROUTE_CFG_PORT(table),
						RIO_INVALID_ROUTE);
	}

	return 0;
}

static int
tsi568_em_init(struct rio_dev *rdev)
{
	u32 regval;
	int portnum;

	pr_debug("TSI568 %s [%d:%d]\n", __func__, rdev->destid, rdev->hopcount);

	/* Make sure that Port-Writes are disabled (for all ports) */
	for (portnum = 0;
	     portnum < RIO_GET_TOTAL_PORTS(rdev->swpinfo); portnum++) {
		rio_read_config_32(rdev, TSI568_SP_MODE(portnum), &regval);
		rio_write_config_32(rdev, TSI568_SP_MODE(portnum),
				    regval | TSI568_SP_MODE_PW_DIS);
	}

	return 0;
}

static struct rio_switch_ops tsi568_switch_ops = {
	.owner = THIS_MODULE,
	.add_entry = tsi568_route_add_entry,
	.get_entry = tsi568_route_get_entry,
	.clr_table = tsi568_route_clr_table,
	.set_domain = NULL,
	.get_domain = NULL,
	.em_init = tsi568_em_init,
	.em_handle = NULL,
};

static int tsi568_probe(struct rio_dev *rdev, const struct rio_device_id *id)
{
	pr_debug("RIO: %s for %s\n", __func__, rio_name(rdev));

	spin_lock(&rdev->rswitch->lock);

	if (rdev->rswitch->ops) {
		spin_unlock(&rdev->rswitch->lock);
		return -EINVAL;
	}

	rdev->rswitch->ops = &tsi568_switch_ops;
	spin_unlock(&rdev->rswitch->lock);
	return 0;
}

static void tsi568_remove(struct rio_dev *rdev)
{
	pr_debug("RIO: %s for %s\n", __func__, rio_name(rdev));
	spin_lock(&rdev->rswitch->lock);
	if (rdev->rswitch->ops != &tsi568_switch_ops) {
		spin_unlock(&rdev->rswitch->lock);
		return;
	}
	rdev->rswitch->ops = NULL;
	spin_unlock(&rdev->rswitch->lock);
}

static const struct rio_device_id tsi568_id_table[] = {
	{RIO_DEVICE(RIO_DID_TSI568, RIO_VID_TUNDRA)},
	{ 0, }	/* terminate list */
};

static struct rio_driver tsi568_driver = {
	.name = "tsi568",
	.id_table = tsi568_id_table,
	.probe = tsi568_probe,
	.remove = tsi568_remove,
};

static int __init tsi568_init(void)
{
	return rio_register_driver(&tsi568_driver);
}

static void __exit tsi568_exit(void)
{
	rio_unregister_driver(&tsi568_driver);
}

device_initcall(tsi568_init);
module_exit(tsi568_exit);

MODULE_DESCRIPTION("IDT Tsi568 Serial RapidIO switch driver");
MODULE_AUTHOR("Integrated Device Technology, Inc.");
MODULE_LICENSE("GPL");
