/*
 *
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef __PVRUSB2_SYSFS_H
#define __PVRUSB2_SYSFS_H

#include <linux/list.h>
#include <linux/sysfs.h>
#include "pvrusb2-context.h"

struct pvr2_sysfs;
struct pvr2_sysfs_class;

struct pvr2_sysfs_class *pvr2_sysfs_class_create(void);
void pvr2_sysfs_class_destroy(struct pvr2_sysfs_class *);

struct pvr2_sysfs *pvr2_sysfs_create(struct pvr2_context *,
				     struct pvr2_sysfs_class *);

#endif /* __PVRUSB2_SYSFS_H */
