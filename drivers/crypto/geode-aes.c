 /* Copyright (C) 2004-2006, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <crypto/algapi.h>

#include <asm/io.h>
#include <asm/delay.h>

#include "geode-aes.h"

/* Register definitions */

#define AES_CTRLA_REG  0x0000

#define AES_CTRL_START     0x01
#define AES_CTRL_DECRYPT   0x00
#define AES_CTRL_ENCRYPT   0x02
#define AES_CTRL_WRKEY     0x04
#define AES_CTRL_DCA       0x08
#define AES_CTRL_SCA       0x10
#define AES_CTRL_CBC       0x20

#define AES_INTR_REG  0x0008

#define AES_INTRA_PENDING (1 << 16)
#define AES_INTRB_PENDING (1 << 17)

#define AES_INTR_PENDING  (AES_INTRA_PENDING | AES_INTRB_PENDING)
#define AES_INTR_MASK     0x07

#define AES_SOURCEA_REG   0x0010
#define AES_DSTA_REG      0x0014
#define AES_LENA_REG      0x0018
#define AES_WRITEKEY0_REG 0x0030
#define AES_WRITEIV0_REG  0x0040

/*  A very large counter that is used to gracefully bail out of an
 *  operation in case of trouble
 */

#define AES_OP_TIMEOUT    0x50000

/* Static structures */

static void __iomem * _iobase;
static spinlock_t lock;

/* Write a 128 bit field (either a writable key or IV) */
static inline void
_writefield(u32 offset, void *value)
{
	int i;
	for(i = 0; i < 4; i++)
		iowrite32(((u32 *) value)[i], _iobase + offset + (i * 4));
}

/* Read a 128 bit field (either a writable key or IV) */
static inline void
_readfield(u32 offset, void *value)
{
	int i;
	for(i = 0; i < 4; i++)
		((u32 *) value)[i] = ioread32(_iobase + offset + (i * 4));
}

static int
do_crypt(void *src, void *dst, int len, u32 flags)
{
	u32 status;
	u32 counter = AES_OP_TIMEOUT;

	iowrite32(virt_to_phys(src), _iobase + AES_SOURCEA_REG);
	iowrite32(virt_to_phys(dst), _iobase + AES_DSTA_REG);
	iowrite32(len,  _iobase + AES_LENA_REG);

	/* Start the operation */
	iowrite32(AES_CTRL_START | flags, _iobase + AES_CTRLA_REG);

	do
		status = ioread32(_iobase + AES_INTR_REG);
	while(!(status & AES_INTRA_PENDING) && --counter);

	/* Clear the event */
	iowrite32((status & 0xFF) | AES_INTRA_PENDING, _iobase + AES_INTR_REG);
	return counter ? 0 : 1;
}

unsigned int
geode_aes_crypt(struct geode_aes_op *op)
{

	u32 flags = 0;
	int iflags;

	if (op->len == 0 || op->src == op->dst)
		return 0;

	if (op->flags & AES_FLAGS_COHERENT)
		flags |= (AES_CTRL_DCA | AES_CTRL_SCA);

	if (op->dir == AES_DIR_ENCRYPT)
		flags |= AES_CTRL_ENCRYPT;

	/* Start the critical section */

	spin_lock_irqsave(&lock, iflags);

	if (op->mode == AES_MODE_CBC) {
		flags |= AES_CTRL_CBC;
		_writefield(AES_WRITEIV0_REG, op->iv);
	}

	if (op->flags & AES_FLAGS_USRKEY) {
		flags |= AES_CTRL_WRKEY;
		_writefield(AES_WRITEKEY0_REG, op->key);
	}

	do_crypt(op->src, op->dst, op->len, flags);

	if (op->mode == AES_MODE_CBC)
		_readfield(AES_WRITEIV0_REG, op->iv);

	spin_unlock_irqrestore(&lock, iflags);

	return op->len;
}

/* CRYPTO-API Functions */

