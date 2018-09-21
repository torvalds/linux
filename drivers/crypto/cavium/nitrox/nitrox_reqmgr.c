// SPDX-License-Identifier: GPL-2.0
#include <linux/gfp.h>
#include <linux/workqueue.h>
#include <crypto/internal/skcipher.h>

#include "nitrox_dev.h"
#include "nitrox_req.h"
#include "nitrox_csr.h"

/* SLC_STORE_INFO */
#define MIN_UDD_LEN 16
/* PKT_IN_HDR + SLC_STORE_INFO */
#define FDATA_SIZE 32
/* Base destination port for the solicited requests */
#define SOLICIT_BASE_DPORT 256
#define PENDING_SIG	0xFFFFFFFFFFFFFFFFUL

#define REQ_NOT_POSTED 1
#define REQ_BACKLOG    2
#define REQ_POSTED     3

/**
 * Response codes from SE microcode
 * 0x00 - Success
 *   Completion with no error
 * 0x43 - ERR_GC_DATA_LEN_INVALID
 *   Invalid Data length if Encryption Data length is
 *   less than 16 bytes for AES-XTS and AES-CTS.
 * 0x45 - ERR_GC_CTX_LEN_INVALID
 *   Invalid context length: CTXL != 23 words.
 * 0x4F - ERR_GC_DOCSIS_CIPHER_INVALID
 *   DOCSIS support is enabled with other than
 *   AES/DES-CBC mode encryption.
 * 0x50 - ERR_GC_DOCSIS_OFFSET_INVALID
 *   Authentication offset is other than 0 with
 *   Encryption IV source = 0.
 *   Authentication offset is other than 8 (DES)/16 (AES)
 *   with Encryption IV source = 1
 * 0x51 - ERR_GC_CRC32_INVALID_SELECTION
 *   CRC32 is enabled for other than DOCSIS encryption.
 * 0x52 - ERR_GC_AES_CCM_FLAG_INVALID
 *   Invalid flag options in AES-CCM IV.
 */

static inline int incr_index(int index, int count, int max)
{
	if ((index + count) >= max)
		index = index + count - max;
	else
		index += count;

	return index;
}

/**
 * dma_free_sglist - unmap and free the sg lists.
 * @ndev: N5 device
 * @sgtbl: SG table
 */
static void softreq_unmap_sgbufs(struct nitrox_softreq *sr)
{
	struct nitrox_device *ndev = sr->ndev;
	struct device *dev = DEV(ndev);
	struct nitrox_sglist *sglist;

	/* unmap in sgbuf */
	sglist = sr->in.sglist;
	if (!sglist)
		goto out_unmap;

	/* unmap iv */
	dma_unmap_single(dev, sglist->dma, sglist->len, DMA_BIDIRECTIONAL);
	/* unmpa src sglist */
	dma_unmap_sg(dev, sr->in.buf, (sr->in.map_bufs_cnt - 1), sr->in.dir);
	/* unamp gather component */
	dma_unmap_single(dev, sr->in.dma, sr->in.len, DMA_TO_DEVICE);
	kfree(sr->in.sglist);
	kfree(sr->in.sgcomp);
	sr->in.sglist = NULL;
	sr->in.buf = NULL;
	sr->in.map_bufs_cnt = 0;

out_unmap:
	/* unmap out sgbuf */
	sglist = sr->out.sglist;
	if (!sglist)
		return;

	/* unmap orh */
	dma_unmap_single(dev, sr->resp.orh_dma, ORH_HLEN, sr->out.dir);

	/* unmap dst sglist */
	if (!sr->inplace) {
		dma_unmap_sg(dev, sr->out.buf, (sr->out.map_bufs_cnt - 3),
			     sr->out.dir);
	}
	/* unmap completion */
	dma_unmap_single(dev, sr->resp.completion_dma, COMP_HLEN, sr->out.dir);

	/* unmap scatter component */
	dma_unmap_single(dev, sr->out.dma, sr->out.len, DMA_TO_DEVICE);
	kfree(sr->out.sglist);
	kfree(sr->out.sgcomp);
	sr->out.sglist = NULL;
	sr->out.buf = NULL;
	sr->out.map_bufs_cnt = 0;
}

