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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LUSTRE_LU_OBJECT_H
#define __LUSTRE_LU_OBJECT_H

#include <stdarg.h>
#include <linux/libcfs/libcfs.h>
#include <lustre/lustre_idl.h>
#include <lu_ref.h>

struct seq_file;
struct proc_dir_entry;
struct lustre_cfg;
struct lprocfs_stats;

/** \defgroup lu lu
 * lu_* data-types represent server-side entities shared by data and meta-data
 * stacks.
 *
 * Design goals:
 *
 * -# support for layering.
 *
 *     Server side object is split into layers, one per device in the
 *     corresponding device stack. Individual layer is represented by struct
 *     lu_object. Compound layered object --- by struct lu_object_header. Most
 *     interface functions take lu_object as an argument and operate on the
 *     whole compound object. This decision was made due to the following
 *     reasons:
 *
 *	- it's envisaged that lu_object will be used much more often than
 *	lu_object_header;
 *
 *	- we want lower (non-top) layers to be able to initiate operations
 *	on the whole object.
 *
 *     Generic code supports layering more complex than simple stacking, e.g.,
 *     it is possible that at some layer object "spawns" multiple sub-objects
 *     on the lower layer.
 *
 * -# fid-based identification.
 *
 *     Compound object is uniquely identified by its fid. Objects are indexed
 *     by their fids (hash table is used for index).
 *
 * -# caching and life-cycle management.
 *
 *     Object's life-time is controlled by reference counting. When reference
 *     count drops to 0, object is returned to cache. Cached objects still
 *     retain their identity (i.e., fid), and can be recovered from cache.
 *
 *     Objects are kept in the global LRU list, and lu_site_purge() function
 *     can be used to reclaim given number of unused objects from the tail of
 *     the LRU.
 *
 * -# avoiding recursion.
 *
 *     Generic code tries to replace recursion through layers by iterations
 *     where possible. Additionally to the end of reducing stack consumption,
 *     data, when practically possible, are allocated through lu_context_key
 *     interface rather than on stack.
 * @{
 */

struct lu_site;
struct lu_object;
struct lu_device;
struct lu_object_header;
struct lu_context;
struct lu_env;

/**
 * Operations common for data and meta-data devices.
 */
struct lu_device_operations {
	/**
	 * Allocate object for the given device (without lower-layer
	 * parts). This is called by lu_object_operations::loo_object_init()
	 * from the parent layer, and should setup at least lu_object::lo_dev
	 * and lu_object::lo_ops fields of resulting lu_object.
	 *
	 * Object creation protocol.
	 *
	 * Due to design goal of avoiding recursion, object creation (see
	 * lu_object_alloc()) is somewhat involved:
	 *
	 *  - first, lu_device_operations::ldo_object_alloc() method of the
	 *  top-level device in the stack is called. It should allocate top
	 *  level object (including lu_object_header), but without any
	 *  lower-layer sub-object(s).
	 *
	 *  - then lu_object_alloc() sets fid in the header of newly created
	 *  object.
	 *
	 *  - then lu_object_operations::loo_object_init() is called. It has
	 *  to allocate lower-layer object(s). To do this,
	 *  lu_object_operations::loo_object_init() calls ldo_object_alloc()
	 *  of the lower-layer device(s).
	 *
	 *  - for all new objects allocated by
	 *  lu_object_operations::loo_object_init() (and inserted into object
	 *  stack), lu_object_operations::loo_object_init() is called again
	 *  repeatedly, until no new objects are created.
	 *
	 * \post ergo(!IS_ERR(result), result->lo_dev == d &&
	 *			     result->lo_ops != NULL);
	 */
	struct lu_object *(*ldo_object_alloc)(const struct lu_env *env,
					      const struct lu_object_header *h,
					      struct lu_device *d);
	/**
	 * process config specific for device.
	 */
	int (*ldo_process_config)(const struct lu_env *env,
				  struct lu_device *, struct lustre_cfg *);
	int (*ldo_recovery_complete)(const struct lu_env *,
				     struct lu_device *);

	/**
	 * initialize local objects for device. this method called after layer has
	 * been initialized (after LCFG_SETUP stage) and before it starts serving
	 * user requests.
	 */

	int (*ldo_prepare)(const struct lu_env *,
			   struct lu_device *parent,
			   struct lu_device *dev);

};

/**
 * For lu_object_conf flags
 */
typedef enum {
	/* This is a new object to be allocated, or the file
	 * corresponding to the object does not exists. */
	LOC_F_NEW	= 0x00000001,
} loc_flags_t;

/**
 * Object configuration, describing particulars of object being created. On
 * server this is not used, as server objects are full identified by fid. On
 * client configuration contains struct lustre_md.
 */
struct lu_object_conf {
	/**
	 * Some hints for obj find and alloc.
	 */
	loc_flags_t     loc_flags;
};

/**
 * Type of "printer" function used by lu_object_operations::loo_object_print()
 * method.
 *
 * Printer function is needed to provide some flexibility in (semi-)debugging
 * output: possible implementations: printk, CDEBUG, sysfs/seq_file
 */
