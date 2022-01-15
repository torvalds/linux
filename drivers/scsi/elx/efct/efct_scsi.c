// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_hw.h"

#define enable_tsend_auto_resp(efct)	1
#define enable_treceive_auto_resp(efct)	0

#define SCSI_IOFMT "[%04x][i:%04x t:%04x h:%04x]"

#define scsi_io_printf(io, fmt, ...) \
	efc_log_debug(io->efct, "[%s]" SCSI_IOFMT fmt, \
		io->node->display_name, io->instance_index,\
		io->init_task_tag, io->tgt_task_tag, io->hw_tag, ##__VA_ARGS__)

#define EFCT_LOG_ENABLE_SCSI_TRACE(efct)                \
		(((efct) != NULL) ? (((efct)->logmask & (1U << 2)) != 0) : 0)

#define scsi_io_trace(io, fmt, ...) \
	do { \
		if (EFCT_LOG_ENABLE_SCSI_TRACE(io->efct)) \
			scsi_io_printf(io, fmt, ##__VA_ARGS__); \
	} while (0)

struct efct_io *
efct_scsi_io_alloc(struct efct_node *node)
{
	struct efct *efct;
	struct efct_xport *xport;
	struct efct_io *io;
	unsigned long flags;

	efct = node->efct;

	xport = efct->xport;

	spin_lock_irqsave(&node->active_ios_lock, flags);

	io = efct_io_pool_io_alloc(efct->xport->io_pool);
	if (!io) {
		efc_log_err(efct, "IO alloc Failed\n");
		atomic_add_return(1, &xport->io_alloc_failed_count);
		return NULL;
	}

	/* initialize refcount */
	kref_init(&io->ref);
	io->release = _efct_scsi_io_free;

	/* set generic fields */
	io->efct = efct;
	io->node = node;
	kref_get(&node->ref);

	/* set type and name */
	io->io_type = EFCT_IO_TYPE_IO;
	io->display_name = "scsi_io";

	io->cmd_ini = false;
	io->cmd_tgt = true;

	/* Add to node's active_ios list */
	INIT_LIST_HEAD(&io->list_entry);
	list_add(&io->list_entry, &node->active_ios);

	spin_unlock_irqrestore(&node->active_ios_lock, flags);

	return io;
}

void
_efct_scsi_io_free(struct kref *arg)
{
	struct efct_io *io = container_of(arg, struct efct_io, ref);
	struct efct *efct = io->efct;
	struct efct_node *node = io->node;
	unsigned long flags = 0;

	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);

	if (io->io_free) {
		efc_log_err(efct, "IO already freed.\n");
		return;
	}

	spin_lock_irqsave(&node->active_ios_lock, flags);
	list_del_init(&io->list_entry);
	spin_unlock_irqrestore(&node->active_ios_lock, flags);

	kref_put(&node->ref, node->release);
	io->node = NULL;
	efct_io_pool_io_free(efct->xport->io_pool, io);
}

void
efct_scsi_io_free(struct efct_io *io)
{
	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);
	WARN_ON(!refcount_read(&io->ref.refcount));
	kref_put(&io->ref, io->release);
}

static void
efct_target_io_cb(struct efct_hw_io *hio, u32 length, int status,
		  u32 ext_status, void *app)
{
	u32 flags = 0;
	struct efct_io *io = app;
	struct efct *efct;
	enum efct_scsi_io_status scsi_stat = EFCT_SCSI_STATUS_GOOD;
	efct_scsi_io_cb_t cb;

	if (!io || !io->efct) {
		pr_err("%s: IO can not be NULL\n", __func__);
		return;
	}

	scsi_io_trace(io, "status x%x ext_status x%x\n", status, ext_status);

	efct = io->efct;

	io->transferred += length;

	if (!io->scsi_tgt_cb) {
		efct_scsi_check_pending(efct);
		return;
	}

	/* Call target server completion */
	cb = io->scsi_tgt_cb;

	/* Clear the callback before invoking the callback */
	io->scsi_tgt_cb = NULL;

	/* if status was good, and auto-good-response was set,
	 * then callback target-server with IO_CMPL_RSP_SENT,
	 * otherwise send IO_CMPL
	 */
	if (status == 0 && io->auto_resp)
		flags |= EFCT_SCSI_IO_CMPL_RSP_SENT;
	else
		flags |= EFCT_SCSI_IO_CMPL;

	switch (status) {
	case SLI4_FC_WCQE_STATUS_SUCCESS:
		scsi_stat = EFCT_SCSI_STATUS_GOOD;
		break;
	case SLI4_FC_WCQE_STATUS_DI_ERROR:
		if (ext_status & SLI4_FC_DI_ERROR_GE)
			scsi_stat = EFCT_SCSI_STATUS_DIF_GUARD_ERR;
		else if (ext_status & SLI4_FC_DI_ERROR_AE)
			scsi_stat = EFCT_SCSI_STATUS_DIF_APP_TAG_ERROR;
		else if (ext_status & SLI4_FC_DI_ERROR_RE)
			scsi_stat = EFCT_SCSI_STATUS_DIF_REF_TAG_ERROR;
		else
			scsi_stat = EFCT_SCSI_STATUS_DIF_UNKNOWN_ERROR;
		break;
	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
		switch (ext_status) {
		case SLI4_FC_LOCAL_REJECT_INVALID_RELOFFSET:
		case SLI4_FC_LOCAL_REJECT_ABORT_REQUESTED:
			scsi_stat = EFCT_SCSI_STATUS_ABORTED;
			break;
		case SLI4_FC_LOCAL_REJECT_INVALID_RPI:
			scsi_stat = EFCT_SCSI_STATUS_NEXUS_LOST;
			break;
		case SLI4_FC_LOCAL_REJECT_NO_XRI:
			scsi_stat = EFCT_SCSI_STATUS_NO_IO;
			break;
		default:
			/*we have seen 0x0d(TX_DMA_FAILED err)*/
			scsi_stat = EFCT_SCSI_STATUS_ERROR;
			break;
		}
		break;

	case SLI4_FC_WCQE_STATUS_TARGET_WQE_TIMEOUT:
		/* target IO timed out */
		scsi_stat = EFCT_SCSI_STATUS_TIMEDOUT_AND_ABORTED;
		break;

	case SLI4_FC_WCQE_STATUS_SHUTDOWN:
		/* Target IO cancelled by HW */
		scsi_stat = EFCT_SCSI_STATUS_SHUTDOWN;
		break;

	default:
		scsi_stat = EFCT_SCSI_STATUS_ERROR;
		break;
	}

	cb(io, scsi_stat, flags, io->scsi_tgt_cb_arg);

	efct_scsi_check_pending(efct);
}

