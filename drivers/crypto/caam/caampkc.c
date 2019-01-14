/*
 * caam - Freescale FSL CAAM support for Public Key Cryptography
 *
 * Copyright 2016 Freescale Semiconductor, Inc.
 *
 * There is no Shared Descriptor for PKC so that the Job Descriptor must carry
 * all the desired key parameters, input and output pointers.
 */
#include "compat.h"
#include "regs.h"
#include "intern.h"
#include "jr.h"
#include "error.h"
#include "desc_constr.h"
#include "sg_sw_sec4.h"
#include "caampkc.h"

#define DESC_RSA_PUB_LEN	(2 * CAAM_CMD_SZ + sizeof(struct rsa_pub_pdb))
#define DESC_RSA_PRIV_F1_LEN	(2 * CAAM_CMD_SZ + \
				 sizeof(struct rsa_priv_f1_pdb))
#define DESC_RSA_PRIV_F2_LEN	(2 * CAAM_CMD_SZ + \
				 sizeof(struct rsa_priv_f2_pdb))
#define DESC_RSA_PRIV_F3_LEN	(2 * CAAM_CMD_SZ + \
				 sizeof(struct rsa_priv_f3_pdb))

static void rsa_io_unmap(struct device *dev, struct rsa_edesc *edesc,
			 struct akcipher_request *req)
{
	dma_unmap_sg(dev, req->dst, edesc->dst_nents, DMA_FROM_DEVICE);
	dma_unmap_sg(dev, req->src, edesc->src_nents, DMA_TO_DEVICE);

	if (edesc->sec4_sg_bytes)
		dma_unmap_single(dev, edesc->sec4_sg_dma, edesc->sec4_sg_bytes,
				 DMA_TO_DEVICE);
}

static void rsa_pub_unmap(struct device *dev, struct rsa_edesc *edesc,
			  struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct rsa_pub_pdb *pdb = &edesc->pdb.pub;

	dma_unmap_single(dev, pdb->n_dma, key->n_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->e_dma, key->e_sz, DMA_TO_DEVICE);
}

static void rsa_priv_f1_unmap(struct device *dev, struct rsa_edesc *edesc,
			      struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct rsa_priv_f1_pdb *pdb = &edesc->pdb.priv_f1;

	dma_unmap_single(dev, pdb->n_dma, key->n_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->d_dma, key->d_sz, DMA_TO_DEVICE);
}

static void rsa_priv_f2_unmap(struct device *dev, struct rsa_edesc *edesc,
			      struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct rsa_priv_f2_pdb *pdb = &edesc->pdb.priv_f2;
	size_t p_sz = key->p_sz;
	size_t q_sz = key->q_sz;

	dma_unmap_single(dev, pdb->d_dma, key->d_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->p_dma, p_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->q_dma, q_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->tmp1_dma, p_sz, DMA_BIDIRECTIONAL);
	dma_unmap_single(dev, pdb->tmp2_dma, q_sz, DMA_BIDIRECTIONAL);
}

static void rsa_priv_f3_unmap(struct device *dev, struct rsa_edesc *edesc,
			      struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct rsa_priv_f3_pdb *pdb = &edesc->pdb.priv_f3;
	size_t p_sz = key->p_sz;
	size_t q_sz = key->q_sz;

	dma_unmap_single(dev, pdb->p_dma, p_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->q_dma, q_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->dp_dma, p_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->dq_dma, q_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->c_dma, p_sz, DMA_TO_DEVICE);
	dma_unmap_single(dev, pdb->tmp1_dma, p_sz, DMA_BIDIRECTIONAL);
	dma_unmap_single(dev, pdb->tmp2_dma, q_sz, DMA_BIDIRECTIONAL);
}

/* RSA Job Completion handler */
static void rsa_pub_done(struct device *dev, u32 *desc, u32 err, void *context)
{
	struct akcipher_request *req = context;
	struct rsa_edesc *edesc;

	if (err)
		caam_jr_strstatus(dev, err);

	edesc = container_of(desc, struct rsa_edesc, hw_desc[0]);

	rsa_pub_unmap(dev, edesc, req);
	rsa_io_unmap(dev, edesc, req);
	kfree(edesc);

	akcipher_request_complete(req, err);
}

