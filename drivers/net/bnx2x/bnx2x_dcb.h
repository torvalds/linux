/* bnx2x_dcb.h: Broadcom Everest network driver.
 *
 * Copyright 2009-2010 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Dmitry Kravkov
 *
 */
#ifndef BNX2X_DCB_H
#define BNX2X_DCB_H

#include "bnx2x_hsi.h"

#define LLFC_DRIVER_TRAFFIC_TYPE_MAX 3 /* NW, iSCSI, FCoE */
struct bnx2x_dcbx_app_params {
	u32 enabled;
	u32 traffic_type_priority[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
};

#define E2_NUM_OF_COS			2
#define BNX2X_DCBX_COS_NOT_STRICT	0
#define BNX2X_DCBX_COS_LOW_STRICT	1
#define BNX2X_DCBX_COS_HIGH_STRICT	2

struct bnx2x_dcbx_cos_params {
	u32	bw_tbl;
	u32	pri_bitmask;
	u8	strict;
	u8	pauseable;
};

struct bnx2x_dcbx_pg_params {
	u32 enabled;
	u8 num_of_cos; /* valid COS entries */
	struct bnx2x_dcbx_cos_params	cos_params[E2_NUM_OF_COS];
};

struct bnx2x_dcbx_pfc_params {
	u32 enabled;
	u32 priority_non_pauseable_mask;
};

struct bnx2x_dcbx_port_params {
	struct bnx2x_dcbx_pfc_params pfc;
	struct bnx2x_dcbx_pg_params  ets;
	struct bnx2x_dcbx_app_params app;
};

#define BNX2X_DCBX_CONFIG_INV_VALUE			(0xFFFFFFFF)
#define BNX2X_DCBX_OVERWRITE_SETTINGS_DISABLE		0
#define BNX2X_DCBX_OVERWRITE_SETTINGS_ENABLE		1
#define BNX2X_DCBX_OVERWRITE_SETTINGS_INVALID	(BNX2X_DCBX_CONFIG_INV_VALUE)

/*******************************************************************************
 * LLDP protocol configuration parameters.
 ******************************************************************************/
struct bnx2x_config_lldp_params {
	u32 overwrite_settings;
	u32 msg_tx_hold;
	u32 msg_fast_tx;
	u32 tx_credit_max;
	u32 msg_tx_interval;
	u32 tx_fast;
};

struct bnx2x_admin_priority_app_table {
		u32 valid;
		u32 priority;
#define INVALID_TRAFFIC_TYPE_PRIORITY	(0xFFFFFFFF)
		u32 traffic_type;
#define TRAFFIC_TYPE_ETH		0
#define TRAFFIC_TYPE_PORT		1
		u32 app_id;
};

/*******************************************************************************
 * DCBX protocol configuration parameters.
 ******************************************************************************/
struct bnx2x_config_dcbx_params {
	u32 overwrite_settings;
	u32 admin_dcbx_version;
	u32 admin_ets_enable;
	u32 admin_pfc_enable;
	u32 admin_tc_supported_tx_enable;
	u32 admin_ets_configuration_tx_enable;
	u32 admin_ets_recommendation_tx_enable;
	u32 admin_pfc_tx_enable;
	u32 admin_application_priority_tx_enable;
	u32 admin_ets_willing;
	u32 admin_ets_reco_valid;
	u32 admin_pfc_willing;
	u32 admin_app_priority_willing;
	u32 admin_configuration_bw_precentage[8];
	u32 admin_configuration_ets_pg[8];
	u32 admin_recommendation_bw_precentage[8];
	u32 admin_recommendation_ets_pg[8];
	u32 admin_pfc_bitmap;
	struct bnx2x_admin_priority_app_table admin_priority_app_table[4];
	u32 admin_default_priority;
};

#define GET_FLAGS(flags, bits)		((flags) & (bits))
#define SET_FLAGS(flags, bits)		((flags) |= (bits))
#define RESET_FLAGS(flags, bits)	((flags) &= ~(bits))

enum {
	DCBX_READ_LOCAL_MIB,
	DCBX_READ_REMOTE_MIB
};

#define ETH_TYPE_FCOE		(0x8906)
#define TCP_PORT_ISCSI		(0xCBC)

#define PFC_VALUE_FRAME_SIZE				(512)
#define PFC_QUANTA_IN_NANOSEC_FROM_SPEED_MEGA(mega_speed)  \
				((1000 * PFC_VALUE_FRAME_SIZE)/(mega_speed))

#define PFC_BRB1_REG_HIGH_LLFC_LOW_THRESHOLD			130
#define PFC_BRB1_REG_HIGH_LLFC_HIGH_THRESHOLD			170



struct cos_entry_help_data {
	u32			pri_join_mask;
	u32			cos_bw;
	u8			strict;
	bool			pausable;
};

struct cos_help_data {
	struct cos_entry_help_data	data[E2_NUM_OF_COS];
	u8				num_of_cos;
};

#define DCBX_ILLEGAL_PG				(0xFF)
#define DCBX_PFC_PRI_MASK			(0xFF)
#define DCBX_STRICT_PRIORITY			(15)
#define DCBX_INVALID_COS_BW			(0xFFFFFFFF)
#define DCBX_PFC_PRI_NON_PAUSE_MASK(bp)		\
			((bp)->dcbx_port_params.pfc.priority_non_pauseable_mask)
#define DCBX_PFC_PRI_PAUSE_MASK(bp)		\
					((u8)~DCBX_PFC_PRI_NON_PAUSE_MASK(bp))
#define DCBX_PFC_PRI_GET_PAUSE(bp, pg_pri)	\
				((pg_pri) & (DCBX_PFC_PRI_PAUSE_MASK(bp)))
#define DCBX_PFC_PRI_GET_NON_PAUSE(bp, pg_pri)	\
			(DCBX_PFC_PRI_NON_PAUSE_MASK(bp) & (pg_pri))
#define IS_DCBX_PFC_PRI_ONLY_PAUSE(bp, pg_pri)	\
			(pg_pri == DCBX_PFC_PRI_GET_PAUSE((bp), (pg_pri)))
#define IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(bp, pg_pri)\
			((pg_pri) == DCBX_PFC_PRI_GET_NON_PAUSE((bp), (pg_pri)))
#define IS_DCBX_PFC_PRI_MIX_PAUSE(bp, pg_pri)	\
			(!(IS_DCBX_PFC_PRI_ONLY_NON_PAUSE((bp), (pg_pri)) || \
			 IS_DCBX_PFC_PRI_ONLY_PAUSE((bp), (pg_pri))))


struct pg_entry_help_data {
	u8	num_of_dif_pri;
	u8	pg;
	u32	pg_priority;
};

struct pg_help_data {
	struct pg_entry_help_data	data[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
	u8				num_of_pg;
};

/* forward DCB/PFC related declarations */
struct bnx2x;
void bnx2x_dcb_init_intmem_pfc(struct bnx2x *bp);
void bnx2x_dcbx_update(struct work_struct *work);
void bnx2x_dcbx_init_params(struct bnx2x *bp);
void bnx2x_dcbx_set_state(struct bnx2x *bp, bool dcb_on, u32 dcbx_enabled);

enum {
	BNX2X_DCBX_STATE_NEG_RECEIVED = 0x1,
	BNX2X_DCBX_STATE_TX_PAUSED = 0x2,
	BNX2X_DCBX_STATE_TX_RELEASED = 0x4
};
void bnx2x_dcbx_set_params(struct bnx2x *bp, u32 state);

/* DCB netlink */
#ifdef BCM_DCB
extern const struct dcbnl_rtnl_ops bnx2x_dcbnl_ops;
#endif /* BCM_DCB */

#endif /* BNX2X_DCB_H */
