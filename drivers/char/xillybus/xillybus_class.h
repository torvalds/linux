/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2021 Xillybus Ltd, http://www.xillybus.com
 *
 * Header file for the Xillybus class
 */

#ifndef __XILLYBUS_CLASS_H
#define __XILLYBUS_CLASS_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>

int xillybus_init_chrdev(struct device *dev,
			 const struct file_operations *fops,
			 struct module *owner,
			 void *private_data,
			 unsigned char *idt, unsigned int len,
			 int num_nodes,
			 const char *prefix, bool enumerate);

void xillybus_cleanup_chrdev(void *private_data,
			     struct device *dev);

int xillybus_find_inode(struct inode *inode,
			void **private_data, int *index);

#endif /* __XILLYBUS_CLASS_H */
