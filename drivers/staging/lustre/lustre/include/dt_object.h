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

#ifndef __LUSTRE_DT_OBJECT_H
#define __LUSTRE_DT_OBJECT_H

/** \defgroup dt dt
 * Sub-class of lu_object with methods common for "data" objects in OST stack.
 *
 * Data objects behave like regular files: you can read/write them, get and
 * set their attributes. Implementation of dt interface is supposed to
 * implement some form of garbage collection, normally reference counting
 * (nlink) based one.
 *
 * Examples: osd (lustre/osd) is an implementation of dt interface.
 * @{
 */


/*
 * super-class definitions.
 */
#include "lu_object.h"

#include "../../include/linux/libcfs/libcfs.h"

struct seq_file;
struct proc_dir_entry;
struct lustre_cfg;

struct thandle;
struct dt_device;
struct dt_object;
struct dt_index_features;
struct niobuf_local;
struct niobuf_remote;
struct ldlm_enqueue_info;

typedef enum {
	MNTOPT_USERXATTR	= 0x00000001,
	MNTOPT_ACL	      = 0x00000002,
} mntopt_t;

struct dt_device_param {
	unsigned	   ddp_max_name_len;
	unsigned	   ddp_max_nlink;
	unsigned	   ddp_block_shift;
	mntopt_t	   ddp_mntopts;
	unsigned	   ddp_max_ea_size;
	void	      *ddp_mnt; /* XXX: old code can retrieve mnt -bzzz */
	int		ddp_mount_type;
	unsigned long long ddp_maxbytes;
	/* percentage of available space to reserve for grant error margin */
	int		ddp_grant_reserved;
	/* per-inode space consumption */
	short	      ddp_inodespace;
	/* per-fragment grant overhead to be used by client for grant
	 * calculation */
	int		ddp_grant_frag;
};

/**
 * Per-transaction commit callback function
 */
struct dt_txn_commit_cb;
typedef void (*dt_cb_t)(struct lu_env *env, struct thandle *th,
			struct dt_txn_commit_cb *cb, int err);
/**
 * Special per-transaction callback for cases when just commit callback
 * is needed and per-device callback are not convenient to use
 */
#define TRANS_COMMIT_CB_MAGIC	0xa0a00a0a
#define MAX_COMMIT_CB_STR_LEN	32

struct dt_txn_commit_cb {
	struct list_head	dcb_linkage;
	dt_cb_t		dcb_func;
	__u32		dcb_magic;
	char		dcb_name[MAX_COMMIT_CB_STR_LEN];
};

/**
 * Operations on dt device.
 */
struct dt_device_operations {
	/**
	 * Return device-wide statistics.
	 */
	int   (*dt_statfs)(const struct lu_env *env,
			   struct dt_device *dev, struct obd_statfs *osfs);
	/**
	 * Create transaction, described by \a param.
	 */
	struct thandle *(*dt_trans_create)(const struct lu_env *env,
					   struct dt_device *dev);
	/**
	 * Start transaction, described by \a param.
	 */
	int   (*dt_trans_start)(const struct lu_env *env,
				struct dt_device *dev, struct thandle *th);
	/**
	 * Finish previously started transaction.
	 */
	int   (*dt_trans_stop)(const struct lu_env *env,
			       struct thandle *th);
	/**
	 * Add commit callback to the transaction.
	 */
	int   (*dt_trans_cb_add)(struct thandle *th,
				 struct dt_txn_commit_cb *dcb);
	/**
	 * Return fid of root index object.
	 */
	int   (*dt_root_get)(const struct lu_env *env,
			     struct dt_device *dev, struct lu_fid *f);
	/**
	 * Return device configuration data.
	 */
	void  (*dt_conf_get)(const struct lu_env *env,
			     const struct dt_device *dev,
			     struct dt_device_param *param);
	/**
	 *  handling device state, mostly for tests
	 */
	int   (*dt_sync)(const struct lu_env *env, struct dt_device *dev);
	int   (*dt_ro)(const struct lu_env *env, struct dt_device *dev);
	/**
	  * Start a transaction commit asynchronously
	  *
	  * \param env environment
	  * \param dev dt_device to start commit on
	  *
	  * \return 0 success, negative value if error
	  */
	 int   (*dt_commit_async)(const struct lu_env *env,
				  struct dt_device *dev);
	/**
	 * Initialize capability context.
	 */
	int   (*dt_init_capa_ctxt)(const struct lu_env *env,
				   struct dt_device *dev,
				   int mode, unsigned long timeout,
				   __u32 alg, struct lustre_capa_key *keys);
};

struct dt_index_features {
	/** required feature flags from enum dt_index_flags */
	__u32 dif_flags;
	/** minimal required key size */
	size_t dif_keysize_min;
	/** maximal required key size, 0 if no limit */
	size_t dif_keysize_max;
	/** minimal required record size */
	size_t dif_recsize_min;
	/** maximal required record size, 0 if no limit */
	size_t dif_recsize_max;
	/** pointer size for record */
	size_t dif_ptrsize;
};

enum dt_index_flags {
	/** index supports variable sized keys */
	DT_IND_VARKEY = 1 << 0,
	/** index supports variable sized records */
	DT_IND_VARREC = 1 << 1,
	/** index can be modified */
	DT_IND_UPDATE = 1 << 2,
	/** index supports records with non-unique (duplicate) keys */
	DT_IND_NONUNQ = 1 << 3,
	/**
	 * index support fixed-size keys sorted with natural numerical way
	 * and is able to return left-side value if no exact value found
	 */
	DT_IND_RANGE = 1 << 4,
};

/**
 * Features, required from index to support file system directories (mapping
 * names to fids).
 */
extern const struct dt_index_features dt_directory_features;
extern const struct dt_index_features dt_otable_features;
extern const struct dt_index_features dt_lfsck_features;

