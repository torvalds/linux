/* SPDX-License-Identifier: GPL-2.0 */
/*
 * apb.h: Advanced PCI Bridge Configuration Registers and Bits
 *
 * Copyright (C) 1998  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC64_APB_H
#define _SPARC64_APB_H

#define APB_TICK_REGISTER			0xb0
#define APB_INT_ACK				0xb8
#define APB_PRIMARY_MASTER_RETRY_LIMIT		0xc0
#define APB_DMA_ASFR				0xc8
#define APB_DMA_AFAR				0xd0
#define APB_PIO_TARGET_RETRY_LIMIT		0xd8
#define APB_PIO_TARGET_LATENCY_TIMER		0xd9
#define APB_DMA_TARGET_RETRY_LIMIT		0xda
#define APB_DMA_TARGET_LATENCY_TIMER		0xdb
#define APB_SECONDARY_MASTER_RETRY_LIMIT	0xdc
#define APB_SECONDARY_CONTROL			0xdd
#define APB_IO_ADDRESS_MAP			0xde
#define APB_MEM_ADDRESS_MAP			0xdf

#define APB_PCI_CONTROL_LOW			0xe0
#  define APB_PCI_CTL_LOW_ARB_PARK			(1 << 21)
#  define APB_PCI_CTL_LOW_ERRINT_EN			(1 << 8)

#define APB_PCI_CONTROL_HIGH			0xe4
#  define APB_PCI_CTL_HIGH_SERR				(1 << 2)
#  define APB_PCI_CTL_HIGH_ARBITER_EN			(1 << 0)

#define APB_PIO_ASFR				0xe8
#define APB_PIO_AFAR				0xf0
#define APB_DIAG_REGISTER			0xf8

#endif /* !(_SPARC64_APB_H) */
