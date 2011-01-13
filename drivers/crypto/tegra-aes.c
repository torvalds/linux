/*
 * drivers/crypto/tegra-aes.c
 *
 * aes driver for NVIDIA tegra aes hardware
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
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

#include <mach/arb_sema.h>
#include <mach/clk.h>

#include <crypto/scatterwalk.h>
#include <crypto/aes.h>
#include <crypto/internal/rng.h>

#include "tegra-aes.h"

#define FLAGS_MODE_MASK		0x000f
#define FLAGS_ENCRYPT		BIT(0)
#define FLAGS_CBC		BIT(1)
#define FLAGS_GIV		BIT(2)
#define FLAGS_RNG		BIT(3)
#define FLAGS_NEW_KEY		BIT(4)
#define FLAGS_NEW_IV		BIT(5)
#define FLAGS_INIT		BIT(6)
#define FLAGS_FAST		BIT(7)
#define FLAGS_BUSY		8

/*
 * Defines AES engine Max process bytes size in one go, which takes 1 msec.
 * AES engine spends about 176 cycles/16-bytes or 11 cycles/byte
 * The duration CPU can use the BSE to 1 msec, then the number of available
 * cycles of AVP/BSE is 216K. In this duration, AES can process 216/11 ~= 19KB
 * Based on this AES_HW_DMA_BUFFER_SIZE_BYTES is configured to 16KB.
 */
#define AES_HW_DMA_BUFFER_SIZE_BYTES 0x4000

/*
 * The key table length is 64 bytes
 * (This includes first upto 32 bytes key + 16 bytes original initial vector
 * and 16 bytes updated initial vector)
 */
#define AES_HW_KEY_TABLE_LENGTH_BYTES 64

#define AES_HW_IV_SIZE 16
#define AES_HW_KEYSCHEDULE_LEN 256
#define ARB_SEMA_TIMEOUT 500

/*
 * The memory being used is divides as follows:
 * 1. Key - 32 bytes
 * 2. Original IV - 16 bytes
 * 3. Updated IV - 16 bytes
 * 4. Key schedule - 256 bytes
 *
 * 1+2+3 constitute the hw key table.
 */
#define AES_IVKEY_SIZE (AES_HW_KEY_TABLE_LENGTH_BYTES + AES_HW_KEYSCHEDULE_LEN)

#define DEFAULT_RNG_BLK_SZ 16

/* As of now only 5 commands are USED for AES encryption/Decryption */
#define AES_HW_MAX_ICQ_LENGTH 5

#define ICQBITSHIFT_BLKCNT 0

/* memdma_vd command */
#define MEMDMA_DIR_DTOVRAM	0
#define MEMDMA_DIR_VTODRAM	1
#define MEMDMABITSHIFT_DIR	25
#define MEMDMABITSHIFT_NUM_WORDS	12

/* Define AES Interactive command Queue commands Bit positions */
enum {
	ICQBITSHIFT_KEYTABLEADDR = 0,
	ICQBITSHIFT_KEYTABLEID = 17,
	ICQBITSHIFT_VRAMSEL = 23,
	ICQBITSHIFT_TABLESEL = 24,
	ICQBITSHIFT_OPCODE = 26,
};

/* Define Ucq opcodes required for AES operation */
enum {
	UCQOPCODE_BLKSTARTENGINE = 0x0E,
	UCQOPCODE_DMASETUP = 0x10,
	UCQOPCODE_DMACOMPLETE = 0x11,
	UCQOPCODE_SETTABLE = 0x15,
	UCQOPCODE_MEMDMAVD = 0x22,
};

/* Define Aes command values */
enum {
	UCQCMD_VRAM_SEL = 0x1,
	UCQCMD_CRYPTO_TABLESEL = 0x3,
	UCQCMD_KEYSCHEDTABLESEL = 0x4,
	UCQCMD_KEYTABLESEL = 0x8,
};

#define UCQCMD_KEYTABLEADDRMASK 0x1FFFF

#define AES_NR_KEYSLOTS	8
#define SSK_SLOT_NUM	4

struct tegra_aes_slot {
	struct list_head node;
	int slot_num;
	bool available;
};

static struct tegra_aes_slot ssk = {
	.slot_num = SSK_SLOT_NUM,
	.available = true,
};

struct tegra_aes_reqctx {
	unsigned long mode;
};

#define TEGRA_AES_QUEUE_LENGTH 50

struct tegra_aes_dev {
	struct device *dev;
	unsigned long phys_base;
	void __iomem *io_base;
	dma_addr_t ivkey_phys_base;
	void __iomem *ivkey_base;
	struct clk *iclk;
	struct clk *pclk;
	struct tegra_aes_ctx *ctx;
	unsigned long flags;
	struct completion op_complete;
	u32 *buf_in;
	dma_addr_t dma_buf_in;
	u32 *buf_out;
	dma_addr_t dma_buf_out;
	u8 *iv;
	u8 dt[DEFAULT_RNG_BLK_SZ];
	int ivlen;
	u64 ctr;
	int res_id;
	spinlock_t lock;
	struct crypto_queue queue;
	struct tegra_aes_slot *slots;
	struct ablkcipher_request *req;
	size_t total;
	struct scatterlist *in_sg;
	size_t in_offset;
	struct scatterlist *out_sg;
	size_t out_offset;
};

