// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * IDT CPS RapidIO switches support
 *
 * Copyright 2009-2010 Integrated Device Technology, Inc.
 * Alexandre Bounine <alexandre.bounine@idt.com>
 */

#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/module.h>
#include "../rio.h"

#define CPS_DEFAULT_ROUTE	0xde
#define CPS_NO_ROUTE		0xdf

#define IDTCPS_RIO_DOMAIN 0xf20020

static int
idtcps_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 route_port)
{
	u32 result;

	if (route_port == RIO_INVALID_ROUTE)
		route_port = CPS_DEFAULT_ROUTE;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_DESTID_SEL_CSR, route_destid);

		rio_mport_read_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR, &result);

		result = (0xffffff00 & result) | (u32)route_port;
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR, result);
	}

	return 0;
}

static int
idtcps_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 *route_port)
{
	u32 result;

	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_DESTID_SEL_CSR, route_destid);

		rio_mport_read_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR, &result);

		if (CPS_DEFAULT_ROUTE == (u8)result ||
		    CPS_NO_ROUTE == (u8)result)
			*route_port = RIO_INVALID_ROUTE;
		else
			*route_port = (u8)result;
	}

	return 0;
}

static int
idtcps_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table)
{
	u32 i;

	if (table == RIO_GLOBAL_TABLE) {
		for (i = 0x80000000; i <= 0x800000ff;) {
			rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_DESTID_SEL_CSR, i);
			rio_mport_write_config_32(mport, destid, hopcount,
				RIO_STD_RTE_CONF_PORT_SEL_CSR,
				(CPS_DEFAULT_ROUTE << 24) |
				(CPS_DEFAULT_ROUTE << 16) |
				(CPS_DEFAULT_ROUTE << 8) | CPS_DEFAULT_ROUTE);
			i += 4;
		}
	}

	return 0;
}

static int
idtcps_set_domain(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u8 sw_domain)
{
	/*
	 * Switch domain configuration operates only at global level
	 */
	rio_mport_write_config_32(mport, destid, hopcount,
				  IDTCPS_RIO_DOMAIN, (u32)sw_domain);
	return 0;
}

static int
idtcps_get_domain(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u8 *sw_domain)
{
	u32 regval;

	/*
	 * Switch domain configuration operates only at global level
	 */
	rio_mport_read_config_32(mport, destid, hopcount,
				IDTCPS_RIO_DOMAIN, &regval);

	*sw_domain = (u8)(regval & 0xff);

	return 0;
}

static struct rio_switch_ops idtcps_switch_ops = {
	.owner = THIS_MODULE,
	.add_entry = idtcps_route_add_entry,
	.get_entry = idtcps_route_get_entry,
	.clr_table = idtcps_route_clr_table,
	.set_domain = idtcps_set_domain,
	.get_domain = idtcps_get_domain,
	.em_init = NULL,
	.em_handle = NULL,
};

static int idtcps_probe(struct rio_dev *rdev, const struct rio_device_id *id)
{
	pr_debug("RIO: %s for %s\n", __func__, rio_name(rdev));

	spin_lock(&rdev->rswitch->lock);

	if (rdev->rswitch->ops) {
		spin_unlock(&rdev->rswitch->lock);
		return -EINVAL;
	}

	rdev->rswitch->ops = &idtcps_switch_ops;

	if (rdev->do_enum) {
		/* set TVAL = ~50us */
		rio_write_config_32(rdev,
			rdev->phys_efptr + RIO_PORT_LINKTO_CTL_CSR, 0x8e << 8);
		/* Ensure that default routing is disabled on startup */
		rio_write_config_32(rdev,
				    RIO_STD_RTE_DEFAULT_PORT, CPS_NO_ROUTE);
	}

	spin_unlock(&rdev->rswitch->lock);
	return 0;
}

static void idtcps_remove(struct rio_dev *rdev)
{
	pr_debug("RIO: %s for %s\n", __func__, rio_name(rdev));
	spin_lock(&rdev->rswitch->lock);
	if (rdev->rswitch->ops != &idtcps_switch_ops) {
		spin_unlock(&rdev->rswitch->lock);
		return;
	}
	rdev->rswitch->ops = NULL;
	spin_unlock(&rdev->rswitch->lock);
}

static const struct rio_device_id idtcps_id_table[] = {
	{RIO_DEVICE(RIO_DID_IDTCPS6Q, RIO_VID_IDT)},
	{RIO_DEVICE(RIO_DID_IDTCPS8, RIO_VID_IDT)},
	{RIO_DEVICE(RIO_DID_IDTCPS10Q, RIO_VID_IDT)},
	{RIO_DEVICE(RIO_DID_IDTCPS12, RIO_VID_IDT)},
	{RIO_DEVICE(RIO_DID_IDTCPS16, RIO_VID_IDT)},
	{RIO_DEVICE(RIO_DID_IDT70K200, RIO_VID_IDT)},
	{ 0, }	/* terminate list */
};

static struct rio_driver idtcps_driver = {
	.name = "idtcps",
	.id_table = idtcps_id_table,
	.probe = idtcps_probe,
	.remove = idtcps_remove,
};

static int __init idtcps_init(void)
{
	return rio_register_driver(&idtcps_driver);
}

static void __exit idtcps_exit(void)
{
	rio_unregister_driver(&idtcps_driver);
}

device_initcall(idtcps_init);
module_exit(idtcps_exit);

MODULE_DESCRIPTION("IDT CPS Gen.1 Serial RapidIO switch family driver");
MODULE_AUTHOR("Integrated Device Technology, Inc.");
MODULE_LICENSE("GPL");
