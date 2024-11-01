/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2022 HiSilicon Limited. */
#ifndef QM_COMMON_H
#define QM_COMMON_H

#define QM_DBG_READ_LEN		256

struct qm_cqe {
	__le32 rsvd0;
	__le16 cmd_id;
	__le16 rsvd1;
	__le16 sq_head;
	__le16 sq_num;
	__le16 rsvd2;
	__le16 w7;
};

struct qm_eqe {
	__le32 dw0;
};

struct qm_aeqe {
	__le32 dw0;
};

struct qm_sqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le16 w8;
	__le16 rsvd0;
	__le16 pasid;
	__le16 w11;
	__le16 cq_num;
	__le16 w13;
	__le32 rsvd1;
};

struct qm_cqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le16 w8;
	__le16 rsvd0;
	__le16 pasid;
	__le16 w11;
	__le32 dw6;
	__le32 rsvd1;
};

struct qm_eqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le32 rsvd[2];
	__le32 dw6;
};

struct qm_aeqc {
	__le16 head;
	__le16 tail;
	__le32 base_l;
	__le32 base_h;
	__le32 dw3;
	__le32 rsvd[2];
	__le32 dw6;
};

static const char * const qm_s[] = {
	"init", "start", "close", "stop",
};

void *hisi_qm_ctx_alloc(struct hisi_qm *qm, size_t ctx_size,
			dma_addr_t *dma_addr);
void hisi_qm_ctx_free(struct hisi_qm *qm, size_t ctx_size,
		      const void *ctx_addr, dma_addr_t *dma_addr);
void hisi_qm_show_last_dfx_regs(struct hisi_qm *qm);
void hisi_qm_set_algqos_init(struct hisi_qm *qm);

#endif
