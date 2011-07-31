/*
 * drivers/misc/tegra-cryptodev.c
 *
 * crypto dev node for NVIDIA tegra aes hardware
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <crypto/rng.h>

#include "tegra-cryptodev.h"

#define NBUFS 2

struct tegra_crypto_ctx {
	struct crypto_ablkcipher *ecb_tfm;
	struct crypto_ablkcipher *cbc_tfm;
	struct crypto_rng *rng;
	int use_ssk;
};

struct tegra_crypto_completion {
	struct completion restart;
	int req_err;
};

static int alloc_bufs(unsigned long *buf[NBUFS])
{
	int i;

	for (i = 0; i < NBUFS; i++) {
		buf[i] = (void *)__get_free_page(GFP_KERNEL);
		if (!buf[i])
			goto err_free_buf;
	}

	return 0;

err_free_buf:
	while (i-- > 0)
		free_page((unsigned long)buf[i]);

	return -ENOMEM;
}

static void free_bufs(unsigned long *buf[NBUFS])
{
	int i;

	for (i = 0; i < NBUFS; i++)
		free_page((unsigned long)buf[i]);
}

static int tegra_crypto_dev_open(struct inode *inode, struct file *filp)
{
	struct tegra_crypto_ctx *ctx;
	int ret = 0;

	ctx = kzalloc(sizeof(struct tegra_crypto_ctx), GFP_KERNEL);
	if (!ctx) {
		pr_err("no memory for context\n");
		return -ENOMEM;
	}

	ctx->ecb_tfm = crypto_alloc_ablkcipher("ecb-aes-tegra",
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(ctx->ecb_tfm)) {
		pr_err("Failed to load transform for ecb-aes-tegra: %ld\n",
			PTR_ERR(ctx->ecb_tfm));
		ret = PTR_ERR(ctx->ecb_tfm);
		goto fail_ecb;
	}

	ctx->cbc_tfm = crypto_alloc_ablkcipher("cbc-aes-tegra",
		CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC, 0);
	if (IS_ERR(ctx->cbc_tfm)) {
		pr_err("Failed to load transform for cbc-aes-tegra: %ld\n",
			PTR_ERR(ctx->cbc_tfm));
		ret = PTR_ERR(ctx->cbc_tfm);
		goto fail_cbc;
	}

	ctx->rng = crypto_alloc_rng("rng-aes-tegra", CRYPTO_ALG_TYPE_RNG, 0);
	if (IS_ERR(ctx->rng)) {
		pr_err("Failed to load transform for tegra rng: %ld\n",
			PTR_ERR(ctx->rng));
		ret = PTR_ERR(ctx->rng);
		goto fail_rng;
	}

	filp->private_data = ctx;
	return ret;

fail_rng:
	crypto_free_ablkcipher(ctx->cbc_tfm);

fail_cbc:
	crypto_free_ablkcipher(ctx->ecb_tfm);

fail_ecb:
	kfree(ctx);
	return ret;
}

static int tegra_crypto_dev_release(struct inode *inode, struct file *filp)
{
	struct tegra_crypto_ctx *ctx = filp->private_data;

	crypto_free_ablkcipher(ctx->ecb_tfm);
	crypto_free_ablkcipher(ctx->cbc_tfm);
	crypto_free_rng(ctx->rng);
	kfree(ctx);
	filp->private_data = NULL;
	return 0;
}

static void tegra_crypt_complete(struct crypto_async_request *req, int err)
{
	struct tegra_crypto_completion *done = req->data;

	if (err != -EINPROGRESS) {
		done->req_err = err;
		complete(&done->restart);
	}
}

static int process_crypt_req(struct tegra_crypto_ctx *ctx, struct tegra_crypt_req *crypt_req)
{
	struct crypto_ablkcipher *tfm;
	struct ablkcipher_request *req = NULL;
	struct scatterlist in_sg;
	struct scatterlist out_sg;
	unsigned long *xbuf[NBUFS];
	int ret = 0, size = 0;
	unsigned long total = 0;
	struct tegra_crypto_completion tcrypt_complete;

	if (crypt_req->op & TEGRA_CRYPTO_ECB) {
		req = ablkcipher_request_alloc(ctx->ecb_tfm, GFP_KERNEL);
		tfm = ctx->ecb_tfm;
	} else {
		req = ablkcipher_request_alloc(ctx->cbc_tfm, GFP_KERNEL);
		tfm = ctx->cbc_tfm;
	}
	if (!req) {
		pr_err("%s: Failed to allocate request\n", __func__);
		return -ENOMEM;
	}

	if ((crypt_req->keylen < 0) || (crypt_req->keylen > AES_MAX_KEY_SIZE))
		return -EINVAL;

	crypto_ablkcipher_clear_flags(tfm, ~0);

	if (!ctx->use_ssk) {
		ret = crypto_ablkcipher_setkey(tfm, crypt_req->key,
			crypt_req->keylen);
		if (ret < 0) {
			pr_err("setkey failed");
			goto process_req_out;
		}
	}

	ret = alloc_bufs(xbuf);
	if (ret < 0) {
		pr_err("alloc_bufs failed");
		goto process_req_out;
	}

	init_completion(&tcrypt_complete.restart);

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
		tegra_crypt_complete, &tcrypt_complete);

	total = crypt_req->plaintext_sz;
	while (total > 0) {
		size = min(total, PAGE_SIZE);
		ret = copy_from_user(xbuf[0],
			(void __user *)crypt_req->plaintext, size);
		if (ret < 0) {
			pr_debug("%s: copy_from_user failed (%d)\n", __func__, ret);
			goto process_req_buf_out;
		}
		sg_init_one(&in_sg, xbuf[0], size);
		sg_init_one(&out_sg, xbuf[1], size);

		ablkcipher_request_set_crypt(req, &in_sg,
			&out_sg, size, crypt_req->iv);

		INIT_COMPLETION(tcrypt_complete.restart);
		tcrypt_complete.req_err = 0;
		ret = crypt_req->encrypt ?
			crypto_ablkcipher_encrypt(req) :
			crypto_ablkcipher_decrypt(req);

		if ((ret == -EINPROGRESS) || (ret == -EBUSY)) {
			/* crypto driver is asynchronous */
			ret = wait_for_completion_interruptible(&tcrypt_complete.restart);

			if (ret < 0)
				goto process_req_buf_out;

			if (tcrypt_complete.req_err < 0) {
				ret = tcrypt_complete.req_err;
				goto process_req_buf_out;
			}
		} else if (ret < 0) {
			pr_debug("%scrypt failed (%d)\n",
				crypt_req->encrypt ? "en" : "de", ret);
			goto process_req_buf_out;
		}

		ret = copy_to_user((void __user *)crypt_req->result, xbuf[1],
			size);
		if (ret < 0)
			goto process_req_buf_out;

		total -= size;
		crypt_req->result += size;
		crypt_req->plaintext += size;
	}

