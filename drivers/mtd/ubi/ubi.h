/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2006, 2007
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_UBI_H__
#define __UBI_UBI_H__

#include <linux/init.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/notifier.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/ubi.h>
#include <asm/pgtable.h>

#include "ubi-media.h"

/* Maximum number of supported UBI devices */
#define UBI_MAX_DEVICES 32

/* UBI name used for character devices, sysfs, etc */
#define UBI_NAME_STR "ubi"

/* Normal UBI messages */
#define ubi_msg(fmt, ...) printk(KERN_NOTICE "UBI: " fmt "\n", ##__VA_ARGS__)
/* UBI warning messages */
#define ubi_warn(fmt, ...) printk(KERN_WARNING "UBI warning: %s: " fmt "\n", \
				  __func__, ##__VA_ARGS__)
/* UBI error messages */
#define ubi_err(fmt, ...) printk(KERN_ERR "UBI error: %s: " fmt "\n", \
				 __func__, ##__VA_ARGS__)

/* Background thread name pattern */
#define UBI_BGT_NAME_PATTERN "ubi_bgt%dd"

/*
 * This marker in the EBA table means that the LEB is um-mapped.
 * NOTE! It has to have the same value as %UBI_ALL.
 */
#define UBI_LEB_UNMAPPED -1

/*
 * In case of errors, UBI tries to repeat the operation several times before
 * returning error. The below constant defines how many times UBI re-tries.
 */
#define UBI_IO_RETRIES 3

/*
 * Length of the protection queue. The length is effectively equivalent to the
 * number of (global) erase cycles PEBs are protected from the wear-leveling
 * worker.
 */
#define UBI_PROT_QUEUE_LEN 10

/* The volume ID/LEB number/erase counter is unknown */
#define UBI_UNKNOWN -1

/*
 * Error codes returned by the I/O sub-system.
 *
 * UBI_IO_FF: the read region of flash contains only 0xFFs
 * UBI_IO_FF_BITFLIPS: the same as %UBI_IO_FF, but also also there was a data
 *                     integrity error reported by the MTD driver
 *                     (uncorrectable ECC error in case of NAND)
 * UBI_IO_BAD_HDR: the EC or VID header is corrupted (bad magic or CRC)
 * UBI_IO_BAD_HDR_EBADMSG: the same as %UBI_IO_BAD_HDR, but also there was a
 *                         data integrity error reported by the MTD driver
 *                         (uncorrectable ECC error in case of NAND)
 * UBI_IO_BITFLIPS: bit-flips were detected and corrected
 *
 * Note, it is probably better to have bit-flip and ebadmsg as flags which can
 * be or'ed with other error code. But this is a big change because there are
 * may callers, so it does not worth the risk of introducing a bug
 */
enum {
	UBI_IO_FF = 1,
	UBI_IO_FF_BITFLIPS,
	UBI_IO_BAD_HDR,
	UBI_IO_BAD_HDR_EBADMSG,
	UBI_IO_BITFLIPS,
};

/*
 * Return codes of the 'ubi_eba_copy_leb()' function.
 *
 * MOVE_CANCEL_RACE: canceled because the volume is being deleted, the source
 *                   PEB was put meanwhile, or there is I/O on the source PEB
 * MOVE_SOURCE_RD_ERR: canceled because there was a read error from the source
 *                     PEB
 * MOVE_TARGET_RD_ERR: canceled because there was a read error from the target
 *                     PEB
 * MOVE_TARGET_WR_ERR: canceled because there was a write error to the target
 *                     PEB
 * MOVE_TARGET_BITFLIPS: canceled because a bit-flip was detected in the
 *                       target PEB
 * MOVE_RETRY: retry scrubbing the PEB
 */
enum {
	MOVE_CANCEL_RACE = 1,
	MOVE_SOURCE_RD_ERR,
	MOVE_TARGET_RD_ERR,
	MOVE_TARGET_WR_ERR,
	MOVE_TARGET_BITFLIPS,
	MOVE_RETRY,
};

