/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     James Puzzo <jamesp at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

#ifndef __DGRP_COMMON_H
#define __DGRP_COMMON_H

#define DIGI_VERSION "1.9-29"

#include <linux/fs.h>
#include <linux/timer.h>
#include "drp.h"

#define DGRP_TTIME 100
#define DGRP_RTIME 100

/************************************************************************
 * All global storage allocation.
 ************************************************************************/

extern int dgrp_rawreadok;  /* Allow raw writing of input */
extern int dgrp_register_cudevices; /* enable legacy cu devices */
extern int dgrp_register_prdevices; /* enable transparent print devices */
extern int dgrp_poll_tick;          /* Poll interval - in ms */

extern struct list_head nd_struct_list;

struct dgrp_poll_data {
	spinlock_t poll_lock;
	struct timer_list timer;
	int poll_tick;
	ulong poll_round;	/* Timer rouding factor */
	long node_active_count;
};

extern struct dgrp_poll_data dgrp_poll_data;
extern void dgrp_poll_handler(unsigned long arg);

/* from dgrp_mon_ops.c */
extern void dgrp_register_mon_hook(struct proc_dir_entry *de);

/* from dgrp_tty.c */
extern int dgrp_tty_init(struct nd_struct *nd);
extern void dgrp_tty_uninit(struct nd_struct *nd);

/* from dgrp_ports_ops.c */
extern void dgrp_register_ports_hook(struct proc_dir_entry *de);

/* from dgrp_net_ops.c */
extern void dgrp_register_net_hook(struct proc_dir_entry *de);

/* from dgrp_dpa_ops.c */
extern void dgrp_register_dpa_hook(struct proc_dir_entry *de);
extern void dgrp_dpa_data(struct nd_struct *, int, u8 *, int);

/* from dgrp_sysfs.c */
extern void dgrp_create_class_sysfs_files(void);
extern void dgrp_remove_class_sysfs_files(void);

extern void dgrp_create_node_class_sysfs_files(struct nd_struct *nd);
extern void dgrp_remove_node_class_sysfs_files(struct nd_struct *nd);

extern void dgrp_create_tty_sysfs(struct un_struct *un, struct device *c);
extern void dgrp_remove_tty_sysfs(struct device *c);

/* from dgrp_specproc.c */
/*
 *  The list of DGRP entries with r/w capabilities.  These
 *  magic numbers are used for identification purposes.
 */
enum {
	DGRP_CONFIG = 1,	/* Configure portservers */
	DGRP_NETDIR = 2,	/* Directory for "net" devices */
	DGRP_MONDIR = 3,	/* Directory for "mon" devices */
	DGRP_PORTSDIR = 4,	/* Directory for "ports" devices */
	DGRP_INFO = 5,		/* Get info. about the running module */
	DGRP_NODEINFO = 6,	/* Get info. about the configured nodes */
	DGRP_DPADIR = 7,	/* Directory for the "dpa" devices */
};

/*
 *  Directions for proc handlers
 */
enum {
	INBOUND = 1,		/* Data being written to kernel */
	OUTBOUND = 2,		/* Data being read from the kernel */
};

/**
 * dgrp_proc_entry: structure for dgrp proc dirs
 * @id: ID number associated with this particular entry.  Should be
 *    unique across all of DGRP.
 * @name: text name associated with the /proc entry
 * @mode: file access permisssions for the /proc entry
 * @child: pointer to table describing a subdirectory for this entry
 * @de: pointer to directory entry for this object once registered.  Used
 *    to grab the handle of the object for unregistration
 * @excl_sem: semaphore to provide exclusive to struct
 * @excl_cnt: counter of current accesses
 *
 *  Each entry in a DGRP proc directory is described with a
 *  dgrp_proc_entry structure.  A collection of these
 *  entries (in an array) represents the members associated
 *  with a particular /proc directory, and is referred to
 *  as a table.  All tables are terminated by an entry with
 *  zeros for every member.
 */
struct dgrp_proc_entry {
	int                  id;          /* Integer identifier */
	const char        *name;          /* ASCII identifier */
	mode_t             mode;          /* File access permissions */
	struct dgrp_proc_entry *child;    /* Child pointer */

	/* file ops to use, pass NULL to use default */
	struct file_operations *proc_file_ops;

	struct proc_dir_entry *de;        /* proc entry pointer */
	struct semaphore   excl_sem;      /* Protects exclusive access var */
	int                excl_cnt;      /* Counts number of curr accesses */
};

extern void dgrp_unregister_proc(void);
extern void dgrp_register_proc(void);

/*-----------------------------------------------------------------------*
 *
 *  Declarations for common operations:
 *
 *      (either used by more than one of net, mon, or tty,
 *       or in interrupt context (i.e. the poller))
 *
 *-----------------------------------------------------------------------*/

void dgrp_carrier(struct ch_struct *ch);
extern int dgrp_inode_permission(struct inode *inode, int op);
extern int dgrp_chk_perm(int mode, int op);


/*
 *  ID manipulation macros (where c1 & c2 are characters, i is
 *  a long integer, and s is a character array of at least three members
 */

static inline void ID_TO_CHAR(long i, char *s)
{
	s[0] = ((i & 0xff00)>>8);
	s[1] = (i & 0xff);
	s[2] = 0;
}

static inline long CHAR_TO_ID(char *s)
{
	return ((s[0] & 0xff) << 8) | (s[1] & 0xff);
}

static inline struct nd_struct *nd_struct_get(long major)
{
	struct nd_struct *nd;

	list_for_each_entry(nd, &nd_struct_list, list) {
		if (major == nd->nd_major)
			return nd;
	}

	return NULL;
}

static inline int nd_struct_add(struct nd_struct *entry)
{
	struct nd_struct *ptr;

	ptr = nd_struct_get(entry->nd_major);

	if (ptr)
		return -EBUSY;

	list_add_tail(&entry->list, &nd_struct_list);

	return 0;
}

static inline int nd_struct_del(struct nd_struct *entry)
{
	struct nd_struct *nd;

	nd = nd_struct_get(entry->nd_major);

	if (!nd)
		return -ENODEV;

	list_del(&nd->list);
	return 0;
}

#endif /* __DGRP_COMMON_H */