static void rsa_priv_f1_done(struct device *dev, u32 *desc, u32 err,
			     void *context)
{
	struct akcipher_request *req = context;
	struct rsa_edesc *edesc;

	if (err)
		caam_jr_strstatus(dev, err);

	edesc = container_of(desc, struct rsa_edesc, hw_desc[0]);

	rsa_priv_f1_unmap(dev, edesc, req);
	rsa_io_unmap(dev, edesc, req);
	kfree(edesc);

	akcipher_request_complete(req, err);
}

static void rsa_priv_f2_done(struct device *dev, u32 *desc, u32 err,
			     void *context)
{
	struct akcipher_request *req = context;
	struct rsa_edesc *edesc;

	if (err)
		caam_jr_strstatus(dev, err);

	edesc = container_of(desc, struct rsa_edesc, hw_desc[0]);

	rsa_priv_f2_unmap(dev, edesc, req);
	rsa_io_unmap(dev, edesc, req);
	kfree(edesc);

	akcipher_request_complete(req, err);
}

static void rsa_priv_f3_done(struct device *dev, u32 *desc, u32 err,
			     void *context)
{
	struct akcipher_request *req = context;
	struct rsa_edesc *edesc;

	if (err)
		caam_jr_strstatus(dev, err);

	edesc = container_of(desc, struct rsa_edesc, hw_desc[0]);

	rsa_priv_f3_unmap(dev, edesc, req);
	rsa_io_unmap(dev, edesc, req);
	kfree(edesc);

	akcipher_request_complete(req, err);
}

static int caam_rsa_count_leading_zeros(struct scatterlist *sgl,
					unsigned int nbytes,
					unsigned int flags)
{
	struct sg_mapping_iter miter;
	int lzeros, ents;
	unsigned int len;
	unsigned int tbytes = nbytes;
	const u8 *buff;

	ents = sg_nents_for_len(sgl, nbytes);
	if (ents < 0)
		return ents;

	sg_miter_start(&miter, sgl, ents, SG_MITER_FROM_SG | flags);

	lzeros = 0;
	len = 0;
	while (nbytes > 0) {
		while (len && !*buff) {
			lzeros++;
			len--;
			buff++;
		}

		if (len && *buff)
			break;

		sg_miter_next(&miter);
		buff = miter.addr;
		len = miter.length;

		nbytes -= lzeros;
		lzeros = 0;
	}

	miter.consumed = lzeros;
	sg_miter_stop(&miter);
	nbytes -= lzeros;

	return tbytes - nbytes;
}

static struct rsa_edesc *rsa_edesc_alloc(struct akcipher_request *req,
					 size_t desclen)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *dev = ctx->dev;
	struct caam_rsa_req_ctx *req_ctx = akcipher_request_ctx(req);
	struct rsa_edesc *edesc;
	gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
		       GFP_KERNEL : GFP_ATOMIC;
	int sg_flags = (flags == GFP_ATOMIC) ? SG_MITER_ATOMIC : 0;
	int sgc;
	int sec4_sg_index, sec4_sg_len = 0, sec4_sg_bytes;
	int src_nents, dst_nents;
	int lzeros;

	lzeros = caam_rsa_count_leading_zeros(req->src, req->src_len, sg_flags);
	if (lzeros < 0)
		return ERR_PTR(lzeros);

	req->src_len -= lzeros;
	req->src = scatterwalk_ffwd(req_ctx->src, req->src, lzeros);

	src_nents = sg_nents_for_len(req->src, req->src_len);
	dst_nents = sg_nents_for_len(req->dst, req->dst_len);

	if (src_nents > 1)
		sec4_sg_len = src_nents;
	if (dst_nents > 1)
		sec4_sg_len += dst_nents;

	sec4_sg_bytes = sec4_sg_len * sizeof(struct sec4_sg_entry);

	/* allocate space for base edesc, hw desc commands and link tables */
	edesc = kzalloc(sizeof(*edesc) + desclen + sec4_sg_bytes,
			GFP_DMA | flags);
	if (!edesc)
		return ERR_PTR(-ENOMEM);

	sgc = dma_map_sg(dev, req->src, src_nents, DMA_TO_DEVICE);
	if (unlikely(!sgc)) {
		dev_err(dev, "unable to map source\n");
		goto src_fail;
	}

	sgc = dma_map_sg(dev, req->dst, dst_nents, DMA_FROM_DEVICE);
	if (unlikely(!sgc)) {
		dev_err(dev, "unable to map destination\n");
		goto dst_fail;
	}

	edesc->sec4_sg = (void *)edesc + sizeof(*edesc) + desclen;

	sec4_sg_index = 0;
	if (src_nents > 1) {
		sg_to_sec4_sg_last(req->src, src_nents, edesc->sec4_sg, 0);
		sec4_sg_index += src_nents;
	}
	if (dst_nents > 1)
		sg_to_sec4_sg_last(req->dst, dst_nents,
				   edesc->sec4_sg + sec4_sg_index, 0);

	/* Save nents for later use in Job Descriptor */
	edesc->src_nents = src_nents;
	edesc->dst_nents = dst_nents;

	if (!sec4_sg_bytes)
		return edesc;

	edesc->sec4_sg_dma = dma_map_single(dev, edesc->sec4_sg,
					    sec4_sg_bytes, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, edesc->sec4_sg_dma)) {
		dev_err(dev, "unable to map S/G table\n");
		goto sec4_sg_fail;
	}

	edesc->sec4_sg_bytes = sec4_sg_bytes;

	return edesc;

