/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "../hal.h"
#include "hal.h"

extern const struct ath12k_hw_regs qcc2072_regs;
extern const struct hal_ops hal_qcc2072_ops;

u32 ath12k_hal_rx_desc_get_mpdu_start_offset_qcc2072(void);
u32 ath12k_hal_rx_desc_get_msdu_end_offset_qcc2072(void);
