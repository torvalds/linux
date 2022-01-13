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

#include "aspeed-hace.h"

// #define ASPEED_RSA_DEBUG

#ifdef ASPEED_RSA_DEBUG
// #define RSA_DBG(fmt, args...) printk(KERN_DEBUG "%s() " fmt, __FUNCTION__, ## args)
#define RSA_DBG(fmt, args...) printk("%s() " fmt, __FUNCTION__, ## args)
#else
#define RSA_DBG(fmt, args...)
#endif

#define ASPEED_RSA_E_BUFF	0x0
#define ASPEED_RSA_XA_BUFF	0x200
#define ASPEED_RSA_NP_BUFF	0x400
#define ASPEED_RSA_N_BUFF	0x800

#define ASPEED_RSA_KEY_LEN	0x200


#define MAX_TABLE_DW 128

void printA(u32 *X)
{
	int i;

	for (i = 127; i >= 0 ; i--)
		printk(KERN_CONT "%#8.8x ", X[i]);
	printk("\n");
}

int get_bit_numbers(u32 *X)
{
	int i, j;
	int nmsb;

	nmsb = MAX_TABLE_DW * 32;
	for (j = MAX_TABLE_DW - 1; j >= 0; j--) {
		if (X[j] == 0) {
			nmsb -= 32;
		} else {
			for (i = 32 - 1; i >= 0; i--)
				if ((X[j] >> i) & 1) {
					i = 0;
					j = 0;
					break;
				} else {
					nmsb--;
				}
		}
	}
	return (nmsb);
}

void Mul2(u32 *T, int mdwm)
{
	u32 msb, temp;
	int j;

	temp = 0;
	for (j = 0; j < mdwm; j++) {
		msb = (T[j] >> 31) & 1;
		T[j] = (T[j] << 1) | temp;
		temp = msb;
	}
}

void Sub2by32(u32 *Borrow, u32 *Sub, u32 C, u32 S, u32 M)
{
	u64 Sub2;

	Sub2  = (u64)S - (u64)M;
	if (C)
		Sub2 -= (u64)1;

	if ((Sub2 >> 32) > 0)
		*Borrow = 1;
	else
		*Borrow = 0;
	*Sub = (u32)(Sub2 & 0xffffffff);
}

void MCompSub(u32 *S, u32 *M, int mdwm)
{
	int flag;
	int j;
	u32 Borrow, Sub;

	flag = 0;    //0: S>=M, 1: S<M
	for (j = mdwm - 1; j >= 0; j--) {
		if (S[j] > M[j])
			break;
		else if (S[j] < M[j]) {
			flag = 1;
			break;
		};
	}

	if (flag == 0) {
		Borrow = 0;
		for (j = 0; j < mdwm; j++) {
			Sub2by32(&Borrow, &Sub, Borrow, S[j], M[j]);
			S[j] = Sub;
		}
	}

}

void MulRmodM(u32 *X, u32 *M, int nm, int mdwm)
{
	int k;

	RSA_DBG("\n");
	for (k = 0; k < nm; k++) {
		Mul2(X, mdwm);
		MCompSub(X, M, mdwm);
	}
}

void Copy(u32 *D, u32 *S)
{
	int j;

	for (j = 0; j < 256; j++)
		D[j] = S[j];
}

void BNCopyToLN(u8 *dst, const u8 *src, int length)
{
	int i, j;
	u8 *end;
	u8 tmp;

	if (dst == src) {
		end = dst + length;
		for (i = 0; i < length / 2; i++) {
			end--;
			tmp = *dst;
			*dst = *end;
			*end = tmp;
			dst++;
		}
	} else {
		i = length - 1;
		for (j = 0; j < length; j++, i--)
			dst[j] = src[i];
	}
}

