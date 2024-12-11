/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024, Intel Corporation. */

#ifndef _HEALTH_H_
#define _HEALTH_H_

#include <linux/types.h>

/**
 * DOC: health.h
 *
 * This header file stores everything that is needed for broadly understood
 * devlink health mechanism for ice driver.
 */

struct ice_aqc_health_status_elem;
struct ice_pf;
struct ice_tx_ring;
struct ice_rq_event_info;

enum ice_mdd_src {
	ICE_MDD_SRC_TX_PQM,
	ICE_MDD_SRC_TX_TCLAN,
	ICE_MDD_SRC_TX_TDPU,
	ICE_MDD_SRC_RX,
};

/**
 * struct ice_health - stores ice devlink health reporters and accompanied data
 * @fw: devlink health reporter for FW Health Status events
 * @mdd: devlink health reporter for MDD detection event
 * @port: devlink health reporter for Port Health Status events
 * @tx_hang: devlink health reporter for tx_hang event
 * @tx_hang_buf: pre-allocated place to put info for Tx hang reporter from
 *               non-sleeping context
 * @tx_ring: ring that the hang occurred on
 * @head: descriptor head
 * @intr: interrupt register value
 * @vsi_num: VSI owning the queue that the hang occurred on
 * @fw_status: buffer for last received FW Status event
 * @port_status: buffer for last received Port Status event
 */
struct ice_health {
	struct devlink_health_reporter *fw;
	struct devlink_health_reporter *mdd;
	struct devlink_health_reporter *port;
	struct devlink_health_reporter *tx_hang;
	struct_group_tagged(ice_health_tx_hang_buf, tx_hang_buf,
		struct ice_tx_ring *tx_ring;
		u32 head;
		u32 intr;
		u16 vsi_num;
	);
	struct ice_aqc_health_status_elem fw_status;
	struct ice_aqc_health_status_elem port_status;
};

void ice_process_health_status_event(struct ice_pf *pf,
				     struct ice_rq_event_info *event);

void ice_health_init(struct ice_pf *pf);
void ice_health_deinit(struct ice_pf *pf);
void ice_health_clear(struct ice_pf *pf);

void ice_prep_tx_hang_report(struct ice_pf *pf, struct ice_tx_ring *tx_ring,
			     u16 vsi_num, u32 head, u32 intr);
void ice_report_mdd_event(struct ice_pf *pf, enum ice_mdd_src src, u8 pf_num,
			  u16 vf_num, u8 event, u16 queue);
void ice_report_tx_hang(struct ice_pf *pf);

#endif /* _HEALTH_H_ */
