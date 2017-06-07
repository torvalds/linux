/*
 * Nest Performance Monitor counter support for POWER8 processors.
 *
 * Copyright (C) 2015 Madhavan Srinivasan, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/perf_event.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/io.h>
#include <asm/opal.h>

#define P8_NEST_MAX_CHIPS		32
#define P8_NEST_MAX_PMUS		32
#define P8_NEST_MAX_PMU_NAME_LEN	256
#define P8_NEST_MAX_EVENTS_SUPPORTED	256
#define P8_NEST_ENGINE_START		1
#define P8_NEST_ENGINE_STOP		0

/*
 * Structure to hold per chip specific memory address
 * information for nest pmus. Nest Counter data are exported
 * in per-chip reserved memory region by the PORE Engine.
 */
struct perchip_nest_info {
	uint32_t chip_id;
	uint64_t pbase;
	uint64_t vbase;
	uint32_t size;
};

/*
 * Place holder for nest pmu events and values.
 */
struct nest_ima_events {
	const char *ev_name;
	const char *ev_value;
};

/*
 * Device tree parser code detects nest pmu support and
 * registers new nest pmus. This structure will
 * hold the pmu functions and attrs for each nest pmu and
 * will be referenced at the time of pmu registration.
 */
struct nest_pmu {
	struct pmu pmu;
	const struct attribute_group *attr_groups[4];
};
