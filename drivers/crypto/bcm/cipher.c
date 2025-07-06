// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Broadcom
 */

#include <linux/err.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <linux/kthread.h>
#include <linux/rtnetlink.h>
#include <linux/sched.h>
#include <linux/string_choices.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/bitops.h>

#include <crypto/algapi.h>
#include <crypto/aead.h>
#include <crypto/internal/aead.h>
#include <crypto/aes.h>
#include <crypto/internal/des.h>
#include <crypto/hmac.h>
#include <crypto/md5.h>
#include <crypto/authenc.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/sha3.h>

#include "util.h"
#include "cipher.h"
#include "spu.h"
#include "spum.h"
#include "spu2.h"

/* ================= Device Structure ================== */

struct bcm_device_private iproc_priv;

/* ==================== Parameters ===================== */

int flow_debug_logging;
module_param(flow_debug_logging, int, 0644);
MODULE_PARM_DESC(flow_debug_logging, "Enable Flow Debug Logging");

int packet_debug_logging;
module_param(packet_debug_logging, int, 0644);
MODULE_PARM_DESC(packet_debug_logging, "Enable Packet Debug Logging");

int debug_logging_sleep;
module_param(debug_logging_sleep, int, 0644);
MODULE_PARM_DESC(debug_logging_sleep, "Packet Debug Logging Sleep");

/*
 * The value of these module parameters is used to set the priority for each
 * algo type when this driver registers algos with the kernel crypto API.
 * To use a priority other than the default, set the priority in the insmod or
 * modprobe. Changing the module priority after init time has no effect.
 *
 * The default priorities are chosen to be lower (less preferred) than ARMv8 CE
 * algos, but more preferred than generic software algos.
 */
static int cipher_pri = 150;
module_param(cipher_pri, int, 0644);
MODULE_PARM_DESC(cipher_pri, "Priority for cipher algos");

static int hash_pri = 100;
module_param(hash_pri, int, 0644);
MODULE_PARM_DESC(hash_pri, "Priority for hash algos");

static int aead_pri = 150;
module_param(aead_pri, int, 0644);
MODULE_PARM_DESC(aead_pri, "Priority for AEAD algos");

/* A type 3 BCM header, expected to precede the SPU header for SPU-M.
 * Bits 3 and 4 in the first byte encode the channel number (the dma ringset).
 * 0x60 - ring 0
 * 0x68 - ring 1
 * 0x70 - ring 2
 * 0x78 - ring 3
 */
static char BCMHEADER[] = { 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28 };
/*
 * Some SPU hw does not use BCM header on SPU messages. So BCM_HDR_LEN
 * is set dynamically after reading SPU type from device tree.
 */
#define BCM_HDR_LEN  iproc_priv.bcm_hdr_len

/* min and max time to sleep before retrying when mbox queue is full. usec */
#define MBOX_SLEEP_MIN  800
#define MBOX_SLEEP_MAX 1000

/**
 * select_channel() - Select a SPU channel to handle a crypto request. Selects
 * channel in round robin order.
 *
 * Return:  channel index
 */
static u8 select_channel(void)
{
	u8 chan_idx = atomic_inc_return(&iproc_priv.next_chan);

	return chan_idx % iproc_priv.spu.num_chan;
}

/**
 * spu_skcipher_rx_sg_create() - Build up the scatterlist of buffers used to
 * receive a SPU response message for an skcipher request. Includes buffers to
 * catch SPU message headers and the response data.
 * @mssg:	mailbox message containing the receive sg
 * @rctx:	crypto request context
 * @rx_frag_num: number of scatterlist elements required to hold the
 *		SPU response message
 * @chunksize:	Number of bytes of response data expected
 * @stat_pad_len: Number of bytes required to pad the STAT field to
 *		a 4-byte boundary
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Returns:
 *   0 if successful
 *   < 0 if an error
 */
static int
spu_skcipher_rx_sg_create(struct brcm_message *mssg,
			    struct iproc_reqctx_s *rctx,
			    u8 rx_frag_num,
			    unsigned int chunksize, u32 stat_pad_len)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 datalen;		/* Number of bytes of response data expected */

	mssg->spu.dst = kmalloc_array(rx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (!mssg->spu.dst)
		return -ENOMEM;

	sg = mssg->spu.dst;
	sg_init_table(sg, rx_frag_num);
	/* Space for SPU message header */
	sg_set_buf(sg++, rctx->msg_buf.spu_resp_hdr, ctx->spu_resp_hdr_len);

	/* If XTS tweak in payload, add buffer to receive encrypted tweak */
	if ((ctx->cipher.mode == CIPHER_MODE_XTS) &&
	    spu->spu_xts_tweak_in_payload())
		sg_set_buf(sg++, rctx->msg_buf.c.supdt_tweak,
			   SPU_XTS_TWEAK_SIZE);

	/* Copy in each dst sg entry from request, up to chunksize */
	datalen = spu_msg_sg_add(&sg, &rctx->dst_sg, &rctx->dst_skip,
				 rctx->dst_nents, chunksize);
	if (datalen < chunksize) {
		pr_err("%s(): failed to copy dst sg to mbox msg. chunksize %u, datalen %u",
		       __func__, chunksize, datalen);
		return -EFAULT;
	}

	if (stat_pad_len)
		sg_set_buf(sg++, rctx->msg_buf.rx_stat_pad, stat_pad_len);

	memset(rctx->msg_buf.rx_stat, 0, SPU_RX_STATUS_LEN);
	sg_set_buf(sg, rctx->msg_buf.rx_stat, spu->spu_rx_status_len());

	return 0;
}

/**
 * spu_skcipher_tx_sg_create() - Build up the scatterlist of buffers used to
 * send a SPU request message for an skcipher request. Includes SPU message
 * headers and the request data.
 * @mssg:	mailbox message containing the transmit sg
 * @rctx:	crypto request context
 * @tx_frag_num: number of scatterlist elements required to construct the
 *		SPU request message
 * @chunksize:	Number of bytes of request data
 * @pad_len:	Number of pad bytes
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Returns:
 *   0 if successful
 *   < 0 if an error
 */
static int
spu_skcipher_tx_sg_create(struct brcm_message *mssg,
			    struct iproc_reqctx_s *rctx,
			    u8 tx_frag_num, unsigned int chunksize, u32 pad_len)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 datalen;		/* Number of bytes of response data expected */
	u32 stat_len;

	mssg->spu.src = kmalloc_array(tx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (unlikely(!mssg->spu.src))
		return -ENOMEM;

	sg = mssg->spu.src;
	sg_init_table(sg, tx_frag_num);

	sg_set_buf(sg++, rctx->msg_buf.bcm_spu_req_hdr,
		   BCM_HDR_LEN + ctx->spu_req_hdr_len);

	/* if XTS tweak in payload, copy from IV (where crypto API puts it) */
	if ((ctx->cipher.mode == CIPHER_MODE_XTS) &&
	    spu->spu_xts_tweak_in_payload())
		sg_set_buf(sg++, rctx->msg_buf.iv_ctr, SPU_XTS_TWEAK_SIZE);

	/* Copy in each src sg entry from request, up to chunksize */
	datalen = spu_msg_sg_add(&sg, &rctx->src_sg, &rctx->src_skip,
				 rctx->src_nents, chunksize);
	if (unlikely(datalen < chunksize)) {
		pr_err("%s(): failed to copy src sg to mbox msg",
		       __func__);
		return -EFAULT;
	}

	if (pad_len)
		sg_set_buf(sg++, rctx->msg_buf.spu_req_pad, pad_len);

	stat_len = spu->spu_tx_status_len();
	if (stat_len) {
		memset(rctx->msg_buf.tx_stat, 0, stat_len);
		sg_set_buf(sg, rctx->msg_buf.tx_stat, stat_len);
	}
	return 0;
}

static int mailbox_send_message(struct brcm_message *mssg, u32 flags,
				u8 chan_idx)
{
	int err;
	int retry_cnt = 0;
	struct device *dev = &(iproc_priv.pdev->dev);

	err = mbox_send_message(iproc_priv.mbox[chan_idx], mssg);
	if (flags & CRYPTO_TFM_REQ_MAY_SLEEP) {
		while ((err == -ENOBUFS) && (retry_cnt < SPU_MB_RETRY_MAX)) {
			/*
			 * Mailbox queue is full. Since MAY_SLEEP is set, assume
			 * not in atomic context and we can wait and try again.
			 */
			retry_cnt++;
			usleep_range(MBOX_SLEEP_MIN, MBOX_SLEEP_MAX);
			err = mbox_send_message(iproc_priv.mbox[chan_idx],
						mssg);
			atomic_inc(&iproc_priv.mb_no_spc);
		}
	}
	if (err < 0) {
		atomic_inc(&iproc_priv.mb_send_fail);
		return err;
	}

	/* Check error returned by mailbox controller */
	err = mssg->error;
	if (unlikely(err < 0)) {
		dev_err(dev, "message error %d", err);
		/* Signal txdone for mailbox channel */
	}

	/* Signal txdone for mailbox channel */
	mbox_client_txdone(iproc_priv.mbox[chan_idx], err);
	return err;
}

/**
 * handle_skcipher_req() - Submit as much of a block cipher request as fits in
 * a single SPU request message, starting at the current position in the request
 * data.
 * @rctx:	Crypto request context
 *
 * This may be called on the crypto API thread, or, when a request is so large
 * it must be broken into multiple SPU messages, on the thread used to invoke
 * the response callback. When requests are broken into multiple SPU
 * messages, we assume subsequent messages depend on previous results, and
 * thus always wait for previous results before submitting the next message.
 * Because requests are submitted in lock step like this, there is no need
 * to synchronize access to request data structures.
 *
 * Return: -EINPROGRESS: request has been accepted and result will be returned
 *			 asynchronously
 *         Any other value indicates an error
 */
static int handle_skcipher_req(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct skcipher_request *req =
	    container_of(areq, struct skcipher_request, base);
	struct iproc_ctx_s *ctx = rctx->ctx;
	struct spu_cipher_parms cipher_parms;
	int err;
	unsigned int chunksize;	/* Num bytes of request to submit */
	int remaining;	/* Bytes of request still to process */
	int chunk_start;	/* Beginning of data for current SPU msg */

	/* IV or ctr value to use in this SPU msg */
	u8 local_iv_ctr[MAX_IV_SIZE];
	u32 stat_pad_len;	/* num bytes to align status field */
	u32 pad_len;		/* total length of all padding */
	struct brcm_message *mssg;	/* mailbox message */

	/* number of entries in src and dst sg in mailbox message. */
	u8 rx_frag_num = 2;	/* response header and STATUS */
	u8 tx_frag_num = 1;	/* request header */

	flow_log("%s\n", __func__);

	cipher_parms.alg = ctx->cipher.alg;
	cipher_parms.mode = ctx->cipher.mode;
	cipher_parms.type = ctx->cipher_type;
	cipher_parms.key_len = ctx->enckeylen;
	cipher_parms.key_buf = ctx->enckey;
	cipher_parms.iv_buf = local_iv_ctr;
	cipher_parms.iv_len = rctx->iv_ctr_len;

	mssg = &rctx->mb_mssg;
	chunk_start = rctx->src_sent;
	remaining = rctx->total_todo - chunk_start;

	/* determine the chunk we are breaking off and update the indexes */
	if ((ctx->max_payload != SPU_MAX_PAYLOAD_INF) &&
	    (remaining > ctx->max_payload))
		chunksize = ctx->max_payload;
	else
		chunksize = remaining;

	rctx->src_sent += chunksize;
	rctx->total_sent = rctx->src_sent;

	/* Count number of sg entries to be included in this request */
	rctx->src_nents = spu_sg_count(rctx->src_sg, rctx->src_skip, chunksize);
	rctx->dst_nents = spu_sg_count(rctx->dst_sg, rctx->dst_skip, chunksize);

	if ((ctx->cipher.mode == CIPHER_MODE_CBC) &&
	    rctx->is_encrypt && chunk_start)
		/*
		 * Encrypting non-first first chunk. Copy last block of
		 * previous result to IV for this chunk.
		 */
		sg_copy_part_to_buf(req->dst, rctx->msg_buf.iv_ctr,
				    rctx->iv_ctr_len,
				    chunk_start - rctx->iv_ctr_len);

	if (rctx->iv_ctr_len) {
		/* get our local copy of the iv */
		__builtin_memcpy(local_iv_ctr, rctx->msg_buf.iv_ctr,
				 rctx->iv_ctr_len);

		/* generate the next IV if possible */
		if ((ctx->cipher.mode == CIPHER_MODE_CBC) &&
		    !rctx->is_encrypt) {
			/*
			 * CBC Decrypt: next IV is the last ciphertext block in
			 * this chunk
			 */
			sg_copy_part_to_buf(req->src, rctx->msg_buf.iv_ctr,
					    rctx->iv_ctr_len,
					    rctx->src_sent - rctx->iv_ctr_len);
		} else if (ctx->cipher.mode == CIPHER_MODE_CTR) {
			/*
			 * The SPU hardware increments the counter once for
			 * each AES block of 16 bytes. So update the counter
			 * for the next chunk, if there is one. Note that for
			 * this chunk, the counter has already been copied to
			 * local_iv_ctr. We can assume a block size of 16,
			 * because we only support CTR mode for AES, not for
			 * any other cipher alg.
			 */
			add_to_ctr(rctx->msg_buf.iv_ctr, chunksize >> 4);
		}
	}

	if (ctx->max_payload == SPU_MAX_PAYLOAD_INF)
		flow_log("max_payload infinite\n");
	else
		flow_log("max_payload %u\n", ctx->max_payload);

	flow_log("sent:%u start:%u remains:%u size:%u\n",
		 rctx->src_sent, chunk_start, remaining, chunksize);

	/* Copy SPU header template created at setkey time */
	memcpy(rctx->msg_buf.bcm_spu_req_hdr, ctx->bcm_spu_req_hdr,
	       sizeof(rctx->msg_buf.bcm_spu_req_hdr));

	spu->spu_cipher_req_finish(rctx->msg_buf.bcm_spu_req_hdr + BCM_HDR_LEN,
				   ctx->spu_req_hdr_len, !(rctx->is_encrypt),
				   &cipher_parms, chunksize);

	atomic64_add(chunksize, &iproc_priv.bytes_out);

	stat_pad_len = spu->spu_wordalign_padlen(chunksize);
	if (stat_pad_len)
		rx_frag_num++;
	pad_len = stat_pad_len;
	if (pad_len) {
		tx_frag_num++;
		spu->spu_request_pad(rctx->msg_buf.spu_req_pad, 0,
				     0, ctx->auth.alg, ctx->auth.mode,
				     rctx->total_sent, stat_pad_len);
	}

	spu->spu_dump_msg_hdr(rctx->msg_buf.bcm_spu_req_hdr + BCM_HDR_LEN,
			      ctx->spu_req_hdr_len);
	packet_log("payload:\n");
	dump_sg(rctx->src_sg, rctx->src_skip, chunksize);
	packet_dump("   pad: ", rctx->msg_buf.spu_req_pad, pad_len);

	/*
	 * Build mailbox message containing SPU request msg and rx buffers
	 * to catch response message
	 */
	memset(mssg, 0, sizeof(*mssg));
	mssg->type = BRCM_MESSAGE_SPU;
	mssg->ctx = rctx;	/* Will be returned in response */

	/* Create rx scatterlist to catch result */
	rx_frag_num += rctx->dst_nents;

	if ((ctx->cipher.mode == CIPHER_MODE_XTS) &&
	    spu->spu_xts_tweak_in_payload())
		rx_frag_num++;	/* extra sg to insert tweak */

	err = spu_skcipher_rx_sg_create(mssg, rctx, rx_frag_num, chunksize,
					  stat_pad_len);
	if (err)
		return err;

	/* Create tx scatterlist containing SPU request message */
	tx_frag_num += rctx->src_nents;
	if (spu->spu_tx_status_len())
		tx_frag_num++;

	if ((ctx->cipher.mode == CIPHER_MODE_XTS) &&
	    spu->spu_xts_tweak_in_payload())
		tx_frag_num++;	/* extra sg to insert tweak */

	err = spu_skcipher_tx_sg_create(mssg, rctx, tx_frag_num, chunksize,
					  pad_len);
	if (err)
		return err;

	err = mailbox_send_message(mssg, req->base.flags, rctx->chan_idx);
	if (unlikely(err < 0))
		return err;

	return -EINPROGRESS;
}

/**
 * handle_skcipher_resp() - Process a block cipher SPU response. Updates the
 * total received count for the request and updates global stats.
 * @rctx:	Crypto request context
 */
