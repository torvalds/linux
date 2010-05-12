/*
 * Performance event support - hardware-specific disambiguation
 *
 * For now this is a compile-time decision, but eventually it should be
 * runtime.  This would allow multiplatform perf event support for e300 (fsl
 * embedded perf counters) plus server/classic, and would accommodate
 * devices other than the core which provide their own performance counters.
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef CONFIG_PPC_PERF_CTRS
#include <asm/perf_event_server.h>
#endif

#ifdef CONFIG_FSL_EMB_PERF_EVENT
#include <asm/perf_event_fsl_emb.h>
#endif
