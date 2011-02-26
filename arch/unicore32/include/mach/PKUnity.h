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
#define PKUNITY_IOSPACE_BASE            0x80000000 /* 0x80000000 - 0xFFFFFFFF 2GB */
#define PKUNITY_PCI_BASE		0x80000000 /* 0x80000000 - 0xBFFFFFFF 1GB */
#include "regs-pci.h"
#define PKUNITY_BOOT_ROM2_BASE		0xF4000000 /* 0xF4000000 - 0xF7FFFFFF 64MB */
#define PKUNITY_BOOT_SRAM2_BASE		0xF8000000 /* 0xF8000000 - 0xFBFFFFFF 64MB */
#define PKUNITY_BOOT_FLASH_BASE		0xFC000000 /* 0xFC000000 - 0xFFFFFFFF 64MB */

/*
 * PKUNITY Memory Map Addresses: 0x0D000000 - 0x0EFFFFFF (32MB)
 */
#define PKUNITY_UVC_MMAP_BASE		0x0D000000 /* 0x0D000000 - 0x0DFFFFFF 16MB */
#define PKUNITY_UVC_MMAP_SIZE		0x01000000 /* 16MB */
#define PKUNITY_UNIGFX_MMAP_BASE        0x0E000000 /* 0x0E000000 - 0x0EFFFFFF 16MB */
#define PKUNITY_UNIGFX_MMAP_SIZE        0x01000000 /* 16MB */

/*
 * PKUNITY System Bus Addresses (PCI): 0x80000000 - 0xBFFFFFFF (1GB)
 */
/* PCI Configuration regs */
#define PKUNITY_PCICFG_BASE             0x80000000 /* 0x80000000 - 0x8000000B 12B */
/* PCI Bridge Base */
#define PKUNITY_PCIBRI_BASE             0x80010000 /* 0x80010000 - 0x80010250 592B */
/* PCI Legacy IO */
#define PKUNITY_PCILIO_BASE             0x80030000 /* 0x80030000 - 0x8003FFFF 64KB */
/* PCI AHB-PCI MEM-mapping */
#define PKUNITY_PCIMEM_BASE             0x90000000 /* 0x90000000 - 0x97FFFFFF 128MB */
/* PCI PCI-AHB MEM-mapping */
#define PKUNITY_PCIAHB_BASE             0x98000000 /* 0x98000000 - 0x9FFFFFFF 128MB */

/*
 * PKUNITY System Bus Addresses (AHB): 0xC0000000 - 0xEDFFFFFF (640MB)
 */
/* AHB-0 is DDR2 SDRAM */
/* AHB-1 is PCI Space */
#define PKUNITY_ARBITER_BASE		0xC0000000 /* AHB-2 */
#define PKUNITY_DDR2CTRL_BASE		0xC0100000 /* AHB-3 */
#define PKUNITY_DMAC_BASE		0xC0200000 /* AHB-4 */
#include "regs-dmac.h"
#define PKUNITY_UMAL_BASE		0xC0300000 /* AHB-5 */
#include "regs-umal.h"
#define PKUNITY_USB_BASE		0xC0400000 /* AHB-6 */
#define PKUNITY_SATA_BASE		0xC0500000 /* AHB-7 */
#define PKUNITY_SMC_BASE		0xC0600000 /* AHB-8 */
/* AHB-9 is for APB bridge */
#define PKUNITY_MME_BASE		0xC0700000 /* AHB-10 */
#define PKUNITY_UNIGFX_BASE		0xC0800000 /* AHB-11 */
#include "regs-unigfx.h"
#define PKUNITY_NAND_BASE		0xC0900000 /* AHB-12 */
#include "regs-nand.h"
#define PKUNITY_H264D_BASE		0xC0A00000 /* AHB-13 */
#define PKUNITY_H264E_BASE		0xC0B00000 /* AHB-14 */

/*
 * PKUNITY Peripheral Bus Addresses (APB): 0xEE000000 - 0xEFFFFFFF (128MB)
 */
#define PKUNITY_UART0_BASE		0xEE000000 /* APB-0 */
#define PKUNITY_UART1_BASE		0xEE100000 /* APB-1 */
#include "regs-uart.h"
#define PKUNITY_I2C_BASE		0xEE200000 /* APB-2 */
#include "regs-i2c.h"
#define PKUNITY_SPI_BASE		0xEE300000 /* APB-3 */
#include "regs-spi.h"
#define PKUNITY_AC97_BASE		0xEE400000 /* APB-4 */
#include "regs-ac97.h"
#define PKUNITY_GPIO_BASE		0xEE500000 /* APB-5 */
#include "regs-gpio.h"
#define PKUNITY_INTC_BASE		0xEE600000 /* APB-6 */
#include "regs-intc.h"
#define PKUNITY_RTC_BASE		0xEE700000 /* APB-7 */
#include "regs-rtc.h"
#define PKUNITY_OST_BASE		0xEE800000 /* APB-8 */
#include "regs-ost.h"
#define PKUNITY_RESETC_BASE		0xEE900000 /* APB-9 */
#include "regs-resetc.h"
#define PKUNITY_PM_BASE			0xEEA00000 /* APB-10 */
#include "regs-pm.h"
#define PKUNITY_PS2_BASE		0xEEB00000 /* APB-11 */
#include "regs-ps2.h"
#define PKUNITY_SDC_BASE		0xEEC00000 /* APB-12 */
#include "regs-sdc.h"

