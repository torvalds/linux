/*
 * Microchip / Atmel ECC (I2C) driver.
 *
 * Copyright (c) 2017, Microchip Technology Inc.
 * Author: Tudor Ambarus <tudor.ambarus@microchip.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/bitrev.h>
#include <linux/crc16.h>
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
#include "atmel-ecc.h"

/* Used for binding tfm objects to i2c clients. */
struct atmel_ecc_driver_data {
	struct list_head i2c_client_list;
	spinlock_t i2c_list_lock;
} ____cacheline_aligned;

static struct atmel_ecc_driver_data driver_data;

/**
 * atmel_ecc_i2c_client_priv - i2c_client private data
 * @client              : pointer to i2c client device
 * @i2c_client_list_node: part of i2c_client_list
 * @lock                : lock for sending i2c commands
 * @wake_token          : wake token array of zeros
 * @wake_token_sz       : size in bytes of the wake_token
 * @tfm_count           : number of active crypto transformations on i2c client
 *
 * Reads and writes from/to the i2c client are sequential. The first byte
 * transmitted to the device is treated as the byte size. Any attempt to send
 * more than this number of bytes will cause the device to not ACK those bytes.
 * After the host writes a single command byte to the input buffer, reads are
 * prohibited until after the device completes command execution. Use a mutex
 * when sending i2c commands.
 */
struct atmel_ecc_i2c_client_priv {
	struct i2c_client *client;
	struct list_head i2c_client_list_node;
	struct mutex lock;
	u8 wake_token[WAKE_TOKEN_MAX_SIZE];
	size_t wake_token_sz;
	atomic_t tfm_count ____cacheline_aligned;
};

/**
 * atmel_ecdh_ctx - transformation context
 * @client     : pointer to i2c client device
 * @fallback   : used for unsupported curves or when user wants to use its own
 *               private key.
 * @public_key : generated when calling set_secret(). It's the responsibility
 *               of the user to not call set_secret() while
 *               generate_public_key() or compute_shared_secret() are in flight.
 * @curve_id   : elliptic curve id
 * @n_sz       : size in bytes of the n prime
 * @do_fallback: true when the device doesn't support the curve or when the user
 *               wants to use its own private key.
 */
struct atmel_ecdh_ctx {
	struct i2c_client *client;
	struct crypto_kpp *fallback;
	const u8 *public_key;
	unsigned int curve_id;
	size_t n_sz;
	bool do_fallback;
};

/**
 * atmel_ecc_work_data - data structure representing the work
 * @ctx : transformation context.
 * @cbk : pointer to a callback function to be invoked upon completion of this
 *        request. This has the form:
 *        callback(struct atmel_ecc_work_data *work_data, void *areq, u8 status)
 *        where:
 *        @work_data: data structure representing the work
 *        @areq     : optional pointer to an argument passed with the original
 *                    request.
 *        @status   : status returned from the i2c client device or i2c error.
 * @areq: optional pointer to a user argument for use at callback time.
 * @work: describes the task to be executed.
 * @cmd : structure used for communicating with the device.
 */
struct atmel_ecc_work_data {
	struct atmel_ecdh_ctx *ctx;
	void (*cbk)(struct atmel_ecc_work_data *work_data, void *areq,
		    int status);
	void *areq;
	struct work_struct work;
	struct atmel_ecc_cmd cmd;
};

static u16 atmel_ecc_crc16(u16 crc, const u8 *buffer, size_t len)
{
	return cpu_to_le16(bitrev16(crc16(crc, buffer, len)));
}

/**
 * atmel_ecc_checksum() - Generate 16-bit CRC as required by ATMEL ECC.
 * CRC16 verification of the count, opcode, param1, param2 and data bytes.
 * The checksum is saved in little-endian format in the least significant
 * two bytes of the command. CRC polynomial is 0x8005 and the initial register
 * value should be zero.
 *
 * @cmd : structure used for communicating with the device.
 */
static void atmel_ecc_checksum(struct atmel_ecc_cmd *cmd)
{
	u8 *data = &cmd->count;
	size_t len = cmd->count - CRC_SIZE;
	u16 *crc16 = (u16 *)(data + len);

	*crc16 = atmel_ecc_crc16(0, data, len);
}

static void atmel_ecc_init_read_cmd(struct atmel_ecc_cmd *cmd)
{
	cmd->word_addr = COMMAND;
	cmd->opcode = OPCODE_READ;
	/*
	 * Read the word from Configuration zone that contains the lock bytes
	 * (UserExtra, Selector, LockValue, LockConfig).
	 */
	cmd->param1 = CONFIG_ZONE;
	cmd->param2 = DEVICE_LOCK_ADDR;
	cmd->count = READ_COUNT;

	atmel_ecc_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_READ;
	cmd->rxsize = READ_RSP_SIZE;
}

