// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip / Atmel ECC (I2C) driver.
 *
 * Copyright (c) 2017, Microchip Technology Inc.
 * Author: Tudor Ambarus <tudor.ambarus@microchip.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <crypto/internal/kpp.h>
#include <crypto/ecdh.h>
#include <crypto/kpp.h>
#include "atmel-i2c.h"

static struct atmel_ecc_driver_data driver_data;

/**
 * struct atmel_ecdh_ctx - transformation context
 * @client     : pointer to i2c client device
 * @fallback   : used for unsupported curves or when user wants to use its own
 *               private key.
 * @public_key : generated when calling set_secret(). It's the responsibility
 *               of the user to not call set_secret() while
 *               generate_public_key() or compute_shared_secret() are in flight.
 * @curve_id   : elliptic curve id
 * @do_fallback: true when the device doesn't support the curve or when the user
 *               wants to use its own private key.
 */
struct atmel_ecdh_ctx {
	struct i2c_client *client;
	struct crypto_kpp *fallback;
	const u8 *public_key;
	unsigned int curve_id;
	bool do_fallback;
};

static void atmel_ecdh_done(struct atmel_i2c_work_data *work_data, void *areq,
			    int status)
{
	struct kpp_request *req = areq;
	struct atmel_i2c_cmd *cmd = &work_data->cmd;
	size_t copied, n_sz;

	if (status)
		goto free_work_data;

	/* might want less than we've got */
	n_sz = min_t(size_t, ATMEL_ECC_NIST_P256_N_SIZE, req->dst_len);

	/* copy the shared secret */
	copied = sg_copy_from_buffer(req->dst, sg_nents_for_len(req->dst, n_sz),
				     &cmd->data[RSP_DATA_IDX], n_sz);
	if (copied != n_sz)
		status = -EINVAL;

	/* fall through */
free_work_data:
	kfree_sensitive(work_data);
	kpp_request_complete(req, status);
}

/*
 * A random private key is generated and stored in the device. The device
 * returns the pair public key.
 */
static int atmel_ecdh_set_secret(struct crypto_kpp *tfm, const void *buf,
				 unsigned int len)
{
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct atmel_i2c_cmd *cmd;
	void *public_key;
	struct ecdh params;
	int ret = -ENOMEM;

	/* free the old public key, if any */
	kfree(ctx->public_key);
	/* make sure you don't free the old public key twice */
	ctx->public_key = NULL;

	if (crypto_ecdh_decode_key(buf, len, &params) < 0) {
		dev_err(&ctx->client->dev, "crypto_ecdh_decode_key failed\n");
		return -EINVAL;
	}

	if (params.key_size) {
		/* fallback to ecdh software implementation */
		ctx->do_fallback = true;
		return crypto_kpp_set_secret(ctx->fallback, buf, len);
	}

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	/*
	 * The device only supports NIST P256 ECC keys. The public key size will
	 * always be the same. Use a macro for the key size to avoid unnecessary
	 * computations.
	 */
	public_key = kmalloc(ATMEL_ECC_PUBKEY_SIZE, GFP_KERNEL);
	if (!public_key)
		goto free_cmd;

	ctx->do_fallback = false;

	atmel_i2c_init_genkey_cmd(cmd, DATA_SLOT_2);

	ret = atmel_i2c_send_receive(ctx->client, cmd);
	if (ret)
		goto free_public_key;

	/* save the public key */
	memcpy(public_key, &cmd->data[RSP_DATA_IDX], ATMEL_ECC_PUBKEY_SIZE);
	ctx->public_key = public_key;

	kfree(cmd);
	return 0;

free_public_key:
	kfree(public_key);
free_cmd:
	kfree(cmd);
	return ret;
}

