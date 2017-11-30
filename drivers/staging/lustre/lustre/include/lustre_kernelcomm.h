// SPDX-License-Identifier: GPL-2.0
/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2013 Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 *
 * Kernel <-> userspace communication routines.
 * The definitions below are used in the kernel and userspace.
 */

#ifndef __LUSTRE_KERNELCOMM_H__
#define __LUSTRE_KERNELCOMM_H__

/* For declarations shared with userspace */
#include <uapi/linux/lustre/lustre_kernelcomm.h>

/* prototype for callback function on kuc groups */
typedef int (*libcfs_kkuc_cb_t)(void *data, void *cb_arg);

/* Kernel methods */
int libcfs_kkuc_msg_put(struct file *fp, void *payload);
int libcfs_kkuc_group_put(unsigned int group, void *payload);
int libcfs_kkuc_group_add(struct file *fp, int uid, unsigned int group,
			  void *data, size_t data_len);
int libcfs_kkuc_group_rem(int uid, unsigned int group);
int libcfs_kkuc_group_foreach(unsigned int group, libcfs_kkuc_cb_t cb_func,
			      void *cb_arg);

#endif /* __LUSTRE_KERNELCOMM_H__ */
