/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

/**
 * @file
 *
 * ABI-related register definitions.
 */

#ifndef __ARCH_ABI_H__

#ifndef __tile__   /* support uncommon use of arch headers in non-tile builds */
# include <arch/chip.h>
# define __INT_REG_BITS CHIP_WORD_SIZE()
#endif

#include <arch/intreg.h>

/* __need_int_reg_t is deprecated: just include <arch/intreg.h> */
#ifndef __need_int_reg_t

#define __ARCH_ABI_H__

#ifndef __ASSEMBLER__
/** Unsigned type that can hold a register. */
typedef __uint_reg_t uint_reg_t;

/** Signed type that can hold a register. */
typedef __int_reg_t int_reg_t;
#endif

/** String prefix to use for printf(). */
#define INT_REG_FMT __INT_REG_FMT

/** Number of bits in a register. */
#define INT_REG_BITS __INT_REG_BITS


/* Registers 0 - 55 are "normal", but some perform special roles. */

#define TREG_FP       52   /**< Frame pointer. */
#define TREG_TP       53   /**< Thread pointer. */
#define TREG_SP       54   /**< Stack pointer. */
#define TREG_LR       55   /**< Link to calling function PC. */

/** Index of last normal general-purpose register. */
#define TREG_LAST_GPR 55

/* Registers 56 - 62 are "special" network registers. */

#define TREG_SN       56   /**< Static network access. */
#define TREG_IDN0     57   /**< IDN demux 0 access. */
#define TREG_IDN1     58   /**< IDN demux 1 access. */
#define TREG_UDN0     59   /**< UDN demux 0 access. */
#define TREG_UDN1     60   /**< UDN demux 1 access. */
#define TREG_UDN2     61   /**< UDN demux 2 access. */
#define TREG_UDN3     62   /**< UDN demux 3 access. */

/* Register 63 is the "special" zero register. */

#define TREG_ZERO     63   /**< "Zero" register; always reads as "0". */


/** By convention, this register is used to hold the syscall number. */
#define TREG_SYSCALL_NR      10

/** Name of register that holds the syscall number, for use in assembly. */
#define TREG_SYSCALL_NR_NAME r10


/**
 * The ABI requires callers to allocate a caller state save area of
 * this many bytes at the bottom of each stack frame.
 */
#define C_ABI_SAVE_AREA_SIZE (2 * (INT_REG_BITS / 8))

/**
 * The operand to an 'info' opcode directing the backtracer to not
 * try to find the calling frame.
 */
#define INFO_OP_CANNOT_BACKTRACE 2


#endif /* !__need_int_reg_t */

/* Make sure we later can get all the definitions and declarations.  */
#undef __need_int_reg_t

#endif /* !__ARCH_ABI_H__ */