static int atmel_ecdh_generate_public_key(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	size_t copied, nbytes;
	int ret = 0;

	if (ctx->do_fallback) {
		kpp_request_set_tfm(req, ctx->fallback);
		return crypto_kpp_generate_public_key(req);
	}

	if (!ctx->public_key)
		return -EINVAL;

	/* might want less than we've got */
	nbytes = min_t(size_t, ATMEL_ECC_PUBKEY_SIZE, req->dst_len);

	/* public key was saved at private key generation */
	copied = sg_copy_from_buffer(req->dst,
				     sg_nents_for_len(req->dst, nbytes),
				     ctx->public_key, nbytes);
	if (copied != nbytes)
		ret = -EINVAL;

	return ret;
}

static int atmel_ecdh_compute_shared_secret(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct atmel_i2c_work_data *work_data;
	gfp_t gfp;
	int ret;

	if (ctx->do_fallback) {
		kpp_request_set_tfm(req, ctx->fallback);
		return crypto_kpp_compute_shared_secret(req);
	}

	/* must have exactly two points to be on the curve */
	if (req->src_len != ATMEL_ECC_PUBKEY_SIZE)
		return -EINVAL;

	gfp = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ? GFP_KERNEL :
							     GFP_ATOMIC;

	work_data = kmalloc(sizeof(*work_data), gfp);
	if (!work_data)
		return -ENOMEM;

	work_data->ctx = ctx;
	work_data->client = ctx->client;

	ret = atmel_i2c_init_ecdh_cmd(&work_data->cmd, req->src);
	if (ret)
		goto free_work_data;

	atmel_i2c_enqueue(work_data, atmel_ecdh_done, req);

	return -EINPROGRESS;

free_work_data:
	kfree(work_data);
	return ret;
}

static struct i2c_client *atmel_ecc_i2c_client_alloc(void)
{
	struct atmel_i2c_client_priv *i2c_priv, *min_i2c_priv = NULL;
	struct i2c_client *client = ERR_PTR(-ENODEV);
	int min_tfm_cnt = INT_MAX;
	int tfm_cnt;

	spin_lock(&driver_data.i2c_list_lock);

	if (list_empty(&driver_data.i2c_client_list)) {
		spin_unlock(&driver_data.i2c_list_lock);
		return ERR_PTR(-ENODEV);
	}

	list_for_each_entry(i2c_priv, &driver_data.i2c_client_list,
			    i2c_client_list_node) {
		tfm_cnt = atomic_read(&i2c_priv->tfm_count);
		if (tfm_cnt < min_tfm_cnt) {
			min_tfm_cnt = tfm_cnt;
			min_i2c_priv = i2c_priv;
		}
		if (!min_tfm_cnt)
			break;
	}

	if (min_i2c_priv) {
		atomic_inc(&min_i2c_priv->tfm_count);
		client = min_i2c_priv->client;
	}

	spin_unlock(&driver_data.i2c_list_lock);

	return client;
}

static void atmel_ecc_i2c_client_free(struct i2c_client *client)
{
	struct atmel_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);

	atomic_dec(&i2c_priv->tfm_count);
}

static int atmel_ecdh_init_tfm(struct crypto_kpp *tfm)
{
	const char *alg = kpp_alg_name(tfm);
	struct crypto_kpp *fallback;
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);

	ctx->curve_id = ECC_CURVE_NIST_P256;
	ctx->client = atmel_ecc_i2c_client_alloc();
	if (IS_ERR(ctx->client)) {
		pr_err("tfm - i2c_client binding failed\n");
		return PTR_ERR(ctx->client);
	}

	fallback = crypto_alloc_kpp(alg, 0, CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback)) {
		dev_err(&ctx->client->dev, "Failed to allocate transformation for '%s': %ld\n",
			alg, PTR_ERR(fallback));
		return PTR_ERR(fallback);
	}

	crypto_kpp_set_flags(fallback, crypto_kpp_get_flags(tfm));
	ctx->fallback = fallback;

	return 0;
}

static void atmel_ecdh_exit_tfm(struct crypto_kpp *tfm)
{
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);

	kfree(ctx->public_key);
	crypto_free_kpp(ctx->fallback);
	atmel_ecc_i2c_client_free(ctx->client);
}