static void handle_skcipher_resp(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 payload_len;

	/* See how much data was returned */
	payload_len = spu->spu_payload_length(rctx->msg_buf.spu_resp_hdr);

	/*
	 * In XTS mode, the first SPU_XTS_TWEAK_SIZE bytes may be the
	 * encrypted tweak ("i") value; we don't count those.
	 */
	if ((ctx->cipher.mode == CIPHER_MODE_XTS) &&
	    spu->spu_xts_tweak_in_payload() &&
	    (payload_len >= SPU_XTS_TWEAK_SIZE))
		payload_len -= SPU_XTS_TWEAK_SIZE;

	atomic64_add(payload_len, &iproc_priv.bytes_in);

	flow_log("%s() offset: %u, bd_len: %u BD:\n",
		 __func__, rctx->total_received, payload_len);

	dump_sg(req->dst, rctx->total_received, payload_len);

	rctx->total_received += payload_len;
	if (rctx->total_received == rctx->total_todo) {
		atomic_inc(&iproc_priv.op_counts[SPU_OP_CIPHER]);
		atomic_inc(
		   &iproc_priv.cipher_cnt[ctx->cipher.alg][ctx->cipher.mode]);
	}
}

/**
 * spu_ahash_rx_sg_create() - Build up the scatterlist of buffers used to
 * receive a SPU response message for an ahash request.
 * @mssg:	mailbox message containing the receive sg
 * @rctx:	crypto request context
 * @rx_frag_num: number of scatterlist elements required to hold the
 *		SPU response message
 * @digestsize: length of hash digest, in bytes
 * @stat_pad_len: Number of bytes required to pad the STAT field to
 *		a 4-byte boundary
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Return:
 *   0 if successful
 *   < 0 if an error
 */
static int
spu_ahash_rx_sg_create(struct brcm_message *mssg,
		       struct iproc_reqctx_s *rctx,
		       u8 rx_frag_num, unsigned int digestsize,
		       u32 stat_pad_len)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	struct iproc_ctx_s *ctx = rctx->ctx;

	mssg->spu.dst = kmalloc_array(rx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (!mssg->spu.dst)
		return -ENOMEM;

	sg = mssg->spu.dst;
	sg_init_table(sg, rx_frag_num);
	/* Space for SPU message header */
	sg_set_buf(sg++, rctx->msg_buf.spu_resp_hdr, ctx->spu_resp_hdr_len);

	/* Space for digest */
	sg_set_buf(sg++, rctx->msg_buf.digest, digestsize);

	if (stat_pad_len)
		sg_set_buf(sg++, rctx->msg_buf.rx_stat_pad, stat_pad_len);

	memset(rctx->msg_buf.rx_stat, 0, SPU_RX_STATUS_LEN);
	sg_set_buf(sg, rctx->msg_buf.rx_stat, spu->spu_rx_status_len());
	return 0;
}

/**
 * spu_ahash_tx_sg_create() -  Build up the scatterlist of buffers used to send
 * a SPU request message for an ahash request. Includes SPU message headers and
 * the request data.
 * @mssg:	mailbox message containing the transmit sg
 * @rctx:	crypto request context
 * @tx_frag_num: number of scatterlist elements required to construct the
 *		SPU request message
 * @spu_hdr_len: length in bytes of SPU message header
 * @hash_carry_len: Number of bytes of data carried over from previous req
 * @new_data_len: Number of bytes of new request data
 * @pad_len:	Number of pad bytes
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Return:
 *   0 if successful
 *   < 0 if an error
 */
static int
spu_ahash_tx_sg_create(struct brcm_message *mssg,
		       struct iproc_reqctx_s *rctx,
		       u8 tx_frag_num,
		       u32 spu_hdr_len,
		       unsigned int hash_carry_len,
		       unsigned int new_data_len, u32 pad_len)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	u32 datalen;		/* Number of bytes of response data expected */
	u32 stat_len;

	mssg->spu.src = kmalloc_array(tx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (!mssg->spu.src)
		return -ENOMEM;

	sg = mssg->spu.src;
	sg_init_table(sg, tx_frag_num);

	sg_set_buf(sg++, rctx->msg_buf.bcm_spu_req_hdr,
		   BCM_HDR_LEN + spu_hdr_len);

	if (hash_carry_len)
		sg_set_buf(sg++, rctx->hash_carry, hash_carry_len);

	if (new_data_len) {
		/* Copy in each src sg entry from request, up to chunksize */
		datalen = spu_msg_sg_add(&sg, &rctx->src_sg, &rctx->src_skip,
					 rctx->src_nents, new_data_len);
		if (datalen < new_data_len) {
			pr_err("%s(): failed to copy src sg to mbox msg",
			       __func__);
			return -EFAULT;
		}
	}

	if (pad_len)
		sg_set_buf(sg++, rctx->msg_buf.spu_req_pad, pad_len);

	stat_len = spu->spu_tx_status_len();
	if (stat_len) {
		memset(rctx->msg_buf.tx_stat, 0, stat_len);
		sg_set_buf(sg, rctx->msg_buf.tx_stat, stat_len);
	}

	return 0;
}

/**
 * handle_ahash_req() - Process an asynchronous hash request from the crypto
 * API.
 * @rctx:  Crypto request context
 *
 * Builds a SPU request message embedded in a mailbox message and submits the
 * mailbox message on a selected mailbox channel. The SPU request message is
 * constructed as a scatterlist, including entries from the crypto API's
 * src scatterlist to avoid copying the data to be hashed. This function is
 * called either on the thread from the crypto API, or, in the case that the
 * crypto API request is too large to fit in a single SPU request message,
 * on the thread that invokes the receive callback with a response message.
 * Because some operations require the response from one chunk before the next
 * chunk can be submitted, we always wait for the response for the previous
 * chunk before submitting the next chunk. Because requests are submitted in
 * lock step like this, there is no need to synchronize access to request data
 * structures.
 *
 * Return:
 *   -EINPROGRESS: request has been submitted to SPU and response will be
 *		   returned asynchronously
 *   -EAGAIN:      non-final request included a small amount of data, which for
 *		   efficiency we did not submit to the SPU, but instead stored
 *		   to be submitted to the SPU with the next part of the request
 *   other:        an error code
 */
static int handle_ahash_req(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	struct crypto_tfm *tfm = crypto_ahash_tfm(ahash);
	unsigned int blocksize = crypto_tfm_alg_blocksize(tfm);
	struct iproc_ctx_s *ctx = rctx->ctx;

	/* number of bytes still to be hashed in this req */
	unsigned int nbytes_to_hash = 0;
	int err;
	unsigned int chunksize = 0;	/* length of hash carry + new data */
	/*
	 * length of new data, not from hash carry, to be submitted in
	 * this hw request
	 */
	unsigned int new_data_len;

	unsigned int __maybe_unused chunk_start = 0;
	u32 db_size;	 /* Length of data field, incl gcm and hash padding */
	int pad_len = 0; /* total pad len, including gcm, hash, stat padding */
	u32 data_pad_len = 0;	/* length of GCM/CCM padding */
	u32 stat_pad_len = 0;	/* length of padding to align STATUS word */
	struct brcm_message *mssg;	/* mailbox message */
	struct spu_request_opts req_opts;
	struct spu_cipher_parms cipher_parms;
	struct spu_hash_parms hash_parms;
	struct spu_aead_parms aead_parms;
	unsigned int local_nbuf;
	u32 spu_hdr_len;
	unsigned int digestsize;
	u16 rem = 0;

	/*
	 * number of entries in src and dst sg. Always includes SPU msg header.
	 * rx always includes a buffer to catch digest and STATUS.
	 */
	u8 rx_frag_num = 3;
	u8 tx_frag_num = 1;

	flow_log("total_todo %u, total_sent %u\n",
		 rctx->total_todo, rctx->total_sent);

	memset(&req_opts, 0, sizeof(req_opts));
	memset(&cipher_parms, 0, sizeof(cipher_parms));
	memset(&hash_parms, 0, sizeof(hash_parms));
	memset(&aead_parms, 0, sizeof(aead_parms));

	req_opts.bd_suppress = true;
	hash_parms.alg = ctx->auth.alg;
	hash_parms.mode = ctx->auth.mode;
	hash_parms.type = HASH_TYPE_NONE;
	hash_parms.key_buf = (u8 *)ctx->authkey;
	hash_parms.key_len = ctx->authkeylen;

	/*
	 * For hash algorithms below assignment looks bit odd but
	 * it's needed for AES-XCBC and AES-CMAC hash algorithms
	 * to differentiate between 128, 192, 256 bit key values.
	 * Based on the key values, hash algorithm is selected.
	 * For example for 128 bit key, hash algorithm is AES-128.
	 */
	cipher_parms.type = ctx->cipher_type;

	mssg = &rctx->mb_mssg;
	chunk_start = rctx->src_sent;

	/*
	 * Compute the amount remaining to hash. This may include data
	 * carried over from previous requests.
	 */
	nbytes_to_hash = rctx->total_todo - rctx->total_sent;
	chunksize = nbytes_to_hash;
	if ((ctx->max_payload != SPU_MAX_PAYLOAD_INF) &&
	    (chunksize > ctx->max_payload))
		chunksize = ctx->max_payload;

	/*
	 * If this is not a final request and the request data is not a multiple
	 * of a full block, then simply park the extra data and prefix it to the
	 * data for the next request.
	 */
	if (!rctx->is_final) {
		u8 *dest = rctx->hash_carry + rctx->hash_carry_len;
		u16 new_len;  /* len of data to add to hash carry */

		rem = chunksize % blocksize;   /* remainder */
		if (rem) {
			/* chunksize not a multiple of blocksize */
			chunksize -= rem;
			if (chunksize == 0) {
				/* Don't have a full block to submit to hw */
				new_len = rem - rctx->hash_carry_len;
				sg_copy_part_to_buf(req->src, dest, new_len,
						    rctx->src_sent);
				rctx->hash_carry_len = rem;
				flow_log("Exiting with hash carry len: %u\n",
					 rctx->hash_carry_len);
				packet_dump("  buf: ",
					    rctx->hash_carry,
					    rctx->hash_carry_len);
				return -EAGAIN;
			}
		}
	}

	/* if we have hash carry, then prefix it to the data in this request */
	local_nbuf = rctx->hash_carry_len;
	rctx->hash_carry_len = 0;
	if (local_nbuf)
		tx_frag_num++;
	new_data_len = chunksize - local_nbuf;

	/* Count number of sg entries to be used in this request */
	rctx->src_nents = spu_sg_count(rctx->src_sg, rctx->src_skip,
				       new_data_len);

	/* AES hashing keeps key size in type field, so need to copy it here */
	if (hash_parms.alg == HASH_ALG_AES)
		hash_parms.type = (enum hash_type)cipher_parms.type;
	else
		hash_parms.type = spu->spu_hash_type(rctx->total_sent);

	digestsize = spu->spu_digest_size(ctx->digestsize, ctx->auth.alg,
					  hash_parms.type);
	hash_parms.digestsize =	digestsize;

	/* update the indexes */
	rctx->total_sent += chunksize;
	/* if you sent a prebuf then that wasn't from this req->src */
	rctx->src_sent += new_data_len;

	if ((rctx->total_sent == rctx->total_todo) && rctx->is_final)
		hash_parms.pad_len = spu->spu_hash_pad_len(hash_parms.alg,
							   hash_parms.mode,
							   chunksize,
							   blocksize);

	/*
	 * If a non-first chunk, then include the digest returned from the
	 * previous chunk so that hw can add to it (except for AES types).
	 */
	if ((hash_parms.type == HASH_TYPE_UPDT) &&
	    (hash_parms.alg != HASH_ALG_AES)) {
		hash_parms.key_buf = rctx->incr_hash;
		hash_parms.key_len = digestsize;
	}

	atomic64_add(chunksize, &iproc_priv.bytes_out);

	flow_log("%s() final: %u nbuf: %u ",
		 __func__, rctx->is_final, local_nbuf);

	if (ctx->max_payload == SPU_MAX_PAYLOAD_INF)
		flow_log("max_payload infinite\n");
	else
		flow_log("max_payload %u\n", ctx->max_payload);

	flow_log("chunk_start: %u chunk_size: %u\n", chunk_start, chunksize);

	/* Prepend SPU header with type 3 BCM header */
	memcpy(rctx->msg_buf.bcm_spu_req_hdr, BCMHEADER, BCM_HDR_LEN);

	hash_parms.prebuf_len = local_nbuf;
	spu_hdr_len = spu->spu_create_request(rctx->msg_buf.bcm_spu_req_hdr +
					      BCM_HDR_LEN,
					      &req_opts, &cipher_parms,
					      &hash_parms, &aead_parms,
					      new_data_len);

	if (spu_hdr_len == 0) {
		pr_err("Failed to create SPU request header\n");
		return -EFAULT;
	}

	/*
	 * Determine total length of padding required. Put all padding in one
	 * buffer.
	 */
	data_pad_len = spu->spu_gcm_ccm_pad_len(ctx->cipher.mode, chunksize);
	db_size = spu_real_db_size(0, 0, local_nbuf, new_data_len,
				   0, 0, hash_parms.pad_len);
	if (spu->spu_tx_status_len())
		stat_pad_len = spu->spu_wordalign_padlen(db_size);
	if (stat_pad_len)
		rx_frag_num++;
	pad_len = hash_parms.pad_len + data_pad_len + stat_pad_len;
	if (pad_len) {
		tx_frag_num++;
		spu->spu_request_pad(rctx->msg_buf.spu_req_pad, data_pad_len,
				     hash_parms.pad_len, ctx->auth.alg,
				     ctx->auth.mode, rctx->total_sent,
				     stat_pad_len);
	}

	spu->spu_dump_msg_hdr(rctx->msg_buf.bcm_spu_req_hdr + BCM_HDR_LEN,
			      spu_hdr_len);
	packet_dump("    prebuf: ", rctx->hash_carry, local_nbuf);
	flow_log("Data:\n");
	dump_sg(rctx->src_sg, rctx->src_skip, new_data_len);
	packet_dump("   pad: ", rctx->msg_buf.spu_req_pad, pad_len);

	/*
	 * Build mailbox message containing SPU request msg and rx buffers
	 * to catch response message
	 */
	memset(mssg, 0, sizeof(*mssg));
	mssg->type = BRCM_MESSAGE_SPU;
	mssg->ctx = rctx;	/* Will be returned in response */

	/* Create rx scatterlist to catch result */
	err = spu_ahash_rx_sg_create(mssg, rctx, rx_frag_num, digestsize,
				     stat_pad_len);
	if (err)
		return err;

	/* Create tx scatterlist containing SPU request message */
	tx_frag_num += rctx->src_nents;
	if (spu->spu_tx_status_len())
		tx_frag_num++;
	err = spu_ahash_tx_sg_create(mssg, rctx, tx_frag_num, spu_hdr_len,
				     local_nbuf, new_data_len, pad_len);
	if (err)
		return err;

	err = mailbox_send_message(mssg, req->base.flags, rctx->chan_idx);
	if (unlikely(err < 0))
		return err;

	return -EINPROGRESS;
}

/**
 * spu_hmac_outer_hash() - Request synchonous software compute of the outer hash
 * for an HMAC request.
 * @req:  The HMAC request from the crypto API
 * @ctx:  The session context
 *
 * Return: 0 if synchronous hash operation successful
 *         -EINVAL if the hash algo is unrecognized
 *         any other value indicates an error
 */