static void softreq_destroy(struct nitrox_softreq *sr)
{
	softreq_unmap_sgbufs(sr);
	kfree(sr);
}

/**
 * create_sg_component - create SG componets for N5 device.
 * @sr: Request structure
 * @sgtbl: SG table
 * @nr_comp: total number of components required
 *
 * Component structure
 *
 *   63     48 47     32 31    16 15      0
 *   --------------------------------------
 *   |   LEN0  |  LEN1  |  LEN2  |  LEN3  |
 *   |-------------------------------------
 *   |               PTR0                 |
 *   --------------------------------------
 *   |               PTR1                 |
 *   --------------------------------------
 *   |               PTR2                 |
 *   --------------------------------------
 *   |               PTR3                 |
 *   --------------------------------------
 *
 *   Returns 0 if success or a negative errno code on error.
 */
static int create_sg_component(struct nitrox_softreq *sr,
			       struct nitrox_sgtable *sgtbl, int map_nents)
{
	struct nitrox_device *ndev = sr->ndev;
	struct nitrox_sgcomp *sgcomp;
	struct nitrox_sglist *sglist;
	dma_addr_t dma;
	size_t sz_comp;
	int i, j, nr_sgcomp;

	nr_sgcomp = roundup(map_nents, 4) / 4;

	/* each component holds 4 dma pointers */
	sz_comp = nr_sgcomp * sizeof(*sgcomp);
	sgcomp = kzalloc(sz_comp, sr->gfp);
	if (!sgcomp)
		return -ENOMEM;

	sgtbl->sgcomp = sgcomp;
	sgtbl->nr_sgcomp = nr_sgcomp;

	sglist = sgtbl->sglist;
	/* populate device sg component */
	for (i = 0; i < nr_sgcomp; i++) {
		for (j = 0; j < 4; j++) {
			sgcomp->len[j] = cpu_to_be16(sglist->len);
			sgcomp->dma[j] = cpu_to_be64(sglist->dma);
			sglist++;
		}
		sgcomp++;
	}
	/* map the device sg component */
	dma = dma_map_single(DEV(ndev), sgtbl->sgcomp, sz_comp, DMA_TO_DEVICE);
	if (dma_mapping_error(DEV(ndev), dma)) {
		kfree(sgtbl->sgcomp);
		sgtbl->sgcomp = NULL;
		return -ENOMEM;
	}

	sgtbl->dma = dma;
	sgtbl->len = sz_comp;

	return 0;
}

/**
 * dma_map_inbufs - DMA map input sglist and creates sglist component
 *                  for N5 device.
 * @sr: Request structure
 * @req: Crypto request structre
 *
 * Returns 0 if successful or a negative errno code on error.
 */
static int dma_map_inbufs(struct nitrox_softreq *sr,
			  struct se_crypto_request *req)
{
	struct device *dev = DEV(sr->ndev);
	struct scatterlist *sg = req->src;
	struct nitrox_sglist *glist;
	int i, nents, ret = 0;
	dma_addr_t dma;
	size_t sz;

	nents = sg_nents(req->src);

	/* creater gather list IV and src entries */
	sz = roundup((1 + nents), 4) * sizeof(*glist);
	glist = kzalloc(sz, sr->gfp);
	if (!glist)
		return -ENOMEM;

	sr->in.sglist = glist;
	/* map IV */
	dma = dma_map_single(dev, &req->iv, req->ivsize, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, dma)) {
		ret = -EINVAL;
		goto iv_map_err;
	}

	sr->in.dir = (req->src == req->dst) ? DMA_BIDIRECTIONAL : DMA_TO_DEVICE;
	/* map src entries */
	nents = dma_map_sg(dev, req->src, nents, sr->in.dir);
	if (!nents) {
		ret = -EINVAL;
		goto src_map_err;
	}
	sr->in.buf = req->src;

	/* store the mappings */
	glist->len = req->ivsize;
	glist->dma = dma;
	glist++;
	sr->in.total_bytes += req->ivsize;

	for_each_sg(req->src, sg, nents, i) {
		glist->len = sg_dma_len(sg);
		glist->dma = sg_dma_address(sg);
		sr->in.total_bytes += glist->len;
		glist++;
	}
	/* roundup map count to align with entires in sg component */
	sr->in.map_bufs_cnt = (1 + nents);

	/* create NITROX gather component */
	ret = create_sg_component(sr, &sr->in, sr->in.map_bufs_cnt);
	if (ret)
		goto incomp_err;

	return 0;