static int
efct_scsi_build_sgls(struct efct_hw *hw, struct efct_hw_io *hio,
		     struct efct_scsi_sgl *sgl, u32 sgl_count,
		     enum efct_hw_io_type type)
{
	int rc;
	u32 i;
	struct efct *efct = hw->os;

	/* Initialize HW SGL */
	rc = efct_hw_io_init_sges(hw, hio, type);
	if (rc) {
		efc_log_err(efct, "efct_hw_io_init_sges failed: %d\n", rc);
		return -EIO;
	}

	for (i = 0; i < sgl_count; i++) {
		/* Add data SGE */
		rc = efct_hw_io_add_sge(hw, hio, sgl[i].addr, sgl[i].len);
		if (rc) {
			efc_log_err(efct, "add sge failed cnt=%d rc=%d\n",
				    sgl_count, rc);
			return rc;
		}
	}

	return 0;
}

static void efc_log_sgl(struct efct_io *io)
{
	struct efct_hw_io *hio = io->hio;
	struct sli4_sge *data = NULL;
	u32 *dword = NULL;
	u32 i;
	u32 n_sge;

	scsi_io_trace(io, "def_sgl at 0x%x 0x%08x\n",
		      upper_32_bits(hio->def_sgl.phys),
		      lower_32_bits(hio->def_sgl.phys));
	n_sge = (hio->sgl == &hio->def_sgl) ? hio->n_sge : hio->def_sgl_count;
	for (i = 0, data = hio->def_sgl.virt; i < n_sge; i++, data++) {
		dword = (u32 *)data;

		scsi_io_trace(io, "SGL %2d 0x%08x 0x%08x 0x%08x 0x%08x\n",
			      i, dword[0], dword[1], dword[2], dword[3]);

		if (dword[2] & (1U << 31))
			break;
	}
}

static void
efct_scsi_check_pending_async_cb(struct efct_hw *hw, int status,
				 u8 *mqe, void *arg)
{
	struct efct_io *io = arg;

	if (io) {
		efct_hw_done_t cb = io->hw_cb;

		if (!io->hw_cb)
			return;

		io->hw_cb = NULL;
		(cb)(io->hio, 0, SLI4_FC_WCQE_STATUS_DISPATCH_ERROR, 0, io);
	}
}

