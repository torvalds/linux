/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Macros for accessing the [V]PCLMULQDQ-based CRC functions that are
 * instantiated by crc-pclmul-template.S
 *
 * Copyright 2025 Google LLC
 *
 * Author: Eric Biggers <ebiggers@google.com>
 */
#ifndef _CRC_PCLMUL_TEMPLATE_H
#define _CRC_PCLMUL_TEMPLATE_H

#include <asm/cpufeatures.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <linux/static_call.h>
#include "crc-pclmul-consts.h"

#define DECLARE_CRC_PCLMUL_FUNCS(prefix, crc_t)				\
crc_t prefix##_pclmul_sse(crc_t crc, const u8 *p, size_t len,		\
			  const void *consts_ptr);			\
crc_t prefix##_vpclmul_avx2(crc_t crc, const u8 *p, size_t len,		\
			    const void *consts_ptr);			\
crc_t prefix##_vpclmul_avx512(crc_t crc, const u8 *p, size_t len,	\
			      const void *consts_ptr);			\
DEFINE_STATIC_CALL(prefix##_pclmul, prefix##_pclmul_sse)

#define INIT_CRC_PCLMUL(prefix)						\
do {									\
	if (IS_ENABLED(CONFIG_AS_VPCLMULQDQ) &&				\
	    boot_cpu_has(X86_FEATURE_VPCLMULQDQ) &&			\
	    boot_cpu_has(X86_FEATURE_AVX2) &&				\
	    cpu_has_xfeatures(XFEATURE_MASK_YMM, NULL)) {		\
		if (boot_cpu_has(X86_FEATURE_AVX512BW) &&		\
		    boot_cpu_has(X86_FEATURE_AVX512VL) &&		\
		    !boot_cpu_has(X86_FEATURE_PREFER_YMM) &&		\
		    cpu_has_xfeatures(XFEATURE_MASK_AVX512, NULL)) {	\
			static_call_update(prefix##_pclmul,		\
					   prefix##_vpclmul_avx512);	\
		} else {						\
			static_call_update(prefix##_pclmul,		\
					   prefix##_vpclmul_avx2);	\
		}							\
	}								\
} while (0)

/*
 * Call a [V]PCLMULQDQ optimized CRC function if the data length is at least 16
 * bytes, the CPU has PCLMULQDQ support, and the current context may use SIMD.
 *
 * 16 bytes is the minimum length supported by the [V]PCLMULQDQ functions.
 * There is overhead associated with kernel_fpu_begin() and kernel_fpu_end(),
 * varying by CPU and factors such as which parts of the "FPU" state userspace
 * has touched, which could result in a larger cutoff being better.  Indeed, a
 * larger cutoff is usually better for a *single* message.  However, the
 * overhead of the FPU section gets amortized if multiple FPU sections get
 * executed before returning to userspace, since the XSAVE and XRSTOR occur only
 * once.  Considering that and the fact that the [V]PCLMULQDQ code is lighter on
 * the dcache than the table-based code is, a 16-byte cutoff seems to work well.
 */
#define CRC_PCLMUL(crc, p, len, prefix, consts, have_pclmulqdq)		\
do {									\
	if ((len) >= 16 && static_branch_likely(&(have_pclmulqdq)) &&	\
	    crypto_simd_usable()) {					\
		const void *consts_ptr;					\
									\
		consts_ptr = (consts).fold_across_128_bits_consts;	\
		kernel_fpu_begin();					\
		crc = static_call(prefix##_pclmul)((crc), (p), (len),	\
						   consts_ptr);		\
		kernel_fpu_end();					\
		return crc;						\
	}								\
} while (0)

#endif /* _CRC_PCLMUL_TEMPLATE_H */
