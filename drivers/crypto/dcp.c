/*
 * Cryptographic API.
 *
 * Support for DCP cryptographic accelerator.
 *
 * Copyright (c) 2013
 * Author: Tobias Rauter <tobias.rauter@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Based on tegra-aes.c, dcp.c (from freescale SDK) and sahara.c
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include <linux/miscdevice.h>

#include <crypto/scatterwalk.h>
#include <crypto/aes.h>


/* IOCTL for DCP OTP Key AES - taken from Freescale's SDK*/
#define DBS_IOCTL_BASE   'd'
#define DBS_ENC	_IOW(DBS_IOCTL_BASE, 0x00, uint8_t[16])
#define DBS_DEC _IOW(DBS_IOCTL_BASE, 0x01, uint8_t[16])

/* DCP channel used for AES */
#define USED_CHANNEL 1
/* Ring Buffers' maximum size */
#define DCP_MAX_PKG 20

/* Control Register */
#define DCP_REG_CTRL 0x000
#define DCP_CTRL_SFRST (1<<31)
#define DCP_CTRL_CLKGATE (1<<30)
#define DCP_CTRL_CRYPTO_PRESENT (1<<29)
#define DCP_CTRL_SHA_PRESENT (1<<28)
#define DCP_CTRL_GATHER_RES_WRITE (1<<23)
#define DCP_CTRL_ENABLE_CONTEXT_CACHE (1<<22)
#define DCP_CTRL_ENABLE_CONTEXT_SWITCH (1<<21)
#define DCP_CTRL_CH_IRQ_E_0 0x01
#define DCP_CTRL_CH_IRQ_E_1 0x02
#define DCP_CTRL_CH_IRQ_E_2 0x04
#define DCP_CTRL_CH_IRQ_E_3 0x08

/* Status register */
#define DCP_REG_STAT 0x010
#define DCP_STAT_OTP_KEY_READY (1<<28)
#define DCP_STAT_CUR_CHANNEL(stat) ((stat>>24)&0x0F)
#define DCP_STAT_READY_CHANNEL(stat) ((stat>>16)&0x0F)
#define DCP_STAT_IRQ(stat) (stat&0x0F)
#define DCP_STAT_CHAN_0 (0x01)
#define DCP_STAT_CHAN_1 (0x02)
#define DCP_STAT_CHAN_2 (0x04)
#define DCP_STAT_CHAN_3 (0x08)

/* Channel Control Register */
#define DCP_REG_CHAN_CTRL 0x020
#define DCP_CHAN_CTRL_CH0_IRQ_MERGED (1<<16)
#define DCP_CHAN_CTRL_HIGH_PRIO_0 (0x0100)
#define DCP_CHAN_CTRL_HIGH_PRIO_1 (0x0200)
#define DCP_CHAN_CTRL_HIGH_PRIO_2 (0x0400)
#define DCP_CHAN_CTRL_HIGH_PRIO_3 (0x0800)
#define DCP_CHAN_CTRL_ENABLE_0 (0x01)
#define DCP_CHAN_CTRL_ENABLE_1 (0x02)
#define DCP_CHAN_CTRL_ENABLE_2 (0x04)
#define DCP_CHAN_CTRL_ENABLE_3 (0x08)

/*
 * Channel Registers:
 * The DCP has 4 channels. Each of this channels
 * has 4 registers (command pointer, semaphore, status and options).
 * The address of register REG of channel CHAN is obtained by
 * dcp_chan_reg(REG, CHAN)
 */
#define DCP_REG_CHAN_PTR	0x00000100
#define DCP_REG_CHAN_SEMA	0x00000110
#define DCP_REG_CHAN_STAT	0x00000120
#define DCP_REG_CHAN_OPT	0x00000130

