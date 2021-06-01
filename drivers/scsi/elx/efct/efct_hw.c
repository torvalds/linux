// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_hw.h"
#include "efct_unsol.h"

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

static u8 efct_hw_iotype_is_originator(u16 io_type)
{
	switch (io_type) {
	case EFCT_HW_FC_CT:
	case EFCT_HW_ELS_REQ:
		return 0;
	default:
		return -EIO;
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
		if ((!efct_hw_iotype_is_originator(io->type)) &&
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
		efc_log_err(hw->os, "efct_hw_reqtag_init failed %d\n", rc);
		return rc;
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
			      !ctx ? U32_MAX :
			      (!ctx->buf ? U32_MAX : *((u32 *)ctx->buf)));
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
