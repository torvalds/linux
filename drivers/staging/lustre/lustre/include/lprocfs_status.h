/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lprocfs_status.h
 *
 * Top level header file for LProc SNMP
 *
 * Author: Hariharan Thantry thantry@users.sourceforge.net
 */
#ifndef _LPROCFS_SNMP_H
#define _LPROCFS_SNMP_H

#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <linux/libcfs/libcfs.h>
#include <uapi/linux/lustre/lustre_cfg.h>
#include <uapi/linux/lustre/lustre_idl.h>

struct lprocfs_vars {
	const char		*name;
	const struct file_operations	*fops;
	void			*data;
	/**
	 * sysfs file mode.
	 */
	umode_t			proc_mode;
};

struct lprocfs_static_vars {
	struct lprocfs_vars *obd_vars;
	const struct attribute_group *sysfs_vars;
};

/* if we find more consumers this could be generalized */
#define OBD_HIST_MAX 32
struct obd_histogram {
	spinlock_t	oh_lock;
	unsigned long	oh_buckets[OBD_HIST_MAX];
};

enum {
	BRW_R_PAGES = 0,
	BRW_W_PAGES,
	BRW_R_RPC_HIST,
	BRW_W_RPC_HIST,
	BRW_R_IO_TIME,
	BRW_W_IO_TIME,
	BRW_R_DISCONT_PAGES,
	BRW_W_DISCONT_PAGES,
	BRW_R_DISCONT_BLOCKS,
	BRW_W_DISCONT_BLOCKS,
	BRW_R_DISK_IOSIZE,
	BRW_W_DISK_IOSIZE,
	BRW_R_DIO_FRAGS,
	BRW_W_DIO_FRAGS,
	BRW_LAST,
};

struct brw_stats {
	struct obd_histogram hist[BRW_LAST];
};

enum {
	RENAME_SAMEDIR_SIZE = 0,
	RENAME_CROSSDIR_SRC_SIZE,
	RENAME_CROSSDIR_TGT_SIZE,
	RENAME_LAST,
};

struct rename_stats {
	struct obd_histogram hist[RENAME_LAST];
};

/* An lprocfs counter can be configured using the enum bit masks below.
 *
 * LPROCFS_CNTR_EXTERNALLOCK indicates that an external lock already
 * protects this counter from concurrent updates. If not specified,
 * lprocfs an internal per-counter lock variable. External locks are
 * not used to protect counter increments, but are used to protect
 * counter readout and resets.
 *
 * LPROCFS_CNTR_AVGMINMAX indicates a multi-valued counter samples,
 * (i.e. counter can be incremented by more than "1"). When specified,
 * the counter maintains min, max and sum in addition to a simple
 * invocation count. This allows averages to be be computed.
 * If not specified, the counter is an increment-by-1 counter.
 * min, max, sum, etc. are not maintained.
 *
 * LPROCFS_CNTR_STDDEV indicates that the counter should track sum of
 * squares (for multi-valued counter samples only). This allows
 * external computation of standard deviation, but involves a 64-bit
 * multiply per counter increment.
 */

enum {
	LPROCFS_CNTR_EXTERNALLOCK = 0x0001,
	LPROCFS_CNTR_AVGMINMAX    = 0x0002,
	LPROCFS_CNTR_STDDEV       = 0x0004,

	/* counter data type */
	LPROCFS_TYPE_REGS	 = 0x0100,
	LPROCFS_TYPE_BYTES	= 0x0200,
	LPROCFS_TYPE_PAGES	= 0x0400,
	LPROCFS_TYPE_CYCLE	= 0x0800,
};

#define LC_MIN_INIT ((~(__u64)0) >> 1)

struct lprocfs_counter_header {
	unsigned int		lc_config;
	const char		*lc_name;   /* must be static */
	const char		*lc_units;  /* must be static */
};

struct lprocfs_counter {
	__s64	lc_count;
	__s64	lc_min;
	__s64	lc_max;
	__s64	lc_sumsquare;
	/*
	 * Every counter has lc_array_sum[0], while lc_array_sum[1] is only
	 * for irq context counter, i.e. stats with
	 * LPROCFS_STATS_FLAG_IRQ_SAFE flag, its counter need
	 * lc_array_sum[1]
	 */
	__s64	lc_array_sum[1];
};

