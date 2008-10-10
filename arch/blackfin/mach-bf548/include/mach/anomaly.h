/*
 * File: include/asm-blackfin/mach-bf548/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - Revision G, 08/07/2008; ADSP-BF542/BF544/BF547/BF548/BF549 Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot2 Not Supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* Spurious Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* False Hardware Error Exception when ISR context is not restored */
#define ANOMALY_05000281 (__SILICON_REVISION__ < 1)
/* SSYNCs After Writes To CAN/DMA MMR Registers Are Not Always Handled Correctly */
#define ANOMALY_05000304 (__SILICON_REVISION__ < 1)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (__SILICON_REVISION__ < 1)
/* TWI Slave Boot Mode Is Not Functional */
#define ANOMALY_05000324 (__SILICON_REVISION__ < 1)
/* External FIFO Boot Mode Is Not Functional */
#define ANOMALY_05000325 (__SILICON_REVISION__ < 2)
/* Data Lost When Core and DMA Accesses Are Made to the USB FIFO Simultaneously */
#define ANOMALY_05000327 (__SILICON_REVISION__ < 1)
/* Incorrect Access of OTP_STATUS During otp_write() Function */
#define ANOMALY_05000328 (__SILICON_REVISION__ < 1)
/* Synchronous Burst Flash Boot Mode Is Not Functional */
#define ANOMALY_05000329 (__SILICON_REVISION__ < 1)
/* Host DMA Boot Modes Are Not Functional */
#define ANOMALY_05000330 (__SILICON_REVISION__ < 1)
/* Inadequate Timing Margins on DDR DQS to DQ and DQM Skew */
#define ANOMALY_05000334 (__SILICON_REVISION__ < 1)
/* Inadequate Rotary Debounce Logic Duration */
#define ANOMALY_05000335 (__SILICON_REVISION__ < 1)
/* Phantom Interrupt Occurs After First Configuration of Host DMA Port */
#define ANOMALY_05000336 (__SILICON_REVISION__ < 1)
/* Disallowed Configuration Prevents Subsequent Allowed Configuration on Host DMA Port */
#define ANOMALY_05000337 (__SILICON_REVISION__ < 1)
/* Slave-Mode SPI0 MISO Failure With CPHA = 0 */
#define ANOMALY_05000338 (__SILICON_REVISION__ < 1)
/* If Memory Reads Are Enabled on SDH or HOSTDP, Other DMAC1 Peripherals Cannot Read */
#define ANOMALY_05000340 (__SILICON_REVISION__ < 1)
/* Boot Host Wait (HWAIT) and Boot Host Wait Alternate (HWAITA) Signals Are Swapped */
#define ANOMALY_05000344 (__SILICON_REVISION__ < 1)
/* USB Calibration Value Is Not Intialized */
#define ANOMALY_05000346 (__SILICON_REVISION__ < 1)
/* USB Calibration Value to use */
#define ANOMALY_05000346_value 0x5411
/* Preboot Routine Incorrectly Alters Reset Value of USB Register */
#define ANOMALY_05000347 (__SILICON_REVISION__ < 1)
/* Data Lost when Core Reads SDH Data FIFO */
#define ANOMALY_05000349 (__SILICON_REVISION__ < 1)
/* PLL Status Register Is Inaccurate */
#define ANOMALY_05000351 (__SILICON_REVISION__ < 1)
/* bfrom_SysControl() Firmware Function Performs Improper System Reset */
#define ANOMALY_05000353 (__SILICON_REVISION__ < 2)
/* Regulator Programming Blocked when Hibernate Wakeup Source Remains Active */
#define ANOMALY_05000355 (__SILICON_REVISION__ < 1)
/* System Stalled During A Core Access To AMC While A Core Access To NFC FIFO Is Required */
#define ANOMALY_05000356 (__SILICON_REVISION__ < 1)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (1)
/* External Memory Read Access Hangs Core With PLL Bypass */
#define ANOMALY_05000360 (1)
/* DMAs that Go Urgent during Tight Core Writes to External Memory Are Blocked */
#define ANOMALY_05000365 (1)
/* WURESET Bit In SYSCR Register Does Not Properly Indicate Hibernate Wake-Up */
#define ANOMALY_05000367 (__SILICON_REVISION__ < 1)
/* Addressing Conflict between Boot ROM and Asynchronous Memory */
#define ANOMALY_05000369 (1)
/* Default PLL MSEL and SSEL Settings Can Cause 400MHz Product To Violate Specifications */
#define ANOMALY_05000370 (__SILICON_REVISION__ < 1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (__SILICON_REVISION__ < 2)
/* USB DP/DM Data Pins May Lose State When Entering Hibernate */
#define ANOMALY_05000372 (__SILICON_REVISION__ < 1)
/* Mobile DDR Operation Not Functional */
#define ANOMALY_05000377 (1)
/* Security/Authentication Speedpath Causes Authentication To Fail To Initiate */
#define ANOMALY_05000378 (__SILICON_REVISION__ < 2)
/* 16-Bit NAND FLASH Boot Mode Is Not Functional */
#define ANOMALY_05000379 (1)
/* 8-Bit NAND Flash Boot Mode Not Functional */
#define ANOMALY_05000382 (__SILICON_REVISION__ < 1)
/* Some ATAPI Modes Are Not Functional */
#define ANOMALY_05000383 (1)
/* Boot from OTP Memory Not Functional */
#define ANOMALY_05000385 (__SILICON_REVISION__ < 1)
/* bfrom_SysControl() Firmware Routine Not Functional */
#define ANOMALY_05000386 (__SILICON_REVISION__ < 1)
/* Programmable Preboot Settings Not Functional */
#define ANOMALY_05000387 (__SILICON_REVISION__ < 1)
/* CRC32 Checksum Support Not Functional */
#define ANOMALY_05000388 (__SILICON_REVISION__ < 1)
/* Reset Vector Must Not Be in SDRAM Memory Space */
#define ANOMALY_05000389 (__SILICON_REVISION__ < 1)
/* Changed Meaning of BCODE Field in SYSCR Register */
#define ANOMALY_05000390 (__SILICON_REVISION__ < 1)
/* Repeated Boot from Page-Mode or Burst-Mode Flash Memory May Fail */
#define ANOMALY_05000391 (__SILICON_REVISION__ < 1)
/* pTempCurrent Not Present in ADI_BOOT_DATA Structure */
#define ANOMALY_05000392 (__SILICON_REVISION__ < 1)
/* Deprecated Value of dTempByteCount in ADI_BOOT_DATA Structure */
#define ANOMALY_05000393 (__SILICON_REVISION__ < 1)
/* Log Buffer Not Functional */
#define ANOMALY_05000394 (__SILICON_REVISION__ < 1)
/* Hook Routine Not Functional */
#define ANOMALY_05000395 (__SILICON_REVISION__ < 1)
/* Header Indirect Bit Not Functional */
#define ANOMALY_05000396 (__SILICON_REVISION__ < 1)
/* BK_ONES, BK_ZEROS, and BK_DATECODE Constants Not Functional */
#define ANOMALY_05000397 (__SILICON_REVISION__ < 1)
/* Lockbox SESR Disallows Certain User Interrupts */
#define ANOMALY_05000404 (__SILICON_REVISION__ < 2)
/* Lockbox SESR Firmware Does Not Save/Restore Full Context */
#define ANOMALY_05000405 (1)
/* Lockbox SESR Argument Checking Does Not Check L2 Memory Protection Range */
#define ANOMALY_05000406 (__SILICON_REVISION__ < 2)
/* Lockbox SESR Firmware Arguments Are Not Retained After First Initialization */
#define ANOMALY_05000407 (__SILICON_REVISION__ < 2)
/* Lockbox Firmware Memory Cleanup Routine Does not Clear Registers */
#define ANOMALY_05000408 (1)
/* Lockbox firmware leaves MDMA0 channel enabled */
#define ANOMALY_05000409 (__SILICON_REVISION__ < 2)
/* bfrom_SysControl() Firmware Function Cannot be Used to Enter Power Saving Modes */
#define ANOMALY_05000411 (__SILICON_REVISION__ < 2)
/* NAND Boot Mode Not Compatible With Some NAND Flash Devices */
#define ANOMALY_05000413 (__SILICON_REVISION__ < 2)
/* OTP_CHECK_FOR_PREV_WRITE Bit is Not Functional in bfrom_OtpWrite() API */
#define ANOMALY_05000414 (__SILICON_REVISION__ < 2)
/* Speculative Fetches Can Cause Undesired External FIFO Operations */
#define ANOMALY_05000416 (1)
/* Multichannel SPORT Channel Misalignment Under Specific Configuration */
#define ANOMALY_05000425 (1)
/* Speculative Fetches of Indirect-Pointer Instructions Can Cause Spurious Hardware Errors */
#define ANOMALY_05000426 (1)
/* CORE_EPPI_PRIO bit and SYS_EPPI_PRIO bit in the HMDMA1_CONTROL register are not functional */
#define ANOMALY_05000427 (__SILICON_REVISION__ < 2)
/* WB_EDGE Bit in NFC_IRQSTAT Incorrectly Behaves as a Buffer Status Bit Instead of an IRQ Status Bit */
#define ANOMALY_05000429 (__SILICON_REVISION__ < 2)
/* Software System Reset Corrupts PLL_LOCKCNT Register */
#define ANOMALY_05000430 (__SILICON_REVISION__ >= 2)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000125 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000183 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000230 (0)
#define ANOMALY_05000244 (0)
#define ANOMALY_05000261 (0)
#define ANOMALY_05000263 (0)
#define ANOMALY_05000266 (0)
#define ANOMALY_05000273 (0)
#define ANOMALY_05000307 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000363 (0)

#endif
