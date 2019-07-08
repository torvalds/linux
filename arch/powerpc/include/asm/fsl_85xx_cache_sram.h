/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2009 Freescale Semiconductor, Inc.
 *
 * Cache SRAM handling for QorIQ platform
 *
 * Author: Vivek Mahajan <vivek.mahajan@freescale.com>

 * This file is derived from the original work done
 * by Sylvain Munaut for the Bestcomm SRAM allocator.
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
