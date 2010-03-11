/*
   md_k.h : kernel internal structure of the Linux MD driver
          Copyright (C) 1996-98 Ingo Molnar, Gadi Oxman
	  
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
*/

#ifndef _MD_MD_H
#define _MD_MD_H

#include <linux/blkdev.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define MaxSector (~(sector_t)0)

typedef struct mddev_s mddev_t;
typedef struct mdk_rdev_s mdk_rdev_t;

/*
 * MD's 'extended' device
 */
struct mdk_rdev_s
{
	struct list_head same_set;	/* RAID devices within the same set */

	sector_t sectors;		/* Device size (in 512bytes sectors) */
	mddev_t *mddev;			/* RAID array if running */
	int last_events;		/* IO event timestamp */

	struct block_device *bdev;	/* block device handle */

	struct page	*sb_page;
	int		sb_loaded;
	__u64		sb_events;
	sector_t	data_offset;	/* start of data in array */
	sector_t 	sb_start;	/* offset of the super block (in 512byte sectors) */
	int		sb_size;	/* bytes in the superblock */
	int		preferred_minor;	/* autorun support */

	struct kobject	kobj;

	/* A device can be in one of three states based on two flags:
	 * Not working:   faulty==1 in_sync==0
	 * Fully working: faulty==0 in_sync==1
	 * Working, but not
	 * in sync with array
	 *                faulty==0 in_sync==0
	 *
	 * It can never have faulty==1, in_sync==1
	 * This reduces the burden of testing multiple flags in many cases
	 */

	unsigned long	flags;
#define	Faulty		1		/* device is known to have a fault */
#define	In_sync		2		/* device is in_sync with rest of array */
#define	WriteMostly	4		/* Avoid reading if at all possible */
#define	BarriersNotsupp	5		/* BIO_RW_BARRIER is not supported */
#define	AllReserved	6		/* If whole device is reserved for
					 * one array */
#define	AutoDetected	7		/* added by auto-detect */
#define Blocked		8		/* An error occured on an externally
					 * managed array, don't allow writes
					 * until it is cleared */
#define StateChanged	9		/* Faulty or Blocked has changed during
					 * interrupt, so it needs to be
					 * notified by the thread */
	wait_queue_head_t blocked_wait;

	int desc_nr;			/* descriptor index in the superblock */
	int raid_disk;			/* role of device in array */
	int saved_raid_disk;		/* role that device used to have in the
					 * array and could again if we did a partial
					 * resync from the bitmap
					 */
	sector_t	recovery_offset;/* If this device has been partially
					 * recovered, this is where we were
					 * up to.
					 */

	atomic_t	nr_pending;	/* number of pending requests.
					 * only maintained for arrays that
					 * support hot removal
					 */
	atomic_t	read_errors;	/* number of consecutive read errors that
					 * we have tried to ignore.
					 */
	atomic_t	corrected_errors; /* number of corrected read errors,
					   * for reporting to userspace and storing
					   * in superblock.
					   */
	struct work_struct del_work;	/* used for delayed sysfs removal */

	struct sysfs_dirent *sysfs_state; /* handle for 'state'
					   * sysfs entry */
};

struct mddev_s
{
	void				*private;
	struct mdk_personality		*pers;
	dev_t				unit;
	int				md_minor;
	struct list_head 		disks;
	unsigned long			flags;
#define MD_CHANGE_DEVS	0	/* Some device status has changed */
#define MD_CHANGE_CLEAN 1	/* transition to or from 'clean' */
#define MD_CHANGE_PENDING 2	/* superblock update in progress */

	int				suspended;
	atomic_t			active_io;
	int				ro;

	struct gendisk			*gendisk;

	struct kobject			kobj;
	int				hold_active;
#define	UNTIL_IOCTL	1
#define	UNTIL_STOP	2