/* index features supported by the accounting objects */
extern const struct dt_index_features dt_acct_features;

/* index features supported by the quota global indexes */
extern const struct dt_index_features dt_quota_glb_features;

/* index features supported by the quota slave indexes */
extern const struct dt_index_features dt_quota_slv_features;

/**
 * This is a general purpose dt allocation hint.
 * It now contains the parent object.
 * It can contain any allocation hint in the future.
 */
struct dt_allocation_hint {
	struct dt_object	   *dah_parent;
	__u32		       dah_mode;
};

/**
 * object type specifier.
 */

enum dt_format_type {
	DFT_REGULAR,
	DFT_DIR,
	/** for mknod */
	DFT_NODE,
	/** for special index */
	DFT_INDEX,
	/** for symbolic link */
	DFT_SYM,
};

/**
 * object format specifier.
 */
struct dt_object_format {
	/** type for dt object */
	enum dt_format_type dof_type;
	union {
		struct dof_regular {
			int striped;
		} dof_reg;
		struct dof_dir {
		} dof_dir;
		struct dof_node {
		} dof_node;
		/**
		 * special index need feature as parameter to create
		 * special idx
		 */
		struct dof_index {
			const struct dt_index_features *di_feat;
		} dof_idx;
	} u;
};

enum dt_format_type dt_mode_to_dft(__u32 mode);

typedef __u64 dt_obj_version_t;

/**
 * Per-dt-object operations.
 */
struct dt_object_operations {
	void  (*do_read_lock)(const struct lu_env *env,
			      struct dt_object *dt, unsigned role);
	void  (*do_write_lock)(const struct lu_env *env,
			       struct dt_object *dt, unsigned role);
	void  (*do_read_unlock)(const struct lu_env *env,
				struct dt_object *dt);
	void  (*do_write_unlock)(const struct lu_env *env,
				 struct dt_object *dt);
	int  (*do_write_locked)(const struct lu_env *env,
				struct dt_object *dt);
	/**
	 * Note: following ->do_{x,}attr_{set,get}() operations are very
	 * similar to ->moo_{x,}attr_{set,get}() operations in struct
	 * md_object_operations (see md_object.h). These operations are not in
	 * lu_object_operations, because ->do_{x,}attr_set() versions take
	 * transaction handle as an argument (this transaction is started by
	 * caller). We might factor ->do_{x,}attr_get() into
	 * lu_object_operations, but that would break existing symmetry.
	 */

	/**
	 * Return standard attributes.
	 *
	 * precondition: lu_object_exists(&dt->do_lu);
	 */
	int   (*do_attr_get)(const struct lu_env *env,
			     struct dt_object *dt, struct lu_attr *attr,
			     struct lustre_capa *capa);
	/**
	 * Set standard attributes.
	 *
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_declare_attr_set)(const struct lu_env *env,
				     struct dt_object *dt,
				     const struct lu_attr *attr,
				     struct thandle *handle);
	int   (*do_attr_set)(const struct lu_env *env,
			     struct dt_object *dt,
			     const struct lu_attr *attr,
			     struct thandle *handle,
			     struct lustre_capa *capa);
	/**
	 * Return a value of an extended attribute.
	 *
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_xattr_get)(const struct lu_env *env, struct dt_object *dt,
			      struct lu_buf *buf, const char *name,
			      struct lustre_capa *capa);
	/**
	 * Set value of an extended attribute.
	 *
	 * \a fl - flags from enum lu_xattr_flags
	 *
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_declare_xattr_set)(const struct lu_env *env,
				      struct dt_object *dt,
				      const struct lu_buf *buf,
				      const char *name, int fl,
				      struct thandle *handle);
	int   (*do_xattr_set)(const struct lu_env *env,
			      struct dt_object *dt, const struct lu_buf *buf,
			      const char *name, int fl, struct thandle *handle,
			      struct lustre_capa *capa);
	/**
	 * Delete existing extended attribute.
	 *
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_declare_xattr_del)(const struct lu_env *env,
				      struct dt_object *dt,
				      const char *name, struct thandle *handle);
	int   (*do_xattr_del)(const struct lu_env *env,
			      struct dt_object *dt,
			      const char *name, struct thandle *handle,
			      struct lustre_capa *capa);
	/**
	 * Place list of existing extended attributes into \a buf (which has
	 * length len).
	 *
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_xattr_list)(const struct lu_env *env,
			       struct dt_object *dt, struct lu_buf *buf,
			       struct lustre_capa *capa);
	/**
	 * Init allocation hint using parent object and child mode.
	 * (1) The \a parent might be NULL if this is a partial creation for
	 *     remote object.
	 * (2) The type of child is in \a child_mode.
	 * (3) The result hint is stored in \a ah;
	 */
	void  (*do_ah_init)(const struct lu_env *env,
			    struct dt_allocation_hint *ah,
			    struct dt_object *parent,
			    struct dt_object *child,
			    umode_t child_mode);
	/**
	 * Create new object on this device.
	 *
	 * precondition: !dt_object_exists(dt);
	 * postcondition: ergo(result == 0, dt_object_exists(dt));
	 */
	int   (*do_declare_create)(const struct lu_env *env,
				   struct dt_object *dt,
				   struct lu_attr *attr,
				   struct dt_allocation_hint *hint,
				   struct dt_object_format *dof,
				   struct thandle *th);
	int   (*do_create)(const struct lu_env *env, struct dt_object *dt,
			   struct lu_attr *attr,
			   struct dt_allocation_hint *hint,
			   struct dt_object_format *dof,
			   struct thandle *th);

	/**
	  Destroy object on this device
	 * precondition: !dt_object_exists(dt);
	 * postcondition: ergo(result == 0, dt_object_exists(dt));
	 */
	int   (*do_declare_destroy)(const struct lu_env *env,
				    struct dt_object *dt,
				    struct thandle *th);
	int   (*do_destroy)(const struct lu_env *env, struct dt_object *dt,
			    struct thandle *th);

