// SPDX-License-Identifier: GPL-2.0

#include <linux/of_device.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include "spacc_hal.h"
#include "spacc_core.h"

static const u8 spacc_ctrl_map[SPACC_CTRL_VER_SIZE][SPACC_CTRL_MAPSIZE] = {
	{ 0, 8, 4, 12, 24, 16, 31, 25, 26, 27, 28, 29, 14, 15 },
	{ 0, 8, 3, 12, 24, 16, 31, 25, 26, 27, 28, 29, 14, 15 },
	{ 0, 4, 8, 13, 15, 16, 24, 25, 26, 27, 28, 29, 30, 31 }
};

static const int keysizes[2][7] = {
	/*   1    2   4   8  16  32   64 */
	{ 5,   8, 16, 24, 32,  0,   0 },  /* cipher key sizes*/
	{ 8,  16, 20, 24, 32, 64, 128 },  /* hash key sizes*/
};


/* bits are 40, 64, 128, 192, 256, and top bit for hash */
static const unsigned char template[] = {
	[CRYPTO_MODE_NULL]              = 0,
	[CRYPTO_MODE_AES_ECB]           = 28,	/* AESECB 128/224/256 */
	[CRYPTO_MODE_AES_CBC]           = 28,	/* AESCBC 128/224/256 */
	[CRYPTO_MODE_AES_CTR]           = 28,	/* AESCTR 128/224/256 */
	[CRYPTO_MODE_AES_CCM]           = 28,	/* AESCCM 128/224/256 */
	[CRYPTO_MODE_AES_GCM]           = 28,	/* AESGCM 128/224/256 */
	[CRYPTO_MODE_AES_F8]            = 28,	/* AESF8  128/224/256 */
	[CRYPTO_MODE_AES_XTS]           = 20,	/* AESXTS 128/256 */
	[CRYPTO_MODE_AES_CFB]           = 28,	/* AESCFB 128/224/256 */
	[CRYPTO_MODE_AES_OFB]           = 28,	/* AESOFB 128/224/256 */
	[CRYPTO_MODE_AES_CS1]           = 28,	/* AESCS1 128/224/256 */
	[CRYPTO_MODE_AES_CS2]           = 28,	/* AESCS2 128/224/256 */
	[CRYPTO_MODE_AES_CS3]           = 28,	/* AESCS3 128/224/256 */
	[CRYPTO_MODE_MULTI2_ECB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_CBC]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_OFB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_MULTI2_CFB]        = 0,	/* MULTI2 */
	[CRYPTO_MODE_3DES_CBC]          = 8,	/* 3DES CBC */
	[CRYPTO_MODE_3DES_ECB]          = 8,	/* 3DES ECB */
	[CRYPTO_MODE_DES_CBC]           = 2,	/* DES CBC */
	[CRYPTO_MODE_DES_ECB]           = 2,	/* DES ECB */
	[CRYPTO_MODE_KASUMI_ECB]        = 4,	/* KASUMI ECB */
	[CRYPTO_MODE_KASUMI_F8]         = 4,	/* KASUMI F8 */
	[CRYPTO_MODE_SNOW3G_UEA2]       = 4,	/* SNOW3G */
	[CRYPTO_MODE_ZUC_UEA3]          = 4,	/* ZUC */
	[CRYPTO_MODE_CHACHA20_STREAM]   = 16,	/* CHACHA20 */
	[CRYPTO_MODE_CHACHA20_POLY1305] = 16,	/* CHACHA20 */
	[CRYPTO_MODE_SM4_ECB]           = 4,	/* SM4ECB 128 */
	[CRYPTO_MODE_SM4_CBC]           = 4,	/* SM4CBC 128 */
	[CRYPTO_MODE_SM4_CFB]           = 4,	/* SM4CFB 128 */
	[CRYPTO_MODE_SM4_OFB]           = 4,	/* SM4OFB 128 */
	[CRYPTO_MODE_SM4_CTR]           = 4,	/* SM4CTR 128 */
	[CRYPTO_MODE_SM4_CCM]           = 4,	/* SM4CCM 128 */
	[CRYPTO_MODE_SM4_GCM]           = 4,	/* SM4GCM 128 */
	[CRYPTO_MODE_SM4_F8]            = 4,	/* SM4F8  128 */
	[CRYPTO_MODE_SM4_XTS]           = 4,	/* SM4XTS 128 */
	[CRYPTO_MODE_SM4_CS1]           = 4,	/* SM4CS1 128 */
	[CRYPTO_MODE_SM4_CS2]           = 4,	/* SM4CS2 128 */
	[CRYPTO_MODE_SM4_CS3]           = 4,	/* SM4CS3 128 */

	[CRYPTO_MODE_HASH_MD5]          = 242,
	[CRYPTO_MODE_HMAC_MD5]          = 242,
	[CRYPTO_MODE_HASH_SHA1]         = 242,
	[CRYPTO_MODE_HMAC_SHA1]         = 242,
	[CRYPTO_MODE_HASH_SHA224]       = 242,
	[CRYPTO_MODE_HMAC_SHA224]       = 242,
	[CRYPTO_MODE_HASH_SHA256]       = 242,
	[CRYPTO_MODE_HMAC_SHA256]       = 242,
	[CRYPTO_MODE_HASH_SHA384]       = 242,
	[CRYPTO_MODE_HMAC_SHA384]       = 242,
	[CRYPTO_MODE_HASH_SHA512]       = 242,
	[CRYPTO_MODE_HMAC_SHA512]       = 242,
	[CRYPTO_MODE_HASH_SHA512_224]   = 242,
	[CRYPTO_MODE_HMAC_SHA512_224]   = 242,
	[CRYPTO_MODE_HASH_SHA512_256]   = 242,
	[CRYPTO_MODE_HMAC_SHA512_256]   = 242,
	[CRYPTO_MODE_MAC_XCBC]          = 154,	/* XaCBC */
	[CRYPTO_MODE_MAC_CMAC]          = 154,	/* CMAC */
	[CRYPTO_MODE_MAC_KASUMI_F9]     = 130,	/* KASUMI */
	[CRYPTO_MODE_MAC_SNOW3G_UIA2]   = 130,	/* SNOW */
	[CRYPTO_MODE_MAC_ZUC_UIA3]      = 130,	/* ZUC */
	[CRYPTO_MODE_MAC_POLY1305]      = 144,
	[CRYPTO_MODE_SSLMAC_MD5]        = 130,
	[CRYPTO_MODE_SSLMAC_SHA1]       = 132,
	[CRYPTO_MODE_HASH_CRC32]        = 0,
	[CRYPTO_MODE_MAC_MICHAEL]       = 129,

	[CRYPTO_MODE_HASH_SHA3_224]     = 242,
	[CRYPTO_MODE_HASH_SHA3_256]     = 242,
	[CRYPTO_MODE_HASH_SHA3_384]     = 242,
	[CRYPTO_MODE_HASH_SHA3_512]     = 242,
	[CRYPTO_MODE_HASH_SHAKE128]     = 242,
	[CRYPTO_MODE_HASH_SHAKE256]     = 242,
	[CRYPTO_MODE_HASH_CSHAKE128]    = 130,
	[CRYPTO_MODE_HASH_CSHAKE256]    = 130,
	[CRYPTO_MODE_MAC_KMAC128]       = 242,
	[CRYPTO_MODE_MAC_KMAC256]       = 242,
	[CRYPTO_MODE_MAC_KMACXOF128]    = 242,
	[CRYPTO_MODE_MAC_KMACXOF256]    = 242,
	[CRYPTO_MODE_HASH_SM3]          = 242,
	[CRYPTO_MODE_HMAC_SM3]          = 242,
	[CRYPTO_MODE_MAC_SM4_XCBC]      = 242,
	[CRYPTO_MODE_MAC_SM4_CMAC]      = 242,
};

