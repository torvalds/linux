// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Andreas Westin <andreas.westin@stericsson.com> for ST-Ericsson.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/klist.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/internal/des.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>

#include <linux/platform_data/crypto-ux500.h>

#include "cryp_p.h"
#include "cryp.h"

#define CRYP_MAX_KEY_SIZE	32
#define BYTES_PER_WORD		4

static int cryp_mode;
static atomic_t session_id;

static struct stedma40_chan_cfg *mem_to_engine;
static struct stedma40_chan_cfg *engine_to_mem;

/**
 * struct cryp_driver_data - data specific to the driver.
 *
 * @device_list: A list of registered devices to choose from.
 * @device_allocation: A semaphore initialized with number of devices.
 */
struct cryp_driver_data {
	struct klist device_list;
	struct semaphore device_allocation;
};

/**
 * struct cryp_ctx - Crypto context
 * @config: Crypto mode.
 * @key: Key array.
 * @keylen: Length of key.
 * @iv: Pointer to initialization vector.
 * @indata: Pointer to indata.
 * @outdata: Pointer to outdata.
 * @datalen: Length of indata.
 * @outlen: Length of outdata.
 * @blocksize: Size of blocks.
 * @updated: Updated flag.
 * @dev_ctx: Device dependent context.
 * @device: Pointer to the device.
 * @session_id: Atomic session ID.
 */
struct cryp_ctx {
	struct cryp_config config;
	u8 key[CRYP_MAX_KEY_SIZE];
	u32 keylen;
	u8 *iv;
	const u8 *indata;
	u8 *outdata;
	u32 datalen;
	u32 outlen;
	u32 blocksize;
	u8 updated;
	struct cryp_device_context dev_ctx;
	struct cryp_device_data *device;
	u32 session_id;
};

static struct cryp_driver_data driver_data;

/**
 * swap_bits_in_byte - mirror the bits in a byte
 * @b: the byte to be mirrored
 *
 * The bits are swapped the following way:
 *  Byte b include bits 0-7, nibble 1 (n1) include bits 0-3 and
 *  nibble 2 (n2) bits 4-7.
 *
 *  Nibble 1 (n1):
 *  (The "old" (moved) bit is replaced with a zero)
 *  1. Move bit 6 and 7, 4 positions to the left.
 *  2. Move bit 3 and 5, 2 positions to the left.
 *  3. Move bit 1-4, 1 position to the left.
 *
 *  Nibble 2 (n2):
 *  1. Move bit 0 and 1, 4 positions to the right.
 *  2. Move bit 2 and 4, 2 positions to the right.
 *  3. Move bit 3-6, 1 position to the right.
 *
 *  Combine the two nibbles to a complete and swapped byte.
 */

static inline u8 swap_bits_in_byte(u8 b)
{
#define R_SHIFT_4_MASK  0xc0 /* Bits 6 and 7, right shift 4 */
#define R_SHIFT_2_MASK  0x28 /* (After right shift 4) Bits 3 and 5,
				  right shift 2 */
#define R_SHIFT_1_MASK  0x1e /* (After right shift 2) Bits 1-4,
				  right shift 1 */
#define L_SHIFT_4_MASK  0x03 /* Bits 0 and 1, left shift 4 */
#define L_SHIFT_2_MASK  0x14 /* (After left shift 4) Bits 2 and 4,
				  left shift 2 */
#define L_SHIFT_1_MASK  0x78 /* (After left shift 1) Bits 3-6,
				  left shift 1 */

	u8 n1;
	u8 n2;

	/* Swap most significant nibble */
	/* Right shift 4, bits 6 and 7 */
	n1 = ((b  & R_SHIFT_4_MASK) >> 4) | (b  & ~(R_SHIFT_4_MASK >> 4));
	/* Right shift 2, bits 3 and 5 */
	n1 = ((n1 & R_SHIFT_2_MASK) >> 2) | (n1 & ~(R_SHIFT_2_MASK >> 2));
	/* Right shift 1, bits 1-4 */
	n1 = (n1  & R_SHIFT_1_MASK) >> 1;

	/* Swap least significant nibble */
	/* Left shift 4, bits 0 and 1 */
	n2 = ((b  & L_SHIFT_4_MASK) << 4) | (b  & ~(L_SHIFT_4_MASK << 4));
	/* Left shift 2, bits 2 and 4 */
	n2 = ((n2 & L_SHIFT_2_MASK) << 2) | (n2 & ~(L_SHIFT_2_MASK << 2));
	/* Left shift 1, bits 3-6 */
	n2 = (n2  & L_SHIFT_1_MASK) << 1;

	return n1 | n2;
}

static inline void swap_words_in_key_and_bits_in_byte(const u8 *in,
						      u8 *out, u32 len)
{
	unsigned int i = 0;
	int j;
	int index = 0;

	j = len - BYTES_PER_WORD;
	while (j >= 0) {
		for (i = 0; i < BYTES_PER_WORD; i++) {
			index = len - j - BYTES_PER_WORD + i;
			out[j + i] =
				swap_bits_in_byte(in[index]);
		}
		j -= BYTES_PER_WORD;
	}
}

static void add_session_id(struct cryp_ctx *ctx)
{
	/*
	 * We never want 0 to be a valid value, since this is the default value
	 * for the software context.
	 */
	if (unlikely(atomic_inc_and_test(&session_id)))
		atomic_inc(&session_id);

	ctx->session_id = atomic_read(&session_id);
}