static int spu_hmac_outer_hash(struct ahash_request *req,
			       struct iproc_ctx_s *ctx)
{
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	unsigned int blocksize =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(ahash));
	int rc;

	switch (ctx->auth.alg) {
	case HASH_ALG_MD5:
		rc = do_shash("md5", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	case HASH_ALG_SHA1:
		rc = do_shash("sha1", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	case HASH_ALG_SHA224:
		rc = do_shash("sha224", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	case HASH_ALG_SHA256:
		rc = do_shash("sha256", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	case HASH_ALG_SHA384:
		rc = do_shash("sha384", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	case HASH_ALG_SHA512:
		rc = do_shash("sha512", req->result, ctx->opad, blocksize,
			      req->result, ctx->digestsize, NULL, 0);
		break;
	default:
		pr_err("%s() Error : unknown hmac type\n", __func__);
		rc = -EINVAL;
	}
	return rc;
}

/**
 * ahash_req_done() - Process a hash result from the SPU hardware.
 * @rctx: Crypto request context
 *
 * Return: 0 if successful
 *         < 0 if an error
 */
static int ahash_req_done(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct ahash_request *req = ahash_request_cast(areq);
	struct iproc_ctx_s *ctx = rctx->ctx;
	int err;

	memcpy(req->result, rctx->msg_buf.digest, ctx->digestsize);

	if (spu->spu_type == SPU_TYPE_SPUM) {
		/* byte swap the output from the UPDT function to network byte
		 * order
		 */
		if (ctx->auth.alg == HASH_ALG_MD5) {
			__swab32s((u32 *)req->result);
			__swab32s(((u32 *)req->result) + 1);
			__swab32s(((u32 *)req->result) + 2);
			__swab32s(((u32 *)req->result) + 3);
			__swab32s(((u32 *)req->result) + 4);
		}
	}

	flow_dump("  digest ", req->result, ctx->digestsize);

	/* if this an HMAC then do the outer hash */
	if (rctx->is_sw_hmac) {
		err = spu_hmac_outer_hash(req, ctx);
		if (err < 0)
			return err;
		flow_dump("  hmac: ", req->result, ctx->digestsize);
	}

	if (rctx->is_sw_hmac || ctx->auth.mode == HASH_MODE_HMAC) {
		atomic_inc(&iproc_priv.op_counts[SPU_OP_HMAC]);
		atomic_inc(&iproc_priv.hmac_cnt[ctx->auth.alg]);
	} else {
		atomic_inc(&iproc_priv.op_counts[SPU_OP_HASH]);
		atomic_inc(&iproc_priv.hash_cnt[ctx->auth.alg]);
	}

	return 0;
}

/**
 * handle_ahash_resp() - Process a SPU response message for a hash request.
 * Checks if the entire crypto API request has been processed, and if so,
 * invokes post processing on the result.
 * @rctx: Crypto request context
 */
static void handle_ahash_resp(struct iproc_reqctx_s *rctx)
{
	struct iproc_ctx_s *ctx = rctx->ctx;
	struct crypto_async_request *areq = rctx->parent;
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *ahash = crypto_ahash_reqtfm(req);
	unsigned int blocksize =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(ahash));
	/*
	 * Save hash to use as input to next op if incremental. Might be copying
	 * too much, but that's easier than figuring out actual digest size here
	 */
	memcpy(rctx->incr_hash, rctx->msg_buf.digest, MAX_DIGEST_SIZE);

	flow_log("%s() blocksize:%u digestsize:%u\n",
		 __func__, blocksize, ctx->digestsize);

	atomic64_add(ctx->digestsize, &iproc_priv.bytes_in);

	if (rctx->is_final && (rctx->total_sent == rctx->total_todo))
		ahash_req_done(rctx);
}

/**
 * spu_aead_rx_sg_create() - Build up the scatterlist of buffers used to receive
 * a SPU response message for an AEAD request. Includes buffers to catch SPU
 * message headers and the response data.
 * @mssg:	mailbox message containing the receive sg
 * @req:	Crypto API request
 * @rctx:	crypto request context
 * @rx_frag_num: number of scatterlist elements required to hold the
 *		SPU response message
 * @assoc_len:	Length of associated data included in the crypto request
 * @ret_iv_len: Length of IV returned in response
 * @resp_len:	Number of bytes of response data expected to be written to
 *              dst buffer from crypto API
 * @digestsize: Length of hash digest, in bytes
 * @stat_pad_len: Number of bytes required to pad the STAT field to
 *		a 4-byte boundary
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Returns:
 *   0 if successful
 *   < 0 if an error
 */
static int spu_aead_rx_sg_create(struct brcm_message *mssg,
				 struct aead_request *req,
				 struct iproc_reqctx_s *rctx,
				 u8 rx_frag_num,
				 unsigned int assoc_len,
				 u32 ret_iv_len, unsigned int resp_len,
				 unsigned int digestsize, u32 stat_pad_len)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 datalen;		/* Number of bytes of response data expected */
	u32 assoc_buf_len;
	u8 data_padlen = 0;

	if (ctx->is_rfc4543) {
		/* RFC4543: only pad after data, not after AAD */
		data_padlen = spu->spu_gcm_ccm_pad_len(ctx->cipher.mode,
							  assoc_len + resp_len);
		assoc_buf_len = assoc_len;
	} else {
		data_padlen = spu->spu_gcm_ccm_pad_len(ctx->cipher.mode,
							  resp_len);
		assoc_buf_len = spu->spu_assoc_resp_len(ctx->cipher.mode,
						assoc_len, ret_iv_len,
						rctx->is_encrypt);
	}

	if (ctx->cipher.mode == CIPHER_MODE_CCM)
		/* ICV (after data) must be in the next 32-bit word for CCM */
		data_padlen += spu->spu_wordalign_padlen(assoc_buf_len +
							 resp_len +
							 data_padlen);

	if (data_padlen)
		/* have to catch gcm pad in separate buffer */
		rx_frag_num++;

	mssg->spu.dst = kmalloc_array(rx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (!mssg->spu.dst)
		return -ENOMEM;

	sg = mssg->spu.dst;
	sg_init_table(sg, rx_frag_num);

	/* Space for SPU message header */
	sg_set_buf(sg++, rctx->msg_buf.spu_resp_hdr, ctx->spu_resp_hdr_len);

	if (assoc_buf_len) {
		/*
		 * Don't write directly to req->dst, because SPU may pad the
		 * assoc data in the response
		 */
		memset(rctx->msg_buf.a.resp_aad, 0, assoc_buf_len);
		sg_set_buf(sg++, rctx->msg_buf.a.resp_aad, assoc_buf_len);
	}

	if (resp_len) {
		/*
		 * Copy in each dst sg entry from request, up to chunksize.
		 * dst sg catches just the data. digest caught in separate buf.
		 */
		datalen = spu_msg_sg_add(&sg, &rctx->dst_sg, &rctx->dst_skip,
					 rctx->dst_nents, resp_len);
		if (datalen < (resp_len)) {
			pr_err("%s(): failed to copy dst sg to mbox msg. expected len %u, datalen %u",
			       __func__, resp_len, datalen);
			return -EFAULT;
		}
	}

	/* If GCM/CCM data is padded, catch padding in separate buffer */
	if (data_padlen) {
		memset(rctx->msg_buf.a.gcmpad, 0, data_padlen);
		sg_set_buf(sg++, rctx->msg_buf.a.gcmpad, data_padlen);
	}

	/* Always catch ICV in separate buffer */
	sg_set_buf(sg++, rctx->msg_buf.digest, digestsize);

	flow_log("stat_pad_len %u\n", stat_pad_len);
	if (stat_pad_len) {
		memset(rctx->msg_buf.rx_stat_pad, 0, stat_pad_len);
		sg_set_buf(sg++, rctx->msg_buf.rx_stat_pad, stat_pad_len);
	}

	memset(rctx->msg_buf.rx_stat, 0, SPU_RX_STATUS_LEN);
	sg_set_buf(sg, rctx->msg_buf.rx_stat, spu->spu_rx_status_len());

	return 0;
}

/**
 * spu_aead_tx_sg_create() - Build up the scatterlist of buffers used to send a
 * SPU request message for an AEAD request. Includes SPU message headers and the
 * request data.
 * @mssg:	mailbox message containing the transmit sg
 * @rctx:	crypto request context
 * @tx_frag_num: number of scatterlist elements required to construct the
 *		SPU request message
 * @spu_hdr_len: length of SPU message header in bytes
 * @assoc:	crypto API associated data scatterlist
 * @assoc_len:	length of associated data
 * @assoc_nents: number of scatterlist entries containing assoc data
 * @aead_iv_len: length of AEAD IV, if included
 * @chunksize:	Number of bytes of request data
 * @aad_pad_len: Number of bytes of padding at end of AAD. For GCM/CCM.
 * @pad_len:	Number of pad bytes
 * @incl_icv:	If true, write separate ICV buffer after data and
 *              any padding
 *
 * The scatterlist that gets allocated here is freed in spu_chunk_cleanup()
 * when the request completes, whether the request is handled successfully or
 * there is an error.
 *
 * Return:
 *   0 if successful
 *   < 0 if an error
 */
static int spu_aead_tx_sg_create(struct brcm_message *mssg,
				 struct iproc_reqctx_s *rctx,
				 u8 tx_frag_num,
				 u32 spu_hdr_len,
				 struct scatterlist *assoc,
				 unsigned int assoc_len,
				 int assoc_nents,
				 unsigned int aead_iv_len,
				 unsigned int chunksize,
				 u32 aad_pad_len, u32 pad_len, bool incl_icv)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct scatterlist *sg;	/* used to build sgs in mbox message */
	struct scatterlist *assoc_sg = assoc;
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 datalen;		/* Number of bytes of data to write */
	u32 written;		/* Number of bytes of data written */
	u32 assoc_offset = 0;
	u32 stat_len;

	mssg->spu.src = kmalloc_array(tx_frag_num, sizeof(struct scatterlist),
				      rctx->gfp);
	if (!mssg->spu.src)
		return -ENOMEM;

	sg = mssg->spu.src;
	sg_init_table(sg, tx_frag_num);

	sg_set_buf(sg++, rctx->msg_buf.bcm_spu_req_hdr,
		   BCM_HDR_LEN + spu_hdr_len);

	if (assoc_len) {
		/* Copy in each associated data sg entry from request */
		written = spu_msg_sg_add(&sg, &assoc_sg, &assoc_offset,
					 assoc_nents, assoc_len);
		if (written < assoc_len) {
			pr_err("%s(): failed to copy assoc sg to mbox msg",
			       __func__);
			return -EFAULT;
		}
	}

	if (aead_iv_len)
		sg_set_buf(sg++, rctx->msg_buf.iv_ctr, aead_iv_len);

	if (aad_pad_len) {
		memset(rctx->msg_buf.a.req_aad_pad, 0, aad_pad_len);
		sg_set_buf(sg++, rctx->msg_buf.a.req_aad_pad, aad_pad_len);
	}

	datalen = chunksize;
	if ((chunksize > ctx->digestsize) && incl_icv)
		datalen -= ctx->digestsize;
	if (datalen) {
		/* For aead, a single msg should consume the entire src sg */
		written = spu_msg_sg_add(&sg, &rctx->src_sg, &rctx->src_skip,
					 rctx->src_nents, datalen);
		if (written < datalen) {
			pr_err("%s(): failed to copy src sg to mbox msg",
			       __func__);
			return -EFAULT;
		}
	}

	if (pad_len) {
		memset(rctx->msg_buf.spu_req_pad, 0, pad_len);
		sg_set_buf(sg++, rctx->msg_buf.spu_req_pad, pad_len);
	}

	if (incl_icv)
		sg_set_buf(sg++, rctx->msg_buf.digest, ctx->digestsize);

	stat_len = spu->spu_tx_status_len();
	if (stat_len) {
		memset(rctx->msg_buf.tx_stat, 0, stat_len);
		sg_set_buf(sg, rctx->msg_buf.tx_stat, stat_len);
	}
	return 0;
}

/**
 * handle_aead_req() - Submit a SPU request message for the next chunk of the
 * current AEAD request.
 * @rctx:  Crypto request context
 *
 * Unlike other operation types, we assume the length of the request fits in
 * a single SPU request message. aead_enqueue() makes sure this is true.
 * Comments for other op types regarding threads applies here as well.
 *
 * Unlike incremental hash ops, where the spu returns the entire hash for
 * truncated algs like sha-224, the SPU returns just the truncated hash in
 * response to aead requests. So digestsize is always ctx->digestsize here.
 *
 * Return: -EINPROGRESS: crypto request has been accepted and result will be
 *			 returned asynchronously
 *         Any other value indicates an error
 */
static int handle_aead_req(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct aead_request *req = container_of(areq,
						struct aead_request, base);
	struct iproc_ctx_s *ctx = rctx->ctx;
	int err;
	unsigned int chunksize;
	unsigned int resp_len;
	u32 spu_hdr_len;
	u32 db_size;
	u32 stat_pad_len;
	u32 pad_len;
	struct brcm_message *mssg;	/* mailbox message */
	struct spu_request_opts req_opts;
	struct spu_cipher_parms cipher_parms;
	struct spu_hash_parms hash_parms;
	struct spu_aead_parms aead_parms;
	int assoc_nents = 0;
	bool incl_icv = false;
	unsigned int digestsize = ctx->digestsize;

	/* number of entries in src and dst sg. Always includes SPU msg header.
	 */
	u8 rx_frag_num = 2;	/* and STATUS */
	u8 tx_frag_num = 1;

	/* doing the whole thing at once */
	chunksize = rctx->total_todo;

	flow_log("%s: chunksize %u\n", __func__, chunksize);

	memset(&req_opts, 0, sizeof(req_opts));
	memset(&hash_parms, 0, sizeof(hash_parms));
	memset(&aead_parms, 0, sizeof(aead_parms));

	req_opts.is_inbound = !(rctx->is_encrypt);
	req_opts.auth_first = ctx->auth_first;
	req_opts.is_aead = true;
	req_opts.is_esp = ctx->is_esp;

	cipher_parms.alg = ctx->cipher.alg;
	cipher_parms.mode = ctx->cipher.mode;
	cipher_parms.type = ctx->cipher_type;
	cipher_parms.key_buf = ctx->enckey;
	cipher_parms.key_len = ctx->enckeylen;
	cipher_parms.iv_buf = rctx->msg_buf.iv_ctr;
	cipher_parms.iv_len = rctx->iv_ctr_len;

	hash_parms.alg = ctx->auth.alg;
	hash_parms.mode = ctx->auth.mode;
	hash_parms.type = HASH_TYPE_NONE;
	hash_parms.key_buf = (u8 *)ctx->authkey;
	hash_parms.key_len = ctx->authkeylen;
	hash_parms.digestsize = digestsize;

	if ((ctx->auth.alg == HASH_ALG_SHA224) &&
	    (ctx->authkeylen < SHA224_DIGEST_SIZE))
		hash_parms.key_len = SHA224_DIGEST_SIZE;

	aead_parms.assoc_size = req->assoclen;
	if (ctx->is_esp && !ctx->is_rfc4543) {
		/*
		 * 8-byte IV is included assoc data in request. SPU2
		 * expects AAD to include just SPI and seqno. So
		 * subtract off the IV len.
		 */
		aead_parms.assoc_size -= GCM_RFC4106_IV_SIZE;

		if (rctx->is_encrypt) {
			aead_parms.return_iv = true;
			aead_parms.ret_iv_len = GCM_RFC4106_IV_SIZE;
			aead_parms.ret_iv_off = GCM_ESP_SALT_SIZE;
		}
	} else {
		aead_parms.ret_iv_len = 0;
	}

	/*
	 * Count number of sg entries from the crypto API request that are to
	 * be included in this mailbox message. For dst sg, don't count space
	 * for digest. Digest gets caught in a separate buffer and copied back
	 * to dst sg when processing response.
	 */
	rctx->src_nents = spu_sg_count(rctx->src_sg, rctx->src_skip, chunksize);
	rctx->dst_nents = spu_sg_count(rctx->dst_sg, rctx->dst_skip, chunksize);
	if (aead_parms.assoc_size)
		assoc_nents = spu_sg_count(rctx->assoc, 0,
					   aead_parms.assoc_size);

	mssg = &rctx->mb_mssg;

	rctx->total_sent = chunksize;
	rctx->src_sent = chunksize;
	if (spu->spu_assoc_resp_len(ctx->cipher.mode,
				    aead_parms.assoc_size,
				    aead_parms.ret_iv_len,
				    rctx->is_encrypt))
		rx_frag_num++;

	aead_parms.iv_len = spu->spu_aead_ivlen(ctx->cipher.mode,
						rctx->iv_ctr_len);

	if (ctx->auth.alg == HASH_ALG_AES)
		hash_parms.type = (enum hash_type)ctx->cipher_type;

	/* General case AAD padding (CCM and RFC4543 special cases below) */
	aead_parms.aad_pad_len = spu->spu_gcm_ccm_pad_len(ctx->cipher.mode,
						 aead_parms.assoc_size);

	/* General case data padding (CCM decrypt special case below) */
	aead_parms.data_pad_len = spu->spu_gcm_ccm_pad_len(ctx->cipher.mode,
							   chunksize);

	if (ctx->cipher.mode == CIPHER_MODE_CCM) {
		/*
		 * for CCM, AAD len + 2 (rather than AAD len) needs to be
		 * 128-bit aligned
		 */
		aead_parms.aad_pad_len = spu->spu_gcm_ccm_pad_len(
					 ctx->cipher.mode,
					 aead_parms.assoc_size + 2);

		/*
		 * And when decrypting CCM, need to pad without including
		 * size of ICV which is tacked on to end of chunk
		 */
		if (!rctx->is_encrypt)
			aead_parms.data_pad_len =
				spu->spu_gcm_ccm_pad_len(ctx->cipher.mode,
							chunksize - digestsize);

		/* CCM also requires software to rewrite portions of IV: */
		spu->spu_ccm_update_iv(digestsize, &cipher_parms, req->assoclen,
				       chunksize, rctx->is_encrypt,
				       ctx->is_esp);
	}

	if (ctx->is_rfc4543) {
		/*
		 * RFC4543: data is included in AAD, so don't pad after AAD
		 * and pad data based on both AAD + data size
		 */
		aead_parms.aad_pad_len = 0;
		if (!rctx->is_encrypt)
			aead_parms.data_pad_len = spu->spu_gcm_ccm_pad_len(
					ctx->cipher.mode,
					aead_parms.assoc_size + chunksize -
					digestsize);
		else
			aead_parms.data_pad_len = spu->spu_gcm_ccm_pad_len(
					ctx->cipher.mode,
					aead_parms.assoc_size + chunksize);

		req_opts.is_rfc4543 = true;
	}

	if (spu_req_incl_icv(ctx->cipher.mode, rctx->is_encrypt)) {
		incl_icv = true;
		tx_frag_num++;
		/* Copy ICV from end of src scatterlist to digest buf */
		sg_copy_part_to_buf(req->src, rctx->msg_buf.digest, digestsize,
				    req->assoclen + rctx->total_sent -
				    digestsize);
	}

	atomic64_add(chunksize, &iproc_priv.bytes_out);

	flow_log("%s()-sent chunksize:%u\n", __func__, chunksize);

	/* Prepend SPU header with type 3 BCM header */
	memcpy(rctx->msg_buf.bcm_spu_req_hdr, BCMHEADER, BCM_HDR_LEN);

	spu_hdr_len = spu->spu_create_request(rctx->msg_buf.bcm_spu_req_hdr +
					      BCM_HDR_LEN, &req_opts,
					      &cipher_parms, &hash_parms,
					      &aead_parms, chunksize);

	/* Determine total length of padding. Put all padding in one buffer. */
	db_size = spu_real_db_size(aead_parms.assoc_size, aead_parms.iv_len, 0,
				   chunksize, aead_parms.aad_pad_len,
				   aead_parms.data_pad_len, 0);

	stat_pad_len = spu->spu_wordalign_padlen(db_size);

	if (stat_pad_len)
		rx_frag_num++;
	pad_len = aead_parms.data_pad_len + stat_pad_len;
	if (pad_len) {
		tx_frag_num++;
		spu->spu_request_pad(rctx->msg_buf.spu_req_pad,
				     aead_parms.data_pad_len, 0,
				     ctx->auth.alg, ctx->auth.mode,
				     rctx->total_sent, stat_pad_len);
	}

	spu->spu_dump_msg_hdr(rctx->msg_buf.bcm_spu_req_hdr + BCM_HDR_LEN,
			      spu_hdr_len);
	dump_sg(rctx->assoc, 0, aead_parms.assoc_size);
	packet_dump("    aead iv: ", rctx->msg_buf.iv_ctr, aead_parms.iv_len);
	packet_log("BD:\n");
	dump_sg(rctx->src_sg, rctx->src_skip, chunksize);
	packet_dump("   pad: ", rctx->msg_buf.spu_req_pad, pad_len);

	/*
	 * Build mailbox message containing SPU request msg and rx buffers
	 * to catch response message
	 */
	memset(mssg, 0, sizeof(*mssg));
	mssg->type = BRCM_MESSAGE_SPU;
	mssg->ctx = rctx;	/* Will be returned in response */

	/* Create rx scatterlist to catch result */
	rx_frag_num += rctx->dst_nents;
	resp_len = chunksize;

	/*
	 * Always catch ICV in separate buffer. Have to for GCM/CCM because of
	 * padding. Have to for SHA-224 and other truncated SHAs because SPU
	 * sends entire digest back.
	 */
	rx_frag_num++;

	if (((ctx->cipher.mode == CIPHER_MODE_GCM) ||
	     (ctx->cipher.mode == CIPHER_MODE_CCM)) && !rctx->is_encrypt) {
		/*
		 * Input is ciphertxt plus ICV, but ICV not incl
		 * in output.
		 */
		resp_len -= ctx->digestsize;
		if (resp_len == 0)
			/* no rx frags to catch output data */
			rx_frag_num -= rctx->dst_nents;
	}

	err = spu_aead_rx_sg_create(mssg, req, rctx, rx_frag_num,
				    aead_parms.assoc_size,
				    aead_parms.ret_iv_len, resp_len, digestsize,
				    stat_pad_len);
	if (err)
		return err;

	/* Create tx scatterlist containing SPU request message */
	tx_frag_num += rctx->src_nents;
	tx_frag_num += assoc_nents;
	if (aead_parms.aad_pad_len)
		tx_frag_num++;
	if (aead_parms.iv_len)
		tx_frag_num++;
	if (spu->spu_tx_status_len())
		tx_frag_num++;
	err = spu_aead_tx_sg_create(mssg, rctx, tx_frag_num, spu_hdr_len,
				    rctx->assoc, aead_parms.assoc_size,
				    assoc_nents, aead_parms.iv_len, chunksize,
				    aead_parms.aad_pad_len, pad_len, incl_icv);
	if (err)
		return err;

	err = mailbox_send_message(mssg, req->base.flags, rctx->chan_idx);
	if (unlikely(err < 0))
		return err;

	return -EINPROGRESS;
}

/**
 * handle_aead_resp() - Process a SPU response message for an AEAD request.
 * @rctx:  Crypto request context
 */
static void handle_aead_resp(struct iproc_reqctx_s *rctx)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_async_request *areq = rctx->parent;
	struct aead_request *req = container_of(areq,
						struct aead_request, base);
	struct iproc_ctx_s *ctx = rctx->ctx;
	u32 payload_len;
	unsigned int icv_offset;
	u32 result_len;

	/* See how much data was returned */
	payload_len = spu->spu_payload_length(rctx->msg_buf.spu_resp_hdr);
	flow_log("payload_len %u\n", payload_len);

	/* only count payload */
	atomic64_add(payload_len, &iproc_priv.bytes_in);

	if (req->assoclen)
		packet_dump("  assoc_data ", rctx->msg_buf.a.resp_aad,
			    req->assoclen);

	/*
	 * Copy the ICV back to the destination
	 * buffer. In decrypt case, SPU gives us back the digest, but crypto
	 * API doesn't expect ICV in dst buffer.
	 */
	result_len = req->cryptlen;
	if (rctx->is_encrypt) {
		icv_offset = req->assoclen + rctx->total_sent;
		packet_dump("  ICV: ", rctx->msg_buf.digest, ctx->digestsize);
		flow_log("copying ICV to dst sg at offset %u\n", icv_offset);
		sg_copy_part_from_buf(req->dst, rctx->msg_buf.digest,
				      ctx->digestsize, icv_offset);
		result_len += ctx->digestsize;
	}

	packet_log("response data:  ");
	dump_sg(req->dst, req->assoclen, result_len);

	atomic_inc(&iproc_priv.op_counts[SPU_OP_AEAD]);
	if (ctx->cipher.alg == CIPHER_ALG_AES) {
		if (ctx->cipher.mode == CIPHER_MODE_CCM)
			atomic_inc(&iproc_priv.aead_cnt[AES_CCM]);
		else if (ctx->cipher.mode == CIPHER_MODE_GCM)
			atomic_inc(&iproc_priv.aead_cnt[AES_GCM]);
		else
			atomic_inc(&iproc_priv.aead_cnt[AUTHENC]);
	} else {
		atomic_inc(&iproc_priv.aead_cnt[AUTHENC]);
	}
}

/**
 * spu_chunk_cleanup() - Do cleanup after processing one chunk of a request
 * @rctx:  request context
 *
 * Mailbox scatterlists are allocated for each chunk. So free them after
 * processing each chunk.
 */
static void spu_chunk_cleanup(struct iproc_reqctx_s *rctx)
{
	/* mailbox message used to tx request */
	struct brcm_message *mssg = &rctx->mb_mssg;

	kfree(mssg->spu.src);
	kfree(mssg->spu.dst);
	memset(mssg, 0, sizeof(struct brcm_message));
}

/**
 * finish_req() - Used to invoke the complete callback from the requester when
 * a request has been handled asynchronously.
 * @rctx:  Request context
 * @err:   Indicates whether the request was successful or not
 *
 * Ensures that cleanup has been done for request
 */
static void finish_req(struct iproc_reqctx_s *rctx, int err)
{
	struct crypto_async_request *areq = rctx->parent;

	flow_log("%s() err:%d\n\n", __func__, err);

	/* No harm done if already called */
	spu_chunk_cleanup(rctx);

	if (areq)
		crypto_request_complete(areq, err);
}

/**
 * spu_rx_callback() - Callback from mailbox framework with a SPU response.
 * @cl:		mailbox client structure for SPU driver
 * @msg:	mailbox message containing SPU response
 */
static void spu_rx_callback(struct mbox_client *cl, void *msg)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct brcm_message *mssg = msg;
	struct iproc_reqctx_s *rctx;
	int err;

	rctx = mssg->ctx;
	if (unlikely(!rctx)) {
		/* This is fatal */
		pr_err("%s(): no request context", __func__);
		err = -EFAULT;
		goto cb_finish;
	}

	/* process the SPU status */
	err = spu->spu_status_process(rctx->msg_buf.rx_stat);
	if (err != 0) {
		if (err == SPU_INVALID_ICV)
			atomic_inc(&iproc_priv.bad_icv);
		err = -EBADMSG;
		goto cb_finish;
	}

	/* Process the SPU response message */
	switch (rctx->ctx->alg->type) {
	case CRYPTO_ALG_TYPE_SKCIPHER:
		handle_skcipher_resp(rctx);
		break;
	case CRYPTO_ALG_TYPE_AHASH:
		handle_ahash_resp(rctx);
		break;
	case CRYPTO_ALG_TYPE_AEAD:
		handle_aead_resp(rctx);
		break;
	default:
		err = -EINVAL;
		goto cb_finish;
	}

	/*
	 * If this response does not complete the request, then send the next
	 * request chunk.
	 */
	if (rctx->total_sent < rctx->total_todo) {
		/* Deallocate anything specific to previous chunk */
		spu_chunk_cleanup(rctx);

		switch (rctx->ctx->alg->type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			err = handle_skcipher_req(rctx);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			err = handle_ahash_req(rctx);
			if (err == -EAGAIN)
				/*
				 * we saved data in hash carry, but tell crypto
				 * API we successfully completed request.
				 */
				err = 0;
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			err = handle_aead_req(rctx);
			break;
		default:
			err = -EINVAL;
		}

		if (err == -EINPROGRESS)
			/* Successfully submitted request for next chunk */
			return;
	}

cb_finish:
	finish_req(rctx, err);
}

