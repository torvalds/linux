/*
 * Support for Marvell's crypto engine which can be found on some Orion5X
 * boards.
 *
 * Author: Sebastian Andrzej Siewior < sebastian at breakpoint dot cc >
 * License: GPLv2
 *
 */
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "mv_cesa.h"
/*
 * STM:
 *   /---------------------------------------\
 *   |					     | request complete
 *  \./					     |
 * IDLE -> new request -> BUSY -> done -> DEQUEUE
 *                         /°\               |
 *			    |		     | more scatter entries
 *			    \________________/
 */
enum engine_status {
	ENGINE_IDLE,
	ENGINE_BUSY,
	ENGINE_W_DEQUEUE,
};

/**
 * struct req_progress - used for every crypt request
 * @src_sg_it:		sg iterator for src
 * @dst_sg_it:		sg iterator for dst
 * @sg_src_left:	bytes left in src to process (scatter list)
 * @src_start:		offset to add to src start position (scatter list)
 * @crypt_len:		length of current crypt process
 * @sg_dst_left:	bytes left dst to process in this scatter list
 * @dst_start:		offset to add to dst start position (scatter list)
 * @total_req_bytes:	total number of bytes processed (request).
 *
 * sg helper are used to iterate over the scatterlist. Since the size of the
 * SRAM may be less than the scatter size, this struct struct is used to keep
 * track of progress within current scatterlist.
 */
struct req_progress {
	struct sg_mapping_iter src_sg_it;
	struct sg_mapping_iter dst_sg_it;

	/* src mostly */
	int sg_src_left;
	int src_start;
	int crypt_len;
	/* dst mostly */
	int sg_dst_left;
	int dst_start;
	int total_req_bytes;
};

struct crypto_priv {
	void __iomem *reg;
	void __iomem *sram;
	int irq;
	struct task_struct *queue_th;

	/* the lock protects queue and eng_st */
	spinlock_t lock;
	struct crypto_queue queue;
	enum engine_status eng_st;
	struct ablkcipher_request *cur_req;
	struct req_progress p;
	int max_req_size;
	int sram_size;
};

static struct crypto_priv *cpg;

struct mv_ctx {
	u8 aes_enc_key[AES_KEY_LEN];
	u32 aes_dec_key[8];
	int key_len;
	u32 need_calc_aes_dkey;
};

enum crypto_op {
	COP_AES_ECB,
	COP_AES_CBC,
};

struct mv_req_ctx {
	enum crypto_op op;
	int decrypt;
};

static void compute_aes_dec_key(struct mv_ctx *ctx)
{
	struct crypto_aes_ctx gen_aes_key;
	int key_pos;

	if (!ctx->need_calc_aes_dkey)
		return;

	crypto_aes_expand_key(&gen_aes_key, ctx->aes_enc_key, ctx->key_len);

	key_pos = ctx->key_len + 24;
	memcpy(ctx->aes_dec_key, &gen_aes_key.key_enc[key_pos], 4 * 4);
	switch (ctx->key_len) {
	case AES_KEYSIZE_256:
		key_pos -= 2;
		/* fall */
	case AES_KEYSIZE_192:
		key_pos -= 2;
		memcpy(&ctx->aes_dec_key[4], &gen_aes_key.key_enc[key_pos],
				4 * 4);
		break;
	}
	ctx->need_calc_aes_dkey = 0;
}

static int mv_setkey_aes(struct crypto_ablkcipher *cipher, const u8 *key,
		unsigned int len)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct mv_ctx *ctx = crypto_tfm_ctx(tfm);

	switch (len) {
	case AES_KEYSIZE_128:
	case AES_KEYSIZE_192:
	case AES_KEYSIZE_256:
		break;
	default:
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	ctx->key_len = len;
	ctx->need_calc_aes_dkey = 1;

	memcpy(ctx->aes_enc_key, key, AES_KEY_LEN);
	return 0;
}

static void setup_data_in(struct ablkcipher_request *req)
{
	int ret;
	void *buf;

	if (!cpg->p.sg_src_left) {
		ret = sg_miter_next(&cpg->p.src_sg_it);
		BUG_ON(!ret);
		cpg->p.sg_src_left = cpg->p.src_sg_it.length;
		cpg->p.src_start = 0;
	}

	cpg->p.crypt_len = min(cpg->p.sg_src_left, cpg->max_req_size);

	buf = cpg->p.src_sg_it.addr;
	buf += cpg->p.src_start;

	memcpy(cpg->sram + SRAM_DATA_IN_START, buf, cpg->p.crypt_len);

	cpg->p.sg_src_left -= cpg->p.crypt_len;
	cpg->p.src_start += cpg->p.crypt_len;
}

