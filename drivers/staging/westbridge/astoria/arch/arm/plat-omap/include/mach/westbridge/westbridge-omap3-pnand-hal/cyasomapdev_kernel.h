/* Cypress Antioch OMAP KERNEL file (cyanomapdev_kernel.h)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor,
## Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef __CY_AS_OMAP_DEV_KERNEL_H__
#define __CY_AS_OMAP_DEV_KERNEL_H__


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/completion.h>

/* include does not seem to work
 * moving for patch submission
#include <mach/gpmc.h>
*/
#include <linux/../../arch/arm/plat-omap/include/plat/gpmc.h>

/*
 * Constants
 */
#define CY_AS_OMAP_KERNEL_HAL_SIG		(0x1441)


/*
 * Data structures
 */
typedef struct cy_as_omap_dev_kernel {
	/* This is the signature for this data structure */
	unsigned int m_sig;

	/* Address base of Antioch Device */
	void *m_addr_base;

	/* This is a pointer to the next Antioch device in the system */
	struct cy_as_omap_dev_kernel *m_next_p;

	/* This is for thread sync */
	struct completion thread_complete;

	/* This is for thread to wait for interrupts */
	cy_as_hal_sleep_channel thread_sc;

	/* This is for thread to exit upon StopOmapKernel */
	int thread_flag; /* set 1 to exit */

	int dma_ch;

	/* This is for dma sync */
	struct completion dma_complete;
} cy_as_omap_dev_kernel;

#endif

/*[]*/
