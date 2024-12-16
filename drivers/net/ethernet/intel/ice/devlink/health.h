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

struct ice_pf;
struct ice_tx_ring;

/**
 * struct ice_health - stores ice devlink health reporters and accompanied data
 * @tx_hang: devlink health reporter for tx_hang event
 * @tx_hang_buf: pre-allocated place to put info for Tx hang reporter from
 *               non-sleeping context
 * @tx_ring: ring that the hang occurred on
 * @head: descriptor head
 * @intr: interrupt register value
 * @vsi_num: VSI owning the queue that the hang occurred on
 */
struct ice_health {
	struct devlink_health_reporter *tx_hang;
	struct_group_tagged(ice_health_tx_hang_buf, tx_hang_buf,
		struct ice_tx_ring *tx_ring;
		u32 head;
		u32 intr;
		u16 vsi_num;
	);
};

void ice_health_init(struct ice_pf *pf);
void ice_health_deinit(struct ice_pf *pf);
void ice_health_clear(struct ice_pf *pf);

void ice_prep_tx_hang_report(struct ice_pf *pf, struct ice_tx_ring *tx_ring,
			     u16 vsi_num, u32 head, u32 intr);
void ice_report_tx_hang(struct ice_pf *pf);

#endif /* _HEALTH_H_ */