	/* Superblock information */
	int				major_version,
					minor_version,
					patch_version;
	int				persistent;
	int 				external;	/* metadata is
							 * managed externally */
	char				metadata_type[17]; /* externally set*/
	int				chunk_sectors;
	time_t				ctime, utime;
	int				level, layout;
	char				clevel[16];
	int				raid_disks;
	int				max_disks;
	sector_t			dev_sectors; 	/* used size of
							 * component devices */
	sector_t			array_sectors; /* exported array size */
	int				external_size; /* size managed
							* externally */
	__u64				events;

	char				uuid[16];

	/* If the array is being reshaped, we need to record the
	 * new shape and an indication of where we are up to.
	 * This is written to the superblock.
	 * If reshape_position is MaxSector, then no reshape is happening (yet).
	 */
	sector_t			reshape_position;
	int				delta_disks, new_level, new_layout;
	int				new_chunk_sectors;

	struct mdk_thread_s		*thread;	/* management thread */
	struct mdk_thread_s		*sync_thread;	/* doing resync or reconstruct */
	sector_t			curr_resync;	/* last block scheduled */
	/* As resync requests can complete out of order, we cannot easily track
	 * how much resync has been completed.  So we occasionally pause until
	 * everything completes, then set curr_resync_completed to curr_resync.
	 * As such it may be well behind the real resync mark, but it is a value
	 * we are certain of.
	 */
	sector_t			curr_resync_completed;
	unsigned long			resync_mark;	/* a recent timestamp */
	sector_t			resync_mark_cnt;/* blocks written at resync_mark */
	sector_t			curr_mark_cnt; /* blocks scheduled now */

	sector_t			resync_max_sectors; /* may be set by personality */

	sector_t			resync_mismatches; /* count of sectors where
							    * parity/replica mismatch found
							    */

	/* allow user-space to request suspension of IO to regions of the array */
	sector_t			suspend_lo;
	sector_t			suspend_hi;
	/* if zero, use the system-wide default */
	int				sync_speed_min;
	int				sync_speed_max;

	/* resync even though the same disks are shared among md-devices */
	int				parallel_resync;

	int				ok_start_degraded;
	/* recovery/resync flags 
	 * NEEDED:   we might need to start a resync/recover
	 * RUNNING:  a thread is running, or about to be started
	 * SYNC:     actually doing a resync, not a recovery
	 * RECOVER:  doing recovery, or need to try it.
	 * INTR:     resync needs to be aborted for some reason
	 * DONE:     thread is done and is waiting to be reaped
	 * REQUEST:  user-space has requested a sync (used with SYNC)
	 * CHECK:    user-space request for check-only, no repair
	 * RESHAPE:  A reshape is happening
	 *
	 * If neither SYNC or RESHAPE are set, then it is a recovery.
	 */
#define	MD_RECOVERY_RUNNING	0
#define	MD_RECOVERY_SYNC	1
#define	MD_RECOVERY_RECOVER	2
#define	MD_RECOVERY_INTR	3
#define	MD_RECOVERY_DONE	4
#define	MD_RECOVERY_NEEDED	5
#define	MD_RECOVERY_REQUESTED	6
#define	MD_RECOVERY_CHECK	7
#define MD_RECOVERY_RESHAPE	8
#define	MD_RECOVERY_FROZEN	9

	unsigned long			recovery;
	int				recovery_disabled; /* if we detect that recovery
							    * will always fail, set this
							    * so we don't loop trying */

	int				in_sync;	/* know to not need resync */
	/* 'open_mutex' avoids races between 'md_open' and 'do_md_stop', so
	 * that we are never stopping an array while it is open.
	 * 'reconfig_mutex' protects all other reconfiguration.
	 * These locks are separate due to conflicting interactions
	 * with bdev->bd_mutex.
	 * Lock ordering is:
	 *  reconfig_mutex -> bd_mutex : e.g. do_md_run -> revalidate_disk
	 *  bd_mutex -> open_mutex:  e.g. __blkdev_get -> md_open
	 */
	struct mutex			open_mutex;
	struct mutex			reconfig_mutex;
	atomic_t			active;		/* general refcount */
	atomic_t			openers;	/* number of active opens */

