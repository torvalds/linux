/*
 * arch/score/kernel/syscall.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <asm/syscalls.h>

asmlinkage long 
sys_mmap2(unsigned long addr, unsigned long len, unsigned long prot,
	  unsigned long flags, unsigned long fd, unsigned long pgoff)
{
	return sys_mmap_pgoff(addr, len, prot, flags, fd, pgoff);
}

asmlinkage long
sys_mmap(unsigned long addr, unsigned long len, unsigned long prot,
	unsigned long flags, unsigned long fd, off_t offset)
{
	if (unlikely(offset & ~PAGE_MASK))
		return -EINVAL;
	return sys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}
