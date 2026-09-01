/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */

#include "qla_target.h"
/**
 * qla24xx_calc_iocbs() - Determine number of Command Type 3 and
 * Continuation Type 1 IOCBs to allocate.
 *
 * @vha: HA context
 * @dsds: number of data segment descriptors needed
 *
 * Returns the number of IOCB entries needed to store @dsds.
 */
static inline uint16_t
qla24xx_calc_iocbs(scsi_qla_host_t *vha, uint16_t dsds)
{
	uint16_t iocbs;

	iocbs = 1;
	if (dsds > 1) {
		iocbs += (dsds - 1) / 5;
		if ((dsds - 1) % 5)
			iocbs++;
	}
	return iocbs;
}

/*
 * qla2x00_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
static __inline__ uint16_t
qla2x00_debounce_register(volatile __le16 __iomem *addr)
{
	volatile uint16_t first;
	volatile uint16_t second;

	do {
		first = rd_reg_word(addr);
		barrier();
		cpu_relax();
		second = rd_reg_word(addr);
	} while (first != second);

	return (first);
}

/**
 * qla29xx_calc_iocbs() - Determine number of Command-Type and Continuation
 * IOCBs to allocate for the 29xx extended (128-byte) IOCB ring.
 * @vha: HA context
 * @dsds: number of data segment descriptors needed
 * @iocb_dsds: number of DSDs embedded in the first (command) IOCB.  The
 *	remaining DSDs ride on Continuation Type 1 Ext IOCBs which hold
 *	NUM_CONT1_DSDS (10) each.
 *
 * Returns the total number of IOCB entries needed to carry @dsds.
 */
static inline uint16_t
qla29xx_calc_iocbs(scsi_qla_host_t *vha, uint16_t dsds, uint8_t iocb_dsds)
{
	uint16_t iocbs = 1;

	if (dsds > iocb_dsds) {
		iocbs += (dsds - iocb_dsds) / NUM_CONT1_DSDS;
		if ((dsds - iocb_dsds) % NUM_CONT1_DSDS)
			iocbs++;
	}
	return iocbs;
}

/**
 * qla_req_entry_size() - request-ring entry stride.
 * @ha: HBA pointer
 *
 * Returns sizeof(struct request_ext) (128) on 29xx, sizeof(request_t) (64)
 * everywhere else.
 */
static inline size_t
qla_req_entry_size(struct qla_hw_data *ha)
{
	return IS_QLA29XX(ha) ? sizeof(struct request_ext) : sizeof(request_t);
}

/**
 * qla_rsp_entry_size() - response-ring entry stride.
 * @ha: HBA pointer
 *
 * Counterpart of qla_req_entry_size() for the response ring.
 */
static inline size_t
qla_rsp_entry_size(struct qla_hw_data *ha)
{
	return IS_QLA29XX(ha) ? sizeof(struct response_ext) : sizeof(response_t);
}

/**
 * qla_sts_cont_data_size() - status-continuation IOCB data payload size.
 * @ha: HBA pointer
 *
 * sts_cont_entry_t and struct sts_cont_entry_ext share the same header and
 * data offset; only the trailing data[] size differs (60 vs 124 bytes).
 * Returns that size so callers need not branch on the adapter type.
 */
static inline u32
qla_sts_cont_data_size(struct qla_hw_data *ha)
{
	return IS_QLA29XX(ha) ?
		sizeof_field(struct sts_cont_entry_ext, data) :
		sizeof_field(sts_cont_entry_t, data);
}

/**
 * qla_logio_set_vp_index() - write vp_index into a login/logout IOCB.
 * @ha: HBA pointer
 * @pkt: logio IOCB (logio_entry_24xx or logio_entry_24xx_ext)
 * @vp_idx: virtual port index
 *
 * vp_index widens from u8 (logio_entry_24xx) to __le16
 * (logio_entry_24xx_ext) on 29xx; write the field at the right width.
 */
