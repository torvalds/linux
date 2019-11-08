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
#include <crypto/internal/kpp.h>
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
	if (static_branch_likely(&have_neon) && may_use_simd()) {
		kernel_neon_begin();
		curve25519_neon(out, scalar, point);
		kernel_neon_end();
	} else {
		curve25519_generic(out, scalar, point);
	}
}
EXPORT_SYMBOL(curve25519_arch);

static int curve25519_set_secret(struct crypto_kpp *tfm, const void *buf,
				 unsigned int len)
{
	u8 *secret = kpp_tfm_ctx(tfm);

	if (!len)
		curve25519_generate_secret(secret);
	else if (len == CURVE25519_KEY_SIZE &&
		 crypto_memneq(buf, curve25519_null_point, CURVE25519_KEY_SIZE))
		memcpy(secret, buf, CURVE25519_KEY_SIZE);
	else
		return -EINVAL;
	return 0;
}

static int curve25519_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	const u8 *secret = kpp_tfm_ctx(tfm);
	u8 public_key[CURVE25519_KEY_SIZE];
	u8 buf[CURVE25519_KEY_SIZE];
	int copied, nbytes;
	u8 const *bp;

	if (req->src) {
		copied = sg_copy_to_buffer(req->src,
					   sg_nents_for_len(req->src,
							    CURVE25519_KEY_SIZE),
					   public_key, CURVE25519_KEY_SIZE);
		if (copied != CURVE25519_KEY_SIZE)
			return -EINVAL;
		bp = public_key;
	} else {
		bp = curve25519_base_point;
	}

	curve25519_arch(buf, secret, bp);

	/* might want less than we've got */
	nbytes = min_t(size_t, CURVE25519_KEY_SIZE, req->dst_len);
	copied = sg_copy_from_buffer(req->dst, sg_nents_for_len(req->dst,
								nbytes),
				     buf, nbytes);
	if (copied != nbytes)
		return -EINVAL;
	return 0;
}

static unsigned int curve25519_max_size(struct crypto_kpp *tfm)
{
	return CURVE25519_KEY_SIZE;
}

static struct kpp_alg curve25519_alg = {
	.base.cra_name		= "curve25519",
	.base.cra_driver_name	= "curve25519-neon",
	.base.cra_priority	= 200,
	.base.cra_module	= THIS_MODULE,
	.base.cra_ctxsize	= CURVE25519_KEY_SIZE,

	.set_secret		= curve25519_set_secret,
	.generate_public_key	= curve25519_compute_value,
	.compute_shared_secret	= curve25519_compute_value,
	.max_size		= curve25519_max_size,
};

static int __init mod_init(void)
{
	if (elf_hwcap & HWCAP_NEON) {
		static_branch_enable(&have_neon);
		return crypto_register_kpp(&curve25519_alg);
	}
	return 0;
}

static void __exit mod_exit(void)
{
	if (elf_hwcap & HWCAP_NEON)
		crypto_unregister_kpp(&curve25519_alg);
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_ALIAS_CRYPTO("curve25519");
MODULE_ALIAS_CRYPTO("curve25519-neon");
MODULE_LICENSE("GPL v2");
