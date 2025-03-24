/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_MAC_H_
#define _FBNIC_MAC_H_

#include <linux/types.h>

struct fbnic_dev;

#define FBNIC_MAX_JUMBO_FRAME_SIZE	9742

enum {
	FBNIC_LINK_EVENT_NONE	= 0,
	FBNIC_LINK_EVENT_UP	= 1,
	FBNIC_LINK_EVENT_DOWN	= 2,
};

/* Treat the FEC bits as a bitmask laid out as follows:
 * Bit 0: RS Enabled
 * Bit 1: BASER(Firecode) Enabled
 * Bit 2: Retrieve FEC from FW
 */
enum {
	FBNIC_FEC_OFF		= 0,
	FBNIC_FEC_RS		= 1,
	FBNIC_FEC_BASER		= 2,
	FBNIC_FEC_AUTO		= 4,
};

#define FBNIC_FEC_MODE_MASK	(FBNIC_FEC_AUTO - 1)

/* Treat the link modes as a set of modulation/lanes bitmask:
 * Bit 0: Lane Count, 0 = R1, 1 = R2
 * Bit 1: Modulation, 0 = NRZ, 1 = PAM4
 * Bit 2: Retrieve link mode from FW
 */
enum {
	FBNIC_LINK_25R1		= 0,
	FBNIC_LINK_50R2		= 1,
	FBNIC_LINK_50R1		= 2,
	FBNIC_LINK_100R2	= 3,
	FBNIC_LINK_AUTO		= 4,
};

#define FBNIC_LINK_MODE_R2	(FBNIC_LINK_50R2)
#define FBNIC_LINK_MODE_PAM4	(FBNIC_LINK_50R1)
#define FBNIC_LINK_MODE_MASK	(FBNIC_LINK_AUTO - 1)

enum fbnic_sensor_id {
	FBNIC_SENSOR_TEMP,		/* Temp in millidegrees Centigrade */
	FBNIC_SENSOR_VOLTAGE,		/* Voltage in millivolts */
};

/* This structure defines the interface hooks for the MAC. The MAC hooks
 * will be configured as a const struct provided with a set of function
 * pointers.
 *
 * void (*init_regs)(struct fbnic_dev *fbd);
 *	Initialize MAC registers to enable Tx/Rx paths and FIFOs.
 *
 * void (*pcs_enable)(struct fbnic_dev *fbd);
 *	Configure and enable PCS to enable link if not already enabled
 * void (*pcs_disable)(struct fbnic_dev *fbd);
 *	Shutdown the link if we are the only consumer of it.
 * bool (*pcs_get_link)(struct fbnic_dev *fbd);
 *	Check PCS link status
 * int (*pcs_get_link_event)(struct fbnic_dev *fbd)
 *	Get the current link event status, reports true if link has
 *	changed to either FBNIC_LINK_EVENT_DOWN or FBNIC_LINK_EVENT_UP
 *
 * void (*link_down)(struct fbnic_dev *fbd);
 *	Configure MAC for link down event
 * void (*link_up)(struct fbnic_dev *fbd, bool tx_pause, bool rx_pause);
 *	Configure MAC for link up event;
 *
 */
struct fbnic_mac {
	void (*init_regs)(struct fbnic_dev *fbd);

	int (*pcs_enable)(struct fbnic_dev *fbd);
	void (*pcs_disable)(struct fbnic_dev *fbd);
	bool (*pcs_get_link)(struct fbnic_dev *fbd);
	int (*pcs_get_link_event)(struct fbnic_dev *fbd);

	void (*get_eth_mac_stats)(struct fbnic_dev *fbd, bool reset,
				  struct fbnic_eth_mac_stats *mac_stats);

	void (*link_down)(struct fbnic_dev *fbd);
	void (*link_up)(struct fbnic_dev *fbd, bool tx_pause, bool rx_pause);

	int (*get_sensor)(struct fbnic_dev *fbd, int id, long *val);
};

int fbnic_mac_init(struct fbnic_dev *fbd);
#endif /* _FBNIC_MAC_H_ */
