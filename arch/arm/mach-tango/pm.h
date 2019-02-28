/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CONFIG_SUSPEND
void __init tango_pm_init(void);
#else
#define tango_pm_init NULL
#endif