typedef int (*lu_printer_t)(const struct lu_env *env,
			    void *cookie, const char *format, ...)
	__attribute__ ((format (printf, 3, 4)));

/**
 * Operations specific for particular lu_object.
 */
struct lu_object_operations {

	/**
	 * Allocate lower-layer parts of the object by calling
	 * lu_device_operations::ldo_object_alloc() of the corresponding
	 * underlying device.
	 *
	 * This method is called once for each object inserted into object
	 * stack. It's responsibility of this method to insert lower-layer
	 * object(s) it create into appropriate places of object stack.
	 */
	int (*loo_object_init)(const struct lu_env *env,
			       struct lu_object *o,
			       const struct lu_object_conf *conf);
	/**
	 * Called (in top-to-bottom order) during object allocation after all
	 * layers were allocated and initialized. Can be used to perform
	 * initialization depending on lower layers.
	 */
	int (*loo_object_start)(const struct lu_env *env,
				struct lu_object *o);
	/**
	 * Called before lu_object_operations::loo_object_free() to signal
	 * that object is being destroyed. Dual to
	 * lu_object_operations::loo_object_init().
	 */
	void (*loo_object_delete)(const struct lu_env *env,
				  struct lu_object *o);
	/**
	 * Dual to lu_device_operations::ldo_object_alloc(). Called when
	 * object is removed from memory.
	 */
	void (*loo_object_free)(const struct lu_env *env,
				struct lu_object *o);
	/**
	 * Called when last active reference to the object is released (and
	 * object returns to the cache). This method is optional.
	 */
	void (*loo_object_release)(const struct lu_env *env,
				   struct lu_object *o);
	/**
	 * Optional debugging helper. Print given object.
	 */
	int (*loo_object_print)(const struct lu_env *env, void *cookie,
				lu_printer_t p, const struct lu_object *o);
	/**
	 * Optional debugging method. Returns true iff method is internally
	 * consistent.
	 */
	int (*loo_object_invariant)(const struct lu_object *o);
};

/**
 * Type of lu_device.
 */
struct lu_device_type;

/**
 * Device: a layer in the server side abstraction stacking.
 */
struct lu_device {
	/**
	 * reference count. This is incremented, in particular, on each object
	 * created at this layer.
	 *
	 * \todo XXX which means that atomic_t is probably too small.
	 */
	atomic_t		       ld_ref;
	/**
	 * Pointer to device type. Never modified once set.
	 */
	struct lu_device_type       *ld_type;
	/**
	 * Operation vector for this device.
	 */
	const struct lu_device_operations *ld_ops;
	/**
	 * Stack this device belongs to.
	 */
	struct lu_site		    *ld_site;
	struct proc_dir_entry	     *ld_proc_entry;

	/** \todo XXX: temporary back pointer into obd. */
	struct obd_device		 *ld_obd;
	/**
	 * A list of references to this object, for debugging.
	 */
	struct lu_ref		      ld_reference;
	/**
	 * Link the device to the site.
	 **/
	struct list_head			 ld_linkage;
};

struct lu_device_type_operations;

/**
 * Tag bits for device type. They are used to distinguish certain groups of
 * device types.
 */
enum lu_device_tag {
	/** this is meta-data device */
	LU_DEVICE_MD = (1 << 0),
	/** this is data device */
	LU_DEVICE_DT = (1 << 1),
	/** data device in the client stack */
	LU_DEVICE_CL = (1 << 2)
};

/**
 * Type of device.
 */
struct lu_device_type {
	/**
	 * Tag bits. Taken from enum lu_device_tag. Never modified once set.
	 */
	__u32				   ldt_tags;
	/**
	 * Name of this class. Unique system-wide. Never modified once set.
	 */
	char				   *ldt_name;
	/**
	 * Operations for this type.
	 */
	const struct lu_device_type_operations *ldt_ops;
	/**
	 * \todo XXX: temporary pointer to associated obd_type.
	 */
	struct obd_type			*ldt_obd_type;
	/**
	 * \todo XXX: temporary: context tags used by obd_*() calls.
	 */
	__u32				   ldt_ctx_tags;
	/**
	 * Number of existing device type instances.
	 */
	unsigned				ldt_device_nr;
	/**
	 * Linkage into a global list of all device types.
	 *
	 * \see lu_device_types.
	 */
	struct list_head			      ldt_linkage;
};

/**
 * Operations on a device type.
 */
struct lu_device_type_operations {
	/**
	 * Allocate new device.
	 */
	struct lu_device *(*ldto_device_alloc)(const struct lu_env *env,
					       struct lu_device_type *t,
					       struct lustre_cfg *lcfg);
	/**
	 * Free device. Dual to
	 * lu_device_type_operations::ldto_device_alloc(). Returns pointer to
	 * the next device in the stack.
	 */
	struct lu_device *(*ldto_device_free)(const struct lu_env *,
					      struct lu_device *);

