/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#ifndef _MIC_FOPS_H_
#define _MIC_FOPS_H_

int mic_open(struct inode *inode, struct file *filp);
int mic_release(struct inode *inode, struct file *filp);
ssize_t mic_read(struct file *filp, char __user *buf,
			size_t count, loff_t *pos);
long mic_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int mic_mmap(struct file *f, struct vm_area_struct *vma);
unsigned int mic_poll(struct file *f, poll_table *wait);

#endif