#define DCP_CHAN_STAT_NEXT_CHAIN_IS_0	0x010000
#define DCP_CHAN_STAT_NO_CHAIN		0x020000
#define DCP_CHAN_STAT_CONTEXT_ERROR	0x030000
#define DCP_CHAN_STAT_PAYLOAD_ERROR	0x040000
#define DCP_CHAN_STAT_INVALID_MODE	0x050000
#define DCP_CHAN_STAT_PAGEFAULT		0x40
#define DCP_CHAN_STAT_DST		0x20
#define DCP_CHAN_STAT_SRC		0x10
#define DCP_CHAN_STAT_PACKET		0x08
#define DCP_CHAN_STAT_SETUP		0x04
#define DCP_CHAN_STAT_MISMATCH		0x02

/* hw packet control*/

#define DCP_PKT_PAYLOAD_KEY	(1<<11)
#define DCP_PKT_OTP_KEY		(1<<10)
#define DCP_PKT_CIPHER_INIT	(1<<9)
#define DCP_PKG_CIPHER_ENCRYPT	(1<<8)
#define DCP_PKT_CIPHER_ENABLE	(1<<5)
#define DCP_PKT_DECR_SEM	(1<<1)
#define DCP_PKT_CHAIN		(1<<2)
#define DCP_PKT_IRQ		1

#define DCP_PKT_MODE_CBC	(1<<4)
#define DCP_PKT_KEYSELECT_OTP	(0xFF<<8)

/* cipher flags */
#define DCP_ENC		0x0001
#define DCP_DEC		0x0002
#define DCP_ECB		0x0004
#define DCP_CBC		0x0008
#define DCP_CBC_INIT	0x0010
#define DCP_NEW_KEY	0x0040
#define DCP_OTP_KEY	0x0080
#define DCP_AES		0x1000

/* DCP Flags */
#define DCP_FLAG_BUSY	0x01
#define DCP_FLAG_PRODUCING	0x02

/* clock defines */
#define CLOCK_ON	1
#define CLOCK_OFF	0

struct dcp_dev_req_ctx {
	int mode;
};

struct dcp_op {
	unsigned int		flags;
	u8			key[AES_KEYSIZE_128];
	int			keylen;

	struct ablkcipher_request	*req;
	struct crypto_ablkcipher	*fallback;

	uint32_t stat;
	uint32_t pkt1;
	uint32_t pkt2;
	struct ablkcipher_walk walk;
};

struct dcp_dev {
	struct device *dev;
	void __iomem *dcp_regs_base;

	int dcp_vmi_irq;
	int dcp_irq;

	spinlock_t queue_lock;
	struct crypto_queue queue;

	uint32_t pkt_produced;
	uint32_t pkt_consumed;

	struct dcp_hw_packet *hw_pkg[DCP_MAX_PKG];
	dma_addr_t hw_phys_pkg;

	/* [KEY][IV] Both with 16 Bytes */
	u8 *payload_base;
	dma_addr_t payload_base_dma;


	struct tasklet_struct	done_task;
	struct tasklet_struct	queue_task;
	struct timer_list	watchdog;

	unsigned long		flags;

	struct dcp_op *ctx;

	struct miscdevice dcp_bootstream_misc;
};

struct dcp_hw_packet {
	uint32_t next;
	uint32_t pkt1;
	uint32_t pkt2;
	uint32_t src;
	uint32_t dst;
	uint32_t size;
	uint32_t payload;
	uint32_t stat;
};

static struct dcp_dev *global_dev;

static inline u32 dcp_chan_reg(u32 reg, int chan)
{
	return reg + (chan) * 0x40;
}

static inline void dcp_write(struct dcp_dev *dev, u32 data, u32 reg)
{
	writel(data, dev->dcp_regs_base + reg);
}

static inline void dcp_set(struct dcp_dev *dev, u32 data, u32 reg)
{
	writel(data, dev->dcp_regs_base + (reg | 0x04));
}

static inline void dcp_clear(struct dcp_dev *dev, u32 data, u32 reg)
{
	writel(data, dev->dcp_regs_base + (reg | 0x08));
}

static inline void dcp_toggle(struct dcp_dev *dev, u32 data, u32 reg)
{
	writel(data, dev->dcp_regs_base + (reg | 0x0C));
}

static inline unsigned int dcp_read(struct dcp_dev *dev, u32 reg)
{
	return readl(dev->dcp_regs_base + reg);
}

