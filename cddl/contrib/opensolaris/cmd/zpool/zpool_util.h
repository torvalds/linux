/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	ZPOOL_UTIL_H
#define	ZPOOL_UTIL_H

#include <libnvpair.h>
#include <libzfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Basic utility functions
 */
void *safe_malloc(size_t);
void zpool_no_memory(void);
uint_t num_logs(nvlist_t *nv);

/*
 * Virtual device functions
 */

nvlist_t *make_root_vdev(zpool_handle_t *zhp, int force, int check_rep,
    boolean_t replacing, boolean_t dryrun, zpool_boot_label_t boot_type,
    uint64_t boot_size, int argc, char **argv);
nvlist_t *split_mirror_vdev(zpool_handle_t *zhp, char *newname,
    nvlist_t *props, splitflags_t flags, int argc, char **argv);

/*
 * Pool list functions
 */
int for_each_pool(int, char **, boolean_t unavail, zprop_list_t **,
    zpool_iter_f, void *);

typedef struct zpool_list zpool_list_t;

zpool_list_t *pool_list_get(int, char **, zprop_list_t **, int *);
void pool_list_update(zpool_list_t *);
int pool_list_iter(zpool_list_t *, int unavail, zpool_iter_f, void *);
void pool_list_free(zpool_list_t *);
int pool_list_count(zpool_list_t *);
void pool_list_remove(zpool_list_t *, zpool_handle_t *);

libzfs_handle_t *g_zfs;

#ifdef	__cplusplus
}
#endif

#endif	/* ZPOOL_UTIL_H */
