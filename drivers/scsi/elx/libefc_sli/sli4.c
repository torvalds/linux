// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/**
 * All common (i.e. transport-independent) SLI-4 functions are implemented
 * in this file.
 */
#include "sli4.h"

static struct sli4_asic_entry_t sli4_asic_table[] = {
	{ SLI4_ASIC_REV_B0, SLI4_ASIC_GEN_5},
	{ SLI4_ASIC_REV_D0, SLI4_ASIC_GEN_5},
	{ SLI4_ASIC_REV_A3, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A0, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A1, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A3, SLI4_ASIC_GEN_6},
	{ SLI4_ASIC_REV_A1, SLI4_ASIC_GEN_7},
	{ SLI4_ASIC_REV_A0, SLI4_ASIC_GEN_7},
};

/* Convert queue type enum (SLI_QTYPE_*) into a string */
static char *SLI4_QNAME[] = {
	"Event Queue",
	"Completion Queue",
	"Mailbox Queue",
	"Work Queue",
	"Receive Queue",
	"Undefined"
};

/**
 * sli_config_cmd_init() - Write a SLI_CONFIG command to the provided buffer.
 *
 * @sli4: SLI context pointer.
 * @buf: Destination buffer for the command.
 * @length: Length in bytes of attached command.
 * @dma: DMA buffer for non-embedded commands.
 * Return: Command payload buffer.
 */
static void *
sli_config_cmd_init(struct sli4 *sli4, void *buf, u32 length,
		    struct efc_dma *dma)
{
	struct sli4_cmd_sli_config *config;
	u32 flags;

	if (length > sizeof(config->payload.embed) && !dma) {
		efc_log_err(sli4, "Too big for an embedded cmd with len(%d)\n",
			    length);
		return NULL;
	}

	memset(buf, 0, SLI4_BMBX_SIZE);

	config = buf;

	config->hdr.command = SLI4_MBX_CMD_SLI_CONFIG;
	if (!dma) {
		flags = SLI4_SLICONF_EMB;
		config->dw1_flags = cpu_to_le32(flags);
		config->payload_len = cpu_to_le32(length);
		return config->payload.embed;
	}

	flags = SLI4_SLICONF_PMDCMD_VAL_1;
	flags &= ~SLI4_SLICONF_EMB;
	config->dw1_flags = cpu_to_le32(flags);

	config->payload.mem.addr.low = cpu_to_le32(lower_32_bits(dma->phys));
	config->payload.mem.addr.high =	cpu_to_le32(upper_32_bits(dma->phys));
	config->payload.mem.length =
				cpu_to_le32(dma->size & SLI4_SLICONF_PMD_LEN);
	config->payload_len = cpu_to_le32(dma->size);
	/* save pointer to DMA for BMBX dumping purposes */
	sli4->bmbx_non_emb_pmd = dma;
	return dma->virt;
}

/**
 * sli_cmd_common_create_cq() - Write a COMMON_CREATE_CQ V2 command.
 *
 * @sli4: SLI context pointer.
 * @buf: Destination buffer for the command.
 * @qmem: DMA memory for queue.
 * @eq_id: EQ id assosiated with this cq.
 * Return: status -EIO/0.
 */
static int
sli_cmd_common_create_cq(struct sli4 *sli4, void *buf, struct efc_dma *qmem,
			 u16 eq_id)
{
	struct sli4_rqst_cmn_create_cq_v2 *cqv2 = NULL;
	u32 p;
	uintptr_t addr;
	u32 num_pages = 0;
	size_t cmd_size = 0;
	u32 page_size = 0;
	u32 n_cqe = 0;
	u32 dw5_flags = 0;
	u16 dw6w1_arm = 0;
	__le32 len;

	/* First calculate number of pages and the mailbox cmd length */
	n_cqe = qmem->size / SLI4_CQE_BYTES;
	switch (n_cqe) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		page_size = SZ_4K;
		break;
	case 4096:
		page_size = SZ_8K;
		break;
	default:
		return -EIO;
	}
	num_pages = sli_page_count(qmem->size, page_size);

	cmd_size = SLI4_RQST_CMDSZ(cmn_create_cq_v2)
		   + SZ_DMAADDR * num_pages;

	cqv2 = sli_config_cmd_init(sli4, buf, cmd_size, NULL);
	if (!cqv2)
		return -EIO;

	len = SLI4_RQST_PYLD_LEN_VAR(cmn_create_cq_v2, SZ_DMAADDR * num_pages);
	sli_cmd_fill_hdr(&cqv2->hdr, SLI4_CMN_CREATE_CQ, SLI4_SUBSYSTEM_COMMON,
			 CMD_V2, len);
	cqv2->page_size = page_size / SLI_PAGE_SIZE;

	/* valid values for number of pages: 1, 2, 4, 8 (sec 4.4.3) */
	cqv2->num_pages = cpu_to_le16(num_pages);
	if (!num_pages || num_pages > SLI4_CREATE_CQV2_MAX_PAGES)
		return -EIO;

	switch (num_pages) {
	case 1:
		dw5_flags |= SLI4_CQ_CNT_VAL(256);
		break;
	case 2:
		dw5_flags |= SLI4_CQ_CNT_VAL(512);
		break;
	case 4:
		dw5_flags |= SLI4_CQ_CNT_VAL(1024);
		break;
	case 8:
		dw5_flags |= SLI4_CQ_CNT_VAL(LARGE);
		cqv2->cqe_count = cpu_to_le16(n_cqe);
		break;
	default:
		efc_log_err(sli4, "num_pages %d not valid\n", num_pages);
		return -EIO;
	}

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		dw5_flags |= SLI4_CREATE_CQV2_AUTOVALID;

	dw5_flags |= SLI4_CREATE_CQV2_EVT;
	dw5_flags |= SLI4_CREATE_CQV2_VALID;

	cqv2->dw5_flags = cpu_to_le32(dw5_flags);
	cqv2->dw6w1_arm = cpu_to_le16(dw6w1_arm);
	cqv2->eq_id = cpu_to_le16(eq_id);

	for (p = 0, addr = qmem->phys; p < num_pages; p++, addr += page_size) {
		cqv2->page_phys_addr[p].low = cpu_to_le32(lower_32_bits(addr));
		cqv2->page_phys_addr[p].high = cpu_to_le32(upper_32_bits(addr));
	}

	return 0;
}

static int
sli_cmd_common_create_eq(struct sli4 *sli4, void *buf, struct efc_dma *qmem)
{
	struct sli4_rqst_cmn_create_eq *eq;
	u32 p;
	uintptr_t addr;
	u16 num_pages;
	u32 dw5_flags = 0;
	u32 dw6_flags = 0, ver;

	eq = sli_config_cmd_init(sli4, buf, SLI4_CFG_PYLD_LENGTH(cmn_create_eq),
				 NULL);
	if (!eq)
		return -EIO;

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		ver = CMD_V2;
	else
		ver = CMD_V0;

	sli_cmd_fill_hdr(&eq->hdr, SLI4_CMN_CREATE_EQ, SLI4_SUBSYSTEM_COMMON,
			 ver, SLI4_RQST_PYLD_LEN(cmn_create_eq));

	/* valid values for number of pages: 1, 2, 4 (sec 4.4.3) */
	num_pages = qmem->size / SLI_PAGE_SIZE;
	eq->num_pages = cpu_to_le16(num_pages);

	switch (num_pages) {
	case 1:
		dw5_flags |= SLI4_EQE_SIZE_4;
		dw6_flags |= SLI4_EQ_CNT_VAL(1024);
		break;
	case 2:
		dw5_flags |= SLI4_EQE_SIZE_4;
		dw6_flags |= SLI4_EQ_CNT_VAL(2048);
		break;
	case 4:
		dw5_flags |= SLI4_EQE_SIZE_4;
		dw6_flags |= SLI4_EQ_CNT_VAL(4096);
		break;
	default:
		efc_log_err(sli4, "num_pages %d not valid\n", num_pages);
		return -EIO;
	}

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		dw5_flags |= SLI4_CREATE_EQ_AUTOVALID;

	dw5_flags |= SLI4_CREATE_EQ_VALID;
	dw6_flags &= (~SLI4_CREATE_EQ_ARM);
	eq->dw5_flags = cpu_to_le32(dw5_flags);
	eq->dw6_flags = cpu_to_le32(dw6_flags);
	eq->dw7_delaymulti = cpu_to_le32(SLI4_CREATE_EQ_DELAYMULTI);

	for (p = 0, addr = qmem->phys; p < num_pages;
	     p++, addr += SLI_PAGE_SIZE) {
		eq->page_address[p].low = cpu_to_le32(lower_32_bits(addr));
		eq->page_address[p].high = cpu_to_le32(upper_32_bits(addr));
	}

	return 0;
}

static int
sli_cmd_common_create_mq_ext(struct sli4 *sli4, void *buf, struct efc_dma *qmem,
			     u16 cq_id)
{
	struct sli4_rqst_cmn_create_mq_ext *mq;
	u32 p;
	uintptr_t addr;
	u32 num_pages;
	u16 dw6w1_flags = 0;

	mq = sli_config_cmd_init(sli4, buf,
				 SLI4_CFG_PYLD_LENGTH(cmn_create_mq_ext), NULL);
	if (!mq)
		return -EIO;

	sli_cmd_fill_hdr(&mq->hdr, SLI4_CMN_CREATE_MQ_EXT,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_create_mq_ext));

	/* valid values for number of pages: 1, 2, 4, 8 (sec 4.4.12) */
	num_pages = qmem->size / SLI_PAGE_SIZE;
	mq->num_pages = cpu_to_le16(num_pages);
	switch (num_pages) {
	case 1:
		dw6w1_flags |= SLI4_MQE_SIZE_16;
		break;
	case 2:
		dw6w1_flags |= SLI4_MQE_SIZE_32;
		break;
	case 4:
		dw6w1_flags |= SLI4_MQE_SIZE_64;
		break;
	case 8:
		dw6w1_flags |= SLI4_MQE_SIZE_128;
		break;
	default:
		efc_log_info(sli4, "num_pages %d not valid\n", num_pages);
		return -EIO;
	}

	mq->async_event_bitmap = cpu_to_le32(SLI4_ASYNC_EVT_FC_ALL);

	if (sli4->params.mq_create_version) {
		mq->cq_id_v1 = cpu_to_le16(cq_id);
		mq->hdr.dw3_version = cpu_to_le32(CMD_V1);
	} else {
		dw6w1_flags |= (cq_id << SLI4_CREATE_MQEXT_CQID_SHIFT);
	}
	mq->dw7_val = cpu_to_le32(SLI4_CREATE_MQEXT_VAL);

	mq->dw6w1_flags = cpu_to_le16(dw6w1_flags);
	for (p = 0, addr = qmem->phys; p < num_pages;
	     p++, addr += SLI_PAGE_SIZE) {
		mq->page_phys_addr[p].low = cpu_to_le32(lower_32_bits(addr));
		mq->page_phys_addr[p].high = cpu_to_le32(upper_32_bits(addr));
	}

	return 0;
}

int
sli_cmd_wq_create(struct sli4 *sli4, void *buf, struct efc_dma *qmem, u16 cq_id)
{
	struct sli4_rqst_wq_create *wq;
	u32 p;
	uintptr_t addr;
	u32 page_size = 0;
	u32 n_wqe = 0;
	u16 num_pages;

	wq = sli_config_cmd_init(sli4, buf, SLI4_CFG_PYLD_LENGTH(wq_create),
				 NULL);
	if (!wq)
		return -EIO;

	sli_cmd_fill_hdr(&wq->hdr, SLI4_OPC_WQ_CREATE, SLI4_SUBSYSTEM_FC,
			 CMD_V1, SLI4_RQST_PYLD_LEN(wq_create));
	n_wqe = qmem->size / sli4->wqe_size;

	switch (qmem->size) {
	case 4096:
	case 8192:
	case 16384:
	case 32768:
		page_size = SZ_4K;
		break;
	case 65536:
		page_size = SZ_8K;
		break;
	case 131072:
		page_size = SZ_16K;
		break;
	case 262144:
		page_size = SZ_32K;
		break;
	case 524288:
		page_size = SZ_64K;
		break;
	default:
		return -EIO;
	}

	/* valid values for number of pages(num_pages): 1-8 */
	num_pages = sli_page_count(qmem->size, page_size);
	wq->num_pages = cpu_to_le16(num_pages);
	if (!num_pages || num_pages > SLI4_WQ_CREATE_MAX_PAGES)
		return -EIO;

	wq->cq_id = cpu_to_le16(cq_id);

	wq->page_size = page_size / SLI_PAGE_SIZE;

	if (sli4->wqe_size == SLI4_WQE_EXT_BYTES)
		wq->wqe_size_byte |= SLI4_WQE_EXT_SIZE;
	else
		wq->wqe_size_byte |= SLI4_WQE_SIZE;

	wq->wqe_count = cpu_to_le16(n_wqe);

	for (p = 0, addr = qmem->phys; p < num_pages; p++, addr += page_size) {
		wq->page_phys_addr[p].low  = cpu_to_le32(lower_32_bits(addr));
		wq->page_phys_addr[p].high = cpu_to_le32(upper_32_bits(addr));
	}

	return 0;
}

static int
sli_cmd_rq_create_v1(struct sli4 *sli4, void *buf, struct efc_dma *qmem,
		     u16 cq_id, u16 buffer_size)
{
	struct sli4_rqst_rq_create_v1 *rq;
	u32 p;
	uintptr_t addr;
	u32 num_pages;

	rq = sli_config_cmd_init(sli4, buf, SLI4_CFG_PYLD_LENGTH(rq_create_v1),
				 NULL);
	if (!rq)
		return -EIO;

	sli_cmd_fill_hdr(&rq->hdr, SLI4_OPC_RQ_CREATE, SLI4_SUBSYSTEM_FC,
			 CMD_V1, SLI4_RQST_PYLD_LEN(rq_create_v1));
	/* Disable "no buffer warnings" to avoid Lancer bug */
	rq->dim_dfd_dnb |= SLI4_RQ_CREATE_V1_DNB;

	/* valid values for number of pages: 1-8 (sec 4.5.6) */
	num_pages = sli_page_count(qmem->size, SLI_PAGE_SIZE);
	rq->num_pages = cpu_to_le16(num_pages);
	if (!num_pages ||
	    num_pages > SLI4_RQ_CREATE_V1_MAX_PAGES) {
		efc_log_info(sli4, "num_pages %d not valid, max %d\n",
			     num_pages, SLI4_RQ_CREATE_V1_MAX_PAGES);
		return -EIO;
	}

	/*
	 * RQE count is the total number of entries (note not lg2(# entries))
	 */
	rq->rqe_count = cpu_to_le16(qmem->size / SLI4_RQE_SIZE);

	rq->rqe_size_byte |= SLI4_RQE_SIZE_8;

	rq->page_size = SLI4_RQ_PAGE_SIZE_4096;

	if (buffer_size < sli4->rq_min_buf_size ||
	    buffer_size > sli4->rq_max_buf_size) {
		efc_log_err(sli4, "buffer_size %d out of range (%d-%d)\n",
			    buffer_size, sli4->rq_min_buf_size,
			    sli4->rq_max_buf_size);
		return -EIO;
	}
	rq->buffer_size = cpu_to_le32(buffer_size);

	rq->cq_id = cpu_to_le16(cq_id);

	for (p = 0, addr = qmem->phys;
			p < num_pages;
			p++, addr += SLI_PAGE_SIZE) {
		rq->page_phys_addr[p].low  = cpu_to_le32(lower_32_bits(addr));
		rq->page_phys_addr[p].high = cpu_to_le32(upper_32_bits(addr));
	}

	return 0;
}

static int
sli_cmd_rq_create_v2(struct sli4 *sli4, u32 num_rqs,
		     struct sli4_queue *qs[], u32 base_cq_id,
		     u32 header_buffer_size,
		     u32 payload_buffer_size, struct efc_dma *dma)
{
	struct sli4_rqst_rq_create_v2 *req = NULL;
	u32 i, p, offset = 0;
	u32 payload_size, page_count;
	uintptr_t addr;
	u32 num_pages;
	__le32 len;

	page_count =  sli_page_count(qs[0]->dma.size, SLI_PAGE_SIZE) * num_rqs;

	/* Payload length must accommodate both request and response */
	payload_size = max(SLI4_RQST_CMDSZ(rq_create_v2) +
			   SZ_DMAADDR * page_count,
			   sizeof(struct sli4_rsp_cmn_create_queue_set));

	dma->size = payload_size;
	dma->virt = dma_alloc_coherent(&sli4->pci->dev, dma->size,
				       &dma->phys, GFP_DMA);
	if (!dma->virt)
		return -EIO;

	memset(dma->virt, 0, payload_size);

	req = sli_config_cmd_init(sli4, sli4->bmbx.virt, payload_size, dma);
	if (!req)
		return -EIO;

	len =  SLI4_RQST_PYLD_LEN_VAR(rq_create_v2, SZ_DMAADDR * page_count);
	sli_cmd_fill_hdr(&req->hdr, SLI4_OPC_RQ_CREATE, SLI4_SUBSYSTEM_FC,
			 CMD_V2, len);
	/* Fill Payload fields */
	req->dim_dfd_dnb |= SLI4_RQCREATEV2_DNB;
	num_pages = sli_page_count(qs[0]->dma.size, SLI_PAGE_SIZE);
	req->num_pages = cpu_to_le16(num_pages);
	req->rqe_count = cpu_to_le16(qs[0]->dma.size / SLI4_RQE_SIZE);
	req->rqe_size_byte |= SLI4_RQE_SIZE_8;
	req->page_size = SLI4_RQ_PAGE_SIZE_4096;
	req->rq_count = num_rqs;
	req->base_cq_id = cpu_to_le16(base_cq_id);
	req->hdr_buffer_size = cpu_to_le16(header_buffer_size);
	req->payload_buffer_size = cpu_to_le16(payload_buffer_size);

	for (i = 0; i < num_rqs; i++) {
		for (p = 0, addr = qs[i]->dma.phys; p < num_pages;
		     p++, addr += SLI_PAGE_SIZE) {
			req->page_phys_addr[offset].low =
					cpu_to_le32(lower_32_bits(addr));
			req->page_phys_addr[offset].high =
					cpu_to_le32(upper_32_bits(addr));
			offset++;
		}
	}

	return 0;
}

static void
__sli_queue_destroy(struct sli4 *sli4, struct sli4_queue *q)
{
	if (!q->dma.size)
		return;

	dma_free_coherent(&sli4->pci->dev, q->dma.size,
			  q->dma.virt, q->dma.phys);
	memset(&q->dma, 0, sizeof(struct efc_dma));
}

int
__sli_queue_init(struct sli4 *sli4, struct sli4_queue *q, u32 qtype,
		 size_t size, u32 n_entries, u32 align)
{
	if (q->dma.virt) {
		efc_log_err(sli4, "%s failed\n", __func__);
		return -EIO;
	}

	memset(q, 0, sizeof(struct sli4_queue));

	q->dma.size = size * n_entries;
	q->dma.virt = dma_alloc_coherent(&sli4->pci->dev, q->dma.size,
					 &q->dma.phys, GFP_DMA);
	if (!q->dma.virt) {
		memset(&q->dma, 0, sizeof(struct efc_dma));
		efc_log_err(sli4, "%s allocation failed\n", SLI4_QNAME[qtype]);
		return -EIO;
	}

	memset(q->dma.virt, 0, size * n_entries);

	spin_lock_init(&q->lock);

	q->type = qtype;
	q->size = size;
	q->length = n_entries;

	if (q->type == SLI4_QTYPE_EQ || q->type == SLI4_QTYPE_CQ) {
		/* For prism, phase will be flipped after
		 * a sweep through eq and cq
		 */
		q->phase = 1;
	}

	/* Limit to hwf the queue size per interrupt */
	q->proc_limit = n_entries / 2;

	if (q->type == SLI4_QTYPE_EQ)
		q->posted_limit = q->length / 2;
	else
		q->posted_limit = 64;

	return 0;
}

int
sli_fc_rq_alloc(struct sli4 *sli4, struct sli4_queue *q,
		u32 n_entries, u32 buffer_size,
		struct sli4_queue *cq, bool is_hdr)
{
	if (__sli_queue_init(sli4, q, SLI4_QTYPE_RQ, SLI4_RQE_SIZE,
			     n_entries, SLI_PAGE_SIZE))
		return -EIO;

	if (sli_cmd_rq_create_v1(sli4, sli4->bmbx.virt, &q->dma, cq->id,
				 buffer_size))
		goto error;

	if (__sli_create_queue(sli4, q))
		goto error;

	if (is_hdr && q->id & 1) {
		efc_log_info(sli4, "bad header RQ_ID %d\n", q->id);
		goto error;
	} else if (!is_hdr  && (q->id & 1) == 0) {
		efc_log_info(sli4, "bad data RQ_ID %d\n", q->id);
		goto error;
	}

	if (is_hdr)
		q->u.flag |= SLI4_QUEUE_FLAG_HDR;
	else
		q->u.flag &= ~SLI4_QUEUE_FLAG_HDR;

	return 0;

error:
	__sli_queue_destroy(sli4, q);
	return -EIO;
}

int
sli_fc_rq_set_alloc(struct sli4 *sli4, u32 num_rq_pairs,
		    struct sli4_queue *qs[], u32 base_cq_id,
		    u32 n_entries, u32 header_buffer_size,
		    u32 payload_buffer_size)
{
	u32 i;
	struct efc_dma dma = {0};
	struct sli4_rsp_cmn_create_queue_set *rsp = NULL;
	void __iomem *db_regaddr = NULL;
	u32 num_rqs = num_rq_pairs * 2;

	for (i = 0; i < num_rqs; i++) {
		if (__sli_queue_init(sli4, qs[i], SLI4_QTYPE_RQ,
				     SLI4_RQE_SIZE, n_entries,
				     SLI_PAGE_SIZE)) {
			goto error;
		}
	}

	if (sli_cmd_rq_create_v2(sli4, num_rqs, qs, base_cq_id,
				 header_buffer_size, payload_buffer_size,
				 &dma)) {
		goto error;
	}

	if (sli_bmbx_command(sli4)) {
		efc_log_err(sli4, "bootstrap mailbox write failed RQSet\n");
		goto error;
	}

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		db_regaddr = sli4->reg[1] + SLI4_IF6_RQ_DB_REG;
	else
		db_regaddr = sli4->reg[0] + SLI4_RQ_DB_REG;

	rsp = dma.virt;
	if (rsp->hdr.status) {
		efc_log_err(sli4, "bad create RQSet status=%#x addl=%#x\n",
			    rsp->hdr.status, rsp->hdr.additional_status);
		goto error;
	}

	for (i = 0; i < num_rqs; i++) {
		qs[i]->id = i + le16_to_cpu(rsp->q_id);
		if ((qs[i]->id & 1) == 0)
			qs[i]->u.flag |= SLI4_QUEUE_FLAG_HDR;
		else
			qs[i]->u.flag &= ~SLI4_QUEUE_FLAG_HDR;

		qs[i]->db_regaddr = db_regaddr;
	}

	dma_free_coherent(&sli4->pci->dev, dma.size, dma.virt, dma.phys);

	return 0;

error:
	for (i = 0; i < num_rqs; i++)
		__sli_queue_destroy(sli4, qs[i]);

	if (dma.virt)
		dma_free_coherent(&sli4->pci->dev, dma.size, dma.virt,
				  dma.phys);

	return -EIO;
}

