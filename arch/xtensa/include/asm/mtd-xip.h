/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_MTD_XIP_H
#define _ASM_MTD_XIP_H

#include <asm/processor.h>

#define xip_irqpending()	(xtensa_get_sr(interrupt) & xtensa_get_sr(intenable))
#define xip_currtime()		(xtensa_get_sr(ccount))
#define xip_elapsed_since(x)	((xtensa_get_sr(ccount) - (x)) / 1000) /* should work up to 1GHz */
#define xip_cpu_idle()		do { asm volatile ("waiti 0"); } while (0)

#endif /* _ASM_MTD_XIP_H */

