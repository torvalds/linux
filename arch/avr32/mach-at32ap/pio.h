/*
 * Atmel PIO2 Port Multiplexer support
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ARCH_AVR32_AT32AP_PIO_H__
#define __ARCH_AVR32_AT32AP_PIO_H__

/* PIO register offsets */
#define PIO_PER                                0x0000
#define PIO_PDR                                0x0004
#define PIO_PSR                                0x0008
#define PIO_OER                                0x0010
#define PIO_ODR                                0x0014
#define PIO_OSR                                0x0018
#define PIO_IFER                               0x0020
#define PIO_IFDR                               0x0024
#define PIO_IFSR                               0x0028
#define PIO_SODR                               0x0030
#define PIO_CODR                               0x0034
#define PIO_ODSR                               0x0038
#define PIO_PDSR                               0x003c
#define PIO_IER                                0x0040
#define PIO_IDR                                0x0044
#define PIO_IMR                                0x0048
#define PIO_ISR                                0x004c
#define PIO_MDER                               0x0050
#define PIO_MDDR                               0x0054
#define PIO_MDSR                               0x0058
#define PIO_PUDR                               0x0060
#define PIO_PUER                               0x0064
#define PIO_PUSR                               0x0068
#define PIO_ASR                                0x0070
#define PIO_BSR                                0x0074
#define PIO_ABSR                               0x0078
#define PIO_OWER                               0x00a0
#define PIO_OWDR                               0x00a4
#define PIO_OWSR                               0x00a8

/* Bitfields in PER */

/* Bitfields in PDR */

/* Bitfields in PSR */

/* Bitfields in OER */

/* Bitfields in ODR */

/* Bitfields in OSR */

/* Bitfields in IFER */

/* Bitfields in IFDR */

/* Bitfields in IFSR */

/* Bitfields in SODR */

/* Bitfields in CODR */

/* Bitfields in ODSR */

/* Bitfields in PDSR */

/* Bitfields in IER */

/* Bitfields in IDR */

/* Bitfields in IMR */

/* Bitfields in ISR */

/* Bitfields in MDER */

/* Bitfields in MDDR */

/* Bitfields in MDSR */

/* Bitfields in PUDR */

/* Bitfields in PUER */

/* Bitfields in PUSR */

/* Bitfields in ASR */

/* Bitfields in BSR */

/* Bitfields in ABSR */
#define PIO_P0_OFFSET                          0
#define PIO_P0_SIZE                            1
#define PIO_P1_OFFSET                          1
#define PIO_P1_SIZE                            1
#define PIO_P2_OFFSET                          2
#define PIO_P2_SIZE                            1
#define PIO_P3_OFFSET                          3
#define PIO_P3_SIZE                            1
#define PIO_P4_OFFSET                          4
#define PIO_P4_SIZE                            1
#define PIO_P5_OFFSET                          5
#define PIO_P5_SIZE                            1
#define PIO_P6_OFFSET                          6
#define PIO_P6_SIZE                            1
#define PIO_P7_OFFSET                          7
#define PIO_P7_SIZE                            1
#define PIO_P8_OFFSET                          8
#define PIO_P8_SIZE                            1
#define PIO_P9_OFFSET                          9
#define PIO_P9_SIZE                            1
#define PIO_P10_OFFSET                         10
#define PIO_P10_SIZE                           1
#define PIO_P11_OFFSET                         11
#define PIO_P11_SIZE                           1
#define PIO_P12_OFFSET                         12
#define PIO_P12_SIZE                           1
#define PIO_P13_OFFSET                         13
#define PIO_P13_SIZE                           1
#define PIO_P14_OFFSET                         14
#define PIO_P14_SIZE                           1
#define PIO_P15_OFFSET                         15
#define PIO_P15_SIZE                           1
#define PIO_P16_OFFSET                         16
#define PIO_P16_SIZE                           1
#define PIO_P17_OFFSET                         17
#define PIO_P17_SIZE                           1
#define PIO_P18_OFFSET                         18
#define PIO_P18_SIZE                           1
#define PIO_P19_OFFSET                         19
#define PIO_P19_SIZE                           1
#define PIO_P20_OFFSET                         20
#define PIO_P20_SIZE                           1
#define PIO_P21_OFFSET                         21
#define PIO_P21_SIZE                           1
#define PIO_P22_OFFSET                         22
#define PIO_P22_SIZE                           1
#define PIO_P23_OFFSET                         23
#define PIO_P23_SIZE                           1
#define PIO_P24_OFFSET                         24
#define PIO_P24_SIZE                           1
#define PIO_P25_OFFSET                         25
#define PIO_P25_SIZE                           1
#define PIO_P26_OFFSET                         26
#define PIO_P26_SIZE                           1
#define PIO_P27_OFFSET                         27
#define PIO_P27_SIZE                           1
#define PIO_P28_OFFSET                         28
#define PIO_P28_SIZE                           1
#define PIO_P29_OFFSET                         29
#define PIO_P29_SIZE                           1
#define PIO_P30_OFFSET                         30
#define PIO_P30_SIZE                           1
#define PIO_P31_OFFSET                         31
#define PIO_P31_SIZE                           1

/* Bitfields in OWER */

/* Bitfields in OWDR */

/* Bitfields in OWSR */

/* Bit manipulation macros */
#define PIO_BIT(name)                          (1 << PIO_##name##_OFFSET)
#define PIO_BF(name,value)                     (((value) & ((1 << PIO_##name##_SIZE) - 1)) << PIO_##name##_OFFSET)
#define PIO_BFEXT(name,value)                  (((value) >> PIO_##name##_OFFSET) & ((1 << PIO_##name##_SIZE) - 1))
#define PIO_BFINS(name,value,old)              (((old) & ~(((1 << PIO_##name##_SIZE) - 1) << PIO_##name##_OFFSET)) | PIO_BF(name,value))

/* Register access macros */
#define pio_readl(port,reg)					\
	__raw_readl((port)->regs + PIO_##reg)
#define pio_writel(port,reg,value)				\
	__raw_writel((value), (port)->regs + PIO_##reg)

void at32_init_pio(struct platform_device *pdev);

#endif /* __ARCH_AVR32_AT32AP_PIO_H__ */