static inline void
qla_logio_set_vp_index(struct qla_hw_data *ha, void *pkt, u16 vp_idx)
{
	if (IS_QLA29XX(ha))
		((struct logio_entry_24xx_ext *)pkt)->vp_index =
			cpu_to_le16(vp_idx);
	else
		((struct logio_entry_24xx *)pkt)->vp_index = vp_idx;
}

static inline u8
qla_calc_queue_count(u16 msix_count)
{
	/*
	 * Request/response queues are bounded by the MSI-X vector count less
	 * the mailbox vector.  These counters are u8, so a board advertising
	 * e.g. 257 vectors would truncate msix_count - 1 (256) to 0 and hand
	 * kzalloc_objs() a zero count (ZERO_SIZE_PTR), faulting on the first
	 * ha->req_q_map[0] store.  Clamp into [1, QLA_MAX_QUEUES - 1].
	 */
	return clamp_t(u16, msix_count - 1, 1, QLA_MAX_QUEUES - 1);
}

static inline void
qla2x00_poll(struct rsp_que *rsp)
{
	struct qla_hw_data *ha = rsp->hw;

	if (IS_P3P_TYPE(ha))
		qla82xx_poll(0, rsp);
	else
		ha->isp_ops->intr_handler(0, rsp);
}

static inline uint8_t *
host_to_fcp_swap(uint8_t *fcp, uint32_t bsize)
{
       uint32_t *ifcp = (uint32_t *) fcp;
       uint32_t *ofcp = (uint32_t *) fcp;
       uint32_t iter = bsize >> 2;

       for (; iter ; iter--)
               *ofcp++ = swab32(*ifcp++);

       return fcp;
}

static inline void
host_to_adap(uint8_t *src, uint8_t *dst, uint32_t bsize)
{
	uint32_t *isrc = (uint32_t *) src;
	__le32 *odest = (__le32 *) dst;
	uint32_t iter = bsize >> 2;

	for ( ; iter--; isrc++)
		*odest++ = cpu_to_le32(*isrc);
}

static inline void
qla2x00_clean_dsd_pool(struct qla_hw_data *ha, struct crc_context *ctx)
{
	struct dsd_dma *dsd, *tdsd;

	/* clean up allocated prev pool */
	list_for_each_entry_safe(dsd, tdsd, &ctx->dsd_list, list) {
		dma_pool_free(ha->dl_dma_pool, dsd->dsd_addr,
		    dsd->dsd_list_dma);
		list_del(&dsd->list);
		kfree(dsd);
	}
	INIT_LIST_HEAD(&ctx->dsd_list);
}

static inline void
qla2x00_set_fcport_disc_state(fc_port_t *fcport, int state)
{
	int old_val;
	uint8_t shiftbits, mask;
	uint8_t port_dstate_str_sz;

	/* This will have to change when the max no. of states > 16 */
	shiftbits = 4;
	mask = (1 << shiftbits) - 1;

	port_dstate_str_sz = sizeof(port_dstate_str) / sizeof(char *);
	fcport->disc_state = state;
	while (1) {
		old_val = atomic_read(&fcport->shadow_disc_state);
		if (old_val == atomic_cmpxchg(&fcport->shadow_disc_state,
		    old_val, (old_val << shiftbits) | state)) {
			ql_dbg(ql_dbg_disc, fcport->vha, 0x2134,
			    "FCPort %8phC disc_state transition: %s to %s - portid=%06x.\n",
			    fcport->port_name, (old_val & mask) < port_dstate_str_sz ?
				    port_dstate_str[old_val & mask] : "Unknown",
			    port_dstate_str[state], fcport->d_id.b24);
			return;
		}
	}
}

