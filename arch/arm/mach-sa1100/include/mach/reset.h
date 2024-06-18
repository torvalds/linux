/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_ARCH_RESET_H
#define __ASM_ARCH_RESET_H

#include "hardware.h"

#define RESET_STATUS_HARDWARE	(1 << 0)	/* Hardware Reset */
#define RESET_STATUS_WATCHDOG	(1 << 1)	/* Watchdog Reset */
#define RESET_STATUS_LOWPOWER	(1 << 2)	/* Exit from Low Power/Sleep */
#define RESET_STATUS_GPIO	(1 << 3)	/* GPIO Reset */
#define RESET_STATUS_ALL	(0xf)

static inline void clear_reset_status(unsigned int mask)
{
	RCSR = mask;
}

#endif /* __ASM_ARCH_RESET_H */
