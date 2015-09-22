/*
 * Common Gemini architecture functions
 *
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __GEMINI_COMMON_H__
#define __GEMINI_COMMON_H__

#include <linux/reboot.h>

struct mtd_partition;

extern void gemini_map_io(void);
extern void gemini_init_irq(void);
extern void gemini_timer_init(void);
extern void gemini_gpio_init(void);
extern void platform_register_rtc(void);

/* Common platform devices registration functions */
extern int platform_register_uart(void);
extern int platform_register_pflash(unsigned int size,
				    struct mtd_partition *parts,
				    unsigned int nr_parts);

extern void gemini_restart(enum reboot_mode mode, const char *cmd);

#endif /* __GEMINI_COMMON_H__ */
