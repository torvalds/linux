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

#define ASPEED_ECDSA_DEBUG

#ifdef ASPEED_ECDSA_DEBUG
// #define ECDSA_DBG(fmt, args...) printk(KERN_DEBUG "%s() " fmt, __FUNCTION__, ## args)
#define ECDSA_DBG(fmt, args...) printk("%s() " fmt, __FUNCTION__, ## args)
#else
#define ECDSA_DBG(fmt, args...)
#endif

uint32_t ecc_program[] = {
	0x00480900, 0x00480900, 0x00480900, 0x00480900,
	0x00480980, 0x00480A00, 0x00480A80, 0x00480B00,
	0x00400000, 0x00080080, 0x00100100, 0x00180180,
	0x00200200, 0x00280280, 0x00300300, 0x00380B80,
	0x28000000, 0x28080080, 0x70300000, 0x8002C040,
	0xF8000000, 0xF8000000, 0x401F8000, 0x8001006E,
	0xF8000000, 0xF8000000, 0x40A78000, 0x8001006E,
	0xF8000000, 0xF8000000, 0x580D4380, 0x58248400,
	0x58158480, 0x582CC500, 0x5041C580, 0x80004021,
	0xF8000000, 0xF8000000, 0x50524600, 0x8000405A,
	0xF8000000, 0xF8000000, 0x58108880, 0x580C4680,
	0x486B4680, 0x486B4680, 0x58084700, 0x48738780,
	0x4873C700, 0x58210780, 0x5803C780, 0x4873C700,
	0x58738780, 0x486B4800, 0x507C0900, 0x506C8780,
	0x5873C800, 0x588C4780, 0x487BC780, 0x487BC780,
	0x487BC780, 0x5083C980, 0x5810C780, 0x487BCA00,
	0x58A50A80, 0x58A54B00, 0x80000012, 0xF8000000,
	0xF8000000, 0x58630680, 0x585AC700, 0x5872C780,
	0x583B8800, 0x48840880, 0x506BC400, 0x50444900,
	0x50848400, 0x58430400, 0x584BC880, 0x50444980,
	0x581D0400, 0x5842CA00, 0x58A50A80, 0x58A54B00,
	0x4A07C000, 0x42004000, 0x8001001D, 0xF8000000,
	0xF8000000, 0x58108880, 0x580C4680, 0x486B4680,
	0x486B4680, 0x58084700, 0x48738780, 0x4873C700,
	0x58210780, 0x5803C780, 0x4873C700, 0x58738780,
	0x486B4800, 0x507C0080, 0x50684780, 0x5873C800,
	0x588C4780, 0x487BC780, 0x487BC780, 0x487BC780,
	0x5810C880, 0x5083C100, 0x488C4180, 0x5818C200,
	0x58190280, 0x80003FA1, 0xF8000000, 0xF8000000,
	0x08900800, 0x08980880, 0x08A00900, 0x08A80980,
	0x08B00A00, 0xF8000000, 0xF8000000, 0xF8000000,
	0xF8000000, 0xB8000000, 0xF8000000, 0xF8000000,
	0xF8000000, 0xF8000000, 0x50A50A00, 0x80003FD0,
	0xF8000000, 0xF8000000, 0xF8000000, 0x80003FCC,
	0xF8000000, 0xF8000000, 0x10080900, 0x10100980,
	0x10180A00, 0x10200A80, 0x10280B00, 0x80003FC4,
	0xF8000000, 0xF8000000, 0xF8000000, 0xF8000000
};

void print_buf(const void *buf, int len)
{
	int i;
	const u8 *_buf = buf;

	for (i = 0; i < len; i++) {
		if (i % 0x10 == 0)
			printk(KERN_CONT "%05x: ", i);
		printk(KERN_CONT "%02x ", _buf[i]);
		if ((i - 0xf) % 0x10 == 0)
			printk(KERN_CONT "\n");
	}
	printk(KERN_CONT "\n");
}
EXPORT_SYMBOL_GPL(print_buf);