int spacc_sg_to_ddt(struct device *dev, struct scatterlist *sg,
		    int nbytes, struct pdu_ddt *ddt, int dma_direction)
{
	struct scatterlist *sg_entry, *sgl;
	int nents, orig_nents;
	int i, rc;

	orig_nents = sg_nents(sg);
	if (orig_nents > 1) {
		sgl = sg_last(sg, orig_nents);
		if (sgl->length == 0)
			orig_nents--;
	}
	nents = dma_map_sg(dev, sg, orig_nents, dma_direction);

	if (nents <= 0)
		return -ENOMEM;

	/* require ATOMIC operations */
	rc = pdu_ddt_init(ddt, nents | 0x80000000);
	if (rc < 0) {
		dma_unmap_sg(dev, sg, nents, dma_direction);
		return -EIO;
	}

	for_each_sg(sg, sg_entry, nents, i) {
		pdu_ddt_add(ddt, sg_dma_address(sg_entry),
			    sg_dma_len(sg_entry));
	}

	dma_sync_sg_for_device(dev, sg, nents, dma_direction);

	return nents;
}

int spacc_set_operation(struct spacc_device *spacc, int handle, int op,
			u32 prot, uint32_t icvcmd, uint32_t icvoff,
			uint32_t icvsz, uint32_t sec_key)
{
	int ret = CRYPTO_OK;
	struct spacc_job *job = NULL;

	if (handle < 0 || handle > SPACC_MAX_JOBS)
		return -ENXIO;

	job = &spacc->job[handle];
	if (!job)
		return -EIO;

	job->op = op;
	if (op == OP_ENCRYPT)
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ENCRYPT);
	else
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ENCRYPT);

	switch (prot) {
	case ICV_HASH:        /* HASH of plaintext */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		break;
	case ICV_HASH_ENCRYPT:
		/* HASH the plaintext and encrypt the lot */
		/* ICV_PT and ICV_APPEND must be set too */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC);
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		 /* This mode is not valid when BIT_ALIGN != 0 */
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case ICV_ENCRYPT_HASH: /* HASH the ciphertext */
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_PT);
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC);
		break;
	case ICV_IGNORE:
		break;
	default:
		ret = -EINVAL;
	}

	job->icv_len = icvsz;

	switch (icvcmd) {
	case IP_ICV_OFFSET:
		job->icv_offset = icvoff;
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case IP_ICV_APPEND:
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_ICV_APPEND);
		break;
	case IP_ICV_IGNORE:
		break;
	default:
		ret = -EINVAL;
	}

	if (sec_key)
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_SEC_KEY);

	return ret;
}

static int _spacc_fifo_full(struct spacc_device *spacc, uint32_t prio)
{
	if (spacc->config.is_qos)
		return readl(spacc->regmap + SPACC_REG_FIFO_STAT) &
		       SPACC_FIFO_STAT_CMDX_FULL(prio);
	else
		return readl(spacc->regmap + SPACC_REG_FIFO_STAT) &
		       SPACC_FIFO_STAT_CMD0_FULL;
}

/* When proc_sz != 0 it overrides the ddt_len value
 * defined in the context referenced by 'job_idx'
 */