static void dcp_dma_unmap(struct dcp_dev *dev, struct dcp_hw_packet *pkt)
{
	dma_unmap_page(dev->dev, pkt->src, pkt->size, DMA_TO_DEVICE);
	dma_unmap_page(dev->dev, pkt->dst, pkt->size, DMA_FROM_DEVICE);
	dev_dbg(dev->dev, "unmap packet %x", (unsigned int) pkt);
}

static int dcp_dma_map(struct dcp_dev *dev,
	struct ablkcipher_walk *walk, struct dcp_hw_packet *pkt)
{
	dev_dbg(dev->dev, "map packet %x", (unsigned int) pkt);
	/* align to length = 16 */
	pkt->size = walk->nbytes - (walk->nbytes % 16);

	pkt->src = dma_map_page(dev->dev, walk->src.page, walk->src.offset,
		pkt->size, DMA_TO_DEVICE);

	if (pkt->src == 0) {
		dev_err(dev->dev, "Unable to map src");
		return -ENOMEM;
	}

	pkt->dst = dma_map_page(dev->dev, walk->dst.page, walk->dst.offset,
		pkt->size, DMA_FROM_DEVICE);

	if (pkt->dst == 0) {
		dev_err(dev->dev, "Unable to map dst");
		dma_unmap_page(dev->dev, pkt->src, pkt->size, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	return 0;
}

static void dcp_op_one(struct dcp_dev *dev, struct dcp_hw_packet *pkt,
			uint8_t last)
{
	struct dcp_op *ctx = dev->ctx;
	pkt->pkt1 = ctx->pkt1;
	pkt->pkt2 = ctx->pkt2;

	pkt->payload = (u32) dev->payload_base_dma;
	pkt->stat = 0;

	if (ctx->flags & DCP_CBC_INIT) {
		pkt->pkt1 |= DCP_PKT_CIPHER_INIT;
		ctx->flags &= ~DCP_CBC_INIT;
	}

	mod_timer(&dev->watchdog, jiffies + msecs_to_jiffies(500));
	pkt->pkt1 |= DCP_PKT_IRQ;
	if (!last)
		pkt->pkt1 |= DCP_PKT_CHAIN;

	dev->pkt_produced++;

	dcp_write(dev, 1,
		dcp_chan_reg(DCP_REG_CHAN_SEMA, USED_CHANNEL));
}

static void dcp_op_proceed(struct dcp_dev *dev)
{
	struct dcp_op *ctx = dev->ctx;
	struct dcp_hw_packet *pkt;

	while (ctx->walk.nbytes) {
		int err = 0;

		pkt = dev->hw_pkg[dev->pkt_produced % DCP_MAX_PKG];
		err = dcp_dma_map(dev, &ctx->walk, pkt);
		if (err) {
			dev->ctx->stat |= err;
			/* start timer to wait for already set up calls */
			mod_timer(&dev->watchdog,
				jiffies + msecs_to_jiffies(500));
			break;
		}


		err = ctx->walk.nbytes - pkt->size;
		ablkcipher_walk_done(dev->ctx->req, &dev->ctx->walk, err);

		dcp_op_one(dev, pkt, ctx->walk.nbytes == 0);
		/* we have to wait if no space is left in buffer */
		if (dev->pkt_produced - dev->pkt_consumed == DCP_MAX_PKG)
			break;
	}
	clear_bit(DCP_FLAG_PRODUCING, &dev->flags);
}

static void dcp_op_start(struct dcp_dev *dev, uint8_t use_walk)
{
	struct dcp_op *ctx = dev->ctx;

	if (ctx->flags & DCP_NEW_KEY) {
		memcpy(dev->payload_base, ctx->key, ctx->keylen);
		ctx->flags &= ~DCP_NEW_KEY;
	}

	ctx->pkt1 = 0;
	ctx->pkt1 |= DCP_PKT_CIPHER_ENABLE;
	ctx->pkt1 |= DCP_PKT_DECR_SEM;

	if (ctx->flags & DCP_OTP_KEY)
		ctx->pkt1 |= DCP_PKT_OTP_KEY;
	else
		ctx->pkt1 |= DCP_PKT_PAYLOAD_KEY;

	if (ctx->flags & DCP_ENC)
		ctx->pkt1 |= DCP_PKG_CIPHER_ENCRYPT;

	ctx->pkt2 = 0;
	if (ctx->flags & DCP_CBC)
		ctx->pkt2 |= DCP_PKT_MODE_CBC;

	dev->pkt_produced = 0;
	dev->pkt_consumed = 0;

	ctx->stat = 0;
	dcp_clear(dev, -1, dcp_chan_reg(DCP_REG_CHAN_STAT, USED_CHANNEL));
	dcp_write(dev, (u32) dev->hw_phys_pkg,
		dcp_chan_reg(DCP_REG_CHAN_PTR, USED_CHANNEL));

	set_bit(DCP_FLAG_PRODUCING, &dev->flags);

	if (use_walk) {
		ablkcipher_walk_init(&ctx->walk, ctx->req->dst,
				ctx->req->src, ctx->req->nbytes);
		ablkcipher_walk_phys(ctx->req, &ctx->walk);
		dcp_op_proceed(dev);
	} else {
		dcp_op_one(dev, dev->hw_pkg[0], 1);
		clear_bit(DCP_FLAG_PRODUCING, &dev->flags);
	}
}

static void dcp_done_task(unsigned long data)
{
	struct dcp_dev *dev = (struct dcp_dev *)data;
	struct dcp_hw_packet *last_packet;
	int fin;
	fin = 0;

	for (last_packet = dev->hw_pkg[(dev->pkt_consumed) % DCP_MAX_PKG];
		last_packet->stat == 1;
		last_packet =
			dev->hw_pkg[++(dev->pkt_consumed) % DCP_MAX_PKG]) {

		dcp_dma_unmap(dev, last_packet);
		last_packet->stat = 0;
		fin++;
	}
	/* the last call of this function already consumed this IRQ's packet */
	if (fin == 0)
		return;

	dev_dbg(dev->dev,
		"Packet(s) done with status %x; finished: %d, produced:%d, complete consumed: %d",
		dev->ctx->stat, fin, dev->pkt_produced, dev->pkt_consumed);

	last_packet = dev->hw_pkg[(dev->pkt_consumed - 1) % DCP_MAX_PKG];
	if (!dev->ctx->stat && last_packet->pkt1 & DCP_PKT_CHAIN) {
		if (!test_and_set_bit(DCP_FLAG_PRODUCING, &dev->flags))
			dcp_op_proceed(dev);
		return;
	}

	while (unlikely(dev->pkt_consumed < dev->pkt_produced)) {
		dcp_dma_unmap(dev,
			dev->hw_pkg[dev->pkt_consumed++ % DCP_MAX_PKG]);
	}

	if (dev->ctx->flags & DCP_OTP_KEY) {
		/* we used the miscdevice, no walk to finish */
		clear_bit(DCP_FLAG_BUSY, &dev->flags);
		return;
	}

	ablkcipher_walk_complete(&dev->ctx->walk);
	dev->ctx->req->base.complete(&dev->ctx->req->base,
			dev->ctx->stat);
	dev->ctx->req = NULL;
	/* in case there are other requests in the queue */
	tasklet_schedule(&dev->queue_task);
}

static void dcp_watchdog(unsigned long data)
{
	struct dcp_dev *dev = (struct dcp_dev *)data;
	dev->ctx->stat |= dcp_read(dev,
			dcp_chan_reg(DCP_REG_CHAN_STAT, USED_CHANNEL));

	dev_err(dev->dev, "Timeout, Channel status: %x", dev->ctx->stat);

	if (!dev->ctx->stat)
		dev->ctx->stat = -ETIMEDOUT;

	dcp_done_task(data);
}


static irqreturn_t dcp_common_irq(int irq, void *context)
{
	u32 msk;
	struct dcp_dev *dev = (struct dcp_dev *) context;

	del_timer(&dev->watchdog);

	msk = DCP_STAT_IRQ(dcp_read(dev, DCP_REG_STAT));
	dcp_clear(dev, msk, DCP_REG_STAT);
	if (msk == 0)
		return IRQ_NONE;

	dev->ctx->stat |= dcp_read(dev,
			dcp_chan_reg(DCP_REG_CHAN_STAT, USED_CHANNEL));

	if (msk & DCP_STAT_CHAN_1)
		tasklet_schedule(&dev->done_task);

	return IRQ_HANDLED;
}

static irqreturn_t dcp_vmi_irq(int irq, void *context)
{
	return dcp_common_irq(irq, context);
}

static irqreturn_t dcp_irq(int irq, void *context)
{
	return dcp_common_irq(irq, context);
}

static void dcp_crypt(struct dcp_dev *dev, struct dcp_op *ctx)
{
	dev->ctx = ctx;

	if ((ctx->flags & DCP_CBC) && ctx->req->info) {
		ctx->flags |= DCP_CBC_INIT;
		memcpy(dev->payload_base + AES_KEYSIZE_128,
			ctx->req->info, AES_KEYSIZE_128);
	}

	dcp_op_start(dev, 1);
}

static void dcp_queue_task(unsigned long data)
{
	struct dcp_dev *dev = (struct dcp_dev *) data;
	struct crypto_async_request *async_req, *backlog;
	struct crypto_ablkcipher *tfm;
	struct dcp_op *ctx;
	struct dcp_dev_req_ctx *rctx;
	struct ablkcipher_request *req;
	unsigned long flags;

	spin_lock_irqsave(&dev->queue_lock, flags);

	backlog = crypto_get_backlog(&dev->queue);
	async_req = crypto_dequeue_request(&dev->queue);

	spin_unlock_irqrestore(&dev->queue_lock, flags);

	if (!async_req)
		goto ret_nothing_done;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	req = ablkcipher_request_cast(async_req);
	tfm = crypto_ablkcipher_reqtfm(req);
	rctx = ablkcipher_request_ctx(req);
	ctx = crypto_ablkcipher_ctx(tfm);

	if (!req->src || !req->dst)
		goto ret_nothing_done;

	ctx->flags |= rctx->mode;
	ctx->req = req;

	dcp_crypt(dev, ctx);

	return;

ret_nothing_done:
	clear_bit(DCP_FLAG_BUSY, &dev->flags);
}


static int dcp_cra_init(struct crypto_tfm *tfm)
{
	const char *name = tfm->__crt_alg->cra_name;
	struct dcp_op *ctx = crypto_tfm_ctx(tfm);

	tfm->crt_ablkcipher.reqsize = sizeof(struct dcp_dev_req_ctx);

	ctx->fallback = crypto_alloc_ablkcipher(name, 0,
				CRYPTO_ALG_ASYNC | CRYPTO_ALG_NEED_FALLBACK);

	if (IS_ERR(ctx->fallback)) {
		dev_err(global_dev->dev, "Error allocating fallback algo %s\n",
			name);
		return PTR_ERR(ctx->fallback);
	}

	return 0;
}

static void dcp_cra_exit(struct crypto_tfm *tfm)
{
	struct dcp_op *ctx = crypto_tfm_ctx(tfm);

	if (ctx->fallback)
		crypto_free_ablkcipher(ctx->fallback);

	ctx->fallback = NULL;
}

/* async interface */
static int dcp_aes_setkey(struct crypto_ablkcipher *tfm, const u8 *key,
		unsigned int len)
{
	struct dcp_op *ctx = crypto_ablkcipher_ctx(tfm);
	unsigned int ret = 0;
	ctx->keylen = len;
	ctx->flags = 0;
	if (len == AES_KEYSIZE_128) {
		if (memcmp(ctx->key, key, AES_KEYSIZE_128)) {
			memcpy(ctx->key, key, len);
			ctx->flags |= DCP_NEW_KEY;
		}
		return 0;
	}

	ctx->fallback->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
	ctx->fallback->base.crt_flags |=
		(tfm->base.crt_flags & CRYPTO_TFM_REQ_MASK);

	ret = crypto_ablkcipher_setkey(ctx->fallback, key, len);
	if (ret) {
		struct crypto_tfm *tfm_aux = crypto_ablkcipher_tfm(tfm);

		tfm_aux->crt_flags &= ~CRYPTO_TFM_RES_MASK;
		tfm_aux->crt_flags |=
			(ctx->fallback->base.crt_flags & CRYPTO_TFM_RES_MASK);
	}
	return ret;
}

static int dcp_aes_cbc_crypt(struct ablkcipher_request *req, int mode)
{
	struct dcp_dev_req_ctx *rctx = ablkcipher_request_ctx(req);
	struct dcp_dev *dev = global_dev;
	unsigned long flags;
	int err = 0;

	if (!IS_ALIGNED(req->nbytes, AES_BLOCK_SIZE))
		return -EINVAL;

	rctx->mode = mode;

	spin_lock_irqsave(&dev->queue_lock, flags);
	err = ablkcipher_enqueue_request(&dev->queue, req);
	spin_unlock_irqrestore(&dev->queue_lock, flags);

	flags = test_and_set_bit(DCP_FLAG_BUSY, &dev->flags);

	if (!(flags & DCP_FLAG_BUSY))
		tasklet_schedule(&dev->queue_task);

	return err;
}

static int dcp_aes_cbc_encrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct dcp_op *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		int err = 0;
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_encrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}