process_req_buf_out:
	free_bufs(xbuf);
process_req_out:
	ablkcipher_request_free(req);

	return ret;
}

static long tegra_crypto_dev_ioctl(struct file *filp,
	unsigned int ioctl_num, unsigned long arg)
{
	struct tegra_crypto_ctx *ctx = filp->private_data;
	struct tegra_crypt_req crypt_req;
	struct tegra_rng_req rng_req;
	char *rng;
	int ret = 0;

	switch (ioctl_num) {
	case TEGRA_CRYPTO_IOCTL_NEED_SSK:
		ctx->use_ssk = (int)arg;
		break;
	case TEGRA_CRYPTO_IOCTL_PROCESS_REQ:
		ret = copy_from_user(&crypt_req, (void __user *)arg, sizeof(crypt_req));
		if (ret < 0) {
			pr_debug("%s: copy_from_user fail(%d)\n", __func__, ret);
			break;
		}

		ret = process_crypt_req(ctx, &crypt_req);
		break;

	case TEGRA_CRYPTO_IOCTL_SET_SEED:
		if (copy_from_user(&rng_req, (void __user *)arg, sizeof(rng_req)))
			return -EFAULT;

		ret = crypto_rng_reset(ctx->rng, rng_req.seed,
			crypto_rng_seedsize(ctx->rng));
		break;
	case TEGRA_CRYPTO_IOCTL_GET_RANDOM:
		if (copy_from_user(&rng_req, (void __user *)arg, sizeof(rng_req)))
			return -EFAULT;

		rng = kzalloc(rng_req.nbytes, GFP_KERNEL);
		if (!rng) {
			pr_err("mem alloc for rng fail");
			ret = -ENODATA;
			goto rng_out;
		}

		ret = crypto_rng_get_bytes(ctx->rng, rng, rng_req.nbytes);

		if (ret != rng_req.nbytes) {
			pr_debug("rng failed");
			ret = -ENODATA;
			goto rng_out;
		}

		ret = copy_to_user((void __user *)rng_req.rdata,
			rng, rng_req.nbytes);
		ret = (ret < 0) ? -ENODATA : 0;
rng_out:
		if (rng)
			kfree(rng);

		break;


	default:
		pr_debug("invalid ioctl code(%d)", ioctl_num);
		ret = -EINVAL;
	}
	return ret;
}

struct file_operations tegra_crypto_fops = {
	.owner = THIS_MODULE,
	.open = tegra_crypto_dev_open,
	.release = tegra_crypto_dev_release,
	.unlocked_ioctl = tegra_crypto_dev_ioctl,
};

struct miscdevice tegra_crypto_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tegra-crypto",
	.fops = &tegra_crypto_fops,
};

static int __init tegra_crypto_dev_init(void)
{
	return misc_register(&tegra_crypto_device);
}

late_initcall(tegra_crypto_dev_init);

MODULE_DESCRIPTION("Tegra AES hw device node.");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPLv2");
