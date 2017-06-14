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
 */

#ifndef _DGNC_MGMT_H
#define _DGNC_MGMT_H

#define MAXMGMTDEVICES 8

int dgnc_mgmt_open(struct inode *inode, struct file *file);
int dgnc_mgmt_close(struct inode *inode, struct file *file);
long dgnc_mgmt_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif	/* _DGNC_MGMT_H */

