/*
 * Copyright (C) 2015 Imagination Technologies
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_DEBUG_H__
#define __MIPS_ASM_DEBUG_H__

#include <linux/dcache.h>

/*
 * mips_debugfs_dir corresponds to the "mips" directory at the top level
 * of the DebugFS hierarchy. MIPS-specific DebugFS entires should be
 * placed beneath this directory.
 */
extern struct dentry *mips_debugfs_dir;

#endif /* __MIPS_ASM_DEBUG_H__ */