int spacc_packet_enqueue_ddt_ex(struct spacc_device *spacc, int use_jb,
				int job_idx, struct pdu_ddt *src_ddt,
				struct pdu_ddt *dst_ddt, u32 proc_sz,
				uint32_t aad_offset, uint32_t pre_aad_sz,
				u32 post_aad_sz, uint32_t iv_offset,
				uint32_t prio)
{
	int i;
	struct spacc_job *job;
	int ret = CRYPTO_OK, proc_len;

	if (job_idx < 0 || job_idx > SPACC_MAX_JOBS)
		return -ENXIO;

	switch (prio)  {
	case SPACC_SW_CTRL_PRIO_MED:
		if (spacc->config.cmd1_fifo_depth == 0)
			return -EINVAL;
		break;
	case SPACC_SW_CTRL_PRIO_LOW:
		if (spacc->config.cmd2_fifo_depth == 0)
			return -EINVAL;
		break;
	}

	job = &spacc->job[job_idx];
	if (!job)
		return -EIO;

	/* process any jobs in the jb*/
	if (use_jb && spacc_process_jb(spacc) != 0)
		goto fifo_full;

	if (_spacc_fifo_full(spacc, prio)) {
		if (use_jb)
			goto fifo_full;
		else
			return -EBUSY;
	}

	/* compute the length we must process, in decrypt mode
	 * with an ICV (hash, hmac or CCM modes)
	 * we must subtract the icv length from the buffer size
	 */
	if (proc_sz == SPACC_AUTO_SIZE) {
		proc_len = src_ddt->len;

		if (job->op == OP_DECRYPT &&
		    (job->hash_mode > 0 ||
		     job->enc_mode == CRYPTO_MODE_AES_CCM ||
		     job->enc_mode == CRYPTO_MODE_AES_GCM)  &&
		    !(job->ctrl & SPACC_CTRL_MASK(SPACC_CTRL_ICV_ENC)))
			proc_len = src_ddt->len - job->icv_len;
	} else {
		proc_len = proc_sz;
	}

	if (pre_aad_sz & SPACC_AADCOPY_FLAG) {
		job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_AAD_COPY);
		pre_aad_sz &= ~(SPACC_AADCOPY_FLAG);
	} else {
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_AAD_COPY);
	}

	job->pre_aad_sz  = pre_aad_sz;
	job->post_aad_sz = post_aad_sz;

	if (spacc->config.dma_type == SPACC_DMA_DDT) {
		pdu_io_cached_write(spacc->regmap + SPACC_REG_SRC_PTR,
				    (uint32_t)src_ddt->phys,
				    &spacc->cache.src_ptr);
		pdu_io_cached_write(spacc->regmap + SPACC_REG_DST_PTR,
				    (uint32_t)dst_ddt->phys,
				    &spacc->cache.dst_ptr);
	} else if (spacc->config.dma_type == SPACC_DMA_LINEAR) {
		pdu_io_cached_write(spacc->regmap + SPACC_REG_SRC_PTR,
				    (uint32_t)src_ddt->virt[0],
				    &spacc->cache.src_ptr);
		pdu_io_cached_write(spacc->regmap + SPACC_REG_DST_PTR,
				    (uint32_t)dst_ddt->virt[0],
				    &spacc->cache.dst_ptr);
	} else {
		return -EIO;
	}

	pdu_io_cached_write(spacc->regmap + SPACC_REG_PROC_LEN,
			    proc_len - job->post_aad_sz,
			    &spacc->cache.proc_len);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_ICV_LEN,
			    job->icv_len, &spacc->cache.icv_len);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_ICV_OFFSET,
			    job->icv_offset, &spacc->cache.icv_offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_PRE_AAD_LEN,
			    job->pre_aad_sz, &spacc->cache.pre_aad);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_POST_AAD_LEN,
			    job->post_aad_sz, &spacc->cache.post_aad);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_IV_OFFSET,
			    iv_offset, &spacc->cache.iv_offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_OFFSET,
			    aad_offset, &spacc->cache.offset);
	pdu_io_cached_write(spacc->regmap + SPACC_REG_AUX_INFO,
			    AUX_DIR(job->auxinfo_dir) |
			    AUX_BIT_ALIGN(job->auxinfo_bit_align) |
			    AUX_CBC_CS(job->auxinfo_cs_mode),
			    &spacc->cache.aux);

	if (job->first_use == 1) {
		writel(job->ckey_sz | SPACC_SET_KEY_CTX(job->ctx_idx),
		       spacc->regmap + SPACC_REG_KEY_SZ);
		writel(job->hkey_sz | SPACC_SET_KEY_CTX(job->ctx_idx),
		       spacc->regmap + SPACC_REG_KEY_SZ);
	}

	job->job_swid = spacc->job_next_swid;
	spacc->job_lookup[job->job_swid] = job_idx;
	spacc->job_next_swid =
		(spacc->job_next_swid + 1) % SPACC_MAX_JOBS;
	writel(SPACC_SW_CTRL_ID_SET(job->job_swid) |
	       SPACC_SW_CTRL_PRIO_SET(prio),
	       spacc->regmap + SPACC_REG_SW_CTRL);
	writel(job->ctrl, spacc->regmap + SPACC_REG_CTRL);

	/* Clear an expansion key after the first call*/
	if (job->first_use == 1) {
		job->first_use = 0;
		job->ctrl &= ~SPACC_CTRL_MASK(SPACC_CTRL_KEY_EXP);
	}

	return ret;

