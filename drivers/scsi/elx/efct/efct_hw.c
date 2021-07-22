// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_hw.h"
#include "efct_unsol.h"

struct efct_hw_link_stat_cb_arg {
	void (*cb)(int status, u32 num_counters,
		   struct efct_hw_link_stat_counts *counters, void *arg);
	void *arg;
};

struct efct_hw_host_stat_cb_arg {
	void (*cb)(int status, u32 num_counters,
		   struct efct_hw_host_stat_counts *counters, void *arg);
	void *arg;
};

struct efct_hw_fw_wr_cb_arg {
	void (*cb)(int status, u32 bytes_written, u32 change_status, void *arg);
	void *arg;
};

struct efct_mbox_rqst_ctx {
	int (*callback)(struct efc *efc, int status, u8 *mqe, void *arg);
	void *arg;
};

static int
efct_hw_link_event_init(struct efct_hw *hw)
{
	hw->link.status = SLI4_LINK_STATUS_MAX;
	hw->link.topology = SLI4_LINK_TOPO_NONE;
	hw->link.medium = SLI4_LINK_MEDIUM_MAX;
	hw->link.speed = 0;
	hw->link.loop_map = NULL;
	hw->link.fc_id = U32_MAX;

	return 0;
}

static int
efct_hw_read_max_dump_size(struct efct_hw *hw)
{
	u8 buf[SLI4_BMBX_SIZE];
	struct efct *efct = hw->os;
	int rc = 0;
	struct sli4_rsp_cmn_set_dump_location *rsp;

	/* attempt to detemine the dump size for function 0 only. */
	if (PCI_FUNC(efct->pci->devfn) != 0)
		return rc;

	if (sli_cmd_common_set_dump_location(&hw->sli, buf, 1, 0, NULL, 0))
		return -EIO;

	rsp = (struct sli4_rsp_cmn_set_dump_location *)
	      (buf + offsetof(struct sli4_cmd_sli_config, payload.embed));

	rc = efct_hw_command(hw, buf, EFCT_CMD_POLL, NULL, NULL);
	if (rc != 0) {
		efc_log_debug(hw->os, "set dump location cmd failed\n");
		return rc;
	}

	hw->dump_size =
	  le32_to_cpu(rsp->buffer_length_dword) & SLI4_CMN_SET_DUMP_BUFFER_LEN;

	efc_log_debug(hw->os, "Dump size %x\n",	hw->dump_size);

	return rc;
}

static int
__efct_read_topology_cb(struct efct_hw *hw, int status, u8 *mqe, void *arg)
{
	struct sli4_cmd_read_topology *read_topo =
				(struct sli4_cmd_read_topology *)mqe;
	u8 speed;
	struct efc_domain_record drec = {0};
	struct efct *efct = hw->os;

	if (status || le16_to_cpu(read_topo->hdr.status)) {
		efc_log_debug(hw->os, "bad status cqe=%#x mqe=%#x\n", status,
			      le16_to_cpu(read_topo->hdr.status));
		return -EIO;
	}

	switch (le32_to_cpu(read_topo->dw2_attentype) &
		SLI4_READTOPO_ATTEN_TYPE) {
	case SLI4_READ_TOPOLOGY_LINK_UP:
		hw->link.status = SLI4_LINK_STATUS_UP;
		break;
	case SLI4_READ_TOPOLOGY_LINK_DOWN:
		hw->link.status = SLI4_LINK_STATUS_DOWN;
		break;
	case SLI4_READ_TOPOLOGY_LINK_NO_ALPA:
		hw->link.status = SLI4_LINK_STATUS_NO_ALPA;
		break;
	default:
		hw->link.status = SLI4_LINK_STATUS_MAX;
		break;
	}

	switch (read_topo->topology) {
	case SLI4_READ_TOPO_NON_FC_AL:
		hw->link.topology = SLI4_LINK_TOPO_NON_FC_AL;
		break;
	case SLI4_READ_TOPO_FC_AL:
		hw->link.topology = SLI4_LINK_TOPO_FC_AL;
		if (hw->link.status == SLI4_LINK_STATUS_UP)
			hw->link.loop_map = hw->loop_map.virt;
		hw->link.fc_id = read_topo->acquired_al_pa;
		break;
	default:
		hw->link.topology = SLI4_LINK_TOPO_MAX;
		break;
	}

	hw->link.medium = SLI4_LINK_MEDIUM_FC;

	speed = (le32_to_cpu(read_topo->currlink_state) &
		 SLI4_READTOPO_LINKSTATE_SPEED) >> 8;
	switch (speed) {
	case SLI4_READ_TOPOLOGY_SPEED_1G:
		hw->link.speed =  1 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_2G:
		hw->link.speed =  2 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_4G:
		hw->link.speed =  4 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_8G:
		hw->link.speed =  8 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_16G:
		hw->link.speed = 16 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_32G:
		hw->link.speed = 32 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_64G:
		hw->link.speed = 64 * 1000;
		break;
	case SLI4_READ_TOPOLOGY_SPEED_128G:
		hw->link.speed = 128 * 1000;
		break;
	}

	drec.speed = hw->link.speed;
	drec.fc_id = hw->link.fc_id;
	drec.is_nport = true;
	efc_domain_cb(efct->efcport, EFC_HW_DOMAIN_FOUND, &drec);

	return 0;
}

static int
efct_hw_cb_link(void *ctx, void *e)
{
	struct efct_hw *hw = ctx;
	struct sli4_link_event *event = e;
	struct efc_domain *d = NULL;
	int rc = 0;
	struct efct *efct = hw->os;

	efct_hw_link_event_init(hw);

	switch (event->status) {
	case SLI4_LINK_STATUS_UP:

		hw->link = *event;
		efct->efcport->link_status = EFC_LINK_STATUS_UP;

		if (event->topology == SLI4_LINK_TOPO_NON_FC_AL) {
			struct efc_domain_record drec = {0};

			efc_log_info(hw->os, "Link Up, NPORT, speed is %d\n",
				     event->speed);
			drec.speed = event->speed;
			drec.fc_id = event->fc_id;
			drec.is_nport = true;
			efc_domain_cb(efct->efcport, EFC_HW_DOMAIN_FOUND,
				      &drec);
		} else if (event->topology == SLI4_LINK_TOPO_FC_AL) {
			u8 buf[SLI4_BMBX_SIZE];

			efc_log_info(hw->os, "Link Up, LOOP, speed is %d\n",
				     event->speed);

			if (!sli_cmd_read_topology(&hw->sli, buf,
						   &hw->loop_map)) {
				rc = efct_hw_command(hw, buf, EFCT_CMD_NOWAIT,
						__efct_read_topology_cb, NULL);
			}

			if (rc)
				efc_log_debug(hw->os, "READ_TOPOLOGY failed\n");
		} else {
			efc_log_info(hw->os, "%s(%#x), speed is %d\n",
				     "Link Up, unsupported topology ",
				     event->topology, event->speed);
		}
		break;
	case SLI4_LINK_STATUS_DOWN:
		efc_log_info(hw->os, "Link down\n");

		hw->link.status = event->status;
		efct->efcport->link_status = EFC_LINK_STATUS_DOWN;

		d = efct->efcport->domain;
		if (d)
			efc_domain_cb(efct->efcport, EFC_HW_DOMAIN_LOST, d);
		break;
	default:
		efc_log_debug(hw->os, "unhandled link status %#x\n",
			      event->status);
		break;
	}

	return 0;
}

int
efct_hw_setup(struct efct_hw *hw, void *os, struct pci_dev *pdev)
{
	u32 i, max_sgl, cpus;

	if (hw->hw_setup_called)
		return 0;

	/*
	 * efct_hw_init() relies on NULL pointers indicating that a structure
	 * needs allocation. If a structure is non-NULL, efct_hw_init() won't
	 * free/realloc that memory
	 */
	memset(hw, 0, sizeof(struct efct_hw));

	hw->hw_setup_called = true;

	hw->os = os;

	mutex_init(&hw->bmbx_lock);
	spin_lock_init(&hw->cmd_lock);
	INIT_LIST_HEAD(&hw->cmd_head);
	INIT_LIST_HEAD(&hw->cmd_pending);
	hw->cmd_head_count = 0;

	/* Create mailbox command ctx pool */
	hw->cmd_ctx_pool = mempool_create_kmalloc_pool(EFCT_CMD_CTX_POOL_SZ,
					sizeof(struct efct_command_ctx));
	if (!hw->cmd_ctx_pool) {
		efc_log_err(hw->os, "failed to allocate mailbox buffer pool\n");
		return -EIO;
	}

	/* Create mailbox request ctx pool for library callback */
	hw->mbox_rqst_pool = mempool_create_kmalloc_pool(EFCT_CMD_CTX_POOL_SZ,
					sizeof(struct efct_mbox_rqst_ctx));
	if (!hw->mbox_rqst_pool) {
		efc_log_err(hw->os, "failed to allocate mbox request pool\n");
		return -EIO;
	}

	spin_lock_init(&hw->io_lock);
	INIT_LIST_HEAD(&hw->io_inuse);
	INIT_LIST_HEAD(&hw->io_free);
	INIT_LIST_HEAD(&hw->io_wait_free);

	atomic_set(&hw->io_alloc_failed_count, 0);

	hw->config.speed = SLI4_LINK_SPEED_AUTO_16_8_4;
	if (sli_setup(&hw->sli, hw->os, pdev, ((struct efct *)os)->reg)) {
		efc_log_err(hw->os, "SLI setup failed\n");
		return -EIO;
	}

	efct_hw_link_event_init(hw);

	sli_callback(&hw->sli, SLI4_CB_LINK, efct_hw_cb_link, hw);

	/*
	 * Set all the queue sizes to the maximum allowed.
	 */
	for (i = 0; i < ARRAY_SIZE(hw->num_qentries); i++)
		hw->num_qentries[i] = hw->sli.qinfo.max_qentries[i];
	/*
	 * Adjust the size of the WQs so that the CQ is twice as big as
	 * the WQ to allow for 2 completions per IO. This allows us to
	 * handle multi-phase as well as aborts.
	 */
	hw->num_qentries[SLI4_QTYPE_WQ] = hw->num_qentries[SLI4_QTYPE_CQ] / 2;

	/*
	 * The RQ assignment for RQ pair mode.
	 */

	hw->config.rq_default_buffer_size = EFCT_HW_RQ_SIZE_PAYLOAD;
	hw->config.n_io = hw->sli.ext[SLI4_RSRC_XRI].size;

	cpus = num_possible_cpus();
	hw->config.n_eq = cpus > EFCT_HW_MAX_NUM_EQ ? EFCT_HW_MAX_NUM_EQ : cpus;

	max_sgl = sli_get_max_sgl(&hw->sli) - SLI4_SGE_MAX_RESERVED;
	max_sgl = (max_sgl > EFCT_FC_MAX_SGL) ? EFCT_FC_MAX_SGL : max_sgl;
	hw->config.n_sgl = max_sgl;

	(void)efct_hw_read_max_dump_size(hw);

	return 0;
}

static void
efct_logfcfi(struct efct_hw *hw, u32 j, u32 i, u32 id)
{
	efc_log_info(hw->os,
		     "REG_FCFI: filter[%d] %08X -> RQ[%d] id=%d\n",
		     j, hw->config.filter_def[j], i, id);
}

static inline void
efct_hw_init_free_io(struct efct_hw_io *io)
{
	/*
	 * Set io->done to NULL, to avoid any callbacks, should
	 * a completion be received for one of these IOs
	 */
	io->done = NULL;
	io->abort_done = NULL;
	io->status_saved = false;
	io->abort_in_progress = false;
	io->type = 0xFFFF;
	io->wq = NULL;
}

static bool efct_hw_iotype_is_originator(u16 io_type)
{
	switch (io_type) {
	case EFCT_HW_FC_CT:
	case EFCT_HW_ELS_REQ:
		return true;
	default:
		return false;
	}
}

static void
efct_hw_io_restore_sgl(struct efct_hw *hw, struct efct_hw_io *io)
{
	/* Restore the default */
	io->sgl = &io->def_sgl;
	io->sgl_count = io->def_sgl_count;
}

static void
efct_hw_wq_process_io(void *arg, u8 *cqe, int status)
{
	struct efct_hw_io *io = arg;
	struct efct_hw *hw = io->hw;
	struct sli4_fc_wcqe *wcqe = (void *)cqe;
	u32	len = 0;
	u32 ext = 0;

	/* clear xbusy flag if WCQE[XB] is clear */
	if (io->xbusy && (wcqe->flags & SLI4_WCQE_XB) == 0)
		io->xbusy = false;

	/* get extended CQE status */
	switch (io->type) {
	case EFCT_HW_BLS_ACC:
	case EFCT_HW_BLS_RJT:
		break;
	case EFCT_HW_ELS_REQ:
		sli_fc_els_did(&hw->sli, cqe, &ext);
		len = sli_fc_response_length(&hw->sli, cqe);
		break;
	case EFCT_HW_ELS_RSP:
	case EFCT_HW_FC_CT_RSP:
		break;
	case EFCT_HW_FC_CT:
		len = sli_fc_response_length(&hw->sli, cqe);
		break;
	case EFCT_HW_IO_TARGET_WRITE:
		len = sli_fc_io_length(&hw->sli, cqe);
		break;
	case EFCT_HW_IO_TARGET_READ:
		len = sli_fc_io_length(&hw->sli, cqe);
		break;
	case EFCT_HW_IO_TARGET_RSP:
		break;
	case EFCT_HW_IO_DNRX_REQUEUE:
		/* release the count for re-posting the buffer */
		/* efct_hw_io_free(hw, io); */
		break;
	default:
		efc_log_err(hw->os, "unhandled io type %#x for XRI 0x%x\n",
			    io->type, io->indicator);
		break;
	}
	if (status) {
		ext = sli_fc_ext_status(&hw->sli, cqe);
		/*
		 * If we're not an originator IO, and XB is set, then issue
		 * abort for the IO from within the HW
		 */
		if (efct_hw_iotype_is_originator(io->type) &&
		    wcqe->flags & SLI4_WCQE_XB) {
			int rc;

			efc_log_debug(hw->os, "aborting xri=%#x tag=%#x\n",
				      io->indicator, io->reqtag);

			/*
			 * Because targets may send a response when the IO
			 * completes using the same XRI, we must wait for the
			 * XRI_ABORTED CQE to issue the IO callback
			 */
			rc = efct_hw_io_abort(hw, io, false, NULL, NULL);
			if (rc == 0) {
				/*
				 * latch status to return after abort is
				 * complete
				 */
				io->status_saved = true;
				io->saved_status = status;
				io->saved_ext = ext;
				io->saved_len = len;
				goto exit_efct_hw_wq_process_io;
			} else if (rc == -EINPROGRESS) {
				/*
				 * Already being aborted by someone else (ABTS
				 * perhaps). Just return original
				 * error.
				 */
				efc_log_debug(hw->os, "%s%#x tag=%#x\n",
					      "abort in progress xri=",
					      io->indicator, io->reqtag);

			} else {
				/* Failed to abort for some other reason, log
				 * error
				 */
				efc_log_debug(hw->os, "%s%#x tag=%#x rc=%d\n",
					      "Failed to abort xri=",
					      io->indicator, io->reqtag, rc);
			}
		}
	}

	if (io->done) {
		efct_hw_done_t done = io->done;

		io->done = NULL;

		if (io->status_saved) {
			/* use latched status if exists */
			status = io->saved_status;
			len = io->saved_len;
			ext = io->saved_ext;
			io->status_saved = false;
		}

		/* Restore default SGL */
		efct_hw_io_restore_sgl(hw, io);
		done(io, len, status, ext, io->arg);
	}

exit_efct_hw_wq_process_io:
	return;
}