/**
 * struct ubi_wl_entry - wear-leveling entry.
 * @u.rb: link in the corresponding (free/used) RB-tree
 * @u.list: link in the protection queue
 * @ec: erase counter
 * @pnum: physical eraseblock number
 *
 * This data structure is used in the WL sub-system. Each physical eraseblock
 * has a corresponding &struct wl_entry object which may be kept in different
 * RB-trees. See WL sub-system for details.
 */
struct ubi_wl_entry {
	union {
		struct rb_node rb;
		struct list_head list;
	} u;
	int ec;
	int pnum;
};

/**
 * struct ubi_ltree_entry - an entry in the lock tree.
 * @rb: links RB-tree nodes
 * @vol_id: volume ID of the locked logical eraseblock
 * @lnum: locked logical eraseblock number
 * @users: how many tasks are using this logical eraseblock or wait for it
 * @mutex: read/write mutex to implement read/write access serialization to
 *         the (@vol_id, @lnum) logical eraseblock
 *
 * This data structure is used in the EBA sub-system to implement per-LEB
 * locking. When a logical eraseblock is being locked - corresponding
 * &struct ubi_ltree_entry object is inserted to the lock tree (@ubi->ltree).
 * See EBA sub-system for details.
 */
struct ubi_ltree_entry {
	struct rb_node rb;
	int vol_id;
	int lnum;
	int users;
	struct rw_semaphore mutex;
};

/**
 * struct ubi_rename_entry - volume re-name description data structure.
 * @new_name_len: new volume name length
 * @new_name: new volume name
 * @remove: if not zero, this volume should be removed, not re-named
 * @desc: descriptor of the volume
 * @list: links re-name entries into a list
 *
 * This data structure is utilized in the multiple volume re-name code. Namely,
 * UBI first creates a list of &struct ubi_rename_entry objects from the
 * &struct ubi_rnvol_req request object, and then utilizes this list to do all
 * the job.
 */
struct ubi_rename_entry {
	int new_name_len;
	char new_name[UBI_VOL_NAME_MAX + 1];
	int remove;
	struct ubi_volume_desc *desc;
	struct list_head list;
};

struct ubi_volume_desc;

/**
 * struct ubi_volume - UBI volume description data structure.
 * @dev: device object to make use of the the Linux device model
 * @cdev: character device object to create character device
 * @ubi: reference to the UBI device description object
 * @vol_id: volume ID
 * @ref_count: volume reference count
 * @readers: number of users holding this volume in read-only mode
 * @writers: number of users holding this volume in read-write mode
 * @exclusive: whether somebody holds this volume in exclusive mode
 *
 * @reserved_pebs: how many physical eraseblocks are reserved for this volume
 * @vol_type: volume type (%UBI_DYNAMIC_VOLUME or %UBI_STATIC_VOLUME)
 * @usable_leb_size: logical eraseblock size without padding
 * @used_ebs: how many logical eraseblocks in this volume contain data
 * @last_eb_bytes: how many bytes are stored in the last logical eraseblock
 * @used_bytes: how many bytes of data this volume contains
 * @alignment: volume alignment
 * @data_pad: how many bytes are not used at the end of physical eraseblocks to
 *            satisfy the requested alignment
 * @name_len: volume name length
 * @name: volume name
 *
 * @upd_ebs: how many eraseblocks are expected to be updated
 * @ch_lnum: LEB number which is being changing by the atomic LEB change
 *           operation
 * @upd_bytes: how many bytes are expected to be received for volume update or
 *             atomic LEB change
 * @upd_received: how many bytes were already received for volume update or
 *                atomic LEB change
 * @upd_buf: update buffer which is used to collect update data or data for
 *           atomic LEB change
 *
 * @eba_tbl: EBA table of this volume (LEB->PEB mapping)
 * @checked: %1 if this static volume was checked
 * @corrupted: %1 if the volume is corrupted (static volumes only)
 * @upd_marker: %1 if the update marker is set for this volume
 * @updating: %1 if the volume is being updated
 * @changing_leb: %1 if the atomic LEB change ioctl command is in progress
 * @direct_writes: %1 if direct writes are enabled for this volume
 *
 * The @corrupted field indicates that the volume's contents is corrupted.
 * Since UBI protects only static volumes, this field is not relevant to
 * dynamic volumes - it is user's responsibility to assure their data
 * integrity.
 *
 * The @upd_marker flag indicates that this volume is either being updated at
 * the moment or is damaged because of an unclean reboot.
 */