fifo_full:
	/* try to add a job to the job buffers*/
	i = spacc->jb_head + 1;
	if (i == SPACC_MAX_JOB_BUFFERS)
		i = 0;

	if (i == spacc->jb_tail)
		return -EBUSY;

	spacc->job_buffer[spacc->jb_head] = (struct spacc_job_buffer) {
		.active		= 1,
		.job_idx	= job_idx,
		.src		= src_ddt,
		.dst		= dst_ddt,
		.proc_sz	= proc_sz,
		.aad_offset	= aad_offset,
		.pre_aad_sz	= pre_aad_sz,
		.post_aad_sz	= post_aad_sz,
		.iv_offset	= iv_offset,
		.prio		= prio
	};

	spacc->jb_head = i;

	return CRYPTO_USED_JB;
}

int spacc_packet_enqueue_ddt(struct spacc_device *spacc, int job_idx,
			     struct pdu_ddt *src_ddt, struct pdu_ddt *dst_ddt,
			     u32 proc_sz, u32 aad_offset, uint32_t pre_aad_sz,
			     uint32_t post_aad_sz, u32 iv_offset, uint32_t prio)
{
	int ret;
	unsigned long lock_flags;

	spin_lock_irqsave(&spacc->lock, lock_flags);
	ret = spacc_packet_enqueue_ddt_ex(spacc, 1, job_idx, src_ddt,
					  dst_ddt, proc_sz, aad_offset,
					  pre_aad_sz, post_aad_sz,
					  iv_offset, prio);
	spin_unlock_irqrestore(&spacc->lock, lock_flags);

	return ret;
}

static int spacc_packet_dequeue(struct spacc_device *spacc, int job_idx)
{
	int ret = CRYPTO_OK;
	struct spacc_job *job = &spacc->job[job_idx];
	unsigned long lock_flag;

	spin_lock_irqsave(&spacc->lock, lock_flag);

	if (!job && !(job_idx == SPACC_JOB_IDX_UNUSED)) {
		ret = -EIO;
	} else if (job->job_done) {
		job->job_done  = 0;
		ret = job->job_err;
	} else {
		ret = -EINPROGRESS;
	}

	spin_unlock_irqrestore(&spacc->lock, lock_flag);

	return ret;
}

int spacc_isenabled(struct spacc_device *spacc, int mode, int keysize)
{
	int x;

	if (mode < 0 || mode > CRYPTO_MODE_LAST)
		return 0;

	if (mode == CRYPTO_MODE_NULL    ||
	    mode == CRYPTO_MODE_AES_XTS ||
	    mode == CRYPTO_MODE_SM4_XTS ||
	    mode == CRYPTO_MODE_AES_F8  ||
	    mode == CRYPTO_MODE_SM4_F8  ||
	    spacc->config.modes[mode] & 128)
		return 1;

	for (x = 0; x < 6; x++) {
		if (keysizes[0][x] == keysize) {
			if (spacc->config.modes[mode] & (1 << x))
				return 1;
			else
				return 0;
		}
	}

	return 0;
}

/* Releases a crypto context back into appropriate module's pool*/
int spacc_close(struct spacc_device *dev, int handle)
{
	return spacc_job_release(dev, handle);
}

static void spacc_static_modes(struct spacc_device *spacc, int x, int y)
{
	/* Disable the algos that as not supported here */
	switch (x) {
	case CRYPTO_MODE_AES_F8:
	case CRYPTO_MODE_AES_CFB:
	case CRYPTO_MODE_AES_OFB:
	case CRYPTO_MODE_MULTI2_ECB:
	case CRYPTO_MODE_MULTI2_CBC:
	case CRYPTO_MODE_MULTI2_CFB:
	case CRYPTO_MODE_MULTI2_OFB:
	case CRYPTO_MODE_MAC_POLY1305:
	case CRYPTO_MODE_HASH_CRC32:
		/* Disable the modes */
		spacc->config.modes[x] &= ~(1 << y);
		break;
	default:
		break;/* Algos are enabled */
	}
}

int spacc_static_config(struct spacc_device *spacc)
{

	int x, y;

	for (x = 0; x < ARRAY_SIZE(template); x++) {
		spacc->config.modes[x] = template[x];

		for (y = 0; y < (ARRAY_SIZE(keysizes[0])); y++) {
			/* List static modes */
			spacc_static_modes(spacc, x, y);
		}
	}

	return 0;
}

int spacc_clone_handle(struct spacc_device *spacc, int old_handle,
		       void *cbdata)
{
	int new_handle;

	new_handle = spacc_job_request(spacc, spacc->job[old_handle].ctx_idx);
	if (new_handle < 0)
		return new_handle;

	spacc->job[new_handle]          = spacc->job[old_handle];
	spacc->job[new_handle].job_used = new_handle;
	spacc->job[new_handle].cbdata   = cbdata;

	return new_handle;
}

/* Allocates a job for spacc module context and initialize
 * it with an appropriate type.
 */
