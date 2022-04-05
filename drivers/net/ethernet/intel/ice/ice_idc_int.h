/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _ICE_IDC_INT_H_
#define _ICE_IDC_INT_H_

#include <linux/net/intel/iidc.h>

struct ice_pf;

void ice_send_event_to_aux(struct ice_pf *pf, struct iidc_event *event);

#endif /* !_ICE_IDC_INT_H_ */