	/**
	 * Initialize the devices after allocation
	 */
	int  (*ldto_device_init)(const struct lu_env *env,
				 struct lu_device *, const char *,
				 struct lu_device *);
	/**
	 * Finalize device. Dual to
	 * lu_device_type_operations::ldto_device_init(). Returns pointer to
	 * the next device in the stack.
	 */
	struct lu_device *(*ldto_device_fini)(const struct lu_env *env,
					      struct lu_device *);
	/**
	 * Initialize device type. This is called on module load.
	 */
	int  (*ldto_init)(struct lu_device_type *t);
	/**
	 * Finalize device type. Dual to
	 * lu_device_type_operations::ldto_init(). Called on module unload.
	 */
	void (*ldto_fini)(struct lu_device_type *t);
	/**
	 * Called when the first device is created.
	 */
	void (*ldto_start)(struct lu_device_type *t);
	/**
	 * Called when number of devices drops to 0.
	 */
	void (*ldto_stop)(struct lu_device_type *t);
};

static inline int lu_device_is_md(const struct lu_device *d)
{
	return ergo(d != NULL, d->ld_type->ldt_tags & LU_DEVICE_MD);
}

/**
 * Common object attributes.
 */
struct lu_attr {
	/** size in bytes */
	__u64	  la_size;
	/** modification time in seconds since Epoch */
	obd_time       la_mtime;
	/** access time in seconds since Epoch */
	obd_time       la_atime;
	/** change time in seconds since Epoch */
	obd_time       la_ctime;
	/** 512-byte blocks allocated to object */
	__u64	  la_blocks;
	/** permission bits and file type */
	__u32	  la_mode;
	/** owner id */
	__u32	  la_uid;
	/** group id */
	__u32	  la_gid;
	/** object flags */
	__u32	  la_flags;
	/** number of persistent references to this object */
	__u32	  la_nlink;
	/** blk bits of the object*/
	__u32	  la_blkbits;
	/** blk size of the object*/
	__u32	  la_blksize;
	/** real device */
	__u32	  la_rdev;
	/**
	 * valid bits
	 *
	 * \see enum la_valid
	 */
	__u64	  la_valid;
};

/** Bit-mask of valid attributes */
enum la_valid {
	LA_ATIME = 1 << 0,
	LA_MTIME = 1 << 1,
	LA_CTIME = 1 << 2,
	LA_SIZE  = 1 << 3,
	LA_MODE  = 1 << 4,
	LA_UID   = 1 << 5,
	LA_GID   = 1 << 6,
	LA_BLOCKS = 1 << 7,
	LA_TYPE   = 1 << 8,
	LA_FLAGS  = 1 << 9,
	LA_NLINK  = 1 << 10,
	LA_RDEV   = 1 << 11,
	LA_BLKSIZE = 1 << 12,
	LA_KILL_SUID = 1 << 13,
	LA_KILL_SGID = 1 << 14,
};

/**
 * Layer in the layered object.
 */
struct lu_object {
	/**
	 * Header for this object.
	 */
	struct lu_object_header	   *lo_header;
	/**
	 * Device for this layer.
	 */
	struct lu_device		  *lo_dev;
	/**
	 * Operations for this object.
	 */
	const struct lu_object_operations *lo_ops;
	/**
	 * Linkage into list of all layers.
	 */
	struct list_head			 lo_linkage;
	/**
	 * Link to the device, for debugging.
	 */
	struct lu_ref_link                 lo_dev_ref;
};

enum lu_object_header_flags {
	/**
	 * Don't keep this object in cache. Object will be destroyed as soon
	 * as last reference to it is released. This flag cannot be cleared
	 * once set.
	 */
	LU_OBJECT_HEARD_BANSHEE = 0,
	/**
	 * Mark this object has already been taken out of cache.
	 */
	LU_OBJECT_UNHASHED = 1
};

enum lu_object_header_attr {
	LOHA_EXISTS   = 1 << 0,
	LOHA_REMOTE   = 1 << 1,
	/**
	 * UNIX file type is stored in S_IFMT bits.
	 */
	LOHA_FT_START = 001 << 12, /**< S_IFIFO */
	LOHA_FT_END   = 017 << 12, /**< S_IFMT */
};

/**
 * "Compound" object, consisting of multiple layers.
 *
 * Compound object with given fid is unique with given lu_site.
 *
 * Note, that object does *not* necessary correspond to the real object in the
 * persistent storage: object is an anchor for locking and method calling, so
 * it is created for things like not-yet-existing child created by mkdir or
 * create calls. lu_object_operations::loo_exists() can be used to check
 * whether object is backed by persistent storage entity.
 */
struct lu_object_header {
	/**
	 * Object flags from enum lu_object_header_flags. Set and checked
	 * atomically.
	 */
	unsigned long	  loh_flags;
	/**
	 * Object reference count. Protected by lu_site::ls_guard.
	 */
	atomic_t	   loh_ref;
	/**
	 * Fid, uniquely identifying this object.
	 */
	struct lu_fid	  loh_fid;
	/**
	 * Common object attributes, cached for efficiency. From enum
	 * lu_object_header_attr.
	 */
	__u32		  loh_attr;
	/**
	 * Linkage into per-site hash table. Protected by lu_site::ls_guard.
	 */
	struct hlist_node       loh_hash;
	/**
	 * Linkage into per-site LRU list. Protected by lu_site::ls_guard.
	 */
	struct list_head	     loh_lru;
	/**
	 * Linkage into list of layers. Never modified once set (except lately
	 * during object destruction). No locking is necessary.
	 */
	struct list_head	     loh_layers;
	/**
	 * A list of references to this object, for debugging.
	 */
	struct lu_ref	  loh_reference;
};

