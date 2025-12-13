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
};

/* Treat the AUI modes as a modulation/lanes bitmask:
 * Bit 0: Lane Count, 0 = R1, 1 = R2
 * Bit 1: Modulation, 0 = NRZ, 1 = PAM4
 * Bit 2: Unknown Modulation/Lane Configuration
 */
enum {
	FBNIC_AUI_25GAUI	= 0,	/* 25.7812GBd	25.78125 * 1 */
	FBNIC_AUI_LAUI2		= 1,	/* 51.5625GBd	25.78128 * 2 */
	FBNIC_AUI_50GAUI1	= 2,	/* 53.125GBd	53.125   * 1 */
	FBNIC_AUI_100GAUI2	= 3,	/* 106.25GBd	53.125   * 2 */
	FBNIC_AUI_UNKNOWN	= 4,
};

#define FBNIC_AUI_MODE_R2	(FBNIC_AUI_LAUI2)
#define FBNIC_AUI_MODE_PAM4	(FBNIC_AUI_50GAUI1)

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

	void (*get_fec_stats)(struct fbnic_dev *fbd, bool reset,
			      struct fbnic_fec_stats *fec_stats);
	void (*get_pcs_stats)(struct fbnic_dev *fbd, bool reset,
			      struct fbnic_pcs_stats *pcs_stats);
	void (*get_eth_mac_stats)(struct fbnic_dev *fbd, bool reset,
				  struct fbnic_eth_mac_stats *mac_stats);
	void (*get_pause_stats)(struct fbnic_dev *fbd, bool reset,
				struct fbnic_pause_stats *pause_stats);
	void (*get_eth_ctrl_stats)(struct fbnic_dev *fbd, bool reset,
				   struct fbnic_eth_ctrl_stats *ctrl_stats);
	void (*get_rmon_stats)(struct fbnic_dev *fbd, bool reset,
			       struct fbnic_rmon_stats *rmon_stats);

	void (*link_down)(struct fbnic_dev *fbd);
	void (*link_up)(struct fbnic_dev *fbd, bool tx_pause, bool rx_pause);

	int (*get_sensor)(struct fbnic_dev *fbd, int id, long *val);
};

int fbnic_mac_init(struct fbnic_dev *fbd);
void fbnic_mac_get_fw_settings(struct fbnic_dev *fbd, u8 *aui, u8 *fec);
#endif /* _FBNIC_MAC_H_ */