static int
sli_res_sli_config(struct sli4 *sli4, void *buf)
{
	struct sli4_cmd_sli_config *sli_config = buf;

	/* sanity check */
	if (!buf || sli_config->hdr.command !=
		    SLI4_MBX_CMD_SLI_CONFIG) {
		efc_log_err(sli4, "bad parameter buf=%p cmd=%#x\n", buf,
			    buf ? sli_config->hdr.command : -1);
		return -EIO;
	}

	if (le16_to_cpu(sli_config->hdr.status))
		return le16_to_cpu(sli_config->hdr.status);

	if (le32_to_cpu(sli_config->dw1_flags) & SLI4_SLICONF_EMB)
		return sli_config->payload.embed[4];

	efc_log_info(sli4, "external buffers not supported\n");
	return -EIO;
}

int
__sli_create_queue(struct sli4 *sli4, struct sli4_queue *q)
{
	struct sli4_rsp_cmn_create_queue *res_q = NULL;

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail %s\n",
			     SLI4_QNAME[q->type]);
		return -EIO;
	}
	if (sli_res_sli_config(sli4, sli4->bmbx.virt)) {
		efc_log_err(sli4, "bad status create %s\n",
			    SLI4_QNAME[q->type]);
		return -EIO;
	}
	res_q = (void *)((u8 *)sli4->bmbx.virt +
			offsetof(struct sli4_cmd_sli_config, payload));

	if (res_q->hdr.status) {
		efc_log_err(sli4, "bad create %s status=%#x addl=%#x\n",
			    SLI4_QNAME[q->type], res_q->hdr.status,
			    res_q->hdr.additional_status);
		return -EIO;
	}
	q->id = le16_to_cpu(res_q->q_id);
	switch (q->type) {
	case SLI4_QTYPE_EQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			q->db_regaddr = sli4->reg[1] + SLI4_IF6_EQ_DB_REG;
		else
			q->db_regaddr =	sli4->reg[0] + SLI4_EQCQ_DB_REG;
		break;
	case SLI4_QTYPE_CQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			q->db_regaddr = sli4->reg[1] + SLI4_IF6_CQ_DB_REG;
		else
			q->db_regaddr =	sli4->reg[0] + SLI4_EQCQ_DB_REG;
		break;
	case SLI4_QTYPE_MQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			q->db_regaddr = sli4->reg[1] + SLI4_IF6_MQ_DB_REG;
		else
			q->db_regaddr =	sli4->reg[0] + SLI4_MQ_DB_REG;
		break;
	case SLI4_QTYPE_RQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			q->db_regaddr = sli4->reg[1] + SLI4_IF6_RQ_DB_REG;
		else
			q->db_regaddr =	sli4->reg[0] + SLI4_RQ_DB_REG;
		break;
	case SLI4_QTYPE_WQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			q->db_regaddr = sli4->reg[1] + SLI4_IF6_WQ_DB_REG;
		else
			q->db_regaddr =	sli4->reg[0] + SLI4_IO_WQ_DB_REG;
		break;
	default:
		break;
	}

	return 0;
}

int
sli_get_queue_entry_size(struct sli4 *sli4, u32 qtype)
{
	u32 size = 0;

	switch (qtype) {
	case SLI4_QTYPE_EQ:
		size = sizeof(u32);
		break;
	case SLI4_QTYPE_CQ:
		size = 16;
		break;
	case SLI4_QTYPE_MQ:
		size = 256;
		break;
	case SLI4_QTYPE_WQ:
		size = sli4->wqe_size;
		break;
	case SLI4_QTYPE_RQ:
		size = SLI4_RQE_SIZE;
		break;
	default:
		efc_log_info(sli4, "unknown queue type %d\n", qtype);
		return -1;
	}
	return size;
}

int
sli_queue_alloc(struct sli4 *sli4, u32 qtype,
		struct sli4_queue *q, u32 n_entries,
		     struct sli4_queue *assoc)
{
	int size;
	u32 align = 0;

	/* get queue size */
	size = sli_get_queue_entry_size(sli4, qtype);
	if (size < 0)
		return -EIO;
	align = SLI_PAGE_SIZE;

	if (__sli_queue_init(sli4, q, qtype, size, n_entries, align))
		return -EIO;

	switch (qtype) {
	case SLI4_QTYPE_EQ:
		if (!sli_cmd_common_create_eq(sli4, sli4->bmbx.virt, &q->dma) &&
		    !__sli_create_queue(sli4, q))
			return 0;

		break;
	case SLI4_QTYPE_CQ:
		if (!sli_cmd_common_create_cq(sli4, sli4->bmbx.virt, &q->dma,
					      assoc ? assoc->id : 0) &&
		    !__sli_create_queue(sli4, q))
			return 0;

		break;
	case SLI4_QTYPE_MQ:
		assoc->u.flag |= SLI4_QUEUE_FLAG_MQ;
		if (!sli_cmd_common_create_mq_ext(sli4, sli4->bmbx.virt,
						  &q->dma, assoc->id) &&
		    !__sli_create_queue(sli4, q))
			return 0;

		break;
	case SLI4_QTYPE_WQ:
		if (!sli_cmd_wq_create(sli4, sli4->bmbx.virt, &q->dma,
				       assoc ? assoc->id : 0) &&
		    !__sli_create_queue(sli4, q))
			return 0;

		break;
	default:
		efc_log_info(sli4, "unknown queue type %d\n", qtype);
	}

	__sli_queue_destroy(sli4, q);
	return -EIO;
}

static int sli_cmd_cq_set_create(struct sli4 *sli4,
				 struct sli4_queue *qs[], u32 num_cqs,
				 struct sli4_queue *eqs[],
				 struct efc_dma *dma)
{
	struct sli4_rqst_cmn_create_cq_set_v0 *req = NULL;
	uintptr_t addr;
	u32 i, offset = 0,  page_bytes = 0, payload_size;
	u32 p = 0, page_size = 0, n_cqe = 0, num_pages_cq;
	u32 dw5_flags = 0;
	u16 dw6w1_flags = 0;
	__le32 req_len;

	n_cqe = qs[0]->dma.size / SLI4_CQE_BYTES;
	switch (n_cqe) {
	case 256:
	case 512:
	case 1024:
	case 2048:
		page_size = 1;
		break;
	case 4096:
		page_size = 2;
		break;
	default:
		return -EIO;
	}

	page_bytes = page_size * SLI_PAGE_SIZE;
	num_pages_cq = sli_page_count(qs[0]->dma.size, page_bytes);
	payload_size = max(SLI4_RQST_CMDSZ(cmn_create_cq_set_v0) +
			   (SZ_DMAADDR * num_pages_cq * num_cqs),
			   sizeof(struct sli4_rsp_cmn_create_queue_set));

	dma->size = payload_size;
	dma->virt = dma_alloc_coherent(&sli4->pci->dev, dma->size,
				       &dma->phys, GFP_DMA);
	if (!dma->virt)
		return -EIO;

	memset(dma->virt, 0, payload_size);

	req = sli_config_cmd_init(sli4, sli4->bmbx.virt, payload_size, dma);
	if (!req)
		return -EIO;

	req_len = SLI4_RQST_PYLD_LEN_VAR(cmn_create_cq_set_v0,
					 SZ_DMAADDR * num_pages_cq * num_cqs);
	sli_cmd_fill_hdr(&req->hdr, SLI4_CMN_CREATE_CQ_SET, SLI4_SUBSYSTEM_FC,
			 CMD_V0, req_len);
	req->page_size = page_size;

	req->num_pages = cpu_to_le16(num_pages_cq);
	switch (num_pages_cq) {
	case 1:
		dw5_flags |= SLI4_CQ_CNT_VAL(256);
		break;
	case 2:
		dw5_flags |= SLI4_CQ_CNT_VAL(512);
		break;
	case 4:
		dw5_flags |= SLI4_CQ_CNT_VAL(1024);
		break;
	case 8:
		dw5_flags |= SLI4_CQ_CNT_VAL(LARGE);
		dw6w1_flags |= (n_cqe & SLI4_CREATE_CQSETV0_CQE_COUNT);
		break;
	default:
		efc_log_info(sli4, "num_pages %d not valid\n", num_pages_cq);
		return -EIO;
	}

	dw5_flags |= SLI4_CREATE_CQSETV0_EVT;
	dw5_flags |= SLI4_CREATE_CQSETV0_VALID;
	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		dw5_flags |= SLI4_CREATE_CQSETV0_AUTOVALID;

	dw6w1_flags &= ~SLI4_CREATE_CQSETV0_ARM;

	req->dw5_flags = cpu_to_le32(dw5_flags);
	req->dw6w1_flags = cpu_to_le16(dw6w1_flags);

	req->num_cq_req = cpu_to_le16(num_cqs);

	/* Fill page addresses of all the CQs. */
	for (i = 0; i < num_cqs; i++) {
		req->eq_id[i] = cpu_to_le16(eqs[i]->id);
		for (p = 0, addr = qs[i]->dma.phys; p < num_pages_cq;
		     p++, addr += page_bytes) {
			req->page_phys_addr[offset].low =
				cpu_to_le32(lower_32_bits(addr));
			req->page_phys_addr[offset].high =
				cpu_to_le32(upper_32_bits(addr));
			offset++;
		}
	}

	return 0;
}

int
sli_cq_alloc_set(struct sli4 *sli4, struct sli4_queue *qs[],
		 u32 num_cqs, u32 n_entries, struct sli4_queue *eqs[])
{
	u32 i;
	struct efc_dma dma = {0};
	struct sli4_rsp_cmn_create_queue_set *res;
	void __iomem *db_regaddr;

	/* Align the queue DMA memory */
	for (i = 0; i < num_cqs; i++) {
		if (__sli_queue_init(sli4, qs[i], SLI4_QTYPE_CQ, SLI4_CQE_BYTES,
				     n_entries, SLI_PAGE_SIZE))
			goto error;
	}

	if (sli_cmd_cq_set_create(sli4, qs, num_cqs, eqs, &dma))
		goto error;

	if (sli_bmbx_command(sli4))
		goto error;

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		db_regaddr = sli4->reg[1] + SLI4_IF6_CQ_DB_REG;
	else
		db_regaddr = sli4->reg[0] + SLI4_EQCQ_DB_REG;

	res = dma.virt;
	if (res->hdr.status) {
		efc_log_err(sli4, "bad create CQSet status=%#x addl=%#x\n",
			    res->hdr.status, res->hdr.additional_status);
		goto error;
	}

	/* Check if we got all requested CQs. */
	if (le16_to_cpu(res->num_q_allocated) != num_cqs) {
		efc_log_crit(sli4, "Requested count CQs doesn't match.\n");
		goto error;
	}
	/* Fill the resp cq ids. */
	for (i = 0; i < num_cqs; i++) {
		qs[i]->id = le16_to_cpu(res->q_id) + i;
		qs[i]->db_regaddr = db_regaddr;
	}

	dma_free_coherent(&sli4->pci->dev, dma.size, dma.virt, dma.phys);

	return 0;

error:
	for (i = 0; i < num_cqs; i++)
		__sli_queue_destroy(sli4, qs[i]);

	if (dma.virt)
		dma_free_coherent(&sli4->pci->dev, dma.size, dma.virt,
				  dma.phys);

	return -EIO;
}

static int
sli_cmd_common_destroy_q(struct sli4 *sli4, u8 opc, u8 subsystem, u16 q_id)
{
	struct sli4_rqst_cmn_destroy_q *req;

	/* Payload length must accommodate both request and response */
	req = sli_config_cmd_init(sli4, sli4->bmbx.virt,
				  SLI4_CFG_PYLD_LENGTH(cmn_destroy_q), NULL);
	if (!req)
		return -EIO;

	sli_cmd_fill_hdr(&req->hdr, opc, subsystem,
			 CMD_V0, SLI4_RQST_PYLD_LEN(cmn_destroy_q));
	req->q_id = cpu_to_le16(q_id);

	return 0;
}

int
sli_queue_free(struct sli4 *sli4, struct sli4_queue *q,
	       u32 destroy_queues, u32 free_memory)
{
	int rc = 0;
	u8 opcode, subsystem;
	struct sli4_rsp_hdr *res;

	if (!q) {
		efc_log_err(sli4, "bad parameter sli4=%p q=%p\n", sli4, q);
		return -EIO;
	}

	if (!destroy_queues)
		goto free_mem;

	switch (q->type) {
	case SLI4_QTYPE_EQ:
		opcode = SLI4_CMN_DESTROY_EQ;
		subsystem = SLI4_SUBSYSTEM_COMMON;
		break;
	case SLI4_QTYPE_CQ:
		opcode = SLI4_CMN_DESTROY_CQ;
		subsystem = SLI4_SUBSYSTEM_COMMON;
		break;
	case SLI4_QTYPE_MQ:
		opcode = SLI4_CMN_DESTROY_MQ;
		subsystem = SLI4_SUBSYSTEM_COMMON;
		break;
	case SLI4_QTYPE_WQ:
		opcode = SLI4_OPC_WQ_DESTROY;
		subsystem = SLI4_SUBSYSTEM_FC;
		break;
	case SLI4_QTYPE_RQ:
		opcode = SLI4_OPC_RQ_DESTROY;
		subsystem = SLI4_SUBSYSTEM_FC;
		break;
	default:
		efc_log_info(sli4, "bad queue type %d\n", q->type);
		rc = -EIO;
		goto free_mem;
	}

	rc = sli_cmd_common_destroy_q(sli4, opcode, subsystem, q->id);
	if (rc)
		goto free_mem;

	rc = sli_bmbx_command(sli4);
	if (rc)
		goto free_mem;

	rc = sli_res_sli_config(sli4, sli4->bmbx.virt);
	if (rc)
		goto free_mem;

	res = (void *)((u8 *)sli4->bmbx.virt +
			     offsetof(struct sli4_cmd_sli_config, payload));
	if (res->status) {
		efc_log_err(sli4, "destroy %s st=%#x addl=%#x\n",
			    SLI4_QNAME[q->type], res->status,
			    res->additional_status);
		rc = -EIO;
		goto free_mem;
	}

free_mem:
	if (free_memory)
		__sli_queue_destroy(sli4, q);

	return rc;
}

int
sli_queue_eq_arm(struct sli4 *sli4, struct sli4_queue *q, bool arm)
{
	u32 val;
	unsigned long flags = 0;
	u32 a = arm ? SLI4_EQCQ_ARM : SLI4_EQCQ_UNARM;

	spin_lock_irqsave(&q->lock, flags);
	if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
		val = sli_format_if6_eq_db_data(q->n_posted, q->id, a);
	else
		val = sli_format_eq_db_data(q->n_posted, q->id, a);

	writel(val, q->db_regaddr);
	q->n_posted = 0;
	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

int
sli_queue_arm(struct sli4 *sli4, struct sli4_queue *q, bool arm)
{
	u32 val = 0;
	unsigned long flags = 0;
	u32 a = arm ? SLI4_EQCQ_ARM : SLI4_EQCQ_UNARM;

	spin_lock_irqsave(&q->lock, flags);

	switch (q->type) {
	case SLI4_QTYPE_EQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			val = sli_format_if6_eq_db_data(q->n_posted, q->id, a);
		else
			val = sli_format_eq_db_data(q->n_posted, q->id, a);

		writel(val, q->db_regaddr);
		q->n_posted = 0;
		break;
	case SLI4_QTYPE_CQ:
		if (sli4->if_type == SLI4_INTF_IF_TYPE_6)
			val = sli_format_if6_cq_db_data(q->n_posted, q->id, a);
		else
			val = sli_format_cq_db_data(q->n_posted, q->id, a);

		writel(val, q->db_regaddr);
		q->n_posted = 0;
		break;
	default:
		efc_log_info(sli4, "should only be used for EQ/CQ, not %s\n",
			     SLI4_QNAME[q->type]);
	}

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

int
sli_wq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	u32 qindex;
	u32 val = 0;

	qindex = q->index;
	qe += q->index * q->size;

	if (sli4->params.perf_wq_id_association)
		sli_set_wq_id_association(entry, q->id);

	memcpy(qe, entry, q->size);
	val = sli_format_wq_db_data(q->id);

	writel(val, q->db_regaddr);
	q->index = (q->index + 1) & (q->length - 1);

	return qindex;
}

int
sli_mq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	u32 qindex;
	u32 val = 0;
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	qindex = q->index;
	qe += q->index * q->size;

	memcpy(qe, entry, q->size);
	val = sli_format_mq_db_data(q->id);
	writel(val, q->db_regaddr);
	q->index = (q->index + 1) & (q->length - 1);
	spin_unlock_irqrestore(&q->lock, flags);

	return qindex;
}

int
sli_rq_write(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	u32 qindex;
	u32 val = 0;

	qindex = q->index;
	qe += q->index * q->size;

	memcpy(qe, entry, q->size);

	/*
	 * In RQ-pair, an RQ either contains the FC header
	 * (i.e. is_hdr == TRUE) or the payload.
	 *
	 * Don't ring doorbell for payload RQ
	 */
	if (!(q->u.flag & SLI4_QUEUE_FLAG_HDR))
		goto skip;

	val = sli_format_rq_db_data(q->id);
	writel(val, q->db_regaddr);
skip:
	q->index = (q->index + 1) & (q->length - 1);

	return qindex;
}

int
sli_eq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	unsigned long flags = 0;
	u16 wflags = 0;

	spin_lock_irqsave(&q->lock, flags);

	qe += q->index * q->size;

	/* Check if eqe is valid */
	wflags = le16_to_cpu(((struct sli4_eqe *)qe)->dw0w0_flags);

	if ((wflags & SLI4_EQE_VALID) != q->phase) {
		spin_unlock_irqrestore(&q->lock, flags);
		return -EIO;
	}

	if (sli4->if_type != SLI4_INTF_IF_TYPE_6) {
		wflags &= ~SLI4_EQE_VALID;
		((struct sli4_eqe *)qe)->dw0w0_flags = cpu_to_le16(wflags);
	}

	memcpy(entry, qe, q->size);
	q->index = (q->index + 1) & (q->length - 1);
	q->n_posted++;
	/*
	 * For prism, the phase value will be used
	 * to check the validity of eq/cq entries.
	 * The value toggles after a complete sweep
	 * through the queue.
	 */

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6 && q->index == 0)
		q->phase ^= (u16)0x1;

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

int
sli_cq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	unsigned long flags = 0;
	u32 dwflags = 0;
	bool valid_bit_set;

	spin_lock_irqsave(&q->lock, flags);

	qe += q->index * q->size;

	/* Check if cqe is valid */
	dwflags = le32_to_cpu(((struct sli4_mcqe *)qe)->dw3_flags);
	valid_bit_set = (dwflags & SLI4_MCQE_VALID) != 0;

	if (valid_bit_set != q->phase) {
		spin_unlock_irqrestore(&q->lock, flags);
		return -EIO;
	}

	if (sli4->if_type != SLI4_INTF_IF_TYPE_6) {
		dwflags &= ~SLI4_MCQE_VALID;
		((struct sli4_mcqe *)qe)->dw3_flags = cpu_to_le32(dwflags);
	}

	memcpy(entry, qe, q->size);
	q->index = (q->index + 1) & (q->length - 1);
	q->n_posted++;
	/*
	 * For prism, the phase value will be used
	 * to check the validity of eq/cq entries.
	 * The value toggles after a complete sweep
	 * through the queue.
	 */

	if (sli4->if_type == SLI4_INTF_IF_TYPE_6 && q->index == 0)
		q->phase ^= (u16)0x1;

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

int
sli_mq_read(struct sli4 *sli4, struct sli4_queue *q, u8 *entry)
{
	u8 *qe = q->dma.virt;
	unsigned long flags = 0;

	spin_lock_irqsave(&q->lock, flags);

	qe += q->u.r_idx * q->size;

	/* Check if mqe is valid */
	if (q->index == q->u.r_idx) {
		spin_unlock_irqrestore(&q->lock, flags);
		return -EIO;
	}

	memcpy(entry, qe, q->size);
	q->u.r_idx = (q->u.r_idx + 1) & (q->length - 1);

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

int
sli_eq_parse(struct sli4 *sli4, u8 *buf, u16 *cq_id)
{
	struct sli4_eqe *eqe = (void *)buf;
	int rc = 0;
	u16 flags = 0;
	u16 majorcode;
	u16 minorcode;

	if (!buf || !cq_id) {
		efc_log_err(sli4, "bad parameters sli4=%p buf=%p cq_id=%p\n",
			    sli4, buf, cq_id);
		return -EIO;
	}

	flags = le16_to_cpu(eqe->dw0w0_flags);
	majorcode = (flags & SLI4_EQE_MJCODE) >> 1;
	minorcode = (flags & SLI4_EQE_MNCODE) >> 4;
	switch (majorcode) {
	case SLI4_MAJOR_CODE_STANDARD:
		*cq_id = le16_to_cpu(eqe->resource_id);
		break;
	case SLI4_MAJOR_CODE_SENTINEL:
		efc_log_info(sli4, "sentinel EQE\n");
		rc = SLI4_EQE_STATUS_EQ_FULL;
		break;
	default:
		efc_log_info(sli4, "Unsupported EQE: major %x minor %x\n",
			     majorcode, minorcode);
		rc = -EIO;
	}

	return rc;
}

int
sli_cq_parse(struct sli4 *sli4, struct sli4_queue *cq, u8 *cqe,
	     enum sli4_qentry *etype, u16 *q_id)
{
	int rc = 0;

	if (!cq || !cqe || !etype) {
		efc_log_err(sli4, "bad params sli4=%p cq=%p cqe=%p etype=%p q_id=%p\n",
			    sli4, cq, cqe, etype, q_id);
		return -EINVAL;
	}

	/* Parse a CQ entry to retrieve the event type and the queue id */
	if (cq->u.flag & SLI4_QUEUE_FLAG_MQ) {
		struct sli4_mcqe	*mcqe = (void *)cqe;

		if (le32_to_cpu(mcqe->dw3_flags) & SLI4_MCQE_AE) {
			*etype = SLI4_QENTRY_ASYNC;
		} else {
			*etype = SLI4_QENTRY_MQ;
			rc = sli_cqe_mq(sli4, mcqe);
		}
		*q_id = -1;
	} else {
		rc = sli_fc_cqe_parse(sli4, cq, cqe, etype, q_id);
	}

	return rc;
}

int
sli_abort_wqe(struct sli4 *sli, void *buf, enum sli4_abort_type type,
	      bool send_abts, u32 ids, u32 mask, u16 tag, u16 cq_id)
{
	struct sli4_abort_wqe *abort = buf;

	memset(buf, 0, sli->wqe_size);

	switch (type) {
	case SLI4_ABORT_XRI:
		abort->criteria = SLI4_ABORT_CRITERIA_XRI_TAG;
		if (mask) {
			efc_log_warn(sli, "%#x aborting XRI %#x warning non-zero mask",
				     mask, ids);
			mask = 0;
		}
		break;
	case SLI4_ABORT_ABORT_ID:
		abort->criteria = SLI4_ABORT_CRITERIA_ABORT_TAG;
		break;
	case SLI4_ABORT_REQUEST_ID:
		abort->criteria = SLI4_ABORT_CRITERIA_REQUEST_TAG;
		break;
	default:
		efc_log_info(sli, "unsupported type %#x\n", type);
		return -EIO;
	}

	abort->ia_ir_byte |= send_abts ? 0 : 1;

	/* Suppress ABTS retries */
	abort->ia_ir_byte |= SLI4_ABRT_WQE_IR;

	abort->t_mask = cpu_to_le32(mask);
	abort->t_tag  = cpu_to_le32(ids);
	abort->command = SLI4_WQE_ABORT;
	abort->request_tag = cpu_to_le16(tag);

	abort->dw10w0_flags = cpu_to_le16(SLI4_ABRT_WQE_QOSD);

	abort->cq_id = cpu_to_le16(cq_id);
	abort->cmdtype_wqec_byte |= SLI4_CMD_ABORT_WQE;

	return 0;
}

int
sli_els_request64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
		      struct sli_els_params *params)
{
	struct sli4_els_request64_wqe *els = buf;
	struct sli4_sge *sge = sgl->virt;
	bool is_fabric = false;
	struct sli4_bde *bptr;