int Compare(u32 *X, u32 *Y)
{
	int j;
	int result;

	result = 0;
	for (j = 256 - 1; j >= 0; j--)
		if (X[j] > Y[j]) {
			result =  1;
			break;
		} else if (X[j] < Y[j]) {
			result = -1;
			break;
		}
	return (result);
}

void Add(u32 *X, u32 *Y)
{
	int j;
	u64 t1;
	u64 t2;
	u64 sum;
	u64 carry;

	carry = 0;
	for (j = 0; j < 255; j++) {
		t1 = X[j];
		t2 = Y[j];
		sum = t1 + t2 + carry;
		X[j]  = sum & 0xffffffff;
		carry = (sum >> 32) & 0xffffffff;
	}
	X[255] = carry;
}

int nmsb(u32 *X)
{
	int i, j;
	int nmsb;

	nmsb = 256 * 32;
	for (j = 256 - 1; j >= 0; j--) {
		if (X[j] == 0)
			nmsb -= 32;
		else {
			for (i = 32 - 1; i >= 0; i--)
				if ((X[j] >> i) & 1) {
					i = 0;
					j = 0;
					break;
				} else {
					nmsb--;
				}
		}
	}
	return (nmsb);

}

void ShiftLeftFast(u32 *R, u32 *X, int nx, int ny)
{
	int j;
	u32 cntb;
	u32 shldw, shrbit;
	u32 shloffset;
	u32 bitbuf;

	cntb = nx / 32;
	if ((nx % 32) > 0)
		cntb++;

	shldw  = cntb - ((nx - ny) / 32);

	shrbit = (nx - ny) % 32;
	shloffset = (nx - ny) / 32;
	bitbuf = 0;
	for (j = shldw - 1; j >= 0; j--) {
		if (shrbit == 0) {
			R[j] = (X[shloffset + j] >> shrbit);
			bitbuf = X[shloffset + j];
		} else {
			R[j] = (X[shloffset + j] >> shrbit) | bitbuf;
			bitbuf = X[shloffset + j] << (32 - shrbit);
		}
	}
}

unsigned char Getbit(u32 *X, int k)
{
	unsigned char bit = ((X[k / 32] >> (k % 32)) & 1) & 0xff;
	return bit;
}

void Substrate(u32 *X, u32 *Y)
{
	int j;
	u64 t1;
	u64 t2;
	u64 sum;
	u32 carry;

	carry = 0;
	for (j = 0; j < 255; j++) {
		t1 = X[j];
		t2 = Y[j];
		if (carry)
			sum = t1 - t2 - 1;
		else
			sum = t1 - t2;
		X[j]  = sum & 0xffffffff;
		carry = (sum >> 32) & 0xffffffff;
	}
	//X[255] = 0xffffffff;
	if (carry > 0)
		X[255] = 0xffffffff;
	else
		X[255] = 0x0;
}

void ShiftLeft(u32 *X, int i)
{
	int j;
	int msb;
	int temp;

	msb = i;
	for (j = 0; j < 256; j++) {
		temp = X[j] >> 31;
		X[j] = (X[j] << 1) | msb;
		msb = temp;
	}
}

void Divide(u32 *Q, u32 *R, u32 *X, u32 *Y)
{
	int j;
	int nx, ny;

	nx = nmsb(X);
	ny = nmsb(Y);
	memset(Q, 0, ASPEED_EUCLID_LEN);
	memset(R, 0, ASPEED_EUCLID_LEN);

	ShiftLeftFast(R, X, nx, ny);
	for (j = nx - ny; j >= 0; j--) {
		if (Compare(R, Y) >= 0) {
			Substrate(R, Y);
			ShiftLeft(Q, 1);
		} else {
			ShiftLeft(Q, 0);
		}
		if (j > 0)
			ShiftLeft(R, Getbit(X, j - 1));
	}
}

void Positive(u32 *X)
{
	u32 D0[256];

	memset(D0, 0, ASPEED_EUCLID_LEN);
	Substrate(D0, X);
	memcpy(X, D0, ASPEED_EUCLID_LEN);
}