struct fld;

struct lu_site_bkt_data {
	/**
	 * number of busy object on this bucket
	 */
	long		      lsb_busy;
	/**
	 * LRU list, updated on each access to object. Protected by
	 * bucket lock of lu_site::ls_obj_hash.
	 *
	 * "Cold" end of LRU is lu_site::ls_lru.next. Accessed object are
	 * moved to the lu_site::ls_lru.prev (this is due to the non-existence
	 * of list_for_each_entry_safe_reverse()).
	 */
	struct list_head		lsb_lru;
	/**
	 * Wait-queue signaled when an object in this site is ultimately
	 * destroyed (lu_object_free()). It is used by lu_object_find() to
	 * wait before re-trying when object in the process of destruction is
	 * found in the hash table.
	 *
	 * \see htable_lookup().
	 */
	wait_queue_head_t	       lsb_marche_funebre;
};

enum {
	LU_SS_CREATED	 = 0,
	LU_SS_CACHE_HIT,
	LU_SS_CACHE_MISS,
	LU_SS_CACHE_RACE,
	LU_SS_CACHE_DEATH_RACE,
	LU_SS_LRU_PURGED,
	LU_SS_LAST_STAT
};

/**
 * lu_site is a "compartment" within which objects are unique, and LRU
 * discipline is maintained.
 *
 * lu_site exists so that multiple layered stacks can co-exist in the same
 * address space.
 *
 * lu_site has the same relation to lu_device as lu_object_header to
 * lu_object.
 */
struct lu_site {
	/**
	 * objects hash table
	 */
	struct cfs_hash	       *ls_obj_hash;
	/**
	 * index of bucket on hash table while purging
	 */
	int		       ls_purge_start;
	/**
	 * Top-level device for this stack.
	 */
	struct lu_device	 *ls_top_dev;
	/**
	 * Bottom-level device for this stack
	 */
	struct lu_device	*ls_bottom_dev;
	/**
	 * Linkage into global list of sites.
	 */
	struct list_head		ls_linkage;
	/**
	 * List for lu device for this site, protected
	 * by ls_ld_lock.
	 **/
	struct list_head		ls_ld_linkage;
	spinlock_t		ls_ld_lock;

	/**
	 * lu_site stats
	 */
	struct lprocfs_stats	*ls_stats;
	/**
	 * XXX: a hack! fld has to find md_site via site, remove when possible
	 */
	struct seq_server_site	*ld_seq_site;
};

static inline struct lu_site_bkt_data *
lu_site_bkt_from_fid(struct lu_site *site, struct lu_fid *fid)
{
	struct cfs_hash_bd bd;

	cfs_hash_bd_get(site->ls_obj_hash, fid, &bd);
	return cfs_hash_bd_extra_get(site->ls_obj_hash, &bd);
}

static inline struct seq_server_site *lu_site2seq(const struct lu_site *s)
{
	return s->ld_seq_site;
}

/** \name ctors
 * Constructors/destructors.
 * @{
 */

int  lu_site_init	 (struct lu_site *s, struct lu_device *d);
void lu_site_fini	 (struct lu_site *s);
int  lu_site_init_finish  (struct lu_site *s);
void lu_stack_fini	(const struct lu_env *env, struct lu_device *top);
void lu_device_get	(struct lu_device *d);
void lu_device_put	(struct lu_device *d);
int  lu_device_init       (struct lu_device *d, struct lu_device_type *t);
void lu_device_fini       (struct lu_device *d);
int  lu_object_header_init(struct lu_object_header *h);
void lu_object_header_fini(struct lu_object_header *h);
int  lu_object_init       (struct lu_object *o,
			   struct lu_object_header *h, struct lu_device *d);
void lu_object_fini       (struct lu_object *o);
void lu_object_add_top    (struct lu_object_header *h, struct lu_object *o);
void lu_object_add	(struct lu_object *before, struct lu_object *o);

void lu_dev_add_linkage(struct lu_site *s, struct lu_device *d);
void lu_dev_del_linkage(struct lu_site *s, struct lu_device *d);

/**
 * Helpers to initialize and finalize device types.
 */

int  lu_device_type_init(struct lu_device_type *ldt);
void lu_device_type_fini(struct lu_device_type *ldt);
void lu_types_stop(void);

/** @} ctors */

/** \name caching
 * Caching and reference counting.
 * @{
 */

/**
 * Acquire additional reference to the given object. This function is used to
 * attain additional reference. To acquire initial reference use
 * lu_object_find().
 */
static inline void lu_object_get(struct lu_object *o)
{
	LASSERT(atomic_read(&o->lo_header->loh_ref) > 0);
	atomic_inc(&o->lo_header->loh_ref);
}

/**
 * Return true of object will not be cached after last reference to it is
 * released.
 */