	int				changed;	/* true if we might need to reread partition info */
	int				degraded;	/* whether md should consider
							 * adding a spare
							 */
	int				barriers_work;	/* initialised to true, cleared as soon
							 * as a barrier request to slave
							 * fails.  Only supported
							 */
	struct bio			*biolist; 	/* bios that need to be retried
							 * because BIO_RW_BARRIER is not supported
							 */

	atomic_t			recovery_active; /* blocks scheduled, but not written */
	wait_queue_head_t		recovery_wait;
	sector_t			recovery_cp;
	sector_t			resync_min;	/* user requested sync
							 * starts here */
	sector_t			resync_max;	/* resync should pause
							 * when it gets here */

	struct sysfs_dirent		*sysfs_state;	/* handle for 'array_state'
							 * file in sysfs.
							 */
	struct sysfs_dirent		*sysfs_action;  /* handle for 'sync_action' */

	struct work_struct del_work;	/* used for delayed sysfs removal */

	spinlock_t			write_lock;
	wait_queue_head_t		sb_wait;	/* for waiting on superblock updates */
	atomic_t			pending_writes;	/* number of active superblock writes */

	unsigned int			safemode;	/* if set, update "clean" superblock
							 * when no writes pending.
							 */ 
	unsigned int			safemode_delay;
	struct timer_list		safemode_timer;
	atomic_t			writes_pending; 
	struct request_queue		*queue;	/* for plugging ... */

	atomic_t                        write_behind; /* outstanding async IO */
	unsigned int                    max_write_behind; /* 0 = sync */

	struct bitmap                   *bitmap; /* the bitmap for the device */
	struct file			*bitmap_file; /* the bitmap file */
	long				bitmap_offset; /* offset from superblock of
							* start of bitmap. May be
							* negative, but not '0'
							*/
	long				default_bitmap_offset; /* this is the offset to use when
								* hot-adding a bitmap.  It should
								* eventually be settable by sysfs.
								*/
	struct mutex			bitmap_mutex;

	struct list_head		all_mddevs;
};


static inline void rdev_dec_pending(mdk_rdev_t *rdev, mddev_t *mddev)
{
	int faulty = test_bit(Faulty, &rdev->flags);
	if (atomic_dec_and_test(&rdev->nr_pending) && faulty)
		set_bit(MD_RECOVERY_NEEDED, &mddev->recovery);
}

static inline void md_sync_acct(struct block_device *bdev, unsigned long nr_sectors)
{
        atomic_add(nr_sectors, &bdev->bd_contains->bd_disk->sync_io);
}

struct mdk_personality
{
	char *name;
	int level;
	struct list_head list;
	struct module *owner;
	int (*make_request)(struct request_queue *q, struct bio *bio);
	int (*run)(mddev_t *mddev);
	int (*stop)(mddev_t *mddev);
	void (*status)(struct seq_file *seq, mddev_t *mddev);
	/* error_handler must set ->faulty and clear ->in_sync
	 * if appropriate, and should abort recovery if needed 
	 */
	void (*error_handler)(mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_add_disk) (mddev_t *mddev, mdk_rdev_t *rdev);
	int (*hot_remove_disk) (mddev_t *mddev, int number);
	int (*spare_active) (mddev_t *mddev);
	sector_t (*sync_request)(mddev_t *mddev, sector_t sector_nr, int *skipped, int go_faster);
	int (*resize) (mddev_t *mddev, sector_t sectors);
	sector_t (*size) (mddev_t *mddev, sector_t sectors, int raid_disks);
	int (*check_reshape) (mddev_t *mddev);
	int (*start_reshape) (mddev_t *mddev);
	void (*finish_reshape) (mddev_t *mddev);
	/* quiesce moves between quiescence states
	 * 0 - fully active
	 * 1 - no new requests allowed
	 * others - reserved
	 */
	void (*quiesce) (mddev_t *mddev, int state);
	/* takeover is used to transition an array from one
	 * personality to another.  The new personality must be able
	 * to handle the data in the current layout.
	 * e.g. 2drive raid1 -> 2drive raid5
	 *      ndrive raid5 -> degraded n+1drive raid6 with special layout
	 * If the takeover succeeds, a new 'private' structure is returned.
	 * This needs to be installed and then ->run used to activate the
	 * array.
	 */
	void *(*takeover) (mddev_t *mddev);
};