static int
efct_hw_setup_io(struct efct_hw *hw)
{
	u32	i = 0;
	struct efct_hw_io	*io = NULL;
	uintptr_t	xfer_virt = 0;
	uintptr_t	xfer_phys = 0;
	u32	index;
	bool new_alloc = true;
	struct efc_dma *dma;
	struct efct *efct = hw->os;

	if (!hw->io) {
		hw->io = kmalloc_array(hw->config.n_io, sizeof(io), GFP_KERNEL);
		if (!hw->io)
			return -ENOMEM;

		memset(hw->io, 0, hw->config.n_io * sizeof(io));

		for (i = 0; i < hw->config.n_io; i++) {
			hw->io[i] = kzalloc(sizeof(*io), GFP_KERNEL);
			if (!hw->io[i])
				goto error;
		}

		/* Create WQE buffs for IO */
		hw->wqe_buffs = kzalloc((hw->config.n_io * hw->sli.wqe_size),
					GFP_KERNEL);
		if (!hw->wqe_buffs) {
			kfree(hw->io);
			return -ENOMEM;
		}

	} else {
		/* re-use existing IOs, including SGLs */
		new_alloc = false;
	}

	if (new_alloc) {
		dma = &hw->xfer_rdy;
		dma->size = sizeof(struct fcp_txrdy) * hw->config.n_io;
		dma->virt = dma_alloc_coherent(&efct->pci->dev,
					       dma->size, &dma->phys, GFP_DMA);
		if (!dma->virt)
			return -ENOMEM;
	}
	xfer_virt = (uintptr_t)hw->xfer_rdy.virt;
	xfer_phys = hw->xfer_rdy.phys;

	/* Initialize the pool of HW IO objects */
	for (i = 0; i < hw->config.n_io; i++) {
		struct hw_wq_callback *wqcb;

		io = hw->io[i];

		/* initialize IO fields */
		io->hw = hw;

		/* Assign a WQE buff */
		io->wqe.wqebuf = &hw->wqe_buffs[i * hw->sli.wqe_size];

		/* Allocate the request tag for this IO */
		wqcb = efct_hw_reqtag_alloc(hw, efct_hw_wq_process_io, io);
		if (!wqcb) {
			efc_log_err(hw->os, "can't allocate request tag\n");
			return -ENOSPC;
		}
		io->reqtag = wqcb->instance_index;

		/* Now for the fields that are initialized on each free */
		efct_hw_init_free_io(io);

		/* The XB flag isn't cleared on IO free, so init to zero */
		io->xbusy = 0;

		if (sli_resource_alloc(&hw->sli, SLI4_RSRC_XRI,
				       &io->indicator, &index)) {
			efc_log_err(hw->os,
				    "sli_resource_alloc failed @ %d\n", i);
			return -ENOMEM;
		}

		if (new_alloc) {
			dma = &io->def_sgl;
			dma->size = hw->config.n_sgl *
					sizeof(struct sli4_sge);
			dma->virt = dma_alloc_coherent(&efct->pci->dev,
						       dma->size, &dma->phys,
						       GFP_DMA);
			if (!dma->virt) {
				efc_log_err(hw->os, "dma_alloc fail %d\n", i);
				memset(&io->def_sgl, 0,
				       sizeof(struct efc_dma));
				return -ENOMEM;
			}
		}
		io->def_sgl_count = hw->config.n_sgl;
		io->sgl = &io->def_sgl;
		io->sgl_count = io->def_sgl_count;

		if (hw->xfer_rdy.size) {
			io->xfer_rdy.virt = (void *)xfer_virt;
			io->xfer_rdy.phys = xfer_phys;
			io->xfer_rdy.size = sizeof(struct fcp_txrdy);

			xfer_virt += sizeof(struct fcp_txrdy);
			xfer_phys += sizeof(struct fcp_txrdy);
		}
	}

	return 0;
error:
	for (i = 0; i < hw->config.n_io && hw->io[i]; i++) {
		kfree(hw->io[i]);
		hw->io[i] = NULL;
	}

	kfree(hw->io);
	hw->io = NULL;

	return -ENOMEM;
}

static int
efct_hw_init_prereg_io(struct efct_hw *hw)
{
	u32 i, idx = 0;
	struct efct_hw_io *io = NULL;
	u8 cmd[SLI4_BMBX_SIZE];
	int rc = 0;
	u32 n_rem;
	u32 n = 0;
	u32 sgls_per_request = 256;
	struct efc_dma **sgls = NULL;
	struct efc_dma req;
	struct efct *efct = hw->os;

	sgls = kmalloc_array(sgls_per_request, sizeof(*sgls), GFP_KERNEL);
	if (!sgls)
		return -ENOMEM;

	memset(&req, 0, sizeof(struct efc_dma));
	req.size = 32 + sgls_per_request * 16;
	req.virt = dma_alloc_coherent(&efct->pci->dev, req.size, &req.phys,
				      GFP_DMA);
	if (!req.virt) {
		kfree(sgls);
		return -ENOMEM;
	}

	for (n_rem = hw->config.n_io; n_rem; n_rem -= n) {
		/* Copy address of SGL's into local sgls[] array, break
		 * out if the xri is not contiguous.
		 */
		u32 min = (sgls_per_request < n_rem) ? sgls_per_request : n_rem;

		for (n = 0; n < min; n++) {
			/* Check that we have contiguous xri values */
			if (n > 0) {
				if (hw->io[idx + n]->indicator !=
				    hw->io[idx + n - 1]->indicator + 1)
					break;
			}

			sgls[n] = hw->io[idx + n]->sgl;
		}

		if (sli_cmd_post_sgl_pages(&hw->sli, cmd,
				hw->io[idx]->indicator,	n, sgls, NULL, &req)) {
			rc = -EIO;
			break;
		}

		rc = efct_hw_command(hw, cmd, EFCT_CMD_POLL, NULL, NULL);
		if (rc) {
			efc_log_err(hw->os, "SGL post failed, rc=%d\n", rc);
			break;
		}

		/* Add to tail if successful */
		for (i = 0; i < n; i++, idx++) {
			io = hw->io[idx];
			io->state = EFCT_HW_IO_STATE_FREE;
			INIT_LIST_HEAD(&io->list_entry);
			list_add_tail(&io->list_entry, &hw->io_free);
		}
	}

	dma_free_coherent(&efct->pci->dev, req.size, req.virt, req.phys);
	memset(&req, 0, sizeof(struct efc_dma));
	kfree(sgls);

	return rc;
}

static int
efct_hw_init_io(struct efct_hw *hw)
{
	u32 i, idx = 0;
	bool prereg = false;
	struct efct_hw_io *io = NULL;
	int rc = 0;

	prereg = hw->sli.params.sgl_pre_registered;

	if (prereg)
		return efct_hw_init_prereg_io(hw);

	for (i = 0; i < hw->config.n_io; i++, idx++) {
		io = hw->io[idx];
		io->state = EFCT_HW_IO_STATE_FREE;
		INIT_LIST_HEAD(&io->list_entry);
		list_add_tail(&io->list_entry, &hw->io_free);
	}

	return rc;
}

static int
efct_hw_config_set_fdt_xfer_hint(struct efct_hw *hw, u32 fdt_xfer_hint)
{
	int rc = 0;
	u8 buf[SLI4_BMBX_SIZE];
	struct sli4_rqst_cmn_set_features_set_fdt_xfer_hint param;

	memset(&param, 0, sizeof(param));
	param.fdt_xfer_hint = cpu_to_le32(fdt_xfer_hint);
	/* build the set_features command */
	sli_cmd_common_set_features(&hw->sli, buf,
		SLI4_SET_FEATURES_SET_FTD_XFER_HINT, sizeof(param), &param);

	rc = efct_hw_command(hw, buf, EFCT_CMD_POLL, NULL, NULL);
	if (rc)
		efc_log_warn(hw->os, "set FDT hint %d failed: %d\n",
			     fdt_xfer_hint, rc);
	else
		efc_log_info(hw->os, "Set FTD transfer hint to %d\n",
			     le32_to_cpu(param.fdt_xfer_hint));

	return rc;
}

static int
efct_hw_config_rq(struct efct_hw *hw)
{
	u32 min_rq_count, i, rc;
	struct sli4_cmd_rq_cfg rq_cfg[SLI4_CMD_REG_FCFI_NUM_RQ_CFG];
	u8 buf[SLI4_BMBX_SIZE];

	efc_log_info(hw->os, "using REG_FCFI standard\n");

	/*
	 * Set the filter match/mask values from hw's
	 * filter_def values
	 */
	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		rq_cfg[i].rq_id = cpu_to_le16(0xffff);
		rq_cfg[i].r_ctl_mask = (u8)hw->config.filter_def[i];
		rq_cfg[i].r_ctl_match = (u8)(hw->config.filter_def[i] >> 8);
		rq_cfg[i].type_mask = (u8)(hw->config.filter_def[i] >> 16);
		rq_cfg[i].type_match = (u8)(hw->config.filter_def[i] >> 24);
	}

	/*
	 * Update the rq_id's of the FCF configuration
	 * (don't update more than the number of rq_cfg
	 * elements)
	 */
	min_rq_count = (hw->hw_rq_count < SLI4_CMD_REG_FCFI_NUM_RQ_CFG)	?
			hw->hw_rq_count : SLI4_CMD_REG_FCFI_NUM_RQ_CFG;
	for (i = 0; i < min_rq_count; i++) {
		struct hw_rq *rq = hw->hw_rq[i];
		u32 j;

		for (j = 0; j < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; j++) {
			u32 mask = (rq->filter_mask != 0) ?
				rq->filter_mask : 1;

			if (!(mask & (1U << j)))
				continue;

			rq_cfg[i].rq_id = cpu_to_le16(rq->hdr->id);
			efct_logfcfi(hw, j, i, rq->hdr->id);
		}
	}

	rc = -EIO;
	if (!sli_cmd_reg_fcfi(&hw->sli, buf, 0,	rq_cfg))
		rc = efct_hw_command(hw, buf, EFCT_CMD_POLL, NULL, NULL);

	if (rc != 0) {
		efc_log_err(hw->os, "FCFI registration failed\n");
		return rc;
	}
	hw->fcf_indicator =
		le16_to_cpu(((struct sli4_cmd_reg_fcfi *)buf)->fcfi);

	return rc;
}

static int
efct_hw_config_mrq(struct efct_hw *hw, u8 mode, u16 fcf_index)
{
	u8 buf[SLI4_BMBX_SIZE], mrq_bitmask = 0;
	struct hw_rq *rq;
	struct sli4_cmd_reg_fcfi_mrq *rsp = NULL;
	struct sli4_cmd_rq_cfg rq_filter[SLI4_CMD_REG_FCFI_MRQ_NUM_RQ_CFG];
	u32 rc, i;

	if (mode == SLI4_CMD_REG_FCFI_SET_FCFI_MODE)
		goto issue_cmd;

	/* Set the filter match/mask values from hw's filter_def values */
	for (i = 0; i < SLI4_CMD_REG_FCFI_NUM_RQ_CFG; i++) {
		rq_filter[i].rq_id = cpu_to_le16(0xffff);
		rq_filter[i].type_mask = (u8)hw->config.filter_def[i];
		rq_filter[i].type_match = (u8)(hw->config.filter_def[i] >> 8);
		rq_filter[i].r_ctl_mask = (u8)(hw->config.filter_def[i] >> 16);
		rq_filter[i].r_ctl_match = (u8)(hw->config.filter_def[i] >> 24);
	}

	rq = hw->hw_rq[0];
	rq_filter[0].rq_id = cpu_to_le16(rq->hdr->id);
	rq_filter[1].rq_id = cpu_to_le16(rq->hdr->id);

	mrq_bitmask = 0x2;
issue_cmd:
	efc_log_debug(hw->os, "Issue reg_fcfi_mrq count:%d policy:%d mode:%d\n",
		      hw->hw_rq_count, hw->config.rq_selection_policy, mode);
	/* Invoke REG_FCFI_MRQ */
	rc = sli_cmd_reg_fcfi_mrq(&hw->sli, buf, mode, fcf_index,
				  hw->config.rq_selection_policy, mrq_bitmask,
				  hw->hw_mrq_count, rq_filter);
	if (rc) {
		efc_log_err(hw->os, "sli_cmd_reg_fcfi_mrq() failed\n");
		return -EIO;
	}

	rc = efct_hw_command(hw, buf, EFCT_CMD_POLL, NULL, NULL);

	rsp = (struct sli4_cmd_reg_fcfi_mrq *)buf;

	if ((rc) || (le16_to_cpu(rsp->hdr.status))) {
		efc_log_err(hw->os, "FCFI MRQ reg failed. cmd=%x status=%x\n",
			    rsp->hdr.command, le16_to_cpu(rsp->hdr.status));
		return -EIO;
	}

	if (mode == SLI4_CMD_REG_FCFI_SET_FCFI_MODE)
		hw->fcf_indicator = le16_to_cpu(rsp->fcfi);

	return 0;
}

static void
efct_hw_queue_hash_add(struct efct_queue_hash *hash,
		       u16 id, u16 index)
{
	u32 hash_index = id & (EFCT_HW_Q_HASH_SIZE - 1);

	/*
	 * Since the hash is always bigger than the number of queues, then we
	 * never have to worry about an infinite loop.
	 */
	while (hash[hash_index].in_use)
		hash_index = (hash_index + 1) & (EFCT_HW_Q_HASH_SIZE - 1);

	/* not used, claim the entry */
	hash[hash_index].id = id;
	hash[hash_index].in_use = true;
	hash[hash_index].index = index;
}

static int
efct_hw_config_sli_port_health_check(struct efct_hw *hw, u8 query, u8 enable)
{
	int rc = 0;
	u8 buf[SLI4_BMBX_SIZE];
	struct sli4_rqst_cmn_set_features_health_check param;
	u32 health_check_flag = 0;

	memset(&param, 0, sizeof(param));

	if (enable)
		health_check_flag |= SLI4_RQ_HEALTH_CHECK_ENABLE;

	if (query)
		health_check_flag |= SLI4_RQ_HEALTH_CHECK_QUERY;

	param.health_check_dword = cpu_to_le32(health_check_flag);

	/* build the set_features command */
	sli_cmd_common_set_features(&hw->sli, buf,
		SLI4_SET_FEATURES_SLI_PORT_HEALTH_CHECK, sizeof(param), &param);

	rc = efct_hw_command(hw, buf, EFCT_CMD_POLL, NULL, NULL);
	if (rc)
		efc_log_err(hw->os, "efct_hw_command returns %d\n", rc);
	else
		efc_log_debug(hw->os, "SLI Port Health Check is enabled\n");

	return rc;
}

