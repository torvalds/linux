// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <asm/checksum.h>
#include <asm/fpu.h>

/*
 * Computes the checksum of a memory block at src, length len,
 * and adds in "sum" (32-bit). If copy is true copies to dst.
 *
 * Returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic.
 *
 * This function must be called with even lengths, except
 * for the last fragment, which may be odd.
 *
 * It's best to have src and dst aligned on a 64-bit boundary.
 */
static __always_inline __wsum csum_copy(void *dst, const void *src, int len, __wsum sum, bool copy)
{
	DECLARE_KERNEL_FPU_ONSTACK8(vxstate);

	if (!cpu_has_vx()) {
		if (copy)
			memcpy(dst, src, len);
		return cksm(dst, len, sum);
	}
	kernel_fpu_begin(&vxstate, KERNEL_VXR_V16V23);
	fpu_vlvgf(16, (__force u32)sum, 1);
	fpu_vzero(17);
	fpu_vzero(18);
	fpu_vzero(19);
	while (len >= 64) {
		fpu_vlm(20, 23, src);
		if (copy) {
			fpu_vstm(20, 23, dst);
			dst += 64;
		}
		fpu_vcksm(16, 20, 16);
		fpu_vcksm(17, 21, 17);
		fpu_vcksm(18, 22, 18);
		fpu_vcksm(19, 23, 19);
		src += 64;
		len -= 64;
	}
	while (len >= 32) {
		fpu_vlm(20, 21, src);
		if (copy) {
			fpu_vstm(20, 21, dst);
			dst += 32;
		}
		fpu_vcksm(16, 20, 16);
		fpu_vcksm(17, 21, 17);
		src += 32;
		len -= 32;
	}
	while (len >= 16) {
		fpu_vl(20, src);
		if (copy) {
			fpu_vst(20, dst);
			dst += 16;
		}
		fpu_vcksm(16, 20, 16);
		src += 16;
		len -= 16;
	}
	if (len) {
		fpu_vll(20, len - 1, src);
		if (copy)
			fpu_vstl(20, len - 1, dst);
		fpu_vcksm(16, 20, 16);
	}
	fpu_vcksm(18, 19, 18);
	fpu_vcksm(16, 17, 16);
	fpu_vcksm(16, 18, 16);
	sum = (__force __wsum)fpu_vlgvf(16, 1);
	kernel_fpu_end(&vxstate, KERNEL_VXR_V16V23);
	return sum;
}

__wsum csum_partial(const void *buff, int len, __wsum sum)
{
	return csum_copy(NULL, buff, len, sum, false);
}
EXPORT_SYMBOL(csum_partial);

__wsum csum_partial_copy_nocheck(const void *src, void *dst, int len)
{
	return csum_copy(dst, src, len, 0, true);
}
EXPORT_SYMBOL(csum_partial_copy_nocheck);
