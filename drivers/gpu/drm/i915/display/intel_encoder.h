/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __INTEL_ENCODER_H__
#define __INTEL_ENCODER_H__

struct intel_digital_port;
struct intel_display;
struct intel_encoder;

void intel_encoder_link_check_init(struct intel_encoder *encoder,
				   void (*callback)(struct intel_encoder *encoder));
void intel_encoder_link_check_queue_work(struct intel_encoder *encoder, int delay_ms);
void intel_encoder_link_check_flush_work(struct intel_encoder *encoder);

void intel_encoder_suspend_all(struct intel_display *display);
void intel_encoder_shutdown_all(struct intel_display *display);

void intel_encoder_block_all_hpds(struct intel_display *display);
void intel_encoder_unblock_all_hpds(struct intel_display *display);

struct intel_digital_port *intel_dig_port_alloc(void);

#endif /* __INTEL_ENCODER_H__ */
