// SPDX-License-Identifier: GPL-2.0+
/*
 * Cryptographic API.
 *
 * s390 implementation of the DES Cipher Algorithm.
 *
 * Copyright IBM Corp. 2003, 2011
 * Author(s): Thomas Spatzier
 *	      Jan Glauber (jan.glauber@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/fips.h>
#include <linux/mutex.h>
#include <crypto/algapi.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>
#include <asm/cpacf.h>

#define DES3_KEY_SIZE	(3 * DES_KEY_SIZE)

static u8 *ctrblk;
static DEFINE_MUTEX(ctrblk_lock);

static cpacf_mask_t km_functions, kmc_functions, kmctr_functions;

struct s390_des_ctx {
	u8 iv[DES_BLOCK_SIZE];
	u8 key[DES3_KEY_SIZE];
};

static int des_setkey(struct crypto_tfm *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	err = crypto_des_verify_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, key_len);
	return 0;
}

static int des_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
			       unsigned int key_len)
{
	return des_setkey(crypto_skcipher_tfm(tfm), key, key_len);
}

static void s390_des_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_DEA, ctx->key, out, in, DES_BLOCK_SIZE);
}

static void s390_des_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_DEA | CPACF_DECRYPT,
		 ctx->key, out, in, DES_BLOCK_SIZE);
}

static struct crypto_alg des_alg = {
	.cra_name		=	"des",
	.cra_driver_name	=	"des-s390",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES_KEY_SIZE,
			.cia_max_keysize	=	DES_KEY_SIZE,
			.cia_setkey		=	des_setkey,
			.cia_encrypt		=	s390_des_encrypt,
			.cia_decrypt		=	s390_des_decrypt,
		}
	}
};

static int ecb_desall_crypt(struct skcipher_request *req, unsigned long fc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_des_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes, n;
	int ret;

	ret = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(DES_BLOCK_SIZE - 1);
		cpacf_km(fc, ctx->key, walk.dst.virt.addr,
			 walk.src.virt.addr, n);
		ret = skcipher_walk_done(&walk, nbytes - n);
	}
	return ret;
}

static int cbc_desall_crypt(struct skcipher_request *req, unsigned long fc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_des_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes, n;
	int ret;
	struct {
		u8 iv[DES_BLOCK_SIZE];
		u8 key[DES3_KEY_SIZE];
	} param;

	ret = skcipher_walk_virt(&walk, req, false);
	if (ret)
		return ret;
	memcpy(param.iv, walk.iv, DES_BLOCK_SIZE);
	memcpy(param.key, ctx->key, DES3_KEY_SIZE);
	while ((nbytes = walk.nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(DES_BLOCK_SIZE - 1);
		cpacf_kmc(fc, &param, walk.dst.virt.addr,
			  walk.src.virt.addr, n);
		memcpy(walk.iv, param.iv, DES_BLOCK_SIZE);
		ret = skcipher_walk_done(&walk, nbytes - n);
	}
	return ret;
}

static int ecb_des_encrypt(struct skcipher_request *req)
{
	return ecb_desall_crypt(req, CPACF_KM_DEA);
}

static int ecb_des_decrypt(struct skcipher_request *req)
{
	return ecb_desall_crypt(req, CPACF_KM_DEA | CPACF_DECRYPT);
}

static struct skcipher_alg ecb_des_alg = {
	.base.cra_name		=	"ecb(des)",
	.base.cra_driver_name	=	"ecb-des-s390",
	.base.cra_priority	=	400,	/* combo: des + ecb */
	.base.cra_blocksize	=	DES_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES_KEY_SIZE,
	.max_keysize		=	DES_KEY_SIZE,
	.setkey			=	des_setkey_skcipher,
	.encrypt		=	ecb_des_encrypt,
	.decrypt		=	ecb_des_decrypt,
};

static int cbc_des_encrypt(struct skcipher_request *req)
{
	return cbc_desall_crypt(req, CPACF_KMC_DEA);
}