	/**
	 * Announce that this object is going to be used as an index. This
	 * operation check that object supports indexing operations and
	 * installs appropriate dt_index_operations vector on success.
	 *
	 * Also probes for features. Operation is successful if all required
	 * features are supported.
	 */
	int   (*do_index_try)(const struct lu_env *env,
			      struct dt_object *dt,
			      const struct dt_index_features *feat);
	/**
	 * Add nlink of the object
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_declare_ref_add)(const struct lu_env *env,
				    struct dt_object *dt, struct thandle *th);
	int   (*do_ref_add)(const struct lu_env *env,
			    struct dt_object *dt, struct thandle *th);
	/**
	 * Del nlink of the object
	 * precondition: dt_object_exists(dt);
	 */
	int   (*do_declare_ref_del)(const struct lu_env *env,
				    struct dt_object *dt, struct thandle *th);
	int   (*do_ref_del)(const struct lu_env *env,
			    struct dt_object *dt, struct thandle *th);

	struct obd_capa *(*do_capa_get)(const struct lu_env *env,
					struct dt_object *dt,
					struct lustre_capa *old,
					__u64 opc);
	int (*do_object_sync)(const struct lu_env *env, struct dt_object *obj,
			      __u64 start, __u64 end);
	/**
	 * Get object info of next level. Currently, only get inode from osd.
	 * This is only used by quota b=16542
	 * precondition: dt_object_exists(dt);
	 */
	int (*do_data_get)(const struct lu_env *env, struct dt_object *dt,
			   void **data);

	/**
	 * Lock object.
	 */
	int (*do_object_lock)(const struct lu_env *env, struct dt_object *dt,
			      struct lustre_handle *lh,
			      struct ldlm_enqueue_info *einfo,
			      void *policy);
};

/**
 * Per-dt-object operations on "file body".
 */
struct dt_body_operations {
	/**
	 * precondition: dt_object_exists(dt);
	 */
	ssize_t (*dbo_read)(const struct lu_env *env, struct dt_object *dt,
			    struct lu_buf *buf, loff_t *pos,
			    struct lustre_capa *capa);
	/**
	 * precondition: dt_object_exists(dt);
	 */
	ssize_t (*dbo_declare_write)(const struct lu_env *env,
				     struct dt_object *dt,
				     const loff_t size, loff_t pos,
				     struct thandle *handle);
	ssize_t (*dbo_write)(const struct lu_env *env, struct dt_object *dt,
			     const struct lu_buf *buf, loff_t *pos,
			     struct thandle *handle, struct lustre_capa *capa,
			     int ignore_quota);
	/*
	 * methods for zero-copy IO
	 */

	/*
	 * precondition: dt_object_exists(dt);
	 * returns:
	 * < 0 - error code
	 * = 0 - illegal
	 * > 0 - number of local buffers prepared
	 */
	int (*dbo_bufs_get)(const struct lu_env *env, struct dt_object *dt,
			    loff_t pos, ssize_t len, struct niobuf_local *lb,
			    int rw, struct lustre_capa *capa);
	/*
	 * precondition: dt_object_exists(dt);
	 */
	int (*dbo_bufs_put)(const struct lu_env *env, struct dt_object *dt,
			    struct niobuf_local *lb, int nr);
	/*
	 * precondition: dt_object_exists(dt);
	 */
	int (*dbo_write_prep)(const struct lu_env *env, struct dt_object *dt,
			      struct niobuf_local *lb, int nr);
	/*
	 * precondition: dt_object_exists(dt);
	 */
	int (*dbo_declare_write_commit)(const struct lu_env *env,
					struct dt_object *dt,
					struct niobuf_local *,
					int, struct thandle *);
	/*
	 * precondition: dt_object_exists(dt);
	 */
	int (*dbo_write_commit)(const struct lu_env *env, struct dt_object *dt,
				struct niobuf_local *, int, struct thandle *);
	/*
	 * precondition: dt_object_exists(dt);
	 */
	int (*dbo_read_prep)(const struct lu_env *env, struct dt_object *dt,
			     struct niobuf_local *lnb, int nr);
	int (*dbo_fiemap_get)(const struct lu_env *env, struct dt_object *dt,
			      struct ll_user_fiemap *fm);
	/**
	 * Punch object's content
	 * precondition: regular object, not index
	 */
	int   (*dbo_declare_punch)(const struct lu_env *, struct dt_object *,
				  __u64, __u64, struct thandle *th);
	int   (*dbo_punch)(const struct lu_env *env, struct dt_object *dt,
			  __u64 start, __u64 end, struct thandle *th,
			  struct lustre_capa *capa);
};

/**
 * Incomplete type of index record.
 */
struct dt_rec;

/**
 * Incomplete type of index key.
 */
struct dt_key;

/**
 * Incomplete type of dt iterator.
 */
struct dt_it;

/**
 * Per-dt-object operations on object as index.
 */
