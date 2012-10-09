/*
 * Cryptographic API.
 * Support for Nomadik hardware crypto engine.

 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 * Author: Andreas Westin <andreas.westin@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/klist.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crypto.h>

#include <linux/regulator/consumer.h>
#include <linux/dmaengine.h>
#include <linux/bitops.h>

#include <crypto/internal/hash.h>
#include <crypto/sha.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>

#include <mach/crypto-ux500.h>
#include <mach/hardware.h>

#include "hash_alg.h"

#define DEV_DBG_NAME "hashX hashX:"

static int hash_mode;
module_param(hash_mode, int, 0);
MODULE_PARM_DESC(hash_mode, "CPU or DMA mode. CPU = 0 (default), DMA = 1");

/**
 * Pre-calculated empty message digests.
 */
static u8 zero_message_hash_sha1[SHA1_DIGEST_SIZE] = {
	0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d,
	0x32, 0x55, 0xbf, 0xef, 0x95, 0x60, 0x18, 0x90,
	0xaf, 0xd8, 0x07, 0x09
};

static u8 zero_message_hash_sha256[SHA256_DIGEST_SIZE] = {
	0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
	0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
	0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
	0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

/* HMAC-SHA1, no key */
static u8 zero_message_hmac_sha1[SHA1_DIGEST_SIZE] = {
	0xfb, 0xdb, 0x1d, 0x1b, 0x18, 0xaa, 0x6c, 0x08,
	0x32, 0x4b, 0x7d, 0x64, 0xb7, 0x1f, 0xb7, 0x63,
	0x70, 0x69, 0x0e, 0x1d
};

/* HMAC-SHA256, no key */
static u8 zero_message_hmac_sha256[SHA256_DIGEST_SIZE] = {
	0xb6, 0x13, 0x67, 0x9a, 0x08, 0x14, 0xd9, 0xec,
	0x77, 0x2f, 0x95, 0xd7, 0x78, 0xc3, 0x5f, 0xc5,
	0xff, 0x16, 0x97, 0xc4, 0x93, 0x71, 0x56, 0x53,
	0xc6, 0xc7, 0x12, 0x14, 0x42, 0x92, 0xc5, 0xad
};

/**
 * struct hash_driver_data - data specific to the driver.
 *
 * @device_list:	A list of registered devices to choose from.
 * @device_allocation:	A semaphore initialized with number of devices.
 */
struct hash_driver_data {
	struct klist		device_list;
	struct semaphore	device_allocation;
};

static struct hash_driver_data	driver_data;

/* Declaration of functions */
/**
 * hash_messagepad - Pads a message and write the nblw bits.
 * @device_data:	Structure for the hash device.
 * @message:		Last word of a message
 * @index_bytes:	The number of bytes in the last message
 *
 * This function manages the final part of the digest calculation, when less
 * than 512 bits (64 bytes) remain in message. This means index_bytes < 64.
 *
 */
static void hash_messagepad(struct hash_device_data *device_data,
		const u32 *message, u8 index_bytes);

/**
 * release_hash_device - Releases a previously allocated hash device.
 * @device_data:	Structure for the hash device.
 *
 */
static void release_hash_device(struct hash_device_data *device_data)
{
	spin_lock(&device_data->ctx_lock);
	device_data->current_ctx->device = NULL;
	device_data->current_ctx = NULL;
	spin_unlock(&device_data->ctx_lock);

	/*
	 * The down_interruptible part for this semaphore is called in
	 * cryp_get_device_data.
	 */
	up(&driver_data.device_allocation);
}

static void hash_dma_setup_channel(struct hash_device_data *device_data,
				struct device *dev)
{
	struct hash_platform_data *platform_data = dev->platform_data;
	dma_cap_zero(device_data->dma.mask);
	dma_cap_set(DMA_SLAVE, device_data->dma.mask);

	device_data->dma.cfg_mem2hash = platform_data->mem_to_engine;
	device_data->dma.chan_mem2hash =
		dma_request_channel(device_data->dma.mask,
				platform_data->dma_filter,
				device_data->dma.cfg_mem2hash);

	init_completion(&device_data->dma.complete);
}

static void hash_dma_callback(void *data)
{
	struct hash_ctx *ctx = (struct hash_ctx *) data;

	complete(&ctx->device->dma.complete);
}

static int hash_set_dma_transfer(struct hash_ctx *ctx, struct scatterlist *sg,
		int len, enum dma_data_direction direction)
{
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *channel = NULL;
	dma_cookie_t cookie;

	if (direction != DMA_TO_DEVICE) {
		dev_err(ctx->device->dev, "[%s] Invalid DMA direction",
				__func__);
		return -EFAULT;
	}

	sg->length = ALIGN(sg->length, HASH_DMA_ALIGN_SIZE);

	channel = ctx->device->dma.chan_mem2hash;
	ctx->device->dma.sg = sg;
	ctx->device->dma.sg_len = dma_map_sg(channel->device->dev,
			ctx->device->dma.sg, ctx->device->dma.nents,
			direction);

	if (!ctx->device->dma.sg_len) {
		dev_err(ctx->device->dev,
				"[%s]: Could not map the sg list (TO_DEVICE)",
				__func__);
		return -EFAULT;
	}

	dev_dbg(ctx->device->dev, "[%s]: Setting up DMA for buffer "
			"(TO_DEVICE)", __func__);
	desc = channel->device->device_prep_slave_sg(channel,
			ctx->device->dma.sg, ctx->device->dma.sg_len,
			direction, DMA_CTRL_ACK | DMA_PREP_INTERRUPT, NULL);
	if (!desc) {
		dev_err(ctx->device->dev,
			"[%s]: device_prep_slave_sg() failed!", __func__);
		return -EFAULT;
	}

	desc->callback = hash_dma_callback;
	desc->callback_param = ctx;

	cookie = desc->tx_submit(desc);
	dma_async_issue_pending(channel);

	return 0;
}

static void hash_dma_done(struct hash_ctx *ctx)
{
	struct dma_chan *chan;

	chan = ctx->device->dma.chan_mem2hash;
	chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
	dma_unmap_sg(chan->device->dev, ctx->device->dma.sg,
			ctx->device->dma.sg_len, DMA_TO_DEVICE);

}

static int hash_dma_write(struct hash_ctx *ctx,
		struct scatterlist *sg, int len)
{
	int error = hash_set_dma_transfer(ctx, sg, len, DMA_TO_DEVICE);
	if (error) {
		dev_dbg(ctx->device->dev, "[%s]: hash_set_dma_transfer() "
			"failed", __func__);
		return error;
	}

	return len;
}

/**
 * get_empty_message_digest - Returns a pre-calculated digest for
 * the empty message.
 * @device_data:	Structure for the hash device.
 * @zero_hash:		Buffer to return the empty message digest.
 * @zero_hash_size:	Hash size of the empty message digest.
 * @zero_digest:	True if zero_digest returned.
 */
static int get_empty_message_digest(
		struct hash_device_data *device_data,
		u8 *zero_hash, u32 *zero_hash_size, bool *zero_digest)
{
	int ret = 0;
	struct hash_ctx *ctx = device_data->current_ctx;
	*zero_digest = false;

	/**
	 * Caller responsible for ctx != NULL.
	 */

	if (HASH_OPER_MODE_HASH == ctx->config.oper_mode) {
		if (HASH_ALGO_SHA1 == ctx->config.algorithm) {
			memcpy(zero_hash, &zero_message_hash_sha1[0],
					SHA1_DIGEST_SIZE);
			*zero_hash_size = SHA1_DIGEST_SIZE;
			*zero_digest = true;
		} else if (HASH_ALGO_SHA256 ==
				ctx->config.algorithm) {
			memcpy(zero_hash, &zero_message_hash_sha256[0],
					SHA256_DIGEST_SIZE);
			*zero_hash_size = SHA256_DIGEST_SIZE;
			*zero_digest = true;
		} else {
			dev_err(device_data->dev, "[%s] "
					"Incorrect algorithm!"
					, __func__);
			ret = -EINVAL;
			goto out;
		}
	} else if (HASH_OPER_MODE_HMAC == ctx->config.oper_mode) {
		if (!ctx->keylen) {
			if (HASH_ALGO_SHA1 == ctx->config.algorithm) {
				memcpy(zero_hash, &zero_message_hmac_sha1[0],
						SHA1_DIGEST_SIZE);
				*zero_hash_size = SHA1_DIGEST_SIZE;
				*zero_digest = true;
			} else if (HASH_ALGO_SHA256 == ctx->config.algorithm) {
				memcpy(zero_hash, &zero_message_hmac_sha256[0],
						SHA256_DIGEST_SIZE);
				*zero_hash_size = SHA256_DIGEST_SIZE;
				*zero_digest = true;
			} else {
				dev_err(device_data->dev, "[%s] "
						"Incorrect algorithm!"
						, __func__);
				ret = -EINVAL;
				goto out;
			}
		} else {
			dev_dbg(device_data->dev, "[%s] Continue hash "
					"calculation, since hmac key avalable",
					__func__);
		}
	}
out:

	return ret;
}

/**
 * hash_disable_power - Request to disable power and clock.
 * @device_data:	Structure for the hash device.
 * @save_device_state:	If true, saves the current hw state.
 *
 * This function request for disabling power (regulator) and clock,
 * and could also save current hw state.
 */
static int hash_disable_power(
		struct hash_device_data *device_data,
		bool			save_device_state)
{
	int ret = 0;
	struct device *dev = device_data->dev;

	spin_lock(&device_data->power_state_lock);
	if (!device_data->power_state)
		goto out;

	if (save_device_state) {
		hash_save_state(device_data,
				&device_data->state);
		device_data->restore_dev_state = true;
	}

	clk_disable(device_data->clk);
	ret = regulator_disable(device_data->regulator);
	if (ret)
		dev_err(dev, "[%s] regulator_disable() failed!", __func__);

	device_data->power_state = false;

out:
	spin_unlock(&device_data->power_state_lock);

	return ret;
}

/**
 * hash_enable_power - Request to enable power and clock.
 * @device_data:		Structure for the hash device.
 * @restore_device_state:	If true, restores a previous saved hw state.
 *
 * This function request for enabling power (regulator) and clock,
 * and could also restore a previously saved hw state.
 */
static int hash_enable_power(
		struct hash_device_data *device_data,
		bool			restore_device_state)
{
	int ret = 0;
	struct device *dev = device_data->dev;

	spin_lock(&device_data->power_state_lock);
	if (!device_data->power_state) {
		ret = regulator_enable(device_data->regulator);
		if (ret) {
			dev_err(dev, "[%s]: regulator_enable() failed!",
					__func__);
			goto out;
		}
		ret = clk_enable(device_data->clk);
		if (ret) {
			dev_err(dev, "[%s]: clk_enable() failed!",
					__func__);
			ret = regulator_disable(
					device_data->regulator);
			goto out;
		}
		device_data->power_state = true;
	}

	if (device_data->restore_dev_state) {
		if (restore_device_state) {
			device_data->restore_dev_state = false;
			hash_resume_state(device_data,
				&device_data->state);
		}
	}
out:
	spin_unlock(&device_data->power_state_lock);

	return ret;
}

/**
 * hash_get_device_data - Checks for an available hash device and return it.
 * @hash_ctx:		Structure for the hash context.
 * @device_data:	Structure for the hash device.
 *
 * This function check for an available hash device and return it to
 * the caller.
 * Note! Caller need to release the device, calling up().
 */
static int hash_get_device_data(struct hash_ctx *ctx,
				struct hash_device_data **device_data)
{
	int			ret;
	struct klist_iter	device_iterator;
	struct klist_node	*device_node;
	struct hash_device_data *local_device_data = NULL;

	/* Wait until a device is available */
	ret = down_interruptible(&driver_data.device_allocation);
	if (ret)
		return ret;  /* Interrupted */

	/* Select a device */
	klist_iter_init(&driver_data.device_list, &device_iterator);
	device_node = klist_next(&device_iterator);
	while (device_node) {
		local_device_data = container_of(device_node,
					   struct hash_device_data, list_node);
		spin_lock(&local_device_data->ctx_lock);
		/* current_ctx allocates a device, NULL = unallocated */
		if (local_device_data->current_ctx) {
			device_node = klist_next(&device_iterator);
		} else {
			local_device_data->current_ctx = ctx;
			ctx->device = local_device_data;
			spin_unlock(&local_device_data->ctx_lock);
			break;
		}
		spin_unlock(&local_device_data->ctx_lock);
	}
	klist_iter_exit(&device_iterator);

	if (!device_node) {
		/**
		 * No free device found.
		 * Since we allocated a device with down_interruptible, this
		 * should not be able to happen.
		 * Number of available devices, which are contained in
		 * device_allocation, is therefore decremented by not doing
		 * an up(device_allocation).
		 */
		return -EBUSY;
	}

	*device_data = local_device_data;

	return 0;
}

/**
 * hash_hw_write_key - Writes the key to the hardware registries.
 *
 * @device_data:	Structure for the hash device.
 * @key:		Key to be written.
 * @keylen:		The lengt of the key.
 *
 * Note! This function DOES NOT write to the NBLW registry, even though
 * specified in the the hw design spec. Either due to incorrect info in the
 * spec or due to a bug in the hw.
 */
static void hash_hw_write_key(struct hash_device_data *device_data,
		const u8 *key, unsigned int keylen)
{
	u32 word = 0;
	int nwords = 1;

	HASH_CLEAR_BITS(&device_data->base->str, HASH_STR_NBLW_MASK);

	while (keylen >= 4) {
		u32 *key_word = (u32 *)key;

		HASH_SET_DIN(key_word, nwords);
		keylen -= 4;
		key += 4;
	}

	/* Take care of the remaining bytes in the last word */
	if (keylen) {
		word = 0;
		while (keylen) {
			word |= (key[keylen - 1] << (8 * (keylen - 1)));
			keylen--;
		}

		HASH_SET_DIN(&word, nwords);
	}

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();

	HASH_SET_DCAL;

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();
}

/**
 * init_hash_hw - Initialise the hash hardware for a new calculation.
 * @device_data:	Structure for the hash device.
 * @ctx:		The hash context.
 *
 * This function will enable the bits needed to clear and start a new
 * calculation.
 */
static int init_hash_hw(struct hash_device_data *device_data,
		struct hash_ctx *ctx)
{
	int ret = 0;

	ret = hash_setconfiguration(device_data, &ctx->config);
	if (ret) {
		dev_err(device_data->dev, "[%s] hash_setconfiguration() "
				"failed!", __func__);
		return ret;
	}

	hash_begin(device_data, ctx);

	if (ctx->config.oper_mode == HASH_OPER_MODE_HMAC)
		hash_hw_write_key(device_data, ctx->key, ctx->keylen);

	return ret;
}

/**
 * hash_get_nents - Return number of entries (nents) in scatterlist (sg).
 *
 * @sg:		Scatterlist.
 * @size:	Size in bytes.
 * @aligned:	True if sg data aligned to work in DMA mode.
 *
 */
static int hash_get_nents(struct scatterlist *sg, int size, bool *aligned)
{
	int nents = 0;
	bool aligned_data = true;

	while (size > 0 && sg) {
		nents++;
		size -= sg->length;

		/* hash_set_dma_transfer will align last nent */
		if ((aligned && !IS_ALIGNED(sg->offset, HASH_DMA_ALIGN_SIZE))
			|| (!IS_ALIGNED(sg->length, HASH_DMA_ALIGN_SIZE) &&
				size > 0))
			aligned_data = false;

		sg = sg_next(sg);
	}

	if (aligned)
		*aligned = aligned_data;

	if (size != 0)
		return -EFAULT;

	return nents;
}

/**
 * hash_dma_valid_data - checks for dma valid sg data.
 * @sg:		Scatterlist.
 * @datasize:	Datasize in bytes.
 *
 * NOTE! This function checks for dma valid sg data, since dma
 * only accept datasizes of even wordsize.
 */
static bool hash_dma_valid_data(struct scatterlist *sg, int datasize)
{
	bool aligned;

	/* Need to include at least one nent, else error */
	if (hash_get_nents(sg, datasize, &aligned) < 1)
		return false;

	return aligned;
}

/**
 * hash_init - Common hash init function for SHA1/SHA2 (SHA256).
 * @req: The hash request for the job.
 *
 * Initialize structures.
 */
static int hash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);

	if (!ctx->key)
		ctx->keylen = 0;

	memset(&req_ctx->state, 0, sizeof(struct hash_state));
	req_ctx->updated = 0;
	if (hash_mode == HASH_MODE_DMA) {
		if (req->nbytes < HASH_DMA_ALIGN_SIZE) {
			req_ctx->dma_mode = false; /* Don't use DMA */

			pr_debug(DEV_DBG_NAME " [%s] DMA mode, but direct "
					"to CPU mode for data size < %d",
					__func__, HASH_DMA_ALIGN_SIZE);
		} else {
			if (req->nbytes >= HASH_DMA_PERFORMANCE_MIN_SIZE &&
					hash_dma_valid_data(req->src,
						req->nbytes)) {
				req_ctx->dma_mode = true;
			} else {
				req_ctx->dma_mode = false;
				pr_debug(DEV_DBG_NAME " [%s] DMA mode, but use"
						" CPU mode for datalength < %d"
						" or non-aligned data, except "
						"in last nent", __func__,
						HASH_DMA_PERFORMANCE_MIN_SIZE);
			}
		}
	}
	return 0;
}