static int cbc_des_decrypt(struct skcipher_request *req)
{
	return cbc_desall_crypt(req, CPACF_KMC_DEA | CPACF_DECRYPT);
}

static struct skcipher_alg cbc_des_alg = {
	.base.cra_name		=	"cbc(des)",
	.base.cra_driver_name	=	"cbc-des-s390",
	.base.cra_priority	=	400,	/* combo: des + cbc */
	.base.cra_blocksize	=	DES_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES_KEY_SIZE,
	.max_keysize		=	DES_KEY_SIZE,
	.ivsize			=	DES_BLOCK_SIZE,
	.setkey			=	des_setkey_skcipher,
	.encrypt		=	cbc_des_encrypt,
	.decrypt		=	cbc_des_decrypt,
};

/*
 * RFC2451:
 *
 *   For DES-EDE3, there is no known need to reject weak or
 *   complementation keys.  Any weakness is obviated by the use of
 *   multiple keys.
 *
 *   However, if the first two or last two independent 64-bit keys are
 *   equal (k1 == k2 or k2 == k3), then the DES3 operation is simply the
 *   same as DES.  Implementers MUST reject keys that exhibit this
 *   property.
 *
 *   In fips mode additinally check for all 3 keys are unique.
 *
 */
static int des3_setkey(struct crypto_tfm *tfm, const u8 *key,
		       unsigned int key_len)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);
	int err;

	err = crypto_des3_ede_verify_key(tfm, key);
	if (err)
		return err;

	memcpy(ctx->key, key, key_len);
	return 0;
}

static int des3_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				unsigned int key_len)
{
	return des3_setkey(crypto_skcipher_tfm(tfm), key, key_len);
}

static void des3_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_TDEA_192, ctx->key, dst, src, DES_BLOCK_SIZE);
}

static void des3_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct s390_des_ctx *ctx = crypto_tfm_ctx(tfm);

	cpacf_km(CPACF_KM_TDEA_192 | CPACF_DECRYPT,
		 ctx->key, dst, src, DES_BLOCK_SIZE);
}

static struct crypto_alg des3_alg = {
	.cra_name		=	"des3_ede",
	.cra_driver_name	=	"des3_ede-s390",
	.cra_priority		=	300,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	DES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct s390_des_ctx),
	.cra_module		=	THIS_MODULE,
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=	DES3_KEY_SIZE,
			.cia_max_keysize	=	DES3_KEY_SIZE,
			.cia_setkey		=	des3_setkey,
			.cia_encrypt		=	des3_encrypt,
			.cia_decrypt		=	des3_decrypt,
		}
	}
};

static int ecb_des3_encrypt(struct skcipher_request *req)
{
	return ecb_desall_crypt(req, CPACF_KM_TDEA_192);
}

static int ecb_des3_decrypt(struct skcipher_request *req)
{
	return ecb_desall_crypt(req, CPACF_KM_TDEA_192 | CPACF_DECRYPT);
}

static struct skcipher_alg ecb_des3_alg = {
	.base.cra_name		=	"ecb(des3_ede)",
	.base.cra_driver_name	=	"ecb-des3_ede-s390",
	.base.cra_priority	=	400,	/* combo: des3 + ecb */
	.base.cra_blocksize	=	DES_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES3_KEY_SIZE,
	.max_keysize		=	DES3_KEY_SIZE,
	.setkey			=	des3_setkey_skcipher,
	.encrypt		=	ecb_des3_encrypt,
	.decrypt		=	ecb_des3_decrypt,
};

static int cbc_des3_encrypt(struct skcipher_request *req)
{
	return cbc_desall_crypt(req, CPACF_KMC_TDEA_192);
}

static int cbc_des3_decrypt(struct skcipher_request *req)
{
	return cbc_desall_crypt(req, CPACF_KMC_TDEA_192 | CPACF_DECRYPT);
}