static int load_ecc_program(struct aspeed_acry_dev *acry_dev)
{
	phys_addr_t  ec_buf_phy;

	ec_buf_phy = virt_to_phys(ecc_program);

	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_MODE_ECC, ASPEED_ACRY_DMA_CMD);
	aspeed_acry_write(acry_dev, ec_buf_phy, ASPEED_ACRY_DMA_SRC_BASE);
	aspeed_acry_write(acry_dev, DMA_DEST_LEN(0x250), ASPEED_ACRY_DMA_DEST);
	aspeed_acry_write(acry_dev, 0x4, ASPEED_ACRY_DRAM_BRUST);
	aspeed_acry_write(acry_dev, 0x0, ASPEED_ACRY_INT_MASK);
	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_ECC_PROG, ASPEED_ACRY_TRIGGER);

	return aspeed_acry_sts_polling(acry_dev, ACRY_DMA_ISR);
}

static int aspeed_acry_ec_resume(struct aspeed_acry_dev *acry_dev)
{
	struct akcipher_request *req = acry_dev->akcipher_req;
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = crypto_tfm_ctx(&cipher->base);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;

	complete(&ctx->completion);
	return 0;
}



int aspeed_acry_ec_trigger(struct aspeed_acry_dev *acry_dev)
{
	struct akcipher_request *req = acry_dev->akcipher_req;
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = crypto_tfm_ctx(&cipher->base);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	unsigned int ndigits = ctx->ndigits;
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	unsigned int curve_id = ctx->curve_id;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);
	u8 *dma_buf = acry_dev->buf_addr;
	u32 p_cmd, e_cmd;
	const u8 one = 1;

	ECDSA_DBG("\n");

	switch (curve_id) {
	/* In FIPS mode only allow P256 and higher */
	case ECC_CURVE_NIST_P192:
		p_cmd = ACRY_ECC_LEN_192;
		e_cmd = ACRY_ECC_P192;
		break;
	case ECC_CURVE_NIST_P256:
		p_cmd = ACRY_ECC_LEN_256;
		e_cmd = ACRY_ECC_P256;
		break;
	default:
		return -EINVAL;
	}

	memset(dma_buf, 0, ASPEED_ACRY_BUFF_SIZE);

	memcpy(dma_buf + ASPEED_EC_X, ctx->x, nbytes);
	memcpy(dma_buf + ASPEED_EC_Y, ctx->y, nbytes);
	memcpy(dma_buf + ASPEED_EC_Z, &one, 1);
	memcpy(dma_buf + ASPEED_EC_Z2, &one, 1);
	memcpy(dma_buf + ASPEED_EC_Z3, &one, 1);
	memcpy(dma_buf + ASPEED_EC_K, ctx->k, nbytes);
	memcpy(dma_buf + ASPEED_EC_P, curve->p, nbytes);
	memcpy(dma_buf + ASPEED_EC_A, curve->a, nbytes);

	acry_dev->resume = aspeed_acry_ec_resume;
	/* write register to trigger engine */
	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_MODE_ECC, ASPEED_ACRY_DMA_CMD);
	aspeed_acry_write(acry_dev, acry_dev->buf_dma_addr, ASPEED_ACRY_DMA_SRC_BASE);
	aspeed_acry_write(acry_dev, DMA_DEST_LEN(0x1800), ASPEED_ACRY_DMA_DEST);
	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_ECC_DATA, ASPEED_ACRY_TRIGGER);
	aspeed_acry_write(acry_dev, 0x0, ASPEED_ACRY_INT_MASK);

	if (aspeed_acry_sts_polling(acry_dev, ACRY_DMA_ISR)) {
		printk("ecc dma timeout");
		return -EINVAL;
	}

	aspeed_acry_write(acry_dev, ACRY_ECC_ISR, ASPEED_ACRY_INT_MASK);
	aspeed_acry_write(acry_dev, p_cmd, ASPEED_ACRY_ECC_P);
	aspeed_acry_write(acry_dev, 0, ASPEED_ACRY_PROGRAM_INDEX);
	aspeed_acry_write(acry_dev, e_cmd, ASPEED_ACRY_CONTROL);
	aspeed_acry_write(acry_dev, ACRY_CMD_ECC_TRIGGER, ASPEED_ACRY_TRIGGER);

	// aspeed_acry_ec_resume(acry_dev); // test
	return -EINPROGRESS;
}