/**
 * hash_processblock - This function processes a single block of 512 bits (64
 *                     bytes), word aligned, starting at message.
 * @device_data:	Structure for the hash device.
 * @message:		Block (512 bits) of message to be written to
 *			the HASH hardware.
 *
 */
static void hash_processblock(
		struct hash_device_data *device_data,
		const u32 *message, int length)
{
	int len = length / HASH_BYTES_PER_WORD;
	/*
	 * NBLW bits. Reset the number of bits in last word (NBLW).
	 */
	HASH_CLEAR_BITS(&device_data->base->str, HASH_STR_NBLW_MASK);

	/*
	 * Write message data to the HASH_DIN register.
	 */
	HASH_SET_DIN(message, len);
}

/**
 * hash_messagepad - Pads a message and write the nblw bits.
 * @device_data:	Structure for the hash device.
 * @message:		Last word of a message.
 * @index_bytes:	The number of bytes in the last message.
 *
 * This function manages the final part of the digest calculation, when less
 * than 512 bits (64 bytes) remain in message. This means index_bytes < 64.
 *
 */
static void hash_messagepad(struct hash_device_data *device_data,
		const u32 *message, u8 index_bytes)
{
	int nwords = 1;

	/*
	 * Clear hash str register, only clear NBLW
	 * since DCAL will be reset by hardware.
	 */
	HASH_CLEAR_BITS(&device_data->base->str, HASH_STR_NBLW_MASK);

	/* Main loop */
	while (index_bytes >= 4) {
		HASH_SET_DIN(message, nwords);
		index_bytes -= 4;
		message++;
	}

	if (index_bytes)
		HASH_SET_DIN(message, nwords);

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();

	/* num_of_bytes == 0 => NBLW <- 0 (32 bits valid in DATAIN) */
	HASH_SET_NBLW(index_bytes * 8);
	dev_dbg(device_data->dev, "[%s] DIN=0x%08x NBLW=%d", __func__,
			readl_relaxed(&device_data->base->din),
			(int)(readl_relaxed(&device_data->base->str) &
				HASH_STR_NBLW_MASK));
	HASH_SET_DCAL;
	dev_dbg(device_data->dev, "[%s] after dcal -> DIN=0x%08x NBLW=%d",
			__func__, readl_relaxed(&device_data->base->din),
			(int)(readl_relaxed(&device_data->base->str) &
				HASH_STR_NBLW_MASK));

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();
}