	memset(buf, 0, sli->wqe_size);

	bptr = &els->els_request_payload;
	if (sli->params.sgl_pre_registered) {
		els->qosd_xbl_hlm_iod_dbde_wqes &= ~SLI4_REQ_WQE_XBL;

		els->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_REQ_WQE_DBDE;
		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (params->xmit_len & SLI4_BDE_LEN_MASK));

		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;
	} else {
		els->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_REQ_WQE_XBL;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
				    ((2 * sizeof(struct sli4_sge)) &
				     SLI4_BDE_LEN_MASK));
		bptr->u.blp.low  = cpu_to_le32(lower_32_bits(sgl->phys));
		bptr->u.blp.high = cpu_to_le32(upper_32_bits(sgl->phys));
	}

	els->els_request_payload_length = cpu_to_le32(params->xmit_len);
	els->max_response_payload_length = cpu_to_le32(params->rsp_len);

	els->xri_tag = cpu_to_le16(params->xri);
	els->timer = params->timeout;
	els->class_byte |= SLI4_GENERIC_CLASS_CLASS_3;

	els->command = SLI4_WQE_ELS_REQUEST64;

	els->request_tag = cpu_to_le16(params->tag);

	els->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_REQ_WQE_IOD;

	els->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_REQ_WQE_QOSD;

	/* figure out the ELS_ID value from the request buffer */

	switch (params->cmd) {
	case ELS_LOGO:
		els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_LOGO << SLI4_REQ_WQE_ELSID_SHFT;
		if (params->rpi_registered) {
			els->ct_byte |=
			SLI4_GENERIC_CONTEXT_RPI << SLI4_REQ_WQE_CT_SHFT;
			els->context_tag = cpu_to_le16(params->rpi);
		} else {
			els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
			els->context_tag = cpu_to_le16(params->vpi);
		}
		if (params->d_id == FC_FID_FLOGI)
			is_fabric = true;
		break;
	case ELS_FDISC:
		if (params->d_id == FC_FID_FLOGI)
			is_fabric = true;
		if (params->s_id == 0) {
			els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_FDISC << SLI4_REQ_WQE_ELSID_SHFT;
			is_fabric = true;
		} else {
			els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_OTHER << SLI4_REQ_WQE_ELSID_SHFT;
		}
		els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
		els->context_tag = cpu_to_le16(params->vpi);
		els->sid_sp_dword |= cpu_to_le32(1 << SLI4_REQ_WQE_SP_SHFT);
		break;
	case ELS_FLOGI:
		els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
		els->context_tag = cpu_to_le16(params->vpi);
		/*
		 * Set SP here ... we haven't done a REG_VPI yet
		 * need to maybe not set this when we have
		 * completed VFI/VPI registrations ...
		 *
		 * Use the FC_ID of the SPORT if it has been allocated,
		 * otherwise use an S_ID of zero.
		 */
		els->sid_sp_dword |= cpu_to_le32(1 << SLI4_REQ_WQE_SP_SHFT);
		if (params->s_id != U32_MAX)
			els->sid_sp_dword |= cpu_to_le32(params->s_id);
		break;
	case ELS_PLOGI:
		els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_PLOGI << SLI4_REQ_WQE_ELSID_SHFT;
		els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
		els->context_tag = cpu_to_le16(params->vpi);
		break;
	case ELS_SCR:
		els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_OTHER << SLI4_REQ_WQE_ELSID_SHFT;
		els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
		els->context_tag = cpu_to_le16(params->vpi);
		break;
	default:
		els->cmdtype_elsid_byte |=
			SLI4_ELS_REQUEST64_OTHER << SLI4_REQ_WQE_ELSID_SHFT;
		if (params->rpi_registered) {
			els->ct_byte |= (SLI4_GENERIC_CONTEXT_RPI <<
					 SLI4_REQ_WQE_CT_SHFT);
			els->context_tag = cpu_to_le16(params->vpi);
		} else {
			els->ct_byte |=
			SLI4_GENERIC_CONTEXT_VPI << SLI4_REQ_WQE_CT_SHFT;
			els->context_tag = cpu_to_le16(params->vpi);
		}
		break;
	}

	if (is_fabric)
		els->cmdtype_elsid_byte |= SLI4_ELS_REQUEST64_CMD_FABRIC;
	else
		els->cmdtype_elsid_byte |= SLI4_ELS_REQUEST64_CMD_NON_FABRIC;

	els->cq_id = cpu_to_le16(SLI4_CQ_DEFAULT);

	if (((els->ct_byte & SLI4_REQ_WQE_CT) >> SLI4_REQ_WQE_CT_SHFT) !=
					SLI4_GENERIC_CONTEXT_RPI)
		els->remote_id_dword = cpu_to_le32(params->d_id);

	if (((els->ct_byte & SLI4_REQ_WQE_CT) >> SLI4_REQ_WQE_CT_SHFT) ==
					SLI4_GENERIC_CONTEXT_VPI)
		els->temporary_rpi = cpu_to_le16(params->rpi);

	return 0;
}

int
sli_fcp_icmnd64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl, u16 xri,
		    u16 tag, u16 cq_id, u32 rpi, u32 rnode_fcid, u8 timeout)
{
	struct sli4_fcp_icmnd64_wqe *icmnd = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;
	u32 len;

	memset(buf, 0, sli->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}
	sge = sgl->virt;
	bptr = &icmnd->bde;
	if (sli->params.sgl_pre_registered) {
		icmnd->qosd_xbl_hlm_iod_dbde_wqes &= ~SLI4_ICMD_WQE_XBL;

		icmnd->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_ICMD_WQE_DBDE;
		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (le32_to_cpu(sge[0].buffer_length) &
				     SLI4_BDE_LEN_MASK));

		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;
	} else {
		icmnd->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_ICMD_WQE_XBL;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
				    (sgl->size & SLI4_BDE_LEN_MASK));

		bptr->u.blp.low  = cpu_to_le32(lower_32_bits(sgl->phys));
		bptr->u.blp.high = cpu_to_le32(upper_32_bits(sgl->phys));
	}

	len = le32_to_cpu(sge[0].buffer_length) +
	      le32_to_cpu(sge[1].buffer_length);
	icmnd->payload_offset_length = cpu_to_le16(len);
	icmnd->xri_tag = cpu_to_le16(xri);
	icmnd->context_tag = cpu_to_le16(rpi);
	icmnd->timer = timeout;

	/* WQE word 4 contains read transfer length */
	icmnd->class_pu_byte |= 2 << SLI4_ICMD_WQE_PU_SHFT;
	icmnd->class_pu_byte |= SLI4_GENERIC_CLASS_CLASS_3;
	icmnd->command = SLI4_WQE_FCP_ICMND64;
	icmnd->dif_ct_bs_byte |=
		SLI4_GENERIC_CONTEXT_RPI << SLI4_ICMD_WQE_CT_SHFT;

	icmnd->abort_tag = cpu_to_le32(xri);

	icmnd->request_tag = cpu_to_le16(tag);
	icmnd->len_loc1_byte |= SLI4_ICMD_WQE_LEN_LOC_BIT1;
	icmnd->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_ICMD_WQE_LEN_LOC_BIT2;
	icmnd->cmd_type_byte |= SLI4_CMD_FCP_ICMND64_WQE;
	icmnd->cq_id = cpu_to_le16(cq_id);

	return  0;
}

int
sli_fcp_iread64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
		    u32 first_data_sge, u32 xfer_len, u16 xri, u16 tag,
		    u16 cq_id, u32 rpi, u32 rnode_fcid,
		    u8 dif, u8 bs, u8 timeout)
{
	struct sli4_fcp_iread64_wqe *iread = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;
	u32 sge_flags, len;

	memset(buf, 0, sli->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}

	sge = sgl->virt;
	bptr = &iread->bde;
	if (sli->params.sgl_pre_registered) {
		iread->qosd_xbl_hlm_iod_dbde_wqes &= ~SLI4_IR_WQE_XBL;

		iread->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IR_WQE_DBDE;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (le32_to_cpu(sge[0].buffer_length) &
				     SLI4_BDE_LEN_MASK));

		bptr->u.blp.low  = sge[0].buffer_address_low;
		bptr->u.blp.high = sge[0].buffer_address_high;
	} else {
		iread->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IR_WQE_XBL;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
				    (sgl->size & SLI4_BDE_LEN_MASK));

		bptr->u.blp.low  =
				cpu_to_le32(lower_32_bits(sgl->phys));
		bptr->u.blp.high =
				cpu_to_le32(upper_32_bits(sgl->phys));

		/*
		 * fill out fcp_cmnd buffer len and change resp buffer to be of
		 * type "skip" (note: response will still be written to sge[1]
		 * if necessary)
		 */
		len = le32_to_cpu(sge[0].buffer_length);
		iread->fcp_cmd_buffer_length = cpu_to_le16(len);

		sge_flags = le32_to_cpu(sge[1].dw2_flags);
		sge_flags &= (~SLI4_SGE_TYPE_MASK);
		sge_flags |= (SLI4_SGE_TYPE_SKIP << SLI4_SGE_TYPE_SHIFT);
		sge[1].dw2_flags = cpu_to_le32(sge_flags);
	}

	len = le32_to_cpu(sge[0].buffer_length) +
	      le32_to_cpu(sge[1].buffer_length);
	iread->payload_offset_length = cpu_to_le16(len);
	iread->total_transfer_length = cpu_to_le32(xfer_len);

	iread->xri_tag = cpu_to_le16(xri);
	iread->context_tag = cpu_to_le16(rpi);

	iread->timer = timeout;

	/* WQE word 4 contains read transfer length */
	iread->class_pu_byte |= 2 << SLI4_IR_WQE_PU_SHFT;
	iread->class_pu_byte |= SLI4_GENERIC_CLASS_CLASS_3;
	iread->command = SLI4_WQE_FCP_IREAD64;
	iread->dif_ct_bs_byte |=
		SLI4_GENERIC_CONTEXT_RPI << SLI4_IR_WQE_CT_SHFT;
	iread->dif_ct_bs_byte |= dif;
	iread->dif_ct_bs_byte  |= bs << SLI4_IR_WQE_BS_SHFT;

	iread->abort_tag = cpu_to_le32(xri);

	iread->request_tag = cpu_to_le16(tag);
	iread->len_loc1_byte |= SLI4_IR_WQE_LEN_LOC_BIT1;
	iread->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IR_WQE_LEN_LOC_BIT2;
	iread->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IR_WQE_IOD;
	iread->cmd_type_byte |= SLI4_CMD_FCP_IREAD64_WQE;
	iread->cq_id = cpu_to_le16(cq_id);

	if (sli->params.perf_hint) {
		bptr = &iread->first_data_bde;
		bptr->bde_type_buflen =	cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			  (le32_to_cpu(sge[first_data_sge].buffer_length) &
			     SLI4_BDE_LEN_MASK));
		bptr->u.data.low =
			sge[first_data_sge].buffer_address_low;
		bptr->u.data.high =
			sge[first_data_sge].buffer_address_high;
	}

	return  0;
}

int
sli_fcp_iwrite64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
		     u32 first_data_sge, u32 xfer_len,
		     u32 first_burst, u16 xri, u16 tag,
		     u16 cq_id, u32 rpi,
		     u32 rnode_fcid,
		     u8 dif, u8 bs, u8 timeout)
{
	struct sli4_fcp_iwrite64_wqe *iwrite = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;
	u32 sge_flags, min, len;

	memset(buf, 0, sli->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}
	sge = sgl->virt;
	bptr = &iwrite->bde;
	if (sli->params.sgl_pre_registered) {
		iwrite->qosd_xbl_hlm_iod_dbde_wqes &= ~SLI4_IWR_WQE_XBL;

		iwrite->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IWR_WQE_DBDE;
		bptr->bde_type_buflen = cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
		       (le32_to_cpu(sge[0].buffer_length) & SLI4_BDE_LEN_MASK));
		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;
	} else {
		iwrite->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IWR_WQE_XBL;

		bptr->bde_type_buflen =	cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
					(sgl->size & SLI4_BDE_LEN_MASK));

		bptr->u.blp.low  = cpu_to_le32(lower_32_bits(sgl->phys));
		bptr->u.blp.high = cpu_to_le32(upper_32_bits(sgl->phys));

		/*
		 * fill out fcp_cmnd buffer len and change resp buffer to be of
		 * type "skip" (note: response will still be written to sge[1]
		 * if necessary)
		 */
		len = le32_to_cpu(sge[0].buffer_length);
		iwrite->fcp_cmd_buffer_length = cpu_to_le16(len);
		sge_flags = le32_to_cpu(sge[1].dw2_flags);
		sge_flags &= ~SLI4_SGE_TYPE_MASK;
		sge_flags |= (SLI4_SGE_TYPE_SKIP << SLI4_SGE_TYPE_SHIFT);
		sge[1].dw2_flags = cpu_to_le32(sge_flags);
	}

	len = le32_to_cpu(sge[0].buffer_length) +
	      le32_to_cpu(sge[1].buffer_length);
	iwrite->payload_offset_length = cpu_to_le16(len);
	iwrite->total_transfer_length = cpu_to_le16(xfer_len);
	min = (xfer_len < first_burst) ? xfer_len : first_burst;
	iwrite->initial_transfer_length = cpu_to_le16(min);

	iwrite->xri_tag = cpu_to_le16(xri);
	iwrite->context_tag = cpu_to_le16(rpi);

	iwrite->timer = timeout;
	/* WQE word 4 contains read transfer length */
	iwrite->class_pu_byte |= 2 << SLI4_IWR_WQE_PU_SHFT;
	iwrite->class_pu_byte |= SLI4_GENERIC_CLASS_CLASS_3;
	iwrite->command = SLI4_WQE_FCP_IWRITE64;
	iwrite->dif_ct_bs_byte |=
			SLI4_GENERIC_CONTEXT_RPI << SLI4_IWR_WQE_CT_SHFT;
	iwrite->dif_ct_bs_byte |= dif;
	iwrite->dif_ct_bs_byte |= bs << SLI4_IWR_WQE_BS_SHFT;

	iwrite->abort_tag = cpu_to_le32(xri);

	iwrite->request_tag = cpu_to_le16(tag);
	iwrite->len_loc1_byte |= SLI4_IWR_WQE_LEN_LOC_BIT1;
	iwrite->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_IWR_WQE_LEN_LOC_BIT2;
	iwrite->cmd_type_byte |= SLI4_CMD_FCP_IWRITE64_WQE;
	iwrite->cq_id = cpu_to_le16(cq_id);

	if (sli->params.perf_hint) {
		bptr = &iwrite->first_data_bde;

		bptr->bde_type_buflen =	cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			 (le32_to_cpu(sge[first_data_sge].buffer_length) &
			     SLI4_BDE_LEN_MASK));

		bptr->u.data.low = sge[first_data_sge].buffer_address_low;
		bptr->u.data.high = sge[first_data_sge].buffer_address_high;
	}

	return  0;
}

int
sli_fcp_treceive64_wqe(struct sli4 *sli, void *buf, struct efc_dma *sgl,
		       u32 first_data_sge, u16 cq_id, u8 dif, u8 bs,
		       struct sli_fcp_tgt_params *params)
{
	struct sli4_fcp_treceive64_wqe *trecv = buf;
	struct sli4_fcp_128byte_wqe *trecv_128 = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;

	memset(buf, 0, sli->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}
	sge = sgl->virt;
	bptr = &trecv->bde;
	if (sli->params.sgl_pre_registered) {
		trecv->qosd_xbl_hlm_iod_dbde_wqes &= ~SLI4_TRCV_WQE_XBL;

		trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_DBDE;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (le32_to_cpu(sge[0].buffer_length)
					& SLI4_BDE_LEN_MASK));

		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;

		trecv->payload_offset_length = sge[0].buffer_length;
	} else {
		trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_XBL;

		/* if data is a single physical address, use a BDE */
		if (!dif &&
		    params->xmit_len <= le32_to_cpu(sge[2].buffer_length)) {
			trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_DBDE;
			bptr->bde_type_buflen =
			      cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
					  (le32_to_cpu(sge[2].buffer_length)
					  & SLI4_BDE_LEN_MASK));

			bptr->u.data.low = sge[2].buffer_address_low;
			bptr->u.data.high = sge[2].buffer_address_high;
		} else {
			bptr->bde_type_buflen =
				cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
				(sgl->size & SLI4_BDE_LEN_MASK));
			bptr->u.blp.low = cpu_to_le32(lower_32_bits(sgl->phys));
			bptr->u.blp.high =
				cpu_to_le32(upper_32_bits(sgl->phys));
		}
	}

	trecv->relative_offset = cpu_to_le32(params->offset);

	if (params->flags & SLI4_IO_CONTINUATION)
		trecv->eat_xc_ccpe |= SLI4_TRCV_WQE_XC;

	trecv->xri_tag = cpu_to_le16(params->xri);

	trecv->context_tag = cpu_to_le16(params->rpi);

	/* WQE uses relative offset */
	trecv->class_ar_pu_byte |= 1 << SLI4_TRCV_WQE_PU_SHFT;

	if (params->flags & SLI4_IO_AUTO_GOOD_RESPONSE)
		trecv->class_ar_pu_byte |= SLI4_TRCV_WQE_AR;

	trecv->command = SLI4_WQE_FCP_TRECEIVE64;
	trecv->class_ar_pu_byte |= SLI4_GENERIC_CLASS_CLASS_3;
	trecv->dif_ct_bs_byte |=
		SLI4_GENERIC_CONTEXT_RPI << SLI4_TRCV_WQE_CT_SHFT;
	trecv->dif_ct_bs_byte |= bs << SLI4_TRCV_WQE_BS_SHFT;

	trecv->remote_xid = cpu_to_le16(params->ox_id);

	trecv->request_tag = cpu_to_le16(params->tag);

	trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_IOD;

	trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_LEN_LOC_BIT2;

	trecv->cmd_type_byte |= SLI4_CMD_FCP_TRECEIVE64_WQE;

	trecv->cq_id = cpu_to_le16(cq_id);

	trecv->fcp_data_receive_length = cpu_to_le32(params->xmit_len);

	if (sli->params.perf_hint) {
		bptr = &trecv->first_data_bde;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (le32_to_cpu(sge[first_data_sge].buffer_length) &
			     SLI4_BDE_LEN_MASK));
		bptr->u.data.low = sge[first_data_sge].buffer_address_low;
		bptr->u.data.high = sge[first_data_sge].buffer_address_high;
	}

	/* The upper 7 bits of csctl is the priority */
	if (params->cs_ctl & SLI4_MASK_CCP) {
		trecv->eat_xc_ccpe |= SLI4_TRCV_WQE_CCPE;
		trecv->ccp = (params->cs_ctl & SLI4_MASK_CCP);
	}

	if (params->app_id && sli->wqe_size == SLI4_WQE_EXT_BYTES &&
	    !(trecv->eat_xc_ccpe & SLI4_TRSP_WQE_EAT)) {
		trecv->lloc1_appid |= SLI4_TRCV_WQE_APPID;
		trecv->qosd_xbl_hlm_iod_dbde_wqes |= SLI4_TRCV_WQE_WQES;
		trecv_128->dw[31] = params->app_id;
	}
	return 0;
}

int
sli_fcp_cont_treceive64_wqe(struct sli4 *sli, void *buf,
			    struct efc_dma *sgl, u32 first_data_sge,
			    u16 sec_xri, u16 cq_id, u8 dif, u8 bs,
			    struct sli_fcp_tgt_params *params)
{
	int rc;

	rc = sli_fcp_treceive64_wqe(sli, buf, sgl, first_data_sge,
				    cq_id, dif, bs, params);
	if (!rc) {
		struct sli4_fcp_treceive64_wqe *trecv = buf;

		trecv->command = SLI4_WQE_FCP_CONT_TRECEIVE64;
		trecv->dword5.sec_xri_tag = cpu_to_le16(sec_xri);
	}
	return rc;
}

int
sli_fcp_trsp64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		   u16 cq_id, u8 port_owned, struct sli_fcp_tgt_params *params)
{
	struct sli4_fcp_trsp64_wqe *trsp = buf;
	struct sli4_fcp_128byte_wqe *trsp_128 = buf;

	memset(buf, 0, sli4->wqe_size);

	if (params->flags & SLI4_IO_AUTO_GOOD_RESPONSE) {
		trsp->class_ag_byte |= SLI4_TRSP_WQE_AG;
	} else {
		struct sli4_sge	*sge = sgl->virt;
		struct sli4_bde *bptr;

		if (sli4->params.sgl_pre_registered || port_owned)
			trsp->qosd_xbl_hlm_dbde_wqes |= SLI4_TRSP_WQE_DBDE;
		else
			trsp->qosd_xbl_hlm_dbde_wqes |= SLI4_TRSP_WQE_XBL;
		bptr = &trsp->bde;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				     (le32_to_cpu(sge[0].buffer_length) &
				      SLI4_BDE_LEN_MASK));
		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;

		trsp->fcp_response_length = cpu_to_le32(params->xmit_len);
	}

	if (params->flags & SLI4_IO_CONTINUATION)
		trsp->eat_xc_ccpe |= SLI4_TRSP_WQE_XC;

	trsp->xri_tag = cpu_to_le16(params->xri);
	trsp->rpi = cpu_to_le16(params->rpi);

	trsp->command = SLI4_WQE_FCP_TRSP64;
	trsp->class_ag_byte |= SLI4_GENERIC_CLASS_CLASS_3;

	trsp->remote_xid = cpu_to_le16(params->ox_id);
	trsp->request_tag = cpu_to_le16(params->tag);
	if (params->flags & SLI4_IO_DNRX)
		trsp->ct_dnrx_byte |= SLI4_TRSP_WQE_DNRX;
	else
		trsp->ct_dnrx_byte &= ~SLI4_TRSP_WQE_DNRX;

	trsp->lloc1_appid |= 0x1;
	trsp->cq_id = cpu_to_le16(cq_id);
	trsp->cmd_type_byte = SLI4_CMD_FCP_TRSP64_WQE;

	/* The upper 7 bits of csctl is the priority */
	if (params->cs_ctl & SLI4_MASK_CCP) {
		trsp->eat_xc_ccpe |= SLI4_TRSP_WQE_CCPE;
		trsp->ccp = (params->cs_ctl & SLI4_MASK_CCP);
	}

	if (params->app_id && sli4->wqe_size == SLI4_WQE_EXT_BYTES &&
	    !(trsp->eat_xc_ccpe & SLI4_TRSP_WQE_EAT)) {
		trsp->lloc1_appid |= SLI4_TRSP_WQE_APPID;
		trsp->qosd_xbl_hlm_dbde_wqes |= SLI4_TRSP_WQE_WQES;
		trsp_128->dw[31] = params->app_id;
	}
	return 0;
}

int
sli_fcp_tsend64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		    u32 first_data_sge, u16 cq_id, u8 dif, u8 bs,
		    struct sli_fcp_tgt_params *params)
{
	struct sli4_fcp_tsend64_wqe *tsend = buf;
	struct sli4_fcp_128byte_wqe *tsend_128 = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;

