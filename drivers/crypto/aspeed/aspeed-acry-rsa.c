/*
 * Crypto driver for the Aspeed SoC
 *
 * Copyright (C) ASPEED Technology Inc.
 * Ryan Chen <ryan_chen@aspeedtech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "aspeed-acry.h"

// #define ASPEED_RSA_DEBUG

#ifdef ASPEED_RSA_DEBUG
// #define RSA_DBG(fmt, args...) printk(KERN_DEBUG "%s() " fmt, __FUNCTION__, ## args)
#define RSA_DBG(fmt, args...) printk("%s() " fmt, __FUNCTION__, ## args)
#else
#define RSA_DBG(fmt, args...)
#endif

void print_dram(const u8 *buf, int len)
{
#ifdef ASPEED_RSA_DEBUG
	int i;

	for (i = 0; i < len; i++) {
		if (i % 0x10 == 0)
			printk(KERN_CONT "%05x: ", i);
		printk(KERN_CONT "%02x ", buf[i]);
		if ((i - 0xf) % 0x10 == 0)
			printk(KERN_CONT "\n");
	}
	printk(KERN_CONT "\n");
#endif
}

// mode 0 : exponential, mode 1 : modulus, mode 2 : data
void print_sram(u8 *buf, int len, int mode)
{
#ifdef ASPEED_RSA_DEBUG
	int i;

	switch (mode) {
	case 0:
		printk(KERN_CONT "exp\n");
		break;
	case 1:
		printk(KERN_CONT "mod\n");
		break;
	case 2:
		printk(KERN_CONT "data\n");
		break;
	}
	for (i = 0; i < len; i++) {
		if (i % 0x10 == 0)
			printk(KERN_CONT "%05x: ", i);
		switch (mode) {
		case 0:
			// printk(KERN_CONT "%02x:", (exp_dw_mapping[i / 4] * 4) + (i % 4));
			printk(KERN_CONT "%02x ", buf[(exp_dw_mapping[i / 4] * 4) + (i % 4)]);
			break;
		case 1:
			// printk(KERN_CONT "%02x:", (mod_dw_mapping[i / 4] * 4) + (i % 4));
			printk(KERN_CONT "%02x ", buf[(mod_dw_mapping[i / 4] * 4) + (i % 4)]);
			break;
		case 2:
			printk(KERN_CONT "%02x ", buf[data_byte_mapping[i]]);
			break;
		}
		if ((i - 0xf) % 0x10 == 0)
			printk(KERN_CONT "\n");
	}
	printk(KERN_CONT "\n");
#endif
}

int aspeed_acry_rsa_sg_copy_to_buffer(u8 *buf, struct scatterlist *src, size_t nbytes)
{
	int i, j;
	static u8 dram_buffer[2048];

	RSA_DBG("\n");
	scatterwalk_map_and_copy(dram_buffer, src, 0, nbytes, 0);

	i = 0;
	for (j = nbytes - 1; j >= 0; j--) {
		buf[data_byte_mapping[i]] =  dram_buffer[j];
		i++;
	}
	for (; i < 2048; i++)
		buf[data_byte_mapping[i]] = 0;
	// printk("src:\n");
	print_dram(dram_buffer, nbytes);
	print_sram(buf, nbytes, 2);

	return 0;
}

// mode 0 : exponential, mode 1 : modulus
int aspeed_acry_rsa_ctx_copy(void *buf, const void *xbuf, size_t nbytes, int mode)
{
	const uint8_t *src = xbuf;
	unsigned nbits, ndw;
	u32 *dw_buf = (u32 *)buf;
	u32 a;
	int i, j;

	RSA_DBG("\n");
	if (nbytes > 512)
		return -ENOMEM;

	while (nbytes > 0 && src[0] == 0) {
		src++;
		nbytes--;
	}
	nbits = nbytes * 8;
	if (nbytes > 0)
		nbits -= count_leading_zeros(src[0]) - (BITS_PER_LONG - 8);

	print_dram(src, nbytes);

	ndw = DIV_ROUND_UP(nbytes, BYTES_PER_DWORD);

	if (nbytes > 0) {
		i = BYTES_PER_DWORD - nbytes % BYTES_PER_DWORD;
		i %= BYTES_PER_DWORD;
		for (j = ndw; j > 0; j--) {
			a = 0;
			for (; i < BYTES_PER_DWORD; i++) {
				a <<= 8;
				a |= *src++;
			}
			i = 0;
			switch (mode) {
			case 0:
				dw_buf[exp_dw_mapping[j - 1]] = a;
				// printk("map :%x ", exp_dw_mapping[j - 1]);
				break;
			case 1:
				dw_buf[mod_dw_mapping[j - 1]] = a;
				// printk("map :%x ", mod_dw_mapping[j - 1]);
				break;
			}
			// printk("a :%x \n", a);
		}
	}
	print_sram(buf, nbytes, mode);


	return nbits;
}

static int aspeed_acry_rsa_transfer(struct aspeed_acry_dev *acry_dev)
{
	struct akcipher_request *req = acry_dev->akcipher_req;
	struct scatterlist *out_sg = req->dst;
	static u8 dram_buffer[2048];
	u8 *sram_buffer = (u8 *)acry_dev->acry_sram;
	int result_nbytes;
	int leading_zero = 1;
	int i, j;

	RSA_DBG("\n");

	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_AHB_CPU, ASPEED_ACRY_DMA_CMD);
	udelay(1);
	// printk("sram result:\n");
	// print_dram(sram_buffer, ASPEED_ACRY_RSA_MAX_LEN * 3);

	// print_sram(sram_buffer, ASPEED_ACRY_RSA_MAX_LEN, 2);
	i = 0;
	leading_zero = 1;
	result_nbytes = ASPEED_ACRY_RSA_MAX_LEN;
	for (j = ASPEED_ACRY_RSA_MAX_LEN - 1; j >= 0; j--) {
		if (sram_buffer[data_byte_mapping[j]] == 0 && leading_zero) {
			result_nbytes--;
		} else {
			leading_zero = 0;
			dram_buffer[i] = sram_buffer[data_byte_mapping[j]];
			i++;
		}
	}
	// printk("result_nbytes: %d, %d\n", result_nbytes, req->dst_len);
	// print_dram(dram_buffer, result_nbytes);
	if (result_nbytes <= req->dst_len) {
		scatterwalk_map_and_copy(dram_buffer, out_sg, 0, result_nbytes, 1);// TODO check sram DW write
		req->dst_len = result_nbytes;
	} else {
		printk("RSA engine error!\n");
	}
	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_AHB_ENGINE, ASPEED_ACRY_DMA_CMD);

	memzero_explicit(acry_dev->buf_addr, ASPEED_ACRY_BUFF_SIZE);

	return aspeed_acry_complete(acry_dev, 0);
}

static inline int aspeed_acry_rsa_wait_for_data_ready(struct aspeed_acry_dev *acry_dev,
		aspeed_acry_fn_t resume)
{
#if 1
	return -EINPROGRESS;
#else
	u32 isr;

	RSA_DBG("\n");
	do {
		isr = aspeed_acry_read(acry_dev, ASPEED_ACRY_STATUS);
	} while (!(isr & ACRY_RSA_ISR));
	aspeed_acry_write(acry_dev, isr, ASPEED_ACRY_STATUS);
	aspeed_acry_write(acry_dev, 0, ASPEED_ACRY_TRIGGER);
	udelay(2);


	return resume(acry_dev);
#endif
}

int aspeed_acry_rsa_trigger(struct aspeed_acry_dev *acry_dev)
{
	struct akcipher_request *req = acry_dev->akcipher_req;
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = crypto_tfm_ctx(&cipher->base);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;

	int ne;
	int nm;

	RSA_DBG("\n");

	memset(acry_dev->buf_addr, 0, ASPEED_ACRY_BUFF_SIZE);

	aspeed_acry_rsa_sg_copy_to_buffer(acry_dev->buf_addr, req->src, req->src_len);

	nm = aspeed_acry_rsa_ctx_copy(acry_dev->buf_addr, ctx->key.n, ctx->key.n_sz, 1);
	if (ctx->enc) {
		ne = aspeed_acry_rsa_ctx_copy(acry_dev->buf_addr, ctx->key.e, ctx->key.e_sz, 0);
	} else {
		ne = aspeed_acry_rsa_ctx_copy(acry_dev->buf_addr, ctx->key.d, ctx->key.d_sz, 0);
	}

	aspeed_acry_write(acry_dev, acry_dev->buf_dma_addr, ASPEED_ACRY_DMA_SRC_BASE);
	aspeed_acry_write(acry_dev, (ne << 16) + nm, ASPEED_ACRY_RSA_KEY_LEN);
	aspeed_acry_write(acry_dev, DMA_DEST_LEN(0x1800), ASPEED_ACRY_DMA_DEST); //TODO check length
	acry_dev->resume = aspeed_acry_rsa_transfer;

	aspeed_acry_write(acry_dev, ACRY_RSA_ISR, ASPEED_ACRY_INT_MASK);
	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_MODE_RSA |
			  ACRY_CMD_DMA_SRAM_AHB_ENGINE, ASPEED_ACRY_DMA_CMD);

	aspeed_acry_write(acry_dev, ACRY_CMD_RSA_TRIGGER |
			  ACRY_CMD_DMA_RSA_TRIGGER, ASPEED_ACRY_TRIGGER);

	return aspeed_acry_rsa_wait_for_data_ready(acry_dev, aspeed_acry_rsa_transfer);
}

static int aspeed_acry_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;
	struct aspeed_acry_dev *acry_dev = ctx->acry_dev;

	RSA_DBG("\n");
	acry_ctx->trigger = aspeed_acry_rsa_trigger;
	ctx->enc = 1;


	return aspeed_acry_handle_queue(acry_dev, &req->base);

}

static int aspeed_acry_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;
	struct aspeed_acry_dev *acry_dev = ctx->acry_dev;

	RSA_DBG("\n");
	acry_ctx->trigger = aspeed_acry_rsa_trigger;
	ctx->enc = 0;

	return aspeed_acry_handle_queue(acry_dev, &req->base);
}

static int aspeed_acry_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
				  unsigned int keylen, int priv)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;
	int ret;

	RSA_DBG("\n");
	if (priv)
		ret = rsa_parse_priv_key(&ctx->key, key, keylen);
	else
		ret = rsa_parse_pub_key(&ctx->key, key, keylen);
	if (ret)
		return ret;

	// printk("raw_key.n_sz %d, raw_key.e_sz %d, raw_key.d_sz %d, raw_key.p_sz %d, raw_key.q_sz %d, raw_key.dp_sz %d, raw_key.dq_sz %d, raw_key.qinv_sz %d\n",
	//        raw_key.n_sz, raw_key.e_sz, raw_key.d_sz,
	//        raw_key.p_sz, raw_key.q_sz, raw_key.dp_sz,
	//        raw_key.dq_sz, raw_key.qinv_sz);
	if (ctx->key.n_sz > 512)
		return -EINVAL;

	return 0;
}

static int aspeed_acry_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				       unsigned int keylen)
{
	RSA_DBG("\n");

	return aspeed_acry_rsa_setkey(tfm, key, keylen, 0);
}

static int aspeed_acry_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
					unsigned int keylen)
{
	RSA_DBG("\n");

	return aspeed_acry_rsa_setkey(tfm, key, keylen, 1);
}

static unsigned int aspeed_acry_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;

	RSA_DBG("key->n_sz %d\n", ctx->key.n_sz);
	return (ctx->key.n_sz) ? ctx->key.n_sz : -EINVAL;
}

static int aspeed_acry_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_rsa_ctx *ctx = &acry_ctx->ctx.rsa_ctx;
	struct akcipher_alg *alg = __crypto_akcipher_alg(tfm->base.__crt_alg);
	struct aspeed_acry_alg *algt;

	RSA_DBG("\n");

	algt = container_of(alg, struct aspeed_acry_alg, alg.akcipher);

	ctx->acry_dev = algt->acry_dev;

	return 0;
}

static void aspeed_acry_rsa_exit_tfm(struct crypto_akcipher *tfm)
{

}

struct aspeed_acry_alg aspeed_acry_akcipher_algs[] = {
	{
		.alg.akcipher = {
			.encrypt = aspeed_acry_rsa_enc,
			.decrypt = aspeed_acry_rsa_dec,
			.sign = aspeed_acry_rsa_dec,
			.verify = aspeed_acry_rsa_enc,
			.set_pub_key = aspeed_acry_rsa_set_pub_key,
			.set_priv_key = aspeed_acry_rsa_set_priv_key,
			.max_size = aspeed_acry_rsa_max_size,
			.init = aspeed_acry_rsa_init_tfm,
			.exit = aspeed_acry_rsa_exit_tfm,
			.base = {
				.cra_name = "rsa",
				.cra_driver_name = "aspeed-rsa",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct aspeed_acry_ctx),
			},
		},
	},
};

int aspeed_register_acry_rsa_algs(struct aspeed_acry_dev *acry_dev)
{
	int i, err;

	for (i = 0; i < ARRAY_SIZE(aspeed_acry_akcipher_algs); i++) {
		aspeed_acry_akcipher_algs[i].acry_dev = acry_dev;
		err = crypto_register_akcipher(&aspeed_acry_akcipher_algs[i].alg.akcipher);
		if (err)
			return err;
	}
	return 0;
}