static inline int
qla2x00_hba_err_chk_enabled(srb_t *sp)
{
	/*
	 * Uncomment when corresponding SCSI changes are done.
	 *
	if (!sp->cmd->prot_chk)
		return 0;
	 *
	 */
	switch (scsi_get_prot_op(GET_CMD_SP(sp))) {
	case SCSI_PROT_READ_STRIP:
	case SCSI_PROT_WRITE_INSERT:
		if (ql2xenablehba_err_chk >= 1)
			return 1;
		break;
	case SCSI_PROT_READ_PASS:
	case SCSI_PROT_WRITE_PASS:
		if (ql2xenablehba_err_chk >= 2)
			return 1;
		break;
	case SCSI_PROT_READ_INSERT:
	case SCSI_PROT_WRITE_STRIP:
		return 1;
	}
	return 0;
}

static inline int
qla2x00_reset_active(scsi_qla_host_t *vha)
{
	scsi_qla_host_t *base_vha = pci_get_drvdata(vha->hw->pdev);

	/* Test appropriate base-vha and vha flags. */
	return test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) ||
	    test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags);
}

static inline int
qla2x00_chip_is_down(scsi_qla_host_t *vha)
{
	return (qla2x00_reset_active(vha) || !vha->hw->flags.fw_started);
}

static void qla2xxx_init_sp(srb_t *sp, scsi_qla_host_t *vha,
			    struct qla_qpair *qpair, fc_port_t *fcport)
{
	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->iocbs = 1;
	sp->vha = vha;
	sp->qpair = qpair;
	sp->cmd_type = TYPE_SRB;
	/* ref : INIT - normal flow */
	kref_init(&sp->cmd_kref);
	INIT_LIST_HEAD(&sp->elem);
}

static inline srb_t *
qla2xxx_get_qpair_sp(scsi_qla_host_t *vha, struct qla_qpair *qpair,
    fc_port_t *fcport, gfp_t flag)
{
	srb_t *sp = NULL;
	uint8_t bail;

	QLA_QPAIR_MARK_BUSY(qpair, bail);
	if (unlikely(bail))
		return NULL;

	sp = mempool_alloc(qpair->srb_mempool, flag);
	if (sp)
		qla2xxx_init_sp(sp, vha, qpair, fcport);
	else
		QLA_QPAIR_MARK_NOT_BUSY(qpair);
	return sp;
}

void qla2xxx_rel_done_warning(srb_t *sp, int res);
void qla2xxx_rel_free_warning(srb_t *sp);

static inline void
qla2xxx_rel_qpair_sp(struct qla_qpair *qpair, srb_t *sp)
{
	sp->qpair = NULL;
	sp->done = qla2xxx_rel_done_warning;
	sp->free = qla2xxx_rel_free_warning;
	mempool_free(sp, qpair->srb_mempool);
	QLA_QPAIR_MARK_NOT_BUSY(qpair);
}

static inline srb_t *
qla2x00_get_sp(scsi_qla_host_t *vha, fc_port_t *fcport, gfp_t flag)
{
	srb_t *sp = NULL;
	struct qla_qpair *qpair;

	if (unlikely(qla_vha_mark_busy(vha)))
		return NULL;

	qpair = vha->hw->base_qpair;
	sp = qla2xxx_get_qpair_sp(vha, qpair, fcport, flag);
	if (!sp)
		goto done;

	sp->vha = vha;
done:
	if (!sp)
		QLA_VHA_MARK_NOT_BUSY(vha);
	return sp;
}

static inline void
qla2x00_rel_sp(srb_t *sp)
{
	QLA_VHA_MARK_NOT_BUSY(sp->vha);
	qla2xxx_rel_qpair_sp(sp->qpair, sp);
}

static inline int
qla2x00_gid_list_size(struct qla_hw_data *ha)
{
	if (IS_QLAFX00(ha))
		return sizeof(uint32_t) * 32;
	else
		return sizeof(struct gid_list_info) * ha->max_fibre_devices;
}

static inline void
qla2x00_handle_mbx_completion(struct qla_hw_data *ha, int status)
{
	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		clear_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}
}

