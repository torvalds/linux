/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2003 Digi International (www.digi.com)
 *	Scott H Kilau <Scott_Kilau at digi dot com>
 */

#ifndef _DGNC_MGMT_H
#define _DGNC_MGMT_H

#define MAXMGMTDEVICES 8

int dgnc_mgmt_open(struct inode *inode, struct file *file);
int dgnc_mgmt_close(struct inode *inode, struct file *file);
long dgnc_mgmt_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

#endif	/* _DGNC_MGMT_H */