static void atmel_ecc_init_genkey_cmd(struct atmel_ecc_cmd *cmd, u16 keyid)
{
	cmd->word_addr = COMMAND;
	cmd->count = GENKEY_COUNT;
	cmd->opcode = OPCODE_GENKEY;
	cmd->param1 = GENKEY_MODE_PRIVATE;
	/* a random private key will be generated and stored in slot keyID */
	cmd->param2 = cpu_to_le16(keyid);

	atmel_ecc_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_GENKEY;
	cmd->rxsize = GENKEY_RSP_SIZE;
}

static int atmel_ecc_init_ecdh_cmd(struct atmel_ecc_cmd *cmd,
				   struct scatterlist *pubkey)
{
	size_t copied;

	cmd->word_addr = COMMAND;
	cmd->count = ECDH_COUNT;
	cmd->opcode = OPCODE_ECDH;
	cmd->param1 = ECDH_PREFIX_MODE;
	/* private key slot */
	cmd->param2 = cpu_to_le16(DATA_SLOT_2);

	/*
	 * The device only supports NIST P256 ECC keys. The public key size will
	 * always be the same. Use a macro for the key size to avoid unnecessary
	 * computations.
	 */
	copied = sg_copy_to_buffer(pubkey, 1, cmd->data, ATMEL_ECC_PUBKEY_SIZE);
	if (copied != ATMEL_ECC_PUBKEY_SIZE)
		return -EINVAL;

	atmel_ecc_checksum(cmd);

	cmd->msecs = MAX_EXEC_TIME_ECDH;
	cmd->rxsize = ECDH_RSP_SIZE;

	return 0;
}

/*
 * After wake and after execution of a command, there will be error, status, or
 * result bytes in the device's output register that can be retrieved by the
 * system. When the length of that group is four bytes, the codes returned are
 * detailed in error_list.
 */
static int atmel_ecc_status(struct device *dev, u8 *status)
{
	size_t err_list_len = ARRAY_SIZE(error_list);
	int i;
	u8 err_id = status[1];

	if (*status != STATUS_SIZE)
		return 0;

	if (err_id == STATUS_WAKE_SUCCESSFUL || err_id == STATUS_NOERR)
		return 0;

	for (i = 0; i < err_list_len; i++)
		if (error_list[i].value == err_id)
			break;

	/* if err_id is not in the error_list then ignore it */
	if (i != err_list_len) {
		dev_err(dev, "%02x: %s:\n", err_id, error_list[i].error_text);
		return err_id;
	}

	return 0;
}

static int atmel_ecc_wakeup(struct i2c_client *client)
{
	struct atmel_ecc_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);
	u8 status[STATUS_RSP_SIZE];
	int ret;

	/*
	 * The device ignores any levels or transitions on the SCL pin when the
	 * device is idle, asleep or during waking up. Don't check for error
	 * when waking up the device.
	 */
	i2c_master_send(client, i2c_priv->wake_token, i2c_priv->wake_token_sz);

	/*
	 * Wait to wake the device. Typical execution times for ecdh and genkey
	 * are around tens of milliseconds. Delta is chosen to 50 microseconds.
	 */
	usleep_range(TWHI_MIN, TWHI_MAX);

	ret = i2c_master_recv(client, status, STATUS_SIZE);
	if (ret < 0)
		return ret;

	return atmel_ecc_status(&client->dev, status);
}

static int atmel_ecc_sleep(struct i2c_client *client)
{
	u8 sleep = SLEEP_TOKEN;

	return i2c_master_send(client, &sleep, 1);
}

static void atmel_ecdh_done(struct atmel_ecc_work_data *work_data, void *areq,
			    int status)
{
	struct kpp_request *req = areq;
	struct atmel_ecdh_ctx *ctx = work_data->ctx;
	struct atmel_ecc_cmd *cmd = &work_data->cmd;
	size_t copied;
	size_t n_sz = ctx->n_sz;

	if (status)
		goto free_work_data;

	/* copy the shared secret */
	copied = sg_copy_from_buffer(req->dst, 1, &cmd->data[RSP_DATA_IDX],
				     n_sz);
	if (copied != n_sz)
		status = -EINVAL;

	/* fall through */
free_work_data:
	kzfree(work_data);
	kpp_request_complete(req, status);
}

/*
 * atmel_ecc_send_receive() - send a command to the device and receive its
 *                            response.
 * @client: i2c client device
 * @cmd   : structure used to communicate with the device
 *
 * After the device receives a Wake token, a watchdog counter starts within the
 * device. After the watchdog timer expires, the device enters sleep mode
 * regardless of whether some I/O transmission or command execution is in
 * progress. If a command is attempted when insufficient time remains prior to
 * watchdog timer execution, the device will return the watchdog timeout error
 * code without attempting to execute the command. There is no way to reset the
 * counter other than to put the device into sleep or idle mode and then
 * wake it up again.
 */