static void aspeed_acry_ec_point_mult(struct akcipher_request *req,
				      struct ecc_point *result, const struct ecc_point *point,
				      const u64 *scalar, u64 *initial_z, const struct ecc_curve *curve,
				      unsigned int ndigits)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	struct aspeed_acry_dev *acry_dev = ctx->acry_dev;
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	u8 *sram_buffer = acry_dev->acry_sram;
	u64 rx[ECC_MAX_DIGITS], ry[ECC_MAX_DIGITS];
	u64 z[ECC_MAX_DIGITS], z2[ECC_MAX_DIGITS];
	u64 z3[ECC_MAX_DIGITS];
	u64 *curve_prime = curve->p;
	int ret;

	ECDSA_DBG("\n");
	init_completion(&ctx->completion);
	/* write data to dma buffer */

	vli_set(ctx->x, point->x, ndigits);
	vli_set(ctx->y, point->y, ndigits);
	vli_set(ctx->k, scalar, ndigits);

	acry_ctx->trigger = aspeed_acry_ec_trigger;
	ret = aspeed_acry_handle_queue(acry_dev, &req->base);
	switch (ret) {
	case 0:
		break;
	case -EINPROGRESS:
	case -EBUSY:
		wait_for_completion(&ctx->completion);
		break;
	default:
		printk("hw haning, using sw");
		ecc_point_mult(result, point, scalar, initial_z, curve, ndigits);
		return;
	}

	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_AHB_CPU, ASPEED_ACRY_DMA_CMD);
	udelay(1);

	memcpy(rx, sram_buffer + 0x300, nbytes);
	memcpy(ry, sram_buffer + 0x330, nbytes);
	memcpy(z, sram_buffer + 0x360, nbytes);
	memcpy(z2, sram_buffer + 0x390, nbytes);
	memcpy(z3, sram_buffer + 0x3c0, nbytes);

	aspeed_acry_write(acry_dev, ACRY_CMD_DMA_SRAM_AHB_ENGINE, ASPEED_ACRY_DMA_CMD);

	memzero_explicit(acry_dev->buf_addr, ASPEED_ACRY_BUFF_SIZE);
	aspeed_acry_complete(acry_dev, 0);

	// vli_mod_inv(z, z, curve_prime, point->ndigits);
	/* (x1, y1) = k x G */
	// ecc_point_mult(x1y1, &curve->g, k, NULL, curve, ndigits);
	// ecc_point_mult(result, point, scalar, initial_z, curve, ndigits);
	/* caculate 1/Z */
	printk("hz\n");
	print_buf(z, nbytes);
	printk("hz-inv\n");

	vli_mod_inv(z, z, curve_prime, point->ndigits);

	print_buf(z, nbytes);

	printk("hpx\n");
	print_buf(rx, nbytes);
	printk("hpy\n");
	print_buf(ry, nbytes);

	vli_mod_inv(z2, z2, curve_prime, point->ndigits);
	vli_mod_inv(z3, z3, curve_prime, point->ndigits);
	vli_mod_mult_fast(result->x, z, rx, curve_prime, ndigits);
	vli_mod_mult_fast(result->y, z2, ry, curve_prime, ndigits);

	printk("hx_res\n");
	print_buf(result->x, nbytes);
	printk("hy_res\n");
	print_buf(result->y, nbytes);
}

void test_c(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	unsigned int ndigits = ctx->ndigits;
	unsigned int curve_id = ctx->curve_id;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);
	struct ecc_point *x1y1 = NULL;
	struct ecc_point *x1y1c = NULL;
	uint32_t k[] = {
		0x58109DB4, 0xE43A1FB8, 0x9103DBBE, 0x83D0DC3A,
		0x1244F0BA, 0xFBF2ABF2, 0x227FD620, 0x882905F1,
		0x00000000, 0x00000000, 0x00000000, 0x00000000
	};

	x1y1 = ecc_alloc_point(ndigits);
	x1y1c = ecc_alloc_point(ndigits);
	/* (x1, y1) = k x G */
	ecc_point_mult(x1y1, &curve->g, (u64 *)k, NULL, curve, ndigits);
	aspeed_acry_ec_point_mult(req, x1y1c, &curve->g, (u64 *)k, NULL, curve, ndigits);
}

