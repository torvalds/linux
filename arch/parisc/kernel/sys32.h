/* 
 *    Copyright (C) 2002 Richard Hirst <rhirst at parisc-linux.org>
 *    Copyright (C) 2003 James Bottomley <jejb at parisc-linux.org>
 *    Copyright (C) 2003 Randolph Chung <tausq with parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _PARISC64_KERNEL_SYS32_H
#define _PARISC64_KERNEL_SYS32_H

#include <linux/compat.h>

/* Call a kernel syscall which will use kernel space instead of user
 * space for its copy_to/from_user.
 */
#define KERNEL_SYSCALL(ret, syscall, args...) \
{ \
    mm_segment_t old_fs = get_fs(); \
    set_fs(KERNEL_DS); \
    ret = syscall(args); \
    set_fs (old_fs); \
}

#endif
