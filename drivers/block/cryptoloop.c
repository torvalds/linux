/*
   Linux loop encryption enabling module

   Copyright (C)  2002 Herbert Valerio Riedel <hvr@gnu.org>
   Copyright (C)  2003 Fruhwirth Clemens <clemens@endorphin.org>

   This module is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This module is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this module; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>

#include <linux/init.h>
#include <linux/string.h>
#include <linux/crypto.h>
#include <linux/blkdev.h>
#include <linux/loop.h>
#include <asm/semaphore.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("loop blockdevice transferfunction adaptor / CryptoAPI");
MODULE_AUTHOR("Herbert Valerio Riedel <hvr@gnu.org>");

#define LOOP_IV_SECTOR_BITS 9
#define LOOP_IV_SECTOR_SIZE (1 << LOOP_IV_SECTOR_BITS)

static int
cryptoloop_init(struct loop_device *lo, const struct loop_info64 *info)
{
	int err = -EINVAL;
	char cms[LO_NAME_SIZE];			/* cipher-mode string */
	char *cipher;
	char *mode;
	char *cmsp = cms;			/* c-m string pointer */
	struct crypto_tfm *tfm = NULL;

	/* encryption breaks for non sector aligned offsets */

	if (info->lo_offset % LOOP_IV_SECTOR_SIZE)
		goto out;

	strncpy(cms, info->lo_crypt_name, LO_NAME_SIZE);
	cms[LO_NAME_SIZE - 1] = 0;
	cipher = strsep(&cmsp, "-");
	mode = strsep(&cmsp, "-");

	if (mode == NULL || strcmp(mode, "cbc") == 0)
		tfm = crypto_alloc_tfm(cipher, CRYPTO_TFM_MODE_CBC);
	else if (strcmp(mode, "ecb") == 0)
		tfm = crypto_alloc_tfm(cipher, CRYPTO_TFM_MODE_ECB);
	if (tfm == NULL)
		return -EINVAL;

	err = tfm->crt_u.cipher.cit_setkey(tfm, info->lo_encrypt_key,
					   info->lo_encrypt_key_size);
	
	if (err != 0)
		goto out_free_tfm;

	lo->key_data = tfm;
	return 0;

 out_free_tfm:
	crypto_free_tfm(tfm);

 out:
	return err;
}


typedef int (*encdec_ecb_t)(struct crypto_tfm *tfm,
			struct scatterlist *sg_out,
			struct scatterlist *sg_in,
			unsigned int nsg);


static int
cryptoloop_transfer_ecb(struct loop_device *lo, int cmd,
			struct page *raw_page, unsigned raw_off,
			struct page *loop_page, unsigned loop_off,
			int size, sector_t IV)
{
	struct crypto_tfm *tfm = (struct crypto_tfm *) lo->key_data;
	struct scatterlist sg_out = { NULL, };
	struct scatterlist sg_in = { NULL, };

	encdec_ecb_t encdecfunc;
	struct page *in_page, *out_page;
	unsigned in_offs, out_offs;

	if (cmd == READ) {
		in_page = raw_page;
		in_offs = raw_off;
		out_page = loop_page;
		out_offs = loop_off;
		encdecfunc = tfm->crt_u.cipher.cit_decrypt;
	} else {
		in_page = loop_page;
		in_offs = loop_off;
		out_page = raw_page;
		out_offs = raw_off;
		encdecfunc = tfm->crt_u.cipher.cit_encrypt;
	}

	while (size > 0) {
		const int sz = min(size, LOOP_IV_SECTOR_SIZE);

		sg_in.page = in_page;
		sg_in.offset = in_offs;
		sg_in.length = sz;

		sg_out.page = out_page;
		sg_out.offset = out_offs;
		sg_out.length = sz;

		encdecfunc(tfm, &sg_out, &sg_in, sz);

		size -= sz;
		in_offs += sz;
		out_offs += sz;
	}

	return 0;
}

typedef int (*encdec_cbc_t)(struct crypto_tfm *tfm,
			struct scatterlist *sg_out,
			struct scatterlist *sg_in,
			unsigned int nsg, u8 *iv);

