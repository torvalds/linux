// SPDX-License-Identifier: GPL-2.0
/*
 * Generate definitions needed by assembly language modules.
 * This code generates raw asm output which is post-processed to extract
 * and format the required data.
 */

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/kbuild.h>
#include <asm/io.h>

void foo(void)
{
	DEFINE(TI_TASK, offsetof(struct thread_info, task));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_CPU, offsetof(struct thread_info, cpu));
	DEFINE(TI_FP, offsetof(struct thread_info, fp));
	DEFINE(TI_STATUS, offsetof(struct thread_info, status));
	BLANK();

        DEFINE(TASK_BLOCKED, offsetof(struct task_struct, blocked));
        DEFINE(TASK_CRED, offsetof(struct task_struct, cred));
        DEFINE(TASK_REAL_PARENT, offsetof(struct task_struct, real_parent));
        DEFINE(TASK_GROUP_LEADER, offsetof(struct task_struct, group_leader));
        DEFINE(TASK_TGID, offsetof(struct task_struct, tgid));
        BLANK();

        DEFINE(CRED_UID,  offsetof(struct cred, uid));
        DEFINE(CRED_EUID, offsetof(struct cred, euid));
        DEFINE(CRED_GID,  offsetof(struct cred, gid));
        DEFINE(CRED_EGID, offsetof(struct cred, egid));
        BLANK();

	DEFINE(SIZEOF_PT_REGS, sizeof(struct pt_regs));
	DEFINE(PT_PTRACED, PT_PTRACED);
	DEFINE(CLONE_VM, CLONE_VM);
	DEFINE(CLONE_UNTRACED, CLONE_UNTRACED);
	DEFINE(SIGCHLD, SIGCHLD);
	BLANK();

	DEFINE(HAE_CACHE, offsetof(struct alpha_machine_vector, hae_cache));
	DEFINE(HAE_REG, offsetof(struct alpha_machine_vector, hae_register));
}