static unsigned int atmel_ecdh_max_size(struct crypto_kpp *tfm)
{
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);

	if (ctx->fallback)
		return crypto_kpp_maxsize(ctx->fallback);

	/*
	 * The device only supports NIST P256 ECC keys. The public key size will
	 * always be the same. Use a macro for the key size to avoid unnecessary
	 * computations.
	 */
	return ATMEL_ECC_PUBKEY_SIZE;
}

static struct kpp_alg atmel_ecdh_nist_p256 = {
	.set_secret = atmel_ecdh_set_secret,
	.generate_public_key = atmel_ecdh_generate_public_key,
	.compute_shared_secret = atmel_ecdh_compute_shared_secret,
	.init = atmel_ecdh_init_tfm,
	.exit = atmel_ecdh_exit_tfm,
	.max_size = atmel_ecdh_max_size,
	.base = {
		.cra_flags = CRYPTO_ALG_NEED_FALLBACK,
		.cra_name = "ecdh-nist-p256",
		.cra_driver_name = "atmel-ecdh",
		.cra_priority = ATMEL_ECC_PRIORITY,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct atmel_ecdh_ctx),
	},
};

static int atmel_ecc_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct atmel_i2c_client_priv *i2c_priv;
	int ret;

	ret = atmel_i2c_probe(client, id);
	if (ret)
		return ret;

	i2c_priv = i2c_get_clientdata(client);

	spin_lock(&driver_data.i2c_list_lock);
	list_add_tail(&i2c_priv->i2c_client_list_node,
		      &driver_data.i2c_client_list);
	spin_unlock(&driver_data.i2c_list_lock);

	ret = crypto_register_kpp(&atmel_ecdh_nist_p256);
	if (ret) {
		spin_lock(&driver_data.i2c_list_lock);
		list_del(&i2c_priv->i2c_client_list_node);
		spin_unlock(&driver_data.i2c_list_lock);

		dev_err(&client->dev, "%s alg registration failed\n",
			atmel_ecdh_nist_p256.base.cra_driver_name);
	} else {
		dev_info(&client->dev, "atmel ecc algorithms registered in /proc/crypto\n");
	}

	return ret;
}

static int atmel_ecc_remove(struct i2c_client *client)
{
	struct atmel_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);

	/* Return EBUSY if i2c client already allocated. */
	if (atomic_read(&i2c_priv->tfm_count)) {
		dev_err(&client->dev, "Device is busy\n");
		return -EBUSY;
	}

	crypto_unregister_kpp(&atmel_ecdh_nist_p256);

	spin_lock(&driver_data.i2c_list_lock);
	list_del(&i2c_priv->i2c_client_list_node);
	spin_unlock(&driver_data.i2c_list_lock);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id atmel_ecc_dt_ids[] = {
	{
		.compatible = "atmel,atecc508a",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, atmel_ecc_dt_ids);
#endif

static const struct i2c_device_id atmel_ecc_id[] = {
	{ "atecc508a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, atmel_ecc_id);

static struct i2c_driver atmel_ecc_driver = {
	.driver = {
		.name	= "atmel-ecc",
		.of_match_table = of_match_ptr(atmel_ecc_dt_ids),
	},
	.probe		= atmel_ecc_probe,
	.remove		= atmel_ecc_remove,
	.id_table	= atmel_ecc_id,
};

static int __init atmel_ecc_init(void)
{
	spin_lock_init(&driver_data.i2c_list_lock);
	INIT_LIST_HEAD(&driver_data.i2c_client_list);
	return i2c_add_driver(&atmel_ecc_driver);
}

static void __exit atmel_ecc_exit(void)
{
	flush_scheduled_work();
	i2c_del_driver(&atmel_ecc_driver);
}

module_init(atmel_ecc_init);
module_exit(atmel_ecc_exit);

MODULE_AUTHOR("Tudor Ambarus <tudor.ambarus@microchip.com>");
MODULE_DESCRIPTION("Microchip / Atmel ECC (I2C) driver");
MODULE_LICENSE("GPL v2");
