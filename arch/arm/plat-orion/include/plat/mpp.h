/*
 * arch/arm/plat-orion/include/plat/mpp.h
 *
 * Marvell Orion SoC MPP handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_MPP_H
#define __PLAT_MPP_H

#define MPP_NUM(x)	((x) & 0xff)
#define MPP_SEL(x)	(((x) >> 8) & 0xf)

/* This is the generic MPP macro, without any variant information.
   Each machine architecture is expected to extend this with further
   bit fields indicating which MPP configurations are valid for a
   specific variant. */

#define GENERIC_MPP(_num, _sel, _in, _out) ( \
	/* MPP number */		((_num) & 0xff) | \
	/* MPP select value */		(((_sel) & 0xf) << 8) | \
	/* may be input signal */	((!!(_in)) << 12) | \
	/* may be output signal */	((!!(_out)) << 13))

#define MPP_INPUT_MASK		GENERIC_MPP(0, 0x0, 1, 0)
#define MPP_OUTPUT_MASK		GENERIC_MPP(0, 0x0, 0, 1)

void __init orion_mpp_conf(unsigned int *mpp_list, unsigned int variant_mask,
			   unsigned int mpp_max, void __iomem *dev_bus);

#endif
