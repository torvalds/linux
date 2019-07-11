/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 */
#ifndef _MIC_X100_CARD_H_
#define _MIC_X100_CARD_H_

#define MIC_X100_MMIO_BASE 0x08007C0000ULL
#define MIC_X100_MMIO_LEN 0x00020000ULL
#define MIC_X100_SBOX_BASE_ADDRESS 0x00010000ULL

#define MIC_X100_SBOX_SPAD0 0x0000AB20
#define MIC_X100_SBOX_SDBIC0 0x0000CC90
#define MIC_X100_SBOX_SDBIC0_DBREQ_BIT 0x80000000
#define MIC_X100_SBOX_RDMASR0	0x0000B180
#define MIC_X100_SBOX_APICICR0 0x0000A9D0

#define MIC_X100_MAX_DOORBELL_IDX 8

#define MIC_X100_NUM_SBOX_IRQ 8
#define MIC_X100_NUM_RDMASR_IRQ 8
#define MIC_X100_SBOX_IRQ_BASE 0
#define MIC_X100_RDMASR_IRQ_BASE 17

#define MIC_X100_IRQ_BASE 26

#endif