static irqreturn_t cryp_interrupt_handler(int irq, void *param)
{
	struct cryp_ctx *ctx;
	int count;
	struct cryp_device_data *device_data;

	if (param == NULL) {
		BUG_ON(!param);
		return IRQ_HANDLED;
	}

	/* The device is coming from the one found in hw_crypt_noxts. */
	device_data = (struct cryp_device_data *)param;

	ctx = device_data->current_ctx;

	if (ctx == NULL) {
		BUG_ON(!ctx);
		return IRQ_HANDLED;
	}

	dev_dbg(ctx->device->dev, "[%s] (len: %d) %s, ", __func__, ctx->outlen,
		cryp_pending_irq_src(device_data, CRYP_IRQ_SRC_OUTPUT_FIFO) ?
		"out" : "in");

	if (cryp_pending_irq_src(device_data,
				 CRYP_IRQ_SRC_OUTPUT_FIFO)) {
		if (ctx->outlen / ctx->blocksize > 0) {
			count = ctx->blocksize / 4;

			readsl(&device_data->base->dout, ctx->outdata, count);
			ctx->outdata += count;
			ctx->outlen -= count;

			if (ctx->outlen == 0) {
				cryp_disable_irq_src(device_data,
						     CRYP_IRQ_SRC_OUTPUT_FIFO);
			}
		}
	} else if (cryp_pending_irq_src(device_data,
					CRYP_IRQ_SRC_INPUT_FIFO)) {
		if (ctx->datalen / ctx->blocksize > 0) {
			count = ctx->blocksize / 4;

			writesl(&device_data->base->din, ctx->indata, count);

			ctx->indata += count;
			ctx->datalen -= count;

			if (ctx->datalen == 0)
				cryp_disable_irq_src(device_data,
						   CRYP_IRQ_SRC_INPUT_FIFO);

			if (ctx->config.algomode == CRYP_ALGO_AES_XTS) {
				CRYP_PUT_BITS(&device_data->base->cr,
					      CRYP_START_ENABLE,
					      CRYP_CR_START_POS,
					      CRYP_CR_START_MASK);

				cryp_wait_until_done(device_data);
			}
		}
	}

	return IRQ_HANDLED;
}

static int mode_is_aes(enum cryp_algo_mode mode)
{
	return	CRYP_ALGO_AES_ECB == mode ||
		CRYP_ALGO_AES_CBC == mode ||
		CRYP_ALGO_AES_CTR == mode ||
		CRYP_ALGO_AES_XTS == mode;
}

static int cfg_iv(struct cryp_device_data *device_data, u32 left, u32 right,
		  enum cryp_init_vector_index index)
{
	struct cryp_init_vector_value vector_value;

	dev_dbg(device_data->dev, "[%s]", __func__);

	vector_value.init_value_left = left;
	vector_value.init_value_right = right;

	return cryp_configure_init_vector(device_data,
					  index,
					  vector_value);
}

static int cfg_ivs(struct cryp_device_data *device_data, struct cryp_ctx *ctx)
{
	int i;
	int status = 0;
	int num_of_regs = ctx->blocksize / 8;
	__be32 *civ = (__be32 *)ctx->iv;
	u32 iv[AES_BLOCK_SIZE / 4];

	dev_dbg(device_data->dev, "[%s]", __func__);

	/*
	 * Since we loop on num_of_regs we need to have a check in case
	 * someone provides an incorrect blocksize which would force calling
	 * cfg_iv with i greater than 2 which is an error.
	 */
	if (num_of_regs > 2) {
		dev_err(device_data->dev, "[%s] Incorrect blocksize %d",
			__func__, ctx->blocksize);
		return -EINVAL;
	}

	for (i = 0; i < ctx->blocksize / 4; i++)
		iv[i] = be32_to_cpup(civ + i);

	for (i = 0; i < num_of_regs; i++) {
		status = cfg_iv(device_data, iv[i*2], iv[i*2+1],
				(enum cryp_init_vector_index) i);
		if (status != 0)
			return status;
	}
	return status;
}

static int set_key(struct cryp_device_data *device_data,
		   u32 left_key,
		   u32 right_key,
		   enum cryp_key_reg_index index)
{
	struct cryp_key_value key_value;
	int cryp_error;

	dev_dbg(device_data->dev, "[%s]", __func__);

	key_value.key_value_left = left_key;
	key_value.key_value_right = right_key;

	cryp_error = cryp_configure_key_values(device_data,
					       index,
					       key_value);
	if (cryp_error != 0)
		dev_err(device_data->dev, "[%s]: "
			"cryp_configure_key_values() failed!", __func__);

	return cryp_error;
}

static int cfg_keys(struct cryp_ctx *ctx)
{
	int i;
	int num_of_regs = ctx->keylen / 8;
	u32 swapped_key[CRYP_MAX_KEY_SIZE / 4];
	__be32 *ckey = (__be32 *)ctx->key;
	int cryp_error = 0;

	dev_dbg(ctx->device->dev, "[%s]", __func__);

	if (mode_is_aes(ctx->config.algomode)) {
		swap_words_in_key_and_bits_in_byte((u8 *)ckey,
						   (u8 *)swapped_key,
						   ctx->keylen);
	} else {
		for (i = 0; i < ctx->keylen / 4; i++)
			swapped_key[i] = be32_to_cpup(ckey + i);
	}

	for (i = 0; i < num_of_regs; i++) {
		cryp_error = set_key(ctx->device,
				     swapped_key[i * 2],
				     swapped_key[i * 2 + 1],
				     (enum cryp_key_reg_index) i);

		if (cryp_error != 0) {
			dev_err(ctx->device->dev, "[%s]: set_key() failed!",
					__func__);
			return cryp_error;
		}
	}
	return cryp_error;
}

