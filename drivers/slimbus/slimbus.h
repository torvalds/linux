// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#ifndef _DRIVERS_SLIMBUS_H
#define _DRIVERS_SLIMBUS_H
#include <linux/module.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/slimbus.h>

/* SLIMbus message types. Related to interpretation of message code. */
#define SLIM_MSG_MT_CORE			0x0

/* Destination type Values */
#define SLIM_MSG_DEST_LOGICALADDR	0
#define SLIM_MSG_DEST_ENUMADDR		1
#define	SLIM_MSG_DEST_BROADCAST		3

/* Standard values per SLIMbus spec needed by controllers and devices */
#define SLIM_MAX_CLK_GEAR		10
#define SLIM_MIN_CLK_GEAR		1

/* Manager's logical address is set to 0xFF per spec */
#define SLIM_LA_MANAGER 0xFF

#define SLIM_MAX_TIDS			256
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
 * struct slim_msg_txn - Message to be sent by the controller.
 *			This structure has packet header,
 *			payload and buffer to be filled (if any)
 * @rl: Header field. remaining length.
 * @mt: Header field. Message type.
 * @mc: Header field. LSB is message code for type mt.
 * @dt: Header field. Destination type.
 * @ec: Element code. Used for elemental access APIs.
 * @tid: Transaction ID. Used for messages expecting response.
 *	(relevant for message-codes involving read operation)
 * @la: Logical address of the device this message is going to.
 *	(Not used when destination type is broadcast.)
 * @msg: Elemental access message to be read/written
 * @comp: completion if read/write is synchronous, used internally
 *	for tid based transactions.
 */
struct slim_msg_txn {
	u8			rl;
	u8			mt;
	u8			mc;
	u8			dt;
	u16			ec;
	u8			tid;
	u8			la;
	struct slim_val_inf	*msg;
	struct	completion	*comp;
};

/* Frequently used message transaction structures */
#define DEFINE_SLIM_LDEST_TXN(name, mc, rl, la, msg) \
	struct slim_msg_txn name = { rl, 0, mc, SLIM_MSG_DEST_LOGICALADDR, 0,\
					0, la, msg, }
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
 * @xfer_msg: Transfer a message on this controller (this can be a broadcast
 *	control/status message like data channel setup, or a unicast message
 *	like value element read/write.
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
	int			(*xfer_msg)(struct slim_controller *ctrl,
					    struct slim_msg_txn *tx);
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
void slim_msg_response(struct slim_controller *ctrl, u8 *reply, u8 tid, u8 l);
int slim_do_transfer(struct slim_controller *ctrl, struct slim_msg_txn *txn);

static inline bool slim_tid_txn(u8 mt, u8 mc)
{
	return (mt == SLIM_MSG_MT_CORE &&
		(mc == SLIM_MSG_MC_REQUEST_INFORMATION ||
		 mc == SLIM_MSG_MC_REQUEST_CLEAR_INFORMATION ||
		 mc == SLIM_MSG_MC_REQUEST_VALUE ||
		 mc == SLIM_MSG_MC_REQUEST_CLEAR_INFORMATION));
}

static inline bool slim_ec_txn(u8 mt, u8 mc)
{
	return (mt == SLIM_MSG_MT_CORE &&
		((mc >= SLIM_MSG_MC_REQUEST_INFORMATION &&
		  mc <= SLIM_MSG_MC_REPORT_INFORMATION) ||
		 (mc >= SLIM_MSG_MC_REQUEST_VALUE &&
		  mc <= SLIM_MSG_MC_CHANGE_VALUE)));
}
#endif /* _LINUX_SLIMBUS_H */
