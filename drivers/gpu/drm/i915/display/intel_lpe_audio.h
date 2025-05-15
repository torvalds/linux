/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_LPE_AUDIO_H__
#define __INTEL_LPE_AUDIO_H__

#include <linux/types.h>

enum port;
enum transcoder;
struct intel_display;

#ifdef I915
int  intel_lpe_audio_init(struct intel_display *display);
void intel_lpe_audio_teardown(struct intel_display *display);
void intel_lpe_audio_irq_handler(struct intel_display *display);
void intel_lpe_audio_notify(struct intel_display *display,
			    enum transcoder cpu_transcoder, enum port port,
			    const void *eld, int ls_clock, bool dp_output);
#else
static inline int intel_lpe_audio_init(struct intel_display *display)
{
	return -ENODEV;
}
static inline void intel_lpe_audio_teardown(struct intel_display *display)
{
}
static inline void intel_lpe_audio_irq_handler(struct intel_display *display)
{
}
static inline void intel_lpe_audio_notify(struct intel_display *display,
					  enum transcoder cpu_transcoder, enum port port,
					  const void *eld, int ls_clock, bool dp_output)
{
}
#endif

#endif /* __INTEL_LPE_AUDIO_H__ */
