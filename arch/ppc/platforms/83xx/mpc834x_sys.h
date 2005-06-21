/*
 * arch/ppc/platforms/83xx/mpc834x_sys.h
 *
 * MPC834X SYS common board definitions
 *
 * Maintainer: Kumar Gala <kumar.gala@freescale.com>
 *
 * Copyright 2005 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#ifndef __MACH_MPC83XX_SYS_H__
#define __MACH_MPC83XX_SYS_H__

#include <linux/config.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <syslib/ppc83xx_setup.h>
#include <asm/ppcboot.h>

#define VIRT_IMMRBAR		((uint)0xfe000000)

#define BCSR_PHYS_ADDR		((uint)0xf8000000)
#define BCSR_SIZE		((uint)(32 * 1024))

#define BCSR_MISC_REG2_OFF	0x07
#define BCSR_MISC_REG2_PORESET	0x01

#define BCSR_MISC_REG3_OFF	0x08
#define BCSR_MISC_REG3_CNFLOCK	0x80

#ifdef CONFIG_PCI
/* PCI interrupt controller */
#define PIRQA        MPC83xx_IRQ_IRQ4
#define PIRQB        MPC83xx_IRQ_IRQ5
#define PIRQC        MPC83xx_IRQ_IRQ6
#define PIRQD        MPC83xx_IRQ_IRQ7

#define MPC834x_SYS_PCI1_LOWER_IO        0x00000000
#define MPC834x_SYS_PCI1_UPPER_IO        0x00ffffff

#define MPC834x_SYS_PCI1_LOWER_MEM       0x80000000
#define MPC834x_SYS_PCI1_UPPER_MEM       0x9fffffff

#define MPC834x_SYS_PCI1_IO_BASE         0xe2000000
#define MPC834x_SYS_PCI1_MEM_OFFSET      0x00000000

#define MPC834x_SYS_PCI1_IO_SIZE         0x01000000
#endif /* CONFIG_PCI */

#endif                /* __MACH_MPC83XX_SYS_H__ */
