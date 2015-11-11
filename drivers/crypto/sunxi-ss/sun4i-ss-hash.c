/*
 * sun4i-ss-hash.c - hardware cryptographic accelerator for Allwinner A20 SoC
 *
 * Copyright (C) 2013-2015 Corentin LABBE <clabbe.montjoie@gmail.com>
 *
 * This file add support for MD5 and SHA1.
 *
 * You could find the datasheet in Documentation/arm/sunxi/README
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "sun4i-ss.h"
#include <linux/scatterlist.h>

/* This is a totally arbitrary value */
#define SS_TIMEOUT 100

int sun4i_hash_crainit(struct crypto_tfm *tfm)
{
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct sun4i_req_ctx));
	return 0;
}

/* sun4i_hash_init: initialize request context */
int sun4i_hash_init(struct ahash_request *areq)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->base.__crt_alg);
	struct sun4i_ss_alg_template *algt;
	struct sun4i_ss_ctx *ss;

	memset(op, 0, sizeof(struct sun4i_req_ctx));

	algt = container_of(alg, struct sun4i_ss_alg_template, alg.hash);
	ss = algt->ss;
	op->ss = algt->ss;
	op->mode = algt->mode;

	return 0;
}

int sun4i_hash_export_md5(struct ahash_request *areq, void *out)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct md5_state *octx = out;
	int i;

	octx->byte_count = op->byte_count + op->len;

	memcpy(octx->block, op->buf, op->len);

	if (op->byte_count > 0) {
		for (i = 0; i < 4; i++)
			octx->hash[i] = op->hash[i];
	} else {
		octx->hash[0] = SHA1_H0;
		octx->hash[1] = SHA1_H1;
		octx->hash[2] = SHA1_H2;
		octx->hash[3] = SHA1_H3;
	}

	return 0;
}

int sun4i_hash_import_md5(struct ahash_request *areq, const void *in)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	const struct md5_state *ictx = in;
	int i;

	sun4i_hash_init(areq);

	op->byte_count = ictx->byte_count & ~0x3F;
	op->len = ictx->byte_count & 0x3F;

	memcpy(op->buf, ictx->block, op->len);

	for (i = 0; i < 4; i++)
		op->hash[i] = ictx->hash[i];

	return 0;
}

int sun4i_hash_export_sha1(struct ahash_request *areq, void *out)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct sha1_state *octx = out;
	int i;

	octx->count = op->byte_count + op->len;

	memcpy(octx->buffer, op->buf, op->len);

	if (op->byte_count > 0) {
		for (i = 0; i < 5; i++)
			octx->state[i] = op->hash[i];
	} else {
		octx->state[0] = SHA1_H0;
		octx->state[1] = SHA1_H1;
		octx->state[2] = SHA1_H2;
		octx->state[3] = SHA1_H3;
		octx->state[4] = SHA1_H4;
	}

	return 0;
}

int sun4i_hash_import_sha1(struct ahash_request *areq, const void *in)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	const struct sha1_state *ictx = in;
	int i;

	sun4i_hash_init(areq);

	op->byte_count = ictx->count & ~0x3F;
	op->len = ictx->count & 0x3F;

	memcpy(op->buf, ictx->buffer, op->len);

	for (i = 0; i < 5; i++)
		op->hash[i] = ictx->state[i];

	return 0;
}

/*
 * sun4i_hash_update: update hash engine
 *
 * Could be used for both SHA1 and MD5
 * Write data by step of 32bits and put then in the SS.
 *
 * Since we cannot leave partial data and hash state in the engine,
 * we need to get the hash state at the end of this function.
 * We can get the hash state every 64 bytes
 *
 * So the first work is to get the number of bytes to write to SS modulo 64
 * The extra bytes will go to a temporary buffer op->buf storing op->len bytes
 *
 * So at the begin of update()
 * if op->len + areq->nbytes < 64
 * => all data will be written to wait buffer (op->buf) and end=0
 * if not, write all data from op->buf to the device and position end to
 * complete to 64bytes
 *
 * example 1:
 * update1 60o => op->len=60
 * update2 60o => need one more word to have 64 bytes
 * end=4
 * so write all data from op->buf and one word of SGs
 * write remaining data in op->buf
 * final state op->len=56
 */