static int cryp_setup_context(struct cryp_ctx *ctx,
			      struct cryp_device_data *device_data)
{
	u32 control_register = CRYP_CR_DEFAULT;

	switch (cryp_mode) {
	case CRYP_MODE_INTERRUPT:
		writel_relaxed(CRYP_IMSC_DEFAULT, &device_data->base->imsc);
		break;

	case CRYP_MODE_DMA:
		writel_relaxed(CRYP_DMACR_DEFAULT, &device_data->base->dmacr);
		break;

	default:
		break;
	}

	if (ctx->updated == 0) {
		cryp_flush_inoutfifo(device_data);
		if (cfg_keys(ctx) != 0) {
			dev_err(ctx->device->dev, "[%s]: cfg_keys failed!",
				__func__);
			return -EINVAL;
		}

		if (ctx->iv &&
		    CRYP_ALGO_AES_ECB != ctx->config.algomode &&
		    CRYP_ALGO_DES_ECB != ctx->config.algomode &&
		    CRYP_ALGO_TDES_ECB != ctx->config.algomode) {
			if (cfg_ivs(device_data, ctx) != 0)
				return -EPERM;
		}

		cryp_set_configuration(device_data, &ctx->config,
				       &control_register);
		add_session_id(ctx);
	} else if (ctx->updated == 1 &&
		   ctx->session_id != atomic_read(&session_id)) {
		cryp_flush_inoutfifo(device_data);
		cryp_restore_device_context(device_data, &ctx->dev_ctx);

		add_session_id(ctx);
		control_register = ctx->dev_ctx.cr;
	} else
		control_register = ctx->dev_ctx.cr;

	writel(control_register |
	       (CRYP_CRYPEN_ENABLE << CRYP_CR_CRYPEN_POS),
	       &device_data->base->cr);

	return 0;
}

static int cryp_get_device_data(struct cryp_ctx *ctx,
				struct cryp_device_data **device_data)
{
	int ret;
	struct klist_iter device_iterator;
	struct klist_node *device_node;
	struct cryp_device_data *local_device_data = NULL;
	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	/* Wait until a device is available */
	ret = down_interruptible(&driver_data.device_allocation);
	if (ret)
		return ret;  /* Interrupted */

	/* Select a device */
	klist_iter_init(&driver_data.device_list, &device_iterator);