int
efct_hw_init(struct efct_hw *hw)
{
	int rc;
	u32 i = 0;
	int rem_count;
	unsigned long flags = 0;
	struct efct_hw_io *temp;
	struct efc_dma *dma;

	/*
	 * Make sure the command lists are empty. If this is start-of-day,
	 * they'll be empty since they were just initialized in efct_hw_setup.
	 * If we've just gone through a reset, the command and command pending
	 * lists should have been cleaned up as part of the reset
	 * (efct_hw_reset()).
	 */
	spin_lock_irqsave(&hw->cmd_lock, flags);
	if (!list_empty(&hw->cmd_head)) {
		spin_unlock_irqrestore(&hw->cmd_lock, flags);
		efc_log_err(hw->os, "command found on cmd list\n");
		return -EIO;
	}
	if (!list_empty(&hw->cmd_pending)) {
		spin_unlock_irqrestore(&hw->cmd_lock, flags);
		efc_log_err(hw->os, "command found on pending list\n");
		return -EIO;
	}
	spin_unlock_irqrestore(&hw->cmd_lock, flags);

	/* Free RQ buffers if prevously allocated */
	efct_hw_rx_free(hw);

	/*
	 * The IO queues must be initialized here for the reset case. The
	 * efct_hw_init_io() function will re-add the IOs to the free list.
	 * The cmd_head list should be OK since we free all entries in
	 * efct_hw_command_cancel() that is called in the efct_hw_reset().
	 */

	/* If we are in this function due to a reset, there may be stale items
	 * on lists that need to be removed.  Clean them up.
	 */
	rem_count = 0;
	while ((!list_empty(&hw->io_wait_free))) {
		rem_count++;
		temp = list_first_entry(&hw->io_wait_free, struct efct_hw_io,
					list_entry);
		list_del_init(&temp->list_entry);
	}
	if (rem_count > 0)
		efc_log_debug(hw->os, "rmvd %d items from io_wait_free list\n",
			      rem_count);

	rem_count = 0;
	while ((!list_empty(&hw->io_inuse))) {
		rem_count++;
		temp = list_first_entry(&hw->io_inuse, struct efct_hw_io,
					list_entry);
		list_del_init(&temp->list_entry);
	}
	if (rem_count > 0)
		efc_log_debug(hw->os, "rmvd %d items from io_inuse list\n",
			      rem_count);

	rem_count = 0;
	while ((!list_empty(&hw->io_free))) {
		rem_count++;
		temp = list_first_entry(&hw->io_free, struct efct_hw_io,
					list_entry);
		list_del_init(&temp->list_entry);
	}
	if (rem_count > 0)
		efc_log_debug(hw->os, "rmvd %d items from io_free list\n",
			      rem_count);

	/* If MRQ not required, Make sure we dont request feature. */
	if (hw->config.n_rq == 1)
		hw->sli.features &= (~SLI4_REQFEAT_MRQP);

	if (sli_init(&hw->sli)) {
		efc_log_err(hw->os, "SLI failed to initialize\n");
		return -EIO;
	}

	if (hw->sliport_healthcheck) {
		rc = efct_hw_config_sli_port_health_check(hw, 0, 1);
		if (rc != 0) {
			efc_log_err(hw->os, "Enable port Health check fail\n");
			return rc;
		}
	}

	/*
	 * Set FDT transfer hint, only works on Lancer
	 */
	if (hw->sli.if_type == SLI4_INTF_IF_TYPE_2) {
		/*
		 * Non-fatal error. In particular, we can disregard failure to
		 * set EFCT_HW_FDT_XFER_HINT on devices with legacy firmware
		 * that do not support EFCT_HW_FDT_XFER_HINT feature.
		 */
		efct_hw_config_set_fdt_xfer_hint(hw, EFCT_HW_FDT_XFER_HINT);
	}

	/* zero the hashes */
	memset(hw->cq_hash, 0, sizeof(hw->cq_hash));
	efc_log_debug(hw->os, "Max CQs %d, hash size = %d\n",
		      EFCT_HW_MAX_NUM_CQ, EFCT_HW_Q_HASH_SIZE);

	memset(hw->rq_hash, 0, sizeof(hw->rq_hash));
	efc_log_debug(hw->os, "Max RQs %d, hash size = %d\n",
		      EFCT_HW_MAX_NUM_RQ, EFCT_HW_Q_HASH_SIZE);

	memset(hw->wq_hash, 0, sizeof(hw->wq_hash));
	efc_log_debug(hw->os, "Max WQs %d, hash size = %d\n",
		      EFCT_HW_MAX_NUM_WQ, EFCT_HW_Q_HASH_SIZE);

	rc = efct_hw_init_queues(hw);
	if (rc)
		return rc;

	rc = efct_hw_map_wq_cpu(hw);
	if (rc)
		return rc;

	/* Allocate and p_st RQ buffers */
	rc = efct_hw_rx_allocate(hw);
	if (rc) {
		efc_log_err(hw->os, "rx_allocate failed\n");
		return rc;
	}

	rc = efct_hw_rx_post(hw);
	if (rc) {
		efc_log_err(hw->os, "WARNING - error posting RQ buffers\n");
		return rc;
	}

	if (hw->config.n_eq == 1) {
		rc = efct_hw_config_rq(hw);
		if (rc) {
			efc_log_err(hw->os, "config rq failed %d\n", rc);
			return rc;
		}
	} else {
		rc = efct_hw_config_mrq(hw, SLI4_CMD_REG_FCFI_SET_FCFI_MODE, 0);
		if (rc != 0) {
			efc_log_err(hw->os, "REG_FCFI_MRQ FCFI reg failed\n");
			return rc;
		}

		rc = efct_hw_config_mrq(hw, SLI4_CMD_REG_FCFI_SET_MRQ_MODE, 0);
		if (rc != 0) {
			efc_log_err(hw->os, "REG_FCFI_MRQ MRQ reg failed\n");
			return rc;
		}
	}

	/*
	 * Allocate the WQ request tag pool, if not previously allocated
	 * (the request tag value is 16 bits, thus the pool allocation size
	 * of 64k)
	 */
	hw->wq_reqtag_pool = efct_hw_reqtag_pool_alloc(hw);
	if (!hw->wq_reqtag_pool) {
		efc_log_err(hw->os, "efct_hw_reqtag_pool_alloc failed\n");
		return -ENOMEM;
	}

	rc = efct_hw_setup_io(hw);
	if (rc) {
		efc_log_err(hw->os, "IO allocation failure\n");
		return rc;
	}

	rc = efct_hw_init_io(hw);
	if (rc) {
		efc_log_err(hw->os, "IO initialization failure\n");
		return rc;
	}

	dma = &hw->loop_map;
	dma->size = SLI4_MIN_LOOP_MAP_BYTES;
	dma->virt = dma_alloc_coherent(&hw->os->pci->dev, dma->size, &dma->phys,
				       GFP_DMA);
	if (!dma->virt)
		return -EIO;

	/*
	 * Arming the EQ allows (e.g.) interrupts when CQ completions write EQ
	 * entries
	 */
	for (i = 0; i < hw->eq_count; i++)
		sli_queue_arm(&hw->sli, &hw->eq[i], true);

	/*
	 * Initialize RQ hash
	 */
	for (i = 0; i < hw->rq_count; i++)
		efct_hw_queue_hash_add(hw->rq_hash, hw->rq[i].id, i);

	/*
	 * Initialize WQ hash
	 */
	for (i = 0; i < hw->wq_count; i++)
		efct_hw_queue_hash_add(hw->wq_hash, hw->wq[i].id, i);

	/*
	 * Arming the CQ allows (e.g.) MQ completions to write CQ entries
	 */
	for (i = 0; i < hw->cq_count; i++) {
		efct_hw_queue_hash_add(hw->cq_hash, hw->cq[i].id, i);
		sli_queue_arm(&hw->sli, &hw->cq[i], true);
	}

	/* Set RQ process limit*/
	for (i = 0; i < hw->hw_rq_count; i++) {
		struct hw_rq *rq = hw->hw_rq[i];

		hw->cq[rq->cq->instance].proc_limit = hw->config.n_io / 2;
	}

	/* record the fact that the queues are functional */
	hw->state = EFCT_HW_STATE_ACTIVE;
	/*
	 * Allocate a HW IOs for send frame.
	 */
	hw->hw_wq[0]->send_frame_io = efct_hw_io_alloc(hw);
	if (!hw->hw_wq[0]->send_frame_io)
		efc_log_err(hw->os, "alloc for send_frame_io failed\n");

	/* Initialize send frame sequence id */
	atomic_set(&hw->send_frame_seq_id, 0);

	return 0;
}

int
efct_hw_parse_filter(struct efct_hw *hw, void *value)
{
	int rc = 0;
	char *p = NULL;
	char *token;
	u32 idx = 0;

	for (idx = 0; idx < ARRAY_SIZE(hw->config.filter_def); idx++)
		hw->config.filter_def[idx] = 0;

	p = kstrdup(value, GFP_KERNEL);
	if (!p || !*p) {
		efc_log_err(hw->os, "p is NULL\n");
		return -ENOMEM;
	}

	idx = 0;
	while ((token = strsep(&p, ",")) && *token) {
		if (kstrtou32(token, 0, &hw->config.filter_def[idx++]))
			efc_log_err(hw->os, "kstrtoint failed\n");

		if (!p || !*p)
			break;

		if (idx == ARRAY_SIZE(hw->config.filter_def))
			break;
	}
	kfree(p);

	return rc;
}

u64
efct_get_wwnn(struct efct_hw *hw)
{
	struct sli4 *sli = &hw->sli;
	u8 p[8];

	memcpy(p, sli->wwnn, sizeof(p));
	return get_unaligned_be64(p);
}

u64
efct_get_wwpn(struct efct_hw *hw)
{
	struct sli4 *sli = &hw->sli;
	u8 p[8];

	memcpy(p, sli->wwpn, sizeof(p));
	return get_unaligned_be64(p);
}

static struct efc_hw_rq_buffer *
efct_hw_rx_buffer_alloc(struct efct_hw *hw, u32 rqindex, u32 count,
			u32 size)
{
	struct efct *efct = hw->os;
	struct efc_hw_rq_buffer *rq_buf = NULL;
	struct efc_hw_rq_buffer *prq;
	u32 i;

	if (!count)
		return NULL;

	rq_buf = kmalloc_array(count, sizeof(*rq_buf), GFP_KERNEL);
	if (!rq_buf)
		return NULL;
	memset(rq_buf, 0, sizeof(*rq_buf) * count);

	for (i = 0, prq = rq_buf; i < count; i ++, prq++) {
		prq->rqindex = rqindex;
		prq->dma.size = size;
		prq->dma.virt = dma_alloc_coherent(&efct->pci->dev,
						   prq->dma.size,
						   &prq->dma.phys,
						   GFP_DMA);
		if (!prq->dma.virt) {
			efc_log_err(hw->os, "DMA allocation failed\n");
			kfree(rq_buf);
			return NULL;
		}
	}
	return rq_buf;
}

static void
efct_hw_rx_buffer_free(struct efct_hw *hw,
		       struct efc_hw_rq_buffer *rq_buf,
			u32 count)
{
	struct efct *efct = hw->os;
	u32 i;
	struct efc_hw_rq_buffer *prq;

	if (rq_buf) {
		for (i = 0, prq = rq_buf; i < count; i++, prq++) {
			dma_free_coherent(&efct->pci->dev,
					  prq->dma.size, prq->dma.virt,
					  prq->dma.phys);
			memset(&prq->dma, 0, sizeof(struct efc_dma));
		}

		kfree(rq_buf);
	}
}

int
efct_hw_rx_allocate(struct efct_hw *hw)
{
	struct efct *efct = hw->os;
	u32 i;
	int rc = 0;
	u32 rqindex = 0;
	u32 hdr_size = EFCT_HW_RQ_SIZE_HDR;
	u32 payload_size = hw->config.rq_default_buffer_size;

	rqindex = 0;

	for (i = 0; i < hw->hw_rq_count; i++) {
		struct hw_rq *rq = hw->hw_rq[i];

		/* Allocate header buffers */
		rq->hdr_buf = efct_hw_rx_buffer_alloc(hw, rqindex,
						      rq->entry_count,
						      hdr_size);
		if (!rq->hdr_buf) {
			efc_log_err(efct, "rx_buffer_alloc hdr_buf failed\n");
			rc = -EIO;
			break;
		}

		efc_log_debug(hw->os,
			      "rq[%2d] rq_id %02d header  %4d by %4d bytes\n",
			      i, rq->hdr->id, rq->entry_count, hdr_size);

		rqindex++;

		/* Allocate payload buffers */
		rq->payload_buf = efct_hw_rx_buffer_alloc(hw, rqindex,
							  rq->entry_count,
							  payload_size);
		if (!rq->payload_buf) {
			efc_log_err(efct, "rx_buffer_alloc fb_buf failed\n");
			rc = -EIO;
			break;
		}
		efc_log_debug(hw->os,
			      "rq[%2d] rq_id %02d default %4d by %4d bytes\n",
			      i, rq->data->id, rq->entry_count, payload_size);
		rqindex++;
	}

	return rc ? -EIO : 0;
}

int
efct_hw_rx_post(struct efct_hw *hw)
{
	u32 i;
	u32 idx;
	u32 rq_idx;
	int rc = 0;

	if (!hw->seq_pool) {
		u32 count = 0;

		for (i = 0; i < hw->hw_rq_count; i++)
			count += hw->hw_rq[i]->entry_count;

		hw->seq_pool = kmalloc_array(count,
				sizeof(struct efc_hw_sequence),	GFP_KERNEL);
		if (!hw->seq_pool)
			return -ENOMEM;
	}

	/*
	 * In RQ pair mode, we MUST post the header and payload buffer at the
	 * same time.
	 */
	for (rq_idx = 0, idx = 0; rq_idx < hw->hw_rq_count; rq_idx++) {
		struct hw_rq *rq = hw->hw_rq[rq_idx];

		for (i = 0; i < rq->entry_count - 1; i++) {
			struct efc_hw_sequence *seq;

			seq = hw->seq_pool + idx;
			idx++;
			seq->header = &rq->hdr_buf[i];
			seq->payload = &rq->payload_buf[i];
			rc = efct_hw_sequence_free(hw, seq);
			if (rc)
				break;
		}
		if (rc)
			break;
	}

	if (rc && hw->seq_pool)
		kfree(hw->seq_pool);

	return rc;
}

void
efct_hw_rx_free(struct efct_hw *hw)
{
	u32 i;

	/* Free hw_rq buffers */
	for (i = 0; i < hw->hw_rq_count; i++) {
		struct hw_rq *rq = hw->hw_rq[i];

		if (rq) {
			efct_hw_rx_buffer_free(hw, rq->hdr_buf,
					       rq->entry_count);
			rq->hdr_buf = NULL;
			efct_hw_rx_buffer_free(hw, rq->payload_buf,
					       rq->entry_count);
			rq->payload_buf = NULL;
		}
	}
}

static int
efct_hw_cmd_submit_pending(struct efct_hw *hw)
{
	int rc = 0;

	/* Assumes lock held */

	/* Only submit MQE if there's room */
	while (hw->cmd_head_count < (EFCT_HW_MQ_DEPTH - 1) &&
	       !list_empty(&hw->cmd_pending)) {
		struct efct_command_ctx *ctx;

		ctx = list_first_entry(&hw->cmd_pending,
				       struct efct_command_ctx, list_entry);
		if (!ctx)
			break;

		list_del_init(&ctx->list_entry);

		list_add_tail(&ctx->list_entry, &hw->cmd_head);
		hw->cmd_head_count++;
		if (sli_mq_write(&hw->sli, hw->mq, ctx->buf) < 0) {
			efc_log_debug(hw->os,
				      "sli_queue_write failed: %d\n", rc);
			rc = -EIO;
			break;
		}
	}
	return rc;
}