void MultiplyLSB(u32 *X, u32 *Y)
{
	int i, j;
	u32 T[256];
	u64 t1;
	u64 t2;
	u64 product;
	u32 carry;
	u32 temp;

	memset(T, 0, ASPEED_EUCLID_LEN);
	for (i = 0; i < 128; i++) {
		carry = 0;
		for (j = 0; j < 130; j++) {
			if (i + j < 130) {
				t1 = X[i];
				t2 = Y[j];
				product = t1 * t2 + carry + T[i + j];
				temp = (product >> 32) & 0xffffffff;
				T[i + j] = product & 0xffffffff;
				carry = temp;
			}
		}
	}
	memcpy(X, T, ASPEED_EUCLID_LEN);
}

//x = lastx - q * t;
void CalEucPar(u32 *x, u32 *lastx, u32 *q, u32 *t)
{
	u32 temp[256];

	memcpy(temp, t, ASPEED_EUCLID_LEN);
	memcpy(x, lastx, ASPEED_EUCLID_LEN);
	if (Getbit(temp, 4095)) {
		Positive(temp);
		MultiplyLSB(temp, q);
		Add(x, temp);
	} else {
		MultiplyLSB(temp, q);
		Substrate(x, temp);
	}
}

void Euclid(struct aspeed_rsa_ctx *ctx, u32 *Mp, u32 *M, u32 *S, int nm)
{
	int j;
	u32 *a = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_A);
	u32 *b = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_B);
	u32 *q = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_Q);
	u32 *r = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_R);
	u32 *x = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_X);
	u32 *y = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_Y);
	u32 *lastx = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_LX);
	u32 *lasty = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_LY);
	u32 *t = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_T);
	u32 *D1 = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_D1);

	RSA_DBG("\n");

	memcpy(a, M, ASPEED_EUCLID_LEN);
	memcpy(b, S, ASPEED_EUCLID_LEN);

	memset(D1, 0, ASPEED_EUCLID_LEN);
	D1[0] = 1;
	memset(x, 0, ASPEED_EUCLID_LEN);
	x[0] = 1;
	memset(lastx, 0, ASPEED_EUCLID_LEN);
	memset(y, 0xff, ASPEED_EUCLID_LEN);
	memset(lasty, 0, ASPEED_EUCLID_LEN);
	lasty[0] = 1;

	// step 2
	while (Compare(b, D1) > 0) {
		//q = a div b, r = a mod b
		Divide(q, r, a, b);
		//a = b;
		memcpy(a, b, ASPEED_EUCLID_LEN);
		//b = r;
		memcpy(b, r, ASPEED_EUCLID_LEN);
		memcpy(t, x, ASPEED_EUCLID_LEN);
		//x = lastx - q * x;
		CalEucPar(x, lastx, q, t);
		memcpy(lastx, t, ASPEED_EUCLID_LEN);
		memcpy(t, y, ASPEED_EUCLID_LEN);
		//y = lasty - q * y;
		CalEucPar(y, lasty, q, t);
		memcpy(lasty, t, ASPEED_EUCLID_LEN);
	}
	memset(r, 0, ASPEED_EUCLID_LEN);
	r[0] = 1;
	for (j = 0; j < nm; j++)
		ShiftLeft(r, 0);
	if (Getbit(x, 4095)) {
		Add(x, M);
		Substrate(y, r);
	}
	Positive(y);
	memcpy(Mp, y, ASPEED_EUCLID_LEN);
#if 0
	printk("Euclid Mp\n");
	printA(Mp);
	MultiplyLSB(x, r);
	// printk("Final R*Rp=\n");
	// printA(x);
	MultiplyLSB(y, M);
	// printk("Final M*Mp=\n");
	// printA(y);
	Substrate(x, y);
	// printk("Final R*Rp-M*Mp=\n");
	// printA(x);
	check = Compare(x, D1);
	if (check == 0)
		printk("***PASS for Eculde check\n");
	else
		printk("***FAIL for Eculde check\n");
