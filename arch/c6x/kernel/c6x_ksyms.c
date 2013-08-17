/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <asm/checksum.h>
#include <linux/io.h>

/*
 * libgcc functions - used internally by the compiler...
 */
extern int __c6xabi_divi(int dividend, int divisor);
EXPORT_SYMBOL(__c6xabi_divi);

extern unsigned __c6xabi_divu(unsigned	dividend, unsigned divisor);
EXPORT_SYMBOL(__c6xabi_divu);

extern int __c6xabi_remi(int dividend, int divisor);
EXPORT_SYMBOL(__c6xabi_remi);

extern unsigned __c6xabi_remu(unsigned	dividend, unsigned divisor);
EXPORT_SYMBOL(__c6xabi_remu);

extern int __c6xabi_divremi(int dividend, int divisor);
EXPORT_SYMBOL(__c6xabi_divremi);

extern unsigned __c6xabi_divremu(unsigned  dividend, unsigned divisor);
EXPORT_SYMBOL(__c6xabi_divremu);

extern unsigned long long __c6xabi_mpyll(unsigned long long src1,
					 unsigned long long src2);
EXPORT_SYMBOL(__c6xabi_mpyll);

extern long long __c6xabi_negll(long long src);
EXPORT_SYMBOL(__c6xabi_negll);

extern unsigned long long __c6xabi_llshl(unsigned long long src1, uint src2);
EXPORT_SYMBOL(__c6xabi_llshl);

extern long long __c6xabi_llshr(long long src1, uint src2);
EXPORT_SYMBOL(__c6xabi_llshr);

extern unsigned long long __c6xabi_llshru(unsigned long long src1, uint src2);
EXPORT_SYMBOL(__c6xabi_llshru);

extern void __c6xabi_strasgi(int *dst, const int *src, unsigned cnt);
EXPORT_SYMBOL(__c6xabi_strasgi);

extern void __c6xabi_push_rts(void);
EXPORT_SYMBOL(__c6xabi_push_rts);

extern void __c6xabi_pop_rts(void);
EXPORT_SYMBOL(__c6xabi_pop_rts);

extern void __c6xabi_strasgi_64plus(int *dst, const int *src, unsigned cnt);
EXPORT_SYMBOL(__c6xabi_strasgi_64plus);

/* lib functions */
EXPORT_SYMBOL(memcpy);