static int
efct_scsi_io_dispatch_hw_io(struct efct_io *io, struct efct_hw_io *hio)
{
	int rc = 0;
	struct efct *efct = io->efct;

	/* Got a HW IO;
	 * update ini/tgt_task_tag with HW IO info and dispatch
	 */
	io->hio = hio;
	if (io->cmd_tgt)
		io->tgt_task_tag = hio->indicator;
	else if (io->cmd_ini)
		io->init_task_tag = hio->indicator;
	io->hw_tag = hio->reqtag;

	hio->eq = io->hw_priv;

	/* Copy WQ steering */
	switch (io->wq_steering) {
	case EFCT_SCSI_WQ_STEERING_CLASS >> EFCT_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = EFCT_HW_WQ_STEERING_CLASS;
		break;
	case EFCT_SCSI_WQ_STEERING_REQUEST >> EFCT_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = EFCT_HW_WQ_STEERING_REQUEST;
		break;
	case EFCT_SCSI_WQ_STEERING_CPU >> EFCT_SCSI_WQ_STEERING_SHIFT:
		hio->wq_steering = EFCT_HW_WQ_STEERING_CPU;
		break;
	}

	switch (io->io_type) {
	case EFCT_IO_TYPE_IO:
		rc = efct_scsi_build_sgls(&efct->hw, io->hio,
					  io->sgl, io->sgl_count, io->hio_type);
		if (rc)
			break;

		if (EFCT_LOG_ENABLE_SCSI_TRACE(efct))
			efc_log_sgl(io);

		if (io->app_id)
			io->iparam.fcp_tgt.app_id = io->app_id;

		io->iparam.fcp_tgt.vpi = io->node->vpi;
		io->iparam.fcp_tgt.rpi = io->node->rpi;
		io->iparam.fcp_tgt.s_id = io->node->port_fc_id;
		io->iparam.fcp_tgt.d_id = io->node->node_fc_id;
		io->iparam.fcp_tgt.xmit_len = io->wire_len;

		rc = efct_hw_io_send(&io->efct->hw, io->hio_type, io->hio,
				     &io->iparam, io->hw_cb, io);
		break;
	default:
		scsi_io_printf(io, "Unknown IO type=%d\n", io->io_type);
		rc = -EIO;
		break;
	}
	return rc;
}

static int
efct_scsi_io_dispatch_no_hw_io(struct efct_io *io)
{
	int rc;

	switch (io->io_type) {
	case EFCT_IO_TYPE_ABORT: {
		struct efct_hw_io *hio_to_abort = NULL;

		hio_to_abort = io->io_to_abort->hio;

		if (!hio_to_abort) {
			/*
			 * If "IO to abort" does not have an
			 * associated HW IO, immediately make callback with
			 * success. The command must have been sent to
			 * the backend, but the data phase has not yet
			 * started, so we don't have a HW IO.
			 *
			 * Note: since the backend shims should be
			 * taking a reference on io_to_abort, it should not
			 * be possible to have been completed and freed by
			 * the backend before the abort got here.
			 */
			scsi_io_printf(io, "IO: not active\n");
			((efct_hw_done_t)io->hw_cb)(io->hio, 0,
					SLI4_FC_WCQE_STATUS_SUCCESS, 0, io);
			rc = 0;
			break;
		}

		/* HW IO is valid, abort it */
		scsi_io_printf(io, "aborting\n");
		rc = efct_hw_io_abort(&io->efct->hw, hio_to_abort,
				      io->send_abts, io->hw_cb, io);
		if (rc) {
			int status = SLI4_FC_WCQE_STATUS_SUCCESS;
			efct_hw_done_t cb = io->hw_cb;

			if (rc != -ENOENT && rc != -EINPROGRESS) {
				status = -1;
				scsi_io_printf(io, "Failed to abort IO rc=%d\n",
					       rc);
			}
			cb(io->hio, 0, status, 0, io);
			rc = 0;
		}

		break;
	}
	default:
		scsi_io_printf(io, "Unknown IO type=%d\n", io->io_type);
		rc = -EIO;
		break;
	}
	return rc;
}

static struct efct_io *
efct_scsi_dispatch_pending(struct efct *efct)
{
	struct efct_xport *xport = efct->xport;
	struct efct_io *io = NULL;
	struct efct_hw_io *hio;
	unsigned long flags = 0;
	int status;

	spin_lock_irqsave(&xport->io_pending_lock, flags);

	if (!list_empty(&xport->io_pending_list)) {
		io = list_first_entry(&xport->io_pending_list, struct efct_io,
				      io_pending_link);
		list_del_init(&io->io_pending_link);
	}

	if (!io) {
		spin_unlock_irqrestore(&xport->io_pending_lock, flags);
		return NULL;
	}

	if (io->io_type == EFCT_IO_TYPE_ABORT) {
		hio = NULL;
	} else {
		hio = efct_hw_io_alloc(&efct->hw);
		if (!hio) {
			/*
			 * No HW IO available.Put IO back on
			 * the front of pending list
			 */
			list_add(&xport->io_pending_list, &io->io_pending_link);
			io = NULL;
		} else {
			hio->eq = io->hw_priv;
		}
	}

	/* Must drop the lock before dispatching the IO */
	spin_unlock_irqrestore(&xport->io_pending_lock, flags);

	if (!io)
		return NULL;

	/*
	 * We pulled an IO off the pending list,
	 * and either got an HW IO or don't need one
	 */
	atomic_sub_return(1, &xport->io_pending_count);
	if (!hio)
		status = efct_scsi_io_dispatch_no_hw_io(io);
	else
		status = efct_scsi_io_dispatch_hw_io(io, hio);
	if (status) {
		/*
		 * Invoke the HW callback, but do so in the
		 * separate execution context,provided by the
		 * NOP mailbox completion processing context
		 * by using efct_hw_async_call()
		 */
		if (efct_hw_async_call(&efct->hw,
				       efct_scsi_check_pending_async_cb, io)) {
			efc_log_debug(efct, "call hw async failed\n");
		}
	}

	return io;
}