struct md_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(mddev_t *, char *);
	ssize_t (*store)(mddev_t *, const char *, size_t);
};


static inline char * mdname (mddev_t * mddev)
{
	return mddev->gendisk ? mddev->gendisk->disk_name : "mdX";
}

/*
 * iterates through some rdev ringlist. It's safe to remove the
 * current 'rdev'. Dont touch 'tmp' though.
 */
#define rdev_for_each_list(rdev, tmp, head)				\
	list_for_each_entry_safe(rdev, tmp, head, same_set)

/*
 * iterates through the 'same array disks' ringlist
 */
#define rdev_for_each(rdev, tmp, mddev)				\
	list_for_each_entry_safe(rdev, tmp, &((mddev)->disks), same_set)

#define rdev_for_each_rcu(rdev, mddev)				\
	list_for_each_entry_rcu(rdev, &((mddev)->disks), same_set)

typedef struct mdk_thread_s {
	void			(*run) (mddev_t *mddev);
	mddev_t			*mddev;
	wait_queue_head_t	wqueue;
	unsigned long           flags;
	struct task_struct	*tsk;
	unsigned long		timeout;
} mdk_thread_t;

#define THREAD_WAKEUP  0

#define __wait_event_lock_irq(wq, condition, lock, cmd) 		\
do {									\
	wait_queue_t __wait;						\
	init_waitqueue_entry(&__wait, current);				\
									\
	add_wait_queue(&wq, &__wait);					\
	for (;;) {							\
		set_current_state(TASK_UNINTERRUPTIBLE);		\
		if (condition)						\
			break;						\
		spin_unlock_irq(&lock);					\
		cmd;							\
		schedule();						\
		spin_lock_irq(&lock);					\
	}								\
	current->state = TASK_RUNNING;					\
	remove_wait_queue(&wq, &__wait);				\
} while (0)

#define wait_event_lock_irq(wq, condition, lock, cmd) 			\
do {									\
	if (condition)	 						\
		break;							\
	__wait_event_lock_irq(wq, condition, lock, cmd);		\
} while (0)

static inline void safe_put_page(struct page *p)
{
	if (p) put_page(p);
}

extern int register_md_personality(struct mdk_personality *p);
extern int unregister_md_personality(struct mdk_personality *p);
extern mdk_thread_t * md_register_thread(void (*run) (mddev_t *mddev),
				mddev_t *mddev, const char *name);
extern void md_unregister_thread(mdk_thread_t *thread);
extern void md_wakeup_thread(mdk_thread_t *thread);
extern void md_check_recovery(mddev_t *mddev);
extern void md_write_start(mddev_t *mddev, struct bio *bi);
extern void md_write_end(mddev_t *mddev);
extern void md_done_sync(mddev_t *mddev, int blocks, int ok);
extern void md_error(mddev_t *mddev, mdk_rdev_t *rdev);

extern int mddev_congested(mddev_t *mddev, int bits);
extern void md_super_write(mddev_t *mddev, mdk_rdev_t *rdev,
			   sector_t sector, int size, struct page *page);
extern void md_super_wait(mddev_t *mddev);
extern int sync_page_io(struct block_device *bdev, sector_t sector, int size,
			struct page *page, int rw);
extern void md_do_sync(mddev_t *mddev);
extern void md_new_event(mddev_t *mddev);
extern int md_allow_write(mddev_t *mddev);
extern void md_wait_for_blocked_rdev(mdk_rdev_t *rdev, mddev_t *mddev);
extern void md_set_array_sectors(mddev_t *mddev, sector_t array_sectors);
extern int md_check_no_bitmap(mddev_t *mddev);
extern int md_integrity_register(mddev_t *mddev);
void md_integrity_add_rdev(mdk_rdev_t *rdev, mddev_t *mddev);

#endif /* _MD_MD_H */
