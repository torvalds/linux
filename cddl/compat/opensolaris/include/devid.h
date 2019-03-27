/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 *
 * $FreeBSD$
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _OPENSOLARIS_DEVID_H_
#define _OPENSOLARIS_DEVID_H_

#include <sys/param.h>
#include <sys/disk.h>
#include <stdlib.h>

typedef struct ddi_devid {
	char devid[DISK_IDENT_SIZE];
} ddi_devid_t;

typedef struct devid_nmlist {
	char devname[MAXPATHLEN];
	dev_t dev;
} devid_nmlist_t;

int devid_str_decode(char *devidstr, ddi_devid_t *retdevid,
    char **retminor_name);
int devid_deviceid_to_nmlist(char *search_path, ddi_devid_t devid,
    char *minor_name, devid_nmlist_t **retlist);
void devid_str_free(char *str);
void devid_free(ddi_devid_t devid);
void devid_free_nmlist(devid_nmlist_t *list);
int devid_get(int fd, ddi_devid_t *retdevid);
int devid_get_minor_name(int fd, char **retminor_name);
char *devid_str_encode(ddi_devid_t devid, char *minor_name);

#endif	/* !_OPENSOLARIS_DEVID_H_ */