static void mv_process_current_q(int first_block)
{
	struct ablkcipher_request *req = cpg->cur_req;
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);
	struct sec_accel_config op;

	switch (req_ctx->op) {
	case COP_AES_ECB:
		op.config = CFG_OP_CRYPT_ONLY | CFG_ENCM_AES | CFG_ENC_MODE_ECB;
		break;
	case COP_AES_CBC:
		op.config = CFG_OP_CRYPT_ONLY | CFG_ENCM_AES | CFG_ENC_MODE_CBC;
		op.enc_iv = ENC_IV_POINT(SRAM_DATA_IV) |
			ENC_IV_BUF_POINT(SRAM_DATA_IV_BUF);
		if (first_block)
			memcpy(cpg->sram + SRAM_DATA_IV, req->info, 16);
		break;
	}
	if (req_ctx->decrypt) {
		op.config |= CFG_DIR_DEC;
		memcpy(cpg->sram + SRAM_DATA_KEY_P, ctx->aes_dec_key,
				AES_KEY_LEN);
	} else {
		op.config |= CFG_DIR_ENC;
		memcpy(cpg->sram + SRAM_DATA_KEY_P, ctx->aes_enc_key,
				AES_KEY_LEN);
	}

	switch (ctx->key_len) {
	case AES_KEYSIZE_128:
		op.config |= CFG_AES_LEN_128;
		break;
	case AES_KEYSIZE_192:
		op.config |= CFG_AES_LEN_192;
		break;
	case AES_KEYSIZE_256:
		op.config |= CFG_AES_LEN_256;
		break;
	}
	op.enc_p = ENC_P_SRC(SRAM_DATA_IN_START) |
		ENC_P_DST(SRAM_DATA_OUT_START);
	op.enc_key_p = SRAM_DATA_KEY_P;

	setup_data_in(req);
	op.enc_len = cpg->p.crypt_len;
	memcpy(cpg->sram + SRAM_CONFIG, &op,
			sizeof(struct sec_accel_config));

	writel(SRAM_CONFIG, cpg->reg + SEC_ACCEL_DESC_P0);
	/* GO */
	writel(SEC_CMD_EN_SEC_ACCL0, cpg->reg + SEC_ACCEL_CMD);

	/*
	 * XXX: add timer if the interrupt does not occur for some mystery
	 * reason
	 */
}

static void mv_crypto_algo_completion(void)
{
	struct ablkcipher_request *req = cpg->cur_req;
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	if (req_ctx->op != COP_AES_CBC)
		return ;

	memcpy(req->info, cpg->sram + SRAM_DATA_IV_BUF, 16);
}

static void dequeue_complete_req(void)
{
	struct ablkcipher_request *req = cpg->cur_req;
	void *buf;
	int ret;

	cpg->p.total_req_bytes += cpg->p.crypt_len;
	do {
		int dst_copy;

		if (!cpg->p.sg_dst_left) {
			ret = sg_miter_next(&cpg->p.dst_sg_it);
			BUG_ON(!ret);
			cpg->p.sg_dst_left = cpg->p.dst_sg_it.length;
			cpg->p.dst_start = 0;
		}

		buf = cpg->p.dst_sg_it.addr;
		buf += cpg->p.dst_start;

		dst_copy = min(cpg->p.crypt_len, cpg->p.sg_dst_left);

		memcpy(buf, cpg->sram + SRAM_DATA_OUT_START, dst_copy);

		cpg->p.sg_dst_left -= dst_copy;
		cpg->p.crypt_len -= dst_copy;
		cpg->p.dst_start += dst_copy;
	} while (cpg->p.crypt_len > 0);

	BUG_ON(cpg->eng_st != ENGINE_W_DEQUEUE);
	if (cpg->p.total_req_bytes < req->nbytes) {
		/* process next scatter list entry */
		cpg->eng_st = ENGINE_BUSY;
		mv_process_current_q(0);
	} else {
		sg_miter_stop(&cpg->p.src_sg_it);
		sg_miter_stop(&cpg->p.dst_sg_it);
		mv_crypto_algo_completion();
		cpg->eng_st = ENGINE_IDLE;
		req->base.complete(&req->base, 0);
	}
}

static int count_sgs(struct scatterlist *sl, unsigned int total_bytes)
{
	int i = 0;

	do {
		total_bytes -= sl[i].length;
		i++;

	} while (total_bytes > 0);

	return i;
}

