/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Generates definitions from c-type structures used by assembly sources.
 */

#include <linux/kbuild.h>
#include <linux/thread_info.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/ptrace.h>
#include <hv/hypervisor.h>

/* Check for compatible compiler early in the build. */
#ifdef CONFIG_TILEGX
# ifndef __tilegx__
#  error Can only build TILE-Gx configurations with tilegx compiler
# endif
# ifndef __LP64__
#  error Must not specify -m32 when building the TILE-Gx kernel
# endif
#else
# ifdef __tilegx__
#  error Can not build TILEPro/TILE64 configurations with tilegx compiler
# endif
#endif

void foo(void)
{
	DEFINE(SINGLESTEP_STATE_BUFFER_OFFSET, \
	       offsetof(struct single_step_state, buffer));
	DEFINE(SINGLESTEP_STATE_FLAGS_OFFSET, \
	       offsetof(struct single_step_state, flags));
	DEFINE(SINGLESTEP_STATE_ORIG_PC_OFFSET, \
	       offsetof(struct single_step_state, orig_pc));
	DEFINE(SINGLESTEP_STATE_NEXT_PC_OFFSET, \
	       offsetof(struct single_step_state, next_pc));
	DEFINE(SINGLESTEP_STATE_BRANCH_NEXT_PC_OFFSET, \
	       offsetof(struct single_step_state, branch_next_pc));
	DEFINE(SINGLESTEP_STATE_UPDATE_VALUE_OFFSET, \
	       offsetof(struct single_step_state, update_value));

	DEFINE(THREAD_INFO_TASK_OFFSET, \
	       offsetof(struct thread_info, task));
	DEFINE(THREAD_INFO_FLAGS_OFFSET, \
	       offsetof(struct thread_info, flags));
	DEFINE(THREAD_INFO_STATUS_OFFSET, \
	       offsetof(struct thread_info, status));
	DEFINE(THREAD_INFO_HOMECACHE_CPU_OFFSET, \
	       offsetof(struct thread_info, homecache_cpu));
	DEFINE(THREAD_INFO_STEP_STATE_OFFSET, \
	       offsetof(struct thread_info, step_state));

	DEFINE(TASK_STRUCT_THREAD_KSP_OFFSET,
	       offsetof(struct task_struct, thread.ksp));
	DEFINE(TASK_STRUCT_THREAD_PC_OFFSET,
	       offsetof(struct task_struct, thread.pc));

	DEFINE(HV_TOPOLOGY_WIDTH_OFFSET, \
	       offsetof(HV_Topology, width));
	DEFINE(HV_TOPOLOGY_HEIGHT_OFFSET, \
	       offsetof(HV_Topology, height));

	DEFINE(IRQ_CPUSTAT_SYSCALL_COUNT_OFFSET, \
	       offsetof(irq_cpustat_t, irq_syscall_count));
}