int
efct_hw_command(struct efct_hw *hw, u8 *cmd, u32 opts, void *cb, void *arg)
{
	int rc = -EIO;
	unsigned long flags = 0;
	void *bmbx = NULL;

	/*
	 * If the chip is in an error state (UE'd) then reject this mailbox
	 * command.
	 */
	if (sli_fw_error_status(&hw->sli) > 0) {
		efc_log_crit(hw->os, "Chip in an error state - reset needed\n");
		efc_log_crit(hw->os, "status=%#x error1=%#x error2=%#x\n",
			     sli_reg_read_status(&hw->sli),
			     sli_reg_read_err1(&hw->sli),
			     sli_reg_read_err2(&hw->sli));

		return -EIO;
	}

	/*
	 * Send a mailbox command to the hardware, and either wait for
	 * a completion (EFCT_CMD_POLL) or get an optional asynchronous
	 * completion (EFCT_CMD_NOWAIT).
	 */

	if (opts == EFCT_CMD_POLL) {
		mutex_lock(&hw->bmbx_lock);
		bmbx = hw->sli.bmbx.virt;

		memset(bmbx, 0, SLI4_BMBX_SIZE);
		memcpy(bmbx, cmd, SLI4_BMBX_SIZE);

		if (sli_bmbx_command(&hw->sli) == 0) {
			rc = 0;
			memcpy(cmd, bmbx, SLI4_BMBX_SIZE);
		}
		mutex_unlock(&hw->bmbx_lock);
	} else if (opts == EFCT_CMD_NOWAIT) {
		struct efct_command_ctx	*ctx = NULL;

		if (hw->state != EFCT_HW_STATE_ACTIVE) {
			efc_log_err(hw->os, "Can't send command, HW state=%d\n",
				    hw->state);
			return -EIO;
		}

		ctx = mempool_alloc(hw->cmd_ctx_pool, GFP_ATOMIC);
		if (!ctx)
			return -ENOSPC;

		memset(ctx, 0, sizeof(struct efct_command_ctx));

		if (cb) {
			ctx->cb = cb;
			ctx->arg = arg;
		}

		memcpy(ctx->buf, cmd, SLI4_BMBX_SIZE);
		ctx->ctx = hw;

		spin_lock_irqsave(&hw->cmd_lock, flags);

		/* Add to pending list */
		INIT_LIST_HEAD(&ctx->list_entry);
		list_add_tail(&ctx->list_entry, &hw->cmd_pending);

		/* Submit as much of the pending list as we can */
		rc = efct_hw_cmd_submit_pending(hw);

		spin_unlock_irqrestore(&hw->cmd_lock, flags);
	}

	return rc;
}

static int
efct_hw_command_process(struct efct_hw *hw, int status, u8 *mqe,
			size_t size)
{
	struct efct_command_ctx *ctx = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&hw->cmd_lock, flags);
	if (!list_empty(&hw->cmd_head)) {
		ctx = list_first_entry(&hw->cmd_head,
				       struct efct_command_ctx, list_entry);
		list_del_init(&ctx->list_entry);
	}
	if (!ctx) {
		efc_log_err(hw->os, "no command context\n");
		spin_unlock_irqrestore(&hw->cmd_lock, flags);
		return -EIO;
	}

	hw->cmd_head_count--;

	/* Post any pending requests */
	efct_hw_cmd_submit_pending(hw);

	spin_unlock_irqrestore(&hw->cmd_lock, flags);

	if (ctx->cb) {
		memcpy(ctx->buf, mqe, size);
		ctx->cb(hw, status, ctx->buf, ctx->arg);
	}

	mempool_free(ctx, hw->cmd_ctx_pool);

	return 0;
}

static int
efct_hw_mq_process(struct efct_hw *hw,
		   int status, struct sli4_queue *mq)
{
	u8 mqe[SLI4_BMBX_SIZE];
	int rc;

	rc = sli_mq_read(&hw->sli, mq, mqe);
	if (!rc)
		rc = efct_hw_command_process(hw, status, mqe, mq->size);

	return rc;
}

static int
efct_hw_command_cancel(struct efct_hw *hw)
{
	unsigned long flags = 0;
	int rc = 0;

	spin_lock_irqsave(&hw->cmd_lock, flags);

	/*
	 * Manually clean up remaining commands. Note: since this calls
	 * efct_hw_command_process(), we'll also process the cmd_pending
	 * list, so no need to manually clean that out.
	 */
	while (!list_empty(&hw->cmd_head)) {
		u8		mqe[SLI4_BMBX_SIZE] = { 0 };
		struct efct_command_ctx *ctx;

		ctx = list_first_entry(&hw->cmd_head,
				       struct efct_command_ctx, list_entry);

		efc_log_debug(hw->os, "hung command %08x\n",
			      !ctx ? U32_MAX : *((u32 *)ctx->buf));
		spin_unlock_irqrestore(&hw->cmd_lock, flags);
		rc = efct_hw_command_process(hw, -1, mqe, SLI4_BMBX_SIZE);
		spin_lock_irqsave(&hw->cmd_lock, flags);
	}

	spin_unlock_irqrestore(&hw->cmd_lock, flags);

	return rc;
}

static void
efct_mbox_rsp_cb(struct efct_hw *hw, int status, u8 *mqe, void *arg)
{
	struct efct_mbox_rqst_ctx *ctx = arg;

	if (ctx) {
		if (ctx->callback)
			(*ctx->callback)(hw->os->efcport, status, mqe,
					 ctx->arg);

		mempool_free(ctx, hw->mbox_rqst_pool);
	}
}

int
efct_issue_mbox_rqst(void *base, void *cmd, void *cb, void *arg)
{
	struct efct_mbox_rqst_ctx *ctx;
	struct efct *efct = base;
	struct efct_hw *hw = &efct->hw;
	int rc;

	/*
	 * Allocate a callback context (which includes the mbox cmd buffer),
	 * we need this to be persistent as the mbox cmd submission may be
	 * queued and executed later execution.
	 */
	ctx = mempool_alloc(hw->mbox_rqst_pool, GFP_ATOMIC);
	if (!ctx)
		return -EIO;

	ctx->callback = cb;
	ctx->arg = arg;

	rc = efct_hw_command(hw, cmd, EFCT_CMD_NOWAIT, efct_mbox_rsp_cb, ctx);
	if (rc) {
		efc_log_err(efct, "issue mbox rqst failure rc:%d\n", rc);
		mempool_free(ctx, hw->mbox_rqst_pool);
		return -EIO;
	}

	return 0;
}

static inline struct efct_hw_io *
_efct_hw_io_alloc(struct efct_hw *hw)
{
	struct efct_hw_io *io = NULL;

	if (!list_empty(&hw->io_free)) {
		io = list_first_entry(&hw->io_free, struct efct_hw_io,
				      list_entry);
		list_del(&io->list_entry);
	}
	if (io) {
		INIT_LIST_HEAD(&io->list_entry);
		list_add_tail(&io->list_entry, &hw->io_inuse);
		io->state = EFCT_HW_IO_STATE_INUSE;
		io->abort_reqtag = U32_MAX;
		io->wq = hw->wq_cpu_array[raw_smp_processor_id()];
		if (!io->wq) {
			efc_log_err(hw->os, "WQ not assigned for cpu:%d\n",
				    raw_smp_processor_id());
			io->wq = hw->hw_wq[0];
		}
		kref_init(&io->ref);
		io->release = efct_hw_io_free_internal;
	} else {
		atomic_add(1, &hw->io_alloc_failed_count);
	}

	return io;
}

struct efct_hw_io *
efct_hw_io_alloc(struct efct_hw *hw)
{
	struct efct_hw_io *io = NULL;
	unsigned long flags = 0;

	spin_lock_irqsave(&hw->io_lock, flags);
	io = _efct_hw_io_alloc(hw);
	spin_unlock_irqrestore(&hw->io_lock, flags);

	return io;
}

static void
efct_hw_io_free_move_correct_list(struct efct_hw *hw,
				  struct efct_hw_io *io)
{
	/*
	 * When an IO is freed, depending on the exchange busy flag,
	 * move it to the correct list.
	 */
	if (io->xbusy) {
		/*
		 * add to wait_free list and wait for XRI_ABORTED CQEs to clean
		 * up
		 */
		INIT_LIST_HEAD(&io->list_entry);
		list_add_tail(&io->list_entry, &hw->io_wait_free);
		io->state = EFCT_HW_IO_STATE_WAIT_FREE;
	} else {
		/* IO not busy, add to free list */
		INIT_LIST_HEAD(&io->list_entry);
		list_add_tail(&io->list_entry, &hw->io_free);
		io->state = EFCT_HW_IO_STATE_FREE;
	}
}

static inline void
efct_hw_io_free_common(struct efct_hw *hw, struct efct_hw_io *io)
{
	/* initialize IO fields */
	efct_hw_init_free_io(io);

	/* Restore default SGL */
	efct_hw_io_restore_sgl(hw, io);
}

void
efct_hw_io_free_internal(struct kref *arg)
{
	unsigned long flags = 0;
	struct efct_hw_io *io =	container_of(arg, struct efct_hw_io, ref);
	struct efct_hw *hw = io->hw;

	/* perform common cleanup */
	efct_hw_io_free_common(hw, io);

	spin_lock_irqsave(&hw->io_lock, flags);
	/* remove from in-use list */
	if (!list_empty(&io->list_entry) && !list_empty(&hw->io_inuse)) {
		list_del_init(&io->list_entry);
		efct_hw_io_free_move_correct_list(hw, io);
	}
	spin_unlock_irqrestore(&hw->io_lock, flags);
}

int
efct_hw_io_free(struct efct_hw *hw, struct efct_hw_io *io)
{
	return kref_put(&io->ref, io->release);
}

struct efct_hw_io *
efct_hw_io_lookup(struct efct_hw *hw, u32 xri)
{
	u32 ioindex;

	ioindex = xri - hw->sli.ext[SLI4_RSRC_XRI].base[0];
	return hw->io[ioindex];
}

int
efct_hw_io_init_sges(struct efct_hw *hw, struct efct_hw_io *io,
		     enum efct_hw_io_type type)
{
	struct sli4_sge	*data = NULL;
	u32 i = 0;
	u32 skips = 0;
	u32 sge_flags = 0;

	if (!io) {
		efc_log_err(hw->os, "bad parameter hw=%p io=%p\n", hw, io);
		return -EIO;
	}

	/* Clear / reset the scatter-gather list */
	io->sgl = &io->def_sgl;
	io->sgl_count = io->def_sgl_count;
	io->first_data_sge = 0;

	memset(io->sgl->virt, 0, 2 * sizeof(struct sli4_sge));
	io->n_sge = 0;
	io->sge_offset = 0;

	io->type = type;

	data = io->sgl->virt;

	/*
	 * Some IO types have underlying hardware requirements on the order
	 * of SGEs. Process all special entries here.
	 */
	switch (type) {
	case EFCT_HW_IO_TARGET_WRITE:

		/* populate host resident XFER_RDY buffer */
		sge_flags = le32_to_cpu(data->dw2_flags);
		sge_flags &= (~SLI4_SGE_TYPE_MASK);
		sge_flags |= (SLI4_SGE_TYPE_DATA << SLI4_SGE_TYPE_SHIFT);
		data->buffer_address_high =
			cpu_to_le32(upper_32_bits(io->xfer_rdy.phys));
		data->buffer_address_low  =
			cpu_to_le32(lower_32_bits(io->xfer_rdy.phys));
		data->buffer_length = cpu_to_le32(io->xfer_rdy.size);
		data->dw2_flags = cpu_to_le32(sge_flags);
		data++;

		skips = EFCT_TARGET_WRITE_SKIPS;

		io->n_sge = 1;
		break;
	case EFCT_HW_IO_TARGET_READ:
		/*
		 * For FCP_TSEND64, the first 2 entries are SKIP SGE's
		 */
		skips = EFCT_TARGET_READ_SKIPS;
		break;
	case EFCT_HW_IO_TARGET_RSP:
		/*
		 * No skips, etc. for FCP_TRSP64
		 */
		break;
	default:
		efc_log_err(hw->os, "unsupported IO type %#x\n", type);
		return -EIO;
	}

	/*
	 * Write skip entries
	 */
	for (i = 0; i < skips; i++) {
		sge_flags = le32_to_cpu(data->dw2_flags);
		sge_flags &= (~SLI4_SGE_TYPE_MASK);
		sge_flags |= (SLI4_SGE_TYPE_SKIP << SLI4_SGE_TYPE_SHIFT);
		data->dw2_flags = cpu_to_le32(sge_flags);
		data++;
	}

	io->n_sge += skips;

	/*
	 * Set last
	 */
	sge_flags = le32_to_cpu(data->dw2_flags);
	sge_flags |= SLI4_SGE_LAST;
	data->dw2_flags = cpu_to_le32(sge_flags);

	return 0;
}

int
efct_hw_io_add_sge(struct efct_hw *hw, struct efct_hw_io *io,
		   uintptr_t addr, u32 length)
{
	struct sli4_sge	*data = NULL;
	u32 sge_flags = 0;

	if (!io || !addr || !length) {
		efc_log_err(hw->os,
			    "bad parameter hw=%p io=%p addr=%lx length=%u\n",
			    hw, io, addr, length);
		return -EIO;
	}

	if (length > hw->sli.sge_supported_length) {
		efc_log_err(hw->os,
			    "length of SGE %d bigger than allowed %d\n",
			    length, hw->sli.sge_supported_length);
		return -EIO;
	}

	data = io->sgl->virt;
	data += io->n_sge;

	sge_flags = le32_to_cpu(data->dw2_flags);
	sge_flags &= ~SLI4_SGE_TYPE_MASK;
	sge_flags |= SLI4_SGE_TYPE_DATA << SLI4_SGE_TYPE_SHIFT;
	sge_flags &= ~SLI4_SGE_DATA_OFFSET_MASK;
	sge_flags |= SLI4_SGE_DATA_OFFSET_MASK & io->sge_offset;

	data->buffer_address_high = cpu_to_le32(upper_32_bits(addr));
	data->buffer_address_low  = cpu_to_le32(lower_32_bits(addr));
	data->buffer_length = cpu_to_le32(length);

	/*
	 * Always assume this is the last entry and mark as such.
	 * If this is not the first entry unset the "last SGE"
	 * indication for the previous entry
	 */
	sge_flags |= SLI4_SGE_LAST;
	data->dw2_flags = cpu_to_le32(sge_flags);

	if (io->n_sge) {
		sge_flags = le32_to_cpu(data[-1].dw2_flags);
		sge_flags &= ~SLI4_SGE_LAST;
		data[-1].dw2_flags = cpu_to_le32(sge_flags);
	}

	/* Set first_data_bde if not previously set */
	if (io->first_data_sge == 0)
		io->first_data_sge = io->n_sge;

	io->sge_offset += length;
	io->n_sge++;

	return 0;
}

