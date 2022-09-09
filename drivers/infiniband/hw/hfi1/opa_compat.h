/* SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause */
/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 */

#ifndef _LINUX_H
#define _LINUX_H
/*
 * This header file is for OPA-specific definitions which are
 * required by the HFI driver, and which aren't yet in the Linux
 * IB core. We'll collect these all here, then merge them into
 * the kernel when that's convenient.
 */

/* OPA SMA attribute IDs */
#define OPA_ATTRIB_ID_CONGESTION_INFO		cpu_to_be16(0x008b)
#define OPA_ATTRIB_ID_HFI_CONGESTION_LOG	cpu_to_be16(0x008f)
#define OPA_ATTRIB_ID_HFI_CONGESTION_SETTING	cpu_to_be16(0x0090)
#define OPA_ATTRIB_ID_CONGESTION_CONTROL_TABLE	cpu_to_be16(0x0091)

/* OPA PMA attribute IDs */
#define OPA_PM_ATTRIB_ID_PORT_STATUS		cpu_to_be16(0x0040)
#define OPA_PM_ATTRIB_ID_CLEAR_PORT_STATUS	cpu_to_be16(0x0041)
#define OPA_PM_ATTRIB_ID_DATA_PORT_COUNTERS	cpu_to_be16(0x0042)
#define OPA_PM_ATTRIB_ID_ERROR_PORT_COUNTERS	cpu_to_be16(0x0043)
#define OPA_PM_ATTRIB_ID_ERROR_INFO		cpu_to_be16(0x0044)

/* OPA status codes */
#define OPA_PM_STATUS_REQUEST_TOO_LARGE		cpu_to_be16(0x100)

static inline u8 port_states_to_logical_state(struct opa_port_states *ps)
{
	return ps->portphysstate_portstate & OPA_PI_MASK_PORT_STATE;
}

static inline u8 port_states_to_phys_state(struct opa_port_states *ps)
{
	return ((ps->portphysstate_portstate &
		  OPA_PI_MASK_PORT_PHYSICAL_STATE) >> 4) & 0xf;
}

/*
 * OPA port physical states
 * IB Volume 1, Table 146 PortInfo/IB Volume 2 Section 5.4.2(1) PortPhysState
 * values are the same in OmniPath Architecture. OPA leverages some of the same
 * concepts as InfiniBand, but has a few other states as well.
 *
 * When writing, only values 0-3 are valid, other values are ignored.
 * When reading, 0 is reserved.
 *
 * Returned by the ibphys_portstate() routine.
 */
enum opa_port_phys_state {
	/* Values 0-7 have the same meaning in OPA as in InfiniBand. */

	IB_PORTPHYSSTATE_NOP = 0,
	/* 1 is reserved */
	IB_PORTPHYSSTATE_POLLING = 2,
	IB_PORTPHYSSTATE_DISABLED = 3,
	IB_PORTPHYSSTATE_TRAINING = 4,
	IB_PORTPHYSSTATE_LINKUP = 5,
	IB_PORTPHYSSTATE_LINK_ERROR_RECOVERY = 6,
	IB_PORTPHYSSTATE_PHY_TEST = 7,
	/* 8 is reserved */

	/*
	 * Offline: Port is quiet (transmitters disabled) due to lack of
	 * physical media, unsupported media, or transition between link up
	 * and next link up attempt
	 */
	OPA_PORTPHYSSTATE_OFFLINE = 9,

	/* 10 is reserved */

	/*
	 * Phy_Test: Specific test patterns are transmitted, and receiver BER
	 * can be monitored. This facilitates signal integrity testing for the
	 * physical layer of the port.
	 */
	OPA_PORTPHYSSTATE_TEST = 11,

	OPA_PORTPHYSSTATE_MAX = 11,
	/* values 12-15 are reserved/ignored */
};

#endif /* _LINUX_H */