	device_node = klist_next(&device_iterator);
	while (device_node) {
		local_device_data = container_of(device_node,
					   struct cryp_device_data, list_node);
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

static void cryp_dma_setup_channel(struct cryp_device_data *device_data,
				   struct device *dev)
{
	struct dma_slave_config mem2cryp = {
		.direction = DMA_MEM_TO_DEV,
		.dst_addr = device_data->phybase + CRYP_DMA_TX_FIFO,
		.dst_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES,
		.dst_maxburst = 4,
	};
	struct dma_slave_config cryp2mem = {
		.direction = DMA_DEV_TO_MEM,
		.src_addr = device_data->phybase + CRYP_DMA_RX_FIFO,
		.src_addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES,
		.src_maxburst = 4,
	};

	dma_cap_zero(device_data->dma.mask);
	dma_cap_set(DMA_SLAVE, device_data->dma.mask);

	device_data->dma.cfg_mem2cryp = mem_to_engine;
	device_data->dma.chan_mem2cryp =
		dma_request_channel(device_data->dma.mask,
				    stedma40_filter,
				    device_data->dma.cfg_mem2cryp);

	device_data->dma.cfg_cryp2mem = engine_to_mem;
	device_data->dma.chan_cryp2mem =
		dma_request_channel(device_data->dma.mask,
				    stedma40_filter,
				    device_data->dma.cfg_cryp2mem);

	dmaengine_slave_config(device_data->dma.chan_mem2cryp, &mem2cryp);
	dmaengine_slave_config(device_data->dma.chan_cryp2mem, &cryp2mem);

	init_completion(&device_data->dma.cryp_dma_complete);
}

static void cryp_dma_out_callback(void *data)
{
	struct cryp_ctx *ctx = (struct cryp_ctx *) data;
	dev_dbg(ctx->device->dev, "[%s]: ", __func__);

	complete(&ctx->device->dma.cryp_dma_complete);
}

static int cryp_set_dma_transfer(struct cryp_ctx *ctx,
				 struct scatterlist *sg,
				 int len,
				 enum dma_data_direction direction)
{
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *channel = NULL;
	dma_cookie_t cookie;

	dev_dbg(ctx->device->dev, "[%s]: ", __func__);

	if (unlikely(!IS_ALIGNED((unsigned long)sg, 4))) {
		dev_err(ctx->device->dev, "[%s]: Data in sg list isn't "
			"aligned! Addr: 0x%08lx", __func__, (unsigned long)sg);
		return -EFAULT;
	}

	switch (direction) {
	case DMA_TO_DEVICE:
		channel = ctx->device->dma.chan_mem2cryp;
		ctx->device->dma.sg_src = sg;
		ctx->device->dma.sg_src_len = dma_map_sg(channel->device->dev,
						 ctx->device->dma.sg_src,
						 ctx->device->dma.nents_src,
						 direction);

		if (!ctx->device->dma.sg_src_len) {
			dev_dbg(ctx->device->dev,
				"[%s]: Could not map the sg list (TO_DEVICE)",
				__func__);
			return -EFAULT;
		}

		dev_dbg(ctx->device->dev, "[%s]: Setting up DMA for buffer "
			"(TO_DEVICE)", __func__);

		desc = dmaengine_prep_slave_sg(channel,
				ctx->device->dma.sg_src,
				ctx->device->dma.sg_src_len,
				DMA_MEM_TO_DEV, DMA_CTRL_ACK);
		break;

	case DMA_FROM_DEVICE:
		channel = ctx->device->dma.chan_cryp2mem;
		ctx->device->dma.sg_dst = sg;
		ctx->device->dma.sg_dst_len = dma_map_sg(channel->device->dev,
						 ctx->device->dma.sg_dst,
						 ctx->device->dma.nents_dst,
						 direction);

		if (!ctx->device->dma.sg_dst_len) {
			dev_dbg(ctx->device->dev,
				"[%s]: Could not map the sg list (FROM_DEVICE)",
				__func__);
			return -EFAULT;
		}

		dev_dbg(ctx->device->dev, "[%s]: Setting up DMA for buffer "
			"(FROM_DEVICE)", __func__);

		desc = dmaengine_prep_slave_sg(channel,
				ctx->device->dma.sg_dst,
				ctx->device->dma.sg_dst_len,
				DMA_DEV_TO_MEM,
				DMA_CTRL_ACK |
				DMA_PREP_INTERRUPT);

		desc->callback = cryp_dma_out_callback;
		desc->callback_param = ctx;
		break;

	default:
		dev_dbg(ctx->device->dev, "[%s]: Invalid DMA direction",
			__func__);
		return -EFAULT;
	}

	cookie = dmaengine_submit(desc);
	if (dma_submit_error(cookie)) {
		dev_dbg(ctx->device->dev, "[%s]: DMA submission failed\n",
			__func__);
		return cookie;
	}

	dma_async_issue_pending(channel);

	return 0;
}

static void cryp_dma_done(struct cryp_ctx *ctx)
{
	struct dma_chan *chan;

	dev_dbg(ctx->device->dev, "[%s]: ", __func__);

	chan = ctx->device->dma.chan_mem2cryp;
	dmaengine_terminate_all(chan);
	dma_unmap_sg(chan->device->dev, ctx->device->dma.sg_src,
		     ctx->device->dma.nents_src, DMA_TO_DEVICE);

	chan = ctx->device->dma.chan_cryp2mem;
	dmaengine_terminate_all(chan);
	dma_unmap_sg(chan->device->dev, ctx->device->dma.sg_dst,
		     ctx->device->dma.nents_dst, DMA_FROM_DEVICE);
}

static int cryp_dma_write(struct cryp_ctx *ctx, struct scatterlist *sg,
			  int len)
{
	int error = cryp_set_dma_transfer(ctx, sg, len, DMA_TO_DEVICE);
	dev_dbg(ctx->device->dev, "[%s]: ", __func__);

	if (error) {
		dev_dbg(ctx->device->dev, "[%s]: cryp_set_dma_transfer() "
			"failed", __func__);
		return error;
	}

	return len;
}

static int cryp_dma_read(struct cryp_ctx *ctx, struct scatterlist *sg, int len)
{
	int error = cryp_set_dma_transfer(ctx, sg, len, DMA_FROM_DEVICE);
	if (error) {
		dev_dbg(ctx->device->dev, "[%s]: cryp_set_dma_transfer() "
			"failed", __func__);
		return error;
	}

	return len;
}

static void cryp_polling_mode(struct cryp_ctx *ctx,
			      struct cryp_device_data *device_data)
{
	int len = ctx->blocksize / BYTES_PER_WORD;
	int remaining_length = ctx->datalen;
	u32 *indata = (u32 *)ctx->indata;
	u32 *outdata = (u32 *)ctx->outdata;

	while (remaining_length > 0) {
		writesl(&device_data->base->din, indata, len);
		indata += len;
		remaining_length -= (len * BYTES_PER_WORD);
		cryp_wait_until_done(device_data);

		readsl(&device_data->base->dout, outdata, len);
		outdata += len;
		cryp_wait_until_done(device_data);
	}
}

static int cryp_disable_power(struct device *dev,
			      struct cryp_device_data *device_data,
			      bool save_device_context)
{
	int ret = 0;

	dev_dbg(dev, "[%s]", __func__);

	spin_lock(&device_data->power_state_spinlock);
	if (!device_data->power_state)
		goto out;

	spin_lock(&device_data->ctx_lock);
	if (save_device_context && device_data->current_ctx) {
		cryp_save_device_context(device_data,
				&device_data->current_ctx->dev_ctx,
				cryp_mode);
		device_data->restore_dev_ctx = true;
	}
	spin_unlock(&device_data->ctx_lock);

	clk_disable(device_data->clk);
	ret = regulator_disable(device_data->pwr_regulator);
	if (ret)
		dev_err(dev, "[%s]: "
				"regulator_disable() failed!",
				__func__);

	device_data->power_state = false;

out:
	spin_unlock(&device_data->power_state_spinlock);

	return ret;
}

static int cryp_enable_power(
		struct device *dev,
		struct cryp_device_data *device_data,
		bool restore_device_context)
{
	int ret = 0;

	dev_dbg(dev, "[%s]", __func__);

	spin_lock(&device_data->power_state_spinlock);
	if (!device_data->power_state) {
		ret = regulator_enable(device_data->pwr_regulator);
		if (ret) {
			dev_err(dev, "[%s]: regulator_enable() failed!",
					__func__);
			goto out;
		}

		ret = clk_enable(device_data->clk);
		if (ret) {
			dev_err(dev, "[%s]: clk_enable() failed!",
					__func__);
			regulator_disable(device_data->pwr_regulator);
			goto out;
		}
		device_data->power_state = true;
	}

	if (device_data->restore_dev_ctx) {
		spin_lock(&device_data->ctx_lock);
		if (restore_device_context && device_data->current_ctx) {
			device_data->restore_dev_ctx = false;
			cryp_restore_device_context(device_data,
					&device_data->current_ctx->dev_ctx);
		}
		spin_unlock(&device_data->ctx_lock);
	}
out:
	spin_unlock(&device_data->power_state_spinlock);

	return ret;
}

static int hw_crypt_noxts(struct cryp_ctx *ctx,
			  struct cryp_device_data *device_data)
{
	int ret = 0;

	const u8 *indata = ctx->indata;
	u8 *outdata = ctx->outdata;
	u32 datalen = ctx->datalen;
	u32 outlen = datalen;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	ctx->outlen = ctx->datalen;

	if (unlikely(!IS_ALIGNED((unsigned long)indata, 4))) {
		pr_debug(DEV_DBG_NAME " [%s]: Data isn't aligned! Addr: "
			 "0x%08lx", __func__, (unsigned long)indata);
		return -EINVAL;
	}

	ret = cryp_setup_context(ctx, device_data);

	if (ret)
		goto out;

	if (cryp_mode == CRYP_MODE_INTERRUPT) {
		cryp_enable_irq_src(device_data, CRYP_IRQ_SRC_INPUT_FIFO |
				    CRYP_IRQ_SRC_OUTPUT_FIFO);

		/*
		 * ctx->outlen is decremented in the cryp_interrupt_handler
		 * function. We had to add cpu_relax() (barrier) to make sure
		 * that gcc didn't optimze away this variable.
		 */
		while (ctx->outlen > 0)
			cpu_relax();
	} else if (cryp_mode == CRYP_MODE_POLLING ||
		   cryp_mode == CRYP_MODE_DMA) {
		/*
		 * The reason for having DMA in this if case is that if we are
		 * running cryp_mode = 2, then we separate DMA routines for
		 * handling cipher/plaintext > blocksize, except when
		 * running the normal CRYPTO_ALG_TYPE_CIPHER, then we still use
		 * the polling mode. Overhead of doing DMA setup eats up the
		 * benefits using it.
		 */
		cryp_polling_mode(ctx, device_data);
	} else {
		dev_err(ctx->device->dev, "[%s]: Invalid operation mode!",
			__func__);
		ret = -EPERM;
		goto out;
	}

	cryp_save_device_context(device_data, &ctx->dev_ctx, cryp_mode);
	ctx->updated = 1;

out:
	ctx->indata = indata;
	ctx->outdata = outdata;
	ctx->datalen = datalen;
	ctx->outlen = outlen;

	return ret;
}

static int get_nents(struct scatterlist *sg, int nbytes)
{
	int nents = 0;

	while (nbytes > 0) {
		nbytes -= sg->length;
		sg = sg_next(sg);
		nents++;
	}

	return nents;
}

static int ablk_dma_crypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(areq);
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct cryp_device_data *device_data;

	int bytes_written = 0;
	int bytes_read = 0;
	int ret;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	ctx->datalen = areq->cryptlen;
	ctx->outlen = areq->cryptlen;

	ret = cryp_get_device_data(ctx, &device_data);
	if (ret)
		return ret;

	ret = cryp_setup_context(ctx, device_data);
	if (ret)
		goto out;

	/* We have the device now, so store the nents in the dma struct. */
	ctx->device->dma.nents_src = get_nents(areq->src, ctx->datalen);
	ctx->device->dma.nents_dst = get_nents(areq->dst, ctx->outlen);

	/* Enable DMA in- and output. */
	cryp_configure_for_dma(device_data, CRYP_DMA_ENABLE_BOTH_DIRECTIONS);

	bytes_written = cryp_dma_write(ctx, areq->src, ctx->datalen);
	bytes_read = cryp_dma_read(ctx, areq->dst, bytes_written);

	wait_for_completion(&ctx->device->dma.cryp_dma_complete);
	cryp_dma_done(ctx);

	cryp_save_device_context(device_data, &ctx->dev_ctx, cryp_mode);
	ctx->updated = 1;

out:
	spin_lock(&device_data->ctx_lock);
	device_data->current_ctx = NULL;
	ctx->device = NULL;
	spin_unlock(&device_data->ctx_lock);

	/*
	 * The down_interruptible part for this semaphore is called in
	 * cryp_get_device_data.
	 */
	up(&driver_data.device_allocation);

	if (unlikely(bytes_written != bytes_read))
		return -EPERM;

	return 0;
}

static int ablk_crypt(struct skcipher_request *areq)
{
	struct skcipher_walk walk;
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(areq);
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);
	struct cryp_device_data *device_data;
	unsigned long src_paddr;
	unsigned long dst_paddr;
	int ret;
	int nbytes;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	ret = cryp_get_device_data(ctx, &device_data);
	if (ret)
		goto out;