/* ==================== Kernel Cryptographic API ==================== */

/**
 * skcipher_enqueue() - Handle skcipher encrypt or decrypt request.
 * @req:	Crypto API request
 * @encrypt:	true if encrypting; false if decrypting
 *
 * Return: -EINPROGRESS if request accepted and result will be returned
 *			asynchronously
 *	   < 0 if an error
 */
static int skcipher_enqueue(struct skcipher_request *req, bool encrypt)
{
	struct iproc_reqctx_s *rctx = skcipher_request_ctx(req);
	struct iproc_ctx_s *ctx =
	    crypto_skcipher_ctx(crypto_skcipher_reqtfm(req));
	int err;

	flow_log("%s() enc:%u\n", __func__, encrypt);

	rctx->gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	rctx->parent = &req->base;
	rctx->is_encrypt = encrypt;
	rctx->bd_suppress = false;
	rctx->total_todo = req->cryptlen;
	rctx->src_sent = 0;
	rctx->total_sent = 0;
	rctx->total_received = 0;
	rctx->ctx = ctx;

	/* Initialize current position in src and dst scatterlists */
	rctx->src_sg = req->src;
	rctx->src_nents = 0;
	rctx->src_skip = 0;
	rctx->dst_sg = req->dst;
	rctx->dst_nents = 0;
	rctx->dst_skip = 0;

	if (ctx->cipher.mode == CIPHER_MODE_CBC ||
	    ctx->cipher.mode == CIPHER_MODE_CTR ||
	    ctx->cipher.mode == CIPHER_MODE_OFB ||
	    ctx->cipher.mode == CIPHER_MODE_XTS ||
	    ctx->cipher.mode == CIPHER_MODE_GCM ||
	    ctx->cipher.mode == CIPHER_MODE_CCM) {
		rctx->iv_ctr_len =
		    crypto_skcipher_ivsize(crypto_skcipher_reqtfm(req));
		memcpy(rctx->msg_buf.iv_ctr, req->iv, rctx->iv_ctr_len);
	} else {
		rctx->iv_ctr_len = 0;
	}

	/* Choose a SPU to process this request */
	rctx->chan_idx = select_channel();
	err = handle_skcipher_req(rctx);
	if (err != -EINPROGRESS)
		/* synchronous result */
		spu_chunk_cleanup(rctx);

	return err;
}

static int des_setkey(struct crypto_skcipher *cipher, const u8 *key,
		      unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_skcipher_ctx(cipher);
	int err;

	err = verify_skcipher_des_key(cipher, key);
	if (err)
		return err;

	ctx->cipher_type = CIPHER_TYPE_DES;
	return 0;
}

static int threedes_setkey(struct crypto_skcipher *cipher, const u8 *key,
			   unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_skcipher_ctx(cipher);
	int err;

	err = verify_skcipher_des3_key(cipher, key);
	if (err)
		return err;

	ctx->cipher_type = CIPHER_TYPE_3DES;
	return 0;
}

static int aes_setkey(struct crypto_skcipher *cipher, const u8 *key,
		      unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_skcipher_ctx(cipher);

	if (ctx->cipher.mode == CIPHER_MODE_XTS)
		/* XTS includes two keys of equal length */
		keylen = keylen / 2;

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->cipher_type = CIPHER_TYPE_AES128;
		break;
	case AES_KEYSIZE_192:
		ctx->cipher_type = CIPHER_TYPE_AES192;
		break;
	case AES_KEYSIZE_256:
		ctx->cipher_type = CIPHER_TYPE_AES256;
		break;
	default:
		return -EINVAL;
	}
	WARN_ON((ctx->max_payload != SPU_MAX_PAYLOAD_INF) &&
		((ctx->max_payload % AES_BLOCK_SIZE) != 0));
	return 0;
}

static int skcipher_setkey(struct crypto_skcipher *cipher, const u8 *key,
			     unsigned int keylen)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct iproc_ctx_s *ctx = crypto_skcipher_ctx(cipher);
	struct spu_cipher_parms cipher_parms;
	u32 alloc_len = 0;
	int err;

	flow_log("skcipher_setkey() keylen: %d\n", keylen);
	flow_dump("  key: ", key, keylen);

	switch (ctx->cipher.alg) {
	case CIPHER_ALG_DES:
		err = des_setkey(cipher, key, keylen);
		break;
	case CIPHER_ALG_3DES:
		err = threedes_setkey(cipher, key, keylen);
		break;
	case CIPHER_ALG_AES:
		err = aes_setkey(cipher, key, keylen);
		break;
	default:
		pr_err("%s() Error: unknown cipher alg\n", __func__);
		err = -EINVAL;
	}
	if (err)
		return err;

	memcpy(ctx->enckey, key, keylen);
	ctx->enckeylen = keylen;

	/* SPU needs XTS keys in the reverse order the crypto API presents */
	if ((ctx->cipher.alg == CIPHER_ALG_AES) &&
	    (ctx->cipher.mode == CIPHER_MODE_XTS)) {
		unsigned int xts_keylen = keylen / 2;

		memcpy(ctx->enckey, key + xts_keylen, xts_keylen);
		memcpy(ctx->enckey + xts_keylen, key, xts_keylen);
	}

	if (spu->spu_type == SPU_TYPE_SPUM)
		alloc_len = BCM_HDR_LEN + SPU_HEADER_ALLOC_LEN;
	else if (spu->spu_type == SPU_TYPE_SPU2)
		alloc_len = BCM_HDR_LEN + SPU2_HEADER_ALLOC_LEN;
	memset(ctx->bcm_spu_req_hdr, 0, alloc_len);
	cipher_parms.iv_buf = NULL;
	cipher_parms.iv_len = crypto_skcipher_ivsize(cipher);
	flow_log("%s: iv_len %u\n", __func__, cipher_parms.iv_len);

	cipher_parms.alg = ctx->cipher.alg;
	cipher_parms.mode = ctx->cipher.mode;
	cipher_parms.type = ctx->cipher_type;
	cipher_parms.key_buf = ctx->enckey;
	cipher_parms.key_len = ctx->enckeylen;

	/* Prepend SPU request message with BCM header */
	memcpy(ctx->bcm_spu_req_hdr, BCMHEADER, BCM_HDR_LEN);
	ctx->spu_req_hdr_len =
	    spu->spu_cipher_req_init(ctx->bcm_spu_req_hdr + BCM_HDR_LEN,
				     &cipher_parms);

	ctx->spu_resp_hdr_len = spu->spu_response_hdr_len(ctx->authkeylen,
							  ctx->enckeylen,
							  false);

	atomic_inc(&iproc_priv.setkey_cnt[SPU_OP_CIPHER]);

	return 0;
}

static int skcipher_encrypt(struct skcipher_request *req)
{
	flow_log("skcipher_encrypt() nbytes:%u\n", req->cryptlen);

	return skcipher_enqueue(req, true);
}

static int skcipher_decrypt(struct skcipher_request *req)
{
	flow_log("skcipher_decrypt() nbytes:%u\n", req->cryptlen);
	return skcipher_enqueue(req, false);
}

