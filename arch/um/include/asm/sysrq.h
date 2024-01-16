/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_SYSRQ_H
#define __UM_SYSRQ_H

struct task_struct;
extern void show_trace(struct task_struct* task, unsigned long *stack);

#endif
