/*
 *  Extracted from cputable.c
 *
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *  Copyright (C) 2005 Stephen Rothwell, IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>

#include <asm/firmware.h>

unsigned long powerpc_firmware_features;
EXPORT_SYMBOL_GPL(powerpc_firmware_features);