	return dcp_aes_cbc_crypt(req, DCP_AES | DCP_ENC | DCP_CBC);
}

static int dcp_aes_cbc_decrypt(struct ablkcipher_request *req)
{
	struct crypto_tfm *tfm =
		crypto_ablkcipher_tfm(crypto_ablkcipher_reqtfm(req));
	struct dcp_op *ctx = crypto_ablkcipher_ctx(
		crypto_ablkcipher_reqtfm(req));

	if (unlikely(ctx->keylen != AES_KEYSIZE_128)) {
		int err = 0;
		ablkcipher_request_set_tfm(req, ctx->fallback);
		err = crypto_ablkcipher_decrypt(req);
		ablkcipher_request_set_tfm(req, __crypto_ablkcipher_cast(tfm));
		return err;
	}
	return dcp_aes_cbc_crypt(req, DCP_AES | DCP_DEC | DCP_CBC);
}

static struct crypto_alg algs[] = {
	{
		.cra_name = "cbc(aes)",
		.cra_driver_name = "dcp-cbc-aes",
		.cra_alignmask = 3,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC |
			  CRYPTO_ALG_NEED_FALLBACK,
		.cra_blocksize = AES_KEYSIZE_128,
		.cra_type = &crypto_ablkcipher_type,
		.cra_priority = 300,
		.cra_u.ablkcipher = {
			.min_keysize =	AES_KEYSIZE_128,
			.max_keysize = AES_KEYSIZE_128,
			.setkey = dcp_aes_setkey,
			.encrypt = dcp_aes_cbc_encrypt,
			.decrypt = dcp_aes_cbc_decrypt,
			.ivsize = AES_KEYSIZE_128,
		}

	},
};