	memset(buf, 0, sli4->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli4, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}
	sge = sgl->virt;

	bptr = &tsend->bde;
	if (sli4->params.sgl_pre_registered) {
		tsend->ll_qd_xbl_hlm_iod_dbde &= ~SLI4_TSEND_WQE_XBL;

		tsend->ll_qd_xbl_hlm_iod_dbde |= SLI4_TSEND_WQE_DBDE;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				   (le32_to_cpu(sge[2].buffer_length) &
				    SLI4_BDE_LEN_MASK));

		/* TSEND64_WQE specifies first two SGE are skipped (3rd is
		 * valid)
		 */
		bptr->u.data.low  = sge[2].buffer_address_low;
		bptr->u.data.high = sge[2].buffer_address_high;
	} else {
		tsend->ll_qd_xbl_hlm_iod_dbde |= SLI4_TSEND_WQE_XBL;

		/* if data is a single physical address, use a BDE */
		if (!dif &&
		    params->xmit_len <= le32_to_cpu(sge[2].buffer_length)) {
			tsend->ll_qd_xbl_hlm_iod_dbde |= SLI4_TSEND_WQE_DBDE;

			bptr->bde_type_buflen =
			    cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
					(le32_to_cpu(sge[2].buffer_length) &
					SLI4_BDE_LEN_MASK));
			/*
			 * TSEND64_WQE specifies first two SGE are skipped
			 * (i.e. 3rd is valid)
			 */
			bptr->u.data.low =
				sge[2].buffer_address_low;
			bptr->u.data.high =
				sge[2].buffer_address_high;
		} else {
			bptr->bde_type_buflen =
				cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
					    (sgl->size &
					     SLI4_BDE_LEN_MASK));
			bptr->u.blp.low =
				cpu_to_le32(lower_32_bits(sgl->phys));
			bptr->u.blp.high =
				cpu_to_le32(upper_32_bits(sgl->phys));
		}
	}

	tsend->relative_offset = cpu_to_le32(params->offset);

	if (params->flags & SLI4_IO_CONTINUATION)
		tsend->dw10byte2 |= SLI4_TSEND_XC;

	tsend->xri_tag = cpu_to_le16(params->xri);

	tsend->rpi = cpu_to_le16(params->rpi);
	/* WQE uses relative offset */
	tsend->class_pu_ar_byte |= 1 << SLI4_TSEND_WQE_PU_SHFT;

	if (params->flags & SLI4_IO_AUTO_GOOD_RESPONSE)
		tsend->class_pu_ar_byte |= SLI4_TSEND_WQE_AR;

	tsend->command = SLI4_WQE_FCP_TSEND64;
	tsend->class_pu_ar_byte |= SLI4_GENERIC_CLASS_CLASS_3;
	tsend->ct_byte |= SLI4_GENERIC_CONTEXT_RPI << SLI4_TSEND_CT_SHFT;
	tsend->ct_byte |= dif;
	tsend->ct_byte |= bs << SLI4_TSEND_BS_SHFT;

	tsend->remote_xid = cpu_to_le16(params->ox_id);

	tsend->request_tag = cpu_to_le16(params->tag);

	tsend->ll_qd_xbl_hlm_iod_dbde |= SLI4_TSEND_LEN_LOC_BIT2;

	tsend->cq_id = cpu_to_le16(cq_id);

	tsend->cmd_type_byte |= SLI4_CMD_FCP_TSEND64_WQE;

	tsend->fcp_data_transmit_length = cpu_to_le32(params->xmit_len);

	if (sli4->params.perf_hint) {
		bptr = &tsend->first_data_bde;
		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (le32_to_cpu(sge[first_data_sge].buffer_length) &
			     SLI4_BDE_LEN_MASK));
		bptr->u.data.low =
			sge[first_data_sge].buffer_address_low;
		bptr->u.data.high =
			sge[first_data_sge].buffer_address_high;
	}

	/* The upper 7 bits of csctl is the priority */
	if (params->cs_ctl & SLI4_MASK_CCP) {
		tsend->dw10byte2 |= SLI4_TSEND_CCPE;
		tsend->ccp = (params->cs_ctl & SLI4_MASK_CCP);
	}

	if (params->app_id && sli4->wqe_size == SLI4_WQE_EXT_BYTES &&
	    !(tsend->dw10byte2 & SLI4_TSEND_EAT)) {
		tsend->dw10byte0 |= SLI4_TSEND_APPID_VALID;
		tsend->ll_qd_xbl_hlm_iod_dbde |= SLI4_TSEND_WQES;
		tsend_128->dw[31] = params->app_id;
	}
	return 0;
}

int
sli_gen_request64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *sgl,
		      struct sli_ct_params *params)
{
	struct sli4_gen_request64_wqe *gen = buf;
	struct sli4_sge *sge = NULL;
	struct sli4_bde *bptr;

	memset(buf, 0, sli4->wqe_size);

	if (!sgl || !sgl->virt) {
		efc_log_err(sli4, "bad parameter sgl=%p virt=%p\n",
			    sgl, sgl ? sgl->virt : NULL);
		return -EIO;
	}
	sge = sgl->virt;
	bptr = &gen->bde;

	if (sli4->params.sgl_pre_registered) {
		gen->dw10flags1 &= ~SLI4_GEN_REQ64_WQE_XBL;

		gen->dw10flags1 |= SLI4_GEN_REQ64_WQE_DBDE;
		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (params->xmit_len & SLI4_BDE_LEN_MASK));

		bptr->u.data.low  = sge[0].buffer_address_low;
		bptr->u.data.high = sge[0].buffer_address_high;
	} else {
		gen->dw10flags1 |= SLI4_GEN_REQ64_WQE_XBL;

		bptr->bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(BLP)) |
				    ((2 * sizeof(struct sli4_sge)) &
				     SLI4_BDE_LEN_MASK));

		bptr->u.blp.low =
			cpu_to_le32(lower_32_bits(sgl->phys));
		bptr->u.blp.high =
			cpu_to_le32(upper_32_bits(sgl->phys));
	}

	gen->request_payload_length = cpu_to_le32(params->xmit_len);
	gen->max_response_payload_length = cpu_to_le32(params->rsp_len);

	gen->df_ctl = params->df_ctl;
	gen->type = params->type;
	gen->r_ctl = params->r_ctl;

	gen->xri_tag = cpu_to_le16(params->xri);

	gen->ct_byte = SLI4_GENERIC_CONTEXT_RPI << SLI4_GEN_REQ64_CT_SHFT;
	gen->context_tag = cpu_to_le16(params->rpi);

	gen->class_byte = SLI4_GENERIC_CLASS_CLASS_3;

	gen->command = SLI4_WQE_GEN_REQUEST64;

	gen->timer = params->timeout;

	gen->request_tag = cpu_to_le16(params->tag);

	gen->dw10flags1 |= SLI4_GEN_REQ64_WQE_IOD;

	gen->dw10flags0 |= SLI4_GEN_REQ64_WQE_QOSD;

	gen->cmd_type_byte = SLI4_CMD_GEN_REQUEST64_WQE;

	gen->cq_id = cpu_to_le16(SLI4_CQ_DEFAULT);

	return 0;
}

int
sli_send_frame_wqe(struct sli4 *sli, void *buf, u8 sof, u8 eof, u32 *hdr,
		   struct efc_dma *payload, u32 req_len, u8 timeout, u16 xri,
		   u16 req_tag)
{
	struct sli4_send_frame_wqe *sf = buf;

	memset(buf, 0, sli->wqe_size);

	sf->dw10flags1 |= SLI4_SF_WQE_DBDE;
	sf->bde.bde_type_buflen = cpu_to_le32(req_len &
					      SLI4_BDE_LEN_MASK);
	sf->bde.u.data.low = cpu_to_le32(lower_32_bits(payload->phys));
	sf->bde.u.data.high = cpu_to_le32(upper_32_bits(payload->phys));

	/* Copy FC header */
	sf->fc_header_0_1[0] = cpu_to_le32(hdr[0]);
	sf->fc_header_0_1[1] = cpu_to_le32(hdr[1]);
	sf->fc_header_2_5[0] = cpu_to_le32(hdr[2]);
	sf->fc_header_2_5[1] = cpu_to_le32(hdr[3]);
	sf->fc_header_2_5[2] = cpu_to_le32(hdr[4]);
	sf->fc_header_2_5[3] = cpu_to_le32(hdr[5]);

	sf->frame_length = cpu_to_le32(req_len);

	sf->xri_tag = cpu_to_le16(xri);
	sf->dw7flags0 &= ~SLI4_SF_PU;
	sf->context_tag = 0;

	sf->ct_byte &= ~SLI4_SF_CT;
	sf->command = SLI4_WQE_SEND_FRAME;
	sf->dw7flags0 |= SLI4_GENERIC_CLASS_CLASS_3;
	sf->timer = timeout;

	sf->request_tag = cpu_to_le16(req_tag);
	sf->eof = eof;
	sf->sof = sof;

	sf->dw10flags1 &= ~SLI4_SF_QOSD;
	sf->dw10flags0 |= SLI4_SF_LEN_LOC_BIT1;
	sf->dw10flags2 &= ~SLI4_SF_XC;

	sf->dw10flags1 |= SLI4_SF_XBL;

	sf->cmd_type_byte |= SLI4_CMD_SEND_FRAME_WQE;
	sf->cq_id = cpu_to_le16(0xffff);

	return 0;
}

int
sli_xmit_bls_rsp64_wqe(struct sli4 *sli, void *buf,
		       struct sli_bls_payload *payload,
		       struct sli_bls_params *params)
{
	struct sli4_xmit_bls_rsp_wqe *bls = buf;
	u32 dw_ridflags = 0;

	/*
	 * Callers can either specify RPI or S_ID, but not both
	 */
	if (params->rpi_registered && params->s_id != U32_MAX) {
		efc_log_info(sli, "S_ID specified for attached remote node %d\n",
			     params->rpi);
		return -EIO;
	}

	memset(buf, 0, sli->wqe_size);

	if (payload->type == SLI4_SLI_BLS_ACC) {
		bls->payload_word0 =
			cpu_to_le32((payload->u.acc.seq_id_last << 16) |
				    (payload->u.acc.seq_id_validity << 24));
		bls->high_seq_cnt = payload->u.acc.high_seq_cnt;
		bls->low_seq_cnt = payload->u.acc.low_seq_cnt;
	} else if (payload->type == SLI4_SLI_BLS_RJT) {
		bls->payload_word0 =
				cpu_to_le32(*((u32 *)&payload->u.rjt));
		dw_ridflags |= SLI4_BLS_RSP_WQE_AR;
	} else {
		efc_log_info(sli, "bad BLS type %#x\n", payload->type);
		return -EIO;
	}

	bls->ox_id = payload->ox_id;
	bls->rx_id = payload->rx_id;

	if (params->rpi_registered) {
		bls->dw8flags0 |=
		SLI4_GENERIC_CONTEXT_RPI << SLI4_BLS_RSP_WQE_CT_SHFT;
		bls->context_tag = cpu_to_le16(params->rpi);
	} else {
		bls->dw8flags0 |=
		SLI4_GENERIC_CONTEXT_VPI << SLI4_BLS_RSP_WQE_CT_SHFT;
		bls->context_tag = cpu_to_le16(params->vpi);

		if (params->s_id != U32_MAX)
			bls->local_n_port_id_dword |=
				cpu_to_le32(params->s_id & 0x00ffffff);
		else
			bls->local_n_port_id_dword |=
				cpu_to_le32(params->s_id & 0x00ffffff);

		dw_ridflags = (dw_ridflags & ~SLI4_BLS_RSP_RID) |
			       (params->d_id & SLI4_BLS_RSP_RID);

		bls->temporary_rpi = cpu_to_le16(params->rpi);
	}

	bls->xri_tag = cpu_to_le16(params->xri);

	bls->dw8flags1 |= SLI4_GENERIC_CLASS_CLASS_3;

	bls->command = SLI4_WQE_XMIT_BLS_RSP;

	bls->request_tag = cpu_to_le16(params->tag);

	bls->dw11flags1 |= SLI4_BLS_RSP_WQE_QOSD;

	bls->remote_id_dword = cpu_to_le32(dw_ridflags);
	bls->cq_id = cpu_to_le16(SLI4_CQ_DEFAULT);

	bls->dw12flags0 |= SLI4_CMD_XMIT_BLS_RSP64_WQE;

	return 0;
}

int
sli_xmit_els_rsp64_wqe(struct sli4 *sli, void *buf, struct efc_dma *rsp,
		       struct sli_els_params *params)
{
	struct sli4_xmit_els_rsp64_wqe *els = buf;

	memset(buf, 0, sli->wqe_size);

	if (sli->params.sgl_pre_registered)
		els->flags2 |= SLI4_ELS_DBDE;
	else
		els->flags2 |= SLI4_ELS_XBL;

	els->els_response_payload.bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (params->rsp_len & SLI4_BDE_LEN_MASK));
	els->els_response_payload.u.data.low =
		cpu_to_le32(lower_32_bits(rsp->phys));
	els->els_response_payload.u.data.high =
		cpu_to_le32(upper_32_bits(rsp->phys));

	els->els_response_payload_length = cpu_to_le32(params->rsp_len);

	els->xri_tag = cpu_to_le16(params->xri);

	els->class_byte |= SLI4_GENERIC_CLASS_CLASS_3;

	els->command = SLI4_WQE_ELS_RSP64;

	els->request_tag = cpu_to_le16(params->tag);

	els->ox_id = cpu_to_le16(params->ox_id);

	els->flags2 |= SLI4_ELS_IOD & SLI4_ELS_REQUEST64_DIR_WRITE;

	els->flags2 |= SLI4_ELS_QOSD;

	els->cmd_type_wqec = SLI4_ELS_REQUEST64_CMD_GEN;

	els->cq_id = cpu_to_le16(SLI4_CQ_DEFAULT);

	if (params->rpi_registered) {
		els->ct_byte |=
			SLI4_GENERIC_CONTEXT_RPI << SLI4_ELS_CT_OFFSET;
		els->context_tag = cpu_to_le16(params->rpi);
		return 0;
	}

	els->ct_byte |= SLI4_GENERIC_CONTEXT_VPI << SLI4_ELS_CT_OFFSET;
	els->context_tag = cpu_to_le16(params->vpi);
	els->rid_dw = cpu_to_le32(params->d_id & SLI4_ELS_RID);
	els->temporary_rpi = cpu_to_le16(params->rpi);
	if (params->s_id != U32_MAX) {
		els->sid_dw |=
		      cpu_to_le32(SLI4_ELS_SP | (params->s_id & SLI4_ELS_SID));
	}

	return 0;
}

int
sli_xmit_sequence64_wqe(struct sli4 *sli4, void *buf, struct efc_dma *payload,
			struct sli_ct_params *params)
{
	struct sli4_xmit_sequence64_wqe *xmit = buf;

	memset(buf, 0, sli4->wqe_size);

	if (!payload || !payload->virt) {
		efc_log_err(sli4, "bad parameter sgl=%p virt=%p\n",
			    payload, payload ? payload->virt : NULL);
		return -EIO;
	}

	if (sli4->params.sgl_pre_registered)
		xmit->dw10w0 |= cpu_to_le16(SLI4_SEQ_WQE_DBDE);
	else
		xmit->dw10w0 |= cpu_to_le16(SLI4_SEQ_WQE_XBL);

	xmit->bde.bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			(params->rsp_len & SLI4_BDE_LEN_MASK));
	xmit->bde.u.data.low  =
			cpu_to_le32(lower_32_bits(payload->phys));
	xmit->bde.u.data.high =
			cpu_to_le32(upper_32_bits(payload->phys));
	xmit->sequence_payload_len = cpu_to_le32(params->rsp_len);

	xmit->remote_n_port_id_dword |= cpu_to_le32(params->d_id & 0x00ffffff);

	xmit->relative_offset = 0;

	/* sequence initiative - this matches what is seen from
	 * FC switches in response to FCGS commands
	 */
	xmit->dw5flags0 &= (~SLI4_SEQ_WQE_SI);
	xmit->dw5flags0 &= (~SLI4_SEQ_WQE_FT);/* force transmit */
	xmit->dw5flags0 &= (~SLI4_SEQ_WQE_XO);/* exchange responder */
	xmit->dw5flags0 |= SLI4_SEQ_WQE_LS;/* last in seqence */
	xmit->df_ctl = params->df_ctl;
	xmit->type = params->type;
	xmit->r_ctl = params->r_ctl;

	xmit->xri_tag = cpu_to_le16(params->xri);
	xmit->context_tag = cpu_to_le16(params->rpi);

	xmit->dw7flags0 &= ~SLI4_SEQ_WQE_DIF;
	xmit->dw7flags0 |=
		SLI4_GENERIC_CONTEXT_RPI << SLI4_SEQ_WQE_CT_SHIFT;
	xmit->dw7flags0 &= ~SLI4_SEQ_WQE_BS;

	xmit->command = SLI4_WQE_XMIT_SEQUENCE64;
	xmit->dw7flags1 |= SLI4_GENERIC_CLASS_CLASS_3;
	xmit->dw7flags1 &= ~SLI4_SEQ_WQE_PU;
	xmit->timer = params->timeout;

	xmit->abort_tag = 0;
	xmit->request_tag = cpu_to_le16(params->tag);
	xmit->remote_xid = cpu_to_le16(params->ox_id);

	xmit->dw10w0 |=
	cpu_to_le16(SLI4_ELS_REQUEST64_DIR_READ << SLI4_SEQ_WQE_IOD_SHIFT);

	xmit->cmd_type_wqec_byte |= SLI4_CMD_XMIT_SEQUENCE64_WQE;

	xmit->dw10w0 |= cpu_to_le16(2 << SLI4_SEQ_WQE_LEN_LOC_SHIFT);

	xmit->cq_id = cpu_to_le16(0xFFFF);

	return 0;
}

int
sli_requeue_xri_wqe(struct sli4 *sli4, void *buf, u16 xri, u16 tag, u16 cq_id)
{
	struct sli4_requeue_xri_wqe *requeue = buf;

	memset(buf, 0, sli4->wqe_size);

	requeue->command = SLI4_WQE_REQUEUE_XRI;
	requeue->xri_tag = cpu_to_le16(xri);
	requeue->request_tag = cpu_to_le16(tag);
	requeue->flags2 |= cpu_to_le16(SLI4_REQU_XRI_WQE_XC);
	requeue->flags1 |= cpu_to_le16(SLI4_REQU_XRI_WQE_QOSD);
	requeue->cq_id = cpu_to_le16(cq_id);
	requeue->cmd_type_wqec_byte = SLI4_CMD_REQUEUE_XRI_WQE;
	return 0;
}

int
sli_fc_process_link_attention(struct sli4 *sli4, void *acqe)
{
	struct sli4_link_attention *link_attn = acqe;
	struct sli4_link_event event = { 0 };

	efc_log_info(sli4, "link=%d attn_type=%#x top=%#x speed=%#x pfault=%#x\n",
		     link_attn->link_number, link_attn->attn_type,
		     link_attn->topology, link_attn->port_speed,
		     link_attn->port_fault);
	efc_log_info(sli4, "shared_lnk_status=%#x logl_lnk_speed=%#x evttag=%#x\n",
		     link_attn->shared_link_status,
		     le16_to_cpu(link_attn->logical_link_speed),
		     le32_to_cpu(link_attn->event_tag));

	if (!sli4->link)
		return -EIO;

	event.medium   = SLI4_LINK_MEDIUM_FC;

	switch (link_attn->attn_type) {
	case SLI4_LNK_ATTN_TYPE_LINK_UP:
		event.status = SLI4_LINK_STATUS_UP;
		break;
	case SLI4_LNK_ATTN_TYPE_LINK_DOWN:
		event.status = SLI4_LINK_STATUS_DOWN;
		break;
	case SLI4_LNK_ATTN_TYPE_NO_HARD_ALPA:
		efc_log_info(sli4, "attn_type: no hard alpa\n");
		event.status = SLI4_LINK_STATUS_NO_ALPA;
		break;
	default:
		efc_log_info(sli4, "attn_type: unknown\n");
		break;
	}

	switch (link_attn->event_type) {
	case SLI4_EVENT_LINK_ATTENTION:
		break;
	case SLI4_EVENT_SHARED_LINK_ATTENTION:
		efc_log_info(sli4, "event_type: FC shared link event\n");
		break;
	default:
		efc_log_info(sli4, "event_type: unknown\n");
		break;
	}

	switch (link_attn->topology) {
	case SLI4_LNK_ATTN_P2P:
		event.topology = SLI4_LINK_TOPO_NON_FC_AL;
		break;
	case SLI4_LNK_ATTN_FC_AL:
		event.topology = SLI4_LINK_TOPO_FC_AL;
		break;
	case SLI4_LNK_ATTN_INTERNAL_LOOPBACK:
		efc_log_info(sli4, "topology Internal loopback\n");
		event.topology = SLI4_LINK_TOPO_LOOPBACK_INTERNAL;
		break;
	case SLI4_LNK_ATTN_SERDES_LOOPBACK:
		efc_log_info(sli4, "topology serdes loopback\n");
		event.topology = SLI4_LINK_TOPO_LOOPBACK_EXTERNAL;
		break;
	default:
		efc_log_info(sli4, "topology: unknown\n");
		break;
	}

	event.speed = link_attn->port_speed * 1000;

	sli4->link(sli4->link_arg, (void *)&event);

	return 0;
}