struct ubi_volume {
	struct device dev;
	struct cdev cdev;
	struct ubi_device *ubi;
	int vol_id;
	int ref_count;
	int readers;
	int writers;
	int exclusive;

	int reserved_pebs;
	int vol_type;
	int usable_leb_size;
	int used_ebs;
	int last_eb_bytes;
	long long used_bytes;
	int alignment;
	int data_pad;
	int name_len;
	char name[UBI_VOL_NAME_MAX + 1];

	int upd_ebs;
	int ch_lnum;
	long long upd_bytes;
	long long upd_received;
	void *upd_buf;

	int *eba_tbl;
	unsigned int checked:1;
	unsigned int corrupted:1;
	unsigned int upd_marker:1;
	unsigned int updating:1;
	unsigned int changing_leb:1;
	unsigned int direct_writes:1;
};

/**
 * struct ubi_volume_desc - UBI volume descriptor returned when it is opened.
 * @vol: reference to the corresponding volume description object
 * @mode: open mode (%UBI_READONLY, %UBI_READWRITE, or %UBI_EXCLUSIVE)
 */
struct ubi_volume_desc {
	struct ubi_volume *vol;
	int mode;
};

struct ubi_wl_entry;

/**
 * struct ubi_device - UBI device description structure
 * @dev: UBI device object to use the the Linux device model
 * @cdev: character device object to create character device
 * @ubi_num: UBI device number
 * @ubi_name: UBI device name
 * @vol_count: number of volumes in this UBI device
 * @volumes: volumes of this UBI device
 * @volumes_lock: protects @volumes, @rsvd_pebs, @avail_pebs, beb_rsvd_pebs,
 *                @beb_rsvd_level, @bad_peb_count, @good_peb_count, @vol_count,
 *                @vol->readers, @vol->writers, @vol->exclusive,
 *                @vol->ref_count, @vol->mapping and @vol->eba_tbl.
 * @ref_count: count of references on the UBI device
 * @image_seq: image sequence number recorded on EC headers
 *
 * @rsvd_pebs: count of reserved physical eraseblocks
 * @avail_pebs: count of available physical eraseblocks
 * @beb_rsvd_pebs: how many physical eraseblocks are reserved for bad PEB
 *                 handling
 * @beb_rsvd_level: normal level of PEBs reserved for bad PEB handling
 *
 * @autoresize_vol_id: ID of the volume which has to be auto-resized at the end
 *                     of UBI initialization
 * @vtbl_slots: how many slots are available in the volume table
 * @vtbl_size: size of the volume table in bytes
 * @vtbl: in-RAM volume table copy
 * @device_mutex: protects on-flash volume table and serializes volume
 *                creation, deletion, update, re-size, re-name and set
 *                property
 *
 * @max_ec: current highest erase counter value
 * @mean_ec: current mean erase counter value
 *
 * @global_sqnum: global sequence number
 * @ltree_lock: protects the lock tree and @global_sqnum
 * @ltree: the lock tree
 * @alc_mutex: serializes "atomic LEB change" operations
 *
 * @used: RB-tree of used physical eraseblocks
 * @erroneous: RB-tree of erroneous used physical eraseblocks
 * @free: RB-tree of free physical eraseblocks
 * @scrub: RB-tree of physical eraseblocks which need scrubbing
 * @pq: protection queue (contain physical eraseblocks which are temporarily
 *      protected from the wear-leveling worker)
 * @pq_head: protection queue head
 * @wl_lock: protects the @used, @free, @pq, @pq_head, @lookuptbl, @move_from,
 *	     @move_to, @move_to_put @erase_pending, @wl_scheduled, @works,
 *	     @erroneous, and @erroneous_peb_count fields
 * @move_mutex: serializes eraseblock moves
 * @work_sem: synchronizes the WL worker with use tasks
 * @wl_scheduled: non-zero if the wear-leveling was scheduled
 * @lookuptbl: a table to quickly find a &struct ubi_wl_entry object for any
 *             physical eraseblock
 * @move_from: physical eraseblock from where the data is being moved
 * @move_to: physical eraseblock where the data is being moved to
 * @move_to_put: if the "to" PEB was put
 * @works: list of pending works
 * @works_count: count of pending works
 * @bgt_thread: background thread description object
 * @thread_enabled: if the background thread is enabled
 * @bgt_name: background thread name
 *
 * @flash_size: underlying MTD device size (in bytes)
 * @peb_count: count of physical eraseblocks on the MTD device
 * @peb_size: physical eraseblock size
 * @bad_peb_limit: top limit of expected bad physical eraseblocks
 * @bad_peb_count: count of bad physical eraseblocks
 * @good_peb_count: count of good physical eraseblocks
 * @corr_peb_count: count of corrupted physical eraseblocks (preserved and not
 *                  used by UBI)
 * @erroneous_peb_count: count of erroneous physical eraseblocks in @erroneous
 * @max_erroneous: maximum allowed amount of erroneous physical eraseblocks
 * @min_io_size: minimal input/output unit size of the underlying MTD device
 * @hdrs_min_io_size: minimal I/O unit size used for VID and EC headers
 * @ro_mode: if the UBI device is in read-only mode
 * @leb_size: logical eraseblock size
 * @leb_start: starting offset of logical eraseblocks within physical
 *             eraseblocks
 * @ec_hdr_alsize: size of the EC header aligned to @hdrs_min_io_size
 * @vid_hdr_alsize: size of the VID header aligned to @hdrs_min_io_size
 * @vid_hdr_offset: starting offset of the volume identifier header (might be
 *                  unaligned)
 * @vid_hdr_aloffset: starting offset of the VID header aligned to
 * @hdrs_min_io_size
 * @vid_hdr_shift: contains @vid_hdr_offset - @vid_hdr_aloffset
 * @bad_allowed: whether the MTD device admits of bad physical eraseblocks or
 *               not
 * @nor_flash: non-zero if working on top of NOR flash
 * @max_write_size: maximum amount of bytes the underlying flash can write at a
 *                  time (MTD write buffer size)
 * @mtd: MTD device descriptor
 *
 * @peb_buf: a buffer of PEB size used for different purposes
 * @buf_mutex: protects @peb_buf
 * @ckvol_mutex: serializes static volume checking when opening
 *
 * @dbg: debugging information for this UBI device
 */
