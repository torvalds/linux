/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Checksum routines
 *
 * Copyright (C) 2023 Rivos Inc.
 */
#ifndef __ASM_RISCV_CHECKSUM_H
#define __ASM_RISCV_CHECKSUM_H

#include <linux/in6.h>
#include <linux/uaccess.h>

#define ip_fast_csum ip_fast_csum

extern unsigned int do_csum(const unsigned char *buff, int len);
#define do_csum do_csum

/* Default version is sufficient for 32 bit */
#ifndef CONFIG_32BIT
#define _HAVE_ARCH_IPV6_CSUM
__sum16 csum_ipv6_magic(const struct in6_addr *saddr,
			const struct in6_addr *daddr,
			__u32 len, __u8 proto, __wsum sum);
#endif

/* Define riscv versions of functions before importing asm-generic/checksum.h */
#include <asm-generic/checksum.h>

/**
 * Quickly compute an IP checksum with the assumption that IPv4 headers will
 * always be in multiples of 32-bits, and have an ihl of at least 5.
 *
 * @ihl: the number of 32 bit segments and must be greater than or equal to 5.
 * @iph: assumed to be word aligned given that NET_IP_ALIGN is set to 2 on
 *  riscv, defining IP headers to be aligned.
 */
static inline __sum16 ip_fast_csum(const void *iph, unsigned int ihl)
{
	unsigned long csum = 0;
	int pos = 0;

	do {
		csum += ((const unsigned int *)iph)[pos];
		if (IS_ENABLED(CONFIG_32BIT))
			csum += csum < ((const unsigned int *)iph)[pos];
	} while (++pos < ihl);

	/*
	 * ZBB only saves three instructions on 32-bit and five on 64-bit so not
	 * worth checking if supported without Alternatives.
	 */
	if (IS_ENABLED(CONFIG_RISCV_ISA_ZBB) &&
	    IS_ENABLED(CONFIG_RISCV_ALTERNATIVE)) {
		unsigned long fold_temp;

		asm_volatile_goto(ALTERNATIVE("j %l[no_zbb]", "nop", 0,
					      RISCV_ISA_EXT_ZBB, 1)
		    :
		    :
		    :
		    : no_zbb);

		if (IS_ENABLED(CONFIG_32BIT)) {
			asm(".option push				\n\
			.option arch,+zbb				\n\
				not	%[fold_temp], %[csum]		\n\
				rori	%[csum], %[csum], 16		\n\
				sub	%[csum], %[fold_temp], %[csum]	\n\
			.option pop"
			: [csum] "+r" (csum), [fold_temp] "=&r" (fold_temp));
		} else {
			asm(".option push				\n\
			.option arch,+zbb				\n\
				rori	%[fold_temp], %[csum], 32	\n\
				add	%[csum], %[fold_temp], %[csum]	\n\
				srli	%[csum], %[csum], 32		\n\
				not	%[fold_temp], %[csum]		\n\
				roriw	%[csum], %[csum], 16		\n\
				subw	%[csum], %[fold_temp], %[csum]	\n\
			.option pop"
			: [csum] "+r" (csum), [fold_temp] "=&r" (fold_temp));
		}
		return (__force __sum16)(csum >> 16);
	}
no_zbb:
#ifndef CONFIG_32BIT
	csum += ror64(csum, 32);
	csum >>= 32;
#endif
	return csum_fold((__force __wsum)csum);
}

#endif /* __ASM_RISCV_CHECKSUM_H */