static int ahash_enqueue(struct ahash_request *req)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	int err;
	const char *alg_name;

	flow_log("ahash_enqueue() nbytes:%u\n", req->nbytes);

	rctx->gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	rctx->parent = &req->base;
	rctx->ctx = ctx;
	rctx->bd_suppress = true;
	memset(&rctx->mb_mssg, 0, sizeof(struct brcm_message));

	/* Initialize position in src scatterlist */
	rctx->src_sg = req->src;
	rctx->src_skip = 0;
	rctx->src_nents = 0;
	rctx->dst_sg = NULL;
	rctx->dst_skip = 0;
	rctx->dst_nents = 0;

	/* SPU2 hardware does not compute hash of zero length data */
	if ((rctx->is_final == 1) && (rctx->total_todo == 0) &&
	    (iproc_priv.spu.spu_type == SPU_TYPE_SPU2)) {
		alg_name = crypto_ahash_alg_name(tfm);
		flow_log("Doing %sfinal %s zero-len hash request in software\n",
			 rctx->is_final ? "" : "non-", alg_name);
		err = do_shash((unsigned char *)alg_name, req->result,
			       NULL, 0, NULL, 0, ctx->authkey,
			       ctx->authkeylen);
		if (err < 0)
			flow_log("Hash request failed with error %d\n", err);
		return err;
	}
	/* Choose a SPU to process this request */
	rctx->chan_idx = select_channel();

	err = handle_ahash_req(rctx);
	if (err != -EINPROGRESS)
		/* synchronous result */
		spu_chunk_cleanup(rctx);

	if (err == -EAGAIN)
		/*
		 * we saved data in hash carry, but tell crypto API
		 * we successfully completed request.
		 */
		err = 0;

	return err;
}

static int __ahash_init(struct ahash_request *req)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);

	flow_log("%s()\n", __func__);

	/* Initialize the context */
	rctx->hash_carry_len = 0;
	rctx->is_final = 0;

	rctx->total_todo = 0;
	rctx->src_sent = 0;
	rctx->total_sent = 0;
	rctx->total_received = 0;

	ctx->digestsize = crypto_ahash_digestsize(tfm);
	/* If we add a hash whose digest is larger, catch it here. */
	WARN_ON(ctx->digestsize > MAX_DIGEST_SIZE);

	rctx->is_sw_hmac = false;

	ctx->spu_resp_hdr_len = spu->spu_response_hdr_len(ctx->authkeylen, 0,
							  true);

	return 0;
}

/**
 * spu_no_incr_hash() - Determine whether incremental hashing is supported.
 * @ctx:  Crypto session context
 *
 * SPU-2 does not support incremental hashing (we'll have to revisit and
 * condition based on chip revision or device tree entry if future versions do
 * support incremental hash)
 *
 * SPU-M also doesn't support incremental hashing of AES-XCBC
 *
 * Return: true if incremental hashing is not supported
 *         false otherwise
 */
static bool spu_no_incr_hash(struct iproc_ctx_s *ctx)
{
	struct spu_hw *spu = &iproc_priv.spu;

	if (spu->spu_type == SPU_TYPE_SPU2)
		return true;

	if ((ctx->auth.alg == HASH_ALG_AES) &&
	    (ctx->auth.mode == HASH_MODE_XCBC))
		return true;

	/* Otherwise, incremental hashing is supported */
	return false;
}

static int ahash_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	const char *alg_name;
	struct crypto_shash *hash;
	int ret;
	gfp_t gfp;

	if (spu_no_incr_hash(ctx)) {
		/*
		 * If we get an incremental hashing request and it's not
		 * supported by the hardware, we need to handle it in software
		 * by calling synchronous hash functions.
		 */
		alg_name = crypto_ahash_alg_name(tfm);
		hash = crypto_alloc_shash(alg_name, 0, 0);
		if (IS_ERR(hash)) {
			ret = PTR_ERR(hash);
			goto err;
		}

		gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
		ctx->shash = kmalloc(sizeof(*ctx->shash) +
				     crypto_shash_descsize(hash), gfp);
		if (!ctx->shash) {
			ret = -ENOMEM;
			goto err_hash;
		}
		ctx->shash->tfm = hash;

		/* Set the key using data we already have from setkey */
		if (ctx->authkeylen > 0) {
			ret = crypto_shash_setkey(hash, ctx->authkey,
						  ctx->authkeylen);
			if (ret)
				goto err_shash;
		}

		/* Initialize hash w/ this key and other params */
		ret = crypto_shash_init(ctx->shash);
		if (ret)
			goto err_shash;
	} else {
		/* Otherwise call the internal function which uses SPU hw */
		ret = __ahash_init(req);
	}

	return ret;

err_shash:
	kfree(ctx->shash);
err_hash:
	crypto_free_shash(hash);
err:
	return ret;
}

static int __ahash_update(struct ahash_request *req)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);

	flow_log("ahash_update() nbytes:%u\n", req->nbytes);

	if (!req->nbytes)
		return 0;
	rctx->total_todo += req->nbytes;
	rctx->src_sent = 0;

	return ahash_enqueue(req);
}

static int ahash_update(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	u8 *tmpbuf;
	int ret;
	int nents;
	gfp_t gfp;

	if (spu_no_incr_hash(ctx)) {
		/*
		 * If we get an incremental hashing request and it's not
		 * supported by the hardware, we need to handle it in software
		 * by calling synchronous hash functions.
		 */
		if (req->src)
			nents = sg_nents(req->src);
		else
			return -EINVAL;

		/* Copy data from req scatterlist to tmp buffer */
		gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
		tmpbuf = kmalloc(req->nbytes, gfp);
		if (!tmpbuf)
			return -ENOMEM;

		if (sg_copy_to_buffer(req->src, nents, tmpbuf, req->nbytes) !=
				req->nbytes) {
			kfree(tmpbuf);
			return -EINVAL;
		}

		/* Call synchronous update */
		ret = crypto_shash_update(ctx->shash, tmpbuf, req->nbytes);
		kfree(tmpbuf);
	} else {
		/* Otherwise call the internal function which uses SPU hw */
		ret = __ahash_update(req);
	}

	return ret;
}

static int __ahash_final(struct ahash_request *req)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);

	flow_log("ahash_final() nbytes:%u\n", req->nbytes);

	rctx->is_final = 1;

	return ahash_enqueue(req);
}

static int ahash_final(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	int ret;

	if (spu_no_incr_hash(ctx)) {
		/*
		 * If we get an incremental hashing request and it's not
		 * supported by the hardware, we need to handle it in software
		 * by calling synchronous hash functions.
		 */
		ret = crypto_shash_final(ctx->shash, req->result);

		/* Done with hash, can deallocate it now */
		crypto_free_shash(ctx->shash->tfm);
		kfree(ctx->shash);

	} else {
		/* Otherwise call the internal function which uses SPU hw */
		ret = __ahash_final(req);
	}

	return ret;
}

static int __ahash_finup(struct ahash_request *req)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);

	flow_log("ahash_finup() nbytes:%u\n", req->nbytes);

	rctx->total_todo += req->nbytes;
	rctx->src_sent = 0;
	rctx->is_final = 1;

	return ahash_enqueue(req);
}

static int ahash_finup(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	u8 *tmpbuf;
	int ret;
	int nents;
	gfp_t gfp;

	if (spu_no_incr_hash(ctx)) {
		/*
		 * If we get an incremental hashing request and it's not
		 * supported by the hardware, we need to handle it in software
		 * by calling synchronous hash functions.
		 */
		if (req->src) {
			nents = sg_nents(req->src);
		} else {
			ret = -EINVAL;
			goto ahash_finup_exit;
		}

		/* Copy data from req scatterlist to tmp buffer */
		gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
		tmpbuf = kmalloc(req->nbytes, gfp);
		if (!tmpbuf) {
			ret = -ENOMEM;
			goto ahash_finup_exit;
		}

		if (sg_copy_to_buffer(req->src, nents, tmpbuf, req->nbytes) !=
				req->nbytes) {
			ret = -EINVAL;
			goto ahash_finup_free;
		}

		/* Call synchronous update */
		ret = crypto_shash_finup(ctx->shash, tmpbuf, req->nbytes,
					 req->result);
	} else {
		/* Otherwise call the internal function which uses SPU hw */
		return __ahash_finup(req);
	}
ahash_finup_free:
	kfree(tmpbuf);

ahash_finup_exit:
	/* Done with hash, can deallocate it now */
	crypto_free_shash(ctx->shash->tfm);
	kfree(ctx->shash);
	return ret;
}

static int ahash_digest(struct ahash_request *req)
{
	int err;

	flow_log("ahash_digest() nbytes:%u\n", req->nbytes);

	/* whole thing at once */
	err = __ahash_init(req);
	if (!err)
		err = __ahash_finup(req);

	return err;
}

static int ahash_setkey(struct crypto_ahash *ahash, const u8 *key,
			unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(ahash);

	flow_log("%s() ahash:%p key:%p keylen:%u\n",
		 __func__, ahash, key, keylen);
	flow_dump("  key: ", key, keylen);

	if (ctx->auth.alg == HASH_ALG_AES) {
		switch (keylen) {
		case AES_KEYSIZE_128:
			ctx->cipher_type = CIPHER_TYPE_AES128;
			break;
		case AES_KEYSIZE_192:
			ctx->cipher_type = CIPHER_TYPE_AES192;
			break;
		case AES_KEYSIZE_256:
			ctx->cipher_type = CIPHER_TYPE_AES256;
			break;
		default:
			pr_err("%s() Error: Invalid key length\n", __func__);
			return -EINVAL;
		}
	} else {
		pr_err("%s() Error: unknown hash alg\n", __func__);
		return -EINVAL;
	}
	memcpy(ctx->authkey, key, keylen);
	ctx->authkeylen = keylen;

	return 0;
}

static int ahash_export(struct ahash_request *req, void *out)
{
	const struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct spu_hash_export_s *spu_exp = (struct spu_hash_export_s *)out;

	spu_exp->total_todo = rctx->total_todo;
	spu_exp->total_sent = rctx->total_sent;
	spu_exp->is_sw_hmac = rctx->is_sw_hmac;
	memcpy(spu_exp->hash_carry, rctx->hash_carry, sizeof(rctx->hash_carry));
	spu_exp->hash_carry_len = rctx->hash_carry_len;
	memcpy(spu_exp->incr_hash, rctx->incr_hash, sizeof(rctx->incr_hash));

	return 0;
}

static int ahash_import(struct ahash_request *req, const void *in)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct spu_hash_export_s *spu_exp = (struct spu_hash_export_s *)in;

	rctx->total_todo = spu_exp->total_todo;
	rctx->total_sent = spu_exp->total_sent;
	rctx->is_sw_hmac = spu_exp->is_sw_hmac;
	memcpy(rctx->hash_carry, spu_exp->hash_carry, sizeof(rctx->hash_carry));
	rctx->hash_carry_len = spu_exp->hash_carry_len;
	memcpy(rctx->incr_hash, spu_exp->incr_hash, sizeof(rctx->incr_hash));

	return 0;
}

