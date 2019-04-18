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
	struct sun4i_tfm_ctx *op = crypto_tfm_ctx(tfm);
	struct ahash_alg *alg = __crypto_ahash_alg(tfm->__crt_alg);
	struct sun4i_ss_alg_template *algt;

	memset(op, 0, sizeof(struct sun4i_tfm_ctx));

	algt = container_of(alg, struct sun4i_ss_alg_template, alg.hash);
	op->ss = algt->ss;

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

	memset(op, 0, sizeof(struct sun4i_req_ctx));

	algt = container_of(alg, struct sun4i_ss_alg_template, alg.hash);
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

	if (op->byte_count) {
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

	if (op->byte_count) {
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

#define SS_HASH_UPDATE 1
#define SS_HASH_FINAL 2

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
static int sun4i_hash(struct ahash_request *areq)
{
	/*
	 * i is the total bytes read from SGs, to be compared to areq->nbytes
	 * i is important because we cannot rely on SG length since the sum of
	 * SG->length could be greater than areq->nbytes
	 *
	 * end is the position when we need to stop writing to the device,
	 * to be compared to i
	 *
	 * in_i: advancement in the current SG
	 */
	unsigned int i = 0, end, fill, min_fill, nwait, nbw = 0, j = 0, todo;
	unsigned int in_i = 0;
	u32 spaces, rx_cnt = SS_RX_DEFAULT, bf[32] = {0}, wb = 0, v, ivmode = 0;
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(areq);
	struct sun4i_tfm_ctx *tfmctx = crypto_ahash_ctx(tfm);
	struct sun4i_ss_ctx *ss = tfmctx->ss;
	struct scatterlist *in_sg = areq->src;
	struct sg_mapping_iter mi;
	int in_r, err = 0;
	size_t copied = 0;

	dev_dbg(ss->dev, "%s %s bc=%llu len=%u mode=%x wl=%u h0=%0x",
		__func__, crypto_tfm_alg_name(areq->base.tfm),
		op->byte_count, areq->nbytes, op->mode,
		op->len, op->hash[0]);

	if (unlikely(!areq->nbytes) && !(op->flags & SS_HASH_FINAL))
		return 0;

	/* protect against overflow */
	if (unlikely(areq->nbytes > UINT_MAX - op->len)) {
		dev_err(ss->dev, "Cannot process too large request\n");
		return -EINVAL;
	}

	if (op->len + areq->nbytes < 64 && !(op->flags & SS_HASH_FINAL)) {
		/* linearize data to op->buf */
		copied = sg_pcopy_to_buffer(areq->src, sg_nents(areq->src),
					    op->buf + op->len, areq->nbytes, 0);
		op->len += copied;
		return 0;
	}

	spin_lock_bh(&ss->slock);

	/*
	 * if some data have been processed before,
	 * we need to restore the partial hash state
	 */
	if (op->byte_count) {
		ivmode = SS_IV_ARBITRARY;
		for (i = 0; i < 5; i++)
			writel(op->hash[i], ss->base + SS_IV0 + i * 4);
	}
	/* Enable the device */
	writel(op->mode | SS_ENABLED | ivmode, ss->base + SS_CTL);

	if (!(op->flags & SS_HASH_UPDATE))
		goto hash_final;

	/* start of handling data */
	if (!(op->flags & SS_HASH_FINAL)) {
		end = ((areq->nbytes + op->len) / 64) * 64 - op->len;

		if (end > areq->nbytes || areq->nbytes - end > 63) {
			dev_err(ss->dev, "ERROR: Bound error %u %u\n",
				end, areq->nbytes);
			err = -EINVAL;
			goto release_ss;
		}
	} else {
		/* Since we have the flag final, we can go up to modulo 4 */
		if (areq->nbytes < 4)
			end = 0;
		else
			end = ((areq->nbytes + op->len) / 4) * 4 - op->len;
	}

	/* TODO if SGlen % 4 and !op->len then DMA */
	i = 1;
	while (in_sg && i == 1) {
		if (in_sg->length % 4)
			i = 0;
		in_sg = sg_next(in_sg);
	}
	if (i == 1 && !op->len && areq->nbytes)
		dev_dbg(ss->dev, "We can DMA\n");

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
		if (op->len || (mi.length - in_i) < 4) {
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
			if (op->len > 3 && !(op->len % 4)) {
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
			if (!rx_cnt) {
				spaces = readl(ss->base + SS_FCSR);
				rx_cnt = SS_RXFIFO_SPACES(spaces);
			}
			if (in_i == mi.length) {
				sg_miter_next(&mi);
				in_i = 0;
			}
		}
	} while (i < end);

	/*
	 * Now we have written to the device all that we can,
	 * store the remaining bytes in op->buf
	 */
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

	/*
	 * End of data process
	 * Now if we have the flag final go to finalize part
	 * If not, store the partial hash
	 */
	if (op->flags & SS_HASH_FINAL)
		goto hash_final;

	writel(op->mode | SS_ENABLED | SS_DATA_END, ss->base + SS_CTL);
	i = 0;
	do {
		v = readl(ss->base + SS_CTL);
		i++;
	} while (i < SS_TIMEOUT && (v & SS_DATA_END));
	if (unlikely(i >= SS_TIMEOUT)) {
		dev_err_ratelimited(ss->dev,
				    "ERROR: hash end timeout %d>%d ctl=%x len=%u\n",
				    i, SS_TIMEOUT, v, areq->nbytes);
		err = -EIO;
		goto release_ss;
	}

	/*
	 * The datasheet isn't very clear about when to retrieve the digest. The
	 * bit SS_DATA_END is cleared when the engine has processed the data and
	 * when the digest is computed *but* it doesn't mean the digest is
	 * available in the digest registers. Hence the delay to be sure we can
	 * read it.
	 */
	ndelay(1);

	for (i = 0; i < crypto_ahash_digestsize(tfm) / 4; i++)
		op->hash[i] = readl(ss->base + SS_MD0 + i * 4);

	goto release_ss;

/*
 * hash_final: finalize hashing operation
 *
 * If we have some remaining bytes, we write them.
 * Then ask the SS for finalizing the hashing operation
 *
 * I do not check RX FIFO size in this function since the size is 32
 * after each enabling and this function neither write more than 32 words.
 * If we come from the update part, we cannot have more than
 * 3 remaining bytes to write and SS is fast enough to not care about it.
 */

hash_final:

	/* write the remaining words of the wait buffer */
	if (op->len) {
		nwait = op->len / 4;
		if (nwait) {
			writesl(ss->base + SS_RXFIFO, op->buf, nwait);
			op->byte_count += 4 * nwait;
		}

		nbw = op->len - 4 * nwait;
		if (nbw) {
			wb = *(u32 *)(op->buf + nwait * 4);
			wb &= GENMASK((nbw * 8) - 1, 0);

			op->byte_count += nbw;
		}
	}

	/* write the remaining bytes of the nbw buffer */
	wb |= ((1 << 7) << (nbw * 8));
	bf[j++] = wb;

	/*
	 * number of space to pad to obtain 64o minus 8(size) minus 4 (final 1)
	 * I take the operations from other MD5/SHA1 implementations
	 */

	/* last block size */
	fill = 64 - (op->byte_count % 64);
	min_fill = 2 * sizeof(u32) + (nbw ? 0 : sizeof(u32));

	/* if we can't fill all data, jump to the next 64 block */
	if (fill < min_fill)
		fill += 64;

	j += (fill - min_fill) / sizeof(u32);

	/* write the length of data */
	if (op->mode == SS_OP_SHA1) {
		__be64 bits = cpu_to_be64(op->byte_count << 3);
		bf[j++] = lower_32_bits(bits);
		bf[j++] = upper_32_bits(bits);
	} else {
		__le64 bits = op->byte_count << 3;
		bf[j++] = lower_32_bits(bits);
		bf[j++] = upper_32_bits(bits);
	}
	writesl(ss->base + SS_RXFIFO, bf, j);

	/* Tell the SS to stop the hashing */
	writel(op->mode | SS_ENABLED | SS_DATA_END, ss->base + SS_CTL);

	/*
	 * Wait for SS to finish the hash.
	 * The timeout could happen only in case of bad overclocking
	 * or driver bug.
	 */
	i = 0;
	do {
		v = readl(ss->base + SS_CTL);
		i++;
	} while (i < SS_TIMEOUT && (v & SS_DATA_END));
	if (unlikely(i >= SS_TIMEOUT)) {
		dev_err_ratelimited(ss->dev,
				    "ERROR: hash end timeout %d>%d ctl=%x len=%u\n",
				    i, SS_TIMEOUT, v, areq->nbytes);
		err = -EIO;
		goto release_ss;
	}

	/*
	 * The datasheet isn't very clear about when to retrieve the digest. The
	 * bit SS_DATA_END is cleared when the engine has processed the data and
	 * when the digest is computed *but* it doesn't mean the digest is
	 * available in the digest registers. Hence the delay to be sure we can
	 * read it.
	 */
	ndelay(1);

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

int sun4i_hash_final(struct ahash_request *areq)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);

	op->flags = SS_HASH_FINAL;
	return sun4i_hash(areq);
}

int sun4i_hash_update(struct ahash_request *areq)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);

	op->flags = SS_HASH_UPDATE;
	return sun4i_hash(areq);
}

/* sun4i_hash_finup: finalize hashing operation after an update */
int sun4i_hash_finup(struct ahash_request *areq)
{
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);

	op->flags = SS_HASH_UPDATE | SS_HASH_FINAL;
	return sun4i_hash(areq);
}

/* combo of init/update/final functions */
int sun4i_hash_digest(struct ahash_request *areq)
{
	int err;
	struct sun4i_req_ctx *op = ahash_request_ctx(areq);

	err = sun4i_hash_init(areq);
	if (err)
		return err;

	op->flags = SS_HASH_UPDATE | SS_HASH_FINAL;
	return sun4i_hash(areq);
}
