/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Program check table.
 *
 *    Copyright IBM Corp. 2012
 */

#include <linux/linkage.h>

#define PGM_CHECK(handler)	.quad handler
#define PGM_CHECK_DEFAULT	PGM_CHECK(default_trap_handler)

/*
 * The program check table contains exactly 128 (0x00-0x7f) entries. Each
 * line defines the function to be called corresponding to the program check
 * interruption code.
 */
.section .rodata, "a"
ENTRY(pgm_check_table)
PGM_CHECK_DEFAULT			/* 00 */
PGM_CHECK(illegal_op)			/* 01 */
PGM_CHECK(privileged_op)		/* 02 */
PGM_CHECK(execute_exception)		/* 03 */
PGM_CHECK(do_protection_exception)	/* 04 */
PGM_CHECK(addressing_exception)		/* 05 */
PGM_CHECK(specification_exception)	/* 06 */
PGM_CHECK(data_exception)		/* 07 */
PGM_CHECK(overflow_exception)		/* 08 */
PGM_CHECK(divide_exception)		/* 09 */
PGM_CHECK(overflow_exception)		/* 0a */
PGM_CHECK(divide_exception)		/* 0b */
PGM_CHECK(hfp_overflow_exception)	/* 0c */
PGM_CHECK(hfp_underflow_exception)	/* 0d */
PGM_CHECK(hfp_significance_exception)	/* 0e */
PGM_CHECK(hfp_divide_exception)		/* 0f */
PGM_CHECK(do_dat_exception)		/* 10 */
PGM_CHECK(do_dat_exception)		/* 11 */
PGM_CHECK(translation_exception)	/* 12 */
PGM_CHECK(special_op_exception)		/* 13 */
PGM_CHECK_DEFAULT			/* 14 */
PGM_CHECK(operand_exception)		/* 15 */
PGM_CHECK_DEFAULT			/* 16 */
PGM_CHECK_DEFAULT			/* 17 */
PGM_CHECK(transaction_exception)	/* 18 */
PGM_CHECK_DEFAULT			/* 19 */
PGM_CHECK_DEFAULT			/* 1a */
PGM_CHECK(vector_exception)		/* 1b */
PGM_CHECK(space_switch_exception)	/* 1c */
PGM_CHECK(hfp_sqrt_exception)		/* 1d */
PGM_CHECK_DEFAULT			/* 1e */
PGM_CHECK_DEFAULT			/* 1f */
PGM_CHECK_DEFAULT			/* 20 */
PGM_CHECK_DEFAULT			/* 21 */
PGM_CHECK_DEFAULT			/* 22 */
PGM_CHECK_DEFAULT			/* 23 */
PGM_CHECK_DEFAULT			/* 24 */
PGM_CHECK_DEFAULT			/* 25 */
PGM_CHECK_DEFAULT			/* 26 */
PGM_CHECK_DEFAULT			/* 27 */
PGM_CHECK_DEFAULT			/* 28 */
PGM_CHECK_DEFAULT			/* 29 */
PGM_CHECK_DEFAULT			/* 2a */
PGM_CHECK_DEFAULT			/* 2b */
PGM_CHECK_DEFAULT			/* 2c */
PGM_CHECK_DEFAULT			/* 2d */
PGM_CHECK_DEFAULT			/* 2e */
PGM_CHECK_DEFAULT			/* 2f */
PGM_CHECK_DEFAULT			/* 30 */
PGM_CHECK_DEFAULT			/* 31 */
PGM_CHECK_DEFAULT			/* 32 */
PGM_CHECK_DEFAULT			/* 33 */
PGM_CHECK_DEFAULT			/* 34 */
PGM_CHECK_DEFAULT			/* 35 */
PGM_CHECK_DEFAULT			/* 36 */
PGM_CHECK_DEFAULT			/* 37 */
PGM_CHECK(do_dat_exception)		/* 38 */
PGM_CHECK(do_dat_exception)		/* 39 */
PGM_CHECK(do_dat_exception)		/* 3a */
PGM_CHECK(do_dat_exception)		/* 3b */
PGM_CHECK_DEFAULT			/* 3c */
PGM_CHECK_DEFAULT			/* 3d */
PGM_CHECK_DEFAULT			/* 3e */
PGM_CHECK_DEFAULT			/* 3f */
PGM_CHECK_DEFAULT			/* 40 */
PGM_CHECK_DEFAULT			/* 41 */
PGM_CHECK_DEFAULT			/* 42 */
PGM_CHECK_DEFAULT			/* 43 */
PGM_CHECK_DEFAULT			/* 44 */
PGM_CHECK_DEFAULT			/* 45 */
PGM_CHECK_DEFAULT			/* 46 */
PGM_CHECK_DEFAULT			/* 47 */
PGM_CHECK_DEFAULT			/* 48 */
PGM_CHECK_DEFAULT			/* 49 */
PGM_CHECK_DEFAULT			/* 4a */
PGM_CHECK_DEFAULT			/* 4b */
PGM_CHECK_DEFAULT			/* 4c */
PGM_CHECK_DEFAULT			/* 4d */
PGM_CHECK_DEFAULT			/* 4e */
PGM_CHECK_DEFAULT			/* 4f */
PGM_CHECK_DEFAULT			/* 50 */
PGM_CHECK_DEFAULT			/* 51 */
PGM_CHECK_DEFAULT			/* 52 */
PGM_CHECK_DEFAULT			/* 53 */
PGM_CHECK_DEFAULT			/* 54 */
PGM_CHECK_DEFAULT			/* 55 */
PGM_CHECK_DEFAULT			/* 56 */
PGM_CHECK_DEFAULT			/* 57 */
PGM_CHECK_DEFAULT			/* 58 */
PGM_CHECK_DEFAULT			/* 59 */
PGM_CHECK_DEFAULT			/* 5a */
PGM_CHECK_DEFAULT			/* 5b */
PGM_CHECK_DEFAULT			/* 5c */
PGM_CHECK_DEFAULT			/* 5d */
PGM_CHECK_DEFAULT			/* 5e */
PGM_CHECK_DEFAULT			/* 5f */
PGM_CHECK_DEFAULT			/* 60 */
PGM_CHECK_DEFAULT			/* 61 */
PGM_CHECK_DEFAULT			/* 62 */
PGM_CHECK_DEFAULT			/* 63 */
PGM_CHECK_DEFAULT			/* 64 */
PGM_CHECK_DEFAULT			/* 65 */
PGM_CHECK_DEFAULT			/* 66 */
PGM_CHECK_DEFAULT			/* 67 */
PGM_CHECK_DEFAULT			/* 68 */
PGM_CHECK_DEFAULT			/* 69 */
PGM_CHECK_DEFAULT			/* 6a */
PGM_CHECK_DEFAULT			/* 6b */
PGM_CHECK_DEFAULT			/* 6c */
PGM_CHECK_DEFAULT			/* 6d */
PGM_CHECK_DEFAULT			/* 6e */
PGM_CHECK_DEFAULT			/* 6f */
PGM_CHECK_DEFAULT			/* 70 */
PGM_CHECK_DEFAULT			/* 71 */
PGM_CHECK_DEFAULT			/* 72 */
PGM_CHECK_DEFAULT			/* 73 */
PGM_CHECK_DEFAULT			/* 74 */
PGM_CHECK_DEFAULT			/* 75 */
PGM_CHECK_DEFAULT			/* 76 */
PGM_CHECK_DEFAULT			/* 77 */
PGM_CHECK_DEFAULT			/* 78 */
PGM_CHECK_DEFAULT			/* 79 */
PGM_CHECK_DEFAULT			/* 7a */
PGM_CHECK_DEFAULT			/* 7b */
PGM_CHECK_DEFAULT			/* 7c */
PGM_CHECK_DEFAULT			/* 7d */
PGM_CHECK_DEFAULT			/* 7e */
PGM_CHECK_DEFAULT			/* 7f */