void
efct_hw_io_abort_all(struct efct_hw *hw)
{
	struct efct_hw_io *io_to_abort	= NULL;
	struct efct_hw_io *next_io = NULL;

	list_for_each_entry_safe(io_to_abort, next_io,
				 &hw->io_inuse, list_entry) {
		efct_hw_io_abort(hw, io_to_abort, true, NULL, NULL);
	}
}

static void
efct_hw_wq_process_abort(void *arg, u8 *cqe, int status)
{
	struct efct_hw_io *io = arg;
	struct efct_hw *hw = io->hw;
	u32 ext = 0;
	u32 len = 0;
	struct hw_wq_callback *wqcb;

	/*
	 * For IOs that were aborted internally, we may need to issue the
	 * callback here depending on whether a XRI_ABORTED CQE is expected ot
	 * not. If the status is Local Reject/No XRI, then
	 * issue the callback now.
	 */
	ext = sli_fc_ext_status(&hw->sli, cqe);
	if (status == SLI4_FC_WCQE_STATUS_LOCAL_REJECT &&
	    ext == SLI4_FC_LOCAL_REJECT_NO_XRI && io->done) {
		efct_hw_done_t done = io->done;

		io->done = NULL;

		/*
		 * Use latched status as this is always saved for an internal
		 * abort Note: We won't have both a done and abort_done
		 * function, so don't worry about
		 *       clobbering the len, status and ext fields.
		 */
		status = io->saved_status;
		len = io->saved_len;
		ext = io->saved_ext;
		io->status_saved = false;
		done(io, len, status, ext, io->arg);
	}

	if (io->abort_done) {
		efct_hw_done_t done = io->abort_done;

		io->abort_done = NULL;
		done(io, len, status, ext, io->abort_arg);
	}

	/* clear abort bit to indicate abort is complete */
	io->abort_in_progress = false;

	/* Free the WQ callback */
	if (io->abort_reqtag == U32_MAX) {
		efc_log_err(hw->os, "HW IO already freed\n");
		return;
	}

	wqcb = efct_hw_reqtag_get_instance(hw, io->abort_reqtag);
	efct_hw_reqtag_free(hw, wqcb);

	/*
	 * Call efct_hw_io_free() because this releases the WQ reservation as
	 * well as doing the refcount put. Don't duplicate the code here.
	 */
	(void)efct_hw_io_free(hw, io);
}

static void
efct_hw_fill_abort_wqe(struct efct_hw *hw, struct efct_hw_wqe *wqe)
{
	struct sli4_abort_wqe *abort = (void *)wqe->wqebuf;

	memset(abort, 0, hw->sli.wqe_size);

	abort->criteria = SLI4_ABORT_CRITERIA_XRI_TAG;
	abort->ia_ir_byte |= wqe->send_abts ? 0 : 1;

	/* Suppress ABTS retries */
	abort->ia_ir_byte |= SLI4_ABRT_WQE_IR;

	abort->t_tag  = cpu_to_le32(wqe->id);
	abort->command = SLI4_WQE_ABORT;
	abort->request_tag = cpu_to_le16(wqe->abort_reqtag);

	abort->dw10w0_flags = cpu_to_le16(SLI4_ABRT_WQE_QOSD);

	abort->cq_id = cpu_to_le16(SLI4_CQ_DEFAULT);
}

int
efct_hw_io_abort(struct efct_hw *hw, struct efct_hw_io *io_to_abort,
		 bool send_abts, void *cb, void *arg)
{
	struct hw_wq_callback *wqcb;
	unsigned long flags = 0;

	if (!io_to_abort) {
		efc_log_err(hw->os, "bad parameter hw=%p io=%p\n",
			    hw, io_to_abort);
		return -EIO;
	}

	if (hw->state != EFCT_HW_STATE_ACTIVE) {
		efc_log_err(hw->os, "cannot send IO abort, HW state=%d\n",
			    hw->state);
		return -EIO;
	}

	/* take a reference on IO being aborted */
	if (kref_get_unless_zero(&io_to_abort->ref) == 0) {
		/* command no longer active */
		efc_log_debug(hw->os,
			      "io not active xri=0x%x tag=0x%x\n",
			      io_to_abort->indicator, io_to_abort->reqtag);
		return -ENOENT;
	}

	/* Must have a valid WQ reference */
	if (!io_to_abort->wq) {
		efc_log_debug(hw->os, "io_to_abort xri=0x%x not active on WQ\n",
			      io_to_abort->indicator);
		/* efct_ref_get(): same function */
		kref_put(&io_to_abort->ref, io_to_abort->release);
		return -ENOENT;
	}

	/*
	 * Validation checks complete; now check to see if already being
	 * aborted, if not set the flag.
	 */
	if (cmpxchg(&io_to_abort->abort_in_progress, false, true)) {
		/* efct_ref_get(): same function */
		kref_put(&io_to_abort->ref, io_to_abort->release);
		efc_log_debug(hw->os,
			      "io already being aborted xri=0x%x tag=0x%x\n",
			      io_to_abort->indicator, io_to_abort->reqtag);
		return -EINPROGRESS;
	}

	/*
	 * If we got here, the possibilities are:
	 * - host owned xri
	 *	- io_to_abort->wq_index != U32_MAX
	 *		- submit ABORT_WQE to same WQ
	 * - port owned xri:
	 *	- rxri: io_to_abort->wq_index == U32_MAX
	 *		- submit ABORT_WQE to any WQ
	 *	- non-rxri
	 *		- io_to_abort->index != U32_MAX
	 *			- submit ABORT_WQE to same WQ
	 *		- io_to_abort->index == U32_MAX
	 *			- submit ABORT_WQE to any WQ
	 */
	io_to_abort->abort_done = cb;
	io_to_abort->abort_arg  = arg;

	/* Allocate a request tag for the abort portion of this IO */
	wqcb = efct_hw_reqtag_alloc(hw, efct_hw_wq_process_abort, io_to_abort);
	if (!wqcb) {
		efc_log_err(hw->os, "can't allocate request tag\n");
		return -ENOSPC;
	}

	io_to_abort->abort_reqtag = wqcb->instance_index;
	io_to_abort->wqe.send_abts = send_abts;
	io_to_abort->wqe.id = io_to_abort->indicator;
	io_to_abort->wqe.abort_reqtag = io_to_abort->abort_reqtag;

	/*
	 * If the wqe is on the pending list, then set this wqe to be
	 * aborted when the IO's wqe is removed from the list.
	 */
	if (io_to_abort->wq) {
		spin_lock_irqsave(&io_to_abort->wq->queue->lock, flags);
		if (io_to_abort->wqe.list_entry.next) {
			io_to_abort->wqe.abort_wqe_submit_needed = true;
			spin_unlock_irqrestore(&io_to_abort->wq->queue->lock,
					       flags);
			return 0;
		}
		spin_unlock_irqrestore(&io_to_abort->wq->queue->lock, flags);
	}

	efct_hw_fill_abort_wqe(hw, &io_to_abort->wqe);

	/* ABORT_WQE does not actually utilize an XRI on the Port,
	 * therefore, keep xbusy as-is to track the exchange's state,
	 * not the ABORT_WQE's state
	 */
	if (efct_hw_wq_write(io_to_abort->wq, &io_to_abort->wqe)) {
		io_to_abort->abort_in_progress = false;
		/* efct_ref_get(): same function */
		kref_put(&io_to_abort->ref, io_to_abort->release);
		return -EIO;
	}

	return 0;
}

void
efct_hw_reqtag_pool_free(struct efct_hw *hw)
{
	u32 i;
	struct reqtag_pool *reqtag_pool = hw->wq_reqtag_pool;
	struct hw_wq_callback *wqcb = NULL;

	if (reqtag_pool) {
		for (i = 0; i < U16_MAX; i++) {
			wqcb = reqtag_pool->tags[i];
			if (!wqcb)
				continue;

			kfree(wqcb);
		}
		kfree(reqtag_pool);
		hw->wq_reqtag_pool = NULL;
	}
}

struct reqtag_pool *
efct_hw_reqtag_pool_alloc(struct efct_hw *hw)
{
	u32 i = 0;
	struct reqtag_pool *reqtag_pool;
	struct hw_wq_callback *wqcb;

	reqtag_pool = kzalloc(sizeof(*reqtag_pool), GFP_KERNEL);
	if (!reqtag_pool)
		return NULL;

	INIT_LIST_HEAD(&reqtag_pool->freelist);
	/* initialize reqtag pool lock */
	spin_lock_init(&reqtag_pool->lock);
	for (i = 0; i < U16_MAX; i++) {
		wqcb = kmalloc(sizeof(*wqcb), GFP_KERNEL);
		if (!wqcb)
			break;

		reqtag_pool->tags[i] = wqcb;
		wqcb->instance_index = i;
		wqcb->callback = NULL;
		wqcb->arg = NULL;
		INIT_LIST_HEAD(&wqcb->list_entry);
		list_add_tail(&wqcb->list_entry, &reqtag_pool->freelist);
	}

	return reqtag_pool;
}

struct hw_wq_callback *
efct_hw_reqtag_alloc(struct efct_hw *hw,
		     void (*callback)(void *arg, u8 *cqe, int status),
		     void *arg)
{
	struct hw_wq_callback *wqcb = NULL;
	struct reqtag_pool *reqtag_pool = hw->wq_reqtag_pool;
	unsigned long flags = 0;

	if (!callback)
		return wqcb;

	spin_lock_irqsave(&reqtag_pool->lock, flags);

	if (!list_empty(&reqtag_pool->freelist)) {
		wqcb = list_first_entry(&reqtag_pool->freelist,
					struct hw_wq_callback, list_entry);
	}

	if (wqcb) {
		list_del_init(&wqcb->list_entry);
		spin_unlock_irqrestore(&reqtag_pool->lock, flags);
		wqcb->callback = callback;
		wqcb->arg = arg;
	} else {
		spin_unlock_irqrestore(&reqtag_pool->lock, flags);
	}

	return wqcb;
}

void
efct_hw_reqtag_free(struct efct_hw *hw, struct hw_wq_callback *wqcb)
{
	unsigned long flags = 0;
	struct reqtag_pool *reqtag_pool = hw->wq_reqtag_pool;

	if (!wqcb->callback)
		efc_log_err(hw->os, "WQCB is already freed\n");

	spin_lock_irqsave(&reqtag_pool->lock, flags);
	wqcb->callback = NULL;
	wqcb->arg = NULL;
	INIT_LIST_HEAD(&wqcb->list_entry);
	list_add(&wqcb->list_entry, &hw->wq_reqtag_pool->freelist);
	spin_unlock_irqrestore(&reqtag_pool->lock, flags);
}

struct hw_wq_callback *
efct_hw_reqtag_get_instance(struct efct_hw *hw, u32 instance_index)
{
	struct hw_wq_callback *wqcb;

	wqcb = hw->wq_reqtag_pool->tags[instance_index];
	if (!wqcb)
		efc_log_err(hw->os, "wqcb for instance %d is null\n",
			    instance_index);

	return wqcb;
}

int
efct_hw_queue_hash_find(struct efct_queue_hash *hash, u16 id)
{
	int index = -1;
	int i = id & (EFCT_HW_Q_HASH_SIZE - 1);

	/*
	 * Since the hash is always bigger than the maximum number of Qs, then
	 * we never have to worry about an infinite loop. We will always find
	 * an unused entry.
	 */
	do {
		if (hash[i].in_use && hash[i].id == id)
			index = hash[i].index;
		else
			i = (i + 1) & (EFCT_HW_Q_HASH_SIZE - 1);
	} while (index == -1 && hash[i].in_use);

	return index;
}

int
efct_hw_process(struct efct_hw *hw, u32 vector,
		u32 max_isr_time_msec)
{
	struct hw_eq *eq;

	/*
	 * The caller should disable interrupts if they wish to prevent us
	 * from processing during a shutdown. The following states are defined:
	 *   EFCT_HW_STATE_UNINITIALIZED - No queues allocated
	 *   EFCT_HW_STATE_QUEUES_ALLOCATED - The state after a chip reset,
	 *                                    queues are cleared.
	 *   EFCT_HW_STATE_ACTIVE - Chip and queues are operational
	 *   EFCT_HW_STATE_RESET_IN_PROGRESS - reset, we still want completions
	 *   EFCT_HW_STATE_TEARDOWN_IN_PROGRESS - We still want mailbox
	 *                                        completions.
	 */
	if (hw->state == EFCT_HW_STATE_UNINITIALIZED)
		return 0;

	/* Get pointer to struct hw_eq */
	eq = hw->hw_eq[vector];
	if (!eq)
		return 0;

	eq->use_count++;

	return efct_hw_eq_process(hw, eq, max_isr_time_msec);
}

int
efct_hw_eq_process(struct efct_hw *hw, struct hw_eq *eq,
		   u32 max_isr_time_msec)
{
	u8 eqe[sizeof(struct sli4_eqe)] = { 0 };
	u32 tcheck_count;
	u64 tstart;
	u64 telapsed;
	bool done = false;

	tcheck_count = EFCT_HW_TIMECHECK_ITERATIONS;
	tstart = jiffies_to_msecs(jiffies);

	while (!done && !sli_eq_read(&hw->sli, eq->queue, eqe)) {
		u16 cq_id = 0;
		int rc;

		rc = sli_eq_parse(&hw->sli, eqe, &cq_id);
		if (unlikely(rc)) {
			if (rc == SLI4_EQE_STATUS_EQ_FULL) {
				u32 i;

				/*
				 * Received a sentinel EQE indicating the
				 * EQ is full. Process all CQs
				 */
				for (i = 0; i < hw->cq_count; i++)
					efct_hw_cq_process(hw, hw->hw_cq[i]);
				continue;
			} else {
				return rc;
			}
		} else {
			int index;

			index  = efct_hw_queue_hash_find(hw->cq_hash, cq_id);

			if (likely(index >= 0))
				efct_hw_cq_process(hw, hw->hw_cq[index]);
			else
				efc_log_err(hw->os, "bad CQ_ID %#06x\n", cq_id);
		}

		if (eq->queue->n_posted > eq->queue->posted_limit)
			sli_queue_arm(&hw->sli, eq->queue, false);

		if (tcheck_count && (--tcheck_count == 0)) {
			tcheck_count = EFCT_HW_TIMECHECK_ITERATIONS;
			telapsed = jiffies_to_msecs(jiffies) - tstart;
			if (telapsed >= max_isr_time_msec)
				done = true;
		}
	}
	sli_queue_eq_arm(&hw->sli, eq->queue, true);

	return 0;
}

static int
_efct_hw_wq_write(struct hw_wq *wq, struct efct_hw_wqe *wqe)
{
	int queue_rc;

	/* Every so often, set the wqec bit to generate comsummed completions */
	if (wq->wqec_count)
		wq->wqec_count--;

	if (wq->wqec_count == 0) {
		struct sli4_generic_wqe *genwqe = (void *)wqe->wqebuf;

		genwqe->cmdtype_wqec_byte |= SLI4_GEN_WQE_WQEC;
		wq->wqec_count = wq->wqec_set_count;
	}

	/* Decrement WQ free count */
	wq->free_count--;

	queue_rc = sli_wq_write(&wq->hw->sli, wq->queue, wqe->wqebuf);

	return (queue_rc < 0) ? -EIO : 0;
}