static int ahash_hmac_setkey(struct crypto_ahash *ahash, const u8 *key,
			     unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(ahash);
	unsigned int blocksize =
		crypto_tfm_alg_blocksize(crypto_ahash_tfm(ahash));
	unsigned int digestsize = crypto_ahash_digestsize(ahash);
	unsigned int index;
	int rc;

	flow_log("%s() ahash:%p key:%p keylen:%u blksz:%u digestsz:%u\n",
		 __func__, ahash, key, keylen, blocksize, digestsize);
	flow_dump("  key: ", key, keylen);

	if (keylen > blocksize) {
		switch (ctx->auth.alg) {
		case HASH_ALG_MD5:
			rc = do_shash("md5", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA1:
			rc = do_shash("sha1", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA224:
			rc = do_shash("sha224", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA256:
			rc = do_shash("sha256", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA384:
			rc = do_shash("sha384", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA512:
			rc = do_shash("sha512", ctx->authkey, key, keylen, NULL,
				      0, NULL, 0);
			break;
		case HASH_ALG_SHA3_224:
			rc = do_shash("sha3-224", ctx->authkey, key, keylen,
				      NULL, 0, NULL, 0);
			break;
		case HASH_ALG_SHA3_256:
			rc = do_shash("sha3-256", ctx->authkey, key, keylen,
				      NULL, 0, NULL, 0);
			break;
		case HASH_ALG_SHA3_384:
			rc = do_shash("sha3-384", ctx->authkey, key, keylen,
				      NULL, 0, NULL, 0);
			break;
		case HASH_ALG_SHA3_512:
			rc = do_shash("sha3-512", ctx->authkey, key, keylen,
				      NULL, 0, NULL, 0);
			break;
		default:
			pr_err("%s() Error: unknown hash alg\n", __func__);
			return -EINVAL;
		}
		if (rc < 0) {
			pr_err("%s() Error %d computing shash for %s\n",
			       __func__, rc, hash_alg_name[ctx->auth.alg]);
			return rc;
		}
		ctx->authkeylen = digestsize;

		flow_log("  keylen > digestsize... hashed\n");
		flow_dump("  newkey: ", ctx->authkey, ctx->authkeylen);
	} else {
		memcpy(ctx->authkey, key, keylen);
		ctx->authkeylen = keylen;
	}

	/*
	 * Full HMAC operation in SPUM is not verified,
	 * So keeping the generation of IPAD, OPAD and
	 * outer hashing in software.
	 */
	if (iproc_priv.spu.spu_type == SPU_TYPE_SPUM) {
		memcpy(ctx->ipad, ctx->authkey, ctx->authkeylen);
		memset(ctx->ipad + ctx->authkeylen, 0,
		       blocksize - ctx->authkeylen);
		ctx->authkeylen = 0;
		unsafe_memcpy(ctx->opad, ctx->ipad, blocksize,
			      "fortified memcpy causes -Wrestrict warning");

		for (index = 0; index < blocksize; index++) {
			ctx->ipad[index] ^= HMAC_IPAD_VALUE;
			ctx->opad[index] ^= HMAC_OPAD_VALUE;
		}

		flow_dump("  ipad: ", ctx->ipad, blocksize);
		flow_dump("  opad: ", ctx->opad, blocksize);
	}
	ctx->digestsize = digestsize;
	atomic_inc(&iproc_priv.setkey_cnt[SPU_OP_HMAC]);

	return 0;
}

static int ahash_hmac_init(struct ahash_request *req)
{
	int ret;
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	unsigned int blocksize =
			crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	flow_log("ahash_hmac_init()\n");

	/* init the context as a hash */
	ret = ahash_init(req);
	if (ret)
		return ret;

	if (!spu_no_incr_hash(ctx)) {
		/* SPU-M can do incr hashing but needs sw for outer HMAC */
		rctx->is_sw_hmac = true;
		ctx->auth.mode = HASH_MODE_HASH;
		/* start with a prepended ipad */
		memcpy(rctx->hash_carry, ctx->ipad, blocksize);
		rctx->hash_carry_len = blocksize;
		rctx->total_todo += blocksize;
	}

	return 0;
}

static int ahash_hmac_update(struct ahash_request *req)
{
	flow_log("ahash_hmac_update() nbytes:%u\n", req->nbytes);

	if (!req->nbytes)
		return 0;

	return ahash_update(req);
}

static int ahash_hmac_final(struct ahash_request *req)
{
	flow_log("ahash_hmac_final() nbytes:%u\n", req->nbytes);

	return ahash_final(req);
}

static int ahash_hmac_finup(struct ahash_request *req)
{
	flow_log("ahash_hmac_finupl() nbytes:%u\n", req->nbytes);

	return ahash_finup(req);
}

static int ahash_hmac_digest(struct ahash_request *req)
{
	struct iproc_reqctx_s *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_ahash_ctx(tfm);
	unsigned int blocksize =
			crypto_tfm_alg_blocksize(crypto_ahash_tfm(tfm));

	flow_log("ahash_hmac_digest() nbytes:%u\n", req->nbytes);

	/* Perform initialization and then call finup */
	__ahash_init(req);

	if (iproc_priv.spu.spu_type == SPU_TYPE_SPU2) {
		/*
		 * SPU2 supports full HMAC implementation in the
		 * hardware, need not to generate IPAD, OPAD and
		 * outer hash in software.
		 * Only for hash key len > hash block size, SPU2
		 * expects to perform hashing on the key, shorten
		 * it to digest size and feed it as hash key.
		 */
		rctx->is_sw_hmac = false;
		ctx->auth.mode = HASH_MODE_HMAC;
	} else {
		rctx->is_sw_hmac = true;
		ctx->auth.mode = HASH_MODE_HASH;
		/* start with a prepended ipad */
		memcpy(rctx->hash_carry, ctx->ipad, blocksize);
		rctx->hash_carry_len = blocksize;
		rctx->total_todo += blocksize;
	}

	return __ahash_finup(req);
}

/* aead helpers */

static int aead_need_fallback(struct aead_request *req)
{
	struct iproc_reqctx_s *rctx = aead_request_ctx(req);
	struct spu_hw *spu = &iproc_priv.spu;
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_aead_ctx(aead);
	u32 payload_len;

	/*
	 * SPU hardware cannot handle the AES-GCM/CCM case where plaintext
	 * and AAD are both 0 bytes long. So use fallback in this case.
	 */
	if (((ctx->cipher.mode == CIPHER_MODE_GCM) ||
	     (ctx->cipher.mode == CIPHER_MODE_CCM)) &&
	    (req->assoclen == 0)) {
		if ((rctx->is_encrypt && (req->cryptlen == 0)) ||
		    (!rctx->is_encrypt && (req->cryptlen == ctx->digestsize))) {
			flow_log("AES GCM/CCM needs fallback for 0 len req\n");
			return 1;
		}
	}

	/* SPU-M hardware only supports CCM digest size of 8, 12, or 16 bytes */
	if ((ctx->cipher.mode == CIPHER_MODE_CCM) &&
	    (spu->spu_type == SPU_TYPE_SPUM) &&
	    (ctx->digestsize != 8) && (ctx->digestsize != 12) &&
	    (ctx->digestsize != 16)) {
		flow_log("%s() AES CCM needs fallback for digest size %d\n",
			 __func__, ctx->digestsize);
		return 1;
	}

	/*
	 * SPU-M on NSP has an issue where AES-CCM hash is not correct
	 * when AAD size is 0
	 */
	if ((ctx->cipher.mode == CIPHER_MODE_CCM) &&
	    (spu->spu_subtype == SPU_SUBTYPE_SPUM_NSP) &&
	    (req->assoclen == 0)) {
		flow_log("%s() AES_CCM needs fallback for 0 len AAD on NSP\n",
			 __func__);
		return 1;
	}

	/*
	 * RFC4106 and RFC4543 cannot handle the case where AAD is other than
	 * 16 or 20 bytes long. So use fallback in this case.
	 */
	if (ctx->cipher.mode == CIPHER_MODE_GCM &&
	    ctx->cipher.alg == CIPHER_ALG_AES &&
	    rctx->iv_ctr_len == GCM_RFC4106_IV_SIZE &&
	    req->assoclen != 16 && req->assoclen != 20) {
		flow_log("RFC4106/RFC4543 needs fallback for assoclen"
			 " other than 16 or 20 bytes\n");
		return 1;
	}

	payload_len = req->cryptlen;
	if (spu->spu_type == SPU_TYPE_SPUM)
		payload_len += req->assoclen;

	flow_log("%s() payload len: %u\n", __func__, payload_len);

	if (ctx->max_payload == SPU_MAX_PAYLOAD_INF)
		return 0;
	else
		return payload_len > ctx->max_payload;
}

static int aead_do_fallback(struct aead_request *req, bool is_encrypt)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct iproc_reqctx_s *rctx = aead_request_ctx(req);
	struct iproc_ctx_s *ctx = crypto_tfm_ctx(tfm);
	struct aead_request *subreq;

	flow_log("%s() enc:%u\n", __func__, is_encrypt);

	if (!ctx->fallback_cipher)
		return -EINVAL;

	subreq = &rctx->req;
	aead_request_set_tfm(subreq, ctx->fallback_cipher);
	aead_request_set_callback(subreq, aead_request_flags(req),
				  req->base.complete, req->base.data);
	aead_request_set_crypt(subreq, req->src, req->dst, req->cryptlen,
			       req->iv);
	aead_request_set_ad(subreq, req->assoclen);

	return is_encrypt ? crypto_aead_encrypt(req) :
			    crypto_aead_decrypt(req);
}

static int aead_enqueue(struct aead_request *req, bool is_encrypt)
{
	struct iproc_reqctx_s *rctx = aead_request_ctx(req);
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct iproc_ctx_s *ctx = crypto_aead_ctx(aead);
	int err;

	flow_log("%s() enc:%u\n", __func__, is_encrypt);

	if (req->assoclen > MAX_ASSOC_SIZE) {
		pr_err
		    ("%s() Error: associated data too long. (%u > %u bytes)\n",
		     __func__, req->assoclen, MAX_ASSOC_SIZE);
		return -EINVAL;
	}

	rctx->gfp = (req->base.flags & (CRYPTO_TFM_REQ_MAY_BACKLOG |
		       CRYPTO_TFM_REQ_MAY_SLEEP)) ? GFP_KERNEL : GFP_ATOMIC;
	rctx->parent = &req->base;
	rctx->is_encrypt = is_encrypt;
	rctx->bd_suppress = false;
	rctx->total_todo = req->cryptlen;
	rctx->src_sent = 0;
	rctx->total_sent = 0;
	rctx->total_received = 0;
	rctx->is_sw_hmac = false;
	rctx->ctx = ctx;
	memset(&rctx->mb_mssg, 0, sizeof(struct brcm_message));

	/* assoc data is at start of src sg */
	rctx->assoc = req->src;

	/*
	 * Init current position in src scatterlist to be after assoc data.
	 * src_skip set to buffer offset where data begins. (Assoc data could
	 * end in the middle of a buffer.)
	 */
	if (spu_sg_at_offset(req->src, req->assoclen, &rctx->src_sg,
			     &rctx->src_skip) < 0) {
		pr_err("%s() Error: Unable to find start of src data\n",
		       __func__);
		return -EINVAL;
	}

	rctx->src_nents = 0;
	rctx->dst_nents = 0;
	if (req->dst == req->src) {
		rctx->dst_sg = rctx->src_sg;
		rctx->dst_skip = rctx->src_skip;
	} else {
		/*
		 * Expect req->dst to have room for assoc data followed by
		 * output data and ICV, if encrypt. So initialize dst_sg
		 * to point beyond assoc len offset.
		 */
		if (spu_sg_at_offset(req->dst, req->assoclen, &rctx->dst_sg,
				     &rctx->dst_skip) < 0) {
			pr_err("%s() Error: Unable to find start of dst data\n",
			       __func__);
			return -EINVAL;
		}
	}

	if (ctx->cipher.mode == CIPHER_MODE_CBC ||
	    ctx->cipher.mode == CIPHER_MODE_CTR ||
	    ctx->cipher.mode == CIPHER_MODE_OFB ||
	    ctx->cipher.mode == CIPHER_MODE_XTS ||
	    ctx->cipher.mode == CIPHER_MODE_GCM) {
		rctx->iv_ctr_len =
			ctx->salt_len +
			crypto_aead_ivsize(crypto_aead_reqtfm(req));
	} else if (ctx->cipher.mode == CIPHER_MODE_CCM) {
		rctx->iv_ctr_len = CCM_AES_IV_SIZE;
	} else {
		rctx->iv_ctr_len = 0;
	}

	rctx->hash_carry_len = 0;

	flow_log("  src sg: %p\n", req->src);
	flow_log("  rctx->src_sg: %p, src_skip %u\n",
		 rctx->src_sg, rctx->src_skip);
	flow_log("  assoc:  %p, assoclen %u\n", rctx->assoc, req->assoclen);
	flow_log("  dst sg: %p\n", req->dst);
	flow_log("  rctx->dst_sg: %p, dst_skip %u\n",
		 rctx->dst_sg, rctx->dst_skip);
	flow_log("  iv_ctr_len:%u\n", rctx->iv_ctr_len);
	flow_dump("  iv: ", req->iv, rctx->iv_ctr_len);
	flow_log("  authkeylen:%u\n", ctx->authkeylen);
	flow_log("  is_esp: %s\n", str_yes_no(ctx->is_esp));

	if (ctx->max_payload == SPU_MAX_PAYLOAD_INF)
		flow_log("  max_payload infinite");
	else
		flow_log("  max_payload: %u\n", ctx->max_payload);

	if (unlikely(aead_need_fallback(req)))
		return aead_do_fallback(req, is_encrypt);

	/*
	 * Do memory allocations for request after fallback check, because if we
	 * do fallback, we won't call finish_req() to dealloc.
	 */
	if (rctx->iv_ctr_len) {
		if (ctx->salt_len)
			memcpy(rctx->msg_buf.iv_ctr + ctx->salt_offset,
			       ctx->salt, ctx->salt_len);
		memcpy(rctx->msg_buf.iv_ctr + ctx->salt_offset + ctx->salt_len,
		       req->iv,
		       rctx->iv_ctr_len - ctx->salt_len - ctx->salt_offset);
	}

	rctx->chan_idx = select_channel();
	err = handle_aead_req(rctx);
	if (err != -EINPROGRESS)
		/* synchronous result */
		spu_chunk_cleanup(rctx);

	return err;
}

static int aead_authenc_setkey(struct crypto_aead *cipher,
			       const u8 *key, unsigned int keylen)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);
	struct crypto_authenc_keys keys;
	int ret;

	flow_log("%s() aead:%p key:%p keylen:%u\n", __func__, cipher, key,
		 keylen);
	flow_dump("  key: ", key, keylen);

	ret = crypto_authenc_extractkeys(&keys, key, keylen);
	if (ret)
		goto badkey;

	if (keys.enckeylen > MAX_KEY_SIZE ||
	    keys.authkeylen > MAX_KEY_SIZE)
		goto badkey;

	ctx->enckeylen = keys.enckeylen;
	ctx->authkeylen = keys.authkeylen;

	memcpy(ctx->enckey, keys.enckey, keys.enckeylen);
	/* May end up padding auth key. So make sure it's zeroed. */
	memset(ctx->authkey, 0, sizeof(ctx->authkey));
	memcpy(ctx->authkey, keys.authkey, keys.authkeylen);

	switch (ctx->alg->cipher_info.alg) {
	case CIPHER_ALG_DES:
		if (verify_aead_des_key(cipher, keys.enckey, keys.enckeylen))
			return -EINVAL;

		ctx->cipher_type = CIPHER_TYPE_DES;
		break;
	case CIPHER_ALG_3DES:
		if (verify_aead_des3_key(cipher, keys.enckey, keys.enckeylen))
			return -EINVAL;

		ctx->cipher_type = CIPHER_TYPE_3DES;
		break;
	case CIPHER_ALG_AES:
		switch (ctx->enckeylen) {
		case AES_KEYSIZE_128:
			ctx->cipher_type = CIPHER_TYPE_AES128;
			break;
		case AES_KEYSIZE_192:
			ctx->cipher_type = CIPHER_TYPE_AES192;
			break;
		case AES_KEYSIZE_256:
			ctx->cipher_type = CIPHER_TYPE_AES256;
			break;
		default:
			goto badkey;
		}
		break;
	default:
		pr_err("%s() Error: Unknown cipher alg\n", __func__);
		return -EINVAL;
	}

	flow_log("  enckeylen:%u authkeylen:%u\n", ctx->enckeylen,
		 ctx->authkeylen);
	flow_dump("  enc: ", ctx->enckey, ctx->enckeylen);
	flow_dump("  auth: ", ctx->authkey, ctx->authkeylen);

	/* setkey the fallback just in case we needto use it */
	if (ctx->fallback_cipher) {
		flow_log("  running fallback setkey()\n");

		ctx->fallback_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
		ctx->fallback_cipher->base.crt_flags |=
		    tfm->crt_flags & CRYPTO_TFM_REQ_MASK;
		ret = crypto_aead_setkey(ctx->fallback_cipher, key, keylen);
		if (ret)
			flow_log("  fallback setkey() returned:%d\n", ret);
	}

	ctx->spu_resp_hdr_len = spu->spu_response_hdr_len(ctx->authkeylen,
							  ctx->enckeylen,
							  false);

	atomic_inc(&iproc_priv.setkey_cnt[SPU_OP_AEAD]);

	return ret;

badkey:
	ctx->enckeylen = 0;
	ctx->authkeylen = 0;
	ctx->digestsize = 0;

	return -EINVAL;
}

static int aead_gcm_ccm_setkey(struct crypto_aead *cipher,
			       const u8 *key, unsigned int keylen)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);
	struct crypto_tfm *tfm = crypto_aead_tfm(cipher);

	int ret = 0;

	flow_log("%s() keylen:%u\n", __func__, keylen);
	flow_dump("  key: ", key, keylen);

	if (!ctx->is_esp)
		ctx->digestsize = keylen;

	ctx->enckeylen = keylen;
	ctx->authkeylen = 0;

	switch (ctx->enckeylen) {
	case AES_KEYSIZE_128:
		ctx->cipher_type = CIPHER_TYPE_AES128;
		break;
	case AES_KEYSIZE_192:
		ctx->cipher_type = CIPHER_TYPE_AES192;
		break;
	case AES_KEYSIZE_256:
		ctx->cipher_type = CIPHER_TYPE_AES256;
		break;
	default:
		goto badkey;
	}

	memcpy(ctx->enckey, key, ctx->enckeylen);

	flow_log("  enckeylen:%u authkeylen:%u\n", ctx->enckeylen,
		 ctx->authkeylen);
	flow_dump("  enc: ", ctx->enckey, ctx->enckeylen);
	flow_dump("  auth: ", ctx->authkey, ctx->authkeylen);

	/* setkey the fallback just in case we need to use it */
	if (ctx->fallback_cipher) {
		flow_log("  running fallback setkey()\n");

		ctx->fallback_cipher->base.crt_flags &= ~CRYPTO_TFM_REQ_MASK;
		ctx->fallback_cipher->base.crt_flags |=
		    tfm->crt_flags & CRYPTO_TFM_REQ_MASK;
		ret = crypto_aead_setkey(ctx->fallback_cipher, key,
					 keylen + ctx->salt_len);
		if (ret)
			flow_log("  fallback setkey() returned:%d\n", ret);
	}

	ctx->spu_resp_hdr_len = spu->spu_response_hdr_len(ctx->authkeylen,
							  ctx->enckeylen,
							  false);

	atomic_inc(&iproc_priv.setkey_cnt[SPU_OP_AEAD]);

	flow_log("  enckeylen:%u authkeylen:%u\n", ctx->enckeylen,
		 ctx->authkeylen);

	return ret;

badkey:
	ctx->enckeylen = 0;
	ctx->authkeylen = 0;
	ctx->digestsize = 0;

	return -EINVAL;
}

/**
 * aead_gcm_esp_setkey() - setkey() operation for ESP variant of GCM AES.
 * @cipher: AEAD structure
 * @key:    Key followed by 4 bytes of salt
 * @keylen: Length of key plus salt, in bytes
 *
 * Extracts salt from key and stores it to be prepended to IV on each request.
 * Digest is always 16 bytes
 *
 * Return: Value from generic gcm setkey.
 */
static int aead_gcm_esp_setkey(struct crypto_aead *cipher,
			       const u8 *key, unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);

	flow_log("%s\n", __func__);

	if (keylen < GCM_ESP_SALT_SIZE)
		return -EINVAL;

	ctx->salt_len = GCM_ESP_SALT_SIZE;
	ctx->salt_offset = GCM_ESP_SALT_OFFSET;
	memcpy(ctx->salt, key + keylen - GCM_ESP_SALT_SIZE, GCM_ESP_SALT_SIZE);
	keylen -= GCM_ESP_SALT_SIZE;
	ctx->digestsize = GCM_ESP_DIGESTSIZE;
	ctx->is_esp = true;
	flow_dump("salt: ", ctx->salt, GCM_ESP_SALT_SIZE);

	return aead_gcm_ccm_setkey(cipher, key, keylen);
}

/**
 * rfc4543_gcm_esp_setkey() - setkey operation for RFC4543 variant of GCM/GMAC.
 * @cipher: AEAD structure
 * @key:    Key followed by 4 bytes of salt
 * @keylen: Length of key plus salt, in bytes
 *
 * Extracts salt from key and stores it to be prepended to IV on each request.
 * Digest is always 16 bytes
 *
 * Return: Value from generic gcm setkey.
 */
static int rfc4543_gcm_esp_setkey(struct crypto_aead *cipher,
				  const u8 *key, unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);

	flow_log("%s\n", __func__);

	if (keylen < GCM_ESP_SALT_SIZE)
		return -EINVAL;

	ctx->salt_len = GCM_ESP_SALT_SIZE;
	ctx->salt_offset = GCM_ESP_SALT_OFFSET;
	memcpy(ctx->salt, key + keylen - GCM_ESP_SALT_SIZE, GCM_ESP_SALT_SIZE);
	keylen -= GCM_ESP_SALT_SIZE;
	ctx->digestsize = GCM_ESP_DIGESTSIZE;
	ctx->is_esp = true;
	ctx->is_rfc4543 = true;
	flow_dump("salt: ", ctx->salt, GCM_ESP_SALT_SIZE);

	return aead_gcm_ccm_setkey(cipher, key, keylen);
}

/**
 * aead_ccm_esp_setkey() - setkey() operation for ESP variant of CCM AES.
 * @cipher: AEAD structure
 * @key:    Key followed by 4 bytes of salt
 * @keylen: Length of key plus salt, in bytes
 *
 * Extracts salt from key and stores it to be prepended to IV on each request.
 * Digest is always 16 bytes
 *
 * Return: Value from generic ccm setkey.
 */
static int aead_ccm_esp_setkey(struct crypto_aead *cipher,
			       const u8 *key, unsigned int keylen)
{
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);

	flow_log("%s\n", __func__);

	if (keylen < CCM_ESP_SALT_SIZE)
		return -EINVAL;

	ctx->salt_len = CCM_ESP_SALT_SIZE;
	ctx->salt_offset = CCM_ESP_SALT_OFFSET;
	memcpy(ctx->salt, key + keylen - CCM_ESP_SALT_SIZE, CCM_ESP_SALT_SIZE);
	keylen -= CCM_ESP_SALT_SIZE;
	ctx->is_esp = true;
	flow_dump("salt: ", ctx->salt, CCM_ESP_SALT_SIZE);

	return aead_gcm_ccm_setkey(cipher, key, keylen);
}

