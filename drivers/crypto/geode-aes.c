// SPDX-License-Identifier: GPL-2.0-or-later
 /* Copyright (C) 2004-2006, Advanced Micro Devices, Inc.
  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/internal/skcipher.h>

#include <linux/io.h>
#include <linux/delay.h>

#include "geode-aes.h"

/* Static structures */

static void __iomem *_iobase;
static spinlock_t lock;

/* Write a 128 bit field (either a writable key or IV) */
static inline void
_writefield(u32 offset, const void *value)
{
	int i;

	for (i = 0; i < 4; i++)
		iowrite32(((const u32 *) value)[i], _iobase + offset + (i * 4));
}

/* Read a 128 bit field (either a writable key or IV) */
static inline void
_readfield(u32 offset, void *value)
{
	int i;

	for (i = 0; i < 4; i++)
		((u32 *) value)[i] = ioread32(_iobase + offset + (i * 4));
}

static int
do_crypt(const void *src, void *dst, u32 len, u32 flags)
{
	u32 status;
	u32 counter = AES_OP_TIMEOUT;

	iowrite32(virt_to_phys((void *)src), _iobase + AES_SOURCEA_REG);
	iowrite32(virt_to_phys(dst), _iobase + AES_DSTA_REG);
	iowrite32(len,  _iobase + AES_LENA_REG);

	/* Start the operation */
	iowrite32(AES_CTRL_START | flags, _iobase + AES_CTRLA_REG);

	do {
		status = ioread32(_iobase + AES_INTR_REG);
		cpu_relax();
	} while (!(status & AES_INTRA_PENDING) && --counter);

	/* Clear the event */
	iowrite32((status & 0xFF) | AES_INTRA_PENDING, _iobase + AES_INTR_REG);
	return counter ? 0 : 1;
}

static void
geode_aes_crypt(const struct geode_aes_tfm_ctx *tctx, const void *src,
		void *dst, u32 len, u8 *iv, int mode, int dir)
{
	u32 flags = 0;
	unsigned long iflags;
	int ret;

	/* If the source and destination is the same, then
	 * we need to turn on the coherent flags, otherwise
	 * we don't need to worry
	 */

	flags |= (AES_CTRL_DCA | AES_CTRL_SCA);

	if (dir == AES_DIR_ENCRYPT)
		flags |= AES_CTRL_ENCRYPT;

	/* Start the critical section */

	spin_lock_irqsave(&lock, iflags);

	if (mode == AES_MODE_CBC) {
		flags |= AES_CTRL_CBC;
		_writefield(AES_WRITEIV0_REG, iv);
	}

	flags |= AES_CTRL_WRKEY;
	_writefield(AES_WRITEKEY0_REG, tctx->key);

	ret = do_crypt(src, dst, len, flags);
	BUG_ON(ret);

	if (mode == AES_MODE_CBC)
		_readfield(AES_WRITEIV0_REG, iv);

	spin_unlock_irqrestore(&lock, iflags);
}

/* CRYPTO-API Functions */

static int geode_setkey_cip(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
	struct geode_aes_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	tctx->keylen = len;

	if (len == AES_KEYSIZE_128) {
		memcpy(tctx->key, key, len);
		return 0;
	}

	if (len != AES_KEYSIZE_192 && len != AES_KEYSIZE_256)
		/* not supported at all */
		return -EINVAL;

	/*
	 * The requested key size is not supported by HW, do a fallback
	 */
	tctx->fallback.cip->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	tctx->fallback.cip->base.crt_flags |=
		(tfm->crt_flags & CRYPTO_TFM_REQ_MASK);

	return crypto_cipher_setkey(tctx->fallback.cip, key, len);
}

static int geode_setkey_skcipher(struct crypto_skcipher *tfm, const u8 *key,
				 unsigned int len)
{
	struct geode_aes_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	tctx->keylen = len;

	if (len == AES_KEYSIZE_128) {
		memcpy(tctx->key, key, len);
		return 0;
	}

	if (len != AES_KEYSIZE_192 && len != AES_KEYSIZE_256)
		/* not supported at all */
		return -EINVAL;

	/*
	 * The requested key size is not supported by HW, do a fallback
	 */
	crypto_skcipher_clear_flags(tctx->fallback.skcipher,
				    CRYPTO_TFM_REQ_MASK);
	crypto_skcipher_set_flags(tctx->fallback.skcipher,
				  crypto_skcipher_get_flags(tfm) &
				  CRYPTO_TFM_REQ_MASK);
	return crypto_skcipher_setkey(tctx->fallback.skcipher, key, len);
}

