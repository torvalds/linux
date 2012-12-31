/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#ifndef _UMP_KERNEL_LINUX_MEM_H_
#define _UMP_KERNEL_LINUX_MEM_H_


int umpp_linux_mmap(struct file * filp, struct vm_area_struct * vma);

#endif /* _UMP_KERNEL_LINUX_MEM_H_ */
