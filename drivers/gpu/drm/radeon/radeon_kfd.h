/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * radeon_kfd.h defines the private interface between the
 * AMD kernel graphics drivers and the AMD KFD.
 */

#ifndef RADEON_KFD_H_INCLUDED
#define RADEON_KFD_H_INCLUDED

#include <linux/types.h>
#include "kgd_kfd_interface.h"

struct radeon_device;

bool radeon_kfd_init(void);
void radeon_kfd_fini(void);

void radeon_kfd_suspend(struct radeon_device *rdev);
int radeon_kfd_resume(struct radeon_device *rdev);
void radeon_kfd_interrupt(struct radeon_device *rdev,
			const void *ih_ring_entry);
void radeon_kfd_device_probe(struct radeon_device *rdev);
void radeon_kfd_device_init(struct radeon_device *rdev);
void radeon_kfd_device_fini(struct radeon_device *rdev);

#endif /* RADEON_KFD_H_INCLUDED */
