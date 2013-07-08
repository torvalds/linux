/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _OBD_LOV_H__
#define _OBD_LOV_H__

#define LOV_DEFAULT_STRIPE_SIZE (1 << LNET_MTU_BITS)

static inline int lov_stripe_md_size(__u16 stripes)
{
	return sizeof(struct lov_stripe_md) + stripes*sizeof(struct lov_oinfo*);
}

static inline __u32 lov_mds_md_size(__u16 stripes, __u32 lmm_magic)
{
	if (lmm_magic == LOV_MAGIC_V3)
		return sizeof(struct lov_mds_md_v3) +
			stripes * sizeof(struct lov_ost_data_v1);
	else
		return sizeof(struct lov_mds_md_v1) +
			stripes * sizeof(struct lov_ost_data_v1);
}

struct lov_version_size {
	__u32   lvs_magic;
	size_t  lvs_lmm_size;
	size_t  lvs_lod_size;
};

static inline __u32 lov_mds_md_stripecnt(int ea_size, __u32 lmm_magic)
{
	static const struct lov_version_size lmm_ver_size[] = {
			{ .lvs_magic = LOV_MAGIC_V3,
			  .lvs_lmm_size = sizeof(struct lov_mds_md_v3),
			  .lvs_lod_size = sizeof(struct lov_ost_data_v1) },
			{ .lvs_magic = LOV_MAGIC_V1,
			  .lvs_lmm_size = sizeof(struct lov_mds_md_v1),
			  .lvs_lod_size = sizeof(struct lov_ost_data_v1)} };
	int i;

	for (i = 0; i < ARRAY_SIZE(lmm_ver_size); i++) {
		if (lmm_magic == lmm_ver_size[i].lvs_magic) {
			if (ea_size <= lmm_ver_size[i].lvs_lmm_size)
				return 0;
			return (ea_size - lmm_ver_size[i].lvs_lmm_size) /
				lmm_ver_size[i].lvs_lod_size;
		}
	}

	/* Invalid LOV magic, so no stripes could fit */
	return 0;
}

/* lov_do_div64(a, b) returns a % b, and a = a / b.
 * The 32-bit code is LOV-specific due to knowing about stripe limits in
 * order to reduce the divisor to a 32-bit number.  If the divisor is
 * already a 32-bit value the compiler handles this directly. */
#if BITS_PER_LONG > 32
# define lov_do_div64(n,base) ({					\
	uint64_t __base = (base);					\
	uint64_t __rem;							\
	__rem = ((uint64_t)(n)) % __base;				\
	(n) = ((uint64_t)(n)) / __base;					\
	__rem;								\
  })
#else
# define lov_do_div64(n,base) ({					\
	uint64_t __rem;							\
	if ((sizeof(base) > 4) && (((base) & 0xffffffff00000000ULL) != 0)) {  \
		int __remainder;					      \
		LASSERTF(!((base) & (LOV_MIN_STRIPE_SIZE - 1)), "64 bit lov " \
			 "division %llu / %llu\n", (n), (uint64_t)(base));    \
		__remainder = (n) & (LOV_MIN_STRIPE_SIZE - 1);		\
		(n) >>= LOV_MIN_STRIPE_BITS;				\
		__rem = do_div(n, (base) >> LOV_MIN_STRIPE_BITS);	\
		__rem <<= LOV_MIN_STRIPE_BITS;				\
		__rem += __remainder;					\
	} else {							\
		__rem = do_div(n, base);				\
	}								\
	__rem;								\
  })
#endif

#define IOC_LOV_TYPE		   'g'
#define IOC_LOV_MIN_NR		 50
#define IOC_LOV_SET_OSC_ACTIVE	 _IOWR('g', 50, long)
#define IOC_LOV_MAX_NR		 50

#define QOS_DEFAULT_THRESHOLD	   10 /* MB */
#define QOS_DEFAULT_MAXAGE	      5  /* Seconds */

#endif