#endif

}

void RSAgetNp(struct aspeed_rsa_ctx *ctx, struct aspeed_rsa_key *rsa_key)
{
	u32 *S = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_S);
	u32 *N = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_N);
	u32 *Np = (u32 *)(ctx->euclid_ctx + ASPEED_EUCLID_NP);

	RSA_DBG("\n");
	memset(N, 0, ASPEED_EUCLID_LEN);
	memset(Np, 0, ASPEED_EUCLID_LEN);
	memset(S, 0, ASPEED_EUCLID_LEN);
	memcpy(N, rsa_key->n, rsa_key->n_sz);
	rsa_key->nm = get_bit_numbers((u32 *)rsa_key->n);
	if ((rsa_key->nm % 32) > 0)
		rsa_key->dwm = (rsa_key->nm / 32) + 1;
	else
		rsa_key->dwm = (rsa_key->nm / 32);

	rsa_key->mdwm = rsa_key->dwm;
	if ((rsa_key->nm % 32) == 0)
		rsa_key->mdwm++;

	// printk("modulus nm bits %d \n", rsa_key->nm);
	// printk("modulus dwm 4bytes %d \n", rsa_key->dwm);
	// printk("modulus mdwm 4bytes %d \n", rsa_key->mdwm);

	S[0] = 1;
	MulRmodM(S, N, rsa_key->nm, rsa_key->mdwm);

	// calculate Mp, R*1/R - Mp*M = 1
	// Because R div M = 1 rem (R-M), S=R-M, so skip first divide.
	Euclid(ctx, Np, N, S, rsa_key->nm);
	memcpy(rsa_key->np, Np, ASPEED_RSA_KEY_LEN);
	return;
}

int aspeed_hace_rsa_handle_queue(struct aspeed_hace_dev *hace_dev,
				 struct crypto_async_request *new_areq)
{
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;
	struct crypto_async_request *areq, *backlog;
	unsigned long flags;
	int err, ret = 0;

	RSA_DBG("\n");
	spin_lock_irqsave(&rsa_engine->lock, flags);
	if (new_areq)
		ret = crypto_enqueue_request(&rsa_engine->queue, new_areq);
	if (rsa_engine->flags & CRYPTO_FLAGS_BUSY) {
		spin_unlock_irqrestore(&rsa_engine->lock, flags);
		return ret;
	}
	backlog = crypto_get_backlog(&rsa_engine->queue);
	areq = crypto_dequeue_request(&rsa_engine->queue);
	if (areq)
		rsa_engine->flags |= CRYPTO_FLAGS_BUSY;
	spin_unlock_irqrestore(&rsa_engine->lock, flags);

	if (!areq)
		return ret;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);


	rsa_engine->akcipher_req = container_of(areq, struct akcipher_request, base);
	rsa_engine->is_async = (areq != new_areq);

	err = aspeed_hace_rsa_trigger(hace_dev);

	return (rsa_engine->is_async) ? ret : err;
}

static int aspeed_akcipher_complete(struct aspeed_hace_dev *hace_dev, int err)
{
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->akcipher_req;

	RSA_DBG("\n");
	rsa_engine->flags &= ~CRYPTO_FLAGS_BUSY;
	if (rsa_engine->is_async)
		req->base.complete(&req->base, err);

	aspeed_hace_rsa_handle_queue(hace_dev, NULL);

	return err;
}

