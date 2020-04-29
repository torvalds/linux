/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	include/asm-mips/dec/kn230.h
 *
 *	DECsystem 5100 (MIPSmate or KN230) definitions.
 *
 *	Copyright (C) 2002, 2003  Maciej W. Rozycki
 */
#ifndef __ASM_MIPS_DEC_KN230_H
#define __ASM_MIPS_DEC_KN230_H

/*
 * CPU interrupt bits.
 */
#define KN230_CPU_INR_HALT	6	/* HALT button */
#define KN230_CPU_INR_BUS	5	/* memory, I/O bus read/write errors */
#define KN230_CPU_INR_RTC	4	/* DS1287 RTC */
#define KN230_CPU_INR_SII	3	/* SII (DC7061) SCSI */
#define KN230_CPU_INR_LANCE	3	/* LANCE (Am7990) Ethernet */
#define KN230_CPU_INR_DZ11	2	/* DZ11 (DC7085) serial */

#endif /* __ASM_MIPS_DEC_KN230_H */