void
efct_scsi_check_pending(struct efct *efct)
{
	struct efct_xport *xport = efct->xport;
	struct efct_io *io = NULL;
	int count = 0;
	unsigned long flags = 0;
	int dispatch = 0;

	/* Guard against recursion */
	if (atomic_add_return(1, &xport->io_pending_recursing)) {
		/* This function is already running.  Decrement and return. */
		atomic_sub_return(1, &xport->io_pending_recursing);
		return;
	}

	while (efct_scsi_dispatch_pending(efct))
		count++;

	if (count) {
		atomic_sub_return(1, &xport->io_pending_recursing);
		return;
	}

	/*
	 * If nothing was removed from the list,
	 * we might be in a case where we need to abort an
	 * active IO and the abort is on the pending list.
	 * Look for an abort we can dispatch.
	 */

	spin_lock_irqsave(&xport->io_pending_lock, flags);

	list_for_each_entry(io, &xport->io_pending_list, io_pending_link) {
		if (io->io_type == EFCT_IO_TYPE_ABORT && io->io_to_abort->hio) {
			/* This IO has a HW IO, so it is
			 * active.  Dispatch the abort.
			 */
			dispatch = 1;
			list_del_init(&io->io_pending_link);
			atomic_sub_return(1, &xport->io_pending_count);
			break;
		}
	}

	spin_unlock_irqrestore(&xport->io_pending_lock, flags);

	if (dispatch) {
		if (efct_scsi_io_dispatch_no_hw_io(io)) {
			if (efct_hw_async_call(&efct->hw,
				efct_scsi_check_pending_async_cb, io)) {
				efc_log_debug(efct, "hw async failed\n");
			}
		}
	}

	atomic_sub_return(1, &xport->io_pending_recursing);
}

int
efct_scsi_io_dispatch(struct efct_io *io, void *cb)
{
	struct efct_hw_io *hio;
	struct efct *efct = io->efct;
	struct efct_xport *xport = efct->xport;
	unsigned long flags = 0;

	io->hw_cb = cb;

	/*
	 * if this IO already has a HW IO, then this is either
	 * not the first phase of the IO. Send it to the HW.
	 */
	if (io->hio)
		return efct_scsi_io_dispatch_hw_io(io, io->hio);

	/*
	 * We don't already have a HW IO associated with the IO. First check
	 * the pending list. If not empty, add IO to the tail and process the
	 * pending list.
	 */
	spin_lock_irqsave(&xport->io_pending_lock, flags);
	if (!list_empty(&xport->io_pending_list)) {
		/*
		 * If this is a low latency request,
		 * the put at the front of the IO pending
		 * queue, otherwise put it at the end of the queue.
		 */
		if (io->low_latency) {
			INIT_LIST_HEAD(&io->io_pending_link);
			list_add(&xport->io_pending_list, &io->io_pending_link);
		} else {
			INIT_LIST_HEAD(&io->io_pending_link);
			list_add_tail(&io->io_pending_link,
				      &xport->io_pending_list);
		}
		spin_unlock_irqrestore(&xport->io_pending_lock, flags);
		atomic_add_return(1, &xport->io_pending_count);
		atomic_add_return(1, &xport->io_total_pending);

		/* process pending list */
		efct_scsi_check_pending(efct);
		return 0;
	}
	spin_unlock_irqrestore(&xport->io_pending_lock, flags);

	/*
	 * We don't have a HW IO associated with the IO and there's nothing
	 * on the pending list. Attempt to allocate a HW IO and dispatch it.
	 */
	hio = efct_hw_io_alloc(&io->efct->hw);
	if (!hio) {
		/* Couldn't get a HW IO. Save this IO on the pending list */
		spin_lock_irqsave(&xport->io_pending_lock, flags);
		INIT_LIST_HEAD(&io->io_pending_link);
		list_add_tail(&io->io_pending_link, &xport->io_pending_list);
		spin_unlock_irqrestore(&xport->io_pending_lock, flags);

		atomic_add_return(1, &xport->io_total_pending);
		atomic_add_return(1, &xport->io_pending_count);
		return 0;
	}

	/* We successfully allocated a HW IO; dispatch to HW */
	return efct_scsi_io_dispatch_hw_io(io, hio);
}