struct dt_index_operations {
	/**
	 * precondition: dt_object_exists(dt);
	 */
	int (*dio_lookup)(const struct lu_env *env, struct dt_object *dt,
			  struct dt_rec *rec, const struct dt_key *key,
			  struct lustre_capa *capa);
	/**
	 * precondition: dt_object_exists(dt);
	 */
	int (*dio_declare_insert)(const struct lu_env *env,
				  struct dt_object *dt,
				  const struct dt_rec *rec,
				  const struct dt_key *key,
				  struct thandle *handle);
	int (*dio_insert)(const struct lu_env *env, struct dt_object *dt,
			  const struct dt_rec *rec, const struct dt_key *key,
			  struct thandle *handle, struct lustre_capa *capa,
			  int ignore_quota);
	/**
	 * precondition: dt_object_exists(dt);
	 */
	int (*dio_declare_delete)(const struct lu_env *env,
				  struct dt_object *dt,
				  const struct dt_key *key,
				  struct thandle *handle);
	int (*dio_delete)(const struct lu_env *env, struct dt_object *dt,
			  const struct dt_key *key, struct thandle *handle,
			  struct lustre_capa *capa);
	/**
	 * Iterator interface
	 */
	struct dt_it_ops {
		/**
		 * Allocate and initialize new iterator.
		 *
		 * precondition: dt_object_exists(dt);
		 */
		struct dt_it *(*init)(const struct lu_env *env,
				      struct dt_object *dt,
				      __u32 attr,
				      struct lustre_capa *capa);
		void	  (*fini)(const struct lu_env *env,
				      struct dt_it *di);
		int	    (*get)(const struct lu_env *env,
				      struct dt_it *di,
				      const struct dt_key *key);
		void	   (*put)(const struct lu_env *env,
				      struct dt_it *di);
		int	   (*next)(const struct lu_env *env,
				      struct dt_it *di);
		struct dt_key *(*key)(const struct lu_env *env,
				      const struct dt_it *di);
		int       (*key_size)(const struct lu_env *env,
				      const struct dt_it *di);
		int	    (*rec)(const struct lu_env *env,
				      const struct dt_it *di,
				      struct dt_rec *rec,
				      __u32 attr);
		__u64	(*store)(const struct lu_env *env,
				      const struct dt_it *di);
		int	   (*load)(const struct lu_env *env,
				      const struct dt_it *di, __u64 hash);
		int	(*key_rec)(const struct lu_env *env,
				      const struct dt_it *di, void* key_rec);
	} dio_it;
};

enum dt_otable_it_valid {
	DOIV_ERROR_HANDLE	= 0x0001,
};

enum dt_otable_it_flags {
	/* Exit when fail. */
	DOIF_FAILOUT	= 0x0001,

	/* Reset iteration position to the device beginning. */
	DOIF_RESET	= 0x0002,

	/* There is up layer component uses the iteration. */
	DOIF_OUTUSED	= 0x0004,
};

/* otable based iteration needs to use the common DT interation APIs.
 * To initialize the iteration, it needs call dio_it::init() firstly.
 * Here is how the otable based iteration should prepare arguments to
 * call dt_it_ops::init().
 *
 * For otable based iteration, the 32-bits 'attr' for dt_it_ops::init()
 * is composed of two parts:
 * low 16-bits is for valid bits, high 16-bits is for flags bits. */
#define DT_OTABLE_IT_FLAGS_SHIFT	16
#define DT_OTABLE_IT_FLAGS_MASK 	0xffff0000

struct dt_device {
	struct lu_device		   dd_lu_dev;
	const struct dt_device_operations *dd_ops;

	/**
	 * List of dt_txn_callback (see below). This is not protected in any
	 * way, because callbacks are supposed to be added/deleted only during
	 * single-threaded start-up shut-down procedures.
	 */
	struct list_head			 dd_txn_callbacks;
};

int  dt_device_init(struct dt_device *dev, struct lu_device_type *t);
void dt_device_fini(struct dt_device *dev);

static inline int lu_device_is_dt(const struct lu_device *d)
{
	return ergo(d != NULL, d->ld_type->ldt_tags & LU_DEVICE_DT);
}

static inline struct dt_device * lu2dt_dev(struct lu_device *l)
{
	LASSERT(lu_device_is_dt(l));
	return container_of0(l, struct dt_device, dd_lu_dev);
}

struct dt_object {
	struct lu_object		   do_lu;
	const struct dt_object_operations *do_ops;
	const struct dt_body_operations   *do_body_ops;
	const struct dt_index_operations  *do_index_ops;
};

/*
 * In-core representation of per-device local object OID storage
 */
struct local_oid_storage {
	/* all initialized llog systems on this node linked by this */
	struct list_head	  los_list;

	/* how many handle's reference this los has */
	atomic_t	  los_refcount;
	struct dt_device *los_dev;
	struct dt_object *los_obj;

	/* data used to generate new fids */
	struct mutex	  los_id_lock;
	__u64		  los_seq;
	__u32		  los_last_oid;
};

static inline struct dt_object *lu2dt(struct lu_object *l)
{
	LASSERT(l == NULL || IS_ERR(l) || lu_device_is_dt(l->lo_dev));
	return container_of0(l, struct dt_object, do_lu);
}

int  dt_object_init(struct dt_object *obj,
		    struct lu_object_header *h, struct lu_device *d);

void dt_object_fini(struct dt_object *obj);

static inline int dt_object_exists(const struct dt_object *dt)
{
	return lu_object_exists(&dt->do_lu);
}

static inline int dt_object_remote(const struct dt_object *dt)
{
	return lu_object_remote(&dt->do_lu);
}

static inline struct dt_object *lu2dt_obj(struct lu_object *o)
{
	LASSERT(ergo(o != NULL, lu_device_is_dt(o->lo_dev)));
	return container_of0(o, struct dt_object, do_lu);
}

/**
 * This is the general purpose transaction handle.
 * 1. Transaction Life Cycle
 *      This transaction handle is allocated upon starting a new transaction,
 *      and deallocated after this transaction is committed.
 * 2. Transaction Nesting
 *      We do _NOT_ support nested transaction. So, every thread should only
 *      have one active transaction, and a transaction only belongs to one
 *      thread. Due to this, transaction handle need no reference count.
 * 3. Transaction & dt_object locking
 *      dt_object locks should be taken inside transaction.
 * 4. Transaction & RPC
 *      No RPC request should be issued inside transaction.
 */
struct thandle {
	/** the dt device on which the transactions are executed */
	struct dt_device *th_dev;

