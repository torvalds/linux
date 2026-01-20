/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_MAC_H_
#define _FBNIC_MAC_H_

#include <linux/types.h>

struct fbnic_dev;

#define FBNIC_MAX_JUMBO_FRAME_SIZE	9742

/* States loosely based on section 136.8.11.7.5 of IEEE 802.3-2022 Ethernet
 * Standard.  These are needed to track the state of the PHY as it has a delay
 * of several seconds from the time link comes up until it has completed
 * training that we need to wait to report the link.
 *
 * Currently we treat training as a single block as this is managed by the
 * firmware.
 *
 * We have FBNIC_PMD_SEND_DATA set to 0 as the expected default at driver load
 * and we initialize the structure containing it to zero at allocation.
 */
enum {
	FBNIC_PMD_SEND_DATA	= 0x0,
	FBNIC_PMD_INITIALIZE	= 0x1,
	FBNIC_PMD_TRAINING	= 0x2,
	FBNIC_PMD_LINK_READY	= 0x3,
};

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
	__FBNIC_AUI_MAX__
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
 * int (*get_link_event)(struct fbnic_dev *fbd)
 *	Get the current link event status, reports true if link has
 *	changed to either FBNIC_LINK_EVENT_DOWN or FBNIC_LINK_EVENT_UP
 * bool (*get_link)(struct fbnic_dev *fbd, u8 aui, u8 fec);
 *	Check link status
 *
 * void (*prepare)(struct fbnic_dev *fbd, u8 aui, u8 fec);
 *	Prepare PHY for init by fetching settings, disabling interrupts,
 *	and sending an updated PHY config to FW if needed.
 *
 * void (*link_down)(struct fbnic_dev *fbd);
 *	Configure MAC for link down event
 * void (*link_up)(struct fbnic_dev *fbd, bool tx_pause, bool rx_pause);
 *	Configure MAC for link up event;
 *
 */
struct fbnic_mac {
	void (*init_regs)(struct fbnic_dev *fbd);

	int (*get_link_event)(struct fbnic_dev *fbd);
	bool (*get_link)(struct fbnic_dev *fbd, u8 aui, u8 fec);

	void (*prepare)(struct fbnic_dev *fbd, u8 aui, u8 fec);

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