int
efct_scsi_io_dispatch_abort(struct efct_io *io, void *cb)
{
	struct efct *efct = io->efct;
	struct efct_xport *xport = efct->xport;
	unsigned long flags = 0;

	io->hw_cb = cb;

	/*
	 * For aborts, we don't need a HW IO, but we still want
	 * to pass through the pending list to preserve ordering.
	 * Thus, if the pending list is not empty, add this abort
	 * to the pending list and process the pending list.
	 */
	spin_lock_irqsave(&xport->io_pending_lock, flags);
	if (!list_empty(&xport->io_pending_list)) {
		INIT_LIST_HEAD(&io->io_pending_link);
		list_add_tail(&io->io_pending_link, &xport->io_pending_list);
		spin_unlock_irqrestore(&xport->io_pending_lock, flags);
		atomic_add_return(1, &xport->io_pending_count);
		atomic_add_return(1, &xport->io_total_pending);

		/* process pending list */
		efct_scsi_check_pending(efct);
		return 0;
	}
	spin_unlock_irqrestore(&xport->io_pending_lock, flags);

	/* nothing on pending list, dispatch abort */
	return efct_scsi_io_dispatch_no_hw_io(io);
}

static inline int
efct_scsi_xfer_data(struct efct_io *io, u32 flags,
		    struct efct_scsi_sgl *sgl, u32 sgl_count, u64 xwire_len,
		    enum efct_hw_io_type type, int enable_ar,
		    efct_scsi_io_cb_t cb, void *arg)
{
	struct efct *efct;
	size_t residual = 0;

	io->sgl_count = sgl_count;

	efct = io->efct;

	scsi_io_trace(io, "%s wire_len %llu\n",
		      (type == EFCT_HW_IO_TARGET_READ) ? "send" : "recv",
		      xwire_len);

	io->hio_type = type;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	residual = io->exp_xfer_len - io->transferred;
	io->wire_len = (xwire_len < residual) ? xwire_len : residual;
	residual = (xwire_len - io->wire_len);

	memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = io->transferred;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	/* if this is the last data phase and there is no residual, enable
	 * auto-good-response
	 */
	if (enable_ar && (flags & EFCT_SCSI_LAST_DATAPHASE) && residual == 0 &&
	    ((io->transferred + io->wire_len) == io->exp_xfer_len) &&
	    (!(flags & EFCT_SCSI_NO_AUTO_RESPONSE))) {
		io->iparam.fcp_tgt.flags |= SLI4_IO_AUTO_GOOD_RESPONSE;
		io->auto_resp = true;
	} else {
		io->auto_resp = false;
	}

	/* save this transfer length */
	io->xfer_req = io->wire_len;

	/* Adjust the transferred count to account for overrun
	 * when the residual is calculated in efct_scsi_send_resp
	 */
	io->transferred += residual;

	/* Adjust the SGL size if there is overrun */

	if (residual) {
		struct efct_scsi_sgl  *sgl_ptr = &io->sgl[sgl_count - 1];

		while (residual) {
			size_t len = sgl_ptr->len;

			if (len > residual) {
				sgl_ptr->len = len - residual;
				residual = 0;
			} else {
				sgl_ptr->len = 0;
				residual -= len;
				io->sgl_count--;
			}
			sgl_ptr--;
		}
	}

	/* Set latency and WQ steering */
	io->low_latency = (flags & EFCT_SCSI_LOW_LATENCY) != 0;
	io->wq_steering = (flags & EFCT_SCSI_WQ_STEERING_MASK) >>
				EFCT_SCSI_WQ_STEERING_SHIFT;
	io->wq_class = (flags & EFCT_SCSI_WQ_CLASS_MASK) >>
				EFCT_SCSI_WQ_CLASS_SHIFT;

	if (efct->xport) {
		struct efct_xport *xport = efct->xport;

		if (type == EFCT_HW_IO_TARGET_READ) {
			xport->fcp_stats.input_requests++;
			xport->fcp_stats.input_bytes += xwire_len;
		} else if (type == EFCT_HW_IO_TARGET_WRITE) {
			xport->fcp_stats.output_requests++;
			xport->fcp_stats.output_bytes += xwire_len;
		}
	}
	return efct_scsi_io_dispatch(io, efct_target_io_cb);
}

int
efct_scsi_send_rd_data(struct efct_io *io, u32 flags,
		       struct efct_scsi_sgl *sgl, u32 sgl_count, u64 len,
		       efct_scsi_io_cb_t cb, void *arg)
{
	return efct_scsi_xfer_data(io, flags, sgl, sgl_count,
				   len, EFCT_HW_IO_TARGET_READ,
				   enable_tsend_auto_resp(io->efct), cb, arg);
}

int
efct_scsi_recv_wr_data(struct efct_io *io, u32 flags,
		       struct efct_scsi_sgl *sgl, u32 sgl_count, u64 len,
		       efct_scsi_io_cb_t cb, void *arg)
{
	return efct_scsi_xfer_data(io, flags, sgl, sgl_count, len,
				   EFCT_HW_IO_TARGET_WRITE,
				   enable_treceive_auto_resp(io->efct), cb, arg);
}

