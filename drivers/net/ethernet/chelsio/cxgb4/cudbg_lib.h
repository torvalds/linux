/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 */

#ifndef __CUDBG_LIB_H__
#define __CUDBG_LIB_H__

int cudbg_collect_reg_dump(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_fw_devlog(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err);
int cudbg_collect_cim_la(struct cudbg_init *pdbg_init,
			 struct cudbg_buffer *dbg_buff,
			 struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ma_la(struct cudbg_init *pdbg_init,
			    struct cudbg_buffer *dbg_buff,
			    struct cudbg_error *cudbg_err);
int cudbg_collect_cim_qcfg(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_tp0(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_tp1(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_ulp(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_sge0(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_sge1(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_ibq_ncsi(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_ulp0(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_ulp1(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_ulp2(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_ulp3(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_sge(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_cim_obq_ncsi(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_edc0_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_edc1_meminfo(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_mc0_meminfo(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_mc1_meminfo(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_rss(struct cudbg_init *pdbg_init,
		      struct cudbg_buffer *dbg_buff,
		      struct cudbg_error *cudbg_err);
int cudbg_collect_rss_vf_config(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err);
int cudbg_collect_tp_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_path_mtu(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_pm_stats(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_hw_sched(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_sge_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_ulprx_la(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_tp_la(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err);
int cudbg_collect_meminfo(struct cudbg_init *pdbg_init,
			  struct cudbg_buffer *dbg_buff,
			  struct cudbg_error *cudbg_err);
int cudbg_collect_cim_pif_la(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err);
int cudbg_collect_clk_info(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_obq_sge_rx_q0(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err);
int cudbg_collect_obq_sge_rx_q1(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err);
int cudbg_collect_pcie_indirect(struct cudbg_init *pdbg_init,
				struct cudbg_buffer *dbg_buff,
				struct cudbg_error *cudbg_err);
int cudbg_collect_pm_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_tid(struct cudbg_init *pdbg_init,
		      struct cudbg_buffer *dbg_buff,
		      struct cudbg_error *cudbg_err);
int cudbg_collect_pcie_config(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_dump_context(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_mps_tcam(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_vpd_data(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_le_tcam(struct cudbg_init *pdbg_init,
			  struct cudbg_buffer *dbg_buff,
			  struct cudbg_error *cudbg_err);
int cudbg_collect_cctrl(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err);
int cudbg_collect_ma_indirect(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_ulptx_la(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_up_cim_indirect(struct cudbg_init *pdbg_init,
				  struct cudbg_buffer *dbg_buff,
				  struct cudbg_error *cudbg_err);
int cudbg_collect_pbt_tables(struct cudbg_init *pdbg_init,
			     struct cudbg_buffer *dbg_buff,
			     struct cudbg_error *cudbg_err);
int cudbg_collect_mbox_log(struct cudbg_init *pdbg_init,
			   struct cudbg_buffer *dbg_buff,
			   struct cudbg_error *cudbg_err);
int cudbg_collect_hma_indirect(struct cudbg_init *pdbg_init,
			       struct cudbg_buffer *dbg_buff,
			       struct cudbg_error *cudbg_err);
int cudbg_collect_hma_meminfo(struct cudbg_init *pdbg_init,
			      struct cudbg_buffer *dbg_buff,
			      struct cudbg_error *cudbg_err);
int cudbg_collect_qdesc(struct cudbg_init *pdbg_init,
			struct cudbg_buffer *dbg_buff,
			struct cudbg_error *cudbg_err);

struct cudbg_entity_hdr *cudbg_get_entity_hdr(void *outbuf, int i);
void cudbg_align_debug_buffer(struct cudbg_buffer *dbg_buff,
			      struct cudbg_entity_hdr *entity_hdr);
u32 cudbg_cim_obq_size(struct adapter *padap, int qid);
int cudbg_dump_context_size(struct adapter *padap);

int cudbg_fill_meminfo(struct adapter *padap,
		       struct cudbg_meminfo *meminfo_buff);
void cudbg_fill_le_tcam_info(struct adapter *padap,
			     struct cudbg_tcam *tcam_region);
void cudbg_fill_qdesc_num_and_size(const struct adapter *padap,
				   u32 *num, u32 *size);

static inline u32 cudbg_uld_txq_to_qtype(u32 uld)
{
	switch (uld) {
	case CXGB4_TX_OFLD:
		return CUDBG_QTYPE_OFLD_TXQ;
	case CXGB4_TX_CRYPTO:
		return CUDBG_QTYPE_CRYPTO_TXQ;
	}

	return CUDBG_QTYPE_UNKNOWN;
}

static inline u32 cudbg_uld_rxq_to_qtype(u32 uld)
{
	switch (uld) {
	case CXGB4_ULD_RDMA:
		return CUDBG_QTYPE_RDMA_RXQ;
	case CXGB4_ULD_ISCSI:
		return CUDBG_QTYPE_ISCSI_RXQ;
	case CXGB4_ULD_ISCSIT:
		return CUDBG_QTYPE_ISCSIT_RXQ;
	case CXGB4_ULD_CRYPTO:
		return CUDBG_QTYPE_CRYPTO_RXQ;
	case CXGB4_ULD_TLS:
		return CUDBG_QTYPE_TLS_RXQ;
	}

	return CUDBG_QTYPE_UNKNOWN;
}

static inline u32 cudbg_uld_flq_to_qtype(u32 uld)
{
	switch (uld) {
	case CXGB4_ULD_RDMA:
		return CUDBG_QTYPE_RDMA_FLQ;
	case CXGB4_ULD_ISCSI:
		return CUDBG_QTYPE_ISCSI_FLQ;
	case CXGB4_ULD_ISCSIT:
		return CUDBG_QTYPE_ISCSIT_FLQ;
	case CXGB4_ULD_CRYPTO:
		return CUDBG_QTYPE_CRYPTO_FLQ;
	case CXGB4_ULD_TLS:
		return CUDBG_QTYPE_TLS_FLQ;
	}

	return CUDBG_QTYPE_UNKNOWN;
}

static inline u32 cudbg_uld_ciq_to_qtype(u32 uld)
{
	switch (uld) {
	case CXGB4_ULD_RDMA:
		return CUDBG_QTYPE_RDMA_CIQ;
	}

	return CUDBG_QTYPE_UNKNOWN;
}

static inline void cudbg_fill_qdesc_txq(const struct sge_txq *txq,
					enum cudbg_qdesc_qtype type,
					struct cudbg_qdesc_entry *entry)
{
	entry->qtype = type;
	entry->qid = txq->cntxt_id;
	entry->desc_size = sizeof(struct tx_desc);
	entry->num_desc = txq->size;
	entry->data_size = txq->size * sizeof(struct tx_desc);
	memcpy(entry->data, txq->desc, entry->data_size);
}

static inline void cudbg_fill_qdesc_rxq(const struct sge_rspq *rxq,
					enum cudbg_qdesc_qtype type,
					struct cudbg_qdesc_entry *entry)
{
	entry->qtype = type;
	entry->qid = rxq->cntxt_id;
	entry->desc_size = rxq->iqe_len;
	entry->num_desc = rxq->size;
	entry->data_size = rxq->size * rxq->iqe_len;
	memcpy(entry->data, rxq->desc, entry->data_size);
}

static inline void cudbg_fill_qdesc_flq(const struct sge_fl *flq,
					enum cudbg_qdesc_qtype type,
					struct cudbg_qdesc_entry *entry)
{
	entry->qtype = type;
	entry->qid = flq->cntxt_id;
	entry->desc_size = sizeof(__be64);
	entry->num_desc = flq->size;
	entry->data_size = flq->size * sizeof(__be64);
	memcpy(entry->data, flq->desc, entry->data_size);
}

static inline
struct cudbg_qdesc_entry *cudbg_next_qdesc(struct cudbg_qdesc_entry *e)
{
	return (struct cudbg_qdesc_entry *)
	       ((u8 *)e + sizeof(*e) + e->data_size);
}
#endif /* __CUDBG_LIB_H__ */
