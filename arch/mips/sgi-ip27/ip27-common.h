/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IP27_COMMON_H
#define __IP27_COMMON_H

extern void ip27_reboot_setup(void);
extern void hub_rt_clock_event_init(void);
extern const struct plat_smp_ops ip27_smp_ops;

#endif /* __IP27_COMMON_H */