static void
geode_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct geode_aes_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	if (unlikely(tctx->keylen != AES_KEYSIZE_128)) {
		crypto_cipher_encrypt_one(tctx->fallback.cip, out, in);
		return;
	}

	geode_aes_crypt(tctx, in, out, AES_BLOCK_SIZE, NULL,
			AES_MODE_ECB, AES_DIR_ENCRYPT);
}


static void
geode_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	const struct geode_aes_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	if (unlikely(tctx->keylen != AES_KEYSIZE_128)) {
		crypto_cipher_decrypt_one(tctx->fallback.cip, out, in);
		return;
	}

	geode_aes_crypt(tctx, in, out, AES_BLOCK_SIZE, NULL,
			AES_MODE_ECB, AES_DIR_DECRYPT);
}

static int fallback_init_cip(struct crypto_tfm *tfm)
{
	const char *name = crypto_tfm_alg_name(tfm);
	struct geode_aes_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	tctx->fallback.cip = crypto_alloc_cipher(name, 0,
						 CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(tctx->fallback.cip)) {
		printk(KERN_ERR "Error allocating fallback algo %s\n", name);
		return PTR_ERR(tctx->fallback.cip);
	}

	return 0;
}

static void fallback_exit_cip(struct crypto_tfm *tfm)
{
	struct geode_aes_tfm_ctx *tctx = crypto_tfm_ctx(tfm);

	crypto_free_cipher(tctx->fallback.cip);
}

static struct crypto_alg geode_alg = {
	.cra_name			=	"aes",
	.cra_driver_name	=	"geode-aes",
	.cra_priority		=	300,
	.cra_alignmask		=	15,
	.cra_flags			=	CRYPTO_ALG_TYPE_CIPHER |
							CRYPTO_ALG_NEED_FALLBACK,
	.cra_init			=	fallback_init_cip,
	.cra_exit			=	fallback_exit_cip,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct geode_aes_tfm_ctx),
	.cra_module			=	THIS_MODULE,
	.cra_u				=	{
		.cipher	=	{
			.cia_min_keysize	=	AES_MIN_KEY_SIZE,
			.cia_max_keysize	=	AES_MAX_KEY_SIZE,
			.cia_setkey			=	geode_setkey_cip,
			.cia_encrypt		=	geode_encrypt,
			.cia_decrypt		=	geode_decrypt
		}
	}
};

static int geode_init_skcipher(struct crypto_skcipher *tfm)
{
	const char *name = crypto_tfm_alg_name(&tfm->base);
	struct geode_aes_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	tctx->fallback.skcipher =
		crypto_alloc_skcipher(name, 0, CRYPTO_ALG_NEED_FALLBACK |
				      CRYPTO_ALG_ASYNC);
	if (IS_ERR(tctx->fallback.skcipher)) {
		printk(KERN_ERR "Error allocating fallback algo %s\n", name);
		return PTR_ERR(tctx->fallback.skcipher);
	}

	crypto_skcipher_set_reqsize(tfm, sizeof(struct skcipher_request) +
				    crypto_skcipher_reqsize(tctx->fallback.skcipher));
	return 0;
}

static void geode_exit_skcipher(struct crypto_skcipher *tfm)
{
	struct geode_aes_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);

	crypto_free_skcipher(tctx->fallback.skcipher);
}

static int geode_skcipher_crypt(struct skcipher_request *req, int mode, int dir)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct geode_aes_tfm_ctx *tctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	if (unlikely(tctx->keylen != AES_KEYSIZE_128)) {
		struct skcipher_request *subreq = skcipher_request_ctx(req);

		*subreq = *req;
		skcipher_request_set_tfm(subreq, tctx->fallback.skcipher);
		if (dir == AES_DIR_DECRYPT)
			return crypto_skcipher_decrypt(subreq);
		else
			return crypto_skcipher_encrypt(subreq);
	}

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) != 0) {
		geode_aes_crypt(tctx, walk.src.virt.addr, walk.dst.virt.addr,
				round_down(nbytes, AES_BLOCK_SIZE),
				walk.iv, mode, dir);
		err = skcipher_walk_done(&walk, nbytes % AES_BLOCK_SIZE);
	}

	return err;
}