int
efct_scsi_send_resp(struct efct_io *io, u32 flags,
		    struct efct_scsi_cmd_resp *rsp,
		    efct_scsi_io_cb_t cb, void *arg)
{
	struct efct *efct;
	int residual;
	/* Always try auto resp */
	bool auto_resp = true;
	u8 scsi_status = 0;
	u16 scsi_status_qualifier = 0;
	u8 *sense_data = NULL;
	u32 sense_data_length = 0;

	efct = io->efct;

	if (rsp) {
		scsi_status = rsp->scsi_status;
		scsi_status_qualifier = rsp->scsi_status_qualifier;
		sense_data = rsp->sense_data;
		sense_data_length = rsp->sense_data_length;
		residual = rsp->residual;
	} else {
		residual = io->exp_xfer_len - io->transferred;
	}

	io->wire_len = 0;
	io->hio_type = EFCT_HW_IO_TARGET_RSP;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = 0;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	/* Set low latency queueing request */
	io->low_latency = (flags & EFCT_SCSI_LOW_LATENCY) != 0;
	io->wq_steering = (flags & EFCT_SCSI_WQ_STEERING_MASK) >>
				EFCT_SCSI_WQ_STEERING_SHIFT;
	io->wq_class = (flags & EFCT_SCSI_WQ_CLASS_MASK) >>
				EFCT_SCSI_WQ_CLASS_SHIFT;

	if (scsi_status != 0 || residual || sense_data_length) {
		struct fcp_resp_with_ext *fcprsp = io->rspbuf.virt;
		u8 *sns_data;

		if (!fcprsp) {
			efc_log_err(efct, "NULL response buffer\n");
			return -EIO;
		}

		sns_data = (u8 *)io->rspbuf.virt + sizeof(*fcprsp);

		auto_resp = false;

		memset(fcprsp, 0, sizeof(*fcprsp));

		io->wire_len += sizeof(*fcprsp);

		fcprsp->resp.fr_status = scsi_status;
		fcprsp->resp.fr_retry_delay =
			cpu_to_be16(scsi_status_qualifier);

		/* set residual status if necessary */
		if (residual != 0) {
			/* FCP: if data transferred is less than the
			 * amount expected, then this is an underflow.
			 * If data transferred would have been greater
			 * than the amount expected this is an overflow
			 */
			if (residual > 0) {
				fcprsp->resp.fr_flags |= FCP_RESID_UNDER;
				fcprsp->ext.fr_resid =	cpu_to_be32(residual);
			} else {
				fcprsp->resp.fr_flags |= FCP_RESID_OVER;
				fcprsp->ext.fr_resid = cpu_to_be32(-residual);
			}
		}

		if (EFCT_SCSI_SNS_BUF_VALID(sense_data) && sense_data_length) {
			if (sense_data_length > SCSI_SENSE_BUFFERSIZE) {
				efc_log_err(efct, "Sense exceeds max size.\n");
				return -EIO;
			}

			fcprsp->resp.fr_flags |= FCP_SNS_LEN_VAL;
			memcpy(sns_data, sense_data, sense_data_length);
			fcprsp->ext.fr_sns_len = cpu_to_be32(sense_data_length);
			io->wire_len += sense_data_length;
		}

		io->sgl[0].addr = io->rspbuf.phys;
		io->sgl[0].dif_addr = 0;
		io->sgl[0].len = io->wire_len;
		io->sgl_count = 1;
	}

	if (auto_resp)
		io->iparam.fcp_tgt.flags |= SLI4_IO_AUTO_GOOD_RESPONSE;

	return efct_scsi_io_dispatch(io, efct_target_io_cb);
}

static int
efct_target_bls_resp_cb(struct efct_hw_io *hio,	u32 length, int status,
			u32 ext_status, void *app)
{
	struct efct_io *io = app;
	struct efct *efct;
	enum efct_scsi_io_status bls_status;

	efct = io->efct;

	/* BLS isn't really a "SCSI" concept, but use SCSI status */
	if (status) {
		io_error_log(io, "s=%#x x=%#x\n", status, ext_status);
		bls_status = EFCT_SCSI_STATUS_ERROR;
	} else {
		bls_status = EFCT_SCSI_STATUS_GOOD;
	}

	if (io->bls_cb) {
		efct_scsi_io_cb_t bls_cb = io->bls_cb;
		void *bls_cb_arg = io->bls_cb_arg;

		io->bls_cb = NULL;
		io->bls_cb_arg = NULL;

		/* invoke callback */
		bls_cb(io, bls_status, 0, bls_cb_arg);
	}

	efct_scsi_check_pending(efct);
	return 0;
}