#define lc_sum		lc_array_sum[0]
#define lc_sum_irq	lc_array_sum[1]

struct lprocfs_percpu {
#ifndef __GNUC__
	__s64			pad;
#endif
	struct lprocfs_counter lp_cntr[0];
};

enum lprocfs_stats_lock_ops {
	LPROCFS_GET_NUM_CPU	= 0x0001, /* number allocated per-CPU stats */
	LPROCFS_GET_SMP_ID	= 0x0002, /* current stat to be updated */
};

enum lprocfs_stats_flags {
	LPROCFS_STATS_FLAG_NONE     = 0x0000, /* per cpu counter */
	LPROCFS_STATS_FLAG_NOPERCPU = 0x0001, /* stats have no percpu
					       * area and need locking
					       */
	LPROCFS_STATS_FLAG_IRQ_SAFE = 0x0002, /* alloc need irq safe */
};

enum lprocfs_fields_flags {
	LPROCFS_FIELDS_FLAGS_CONFIG     = 0x0001,
	LPROCFS_FIELDS_FLAGS_SUM	= 0x0002,
	LPROCFS_FIELDS_FLAGS_MIN	= 0x0003,
	LPROCFS_FIELDS_FLAGS_MAX	= 0x0004,
	LPROCFS_FIELDS_FLAGS_AVG	= 0x0005,
	LPROCFS_FIELDS_FLAGS_SUMSQUARE  = 0x0006,
	LPROCFS_FIELDS_FLAGS_COUNT      = 0x0007,
};

struct lprocfs_stats {
	/* # of counters */
	unsigned short			ls_num;
	/* 1 + the biggest cpu # whose ls_percpu slot has been allocated */
	unsigned short			ls_biggest_alloc_num;
	enum lprocfs_stats_flags	ls_flags;
	/* Lock used when there are no percpu stats areas; For percpu stats,
	 * it is used to protect ls_biggest_alloc_num change
	 */
	spinlock_t			ls_lock;

	/* has ls_num of counter headers */
	struct lprocfs_counter_header	*ls_cnt_header;
	struct lprocfs_percpu		*ls_percpu[0];
};

#define OPC_RANGE(seg) (seg ## _LAST_OPC - seg ## _FIRST_OPC)