static int
cryptoloop_transfer_cbc(struct loop_device *lo, int cmd,
			struct page *raw_page, unsigned raw_off,
			struct page *loop_page, unsigned loop_off,
			int size, sector_t IV)
{
	struct crypto_tfm *tfm = (struct crypto_tfm *) lo->key_data;
	struct scatterlist sg_out = { NULL, };
	struct scatterlist sg_in = { NULL, };

	encdec_cbc_t encdecfunc;
	struct page *in_page, *out_page;
	unsigned in_offs, out_offs;

	if (cmd == READ) {
		in_page = raw_page;
		in_offs = raw_off;
		out_page = loop_page;
		out_offs = loop_off;
		encdecfunc = tfm->crt_u.cipher.cit_decrypt_iv;
	} else {
		in_page = loop_page;
		in_offs = loop_off;
		out_page = raw_page;
		out_offs = raw_off;
		encdecfunc = tfm->crt_u.cipher.cit_encrypt_iv;
	}

	while (size > 0) {
		const int sz = min(size, LOOP_IV_SECTOR_SIZE);
		u32 iv[4] = { 0, };
		iv[0] = cpu_to_le32(IV & 0xffffffff);

		sg_in.page = in_page;
		sg_in.offset = in_offs;
		sg_in.length = sz;

		sg_out.page = out_page;
		sg_out.offset = out_offs;
		sg_out.length = sz;

		encdecfunc(tfm, &sg_out, &sg_in, sz, (u8 *)iv);

		IV++;
		size -= sz;
		in_offs += sz;
		out_offs += sz;
	}

	return 0;
}

static int
cryptoloop_transfer(struct loop_device *lo, int cmd,
		    struct page *raw_page, unsigned raw_off,
		    struct page *loop_page, unsigned loop_off,
		    int size, sector_t IV)
{
	struct crypto_tfm *tfm = (struct crypto_tfm *) lo->key_data;
	if(tfm->crt_cipher.cit_mode == CRYPTO_TFM_MODE_ECB)
	{
		lo->transfer = cryptoloop_transfer_ecb;
		return cryptoloop_transfer_ecb(lo, cmd, raw_page, raw_off,
					       loop_page, loop_off, size, IV);
	}	
	if(tfm->crt_cipher.cit_mode == CRYPTO_TFM_MODE_CBC)
	{	
		lo->transfer = cryptoloop_transfer_cbc;
		return cryptoloop_transfer_cbc(lo, cmd, raw_page, raw_off,
					       loop_page, loop_off, size, IV);
	}
	
	/*  This is not supposed to happen */

	printk( KERN_ERR "cryptoloop: unsupported cipher mode in cryptoloop_transfer!\n");
	return -EINVAL;
}

static int
cryptoloop_ioctl(struct loop_device *lo, int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int
cryptoloop_release(struct loop_device *lo)
{
	struct crypto_tfm *tfm = (struct crypto_tfm *) lo->key_data;
	if (tfm != NULL) {
		crypto_free_tfm(tfm);
		lo->key_data = NULL;
		return 0;
	}
	printk(KERN_ERR "cryptoloop_release(): tfm == NULL?\n");
	return -EINVAL;
}

static struct loop_func_table cryptoloop_funcs = {
	.number = LO_CRYPT_CRYPTOAPI,
	.init = cryptoloop_init,
	.ioctl = cryptoloop_ioctl,
	.transfer = cryptoloop_transfer,
	.release = cryptoloop_release,
	.owner = THIS_MODULE
};

static int __init
init_cryptoloop(void)
{
	int rc = loop_register_transfer(&cryptoloop_funcs);

	if (rc)
		printk(KERN_ERR "cryptoloop: loop_register_transfer failed\n");
	return rc;
}

static void __exit
cleanup_cryptoloop(void)
{
	if (loop_unregister_transfer(LO_CRYPT_CRYPTOAPI))
		printk(KERN_ERR
			"cryptoloop: loop_unregister_transfer failed\n");
}

module_init(init_cryptoloop);
module_exit(cleanup_cryptoloop);