sec4_sg_fail:
	dma_unmap_sg(dev, req->dst, dst_nents, DMA_FROM_DEVICE);
dst_fail:
	dma_unmap_sg(dev, req->src, src_nents, DMA_TO_DEVICE);
src_fail:
	kfree(edesc);
	return ERR_PTR(-ENOMEM);
}

static int set_rsa_pub_pdb(struct akcipher_request *req,
			   struct rsa_edesc *edesc)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct device *dev = ctx->dev;
	struct rsa_pub_pdb *pdb = &edesc->pdb.pub;
	int sec4_sg_index = 0;

	pdb->n_dma = dma_map_single(dev, key->n, key->n_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->n_dma)) {
		dev_err(dev, "Unable to map RSA modulus memory\n");
		return -ENOMEM;
	}

	pdb->e_dma = dma_map_single(dev, key->e, key->e_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->e_dma)) {
		dev_err(dev, "Unable to map RSA public exponent memory\n");
		dma_unmap_single(dev, pdb->n_dma, key->n_sz, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	if (edesc->src_nents > 1) {
		pdb->sgf |= RSA_PDB_SGF_F;
		pdb->f_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
	} else {
		pdb->f_dma = sg_dma_address(req->src);
	}

	if (edesc->dst_nents > 1) {
		pdb->sgf |= RSA_PDB_SGF_G;
		pdb->g_dma = edesc->sec4_sg_dma +
			     sec4_sg_index * sizeof(struct sec4_sg_entry);
	} else {
		pdb->g_dma = sg_dma_address(req->dst);
	}

	pdb->sgf |= (key->e_sz << RSA_PDB_E_SHIFT) | key->n_sz;
	pdb->f_len = req->src_len;

	return 0;
}

static int set_rsa_priv_f1_pdb(struct akcipher_request *req,
			       struct rsa_edesc *edesc)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct device *dev = ctx->dev;
	struct rsa_priv_f1_pdb *pdb = &edesc->pdb.priv_f1;
	int sec4_sg_index = 0;

	pdb->n_dma = dma_map_single(dev, key->n, key->n_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->n_dma)) {
		dev_err(dev, "Unable to map modulus memory\n");
		return -ENOMEM;
	}

	pdb->d_dma = dma_map_single(dev, key->d, key->d_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->d_dma)) {
		dev_err(dev, "Unable to map RSA private exponent memory\n");
		dma_unmap_single(dev, pdb->n_dma, key->n_sz, DMA_TO_DEVICE);
		return -ENOMEM;
	}

	if (edesc->src_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_G;
		pdb->g_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
	} else {
		pdb->g_dma = sg_dma_address(req->src);
	}

	if (edesc->dst_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_F;
		pdb->f_dma = edesc->sec4_sg_dma +
			     sec4_sg_index * sizeof(struct sec4_sg_entry);
	} else {
		pdb->f_dma = sg_dma_address(req->dst);
	}

	pdb->sgf |= (key->d_sz << RSA_PDB_D_SHIFT) | key->n_sz;

	return 0;
}