static int
geode_setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int len)
{
	struct geode_aes_op *op = crypto_tfm_ctx(tfm);

	if (len != AES_KEY_LENGTH) {
		tfm->crt_flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	memcpy(op->key, key, len);
	return 0;
}

static void
geode_encrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct geode_aes_op *op = crypto_tfm_ctx(tfm);

	if ((out == NULL) || (in == NULL))
		return;

	op->src = (void *) in;
	op->dst = (void *) out;
	op->mode = AES_MODE_ECB;
	op->flags = 0;
	op->len = AES_MIN_BLOCK_SIZE;
	op->dir = AES_DIR_ENCRYPT;

	geode_aes_crypt(op);
}


static void
geode_decrypt(struct crypto_tfm *tfm, u8 *out, const u8 *in)
{
	struct geode_aes_op *op = crypto_tfm_ctx(tfm);

	if ((out == NULL) || (in == NULL))
		return;

	op->src = (void *) in;
	op->dst = (void *) out;
	op->mode = AES_MODE_ECB;
	op->flags = 0;
	op->len = AES_MIN_BLOCK_SIZE;
	op->dir = AES_DIR_DECRYPT;

	geode_aes_crypt(op);
}


static struct crypto_alg geode_alg = {
	.cra_name               =       "aes",
	.cra_driver_name	=       "geode-aes-128",
	.cra_priority           =       300,
	.cra_alignmask          =       15,
	.cra_flags		=	CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		=	AES_MIN_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct geode_aes_op),
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(geode_alg.cra_list),
	.cra_u			=	{
		.cipher = {
			.cia_min_keysize	=  AES_KEY_LENGTH,
			.cia_max_keysize	=  AES_KEY_LENGTH,
			.cia_setkey		=  geode_setkey,
			.cia_encrypt		=  geode_encrypt,
			.cia_decrypt		=  geode_decrypt
		}
	}
};

static int
geode_cbc_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
	struct geode_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err, ret;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while((nbytes = walk.nbytes)) {
		op->src = walk.src.virt.addr,
		op->dst = walk.dst.virt.addr;
		op->mode = AES_MODE_CBC;
		op->len = nbytes - (nbytes % AES_MIN_BLOCK_SIZE);
		op->dir = AES_DIR_DECRYPT;

		memcpy(op->iv, walk.iv, AES_IV_LENGTH);

		ret = geode_aes_crypt(op);

		memcpy(walk.iv, op->iv, AES_IV_LENGTH);
		nbytes -= ret;

		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int
geode_cbc_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
	struct geode_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err, ret;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while((nbytes = walk.nbytes)) {
		op->src = walk.src.virt.addr,
		op->dst = walk.dst.virt.addr;
		op->mode = AES_MODE_CBC;
		op->len = nbytes - (nbytes % AES_MIN_BLOCK_SIZE);
		op->dir = AES_DIR_ENCRYPT;

		memcpy(op->iv, walk.iv, AES_IV_LENGTH);

		ret = geode_aes_crypt(op);
		nbytes -= ret;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static struct crypto_alg geode_cbc_alg = {
	.cra_name		=	"cbc(aes)",
	.cra_driver_name	=	"cbc-aes-geode-128",
	.cra_priority		=	400,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_MIN_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct geode_aes_op),
	.cra_alignmask		=	15,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(geode_cbc_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_KEY_LENGTH,
			.max_keysize		=	AES_KEY_LENGTH,
			.setkey			=	geode_setkey,
			.encrypt		=	geode_cbc_encrypt,
			.decrypt		=	geode_cbc_decrypt,
		}
	}
};

