/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * all header files
 */

#ifndef __AUFS_H__
#define __AUFS_H__

#ifdef __KERNEL__

#define AuStub(type, name, body, ...) \
	static inline type name(__VA_ARGS__) { body; }

#define AuStubVoid(name, ...) \
	AuStub(void, name, , __VA_ARGS__)
#define AuStubInt0(name, ...) \
	AuStub(int, name, return 0, __VA_ARGS__)

#include "debug.h"

#include "branch.h"
#include "cpup.h"
#include "dcsub.h"
#include "dbgaufs.h"
#include "dentry.h"
#include "dir.h"
#include "dynop.h"
#include "file.h"
#include "fstype.h"
#include "inode.h"
#include "loop.h"
#include "module.h"
#include "opts.h"
#include "rwsem.h"
#include "spl.h"
#include "super.h"
#include "sysaufs.h"
#include "vfsub.h"
#include "whout.h"
#include "wkq.h"

#endif /* __KERNEL__ */
#endif /* __AUFS_H__ */
