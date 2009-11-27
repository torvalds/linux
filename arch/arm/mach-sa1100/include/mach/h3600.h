/*
 *
 * Definitions for H3600 Handheld Computer
 *
 * Copyright 2000 Compaq Computer Corporation.
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * COMPAQ COMPUTER CORPORATION MAKES NO WARRANTIES, EXPRESSED OR IMPLIED,
 * AS TO THE USEFULNESS OR CORRECTNESS OF THIS CODE OR ITS
 * FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 * Author: Jamey Hicks.
 *
 * History:
 *
 * 2001-10-??	Andrew Christian   Added support for iPAQ H3800
 *
 */

#ifndef _INCLUDE_H3600_H_
#define _INCLUDE_H3600_H_

/* Physical memory regions corresponding to chip selects */
#define H3600_EGPIO_PHYS	(SA1100_CS5_PHYS + 0x01000000)
#define H3600_BANK_2_PHYS	SA1100_CS2_PHYS
#define H3600_BANK_4_PHYS	SA1100_CS4_PHYS

/* Virtual memory regions corresponding to chip selects 2 & 4 (used on sleeves) */
#define H3600_EGPIO_VIRT	0xf0000000
#define H3600_BANK_2_VIRT	0xf1000000
#define H3600_BANK_4_VIRT	0xf3800000

#endif /* _INCLUDE_H3600_H_ */
