// SPDX-License-Identifier: GPL-2.0-or-later

#include <crypto/curve25519.h>
#include <crypto/internal/kpp.h>
#include <crypto/kpp.h>
#include <linux/module.h>
#include <linux/scatterlist.h>

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

	curve25519_generic(buf, secret, bp);

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
	.base.cra_driver_name	= "curve25519-generic",
	.base.cra_priority	= 100,
	.base.cra_module	= THIS_MODULE,
	.base.cra_ctxsize	= CURVE25519_KEY_SIZE,

	.set_secret		= curve25519_set_secret,
	.generate_public_key	= curve25519_compute_value,
	.compute_shared_secret	= curve25519_compute_value,
	.max_size		= curve25519_max_size,
};

static int __init curve25519_init(void)
{
	return crypto_register_kpp(&curve25519_alg);
}

static void __exit curve25519_exit(void)
{
	crypto_unregister_kpp(&curve25519_alg);
}

subsys_initcall(curve25519_init);
module_exit(curve25519_exit);

MODULE_ALIAS_CRYPTO("curve25519");
MODULE_ALIAS_CRYPTO("curve25519-generic");
MODULE_DESCRIPTION("Curve25519 elliptic curve (RFC7748)");
MODULE_LICENSE("GPL");
