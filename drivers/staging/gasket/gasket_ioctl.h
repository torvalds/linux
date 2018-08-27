/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Google, Inc. */
#ifndef __GASKET_IOCTL_H__
#define __GASKET_IOCTL_H__

#include "gasket_core.h"

#include <linux/compiler.h>

/*
 * Handle Gasket common ioctls.
 * @filp: Pointer to the ioctl's file.
 * @cmd: Ioctl command.
 * @arg: Ioctl argument pointer.
 *
 * Returns 0 on success and nonzero on failure.
 */
long gasket_handle_ioctl(struct file *filp, uint cmd, void __user *argp);

/*
 * Determines if an ioctl is part of the standard Gasket framework.
 * @cmd: The ioctl number to handle.
 *
 * Returns 1 if the ioctl is supported and 0 otherwise.
 */
long gasket_is_supported_ioctl(uint cmd);

#endif