/* DCP bootstream verification interface: uses OTP key for crypto */
static int dcp_bootstream_open(struct inode *inode, struct file *file)
{
	file->private_data = container_of((file->private_data),
			struct dcp_dev, dcp_bootstream_misc);
	return 0;
}

static long dcp_bootstream_ioctl(struct file *file,
					 unsigned int cmd, unsigned long arg)
{
	struct dcp_dev *dev = (struct dcp_dev *) file->private_data;
	void __user *argp = (void __user *)arg;
	int ret;

	if (dev == NULL)
		return -EBADF;

	if (cmd != DBS_ENC && cmd != DBS_DEC)
		return -EINVAL;

	if (copy_from_user(dev->payload_base, argp, 16))
		return -EFAULT;

	if (test_and_set_bit(DCP_FLAG_BUSY, &dev->flags))
		return -EAGAIN;

	dev->ctx = kzalloc(sizeof(struct dcp_op), GFP_KERNEL);
	if (!dev->ctx) {
		dev_err(dev->dev,
			"cannot allocate context for OTP crypto");
		clear_bit(DCP_FLAG_BUSY, &dev->flags);
		return -ENOMEM;
	}

	dev->ctx->flags = DCP_AES | DCP_ECB | DCP_OTP_KEY | DCP_CBC_INIT;
	dev->ctx->flags |= (cmd == DBS_ENC) ? DCP_ENC : DCP_DEC;
	dev->hw_pkg[0]->src = dev->payload_base_dma;
	dev->hw_pkg[0]->dst = dev->payload_base_dma;
	dev->hw_pkg[0]->size = 16;

	dcp_op_start(dev, 0);

	while (test_bit(DCP_FLAG_BUSY, &dev->flags))
		cpu_relax();

	ret = dev->ctx->stat;
	if (!ret && copy_to_user(argp, dev->payload_base, 16))
		ret =  -EFAULT;

	kfree(dev->ctx);

	return ret;
}

