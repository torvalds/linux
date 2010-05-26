/*
 * RapidIO interconnect services
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/list.h>
#include <linux/rio.h>

/* Functions internal to the RIO core code */

extern u32 rio_mport_get_feature(struct rio_mport *mport, int local, u16 destid,
				 u8 hopcount, int ftr);
extern u32 rio_mport_get_physefb(struct rio_mport *port, int local,
				 u16 destid, u8 hopcount);
extern u32 rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
			     u8 hopcount, u32 from);
extern int rio_create_sysfs_dev_files(struct rio_dev *rdev);
extern int rio_enum_mport(struct rio_mport *mport);
extern int rio_disc_mport(struct rio_mport *mport);
extern int rio_std_route_add_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 route_port);
extern int rio_std_route_get_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 *route_port);
extern int rio_std_route_clr_table(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table);
extern int rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock);

/* Structures internal to the RIO core code */
extern struct device_attribute rio_dev_attrs[];
extern spinlock_t rio_global_list_lock;

extern struct rio_route_ops __start_rio_route_ops[];
extern struct rio_route_ops __end_rio_route_ops[];

/* Helpers internal to the RIO core code */
#define DECLARE_RIO_ROUTE_SECTION(section, name, vid, did, add_hook, get_hook, clr_hook) \
	static const struct rio_route_ops __rio_route_##name __used \
	__section(section) = { vid, did, add_hook, get_hook, clr_hook };

/**
 * DECLARE_RIO_ROUTE_OPS - Registers switch routing operations
 * @vid: RIO vendor ID
 * @did: RIO device ID
 * @add_hook: Callback that adds a route entry
 * @get_hook: Callback that gets a route entry
 *
 * Manipulating switch route tables in RIO is switch specific. This
 * registers a switch by vendor and device ID with two callbacks for
 * modifying and retrieving route entries in a switch. A &struct
 * rio_route_ops is initialized with the ops and placed into a
 * RIO-specific kernel section.
 */
#define DECLARE_RIO_ROUTE_OPS(vid, did, add_hook, get_hook, clr_hook)	\
	DECLARE_RIO_ROUTE_SECTION(.rio_route_ops, vid##did,		\
			vid, did, add_hook, get_hook, clr_hook)

#define RIO_GET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x00ff0000) >> 16))
#define RIO_SET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x000000ff) << 16))

/*
 *   RapidIO Error Management
 */
extern struct rio_em_ops __start_rio_em_ops[];
extern struct rio_em_ops __end_rio_em_ops[];

/* Helpers internal to the RIO core code */
#define DECLARE_RIO_EM_SECTION(section, name, vid, did, init_hook, em_hook)  \
	static const struct rio_em_ops __rio_em_##name __used   \
	__section(section) = { vid, did, init_hook, em_hook };

/**
 * DECLARE_RIO_EM_OPS - Registers switch EM operations
 * @vid: RIO vendor ID
 * @did: RIO device ID
 * @init_hook: Callback that initializes device specific EM
 * @em_hook: Callback that handles device specific EM
 *
 * A &struct rio_em_ops is initialized with the ops and placed into a
 * RIO-specific kernel section.
 */
#define DECLARE_RIO_EM_OPS(vid, did, init_hook, em_hook)	\
	DECLARE_RIO_EM_SECTION(.rio_em_ops, vid##did,		\
			vid, did, init_hook, em_hook)
