/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Google, Inc.
 */

#ifndef _LINUX_RUST_BINDER_H
#define _LINUX_RUST_BINDER_H

#include <uapi/linux/android/binder.h>
#include <uapi/linux/android/binderfs.h>

/*
 * These symbols are exposed by `rust_binderfs.c` and exist here so that Rust
 * Binder can call them.
 */
int init_rust_binderfs(void);

struct dentry;
struct inode;
struct dentry *rust_binderfs_create_proc_file(struct inode *nodp, int pid);
void rust_binderfs_remove_file(struct dentry *dentry);

/*
 * The internal data types in the Rust Binder driver are opaque to C, so we use
 * void pointer typedefs for these types.
 */

typedef void *rust_binder_transaction;
typedef void *rust_binder_process;
typedef void *rust_binder_node;

struct rb_process_layout {
	size_t arc_offset;
	size_t task;
};

struct rb_transaction_layout {
	size_t debug_id;
	size_t code;
	size_t flags;
	size_t from_thread;
	size_t to_proc;
	size_t target_node;
};

struct rb_node_layout {
	size_t arc_offset;
	size_t debug_id;
	size_t ptr;
};

struct rust_binder_layout {
	struct rb_transaction_layout t;
	struct rb_process_layout p;
	struct rb_node_layout n;
};

extern const struct rust_binder_layout RUST_BINDER_LAYOUT;

static inline size_t rust_binder_transaction_debug_id(rust_binder_transaction t)
{
	return *(size_t *) (t + RUST_BINDER_LAYOUT.t.debug_id);
}

static inline u32 rust_binder_transaction_code(rust_binder_transaction t)
{
	return *(u32 *) (t + RUST_BINDER_LAYOUT.t.code);
}

static inline u32 rust_binder_transaction_flags(rust_binder_transaction t)
{
	return *(u32 *) (t + RUST_BINDER_LAYOUT.t.flags);
}

// Nullable!
static inline rust_binder_node rust_binder_transaction_target_node(rust_binder_transaction t)
{
	void *p = *(void **) (t + RUST_BINDER_LAYOUT.t.target_node);

	if (p)
		p = p + RUST_BINDER_LAYOUT.n.arc_offset;
	return p;
}

static inline rust_binder_process rust_binder_transaction_to_proc(rust_binder_transaction t)
{
	void *p = *(void **) (t + RUST_BINDER_LAYOUT.t.to_proc);

	return p + RUST_BINDER_LAYOUT.p.arc_offset;
}

static inline struct task_struct *rust_binder_process_task(rust_binder_process t)
{
	return *(struct task_struct **) (t + RUST_BINDER_LAYOUT.p.task);
}

static inline size_t rust_binder_node_debug_id(rust_binder_node t)
{
	return *(size_t *) (t + RUST_BINDER_LAYOUT.n.debug_id);
}

#endif
