/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SUSPEND_H
#define __ASM_SUSPEND_H

void loongarch_common_suspend(void);
void loongarch_common_resume(void);
void loongarch_suspend_enter(void);
void loongarch_wakeup_start(void);

#endif