int
sli_fc_cqe_parse(struct sli4 *sli4, struct sli4_queue *cq,
		 u8 *cqe, enum sli4_qentry *etype, u16 *r_id)
{
	u8 code = cqe[SLI4_CQE_CODE_OFFSET];
	int rc;

	switch (code) {
	case SLI4_CQE_CODE_WORK_REQUEST_COMPLETION:
	{
		struct sli4_fc_wcqe *wcqe = (void *)cqe;

		*etype = SLI4_QENTRY_WQ;
		*r_id = le16_to_cpu(wcqe->request_tag);
		rc = wcqe->status;

		/* Flag errors except for FCP_RSP_FAILURE */
		if (rc && rc != SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE) {
			efc_log_info(sli4, "WCQE: status=%#x hw_status=%#x tag=%#x\n",
				     wcqe->status, wcqe->hw_status,
				     le16_to_cpu(wcqe->request_tag));
			efc_log_info(sli4, "w1=%#x w2=%#x xb=%d\n",
				     le32_to_cpu(wcqe->wqe_specific_1),
				     le32_to_cpu(wcqe->wqe_specific_2),
				     (wcqe->flags & SLI4_WCQE_XB));
			efc_log_info(sli4, "      %08X %08X %08X %08X\n",
				     ((u32 *)cqe)[0], ((u32 *)cqe)[1],
				     ((u32 *)cqe)[2], ((u32 *)cqe)[3]);
		}

		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC:
	{
		struct sli4_fc_async_rcqe *rcqe = (void *)cqe;

		*etype = SLI4_QENTRY_RQ;
		*r_id = le16_to_cpu(rcqe->fcfi_rq_id_word) & SLI4_RACQE_RQ_ID;
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_RQ_ASYNC_V1:
	{
		struct sli4_fc_async_rcqe_v1 *rcqe = (void *)cqe;

		*etype = SLI4_QENTRY_RQ;
		*r_id = le16_to_cpu(rcqe->rq_id);
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD:
	{
		struct sli4_fc_optimized_write_cmd_cqe *optcqe = (void *)cqe;

		*etype = SLI4_QENTRY_OPT_WRITE_CMD;
		*r_id = le16_to_cpu(optcqe->rq_id);
		rc = optcqe->status;
		break;
	}
	case SLI4_CQE_CODE_OPTIMIZED_WRITE_DATA:
	{
		struct sli4_fc_optimized_write_data_cqe *dcqe = (void *)cqe;

		*etype = SLI4_QENTRY_OPT_WRITE_DATA;
		*r_id = le16_to_cpu(dcqe->xri);
		rc = dcqe->status;

		/* Flag errors */
		if (rc != SLI4_FC_WCQE_STATUS_SUCCESS) {
			efc_log_info(sli4, "Optimized DATA CQE: status=%#x\n",
				     dcqe->status);
			efc_log_info(sli4, "hstat=%#x xri=%#x dpl=%#x w3=%#x xb=%d\n",
				     dcqe->hw_status, le16_to_cpu(dcqe->xri),
				     le32_to_cpu(dcqe->total_data_placed),
				     ((u32 *)cqe)[3],
				     (dcqe->flags & SLI4_OCQE_XB));
		}
		break;
	}
	case SLI4_CQE_CODE_RQ_COALESCING:
	{
		struct sli4_fc_coalescing_rcqe *rcqe = (void *)cqe;

		*etype = SLI4_QENTRY_RQ;
		*r_id = le16_to_cpu(rcqe->rq_id);
		rc = rcqe->status;
		break;
	}
	case SLI4_CQE_CODE_XRI_ABORTED:
	{
		struct sli4_fc_xri_aborted_cqe *xa = (void *)cqe;

		*etype = SLI4_QENTRY_XABT;
		*r_id = le16_to_cpu(xa->xri);
		rc = 0;
		break;
	}
	case SLI4_CQE_CODE_RELEASE_WQE:
	{
		struct sli4_fc_wqec *wqec = (void *)cqe;

		*etype = SLI4_QENTRY_WQ_RELEASE;
		*r_id = le16_to_cpu(wqec->wq_id);
		rc = 0;
		break;
	}
	default:
		efc_log_info(sli4, "CQE completion code %d not handled\n",
			     code);
		*etype = SLI4_QENTRY_MAX;
		*r_id = U16_MAX;
		rc = -EINVAL;
	}

	return rc;
}

u32
sli_fc_response_length(struct sli4 *sli4, u8 *cqe)
{
	struct sli4_fc_wcqe *wcqe = (void *)cqe;

	return le32_to_cpu(wcqe->wqe_specific_1);
}

u32
sli_fc_io_length(struct sli4 *sli4, u8 *cqe)
{
	struct sli4_fc_wcqe *wcqe = (void *)cqe;

	return le32_to_cpu(wcqe->wqe_specific_1);
}

int
sli_fc_els_did(struct sli4 *sli4, u8 *cqe, u32 *d_id)
{
	struct sli4_fc_wcqe *wcqe = (void *)cqe;

	*d_id = 0;

	if (wcqe->status)
		return -EIO;
	*d_id = le32_to_cpu(wcqe->wqe_specific_2) & 0x00ffffff;
	return 0;
}

u32
sli_fc_ext_status(struct sli4 *sli4, u8 *cqe)
{
	struct sli4_fc_wcqe *wcqe = (void *)cqe;
	u32	mask;

	switch (wcqe->status) {
	case SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE:
		mask = U32_MAX;
		break;
	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
	case SLI4_FC_WCQE_STATUS_CMD_REJECT:
		mask = 0xff;
		break;
	case SLI4_FC_WCQE_STATUS_NPORT_RJT:
	case SLI4_FC_WCQE_STATUS_FABRIC_RJT:
	case SLI4_FC_WCQE_STATUS_NPORT_BSY:
	case SLI4_FC_WCQE_STATUS_FABRIC_BSY:
	case SLI4_FC_WCQE_STATUS_LS_RJT:
		mask = U32_MAX;
		break;
	case SLI4_FC_WCQE_STATUS_DI_ERROR:
		mask = U32_MAX;
		break;
	default:
		mask = 0;
	}

	return le32_to_cpu(wcqe->wqe_specific_2) & mask;
}

int
sli_fc_rqe_rqid_and_index(struct sli4 *sli4, u8 *cqe, u16 *rq_id, u32 *index)
{
	int rc = -EIO;
	u8 code = 0;
	u16 rq_element_index;

	*rq_id = 0;
	*index = U32_MAX;

	code = cqe[SLI4_CQE_CODE_OFFSET];

	/* Retrieve the RQ index from the completion */
	if (code == SLI4_CQE_CODE_RQ_ASYNC) {
		struct sli4_fc_async_rcqe *rcqe = (void *)cqe;

		*rq_id = le16_to_cpu(rcqe->fcfi_rq_id_word) & SLI4_RACQE_RQ_ID;
		rq_element_index =
		le16_to_cpu(rcqe->rq_elmt_indx_word) & SLI4_RACQE_RQ_EL_INDX;
		*index = rq_element_index;
		if (rcqe->status == SLI4_FC_ASYNC_RQ_SUCCESS) {
			rc = 0;
		} else {
			rc = rcqe->status;
			efc_log_info(sli4, "status=%02x (%s) rq_id=%d\n",
				     rcqe->status,
				     sli_fc_get_status_string(rcqe->status),
				     le16_to_cpu(rcqe->fcfi_rq_id_word) &
				     SLI4_RACQE_RQ_ID);

			efc_log_info(sli4, "pdpl=%x sof=%02x eof=%02x hdpl=%x\n",
				     le16_to_cpu(rcqe->data_placement_length),
				     rcqe->sof_byte, rcqe->eof_byte,
				     rcqe->hdpl_byte & SLI4_RACQE_HDPL);
		}
	} else if (code == SLI4_CQE_CODE_RQ_ASYNC_V1) {
		struct sli4_fc_async_rcqe_v1 *rcqe_v1 = (void *)cqe;

		*rq_id = le16_to_cpu(rcqe_v1->rq_id);
		rq_element_index =
			(le16_to_cpu(rcqe_v1->rq_elmt_indx_word) &
			 SLI4_RACQE_RQ_EL_INDX);
		*index = rq_element_index;
		if (rcqe_v1->status == SLI4_FC_ASYNC_RQ_SUCCESS) {
			rc = 0;
		} else {
			rc = rcqe_v1->status;
			efc_log_info(sli4, "status=%02x (%s) rq_id=%d, index=%x\n",
				     rcqe_v1->status,
				     sli_fc_get_status_string(rcqe_v1->status),
				     le16_to_cpu(rcqe_v1->rq_id), rq_element_index);

			efc_log_info(sli4, "pdpl=%x sof=%02x eof=%02x hdpl=%x\n",
				     le16_to_cpu(rcqe_v1->data_placement_length),
			rcqe_v1->sof_byte, rcqe_v1->eof_byte,
			rcqe_v1->hdpl_byte & SLI4_RACQE_HDPL);
		}
	} else if (code == SLI4_CQE_CODE_OPTIMIZED_WRITE_CMD) {
		struct sli4_fc_optimized_write_cmd_cqe *optcqe = (void *)cqe;

		*rq_id = le16_to_cpu(optcqe->rq_id);
		*index = le16_to_cpu(optcqe->w1) & SLI4_OCQE_RQ_EL_INDX;
		if (optcqe->status == SLI4_FC_ASYNC_RQ_SUCCESS) {
			rc = 0;
		} else {
			rc = optcqe->status;
			efc_log_info(sli4, "stat=%02x (%s) rqid=%d, idx=%x pdpl=%x\n",
				     optcqe->status,
				     sli_fc_get_status_string(optcqe->status),
				     le16_to_cpu(optcqe->rq_id), *index,
				     le16_to_cpu(optcqe->data_placement_length));

			efc_log_info(sli4, "hdpl=%x oox=%d agxr=%d xri=0x%x rpi=%x\n",
				     (optcqe->hdpl_vld & SLI4_OCQE_HDPL),
				     (optcqe->flags1 & SLI4_OCQE_OOX),
				     (optcqe->flags1 & SLI4_OCQE_AGXR),
				     optcqe->xri, le16_to_cpu(optcqe->rpi));
		}
	} else if (code == SLI4_CQE_CODE_RQ_COALESCING) {
		struct sli4_fc_coalescing_rcqe  *rcqe = (void *)cqe;

		rq_element_index = (le16_to_cpu(rcqe->rq_elmt_indx_word) &
				    SLI4_RCQE_RQ_EL_INDX);

		*rq_id = le16_to_cpu(rcqe->rq_id);
		if (rcqe->status == SLI4_FC_COALESCE_RQ_SUCCESS) {
			*index = rq_element_index;
			rc = 0;
		} else {
			*index = U32_MAX;
			rc = rcqe->status;

			efc_log_info(sli4, "stat=%02x (%s) rq_id=%d, idx=%x\n",
				     rcqe->status,
				     sli_fc_get_status_string(rcqe->status),
				     le16_to_cpu(rcqe->rq_id), rq_element_index);
			efc_log_info(sli4, "rq_id=%#x sdpl=%x\n",
				     le16_to_cpu(rcqe->rq_id),
				     le16_to_cpu(rcqe->seq_placement_length));
		}
	} else {
		struct sli4_fc_async_rcqe *rcqe = (void *)cqe;

		*index = U32_MAX;
		rc = rcqe->status;

		efc_log_info(sli4, "status=%02x rq_id=%d, index=%x pdpl=%x\n",
			     rcqe->status,
			     le16_to_cpu(rcqe->fcfi_rq_id_word) & SLI4_RACQE_RQ_ID,
			     (le16_to_cpu(rcqe->rq_elmt_indx_word) & SLI4_RACQE_RQ_EL_INDX),
			     le16_to_cpu(rcqe->data_placement_length));
		efc_log_info(sli4, "sof=%02x eof=%02x hdpl=%x\n",
			     rcqe->sof_byte, rcqe->eof_byte,
			     rcqe->hdpl_byte & SLI4_RACQE_HDPL);
	}

	return rc;
}

static int
sli_bmbx_wait(struct sli4 *sli4, u32 msec)
{
	u32 val;
	unsigned long end;

	/* Wait for the bootstrap mailbox to report "ready" */
	end = jiffies + msecs_to_jiffies(msec);
	do {
		val = readl(sli4->reg[0] + SLI4_BMBX_REG);
		if (val & SLI4_BMBX_RDY)
			return 0;

		usleep_range(1000, 2000);
	} while (time_before(jiffies, end));

	return -EIO;
}

static int
sli_bmbx_write(struct sli4 *sli4)
{
	u32 val;

	/* write buffer location to bootstrap mailbox register */
	val = sli_bmbx_write_hi(sli4->bmbx.phys);
	writel(val, (sli4->reg[0] + SLI4_BMBX_REG));

	if (sli_bmbx_wait(sli4, SLI4_BMBX_DELAY_US)) {
		efc_log_crit(sli4, "BMBX WRITE_HI failed\n");
		return -EIO;
	}
	val = sli_bmbx_write_lo(sli4->bmbx.phys);
	writel(val, (sli4->reg[0] + SLI4_BMBX_REG));

	/* wait for SLI Port to set ready bit */
	return sli_bmbx_wait(sli4, SLI4_BMBX_TIMEOUT_MSEC);
}

int
sli_bmbx_command(struct sli4 *sli4)
{
	void *cqe = (u8 *)sli4->bmbx.virt + SLI4_BMBX_SIZE;

	if (sli_fw_error_status(sli4) > 0) {
		efc_log_crit(sli4, "Chip is in an error state -Mailbox command rejected");
		efc_log_crit(sli4, " status=%#x error1=%#x error2=%#x\n",
			     sli_reg_read_status(sli4),
			     sli_reg_read_err1(sli4),
			     sli_reg_read_err2(sli4));
		return -EIO;
	}

	/* Submit a command to the bootstrap mailbox and check the status */
	if (sli_bmbx_write(sli4)) {
		efc_log_crit(sli4, "bmbx write fail phys=%pad reg=%#x\n",
			     &sli4->bmbx.phys, readl(sli4->reg[0] + SLI4_BMBX_REG));
		return -EIO;
	}

	/* check completion queue entry status */
	if (le32_to_cpu(((struct sli4_mcqe *)cqe)->dw3_flags) &
	    SLI4_MCQE_VALID) {
		return sli_cqe_mq(sli4, cqe);
	}
	efc_log_crit(sli4, "invalid or wrong type\n");
	return -EIO;
}

int
sli_cmd_config_link(struct sli4 *sli4, void *buf)
{
	struct sli4_cmd_config_link *config_link = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	config_link->hdr.command = SLI4_MBX_CMD_CONFIG_LINK;

	/* Port interprets zero in a field as "use default value" */

	return 0;
}

int
sli_cmd_down_link(struct sli4 *sli4, void *buf)
{
	struct sli4_mbox_command_header *hdr = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	hdr->command = SLI4_MBX_CMD_DOWN_LINK;

	/* Port interprets zero in a field as "use default value" */

	return 0;
}

int
sli_cmd_dump_type4(struct sli4 *sli4, void *buf, u16 wki)
{
	struct sli4_cmd_dump4 *cmd = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	cmd->hdr.command = SLI4_MBX_CMD_DUMP;
	cmd->type_dword = cpu_to_le32(0x4);
	cmd->wki_selection = cpu_to_le16(wki);
	return 0;
}

int
sli_cmd_common_read_transceiver_data(struct sli4 *sli4, void *buf, u32 page_num,
				     struct efc_dma *dma)
{
	struct sli4_rqst_cmn_read_transceiver_data *req = NULL;
	u32 psize;

	if (!dma)
		psize = SLI4_CFG_PYLD_LENGTH(cmn_read_transceiver_data);
	else
		psize = dma->size;

	req = sli_config_cmd_init(sli4, buf, psize, dma);
	if (!req)
		return -EIO;

	sli_cmd_fill_hdr(&req->hdr, SLI4_CMN_READ_TRANS_DATA,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_read_transceiver_data));

	req->page_number = cpu_to_le32(page_num);
	req->port = cpu_to_le32(sli4->port_number);

	return 0;
}

int
sli_cmd_read_link_stats(struct sli4 *sli4, void *buf, u8 req_ext_counters,
			u8 clear_overflow_flags,
			u8 clear_all_counters)
{
	struct sli4_cmd_read_link_stats *cmd = buf;
	u32 flags;

	memset(buf, 0, SLI4_BMBX_SIZE);

	cmd->hdr.command = SLI4_MBX_CMD_READ_LNK_STAT;

	flags = 0;
	if (req_ext_counters)
		flags |= SLI4_READ_LNKSTAT_REC;
	if (clear_all_counters)
		flags |= SLI4_READ_LNKSTAT_CLRC;
	if (clear_overflow_flags)
		flags |= SLI4_READ_LNKSTAT_CLOF;

	cmd->dw1_flags = cpu_to_le32(flags);
	return 0;
}

int
sli_cmd_read_status(struct sli4 *sli4, void *buf, u8 clear_counters)
{
	struct sli4_cmd_read_status *cmd = buf;
	u32 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	cmd->hdr.command = SLI4_MBX_CMD_READ_STATUS;
	if (clear_counters)
		flags |= SLI4_READSTATUS_CLEAR_COUNTERS;
	else
		flags &= ~SLI4_READSTATUS_CLEAR_COUNTERS;

	cmd->dw1_flags = cpu_to_le32(flags);
	return 0;
}

int
sli_cmd_init_link(struct sli4 *sli4, void *buf, u32 speed, u8 reset_alpa)
{
	struct sli4_cmd_init_link *init_link = buf;
	u32 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	init_link->hdr.command = SLI4_MBX_CMD_INIT_LINK;

	init_link->sel_reset_al_pa_dword =
				cpu_to_le32(reset_alpa);
	flags &= ~SLI4_INIT_LINK_F_LOOPBACK;

	init_link->link_speed_sel_code = cpu_to_le32(speed);
	switch (speed) {
	case SLI4_LINK_SPEED_1G:
	case SLI4_LINK_SPEED_2G:
	case SLI4_LINK_SPEED_4G:
	case SLI4_LINK_SPEED_8G:
	case SLI4_LINK_SPEED_16G:
	case SLI4_LINK_SPEED_32G:
	case SLI4_LINK_SPEED_64G:
		flags |= SLI4_INIT_LINK_F_FIXED_SPEED;
		break;
	case SLI4_LINK_SPEED_10G:
		efc_log_info(sli4, "unsupported FC speed %d\n", speed);
		init_link->flags0 = cpu_to_le32(flags);
		return -EIO;
	}

	switch (sli4->topology) {
	case SLI4_READ_CFG_TOPO_FC:
		/* Attempt P2P but failover to FC-AL */
		flags |= SLI4_INIT_LINK_F_FAIL_OVER;
		flags |= SLI4_INIT_LINK_F_P2P_FAIL_OVER;
		break;
	case SLI4_READ_CFG_TOPO_FC_AL:
		flags |= SLI4_INIT_LINK_F_FCAL_ONLY;
		if (speed == SLI4_LINK_SPEED_16G ||
		    speed == SLI4_LINK_SPEED_32G) {
			efc_log_info(sli4, "unsupported FC-AL speed %d\n",
				     speed);
			init_link->flags0 = cpu_to_le32(flags);
			return -EIO;
		}
		break;
	case SLI4_READ_CFG_TOPO_NON_FC_AL:
		flags |= SLI4_INIT_LINK_F_P2P_ONLY;
		break;
	default:

		efc_log_info(sli4, "unsupported topology %#x\n", sli4->topology);

		init_link->flags0 = cpu_to_le32(flags);
		return -EIO;
	}

	flags &= ~SLI4_INIT_LINK_F_UNFAIR;
	flags &= ~SLI4_INIT_LINK_F_NO_LIRP;
	flags &= ~SLI4_INIT_LINK_F_LOOP_VALID_CHK;
	flags &= ~SLI4_INIT_LINK_F_NO_LISA;
	flags &= ~SLI4_INIT_LINK_F_PICK_HI_ALPA;
	init_link->flags0 = cpu_to_le32(flags);

	return 0;
}

int
sli_cmd_init_vfi(struct sli4 *sli4, void *buf, u16 vfi, u16 fcfi, u16 vpi)
{
	struct sli4_cmd_init_vfi *init_vfi = buf;
	u16 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	init_vfi->hdr.command = SLI4_MBX_CMD_INIT_VFI;
	init_vfi->vfi = cpu_to_le16(vfi);
	init_vfi->fcfi = cpu_to_le16(fcfi);

	/*
	 * If the VPI is valid, initialize it at the same time as
	 * the VFI
	 */
	if (vpi != U16_MAX) {
		flags |= SLI4_INIT_VFI_FLAG_VP;
		init_vfi->flags0_word = cpu_to_le16(flags);
		init_vfi->vpi = cpu_to_le16(vpi);
	}

	return 0;
}

int
sli_cmd_init_vpi(struct sli4 *sli4, void *buf, u16 vpi, u16 vfi)
{
	struct sli4_cmd_init_vpi *init_vpi = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	init_vpi->hdr.command = SLI4_MBX_CMD_INIT_VPI;
	init_vpi->vpi = cpu_to_le16(vpi);
	init_vpi->vfi = cpu_to_le16(vfi);

	return 0;
}

int
sli_cmd_post_xri(struct sli4 *sli4, void *buf, u16 xri_base, u16 xri_count)
{
	struct sli4_cmd_post_xri *post_xri = buf;
	u16 xri_count_flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	post_xri->hdr.command = SLI4_MBX_CMD_POST_XRI;
	post_xri->xri_base = cpu_to_le16(xri_base);
	xri_count_flags = xri_count & SLI4_POST_XRI_COUNT;
	xri_count_flags |= SLI4_POST_XRI_FLAG_ENX;
	xri_count_flags |= SLI4_POST_XRI_FLAG_VAL;
	post_xri->xri_count_flags = cpu_to_le16(xri_count_flags);

	return 0;
}

int
sli_cmd_release_xri(struct sli4 *sli4, void *buf, u8 num_xri)
{
	struct sli4_cmd_release_xri *release_xri = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	release_xri->hdr.command = SLI4_MBX_CMD_RELEASE_XRI;
	release_xri->xri_count_word = cpu_to_le16(num_xri &
					SLI4_RELEASE_XRI_COUNT);

	return 0;
}

static int
sli_cmd_read_config(struct sli4 *sli4, void *buf)
{
	struct sli4_cmd_read_config *read_config = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	read_config->hdr.command = SLI4_MBX_CMD_READ_CONFIG;

	return 0;
}

int
sli_cmd_read_nvparms(struct sli4 *sli4, void *buf)
{
	struct sli4_cmd_read_nvparms *read_nvparms = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	read_nvparms->hdr.command = SLI4_MBX_CMD_READ_NVPARMS;

	return 0;
}

int
sli_cmd_write_nvparms(struct sli4 *sli4, void *buf, u8 *wwpn, u8 *wwnn,
		      u8 hard_alpa, u32 preferred_d_id)
{
	struct sli4_cmd_write_nvparms *write_nvparms = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	write_nvparms->hdr.command = SLI4_MBX_CMD_WRITE_NVPARMS;
	memcpy(write_nvparms->wwpn, wwpn, 8);
	memcpy(write_nvparms->wwnn, wwnn, 8);

	write_nvparms->hard_alpa_d_id =
			cpu_to_le32((preferred_d_id << 8) | hard_alpa);
	return 0;
}

static int
sli_cmd_read_rev(struct sli4 *sli4, void *buf, struct efc_dma *vpd)
{
	struct sli4_cmd_read_rev *read_rev = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	read_rev->hdr.command = SLI4_MBX_CMD_READ_REV;

	if (vpd && vpd->size) {
		read_rev->flags0_word |= cpu_to_le16(SLI4_READ_REV_FLAG_VPD);

		read_rev->available_length_dword =
			cpu_to_le32(vpd->size &
				    SLI4_READ_REV_AVAILABLE_LENGTH);

		read_rev->hostbuf.low =
				cpu_to_le32(lower_32_bits(vpd->phys));
		read_rev->hostbuf.high =
				cpu_to_le32(upper_32_bits(vpd->phys));
	}

	return 0;
}

int
sli_cmd_read_sparm64(struct sli4 *sli4, void *buf, struct efc_dma *dma, u16 vpi)
{
	struct sli4_cmd_read_sparm64 *read_sparm64 = buf;

	if (vpi == U16_MAX) {
		efc_log_err(sli4, "special VPI not supported!!!\n");
		return -EIO;
	}

	if (!dma || !dma->phys) {
		efc_log_err(sli4, "bad DMA buffer\n");
		return -EIO;
	}

	memset(buf, 0, SLI4_BMBX_SIZE);

	read_sparm64->hdr.command = SLI4_MBX_CMD_READ_SPARM64;

	read_sparm64->bde_64.bde_type_buflen =
			cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
				    (dma->size & SLI4_BDE_LEN_MASK));
	read_sparm64->bde_64.u.data.low =
			cpu_to_le32(lower_32_bits(dma->phys));
	read_sparm64->bde_64.u.data.high =
			cpu_to_le32(upper_32_bits(dma->phys));

	read_sparm64->vpi = cpu_to_le16(vpi);

	return 0;
}

int
sli_cmd_read_topology(struct sli4 *sli4, void *buf, struct efc_dma *dma)
{
	struct sli4_cmd_read_topology *read_topo = buf;

	if (!dma || !dma->size)
		return -EIO;

	if (dma->size < SLI4_MIN_LOOP_MAP_BYTES) {
		efc_log_err(sli4, "loop map buffer too small %zx\n", dma->size);
		return -EIO;
	}

	memset(buf, 0, SLI4_BMBX_SIZE);

	read_topo->hdr.command = SLI4_MBX_CMD_READ_TOPOLOGY;

	memset(dma->virt, 0, dma->size);

	read_topo->bde_loop_map.bde_type_buflen =
					cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
					(dma->size & SLI4_BDE_LEN_MASK));
	read_topo->bde_loop_map.u.data.low  =
				cpu_to_le32(lower_32_bits(dma->phys));
	read_topo->bde_loop_map.u.data.high =
				cpu_to_le32(upper_32_bits(dma->phys));

	return 0;
}

