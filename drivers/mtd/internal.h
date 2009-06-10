/* Internal MTD definitions
 *
 * Copyright © 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*
 * mtdbdi.c
 */
extern struct backing_dev_info mtd_bdi_unmappable;
extern struct backing_dev_info mtd_bdi_ro_mappable;
extern struct backing_dev_info mtd_bdi_rw_mappable;