static int aspeed_akcipher_transfer(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->akcipher_req;
	struct scatterlist *out_sg = req->dst;
	u8 *xa_buff = rsa_engine->rsa_buff + ASPEED_RSA_XA_BUFF;
	int nbytes = 0;
	int err = 0;
	int result_length;

	RSA_DBG("\n");
	result_length = (get_bit_numbers((u32 *)xa_buff) + 7) / 8;
#if 0
	printk("after np\n");
	printA(rsa_key->np);
	printk("after decrypt\n");
	printA(xa_buff);
	printk("result length: %d\n", result_length);
#endif
	BNCopyToLN(xa_buff, xa_buff, result_length);
	nbytes = sg_copy_from_buffer(out_sg, sg_nents(req->dst), xa_buff,
				     req->dst_len);
	if (!nbytes) {
		printk("sg_copy_from_buffer nbytes error \n");
		return -EINVAL;
	}
	return aspeed_akcipher_complete(hace_dev, err);
}

static inline int aspeed_akcipher_wait_for_data_ready(struct aspeed_hace_dev *hace_dev,
		aspeed_hace_fn_t resume)
{
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;

#ifdef CONFIG_CRYPTO_DEV_ASPEED_AKCIPHER_INT
	u32 isr = aspeed_hace_read(hace_dev, ASPEED_HACE_STS);

	RSA_DBG("\n");
	if (unlikely(isr & HACE_RSA_ISR))
		return resume(hace_dev);

	rsa_engine->resume = resume;
	return -EINPROGRESS;
#else
	RSA_DBG("\n");
	while (aspeed_hace_read(hace_dev, ASPEED_HACE_STS) & HACE_RSA_BUSY);
	aspeed_hace_write(hace_dev, 0, ASPEED_HACE_RSA_CMD);
	udelay(2);

	return resume(hace_dev);
#endif
}

int aspeed_hace_rsa_trigger(struct aspeed_hace_dev *hace_dev)
{
	struct aspeed_hace_engine_rsa *rsa_engine = &hace_dev->rsa_engine;
	struct akcipher_request *req = rsa_engine->akcipher_req;
	struct crypto_akcipher *cipher = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = crypto_tfm_ctx(&cipher->base);
	struct scatterlist *in_sg = req->src;
	struct aspeed_rsa_key *rsa_key = &ctx->key;
	int nbytes = 0;
	u8 *xa_buff = rsa_engine->rsa_buff + ASPEED_RSA_XA_BUFF;
	u8 *e_buff = rsa_engine->rsa_buff + ASPEED_RSA_E_BUFF;
	u8 *n_buff = rsa_engine->rsa_buff + ASPEED_RSA_N_BUFF;
	u8 *np_buff = rsa_engine->rsa_buff + ASPEED_RSA_NP_BUFF;

	RSA_DBG("\n");
#if 0
	printk("rsa_buff: \t%x\n", rsa_engine->rsa_buff);
	printk("xa_buff: \t%x\n", xa_buff);
	printk("e_buff: \t%x\n", e_buff);
#endif
	memcpy(n_buff, rsa_key->n, 512);
	memcpy(np_buff, rsa_key->np, 512);
	memset(xa_buff, 0, ASPEED_RSA_KEY_LEN);
	nbytes = sg_copy_to_buffer(in_sg, sg_nents(req->src), xa_buff, req->src_len);
	if (!nbytes || (nbytes != req->src_len)) {
		printk("sg_copy_to_buffer nbytes error \n");
		return -EINVAL;
	}
	BNCopyToLN(xa_buff, xa_buff, nbytes);
#if 0
	printk("copy nbytes %d, req->src_len %d , nb_in_sg %d, nb_out_sg %d \n", nbytes, req->src_len, sg_nents(req->src), sg_nents(req->dst));
	printk("input message:\n");
	printA(xa_buff);
	printk("input M:\n");
	printA((u8 *)(rsa_engine->rsa_buff + ASPEED_RSA_N_BUFF));
	printk("input Mp:\n");
	printA((u8 *)(rsa_engine->rsa_buff + ASPEED_RSA_NP_BUFF));
	printk("ne = %d nm = %d\n", rsa_key->ne, rsa_key->nm);
	printk("ready rsa\n");
#endif
	if (ctx->enc) {
		memcpy(e_buff, rsa_key->e, 512);
		// printk("rsa_key->e: %d\n", rsa_key->ne);
		// printk("input E:\n");
		// printA(e_buff);
		aspeed_hace_write(hace_dev, rsa_key->ne + (rsa_key->nm << 16),
				  ASPEED_HACE_RSA_MD_EXP_BIT);
	} else {
		memcpy(e_buff, rsa_key->d, 512);
		// printk("rsa_key->d: %d\n", rsa_key->nd);
		// printk("input E:\n");
		// printA(e_buff);
		aspeed_hace_write(hace_dev, rsa_key->nd + (rsa_key->nm << 16),
				  ASPEED_HACE_RSA_MD_EXP_BIT);
	}
#ifdef CONFIG_CRYPTO_DEV_ASPEED_AKCIPHER_INT
	aspeed_hace_write(hace_dev,
			  RSA_CMD_SRAM_ENGINE_ACCESSABLE | RSA_CMD_FIRE | RSA_CMD_INT_ENABLE,
			  ASPEED_HACE_RSA_CMD);
	rsa_engine->resume = aspeed_akcipher_transfer;
	return aspeed_akcipher_wait_for_data_ready(hace_dev, aspeed_akcipher_transfer);
#else
	aspeed_hace_write(hace_dev,
			  RSA_CMD_SRAM_ENGINE_ACCESSABLE | RSA_CMD_FIRE,
			  ASPEED_HACE_RSA_CMD);
	return aspeed_akcipher_wait_for_data_ready(hace_dev, aspeed_akcipher_transfer);
#endif

}