struct ubi_device {
	struct cdev cdev;
	struct device dev;
	int ubi_num;
	char ubi_name[sizeof(UBI_NAME_STR)+5];
	int vol_count;
	struct ubi_volume *volumes[UBI_MAX_VOLUMES+UBI_INT_VOL_COUNT];
	spinlock_t volumes_lock;
	int ref_count;
	int image_seq;

	int rsvd_pebs;
	int avail_pebs;
	int beb_rsvd_pebs;
	int beb_rsvd_level;
	int bad_peb_limit;

	int autoresize_vol_id;
	int vtbl_slots;
	int vtbl_size;
	struct ubi_vtbl_record *vtbl;
	struct mutex device_mutex;

	int max_ec;
	/* Note, mean_ec is not updated run-time - should be fixed */
	int mean_ec;

	/* EBA sub-system's stuff */
	unsigned long long global_sqnum;
	spinlock_t ltree_lock;
	struct rb_root ltree;
	struct mutex alc_mutex;

	/* Wear-leveling sub-system's stuff */
	struct rb_root used;
	struct rb_root erroneous;
	struct rb_root free;
	struct rb_root scrub;
	struct list_head pq[UBI_PROT_QUEUE_LEN];
	int pq_head;
	spinlock_t wl_lock;
	struct mutex move_mutex;
	struct rw_semaphore work_sem;
	int wl_scheduled;
	struct ubi_wl_entry **lookuptbl;
	struct ubi_wl_entry *move_from;
	struct ubi_wl_entry *move_to;
	int move_to_put;
	struct list_head works;
	int works_count;
	struct task_struct *bgt_thread;
	int thread_enabled;
	char bgt_name[sizeof(UBI_BGT_NAME_PATTERN)+2];