/* Pack all opcodes down into a single monotonically increasing index */
static inline int opcode_offset(__u32 opc)
{
	if (opc < OST_LAST_OPC) {
		 /* OST opcode */
		return (opc - OST_FIRST_OPC);
	} else if (opc < MDS_LAST_OPC) {
		/* MDS opcode */
		return (opc - MDS_FIRST_OPC +
			OPC_RANGE(OST));
	} else if (opc < LDLM_LAST_OPC) {
		/* LDLM Opcode */
		return (opc - LDLM_FIRST_OPC +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < MGS_LAST_OPC) {
		/* MGS Opcode */
		return (opc - MGS_FIRST_OPC +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < OBD_LAST_OPC) {
		/* OBD Ping */
		return (opc - OBD_FIRST_OPC +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < LLOG_LAST_OPC) {
		/* LLOG Opcode */
		return (opc - LLOG_FIRST_OPC +
			OPC_RANGE(OBD) +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < QUOTA_LAST_OPC) {
		/* LQUOTA Opcode */
		return (opc - QUOTA_FIRST_OPC +
			OPC_RANGE(LLOG) +
			OPC_RANGE(OBD) +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < SEQ_LAST_OPC) {
		/* SEQ opcode */
		return (opc - SEQ_FIRST_OPC +
			OPC_RANGE(QUOTA) +
			OPC_RANGE(LLOG) +
			OPC_RANGE(OBD) +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < SEC_LAST_OPC) {
		/* SEC opcode */
		return (opc - SEC_FIRST_OPC +
			OPC_RANGE(SEQ) +
			OPC_RANGE(QUOTA) +
			OPC_RANGE(LLOG) +
			OPC_RANGE(OBD) +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else if (opc < FLD_LAST_OPC) {
		/* FLD opcode */
		return (opc - FLD_FIRST_OPC +
			OPC_RANGE(SEC) +
			OPC_RANGE(SEQ) +
			OPC_RANGE(QUOTA) +
			OPC_RANGE(LLOG) +
			OPC_RANGE(OBD) +
			OPC_RANGE(MGS) +
			OPC_RANGE(LDLM) +
			OPC_RANGE(MDS) +
			OPC_RANGE(OST));
	} else {
		/* Unknown Opcode */
		return -1;
	}
}

#define LUSTRE_MAX_OPCODES (OPC_RANGE(OST)  + \
			    OPC_RANGE(MDS)  + \
			    OPC_RANGE(LDLM) + \
			    OPC_RANGE(MGS)  + \
			    OPC_RANGE(OBD)  + \
			    OPC_RANGE(LLOG) + \
			    OPC_RANGE(SEC)  + \
			    OPC_RANGE(SEQ)  + \
			    OPC_RANGE(SEC)  + \
			    OPC_RANGE(FLD))

#define EXTRA_MAX_OPCODES ((PTLRPC_LAST_CNTR - PTLRPC_FIRST_CNTR)  + \
			    OPC_RANGE(EXTRA))

enum {
	PTLRPC_REQWAIT_CNTR = 0,
	PTLRPC_REQQDEPTH_CNTR,
	PTLRPC_REQACTIVE_CNTR,
	PTLRPC_TIMEOUT,
	PTLRPC_REQBUF_AVAIL_CNTR,
	PTLRPC_LAST_CNTR
};

#define PTLRPC_FIRST_CNTR PTLRPC_REQWAIT_CNTR

enum {
	LDLM_GLIMPSE_ENQUEUE = 0,
	LDLM_PLAIN_ENQUEUE,
	LDLM_EXTENT_ENQUEUE,
	LDLM_FLOCK_ENQUEUE,
	LDLM_IBITS_ENQUEUE,
	MDS_REINT_SETATTR,
	MDS_REINT_CREATE,
	MDS_REINT_LINK,
	MDS_REINT_UNLINK,
	MDS_REINT_RENAME,
	MDS_REINT_OPEN,
	MDS_REINT_SETXATTR,
	BRW_READ_BYTES,
	BRW_WRITE_BYTES,
	EXTRA_LAST_OPC
};

#define EXTRA_FIRST_OPC LDLM_GLIMPSE_ENQUEUE
/* class_obd.c */
extern struct dentry *debugfs_lustre_root;
extern struct kobject *lustre_kobj;

struct obd_device;
struct obd_histogram;

/* Days / hours / mins / seconds format */
struct dhms {
	int d, h, m, s;
};

static inline void s2dhms(struct dhms *ts, time64_t secs64)
{
	unsigned int secs;

	ts->d = div_u64_rem(secs64, 86400, &secs);
	ts->h = secs / 3600;
	secs = secs % 3600;
	ts->m = secs / 60;
	ts->s = secs % 60;
}

#define DHMS_FMT "%dd%dh%02dm%02ds"
#define DHMS_VARS(x) (x)->d, (x)->h, (x)->m, (x)->s

#define JOBSTATS_JOBID_VAR_MAX_LEN	20
#define JOBSTATS_DISABLE		"disable"
#define JOBSTATS_PROCNAME_UID		"procname_uid"
#define JOBSTATS_NODELOCAL		"nodelocal"

/* obd_config.c */
void lustre_register_client_process_config(int (*cpc)(struct lustre_cfg *lcfg));

int lprocfs_write_frac_helper(const char __user *buffer,
			      unsigned long count, int *val, int mult);
int lprocfs_read_frac_helper(char *buffer, unsigned long count,
			     long val, int mult);

int lprocfs_stats_alloc_one(struct lprocfs_stats *stats,
			    unsigned int cpuid);
int lprocfs_stats_lock(struct lprocfs_stats *stats,
		       enum lprocfs_stats_lock_ops opc,
		       unsigned long *flags);
void lprocfs_stats_unlock(struct lprocfs_stats *stats,
			  enum lprocfs_stats_lock_ops opc,
			  unsigned long *flags);

static inline unsigned int
lprocfs_stats_counter_size(struct lprocfs_stats *stats)
{
	unsigned int percpusize;

	percpusize = offsetof(struct lprocfs_percpu, lp_cntr[stats->ls_num]);

	/* irq safe stats need lc_array_sum[1] */
	if ((stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0)
		percpusize += stats->ls_num * sizeof(__s64);

	if ((stats->ls_flags & LPROCFS_STATS_FLAG_NOPERCPU) == 0)
		percpusize = L1_CACHE_ALIGN(percpusize);

	return percpusize;
}

static inline struct lprocfs_counter *
lprocfs_stats_counter_get(struct lprocfs_stats *stats, unsigned int cpuid,
			  int index)
{
	struct lprocfs_counter *cntr;

	cntr = &stats->ls_percpu[cpuid]->lp_cntr[index];

	if ((stats->ls_flags & LPROCFS_STATS_FLAG_IRQ_SAFE) != 0)
		cntr = (void *)cntr + index * sizeof(__s64);

	return cntr;
}

/* Two optimized LPROCFS counter increment functions are provided:
 *     lprocfs_counter_incr(cntr, value) - optimized for by-one counters
 *     lprocfs_counter_add(cntr) - use for multi-valued counters
 * Counter data layout allows config flag, counter lock and the
 * count itself to reside within a single cache line.
 */

void lprocfs_counter_add(struct lprocfs_stats *stats, int idx, long amount);
void lprocfs_counter_sub(struct lprocfs_stats *stats, int idx, long amount);

#define lprocfs_counter_incr(stats, idx) \
	lprocfs_counter_add(stats, idx, 1)
#define lprocfs_counter_decr(stats, idx) \
	lprocfs_counter_sub(stats, idx, 1)

__s64 lprocfs_read_helper(struct lprocfs_counter *lc,
			  struct lprocfs_counter_header *header,
			  enum lprocfs_stats_flags flags,
			  enum lprocfs_fields_flags field);
__u64 lprocfs_stats_collector(struct lprocfs_stats *stats, int idx,
			      enum lprocfs_fields_flags field);

extern struct lprocfs_stats *
lprocfs_alloc_stats(unsigned int num, enum lprocfs_stats_flags flags);
void lprocfs_clear_stats(struct lprocfs_stats *stats);
void lprocfs_free_stats(struct lprocfs_stats **stats);
void lprocfs_counter_init(struct lprocfs_stats *stats, int index,
			  unsigned int conf, const char *name,
			  const char *units);
struct obd_export;
int lprocfs_exp_cleanup(struct obd_export *exp);
struct dentry *ldebugfs_add_simple(struct dentry *root,
				   char *name,
				   void *data,
				   const struct file_operations *fops);

int ldebugfs_register_stats(struct dentry *parent,
			    const char *name,
			    struct lprocfs_stats *stats);

/* lprocfs_status.c */
int ldebugfs_add_vars(struct dentry *parent,
		      struct lprocfs_vars *var,
		      void *data);

struct dentry *ldebugfs_register(const char *name,
				 struct dentry *parent,
				 struct lprocfs_vars *list,
				 void *data);

void ldebugfs_remove(struct dentry **entryp);

int lprocfs_obd_setup(struct obd_device *obd, struct lprocfs_vars *list,
		      const struct attribute_group *attrs);
int lprocfs_obd_cleanup(struct obd_device *obd);

int ldebugfs_seq_create(struct dentry *parent,
			const char *name,
			umode_t mode,
			const struct file_operations *seq_fops,
			void *data);
int ldebugfs_obd_seq_create(struct obd_device *dev,
			    const char *name,
			    umode_t mode,
			    const struct file_operations *seq_fops,
			    void *data);

/* Generic callbacks */

int lprocfs_rd_uint(struct seq_file *m, void *data);
int lprocfs_wr_uint(struct file *file, const char __user *buffer,
		    unsigned long count, void *data);
int lprocfs_rd_server_uuid(struct seq_file *m, void *data);
int lprocfs_rd_conn_uuid(struct seq_file *m, void *data);
int lprocfs_rd_import(struct seq_file *m, void *data);
int lprocfs_rd_state(struct seq_file *m, void *data);
int lprocfs_rd_connect_flags(struct seq_file *m, void *data);

struct adaptive_timeout;
int lprocfs_at_hist_helper(struct seq_file *m, struct adaptive_timeout *at);
int lprocfs_rd_timeouts(struct seq_file *m, void *data);
int lprocfs_wr_ping(struct file *file, const char __user *buffer,
		    size_t count, loff_t *off);
int lprocfs_wr_import(struct file *file, const char __user *buffer,
		      size_t count, loff_t *off);
int lprocfs_rd_pinger_recov(struct seq_file *m, void *n);
int lprocfs_wr_pinger_recov(struct file *file, const char __user *buffer,
			    size_t count, loff_t *off);

/* Statfs helpers */

int lprocfs_write_helper(const char __user *buffer, unsigned long count,
			 int *val);
int lprocfs_write_u64_helper(const char __user *buffer,
			     unsigned long count, __u64 *val);
int lprocfs_write_frac_u64_helper(const char __user *buffer,
				  unsigned long count,
				  __u64 *val, int mult);
char *lprocfs_find_named_value(const char *buffer, const char *name,
			       size_t *count);
void lprocfs_oh_tally(struct obd_histogram *oh, unsigned int value);
void lprocfs_oh_tally_log2(struct obd_histogram *oh, unsigned int value);
void lprocfs_oh_clear(struct obd_histogram *oh);
unsigned long lprocfs_oh_sum(struct obd_histogram *oh);

void lprocfs_stats_collect(struct lprocfs_stats *stats, int idx,
			   struct lprocfs_counter *cnt);

int lprocfs_single_release(struct inode *inode, struct file *file);
int lprocfs_seq_release(struct inode *inode, struct file *file);

/* write the name##_seq_show function, call LPROC_SEQ_FOPS_RO for read-only
 * proc entries; otherwise, you will define name##_seq_write function also for
 * a read-write proc entry, and then call LPROC_SEQ_SEQ instead. Finally,
 * call ldebugfs_obd_seq_create(obd, filename, 0444, &name#_fops, data);
 */
#define __LPROC_SEQ_FOPS(name, custom_seq_write)			\
static int name##_single_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, name##_seq_show, inode->i_private);	\
}									\
static const struct file_operations name##_fops = {			\
	.owner   = THIS_MODULE,					    \
	.open    = name##_single_open,				     \
	.read    = seq_read,					       \
	.write   = custom_seq_write,				       \
	.llseek  = seq_lseek,					      \
	.release = lprocfs_single_release,				 \
}

#define LPROC_SEQ_FOPS_RO(name)	 __LPROC_SEQ_FOPS(name, NULL)
#define LPROC_SEQ_FOPS(name)	    __LPROC_SEQ_FOPS(name, name##_seq_write)

#define LPROC_SEQ_FOPS_RO_TYPE(name, type)				\
	static int name##_##type##_seq_show(struct seq_file *m, void *v)\
	{								\
		return lprocfs_rd_##type(m, m->private);		\
	}								\
	LPROC_SEQ_FOPS_RO(name##_##type)

#define LPROC_SEQ_FOPS_RW_TYPE(name, type)				\
	static int name##_##type##_seq_show(struct seq_file *m, void *v)\
	{								\
		return lprocfs_rd_##type(m, m->private);		\
	}								\
	static ssize_t name##_##type##_seq_write(struct file *file,	\
			const char __user *buffer, size_t count,	\
						loff_t *off)		\
	{								\
		struct seq_file *seq = file->private_data;		\
		return lprocfs_wr_##type(file, buffer,			\
					 count, seq->private);		\
	}								\
	LPROC_SEQ_FOPS(name##_##type)

#define LPROC_SEQ_FOPS_WR_ONLY(name, type)				\
	static ssize_t name##_##type##_write(struct file *file,		\
			const char __user *buffer, size_t count,	\
						loff_t *off)		\
	{								\
		return lprocfs_wr_##type(file, buffer, count, off);	\
	}								\
	static int name##_##type##_open(struct inode *inode, struct file *file) \
	{								\
		return single_open(file, NULL, inode->i_private);	\
	}								\
	static const struct file_operations name##_##type##_fops = {	\
		.open	= name##_##type##_open,				\
		.write	= name##_##type##_write,			\
		.release = lprocfs_single_release,			\
	}

struct lustre_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, struct attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct attribute *attr,
			 const char *buf, size_t len);
};

#define LUSTRE_ATTR(name, mode, show, store) \
static struct lustre_attr lustre_attr_##name = __ATTR(name, mode, show, store)

#define LUSTRE_RO_ATTR(name) LUSTRE_ATTR(name, 0444, name##_show, NULL)
#define LUSTRE_RW_ATTR(name) LUSTRE_ATTR(name, 0644, name##_show, name##_store)

extern const struct sysfs_ops lustre_sysfs_ops;

struct root_squash_info;
int lprocfs_wr_root_squash(const char __user *buffer, unsigned long count,
			   struct root_squash_info *squash, char *name);
int lprocfs_wr_nosquash_nids(const char __user *buffer, unsigned long count,
			     struct root_squash_info *squash, char *name);

/* all quota proc functions */
int lprocfs_quota_rd_bunit(char *page, char **start,
			   loff_t off, int count,
			   int *eof, void *data);
int lprocfs_quota_wr_bunit(struct file *file, const char *buffer,
			   unsigned long count, void *data);
int lprocfs_quota_rd_btune(char *page, char **start,
			   loff_t off, int count,
			   int *eof, void *data);
int lprocfs_quota_wr_btune(struct file *file, const char *buffer,
			   unsigned long count, void *data);
int lprocfs_quota_rd_iunit(char *page, char **start,
			   loff_t off, int count,
			   int *eof, void *data);
int lprocfs_quota_wr_iunit(struct file *file, const char *buffer,
			   unsigned long count, void *data);
int lprocfs_quota_rd_itune(char *page, char **start,
			   loff_t off, int count,
			   int *eof, void *data);
int lprocfs_quota_wr_itune(struct file *file, const char *buffer,
			   unsigned long count, void *data);
int lprocfs_quota_rd_type(char *page, char **start, loff_t off, int count,
			  int *eof, void *data);
int lprocfs_quota_wr_type(struct file *file, const char *buffer,
			  unsigned long count, void *data);
int lprocfs_quota_rd_switch_seconds(char *page, char **start, loff_t off,
				    int count, int *eof, void *data);
int lprocfs_quota_wr_switch_seconds(struct file *file,
				    const char *buffer,
				    unsigned long count, void *data);
int lprocfs_quota_rd_sync_blk(char *page, char **start, loff_t off,
			      int count, int *eof, void *data);
int lprocfs_quota_wr_sync_blk(struct file *file, const char *buffer,
			      unsigned long count, void *data);
int lprocfs_quota_rd_switch_qs(char *page, char **start, loff_t off,
			       int count, int *eof, void *data);
int lprocfs_quota_wr_switch_qs(struct file *file,
			       const char *buffer, unsigned long count,
			       void *data);
int lprocfs_quota_rd_boundary_factor(char *page, char **start, loff_t off,
				     int count, int *eof, void *data);
int lprocfs_quota_wr_boundary_factor(struct file *file,
				     const char *buffer, unsigned long count,
				     void *data);
int lprocfs_quota_rd_least_bunit(char *page, char **start, loff_t off,
				 int count, int *eof, void *data);
int lprocfs_quota_wr_least_bunit(struct file *file,
				 const char *buffer, unsigned long count,
				 void *data);
int lprocfs_quota_rd_least_iunit(char *page, char **start, loff_t off,
				 int count, int *eof, void *data);
int lprocfs_quota_wr_least_iunit(struct file *file,
				 const char *buffer, unsigned long count,
				 void *data);
int lprocfs_quota_rd_qs_factor(char *page, char **start, loff_t off,
			       int count, int *eof, void *data);
int lprocfs_quota_wr_qs_factor(struct file *file,
			       const char *buffer, unsigned long count,
			       void *data);
#endif /* LPROCFS_SNMP_H */