int sun4i_hash_update(struct ahash_request *areq)
{
	u32 v, ivmode = 0;
	unsigned int i = 0;
	/*
	 * i is the total bytes read from SGs, to be compared to areq->nbytes
	 * i is important because we cannot rely on SG length since the sum of
	 * SG->length could be greater than areq->nbytes
	 */

	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct sun4i_ss_ctx *ss = op->ss;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	unsigned int in_i = 0; /* advancement in the current SG */
	unsigned int end;
	/*
	 * end is the position when we need to stop writing to the device,
	 * to be compared to i
	 */
	int in_r, err = 0;
	unsigned int todo;
	u32 spaces, rx_cnt = SS_RX_DEFAULT;
	size_t copied = 0;
	struct sg_mapping_iter mi;

	dev_dbg(ss->dev, "%s %s bc=%llu len=%u mode=%x wl=%u h0=%0x",
		__func__, crypto_tfm_alg_name(areq->base.tfm),
		op->byte_count, areq->nbytes, op->mode,
		op->len, op->hash[0]);

	if (areq->nbytes == 0)
		return 0;

	/* protect against overflow */
	if (areq->nbytes > UINT_MAX - op->len) {
		dev_err(ss->dev, "Cannot process too large request\n");
		return -EINVAL;
	}

	if (op->len + areq->nbytes < 64) {
		/* linearize data to op->buf */
		copied = sg_pcopy_to_buffer(areq->src, sg_nents(areq->src),
					    op->buf + op->len, areq->nbytes, 0);
		op->len += copied;
		return 0;
	}

	end = ((areq->nbytes + op->len) / 64) * 64 - op->len;

	if (end > areq->nbytes || areq->nbytes - end > 63) {
		dev_err(ss->dev, "ERROR: Bound error %u %u\n",
			end, areq->nbytes);
		return -EINVAL;
	}

	spin_lock_bh(&ss->slock);

	/*
	 * if some data have been processed before,
	 * we need to restore the partial hash state
	 */
	if (op->byte_count > 0) {
		ivmode = SS_IV_ARBITRARY;
		for (i = 0; i < 5; i++)
			writel(op->hash[i], ss->base + SS_IV0 + i * 4);
	}
	/* Enable the device */
	writel(op->mode | SS_ENABLED | ivmode, ss->base + SS_CTL);

	i = 0;
	sg_miter_start(&mi, areq->src, sg_nents(areq->src),
		       SG_MITER_FROM_SG | SG_MITER_ATOMIC);
	sg_miter_next(&mi);
	in_i = 0;

	do {
		/*
		 * we need to linearize in two case:
		 * - the buffer is already used
		 * - the SG does not have enough byte remaining ( < 4)
		 */
		if (op->len > 0 || (mi.length - in_i) < 4) {
			/*
			 * if we have entered here we have two reason to stop
			 * - the buffer is full
			 * - reach the end
			 */
			while (op->len < 64 && i < end) {
				/* how many bytes we can read from current SG */
				in_r = min3(mi.length - in_i, end - i,
					    64 - op->len);
				memcpy(op->buf + op->len, mi.addr + in_i, in_r);
				op->len += in_r;
				i += in_r;
				in_i += in_r;
				if (in_i == mi.length) {
					sg_miter_next(&mi);
					in_i = 0;
				}
			}
			if (op->len > 3 && (op->len % 4) == 0) {
				/* write buf to the device */
				writesl(ss->base + SS_RXFIFO, op->buf,
					op->len / 4);
				op->byte_count += op->len;
				op->len = 0;
			}
		}
		if (mi.length - in_i > 3 && i < end) {
			/* how many bytes we can read from current SG */
			in_r = min3(mi.length - in_i, areq->nbytes - i,
				    ((mi.length - in_i) / 4) * 4);
			/* how many bytes we can write in the device*/
			todo = min3((u32)(end - i) / 4, rx_cnt, (u32)in_r / 4);
			writesl(ss->base + SS_RXFIFO, mi.addr + in_i, todo);
			op->byte_count += todo * 4;
			i += todo * 4;
			in_i += todo * 4;
			rx_cnt -= todo;
			if (rx_cnt == 0) {
				spaces = readl(ss->base + SS_FCSR);
				rx_cnt = SS_RXFIFO_SPACES(spaces);
			}
			if (in_i == mi.length) {
				sg_miter_next(&mi);
				in_i = 0;
			}
		}
	} while (i < end);
	/* final linear */
	if ((areq->nbytes - i) < 64) {
		while (i < areq->nbytes && in_i < mi.length && op->len < 64) {
			/* how many bytes we can read from current SG */
			in_r = min3(mi.length - in_i, areq->nbytes - i,
				    64 - op->len);
			memcpy(op->buf + op->len, mi.addr + in_i, in_r);
			op->len += in_r;
			i += in_r;
			in_i += in_r;
			if (in_i == mi.length) {
				sg_miter_next(&mi);
				in_i = 0;
			}
		}
	}

	sg_miter_stop(&mi);

	writel(op->mode | SS_ENABLED | SS_DATA_END, ss->base + SS_CTL);
	i = 0;
	do {
		v = readl(ss->base + SS_CTL);
		i++;
	} while (i < SS_TIMEOUT && (v & SS_DATA_END) > 0);
	if (i >= SS_TIMEOUT) {
		dev_err_ratelimited(ss->dev,
				    "ERROR: hash end timeout %d>%d ctl=%x len=%u\n",
				    i, SS_TIMEOUT, v, areq->nbytes);
		err = -EIO;
		goto release_ss;
	}

	/* get the partial hash only if something was written */
	for (i = 0; i < crypto_ahash_digestsize(tfm) / 4; i++)
		op->hash[i] = readl(ss->base + SS_MD0 + i * 4);

release_ss:
	writel(0, ss->base + SS_CTL);
	spin_unlock_bh(&ss->slock);
	return err;
}