	/* I/O sub-system's stuff */
	long long flash_size;
	int peb_count;
	int peb_size;
	int bad_peb_count;
	int good_peb_count;
	int corr_peb_count;
	int erroneous_peb_count;
	int max_erroneous;
	int min_io_size;
	int hdrs_min_io_size;
	int ro_mode;
	int leb_size;
	int leb_start;
	int ec_hdr_alsize;
	int vid_hdr_alsize;
	int vid_hdr_offset;
	int vid_hdr_aloffset;
	int vid_hdr_shift;
	unsigned int bad_allowed:1;
	unsigned int nor_flash:1;
	int max_write_size;
	struct mtd_info *mtd;

	void *peb_buf;
	struct mutex buf_mutex;
	struct mutex ckvol_mutex;

	struct ubi_debug_info *dbg;
};

/**
 * struct ubi_ainf_peb - attach information about a physical eraseblock.
 * @ec: erase counter (%UBI_UNKNOWN if it is unknown)
 * @pnum: physical eraseblock number
 * @vol_id: ID of the volume this LEB belongs to
 * @lnum: logical eraseblock number
 * @scrub: if this physical eraseblock needs scrubbing
 * @copy_flag: this LEB is a copy (@copy_flag is set in VID header of this LEB)
 * @sqnum: sequence number
 * @u: unions RB-tree or @list links
 * @u.rb: link in the per-volume RB-tree of &struct ubi_ainf_peb objects
 * @u.list: link in one of the eraseblock lists
 *
 * One object of this type is allocated for each physical eraseblock when
 * attaching an MTD device. Note, if this PEB does not belong to any LEB /
 * volume, the @vol_id and @lnum fields are initialized to %UBI_UNKNOWN.
 */
struct ubi_ainf_peb {
	int ec;
	int pnum;
	int vol_id;
	int lnum;
	unsigned int scrub:1;
	unsigned int copy_flag:1;
	unsigned long long sqnum;
	union {
		struct rb_node rb;
		struct list_head list;
	} u;
};

/**
 * struct ubi_ainf_volume - attaching information about a volume.
 * @vol_id: volume ID
 * @highest_lnum: highest logical eraseblock number in this volume
 * @leb_count: number of logical eraseblocks in this volume
 * @vol_type: volume type
 * @used_ebs: number of used logical eraseblocks in this volume (only for
 *            static volumes)
 * @last_data_size: amount of data in the last logical eraseblock of this
 *                  volume (always equivalent to the usable logical eraseblock
 *                  size in case of dynamic volumes)
 * @data_pad: how many bytes at the end of logical eraseblocks of this volume
 *            are not used (due to volume alignment)
 * @compat: compatibility flags of this volume
 * @rb: link in the volume RB-tree
 * @root: root of the RB-tree containing all the eraseblock belonging to this
 *        volume (&struct ubi_ainf_peb objects)
 *
 * One object of this type is allocated for each volume when attaching an MTD
 * device.
 */
struct ubi_ainf_volume {
	int vol_id;
	int highest_lnum;
	int leb_count;
	int vol_type;
	int used_ebs;
	int last_data_size;
	int data_pad;
	int compat;
	struct rb_node rb;
	struct rb_root root;
};

