/* MTD backing device capabilities
 *
 * Copyright © 2006 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/backing-dev.h>
#include <linux/mtd/mtd.h>
#include "internal.h"

/*
 * backing device capabilities for non-mappable devices (such as NAND flash)
 * - permits private mappings, copies are taken of the data
 */
struct backing_dev_info mtd_bdi_unmappable = {
	.capabilities	= BDI_CAP_MAP_COPY,
};

/*
 * backing device capabilities for R/O mappable devices (such as ROM)
 * - permits private mappings, copies are taken of the data
 * - permits non-writable shared mappings
 */
struct backing_dev_info mtd_bdi_ro_mappable = {
	.capabilities	= (BDI_CAP_MAP_COPY | BDI_CAP_MAP_DIRECT |
			   BDI_CAP_EXEC_MAP | BDI_CAP_READ_MAP),
};

/*
 * backing device capabilities for writable mappable devices (such as RAM)
 * - permits private mappings, copies are taken of the data
 * - permits non-writable shared mappings
 */
struct backing_dev_info mtd_bdi_rw_mappable = {
	.capabilities	= (BDI_CAP_MAP_COPY | BDI_CAP_MAP_DIRECT |
			   BDI_CAP_EXEC_MAP | BDI_CAP_READ_MAP |
			   BDI_CAP_WRITE_MAP),
};