int
sli_cmd_reg_fcfi(struct sli4 *sli4, void *buf, u16 index,
		 struct sli4_cmd_rq_cfg *rq_cfg)
{
	struct sli4_cmd_reg_fcfi *reg_fcfi = buf;
	u32 i;

	memset(buf, 0, SLI4_BMBX_SIZE);

	reg_fcfi->hdr.command = SLI4_MBX_CMD_REG_FCFI;

	reg_fcfi->fcf_index = cpu_to_le16(index);

	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		switch (i) {
		case 0:
			reg_fcfi->rqid0 = rq_cfg[0].rq_id;
			break;
		case 1:
			reg_fcfi->rqid1 = rq_cfg[1].rq_id;
			break;
		case 2:
			reg_fcfi->rqid2 = rq_cfg[2].rq_id;
			break;
		case 3:
			reg_fcfi->rqid3 = rq_cfg[3].rq_id;
			break;
		}
		reg_fcfi->rq_cfg[i].r_ctl_mask = rq_cfg[i].r_ctl_mask;
		reg_fcfi->rq_cfg[i].r_ctl_match = rq_cfg[i].r_ctl_match;
		reg_fcfi->rq_cfg[i].type_mask = rq_cfg[i].type_mask;
		reg_fcfi->rq_cfg[i].type_match = rq_cfg[i].type_match;
	}

	return 0;
}

int
sli_cmd_reg_fcfi_mrq(struct sli4 *sli4, void *buf, u8 mode, u16 fcf_index,
		     u8 rq_selection_policy, u8 mrq_bit_mask, u16 num_mrqs,
		     struct sli4_cmd_rq_cfg *rq_cfg)
{
	struct sli4_cmd_reg_fcfi_mrq *reg_fcfi_mrq = buf;
	u32 i;
	u32 mrq_flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	reg_fcfi_mrq->hdr.command = SLI4_MBX_CMD_REG_FCFI_MRQ;
	if (mode == SLI4_CMD_REG_FCFI_SET_FCFI_MODE) {
		reg_fcfi_mrq->fcf_index = cpu_to_le16(fcf_index);
		goto done;
	}

	reg_fcfi_mrq->dw8_vlan = cpu_to_le32(SLI4_REGFCFI_MRQ_MODE);

	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		reg_fcfi_mrq->rq_cfg[i].r_ctl_mask = rq_cfg[i].r_ctl_mask;
		reg_fcfi_mrq->rq_cfg[i].r_ctl_match = rq_cfg[i].r_ctl_match;
		reg_fcfi_mrq->rq_cfg[i].type_mask = rq_cfg[i].type_mask;
		reg_fcfi_mrq->rq_cfg[i].type_match = rq_cfg[i].type_match;

		switch (i) {
		case 3:
			reg_fcfi_mrq->rqid3 = rq_cfg[i].rq_id;
			break;
		case 2:
			reg_fcfi_mrq->rqid2 = rq_cfg[i].rq_id;
			break;
		case 1:
			reg_fcfi_mrq->rqid1 = rq_cfg[i].rq_id;
			break;
		case 0:
			reg_fcfi_mrq->rqid0 = rq_cfg[i].rq_id;
			break;
		}
	}

	mrq_flags = num_mrqs & SLI4_REGFCFI_MRQ_MASK_NUM_PAIRS;
	mrq_flags |= (mrq_bit_mask << 8);
	mrq_flags |= (rq_selection_policy << 12);
	reg_fcfi_mrq->dw9_mrqflags = cpu_to_le32(mrq_flags);
done:
	return 0;
}

int
sli_cmd_reg_rpi(struct sli4 *sli4, void *buf, u32 rpi, u32 vpi, u32 fc_id,
		struct efc_dma *dma, u8 update, u8 enable_t10_pi)
{
	struct sli4_cmd_reg_rpi *reg_rpi = buf;
	u32 rportid_flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	reg_rpi->hdr.command = SLI4_MBX_CMD_REG_RPI;

	reg_rpi->rpi = cpu_to_le16(rpi);

	rportid_flags = fc_id & SLI4_REGRPI_REMOTE_N_PORTID;

	if (update)
		rportid_flags |= SLI4_REGRPI_UPD;
	else
		rportid_flags &= ~SLI4_REGRPI_UPD;

	if (enable_t10_pi)
		rportid_flags |= SLI4_REGRPI_ETOW;
	else
		rportid_flags &= ~SLI4_REGRPI_ETOW;

	reg_rpi->dw2_rportid_flags = cpu_to_le32(rportid_flags);

	reg_rpi->bde_64.bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (SLI4_REG_RPI_BUF_LEN & SLI4_BDE_LEN_MASK));
	reg_rpi->bde_64.u.data.low  =
		cpu_to_le32(lower_32_bits(dma->phys));
	reg_rpi->bde_64.u.data.high =
		cpu_to_le32(upper_32_bits(dma->phys));

	reg_rpi->vpi = cpu_to_le16(vpi);

	return 0;
}

int
sli_cmd_reg_vfi(struct sli4 *sli4, void *buf, size_t size,
		u16 vfi, u16 fcfi, struct efc_dma dma,
		u16 vpi, __be64 sli_wwpn, u32 fc_id)
{
	struct sli4_cmd_reg_vfi *reg_vfi = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	reg_vfi->hdr.command = SLI4_MBX_CMD_REG_VFI;

	reg_vfi->vfi = cpu_to_le16(vfi);

	reg_vfi->fcfi = cpu_to_le16(fcfi);

	reg_vfi->sparm.bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (SLI4_REG_RPI_BUF_LEN & SLI4_BDE_LEN_MASK));
	reg_vfi->sparm.u.data.low  =
		cpu_to_le32(lower_32_bits(dma.phys));
	reg_vfi->sparm.u.data.high =
		cpu_to_le32(upper_32_bits(dma.phys));

	reg_vfi->e_d_tov = cpu_to_le32(sli4->e_d_tov);
	reg_vfi->r_a_tov = cpu_to_le32(sli4->r_a_tov);

	reg_vfi->dw0w1_flags |= cpu_to_le16(SLI4_REGVFI_VP);
	reg_vfi->vpi = cpu_to_le16(vpi);
	memcpy(reg_vfi->wwpn, &sli_wwpn, sizeof(reg_vfi->wwpn));
	reg_vfi->dw10_lportid_flags = cpu_to_le32(fc_id);

	return 0;
}

int
sli_cmd_reg_vpi(struct sli4 *sli4, void *buf, u32 fc_id, __be64 sli_wwpn,
		u16 vpi, u16 vfi, bool update)
{
	struct sli4_cmd_reg_vpi *reg_vpi = buf;
	u32 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	reg_vpi->hdr.command = SLI4_MBX_CMD_REG_VPI;

	flags = (fc_id & SLI4_REGVPI_LOCAL_N_PORTID);
	if (update)
		flags |= SLI4_REGVPI_UPD;
	else
		flags &= ~SLI4_REGVPI_UPD;

	reg_vpi->dw2_lportid_flags = cpu_to_le32(flags);
	memcpy(reg_vpi->wwpn, &sli_wwpn, sizeof(reg_vpi->wwpn));
	reg_vpi->vpi = cpu_to_le16(vpi);
	reg_vpi->vfi = cpu_to_le16(vfi);

	return 0;
}

static int
sli_cmd_request_features(struct sli4 *sli4, void *buf, u32 features_mask,
			 bool query)
{
	struct sli4_cmd_request_features *req_features = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	req_features->hdr.command = SLI4_MBX_CMD_RQST_FEATURES;

	if (query)
		req_features->dw1_qry = cpu_to_le32(SLI4_REQFEAT_QRY);

	req_features->cmd = cpu_to_le32(features_mask);

	return 0;
}

int
sli_cmd_unreg_fcfi(struct sli4 *sli4, void *buf, u16 indicator)
{
	struct sli4_cmd_unreg_fcfi *unreg_fcfi = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	unreg_fcfi->hdr.command = SLI4_MBX_CMD_UNREG_FCFI;
	unreg_fcfi->fcfi = cpu_to_le16(indicator);

	return 0;
}

int
sli_cmd_unreg_rpi(struct sli4 *sli4, void *buf, u16 indicator,
		  enum sli4_resource which, u32 fc_id)
{
	struct sli4_cmd_unreg_rpi *unreg_rpi = buf;
	u32 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	unreg_rpi->hdr.command = SLI4_MBX_CMD_UNREG_RPI;
	switch (which) {
	case SLI4_RSRC_RPI:
		flags |= SLI4_UNREG_RPI_II_RPI;
		if (fc_id == U32_MAX)
			break;

		flags |= SLI4_UNREG_RPI_DP;
		unreg_rpi->dw2_dest_n_portid =
			cpu_to_le32(fc_id & SLI4_UNREG_RPI_DEST_N_PORTID_MASK);
		break;
	case SLI4_RSRC_VPI:
		flags |= SLI4_UNREG_RPI_II_VPI;
		break;
	case SLI4_RSRC_VFI:
		flags |= SLI4_UNREG_RPI_II_VFI;
		break;
	case SLI4_RSRC_FCFI:
		flags |= SLI4_UNREG_RPI_II_FCFI;
		break;
	default:
		efc_log_info(sli4, "unknown type %#x\n", which);
		return -EIO;
	}

	unreg_rpi->dw1w1_flags = cpu_to_le16(flags);
	unreg_rpi->index = cpu_to_le16(indicator);

	return 0;
}

int
sli_cmd_unreg_vfi(struct sli4 *sli4, void *buf, u16 index, u32 which)
{
	struct sli4_cmd_unreg_vfi *unreg_vfi = buf;

	memset(buf, 0, SLI4_BMBX_SIZE);

	unreg_vfi->hdr.command = SLI4_MBX_CMD_UNREG_VFI;
	switch (which) {
	case SLI4_UNREG_TYPE_DOMAIN:
		unreg_vfi->index = cpu_to_le16(index);
		break;
	case SLI4_UNREG_TYPE_FCF:
		unreg_vfi->index = cpu_to_le16(index);
		break;
	case SLI4_UNREG_TYPE_ALL:
		unreg_vfi->index = cpu_to_le16(U32_MAX);
		break;
	default:
		return -EIO;
	}

	if (which != SLI4_UNREG_TYPE_DOMAIN)
		unreg_vfi->dw2_flags = cpu_to_le16(SLI4_UNREG_VFI_II_FCFI);

	return 0;
}

int
sli_cmd_unreg_vpi(struct sli4 *sli4, void *buf, u16 indicator, u32 which)
{
	struct sli4_cmd_unreg_vpi *unreg_vpi = buf;
	u32 flags = 0;

	memset(buf, 0, SLI4_BMBX_SIZE);

	unreg_vpi->hdr.command = SLI4_MBX_CMD_UNREG_VPI;
	unreg_vpi->index = cpu_to_le16(indicator);
	switch (which) {
	case SLI4_UNREG_TYPE_PORT:
		flags |= SLI4_UNREG_VPI_II_VPI;
		break;
	case SLI4_UNREG_TYPE_DOMAIN:
		flags |= SLI4_UNREG_VPI_II_VFI;
		break;
	case SLI4_UNREG_TYPE_FCF:
		flags |= SLI4_UNREG_VPI_II_FCFI;
		break;
	case SLI4_UNREG_TYPE_ALL:
		/* override indicator */
		unreg_vpi->index = cpu_to_le16(U32_MAX);
		flags |= SLI4_UNREG_VPI_II_FCFI;
		break;
	default:
		return -EIO;
	}

	unreg_vpi->dw2w0_flags = cpu_to_le16(flags);
	return 0;
}

static int
sli_cmd_common_modify_eq_delay(struct sli4 *sli4, void *buf,
			       struct sli4_queue *q, int num_q, u32 shift,
			       u32 delay_mult)
{
	struct sli4_rqst_cmn_modify_eq_delay *req = NULL;
	int i;

	req = sli_config_cmd_init(sli4, buf,
			SLI4_CFG_PYLD_LENGTH(cmn_modify_eq_delay), NULL);
	if (!req)
		return -EIO;

	sli_cmd_fill_hdr(&req->hdr, SLI4_CMN_MODIFY_EQ_DELAY,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_modify_eq_delay));
	req->num_eq = cpu_to_le32(num_q);

	for (i = 0; i < num_q; i++) {
		req->eq_delay_record[i].eq_id = cpu_to_le32(q[i].id);
		req->eq_delay_record[i].phase = cpu_to_le32(shift);
		req->eq_delay_record[i].delay_multiplier =
			cpu_to_le32(delay_mult);
	}

	return 0;
}

void
sli4_cmd_lowlevel_set_watchdog(struct sli4 *sli4, void *buf,
			       size_t size, u16 timeout)
{
	struct sli4_rqst_lowlevel_set_watchdog *req = NULL;

	req = sli_config_cmd_init(sli4, buf,
			SLI4_CFG_PYLD_LENGTH(lowlevel_set_watchdog), NULL);
	if (!req)
		return;

	sli_cmd_fill_hdr(&req->hdr, SLI4_OPC_LOWLEVEL_SET_WATCHDOG,
			 SLI4_SUBSYSTEM_LOWLEVEL, CMD_V0,
			 SLI4_RQST_PYLD_LEN(lowlevel_set_watchdog));
	req->watchdog_timeout = cpu_to_le16(timeout);
}

static int
sli_cmd_common_get_cntl_attributes(struct sli4 *sli4, void *buf,
				   struct efc_dma *dma)
{
	struct sli4_rqst_hdr *hdr = NULL;

	hdr = sli_config_cmd_init(sli4, buf, SLI4_RQST_CMDSZ(hdr), dma);
	if (!hdr)
		return -EIO;

	hdr->opcode = SLI4_CMN_GET_CNTL_ATTRIBUTES;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = cpu_to_le32(dma->size);

	return 0;
}

static int
sli_cmd_common_get_cntl_addl_attributes(struct sli4 *sli4, void *buf,
					struct efc_dma *dma)
{
	struct sli4_rqst_hdr *hdr = NULL;

	hdr = sli_config_cmd_init(sli4, buf, SLI4_RQST_CMDSZ(hdr), dma);
	if (!hdr)
		return -EIO;

	hdr->opcode = SLI4_CMN_GET_CNTL_ADDL_ATTRS;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = cpu_to_le32(dma->size);

	return 0;
}

int
sli_cmd_common_nop(struct sli4 *sli4, void *buf, uint64_t context)
{
	struct sli4_rqst_cmn_nop *nop = NULL;

	nop = sli_config_cmd_init(sli4, buf, SLI4_CFG_PYLD_LENGTH(cmn_nop),
				  NULL);
	if (!nop)
		return -EIO;

	sli_cmd_fill_hdr(&nop->hdr, SLI4_CMN_NOP, SLI4_SUBSYSTEM_COMMON,
			 CMD_V0, SLI4_RQST_PYLD_LEN(cmn_nop));

	memcpy(&nop->context, &context, sizeof(context));

	return 0;
}

int
sli_cmd_common_get_resource_extent_info(struct sli4 *sli4, void *buf, u16 rtype)
{
	struct sli4_rqst_cmn_get_resource_extent_info *ext = NULL;

	ext = sli_config_cmd_init(sli4, buf,
			SLI4_RQST_CMDSZ(cmn_get_resource_extent_info), NULL);
	if (!ext)
		return -EIO;

	sli_cmd_fill_hdr(&ext->hdr, SLI4_CMN_GET_RSC_EXTENT_INFO,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_get_resource_extent_info));

	ext->resource_type = cpu_to_le16(rtype);

	return 0;
}

int
sli_cmd_common_get_sli4_parameters(struct sli4 *sli4, void *buf)
{
	struct sli4_rqst_hdr *hdr = NULL;

	hdr = sli_config_cmd_init(sli4, buf,
			SLI4_CFG_PYLD_LENGTH(cmn_get_sli4_params), NULL);
	if (!hdr)
		return -EIO;

	hdr->opcode = SLI4_CMN_GET_SLI4_PARAMS;
	hdr->subsystem = SLI4_SUBSYSTEM_COMMON;
	hdr->request_length = SLI4_RQST_PYLD_LEN(cmn_get_sli4_params);

	return 0;
}

static int
sli_cmd_common_get_port_name(struct sli4 *sli4, void *buf)
{
	struct sli4_rqst_cmn_get_port_name *pname;

	pname = sli_config_cmd_init(sli4, buf,
			SLI4_CFG_PYLD_LENGTH(cmn_get_port_name), NULL);
	if (!pname)
		return -EIO;

	sli_cmd_fill_hdr(&pname->hdr, SLI4_CMN_GET_PORT_NAME,
			 SLI4_SUBSYSTEM_COMMON, CMD_V1,
			 SLI4_RQST_PYLD_LEN(cmn_get_port_name));

	/* Set the port type value (ethernet=0, FC=1) for V1 commands */
	pname->port_type = SLI4_PORT_TYPE_FC;

	return 0;
}

int
sli_cmd_common_write_object(struct sli4 *sli4, void *buf, u16 noc,
			    u16 eof, u32 desired_write_length,
			    u32 offset, char *obj_name,
			    struct efc_dma *dma)
{
	struct sli4_rqst_cmn_write_object *wr_obj = NULL;
	struct sli4_bde *bde;
	u32 dwflags = 0;

	wr_obj = sli_config_cmd_init(sli4, buf,
			SLI4_RQST_CMDSZ(cmn_write_object) + sizeof(*bde), NULL);
	if (!wr_obj)
		return -EIO;

	sli_cmd_fill_hdr(&wr_obj->hdr, SLI4_CMN_WRITE_OBJECT,
		SLI4_SUBSYSTEM_COMMON, CMD_V0,
		SLI4_RQST_PYLD_LEN_VAR(cmn_write_object, sizeof(*bde)));

	if (noc)
		dwflags |= SLI4_RQ_DES_WRITE_LEN_NOC;
	if (eof)
		dwflags |= SLI4_RQ_DES_WRITE_LEN_EOF;
	dwflags |= (desired_write_length & SLI4_RQ_DES_WRITE_LEN);

	wr_obj->desired_write_len_dword = cpu_to_le32(dwflags);

	wr_obj->write_offset = cpu_to_le32(offset);
	strncpy(wr_obj->object_name, obj_name, sizeof(wr_obj->object_name) - 1);
	wr_obj->host_buffer_descriptor_count = cpu_to_le32(1);

	bde = (struct sli4_bde *)wr_obj->host_buffer_descriptor;

	/* Setup to transfer xfer_size bytes to device */
	bde->bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (desired_write_length & SLI4_BDE_LEN_MASK));
	bde->u.data.low = cpu_to_le32(lower_32_bits(dma->phys));
	bde->u.data.high = cpu_to_le32(upper_32_bits(dma->phys));

	return 0;
}

int
sli_cmd_common_delete_object(struct sli4 *sli4, void *buf, char *obj_name)
{
	struct sli4_rqst_cmn_delete_object *req = NULL;

	req = sli_config_cmd_init(sli4, buf,
				  SLI4_RQST_CMDSZ(cmn_delete_object), NULL);
	if (!req)
		return -EIO;

	sli_cmd_fill_hdr(&req->hdr, SLI4_CMN_DELETE_OBJECT,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_delete_object));

	strncpy(req->object_name, obj_name, sizeof(req->object_name) - 1);
	return 0;
}

int
sli_cmd_common_read_object(struct sli4 *sli4, void *buf, u32 desired_read_len,
			   u32 offset, char *obj_name, struct efc_dma *dma)
{
	struct sli4_rqst_cmn_read_object *rd_obj = NULL;
	struct sli4_bde *bde;

	rd_obj = sli_config_cmd_init(sli4, buf,
			SLI4_RQST_CMDSZ(cmn_read_object) + sizeof(*bde), NULL);
	if (!rd_obj)
		return -EIO;

	sli_cmd_fill_hdr(&rd_obj->hdr, SLI4_CMN_READ_OBJECT,
		SLI4_SUBSYSTEM_COMMON, CMD_V0,
		SLI4_RQST_PYLD_LEN_VAR(cmn_read_object, sizeof(*bde)));
	rd_obj->desired_read_length_dword =
		cpu_to_le32(desired_read_len & SLI4_REQ_DESIRE_READLEN);

	rd_obj->read_offset = cpu_to_le32(offset);
	strncpy(rd_obj->object_name, obj_name, sizeof(rd_obj->object_name) - 1);
	rd_obj->host_buffer_descriptor_count = cpu_to_le32(1);

	bde = (struct sli4_bde *)rd_obj->host_buffer_descriptor;

	/* Setup to transfer xfer_size bytes to device */
	bde->bde_type_buflen =
		cpu_to_le32((SLI4_BDE_TYPE_VAL(64)) |
			    (desired_read_len & SLI4_BDE_LEN_MASK));
	if (dma) {
		bde->u.data.low = cpu_to_le32(lower_32_bits(dma->phys));
		bde->u.data.high = cpu_to_le32(upper_32_bits(dma->phys));
	} else {
		bde->u.data.low = 0;
		bde->u.data.high = 0;
	}

	return 0;
}

int
sli_cmd_dmtf_exec_clp_cmd(struct sli4 *sli4, void *buf, struct efc_dma *cmd,
			  struct efc_dma *resp)
{
	struct sli4_rqst_dmtf_exec_clp_cmd *clp_cmd = NULL;

	clp_cmd = sli_config_cmd_init(sli4, buf,
				SLI4_RQST_CMDSZ(dmtf_exec_clp_cmd), NULL);
	if (!clp_cmd)
		return -EIO;

	sli_cmd_fill_hdr(&clp_cmd->hdr, DMTF_EXEC_CLP_CMD, SLI4_SUBSYSTEM_DMTF,
			 CMD_V0, SLI4_RQST_PYLD_LEN(dmtf_exec_clp_cmd));

	clp_cmd->cmd_buf_length = cpu_to_le32(cmd->size);
	clp_cmd->cmd_buf_addr_low =  cpu_to_le32(lower_32_bits(cmd->phys));
	clp_cmd->cmd_buf_addr_high =  cpu_to_le32(upper_32_bits(cmd->phys));
	clp_cmd->resp_buf_length = cpu_to_le32(resp->size);
	clp_cmd->resp_buf_addr_low =  cpu_to_le32(lower_32_bits(resp->phys));
	clp_cmd->resp_buf_addr_high =  cpu_to_le32(upper_32_bits(resp->phys));
	return 0;
}

int
sli_cmd_common_set_dump_location(struct sli4 *sli4, void *buf, bool query,
				 bool is_buffer_list,
				 struct efc_dma *buffer, u8 fdb)
{
	struct sli4_rqst_cmn_set_dump_location *set_dump_loc = NULL;
	u32 buffer_length_flag = 0;

	set_dump_loc = sli_config_cmd_init(sli4, buf,
				SLI4_RQST_CMDSZ(cmn_set_dump_location), NULL);
	if (!set_dump_loc)
		return -EIO;