static struct tegra_aes_dev *aes_dev;

struct tegra_aes_ctx {
	struct tegra_aes_dev *dd;
	unsigned long flags;
	struct tegra_aes_slot *slot;
	int keylen;
};

static struct tegra_aes_ctx rng_ctx = {
	.flags = FLAGS_NEW_KEY,
	.keylen = AES_KEYSIZE_128,
};

/* keep registered devices data here */
static LIST_HEAD(dev_list);
static DEFINE_SPINLOCK(list_lock);
static DEFINE_MUTEX(aes_lock);

static void aes_workqueue_handler(struct work_struct *work);
static DECLARE_WORK(aes_work, aes_workqueue_handler);
static struct workqueue_struct *aes_wq;

extern unsigned long long tegra_chip_uid(void);

static inline u32 aes_readl(struct tegra_aes_dev *dd, u32 offset)
{
	return readl(dd->io_base + offset);
}

static inline void aes_writel(struct tegra_aes_dev *dd, u32 val, u32 offset)
{
	writel(val, dd->io_base + offset);
}

static int aes_hw_init(struct tegra_aes_dev *dd)
{
	int ret = 0;

	ret = clk_enable(dd->pclk);
	if (ret < 0) {
		dev_err(dd->dev, "%s: pclock enable fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = clk_enable(dd->iclk);
	if (ret < 0) {
		dev_err(dd->dev, "%s: iclock enable fail(%d)\n", __func__, ret);
		clk_disable(dd->pclk);
		return ret;
	}

	ret = clk_set_rate(dd->iclk, 240000000);
	if (ret) {
		dev_err(dd->dev, "%s: iclk set_rate fail(%d)\n", __func__, ret);
		clk_disable(dd->iclk);
		clk_disable(dd->pclk);
		return ret;
	}

	aes_writel(dd, 0x33, INT_ENB);
	return ret;
}

static void aes_hw_deinit(struct tegra_aes_dev *dd)
{
	clk_disable(dd->iclk);
	clk_disable(dd->pclk);
}

static int aes_start_crypt(struct tegra_aes_dev *dd, u32 in_addr, u32 out_addr,
	int nblocks, int mode, bool upd_iv)
{
	u32 cmdq[AES_HW_MAX_ICQ_LENGTH];
	int qlen = 0, i, eng_busy, icq_empty, dma_busy, ret = 0;
	u32 value;

	cmdq[qlen++] = UCQOPCODE_DMASETUP << ICQBITSHIFT_OPCODE;
	cmdq[qlen++] = in_addr;
	cmdq[qlen++] = UCQOPCODE_BLKSTARTENGINE << ICQBITSHIFT_OPCODE |
		(nblocks-1) << ICQBITSHIFT_BLKCNT;
	cmdq[qlen++] = UCQOPCODE_DMACOMPLETE << ICQBITSHIFT_OPCODE;

	value = aes_readl(dd, CMDQUE_CONTROL);
	/* access SDRAM through AHB */
	value &= ~CMDQ_CTRL_SRC_STM_SEL_FIELD;
	value &= ~CMDQ_CTRL_DST_STM_SEL_FIELD;
	value |= (CMDQ_CTRL_SRC_STM_SEL_FIELD | CMDQ_CTRL_DST_STM_SEL_FIELD |
		CMDQ_CTRL_ICMDQEN_FIELD);
	aes_writel(dd, value, CMDQUE_CONTROL);
	dev_dbg(dd->dev, "cmd_q_ctrl=0x%x", value);

	value = 0;
	value |= CONFIG_ENDIAN_ENB_FIELD;
	aes_writel(dd, value, CONFIG);
	dev_dbg(dd->dev, "config=0x%x", value);

	value = aes_readl(dd, SECURE_CONFIG_EXT);
	value &= ~SECURE_OFFSET_CNT_FIELD;
	aes_writel(dd, value, SECURE_CONFIG_EXT);
	dev_dbg(dd->dev, "secure_cfg_xt=0x%x", value);

	if (mode & FLAGS_CBC) {
		value = ((0x1 << SECURE_INPUT_ALG_SEL_SHIFT) |
			((dd->ctx->keylen * 8) << SECURE_INPUT_KEY_LEN_SHIFT) |
			((u32)upd_iv << SECURE_IV_SELECT_SHIFT) |
			(((mode & FLAGS_ENCRYPT) ? 2 : 3)
				<< SECURE_XOR_POS_SHIFT) |
			(0 << SECURE_INPUT_SEL_SHIFT) |
			(((mode & FLAGS_ENCRYPT) ? 2 : 3)
				<< SECURE_VCTRAM_SEL_SHIFT) |
			((mode & FLAGS_ENCRYPT) ? 1 : 0)
				<< SECURE_CORE_SEL_SHIFT |
			(0 << SECURE_RNG_ENB_SHIFT) |
			(0 << SECURE_HASH_ENB_SHIFT));
	} else if (mode & FLAGS_RNG){
		value = ((0x1 << SECURE_INPUT_ALG_SEL_SHIFT) |
			((dd->ctx->keylen * 8) << SECURE_INPUT_KEY_LEN_SHIFT) |
			((u32)upd_iv << SECURE_IV_SELECT_SHIFT) |
			(0 << SECURE_XOR_POS_SHIFT) |
			(0 << SECURE_INPUT_SEL_SHIFT) |
			((mode & FLAGS_ENCRYPT) ? 1 : 0)
				<< SECURE_CORE_SEL_SHIFT |
			(1 << SECURE_RNG_ENB_SHIFT) |
			(0 << SECURE_HASH_ENB_SHIFT));
	} else {
		value = ((0x1 << SECURE_INPUT_ALG_SEL_SHIFT) |
			((dd->ctx->keylen * 8) << SECURE_INPUT_KEY_LEN_SHIFT) |
			((u32)upd_iv << SECURE_IV_SELECT_SHIFT) |
			(0 << SECURE_XOR_POS_SHIFT) |
			(0 << SECURE_INPUT_SEL_SHIFT) |
			(((mode & FLAGS_ENCRYPT) ? 1 : 0)
				<< SECURE_CORE_SEL_SHIFT) |
			(0 << SECURE_RNG_ENB_SHIFT) |
				(0 << SECURE_HASH_ENB_SHIFT));
	}
	dev_dbg(dd->dev, "secure_in_sel=0x%x", value);
	aes_writel(dd, value, SECURE_INPUT_SELECT);

	aes_writel(dd, out_addr, SECURE_DEST_ADDR);
	INIT_COMPLETION(dd->op_complete);

	for (i = 0; i < qlen - 1; i++) {
		do {
			value = aes_readl(dd, INTR_STATUS);
			eng_busy = value & (0x1);
			icq_empty = value & (0x1<<3);
			dma_busy = value & (0x1<<23);
		} while (eng_busy & (!icq_empty) & dma_busy);
		aes_writel(dd, cmdq[i], ICMDQUE_WR);
	}

	ret = wait_for_completion_timeout(&dd->op_complete, msecs_to_jiffies(150));
	if (ret == 0) {
		dev_err(dd->dev, "timed out (0x%x)\n",
			aes_readl(dd, INTR_STATUS));
		return -ETIMEDOUT;
	}

	aes_writel(dd, cmdq[qlen - 1], ICMDQUE_WR);
	do {
		value = aes_readl(dd, INTR_STATUS);
		eng_busy = value & (0x1);
		icq_empty = value & (0x1<<3);
		dma_busy = value & (0x1<<23);
	} while (eng_busy & (!icq_empty) & dma_busy);

	return 0;
}

static void aes_release_key_slot(struct tegra_aes_dev *dd)
{
	spin_lock(&list_lock);
	dd->ctx->slot->available = true;
	dd->ctx->slot = NULL;
	spin_unlock(&list_lock);
}

static struct tegra_aes_slot *aes_find_key_slot(struct tegra_aes_dev *dd)
{
	struct tegra_aes_slot *slot = NULL;
	bool found = false;

	spin_lock(&list_lock);
	list_for_each_entry(slot, &dev_list, node) {
		dev_dbg(dd->dev, "empty:%d, num:%d\n", slot->available,
			slot->slot_num);
		if (slot->available) {
			slot->available = false;
			found = true;
			break;
		}
	}
	spin_unlock(&list_lock);
	return found ? slot : NULL;
}

static int aes_set_key(struct tegra_aes_dev *dd)
{
	u32 value, cmdq[2];
	struct tegra_aes_ctx *ctx = dd->ctx;
	int i, eng_busy, icq_empty, dma_busy;
	bool use_ssk = false;

	if (!ctx) {
		dev_err(dd->dev, "%s: context invalid\n", __func__);
		return -EINVAL;
	}

	/* use ssk? */
	if (!dd->ctx->slot) {
		dev_dbg(dd->dev, "using ssk");
		dd->ctx->slot = &ssk;
		use_ssk = true;
	}

	/* disable key read from hw */
	value = aes_readl(dd, SECURE_SEC_SEL0+(ctx->slot->slot_num*4));
	value &= ~SECURE_SEL0_KEYREAD_ENB0_FIELD;
	aes_writel(dd, value, SECURE_SEC_SEL0+(ctx->slot->slot_num*4));

	/* enable key schedule generation in hardware */
	value = aes_readl(dd, SECURE_CONFIG_EXT);
	value &= ~SECURE_KEY_SCH_DIS_FIELD;
	aes_writel(dd, value, SECURE_CONFIG_EXT);

	/* select the key slot */
	value = aes_readl(dd, SECURE_CONFIG);
	value &= ~SECURE_KEY_INDEX_FIELD;
	value |= (ctx->slot->slot_num << SECURE_KEY_INDEX_SHIFT);
	aes_writel(dd, value, SECURE_CONFIG);

	if (use_ssk)
		goto out;

	/* copy the key table from sdram to vram */
	cmdq[0] = 0;
	cmdq[0] = UCQOPCODE_MEMDMAVD << ICQBITSHIFT_OPCODE |
		(MEMDMA_DIR_DTOVRAM << MEMDMABITSHIFT_DIR) |
		(AES_HW_KEY_TABLE_LENGTH_BYTES/sizeof(u32))
			<< MEMDMABITSHIFT_NUM_WORDS;
	cmdq[1] = (u32)dd->ivkey_phys_base;
	for (i = 0; i < ARRAY_SIZE(cmdq); i++) {
		aes_writel(dd, cmdq[i], ICMDQUE_WR);
		do {
			value = aes_readl(dd, INTR_STATUS);
			eng_busy = value & (0x1);
			icq_empty = value & (0x1<<3);
			dma_busy = value & (0x1<<23);
		} while (eng_busy & (!icq_empty) & dma_busy);
	}

	/* settable command to get key into internal registers */
	value = 0;
	value = UCQOPCODE_SETTABLE << ICQBITSHIFT_OPCODE |
		UCQCMD_CRYPTO_TABLESEL << ICQBITSHIFT_TABLESEL |
		UCQCMD_VRAM_SEL << ICQBITSHIFT_VRAMSEL |
		(UCQCMD_KEYTABLESEL | ctx->slot->slot_num)
			<< ICQBITSHIFT_KEYTABLEID;
	aes_writel(dd, value, ICMDQUE_WR);
	do {
		value = aes_readl(dd, INTR_STATUS);
		eng_busy = value & (0x1);
		icq_empty = value & (0x1<<3);
	} while (eng_busy & (!icq_empty));

out:
	return 0;
}

static int tegra_aes_handle_req(struct tegra_aes_dev *dd)
{
	struct crypto_async_request *async_req, *backlog;
	struct tegra_aes_ctx *ctx;
	struct tegra_aes_reqctx *rctx;
	struct ablkcipher_request *req;
	unsigned long flags;
	int dma_max = AES_HW_DMA_BUFFER_SIZE_BYTES;
	int ret = 0, nblocks, total;
	int count = 0;
	dma_addr_t addr_in, addr_out;
	struct scatterlist *in_sg, *out_sg;

	if (!dd)
		return -EINVAL;

	spin_lock_irqsave(&dd->lock, flags);
	backlog = crypto_get_backlog(&dd->queue);
	async_req = crypto_dequeue_request(&dd->queue);
	if (!async_req)
		clear_bit(FLAGS_BUSY, &dd->flags);
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!async_req)
		return -ENODATA;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ablkcipher_request_cast(async_req);

	dev_dbg(dd->dev, "%s: get new req\n", __func__);

	/* take mutex to access the aes hw */
	mutex_lock(&aes_lock);

	/* assign new request to device */
	dd->req = req;
	dd->total = req->nbytes;
	dd->in_offset = 0;
	dd->in_sg = req->src;
	dd->out_offset = 0;
	dd->out_sg = req->dst;

	in_sg = dd->in_sg;
	out_sg = dd->out_sg;

	if (!in_sg || !out_sg) {
		mutex_unlock(&aes_lock);
		return -EINVAL;
	}

	total = dd->total;
	rctx = ablkcipher_request_ctx(req);
	ctx = crypto_ablkcipher_ctx(crypto_ablkcipher_reqtfm(req));
	rctx->mode &= FLAGS_MODE_MASK;
	dd->flags = (dd->flags & ~FLAGS_MODE_MASK) | rctx->mode;

	dd->iv = (u8 *)req->info;
	dd->ivlen = AES_BLOCK_SIZE;

	if ((dd->flags & FLAGS_CBC) && dd->iv)
		dd->flags |= FLAGS_NEW_IV;
	else
		dd->flags &= ~FLAGS_NEW_IV;

	ctx->dd = dd;
	if (dd->ctx != ctx) {
		/* assign new context to device */
		dd->ctx = ctx;
		ctx->flags |= FLAGS_NEW_KEY;
	}

	/* take the hardware semaphore */
	if (tegra_arb_mutex_lock_timeout(dd->res_id, ARB_SEMA_TIMEOUT) < 0) {
		dev_err(dd->dev, "aes hardware not available\n");
		mutex_unlock(&aes_lock);
		return -EBUSY;
	}

	ret = aes_hw_init(dd);
	if (ret < 0) {
		dev_err(dd->dev, "%s: hw init fail(%d)\n", __func__, ret);
		goto fail;
	}

	aes_set_key(dd);

	/* set iv to the aes hw slot */
	memset(dd->buf_in, 0 , AES_BLOCK_SIZE);
	ret = copy_from_user((void *)dd->buf_in, (void __user *)dd->iv,
		dd->ivlen);
	if (ret < 0) {
		dev_err(dd->dev, "copy_from_user fail(%d)\n", ret);
		goto out;
	}

	ret = aes_start_crypt(dd, (u32)dd->dma_buf_in,
	  (u32)dd->dma_buf_out, 1, FLAGS_CBC, false);
	if (ret < 0) {
		dev_err(dd->dev, "aes_start_crypt fail(%d)\n", ret);
		goto out;
	}
	memset(dd->buf_in, 0, AES_BLOCK_SIZE);

	while (total) {
		dev_dbg(dd->dev, "remain: 0x%x\n", total);

		ret = dma_map_sg(dd->dev, in_sg, 1, DMA_TO_DEVICE);
		if (!ret) {
			dev_err(dd->dev, "dma_map_sg() error\n");
			goto out;
		}

		ret = dma_map_sg(dd->dev, out_sg, 1, DMA_FROM_DEVICE);
		if (!ret) {
				dev_err(dd->dev, "dma_map_sg() error\n");
				dma_unmap_sg(dd->dev, dd->in_sg,
					1, DMA_TO_DEVICE);
				goto out;
			}

		addr_in = sg_dma_address(in_sg);
		addr_out = sg_dma_address(out_sg);
		dd->flags |= FLAGS_FAST;
		count = min((int)sg_dma_len(in_sg), (int)dma_max);
		WARN_ON(sg_dma_len(in_sg) != sg_dma_len(out_sg));
		nblocks = DIV_ROUND_UP(count, AES_BLOCK_SIZE);

		ret = aes_start_crypt(dd, addr_in, addr_out, nblocks,
			dd->flags, true);

		dma_unmap_sg(dd->dev, out_sg, 1, DMA_FROM_DEVICE);
		dma_unmap_sg(dd->dev, in_sg, 1, DMA_TO_DEVICE);

		if (ret < 0) {
			dev_err(dd->dev, "aes_start_crypt fail(%d)\n", ret);
			goto out;
		}
		dd->flags &= ~FLAGS_FAST;

		dev_dbg(dd->dev, "out: copied 0x%x\n", count);
		total -= count;
		in_sg = sg_next(in_sg);
		out_sg = sg_next(out_sg);
		WARN_ON(((total != 0) && (!in_sg || !out_sg)));
	}

out:
	aes_hw_deinit(dd);

fail:
	/* release the hardware semaphore */
	tegra_arb_mutex_unlock(dd->res_id);

	dd->total = total;

	/* release the mutex */
	mutex_unlock(&aes_lock);

	if (dd->req->base.complete)
		dd->req->base.complete(&dd->req->base, ret);

	dev_dbg(dd->dev, "%s: exit\n", __func__);
	return ret;
}

static int tegra_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
	unsigned int keylen)
{
	struct tegra_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct tegra_aes_dev *dd = aes_dev;
	struct tegra_aes_slot *key_slot;

	if (!ctx || !dd) {
		dev_err(dd->dev, "ctx=0x%x, dd=0x%x\n",
			(unsigned int)ctx, (unsigned int)dd);
		return -EINVAL;
	}

	if ((keylen != AES_KEYSIZE_128) && (keylen != AES_KEYSIZE_192) &&
		(keylen != AES_KEYSIZE_256)) {
		dev_err(dd->dev, "unsupported key size\n");
		return -EINVAL;
	}

	dev_dbg(dd->dev, "keylen: %d\n", keylen);

	ctx->dd = dd;
	dd->ctx = ctx;

	if (ctx->slot)
		aes_release_key_slot(dd);

	key_slot = aes_find_key_slot(dd);
	if (!key_slot) {
		dev_err(dd->dev, "no empty slot\n");
		return -ENOMEM;
	}

	ctx->slot = key_slot;
	ctx->keylen = keylen;
	ctx->flags |= FLAGS_NEW_KEY;

	/* copy the key */
	memset(dd->ivkey_base, 0, AES_HW_KEY_TABLE_LENGTH_BYTES);
	memcpy(dd->ivkey_base, key, keylen);

	dev_dbg(dd->dev, "done\n");
	return 0;
}