static inline void
qla2x00_set_retry_delay_timestamp(fc_port_t *fcport, uint16_t sts_qual)
{
	u8 scope;
	u16 qual;
#define SQ_SCOPE_MASK		0xc000 /* SAM-6 rev5 5.3.2 */
#define SQ_SCOPE_SHIFT		14
#define SQ_QUAL_MASK		0x3fff

#define SQ_MAX_WAIT_SEC		60 /* Max I/O hold off time in seconds. */
#define SQ_MAX_WAIT_TIME	(SQ_MAX_WAIT_SEC * 10) /* in 100ms. */

	if (!sts_qual) /* Common case. */
		return;

	scope = (sts_qual & SQ_SCOPE_MASK) >> SQ_SCOPE_SHIFT;
	/* Handle only scope 1 or 2, which is for I-T nexus. */
	if (scope != 1 && scope != 2)
		return;

	/* Skip processing, if retry delay timer is already in effect. */
	if (fcport->retry_delay_timestamp &&
	    time_before(jiffies, fcport->retry_delay_timestamp))
		return;

	qual = sts_qual & SQ_QUAL_MASK;
	if (qual < 1 || qual > 0x3fef)
		return;
	qual = min(qual, (u16)SQ_MAX_WAIT_TIME);

	/* qual is expressed in 100ms increments. */
	fcport->retry_delay_timestamp = jiffies + (qual * HZ / 10);

	ql_log(ql_log_warn, fcport->vha, 0x5101,
	       "%8phC: I/O throttling requested (status qualifier = %04xh), holding off I/Os for %ums.\n",
	       fcport->port_name, sts_qual, qual * 100);
}

static inline bool
qla_is_exch_offld_enabled(struct scsi_qla_host *vha)
{
	if (qla_ini_mode_enabled(vha) &&
	    (vha->ql2xiniexchg > FW_DEF_EXCHANGES_CNT))
		return true;
	else if (qla_tgt_mode_enabled(vha) &&
	    (vha->ql2xexchoffld > FW_DEF_EXCHANGES_CNT))
		return true;
	else if (qla_dual_mode_enabled(vha) &&
	    ((vha->ql2xiniexchg + vha->ql2xexchoffld) > FW_DEF_EXCHANGES_CNT))
		return true;
	else
		return false;
}

static inline void
qla_cpu_update(struct qla_qpair *qpair, uint16_t cpuid)
{
	qpair->cpuid = cpuid;

	if (!list_empty(&qpair->hints_list)) {
		struct qla_qpair_hint *h;

		list_for_each_entry(h, &qpair->hints_list, hint_elem)
			h->cpuid = qpair->cpuid;
	}
}

static inline struct qla_qpair_hint *
qla_qpair_to_hint(struct qla_tgt *tgt, struct qla_qpair *qpair)
{
	struct qla_qpair_hint *h;
	u16 i;

	for (i = 0; i < tgt->ha->max_qpairs + 1; i++) {
		h = &tgt->qphints[i];
		if (h->qpair == qpair)
			return h;
	}

	return NULL;
}

static inline void
qla_83xx_start_iocbs(struct qla_qpair *qpair)
{
	struct req_que *req = qpair->req;
	struct qla_hw_data *ha = qpair->vha->hw;

	/*
	 * 29xx uses the 128-byte-strided extended request ring; advance the
	 * matching ring_ext_ptr so the next IOCB allocator sees the correct
	 * slot.  All other 83xx-family generations (83xx/27xx/28xx) keep the
	 * 64-byte ring_ptr.
	 */
	req->ring_index++;
	if (IS_QLA29XX(ha)) {
		if (req->ring_index == req->length) {
			req->ring_index = 0;
			req->ring_ext_ptr = req->ring_ext;
		} else {
			req->ring_ext_ptr++;
		}
	} else {
		if (req->ring_index == req->length) {
			req->ring_index = 0;
			req->ring_ptr = req->ring;
		} else {
			req->ring_ptr++;
		}
	}

	wrt_reg_dword(req->req_q_in, req->ring_index);
}

