/*
 * Copyright (C) 2010 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General
 * Public License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 * The full GNU General Public License is included in this
 * distribution in the file called COPYING.
 */


#ifndef _RAR_REGISTER_H
#define _RAR_REGISTER_H

# include <linux/types.h>

/* following are used both in drivers as well as user space apps */
enum RAR_type {
	RAR_TYPE_VIDEO = 0,
	RAR_TYPE_AUDIO,
	RAR_TYPE_IMAGE,
	RAR_TYPE_DATA
};

#ifdef __KERNEL__

/* PCI device id for controller */
#define PCI_RAR_DEVICE_ID 0x4110

/* The register_rar function is to used by other device drivers
 * to ensure that this driver is ready. As we cannot be sure of
 * the compile/execute order of dirvers in ther kernel, it is
 * best to give this driver a callback function to call when
 * it is ready to give out addresses. The callback function
 * would have those steps that continue the initialization of
 * a driver that do require a valid RAR address. One of those
 * steps would be to call get_rar_address()
 * This function return 0 on success an -1 on failure.
 */
int register_rar(int (*callback)(void *yourparameter), void *yourparameter);

/* The get_rar_address function is used by other device drivers
 * to obtain RAR address information on a RAR. It takes two
 * parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar for which you wish to retrieve
 * the address information.
 * Values can be 0,1, or 2.
 *
 * struct RAR_address_struct is a pointer to a place to which the function
 * can return the address structure for the RAR.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int rar_get_address(int rar_index,
		dma_addr_t *start_address,
		dma_addr_t *end_address);

/* The lock_rar function is ued by other device drivers to lock an RAR.
 * once an RAR is locked, it stays locked until the next system reboot.
 * The function takes one parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar that you want to lock.
 * Values can be 0,1, or 2.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
int rar_lock(int rar_index);

#endif  /* __KERNEL__ */
#endif  /* _RAR_REGISTER_H */