static void aes_workqueue_handler(struct work_struct *work)
{
	struct tegra_aes_dev *dd = aes_dev;
	int ret;

	set_bit(FLAGS_BUSY, &dd->flags);

	do {
		ret = tegra_aes_handle_req(dd);
	} while (!ret);
}

static irqreturn_t aes_irq(int irq, void *dev_id)
{
	struct tegra_aes_dev *dd = (struct tegra_aes_dev *)dev_id;
	u32 value = aes_readl(dd, INTR_STATUS);

	dev_dbg(dd->dev, "irq_stat: 0x%x", value);
	if (!((value & ENGINE_BUSY_FIELD) & !(value & ICQ_EMPTY_FIELD)))
		complete(&dd->op_complete);

	return IRQ_HANDLED;
}

static int tegra_aes_crypt(struct ablkcipher_request *req, unsigned long mode)
{
	struct tegra_aes_reqctx *rctx = ablkcipher_request_ctx(req);
	struct tegra_aes_dev *dd = aes_dev;
	unsigned long flags;
	int err = 0;
	int busy;

	dev_dbg(dd->dev, "nbytes: %d, enc: %d, cbc: %d\n", req->nbytes,
		!!(mode & FLAGS_ENCRYPT),
		!!(mode & FLAGS_CBC));

	rctx->mode = mode;

	spin_lock_irqsave(&dd->lock, flags);
	err = ablkcipher_enqueue_request(&dd->queue, req);
	busy = test_and_set_bit(FLAGS_BUSY, &dd->flags);
	spin_unlock_irqrestore(&dd->lock, flags);

	if (!busy)
		queue_work(aes_wq, &aes_work);

	return err;
}