static void
hw_wq_submit_pending(struct hw_wq *wq, u32 update_free_count)
{
	struct efct_hw_wqe *wqe;
	unsigned long flags = 0;

	spin_lock_irqsave(&wq->queue->lock, flags);

	/* Update free count with value passed in */
	wq->free_count += update_free_count;

	while ((wq->free_count > 0) && (!list_empty(&wq->pending_list))) {
		wqe = list_first_entry(&wq->pending_list,
				       struct efct_hw_wqe, list_entry);
		list_del_init(&wqe->list_entry);
		_efct_hw_wq_write(wq, wqe);

		if (wqe->abort_wqe_submit_needed) {
			wqe->abort_wqe_submit_needed = false;
			efct_hw_fill_abort_wqe(wq->hw, wqe);
			INIT_LIST_HEAD(&wqe->list_entry);
			list_add_tail(&wqe->list_entry, &wq->pending_list);
			wq->wq_pending_count++;
		}
	}

	spin_unlock_irqrestore(&wq->queue->lock, flags);
}

void
efct_hw_cq_process(struct efct_hw *hw, struct hw_cq *cq)
{
	u8 cqe[sizeof(struct sli4_mcqe)];
	u16 rid = U16_MAX;
	/* completion type */
	enum sli4_qentry ctype;
	u32 n_processed = 0;
	u32 tstart, telapsed;

	tstart = jiffies_to_msecs(jiffies);

	while (!sli_cq_read(&hw->sli, cq->queue, cqe)) {
		int status;

		status = sli_cq_parse(&hw->sli, cq->queue, cqe, &ctype, &rid);
		/*
		 * The sign of status is significant. If status is:
		 * == 0 : call completed correctly and
		 * the CQE indicated success
		 * > 0 : call completed correctly and
		 * the CQE indicated an error
		 * < 0 : call failed and no information is available about the
		 * CQE
		 */
		if (status < 0) {
			if (status == SLI4_MCQE_STATUS_NOT_COMPLETED)
				/*
				 * Notification that an entry was consumed,
				 * but not completed
				 */
				continue;

			break;
		}

		switch (ctype) {
		case SLI4_QENTRY_ASYNC:
			sli_cqe_async(&hw->sli, cqe);
			break;
		case SLI4_QENTRY_MQ:
			/*
			 * Process MQ entry. Note there is no way to determine
			 * the MQ_ID from the completion entry.
			 */
			efct_hw_mq_process(hw, status, hw->mq);
			break;
		case SLI4_QENTRY_WQ:
			efct_hw_wq_process(hw, cq, cqe, status, rid);
			break;
		case SLI4_QENTRY_WQ_RELEASE: {
			u32 wq_id = rid;
			int index;
			struct hw_wq *wq = NULL;

			index = efct_hw_queue_hash_find(hw->wq_hash, wq_id);

			if (likely(index >= 0)) {
				wq = hw->hw_wq[index];
			} else {
				efc_log_err(hw->os, "bad WQ_ID %#06x\n", wq_id);
				break;
			}
			/* Submit any HW IOs that are on the WQ pending list */
			hw_wq_submit_pending(wq, wq->wqec_set_count);

			break;
		}

		case SLI4_QENTRY_RQ:
			efct_hw_rqpair_process_rq(hw, cq, cqe);
			break;
		case SLI4_QENTRY_XABT: {
			efct_hw_xabt_process(hw, cq, cqe, rid);
			break;
		}
		default:
			efc_log_debug(hw->os, "unhandled ctype=%#x rid=%#x\n",
				      ctype, rid);
			break;
		}

		n_processed++;
		if (n_processed == cq->queue->proc_limit)
			break;

		if (cq->queue->n_posted >= cq->queue->posted_limit)
			sli_queue_arm(&hw->sli, cq->queue, false);
	}

	sli_queue_arm(&hw->sli, cq->queue, true);

	if (n_processed > cq->queue->max_num_processed)
		cq->queue->max_num_processed = n_processed;
	telapsed = jiffies_to_msecs(jiffies) - tstart;
	if (telapsed > cq->queue->max_process_time)
		cq->queue->max_process_time = telapsed;
}

void
efct_hw_wq_process(struct efct_hw *hw, struct hw_cq *cq,
		   u8 *cqe, int status, u16 rid)
{
	struct hw_wq_callback *wqcb;

	if (rid == EFCT_HW_REQUE_XRI_REGTAG) {
		if (status)
			efc_log_err(hw->os, "reque xri failed, status = %d\n",
				    status);
		return;
	}

	wqcb = efct_hw_reqtag_get_instance(hw, rid);
	if (!wqcb) {
		efc_log_err(hw->os, "invalid request tag: x%x\n", rid);
		return;
	}

	if (!wqcb->callback) {
		efc_log_err(hw->os, "wqcb callback is NULL\n");
		return;
	}

	(*wqcb->callback)(wqcb->arg, cqe, status);
}

void
efct_hw_xabt_process(struct efct_hw *hw, struct hw_cq *cq,
		     u8 *cqe, u16 rid)
{
	/* search IOs wait free list */
	struct efct_hw_io *io = NULL;
	unsigned long flags = 0;

	io = efct_hw_io_lookup(hw, rid);
	if (!io) {
		/* IO lookup failure should never happen */
		efc_log_err(hw->os, "xabt io lookup failed rid=%#x\n", rid);
		return;
	}

	if (!io->xbusy)
		efc_log_debug(hw->os, "xabt io not busy rid=%#x\n", rid);
	else
		/* mark IO as no longer busy */
		io->xbusy = false;

	/*
	 * For IOs that were aborted internally, we need to issue any pending
	 * callback here.
	 */
	if (io->done) {
		efct_hw_done_t done = io->done;
		void		*arg = io->arg;

		/*
		 * Use latched status as this is always saved for an internal
		 * abort
		 */
		int status = io->saved_status;
		u32 len = io->saved_len;
		u32 ext = io->saved_ext;

		io->done = NULL;
		io->status_saved = false;

		done(io, len, status, ext, arg);
	}

	spin_lock_irqsave(&hw->io_lock, flags);
	if (io->state == EFCT_HW_IO_STATE_INUSE ||
	    io->state == EFCT_HW_IO_STATE_WAIT_FREE) {
		/* if on wait_free list, caller has already freed IO;
		 * remove from wait_free list and add to free list.
		 * if on in-use list, already marked as no longer busy;
		 * just leave there and wait for caller to free.
		 */
		if (io->state == EFCT_HW_IO_STATE_WAIT_FREE) {
			io->state = EFCT_HW_IO_STATE_FREE;
			list_del_init(&io->list_entry);
			efct_hw_io_free_move_correct_list(hw, io);
		}
	}
	spin_unlock_irqrestore(&hw->io_lock, flags);
}

static int
efct_hw_flush(struct efct_hw *hw)
{
	u32 i = 0;

	/* Process any remaining completions */
	for (i = 0; i < hw->eq_count; i++)
		efct_hw_process(hw, i, ~0);

	return 0;
}

int
efct_hw_wq_write(struct hw_wq *wq, struct efct_hw_wqe *wqe)
{
	int rc = 0;
	unsigned long flags = 0;

	spin_lock_irqsave(&wq->queue->lock, flags);
	if (list_empty(&wq->pending_list)) {
		if (wq->free_count > 0) {
			rc = _efct_hw_wq_write(wq, wqe);
		} else {
			INIT_LIST_HEAD(&wqe->list_entry);
			list_add_tail(&wqe->list_entry, &wq->pending_list);
			wq->wq_pending_count++;
		}

		spin_unlock_irqrestore(&wq->queue->lock, flags);
		return rc;
	}

	INIT_LIST_HEAD(&wqe->list_entry);
	list_add_tail(&wqe->list_entry, &wq->pending_list);
	wq->wq_pending_count++;
	while (wq->free_count > 0) {
		wqe = list_first_entry(&wq->pending_list, struct efct_hw_wqe,
				       list_entry);
		if (!wqe)
			break;

		list_del_init(&wqe->list_entry);
		rc = _efct_hw_wq_write(wq, wqe);
		if (rc)
			break;

		if (wqe->abort_wqe_submit_needed) {
			wqe->abort_wqe_submit_needed = false;
			efct_hw_fill_abort_wqe(wq->hw, wqe);

			INIT_LIST_HEAD(&wqe->list_entry);
			list_add_tail(&wqe->list_entry, &wq->pending_list);
			wq->wq_pending_count++;
		}
	}

	spin_unlock_irqrestore(&wq->queue->lock, flags);

	return rc;
}

int
efct_efc_bls_send(struct efc *efc, u32 type, struct sli_bls_params *bls)
{
	struct efct *efct = efc->base;

	return efct_hw_bls_send(efct, type, bls, NULL, NULL);
}

int
efct_hw_bls_send(struct efct *efct, u32 type, struct sli_bls_params *bls_params,
		 void *cb, void *arg)
{
	struct efct_hw *hw = &efct->hw;
	struct efct_hw_io *hio;
	struct sli_bls_payload bls;
	int rc;

	if (hw->state != EFCT_HW_STATE_ACTIVE) {
		efc_log_err(hw->os,
			    "cannot send BLS, HW state=%d\n", hw->state);
		return -EIO;
	}

	hio = efct_hw_io_alloc(hw);
	if (!hio) {
		efc_log_err(hw->os, "HIO allocation failed\n");
		return -EIO;
	}

	hio->done = cb;
	hio->arg  = arg;

	bls_params->xri = hio->indicator;
	bls_params->tag = hio->reqtag;

	if (type == FC_RCTL_BA_ACC) {
		hio->type = EFCT_HW_BLS_ACC;
		bls.type = SLI4_SLI_BLS_ACC;
		memcpy(&bls.u.acc, bls_params->payload, sizeof(bls.u.acc));
	} else {
		hio->type = EFCT_HW_BLS_RJT;
		bls.type = SLI4_SLI_BLS_RJT;
		memcpy(&bls.u.rjt, bls_params->payload, sizeof(bls.u.rjt));
	}

	bls.ox_id = cpu_to_le16(bls_params->ox_id);
	bls.rx_id = cpu_to_le16(bls_params->rx_id);

	if (sli_xmit_bls_rsp64_wqe(&hw->sli, hio->wqe.wqebuf,
				   &bls, bls_params)) {
		efc_log_err(hw->os, "XMIT_BLS_RSP64 WQE error\n");
		return -EIO;
	}

	hio->xbusy = true;

	/*
	 * Add IO to active io wqe list before submitting, in case the
	 * wcqe processing preempts this thread.
	 */
	hio->wq->use_count++;
	rc = efct_hw_wq_write(hio->wq, &hio->wqe);
	if (rc >= 0) {
		/* non-negative return is success */
		rc = 0;
	} else {
		/* failed to write wqe, remove from active wqe list */
		efc_log_err(hw->os,
			    "sli_queue_write failed: %d\n", rc);
		hio->xbusy = false;
	}

	return rc;
}

static int
efct_els_ssrs_send_cb(struct efct_hw_io *hio, u32 length, int status,
		      u32 ext_status, void *arg)
{
	struct efc_disc_io *io = arg;

	efc_disc_io_complete(io, length, status, ext_status);
	return 0;
}

static inline void
efct_fill_els_params(struct efc_disc_io *io, struct sli_els_params *params)
{
	u8 *cmd = io->req.virt;

	params->cmd = *cmd;
	params->s_id = io->s_id;
	params->d_id = io->d_id;
	params->ox_id = io->iparam.els.ox_id;
	params->rpi = io->rpi;
	params->vpi = io->vpi;
	params->rpi_registered = io->rpi_registered;
	params->xmit_len = io->xmit_len;
	params->rsp_len = io->rsp_len;
	params->timeout = io->iparam.els.timeout;
}

static inline void
efct_fill_ct_params(struct efc_disc_io *io, struct sli_ct_params *params)
{
	params->r_ctl = io->iparam.ct.r_ctl;
	params->type = io->iparam.ct.type;
	params->df_ctl =  io->iparam.ct.df_ctl;
	params->d_id = io->d_id;
	params->ox_id = io->iparam.ct.ox_id;
	params->rpi = io->rpi;
	params->vpi = io->vpi;
	params->rpi_registered = io->rpi_registered;
	params->xmit_len = io->xmit_len;
	params->rsp_len = io->rsp_len;
	params->timeout = io->iparam.ct.timeout;
}

/**
 * efct_els_hw_srrs_send() - Send a single request and response cmd.
 * @efc: efc library structure
 * @io: Discovery IO used to hold els and ct cmd context.
 *
 * This routine supports communication sequences consisting of a single
 * request and single response between two endpoints. Examples include:
 *  - Sending an ELS request.
 *  - Sending an ELS response - To send an ELS response, the caller must provide
 * the OX_ID from the received request.
 *  - Sending a FC Common Transport (FC-CT) request - To send a FC-CT request,
 * the caller must provide the R_CTL, TYPE, and DF_CTL
 * values to place in the FC frame header.
 *
 * Return: Status of the request.
 */