/**
 * struct ubi_attach_info - MTD device attaching information.
 * @volumes: root of the volume RB-tree
 * @corr: list of corrupted physical eraseblocks
 * @free: list of free physical eraseblocks
 * @erase: list of physical eraseblocks which have to be erased
 * @alien: list of physical eraseblocks which should not be used by UBI (e.g.,
 *         those belonging to "preserve"-compatible internal volumes)
 * @corr_peb_count: count of PEBs in the @corr list
 * @empty_peb_count: count of PEBs which are presumably empty (contain only
 *                   0xFF bytes)
 * @alien_peb_count: count of PEBs in the @alien list
 * @bad_peb_count: count of bad physical eraseblocks
 * @maybe_bad_peb_count: count of bad physical eraseblocks which are not marked
 *                       as bad yet, but which look like bad
 * @vols_found: number of volumes found
 * @highest_vol_id: highest volume ID
 * @is_empty: flag indicating whether the MTD device is empty or not
 * @min_ec: lowest erase counter value
 * @max_ec: highest erase counter value
 * @max_sqnum: highest sequence number value
 * @mean_ec: mean erase counter value
 * @ec_sum: a temporary variable used when calculating @mean_ec
 * @ec_count: a temporary variable used when calculating @mean_ec
 * @aeb_slab_cache: slab cache for &struct ubi_ainf_peb objects
 *
 * This data structure contains the result of attaching an MTD device and may
 * be used by other UBI sub-systems to build final UBI data structures, further
 * error-recovery and so on.
 */
struct ubi_attach_info {
	struct rb_root volumes;
	struct list_head corr;
	struct list_head free;
	struct list_head erase;
	struct list_head alien;
	int corr_peb_count;
	int empty_peb_count;
	int alien_peb_count;
	int bad_peb_count;
	int maybe_bad_peb_count;
	int vols_found;
	int highest_vol_id;
	int is_empty;
	int min_ec;
	int max_ec;
	unsigned long long max_sqnum;
	int mean_ec;
	uint64_t ec_sum;
	int ec_count;
	struct kmem_cache *aeb_slab_cache;
};

#include "debug.h"

extern struct kmem_cache *ubi_wl_entry_slab;
extern const struct file_operations ubi_ctrl_cdev_operations;
extern const struct file_operations ubi_cdev_operations;
extern const struct file_operations ubi_vol_cdev_operations;
extern struct class *ubi_class;
extern struct mutex ubi_devices_mutex;
extern struct blocking_notifier_head ubi_notifiers;

/* scan.c */
int ubi_add_to_av(struct ubi_device *ubi, struct ubi_attach_info *ai, int pnum,
		  int ec, const struct ubi_vid_hdr *vid_hdr, int bitflips);
struct ubi_ainf_volume *ubi_find_av(const struct ubi_attach_info *ai,
				    int vol_id);
void ubi_remove_av(struct ubi_attach_info *ai, struct ubi_ainf_volume *av);
struct ubi_ainf_peb *ubi_early_get_peb(struct ubi_device *ubi,
				       struct ubi_attach_info *ai);
int ubi_attach(struct ubi_device *ubi);
void ubi_destroy_ai(struct ubi_attach_info *ai);

/* vtbl.c */
int ubi_change_vtbl_record(struct ubi_device *ubi, int idx,
			   struct ubi_vtbl_record *vtbl_rec);
int ubi_vtbl_rename_volumes(struct ubi_device *ubi,
			    struct list_head *rename_list);
int ubi_read_volume_table(struct ubi_device *ubi, struct ubi_attach_info *ai);

/* vmt.c */
int ubi_create_volume(struct ubi_device *ubi, struct ubi_mkvol_req *req);
int ubi_remove_volume(struct ubi_volume_desc *desc, int no_vtbl);
int ubi_resize_volume(struct ubi_volume_desc *desc, int reserved_pebs);
int ubi_rename_volumes(struct ubi_device *ubi, struct list_head *rename_list);
int ubi_add_volume(struct ubi_device *ubi, struct ubi_volume *vol);
void ubi_free_volume(struct ubi_device *ubi, struct ubi_volume *vol);

/* upd.c */
int ubi_start_update(struct ubi_device *ubi, struct ubi_volume *vol,
		     long long bytes);
int ubi_more_update_data(struct ubi_device *ubi, struct ubi_volume *vol,
			 const void __user *buf, int count);
int ubi_start_leb_change(struct ubi_device *ubi, struct ubi_volume *vol,
			 const struct ubi_leb_change_req *req);
int ubi_more_leb_change_data(struct ubi_device *ubi, struct ubi_volume *vol,
			     const void __user *buf, int count);

/* misc.c */
int ubi_calc_data_len(const struct ubi_device *ubi, const void *buf,
		      int length);