static int tegra_aes_ecb_encrypt(struct ablkcipher_request *req)
{
	return tegra_aes_crypt(req, FLAGS_ENCRYPT);
}

static int tegra_aes_ecb_decrypt(struct ablkcipher_request *req)
{
	return tegra_aes_crypt(req, 0);
}

static int tegra_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	return tegra_aes_crypt(req, FLAGS_ENCRYPT | FLAGS_CBC);
}

static int tegra_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	return tegra_aes_crypt(req, FLAGS_CBC);
}

static int tegra_aes_get_random(struct crypto_rng *tfm, u8 *rdata,
	unsigned int dlen)
{
	struct tegra_aes_dev *dd = aes_dev;
	struct tegra_aes_ctx *ctx = &rng_ctx;
	int ret, i;
	u8 *dest = rdata, *dt = dd->dt;

	/* take mutex to access the aes hw */
	mutex_lock(&aes_lock);

	/* take the hardware semaphore */
	if (tegra_arb_mutex_lock_timeout(dd->res_id, ARB_SEMA_TIMEOUT) < 0) {
		dev_err(dd->dev, "aes hardware not available\n");
		mutex_unlock(&aes_lock);
		return -EBUSY;
	}

	ret = aes_hw_init(dd);
	if (ret < 0) {
		dev_err(dd->dev, "%s: hw init fail(%d)\n", __func__, ret);
		dlen = ret;
		goto fail;
	}

	ctx->dd = dd;
	dd->ctx = ctx;
	dd->flags = FLAGS_ENCRYPT | FLAGS_RNG;

	memset(dd->buf_in, 0, AES_BLOCK_SIZE);
	memcpy(dd->buf_in, dt, DEFAULT_RNG_BLK_SZ);

	ret = aes_start_crypt(dd, (u32)dd->dma_buf_in,
		(u32)dd->dma_buf_out, 1, dd->flags, true);
	if (ret < 0) {
		dev_err(dd->dev, "aes_start_crypt fail(%d)\n", ret);
		dlen = ret;
		goto out;
	}
	memcpy(dest, dd->buf_out, dlen);

	/* update the DT */
	for (i = DEFAULT_RNG_BLK_SZ - 1; i >= 0; i--) {
		dt[i] += 1;
		if (dt[i] != 0)
			break;
	}

out:
	aes_hw_deinit(dd);

fail:
	/* release the hardware semaphore */
	tegra_arb_mutex_unlock(dd->res_id);
	mutex_unlock(&aes_lock);
	dev_dbg(dd->dev, "%s: done\n", __func__);
	return dlen;
}

