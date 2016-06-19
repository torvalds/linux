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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define MAX_STRING_SIZE 128

extern int ldlm_srv_namespace_nr;
extern int ldlm_cli_namespace_nr;
extern struct mutex ldlm_srv_namespace_lock;
extern struct list_head ldlm_srv_namespace_list;
extern struct mutex ldlm_cli_namespace_lock;
extern struct list_head ldlm_cli_active_namespace_list;

static inline int ldlm_namespace_nr_read(ldlm_side_t client)
{
	return client == LDLM_NAMESPACE_SERVER ?
		ldlm_srv_namespace_nr : ldlm_cli_namespace_nr;
}

static inline void ldlm_namespace_nr_inc(ldlm_side_t client)
{
	if (client == LDLM_NAMESPACE_SERVER)
		ldlm_srv_namespace_nr++;
	else
		ldlm_cli_namespace_nr++;
}

static inline void ldlm_namespace_nr_dec(ldlm_side_t client)
{
	if (client == LDLM_NAMESPACE_SERVER)
		ldlm_srv_namespace_nr--;
	else
		ldlm_cli_namespace_nr--;
}

static inline struct list_head *ldlm_namespace_list(ldlm_side_t client)
{
	return client == LDLM_NAMESPACE_SERVER ?
		&ldlm_srv_namespace_list : &ldlm_cli_active_namespace_list;
}

static inline struct mutex *ldlm_namespace_lock(ldlm_side_t client)
{
	return client == LDLM_NAMESPACE_SERVER ?
		&ldlm_srv_namespace_lock : &ldlm_cli_namespace_lock;
}

/* ns_bref is the number of resources in this namespace */
static inline int ldlm_ns_empty(struct ldlm_namespace *ns)
{
	return atomic_read(&ns->ns_bref) == 0;
}

void ldlm_namespace_move_to_active_locked(struct ldlm_namespace *, ldlm_side_t);
void ldlm_namespace_move_to_inactive_locked(struct ldlm_namespace *,
					    ldlm_side_t);
struct ldlm_namespace *ldlm_namespace_first_locked(ldlm_side_t);

/* ldlm_request.c */
/* Cancel lru flag, it indicates we cancel aged locks. */
enum {
	LDLM_CANCEL_AGED   = 1 << 0, /* Cancel aged locks (non lru resize). */
	LDLM_CANCEL_PASSED = 1 << 1, /* Cancel passed number of locks. */
	LDLM_CANCEL_SHRINK = 1 << 2, /* Cancel locks from shrinker. */
	LDLM_CANCEL_LRUR   = 1 << 3, /* Cancel locks from lru resize. */
	LDLM_CANCEL_NO_WAIT = 1 << 4 /* Cancel locks w/o blocking (neither
				      * sending nor waiting for any rpcs)
				      */
};

int ldlm_cancel_lru(struct ldlm_namespace *ns, int nr,
		    enum ldlm_cancel_flags sync, int flags);
int ldlm_cancel_lru_local(struct ldlm_namespace *ns,
			 struct list_head *cancels, int count, int max,
			 enum ldlm_cancel_flags cancel_flags, int flags);
extern int ldlm_enqueue_min;

/* ldlm_resource.c */
int ldlm_resource_putref_locked(struct ldlm_resource *res);
void ldlm_namespace_free_prior(struct ldlm_namespace *ns,
			       struct obd_import *imp, int force);
void ldlm_namespace_free_post(struct ldlm_namespace *ns);
/* ldlm_lock.c */

struct ldlm_cb_set_arg {
	struct ptlrpc_request_set	*set;
	int				 type; /* LDLM_{CP,BL,GL}_CALLBACK */
	atomic_t			 restart;
	struct list_head			*list;
	union ldlm_gl_desc		*gl_desc; /* glimpse AST descriptor */
};

enum ldlm_desc_ast_t {
	LDLM_WORK_BL_AST,
	LDLM_WORK_CP_AST,
	LDLM_WORK_REVOKE_AST,
	LDLM_WORK_GL_AST
};

void ldlm_grant_lock(struct ldlm_lock *lock, struct list_head *work_list);
int ldlm_fill_lvb(struct ldlm_lock *lock, struct req_capsule *pill,
		  enum req_location loc, void *data, int size);
struct ldlm_lock *
ldlm_lock_create(struct ldlm_namespace *ns, const struct ldlm_res_id *,
		 enum ldlm_type type, enum ldlm_mode mode,
		 const struct ldlm_callback_suite *cbs,
		 void *data, __u32 lvb_len, enum lvb_type lvb_type);