static int set_rsa_priv_f2_pdb(struct akcipher_request *req,
			       struct rsa_edesc *edesc)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct device *dev = ctx->dev;
	struct rsa_priv_f2_pdb *pdb = &edesc->pdb.priv_f2;
	int sec4_sg_index = 0;
	size_t p_sz = key->p_sz;
	size_t q_sz = key->q_sz;

	pdb->d_dma = dma_map_single(dev, key->d, key->d_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->d_dma)) {
		dev_err(dev, "Unable to map RSA private exponent memory\n");
		return -ENOMEM;
	}

	pdb->p_dma = dma_map_single(dev, key->p, p_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->p_dma)) {
		dev_err(dev, "Unable to map RSA prime factor p memory\n");
		goto unmap_d;
	}

	pdb->q_dma = dma_map_single(dev, key->q, q_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->q_dma)) {
		dev_err(dev, "Unable to map RSA prime factor q memory\n");
		goto unmap_p;
	}

	pdb->tmp1_dma = dma_map_single(dev, key->tmp1, p_sz, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, pdb->tmp1_dma)) {
		dev_err(dev, "Unable to map RSA tmp1 memory\n");
		goto unmap_q;
	}

	pdb->tmp2_dma = dma_map_single(dev, key->tmp2, q_sz, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, pdb->tmp2_dma)) {
		dev_err(dev, "Unable to map RSA tmp2 memory\n");
		goto unmap_tmp1;
	}

	if (edesc->src_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_G;
		pdb->g_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
	} else {
		pdb->g_dma = sg_dma_address(req->src);
	}

	if (edesc->dst_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_F;
		pdb->f_dma = edesc->sec4_sg_dma +
			     sec4_sg_index * sizeof(struct sec4_sg_entry);
	} else {
		pdb->f_dma = sg_dma_address(req->dst);
	}

	pdb->sgf |= (key->d_sz << RSA_PDB_D_SHIFT) | key->n_sz;
	pdb->p_q_len = (q_sz << RSA_PDB_Q_SHIFT) | p_sz;

	return 0;

unmap_tmp1:
	dma_unmap_single(dev, pdb->tmp1_dma, p_sz, DMA_BIDIRECTIONAL);
unmap_q:
	dma_unmap_single(dev, pdb->q_dma, q_sz, DMA_TO_DEVICE);
unmap_p:
	dma_unmap_single(dev, pdb->p_dma, p_sz, DMA_TO_DEVICE);
unmap_d:
	dma_unmap_single(dev, pdb->d_dma, key->d_sz, DMA_TO_DEVICE);

	return -ENOMEM;
}

static int set_rsa_priv_f3_pdb(struct akcipher_request *req,
			       struct rsa_edesc *edesc)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct device *dev = ctx->dev;
	struct rsa_priv_f3_pdb *pdb = &edesc->pdb.priv_f3;
	int sec4_sg_index = 0;
	size_t p_sz = key->p_sz;
	size_t q_sz = key->q_sz;

	pdb->p_dma = dma_map_single(dev, key->p, p_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->p_dma)) {
		dev_err(dev, "Unable to map RSA prime factor p memory\n");
		return -ENOMEM;
	}

	pdb->q_dma = dma_map_single(dev, key->q, q_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->q_dma)) {
		dev_err(dev, "Unable to map RSA prime factor q memory\n");
		goto unmap_p;
	}

	pdb->dp_dma = dma_map_single(dev, key->dp, p_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->dp_dma)) {
		dev_err(dev, "Unable to map RSA exponent dp memory\n");
		goto unmap_q;
	}

	pdb->dq_dma = dma_map_single(dev, key->dq, q_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->dq_dma)) {
		dev_err(dev, "Unable to map RSA exponent dq memory\n");
		goto unmap_dp;
	}

	pdb->c_dma = dma_map_single(dev, key->qinv, p_sz, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, pdb->c_dma)) {
		dev_err(dev, "Unable to map RSA CRT coefficient qinv memory\n");
		goto unmap_dq;
	}

	pdb->tmp1_dma = dma_map_single(dev, key->tmp1, p_sz, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, pdb->tmp1_dma)) {
		dev_err(dev, "Unable to map RSA tmp1 memory\n");
		goto unmap_qinv;
	}

	pdb->tmp2_dma = dma_map_single(dev, key->tmp2, q_sz, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, pdb->tmp2_dma)) {
		dev_err(dev, "Unable to map RSA tmp2 memory\n");
		goto unmap_tmp1;
	}

	if (edesc->src_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_G;
		pdb->g_dma = edesc->sec4_sg_dma;
		sec4_sg_index += edesc->src_nents;
	} else {
		pdb->g_dma = sg_dma_address(req->src);
	}

	if (edesc->dst_nents > 1) {
		pdb->sgf |= RSA_PRIV_PDB_SGF_F;
		pdb->f_dma = edesc->sec4_sg_dma +
			     sec4_sg_index * sizeof(struct sec4_sg_entry);
	} else {
		pdb->f_dma = sg_dma_address(req->dst);
	}

	pdb->sgf |= key->n_sz;
	pdb->p_q_len = (q_sz << RSA_PDB_Q_SHIFT) | p_sz;

	return 0;