static int atmel_ecc_send_receive(struct i2c_client *client,
				  struct atmel_ecc_cmd *cmd)
{
	struct atmel_ecc_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&i2c_priv->lock);

	ret = atmel_ecc_wakeup(client);
	if (ret)
		goto err;

	/* send the command */
	ret = i2c_master_send(client, (u8 *)cmd, cmd->count + WORD_ADDR_SIZE);
	if (ret < 0)
		goto err;

	/* delay the appropriate amount of time for command to execute */
	msleep(cmd->msecs);

	/* receive the response */
	ret = i2c_master_recv(client, cmd->data, cmd->rxsize);
	if (ret < 0)
		goto err;

	/* put the device into low-power mode */
	ret = atmel_ecc_sleep(client);
	if (ret < 0)
		goto err;

	mutex_unlock(&i2c_priv->lock);
	return atmel_ecc_status(&client->dev, cmd->data);
err:
	mutex_unlock(&i2c_priv->lock);
	return ret;
}

static void atmel_ecc_work_handler(struct work_struct *work)
{
	struct atmel_ecc_work_data *work_data =
			container_of(work, struct atmel_ecc_work_data, work);
	struct atmel_ecc_cmd *cmd = &work_data->cmd;
	struct i2c_client *client = work_data->ctx->client;
	int status;

	status = atmel_ecc_send_receive(client, cmd);
	work_data->cbk(work_data, work_data->areq, status);
}

static void atmel_ecc_enqueue(struct atmel_ecc_work_data *work_data,
			      void (*cbk)(struct atmel_ecc_work_data *work_data,
					  void *areq, int status),
			      void *areq)
{
	work_data->cbk = (void *)cbk;
	work_data->areq = areq;

	INIT_WORK(&work_data->work, atmel_ecc_work_handler);
	schedule_work(&work_data->work);
}

static unsigned int atmel_ecdh_supported_curve(unsigned int curve_id)
{
	if (curve_id == ECC_CURVE_NIST_P256)
		return ATMEL_ECC_NIST_P256_N_SIZE;

	return 0;
}

/*
 * A random private key is generated and stored in the device. The device
 * returns the pair public key.
 */
static int atmel_ecdh_set_secret(struct crypto_kpp *tfm, const void *buf,
				 unsigned int len)
{
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct atmel_ecc_cmd *cmd;
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

	ctx->n_sz = atmel_ecdh_supported_curve(params.curve_id);
	if (!ctx->n_sz || params.key_size) {
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
	ctx->curve_id = params.curve_id;

	atmel_ecc_init_genkey_cmd(cmd, DATA_SLOT_2);

	ret = atmel_ecc_send_receive(ctx->client, cmd);
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
	size_t copied;
	int ret = 0;

	if (ctx->do_fallback) {
		kpp_request_set_tfm(req, ctx->fallback);
		return crypto_kpp_generate_public_key(req);
	}

	/* public key was saved at private key generation */
	copied = sg_copy_from_buffer(req->dst, 1, ctx->public_key,
				     ATMEL_ECC_PUBKEY_SIZE);
	if (copied != ATMEL_ECC_PUBKEY_SIZE)
		ret = -EINVAL;

	return ret;
}

static int atmel_ecdh_compute_shared_secret(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);
	struct atmel_ecc_work_data *work_data;
	gfp_t gfp;
	int ret;

	if (ctx->do_fallback) {
		kpp_request_set_tfm(req, ctx->fallback);
		return crypto_kpp_compute_shared_secret(req);
	}

	gfp = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ? GFP_KERNEL :
							     GFP_ATOMIC;

	work_data = kmalloc(sizeof(*work_data), gfp);
	if (!work_data)
		return -ENOMEM;

	work_data->ctx = ctx;

	ret = atmel_ecc_init_ecdh_cmd(&work_data->cmd, req->src);
	if (ret)
		goto free_work_data;

	atmel_ecc_enqueue(work_data, atmel_ecdh_done, req);

	return -EINPROGRESS;

free_work_data:
	kfree(work_data);
	return ret;
}

static struct i2c_client *atmel_ecc_i2c_client_alloc(void)
{
	struct atmel_ecc_i2c_client_priv *i2c_priv, *min_i2c_priv = NULL;
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
	struct atmel_ecc_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);

	atomic_dec(&i2c_priv->tfm_count);
}