static inline int lu_object_is_dying(const struct lu_object_header *h)
{
	return test_bit(LU_OBJECT_HEARD_BANSHEE, &h->loh_flags);
}

void lu_object_put(const struct lu_env *env, struct lu_object *o);
void lu_object_put_nocache(const struct lu_env *env, struct lu_object *o);
void lu_object_unhash(const struct lu_env *env, struct lu_object *o);

int lu_site_purge(const struct lu_env *env, struct lu_site *s, int nr);

void lu_site_print(const struct lu_env *env, struct lu_site *s, void *cookie,
		   lu_printer_t printer);
struct lu_object *lu_object_find(const struct lu_env *env,
				 struct lu_device *dev, const struct lu_fid *f,
				 const struct lu_object_conf *conf);
struct lu_object *lu_object_find_at(const struct lu_env *env,
				    struct lu_device *dev,
				    const struct lu_fid *f,
				    const struct lu_object_conf *conf);
struct lu_object *lu_object_find_slice(const struct lu_env *env,
				       struct lu_device *dev,
				       const struct lu_fid *f,
				       const struct lu_object_conf *conf);
/** @} caching */

/** \name helpers
 * Helpers.
 * @{
 */

/**
 * First (topmost) sub-object of given compound object
 */
static inline struct lu_object *lu_object_top(struct lu_object_header *h)
{
	LASSERT(!list_empty(&h->loh_layers));
	return container_of0(h->loh_layers.next, struct lu_object, lo_linkage);
}

/**
 * Next sub-object in the layering
 */
static inline struct lu_object *lu_object_next(const struct lu_object *o)
{
	return container_of0(o->lo_linkage.next, struct lu_object, lo_linkage);
}

/**
 * Pointer to the fid of this object.
 */
static inline const struct lu_fid *lu_object_fid(const struct lu_object *o)
{
	return &o->lo_header->loh_fid;
}

/**
 * return device operations vector for this object
 */
static const inline struct lu_device_operations *
lu_object_ops(const struct lu_object *o)
{
	return o->lo_dev->ld_ops;
}

/**
 * Given a compound object, find its slice, corresponding to the device type
 * \a dtype.
 */
struct lu_object *lu_object_locate(struct lu_object_header *h,
				   const struct lu_device_type *dtype);

/**
 * Printer function emitting messages through libcfs_debug_msg().
 */
int lu_cdebug_printer(const struct lu_env *env,
		      void *cookie, const char *format, ...);

/**
 * Print object description followed by a user-supplied message.
 */
#define LU_OBJECT_DEBUG(mask, env, object, format, ...)		   \
do {								      \
	LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, mask, NULL);		  \
									  \
	if (cfs_cdebug_show(mask, DEBUG_SUBSYSTEM)) {		     \
		lu_object_print(env, &msgdata, lu_cdebug_printer, object);\
		CDEBUG(mask, format , ## __VA_ARGS__);		    \
	}								 \
} while (0)

/**
 * Print short object description followed by a user-supplied message.
 */
