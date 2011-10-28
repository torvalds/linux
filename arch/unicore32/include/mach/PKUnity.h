/*
 * linux/arch/unicore32/include/mach/PKUnity.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Be sure that virtual mapping is defined right */
#ifndef __MACH_PUV3_HARDWARE_H__
#error You must include hardware.h not PKUnity.h
#endif

#include "bitfield.h"

/*
 * Memory Definitions
 */
#define PKUNITY_SDRAM_BASE		0x00000000 /* 0x00000000 - 0x7FFFFFFF 2GB */
#define PKUNITY_MMIO_BASE		0x80000000 /* 0x80000000 - 0xFFFFFFFF 2GB */

/*
 * PKUNITY System Bus Addresses (PCI): 0x80000000 - 0xBFFFFFFF (1GB)
 * 0x80000000 - 0x8000000B 12B    PCI Configuration regs
 * 0x80010000 - 0x80010250 592B   PCI Bridge Base
 * 0x80030000 - 0x8003FFFF 64KB   PCI Legacy IO
 * 0x90000000 - 0x97FFFFFF 128MB  PCI AHB-PCI MEM-mapping
 * 0x98000000 - 0x9FFFFFFF 128MB  PCI PCI-AHB MEM-mapping
 */
#define PKUNITY_PCI_BASE		io_p2v(0x80000000) /* 0x80000000 - 0xBFFFFFFF 1GB */
#include "regs-pci.h"

#define PKUNITY_PCICFG_BASE		(PKUNITY_PCI_BASE + 0x0)
#define PKUNITY_PCIBRI_BASE		(PKUNITY_PCI_BASE + 0x00010000)
#define PKUNITY_PCILIO_BASE		(PKUNITY_PCI_BASE + 0x00030000)
#define PKUNITY_PCIMEM_BASE		(PKUNITY_PCI_BASE + 0x10000000)
#define PKUNITY_PCIAHB_BASE		(PKUNITY_PCI_BASE + 0x18000000)

/*
 * PKUNITY System Bus Addresses (AHB): 0xC0000000 - 0xEDFFFFFF (640MB)
 */
#define PKUNITY_AHB_BASE		io_p2v(0xC0000000)

/* AHB-0 is DDR2 SDRAM */
/* AHB-1 is PCI Space */
#define PKUNITY_ARBITER_BASE		(PKUNITY_AHB_BASE + 0x000000) /* AHB-2 */
#define PKUNITY_DDR2CTRL_BASE		(PKUNITY_AHB_BASE + 0x100000) /* AHB-3 */
#define PKUNITY_DMAC_BASE		(PKUNITY_AHB_BASE + 0x200000) /* AHB-4 */
#include "regs-dmac.h"
#define PKUNITY_UMAL_BASE		(PKUNITY_AHB_BASE + 0x300000) /* AHB-5 */
#include "regs-umal.h"
#define PKUNITY_USB_BASE		(PKUNITY_AHB_BASE + 0x400000) /* AHB-6 */
#define PKUNITY_SATA_BASE		(PKUNITY_AHB_BASE + 0x500000) /* AHB-7 */
#define PKUNITY_SMC_BASE		(PKUNITY_AHB_BASE + 0x600000) /* AHB-8 */
/* AHB-9 is for APB bridge */
#define PKUNITY_MME_BASE		(PKUNITY_AHB_BASE + 0x700000) /* AHB-10 */
#define PKUNITY_UNIGFX_BASE		(PKUNITY_AHB_BASE + 0x800000) /* AHB-11 */
#include "regs-unigfx.h"
#define PKUNITY_NAND_BASE		(PKUNITY_AHB_BASE + 0x900000) /* AHB-12 */
#include "regs-nand.h"
#define PKUNITY_H264D_BASE		(PKUNITY_AHB_BASE + 0xA00000) /* AHB-13 */
#define PKUNITY_H264E_BASE		(PKUNITY_AHB_BASE + 0xB00000) /* AHB-14 */

/*
 * PKUNITY Peripheral Bus Addresses (APB): 0xEE000000 - 0xEFFFFFFF (128MB)
 */
#define PKUNITY_APB_BASE		io_p2v(0xEE000000)

#define PKUNITY_UART0_BASE		(PKUNITY_APB_BASE + 0x000000) /* APB-0 */
#define PKUNITY_UART1_BASE		(PKUNITY_APB_BASE + 0x100000) /* APB-1 */
#include "regs-uart.h"
#define PKUNITY_I2C_BASE		(PKUNITY_APB_BASE + 0x200000) /* APB-2 */
#include "regs-i2c.h"
#define PKUNITY_SPI_BASE		(PKUNITY_APB_BASE + 0x300000) /* APB-3 */
#include "regs-spi.h"
#define PKUNITY_AC97_BASE		(PKUNITY_APB_BASE + 0x400000) /* APB-4 */
#include "regs-ac97.h"
#define PKUNITY_GPIO_BASE		(PKUNITY_APB_BASE + 0x500000) /* APB-5 */
#include "regs-gpio.h"
#define PKUNITY_INTC_BASE		(PKUNITY_APB_BASE + 0x600000) /* APB-6 */
#include "regs-intc.h"
#define PKUNITY_RTC_BASE		(PKUNITY_APB_BASE + 0x700000) /* APB-7 */
#include "regs-rtc.h"
#define PKUNITY_OST_BASE		(PKUNITY_APB_BASE + 0x800000) /* APB-8 */
#include "regs-ost.h"
#define PKUNITY_RESETC_BASE		(PKUNITY_APB_BASE + 0x900000) /* APB-9 */
#include "regs-resetc.h"
#define PKUNITY_PM_BASE			(PKUNITY_APB_BASE + 0xA00000) /* APB-10 */
#include "regs-pm.h"
#define PKUNITY_PS2_BASE		(PKUNITY_APB_BASE + 0xB00000) /* APB-11 */
#include "regs-ps2.h"
#define PKUNITY_SDC_BASE		(PKUNITY_APB_BASE + 0xC00000) /* APB-12 */
#include "regs-sdc.h"