static int
efct_target_send_bls_resp(struct efct_io *io,
			  efct_scsi_io_cb_t cb, void *arg)
{
	struct efct_node *node = io->node;
	struct sli_bls_params *bls = &io->iparam.bls;
	struct efct *efct = node->efct;
	struct fc_ba_acc *acc;
	int rc;

	/* fill out IO structure with everything needed to send BA_ACC */
	memset(&io->iparam, 0, sizeof(io->iparam));
	bls->ox_id = io->init_task_tag;
	bls->rx_id = io->abort_rx_id;
	bls->vpi = io->node->vpi;
	bls->rpi = io->node->rpi;
	bls->s_id = U32_MAX;
	bls->d_id = io->node->node_fc_id;
	bls->rpi_registered = true;

	acc = (void *)bls->payload;
	acc->ba_ox_id = cpu_to_be16(bls->ox_id);
	acc->ba_rx_id = cpu_to_be16(bls->rx_id);
	acc->ba_high_seq_cnt = cpu_to_be16(U16_MAX);

	/* generic io fields have already been populated */

	/* set type and BLS-specific fields */
	io->io_type = EFCT_IO_TYPE_BLS_RESP;
	io->display_name = "bls_rsp";
	io->hio_type = EFCT_HW_BLS_ACC;
	io->bls_cb = cb;
	io->bls_cb_arg = arg;

	/* dispatch IO */
	rc = efct_hw_bls_send(efct, FC_RCTL_BA_ACC, bls,
			      efct_target_bls_resp_cb, io);
	return rc;
}

static int efct_bls_send_rjt_cb(struct efct_hw_io *hio, u32 length, int status,
				u32 ext_status, void *app)
{
	struct efct_io *io = app;

	efct_scsi_io_free(io);
	return 0;
}

struct efct_io *
efct_bls_send_rjt(struct efct_io *io, struct fc_frame_header *hdr)
{
	struct efct_node *node = io->node;
	struct sli_bls_params *bls = &io->iparam.bls;
	struct efct *efct = node->efct;
	struct fc_ba_rjt *acc;
	int rc;

	/* fill out BLS Response-specific fields */
	io->io_type = EFCT_IO_TYPE_BLS_RESP;
	io->display_name = "ba_rjt";
	io->hio_type = EFCT_HW_BLS_RJT;
	io->init_task_tag = be16_to_cpu(hdr->fh_ox_id);

	/* fill out iparam fields */
	memset(&io->iparam, 0, sizeof(io->iparam));
	bls->ox_id = be16_to_cpu(hdr->fh_ox_id);
	bls->rx_id = be16_to_cpu(hdr->fh_rx_id);
	bls->vpi = io->node->vpi;
	bls->rpi = io->node->rpi;
	bls->s_id = U32_MAX;
	bls->d_id = io->node->node_fc_id;
	bls->rpi_registered = true;

	acc = (void *)bls->payload;
	acc->br_reason = ELS_RJT_UNAB;
	acc->br_explan = ELS_EXPL_NONE;

	rc = efct_hw_bls_send(efct, FC_RCTL_BA_RJT, bls, efct_bls_send_rjt_cb,
			      io);
	if (rc) {
		efc_log_err(efct, "efct_scsi_io_dispatch() failed: %d\n", rc);
		efct_scsi_io_free(io);
		io = NULL;
	}
	return io;
}

int
efct_scsi_send_tmf_resp(struct efct_io *io,
			enum efct_scsi_tmf_resp rspcode,
			u8 addl_rsp_info[3],
			efct_scsi_io_cb_t cb, void *arg)
{
	int rc;
	struct {
		struct fcp_resp_with_ext rsp_ext;
		struct fcp_resp_rsp_info info;
	} *fcprsp;
	u8 fcp_rspcode;

	io->wire_len = 0;

	switch (rspcode) {
	case EFCT_SCSI_TMF_FUNCTION_COMPLETE:
		fcp_rspcode = FCP_TMF_CMPL;
		break;
	case EFCT_SCSI_TMF_FUNCTION_SUCCEEDED:
	case EFCT_SCSI_TMF_FUNCTION_IO_NOT_FOUND:
		fcp_rspcode = FCP_TMF_CMPL;
		break;
	case EFCT_SCSI_TMF_FUNCTION_REJECTED:
		fcp_rspcode = FCP_TMF_REJECTED;
		break;
	case EFCT_SCSI_TMF_INCORRECT_LOGICAL_UNIT_NUMBER:
		fcp_rspcode = FCP_TMF_INVALID_LUN;
		break;
	case EFCT_SCSI_TMF_SERVICE_DELIVERY:
		fcp_rspcode = FCP_TMF_FAILED;
		break;
	default:
		fcp_rspcode = FCP_TMF_REJECTED;
		break;
	}

	io->hio_type = EFCT_HW_IO_TARGET_RSP;

	io->scsi_tgt_cb = cb;
	io->scsi_tgt_cb_arg = arg;

	if (io->tmf_cmd == EFCT_SCSI_TMF_ABORT_TASK) {
		rc = efct_target_send_bls_resp(io, cb, arg);
		return rc;
	}

	/* populate the FCP TMF response */
	fcprsp = io->rspbuf.virt;
	memset(fcprsp, 0, sizeof(*fcprsp));

	fcprsp->rsp_ext.resp.fr_flags |= FCP_SNS_LEN_VAL;

	if (addl_rsp_info) {
		memcpy(fcprsp->info._fr_resvd, addl_rsp_info,
		       sizeof(fcprsp->info._fr_resvd));
	}
	fcprsp->info.rsp_code = fcp_rspcode;

	io->wire_len = sizeof(*fcprsp);

	fcprsp->rsp_ext.ext.fr_rsp_len =
			cpu_to_be32(sizeof(struct fcp_resp_rsp_info));

	io->sgl[0].addr = io->rspbuf.phys;
	io->sgl[0].dif_addr = 0;
	io->sgl[0].len = io->wire_len;
	io->sgl_count = 1;

	memset(&io->iparam, 0, sizeof(io->iparam));
	io->iparam.fcp_tgt.ox_id = io->init_task_tag;
	io->iparam.fcp_tgt.offset = 0;
	io->iparam.fcp_tgt.cs_ctl = io->cs_ctl;
	io->iparam.fcp_tgt.timeout = io->timeout;

	rc = efct_scsi_io_dispatch(io, efct_target_io_cb);

	return rc;
}