	ret = skcipher_walk_async(&walk, areq);

	if (ret) {
		pr_err(DEV_DBG_NAME "[%s]: skcipher_walk_async() failed!",
			__func__);
		goto out;
	}

	while ((nbytes = walk.nbytes) > 0) {
		ctx->iv = walk.iv;
		src_paddr = (page_to_phys(walk.src.phys.page) + walk.src.phys.offset);
		ctx->indata = phys_to_virt(src_paddr);

		dst_paddr = (page_to_phys(walk.dst.phys.page) + walk.dst.phys.offset);
		ctx->outdata = phys_to_virt(dst_paddr);

		ctx->datalen = nbytes - (nbytes % ctx->blocksize);

		ret = hw_crypt_noxts(ctx, device_data);
		if (ret)
			goto out;

		nbytes -= ctx->datalen;
		ret = skcipher_walk_done(&walk, nbytes);
		if (ret)
			goto out;
	}

out:
	/* Release the device */
	spin_lock(&device_data->ctx_lock);
	device_data->current_ctx = NULL;
	ctx->device = NULL;
	spin_unlock(&device_data->ctx_lock);

	/*
	 * The down_interruptible part for this semaphore is called in
	 * cryp_get_device_data.
	 */
	up(&driver_data.device_allocation);

	return ret;
}