static void mv_enqueue_new_req(struct ablkcipher_request *req)
{
	int num_sgs;

	cpg->cur_req = req;
	memset(&cpg->p, 0, sizeof(struct req_progress));

	num_sgs = count_sgs(req->src, req->nbytes);
	sg_miter_start(&cpg->p.src_sg_it, req->src, num_sgs, SG_MITER_FROM_SG);

	num_sgs = count_sgs(req->dst, req->nbytes);
	sg_miter_start(&cpg->p.dst_sg_it, req->dst, num_sgs, SG_MITER_TO_SG);
	mv_process_current_q(1);
}

static int queue_manag(void *data)
{
	cpg->eng_st = ENGINE_IDLE;
	do {
		struct ablkcipher_request *req;
		struct crypto_async_request *async_req = NULL;
		struct crypto_async_request *backlog;

		__set_current_state(TASK_INTERRUPTIBLE);

		if (cpg->eng_st == ENGINE_W_DEQUEUE)
			dequeue_complete_req();

		spin_lock_irq(&cpg->lock);
		if (cpg->eng_st == ENGINE_IDLE) {
			backlog = crypto_get_backlog(&cpg->queue);
			async_req = crypto_dequeue_request(&cpg->queue);
			if (async_req) {
				BUG_ON(cpg->eng_st != ENGINE_IDLE);
				cpg->eng_st = ENGINE_BUSY;
			}
		}
		spin_unlock_irq(&cpg->lock);

		if (backlog) {
			backlog->complete(backlog, -EINPROGRESS);
			backlog = NULL;
		}

		if (async_req) {
			req = container_of(async_req,
					struct ablkcipher_request, base);
			mv_enqueue_new_req(req);
			async_req = NULL;
		}

		schedule();

	} while (!kthread_should_stop());
	return 0;
}

static int mv_handle_req(struct ablkcipher_request *req)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&cpg->lock, flags);
	ret = ablkcipher_enqueue_request(&cpg->queue, req);
	spin_unlock_irqrestore(&cpg->lock, flags);
	wake_up_process(cpg->queue_th);
	return ret;
}

static int mv_enc_aes_ecb(struct ablkcipher_request *req)
{
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_ECB;
	req_ctx->decrypt = 0;

	return mv_handle_req(req);
}

static int mv_dec_aes_ecb(struct ablkcipher_request *req)
{
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_ECB;
	req_ctx->decrypt = 1;

	compute_aes_dec_key(ctx);
	return mv_handle_req(req);
}

static int mv_enc_aes_cbc(struct ablkcipher_request *req)
{
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_CBC;
	req_ctx->decrypt = 0;

	return mv_handle_req(req);
}

static int mv_dec_aes_cbc(struct ablkcipher_request *req)
{
	struct mv_ctx *ctx = crypto_tfm_ctx(req->base.tfm);
	struct mv_req_ctx *req_ctx = ablkcipher_request_ctx(req);

	req_ctx->op = COP_AES_CBC;
	req_ctx->decrypt = 1;

	compute_aes_dec_key(ctx);
	return mv_handle_req(req);
}

static int mv_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct mv_req_ctx);
	return 0;
}

irqreturn_t crypto_int(int irq, void *priv)
{
	u32 val;

	val = readl(cpg->reg + SEC_ACCEL_INT_STATUS);
	if (!(val & SEC_INT_ACCEL0_DONE))
		return IRQ_NONE;

	val &= ~SEC_INT_ACCEL0_DONE;
	writel(val, cpg->reg + FPGA_INT_STATUS);
	writel(val, cpg->reg + SEC_ACCEL_INT_STATUS);
	BUG_ON(cpg->eng_st != ENGINE_BUSY);
	cpg->eng_st = ENGINE_W_DEQUEUE;
	wake_up_process(cpg->queue_th);
	return IRQ_HANDLED;
}

struct crypto_alg mv_aes_alg_ecb = {
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "mv-ecb-aes",
	.cra_priority	= 300,
	.cra_flags	= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize	= 16,
	.cra_ctxsize	= sizeof(struct mv_ctx),
	.cra_alignmask	= 0,
	.cra_type	= &crypto_ablkcipher_type,
	.cra_module	= THIS_MODULE,
	.cra_init	= mv_cra_init,
	.cra_u		= {
		.ablkcipher = {
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	mv_setkey_aes,
			.encrypt	=	mv_enc_aes_ecb,
			.decrypt	=	mv_dec_aes_ecb,
		},
	},
};

