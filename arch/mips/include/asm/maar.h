/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2014 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
 */

#ifndef __MIPS_ASM_MIPS_MAAR_H__
#define __MIPS_ASM_MIPS_MAAR_H__

#include <asm/hazards.h>
#include <asm/mipsregs.h>

/**
 * platform_maar_init() - perform platform-level MAAR configuration
 * @num_pairs:	The number of MAAR pairs present in the system.
 *
 * Platforms should implement this function such that it configures as many
 * MAAR pairs as required, from 0 up to the maximum of num_pairs-1, and returns
 * the number that were used. Any further MAARs will be configured to be
 * invalid. The default implementation of this function will simply indicate
 * that it has configured 0 MAAR pairs.
 *
 * Return:	The number of MAAR pairs configured.
 */
unsigned platform_maar_init(unsigned num_pairs);

/**
 * write_maar_pair() - write to a pair of MAARs
 * @idx:	The index of the pair (ie. use MAARs idx*2 & (idx*2)+1).
 * @lower:	The lowest address that the MAAR pair will affect. Must be
 *		aligned to a 2^16 byte boundary.
 * @upper:	The highest address that the MAAR pair will affect. Must be
 *		aligned to one byte before a 2^16 byte boundary.
 * @attrs:	The accessibility attributes to program, eg. MIPS_MAAR_S. The
 *		MIPS_MAAR_VL attribute will automatically be set.
 *
 * Program the pair of MAAR registers specified by idx to apply the attributes
 * specified by attrs to the range of addresses from lower to higher.
 */
static inline void write_maar_pair(unsigned idx, phys_addr_t lower,
				   phys_addr_t upper, unsigned attrs)
{
	/* Addresses begin at bit 16, but are shifted right 4 bits */
	BUG_ON(lower & (0xffff | ~(MIPS_MAAR_ADDR << 4)));
	BUG_ON(((upper & 0xffff) != 0xffff)
		|| ((upper & ~0xffffull) & ~(MIPS_MAAR_ADDR << 4)));

	/* Automatically set MIPS_MAAR_VL */
	attrs |= MIPS_MAAR_VL;

	/* Write the upper address & attributes (only MIPS_MAAR_VL matters) */
	write_c0_maari(idx << 1);
	back_to_back_c0_hazard();
	write_c0_maar(((upper >> 4) & MIPS_MAAR_ADDR) | attrs);
	back_to_back_c0_hazard();

	/* Write the lower address & attributes */
	write_c0_maari((idx << 1) | 0x1);
	back_to_back_c0_hazard();
	write_c0_maar((lower >> 4) | attrs);
	back_to_back_c0_hazard();
}

/**
 * maar_init() - initialise MAARs
 *
 * Performs initialisation of MAARs for the current CPU, making use of the
 * platforms implementation of platform_maar_init where necessary and
 * duplicating the setup it provides on secondary CPUs.
 */
extern void maar_init(void);

/**
 * struct maar_config - MAAR configuration data
 * @lower:	The lowest address that the MAAR pair will affect. Must be
 *		aligned to a 2^16 byte boundary.
 * @upper:	The highest address that the MAAR pair will affect. Must be
 *		aligned to one byte before a 2^16 byte boundary.
 * @attrs:	The accessibility attributes to program, eg. MIPS_MAAR_S. The
 *		MIPS_MAAR_VL attribute will automatically be set.
 *
 * Describes the configuration of a pair of Memory Accessibility Attribute
 * Registers - applying attributes from attrs to the range of physical
 * addresses from lower to upper inclusive.
 */
struct maar_config {
	phys_addr_t lower;
	phys_addr_t upper;
	unsigned attrs;
};

/**
 * maar_config() - configure MAARs according to provided data
 * @cfg:	Pointer to an array of struct maar_config.
 * @num_cfg:	The number of structs in the cfg array.
 * @num_pairs:	The number of MAAR pairs present in the system.
 *
 * Configures as many MAARs as are present and specified in the cfg
 * array with the values taken from the cfg array.
 *
 * Return:	The number of MAAR pairs configured.
 */
static inline unsigned maar_config(const struct maar_config *cfg,
				   unsigned num_cfg, unsigned num_pairs)
{
	unsigned i;

	for (i = 0; i < min(num_cfg, num_pairs); i++)
		write_maar_pair(i, cfg[i].lower, cfg[i].upper, cfg[i].attrs);

	return i;
}

#endif /* __MIPS_ASM_MIPS_MAAR_H__ */