	/** context for this transaction, tag is LCT_TX_HANDLE */
	struct lu_context th_ctx;

	/** additional tags (layers can add in declare) */
	__u32	     th_tags;

	/** the last operation result in this transaction.
	 * this value is used in recovery */
	__s32	     th_result;

	/** whether we need sync commit */
	unsigned int		th_sync:1;

	/* local transation, no need to inform other layers */
	unsigned int		th_local:1;

	/* In DNE, one transaction can be disassemblied into
	 * updates on several different MDTs, and these updates
	 * will be attached to th_remote_update_list per target.
	 * Only single thread will access the list, no need lock
	 */
	struct list_head		th_remote_update_list;
	struct update_request	*th_current_request;
};

/**
 * Transaction call-backs.
 *
 * These are invoked by osd (or underlying transaction engine) when
 * transaction changes state.
 *
 * Call-backs are used by upper layers to modify transaction parameters and to
 * perform some actions on for each transaction state transition. Typical
 * example is mdt registering call-back to write into last-received file
 * before each transaction commit.
 */
struct dt_txn_callback {
	int (*dtc_txn_start)(const struct lu_env *env,
			     struct thandle *txn, void *cookie);
	int (*dtc_txn_stop)(const struct lu_env *env,
			    struct thandle *txn, void *cookie);
	void (*dtc_txn_commit)(struct thandle *txn, void *cookie);
	void		*dtc_cookie;
	__u32		dtc_tag;
	struct list_head	   dtc_linkage;
};

void dt_txn_callback_add(struct dt_device *dev, struct dt_txn_callback *cb);
void dt_txn_callback_del(struct dt_device *dev, struct dt_txn_callback *cb);

int dt_txn_hook_start(const struct lu_env *env,
		      struct dt_device *dev, struct thandle *txn);
int dt_txn_hook_stop(const struct lu_env *env, struct thandle *txn);
void dt_txn_hook_commit(struct thandle *txn);

int dt_try_as_dir(const struct lu_env *env, struct dt_object *obj);

/**
 * Callback function used for parsing path.
 * \see llo_store_resolve
 */
typedef int (*dt_entry_func_t)(const struct lu_env *env,
			    const char *name,
			    void *pvt);

#define DT_MAX_PATH 1024

int dt_path_parser(const struct lu_env *env,
		   char *local, dt_entry_func_t entry_func,
		   void *data);

struct dt_object *
dt_store_resolve(const struct lu_env *env, struct dt_device *dt,
		 const char *path, struct lu_fid *fid);

struct dt_object *dt_store_open(const struct lu_env *env,
				struct dt_device *dt,
				const char *dirname,
				const char *filename,
				struct lu_fid *fid);

struct dt_object *dt_find_or_create(const struct lu_env *env,
				    struct dt_device *dt,
				    const struct lu_fid *fid,
				    struct dt_object_format *dof,
				    struct lu_attr *attr);

struct dt_object *dt_locate_at(const struct lu_env *env,
			       struct dt_device *dev,
			       const struct lu_fid *fid,
			       struct lu_device *top_dev);
static inline struct dt_object *
dt_locate(const struct lu_env *env, struct dt_device *dev,
	  const struct lu_fid *fid)
{
	return dt_locate_at(env, dev, fid, dev->dd_lu_dev.ld_site->ls_top_dev);
}


int local_oid_storage_init(const struct lu_env *env, struct dt_device *dev,
			   const struct lu_fid *first_fid,
			   struct local_oid_storage **los);
void local_oid_storage_fini(const struct lu_env *env,
			    struct local_oid_storage *los);
int local_object_fid_generate(const struct lu_env *env,
			      struct local_oid_storage *los,
			      struct lu_fid *fid);
int local_object_declare_create(const struct lu_env *env,
				struct local_oid_storage *los,
				struct dt_object *o,
				struct lu_attr *attr,
				struct dt_object_format *dof,
				struct thandle *th);
int local_object_create(const struct lu_env *env,
			struct local_oid_storage *los,
			struct dt_object *o,
			struct lu_attr *attr, struct dt_object_format *dof,
			struct thandle *th);
struct dt_object *local_file_find_or_create(const struct lu_env *env,
					    struct local_oid_storage *los,
					    struct dt_object *parent,
					    const char *name, __u32 mode);
struct dt_object *local_file_find_or_create_with_fid(const struct lu_env *env,
						     struct dt_device *dt,
						     const struct lu_fid *fid,
						     struct dt_object *parent,
						     const char *name,
						     __u32 mode);
struct dt_object *
local_index_find_or_create(const struct lu_env *env,
			   struct local_oid_storage *los,
			   struct dt_object *parent,
			   const char *name, __u32 mode,
			   const struct dt_index_features *ft);
struct dt_object *
local_index_find_or_create_with_fid(const struct lu_env *env,
				    struct dt_device *dt,
				    const struct lu_fid *fid,
				    struct dt_object *parent,
				    const char *name, __u32 mode,
				    const struct dt_index_features *ft);
int local_object_unlink(const struct lu_env *env, struct dt_device *dt,
			struct dt_object *parent, const char *name);

static inline int dt_object_lock(const struct lu_env *env,
				 struct dt_object *o, struct lustre_handle *lh,
				 struct ldlm_enqueue_info *einfo,
				 void *policy)
{
	LASSERT(o);
	LASSERT(o->do_ops);
	LASSERT(o->do_ops->do_object_lock);
	return o->do_ops->do_object_lock(env, o, lh, einfo, policy);
}

int dt_lookup_dir(const struct lu_env *env, struct dt_object *dir,
		  const char *name, struct lu_fid *fid);

static inline int dt_object_sync(const struct lu_env *env, struct dt_object *o,
				 __u64 start, __u64 end)
{
	LASSERT(o);
	LASSERT(o->do_ops);
	LASSERT(o->do_ops->do_object_sync);
	return o->do_ops->do_object_sync(env, o, start, end);
}

