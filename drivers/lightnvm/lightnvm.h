/*
 * Copyright (C) 2016 CNEX Labs. All rights reserved.
 * Initial release: Matias Bjorling <matias@cnexlabs.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 */

#ifndef LIGHTNVM_H
#define LIGHTNVM_H

#include <linux/lightnvm.h>

/* core -> sysfs.c */
int __must_check nvm_sysfs_register_dev(struct nvm_dev *);
void nvm_sysfs_unregister_dev(struct nvm_dev *);
int nvm_sysfs_register(void);
void nvm_sysfs_unregister(void);

/* sysfs > core */
void nvm_free(struct nvm_dev *);

#endif