static int aead_setauthsize(struct crypto_aead *cipher, unsigned int authsize)
{
	struct iproc_ctx_s *ctx = crypto_aead_ctx(cipher);
	int ret = 0;

	flow_log("%s() authkeylen:%u authsize:%u\n",
		 __func__, ctx->authkeylen, authsize);

	ctx->digestsize = authsize;

	/* setkey the fallback just in case we needto use it */
	if (ctx->fallback_cipher) {
		flow_log("  running fallback setauth()\n");

		ret = crypto_aead_setauthsize(ctx->fallback_cipher, authsize);
		if (ret)
			flow_log("  fallback setauth() returned:%d\n", ret);
	}

	return ret;
}

static int aead_encrypt(struct aead_request *req)
{
	flow_log("%s() cryptlen:%u %08x\n", __func__, req->cryptlen,
		 req->cryptlen);
	dump_sg(req->src, 0, req->cryptlen + req->assoclen);
	flow_log("  assoc_len:%u\n", req->assoclen);

	return aead_enqueue(req, true);
}

static int aead_decrypt(struct aead_request *req)
{
	flow_log("%s() cryptlen:%u\n", __func__, req->cryptlen);
	dump_sg(req->src, 0, req->cryptlen + req->assoclen);
	flow_log("  assoc_len:%u\n", req->assoclen);

	return aead_enqueue(req, false);
}

/* ==================== Supported Cipher Algorithms ==================== */

static struct iproc_alg_s driver_algs[] = {
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "gcm(aes)",
			.cra_driver_name = "gcm-aes-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK
		 },
		 .setkey = aead_gcm_ccm_setkey,
		 .ivsize = GCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_GCM,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_GCM,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "ccm(aes)",
			.cra_driver_name = "ccm-aes-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK
		 },
		 .setkey = aead_gcm_ccm_setkey,
		 .ivsize = CCM_AES_IV_SIZE,
		.maxauthsize = AES_BLOCK_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CCM,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_CCM,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "rfc4106(gcm(aes))",
			.cra_driver_name = "gcm-aes-esp-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK
		 },
		 .setkey = aead_gcm_esp_setkey,
		 .ivsize = GCM_RFC4106_IV_SIZE,
		 .maxauthsize = AES_BLOCK_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_GCM,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_GCM,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "rfc4309(ccm(aes))",
			.cra_driver_name = "ccm-aes-esp-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK
		 },
		 .setkey = aead_ccm_esp_setkey,
		 .ivsize = CCM_AES_IV_SIZE,
		 .maxauthsize = AES_BLOCK_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CCM,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_CCM,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "rfc4543(gcm(aes))",
			.cra_driver_name = "gmac-aes-esp-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK
		 },
		 .setkey = rfc4543_gcm_esp_setkey,
		 .ivsize = GCM_RFC4106_IV_SIZE,
		 .maxauthsize = AES_BLOCK_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_GCM,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_GCM,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(md5),cbc(aes))",
			.cra_driver_name = "authenc-hmac-md5-cbc-aes-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		.ivsize = AES_BLOCK_SIZE,
		.maxauthsize = MD5_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_MD5,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha1),cbc(aes))",
			.cra_driver_name = "authenc-hmac-sha1-cbc-aes-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = AES_BLOCK_SIZE,
		 .maxauthsize = SHA1_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA1,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha256),cbc(aes))",
			.cra_driver_name = "authenc-hmac-sha256-cbc-aes-iproc",
			.cra_blocksize = AES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = AES_BLOCK_SIZE,
		 .maxauthsize = SHA256_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA256,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(md5),cbc(des))",
			.cra_driver_name = "authenc-hmac-md5-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = MD5_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_MD5,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha1),cbc(des))",
			.cra_driver_name = "authenc-hmac-sha1-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = SHA1_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA1,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha224),cbc(des))",
			.cra_driver_name = "authenc-hmac-sha224-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = SHA224_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA224,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha256),cbc(des))",
			.cra_driver_name = "authenc-hmac-sha256-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = SHA256_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA256,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha384),cbc(des))",
			.cra_driver_name = "authenc-hmac-sha384-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = SHA384_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA384,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha512),cbc(des))",
			.cra_driver_name = "authenc-hmac-sha512-cbc-des-iproc",
			.cra_blocksize = DES_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES_BLOCK_SIZE,
		 .maxauthsize = SHA512_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA512,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(md5),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-md5-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = MD5_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_MD5,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha1),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha1-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = SHA1_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA1,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha224),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha224-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = SHA224_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA224,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha256),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha256-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = SHA256_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA256,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha384),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha384-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = SHA384_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA384,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AEAD,
	 .alg.aead = {
		 .base = {
			.cra_name = "authenc(hmac(sha512),cbc(des3_ede))",
			.cra_driver_name = "authenc-hmac-sha512-cbc-des3-iproc",
			.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.cra_flags = CRYPTO_ALG_NEED_FALLBACK |
				     CRYPTO_ALG_ASYNC |
				     CRYPTO_ALG_ALLOCATES_MEMORY
		 },
		 .setkey = aead_authenc_setkey,
		 .ivsize = DES3_EDE_BLOCK_SIZE,
		 .maxauthsize = SHA512_DIGEST_SIZE,
	 },
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA512,
		       .mode = HASH_MODE_HMAC,
		       },
	 .auth_first = 0,
	 },

/* SKCIPHER algorithms. */
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "cbc(des)",
			.base.cra_driver_name = "cbc-des-iproc",
			.base.cra_blocksize = DES_BLOCK_SIZE,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = DES_BLOCK_SIZE,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "ecb(des)",
			.base.cra_driver_name = "ecb-des-iproc",
			.base.cra_blocksize = DES_BLOCK_SIZE,
			.min_keysize = DES_KEY_SIZE,
			.max_keysize = DES_KEY_SIZE,
			.ivsize = 0,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_DES,
			 .mode = CIPHER_MODE_ECB,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "cbc(des3_ede)",
			.base.cra_driver_name = "cbc-des3-iproc",
			.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = DES3_EDE_BLOCK_SIZE,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "ecb(des3_ede)",
			.base.cra_driver_name = "ecb-des3-iproc",
			.base.cra_blocksize = DES3_EDE_BLOCK_SIZE,
			.min_keysize = DES3_EDE_KEY_SIZE,
			.max_keysize = DES3_EDE_KEY_SIZE,
			.ivsize = 0,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_3DES,
			 .mode = CIPHER_MODE_ECB,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "cbc(aes)",
			.base.cra_driver_name = "cbc-aes-iproc",
			.base.cra_blocksize = AES_BLOCK_SIZE,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CBC,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "ecb(aes)",
			.base.cra_driver_name = "ecb-aes-iproc",
			.base.cra_blocksize = AES_BLOCK_SIZE,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = 0,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_ECB,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "ctr(aes)",
			.base.cra_driver_name = "ctr-aes-iproc",
			.base.cra_blocksize = AES_BLOCK_SIZE,
			.min_keysize = AES_MIN_KEY_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_CTR,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },
{
	 .type = CRYPTO_ALG_TYPE_SKCIPHER,
	 .alg.skcipher = {
			.base.cra_name = "xts(aes)",
			.base.cra_driver_name = "xts-aes-iproc",
			.base.cra_blocksize = AES_BLOCK_SIZE,
			.min_keysize = 2 * AES_MIN_KEY_SIZE,
			.max_keysize = 2 * AES_MAX_KEY_SIZE,
			.ivsize = AES_BLOCK_SIZE,
			},
	 .cipher_info = {
			 .alg = CIPHER_ALG_AES,
			 .mode = CIPHER_MODE_XTS,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_NONE,
		       .mode = HASH_MODE_NONE,
		       },
	 },

/* AHASH algorithms. */
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = MD5_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "md5",
				    .cra_driver_name = "md5-iproc",
				    .cra_blocksize = MD5_BLOCK_WORDS * 4,
				    .cra_flags = CRYPTO_ALG_ASYNC |
						 CRYPTO_ALG_ALLOCATES_MEMORY,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_MD5,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = MD5_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(md5)",
				    .cra_driver_name = "hmac-md5-iproc",
				    .cra_blocksize = MD5_BLOCK_WORDS * 4,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_MD5,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA1_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha1",
				    .cra_driver_name = "sha1-iproc",
				    .cra_blocksize = SHA1_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA1,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA1_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha1)",
				    .cra_driver_name = "hmac-sha1-iproc",
				    .cra_blocksize = SHA1_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA1,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
			.halg.digestsize = SHA224_DIGEST_SIZE,
			.halg.base = {
				    .cra_name = "sha224",
				    .cra_driver_name = "sha224-iproc",
				    .cra_blocksize = SHA224_BLOCK_SIZE,
			}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA224,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA224_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha224)",
				    .cra_driver_name = "hmac-sha224-iproc",
				    .cra_blocksize = SHA224_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA224,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA256_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha256",
				    .cra_driver_name = "sha256-iproc",
				    .cra_blocksize = SHA256_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA256,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA256_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha256)",
				    .cra_driver_name = "hmac-sha256-iproc",
				    .cra_blocksize = SHA256_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA256,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	.type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA384_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha384",
				    .cra_driver_name = "sha384-iproc",
				    .cra_blocksize = SHA384_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA384,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA384_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha384)",
				    .cra_driver_name = "hmac-sha384-iproc",
				    .cra_blocksize = SHA384_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA384,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA512_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha512",
				    .cra_driver_name = "sha512-iproc",
				    .cra_blocksize = SHA512_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA512,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA512_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha512)",
				    .cra_driver_name = "hmac-sha512-iproc",
				    .cra_blocksize = SHA512_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA512,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_224_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha3-224",
				    .cra_driver_name = "sha3-224-iproc",
				    .cra_blocksize = SHA3_224_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_224,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_224_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha3-224)",
				    .cra_driver_name = "hmac-sha3-224-iproc",
				    .cra_blocksize = SHA3_224_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_224,
		       .mode = HASH_MODE_HMAC
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_256_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha3-256",
				    .cra_driver_name = "sha3-256-iproc",
				    .cra_blocksize = SHA3_256_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_256,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_256_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha3-256)",
				    .cra_driver_name = "hmac-sha3-256-iproc",
				    .cra_blocksize = SHA3_256_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_256,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_384_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha3-384",
				    .cra_driver_name = "sha3-384-iproc",
				    .cra_blocksize = SHA3_224_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_384,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_384_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha3-384)",
				    .cra_driver_name = "hmac-sha3-384-iproc",
				    .cra_blocksize = SHA3_384_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_384,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_512_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "sha3-512",
				    .cra_driver_name = "sha3-512-iproc",
				    .cra_blocksize = SHA3_512_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_512,
		       .mode = HASH_MODE_HASH,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = SHA3_512_DIGEST_SIZE,
		      .halg.base = {
				    .cra_name = "hmac(sha3-512)",
				    .cra_driver_name = "hmac-sha3-512-iproc",
				    .cra_blocksize = SHA3_512_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_SHA3_512,
		       .mode = HASH_MODE_HMAC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = AES_BLOCK_SIZE,
		      .halg.base = {
				    .cra_name = "xcbc(aes)",
				    .cra_driver_name = "xcbc-aes-iproc",
				    .cra_blocksize = AES_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_XCBC,
		       },
	 },
	{
	 .type = CRYPTO_ALG_TYPE_AHASH,
	 .alg.hash = {
		      .halg.digestsize = AES_BLOCK_SIZE,
		      .halg.base = {
				    .cra_name = "cmac(aes)",
				    .cra_driver_name = "cmac-aes-iproc",
				    .cra_blocksize = AES_BLOCK_SIZE,
				}
		      },
	 .cipher_info = {
			 .alg = CIPHER_ALG_NONE,
			 .mode = CIPHER_MODE_NONE,
			 },
	 .auth_info = {
		       .alg = HASH_ALG_AES,
		       .mode = HASH_MODE_CMAC,
		       },
	 },
};

static int generic_cra_init(struct crypto_tfm *tfm,
			    struct iproc_alg_s *cipher_alg)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct iproc_ctx_s *ctx = crypto_tfm_ctx(tfm);
	unsigned int blocksize = crypto_tfm_alg_blocksize(tfm);

	flow_log("%s()\n", __func__);

	ctx->alg = cipher_alg;
	ctx->cipher = cipher_alg->cipher_info;
	ctx->auth = cipher_alg->auth_info;
	ctx->auth_first = cipher_alg->auth_first;
	ctx->max_payload = spu->spu_ctx_max_payload(ctx->cipher.alg,
						    ctx->cipher.mode,
						    blocksize);
	ctx->fallback_cipher = NULL;

	ctx->enckeylen = 0;
	ctx->authkeylen = 0;

	atomic_inc(&iproc_priv.stream_count);
	atomic_inc(&iproc_priv.session_count);

	return 0;
}

static int skcipher_init_tfm(struct crypto_skcipher *skcipher)
{
	struct crypto_tfm *tfm = crypto_skcipher_tfm(skcipher);
	struct skcipher_alg *alg = crypto_skcipher_alg(skcipher);
	struct iproc_alg_s *cipher_alg;

	flow_log("%s()\n", __func__);

	crypto_skcipher_set_reqsize(skcipher, sizeof(struct iproc_reqctx_s));

	cipher_alg = container_of(alg, struct iproc_alg_s, alg.skcipher);
	return generic_cra_init(tfm, cipher_alg);
}

static int ahash_cra_init(struct crypto_tfm *tfm)
{
	int err;
	struct crypto_alg *alg = tfm->__crt_alg;
	struct iproc_alg_s *cipher_alg;

	cipher_alg = container_of(__crypto_ahash_alg(alg), struct iproc_alg_s,
				  alg.hash);

	err = generic_cra_init(tfm, cipher_alg);
	flow_log("%s()\n", __func__);

	/*
	 * export state size has to be < 512 bytes. So don't include msg bufs
	 * in state size.
	 */
	crypto_ahash_set_reqsize(__crypto_ahash_cast(tfm),
				 sizeof(struct iproc_reqctx_s));

	return err;
}

static int aead_cra_init(struct crypto_aead *aead)
{
	unsigned int reqsize = sizeof(struct iproc_reqctx_s);
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct iproc_ctx_s *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct aead_alg *aalg = container_of(alg, struct aead_alg, base);
	struct iproc_alg_s *cipher_alg = container_of(aalg, struct iproc_alg_s,
						      alg.aead);

	int err = generic_cra_init(tfm, cipher_alg);

	flow_log("%s()\n", __func__);

	ctx->is_esp = false;
	ctx->salt_len = 0;
	ctx->salt_offset = 0;

	/* random first IV */
	get_random_bytes(ctx->iv, MAX_IV_SIZE);
	flow_dump("  iv: ", ctx->iv, MAX_IV_SIZE);

	if (err)
		goto out;

	if (!(alg->cra_flags & CRYPTO_ALG_NEED_FALLBACK))
		goto reqsize;

	flow_log("%s() creating fallback cipher\n", __func__);

	ctx->fallback_cipher = crypto_alloc_aead(alg->cra_name, 0,
						 CRYPTO_ALG_ASYNC |
						 CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(ctx->fallback_cipher)) {
		pr_err("%s() Error: failed to allocate fallback for %s\n",
		       __func__, alg->cra_name);
		return PTR_ERR(ctx->fallback_cipher);
	}

	reqsize += crypto_aead_reqsize(ctx->fallback_cipher);

reqsize:
	crypto_aead_set_reqsize(aead, reqsize);

out:
	return err;
}

static void generic_cra_exit(struct crypto_tfm *tfm)
{
	atomic_dec(&iproc_priv.session_count);
}

static void skcipher_exit_tfm(struct crypto_skcipher *tfm)
{
	generic_cra_exit(crypto_skcipher_tfm(tfm));
}

static void aead_cra_exit(struct crypto_aead *aead)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct iproc_ctx_s *ctx = crypto_tfm_ctx(tfm);

	generic_cra_exit(tfm);

	if (ctx->fallback_cipher) {
		crypto_free_aead(ctx->fallback_cipher);
		ctx->fallback_cipher = NULL;
	}
}

/**
 * spu_functions_register() - Specify hardware-specific SPU functions based on
 * SPU type read from device tree.
 * @dev:	device structure
 * @spu_type:	SPU hardware generation
 * @spu_subtype: SPU hardware version
 */
static void spu_functions_register(struct device *dev,
				   enum spu_spu_type spu_type,
				   enum spu_spu_subtype spu_subtype)
{
	struct spu_hw *spu = &iproc_priv.spu;