int dt_declare_version_set(const struct lu_env *env, struct dt_object *o,
			   struct thandle *th);
void dt_version_set(const struct lu_env *env, struct dt_object *o,
		    dt_obj_version_t version, struct thandle *th);
dt_obj_version_t dt_version_get(const struct lu_env *env, struct dt_object *o);


int dt_read(const struct lu_env *env, struct dt_object *dt,
	    struct lu_buf *buf, loff_t *pos);
int dt_record_read(const struct lu_env *env, struct dt_object *dt,
		   struct lu_buf *buf, loff_t *pos);
int dt_record_write(const struct lu_env *env, struct dt_object *dt,
		    const struct lu_buf *buf, loff_t *pos, struct thandle *th);
typedef int (*dt_index_page_build_t)(const struct lu_env *env,
				     union lu_page *lp, int nob,
				     const struct dt_it_ops *iops,
				     struct dt_it *it, __u32 attr, void *arg);
int dt_index_walk(const struct lu_env *env, struct dt_object *obj,
		  const struct lu_rdpg *rdpg, dt_index_page_build_t filler,
		  void *arg);
int dt_index_read(const struct lu_env *env, struct dt_device *dev,
		  struct idx_info *ii, const struct lu_rdpg *rdpg);

static inline struct thandle *dt_trans_create(const struct lu_env *env,
					      struct dt_device *d)
{
	LASSERT(d->dd_ops->dt_trans_create);
	return d->dd_ops->dt_trans_create(env, d);
}

static inline int dt_trans_start(const struct lu_env *env,
				 struct dt_device *d, struct thandle *th)
{
	LASSERT(d->dd_ops->dt_trans_start);
	return d->dd_ops->dt_trans_start(env, d, th);
}

/* for this transaction hooks shouldn't be called */
static inline int dt_trans_start_local(const struct lu_env *env,
				       struct dt_device *d, struct thandle *th)
{
	LASSERT(d->dd_ops->dt_trans_start);
	th->th_local = 1;
	return d->dd_ops->dt_trans_start(env, d, th);
}

static inline int dt_trans_stop(const struct lu_env *env,
				struct dt_device *d, struct thandle *th)
{
	LASSERT(d->dd_ops->dt_trans_stop);
	return d->dd_ops->dt_trans_stop(env, th);
}

static inline int dt_trans_cb_add(struct thandle *th,
				  struct dt_txn_commit_cb *dcb)
{
	LASSERT(th->th_dev->dd_ops->dt_trans_cb_add);
	dcb->dcb_magic = TRANS_COMMIT_CB_MAGIC;
	return th->th_dev->dd_ops->dt_trans_cb_add(th, dcb);
}
/** @} dt */


static inline int dt_declare_record_write(const struct lu_env *env,
					  struct dt_object *dt,
					  int size, loff_t pos,
					  struct thandle *th)
{
	int rc;

	LASSERTF(dt != NULL, "dt is NULL when we want to write record\n");
	LASSERT(th != NULL);
	LASSERT(dt->do_body_ops);
	LASSERT(dt->do_body_ops->dbo_declare_write);
	rc = dt->do_body_ops->dbo_declare_write(env, dt, size, pos, th);
	return rc;
}

static inline int dt_declare_create(const struct lu_env *env,
				    struct dt_object *dt,
				    struct lu_attr *attr,
				    struct dt_allocation_hint *hint,
				    struct dt_object_format *dof,
				    struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_create);
	return dt->do_ops->do_declare_create(env, dt, attr, hint, dof, th);
}

static inline int dt_create(const struct lu_env *env,
				    struct dt_object *dt,
				    struct lu_attr *attr,
				    struct dt_allocation_hint *hint,
				    struct dt_object_format *dof,
				    struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_create);
	return dt->do_ops->do_create(env, dt, attr, hint, dof, th);
}

static inline int dt_declare_destroy(const struct lu_env *env,
				     struct dt_object *dt,
				     struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_destroy);
	return dt->do_ops->do_declare_destroy(env, dt, th);
}

static inline int dt_destroy(const struct lu_env *env,
			     struct dt_object *dt,
			     struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_destroy);
	return dt->do_ops->do_destroy(env, dt, th);
}

static inline void dt_read_lock(const struct lu_env *env,
				struct dt_object *dt,
				unsigned role)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_read_lock);
	dt->do_ops->do_read_lock(env, dt, role);
}

static inline void dt_write_lock(const struct lu_env *env,
				struct dt_object *dt,
				unsigned role)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_write_lock);
	dt->do_ops->do_write_lock(env, dt, role);
}

static inline void dt_read_unlock(const struct lu_env *env,
				struct dt_object *dt)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_read_unlock);
	dt->do_ops->do_read_unlock(env, dt);
}

static inline void dt_write_unlock(const struct lu_env *env,
				struct dt_object *dt)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_write_unlock);
	dt->do_ops->do_write_unlock(env, dt);
}

static inline int dt_write_locked(const struct lu_env *env,
				  struct dt_object *dt)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_write_locked);
	return dt->do_ops->do_write_locked(env, dt);
}

static inline int dt_attr_get(const struct lu_env *env, struct dt_object *dt,
			      struct lu_attr *la, void *arg)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_attr_get);
	return dt->do_ops->do_attr_get(env, dt, la, arg);
}

static inline int dt_declare_attr_set(const struct lu_env *env,
				      struct dt_object *dt,
				      const struct lu_attr *la,
				      struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_attr_set);
	return dt->do_ops->do_declare_attr_set(env, dt, la, th);
}

static inline int dt_attr_set(const struct lu_env *env, struct dt_object *dt,
			      const struct lu_attr *la, struct thandle *th,
			      struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_attr_set);
	return dt->do_ops->do_attr_set(env, dt, la, th, capa);
}