/**
 * hash_incrementlength - Increments the length of the current message.
 * @ctx: Hash context
 * @incr: Length of message processed already
 *
 * Overflow cannot occur, because conditions for overflow are checked in
 * hash_hw_update.
 */
static void hash_incrementlength(struct hash_req_ctx *ctx, u32 incr)
{
	ctx->state.length.low_word += incr;

	/* Check for wrap-around */
	if (ctx->state.length.low_word < incr)
		ctx->state.length.high_word++;
}

/**
 * hash_setconfiguration - Sets the required configuration for the hash
 *                         hardware.
 * @device_data:	Structure for the hash device.
 * @config:		Pointer to a configuration structure.
 */
int hash_setconfiguration(struct hash_device_data *device_data,
		struct hash_config *config)
{
	int ret = 0;

	if (config->algorithm != HASH_ALGO_SHA1 &&
	    config->algorithm != HASH_ALGO_SHA256)
		return -EPERM;

	/*
	 * DATAFORM bits. Set the DATAFORM bits to 0b11, which means the data
	 * to be written to HASH_DIN is considered as 32 bits.
	 */
	HASH_SET_DATA_FORMAT(config->data_format);

	/*
	 * ALGO bit. Set to 0b1 for SHA-1 and 0b0 for SHA-256
	 */
	switch (config->algorithm) {
	case HASH_ALGO_SHA1:
		HASH_SET_BITS(&device_data->base->cr, HASH_CR_ALGO_MASK);
		break;

	case HASH_ALGO_SHA256:
		HASH_CLEAR_BITS(&device_data->base->cr, HASH_CR_ALGO_MASK);
		break;

	default:
		dev_err(device_data->dev, "[%s] Incorrect algorithm.",
				__func__);
		return -EPERM;
	}

	/*
	 * MODE bit. This bit selects between HASH or HMAC mode for the
	 * selected algorithm. 0b0 = HASH and 0b1 = HMAC.
	 */
	if (HASH_OPER_MODE_HASH == config->oper_mode)
		HASH_CLEAR_BITS(&device_data->base->cr,
				HASH_CR_MODE_MASK);
	else if (HASH_OPER_MODE_HMAC == config->oper_mode) {
		HASH_SET_BITS(&device_data->base->cr,
				HASH_CR_MODE_MASK);
		if (device_data->current_ctx->keylen > HASH_BLOCK_SIZE) {
			/* Truncate key to blocksize */
			dev_dbg(device_data->dev, "[%s] LKEY set", __func__);
			HASH_SET_BITS(&device_data->base->cr,
					HASH_CR_LKEY_MASK);
		} else {
			dev_dbg(device_data->dev, "[%s] LKEY cleared",
					__func__);
			HASH_CLEAR_BITS(&device_data->base->cr,
					HASH_CR_LKEY_MASK);
		}
	} else {	/* Wrong hash mode */
		ret = -EPERM;
		dev_err(device_data->dev, "[%s] HASH_INVALID_PARAMETER!",
				__func__);
	}
	return ret;
}

