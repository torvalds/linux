/* $NetBSD: all_sync_ops_linkable.c,v 1.4 2014/02/21 10:26:25 martin Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Martin Husemann <martin@NetBSD.org>.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is a simple link-time test to verify all builtin atomic sync
 * operations are available. Depending on the exact cpu/arch code generator
 * options, some of these need support functions (which on NetBSD we
 * typically provide in src/common/lib/libc/atomic).
 *
 * The list of operations has been extracted from sync-builtins.def file
 * in the gcc distribution (as of gcc 4.8.2).
 */

#include <machine/types.h>
#include <sys/inttypes.h>

volatile uint8_t u8 = 0;
volatile uint16_t u16 = 0;
volatile uint32_t u32 = 0;

#ifdef __HAVE_ATOMIC64_OPS
volatile uint64_t u64 = 0;
#endif

int
main(int argc, char **argv)
{
	__sync_synchronize();
	__sync_add_and_fetch(&u8, 1);
	__sync_add_and_fetch_1(&u8, 1);
	__sync_add_and_fetch_2(&u16, 1);
	__sync_add_and_fetch_4(&u32, 1);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_add_and_fetch_8(&u64, 1);
#endif
	__sync_bool_compare_and_swap(&u8, 1, 2);
	__sync_bool_compare_and_swap_1(&u8, 1, 2);
	__sync_bool_compare_and_swap_2(&u16, 1, 2);
	__sync_bool_compare_and_swap_4(&u32, 1, 2);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_bool_compare_and_swap_8(&u64, 1, 2);
#endif
	__sync_fetch_and_add(&u8, 1);
	__sync_fetch_and_add_1(&u8, 1);
	__sync_fetch_and_add_2(&u16, 1);
	__sync_fetch_and_add_4(&u32, 1);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_add_8(&u64, 1);
#endif
	__sync_fetch_and_and(&u8, 0x80);
	__sync_fetch_and_and_1(&u8, 0x80);
	__sync_fetch_and_and_2(&u16, 0x80);
	__sync_fetch_and_and_4(&u32, 0x80);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_and_8(&u64, 0x80);
#endif
#ifndef __clang__
	__sync_fetch_and_nand(&u8, 0x80);
	__sync_fetch_and_nand_1(&u8, 0x80);
	__sync_fetch_and_nand_2(&u16, 0x80);
	__sync_fetch_and_nand_4(&u32, 0x80);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_nand_8(&u64, 0x80);
#endif
#endif
	__sync_fetch_and_or(&u8, 0x80);
	__sync_fetch_and_or_1(&u8, 0x80);
	__sync_fetch_and_or_2(&u16, 0x80);
	__sync_fetch_and_or_4(&u32, 0x80);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_or_8(&u64, 0x80);
#endif
	__sync_fetch_and_sub(&u8, 0x80);
	__sync_fetch_and_sub_1(&u8, 0x80);
	__sync_fetch_and_sub_2(&u16, 0x80);
	__sync_fetch_and_sub_4(&u32, 0x80);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_sub_8(&u64, 0x80);
#endif
	__sync_fetch_and_xor(&u8, 0x80);
	__sync_fetch_and_xor_1(&u8, 0x80);
	__sync_fetch_and_xor_2(&u16, 0x80);
	__sync_fetch_and_xor_4(&u32, 0x80);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_fetch_and_xor_8(&u64, 0x80);
#endif
	__sync_lock_release(&u8);
	__sync_lock_release_1(&u8);
	__sync_lock_release_2(&u16);
	__sync_lock_release_4(&u32);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_lock_release_8(&u64);
#endif
	__sync_lock_test_and_set(&u8, 5);
	__sync_lock_test_and_set_1(&u8, 5);
	__sync_lock_test_and_set_2(&u16, 5);
	__sync_lock_test_and_set_4(&u32, 5);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_lock_test_and_set_8(&u64, 5);
#endif
#ifndef __clang__
	__sync_nand_and_fetch(&u8, 5);
	__sync_nand_and_fetch_1(&u8, 5);
	__sync_nand_and_fetch_2(&u16, 5);
	__sync_nand_and_fetch_4(&u32, 5);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_nand_and_fetch_8(&u64, 5);
#endif
#endif
	__sync_or_and_fetch(&u8, 5);
	__sync_or_and_fetch_1(&u8, 5);
	__sync_or_and_fetch_2(&u16, 5);
	__sync_or_and_fetch_4(&u32, 5);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_or_and_fetch_8(&u64, 5);
#endif
	__sync_sub_and_fetch(&u8, 5);
	__sync_sub_and_fetch_1(&u8, 5);
	__sync_sub_and_fetch_2(&u16, 5);
	__sync_sub_and_fetch_4(&u32, 5);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_sub_and_fetch_8(&u64, 5);
#endif
	__sync_val_compare_and_swap(&u8, 5, 9);
	__sync_val_compare_and_swap_1(&u8, 5, 9);
	__sync_val_compare_and_swap_2(&u16, 5, 9);
	__sync_val_compare_and_swap_4(&u32, 5, 9);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_val_compare_and_swap_8(&u64, 5, 9);
#endif
	__sync_xor_and_fetch(&u8, 5);
	__sync_xor_and_fetch_1(&u8, 5);
	__sync_xor_and_fetch_2(&u16, 5);
	__sync_xor_and_fetch_4(&u32, 5);
#ifdef __HAVE_ATOMIC64_OPS
	__sync_xor_and_fetch_8(&u64, 5);
#endif

	return 0;
}