static inline int dt_declare_ref_add(const struct lu_env *env,
				     struct dt_object *dt, struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_ref_add);
	return dt->do_ops->do_declare_ref_add(env, dt, th);
}

static inline int dt_ref_add(const struct lu_env *env,
			     struct dt_object *dt, struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_ref_add);
	return dt->do_ops->do_ref_add(env, dt, th);
}

static inline int dt_declare_ref_del(const struct lu_env *env,
				     struct dt_object *dt, struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_ref_del);
	return dt->do_ops->do_declare_ref_del(env, dt, th);
}

static inline int dt_ref_del(const struct lu_env *env,
			     struct dt_object *dt, struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_ref_del);
	return dt->do_ops->do_ref_del(env, dt, th);
}

static inline struct obd_capa *dt_capa_get(const struct lu_env *env,
					   struct dt_object *dt,
					   struct lustre_capa *old, __u64 opc)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_ref_del);
	return dt->do_ops->do_capa_get(env, dt, old, opc);
}

static inline int dt_bufs_get(const struct lu_env *env, struct dt_object *d,
			      struct niobuf_remote *rnb,
			      struct niobuf_local *lnb, int rw,
			      struct lustre_capa *capa)
{
	LASSERT(d);
	LASSERT(d->do_body_ops);
	LASSERT(d->do_body_ops->dbo_bufs_get);
	return d->do_body_ops->dbo_bufs_get(env, d, rnb->offset,
					    rnb->len, lnb, rw, capa);
}

static inline int dt_bufs_put(const struct lu_env *env, struct dt_object *d,
			      struct niobuf_local *lnb, int n)
{
	LASSERT(d);
	LASSERT(d->do_body_ops);
	LASSERT(d->do_body_ops->dbo_bufs_put);
	return d->do_body_ops->dbo_bufs_put(env, d, lnb, n);
}

static inline int dt_write_prep(const struct lu_env *env, struct dt_object *d,
				struct niobuf_local *lnb, int n)
{
	LASSERT(d);
	LASSERT(d->do_body_ops);
	LASSERT(d->do_body_ops->dbo_write_prep);
	return d->do_body_ops->dbo_write_prep(env, d, lnb, n);
}

static inline int dt_declare_write_commit(const struct lu_env *env,
					  struct dt_object *d,
					  struct niobuf_local *lnb,
					  int n, struct thandle *th)
{
	LASSERTF(d != NULL, "dt is NULL when we want to declare write\n");
	LASSERT(th != NULL);
	return d->do_body_ops->dbo_declare_write_commit(env, d, lnb, n, th);
}


static inline int dt_write_commit(const struct lu_env *env,
				  struct dt_object *d, struct niobuf_local *lnb,
				  int n, struct thandle *th)
{
	LASSERT(d);
	LASSERT(d->do_body_ops);
	LASSERT(d->do_body_ops->dbo_write_commit);
	return d->do_body_ops->dbo_write_commit(env, d, lnb, n, th);
}

static inline int dt_read_prep(const struct lu_env *env, struct dt_object *d,
			       struct niobuf_local *lnb, int n)
{
	LASSERT(d);
	LASSERT(d->do_body_ops);
	LASSERT(d->do_body_ops->dbo_read_prep);
	return d->do_body_ops->dbo_read_prep(env, d, lnb, n);
}

static inline int dt_declare_punch(const struct lu_env *env,
				   struct dt_object *dt, __u64 start,
				   __u64 end, struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_body_ops);
	LASSERT(dt->do_body_ops->dbo_declare_punch);
	return dt->do_body_ops->dbo_declare_punch(env, dt, start, end, th);
}

static inline int dt_punch(const struct lu_env *env, struct dt_object *dt,
			   __u64 start, __u64 end, struct thandle *th,
			   struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_body_ops);
	LASSERT(dt->do_body_ops->dbo_punch);
	return dt->do_body_ops->dbo_punch(env, dt, start, end, th, capa);
}

static inline int dt_fiemap_get(const struct lu_env *env, struct dt_object *d,
				struct ll_user_fiemap *fm)
{
	LASSERT(d);
	if (d->do_body_ops == NULL)
		return -EPROTO;
	if (d->do_body_ops->dbo_fiemap_get == NULL)
		return -EOPNOTSUPP;
	return d->do_body_ops->dbo_fiemap_get(env, d, fm);
}

static inline int dt_statfs(const struct lu_env *env, struct dt_device *dev,
			    struct obd_statfs *osfs)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_statfs);
	return dev->dd_ops->dt_statfs(env, dev, osfs);
}

static inline int dt_root_get(const struct lu_env *env, struct dt_device *dev,
			      struct lu_fid *f)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_root_get);
	return dev->dd_ops->dt_root_get(env, dev, f);
}

static inline void dt_conf_get(const struct lu_env *env,
			       const struct dt_device *dev,
			       struct dt_device_param *param)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_conf_get);
	return dev->dd_ops->dt_conf_get(env, dev, param);
}

static inline int dt_sync(const struct lu_env *env, struct dt_device *dev)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_sync);
	return dev->dd_ops->dt_sync(env, dev);
}

static inline int dt_ro(const struct lu_env *env, struct dt_device *dev)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_ro);
	return dev->dd_ops->dt_ro(env, dev);
}

static inline int dt_declare_insert(const struct lu_env *env,
				    struct dt_object *dt,
				    const struct dt_rec *rec,
				    const struct dt_key *key,
				    struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_index_ops);
	LASSERT(dt->do_index_ops->dio_declare_insert);
	return dt->do_index_ops->dio_declare_insert(env, dt, rec, key, th);
}