static int tegra_aes_rng_reset(struct crypto_rng *tfm, u8 *seed,
	unsigned int slen)
{
	struct tegra_aes_dev *dd = aes_dev;
	struct tegra_aes_ctx *ctx = &rng_ctx;
	struct tegra_aes_slot *key_slot;
	struct timespec ts;
	int ret = 0;
	u64 nsec, tmp[2];
	u8 *dt;

	if (!ctx || !dd) {
		dev_err(dd->dev, "ctx=0x%x, dd=0x%x\n",
			(unsigned int)ctx, (unsigned int)dd);
		return -EINVAL;
	}

	if (slen < (DEFAULT_RNG_BLK_SZ + AES_KEYSIZE_128)) {
		dev_err(dd->dev, "seed size invalid");
		return -ENOMEM;
	}

	/* take mutex to access the aes hw */
	mutex_lock(&aes_lock);

	if (!ctx->slot) {
		key_slot = aes_find_key_slot(dd);
		if (!key_slot) {
			dev_err(dd->dev, "no empty slot\n");
			mutex_unlock(&aes_lock);
			return -ENOMEM;
		}
		ctx->slot = key_slot;
	}

	ctx->dd = dd;
	dd->ctx = ctx;
	dd->ctr = 0;

	ctx->keylen = AES_KEYSIZE_128;
	ctx->flags |= FLAGS_NEW_KEY;

	/* copy the key to the key slot */
	memset(dd->ivkey_base, 0, AES_HW_KEY_TABLE_LENGTH_BYTES);
	memcpy(dd->ivkey_base, seed + DEFAULT_RNG_BLK_SZ, AES_KEYSIZE_128);

	dd->iv = seed;
	dd->ivlen = slen;

	dd->flags = FLAGS_ENCRYPT | FLAGS_RNG;

	/* take the hardware semaphore */
	if (tegra_arb_mutex_lock_timeout(dd->res_id, ARB_SEMA_TIMEOUT) < 0) {
		dev_err(dd->dev, "aes hardware not available\n");
		mutex_unlock(&aes_lock);
		return -EBUSY;
	}

	ret = aes_hw_init(dd);
	if (ret < 0) {
		dev_err(dd->dev, "%s: hw init fail(%d)\n", __func__, ret);
		goto fail;
	}

	aes_set_key(dd);

	/* set seed to the aes hw slot */
	memset(dd->buf_in, 0, AES_BLOCK_SIZE);
	memcpy(dd->buf_in, dd->iv, DEFAULT_RNG_BLK_SZ);
	ret = aes_start_crypt(dd, (u32)dd->dma_buf_in,
	  (u32)dd->dma_buf_out, 1, FLAGS_CBC, false);
	if (ret < 0) {
		dev_err(dd->dev, "aes_start_crypt fail(%d)\n", ret);
		goto out;
	}

	if (dd->ivlen >= (2 * DEFAULT_RNG_BLK_SZ + AES_KEYSIZE_128)) {
		dt = dd->iv + DEFAULT_RNG_BLK_SZ + AES_KEYSIZE_128;
	} else {
		getnstimeofday(&ts);
		nsec = timespec_to_ns(&ts);
		do_div(nsec, 1000);
		nsec ^= dd->ctr << 56;
		dd->ctr++;
		tmp[0] = nsec;
		tmp[1] = tegra_chip_uid();
		dt = (u8 *)tmp;
	}
	memcpy(dd->dt, dt, DEFAULT_RNG_BLK_SZ);

out:
	aes_hw_deinit(dd);

fail:
	/* release the hardware semaphore */
	tegra_arb_mutex_unlock(dd->res_id);
	mutex_unlock(&aes_lock);

	dev_dbg(dd->dev, "%s: done\n", __func__);
	return ret;
}

