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

#include <crypto/skcipher.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include "loop.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("loop blockdevice transferfunction adaptor / CryptoAPI");
MODULE_AUTHOR("Herbert Valerio Riedel <hvr@gnu.org>");

#define LOOP_IV_SECTOR_BITS 9
#define LOOP_IV_SECTOR_SIZE (1 << LOOP_IV_SECTOR_BITS)

static int
cryptoloop_init(struct loop_device *lo, const struct loop_info64 *info)
{
	int err = -EINVAL;
	int cipher_len;
	int mode_len;
	char cms[LO_NAME_SIZE];			/* cipher-mode string */
	char *cipher;
	char *mode;
	char *cmsp = cms;			/* c-m string pointer */
	struct crypto_skcipher *tfm;

	/* encryption breaks for non sector aligned offsets */

	if (info->lo_offset % LOOP_IV_SECTOR_SIZE)
		goto out;

	strncpy(cms, info->lo_crypt_name, LO_NAME_SIZE);
	cms[LO_NAME_SIZE - 1] = 0;

	cipher = cmsp;
	cipher_len = strcspn(cmsp, "-");

	mode = cmsp + cipher_len;
	mode_len = 0;
	if (*mode) {
		mode++;
		mode_len = strcspn(mode, "-");
	}

	if (!mode_len) {
		mode = "cbc";
		mode_len = 3;
	}

	if (cipher_len + mode_len + 3 > LO_NAME_SIZE)
		return -EINVAL;

	memmove(cms, mode, mode_len);
	cmsp = cms + mode_len;
	*cmsp++ = '(';
	memcpy(cmsp, info->lo_crypt_name, cipher_len);
	cmsp += cipher_len;
	*cmsp++ = ')';
	*cmsp = 0;

	tfm = crypto_alloc_skcipher(cms, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	err = crypto_skcipher_setkey(tfm, info->lo_encrypt_key,
				     info->lo_encrypt_key_size);
	
	if (err != 0)
		goto out_free_tfm;

	lo->key_data = tfm;
	return 0;

 out_free_tfm:
	crypto_free_skcipher(tfm);

 out:
	return err;
}


typedef int (*encdec_cbc_t)(struct skcipher_request *req);

static int
cryptoloop_transfer(struct loop_device *lo, int cmd,
		    struct page *raw_page, unsigned raw_off,
		    struct page *loop_page, unsigned loop_off,
		    int size, sector_t IV)
{
	struct crypto_skcipher *tfm = lo->key_data;
	SKCIPHER_REQUEST_ON_STACK(req, tfm);
	struct scatterlist sg_out;
	struct scatterlist sg_in;

	encdec_cbc_t encdecfunc;
	struct page *in_page, *out_page;
	unsigned in_offs, out_offs;
	int err;

	skcipher_request_set_tfm(req, tfm);
	skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP,
				      NULL, NULL);

	sg_init_table(&sg_out, 1);
	sg_init_table(&sg_in, 1);

	if (cmd == READ) {
		in_page = raw_page;
		in_offs = raw_off;
		out_page = loop_page;
		out_offs = loop_off;
		encdecfunc = crypto_skcipher_decrypt;
	} else {
		in_page = loop_page;
		in_offs = loop_off;
		out_page = raw_page;
		out_offs = raw_off;
		encdecfunc = crypto_skcipher_encrypt;
	}

	while (size > 0) {
		const int sz = min(size, LOOP_IV_SECTOR_SIZE);
		u32 iv[4] = { 0, };
		iv[0] = cpu_to_le32(IV & 0xffffffff);

		sg_set_page(&sg_in, in_page, sz, in_offs);
		sg_set_page(&sg_out, out_page, sz, out_offs);

		skcipher_request_set_crypt(req, &sg_in, &sg_out, sz, iv);
		err = encdecfunc(req);
		if (err)
			goto out;

		IV++;
		size -= sz;
		in_offs += sz;
		out_offs += sz;
	}

	err = 0;

out:
	skcipher_request_zero(req);
	return err;
}

static int
cryptoloop_ioctl(struct loop_device *lo, int cmd, unsigned long arg)
{
	return -EINVAL;
}

static int
cryptoloop_release(struct loop_device *lo)
{
	struct crypto_skcipher *tfm = lo->key_data;
	if (tfm != NULL) {
		crypto_free_skcipher(tfm);
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
