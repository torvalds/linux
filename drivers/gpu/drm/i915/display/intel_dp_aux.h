/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020-2021 Intel Corporation
 */

#ifndef __INTEL_DP_AUX_H__
#define __INTEL_DP_AUX_H__

#include <linux/types.h>

enum aux_ch;
struct intel_display;
struct intel_dp;
struct intel_encoder;

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

enum aux_ch intel_dp_aux_ch(struct intel_encoder *encoder);

void intel_dp_aux_irq_handler(struct intel_display *display);
u32 intel_dp_aux_pack(const u8 *src, int src_bytes);
int intel_dp_aux_fw_sync_len(struct intel_dp *intel_dp);

#endif /* __INTEL_DP_AUX_H__ */