int ubi_check_volume(struct ubi_device *ubi, int vol_id);
void ubi_update_reserved(struct ubi_device *ubi);
void ubi_calculate_reserved(struct ubi_device *ubi);
int ubi_check_pattern(const void *buf, uint8_t patt, int size);

/* eba.c */
int ubi_eba_unmap_leb(struct ubi_device *ubi, struct ubi_volume *vol,
		      int lnum);
int ubi_eba_read_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
		     void *buf, int offset, int len, int check);
int ubi_eba_write_leb(struct ubi_device *ubi, struct ubi_volume *vol, int lnum,
		      const void *buf, int offset, int len);
int ubi_eba_write_leb_st(struct ubi_device *ubi, struct ubi_volume *vol,
			 int lnum, const void *buf, int len, int used_ebs);
int ubi_eba_atomic_leb_change(struct ubi_device *ubi, struct ubi_volume *vol,
			      int lnum, const void *buf, int len);
int ubi_eba_copy_leb(struct ubi_device *ubi, int from, int to,
		     struct ubi_vid_hdr *vid_hdr);
int ubi_eba_init(struct ubi_device *ubi, struct ubi_attach_info *ai);

/* wl.c */
int ubi_wl_get_peb(struct ubi_device *ubi);
int ubi_wl_put_peb(struct ubi_device *ubi, int vol_id, int lnum,
		   int pnum, int torture);
int ubi_wl_flush(struct ubi_device *ubi, int vol_id, int lnum);
int ubi_wl_scrub_peb(struct ubi_device *ubi, int pnum);
int ubi_wl_init(struct ubi_device *ubi, struct ubi_attach_info *ai);
void ubi_wl_close(struct ubi_device *ubi);
int ubi_thread(void *u);

/* io.c */
int ubi_io_read(const struct ubi_device *ubi, void *buf, int pnum, int offset,
		int len);
int ubi_io_write(struct ubi_device *ubi, const void *buf, int pnum, int offset,
		 int len);
int ubi_io_sync_erase(struct ubi_device *ubi, int pnum, int torture);
int ubi_io_is_bad(const struct ubi_device *ubi, int pnum);
int ubi_io_mark_bad(const struct ubi_device *ubi, int pnum);
int ubi_io_read_ec_hdr(struct ubi_device *ubi, int pnum,
		       struct ubi_ec_hdr *ec_hdr, int verbose);
int ubi_io_write_ec_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_ec_hdr *ec_hdr);
int ubi_io_read_vid_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_vid_hdr *vid_hdr, int verbose);
int ubi_io_write_vid_hdr(struct ubi_device *ubi, int pnum,
			 struct ubi_vid_hdr *vid_hdr);

/* build.c */
int ubi_attach_mtd_dev(struct mtd_info *mtd, int ubi_num, int vid_hdr_offset);
int ubi_detach_mtd_dev(int ubi_num, int anyway);
struct ubi_device *ubi_get_device(int ubi_num);
void ubi_put_device(struct ubi_device *ubi);
struct ubi_device *ubi_get_by_major(int major);
int ubi_major2num(int major);
int ubi_volume_notify(struct ubi_device *ubi, struct ubi_volume *vol,
		      int ntype);
int ubi_notify_all(struct ubi_device *ubi, int ntype,
		   struct notifier_block *nb);
int ubi_enumerate_volumes(struct notifier_block *nb);
void ubi_free_internal_volumes(struct ubi_device *ubi);

/* kapi.c */
void ubi_do_get_device_info(struct ubi_device *ubi, struct ubi_device_info *di);
void ubi_do_get_volume_info(struct ubi_device *ubi, struct ubi_volume *vol,
			    struct ubi_volume_info *vi);

/*
 * ubi_rb_for_each_entry - walk an RB-tree.
 * @rb: a pointer to type 'struct rb_node' to use as a loop counter
 * @pos: a pointer to RB-tree entry type to use as a loop counter
 * @root: RB-tree's root
 * @member: the name of the 'struct rb_node' within the RB-tree entry
 */