	sli_cmd_fill_hdr(&set_dump_loc->hdr, SLI4_CMN_SET_DUMP_LOCATION,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_set_dump_location));

	if (is_buffer_list)
		buffer_length_flag |= SLI4_CMN_SET_DUMP_BLP;

	if (query)
		buffer_length_flag |= SLI4_CMN_SET_DUMP_QRY;

	if (fdb)
		buffer_length_flag |= SLI4_CMN_SET_DUMP_FDB;

	if (buffer) {
		set_dump_loc->buf_addr_low =
			cpu_to_le32(lower_32_bits(buffer->phys));
		set_dump_loc->buf_addr_high =
			cpu_to_le32(upper_32_bits(buffer->phys));

		buffer_length_flag |=
			buffer->len & SLI4_CMN_SET_DUMP_BUFFER_LEN;
	} else {
		set_dump_loc->buf_addr_low = 0;
		set_dump_loc->buf_addr_high = 0;
		set_dump_loc->buffer_length_dword = 0;
	}
	set_dump_loc->buffer_length_dword = cpu_to_le32(buffer_length_flag);
	return 0;
}

int
sli_cmd_common_set_features(struct sli4 *sli4, void *buf, u32 feature,
			    u32 param_len, void *parameter)
{
	struct sli4_rqst_cmn_set_features *cmd = NULL;

	cmd = sli_config_cmd_init(sli4, buf,
				  SLI4_RQST_CMDSZ(cmn_set_features), NULL);
	if (!cmd)
		return -EIO;

	sli_cmd_fill_hdr(&cmd->hdr, SLI4_CMN_SET_FEATURES,
			 SLI4_SUBSYSTEM_COMMON, CMD_V0,
			 SLI4_RQST_PYLD_LEN(cmn_set_features));

	cmd->feature = cpu_to_le32(feature);
	cmd->param_len = cpu_to_le32(param_len);
	memcpy(cmd->params, parameter, param_len);

	return 0;
}

int
sli_cqe_mq(struct sli4 *sli4, void *buf)
{
	struct sli4_mcqe *mcqe = buf;
	u32 dwflags = le32_to_cpu(mcqe->dw3_flags);
	/*
	 * Firmware can split mbx completions into two MCQEs: first with only
	 * the "consumed" bit set and a second with the "complete" bit set.
	 * Thus, ignore MCQE unless "complete" is set.
	 */
	if (!(dwflags & SLI4_MCQE_COMPLETED))
		return SLI4_MCQE_STATUS_NOT_COMPLETED;

	if (le16_to_cpu(mcqe->completion_status)) {
		efc_log_info(sli4, "status(st=%#x ext=%#x con=%d cmp=%d ae=%d val=%d)\n",
			     le16_to_cpu(mcqe->completion_status),
			     le16_to_cpu(mcqe->extended_status),
			     (dwflags & SLI4_MCQE_CONSUMED),
			     (dwflags & SLI4_MCQE_COMPLETED),
			     (dwflags & SLI4_MCQE_AE),
			     (dwflags & SLI4_MCQE_VALID));
	}

	return le16_to_cpu(mcqe->completion_status);
}

int
sli_cqe_async(struct sli4 *sli4, void *buf)
{
	struct sli4_acqe *acqe = buf;
	int rc = -EIO;

	if (!buf) {
		efc_log_err(sli4, "bad parameter sli4=%p buf=%p\n", sli4, buf);
		return -EIO;
	}

	switch (acqe->event_code) {
	case SLI4_ACQE_EVENT_CODE_LINK_STATE:
		efc_log_info(sli4, "Unsupported by FC link, evt code:%#x\n",
			     acqe->event_code);
		break;
	case SLI4_ACQE_EVENT_CODE_GRP_5:
		efc_log_info(sli4, "ACQE GRP5\n");
		break;
	case SLI4_ACQE_EVENT_CODE_SLI_PORT_EVENT:
		efc_log_info(sli4, "ACQE SLI Port, type=0x%x, data1,2=0x%08x,0x%08x\n",
			     acqe->event_type,
			     le32_to_cpu(acqe->event_data[0]),
			     le32_to_cpu(acqe->event_data[1]));
		break;
	case SLI4_ACQE_EVENT_CODE_FC_LINK_EVENT:
		rc = sli_fc_process_link_attention(sli4, buf);
		break;
	default:
		efc_log_info(sli4, "ACQE unknown=%#x\n", acqe->event_code);
	}

	return rc;
}

bool
sli_fw_ready(struct sli4 *sli4)
{
	u32 val;

	/* Determine if the chip FW is in a ready state */
	val = sli_reg_read_status(sli4);
	return (val & SLI4_PORT_STATUS_RDY) ? 1 : 0;
}

static bool
sli_wait_for_fw_ready(struct sli4 *sli4, u32 timeout_ms)
{
	unsigned long end;

	end = jiffies + msecs_to_jiffies(timeout_ms);

	do {
		if (sli_fw_ready(sli4))
			return true;

		usleep_range(1000, 2000);
	} while (time_before(jiffies, end));

	return false;
}

static bool
sli_sliport_reset(struct sli4 *sli4)
{
	bool rc;
	u32 val;

	val = SLI4_PORT_CTRL_IP;
	/* Initialize port, endian */
	writel(val, (sli4->reg[0] + SLI4_PORT_CTRL_REG));

	rc = sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC);
	if (!rc)
		efc_log_crit(sli4, "port failed to become ready after initialization\n");

	return rc;
}

static bool
sli_fw_init(struct sli4 *sli4)
{
	/*
	 * Is firmware ready for operation?
	 */
	if (!sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC)) {
		efc_log_crit(sli4, "FW status is NOT ready\n");
		return false;
	}

	/*
	 * Reset port to a known state
	 */
	return sli_sliport_reset(sli4);
}

static int
sli_request_features(struct sli4 *sli4, u32 *features, bool query)
{
	struct sli4_cmd_request_features *req_features = sli4->bmbx.virt;

	if (sli_cmd_request_features(sli4, sli4->bmbx.virt, *features, query)) {
		efc_log_err(sli4, "bad REQUEST_FEATURES write\n");
		return -EIO;
	}

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail\n");
		return -EIO;
	}

	if (le16_to_cpu(req_features->hdr.status)) {
		efc_log_err(sli4, "REQUEST_FEATURES bad status %#x\n",
			    le16_to_cpu(req_features->hdr.status));
		return -EIO;
	}

	*features = le32_to_cpu(req_features->resp);
	return 0;
}

void
sli_calc_max_qentries(struct sli4 *sli4)
{
	enum sli4_qtype q;
	u32 qentries;

	for (q = SLI4_QTYPE_EQ; q < SLI4_QTYPE_MAX; q++) {
		sli4->qinfo.max_qentries[q] =
			sli_convert_mask_to_count(sli4->qinfo.count_method[q],
						  sli4->qinfo.count_mask[q]);
	}

	/* single, continguous DMA allocations will be called for each queue
	 * of size (max_qentries * queue entry size); since these can be large,
	 * check against the OS max DMA allocation size
	 */
	for (q = SLI4_QTYPE_EQ; q < SLI4_QTYPE_MAX; q++) {
		qentries = sli4->qinfo.max_qentries[q];

		efc_log_info(sli4, "[%s]: max_qentries from %d to %d\n",
			     SLI4_QNAME[q],
			     sli4->qinfo.max_qentries[q], qentries);
		sli4->qinfo.max_qentries[q] = qentries;
	}
}

static int
sli_get_read_config(struct sli4 *sli4)
{
	struct sli4_rsp_read_config *conf = sli4->bmbx.virt;
	u32 i, total, total_size;
	u32 *base;

	if (sli_cmd_read_config(sli4, sli4->bmbx.virt)) {
		efc_log_err(sli4, "bad READ_CONFIG write\n");
		return -EIO;
	}

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox fail (READ_CONFIG)\n");
		return -EIO;
	}

	if (le16_to_cpu(conf->hdr.status)) {
		efc_log_err(sli4, "READ_CONFIG bad status %#x\n",
			    le16_to_cpu(conf->hdr.status));
		return -EIO;
	}

	sli4->params.has_extents =
	  le32_to_cpu(conf->ext_dword) & SLI4_READ_CFG_RESP_RESOURCE_EXT;
	if (sli4->params.has_extents) {
		efc_log_err(sli4, "extents not supported\n");
		return -EIO;
	}

	base = sli4->ext[0].base;
	if (!base) {
		int size = SLI4_RSRC_MAX * sizeof(u32);

		base = kzalloc(size, GFP_KERNEL);
		if (!base)
			return -EIO;
	}

	for (i = 0; i < SLI4_RSRC_MAX; i++) {
		sli4->ext[i].number = 1;
		sli4->ext[i].n_alloc = 0;
		sli4->ext[i].base = &base[i];
	}

	sli4->ext[SLI4_RSRC_VFI].base[0] = le16_to_cpu(conf->vfi_base);
	sli4->ext[SLI4_RSRC_VFI].size = le16_to_cpu(conf->vfi_count);

	sli4->ext[SLI4_RSRC_VPI].base[0] = le16_to_cpu(conf->vpi_base);
	sli4->ext[SLI4_RSRC_VPI].size = le16_to_cpu(conf->vpi_count);

	sli4->ext[SLI4_RSRC_RPI].base[0] = le16_to_cpu(conf->rpi_base);
	sli4->ext[SLI4_RSRC_RPI].size = le16_to_cpu(conf->rpi_count);

	sli4->ext[SLI4_RSRC_XRI].base[0] = le16_to_cpu(conf->xri_base);
	sli4->ext[SLI4_RSRC_XRI].size = le16_to_cpu(conf->xri_count);

	sli4->ext[SLI4_RSRC_FCFI].base[0] = 0;
	sli4->ext[SLI4_RSRC_FCFI].size = le16_to_cpu(conf->fcfi_count);

	for (i = 0; i < SLI4_RSRC_MAX; i++) {
		total = sli4->ext[i].number * sli4->ext[i].size;
		total_size = BITS_TO_LONGS(total) * sizeof(long);
		sli4->ext[i].use_map = kzalloc(total_size, GFP_KERNEL);
		if (!sli4->ext[i].use_map) {
			efc_log_err(sli4, "bitmap memory allocation failed %d\n",
				    i);
			return -EIO;
		}
		sli4->ext[i].map_size = total;
	}

	sli4->topology = (le32_to_cpu(conf->topology_dword) &
			  SLI4_READ_CFG_RESP_TOPOLOGY) >> 24;
	switch (sli4->topology) {
	case SLI4_READ_CFG_TOPO_FC:
		efc_log_info(sli4, "FC (unknown)\n");
		break;
	case SLI4_READ_CFG_TOPO_NON_FC_AL:
		efc_log_info(sli4, "FC (direct attach)\n");
		break;
	case SLI4_READ_CFG_TOPO_FC_AL:
		efc_log_info(sli4, "FC (arbitrated loop)\n");
		break;
	default:
		efc_log_info(sli4, "bad topology %#x\n", sli4->topology);
	}

	sli4->e_d_tov = le16_to_cpu(conf->e_d_tov);
	sli4->r_a_tov = le16_to_cpu(conf->r_a_tov);

	sli4->link_module_type = le16_to_cpu(conf->lmt);

	sli4->qinfo.max_qcount[SLI4_QTYPE_EQ] =	le16_to_cpu(conf->eq_count);
	sli4->qinfo.max_qcount[SLI4_QTYPE_CQ] =	le16_to_cpu(conf->cq_count);
	sli4->qinfo.max_qcount[SLI4_QTYPE_WQ] =	le16_to_cpu(conf->wq_count);
	sli4->qinfo.max_qcount[SLI4_QTYPE_RQ] =	le16_to_cpu(conf->rq_count);

	/*
	 * READ_CONFIG doesn't give the max number of MQ. Applications
	 * will typically want 1, but we may need another at some future
	 * date. Dummy up a "max" MQ count here.
	 */
	sli4->qinfo.max_qcount[SLI4_QTYPE_MQ] = SLI4_USER_MQ_COUNT;
	return 0;
}

static int
sli_get_sli4_parameters(struct sli4 *sli4)
{
	struct sli4_rsp_cmn_get_sli4_params *parms;
	u32 dw_loopback;
	u32 dw_eq_pg_cnt;
	u32 dw_cq_pg_cnt;
	u32 dw_mq_pg_cnt;
	u32 dw_wq_pg_cnt;
	u32 dw_rq_pg_cnt;
	u32 dw_sgl_pg_cnt;

	if (sli_cmd_common_get_sli4_parameters(sli4, sli4->bmbx.virt))
		return -EIO;

	parms = (struct sli4_rsp_cmn_get_sli4_params *)
		 (((u8 *)sli4->bmbx.virt) +
		  offsetof(struct sli4_cmd_sli_config, payload.embed));

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail\n");
		return -EIO;
	}

	if (parms->hdr.status) {
		efc_log_err(sli4, "COMMON_GET_SLI4_PARAMETERS bad status %#x",
			    parms->hdr.status);
		efc_log_err(sli4, "additional status %#x\n",
			    parms->hdr.additional_status);
		return -EIO;
	}

	dw_loopback = le32_to_cpu(parms->dw16_loopback_scope);
	dw_eq_pg_cnt = le32_to_cpu(parms->dw6_eq_page_cnt);
	dw_cq_pg_cnt = le32_to_cpu(parms->dw8_cq_page_cnt);
	dw_mq_pg_cnt = le32_to_cpu(parms->dw10_mq_page_cnt);
	dw_wq_pg_cnt = le32_to_cpu(parms->dw12_wq_page_cnt);
	dw_rq_pg_cnt = le32_to_cpu(parms->dw14_rq_page_cnt);

	sli4->params.auto_reg =	(dw_loopback & SLI4_PARAM_AREG);
	sli4->params.auto_xfer_rdy = (dw_loopback & SLI4_PARAM_AGXF);
	sli4->params.hdr_template_req =	(dw_loopback & SLI4_PARAM_HDRR);
	sli4->params.t10_dif_inline_capable = (dw_loopback & SLI4_PARAM_TIMM);
	sli4->params.t10_dif_separate_capable =	(dw_loopback & SLI4_PARAM_TSMM);

	sli4->params.mq_create_version = GET_Q_CREATE_VERSION(dw_mq_pg_cnt);
	sli4->params.cq_create_version = GET_Q_CREATE_VERSION(dw_cq_pg_cnt);

	sli4->rq_min_buf_size =	le16_to_cpu(parms->min_rq_buffer_size);
	sli4->rq_max_buf_size = le32_to_cpu(parms->max_rq_buffer_size);

	sli4->qinfo.qpage_count[SLI4_QTYPE_EQ] =
		(dw_eq_pg_cnt & SLI4_PARAM_EQ_PAGE_CNT_MASK);
	sli4->qinfo.qpage_count[SLI4_QTYPE_CQ] =
		(dw_cq_pg_cnt & SLI4_PARAM_CQ_PAGE_CNT_MASK);
	sli4->qinfo.qpage_count[SLI4_QTYPE_MQ] =
		(dw_mq_pg_cnt & SLI4_PARAM_MQ_PAGE_CNT_MASK);
	sli4->qinfo.qpage_count[SLI4_QTYPE_WQ] =
		(dw_wq_pg_cnt & SLI4_PARAM_WQ_PAGE_CNT_MASK);
	sli4->qinfo.qpage_count[SLI4_QTYPE_RQ] =
		(dw_rq_pg_cnt & SLI4_PARAM_RQ_PAGE_CNT_MASK);

	/* save count methods and masks for each queue type */

	sli4->qinfo.count_mask[SLI4_QTYPE_EQ] =
			le16_to_cpu(parms->eqe_count_mask);
	sli4->qinfo.count_method[SLI4_QTYPE_EQ] =
			GET_Q_CNT_METHOD(dw_eq_pg_cnt);

	sli4->qinfo.count_mask[SLI4_QTYPE_CQ] =
			le16_to_cpu(parms->cqe_count_mask);
	sli4->qinfo.count_method[SLI4_QTYPE_CQ] =
			GET_Q_CNT_METHOD(dw_cq_pg_cnt);

	sli4->qinfo.count_mask[SLI4_QTYPE_MQ] =
			le16_to_cpu(parms->mqe_count_mask);
	sli4->qinfo.count_method[SLI4_QTYPE_MQ] =
			GET_Q_CNT_METHOD(dw_mq_pg_cnt);

	sli4->qinfo.count_mask[SLI4_QTYPE_WQ] =
			le16_to_cpu(parms->wqe_count_mask);
	sli4->qinfo.count_method[SLI4_QTYPE_WQ] =
			GET_Q_CNT_METHOD(dw_wq_pg_cnt);

	sli4->qinfo.count_mask[SLI4_QTYPE_RQ] =
			le16_to_cpu(parms->rqe_count_mask);
	sli4->qinfo.count_method[SLI4_QTYPE_RQ] =
			GET_Q_CNT_METHOD(dw_rq_pg_cnt);

	/* now calculate max queue entries */
	sli_calc_max_qentries(sli4);

	dw_sgl_pg_cnt = le32_to_cpu(parms->dw18_sgl_page_cnt);

	/* max # of pages */
	sli4->max_sgl_pages = (dw_sgl_pg_cnt & SLI4_PARAM_SGL_PAGE_CNT_MASK);

	/* bit map of available sizes */
	sli4->sgl_page_sizes = (dw_sgl_pg_cnt &
				SLI4_PARAM_SGL_PAGE_SZS_MASK) >> 8;
	/* ignore HLM here. Use value from REQUEST_FEATURES */
	sli4->sge_supported_length = le32_to_cpu(parms->sge_supported_length);
	sli4->params.sgl_pre_reg_required = (dw_loopback & SLI4_PARAM_SGLR);
	/* default to using pre-registered SGL's */
	sli4->params.sgl_pre_registered = true;

	sli4->params.perf_hint = dw_loopback & SLI4_PARAM_PHON;
	sli4->params.perf_wq_id_association = (dw_loopback & SLI4_PARAM_PHWQ);

	sli4->rq_batch = (le16_to_cpu(parms->dw15w1_rq_db_window) &
			  SLI4_PARAM_RQ_DB_WINDOW_MASK) >> 12;

	/* Use the highest available WQE size. */
	if (((dw_wq_pg_cnt & SLI4_PARAM_WQE_SZS_MASK) >> 8) &
	    SLI4_128BYTE_WQE_SUPPORT)
		sli4->wqe_size = SLI4_WQE_EXT_BYTES;
	else
		sli4->wqe_size = SLI4_WQE_BYTES;

	return 0;
}

static int
sli_get_ctrl_attributes(struct sli4 *sli4)
{
	struct sli4_rsp_cmn_get_cntl_attributes *attr;
	struct sli4_rsp_cmn_get_cntl_addl_attributes *add_attr;
	struct efc_dma data;
	u32 psize;

	/*
	 * Issue COMMON_GET_CNTL_ATTRIBUTES to get port_number. Temporarily
	 * uses VPD DMA buffer as the response won't fit in the embedded
	 * buffer.
	 */
	memset(sli4->vpd_data.virt, 0, sli4->vpd_data.size);
	if (sli_cmd_common_get_cntl_attributes(sli4, sli4->bmbx.virt,
					       &sli4->vpd_data)) {
		efc_log_err(sli4, "bad COMMON_GET_CNTL_ATTRIBUTES write\n");
		return -EIO;
	}

	attr =	sli4->vpd_data.virt;

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail\n");
		return -EIO;
	}

	if (attr->hdr.status) {
		efc_log_err(sli4, "COMMON_GET_CNTL_ATTRIBUTES bad status %#x",
			    attr->hdr.status);
		efc_log_err(sli4, "additional status %#x\n",
			    attr->hdr.additional_status);
		return -EIO;
	}

	sli4->port_number = attr->port_num_type_flags & SLI4_CNTL_ATTR_PORTNUM;

	memcpy(sli4->bios_version_string, attr->bios_version_str,
	       sizeof(sli4->bios_version_string));

	/* get additional attributes */
	psize = sizeof(struct sli4_rsp_cmn_get_cntl_addl_attributes);
	data.size = psize;
	data.virt = dma_alloc_coherent(&sli4->pci->dev, data.size,
				       &data.phys, GFP_DMA);
	if (!data.virt) {
		memset(&data, 0, sizeof(struct efc_dma));
		efc_log_err(sli4, "Failed to allocate memory for GET_CNTL_ADDL_ATTR\n");
		return -EIO;
	}

	if (sli_cmd_common_get_cntl_addl_attributes(sli4, sli4->bmbx.virt,
						    &data)) {
		efc_log_err(sli4, "bad GET_CNTL_ADDL_ATTR write\n");
		dma_free_coherent(&sli4->pci->dev, data.size,
				  data.virt, data.phys);
		return -EIO;
	}

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "mailbox fail (GET_CNTL_ADDL_ATTR)\n");
		dma_free_coherent(&sli4->pci->dev, data.size,
				  data.virt, data.phys);
		return -EIO;
	}

	add_attr = data.virt;
	if (add_attr->hdr.status) {
		efc_log_err(sli4, "GET_CNTL_ADDL_ATTR bad status %#x\n",
			    add_attr->hdr.status);
		dma_free_coherent(&sli4->pci->dev, data.size,
				  data.virt, data.phys);
		return -EIO;
	}

	memcpy(sli4->ipl_name, add_attr->ipl_file_name, sizeof(sli4->ipl_name));

	efc_log_info(sli4, "IPL:%s\n", (char *)sli4->ipl_name);

	dma_free_coherent(&sli4->pci->dev, data.size, data.virt,
			  data.phys);
	memset(&data, 0, sizeof(struct efc_dma));
	return 0;
}

static int
sli_get_fw_rev(struct sli4 *sli4)
{
	struct sli4_cmd_read_rev	*read_rev = sli4->bmbx.virt;

	if (sli_cmd_read_rev(sli4, sli4->bmbx.virt, &sli4->vpd_data))
		return -EIO;

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail (READ_REV)\n");
		return -EIO;
	}

	if (le16_to_cpu(read_rev->hdr.status)) {
		efc_log_err(sli4, "READ_REV bad status %#x\n",
			    le16_to_cpu(read_rev->hdr.status));
		return -EIO;
	}

	sli4->fw_rev[0] = le32_to_cpu(read_rev->first_fw_id);
	memcpy(sli4->fw_name[0], read_rev->first_fw_name,
	       sizeof(sli4->fw_name[0]));

	sli4->fw_rev[1] = le32_to_cpu(read_rev->second_fw_id);
	memcpy(sli4->fw_name[1], read_rev->second_fw_name,
	       sizeof(sli4->fw_name[1]));

	sli4->hw_rev[0] = le32_to_cpu(read_rev->first_hw_rev);
	sli4->hw_rev[1] = le32_to_cpu(read_rev->second_hw_rev);
	sli4->hw_rev[2] = le32_to_cpu(read_rev->third_hw_rev);

	efc_log_info(sli4, "FW1:%s (%08x) / FW2:%s (%08x)\n",
		     read_rev->first_fw_name, le32_to_cpu(read_rev->first_fw_id),
		     read_rev->second_fw_name, le32_to_cpu(read_rev->second_fw_id));

	efc_log_info(sli4, "HW1: %08x / HW2: %08x\n",
		     le32_to_cpu(read_rev->first_hw_rev),
		     le32_to_cpu(read_rev->second_hw_rev));

	/* Check that all VPD data was returned */
	if (le32_to_cpu(read_rev->returned_vpd_length) !=
	    le32_to_cpu(read_rev->actual_vpd_length)) {
		efc_log_info(sli4, "VPD length: avail=%d return=%d actual=%d\n",
			     le32_to_cpu(read_rev->available_length_dword) &
				    SLI4_READ_REV_AVAILABLE_LENGTH,
			     le32_to_cpu(read_rev->returned_vpd_length),
			     le32_to_cpu(read_rev->actual_vpd_length));
	}
	sli4->vpd_length = le32_to_cpu(read_rev->returned_vpd_length);
	return 0;
}

