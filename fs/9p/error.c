/*
 * linux/fs/9p/error.c
 *
 * Error string handling
 *
 * Plan 9 uses error strings, Unix uses error numbers.  These functions
 * try to help manage that and provide for dynamically adding error
 * mappings.
 *
 *  Copyright (C) 2004 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/list.h>
#include <linux/jhash.h>

#include "debug.h"
#include "error.h"

/**
 * v9fs_error_init - preload
 * @errstr: error string
 *
 */

int v9fs_error_init(void)
{
	struct errormap *c;
	int bucket;

	/* initialize hash table */
	for (bucket = 0; bucket < ERRHASHSZ; bucket++)
		INIT_HLIST_HEAD(&hash_errmap[bucket]);

	/* load initial error map into hash table */
	for (c = errmap; c->name != NULL; c++) {
		bucket = jhash(c->name, strlen(c->name), 0) % ERRHASHSZ;
		INIT_HLIST_NODE(&c->list);
		hlist_add_head(&c->list, &hash_errmap[bucket]);
	}

	return 1;
}

/**
 * errstr2errno - convert error string to error number
 * @errstr: error string
 *
 */

int v9fs_errstr2errno(char *errstr)
{
	int errno = 0;
	struct hlist_node *p = NULL;
	struct errormap *c = NULL;
	int bucket = jhash(errstr, strlen(errstr), 0) % ERRHASHSZ;

	hlist_for_each_entry(c, p, &hash_errmap[bucket], list) {
		if (!strcmp(c->name, errstr)) {
			errno = c->val;
			break;
		}
	}

	if (errno == 0) {
		/* TODO: if error isn't found, add it dynamically */
		printk(KERN_ERR "%s: errstr :%s: not found\n", __FUNCTION__,
		       errstr);
		errno = 1;
	}

	return -errno;
}
