/*
 *  Copyright (C) 2013-2014 Chelsio Communications.  All rights reserved.
 *
 *  Written by Anish Bhatt (anish@chelsio.com)
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#ifndef __CXGB4_DCB_H
#define __CXGB4_DCB_H

#include <linux/netdevice.h>
#include <linux/dcbnl.h>
#include <net/dcbnl.h>

#ifdef CONFIG_CHELSIO_T4_DCB

#define CXGB4_DCBX_FW_SUPPORT \
	(DCB_CAP_DCBX_VER_CEE | \
	 DCB_CAP_DCBX_VER_IEEE | \
	 DCB_CAP_DCBX_LLD_MANAGED)
#define CXGB4_DCBX_HOST_SUPPORT \
	(DCB_CAP_DCBX_VER_CEE | \
	 DCB_CAP_DCBX_VER_IEEE | \
	 DCB_CAP_DCBX_HOST)

#define CXGB4_MAX_PRIORITY      CXGB4_MAX_DCBX_APP_SUPPORTED
#define CXGB4_MAX_TCS           CXGB4_MAX_DCBX_APP_SUPPORTED

#define INIT_PORT_DCB_CMD(__pcmd, __port, __op, __action) \
	do { \
		memset(&(__pcmd), 0, sizeof(__pcmd)); \
		(__pcmd).op_to_portid = \
			cpu_to_be32(FW_CMD_OP_V(FW_PORT_CMD) | \
				    FW_CMD_REQUEST_F | \
				    FW_CMD_##__op##_F | \
				    FW_PORT_CMD_PORTID_V(__port)); \
		(__pcmd).action_to_len16 = \
			cpu_to_be32(FW_PORT_CMD_ACTION_V(__action) | \
				    FW_LEN16(pcmd)); \
	} while (0)

#define INIT_PORT_DCB_READ_PEER_CMD(__pcmd, __port) \
	INIT_PORT_DCB_CMD(__pcmd, __port, READ, FW_PORT_ACTION_DCB_READ_RECV)

#define INIT_PORT_DCB_READ_LOCAL_CMD(__pcmd, __port) \
	INIT_PORT_DCB_CMD(__pcmd, __port, READ, FW_PORT_ACTION_DCB_READ_TRANS)

#define INIT_PORT_DCB_READ_SYNC_CMD(__pcmd, __port) \
	INIT_PORT_DCB_CMD(__pcmd, __port, READ, FW_PORT_ACTION_DCB_READ_DET)

#define INIT_PORT_DCB_WRITE_CMD(__pcmd, __port) \
	INIT_PORT_DCB_CMD(__pcmd, __port, EXEC, FW_PORT_ACTION_L2_DCB_CFG)

#define IEEE_FAUX_SYNC(__dev, __dcb) \
	do { \
		if ((__dcb)->dcb_version == FW_PORT_DCB_VER_IEEE) \
			cxgb4_dcb_state_fsm((__dev), \
					    CXGB4_DCB_STATE_FW_ALLSYNCED); \
	} while (0)

/* States we can be in for a port's Data Center Bridging.
 */
enum cxgb4_dcb_state {
	CXGB4_DCB_STATE_START,		/* initial unknown state */
	CXGB4_DCB_STATE_HOST,		/* we're using Host DCB (if at all) */
	CXGB4_DCB_STATE_FW_INCOMPLETE,	/* using firmware DCB, incomplete */
	CXGB4_DCB_STATE_FW_ALLSYNCED,	/* using firmware DCB, all sync'ed */
};

/* Data Center Bridging state input for the Finite State Machine.
 */
enum cxgb4_dcb_state_input {
	/* Input from the firmware.
	 */
	CXGB4_DCB_INPUT_FW_DISABLED,	/* firmware DCB disabled */
	CXGB4_DCB_INPUT_FW_ENABLED,	/* firmware DCB enabled */
	CXGB4_DCB_INPUT_FW_INCOMPLETE,	/* firmware reports incomplete DCB */
	CXGB4_DCB_INPUT_FW_ALLSYNCED,	/* firmware reports all sync'ed */

};

/* Firmware DCB messages that we've received so far ...
 */
enum cxgb4_dcb_fw_msgs {
	CXGB4_DCB_FW_PGID	= 0x01,
	CXGB4_DCB_FW_PGRATE	= 0x02,
	CXGB4_DCB_FW_PRIORATE	= 0x04,
	CXGB4_DCB_FW_PFC	= 0x08,
	CXGB4_DCB_FW_APP_ID	= 0x10,
};

#define CXGB4_MAX_DCBX_APP_SUPPORTED 8

/* Data Center Bridging support;
 */
struct port_dcb_info {
	enum cxgb4_dcb_state state;	/* DCB State Machine */
	enum cxgb4_dcb_fw_msgs msgs;	/* DCB Firmware messages received */
	unsigned int supported;		/* OS DCB capabilities supported */
	bool enabled;			/* OS Enabled state */

	/* Cached copies of DCB information sent by the firmware (in Host
	 * Native Endian format).
	 */
	u32	pgid;			/* Priority Group[0..7] */
	u8	dcb_version;		/* Running DCBx version */
	u8	pfcen;			/* Priority Flow Control[0..7] */
	u8	pg_num_tcs_supported;	/* max PG Traffic Classes */
	u8	pfc_num_tcs_supported;	/* max PFC Traffic Classes */
	u8	pgrate[8];		/* Priority Group Rate[0..7] */
	u8	priorate[8];		/* Priority Rate[0..7] */
	u8	tsa[8];			/* TSA Algorithm[0..7] */
	struct app_priority { /* Application Information */
		u8	user_prio_map;	/* Priority Map bitfield */
		u8	sel_field;	/* Protocol ID interpretation */
		u16	protocolid;	/* Protocol ID */
	} app_priority[CXGB4_MAX_DCBX_APP_SUPPORTED];
};

void cxgb4_dcb_state_init(struct net_device *);
void cxgb4_dcb_version_init(struct net_device *);
void cxgb4_dcb_state_fsm(struct net_device *, enum cxgb4_dcb_state_input);
void cxgb4_dcb_handle_fw_update(struct adapter *, const struct fw_port_cmd *);
void cxgb4_dcb_set_caps(struct adapter *, const struct fw_port_cmd *);
extern const struct dcbnl_rtnl_ops cxgb4_dcb_ops;

static inline __u8 bitswap_1(unsigned char val)
{
	return ((val & 0x80) >> 7) |
	       ((val & 0x40) >> 5) |
	       ((val & 0x20) >> 3) |
	       ((val & 0x10) >> 1) |
	       ((val & 0x08) << 1) |
	       ((val & 0x04) << 3) |
	       ((val & 0x02) << 5) |
	       ((val & 0x01) << 7);
}
#define CXGB4_DCB_ENABLED true

#else /* !CONFIG_CHELSIO_T4_DCB */

static inline void cxgb4_dcb_state_init(struct net_device *dev)
{
}

#define CXGB4_DCB_ENABLED false

#endif /* !CONFIG_CHELSIO_T4_DCB */

#endif /* __CXGB4_DCB_H */