static const struct file_operations dcp_bootstream_fops = {
	.owner =		THIS_MODULE,
	.unlocked_ioctl =	dcp_bootstream_ioctl,
	.open =			dcp_bootstream_open,
};

static int dcp_probe(struct platform_device *pdev)
{
	struct dcp_dev *dev = NULL;
	struct resource *r;
	int i, ret, j;

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	global_dev = dev;
	dev->dev = &pdev->dev;

	platform_set_drvdata(pdev, dev);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		dev_err(&pdev->dev, "failed to get IORESOURCE_MEM\n");
		return -ENXIO;
	}
	dev->dcp_regs_base = devm_ioremap(&pdev->dev, r->start,
					  resource_size(r));

	dcp_set(dev, DCP_CTRL_SFRST, DCP_REG_CTRL);
	udelay(10);
	dcp_clear(dev, DCP_CTRL_SFRST | DCP_CTRL_CLKGATE, DCP_REG_CTRL);

	dcp_write(dev, DCP_CTRL_GATHER_RES_WRITE |
		DCP_CTRL_ENABLE_CONTEXT_CACHE | DCP_CTRL_CH_IRQ_E_1,
		DCP_REG_CTRL);

	dcp_write(dev, DCP_CHAN_CTRL_ENABLE_1, DCP_REG_CHAN_CTRL);

	for (i = 0; i < 4; i++)
		dcp_clear(dev, -1, dcp_chan_reg(DCP_REG_CHAN_STAT, i));

	dcp_clear(dev, -1, DCP_REG_STAT);


	r = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!r) {
		dev_err(&pdev->dev, "can't get IRQ resource (0)\n");
		return -EIO;
	}
	dev->dcp_vmi_irq = r->start;
	ret = request_irq(dev->dcp_vmi_irq, dcp_vmi_irq, 0, "dcp", dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't request_irq (0)\n");
		return -EIO;
	}

	r = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!r) {
		dev_err(&pdev->dev, "can't get IRQ resource (1)\n");
		ret = -EIO;
		goto err_free_irq0;
	}
	dev->dcp_irq = r->start;
	ret = request_irq(dev->dcp_irq, dcp_irq, 0, "dcp", dev);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't request_irq (1)\n");
		ret = -EIO;
		goto err_free_irq0;
	}

	dev->hw_pkg[0] = dma_alloc_coherent(&pdev->dev,
			DCP_MAX_PKG * sizeof(struct dcp_hw_packet),
			&dev->hw_phys_pkg,
			GFP_KERNEL);
	if (!dev->hw_pkg[0]) {
		dev_err(&pdev->dev, "Could not allocate hw descriptors\n");
		ret = -ENOMEM;
		goto err_free_irq1;
	}

	for (i = 1; i < DCP_MAX_PKG; i++) {
		dev->hw_pkg[i - 1]->next = dev->hw_phys_pkg
				+ i * sizeof(struct dcp_hw_packet);
		dev->hw_pkg[i] = dev->hw_pkg[i - 1] + 1;
	}
	dev->hw_pkg[i - 1]->next = dev->hw_phys_pkg;


	dev->payload_base = dma_alloc_coherent(&pdev->dev, 2 * AES_KEYSIZE_128,
			&dev->payload_base_dma, GFP_KERNEL);
	if (!dev->payload_base) {
		dev_err(&pdev->dev, "Could not allocate memory for key\n");
		ret = -ENOMEM;
		goto err_free_hw_packet;
	}
	tasklet_init(&dev->queue_task, dcp_queue_task,
		(unsigned long) dev);
	tasklet_init(&dev->done_task, dcp_done_task,
		(unsigned long) dev);
	spin_lock_init(&dev->queue_lock);

	crypto_init_queue(&dev->queue, 10);

	init_timer(&dev->watchdog);
	dev->watchdog.function = &dcp_watchdog;
	dev->watchdog.data = (unsigned long)dev;

	dev->dcp_bootstream_misc.minor = MISC_DYNAMIC_MINOR,
	dev->dcp_bootstream_misc.name = "dcpboot",
	dev->dcp_bootstream_misc.fops = &dcp_bootstream_fops,
	ret = misc_register(&dev->dcp_bootstream_misc);
	if (ret != 0) {
		dev_err(dev->dev, "Unable to register misc device\n");
		goto err_free_key_iv;
	}

	for (i = 0; i < ARRAY_SIZE(algs); i++) {
		algs[i].cra_priority = 300;
		algs[i].cra_ctxsize = sizeof(struct dcp_op);
		algs[i].cra_module = THIS_MODULE;
		algs[i].cra_init = dcp_cra_init;
		algs[i].cra_exit = dcp_cra_exit;
		if (crypto_register_alg(&algs[i])) {
			dev_err(&pdev->dev, "register algorithm failed\n");
			ret = -ENOMEM;
			goto err_unregister;
		}
	}
	dev_notice(&pdev->dev, "DCP crypto enabled.!\n");

	return 0;

