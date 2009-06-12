/*
 * File: include/asm-blackfin/mach-bf538/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file should be up to date with:
 *  - Revision G, 09/18/2008; ADSP-BF538/BF538F Blackfin Processor Anomaly List
 *  - Revision L, 09/18/2008; ADSP-BF539/BF539F Blackfin Processor Anomaly List
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* We do not support old silicon - sorry */
#if __SILICON_REVISION__ < 4
# error will not work on BF538/BF539 silicon version 0.0, 0.1, 0.2, or 0.3
#endif

#if defined(__ADSPBF538__)
# define ANOMALY_BF538 1
#else
# define ANOMALY_BF538 0
#endif
#if defined(__ADSPBF539__)
# define ANOMALY_BF539 1
#else
# define ANOMALY_BF539 0
#endif

/* Multi-issue instruction with dsp32shiftimm in slot1 and P-reg store in slot 2 not supported */
#define ANOMALY_05000074 (1)
/* DMA_RUN Bit Is Not Valid after a Peripheral Receive Channel DMA Stops */
#define ANOMALY_05000119 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* PPI Data Lengths Between 8 and 16 Do Not Zero Out Upper Bits */
#define ANOMALY_05000166 (1)
/* PPI_COUNT Cannot Be Programmed to 0 in General Purpose TX or RX Modes */
#define ANOMALY_05000179 (1)
/* PPI_DELAY Not Functional in PPI Modes with 0 Frame Syncs */
#define ANOMALY_05000180 (1)
/* False I/O Pin Interrupts on Edge-Sensitive Inputs When Polarity Setting Is Changed */
#define ANOMALY_05000193 (1)
/* Current DMA Address Shows Wrong Value During Carry Fix */
#define ANOMALY_05000199 (__SILICON_REVISION__ < 4)
/* NMI Event at Boot Time Results in Unpredictable State */
#define ANOMALY_05000219 (1)
/* SPI Slave Boot Mode Modifies Registers from Reset Value */
#define ANOMALY_05000229 (1)
/* PPI_FS3 Is Not Driven in 2 or 3 Internal Frame Sync Transmit Modes */
#define ANOMALY_05000233 (1)
/* If I-Cache Is On, CSYNC/SSYNC/IDLE Around Change of Control Causes Failures */
#define ANOMALY_05000244 (__SILICON_REVISION__ < 3)
/* False Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Maximum External Clock Speed for Timers */
#define ANOMALY_05000253 (1)
/* DCPLB_FAULT_ADDR MMR Register May Be Corrupted */
#define ANOMALY_05000261 (__SILICON_REVISION__ < 3)
/* High I/O Activity Causes Output Voltage of Internal Voltage Regulator (Vddint) to Decrease */
#define ANOMALY_05000270 (__SILICON_REVISION__ < 4)
/* Certain Data Cache Writethrough Modes Fail for Vddint <= 0.9V */
#define ANOMALY_05000272 (1)
/* Writes to Synchronous SDRAM Memory May Be Lost */
#define ANOMALY_05000273 (__SILICON_REVISION__ < 4)
/* Writes to an I/O Data Register One SCLK Cycle after an Edge Is Detected May Clear Interrupt */
#define ANOMALY_05000277 (__SILICON_REVISION__ < 4)
/* Disabling Peripherals with DMA Running May Cause DMA System Instability */
#define ANOMALY_05000278 (__SILICON_REVISION__ < 4)
/* False Hardware Error Exception When ISR Context Is Not Restored */
#define ANOMALY_05000281 (__SILICON_REVISION__ < 4)
/* Memory DMA Corruption with 32-Bit Data and Traffic Control */
#define ANOMALY_05000282 (__SILICON_REVISION__ < 4)
/* System MMR Write Is Stalled Indefinitely When Killed in a Particular Stage */
#define ANOMALY_05000283 (__SILICON_REVISION__ < 4)
/* SPORTs May Receive Bad Data If FIFOs Fill Up */
#define ANOMALY_05000288 (__SILICON_REVISION__ < 4)
/* Reads from CAN Mailbox and Acceptance Mask Area Can Fail */
#define ANOMALY_05000291 (__SILICON_REVISION__ < 4)
/* Hibernate Leakage Current Is Higher Than Specified */
#define ANOMALY_05000293 (__SILICON_REVISION__ < 4)
/* Timer Pin Limitations for PPI TX Modes with External Frame Syncs */
#define ANOMALY_05000294 (1)
/* Memory-To-Memory DMA Source/Destination Descriptors Must Be in Same Memory Space */
#define ANOMALY_05000301 (__SILICON_REVISION__ < 4)
/* SSYNCs After Writes To CAN/DMA MMR Registers Are Not Always Handled Correctly */
#define ANOMALY_05000304 (__SILICON_REVISION__ < 4)
/* SCKELOW Bit Does Not Maintain State Through Hibernate */
#define ANOMALY_05000307 (__SILICON_REVISION__ < 4)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* Errors When SSYNC, CSYNC, or Loads to LT, LB and LC Registers Are Interrupted */
#define ANOMALY_05000312 (__SILICON_REVISION__ < 5)
/* PPI Is Level-Sensitive on First Transfer In Single Frame Sync Modes */
#define ANOMALY_05000313 (__SILICON_REVISION__ < 4)
/* Killed System MMR Write Completes Erroneously On Next System MMR Access */
#define ANOMALY_05000315 (__SILICON_REVISION__ < 4)
/* PFx Glitch on Write to FIO_FLAG_D or FIO_FLAG_T */
#define ANOMALY_05000318 (ANOMALY_BF539 && __SILICON_REVISION__ < 4)
/* Regulator Programming Blocked when Hibernate Wakeup Source Remains Active */
#define ANOMALY_05000355 (__SILICON_REVISION__ < 5)
/* Serial Port (SPORT) Multichannel Transmit Failure when Channel 0 Is Disabled */
#define ANOMALY_05000357 (__SILICON_REVISION__ < 5)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* Possible RETS Register Corruption when Subroutine Is under 5 Cycles in Duration */
#define ANOMALY_05000371 (__SILICON_REVISION__ < 5)
/* Entering Hibernate State with Peripheral Wakeups Enabled Draws Excess Current */
#define ANOMALY_05000374 (__SILICON_REVISION__ == 4)
/* New Feature: Open-Drain GPIO Outputs on PC1 and PC4 (Not Available on Older Silicon) */
#define ANOMALY_05000375 (__SILICON_REVISION__ < 4)
/* SSYNC Stalls Processor when Executed from Non-Cacheable Memory */
#define ANOMALY_05000402 (__SILICON_REVISION__ < 4)
/* Level-Sensitive External GPIO Wakeups May Cause Indefinite Stall */
#define ANOMALY_05000403 (1)
/* Speculative Fetches Can Cause Undesired External FIFO Operations */
#define ANOMALY_05000416 (1)
/* Multichannel SPORT Channel Misalignment Under Specific Configuration */
#define ANOMALY_05000425 (1)
/* Speculative Fetches of Indirect-Pointer Instructions Can Cause False Hardware Errors */
#define ANOMALY_05000426 (1)
/* Specific GPIO Pins May Change State when Entering Hibernate */
#define ANOMALY_05000436 (__SILICON_REVISION__ > 3)
/* IFLUSH Instruction at End of Hardware Loop Causes Infinite Stall */
#define ANOMALY_05000443 (1)
/* False Hardware Error when RETI points to invalid memory */
#define ANOMALY_05000461 (1)