int spacc_open(struct spacc_device *spacc, int enc, int hash, int ctxid,
	       int secure_mode, spacc_callback cb, void *cbdata)
{
	u32 ctrl = 0;
	int job_idx = 0;
	int ret = CRYPTO_OK;
	struct spacc_job *job = NULL;

	job_idx = spacc_job_request(spacc, ctxid);
	if (job_idx < 0)
		return -EIO;

	job = &spacc->job[job_idx];

	if (secure_mode && job->ctx_idx > spacc->config.num_sec_ctx) {
		pr_debug("ERR: For secure contexts");
		pr_debug("ERR: Job ctx ID is outside allowed range\n");
		spacc_job_release(spacc, job_idx);
		return -EIO;
	}

	job->auxinfo_cs_mode	= 0;
	job->auxinfo_bit_align	= 0;
	job->auxinfo_dir	= 0;
	job->icv_len		= 0;

	switch (enc) {
	case CRYPTO_MODE_NULL:
		break;
	case CRYPTO_MODE_AES_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_AES_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_AES_CS3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 3;
		break;
	case CRYPTO_MODE_AES_CTR:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CTR);
		break;
	case CRYPTO_MODE_AES_XTS:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_AES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_XTS);
		break;
	case CRYPTO_MODE_3DES_CBC:
	case CRYPTO_MODE_DES_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_DES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_3DES_ECB:
	case CRYPTO_MODE_DES_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_DES);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_CHACHA20_STREAM:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_CHACHA20);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CHACHA_STREAM);
		break;
	case CRYPTO_MODE_SM4_ECB:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_ECB);
		break;
	case CRYPTO_MODE_SM4_CBC:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		break;
	case CRYPTO_MODE_SM4_CS3:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CBC);
		job->auxinfo_cs_mode = 3;
		break;
	case CRYPTO_MODE_SM4_CTR:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_CTR);
		break;
	case CRYPTO_MODE_SM4_XTS:
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_ALG, C_SM4);
		ctrl |= SPACC_CTRL_SET(SPACC_CTRL_CIPH_MODE, CM_XTS);
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_MSG_BEGIN) |
		SPACC_CTRL_MASK(SPACC_CTRL_MSG_END);

	if (ret != CRYPTO_OK) {
		spacc_job_release(spacc, job_idx);
	} else {
		ret		= job_idx;
		job->first_use	= 1;
		job->enc_mode	= enc;
		job->hash_mode	= hash;
		job->ckey_sz	= 0;
		job->hkey_sz	= 0;
		job->job_done	= 0;
		job->job_swid	= 0;
		job->job_secure	= !!secure_mode;

		job->auxinfo_bit_align = 0;
		job->job_err	= -EINPROGRESS;
		job->ctrl	= ctrl |
				  SPACC_CTRL_SET(SPACC_CTRL_CTX_IDX,
						 job->ctx_idx);
		job->cb		= cb;
		job->cbdata	= cbdata;
	}

	return ret;
}