static int tegra_aes_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct tegra_aes_reqctx);

	return 0;
}

static struct crypto_alg algs[] = {
	{
		.cra_name = "disabled_ecb(aes)",
		.cra_driver_name = "ecb-aes-tegra",
		.cra_priority = 100,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct tegra_aes_ctx),
		.cra_alignmask = 3,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_aes_cra_init,
		.cra_u.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.setkey = tegra_aes_setkey,
			.encrypt = tegra_aes_ecb_encrypt,
			.decrypt = tegra_aes_ecb_decrypt,
		},
	}, {
		.cra_name = "disabled_cbc(aes)",
		.cra_driver_name = "cbc-aes-tegra",
		.cra_priority = 100,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize  = sizeof(struct tegra_aes_ctx),
		.cra_alignmask = 3,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_aes_cra_init,
		.cra_u.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_MIN_KEY_SIZE,
			.setkey = tegra_aes_setkey,
			.encrypt = tegra_aes_cbc_encrypt,
			.decrypt = tegra_aes_cbc_decrypt,
		}
	}, {
		.cra_name = "disabled_ansi_cprng",
		.cra_driver_name = "rng-aes-tegra",
		.cra_priority = 100,
		.cra_flags = CRYPTO_ALG_TYPE_RNG,
		.cra_ctxsize = sizeof(struct tegra_aes_ctx),
		.cra_type = &crypto_rng_type,
		.cra_module = THIS_MODULE,
		.cra_init = tegra_aes_cra_init,
		.cra_u.rng = {
			.rng_make_random = tegra_aes_get_random,
			.rng_reset = tegra_aes_rng_reset,
			.seedsize = AES_KEYSIZE_128 + (2 * DEFAULT_RNG_BLK_SZ),
		}
	}
};

