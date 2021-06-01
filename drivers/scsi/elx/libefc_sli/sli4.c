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