static int spacc_xof_stringsize_autodetect(struct spacc_device *spacc)
{
	void *virt;
	dma_addr_t dma;
	struct pdu_ddt	ddt;
	int ss, alg, i, stat;
	unsigned long spacc_ctrl[2] = {0xF400B400, 0xF400D400};
	unsigned char buf[256];
	unsigned long buflen, rbuf;
	unsigned char test_str[6] = {0x01, 0x20, 0x54, 0x45, 0x53, 0x54};
	unsigned char md[2][16] = {
			 {0xc3, 0x6d, 0x0a, 0x88, 0xfa, 0x37, 0x4c, 0x9b,
			  0x44, 0x74, 0xeb, 0x00, 0x5f, 0xe8, 0xca, 0x25},
			 {0x68, 0x77, 0x04, 0x11, 0xf8, 0xe3, 0xb0, 0x1e,
			  0x0d, 0xbf, 0x71, 0x6a, 0xe9, 0x87, 0x1a, 0x0d}};

	virt = dma_alloc_coherent(get_ddt_device(), 256, &dma, GFP_KERNEL);
	if (!virt)
		return -EIO;

	if (pdu_ddt_init(&ddt, 1)) {
		dma_free_coherent(get_ddt_device(), 256, virt, dma);
		return -EIO;
	}
	pdu_ddt_add(&ddt, dma, 256);

	/* populate registers for jobs*/
	writel((uint32_t)ddt.phys, spacc->regmap + SPACC_REG_SRC_PTR);
	writel((uint32_t)ddt.phys, spacc->regmap + SPACC_REG_DST_PTR);

	writel(16, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(16, spacc->regmap + SPACC_REG_PRE_AAD_LEN);
	writel(16, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(6, spacc->regmap + SPACC_REG_KEY_SZ);
	writel(0, spacc->regmap + SPACC_REG_SW_CTRL);

	/* repeat for 2 algorithms, CSHAKE128 and KMAC128*/
	for (alg = 0; (alg < 2) && (spacc->config.string_size == 0); alg++) {
		/* repeat for 4 string_size sizes*/
		for (ss = 0; ss < 4; ss++) {
			buflen = (32UL << ss);
			if (buflen > spacc->config.hash_page_size)
				break;

			/* clear I/O memory*/
			memset(virt, 0, 256);

			/* clear buf and then insert test string*/
			memset(buf, 0, sizeof(buf));
			memcpy(buf, test_str, sizeof(test_str));
			memcpy(buf + (buflen >> 1), test_str, sizeof(test_str));

			/* write key context */
			pdu_to_dev_s(spacc->regmap + SPACC_CTX_HASH_KEY,
				     buf,
				     spacc->config.hash_page_size >> 2,
				     spacc->config.spacc_endian);

			/* write ctrl register */
			writel(spacc_ctrl[alg], spacc->regmap + SPACC_REG_CTRL);

			/* wait for job to complete */
			for (i = 0; i < 20; i++) {
				rbuf = 0;
				rbuf = readl(spacc->regmap +
					     SPACC_REG_FIFO_STAT) &
				       SPACC_FIFO_STAT_STAT_EMPTY;
				if (!rbuf) {
					/* check result, if it matches,
					 * we have string_size
					 */
					writel(1, spacc->regmap +
					       SPACC_REG_STAT_POP);
					rbuf = 0;
					rbuf = readl(spacc->regmap +
						     SPACC_REG_STATUS);
					stat = SPACC_GET_STATUS_RET_CODE(rbuf);
					if ((!memcmp(virt, md[alg], 16)) &&
					    stat == SPACC_OK) {
						spacc->config.string_size =
								(16 << ss);
					}
					break;
				}
			}
		}
	}

	/* reset registers */
	writel(0, spacc->regmap + SPACC_REG_IRQ_CTRL);
	writel(0, spacc->regmap + SPACC_REG_IRQ_EN);
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_IRQ_STAT);

	writel(0, spacc->regmap + SPACC_REG_SRC_PTR);
	writel(0, spacc->regmap + SPACC_REG_DST_PTR);
	writel(0, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(0, spacc->regmap + SPACC_REG_PRE_AAD_LEN);

	pdu_ddt_free(&ddt);
	dma_free_coherent(get_ddt_device(), 256, virt, dma);

	return CRYPTO_OK;
}

/* free up the memory */
void spacc_fini(struct spacc_device *spacc)
{
	vfree(spacc->ctx);
	vfree(spacc->job);
}

int spacc_init(void __iomem *baseaddr, struct spacc_device *spacc,
	       struct pdu_info *info)
{
	unsigned long id;
	char version_string[3][16]  = { "SPACC", "SPACC-PDU" };
	char idx_string[2][16]      = { "(Normal Port)", "(Secure Port)" };
	char dma_type_string[4][16] = { "Unknown", "Scattergather", "Linear",
					"Unknown" };

	if (!baseaddr) {
		pr_debug("ERR: baseaddr is NULL\n");
		return -1;
	}
	if (!spacc) {
		pr_debug("ERR: spacc is NULL\n");
		return -1;
	}

	memset(spacc, 0, sizeof(*spacc));
	spin_lock_init(&spacc->lock);
	spin_lock_init(&spacc->ctx_lock);

	/* assign the baseaddr*/
	spacc->regmap = baseaddr;

	/* version info*/
	spacc->config.version     = info->spacc_version.version;
	spacc->config.pdu_version = (info->pdu_config.major << 4) |
				    info->pdu_config.minor;
	spacc->config.project     = info->spacc_version.project;
	spacc->config.is_pdu      = info->spacc_version.is_pdu;
	spacc->config.is_qos      = info->spacc_version.qos;

	/* misc*/
	spacc->config.is_partial        = info->spacc_version.partial;
	spacc->config.num_ctx           = info->spacc_config.num_ctx;
	spacc->config.ciph_page_size    = 1U <<
					  info->spacc_config.ciph_ctx_page_size;

	spacc->config.hash_page_size    = 1U <<
					  info->spacc_config.hash_ctx_page_size;

	spacc->config.dma_type          = info->spacc_config.dma_type;
	spacc->config.idx               = info->spacc_version.vspacc_idx;
	spacc->config.cmd0_fifo_depth   = info->spacc_config.cmd0_fifo_depth;
	spacc->config.cmd1_fifo_depth   = info->spacc_config.cmd1_fifo_depth;
	spacc->config.cmd2_fifo_depth   = info->spacc_config.cmd2_fifo_depth;
	spacc->config.stat_fifo_depth   = info->spacc_config.stat_fifo_depth;
	spacc->config.fifo_cnt          = 1;
	spacc->config.is_ivimport       = info->spacc_version.ivimport;

	/* ctrl register map*/
	if (spacc->config.version <= 0x4E)
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_0];
	else if (spacc->config.version <= 0x60)
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_1];
	else
		spacc->config.ctrl_map = spacc_ctrl_map[SPACC_CTRL_VER_2];

	spacc->job_next_swid   = 0;
	spacc->wdcnt           = 0;
	spacc->config.wd_timer = SPACC_WD_TIMER_INIT;

	/* version 4.10 uses IRQ,
	 * above uses WD and we don't support below 4.00
	 */
	if (spacc->config.version < 0x40) {
		pr_debug("ERR: Unsupported SPAcc version\n");
		return -EIO;
	} else if (spacc->config.version < 0x4B) {
		spacc->op_mode = SPACC_OP_MODE_IRQ;
	} else {
		spacc->op_mode = SPACC_OP_MODE_WD;
	}

	/* set threshold and enable irq
	 * on 4.11 and newer cores we can derive this
	 * from the HW reported depths.
	 */
	if (spacc->config.stat_fifo_depth == 1)
		spacc->config.ideal_stat_level = 1;
	else if (spacc->config.stat_fifo_depth <= 4)
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 1;
	else if (spacc->config.stat_fifo_depth <= 8)
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 2;
	else
		spacc->config.ideal_stat_level =
					spacc->config.stat_fifo_depth - 4;

	/* determine max PROClen value */
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_PROC_LEN);
	spacc->config.max_msg_size = readl(spacc->regmap + SPACC_REG_PROC_LEN);

	/* read config info*/
	if (spacc->config.is_pdu) {
		pr_debug("PDU:\n");
		pr_debug("   MAJOR      : %u\n", info->pdu_config.major);
		pr_debug("   MINOR      : %u\n", info->pdu_config.minor);
	}

	id = readl(spacc->regmap + SPACC_REG_ID);
	pr_debug("SPACC ID: (%08lx)\n", (unsigned long)id);
	pr_debug("   MAJOR      : %x\n", info->spacc_version.major);
	pr_debug("   MINOR      : %x\n", info->spacc_version.minor);
	pr_debug("   QOS        : %x\n", info->spacc_version.qos);
	pr_debug("   IVIMPORT   : %x\n", spacc->config.is_ivimport);

	if (spacc->config.version >= 0x48)
		pr_debug("   TYPE       : %lx (%s)\n", SPACC_ID_TYPE(id),
			version_string[SPACC_ID_TYPE(id) & 3]);

	pr_debug("   AUX        : %x\n", info->spacc_version.qos);
	pr_debug("   IDX        : %lx %s\n", SPACC_ID_VIDX(id),
			spacc->config.is_secure ?
			(idx_string[spacc->config.is_secure_port & 1]) : "");
	pr_debug("   PARTIAL    : %x\n", info->spacc_version.partial);
	pr_debug("   PROJECT    : %x\n", info->spacc_version.project);

	if (spacc->config.version >= 0x48)
		id = readl(spacc->regmap + SPACC_REG_CONFIG);
	else
		id = 0xFFFFFFFF;

	pr_debug("SPACC CFG: (%08lx)\n", id);
	pr_debug("   CTX CNT    : %u\n", info->spacc_config.num_ctx);
	pr_debug("   VSPACC CNT : %u\n", info->spacc_config.num_vspacc);
	pr_debug("   CIPH SZ    : %-3lu bytes\n", 1UL <<
				  info->spacc_config.ciph_ctx_page_size);
	pr_debug("   HASH SZ    : %-3lu bytes\n", 1UL <<
				  info->spacc_config.hash_ctx_page_size);
	pr_debug("   DMA TYPE   : %u (%s)\n", info->spacc_config.dma_type,
			dma_type_string[info->spacc_config.dma_type & 3]);
	pr_debug("   MAX PROCLEN: %lu bytes\n", (unsigned long)
				  spacc->config.max_msg_size);
	pr_debug("   FIFO CONFIG :\n");
	pr_debug("      CMD0 DEPTH: %d\n", spacc->config.cmd0_fifo_depth);

	if (spacc->config.is_qos) {
		pr_debug("      CMD1 DEPTH: %d\n",
				spacc->config.cmd1_fifo_depth);
		pr_debug("      CMD2 DEPTH: %d\n",
				spacc->config.cmd2_fifo_depth);
	}
	pr_debug("      STAT DEPTH: %d\n", spacc->config.stat_fifo_depth);

	if (spacc->config.dma_type == SPACC_DMA_DDT) {
		writel(0x1234567F, baseaddr + SPACC_REG_DST_PTR);
		writel(0xDEADBEEF, baseaddr + SPACC_REG_SRC_PTR);

		if (((readl(baseaddr + SPACC_REG_DST_PTR)) !=
					(0x1234567F & SPACC_DST_PTR_PTR)) ||
		    ((readl(baseaddr + SPACC_REG_SRC_PTR)) !=
		     (0xDEADBEEF & SPACC_SRC_PTR_PTR))) {
			pr_debug("ERR: Failed to set pointers\n");
			goto ERR;
		}
	}

	/* zero the IRQ CTRL/EN register
	 * (to make sure we're in a sane state)
	 */
	writel(0, spacc->regmap + SPACC_REG_IRQ_CTRL);
	writel(0, spacc->regmap + SPACC_REG_IRQ_EN);
	writel(0xFFFFFFFF, spacc->regmap + SPACC_REG_IRQ_STAT);

	/* init cache*/
	memset(&spacc->cache, 0, sizeof(spacc->cache));
	writel(0, spacc->regmap + SPACC_REG_SRC_PTR);
	writel(0, spacc->regmap + SPACC_REG_DST_PTR);
	writel(0, spacc->regmap + SPACC_REG_PROC_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_LEN);
	writel(0, spacc->regmap + SPACC_REG_ICV_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_PRE_AAD_LEN);
	writel(0, spacc->regmap + SPACC_REG_POST_AAD_LEN);
	writel(0, spacc->regmap + SPACC_REG_IV_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_OFFSET);
	writel(0, spacc->regmap + SPACC_REG_AUX_INFO);

	spacc->ctx = vmalloc(sizeof(struct spacc_ctx) * spacc->config.num_ctx);
	if (!spacc->ctx)
		goto ERR;

	spacc->job = vmalloc(sizeof(struct spacc_job) * SPACC_MAX_JOBS);
	if (!spacc->job)
		goto ERR;

	/* initialize job_idx and lookup table */
	spacc_job_init_all(spacc);

	/* initialize contexts */
	spacc_ctx_init_all(spacc);

	/* autodetect and set string size setting*/
	if (spacc->config.version == 0x61 || spacc->config.version >= 0x65)
		spacc_xof_stringsize_autodetect(spacc);

	return CRYPTO_OK;