static int aes_skcipher_setkey(struct crypto_skcipher *cipher,
				 const u8 *key, unsigned int keylen)
{
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->config.keysize = CRYP_KEY_SIZE_128;
		break;

	case AES_KEYSIZE_192:
		ctx->config.keysize = CRYP_KEY_SIZE_192;
		break;

	case AES_KEYSIZE_256:
		ctx->config.keysize = CRYP_KEY_SIZE_256;
		break;

	default:
		pr_err(DEV_DBG_NAME "[%s]: Unknown keylen!", __func__);
		return -EINVAL;
	}

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	ctx->updated = 0;

	return 0;
}

static int des_skcipher_setkey(struct crypto_skcipher *cipher,
				 const u8 *key, unsigned int keylen)
{
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);
	int err;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	err = verify_skcipher_des_key(cipher, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	ctx->updated = 0;
	return 0;
}

static int des3_skcipher_setkey(struct crypto_skcipher *cipher,
				  const u8 *key, unsigned int keylen)
{
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);
	int err;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	err = verify_skcipher_des3_key(cipher, key);
	if (err)
		return err;

	memcpy(ctx->key, key, keylen);
	ctx->keylen = keylen;

	ctx->updated = 0;
	return 0;
}

static int cryp_blk_encrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(areq);
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	ctx->config.algodir = CRYP_ALGORITHM_ENCRYPT;

	/*
	 * DMA does not work for DES due to a hw bug */
	if (cryp_mode == CRYP_MODE_DMA && mode_is_aes(ctx->config.algomode))
		return ablk_dma_crypt(areq);

	/* For everything except DMA, we run the non DMA version. */
	return ablk_crypt(areq);
}

static int cryp_blk_decrypt(struct skcipher_request *areq)
{
	struct crypto_skcipher *cipher = crypto_skcipher_reqtfm(areq);
	struct cryp_ctx *ctx = crypto_skcipher_ctx(cipher);

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	ctx->config.algodir = CRYP_ALGORITHM_DECRYPT;

	/* DMA does not work for DES due to a hw bug */
	if (cryp_mode == CRYP_MODE_DMA && mode_is_aes(ctx->config.algomode))
		return ablk_dma_crypt(areq);

	/* For everything except DMA, we run the non DMA version. */
	return ablk_crypt(areq);
}

struct cryp_algo_template {
	enum cryp_algo_mode algomode;
	struct skcipher_alg skcipher;
};

static int cryp_init_tfm(struct crypto_skcipher *tfm)
{
	struct cryp_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_alg *alg = crypto_skcipher_alg(tfm);
	struct cryp_algo_template *cryp_alg = container_of(alg,
			struct cryp_algo_template,
			skcipher);

	ctx->config.algomode = cryp_alg->algomode;
	ctx->blocksize = crypto_skcipher_blocksize(tfm);

	return 0;
}

static struct cryp_algo_template cryp_algs[] = {
	{
		.algomode = CRYP_ALGO_AES_ECB,
		.skcipher = {
			.base.cra_name		= "ecb(aes)",
			.base.cra_driver_name	= "ecb-aes-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= AES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.setkey			= aes_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.init			= cryp_init_tfm,
		}
	},
	{
		.algomode = CRYP_ALGO_AES_CBC,
		.skcipher = {
			.base.cra_name		= "cbc(aes)",
			.base.cra_driver_name	= "cbc-aes-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= AES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.setkey			= aes_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.init			= cryp_init_tfm,
			.ivsize			= AES_BLOCK_SIZE,
		}
	},
	{
		.algomode = CRYP_ALGO_AES_CTR,
		.skcipher = {
			.base.cra_name		= "ctr(aes)",
			.base.cra_driver_name	= "ctr-aes-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= 1,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= AES_MIN_KEY_SIZE,
			.max_keysize		= AES_MAX_KEY_SIZE,
			.setkey			= aes_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.init			= cryp_init_tfm,
			.ivsize			= AES_BLOCK_SIZE,
			.chunksize		= AES_BLOCK_SIZE,
		}
	},
	{
		.algomode = CRYP_ALGO_DES_ECB,
		.skcipher = {
			.base.cra_name		= "ecb(des)",
			.base.cra_driver_name	= "ecb-des-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= DES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= DES_KEY_SIZE,
			.max_keysize		= DES_KEY_SIZE,
			.setkey			= des_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.init			= cryp_init_tfm,
		}
	},
	{
		.algomode = CRYP_ALGO_TDES_ECB,
		.skcipher = {
			.base.cra_name		= "ecb(des3_ede)",
			.base.cra_driver_name	= "ecb-des3_ede-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= DES3_EDE_KEY_SIZE,
			.max_keysize		= DES3_EDE_KEY_SIZE,
			.setkey			= des3_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.init			= cryp_init_tfm,
		}
	},
	{
		.algomode = CRYP_ALGO_DES_CBC,
		.skcipher = {
			.base.cra_name		= "cbc(des)",
			.base.cra_driver_name	= "cbc-des-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= DES_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= DES_KEY_SIZE,
			.max_keysize		= DES_KEY_SIZE,
			.setkey			= des_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.ivsize			= DES_BLOCK_SIZE,
			.init			= cryp_init_tfm,
		}
	},
	{
		.algomode = CRYP_ALGO_TDES_CBC,
		.skcipher = {
			.base.cra_name		= "cbc(des3_ede)",
			.base.cra_driver_name	= "cbc-des3_ede-ux500",
			.base.cra_priority	= 300,
			.base.cra_flags		= CRYPTO_ALG_ASYNC,
			.base.cra_blocksize	= DES3_EDE_BLOCK_SIZE,
			.base.cra_ctxsize	= sizeof(struct cryp_ctx),
			.base.cra_alignmask	= 3,
			.base.cra_module	= THIS_MODULE,

			.min_keysize		= DES3_EDE_KEY_SIZE,
			.max_keysize		= DES3_EDE_KEY_SIZE,
			.setkey			= des3_skcipher_setkey,
			.encrypt		= cryp_blk_encrypt,
			.decrypt		= cryp_blk_decrypt,
			.ivsize			= DES3_EDE_BLOCK_SIZE,
			.init			= cryp_init_tfm,
		}
	}
};