	if (spu_type == SPU_TYPE_SPUM) {
		dev_dbg(dev, "Registering SPUM functions");
		spu->spu_dump_msg_hdr = spum_dump_msg_hdr;
		spu->spu_payload_length = spum_payload_length;
		spu->spu_response_hdr_len = spum_response_hdr_len;
		spu->spu_hash_pad_len = spum_hash_pad_len;
		spu->spu_gcm_ccm_pad_len = spum_gcm_ccm_pad_len;
		spu->spu_assoc_resp_len = spum_assoc_resp_len;
		spu->spu_aead_ivlen = spum_aead_ivlen;
		spu->spu_hash_type = spum_hash_type;
		spu->spu_digest_size = spum_digest_size;
		spu->spu_create_request = spum_create_request;
		spu->spu_cipher_req_init = spum_cipher_req_init;
		spu->spu_cipher_req_finish = spum_cipher_req_finish;
		spu->spu_request_pad = spum_request_pad;
		spu->spu_tx_status_len = spum_tx_status_len;
		spu->spu_rx_status_len = spum_rx_status_len;
		spu->spu_status_process = spum_status_process;
		spu->spu_xts_tweak_in_payload = spum_xts_tweak_in_payload;
		spu->spu_ccm_update_iv = spum_ccm_update_iv;
		spu->spu_wordalign_padlen = spum_wordalign_padlen;
		if (spu_subtype == SPU_SUBTYPE_SPUM_NS2)
			spu->spu_ctx_max_payload = spum_ns2_ctx_max_payload;
		else
			spu->spu_ctx_max_payload = spum_nsp_ctx_max_payload;
	} else {
		dev_dbg(dev, "Registering SPU2 functions");
		spu->spu_dump_msg_hdr = spu2_dump_msg_hdr;
		spu->spu_ctx_max_payload = spu2_ctx_max_payload;
		spu->spu_payload_length = spu2_payload_length;
		spu->spu_response_hdr_len = spu2_response_hdr_len;
		spu->spu_hash_pad_len = spu2_hash_pad_len;
		spu->spu_gcm_ccm_pad_len = spu2_gcm_ccm_pad_len;
		spu->spu_assoc_resp_len = spu2_assoc_resp_len;
		spu->spu_aead_ivlen = spu2_aead_ivlen;
		spu->spu_hash_type = spu2_hash_type;
		spu->spu_digest_size = spu2_digest_size;
		spu->spu_create_request = spu2_create_request;
		spu->spu_cipher_req_init = spu2_cipher_req_init;
		spu->spu_cipher_req_finish = spu2_cipher_req_finish;
		spu->spu_request_pad = spu2_request_pad;
		spu->spu_tx_status_len = spu2_tx_status_len;
		spu->spu_rx_status_len = spu2_rx_status_len;
		spu->spu_status_process = spu2_status_process;
		spu->spu_xts_tweak_in_payload = spu2_xts_tweak_in_payload;
		spu->spu_ccm_update_iv = spu2_ccm_update_iv;
		spu->spu_wordalign_padlen = spu2_wordalign_padlen;
	}
}

/**
 * spu_mb_init() - Initialize mailbox client. Request ownership of a mailbox
 * channel for the SPU being probed.
 * @dev:  SPU driver device structure
 *
 * Return: 0 if successful
 *	   < 0 otherwise
 */
static int spu_mb_init(struct device *dev)
{
	struct mbox_client *mcl = &iproc_priv.mcl;
	int err, i;

	iproc_priv.mbox = devm_kcalloc(dev, iproc_priv.spu.num_chan,
				  sizeof(struct mbox_chan *), GFP_KERNEL);
	if (!iproc_priv.mbox)
		return -ENOMEM;

	mcl->dev = dev;
	mcl->tx_block = false;
	mcl->tx_tout = 0;
	mcl->knows_txdone = true;
	mcl->rx_callback = spu_rx_callback;
	mcl->tx_done = NULL;

	for (i = 0; i < iproc_priv.spu.num_chan; i++) {
		iproc_priv.mbox[i] = mbox_request_channel(mcl, i);
		if (IS_ERR(iproc_priv.mbox[i])) {
			err = PTR_ERR(iproc_priv.mbox[i]);
			dev_err(dev,
				"Mbox channel %d request failed with err %d",
				i, err);
			iproc_priv.mbox[i] = NULL;
			goto free_channels;
		}
	}

	return 0;
free_channels:
	for (i = 0; i < iproc_priv.spu.num_chan; i++) {
		if (iproc_priv.mbox[i])
			mbox_free_channel(iproc_priv.mbox[i]);
	}

	return err;
}

static void spu_mb_release(struct platform_device *pdev)
{
	int i;

	for (i = 0; i < iproc_priv.spu.num_chan; i++)
		mbox_free_channel(iproc_priv.mbox[i]);
}

static void spu_counters_init(void)
{
	int i;
	int j;

	atomic_set(&iproc_priv.session_count, 0);
	atomic_set(&iproc_priv.stream_count, 0);
	atomic_set(&iproc_priv.next_chan, (int)iproc_priv.spu.num_chan);
	atomic64_set(&iproc_priv.bytes_in, 0);
	atomic64_set(&iproc_priv.bytes_out, 0);
	for (i = 0; i < SPU_OP_NUM; i++) {
		atomic_set(&iproc_priv.op_counts[i], 0);
		atomic_set(&iproc_priv.setkey_cnt[i], 0);
	}
	for (i = 0; i < CIPHER_ALG_LAST; i++)
		for (j = 0; j < CIPHER_MODE_LAST; j++)
			atomic_set(&iproc_priv.cipher_cnt[i][j], 0);

	for (i = 0; i < HASH_ALG_LAST; i++) {
		atomic_set(&iproc_priv.hash_cnt[i], 0);
		atomic_set(&iproc_priv.hmac_cnt[i], 0);
	}
	for (i = 0; i < AEAD_TYPE_LAST; i++)
		atomic_set(&iproc_priv.aead_cnt[i], 0);

	atomic_set(&iproc_priv.mb_no_spc, 0);
	atomic_set(&iproc_priv.mb_send_fail, 0);
	atomic_set(&iproc_priv.bad_icv, 0);
}

static int spu_register_skcipher(struct iproc_alg_s *driver_alg)
{
	struct skcipher_alg *crypto = &driver_alg->alg.skcipher;
	int err;

	crypto->base.cra_module = THIS_MODULE;
	crypto->base.cra_priority = cipher_pri;
	crypto->base.cra_alignmask = 0;
	crypto->base.cra_ctxsize = sizeof(struct iproc_ctx_s);
	crypto->base.cra_flags = CRYPTO_ALG_ASYNC |
				 CRYPTO_ALG_ALLOCATES_MEMORY |
				 CRYPTO_ALG_KERN_DRIVER_ONLY;

	crypto->init = skcipher_init_tfm;
	crypto->exit = skcipher_exit_tfm;
	crypto->setkey = skcipher_setkey;
	crypto->encrypt = skcipher_encrypt;
	crypto->decrypt = skcipher_decrypt;

	err = crypto_register_skcipher(crypto);
	/* Mark alg as having been registered, if successful */
	if (err == 0)
		driver_alg->registered = true;
	pr_debug("  registered skcipher %s\n", crypto->base.cra_driver_name);
	return err;
}

static int spu_register_ahash(struct iproc_alg_s *driver_alg)
{
	struct spu_hw *spu = &iproc_priv.spu;
	struct ahash_alg *hash = &driver_alg->alg.hash;
	int err;

	/* AES-XCBC is the only AES hash type currently supported on SPU-M */
	if ((driver_alg->auth_info.alg == HASH_ALG_AES) &&
	    (driver_alg->auth_info.mode != HASH_MODE_XCBC) &&
	    (spu->spu_type == SPU_TYPE_SPUM))
		return 0;

	/* SHA3 algorithm variants are not registered for SPU-M or SPU2. */
	if ((driver_alg->auth_info.alg >= HASH_ALG_SHA3_224) &&
	    (spu->spu_subtype != SPU_SUBTYPE_SPU2_V2))
		return 0;

	hash->halg.base.cra_module = THIS_MODULE;
	hash->halg.base.cra_priority = hash_pri;
	hash->halg.base.cra_alignmask = 0;
	hash->halg.base.cra_ctxsize = sizeof(struct iproc_ctx_s);
	hash->halg.base.cra_init = ahash_cra_init;
	hash->halg.base.cra_exit = generic_cra_exit;
	hash->halg.base.cra_flags = CRYPTO_ALG_ASYNC |
				    CRYPTO_ALG_ALLOCATES_MEMORY;
	hash->halg.statesize = sizeof(struct spu_hash_export_s);

	if (driver_alg->auth_info.mode != HASH_MODE_HMAC) {
		hash->init = ahash_init;
		hash->update = ahash_update;
		hash->final = ahash_final;
		hash->finup = ahash_finup;
		hash->digest = ahash_digest;
		if ((driver_alg->auth_info.alg == HASH_ALG_AES) &&
		    ((driver_alg->auth_info.mode == HASH_MODE_XCBC) ||
		    (driver_alg->auth_info.mode == HASH_MODE_CMAC))) {
			hash->setkey = ahash_setkey;
		}
	} else {
		hash->setkey = ahash_hmac_setkey;
		hash->init = ahash_hmac_init;
		hash->update = ahash_hmac_update;
		hash->final = ahash_hmac_final;
		hash->finup = ahash_hmac_finup;
		hash->digest = ahash_hmac_digest;
	}
	hash->export = ahash_export;
	hash->import = ahash_import;

	err = crypto_register_ahash(hash);
	/* Mark alg as having been registered, if successful */
	if (err == 0)
		driver_alg->registered = true;
	pr_debug("  registered ahash %s\n",
		 hash->halg.base.cra_driver_name);
	return err;
}

static int spu_register_aead(struct iproc_alg_s *driver_alg)
{
	struct aead_alg *aead = &driver_alg->alg.aead;
	int err;

	aead->base.cra_module = THIS_MODULE;
	aead->base.cra_priority = aead_pri;
	aead->base.cra_alignmask = 0;
	aead->base.cra_ctxsize = sizeof(struct iproc_ctx_s);

	aead->base.cra_flags |= CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY;
	/* setkey set in alg initialization */
	aead->setauthsize = aead_setauthsize;
	aead->encrypt = aead_encrypt;
	aead->decrypt = aead_decrypt;
	aead->init = aead_cra_init;
	aead->exit = aead_cra_exit;

	err = crypto_register_aead(aead);
	/* Mark alg as having been registered, if successful */
	if (err == 0)
		driver_alg->registered = true;
	pr_debug("  registered aead %s\n", aead->base.cra_driver_name);
	return err;
}

/* register crypto algorithms the device supports */
static int spu_algs_register(struct device *dev)
{
	int i, j;
	int err;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		switch (driver_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			err = spu_register_skcipher(&driver_algs[i]);
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			err = spu_register_ahash(&driver_algs[i]);
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			err = spu_register_aead(&driver_algs[i]);
			break;
		default:
			dev_err(dev,
				"iproc-crypto: unknown alg type: %d",
				driver_algs[i].type);
			err = -EINVAL;
		}

		if (err) {
			dev_err(dev, "alg registration failed with error %d\n",
				err);
			goto err_algs;
		}
	}

	return 0;

err_algs:
	for (j = 0; j < i; j++) {
		/* Skip any algorithm not registered */
		if (!driver_algs[j].registered)
			continue;
		switch (driver_algs[j].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			crypto_unregister_skcipher(&driver_algs[j].alg.skcipher);
			driver_algs[j].registered = false;
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&driver_algs[j].alg.hash);
			driver_algs[j].registered = false;
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			crypto_unregister_aead(&driver_algs[j].alg.aead);
			driver_algs[j].registered = false;
			break;
		}
	}
	return err;
}

/* ==================== Kernel Platform API ==================== */

static struct spu_type_subtype spum_ns2_types = {
	SPU_TYPE_SPUM, SPU_SUBTYPE_SPUM_NS2
};

static struct spu_type_subtype spum_nsp_types = {
	SPU_TYPE_SPUM, SPU_SUBTYPE_SPUM_NSP
};

static struct spu_type_subtype spu2_types = {
	SPU_TYPE_SPU2, SPU_SUBTYPE_SPU2_V1
};

static struct spu_type_subtype spu2_v2_types = {
	SPU_TYPE_SPU2, SPU_SUBTYPE_SPU2_V2
};

static const struct of_device_id bcm_spu_dt_ids[] = {
	{
		.compatible = "brcm,spum-crypto",
		.data = &spum_ns2_types,
	},
	{
		.compatible = "brcm,spum-nsp-crypto",
		.data = &spum_nsp_types,
	},
	{
		.compatible = "brcm,spu2-crypto",
		.data = &spu2_types,
	},
	{
		.compatible = "brcm,spu2-v2-crypto",
		.data = &spu2_v2_types,
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, bcm_spu_dt_ids);

static int spu_dt_read(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spu_hw *spu = &iproc_priv.spu;
	struct resource *spu_ctrl_regs;
	const struct spu_type_subtype *matched_spu_type;
	struct device_node *dn = pdev->dev.of_node;
	int err, i;

	/* Count number of mailbox channels */
	spu->num_chan = of_count_phandle_with_args(dn, "mboxes", "#mbox-cells");

	matched_spu_type = of_device_get_match_data(dev);
	if (!matched_spu_type) {
		dev_err(dev, "Failed to match device\n");
		return -ENODEV;
	}

	spu->spu_type = matched_spu_type->type;
	spu->spu_subtype = matched_spu_type->subtype;

	for (i = 0; (i < MAX_SPUS) && ((spu_ctrl_regs =
		platform_get_resource(pdev, IORESOURCE_MEM, i)) != NULL); i++) {

		spu->reg_vbase[i] = devm_ioremap_resource(dev, spu_ctrl_regs);
		if (IS_ERR(spu->reg_vbase[i])) {
			err = PTR_ERR(spu->reg_vbase[i]);
			dev_err(dev, "Failed to map registers: %d\n",
				err);
			spu->reg_vbase[i] = NULL;
			return err;
		}
	}
	spu->num_spu = i;
	dev_dbg(dev, "Device has %d SPUs", spu->num_spu);

	return 0;
}

static int bcm_spu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spu_hw *spu = &iproc_priv.spu;
	int err;

	iproc_priv.pdev  = pdev;
	platform_set_drvdata(iproc_priv.pdev,
			     &iproc_priv);

	err = spu_dt_read(pdev);
	if (err < 0)
		goto failure;

	err = spu_mb_init(dev);
	if (err < 0)
		goto failure;

	if (spu->spu_type == SPU_TYPE_SPUM)
		iproc_priv.bcm_hdr_len = 8;
	else if (spu->spu_type == SPU_TYPE_SPU2)
		iproc_priv.bcm_hdr_len = 0;

	spu_functions_register(dev, spu->spu_type, spu->spu_subtype);

	spu_counters_init();

	spu_setup_debugfs();

	err = spu_algs_register(dev);
	if (err < 0)
		goto fail_reg;

	return 0;

fail_reg:
	spu_free_debugfs();
failure:
	spu_mb_release(pdev);
	dev_err(dev, "%s failed with error %d.\n", __func__, err);

	return err;
}

static void bcm_spu_remove(struct platform_device *pdev)
{
	int i;
	struct device *dev = &pdev->dev;
	char *cdn;

	for (i = 0; i < ARRAY_SIZE(driver_algs); i++) {
		/*
		 * Not all algorithms were registered, depending on whether
		 * hardware is SPU or SPU2.  So here we make sure to skip
		 * those algorithms that were not previously registered.
		 */
		if (!driver_algs[i].registered)
			continue;

		switch (driver_algs[i].type) {
		case CRYPTO_ALG_TYPE_SKCIPHER:
			crypto_unregister_skcipher(&driver_algs[i].alg.skcipher);
			dev_dbg(dev, "  unregistered cipher %s\n",
				driver_algs[i].alg.skcipher.base.cra_driver_name);
			driver_algs[i].registered = false;
			break;
		case CRYPTO_ALG_TYPE_AHASH:
			crypto_unregister_ahash(&driver_algs[i].alg.hash);
			cdn = driver_algs[i].alg.hash.halg.base.cra_driver_name;
			dev_dbg(dev, "  unregistered hash %s\n", cdn);
			driver_algs[i].registered = false;
			break;
		case CRYPTO_ALG_TYPE_AEAD:
			crypto_unregister_aead(&driver_algs[i].alg.aead);
			dev_dbg(dev, "  unregistered aead %s\n",
				driver_algs[i].alg.aead.base.cra_driver_name);
			driver_algs[i].registered = false;
			break;
		}
	}
	spu_free_debugfs();
	spu_mb_release(pdev);
}

/* ===== Kernel Module API ===== */

static struct platform_driver bcm_spu_pdriver = {
	.driver = {
		   .name = "brcm-spu-crypto",
		   .of_match_table = of_match_ptr(bcm_spu_dt_ids),
		   },
	.probe = bcm_spu_probe,
	.remove = bcm_spu_remove,
};
module_platform_driver(bcm_spu_pdriver);

MODULE_AUTHOR("Rob Rice <rob.rice@broadcom.com>");
MODULE_DESCRIPTION("Broadcom symmetric crypto offload driver");
MODULE_LICENSE("GPL v2");
