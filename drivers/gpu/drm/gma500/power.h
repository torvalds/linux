/**************************************************************************
 * Copyright (c) 2009-2011, Intel Corporation.
 * All Rights Reserved.

 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Benjamin Defnet <benjamin.r.defnet@intel.com>
 *    Rajesh Poornachandran <rajesh.poornachandran@intel.com>
 * Massively reworked
 *    Alan Cox <alan@linux.intel.com>
 */
#ifndef _PSB_POWERMGMT_H_
#define _PSB_POWERMGMT_H_

#include <linux/pci.h>

struct device;
struct drm_device;

void gma_power_init(struct drm_device *dev);
void gma_power_uninit(struct drm_device *dev);

/*
 * The kernel bus power management  will call these functions
 */
int gma_power_suspend(struct device *dev);
int gma_power_resume(struct device *dev);

/*
 * These are the functions the driver should use to wrap all hw access
 * (i.e. register reads and writes)
 */
bool gma_power_begin(struct drm_device *dev, bool force);
void gma_power_end(struct drm_device *dev);

#endif /*_PSB_POWERMGMT_H_*/