/**
 * hash_begin - This routine resets some globals and initializes the hash
 *              hardware.
 * @device_data:	Structure for the hash device.
 * @ctx:		Hash context.
 */
void hash_begin(struct hash_device_data *device_data, struct hash_ctx *ctx)
{
	/* HW and SW initializations */
	/* Note: there is no need to initialize buffer and digest members */

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();

	/*
	 * INIT bit. Set this bit to 0b1 to reset the HASH processor core and
	 * prepare the initialize the HASH accelerator to compute the message
	 * digest of a new message.
	 */
	HASH_INITIALIZE;

	/*
	 * NBLW bits. Reset the number of bits in last word (NBLW).
	 */
	HASH_CLEAR_BITS(&device_data->base->str, HASH_STR_NBLW_MASK);
}

int hash_process_data(
		struct hash_device_data *device_data,
		struct hash_ctx *ctx, struct hash_req_ctx *req_ctx,
		int msg_length, u8 *data_buffer, u8 *buffer, u8 *index)
{
	int ret = 0;
	u32 count;

	do {
		if ((*index + msg_length) < HASH_BLOCK_SIZE) {
			for (count = 0; count < msg_length; count++) {
				buffer[*index + count] =
					*(data_buffer + count);
			}
			*index += msg_length;
			msg_length = 0;
		} else {
			if (req_ctx->updated) {

				ret = hash_resume_state(device_data,
						&device_data->state);
				memmove(req_ctx->state.buffer,
						device_data->state.buffer,
						HASH_BLOCK_SIZE / sizeof(u32));
				if (ret) {
					dev_err(device_data->dev, "[%s] "
							"hash_resume_state()"
							" failed!", __func__);
					goto out;
				}
			} else {
				ret = init_hash_hw(device_data, ctx);
				if (ret) {
					dev_err(device_data->dev, "[%s] "
							"init_hash_hw()"
							" failed!", __func__);
					goto out;
				}
				req_ctx->updated = 1;
			}
			/*
			 * If 'data_buffer' is four byte aligned and
			 * local buffer does not have any data, we can
			 * write data directly from 'data_buffer' to
			 * HW peripheral, otherwise we first copy data
			 * to a local buffer
			 */
			if ((0 == (((u32)data_buffer) % 4))
					&& (0 == *index))
				hash_processblock(device_data,
						(const u32 *)
						data_buffer, HASH_BLOCK_SIZE);
			else {
				for (count = 0; count <
						(u32)(HASH_BLOCK_SIZE -
							*index);
						count++) {
					buffer[*index + count] =
						*(data_buffer + count);
				}
				hash_processblock(device_data,
						(const u32 *)buffer,
						HASH_BLOCK_SIZE);
			}
			hash_incrementlength(req_ctx, HASH_BLOCK_SIZE);
			data_buffer += (HASH_BLOCK_SIZE - *index);

			msg_length -= (HASH_BLOCK_SIZE - *index);
			*index = 0;

			ret = hash_save_state(device_data,
					&device_data->state);

			memmove(device_data->state.buffer,
					req_ctx->state.buffer,
					HASH_BLOCK_SIZE / sizeof(u32));
			if (ret) {
				dev_err(device_data->dev, "[%s] "
						"hash_save_state()"
						" failed!", __func__);
				goto out;
			}
		}
	} while (msg_length != 0);
out:

	return ret;
}

/**
 * hash_dma_final - The hash dma final function for SHA1/SHA256.
 * @req:	The hash request for the job.
 */
static int hash_dma_final(struct ahash_request *req)
{
	int ret = 0;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct hash_device_data *device_data;
	u8 digest[SHA256_DIGEST_SIZE];
	int bytes_written = 0;

	ret = hash_get_device_data(ctx, &device_data);
	if (ret)
		return ret;

	dev_dbg(device_data->dev, "[%s] (ctx=0x%x)!", __func__, (u32) ctx);

	if (req_ctx->updated) {
		ret = hash_resume_state(device_data, &device_data->state);

		if (ret) {
			dev_err(device_data->dev, "[%s] hash_resume_state() "
					"failed!", __func__);
			goto out;
		}

	}

	if (!req_ctx->updated) {
		ret = hash_setconfiguration(device_data, &ctx->config);
		if (ret) {
			dev_err(device_data->dev, "[%s] "
					"hash_setconfiguration() failed!",
					__func__);
			goto out;
		}

		/* Enable DMA input */
		if (hash_mode != HASH_MODE_DMA || !req_ctx->dma_mode) {
			HASH_CLEAR_BITS(&device_data->base->cr,
					HASH_CR_DMAE_MASK);
		} else {
			HASH_SET_BITS(&device_data->base->cr,
					HASH_CR_DMAE_MASK);
			HASH_SET_BITS(&device_data->base->cr,
					HASH_CR_PRIVN_MASK);
		}

		HASH_INITIALIZE;

		if (ctx->config.oper_mode == HASH_OPER_MODE_HMAC)
			hash_hw_write_key(device_data, ctx->key, ctx->keylen);

		/* Number of bits in last word = (nbytes * 8) % 32 */
		HASH_SET_NBLW((req->nbytes * 8) % 32);
		req_ctx->updated = 1;
	}

	/* Store the nents in the dma struct. */
	ctx->device->dma.nents = hash_get_nents(req->src, req->nbytes, NULL);
	if (!ctx->device->dma.nents) {
		dev_err(device_data->dev, "[%s] "
				"ctx->device->dma.nents = 0", __func__);
		goto out;
	}

	bytes_written = hash_dma_write(ctx, req->src, req->nbytes);
	if (bytes_written != req->nbytes) {
		dev_err(device_data->dev, "[%s] "
				"hash_dma_write() failed!", __func__);
		goto out;
	}

	wait_for_completion(&ctx->device->dma.complete);
	hash_dma_done(ctx);

	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();

	if (ctx->config.oper_mode == HASH_OPER_MODE_HMAC && ctx->key) {
		unsigned int keylen = ctx->keylen;
		u8 *key = ctx->key;

		dev_dbg(device_data->dev, "[%s] keylen: %d", __func__,
				ctx->keylen);
		hash_hw_write_key(device_data, key, keylen);
	}

	hash_get_digest(device_data, digest, ctx->config.algorithm);
	memcpy(req->result, digest, ctx->digestsize);

out:
	release_hash_device(device_data);

	/**
	 * Allocated in setkey, and only used in HMAC.
	 */
	kfree(ctx->key);

	return ret;
}

/**
 * hash_hw_final - The final hash calculation function
 * @req:	The hash request for the job.
 */
