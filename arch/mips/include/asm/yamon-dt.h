/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_YAMON_DT_H__
#define __MIPS_ASM_YAMON_DT_H__

#include <linux/types.h>

/**
 * struct yamon_mem_region - Represents a contiguous range of physical RAM.
 * @start:	Start physical address.
 * @size:	Maximum size of region.
 * @discard:	Length of additional memory to discard after the region.
 */
struct yamon_mem_region {
	phys_addr_t	start;
	phys_addr_t	size;
	phys_addr_t	discard;
};

/**
 * yamon_dt_append_cmdline() - Append YAMON-provided command line to /chosen
 * @fdt: the FDT blob
 *
 * Write the YAMON-provided command line to the bootargs property of the
 * /chosen node in @fdt.
 *
 * Return: 0 on success, else -errno
 */
extern __init int yamon_dt_append_cmdline(void *fdt);

/**
 * yamon_dt_append_memory() - Append YAMON-provided memory info to /memory
 * @fdt:	the FDT blob
 * @regions:	zero size terminated array of physical memory regions
 *
 * Generate a /memory node in @fdt based upon memory size information provided
 * by YAMON in its environment and the @regions array.
 *
 * Return: 0 on success, else -errno
 */
extern __init int yamon_dt_append_memory(void *fdt,
					const struct yamon_mem_region *regions);

/**
 * yamon_dt_serial_config() - Append YAMON-provided serial config to /chosen
 * @fdt: the FDT blob
 *
 * Generate a stdout-path property in the /chosen node of @fdt, based upon
 * information provided in the YAMON environment about the UART configuration
 * of the system.
 *
 * Return: 0 on success, else -errno
 */
extern __init int yamon_dt_serial_config(void *fdt);

#endif /* __MIPS_ASM_YAMON_DT_H__ */