unmap_tmp1:
	dma_unmap_single(dev, pdb->tmp1_dma, p_sz, DMA_BIDIRECTIONAL);
unmap_qinv:
	dma_unmap_single(dev, pdb->c_dma, p_sz, DMA_TO_DEVICE);
unmap_dq:
	dma_unmap_single(dev, pdb->dq_dma, q_sz, DMA_TO_DEVICE);
unmap_dp:
	dma_unmap_single(dev, pdb->dp_dma, p_sz, DMA_TO_DEVICE);
unmap_q:
	dma_unmap_single(dev, pdb->q_dma, q_sz, DMA_TO_DEVICE);
unmap_p:
	dma_unmap_single(dev, pdb->p_dma, p_sz, DMA_TO_DEVICE);

	return -ENOMEM;
}

static int caam_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	struct device *jrdev = ctx->dev;
	struct rsa_edesc *edesc;
	int ret;

	if (unlikely(!key->n || !key->e))
		return -EINVAL;

	if (req->dst_len < key->n_sz) {
		req->dst_len = key->n_sz;
		dev_err(jrdev, "Output buffer length less than parameter n\n");
		return -EOVERFLOW;
	}

	/* Allocate extended descriptor */
	edesc = rsa_edesc_alloc(req, DESC_RSA_PUB_LEN);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Set RSA Encrypt Protocol Data Block */
	ret = set_rsa_pub_pdb(req, edesc);
	if (ret)
		goto init_fail;

	/* Initialize Job Descriptor */
	init_rsa_pub_desc(edesc->hw_desc, &edesc->pdb.pub);

	ret = caam_jr_enqueue(jrdev, edesc->hw_desc, rsa_pub_done, req);
	if (!ret)
		return -EINPROGRESS;

	rsa_pub_unmap(jrdev, edesc, req);

init_fail:
	rsa_io_unmap(jrdev, edesc, req);
	kfree(edesc);
	return ret;
}

static int caam_rsa_dec_priv_f1(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *jrdev = ctx->dev;
	struct rsa_edesc *edesc;
	int ret;

	/* Allocate extended descriptor */
	edesc = rsa_edesc_alloc(req, DESC_RSA_PRIV_F1_LEN);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Set RSA Decrypt Protocol Data Block - Private Key Form #1 */
	ret = set_rsa_priv_f1_pdb(req, edesc);
	if (ret)
		goto init_fail;

	/* Initialize Job Descriptor */
	init_rsa_priv_f1_desc(edesc->hw_desc, &edesc->pdb.priv_f1);

	ret = caam_jr_enqueue(jrdev, edesc->hw_desc, rsa_priv_f1_done, req);
	if (!ret)
		return -EINPROGRESS;

	rsa_priv_f1_unmap(jrdev, edesc, req);

init_fail:
	rsa_io_unmap(jrdev, edesc, req);
	kfree(edesc);
	return ret;
}

