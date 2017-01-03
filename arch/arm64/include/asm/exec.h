/*
 * Based on arch/arm/include/asm/exec.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_EXEC_H
#define __ASM_EXEC_H

#include <linux/sched.h>

extern unsigned long arch_align_stack(unsigned long sp);
void uao_thread_switch(struct task_struct *next);

#endif	/* __ASM_EXEC_H */
