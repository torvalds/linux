// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Extracted from cputable.c
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *  Copyright (C) 2005 Stephen Rothwell, IBM Corporation
 */

#include <linux/export.h>
#include <linux/cache.h>

#include <asm/firmware.h>

unsigned long powerpc_firmware_features __read_mostly;
EXPORT_SYMBOL_GPL(powerpc_firmware_features);