static int caam_rsa_dec_priv_f2(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *jrdev = ctx->dev;
	struct rsa_edesc *edesc;
	int ret;

	/* Allocate extended descriptor */
	edesc = rsa_edesc_alloc(req, DESC_RSA_PRIV_F2_LEN);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Set RSA Decrypt Protocol Data Block - Private Key Form #2 */
	ret = set_rsa_priv_f2_pdb(req, edesc);
	if (ret)
		goto init_fail;

	/* Initialize Job Descriptor */
	init_rsa_priv_f2_desc(edesc->hw_desc, &edesc->pdb.priv_f2);

	ret = caam_jr_enqueue(jrdev, edesc->hw_desc, rsa_priv_f2_done, req);
	if (!ret)
		return -EINPROGRESS;

	rsa_priv_f2_unmap(jrdev, edesc, req);

init_fail:
	rsa_io_unmap(jrdev, edesc, req);
	kfree(edesc);
	return ret;
}

static int caam_rsa_dec_priv_f3(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct device *jrdev = ctx->dev;
	struct rsa_edesc *edesc;
	int ret;

	/* Allocate extended descriptor */
	edesc = rsa_edesc_alloc(req, DESC_RSA_PRIV_F3_LEN);
	if (IS_ERR(edesc))
		return PTR_ERR(edesc);

	/* Set RSA Decrypt Protocol Data Block - Private Key Form #3 */
	ret = set_rsa_priv_f3_pdb(req, edesc);
	if (ret)
		goto init_fail;

	/* Initialize Job Descriptor */
	init_rsa_priv_f3_desc(edesc->hw_desc, &edesc->pdb.priv_f3);

	ret = caam_jr_enqueue(jrdev, edesc->hw_desc, rsa_priv_f3_done, req);
	if (!ret)
		return -EINPROGRESS;

	rsa_priv_f3_unmap(jrdev, edesc, req);

init_fail:
	rsa_io_unmap(jrdev, edesc, req);
	kfree(edesc);
	return ret;
}

static int caam_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;
	int ret;

	if (unlikely(!key->n || !key->d))
		return -EINVAL;

	if (req->dst_len < key->n_sz) {
		req->dst_len = key->n_sz;
		dev_err(ctx->dev, "Output buffer length less than parameter n\n");
		return -EOVERFLOW;
	}

	if (key->priv_form == FORM3)
		ret = caam_rsa_dec_priv_f3(req);
	else if (key->priv_form == FORM2)
		ret = caam_rsa_dec_priv_f2(req);
	else
		ret = caam_rsa_dec_priv_f1(req);

	return ret;
}

static void caam_rsa_free_key(struct caam_rsa_key *key)
{
	kzfree(key->d);
	kzfree(key->p);
	kzfree(key->q);
	kzfree(key->dp);
	kzfree(key->dq);
	kzfree(key->qinv);
	kzfree(key->tmp1);
	kzfree(key->tmp2);
	kfree(key->e);
	kfree(key->n);
	memset(key, 0, sizeof(*key));
}

static void caam_rsa_drop_leading_zeros(const u8 **ptr, size_t *nbytes)
{
	while (!**ptr && *nbytes) {
		(*ptr)++;
		(*nbytes)--;
	}
}

/**
 * caam_read_rsa_crt - Used for reading dP, dQ, qInv CRT members.
 * dP, dQ and qInv could decode to less than corresponding p, q length, as the
 * BER-encoding requires that the minimum number of bytes be used to encode the
 * integer. dP, dQ, qInv decoded values have to be zero-padded to appropriate
 * length.
 *
 * @ptr   : pointer to {dP, dQ, qInv} CRT member
 * @nbytes: length in bytes of {dP, dQ, qInv} CRT member
 * @dstlen: length in bytes of corresponding p or q prime factor
 */
static u8 *caam_read_rsa_crt(const u8 *ptr, size_t nbytes, size_t dstlen)
{
	u8 *dst;

	caam_rsa_drop_leading_zeros(&ptr, &nbytes);
	if (!nbytes)
		return NULL;

	dst = kzalloc(dstlen, GFP_DMA | GFP_KERNEL);
	if (!dst)
		return NULL;

	memcpy(dst + (dstlen - nbytes), ptr, nbytes);

	return dst;
}

/**
 * caam_read_raw_data - Read a raw byte stream as a positive integer.
 * The function skips buffer's leading zeros, copies the remained data
 * to a buffer allocated in the GFP_DMA | GFP_KERNEL zone and returns
 * the address of the new buffer.
 *
 * @buf   : The data to read
 * @nbytes: The amount of data to read
 */