static int aspeed_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;

	ctx->enc = 1;
	RSA_DBG("\n");

	return aspeed_hace_rsa_handle_queue(hace_dev, &req->base);

}

static int aspeed_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_hace_dev *hace_dev = ctx->hace_dev;

	ctx->enc = 0;
	RSA_DBG("\n");

	return aspeed_hace_rsa_handle_queue(hace_dev, &req->base);
}



static void aspeed_rsa_free_key(struct aspeed_rsa_key *key)
{
	RSA_DBG("\n");
	kzfree(key->d);
	kzfree(key->e);
	kzfree(key->n);
	kzfree(key->np);

	key->d_sz = 0;
	key->e_sz = 0;
	key->n_sz = 0;

	key->d = NULL;
	key->e = NULL;
	key->n = NULL;
	key->np = NULL;
	return;
}

static int aspeed_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
			     unsigned int keylen, int priv)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key;
	struct aspeed_rsa_key *rsa_key = &ctx->key;
	int ret;

	/* Free the old RSA key if any */
	aspeed_rsa_free_key(rsa_key);

	if (priv)
		ret = rsa_parse_priv_key(&raw_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&raw_key, key, keylen);
	if (ret)
		return ret;
	RSA_DBG("\n");
	// printk("raw_key.n_sz %d, raw_key.e_sz %d, raw_key.d_sz %d, raw_key.p_sz %d, raw_key.q_sz %d, raw_key.dp_sz %d, raw_key.dq_sz %d, raw_key.qinv_sz %d\n",
	// 	raw_key.n_sz, raw_key.e_sz, raw_key.d_sz,
	// 	raw_key.p_sz, raw_key.q_sz, raw_key.dp_sz,
	// 	raw_key.dq_sz, raw_key.qinv_sz);
	if (raw_key.n_sz > ASPEED_RSA_BUFF_SIZE) {
		aspeed_rsa_free_key(rsa_key);
		return -EINVAL;
	}

	if (priv) {
		rsa_key->d = kzalloc(ASPEED_RSA_KEY_LEN, GFP_KERNEL);
		if (!rsa_key->d)
			goto err;
		BNCopyToLN(rsa_key->d, raw_key.d, raw_key.d_sz);
		rsa_key->nd = get_bit_numbers((u32 *)rsa_key->d);
		// printk("D=\n");
		// printA(rsa_key->d);
	}

	rsa_key->e = kzalloc(ASPEED_RSA_KEY_LEN, GFP_KERNEL);
	if (!rsa_key->e)
		goto err;
	BNCopyToLN(rsa_key->e, raw_key.e, raw_key.e_sz);
	rsa_key->ne = get_bit_numbers((u32 *)rsa_key->e);
	// printk("E=\n");
	// printA((u32 *)rsa_key->e);
	rsa_key->n = kzalloc(ASPEED_RSA_KEY_LEN, GFP_KERNEL);
	if (!rsa_key->n)
		goto err;

	rsa_key->n_sz = raw_key.n_sz;

	BNCopyToLN(rsa_key->n, raw_key.n, raw_key.n_sz);

	rsa_key->np = kzalloc(ASPEED_RSA_KEY_LEN, GFP_KERNEL);
	if (!rsa_key->n)
		goto err;
	RSAgetNp(ctx, rsa_key);

	return 0;