/**
 * qla_rsp_ring_advance() - Advance the response queue consumer pointer
 * to the next IOCB slot, handling both 24xx (64-byte) and 29xx (128-byte)
 * ring strides.
 *
 * On 29xx, ring_ext_ptr is the authoritative slot pointer (correct 128-byte
 * pitch) and ring_ptr is kept in sync as a response_t view of the same slot
 * so existing 24xx-shaped reads (rsp->ring_ptr->signature,
 * (struct sts_entry_24xx *)rsp->ring_ptr, etc.) keep working unchanged; the
 * first 64 bytes of struct response_ext are layout-compatible with response_t.
 */
static inline void
qla_rsp_ring_advance(struct rsp_que *rsp)
{
	rsp->ring_index++;
	if (rsp->ring_index == rsp->length) {
		rsp->ring_index = 0;
		rsp->ring_ptr = rsp->ring;
		if (rsp->hw && IS_QLA29XX(rsp->hw))
			rsp->ring_ext_ptr = rsp->ring_ext;
	} else if (rsp->hw && IS_QLA29XX(rsp->hw)) {
		rsp->ring_ext_ptr++;
		rsp->ring_ptr = (response_t *)rsp->ring_ext_ptr;
	} else {
		rsp->ring_ptr++;
	}
}

/**
 * qla_req_ring_slot() - return the current request-ring producer slot.
 * @ha: HBA pointer
 * @req: request queue
 *
 * On 29xx the firmware-visible ring uses 128-byte-strided entries
 * referenced by ring_ext_ptr; on earlier adapters the 64-byte ring
 * referenced by ring_ptr is used.  The returned pointer is
 * layout-compatible with request_t for common header writes; callers
 * needing 29xx-specific fields should cast to struct request_ext.
 */
static inline void *
qla_req_ring_slot(struct qla_hw_data *ha, struct req_que *req)
{
	return IS_QLA29XX(ha) ? (void *)req->ring_ext_ptr
			      : (void *)req->ring_ptr;
}

/**
 * qla_req_ring_advance() - advance request-ring producer pointer.
 * @ha: HBA pointer
 * @req: request queue
 *
 * Mirrors qla_rsp_ring_advance().  Does NOT publish the new producer
 * index to firmware; callers that need to do so should follow with a
 * wrt_reg_dword or qla_83xx_start_iocbs().
 */
static inline void
qla_req_ring_advance(struct qla_hw_data *ha, struct req_que *req)
{
	req->ring_index++;
	if (IS_QLA29XX(ha)) {
		if (req->ring_index == req->length) {
			req->ring_index = 0;
			req->ring_ext_ptr = req->ring_ext;
		} else {
			req->ring_ext_ptr++;
		}
	} else {
		if (req->ring_index == req->length) {
			req->ring_index = 0;
			req->ring_ptr = req->ring;
		} else {
			req->ring_ptr++;
		}
	}
}

/**
 * qla_rsp_ring_rewind_to() - Restore the response queue consumer pointer
 * to a previously-observed slot (used when we need to defer processing an
 * IOCB whose continuation entries have not yet arrived).
 * @rsp: response queue
 * @pkt: 64-byte view of the slot to rewind to (captured from a prior read
 *       of rsp->ring_ptr)
 * @idx: matching ring_index value (also captured before the advance)
 *
 * On 29xx, pkt was originally obtained as (response_t *)rsp->ring_ext_ptr,
 * so casting back to struct response_ext * recovers the 128-byte-stride slot
 * pointer.
 */
static inline void
qla_rsp_ring_rewind_to(struct rsp_que *rsp, response_t *pkt, uint16_t idx)
{
	rsp->ring_ptr = pkt;
	rsp->ring_index = idx;
	if (rsp->hw && IS_QLA29XX(rsp->hw))
		rsp->ring_ext_ptr = (struct response_ext *)pkt;
}

