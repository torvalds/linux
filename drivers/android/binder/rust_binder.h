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

#endif