/**
 * cryp_algs_register_all -
 */
static int cryp_algs_register_all(void)
{
	int ret;
	int i;
	int count;

	pr_debug("[%s]", __func__);

	for (i = 0; i < ARRAY_SIZE(cryp_algs); i++) {
		ret = crypto_register_skcipher(&cryp_algs[i].skcipher);
		if (ret) {
			count = i;
			pr_err("[%s] alg registration failed",
					cryp_algs[i].skcipher.base.cra_driver_name);
			goto unreg;
		}
	}
	return 0;
unreg:
	for (i = 0; i < count; i++)
		crypto_unregister_skcipher(&cryp_algs[i].skcipher);
	return ret;
}

/**
 * cryp_algs_unregister_all -
 */
static void cryp_algs_unregister_all(void)
{
	int i;

	pr_debug(DEV_DBG_NAME " [%s]", __func__);

	for (i = 0; i < ARRAY_SIZE(cryp_algs); i++)
		crypto_unregister_skcipher(&cryp_algs[i].skcipher);
}

static int ux500_cryp_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct cryp_device_data *device_data;
	struct cryp_protection_config prot = {
		.privilege_access = CRYP_STATE_ENABLE
	};
	struct device *dev = &pdev->dev;

	dev_dbg(dev, "[%s]", __func__);
	device_data = devm_kzalloc(dev, sizeof(*device_data), GFP_ATOMIC);
	if (!device_data) {
		ret = -ENOMEM;
		goto out;
	}

	device_data->dev = dev;
	device_data->current_ctx = NULL;

	/* Grab the DMA configuration from platform data. */
	mem_to_engine = &((struct cryp_platform_data *)
			 dev->platform_data)->mem_to_engine;
	engine_to_mem = &((struct cryp_platform_data *)
			 dev->platform_data)->engine_to_mem;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "[%s]: platform_get_resource() failed",
				__func__);
		ret = -ENODEV;
		goto out;
	}

	device_data->phybase = res->start;
	device_data->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(device_data->base)) {
		ret = PTR_ERR(device_data->base);
		goto out;
	}

	spin_lock_init(&device_data->ctx_lock);
	spin_lock_init(&device_data->power_state_spinlock);

	/* Enable power for CRYP hardware block */
	device_data->pwr_regulator = regulator_get(&pdev->dev, "v-ape");
	if (IS_ERR(device_data->pwr_regulator)) {
		dev_err(dev, "[%s]: could not get cryp regulator", __func__);
		ret = PTR_ERR(device_data->pwr_regulator);
		device_data->pwr_regulator = NULL;
		goto out;
	}

	/* Enable the clk for CRYP hardware block */
	device_data->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(device_data->clk)) {
		dev_err(dev, "[%s]: clk_get() failed!", __func__);
		ret = PTR_ERR(device_data->clk);
		goto out_regulator;
	}

	ret = clk_prepare(device_data->clk);
	if (ret) {
		dev_err(dev, "[%s]: clk_prepare() failed!", __func__);
		goto out_regulator;
	}

	/* Enable device power (and clock) */
	ret = cryp_enable_power(device_data->dev, device_data, false);
	if (ret) {
		dev_err(dev, "[%s]: cryp_enable_power() failed!", __func__);
		goto out_clk_unprepare;
	}

	if (cryp_check(device_data)) {
		dev_err(dev, "[%s]: cryp_check() failed!", __func__);
		ret = -EINVAL;
		goto out_power;
	}

	if (cryp_configure_protection(device_data, &prot)) {
		dev_err(dev, "[%s]: cryp_configure_protection() failed!",
			__func__);
		ret = -EINVAL;
		goto out_power;
	}

	device_data->irq = platform_get_irq(pdev, 0);
	if (device_data->irq <= 0) {
		ret = device_data->irq ? device_data->irq : -ENXIO;
		goto out_power;
	}

	ret = devm_request_irq(&pdev->dev, device_data->irq,
			       cryp_interrupt_handler, 0, "cryp1", device_data);
	if (ret) {
		dev_err(dev, "[%s]: Unable to request IRQ", __func__);
		goto out_power;
	}

	if (cryp_mode == CRYP_MODE_DMA)
		cryp_dma_setup_channel(device_data, dev);

	platform_set_drvdata(pdev, device_data);

	/* Put the new device into the device list... */
	klist_add_tail(&device_data->list_node, &driver_data.device_list);

	/* ... and signal that a new device is available. */
	up(&driver_data.device_allocation);

	atomic_set(&session_id, 1);

	ret = cryp_algs_register_all();
	if (ret) {
		dev_err(dev, "[%s]: cryp_algs_register_all() failed!",
			__func__);
		goto out_power;
	}

	dev_info(dev, "successfully registered\n");

	return 0;

