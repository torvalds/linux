/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#ifndef _MIC_X100_HW_H_
#define _MIC_X100_HW_H_

#define MIC_X100_PCI_DEVICE_2250 0x2250
#define MIC_X100_PCI_DEVICE_2251 0x2251
#define MIC_X100_PCI_DEVICE_2252 0x2252
#define MIC_X100_PCI_DEVICE_2253 0x2253
#define MIC_X100_PCI_DEVICE_2254 0x2254
#define MIC_X100_PCI_DEVICE_2255 0x2255
#define MIC_X100_PCI_DEVICE_2256 0x2256
#define MIC_X100_PCI_DEVICE_2257 0x2257
#define MIC_X100_PCI_DEVICE_2258 0x2258
#define MIC_X100_PCI_DEVICE_2259 0x2259
#define MIC_X100_PCI_DEVICE_225a 0x225a
#define MIC_X100_PCI_DEVICE_225b 0x225b
#define MIC_X100_PCI_DEVICE_225c 0x225c
#define MIC_X100_PCI_DEVICE_225d 0x225d
#define MIC_X100_PCI_DEVICE_225e 0x225e

#define MIC_X100_APER_BAR 0
#define MIC_X100_MMIO_BAR 4

#define MIC_X100_SBOX_BASE_ADDRESS 0x00010000
#define MIC_X100_SBOX_SPAD0 0x0000AB20
extern struct mic_hw_ops mic_x100_ops;

#endif