static void aspeed_acry_ecdsa_parse_msg(struct akcipher_request *req, u64 *msg,
					unsigned int ndigits)
{
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	unsigned int hash_len, hash_off;
	unsigned char *hash, *msg_ptr;
	int i;

	/*
	 * If hash_len == nbytes:
	 *	copy nbytes from req
	 * If hash_len > nbytes:
	 *	copy left most nbytes from hash ignoring LSBs
	 * If hash_len < nbytes:
	 *	copy hash_len from req and zero remaining bytes
	 *	(nbytes - hash_len)
	 */
	hash_len = req->src[0].length;
	hash_off = hash_len <= nbytes ? 0 : hash_len - nbytes;

	msg_ptr = (unsigned char *)msg;
	hash = sg_virt(&req->src[0]);

	for (i = hash_off; i < hash_len; i++)
		*msg_ptr++ = hash[i];
	for (; i < nbytes; i++)
		*msg_ptr++ = 0;
}

static int aspeed_acry_get_rnd_bytes(u8 *rdata, unsigned int dlen)
{
	int err;

	err = crypto_get_default_rng();
	if (err)
		return err;

	err = crypto_rng_get_bytes(crypto_default_rng, rdata, dlen);
	crypto_put_default_rng();
	return err;
}

static int aspeed_acry_ecdsa_sign(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	unsigned int ndigits = ctx->ndigits;
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	unsigned int curve_id = ctx->curve_id;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);
	struct ecc_point *x1y1 = NULL;
	struct ecc_point *x1y1c = NULL;
	u64 z[ECC_MAX_DIGITS], d[ECC_MAX_DIGITS];
	u64 k[ECC_MAX_DIGITS], k_inv[ECC_MAX_DIGITS];
	u64 r[ECC_MAX_DIGITS], s[ECC_MAX_DIGITS];
	u64 dr[ECC_MAX_DIGITS], zdr[ECC_MAX_DIGITS];
	u8 *r_ptr, *s_ptr;
	int err;

	ECDSA_DBG("\n");
	ctx->sign = 1;
	if (req->dst_len < 2 * nbytes) {
		req->dst_len = 2 * nbytes;
		return -EINVAL;
	}

	if (!curve)
		return -EINVAL;

	aspeed_acry_ecdsa_parse_msg(req, z, ndigits);

	/* d */
	vli_set(d, (const u64 *)ctx->private_key, ndigits);

	/* k */
	err = aspeed_acry_get_rnd_bytes((u8 *)k, nbytes);
	if (err)
		return err;

#if defined(CONFIG_CRYPTO_MANAGER2)
	if (req->info)
		vli_copy_from_buf(k, ndigits, req->info, nbytes);
#endif

	x1y1 = ecc_alloc_point(ndigits);
	x1y1c = ecc_alloc_point(ndigits);
	if (!x1y1)
		return -ENOMEM;

	/* (x1, y1) = k x G */
	// printk("testc.........................\n");
	// test_c(req);
	// printk("testc.........................\n");
	ecc_point_mult(x1y1, &curve->g, k, NULL, curve, ndigits);
	aspeed_acry_ec_point_mult(req, x1y1c, &curve->g, k, NULL, curve, ndigits);

	printk("sx\n");
	print_buf((const u8 *)x1y1->x, nbytes);
	printk("hx\n");
	print_buf((const u8 *)x1y1c->x, nbytes);
	printk("sy\n");
	print_buf((const u8 *)x1y1->y, nbytes);
	printk("hy\n");
	print_buf((const u8 *)x1y1c->y, nbytes);
	/* r = x1 mod n */
	vli_mod(r, x1y1->x, curve->n, ndigits);

	/* k^-1 */
	vli_mod_inv(k_inv, k, curve->n, ndigits);

	/* d . r mod n */
	vli_mod_mult(dr, d, r, curve->n, ndigits);

	/* z + dr mod n */
	vli_mod_add(zdr, z, dr, curve->n, ndigits);

	/* k^-1 . ( z + dr) mod n */
	vli_mod_mult(s, k_inv, zdr, curve->n, ndigits);

	/* write signature (r,s) in dst */
	r_ptr = sg_virt(req->dst);
	s_ptr = (u8 *)sg_virt(req->dst) + nbytes;

	vli_copy_to_buf(r_ptr, nbytes, r, ndigits);
	vli_copy_to_buf(s_ptr, nbytes, s, ndigits);

	req->dst_len = 2 * nbytes;

	ecc_free_point(x1y1);
	return 0;
}

