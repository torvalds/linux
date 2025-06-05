// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 SiFive
 * Author: Andy Chiu <andy.chiu@sifive.com>
 */
#include <linux/linkage.h>
#include <asm/asm.h>

#include <asm/vector.h>
#include <asm/simd.h>

#ifdef CONFIG_MMU
#include <asm/asm-prototypes.h>
#endif

#ifdef CONFIG_MMU
size_t riscv_v_usercopy_threshold = CONFIG_RISCV_ISA_V_UCOPY_THRESHOLD;
int __asm_vector_usercopy(void *dst, void *src, size_t n);
int __asm_vector_usercopy_sum_enabled(void *dst, void *src, size_t n);
int fallback_scalar_usercopy(void *dst, void *src, size_t n);
int fallback_scalar_usercopy_sum_enabled(void *dst, void *src, size_t n);
asmlinkage int enter_vector_usercopy(void *dst, void *src, size_t n,
				     bool enable_sum)
{
	size_t remain, copied;

	/* skip has_vector() check because it has been done by the asm  */
	if (!may_use_simd())
		goto fallback;

	kernel_vector_begin();
	remain = enable_sum ? __asm_vector_usercopy(dst, src, n) :
			      __asm_vector_usercopy_sum_enabled(dst, src, n);
	kernel_vector_end();

	if (remain) {
		copied = n - remain;
		dst += copied;
		src += copied;
		n = remain;
		goto fallback;
	}

	return remain;

fallback:
	return enable_sum ? fallback_scalar_usercopy(dst, src, n) :
			    fallback_scalar_usercopy_sum_enabled(dst, src, n);
}
#endif
