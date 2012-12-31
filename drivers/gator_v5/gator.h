/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef GATOR_H_
#define GATOR_H_

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/list.h>

#define GATOR_PERF_SUPPORT		LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
#define GATOR_PERF_PMU_SUPPORT  GATOR_PERF_SUPPORT && defined(CONFIG_PERF_EVENTS) && defined(CONFIG_HW_PERF_EVENTS)
#define GATOR_NO_PERF_SUPPORT   (!(GATOR_PERF_SUPPORT))
#define GATOR_CPU_FREQ_SUPPORT  (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)) && defined(CONFIG_CPU_FREQ)

// cpu ids
#define ARM1136		0xb36
#define ARM1156		0xb56
#define ARM1176		0xb76
#define ARM11MPCORE 0xb02
#define CORTEX_A5	0xc05
#define CORTEX_A7	0xc07
#define CORTEX_A8	0xc08
#define CORTEX_A9	0xc09
#define CORTEX_A15	0xc0f
#define SCORPION	0x00f
#define SCORPIONMP	0x02d
#define KRAITSIM	0x049
#define KRAIT       0x04d

/******************************************************************************
 * Filesystem
 ******************************************************************************/
int gatorfs_create_file_perm(struct super_block *sb, struct dentry *root,
	char const *name, const struct file_operations *fops, int perm);

struct dentry *gatorfs_mkdir(struct super_block *sb,
	struct dentry *root, char const *name);

int gatorfs_create_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val);

int gatorfs_create_ro_ulong(struct super_block *sb, struct dentry *root,
	char const *name, unsigned long *val);

void gator_op_create_files(struct super_block *sb, struct dentry *root);

/******************************************************************************
 * Tracepoints
 ******************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
#	error Kernels prior to 2.6.32 not supported
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
#	define GATOR_DEFINE_PROBE(probe_name, proto) \
		static void probe_##probe_name(PARAMS(proto))
#	define GATOR_REGISTER_TRACE(probe_name) \
		register_trace_##probe_name(probe_##probe_name)
#	define GATOR_UNREGISTER_TRACE(probe_name) \
		unregister_trace_##probe_name(probe_##probe_name)
#else
#	define GATOR_DEFINE_PROBE(probe_name, proto) \
		static void probe_##probe_name(void *data, PARAMS(proto))
#	define GATOR_REGISTER_TRACE(probe_name) \
		register_trace_##probe_name(probe_##probe_name, NULL)
#	define GATOR_UNREGISTER_TRACE(probe_name) \
		unregister_trace_##probe_name(probe_##probe_name, NULL)
#endif

/******************************************************************************
 * Events
 ******************************************************************************/
struct gator_interface {
	int  (*create_files)(struct super_block *sb, struct dentry *root);
	int  (*start)(void);
	void (*stop)(void);
	int  (*online)(int** buffer);
	int  (*offline)(int** buffer);
	void  (*online_dispatch)(int cpu);  // called in process context but may not be running on core 'cpu'
	void  (*offline_dispatch)(int cpu); // called in process context but may not be running on core 'cpu'
	int  (*read)(int **buffer);
	int  (*read64)(long long **buffer);
	struct list_head list;
};

// gator_events_init is used as a search term in gator_events.sh
#define gator_events_init(initfn) \
	static inline int __gator_events_init_test(void) \
	{ return initfn(); }

int gator_events_install(struct gator_interface *interface);
int gator_events_get_key(void);
extern u32 gator_cpuid(void);

#endif // GATOR_H_