enum ldlm_error ldlm_lock_enqueue(struct ldlm_namespace *, struct ldlm_lock **,
				  void *cookie, __u64 *flags);
void ldlm_lock_addref_internal(struct ldlm_lock *, __u32 mode);
void ldlm_lock_addref_internal_nolock(struct ldlm_lock *, __u32 mode);
void ldlm_lock_decref_internal(struct ldlm_lock *, __u32 mode);
void ldlm_lock_decref_internal_nolock(struct ldlm_lock *, __u32 mode);
int ldlm_run_ast_work(struct ldlm_namespace *ns, struct list_head *rpc_list,
		      enum ldlm_desc_ast_t ast_type);
int ldlm_lock_remove_from_lru(struct ldlm_lock *lock);
int ldlm_lock_remove_from_lru_nolock(struct ldlm_lock *lock);
void ldlm_lock_destroy_nolock(struct ldlm_lock *lock);

/* ldlm_lockd.c */
int ldlm_bl_to_thread_lock(struct ldlm_namespace *ns, struct ldlm_lock_desc *ld,
			   struct ldlm_lock *lock);
int ldlm_bl_to_thread_list(struct ldlm_namespace *ns,
			   struct ldlm_lock_desc *ld,
			   struct list_head *cancels, int count,
			   enum ldlm_cancel_flags cancel_flags);

void ldlm_handle_bl_callback(struct ldlm_namespace *ns,
			     struct ldlm_lock_desc *ld, struct ldlm_lock *lock);

extern struct kmem_cache *ldlm_resource_slab;
extern struct kset *ldlm_ns_kset;

/* ldlm_lockd.c & ldlm_lock.c */
extern struct kmem_cache *ldlm_lock_slab;

/* ldlm_extent.c */
void ldlm_extent_add_lock(struct ldlm_resource *res, struct ldlm_lock *lock);
void ldlm_extent_unlink_lock(struct ldlm_lock *lock);

/* l_lock.c */
void l_check_ns_lock(struct ldlm_namespace *ns);
void l_check_no_ns_lock(struct ldlm_namespace *ns);

extern struct dentry *ldlm_svc_debugfs_dir;

struct ldlm_state {
	struct ptlrpc_service *ldlm_cb_service;
	struct ptlrpc_service *ldlm_cancel_service;
	struct ptlrpc_client *ldlm_client;
	struct ptlrpc_connection *ldlm_server_conn;
	struct ldlm_bl_pool *ldlm_bl_pool;
};

/* ldlm_pool.c */
__u64 ldlm_pool_get_slv(struct ldlm_pool *pl);
void ldlm_pool_set_clv(struct ldlm_pool *pl, __u64 clv);
__u32 ldlm_pool_get_lvf(struct ldlm_pool *pl);

/* interval tree, for LDLM_EXTENT. */
extern struct kmem_cache *ldlm_interval_slab; /* slab cache for ldlm_interval */
struct ldlm_interval *ldlm_interval_detach(struct ldlm_lock *l);
struct ldlm_interval *ldlm_interval_alloc(struct ldlm_lock *lock);
void ldlm_interval_free(struct ldlm_interval *node);
/* this function must be called with res lock held */
static inline struct ldlm_extent *
ldlm_interval_extent(struct ldlm_interval *node)
{
	struct ldlm_lock *lock;

	LASSERT(!list_empty(&node->li_group));

	lock = list_entry(node->li_group.next, struct ldlm_lock,
			      l_sl_policy);
	return &lock->l_policy_data.l_extent;
}

int ldlm_init(void);
void ldlm_exit(void);

enum ldlm_policy_res {
	LDLM_POLICY_CANCEL_LOCK,
	LDLM_POLICY_KEEP_LOCK,
	LDLM_POLICY_SKIP_LOCK
};

typedef enum ldlm_policy_res ldlm_policy_res_t;

#define LDLM_POOL_SYSFS_PRINT_int(v) sprintf(buf, "%d\n", v)
#define LDLM_POOL_SYSFS_SET_int(a, b) { a = b; }
#define LDLM_POOL_SYSFS_PRINT_u64(v) sprintf(buf, "%lld\n", v)
#define LDLM_POOL_SYSFS_SET_u64(a, b) { a = b; }
#define LDLM_POOL_SYSFS_PRINT_atomic(v) sprintf(buf, "%d\n", atomic_read(&v))
#define LDLM_POOL_SYSFS_SET_atomic(a, b) atomic_set(&a, b)