static int geode_cbc_encrypt(struct skcipher_request *req)
{
	return geode_skcipher_crypt(req, AES_MODE_CBC, AES_DIR_ENCRYPT);
}

static int geode_cbc_decrypt(struct skcipher_request *req)
{
	return geode_skcipher_crypt(req, AES_MODE_CBC, AES_DIR_DECRYPT);
}

static int geode_ecb_encrypt(struct skcipher_request *req)
{
	return geode_skcipher_crypt(req, AES_MODE_ECB, AES_DIR_ENCRYPT);
}

static int geode_ecb_decrypt(struct skcipher_request *req)
{
	return geode_skcipher_crypt(req, AES_MODE_ECB, AES_DIR_DECRYPT);
}

static struct skcipher_alg geode_skcipher_algs[] = {
	{
		.base.cra_name		= "cbc(aes)",
		.base.cra_driver_name	= "cbc-aes-geode",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct geode_aes_tfm_ctx),
		.base.cra_alignmask	= 15,
		.base.cra_module	= THIS_MODULE,
		.init			= geode_init_skcipher,
		.exit			= geode_exit_skcipher,
		.setkey			= geode_setkey_skcipher,
		.encrypt		= geode_cbc_encrypt,
		.decrypt		= geode_cbc_decrypt,
		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
		.ivsize			= AES_BLOCK_SIZE,
	}, {
		.base.cra_name		= "ecb(aes)",
		.base.cra_driver_name	= "ecb-aes-geode",
		.base.cra_priority	= 400,
		.base.cra_flags		= CRYPTO_ALG_KERN_DRIVER_ONLY |
					  CRYPTO_ALG_NEED_FALLBACK,
		.base.cra_blocksize	= AES_BLOCK_SIZE,
		.base.cra_ctxsize	= sizeof(struct geode_aes_tfm_ctx),
		.base.cra_alignmask	= 15,
		.base.cra_module	= THIS_MODULE,
		.init			= geode_init_skcipher,
		.exit			= geode_exit_skcipher,
		.setkey			= geode_setkey_skcipher,
		.encrypt		= geode_ecb_encrypt,
		.decrypt		= geode_ecb_decrypt,
		.min_keysize		= AES_MIN_KEY_SIZE,
		.max_keysize		= AES_MAX_KEY_SIZE,
	},
};

static void geode_aes_remove(struct pci_dev *dev)
{
	crypto_unregister_alg(&geode_alg);
	crypto_unregister_skciphers(geode_skcipher_algs,
				    ARRAY_SIZE(geode_skcipher_algs));

	pci_iounmap(dev, _iobase);
	_iobase = NULL;

	pci_release_regions(dev);
	pci_disable_device(dev);
}


static int geode_aes_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret;

	ret = pci_enable_device(dev);
	if (ret)
		return ret;

	ret = pci_request_regions(dev, "geode-aes");
	if (ret)
		goto eenable;

	_iobase = pci_iomap(dev, 0, 0);

	if (_iobase == NULL) {
		ret = -ENOMEM;
		goto erequest;
	}

	spin_lock_init(&lock);

	/* Clear any pending activity */
	iowrite32(AES_INTR_PENDING | AES_INTR_MASK, _iobase + AES_INTR_REG);

	ret = crypto_register_alg(&geode_alg);
	if (ret)
		goto eiomap;

	ret = crypto_register_skciphers(geode_skcipher_algs,
					ARRAY_SIZE(geode_skcipher_algs));
	if (ret)
		goto ealg;

	dev_notice(&dev->dev, "GEODE AES engine enabled.\n");
	return 0;

 ealg:
	crypto_unregister_alg(&geode_alg);

 eiomap:
	pci_iounmap(dev, _iobase);

 erequest:
	pci_release_regions(dev);

 eenable:
	pci_disable_device(dev);

	dev_err(&dev->dev, "GEODE AES initialization failed.\n");
	return ret;
}

static struct pci_device_id geode_aes_tbl[] = {
	{ PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_LX_AES), },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, geode_aes_tbl);

static struct pci_driver geode_aes_driver = {
	.name = "Geode LX AES",
	.id_table = geode_aes_tbl,
	.probe = geode_aes_probe,
	.remove = geode_aes_remove,
};

module_pci_driver(geode_aes_driver);

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("Geode LX Hardware AES driver");
MODULE_LICENSE("GPL");