static struct skcipher_alg cbc_des3_alg = {
	.base.cra_name		=	"cbc(des3_ede)",
	.base.cra_driver_name	=	"cbc-des3_ede-s390",
	.base.cra_priority	=	400,	/* combo: des3 + cbc */
	.base.cra_blocksize	=	DES_BLOCK_SIZE,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES3_KEY_SIZE,
	.max_keysize		=	DES3_KEY_SIZE,
	.ivsize			=	DES_BLOCK_SIZE,
	.setkey			=	des3_setkey_skcipher,
	.encrypt		=	cbc_des3_encrypt,
	.decrypt		=	cbc_des3_decrypt,
};

static unsigned int __ctrblk_init(u8 *ctrptr, u8 *iv, unsigned int nbytes)
{
	unsigned int i, n;

	/* align to block size, max. PAGE_SIZE */
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(DES_BLOCK_SIZE - 1);
	memcpy(ctrptr, iv, DES_BLOCK_SIZE);
	for (i = (n / DES_BLOCK_SIZE) - 1; i > 0; i--) {
		memcpy(ctrptr + DES_BLOCK_SIZE, ctrptr, DES_BLOCK_SIZE);
		crypto_inc(ctrptr + DES_BLOCK_SIZE, DES_BLOCK_SIZE);
		ctrptr += DES_BLOCK_SIZE;
	}
	return n;
}

static int ctr_desall_crypt(struct skcipher_request *req, unsigned long fc)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_des_ctx *ctx = crypto_skcipher_ctx(tfm);
	u8 buf[DES_BLOCK_SIZE], *ctrptr;
	struct skcipher_walk walk;
	unsigned int n, nbytes;
	int ret, locked;

	locked = mutex_trylock(&ctrblk_lock);

	ret = skcipher_walk_virt(&walk, req, false);
	while ((nbytes = walk.nbytes) >= DES_BLOCK_SIZE) {
		n = DES_BLOCK_SIZE;
		if (nbytes >= 2*DES_BLOCK_SIZE && locked)
			n = __ctrblk_init(ctrblk, walk.iv, nbytes);
		ctrptr = (n > DES_BLOCK_SIZE) ? ctrblk : walk.iv;
		cpacf_kmctr(fc, ctx->key, walk.dst.virt.addr,
			    walk.src.virt.addr, n, ctrptr);
		if (ctrptr == ctrblk)
			memcpy(walk.iv, ctrptr + n - DES_BLOCK_SIZE,
				DES_BLOCK_SIZE);
		crypto_inc(walk.iv, DES_BLOCK_SIZE);
		ret = skcipher_walk_done(&walk, nbytes - n);
	}
	if (locked)
		mutex_unlock(&ctrblk_lock);
	/* final block may be < DES_BLOCK_SIZE, copy only nbytes */
	if (nbytes) {
		cpacf_kmctr(fc, ctx->key, buf, walk.src.virt.addr,
			    DES_BLOCK_SIZE, walk.iv);
		memcpy(walk.dst.virt.addr, buf, nbytes);
		crypto_inc(walk.iv, DES_BLOCK_SIZE);
		ret = skcipher_walk_done(&walk, 0);
	}
	return ret;
}

static int ctr_des_crypt(struct skcipher_request *req)
{
	return ctr_desall_crypt(req, CPACF_KMCTR_DEA);
}

static struct skcipher_alg ctr_des_alg = {
	.base.cra_name		=	"ctr(des)",
	.base.cra_driver_name	=	"ctr-des-s390",
	.base.cra_priority	=	400,	/* combo: des + ctr */
	.base.cra_blocksize	=	1,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES_KEY_SIZE,
	.max_keysize		=	DES_KEY_SIZE,
	.ivsize			=	DES_BLOCK_SIZE,
	.setkey			=	des_setkey_skcipher,
	.encrypt		=	ctr_des_crypt,
	.decrypt		=	ctr_des_crypt,
	.chunksize		=	DES_BLOCK_SIZE,
};