/* Anomalies that don't exist on this proc */
#define ANOMALY_05000099 (0)
#define ANOMALY_05000120 (0)
#define ANOMALY_05000149 (0)
#define ANOMALY_05000158 (0)
#define ANOMALY_05000171 (0)
#define ANOMALY_05000198 (0)
#define ANOMALY_05000215 (0)
#define ANOMALY_05000220 (0)
#define ANOMALY_05000227 (0)
#define ANOMALY_05000230 (0)
#define ANOMALY_05000231 (0)
#define ANOMALY_05000242 (0)
#define ANOMALY_05000248 (0)
#define ANOMALY_05000250 (0)
#define ANOMALY_05000254 (0)
#define ANOMALY_05000263 (0)
#define ANOMALY_05000274 (0)
#define ANOMALY_05000287 (0)
#define ANOMALY_05000305 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000353 (1)
#define ANOMALY_05000362 (1)
#define ANOMALY_05000363 (0)
#define ANOMALY_05000380 (0)
#define ANOMALY_05000386 (1)
#define ANOMALY_05000389 (0)
#define ANOMALY_05000400 (0)
#define ANOMALY_05000412 (0)
#define ANOMALY_05000430 (0)
#define ANOMALY_05000432 (0)
#define ANOMALY_05000435 (0)
#define ANOMALY_05000447 (0)
#define ANOMALY_05000448 (0)
#define ANOMALY_05000456 (0)
#define ANOMALY_05000450 (0)

#endif
