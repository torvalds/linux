/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Control and Management Interface (SCMI) Message Protocol Quirks
 *
 * Copyright (C) 2025 ARM Ltd.
 */
#ifndef _SCMI_QUIRKS_H
#define _SCMI_QUIRKS_H

#include <linux/static_key.h>
#include <linux/types.h>

#ifdef CONFIG_ARM_SCMI_QUIRKS

#define DECLARE_SCMI_QUIRK(_qn)						\
	DECLARE_STATIC_KEY_FALSE(scmi_quirk_ ## _qn)

/*
 * A helper to associate the actual code snippet to use as a quirk
 * named as _qn.
 */
#define SCMI_QUIRK(_qn, _blk)						\
	do {								\
		if (static_branch_unlikely(&(scmi_quirk_ ## _qn)))	\
			(_blk);						\
	} while (0)

void scmi_quirks_initialize(void);
void scmi_quirks_enable(struct device *dev, const char *vend,
			const char *subv, const u32 impl);

#else

#define DECLARE_SCMI_QUIRK(_qn)
/* Force quirks compilation even when SCMI Quirks are disabled */
#define SCMI_QUIRK(_qn, _blk)						\
	do {								\
		if (0)							\
			(_blk);						\
	} while (0)

static inline void scmi_quirks_initialize(void) { }
static inline void scmi_quirks_enable(struct device *dev, const char *vend,
				      const char *sub_vend, const u32 impl) { }

#endif /* CONFIG_ARM_SCMI_QUIRKS */

/* Quirk delarations */
DECLARE_SCMI_QUIRK(clock_rates_triplet_out_of_spec);
DECLARE_SCMI_QUIRK(perf_level_get_fc_force);

#endif /* _SCMI_QUIRKS_H */