static int
geode_ecb_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
	struct geode_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err, ret;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while((nbytes = walk.nbytes)) {
		op->src = walk.src.virt.addr,
		op->dst = walk.dst.virt.addr;
		op->mode = AES_MODE_ECB;
		op->len = nbytes - (nbytes % AES_MIN_BLOCK_SIZE);
		op->dir = AES_DIR_DECRYPT;

		ret = geode_aes_crypt(op);
		nbytes -= ret;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static int
geode_ecb_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
	struct geode_aes_op *op = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	int err, ret;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);

	while((nbytes = walk.nbytes)) {
		op->src = walk.src.virt.addr,
		op->dst = walk.dst.virt.addr;
		op->mode = AES_MODE_ECB;
		op->len = nbytes - (nbytes % AES_MIN_BLOCK_SIZE);
		op->dir = AES_DIR_ENCRYPT;

		ret = geode_aes_crypt(op);
		nbytes -= ret;
		ret =  blkcipher_walk_done(desc, &walk, nbytes);
	}

	return err;
}

static struct crypto_alg geode_ecb_alg = {
	.cra_name		=	"ecb(aes)",
	.cra_driver_name	=	"ecb-aes-geode-128",
	.cra_priority		=	400,
	.cra_flags		=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_MIN_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct geode_aes_op),
	.cra_alignmask		=	15,
	.cra_type		=	&crypto_blkcipher_type,
	.cra_module		=	THIS_MODULE,
	.cra_list		=	LIST_HEAD_INIT(geode_ecb_alg.cra_list),
	.cra_u			=	{
		.blkcipher = {
			.min_keysize		=	AES_KEY_LENGTH,
			.max_keysize		=	AES_KEY_LENGTH,
			.setkey			=	geode_setkey,
			.encrypt		=	geode_ecb_encrypt,
			.decrypt		=	geode_ecb_decrypt,
		}
	}
};

static void
geode_aes_remove(struct pci_dev *dev)
{
	crypto_unregister_alg(&geode_alg);
	crypto_unregister_alg(&geode_ecb_alg);
	crypto_unregister_alg(&geode_cbc_alg);

	pci_iounmap(dev, _iobase);
	_iobase = NULL;

	pci_release_regions(dev);
	pci_disable_device(dev);
}


static int
geode_aes_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret;

	if ((ret = pci_enable_device(dev)))
		return ret;

	if ((ret = pci_request_regions(dev, "geode-aes-128")))
		goto eenable;

	_iobase = pci_iomap(dev, 0, 0);

	if (_iobase == NULL) {
		ret = -ENOMEM;
		goto erequest;
	}

	spin_lock_init(&lock);

	/* Clear any pending activity */
	iowrite32(AES_INTR_PENDING | AES_INTR_MASK, _iobase + AES_INTR_REG);

	if ((ret = crypto_register_alg(&geode_alg)))
		goto eiomap;

	if ((ret = crypto_register_alg(&geode_ecb_alg)))
		goto ealg;

	if ((ret = crypto_register_alg(&geode_cbc_alg)))
		goto eecb;

	printk(KERN_NOTICE "geode-aes: GEODE AES engine enabled.\n");
	return 0;

 eecb:
	crypto_unregister_alg(&geode_ecb_alg);

 ealg:
	crypto_unregister_alg(&geode_alg);

 eiomap:
	pci_iounmap(dev, _iobase);

 erequest:
	pci_release_regions(dev);

 eenable:
	pci_disable_device(dev);

	printk(KERN_ERR "geode-aes:  GEODE AES initialization failed.\n");
	return ret;
}

static struct pci_device_id geode_aes_tbl[] = {
	{ PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_LX_AES, PCI_ANY_ID, PCI_ANY_ID} ,
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, geode_aes_tbl);

static struct pci_driver geode_aes_driver = {
	.name = "Geode LX AES",
	.id_table = geode_aes_tbl,
	.probe = geode_aes_probe,
	.remove = __devexit_p(geode_aes_remove)
};

static int __init
geode_aes_init(void)
{
	return pci_module_init(&geode_aes_driver);
}

static void __exit
geode_aes_exit(void)
{
	pci_unregister_driver(&geode_aes_driver);
}

MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION("Geode LX Hardware AES driver");
MODULE_LICENSE("GPL");

module_init(geode_aes_init);
module_exit(geode_aes_exit);
