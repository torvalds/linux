/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/reboot.h>
#define ARRAY_AND_SIZE(x)	(x), ARRAY_SIZE(x)

extern void timer_init(int irq);

extern void __init mmp_map_io(void);
extern void mmp_restart(enum reboot_mode, const char *);