static inline int
qla2xxx_get_fc4_priority(struct scsi_qla_host *vha)
{
	uint32_t data;

	data =
	    ((uint8_t *)vha->hw->nvram)[NVRAM_DUAL_FCP_NVME_FLAG_OFFSET];


	return (data >> 6) & BIT_0 ? FC4_PRIORITY_FCP : FC4_PRIORITY_NVME;
}

enum {
	RESOURCE_NONE,
	RESOURCE_IOCB = BIT_0,
	RESOURCE_EXCH = BIT_1,  /* exchange */
	RESOURCE_FORCE = BIT_2,
	RESOURCE_HA = BIT_3,
};

static inline int
qla_get_fw_resources(struct qla_qpair *qp, struct iocb_resource *iores)
{
	u16 iocbs_used, i;
	u16 exch_used;
	struct qla_hw_data *ha = qp->hw;

	if (!ql2xenforce_iocb_limit) {
		iores->res_type = RESOURCE_NONE;
		return 0;
	}
	if (iores->res_type & RESOURCE_FORCE)
		goto force;

	if ((iores->iocb_cnt + qp->fwres.iocbs_used) >= qp->fwres.iocbs_qp_limit) {
		/* no need to acquire qpair lock. It's just rough calculation */
		iocbs_used = ha->base_qpair->fwres.iocbs_used;
		for (i = 0; i < ha->max_qpairs; i++) {
			if (ha->queue_pair_map[i])
				iocbs_used += ha->queue_pair_map[i]->fwres.iocbs_used;
		}

		if ((iores->iocb_cnt + iocbs_used) >= qp->fwres.iocbs_limit) {
			iores->res_type = RESOURCE_NONE;
			return -ENOSPC;
		}
	}

	if (iores->res_type & RESOURCE_EXCH) {
		exch_used = ha->base_qpair->fwres.exch_used;
		for (i = 0; i < ha->max_qpairs; i++) {
			if (ha->queue_pair_map[i])
				exch_used += ha->queue_pair_map[i]->fwres.exch_used;
		}

		if ((exch_used + iores->exch_cnt) >= qp->fwres.exch_limit) {
			iores->res_type = RESOURCE_NONE;
			return -ENOSPC;
		}
	}

	if (ql2xenforce_iocb_limit == 2) {
		if ((iores->iocb_cnt + atomic_read(&ha->fwres.iocb_used)) >=
		    ha->fwres.iocb_limit) {
			iores->res_type = RESOURCE_NONE;
			return -ENOSPC;
		}

		if (iores->res_type & RESOURCE_EXCH) {
			if ((iores->exch_cnt + atomic_read(&ha->fwres.exch_used)) >=
			    ha->fwres.exch_limit) {
				iores->res_type = RESOURCE_NONE;
				return -ENOSPC;
			}
		}
	}

force:
	qp->fwres.iocbs_used += iores->iocb_cnt;
	qp->fwres.exch_used += iores->exch_cnt;
	if (ql2xenforce_iocb_limit == 2) {
		atomic_add(iores->iocb_cnt, &ha->fwres.iocb_used);
		atomic_add(iores->exch_cnt, &ha->fwres.exch_used);
		iores->res_type |= RESOURCE_HA;
	}
	return 0;
}

/*
 * decrement to zero.  This routine will not decrement below zero
 * @v:  pointer of type atomic_t
 * @amount: amount to decrement from v
 */
static void qla_atomic_dtz(atomic_t *v, int amount)
{
	int c, old, dec;

	c = atomic_read(v);
	for (;;) {
		dec = c - amount;
		if (unlikely(dec < 0))
			dec = 0;

		old = atomic_cmpxchg((v), c, dec);
		if (likely(old == c))
			break;
		c = old;
	}
}