int hash_hw_final(struct ahash_request *req)
{
	int ret = 0;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct hash_device_data *device_data;
	u8 digest[SHA256_DIGEST_SIZE];

	ret = hash_get_device_data(ctx, &device_data);
	if (ret)
		return ret;

	dev_dbg(device_data->dev, "[%s] (ctx=0x%x)!", __func__, (u32) ctx);

	if (req_ctx->updated) {
		ret = hash_resume_state(device_data, &device_data->state);

		if (ret) {
			dev_err(device_data->dev, "[%s] hash_resume_state() "
					"failed!", __func__);
			goto out;
		}
	} else if (req->nbytes == 0 && ctx->keylen == 0) {
		u8 zero_hash[SHA256_DIGEST_SIZE];
		u32 zero_hash_size = 0;
		bool zero_digest = false;
		/**
		 * Use a pre-calculated empty message digest
		 * (workaround since hw return zeroes, hw bug!?)
		 */
		ret = get_empty_message_digest(device_data, &zero_hash[0],
				&zero_hash_size, &zero_digest);
		if (!ret && likely(zero_hash_size == ctx->digestsize) &&
				zero_digest) {
			memcpy(req->result, &zero_hash[0], ctx->digestsize);
			goto out;
		} else if (!ret && !zero_digest) {
			dev_dbg(device_data->dev, "[%s] HMAC zero msg with "
					"key, continue...", __func__);
		} else {
			dev_err(device_data->dev, "[%s] ret=%d, or wrong "
					"digest size? %s", __func__, ret,
					(zero_hash_size == ctx->digestsize) ?
					"true" : "false");
			/* Return error */
			goto out;
		}
	} else if (req->nbytes == 0 && ctx->keylen > 0) {
		dev_err(device_data->dev, "[%s] Empty message with "
				"keylength > 0, NOT supported.", __func__);
		goto out;
	}

	if (!req_ctx->updated) {
		ret = init_hash_hw(device_data, ctx);
		if (ret) {
			dev_err(device_data->dev, "[%s] init_hash_hw() "
					"failed!", __func__);
			goto out;
		}
	}

	if (req_ctx->state.index) {
		hash_messagepad(device_data, req_ctx->state.buffer,
				req_ctx->state.index);
	} else {
		HASH_SET_DCAL;
		while (device_data->base->str & HASH_STR_DCAL_MASK)
			cpu_relax();
	}

	if (ctx->config.oper_mode == HASH_OPER_MODE_HMAC && ctx->key) {
		unsigned int keylen = ctx->keylen;
		u8 *key = ctx->key;

		dev_dbg(device_data->dev, "[%s] keylen: %d", __func__,
				ctx->keylen);
		hash_hw_write_key(device_data, key, keylen);
	}

	hash_get_digest(device_data, digest, ctx->config.algorithm);
	memcpy(req->result, digest, ctx->digestsize);

out:
	release_hash_device(device_data);

	/**
	 * Allocated in setkey, and only used in HMAC.
	 */
	kfree(ctx->key);

	return ret;
}

/**
 * hash_hw_update - Updates current HASH computation hashing another part of
 *                  the message.
 * @req:	Byte array containing the message to be hashed (caller
 *		allocated).
 */
int hash_hw_update(struct ahash_request *req)
{
	int ret = 0;
	u8 index = 0;
	u8 *buffer;
	struct hash_device_data *device_data;
	u8 *data_buffer;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_hash_walk walk;
	int msg_length = crypto_hash_walk_first(req, &walk);

	/* Empty message ("") is correct indata */
	if (msg_length == 0)
		return ret;

	index = req_ctx->state.index;
	buffer = (u8 *)req_ctx->state.buffer;

	/* Check if ctx->state.length + msg_length
	   overflows */
	if (msg_length > (req_ctx->state.length.low_word + msg_length) &&
			HASH_HIGH_WORD_MAX_VAL ==
			req_ctx->state.length.high_word) {
		pr_err(DEV_DBG_NAME " [%s] HASH_MSG_LENGTH_OVERFLOW!",
				__func__);
		return -EPERM;
	}

	ret = hash_get_device_data(ctx, &device_data);
	if (ret)
		return ret;

	/* Main loop */
	while (0 != msg_length) {
		data_buffer = walk.data;
		ret = hash_process_data(device_data, ctx, req_ctx, msg_length,
				data_buffer, buffer, &index);

		if (ret) {
			dev_err(device_data->dev, "[%s] hash_internal_hw_"
					"update() failed!", __func__);
			goto out;
		}

		msg_length = crypto_hash_walk_done(&walk, 0);
	}

	req_ctx->state.index = index;
	dev_dbg(device_data->dev, "[%s] indata length=%d, bin=%d))",
			__func__, req_ctx->state.index,
			req_ctx->state.bit_index);

out:
	release_hash_device(device_data);

	return ret;
}

/**
 * hash_resume_state - Function that resumes the state of an calculation.
 * @device_data:	Pointer to the device structure.
 * @device_state:	The state to be restored in the hash hardware
 */
int hash_resume_state(struct hash_device_data *device_data,
		const struct hash_state *device_state)
{
	u32 temp_cr;
	s32 count;
	int hash_mode = HASH_OPER_MODE_HASH;

	if (NULL == device_state) {
		dev_err(device_data->dev, "[%s] HASH_INVALID_PARAMETER!",
				__func__);
		return -EPERM;
	}

	/* Check correctness of index and length members */
	if (device_state->index > HASH_BLOCK_SIZE
	    || (device_state->length.low_word % HASH_BLOCK_SIZE) != 0) {
		dev_err(device_data->dev, "[%s] HASH_INVALID_PARAMETER!",
				__func__);
		return -EPERM;
	}

	/*
	 * INIT bit. Set this bit to 0b1 to reset the HASH processor core and
	 * prepare the initialize the HASH accelerator to compute the message
	 * digest of a new message.
	 */
	HASH_INITIALIZE;

	temp_cr = device_state->temp_cr;
	writel_relaxed(temp_cr & HASH_CR_RESUME_MASK, &device_data->base->cr);

	if (device_data->base->cr & HASH_CR_MODE_MASK)
		hash_mode = HASH_OPER_MODE_HMAC;
	else
		hash_mode = HASH_OPER_MODE_HASH;

	for (count = 0; count < HASH_CSR_COUNT; count++) {
		if ((count >= 36) && (hash_mode == HASH_OPER_MODE_HASH))
			break;

		writel_relaxed(device_state->csr[count],
				&device_data->base->csrx[count]);
	}

	writel_relaxed(device_state->csfull, &device_data->base->csfull);
	writel_relaxed(device_state->csdatain, &device_data->base->csdatain);

	writel_relaxed(device_state->str_reg, &device_data->base->str);
	writel_relaxed(temp_cr, &device_data->base->cr);

	return 0;
}

/**
 * hash_save_state - Function that saves the state of hardware.
 * @device_data:	Pointer to the device structure.
 * @device_state:	The strucure where the hardware state should be saved.
 */
int hash_save_state(struct hash_device_data *device_data,
		struct hash_state *device_state)
{
	u32 temp_cr;
	u32 count;
	int hash_mode = HASH_OPER_MODE_HASH;

	if (NULL == device_state) {
		dev_err(device_data->dev, "[%s] HASH_INVALID_PARAMETER!",
				__func__);
		return -ENOTSUPP;
	}

	/* Write dummy value to force digest intermediate calculation. This
	 * actually makes sure that there isn't any ongoing calculation in the
	 * hardware.
	 */
	while (device_data->base->str & HASH_STR_DCAL_MASK)
		cpu_relax();

	temp_cr = readl_relaxed(&device_data->base->cr);

	device_state->str_reg = readl_relaxed(&device_data->base->str);

	device_state->din_reg = readl_relaxed(&device_data->base->din);

	if (device_data->base->cr & HASH_CR_MODE_MASK)
		hash_mode = HASH_OPER_MODE_HMAC;
	else
		hash_mode = HASH_OPER_MODE_HASH;

