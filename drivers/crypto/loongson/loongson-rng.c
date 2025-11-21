// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
/* Copyright (c) 2025 Loongson Technology Corporation Limited. */

#include <linux/crypto.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mfd/loongson-se.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <crypto/internal/rng.h>

#define SE_SEED_SIZE 32

struct loongson_rng_list {
	struct mutex lock;
	struct list_head list;
	int registered;
};

struct loongson_rng {
	u32 used;
	struct loongson_se_engine *engine;
	struct list_head list;
	struct mutex lock;
};

struct loongson_rng_ctx {
	struct loongson_rng *rng;
};

struct loongson_rng_cmd {
	u32 cmd_id;
	union {
		u32 len;
		u32 ret;
	} u;
	u32 seed_off;
	u32 out_off;
	u32 pad[4];
};

static struct loongson_rng_list rng_devices = {
	.lock = __MUTEX_INITIALIZER(rng_devices.lock),
	.list = LIST_HEAD_INIT(rng_devices.list),
};

static int loongson_rng_generate(struct crypto_rng *tfm, const u8 *src,
			  unsigned int slen, u8 *dstn, unsigned int dlen)
{
	struct loongson_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct loongson_rng *rng = ctx->rng;
	struct loongson_rng_cmd *cmd = rng->engine->command;
	int err, len;

	mutex_lock(&rng->lock);
	cmd->seed_off = 0;
	do {
		len = min(dlen, rng->engine->buffer_size);
		cmd = rng->engine->command;
		cmd->u.len = len;
		err = loongson_se_send_engine_cmd(rng->engine);
		if (err)
			break;

		cmd = rng->engine->command_ret;
		if (cmd->u.ret) {
			err = -EIO;
			break;
		}

		memcpy(dstn, rng->engine->data_buffer, len);
		dlen -= len;
		dstn += len;
	} while (dlen > 0);
	mutex_unlock(&rng->lock);

	return err;
}

static int loongson_rng_init(struct crypto_tfm *tfm)
{
	struct loongson_rng_ctx *ctx = crypto_tfm_ctx(tfm);
	struct loongson_rng *rng;
	u32 min_used = U32_MAX;

	mutex_lock(&rng_devices.lock);
	list_for_each_entry(rng, &rng_devices.list, list) {
		if (rng->used < min_used) {
			ctx->rng = rng;
			min_used = rng->used;
		}
	}
	ctx->rng->used++;
	mutex_unlock(&rng_devices.lock);

	return 0;
}

static void loongson_rng_exit(struct crypto_tfm *tfm)
{
	struct loongson_rng_ctx *ctx = crypto_tfm_ctx(tfm);

	mutex_lock(&rng_devices.lock);
	ctx->rng->used--;
	mutex_unlock(&rng_devices.lock);
}

static int loongson_rng_seed(struct crypto_rng *tfm, const u8 *seed,
			     unsigned int slen)
{
	struct loongson_rng_ctx *ctx = crypto_rng_ctx(tfm);
	struct loongson_rng *rng = ctx->rng;
	struct loongson_rng_cmd *cmd;
	int err;

	if (slen < SE_SEED_SIZE)
		return -EINVAL;

	slen = min(slen, rng->engine->buffer_size);

	mutex_lock(&rng->lock);
	cmd = rng->engine->command;
	cmd->u.len = slen;
	cmd->seed_off = rng->engine->buffer_off;
	memcpy(rng->engine->data_buffer, seed, slen);
	err = loongson_se_send_engine_cmd(rng->engine);
	if (err)
		goto out;

	cmd = rng->engine->command_ret;
	if (cmd->u.ret)
		err = -EIO;
out:
	mutex_unlock(&rng->lock);

	return err;
}

static struct rng_alg loongson_rng_alg = {
	.generate = loongson_rng_generate,
	.seed =	loongson_rng_seed,
	.seedsize = SE_SEED_SIZE,
	.base = {
		.cra_name = "stdrng",
		.cra_driver_name = "loongson_stdrng",
		.cra_priority = 300,
		.cra_ctxsize = sizeof(struct loongson_rng_ctx),
		.cra_module = THIS_MODULE,
		.cra_init = loongson_rng_init,
		.cra_exit = loongson_rng_exit,
	},
};

static int loongson_rng_probe(struct platform_device *pdev)
{
	struct loongson_rng_cmd *cmd;
	struct loongson_rng *rng;
	int ret = 0;

	rng = devm_kzalloc(&pdev->dev, sizeof(*rng), GFP_KERNEL);
	if (!rng)
		return -ENOMEM;

	rng->engine = loongson_se_init_engine(pdev->dev.parent, SE_ENGINE_RNG);
	if (!rng->engine)
		return -ENODEV;
	cmd = rng->engine->command;
	cmd->cmd_id = SE_CMD_RNG;
	cmd->out_off = rng->engine->buffer_off;
	mutex_init(&rng->lock);

	mutex_lock(&rng_devices.lock);

	if (!rng_devices.registered) {
		ret = crypto_register_rng(&loongson_rng_alg);
		if (ret) {
			dev_err(&pdev->dev, "failed to register crypto(%d)\n", ret);
			goto out;
		}
		rng_devices.registered = 1;
	}

	list_add_tail(&rng->list, &rng_devices.list);
out:
	mutex_unlock(&rng_devices.lock);

	return ret;
}

static struct platform_driver loongson_rng_driver = {
	.probe		= loongson_rng_probe,
	.driver		= {
		.name	= "loongson-rng",
	},
};
module_platform_driver(loongson_rng_driver);

MODULE_ALIAS("platform:loongson-rng");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yinggang Gu <guyinggang@loongson.cn>");
MODULE_AUTHOR("Qunqin Zhao <zhaoqunqin@loongson.cn>");
MODULE_DESCRIPTION("Loongson Random Number Generator driver");
