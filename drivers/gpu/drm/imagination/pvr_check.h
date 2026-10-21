/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2026 Imagination Technologies Ltd. */

#ifndef PVR_CHECK_H
#define PVR_CHECK_H

#include <linux/build_bug.h>
#include <linux/overflow.h>
#include <linux/stddef.h>

#define OFFSET_CHECK(type, member, offset) \
	static_assert(offsetof(type, member) == (offset), \
	"offsetof(" #type ", " #member ") incorrect")

#define SIZE_CHECK(type, size) \
	static_assert(sizeof(type) == (size), #type " is incorrect size")

#define ALIGN_CHECK(type, align) \
	static_assert(__alignof__(type) <= (align), #type " has incorrect alignment")

/*
 * Where the last member of a struct is a flexible array member, using
 * SIZE_CHECK() is pointless. If the structure is not already padded to
 * alignment without the flexible array member, sizeof() will not match the
 * offset of the flexible array member and the "correct" sizeof() value is
 * completely meaningless.
 *
 * In those instances, use FLEX_ARRAY_CHECK() instead to assert that the final
 * field is a flexible array member and that it behaves as expected.
 */
#define FLEX_ARRAY_CHECK(type, member)                               \
	static_assert(flex_array_size((type *)NULL, member, 1) ==    \
				      sizeof_field(type, member[0]), \
		      #type "->" #member " is incorrect size")

#endif /* PVR_CHECK_H */
