/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __GASKET_IOCTL_H__
#define __GASKET_IOCTL_H__

#include "gasket_core.h"

/*
 * Handle Gasket common ioctls.
 * @filp: Pointer to the ioctl's file.
 * @cmd: Ioctl command.
 * @arg: Ioctl argument pointer.
 *
 * Returns 0 on success and nonzero on failure.
 */
long gasket_handle_ioctl(struct file *filp, uint cmd, ulong arg);

/*
 * Determines if an ioctl is part of the standard Gasket framework.
 * @cmd: The ioctl number to handle.
 *
 * Returns 1 if the ioctl is supported and 0 otherwise.
 */
long gasket_is_supported_ioctl(uint cmd);

#endif
