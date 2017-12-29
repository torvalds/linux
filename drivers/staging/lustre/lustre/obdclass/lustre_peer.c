// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_RPC

#include <obd.h>
#include <obd_support.h>
#include <obd_class.h>
#include <lustre_lib.h>
#include <lustre_ha.h>
#include <lustre_net.h>
#include <lprocfs_status.h>

#define NIDS_MAX	32

struct uuid_nid_data {
	struct list_head       un_list;
	struct obd_uuid  un_uuid;
	int	      un_nid_count;
	lnet_nid_t       un_nids[NIDS_MAX];
};

/* FIXME: This should probably become more elegant than a global linked list */
static struct list_head	g_uuid_list;
static spinlock_t	g_uuid_lock;

void class_init_uuidlist(void)
{
	INIT_LIST_HEAD(&g_uuid_list);
	spin_lock_init(&g_uuid_lock);
}

void class_exit_uuidlist(void)
{
	/* delete all */
	class_del_uuid(NULL);
}

int lustre_uuid_to_peer(const char *uuid, lnet_nid_t *peer_nid, int index)
{
	struct uuid_nid_data *data;
	struct obd_uuid tmp;
	int rc = -ENOENT;

	obd_str2uuid(&tmp, uuid);
	spin_lock(&g_uuid_lock);
	list_for_each_entry(data, &g_uuid_list, un_list) {
		if (obd_uuid_equals(&data->un_uuid, &tmp)) {
			if (index >= data->un_nid_count)
				break;

			rc = 0;
			*peer_nid = data->un_nids[index];
			break;
		}
	}
	spin_unlock(&g_uuid_lock);
	return rc;
}
EXPORT_SYMBOL(lustre_uuid_to_peer);

/* Add a nid to a niduuid.  Multiple nids can be added to a single uuid;
 * LNET will choose the best one.
 */
int class_add_uuid(const char *uuid, __u64 nid)
{
	struct uuid_nid_data *data, *entry;
	int found = 0;

	LASSERT(nid != 0);  /* valid newconfig NID is never zero */

	if (strlen(uuid) > UUID_MAX - 1)
		return -EOVERFLOW;

	data = kzalloc(sizeof(*data), GFP_NOFS);
	if (!data)
		return -ENOMEM;

	obd_str2uuid(&data->un_uuid, uuid);
	data->un_nids[0] = nid;
	data->un_nid_count = 1;

	spin_lock(&g_uuid_lock);
	list_for_each_entry(entry, &g_uuid_list, un_list) {
		if (obd_uuid_equals(&entry->un_uuid, &data->un_uuid)) {
			int i;

			found = 1;
			for (i = 0; i < entry->un_nid_count; i++)
				if (nid == entry->un_nids[i])
					break;

			if (i == entry->un_nid_count) {
				LASSERT(entry->un_nid_count < NIDS_MAX);
				entry->un_nids[entry->un_nid_count++] = nid;
			}
			break;
		}
	}
	if (!found)
		list_add(&data->un_list, &g_uuid_list);
	spin_unlock(&g_uuid_lock);

	if (found) {
		CDEBUG(D_INFO, "found uuid %s %s cnt=%d\n", uuid,
		       libcfs_nid2str(nid), entry->un_nid_count);
		kfree(data);
	} else {
		CDEBUG(D_INFO, "add uuid %s %s\n", uuid, libcfs_nid2str(nid));
	}
	return 0;
}

/* Delete the nids for one uuid if specified, otherwise delete all */
int class_del_uuid(const char *uuid)
{
	LIST_HEAD(deathrow);
	struct uuid_nid_data *data;
	struct uuid_nid_data *temp;

	spin_lock(&g_uuid_lock);
	if (uuid) {
		struct obd_uuid tmp;

		obd_str2uuid(&tmp, uuid);
		list_for_each_entry(data, &g_uuid_list, un_list) {
			if (obd_uuid_equals(&data->un_uuid, &tmp)) {
				list_move(&data->un_list, &deathrow);
				break;
			}
		}
	} else {
		list_splice_init(&g_uuid_list, &deathrow);
	}
	spin_unlock(&g_uuid_lock);

	if (uuid && list_empty(&deathrow)) {
		CDEBUG(D_INFO, "Try to delete a non-existent uuid %s\n", uuid);
		return -EINVAL;
	}

	list_for_each_entry_safe(data, temp, &deathrow, un_list) {
		list_del(&data->un_list);

		CDEBUG(D_INFO, "del uuid %s %s/%d\n",
		       obd_uuid2str(&data->un_uuid),
		       libcfs_nid2str(data->un_nids[0]),
		       data->un_nid_count);

		kfree(data);
	}

	return 0;
}

/* check if @nid exists in nid list of @uuid */
int class_check_uuid(struct obd_uuid *uuid, __u64 nid)
{
	struct uuid_nid_data *entry;
	int found = 0;

	CDEBUG(D_INFO, "check if uuid %s has %s.\n",
	       obd_uuid2str(uuid), libcfs_nid2str(nid));

	spin_lock(&g_uuid_lock);
	list_for_each_entry(entry, &g_uuid_list, un_list) {
		int i;

		if (!obd_uuid_equals(&entry->un_uuid, uuid))
			continue;

		/* found the uuid, check if it has @nid */
		for (i = 0; i < entry->un_nid_count; i++) {
			if (entry->un_nids[i] == nid) {
				found = 1;
				break;
			}
		}
		break;
	}
	spin_unlock(&g_uuid_lock);
	return found;
}
EXPORT_SYMBOL(class_check_uuid);