/*
 * sun4i_hash_final: finalize hashing operation
 *
 * If we have some remaining bytes, we write them.
 * Then ask the SS for finalizing the hashing operation
 *
 * I do not check RX FIFO size in this function since the size is 32
 * after each enabling and this function neither write more than 32 words.
 */
int sun4i_hash_final(struct ahash_request *areq)
{
	u32 v, ivmode = 0;
	unsigned int i;
	unsigned int j = 0;
	int zeros, err = 0;
	unsigned int index, padlen;
	__be64 bits;
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct sun4i_ss_ctx *ss = op->ss;
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	u32 bf[32];
	u32 wb = 0;
	unsigned int nwait, nbw = 0;

	dev_dbg(ss->dev, "%s: byte=%llu len=%u mode=%x wl=%u h=%x",
		__func__, op->byte_count, areq->nbytes, op->mode,
		op->len, op->hash[0]);

	spin_lock_bh(&ss->slock);

	/*
	 * if we have already written something,
	 * restore the partial hash state
	 */
	if (op->byte_count > 0) {
		ivmode = SS_IV_ARBITRARY;
		for (i = 0; i < crypto_ahash_digestsize(tfm) / 4; i++)
			writel(op->hash[i], ss->base + SS_IV0 + i * 4);
	}
	writel(op->mode | SS_ENABLED | ivmode, ss->base + SS_CTL);

	/* write the remaining words of the wait buffer */
	if (op->len > 0) {
		nwait = op->len / 4;
		if (nwait > 0) {
			writesl(ss->base + SS_RXFIFO, op->buf, nwait);
			op->byte_count += 4 * nwait;
		}
		nbw = op->len - 4 * nwait;
		wb = *(u32 *)(op->buf + nwait * 4);
		wb &= (0xFFFFFFFF >> (4 - nbw) * 8);
	}

	/* write the remaining bytes of the nbw buffer */
	if (nbw > 0) {
		wb |= ((1 << 7) << (nbw * 8));
		bf[j++] = wb;
	} else {
		bf[j++] = 1 << 7;
	}

	/*
	 * number of space to pad to obtain 64o minus 8(size) minus 4 (final 1)
	 * I take the operations from other MD5/SHA1 implementations
	 */

	/* we have already send 4 more byte of which nbw data */
	if (op->mode == SS_OP_MD5) {
		index = (op->byte_count + 4) & 0x3f;
		op->byte_count += nbw;
		if (index > 56)
			zeros = (120 - index) / 4;
		else
			zeros = (56 - index) / 4;
	} else {
		op->byte_count += nbw;
		index = op->byte_count & 0x3f;
		padlen = (index < 56) ? (56 - index) : ((64 + 56) - index);
		zeros = (padlen - 1) / 4;
	}

	memset(bf + j, 0, 4 * zeros);
	j += zeros;

	/* write the length of data */
	if (op->mode == SS_OP_SHA1) {
		bits = cpu_to_be64(op->byte_count << 3);
		bf[j++] = bits & 0xffffffff;
		bf[j++] = (bits >> 32) & 0xffffffff;
	} else {
		bf[j++] = (op->byte_count << 3) & 0xffffffff;
		bf[j++] = (op->byte_count >> 29) & 0xffffffff;
	}
	writesl(ss->base + SS_RXFIFO, bf, j);

	/* Tell the SS to stop the hashing */
	writel(op->mode | SS_ENABLED | SS_DATA_END, ss->base + SS_CTL);

	/*
	 * Wait for SS to finish the hash.
	 * The timeout could happen only in case of bad overcloking
	 * or driver bug.
	 */
	i = 0;
	do {
		v = readl(ss->base + SS_CTL);
		i++;
	} while (i < SS_TIMEOUT && (v & SS_DATA_END) > 0);
	if (i >= SS_TIMEOUT) {
		dev_err_ratelimited(ss->dev,
				    "ERROR: hash end timeout %d>%d ctl=%x len=%u\n",
				    i, SS_TIMEOUT, v, areq->nbytes);
		err = -EIO;
		goto release_ss;
	}

	/* Get the hash from the device */
	if (op->mode == SS_OP_SHA1) {
		for (i = 0; i < 5; i++) {
			v = cpu_to_be32(readl(ss->base + SS_MD0 + i * 4));
			memcpy(areq->result + i * 4, &v, 4);
		}
	} else {
		for (i = 0; i < 4; i++) {
			v = readl(ss->base + SS_MD0 + i * 4);
			memcpy(areq->result + i * 4, &v, 4);
		}
	}

release_ss:
	writel(0, ss->base + SS_CTL);
	spin_unlock_bh(&ss->slock);
	return err;
}

/* sun4i_hash_finup: finalize hashing operation after an update */
int sun4i_hash_finup(struct ahash_request *areq)
{
	int err;

	err = sun4i_hash_update(areq);
	if (err != 0)
		return err;

	return sun4i_hash_final(areq);
}

/* combo of init/update/final functions */
int sun4i_hash_digest(struct ahash_request *areq)
{
	int err;

	err = sun4i_hash_init(areq);
	if (err != 0)
		return err;

	err = sun4i_hash_update(areq);
	if (err != 0)
		return err;

	return sun4i_hash_final(areq);
}
