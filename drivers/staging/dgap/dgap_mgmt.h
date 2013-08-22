/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	NOTE: THIS IS A SHARED HEADER. DO NOT CHANGE CODING STYLE!!!
 */

#ifndef __DGAP_MGMT_H
#define __DGAP_MGMT_H

#define MGMT_MGMT 0
#define MGMT_DOWNLD 1

int dgap_mgmt_open(struct inode *inode, struct file *file);
int dgap_mgmt_close(struct inode *inode, struct file *file);
long dgap_mgmt_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif

