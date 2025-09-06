// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * Based on public domain code from Daniel J. Bernstein and Peter Schwabe. This
 * began from SUPERCOP's curve25519/neon2/scalarmult.s, but has subsequently been
 * manually reworked for use in kernel space.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jump_label.h>
#include <crypto/curve25519.h>

asmlinkage void curve25519_neon(u8 mypublic[CURVE25519_KEY_SIZE],
				const u8 secret[CURVE25519_KEY_SIZE],
				const u8 basepoint[CURVE25519_KEY_SIZE]);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

void curve25519_arch(u8 out[CURVE25519_KEY_SIZE],
		     const u8 scalar[CURVE25519_KEY_SIZE],
		     const u8 point[CURVE25519_KEY_SIZE])
{
	if (static_branch_likely(&have_neon) && crypto_simd_usable()) {
		kernel_neon_begin();
		curve25519_neon(out, scalar, point);
		kernel_neon_end();
	} else {
		curve25519_generic(out, scalar, point);
	}
}
EXPORT_SYMBOL(curve25519_arch);

void curve25519_base_arch(u8 pub[CURVE25519_KEY_SIZE],
			  const u8 secret[CURVE25519_KEY_SIZE])
{
	return curve25519_arch(pub, secret, curve25519_base_point);
}
EXPORT_SYMBOL(curve25519_base_arch);

static int __init arm_curve25519_init(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
	return 0;
}

static void __exit arm_curve25519_exit(void)
{
}

module_init(arm_curve25519_init);
module_exit(arm_curve25519_exit);

MODULE_DESCRIPTION("Public key crypto: Curve25519 (NEON-accelerated)");
MODULE_LICENSE("GPL v2");