	for (count = 0; count < HASH_CSR_COUNT; count++) {
		if ((count >= 36) && (hash_mode == HASH_OPER_MODE_HASH))
			break;

		device_state->csr[count] =
			readl_relaxed(&device_data->base->csrx[count]);
	}

	device_state->csfull = readl_relaxed(&device_data->base->csfull);
	device_state->csdatain = readl_relaxed(&device_data->base->csdatain);

	device_state->temp_cr = temp_cr;

	return 0;
}

/**
 * hash_check_hw - This routine checks for peripheral Ids and PCell Ids.
 * @device_data:
 *
 */
int hash_check_hw(struct hash_device_data *device_data)
{
	/* Checking Peripheral Ids  */
	if (HASH_P_ID0 == readl_relaxed(&device_data->base->periphid0)
		&& HASH_P_ID1 == readl_relaxed(&device_data->base->periphid1)
		&& HASH_P_ID2 == readl_relaxed(&device_data->base->periphid2)
		&& HASH_P_ID3 == readl_relaxed(&device_data->base->periphid3)
		&& HASH_CELL_ID0 == readl_relaxed(&device_data->base->cellid0)
		&& HASH_CELL_ID1 == readl_relaxed(&device_data->base->cellid1)
		&& HASH_CELL_ID2 == readl_relaxed(&device_data->base->cellid2)
		&& HASH_CELL_ID3 == readl_relaxed(&device_data->base->cellid3)
	   ) {
		return 0;
	}

	dev_err(device_data->dev, "[%s] HASH_UNSUPPORTED_HW!",
			__func__);
	return -ENOTSUPP;
}

/**
 * hash_get_digest - Gets the digest.
 * @device_data:	Pointer to the device structure.
 * @digest:		User allocated byte array for the calculated digest.
 * @algorithm:		The algorithm in use.
 */
void hash_get_digest(struct hash_device_data *device_data,
		u8 *digest, int algorithm)
{
	u32 temp_hx_val, count;
	int loop_ctr;

	if (algorithm != HASH_ALGO_SHA1 && algorithm != HASH_ALGO_SHA256) {
		dev_err(device_data->dev, "[%s] Incorrect algorithm %d",
				__func__, algorithm);
		return;
	}

	if (algorithm == HASH_ALGO_SHA1)
		loop_ctr = SHA1_DIGEST_SIZE / sizeof(u32);
	else
		loop_ctr = SHA256_DIGEST_SIZE / sizeof(u32);

	dev_dbg(device_data->dev, "[%s] digest array:(0x%x)",
			__func__, (u32) digest);

	/* Copy result into digest array */
	for (count = 0; count < loop_ctr; count++) {
		temp_hx_val = readl_relaxed(&device_data->base->hx[count]);
		digest[count * 4] = (u8) ((temp_hx_val >> 24) & 0xFF);
		digest[count * 4 + 1] = (u8) ((temp_hx_val >> 16) & 0xFF);
		digest[count * 4 + 2] = (u8) ((temp_hx_val >> 8) & 0xFF);
		digest[count * 4 + 3] = (u8) ((temp_hx_val >> 0) & 0xFF);
	}
}

/**
 * hash_update - The hash update function for SHA1/SHA2 (SHA256).
 * @req: The hash request for the job.
 */
static int ahash_update(struct ahash_request *req)
{
	int ret = 0;
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);

	if (hash_mode != HASH_MODE_DMA || !req_ctx->dma_mode)
		ret = hash_hw_update(req);
	/* Skip update for DMA, all data will be passed to DMA in final */

	if (ret) {
		pr_err(DEV_DBG_NAME " [%s] hash_hw_update() failed!",
				__func__);
	}

	return ret;
}

/**
 * hash_final - The hash final function for SHA1/SHA2 (SHA256).
 * @req:	The hash request for the job.
 */
static int ahash_final(struct ahash_request *req)
{
	int ret = 0;
	struct hash_req_ctx *req_ctx = ahash_request_ctx(req);

	pr_debug(DEV_DBG_NAME " [%s] data size: %d", __func__, req->nbytes);

	if ((hash_mode == HASH_MODE_DMA) && req_ctx->dma_mode)
		ret = hash_dma_final(req);
	else
		ret = hash_hw_final(req);

	if (ret) {
		pr_err(DEV_DBG_NAME " [%s] hash_hw/dma_final() failed",
				__func__);
	}

	return ret;
}

static int hash_setkey(struct crypto_ahash *tfm,
		const u8 *key, unsigned int keylen, int alg)
{
	int ret = 0;
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);

	/**
	 * Freed in final.
	 */
	ctx->key = kmalloc(keylen, GFP_KERNEL);
	if (!ctx->key) {
		pr_err(DEV_DBG_NAME " [%s] Failed to allocate ctx->key "
		       "for %d\n", __func__, alg);
		return -ENOMEM;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	return ret;
}

static int ahash_sha1_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ctx->config.data_format = HASH_DATA_8_BITS;
	ctx->config.algorithm = HASH_ALGO_SHA1;
	ctx->config.oper_mode = HASH_OPER_MODE_HASH;
	ctx->digestsize = SHA1_DIGEST_SIZE;

	return hash_init(req);
}

static int ahash_sha256_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ctx->config.data_format = HASH_DATA_8_BITS;
	ctx->config.algorithm = HASH_ALGO_SHA256;
	ctx->config.oper_mode = HASH_OPER_MODE_HASH;
	ctx->digestsize = SHA256_DIGEST_SIZE;

	return hash_init(req);
}

static int ahash_sha1_digest(struct ahash_request *req)
{
	int ret2, ret1;

	ret1 = ahash_sha1_init(req);
	if (ret1)
		goto out;

	ret1 = ahash_update(req);
	ret2 = ahash_final(req);

out:
	return ret1 ? ret1 : ret2;
}

static int ahash_sha256_digest(struct ahash_request *req)
{
	int ret2, ret1;

	ret1 = ahash_sha256_init(req);
	if (ret1)
		goto out;

	ret1 = ahash_update(req);
	ret2 = ahash_final(req);

out:
	return ret1 ? ret1 : ret2;
}

static int hmac_sha1_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ctx->config.data_format	= HASH_DATA_8_BITS;
	ctx->config.algorithm	= HASH_ALGO_SHA1;
	ctx->config.oper_mode	= HASH_OPER_MODE_HMAC;
	ctx->digestsize		= SHA1_DIGEST_SIZE;

	return hash_init(req);
}

static int hmac_sha256_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ctx->config.data_format	= HASH_DATA_8_BITS;
	ctx->config.algorithm	= HASH_ALGO_SHA256;
	ctx->config.oper_mode	= HASH_OPER_MODE_HMAC;
	ctx->digestsize		= SHA256_DIGEST_SIZE;

	return hash_init(req);
}

static int hmac_sha1_digest(struct ahash_request *req)
{
	int ret2, ret1;

	ret1 = hmac_sha1_init(req);
	if (ret1)
		goto out;

	ret1 = ahash_update(req);
	ret2 = ahash_final(req);

out:
	return ret1 ? ret1 : ret2;
}

static int hmac_sha256_digest(struct ahash_request *req)
{
	int ret2, ret1;

	ret1 = hmac_sha256_init(req);
	if (ret1)
		goto out;

	ret1 = ahash_update(req);
	ret2 = ahash_final(req);

out:
	return ret1 ? ret1 : ret2;
}

static int hmac_sha1_setkey(struct crypto_ahash *tfm,
		const u8 *key, unsigned int keylen)
{
	return hash_setkey(tfm, key, keylen, HASH_ALGO_SHA1);
}