ERR:
	spacc_fini(spacc);
	pr_debug("ERR: Crypto Failed\n");

	return -EIO;
}

/* callback function to initialize tasklet running */
void spacc_pop_jobs(unsigned long data)
{
	int num = 0;
	struct spacc_priv *priv =  (struct spacc_priv *)data;
	struct spacc_device *spacc = &priv->spacc;

	/* decrement the WD CNT here since
	 * now we're actually going to respond
	 * to the IRQ completely
	 */
	if (spacc->wdcnt)
		--(spacc->wdcnt);

	spacc_pop_packets(spacc, &num);
}

int spacc_remove(struct platform_device *pdev)
{
	struct spacc_device *spacc;
	struct spacc_priv *priv = platform_get_drvdata(pdev);

	/* free test vector memory*/
	spacc = &priv->spacc;
	spacc_fini(spacc);

	tasklet_kill(&priv->pop_jobs);

	/* devm functions do proper cleanup */
	pdu_mem_deinit(&pdev->dev);
	dev_dbg(&pdev->dev, "removed!\n");

	return 0;
}

int spacc_set_key_exp(struct spacc_device *spacc, int job_idx)
{
	struct spacc_ctx *ctx = NULL;
	struct spacc_job *job = NULL;

	if (job_idx < 0 || job_idx > SPACC_MAX_JOBS) {
		pr_debug("ERR: Invalid Job id specified (out of range)\n");
		return -ENXIO;
	}

	job = &spacc->job[job_idx];
	ctx = context_lookup_by_job(spacc, job_idx);

	if (!ctx) {
		pr_debug("ERR: Failed to find ctx id\n");
		return -EIO;
	}

	job->ctrl |= SPACC_CTRL_MASK(SPACC_CTRL_KEY_EXP);

	return CRYPTO_OK;
}

