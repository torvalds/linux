/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _MSHV_VTL_H
#define _MSHV_VTL_H

#include <linux/mshv.h>
#include <linux/types.h>

struct mshv_vtl_run {
	u32 cancel;
	u32 vtl_ret_action_size;
	u32 pad[2];
	char exit_message[MSHV_MAX_RUN_MSG_SIZE];
	union {
		struct mshv_vtl_cpu_context cpu_context;

		/*
		 * Reserving room for the cpu context to grow and to maintain compatibility
		 * with user mode.
		 */
		char reserved[1024];
	};
	char vtl_ret_actions[MSHV_MAX_RUN_MSG_SIZE];
};

#endif /* _MSHV_VTL_H */
