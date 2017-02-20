/*
 * Export AVR32-specific functions for loadable modules.
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#include <asm/checksum.h>
#include <linux/uaccess.h>

/*
 * GCC functions
 */
extern unsigned long long __avr32_lsl64(unsigned long long u, unsigned long b);
extern unsigned long long __avr32_lsr64(unsigned long long u, unsigned long b);
extern unsigned long long __avr32_asr64(unsigned long long u, unsigned long b);
EXPORT_SYMBOL(__avr32_lsl64);
EXPORT_SYMBOL(__avr32_lsr64);
EXPORT_SYMBOL(__avr32_asr64);

/*
 * String functions
 */
EXPORT_SYMBOL(memset);
EXPORT_SYMBOL(memcpy);

EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);

/*
 * Userspace access stuff.
 */
EXPORT_SYMBOL(___copy_from_user);
EXPORT_SYMBOL(copy_to_user);
EXPORT_SYMBOL(__copy_user);
EXPORT_SYMBOL(strncpy_from_user);
EXPORT_SYMBOL(__strncpy_from_user);
EXPORT_SYMBOL(clear_user);
EXPORT_SYMBOL(__clear_user);
EXPORT_SYMBOL(strnlen_user);

EXPORT_SYMBOL(csum_partial);
EXPORT_SYMBOL(csum_partial_copy_generic);

/* Delay loops (lib/delay.S) */
EXPORT_SYMBOL(__ndelay);
EXPORT_SYMBOL(__udelay);
EXPORT_SYMBOL(__const_udelay);

/* Bit operations (lib/findbit.S) */
EXPORT_SYMBOL(find_first_zero_bit);
EXPORT_SYMBOL(find_next_zero_bit);
EXPORT_SYMBOL(find_first_bit);
EXPORT_SYMBOL(find_next_bit);
EXPORT_SYMBOL(find_next_bit_le);
EXPORT_SYMBOL(find_next_zero_bit_le);

/* I/O primitives (lib/io-*.S) */
EXPORT_SYMBOL(__raw_readsb);
EXPORT_SYMBOL(__raw_readsw);
EXPORT_SYMBOL(__raw_readsl);
EXPORT_SYMBOL(__raw_writesb);
EXPORT_SYMBOL(__raw_writesw);
EXPORT_SYMBOL(__raw_writesl);
