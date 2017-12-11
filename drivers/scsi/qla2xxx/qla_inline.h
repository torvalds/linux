/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#include "qla_target.h"
/**
 * qla24xx_calc_iocbs() - Determine number of Command Type 3 and
 * Continuation Type 1 IOCBs to allocate.
 *
 * @dsds: number of data segment decriptors needed
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
qla2x00_debounce_register(volatile uint16_t __iomem *addr)
{
	volatile uint16_t first;
	volatile uint16_t second;

	do {
		first = RD_REG_WORD(addr);
		barrier();
		cpu_relax();
		second = RD_REG_WORD(addr);
	} while (first != second);

	return (first);
}

static inline void
qla2x00_poll(struct rsp_que *rsp)
{
	unsigned long flags;
	struct qla_hw_data *ha = rsp->hw;
	local_irq_save(flags);
	if (IS_P3P_TYPE(ha))
		qla82xx_poll(0, rsp);
	else
		ha->isp_ops->intr_handler(0, rsp);
	local_irq_restore(flags);
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
qla2x00_set_reserved_loop_ids(struct qla_hw_data *ha)
{
	int i;

	if (IS_FWI2_CAPABLE(ha))
		return;

	for (i = 0; i < SNS_FIRST_LOOP_ID; i++)
		set_bit(i, ha->loop_id_map);
	set_bit(MANAGEMENT_SERVER, ha->loop_id_map);
	set_bit(BROADCAST, ha->loop_id_map);
}

static inline int
qla2x00_is_reserved_id(scsi_qla_host_t *vha, uint16_t loop_id)
{
	struct qla_hw_data *ha = vha->hw;
	if (IS_FWI2_CAPABLE(ha))
		return (loop_id > NPH_LAST_HANDLE);

	return ((loop_id > ha->max_loop_id && loop_id < SNS_FIRST_LOOP_ID) ||
	    loop_id == MANAGEMENT_SERVER || loop_id == BROADCAST);
}

static inline void
qla2x00_clear_loop_id(fc_port_t *fcport) {
	struct qla_hw_data *ha = fcport->vha->hw;

	if (fcport->loop_id == FC_NO_LOOP_ID ||
	    qla2x00_is_reserved_id(fcport->vha, fcport->loop_id))
		return;

	clear_bit(fcport->loop_id, ha->loop_id_map);
	fcport->loop_id = FC_NO_LOOP_ID;
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
qla2x00_set_fcport_state(fc_port_t *fcport, int state)
{
	int old_state;

	old_state = atomic_read(&fcport->state);
	atomic_set(&fcport->state, state);

	/* Don't print state transitions during initial allocation of fcport */
	if (old_state && old_state != state) {
		ql_dbg(ql_dbg_disc, fcport->vha, 0x207d,
		    "FCPort %8phC state transitioned from %s to %s - "
			"portid=%02x%02x%02x.\n", fcport->port_name,
		    port_state_str[old_state], port_state_str[state],
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa);
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

static inline srb_t *
qla2xxx_get_qpair_sp(struct qla_qpair *qpair, fc_port_t *fcport, gfp_t flag)
{
	srb_t *sp = NULL;
	uint8_t bail;

	QLA_QPAIR_MARK_BUSY(qpair, bail);
	if (unlikely(bail))
		return NULL;

	sp = mempool_alloc(qpair->srb_mempool, flag);
	if (!sp)
		goto done;

	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->iocbs = 1;
	sp->vha = qpair->vha;
done:
	if (!sp)
		QLA_QPAIR_MARK_NOT_BUSY(qpair);
	return sp;
}

static inline void
qla2xxx_rel_qpair_sp(struct qla_qpair *qpair, srb_t *sp)
{
	mempool_free(sp, qpair->srb_mempool);
	QLA_QPAIR_MARK_NOT_BUSY(qpair);
}

static inline srb_t *
qla2x00_get_sp(scsi_qla_host_t *vha, fc_port_t *fcport, gfp_t flag)
{
	srb_t *sp = NULL;
	uint8_t bail;

	QLA_VHA_MARK_BUSY(vha, bail);
	if (unlikely(bail))
		return NULL;

	sp = mempool_alloc(vha->hw->srb_mempool, flag);
	if (!sp)
		goto done;

	memset(sp, 0, sizeof(*sp));
	sp->fcport = fcport;
	sp->cmd_type = TYPE_SRB;
	sp->iocbs = 1;
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
	mempool_free(sp, sp->vha->hw->srb_mempool);
}

static inline void
qla2x00_init_timer(srb_t *sp, unsigned long tmo)
{
	timer_setup(&sp->u.iocb_cmd.timer, qla2x00_sp_timeout, 0);
	sp->u.iocb_cmd.timer.expires = jiffies + tmo * HZ;
	add_timer(&sp->u.iocb_cmd.timer);
	sp->free = qla2x00_sp_free;
	if (IS_QLAFX00(sp->vha->hw) && (sp->type == SRB_FXIOCB_DCMD))
		init_completion(&sp->u.iocb_cmd.u.fxiocb.fxiocb_comp);
	if (sp->type == SRB_ELS_DCMD)
		init_completion(&sp->u.iocb_cmd.u.els_logo.comp);
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
qla2x00_set_retry_delay_timestamp(fc_port_t *fcport, uint16_t retry_delay)
{
	if (retry_delay)
		fcport->retry_delay_timestamp = jiffies +
		    (retry_delay * HZ / 10);
}

static inline bool
qla_is_exch_offld_enabled(struct scsi_qla_host *vha)
{
	if (qla_ini_mode_enabled(vha) &&
	    (ql2xiniexchg > FW_DEF_EXCHANGES_CNT))
		return true;
	else if (qla_tgt_mode_enabled(vha) &&
	    (ql2xexchoffld > FW_DEF_EXCHANGES_CNT))
		return true;
	else if (qla_dual_mode_enabled(vha) &&
	    ((ql2xiniexchg + ql2xexchoffld) > FW_DEF_EXCHANGES_CNT))
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

	req->ring_index++;
	if (req->ring_index == req->length) {
		req->ring_index = 0;
		req->ring_ptr = req->ring;
	} else
		req->ring_ptr++;

	WRT_REG_DWORD(req->req_q_in, req->ring_index);
}
