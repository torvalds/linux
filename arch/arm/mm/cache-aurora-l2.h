/*
 * AURORA shared L2 cache controller support
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARM_HARDWARE_AURORA_L2_H
#define __ASM_ARM_HARDWARE_AURORA_L2_H

#define AURORA_SYNC_REG		    0x700
#define AURORA_RANGE_BASE_ADDR_REG  0x720
#define AURORA_FLUSH_PHY_ADDR_REG   0x7f0
#define AURORA_INVAL_RANGE_REG	    0x774
#define AURORA_CLEAN_RANGE_REG	    0x7b4
#define AURORA_FLUSH_RANGE_REG	    0x7f4

#define AURORA_ACR_REPLACEMENT_OFFSET	    27
#define AURORA_ACR_REPLACEMENT_MASK	     \
	(0x3 << AURORA_ACR_REPLACEMENT_OFFSET)
#define AURORA_ACR_REPLACEMENT_TYPE_WAYRR    \
	(0 << AURORA_ACR_REPLACEMENT_OFFSET)
#define AURORA_ACR_REPLACEMENT_TYPE_LFSR     \
	(1 << AURORA_ACR_REPLACEMENT_OFFSET)
#define AURORA_ACR_REPLACEMENT_TYPE_SEMIPLRU \
	(3 << AURORA_ACR_REPLACEMENT_OFFSET)

#define AURORA_ACR_FORCE_WRITE_POLICY_OFFSET	0
#define AURORA_ACR_FORCE_WRITE_POLICY_MASK	\
	(0x3 << AURORA_ACR_FORCE_WRITE_POLICY_OFFSET)
#define AURORA_ACR_FORCE_WRITE_POLICY_DIS	\
	(0 << AURORA_ACR_FORCE_WRITE_POLICY_OFFSET)
#define AURORA_ACR_FORCE_WRITE_BACK_POLICY	\
	(1 << AURORA_ACR_FORCE_WRITE_POLICY_OFFSET)
#define AURORA_ACR_FORCE_WRITE_THRO_POLICY	\
	(2 << AURORA_ACR_FORCE_WRITE_POLICY_OFFSET)

#define MAX_RANGE_SIZE		1024

#define AURORA_WAY_SIZE_SHIFT	2

#define AURORA_CTRL_FW		0x100

/* chose a number outside L2X0_CACHE_ID_PART_MASK to be sure to make
 * the distinction between a number coming from hardware and a number
 * coming from the device tree */
#define AURORA_CACHE_ID	       0x100

#endif /* __ASM_ARM_HARDWARE_AURORA_L2_H */
