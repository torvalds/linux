/*
 * Support for atomisp driver sysfs interface.
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef	__ATOMISP_DRVFS_H__
#define	__ATOMISP_DRVFS_H__

extern int atomisp_drvfs_init(struct pci_driver *drv, struct atomisp_device
				*isp);
extern void atomisp_drvfs_exit(void);

#endif /* __ATOMISP_DRVFS_H__ */