static int ctr_des3_crypt(struct skcipher_request *req)
{
	return ctr_desall_crypt(req, CPACF_KMCTR_TDEA_192);
}

static struct skcipher_alg ctr_des3_alg = {
	.base.cra_name		=	"ctr(des3_ede)",
	.base.cra_driver_name	=	"ctr-des3_ede-s390",
	.base.cra_priority	=	400,	/* combo: des3 + ede */
	.base.cra_blocksize	=	1,
	.base.cra_ctxsize	=	sizeof(struct s390_des_ctx),
	.base.cra_module	=	THIS_MODULE,
	.min_keysize		=	DES3_KEY_SIZE,
	.max_keysize		=	DES3_KEY_SIZE,
	.ivsize			=	DES_BLOCK_SIZE,
	.setkey			=	des3_setkey_skcipher,
	.encrypt		=	ctr_des3_crypt,
	.decrypt		=	ctr_des3_crypt,
	.chunksize		=	DES_BLOCK_SIZE,
};

static struct crypto_alg *des_s390_algs_ptr[2];
static int des_s390_algs_num;
static struct skcipher_alg *des_s390_skciphers_ptr[6];
static int des_s390_skciphers_num;

static int des_s390_register_alg(struct crypto_alg *alg)
{
	int ret;

	ret = crypto_register_alg(alg);
	if (!ret)
		des_s390_algs_ptr[des_s390_algs_num++] = alg;
	return ret;
}

static int des_s390_register_skcipher(struct skcipher_alg *alg)
{
	int ret;

	ret = crypto_register_skcipher(alg);
	if (!ret)
		des_s390_skciphers_ptr[des_s390_skciphers_num++] = alg;
	return ret;
}

static void des_s390_exit(void)
{
	while (des_s390_algs_num--)
		crypto_unregister_alg(des_s390_algs_ptr[des_s390_algs_num]);
	while (des_s390_skciphers_num--)
		crypto_unregister_skcipher(des_s390_skciphers_ptr[des_s390_skciphers_num]);
	if (ctrblk)
		free_page((unsigned long) ctrblk);
}

static int __init des_s390_init(void)
{
	int ret;

	/* Query available functions for KM, KMC and KMCTR */
	cpacf_query(CPACF_KM, &km_functions);
	cpacf_query(CPACF_KMC, &kmc_functions);
	cpacf_query(CPACF_KMCTR, &kmctr_functions);

	if (cpacf_test_func(&km_functions, CPACF_KM_DEA)) {
		ret = des_s390_register_alg(&des_alg);
		if (ret)
			goto out_err;
		ret = des_s390_register_skcipher(&ecb_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmc_functions, CPACF_KMC_DEA)) {
		ret = des_s390_register_skcipher(&cbc_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&km_functions, CPACF_KM_TDEA_192)) {
		ret = des_s390_register_alg(&des3_alg);
		if (ret)
			goto out_err;
		ret = des_s390_register_skcipher(&ecb_des3_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmc_functions, CPACF_KMC_TDEA_192)) {
		ret = des_s390_register_skcipher(&cbc_des3_alg);
		if (ret)
			goto out_err;
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_DEA) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_TDEA_192)) {
		ctrblk = (u8 *) __get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			ret = -ENOMEM;
			goto out_err;
		}
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_DEA)) {
		ret = des_s390_register_skcipher(&ctr_des_alg);
		if (ret)
			goto out_err;
	}
	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_TDEA_192)) {
		ret = des_s390_register_skcipher(&ctr_des3_alg);
		if (ret)
			goto out_err;
	}

	return 0;
out_err:
	des_s390_exit();
	return ret;
}

module_cpu_feature_match(MSA, des_s390_init);
module_exit(des_s390_exit);

MODULE_ALIAS_CRYPTO("des");
MODULE_ALIAS_CRYPTO("des3_ede");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DES & Triple DES EDE Cipher Algorithms");
