/*
 * File: include/asm-blackfin/mach-bf518/anomaly.h
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2004-2008 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

/* This file shoule be up to date with:
 *  - ????
 */

#ifndef _MACH_ANOMALY_H_
#define _MACH_ANOMALY_H_

/* Multi-Issue Instruction with dsp32shiftimm in slot1 and P-reg Store in slot2 Not Supported */
#define ANOMALY_05000074 (1)
/* Rx.H Cannot Be Used to Access 16-bit System MMR Registers */
#define ANOMALY_05000122 (1)
/* False Hardware Error from an Access in the Shadow of a Conditional Branch */
#define ANOMALY_05000245 (1)
/* Sensitivity To Noise with Slow Input Edge Rates on External SPORT TX and RX Clocks */
#define ANOMALY_05000265 (1)
/* False Hardware Errors Caused by Fetches at the Boundary of Reserved Memory */
#define ANOMALY_05000310 (1)
/* PPI Underflow Error Goes Undetected in ITU-R 656 Mode */
#define ANOMALY_05000366 (1)
/* Lockbox SESR Firmware Does Not Save/Restore Full Context */
#define ANOMALY_05000405 (1)
/* Lockbox Firmware Memory Cleanup Routine Does not Clear Registers */
#define ANOMALY_05000408 (1)
/* Speculative Fetches Can Cause Undesired External FIFO Operations */
#define ANOMALY_05000416 (1)
/* TWI Fall Time (Tof) May Violate the Minimum I2C Specification */
#define ANOMALY_05000421 (1)
/* TWI Input Capacitance (Ci) May Violate the Maximum I2C Specification */
#define ANOMALY_05000422 (1)
/* Speculative Fetches of Indirect-Pointer Instructions Can Cause False Hardware Errors */
#define ANOMALY_05000426 (1)
/* Software System Reset Corrupts PLL_LOCKCNT Register */
#define ANOMALY_05000430 (1)
/* Incorrect Use of Stack in Lockbox Firmware During Authentication */
#define ANOMALY_05000431 (1)
/* Certain SIC Registers are not Reset After Soft or Core Double Fault Reset */
#define ANOMALY_05000435 (1)
/* PORTx_DRIVE and PORTx_HYSTERESIS Registers Read Back Incorrect Values */
#define ANOMALY_05000438 (1)
/* Preboot Cannot be Used to Program the PLL_DIV Register */
#define ANOMALY_05000439 (1)
/* bfrom_SysControl() Cannot be Used to Write the PLL_DIV Register */
#define ANOMALY_05000440 (1)
/* IFLUSH Instruction at End of Hardware Loop Causes Infinite Stall */
#define ANOMALY_05000443 (1)
/* Incorrect L1 Instruction Bank B Memory Map Location */
#define ANOMALY_05000444 (1)

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
#define ANOMALY_05000285 (0)
#define ANOMALY_05000307 (0)
#define ANOMALY_05000311 (0)
#define ANOMALY_05000312 (0)
#define ANOMALY_05000323 (0)
#define ANOMALY_05000353 (0)
#define ANOMALY_05000363 (0)
#define ANOMALY_05000386 (0)
#define ANOMALY_05000412 (0)
#define ANOMALY_05000432 (0)

#endif