static int
sli_get_config(struct sli4 *sli4)
{
	struct sli4_rsp_cmn_get_port_name *port_name;
	struct sli4_cmd_read_nvparms *read_nvparms;

	/*
	 * Read the device configuration
	 */
	if (sli_get_read_config(sli4))
		return -EIO;

	if (sli_get_sli4_parameters(sli4))
		return -EIO;

	if (sli_get_ctrl_attributes(sli4))
		return -EIO;

	if (sli_cmd_common_get_port_name(sli4, sli4->bmbx.virt))
		return -EIO;

	port_name = (struct sli4_rsp_cmn_get_port_name *)
		    (((u8 *)sli4->bmbx.virt) +
		    offsetof(struct sli4_cmd_sli_config, payload.embed));

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox fail (GET_PORT_NAME)\n");
		return -EIO;
	}

	sli4->port_name[0] = port_name->port_name[sli4->port_number];
	sli4->port_name[1] = '\0';

	if (sli_get_fw_rev(sli4))
		return -EIO;

	if (sli_cmd_read_nvparms(sli4, sli4->bmbx.virt)) {
		efc_log_err(sli4, "bad READ_NVPARMS write\n");
		return -EIO;
	}

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox fail (READ_NVPARMS)\n");
		return -EIO;
	}

	read_nvparms = sli4->bmbx.virt;
	if (le16_to_cpu(read_nvparms->hdr.status)) {
		efc_log_err(sli4, "READ_NVPARMS bad status %#x\n",
			    le16_to_cpu(read_nvparms->hdr.status));
		return -EIO;
	}

	memcpy(sli4->wwpn, read_nvparms->wwpn, sizeof(sli4->wwpn));
	memcpy(sli4->wwnn, read_nvparms->wwnn, sizeof(sli4->wwnn));

	efc_log_info(sli4, "WWPN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		     sli4->wwpn[0], sli4->wwpn[1], sli4->wwpn[2], sli4->wwpn[3],
		     sli4->wwpn[4], sli4->wwpn[5], sli4->wwpn[6], sli4->wwpn[7]);
	efc_log_info(sli4, "WWNN %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		     sli4->wwnn[0], sli4->wwnn[1], sli4->wwnn[2], sli4->wwnn[3],
		     sli4->wwnn[4], sli4->wwnn[5], sli4->wwnn[6], sli4->wwnn[7]);

	return 0;
}

int
sli_setup(struct sli4 *sli4, void *os, struct pci_dev  *pdev,
	  void __iomem *reg[])
{
	u32 intf = U32_MAX;
	u32 pci_class_rev = 0;
	u32 rev_id = 0;
	u32 family = 0;
	u32 asic_id = 0;
	u32 i;
	struct sli4_asic_entry_t *asic;

	memset(sli4, 0, sizeof(struct sli4));

	sli4->os = os;
	sli4->pci = pdev;

	for (i = 0; i < 6; i++)
		sli4->reg[i] = reg[i];
	/*
	 * Read the SLI_INTF register to discover the register layout
	 * and other capability information
	 */
	if (pci_read_config_dword(pdev, SLI4_INTF_REG, &intf))
		return -EIO;

	if ((intf & SLI4_INTF_VALID_MASK) != (u32)SLI4_INTF_VALID_VALUE) {
		efc_log_err(sli4, "SLI_INTF is not valid\n");
		return -EIO;
	}

	/* driver only support SLI-4 */
	if ((intf & SLI4_INTF_REV_MASK) != SLI4_INTF_REV_S4) {
		efc_log_err(sli4, "Unsupported SLI revision (intf=%#x)\n", intf);
		return -EIO;
	}

	sli4->sli_family = intf & SLI4_INTF_FAMILY_MASK;

	sli4->if_type = intf & SLI4_INTF_IF_TYPE_MASK;
	efc_log_info(sli4, "status=%#x error1=%#x error2=%#x\n",
		     sli_reg_read_status(sli4),
		     sli_reg_read_err1(sli4),
		     sli_reg_read_err2(sli4));

	/*
	 * set the ASIC type and revision
	 */
	if (pci_read_config_dword(pdev, PCI_CLASS_REVISION, &pci_class_rev))
		return -EIO;

	rev_id = pci_class_rev & 0xff;
	family = sli4->sli_family;
	if (family == SLI4_FAMILY_CHECK_ASIC_TYPE) {
		if (!pci_read_config_dword(pdev, SLI4_ASIC_ID_REG, &asic_id))
			family = asic_id & SLI4_ASIC_GEN_MASK;
	}

	for (i = 0, asic = sli4_asic_table; i < ARRAY_SIZE(sli4_asic_table);
	     i++, asic++) {
		if (rev_id == asic->rev_id && family == asic->family) {
			sli4->asic_type = family;
			sli4->asic_rev = rev_id;
			break;
		}
	}
	/* Fail if no matching asic type/rev was found */
	if (!sli4->asic_type) {
		efc_log_err(sli4, "no matching asic family/rev found: %02x/%02x\n",
			    family, rev_id);
		return -EIO;
	}

	/*
	 * The bootstrap mailbox is equivalent to a MQ with a single 256 byte
	 * entry, a CQ with a single 16 byte entry, and no event queue.
	 * Alignment must be 16 bytes as the low order address bits in the
	 * address register are also control / status.
	 */
	sli4->bmbx.size = SLI4_BMBX_SIZE + sizeof(struct sli4_mcqe);
	sli4->bmbx.virt = dma_alloc_coherent(&pdev->dev, sli4->bmbx.size,
					     &sli4->bmbx.phys, GFP_DMA);
	if (!sli4->bmbx.virt) {
		memset(&sli4->bmbx, 0, sizeof(struct efc_dma));
		efc_log_err(sli4, "bootstrap mailbox allocation failed\n");
		return -EIO;
	}

	if (sli4->bmbx.phys & SLI4_BMBX_MASK_LO) {
		efc_log_err(sli4, "bad alignment for bootstrap mailbox\n");
		return -EIO;
	}

	efc_log_info(sli4, "bmbx v=%p p=0x%x %08x s=%zd\n", sli4->bmbx.virt,
		     upper_32_bits(sli4->bmbx.phys),
		     lower_32_bits(sli4->bmbx.phys), sli4->bmbx.size);

	/* 4096 is arbitrary. What should this value actually be? */
	sli4->vpd_data.size = 4096;
	sli4->vpd_data.virt = dma_alloc_coherent(&pdev->dev,
						 sli4->vpd_data.size,
						 &sli4->vpd_data.phys,
						 GFP_DMA);
	if (!sli4->vpd_data.virt) {
		memset(&sli4->vpd_data, 0, sizeof(struct efc_dma));
		/* Note that failure isn't fatal in this specific case */
		efc_log_info(sli4, "VPD buffer allocation failed\n");
	}

	if (!sli_fw_init(sli4)) {
		efc_log_err(sli4, "FW initialization failed\n");
		return -EIO;
	}

	/*
	 * Set one of fcpi(initiator), fcpt(target), fcpc(combined) to true
	 * in addition to any other desired features
	 */
	sli4->features = (SLI4_REQFEAT_IAAB | SLI4_REQFEAT_NPIV |
				 SLI4_REQFEAT_DIF | SLI4_REQFEAT_VF |
				 SLI4_REQFEAT_FCPC | SLI4_REQFEAT_IAAR |
				 SLI4_REQFEAT_HLM | SLI4_REQFEAT_PERFH |
				 SLI4_REQFEAT_RXSEQ | SLI4_REQFEAT_RXRI |
				 SLI4_REQFEAT_MRQP);

	/* use performance hints if available */
	if (sli4->params.perf_hint)
		sli4->features |= SLI4_REQFEAT_PERFH;

	if (sli_request_features(sli4, &sli4->features, true))
		return -EIO;

	if (sli_get_config(sli4))
		return -EIO;

	return 0;
}

int
sli_init(struct sli4 *sli4)
{
	if (sli4->params.has_extents) {
		efc_log_info(sli4, "extend allocation not supported\n");
		return -EIO;
	}

	sli4->features &= (~SLI4_REQFEAT_HLM);
	sli4->features &= (~SLI4_REQFEAT_RXSEQ);
	sli4->features &= (~SLI4_REQFEAT_RXRI);

	if (sli_request_features(sli4, &sli4->features, false))
		return -EIO;

	return 0;
}

int
sli_reset(struct sli4 *sli4)
{
	u32	i;

	if (!sli_fw_init(sli4)) {
		efc_log_crit(sli4, "FW initialization failed\n");
		return -EIO;
	}

	kfree(sli4->ext[0].base);
	sli4->ext[0].base = NULL;

	for (i = 0; i < SLI4_RSRC_MAX; i++) {
		kfree(sli4->ext[i].use_map);
		sli4->ext[i].use_map = NULL;
		sli4->ext[i].base = NULL;
	}

	return sli_get_config(sli4);
}

int
sli_fw_reset(struct sli4 *sli4)
{
	/*
	 * Firmware must be ready before issuing the reset.
	 */
	if (!sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC)) {
		efc_log_crit(sli4, "FW status is NOT ready\n");
		return -EIO;
	}

	/* Lancer uses PHYDEV_CONTROL */
	writel(SLI4_PHYDEV_CTRL_FRST, (sli4->reg[0] + SLI4_PHYDEV_CTRL_REG));

	/* wait for the FW to become ready after the reset */
	if (!sli_wait_for_fw_ready(sli4, SLI4_FW_READY_TIMEOUT_MSEC)) {
		efc_log_crit(sli4, "Failed to be ready after firmware reset\n");
		return -EIO;
	}
	return 0;
}

void
sli_teardown(struct sli4 *sli4)
{
	u32 i;

	kfree(sli4->ext[0].base);
	sli4->ext[0].base = NULL;

	for (i = 0; i < SLI4_RSRC_MAX; i++) {
		sli4->ext[i].base = NULL;

		kfree(sli4->ext[i].use_map);
		sli4->ext[i].use_map = NULL;
	}

	if (!sli_sliport_reset(sli4))
		efc_log_err(sli4, "FW deinitialization failed\n");

	dma_free_coherent(&sli4->pci->dev, sli4->vpd_data.size,
			  sli4->vpd_data.virt, sli4->vpd_data.phys);
	memset(&sli4->vpd_data, 0, sizeof(struct efc_dma));

	dma_free_coherent(&sli4->pci->dev, sli4->bmbx.size,
			  sli4->bmbx.virt, sli4->bmbx.phys);
	memset(&sli4->bmbx, 0, sizeof(struct efc_dma));
}

int
sli_callback(struct sli4 *sli4, enum sli4_callback which,
	     void *func, void *arg)
{
	if (!func) {
		efc_log_err(sli4, "bad parameter sli4=%p which=%#x func=%p\n",
			    sli4, which, func);
		return -EIO;
	}

	switch (which) {
	case SLI4_CB_LINK:
		sli4->link = func;
		sli4->link_arg = arg;
		break;
	default:
		efc_log_info(sli4, "unknown callback %#x\n", which);
		return -EIO;
	}

	return 0;
}

int
sli_eq_modify_delay(struct sli4 *sli4, struct sli4_queue *eq,
		    u32 num_eq, u32 shift, u32 delay_mult)
{
	sli_cmd_common_modify_eq_delay(sli4, sli4->bmbx.virt, eq, num_eq,
				       shift, delay_mult);

	if (sli_bmbx_command(sli4)) {
		efc_log_crit(sli4, "bootstrap mailbox write fail (MODIFY EQ DELAY)\n");
		return -EIO;
	}
	if (sli_res_sli_config(sli4, sli4->bmbx.virt)) {
		efc_log_err(sli4, "bad status MODIFY EQ DELAY\n");
		return -EIO;
	}

	return 0;
}

int
sli_resource_alloc(struct sli4 *sli4, enum sli4_resource rtype,
		   u32 *rid, u32 *index)
{
	int rc = 0;
	u32 size;
	u32 ext_idx;
	u32 item_idx;
	u32 position;

	*rid = U32_MAX;
	*index = U32_MAX;

	switch (rtype) {
	case SLI4_RSRC_VFI:
	case SLI4_RSRC_VPI:
	case SLI4_RSRC_RPI:
	case SLI4_RSRC_XRI:
		position =
		find_first_zero_bit(sli4->ext[rtype].use_map,
				    sli4->ext[rtype].map_size);
		if (position >= sli4->ext[rtype].map_size) {
			efc_log_err(sli4, "out of resource %d (alloc=%d)\n",
				    rtype, sli4->ext[rtype].n_alloc);
			rc = -EIO;
			break;
		}
		set_bit(position, sli4->ext[rtype].use_map);
		*index = position;

		size = sli4->ext[rtype].size;

		ext_idx = *index / size;
		item_idx   = *index % size;

		*rid = sli4->ext[rtype].base[ext_idx] + item_idx;

		sli4->ext[rtype].n_alloc++;
		break;
	default:
		rc = -EIO;
	}

	return rc;
}

int
sli_resource_free(struct sli4 *sli4, enum sli4_resource rtype, u32 rid)
{
	int rc = -EIO;
	u32 x;
	u32 size, *base;

	switch (rtype) {
	case SLI4_RSRC_VFI:
	case SLI4_RSRC_VPI:
	case SLI4_RSRC_RPI:
	case SLI4_RSRC_XRI:
		/*
		 * Figure out which extent contains the resource ID. I.e. find
		 * the extent such that
		 *   extent->base <= resource ID < extent->base + extent->size
		 */
		base = sli4->ext[rtype].base;
		size = sli4->ext[rtype].size;

		/*
		 * In the case of FW reset, this may be cleared
		 * but the force_free path will still attempt to
		 * free the resource. Prevent a NULL pointer access.
		 */
		if (!base)
			break;

		for (x = 0; x < sli4->ext[rtype].number; x++) {
			if ((rid < base[x] || (rid >= (base[x] + size))))
				continue;

			rid -= base[x];
			clear_bit((x * size) + rid, sli4->ext[rtype].use_map);
			rc = 0;
			break;
		}
		break;
	default:
		break;
	}

	return rc;
}

int
sli_resource_reset(struct sli4 *sli4, enum sli4_resource rtype)
{
	int rc = -EIO;
	u32 i;

	switch (rtype) {
	case SLI4_RSRC_VFI:
	case SLI4_RSRC_VPI:
	case SLI4_RSRC_RPI:
	case SLI4_RSRC_XRI:
		for (i = 0; i < sli4->ext[rtype].map_size; i++)
			clear_bit(i, sli4->ext[rtype].use_map);
		rc = 0;
		break;
	default:
		break;
	}

	return rc;
}

int sli_raise_ue(struct sli4 *sli4, u8 dump)
{
	u32 val = 0;

	if (dump == SLI4_FUNC_DESC_DUMP) {
		val = SLI4_PORT_CTRL_FDD | SLI4_PORT_CTRL_IP;
		writel(val, (sli4->reg[0] + SLI4_PORT_CTRL_REG));
	} else {
		val = SLI4_PHYDEV_CTRL_FRST;

		if (dump == SLI4_CHIP_LEVEL_DUMP)
			val |= SLI4_PHYDEV_CTRL_DD;
		writel(val, (sli4->reg[0] + SLI4_PHYDEV_CTRL_REG));
	}

	return 0;
}

int sli_dump_is_ready(struct sli4 *sli4)
{
	int rc = SLI4_DUMP_READY_STATUS_NOT_READY;
	u32 port_val;
	u32 bmbx_val;

	/*
	 * Ensure that the port is ready AND the mailbox is
	 * ready before signaling that the dump is ready to go.
	 */
	port_val = sli_reg_read_status(sli4);
	bmbx_val = readl(sli4->reg[0] + SLI4_BMBX_REG);

	if ((bmbx_val & SLI4_BMBX_RDY) &&
	    (port_val & SLI4_PORT_STATUS_RDY)) {
		if (port_val & SLI4_PORT_STATUS_DIP)
			rc = SLI4_DUMP_READY_STATUS_DD_PRESENT;
		else if (port_val & SLI4_PORT_STATUS_FDP)
			rc = SLI4_DUMP_READY_STATUS_FDB_PRESENT;
	}

	return rc;
}

bool sli_reset_required(struct sli4 *sli4)
{
	u32 val;

	val = sli_reg_read_status(sli4);
	return (val & SLI4_PORT_STATUS_RN);
}

int
sli_cmd_post_sgl_pages(struct sli4 *sli4, void *buf, u16 xri,
		       u32 xri_count, struct efc_dma *page0[],
		       struct efc_dma *page1[], struct efc_dma *dma)
{
	struct sli4_rqst_post_sgl_pages *post = NULL;
	u32 i;
	__le32 req_len;

	post = sli_config_cmd_init(sli4, buf,
				   SLI4_CFG_PYLD_LENGTH(post_sgl_pages), dma);
	if (!post)
		return -EIO;

	/* payload size calculation */
	/* 4 = xri_start + xri_count */
	/* xri_count = # of XRI's registered */
	/* sizeof(uint64_t) = physical address size */
	/* 2 = # of physical addresses per page set */
	req_len = cpu_to_le32(4 + (xri_count * (sizeof(uint64_t) * 2)));
	sli_cmd_fill_hdr(&post->hdr, SLI4_OPC_POST_SGL_PAGES, SLI4_SUBSYSTEM_FC,
			 CMD_V0, req_len);
	post->xri_start = cpu_to_le16(xri);
	post->xri_count = cpu_to_le16(xri_count);

	for (i = 0; i < xri_count; i++) {
		post->page_set[i].page0_low  =
				cpu_to_le32(lower_32_bits(page0[i]->phys));
		post->page_set[i].page0_high =
				cpu_to_le32(upper_32_bits(page0[i]->phys));
	}

	if (page1) {
		for (i = 0; i < xri_count; i++) {
			post->page_set[i].page1_low =
				cpu_to_le32(lower_32_bits(page1[i]->phys));
			post->page_set[i].page1_high =
				cpu_to_le32(upper_32_bits(page1[i]->phys));
		}
	}

	return 0;
}

int
sli_cmd_post_hdr_templates(struct sli4 *sli4, void *buf, struct efc_dma *dma,
			   u16 rpi, struct efc_dma *payload_dma)
{
	struct sli4_rqst_post_hdr_templates *req = NULL;
	uintptr_t phys = 0;
	u32 i = 0;
	u32 page_count, payload_size;

	page_count = sli_page_count(dma->size, SLI_PAGE_SIZE);

	payload_size = ((sizeof(struct sli4_rqst_post_hdr_templates) +
		(page_count * SZ_DMAADDR)) - sizeof(struct sli4_rqst_hdr));

	if (page_count > 16) {
		/*
		 * We can't fit more than 16 descriptors into an embedded mbox
		 * command, it has to be non-embedded
		 */
		payload_dma->size = payload_size;
		payload_dma->virt = dma_alloc_coherent(&sli4->pci->dev,
						       payload_dma->size,
					     &payload_dma->phys, GFP_DMA);
		if (!payload_dma->virt) {
			memset(payload_dma, 0, sizeof(struct efc_dma));
			efc_log_err(sli4, "mbox payload memory allocation fail\n");
			return -EIO;
		}
		req = sli_config_cmd_init(sli4, buf, payload_size, payload_dma);
	} else {
		req = sli_config_cmd_init(sli4, buf, payload_size, NULL);
	}

	if (!req)
		return -EIO;

	if (rpi == U16_MAX)
		rpi = sli4->ext[SLI4_RSRC_RPI].base[0];

	sli_cmd_fill_hdr(&req->hdr, SLI4_OPC_POST_HDR_TEMPLATES,
			 SLI4_SUBSYSTEM_FC, CMD_V0,
			 SLI4_RQST_PYLD_LEN(post_hdr_templates));

	req->rpi_offset = cpu_to_le16(rpi);
	req->page_count = cpu_to_le16(page_count);
	phys = dma->phys;
	for (i = 0; i < page_count; i++) {
		req->page_descriptor[i].low  = cpu_to_le32(lower_32_bits(phys));
		req->page_descriptor[i].high = cpu_to_le32(upper_32_bits(phys));

		phys += SLI_PAGE_SIZE;
	}

	return 0;
}

u32
sli_fc_get_rpi_requirements(struct sli4 *sli4, u32 n_rpi)
{
	u32 bytes = 0;

	/* Check if header templates needed */
	if (sli4->params.hdr_template_req)
		/* round up to a page */
		bytes = round_up(n_rpi * SLI4_HDR_TEMPLATE_SIZE, SLI_PAGE_SIZE);

	return bytes;
}

const char *
sli_fc_get_status_string(u32 status)
{
	static struct {
		u32 code;
		const char *label;
	} lookup[] = {
		{SLI4_FC_WCQE_STATUS_SUCCESS,		"SUCCESS"},
		{SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE,	"FCP_RSP_FAILURE"},
		{SLI4_FC_WCQE_STATUS_REMOTE_STOP,	"REMOTE_STOP"},
		{SLI4_FC_WCQE_STATUS_LOCAL_REJECT,	"LOCAL_REJECT"},
		{SLI4_FC_WCQE_STATUS_NPORT_RJT,		"NPORT_RJT"},
		{SLI4_FC_WCQE_STATUS_FABRIC_RJT,	"FABRIC_RJT"},
		{SLI4_FC_WCQE_STATUS_NPORT_BSY,		"NPORT_BSY"},
		{SLI4_FC_WCQE_STATUS_FABRIC_BSY,	"FABRIC_BSY"},
		{SLI4_FC_WCQE_STATUS_LS_RJT,		"LS_RJT"},
		{SLI4_FC_WCQE_STATUS_CMD_REJECT,	"CMD_REJECT"},
		{SLI4_FC_WCQE_STATUS_FCP_TGT_LENCHECK,	"FCP_TGT_LENCHECK"},
		{SLI4_FC_WCQE_STATUS_RQ_BUF_LEN_EXCEEDED, "BUF_LEN_EXCEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_BUF_NEEDED,
				"RQ_INSUFF_BUF_NEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_FRM_DISC, "RQ_INSUFF_FRM_DESC"},
		{SLI4_FC_WCQE_STATUS_RQ_DMA_FAILURE,	"RQ_DMA_FAILURE"},
		{SLI4_FC_WCQE_STATUS_FCP_RSP_TRUNCATE,	"FCP_RSP_TRUNCATE"},
		{SLI4_FC_WCQE_STATUS_DI_ERROR,		"DI_ERROR"},
		{SLI4_FC_WCQE_STATUS_BA_RJT,		"BA_RJT"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_NEEDED,
				"RQ_INSUFF_XRI_NEEDED"},
		{SLI4_FC_WCQE_STATUS_RQ_INSUFF_XRI_DISC, "INSUFF_XRI_DISC"},
		{SLI4_FC_WCQE_STATUS_RX_ERROR_DETECT,	"RX_ERROR_DETECT"},
		{SLI4_FC_WCQE_STATUS_RX_ABORT_REQUEST,	"RX_ABORT_REQUEST"},
		};
	u32 i;

	for (i = 0; i < ARRAY_SIZE(lookup); i++) {
		if (status == lookup[i].code)
			return lookup[i].label;
	}
	return "unknown";
}
