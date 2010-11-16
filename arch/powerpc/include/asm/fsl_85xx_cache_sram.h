/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * Cache SRAM handling for QorIQ platform
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>

 * This file is derived from the original work done
 * by Sylvain Munaut for the Bestcomm SRAM allocator.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_POWERPC_FSL_85XX_CACHE_SRAM_H__
#define __ASM_POWERPC_FSL_85XX_CACHE_SRAM_H__

#include <asm/rheap.h>
#include <linux/spinlock.h>

/*
 * Cache-SRAM
 */

struct mpc85xx_cache_sram {
	phys_addr_t base_phys;
	void *base_virt;
	unsigned int size;
	rh_info_t *rh;
	spinlock_t lock;
};

extern void mpc85xx_cache_sram_free(void *ptr);
extern void *mpc85xx_cache_sram_alloc(unsigned int size,
				  phys_addr_t *phys, unsigned int align);

#endif /* __AMS_POWERPC_FSL_85XX_CACHE_SRAM_H__ */
