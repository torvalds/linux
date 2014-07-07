/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.  A copy is
 * included in the COPYING file that accompanied this code.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * lustre/obdclass/local_storage.c
 *
 * Local storage for file/objects with fid generation. Works on top of OSD.
 *
 * Author: Mikhail Pershin <mike.pershin@intel.com>
 */
#ifndef __LOCAL_STORAGE_H
#define __LOCAL_STORAGE_H

#include <dt_object.h>
#include <obd.h>
#include <lustre_fid.h>
#include <lustre_disk.h>

struct ls_device {
	struct dt_device	 ls_top_dev;
	/* all initialized ls_devices on this node linked by this */
	struct list_head		 ls_linkage;
	/* how many handle's reference this local storage */
	atomic_t		 ls_refcount;
	/* underlaying OSD device */
	struct dt_device	*ls_osd;
	/* list of all local OID storages */
	struct list_head		 ls_los_list;
	struct mutex		 ls_los_mutex;
};

static inline struct ls_device *dt2ls_dev(struct dt_device *d)
{
	return container_of0(d, struct ls_device, ls_top_dev);
}

struct ls_object {
	struct lu_object_header	 ls_header;
	struct dt_object	 ls_obj;
};

static inline struct ls_object *lu2ls_obj(struct lu_object *o)
{
	return container_of0(o, struct ls_object, ls_obj.do_lu);
}

static inline struct dt_object *ls_locate(const struct lu_env *env,
					  struct ls_device *ls,
					  const struct lu_fid *fid)
{
	return dt_locate_at(env, ls->ls_osd, fid, &ls->ls_top_dev.dd_lu_dev);
}

struct ls_device *ls_device_get(struct dt_device *dev);
void ls_device_put(const struct lu_env *env, struct ls_device *ls);
struct local_oid_storage *dt_los_find(struct ls_device *ls, __u64 seq);
void dt_los_put(struct local_oid_storage *los);

/* Lustre 2.3 on-disk structure describing local object OIDs storage
 * the structure to be used with any sequence managed by
 * local object library.
 * Obsoleted since 2.4 but is kept for compatibility reasons,
 * see lastid_compat_check() in obdclass/local_storage.c */
struct los_ondisk {
	__u32 lso_magic;
	__u32 lso_next_oid;
};

#define LOS_MAGIC	0xdecafbee
#endif
