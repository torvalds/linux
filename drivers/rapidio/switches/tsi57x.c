/*
 * RapidIO Tsi57x switch family support
 *
 * Copyright 2009 Integrated Device Technology, Inc.
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/rio_ids.h>
#include <linux/delay.h>
#include "../rio.h"

/* Global (broadcast) route registers */
#define SPBC_ROUTE_CFG_DESTID	0x10070
#define SPBC_ROUTE_CFG_PORT	0x10074

/* Per port route registers */
#define SPP_ROUTE_CFG_DESTID(n)	(0x11070 + 0x100*n)
#define SPP_ROUTE_CFG_PORT(n)	(0x11074 + 0x100*n)

static int
tsi57x_route_add_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 route_port)
{
	if (table == RIO_GLOBAL_TABLE) {
		rio_mport_write_config_32(mport, destid, hopcount,
					  SPBC_ROUTE_CFG_DESTID, route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
					  SPBC_ROUTE_CFG_PORT, route_port);
	} else {
		rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_DESTID(table), route_destid);
		rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table), route_port);
	}

	udelay(10);

	return 0;
}

static int
tsi57x_route_get_entry(struct rio_mport *mport, u16 destid, u8 hopcount,
		       u16 table, u16 route_destid, u8 *route_port)
{
	int ret = 0;
	u32 result;

	if (table == RIO_GLOBAL_TABLE) {
		/* Use local RT of the ingress port to avoid possible
		   race condition */
		rio_mport_read_config_32(mport, destid, hopcount,
			RIO_SWP_INFO_CAR, &result);
		table = (result & RIO_SWP_INFO_PORT_NUM_MASK);
	}

	rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_DESTID(table), route_destid);
	rio_mport_read_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table), &result);

	*route_port = (u8)result;
	if (*route_port > 15)
		ret = -1;

	return ret;
}

static int
tsi57x_route_clr_table(struct rio_mport *mport, u16 destid, u8 hopcount,
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
				SPP_ROUTE_CFG_DESTID(table), 0x80000000);
		for (route_idx = 0; route_idx <= lut_size; route_idx++)
			rio_mport_write_config_32(mport, destid, hopcount,
				SPP_ROUTE_CFG_PORT(table) , RIO_INVALID_ROUTE);
	}

	return 0;
}

DECLARE_RIO_ROUTE_OPS(RIO_VID_TUNDRA, RIO_DID_TSI572, tsi57x_route_add_entry, tsi57x_route_get_entry, tsi57x_route_clr_table);
DECLARE_RIO_ROUTE_OPS(RIO_VID_TUNDRA, RIO_DID_TSI574, tsi57x_route_add_entry, tsi57x_route_get_entry, tsi57x_route_clr_table);
DECLARE_RIO_ROUTE_OPS(RIO_VID_TUNDRA, RIO_DID_TSI577, tsi57x_route_add_entry, tsi57x_route_get_entry, tsi57x_route_clr_table);
DECLARE_RIO_ROUTE_OPS(RIO_VID_TUNDRA, RIO_DID_TSI578, tsi57x_route_add_entry, tsi57x_route_get_entry, tsi57x_route_clr_table);