static inline u8 *caam_read_raw_data(const u8 *buf, size_t *nbytes)
{

	caam_rsa_drop_leading_zeros(&buf, nbytes);
	if (!*nbytes)
		return NULL;

	return kmemdup(buf, *nbytes, GFP_DMA | GFP_KERNEL);
}

static int caam_rsa_check_key_length(unsigned int len)
{
	if (len > 4096)
		return -EINVAL;
	return 0;
}

static int caam_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
				unsigned int keylen)
{
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key = {NULL};
	struct caam_rsa_key *rsa_key = &ctx->key;
	int ret;

	/* Free the old RSA key if any */
	caam_rsa_free_key(rsa_key);

	ret = rsa_parse_pub_key(&raw_key, key, keylen);
	if (ret)
		return ret;

	/* Copy key in DMA zone */
	rsa_key->e = kzalloc(raw_key.e_sz, GFP_DMA | GFP_KERNEL);
	if (!rsa_key->e)
		goto err;

	/*
	 * Skip leading zeros and copy the positive integer to a buffer
	 * allocated in the GFP_DMA | GFP_KERNEL zone. The decryption descriptor
	 * expects a positive integer for the RSA modulus and uses its length as
	 * decryption output length.
	 */
	rsa_key->n = caam_read_raw_data(raw_key.n, &raw_key.n_sz);
	if (!rsa_key->n)
		goto err;

	if (caam_rsa_check_key_length(raw_key.n_sz << 3)) {
		caam_rsa_free_key(rsa_key);
		return -EINVAL;
	}

	rsa_key->e_sz = raw_key.e_sz;
	rsa_key->n_sz = raw_key.n_sz;

	memcpy(rsa_key->e, raw_key.e, raw_key.e_sz);

	return 0;
err:
	caam_rsa_free_key(rsa_key);
	return -ENOMEM;
}

static void caam_rsa_set_priv_key_form(struct caam_rsa_ctx *ctx,
				       struct rsa_key *raw_key)
{
	struct caam_rsa_key *rsa_key = &ctx->key;
	size_t p_sz = raw_key->p_sz;
	size_t q_sz = raw_key->q_sz;

	rsa_key->p = caam_read_raw_data(raw_key->p, &p_sz);
	if (!rsa_key->p)
		return;
	rsa_key->p_sz = p_sz;

	rsa_key->q = caam_read_raw_data(raw_key->q, &q_sz);
	if (!rsa_key->q)
		goto free_p;
	rsa_key->q_sz = q_sz;

	rsa_key->tmp1 = kzalloc(raw_key->p_sz, GFP_DMA | GFP_KERNEL);
	if (!rsa_key->tmp1)
		goto free_q;

	rsa_key->tmp2 = kzalloc(raw_key->q_sz, GFP_DMA | GFP_KERNEL);
	if (!rsa_key->tmp2)
		goto free_tmp1;

	rsa_key->priv_form = FORM2;

	rsa_key->dp = caam_read_rsa_crt(raw_key->dp, raw_key->dp_sz, p_sz);
	if (!rsa_key->dp)
		goto free_tmp2;

	rsa_key->dq = caam_read_rsa_crt(raw_key->dq, raw_key->dq_sz, q_sz);
	if (!rsa_key->dq)
		goto free_dp;

	rsa_key->qinv = caam_read_rsa_crt(raw_key->qinv, raw_key->qinv_sz,
					  q_sz);
	if (!rsa_key->qinv)
		goto free_dq;

	rsa_key->priv_form = FORM3;

	return;

free_dq:
	kzfree(rsa_key->dq);
free_dp:
	kzfree(rsa_key->dp);
free_tmp2:
	kzfree(rsa_key->tmp2);
free_tmp1:
	kzfree(rsa_key->tmp1);
free_q:
	kzfree(rsa_key->q);
free_p:
	kzfree(rsa_key->p);
}

