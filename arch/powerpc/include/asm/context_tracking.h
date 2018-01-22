/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_CONTEXT_TRACKING_H
#define _ASM_POWERPC_CONTEXT_TRACKING_H

#ifdef CONFIG_CONTEXT_TRACKING
#define SCHEDULE_USER bl	schedule_user
#else
#define SCHEDULE_USER bl	schedule
#endif

#endif
