/*
 * arch/ppc/platforms/85xx/mpc85xx_cds_common.h
 *
 * MPC85xx CDS board definitions
 *
 * Maintainer: Kumar Gala <galak@kernel.crashing.org>
 *
 * Copyright 2004 Freescale Semiconductor, Inc
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_MPC85XX_CDS_H__
#define __MACH_MPC85XX_CDS_H__

/* CADMUS info */
#define CADMUS_BASE (0xf8004000)
#define CADMUS_SIZE (256)
#define CM_VER	(0)
#define CM_CSR	(1)
#define CM_RST	(2)

/* CDS NVRAM/RTC */
#define CDS_RTC_ADDR	(0xf8000000)
#define CDS_RTC_SIZE	(8 * 1024)

/* PCI interrupt controller */
#define PIRQ0A			MPC85xx_IRQ_EXT0
#define PIRQ0B			MPC85xx_IRQ_EXT1
#define PIRQ0C			MPC85xx_IRQ_EXT2
#define PIRQ0D			MPC85xx_IRQ_EXT3
#define PIRQ1A			MPC85xx_IRQ_EXT11

#define NR_8259_INTS		16
#define CPM_IRQ_OFFSET		NR_8259_INTS

#define MPC85xx_OPENPIC_IRQ_OFFSET	80

#endif /* __MACH_MPC85XX_CDS_H__ */
