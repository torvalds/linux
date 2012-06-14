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

#ifndef _ASM_TILE_BITOPS_32_H
#define _ASM_TILE_BITOPS_32_H

#include <linux/compiler.h>
#include <linux/atomic.h>

/* Tile-specific routines to support <asm/bitops.h>. */
unsigned long _atomic_or(volatile unsigned long *p, unsigned long mask);
unsigned long _atomic_andn(volatile unsigned long *p, unsigned long mask);
unsigned long _atomic_xor(volatile unsigned long *p, unsigned long mask);

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.
 * See __set_bit() if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(unsigned nr, volatile unsigned long *addr)
{
	_atomic_or(addr + BIT_WORD(nr), BIT_MASK(nr));
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.
 * See __clear_bit() if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 *
 * clear_bit() may not contain a memory barrier, so if it is used for
 * locking purposes, you should call smp_mb__before_clear_bit() and/or
 * smp_mb__after_clear_bit() to ensure changes are visible on other cpus.
 */
static inline void clear_bit(unsigned nr, volatile unsigned long *addr)
{
	_atomic_andn(addr + BIT_WORD(nr), BIT_MASK(nr));
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * See __change_bit() if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void change_bit(unsigned nr, volatile unsigned long *addr)
{
	_atomic_xor(addr + BIT_WORD(nr), BIT_MASK(nr));
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_set_bit(unsigned nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	addr += BIT_WORD(nr);
	smp_mb();  /* barrier for proper semantics */
	return (_atomic_or(addr, mask) & mask) != 0;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_clear_bit(unsigned nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	addr += BIT_WORD(nr);
	smp_mb();  /* barrier for proper semantics */
	return (_atomic_andn(addr, mask) & mask) != 0;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_change_bit(unsigned nr,
				      volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	addr += BIT_WORD(nr);
	smp_mb();  /* barrier for proper semantics */
	return (_atomic_xor(addr, mask) & mask) != 0;
}

/* See discussion at smp_mb__before_atomic_dec() in <asm/atomic_32.h>. */
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	do {} while (0)

#include <asm-generic/bitops/ext2-atomic.h>

#endif /* _ASM_TILE_BITOPS_32_H */