incomp_err:
	dma_unmap_sg(dev, req->src, nents, sr->in.dir);
	sr->in.map_bufs_cnt = 0;
src_map_err:
	dma_unmap_single(dev, dma, req->ivsize, DMA_BIDIRECTIONAL);
iv_map_err:
	kfree(sr->in.sglist);
	sr->in.sglist = NULL;
	return ret;
}

static int dma_map_outbufs(struct nitrox_softreq *sr,
			   struct se_crypto_request *req)
{
	struct device *dev = DEV(sr->ndev);
	struct nitrox_sglist *glist = sr->in.sglist;
	struct nitrox_sglist *slist;
	struct scatterlist *sg;
	int i, nents, map_bufs_cnt, ret = 0;
	size_t sz;

	nents = sg_nents(req->dst);

	/* create scatter list ORH, IV, dst entries and Completion header */
	sz = roundup((3 + nents), 4) * sizeof(*slist);
	slist = kzalloc(sz, sr->gfp);
	if (!slist)
		return -ENOMEM;

	sr->out.sglist = slist;
	sr->out.dir = DMA_BIDIRECTIONAL;
	/* map ORH */
	sr->resp.orh_dma = dma_map_single(dev, &sr->resp.orh, ORH_HLEN,
					  sr->out.dir);
	if (dma_mapping_error(dev, sr->resp.orh_dma)) {
		ret = -EINVAL;
		goto orh_map_err;
	}

	/* map completion */
	sr->resp.completion_dma = dma_map_single(dev, &sr->resp.completion,
						 COMP_HLEN, sr->out.dir);
	if (dma_mapping_error(dev, sr->resp.completion_dma)) {
		ret = -EINVAL;
		goto compl_map_err;
	}

	sr->inplace = (req->src == req->dst) ? true : false;
	/* out place */
	if (!sr->inplace) {
		nents = dma_map_sg(dev, req->dst, nents, sr->out.dir);
		if (!nents) {
			ret = -EINVAL;
			goto dst_map_err;
		}
	}
	sr->out.buf = req->dst;

	/* store the mappings */
	/* orh */
	slist->len = ORH_HLEN;
	slist->dma = sr->resp.orh_dma;
	slist++;

	/* copy the glist mappings */
	if (sr->inplace) {
		nents = sr->in.map_bufs_cnt - 1;
		map_bufs_cnt = sr->in.map_bufs_cnt;
		while (map_bufs_cnt--) {
			slist->len = glist->len;
			slist->dma = glist->dma;
			slist++;
			glist++;
		}
	} else {
		/* copy iv mapping */
		slist->len = glist->len;
		slist->dma = glist->dma;
		slist++;
		/* copy remaining maps */
		for_each_sg(req->dst, sg, nents, i) {
			slist->len = sg_dma_len(sg);
			slist->dma = sg_dma_address(sg);
			slist++;
		}
	}

	/* completion */
	slist->len = COMP_HLEN;
	slist->dma = sr->resp.completion_dma;

	sr->out.map_bufs_cnt = (3 + nents);

	ret = create_sg_component(sr, &sr->out, sr->out.map_bufs_cnt);
	if (ret)
		goto outcomp_map_err;

	return 0;

outcomp_map_err:
	if (!sr->inplace)
		dma_unmap_sg(dev, req->dst, nents, sr->out.dir);
	sr->out.map_bufs_cnt = 0;
	sr->out.buf = NULL;
dst_map_err:
	dma_unmap_single(dev, sr->resp.completion_dma, COMP_HLEN, sr->out.dir);
	sr->resp.completion_dma = 0;
compl_map_err:
	dma_unmap_single(dev, sr->resp.orh_dma, ORH_HLEN, sr->out.dir);
	sr->resp.orh_dma = 0;
orh_map_err:
	kfree(sr->out.sglist);
	sr->out.sglist = NULL;
	return ret;
}

