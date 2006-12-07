/*
 * Header file for Freescale MPC52xx Interrupt controller
 *
 * Copyright (C) 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __POWERPC_SYSDEV_MPC52xx_PIC_H__
#define __POWERPC_SYSDEV_MPC52xx_PIC_H__

#include <asm/types.h>


/* HW IRQ mapping */
#define MPC52xx_IRQ_L1_CRIT	(0)
#define MPC52xx_IRQ_L1_MAIN	(1)
#define MPC52xx_IRQ_L1_PERP	(2)
#define MPC52xx_IRQ_L1_SDMA	(3)

#define MPC52xx_IRQ_L1_OFFSET   (6)
#define MPC52xx_IRQ_L1_MASK     (0x00c0)

#define MPC52xx_IRQ_L2_OFFSET   (0)
#define MPC52xx_IRQ_L2_MASK     (0x003f)

#define MPC52xx_IRQ_HIGHTESTHWIRQ (0xd0)


/* Interrupt controller Register set */
struct mpc52xx_intr {
	u32 per_mask;		/* INTR + 0x00 */
	u32 per_pri1;		/* INTR + 0x04 */
	u32 per_pri2;		/* INTR + 0x08 */
	u32 per_pri3;		/* INTR + 0x0c */
	u32 ctrl;		/* INTR + 0x10 */
	u32 main_mask;		/* INTR + 0x14 */
	u32 main_pri1;		/* INTR + 0x18 */
	u32 main_pri2;		/* INTR + 0x1c */
	u32 reserved1;		/* INTR + 0x20 */
	u32 enc_status;		/* INTR + 0x24 */
	u32 crit_status;	/* INTR + 0x28 */
	u32 main_status;	/* INTR + 0x2c */
	u32 per_status;		/* INTR + 0x30 */
	u32 reserved2;		/* INTR + 0x34 */
	u32 per_error;		/* INTR + 0x38 */
};

#endif /* __POWERPC_SYSDEV_MPC52xx_PIC_H__ */