static int tegra_aes_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_aes_dev *dd;
	struct resource *res;
	int err = -ENOMEM, i = 0, j;

	if (aes_dev)
		return -EEXIST;

	dd = kzalloc(sizeof(struct tegra_aes_dev), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(dev, "unable to alloc data struct.\n");
		return -ENOMEM;;
	}
	dd->dev = dev;
	platform_set_drvdata(pdev, dd);

	dd->slots = kzalloc(sizeof(struct tegra_aes_slot) * AES_NR_KEYSLOTS,
		GFP_KERNEL);
	if (dd->slots == NULL) {
		dev_err(dev, "unable to alloc slot struct.\n");
		goto out;
	}

	spin_lock_init(&dd->lock);
	crypto_init_queue(&dd->queue, TEGRA_AES_QUEUE_LENGTH);

	/* Get the module base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "invalid resource type: base\n");
		err = -ENODEV;
		goto out;
	}
	dd->phys_base = res->start;

	dd->io_base = ioremap(dd->phys_base, resource_size(res));
	if (!dd->io_base) {
		dev_err(dev, "can't ioremap phys_base\n");
		err = -ENOMEM;
		goto out;
	}

	dd->res_id = TEGRA_ARB_AES;

	/* Initialise the master bsev clock */
	dd->pclk = clk_get(dev, "bsev");
	if (!dd->pclk) {
		dev_err(dev, "pclock intialization failed.\n");
		err = -ENODEV;
		goto out;
	}

	/* Initialize the vde clock */
	dd->iclk = clk_get(dev, "vde");
	if (!dd->iclk) {
		dev_err(dev, "iclock intialization failed.\n");
		err = -ENODEV;
		goto out;
	}

	/*
	 * the foll contiguous memory is allocated as follows -
	 * - hardware key table
	 * - key schedule
	 */
	dd->ivkey_base = dma_alloc_coherent(dev, SZ_512, &dd->ivkey_phys_base,
		GFP_KERNEL);
	if (!dd->ivkey_base) {
		dev_err(dev, "can not allocate iv/key buffer\n");
		err = -ENOMEM;
		goto out;
	}

	dd->buf_in = dma_alloc_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
		&dd->dma_buf_in, GFP_KERNEL);
	if (!dd->buf_in) {
		dev_err(dev, "can not allocate dma-in buffer\n");
		err = -ENOMEM;
		goto out;
	}

	dd->buf_out = dma_alloc_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
		&dd->dma_buf_out, GFP_KERNEL);
	if (!dd->buf_out) {
		dev_err(dev, "can not allocate dma-out buffer\n");
		err = -ENOMEM;
		goto out;
	}

	init_completion(&dd->op_complete);
	aes_wq = alloc_workqueue("aes_wq", WQ_HIGHPRI, 16);
	if (!aes_wq) {
		dev_err(dev, "alloc_workqueue failed\n");
		goto out;
	}

	/* get the irq */
	err = request_irq(INT_VDE_BSE_V, aes_irq, IRQF_TRIGGER_HIGH,
		"tegra-aes", dd);
	if (err) {
		dev_err(dev, "request_irq failed\n");
		goto out;
	}

	spin_lock_init(&list_lock);
	spin_lock(&list_lock);
	for (i = 0; i < AES_NR_KEYSLOTS; i++) {
		dd->slots[i].available = true;
		dd->slots[i].slot_num = i;
		INIT_LIST_HEAD(&dd->slots[i].node);
		list_add_tail(&dd->slots[i].node, &dev_list);
	}
	spin_unlock(&list_lock);

	aes_dev = dd;
	for (i = 0; i < ARRAY_SIZE(algs); i++) {
		INIT_LIST_HEAD(&algs[i].cra_list);
		err = crypto_register_alg(&algs[i]);
		if (err)
			goto out;
	}

	dev_info(dev, "registered");
	return 0;

out:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&algs[j]);
	if (dd->ivkey_base)
		dma_free_coherent(dev, SZ_512, dd->ivkey_base,
			dd->ivkey_phys_base);
	if (dd->buf_in)
		dma_free_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
			dd->buf_in, dd->dma_buf_in);
	if (dd->buf_out)
		dma_free_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
			dd->buf_out, dd->dma_buf_out);
	if (dd->io_base)
		iounmap(dd->io_base);
	if (dd->iclk)
		clk_put(dd->iclk);
	if (dd->pclk)
		clk_put(dd->pclk);
	if (aes_wq)
		destroy_workqueue(aes_wq);
	free_irq(INT_VDE_BSE_V, dd);
	spin_lock(&list_lock);
	list_del(&dev_list);
	spin_unlock(&list_lock);

	kfree(dd->slots);
	kfree(dd);
	aes_dev = NULL;
	dev_err(dev, "%s: initialization failed.\n", __func__);
	return err;
}

static int __devexit tegra_aes_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_aes_dev *dd = platform_get_drvdata(pdev);
	int i;

	if (!dd)
		return -ENODEV;

	cancel_work_sync(&aes_work);
	destroy_workqueue(aes_wq);
	free_irq(INT_VDE_BSE_V, dd);
	spin_lock(&list_lock);
	list_del(&dev_list);
	spin_unlock(&list_lock);

	for (i = 0; i < ARRAY_SIZE(algs); i++)
		crypto_unregister_alg(&algs[i]);

	dma_free_coherent(dev, SZ_512, dd->ivkey_base,
		dd->ivkey_phys_base);
	dma_free_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
		dd->buf_in, dd->dma_buf_in);
	dma_free_coherent(dev, AES_HW_DMA_BUFFER_SIZE_BYTES,
		dd->buf_out, dd->dma_buf_out);
	iounmap(dd->io_base);
	clk_put(dd->iclk);
	clk_put(dd->pclk);
	kfree(dd->slots);
	kfree(dd);
	aes_dev = NULL;

	return 0;
}

static struct platform_driver tegra_aes_driver = {
	.probe  = tegra_aes_probe,
	.remove = __devexit_p(tegra_aes_remove),
	.driver = {
		.name   = "tegra-aes",
		.owner  = THIS_MODULE,
	},
};

static int __init tegra_aes_mod_init(void)
{
	mutex_init(&aes_lock);
	INIT_LIST_HEAD(&dev_list);
	return  platform_driver_register(&tegra_aes_driver);
}

static void __exit tegra_aes_mod_exit(void)
{
	platform_driver_unregister(&tegra_aes_driver);
}

module_init(tegra_aes_mod_init);
module_exit(tegra_aes_mod_exit);

MODULE_DESCRIPTION("Tegra AES hw acceleration support.");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_LICENSE("GPLv2");