static inline int softreq_map_iobuf(struct nitrox_softreq *sr,
				    struct se_crypto_request *creq)
{
	int ret;

	ret = dma_map_inbufs(sr, creq);
	if (ret)
		return ret;

	ret = dma_map_outbufs(sr, creq);
	if (ret)
		softreq_unmap_sgbufs(sr);

	return ret;
}

static inline void backlog_list_add(struct nitrox_softreq *sr,
				    struct nitrox_cmdq *cmdq)
{
	INIT_LIST_HEAD(&sr->backlog);

	spin_lock_bh(&cmdq->backlog_lock);
	list_add_tail(&sr->backlog, &cmdq->backlog_head);
	atomic_inc(&cmdq->backlog_count);
	atomic_set(&sr->status, REQ_BACKLOG);
	spin_unlock_bh(&cmdq->backlog_lock);
}

static inline void response_list_add(struct nitrox_softreq *sr,
				     struct nitrox_cmdq *cmdq)
{
	INIT_LIST_HEAD(&sr->response);

	spin_lock_bh(&cmdq->response_lock);
	list_add_tail(&sr->response, &cmdq->response_head);
	spin_unlock_bh(&cmdq->response_lock);
}

static inline void response_list_del(struct nitrox_softreq *sr,
				     struct nitrox_cmdq *cmdq)
{
	spin_lock_bh(&cmdq->response_lock);
	list_del(&sr->response);
	spin_unlock_bh(&cmdq->response_lock);
}

static struct nitrox_softreq *
get_first_response_entry(struct nitrox_cmdq *cmdq)
{
	return list_first_entry_or_null(&cmdq->response_head,
					struct nitrox_softreq, response);
}

static inline bool cmdq_full(struct nitrox_cmdq *cmdq, int qlen)
{
	if (atomic_inc_return(&cmdq->pending_count) > qlen) {
		atomic_dec(&cmdq->pending_count);
		/* sync with other cpus */
		smp_mb__after_atomic();
		return true;
	}
	return false;
}

/**
 * post_se_instr - Post SE instruction to Packet Input ring
 * @sr: Request structure
 *
 * Returns 0 if successful or a negative error code,
 * if no space in ring.
 */
static void post_se_instr(struct nitrox_softreq *sr,
			  struct nitrox_cmdq *cmdq)
{
	struct nitrox_device *ndev = sr->ndev;
	int idx;
	u8 *ent;

	spin_lock_bh(&cmdq->cmdq_lock);

	idx = cmdq->write_idx;
	/* copy the instruction */
	ent = cmdq->head + (idx * cmdq->instr_size);
	memcpy(ent, &sr->instr, cmdq->instr_size);

	atomic_set(&sr->status, REQ_POSTED);
	response_list_add(sr, cmdq);
	sr->tstamp = jiffies;
	/* flush the command queue updates */
	dma_wmb();

	/* Ring doorbell with count 1 */
	writeq(1, cmdq->dbell_csr_addr);
	/* orders the doorbell rings */
	mmiowb();

	cmdq->write_idx = incr_index(idx, 1, ndev->qlen);

	spin_unlock_bh(&cmdq->cmdq_lock);

	/* increment the posted command count */
	atomic64_inc(&ndev->stats.posted);
}

static int post_backlog_cmds(struct nitrox_cmdq *cmdq)
{
	struct nitrox_device *ndev = cmdq->ndev;
	struct nitrox_softreq *sr, *tmp;
	int ret = 0;

	if (!atomic_read(&cmdq->backlog_count))
		return 0;

	spin_lock_bh(&cmdq->backlog_lock);

	list_for_each_entry_safe(sr, tmp, &cmdq->backlog_head, backlog) {
		struct skcipher_request *skreq;

		/* submit until space available */
		if (unlikely(cmdq_full(cmdq, ndev->qlen))) {
			ret = -ENOSPC;
			break;
		}
		/* delete from backlog list */
		list_del(&sr->backlog);
		atomic_dec(&cmdq->backlog_count);
		/* sync with other cpus */
		smp_mb__after_atomic();

		skreq = sr->skreq;
		/* post the command */
		post_se_instr(sr, cmdq);

		/* backlog requests are posted, wakeup with -EINPROGRESS */
		skcipher_request_complete(skreq, -EINPROGRESS);
	}
	spin_unlock_bh(&cmdq->backlog_lock);

	return ret;
}