err:
	aspeed_rsa_free_key(rsa_key);
	return -ENOMEM;
}

static int aspeed_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				  unsigned int keylen)
{
	RSA_DBG("\n");

	return aspeed_rsa_setkey(tfm, key, keylen, 0);
}

static int aspeed_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				   unsigned int keylen)
{
	RSA_DBG("\n");

	return aspeed_rsa_setkey(tfm, key, keylen, 1);
}

static unsigned int aspeed_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_rsa_key *key = &ctx->key;

	RSA_DBG("key->n_sz %d %x\n", key->n_sz, key->n);
	return (key->n) ? key->n_sz : -EINVAL;
}

static int aspeed_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct akcipher_alg *alg = __crypto_akcipher_alg(tfm->base.__crt_alg);
	struct aspeed_hace_alg *algt;

	RSA_DBG("\n");

	algt = container_of(alg, struct aspeed_hace_alg, alg.akcipher);

	ctx->hace_dev = algt->hace_dev;

	ctx->euclid_ctx = kzalloc(ASPEED_EUCLID_CTX_LEN, GFP_KERNEL);
	return 0;
}

static void aspeed_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct aspeed_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct aspeed_rsa_key *key = &ctx->key;

	RSA_DBG("\n");

	aspeed_rsa_free_key(key);
	kfree(ctx->euclid_ctx);
}

struct aspeed_hace_alg aspeed_akcipher_algs[] = {
	{
		.alg.akcipher = {
			.encrypt = aspeed_rsa_enc,
			.decrypt = aspeed_rsa_dec,
			.sign = aspeed_rsa_dec,
			.verify = aspeed_rsa_enc,
			.set_pub_key = aspeed_rsa_set_pub_key,
			.set_priv_key = aspeed_rsa_set_priv_key,
			.max_size = aspeed_rsa_max_size,
			.init = aspeed_rsa_init_tfm,
			.exit = aspeed_rsa_exit_tfm,
			.base = {
				.cra_name = "rsa",
				.cra_driver_name = "aspeed-rsa",
				.cra_priority = 300,
				.cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
				CRYPTO_ALG_ASYNC |
				CRYPTO_ALG_KERN_DRIVER_ONLY,
				.cra_module = THIS_MODULE,
				.cra_ctxsize = sizeof(struct aspeed_rsa_ctx),
			},
		},
	},
};

int aspeed_register_hace_rsa_algs(struct aspeed_hace_dev *hace_dev)
{
	int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(aspeed_akcipher_algs); i++) {
		aspeed_akcipher_algs[i].hace_dev = hace_dev;
		err = crypto_register_akcipher(&aspeed_akcipher_algs[i].alg.akcipher);
		if (err)
			return err;
	}
	return 0;
}