static inline void
qla_put_fw_resources(struct qla_qpair *qp, struct iocb_resource *iores)
{
	struct qla_hw_data *ha = qp->hw;

	if (iores->res_type & RESOURCE_HA) {
		if (iores->res_type & RESOURCE_IOCB)
			qla_atomic_dtz(&ha->fwres.iocb_used, iores->iocb_cnt);

		if (iores->res_type & RESOURCE_EXCH)
			qla_atomic_dtz(&ha->fwres.exch_used, iores->exch_cnt);
	}

	if (iores->res_type & RESOURCE_IOCB) {
		if (qp->fwres.iocbs_used >= iores->iocb_cnt) {
			qp->fwres.iocbs_used -= iores->iocb_cnt;
		} else {
			/* should not happen */
			qp->fwres.iocbs_used = 0;
		}
	}

	if (iores->res_type & RESOURCE_EXCH) {
		if (qp->fwres.exch_used >= iores->exch_cnt) {
			qp->fwres.exch_used -= iores->exch_cnt;
		} else {
			/* should not happen */
			qp->fwres.exch_used = 0;
		}
	}
	iores->res_type = RESOURCE_NONE;
}

#define ISP_REG_DISCONNECT 0xffffffffU
/**************************************************************************
 * qla2x00_isp_reg_stat
 *
 * Description:
 *        Read the host status register of ISP before aborting the command.
 *
 * Input:
 *       ha = pointer to host adapter structure.
 *
 *
 * Returns:
 *       Either true or false.
 *
 * Note: Return true if there is register disconnect.
 **************************************************************************/
static inline
uint32_t qla2x00_isp_reg_stat(struct qla_hw_data *ha)
{
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &ha->iobase->isp82;

	if (IS_P3P_TYPE(ha))
		return ((rd_reg_dword(&reg82->host_int)) == ISP_REG_DISCONNECT);
	else
		return ((rd_reg_dword(&reg->host_status)) ==
			ISP_REG_DISCONNECT);
}

static inline
bool qla_pci_disconnected(struct scsi_qla_host *vha,
			  struct device_reg_24xx __iomem *reg)
{
	uint32_t stat;
	bool ret = false;

	stat = rd_reg_dword(&reg->host_status);
	if (stat == 0xffffffff) {
		ql_log(ql_log_info, vha, 0x8041,
		       "detected PCI disconnect.\n");
		qla_schedule_eeh_work(vha);
		ret = true;
	}
	return ret;
}

static inline bool
fcport_is_smaller(fc_port_t *fcport)
{
	if (wwn_to_u64(fcport->port_name) <
		wwn_to_u64(fcport->vha->port_name))
		return true;
	else
		return false;
}

static inline bool
fcport_is_bigger(fc_port_t *fcport)
{
	return !fcport_is_smaller(fcport);
}

static inline struct qla_qpair *
qla_mapq_nvme_select_qpair(struct qla_hw_data *ha, struct qla_qpair *qpair)
{
	int cpuid = raw_smp_processor_id();

	if (qpair->cpuid != cpuid &&
	    ha->qp_cpu_map[cpuid]) {
		qpair = ha->qp_cpu_map[cpuid];
	}
	return qpair;
}

static inline void
qla_mapq_init_qp_cpu_map(struct qla_hw_data *ha,
			 struct qla_msix_entry *msix,
			 struct qla_qpair *qpair)
{
	const struct cpumask *mask;
	unsigned int cpu;

	if (!ha->qp_cpu_map)
		return;
	mask = pci_irq_get_affinity(ha->pdev, msix->vector_base0);
	if (!mask)
		return;
	qpair->cpuid = cpumask_first(mask);
	for_each_cpu(cpu, mask) {
		ha->qp_cpu_map[cpu] = qpair;
	}
	msix->cpuid = qpair->cpuid;
	qpair->cpu_mapped = true;
}

static inline void
qla_mapq_free_qp_cpu_map(struct qla_hw_data *ha)
{
	if (ha->qp_cpu_map) {
		kfree(ha->qp_cpu_map);
		ha->qp_cpu_map = NULL;
	}
}