static int nitrox_enqueue_request(struct nitrox_softreq *sr)
{
	struct nitrox_cmdq *cmdq = sr->cmdq;
	struct nitrox_device *ndev = sr->ndev;

	/* try to post backlog requests */
	post_backlog_cmds(cmdq);

	if (unlikely(cmdq_full(cmdq, ndev->qlen))) {
		if (!(sr->flags & CRYPTO_TFM_REQ_MAY_BACKLOG)) {
			/* increment drop count */
			atomic64_inc(&ndev->stats.dropped);
			return -ENOSPC;
		}
		/* add to backlog list */
		backlog_list_add(sr, cmdq);
		return -EBUSY;
	}
	post_se_instr(sr, cmdq);

	return -EINPROGRESS;
}

/**
 * nitrox_se_request - Send request to SE core
 * @ndev: NITROX device
 * @req: Crypto request
 *
 * Returns 0 on success, or a negative error code.
 */
int nitrox_process_se_request(struct nitrox_device *ndev,
			      struct se_crypto_request *req,
			      completion_t callback,
			      struct skcipher_request *skreq)
{
	struct nitrox_softreq *sr;
	dma_addr_t ctx_handle = 0;
	int qno, ret = 0;

	if (!nitrox_ready(ndev))
		return -ENODEV;

	sr = kzalloc(sizeof(*sr), req->gfp);
	if (!sr)
		return -ENOMEM;

	sr->ndev = ndev;
	sr->flags = req->flags;
	sr->gfp = req->gfp;
	sr->callback = callback;
	sr->skreq = skreq;

	atomic_set(&sr->status, REQ_NOT_POSTED);

	WRITE_ONCE(sr->resp.orh, PENDING_SIG);
	WRITE_ONCE(sr->resp.completion, PENDING_SIG);

	ret = softreq_map_iobuf(sr, req);
	if (ret) {
		kfree(sr);
		return ret;
	}

	/* get the context handle */
	if (req->ctx_handle) {
		struct ctx_hdr *hdr;
		u8 *ctx_ptr;

		ctx_ptr = (u8 *)(uintptr_t)req->ctx_handle;
		hdr = (struct ctx_hdr *)(ctx_ptr - sizeof(struct ctx_hdr));
		ctx_handle = hdr->ctx_dma;
	}

	/* select the queue */
	qno = smp_processor_id() % ndev->nr_queues;

	sr->cmdq = &ndev->pkt_cmdqs[qno];

	/*
	 * 64-Byte Instruction Format
	 *
	 *  ----------------------
	 *  |      DPTR0         | 8 bytes
	 *  ----------------------
	 *  |  PKT_IN_INSTR_HDR  | 8 bytes
	 *  ----------------------
	 *  |    PKT_IN_HDR      | 16 bytes
	 *  ----------------------
	 *  |    SLC_INFO        | 16 bytes
	 *  ----------------------
	 *  |   Front data       | 16 bytes
	 *  ----------------------
	 */

	/* fill the packet instruction */
	/* word 0 */
	sr->instr.dptr0 = cpu_to_be64(sr->in.dma);

	/* word 1 */
	sr->instr.ih.value = 0;
	sr->instr.ih.s.g = 1;
	sr->instr.ih.s.gsz = sr->in.map_bufs_cnt;
	sr->instr.ih.s.ssz = sr->out.map_bufs_cnt;
	sr->instr.ih.s.fsz = FDATA_SIZE + sizeof(struct gphdr);
	sr->instr.ih.s.tlen = sr->instr.ih.s.fsz + sr->in.total_bytes;
	sr->instr.ih.value = cpu_to_be64(sr->instr.ih.value);

	/* word 2 */
	sr->instr.irh.value[0] = 0;
	sr->instr.irh.s.uddl = MIN_UDD_LEN;
	/* context length in 64-bit words */
	sr->instr.irh.s.ctxl = (req->ctrl.s.ctxl / 8);
	/* offset from solicit base port 256 */
	sr->instr.irh.s.destport = SOLICIT_BASE_DPORT + qno;
	sr->instr.irh.s.ctxc = req->ctrl.s.ctxc;
	sr->instr.irh.s.arg = req->ctrl.s.arg;
	sr->instr.irh.s.opcode = req->opcode;
	sr->instr.irh.value[0] = cpu_to_be64(sr->instr.irh.value[0]);

