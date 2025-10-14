/* SPDX-License-Identifier: GPL-2.0 */
/* rust_binder_internal.h
 *
 * This file contains internal data structures used by Rust Binder. Mostly,
 * these are type definitions used only by binderfs or things that Rust Binder
 * define and export to binderfs.
 *
 * It does not include things exported by binderfs to Rust Binder since this
 * file is not included as input to bindgen.
 *
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _LINUX_RUST_BINDER_INTERNAL_H
#define _LINUX_RUST_BINDER_INTERNAL_H

#define RUST_BINDERFS_SUPER_MAGIC	0x6c6f6f71

#include <linux/seq_file.h>
#include <uapi/linux/android/binder.h>
#include <uapi/linux/android/binderfs.h>

/*
 * The internal data types in the Rust Binder driver are opaque to C, so we use
 * void pointer typedefs for these types.
 */
typedef void *rust_binder_context;

/**
 * struct binder_device - information about a binder device node
 * @minor:     the minor number used by this device
 * @ctx:       the Rust Context used by this device, or null for binder-control
 *
 * This is used as the private data for files directly in binderfs, but not
 * files in the binder_logs subdirectory. This struct owns a refcount on `ctx`
 * and the entry for `minor` in `binderfs_minors`. For binder-control `ctx` is
 * null.
 */
struct binder_device {
	int minor;
	rust_binder_context ctx;
};

int rust_binder_stats_show(struct seq_file *m, void *unused);
int rust_binder_state_show(struct seq_file *m, void *unused);
int rust_binder_transactions_show(struct seq_file *m, void *unused);
int rust_binder_proc_show(struct seq_file *m, void *pid);

extern const struct file_operations rust_binder_fops;
rust_binder_context rust_binder_new_context(char *name);
void rust_binder_remove_context(rust_binder_context device);

/**
 * binderfs_mount_opts - mount options for binderfs
 * @max: maximum number of allocatable binderfs binder devices
 * @stats_mode: enable binder stats in binderfs.
 */
struct binderfs_mount_opts {
	int max;
	int stats_mode;
};

/**
 * binderfs_info - information about a binderfs mount
 * @ipc_ns:         The ipc namespace the binderfs mount belongs to.
 * @control_dentry: This records the dentry of this binderfs mount
 *                  binder-control device.
 * @root_uid:       uid that needs to be used when a new binder device is
 *                  created.
 * @root_gid:       gid that needs to be used when a new binder device is
 *                  created.
 * @mount_opts:     The mount options in use.
 * @device_count:   The current number of allocated binder devices.
 * @proc_log_dir:   Pointer to the directory dentry containing process-specific
 *                  logs.
 */
struct binderfs_info {
	struct ipc_namespace *ipc_ns;
	struct dentry *control_dentry;
	kuid_t root_uid;
	kgid_t root_gid;
	struct binderfs_mount_opts mount_opts;
	int device_count;
	struct dentry *proc_log_dir;
};

#endif /* _LINUX_RUST_BINDER_INTERNAL_H */