err_unregister:
	for (j = 0; j < i; j++)
		crypto_unregister_alg(&algs[j]);
err_free_key_iv:
	dma_free_coherent(&pdev->dev, 2 * AES_KEYSIZE_128, dev->payload_base,
			dev->payload_base_dma);
err_free_hw_packet:
	dma_free_coherent(&pdev->dev, DCP_MAX_PKG *
		sizeof(struct dcp_hw_packet), dev->hw_pkg[0],
		dev->hw_phys_pkg);
err_free_irq1:
	free_irq(dev->dcp_irq, dev);
err_free_irq0:
	free_irq(dev->dcp_vmi_irq, dev);

	return ret;
}

static int dcp_remove(struct platform_device *pdev)
{
	struct dcp_dev *dev;
	int j;
	dev = platform_get_drvdata(pdev);

	dma_free_coherent(&pdev->dev,
			DCP_MAX_PKG * sizeof(struct dcp_hw_packet),
			dev->hw_pkg[0],	dev->hw_phys_pkg);

	dma_free_coherent(&pdev->dev, 2 * AES_KEYSIZE_128, dev->payload_base,
			dev->payload_base_dma);

	free_irq(dev->dcp_irq, dev);
	free_irq(dev->dcp_vmi_irq, dev);

	tasklet_kill(&dev->done_task);
	tasklet_kill(&dev->queue_task);

	for (j = 0; j < ARRAY_SIZE(algs); j++)
		crypto_unregister_alg(&algs[j]);

	misc_deregister(&dev->dcp_bootstream_misc);

	return 0;
}

static struct of_device_id fs_dcp_of_match[] = {
	{	.compatible = "fsl-dcp"},
	{},
};

static struct platform_driver fs_dcp_driver = {
	.probe = dcp_probe,
	.remove = dcp_remove,
	.driver = {
		.name = "fsl-dcp",
		.owner = THIS_MODULE,
		.of_match_table = fs_dcp_of_match
	}
};

module_platform_driver(fs_dcp_driver);


MODULE_AUTHOR("Tobias Rauter <tobias.rauter@gmail.com>");
MODULE_DESCRIPTION("Freescale DCP Crypto Driver");
MODULE_LICENSE("GPL");