	/* word 3 */
	sr->instr.irh.s.ctxp = cpu_to_be64(ctx_handle);

	/* word 4 */
	sr->instr.slc.value[0] = 0;
	sr->instr.slc.s.ssz = sr->out.map_bufs_cnt;
	sr->instr.slc.value[0] = cpu_to_be64(sr->instr.slc.value[0]);

	/* word 5 */
	sr->instr.slc.s.rptr = cpu_to_be64(sr->out.dma);

	/*
	 * No conversion for front data,
	 * It goes into payload
	 * put GP Header in front data
	 */
	sr->instr.fdata[0] = *((u64 *)&req->gph);
	sr->instr.fdata[1] = 0;

	ret = nitrox_enqueue_request(sr);
	if (ret == -ENOSPC)
		goto send_fail;

	return ret;

send_fail:
	softreq_destroy(sr);
	return ret;
}

static inline int cmd_timeout(unsigned long tstamp, unsigned long timeout)
{
	return time_after_eq(jiffies, (tstamp + timeout));
}

void backlog_qflush_work(struct work_struct *work)
{
	struct nitrox_cmdq *cmdq;

	cmdq = container_of(work, struct nitrox_cmdq, backlog_qflush);
	post_backlog_cmds(cmdq);
}

/**
 * process_request_list - process completed requests
 * @ndev: N5 device
 * @qno: queue to operate
 *
 * Returns the number of responses processed.
 */
static void process_response_list(struct nitrox_cmdq *cmdq)
{
	struct nitrox_device *ndev = cmdq->ndev;
	struct nitrox_softreq *sr;
	struct skcipher_request *skreq;
	completion_t callback;
	int req_completed = 0, err = 0, budget;

	/* check all pending requests */
	budget = atomic_read(&cmdq->pending_count);

	while (req_completed < budget) {
		sr = get_first_response_entry(cmdq);
		if (!sr)
			break;

		if (atomic_read(&sr->status) != REQ_POSTED)
			break;

		/* check orh and completion bytes updates */
		if (READ_ONCE(sr->resp.orh) == READ_ONCE(sr->resp.completion)) {
			/* request not completed, check for timeout */
			if (!cmd_timeout(sr->tstamp, ndev->timeout))
				break;
			dev_err_ratelimited(DEV(ndev),
					    "Request timeout, orh 0x%016llx\n",
					    READ_ONCE(sr->resp.orh));
		}
		atomic_dec(&cmdq->pending_count);
		atomic64_inc(&ndev->stats.completed);
		/* sync with other cpus */
		smp_mb__after_atomic();
		/* remove from response list */
		response_list_del(sr, cmdq);

		callback = sr->callback;
		skreq = sr->skreq;

		/* ORH error code */
		err = READ_ONCE(sr->resp.orh) & 0xff;
		softreq_destroy(sr);

		if (callback)
			callback(skreq, err);

		req_completed++;
	}
}

/**
 * pkt_slc_resp_handler - post processing of SE responses
 */
void pkt_slc_resp_handler(unsigned long data)
{
	struct bh_data *bh = (void *)(uintptr_t)(data);
	struct nitrox_cmdq *cmdq = bh->cmdq;
	union nps_pkt_slc_cnts pkt_slc_cnts;

	/* read completion count */
	pkt_slc_cnts.value = readq(bh->completion_cnt_csr_addr);
	/* resend the interrupt if more work to do */
	pkt_slc_cnts.s.resend = 1;

	process_response_list(cmdq);

	/*
	 * clear the interrupt with resend bit enabled,
	 * MSI-X interrupt generates if Completion count > Threshold
	 */
	writeq(pkt_slc_cnts.value, bh->completion_cnt_csr_addr);
	/* order the writes */
	mmiowb();

	if (atomic_read(&cmdq->backlog_count))
		schedule_work(&cmdq->backlog_qflush);
}
