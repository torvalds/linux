/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef AMDGPU_UTILS_H_
#define AMDGPU_UTILS_H_

/* ---------- Generic 2‑bit capability attribute encoding ----------
 * 00 INVALID, 01 RO, 10 WO, 11 RW
 */
enum amdgpu_cap_attr {
	AMDGPU_CAP_ATTR_INVALID = 0,
	AMDGPU_CAP_ATTR_RO      = 1 << 0,
	AMDGPU_CAP_ATTR_WO      = 1 << 1,
	AMDGPU_CAP_ATTR_RW      = (AMDGPU_CAP_ATTR_RO | AMDGPU_CAP_ATTR_WO),
};

#define AMDGPU_CAP_ATTR_BITS 2
#define AMDGPU_CAP_ATTR_MAX  ((1U << AMDGPU_CAP_ATTR_BITS) - 1)

/* Internal helper to build helpers for a given enum NAME */
#define DECLARE_ATTR_CAP_CLASS_HELPERS(NAME)							\
enum { NAME##_BITMAP_BITS = NAME##_COUNT * AMDGPU_CAP_ATTR_BITS };				\
struct NAME##_caps {										\
	DECLARE_BITMAP(bmap, NAME##_BITMAP_BITS);						\
};												\
static inline unsigned int NAME##_ATTR_START(enum NAME##_cap_id cap)				\
{ return (unsigned int)cap * AMDGPU_CAP_ATTR_BITS; }						\
static inline void NAME##_attr_init(struct NAME##_caps *c)					\
{ if (c) bitmap_zero(c->bmap, NAME##_BITMAP_BITS); }						\
static inline int NAME##_attr_set(struct NAME##_caps *c,					\
				  enum NAME##_cap_id cap, enum amdgpu_cap_attr attr)		\
{												\
	if (!c)											\
		return -EINVAL;									\
	if (cap >= NAME##_COUNT)								\
		return -EINVAL;									\
	if ((unsigned int)attr > AMDGPU_CAP_ATTR_MAX)						\
		return -EINVAL;									\
	bitmap_write(c->bmap, (unsigned long)attr,						\
			NAME##_ATTR_START(cap), AMDGPU_CAP_ATTR_BITS);				\
	return 0;										\
}												\
static inline int NAME##_attr_get(const struct NAME##_caps *c,					\
				  enum NAME##_cap_id cap, enum amdgpu_cap_attr *out)		\
{												\
	unsigned long v;									\
	if (!c || !out)										\
		return -EINVAL;									\
	if (cap >= NAME##_COUNT)								\
		return -EINVAL;									\
	v = bitmap_read(c->bmap, NAME##_ATTR_START(cap), AMDGPU_CAP_ATTR_BITS);			\
	*out = (enum amdgpu_cap_attr)v;								\
	return 0;										\
}												\
static inline bool NAME##_cap_is_ro(const struct NAME##_caps *c, enum NAME##_cap_id id)		\
{ enum amdgpu_cap_attr a; return !NAME##_attr_get(c, id, &a) && a == AMDGPU_CAP_ATTR_RO; }	\
static inline bool NAME##_cap_is_wo(const struct NAME##_caps *c, enum NAME##_cap_id id)		\
{ enum amdgpu_cap_attr a; return !NAME##_attr_get(c, id, &a) && a == AMDGPU_CAP_ATTR_WO; }	\
static inline bool NAME##_cap_is_rw(const struct NAME##_caps *c, enum NAME##_cap_id id)		\
{ enum amdgpu_cap_attr a; return !NAME##_attr_get(c, id, &a) && a == AMDGPU_CAP_ATTR_RW; }

/* Element expander for enum creation */
#define _CAP_ENUM_ELEM(x) x,

/* Public macro: declare enum + helpers from an X‑macro list */
#define DECLARE_ATTR_CAP_CLASS(NAME, LIST_MACRO)						\
	enum NAME##_cap_id { LIST_MACRO(_CAP_ENUM_ELEM) NAME##_COUNT };				\
	DECLARE_ATTR_CAP_CLASS_HELPERS(NAME)

#endif /* AMDGPU_UTILS_H_ */
