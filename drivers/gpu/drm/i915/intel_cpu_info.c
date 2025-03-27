// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 *
 * Avoid INTEL_<PLATFORM> name collisions between asm/intel-family.h and
 * intel_device_info.h by having a separate file.
 */

#include "intel_cpu_info.h"

#ifdef CONFIG_X86
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>

static const struct x86_cpu_id g8_cpu_ids[] = {
	X86_MATCH_VFM(INTEL_ALDERLAKE,		NULL),
	X86_MATCH_VFM(INTEL_ALDERLAKE_L,	NULL),
	X86_MATCH_VFM(INTEL_COMETLAKE,		NULL),
	X86_MATCH_VFM(INTEL_KABYLAKE,		NULL),
	X86_MATCH_VFM(INTEL_KABYLAKE_L,		NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE,		NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_P,	NULL),
	X86_MATCH_VFM(INTEL_RAPTORLAKE_S,	NULL),
	X86_MATCH_VFM(INTEL_ROCKETLAKE,		NULL),
	{}
};

/**
 * intel_match_g8_cpu - match current CPU against g8_cpu_ids
 *
 * This matches current CPU against g8_cpu_ids, which are applicable
 * for G8 workaround.
 *
 * Returns: %true if matches, %false otherwise.
 */
bool intel_match_g8_cpu(void)
{
	return x86_match_cpu(g8_cpu_ids);
}
#else /* CONFIG_X86 */

bool intel_match_g8_cpu(void) { return false; }

#endif /* CONFIG_X86 */
