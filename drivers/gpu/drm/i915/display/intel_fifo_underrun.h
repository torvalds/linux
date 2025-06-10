/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_FIFO_UNDERRUN_H__
#define __INTEL_FIFO_UNDERRUN_H__

#include <linux/types.h>

enum pipe;
struct intel_crtc;
struct intel_display;

void intel_init_fifo_underrun_reporting(struct intel_display *display,
					struct intel_crtc *crtc, bool enable);
bool intel_set_cpu_fifo_underrun_reporting(struct intel_display *display,
					   enum pipe pipe, bool enable);
bool intel_set_pch_fifo_underrun_reporting(struct intel_display *display,
					   enum pipe pch_transcoder,
					   bool enable);
void intel_cpu_fifo_underrun_irq_handler(struct intel_display *display,
					 enum pipe pipe);
void intel_pch_fifo_underrun_irq_handler(struct intel_display *display,
					 enum pipe pch_transcoder);
void intel_check_cpu_fifo_underruns(struct intel_display *display);
void intel_check_pch_fifo_underruns(struct intel_display *display);

#endif /* __INTEL_FIFO_UNDERRUN_H__ */