struct crypto_alg mv_aes_alg_cbc = {
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "mv-cbc-aes",
	.cra_priority	= 300,
	.cra_flags	= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize	= AES_BLOCK_SIZE,
	.cra_ctxsize	= sizeof(struct mv_ctx),
	.cra_alignmask	= 0,
	.cra_type	= &crypto_ablkcipher_type,
	.cra_module	= THIS_MODULE,
	.cra_init	= mv_cra_init,
	.cra_u		= {
		.ablkcipher = {
			.ivsize		=	AES_BLOCK_SIZE,
			.min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey		=	mv_setkey_aes,
			.encrypt	=	mv_enc_aes_cbc,
			.decrypt	=	mv_dec_aes_cbc,
		},
	},
};

static int mv_probe(struct platform_device *pdev)
{
	struct crypto_priv *cp;
	struct resource *res;
	int irq;
	int ret;

	if (cpg) {
		printk(KERN_ERR "Second crypto dev?\n");
		return -EEXIST;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (!res)
		return -ENXIO;

	cp = kzalloc(sizeof(*cp), GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	spin_lock_init(&cp->lock);
	crypto_init_queue(&cp->queue, 50);
	cp->reg = ioremap(res->start, res->end - res->start + 1);
	if (!cp->reg) {
		ret = -ENOMEM;
		goto err;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "sram");
	if (!res) {
		ret = -ENXIO;
		goto err_unmap_reg;
	}
	cp->sram_size = res->end - res->start + 1;
	cp->max_req_size = cp->sram_size - SRAM_CFG_SPACE;
	cp->sram = ioremap(res->start, cp->sram_size);
	if (!cp->sram) {
		ret = -ENOMEM;
		goto err_unmap_reg;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0 || irq == NO_IRQ) {
		ret = irq;
		goto err_unmap_sram;
	}
	cp->irq = irq;

	platform_set_drvdata(pdev, cp);
	cpg = cp;

	cp->queue_th = kthread_run(queue_manag, cp, "mv_crypto");
	if (IS_ERR(cp->queue_th)) {
		ret = PTR_ERR(cp->queue_th);
		goto err_thread;
	}

	ret = request_irq(irq, crypto_int, IRQF_DISABLED, dev_name(&pdev->dev),
			cp);
	if (ret)
		goto err_unmap_sram;

	writel(SEC_INT_ACCEL0_DONE, cpg->reg + SEC_ACCEL_INT_MASK);
	writel(SEC_CFG_STOP_DIG_ERR, cpg->reg + SEC_ACCEL_CFG);

	ret = crypto_register_alg(&mv_aes_alg_ecb);
	if (ret)
		goto err_reg;

	ret = crypto_register_alg(&mv_aes_alg_cbc);
	if (ret)
		goto err_unreg_ecb;
	return 0;
err_unreg_ecb:
	crypto_unregister_alg(&mv_aes_alg_ecb);
err_thread:
	free_irq(irq, cp);
err_reg:
	kthread_stop(cp->queue_th);
err_unmap_sram:
	iounmap(cp->sram);
err_unmap_reg:
	iounmap(cp->reg);
err:
	kfree(cp);
	cpg = NULL;
	platform_set_drvdata(pdev, NULL);
	return ret;
}

static int mv_remove(struct platform_device *pdev)
{
	struct crypto_priv *cp = platform_get_drvdata(pdev);

	crypto_unregister_alg(&mv_aes_alg_ecb);
	crypto_unregister_alg(&mv_aes_alg_cbc);
	kthread_stop(cp->queue_th);
	free_irq(cp->irq, cp);
	memset(cp->sram, 0, cp->sram_size);
	iounmap(cp->sram);
	iounmap(cp->reg);
	kfree(cp);
	cpg = NULL;
	return 0;
}

static struct platform_driver marvell_crypto = {
	.probe		= mv_probe,
	.remove		= mv_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "mv_crypto",
	},
};
MODULE_ALIAS("platform:mv_crypto");

static int __init mv_crypto_init(void)
{
	return platform_driver_register(&marvell_crypto);
}
module_init(mv_crypto_init);

static void __exit mv_crypto_exit(void)
{
	platform_driver_unregister(&marvell_crypto);
}
module_exit(mv_crypto_exit);

MODULE_AUTHOR("Sebastian Andrzej Siewior <sebastian@breakpoint.cc>");
MODULE_DESCRIPTION("Support for Marvell's cryptographic engine");
MODULE_LICENSE("GPL");
