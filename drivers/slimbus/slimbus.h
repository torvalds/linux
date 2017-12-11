// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#ifndef _DRIVERS_SLIMBUS_H
#define _DRIVERS_SLIMBUS_H
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/slimbus.h>

/* Standard values per SLIMbus spec needed by controllers and devices */
#define SLIM_MAX_CLK_GEAR		10
#define SLIM_MIN_CLK_GEAR		1

/* Manager's logical address is set to 0xFF per spec */
#define SLIM_LA_MANAGER 0xFF

/**
 * struct slim_framer - Represents SLIMbus framer.
 * Every controller may have multiple framers. There is 1 active framer device
 * responsible for clocking the bus.
 * Manager is responsible for framer hand-over.
 * @dev: Driver model representation of the device.
 * @e_addr: Enumeration address of the framer.
 * @rootfreq: Root Frequency at which the framer can run. This is maximum
 *	frequency ('clock gear 10') at which the bus can operate.
 * @superfreq: Superframes per root frequency. Every frame is 6144 bits.
 */
struct slim_framer {
	struct device		dev;
	struct slim_eaddr	e_addr;
	int			rootfreq;
	int			superfreq;
};

#define to_slim_framer(d) container_of(d, struct slim_framer, dev)

/**
 * struct slim_controller  - Controls every instance of SLIMbus
 *				(similar to 'master' on SPI)
 * @dev: Device interface to this driver
 * @id: Board-specific number identifier for this controller/bus
 * @name: Name for this controller
 * @min_cg: Minimum clock gear supported by this controller (default value: 1)
 * @max_cg: Maximum clock gear supported by this controller (default value: 10)
 * @clkgear: Current clock gear in which this bus is running
 * @laddr_ida: logical address id allocator
 * @a_framer: Active framer which is clocking the bus managed by this controller
 * @lock: Mutex protecting controller data structures
 * @devices: Slim device list
 * @tid_idr: tid id allocator
 * @txn_lock: Lock to protect table of transactions
 * @set_laddr: Setup logical address at laddr for the slave with elemental
 *	address e_addr. Drivers implementing controller will be expected to
 *	send unicast message to this device with its logical address.
 * @get_laddr: It is possible that controller needs to set fixed logical
 *	address table and get_laddr can be used in that case so that controller
 *	can do this assignment. Use case is when the master is on the remote
 *	processor side, who is resposible for allocating laddr.
 *
 *	'Manager device' is responsible for  device management, bandwidth
 *	allocation, channel setup, and port associations per channel.
 *	Device management means Logical address assignment/removal based on
 *	enumeration (report-present, report-absent) of a device.
 *	Bandwidth allocation is done dynamically by the manager based on active
 *	channels on the bus, message-bandwidth requests made by SLIMbus devices.
 *	Based on current bandwidth usage, manager chooses a frequency to run
 *	the bus at (in steps of 'clock-gear', 1 through 10, each clock gear
 *	representing twice the frequency than the previous gear).
 *	Manager is also responsible for entering (and exiting) low-power-mode
 *	(known as 'clock pause').
 *	Manager can do handover of framer if there are multiple framers on the
 *	bus and a certain usecase warrants using certain framer to avoid keeping
 *	previous framer being powered-on.
 *
 *	Controller here performs duties of the manager device, and 'interface
 *	device'. Interface device is responsible for monitoring the bus and
 *	reporting information such as loss-of-synchronization, data
 *	slot-collision.
 */
struct slim_controller {
	struct device		*dev;
	unsigned int		id;
	char			name[SLIMBUS_NAME_SIZE];
	int			min_cg;
	int			max_cg;
	int			clkgear;
	struct ida		laddr_ida;
	struct slim_framer	*a_framer;
	struct mutex		lock;
	struct list_head	devices;
	struct idr		tid_idr;
	spinlock_t		txn_lock;
	int			(*set_laddr)(struct slim_controller *ctrl,
					     struct slim_eaddr *ea, u8 laddr);
	int			(*get_laddr)(struct slim_controller *ctrl,
					     struct slim_eaddr *ea, u8 *laddr);
};

int slim_device_report_present(struct slim_controller *ctrl,
			       struct slim_eaddr *e_addr, u8 *laddr);
void slim_report_absent(struct slim_device *sbdev);
int slim_register_controller(struct slim_controller *ctrl);
int slim_unregister_controller(struct slim_controller *ctrl);

#endif /* _LINUX_SLIMBUS_H */
