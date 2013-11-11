/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_SAMSUNG_MFC_H
#define __PLAT_SAMSUNG_MFC_H __FILE__

struct s5p_mfc_dt_meminfo {
	unsigned long	loff;
	unsigned long	lsize;
	unsigned long	roff;
	unsigned long	rsize;
	char		*compatible;
};

/**
 * s5p_mfc_reserve_mem - function to early reserve memory for MFC driver
 * @rbase:	base address for MFC 'right' memory interface
 * @rsize:	size of the memory reserved for MFC 'right' interface
 * @lbase:	base address for MFC 'left' memory interface
 * @lsize:	size of the memory reserved for MFC 'left' interface
 *
 * This function reserves system memory for both MFC device memory
 * interfaces and registers it to respective struct device entries as
 * coherent memory.
 */
void __init s5p_mfc_reserve_mem(phys_addr_t rbase, unsigned int rsize,
				phys_addr_t lbase, unsigned int lsize);

int __init s5p_fdt_find_mfc_mem(unsigned long node, const char *uname,
				int depth, void *data);

#endif /* __PLAT_SAMSUNG_MFC_H */