out_power:
	cryp_disable_power(device_data->dev, device_data, false);

out_clk_unprepare:
	clk_unprepare(device_data->clk);

out_regulator:
	regulator_put(device_data->pwr_regulator);

out:
	return ret;
}

static int ux500_cryp_remove(struct platform_device *pdev)
{
	struct cryp_device_data *device_data;

	dev_dbg(&pdev->dev, "[%s]", __func__);
	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(&pdev->dev, "[%s]: platform_get_drvdata() failed!",
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
		cryp_algs_unregister_all();

	if (cryp_disable_power(&pdev->dev, device_data, false))
		dev_err(&pdev->dev, "[%s]: cryp_disable_power() failed",
			__func__);

	clk_unprepare(device_data->clk);
	regulator_put(device_data->pwr_regulator);

	return 0;
}

static void ux500_cryp_shutdown(struct platform_device *pdev)
{
	struct cryp_device_data *device_data;

	dev_dbg(&pdev->dev, "[%s]", __func__);

	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(&pdev->dev, "[%s]: platform_get_drvdata() failed!",
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
		cryp_algs_unregister_all();

	if (cryp_disable_power(&pdev->dev, device_data, false))
		dev_err(&pdev->dev, "[%s]: cryp_disable_power() failed",
			__func__);

}

#ifdef CONFIG_PM_SLEEP
static int ux500_cryp_suspend(struct device *dev)
{
	int ret;
	struct platform_device *pdev = to_platform_device(dev);
	struct cryp_device_data *device_data;
	struct cryp_ctx *temp_ctx = NULL;

	dev_dbg(dev, "[%s]", __func__);

	/* Handle state? */
	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(dev, "[%s]: platform_get_drvdata() failed!", __func__);
		return -ENOMEM;
	}

	disable_irq(device_data->irq);

	spin_lock(&device_data->ctx_lock);
	if (!device_data->current_ctx)
		device_data->current_ctx++;
	spin_unlock(&device_data->ctx_lock);

	if (device_data->current_ctx == ++temp_ctx) {
		if (down_interruptible(&driver_data.device_allocation))
			dev_dbg(dev, "[%s]: down_interruptible() failed",
				__func__);
		ret = cryp_disable_power(dev, device_data, false);

	} else
		ret = cryp_disable_power(dev, device_data, true);

	if (ret)
		dev_err(dev, "[%s]: cryp_disable_power()", __func__);

	return ret;
}

static int ux500_cryp_resume(struct device *dev)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(dev);
	struct cryp_device_data *device_data;
	struct cryp_ctx *temp_ctx = NULL;

	dev_dbg(dev, "[%s]", __func__);

	device_data = platform_get_drvdata(pdev);
	if (!device_data) {
		dev_err(dev, "[%s]: platform_get_drvdata() failed!", __func__);
		return -ENOMEM;
	}

	spin_lock(&device_data->ctx_lock);
	if (device_data->current_ctx == ++temp_ctx)
		device_data->current_ctx = NULL;
	spin_unlock(&device_data->ctx_lock);


	if (!device_data->current_ctx)
		up(&driver_data.device_allocation);
	else
		ret = cryp_enable_power(dev, device_data, true);

	if (ret)
		dev_err(dev, "[%s]: cryp_enable_power() failed!", __func__);
	else
		enable_irq(device_data->irq);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(ux500_cryp_pm, ux500_cryp_suspend, ux500_cryp_resume);

static const struct of_device_id ux500_cryp_match[] = {
	{ .compatible = "stericsson,ux500-cryp" },
	{ },
};
MODULE_DEVICE_TABLE(of, ux500_cryp_match);

static struct platform_driver cryp_driver = {
	.probe  = ux500_cryp_probe,
	.remove = ux500_cryp_remove,
	.shutdown = ux500_cryp_shutdown,
	.driver = {
		.name  = "cryp1",
		.of_match_table = ux500_cryp_match,
		.pm    = &ux500_cryp_pm,
	}
};

static int __init ux500_cryp_mod_init(void)
{
	pr_debug("[%s] is called!", __func__);
	klist_init(&driver_data.device_list, NULL, NULL);
	/* Initialize the semaphore to 0 devices (locked state) */
	sema_init(&driver_data.device_allocation, 0);
	return platform_driver_register(&cryp_driver);
}

static void __exit ux500_cryp_mod_fini(void)
{
	pr_debug("[%s] is called!", __func__);
	platform_driver_unregister(&cryp_driver);
}

module_init(ux500_cryp_mod_init);
module_exit(ux500_cryp_mod_fini);

module_param(cryp_mode, int, 0);

MODULE_DESCRIPTION("Driver for ST-Ericsson UX500 CRYP crypto engine.");
MODULE_ALIAS_CRYPTO("aes-all");
MODULE_ALIAS_CRYPTO("des-all");

MODULE_LICENSE("GPL");