static int hmac_sha256_setkey(struct crypto_ahash *tfm,
		const u8 *key, unsigned int keylen)
{
	return hash_setkey(tfm, key, keylen, HASH_ALGO_SHA256);
}

struct hash_algo_template {
	struct hash_config conf;
	struct ahash_alg hash;
};

static int hash_cra_init(struct crypto_tfm *tfm)
{
	struct hash_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct hash_algo_template *hash_alg;

	hash_alg = container_of(__crypto_ahash_alg(alg),
			struct hash_algo_template,
			hash);

	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
			sizeof(struct hash_req_ctx));

	ctx->config.data_format = HASH_DATA_8_BITS;
	ctx->config.algorithm = hash_alg->conf.algorithm;
	ctx->config.oper_mode = hash_alg->conf.oper_mode;

	ctx->digestsize = hash_alg->hash.halg.digestsize;

	return 0;
}

static struct hash_algo_template hash_algs[] = {
	{
			.conf.algorithm	= HASH_ALGO_SHA1,
			.conf.oper_mode	= HASH_OPER_MODE_HASH,
			.hash = {
				.init = hash_init,
				.update = ahash_update,
				.final = ahash_final,
				.digest = ahash_sha1_digest,
				.halg.digestsize = SHA1_DIGEST_SIZE,
				.halg.statesize = sizeof(struct hash_ctx),
				.halg.base = {
					.cra_name = "sha1",
					.cra_driver_name = "sha1-ux500",
					.cra_flags = CRYPTO_ALG_TYPE_AHASH |
							CRYPTO_ALG_ASYNC,
					.cra_blocksize = SHA1_BLOCK_SIZE,
					.cra_ctxsize = sizeof(struct hash_ctx),
					.cra_init = hash_cra_init,
					.cra_module = THIS_MODULE,
			}
		}
	},
	{
			.conf.algorithm		= HASH_ALGO_SHA256,
			.conf.oper_mode		= HASH_OPER_MODE_HASH,
			.hash = {
				.init = hash_init,
				.update	= ahash_update,
				.final = ahash_final,
				.digest = ahash_sha256_digest,
				.halg.digestsize = SHA256_DIGEST_SIZE,
				.halg.statesize = sizeof(struct hash_ctx),
				.halg.base = {
					.cra_name = "sha256",
					.cra_driver_name = "sha256-ux500",
					.cra_flags = CRYPTO_ALG_TYPE_AHASH |
							CRYPTO_ALG_ASYNC,
					.cra_blocksize = SHA256_BLOCK_SIZE,
					.cra_ctxsize = sizeof(struct hash_ctx),
					.cra_type = &crypto_ahash_type,
					.cra_init = hash_cra_init,
					.cra_module = THIS_MODULE,
				}
			}

	},
	{
			.conf.algorithm		= HASH_ALGO_SHA1,
			.conf.oper_mode		= HASH_OPER_MODE_HMAC,
			.hash = {
				.init = hash_init,
				.update = ahash_update,
				.final = ahash_final,
				.digest = hmac_sha1_digest,
				.setkey = hmac_sha1_setkey,
				.halg.digestsize = SHA1_DIGEST_SIZE,
				.halg.statesize = sizeof(struct hash_ctx),
				.halg.base = {
					.cra_name = "hmac(sha1)",
					.cra_driver_name = "hmac-sha1-ux500",
					.cra_flags = CRYPTO_ALG_TYPE_AHASH |
							CRYPTO_ALG_ASYNC,
					.cra_blocksize = SHA1_BLOCK_SIZE,
					.cra_ctxsize = sizeof(struct hash_ctx),
					.cra_type = &crypto_ahash_type,
					.cra_init = hash_cra_init,
					.cra_module = THIS_MODULE,
				}
			}
	},
	{
			.conf.algorithm		= HASH_ALGO_SHA256,
			.conf.oper_mode		= HASH_OPER_MODE_HMAC,
			.hash = {
				.init = hash_init,
				.update = ahash_update,
				.final = ahash_final,
				.digest = hmac_sha256_digest,
				.setkey = hmac_sha256_setkey,
				.halg.digestsize = SHA256_DIGEST_SIZE,
				.halg.statesize = sizeof(struct hash_ctx),
				.halg.base = {
					.cra_name = "hmac(sha256)",
					.cra_driver_name = "hmac-sha256-ux500",
					.cra_flags = CRYPTO_ALG_TYPE_AHASH |
							CRYPTO_ALG_ASYNC,
					.cra_blocksize = SHA256_BLOCK_SIZE,
					.cra_ctxsize = sizeof(struct hash_ctx),
					.cra_type = &crypto_ahash_type,
					.cra_init = hash_cra_init,
					.cra_module = THIS_MODULE,
				}
			}
	}
};

/**
 * hash_algs_register_all -
 */
static int ahash_algs_register_all(struct hash_device_data *device_data)
{
	int ret;
	int i;
	int count;

	for (i = 0; i < ARRAY_SIZE(hash_algs); i++) {
		ret = crypto_register_ahash(&hash_algs[i].hash);
		if (ret) {
			count = i;
			dev_err(device_data->dev, "[%s] alg registration failed",
				hash_algs[i].hash.halg.base.cra_driver_name);
			goto unreg;
		}
	}
	return 0;
unreg:
	for (i = 0; i < count; i++)
		crypto_unregister_ahash(&hash_algs[i].hash);
	return ret;
}

/**
 * hash_algs_unregister_all -
 */
static void ahash_algs_unregister_all(struct hash_device_data *device_data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hash_algs); i++)
		crypto_unregister_ahash(&hash_algs[i].hash);
}

/**
 * ux500_hash_probe - Function that probes the hash hardware.
 * @pdev: The platform device.
 */
static int ux500_hash_probe(struct platform_device *pdev)
{
	int			ret = 0;
	struct resource		*res = NULL;
	struct hash_device_data *device_data;
	struct device		*dev = &pdev->dev;

	device_data = kzalloc(sizeof(struct hash_device_data), GFP_ATOMIC);
	if (!device_data) {
		dev_dbg(dev, "[%s] kzalloc() failed!", __func__);
		ret = -ENOMEM;
		goto out;
	}

	device_data->dev = dev;
	device_data->current_ctx = NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_dbg(dev, "[%s] platform_get_resource() failed!", __func__);
		ret = -ENODEV;
		goto out_kfree;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		dev_dbg(dev, "[%s] request_mem_region() failed!", __func__);
		ret = -EBUSY;
		goto out_kfree;
	}

	device_data->base = ioremap(res->start, resource_size(res));
	if (!device_data->base) {
		dev_err(dev, "[%s] ioremap() failed!",
				__func__);
		ret = -ENOMEM;
		goto out_free_mem;
	}
	spin_lock_init(&device_data->ctx_lock);
	spin_lock_init(&device_data->power_state_lock);

	/* Enable power for HASH1 hardware block */
	device_data->regulator = regulator_get(dev, "v-ape");
	if (IS_ERR(device_data->regulator)) {
		dev_err(dev, "[%s] regulator_get() failed!", __func__);
		ret = PTR_ERR(device_data->regulator);
		device_data->regulator = NULL;
		goto out_unmap;
	}

	/* Enable the clock for HASH1 hardware block */
	device_data->clk = clk_get(dev, NULL);
	if (IS_ERR(device_data->clk)) {
		dev_err(dev, "[%s] clk_get() failed!", __func__);
		ret = PTR_ERR(device_data->clk);
		goto out_regulator;
	}

	/* Enable device power (and clock) */
	ret = hash_enable_power(device_data, false);
	if (ret) {
		dev_err(dev, "[%s]: hash_enable_power() failed!", __func__);
		goto out_clk;
	}

	ret = hash_check_hw(device_data);
	if (ret) {
		dev_err(dev, "[%s] hash_check_hw() failed!", __func__);
		goto out_power;
	}

	if (hash_mode == HASH_MODE_DMA)
		hash_dma_setup_channel(device_data, dev);

	platform_set_drvdata(pdev, device_data);

	/* Put the new device into the device list... */
	klist_add_tail(&device_data->list_node, &driver_data.device_list);
	/* ... and signal that a new device is available. */
	up(&driver_data.device_allocation);

	ret = ahash_algs_register_all(device_data);
	if (ret) {
		dev_err(dev, "[%s] ahash_algs_register_all() "
				"failed!", __func__);
		goto out_power;
	}

	dev_info(dev, "[%s] successfully probed\n", __func__);
	return 0;

