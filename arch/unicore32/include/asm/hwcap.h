/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/hwcap.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_HWCAP_H__
#define __UNICORE_HWCAP_H__

/*
 * HWCAP flags
 */
#define HWCAP_MSP		1
#define HWCAP_UNICORE16		2
#define HWCAP_CMOV		4
#define HWCAP_UNICORE_F64       8
#define HWCAP_TLS		0x80

#if defined(__KERNEL__) && !defined(__ASSEMBLY__)
/*
 * This yields a mask that user programs can use to figure out what
 * instruction set this cpu supports.
 */
#define ELF_HWCAP		(HWCAP_CMOV | HWCAP_UNICORE_F64)
#endif

#endif