static int atmel_ecdh_init_tfm(struct crypto_kpp *tfm)
{
	const char *alg = kpp_alg_name(tfm);
	struct crypto_kpp *fallback;
	struct atmel_ecdh_ctx *ctx = kpp_tfm_ctx(tfm);

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

	dev_info(&ctx->client->dev, "Using '%s' as fallback implementation.\n",
		 crypto_tfm_alg_driver_name(crypto_kpp_tfm(fallback)));

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

static struct kpp_alg atmel_ecdh = {
	.set_secret = atmel_ecdh_set_secret,
	.generate_public_key = atmel_ecdh_generate_public_key,
	.compute_shared_secret = atmel_ecdh_compute_shared_secret,
	.init = atmel_ecdh_init_tfm,
	.exit = atmel_ecdh_exit_tfm,
	.max_size = atmel_ecdh_max_size,
	.base = {
		.cra_flags = CRYPTO_ALG_NEED_FALLBACK,
		.cra_name = "ecdh",
		.cra_driver_name = "atmel-ecdh",
		.cra_priority = ATMEL_ECC_PRIORITY,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct atmel_ecdh_ctx),
	},
};

static inline size_t atmel_ecc_wake_token_sz(u32 bus_clk_rate)
{
	u32 no_of_bits = DIV_ROUND_UP(TWLO_USEC * bus_clk_rate, USEC_PER_SEC);

	/* return the size of the wake_token in bytes */
	return DIV_ROUND_UP(no_of_bits, 8);
}

static int device_sanity_check(struct i2c_client *client)
{
	struct atmel_ecc_cmd *cmd;
	int ret;

	cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	atmel_ecc_init_read_cmd(cmd);

	ret = atmel_ecc_send_receive(client, cmd);
	if (ret)
		goto free_cmd;

	/*
	 * It is vital that the Configuration, Data and OTP zones be locked
	 * prior to release into the field of the system containing the device.
	 * Failure to lock these zones may permit modification of any secret
	 * keys and may lead to other security problems.
	 */
	if (cmd->data[LOCK_CONFIG_IDX] || cmd->data[LOCK_VALUE_IDX]) {
		dev_err(&client->dev, "Configuration or Data and OTP zones are unlocked!\n");
		ret = -ENOTSUPP;
	}

	/* fall through */
free_cmd:
	kfree(cmd);
	return ret;
}

static int atmel_ecc_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct atmel_ecc_i2c_client_priv *i2c_priv;
	struct device *dev = &client->dev;
	int ret;
	u32 bus_clk_rate;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "I2C_FUNC_I2C not supported\n");
		return -ENODEV;
	}

	ret = of_property_read_u32(client->adapter->dev.of_node,
				   "clock-frequency", &bus_clk_rate);
	if (ret) {
		dev_err(dev, "of: failed to read clock-frequency property\n");
		return ret;
	}

	if (bus_clk_rate > 1000000L) {
		dev_err(dev, "%d exceeds maximum supported clock frequency (1MHz)\n",
			bus_clk_rate);
		return -EINVAL;
	}

	i2c_priv = devm_kmalloc(dev, sizeof(*i2c_priv), GFP_KERNEL);
	if (!i2c_priv)
		return -ENOMEM;

	i2c_priv->client = client;
	mutex_init(&i2c_priv->lock);

	/*
	 * WAKE_TOKEN_MAX_SIZE was calculated for the maximum bus_clk_rate -
	 * 1MHz. The previous bus_clk_rate check ensures us that wake_token_sz
	 * will always be smaller than or equal to WAKE_TOKEN_MAX_SIZE.
	 */
	i2c_priv->wake_token_sz = atmel_ecc_wake_token_sz(bus_clk_rate);

	memset(i2c_priv->wake_token, 0, sizeof(i2c_priv->wake_token));

	atomic_set(&i2c_priv->tfm_count, 0);

	i2c_set_clientdata(client, i2c_priv);

	ret = device_sanity_check(client);
	if (ret)
		return ret;

	spin_lock(&driver_data.i2c_list_lock);
	list_add_tail(&i2c_priv->i2c_client_list_node,
		      &driver_data.i2c_client_list);
	spin_unlock(&driver_data.i2c_list_lock);

	ret = crypto_register_kpp(&atmel_ecdh);
	if (ret) {
		spin_lock(&driver_data.i2c_list_lock);
		list_del(&i2c_priv->i2c_client_list_node);
		spin_unlock(&driver_data.i2c_list_lock);

		dev_err(dev, "%s alg registration failed\n",
			atmel_ecdh.base.cra_driver_name);
	} else {
		dev_info(dev, "atmel ecc algorithms registered in /proc/crypto\n");
	}

	return ret;
}

static int atmel_ecc_remove(struct i2c_client *client)
{
	struct atmel_ecc_i2c_client_priv *i2c_priv = i2c_get_clientdata(client);

	/* Return EBUSY if i2c client already allocated. */
	if (atomic_read(&i2c_priv->tfm_count)) {
		dev_err(&client->dev, "Device is busy\n");
		return -EBUSY;
	}

	crypto_unregister_kpp(&atmel_ecdh);

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