#define LU_OBJECT_HEADER(mask, env, object, format, ...)		\
do {								    \
	LIBCFS_DEBUG_MSG_DATA_DECL(msgdata, mask, NULL);		\
									\
	if (cfs_cdebug_show(mask, DEBUG_SUBSYSTEM)) {		   \
		lu_object_header_print(env, &msgdata, lu_cdebug_printer,\
				       (object)->lo_header);	    \
		lu_cdebug_printer(env, &msgdata, "\n");		 \
		CDEBUG(mask, format , ## __VA_ARGS__);		  \
	}							       \
} while (0)

void lu_object_print       (const struct lu_env *env, void *cookie,
			    lu_printer_t printer, const struct lu_object *o);
void lu_object_header_print(const struct lu_env *env, void *cookie,
			    lu_printer_t printer,
			    const struct lu_object_header *hdr);

/**
 * Check object consistency.
 */
int lu_object_invariant(const struct lu_object *o);


/**
 * Check whether object exists, no matter on local or remote storage.
 * Note: LOHA_EXISTS will be set once some one created the object,
 * and it does not needs to be committed to storage.
 */
#define lu_object_exists(o) ((o)->lo_header->loh_attr & LOHA_EXISTS)

/**
 * Check whether object on the remote storage.
 */
#define lu_object_remote(o) unlikely((o)->lo_header->loh_attr & LOHA_REMOTE)

static inline int lu_object_assert_exists(const struct lu_object *o)
{
	return lu_object_exists(o);
}

static inline int lu_object_assert_not_exists(const struct lu_object *o)
{
	return !lu_object_exists(o);
}

/**
 * Attr of this object.
 */
static inline __u32 lu_object_attr(const struct lu_object *o)
{
	LASSERT(lu_object_exists(o) != 0);
	return o->lo_header->loh_attr;
}

static inline void lu_object_ref_add(struct lu_object *o,
				     const char *scope,
				     const void *source)
{
	lu_ref_add(&o->lo_header->loh_reference, scope, source);
}

static inline void lu_object_ref_add_at(struct lu_object *o,
					struct lu_ref_link *link,
					const char *scope,
					const void *source)
{
	lu_ref_add_at(&o->lo_header->loh_reference, link, scope, source);
}

static inline void lu_object_ref_del(struct lu_object *o,
				     const char *scope, const void *source)
{
	lu_ref_del(&o->lo_header->loh_reference, scope, source);
}

static inline void lu_object_ref_del_at(struct lu_object *o,
					struct lu_ref_link *link,
					const char *scope, const void *source)
{
	lu_ref_del_at(&o->lo_header->loh_reference, link, scope, source);
}

/** input params, should be filled out by mdt */
struct lu_rdpg {
	/** hash */
	__u64		   rp_hash;
	/** count in bytes */
	unsigned int	    rp_count;
	/** number of pages */
	unsigned int	    rp_npages;
	/** requested attr */
	__u32		   rp_attrs;
	/** pointers to pages */
	struct page	   **rp_pages;
};

enum lu_xattr_flags {
	LU_XATTR_REPLACE = (1 << 0),
	LU_XATTR_CREATE  = (1 << 1)
};

/** @} helpers */

/** \name lu_context
 * @{ */

/** For lu_context health-checks */
enum lu_context_state {
	LCS_INITIALIZED = 1,
	LCS_ENTERED,
	LCS_LEFT,
	LCS_FINALIZED
};

/**
 * lu_context. Execution context for lu_object methods. Currently associated
 * with thread.
 *
 * All lu_object methods, except device and device type methods (called during
 * system initialization and shutdown) are executed "within" some
 * lu_context. This means, that pointer to some "current" lu_context is passed
 * as an argument to all methods.
 *
 * All service ptlrpc threads create lu_context as part of their
 * initialization. It is possible to create "stand-alone" context for other
 * execution environments (like system calls).
 *
 * lu_object methods mainly use lu_context through lu_context_key interface
 * that allows each layer to associate arbitrary pieces of data with each
 * context (see pthread_key_create(3) for similar interface).
 *
 * On a client, lu_context is bound to a thread, see cl_env_get().
 *
 * \see lu_context_key
 */
struct lu_context {
	/**
	 * lu_context is used on the client side too. Yet we don't want to
	 * allocate values of server-side keys for the client contexts and
	 * vice versa.
	 *
	 * To achieve this, set of tags in introduced. Contexts and keys are
	 * marked with tags. Key value are created only for context whose set
	 * of tags has non-empty intersection with one for key. Tags are taken
	 * from enum lu_context_tag.
	 */
	__u32		  lc_tags;
	enum lu_context_state  lc_state;
	/**
	 * Pointer to the home service thread. NULL for other execution
	 * contexts.
	 */
	struct ptlrpc_thread  *lc_thread;
	/**
	 * Pointer to an array with key values. Internal implementation
	 * detail.
	 */
	void		 **lc_value;
	/**
	 * Linkage into a list of all remembered contexts. Only
	 * `non-transient' contexts, i.e., ones created for service threads
	 * are placed here.
	 */
	struct list_head	     lc_remember;
	/**
	 * Version counter used to skip calls to lu_context_refill() when no
	 * keys were registered.
	 */
	unsigned	       lc_version;
	/**
	 * Debugging cookie.
	 */
	unsigned	       lc_cookie;
};

/**
 * lu_context_key interface. Similar to pthread_key.
 */

enum lu_context_tag {
	/**
	 * Thread on md server
	 */
	LCT_MD_THREAD = 1 << 0,
	/**
	 * Thread on dt server
	 */
	LCT_DT_THREAD = 1 << 1,
	/**
	 * Context for transaction handle
	 */
	LCT_TX_HANDLE = 1 << 2,
	/**
	 * Thread on client
	 */
	LCT_CL_THREAD = 1 << 3,
	/**
	 * A per-request session on a server, and a per-system-call session on
	 * a client.
	 */
	LCT_SESSION   = 1 << 4,
	/**
	 * A per-request data on OSP device
	 */
	LCT_OSP_THREAD = 1 << 5,
	/**
	 * MGS device thread
	 */
	LCT_MG_THREAD = 1 << 6,
	/**
	 * Context for local operations
	 */
	LCT_LOCAL = 1 << 7,
	/**
	 * Set when at least one of keys, having values in this context has
	 * non-NULL lu_context_key::lct_exit() method. This is used to
	 * optimize lu_context_exit() call.
	 */
	LCT_HAS_EXIT  = 1 << 28,
	/**
	 * Don't add references for modules creating key values in that context.
	 * This is only for contexts used internally by lu_object framework.
	 */
	LCT_NOREF     = 1 << 29,
	/**
	 * Key is being prepared for retiring, don't create new values for it.
	 */
	LCT_QUIESCENT = 1 << 30,
	/**
	 * Context should be remembered.
	 */
	LCT_REMEMBER  = 1 << 31,
	/**
	 * Contexts usable in cache shrinker thread.
	 */
	LCT_SHRINKER  = LCT_MD_THREAD|LCT_DT_THREAD|LCT_CL_THREAD|LCT_NOREF
};

/**
 * Key. Represents per-context value slot.
 *
 * Keys are usually registered when module owning the key is initialized, and
 * de-registered when module is unloaded. Once key is registered, all new
 * contexts with matching tags, will get key value. "Old" contexts, already
 * initialized at the time of key registration, can be forced to get key value
 * by calling lu_context_refill().
 *
 * Every key value is counted in lu_context_key::lct_used and acquires a
 * reference on an owning module. This means, that all key values have to be
 * destroyed before module can be unloaded. This is usually achieved by
 * stopping threads started by the module, that created contexts in their
 * entry functions. Situation is complicated by the threads shared by multiple
 * modules, like ptlrpcd daemon on a client. To work around this problem,
 * contexts, created in such threads, are `remembered' (see
 * LCT_REMEMBER)---i.e., added into a global list. When module is preparing
 * for unloading it does the following:
 *
 *     - marks its keys as `quiescent' (lu_context_tag::LCT_QUIESCENT)
 *       preventing new key values from being allocated in the new contexts,
 *       and
 *
 *     - scans a list of remembered contexts, destroying values of module
 *       keys, thus releasing references to the module.
 *
 * This is done by lu_context_key_quiesce(). If module is re-activated
 * before key has been de-registered, lu_context_key_revive() call clears
 * `quiescent' marker.
 *
 * lu_context code doesn't provide any internal synchronization for these
 * activities---it's assumed that startup (including threads start-up) and
 * shutdown are serialized by some external means.
 *
 * \see lu_context
 */
struct lu_context_key {
	/**
	 * Set of tags for which values of this key are to be instantiated.
	 */
	__u32 lct_tags;
	/**
	 * Value constructor. This is called when new value is created for a
	 * context. Returns pointer to new value of error pointer.
	 */
	void  *(*lct_init)(const struct lu_context *ctx,
			   struct lu_context_key *key);
	/**
	 * Value destructor. Called when context with previously allocated
	 * value of this slot is destroyed. \a data is a value that was returned
	 * by a matching call to lu_context_key::lct_init().
	 */
	void   (*lct_fini)(const struct lu_context *ctx,
			   struct lu_context_key *key, void *data);
	/**
	 * Optional method called on lu_context_exit() for all allocated
	 * keys. Can be used by debugging code checking that locks are
	 * released, etc.
	 */
	void   (*lct_exit)(const struct lu_context *ctx,
			   struct lu_context_key *key, void *data);
	/**
	 * Internal implementation detail: index within lu_context::lc_value[]
	 * reserved for this key.
	 */
	int      lct_index;
	/**
	 * Internal implementation detail: number of values created for this
	 * key.
	 */
	atomic_t lct_used;
	/**
	 * Internal implementation detail: module for this key.
	 */
	struct module *lct_owner;
	/**
	 * References to this key. For debugging.
	 */
	struct lu_ref  lct_reference;
};

#define LU_KEY_INIT(mod, type)				    \
	static void* mod##_key_init(const struct lu_context *ctx, \
				    struct lu_context_key *key)   \
	{							 \
		type *value;				      \
								  \
		CLASSERT(PAGE_CACHE_SIZE >= sizeof (*value));       \
								  \
		OBD_ALLOC_PTR(value);			     \
		if (value == NULL)				\
			value = ERR_PTR(-ENOMEM);		 \
								  \
		return value;				     \
	}							 \
	struct __##mod##__dummy_init {;} /* semicolon catcher */

