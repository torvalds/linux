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

#define RIO_MAX_CHK_RETRY	3
#define RIO_MPORT_ANY		(-1)

/* Functions internal to the RIO core code */

extern u32 rio_mport_get_feature(struct rio_mport *mport, int local, u16 destid,
				 u8 hopcount, int ftr);
extern u32 rio_mport_get_physefb(struct rio_mport *port, int local,
				 u16 destid, u8 hopcount);
extern u32 rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
			     u8 hopcount, u32 from);
extern int rio_mport_chk_dev_access(struct rio_mport *mport, u16 destid,
				    u8 hopcount);
extern int rio_create_sysfs_dev_files(struct rio_dev *rdev);
extern int rio_lock_device(struct rio_mport *port, u16 destid,
			u8 hopcount, int wait_ms);
extern int rio_unlock_device(struct rio_mport *port, u16 destid, u8 hopcount);
extern int rio_route_add_entry(struct rio_dev *rdev,
			u16 table, u16 route_destid, u8 route_port, int lock);
extern int rio_route_get_entry(struct rio_dev *rdev, u16 table,
			u16 route_destid, u8 *route_port, int lock);
extern int rio_route_clr_table(struct rio_dev *rdev, u16 table, int lock);
extern int rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock);
extern struct rio_dev *rio_get_comptag(u32 comp_tag, struct rio_dev *from);
extern int rio_add_device(struct rio_dev *rdev);
extern int rio_enable_rx_tx_port(struct rio_mport *port, int local, u16 destid,
				 u8 hopcount, u8 port_num);
extern int rio_register_scan(int mport_id, struct rio_scan *scan_ops);
extern int rio_unregister_scan(int mport_id, struct rio_scan *scan_ops);
extern void rio_attach_device(struct rio_dev *rdev);
extern struct rio_mport *rio_find_mport(int mport_id);
extern int rio_mport_scan(int mport_id);

/* Structures internal to the RIO core code */
extern const struct attribute_group *rio_dev_groups[];
extern const struct attribute_group *rio_bus_groups[];
extern const struct attribute_group *rio_mport_groups[];

#define RIO_GET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x00ff0000) >> 16))
#define RIO_SET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x000000ff) << 16))