#define ubi_rb_for_each_entry(rb, pos, root, member)                         \
	for (rb = rb_first(root),                                            \
	     pos = (rb ? container_of(rb, typeof(*pos), member) : NULL);     \
	     rb;                                                             \
	     rb = rb_next(rb),                                               \
	     pos = (rb ? container_of(rb, typeof(*pos), member) : NULL))

/*
 * ubi_move_aeb_to_list - move a PEB from the volume tree to a list.
 *
 * @av: volume attaching information
 * @aeb: attaching eraseblock information
 * @list: the list to move to
 */
static inline void ubi_move_aeb_to_list(struct ubi_ainf_volume *av,
					 struct ubi_ainf_peb *aeb,
					 struct list_head *list)
{
		rb_erase(&aeb->u.rb, &av->root);
		list_add_tail(&aeb->u.list, list);
}

/**
 * ubi_zalloc_vid_hdr - allocate a volume identifier header object.
 * @ubi: UBI device description object
 * @gfp_flags: GFP flags to allocate with
 *
 * This function returns a pointer to the newly allocated and zero-filled
 * volume identifier header object in case of success and %NULL in case of
 * failure.
 */
static inline struct ubi_vid_hdr *
ubi_zalloc_vid_hdr(const struct ubi_device *ubi, gfp_t gfp_flags)
{
	void *vid_hdr;

	vid_hdr = kzalloc(ubi->vid_hdr_alsize, gfp_flags);
	if (!vid_hdr)
		return NULL;

	/*
	 * VID headers may be stored at un-aligned flash offsets, so we shift
	 * the pointer.
	 */
	return vid_hdr + ubi->vid_hdr_shift;
}

/**
 * ubi_free_vid_hdr - free a volume identifier header object.
 * @ubi: UBI device description object
 * @vid_hdr: the object to free
 */
static inline void ubi_free_vid_hdr(const struct ubi_device *ubi,
				    struct ubi_vid_hdr *vid_hdr)
{
	void *p = vid_hdr;

	if (!p)
		return;

	kfree(p - ubi->vid_hdr_shift);
}

/*
 * This function is equivalent to 'ubi_io_read()', but @offset is relative to
 * the beginning of the logical eraseblock, not to the beginning of the
 * physical eraseblock.
 */
static inline int ubi_io_read_data(const struct ubi_device *ubi, void *buf,
				   int pnum, int offset, int len)
{
	ubi_assert(offset >= 0);
	return ubi_io_read(ubi, buf, pnum, offset + ubi->leb_start, len);
}

/*
 * This function is equivalent to 'ubi_io_write()', but @offset is relative to
 * the beginning of the logical eraseblock, not to the beginning of the
 * physical eraseblock.
 */
static inline int ubi_io_write_data(struct ubi_device *ubi, const void *buf,
				    int pnum, int offset, int len)
{
	ubi_assert(offset >= 0);
	return ubi_io_write(ubi, buf, pnum, offset + ubi->leb_start, len);
}

/**
 * ubi_ro_mode - switch to read-only mode.
 * @ubi: UBI device description object
 */
static inline void ubi_ro_mode(struct ubi_device *ubi)
{
	if (!ubi->ro_mode) {
		ubi->ro_mode = 1;
		ubi_warn("switch to read-only mode");
		dump_stack();
	}
}

/**
 * vol_id2idx - get table index by volume ID.
 * @ubi: UBI device description object
 * @vol_id: volume ID
 */
static inline int vol_id2idx(const struct ubi_device *ubi, int vol_id)
{
	if (vol_id >= UBI_INTERNAL_VOL_START)
		return vol_id - UBI_INTERNAL_VOL_START + ubi->vtbl_slots;
	else
		return vol_id;
}

/**
 * idx2vol_id - get volume ID by table index.
 * @ubi: UBI device description object
 * @idx: table index
 */
static inline int idx2vol_id(const struct ubi_device *ubi, int idx)
{
	if (idx >= ubi->vtbl_slots)
		return idx - ubi->vtbl_slots + UBI_INTERNAL_VOL_START;
	else
		return idx;
}

#endif /* !__UBI_UBI_H__ */