static inline int dt_insert(const struct lu_env *env,
				    struct dt_object *dt,
				    const struct dt_rec *rec,
				    const struct dt_key *key,
				    struct thandle *th,
				    struct lustre_capa *capa,
				    int noquota)
{
	LASSERT(dt);
	LASSERT(dt->do_index_ops);
	LASSERT(dt->do_index_ops->dio_insert);
	return dt->do_index_ops->dio_insert(env, dt, rec, key, th,
					    capa, noquota);
}

static inline int dt_declare_xattr_del(const struct lu_env *env,
				       struct dt_object *dt,
				       const char *name,
				       struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_xattr_del);
	return dt->do_ops->do_declare_xattr_del(env, dt, name, th);
}

static inline int dt_xattr_del(const struct lu_env *env,
			       struct dt_object *dt, const char *name,
			       struct thandle *th,
			       struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_xattr_del);
	return dt->do_ops->do_xattr_del(env, dt, name, th, capa);
}

static inline int dt_declare_xattr_set(const struct lu_env *env,
				      struct dt_object *dt,
				      const struct lu_buf *buf,
				      const char *name, int fl,
				      struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_declare_xattr_set);
	return dt->do_ops->do_declare_xattr_set(env, dt, buf, name, fl, th);
}

static inline int dt_xattr_set(const struct lu_env *env,
			      struct dt_object *dt, const struct lu_buf *buf,
			      const char *name, int fl, struct thandle *th,
			      struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_xattr_set);
	return dt->do_ops->do_xattr_set(env, dt, buf, name, fl, th, capa);
}

static inline int dt_xattr_get(const struct lu_env *env,
			      struct dt_object *dt, struct lu_buf *buf,
			      const char *name, struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_xattr_get);
	return dt->do_ops->do_xattr_get(env, dt, buf, name, capa);
}

static inline int dt_xattr_list(const struct lu_env *env,
			       struct dt_object *dt, struct lu_buf *buf,
			       struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_ops);
	LASSERT(dt->do_ops->do_xattr_list);
	return dt->do_ops->do_xattr_list(env, dt, buf, capa);
}

static inline int dt_declare_delete(const struct lu_env *env,
				    struct dt_object *dt,
				    const struct dt_key *key,
				    struct thandle *th)
{
	LASSERT(dt);
	LASSERT(dt->do_index_ops);
	LASSERT(dt->do_index_ops->dio_declare_delete);
	return dt->do_index_ops->dio_declare_delete(env, dt, key, th);
}

static inline int dt_delete(const struct lu_env *env,
			    struct dt_object *dt,
			    const struct dt_key *key,
			    struct thandle *th,
			    struct lustre_capa *capa)
{
	LASSERT(dt);
	LASSERT(dt->do_index_ops);
	LASSERT(dt->do_index_ops->dio_delete);
	return dt->do_index_ops->dio_delete(env, dt, key, th, capa);
}

static inline int dt_commit_async(const struct lu_env *env,
				  struct dt_device *dev)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_commit_async);
	return dev->dd_ops->dt_commit_async(env, dev);
}

static inline int dt_init_capa_ctxt(const struct lu_env *env,
				    struct dt_device *dev,
				    int mode, unsigned long timeout,
				    __u32 alg, struct lustre_capa_key *keys)
{
	LASSERT(dev);
	LASSERT(dev->dd_ops);
	LASSERT(dev->dd_ops->dt_init_capa_ctxt);
	return dev->dd_ops->dt_init_capa_ctxt(env, dev, mode,
					      timeout, alg, keys);
}

static inline int dt_lookup(const struct lu_env *env,
			    struct dt_object *dt,
			    struct dt_rec *rec,
			    const struct dt_key *key,
			    struct lustre_capa *capa)
{
	int ret;

	LASSERT(dt);
	LASSERT(dt->do_index_ops);
	LASSERT(dt->do_index_ops->dio_lookup);

	ret = dt->do_index_ops->dio_lookup(env, dt, rec, key, capa);
	if (ret > 0)
		ret = 0;
	else if (ret == 0)
		ret = -ENOENT;
	return ret;
}

#define LU221_BAD_TIME (0x80000000U + 24 * 3600)

struct dt_find_hint {
	struct lu_fid	*dfh_fid;
	struct dt_device     *dfh_dt;
	struct dt_object     *dfh_o;
};

struct dt_thread_info {
	char		     dti_buf[DT_MAX_PATH];
	struct dt_find_hint      dti_dfh;
	struct lu_attr	   dti_attr;
	struct lu_fid	    dti_fid;
	struct dt_object_format  dti_dof;
	struct lustre_mdt_attrs  dti_lma;
	struct lu_buf	    dti_lb;
	loff_t		   dti_off;
};

extern struct lu_context_key dt_key;

static inline struct dt_thread_info *dt_info(const struct lu_env *env)
{
	struct dt_thread_info *dti;

	dti = lu_context_key_get(&env->le_ctx, &dt_key);
	LASSERT(dti);
	return dti;
}

int dt_global_init(void);
void dt_global_fini(void);

#if defined (CONFIG_PROC_FS)
int lprocfs_dt_rd_blksize(char *page, char **start, off_t off,
			  int count, int *eof, void *data);
int lprocfs_dt_rd_kbytestotal(char *page, char **start, off_t off,
			      int count, int *eof, void *data);
int lprocfs_dt_rd_kbytesfree(char *page, char **start, off_t off,
			     int count, int *eof, void *data);
int lprocfs_dt_rd_kbytesavail(char *page, char **start, off_t off,
			      int count, int *eof, void *data);
int lprocfs_dt_rd_filestotal(char *page, char **start, off_t off,
			     int count, int *eof, void *data);
int lprocfs_dt_rd_filesfree(char *page, char **start, off_t off,
			    int count, int *eof, void *data);
#endif /* CONFIG_PROC_FS */

#endif /* __LUSTRE_DT_OBJECT_H */