static int aspeed_acry_ecdsa_verify(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;

	unsigned int ndigits = ctx->ndigits;
	unsigned int nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
	unsigned int curve_id = ctx->curve_id;
	const struct ecc_curve *curve = ecc_get_curve(curve_id);
	struct ecc_point *x1y1 = NULL, *x2y2 = NULL, *Q = NULL;
	u64 r[ECC_MAX_DIGITS], s[ECC_MAX_DIGITS], v[ECC_MAX_DIGITS];
	u64 z[ECC_MAX_DIGITS], w[ECC_MAX_DIGITS];
	u64 u1[ECC_MAX_DIGITS], u2[ECC_MAX_DIGITS];
	u64 x1[ECC_MAX_DIGITS], x2[ECC_MAX_DIGITS];
	u64 y1[ECC_MAX_DIGITS], y2[ECC_MAX_DIGITS];
	int ret;

	ECDSA_DBG("\n");
	ctx->sign = 0;

	if (!curve)
		return -EINVAL;

	x1y1 = ecc_alloc_point(ndigits);
	x2y2 = ecc_alloc_point(ndigits);
	Q = ecc_alloc_point(ndigits);
	if (!x1y1 || !x2y2 || !Q) {
		ret = -ENOMEM;
		goto exit;
	}

	aspeed_acry_ecdsa_parse_msg(req, z, ndigits);

	/* Signature r,s */
	vli_copy_from_buf(r, ndigits, sg_virt(&req->src[1]), nbytes);
	vli_copy_from_buf(s, ndigits, sg_virt(&req->src[2]), nbytes);

	/* w = s^-1 mod n */
	vli_mod_inv(w, s, curve->n, ndigits);

	/* u1 = zw mod n */
	vli_mod_mult(u1, z, w, curve->n, ndigits);

	/* u2 = rw mod n */
	vli_mod_mult(u2, r, w, curve->n, ndigits);

	/* u1 . G */
	ecc_point_mult(x1y1, &curve->g, u1, NULL, curve, ndigits);
	// aspeed_acry_ec_point_mult(req, x1y1, &curve->g, u1, NULL, curve, ndigits);

	/* Q=(Qx,Qy) */
	vli_set(Q->x, ctx->Qx, ndigits);
	vli_set(Q->y, ctx->Qy, ndigits);

	/* u2 x Q */
	ecc_point_mult(x2y2, Q, u2, NULL, curve, ndigits);
	// aspeed_acry_ec_point_mult(req, x2y2, Q, u2, NULL, curve, ndigits);

	vli_set(x1, x1y1->x, ndigits);
	vli_set(y1, x1y1->y, ndigits);
	vli_set(x2, x2y2->x, ndigits);
	vli_set(y2, x2y2->y, ndigits);

	/* x1y1 + x2y2 => P + Q; P + Q in x2 y2 */
	ecc_point_add(x1, y1, x2, y2, curve->p, ndigits);

	/* v = x mod n */
	vli_mod(v, x2, curve->n, ndigits);

	/* validate signature */
	ret = vli_cmp(v, r, ndigits) == 0 ? 0 : -EBADMSG;
exit:
	ecc_free_point(x1y1);
	ecc_free_point(x2y2);
	ecc_free_point(Q);

	ECDSA_DBG("verify result:%d\n", ret);
	return ret;
}

int aspeed_acry_ecdsa_dummy_enc(struct akcipher_request *req)
{
	return -EINVAL;
}

int aspeed_acry_ecdsa_dummy_dec(struct akcipher_request *req)
{
	return -EINVAL;
}

