/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 */
#ifndef _ASM_VPE_H
#define _ASM_VPE_H

#include <linux/init.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/spinlock.h>

#define VPE_MODULE_NAME "vpe"
#define VPE_MODULE_MINOR 1

/* grab the likely amount of memory we will need. */
#ifdef CONFIG_MIPS_VPE_LOADER_TOM
#define P_SIZE (2 * 1024 * 1024)
#else
/* add an overhead to the max kmalloc size for non-striped symbols/etc */
#define P_SIZE (256 * 1024)
#endif

#define MAX_VPES 16

static inline int aprp_cpu_index(void)
{
	extern int tclimit;
	return tclimit;
}

enum vpe_state {
	VPE_STATE_UNUSED = 0,
	VPE_STATE_INUSE,
	VPE_STATE_RUNNING
};

enum tc_state {
	TC_STATE_UNUSED = 0,
	TC_STATE_INUSE,
	TC_STATE_RUNNING,
	TC_STATE_DYNAMIC
};

struct vpe {
	enum vpe_state state;

	/* (device) minor associated with this vpe */
	int minor;

	/* elfloader stuff */
	void *load_addr;
	unsigned long len;
	char *pbuffer;
	unsigned long plen;

	unsigned long __start;

	/* tc's associated with this vpe */
	struct list_head tc;

	/* The list of vpe's */
	struct list_head list;

	/* shared symbol address */
	void *shared_ptr;

	/* the list of who wants to know when something major happens */
	struct list_head notify;

	unsigned int ntcs;
};

struct tc {
	enum tc_state state;
	int index;

	struct vpe *pvpe;	/* parent VPE */
	struct list_head tc;	/* The list of TC's with this VPE */
	struct list_head list;	/* The global list of tc's */
};

struct vpe_notifications {
	void (*start)(int vpe);
	void (*stop)(int vpe);

	struct list_head list;
};

struct vpe_control {
	spinlock_t vpe_list_lock;
	struct list_head vpe_list;      /* Virtual processing elements */
	spinlock_t tc_list_lock;
	struct list_head tc_list;       /* Thread contexts */
};

extern struct vpe_control vpecontrol;
extern const struct file_operations vpe_fops;

int vpe_notify(int index, struct vpe_notifications *notify);

void *vpe_get_shared(int index);

struct vpe *get_vpe(int minor);
struct tc *get_tc(int index);
struct vpe *alloc_vpe(int minor);
struct tc *alloc_tc(int index);
void release_vpe(struct vpe *v);

void *alloc_progmem(unsigned long len);
void release_progmem(void *ptr);

int vpe_run(struct vpe *v);
void cleanup_tc(struct tc *tc);

int __init vpe_module_init(void);
void __exit vpe_module_exit(void);

#ifdef CONFIG_MIPS_VPE_LOADER_MT
void *vpe_alloc(void);
int vpe_start(void *vpe, unsigned long start);
int vpe_stop(void *vpe);
int vpe_free(void *vpe);
#endif /* CONFIG_MIPS_VPE_LOADER_MT */

#endif /* _ASM_VPE_H */