int
efct_els_hw_srrs_send(struct efc *efc, struct efc_disc_io *io)
{
	struct efct *efct = efc->base;
	struct efct_hw_io *hio;
	struct efct_hw *hw = &efct->hw;
	struct efc_dma *send = &io->req;
	struct efc_dma *receive = &io->rsp;
	struct sli4_sge	*sge = NULL;
	int rc = 0;
	u32 len = io->xmit_len;
	u32 sge0_flags;
	u32 sge1_flags;

	hio = efct_hw_io_alloc(hw);
	if (!hio) {
		pr_err("HIO alloc failed\n");
		return -EIO;
	}

	if (hw->state != EFCT_HW_STATE_ACTIVE) {
		efc_log_debug(hw->os,
			      "cannot send SRRS, HW state=%d\n", hw->state);
		return -EIO;
	}

	hio->done = efct_els_ssrs_send_cb;
	hio->arg  = io;

	sge = hio->sgl->virt;

	/* clear both SGE */
	memset(hio->sgl->virt, 0, 2 * sizeof(struct sli4_sge));

	sge0_flags = le32_to_cpu(sge[0].dw2_flags);
	sge1_flags = le32_to_cpu(sge[1].dw2_flags);
	if (send->size) {
		sge[0].buffer_address_high =
			cpu_to_le32(upper_32_bits(send->phys));
		sge[0].buffer_address_low  =
			cpu_to_le32(lower_32_bits(send->phys));

		sge0_flags |= (SLI4_SGE_TYPE_DATA << SLI4_SGE_TYPE_SHIFT);

		sge[0].buffer_length = cpu_to_le32(len);
	}

	if (io->io_type == EFC_DISC_IO_ELS_REQ ||
	    io->io_type == EFC_DISC_IO_CT_REQ) {
		sge[1].buffer_address_high =
			cpu_to_le32(upper_32_bits(receive->phys));
		sge[1].buffer_address_low  =
			cpu_to_le32(lower_32_bits(receive->phys));

		sge1_flags |= (SLI4_SGE_TYPE_DATA << SLI4_SGE_TYPE_SHIFT);
		sge1_flags |= SLI4_SGE_LAST;

		sge[1].buffer_length = cpu_to_le32(receive->size);
	} else {
		sge0_flags |= SLI4_SGE_LAST;
	}

	sge[0].dw2_flags = cpu_to_le32(sge0_flags);
	sge[1].dw2_flags = cpu_to_le32(sge1_flags);

	switch (io->io_type) {
	case EFC_DISC_IO_ELS_REQ: {
		struct sli_els_params els_params;

		hio->type = EFCT_HW_ELS_REQ;
		efct_fill_els_params(io, &els_params);
		els_params.xri = hio->indicator;
		els_params.tag = hio->reqtag;

		if (sli_els_request64_wqe(&hw->sli, hio->wqe.wqebuf, hio->sgl,
					  &els_params)) {
			efc_log_err(hw->os, "REQ WQE error\n");
			rc = -EIO;
		}
		break;
	}
	case EFC_DISC_IO_ELS_RESP: {
		struct sli_els_params els_params;

		hio->type = EFCT_HW_ELS_RSP;
		efct_fill_els_params(io, &els_params);
		els_params.xri = hio->indicator;
		els_params.tag = hio->reqtag;
		if (sli_xmit_els_rsp64_wqe(&hw->sli, hio->wqe.wqebuf, send,
					   &els_params)){
			efc_log_err(hw->os, "RSP WQE error\n");
			rc = -EIO;
		}
		break;
	}
	case EFC_DISC_IO_CT_REQ: {
		struct sli_ct_params ct_params;

		hio->type = EFCT_HW_FC_CT;
		efct_fill_ct_params(io, &ct_params);
		ct_params.xri = hio->indicator;
		ct_params.tag = hio->reqtag;
		if (sli_gen_request64_wqe(&hw->sli, hio->wqe.wqebuf, hio->sgl,
					  &ct_params)){
			efc_log_err(hw->os, "GEN WQE error\n");
			rc = -EIO;
		}
		break;
	}
	case EFC_DISC_IO_CT_RESP: {
		struct sli_ct_params ct_params;

		hio->type = EFCT_HW_FC_CT_RSP;
		efct_fill_ct_params(io, &ct_params);
		ct_params.xri = hio->indicator;
		ct_params.tag = hio->reqtag;
		if (sli_xmit_sequence64_wqe(&hw->sli, hio->wqe.wqebuf, hio->sgl,
					    &ct_params)){
			efc_log_err(hw->os, "XMIT SEQ WQE error\n");
			rc = -EIO;
		}
		break;
	}
	default:
		efc_log_err(hw->os, "bad SRRS type %#x\n", io->io_type);
		rc = -EIO;
	}

	if (rc == 0) {
		hio->xbusy = true;

		/*
		 * Add IO to active io wqe list before submitting, in case the
		 * wcqe processing preempts this thread.
		 */
		hio->wq->use_count++;
		rc = efct_hw_wq_write(hio->wq, &hio->wqe);
		if (rc >= 0) {
			/* non-negative return is success */
			rc = 0;
		} else {
			/* failed to write wqe, remove from active wqe list */
			efc_log_err(hw->os,
				    "sli_queue_write failed: %d\n", rc);
			hio->xbusy = false;
		}
	}

	return rc;
}

int
efct_hw_io_send(struct efct_hw *hw, enum efct_hw_io_type type,
		struct efct_hw_io *io, union efct_hw_io_param_u *iparam,
		void *cb, void *arg)
{
	int rc = 0;
	bool send_wqe = true;

	if (!io) {
		pr_err("bad parm hw=%p io=%p\n", hw, io);
		return -EIO;
	}

	if (hw->state != EFCT_HW_STATE_ACTIVE) {
		efc_log_err(hw->os, "cannot send IO, HW state=%d\n", hw->state);
		return -EIO;
	}

	/*
	 * Save state needed during later stages
	 */
	io->type  = type;
	io->done  = cb;
	io->arg   = arg;

	/*
	 * Format the work queue entry used to send the IO
	 */
	switch (type) {
	case EFCT_HW_IO_TARGET_WRITE: {
		u16 *flags = &iparam->fcp_tgt.flags;
		struct fcp_txrdy *xfer = io->xfer_rdy.virt;

		/*
		 * Fill in the XFER_RDY for IF_TYPE 0 devices
		 */
		xfer->ft_data_ro = cpu_to_be32(iparam->fcp_tgt.offset);
		xfer->ft_burst_len = cpu_to_be32(iparam->fcp_tgt.xmit_len);

		if (io->xbusy)
			*flags |= SLI4_IO_CONTINUATION;
		else
			*flags &= ~SLI4_IO_CONTINUATION;
		iparam->fcp_tgt.xri = io->indicator;
		iparam->fcp_tgt.tag = io->reqtag;

		if (sli_fcp_treceive64_wqe(&hw->sli, io->wqe.wqebuf,
					   &io->def_sgl, io->first_data_sge,
					   SLI4_CQ_DEFAULT,
					   0, 0, &iparam->fcp_tgt)) {
			efc_log_err(hw->os, "TRECEIVE WQE error\n");
			rc = -EIO;
		}
		break;
	}
	case EFCT_HW_IO_TARGET_READ: {
		u16 *flags = &iparam->fcp_tgt.flags;

		if (io->xbusy)
			*flags |= SLI4_IO_CONTINUATION;
		else
			*flags &= ~SLI4_IO_CONTINUATION;

		iparam->fcp_tgt.xri = io->indicator;
		iparam->fcp_tgt.tag = io->reqtag;

		if (sli_fcp_tsend64_wqe(&hw->sli, io->wqe.wqebuf,
					&io->def_sgl, io->first_data_sge,
					SLI4_CQ_DEFAULT,
					0, 0, &iparam->fcp_tgt)) {
			efc_log_err(hw->os, "TSEND WQE error\n");
			rc = -EIO;
		}
		break;
	}
	case EFCT_HW_IO_TARGET_RSP: {
		u16 *flags = &iparam->fcp_tgt.flags;

		if (io->xbusy)
			*flags |= SLI4_IO_CONTINUATION;
		else
			*flags &= ~SLI4_IO_CONTINUATION;

		iparam->fcp_tgt.xri = io->indicator;
		iparam->fcp_tgt.tag = io->reqtag;

		if (sli_fcp_trsp64_wqe(&hw->sli, io->wqe.wqebuf,
				       &io->def_sgl, SLI4_CQ_DEFAULT,
				       0, &iparam->fcp_tgt)) {
			efc_log_err(hw->os, "TRSP WQE error\n");
			rc = -EIO;
		}

		break;
	}
	default:
		efc_log_err(hw->os, "unsupported IO type %#x\n", type);
		rc = -EIO;
	}

	if (send_wqe && rc == 0) {
		io->xbusy = true;

		/*
		 * Add IO to active io wqe list before submitting, in case the
		 * wcqe processing preempts this thread.
		 */
		hw->tcmd_wq_submit[io->wq->instance]++;
		io->wq->use_count++;
		rc = efct_hw_wq_write(io->wq, &io->wqe);
		if (rc >= 0) {
			/* non-negative return is success */
			rc = 0;
		} else {
			/* failed to write wqe, remove from active wqe list */
			efc_log_err(hw->os,
				    "sli_queue_write failed: %d\n", rc);
			io->xbusy = false;
		}
	}

	return rc;
}

int
efct_hw_send_frame(struct efct_hw *hw, struct fc_frame_header *hdr,
		   u8 sof, u8 eof, struct efc_dma *payload,
		   struct efct_hw_send_frame_context *ctx,
		   void (*callback)(void *arg, u8 *cqe, int status),
		   void *arg)
{
	int rc;
	struct efct_hw_wqe *wqe;
	u32 xri;
	struct hw_wq *wq;

	wqe = &ctx->wqe;

	/* populate the callback object */
	ctx->hw = hw;

	/* Fetch and populate request tag */
	ctx->wqcb = efct_hw_reqtag_alloc(hw, callback, arg);
	if (!ctx->wqcb) {
		efc_log_err(hw->os, "can't allocate request tag\n");
		return -ENOSPC;
	}

	wq = hw->hw_wq[0];

	/* Set XRI and RX_ID in the header based on which WQ, and which
	 * send_frame_io we are using
	 */
	xri = wq->send_frame_io->indicator;

	/* Build the send frame WQE */
	rc = sli_send_frame_wqe(&hw->sli, wqe->wqebuf,
				sof, eof, (u32 *)hdr, payload, payload->len,
				EFCT_HW_SEND_FRAME_TIMEOUT, xri,
				ctx->wqcb->instance_index);
	if (rc) {
		efc_log_err(hw->os, "sli_send_frame_wqe failed: %d\n", rc);
		return -EIO;
	}

	/* Write to WQ */
	rc = efct_hw_wq_write(wq, wqe);
	if (rc) {
		efc_log_err(hw->os, "efct_hw_wq_write failed: %d\n", rc);
		return -EIO;
	}

	wq->use_count++;

	return 0;
}

static int
efct_hw_cb_link_stat(struct efct_hw *hw, int status,
		     u8 *mqe, void  *arg)
{
	struct sli4_cmd_read_link_stats *mbox_rsp;
	struct efct_hw_link_stat_cb_arg *cb_arg = arg;
	struct efct_hw_link_stat_counts counts[EFCT_HW_LINK_STAT_MAX];
	u32 num_counters, i;
	u32 mbox_rsp_flags = 0;

	mbox_rsp = (struct sli4_cmd_read_link_stats *)mqe;
	mbox_rsp_flags = le32_to_cpu(mbox_rsp->dw1_flags);
	num_counters = (mbox_rsp_flags & SLI4_READ_LNKSTAT_GEC) ? 20 : 13;
	memset(counts, 0, sizeof(struct efct_hw_link_stat_counts) *
				 EFCT_HW_LINK_STAT_MAX);

	/* Fill overflow counts, mask starts from SLI4_READ_LNKSTAT_W02OF*/
	for (i = 0; i < EFCT_HW_LINK_STAT_MAX; i++)
		counts[i].overflow = (mbox_rsp_flags & (1 << (i + 2)));

	counts[EFCT_HW_LINK_STAT_LINK_FAILURE_COUNT].counter =
		 le32_to_cpu(mbox_rsp->linkfail_errcnt);
	counts[EFCT_HW_LINK_STAT_LOSS_OF_SYNC_COUNT].counter =
		 le32_to_cpu(mbox_rsp->losssync_errcnt);
	counts[EFCT_HW_LINK_STAT_LOSS_OF_SIGNAL_COUNT].counter =
		 le32_to_cpu(mbox_rsp->losssignal_errcnt);
	counts[EFCT_HW_LINK_STAT_PRIMITIVE_SEQ_COUNT].counter =
		 le32_to_cpu(mbox_rsp->primseq_errcnt);
	counts[EFCT_HW_LINK_STAT_INVALID_XMIT_WORD_COUNT].counter =
		 le32_to_cpu(mbox_rsp->inval_txword_errcnt);
	counts[EFCT_HW_LINK_STAT_CRC_COUNT].counter =
		le32_to_cpu(mbox_rsp->crc_errcnt);
	counts[EFCT_HW_LINK_STAT_PRIMITIVE_SEQ_TIMEOUT_COUNT].counter =
		le32_to_cpu(mbox_rsp->primseq_eventtimeout_cnt);
	counts[EFCT_HW_LINK_STAT_ELASTIC_BUFFER_OVERRUN_COUNT].counter =
		 le32_to_cpu(mbox_rsp->elastic_bufoverrun_errcnt);
	counts[EFCT_HW_LINK_STAT_ARB_TIMEOUT_COUNT].counter =
		 le32_to_cpu(mbox_rsp->arbit_fc_al_timeout_cnt);
	counts[EFCT_HW_LINK_STAT_ADVERTISED_RCV_B2B_CREDIT].counter =
		 le32_to_cpu(mbox_rsp->adv_rx_buftor_to_buf_credit);
	counts[EFCT_HW_LINK_STAT_CURR_RCV_B2B_CREDIT].counter =
		 le32_to_cpu(mbox_rsp->curr_rx_buf_to_buf_credit);
	counts[EFCT_HW_LINK_STAT_ADVERTISED_XMIT_B2B_CREDIT].counter =
		 le32_to_cpu(mbox_rsp->adv_tx_buf_to_buf_credit);
	counts[EFCT_HW_LINK_STAT_CURR_XMIT_B2B_CREDIT].counter =
		 le32_to_cpu(mbox_rsp->curr_tx_buf_to_buf_credit);
	counts[EFCT_HW_LINK_STAT_RCV_EOFA_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_eofa_cnt);
	counts[EFCT_HW_LINK_STAT_RCV_EOFDTI_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_eofdti_cnt);
	counts[EFCT_HW_LINK_STAT_RCV_EOFNI_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_eofni_cnt);
	counts[EFCT_HW_LINK_STAT_RCV_SOFF_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_soff_cnt);
	counts[EFCT_HW_LINK_STAT_RCV_DROPPED_NO_AER_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_dropped_no_aer_cnt);
	counts[EFCT_HW_LINK_STAT_RCV_DROPPED_NO_RPI_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_dropped_no_avail_rpi_rescnt);
	counts[EFCT_HW_LINK_STAT_RCV_DROPPED_NO_XRI_COUNT].counter =
		 le32_to_cpu(mbox_rsp->rx_dropped_no_avail_xri_rescnt);

	if (cb_arg) {
		if (cb_arg->cb) {
			if (status == 0 && le16_to_cpu(mbox_rsp->hdr.status))
				status = le16_to_cpu(mbox_rsp->hdr.status);
			cb_arg->cb(status, num_counters, counts, cb_arg->arg);
		}

		kfree(cb_arg);
	}

	return 0;
}

int
efct_hw_get_link_stats(struct efct_hw *hw, u8 req_ext_counters,
		       u8 clear_overflow_flags, u8 clear_all_counters,
		       void (*cb)(int status, u32 num_counters,
				  struct efct_hw_link_stat_counts *counters,
				  void *arg),
		       void *arg)
{
	int rc = -EIO;
	struct efct_hw_link_stat_cb_arg *cb_arg;
	u8 mbxdata[SLI4_BMBX_SIZE];

	cb_arg = kzalloc(sizeof(*cb_arg), GFP_ATOMIC);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->cb = cb;
	cb_arg->arg = arg;

	/* Send the HW command */
	if (!sli_cmd_read_link_stats(&hw->sli, mbxdata, req_ext_counters,
				    clear_overflow_flags, clear_all_counters))
		rc = efct_hw_command(hw, mbxdata, EFCT_CMD_NOWAIT,
				     efct_hw_cb_link_stat, cb_arg);

	if (rc)
		kfree(cb_arg);

	return rc;
}