static int caam_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
				 unsigned int keylen)
{
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct rsa_key raw_key = {NULL};
	struct caam_rsa_key *rsa_key = &ctx->key;
	int ret;

	/* Free the old RSA key if any */
	caam_rsa_free_key(rsa_key);

	ret = rsa_parse_priv_key(&raw_key, key, keylen);
	if (ret)
		return ret;

	/* Copy key in DMA zone */
	rsa_key->d = kzalloc(raw_key.d_sz, GFP_DMA | GFP_KERNEL);
	if (!rsa_key->d)
		goto err;

	rsa_key->e = kzalloc(raw_key.e_sz, GFP_DMA | GFP_KERNEL);
	if (!rsa_key->e)
		goto err;

	/*
	 * Skip leading zeros and copy the positive integer to a buffer
	 * allocated in the GFP_DMA | GFP_KERNEL zone. The decryption descriptor
	 * expects a positive integer for the RSA modulus and uses its length as
	 * decryption output length.
	 */
	rsa_key->n = caam_read_raw_data(raw_key.n, &raw_key.n_sz);
	if (!rsa_key->n)
		goto err;

	if (caam_rsa_check_key_length(raw_key.n_sz << 3)) {
		caam_rsa_free_key(rsa_key);
		return -EINVAL;
	}

	rsa_key->d_sz = raw_key.d_sz;
	rsa_key->e_sz = raw_key.e_sz;
	rsa_key->n_sz = raw_key.n_sz;

	memcpy(rsa_key->d, raw_key.d, raw_key.d_sz);
	memcpy(rsa_key->e, raw_key.e, raw_key.e_sz);

	caam_rsa_set_priv_key_form(ctx, &raw_key);

	return 0;

err:
	caam_rsa_free_key(rsa_key);
	return -ENOMEM;
}

static unsigned int caam_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	return ctx->key.n_sz;
}

/* Per session pkc's driver context creation function */
static int caam_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);

	ctx->dev = caam_jr_alloc();

	if (IS_ERR(ctx->dev)) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return PTR_ERR(ctx->dev);
	}

	return 0;
}

/* Per session pkc's driver context cleanup function */
static void caam_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct caam_rsa_ctx *ctx = akcipher_tfm_ctx(tfm);
	struct caam_rsa_key *key = &ctx->key;

	caam_rsa_free_key(key);
	caam_jr_free(ctx->dev);
}

static struct akcipher_alg caam_rsa = {
	.encrypt = caam_rsa_enc,
	.decrypt = caam_rsa_dec,
	.sign = caam_rsa_dec,
	.verify = caam_rsa_enc,
	.set_pub_key = caam_rsa_set_pub_key,
	.set_priv_key = caam_rsa_set_priv_key,
	.max_size = caam_rsa_max_size,
	.init = caam_rsa_init_tfm,
	.exit = caam_rsa_exit_tfm,
	.reqsize = sizeof(struct caam_rsa_req_ctx),
	.base = {
		.cra_name = "rsa",
		.cra_driver_name = "rsa-caam",
		.cra_priority = 3000,
		.cra_module = THIS_MODULE,
		.cra_ctxsize = sizeof(struct caam_rsa_ctx),
	},
};

/* Public Key Cryptography module initialization handler */
static int __init caam_pkc_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;
	struct device *ctrldev;
	struct caam_drv_private *priv;
	u32 cha_inst, pk_inst;
	int err;

	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev) {
		of_node_put(dev_node);
		return -ENODEV;
	}

	ctrldev = &pdev->dev;
	priv = dev_get_drvdata(ctrldev);
	of_node_put(dev_node);

	/*
	 * If priv is NULL, it's probably because the caam driver wasn't
	 * properly initialized (e.g. RNG4 init failed). Thus, bail out here.
	 */
	if (!priv)
		return -ENODEV;

	/* Determine public key hardware accelerator presence. */
	cha_inst = rd_reg32(&priv->ctrl->perfmon.cha_num_ls);
	pk_inst = (cha_inst & CHA_ID_LS_PK_MASK) >> CHA_ID_LS_PK_SHIFT;

	/* Do not register algorithms if PKHA is not present. */
	if (!pk_inst)
		return -ENODEV;

	err = crypto_register_akcipher(&caam_rsa);
	if (err)
		dev_warn(ctrldev, "%s alg registration failed\n",
			 caam_rsa.base.cra_driver_name);
	else
		dev_info(ctrldev, "caam pkc algorithms registered in /proc/crypto\n");

	return err;
}

static void __exit caam_pkc_exit(void)
{
	crypto_unregister_akcipher(&caam_rsa);
}

module_init(caam_pkc_init);
module_exit(caam_pkc_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL CAAM support for PKC functions of crypto API");
MODULE_AUTHOR("Freescale Semiconductor");