#define LU_KEY_FINI(mod, type)					      \
	static void mod##_key_fini(const struct lu_context *ctx,	    \
				    struct lu_context_key *key, void* data) \
	{								   \
		type *info = data;					  \
									    \
		OBD_FREE_PTR(info);					 \
	}								   \
	struct __##mod##__dummy_fini {;} /* semicolon catcher */

#define LU_KEY_INIT_FINI(mod, type)   \
	LU_KEY_INIT(mod,type);	\
	LU_KEY_FINI(mod,type)

#define LU_CONTEXT_KEY_DEFINE(mod, tags)		\
	struct lu_context_key mod##_thread_key = {      \
		.lct_tags = tags,		       \
		.lct_init = mod##_key_init,	     \
		.lct_fini = mod##_key_fini	      \
	}

#define LU_CONTEXT_KEY_INIT(key)			\
do {						    \
	(key)->lct_owner = THIS_MODULE;		 \
} while (0)

int   lu_context_key_register(struct lu_context_key *key);
void  lu_context_key_degister(struct lu_context_key *key);
void *lu_context_key_get     (const struct lu_context *ctx,
			       const struct lu_context_key *key);
void  lu_context_key_quiesce (struct lu_context_key *key);
void  lu_context_key_revive  (struct lu_context_key *key);


/*
 * LU_KEY_INIT_GENERIC() has to be a macro to correctly determine an
 * owning module.
 */

#define LU_KEY_INIT_GENERIC(mod)					\
	static void mod##_key_init_generic(struct lu_context_key *k, ...) \
	{							       \
		struct lu_context_key *key = k;			 \
		va_list args;					   \
									\
		va_start(args, k);				      \
		do {						    \
			LU_CONTEXT_KEY_INIT(key);		       \
			key = va_arg(args, struct lu_context_key *);    \
		} while (key != NULL);				  \
		va_end(args);					   \
	}