int aspeed_acry_ecdsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				  unsigned int keylen)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	struct ecdsa params;
	unsigned int ndigits;
	unsigned int nbytes;
	u8 *params_qx, *params_qy;
	int err = 0;

	ECDSA_DBG("\n");
	if (crypto_ecdsa_parse_pub_key(key, keylen, &params))
		return -EINVAL;

	ndigits = ecdsa_supported_curve(params.curve_id);
	if (!ndigits)
		return -EINVAL;

	err = ecc_is_pub_key_valid(params.curve_id, ndigits,
				   params.key, params.key_size);
	if (err)
		return err;

	ctx->curve_id = params.curve_id;
	ctx->ndigits = ndigits;
	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	params_qx = params.key;
	params_qy = params_qx + ECC_MAX_DIGIT_BYTES;


	vli_copy_from_buf(ctx->Qx, ndigits, params_qx, nbytes);
	vli_copy_from_buf(ctx->Qy, ndigits, params_qy, nbytes);

	memzero_explicit(&params, sizeof(params));
	return 0;
}

int aspeed_acry_ecdsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				   unsigned int keylen)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	struct ecdsa params;
	unsigned int ndigits;
	unsigned int nbytes;

	ECDSA_DBG("\n");
	if (crypto_ecdsa_parse_priv_key(key, keylen, &params))
		return -EINVAL;

	ndigits = ecdsa_supported_curve(params.curve_id);
	if (!ndigits)
		return -EINVAL;

	ctx->curve_id = params.curve_id;
	ctx->ndigits = ndigits;
	nbytes = ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	if (ecc_is_key_valid(ctx->curve_id, ctx->ndigits,
			     (const u64 *)params.key, params.key_size) < 0)
		return -EINVAL;

	vli_copy_from_buf(ctx->private_key, ndigits, params.key, nbytes);

	memzero_explicit(&params, sizeof(params));
	return 0;
}

static unsigned int aspeed_acry_ecdsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	int nbytes = ctx->ndigits << ECC_DIGITS_TO_BYTES_SHIFT;

	/* For r,s */
	return 2 * nbytes;
}

static int aspeed_acry_ecdsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_acry_ctx *acry_ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_acry_ecdsa_ctx *ctx = &acry_ctx->ctx.ecdsa_ctx;
	struct akcipher_alg *alg = __crypto_akcipher_alg(tfm->base.__crt_alg);
	struct aspeed_acry_alg *algt;

	ECDSA_DBG("\n");

	algt = container_of(alg, struct aspeed_acry_alg, alg.akcipher);

	ctx->acry_dev = algt->acry_dev;

	return 0;
}

static void aspeed_acry_ecdsa_exit_tfm(struct crypto_akcipher *tfm)
{

}

struct aspeed_acry_alg aspeed_acry_ecdsa_algs = {
	.alg.akcipher = {
		.encrypt = aspeed_acry_ecdsa_dummy_enc,
		.decrypt = aspeed_acry_ecdsa_dummy_dec,
		.sign = aspeed_acry_ecdsa_sign,
		.verify = aspeed_acry_ecdsa_verify,
		.set_pub_key = aspeed_acry_ecdsa_set_pub_key,
		.set_priv_key = aspeed_acry_ecdsa_set_priv_key,
		.max_size = aspeed_acry_ecdsa_max_size,
		.init = aspeed_acry_ecdsa_init_tfm,
		.exit = aspeed_acry_ecdsa_exit_tfm,
		.base = {
			.cra_name = "ecdsa",
			.cra_driver_name = "aspeed-ecdsa",
			.cra_priority = 300,
			.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
			CRYPTO_ALG_ASYNC |
			CRYPTO_ALG_KERN_DRIVER_ONLY,
			.cra_module = THIS_MODULE,
			.cra_ctxsize = sizeof(struct aspeed_acry_ctx),
		},
	},
};

int aspeed_register_acry_ecdsa_algs(struct aspeed_acry_dev *acry_dev)
{
	int err;

	aspeed_acry_ecdsa_algs.acry_dev = acry_dev;
	err = crypto_register_akcipher(&aspeed_acry_ecdsa_algs.alg.akcipher);
	if (err)
		return err;

	return load_ecc_program(acry_dev);
}