/*
 * Driver frontend for IBM Power 842 compression accelerator
 *
 * Copyright (C) 2015 Dan Streetman, IBM Corp
 *
 * Designer of the Power data compression engine:
 *   Bulent Abali <abali@us.ibm.com>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "nx-842.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dan Streetman <ddstreet@ieee.org>");
MODULE_DESCRIPTION("842 H/W Compression driver for IBM Power processors");

/**
 * nx842_constraints
 *
 * This provides the driver's constraints.  Different nx842 implementations
 * may have varying requirements.  The constraints are:
 *   @alignment:	All buffers should be aligned to this
 *   @multiple:		All buffer lengths should be a multiple of this
 *   @minimum:		Buffer lengths must not be less than this amount
 *   @maximum:		Buffer lengths must not be more than this amount
 *
 * The constraints apply to all buffers and lengths, both input and output,
 * for both compression and decompression, except for the minimum which
 * only applies to compression input and decompression output; the
 * compressed data can be less than the minimum constraint.  It can be
 * assumed that compressed data will always adhere to the multiple
 * constraint.
 *
 * The driver may succeed even if these constraints are violated;
 * however the driver can return failure or suffer reduced performance
 * if any constraint is not met.
 */
int nx842_constraints(struct nx842_constraints *c)
{
	memcpy(c, nx842_platform_driver()->constraints, sizeof(*c));
	return 0;
}
EXPORT_SYMBOL_GPL(nx842_constraints);

/**
 * nx842_workmem_size
 *
 * Get the amount of working memory the driver requires.
 */
size_t nx842_workmem_size(void)
{
	return nx842_platform_driver()->workmem_size;
}
EXPORT_SYMBOL_GPL(nx842_workmem_size);

int nx842_compress(const unsigned char *in, unsigned int ilen,
		   unsigned char *out, unsigned int *olen, void *wmem)
{
	return nx842_platform_driver()->compress(in, ilen, out, olen, wmem);
}
EXPORT_SYMBOL_GPL(nx842_compress);

int nx842_decompress(const unsigned char *in, unsigned int ilen,
		     unsigned char *out, unsigned int *olen, void *wmem)
{
	return nx842_platform_driver()->decompress(in, ilen, out, olen, wmem);
}
EXPORT_SYMBOL_GPL(nx842_decompress);

static __init int nx842_init(void)
{
	request_module("nx-compress-powernv");
	request_module("nx-compress-pseries");

	/* we prevent loading if there's no platform driver, and we get the
	 * module that set it so it won't unload, so we don't need to check
	 * if it's set in any of the above functions
	 */
	if (!nx842_platform_driver_get()) {
		pr_err("no nx842 driver found.\n");
		return -ENODEV;
	}

	return 0;
}
module_init(nx842_init);

static void __exit nx842_exit(void)
{
	nx842_platform_driver_put();
}
module_exit(nx842_exit);