#define LU_TYPE_INIT(mod, ...)					  \
	LU_KEY_INIT_GENERIC(mod)					\
	static int mod##_type_init(struct lu_device_type *t)	    \
	{							       \
		mod##_key_init_generic(__VA_ARGS__, NULL);	      \
		return lu_context_key_register_many(__VA_ARGS__, NULL); \
	}							       \
	struct __##mod##_dummy_type_init {;}

#define LU_TYPE_FINI(mod, ...)					  \
	static void mod##_type_fini(struct lu_device_type *t)	   \
	{							       \
		lu_context_key_degister_many(__VA_ARGS__, NULL);	\
	}							       \
	struct __##mod##_dummy_type_fini {;}

#define LU_TYPE_START(mod, ...)				 \
	static void mod##_type_start(struct lu_device_type *t)  \
	{						       \
		lu_context_key_revive_many(__VA_ARGS__, NULL);  \
	}						       \
	struct __##mod##_dummy_type_start {;}

#define LU_TYPE_STOP(mod, ...)				  \
	static void mod##_type_stop(struct lu_device_type *t)   \
	{						       \
		lu_context_key_quiesce_many(__VA_ARGS__, NULL); \
	}						       \
	struct __##mod##_dummy_type_stop {;}



#define LU_TYPE_INIT_FINI(mod, ...)	     \
	LU_TYPE_INIT(mod, __VA_ARGS__);	 \
	LU_TYPE_FINI(mod, __VA_ARGS__);	 \
	LU_TYPE_START(mod, __VA_ARGS__);	\
	LU_TYPE_STOP(mod, __VA_ARGS__)

int   lu_context_init  (struct lu_context *ctx, __u32 tags);
void  lu_context_fini  (struct lu_context *ctx);
void  lu_context_enter (struct lu_context *ctx);
void  lu_context_exit  (struct lu_context *ctx);
int   lu_context_refill(struct lu_context *ctx);

/*
 * Helper functions to operate on multiple keys. These are used by the default
 * device type operations, defined by LU_TYPE_INIT_FINI().
 */

int  lu_context_key_register_many(struct lu_context_key *k, ...);
void lu_context_key_degister_many(struct lu_context_key *k, ...);
void lu_context_key_revive_many  (struct lu_context_key *k, ...);
void lu_context_key_quiesce_many (struct lu_context_key *k, ...);

/*
 * update/clear ctx/ses tags.
 */
void lu_context_tags_update(__u32 tags);
void lu_context_tags_clear(__u32 tags);
void lu_session_tags_update(__u32 tags);
void lu_session_tags_clear(__u32 tags);

/**
 * Environment.
 */
struct lu_env {
	/**
	 * "Local" context, used to store data instead of stack.
	 */
	struct lu_context  le_ctx;
	/**
	 * "Session" context for per-request data.
	 */
	struct lu_context *le_ses;
};

int  lu_env_init  (struct lu_env *env, __u32 tags);
void lu_env_fini  (struct lu_env *env);
int  lu_env_refill(struct lu_env *env);
int  lu_env_refill_by_tags(struct lu_env *env, __u32 ctags, __u32 stags);

/** @} lu_context */

/**
 * Output site statistical counters into a buffer. Suitable for
 * ll_rd_*()-style functions.
 */
int lu_site_stats_print(const struct lu_site *s, struct seq_file *m);

/**
 * Common name structure to be passed around for various name related methods.
 */
struct lu_name {
	const char    *ln_name;
	int	    ln_namelen;
};

/**
 * Common buffer structure to be passed around for various xattr_{s,g}et()
 * methods.
 */
struct lu_buf {
	void   *lb_buf;
	ssize_t lb_len;
};

#define DLUBUF "(%p %zu)"
#define PLUBUF(buf) (buf)->lb_buf, (buf)->lb_len
/**
 * One-time initializers, called at obdclass module initialization, not
 * exported.
 */

/**
 * Initialization of global lu_* data.
 */
int lu_global_init(void);

/**
 * Dual to lu_global_init().
 */
void lu_global_fini(void);

struct lu_kmem_descr {
	struct kmem_cache **ckd_cache;
	const char       *ckd_name;
	const size_t      ckd_size;
};

int  lu_kmem_init(struct lu_kmem_descr *caches);
void lu_kmem_fini(struct lu_kmem_descr *caches);

void lu_object_assign_fid(const struct lu_env *env, struct lu_object *o,
			  const struct lu_fid *fid);
struct lu_object *lu_object_anon(const struct lu_env *env,
				 struct lu_device *dev,
				 const struct lu_object_conf *conf);

/** null buffer */
extern struct lu_buf LU_BUF_NULL;

void lu_buf_free(struct lu_buf *buf);
void lu_buf_alloc(struct lu_buf *buf, int size);
void lu_buf_realloc(struct lu_buf *buf, int size);

int lu_buf_check_and_grow(struct lu_buf *buf, int len);
struct lu_buf *lu_buf_check_and_alloc(struct lu_buf *buf, int len);

/** @} lu */
#endif /* __LUSTRE_LU_OBJECT_H */
