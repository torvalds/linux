/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2020 Intel Corporation */

#ifndef _IGC_TSN_H_
#define _IGC_TSN_H_

int igc_tsn_offload_apply(struct igc_adapter *adapter);
int igc_tsn_reset(struct igc_adapter *adapter);
void igc_tsn_adjust_txtime_offset(struct igc_adapter *adapter);

#endif /* _IGC_BASE_H */