int spacc_compute_xcbc_key(struct spacc_device *spacc, int mode_id,
			   int job_idx, const unsigned char *key,
			   int keylen, unsigned char *xcbc_out)
{
	unsigned char *buf;
	dma_addr_t bufphys;
	struct pdu_ddt ddt;
	unsigned char iv[16];
	int err, i, handle, usecbc, ctx_idx;

	if (job_idx >= 0 && job_idx < SPACC_MAX_JOBS)
		ctx_idx = spacc->job[job_idx].ctx_idx;
	else
		ctx_idx = -1;

	if (mode_id == CRYPTO_MODE_MAC_XCBC) {
		/* figure out if we can schedule the key  */
		if (spacc_isenabled(spacc, CRYPTO_MODE_AES_ECB, 16))
			usecbc = 0;
		else if (spacc_isenabled(spacc, CRYPTO_MODE_AES_CBC, 16))
			usecbc = 1;
		else
			return -1;
	} else if (mode_id == CRYPTO_MODE_MAC_SM4_XCBC) {
		/* figure out if we can schedule the key  */
		if (spacc_isenabled(spacc, CRYPTO_MODE_SM4_ECB, 16))
			usecbc = 0;
		else if (spacc_isenabled(spacc, CRYPTO_MODE_SM4_CBC, 16))
			usecbc = 1;
		else
			return -1;
	} else {
		return -1;
	}

	memset(iv, 0, sizeof(iv));
	memset(&ddt, 0, sizeof(ddt));

	buf = dma_alloc_coherent(get_ddt_device(), 64, &bufphys, GFP_KERNEL);
	if (!buf)
		return -EINVAL;

	handle = -1;

	/* set to 1111...., 2222...., 333... */
	for (i = 0; i < 48; i++)
		buf[i] = (i >> 4) + 1;

	/* build DDT */
	err = pdu_ddt_init(&ddt, 1);
	if (err)
		goto xcbc_err;

	pdu_ddt_add(&ddt, bufphys, 48);

	/* open a handle in either CBC or ECB mode */
	if (mode_id == CRYPTO_MODE_MAC_XCBC) {
		handle = spacc_open(spacc, (usecbc ?
				    CRYPTO_MODE_AES_CBC : CRYPTO_MODE_AES_ECB),
				    CRYPTO_MODE_NULL, ctx_idx, 0, NULL, NULL);
		if (handle < 0) {
			err = handle;
			goto xcbc_err;
		}
	} else if (mode_id == CRYPTO_MODE_MAC_SM4_XCBC) {
		handle = spacc_open(spacc, (usecbc ?
				    CRYPTO_MODE_SM4_CBC : CRYPTO_MODE_SM4_ECB),
				    CRYPTO_MODE_NULL, ctx_idx, 0, NULL, NULL);
		if (handle < 0) {
			err = handle;
			goto xcbc_err;
		}
	}
	spacc_set_operation(spacc, handle, OP_ENCRYPT, 0, 0, 0, 0, 0);

	if (usecbc) {
		/* we can do the ECB work in CBC using three
		 * jobs with the IVreset to zero each time
		 */
		for (i = 0; i < 3; i++) {
			spacc_write_context(spacc, handle,
					    SPACC_CRYPTO_OPERATION, key,
					    keylen, iv, 16);
			err = spacc_packet_enqueue_ddt(spacc, handle, &ddt,
						&ddt, 16, (i * 16) |
						((i * 16) << 16), 0, 0, 0, 0);
			if (err != CRYPTO_OK)
				goto xcbc_err;

			do {
				err = spacc_packet_dequeue(spacc, handle);
			} while (err == -EINPROGRESS);
			if (err != CRYPTO_OK)
				goto xcbc_err;
		}
	} else {
		/* do the 48 bytes as a single SPAcc job this is the ideal case
		 * but only possible if ECB was enabled in the core
		 */
		spacc_write_context(spacc, handle, SPACC_CRYPTO_OPERATION,
				    key, keylen, iv, 16);
		err = spacc_packet_enqueue_ddt(spacc, handle, &ddt, &ddt, 48,
					       0, 0, 0, 0, 0);
		if (err != CRYPTO_OK)
			goto xcbc_err;

		do {
			err = spacc_packet_dequeue(spacc, handle);
		} while (err == -EINPROGRESS);
		if (err != CRYPTO_OK)
			goto xcbc_err;
	}

	/* now we can copy the key*/
	memcpy(xcbc_out, buf, 48);
	memset(buf, 0, 64);

xcbc_err:
	dma_free_coherent(get_ddt_device(), 64, buf, bufphys);
	pdu_ddt_free(&ddt);
	if (handle >= 0)
		spacc_close(spacc, handle);

	if (err)
		return -EINVAL;

	return 0;
}
