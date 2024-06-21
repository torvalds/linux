/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_ENCODER_H__
#define __INTEL_ENCODER_H__

struct intel_encoder;

void intel_encoder_link_check_init(struct intel_encoder *encoder,
				   void (*callback)(struct intel_encoder *encoder));
void intel_encoder_link_check_queue_work(struct intel_encoder *encoder, int delay_ms);
void intel_encoder_link_check_flush_work(struct intel_encoder *encoder);

#endif /* __INTEL_ENCODER_H__ */