#define LDLM_POOL_SYSFS_READER_SHOW(var, type)				    \
	static ssize_t var##_show(struct kobject *kobj,			    \
				  struct attribute *attr,		    \
				  char *buf)				    \
	{								    \
		struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool, \
						    pl_kobj);		    \
		type tmp;						    \
									    \
		spin_lock(&pl->pl_lock);				    \
		tmp = pl->pl_##var;					    \
		spin_unlock(&pl->pl_lock);				    \
									    \
		return LDLM_POOL_SYSFS_PRINT_##type(tmp);		    \
	}								    \
	struct __##var##__dummy_read {; } /* semicolon catcher */

#define LDLM_POOL_SYSFS_WRITER_STORE(var, type)				    \
	static ssize_t var##_store(struct kobject *kobj,		    \
				     struct attribute *attr,		    \
				     const char *buffer,		    \
				     size_t count)			    \
	{								    \
		struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool, \
						    pl_kobj);		    \
		unsigned long tmp;					    \
		int rc;							    \
									    \
		rc = kstrtoul(buffer, 10, &tmp);			    \
		if (rc < 0) {						    \
			return rc;					    \
		}							    \
									    \
		spin_lock(&pl->pl_lock);				    \
		LDLM_POOL_SYSFS_SET_##type(pl->pl_##var, tmp);		    \
		spin_unlock(&pl->pl_lock);				    \
									    \
		return count;						    \
	}								    \
	struct __##var##__dummy_write {; } /* semicolon catcher */

#define LDLM_POOL_SYSFS_READER_NOLOCK_SHOW(var, type)			    \
	static ssize_t var##_show(struct kobject *kobj,		    \
				    struct attribute *attr,		    \
				    char *buf)				    \
	{								    \
		struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool, \
						    pl_kobj);		    \
									    \
		return LDLM_POOL_SYSFS_PRINT_##type(pl->pl_##var);	    \
	}								    \
	struct __##var##__dummy_read {; } /* semicolon catcher */

#define LDLM_POOL_SYSFS_WRITER_NOLOCK_STORE(var, type)			    \
	static ssize_t var##_store(struct kobject *kobj,		    \
				     struct attribute *attr,		    \
				     const char *buffer,		    \
				     size_t count)			    \
	{								    \
		struct ldlm_pool *pl = container_of(kobj, struct ldlm_pool, \
						    pl_kobj);		    \
		unsigned long tmp;					    \
		int rc;							    \
									    \
		rc = kstrtoul(buffer, 10, &tmp);			    \
		if (rc < 0) {						    \
			return rc;					    \
		}							    \
									    \
		LDLM_POOL_SYSFS_SET_##type(pl->pl_##var, tmp);		    \
									    \
		return count;						    \
	}								    \
	struct __##var##__dummy_write {; } /* semicolon catcher */

static inline int is_granted_or_cancelled(struct ldlm_lock *lock)
{
	int ret = 0;

	lock_res_and_lock(lock);
	if (((lock->l_req_mode == lock->l_granted_mode) &&
	     !(lock->l_flags & LDLM_FL_CP_REQD)) ||
	    (lock->l_flags & (LDLM_FL_FAILED | LDLM_FL_CANCEL)))
		ret = 1;
	unlock_res_and_lock(lock);

	return ret;
}

typedef void (*ldlm_policy_wire_to_local_t)(const ldlm_wire_policy_data_t *,
					    ldlm_policy_data_t *);

typedef void (*ldlm_policy_local_to_wire_t)(const ldlm_policy_data_t *,
					    ldlm_wire_policy_data_t *);

void ldlm_plain_policy_wire_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy);
void ldlm_plain_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy);
void ldlm_ibits_policy_wire_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy);
void ldlm_ibits_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy);
void ldlm_extent_policy_wire_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy);
void ldlm_extent_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy);
void ldlm_flock_policy_wire18_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy);
void ldlm_flock_policy_wire21_to_local(const ldlm_wire_policy_data_t *wpolicy,
				     ldlm_policy_data_t *lpolicy);

void ldlm_flock_policy_local_to_wire(const ldlm_policy_data_t *lpolicy,
				     ldlm_wire_policy_data_t *wpolicy);