static int
efct_hw_cb_host_stat(struct efct_hw *hw, int status, u8 *mqe, void  *arg)
{
	struct sli4_cmd_read_status *mbox_rsp =
					(struct sli4_cmd_read_status *)mqe;
	struct efct_hw_host_stat_cb_arg *cb_arg = arg;
	struct efct_hw_host_stat_counts counts[EFCT_HW_HOST_STAT_MAX];
	u32 num_counters = EFCT_HW_HOST_STAT_MAX;

	memset(counts, 0, sizeof(struct efct_hw_host_stat_counts) *
	       EFCT_HW_HOST_STAT_MAX);

	counts[EFCT_HW_HOST_STAT_TX_KBYTE_COUNT].counter =
		 le32_to_cpu(mbox_rsp->trans_kbyte_cnt);
	counts[EFCT_HW_HOST_STAT_RX_KBYTE_COUNT].counter =
		 le32_to_cpu(mbox_rsp->recv_kbyte_cnt);
	counts[EFCT_HW_HOST_STAT_TX_FRAME_COUNT].counter =
		 le32_to_cpu(mbox_rsp->trans_frame_cnt);
	counts[EFCT_HW_HOST_STAT_RX_FRAME_COUNT].counter =
		 le32_to_cpu(mbox_rsp->recv_frame_cnt);
	counts[EFCT_HW_HOST_STAT_TX_SEQ_COUNT].counter =
		 le32_to_cpu(mbox_rsp->trans_seq_cnt);
	counts[EFCT_HW_HOST_STAT_RX_SEQ_COUNT].counter =
		 le32_to_cpu(mbox_rsp->recv_seq_cnt);
	counts[EFCT_HW_HOST_STAT_TOTAL_EXCH_ORIG].counter =
		 le32_to_cpu(mbox_rsp->tot_exchanges_orig);
	counts[EFCT_HW_HOST_STAT_TOTAL_EXCH_RESP].counter =
		 le32_to_cpu(mbox_rsp->tot_exchanges_resp);
	counts[EFCT_HW_HOSY_STAT_RX_P_BSY_COUNT].counter =
		 le32_to_cpu(mbox_rsp->recv_p_bsy_cnt);
	counts[EFCT_HW_HOST_STAT_RX_F_BSY_COUNT].counter =
		 le32_to_cpu(mbox_rsp->recv_f_bsy_cnt);
	counts[EFCT_HW_HOST_STAT_DROP_FRM_DUE_TO_NO_RQ_BUF_COUNT].counter =
		 le32_to_cpu(mbox_rsp->no_rq_buf_dropped_frames_cnt);
	counts[EFCT_HW_HOST_STAT_EMPTY_RQ_TIMEOUT_COUNT].counter =
		 le32_to_cpu(mbox_rsp->empty_rq_timeout_cnt);
	counts[EFCT_HW_HOST_STAT_DROP_FRM_DUE_TO_NO_XRI_COUNT].counter =
		 le32_to_cpu(mbox_rsp->no_xri_dropped_frames_cnt);
	counts[EFCT_HW_HOST_STAT_EMPTY_XRI_POOL_COUNT].counter =
		 le32_to_cpu(mbox_rsp->empty_xri_pool_cnt);

	if (cb_arg) {
		if (cb_arg->cb) {
			if (status == 0 && le16_to_cpu(mbox_rsp->hdr.status))
				status = le16_to_cpu(mbox_rsp->hdr.status);
			cb_arg->cb(status, num_counters, counts, cb_arg->arg);
		}

		kfree(cb_arg);
	}

	return 0;
}

int
efct_hw_get_host_stats(struct efct_hw *hw, u8 cc,
		       void (*cb)(int status, u32 num_counters,
				  struct efct_hw_host_stat_counts *counters,
				  void *arg),
		       void *arg)
{
	int rc = -EIO;
	struct efct_hw_host_stat_cb_arg *cb_arg;
	u8 mbxdata[SLI4_BMBX_SIZE];

	cb_arg = kmalloc(sizeof(*cb_arg), GFP_ATOMIC);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->cb = cb;
	cb_arg->arg = arg;

	 /* Send the HW command to get the host stats */
	if (!sli_cmd_read_status(&hw->sli, mbxdata, cc))
		rc = efct_hw_command(hw, mbxdata, EFCT_CMD_NOWAIT,
				     efct_hw_cb_host_stat, cb_arg);

	if (rc) {
		efc_log_debug(hw->os, "READ_HOST_STATS failed\n");
		kfree(cb_arg);
	}

	return rc;
}

struct efct_hw_async_call_ctx {
	efct_hw_async_cb_t callback;
	void *arg;
	u8 cmd[SLI4_BMBX_SIZE];
};

static void
efct_hw_async_cb(struct efct_hw *hw, int status, u8 *mqe, void *arg)
{
	struct efct_hw_async_call_ctx *ctx = arg;

	if (ctx) {
		if (ctx->callback)
			(*ctx->callback)(hw, status, mqe, ctx->arg);

		kfree(ctx);
	}
}

int
efct_hw_async_call(struct efct_hw *hw, efct_hw_async_cb_t callback, void *arg)
{
	struct efct_hw_async_call_ctx *ctx;
	int rc;

	/*
	 * Allocate a callback context (which includes the mbox cmd buffer),
	 * we need this to be persistent as the mbox cmd submission may be
	 * queued and executed later execution.
	 */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->callback = callback;
	ctx->arg = arg;

	/* Build and send a NOP mailbox command */
	if (sli_cmd_common_nop(&hw->sli, ctx->cmd, 0)) {
		efc_log_err(hw->os, "COMMON_NOP format failure\n");
		kfree(ctx);
		return -EIO;
	}

	rc = efct_hw_command(hw, ctx->cmd, EFCT_CMD_NOWAIT, efct_hw_async_cb,
			     ctx);
	if (rc) {
		efc_log_err(hw->os, "COMMON_NOP command failure, rc=%d\n", rc);
		kfree(ctx);
		return -EIO;
	}
	return 0;
}

static int
efct_hw_cb_fw_write(struct efct_hw *hw, int status, u8 *mqe, void  *arg)
{
	struct sli4_cmd_sli_config *mbox_rsp =
					(struct sli4_cmd_sli_config *)mqe;
	struct sli4_rsp_cmn_write_object *wr_obj_rsp;
	struct efct_hw_fw_wr_cb_arg *cb_arg = arg;
	u32 bytes_written;
	u16 mbox_status;
	u32 change_status;

	wr_obj_rsp = (struct sli4_rsp_cmn_write_object *)
		      &mbox_rsp->payload.embed;
	bytes_written = le32_to_cpu(wr_obj_rsp->actual_write_length);
	mbox_status = le16_to_cpu(mbox_rsp->hdr.status);
	change_status = (le32_to_cpu(wr_obj_rsp->change_status_dword) &
			 RSP_CHANGE_STATUS);

	if (cb_arg) {
		if (cb_arg->cb) {
			if (!status && mbox_status)
				status = mbox_status;
			cb_arg->cb(status, bytes_written, change_status,
				   cb_arg->arg);
		}

		kfree(cb_arg);
	}

	return 0;
}

int
efct_hw_firmware_write(struct efct_hw *hw, struct efc_dma *dma, u32 size,
		       u32 offset, int last,
		       void (*cb)(int status, u32 bytes_written,
				   u32 change_status, void *arg),
		       void *arg)
{
	int rc = -EIO;
	u8 mbxdata[SLI4_BMBX_SIZE];
	struct efct_hw_fw_wr_cb_arg *cb_arg;
	int noc = 0;

	cb_arg = kzalloc(sizeof(*cb_arg), GFP_KERNEL);
	if (!cb_arg)
		return -ENOMEM;

	cb_arg->cb = cb;
	cb_arg->arg = arg;

	/* Write a portion of a firmware image to the device */
	if (!sli_cmd_common_write_object(&hw->sli, mbxdata,
					 noc, last, size, offset, "/prg/",
					 dma))
		rc = efct_hw_command(hw, mbxdata, EFCT_CMD_NOWAIT,
				     efct_hw_cb_fw_write, cb_arg);

	if (rc != 0) {
		efc_log_debug(hw->os, "COMMON_WRITE_OBJECT failed\n");
		kfree(cb_arg);
	}

	return rc;
}

static int
efct_hw_cb_port_control(struct efct_hw *hw, int status, u8 *mqe,
			void  *arg)
{
	return 0;
}

int
efct_hw_port_control(struct efct_hw *hw, enum efct_hw_port ctrl,
		     uintptr_t value,
		     void (*cb)(int status, uintptr_t value, void *arg),
		     void *arg)
{
	int rc = -EIO;
	u8 link[SLI4_BMBX_SIZE];
	u32 speed = 0;
	u8 reset_alpa = 0;

	switch (ctrl) {
	case EFCT_HW_PORT_INIT:
		if (!sli_cmd_config_link(&hw->sli, link))
			rc = efct_hw_command(hw, link, EFCT_CMD_NOWAIT,
					     efct_hw_cb_port_control, NULL);

		if (rc != 0) {
			efc_log_err(hw->os, "CONFIG_LINK failed\n");
			break;
		}
		speed = hw->config.speed;
		reset_alpa = (u8)(value & 0xff);

		rc = -EIO;
		if (!sli_cmd_init_link(&hw->sli, link, speed, reset_alpa))
			rc = efct_hw_command(hw, link, EFCT_CMD_NOWAIT,
					     efct_hw_cb_port_control, NULL);
		/* Free buffer on error, since no callback is coming */
		if (rc)
			efc_log_err(hw->os, "INIT_LINK failed\n");
		break;

	case EFCT_HW_PORT_SHUTDOWN:
		if (!sli_cmd_down_link(&hw->sli, link))
			rc = efct_hw_command(hw, link, EFCT_CMD_NOWAIT,
					     efct_hw_cb_port_control, NULL);
		/* Free buffer on error, since no callback is coming */
		if (rc)
			efc_log_err(hw->os, "DOWN_LINK failed\n");
		break;

	default:
		efc_log_debug(hw->os, "unhandled control %#x\n", ctrl);
		break;
	}

	return rc;
}

void
efct_hw_teardown(struct efct_hw *hw)
{
	u32 i = 0;
	u32 destroy_queues;
	u32 free_memory;
	struct efc_dma *dma;
	struct efct *efct = hw->os;

	destroy_queues = (hw->state == EFCT_HW_STATE_ACTIVE);
	free_memory = (hw->state != EFCT_HW_STATE_UNINITIALIZED);

	/* Cancel Sliport Healthcheck */
	if (hw->sliport_healthcheck) {
		hw->sliport_healthcheck = 0;
		efct_hw_config_sli_port_health_check(hw, 0, 0);
	}

	if (hw->state != EFCT_HW_STATE_QUEUES_ALLOCATED) {
		hw->state = EFCT_HW_STATE_TEARDOWN_IN_PROGRESS;

		efct_hw_flush(hw);

		if (list_empty(&hw->cmd_head))
			efc_log_debug(hw->os,
				      "All commands completed on MQ queue\n");
		else
			efc_log_debug(hw->os,
				      "Some cmds still pending on MQ queue\n");

		/* Cancel any remaining commands */
		efct_hw_command_cancel(hw);
	} else {
		hw->state = EFCT_HW_STATE_TEARDOWN_IN_PROGRESS;
	}

	dma_free_coherent(&efct->pci->dev,
			  hw->rnode_mem.size, hw->rnode_mem.virt,
			  hw->rnode_mem.phys);
	memset(&hw->rnode_mem, 0, sizeof(struct efc_dma));

	if (hw->io) {
		for (i = 0; i < hw->config.n_io; i++) {
			if (hw->io[i] && hw->io[i]->sgl &&
			    hw->io[i]->sgl->virt) {
				dma_free_coherent(&efct->pci->dev,
						  hw->io[i]->sgl->size,
						  hw->io[i]->sgl->virt,
						  hw->io[i]->sgl->phys);
			}
			kfree(hw->io[i]);
			hw->io[i] = NULL;
		}
		kfree(hw->io);
		hw->io = NULL;
		kfree(hw->wqe_buffs);
		hw->wqe_buffs = NULL;
	}

	dma = &hw->xfer_rdy;
	dma_free_coherent(&efct->pci->dev,
			  dma->size, dma->virt, dma->phys);
	memset(dma, 0, sizeof(struct efc_dma));

	dma = &hw->loop_map;
	dma_free_coherent(&efct->pci->dev,
			  dma->size, dma->virt, dma->phys);
	memset(dma, 0, sizeof(struct efc_dma));

	for (i = 0; i < hw->wq_count; i++)
		sli_queue_free(&hw->sli, &hw->wq[i], destroy_queues,
			       free_memory);

	for (i = 0; i < hw->rq_count; i++)
		sli_queue_free(&hw->sli, &hw->rq[i], destroy_queues,
			       free_memory);

	for (i = 0; i < hw->mq_count; i++)
		sli_queue_free(&hw->sli, &hw->mq[i], destroy_queues,
			       free_memory);

	for (i = 0; i < hw->cq_count; i++)
		sli_queue_free(&hw->sli, &hw->cq[i], destroy_queues,
			       free_memory);

	for (i = 0; i < hw->eq_count; i++)
		sli_queue_free(&hw->sli, &hw->eq[i], destroy_queues,
			       free_memory);

	/* Free rq buffers */
	efct_hw_rx_free(hw);

	efct_hw_queue_teardown(hw);

	kfree(hw->wq_cpu_array);

	sli_teardown(&hw->sli);

	/* record the fact that the queues are non-functional */
	hw->state = EFCT_HW_STATE_UNINITIALIZED;

	/* free sequence free pool */
	kfree(hw->seq_pool);
	hw->seq_pool = NULL;

	/* free hw_wq_callback pool */
	efct_hw_reqtag_pool_free(hw);

	mempool_destroy(hw->cmd_ctx_pool);
	mempool_destroy(hw->mbox_rqst_pool);

	/* Mark HW setup as not having been called */
	hw->hw_setup_called = false;
}

static int
efct_hw_sli_reset(struct efct_hw *hw, enum efct_hw_reset reset,
		  enum efct_hw_state prev_state)
{
	int rc = 0;

	switch (reset) {
	case EFCT_HW_RESET_FUNCTION:
		efc_log_debug(hw->os, "issuing function level reset\n");
		if (sli_reset(&hw->sli)) {
			efc_log_err(hw->os, "sli_reset failed\n");
			rc = -EIO;
		}
		break;
	case EFCT_HW_RESET_FIRMWARE:
		efc_log_debug(hw->os, "issuing firmware reset\n");
		if (sli_fw_reset(&hw->sli)) {
			efc_log_err(hw->os, "sli_soft_reset failed\n");
			rc = -EIO;
		}
		/*
		 * Because the FW reset leaves the FW in a non-running state,
		 * follow that with a regular reset.
		 */
		efc_log_debug(hw->os, "issuing function level reset\n");
		if (sli_reset(&hw->sli)) {
			efc_log_err(hw->os, "sli_reset failed\n");
			rc = -EIO;
		}
		break;
	default:
		efc_log_err(hw->os, "unknown type - no reset performed\n");
		hw->state = prev_state;
		rc = -EINVAL;
		break;
	}

	return rc;
}

int
efct_hw_reset(struct efct_hw *hw, enum efct_hw_reset reset)
{
	int rc = 0;
	enum efct_hw_state prev_state = hw->state;

	if (hw->state != EFCT_HW_STATE_ACTIVE)
		efc_log_debug(hw->os,
			      "HW state %d is not active\n", hw->state);

	hw->state = EFCT_HW_STATE_RESET_IN_PROGRESS;

	/*
	 * If the prev_state is already reset/teardown in progress,
	 * don't continue further
	 */
	if (prev_state == EFCT_HW_STATE_RESET_IN_PROGRESS ||
	    prev_state == EFCT_HW_STATE_TEARDOWN_IN_PROGRESS)
		return efct_hw_sli_reset(hw, reset, prev_state);

	if (prev_state != EFCT_HW_STATE_UNINITIALIZED) {
		efct_hw_flush(hw);

		if (list_empty(&hw->cmd_head))
			efc_log_debug(hw->os,
				      "All commands completed on MQ queue\n");
		else
			efc_log_err(hw->os,
				    "Some commands still pending on MQ queue\n");
	}

	/* Reset the chip */
	rc = efct_hw_sli_reset(hw, reset, prev_state);
	if (rc == -EINVAL)
		return -EIO;

	return rc;
}
