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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_FILTER

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/libcfs/libcfs.h>
#include <lustre_fsfilt.h>

LIST_HEAD(fsfilt_types);

static struct fsfilt_operations *fsfilt_search_type(const char *type)
{
	struct fsfilt_operations *found;
	struct list_head *p;

	list_for_each(p, &fsfilt_types) {
		found = list_entry(p, struct fsfilt_operations, fs_list);
		if (!strcmp(found->fs_type, type))
			return found;
	}
	return NULL;
}

int fsfilt_register_ops(struct fsfilt_operations *fs_ops)
{
	struct fsfilt_operations *found;

	/* lock fsfilt_types list */
	found = fsfilt_search_type(fs_ops->fs_type);
	if (found) {
		if (found != fs_ops) {
			CERROR("different operations for type %s\n",
			       fs_ops->fs_type);
			/* unlock fsfilt_types list */
			return -EEXIST;
		}
	} else {
		try_module_get(THIS_MODULE);
		list_add(&fs_ops->fs_list, &fsfilt_types);
	}

	/* unlock fsfilt_types list */
	return 0;
}
EXPORT_SYMBOL(fsfilt_register_ops);

void fsfilt_unregister_ops(struct fsfilt_operations *fs_ops)
{
	struct list_head *p;

	/* lock fsfilt_types list */
	list_for_each(p, &fsfilt_types) {
		struct fsfilt_operations *found;

		found = list_entry(p, typeof(*found), fs_list);
		if (found == fs_ops) {
			list_del(p);
			module_put(THIS_MODULE);
			break;
		}
	}
	/* unlock fsfilt_types list */
}
EXPORT_SYMBOL(fsfilt_unregister_ops);

struct fsfilt_operations *fsfilt_get_ops(const char *type)
{
	struct fsfilt_operations *fs_ops;

	/* lock fsfilt_types list */
	fs_ops = fsfilt_search_type(type);
	if (!fs_ops) {
		char name[32];
		int rc;

		snprintf(name, sizeof(name) - 1, "fsfilt_%s", type);
		name[sizeof(name) - 1] = '\0';

		rc = request_module("%s", name);
		if (!rc) {
			fs_ops = fsfilt_search_type(type);
			CDEBUG(D_INFO, "Loaded module '%s'\n", name);
			if (!fs_ops)
				rc = -ENOENT;
		}

		if (rc) {
			CERROR("Can't find %s interface\n", name);
			return ERR_PTR(rc < 0 ? rc : -rc);
			/* unlock fsfilt_types list */
		}
	}
	try_module_get(fs_ops->fs_owner);
	/* unlock fsfilt_types list */

	return fs_ops;
}
EXPORT_SYMBOL(fsfilt_get_ops);

void fsfilt_put_ops(struct fsfilt_operations *fs_ops)
{
	module_put(fs_ops->fs_owner);
}
EXPORT_SYMBOL(fsfilt_put_ops);