static int
efct_target_abort_cb(struct efct_hw_io *hio, u32 length, int status,
		     u32 ext_status, void *app)
{
	struct efct_io *io = app;
	struct efct *efct;
	enum efct_scsi_io_status scsi_status;
	efct_scsi_io_cb_t abort_cb;
	void *abort_cb_arg;

	efct = io->efct;

	if (!io->abort_cb)
		goto done;

	abort_cb = io->abort_cb;
	abort_cb_arg = io->abort_cb_arg;

	io->abort_cb = NULL;
	io->abort_cb_arg = NULL;

	switch (status) {
	case SLI4_FC_WCQE_STATUS_SUCCESS:
		scsi_status = EFCT_SCSI_STATUS_GOOD;
		break;
	case SLI4_FC_WCQE_STATUS_LOCAL_REJECT:
		switch (ext_status) {
		case SLI4_FC_LOCAL_REJECT_NO_XRI:
			scsi_status = EFCT_SCSI_STATUS_NO_IO;
			break;
		case SLI4_FC_LOCAL_REJECT_ABORT_IN_PROGRESS:
			scsi_status = EFCT_SCSI_STATUS_ABORT_IN_PROGRESS;
			break;
		default:
			/*we have seen 0x15 (abort in progress)*/
			scsi_status = EFCT_SCSI_STATUS_ERROR;
			break;
		}
		break;
	case SLI4_FC_WCQE_STATUS_FCP_RSP_FAILURE:
		scsi_status = EFCT_SCSI_STATUS_CHECK_RESPONSE;
		break;
	default:
		scsi_status = EFCT_SCSI_STATUS_ERROR;
		break;
	}
	/* invoke callback */
	abort_cb(io->io_to_abort, scsi_status, 0, abort_cb_arg);

done:
	/* done with IO to abort,efct_ref_get(): efct_scsi_tgt_abort_io() */
	kref_put(&io->io_to_abort->ref, io->io_to_abort->release);

	efct_io_pool_io_free(efct->xport->io_pool, io);

	efct_scsi_check_pending(efct);
	return 0;
}

int
efct_scsi_tgt_abort_io(struct efct_io *io, efct_scsi_io_cb_t cb, void *arg)
{
	struct efct *efct;
	struct efct_xport *xport;
	int rc;
	struct efct_io *abort_io = NULL;

	efct = io->efct;
	xport = efct->xport;

	/* take a reference on IO being aborted */
	if (kref_get_unless_zero(&io->ref) == 0) {
		/* command no longer active */
		scsi_io_printf(io, "command no longer active\n");
		return -EIO;
	}

	/*
	 * allocate a new IO to send the abort request. Use efct_io_alloc()
	 * directly, as we need an IO object that will not fail allocation
	 * due to allocations being disabled (in efct_scsi_io_alloc())
	 */
	abort_io = efct_io_pool_io_alloc(efct->xport->io_pool);
	if (!abort_io) {
		atomic_add_return(1, &xport->io_alloc_failed_count);
		kref_put(&io->ref, io->release);
		return -EIO;
	}

	/* Save the target server callback and argument */
	/* set generic fields */
	abort_io->cmd_tgt = true;
	abort_io->node = io->node;

	/* set type and abort-specific fields */
	abort_io->io_type = EFCT_IO_TYPE_ABORT;
	abort_io->display_name = "tgt_abort";
	abort_io->io_to_abort = io;
	abort_io->send_abts = false;
	abort_io->abort_cb = cb;
	abort_io->abort_cb_arg = arg;

	/* now dispatch IO */
	rc = efct_scsi_io_dispatch_abort(abort_io, efct_target_abort_cb);
	if (rc)
		kref_put(&io->ref, io->release);
	return rc;
}

void
efct_scsi_io_complete(struct efct_io *io)
{
	if (io->io_free) {
		efc_log_debug(io->efct, "completion for non-busy io tag 0x%x\n",
			      io->tag);
		return;
	}

	scsi_io_trace(io, "freeing io 0x%p %s\n", io, io->display_name);
	kref_put(&io->ref, io->release);
}