static inline int qla_mapq_alloc_qp_cpu_map(struct qla_hw_data *ha)
{
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	if (!ha->qp_cpu_map) {
		ha->qp_cpu_map = kzalloc_objs(struct qla_qpair *, nr_cpu_ids);
		if (!ha->qp_cpu_map) {
			ql_log(ql_log_fatal, vha, 0x0180,
			       "Unable to allocate memory for qp_cpu_map ptrs.\n");
			return -1;
		}
	}
	return 0;
}

static inline bool val_is_in_range(u32 val, u32 start, u32 end)
{
	if (val >= start && val <= end)
		return true;
	else
		return false;
}

/*
 * Common fields extracted from FWI2 status IOCBs.  Populated once so
 * callers avoid duplicated IS_QLA29XX() branches for every field access.
 */
struct qla_sts_fwi2 {
	u8  *data;
	u32 data_sz;
	u16 scsi_status;
	u16 sts_qual;
	u32 sense_len;
	u32 rsp_data_len;
	u32 rsp_residual_count;
};

static inline void
qla_sts_fwi2_extract(struct qla_hw_data *ha, void *pkt,
		      struct qla_sts_fwi2 *sf)
{
	if (IS_QLA29XX(ha)) {
		struct sts_entry_24xx_ext *s = pkt;

		sf->scsi_status = le16_to_cpu(s->u2.scsi_status);
		sf->sts_qual = le16_to_cpu(s->u2.retry_delay_timer);
		sf->sense_len = le32_to_cpu(s->u2.sense_len);
		sf->rsp_data_len = le32_to_cpu(s->u2.rsp_data_len_ndma);
		sf->rsp_residual_count = le32_to_cpu(s->u2.rsp_residual_count);
		sf->data = s->u2.data;
		sf->data_sz = sizeof(s->u2.data);
		host_to_fcp_swap(s->u2.data, sizeof(s->u2.data));
		host_to_fcp_swap(s->act_dif, sizeof(s->act_dif));
		host_to_fcp_swap(s->exp_dif, sizeof(s->exp_dif));
	} else {
		struct sts_entry_24xx *s = pkt;

		sf->scsi_status = le16_to_cpu(s->scsi_status);
		sf->sts_qual = le16_to_cpu(s->status_qualifier);
		sf->sense_len = le32_to_cpu(s->sense_len);
		sf->rsp_data_len = le32_to_cpu(s->rsp_data_len);
		sf->rsp_residual_count = le32_to_cpu(s->rsp_residual_count);
		sf->data = s->data;
		sf->data_sz = sizeof(s->data);
		host_to_fcp_swap(s->data, sizeof(s->data));
	}
}

/*
 * qla_els_set_vp_sof() - write the vp_index / sof_type pair into an ELS
 *    pass-through IOCB (els_entry_24xx{,_ext}).
 *
 * Both layouts have the same 16-bit slot at offset 14, but it is encoded
 * differently:
 *   - 24xx: separate u8 vp_index + u8 sof_type with EST_SOFI3 (1 << 4)
 *   - 29xx: __le16 vp_index_sof with bits [8:0]=VP index, [15:12]=SOF type
 *           and ELS_EXT_EST_SOFI3
 * so this is the single point in the driver that knows about that
 * encoding split.
 */
static inline void
qla_els_set_vp_sof(struct scsi_qla_host *vha, void *pkt, u16 vp_idx)
{
	if (IS_QLA29XX(vha->hw)) {
		struct els_entry_24xx_ext *ext = pkt;

		ext->vp_index_sof =
		    qla_ext_build_vp_sof(vp_idx, ELS_EXT_EST_SOFI3);
	} else {
		struct els_entry_24xx *e = pkt;

		e->vp_index = vp_idx;
		e->sof_type = EST_SOFI3;
	}
}