out_power:
	hash_disable_power(device_data, false);

out_clk:
	clk_put(device_data->clk);

out_regulator:
	regulator_put(device_data->regulator);

out_unmap:
	iounmap(device_data->base);

out_free_mem:
	release_mem_region(res->start, resource_size(res));

out_kfree:
	kfree(device_data);
out:
	return ret;
}

/**
 * ux500_hash_remove - Function that removes the hash device from the platform.
 * @pdev: The platform device.
 */
static int ux500_hash_remove(struct platform_device *pdev)
{
	struct resource		*res;
	struct hash_device_data *device_data;
	struct device		*dev = &pdev->dev;

	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(dev, "[%s]: platform_get_drvdata() failed!",
			__func__);
		return -ENOMEM;
	}

	/* Try to decrease the number of available devices. */
	if (down_trylock(&driver_data.device_allocation))
		return -EBUSY;

	/* Check that the device is free */
	spin_lock(&device_data->ctx_lock);
	/* current_ctx allocates a device, NULL = unallocated */
	if (device_data->current_ctx) {
		/* The device is busy */
		spin_unlock(&device_data->ctx_lock);
		/* Return the device to the pool. */
		up(&driver_data.device_allocation);
		return -EBUSY;
	}

	spin_unlock(&device_data->ctx_lock);

	/* Remove the device from the list */
	if (klist_node_attached(&device_data->list_node))
		klist_remove(&device_data->list_node);

	/* If this was the last device, remove the services */
	if (list_empty(&driver_data.device_list.k_list))
		ahash_algs_unregister_all(device_data);

	if (hash_disable_power(device_data, false))
		dev_err(dev, "[%s]: hash_disable_power() failed",
			__func__);

	clk_put(device_data->clk);
	regulator_put(device_data->regulator);

	iounmap(device_data->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));

	kfree(device_data);

	return 0;
}

/**
 * ux500_hash_shutdown - Function that shutdown the hash device.
 * @pdev: The platform device
 */
static void ux500_hash_shutdown(struct platform_device *pdev)
{
	struct resource *res = NULL;
	struct hash_device_data *device_data;

	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(&pdev->dev, "[%s] platform_get_drvdata() failed!",
				__func__);
		return;
	}

	/* Check that the device is free */
	spin_lock(&device_data->ctx_lock);
	/* current_ctx allocates a device, NULL = unallocated */
	if (!device_data->current_ctx) {
		if (down_trylock(&driver_data.device_allocation))
			dev_dbg(&pdev->dev, "[%s]: Cryp still in use!"
				"Shutting down anyway...", __func__);
		/**
		 * (Allocate the device)
		 * Need to set this to non-null (dummy) value,
		 * to avoid usage if context switching.
		 */
		device_data->current_ctx++;
	}
	spin_unlock(&device_data->ctx_lock);

	/* Remove the device from the list */
	if (klist_node_attached(&device_data->list_node))
		klist_remove(&device_data->list_node);

	/* If this was the last device, remove the services */
	if (list_empty(&driver_data.device_list.k_list))
		ahash_algs_unregister_all(device_data);

	iounmap(device_data->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res)
		release_mem_region(res->start, resource_size(res));

	if (hash_disable_power(device_data, false))
		dev_err(&pdev->dev, "[%s] hash_disable_power() failed",
				__func__);
}

/**
 * ux500_hash_suspend - Function that suspends the hash device.
 * @dev:	Device to suspend.
 */
static int ux500_hash_suspend(struct device *dev)
{
	int ret;
	struct hash_device_data *device_data;
	struct hash_ctx *temp_ctx = NULL;

	device_data = dev_get_drvdata(dev);
	if (!device_data) {
		dev_err(dev, "[%s] platform_get_drvdata() failed!", __func__);
		return -ENOMEM;
	}

	spin_lock(&device_data->ctx_lock);
	if (!device_data->current_ctx)
		device_data->current_ctx++;
	spin_unlock(&device_data->ctx_lock);

	if (device_data->current_ctx == ++temp_ctx) {
		if (down_interruptible(&driver_data.device_allocation))
			dev_dbg(dev, "[%s]: down_interruptible() failed",
				__func__);
		ret = hash_disable_power(device_data, false);

	} else
		ret = hash_disable_power(device_data, true);

	if (ret)
		dev_err(dev, "[%s]: hash_disable_power()", __func__);

	return ret;
}

/**
 * ux500_hash_resume - Function that resume the hash device.
 * @dev:	Device to resume.
 */
static int ux500_hash_resume(struct device *dev)
{
	int ret = 0;
	struct hash_device_data *device_data;
	struct hash_ctx *temp_ctx = NULL;

	device_data = dev_get_drvdata(dev);
	if (!device_data) {
		dev_err(dev, "[%s] platform_get_drvdata() failed!", __func__);
		return -ENOMEM;
	}

	spin_lock(&device_data->ctx_lock);
	if (device_data->current_ctx == ++temp_ctx)
		device_data->current_ctx = NULL;
	spin_unlock(&device_data->ctx_lock);

	if (!device_data->current_ctx)
		up(&driver_data.device_allocation);
	else
		ret = hash_enable_power(device_data, true);

	if (ret)
		dev_err(dev, "[%s]: hash_enable_power() failed!", __func__);

	return ret;
}

static SIMPLE_DEV_PM_OPS(ux500_hash_pm, ux500_hash_suspend, ux500_hash_resume);

static struct platform_driver hash_driver = {
	.probe  = ux500_hash_probe,
	.remove = ux500_hash_remove,
	.shutdown = ux500_hash_shutdown,
	.driver = {
		.owner = THIS_MODULE,
		.name  = "hash1",
		.pm    = &ux500_hash_pm,
	}
};

/**
 * ux500_hash_mod_init - The kernel module init function.
 */
static int __init ux500_hash_mod_init(void)
{
	klist_init(&driver_data.device_list, NULL, NULL);
	/* Initialize the semaphore to 0 devices (locked state) */
	sema_init(&driver_data.device_allocation, 0);

	return platform_driver_register(&hash_driver);
}

/**
 * ux500_hash_mod_fini - The kernel module exit function.
 */
static void __exit ux500_hash_mod_fini(void)
{
	platform_driver_unregister(&hash_driver);
	return;
}

module_init(ux500_hash_mod_init);
module_exit(ux500_hash_mod_fini);

MODULE_DESCRIPTION("Driver for ST-Ericsson UX500 HASH engine.");
MODULE_LICENSE("GPL");

MODULE_ALIAS("sha1-all");
MODULE_ALIAS("sha256-all");
MODULE_ALIAS("hmac-sha1-all");
MODULE_ALIAS("hmac-sha256-all");
