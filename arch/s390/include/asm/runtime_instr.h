/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RUNTIME_INSTR_H
#define _RUNTIME_INSTR_H

#include <uapi/asm/runtime_instr.h>

extern struct runtime_instr_cb runtime_instr_empty_cb;

static inline void save_ri_cb(struct runtime_instr_cb *cb_prev)
{
	if (cb_prev)
		store_runtime_instr_cb(cb_prev);
}

static inline void restore_ri_cb(struct runtime_instr_cb *cb_next,
				 struct runtime_instr_cb *cb_prev)
{
	if (cb_next)
		load_runtime_instr_cb(cb_next);
	else if (cb_prev)
		load_runtime_instr_cb(&runtime_instr_empty_cb);
}

struct task_struct;

void runtime_instr_release(struct task_struct *tsk);

#endif /* _RUNTIME_INSTR_H */
